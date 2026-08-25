#include <hlclient/goldsrc/bsp/goldsrc_face_geometry_builder.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::bsp {
namespace {

inline constexpr double kPlanarityTolerance = 0.02;
inline constexpr double kMinimumCollinearDirectionDot = 0.99;
inline constexpr double kDistanceToleranceScale = 32.0;
inline constexpr double kAreaToleranceScale = 64.0;
inline constexpr double kMinimumAreaTolerance = 1.0e-12;
inline constexpr double kMaximumAreaTolerance = 1.0e-4;

struct DoubleVector2 {
    double x{0.0};
    double y{0.0};
};

struct DoubleVector3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

[[nodiscard]] bool supported_profile(
    const GoldSrcFaceOrientationCompatibilityProfile profile) noexcept
{
    return profile == GoldSrcFaceOrientationCompatibilityProfile::
                          valve_qbsp_clockwise_wire_to_counter_clockwise_render;
}

[[nodiscard]] bool finite(const assets::AssetVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const DoubleVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] DoubleVector3 as_double(
    const assets::AssetVector3 value) noexcept
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

[[nodiscard]] DoubleVector3 add(
    const DoubleVector3 left,
    const DoubleVector3 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] DoubleVector3 subtract(
    const DoubleVector3 left,
    const DoubleVector3 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] DoubleVector3 multiply(
    const DoubleVector3 value,
    const double scale) noexcept
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] DoubleVector3 cross(
    const DoubleVector3 left,
    const DoubleVector3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double dot(
    const DoubleVector3 left,
    const DoubleVector3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] double length_squared(const DoubleVector3 value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] double length(const DoubleVector3 value) noexcept
{
    return std::sqrt(length_squared(value));
}

[[nodiscard]] bool identical_position(
    const assets::AssetVector3 left,
    const assets::AssetVector3 right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] GoldSrcFaceGeometryDiagnostic base_diagnostic(
    const GoldSrcFaceGeometryBuildInput& input) noexcept
{
    GoldSrcFaceGeometryDiagnostic diagnostic;
    diagnostic.source_model_index = input.source_model_index;
    diagnostic.source_face_ordinal = input.source_face_ordinal;
    diagnostic.plane_index = input.face.plane_index;
    diagnostic.face_side = input.face.side;
    diagnostic.first_surfedge = input.face.first_surfedge;
    diagnostic.surfedge_count = input.face.surfedge_count > 0
        ? static_cast<std::uint32_t>(input.face.surfedge_count)
        : 0U;
    diagnostic.compatibility_profile = input.compatibility_profile;
    return diagnostic;
}

[[nodiscard]] GoldSrcFaceGeometryBuildResult failure(
    const GoldSrcFaceGeometryErrorCode code,
    GoldSrcFaceGeometryDiagnostic diagnostic,
    const std::size_t pair_test_count = 0U)
{
    diagnostic.failure_classification = code;
    return {
        std::nullopt,
        GoldSrcFaceGeometryError{code, std::move(diagnostic)},
        pair_test_count,
    };
}

[[nodiscard]] assets::WorldBounds calculate_bounds(
    const std::span<const GoldSrcFaceGeometryCorner> corners) noexcept
{
    assets::WorldBounds bounds{corners.front().position, corners.front().position};
    for (const auto& corner : corners.subspan(1U)) {
        bounds.minimum.x = std::min(bounds.minimum.x, corner.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, corner.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, corner.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, corner.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, corner.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, corner.position.z);
    }
    return bounds;
}

[[nodiscard]] DoubleVector3 calculate_area_vector(
    const std::span<const GoldSrcFaceGeometryCorner> corners) noexcept
{
    DoubleVector3 centroid{};
    for (const auto& corner : corners) {
        centroid = add(centroid, as_double(corner.position));
    }
    centroid = multiply(centroid, 1.0 / static_cast<double>(corners.size()));

    DoubleVector3 area{};
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const auto next = (index + 1U) % corners.size();
        const auto first = subtract(as_double(corners[index].position), centroid);
        const auto second = subtract(as_double(corners[next].position), centroid);
        area = add(area, cross(first, second));
    }
    return area;
}

