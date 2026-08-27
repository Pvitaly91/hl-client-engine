#pragma once

#include <hlclient/goldsrc/usercmd_schema_binding.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kGoldSrcUserCmdDeltaMaskBytes = 2U;

enum class GoldSrcUserCmdDeltaCompatibilityProfile : std::uint8_t {
    stock_protocol_48_build_10210_usercmd_v1,
    synthetic_usercmd_delta_v1,
    stock_evidence_pending,
};

enum class GoldSrcUserCmdDeltaBasePolicy : std::uint8_t {
    explicit_command,
    synthetic_default_state,
};

enum class GoldSrcUserCmdDeltaEndPolicy : std::uint8_t {
    leave_trailing_bits,
    require_exact_end,
};

enum class GoldSrcUserCmdDeltaErrorCode : std::uint8_t {
    invalid_configuration,
    stock_evidence_pending,
    invalid_binding,
    incompatible_state_profile,
    invalid_base_policy,
    unsupported_weapon_selection,
    invalid_input_geometry,
    invalid_decode_context,
    numeric_overflow,
    encoded_limit_exceeded,
    writer_failure,
    truncated_mask,
    mask_byte_count_exceeded,
    noncanonical_mask,
    mask_bit_out_of_range,
    truncated_value,
    nonzero_padding,
    unexpected_trailing_bits,
    state_validation_failed,
};

struct GoldSrcUserCmdDeltaError {
    GoldSrcUserCmdDeltaErrorCode code{
        GoldSrcUserCmdDeltaErrorCode::invalid_configuration};
    std::size_t bit_offset{0U};
    std::optional<std::size_t> field_index;
    std::string_view context;
};

struct GoldSrcUserCmdDeltaEncodeContext {
    GoldSrcUserCmdDeltaBasePolicy base_policy{
        GoldSrcUserCmdDeltaBasePolicy::explicit_command};
};

struct GoldSrcUserCmdDeltaDecodeContext {
    std::span<const std::byte> bytes;
    std::size_t start_bit_offset{0U};
    std::size_t bit_length{static_cast<std::size_t>(-1)};
    GoldSrcUserCmdDeltaEndPolicy end_policy{
        GoldSrcUserCmdDeltaEndPolicy::leave_trailing_bits};
    GoldSrcUserCmdDeltaBasePolicy base_policy{
        GoldSrcUserCmdDeltaBasePolicy::explicit_command};

    // Command identity and sampling metadata are deliberately supplied by the
    // caller because the synthetic 15-field delta contains none of them.
    GoldSrcUserCmdSequence command_sequence{};
    std::uint64_t source_input_sequence{0U};
    std::int64_t sample_time_nanoseconds{0};
    std::optional<std::uint64_t> sample_duration_nanoseconds;
};

struct GoldSrcUserCmdEncodedDelta {
    std::vector<std::byte> bytes;
    std::size_t bit_length{0U};
    std::size_t meaningful_bit_length{0U};
    std::size_t padding_bits{0U};
    std::size_t changed_field_count{0U};
    std::uint8_t mask_byte_count{0U};
    std::array<std::uint8_t, kGoldSrcUserCmdDeltaMaskBytes> field_mask{};
};

struct GoldSrcUserCmdDeltaEncodeResult {
    std::optional<GoldSrcUserCmdEncodedDelta> delta;
    std::optional<GoldSrcUserCmdDeltaError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return delta.has_value();
    }
};

