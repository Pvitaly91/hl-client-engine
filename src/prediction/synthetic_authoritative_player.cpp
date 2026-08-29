#include <hlclient/prediction/synthetic_authoritative_player.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::prediction {
namespace {

[[nodiscard]] PredictionError failure(
    const PredictionErrorCode code,
    const std::string_view context,
    const std::optional<goldsrc::GoldSrcUserCmdSequence> sequence =
        std::nullopt) noexcept
{
    return PredictionError{code, sequence, context};
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] double vector_magnitude(
    const assets::AssetVector3& value) noexcept
{
    return std::sqrt(
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z);
}

[[nodiscard]] assets::AssetVector3 add_vectors(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] bool same_collision_session(
    const PredictionSessionIdentity& session,
    const goldsrc::movement::ILocalMovementCollision& collision) noexcept
{
    const auto identity = collision.session_identity();
    return collision.valid() && identity && identity->valid() &&
        collision.profile() == identity->profile &&
        identity->profile == session.collision_profile &&
        session.collision_world_primary == identity->collision_world_primary &&
        session.collision_world_secondary ==
            identity->collision_world_secondary &&
        session.collision_world_revision ==
            identity->collision_world_revision &&
        session.collision_scene_signature == identity->scene_signature;
}

[[nodiscard]] std::optional<PredictionError> validate_collision_position(
    const movement::LocalPlayerMovementState& state,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config,
    const std::optional<goldsrc::GoldSrcUserCmdSequence> sequence =
        std::nullopt) noexcept
{
    const auto tested = collision.test_position(
        state.origin(), state.hull(), scratch.collision,
        movement_config.collision_query);
    if (!tested || !tested.result) {
        return failure(
            PredictionErrorCode::invalid_authoritative_state,
            "synthetic authoritative collision validation failed",
            sequence);
    }
    if (tested.result->status ==
        goldsrc::movement::LocalMovementPositionStatus::blocking) {
        return failure(
            PredictionErrorCode::authoritative_state_blocking,
            "synthetic authoritative state is blocking",
            sequence);
    }
    return std::nullopt;
}

enum class ScenarioCorrection : std::uint8_t {
    none,
    small_position,
    velocity,
    large_position,
    teleport,
};

[[nodiscard]] ScenarioCorrection correction_for_command(
    const SyntheticAuthoritativePlayerConfig& config,
    const std::uint32_t sequence) noexcept
{
    const auto trigger = config.correction_command_sequence;
    const auto periodic_velocity = [sequence, trigger](
                                       const std::uint32_t period) noexcept {
        return sequence >= trigger &&
            (sequence - trigger) % period == 0U
            ? ScenarioCorrection::velocity
            : ScenarioCorrection::none;
    };
    switch (config.scenario) {
    case SyntheticAuthoritativeScenario::small_position_correction:
        return sequence == trigger ? ScenarioCorrection::small_position
                                   : ScenarioCorrection::none;
    case SyntheticAuthoritativeScenario::velocity_correction:
        return sequence == trigger ? ScenarioCorrection::velocity
                                   : ScenarioCorrection::none;
    case SyntheticAuthoritativeScenario::large_position_correction:
        return sequence == trigger ? ScenarioCorrection::large_position
                                   : ScenarioCorrection::none;
    case SyntheticAuthoritativeScenario::teleport:
        return sequence == trigger ? ScenarioCorrection::teleport
                                   : ScenarioCorrection::none;
    case SyntheticAuthoritativeScenario::wall_replay:
        return periodic_velocity(240U);
    case SyntheticAuthoritativeScenario::jump_replay:
        return periodic_velocity(100U);
    case SyntheticAuthoritativeScenario::duck_replay:
        return periodic_velocity(50U);
    case SyntheticAuthoritativeScenario::mixed:
        if (sequence == trigger) {
            return ScenarioCorrection::small_position;
        }
        if (sequence == trigger + 1U) {
            return ScenarioCorrection::velocity;
        }
        if (sequence == trigger + 2U) {
            return ScenarioCorrection::large_position;
        }
        if (sequence == trigger + 3U) {
            return ScenarioCorrection::teleport;
        }
        return ScenarioCorrection::none;
    case SyntheticAuthoritativeScenario::exact_authority:
    case SyntheticAuthoritativeScenario::delayed_authority:
    case SyntheticAuthoritativeScenario::stale_and_duplicate_updates:
        return ScenarioCorrection::none;
    }
    return ScenarioCorrection::none;
}

struct CorrectedStateResult {
    std::optional<movement::LocalPlayerMovementState> state;
    std::optional<PredictionError> error;
    AuthoritativePlayerDiscontinuity discontinuity{
        AuthoritativePlayerDiscontinuity::normal};
    bool correction_applied{false};
};

[[nodiscard]] CorrectedStateResult apply_scenario_correction(
    movement::LocalPlayerMovementState simulated_state,
    const SyntheticAuthoritativePlayerConfig& config,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config,
    const goldsrc::GoldSrcUserCmdSequence sequence) noexcept
{
    const auto correction = correction_for_command(config, sequence.value());
    if (correction == ScenarioCorrection::none) {
        return {std::move(simulated_state), std::nullopt,
            AuthoritativePlayerDiscontinuity::normal, false};
    }

    auto create_info =
        movement::local_player_movement_state_create_info(simulated_state);
    auto discontinuity = AuthoritativePlayerDiscontinuity::normal;
    switch (correction) {
    case ScenarioCorrection::small_position:
        create_info.origin =
            add_vectors(create_info.origin, config.small_position_delta);
        break;
    case ScenarioCorrection::velocity:
        create_info.velocity =
            add_vectors(create_info.velocity, config.velocity_delta);
        break;
    case ScenarioCorrection::large_position:
        create_info.origin =
            add_vectors(create_info.origin, config.large_position_delta);
        break;
    case ScenarioCorrection::teleport:
        if (!config.teleport_origin) {
            return {std::nullopt,
                failure(
                    PredictionErrorCode::invalid_configuration,
                    "synthetic teleport scenario has no destination",
                    sequence),
                AuthoritativePlayerDiscontinuity::normal, false};
        }
        create_info.origin = *config.teleport_origin;
        discontinuity = AuthoritativePlayerDiscontinuity::teleport;
        break;
    case ScenarioCorrection::none: break;
    }
    const auto created = movement::LocalPlayerMovementState::create(
        create_info, movement_config.state_limits);
    if (!created || !created.state) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "synthetic correction produced an invalid movement state",
                sequence),
            AuthoritativePlayerDiscontinuity::normal, false};
    }
    return {std::move(*created.state), std::nullopt, discontinuity, true};
}

