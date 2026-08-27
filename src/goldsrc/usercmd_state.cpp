#include <hlclient/goldsrc/usercmd_state.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool finite_array(const std::array<float, 3U>& values) noexcept
{
    return std::ranges::all_of(values, [](const float value) {
        return std::isfinite(value);
    });
}

[[nodiscard]] bool magnitude_within(
    const std::array<float, 3U>& values,
    const float maximum) noexcept
{
    return std::ranges::all_of(values, [maximum](const float value) {
        return std::abs(value) <= maximum;
    });
}

[[nodiscard]] GoldSrcUserCmdState::CreationResult failure(
    const GoldSrcUserCmdErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, GoldSrcUserCmdError{code, context}};
}

[[nodiscard]] bool synthetic_profile_tuple(
    const GoldSrcUserCmdCreateInfo& info) noexcept
{
    return info.compatibility_profile ==
               GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1 &&
           info.input_mapping_profile ==
               GoldSrcUserCmdInputMappingProfile::synthetic_explicit_v1 &&
           info.schema_binding_profile ==
               GoldSrcUserCmdSchemaBindingProfile::synthetic_usercmd_schema_v1;
}

} // namespace

bool valid_goldsrc_usercmd_limits(const GoldSrcUserCmdLimits& limits) noexcept
{
    return limits.maximum_msec > 0U &&
           limits.maximum_msec <= kGoldSrcUserCmdHardLimits.maximum_msec &&
           limits.maximum_lerp_msec <=
               kGoldSrcUserCmdHardLimits.maximum_lerp_msec &&
           std::isfinite(limits.maximum_move_magnitude) &&
           limits.maximum_move_magnitude > 0.0F &&
           limits.maximum_move_magnitude <=
               kGoldSrcUserCmdHardLimits.maximum_move_magnitude &&
           std::isfinite(limits.maximum_angle_magnitude) &&
           limits.maximum_angle_magnitude >= 360.0F &&
           limits.maximum_angle_magnitude <=
               kGoldSrcUserCmdHardLimits.maximum_angle_magnitude &&
           std::isfinite(limits.maximum_impact_position_magnitude) &&
           limits.maximum_impact_position_magnitude > 0.0F &&
           limits.maximum_impact_position_magnitude <=
               kGoldSrcUserCmdHardLimits.maximum_impact_position_magnitude &&
           limits.maximum_buttons_mask != 0U &&
           limits.maximum_buttons_mask <=
               kGoldSrcUserCmdHardLimits.maximum_buttons_mask &&
           limits.maximum_command_sequence > 0U &&
           limits.maximum_history_entries > 0U &&
           limits.maximum_history_entries <=
               kGoldSrcUserCmdHardLimits.maximum_history_entries &&
           limits.maximum_commands_per_packet > 0U &&
           limits.maximum_commands_per_packet <=
               kGoldSrcUserCmdHardLimits.maximum_commands_per_packet &&
           limits.maximum_backup_commands <=
               kGoldSrcUserCmdHardLimits.maximum_backup_commands &&
           limits.maximum_new_commands > 0U &&
           limits.maximum_new_commands <=
               kGoldSrcUserCmdHardLimits.maximum_new_commands &&
           limits.maximum_backup_commands + limits.maximum_new_commands <=
               limits.maximum_commands_per_packet &&
           limits.maximum_encoded_bits > 0U &&
           limits.maximum_encoded_bits <=
               kGoldSrcUserCmdHardLimits.maximum_encoded_bits &&
           limits.maximum_encoded_bytes > 0U &&
           limits.maximum_encoded_bytes <=
               kGoldSrcUserCmdHardLimits.maximum_encoded_bytes &&
           limits.maximum_encoded_bits <= limits.maximum_encoded_bytes * 8U;
}

