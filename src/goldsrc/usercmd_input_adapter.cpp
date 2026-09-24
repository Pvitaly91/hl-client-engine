#include <hlclient/goldsrc/usercmd_input_adapter.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace hlclient::goldsrc {
namespace {

using gameplay_input::GameplayButton;
using gameplay_input::GameplayButtonMask;

[[nodiscard]] GoldSrcUserCmdBuildResult failure(
    const GoldSrcUserCmdInputAdapterErrorCode code,
    const std::string_view context,
    const std::optional<GoldSrcUserCmdErrorCode> state_code = std::nullopt) noexcept
{
    GoldSrcUserCmdBuildResult result;
    result.error = GoldSrcUserCmdInputAdapterError{code, state_code, context};
    return result;
}

[[nodiscard]] float mapped_axis(
    const float axis,
    const float positive_speed,
    const float negative_speed) noexcept
{
    return axis >= 0.0F ? axis * positive_speed : axis * negative_speed;
}

[[nodiscard]] bool valid_speed_config(
    const GoldSrcUserCmdMovementSpeedConfig& config) noexcept
{
    return std::isfinite(config.forward_speed) &&
           std::isfinite(config.backward_speed) &&
           std::isfinite(config.side_speed) && config.forward_speed >= 0.0F &&
           config.backward_speed >= 0.0F && config.side_speed >= 0.0F;
}

} // namespace

GoldSrcUserCmdOneShotPlan::GoldSrcUserCmdOneShotPlan(
    GoldSrcUserCmdOneShotPlan&& other) noexcept
    : sequence_{other.sequence_},
      consumes_buttons_{std::exchange(other.consumes_buttons_, 0U)},
      consumes_impulse_{std::exchange(other.consumes_impulse_, false)},
      consumes_weapon_selection_{
          std::exchange(other.consumes_weapon_selection_, false)},
      consumable_{std::exchange(other.consumable_, false)},
      committed_{std::exchange(other.committed_, false)}
{
    other.sequence_ = {};
}

GoldSrcUserCmdOneShotPlan& GoldSrcUserCmdOneShotPlan::operator=(
    GoldSrcUserCmdOneShotPlan&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    sequence_ = other.sequence_;
    consumes_buttons_ = std::exchange(other.consumes_buttons_, 0U);
    consumes_impulse_ = std::exchange(other.consumes_impulse_, false);
    consumes_weapon_selection_ =
        std::exchange(other.consumes_weapon_selection_, false);
    consumable_ = std::exchange(other.consumable_, false);
    committed_ = std::exchange(other.committed_, false);
    other.sequence_ = {};
    return *this;
}

GoldSrcUserCmdSequence GoldSrcUserCmdOneShotPlan::command_sequence() const noexcept
{
    return sequence_;
}

bool GoldSrcUserCmdOneShotPlan::consumes_impulse() const noexcept
{
    return consumes_impulse_;
}

bool GoldSrcUserCmdOneShotPlan::consumes_weapon_selection() const noexcept
{
    return consumes_weapon_selection_;
}

gameplay_input::GameplayButtonMask
GoldSrcUserCmdOneShotPlan::consumes_buttons() const noexcept
{
    return consumes_buttons_;
}

bool GoldSrcUserCmdOneShotPlan::committed() const noexcept { return committed_; }

bool GoldSrcUserCmdOneShotPlan::commit_after_history_insert(
    const GoldSrcUserCmdSequence inserted_sequence) noexcept
{
    if (!consumable_ || !inserted_sequence.valid() ||
        inserted_sequence != sequence_) {
        return false;
    }
    consumable_ = false;
    committed_ = true;
    return true;
}

void GoldSrcUserCmdOneShotPlan::abandon() noexcept
{
    consumable_ = false;
}

GoldSrcUserCmdOneShotPlan::GoldSrcUserCmdOneShotPlan(
    const GoldSrcUserCmdSequence sequence,
    const gameplay_input::GameplayButtonMask consumes_buttons,
    const bool consumes_impulse,
    const bool consumes_weapon_selection) noexcept
    : sequence_{sequence},
      consumes_buttons_{consumes_buttons},
      consumes_impulse_{consumes_impulse},
      consumes_weapon_selection_{consumes_weapon_selection}
{
}

