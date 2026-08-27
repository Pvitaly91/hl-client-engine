#pragma once

#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::goldsrc {

enum class GoldSrcUserCmdSemanticField : std::uint8_t {
    lerp_msec,
    msec,
    view_yaw,
    view_pitch,
    buttons,
    forward_move,
    light_level,
    side_move,
    up_move,
    impulse,
    view_roll,
    impact_index,
    impact_position_x,
    impact_position_y,
    impact_position_z,
};

// Names the controlled stock experiment that must establish the runtime
// meaning of one descriptor row. These are research classifications, not
// claims that the corresponding stock experiment has been accepted.
enum class GoldSrcUserCmdControlledEvidenceScenario : std::uint8_t {
    timing_lerp_msec,
    timing_command_msec,
    view_yaw,
    view_pitch,
    button_mask,
    movement_forward,
    auxiliary_light_level,
    movement_side,
    movement_up,
    auxiliary_impulse,
    view_roll_policy,
    auxiliary_impact_index,
    auxiliary_impact_position_x,
    auxiliary_impact_position_y,
    auxiliary_impact_position_z,
};

enum class GoldSrcUserCmdFieldEvidenceConfidence : std::uint8_t {
    accepted_descriptor_metadata_stock_runtime_pending,
};

enum class GoldSrcUserCmdFieldCodecSupport : std::uint8_t {
    unsupported,
    synthetic_only,
};

// One explicit metadata table drives both schema validation and the optional
// synthetic-registry factory. Description offsets are retained only to
// reproduce the captured schema descriptor; neither the binding nor codec uses
// them as C/C++ object offsets.
struct GoldSrcUserCmdSchemaBindingEntry {
    std::size_t wire_index{0U};
    std::string_view exact_name;
    DeltaFieldBaseType base_type{DeltaFieldBaseType::byte_value};
    bool signed_value{false};
    std::uint8_t significant_bits{0U};
    std::uint32_t premultiply_wire_value{0U};
    std::uint32_t postmultiply_wire_value{0U};
    std::uint16_t description_offset{0U};
    std::uint8_t description_presence_mask{0U};
    GoldSrcUserCmdSemanticField semantic_field{
        GoldSrcUserCmdSemanticField::lerp_msec};
    GoldSrcUserCmdControlledEvidenceScenario controlled_evidence_scenario{
        GoldSrcUserCmdControlledEvidenceScenario::timing_lerp_msec};
    GoldSrcUserCmdFieldEvidenceConfidence evidence_confidence{
        GoldSrcUserCmdFieldEvidenceConfidence::
            accepted_descriptor_metadata_stock_runtime_pending};
    GoldSrcUserCmdFieldCodecSupport encode_support{
        GoldSrcUserCmdFieldCodecSupport::unsupported};
    GoldSrcUserCmdFieldCodecSupport decode_support{
        GoldSrcUserCmdFieldCodecSupport::unsupported};
};

[[nodiscard]] std::span<const GoldSrcUserCmdSchemaBindingEntry>
goldsrc_usercmd_schema_binding_entries() noexcept;

enum class GoldSrcUserCmdSchemaBindingErrorCode : std::uint8_t {
    invalid_profile,
    stock_evidence_pending,
    schema_not_found,
    field_count_mismatch,
    field_definition_mismatch,
    synthetic_schema_build_failed,
    registry_build_failed,
};

struct GoldSrcUserCmdSchemaBindingError {
    GoldSrcUserCmdSchemaBindingErrorCode code{
        GoldSrcUserCmdSchemaBindingErrorCode::invalid_profile};
    std::optional<std::size_t> field_index;
    std::string_view context;
};

class GoldSrcUserCmdSchemaBinding final {
public:
    GoldSrcUserCmdSchemaBinding(const GoldSrcUserCmdSchemaBinding&) = default;
    GoldSrcUserCmdSchemaBinding(GoldSrcUserCmdSchemaBinding&&) noexcept = default;
    GoldSrcUserCmdSchemaBinding& operator=(
        const GoldSrcUserCmdSchemaBinding&) = delete;
    GoldSrcUserCmdSchemaBinding& operator=(
        GoldSrcUserCmdSchemaBinding&&) = delete;
    ~GoldSrcUserCmdSchemaBinding() = default;

    [[nodiscard]] const DeltaSchema& schema() const noexcept;
    [[nodiscard]] GoldSrcUserCmdSchemaBindingProfile profile() const noexcept;
    [[nodiscard]] std::span<const GoldSrcUserCmdSchemaBindingEntry> entries()
        const noexcept;

private:
    friend struct GoldSrcUserCmdSchemaBindingResult;
    friend GoldSrcUserCmdSchemaBindingResult bind_goldsrc_usercmd_schema(
        const DeltaSchemaRegistryState&,
        GoldSrcUserCmdSchemaBindingProfile);

    GoldSrcUserCmdSchemaBinding(
        const DeltaSchema& schema,
        GoldSrcUserCmdSchemaBindingProfile profile);

    DeltaSchema schema_;
    GoldSrcUserCmdSchemaBindingProfile profile_{
        GoldSrcUserCmdSchemaBindingProfile::synthetic_usercmd_schema_v1};
};

struct GoldSrcUserCmdSchemaBindingResult {
    std::optional<GoldSrcUserCmdSchemaBinding> binding;
    std::optional<GoldSrcUserCmdSchemaBindingError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return binding.has_value();
    }
};

[[nodiscard]] GoldSrcUserCmdSchemaBindingResult bind_goldsrc_usercmd_schema(
    const DeltaSchemaRegistryState& registry,
    GoldSrcUserCmdSchemaBindingProfile profile =
        GoldSrcUserCmdSchemaBindingProfile::synthetic_usercmd_schema_v1);

struct GoldSrcUserCmdSyntheticSchemaRegistryResult {
    std::optional<DeltaSchemaRegistryState> registry;
    std::optional<GoldSrcUserCmdSchemaBindingError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return registry.has_value();
    }
};

// Creates descriptor metadata for deterministic tools and fake peers only.
// It does not enable the stock runtime delta or client-move profiles.
[[nodiscard]] GoldSrcUserCmdSyntheticSchemaRegistryResult
make_synthetic_usercmd_schema_registry();

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcUserCmdSchemaBindingErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcUserCmdSchemaBindingErrorCode::invalid_profile:
        return "invalid_profile";
    case GoldSrcUserCmdSchemaBindingErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case GoldSrcUserCmdSchemaBindingErrorCode::schema_not_found:
        return "schema_not_found";
    case GoldSrcUserCmdSchemaBindingErrorCode::field_count_mismatch:
        return "field_count_mismatch";
    case GoldSrcUserCmdSchemaBindingErrorCode::field_definition_mismatch:
        return "field_definition_mismatch";
    case GoldSrcUserCmdSchemaBindingErrorCode::synthetic_schema_build_failed:
        return "synthetic_schema_build_failed";
    case GoldSrcUserCmdSchemaBindingErrorCode::registry_build_failed:
        return "registry_build_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
