#include <hlclient/platform/windows/stock_external_target_review.hpp>
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
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

struct MountPointBuffer final {
    ULONG tag;
    USHORT length;
    USHORT reserved;
    USHORT substitute_offset;
    USHORT substitute_length;
    USHORT print_offset;
    USHORT print_length;
    WCHAR names[1];
};

[[nodiscard]] fs::path unique_root()
{
    std::array<wchar_t, 32'768U> module{};
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    return fs::path{std::wstring_view{module.data(), size}}.parent_path() /
           (L"hlclient-external-target-materialization-test-" +
            std::to_wstring(::GetCurrentProcessId()) + L"-" +
            std::to_wstring(
                std::chrono::steady_clock::now().time_since_epoch().count()));
}

void write_file(const fs::path& path, const std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] std::string minimal_bsp30_bytes()
{
    std::string bytes(124U, '\0');
    bytes[0] = static_cast<char>(30U);
    for (std::size_t lump = 0U; lump < 15U; ++lump) {
        bytes[4U + lump * 8U] = static_cast<char>(bytes.size());
    }
    return bytes;
}

void write_appmanifest70(const fs::path& steamapps_root)
{
    write_file(
        steamapps_root / L"appmanifest_70.acf",
        "\"AppState\"\r\n{\r\n\t\"appid\"\t\"70\"\r\n"
        "\t\"installdir\"\t\"Half-Life\"\r\n}\r\n");
}

[[nodiscard]] std::string vdf_path(const fs::path& path)
{
    const auto value = path.lexically_normal().native();
    const int required = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    REQUIRE(required > 0);
    std::string utf8(static_cast<std::size_t>(required), '\0');
    REQUIRE(::WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), utf8.data(), required,
                nullptr, nullptr) == required);
    std::string escaped;
    for (const char item : utf8) {
        if (item == '\\' || item == '"') escaped.push_back('\\');
        escaped.push_back(item);
    }
    return escaped;
}

