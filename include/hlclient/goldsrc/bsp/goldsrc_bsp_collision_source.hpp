#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace hlclient::goldsrc::bsp {

inline constexpr std::size_t kGoldSrcBspDefaultMaximumCollisionValidationSteps = 262'144U;
inline constexpr std::size_t kGoldSrcBspHardMaximumCollisionValidationSteps = 1'048'576U;

enum class GoldSrcBspCollisionCompatibilityProfile {
    valve_bsp_v30_clip_hulls_v1,
};

enum class GoldSrcBspCollisionEvidenceProfile {
    public_valve_bsp_compiler_and_original_map_validation,
};

enum class GoldSrcBspCollisionTreeDomain {
    node_leaf,
    clipnode_contents,
};

enum class GoldSrcBspCollisionHullOrdinal : std::uint8_t {
    point = 0U,
    standing_32x32x72 = 1U,
    large_64_cube = 2U,
    duck_32x32x36 = 3U,
};

struct GoldSrcContentsCode {
    std::int32_t value{-1};

    [[nodiscard]] friend constexpr bool operator==(const GoldSrcContentsCode&,
                                                   const GoldSrcContentsCode&) = default;
};

struct GoldSrcBspCollisionSourceNodeReference {
    std::uint32_t source_node_index{0U};

    [[nodiscard]] friend constexpr bool operator==(const GoldSrcBspCollisionSourceNodeReference&,
                                                   const GoldSrcBspCollisionSourceNodeReference&) =
        default;
};

struct GoldSrcBspCollisionSourceLeafReference {
    std::uint32_t source_leaf_index{0U};

    [[nodiscard]] friend constexpr bool operator==(const GoldSrcBspCollisionSourceLeafReference&,
                                                   const GoldSrcBspCollisionSourceLeafReference&) =
        default;
};

struct GoldSrcBspCollisionSourceClipnodeReference {
    std::uint32_t source_clipnode_index{0U};

    [[nodiscard]] friend constexpr bool operator==(
        const GoldSrcBspCollisionSourceClipnodeReference&,
        const GoldSrcBspCollisionSourceClipnodeReference&) = default;
};

using GoldSrcBspCollisionSourceNodeChild =
    std::variant<GoldSrcBspCollisionSourceNodeReference, GoldSrcBspCollisionSourceLeafReference>;

using GoldSrcBspCollisionSourceClipnodeChild =
    std::variant<GoldSrcBspCollisionSourceClipnodeReference, GoldSrcContentsCode>;

using GoldSrcBspCollisionSourceClipHullRoot =
    std::variant<GoldSrcBspCollisionSourceClipnodeReference, GoldSrcContentsCode>;

struct GoldSrcBspCollisionHullExtents {
    assets::AssetVector3 minimum{};
    assets::AssetVector3 maximum{};
};

inline constexpr GoldSrcBspCollisionHullExtents kGoldSrcBspPointHullExtents{
    {0.0F, 0.0F, 0.0F},
    {0.0F, 0.0F, 0.0F},
};
inline constexpr GoldSrcBspCollisionHullExtents kGoldSrcBspStandingHullExtents{
    {-16.0F, -16.0F, -36.0F},
    {16.0F, 16.0F, 36.0F},
};
inline constexpr GoldSrcBspCollisionHullExtents kGoldSrcBspLargeHullExtents{
    {-32.0F, -32.0F, -32.0F},
    {32.0F, 32.0F, 32.0F},
};
inline constexpr GoldSrcBspCollisionHullExtents kGoldSrcBspDuckHullExtents{
    {-16.0F, -16.0F, -18.0F},
    {16.0F, 16.0F, 18.0F},
};

struct GoldSrcBspCollisionSourcePlane {
    std::uint32_t source_plane_index{0U};
    assets::AssetVector3 normal{};
    double distance{0.0};
    std::int32_t source_type{0};
};

struct GoldSrcBspCollisionSourceNode {
    std::uint32_t source_node_index{0U};
    std::uint32_t source_plane_index{0U};
    std::array<GoldSrcBspCollisionSourceNodeChild, 2U> children{};
    assets::WorldBounds bounds{};
    std::uint32_t first_source_face{0U};
    std::uint32_t source_face_count{0U};
};

struct GoldSrcBspCollisionSourceLeaf {
    std::uint32_t source_leaf_index{0U};
    GoldSrcContentsCode contents{};
    assets::WorldBounds bounds{};
};

struct GoldSrcBspCollisionSourceClipnode {
    std::uint32_t source_clipnode_index{0U};
    std::uint32_t source_plane_index{0U};
    std::array<GoldSrcBspCollisionSourceClipnodeChild, 2U> children{};
};