struct GoldSrcUserCmdDeltaDecodeResult {
    std::optional<GoldSrcUserCmdState> state;
    std::optional<GoldSrcUserCmdDeltaError> error;
    std::size_t bits_consumed{0U};
    std::size_t next_bit_offset{0U};
    std::size_t next_byte_offset{0U};
    std::size_t changed_field_count{0U};
    std::uint8_t mask_byte_count{0U};
    std::array<std::uint8_t, kGoldSrcUserCmdDeltaMaskBytes> field_mask{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class GoldSrcUserCmdDeltaCodec final {
public:
    explicit GoldSrcUserCmdDeltaCodec(
        GoldSrcUserCmdLimits limits = {},
        GoldSrcUserCmdDeltaCompatibilityProfile profile =
            GoldSrcUserCmdDeltaCompatibilityProfile::
                stock_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const GoldSrcUserCmdLimits& limits() const noexcept;
    [[nodiscard]] GoldSrcUserCmdDeltaCompatibilityProfile profile()
        const noexcept;

    [[nodiscard]] GoldSrcUserCmdDeltaEncodeResult encode_delta(
        const GoldSrcUserCmdState& base,
        const GoldSrcUserCmdState& current,
        const GoldSrcUserCmdSchemaBinding& binding,
        const GoldSrcUserCmdDeltaEncodeContext& context = {}) const;

    [[nodiscard]] GoldSrcUserCmdDeltaDecodeResult decode_delta(
        const GoldSrcUserCmdState& base,
        const GoldSrcUserCmdSchemaBinding& binding,
        const GoldSrcUserCmdDeltaDecodeContext& context) const;

private:
    GoldSrcUserCmdLimits limits_;
    GoldSrcUserCmdDeltaCompatibilityProfile profile_{
        GoldSrcUserCmdDeltaCompatibilityProfile::stock_evidence_pending};
};

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcUserCmdDeltaCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcUserCmdDeltaCompatibilityProfile::
        stock_protocol_48_build_10210_usercmd_v1:
        return "stock_protocol_48_build_10210_usercmd_v1";
    case GoldSrcUserCmdDeltaCompatibilityProfile::synthetic_usercmd_delta_v1:
        return "synthetic_usercmd_delta_v1";
    case GoldSrcUserCmdDeltaCompatibilityProfile::stock_evidence_pending:
        return "stock_evidence_pending";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcUserCmdDeltaErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcUserCmdDeltaErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcUserCmdDeltaErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case GoldSrcUserCmdDeltaErrorCode::invalid_binding: return "invalid_binding";
    case GoldSrcUserCmdDeltaErrorCode::incompatible_state_profile:
        return "incompatible_state_profile";
    case GoldSrcUserCmdDeltaErrorCode::invalid_base_policy:
        return "invalid_base_policy";
    case GoldSrcUserCmdDeltaErrorCode::unsupported_weapon_selection:
        return "unsupported_weapon_selection";
    case GoldSrcUserCmdDeltaErrorCode::invalid_input_geometry:
        return "invalid_input_geometry";
    case GoldSrcUserCmdDeltaErrorCode::invalid_decode_context:
        return "invalid_decode_context";
    case GoldSrcUserCmdDeltaErrorCode::numeric_overflow:
        return "numeric_overflow";
    case GoldSrcUserCmdDeltaErrorCode::encoded_limit_exceeded:
        return "encoded_limit_exceeded";
    case GoldSrcUserCmdDeltaErrorCode::writer_failure: return "writer_failure";
    case GoldSrcUserCmdDeltaErrorCode::truncated_mask: return "truncated_mask";
    case GoldSrcUserCmdDeltaErrorCode::mask_byte_count_exceeded:
        return "mask_byte_count_exceeded";
    case GoldSrcUserCmdDeltaErrorCode::noncanonical_mask:
        return "noncanonical_mask";
    case GoldSrcUserCmdDeltaErrorCode::mask_bit_out_of_range:
        return "mask_bit_out_of_range";
    case GoldSrcUserCmdDeltaErrorCode::truncated_value:
        return "truncated_value";
    case GoldSrcUserCmdDeltaErrorCode::nonzero_padding:
        return "nonzero_padding";
    case GoldSrcUserCmdDeltaErrorCode::unexpected_trailing_bits:
        return "unexpected_trailing_bits";
    case GoldSrcUserCmdDeltaErrorCode::state_validation_failed:
        return "state_validation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
