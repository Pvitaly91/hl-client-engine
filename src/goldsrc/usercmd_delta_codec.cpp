#include <hlclient/goldsrc/usercmd_delta_codec.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>
#include <hlclient/goldsrc/bit_writer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hlclient::goldsrc {
namespace {

using RawUserCmdValues =
    std::array<std::uint32_t, kGoldSrcUserCmdSchemaFieldCount>;

[[nodiscard]] bool valid_profile(
    const GoldSrcUserCmdDeltaCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcUserCmdDeltaCompatibilityProfile::
        stock_protocol_48_build_10210_usercmd_v1:
    case GoldSrcUserCmdDeltaCompatibilityProfile::synthetic_usercmd_delta_v1:
    case GoldSrcUserCmdDeltaCompatibilityProfile::stock_evidence_pending:
        return true;
    }
    return false;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] std::uint32_t width_mask(const std::uint8_t width) noexcept
{
    return width == 32U
               ? std::numeric_limits<std::uint32_t>::max()
               : (std::uint32_t{1U} << width) - 1U;
}

[[nodiscard]] bool valid_binding(
    const GoldSrcUserCmdSchemaBinding& binding) noexcept
{
    return binding.profile() ==
               GoldSrcUserCmdSchemaBindingProfile::
                   synthetic_usercmd_schema_v1 &&
           binding.schema().name() == "usercmd_t" &&
           binding.schema().field_count() == kGoldSrcUserCmdSchemaFieldCount &&
           binding.entries().size() == kGoldSrcUserCmdSchemaFieldCount;
}

[[nodiscard]] bool synthetic_state_profile(
    const GoldSrcUserCmdState& state) noexcept
{
    return state.compatibility_profile() ==
               GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1 &&
           state.input_mapping_profile() ==
               GoldSrcUserCmdInputMappingProfile::synthetic_explicit_v1 &&
           state.schema_binding_profile() ==
               GoldSrcUserCmdSchemaBindingProfile::
                   synthetic_usercmd_schema_v1;
}

[[nodiscard]] bool valid_base_policy(
    const GoldSrcUserCmdDeltaBasePolicy policy) noexcept
{
    return policy == GoldSrcUserCmdDeltaBasePolicy::explicit_command ||
           policy == GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state;
}

[[nodiscard]] bool valid_end_policy(
    const GoldSrcUserCmdDeltaEndPolicy policy) noexcept
{
    return policy == GoldSrcUserCmdDeltaEndPolicy::leave_trailing_bits ||
           policy == GoldSrcUserCmdDeltaEndPolicy::require_exact_end;
}

[[nodiscard]] GoldSrcUserCmdDeltaEncodeResult encode_failure(
    const GoldSrcUserCmdDeltaErrorCode code,
    const std::optional<std::size_t> field_index,
    const std::string_view context) noexcept
{
    return {
        std::nullopt,
        GoldSrcUserCmdDeltaError{code, 0U, field_index, context},
    };
}

[[nodiscard]] GoldSrcUserCmdDeltaDecodeResult decode_failure(
    const GoldSrcUserCmdDeltaErrorCode code,
    const std::size_t initial_bit_offset,
    const std::size_t diagnostic_bit_offset,
    const std::optional<std::size_t> field_index,
    const std::string_view context) noexcept
{
    return {
        std::nullopt,
        GoldSrcUserCmdDeltaError{
            code, diagnostic_bit_offset, field_index, context},
        0U,
        initial_bit_offset,
        initial_bit_offset / 8U,
        0U,
        0U,
        {},
    };
}

