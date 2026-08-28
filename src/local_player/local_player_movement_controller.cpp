#include <hlclient/local_player/local_player_movement_controller.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::local_player {
namespace {

[[nodiscard]] bool finite_speed_config(
    const goldsrc::GoldSrcUserCmdMovementSpeedConfig& config) noexcept
{
    return std::isfinite(config.forward_speed) &&
        std::isfinite(config.backward_speed) &&
        std::isfinite(config.side_speed) && config.forward_speed >= 0.0F &&
        config.backward_speed >= 0.0F && config.side_speed >= 0.0F;
}

[[nodiscard]] bool same_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool checked_add(
    std::uint64_t& destination,
    const std::uint64_t value) noexcept
{
    if (destination > UINT64_MAX - value) {
        return false;
    }
    destination += value;
    return true;
}

[[nodiscard]] bool add_statistics(
    movement::PlayerMovementStatistics& destination,
    const movement::PlayerMovementStatistics& source) noexcept
{
    return checked_add(destination.command_count, source.command_count) &&
        checked_add(destination.substep_count, source.substep_count) &&
        checked_add(
            destination.grounded_command_count,
            source.grounded_command_count) &&
        checked_add(
            destination.airborne_command_count,
            source.airborne_command_count) &&
        checked_add(destination.ground_probe_count, source.ground_probe_count) &&
        checked_add(destination.trace_count, source.trace_count) &&
        checked_add(
            destination.collision_hit_count,
            source.collision_hit_count) &&
        checked_add(destination.slide_bump_count, source.slide_bump_count) &&
        checked_add(destination.clip_plane_count, source.clip_plane_count) &&
        checked_add(destination.step_attempt_count, source.step_attempt_count) &&
        checked_add(destination.step_success_count, source.step_success_count) &&
        checked_add(destination.jump_count, source.jump_count) &&
        checked_add(destination.duck_enter_count, source.duck_enter_count) &&
        checked_add(destination.duck_exit_count, source.duck_exit_count) &&
        checked_add(destination.stand_blocked_count, source.stand_blocked_count) &&
        checked_add(destination.start_solid_count, source.start_solid_count) &&
        checked_add(destination.all_solid_count, source.all_solid_count) &&
        std::isfinite(
            destination.total_horizontal_distance +
            source.total_horizontal_distance) &&
        std::isfinite(
            destination.total_vertical_distance +
            source.total_vertical_distance) &&
        ((destination.total_horizontal_distance +=
              source.total_horizontal_distance),
         true) &&
        ((destination.total_vertical_distance += source.total_vertical_distance),
         true);
}

[[nodiscard]] LocalPlayerMovementControllerUpdateResult failure(
    const LocalPlayerMovementControllerErrorCode code,
    const movement::LocalPlayerMovementState* state,
    const gameplay_camera::GameplayCameraState* camera,
    movement::PlayerMovementStatistics statistics,
    const std::size_t generated_commands,
    const std::string_view context) noexcept
{
    LocalPlayerMovementControllerUpdateResult result;
    if (state != nullptr) {
        result.player_state.emplace(*state);
        result.final_state_signature =
            movement::local_player_movement_state_signature(*state);
    }
    if (camera != nullptr) {
        result.camera.emplace(*camera);
    }
    result.statistics = statistics;
    result.generated_command_count = generated_commands;
    result.error = LocalPlayerMovementControllerError{
        code, std::nullopt, std::nullopt, std::nullopt, std::nullopt, context};
    return result;
}

} // namespace

std::string_view to_string(
    const LocalPlayerMovementControllerErrorCode code) noexcept
{
    switch (code) {
    case LocalPlayerMovementControllerErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalPlayerMovementControllerErrorCode::invalid_initial_state:
        return "invalid_initial_state";
    case LocalPlayerMovementControllerErrorCode::camera_update_failed:
        return "camera_update_failed";
    case LocalPlayerMovementControllerErrorCode::scheduler_failed:
        return "scheduler_failed";
    case LocalPlayerMovementControllerErrorCode::command_build_failed:
        return "command_build_failed";
    case LocalPlayerMovementControllerErrorCode::movement_simulation_failed:
        return "movement_simulation_failed";
    case LocalPlayerMovementControllerErrorCode::camera_revision_exhausted:
        return "camera_revision_exhausted";
    case LocalPlayerMovementControllerErrorCode::statistics_overflow:
        return "statistics_overflow";
    }
    return "unknown";
}

