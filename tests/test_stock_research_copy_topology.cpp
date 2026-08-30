#include <hlclient/platform/windows/stock_research_copy.hpp>

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

struct LocalMountPointReparseBuffer final {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    USHORT substitute_name_offset;
    USHORT substitute_name_length;
    USHORT print_name_offset;
    USHORT print_name_length;
    WCHAR path_buffer[1];
};

inline constexpr std::size_t kLocalReparseHeaderBytes =
    offsetof(LocalMountPointReparseBuffer, substitute_name_offset);

struct LocalGuidReparseBuffer final {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    GUID reparse_guid;
    UCHAR data_buffer[1];
};

inline constexpr ULONG kFixtureUnsupportedReparseTag = 0x00000042UL;

[[nodiscard]] fs::path unique_fixture_root()
{
    // Keep hostile topology fixtures below the test binary.  The Codex/CI
    // restricted token can traverse the user profile temp path but cannot
    // acquire an identity handle for every profile ancestor; this suite is
    // specifically exercising a fully pinned destination chain.
    std::array<wchar_t, 32'768U> module{};
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    std::error_code error;
    const auto canonical = fs::canonical(
        fs::path{std::wstring_view{module.data(), size}}.parent_path(), error);
    REQUIRE_FALSE(error);
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    return canonical /
        (L"hlclient-stock-research-copy-test-" +
         std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(nonce));
}

[[nodiscard]] fs::path module_directory()
{
    std::array<wchar_t, 32'768U> module{};
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    return fs::path{std::wstring_view{module.data(), size}}.parent_path();
}