[[nodiscard]] bool scenario_uses_small_correction(
    const SyntheticAuthoritativeScenario scenario) noexcept
{
    return scenario ==
            SyntheticAuthoritativeScenario::small_position_correction ||
        scenario == SyntheticAuthoritativeScenario::mixed;
}

[[nodiscard]] bool scenario_uses_velocity_correction(
    const SyntheticAuthoritativeScenario scenario) noexcept
{
    return scenario == SyntheticAuthoritativeScenario::velocity_correction ||
        scenario == SyntheticAuthoritativeScenario::wall_replay ||
        scenario == SyntheticAuthoritativeScenario::jump_replay ||
        scenario == SyntheticAuthoritativeScenario::duck_replay ||
        scenario == SyntheticAuthoritativeScenario::mixed;
}

[[nodiscard]] bool scenario_uses_large_correction(
    const SyntheticAuthoritativeScenario scenario) noexcept
{
    return scenario ==
            SyntheticAuthoritativeScenario::large_position_correction ||
        scenario == SyntheticAuthoritativeScenario::mixed;
}

[[nodiscard]] bool scenario_uses_teleport(
    const SyntheticAuthoritativeScenario scenario) noexcept
{
    return scenario == SyntheticAuthoritativeScenario::teleport ||
        scenario == SyntheticAuthoritativeScenario::mixed;
}

} // namespace

