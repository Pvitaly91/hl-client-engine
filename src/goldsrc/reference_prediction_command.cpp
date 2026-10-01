#include <hlclient/goldsrc/reference_prediction_command.hpp>

namespace hlclient::goldsrc {

GoldSrcUserCmdState::CreationResult reference_dry_walk_movement_command(
    const GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept
{
    if (!identity.valid() || !valid_wire_usercmd(wire)) {
        return {{}, GoldSrcUserCmdError{
            GoldSrcUserCmdErrorCode::invalid_sequence,
            "prediction command identity or immutable wire value is invalid"}};
    }
    if (wire.msec == 0U || wire.msec > 50U ||
        (wire.buttons & ~kReferenceGoldSrcButtonDirections) != 0U ||
        wire.up != 0 || wire.impulse != 0U || wire.impact_index != 0U ||
        wire.impact_eighths != std::array<std::int16_t, 3U>{}) {
        return {{}, GoldSrcUserCmdError{
            GoldSrcUserCmdErrorCode::unsupported_field_mapping,
            "reference dry-walk prediction excludes action, upmove and long commands"}};
    }
    constexpr float kDegreesPerTurn = 360.0F / 65'536.0F;
    GoldSrcUserCmdCreateInfo info;
    info.command_sequence = identity;
    info.compatibility_profile = GoldSrcUserCmdCompatibilityProfile::
        public_goldsrc48_dry_walk_prediction_v1;
    info.input_mapping_profile = GoldSrcUserCmdInputMappingProfile::
        reference_immutable_wire_v1;
    info.schema_binding_profile = GoldSrcUserCmdSchemaBindingProfile::
        public_goldsrc48_usercmd_schema_v1;
    info.lerp_msec = wire.lerp_msec;
    info.msec = wire.msec;
    info.view_angles = {
        static_cast<float>(wire.angle_turns[0U]) * kDegreesPerTurn,
        static_cast<float>(wire.angle_turns[1U]) * kDegreesPerTurn,
        static_cast<float>(wire.angle_turns[2U]) * kDegreesPerTurn};
    info.forward_move = static_cast<float>(wire.forward);
    info.side_move = static_cast<float>(wire.side);
    info.light_level = wire.light_level;
    info.sample_duration_nanoseconds =
        static_cast<std::uint64_t>(wire.msec) * 1'000'000ULL;
    return GoldSrcUserCmdState::create(info);
}

static GoldSrcUserCmdState::CreationResult reference_actions_movement_command(
    const GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire,
    const bool weapon_buttons, const bool use_button = false) noexcept
{
    constexpr std::uint16_t movement_allowed =
        kReferenceGoldSrcButtonDirections |
        kReferenceGoldSrcButtonJump | kReferenceGoldSrcButtonDuck;
    constexpr std::uint16_t weapon_allowed =
        kReferenceGoldSrcButtonAttack | kReferenceGoldSrcButtonReload;
    const std::uint16_t allowed = movement_allowed |
        (weapon_buttons ? weapon_allowed : 0U) |
        (use_button ? kReferenceGoldSrcButtonUse : 0U);
    if (!identity.valid() || !valid_wire_usercmd(wire) || wire.msec == 0U ||
        wire.msec > 50U || (wire.buttons & ~allowed) != 0U ||
        wire.up != 0 || wire.impulse != 0U || wire.impact_index != 0U ||
        wire.impact_eighths != std::array<std::int16_t, 3U>{}) {
        return {{}, GoldSrcUserCmdError{
            GoldSrcUserCmdErrorCode::unsupported_field_mapping,
            "reference jump/duck command exceeds the action profile"}};
    }
    constexpr float degrees_per_turn = 360.0F / 65'536.0F;
    GoldSrcUserCmdCreateInfo info;
    info.command_sequence = identity;
    info.compatibility_profile = use_button
        ? GoldSrcUserCmdCompatibilityProfile::public_goldsrc48_jump_duck_weapon_use_prediction_v4
        : weapon_buttons
        ? GoldSrcUserCmdCompatibilityProfile::
              public_goldsrc48_jump_duck_weapon_prediction_v3
        : GoldSrcUserCmdCompatibilityProfile::
              public_goldsrc48_jump_duck_prediction_v2;
    info.input_mapping_profile = GoldSrcUserCmdInputMappingProfile::
        reference_immutable_wire_v1;
    info.schema_binding_profile = GoldSrcUserCmdSchemaBindingProfile::
        public_goldsrc48_usercmd_schema_v1;
    info.lerp_msec = wire.lerp_msec;
    info.msec = wire.msec;
    info.view_angles = {
        static_cast<float>(wire.angle_turns[0U]) * degrees_per_turn,
        static_cast<float>(wire.angle_turns[1U]) * degrees_per_turn,
        static_cast<float>(wire.angle_turns[2U]) * degrees_per_turn};
    info.forward_move = static_cast<float>(wire.forward);
    info.side_move = static_cast<float>(wire.side);
    info.buttons = wire.buttons;
    info.light_level = wire.light_level;
    info.sample_duration_nanoseconds =
        static_cast<std::uint64_t>(wire.msec) * 1'000'000ULL;
    return GoldSrcUserCmdState::create(info);
}

GoldSrcUserCmdState::CreationResult reference_jump_duck_movement_command(
    const GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept
{
    return reference_actions_movement_command(identity, wire, false);
}

GoldSrcUserCmdState::CreationResult reference_jump_duck_weapon_movement_command(
    const GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept
{
    return reference_actions_movement_command(identity, wire, true);
}

GoldSrcUserCmdState::CreationResult reference_jump_duck_weapon_use_movement_command(
    const GoldSrcUserCmdSequence identity, const GoldSrcWireUserCmd& wire) noexcept
{
    return reference_actions_movement_command(identity, wire, true, true);
}

} // namespace hlclient::goldsrc