void write_file(const fs::path& path, const std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool try_write_alternate_stream(
    const fs::path& base,
    const std::wstring_view stream_name,
    const std::string_view bytes,
    const DWORD share_access = 0U)
{
    fs::path stream_path;
    try {
        stream_path = fs::path{
            base.native() + L":" + std::wstring{stream_name}};
    } catch (...) {
        return false;
    }
    std::error_code type_error;
    const bool directory = fs::is_directory(base, type_error);
    if (type_error) return false;
    const HANDLE handle = ::CreateFileW(
        stream_path.c_str(), GENERIC_WRITE, share_access, nullptr, CREATE_NEW,
        directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0U;
    const bool valid_size =
        bytes.size() <= static_cast<std::size_t>(MAXDWORD);
    const BOOL write_ok = valid_size
        ? ::WriteFile(
              handle, bytes.data(), static_cast<DWORD>(bytes.size()),
              &written, nullptr)
        : FALSE;
    const BOOL flush_ok = write_ok != FALSE ? ::FlushFileBuffers(handle) : FALSE;
    const BOOL close_ok = ::CloseHandle(handle);
    return write_ok != FALSE &&
           written == static_cast<DWORD>(bytes.size()) &&
           flush_ok != FALSE && close_ok != FALSE;
}

void populate_stock_root(const fs::path& root)
{
    fs::create_directories(root);
    write_file(root / L"hl.exe", "fake client");
    write_file(root / L"hlds.exe", "fake server");
    write_file(root / L"valve" / L"maps" / L"boot_camp.bsp", "map");
}

[[nodiscard]] bool contains_category(
    const windows::StockResearchTopologySummary& summary,
    const windows::StockResearchTopologyCategory category)
{
    return std::ranges::find(summary.categories, category) !=
           summary.categories.end();
}

[[nodiscard]] bool create_mount_point_reparse(
    const fs::path& link,
    const std::wstring_view substitute,
    const std::wstring_view print_name)
{
    std::error_code error;
    fs::create_directories(link, error);
    if (error) return false;
    HANDLE handle = ::CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const auto substitute_bytes = static_cast<USHORT>(
        substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<USHORT>(print_name.size() * sizeof(wchar_t));
    const auto path_bytes = static_cast<std::size_t>(substitute_bytes) +
        sizeof(wchar_t) + print_bytes + sizeof(wchar_t);
    std::vector<std::byte> storage(
        kLocalReparseHeaderBytes + 8U + path_bytes);
    auto* data =
        reinterpret_cast<LocalMountPointReparseBuffer*>(storage.data());
    data->reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->reserved = 0U;
    data->substitute_name_offset = 0U;
    data->substitute_name_length = substitute_bytes;
    data->print_name_offset =
        static_cast<USHORT>(substitute_bytes + sizeof(wchar_t));
    data->print_name_length = print_bytes;
    std::copy(
        substitute.begin(), substitute.end(),
        data->path_buffer);
    auto* print = reinterpret_cast<wchar_t*>(
        reinterpret_cast<std::byte*>(data->path_buffer) +
        data->print_name_offset);
    std::copy(print_name.begin(), print_name.end(), print);
    data->reparse_data_length = static_cast<USHORT>(8U + path_bytes);
    DWORD returned = 0U;
    const BOOL ok = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data,
        static_cast<DWORD>(
            kLocalReparseHeaderBytes + data->reparse_data_length),
        nullptr, 0U,
        &returned, nullptr);
    static_cast<void>(::CloseHandle(handle));
    return ok != FALSE;
}

[[nodiscard]] bool create_junction(
    const fs::path& link, const fs::path& target)
{
    const std::wstring substitute = L"\\??\\" + target.native();
    return create_mount_point_reparse(link, substitute, target.native());
}

[[nodiscard]] bool create_broken_volume_mount_point(const fs::path& link)
{
    constexpr std::wstring_view target =
        LR"(\??\Volume{00000000-0000-0000-0000-000000000001}\)";
    return create_mount_point_reparse(link, target, target);
}

[[nodiscard]] bool create_unsupported_reparse_point(const fs::path& link)
{
    std::error_code error;
    fs::create_directories(link, error);
    if (error) return false;
    HANDLE handle = ::CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    std::array<std::byte, sizeof(LocalGuidReparseBuffer) + 8U> storage{};
    auto* const data =
        reinterpret_cast<LocalGuidReparseBuffer*>(storage.data());
    data->reparse_tag = kFixtureUnsupportedReparseTag;
    data->reparse_data_length =
        static_cast<USHORT>(sizeof(GUID) + 8U);
    data->reserved = 0U;
    data->reparse_guid = GUID{
        0x768f8e39UL, 0x9c6aU, 0x4c73U,
        {0xa8U, 0x42U, 0x7cU, 0x21U, 0x64U, 0xf6U, 0x01U, 0x17U}};
    std::ranges::fill(data->data_buffer, data->data_buffer + 8U, 0x5aU);
    DWORD returned = 0U;
    const BOOL ok = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data,
        static_cast<DWORD>(
            offsetof(LocalGuidReparseBuffer, reparse_guid) +
            data->reparse_data_length),
        nullptr, 0U, &returned, nullptr);
    static_cast<void>(::CloseHandle(handle));
    return ok != FALSE;
}

[[nodiscard]] bool create_directory_symlink_fixture(
    const fs::path& link, const fs::path& target)
{
    return ::CreateSymbolicLinkW(
               link.c_str(), target.c_str(),
               SYMBOLIC_LINK_FLAG_DIRECTORY |
                   SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
}

struct ScopedSubstDrive final {
    wchar_t letter{L'\0'};
    std::wstring native_target;

    ScopedSubstDrive() = default;
    ScopedSubstDrive(const ScopedSubstDrive&) = delete;
    ScopedSubstDrive& operator=(const ScopedSubstDrive&) = delete;
    ~ScopedSubstDrive()
    {
        if (letter == L'\0') return;
        const std::array<wchar_t, 3U> drive{letter, L':', L'\0'};
        static_cast<void>(::DefineDosDeviceW(
            DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE |
                DDD_RAW_TARGET_PATH,
            drive.data(), native_target.c_str()));
    }

    [[nodiscard]] fs::path root() const
    {
        return fs::path{std::wstring{letter} + L":\\"};
    }
};

[[nodiscard]] bool create_subst_drive(
    const fs::path& target, ScopedSubstDrive& drive)
{
    for (wchar_t candidate = L'Z'; candidate >= L'R'; --candidate) {
        const std::array<wchar_t, 3U> name{candidate, L':', L'\0'};
        std::array<wchar_t, 4U> existing{};
        if (::QueryDosDeviceW(
                name.data(), existing.data(),
                static_cast<DWORD>(existing.size())) != 0U) {
            continue;
        }
        const std::wstring native = L"\\??\\" + target.native();
        if (::DefineDosDeviceW(
                DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM,
                name.data(), native.c_str()) != FALSE) {
            drive.letter = candidate;
            drive.native_target = native;
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::uint32_t link_count(const fs::path& path)
{
    HANDLE handle = ::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    FILE_STANDARD_INFO standard{};
    const BOOL ok = ::GetFileInformationByHandleEx(
        handle, FileStandardInfo, &standard, sizeof(standard));
    static_cast<void>(::CloseHandle(handle));
    REQUIRE(ok != FALSE);
    return standard.NumberOfLinks;
}

struct Fixture final {
    Fixture() : root{unique_fixture_root()} { fs::create_directories(root); }
    ~Fixture()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path root;
};

struct OwnedTree final {
    explicit OwnedTree(fs::path value) : root{std::move(value)}
    {
        fs::create_directories(root);
    }
    ~OwnedTree()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path root;
};

struct MutationContext final {
    fs::path path;
    bool mutated{false};
};

struct SourceRootReplacementContext final {
    fs::path source;
    fs::path displaced;
    bool replaced{false};
};

struct DestinationReplacementContext final {
    fs::path destination;
    fs::path displaced;
    bool attempted{false};
    bool blocked{false};
    bool replaced{false};
};

struct StagingReplacementContext final {
    fs::path parent;
    fs::path staging;
    fs::path displaced;
    bool attempted{false};
    bool blocked{false};
    bool replaced{false};
};

struct TransitionReplacementContext final {
    fs::path parent;
    fs::path staging;
    fs::path displaced;
    bool attempted{false};
    bool replaced{false};
};

struct TransitionSharingContext final {
    ~TransitionSharingContext()
    {
        if (releaser.joinable()) releaser.join();
    }
    fs::path parent;
    bool attempted{false};
    bool opened{false};
    std::atomic<bool> released{false};
    std::thread releaser;
};

struct PublishedMutationContext final {
    fs::path path;
    bool mutated{false};
};

struct LatePublishedMutationContext final {
    fs::path path;
    bool attempted{false};
    bool mutated{false};
};

struct PublishedAdsContext final {
    fs::path path;
    std::wstring stream_name;
    bool attempted{false};
    bool created{false};
};

struct PublishedHardlinkContext final {
    fs::path published_file;
    fs::path outside_link;
    bool attempted{false};
    bool created{false};
};

struct PublishedChildJunctionContext final {
    fs::path destination_parent;
    fs::path parent_displaced;
    fs::path destination;
    fs::path child_displaced;
    fs::path external;
    bool attempted{false};
    bool child_blocked{false};
    bool parent_blocked{false};
    bool replaced{false};
};

struct PublishedUnknownChildContext final {
    fs::path path;
    bool attempted{false};
    bool created{false};
};

struct CommitWindowMutationContext final {
    fs::path path;
    bool inventory_matched{false};
    bool guards_acquired{false};
    bool attempted{false};
    bool created{false};
};

struct SourceCommitMutationContext final {
    fs::path path;
    bool attempted{false};
    bool blocked{false};
};

struct MarkerValidationMutationContext final {
    fs::path path;
    bool attempted{false};
    bool blocked{false};
};

struct PublishedOplockBreakContext final {
    HANDLE guards_acquired_event{nullptr};
    HANDLE resume_verification_event{nullptr};
    HANDLE hostile_open_returned_event{nullptr};
    std::atomic<bool> hook_attempted{false};
    std::atomic<DWORD> hook_wait_result{WAIT_FAILED};
    std::atomic<bool> release_hook_attempted{false};
    std::atomic<DWORD> release_hook_wait_result{WAIT_FAILED};
};

void mutate_source(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<MutationContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::source_file_opened ||
        context.mutated) {
        return;
    }
    std::ofstream output{context.path, std::ios::binary | std::ios::app};
    output << 'x';
    output.flush();
    context.mutated = true;
}

void replace_source_root_identity(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<SourceRootReplacementContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     before_source_reinventory ||
        context.replaced) {
        return;
    }
    std::error_code error;
    fs::rename(context.source, context.displaced, error);
    REQUIRE_FALSE(error);
    populate_stock_root(context.source);
    write_file(
        context.source / L"replacement-sentinel.bin", "must survive");
    context.replaced = true;
}

void replace_published_destination(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<DestinationReplacementContext*>(opaque);
    if (phase !=
            windows::StockResearchCopyProgressPhase::after_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    std::error_code error;
    fs::rename(context.destination, context.displaced, error);
    if (error) {
        context.blocked = true;
        return;
    }
    fs::create_directories(context.destination, error);
    REQUIRE_FALSE(error);
    write_file(context.destination / L"unrelated.txt", "must survive");
    context.replaced = true;
}

void replace_staging_directory(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<StagingReplacementContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     after_staging_identity_acquired ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    for (const auto& entry : fs::directory_iterator{context.parent}) {
        const auto name = entry.path().filename().native();
        if (!name.starts_with(L".hlclient-stock-research-copy-")) continue;
        REQUIRE(context.staging.empty());
        context.staging = entry.path();
    }
    REQUIRE_FALSE(context.staging.empty());
    std::error_code error;
    fs::rename(context.staging, context.displaced, error);
    if (error) {
        context.blocked = true;
        return;
    }
    fs::create_directories(context.staging, error);
    REQUIRE_FALSE(error);
    write_file(context.staging / L"unrelated.txt", "must survive");
    context.replaced = true;
}

void replace_staging_during_publish_transition(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<TransitionReplacementContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     before_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    for (const auto& entry : fs::directory_iterator{context.parent}) {
        const auto name = entry.path().filename().native();
        if (!name.starts_with(L".hlclient-stock-research-copy-")) continue;
        if (!context.staging.empty()) return;
        context.staging = entry.path();
    }
    if (context.staging.empty()) return;
    std::error_code error;
    fs::rename(context.staging, context.displaced, error);
    if (error) return;
    fs::create_directories(context.staging, error);
    if (error) return;
    write_file(context.staging / L"unrelated.txt", "must survive");
    context.replaced = true;
}

void retain_staging_descendant_during_publish_transition(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<TransitionSharingContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     before_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    fs::path staging;
    for (const auto& entry : fs::directory_iterator{context.parent}) {
        const auto name = entry.path().filename().native();
        if (!name.starts_with(L".hlclient-stock-research-copy-")) continue;
        if (!staging.empty()) return;
        staging = entry.path();
    }
    if (staging.empty()) return;
    const HANDLE blocker = ::CreateFileW(
        (staging / L"hl.exe").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (blocker == INVALID_HANDLE_VALUE) return;
    context.opened = true;
    context.releaser = std::thread{[&context, blocker] {
        ::Sleep(150U);
        static_cast<void>(::CloseHandle(blocker));
        context.released.store(true);
    }};
}

void mutate_published_file(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedMutationContext*>(opaque);
    if (phase !=
            windows::StockResearchCopyProgressPhase::after_destination_publish ||
        context.mutated) {
        return;
    }
    std::ofstream output{context.path, std::ios::binary | std::ios::app};
    output << 'x';
    output.flush();
    context.mutated = output.good();
}

void add_published_ads(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedAdsContext*>(opaque);
    if (phase !=
            windows::StockResearchCopyProgressPhase::after_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    context.created = try_write_alternate_stream(
        context.path, context.stream_name, "private");
}

void add_published_hardlink(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedHardlinkContext*>(opaque);
    if (phase !=
            windows::StockResearchCopyProgressPhase::after_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    context.created = ::CreateHardLinkW(
                          context.outside_link.c_str(),
                          context.published_file.c_str(), nullptr) != FALSE;
}

void replace_published_child_with_junction(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedChildJunctionContext*>(opaque);
    if (phase !=
            windows::StockResearchCopyProgressPhase::after_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    const auto child = context.destination / L"valve";
    std::error_code error;
    fs::rename(child, context.child_displaced, error);
    if (error) {
        context.child_blocked = true;
    } else {
        if (create_junction(child, context.external)) {
            context.replaced = true;
            return;
        }
        fs::remove(child, error);
        error.clear();
        fs::rename(context.child_displaced, child, error);
        return;
    }

    error.clear();
    fs::rename(
        context.destination_parent, context.parent_displaced, error);
    if (error) {
        context.parent_blocked = true;
        return;
    }
    if (create_junction(context.destination_parent, context.external)) {
        context.replaced = true;
        return;
    }
    fs::remove(context.destination_parent, error);
    error.clear();
    fs::rename(
        context.parent_displaced, context.destination_parent, error);
}

void add_unknown_published_child(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedUnknownChildContext*>(opaque);
    if (phase !=
            windows::StockResearchCopyProgressPhase::after_destination_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    write_file(context.path, "unowned");
    context.created = fs::is_regular_file(context.path);
}

void add_late_unknown_published_child(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedUnknownChildContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     during_published_inventory ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    std::ofstream output{context.path, std::ios::binary | std::ios::trunc};
    output << "late-unowned";
    output.flush();
    context.created = output.good() && fs::is_regular_file(context.path);
}

void add_commit_window_unknown_child(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<CommitWindowMutationContext*>(opaque);
    if (phase == windows::StockResearchCopyProgressPhase::
                     during_published_inventory) {
        context.inventory_matched = true;
        return;
    }
    if (phase == windows::StockResearchCopyProgressPhase::
                     after_published_file_guards_acquired) {
        context.guards_acquired = true;
        return;
    }
    if (phase != windows::StockResearchCopyProgressPhase::
                     before_commit_marker_publish || context.attempted) {
        return;
    }
    context.attempted = true;
    std::ofstream output{context.path, std::ios::binary | std::ios::trunc};
    output << "commit-window-unowned";
    output.flush();
    context.created = output.good() && fs::is_regular_file(context.path);
}

void attempt_source_commit_window_mutation(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<SourceCommitMutationContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     before_commit_marker_publish ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    const HANDLE handle = ::CreateFileW(
        context.path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    context.blocked = handle == INVALID_HANDLE_VALUE;
    if (handle != INVALID_HANDLE_VALUE) {
        static_cast<void>(::CloseHandle(handle));
    }
}

void attempt_marker_validation_mutation(
    const windows::StockResearchMarkerValidationPhase phase,
    void* const opaque)
{
    auto& context = *static_cast<MarkerValidationMutationContext*>(opaque);
    if (phase != windows::StockResearchMarkerValidationPhase::
                     after_handle_acquired ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    const HANDLE handle = ::CreateFileW(
        context.path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    context.blocked = handle == INVALID_HANDLE_VALUE;
    if (handle != INVALID_HANDLE_VALUE) {
        static_cast<void>(::CloseHandle(handle));
    }
}

void add_late_published_ads(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedAdsContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     during_published_inventory ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    context.created = try_write_alternate_stream(
        context.path, context.stream_name, "late-private",
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
}

void mutate_late_published_file(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<LatePublishedMutationContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     during_published_inventory ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    std::ofstream output{context.path, std::ios::binary | std::ios::app};
    output << 'x';
    output.flush();
    context.mutated = output.good();
}

void add_late_published_hardlink(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedHardlinkContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     during_published_inventory ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    context.created = ::CreateHardLinkW(
                          context.outside_link.c_str(),
                          context.published_file.c_str(), nullptr) != FALSE;
}

void pause_with_published_file_guards(
    const windows::StockResearchCopyProgressPhase phase,
    const std::size_t,
    void* const opaque)
{
    auto& context = *static_cast<PublishedOplockBreakContext*>(opaque);
    if (phase == windows::StockResearchCopyProgressPhase::
                     after_published_file_guards_acquired) {
        if (context.hook_attempted.exchange(true)) return;
        if (::SetEvent(context.guards_acquired_event) == FALSE) {
            context.hook_wait_result.store(WAIT_FAILED);
            return;
        }
        context.hook_wait_result.store(::WaitForSingleObject(
            context.resume_verification_event, 10'000U));
        return;
    }
    if (phase != windows::StockResearchCopyProgressPhase::
                     after_published_file_guards_released_on_failure ||
        context.release_hook_attempted.exchange(true)) {
        return;
    }
    context.release_hook_wait_result.store(::WaitForSingleObject(
        context.hostile_open_returned_event, 10'000U));
}

} // namespace

TEST_CASE(
    "Stock research copy materializes an ordinary tree transactionally",
    "[windows][stock-runtime][research-copy][topology]")
{
    Fixture fixture;
    const auto source = fixture.root / L"ordinary-source";
    const auto destination = fixture.root / L"ordinary-destination";
    populate_stock_root(source);

    const auto topology = windows::inspect_stock_research_topology(source);
    INFO("topology code=" << windows::to_string(topology.code)
                           << " native=" << topology.native_error);
    REQUIRE(topology);
    CHECK(topology.summary->safe_to_materialize);
    CHECK(contains_category(
        *topology.summary,
        windows::StockResearchTopologyCategory::ordinary_tree));

    const auto copied =
        windows::materialize_stock_research_copy(source, destination);
    INFO("copy code=" << windows::to_string(copied.code)
                       << " native=" << copied.native_error
                       << " materialization="
                       << copied.materialization.has_value());
    REQUIRE(copied);
    CHECK(copied.materialization->preparation_status ==
          "exact-materialized-copy-verified");
    CHECK(copied.materialization->destination_reparse_count == 0U);
    CHECK(copied.materialization->destination_hardlink_count == 0U);
    CHECK(copied.materialization->destination_alternate_data_stream_count ==
          0U);
    CHECK(fs::is_regular_file(destination / L"hl.exe"));
    CHECK(fs::is_regular_file(destination / L"hlds.exe"));
    CHECK(fs::is_regular_file(
        destination / L".hlclient-research-preparation.json"));
    CHECK(fs::is_regular_file(
        destination / L".hlclient-research-isolated"));
    CHECK(fs::is_regular_file(
        destination / L".hlclient-research-pending"));

    std::ifstream manifest{
        destination / L".hlclient-research-preparation.json"};
    const std::string json{
        std::istreambuf_iterator<char>{manifest},
        std::istreambuf_iterator<char>{}};
    CHECK(json.find("hlclient.stock-runtime-research-preparation.v2") !=
          std::string::npos);
    CHECK(json.find(source.string()) == std::string::npos);
    CHECK(json.find(destination.string()) == std::string::npos);
}

TEST_CASE(
    "Stock research isolation marker validation is retained and bounded",
    "[windows][stock-runtime][research-copy][marker]")
{
    Fixture fixture;
    const auto marker = fixture.root / L".hlclient-research-isolated";
    constexpr std::string_view exact =
        "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1";
    write_file(marker, exact);

    MarkerValidationMutationContext context{marker};
    windows::StockResearchMarkerValidationOptions options;
    options.progress_hook = &attempt_marker_validation_mutation;
    options.progress_context = &context;
    CHECK(windows::stock_research_isolation_marker_exact(
        fixture.root, options));
    CHECK(context.attempted);
    CHECK(context.blocked);
    CHECK(read_file(marker) == exact);

    const HANDLE wrapper_guard = ::CreateFileW(
        marker.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    REQUIRE(wrapper_guard != INVALID_HANDLE_VALUE);
    CHECK(windows::stock_research_isolation_marker_exact(fixture.root));
    REQUIRE(::CloseHandle(wrapper_guard) != FALSE);

    write_file(marker, std::string(1'024U, 'x'));
    CHECK_FALSE(windows::stock_research_isolation_marker_exact(fixture.root));

    write_file(marker, exact);
    const auto outside = fixture.root / L"outside-marker-link";
    REQUIRE(::CreateHardLinkW(outside.c_str(), marker.c_str(), nullptr) != FALSE);
    CHECK_FALSE(windows::stock_research_isolation_marker_exact(fixture.root));
    REQUIRE(fs::remove(outside));

    if (try_write_alternate_stream(marker, L"private", "x")) {
        CHECK_FALSE(
            windows::stock_research_isolation_marker_exact(fixture.root));
    }
}

TEST_CASE(
    "Stock research copy resolves safe root and ancestor junctions",
    "[windows][stock-runtime][research-copy][topology][junction]")
{
    Fixture fixture;
    const auto physical_parent = fixture.root / L"physical";
    const auto physical_source = physical_parent / L"Half-Life";
    populate_stock_root(physical_source);

    const auto root_alias = fixture.root / L"root-alias";
    REQUIRE(create_junction(root_alias, physical_source));
    auto topology = windows::inspect_stock_research_topology(root_alias);
    REQUIRE(topology);
    CHECK(topology.summary->safe_to_materialize);
    CHECK(topology.summary->root_reparse);
    CHECK(contains_category(
        *topology.summary,
        windows::StockResearchTopologyCategory::source_root_reparse));
    REQUIRE(windows::materialize_stock_research_copy(
        root_alias, fixture.root / L"root-copy"));

    const auto ancestor_alias = fixture.root / L"ancestor-alias";
    REQUIRE(create_junction(ancestor_alias, physical_parent));
    topology = windows::inspect_stock_research_topology(
        ancestor_alias / L"Half-Life");
    REQUIRE(topology);
    CHECK(topology.summary->safe_to_materialize);
    CHECK_FALSE(topology.summary->root_reparse);
    CHECK(contains_category(
        *topology.summary,
        windows::StockResearchTopologyCategory::
            source_path_ancestor_reparse));
}

TEST_CASE(
    "Contained directory junction becomes an ordinary destination directory",
    "[windows][stock-runtime][research-copy][topology][junction]")
{
    Fixture fixture;
    const auto source = fixture.root / L"source";
    populate_stock_root(source);
    write_file(source / L"physical" / L"payload.bin", "payload");
    REQUIRE(create_junction(
        source / L"valve" / L"linked", source / L"physical"));

    const auto topology = windows::inspect_stock_research_topology(source);
    REQUIRE(topology);
    CHECK(topology.summary->safe_to_materialize);
    CHECK(topology.summary->contained_target_count == 1U);
    CHECK(contains_category(
        *topology.summary,
        windows::StockResearchTopologyCategory::
            source_internal_directory_junction));

    const auto destination = fixture.root / L"destination";
    const auto copied =
        windows::materialize_stock_research_copy(source, destination);
    REQUIRE(copied);
    CHECK(copied.materialization->materialized_link_count == 1U);
    CHECK(fs::is_regular_file(
        destination / L"valve" / L"linked" / L"payload.bin"));
    CHECK((fs::status(destination / L"valve" / L"linked").permissions() &
           fs::perms::owner_read) != fs::perms::none);
    CHECK((::GetFileAttributesW(
               (destination / L"valve" / L"linked").c_str()) &
           FILE_ATTRIBUTE_REPARSE_POINT) == 0U);
}

TEST_CASE(
    "Contained and escaped directory symlinks are classified by physical target",
    "[windows][stock-runtime][research-copy][topology][symlink]")
{
    SECTION("contained directory symlink") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        write_file(source / L"physical" / L"payload.bin", "payload");
        if (!create_directory_symlink_fixture(
                source / L"valve" / L"linked", source / L"physical")) {
            SKIP("Windows directory-symlink capability is unavailable");
        }

        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK(topology.summary->safe_to_materialize);
        CHECK(topology.summary->contained_target_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_internal_directory_symlink));

        const auto destination = fixture.root / L"destination";
        const auto copied =
            windows::materialize_stock_research_copy(source, destination);
        REQUIRE(copied);
        CHECK(fs::is_regular_file(
            destination / L"valve" / L"linked" / L"payload.bin"));
        CHECK((::GetFileAttributesW(
                   (destination / L"valve" / L"linked").c_str()) &
               FILE_ATTRIBUTE_REPARSE_POINT) == 0U);
    }

    SECTION("escaped directory symlink") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto external = fixture.root / L"external";
        populate_stock_root(source);
        write_file(external / L"outside.bin", "outside");
        if (!create_directory_symlink_fixture(source / L"escape", external)) {
            SKIP("Windows directory-symlink capability is unavailable");
        }

        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->escaped_target_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_link_target_outside_root));
    }
}

TEST_CASE(
    "Mount points unsupported tags and excessive link depth fail closed",
    "[windows][stock-runtime][research-copy][topology][reparse][reject]")
{
    SECTION("internal volume mount point") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        if (!create_broken_volume_mount_point(source / L"mount")) {
            SKIP("NTFS mount-point reparse fixture capability is unavailable");
        }

        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_internal_mount_point));
    }

    SECTION("unsupported reparse tag") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        if (!create_unsupported_reparse_point(source / L"unsupported")) {
            SKIP("Custom reparse-tag fixture capability is unavailable");
        }

        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_unsupported_reparse_tag));
    }

    SECTION("maximum reparse depth") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        write_file(source / L"target-b" / L"payload.bin", "payload");
        fs::create_directories(source / L"target-a");
        REQUIRE(create_junction(
            source / L"target-a" / L"nested", source / L"target-b"));
        REQUIRE(create_junction(
            source / L"alias", source / L"target-a"));
        windows::StockResearchCopyLimits limits;
        limits.maximum_reparse_depth = 1U;

        const auto topology =
            windows::inspect_stock_research_topology(source, limits);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_link_depth_exceeded));
    }
}

TEST_CASE(
    "Escaped junction and link cycles are classified without traversal",
    "[windows][stock-runtime][research-copy][topology][reject]")
{
    SECTION("escaped target") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto external = fixture.root / L"external";
        populate_stock_root(source);
        write_file(external / L"outside.bin", "outside");
        REQUIRE(create_junction(source / L"escape", external));
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->escaped_target_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_link_target_outside_root));
        const auto destination = fixture.root / L"destination";
        const auto copied =
            windows::materialize_stock_research_copy(source, destination);
        CHECK_FALSE(copied);
        CHECK(copied.code ==
              windows::StockResearchCopyErrorCode::source_topology_unsafe);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("cycle") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        fs::create_directories(source / L"cycle-parent");
        REQUIRE(create_junction(
            source / L"cycle-parent" / L"back", source));
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::source_link_cycle));
    }

    SECTION("cross-volume target") {
        Fixture fixture;
        const auto other_parent = module_directory();
        if (fixture.root.root_name() == other_parent.root_name()) {
            SKIP("A second writable local volume is unavailable");
        }
        OwnedTree external{
            other_parent /
            (L"hlclient-cross-volume-target-" +
             std::to_wstring(::GetCurrentProcessId()) + L"-" +
             std::to_wstring(
                 std::chrono::steady_clock::now()
                     .time_since_epoch()
                     .count()))};
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        write_file(external.root / L"outside.bin", "outside");
        if (!create_junction(source / L"cross-volume", external.root)) {
            SKIP("Cross-volume junction fixture capability is unavailable");
        }
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->escaped_target_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_link_target_outside_root));
    }
}

