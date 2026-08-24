#include <hlclient/world_visibility/world_visibility_resolver.hpp>

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace hlclient::world_visibility {
namespace {

constexpr std::uint8_t kPvsCandidateBit = 1U << 0U;
constexpr std::uint8_t kLeafFrustumCandidateBit = 1U << 1U;
constexpr std::uint64_t kSignatureFnvOffsetBasis =
    14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kSignatureFnvPrime = 1'099'511'628'211ULL;

class VisibilitySignatureHasher final {
public:
    void add(const bool value) noexcept
    {
        add(static_cast<std::uint8_t>(value ? 1U : 0U));
    }

    template <typename Integer>
        requires std::is_integral_v<Integer> &&
            (!std::is_same_v<std::remove_cv_t<Integer>, bool>)
    void add(const Integer value) noexcept
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto remaining = static_cast<Unsigned>(value);
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            value_ ^= static_cast<std::uint8_t>(remaining & 0xFFU);
            value_ *= kSignatureFnvPrime;
            remaining >>= 8U;
        }
    }

    template <typename Enumeration>
        requires std::is_enum_v<Enumeration>
    void add(const Enumeration value) noexcept
    {
        add(static_cast<std::underlying_type_t<Enumeration>>(value));
    }

    void add_float(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? kSignatureFnvOffsetBasis : value_;
    }

private:
    std::uint64_t value_{kSignatureFnvOffsetBasis};
};

void hash_vector(
    VisibilitySignatureHasher& hasher,
    const assets::AssetVector3& value) noexcept
{
    hasher.add_float(value.x);
    hasher.add_float(value.y);
    hasher.add_float(value.z);
}

void hash_bounds(
    VisibilitySignatureHasher& hasher,
    const assets::WorldBounds& value) noexcept
{
    hash_vector(hasher, value.minimum);
    hash_vector(hasher, value.maximum);
}

void hash_surface(
    VisibilitySignatureHasher& hasher,
    const WorldVisibleSurfaceInput& surface) noexcept
{
    hasher.add(surface.source_surface_index);
    hasher.add(surface.first_index);
    hasher.add(surface.index_count);
    hasher.add(static_cast<std::uint64_t>(surface.render_material_index));
    hash_bounds(hasher, surface.bounds);
    hasher.add(surface.alpha_mode);
    hasher.add(surface.lightmap_mode);
    hasher.add(surface.lightmap_atlas_page_index.has_value());
    hasher.add(static_cast<std::uint64_t>(
        surface.lightmap_atlas_page_index.value_or(0U)));
}

