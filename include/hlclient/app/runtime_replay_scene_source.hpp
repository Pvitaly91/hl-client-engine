#pragma once
#include <hlclient/app/runtime_replay_local_assets.hpp>

#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/app/runtime_replay_visual_projection.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/runtime_replay_capture.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::app {

inline constexpr std::size_t kMaximumApplicationReplayRecordsPerUpdate = 1'024U;
inline constexpr std::size_t kMaximumApplicationReplayBytesPerUpdate =
    16U * 1'024U * 1'024U;

struct RuntimeReplaySchedulingLimits final {
    std::size_t maximum_records_per_update{2U};
    std::size_t maximum_payload_bytes_per_update{1U * 1'024U * 1'024U};

    [[nodiscard]] friend bool operator==(
        const RuntimeReplaySchedulingLimits&,
        const RuntimeReplaySchedulingLimits&) noexcept = default;
};

enum class RuntimeReplaySourceState : std::uint8_t {
    not_started,
    running,
    completed,
    failed,
    stopped,
};

enum class RuntimeReplaySourceInputKind : std::uint8_t {
    builtin_fixture,
    functional_stock_capture,
};

enum class RuntimeReplayStopReason : std::uint8_t {
    explicit_stop,
    application_update_limit_reached,
};

enum class RuntimeReplaySourceErrorCode : std::uint8_t {
    invalid_configuration,
    already_started,
    not_running,
    invalid_elapsed_time,
    session_initialization_failed,
    capture_load_failed,
    record_too_large,
    record_failed,
    visual_projection_failed,
    restart_generation_not_newer,
    application_update_limit_reached,
    explicitly_stopped,
};

struct RuntimeReplaySourceError final {
    RuntimeReplaySourceErrorCode code{
        RuntimeReplaySourceErrorCode::invalid_configuration};
    std::optional<goldsrc::RuntimeReplayError> session_error;
    std::optional<RuntimeReplayVisualProjectionError> visual_error;
    std::string context;
    std::optional<goldsrc::RuntimeReplayCaptureError> capture_error;
};

struct RuntimeReplaySourceOperationResult final {
    bool succeeded{false};
    std::optional<RuntimeReplaySourceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return succeeded && !error.has_value();
    }
};

struct RuntimeReplaySourceSummary final {
    goldsrc::RuntimeReplayFixtureKind fixture{
        goldsrc::RuntimeReplayFixtureKind::basic_mixed};
    goldsrc::RuntimeReplayCompatibilityProfile profile{
        goldsrc::RuntimeReplayCompatibilityProfile::
            public_goldsrc48_runtime_replay_v1};
    std::uint64_t generation{0U};
    std::size_t input_records{0U};
    std::size_t applied_records{0U};
    std::size_t failed_records{0U};
    std::size_t visual_failed_records{0U};
    std::size_t pending_records{0U};
    std::size_t application_updates{0U};
    std::size_t session_finish_count{0U};
    RuntimeReplaySourceState state{RuntimeReplaySourceState::not_started};
    std::optional<RuntimeReplayVisualProjectionSummary> visual;
    std::optional<RuntimeReplaySourceError> terminal_error;
    RuntimeReplaySourceInputKind input_kind{
        RuntimeReplaySourceInputKind::builtin_fixture};
    std::optional<goldsrc::RuntimeReplayCaptureSummary> capture;
};

struct RuntimeReplaySceneSourceCreateResult final {
    std::unique_ptr<class RuntimeReplaySceneSource> source;
    std::optional<RuntimeReplaySourceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return source != nullptr && !error.has_value();
    }
};

class RuntimeReplaySceneSource final : public client::IClientSceneSource {
public:
    RuntimeReplaySceneSource(const RuntimeReplaySceneSource&) = delete;
    RuntimeReplaySceneSource& operator=(const RuntimeReplaySceneSource&) = delete;
    RuntimeReplaySceneSource(RuntimeReplaySceneSource&&) = delete;
    RuntimeReplaySceneSource& operator=(RuntimeReplaySceneSource&&) = delete;
    ~RuntimeReplaySceneSource() override;