[[nodiscard]] double maximum_absolute_coordinate(
    const std::span<const GoldSrcFaceGeometryCorner> corners) noexcept
{
    double maximum = 0.0;
    for (const auto& corner : corners) {
        maximum = std::max(maximum,
            std::abs(static_cast<double>(corner.position.x)));
        maximum = std::max(maximum,
            std::abs(static_cast<double>(corner.position.y)));
        maximum = std::max(maximum,
            std::abs(static_cast<double>(corner.position.z)));
    }
    return maximum;
}

[[nodiscard]] double distance_tolerance(
    const std::span<const GoldSrcFaceGeometryCorner> corners) noexcept
{
    const auto minimum = kDistanceToleranceScale *
        static_cast<double>(std::numeric_limits<float>::epsilon());
    const auto scaled = minimum *
        std::max(1.0, maximum_absolute_coordinate(corners));
    return std::clamp(scaled, minimum, 0.01);
}

[[nodiscard]] double polygon_extent(const assets::WorldBounds& bounds) noexcept
{
    const auto x = static_cast<double>(bounds.maximum.x) -
        static_cast<double>(bounds.minimum.x);
    const auto y = static_cast<double>(bounds.maximum.y) -
        static_cast<double>(bounds.minimum.y);
    const auto z = static_cast<double>(bounds.maximum.z) -
        static_cast<double>(bounds.minimum.z);
    return std::max({x, y, z});
}

[[nodiscard]] double area_tolerance(const assets::WorldBounds& bounds) noexcept
{
    const auto extent = polygon_extent(bounds);
    const auto extent_squared = extent * extent;
    const auto scaled = kAreaToleranceScale *
        std::numeric_limits<double>::epsilon() *
        std::max(1.0, extent_squared);
    return std::clamp(
        scaled, kMinimumAreaTolerance, kMaximumAreaTolerance);
}

[[nodiscard]] double triangle_winding_tolerance(
    const assets::AssetVector3 first,
    const assets::AssetVector3 second,
    const assets::AssetVector3 third) noexcept
{
    assets::WorldBounds bounds{first, first};
    const auto expand = [&bounds](const assets::AssetVector3 point) {
        bounds.minimum.x = std::min(bounds.minimum.x, point.x);
        bounds.minimum.y = std::min(bounds.minimum.y, point.y);
        bounds.minimum.z = std::min(bounds.minimum.z, point.z);
        bounds.maximum.x = std::max(bounds.maximum.x, point.x);
        bounds.maximum.y = std::max(bounds.maximum.y, point.y);
        bounds.maximum.z = std::max(bounds.maximum.z, point.z);
    };
    expand(second);
    expand(third);
    return area_tolerance(bounds);
}

[[nodiscard]] DoubleVector2 project(
    const assets::AssetVector3 position,
    const std::size_t dropped_axis) noexcept
{
    switch (dropped_axis) {
    case 0U:
        return {static_cast<double>(position.y), static_cast<double>(position.z)};
    case 1U:
        return {static_cast<double>(position.x), static_cast<double>(position.z)};
    default:
        return {static_cast<double>(position.x), static_cast<double>(position.y)};
    }
}

[[nodiscard]] double orient_2d(
    const DoubleVector2 first,
    const DoubleVector2 second,
    const DoubleVector2 point) noexcept
{
    return (second.x - first.x) * (point.y - first.y) -
        (second.y - first.y) * (point.x - first.x);
}

[[nodiscard]] bool point_on_segment(
    const DoubleVector2 point,
    const DoubleVector2 first,
    const DoubleVector2 second,
    const double distance_epsilon,
    const double area_epsilon) noexcept
{
    if (std::abs(orient_2d(first, second, point)) > area_epsilon) {
        return false;
    }
    return point.x >= std::min(first.x, second.x) - distance_epsilon &&
        point.x <= std::max(first.x, second.x) + distance_epsilon &&
        point.y >= std::min(first.y, second.y) - distance_epsilon &&
        point.y <= std::max(first.y, second.y) + distance_epsilon;
}