LocalPlayerMovementController::LocalPlayerMovementController(
    movement::LocalPlayerMovementState initial_state,
    goldsrc::movement::GoldSrcMovementEnvironment environment,
    LocalPlayerMovementControllerConfig config) noexcept
    : player_state_{std::move(initial_state)},
      environment_{std::move(environment)},
      config_{std::move(config)},
      scheduler_{config_.scheduler}
{
    valid_configuration_ = scheduler_.valid_configuration() &&
        finite_speed_config(config_.movement_speeds) &&
        goldsrc::movement::valid_goldsrc_local_movement_config(
            config_.movement) &&
        player_state_->source_command_sequence() == 0U &&
        player_state_->command_profile() ==
            movement::GoldSrcMovementCommandProfile::
                synthetic_usercmd_semantics_v1 &&
        environment_->profile() == goldsrc::movement::
            GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1;
    bool revision_changed = false;
    std::optional<gameplay_camera::GameplayCameraError> camera_error;
    auto initial_camera = make_player_camera(
        *player_state_, player_state_->view_angles().y,
        player_state_->view_angles().x, 0U, revision_changed, camera_error);
    if (initial_camera) {
        camera_.emplace(std::move(*initial_camera));
    }
    valid_configuration_ = valid_configuration_ && camera_.has_value() &&
        !camera_error.has_value();
}

bool LocalPlayerMovementController::valid_configuration() const noexcept
{
    return valid_configuration_;
}

const movement::LocalPlayerMovementState&
LocalPlayerMovementController::player_state() const noexcept
{
    return *player_state_;
}

const gameplay_camera::GameplayCameraState&
LocalPlayerMovementController::camera() const noexcept
{
    return *camera_;
}

const goldsrc::movement::GoldSrcMovementEnvironment&
LocalPlayerMovementController::environment() const noexcept
{
    return *environment_;
}

const LocalPlayerMovementControllerConfig&
LocalPlayerMovementController::config() const noexcept
{
    return config_;
}

gameplay_input::GameplayButtonMask
LocalPlayerMovementController::pending_one_shots() const noexcept
{
    return pending_one_shots_;
}

std::optional<gameplay_camera::GameplayCameraState>
LocalPlayerMovementController::make_player_camera(
    const movement::LocalPlayerMovementState& state,
    const double yaw_degrees,
    const double pitch_degrees,
    const std::uint64_t previous_revision,
    bool& revision_changed,
    std::optional<gameplay_camera::GameplayCameraError>& error) const noexcept
{
    const assets::AssetVector3 position{
        state.origin().x + state.view_offset().x,
        state.origin().y + state.view_offset().y,
        state.origin().z + state.view_offset().z,
    };
    const auto normalized_yaw =
        gameplay_camera::normalize_yaw_degrees(yaw_degrees);
    const auto clamped_pitch = gameplay_camera::clamp_pitch_degrees(
        pitch_degrees, config_.camera.minimum_pitch_degrees(),
        config_.camera.maximum_pitch_degrees());
    if (!normalized_yaw || !clamped_pitch) {
        error = gameplay_camera::GameplayCameraError{
            gameplay_camera::GameplayCameraErrorCode::non_finite_camera,
            "player camera angles are invalid"};
        return std::nullopt;
    }
    revision_changed = !camera_ || !same_vector(camera_->position(), position) ||
        camera_->yaw_degrees() != *normalized_yaw ||
        camera_->pitch_degrees() != *clamped_pitch ||
        camera_->mode() != gameplay_camera::GameplayCameraMode::player_walk;
    if (revision_changed &&
        previous_revision >= config_.camera.maximum_camera_revisions()) {
        error = gameplay_camera::GameplayCameraError{
            gameplay_camera::GameplayCameraErrorCode::revision_limit_exceeded,
            "player camera revision is exhausted"};
        return std::nullopt;
    }
    gameplay_camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = position;
    create_info.yaw_degrees = *normalized_yaw;
    create_info.pitch_degrees = *clamped_pitch;
    create_info.vertical_fov_radians = config_.camera.vertical_fov_radians();
    create_info.near_plane = config_.camera.near_plane();
    create_info.far_plane = config_.camera.far_plane();
    create_info.mode = gameplay_camera::GameplayCameraMode::player_walk;
    create_info.revision = previous_revision +
        static_cast<std::uint64_t>(revision_changed);
    const auto created = gameplay_camera::GameplayCameraState::create(create_info);
    if (!created || !created.state) {
        error = created.error;
        return std::nullopt;
    }
    return created.state;
}

