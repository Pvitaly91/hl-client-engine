#include <hlclient/app/runtime_replay_scene_source.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace hlclient::app {
namespace {

[[nodiscard]] RuntimeReplaySourceError error(
    const RuntimeReplaySourceErrorCode code,
    std::string context,
    std::optional<goldsrc::RuntimeReplayError> session_error = std::nullopt,
    std::optional<RuntimeReplayVisualProjectionError> visual_error =
        std::nullopt)
{
    return RuntimeReplaySourceError{
        code, std::move(session_error), std::move(visual_error),
        std::move(context)};
}

[[nodiscard]] bool valid_fixture(
    const goldsrc::RuntimeReplayFixture& fixture) noexcept
{
    if (fixture.initialization.generation == 0U ||
        fixture.baseline_consumed_bits == 0U ||
        fixture.initialization.profile !=
            goldsrc::RuntimeReplayCompatibilityProfile::
                public_goldsrc48_runtime_replay_v1) {
        return false;
    }
    if (!fixture.presentation_offsets_seconds.empty() &&
        fixture.presentation_offsets_seconds.size() != fixture.records.size()) {
        return false;
    }
    double previous_offset = 0.0;
    for (const auto offset : fixture.presentation_offsets_seconds) {
        if (!std::isfinite(offset) || offset < previous_offset) {
            return false;
        }
        previous_offset = offset;
    }
    return std::all_of(
        fixture.records.begin(), fixture.records.end(),
        [&](const goldsrc::RuntimeReplayRecord& record) {
            return record.generation == fixture.initialization.generation &&
                record.profile == fixture.initialization.profile;
        });
}

[[nodiscard]] client::SceneUpdateResult update_failure(
    const RuntimeReplaySourceError& value)
{
    return client::SceneUpdateResult{
        false,
        std::string{to_string(value.code)} + ": " + value.context};
}

} // namespace

bool valid_runtime_replay_scheduling_limits(
    const RuntimeReplaySchedulingLimits& limits) noexcept
{
    return limits.maximum_records_per_update != 0U &&
        limits.maximum_records_per_update <=
            kMaximumApplicationReplayRecordsPerUpdate &&
        limits.maximum_payload_bytes_per_update != 0U &&
        limits.maximum_payload_bytes_per_update <=
            kMaximumApplicationReplayBytesPerUpdate;
}

RuntimeReplaySceneSource::RuntimeReplaySceneSource(
    goldsrc::RuntimeReplayFixture fixture,
    const RuntimeReplaySchedulingLimits limits,
    std::unique_ptr<RuntimeReplayVisualProjection> visual_projection) noexcept
    : fixture_{std::move(fixture)}, limits_{limits},
      visual_projection_{std::move(visual_projection)}
{
}

RuntimeReplaySceneSource::~RuntimeReplaySceneSource()
{
    finish_session_once();
}

