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
    if (wire.msec == 0U || wire.msec > 50U || wire.buttons != 0U ||
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

} // namespace hlclient::goldsrc
