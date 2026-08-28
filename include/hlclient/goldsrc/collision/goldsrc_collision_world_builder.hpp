#pragma once

#include <hlclient/collision/collision_world_package.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_collision_source.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::bsp {
struct GoldSrcBspParsedDocument;
}

namespace hlclient::goldsrc::collision {

inline constexpr std::size_t kGoldSrcCollisionHardMaximumHulls = 1'600U;
inline constexpr std::size_t
    kGoldSrcCollisionHardMaximumReachableLinks = 67'108'864U;
inline constexpr std::size_t
    kGoldSrcCollisionHardMaximumValidationSteps = 67'108'864U;
inline constexpr std::size_t kGoldSrcCollisionHardMaximumTreeDepth = 65'536U;
inline constexpr std::size_t kGoldSrcCollisionHardMaximumBytes =
    256U * 1024U * 1024U;

struct GoldSrcCollisionBuildLimits {
    std::size_t maximum_planes{
        hlclient::collision::kCollisionHardMaximumPlanes};
    std::size_t maximum_nodes{
        hlclient::collision::kCollisionHardMaximumNodes};
    std::size_t maximum_leaves{
        hlclient::collision::kCollisionHardMaximumLeaves};
    std::size_t maximum_clipnodes{
        hlclient::collision::kCollisionHardMaximumClipnodes};
    std::size_t maximum_models{
        hlclient::collision::kCollisionHardMaximumModels};
    std::size_t maximum_hulls{kGoldSrcCollisionHardMaximumHulls};
    std::size_t maximum_reachable_links{
        kGoldSrcCollisionHardMaximumReachableLinks};
    std::size_t maximum_validation_steps{
        kGoldSrcCollisionHardMaximumValidationSteps};
    std::size_t maximum_tree_depth{kGoldSrcCollisionHardMaximumTreeDepth};
    std::size_t maximum_collision_bytes{
        kGoldSrcCollisionHardMaximumBytes};
};

[[nodiscard]] bool valid_goldsrc_collision_build_limits(
    const GoldSrcCollisionBuildLimits& limits) noexcept;

enum class CollisionWorldBuildErrorCode : std::uint8_t {
    invalid_configuration,
    unsupported_profile,
    count_limit_exceeded,
    memory_limit_exceeded,
    invalid_plane,
    invalid_contents,
    invalid_node_reference,
    invalid_leaf_reference,
    invalid_clipnode_reference,
    invalid_model,
    invalid_hull_profile,
    cycle_detected,
    traversal_limit_exceeded,
    unable_to_publish,
};

[[nodiscard]] std::string_view to_string(
    CollisionWorldBuildErrorCode code) noexcept;

struct CollisionWorldBuildError {
    CollisionWorldBuildErrorCode code{
        CollisionWorldBuildErrorCode::invalid_configuration};
    std::optional<std::uint32_t> source_model_index;
    std::optional<hlclient::collision::CollisionHullOrdinal> hull;
    std::optional<std::uint32_t> source_record_index;
    std::string context;
};

struct CollisionWorldBuildResult {
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package;
    std::optional<CollisionWorldBuildError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return package != nullptr && !error.has_value();
    }
};

class GoldSrcCollisionWorldBuilder final {
public:
    [[nodiscard]] static CollisionWorldBuildResult build(
        const bsp::GoldSrcBspCollisionSource& source,
        const GoldSrcCollisionBuildLimits& limits = {});

    [[nodiscard]] static CollisionWorldBuildResult build(
        const bsp::GoldSrcBspParsedDocument& document,
        const GoldSrcCollisionBuildLimits& limits = {});
};

} // namespace hlclient::goldsrc::collision