[[nodiscard]] std::uint64_t visibility_input_signature_impl(
    const WorldVisibilityResolveInput& input) noexcept
{
    VisibilitySignatureHasher hasher;
    hasher.add(static_cast<std::uint32_t>(0x56534947U));
    hasher.add(input.spatial_package != nullptr);
    if (input.spatial_package != nullptr) {
        const auto& package = *input.spatial_package;
        hasher.add(package.compatibility_profile());
        hasher.add(package.evidence_profile());
        const auto& model = package.world_model();
        hasher.add(model.root_node_index);
        hasher.add(model.visible_leaf_count);
        hash_bounds(hasher, model.bounds);
        hasher.add(static_cast<std::uint64_t>(package.planes().size()));
        for (const auto& plane : package.planes()) {
            hash_vector(hasher, plane.normal);
            hasher.add_float(plane.distance);
            hasher.add(plane.source_type.has_value());
            hasher.add(plane.source_type.value_or(0));
        }
        hasher.add(static_cast<std::uint64_t>(package.nodes().size()));
        for (const auto& node : package.nodes()) {
            hasher.add(node.plane_index);
            for (const auto& child : node.children) {
                hasher.add(child.kind);
                hasher.add(child.index);
            }
            hash_bounds(hasher, node.bounds);
            hasher.add(node.first_source_face.has_value());
            hasher.add(node.first_source_face.value_or(0U));
            hasher.add(node.source_face_count.has_value());
            hasher.add(node.source_face_count.value_or(0U));
        }
        hasher.add(static_cast<std::uint64_t>(package.leaves().size()));
        for (const auto& leaf : package.leaves()) {
            hasher.add(leaf.source_leaf_index);
            hasher.add(leaf.contents);
            hash_bounds(hasher, leaf.bounds);
            hasher.add(leaf.pvs_row_index.has_value());
            hasher.add(leaf.pvs_row_index.value_or(0U));
            hasher.add(leaf.surface_membership.source_leaf_index);
            hasher.add(leaf.surface_membership.source_marksurface_count);
            hasher.add(static_cast<std::uint64_t>(
                leaf.surface_membership.world_surface_indices.size()));
            for (const auto surface_index :
                leaf.surface_membership.world_surface_indices) {
                hasher.add(surface_index);
            }
            hasher.add(leaf.pvs_bit_addressable);
            hasher.add(leaf.solid_or_special);
        }
        const auto& pvs = package.pvs_table();
        hasher.add(static_cast<std::uint64_t>(pvs.row_byte_count()));
        hasher.add(pvs.visible_leaf_count());
        hasher.add(static_cast<std::uint64_t>(pvs.unique_row_count()));
        hasher.add(pvs.all_visible_row_index());
        hasher.add(static_cast<std::uint64_t>(
            pvs.leaf_row_indices().size()));
        for (const auto row_index : pvs.leaf_row_indices()) {
            hasher.add(row_index.has_value());
            hasher.add(row_index.value_or(0U));
        }
        for (std::size_t row_index = 0U;
             row_index < pvs.unique_row_count(); ++row_index) {
            const auto row = pvs.row(static_cast<std::uint32_t>(row_index));
            hasher.add(row.has_value());
            if (row) {
                hasher.add(static_cast<std::uint64_t>(row->size()));
                for (const auto value : *row) {
                    hasher.add(std::to_integer<std::uint8_t>(value));
                }
            }
        }
    }
    hasher.add(static_cast<std::uint64_t>(input.world_surfaces.size()));
    for (const auto& surface : input.world_surfaces) {
        hash_surface(hasher, surface);
    }
    hasher.add(static_cast<std::uint64_t>(input.brush_instances.size()));
    for (const auto& instance : input.brush_instances) {
        hasher.add(instance.source_instance_index);
        hash_bounds(hasher, instance.transformed_bounds);
        hasher.add(static_cast<std::uint64_t>(
            instance.touched_leaf_indices.size()));
        for (const auto leaf_index : instance.touched_leaf_indices) {
            hasher.add(leaf_index);
        }
        hasher.add(instance.supported_for_static_opaque_rendering);
    }
    return hasher.value();
}

struct SurfaceLookupEntry {
    std::uint32_t source_index{0U};
    std::size_t input_index{0U};
};

struct PvsSelection {
    bool available{false};
    bool visible_leaf_limit_exceeded{false};
    WorldPvsFallbackReason fallback_reason{WorldPvsFallbackReason::none};
    std::optional<std::uint32_t> camera_leaf_index;
    std::vector<std::uint32_t> leaf_indices;
};

[[nodiscard]] WorldVisibilityResolveResult fail(
    const WorldVisibilityErrorCode code,
    const std::optional<std::size_t> index,
    std::string message)
{
    return {
        std::nullopt,
        WorldVisibilityError{code, index, std::move(message)},
    };
}

[[nodiscard]] bool finite_bounds(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) &&
        renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool valid_mode(const WorldVisibilityMode mode) noexcept
{
    return mode == WorldVisibilityMode::all ||
        mode == WorldVisibilityMode::frustum_only ||
        mode == WorldVisibilityMode::pvs_only ||
        mode == WorldVisibilityMode::pvs_and_frustum;
}

[[nodiscard]] bool valid_fallback(const WorldPvsFallbackPolicy policy) noexcept
{
    return policy == WorldPvsFallbackPolicy::fail_closed ||
        policy == WorldPvsFallbackPolicy::frustum_only ||
        policy == WorldPvsFallbackPolicy::all_surfaces;
}

[[nodiscard]] bool uses_pvs(const WorldVisibilityMode mode) noexcept
{
    return mode == WorldVisibilityMode::pvs_only ||
        mode == WorldVisibilityMode::pvs_and_frustum;
}

[[nodiscard]] bool uses_frustum(const WorldVisibilityMode mode) noexcept
{
    return mode == WorldVisibilityMode::frustum_only ||
        mode == WorldVisibilityMode::pvs_and_frustum;
}

