#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <hlclient/platform/windows/binary_identity.hpp>

namespace hlclient::platform::windows {

struct BoundedProcessLogLimits final {
    std::size_t maximum_bytes{1U * 1'024U * 1'024U};
    std::size_t maximum_line_length{4'096U};
    std::size_t maximum_line_count{8'192U};
};

struct BoundedProcessLogSnapshot final {
    std::string bytes;
    std::size_t observed_bytes{0U};
    std::size_t observed_line_count{0U};
    std::size_t maximum_observed_line_length{0U};
    bool byte_truncated{false};
    bool line_count_truncated{false};
    bool line_length_truncated{false};
    bool capture_failed{false};
    std::uint32_t native_error{0U};
};

[[nodiscard]] bool validate_bounded_process_log_limits(
    const BoundedProcessLogLimits& limits) noexcept;
[[nodiscard]] bool bounded_process_log_snapshot_complete(
    const BoundedProcessLogSnapshot& snapshot) noexcept;

enum class BoundedProcessLogReadDisposition {
    data,
    clean_eof,
    error,
};

[[nodiscard]] BoundedProcessLogReadDisposition
classify_bounded_process_log_read(
    bool read_succeeded,
    std::uint32_t byte_count,
    std::uint32_t native_error,
    bool locally_cancelled) noexcept;

class BoundedProcessLogCapture final {
public:
    BoundedProcessLogCapture() noexcept;
    ~BoundedProcessLogCapture();
    BoundedProcessLogCapture(BoundedProcessLogCapture&&) noexcept;
    BoundedProcessLogCapture& operator=(BoundedProcessLogCapture&&) noexcept;
    BoundedProcessLogCapture(const BoundedProcessLogCapture&) = delete;
    BoundedProcessLogCapture& operator=(const BoundedProcessLogCapture&) = delete;

    [[nodiscard]] static std::optional<BoundedProcessLogCapture> create(
        const BoundedProcessLogLimits& limits) noexcept;
    [[nodiscard]] void* inherited_write_handle() const noexcept;
    // Called after the child inherited its duplicate; the parent must not keep
    // a writer alive or EOF could never be observed.
    void close_parent_write_handle() noexcept;
    [[nodiscard]] BoundedProcessLogSnapshot snapshot() const;
    [[nodiscard]] BoundedProcessLogSnapshot finish(
        std::chrono::milliseconds timeout = std::chrono::seconds{2}) noexcept;

private:
    struct Impl;
    explicit BoundedProcessLogCapture(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::wstring quote_windows_command_line_argument(
    std::wstring_view argument);
[[nodiscard]] std::wstring build_windows_command_line(
    const std::filesystem::path& executable,
    std::span<const std::wstring> arguments);

enum class OwnedProcessErrorCode {
    none,
    invalid_specification,
    job_creation_failed,
    job_limit_failed,
    preexisting_process,
    command_line_too_long,
    startup_attribute_failed,
    create_process_failed,
    assign_job_failed,
    child_cleanup_unconfirmed,
    process_identity_mismatch,
    resume_failed,
    process_query_failed,
    process_timeout,
    process_exit_unexpected,
    log_capture_failed,
    unexpected_child_process,
};

enum class OwnedJobCleanupErrorCode {
    none,
    invalid_job,
    terminate_failed,
    failed_child_cleanup_failed,
    accounting_query_failed,
    timeout,
};

struct OwnedJobCleanupResult final {
    OwnedJobCleanupErrorCode code{OwnedJobCleanupErrorCode::none};
    std::uint32_t native_error{0U};
    std::size_t active_process_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == OwnedJobCleanupErrorCode::none &&
               active_process_count == 0U;
    }
};

struct OwnedProcessLaunchSpec final {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
    std::filesystem::path working_directory;
    WindowsBinaryIdentity expected_identity;
    void* stdout_handle{nullptr};
    void* stderr_handle{nullptr};
    std::vector<void*> additional_inherited_handles;
    bool prohibit_child_processes{true};
    // CREATE_NO_WINDOW is incompatible with the child-process restriction on
    // affected Windows builds. Console tools inherit the orchestrator console;
    // every standard handle is still explicitly constrained below.
    bool create_no_window{false};
};

struct OwnedProcessLaunchResult final {
    OwnedProcessErrorCode code{OwnedProcessErrorCode::none};
    std::uint32_t native_error{0U};
    std::uint32_t process_id{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == OwnedProcessErrorCode::none;
    }
};

enum class StockRuntimeStartupStage {
    initial,
    guard_running,
    guard_ready,
    active_canary_passed,
    run_root_ready,
    relay_running,
    relay_ready,
    server_running,
    server_ready,
    client_running,
    capture_running,
    cleanup_in_progress,
    complete,
    failed,
};

enum class StockRuntimeStartupEvent {
    guard_started,
    guard_readiness_observed,
    active_canary_succeeded,
    run_root_created,
    relay_started,
    relay_readiness_observed,
    server_started,
    server_readiness_observed,
    client_started,
    client_readiness_observed,
    relay_readiness_timeout,
    server_banner_mismatch,
    server_readiness_timeout,
    client_readiness_timeout,
    guard_early_exit,
    relay_early_exit,
    server_early_exit,
    client_early_exit,
    cancellation_requested,
    cleanup_completed,
    cleanup_inexact,
};

enum class StockRuntimeStartupFailure {
    none,
    out_of_order,
    relay_not_ready,
    server_banner_mismatch,
    server_timeout,
    client_timeout,
    guard_early_exit,
    relay_early_exit,
    server_early_exit,
    client_early_exit,
    cleanup_inexact,
};

struct StockRuntimeStartupState final {
    StockRuntimeStartupStage stage{StockRuntimeStartupStage::initial};
    StockRuntimeStartupFailure failure{StockRuntimeStartupFailure::none};
};

// Pure, allocation-free startup ordering boundary shared by active orchestration
// and fake failure-path tests.
[[nodiscard]] bool apply_stock_runtime_startup_event(
    StockRuntimeStartupState& state,
    StockRuntimeStartupEvent event) noexcept;
[[nodiscard]] std::string_view to_string(
    StockRuntimeStartupFailure failure) noexcept;

class OwnedProcess final {
public:
    OwnedProcess() noexcept;
    ~OwnedProcess();
    OwnedProcess(OwnedProcess&&) noexcept;
    OwnedProcess& operator=(OwnedProcess&&) noexcept;
    OwnedProcess(const OwnedProcess&) = delete;
    OwnedProcess& operator=(const OwnedProcess&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t process_id() const noexcept;
    [[nodiscard]] void* native_process_handle() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> wait(
        std::chrono::milliseconds timeout) noexcept;
    void terminate(std::uint32_t exit_code) noexcept;

private:
    friend class KillOnCloseProcessJob;
    struct Impl;
    explicit OwnedProcess(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

class KillOnCloseProcessJob final {
public:
    KillOnCloseProcessJob() noexcept;
    ~KillOnCloseProcessJob();
    KillOnCloseProcessJob(KillOnCloseProcessJob&&) noexcept;
    KillOnCloseProcessJob& operator=(KillOnCloseProcessJob&&) noexcept;
    KillOnCloseProcessJob(const KillOnCloseProcessJob&) = delete;
    KillOnCloseProcessJob& operator=(const KillOnCloseProcessJob&) = delete;

    [[nodiscard]] static std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>
    create(std::size_t maximum_processes) noexcept;
    // Adopts this process's inherited copy of a wrapper-owned Job handle. The
    // wrapper retains its own handle for crash-safe exact cleanup accounting.
    [[nodiscard]] static std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>
    adopt_inherited(void* job_handle, std::size_t maximum_processes) noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::optional<std::size_t> active_process_count() const noexcept;
    [[nodiscard]] std::pair<OwnedProcess, OwnedProcessLaunchResult> launch(
        const OwnedProcessLaunchSpec& spec) noexcept;
    void terminate(std::uint32_t exit_code) noexcept;
    [[nodiscard]] OwnedJobCleanupResult terminate_and_wait(
        std::uint32_t exit_code,
        std::chrono::milliseconds timeout) noexcept;
    void close() noexcept;

private:
    struct Impl;
    explicit KillOnCloseProcessJob(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool stock_runtime_graceful_cleanup_is_exact(
    const std::optional<std::uint32_t>& client_exit,
    const std::optional<std::uint32_t>& server_exit,
    const std::optional<std::uint32_t>& relay_exit,
    const std::optional<std::uint32_t>& guard_exit,
    const std::optional<std::size_t>& active_process_count) noexcept;

[[nodiscard]] bool stock_runtime_stock_process_shutdown_confirmed(
    const std::optional<std::uint32_t>& client_exit,
    const std::optional<std::uint32_t>& server_exit) noexcept;

// A redundant WFP owner may be released only after both owned Jobs have
// independently produced exact zero-process cleanup results.
[[nodiscard]] bool stock_runtime_owned_jobs_allow_isolation_release(
    const OwnedJobCleanupResult& campaign_cleanup,
    const std::optional<OwnedJobCleanupResult>& guard_cleanup) noexcept;

enum class ExactImageProcessScanErrorCode {
    none,
    snapshot_failed,
    enumeration_failed,
    process_open_failed,
    process_path_query_failed,
    process_identity_query_failed,
};

struct ExactImageProcessScanResult final {
    ExactImageProcessScanErrorCode code{ExactImageProcessScanErrorCode::none};
    std::uint32_t native_error{0U};
    std::uint32_t process_id{0U};
    std::vector<std::uint32_t> process_ids;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == ExactImageProcessScanErrorCode::none;
    }
};

[[nodiscard]] ExactImageProcessScanResult
find_processes_with_exact_image_identity(
    const WindowsBinaryIdentity& identity) noexcept;

struct HldsRuntimeProfile final {
    WindowsFileVersion engine_version{};
    std::uint32_t protocol{0U};
    std::uint32_t build{0U};
    std::string game;
    std::string map;
    std::uint16_t port{0U};
    bool ready{false};
};

enum class HldsRuntimeProfileField {
    none,
    engine_version,
    runtime_mode,
    game,
    protocol,
    build,
    endpoint_address,
    endpoint_port,
    map,
    duplicate_field,
    missing_field,
    malformed_field,
    process_log_truncated,
};

enum class HldsRuntimeProfileParseStatus {
    incomplete,
    valid,
    profile_mismatch,
    malformed,
    duplicate_field,
    process_log_truncated,
};

enum class HldsRuntimeProfileFieldStatus {
    match,
    mismatch,
    absent,
    malformed,
};

enum class HldsRuntimeEndpointAddressCategory {
    loopback_ipv4,
    loopback_ipv6,
    non_loopback,
    malformed,
    absent,
};

enum class HldsRuntimeModeCategory {
    stdio,
    other,
    malformed,
    absent,
};

struct HldsRuntimeProfileFieldDiagnostic final {
    HldsRuntimeProfileFieldStatus status{
        HldsRuntimeProfileFieldStatus::absent};
    bool present{false};
    bool duplicate{false};
    bool syntax_valid{false};
    bool matches{false};
};

// A value-only, bounded diagnostic. It deliberately cannot retain source
// lines, banner suffixes, paths, hostnames, or arbitrary process output.
struct HldsRuntimeProfileDiagnostic final {
    HldsRuntimeProfileParseStatus parse_status{
        HldsRuntimeProfileParseStatus::incomplete};
    HldsRuntimeProfileField mismatch_field{
        HldsRuntimeProfileField::missing_field};
    HldsRuntimeProfileFieldDiagnostic engine_version;
    HldsRuntimeProfileFieldDiagnostic runtime_mode;
    HldsRuntimeProfileFieldDiagnostic game;
    HldsRuntimeProfileFieldDiagnostic protocol;
    HldsRuntimeProfileFieldDiagnostic build;
    HldsRuntimeProfileFieldDiagnostic endpoint_address;
    HldsRuntimeProfileFieldDiagnostic endpoint_port;
    HldsRuntimeProfileFieldDiagnostic map;
    std::optional<WindowsFileVersion> observed_engine_version;
    std::optional<std::uint32_t> observed_protocol;
    std::optional<std::uint32_t> observed_build;
    HldsRuntimeEndpointAddressCategory endpoint_address_category{
        HldsRuntimeEndpointAddressCategory::absent};
    HldsRuntimeModeCategory runtime_mode_category{
        HldsRuntimeModeCategory::absent};
    bool endpoint_port_matches{false};
    bool map_matches{false};
    bool game_matches{false};
    std::size_t duplicate_field_count{0U};
    std::size_t observed_byte_count{0U};
    std::size_t observed_line_count{0U};
    bool process_log_truncated{false};
};

enum class HldsBannerParseErrorCode {
    none,
    too_large,
    line_too_long,
    duplicate_field,
    malformed,
    missing_field,
    profile_mismatch,
    process_log_truncated,
};

struct HldsBannerParseResult final {
    std::optional<HldsRuntimeProfile> profile;
    HldsBannerParseErrorCode code{HldsBannerParseErrorCode::none};
    HldsRuntimeProfileDiagnostic diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return profile.has_value();
    }
};

// Parses only literal bounded profile lines known to be printed by HLDS. It
// does not infer a version/build from a filename or arbitrary substrings.
[[nodiscard]] HldsBannerParseResult parse_required_hlds_runtime_banner(
    std::string_view output,
    std::string_view requested_map,
    std::uint16_t requested_port) noexcept;
[[nodiscard]] HldsBannerParseResult diagnose_required_hlds_runtime_banner(
    const BoundedProcessLogSnapshot& snapshot,
    std::string_view requested_map,
    std::uint16_t requested_port) noexcept;

[[nodiscard]] std::string_view to_string(OwnedProcessErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(OwnedJobCleanupErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(
    ExactImageProcessScanErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(HldsBannerParseErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(HldsRuntimeProfileField field) noexcept;
[[nodiscard]] std::string_view to_string(
    HldsRuntimeProfileParseStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    HldsRuntimeProfileFieldStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    HldsRuntimeEndpointAddressCategory category) noexcept;
[[nodiscard]] std::string_view to_string(
    HldsRuntimeModeCategory category) noexcept;

} // namespace hlclient::platform::windows