[[nodiscard]] bool segments_intersect(
    const DoubleVector2 first_start,
    const DoubleVector2 first_end,
    const DoubleVector2 second_start,
    const DoubleVector2 second_end,
    const double distance_epsilon,
    const double area_epsilon) noexcept
{
    const auto first_start_side = orient_2d(first_start, first_end, second_start);
    const auto first_end_side = orient_2d(first_start, first_end, second_end);
    const auto second_start_side = orient_2d(second_start, second_end, first_start);
    const auto second_end_side = orient_2d(second_start, second_end, first_end);
    const auto opposite = [area_epsilon](const double left, const double right) {
        return (left > area_epsilon && right < -area_epsilon) ||
            (left < -area_epsilon && right > area_epsilon);
    };
    if (opposite(first_start_side, first_end_side) &&
        opposite(second_start_side, second_end_side)) {
        return true;
    }
    return point_on_segment(
               second_start, first_start, first_end,
               distance_epsilon, area_epsilon) ||
        point_on_segment(
            second_end, first_start, first_end,
            distance_epsilon, area_epsilon) ||
        point_on_segment(
            first_start, second_start, second_end,
            distance_epsilon, area_epsilon) ||
        point_on_segment(
            first_end, second_start, second_end,
            distance_epsilon, area_epsilon);
}

[[nodiscard]] std::size_t dominant_axis(
    const DoubleVector3 normal) noexcept
{
    const auto x = std::abs(normal.x);
    const auto y = std::abs(normal.y);
    const auto z = std::abs(normal.z);
    if (x >= y && x >= z) {
        return 0U;
    }
    return y >= z ? 1U : 2U;
}

struct IntersectionResult {
    bool intersects{false};
    bool limit_exceeded{false};
    std::size_t pair_test_count{0U};
};

[[nodiscard]] IntersectionResult find_self_intersection(
    const std::span<const GoldSrcFaceGeometryCorner> corners,
    const DoubleVector3 normal,
    const double distance_epsilon,
    const double area_epsilon,
    const std::size_t maximum_pair_tests) noexcept
{
    const auto dropped_axis = dominant_axis(normal);
    IntersectionResult result;
    for (std::size_t first_edge = 0U;
         first_edge < corners.size();
         ++first_edge) {
        const auto first_next = (first_edge + 1U) % corners.size();
        for (std::size_t second_edge = first_edge + 1U;
             second_edge < corners.size();
             ++second_edge) {
            const auto second_next = (second_edge + 1U) % corners.size();
            if (first_next == second_edge || second_next == first_edge) {
                continue;
            }
            if (result.pair_test_count >= maximum_pair_tests) {
                result.limit_exceeded = true;
                return result;
            }
            ++result.pair_test_count;
            if (segments_intersect(
                    project(corners[first_edge].position, dropped_axis),
                    project(corners[first_next].position, dropped_axis),
                    project(corners[second_edge].position, dropped_axis),
                    project(corners[second_next].position, dropped_axis),
                    distance_epsilon,
                    area_epsilon)) {
                result.intersects = true;
                return result;
            }
        }
    }
    return result;
}

[[nodiscard]] bool removable_collinear_corner(
    const GoldSrcFaceGeometryCorner& first,
    const GoldSrcFaceGeometryCorner& middle,
    const GoldSrcFaceGeometryCorner& last,
    const double tolerance) noexcept
{
    const auto a = as_double(first.position);
    const auto b = as_double(middle.position);
    const auto c = as_double(last.position);
    const auto ab = subtract(b, a);
    const auto bc = subtract(c, b);
    const auto ac = subtract(c, a);
    const auto ab_length = length(ab);
    const auto bc_length = length(bc);
    const auto ac_length = length(ac);
    if (!(ab_length > tolerance) || !(bc_length > tolerance) ||
        !(ac_length > tolerance)) {
        return false;
    }

    const auto line_distance = length(cross(ab, ac)) / ac_length;
    if (!std::isfinite(line_distance) || line_distance > tolerance) {
        return false;
    }
    const auto direction = dot(ab, bc) / (ab_length * bc_length);
    if (!std::isfinite(direction) ||
        direction < kMinimumCollinearDirectionDot) {
        return false;
    }

    const auto parameter = dot(ab, ac) / length_squared(ac);
    const auto endpoint_margin = tolerance / ac_length;
    return endpoint_margin < 0.5 &&
        parameter > endpoint_margin && parameter < 1.0 - endpoint_margin;
}