[[nodiscard]] WorldVisibilityMode fallback_mode(
    const WorldVisibilityMode requested,
    const WorldPvsFallbackPolicy policy) noexcept
{
    if (policy == WorldPvsFallbackPolicy::frustum_only) {
        return WorldVisibilityMode::frustum_only;
    }
    if (policy == WorldPvsFallbackPolicy::all_surfaces) {
        return WorldVisibilityMode::all;
    }
    return requested;
}

[[nodiscard]] const SurfaceLookupEntry* find_surface(
    const std::span<const SurfaceLookupEntry> lookup,
    const std::uint32_t source_index) noexcept
{
    const auto found = std::ranges::lower_bound(
        lookup, source_index, {}, &SurfaceLookupEntry::source_index);
    return found == lookup.end() || found->source_index != source_index
        ? nullptr
        : &*found;
}

[[nodiscard]] const world_spatial::WorldSpatialLeaf* find_leaf(
    const world_spatial::WorldSpatialPackage& package,
    const std::uint32_t source_index) noexcept
{
    const auto leaves = package.leaves();
    const auto found = std::ranges::find(
        leaves, source_index, &world_spatial::WorldSpatialLeaf::source_leaf_index);
    return found == leaves.end() ? nullptr : &*found;
}

[[nodiscard]] bool contains_sorted(
    const std::span<const std::uint32_t> values,
    const std::uint32_t value) noexcept
{
    return std::ranges::binary_search(values, value);
}

[[nodiscard]] PvsSelection resolve_pvs(
    const WorldVisibilityResolveInput& input,
    const WorldVisibilityLimits& limits)
{
    PvsSelection selection;
    if (input.spatial_package == nullptr) {
        selection.fallback_reason =
            WorldPvsFallbackReason::visibility_data_absent;
        return selection;
    }
    const auto& package = *input.spatial_package;
    const auto located = world_spatial::WorldSpatialQuery::locate_point(
        package,
        input.camera.position,
        world_spatial::WorldSpatialQueryLimits{
            limits.maximum_spatial_query_steps,
            limits.maximum_visible_leaves,
        });
    if (!located || !located.result) {
        selection.fallback_reason =
            WorldPvsFallbackReason::camera_point_query_failed;
        return selection;
    }

    selection.camera_leaf_index = located.result->leaf_index;
    if (located.result->leaf_index == 0U) {
        selection.fallback_reason =
            WorldPvsFallbackReason::camera_in_leaf_zero;
        return selection;
    }
    if (located.result->solid_or_special) {
        selection.fallback_reason =
            WorldPvsFallbackReason::camera_in_solid_leaf;
        return selection;
    }

    const auto& pvs = package.pvs_table();
    if (pvs.visible_leaf_count() == 0U || pvs.row_byte_count() == 0U) {
        selection.fallback_reason =
            WorldPvsFallbackReason::visibility_data_absent;
        return selection;
    }
    if (!located.result->pvs_available ||
        !pvs.leaf_has_usable_row(located.result->leaf_index)) {
        selection.fallback_reason =
            WorldPvsFallbackReason::pvs_row_unavailable;
        return selection;
    }

    const auto leaves = package.leaves();
    selection.leaf_indices.reserve(
        std::min(leaves.size(), limits.maximum_visible_leaves));
    for (const auto& leaf : leaves) {
        if (leaf.source_leaf_index == 0U || !leaf.pvs_bit_addressable ||
            leaf.solid_or_special) {
            continue;
        }
        const auto visible = pvs.leaf_is_visible_from(
            located.result->leaf_index, leaf.source_leaf_index);
        if (!visible) {
            selection.leaf_indices.clear();
            selection.fallback_reason =
                WorldPvsFallbackReason::pvs_row_unavailable;
            return selection;
        }
        if (*visible) {
            if (selection.leaf_indices.size() >=
                limits.maximum_visible_leaves) {
                selection.leaf_indices.clear();
                selection.visible_leaf_limit_exceeded = true;
                return selection;
            }
            selection.leaf_indices.push_back(leaf.source_leaf_index);
        }
    }
    if (std::ranges::find(selection.leaf_indices,
            located.result->leaf_index) == selection.leaf_indices.end()) {
        if (selection.leaf_indices.size() >= limits.maximum_visible_leaves) {
            selection.leaf_indices.clear();
            selection.visible_leaf_limit_exceeded = true;
            return selection;
        }
        selection.leaf_indices.push_back(located.result->leaf_index);
    }
    std::ranges::sort(selection.leaf_indices);
    selection.leaf_indices.erase(
        std::unique(selection.leaf_indices.begin(), selection.leaf_indices.end()),
        selection.leaf_indices.end());
    if (selection.leaf_indices.size() > limits.maximum_visible_leaves) {
        selection.leaf_indices.clear();
        selection.visible_leaf_limit_exceeded = true;
        return selection;
    }
    selection.available = true;
    return selection;
}

} // namespace

