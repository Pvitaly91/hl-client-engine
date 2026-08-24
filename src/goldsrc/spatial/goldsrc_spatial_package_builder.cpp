#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::spatial {
namespace {

using world_spatial::WorldSpatialNodeChildKind;

[[nodiscard]] GoldSrcSpatialBuildResult failure(
    const GoldSrcSpatialImportErrorCode code,
    const std::optional<std::size_t> source_element_index = std::nullopt,
    const std::optional<GoldSrcPvsDecodeError> pvs_error = std::nullopt) noexcept
{
    return GoldSrcSpatialBuildResult{
        std::nullopt,
        GoldSrcSpatialImportError{code, source_element_index, pvs_error},
    };
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool supported_contents(const std::int32_t contents) noexcept
{
    return contents >= -15 && contents <= -1;
}

[[nodiscard]] std::optional<world_spatial::WorldSpatialNodeChild> decode_child(
    const std::int32_t encoded,
    const std::size_t source_node_index,
    const std::size_t node_count,
    const std::size_t leaf_count) noexcept
{
    if (encoded < static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()) ||
        encoded > static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())) {
        return std::nullopt;
    }
    if (encoded >= 0) {
        const auto node_index = static_cast<std::size_t>(encoded);
        if (node_index >= node_count || node_index == source_node_index) {
            return std::nullopt;
        }
        return world_spatial::WorldSpatialNodeChild{
            WorldSpatialNodeChildKind::node,
            static_cast<std::uint32_t>(node_index),
        };
    }
    if (encoded == static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min())) {
        return std::nullopt;
    }
    const auto leaf_index = static_cast<std::size_t>(-encoded - 1);
    if (leaf_index >= leaf_count) {
        return std::nullopt;
    }
    return world_spatial::WorldSpatialNodeChild{
        WorldSpatialNodeChildKind::leaf,
        static_cast<std::uint32_t>(leaf_index),
    };
}

[[nodiscard]] bool reachable_world_graph_is_acyclic(
    const std::span<const world_spatial::WorldSpatialNode> nodes,
    const std::uint32_t root_node_index)
{
    enum class VisitState : std::uint8_t {
        unvisited,
        active,
        complete,
    };
    struct Frame {
        std::uint32_t node_index{0U};
        std::size_t next_child{0U};
    };

    if (static_cast<std::size_t>(root_node_index) >= nodes.size()) {
        return false;
    }
    std::vector<VisitState> states(nodes.size(), VisitState::unvisited);
    std::vector<Frame> stack;
    stack.reserve(nodes.size());
    states[root_node_index] = VisitState::active;
    stack.push_back(Frame{root_node_index, 0U});

    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.next_child == 2U) {
            states[frame.node_index] = VisitState::complete;
            stack.pop_back();
            continue;
        }
        const auto child = nodes[frame.node_index].children[frame.next_child];
        ++frame.next_child;
        if (child.kind == WorldSpatialNodeChildKind::leaf) {
            continue;
        }
        const auto child_index = static_cast<std::size_t>(child.index);
        if (child_index >= nodes.size() || states[child_index] == VisitState::active) {
            return false;
        }
        if (states[child_index] == VisitState::unvisited) {
            states[child_index] = VisitState::active;
            stack.push_back(Frame{child.index, 0U});
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::byte> all_visible_row(
    const std::uint32_t visible_leaf_count,
    const std::size_t row_byte_count)
{
    std::vector<std::byte> row(row_byte_count, std::byte{0xFFU});
    const auto partial_bit_count = visible_leaf_count % 8U;
    if (!row.empty() && partial_bit_count != 0U) {
        const auto mask = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(1U) << partial_bit_count) - 1U);
        row.back() = static_cast<std::byte>(mask);
    }
    return row;
}