void write_libraryfolders70(const fs::path& steamapps_root)
{
    write_file(
        steamapps_root / L"libraryfolders.vdf",
        "\"libraryfolders\"\r\n{\r\n\t\"0\"\r\n\t{\r\n"
        "\t\t\"path\"\t\"" + vdf_path(steamapps_root.parent_path()) +
            "\"\r\n\t\t\"apps\"\r\n\t\t{\r\n"
            "\t\t\t\"70\"\t\"1\"\r\n\t\t}\r\n\t}\r\n}\r\n");
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool create_junction(
    const fs::path& link, const fs::path& target)
{
    std::error_code error;
    fs::create_directories(link, error);
    if (error) return false;
    const HANDLE handle = ::CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + target.native();
    const auto substitute_bytes =
        static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<USHORT>(target.native().size() * sizeof(wchar_t));
    const auto bytes = static_cast<std::size_t>(substitute_bytes) +
                       sizeof(wchar_t) + print_bytes + sizeof(wchar_t);
    constexpr auto header = offsetof(MountPointBuffer, substitute_offset);
    std::vector<std::byte> storage(header + 8U + bytes);
    auto* data = reinterpret_cast<MountPointBuffer*>(storage.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->reserved = 0U;
    data->substitute_offset = 0U;
    data->substitute_length = substitute_bytes;
    data->print_offset =
        static_cast<USHORT>(substitute_bytes + sizeof(wchar_t));
    data->print_length = print_bytes;
    std::copy(substitute.begin(), substitute.end(), data->names);
    auto* print_at = reinterpret_cast<wchar_t*>(
        reinterpret_cast<std::byte*>(data->names) + data->print_offset);
    std::copy(target.native().begin(), target.native().end(), print_at);
    data->length = static_cast<USHORT>(8U + bytes);
    DWORD returned = 0U;
    const BOOL ok = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data,
        static_cast<DWORD>(header + data->length), nullptr, 0U, &returned,
        nullptr);
    static_cast<void>(::CloseHandle(handle));
    return ok != FALSE;
}

[[nodiscard]] std::uint32_t link_count(const fs::path& path)
{
    const HANDLE handle = ::CreateFileW(
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
    Fixture() : root{unique_root()}
    {
        fs::create_directories(source_root());
        fs::create_directories(root / L"manual-artifacts");
        write_appmanifest70(steamapps_root());
        write_libraryfolders70(steamapps_root());
        write_file(source_root() / L"hl.exe", "fake client");
        write_file(source_root() / L"hlds.exe", "fake server");
        write_file(source_root() / L"valve" / L"maps" / L"local.bsp",
                   "local map");
        write_file(
            external_root() / L"maps" / L"arena.bsp",
            minimal_bsp30_bytes());
    }
    ~Fixture()
    {
        static_cast<void>(::RemoveDirectoryW(
            (source_root() / L"shared-assets").c_str()));
        std::error_code error;
        fs::remove_all(root, error);
    }
    [[nodiscard]] bool link_external() const
    {
        return create_junction(source_root() / L"shared-assets",
                               external_root());
    }
    [[nodiscard]] fs::path steamapps_root() const
    {
        return root / L"review-library" / L"steamapps";
    }
    [[nodiscard]] fs::path app_root() const
    {
        return steamapps_root() / L"common" / L"Half-Life";
    }
    [[nodiscard]] fs::path source_root() const
    {
        return app_root() / L"source";
    }
    [[nodiscard]] fs::path external_root() const
    {
        return app_root() / L"external-assets";
    }
    fs::path root;
};

[[nodiscard]] fs::path review_parent(const Fixture& fixture)
{
    return fixture.root / L"manual-artifacts" /
           L"stock-runtime-source-review";
}

struct ExternalMutationContext final {
    fs::path path;
    bool attempted{false};
    bool mutated{false};
};

void mutate_external_before_reinventory(
    const windows::StockResearchCopyProgressPhase phase,
    std::size_t,
    void* opaque)
{
    auto& context = *static_cast<ExternalMutationContext*>(opaque);
    if (phase != windows::StockResearchCopyProgressPhase::
                     before_source_reinventory ||
        context.attempted) {
        return;
    }
    context.attempted = true;
    const HANDLE file = ::CreateFileW(
        context.path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER zero{};
    DWORD written = 0U;
    constexpr std::string_view bytes{"mutated during materialization"};
    context.mutated =
        ::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE &&
        ::SetEndOfFile(file) != FALSE &&
        ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                    &written, nullptr) != FALSE &&
        written == bytes.size() && ::FlushFileBuffers(file) != FALSE;
    static_cast<void>(::CloseHandle(file));
}

} // namespace

TEST_CASE(
    "Approved external target materializes as independent ordinary content",
    "[windows][stock-runtime][external-target][materialization]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto source = fixture.source_root();
    const auto target_file =
        fixture.external_root() / L"maps" / L"arena.bsp";
    const auto review = windows::review_stock_external_targets(
        source, review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);
    const auto approval = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approval);

    windows::StockResearchCopyOptions options;
    options.external_target_approval_manifest =
        approval.value->approval_manifest;
    const auto destination = fixture.root / L"materialized";
    const auto copied = windows::materialize_stock_research_copy(
        source, destination, options);
    INFO("materialization code=" << windows::to_string(copied.code)
                                  << " native=" << copied.native_error);
    REQUIRE(copied);
    const auto copied_file =
        destination / L"shared-assets" / L"maps" / L"arena.bsp";
    REQUIRE(fs::is_regular_file(copied_file));
    CHECK((::GetFileAttributesW(copied_file.c_str()) &
           FILE_ATTRIBUTE_REPARSE_POINT) == 0U);
    CHECK(link_count(copied_file) == 1U);
    CHECK(read_file(copied_file) == minimal_bsp30_bytes());
    CHECK(copied.materialization->approved_external_materialized_link_count ==
          1U);
    CHECK(copied.materialization->destination_reparse_count == 0U);
    CHECK(copied.materialization->destination_hardlink_count == 0U);
    CHECK(copied.materialization->source_unchanged);
    CHECK(copied.materialization->external_targets_unchanged);
    CHECK_FALSE(copied.materialization->external_approval_sha256.empty());
    CHECK(copied.materialization->evidence_eligibility ==
          windows::StockResearchCopyEvidenceEligibility::eligible);

    write_file(copied_file, "destination-only mutation");
    CHECK(read_file(target_file) == minimal_bsp30_bytes());
    CHECK(read_file(copied_file) == "destination-only mutation");
}