TEST_CASE(
    "Hardlinked source files are materialized as independent files",
    "[windows][stock-runtime][research-copy][topology][hardlink]")
{
    Fixture fixture;
    const auto source = fixture.root / L"source";
    populate_stock_root(source);
    write_file(source / L"shared.bin", "shared bytes");
    REQUIRE(::CreateHardLinkW(
                (source / L"shared-alias.bin").c_str(),
                (source / L"shared.bin").c_str(), nullptr) != FALSE);
    const auto topology = windows::inspect_stock_research_topology(source);
    REQUIRE(topology);
    CHECK(topology.summary->safe_to_materialize);
    CHECK(topology.summary->hardlink_count == 2U);
    CHECK(contains_category(
        *topology.summary,
        windows::StockResearchTopologyCategory::source_file_hardlink));

    const auto destination = fixture.root / L"destination";
    const auto copied =
        windows::materialize_stock_research_copy(source, destination);
    REQUIRE(copied);
    CHECK(copied.materialization->materialized_hardlink_count == 2U);
    CHECK(link_count(destination / L"shared.bin") == 1U);
    CHECK(link_count(destination / L"shared-alias.bin") == 1U);
}

TEST_CASE(
    "ADS and file symlinks remain fail closed",
    "[windows][stock-runtime][research-copy][topology][reject]")
{
    SECTION("alternate data stream") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        write_file(source / L"ordinary.bin", "ordinary");
        write_file(source / L"ordinary.bin:private", "private");
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->alternate_data_stream_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_alternate_data_stream));
    }

    SECTION("source-root alternate data stream") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        if (!try_write_alternate_stream(source, L"root-private", "private")) {
            SKIP("Directory ADS fixture capability is unavailable");
        }
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->alternate_data_stream_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_alternate_data_stream));
    }

    SECTION("nested-directory alternate data stream") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        if (!try_write_alternate_stream(
                source / L"valve", L"directory-private", "private")) {
            SKIP("Directory ADS fixture capability is unavailable");
        }
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->alternate_data_stream_count == 1U);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_alternate_data_stream));
    }

    SECTION("file symlink when capability exists") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        write_file(source / L"target.bin", "target");
        const auto link = source / L"file-link.bin";
        if (::CreateSymbolicLinkW(
                link.c_str(), (source / L"target.bin").c_str(),
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == FALSE) {
            SKIP("Windows file-symlink capability is unavailable");
        }
        const auto topology = windows::inspect_stock_research_topology(source);
        REQUIRE(topology);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_internal_file_symlink));
    }
}

