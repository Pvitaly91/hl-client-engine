#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::size_t kGoldSrcUserCmdSchemaFieldCount = 15U;
inline constexpr std::size_t kDefaultGoldSrcUserCmdHistoryEntries = 64U;
inline constexpr std::size_t kMaximumGoldSrcUserCmdHistoryEntries = 256U;

// Stable synthetic command-state button bits.  These live beside the command
// value type so offline consumers do not need the gameplay input adapter.
inline constexpr std::uint16_t kSyntheticGoldSrcButtonAttack = 1U << 0U;
inline constexpr std::uint16_t kSyntheticGoldSrcButtonJump = 1U << 1U;
inline constexpr std::uint16_t kSyntheticGoldSrcButtonDuck = 1U << 2U;
inline constexpr std::uint16_t kSyntheticGoldSrcButtonUse = 1U << 5U;
inline constexpr std::uint16_t kSyntheticGoldSrcButtonAttack2 = 1U << 11U;
inline constexpr std::uint16_t kSyntheticGoldSrcButtonRun = 1U << 12U;
inline constexpr std::uint16_t kSyntheticGoldSrcButtonReload = 1U << 13U;

enum class GoldSrcUserCmdCompatibilityProfile : std::uint8_t {
    synthetic_usercmd_v1,
    stock_protocol_48_build_10210,
    stock_protocol_48_evidence_pending,
};

enum class GoldSrcUserCmdInputMappingProfile : std::uint8_t {
    synthetic_explicit_v1,
    stock_protocol_48_controlled_profile_v1,
    stock_protocol_48_evidence_pending,
};

enum class GoldSrcUserCmdSchemaBindingProfile : std::uint8_t {
    synthetic_usercmd_schema_v1,
    stock_protocol_48_build_10210_schema_only,
    stock_protocol_48_evidence_pending,
};

class GoldSrcUserCmdSequence final {
public:
    constexpr GoldSrcUserCmdSequence() noexcept = default;

    [[nodiscard]] static constexpr std::optional<GoldSrcUserCmdSequence> create(
        const std::uint32_t value,
        const std::uint32_t maximum = UINT32_MAX) noexcept
    {
        if (value == 0U || value > maximum) {
            return std::nullopt;
        }
        return GoldSrcUserCmdSequence{value};
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value_ != 0U;
    }

    friend constexpr bool operator==(
        const GoldSrcUserCmdSequence&,
        const GoldSrcUserCmdSequence&) noexcept = default;

private:
    explicit constexpr GoldSrcUserCmdSequence(const std::uint32_t value) noexcept
        : value_{value}
    {
    }

    std::uint32_t value_{0U};
};

