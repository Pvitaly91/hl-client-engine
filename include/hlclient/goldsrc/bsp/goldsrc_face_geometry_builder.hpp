#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::bsp {

enum class GoldSrcFaceOrientationCompatibilityProfile : std::uint8_t {
    valve_qbsp_clockwise_wire_to_counter_clockwise_render,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcFaceOrientationCompatibilityProfile profile) noexcept;

// Project-owned views over records that the canonical BSP parser has already
// decoded. They deliberately contain no wire pointers, ABI-dependent SDK
// structures, paths, texture names, or source bytes.
struct GoldSrcFaceGeometrySourcePlane {
    assets::AssetVector3 normal{};
    double distance{0.0};
};

struct GoldSrcFaceGeometrySourceEdge {
    std::array<std::uint16_t, 2U> vertex_indices{};
};

struct GoldSrcFaceGeometrySourceFace {
    std::uint32_t plane_index{0U};
    std::int16_t side{0};
    std::int32_t first_surfedge{0};
    std::int16_t surfedge_count{0};
};

struct GoldSrcFaceGeometrySourceTexinfo {
    std::array<float, 4U> s{};
    std::array<float, 4U> t{};
};

struct GoldSrcFaceGeometryLimits {
    std::size_t maximum_face_edges{4'096U};
    // This is a per-call remaining work budget. The result reports the exact
    // amount consumed so the parser can enforce its aggregate document limit.
    std::size_t maximum_polygon_edge_pair_tests{16'777'216U};
};

[[nodiscard]] bool valid_goldsrc_face_geometry_limits(
    const GoldSrcFaceGeometryLimits& limits) noexcept;

enum class GoldSrcFaceGeometryErrorCode : std::uint8_t {
    none,
    invalid_configuration,
    invalid_plane,
    invalid_face_side,
    invalid_surfedge_range,
    invalid_surfedge_reference,
    invalid_edge_reference,
    invalid_vertex_reference,
    broken_face_edge_loop,
    duplicate_face_vertex,
    nonplanar_face,
    self_intersecting_face,
    degenerate_face,
    invalid_face_winding,
    concave_face,
    collinear_canonicalization_failed,
    invalid_texture_coordinate,
    geometry_limit_exceeded,
    unable_to_retain_candidate,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcFaceGeometryErrorCode code) noexcept;

// Fixed-size, bounded metadata for one face. It never retains native paths,
// material names, raw arrays, source bytes, or complete source geometry.
struct GoldSrcFaceGeometryDiagnostic {
    std::uint32_t source_model_index{0U};
    std::uint32_t source_face_ordinal{0U};
    std::uint32_t plane_index{0U};
    std::int16_t face_side{0};
    std::int32_t first_surfedge{0};
    std::uint32_t surfedge_count{0U};
    std::uint32_t reconstructed_corner_count{0U};
    std::uint32_t canonical_corner_count{0U};
    std::uint32_t unique_vertex_count{0U};
    std::uint32_t negative_surfedge_count{0U};
    std::uint32_t positive_surfedge_count{0U};
    std::uint32_t removed_collinear_corner_count{0U};
    assets::AssetVector3 emitted_face_normal{};
    assets::AssetVector3 polygon_area_vector_direction{};
    double area_vector_magnitude{0.0};
    double signed_area_normal_dot{0.0};
    double minimum_triangle_winding_dot{0.0};
    double maximum_triangle_winding_dot{0.0};
    double maximum_planarity_deviation{0.0};
    double distance_tolerance{0.0};
    // Whole-polygon orientation tolerance. Individual triangle winding uses
    // the independently derived maximum below.
    double polygon_area_tolerance{0.0};
    double maximum_triangle_winding_tolerance{0.0};
    assets::WorldBounds polygon_bounds{};
    GoldSrcFaceOrientationCompatibilityProfile compatibility_profile{
        GoldSrcFaceOrientationCompatibilityProfile::
            valve_qbsp_clockwise_wire_to_counter_clockwise_render};
    GoldSrcFaceGeometryErrorCode failure_classification{
        GoldSrcFaceGeometryErrorCode::none};
};

struct GoldSrcFaceGeometryCorner {
    std::uint32_t source_vertex_index{0U};
    assets::AssetVector3 position{};
    // Raw affine texel-space coordinates retained in the same canonical
    // corner order as position. No texture-size normalization is performed.
    assets::AssetVector2 texture_coordinate{};
};

struct GoldSrcFaceGeometryCandidate {
    std::vector<GoldSrcFaceGeometryCorner> corners;
    // Local indices into corners. Every three consecutive values form one
    // counter-clockwise triangle relative to emitted_face_normal.
    std::vector<std::uint32_t> local_triangle_indices;
    assets::AssetVector3 emitted_face_normal{};
    assets::WorldBounds bounds{};
    GoldSrcFaceOrientationCompatibilityProfile compatibility_profile{
        GoldSrcFaceOrientationCompatibilityProfile::
            valve_qbsp_clockwise_wire_to_counter_clockwise_render};
    GoldSrcFaceGeometryDiagnostic diagnostic{};
};

struct GoldSrcFaceGeometryBuildInput {
    std::uint32_t source_model_index{0U};
    std::uint32_t source_face_ordinal{0U};
    GoldSrcFaceGeometrySourcePlane plane{};
    GoldSrcFaceGeometrySourceFace face{};
    std::span<const GoldSrcFaceGeometrySourceEdge> edges;
    std::span<const std::int32_t> surfedges;
    std::span<const assets::AssetVector3> vertices;
    GoldSrcFaceGeometrySourceTexinfo texinfo{};
    GoldSrcFaceGeometryLimits limits{};
    GoldSrcFaceOrientationCompatibilityProfile compatibility_profile{
        GoldSrcFaceOrientationCompatibilityProfile::
            valve_qbsp_clockwise_wire_to_counter_clockwise_render};
};

struct GoldSrcFaceGeometryError {
    GoldSrcFaceGeometryErrorCode code{
        GoldSrcFaceGeometryErrorCode::invalid_configuration};
    GoldSrcFaceGeometryDiagnostic diagnostic{};
};

struct GoldSrcFaceGeometryBuildResult {
    std::optional<GoldSrcFaceGeometryCandidate> candidate;
    std::optional<GoldSrcFaceGeometryError> error;
    std::size_t polygon_edge_pair_test_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return candidate.has_value();
    }
};

class GoldSrcFaceGeometryBuilder final {
public:
    [[nodiscard]] static GoldSrcFaceGeometryBuildResult build(
        const GoldSrcFaceGeometryBuildInput& input);
};

} // namespace hlclient::goldsrc::bsp
