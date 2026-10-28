#include "prepared_world_visibility.hpp"

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::renderer::opengl {
namespace {

using assets::AssetVector3;
using assets::WorldBounds;
using world_spatial::WorldSpatialNodeChildKind;
using world_visibility::WorldViewFrustum;

[[nodiscard]] bool finite(AssetVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const WorldBounds& bounds) noexcept
{
    return finite(bounds.minimum) && finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

void include(WorldBounds& target, const WorldBounds& value) noexcept
{
    target.minimum.x = std::min(target.minimum.x, value.minimum.x);
    target.minimum.y = std::min(target.minimum.y, value.minimum.y);
    target.minimum.z = std::min(target.minimum.z, value.minimum.z);
    target.maximum.x = std::max(target.maximum.x, value.maximum.x);
    target.maximum.y = std::max(target.maximum.y, value.maximum.y);
    target.maximum.z = std::max(target.maximum.z, value.maximum.z);
}

[[nodiscard]] double plane_distance(
    const world_spatial::WorldSpatialPlane& plane, AssetVector3 point) noexcept
{
    return static_cast<double>(plane.normal.x) * point.x +
        static_cast<double>(plane.normal.y) * point.y +
        static_cast<double>(plane.normal.z) * point.z - plane.distance;
}

// Matches the accepted prepared-world selector's conservative roundoff margin.
// A cleared plane must contain the FULL subtree geometry, not BSP leaf bounds.
[[nodiscard]] bool clip_bounds(const WorldViewFrustum& frustum,
    const WorldBounds& bounds, std::uint8_t& mask) noexcept
{
    const auto planes = frustum.planes();
    for (std::size_t index = 0U; index < planes.size(); ++index) {
        const auto bit = static_cast<std::uint8_t>(1U << index);
        if ((mask & bit) == 0U) continue;
        const auto& plane = planes[index];
        const double px = plane.normal.x >= 0.0F ? bounds.maximum.x : bounds.minimum.x;
        const double py = plane.normal.y >= 0.0F ? bounds.maximum.y : bounds.minimum.y;
        const double pz = plane.normal.z >= 0.0F ? bounds.maximum.z : bounds.minimum.z;
        const double nx = plane.normal.x >= 0.0F ? bounds.minimum.x : bounds.maximum.x;
        const double ny = plane.normal.y >= 0.0F ? bounds.minimum.y : bounds.maximum.y;
        const double nz = plane.normal.z >= 0.0F ? bounds.minimum.z : bounds.maximum.z;
        const auto distance = [&](double x, double y, double z) noexcept {
            return plane.normal.x * x + plane.normal.y * y + plane.normal.z * z +
                static_cast<double>(plane.signed_offset);
        };
        const double magnitude = std::abs(plane.normal.x * px) +
            std::abs(plane.normal.y * py) + std::abs(plane.normal.z * pz) +
            std::abs(static_cast<double>(plane.signed_offset));
        const double tolerance = 1.0 + 64.0 *
            std::numeric_limits<float>::epsilon() * magnitude;
        if (distance(px, py, pz) < -tolerance) return false;
        if (distance(nx, ny, nz) >= tolerance)
            mask = static_cast<std::uint8_t>(mask & ~bit);
    }
    return true;
}

// A camera-centred sphere enclosing the near-plane rectangle. This derives
// the conservative boundary test from the actual frustum, not an Xash cvar
// or an assumed GoldSrc 16-unit water probe.
[[nodiscard]] double near_volume_radius(
    const RenderCamera& camera, const WorldViewFrustum& frustum) noexcept
{
    const double dx = static_cast<double>(camera.target.x) - camera.position.x;
    const double dy = static_cast<double>(camera.target.y) - camera.position.y;
    const double dz = static_cast<double>(camera.target.z) - camera.position.z;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const auto angle_squared = [&](std::size_t first) noexcept {
        double sine = 0.0;
        for (std::size_t i = first; i < first + 2U; ++i) {
            const auto& normal = frustum.planes()[i].normal;
            sine = std::max(sine, std::abs((normal.x * dx + normal.y * dy + normal.z * dz) / length));
        }
        if (sine >= 1.0) return std::numeric_limits<double>::infinity();
        return sine * sine / (1.0 - sine * sine);
    };
    return camera.near_plane * std::sqrt(1.0 + angle_squared(0U) + angle_squared(2U));
}

} // namespace

std::string_view to_string(PreparedWorldPvsFallback value) noexcept
{
    switch (value) {
    case PreparedWorldPvsFallback::none: return "none";
    case PreparedWorldPvsFallback::policy_disabled: return "policy_disabled";
    case PreparedWorldPvsFallback::invalid_or_absent_pvs: return "invalid_or_absent_pvs";
    case PreparedWorldPvsFallback::row_unavailable: return "row_unavailable";
    case PreparedWorldPvsFallback::solid_or_special_leaf: return "solid_or_special_leaf";
    case PreparedWorldPvsFallback::liquid_or_content_boundary: return "liquid_or_content_boundary";
    }
    return "unknown";
}

bool PreparedWorldVisibility::prepare(
    const world_scene_render::WorldSceneRenderPackage& scene) noexcept
{
    const auto preparations = statistics_.preparation_count + 1U;
    PreparedWorldVisibility candidate;
    bool valid = false;
    try {
        valid = candidate.prepare_impl(scene);
    } catch (const std::bad_alloc&) {
        candidate.status_ = PreparedWorldVisibilityStatus::allocation_failed;
    } catch (const std::length_error&) {
        candidate.status_ = PreparedWorldVisibilityStatus::allocation_failed;
    }
    if (!valid) {
        const auto failure = candidate.status_;
        candidate = PreparedWorldVisibility{};
        candidate.status_ = failure;
    }
    candidate.statistics_.preparation_count = preparations;
    *this = std::move(candidate);
    return valid;
}

bool PreparedWorldVisibility::prepare_impl(
    const world_scene_render::WorldSceneRenderPackage& scene)
{
    status_ = PreparedWorldVisibilityStatus::invalid_spatial;
    const auto& spatial = scene.spatial_package();
    const auto nodes = spatial.nodes();
    const auto leaves = spatial.leaves();
    const auto planes = spatial.planes();
    const auto root = spatial.world_model().root_node_index;
    if (nodes.empty() || leaves.empty() || planes.empty() || root >= nodes.size() ||
        nodes.size() > world_spatial::kWorldSpatialHardMaximumQueryNodes ||
        leaves.size() > world_spatial::kWorldSpatialHardMaximumQueryPackageLeaves ||
        planes.size() > world_spatial::kWorldSpatialHardMaximumQueryPlanes) return false;
    nodes_.resize(nodes.size());
    leaf_parent_.assign(leaves.size(), invalid_index);
    plane_lengths_.reserve(planes.size());
    for (const auto& plane : planes) {
        if (!finite(plane.normal) || !std::isfinite(plane.distance)) return false;
        const double length = std::sqrt(static_cast<double>(plane.normal.x) * plane.normal.x +
            static_cast<double>(plane.normal.y) * plane.normal.y +
            static_cast<double>(plane.normal.z) * plane.normal.z);
        if (!(length > 0.0)) return false;
        plane_lengths_.push_back(length);
    }
    for (std::size_t i = 0U; i < leaves.size(); ++i) {
        if (leaves[i].source_leaf_index != i ||
            leaves[i].surface_membership.source_leaf_index != i ||
            !valid_bounds(leaves[i].bounds)) return false;
    }
    // Parent-before-child order both validates the rooted tree and supports
    // a reverse-order subtree bounds reduction. Leaf zero may be shared.
    std::vector<std::uint32_t> order;
    order.reserve(nodes.size());
    std::vector<std::uint8_t> reached(nodes.size(), 0U);
    reached[root] = 1U;
    order.push_back(root);
    for (std::size_t cursor = 0U; cursor < order.size(); ++cursor) {
        const auto index = order[cursor];
        const auto& node = nodes[index];
        if (node.plane_index >= planes.size() || !valid_bounds(node.bounds)) return false;
        for (const auto child : node.children) {
            if (child.kind == WorldSpatialNodeChildKind::node) {
                if (child.index >= nodes.size() || reached[child.index] != 0U) return false;
                reached[child.index] = 1U;
                nodes_[child.index].parent = index;
                nodes_[child.index].depth = nodes_[index].depth + 1U;
                order.push_back(child.index);
            } else if (child.kind == WorldSpatialNodeChildKind::leaf) {
                if (child.index >= leaves.size()) return false;
                if (child.index != 0U && leaf_parent_[child.index] != invalid_index) return false;
                leaf_parent_[child.index] = index;
            } else return false;
        }
    }

    status_ = PreparedWorldVisibilityStatus::invalid_geometry;
    if (!scene.world_package()) return false;
    const auto& package = *scene.world_package();
    const auto ranges = package.surface_ranges();
    const auto indices = package.indices();
    const auto vertices = package.vertices();
    if (ranges.empty() || ranges.size() > 65'535U ||
        scene.world_surfaces().size() != ranges.size()) return false;
    surfaces_.resize(ranges.size());
    always_surfaces_.reserve(ranges.size());
    for (std::size_t i = 0U; i < ranges.size(); ++i) {
        const auto& range = ranges[i];
        const std::size_t first = range.first_index;
        const std::size_t count = range.index_count;
        if (range.source_world_surface_index != i || count == 0U || count % 3U != 0U ||
            first > indices.size() || count > indices.size() - first ||
            range.render_material_index >= package.materials().size()) return false;
        auto& surface = surfaces_[i];
        surface.sky = package.materials()[range.render_material_index].sky_surface;
        if (surface.sky) always_surfaces_.push_back(static_cast<std::uint32_t>(i));
        for (std::size_t j = first; j < first + count; ++j) {
            if (indices[j] >= vertices.size() || !finite(vertices[indices[j]].position)) return false;
            const auto position = vertices[indices[j]].position;
            if (j == first) surface.bounds = {position, position};
            else include(surface.bounds, {position, position});
        }
        const auto a = vertices[indices[first]].position;
        const auto b = vertices[indices[first + 1U]].position;
        const auto c = vertices[indices[first + 2U]].position;
        const std::array<double, 3U> ab{static_cast<double>(b.x) - a.x,
            static_cast<double>(b.y) - a.y, static_cast<double>(b.z) - a.z};
        const std::array<double, 3U> ac{static_cast<double>(c.x) - a.x,
            static_cast<double>(c.y) - a.y, static_cast<double>(c.z) - a.z};
        surface.normal = {ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
        const double length = std::sqrt(surface.normal[0] * surface.normal[0] +
            surface.normal[1] * surface.normal[1] + surface.normal[2] * surface.normal[2]);
        if (length > 0.0 && std::isfinite(length)) {
            for (auto& value : surface.normal) value /= length;
            surface.plane_offset = surface.normal[0] * a.x + surface.normal[1] * a.y + surface.normal[2] * a.z;
            surface.plane_valid = true;
            for (std::size_t j = first; j < first + count; j += 3U) {
                const auto p = vertices[indices[j]].position;
                const auto q = vertices[indices[j + 1U]].position;
                const auto r = vertices[indices[j + 2U]].position;
                const double ux = static_cast<double>(q.x) - p.x, uy = static_cast<double>(q.y) - p.y, uz = static_cast<double>(q.z) - p.z;
                const double vx = static_cast<double>(r.x) - p.x, vy = static_cast<double>(r.y) - p.y, vz = static_cast<double>(r.z) - p.z;
                std::array<double, 3U> triangle_normal{
                    uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
                const double triangle_length = std::sqrt(triangle_normal[0] * triangle_normal[0] +
                    triangle_normal[1] * triangle_normal[1] + triangle_normal[2] * triangle_normal[2]);
                if (!(triangle_length > 0.0) || !std::isfinite(triangle_length)) {
                    surface.plane_valid = false;
                    continue;
                }
                for (auto& value : triangle_normal) value /= triangle_length;
                const double triangle_offset = triangle_normal[0] * p.x +
                    triangle_normal[1] * p.y + triangle_normal[2] * p.z;
                // Approximate coplanarity is NOT sufficient: an arbitrarily
                // distant eye can lie on opposite sides of slightly different
                // triangle planes. Only exactly agreeing geometric planes use
                // one CPU face test; uncertain/nonplanar faces stay with GL.
                if (triangle_normal != surface.normal || triangle_offset != surface.plane_offset)
                    surface.plane_valid = false;
            }
        }
    }
    // LCA ownership prevents a polygon crossing one referring leaf's bounds
    // from being discarded with that leaf. Full indexed geometry bounds are
    // propagated through all owners and ancestors below.
    for (std::size_t leaf_index = 0U; leaf_index < leaves.size(); ++leaf_index) {
        const auto parent = leaf_parent_[leaf_index];
        for (const auto surface_index : leaves[leaf_index].surface_membership.world_surface_indices) {
            if (surface_index >= surfaces_.size()) return false;
            auto& surface = surfaces_[surface_index];
            if (leaf_index == 0U || parent == invalid_index || leaves[leaf_index].solid_or_special ||
                !leaves[leaf_index].pvs_bit_addressable ||
                leaf_index>spatial.pvs_table().visible_leaf_count()) {
                surface.unlinked = true;
                continue;
            }
            surface.owner = surface.owner == invalid_index ? parent : common_ancestor(surface.owner, parent);
        }
    }
    for (auto& surface : surfaces_) {
        if (surface.owner == invalid_index || surface.unlinked) {
            surface.owner = root;
            surface.unlinked = true;
        }
        auto& node = nodes_[surface.owner];
        ++node.surface_count;
        // Existing finite sky polygons also contribute depth. The historical
        // PVS negative control loses those pixels; retain all sky outside this
        // pruning tree and leave its existing GL clipping/culling unchanged.
        if (surface.sky) continue;
        if (node.has_geometry) include(node.render_bounds, surface.bounds);
        else { node.render_bounds = surface.bounds; node.has_geometry = true; }
    }
    std::uint32_t next_surface = 0U;
    for (auto& node : nodes_) {
        node.first_surface = next_surface;
        next_surface += node.surface_count;
    }
    owned_surfaces_.resize(surfaces_.size());
    std::vector<std::uint32_t> written(nodes_.size(), 0U);
    for (std::uint32_t i = 0U; i < surfaces_.size(); ++i) {
        const auto owner = surfaces_[i].owner;
        owned_surfaces_[nodes_[owner].first_surface + written[owner]++] = i;
    }
    for (auto iterator = order.rbegin(); iterator != order.rend(); ++iterator) {
        const auto& node = nodes_[*iterator];
        if (node.parent != invalid_index && node.has_geometry) {
            auto& parent = nodes_[node.parent];
            if (parent.has_geometry) include(parent.render_bounds, node.render_bounds);
            else { parent.render_bounds = node.render_bounds; parent.has_geometry = true; }
        }
    }

    const auto& pvs = spatial.pvs_table();
    const auto rows = pvs.leaf_row_indices();
    pvs_valid_ = pvs.visible_leaf_count() > 0U &&
        pvs.visible_leaf_count() < leaves.size() && rows.size() == leaves.size() &&
        pvs.visible_leaf_count() == spatial.world_model().visible_leaf_count &&
        pvs.row_byte_count() == (static_cast<std::size_t>(pvs.visible_leaf_count()) + 7U) / 8U &&
        pvs.unique_row_count() > 0U;
    if (pvs_valid_) {
        for (std::uint32_t i = 0U; i < pvs.unique_row_count(); ++i) {
            const auto row = pvs.row(i);
            if (!row || row->size() != pvs.row_byte_count()) pvs_valid_ = false;
        }
        for (const auto row : rows) {
            if (row && *row >= pvs.unique_row_count()) pvs_valid_ = false;
        }
    }
    pvs_nodes_.resize(nodes_.size());
    pvs_surfaces_.resize(surfaces_.size());
    selected_.resize(surfaces_.size());
    stack_.resize(nodes_.size());
    query_stack_.resize(nodes_.size() + 1U);
    const auto account = [&](const auto& values) {
        statistics_.prepared_bytes += values.capacity() * sizeof(values[0]);
        statistics_.storage_allocation_count += values.capacity() > 0U ? 1U : 0U;
    };
    account(nodes_); account(surfaces_); account(leaf_parent_); account(owned_surfaces_);
    account(always_surfaces_);
    account(plane_lengths_); account(pvs_nodes_); account(pvs_surfaces_); account(selected_);
    account(stack_); account(query_stack_);
    scene_ = &scene;
    status_ = PreparedWorldVisibilityStatus::ready;
    return true;
}

std::uint32_t PreparedWorldVisibility::common_ancestor(
    std::uint32_t left, std::uint32_t right) const noexcept
{
    while (nodes_[left].depth > nodes_[right].depth) left = nodes_[left].parent;
    while (nodes_[right].depth > nodes_[left].depth) right = nodes_[right].parent;
    while (left != right) { left = nodes_[left].parent; right = nodes_[right].parent; }
    return left;
}

std::uint32_t PreparedWorldVisibility::locate_leaf(AssetVector3 point) noexcept
{
    const auto& spatial = scene_->spatial_package();
    auto index = spatial.world_model().root_node_index;
    for (;;) {
        ++statistics_.query_nodes;
        const auto& node = spatial.nodes()[index];
        const auto child = node.children[plane_distance(spatial.planes()[node.plane_index], point) >= 0.0 ? 0U : 1U];
        if (child.kind == WorldSpatialNodeChildKind::leaf) return child.index;
        index = child.index;
    }
}

bool PreparedWorldVisibility::content_boundary(
    AssetVector3 point, double radius, std::uint32_t camera_leaf) noexcept
{
    if (!std::isfinite(radius)) return true;
    const auto& spatial = scene_->spatial_package();
    std::size_t count = 0U;
    query_stack_[count++] = spatial.world_model().root_node_index;
    while (count != 0U) {
        const auto& node = spatial.nodes()[query_stack_[--count]];
        ++statistics_.query_nodes;
        const double distance = plane_distance(spatial.planes()[node.plane_index], point);
        const double reach = radius * plane_lengths_[node.plane_index];
        for (std::size_t side = 0U; side < 2U; ++side) {
            if ((side == 0U && distance < -reach) || (side == 1U && distance > reach)) continue;
            const auto child = node.children[side];
            if (child.kind == WorldSpatialNodeChildKind::leaf) {
                // Even empty/empty leaves can have different PVS. A near
                // clip volume spanning them must not trust only the eye row.
                if (child.index != camera_leaf) return true;
            } else query_stack_[count++] = child.index;
        }
    }
    return false;
}

void PreparedWorldVisibility::mark_node_ancestors(std::uint32_t node) noexcept
{
    while (node != invalid_index && pvs_nodes_[node] == 0U) {
        pvs_nodes_[node] = 1U;
        node = nodes_[node].parent;
    }
}

void PreparedWorldVisibility::rebuild_pvs(
    std::uint32_t leaf, PreparedWorldPvsFallback fallback) noexcept
{
    std::fill(pvs_nodes_.begin(), pvs_nodes_.end(), 0U);
    std::fill(pvs_surfaces_.begin(), pvs_surfaces_.end(), 0U);
    if (fallback != PreparedWorldPvsFallback::none) {
        std::fill(pvs_nodes_.begin(), pvs_nodes_.end(), 1U);
        std::fill(pvs_surfaces_.begin(), pvs_surfaces_.end(), 1U);
    } else {
        const auto& spatial = scene_->spatial_package();
        const auto row = *spatial.pvs_table().row_for_leaf(leaf);
        const auto leaves = spatial.leaves();
        for (std::size_t i = 1U; i < leaves.size(); ++i) {
            const auto bit = i - 1U;
            const bool visible = i == leaf || (bit < spatial.pvs_table().visible_leaf_count() &&
                (std::to_integer<std::uint8_t>(row[bit / 8U]) & (1U << (bit % 8U))) != 0U);
            if (!visible || leaves[i].solid_or_special) continue;
            for (const auto surface : leaves[i].surface_membership.world_surface_indices)
                pvs_surfaces_[surface] = 1U;
            if (leaf_parent_[i] != invalid_index) mark_node_ancestors(leaf_parent_[i]);
        }
        for (std::size_t i = 0U; i < surfaces_.size(); ++i) {
            if (surfaces_[i].unlinked || surfaces_[i].sky) pvs_surfaces_[i] = 1U;
            if (pvs_surfaces_[i] != 0U) mark_node_ancestors(surfaces_[i].owner);
        }
    }
    context_leaf_ = leaf;
    context_fallback_ = fallback;
    context_candidate_count_ = 0U;
    for (const auto selected : pvs_surfaces_) context_candidate_count_ += selected;
    ++statistics_.pvs_rebuilds;
}

bool PreparedWorldVisibility::select(const RenderCamera& camera,
    const WorldViewFrustum& frustum, bool cull_back, bool use_pvs,
    bool cull_retained_sky) noexcept
{
    if (!ready() || !renderer::is_valid(camera)) return false;
    statistics_.visited_nodes = 0U; statistics_.query_nodes = 0U;
    statistics_.pvs_rejected_nodes = 0U; statistics_.frustum_rejected_nodes = 0U;
    statistics_.considered_surfaces = 0U; statistics_.selected_surfaces = 0U;
    statistics_.tested_bounds = 0U; statistics_.backface_rejected_surfaces = 0U;
    statistics_.pvs_candidate_surfaces = 0U;
    const auto leaf_index = locate_leaf(camera.position);
    statistics_.camera_leaf = leaf_index;
    const auto& spatial = scene_->spatial_package();
    const auto& leaf = spatial.leaves()[leaf_index];
    auto fallback = PreparedWorldPvsFallback::none;
    if (!use_pvs) fallback = PreparedWorldPvsFallback::policy_disabled;
    else if (leaf_index == 0U || leaf.solid_or_special) fallback = PreparedWorldPvsFallback::solid_or_special_leaf;
    else if (!pvs_valid_) fallback = PreparedWorldPvsFallback::invalid_or_absent_pvs;
    else if (leaf_index > spatial.pvs_table().visible_leaf_count() ||
        !leaf.pvs_bit_addressable || !spatial.pvs_table().leaf_has_usable_row(leaf_index))
        fallback = PreparedWorldPvsFallback::row_unavailable;
    else if (leaf.contents != -1 || content_boundary(camera.position, near_volume_radius(camera, frustum), leaf_index))
        fallback = PreparedWorldPvsFallback::liquid_or_content_boundary;
    statistics_.fallback = fallback;
    if (!context_leaf_ || *context_leaf_ != leaf_index || context_fallback_ != fallback)
        rebuild_pvs(leaf_index, fallback);
    else ++statistics_.cache_hits;
    statistics_.pvs_candidate_surfaces = context_candidate_count_;
    std::fill(selected_.begin(), selected_.end(), 0U);
    for (const auto index : always_surfaces_) {
        ++statistics_.considered_surfaces;
        // Finite sky still ignores eye-only PVS and CPU backface pruning.
        // The opt-in control only rejects a complete indexed-vertex AABB
        // outside the final camera's conservative frustum. Keep candidates
        // unchanged so camera/policy changes reuse the same PVS index stream.
        if(cull_retained_sky) {
            auto mask=std::uint8_t{0x3fU};
            ++statistics_.tested_bounds;
            if(!clip_bounds(frustum,surfaces_[index].bounds,mask)) continue;
        }
        selected_[index] = 1U;
        ++statistics_.selected_surfaces;
    }
    std::size_t stack_size = 0U;
    stack_[stack_size++] = {spatial.world_model().root_node_index, 0x3fU};
    while (stack_size != 0U) {
        auto entry = stack_[--stack_size];
        ++statistics_.visited_nodes;
        const auto& node = nodes_[entry.node];
        if (!node.has_geometry) continue;
        if (pvs_nodes_[entry.node] == 0U) { ++statistics_.pvs_rejected_nodes; continue; }
        ++statistics_.tested_bounds;
        if (!clip_bounds(frustum, node.render_bounds, entry.plane_mask)) {
            ++statistics_.frustum_rejected_nodes;
            continue;
        }
        for (std::size_t i = node.first_surface; i < static_cast<std::size_t>(node.first_surface) + node.surface_count; ++i) {
            const auto index = owned_surfaces_[i];
            if (surfaces_[index].sky) continue;
            if (pvs_surfaces_[index] == 0U) continue;
            ++statistics_.considered_surfaces;
            const auto& surface = surfaces_[index];
            if (cull_back && surface.plane_valid) {
                const double x = surface.normal[0] * camera.position.x;
                const double y = surface.normal[1] * camera.position.y;
                const double z = surface.normal[2] * camera.position.z;
                const double tolerance = 1.0 + 64.0 * std::numeric_limits<float>::epsilon() *
                    (std::abs(x) + std::abs(y) + std::abs(z) + std::abs(surface.plane_offset));
                if (x + y + z - surface.plane_offset < -tolerance) {
                    ++statistics_.backface_rejected_surfaces;
                    continue;
                }
            }
            auto mask = entry.plane_mask;
            if (mask != 0U) {
                ++statistics_.tested_bounds;
                if (!clip_bounds(frustum, surface.bounds, mask)) continue;
            }
            selected_[index] = 1U;
            ++statistics_.selected_surfaces;
        }
        for (const auto child : spatial.nodes()[entry.node].children) {
            if (child.kind == WorldSpatialNodeChildKind::node)
                stack_[stack_size++] = {child.index, entry.plane_mask};
        }
    }
    return true;
}

void PreparedWorldVisibility::reset() noexcept { *this = PreparedWorldVisibility{}; }
bool PreparedWorldVisibility::ready() const noexcept { return status_ == PreparedWorldVisibilityStatus::ready; }
PreparedWorldVisibilityStatus PreparedWorldVisibility::status() const noexcept { return status_; }
std::span<const std::uint8_t> PreparedWorldVisibility::selected_surfaces() const noexcept { return selected_; }
std::span<const std::uint8_t> PreparedWorldVisibility::pvs_surfaces() const noexcept { return pvs_surfaces_; }
const PreparedWorldVisibilityStatistics& PreparedWorldVisibility::statistics() const noexcept { return statistics_; }

} // namespace hlclient::renderer::opengl