GoldSrcUserCmdButtonMapping::Result
GoldSrcUserCmdButtonMapping::synthetic_explicit_v1(
    const GameplayButtonMask held_actions) noexcept
{
    Result result;
    const auto map = [&result, held_actions](
                         const GameplayButton action,
                         const std::uint16_t wire_bit) {
        if (gameplay_input::gameplay_button_is_set(held_actions, action)) {
            result.wire_buttons |= wire_bit;
        }
    };
    map(GameplayButton::attack_primary, kSyntheticGoldSrcButtonAttack);
    map(GameplayButton::jump, kSyntheticGoldSrcButtonJump);
    map(GameplayButton::duck, kSyntheticGoldSrcButtonDuck);
    map(GameplayButton::use, kSyntheticGoldSrcButtonUse);
    map(GameplayButton::attack_secondary, kSyntheticGoldSrcButtonAttack2);
    map(GameplayButton::speed, kSyntheticGoldSrcButtonRun);
    map(GameplayButton::reload, kSyntheticGoldSrcButtonReload);

    constexpr GameplayButton ignored[]{
        GameplayButton::walk,
        GameplayButton::scoreboard,
    };
    for (const auto action : ignored) {
        if (gameplay_input::gameplay_button_is_set(held_actions, action)) {
            result.ignored_actions |= gameplay_input::gameplay_button_mask(action);
        }
    }
    return result;
}

GoldSrcUserCmdBuildResult GoldSrcUserCmdInputAdapter::build(
    const gameplay_input::GameplayInputIntent& intent,
    const gameplay_camera::GameplayCameraState& camera,
    const GoldSrcUserCmdBuildContext& context,
    const GoldSrcUserCmdLimits& limits) const noexcept
{
    if (!context.command_sequence.valid() ||
        context.command_sample_duration_nanoseconds >
            static_cast<std::uint64_t>(UINT8_MAX) * 1'000'000U ||
        !valid_speed_config(context.movement_speeds)) {
        return failure(
            GoldSrcUserCmdInputAdapterErrorCode::invalid_context,
            "Usercmd build context is invalid");
    }
    if (context.compatibility_profile !=
            GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1 ||
        context.mapping_profile !=
            GoldSrcUserCmdInputMappingProfile::synthetic_explicit_v1) {
        return failure(
            context.compatibility_profile ==
                    GoldSrcUserCmdCompatibilityProfile::
                        stock_protocol_48_evidence_pending ||
                context.mapping_profile ==
                    GoldSrcUserCmdInputMappingProfile::
                        stock_protocol_48_evidence_pending
                ? GoldSrcUserCmdInputAdapterErrorCode::stock_evidence_pending
                : GoldSrcUserCmdInputAdapterErrorCode::unsupported_profile,
            "Only the explicit synthetic input mapping is executable");
    }
    if (context.weapon_selection && *context.weapon_selection != 0U) {
        return failure(
            GoldSrcUserCmdInputAdapterErrorCode::unsupported_weapon_selection,
            "weaponselect is absent from the accepted 15-field schema descriptor");
    }

    constexpr auto known_gameplay_buttons =
        (gameplay_input::GameplayButtonMask{1U} <<
         static_cast<std::uint32_t>(GameplayButton::count)) -
        1U;
    if ((context.one_shot_buttons & ~known_gameplay_buttons) != 0U) {
        return failure(
            GoldSrcUserCmdInputAdapterErrorCode::invalid_context,
            "One-shot gameplay-button mask contains an unknown project bit");
    }
    const auto held = intent.focused()
        ? intent.held_buttons() | context.one_shot_buttons
        : 0U;
    const auto mapped_buttons =
        GoldSrcUserCmdButtonMapping::synthetic_explicit_v1(held);
    if (context.strict_unmapped_actions && mapped_buttons.ignored_actions != 0U) {
        return failure(
            GoldSrcUserCmdInputAdapterErrorCode::unsupported_action,
            "A strict synthetic input mapping received an unmapped action");
    }

    GoldSrcUserCmdCreateInfo info;
    info.lerp_msec = context.lerp_msec;
    info.msec = context.command_msec;
    // Canonical semantic order is pitch, yaw, roll. The accepted schema later
    // binds yaw before pitch; the codec owns that wire reordering.
    info.view_angles = {
        static_cast<float>(camera.pitch_degrees()),
        static_cast<float>(camera.yaw_degrees()),
        0.0F,
    };
    if (intent.focused()) {
        info.forward_move = mapped_axis(
            intent.forward_axis(),
            context.movement_speeds.forward_speed,
            context.movement_speeds.backward_speed);
        info.side_move = intent.side_axis() * context.movement_speeds.side_speed;
    }
    // The preview/free-flight diagnostic vertical axis deliberately never
    // leaks into synthetic or pending-stock upmove.
    info.up_move = 0.0F;
    info.light_level = context.light_level;
    info.buttons = mapped_buttons.wire_buttons;
    info.impulse = intent.focused() ? context.impulse.value_or(0U) : 0U;
    info.weapon_select = 0U;
    info.impact_index = context.impact_index;
    info.impact_position = context.impact_position;
    info.command_sequence = context.command_sequence;
    info.source_input_sequence = intent.input_sequence();
    info.sample_time_nanoseconds = context.command_sample_time_nanoseconds;
    info.sample_duration_nanoseconds =
        context.command_sample_duration_nanoseconds;
    info.compatibility_profile = context.compatibility_profile;
    info.input_mapping_profile = context.mapping_profile;
    info.schema_binding_profile =
        GoldSrcUserCmdSchemaBindingProfile::synthetic_usercmd_schema_v1;

    auto created = GoldSrcUserCmdState::create(info, limits);
    if (!created || !created.state) {
        return failure(
            GoldSrcUserCmdInputAdapterErrorCode::state_validation_failed,
            "Mapped input failed bounded usercmd state validation",
            created.error ? std::optional{created.error->code} : std::nullopt);
    }

    GoldSrcUserCmdBuildResult result;
    result.command.emplace(std::move(*created.state));
    result.ignored_actions = mapped_buttons.ignored_actions;
    const auto consumes_buttons = intent.focused()
        ? context.one_shot_buttons
        : gameplay_input::GameplayButtonMask{0U};
    const bool consumes_impulse = intent.focused() && context.impulse.has_value();
    const bool consumes_weapon = false;
    if (consumes_buttons != 0U || consumes_impulse || consumes_weapon) {
        result.one_shot_plan.emplace(GoldSrcUserCmdOneShotPlan{
            context.command_sequence,
            consumes_buttons,
            consumes_impulse,
            consumes_weapon});
    }
    return result;
}