struct GoldSrcUserCmdLimits {
    std::uint16_t maximum_msec{255U};
    std::uint16_t maximum_lerp_msec{511U};
    float maximum_move_magnitude{2'047.0F};
    float maximum_angle_magnitude{360.0F};
    float maximum_impact_position_magnitude{4'095.875F};
    std::uint16_t maximum_buttons_mask{UINT16_MAX};
    std::uint32_t maximum_command_sequence{UINT32_MAX};
    std::size_t maximum_history_entries{kDefaultGoldSrcUserCmdHistoryEntries};
    std::size_t maximum_commands_per_packet{16U};
    std::size_t maximum_backup_commands{7U};
    std::size_t maximum_new_commands{8U};
    std::size_t maximum_encoded_bits{8'192U};
    std::size_t maximum_encoded_bytes{1'024U};
};

inline constexpr GoldSrcUserCmdLimits kGoldSrcUserCmdHardLimits{
    255U,
    511U,
    32'767.0F,
    360'000.0F,
    4'095.875F,
    UINT16_MAX,
    UINT32_MAX,
    kMaximumGoldSrcUserCmdHistoryEntries,
    32U,
    15U,
    15U,
    65'536U,
    8'192U,
};

[[nodiscard]] bool valid_goldsrc_usercmd_limits(
    const GoldSrcUserCmdLimits& limits) noexcept;

enum class GoldSrcUserCmdErrorCode : std::uint8_t {
    invalid_limits,
    invalid_sequence,
    invalid_profile,
    stock_evidence_pending,
    non_finite_value,
    duration_out_of_range,
    lerp_out_of_range,
    movement_out_of_range,
    angle_out_of_range,
    buttons_out_of_range,
    impact_out_of_range,
    impossible_impact_fields,
    unsupported_field_mapping,
};

struct GoldSrcUserCmdError {
    GoldSrcUserCmdErrorCode code{GoldSrcUserCmdErrorCode::invalid_limits};
    std::string_view context;
};

struct GoldSrcUserCmdCreateInfo {
    std::uint16_t lerp_msec{0U};
    std::uint8_t msec{0U};
    std::array<float, 3U> view_angles{};
    float forward_move{0.0F};
    float side_move{0.0F};
    float up_move{0.0F};
    std::uint8_t light_level{0U};
    std::uint16_t buttons{0U};
    std::uint8_t impulse{0U};
    std::uint8_t weapon_select{0U};
    std::int32_t impact_index{0};
    std::array<float, 3U> impact_position{};
    GoldSrcUserCmdSequence command_sequence{};
    GoldSrcUserCmdCompatibilityProfile compatibility_profile{
        GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1};
    GoldSrcUserCmdInputMappingProfile input_mapping_profile{
        GoldSrcUserCmdInputMappingProfile::synthetic_explicit_v1};
    GoldSrcUserCmdSchemaBindingProfile schema_binding_profile{
        GoldSrcUserCmdSchemaBindingProfile::synthetic_usercmd_schema_v1};
    std::uint64_t source_input_sequence{0U};
    std::int64_t sample_time_nanoseconds{0};
    std::uint64_t sample_duration_nanoseconds{0U};
};

class GoldSrcUserCmdState final {
public:
    struct CreationResult;

    GoldSrcUserCmdState(const GoldSrcUserCmdState&) = default;
    GoldSrcUserCmdState(GoldSrcUserCmdState&&) noexcept = default;
    GoldSrcUserCmdState& operator=(const GoldSrcUserCmdState&) = delete;
    GoldSrcUserCmdState& operator=(GoldSrcUserCmdState&&) = delete;
    ~GoldSrcUserCmdState() = default;

    [[nodiscard]] static CreationResult create(
        const GoldSrcUserCmdCreateInfo& create_info,
        const GoldSrcUserCmdLimits& limits = {}) noexcept;

    [[nodiscard]] std::uint16_t lerp_msec() const noexcept;
    [[nodiscard]] std::uint8_t msec() const noexcept;
    [[nodiscard]] const std::array<float, 3U>& view_angles() const noexcept;
    [[nodiscard]] float forward_move() const noexcept;
    [[nodiscard]] float side_move() const noexcept;
    [[nodiscard]] float up_move() const noexcept;
    [[nodiscard]] std::uint8_t light_level() const noexcept;
    [[nodiscard]] std::uint16_t buttons() const noexcept;
    [[nodiscard]] std::uint8_t impulse() const noexcept;
    [[nodiscard]] std::uint8_t weapon_select() const noexcept;
    [[nodiscard]] std::int32_t impact_index() const noexcept;
    [[nodiscard]] const std::array<float, 3U>& impact_position() const noexcept;
    [[nodiscard]] GoldSrcUserCmdSequence command_sequence() const noexcept;
    [[nodiscard]] GoldSrcUserCmdCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] GoldSrcUserCmdInputMappingProfile input_mapping_profile()
        const noexcept;
    [[nodiscard]] GoldSrcUserCmdSchemaBindingProfile schema_binding_profile()
        const noexcept;
    [[nodiscard]] std::uint64_t source_input_sequence() const noexcept;
    [[nodiscard]] std::int64_t sample_time_nanoseconds() const noexcept;
    [[nodiscard]] std::uint64_t sample_duration_nanoseconds() const noexcept;

private:
    explicit GoldSrcUserCmdState(
        const GoldSrcUserCmdCreateInfo& create_info) noexcept;

    GoldSrcUserCmdCreateInfo values_;
};

struct GoldSrcUserCmdState::CreationResult {
    std::optional<GoldSrcUserCmdState> state;
    std::optional<GoldSrcUserCmdError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

[[nodiscard]] GoldSrcUserCmdCreateInfo goldsrc_usercmd_default_create_info(
    GoldSrcUserCmdSequence sequence,
    std::int64_t sample_time_nanoseconds = 0) noexcept;

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcUserCmdCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1:
        return "synthetic_usercmd_v1";
    case GoldSrcUserCmdCompatibilityProfile::stock_protocol_48_build_10210:
        return "stock_protocol_48_build_10210";
    case GoldSrcUserCmdCompatibilityProfile::stock_protocol_48_evidence_pending:
        return "stock_protocol_48_evidence_pending";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcUserCmdErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcUserCmdErrorCode::invalid_limits: return "invalid_limits";
    case GoldSrcUserCmdErrorCode::invalid_sequence: return "invalid_sequence";
    case GoldSrcUserCmdErrorCode::invalid_profile: return "invalid_profile";
    case GoldSrcUserCmdErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case GoldSrcUserCmdErrorCode::non_finite_value: return "non_finite_value";
    case GoldSrcUserCmdErrorCode::duration_out_of_range:
        return "duration_out_of_range";
    case GoldSrcUserCmdErrorCode::lerp_out_of_range: return "lerp_out_of_range";
    case GoldSrcUserCmdErrorCode::movement_out_of_range:
        return "movement_out_of_range";
    case GoldSrcUserCmdErrorCode::angle_out_of_range: return "angle_out_of_range";
    case GoldSrcUserCmdErrorCode::buttons_out_of_range:
        return "buttons_out_of_range";
    case GoldSrcUserCmdErrorCode::impact_out_of_range:
        return "impact_out_of_range";
    case GoldSrcUserCmdErrorCode::impossible_impact_fields:
        return "impossible_impact_fields";
    case GoldSrcUserCmdErrorCode::unsupported_field_mapping:
        return "unsupported_field_mapping";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
