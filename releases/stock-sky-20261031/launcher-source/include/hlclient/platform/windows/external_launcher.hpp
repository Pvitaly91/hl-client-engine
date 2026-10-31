#pragma once
#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/platform/windows/binary_identity.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::platform::windows {
class SecureOutputDirectory;
[[nodiscard]] std::wstring external_widen(std::string_view);
[[nodiscard]] std::string external_utf8(std::wstring_view);
[[nodiscard]] std::optional<std::string> read_external_text(const std::filesystem::path&, std::size_t maximum_bytes);
[[nodiscard]] std::optional<core::SceneAntialiasingObservation> read_external_msaa_observation(
    const SecureOutputDirectory&,std::string_view expected_run_id);
enum class ExternalMsaaPublicationResult { unchanged, published, retry_pending, failed };
struct ExternalMsaaPublicationStatistics final {
    std::size_t attempts{},publications{},retryable_failures{};
};
// One session's fixed observation leaf only. Exact owned-file deletion and
// non-replacing publication tolerate a reader-held delete-pending gap using
// next-frame retries, never sleeps or foreign-leaf replacement. Retry counts
// are bounded; individual OS writes/flushes remain synchronous without a deadline.
class ExternalMsaaObservationPublisher final {
public:
    static constexpr unsigned maximum_consecutive_attempts=8;
    static constexpr std::size_t maximum_session_attempts=256;
    static constexpr auto retry_cooldown=std::chrono::milliseconds{50};
    [[nodiscard]] ExternalMsaaPublicationResult publish(
        const SecureOutputDirectory&,std::string_view json,std::string_view expected_run_id,
        std::chrono::steady_clock::time_point now);
    [[nodiscard]] const ExternalMsaaPublicationStatistics& statistics() const noexcept {return statistics_;}
    [[nodiscard]] bool retry_pending() const noexcept {return !failed_ && consecutive_attempts_!=0U;}
private:
    SecureOutputPublishedFile file_;
    std::string last_published_;
    std::chrono::steady_clock::time_point next_attempt_{};
    ExternalMsaaPublicationStatistics statistics_;
    unsigned consecutive_attempts_{};
    bool failed_{};
};
[[nodiscard]] bool write_external_text_atomic(const std::filesystem::path&, std::string_view, bool replace);
[[nodiscard]] std::string new_external_run_id();
struct ExternalSettingsLoad final {
    core::ExternalLaunchConfiguration configuration;
    std::string status{"defaults"};
    std::filesystem::path corrupt_backup;
};
[[nodiscard]] ExternalSettingsLoad load_external_settings(const std::filesystem::path&, const core::ExternalLaunchConfiguration& defaults);
[[nodiscard]] bool save_external_settings(const std::filesystem::path&, const core::ExternalLaunchConfiguration&);
// Returns only this user's project-owned settings location, without creating
// directories, opening resources or changing any stock configuration.
[[nodiscard]] std::optional<std::filesystem::path> default_external_settings_path();
// Read/merge/atomic-replace serialized against other project clients. Only
// fields explicitly edited since baseline overwrite the latest stored values.
[[nodiscard]] bool merge_external_settings(const std::filesystem::path&,
    const core::ExternalLaunchConfiguration& baseline,const core::ExternalLaunchConfiguration& edited);
struct ExternalChildStatus final {
    bool running{};
    std::optional<std::uint32_t> exit_code;
    std::uint32_t native_error{};
};
// Explicit per-launch override, never a process-wide or persisted environment edit.
// Historical values keep their meanings. Values 4/5 belong to preserved,
// rejected experiments and are deliberately not reassigned or accepted here.
enum class BackgroundSkyPolicy { original=0, depth=1, no_background=2, no_sky_color=3,
    world_sky_non_occluding=6 };
// Owns exactly one native child, its private cancel event and bounded log reader.
// Destructor closes handles only: normal launcher close is an explicit UI
// decision, never an implicit kill-on-job-close side effect.
class ExternalClientProcess final {
public:
    ExternalClientProcess(); ~ExternalClientProcess();
    ExternalClientProcess(const ExternalClientProcess&)=delete;
    ExternalClientProcess& operator=(const ExternalClientProcess&)=delete;
    [[nodiscard]] bool launch(const WindowsBinaryIdentity&, const std::vector<std::wstring>& arguments,
        bool add_cancel_argument=true);
    [[nodiscard]] bool launch(const WindowsBinaryIdentity&, const std::vector<std::wstring>& arguments,
        bool add_cancel_argument, std::optional<BackgroundSkyPolicy> background_sky_policy);
    void request_cancel() noexcept;
    [[nodiscard]] bool terminate_owned_child(std::uint32_t exit_code) noexcept;
    [[nodiscard]] ExternalChildStatus poll() noexcept;
    [[nodiscard]] std::uint32_t process_id() const noexcept;
    [[nodiscard]] std::uint64_t process_creation() const noexcept;
    [[nodiscard]] std::uint32_t launch_error() const noexcept;
    [[nodiscard]] BoundedProcessLogSnapshot log_snapshot() const;
    [[nodiscard]] BoundedProcessLogSnapshot finish_log();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Whitelist parser: never promotes arbitrary server/log prose into GUI state.
struct ExternalStatusMarker {std::string stage,reason;};
[[nodiscard]] std::optional<ExternalStatusMarker> latest_external_status_marker(std::string_view);
struct ExternalFailureMarkers {
    std::string primary_reason;
    std::string cleanup_reason;
};
// Current application phase is independent of immutable failures from previous
// attempts. A validated new marker wins; historical failures are a fallback
// only when no current marker exists. Cancellation retains native ownership.
[[nodiscard]] std::string external_progress_stage(std::string_view previous_stage,
    const std::optional<ExternalStatusMarker>& current_marker,
    const ExternalFailureMarkers& history,bool gameplay_receipt_ready,bool cancelled);
// Application shutdown is not gameplay acceptance, and cannot hide a nonzero
// native exit or forced termination even after a successful reconnect.
[[nodiscard]] bool external_application_close_succeeded(
    const std::optional<ExternalStatusMarker>& final_marker,
    std::optional<std::uint32_t> exit_code,bool forced) noexcept;
// Scan the bounded retained capture, not just the last status in a polling
// interval. A later cleanup failure must never replace the original error.
[[nodiscard]] ExternalFailureMarkers external_failure_markers(std::string_view);
[[nodiscard]] std::string redacted_external_status_log(std::string_view);
struct ExternalEmergencyCloseResult {bool exited{},forced{};std::optional<std::uint32_t> exit_code;};
// Last-resort launcher exception cleanup: cancel, wait 1600ms, terminate only
// the still-owned child handle, wait 2000ms. No PID/name search or server scope.
[[nodiscard]] ExternalEmergencyCloseResult close_external_child_after_failure(ExternalClientProcess&);
} // namespace hlclient::platform::windows


