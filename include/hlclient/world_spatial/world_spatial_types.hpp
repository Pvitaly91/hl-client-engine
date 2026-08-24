#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hlclient::world_spatial {

enum class WorldSpatialCompatibilityProfile {
    goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
};

enum class WorldSpatialEvidenceProfile {
    canonical_validated_bsp_records,
};

enum class WorldSpatialNodeChildKind {
    node,
    leaf,
};

struct WorldSpatialPlane {
    assets::AssetVector3 normal{};
    float distance{0.0F};
    std::optional<std::int32_t> source_type;
};

struct WorldSpatialNodeChild {
    WorldSpatialNodeChildKind kind{WorldSpatialNodeChildKind::leaf};
    std::uint32_t index{0U};
};

struct WorldSpatialNode {
    std::uint32_t plane_index{0U};
    // GoldSrc child 0 is the front half-space and child 1 is the back
    // half-space. The encoded wire sign is intentionally not retained.
    std::array<WorldSpatialNodeChild, 2U> children{};
    assets::WorldBounds bounds{};
    std::optional<std::uint32_t> first_source_face;
    std::optional<std::uint32_t> source_face_count;
};

struct WorldLeafSurfaceMembership {
    std::uint32_t source_leaf_index{0U};
    std::uint32_t source_marksurface_count{0U};
    std::vector<std::uint32_t> world_surface_indices;
};

struct WorldSpatialLeaf {
    std::uint32_t source_leaf_index{0U};
    std::int32_t contents{0};
    assets::WorldBounds bounds{};
    // Leaf zero is deliberately std::nullopt: it is retained spatially but
    // never participates in GoldSrc PVS bit numbering.
    std::optional<std::uint32_t> pvs_row_index;
    WorldLeafSurfaceMembership surface_membership{};
    bool pvs_bit_addressable{false};
    // Format-neutral camera fallback signal. For the supported GoldSrc
    // profile this is true for leaf zero and CONTENTS_SOLID leaves.
    bool solid_or_special{false};
};

class WorldPvsTable final {
public:
    WorldPvsTable() = default;
    WorldPvsTable(
        std::size_t row_byte_count,
        std::uint32_t visible_leaf_count,
        std::vector<std::vector<std::byte>> unique_rows,
        std::vector<std::optional<std::uint32_t>> leaf_row_indices,
        std::uint32_t all_visible_row_index);

    [[nodiscard]] std::size_t row_byte_count() const noexcept;
    [[nodiscard]] std::uint32_t visible_leaf_count() const noexcept;
    [[nodiscard]] std::size_t unique_row_count() const noexcept;
    [[nodiscard]] std::uint32_t all_visible_row_index() const noexcept;
    [[nodiscard]] std::span<const std::optional<std::uint32_t>>
    leaf_row_indices() const noexcept;
    [[nodiscard]] std::optional<std::span<const std::byte>> row(
        std::uint32_t row_index) const noexcept;
    [[nodiscard]] std::optional<std::span<const std::byte>> row_for_leaf(
        std::uint32_t source_leaf_index) const noexcept;
    [[nodiscard]] bool leaf_has_usable_row(
        std::uint32_t source_leaf_index) const noexcept;
    [[nodiscard]] std::optional<bool> leaf_is_visible_from(
        std::uint32_t camera_leaf_index,
        std::uint32_t candidate_leaf_index) const noexcept;

private:
    std::size_t row_byte_count_{0U};
    std::uint32_t visible_leaf_count_{0U};
    std::vector<std::vector<std::byte>> unique_rows_;
    std::vector<std::optional<std::uint32_t>> leaf_row_indices_;
    std::uint32_t all_visible_row_index_{0U};
};

struct WorldSpatialModelMetadata {
    std::uint32_t root_node_index{0U};
    std::uint32_t visible_leaf_count{0U};
    assets::WorldBounds bounds{};
};

struct WorldSpatialStatistics {
    std::uint64_t plane_count{0U};
    std::uint64_t node_count{0U};
    std::uint64_t leaf_count{0U};
    std::uint64_t marksurface_link_count{0U};
    std::uint64_t mapped_world_surface_link_count{0U};
    std::uint64_t unique_pvs_row_count{0U};
    std::uint64_t decompressed_pvs_bytes{0U};
};

class WorldSpatialPackage final {
public:
    WorldSpatialPackage() = default;
    WorldSpatialPackage(
        std::vector<WorldSpatialPlane> planes,
        std::vector<WorldSpatialNode> nodes,
        std::vector<WorldSpatialLeaf> leaves,
        WorldPvsTable pvs_table,
        WorldSpatialModelMetadata world_model,
        WorldSpatialStatistics statistics,
        WorldSpatialCompatibilityProfile compatibility_profile,
        WorldSpatialEvidenceProfile evidence_profile);

    [[nodiscard]] std::span<const WorldSpatialPlane> planes() const noexcept;
    [[nodiscard]] std::span<const WorldSpatialNode> nodes() const noexcept;
    [[nodiscard]] std::span<const WorldSpatialLeaf> leaves() const noexcept;
    [[nodiscard]] const WorldPvsTable& pvs_table() const noexcept;
    [[nodiscard]] const WorldSpatialModelMetadata& world_model() const noexcept;
    [[nodiscard]] const WorldSpatialStatistics& statistics() const noexcept;
    [[nodiscard]] WorldSpatialCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] WorldSpatialEvidenceProfile evidence_profile() const noexcept;

private:
    std::vector<WorldSpatialPlane> planes_;
    std::vector<WorldSpatialNode> nodes_;
    std::vector<WorldSpatialLeaf> leaves_;
    WorldPvsTable pvs_table_;
    WorldSpatialModelMetadata world_model_{};
    WorldSpatialStatistics statistics_{};
    WorldSpatialCompatibilityProfile compatibility_profile_{
        WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero};
    WorldSpatialEvidenceProfile evidence_profile_{
        WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
};

} // namespace hlclient::world_spatial
