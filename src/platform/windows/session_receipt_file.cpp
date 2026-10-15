#include <hlclient/platform/windows/session_receipt_file.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace hlclient::platform::windows {
namespace {

class ReceiptHandle final {
public:
    explicit ReceiptHandle(const HANDLE value) noexcept : value_{value} {}
    ~ReceiptHandle() { if (value_ != INVALID_HANDLE_VALUE) ::CloseHandle(value_); }
    ReceiptHandle(const ReceiptHandle&) = delete;
    ReceiptHandle& operator=(const ReceiptHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] SessionReceiptFileResult failed(const SessionReceiptFileStatus status,
    const unsigned long error = 0U) noexcept
{
    return {status, std::nullopt, error};
}

[[nodiscard]] bool same_file(const BY_HANDLE_FILE_INFORMATION& before,
                             const BY_HANDLE_FILE_INFORMATION& after) noexcept
{
    return before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
        before.nFileIndexHigh == after.nFileIndexHigh &&
        before.nFileIndexLow == after.nFileIndexLow &&
        before.nFileSizeHigh == after.nFileSizeHigh &&
        before.nFileSizeLow == after.nFileSizeLow &&
        before.ftLastWriteTime.dwHighDateTime == after.ftLastWriteTime.dwHighDateTime &&
        before.ftLastWriteTime.dwLowDateTime == after.ftLastWriteTime.dwLowDateTime &&
        before.dwFileAttributes == after.dwFileAttributes &&
        after.nNumberOfLinks == 1U;
}

[[nodiscard]] SessionReceiptFileResult read_known_leaf(
    const SecureOutputDirectory& directory, const std::wstring_view leaf)
{
    const auto path = directory.canonical_path() / leaf;
    // The parent capability pins the exact directory/ancestors. OPEN_REPARSE
    // inspects the child itself, and sharing denies replacement and writes for
    // the complete bounded read. Hardlinked files are not an owning receipt.
    const ReceiptHandle file{::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file.valid()) {
        const auto error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return failed(SessionReceiptFileStatus::missing);
        return failed(SessionReceiptFileStatus::open_failed, error);
    }
    BY_HANDLE_FILE_INFORMATION before{};
    if (::GetFileType(file.get()) != FILE_TYPE_DISK ||
        !::GetFileInformationByHandle(file.get(), &before) ||
        (before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U ||
        before.nNumberOfLinks != 1U) return failed(SessionReceiptFileStatus::not_regular_file);
    if (before.nFileSizeHigh != 0U ||
        before.nFileSizeLow == 0U || before.nFileSizeLow > app::kSessionReceiptMaximumBytes)
        return failed(SessionReceiptFileStatus::size_limit);
    std::array<char, app::kSessionReceiptMaximumBytes + 1U> bytes{};
    DWORD count{};
    if (!::ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
        return failed(SessionReceiptFileStatus::read_failed, ::GetLastError());
    BY_HANDLE_FILE_INFORMATION after{};
    if (count != before.nFileSizeLow ||
        !::GetFileInformationByHandle(file.get(), &after) || !same_file(before, after))
        return failed(SessionReceiptFileStatus::read_failed);
    auto decoded = app::decode_session_receipt(std::string_view{bytes.data(), count});
    if (!decoded) return failed(SessionReceiptFileStatus::invalid_receipt);
    return {SessionReceiptFileStatus::ready, std::move(decoded), 0U};
}

[[nodiscard]] bool counters_extend(const app::SessionReceiptCounters& before,
                                    const app::SessionReceiptCounters& after) noexcept
{
    const auto extends = [](const std::optional<std::uint64_t>& first,
                            const std::optional<std::uint64_t>& second) {
        return !first || (second && *second >= *first);
    };
    return extends(before.new_usercmd_count, after.new_usercmd_count) &&
        extends(before.transmitted_usercmd_packet_count, after.transmitted_usercmd_packet_count) &&
        extends(before.server_sample_count, after.server_sample_count) &&
        (!before.usercmd_zero_observed || after.usercmd_zero_observed);
}

[[nodiscard]] bool snapshot_extends(const app::SessionReceiptSnapshot& before,
                                    const app::SessionReceiptSnapshot& after) noexcept
{
    if (before.identity != after.identity || before.generation != after.generation ||
        before.generation_ordinal != after.generation_ordinal ||
        (before.receiving_entity && before.receiving_entity != after.receiving_entity) ||
        before.host_qpc_frequency != after.host_qpc_frequency ||
        (before.fact_mask & after.fact_mask) != before.fact_mask ||
        !counters_extend(before.counters, after.counters)) return false;
    for (std::size_t index = 0U; index < before.milestones.size(); ++index)
        if (before.milestones[index] && before.milestones[index] != after.milestones[index])
            return false;
    for (std::size_t index = 0U; index < before.peer_removal_count; ++index) {
        const auto& old = before.peer_removals[index];
        bool found{};
        for (std::size_t next = 0U; next < after.peer_removal_count; ++next) {
            const auto& updated = after.peer_removals[next];
            if (old.entity != updated.entity) continue;
            found = updated.source_ordinal >= old.source_ordinal &&
                updated.process_ms >= old.process_ms &&
                (old.host_qpc_ticks == 0U || updated.host_qpc_ticks >= old.host_qpc_ticks);
            break;
        }
        if (!found) return false;
    }
    return true;
}

[[nodiscard]] bool generation_after(const app::SessionReceiptGeneration& old,
                                    const app::SessionReceiptGeneration& next) noexcept
{
    return next.network_generation > old.network_generation ||
        (next.network_generation == old.network_generation &&
         next.map_generation > old.map_generation);
}

} // namespace

std::optional<SessionReceiptClockObservation> query_session_receipt_clock() noexcept
{
    LARGE_INTEGER ticks{}, frequency{};
    if (!::QueryPerformanceFrequency(&frequency) || !::QueryPerformanceCounter(&ticks) ||
        frequency.QuadPart <= 0 || ticks.QuadPart <= 0) return std::nullopt;
    return SessionReceiptClockObservation{static_cast<std::uint64_t>(ticks.QuadPart),
        static_cast<std::uint64_t>(frequency.QuadPart)};
}

std::optional<SessionReceiptProcessIdentity>
query_current_session_receipt_process_identity() noexcept
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
        return std::nullopt;
    const auto creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) |
        static_cast<std::uint64_t>(created.dwLowDateTime);
    if (creation == 0U) return std::nullopt;
    return SessionReceiptProcessIdentity{::GetCurrentProcessId(), creation};
}

SessionReceiptFileResult publish_session_receipt(const SecureOutputDirectory& directory,
    const app::SessionReceiptSnapshot& snapshot) noexcept
{
    try {
        const auto encoded = app::encode_session_receipt(snapshot);
        if (!encoded) return failed(SessionReceiptFileStatus::invalid_receipt);
        const auto leaf = app::session_receipt_leaf(snapshot.identity.role,
            snapshot.generation_ordinal, snapshot.stage);
        if (leaf.empty()) return failed(SessionReceiptFileStatus::invalid_receipt);
        const auto written = secure_atomic_write_new(directory, leaf,
            std::as_bytes(std::span{encoded->data(), encoded->size()}));
        if (!written) return failed(SessionReceiptFileStatus::publish_failed,
            written.error ? written.error->operating_system_error : 0U);
        return {SessionReceiptFileStatus::ready, snapshot, 0U};
    } catch (...) {
        return failed(SessionReceiptFileStatus::publish_failed, ERROR_NOT_ENOUGH_MEMORY);
    }
}

static SessionReceiptFileResult read_latest_session_receipt_impl(const SecureOutputDirectory& directory,
    const app::SessionReceiptIdentity& expected, const std::optional<std::string_view> expected_map_identifier,
    const bool require_terminal,
    const std::optional<std::uint64_t> expected_network_generation,
    const std::optional<std::uint64_t> expected_map_generation) noexcept
{
    try {
        if (!directory.valid()) return failed(SessionReceiptFileStatus::invalid_identity);
        app::SessionReceiptSnapshot identity_probe;
        identity_probe.identity = expected;
        identity_probe.generation_ordinal = 1U;
        if (!app::valid_session_receipt(identity_probe))
            return failed(SessionReceiptFileStatus::invalid_identity);
        if(expected_map_identifier) {
            const auto map = app::normalized_session_map_identifier(*expected_map_identifier);
            if (!map || *map != *expected_map_identifier) return failed(SessionReceiptFileStatus::invalid_identity);
        }
        std::optional<app::SessionReceiptSnapshot> latest;
        for (std::uint32_t ordinal = 1U; ordinal <= app::kSessionReceiptMaximumGenerations; ++ordinal) {
            std::optional<app::SessionReceiptSnapshot> generation;
            for (std::uint32_t stage = 0U; stage < 6U; ++stage) {
                const auto selected_stage = static_cast<app::SessionReceiptStage>(stage);
                auto loaded = read_known_leaf(directory,
                    app::session_receipt_leaf(expected.role, ordinal, selected_stage));
                if (loaded.status == SessionReceiptFileStatus::missing) continue;
                if (!loaded) return loaded;
                const auto& current = *loaded.snapshot;
                if (current.identity != expected || current.generation_ordinal != ordinal ||
                    current.stage != selected_stage)
                    return failed(SessionReceiptFileStatus::invalid_identity);
                if (generation && !snapshot_extends(*generation, current))
                    return failed(SessionReceiptFileStatus::inconsistent_history);
                generation = std::move(loaded.snapshot);
            }
            if (!generation) continue;
            if (latest && !generation_after(latest->generation, generation->generation))
                return failed(SessionReceiptFileStatus::inconsistent_history);
            latest = std::move(generation);
        }
        auto terminal = read_known_leaf(directory,
            app::session_receipt_leaf(expected.role, 1U, app::SessionReceiptStage::terminal));
        const bool terminal_present = terminal.status != SessionReceiptFileStatus::missing;
        if (terminal_present) {
            if (!terminal) return terminal;
            const auto& final = *terminal.snapshot;
            if (final.identity != expected || final.stage != app::SessionReceiptStage::terminal)
                return failed(SessionReceiptFileStatus::invalid_identity);
            if (latest) {
                if (final.generation_ordinal < latest->generation_ordinal ||
                    (final.generation_ordinal == latest->generation_ordinal &&
                     !snapshot_extends(*latest, final)) ||
                    (final.generation_ordinal > latest->generation_ordinal &&
                     !generation_after(latest->generation, final.generation)))
                    return failed(SessionReceiptFileStatus::inconsistent_history);
            }
            latest = std::move(terminal.snapshot);
        }
        if (!latest) return failed(SessionReceiptFileStatus::missing);
        if ((expected_map_identifier && latest->generation.map_identifier != "not_observed" &&
             latest->generation.map_identifier != *expected_map_identifier) ||
            (expected_network_generation &&
             *expected_network_generation != latest->generation.network_generation) ||
            (expected_map_generation &&
             *expected_map_generation != latest->generation.map_generation))
            return failed(SessionReceiptFileStatus::invalid_identity);
        if (require_terminal && !terminal_present)
            return {SessionReceiptFileStatus::terminal_missing, std::move(latest), 0U};
        return {SessionReceiptFileStatus::ready, std::move(latest), 0U};
    } catch (...) {
        return failed(SessionReceiptFileStatus::read_failed, ERROR_NOT_ENOUGH_MEMORY);
    }
}

SessionReceiptFileResult read_latest_session_receipt(const SecureOutputDirectory& directory,
    const app::SessionReceiptIdentity& expected,const std::string_view expected_map_identifier,
    const bool require_terminal,const std::optional<std::uint64_t> expected_network_generation,
    const std::optional<std::uint64_t> expected_map_generation) noexcept {
    return read_latest_session_receipt_impl(directory,expected,expected_map_identifier,
        require_terminal,expected_network_generation,expected_map_generation);
}

SessionReceiptFileResult read_latest_external_session_receipt(const SecureOutputDirectory& directory,
    const app::SessionReceiptIdentity& expected,const bool require_terminal) noexcept {
    return read_latest_session_receipt_impl(directory,expected,std::nullopt,
        require_terminal,std::nullopt,std::nullopt);
}

std::string_view to_string(const SessionReceiptFileStatus status) noexcept
{
    switch (status) {
    case SessionReceiptFileStatus::ready: return "ready";
    case SessionReceiptFileStatus::missing: return "missing";
    case SessionReceiptFileStatus::terminal_missing: return "terminal_missing";
    case SessionReceiptFileStatus::invalid_identity: return "invalid_identity";
    case SessionReceiptFileStatus::invalid_receipt: return "invalid_receipt";
    case SessionReceiptFileStatus::inconsistent_history: return "inconsistent_history";
    case SessionReceiptFileStatus::open_failed: return "open_failed";
    case SessionReceiptFileStatus::not_regular_file: return "not_regular_file";
    case SessionReceiptFileStatus::size_limit: return "size_limit";
    case SessionReceiptFileStatus::read_failed: return "read_failed";
    case SessionReceiptFileStatus::publish_failed: return "publish_failed";
    }
    return "invalid";
}

} // namespace hlclient::platform::windows
