#include <hlclient/collision/collision_world_package.hpp>

#include <utility>

namespace hlclient::collision {

std::string_view to_string(const CollisionHullOrdinal hull) noexcept
{
    switch (hull) {
    case CollisionHullOrdinal::point: return "point";
    case CollisionHullOrdinal::standing_32x32x72:
        return "standing_32x32x72";
    case CollisionHullOrdinal::large_64_cube: return "large_64_cube";
    case CollisionHullOrdinal::duck_32x32x36: return "duck_32x32x36";
    }
    return "unknown";
}

std::optional<CollisionHullOrdinal> collision_hull_ordinal(
    const std::size_t ordinal) noexcept
{
    switch (ordinal) {
    case 0U: return CollisionHullOrdinal::point;
    case 1U: return CollisionHullOrdinal::standing_32x32x72;
    case 2U: return CollisionHullOrdinal::large_64_cube;
    case 3U: return CollisionHullOrdinal::duck_32x32x36;
    default: return std::nullopt;
    }
}

std::optional<CollisionHullProfile> standard_collision_hull_profile(
    const CollisionHullOrdinal ordinal) noexcept
{
    switch (ordinal) {
    case CollisionHullOrdinal::point:
        return CollisionHullProfile{ordinal, {}, {}};
    case CollisionHullOrdinal::standing_32x32x72:
        return CollisionHullProfile{
            ordinal, {-16.0F, -16.0F, -36.0F}, {16.0F, 16.0F, 36.0F}};
    case CollisionHullOrdinal::large_64_cube:
        return CollisionHullProfile{
            ordinal, {-32.0F, -32.0F, -32.0F}, {32.0F, 32.0F, 32.0F}};
    case CollisionHullOrdinal::duck_32x32x36:
        return CollisionHullProfile{
            ordinal, {-16.0F, -16.0F, -18.0F}, {16.0F, 16.0F, 18.0F}};
    }
    return std::nullopt;
}

const CollisionHull* CollisionModel::hull(
    const CollisionHullOrdinal ordinal) const noexcept
{
    const auto ordinal_value = static_cast<std::size_t>(ordinal);
    if (!collision_hull_ordinal(ordinal_value) || ordinal_value >= hulls.size()) {
        return nullptr;
    }
    const auto& candidate = hulls[ordinal_value];
    return candidate.ordinal == ordinal ? &candidate : nullptr;
}

CollisionWorldPackage::CollisionWorldPackage(
    std::vector<CollisionPlane> planes,
    std::vector<CollisionNode> nodes,
    std::vector<CollisionLeaf> leaves,
    std::vector<CollisionClipnode> clipnodes,
    std::vector<CollisionModel> models,
    CollisionWorldIdentity identity,
    const CollisionWorldStatistics statistics,
    const CollisionWorldCompatibilityProfile compatibility_profile,
    const CollisionWorldEvidenceProfile evidence_profile)
    : planes_{std::move(planes)},
      nodes_{std::move(nodes)},
      leaves_{std::move(leaves)},
      clipnodes_{std::move(clipnodes)},
      models_{std::move(models)},
      identity_{std::move(identity)},
      statistics_{statistics},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

std::span<const CollisionPlane> CollisionWorldPackage::planes() const noexcept
{
    return planes_;
}

std::span<const CollisionNode> CollisionWorldPackage::nodes() const noexcept
{
    return nodes_;
}

std::span<const CollisionLeaf> CollisionWorldPackage::leaves() const noexcept
{
    return leaves_;
}

std::span<const CollisionClipnode> CollisionWorldPackage::clipnodes() const noexcept
{
    return clipnodes_;
}

std::span<const CollisionModel> CollisionWorldPackage::models() const noexcept
{
    return models_;
}

const CollisionModel* CollisionWorldPackage::model(
    const std::uint32_t source_model_index) const noexcept
{
    const CollisionModel* found = nullptr;
    for (const auto& candidate : models_) {
        if (candidate.source_model_index != source_model_index) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &candidate;
    }
    return found;
}

const CollisionWorldIdentity& CollisionWorldPackage::identity() const noexcept
{
    return identity_;
}

const CollisionWorldStatistics& CollisionWorldPackage::statistics() const noexcept
{
    return statistics_;
}

CollisionWorldCompatibilityProfile
CollisionWorldPackage::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

CollisionWorldEvidenceProfile CollisionWorldPackage::evidence_profile() const noexcept
{
    return evidence_profile_;
}

} // namespace hlclient::collision