GoldSrcReferenceWireUserCmdBuildResult
GoldSrcUserCmdInputAdapter::build_reference_wire(
    const gameplay_input::GameplayInputIntent& intent,
    const gameplay_camera::GameplayCameraState& camera,
    const GoldSrcUserCmdBuildContext& context,
    const GoldSrcUserCmdLimits& limits) const noexcept
{
    const auto wire_failure = [](const GoldSrcUserCmdInputAdapterErrorCode code,
                                 const std::string_view text) {
        GoldSrcReferenceWireUserCmdBuildResult result;
        result.error = GoldSrcUserCmdInputAdapterError{code, std::nullopt, text};
        return result;
    };
    if (!valid_goldsrc_usercmd_limits(limits) ||
        !context.command_sequence.valid() ||
        context.command_msec > limits.maximum_msec ||
        context.command_sample_duration_nanoseconds >
            static_cast<std::uint64_t>(limits.maximum_msec) * 1'000'000U ||
        context.lerp_msec > limits.maximum_lerp_msec ||
        !valid_speed_config(context.movement_speeds) ||
        !std::isfinite(context.reference_movement.speed_key_multiplier) ||
        context.reference_movement.speed_key_multiplier <= 0.0F ||
        context.reference_movement.speed_key_multiplier > 1.0F ||
        (context.reference_movement.client_maxspeed &&
         (!std::isfinite(*context.reference_movement.client_maxspeed) ||
          *context.reference_movement.client_maxspeed < 0.0F))) {
        return wire_failure(
            GoldSrcUserCmdInputAdapterErrorCode::invalid_context,
            "Reference wire-command build context is invalid");
    }
    constexpr auto jump = gameplay_input::gameplay_button_mask(
        gameplay_input::GameplayButton::jump);
    constexpr auto duck = gameplay_input::gameplay_button_mask(
        gameplay_input::GameplayButton::duck);
    constexpr auto speed = gameplay_input::gameplay_button_mask(
        gameplay_input::GameplayButton::speed);
    constexpr auto permitted_actions = jump | duck | speed;
    const auto observed_actions = intent.held_buttons() |
        intent.pressed_buttons() | intent.released_buttons() |
        context.one_shot_buttons;
    if ((context.reference_button_policy == GoldSrcReferenceButtonPolicy::none &&
         observed_actions != 0U) ||
        (context.reference_button_policy == GoldSrcReferenceButtonPolicy::jump_duck &&
         (observed_actions & ~permitted_actions) != 0U) ||
        context.impulse.value_or(0U) != 0U ||
        context.weapon_selection.value_or(0U) != 0U ||
        context.impact_index != 0 ||
        std::ranges::any_of(context.impact_position, [](const float value) {
            return value != 0.0F;
        })) {
        return wire_failure(
            GoldSrcUserCmdInputAdapterErrorCode::unsupported_action,
            "Controlled reference movement rejects actions outside its selected jump/duck policy");
    }

    const auto pitch = quantize_wire_angle(camera.pitch_degrees());
    const auto yaw = quantize_wire_angle(camera.yaw_degrees());
    const auto roll = quantize_wire_angle(0.0);
    float requested_forward = intent.focused()
        ? mapped_axis(intent.forward_axis(),
              context.movement_speeds.forward_speed,
              context.movement_speeds.backward_speed)
        : 0.0F;
    float requested_side = intent.focused()
        ? intent.side_axis() * context.movement_speeds.side_speed
        : 0.0F;
    float forward_value = requested_forward;
    float side_value = requested_side;
    const float multiplier = intent.focused() && (intent.held_buttons() & speed)
        ? context.reference_movement.speed_key_multiplier : 1.0F;
    forward_value *= multiplier;
    side_value *= multiplier;
    // Reference order: speed key first, then a uniform vector limit.  Zero
    // clientmaxspeed is a no-limit sentinel in the pinned Valve input path.
    if (context.reference_movement.client_maxspeed &&
        *context.reference_movement.client_maxspeed > 0.0F) {
        const double magnitude = std::hypot(static_cast<double>(forward_value),
                                            static_cast<double>(side_value));
        if (magnitude > *context.reference_movement.client_maxspeed) {
            const auto scale = static_cast<float>(
                static_cast<double>(*context.reference_movement.client_maxspeed) /
                magnitude);
            forward_value *= scale;
            side_value *= scale;
        }
    }
    const auto forward = quantize_wire_movement(forward_value);
    const auto side = quantize_wire_movement(side_value);
    if (!pitch || !yaw || !roll || !forward || !side) {
        return wire_failure(
            GoldSrcUserCmdInputAdapterErrorCode::state_validation_failed,
            "Controlled reference movement could not be quantized exactly");
    }

    GoldSrcWireUserCmd command;
    command.lerp_msec = context.lerp_msec;
    command.msec = context.command_msec;
    command.angle_turns = {*pitch, *yaw, *roll};
    command.forward = *forward;
    command.side = *side;
    command.up = 0;
    if (context.reference_button_policy == GoldSrcReferenceButtonPolicy::jump_duck &&
        intent.focused()) {
        const auto actions = intent.held_buttons() | context.one_shot_buttons;
        command.buttons = static_cast<std::uint16_t>(
            ((actions & jump) != 0U ? kReferenceGoldSrcButtonJump : 0U) |
            ((actions & duck) != 0U ? kReferenceGoldSrcButtonDuck : 0U));
    } else {
        command.buttons = 0U;
    }
    command.light_level = context.light_level;
    command.impulse = 0U;
    command.impact_index = 0U;
    if (!valid_wire_usercmd(command)) {
        return wire_failure(
            GoldSrcUserCmdInputAdapterErrorCode::state_validation_failed,
            "Controlled reference movement failed wire-value validation");
    }
    GoldSrcReferenceWireUserCmdBuildResult result;
    result.command = command;
    result.requested_forward = requested_forward;
    result.requested_side = requested_side;
    result.applied_speed_multiplier = multiplier;
    result.client_maxspeed = context.reference_movement.client_maxspeed;
    if (intent.focused() && context.one_shot_buttons != 0U) {
        result.one_shot_plan.emplace(GoldSrcUserCmdOneShotPlan{
            context.command_sequence, context.one_shot_buttons, false, false});
    }
    return result;
}

} // namespace hlclient::goldsrc