TEST_CASE(
    "Remote substituted and bounded sources complete with unsafe summaries",
    "[windows][stock-runtime][research-copy][topology][bounds][reject]")
{
    SECTION("UNC source") {
        const auto topology = windows::inspect_stock_research_topology(
            LR"(\\hlclient-invalid-host\unreachable\Half-Life)");
        REQUIRE(topology);
        CHECK(topology.summary->inspection_complete);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::source_unc_path));
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::source_remote_volume));
    }

    SECTION("SUBST source") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        ScopedSubstDrive drive;
        if (!create_subst_drive(source, drive)) {
            SKIP("Per-session SUBST mapping capability is unavailable");
        }
        const auto topology =
            windows::inspect_stock_research_topology(drive.root());
        REQUIRE(topology);
        CHECK(topology.summary->inspection_complete);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::source_subst_drive));
    }

    SECTION("entry bound") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        windows::StockResearchCopyLimits limits;
        limits.maximum_entries = 2U;
        const auto topology =
            windows::inspect_stock_research_topology(source, limits);
        REQUIRE(topology);
        CHECK(topology.summary->inspection_complete);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->entry_count == limits.maximum_entries);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_entry_limit_exceeded));
    }

    SECTION("byte bound") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        windows::StockResearchCopyLimits limits;
        limits.maximum_file_bytes = 5U;
        limits.maximum_total_bytes = 5U;
        const auto topology =
            windows::inspect_stock_research_topology(source, limits);
        REQUIRE(topology);
        CHECK(topology.summary->inspection_complete);
        CHECK_FALSE(topology.summary->safe_to_materialize);
        CHECK(topology.summary->byte_count <= limits.maximum_total_bytes);
        CHECK(contains_category(
            *topology.summary,
            windows::StockResearchTopologyCategory::
                source_byte_limit_exceeded));
    }
}