std::string_view to_string(
    const SyntheticAuthoritativeScenario scenario) noexcept
{
    switch (scenario) {
    case SyntheticAuthoritativeScenario::exact_authority:
        return "exact_authority";
    case SyntheticAuthoritativeScenario::delayed_authority:
        return "delayed_authority";
    case SyntheticAuthoritativeScenario::small_position_correction:
        return "small_position_correction";
    case SyntheticAuthoritativeScenario::velocity_correction:
        return "velocity_correction";
    case SyntheticAuthoritativeScenario::large_position_correction:
        return "large_position_correction";
    case SyntheticAuthoritativeScenario::teleport: return "teleport";
    case SyntheticAuthoritativeScenario::stale_and_duplicate_updates:
        return "stale_and_duplicate_updates";
    case SyntheticAuthoritativeScenario::wall_replay: return "wall_replay";
    case SyntheticAuthoritativeScenario::jump_replay: return "jump_replay";
    case SyntheticAuthoritativeScenario::duck_replay: return "duck_replay";
    case SyntheticAuthoritativeScenario::mixed: return "mixed";
    }
    return "unknown";
}

bool valid_synthetic_authoritative_player_config(
    const SyntheticAuthoritativePlayerConfig& config) noexcept
{
    if (!config.session.valid() ||
        config.command_delay > kMaximumSyntheticAuthorityDelayCommands ||
        config.maximum_pending_updates == 0U ||
        config.maximum_pending_updates >
            kMaximumSyntheticAuthorityPendingUpdates ||
        config.correction_command_sequence == 0U ||
        config.first_update_ordinal == 0U ||
        config.maximum_authoritative_updates == 0U ||
        !finite_vector(config.small_position_delta) ||
        !finite_vector(config.velocity_delta) ||
        !finite_vector(config.large_position_delta) ||
        (config.teleport_origin &&
            !finite_vector(*config.teleport_origin))) {
        return false;
    }
    if (config.scenario == SyntheticAuthoritativeScenario::exact_authority &&
        config.command_delay != 0U) {
        return false;
    }
    if (config.scenario ==
            SyntheticAuthoritativeScenario::delayed_authority &&
        config.command_delay == 0U) {
        return false;
    }
    if (config.scenario ==
            SyntheticAuthoritativeScenario::stale_and_duplicate_updates &&
        config.maximum_pending_updates < 3U) {
        return false;
    }
    if (config.scenario == SyntheticAuthoritativeScenario::mixed &&
        config.correction_command_sequence > UINT32_MAX - 3U) {
        return false;
    }
    if (scenario_uses_teleport(config.scenario) &&
        !config.teleport_origin) {
        return false;
    }

    const auto small_magnitude = vector_magnitude(config.small_position_delta);
    const auto velocity_magnitude = vector_magnitude(config.velocity_delta);
    const auto large_magnitude = vector_magnitude(config.large_position_delta);
    if (scenario_uses_small_correction(config.scenario) &&
        (!std::isfinite(small_magnitude) || small_magnitude <= 0.0 ||
            small_magnitude > 4.0)) {
        return false;
    }
    if (scenario_uses_velocity_correction(config.scenario) &&
        (!std::isfinite(velocity_magnitude) || velocity_magnitude <= 0.0 ||
            velocity_magnitude > 4'096.0)) {
        return false;
    }
    if (scenario_uses_large_correction(config.scenario) &&
        (!std::isfinite(large_magnitude) || large_magnitude <= 16.0 ||
            large_magnitude > 4'096.0)) {
        return false;
    }
    return true;
}

SyntheticAuthoritativePlayerSimulator::SyntheticAuthoritativePlayerSimulator(
    movement::LocalPlayerMovementState initial_state,
    goldsrc::movement::GoldSrcMovementEnvironment environment,
    SyntheticAuthoritativePlayerConfig config,
    goldsrc::movement::GoldSrcLocalMovementConfig movement_config) noexcept
    : current_state_{std::move(initial_state)},
      environment_{std::move(environment)},
      config_{std::move(config)},
      movement_config_{std::move(movement_config)},
      next_update_ordinal_{config_.first_update_ordinal}
{
}

SyntheticAuthoritativePlayerSimulator::SyntheticAuthoritativePlayerSimulator(
    SyntheticAuthoritativePlayerSimulator&& other) noexcept
    : current_state_{std::move(other.current_state_)},
      environment_{std::move(other.environment_)},
      config_{std::move(other.config_)},
      movement_config_{std::move(other.movement_config_)},
      delayed_updates_{std::move(other.delayed_updates_)},
      delayed_head_{std::exchange(other.delayed_head_, 0U)},
      delayed_count_{std::exchange(other.delayed_count_, 0U)},
      next_update_ordinal_{std::exchange(other.next_update_ordinal_, 0U)},
      update_ordinal_exhausted_{
          std::exchange(other.update_ordinal_exhausted_, true)},
      statistics_{std::exchange(
          other.statistics_, SyntheticAuthoritativeSimulatorStatistics{})}
{
}

SyntheticAuthoritativeSimulatorCreateResult
SyntheticAuthoritativePlayerSimulator::create(
    movement::LocalPlayerMovementState initial_state,
    goldsrc::movement::GoldSrcMovementEnvironment environment,
    SyntheticAuthoritativePlayerConfig config,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    goldsrc::movement::GoldSrcLocalMovementConfig movement_config) noexcept
{
    SyntheticAuthoritativeSimulatorCreateResult result;
    if (!valid_synthetic_authoritative_player_config(config) ||
        !goldsrc::movement::valid_goldsrc_local_movement_config(
            movement_config)) {
        result.error.emplace(failure(
            PredictionErrorCode::invalid_configuration,
            "synthetic authoritative simulator configuration is invalid"));
        return result;
    }
    if (initial_state.source_command_sequence() != 0U ||
        initial_state.command_profile() != config.session.command_profile ||
        initial_state.compatibility_profile() != movement::
            GoldSrcMovementCompatibilityProfile::
                public_valve_pm_shared_dry_walk_subset_v1 ||
        initial_state.evidence_profile() != movement::
            GoldSrcMovementEvidenceProfile::
                public_valve_pm_shared_and_independent_fixtures ||
        initial_state.mode() == movement::PlayerMovementMode::
            unsupported_liquid ||
        initial_state.mode() == movement::PlayerMovementMode::
            unsupported_ladder ||
        initial_state.mode() == movement::PlayerMovementMode::
            invalid_or_stuck ||
        movement::local_player_movement_state_signature(initial_state) !=
            config.session.spawn_initial_state_signature) {
        result.error.emplace(failure(
            PredictionErrorCode::invalid_session_identity,
            "synthetic authority initial state does not match its session"));
        return result;
    }
    if (!same_collision_session(config.session, collision)) {
        result.error.emplace(failure(
            PredictionErrorCode::collision_world_mismatch,
            "synthetic authority collision identity does not match its session"));
        return result;
    }
    if (prediction_movement_environment_signature(environment) !=
            config.session.movement_environment_signature ||
        prediction_movement_config_signature(movement_config) !=
            config.session.movement_config_signature ||
        environment.profile() != goldsrc::movement::
            GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1) {
        result.error.emplace(failure(
            PredictionErrorCode::movement_environment_mismatch,
            "synthetic authority movement context does not match its session"));
        return result;
    }
    if (const auto error = validate_collision_position(
            initial_state, collision, scratch, movement_config)) {
        result.error.emplace(*error);
        return result;
    }
    result.simulator.emplace(SyntheticAuthoritativePlayerSimulator{
        std::move(initial_state), std::move(environment), std::move(config),
        std::move(movement_config)});
    return result;
}

const movement::LocalPlayerMovementState&
SyntheticAuthoritativePlayerSimulator::current_state() const noexcept
{
    return *current_state_;
}

const SyntheticAuthoritativePlayerConfig&
SyntheticAuthoritativePlayerSimulator::config() const noexcept
{
    return config_;
}

const SyntheticAuthoritativeSimulatorStatistics&
SyntheticAuthoritativePlayerSimulator::statistics() const noexcept
{
    return statistics_;
}

std::size_t
SyntheticAuthoritativePlayerSimulator::pending_delayed_update_count()
    const noexcept
{
    return delayed_count_;
}

std::optional<goldsrc::GoldSrcUserCmdSequence>
SyntheticAuthoritativePlayerSimulator::next_expected_command_sequence()
    const noexcept
{
    if (!current_state_ ||
        current_state_->source_command_sequence() == UINT32_MAX) {
        return std::nullopt;
    }
    return goldsrc::GoldSrcUserCmdSequence::create(
        current_state_->source_command_sequence() + 1U);
}

std::optional<PredictionError>
SyntheticAuthoritativePlayerSimulator::validate_command_sequence(
    const goldsrc::GoldSrcUserCmdState& command) const noexcept
{
    const auto expected = next_expected_command_sequence();
    const auto sequence = command.command_sequence();
    if (!sequence.valid()) {
        return failure(
            PredictionErrorCode::prediction_command_gap,
            "synthetic authority received an invalid command sequence");
    }
    if (!expected) {
        return failure(
            PredictionErrorCode::revision_exhausted,
            "synthetic authority command sequence is exhausted",
            sequence);
    }
    if (sequence == *expected) {
        return std::nullopt;
    }
    if (sequence.value() == current_state_->source_command_sequence()) {
        return failure(
            PredictionErrorCode::duplicate_predicted_command,
            "synthetic authority received a duplicate command",
            sequence);
    }
    if (sequence.value() < expected->value()) {
        return failure(
            PredictionErrorCode::out_of_order_predicted_command,
            "synthetic authority received an out-of-order command",
            sequence);
    }
    return failure(
        PredictionErrorCode::prediction_command_gap,
        "synthetic authority command stream has a gap",
        sequence);
}

std::optional<goldsrc::GoldSrcUserCmdSequence>
SyntheticAuthoritativePlayerSimulator::released_sequence_after_accepting(
    const goldsrc::GoldSrcUserCmdState& command) const noexcept
{
    if (config_.command_delay == 0U) {
        return command.command_sequence();
    }
    if (delayed_count_ < config_.command_delay) {
        return std::nullopt;
    }
    const auto& front = delayed_updates_[delayed_head_];
    if (!front || !front->acknowledgement().sequence()) {
        return std::nullopt;
    }
    return *front->acknowledgement().sequence();
}

void SyntheticAuthoritativePlayerSimulator::push_delayed(
    AuthoritativePlayerState state) noexcept
{
    const auto index =
        (delayed_head_ + delayed_count_) % delayed_updates_.size();
    delayed_updates_[index].emplace(std::move(state));
    ++delayed_count_;
    statistics_.delayed_update_high_water_mark = std::max(
        statistics_.delayed_update_high_water_mark, delayed_count_);
}

std::optional<AuthoritativePlayerState>
SyntheticAuthoritativePlayerSimulator::pop_delayed() noexcept
{
    if (delayed_count_ == 0U) {
        return std::nullopt;
    }
    auto& front = delayed_updates_[delayed_head_];
    std::optional<AuthoritativePlayerState> result;
    if (front) {
        result.emplace(std::move(*front));
    }
    front.reset();
    delayed_head_ = (delayed_head_ + 1U) % delayed_updates_.size();
    --delayed_count_;
    return result;
}

SyntheticAuthoritativeSimulationResult
SyntheticAuthoritativePlayerSimulator::simulate_command(
    const goldsrc::GoldSrcUserCmdState& command,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch) noexcept
{
    SyntheticAuthoritativeSimulationResult result;
    if (const auto error = validate_command_sequence(command)) {
        result.error.emplace(*error);
        return result;
    }
    const auto sequence = command.command_sequence();
    if (!same_collision_session(config_.session, collision)) {
        result.error.emplace(failure(
            PredictionErrorCode::collision_world_mismatch,
            "synthetic authority collision session changed",
            sequence));
        return result;
    }
    if (update_ordinal_exhausted_ ||
        statistics_.processed_command_count >=
            config_.maximum_authoritative_updates) {
        result.error.emplace(failure(
            PredictionErrorCode::revision_exhausted,
            "synthetic authority update ordinal is exhausted",
            sequence));
        return result;
    }
    if (next_update_ordinal_ == 0U) {
        result.error.emplace(failure(
            PredictionErrorCode::invalid_authority_update_ordinal,
            "synthetic authority update ordinal is zero before simulation",
            sequence));
        return result;
    }

    auto simulated = goldsrc::movement::GoldSrcLocalMovementKernel::simulate(
        *current_state_, command, environment_, collision, scratch,
        movement_config_);
    result.simulation_statistics = simulated.statistics;
    result.simulation_touch_count = simulated.touches.size();
    if (!simulated || !simulated.state) {
        result.error.emplace(failure(
            PredictionErrorCode::prediction_replay_failed,
            simulated.error
                ? goldsrc::movement::to_string(simulated.error->code)
                : std::string_view{
                      "independent synthetic authoritative movement failed"},
            sequence));
        return result;
    }

    auto corrected = apply_scenario_correction(
        std::move(*simulated.state), config_, movement_config_, sequence);
    if (corrected.error || !corrected.state) {
        result.error = corrected.error;
        return result;
    }
    if (const auto error = validate_collision_position(
            *corrected.state, collision, scratch, movement_config_,
            sequence)) {
        result.error.emplace(*error);
        return result;
    }
    if (corrected.state->simulation_time_nanoseconds() >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        result.error.emplace(failure(
            PredictionErrorCode::invalid_authoritative_state,
            "synthetic authority time exceeds the signed time domain",
            sequence));
        return result;
    }

    AuthoritativePlayerUpdateIdentityCreateInfo identity_info;
    identity_info.session = config_.session;
    identity_info.update_ordinal = next_update_ordinal_;
    identity_info.acknowledgement =
        AuthoritativeCommandAcknowledgement::for_sequence(sequence);
    identity_info.synthetic_authority_time_nanoseconds =
        static_cast<std::int64_t>(
            corrected.state->simulation_time_nanoseconds());
    identity_info.discontinuity = corrected.discontinuity;
    auto identity = AuthoritativePlayerUpdateIdentity::create(identity_info);
    if (!identity || !identity.identity) {
        result.error = identity.error;
        return result;
    }
    auto authoritative = AuthoritativePlayerState::from_synthetic_complete_state(
        std::move(*identity.identity), std::move(*corrected.state));
    if (!authoritative || !authoritative.state) {
        result.error = authoritative.error;
        return result;
    }

    movement::LocalPlayerMovementState committed_state{
        authoritative.state->movement_state()};
    result.resulting_state_signature = authoritative.state->state_signature();
    result.discontinuity =
        authoritative.state->update_identity().discontinuity();
    result.correction_applied = corrected.correction_applied;

    if (config_.command_delay == 0U) {
        result.released_update.emplace(std::move(*authoritative.state));
    } else {
        if (delayed_count_ == config_.command_delay) {
            auto released = pop_delayed();
            if (released) {
                result.released_update.emplace(std::move(*released));
            }
        }
        push_delayed(std::move(*authoritative.state));
    }

    current_state_.emplace(std::move(committed_state));
    ++statistics_.processed_command_count;
    ++statistics_.generated_update_count;
    if (corrected.correction_applied) {
        ++statistics_.correction_count;
    }
    if (result.discontinuity == AuthoritativePlayerDiscontinuity::teleport) {
        ++statistics_.teleport_count;
    }
    if (next_update_ordinal_ == UINT64_MAX) {
        update_ordinal_exhausted_ = true;
    } else {
        ++next_update_ordinal_;
    }
    result.command_processed = true;
    return result;
}

SyntheticAuthoritativeSimulationResult
SyntheticAuthoritativePlayerSimulator::release_next_delayed() noexcept
{
    SyntheticAuthoritativeSimulationResult result;
    auto released = pop_delayed();
    if (released) {
        result.resulting_state_signature = released->state_signature();
        result.discontinuity = released->update_identity().discontinuity();
        result.released_update.emplace(std::move(*released));
    }
    return result;
}

SyntheticAuthoritativePlayerStateSource::
    SyntheticAuthoritativePlayerStateSource(
        SyntheticAuthoritativePlayerSimulator simulator) noexcept
    : simulator_{std::move(simulator)}
{
}

SyntheticAuthoritativePlayerStateSource::
    SyntheticAuthoritativePlayerStateSource(
        SyntheticAuthoritativePlayerStateSource&& other) noexcept
    : simulator_{std::move(other.simulator_)},
      output_updates_{std::move(other.output_updates_)},
      output_head_{std::exchange(other.output_head_, 0U)},
      output_count_{std::exchange(other.output_count_, 0U)},
      previous_base_update_{std::move(other.previous_base_update_)},
      stale_duplicate_injected_{
          std::exchange(other.stale_duplicate_injected_, false)},
      statistics_{std::exchange(
          other.statistics_, SyntheticAuthoritativeSourceStatistics{})}
{
}

SyntheticAuthoritativeSourceCreateResult
SyntheticAuthoritativePlayerStateSource::create(
    movement::LocalPlayerMovementState initial_state,
    goldsrc::movement::GoldSrcMovementEnvironment environment,
    SyntheticAuthoritativePlayerConfig config,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    goldsrc::movement::GoldSrcLocalMovementConfig movement_config) noexcept
{
    SyntheticAuthoritativeSourceCreateResult result;
    auto simulator = SyntheticAuthoritativePlayerSimulator::create(
        std::move(initial_state), std::move(environment), std::move(config),
        collision, scratch, std::move(movement_config));
    if (!simulator || !simulator.simulator) {
        result.error = simulator.error;
        return result;
    }
    result.source.emplace(SyntheticAuthoritativePlayerStateSource{
        std::move(*simulator.simulator)});
    return result;
}

bool SyntheticAuthoritativePlayerStateSource::stale_duplicate_injection_due(
    const goldsrc::GoldSrcUserCmdSequence released_sequence) const noexcept
{
    return simulator_.config().scenario ==
            SyntheticAuthoritativeScenario::stale_and_duplicate_updates &&
        !stale_duplicate_injected_ && previous_base_update_.has_value() &&
        released_sequence.value() >=
            simulator_.config().correction_command_sequence;
}

std::size_t SyntheticAuthoritativePlayerStateSource::required_queue_slots(
    const goldsrc::GoldSrcUserCmdSequence released_sequence) const noexcept
{
    return stale_duplicate_injection_due(released_sequence) ? 3U : 1U;
}

bool SyntheticAuthoritativePlayerStateSource::has_queue_capacity(
    const std::size_t required) const noexcept
{
    const auto maximum = simulator_.config().maximum_pending_updates;
    return required <= maximum && output_count_ <= maximum - required;
}

void SyntheticAuthoritativePlayerStateSource::queue_update(
    const AuthoritativePlayerState& update) noexcept
{
    const auto index =
        (output_head_ + output_count_) % output_updates_.size();
    output_updates_[index].emplace(update);
    ++output_count_;
    ++statistics_.queued_update_count;
    statistics_.output_queue_high_water_mark = std::max(
        statistics_.output_queue_high_water_mark, output_count_);
}

void SyntheticAuthoritativePlayerStateSource::queue_released_update(
    const AuthoritativePlayerState& update) noexcept
{
    const auto& sequence = update.acknowledgement().sequence();
    const bool inject = sequence && stale_duplicate_injection_due(*sequence);
    queue_update(update);
    if (inject) {
        queue_update(update);
        queue_update(*previous_base_update_);
        ++statistics_.duplicate_update_count;
        ++statistics_.stale_update_count;
        stale_duplicate_injected_ = true;
    }
    previous_base_update_.reset();
    previous_base_update_.emplace(update);
}

SyntheticAuthoritativeSourceOperationResult
SyntheticAuthoritativePlayerStateSource::submit_command(
    const goldsrc::GoldSrcUserCmdState& command,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch) noexcept
{
    SyntheticAuthoritativeSourceOperationResult operation;
    if (const auto error = simulator_.validate_command_sequence(command)) {
        operation.error.emplace(*error);
        operation.pending_delayed_update_count =
            simulator_.pending_delayed_update_count();
        operation.output_queue_size = output_count_;
        return operation;
    }
    const auto release_sequence =
        simulator_.released_sequence_after_accepting(command);
    if (release_sequence) {
        const auto required = required_queue_slots(*release_sequence);
        if (!has_queue_capacity(required)) {
            if (statistics_.backpressure_count != UINT64_MAX) {
                ++statistics_.backpressure_count;
            }
            operation.error.emplace(failure(
                PredictionErrorCode::authoritative_update_backpressure,
                "synthetic authoritative output queue is full",
                command.command_sequence()));
            operation.pending_delayed_update_count =
                simulator_.pending_delayed_update_count();
            operation.output_queue_size = output_count_;
            return operation;
        }
    }

    auto simulated = simulator_.simulate_command(command, collision, scratch);
    operation.command_processed = simulated.command_processed;
    if (!simulated) {
        operation.error = simulated.error;
    } else if (simulated.released_update) {
        const auto before = output_count_;
        queue_released_update(*simulated.released_update);
        operation.queued_update_count = output_count_ - before;
    }
    operation.pending_delayed_update_count =
        simulator_.pending_delayed_update_count();
    operation.output_queue_size = output_count_;
    return operation;
}

SyntheticAuthoritativeSourceOperationResult
SyntheticAuthoritativePlayerStateSource::flush_next_delayed() noexcept
{
    SyntheticAuthoritativeSourceOperationResult operation;
    if (simulator_.delayed_count_ == 0U) {
        operation.output_queue_size = output_count_;
        return operation;
    }
    const auto& front = simulator_.delayed_updates_[simulator_.delayed_head_];
    if (!front || !front->acknowledgement().sequence()) {
        operation.error.emplace(failure(
            PredictionErrorCode::invalid_authoritative_state,
            "synthetic delayed authority queue is corrupt"));
        operation.pending_delayed_update_count =
            simulator_.pending_delayed_update_count();
        operation.output_queue_size = output_count_;
        return operation;
    }
    const auto required = required_queue_slots(
        *front->acknowledgement().sequence());
    if (!has_queue_capacity(required)) {
        if (statistics_.backpressure_count != UINT64_MAX) {
            ++statistics_.backpressure_count;
        }
        operation.error.emplace(failure(
            PredictionErrorCode::authoritative_update_backpressure,
            "synthetic authoritative output queue is full"));
        operation.pending_delayed_update_count =
            simulator_.pending_delayed_update_count();
        operation.output_queue_size = output_count_;
        return operation;
    }

    auto released = simulator_.release_next_delayed();
    if (!released) {
        operation.error = released.error;
    } else if (released.released_update) {
        const auto before = output_count_;
        queue_released_update(*released.released_update);
        operation.queued_update_count = output_count_ - before;
    }
    operation.pending_delayed_update_count =
        simulator_.pending_delayed_update_count();
    operation.output_queue_size = output_count_;
    return operation;
}

AuthoritativePlayerStatePollResult
SyntheticAuthoritativePlayerStateSource::poll_next()
{
    AuthoritativePlayerStatePollResult result;
    if (output_count_ == 0U) {
        return result;
    }
    auto& front = output_updates_[output_head_];
    if (!front) {
        result.error.emplace(failure(
            PredictionErrorCode::invalid_authoritative_state,
            "synthetic authoritative output queue is corrupt"));
        return result;
    }
    result.state.emplace(std::move(*front));
    front.reset();
    output_head_ = (output_head_ + 1U) % output_updates_.size();
    --output_count_;
    ++statistics_.polled_update_count;
    return result;
}

const SyntheticAuthoritativePlayerSimulator&
SyntheticAuthoritativePlayerStateSource::simulator() const noexcept
{
    return simulator_;
}

const SyntheticAuthoritativeSourceStatistics&
SyntheticAuthoritativePlayerStateSource::statistics() const noexcept
{
    return statistics_;
}

std::size_t SyntheticAuthoritativePlayerStateSource::queued_update_count()
    const noexcept
{
    return output_count_;
}

} // namespace hlclient::prediction
