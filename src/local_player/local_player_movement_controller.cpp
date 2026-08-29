#include <hlclient/local_player/local_player_movement_controller.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
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
    auto staged = destination;
    if (!checked_add(staged.command_count, source.command_count) ||
        !checked_add(staged.substep_count, source.substep_count) ||
        !checked_add(
            staged.grounded_command_count,
            source.grounded_command_count) ||
        !checked_add(
            staged.airborne_command_count,
            source.airborne_command_count) ||
        !checked_add(staged.ground_probe_count, source.ground_probe_count) ||
        !checked_add(staged.trace_count, source.trace_count) ||
        !checked_add(staged.collision_hit_count, source.collision_hit_count) ||
        !checked_add(staged.slide_bump_count, source.slide_bump_count) ||
        !checked_add(staged.clip_plane_count, source.clip_plane_count) ||
        !checked_add(staged.step_attempt_count, source.step_attempt_count) ||
        !checked_add(staged.step_success_count, source.step_success_count) ||
        !checked_add(staged.jump_count, source.jump_count) ||
        !checked_add(staged.duck_enter_count, source.duck_enter_count) ||
        !checked_add(staged.duck_exit_count, source.duck_exit_count) ||
        !checked_add(staged.stand_blocked_count, source.stand_blocked_count) ||
        !checked_add(staged.start_solid_count, source.start_solid_count) ||
        !checked_add(staged.all_solid_count, source.all_solid_count)) {
        return false;
    }
    const auto horizontal = staged.total_horizontal_distance +
        source.total_horizontal_distance;
    const auto vertical = staged.total_vertical_distance +
        source.total_vertical_distance;
    if (!std::isfinite(horizontal) || !std::isfinite(vertical)) {
        return false;
    }
    staged.total_horizontal_distance = horizontal;
    staged.total_vertical_distance = vertical;
    destination = staged;
    return true;
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

[[nodiscard]] LocalPlayerMovementPreparedTouchSummary summarize_touches(
    const std::span<const movement::PlayerMovementTouch> touches) noexcept
{
    LocalPlayerMovementPreparedTouchSummary result;
    result.touch_count = touches.size();
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    const auto mix = [&hash](const std::uint64_t value) noexcept {
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            hash ^= static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(index * 8U));
            hash *= 1'099'511'628'211ULL;
        }
    };
    mix(static_cast<std::uint64_t>(touches.size()));
    if (!touches.empty()) {
        result.first_hit_kind = touches.front().hit.kind;
        result.last_hit_kind = touches.back().hit.kind;
        for (const auto& touch : touches) {
            mix(static_cast<std::uint64_t>(touch.hit.kind));
            mix(touch.hit.source_model_index);
            mix(touch.source_command_sequence);
            mix(std::bit_cast<std::uint64_t>(touch.fraction));
        }
    }
    result.deterministic_signature = hash;
    return result;
}

[[nodiscard]] std::uint64_t prepared_plan_identity(
    const std::uint64_t base_revision,
    const std::int64_t monotonic_time_nanoseconds,
    const std::span<const LocalPlayerMovementPreparedCommand> commands) noexcept
{
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    const auto mix = [&hash](const std::uint64_t value) noexcept {
        hash ^= value;
        hash *= 1'099'511'628'211ULL;
    };
    mix(base_revision);
    mix(std::bit_cast<std::uint64_t>(monotonic_time_nanoseconds));
    mix(static_cast<std::uint64_t>(commands.size()));
    for (const auto& command : commands) {
        mix(command.command ? command.command->command_sequence().value() : 0U);
        mix(command.post_state
                ? movement::local_player_movement_state_signature(
                      *command.post_state)
                : 0U);
    }
    return hash == 0U ? 1U : hash;
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
    case LocalPlayerMovementControllerErrorCode::stale_prepared_update:
        return "stale_prepared_update";
    case LocalPlayerMovementControllerErrorCode::
            prepared_update_already_consumed:
        return "prepared_update_already_consumed";
    case LocalPlayerMovementControllerErrorCode::invalid_reconciled_state:
        return "invalid_reconciled_state";
    case LocalPlayerMovementControllerErrorCode::allocation_failed:
        return "allocation_failed";
    }
    return "unknown";
}