std::uint64_t world_visibility_input_signature(
    const WorldVisibilityResolveInput& input) noexcept
{
    return visibility_input_signature_impl(input);
}

std::string_view to_string(const WorldVisibilityErrorCode code) noexcept
{
    switch (code) {
    case WorldVisibilityErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldVisibilityErrorCode::invalid_camera:
        return "invalid_camera";
    case WorldVisibilityErrorCode::invalid_extent:
        return "invalid_extent";
    case WorldVisibilityErrorCode::invalid_world_surface:
        return "invalid_world_surface";
    case WorldVisibilityErrorCode::duplicate_world_surface:
        return "duplicate_world_surface";
    case WorldVisibilityErrorCode::invalid_brush_instance:
        return "invalid_brush_instance";
    case WorldVisibilityErrorCode::duplicate_brush_instance:
        return "duplicate_brush_instance";
    case WorldVisibilityErrorCode::invalid_touched_leaf:
        return "invalid_touched_leaf";
    case WorldVisibilityErrorCode::invalid_spatial_surface_reference:
        return "invalid_spatial_surface_reference";
    case WorldVisibilityErrorCode::invalid_spatial_package:
        return "invalid_spatial_package";
    case WorldVisibilityErrorCode::invalid_frustum:
        return "invalid_frustum";
    case WorldVisibilityErrorCode::visible_leaf_limit_exceeded:
        return "visible_leaf_limit_exceeded";
    case WorldVisibilityErrorCode::visible_world_surface_limit_exceeded:
        return "visible_world_surface_limit_exceeded";
    case WorldVisibilityErrorCode::visible_brush_instance_limit_exceeded:
        return "visible_brush_instance_limit_exceeded";
    case WorldVisibilityErrorCode::draw_command_limit_exceeded:
        return "draw_command_limit_exceeded";
    case WorldVisibilityErrorCode::surface_dedup_limit_exceeded:
        return "surface_dedup_limit_exceeded";
    case WorldVisibilityErrorCode::unable_to_retain_visibility:
        return "unable_to_retain_visibility";
    }
    return "unknown";
}