TEST_CASE(
    "Research copy rejects concurrent source mutation and unsafe destinations",
    "[windows][stock-runtime][research-copy][materialize][reject]")
{
    SECTION("concurrent mutation") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        const auto destination = fixture.root / L"destination";
        MutationContext context{source / L"hl.exe", false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &mutate_source;
        options.progress_context = &context;
        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        CHECK_FALSE(copied);
        CHECK(context.mutated);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  source_changed_during_materialization);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("source-root identity replacement") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto displaced = fixture.root / L"displaced-source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        SourceRootReplacementContext context{source, displaced, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &replace_source_root_identity;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK_FALSE(copied);
        CHECK(context.replaced);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  source_changed_during_materialization);
        CHECK_FALSE(fs::exists(destination));
        CHECK(fs::exists(source / L"replacement-sentinel.bin"));
        CHECK(fs::exists(displaced / L"hl.exe"));
    }

    SECTION("destination inside source") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        const auto destination = source / L"research-copy";
        const auto copied =
            windows::materialize_stock_research_copy(source, destination);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_overlaps_source);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("destination below steamapps") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        populate_stock_root(source);
        const auto destination =
            fixture.root / L"steamapps" / L"research-copy";
        const auto copied =
            windows::materialize_stock_research_copy(source, destination);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_overlaps_steam_library);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("source is already inside destination") {
        Fixture fixture;
        const auto destination = fixture.root / L"existing-container";
        const auto source = destination / L"source";
        populate_stock_root(source);
        const auto copied =
            windows::materialize_stock_research_copy(source, destination);
        CHECK_FALSE(copied);
        CHECK(copied.code ==
              windows::StockResearchCopyErrorCode::destination_exists);
        CHECK(fs::exists(source / L"hl.exe"));
    }

    SECTION("configured Steam library alias resolves to destination volume identity") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto custom_library = fixture.root / L"custom-library";
        const auto custom_library_alias = fixture.root / L"library-alias";
        populate_stock_root(source);
        fs::create_directories(custom_library);
        REQUIRE(create_junction(custom_library_alias, custom_library));
        const auto destination = custom_library / L"research-copy";
        windows::StockResearchCopyOptions options;
        options.configured_steam_library_roots.push_back(
            custom_library_alias);

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_overlaps_steam_library);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("published destination root cannot be renamed or substituted") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        const auto displaced = fixture.root / L"displaced-published-copy";
        populate_stock_root(source);
        DestinationReplacementContext context{
            destination, displaced, false, false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &replace_published_destination;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(copied);
        CHECK(context.attempted);
        CHECK(context.blocked);
        CHECK_FALSE(context.replaced);
        CHECK(fs::exists(destination / L"hl.exe"));
        CHECK_FALSE(fs::exists(displaced));
    }

    SECTION("staging root cannot be renamed or substituted") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        const auto displaced = fixture.root / L"displaced-staging";
        populate_stock_root(source);
        StagingReplacementContext context{
            fixture.root, {}, displaced, false, false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &replace_staging_directory;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(copied);
        CHECK(context.attempted);
        CHECK(context.blocked);
        CHECK_FALSE(context.replaced);
        CHECK(fs::exists(destination / L"hl.exe"));
        CHECK_FALSE(fs::exists(displaced));
    }

    SECTION("share-delete publish transition rejects staging substitution") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        const auto displaced = fixture.root / L"displaced-transition";
        populate_stock_root(source);
        TransitionReplacementContext context{
            fixture.root, {}, displaced, false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &replace_staging_during_publish_transition;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK_FALSE(copied);
        CHECK(context.attempted);
        CHECK(context.replaced);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_identity_invalid);
        CHECK_FALSE(fs::exists(destination));
        CHECK_FALSE(fs::exists(displaced));
        REQUIRE_FALSE(context.staging.empty());
        CHECK(read_file(context.staging / L"unrelated.txt") ==
              "must survive");
    }

    SECTION("transient descendant share is retried during root publish") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        TransitionSharingContext context;
        context.parent = fixture.root;
        windows::StockResearchCopyOptions options;
        options.progress_hook =
            &retain_staging_descendant_during_publish_transition;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        if (context.releaser.joinable()) context.releaser.join();

        INFO("copy code=" << windows::to_string(copied.code)
                            << " native=" << copied.native_error);
        CHECK(copied);
        CHECK(context.attempted);
        CHECK(context.opened);
        CHECK(context.released.load());
        CHECK(fs::exists(destination / L"hl.exe"));
    }
}