LocalPlayerMovementPreparedUpdate::LocalPlayerMovementPreparedUpdate(
    const void* const owner,
    const std::uint64_t base_controller_revision,
    const std::uint64_t plan_identity,
    std::vector<LocalPlayerMovementPreparedCommand> commands,
    movement::LocalPlayerMovementState final_player_state,
    gameplay_camera::GameplayCameraState final_camera,
    goldsrc::GoldSrcUserCmdScheduler staged_scheduler,
    const gameplay_input::GameplayButtonMask staged_pending_one_shots,
    const movement::PlayerMovementStatistics statistics,
    const std::uint64_t committed_touch_match_count,
    const bool camera_revision_changed) noexcept
    : owner_{owner},
      base_controller_revision_{base_controller_revision},
      plan_identity_{plan_identity},
      commands_{std::move(commands)},
      final_player_state_{std::move(final_player_state)},
      final_camera_{std::move(final_camera)},
      staged_scheduler_{std::move(staged_scheduler)},
      staged_pending_one_shots_{staged_pending_one_shots},
      statistics_{statistics},
      committed_touch_match_count_{committed_touch_match_count},
      camera_revision_changed_{camera_revision_changed}
{
}

LocalPlayerMovementPreparedUpdate::LocalPlayerMovementPreparedUpdate(
    LocalPlayerMovementPreparedUpdate&& other) noexcept
    : owner_{other.owner_},
      base_controller_revision_{other.base_controller_revision_},
      plan_identity_{other.plan_identity_},
      commands_{std::move(other.commands_)},
      final_player_state_{std::move(other.final_player_state_)},
      final_camera_{std::move(other.final_camera_)},
      staged_scheduler_{std::move(other.staged_scheduler_)},
      staged_pending_one_shots_{other.staged_pending_one_shots_},
      statistics_{other.statistics_},
      committed_touch_match_count_{other.committed_touch_match_count_},
      camera_revision_changed_{other.camera_revision_changed_},
      consumable_{other.consumable_}
{
    other.invalidate_after_move();
}

LocalPlayerMovementPreparedUpdate&
LocalPlayerMovementPreparedUpdate::operator=(
    LocalPlayerMovementPreparedUpdate&& other) noexcept
{
    if (this == &other) {
        invalidate_after_move();
        return *this;
    }

    owner_ = other.owner_;
    base_controller_revision_ = other.base_controller_revision_;
    plan_identity_ = other.plan_identity_;
    commands_ = std::move(other.commands_);
    final_player_state_.reset();
    if (other.final_player_state_) {
        final_player_state_.emplace(std::move(*other.final_player_state_));
    }
    final_camera_.reset();
    if (other.final_camera_) {
        final_camera_.emplace(std::move(*other.final_camera_));
    }
    staged_scheduler_ = std::move(other.staged_scheduler_);
    staged_pending_one_shots_ = other.staged_pending_one_shots_;
    statistics_ = other.statistics_;
    committed_touch_match_count_ = other.committed_touch_match_count_;
    camera_revision_changed_ = other.camera_revision_changed_;
    consumable_ = other.consumable_;

    other.invalidate_after_move();
    return *this;
}

void LocalPlayerMovementPreparedUpdate::invalidate_after_move() noexcept
{
    owner_ = nullptr;
    base_controller_revision_ = 0U;
    plan_identity_ = 0U;
    commands_.clear();
    final_player_state_.reset();
    final_camera_.reset();
    staged_pending_one_shots_ = 0U;
    statistics_ = {};
    committed_touch_match_count_ = 0U;
    camera_revision_changed_ = false;
    consumable_ = false;
}