LocalPlayerMovementControllerUpdateResult
LocalPlayerMovementController::update(
    const std::int64_t monotonic_time_nanoseconds,
    const gameplay_input::GameplayInputIntent& intent,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch)
{
    if (!valid_configuration_ || !player_state_ || !environment_ || !camera_) {
        return failure(
            LocalPlayerMovementControllerErrorCode::invalid_configuration,
            player_state_ ? &*player_state_ : nullptr,
            camera_ ? &*camera_ : nullptr, {}, 0U,
            "local player movement controller is invalid");
    }
    if (intent.focused()) {
        pending_one_shots_ |= intent.pressed_buttons();
    } else {
        pending_one_shots_ = 0U;
    }
    auto staged_pending_one_shots = pending_one_shots_;

    auto yaw = camera_->yaw_degrees();
    auto pitch = camera_->pitch_degrees();
    if (intent.focused() && intent.captured()) {
        yaw += intent.look_delta_yaw_degrees();
        pitch += intent.look_delta_pitch_degrees();
    }
    bool provisional_camera_changed = false;
    std::optional<gameplay_camera::GameplayCameraError> camera_error;
    auto provisional_camera = make_player_camera(
        *player_state_, yaw, pitch, camera_->revision(),
        provisional_camera_changed, camera_error);
    if (!provisional_camera || camera_error) {
        auto result = failure(
            camera_error && camera_error->code ==
                    gameplay_camera::GameplayCameraErrorCode::
                        revision_limit_exceeded
                ? LocalPlayerMovementControllerErrorCode::
                    camera_revision_exhausted
                : LocalPlayerMovementControllerErrorCode::camera_update_failed,
            &*player_state_, &*camera_, {}, 0U,
            "local player look update failed");
        result.error->camera_error = camera_error;
        return result;
    }

    auto staged_scheduler = scheduler_;
    const auto scheduled = staged_scheduler.update(
        monotonic_time_nanoseconds, intent, *provisional_camera);
    if (!scheduled) {
        auto result = failure(
            LocalPlayerMovementControllerErrorCode::scheduler_failed,
            &*player_state_, &*camera_, {}, 0U,
            "fixed usercmd scheduler failed");
        result.error->scheduler_error = scheduled.error;
        return result;
    }

    std::optional<movement::LocalPlayerMovementState> staged_state{
        *player_state_};
    movement::PlayerMovementStatistics aggregate_statistics;
    std::size_t generated_commands = 0U;
    for (std::size_t index = 0U; index < scheduled.requests.size(); ++index) {
        const auto& request = scheduled.requests[index];
        goldsrc::GoldSrcUserCmdBuildContext context;
        context.command_sequence = request.command_sequence;
        context.command_msec = request.command_msec;
        context.command_sample_duration_nanoseconds =
            request.sample_duration_nanoseconds;
        context.command_sample_time_nanoseconds = request.sample_time_nanoseconds;
        context.movement_speeds = config_.movement_speeds;
        context.one_shot_buttons =
            index == 0U ? staged_pending_one_shots : 0U;
        const auto built = input_adapter_.build(
            intent, *provisional_camera, context);
        if (!built || !built.command) {
            auto result = failure(
                LocalPlayerMovementControllerErrorCode::command_build_failed,
                &*player_state_, &*camera_, aggregate_statistics,
                generated_commands, "synthetic usercmd build failed");
            result.error->command_error = built.error;
            return result;
        }
        auto simulated = goldsrc::movement::GoldSrcLocalMovementKernel::simulate(
            *staged_state, *built.command, *environment_, collision, scratch,
            config_.movement);
        if (!simulated || !simulated.state) {
            auto result = failure(
                LocalPlayerMovementControllerErrorCode::
                    movement_simulation_failed,
                &*player_state_, &*camera_, aggregate_statistics,
                generated_commands, "local movement command failed");
            result.error->movement_error = simulated.error;
            return result;
        }
        if (!add_statistics(aggregate_statistics, simulated.statistics)) {
            return failure(
                LocalPlayerMovementControllerErrorCode::statistics_overflow,
                &*player_state_, &*camera_, aggregate_statistics,
                generated_commands,
                "movement statistics aggregation overflowed");
        }
        staged_state.emplace(std::move(*simulated.state));
        ++generated_commands;
        if (index == 0U) {
            staged_pending_one_shots &= ~context.one_shot_buttons;
        }
    }

    bool final_camera_changed = false;
    camera_error.reset();
    auto final_camera = make_player_camera(
        *staged_state, provisional_camera->yaw_degrees(),
        provisional_camera->pitch_degrees(), camera_->revision(),
        final_camera_changed, camera_error);
    if (!final_camera || camera_error) {
        auto result = failure(
            LocalPlayerMovementControllerErrorCode::camera_update_failed,
            &*player_state_, &*camera_, aggregate_statistics,
            generated_commands, "player camera publication failed");
        result.error->camera_error = camera_error;
        return result;
    }

    const bool player_changed = generated_commands != 0U;
    scheduler_ = std::move(staged_scheduler);
    pending_one_shots_ = staged_pending_one_shots;
    player_state_.emplace(std::move(*staged_state));
    camera_.emplace(std::move(*final_camera));
    LocalPlayerMovementControllerUpdateResult result;
    result.player_state.emplace(*player_state_);
    result.camera.emplace(*camera_);
    result.statistics = aggregate_statistics;
    result.generated_command_count = generated_commands;
    result.final_state_signature =
        movement::local_player_movement_state_signature(*player_state_);
    result.player_state_changed = player_changed;
    result.camera_revision_changed = final_camera_changed;
    return result;
}

} // namespace hlclient::local_player