[[nodiscard]] std::optional<std::size_t> first_removable_corner(
    const std::span<const GoldSrcFaceGeometryCorner> corners,
    const double tolerance) noexcept
{
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const auto previous = index == 0U ? corners.size() - 1U : index - 1U;
        const auto next = (index + 1U) % corners.size();
        if (removable_collinear_corner(
                corners[previous], corners[index], corners[next], tolerance)) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool finite_texinfo(
    const GoldSrcFaceGeometrySourceTexinfo& texinfo) noexcept
{
    return std::ranges::all_of(texinfo.s, [](const float value) {
               return std::isfinite(value);
           }) &&
        std::ranges::all_of(texinfo.t, [](const float value) {
            return std::isfinite(value);
        });
}

[[nodiscard]] std::optional<assets::AssetVector2> texture_coordinate(
    const assets::AssetVector3 position,
    const GoldSrcFaceGeometrySourceTexinfo& texinfo) noexcept
{
    const auto calculate = [position](const std::array<float, 4U>& vector) {
        return static_cast<double>(position.x) * static_cast<double>(vector[0U]) +
            static_cast<double>(position.y) * static_cast<double>(vector[1U]) +
            static_cast<double>(position.z) * static_cast<double>(vector[2U]) +
            static_cast<double>(vector[3U]);
    };
    const auto s = calculate(texinfo.s);
    const auto t = calculate(texinfo.t);
    const auto maximum = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(s) || !std::isfinite(t) ||
        std::abs(s) > maximum || std::abs(t) > maximum) {
        return std::nullopt;
    }
    return assets::AssetVector2{static_cast<float>(s), static_cast<float>(t)};
}