[[nodiscard]] std::optional<std::uint32_t> quantize_unsigned(
    const std::uint32_t value,
    const std::uint8_t width) noexcept
{
    if (width == 0U || width > 32U ||
        (width < 32U && value > width_mask(width))) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::uint32_t> quantize_signed_scaled(
    const long double value,
    const DeltaFieldDefinition& field) noexcept
{
    const auto width = field.significant_bits();
    const auto premultiply = field.premultiply_wire_value();
    const auto postmultiply = field.postmultiply_wire_value();
    if (!std::isfinite(value) || width == 0U || width > 32U ||
        premultiply == 0U || postmultiply == 0U) {
        return std::nullopt;
    }

    const auto scaled = value * static_cast<long double>(premultiply) /
                        static_cast<long double>(postmultiply);
    if (!std::isfinite(scaled)) {
        return std::nullopt;
    }
    const auto rounded = scaled >= 0.0L ? std::floor(scaled + 0.5L)
                                        : std::ceil(scaled - 0.5L);
    const auto minimum = width == 32U
                             ? static_cast<long double>(
                                   std::numeric_limits<std::int32_t>::min())
                             : -static_cast<long double>(
                                   std::int64_t{1} << (width - 1U));
    const auto maximum = width == 32U
                             ? static_cast<long double>(
                                   std::numeric_limits<std::int32_t>::max())
                             : static_cast<long double>(
                                   (std::int64_t{1} << (width - 1U)) - 1);
    if (rounded < minimum || rounded > maximum) {
        return std::nullopt;
    }

    const auto quantized = static_cast<std::int64_t>(rounded);
    return static_cast<std::uint32_t>(quantized) & width_mask(width);
}

[[nodiscard]] std::optional<std::uint32_t> quantize_angle(
    const long double degrees,
    const std::uint8_t width) noexcept
{
    if (!std::isfinite(degrees) || width == 0U || width > 32U) {
        return std::nullopt;
    }
    auto normalized = std::fmod(degrees, 360.0L);
    if (normalized < 0.0L) {
        normalized += 360.0L;
    }
    const auto modulus = std::uint64_t{1U} << width;
    const auto scaled =
        normalized * static_cast<long double>(modulus) / 360.0L;
    const auto rounded = std::floor(scaled + 0.5L);
    if (!std::isfinite(rounded) || rounded < 0.0L ||
        rounded > static_cast<long double>(modulus)) {
        return std::nullopt;
    }
    auto quantized = static_cast<std::uint64_t>(rounded);
    if (quantized == modulus) {
        quantized = 0U;
    }
    return static_cast<std::uint32_t>(quantized);
}

[[nodiscard]] std::optional<GoldSrcUserCmdDeltaError> quantize_state(
    const GoldSrcUserCmdState& state,
    const GoldSrcUserCmdSchemaBinding& binding,
    RawUserCmdValues& output) noexcept
{
    const auto& schema_fields = binding.schema().fields();
    for (const auto& entry : binding.entries()) {
        if (entry.wire_index >= output.size() ||
            entry.wire_index >= schema_fields.size()) {
            return GoldSrcUserCmdDeltaError{
                GoldSrcUserCmdDeltaErrorCode::invalid_binding,
                0U,
                entry.wire_index,
                "Usercmd binding field index is outside the exact schema"};
        }
        const auto& field = schema_fields[entry.wire_index];
        std::optional<std::uint32_t> raw;
        switch (entry.semantic_field) {
        case GoldSrcUserCmdSemanticField::lerp_msec:
            raw = quantize_unsigned(state.lerp_msec(), field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::msec:
            raw = quantize_unsigned(state.msec(), field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::view_yaw:
            raw = quantize_angle(
                state.view_angles()[1U], field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::view_pitch:
            raw = quantize_angle(
                state.view_angles()[0U], field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::buttons:
            raw = quantize_unsigned(state.buttons(), field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::forward_move:
            raw = quantize_signed_scaled(state.forward_move(), field);
            break;
        case GoldSrcUserCmdSemanticField::light_level:
            raw = quantize_unsigned(
                state.light_level(), field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::side_move:
            raw = quantize_signed_scaled(state.side_move(), field);
            break;
        case GoldSrcUserCmdSemanticField::up_move:
            raw = quantize_signed_scaled(state.up_move(), field);
            break;
        case GoldSrcUserCmdSemanticField::impulse:
            raw = quantize_unsigned(state.impulse(), field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::view_roll:
            raw = quantize_angle(
                state.view_angles()[2U], field.significant_bits());
            break;
        case GoldSrcUserCmdSemanticField::impact_index:
            if (state.impact_index() >= 0) {
                raw = quantize_unsigned(
                    static_cast<std::uint32_t>(state.impact_index()),
                    field.significant_bits());
            }
            break;
        case GoldSrcUserCmdSemanticField::impact_position_x:
            raw = quantize_signed_scaled(state.impact_position()[0U], field);
            break;
        case GoldSrcUserCmdSemanticField::impact_position_y:
            raw = quantize_signed_scaled(state.impact_position()[1U], field);
            break;
        case GoldSrcUserCmdSemanticField::impact_position_z:
            raw = quantize_signed_scaled(state.impact_position()[2U], field);
            break;
        }
        if (!raw) {
            return GoldSrcUserCmdDeltaError{
                GoldSrcUserCmdDeltaErrorCode::numeric_overflow,
                0U,
                entry.wire_index,
                "Usercmd field cannot be represented by its synthetic schema width"};
        }
        output[entry.wire_index] = *raw;
    }
    return std::nullopt;
}

[[nodiscard]] bool all_wire_values_zero(
    const RawUserCmdValues& values) noexcept
{
    return std::ranges::all_of(values, [](const std::uint32_t value) {
        return value == 0U;
    });
}

[[nodiscard]] std::int32_t sign_extend(
    const std::uint32_t raw,
    const std::uint8_t width) noexcept
{
    if (width == 32U) {
        const auto widened = static_cast<std::int64_t>(raw);
        return widened <= std::numeric_limits<std::int32_t>::max()
                   ? static_cast<std::int32_t>(widened)
                   : static_cast<std::int32_t>(
                         widened - (std::int64_t{1} << 32U));
    }
    const auto sign_bit = std::uint32_t{1U} << (width - 1U);
    if ((raw & sign_bit) == 0U) {
        return static_cast<std::int32_t>(raw);
    }
    const auto modulus = std::int64_t{1} << width;
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(raw) - modulus);
}

[[nodiscard]] float decode_signed_scaled(
    const std::uint32_t raw,
    const DeltaFieldDefinition& field) noexcept
{
    const auto quantized = sign_extend(raw, field.significant_bits());
    const auto value = static_cast<long double>(quantized) *
                       static_cast<long double>(
                           field.postmultiply_wire_value()) /
                       static_cast<long double>(
                           field.premultiply_wire_value());
    return static_cast<float>(value);
}

[[nodiscard]] float decode_angle(
    const std::uint32_t raw,
    const std::uint8_t width) noexcept
{
    const auto modulus = std::uint64_t{1U} << width;
    return static_cast<float>(
        static_cast<long double>(raw) * 360.0L /
        static_cast<long double>(modulus));
}

void copy_base_wire_values(
    const GoldSrcUserCmdState& base,
    GoldSrcUserCmdCreateInfo& output) noexcept
{
    output.lerp_msec = base.lerp_msec();
    output.msec = base.msec();
    output.view_angles = base.view_angles();
    output.forward_move = base.forward_move();
    output.side_move = base.side_move();
    output.up_move = base.up_move();
    output.light_level = base.light_level();
    output.buttons = base.buttons();
    output.impulse = base.impulse();
    output.weapon_select = 0U;
    output.impact_index = base.impact_index();
    output.impact_position = base.impact_position();
}

void decode_selected_field(
    const GoldSrcUserCmdSchemaBindingEntry& entry,
    const DeltaFieldDefinition& field,
    const std::uint32_t raw,
    GoldSrcUserCmdCreateInfo& output) noexcept
{
    switch (entry.semantic_field) {
    case GoldSrcUserCmdSemanticField::lerp_msec:
        output.lerp_msec = static_cast<std::uint16_t>(raw);
        break;
    case GoldSrcUserCmdSemanticField::msec:
        output.msec = static_cast<std::uint8_t>(raw);
        break;
    case GoldSrcUserCmdSemanticField::view_yaw:
        output.view_angles[1U] = decode_angle(raw, field.significant_bits());
        break;
    case GoldSrcUserCmdSemanticField::view_pitch:
        output.view_angles[0U] = decode_angle(raw, field.significant_bits());
        break;
    case GoldSrcUserCmdSemanticField::buttons:
        output.buttons = static_cast<std::uint16_t>(raw);
        break;
    case GoldSrcUserCmdSemanticField::forward_move:
        output.forward_move = decode_signed_scaled(raw, field);
        break;
    case GoldSrcUserCmdSemanticField::light_level:
        output.light_level = static_cast<std::uint8_t>(raw);
        break;
    case GoldSrcUserCmdSemanticField::side_move:
        output.side_move = decode_signed_scaled(raw, field);
        break;
    case GoldSrcUserCmdSemanticField::up_move:
        output.up_move = decode_signed_scaled(raw, field);
        break;
    case GoldSrcUserCmdSemanticField::impulse:
        output.impulse = static_cast<std::uint8_t>(raw);
        break;
    case GoldSrcUserCmdSemanticField::view_roll:
        output.view_angles[2U] = decode_angle(raw, field.significant_bits());
        break;
    case GoldSrcUserCmdSemanticField::impact_index:
        output.impact_index = static_cast<std::int32_t>(raw);
        break;
    case GoldSrcUserCmdSemanticField::impact_position_x:
        output.impact_position[0U] = decode_signed_scaled(raw, field);
        break;
    case GoldSrcUserCmdSemanticField::impact_position_y:
        output.impact_position[1U] = decode_signed_scaled(raw, field);
        break;
    case GoldSrcUserCmdSemanticField::impact_position_z:
        output.impact_position[2U] = decode_signed_scaled(raw, field);
        break;
    }
}

} // namespace

GoldSrcUserCmdDeltaCodec::GoldSrcUserCmdDeltaCodec(
    GoldSrcUserCmdLimits limits,
    const GoldSrcUserCmdDeltaCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool GoldSrcUserCmdDeltaCodec::valid_configuration() const noexcept
{
    return valid_goldsrc_usercmd_limits(limits_) && valid_profile(profile_);
}

const GoldSrcUserCmdLimits& GoldSrcUserCmdDeltaCodec::limits() const noexcept
{
    return limits_;
}

GoldSrcUserCmdDeltaCompatibilityProfile
GoldSrcUserCmdDeltaCodec::profile() const noexcept
{
    return profile_;
}

GoldSrcUserCmdDeltaEncodeResult GoldSrcUserCmdDeltaCodec::encode_delta(
    const GoldSrcUserCmdState& base,
    const GoldSrcUserCmdState& current,
    const GoldSrcUserCmdSchemaBinding& binding,
    const GoldSrcUserCmdDeltaEncodeContext& context) const
{
    if (!valid_configuration()) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_configuration,
            std::nullopt,
            "Usercmd delta codec limits or profile are invalid");
    }
    if (profile_ != GoldSrcUserCmdDeltaCompatibilityProfile::
                        synthetic_usercmd_delta_v1) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::stock_evidence_pending,
            std::nullopt,
            "Stock usercmd delta encoding remains evidence-pending");
    }
    if (!valid_binding(binding)) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_binding,
            std::nullopt,
            "Usercmd delta codec requires the exact synthetic binding");
    }
    if (!synthetic_state_profile(base) || !synthetic_state_profile(current)) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::incompatible_state_profile,
            std::nullopt,
            "Usercmd delta inputs do not use the synthetic profile tuple");
    }
    if (!valid_base_policy(context.base_policy)) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_base_policy,
            std::nullopt,
            "Usercmd delta base policy is invalid");
    }
    if (base.weapon_select() != 0U || current.weapon_select() != 0U) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::unsupported_weapon_selection,
            std::nullopt,
            "weapon_select is absent from the exact fifteen-field schema");
    }

    RawUserCmdValues base_values{};
    if (const auto error = quantize_state(base, binding, base_values)) {
        return {std::nullopt, error};
    }
    if (context.base_policy ==
            GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state &&
        !all_wire_values_zero(base_values)) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_base_policy,
            std::nullopt,
            "Synthetic default base must have fifteen zero wire values");
    }

    RawUserCmdValues current_values{};
    if (const auto error = quantize_state(current, binding, current_values)) {
        return {std::nullopt, error};
    }

    std::array<std::uint8_t, kGoldSrcUserCmdDeltaMaskBytes> mask{};
    std::size_t changed_count = 0U;
    std::optional<std::size_t> highest_changed;
    for (std::size_t index = 0U; index < current_values.size(); ++index) {
        if (current_values[index] == base_values[index]) {
            continue;
        }
        mask[index / 8U] = static_cast<std::uint8_t>(
            mask[index / 8U] | (std::uint8_t{1U} << (index & 7U)));
        ++changed_count;
        highest_changed = index;
    }
    const auto mask_byte_count = highest_changed
                                     ? static_cast<std::uint8_t>(
                                           *highest_changed / 8U + 1U)
                                     : std::uint8_t{0U};

    std::size_t meaningful_bits = 8U +
                                  static_cast<std::size_t>(mask_byte_count) *
                                      8U;
    for (std::size_t index = 0U; index < current_values.size(); ++index) {
        if ((mask[index / 8U] &
             (std::uint8_t{1U} << (index & 7U))) == 0U) {
            continue;
        }
        if (!checked_add(
                meaningful_bits,
                binding.schema().fields()[index].significant_bits(),
                meaningful_bits)) {
            return encode_failure(
                GoldSrcUserCmdDeltaErrorCode::encoded_limit_exceeded,
                index,
                "Usercmd delta bit count overflowed");
        }
    }
    const auto padding_bits = (8U - (meaningful_bits & 7U)) & 7U;
    std::size_t encoded_bits = 0U;
    if (!checked_add(meaningful_bits, padding_bits, encoded_bits)) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::encoded_limit_exceeded,
            std::nullopt,
            "Usercmd delta padded bit count overflowed");
    }
    const auto encoded_bytes = encoded_bits / 8U;
    if (encoded_bits > limits_.maximum_encoded_bits ||
        encoded_bytes > limits_.maximum_encoded_bytes) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::encoded_limit_exceeded,
            std::nullopt,
            "Usercmd delta exceeds the configured encoded bound");
    }

    std::vector<std::byte> bytes(encoded_bytes, std::byte{0U});
    BitWriter writer{bytes, 0U, encoded_bits};
    if (!writer.valid() ||
        !writer.write_bits(mask_byte_count, 8U)) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::writer_failure,
            std::nullopt,
            "Usercmd delta writer rejected its preflighted geometry");
    }
    for (std::size_t index = 0U; index < mask_byte_count; ++index) {
        if (!writer.write_bits(mask[index], 8U)) {
            return encode_failure(
                GoldSrcUserCmdDeltaErrorCode::writer_failure,
                std::nullopt,
                "Usercmd delta writer rejected its changed mask");
        }
    }
    for (std::size_t index = 0U; index < current_values.size(); ++index) {
        if ((mask[index / 8U] &
             (std::uint8_t{1U} << (index & 7U))) == 0U) {
            continue;
        }
        if (!writer.write_bits(
                current_values[index],
                binding.schema().fields()[index].significant_bits())) {
            return encode_failure(
                GoldSrcUserCmdDeltaErrorCode::writer_failure,
                index,
                "Usercmd delta writer rejected a preflighted field");
        }
    }
    if (writer.align_to_byte_zero_padding() != BitWriterError::none ||
        writer.written_bits() != encoded_bits ||
        writer.remaining_bits() != 0U) {
        return encode_failure(
            GoldSrcUserCmdDeltaErrorCode::writer_failure,
            std::nullopt,
            "Usercmd delta writer did not reach its exact padded end");
    }

    return {
        GoldSrcUserCmdEncodedDelta{
            std::move(bytes),
            encoded_bits,
            meaningful_bits,
            padding_bits,
            changed_count,
            mask_byte_count,
            mask,
        },
        std::nullopt,
    };
}