struct GoldSrcBspCollisionSourcePointHull {
    GoldSrcBspCollisionHullOrdinal ordinal{GoldSrcBspCollisionHullOrdinal::point};
    GoldSrcBspCollisionTreeDomain tree_domain{GoldSrcBspCollisionTreeDomain::node_leaf};
    GoldSrcBspCollisionSourceNodeReference root{};
    GoldSrcBspCollisionHullExtents extents{kGoldSrcBspPointHullExtents};
    GoldSrcBspCollisionCompatibilityProfile compatibility_profile{
        GoldSrcBspCollisionCompatibilityProfile::valve_bsp_v30_clip_hulls_v1};
    GoldSrcBspCollisionEvidenceProfile evidence_profile{
        GoldSrcBspCollisionEvidenceProfile::public_valve_bsp_compiler_and_original_map_validation};
};

struct GoldSrcBspCollisionSourceClipHull {
    GoldSrcBspCollisionHullOrdinal ordinal{GoldSrcBspCollisionHullOrdinal::standing_32x32x72};
    GoldSrcBspCollisionTreeDomain tree_domain{GoldSrcBspCollisionTreeDomain::clipnode_contents};
    GoldSrcBspCollisionSourceClipHullRoot root{};
    GoldSrcBspCollisionHullExtents extents{kGoldSrcBspStandingHullExtents};
    GoldSrcBspCollisionCompatibilityProfile compatibility_profile{
        GoldSrcBspCollisionCompatibilityProfile::valve_bsp_v30_clip_hulls_v1};
    GoldSrcBspCollisionEvidenceProfile evidence_profile{
        GoldSrcBspCollisionEvidenceProfile::public_valve_bsp_compiler_and_original_map_validation};
};

struct GoldSrcBspCollisionSourceModel {
    std::uint32_t source_model_index{0U};
    assets::AssetVector3 source_origin{};
    assets::WorldBounds source_bounds{};
    GoldSrcBspCollisionSourcePointHull point_hull{};
    GoldSrcBspCollisionSourceClipHull standing_hull{};
    GoldSrcBspCollisionSourceClipHull large_hull{};
    GoldSrcBspCollisionSourceClipHull duck_hull{};
    std::uint32_t visible_leaf_count{0U};
    std::uint32_t first_source_face{0U};
    std::uint32_t source_face_count{0U};
};

struct GoldSrcBspCollisionSourceStatistics {
    std::uint64_t plane_count{0U};
    std::uint64_t node_count{0U};
    std::uint64_t leaf_count{0U};
    std::uint64_t clipnode_count{0U};
    std::uint64_t model_count{0U};
    std::uint64_t reachable_hull0_nodes{0U};
    std::uint64_t unreachable_hull0_nodes{0U};
    std::uint64_t reachable_clipnodes{0U};
    std::uint64_t unreachable_clipnodes{0U};
    // Terminal counts are reference occurrences: one per retained leaf, one
    // per negative clipnode child, and one per direct clip-hull root.
    std::uint64_t terminal_empty_count{0U};
    std::uint64_t terminal_solid_count{0U};
    std::uint64_t terminal_liquid_count{0U};
    std::uint64_t terminal_special_count{0U};
    std::uint64_t model_hull_root_count{0U};
    std::uint64_t direct_terminal_root_count{0U};
    std::uint64_t maximum_tree_depth{0U};
    std::uint64_t validation_step_count{0U};
};

struct GoldSrcBspCollisionSource {
    std::vector<GoldSrcBspCollisionSourcePlane> planes;
    std::vector<GoldSrcBspCollisionSourceNode> nodes;
    std::vector<GoldSrcBspCollisionSourceLeaf> leaves;
    std::vector<GoldSrcBspCollisionSourceClipnode> clipnodes;
    std::vector<GoldSrcBspCollisionSourceModel> models;
    assets::AssetSourceFingerprint source_fingerprint{};
    std::int32_t source_bsp_version{kGoldSrcBspVersion};
    GoldSrcBspCollisionCompatibilityProfile compatibility_profile{
        GoldSrcBspCollisionCompatibilityProfile::valve_bsp_v30_clip_hulls_v1};
    GoldSrcBspCollisionEvidenceProfile evidence_profile{
        GoldSrcBspCollisionEvidenceProfile::public_valve_bsp_compiler_and_original_map_validation};
    GoldSrcBspCollisionSourceStatistics statistics{};
};

} // namespace hlclient::goldsrc::bsp