[[nodiscard]] GoldSrcFaceGeometryBuildResult build_candidate(
    const GoldSrcFaceGeometryBuildInput& input)
{
    auto diagnostic = base_diagnostic(input);
    if (!valid_goldsrc_face_geometry_limits(input.limits) ||
        !supported_profile(input.compatibility_profile)) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_configuration,
            std::move(diagnostic));
    }
    if (!finite(input.plane.normal) || !std::isfinite(input.plane.distance)) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_plane,
            std::move(diagnostic));
    }
    const auto source_normal = as_double(input.plane.normal);
    const auto source_normal_length = length(source_normal);
    if (!(source_normal_length > std::numeric_limits<double>::epsilon()) ||
        !std::isfinite(source_normal_length) ||
        std::abs(source_normal_length - 1.0) >
            static_cast<double>(kGoldSrcBspPlaneUnitTolerance)) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_plane,
            std::move(diagnostic));
    }
    if (input.face.side != 0 && input.face.side != 1) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_face_side,
            std::move(diagnostic));
    }
    if (!finite_texinfo(input.texinfo)) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_texture_coordinate,
            std::move(diagnostic));
    }
    if (input.face.first_surfedge < 0 || input.face.surfedge_count < 3) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_surfedge_range,
            std::move(diagnostic));
    }
    const auto corner_count = static_cast<std::size_t>(input.face.surfedge_count);
    if (corner_count > input.limits.maximum_face_edges) {
        return failure(
            GoldSrcFaceGeometryErrorCode::geometry_limit_exceeded,
            std::move(diagnostic));
    }
    const auto first_surfedge = static_cast<std::size_t>(
        static_cast<std::uint32_t>(input.face.first_surfedge));
    if (first_surfedge > input.surfedges.size() ||
        corner_count > input.surfedges.size() - first_surfedge) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_surfedge_range,
            std::move(diagnostic));
    }

    // The public input is a view over the parser's already canonicalized plane.
    // Validate that contract above, but do not normalize a second time: doing so
    // changes valid sloped-plane floats and breaks byte-stable M4.1 output.
    const auto normalized_source_normal = source_normal;
    const auto normalized_plane_distance = input.plane.distance;
    const auto emitted_normal = input.face.side == 1
        ? multiply(normalized_source_normal, -1.0)
        : normalized_source_normal;
    diagnostic.emitted_face_normal = {
        static_cast<float>(emitted_normal.x),
        static_cast<float>(emitted_normal.y),
        static_cast<float>(emitted_normal.z),
    };

    std::vector<GoldSrcFaceGeometryCorner> raw_corners;
    std::vector<std::uint32_t> sorted_vertex_indices;
    raw_corners.reserve(corner_count);
    sorted_vertex_indices.reserve(corner_count);
    std::optional<std::uint32_t> first_start;
    std::optional<std::uint32_t> previous_end;
    for (std::size_t corner = 0U; corner < corner_count; ++corner) {
        const auto oriented_edge = input.surfedges[first_surfedge + corner];
        if (oriented_edge == 0 ||
            oriented_edge == std::numeric_limits<std::int32_t>::min()) {
            return failure(
                GoldSrcFaceGeometryErrorCode::invalid_surfedge_reference,
                std::move(diagnostic));
        }
        const auto edge_index = oriented_edge < 0
            ? static_cast<std::size_t>(-static_cast<std::int64_t>(oriented_edge))
            : static_cast<std::size_t>(oriented_edge);
        if (edge_index >= input.edges.size()) {
            return failure(
                GoldSrcFaceGeometryErrorCode::invalid_edge_reference,
                std::move(diagnostic));
        }
        if (oriented_edge < 0) {
            ++diagnostic.negative_surfedge_count;
        } else {
            ++diagnostic.positive_surfedge_count;
        }
        const auto& edge = input.edges[edge_index];
        const auto start = static_cast<std::uint32_t>(
            oriented_edge < 0 ? edge.vertex_indices[1U]
                              : edge.vertex_indices[0U]);
        const auto end = static_cast<std::uint32_t>(
            oriented_edge < 0 ? edge.vertex_indices[0U]
                              : edge.vertex_indices[1U]);
        if (static_cast<std::size_t>(start) >= input.vertices.size() ||
            static_cast<std::size_t>(end) >= input.vertices.size()) {
            return failure(
                GoldSrcFaceGeometryErrorCode::invalid_vertex_reference,
                std::move(diagnostic));
        }
        if (start == end || (previous_end && *previous_end != start)) {
            return failure(
                GoldSrcFaceGeometryErrorCode::broken_face_edge_loop,
                std::move(diagnostic));
        }
        if (!first_start) {
            first_start = start;
        }
        const auto position = input.vertices[static_cast<std::size_t>(start)];
        if (!finite(position) ||
            (!raw_corners.empty() &&
             identical_position(raw_corners.back().position, position))) {
            return failure(
                GoldSrcFaceGeometryErrorCode::broken_face_edge_loop,
                std::move(diagnostic));
        }
        const auto coordinate = texture_coordinate(position, input.texinfo);
        if (!coordinate) {
            return failure(
                GoldSrcFaceGeometryErrorCode::invalid_texture_coordinate,
                std::move(diagnostic));
        }
        raw_corners.push_back(
            GoldSrcFaceGeometryCorner{start, position, *coordinate});
        sorted_vertex_indices.push_back(start);
        previous_end = end;
    }
    diagnostic.reconstructed_corner_count =
        static_cast<std::uint32_t>(raw_corners.size());
    if (!first_start || !previous_end || *previous_end != *first_start ||
        identical_position(raw_corners.back().position, raw_corners.front().position)) {
        return failure(
            GoldSrcFaceGeometryErrorCode::broken_face_edge_loop,
            std::move(diagnostic));
    }

    std::ranges::sort(sorted_vertex_indices);
    const auto unique_end = std::ranges::unique(sorted_vertex_indices).begin();
    diagnostic.unique_vertex_count = static_cast<std::uint32_t>(
        unique_end - sorted_vertex_indices.begin());
    if (unique_end != sorted_vertex_indices.end()) {
        return failure(
            GoldSrcFaceGeometryErrorCode::duplicate_face_vertex,
            std::move(diagnostic));
    }

    diagnostic.polygon_bounds = calculate_bounds(raw_corners);
    diagnostic.distance_tolerance = distance_tolerance(raw_corners);
    diagnostic.polygon_area_tolerance =
        area_tolerance(diagnostic.polygon_bounds);
    if (!std::isfinite(diagnostic.distance_tolerance) ||
        !std::isfinite(diagnostic.polygon_area_tolerance)) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_configuration,
            std::move(diagnostic));
    }

    for (const auto& corner : raw_corners) {
        const auto point = as_double(corner.position);
        const auto deviation = std::abs(
            dot(point, normalized_source_normal) - normalized_plane_distance);
        diagnostic.maximum_planarity_deviation = std::max(
            diagnostic.maximum_planarity_deviation, deviation);
        if (!std::isfinite(deviation) || deviation > kPlanarityTolerance) {
            return failure(
                GoldSrcFaceGeometryErrorCode::nonplanar_face,
                std::move(diagnostic));
        }
    }

    const auto intersection = find_self_intersection(
        raw_corners,
        emitted_normal,
        diagnostic.distance_tolerance,
        diagnostic.polygon_area_tolerance,
        input.limits.maximum_polygon_edge_pair_tests);
    if (intersection.limit_exceeded) {
        return failure(
            GoldSrcFaceGeometryErrorCode::geometry_limit_exceeded,
            std::move(diagnostic),
            intersection.pair_test_count);
    }
    if (intersection.intersects) {
        return failure(
            GoldSrcFaceGeometryErrorCode::self_intersecting_face,
            std::move(diagnostic),
            intersection.pair_test_count);
    }

    const auto raw_area = calculate_area_vector(raw_corners);
    const auto raw_area_magnitude = length(raw_area);
    const auto raw_signed_area = dot(raw_area, emitted_normal);
    diagnostic.area_vector_magnitude = raw_area_magnitude;
    diagnostic.signed_area_normal_dot = raw_signed_area;
    if (raw_area_magnitude > 0.0 && std::isfinite(raw_area_magnitude)) {
        const auto direction = multiply(raw_area, 1.0 / raw_area_magnitude);
        diagnostic.polygon_area_vector_direction = {
            static_cast<float>(direction.x),
            static_cast<float>(direction.y),
            static_cast<float>(direction.z),
        };
    }
    if (!finite(raw_area) || !std::isfinite(raw_area_magnitude) ||
        !std::isfinite(raw_signed_area) ||
        raw_area_magnitude <= diagnostic.polygon_area_tolerance ||
        std::abs(raw_signed_area) <=
            diagnostic.polygon_area_tolerance) {
        return failure(
            GoldSrcFaceGeometryErrorCode::degenerate_face,
            std::move(diagnostic),
            intersection.pair_test_count);
    }
    // Public Valve qbsp evidence establishes that the wire loop is clockwise
    // relative to the side-adjusted face normal. The opposite sign is not an
    // alternate accepted encoding: it is malformed for this explicit profile.
    if (raw_signed_area >= -diagnostic.polygon_area_tolerance) {
        return failure(
            GoldSrcFaceGeometryErrorCode::invalid_face_winding,
            std::move(diagnostic),
            intersection.pair_test_count);
    }

    const auto original_corner_count = raw_corners.size();
    for (std::size_t pass = 0U;
         pass < original_corner_count && raw_corners.size() > 3U;
         ++pass) {
        const auto removable = first_removable_corner(
            raw_corners, diagnostic.distance_tolerance);
        if (!removable) {
            break;
        }
        raw_corners.erase(
            raw_corners.begin() + static_cast<std::ptrdiff_t>(*removable));
        ++diagnostic.removed_collinear_corner_count;
    }
    if (raw_corners.size() < 3U) {
        return failure(
            GoldSrcFaceGeometryErrorCode::collinear_canonicalization_failed,
            std::move(diagnostic),
            intersection.pair_test_count);
    }

    const auto cleaned_raw_area = calculate_area_vector(raw_corners);
    const auto cleaned_raw_signed_area = dot(cleaned_raw_area, emitted_normal);
    if (!finite(cleaned_raw_area) || !std::isfinite(cleaned_raw_signed_area) ||
        cleaned_raw_signed_area >=
            -diagnostic.polygon_area_tolerance) {
        return failure(
            GoldSrcFaceGeometryErrorCode::collinear_canonicalization_failed,
            std::move(diagnostic),
            intersection.pair_test_count);
    }

    std::vector<GoldSrcFaceGeometryCorner> canonical_corners;
    canonical_corners.reserve(raw_corners.size());
    canonical_corners.push_back(raw_corners.front());
    for (std::size_t index = raw_corners.size(); index > 1U; --index) {
        canonical_corners.push_back(raw_corners[index - 1U]);
    }
    diagnostic.canonical_corner_count =
        static_cast<std::uint32_t>(canonical_corners.size());

    const auto canonical_area = calculate_area_vector(canonical_corners);
    const auto canonical_signed_area = dot(canonical_area, emitted_normal);
    if (!finite(canonical_area) || !std::isfinite(canonical_signed_area) ||
        canonical_signed_area <= diagnostic.polygon_area_tolerance) {
        return failure(
            GoldSrcFaceGeometryErrorCode::collinear_canonicalization_failed,
            std::move(diagnostic),
            intersection.pair_test_count);
    }

    for (std::size_t corner = 0U;
         corner < canonical_corners.size();
         ++corner) {
        const auto& first = canonical_corners[corner].position;
        const auto& second = canonical_corners[
            (corner + 1U) % canonical_corners.size()].position;
        const auto& third = canonical_corners[
            (corner + 2U) % canonical_corners.size()].position;
        const auto first_edge = subtract(as_double(second), as_double(first));
        const auto second_edge = subtract(as_double(third), as_double(second));
        const auto turn = dot(cross(first_edge, second_edge), emitted_normal);
        const auto turn_tolerance = triangle_winding_tolerance(
            first, second, third);
        diagnostic.maximum_triangle_winding_tolerance = std::max(
            diagnostic.maximum_triangle_winding_tolerance,
            turn_tolerance);
        if (!std::isfinite(turn)) {
            return failure(
                GoldSrcFaceGeometryErrorCode::degenerate_face,
                std::move(diagnostic),
                intersection.pair_test_count);
        }
        if (turn < -turn_tolerance) {
            return failure(
                GoldSrcFaceGeometryErrorCode::concave_face,
                std::move(diagnostic),
                intersection.pair_test_count);
        }
        if (turn <= turn_tolerance) {
            return failure(
                GoldSrcFaceGeometryErrorCode::
                    collinear_canonicalization_failed,
                std::move(diagnostic),
                intersection.pair_test_count);
        }
    }

    std::vector<std::uint32_t> triangle_indices;
    triangle_indices.reserve((canonical_corners.size() - 2U) * 3U);
    auto minimum_winding = std::numeric_limits<double>::infinity();
    auto maximum_winding = -std::numeric_limits<double>::infinity();
    const auto first = as_double(canonical_corners.front().position);
    for (std::size_t triangle = 0U;
         triangle < canonical_corners.size() - 2U;
         ++triangle) {
        const auto second = as_double(canonical_corners[triangle + 1U].position);
        const auto third = as_double(canonical_corners[triangle + 2U].position);
        const auto winding = dot(
            cross(subtract(second, first), subtract(third, first)),
            emitted_normal);
        const auto winding_tolerance = triangle_winding_tolerance(
            canonical_corners.front().position,
            canonical_corners[triangle + 1U].position,
            canonical_corners[triangle + 2U].position);
        diagnostic.maximum_triangle_winding_tolerance = std::max(
            diagnostic.maximum_triangle_winding_tolerance,
            winding_tolerance);
        minimum_winding = std::min(minimum_winding, winding);
        maximum_winding = std::max(maximum_winding, winding);
        diagnostic.minimum_triangle_winding_dot = minimum_winding;
        diagnostic.maximum_triangle_winding_dot = maximum_winding;
        if (!std::isfinite(winding) || winding <= winding_tolerance) {
            return failure(
                winding < -winding_tolerance
                    ? GoldSrcFaceGeometryErrorCode::invalid_face_winding
                    : GoldSrcFaceGeometryErrorCode::degenerate_face,
                std::move(diagnostic),
                intersection.pair_test_count);
        }
        triangle_indices.push_back(0U);
        triangle_indices.push_back(static_cast<std::uint32_t>(triangle + 1U));
        triangle_indices.push_back(static_cast<std::uint32_t>(triangle + 2U));
    }

    diagnostic.failure_classification = GoldSrcFaceGeometryErrorCode::none;
    GoldSrcFaceGeometryCandidate candidate;
    candidate.corners = std::move(canonical_corners);
    candidate.local_triangle_indices = std::move(triangle_indices);
    candidate.emitted_face_normal = diagnostic.emitted_face_normal;
    candidate.bounds = calculate_bounds(candidate.corners);
    candidate.compatibility_profile = input.compatibility_profile;
    candidate.diagnostic = diagnostic;
    return {
        std::move(candidate),
        std::nullopt,
        intersection.pair_test_count,
    };
}

} // namespace