GoldSrcUserCmdDeltaDecodeResult GoldSrcUserCmdDeltaCodec::decode_delta(
    const GoldSrcUserCmdState& base,
    const GoldSrcUserCmdSchemaBinding& binding,
    const GoldSrcUserCmdDeltaDecodeContext& context) const
{
    const auto initial_bit_offset = context.start_bit_offset;
    if (!valid_configuration()) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_configuration,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta codec limits or profile are invalid");
    }
    // The gate deliberately precedes binding, state, context and byte
    // inspection so a reserved stock profile cannot drift into synthetic I/O.
    if (profile_ != GoldSrcUserCmdDeltaCompatibilityProfile::
                        synthetic_usercmd_delta_v1) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::stock_evidence_pending,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Stock usercmd delta decoding remains evidence-pending");
    }
    if (!valid_binding(binding)) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_binding,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta codec requires the exact synthetic binding");
    }
    if (!synthetic_state_profile(base)) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::incompatible_state_profile,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta base does not use the synthetic profile tuple");
    }
    if (base.weapon_select() != 0U) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::unsupported_weapon_selection,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "weapon_select is absent from the exact fifteen-field schema");
    }
    if (!valid_base_policy(context.base_policy)) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_base_policy,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta base policy is invalid");
    }
    if (!valid_end_policy(context.end_policy) ||
        !context.command_sequence.valid() ||
        context.command_sequence.value() > limits_.maximum_command_sequence) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_decode_context,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd decode identity or end policy is invalid");
    }

    RawUserCmdValues base_values{};
    if (const auto error = quantize_state(base, binding, base_values)) {
        return decode_failure(
            error->code,
            initial_bit_offset,
            initial_bit_offset,
            error->field_index,
            error->context);
    }
    if (context.base_policy ==
            GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state &&
        !all_wire_values_zero(base_values)) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_base_policy,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Synthetic default base must have fifteen zero wire values");
    }

    if (context.bytes.size() >
        std::numeric_limits<std::size_t>::max() / 8U) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta input bit size overflowed");
    }
    const auto available_bits = context.bytes.size() * 8U;
    if (initial_bit_offset > available_bits ||
        (initial_bit_offset & 7U) != 0U) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Synthetic usercmd delta must start at a valid byte boundary");
    }
    const auto remaining_bits = available_bits - initial_bit_offset;
    const auto selected_bits =
        context.bit_length == static_cast<std::size_t>(-1)
            ? remaining_bits
            : context.bit_length;
    if (selected_bits > remaining_bits ||
        selected_bits > limits_.maximum_encoded_bits ||
        (selected_bits + 7U) / 8U > limits_.maximum_encoded_bytes) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta selected geometry exceeds its owning bounds");
    }

    BitReader reader{context.bytes, initial_bit_offset, selected_bits};
    if (!reader.valid()) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Usercmd delta bit reader rejected the selected geometry");
    }
    const auto mask_count_read = reader.read_bits(8U);
    if (!mask_count_read) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::truncated_mask,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Usercmd delta mask-byte count is truncated");
    }
    if (mask_count_read.value > kGoldSrcUserCmdDeltaMaskBytes) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::mask_byte_count_exceeded,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Usercmd delta mask-byte count exceeds fifteen-field coverage");
    }
    const auto mask_byte_count =
        static_cast<std::uint8_t>(mask_count_read.value);
    std::array<std::uint8_t, kGoldSrcUserCmdDeltaMaskBytes> mask{};
    for (std::size_t index = 0U; index < mask_byte_count; ++index) {
        const auto mask_read = reader.read_bits(8U);
        if (!mask_read) {
            return decode_failure(
                GoldSrcUserCmdDeltaErrorCode::truncated_mask,
                initial_bit_offset,
                reader.bit_offset(),
                std::nullopt,
                "Usercmd delta changed-field mask is truncated");
        }
        mask[index] = static_cast<std::uint8_t>(mask_read.value);
    }
    if (mask_byte_count > 0U && mask[mask_byte_count - 1U] == 0U) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::noncanonical_mask,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Usercmd delta mask uses a non-minimal byte count");
    }
    if ((mask[1U] & 0x80U) != 0U) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::mask_bit_out_of_range,
            initial_bit_offset,
            reader.bit_offset(),
            15U,
            "Usercmd delta mask selects field fifteen outside the schema");
    }

    RawUserCmdValues decoded_values = base_values;
    std::size_t changed_count = 0U;
    for (std::size_t index = 0U; index < decoded_values.size(); ++index) {
        if ((mask[index / 8U] &
             (std::uint8_t{1U} << (index & 7U))) == 0U) {
            continue;
        }
        const auto raw = reader.read_bits(
            binding.schema().fields()[index].significant_bits());
        if (!raw) {
            return decode_failure(
                GoldSrcUserCmdDeltaErrorCode::truncated_value,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Usercmd delta field bits are truncated");
        }
        decoded_values[index] = raw.value;
        ++changed_count;
    }
    const auto padding_error = reader.align_to_byte_zero_padding();
    if (padding_error != BitReaderError::none) {
        return decode_failure(
            padding_error == BitReaderError::nonzero_padding
                ? GoldSrcUserCmdDeltaErrorCode::nonzero_padding
                : GoldSrcUserCmdDeltaErrorCode::truncated_value,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Usercmd delta has truncated or nonzero byte padding");
    }
    if (context.end_policy ==
            GoldSrcUserCmdDeltaEndPolicy::require_exact_end &&
        reader.remaining_bits() != 0U) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::unexpected_trailing_bits,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Usercmd delta leaves bits after its aligned object end");
    }

    auto create_info = goldsrc_usercmd_default_create_info(
        context.command_sequence,
        context.sample_time_nanoseconds);
    copy_base_wire_values(base, create_info);
    create_info.command_sequence = context.command_sequence;
    create_info.source_input_sequence = context.source_input_sequence;
    for (std::size_t index = 0U; index < decoded_values.size(); ++index) {
        if ((mask[index / 8U] &
             (std::uint8_t{1U} << (index & 7U))) == 0U) {
            continue;
        }
        decode_selected_field(
            binding.entries()[index],
            binding.schema().fields()[index],
            decoded_values[index],
            create_info);
    }
    create_info.sample_duration_nanoseconds =
        context.sample_duration_nanoseconds.value_or(
            static_cast<std::uint64_t>(create_info.msec) * 1'000'000U);

    auto created = GoldSrcUserCmdState::create(create_info, limits_);
    if (!created || !created.state) {
        return decode_failure(
            GoldSrcUserCmdDeltaErrorCode::state_validation_failed,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Decoded usercmd fields do not form a valid immutable state");
    }

    const auto bits_consumed = reader.bit_offset() - initial_bit_offset;
    return {
        std::move(created.state),
        std::nullopt,
        bits_consumed,
        reader.bit_offset(),
        reader.bit_offset() / 8U,
        changed_count,
        mask_byte_count,
        mask,
    };
}

} // namespace hlclient::goldsrc
