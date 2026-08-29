#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/collision/collision_contents.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::collision {

inline constexpr std::size_t kCollisionHardMaximumPlanes = 32'767U;
inline constexpr std::size_t kCollisionHardMaximumNodes = 32'767U;
inline constexpr std::size_t kCollisionHardMaximumLeaves = 8'192U;
inline constexpr std::size_t kCollisionHardMaximumClipnodes = 32'767U;
inline constexpr std::size_t kCollisionHardMaximumModels = 400U;
inline constexpr std::size_t kCollisionHullCount = 4U;

enum class CollisionWorldCompatibilityProfile : std::uint8_t {
    valve_bsp_v30_clip_hulls_v1,
};

enum class CollisionWorldEvidenceProfile : std::uint8_t {
    public_valve_bsp_compiler_and_original_map_validation,
};

enum class CollisionTraceCompatibilityProfile : std::uint8_t {
    project_deterministic_bsp_hull_trace_v1,
    stock_engine_trace_behavior_evidence_pending,
};

enum class CollisionTraceEvidenceProfile : std::uint8_t {
    public_bsp_structure_and_independent_fixtures,
    stock_engine_trace_behavior_evidence_pending,
};

enum class CollisionHullOrdinal : std::uint8_t {
    point = 0U,
    standing_32x32x72 = 1U,
    large_64_cube = 2U,
    duck_32x32x36 = 3U,
};

[[nodiscard]] std::string_view to_string(CollisionHullOrdinal hull) noexcept;
[[nodiscard]] std::optional<CollisionHullOrdinal> collision_hull_ordinal(
    std::size_t ordinal) noexcept;

struct CollisionHullProfile {
    CollisionHullOrdinal ordinal{CollisionHullOrdinal::point};
    assets::AssetVector3 clip_mins{};
    assets::AssetVector3 clip_maxs{};

    [[nodiscard]] friend bool operator==(
        const CollisionHullProfile& left,
        const CollisionHullProfile& right) noexcept
    {
        return left.ordinal == right.ordinal &&
            left.clip_mins.x == right.clip_mins.x &&
            left.clip_mins.y == right.clip_mins.y &&
            left.clip_mins.z == right.clip_mins.z &&
            left.clip_maxs.x == right.clip_maxs.x &&
            left.clip_maxs.y == right.clip_maxs.y &&
            left.clip_maxs.z == right.clip_maxs.z;
    }
};

[[nodiscard]] std::optional<CollisionHullProfile>
standard_collision_hull_profile(CollisionHullOrdinal ordinal) noexcept;

struct CollisionPlane {
    assets::AssetVector3 normal{};
    double distance{0.0};
    std::uint32_t source_plane_index{0U};
    std::int32_t source_type{0};
};

enum class CollisionNodeChildKind : std::uint8_t {
    node,
    leaf,
};

struct CollisionNodeChild {
    CollisionNodeChildKind kind{CollisionNodeChildKind::leaf};
    std::uint32_t index{0U};
};

struct CollisionNode {
    std::uint32_t plane_index{0U};
    std::array<CollisionNodeChild, 2U> children{};
};

struct CollisionLeaf {
    std::uint32_t source_leaf_index{0U};
    CollisionContents contents{};
};

enum class CollisionClipnodeChildKind : std::uint8_t {
    clipnode,
    terminal,
};

struct CollisionClipnodeChild {
    CollisionClipnodeChildKind kind{CollisionClipnodeChildKind::terminal};
    std::uint32_t index{0U};
    CollisionContents terminal{};
};

struct CollisionClipnode {
    std::uint32_t plane_index{0U};
    std::array<CollisionClipnodeChild, 2U> children{};
};

enum class CollisionHullTreeDomain : std::uint8_t {
    node_leaf,
    clipnode,
};

enum class CollisionHullRootKind : std::uint8_t {
    node,
    clipnode,
    terminal,
};

struct CollisionHullRoot {
    CollisionHullRootKind kind{CollisionHullRootKind::node};
    std::uint32_t index{0U};
    CollisionContents terminal{};
};