WorldVisibilityResolveResult WorldVisibilityResolver::resolve(
    const WorldVisibilityResolveInput& input,
    const WorldVisibilityLimits& limits) const
{
    try {
        if (!valid_mode(input.mode) || !valid_fallback(input.pvs_fallback_policy) ||
            input.revision == 0U || limits.maximum_visible_leaves == 0U ||
            limits.maximum_visible_world_surfaces == 0U ||
            limits.maximum_visible_brush_instances == 0U ||
            limits.maximum_draw_commands == 0U ||
            limits.maximum_surface_dedup_bytes == 0U ||
            limits.maximum_spatial_query_steps == 0U ||
            limits.maximum_spatial_query_steps >
                world_spatial::kWorldSpatialHardMaximumQuerySteps ||
            limits.maximum_visible_leaves >
                world_spatial::kWorldSpatialHardMaximumBoxQueryLeaves) {
            return fail(WorldVisibilityErrorCode::invalid_configuration,
                std::nullopt,
                "Visibility mode, revision or operation limits are invalid");
        }
        if (input.scene_identity.visibility_input_signature != 0U &&
            input.scene_identity.visibility_input_signature !=
                world_visibility_input_signature(input)) {
            return fail(WorldVisibilityErrorCode::invalid_configuration,
                std::nullopt,
                "Visibility adapters do not match the bound scene identity");
        }
        if (!renderer::is_valid(input.camera)) {
            return fail(WorldVisibilityErrorCode::invalid_camera,
                std::nullopt,
                "Visibility camera is non-finite or geometrically invalid");
        }
        if (input.world_surfaces.size() >
            limits.maximum_visible_world_surfaces) {
            return fail(
                WorldVisibilityErrorCode::visible_world_surface_limit_exceeded,
                input.world_surfaces.size(),
                "World surface input exceeds the configured visibility limit");
        }
        if (input.brush_instances.size() >
            limits.maximum_visible_brush_instances) {
            return fail(
                WorldVisibilityErrorCode::visible_brush_instance_limit_exceeded,
                input.brush_instances.size(),
                "Brush instance input exceeds the configured visibility limit");
        }
        if (input.world_surfaces.size() >
            limits.maximum_surface_dedup_bytes / sizeof(std::uint8_t)) {
            return fail(WorldVisibilityErrorCode::surface_dedup_limit_exceeded,
                input.world_surfaces.size(),
                "Surface deduplication state exceeds the configured byte limit");
        }

        std::vector<SurfaceLookupEntry> surface_lookup;
        surface_lookup.reserve(input.world_surfaces.size());
        for (std::size_t index = 0U; index < input.world_surfaces.size(); ++index) {
            const auto& surface = input.world_surfaces[index];
            if (!finite_bounds(surface.bounds)) {
                return fail(WorldVisibilityErrorCode::invalid_world_surface,
                    index,
                    "World visibility surface has invalid bounds");
            }
            surface_lookup.push_back({surface.source_surface_index, index});
        }
        std::ranges::sort(
            surface_lookup, {}, &SurfaceLookupEntry::source_index);
        if (std::adjacent_find(surface_lookup.begin(), surface_lookup.end(),
                [](const auto& left, const auto& right) {
                    return left.source_index == right.source_index;
                }) != surface_lookup.end()) {
            return fail(WorldVisibilityErrorCode::duplicate_world_surface,
                std::nullopt,
                "World visibility surface ordinals must be unique");
        }

        std::vector<std::uint32_t> brush_ordinals;
        brush_ordinals.reserve(input.brush_instances.size());
        for (std::size_t index = 0U; index < input.brush_instances.size(); ++index) {
            const auto& instance = input.brush_instances[index];
            if (!finite_bounds(instance.transformed_bounds) ||
                instance.touched_leaf_indices.size() >
                    limits.maximum_visible_leaves) {
                return fail(WorldVisibilityErrorCode::invalid_brush_instance,
                    index,
                    "Brush visibility input has invalid bounds or leaf cardinality");
            }
            std::vector<std::uint32_t> touched(
                instance.touched_leaf_indices.begin(),
                instance.touched_leaf_indices.end());
            std::ranges::sort(touched);
            if (std::adjacent_find(touched.begin(), touched.end()) != touched.end()) {
                return fail(WorldVisibilityErrorCode::invalid_touched_leaf,
                    index,
                    "Brush touched-leaf membership must be deduplicated");
            }
            brush_ordinals.push_back(instance.source_instance_index);
        }
        std::ranges::sort(brush_ordinals);
        if (std::adjacent_find(brush_ordinals.begin(), brush_ordinals.end()) !=
            brush_ordinals.end()) {
            return fail(WorldVisibilityErrorCode::duplicate_brush_instance,
                std::nullopt,
                "Brush instance ordinals must be unique");
        }

        if (input.spatial_package != nullptr) {
            std::vector<std::uint32_t> leaf_ordinals;
            leaf_ordinals.reserve(input.spatial_package->leaves().size());
            std::size_t leaf_position = 0U;
            for (const auto& leaf : input.spatial_package->leaves()) {
                if (!finite_bounds(leaf.bounds) ||
                    leaf.source_leaf_index != leaf_position ||
                    leaf.surface_membership.source_leaf_index !=
                        leaf.source_leaf_index) {
                    return fail(WorldVisibilityErrorCode::invalid_spatial_package,
                        leaf.source_leaf_index,
                        "Spatial leaf bounds or membership identity is invalid");
                }
                leaf_ordinals.push_back(leaf.source_leaf_index);
                ++leaf_position;
            }
            std::ranges::sort(leaf_ordinals);
            if (std::adjacent_find(leaf_ordinals.begin(), leaf_ordinals.end()) !=
                leaf_ordinals.end()) {
                return fail(WorldVisibilityErrorCode::invalid_spatial_package,
                    std::nullopt,
                    "Spatial leaf source ordinals must be unique");
            }
            for (std::size_t index = 0U; index < input.brush_instances.size(); ++index) {
                for (const auto leaf_index :
                    input.brush_instances[index].touched_leaf_indices) {
                    if (!std::ranges::binary_search(leaf_ordinals, leaf_index)) {
                        return fail(WorldVisibilityErrorCode::invalid_touched_leaf,
                            index,
                            "Brush instance references an unavailable spatial leaf");
                    }
                }
            }
        }

        PvsSelection pvs;
        auto applied_mode = input.mode;
        auto fallback_reason = WorldPvsFallbackReason::none;
        if (uses_pvs(input.mode)) {
            pvs = resolve_pvs(input, limits);
            if (pvs.visible_leaf_limit_exceeded) {
                return fail(WorldVisibilityErrorCode::visible_leaf_limit_exceeded,
                    limits.maximum_visible_leaves,
                    "PVS visible leaf set exceeds the configured limit");
            }
            if (!pvs.available) {
                fallback_reason = pvs.fallback_reason;
                applied_mode = fallback_mode(
                    input.mode, input.pvs_fallback_policy);
            }
        }

        if (uses_pvs(input.mode) && !pvs.available &&
            input.pvs_fallback_policy == WorldPvsFallbackPolicy::fail_closed) {
            WorldVisibilityStatistics statistics;
            statistics.total_world_surface_count = input.world_surfaces.size();
            statistics.world_surface_culled_by_pvs_count =
                input.world_surfaces.size();
            statistics.total_brush_instance_count = input.brush_instances.size();
            statistics.supported_brush_instance_count =
                static_cast<std::size_t>(std::ranges::count_if(
                    input.brush_instances,
                    [&input](const WorldVisibilityBrushInstanceInput& instance) {
                        return input.brush_instances_enabled &&
                            instance.supported_for_static_opaque_rendering;
                    }));
            statistics.brush_instance_culled_by_pvs_count =
                statistics.supported_brush_instance_count;
            return {
                WorldVisibilitySet{
                    input.mode,
                    applied_mode,
                    fallback_reason,
                    pvs.camera_leaf_index,
                    {},
                    {},
                    {},
                    statistics,
                    input.revision,
                    input.scene_identity,
                },
                std::nullopt,
            };
        }

        std::optional<WorldViewFrustum> frustum;
        if (uses_frustum(applied_mode)) {
            if (input.extent.width <= 0 || input.extent.height <= 0) {
                return fail(WorldVisibilityErrorCode::invalid_extent,
                    std::nullopt,
                    "Frustum visibility requires a positive render extent");
            }
            auto created = WorldViewFrustum::from_camera(input.camera, input.extent);
            if (!created || !created.frustum) {
                return fail(WorldVisibilityErrorCode::invalid_frustum,
                    std::nullopt,
                    "Unable to build a normalized camera frustum");
            }
            frustum = std::move(*created.frustum);
        }

        std::vector<std::uint8_t> surface_state(
            input.world_surfaces.size(), std::uint8_t{0U});
        std::vector<std::uint32_t> visible_leaves;
        std::vector<std::uint32_t> pvs_visible_leaves;
        if (uses_pvs(applied_mode)) {
            pvs_visible_leaves = pvs.leaf_indices;
        }

        auto mark_leaf_surfaces = [&](
            const world_spatial::WorldSpatialLeaf& leaf,
            const std::uint8_t flag) -> std::optional<WorldVisibilityError> {
            for (const auto source_surface :
                leaf.surface_membership.world_surface_indices) {
                const auto* found = find_surface(surface_lookup, source_surface);
                if (found == nullptr) {
                    return WorldVisibilityError{
                        WorldVisibilityErrorCode::invalid_spatial_surface_reference,
                        source_surface,
                        "Spatial leaf references an unavailable world render surface",
                    };
                }
                surface_state[found->input_index] |= flag;
            }
            return std::nullopt;
        };

        if (applied_mode == WorldVisibilityMode::all) {
            if (input.spatial_package != nullptr) {
                for (const auto& leaf : input.spatial_package->leaves()) {
                    if (leaf.source_leaf_index != 0U && !leaf.solid_or_special) {
                        visible_leaves.push_back(leaf.source_leaf_index);
                    }
                }
            }
            std::ranges::fill(surface_state,
                static_cast<std::uint8_t>(
                    kPvsCandidateBit | kLeafFrustumCandidateBit));
        } else if (applied_mode == WorldVisibilityMode::frustum_only) {
            if (input.spatial_package != nullptr) {
                for (const auto& leaf : input.spatial_package->leaves()) {
                    if (leaf.source_leaf_index == 0U || leaf.solid_or_special) {
                        continue;
                    }
                    const auto classification = frustum->classify(leaf.bounds);
                    if (!classification || !classification.classification) {
                        return fail(WorldVisibilityErrorCode::invalid_spatial_package,
                            leaf.source_leaf_index,
                            "Spatial leaf bounds cannot be classified by the frustum");
                    }
                    if (*classification.classification !=
                        WorldBoundsClassification::outside) {
                        visible_leaves.push_back(leaf.source_leaf_index);
                    }
                }
            }
            std::ranges::fill(surface_state, kPvsCandidateBit);
        } else {
            if (input.spatial_package == nullptr) {
                return fail(WorldVisibilityErrorCode::invalid_spatial_package,
                    std::nullopt,
                    "Applied PVS mode requires a spatial package");
            }
            for (const auto leaf_index : pvs_visible_leaves) {
                const auto* leaf = find_leaf(*input.spatial_package, leaf_index);
                if (leaf == nullptr) {
                    return fail(WorldVisibilityErrorCode::invalid_spatial_package,
                        leaf_index,
                        "PVS selected an unavailable spatial leaf");
                }
                if (const auto marking_error =
                        mark_leaf_surfaces(*leaf, kPvsCandidateBit)) {
                    return {std::nullopt, *marking_error};
                }
                bool leaf_passes = true;
                if (uses_frustum(applied_mode)) {
                    const auto classification = frustum->classify(leaf->bounds);
                    if (!classification || !classification.classification) {
                        return fail(WorldVisibilityErrorCode::invalid_spatial_package,
                            leaf_index,
                            "PVS leaf bounds cannot be classified by the frustum");
                    }
                    leaf_passes = *classification.classification !=
                        WorldBoundsClassification::outside;
                }
                if (leaf_passes) {
                    visible_leaves.push_back(leaf_index);
                    if (const auto marking_error =
                            mark_leaf_surfaces(*leaf, kLeafFrustumCandidateBit)) {
                        return {std::nullopt, *marking_error};
                    }
                }
            }
        }

        std::ranges::sort(visible_leaves);
        visible_leaves.erase(
            std::unique(visible_leaves.begin(), visible_leaves.end()),
            visible_leaves.end());
        if (visible_leaves.size() > limits.maximum_visible_leaves) {
            return fail(WorldVisibilityErrorCode::visible_leaf_limit_exceeded,
                visible_leaves.size(),
                "Visible leaf set exceeds the configured limit");
        }

        WorldVisibilityStatistics statistics;
        statistics.total_world_surface_count = input.world_surfaces.size();
        statistics.total_brush_instance_count = input.brush_instances.size();
        for (const auto state : surface_state) {
            if ((state & kPvsCandidateBit) != 0U) {
                ++statistics.pvs_candidate_world_surface_count;
            }
        }
        statistics.world_surface_culled_by_pvs_count =
            statistics.total_world_surface_count -
            statistics.pvs_candidate_world_surface_count;

        std::vector<std::uint32_t> visible_world_surfaces;
        visible_world_surfaces.reserve(std::min(input.world_surfaces.size(),
            limits.maximum_visible_world_surfaces));
        for (const auto& lookup : surface_lookup) {
            const auto& surface = input.world_surfaces[lookup.input_index];
            const auto state = surface_state[lookup.input_index];
            bool selected = false;
            if (applied_mode == WorldVisibilityMode::all ||
                applied_mode == WorldVisibilityMode::pvs_only) {
                selected = (state & kPvsCandidateBit) != 0U;
            } else if (applied_mode == WorldVisibilityMode::frustum_only) {
                const auto classification = frustum->classify(surface.bounds);
                if (!classification || !classification.classification) {
                    return fail(WorldVisibilityErrorCode::invalid_world_surface,
                        lookup.source_index,
                        "World surface bounds cannot be classified by the frustum");
                }
                selected = *classification.classification !=
                    WorldBoundsClassification::outside;
            } else {
                if ((state & kLeafFrustumCandidateBit) != 0U) {
                    const auto classification = frustum->classify(surface.bounds);
                    if (!classification || !classification.classification) {
                        return fail(WorldVisibilityErrorCode::invalid_world_surface,
                            lookup.source_index,
                            "PVS surface bounds cannot be classified by the frustum");
                    }
                    selected = *classification.classification !=
                        WorldBoundsClassification::outside;
                }
            }
            if (selected) {
                if (visible_world_surfaces.size() >=
                    limits.maximum_visible_world_surfaces) {
                    return fail(
                        WorldVisibilityErrorCode::
                            visible_world_surface_limit_exceeded,
                        lookup.source_index,
                        "Visible world surface set exceeds the configured limit");
                }
                visible_world_surfaces.push_back(lookup.source_index);
            }
        }
        statistics.visible_world_surface_count = visible_world_surfaces.size();
        statistics.frustum_visible_world_surface_count =
            visible_world_surfaces.size();
        statistics.world_surface_culled_by_frustum_count =
            statistics.pvs_candidate_world_surface_count -
            statistics.visible_world_surface_count;

        std::vector<std::uint32_t> visible_brush_instances;
        visible_brush_instances.reserve(std::min(input.brush_instances.size(),
            limits.maximum_visible_brush_instances));
        for (const auto& instance : input.brush_instances) {
            if (!input.brush_instances_enabled ||
                !instance.supported_for_static_opaque_rendering) {
                continue;
            }
            ++statistics.supported_brush_instance_count;
            bool pvs_visible = true;
            if (uses_pvs(applied_mode)) {
                pvs_visible = std::ranges::any_of(
                    instance.touched_leaf_indices,
                    [&](const std::uint32_t leaf_index) {
                        return leaf_index != 0U &&
                            contains_sorted(pvs_visible_leaves, leaf_index);
                    });
            }
            if (!pvs_visible) {
                continue;
            }
            ++statistics.pvs_visible_brush_instance_count;

            bool frustum_visible = true;
            if (uses_frustum(applied_mode)) {
                const auto classification =
                    frustum->classify(instance.transformed_bounds);
                if (!classification || !classification.classification) {
                    return fail(WorldVisibilityErrorCode::invalid_brush_instance,
                        instance.source_instance_index,
                        "Brush instance bounds cannot be classified by the frustum");
                }
                frustum_visible = *classification.classification !=
                    WorldBoundsClassification::outside;
            }
            if (!frustum_visible) {
                continue;
            }
            ++statistics.frustum_visible_brush_instance_count;
            if (visible_brush_instances.size() >=
                limits.maximum_visible_brush_instances) {
                return fail(
                    WorldVisibilityErrorCode::visible_brush_instance_limit_exceeded,
                    instance.source_instance_index,
                    "Visible brush instance set exceeds the configured limit");
            }
            visible_brush_instances.push_back(instance.source_instance_index);
        }
        std::ranges::sort(visible_brush_instances);
        statistics.visible_brush_instance_count = visible_brush_instances.size();
        statistics.brush_instance_culled_by_pvs_count =
            statistics.supported_brush_instance_count -
            statistics.pvs_visible_brush_instance_count;
        statistics.brush_instance_culled_by_frustum_count =
            statistics.pvs_visible_brush_instance_count -
            statistics.visible_brush_instance_count;

        if (visible_world_surfaces.size() > limits.maximum_draw_commands ||
            visible_brush_instances.size() >
                limits.maximum_draw_commands -
                    std::min(visible_world_surfaces.size(),
                        limits.maximum_draw_commands)) {
            return fail(WorldVisibilityErrorCode::draw_command_limit_exceeded,
                std::nullopt,
                "Minimum visible object command count exceeds the configured limit");
        }

        return {
            WorldVisibilitySet{
                input.mode,
                applied_mode,
                fallback_reason,
                pvs.camera_leaf_index,
                std::move(visible_leaves),
                std::move(visible_world_surfaces),
                std::move(visible_brush_instances),
                statistics,
                input.revision,
                input.scene_identity,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(WorldVisibilityErrorCode::unable_to_retain_visibility,
            std::nullopt,
            "Unable to retain bounded visibility state");
    } catch (const std::length_error&) {
        return fail(WorldVisibilityErrorCode::unable_to_retain_visibility,
            std::nullopt,
            "Visibility container length is invalid");
    }
}

} // namespace hlclient::world_visibility