RuntimeReplaySceneSourceCreateResult RuntimeReplaySceneSource::create(
    goldsrc::RuntimeReplayFixture fixture,
    const RuntimeReplaySchedulingLimits limits,
    const bool diagnostic_visuals,
    const RuntimeReplayVisualProjectionLimits visual_limits)
{
    if (!valid_fixture(fixture) ||
        !valid_runtime_replay_scheduling_limits(limits)) {
        return {{}, error(RuntimeReplaySourceErrorCode::invalid_configuration,
            "runtime replay source fixture or scheduling limits are invalid")};
    }
    std::unique_ptr<RuntimeReplayVisualProjection> visual_projection;
    if (diagnostic_visuals) {
        auto created = RuntimeReplayVisualProjection::create(visual_limits);
        if (!created || !created.projection) {
            return {{}, error(
                RuntimeReplaySourceErrorCode::invalid_configuration,
                created.error
                    ? created.error->context
                    : "unable to create diagnostic visual projection",
                std::nullopt,
                std::move(created.error))};
        }
        visual_projection = std::move(created.projection);
    }
    try {
        return {
            std::unique_ptr<RuntimeReplaySceneSource>{
                new RuntimeReplaySceneSource{std::move(fixture), limits,
                    std::move(visual_projection)}},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return {{}, error(RuntimeReplaySourceErrorCode::invalid_configuration,
            "unable to retain the application replay source")};
    }
}

RuntimeReplaySceneSourceCreateResult RuntimeReplaySceneSource::create_capture(
    const std::filesystem::path& exact_functional_run_directory,
    const RuntimeReplaySchedulingLimits limits,
    std::optional<std::filesystem::path> local_basedir,
    const std::string_view game, const bool paced)
{
    const auto loaded = goldsrc::RuntimeReplayCaptureLoader{}.load(
        exact_functional_run_directory);
    if (!loaded || !loaded.state) {
        RuntimeReplaySourceError source_error;
        source_error.code = RuntimeReplaySourceErrorCode::capture_load_failed;
        source_error.context = loaded.error
            ? loaded.error->context
            : "capture-backed runtime replay input is absent";
        source_error.capture_error = loaded.error;
        return {{}, std::move(source_error)};
    }
    goldsrc::RuntimeReplayFixture replay;
    try {
        replay.initialization = loaded.state->initialization();
        replay.records = loaded.state->records();
        replay.baseline_consumed_bits = loaded.state->baseline_consumed_bits();
    } catch (const std::bad_alloc&) {
        RuntimeReplaySourceError source_error;
        source_error.code = RuntimeReplaySourceErrorCode::capture_load_failed;
        source_error.context =
            "unable to retain capture-backed application replay input";
        source_error.capture_error = goldsrc::RuntimeReplayCaptureError{
            goldsrc::RuntimeReplayCaptureErrorCode::allocation_failed,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, 0U, {}, source_error.context};
        return {{}, std::move(source_error)};
    }
    auto created = create(std::move(replay), limits, false);
    if (!created || !created.source) {
        return created;
    }
    created.source->input_kind_ =
        RuntimeReplaySourceInputKind::functional_stock_capture;
    created.source->capture_summary_ = loaded.state->summary();
    if (local_basedir) {
        auto enabled = created.source->enable_local_assets(
            *loaded.state, *local_basedir, game, paced);
        if (!enabled) { return {{}, std::move(enabled.error)}; }
    }
    return created;
}

RuntimeReplaySourceOperationResult RuntimeReplaySceneSource::start()
{
    if (state_ != RuntimeReplaySourceState::not_started) {
        return {false, error(RuntimeReplaySourceErrorCode::already_started,
            "runtime replay source was already started")};
    }
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        fixture_.initialization, world_state_);
    if (!initialized || !initialized.session) {
        return fail(error(
            RuntimeReplaySourceErrorCode::session_initialization_failed,
            initialized.error ? initialized.error->context
                              : "RuntimeReplaySession initialization failed",
            std::move(initialized.error)));
    }
    session_ = std::move(initialized.session);
    if (local_assets_) {
        const auto projected=local_assets_->reset_generation(*world_state_.runtime_observation(),world_state_);
        if (!projected) { return fail(error(RuntimeReplaySourceErrorCode::visual_projection_failed,
            projected.error->context,std::nullopt,projected.error)); }
    }
    if (visual_projection_) {
        world_state_.set_camera({
            {0.0F, -180.0F, 80.0F},
            {0.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F},
            0.872664626F,
            1.0F,
            1'024.0F,
        });
        const auto& observation = world_state_.runtime_observation();
        const auto projected = observation
            ? visual_projection_->reset_generation(*observation, world_state_)
            : RuntimeReplayVisualProjectionResult{};
        if (!projected) {
            return fail(error(
                RuntimeReplaySourceErrorCode::visual_projection_failed,
                projected.error
                    ? projected.error->context
                    : "runtime replay generation visual reset failed",
                std::nullopt,
                projected.error));
        }
    }
    state_ = RuntimeReplaySourceState::running;
    if (fixture_.records.empty()) {
        finish_session_once();
        state_ = RuntimeReplaySourceState::completed;
    }
    return {true, std::nullopt};
}

RuntimeReplaySourceOperationResult RuntimeReplaySceneSource::restart(
    goldsrc::RuntimeReplayFixture fixture)
{
    if (!valid_fixture(fixture)) {
        return {false, error(RuntimeReplaySourceErrorCode::invalid_configuration,
            "replacement replay fixture is invalid")};
    }
    if (fixture.initialization.generation <=
        fixture_.initialization.generation) {
        return {false, error(
            RuntimeReplaySourceErrorCode::restart_generation_not_newer,
            "restart requires a strictly newer generation")};
    }

    finish_session_once();
    session_.reset();
    world_state_.clear_runtime_observation();
    world_state_.clear_dynamic_entities();
    world_state_.clear_static_world();
    local_assets_.reset();
    fixture_ = std::move(fixture);
    next_record_ = 0U;
    applied_records_ = 0U;
    failed_records_ = 0U;
    visual_failed_records_ = 0U;
    application_updates_ = 0U;
    session_finish_count_ = 0U;
    presentation_elapsed_seconds_ = 0.0;
    state_ = RuntimeReplaySourceState::not_started;
    last_error_.reset();
    input_kind_ = RuntimeReplaySourceInputKind::builtin_fixture;
    capture_summary_.reset();
    return start();
}

