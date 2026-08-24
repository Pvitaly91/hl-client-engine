#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::brush_models {

inline constexpr std::size_t kDefaultMaximumBrushSubmodelInstances = 4'096U;
inline constexpr std::size_t kHardMaximumBrushSubmodelInstances = 8'192U;
inline constexpr std::size_t kDefaultMaximumBrushTouchedLeafLinks = 262'144U;
inline constexpr std::size_t kHardMaximumBrushTouchedLeafLinks = 4'194'304U;
inline constexpr std::size_t kHardMaximumBrushSourceModels = 4'096U;

struct BrushSubmodelInstanceBuildLimits {
    std::size_t maximum_instances{kDefaultMaximumBrushSubmodelInstances};
    std::size_t maximum_touched_leaf_links{
        kDefaultMaximumBrushTouchedLeafLinks};
    std::size_t maximum_source_models{kHardMaximumBrushSourceModels};
    world_spatial::WorldSpatialQueryLimits spatial_query_limits{};
};

[[nodiscard]] bool valid_brush_submodel_instance_build_limits(
    const BrushSubmodelInstanceBuildLimits& limits) noexcept;

// Ordered canonical brush-model metadata. Geometry presence is separate from
// the record itself so a referenced but unavailable model becomes a typed
// instance outcome instead of disappearing.
struct BrushSubmodelModelMetadata {
    std::uint32_t source_model_index{0U};
    assets::AssetVector3 source_model_origin{};
    assets::WorldBounds local_bounds{};
    bool geometry_present{false};
};

struct BrushSubmodelInstance {
    std::size_t source_entity_ordinal{0U};
    std::optional<std::uint32_t> source_model_index;
    GoldSrcBrushClassnameCategory classname_category{
        GoldSrcBrushClassnameCategory::absent};
    std::optional<BrushSubmodelTransform> transform;
    std::optional<assets::WorldBounds> transformed_bounds;
    // Leaf zero and solid/special leaves are deliberately excluded. Ordering
    // is the deterministic front-before-back order from WorldSpatialQuery.
    std::vector<std::uint32_t> touched_world_leaves;
    BrushSubmodelInstanceStatus status{
        BrushSubmodelInstanceStatus::invalid_entity_metadata};
    std::optional<world_spatial::WorldSpatialQueryErrorCode> spatial_query_error;

    [[nodiscard]] bool renderable() const noexcept
    {
        return status == BrushSubmodelInstanceStatus::supported_static_opaque;
    }
};

struct BrushSubmodelInstanceStatistics {
    std::uint64_t source_entity_count{0U};
    std::uint64_t brush_candidate_count{0U};
    std::uint64_t supported_static_opaque_count{0U};
    std::uint64_t unsupported_transform_count{0U};
    std::uint64_t unsupported_rendermode_count{0U};
    std::uint64_t invalid_model_reference_count{0U};
    std::uint64_t missing_model_geometry_count{0U};
    std::uint64_t invalid_entity_metadata_count{0U};
    std::uint64_t outside_world_spatial_tree_count{0U};
    std::uint64_t no_visible_leaf_membership_count{0U};
    std::uint64_t touched_leaf_link_count{0U};
};

class BrushSubmodelInstanceSet final {
public:
    BrushSubmodelInstanceSet(
        std::vector<BrushSubmodelInstance> instances,
        BrushSubmodelInstanceStatistics statistics) noexcept;

    [[nodiscard]] std::span<const BrushSubmodelInstance> instances() const noexcept;
    [[nodiscard]] const BrushSubmodelInstanceStatistics& statistics() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::vector<BrushSubmodelInstance> instances_;
    BrushSubmodelInstanceStatistics statistics_{};
};

enum class BrushSubmodelInstanceBuildErrorCode {
    invalid_configuration,
    invalid_source_model_count,
    invalid_model_metadata,
    instance_limit_exceeded,
    touched_leaf_limit_exceeded,
    unable_to_retain_instances,
};

[[nodiscard]] std::string_view to_string(
    BrushSubmodelInstanceBuildErrorCode code) noexcept;

struct BrushSubmodelInstanceBuildError {
    BrushSubmodelInstanceBuildErrorCode code{
        BrushSubmodelInstanceBuildErrorCode::invalid_configuration};
    std::optional<std::size_t> source_entity_ordinal;
    std::optional<std::uint32_t> source_model_index;
};

struct BrushSubmodelInstanceBuildResult {
    std::optional<BrushSubmodelInstanceSet> instance_set;
    std::optional<BrushSubmodelInstanceBuildError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return instance_set.has_value();
    }
};

class BrushSubmodelInstanceSetBuilder final {
public:
    // source_model_count includes world model zero. Model metadata must be
    // strictly increasing by source_model_index; gaps are allowed and produce
    // missing_model_geometry for references into those gaps.
    [[nodiscard]] static BrushSubmodelInstanceBuildResult build(
        const bsp::GoldSrcEntityDocument& entity_document,
        std::span<const BrushSubmodelModelMetadata> ordered_models,
        std::size_t source_model_count,
        const world_spatial::WorldSpatialPackage& spatial_package,
        const BrushSubmodelInstanceBuildLimits& limits = {});
};

} // namespace hlclient::goldsrc::brush_models