std::uint64_t
LocalPlayerMovementPreparedUpdate::base_controller_revision() const noexcept
{ return base_controller_revision_; }
std::uint64_t LocalPlayerMovementPreparedUpdate::plan_identity() const noexcept
{ return plan_identity_; }
std::span<const LocalPlayerMovementPreparedCommand>
LocalPlayerMovementPreparedUpdate::commands() const noexcept
{ return commands_; }
const movement::LocalPlayerMovementState&
LocalPlayerMovementPreparedUpdate::final_player_state() const noexcept
{ return *final_player_state_; }
const gameplay_camera::GameplayCameraState&
LocalPlayerMovementPreparedUpdate::final_camera() const noexcept
{ return *final_camera_; }
const movement::PlayerMovementStatistics&
LocalPlayerMovementPreparedUpdate::statistics() const noexcept
{ return statistics_; }
std::uint64_t
LocalPlayerMovementPreparedUpdate::committed_touch_match_count() const noexcept
{ return committed_touch_match_count_; }
bool LocalPlayerMovementPreparedUpdate::consumable() const noexcept
{ return consumable_; }

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

std::uint64_t LocalPlayerMovementController::revision() const noexcept
{
    return revision_;
}

void LocalPlayerMovementController::capture_pending_input(
    const gameplay_input::GameplayInputIntent& intent) noexcept
{
    const auto previous = pending_one_shots_;
    if (intent.focused()) {
        pending_one_shots_ |= intent.pressed_buttons();
    } else {
        pending_one_shots_ = 0U;
    }
    if (pending_one_shots_ == previous) {
        return;
    }
    if (revision_ == UINT64_MAX) {
        valid_configuration_ = false;
    } else {
        ++revision_;
    }
}

