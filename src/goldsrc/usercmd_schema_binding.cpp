#include <hlclient/goldsrc/usercmd_schema_binding.hpp>

#include <hlclient/goldsrc/bit_writer.hpp>

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

constexpr std::string_view kUserCmdSchemaName{"usercmd_t"};
constexpr std::uint8_t kStorageSize = 1U;
constexpr auto kDescriptorOnlyConfidence =
    GoldSrcUserCmdFieldEvidenceConfidence::
        accepted_descriptor_metadata_stock_runtime_pending;
constexpr auto kSyntheticCodecSupport =
    GoldSrcUserCmdFieldCodecSupport::synthetic_only;

constexpr std::array<GoldSrcUserCmdSchemaBindingEntry,
                     kGoldSrcUserCmdSchemaFieldCount>
    kBindingEntries{{
        {0U, "lerp_msec", DeltaFieldBaseType::short_value, false, 9U,
         4'000U, 4'000U, 0U, 0x7bU,
         GoldSrcUserCmdSemanticField::lerp_msec,
         GoldSrcUserCmdControlledEvidenceScenario::timing_lerp_msec,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {1U, "msec", DeltaFieldBaseType::byte_value, false, 8U,
         4'000U, 4'000U, 2U, 0x7fU,
         GoldSrcUserCmdSemanticField::msec,
         GoldSrcUserCmdControlledEvidenceScenario::timing_command_msec,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {2U, "viewangles[1]", DeltaFieldBaseType::angle, false, 16U,
         4'000U, 4'000U, 8U, 0x7fU,
         GoldSrcUserCmdSemanticField::view_yaw,
         GoldSrcUserCmdControlledEvidenceScenario::view_yaw,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {3U, "viewangles[0]", DeltaFieldBaseType::angle, false, 16U,
         4'000U, 4'000U, 4U, 0x7fU,
         GoldSrcUserCmdSemanticField::view_pitch,
         GoldSrcUserCmdControlledEvidenceScenario::view_pitch,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {4U, "buttons", DeltaFieldBaseType::short_value, false, 16U,
         4'000U, 4'000U, 30U, 0x7fU,
         GoldSrcUserCmdSemanticField::buttons,
         GoldSrcUserCmdControlledEvidenceScenario::button_mask,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {5U, "forwardmove", DeltaFieldBaseType::float_value, true, 12U,
         4'000U, 4'000U, 16U, 0x7fU,
         GoldSrcUserCmdSemanticField::forward_move,
         GoldSrcUserCmdControlledEvidenceScenario::movement_forward,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {6U, "lightlevel", DeltaFieldBaseType::byte_value, false, 8U,
         4'000U, 4'000U, 28U, 0x7fU,
         GoldSrcUserCmdSemanticField::light_level,
         GoldSrcUserCmdControlledEvidenceScenario::auxiliary_light_level,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {7U, "sidemove", DeltaFieldBaseType::float_value, true, 12U,
         4'000U, 4'000U, 20U, 0x7fU,
         GoldSrcUserCmdSemanticField::side_move,
         GoldSrcUserCmdControlledEvidenceScenario::movement_side,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {8U, "upmove", DeltaFieldBaseType::float_value, true, 12U,
         4'000U, 4'000U, 24U, 0x7fU,
         GoldSrcUserCmdSemanticField::up_move,
         GoldSrcUserCmdControlledEvidenceScenario::movement_up,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {9U, "impulse", DeltaFieldBaseType::byte_value, false, 8U,
         4'000U, 4'000U, 32U, 0x7fU,
         GoldSrcUserCmdSemanticField::impulse,
         GoldSrcUserCmdControlledEvidenceScenario::auxiliary_impulse,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {10U, "viewangles[2]", DeltaFieldBaseType::angle, false, 16U,
         4'000U, 4'000U, 12U, 0x7fU,
         GoldSrcUserCmdSemanticField::view_roll,
         GoldSrcUserCmdControlledEvidenceScenario::view_roll_policy,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {11U, "impact_index", DeltaFieldBaseType::integer_value, false, 6U,
         4'000U, 4'000U, 36U, 0x7fU,
         GoldSrcUserCmdSemanticField::impact_index,
         GoldSrcUserCmdControlledEvidenceScenario::auxiliary_impact_index,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {12U, "impact_position[0]", DeltaFieldBaseType::float_value, true,
         16U, 32'000U, 4'000U, 40U, 0x7fU,
         GoldSrcUserCmdSemanticField::impact_position_x,
         GoldSrcUserCmdControlledEvidenceScenario::
             auxiliary_impact_position_x,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {13U, "impact_position[1]", DeltaFieldBaseType::float_value, true,
         16U, 32'000U, 4'000U, 44U, 0x7fU,
         GoldSrcUserCmdSemanticField::impact_position_y,
         GoldSrcUserCmdControlledEvidenceScenario::
             auxiliary_impact_position_y,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
        {14U, "impact_position[2]", DeltaFieldBaseType::float_value, true,
         16U, 32'000U, 4'000U, 48U, 0x7fU,
         GoldSrcUserCmdSemanticField::impact_position_z,
         GoldSrcUserCmdControlledEvidenceScenario::
             auxiliary_impact_position_z,
         kDescriptorOnlyConfidence, kSyntheticCodecSupport,
         kSyntheticCodecSupport},
    }};

[[nodiscard]] GoldSrcUserCmdSchemaBindingResult binding_failure(
    const GoldSrcUserCmdSchemaBindingErrorCode code,
    const std::optional<std::size_t> field_index,
    const std::string_view context) noexcept
{
    return {
        std::nullopt,
        GoldSrcUserCmdSchemaBindingError{code, field_index, context},
    };
}

[[nodiscard]] GoldSrcUserCmdSyntheticSchemaRegistryResult registry_failure(
    const GoldSrcUserCmdSchemaBindingErrorCode code,
    const std::optional<std::size_t> field_index,
    const std::string_view context) noexcept
{
    return {
        std::nullopt,
        GoldSrcUserCmdSchemaBindingError{code, field_index, context},
    };
}

[[nodiscard]] bool write_string(
    BitWriter& writer,
    const std::string_view value) noexcept
{
    for (const char character : value) {
        const auto byte = static_cast<std::uint8_t>(
            static_cast<unsigned char>(character));
        if (!writer.write_bits(byte, 8U)) {
            return false;
        }
    }
    return static_cast<bool>(writer.write_bits(0U, 8U));
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

[[nodiscard]] std::optional<std::size_t> synthetic_schema_bit_count() noexcept
{
    std::size_t bits = 8U + (kUserCmdSchemaName.size() + 1U) * 8U + 16U;
    for (const auto& entry : kBindingEntries) {
        std::size_t field_bits = 3U + 8U + 32U +
                                 (entry.exact_name.size() + 1U) * 8U +
                                 8U + 8U + 32U + 32U;
        if (entry.description_offset != 0U) {
            if (!checked_add(field_bits, 16U, field_bits)) {
                return std::nullopt;
            }
        }
        if (!checked_add(bits, field_bits, bits)) {
            return std::nullopt;
        }
    }
    if (!checked_add(bits, (8U - (bits & 7U)) & 7U, bits)) {
        return std::nullopt;
    }
    return bits;
}

[[nodiscard]] bool write_synthetic_schema(BitWriter& writer) noexcept
{
    if (!writer.write_bits(kDeltaDescriptionOpcode, 8U) ||
        !write_string(writer, kUserCmdSchemaName) ||
        !writer.write_bits(
            static_cast<std::uint32_t>(kBindingEntries.size()), 16U)) {
        return false;
    }

    for (const auto& entry : kBindingEntries) {
        const auto type_flags =
            static_cast<std::uint32_t>(entry.base_type) |
            (entry.signed_value ? kDeltaSignedModifier : 0U);
        if (!writer.write_bits(1U, 3U) ||
            !writer.write_bits(entry.description_presence_mask, 8U) ||
            !writer.write_bits(type_flags, 32U) ||
            !write_string(writer, entry.exact_name)) {
            return false;
        }
        if (entry.description_offset != 0U &&
            !writer.write_bits(entry.description_offset, 16U)) {
            return false;
        }
        if (!writer.write_bits(kStorageSize, 8U) ||
            !writer.write_bits(entry.significant_bits, 8U) ||
            !writer.write_bits(entry.premultiply_wire_value, 32U) ||
            !writer.write_bits(entry.postmultiply_wire_value, 32U)) {
            return false;
        }
    }
    return writer.align_to_byte_zero_padding() == BitWriterError::none;
}

} // namespace

std::span<const GoldSrcUserCmdSchemaBindingEntry>
goldsrc_usercmd_schema_binding_entries() noexcept
{
    return kBindingEntries;
}

GoldSrcUserCmdSchemaBinding::GoldSrcUserCmdSchemaBinding(
    const DeltaSchema& schema,
    const GoldSrcUserCmdSchemaBindingProfile profile)
    : schema_{schema}, profile_{profile}
{
}

const DeltaSchema& GoldSrcUserCmdSchemaBinding::schema() const noexcept
{
    return schema_;
}

GoldSrcUserCmdSchemaBindingProfile
GoldSrcUserCmdSchemaBinding::profile() const noexcept
{
    return profile_;
}

std::span<const GoldSrcUserCmdSchemaBindingEntry>
GoldSrcUserCmdSchemaBinding::entries() const noexcept
{
    return kBindingEntries;
}

GoldSrcUserCmdSchemaBindingResult bind_goldsrc_usercmd_schema(
    const DeltaSchemaRegistryState& registry,
    const GoldSrcUserCmdSchemaBindingProfile profile)
{
    if (profile ==
            GoldSrcUserCmdSchemaBindingProfile::
                stock_protocol_48_build_10210_schema_only ||
        profile == GoldSrcUserCmdSchemaBindingProfile::
                       stock_protocol_48_evidence_pending) {
        return binding_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::stock_evidence_pending,
            std::nullopt,
            "Stock usercmd runtime binding remains evidence-pending");
    }
    if (profile != GoldSrcUserCmdSchemaBindingProfile::
                       synthetic_usercmd_schema_v1) {
        return binding_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::invalid_profile,
            std::nullopt,
            "Usercmd schema-binding profile is invalid");
    }

    const auto* schema = registry.find_exact(kUserCmdSchemaName);
    if (schema == nullptr) {
        return binding_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::schema_not_found,
            std::nullopt,
            "The exact usercmd_t schema is absent from the registry");
    }
    if (schema->field_count() != kBindingEntries.size()) {
        return binding_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::field_count_mismatch,
            std::nullopt,
            "The usercmd_t schema does not contain exactly fifteen fields");
    }

    for (std::size_t index = 0U; index < kBindingEntries.size(); ++index) {
        const auto& expected = kBindingEntries[index];
        const auto& actual = schema->fields()[index];
        if (actual.wire_index() != expected.wire_index ||
            actual.name() != expected.exact_name ||
            actual.type_flags().base_type() != expected.base_type ||
            actual.type_flags().signed_value() != expected.signed_value ||
            actual.offset() != expected.description_offset ||
            actual.storage_size() != kStorageSize ||
            actual.significant_bits() != expected.significant_bits ||
            actual.premultiply_wire_value() !=
                expected.premultiply_wire_value ||
            actual.postmultiply_wire_value() !=
                expected.postmultiply_wire_value ||
            actual.presence_mask() != expected.description_presence_mask ||
            expected.encode_support != kSyntheticCodecSupport ||
            expected.decode_support != kSyntheticCodecSupport) {
            return binding_failure(
                GoldSrcUserCmdSchemaBindingErrorCode::
                    field_definition_mismatch,
                index,
                "A usercmd_t field differs from the explicit synthetic binding");
        }
    }

    return {
        GoldSrcUserCmdSchemaBinding{*schema, profile},
        std::nullopt,
    };
}

GoldSrcUserCmdSyntheticSchemaRegistryResult
make_synthetic_usercmd_schema_registry()
{
    const auto bit_count = synthetic_schema_bit_count();
    if (!bit_count || (*bit_count & 7U) != 0U) {
        return registry_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::
                synthetic_schema_build_failed,
            std::nullopt,
            "Synthetic usercmd schema size calculation failed");
    }

    std::vector<std::byte> bytes(*bit_count / 8U, std::byte{0U});
    BitWriter writer{bytes, 0U, *bit_count};
    if (!writer.valid() || !write_synthetic_schema(writer) ||
        writer.written_bits() != *bit_count) {
        return registry_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::
                synthetic_schema_build_failed,
            std::nullopt,
            "Synthetic usercmd schema encoding failed transactionally");
    }

    const auto parsed = DeltaDescriptionParser{}.parse(bytes, 0U, *bit_count);
    if (!parsed || !parsed.schema) {
        return registry_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::
                synthetic_schema_build_failed,
            parsed.error ? parsed.error->field_index : std::nullopt,
            "Synthetic usercmd schema could not be parsed by the schema API");
    }

    DeltaSchemaRegistryBuilder builder;
    const auto inserted = builder.insert(*parsed.schema);
    if (!inserted) {
        return registry_failure(
            GoldSrcUserCmdSchemaBindingErrorCode::registry_build_failed,
            inserted.error ? inserted.error->schema_index : std::nullopt,
            "Synthetic usercmd schema registry rejected its sole schema");
    }
    auto registry = std::move(builder).publish();
    const auto verified = bind_goldsrc_usercmd_schema(registry);
    if (!verified) {
        return registry_failure(
            verified.error->code,
            verified.error->field_index,
            verified.error->context);
    }

    return {std::move(registry), std::nullopt};
}

} // namespace hlclient::goldsrc