void RuntimeReplaySceneSource::stop(const RuntimeReplayStopReason reason)
{
    if (terminal()) {
        return;
    }
    finish_session_once();
    state_ = RuntimeReplaySourceState::stopped;
    if (reason == RuntimeReplayStopReason::application_update_limit_reached) {
        last_error_ = error(
            RuntimeReplaySourceErrorCode::application_update_limit_reached,
            "application update limit was reached before replay EOF");
    } else {
        last_error_ = error(
            RuntimeReplaySourceErrorCode::explicitly_stopped,
            "runtime replay was explicitly stopped before EOF");
    }
}

client::SceneUpdateResult RuntimeReplaySceneSource::update(
    const client::FrameTime elapsed)
{
    if (state_ != RuntimeReplaySourceState::running || !session_) {
        const auto value = error(RuntimeReplaySourceErrorCode::not_running,
            "runtime replay source is not running");
        return update_failure(value);
    }
    ++application_updates_;
    if (!std::isfinite(elapsed.count()) || elapsed.count() < 0.0) {
        const auto result = fail(error(
            RuntimeReplaySourceErrorCode::invalid_elapsed_time,
            "application elapsed time must be finite and non-negative"));
        return update_failure(*result.error);
    }
    world_state_.advance(elapsed);
    presentation_elapsed_seconds_ += elapsed.count();

    std::size_t records_this_update = 0U;
    std::size_t bytes_this_update = 0U;
    while (next_record_ < fixture_.records.size() &&
           records_this_update < limits_.maximum_records_per_update) {
        if (!fixture_.presentation_offsets_seconds.empty() &&
            presentation_elapsed_seconds_ <
                fixture_.presentation_offsets_seconds[next_record_]) {
            break;
        }
        const auto& record = fixture_.records[next_record_];
        const auto record_bytes = record.payload.bytes.size();
        if (record_bytes > limits_.maximum_payload_bytes_per_update) {
            ++next_record_;
            ++failed_records_;
            const auto result = fail(error(
                RuntimeReplaySourceErrorCode::record_too_large,
                "record exceeds the application per-update byte budget"));
            return update_failure(*result.error);
        }
        if (record_bytes >
            limits_.maximum_payload_bytes_per_update - bytes_this_update) {
            break;
        }

        const auto applied = session_->apply_record(record);
        ++next_record_;
        if (!applied) {
            ++failed_records_;
            const auto result = fail(error(
                RuntimeReplaySourceErrorCode::record_failed,
                applied.error ? applied.error->context
                              : "RuntimeReplaySession rejected a record",
                applied.error));
            return update_failure(*result.error);
        }
        ++applied_records_;
        if (local_assets_) {
            if (applied.event && applied.event->entities_observed) {
                const auto projected=local_assets_->project_entities(*world_state_.runtime_observation(),world_state_);
                if (!projected) {
                    ++visual_failed_records_;
                    const auto result=fail(error(RuntimeReplaySourceErrorCode::visual_projection_failed,
                        projected.error->context,std::nullopt,projected.error));
                    return update_failure(*result.error);
                }
            } else { local_assets_->acknowledge_unchanged(); }
        }
        if (visual_projection_) {
            const auto& observation = world_state_.runtime_observation();
            const auto projected = observation
                ? (applied.event && applied.event->entities_observed
                    ? visual_projection_->project_entities(
                          *observation, world_state_)
                    : visual_projection_->acknowledge_unchanged(*observation))
                : RuntimeReplayVisualProjectionResult{};
            if (!projected) {
                ++visual_failed_records_;
                const auto result = fail(error(
                    RuntimeReplaySourceErrorCode::visual_projection_failed,
                    projected.error
                        ? projected.error->context
                        : "committed runtime record could not be projected",
                    std::nullopt,
                    projected.error));
                return update_failure(*result.error);
            }
        }
        ++records_this_update;
        bytes_this_update += record_bytes;
    }

    if (next_record_ == fixture_.records.size()) {
        finish_session_once();
        state_ = RuntimeReplaySourceState::completed;
    }
    return {};
}

const client::ClientWorldState& RuntimeReplaySceneSource::world_state()
    const noexcept
{
    return world_state_;
}

bool RuntimeReplaySceneSource::terminal() const noexcept
{
    return state_ == RuntimeReplaySourceState::completed ||
        state_ == RuntimeReplaySourceState::failed ||
        state_ == RuntimeReplaySourceState::stopped;
}