GoldSrcUserCmdState::CreationResult GoldSrcUserCmdState::create(
    const GoldSrcUserCmdCreateInfo& create_info,
    const GoldSrcUserCmdLimits& limits) noexcept
{
    if (!valid_goldsrc_usercmd_limits(limits)) {
        return failure(
            GoldSrcUserCmdErrorCode::invalid_limits,
            "Usercmd safety limits are invalid");
    }
    if (!create_info.command_sequence.valid() ||
        create_info.command_sequence.value() > limits.maximum_command_sequence) {
        return failure(
            GoldSrcUserCmdErrorCode::invalid_sequence,
            "Usercmd project-local sequence is outside its configured domain");
    }
    if (create_info.compatibility_profile ==
            GoldSrcUserCmdCompatibilityProfile::stock_protocol_48_build_10210 ||
        create_info.input_mapping_profile ==
            GoldSrcUserCmdInputMappingProfile::
                stock_protocol_48_controlled_profile_v1) {
        return failure(
            GoldSrcUserCmdErrorCode::stock_evidence_pending,
            "A controlled stock usercmd profile requires accepted wire evidence");
    }
    if (!synthetic_profile_tuple(create_info)) {
        return failure(
            create_info.compatibility_profile ==
                    GoldSrcUserCmdCompatibilityProfile::
                        stock_protocol_48_evidence_pending
                ? GoldSrcUserCmdErrorCode::stock_evidence_pending
                : GoldSrcUserCmdErrorCode::invalid_profile,
            "Usercmd compatibility, input, and schema profiles do not form a supported tuple");
    }
    if (create_info.lerp_msec > limits.maximum_lerp_msec) {
        return failure(
            GoldSrcUserCmdErrorCode::lerp_out_of_range,
            "Usercmd lerp duration exceeds the configured bound");
    }
    if (create_info.msec > limits.maximum_msec ||
        create_info.sample_duration_nanoseconds >
            static_cast<std::uint64_t>(limits.maximum_msec) * 1'000'000U) {
        return failure(
            GoldSrcUserCmdErrorCode::duration_out_of_range,
            "Usercmd sample duration exceeds the configured wire duration");
    }
    if (!finite_array(create_info.view_angles) ||
        !finite_array(create_info.impact_position) ||
        !std::isfinite(create_info.forward_move) ||
        !std::isfinite(create_info.side_move) ||
        !std::isfinite(create_info.up_move)) {
        return failure(
            GoldSrcUserCmdErrorCode::non_finite_value,
            "Usercmd numeric values must be finite");
    }
    if (!magnitude_within(
            create_info.view_angles, limits.maximum_angle_magnitude)) {
        return failure(
            GoldSrcUserCmdErrorCode::angle_out_of_range,
            "Usercmd view angle exceeds the configured magnitude");
    }
    if (std::abs(create_info.forward_move) > limits.maximum_move_magnitude ||
        std::abs(create_info.side_move) > limits.maximum_move_magnitude ||
        std::abs(create_info.up_move) > limits.maximum_move_magnitude) {
        return failure(
            GoldSrcUserCmdErrorCode::movement_out_of_range,
            "Usercmd movement exceeds the configured magnitude");
    }
    if ((create_info.buttons & ~limits.maximum_buttons_mask) != 0U) {
        return failure(
            GoldSrcUserCmdErrorCode::buttons_out_of_range,
            "Usercmd button mask contains unsupported bits");
    }
    if (create_info.impact_index < 0 || create_info.impact_index > 63 ||
        !magnitude_within(
            create_info.impact_position,
            limits.maximum_impact_position_magnitude)) {
        return failure(
            GoldSrcUserCmdErrorCode::impact_out_of_range,
            "Usercmd impact metadata exceeds its synthetic schema bounds");
    }
    const bool impact_position_is_zero = std::ranges::all_of(
        create_info.impact_position,
        [](const float value) { return value == 0.0F; });
    if ((create_info.impact_index == 0) != impact_position_is_zero) {
        return failure(
            GoldSrcUserCmdErrorCode::impossible_impact_fields,
            "Impact index and position must both be absent or both be present");
    }

    return {GoldSrcUserCmdState{create_info}, std::nullopt};
}

GoldSrcUserCmdState::GoldSrcUserCmdState(
    const GoldSrcUserCmdCreateInfo& create_info) noexcept
    : values_{create_info}
{
}

std::uint16_t GoldSrcUserCmdState::lerp_msec() const noexcept
{
    return values_.lerp_msec;
}

std::uint8_t GoldSrcUserCmdState::msec() const noexcept { return values_.msec; }

const std::array<float, 3U>& GoldSrcUserCmdState::view_angles() const noexcept
{
    return values_.view_angles;
}

float GoldSrcUserCmdState::forward_move() const noexcept
{
    return values_.forward_move;
}

float GoldSrcUserCmdState::side_move() const noexcept { return values_.side_move; }
float GoldSrcUserCmdState::up_move() const noexcept { return values_.up_move; }
std::uint8_t GoldSrcUserCmdState::light_level() const noexcept
{
    return values_.light_level;
}
std::uint16_t GoldSrcUserCmdState::buttons() const noexcept
{
    return values_.buttons;
}
std::uint8_t GoldSrcUserCmdState::impulse() const noexcept
{
    return values_.impulse;
}
std::uint8_t GoldSrcUserCmdState::weapon_select() const noexcept
{
    return values_.weapon_select;
}
std::int32_t GoldSrcUserCmdState::impact_index() const noexcept
{
    return values_.impact_index;
}
const std::array<float, 3U>& GoldSrcUserCmdState::impact_position() const noexcept
{
    return values_.impact_position;
}
GoldSrcUserCmdSequence GoldSrcUserCmdState::command_sequence() const noexcept
{
    return values_.command_sequence;
}
GoldSrcUserCmdCompatibilityProfile
GoldSrcUserCmdState::compatibility_profile() const noexcept
{
    return values_.compatibility_profile;
}
GoldSrcUserCmdInputMappingProfile
GoldSrcUserCmdState::input_mapping_profile() const noexcept
{
    return values_.input_mapping_profile;
}
GoldSrcUserCmdSchemaBindingProfile
GoldSrcUserCmdState::schema_binding_profile() const noexcept
{
    return values_.schema_binding_profile;
}
std::uint64_t GoldSrcUserCmdState::source_input_sequence() const noexcept
{
    return values_.source_input_sequence;
}
std::int64_t GoldSrcUserCmdState::sample_time_nanoseconds() const noexcept
{
    return values_.sample_time_nanoseconds;
}
std::uint64_t GoldSrcUserCmdState::sample_duration_nanoseconds() const noexcept
{
    return values_.sample_duration_nanoseconds;
}

GoldSrcUserCmdCreateInfo goldsrc_usercmd_default_create_info(
    const GoldSrcUserCmdSequence sequence,
    const std::int64_t sample_time_nanoseconds) noexcept
{
    GoldSrcUserCmdCreateInfo info;
    info.command_sequence = sequence;
    info.sample_time_nanoseconds = sample_time_nanoseconds;
    return info;
}

} // namespace hlclient::goldsrc
