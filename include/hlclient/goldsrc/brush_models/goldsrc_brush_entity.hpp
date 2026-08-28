#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_transform.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::brush_models {

enum class BrushSubmodelInstanceStatus {
    supported_static_opaque,
    unsupported_transform,
    unsupported_rendermode,
    invalid_model_reference,
    missing_model_geometry,
    invalid_entity_metadata,
    outside_world_spatial_tree,
    no_visible_leaf_membership,
};

[[nodiscard]] std::string_view to_string(
    BrushSubmodelInstanceStatus status) noexcept;

enum class GoldSrcBrushModelReferenceErrorCode {
    not_brush_reference,
    world_model_reference,
    invalid_syntax,
    index_overflow,
    index_out_of_range,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcBrushModelReferenceErrorCode code) noexcept;

struct GoldSrcBrushModelReferenceResult {
    std::optional<std::uint32_t> source_model_index;
    std::optional<GoldSrcBrushModelReferenceErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return source_model_index.has_value();
    }
};

// Accepts exactly `*<positive-decimal-index>` with no sign, suffix or
// whitespace. `*0` is the world model and never an instance.
[[nodiscard]] GoldSrcBrushModelReferenceResult parse_brush_model_reference(
    std::string_view value,
    std::size_t source_model_count) noexcept;

// Backward-compatible aliases keep brush-model callers source-compatible while
// the parser implementation lives in the renderer-neutral BSP entity layer.
using GoldSrcEntityNumberErrorCode = bsp::GoldSrcEntityNumberErrorCode;
using GoldSrcEntityVectorResult = bsp::GoldSrcEntityVectorResult;
using GoldSrcEntityAnglesSource = bsp::GoldSrcEntityAnglesSource;
using GoldSrcEntityAnglesResult = bsp::GoldSrcEntityAnglesResult;
using bsp::parse_entity_angles;
using bsp::parse_entity_vector3;
using bsp::to_string;

enum class GoldSrcBrushClassnameCategory {
    absent,
    function_entity,
    other,
};

struct GoldSrcBrushEntityMetadata {
    std::size_t source_entity_ordinal{0U};
    std::optional<std::uint32_t> source_model_index;
    GoldSrcBrushClassnameCategory classname_category{
        GoldSrcBrushClassnameCategory::absent};
    assets::AssetVector3 origin{};
    assets::AssetVector3 angles_degrees{};
    GoldSrcEntityAnglesSource angles_source{
        GoldSrcEntityAnglesSource::default_zero};
    std::int32_t rendermode{0};
    std::optional<float> render_amount;
    BrushSubmodelInstanceStatus status{
        BrushSubmodelInstanceStatus::invalid_entity_metadata};
};

struct GoldSrcBrushEntityInterpretation {
    // No value means the entity has no brush-model reference (for example a
    // point entity or a studio-model reference) and is outside M4.4 scope.
    std::optional<GoldSrcBrushEntityMetadata> metadata;
};

// Interprets only classname/model/origin/angles/angle/rendermode/renderamt.
// Every other ordered pair remains inert in the source document. Ambiguous
// interpreted keys never use a last-value-wins policy.
[[nodiscard]] GoldSrcBrushEntityInterpretation interpret_brush_entity(
    const bsp::GoldSrcEntityRecord& entity,
    std::size_t source_entity_ordinal,
    std::size_t source_model_count) noexcept;

} // namespace hlclient::goldsrc::brush_models