RuntimeReplaySourceState RuntimeReplaySceneSource::state() const noexcept
{
    return state_;
}

RuntimeReplaySourceSummary RuntimeReplaySceneSource::summary() const
{
    return RuntimeReplaySourceSummary{
        fixture_.kind,
        fixture_.initialization.profile,
        fixture_.initialization.generation,
        fixture_.records.size(),
        applied_records_,
        failed_records_,
        visual_failed_records_,
        fixture_.records.size() - next_record_,
        application_updates_,
        session_finish_count_,
        state_,
        visual_projection_
            ? std::optional{visual_projection_->summary()}
            : std::nullopt,
        last_error_,
        input_kind_,
        capture_summary_};
}

bool RuntimeReplaySceneSource::diagnostic_visuals_enabled() const noexcept
{
    return visual_projection_ != nullptr;
}

RuntimeReplaySourceOperationResult RuntimeReplaySceneSource::enable_local_assets(
    const goldsrc::RuntimeReplayCaptureState& capture,
    const std::filesystem::path& basedir, std::string_view game, bool paced)
{
    if (state_!=RuntimeReplaySourceState::not_started || visual_projection_ ||
        input_kind_!=RuntimeReplaySourceInputKind::functional_stock_capture ||
        !capture_summary_ || capture.summary().corpus_structural_sha256 != capture_summary_->corpus_structural_sha256) {
        return {false,error(RuntimeReplaySourceErrorCode::invalid_configuration,"local assets require the exact unstarted capture source")};
    }
    auto created=RuntimeReplayLocalAssets::create(capture,basedir,game);
    if (!created.projection) { return {false,error(RuntimeReplaySourceErrorCode::visual_projection_failed,created.error->context)}; }
    local_assets_=std::move(created.projection);
    if (paced) { fixture_.presentation_offsets_seconds=capture.presentation_offsets_seconds(); }
    return {true,{}};
}

const std::optional<RuntimeReplaySourceError>&
RuntimeReplaySceneSource::last_error() const noexcept
{
    return last_error_;
}

RuntimeReplaySourceOperationResult RuntimeReplaySceneSource::fail(
    RuntimeReplaySourceError value)
{
    finish_session_once();
    state_ = RuntimeReplaySourceState::failed;
    last_error_ = std::move(value);
    return {false, last_error_};
}

void RuntimeReplaySceneSource::finish_session_once() noexcept
{
    if (session_ &&
        session_->status() == goldsrc::RuntimeReplaySessionStatus::active) {
        session_->finish();
        ++session_finish_count_;
    }
}

std::string_view to_string(const RuntimeReplaySourceState state) noexcept
{
    switch (state) {
    case RuntimeReplaySourceState::not_started: return "not_started";
    case RuntimeReplaySourceState::running: return "running";
    case RuntimeReplaySourceState::completed: return "completed";
    case RuntimeReplaySourceState::failed: return "failed";
    case RuntimeReplaySourceState::stopped: return "stopped";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplaySourceInputKind kind) noexcept
{
    switch (kind) {
    case RuntimeReplaySourceInputKind::builtin_fixture:
        return "offline_builtin_fixture";
    case RuntimeReplaySourceInputKind::functional_stock_capture:
        return "functional_stock_capture";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplaySourceErrorCode code) noexcept
{
    switch (code) {
    case RuntimeReplaySourceErrorCode::invalid_configuration:
        return "invalid_configuration";
    case RuntimeReplaySourceErrorCode::already_started:
        return "already_started";
    case RuntimeReplaySourceErrorCode::not_running: return "not_running";
    case RuntimeReplaySourceErrorCode::invalid_elapsed_time:
        return "invalid_elapsed_time";
    case RuntimeReplaySourceErrorCode::session_initialization_failed:
        return "session_initialization_failed";
    case RuntimeReplaySourceErrorCode::capture_load_failed:
        return "capture_load_failed";
    case RuntimeReplaySourceErrorCode::record_too_large:
        return "record_too_large";
    case RuntimeReplaySourceErrorCode::record_failed: return "record_failed";
    case RuntimeReplaySourceErrorCode::visual_projection_failed:
        return "visual_projection_failed";
    case RuntimeReplaySourceErrorCode::restart_generation_not_newer:
        return "restart_generation_not_newer";
    case RuntimeReplaySourceErrorCode::application_update_limit_reached:
        return "application_update_limit_reached";
    case RuntimeReplaySourceErrorCode::explicitly_stopped:
        return "explicitly_stopped";
    }
    return "unknown";
}

} // namespace hlclient::app