void mask_unused_final_pvs_bits(
    std::vector<std::byte>& row,
    const std::uint32_t visible_leaf_count) noexcept
{
    const auto partial_bit_count = visible_leaf_count % 8U;
    if (row.empty() || partial_bit_count == 0U) {
        return;
    }
    const auto mask = static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(1U) << partial_bit_count) - 1U);
    row.back() &= static_cast<std::byte>(mask);
}

[[nodiscard]] std::uint64_t pvs_row_hash(
    const std::span<const std::byte> row) noexcept
{
    // The hash is only an accelerator. Every match is confirmed byte-for-byte
    // before rows are deduplicated, so collisions cannot affect semantics.
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    for (const auto value : row) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

} // namespace

bool valid_goldsrc_spatial_import_limits(
    const GoldSrcSpatialImportLimits& limits) noexcept
{
    return limits.maximum_planes > 0U &&
        limits.maximum_planes <= kGoldSrcSpatialHardMaximumPlanes &&
        limits.maximum_nodes > 0U &&
        limits.maximum_nodes <= kGoldSrcSpatialHardMaximumNodes &&
        limits.maximum_leaves > 0U &&
        limits.maximum_leaves <= kGoldSrcSpatialHardMaximumLeaves &&
        limits.maximum_marksurface_links > 0U &&
        limits.maximum_marksurface_links <=
            kGoldSrcSpatialHardMaximumMarksurfaceLinks &&
        limits.maximum_unique_pvs_rows > 0U &&
        limits.maximum_unique_pvs_rows <=
            kGoldSrcSpatialHardMaximumUniquePvsRows &&
        limits.maximum_pvs_row_bytes > 0U &&
        limits.maximum_pvs_row_bytes <= kGoldSrcPvsHardMaximumRowBytes &&
        limits.maximum_decompressed_pvs_bytes > 0U &&
        limits.maximum_decompressed_pvs_bytes <=
            kGoldSrcPvsHardMaximumDecompressedBytes &&
        limits.maximum_world_surfaces > 0U &&
        limits.maximum_world_surfaces <=
            kGoldSrcSpatialHardMaximumWorldSurfaces &&
        limits.maximum_source_faces > 0U &&
        limits.maximum_source_faces <= kGoldSrcSpatialHardMaximumSourceFaces;
}

std::string_view to_string(const GoldSrcSpatialImportErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcSpatialImportErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcSpatialImportErrorCode::count_limit_exceeded:
        return "count_limit_exceeded";
    case GoldSrcSpatialImportErrorCode::invalid_plane: return "invalid_plane";
    case GoldSrcSpatialImportErrorCode::invalid_node: return "invalid_node";
    case GoldSrcSpatialImportErrorCode::invalid_leaf: return "invalid_leaf";
    case GoldSrcSpatialImportErrorCode::invalid_world_model:
        return "invalid_world_model";
    case GoldSrcSpatialImportErrorCode::node_cycle: return "node_cycle";
    case GoldSrcSpatialImportErrorCode::invalid_marksurface_reference:
        return "invalid_marksurface_reference";
    case GoldSrcSpatialImportErrorCode::invalid_face_ownership:
        return "invalid_face_ownership";
    case GoldSrcSpatialImportErrorCode::invalid_world_surface:
        return "invalid_world_surface";
    case GoldSrcSpatialImportErrorCode::ambiguous_world_surface_mapping:
        return "ambiguous_world_surface_mapping";
    case GoldSrcSpatialImportErrorCode::pvs_decode_failed:
        return "pvs_decode_failed";
    case GoldSrcSpatialImportErrorCode::pvs_table_limit_exceeded:
        return "pvs_table_limit_exceeded";
    case GoldSrcSpatialImportErrorCode::unable_to_retain_package:
        return "unable_to_retain_package";
    }
    return "unknown";
}