struct CollisionHull {
    CollisionHullOrdinal ordinal{CollisionHullOrdinal::point};
    CollisionHullTreeDomain domain{CollisionHullTreeDomain::node_leaf};
    CollisionHullRoot root{};
    CollisionHullProfile profile{};
};

struct CollisionModel {
    std::uint32_t source_model_index{0U};
    assets::AssetVector3 source_origin{};
    assets::WorldBounds source_bounds{};
    std::uint32_t first_source_face{0U};
    std::uint32_t source_face_count{0U};
    std::array<CollisionHull, kCollisionHullCount> hulls{};

    [[nodiscard]] const CollisionHull* hull(
        CollisionHullOrdinal ordinal) const noexcept;
};

struct CollisionWorldStatistics {
    std::uint64_t plane_count{0U};
    std::uint64_t node_count{0U};
    std::uint64_t leaf_count{0U};
    std::uint64_t clipnode_count{0U};
    std::uint64_t model_count{0U};
    std::uint64_t reachable_hull0_nodes{0U};
    std::uint64_t reachable_clipnodes{0U};
    std::uint64_t unreachable_clipnodes{0U};
    std::uint64_t model_hull_root_count{0U};
    std::uint64_t direct_terminal_root_count{0U};
    std::uint64_t maximum_tree_depth{0U};
};

struct CollisionWorldIdentity {
    std::optional<assets::AssetSourceFingerprint> source_fingerprint;
    std::uint64_t source_revision{0U};

    [[nodiscard]] friend bool operator==(
        const CollisionWorldIdentity&,
        const CollisionWorldIdentity&) = default;
};

// Builder-facing owning construction is public. All retained state is exposed
// only through const views; queries nevertheless validate every traversed
// record so test-only malformed packages fail safely.
class CollisionWorldPackage final {
public:
    CollisionWorldPackage() = default;
    CollisionWorldPackage(
        std::vector<CollisionPlane> planes,
        std::vector<CollisionNode> nodes,
        std::vector<CollisionLeaf> leaves,
        std::vector<CollisionClipnode> clipnodes,
        std::vector<CollisionModel> models,
        CollisionWorldIdentity identity = {},
        CollisionWorldStatistics statistics = {},
        CollisionWorldCompatibilityProfile compatibility_profile =
            CollisionWorldCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1,
        CollisionWorldEvidenceProfile evidence_profile =
            CollisionWorldEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation);

    [[nodiscard]] std::span<const CollisionPlane> planes() const noexcept;
    [[nodiscard]] std::span<const CollisionNode> nodes() const noexcept;
    [[nodiscard]] std::span<const CollisionLeaf> leaves() const noexcept;
    [[nodiscard]] std::span<const CollisionClipnode> clipnodes() const noexcept;
    [[nodiscard]] std::span<const CollisionModel> models() const noexcept;
    [[nodiscard]] const CollisionModel* model(
        std::uint32_t source_model_index) const noexcept;
    [[nodiscard]] const CollisionWorldIdentity& identity() const noexcept;
    [[nodiscard]] const CollisionWorldStatistics& statistics() const noexcept;
    [[nodiscard]] CollisionWorldCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] CollisionWorldEvidenceProfile evidence_profile() const noexcept;

private:
    std::vector<CollisionPlane> planes_;
    std::vector<CollisionNode> nodes_;
    std::vector<CollisionLeaf> leaves_;
    std::vector<CollisionClipnode> clipnodes_;
    std::vector<CollisionModel> models_;
    CollisionWorldIdentity identity_{};
    CollisionWorldStatistics statistics_{};
    CollisionWorldCompatibilityProfile compatibility_profile_{
        CollisionWorldCompatibilityProfile::valve_bsp_v30_clip_hulls_v1};
    CollisionWorldEvidenceProfile evidence_profile_{
        CollisionWorldEvidenceProfile::
            public_valve_bsp_compiler_and_original_map_validation};
};

} // namespace hlclient::collision