void LocalPlayerMovementController::discard_pending_input() noexcept
{
    if (pending_one_shots_ == 0U) {
        return;
    }
    pending_one_shots_ = 0U;
    if (revision_ == UINT64_MAX) {
        valid_configuration_ = false;
    } else {
        ++revision_;
    }
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

LocalPlayerMovementPrepareResult LocalPlayerMovementController::prepare_update(
    const std::int64_t monotonic_time_nanoseconds,
    const gameplay_input::GameplayInputIntent& intent,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    const LocalPlayerMovementCommittedTouchFilter* const
        committed_touch_filter) const
{
    if (!valid_configuration_ || !player_state_ || !environment_ || !camera_) {
        return {std::nullopt, failure(
            LocalPlayerMovementControllerErrorCode::invalid_configuration,
            player_state_ ? &*player_state_ : nullptr,
            camera_ ? &*camera_ : nullptr, {}, 0U,
            "local player movement controller is invalid")};
    }
    auto staged_pending_one_shots = pending_one_shots_;
    if (intent.focused()) {
        staged_pending_one_shots |= intent.pressed_buttons();
    } else {
        staged_pending_one_shots = 0U;
    }

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
        return {std::nullopt, std::move(result)};
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
        return {std::nullopt, std::move(result)};
    }

    try {
        auto staged_state =
            std::make_shared<const movement::LocalPlayerMovementState>(
                *player_state_);
        movement::PlayerMovementStatistics aggregate_statistics;
        std::uint64_t committed_touch_match_count = 0U;
        std::vector<LocalPlayerMovementPreparedCommand> prepared_commands;
        prepared_commands.reserve(scheduled.requests.size());
        for (std::size_t index = 0U; index < scheduled.requests.size(); ++index) {
            const auto& request = scheduled.requests[index];
            goldsrc::GoldSrcUserCmdBuildContext context;
            context.command_sequence = request.command_sequence;
            context.command_msec = request.command_msec;
            context.command_sample_duration_nanoseconds =
                request.sample_duration_nanoseconds;
            context.command_sample_time_nanoseconds =
                request.sample_time_nanoseconds;
            context.movement_speeds = config_.movement_speeds;
            context.one_shot_buttons =
                index == 0U ? staged_pending_one_shots : 0U;
            auto built = input_adapter_.build(
                intent, *provisional_camera, context);
            if (!built || !built.command) {
                auto result = failure(
                    LocalPlayerMovementControllerErrorCode::command_build_failed,
                    &*player_state_, &*camera_, aggregate_statistics,
                    prepared_commands.size(), "synthetic usercmd build failed");
                result.error->command_error = built.error;
                return {std::nullopt, std::move(result)};
            }
            auto command = std::make_shared<const goldsrc::GoldSrcUserCmdState>(
                std::move(*built.command));
            auto simulated =
                goldsrc::movement::GoldSrcLocalMovementKernel::simulate(
                    *staged_state, *command, *environment_, collision, scratch,
                    config_.movement);
            if (!simulated || !simulated.state) {
                auto result = failure(
                    LocalPlayerMovementControllerErrorCode::
                        movement_simulation_failed,
                    &*player_state_, &*camera_, aggregate_statistics,
                    prepared_commands.size(), "local movement command failed");
                result.error->movement_error = simulated.error;
                return {std::nullopt, std::move(result)};
            }
            if (!add_statistics(aggregate_statistics, simulated.statistics)) {
                return {std::nullopt,
                    failure(
                        LocalPlayerMovementControllerErrorCode::
                            statistics_overflow,
                        &*player_state_, &*camera_, aggregate_statistics,
                        prepared_commands.size(),
                        "movement statistics aggregation overflowed")};
            }
            if (committed_touch_filter != nullptr) {
                for (const auto& touch : simulated.touches) {
                    if (touch.hit != committed_touch_filter->hit ||
                        touch.plane != committed_touch_filter->plane) {
                        continue;
                    }
                    if (committed_touch_match_count == UINT64_MAX) {
                        return {std::nullopt,
                            failure(
                                LocalPlayerMovementControllerErrorCode::
                                    statistics_overflow,
                                &*player_state_, &*camera_,
                                aggregate_statistics,
                                prepared_commands.size(),
                                "committed touch match count overflowed")};
                    }
                    ++committed_touch_match_count;
                }
            }
            auto post_state =
                std::make_shared<const movement::LocalPlayerMovementState>(
                    std::move(*simulated.state));
            prepared_commands.push_back(LocalPlayerMovementPreparedCommand{
                std::move(command), staged_state, post_state,
                simulated.statistics, summarize_touches(simulated.touches)});
            staged_state = std::move(post_state);
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
                prepared_commands.size(), "player camera publication failed");
            result.error->camera_error = camera_error;
            return {std::nullopt, std::move(result)};
        }

        const auto identity = prepared_plan_identity(
            revision_, monotonic_time_nanoseconds, prepared_commands);
        LocalPlayerMovementPreparedUpdate prepared{this, revision_, identity,
            std::move(prepared_commands), *staged_state,
            std::move(*final_camera), std::move(staged_scheduler),
            staged_pending_one_shots, aggregate_statistics,
            committed_touch_match_count, final_camera_changed};
        LocalPlayerMovementControllerUpdateResult preview;
        preview.player_state.emplace(prepared.final_player_state());
        preview.camera.emplace(prepared.final_camera());
        preview.statistics = prepared.statistics();
        preview.generated_command_count = prepared.commands().size();
        preview.final_state_signature =
            movement::local_player_movement_state_signature(
                prepared.final_player_state());
        preview.committed_touch_match_count =
            prepared.committed_touch_match_count();
        preview.player_state_changed = !prepared.commands().empty();
        preview.camera_revision_changed = final_camera_changed;
        return {std::optional<LocalPlayerMovementPreparedUpdate>{
                    std::move(prepared)},
            std::move(preview)};
    } catch (const std::bad_alloc&) {
        return {std::nullopt,
            failure(LocalPlayerMovementControllerErrorCode::allocation_failed,
                &*player_state_, &*camera_, {}, 0U,
                "prepared movement update allocation failed")};
    }
}

std::optional<LocalPlayerMovementControllerError>
LocalPlayerMovementController::preflight_prepared_update(
    const LocalPlayerMovementPreparedUpdate& prepared) const noexcept
{
    if (!prepared.consumable_ || !prepared.final_player_state_ ||
        !prepared.final_camera_) {
        return LocalPlayerMovementControllerError{
            LocalPlayerMovementControllerErrorCode::
                prepared_update_already_consumed,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            "prepared movement update is already consumed"};
    }
    if (prepared.owner_ != this ||
        prepared.base_controller_revision_ != revision_ ||
        revision_ == UINT64_MAX) {
        return LocalPlayerMovementControllerError{
            LocalPlayerMovementControllerErrorCode::stale_prepared_update,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            "prepared movement update owner or revision is stale"};
    }
    return std::nullopt;
}

