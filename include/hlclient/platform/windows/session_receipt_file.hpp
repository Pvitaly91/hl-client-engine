#pragma once

#include <hlclient/app/session_receipt.hpp>
#include <hlclient/platform/windows/secure_output.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::platform::windows {

enum class SessionReceiptFileStatus {
    ready, missing, terminal_missing, invalid_identity, invalid_receipt,
    inconsistent_history, open_failed, not_regular_file, size_limit,
    read_failed, publish_failed,
};
struct SessionReceiptFileResult final {
    SessionReceiptFileStatus status{SessionReceiptFileStatus::missing};
    std::optional<app::SessionReceiptSnapshot> snapshot;
    unsigned long operating_system_error{};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return status == SessionReceiptFileStatus::ready && snapshot.has_value();
    }
};
struct SessionReceiptClockObservation final {
    std::uint64_t ticks{};
    std::uint64_t frequency{};
};
struct SessionReceiptProcessIdentity final {
    std::uint32_t process_id{};
    std::uint64_t process_creation{};
};
[[nodiscard]] std::optional<SessionReceiptClockObservation>
query_session_receipt_clock() noexcept;
[[nodiscard]] std::optional<SessionReceiptProcessIdentity>
query_current_session_receipt_process_identity() noexcept;
// Existing secure-output capability pins the caller-owned exact run directory.
// Neither function creates a directory, replaces a file or follows a link.
[[nodiscard]] SessionReceiptFileResult publish_session_receipt(
    const SecureOutputDirectory&, const app::SessionReceiptSnapshot&) noexcept;
[[nodiscard]] SessionReceiptFileResult read_latest_session_receipt(
    const SecureOutputDirectory&, const app::SessionReceiptIdentity& expected,
    std::string_view expected_map_identifier,
    bool require_terminal = false,
    std::optional<std::uint64_t> expected_network_generation = std::nullopt,
    std::optional<std::uint64_t> expected_map_generation = std::nullopt) noexcept;
// Unknown server-selected map, same exact process/run and secure history
// validation. This never turns a configured map into observed readiness.
[[nodiscard]] SessionReceiptFileResult read_latest_external_session_receipt(
    const SecureOutputDirectory&, const app::SessionReceiptIdentity& expected,
    bool require_terminal = false) noexcept;
[[nodiscard]] std::string_view to_string(SessionReceiptFileStatus) noexcept;

} // namespace hlclient::platform::windows
