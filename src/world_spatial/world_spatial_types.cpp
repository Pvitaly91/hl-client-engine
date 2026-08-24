#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <utility>

namespace hlclient::world_spatial {

WorldPvsTable::WorldPvsTable(
    const std::size_t row_byte_count,
    const std::uint32_t visible_leaf_count,
    std::vector<std::vector<std::byte>> unique_rows,
    std::vector<std::optional<std::uint32_t>> leaf_row_indices,
    const std::uint32_t all_visible_row_index)
    : row_byte_count_(row_byte_count),
      visible_leaf_count_(visible_leaf_count),
      unique_rows_(std::move(unique_rows)),
      leaf_row_indices_(std::move(leaf_row_indices)),
      all_visible_row_index_(all_visible_row_index)
{
}

std::size_t WorldPvsTable::row_byte_count() const noexcept
{
    return row_byte_count_;
}

std::uint32_t WorldPvsTable::visible_leaf_count() const noexcept
{
    return visible_leaf_count_;
}

std::size_t WorldPvsTable::unique_row_count() const noexcept
{
    return unique_rows_.size();
}

std::uint32_t WorldPvsTable::all_visible_row_index() const noexcept
{
    return all_visible_row_index_;
}

std::span<const std::optional<std::uint32_t>>
WorldPvsTable::leaf_row_indices() const noexcept
{
    return leaf_row_indices_;
}

std::optional<std::span<const std::byte>> WorldPvsTable::row(
    const std::uint32_t row_index) const noexcept
{
    if (static_cast<std::size_t>(row_index) >= unique_rows_.size()) {
        return std::nullopt;
    }
    return std::span<const std::byte>{unique_rows_[row_index]};
}

std::optional<std::span<const std::byte>> WorldPvsTable::row_for_leaf(
    const std::uint32_t source_leaf_index) const noexcept
{
    if (static_cast<std::size_t>(source_leaf_index) >= leaf_row_indices_.size()) {
        return std::nullopt;
    }
    const auto row_index = leaf_row_indices_[source_leaf_index];
    return row_index ? row(*row_index) : std::nullopt;
}

bool WorldPvsTable::leaf_has_usable_row(
    const std::uint32_t source_leaf_index) const noexcept
{
    return row_for_leaf(source_leaf_index).has_value();
}

std::optional<bool> WorldPvsTable::leaf_is_visible_from(
    const std::uint32_t camera_leaf_index,
    const std::uint32_t candidate_leaf_index) const noexcept
{
    const auto camera_row = row_for_leaf(camera_leaf_index);
    if (!camera_row) {
        return std::nullopt;
    }
    if (candidate_leaf_index == 0U || candidate_leaf_index > visible_leaf_count_) {
        return false;
    }
    const auto bit_index = static_cast<std::size_t>(candidate_leaf_index - 1U);
    const auto byte_index = bit_index / 8U;
    if (byte_index >= camera_row->size()) {
        return false;
    }
    const auto bit_mask = static_cast<std::uint8_t>(1U << (bit_index % 8U));
    return (std::to_integer<std::uint8_t>((*camera_row)[byte_index]) & bit_mask) != 0U;
}

WorldSpatialPackage::WorldSpatialPackage(
    std::vector<WorldSpatialPlane> planes,
    std::vector<WorldSpatialNode> nodes,
    std::vector<WorldSpatialLeaf> leaves,
    WorldPvsTable pvs_table,
    const WorldSpatialModelMetadata world_model,
    const WorldSpatialStatistics statistics,
    const WorldSpatialCompatibilityProfile compatibility_profile,
    const WorldSpatialEvidenceProfile evidence_profile)
    : planes_(std::move(planes)),
      nodes_(std::move(nodes)),
      leaves_(std::move(leaves)),
      pvs_table_(std::move(pvs_table)),
      world_model_(world_model),
      statistics_(statistics),
      compatibility_profile_(compatibility_profile),
      evidence_profile_(evidence_profile)
{
}

std::span<const WorldSpatialPlane> WorldSpatialPackage::planes() const noexcept
{
    return planes_;
}

std::span<const WorldSpatialNode> WorldSpatialPackage::nodes() const noexcept
{
    return nodes_;
}

std::span<const WorldSpatialLeaf> WorldSpatialPackage::leaves() const noexcept
{
    return leaves_;
}

const WorldPvsTable& WorldSpatialPackage::pvs_table() const noexcept
{
    return pvs_table_;
}

const WorldSpatialModelMetadata& WorldSpatialPackage::world_model() const noexcept
{
    return world_model_;
}

const WorldSpatialStatistics& WorldSpatialPackage::statistics() const noexcept
{
    return statistics_;
}

WorldSpatialCompatibilityProfile WorldSpatialPackage::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

WorldSpatialEvidenceProfile WorldSpatialPackage::evidence_profile() const noexcept
{
    return evidence_profile_;
}

} // namespace hlclient::world_spatial
