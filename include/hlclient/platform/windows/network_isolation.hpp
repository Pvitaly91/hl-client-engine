#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hlclient/platform/windows/binary_identity.hpp>

namespace hlclient::platform::windows {

inline constexpr std::size_t kMaximumIsolatedApplications = 8U;

class KillOnCloseProcessJob;

struct NetworkIsolationApplication final {
    WindowsBinaryIdentity identity;
    // Opaque bytes returned by FwpmGetAppIdFromFileName0. They are retained
    // only for the dynamic session and never serialized.
    std::vector<std::byte> wfp_application_id;
};

struct NetworkIsolationPolicy final {
    std::vector<NetworkIsolationApplication> applications;
    bool allow_ipv4_loopback{true};
    bool allow_ipv6_loopback{true};
    bool block_non_loopback_outbound{true};
    bool block_non_loopback_inbound_accept{true};
    bool dynamic_session_required{true};
    bool persistent_filters_allowed{false};
};

enum class NetworkIsolationErrorCode {
    none,
    unsupported_platform,
    empty_application_set,
    too_many_applications,
    duplicate_application,
    stale_application_identity,
    invalid_application_id,
    policy_not_fail_closed,
    privilege_required,
    wfp_unavailable,
    dynamic_session_failed,
    provider_failed,
    sublayer_failed,
    transaction_failed,
    filter_failed,
    commit_failed,
    canary_unavailable,
    canary_loopback_failed,
    canary_non_loopback_not_denied,
    canary_denial_not_os_classified,
};

struct NetworkIsolationValidation final {
    NetworkIsolationErrorCode code{NetworkIsolationErrorCode::none};
    std::size_t application_index{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == NetworkIsolationErrorCode::none;
    }
};

struct NetworkIsolationFilterPlan final {
    std::uint32_t ipv4_loopback_network_host_order{0U};
    std::uint32_t ipv4_loopback_mask_host_order{0U};
    std::array<std::byte, 16U> ipv6_loopback_address{};
    std::uint8_t ipv6_loopback_prefix_length{0U};
    std::size_t permit_filter_count{0U};
    std::size_t block_filter_count{0U};
    bool dynamic_only{false};
    std::size_t persistent_filter_count{0U};
};

[[nodiscard]] std::optional<NetworkIsolationFilterPlan>
build_network_isolation_filter_plan(
    const NetworkIsolationPolicy& policy) noexcept;

[[nodiscard]] NetworkIsolationValidation validate_network_isolation_policy(
    const NetworkIsolationPolicy& policy) noexcept;

[[nodiscard]] std::optional<NetworkIsolationApplication>
observe_network_isolation_application(
    const std::filesystem::path& executable,
    WindowsBinaryIdentityErrorCode& binary_error,
    NetworkIsolationErrorCode& isolation_error) noexcept;

struct NetworkIsolationAttestation final {
    bool dynamic_session{false};
    bool ipv4_loopback_allowed{false};
    bool ipv6_loopback_allowed{false};
    bool non_loopback_outbound_blocked{false};
    bool non_loopback_inbound_accept_blocked{false};
    std::size_t application_count{0U};
    std::size_t permit_filter_count{0U};
    std::size_t block_filter_count{0U};
    std::size_t persistent_rule_count{0U};
};

struct NetworkIsolationStartResult final {
    NetworkIsolationErrorCode code{NetworkIsolationErrorCode::none};
    std::uint32_t native_error{0U};
    std::optional<NetworkIsolationAttestation> attestation;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return attestation.has_value();
    }
};

enum class NetworkIsolationHeartbeatReadDisposition {
    continue_waiting,
    clean_eof,
    error,
};

// Anonymous-pipe shutdown is clean only when the owning writer closes its end.
// Any other read failure must make the guard exit nonzero.
[[nodiscard]] NetworkIsolationHeartbeatReadDisposition
classify_network_isolation_heartbeat_read(
    bool read_succeeded,
    std::uint32_t byte_count,
    std::uint32_t native_error) noexcept;

// A successfully started object owns a dynamic WFP engine session. Destroying
// it removes the provider, sublayer and filters automatically. It never adds
// persistent rules.
class DynamicNetworkIsolationSession final {
public:
    DynamicNetworkIsolationSession() noexcept;
    ~DynamicNetworkIsolationSession();
    DynamicNetworkIsolationSession(DynamicNetworkIsolationSession&&) noexcept;
    DynamicNetworkIsolationSession& operator=(
        DynamicNetworkIsolationSession&&) noexcept;
    DynamicNetworkIsolationSession(const DynamicNetworkIsolationSession&) = delete;
    DynamicNetworkIsolationSession& operator=(
        const DynamicNetworkIsolationSession&) = delete;

    [[nodiscard]] static std::pair<DynamicNetworkIsolationSession,
                                   NetworkIsolationStartResult>
    start(const NetworkIsolationPolicy& policy) noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const NetworkIsolationAttestation& attestation() const noexcept;
    void close() noexcept;

private:
    struct Impl;
    explicit DynamicNetworkIsolationSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool windows_process_is_elevated() noexcept;

enum class NetworkIsolationCanaryStatus {
    success,
    ipv6_capability_unavailable,
    isolation_canary_unavailable,
    loopback_failed,
    non_loopback_not_denied,
    denied_without_os_classification,
};

struct NetworkIsolationCanaryResult final {
    NetworkIsolationCanaryStatus status{
        NetworkIsolationCanaryStatus::isolation_canary_unavailable};
    bool ipv4_loopback_allowed{false};
    bool ipv6_loopback_allowed{false};
    bool non_loopback_os_denied{false};
    std::uint32_t native_error{0U};
    // Exact number of probe helpers successfully launched by this call. This
    // remains zero until launch succeeds and is retained on every failure path.
    std::size_t processes_started{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return status == NetworkIsolationCanaryStatus::success;
    }
};

// This routine is intentionally conservative: if Windows cannot expose a
// deterministic non-loopback address and an immediate OS-denied connect, the
// result is typed unavailable and active stock launch must remain disabled.
[[nodiscard]] NetworkIsolationCanaryResult run_network_isolation_canary(
    const std::filesystem::path& probe_executable,
    const NetworkIsolationPolicy& policy,
    std::chrono::milliseconds timeout = std::chrono::seconds{10}) noexcept;

// Exercises a policy already owned by a separately monitored dynamic guard.
// It does not create or close WFP state. The caller must have included this
// exact probe image in that guard's policy. Probe helpers are owned by the
// supplied campaign job rather than by a second, independently scoped job.
[[nodiscard]] NetworkIsolationCanaryResult
run_network_isolation_canary_under_existing_guard(
    KillOnCloseProcessJob& campaign_job,
    const std::filesystem::path& probe_executable,
    std::chrono::milliseconds timeout = std::chrono::seconds{10}) noexcept;

[[nodiscard]] std::string_view to_string(NetworkIsolationErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(
    NetworkIsolationCanaryStatus status) noexcept;

} // namespace hlclient::platform::windows