GoldSrcSpatialBuildResult GoldSrcSpatialPackageBuilder::build(
    const GoldSrcSpatialBuildInput& input,
    const GoldSrcSpatialImportLimits& limits)
{
    try {
    if (!valid_goldsrc_spatial_import_limits(limits)) {
        return failure(GoldSrcSpatialImportErrorCode::invalid_configuration);
    }
    if (input.planes.empty() || input.nodes.empty() || input.leaves.empty()) {
        return failure(GoldSrcSpatialImportErrorCode::invalid_configuration);
    }
    if (input.planes.size() > limits.maximum_planes ||
        input.nodes.size() > limits.maximum_nodes ||
        input.leaves.size() > limits.maximum_leaves ||
        input.marksurface_face_ordinals.size() >
            limits.maximum_marksurface_links ||
        input.world_surfaces.size() > limits.maximum_world_surfaces ||
        input.submodel_face_ordinals.size() > limits.maximum_source_faces ||
        static_cast<std::size_t>(input.source_face_count) >
            limits.maximum_source_faces) {
        return failure(GoldSrcSpatialImportErrorCode::count_limit_exceeded);
    }
    if (!valid_bounds(input.world_model.bounds) ||
        input.world_model.render_headnode < 0 ||
        static_cast<std::size_t>(input.world_model.render_headnode) >=
            input.nodes.size() ||
        input.world_model.visible_leaf_count < 0) {
        return failure(GoldSrcSpatialImportErrorCode::invalid_world_model);
    }
    const auto visible_leaf_count = static_cast<std::uint32_t>(
        input.world_model.visible_leaf_count);
    if (static_cast<std::size_t>(visible_leaf_count) + 1U > input.leaves.size()) {
        return failure(GoldSrcSpatialImportErrorCode::invalid_world_model);
    }
    const auto row_byte_count =
        (static_cast<std::size_t>(visible_leaf_count) + 7U) / 8U;
    if (row_byte_count > limits.maximum_pvs_row_bytes) {
        return failure(GoldSrcSpatialImportErrorCode::pvs_table_limit_exceeded);
    }

    enum class FaceOwnership : std::uint8_t {
        unowned,
        world,
        submodel,
    };
    std::vector<FaceOwnership> face_ownership(
        static_cast<std::size_t>(input.source_face_count),
        FaceOwnership::unowned);
    std::unordered_map<std::uint32_t, std::uint32_t> world_surface_by_face;
    world_surface_by_face.reserve(input.world_surfaces.size());
    for (std::size_t surface_index = 0U;
         surface_index < input.world_surfaces.size();
         ++surface_index) {
        const auto& surface = input.world_surfaces[surface_index];
        if (!surface.source_surface_ordinal ||
            *surface.source_surface_ordinal >= input.source_face_count ||
            !valid_bounds(surface.bounds)) {
            return failure(
                GoldSrcSpatialImportErrorCode::invalid_world_surface,
                surface_index);
        }
        const auto [unused, inserted] = world_surface_by_face.emplace(
            *surface.source_surface_ordinal,
            static_cast<std::uint32_t>(surface_index));
        static_cast<void>(unused);
        if (!inserted) {
            return failure(
                GoldSrcSpatialImportErrorCode::ambiguous_world_surface_mapping,
                surface_index);
        }
        face_ownership[*surface.source_surface_ordinal] = FaceOwnership::world;
    }
    for (std::size_t ownership_index = 0U;
         ownership_index < input.submodel_face_ordinals.size();
         ++ownership_index) {
        const auto source_face = input.submodel_face_ordinals[ownership_index];
        if (source_face >= input.source_face_count ||
            face_ownership[source_face] != FaceOwnership::unowned) {
            return failure(
                GoldSrcSpatialImportErrorCode::invalid_face_ownership,
                ownership_index);
        }
        face_ownership[source_face] = FaceOwnership::submodel;
    }

    std::vector<world_spatial::WorldSpatialPlane> planes;
    planes.reserve(input.planes.size());
    for (std::size_t plane_index = 0U; plane_index < input.planes.size(); ++plane_index) {
        const auto& source = input.planes[plane_index];
        if (!finite_vector(source.normal) || !std::isfinite(source.distance)) {
            return failure(GoldSrcSpatialImportErrorCode::invalid_plane, plane_index);
        }
        const auto length_squared =
            static_cast<double>(source.normal.x) * source.normal.x +
            static_cast<double>(source.normal.y) * source.normal.y +
            static_cast<double>(source.normal.z) * source.normal.z;
        if (!std::isfinite(length_squared) ||
            length_squared <= std::numeric_limits<double>::epsilon()) {
            return failure(GoldSrcSpatialImportErrorCode::invalid_plane, plane_index);
        }
        const auto length = std::sqrt(length_squared);
        const auto normalized_distance = source.distance / length;
        const auto maximum_float = static_cast<double>(
            std::numeric_limits<float>::max());
        if (!std::isfinite(normalized_distance) ||
            std::abs(normalized_distance) > maximum_float) {
            return failure(GoldSrcSpatialImportErrorCode::invalid_plane, plane_index);
        }
        planes.push_back(world_spatial::WorldSpatialPlane{
            assets::AssetVector3{
                static_cast<float>(static_cast<double>(source.normal.x) / length),
                static_cast<float>(static_cast<double>(source.normal.y) / length),
                static_cast<float>(static_cast<double>(source.normal.z) / length),
            },
            static_cast<float>(normalized_distance),
            source.source_type,
        });
    }

    std::vector<world_spatial::WorldSpatialNode> nodes;
    nodes.reserve(input.nodes.size());
    for (std::size_t node_index = 0U; node_index < input.nodes.size(); ++node_index) {
        const auto& source = input.nodes[node_index];
        if (source.plane_index < 0 ||
            static_cast<std::size_t>(source.plane_index) >= planes.size() ||
            !valid_bounds(source.bounds) ||
            source.first_source_face.has_value() !=
                source.source_face_count.has_value()) {
            return failure(GoldSrcSpatialImportErrorCode::invalid_node, node_index);
        }
        if (source.first_source_face) {
            std::size_t face_end = 0U;
            if (!checked_add(
                    static_cast<std::size_t>(*source.first_source_face),
                    static_cast<std::size_t>(*source.source_face_count),
                    face_end) ||
                face_end > static_cast<std::size_t>(input.source_face_count)) {
                return failure(GoldSrcSpatialImportErrorCode::invalid_node, node_index);
            }
        }
        const auto front = decode_child(
            source.encoded_children[0U],
            node_index,
            input.nodes.size(),
            input.leaves.size());
        const auto back = decode_child(
            source.encoded_children[1U],
            node_index,
            input.nodes.size(),
            input.leaves.size());
        if (!front || !back) {
            return failure(GoldSrcSpatialImportErrorCode::invalid_node, node_index);
        }
        nodes.push_back(world_spatial::WorldSpatialNode{
            static_cast<std::uint32_t>(source.plane_index),
            {*front, *back},
            source.bounds,
            source.first_source_face,
            source.source_face_count,
        });
    }

    const auto root_node_index = static_cast<std::uint32_t>(
        input.world_model.render_headnode);
    if (!reachable_world_graph_is_acyclic(nodes, root_node_index)) {
        return failure(GoldSrcSpatialImportErrorCode::node_cycle);
    }

    std::vector<world_spatial::WorldSpatialLeaf> leaves;
    leaves.reserve(input.leaves.size());
    std::vector<std::uint32_t> surface_seen_generation(
        input.world_surfaces.size(),
        0U);
    std::size_t total_marksurface_links = 0U;
    std::size_t mapped_world_surface_links = 0U;
    for (std::size_t leaf_index = 0U; leaf_index < input.leaves.size(); ++leaf_index) {
        const auto& source = input.leaves[leaf_index];
        std::size_t marksurface_end = 0U;
        if (!supported_contents(source.contents) ||
            source.visibility_offset < -1 || !valid_bounds(source.bounds) ||
            !checked_add(
                static_cast<std::size_t>(source.first_marksurface),
                static_cast<std::size_t>(source.marksurface_count),
                marksurface_end) ||
            marksurface_end > input.marksurface_face_ordinals.size() ||
            !checked_add(
                total_marksurface_links,
                static_cast<std::size_t>(source.marksurface_count),
                total_marksurface_links) ||
            total_marksurface_links > limits.maximum_marksurface_links ||
            (source.visibility_offset >= 0 &&
             static_cast<std::size_t>(source.visibility_offset) >=
                 input.visibility_bytes.size())) {
            return failure(GoldSrcSpatialImportErrorCode::invalid_leaf, leaf_index);
        }

        world_spatial::WorldLeafSurfaceMembership membership;
        membership.source_leaf_index = static_cast<std::uint32_t>(leaf_index);
        membership.source_marksurface_count = source.marksurface_count;
        membership.world_surface_indices.reserve(source.marksurface_count);
        const auto generation = static_cast<std::uint32_t>(leaf_index + 1U);
        for (std::size_t link = static_cast<std::size_t>(source.first_marksurface);
             link < marksurface_end;
             ++link) {
            const auto source_face = input.marksurface_face_ordinals[link];
            if (source_face >= input.source_face_count) {
                return failure(
                    GoldSrcSpatialImportErrorCode::invalid_marksurface_reference,
                    link);
            }
            const auto mapped = world_surface_by_face.find(source_face);
            if (mapped == world_surface_by_face.end()) {
                if (face_ownership[source_face] == FaceOwnership::submodel) {
                    continue;
                }
                return failure(
                    GoldSrcSpatialImportErrorCode::invalid_marksurface_reference,
                    link);
            }
            const auto surface_index = mapped->second;
            if (surface_seen_generation[surface_index] == generation) {
                continue;
            }
            surface_seen_generation[surface_index] = generation;
            membership.world_surface_indices.push_back(surface_index);
            ++mapped_world_surface_links;
        }
        leaves.push_back(world_spatial::WorldSpatialLeaf{
            static_cast<std::uint32_t>(leaf_index),
            source.contents,
            source.bounds,
            std::nullopt,
            std::move(membership),
            leaf_index != 0U && leaf_index <= visible_leaf_count,
            leaf_index == 0U || source.contents == -2,
        });
    }

    auto all_visible = all_visible_row(visible_leaf_count, row_byte_count);
    std::vector<std::vector<std::byte>> unique_rows;
    unique_rows.reserve(std::min(
        input.leaves.size(),
        limits.maximum_unique_pvs_rows));
    unique_rows.push_back(all_visible);
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
        unique_row_indices_by_hash;
    unique_row_indices_by_hash.reserve(std::min(
        input.leaves.size(),
        limits.maximum_unique_pvs_rows));
    unique_row_indices_by_hash[pvs_row_hash(all_visible)].push_back(0U);
    std::unordered_map<std::size_t, std::uint32_t> row_index_by_source_offset;
    row_index_by_source_offset.reserve(input.leaves.size());
    std::vector<std::optional<std::uint32_t>> leaf_row_indices(
        input.leaves.size(),
        std::nullopt);
    std::size_t decompressed_pvs_bytes = row_byte_count;
    if (decompressed_pvs_bytes > limits.maximum_decompressed_pvs_bytes) {
        return failure(GoldSrcSpatialImportErrorCode::pvs_table_limit_exceeded);
    }

    const GoldSrcPvsDecodeLimits decoder_limits{
        limits.maximum_pvs_row_bytes,
        limits.maximum_decompressed_pvs_bytes,
    };
    for (std::size_t leaf_index = 1U;
         leaf_index <= static_cast<std::size_t>(visible_leaf_count);
         ++leaf_index) {
        const auto visibility_offset = input.leaves[leaf_index].visibility_offset;
        std::uint32_t row_index = 0U;
        if (visibility_offset >= 0) {
            const auto source_offset = static_cast<std::size_t>(visibility_offset);
            const auto cached = row_index_by_source_offset.find(source_offset);
            if (cached != row_index_by_source_offset.end()) {
                row_index = cached->second;
            } else {
                auto decoded = GoldSrcPvsDecoder::decode(
                    input.visibility_bytes,
                    source_offset,
                    row_byte_count,
                    decoder_limits);
                if (!decoded) {
                    return failure(
                        GoldSrcSpatialImportErrorCode::pvs_decode_failed,
                        leaf_index,
                        decoded.error);
                }
                mask_unused_final_pvs_bits(*decoded.row, visible_leaf_count);
                const auto decoded_hash = pvs_row_hash(*decoded.row);
                const auto bucket = unique_row_indices_by_hash.find(decoded_hash);
                std::optional<std::uint32_t> existing_row_index;
                if (bucket != unique_row_indices_by_hash.end()) {
                    for (const auto candidate_index : bucket->second) {
                        if (unique_rows[candidate_index] == *decoded.row) {
                            existing_row_index = candidate_index;
                            break;
                        }
                    }
                }
                if (existing_row_index) {
                    row_index = *existing_row_index;
                } else {
                    std::size_t new_decompressed_size = 0U;
                    if (unique_rows.size() >= limits.maximum_unique_pvs_rows ||
                        !checked_add(
                            decompressed_pvs_bytes,
                            row_byte_count,
                            new_decompressed_size) ||
                        new_decompressed_size >
                            limits.maximum_decompressed_pvs_bytes) {
                        return failure(
                            GoldSrcSpatialImportErrorCode::pvs_table_limit_exceeded,
                            leaf_index);
                    }
                    row_index = static_cast<std::uint32_t>(unique_rows.size());
                    decompressed_pvs_bytes = new_decompressed_size;
                    unique_rows.push_back(std::move(*decoded.row));
                    unique_row_indices_by_hash[decoded_hash].push_back(row_index);
                }
                row_index_by_source_offset.emplace(source_offset, row_index);
            }
        }
        leaf_row_indices[leaf_index] = row_index;
        leaves[leaf_index].pvs_row_index = row_index;
    }

    world_spatial::WorldPvsTable pvs_table{
        row_byte_count,
        visible_leaf_count,
        std::move(unique_rows),
        std::move(leaf_row_indices),
        0U,
    };
    const world_spatial::WorldSpatialStatistics statistics{
        static_cast<std::uint64_t>(planes.size()),
        static_cast<std::uint64_t>(nodes.size()),
        static_cast<std::uint64_t>(leaves.size()),
        static_cast<std::uint64_t>(total_marksurface_links),
        static_cast<std::uint64_t>(mapped_world_surface_links),
        static_cast<std::uint64_t>(pvs_table.unique_row_count()),
        static_cast<std::uint64_t>(decompressed_pvs_bytes),
    };

    return GoldSrcSpatialBuildResult{
        world_spatial::WorldSpatialPackage{
            std::move(planes),
            std::move(nodes),
            std::move(leaves),
            std::move(pvs_table),
            world_spatial::WorldSpatialModelMetadata{
                root_node_index,
                visible_leaf_count,
                input.world_model.bounds,
            },
            statistics,
            world_spatial::WorldSpatialCompatibilityProfile::
                goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
            world_spatial::WorldSpatialEvidenceProfile::
                canonical_validated_bsp_records,
        },
        std::nullopt,
    };
    } catch (const std::bad_alloc&) {
        return failure(
            GoldSrcSpatialImportErrorCode::unable_to_retain_package);
    } catch (const std::length_error&) {
        return failure(
            GoldSrcSpatialImportErrorCode::unable_to_retain_package);
    }
}

} // namespace hlclient::goldsrc::spatial