TEST_CASE(
    "Approved external hardlinks materialize as independent ordinary files",
    "[windows][stock-runtime][external-target][materialization][hardlink]")
{
    Fixture fixture;
    const auto target =
        fixture.external_root() / L"maps" / L"arena.bsp";
    const auto alias =
        fixture.external_root() / L"maps" / L"arena-copy.bsp";
    if (::CreateHardLinkW(alias.c_str(), target.c_str(), nullptr) == FALSE) {
        SKIP("Windows hardlink fixture capability is unavailable");
    }
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto source = fixture.source_root();
    const auto review = windows::review_stock_external_targets(
        source, review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);
    const auto approval = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approval);
    windows::StockResearchCopyOptions options;
    options.external_target_approval_manifest =
        approval.value->approval_manifest;
    const auto destination = fixture.root / L"materialized-hardlinks";
    const auto copied = windows::materialize_stock_research_copy(
        source, destination, options);
    INFO("materialization code=" << windows::to_string(copied.code)
                                  << " native=" << copied.native_error);
    REQUIRE(copied);
    const auto first =
        destination / L"shared-assets" / L"maps" / L"arena.bsp";
    const auto second =
        destination / L"shared-assets" / L"maps" / L"arena-copy.bsp";
    CHECK(link_count(first) == 1U);
    CHECK(link_count(second) == 1U);
    CHECK(copied.materialization->destination_hardlink_count == 0U);
    write_file(first, "changed copy only");
    CHECK(read_file(second) == minimal_bsp30_bytes());
    CHECK(read_file(target) == minimal_bsp30_bytes());
}

TEST_CASE(
    "Concurrent external mutation fails without partial publication",
    "[windows][stock-runtime][external-target][materialization][mutation]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto source = fixture.source_root();
    const auto review = windows::review_stock_external_targets(
        source, review_parent(fixture));
    REQUIRE(review);
    const auto approval = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approval);
    ExternalMutationContext context{
        fixture.external_root() / L"maps" / L"arena.bsp"};
    windows::StockResearchCopyOptions options;
    options.external_target_approval_manifest =
        approval.value->approval_manifest;
    options.progress_hook = &mutate_external_before_reinventory;
    options.progress_context = &context;
    const auto destination = fixture.root / L"materialized-mutation";
    const auto copied = windows::materialize_stock_research_copy(
        source, destination, options);
    CHECK_FALSE(copied);
    CHECK(context.attempted);
    CHECK(context.mutated);
    CHECK(copied.code == windows::StockResearchCopyErrorCode::
                              source_or_external_target_changed_during_materialization);
    CHECK_FALSE(fs::exists(destination));
    CHECK(read_file(source / L"hl.exe") == "fake client");
    CHECK(read_file(source / L"hlds.exe") == "fake server");
}

TEST_CASE(
    "Steam provenance metadata is held immutable through materialization commit",
    "[windows][stock-runtime][external-target][materialization][provenance]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto source = fixture.source_root();
    const auto review = windows::review_stock_external_targets(
        source, review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);
    const auto approval = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approval);

    const auto metadata =
        fixture.steamapps_root() / L"libraryfolders.vdf";
    const auto original = read_file(metadata);
    ExternalMutationContext context{metadata};
    windows::StockResearchCopyOptions options;
    options.external_target_approval_manifest =
        approval.value->approval_manifest;
    options.progress_hook = &mutate_external_before_reinventory;
    options.progress_context = &context;
    const auto destination = fixture.root / L"materialized-provenance";
    const auto copied = windows::materialize_stock_research_copy(
        source, destination, options);
    INFO("materialization code=" << windows::to_string(copied.code)
                                  << " native=" << copied.native_error);
    REQUIRE(copied);
    CHECK(context.attempted);
    CHECK_FALSE(context.mutated);
    CHECK(read_file(metadata) == original);
    CHECK(fs::is_regular_file(
        destination / L".hlclient-research-isolated"));
}

TEST_CASE(
    "Destination overlapping an approved external target is a typed collision",
    "[windows][stock-runtime][external-target][materialization][collision]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto source = fixture.source_root();
    const auto review = windows::review_stock_external_targets(
        source, review_parent(fixture));
    REQUIRE(review);
    const auto approval = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approval);
    windows::StockResearchCopyOptions options;
    options.external_target_approval_manifest =
        approval.value->approval_manifest;
    const auto destination = fixture.external_root() /
                             L"colliding-materialization";
    const auto copied = windows::materialize_stock_research_copy(
        source, destination, options);
    CHECK_FALSE(copied);
    CHECK(copied.code == windows::StockResearchCopyErrorCode::
                              external_materialization_path_collision);
    CHECK_FALSE(fs::exists(destination));
    CHECK(read_file(
              fixture.external_root() / L"maps" / L"arena.bsp") ==
          minimal_bsp30_bytes());
    CHECK(read_file(source / L"hl.exe") == "fake client");
}