TEST_CASE(
    "Published research tree is reinventoried with metadata and root streams",
    "[windows][stock-runtime][research-copy][materialize][publish][reject]")
{
    SECTION("preparation manifest mutation") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedMutationContext context{
            destination / L".hlclient-research-preparation.json", false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &mutate_published_file;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK_FALSE(copied);
        CHECK(context.mutated);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("published root alternate data stream") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedAdsContext context{
            destination, L"root-private", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_published_ads;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        REQUIRE(context.attempted);
        if (!context.created) {
            SKIP("Directory ADS fixture capability is unavailable");
        }

        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("published metadata alternate data stream") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedAdsContext context{
            destination / L".hlclient-research-pending",
            L"metadata-private", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_published_ads;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        REQUIRE(context.attempted);
        if (!context.created) {
            SKIP("File ADS fixture capability is unavailable");
        }

        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("published file gains an outside hardlink") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        const auto outside_link = fixture.root / L"outside-hl.exe";
        populate_stock_root(source);
        PublishedHardlinkContext context{
            destination / L"hl.exe", outside_link, false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_published_hardlink;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        REQUIRE(context.attempted);
        if (!context.created) {
            SKIP("NTFS hardlink mutation fixture capability is unavailable");
        }

        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
        CHECK(fs::is_regular_file(outside_link));
        CHECK(link_count(outside_link) == 1U);
    }

    SECTION("unknown published child is quarantined before cleanup refusal") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedUnknownChildContext context{
            destination / L"unowned.txt", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_unknown_published_child;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK_FALSE(copied);
        CHECK(context.attempted);
        CHECK(context.created);
        CHECK(copied.code ==
              windows::StockResearchCopyErrorCode::cleanup_failed);
        CHECK_FALSE(fs::exists(destination));

        std::vector<fs::path> quarantines;
        for (const auto& entry : fs::directory_iterator{fixture.root}) {
            if (entry.path().filename().native().starts_with(
                    L".hlclient-stock-research-quarantine-")) {
                quarantines.push_back(entry.path());
            }
        }
        REQUIRE(quarantines.size() == 1U);
        CHECK(read_file(quarantines.front() / L"unowned.txt") == "unowned");
        std::vector<fs::path> failure_markers;
        for (const auto& entry : fs::directory_iterator{quarantines.front()}) {
            if (entry.path().filename().native().starts_with(
                    L".hlclient-stock-research-failure-")) {
                failure_markers.push_back(entry.path());
            }
        }
        REQUIRE(failure_markers.size() == 1U);
        const auto failure_metadata = read_file(failure_markers.front());
        CHECK(failure_metadata.find(
                  "hlclient.stock-research-copy-failure.v1") !=
              std::string::npos);
        CHECK(failure_metadata.find("\"category\":\"cleanup_failed\"") !=
              std::string::npos);
        CHECK(failure_metadata.find("\"paths_recorded\":false") !=
              std::string::npos);
        CHECK(failure_metadata.find(fixture.root.string()) ==
              std::string::npos);
    }

}

TEST_CASE(
    "Final published inventory uses an atomic mutation witness",
    "[windows][stock-runtime][research-copy][materialize][publish][race]")
{
    SECTION("source mutation immediately before commit is blocked") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        const auto source_file = source / L"valve" / L"maps" /
                                 L"boot_camp.bsp";
        const auto before = read_file(source_file);
        SourceCommitMutationContext context{source_file};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &attempt_source_commit_window_mutation;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        REQUIRE(copied);
        CHECK(context.attempted);
        CHECK(context.blocked);
        CHECK(read_file(source_file) == before);
        CHECK(fs::is_regular_file(
            destination / L".hlclient-research-isolated"));
    }

    SECTION("unknown child immediately before commit marker rename") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        CommitWindowMutationContext context{
            destination / L"commit-window-unowned.txt"};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_commit_window_unknown_child;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(context.inventory_matched);
        CHECK(context.guards_acquired);
        CHECK(context.attempted);
        CHECK(context.created);
        CHECK_FALSE(copied);
        CHECK(copied.code ==
              windows::StockResearchCopyErrorCode::cleanup_failed);
        CHECK_FALSE(fs::exists(destination));

        std::vector<fs::path> quarantines;
        for (const auto& entry : fs::directory_iterator{fixture.root}) {
            if (entry.path().filename().native().starts_with(
                    L".hlclient-stock-research-quarantine-")) {
                quarantines.push_back(entry.path());
            }
        }
        REQUIRE(quarantines.size() == 1U);
        CHECK(fs::is_regular_file(
            quarantines.front() / L"commit-window-unowned.txt"));
        CHECK_FALSE(fs::exists(
            quarantines.front() / L".hlclient-research-isolated"));
    }

    SECTION("unknown child after cached inventory") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedUnknownChildContext context{
            destination / L"late-unowned.txt", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_late_unknown_published_child;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(context.attempted);
        CHECK(context.created);
        CHECK_FALSE(copied);
        CHECK(copied.code ==
              windows::StockResearchCopyErrorCode::cleanup_failed);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("root ADS after cached inventory") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedAdsContext context{
            destination, L"late-root-private", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_late_published_ads;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);
        REQUIRE(context.attempted);
        if (!context.created) {
            SKIP("Concurrent directory ADS fixture capability is unavailable");
        }

        CHECK(context.attempted);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("file bytes after cached inventory are revalidated") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        LatePublishedMutationContext context{
            destination / L"hl.exe", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &mutate_late_published_file;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(context.attempted);
        CHECK(context.mutated);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("file ADS after cached inventory is revalidated") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        populate_stock_root(source);
        PublishedAdsContext context{
            destination / L"hl.exe", L"late-file-private", false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_late_published_ads;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(context.attempted);
        CHECK(context.created);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
    }

    SECTION("outside hardlink after cached inventory is revalidated") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        const auto outside_link = fixture.root / L"late-outside-hl.exe";
        populate_stock_root(source);
        PublishedHardlinkContext context{
            destination / L"hl.exe", outside_link, false, false};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &add_late_published_hardlink;
        options.progress_context = &context;

        const auto copied = windows::materialize_stock_research_copy(
            source, destination, options);

        CHECK(context.attempted);
        CHECK(context.created);
        CHECK_FALSE(copied);
        CHECK(copied.code == windows::StockResearchCopyErrorCode::
                                  destination_inventory_mismatch);
        CHECK_FALSE(fs::exists(destination));
        CHECK(fs::is_regular_file(outside_link));
        CHECK(link_count(outside_link) == 1U);
    }

    SECTION("oplock break is tombstoned before hostile open resumes") {
        Fixture fixture;
        const auto source = fixture.root / L"source";
        const auto destination = fixture.root / L"destination";
        const auto published_file = destination / L"hl.exe";
        populate_stock_root(source);

        const HANDLE guards_acquired =
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const HANDLE resume_verification =
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const HANDLE hostile_started =
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const HANDLE hostile_returned =
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const HANDLE release_hostile =
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const HANDLE materialization_returned =
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const std::array events{
            guards_acquired, resume_verification, hostile_started,
            hostile_returned, release_hostile, materialization_returned};
        REQUIRE(std::ranges::none_of(events, [](const HANDLE event) {
            return event == nullptr || event == INVALID_HANDLE_VALUE;
        }));

        PublishedOplockBreakContext context{
            guards_acquired, resume_verification, hostile_returned};
        windows::StockResearchCopyOptions options;
        options.progress_hook = &pause_with_published_file_guards;
        options.progress_context = &context;
        windows::StockResearchMaterializationResult copied;
        std::thread materializer{[&]() {
            copied = windows::materialize_stock_research_copy(
                source, destination, options);
            static_cast<void>(::SetEvent(materialization_returned));
        }};

        const DWORD guards_wait =
            ::WaitForSingleObject(guards_acquired, 10'000U);
        std::atomic<bool> hostile_opened{false};
        std::atomic<bool> destination_absent_when_hostile_returned{false};
        std::atomic<bool> commit_marker_absent_when_hostile_returned{false};
        std::atomic<DWORD> hostile_open_error{ERROR_SUCCESS};
        std::thread hostile;
        DWORD hostile_started_wait = WAIT_FAILED;
        DWORD hostile_early_wait = WAIT_FAILED;
        if (guards_wait == WAIT_OBJECT_0) {
            hostile = std::thread{[&]() {
                static_cast<void>(::SetEvent(hostile_started));
                const HANDLE handle = ::CreateFileW(
                    published_file.c_str(),
                    GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr);
                if (handle == INVALID_HANDLE_VALUE) {
                    hostile_open_error.store(::GetLastError());
                } else {
                    hostile_opened.store(true);
                    const DWORD attributes =
                        ::GetFileAttributesW(destination.c_str());
                    const DWORD attributes_error =
                        attributes == INVALID_FILE_ATTRIBUTES
                            ? ::GetLastError()
                            : ERROR_SUCCESS;
                    destination_absent_when_hostile_returned.store(
                        attributes == INVALID_FILE_ATTRIBUTES &&
                        (attributes_error == ERROR_FILE_NOT_FOUND ||
                         attributes_error == ERROR_PATH_NOT_FOUND));
                    commit_marker_absent_when_hostile_returned.store(
                        ::GetFileAttributesW(
                            (destination /
                             L".hlclient-research-isolated").c_str()) ==
                        INVALID_FILE_ATTRIBUTES);
                }
                static_cast<void>(::SetEvent(hostile_returned));
                static_cast<void>(
                    ::WaitForSingleObject(release_hostile, 10'000U));
                if (handle != INVALID_HANDLE_VALUE) {
                    static_cast<void>(::CloseHandle(handle));
                }
            }};
            hostile_started_wait =
                ::WaitForSingleObject(hostile_started, 10'000U);
            if (hostile_started_wait == WAIT_OBJECT_0) {
                hostile_early_wait =
                    ::WaitForSingleObject(hostile_returned, 250U);
            }
        }

        static_cast<void>(::SetEvent(resume_verification));
        const DWORD materialization_wait =
            ::WaitForSingleObject(materialization_returned, 10'000U);
        const DWORD hostile_return_wait = hostile.joinable()
                                              ? ::WaitForSingleObject(
                                                    hostile_returned, 10'000U)
                                              : WAIT_FAILED;
        static_cast<void>(::SetEvent(release_hostile));
        if (hostile.joinable()) hostile.join();
        materializer.join();
        for (const HANDLE event : events) {
            static_cast<void>(::CloseHandle(event));
        }

        CHECK(guards_wait == WAIT_OBJECT_0);
        CHECK(context.hook_attempted.load());
        CHECK(context.hook_wait_result.load() == WAIT_OBJECT_0);
        CHECK(context.release_hook_attempted.load());
        CHECK(context.release_hook_wait_result.load() == WAIT_OBJECT_0);
        CHECK(hostile_started_wait == WAIT_OBJECT_0);
        CHECK(hostile_early_wait == WAIT_TIMEOUT);
        CHECK(hostile_return_wait == WAIT_OBJECT_0);
        CHECK(hostile_opened.load());
        CHECK(hostile_open_error.load() == ERROR_SUCCESS);
        CHECK_FALSE(destination_absent_when_hostile_returned.load());
        CHECK(commit_marker_absent_when_hostile_returned.load());
        CHECK(materialization_wait == WAIT_OBJECT_0);
        CHECK_FALSE(copied);
        CHECK(copied.code ==
              windows::StockResearchCopyErrorCode::cleanup_failed);
        CHECK(fs::is_directory(destination));
        CHECK_FALSE(fs::exists(
            destination / L".hlclient-research-isolated"));
        CHECK(fs::is_regular_file(
            destination / L".hlclient-research-pending"));
        std::vector<fs::path> failure_markers;
        for (const auto& entry : fs::directory_iterator{destination}) {
            if (entry.path().filename().native().starts_with(
                    L".hlclient-stock-research-failure-")) {
                failure_markers.push_back(entry.path());
            }
        }
        REQUIRE(failure_markers.size() == 1U);
        const auto failure_metadata = read_file(failure_markers.front());
        CHECK(failure_metadata.find(
                  "hlclient.stock-research-copy-failure.v1") !=
              std::string::npos);
        CHECK(failure_metadata.find("\"category\":\"cleanup_failed\"") !=
              std::string::npos);
        CHECK(failure_metadata.find("\"paths_recorded\":false") !=
              std::string::npos);
    }
}

TEST_CASE(
    "Pinned published topology rejects child and parent junction swaps",
    "[windows][stock-runtime][research-copy][materialize][cleanup][race]")
{
    Fixture fixture;
    const auto source = fixture.root / L"source";
    const auto destination_parent = fixture.root / L"owned-parent";
    const auto destination = destination_parent / L"destination";
    const auto parent_displaced = fixture.root / L"displaced-parent";
    const auto child_displaced = fixture.root / L"displaced-valve";
    const auto external = fixture.root / L"external";
    populate_stock_root(source);
    write_file(external / L"outside-sentinel.bin", "must survive");
    const auto sentinel_before =
        read_file(external / L"outside-sentinel.bin");
    PublishedChildJunctionContext context{
        destination_parent, parent_displaced, destination, child_displaced,
        external, false, false, false, false};
    windows::StockResearchCopyOptions options;
    options.progress_hook = &replace_published_child_with_junction;
    options.progress_context = &context;

    const auto copied = windows::materialize_stock_research_copy(
        source, destination, options);

    CHECK(copied);
    CHECK(context.attempted);
    CHECK(context.child_blocked);
    CHECK(context.parent_blocked);
    CHECK_FALSE(context.replaced);
    CHECK(read_file(external / L"outside-sentinel.bin") == sentinel_before);
    CHECK(std::ranges::distance(fs::directory_iterator{external},
                                fs::directory_iterator{}) == 1);
    CHECK_FALSE(fs::exists(child_displaced));
    CHECK_FALSE(fs::exists(parent_displaced));
    CHECK(fs::exists(
        destination / L"valve" / L"maps" / L"boot_camp.bsp"));
    const DWORD attributes =
        ::GetFileAttributesW((destination / L"valve").c_str());
    CHECK(attributes != INVALID_FILE_ATTRIBUTES);
    CHECK((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U);
}