LocalPlayerMovementControllerUpdateResult
LocalPlayerMovementController::commit_prepared_update(
    LocalPlayerMovementPreparedUpdate&& prepared) noexcept
{
    if (const auto error = preflight_prepared_update(prepared)) {
        auto result = failure(error->code, player_state_ ? &*player_state_ : nullptr,
            camera_ ? &*camera_ : nullptr, prepared.statistics_,
            prepared.commands_.size(), error->context);
        result.error = error;
        return result;
    }
    scheduler_ = std::move(prepared.staged_scheduler_);
    pending_one_shots_ = prepared.staged_pending_one_shots_;
    player_state_.emplace(std::move(*prepared.final_player_state_));
    camera_.emplace(std::move(*prepared.final_camera_));
    ++revision_;
    prepared.consumable_ = false;

    LocalPlayerMovementControllerUpdateResult result;
    result.player_state.emplace(*player_state_);
    result.camera.emplace(*camera_);
    result.statistics = prepared.statistics_;
    result.generated_command_count = prepared.commands_.size();
    result.final_state_signature =
        movement::local_player_movement_state_signature(*player_state_);
    result.committed_touch_match_count =
        prepared.committed_touch_match_count_;
    result.player_state_changed = !prepared.commands_.empty();
    result.camera_revision_changed = prepared.camera_revision_changed_;
    return result;
}

void LocalPlayerMovementController::abandon_prepared_update(
    LocalPlayerMovementPreparedUpdate& prepared) const noexcept
{
    if (prepared.owner_ == this) {
        prepared.consumable_ = false;
    }
}

LocalPlayerMovementControllerUpdateResult
LocalPlayerMovementController::replace_simulation_state(
    const std::uint64_t expected_controller_revision,
    movement::LocalPlayerMovementState corrected_state,
    const bool reset_command_stream) noexcept
{
    if (!valid_configuration_ || !player_state_ || !camera_ ||
        expected_controller_revision != revision_ || revision_ == UINT64_MAX ||
        corrected_state.command_profile() != player_state_->command_profile() ||
        (reset_command_stream
                ? corrected_state.source_command_sequence() != 0U
                : corrected_state.source_command_sequence() !=
                    player_state_->source_command_sequence())) {
        return failure(
            expected_controller_revision != revision_
                ? LocalPlayerMovementControllerErrorCode::stale_prepared_update
                : LocalPlayerMovementControllerErrorCode::
                    invalid_reconciled_state,
            &*player_state_, &*camera_, {}, 0U,
            "corrected simulation state is invalid or stale");
    }
    bool camera_changed = false;
    std::optional<gameplay_camera::GameplayCameraError> camera_error;
    auto corrected_camera = make_player_camera(corrected_state,
        camera_->yaw_degrees(), camera_->pitch_degrees(), camera_->revision(),
        camera_changed, camera_error);
    if (!corrected_camera || camera_error) {
        auto result = failure(
            LocalPlayerMovementControllerErrorCode::camera_update_failed,
            &*player_state_, &*camera_, {}, 0U,
            "corrected movement camera publication failed");
        result.error->camera_error = camera_error;
        return result;
    }
    if (reset_command_stream) {
        scheduler_.reset();
        pending_one_shots_ = 0U;
    }
    player_state_.emplace(std::move(corrected_state));
    camera_.emplace(std::move(*corrected_camera));
    ++revision_;
    LocalPlayerMovementControllerUpdateResult result;
    result.player_state.emplace(*player_state_);
    result.camera.emplace(*camera_);
    result.final_state_signature =
        movement::local_player_movement_state_signature(*player_state_);
    result.player_state_changed = true;
    result.camera_revision_changed = camera_changed;
    return result;
}

LocalPlayerMovementControllerUpdateResult
LocalPlayerMovementController::update(
    const std::int64_t monotonic_time_nanoseconds,
    const gameplay_input::GameplayInputIntent& intent,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    const LocalPlayerMovementCommittedTouchFilter* const
        committed_touch_filter)
{
    capture_pending_input(intent);
    auto prepared = prepare_update(monotonic_time_nanoseconds, intent,
        collision, scratch, committed_touch_filter);
    if (!prepared || !prepared.prepared_update) {
        return std::move(prepared.preview);
    }
    return commit_prepared_update(std::move(*prepared.prepared_update));
}

} // namespace hlclient::local_player