    [[nodiscard]] static RuntimeReplaySceneSourceCreateResult create(
        goldsrc::RuntimeReplayFixture fixture,
        RuntimeReplaySchedulingLimits limits = {},
        bool diagnostic_visuals = false,
        RuntimeReplayVisualProjectionLimits visual_limits = {});
    [[nodiscard]] static RuntimeReplaySceneSourceCreateResult create_capture(
        const std::filesystem::path& exact_functional_run_directory,
        RuntimeReplaySchedulingLimits limits = {},
        std::optional<std::filesystem::path> local_basedir = std::nullopt,
        std::string_view game = "valve", bool paced = false);

    [[nodiscard]] RuntimeReplaySourceOperationResult start();
    [[nodiscard]] RuntimeReplaySourceOperationResult restart(
        goldsrc::RuntimeReplayFixture fixture);
    void stop(RuntimeReplayStopReason reason =
        RuntimeReplayStopReason::explicit_stop);

    [[nodiscard]] client::SceneUpdateResult update(
        client::FrameTime elapsed) override;
    [[nodiscard]] const client::ClientWorldState& world_state()
        const noexcept override;

    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] RuntimeReplaySourceState state() const noexcept;
    [[nodiscard]] RuntimeReplaySourceSummary summary() const;
    [[nodiscard]] const std::optional<RuntimeReplaySourceError>& last_error()
        const noexcept;
    [[nodiscard]] bool diagnostic_visuals_enabled() const noexcept;
    [[nodiscard]] bool visuals_enabled() const noexcept { return diagnostic_visuals_enabled() || local_assets_ != nullptr; }
    [[nodiscard]] const RuntimeReplayLocalAssets* local_assets() const noexcept { return local_assets_.get(); }
    [[nodiscard]] bool presentation_paced() const noexcept { return !fixture_.presentation_offsets_seconds.empty(); }
    [[nodiscard]] double presentation_duration_seconds() const noexcept { return presentation_paced() ? fixture_.presentation_offsets_seconds.back() : 0.0; }
    [[nodiscard]] RuntimeReplaySourceOperationResult enable_local_assets(
        const goldsrc::RuntimeReplayCaptureState& capture,
        const std::filesystem::path& basedir, std::string_view game, bool paced);

private:
    RuntimeReplaySceneSource(
        goldsrc::RuntimeReplayFixture fixture,
        RuntimeReplaySchedulingLimits limits,
        std::unique_ptr<RuntimeReplayVisualProjection> visual_projection)
        noexcept;

    [[nodiscard]] RuntimeReplaySourceOperationResult fail(
        RuntimeReplaySourceError error);
    void finish_session_once() noexcept;

    goldsrc::RuntimeReplayFixture fixture_;
    RuntimeReplaySchedulingLimits limits_;
    client::ClientWorldState world_state_;
    std::unique_ptr<goldsrc::RuntimeReplaySession> session_;
    std::unique_ptr<RuntimeReplayVisualProjection> visual_projection_;
    std::unique_ptr<RuntimeReplayLocalAssets> local_assets_;
    std::size_t next_record_{0U};
    std::size_t applied_records_{0U};
    std::size_t failed_records_{0U};
    std::size_t visual_failed_records_{0U};
    std::size_t application_updates_{0U};
    std::size_t session_finish_count_{0U};
    double presentation_elapsed_seconds_{0.0};
    RuntimeReplaySourceState state_{RuntimeReplaySourceState::not_started};
    std::optional<RuntimeReplaySourceError> last_error_;
    RuntimeReplaySourceInputKind input_kind_{
        RuntimeReplaySourceInputKind::builtin_fixture};
    std::optional<goldsrc::RuntimeReplayCaptureSummary> capture_summary_;
};

[[nodiscard]] bool valid_runtime_replay_scheduling_limits(
    const RuntimeReplaySchedulingLimits& limits) noexcept;
[[nodiscard]] std::string_view to_string(RuntimeReplaySourceState state) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplaySourceInputKind kind) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplaySourceErrorCode code) noexcept;

} // namespace hlclient::app