std::string_view to_string(
    const GoldSrcFaceOrientationCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcFaceOrientationCompatibilityProfile::
        valve_qbsp_clockwise_wire_to_counter_clockwise_render:
        return "valve_qbsp_clockwise_wire_to_counter_clockwise_render";
    }
    return "unknown";
}

bool valid_goldsrc_face_geometry_limits(
    const GoldSrcFaceGeometryLimits& limits) noexcept
{
    return limits.maximum_face_edges >= 3U &&
        limits.maximum_face_edges <= kGoldSrcBspHardMaximumFaceEdges &&
        limits.maximum_polygon_edge_pair_tests <=
            kGoldSrcBspHardMaximumPolygonEdgePairTests;
}

std::string_view to_string(const GoldSrcFaceGeometryErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcFaceGeometryErrorCode::none: return "none";
    case GoldSrcFaceGeometryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcFaceGeometryErrorCode::invalid_plane: return "invalid_plane";
    case GoldSrcFaceGeometryErrorCode::invalid_face_side:
        return "invalid_face_side";
    case GoldSrcFaceGeometryErrorCode::invalid_surfedge_range:
        return "invalid_surfedge_range";
    case GoldSrcFaceGeometryErrorCode::invalid_surfedge_reference:
        return "invalid_surfedge_reference";
    case GoldSrcFaceGeometryErrorCode::invalid_edge_reference:
        return "invalid_edge_reference";
    case GoldSrcFaceGeometryErrorCode::invalid_vertex_reference:
        return "invalid_vertex_reference";
    case GoldSrcFaceGeometryErrorCode::broken_face_edge_loop:
        return "broken_face_edge_loop";
    case GoldSrcFaceGeometryErrorCode::duplicate_face_vertex:
        return "duplicate_face_vertex";
    case GoldSrcFaceGeometryErrorCode::nonplanar_face:
        return "nonplanar_face";
    case GoldSrcFaceGeometryErrorCode::self_intersecting_face:
        return "self_intersecting_face";
    case GoldSrcFaceGeometryErrorCode::degenerate_face:
        return "degenerate_face";
    case GoldSrcFaceGeometryErrorCode::invalid_face_winding:
        return "invalid_face_winding";
    case GoldSrcFaceGeometryErrorCode::concave_face: return "concave_face";
    case GoldSrcFaceGeometryErrorCode::collinear_canonicalization_failed:
        return "collinear_canonicalization_failed";
    case GoldSrcFaceGeometryErrorCode::invalid_texture_coordinate:
        return "invalid_texture_coordinate";
    case GoldSrcFaceGeometryErrorCode::geometry_limit_exceeded:
        return "geometry_limit_exceeded";
    case GoldSrcFaceGeometryErrorCode::unable_to_retain_candidate:
        return "unable_to_retain_candidate";
    }
    return "unknown";
}

GoldSrcFaceGeometryBuildResult GoldSrcFaceGeometryBuilder::build(
    const GoldSrcFaceGeometryBuildInput& input)
{
    try {
        return build_candidate(input);
    } catch (const std::bad_alloc&) {
        return failure(
            GoldSrcFaceGeometryErrorCode::unable_to_retain_candidate,
            base_diagnostic(input));
    } catch (...) {
        return failure(
            GoldSrcFaceGeometryErrorCode::unable_to_retain_candidate,
            base_diagnostic(input));
    }
}

} // namespace hlclient::goldsrc::bsp
