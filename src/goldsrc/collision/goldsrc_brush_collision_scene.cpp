#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc::collision {
namespace {

namespace core_collision = hlclient::collision;
namespace brush = hlclient::goldsrc::brush_models;

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool zero_vector(const assets::AssetVector3& value) noexcept
{
    return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

[[nodiscard]] bool same_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool same_bounds(
    const assets::WorldBounds& left,
    const assets::WorldBounds& right) noexcept
{
    return same_vector(left.minimum, right.minimum) &&
        same_vector(left.maximum, right.maximum);
}

[[nodiscard]] bool same_hull_root(
    const core_collision::CollisionHullRoot& left,
    const core_collision::CollisionHullRoot& right) noexcept
{
    return left.kind == right.kind && left.index == right.index &&
        left.terminal == right.terminal;
}

[[nodiscard]] bool same_world_identity(
    const core_collision::CollisionWorldIdentity& left,
    const core_collision::CollisionWorldIdentity& right) noexcept
{
    return left.source_fingerprint == right.source_fingerprint &&
        left.source_revision == right.source_revision;
}

[[nodiscard]] bool supported_role(const BrushCollisionRole role) noexcept
{
    switch (role) {
    case BrushCollisionRole::solid:
    case BrushCollisionRole::non_solid:
    case BrushCollisionRole::unsupported:
    case BrushCollisionRole::evidence_pending:
        return true;
    }
    return false;
}

[[nodiscard]] bool supported_provider_profile(
    const BrushCollisionRoleProviderProfile profile) noexcept
{
    switch (profile) {
    case BrushCollisionRoleProviderProfile::
        explicit_synthetic_brush_solidity_v1:
    case BrushCollisionRoleProviderProfile::
        stock_brush_solidity_evidence_pending:
        return true;
    }
    return false;
}

[[nodiscard]] bool identity_less(
    const BrushCollisionInstanceIdentity& left,
    const BrushCollisionInstanceIdentity& right) noexcept
{
    if (left.stable_instance_ordinal != right.stable_instance_ordinal) {
        return left.stable_instance_ordinal < right.stable_instance_ordinal;
    }
    if (left.source_model_index != right.source_model_index) {
        return left.source_model_index < right.source_model_index;
    }
    return left.source_entity_index < right.source_entity_index;
}

[[nodiscard]] bool duplicate_stable_identity(
    const BrushCollisionInstanceIdentity& left,
    const BrushCollisionInstanceIdentity& right) noexcept
{
    return left.stable_instance_ordinal == right.stable_instance_ordinal &&
        left.source_model_index == right.source_model_index;
}

[[nodiscard]] BrushCollisionModelLibraryBuildResult library_failure(
    const BrushCollisionModelLibraryErrorCode code,
    const std::optional<std::uint32_t> model = std::nullopt) noexcept
{
    return {
        {},
        BrushCollisionModelLibraryError{code, model},
    };
}

[[nodiscard]] bool valid_model_hulls(
    const core_collision::CollisionModel& model) noexcept
{
    for (std::size_t ordinal = 0U; ordinal < model.hulls.size(); ++ordinal) {
        const auto expected = core_collision::collision_hull_ordinal(ordinal);
        const auto expected_profile = expected
            ? core_collision::standard_collision_hull_profile(*expected)
            : std::nullopt;
        const auto expected_domain = ordinal == 0U
            ? core_collision::CollisionHullTreeDomain::node_leaf
            : core_collision::CollisionHullTreeDomain::clipnode;
        const auto root_kind = model.hulls[ordinal].root.kind;
        const bool valid_root = ordinal == 0U
            ? root_kind == core_collision::CollisionHullRootKind::node
            : root_kind == core_collision::CollisionHullRootKind::clipnode ||
                root_kind == core_collision::CollisionHullRootKind::terminal;
        if (!expected || !expected_profile ||
            model.hulls[ordinal].ordinal != *expected ||
            model.hulls[ordinal].domain != expected_domain || !valid_root ||
            !(model.hulls[ordinal].profile == *expected_profile) ||
            model.hull(*expected) == nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ExplicitBrushCollisionTraceQueryResult trace_failure(
    const ExplicitBrushCollisionTraceErrorCode code,
    std::optional<core_collision::CollisionQueryError> query_error =
        std::nullopt) noexcept
{
    return {
        std::nullopt,
        ExplicitBrushCollisionTraceError{code, std::move(query_error)},
    };
}

struct PreparedBroadPhase {
    bool valid{false};
    bool intersects{false};
    assets::WorldBounds expanded_world_bounds{};
    ExplicitBrushCollisionTraceErrorCode error{
        ExplicitBrushCollisionTraceErrorCode::invalid_model};
    std::optional<core_collision::CollisionQueryError> query_error;
};

[[nodiscard]] PreparedBroadPhase broad_phase_failure(
    const ExplicitBrushCollisionTraceErrorCode error,
    const std::optional<core_collision::CollisionQueryErrorCode> query_error,
    const std::uint32_t source_model_index) noexcept
{
    return {
        false,
        false,
        {},
        error,
        query_error
            ? std::optional{core_collision::CollisionQueryError{
                  *query_error,
                  source_model_index,
                  std::nullopt,
                  0U,
              }}
            : std::nullopt,
    };
}

[[nodiscard]] bool valid_package_shape(
    const core_collision::CollisionWorldPackage& package) noexcept
{
    return package.compatibility_profile() ==
            core_collision::CollisionWorldCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1 &&
        package.evidence_profile() ==
            core_collision::CollisionWorldEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation &&
        !package.planes().empty() && !package.nodes().empty() &&
        !package.leaves().empty() && !package.models().empty() &&
        package.planes().size() <=
            core_collision::kCollisionHardMaximumPlanes &&
        package.nodes().size() <=
            core_collision::kCollisionHardMaximumNodes &&
        package.leaves().size() <=
            core_collision::kCollisionHardMaximumLeaves &&
        package.clipnodes().size() <=
            core_collision::kCollisionHardMaximumClipnodes &&
        package.models().size() <=
            core_collision::kCollisionHardMaximumModels;
}

[[nodiscard]] bool coherent_brush_model(
    const BrushCollisionModel& model,
    const core_collision::CollisionModel& retained) noexcept
{
    if (model.source_model_index == 0U ||
        retained.source_model_index != model.source_model_index ||
        !finite_bounds(model.local_bounds) ||
        !finite_vector(model.source_origin) ||
        !same_bounds(model.local_bounds, retained.source_bounds) ||
        !same_vector(model.source_origin, retained.source_origin) ||
        !same_world_identity(
            model.collision_identity, model.collision_world->identity()) ||
        !valid_model_hulls(retained)) {
        return false;
    }
    for (std::size_t hull = 0U; hull < model.hull_roots.size(); ++hull) {
        if (!same_hull_root(model.hull_roots[hull], retained.hulls[hull].root)) {
            return false;
        }
    }
    return true;
}

constexpr double kBroadPhasePlaneUnitTolerance = 1.0e-3;

enum class BroadPhaseProofReferenceKind : std::uint8_t {
    node,
    leaf,
    clipnode,
    terminal,
};

struct BroadPhaseProofReference {
    BroadPhaseProofReferenceKind kind{
        BroadPhaseProofReferenceKind::terminal};
    std::uint32_t index{0U};
    core_collision::CollisionContents terminal{};
};

struct BroadPhaseProofBounds {
    std::array<double, 3U> minimum{
        -(std::numeric_limits<double>::infinity)(),
        -(std::numeric_limits<double>::infinity)(),
        -(std::numeric_limits<double>::infinity)(),
    };
    std::array<double, 3U> maximum{
        (std::numeric_limits<double>::infinity)(),
        (std::numeric_limits<double>::infinity)(),
        (std::numeric_limits<double>::infinity)(),
    };
};

struct BroadPhaseProofFrame {
    BroadPhaseProofReference reference{};
    BroadPhaseProofBounds bounds{};
    bool leave{false};
};

[[nodiscard]] bool valid_proof_plane(
    const core_collision::CollisionPlane& plane) noexcept
{
    if (!finite_vector(plane.normal) || !std::isfinite(plane.distance) ||
        plane.source_type < 0 || plane.source_type > 5) {
        return false;
    }
    const auto length_squared =
        static_cast<double>(plane.normal.x) * plane.normal.x +
        static_cast<double>(plane.normal.y) * plane.normal.y +
        static_cast<double>(plane.normal.z) * plane.normal.z;
    if (!std::isfinite(length_squared) || !(length_squared > 0.0)) {
        return false;
    }
    const auto length = std::sqrt(length_squared);
    return std::isfinite(length) &&
        std::abs(length - 1.0) <= kBroadPhasePlaneUnitTolerance;
}

[[nodiscard]] std::optional<core_collision::CollisionContents>
checked_proof_contents(
    const core_collision::CollisionContents& contents) noexcept
{
    const auto decoded =
        core_collision::decode_goldsrc_contents(contents.source);
    if (!decoded || decoded->category != contents.category) {
        return std::nullopt;
    }
    return decoded;
}

struct AxialPlaneConstraint {
    std::size_t axis{0U};
    double sign{1.0};
};

[[nodiscard]] std::optional<AxialPlaneConstraint> exact_axial_constraint(
    const core_collision::CollisionPlane& plane) noexcept
{
    const std::array components{
        plane.normal.x,
        plane.normal.y,
        plane.normal.z,
    };
    for (std::size_t axis = 0U; axis < components.size(); ++axis) {
        const auto first_other = (axis + 1U) % components.size();
        const auto second_other = (axis + 2U) % components.size();
        if ((components[axis] == 1.0F || components[axis] == -1.0F) &&
            components[first_other] == 0.0F &&
            components[second_other] == 0.0F) {
            return AxialPlaneConstraint{
                axis,
                components[axis] > 0.0F ? 1.0 : -1.0,
            };
        }
    }
    return std::nullopt;
}

void apply_axial_constraint(
    BroadPhaseProofBounds& bounds,
    const core_collision::CollisionPlane& plane,
    const bool front_child) noexcept
{
    const auto axial = exact_axial_constraint(plane);
    if (!axial) {
        return;
    }
    const auto threshold = plane.distance * axial->sign;
    const bool constrains_minimum =
        (front_child && axial->sign > 0.0) ||
        (!front_child && axial->sign < 0.0);
    if (constrains_minimum) {
        bounds.minimum[axial->axis] =
            std::max(bounds.minimum[axial->axis], threshold);
    } else {
        bounds.maximum[axial->axis] =
            std::min(bounds.maximum[axial->axis], threshold);
    }
}

[[nodiscard]] bool feasible_proof_bounds(
    const BroadPhaseProofBounds& bounds) noexcept
{
    for (std::size_t axis = 0U; axis < bounds.minimum.size(); ++axis) {
        if (bounds.minimum[axis] > bounds.maximum[axis]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<BroadPhaseProofReference> node_proof_child(
    const core_collision::CollisionNodeChild& child) noexcept
{
    switch (child.kind) {
    case core_collision::CollisionNodeChildKind::node:
        return BroadPhaseProofReference{
            BroadPhaseProofReferenceKind::node, child.index, {}};
    case core_collision::CollisionNodeChildKind::leaf:
        return BroadPhaseProofReference{
            BroadPhaseProofReferenceKind::leaf, child.index, {}};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<BroadPhaseProofReference> clip_proof_child(
    const core_collision::CollisionClipnodeChild& child) noexcept
{
    switch (child.kind) {
    case core_collision::CollisionClipnodeChildKind::clipnode:
        return BroadPhaseProofReference{
            BroadPhaseProofReferenceKind::clipnode, child.index, {}};
    case core_collision::CollisionClipnodeChildKind::terminal:
        return BroadPhaseProofReference{
            BroadPhaseProofReferenceKind::terminal, 0U, child.terminal};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<float> outward_float(
    const double value,
    const bool lower) noexcept
{
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    auto converted = static_cast<float>(value);
    if (!std::isfinite(converted)) {
        return std::nullopt;
    }
    const auto direction = lower
        ? -(std::numeric_limits<float>::infinity)()
        : (std::numeric_limits<float>::infinity)();
    const bool rounded_inward = lower
        ? static_cast<double>(converted) > value
        : static_cast<double>(converted) < value;
    if (rounded_inward) {
        converted = std::nextafter(converted, direction);
    }
    // One additional representable step makes the published float bound
    // outward even at an exactly representable source-plane distance.
    converted = std::nextafter(converted, direction);
    if (!std::isfinite(converted)) {
        return std::nullopt;
    }
    return converted;
}

[[nodiscard]] std::optional<assets::WorldBounds>
derive_conservative_blocking_bounds(
    const core_collision::CollisionWorldPackage& package,
    const core_collision::CollisionHull& hull,
    std::size_t& remaining_steps) noexcept
{
    if (remaining_steps == 0U) {
        return std::nullopt;
    }
    try {
        BroadPhaseProofReference root;
        std::size_t active_count = 0U;
        switch (hull.root.kind) {
        case core_collision::CollisionHullRootKind::node:
            root = {BroadPhaseProofReferenceKind::node, hull.root.index, {}};
            active_count = package.nodes().size();
            break;
        case core_collision::CollisionHullRootKind::clipnode:
            root = {
                BroadPhaseProofReferenceKind::clipnode, hull.root.index, {}};
            active_count = package.clipnodes().size();
            break;
        case core_collision::CollisionHullRootKind::terminal:
            root = {
                BroadPhaseProofReferenceKind::terminal,
                0U,
                hull.root.terminal,
            };
            break;
        }

        std::vector<std::uint8_t> active(active_count, 0U);
        std::vector<BroadPhaseProofFrame> stack;
        stack.reserve(std::min(active_count + 1U,
            core_collision::kCollisionHardMaximumStackEntries));
        stack.push_back(BroadPhaseProofFrame{root, {}, false});

        BroadPhaseProofBounds retained;
        retained.minimum.fill((std::numeric_limits<double>::infinity)());
        retained.maximum.fill(-(std::numeric_limits<double>::infinity)());
        bool saw_blocking = false;
        const auto retain_terminal = [&](
                                         const core_collision::
                                             CollisionContents& terminal,
                                         const BroadPhaseProofBounds& bounds) {
            const auto checked = checked_proof_contents(terminal);
            if (!checked) {
                return false;
            }
            if (!core_collision::blocks(
                    core_collision::CollisionContentsPolicy::
                        project_solid_only_v1,
                    checked->category)) {
                return true;
            }
            for (std::size_t axis = 0U; axis < bounds.minimum.size(); ++axis) {
                if (!std::isfinite(bounds.minimum[axis]) ||
                    !std::isfinite(bounds.maximum[axis])) {
                    return false;
                }
                retained.minimum[axis] =
                    std::min(retained.minimum[axis], bounds.minimum[axis]);
                retained.maximum[axis] =
                    std::max(retained.maximum[axis], bounds.maximum[axis]);
            }
            saw_blocking = true;
            return true;
        };

        while (!stack.empty()) {
            if (remaining_steps == 0U) {
                return std::nullopt;
            }
            --remaining_steps;
            auto frame = stack.back();
            stack.pop_back();

            if (frame.leave) {
                if (frame.reference.index >= active.size()) {
                    return std::nullopt;
                }
                active[frame.reference.index] = 0U;
                continue;
            }

            if (frame.reference.kind ==
                BroadPhaseProofReferenceKind::leaf) {
                if (frame.reference.index >= package.leaves().size() ||
                    !retain_terminal(
                        package.leaves()[frame.reference.index].contents,
                        frame.bounds)) {
                    return std::nullopt;
                }
                continue;
            }
            if (frame.reference.kind ==
                BroadPhaseProofReferenceKind::terminal) {
                if (!retain_terminal(frame.reference.terminal, frame.bounds)) {
                    return std::nullopt;
                }
                continue;
            }

            const bool node = frame.reference.kind ==
                BroadPhaseProofReferenceKind::node;
            const auto domain_size =
                node ? package.nodes().size() : package.clipnodes().size();
            if (frame.reference.index >= domain_size ||
                frame.reference.index >= active.size() ||
                active[frame.reference.index] != 0U) {
                return std::nullopt;
            }
            const auto plane_index = node
                ? package.nodes()[frame.reference.index].plane_index
                : package.clipnodes()[frame.reference.index].plane_index;
            if (plane_index >= package.planes().size() ||
                !valid_proof_plane(package.planes()[plane_index])) {
                return std::nullopt;
            }
            if (stack.size() + 3U >
                core_collision::kCollisionHardMaximumStackEntries) {
                return std::nullopt;
            }

            active[frame.reference.index] = 1U;
            auto leave = frame;
            leave.leave = true;
            stack.push_back(leave);

            for (std::size_t reverse_side = 0U; reverse_side < 2U;
                 ++reverse_side) {
                const auto side = 1U - reverse_side;
                const auto child = node
                    ? node_proof_child(
                          package.nodes()[frame.reference.index].children[side])
                    : clip_proof_child(package
                          .clipnodes()[frame.reference.index]
                          .children[side]);
                if (!child) {
                    return std::nullopt;
                }
                auto child_bounds = frame.bounds;
                apply_axial_constraint(child_bounds,
                    package.planes()[plane_index],
                    side == 0U);
                if (feasible_proof_bounds(child_bounds)) {
                    stack.push_back(BroadPhaseProofFrame{
                        *child, child_bounds, false});
                }
            }
        }

        if (!saw_blocking) {
            return std::nullopt;
        }
        std::array<float, 3U> minimum{};
        std::array<float, 3U> maximum{};
        for (std::size_t axis = 0U; axis < minimum.size(); ++axis) {
            const auto converted_minimum =
                outward_float(retained.minimum[axis], true);
            const auto converted_maximum =
                outward_float(retained.maximum[axis], false);
            if (!converted_minimum || !converted_maximum) {
                return std::nullopt;
            }
            minimum[axis] = *converted_minimum;
            maximum[axis] = *converted_maximum;
        }
        const assets::WorldBounds output{
            {minimum[0U], minimum[1U], minimum[2U]},
            {maximum[0U], maximum[1U], maximum[2U]},
        };
        return finite_bounds(output)
            ? std::optional<assets::WorldBounds>{output}
            : std::nullopt;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

struct OutwardDoubleInterval {
    double minimum{0.0};
    double maximum{0.0};
};

[[nodiscard]] std::optional<OutwardDoubleInterval> subtract_outward(
    const double left,
    const double right) noexcept
{
    const auto rounded = left - right;
    if (!std::isfinite(rounded)) {
        return std::nullopt;
    }
    return OutwardDoubleInterval{
        std::nextafter(
            rounded, -(std::numeric_limits<double>::infinity)()),
        std::nextafter(
            rounded, (std::numeric_limits<double>::infinity)()),
    };
}

[[nodiscard]] std::optional<OutwardDoubleInterval> divide_outward(
    const OutwardDoubleInterval& numerator,
    const OutwardDoubleInterval& denominator) noexcept
{
    if (denominator.minimum <= 0.0 && denominator.maximum >= 0.0) {
        return std::nullopt;
    }

    OutwardDoubleInterval output{
        (std::numeric_limits<double>::infinity)(),
        -(std::numeric_limits<double>::infinity)(),
    };
    const std::array numerators{numerator.minimum, numerator.maximum};
    const std::array denominators{denominator.minimum, denominator.maximum};
    for (const auto left : numerators) {
        for (const auto right : denominators) {
            const auto rounded = left / right;
            if (!std::isfinite(rounded)) {
                return std::nullopt;
            }
            output.minimum = std::min(output.minimum,
                std::nextafter(rounded,
                    -(std::numeric_limits<double>::infinity)()));
            output.maximum = std::max(output.maximum,
                std::nextafter(rounded,
                    (std::numeric_limits<double>::infinity)()));
        }
    }
    return output;
}

[[nodiscard]] bool segment_intersects_bounds(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const assets::WorldBounds& bounds) noexcept
{
    double minimum_fraction = 0.0;
    double maximum_fraction = 1.0;
    const auto test_axis = [&](const float start_value,
                               const float end_value,
                               const float minimum,
                               const float maximum) {
        if (start_value == end_value) {
            return start_value >= minimum && start_value <= maximum;
        }

        const auto start_double = static_cast<double>(start_value);
        const auto delta = subtract_outward(
            static_cast<double>(end_value), start_double);
        const auto minimum_numerator = subtract_outward(
            static_cast<double>(minimum), start_double);
        const auto maximum_numerator = subtract_outward(
            static_cast<double>(maximum), start_double);
        if (!delta || !minimum_numerator || !maximum_numerator) {
            return true;
        }
        const auto minimum_boundary =
            divide_outward(*minimum_numerator, *delta);
        const auto maximum_boundary =
            divide_outward(*maximum_numerator, *delta);
        if (!minimum_boundary || !maximum_boundary) {
            return true;
        }

        const auto& near_boundary = end_value > start_value
            ? *minimum_boundary
            : *maximum_boundary;
        const auto& far_boundary = end_value > start_value
            ? *maximum_boundary
            : *minimum_boundary;
        // `near.minimum` is a lower bound on the exact entry fraction and
        // `far.maximum` is an upper bound on the exact exit fraction. Reject
        // only when even those outward bounds are disjoint; any arithmetic
        // uncertainty deliberately falls through to the exact BSP trace.
        minimum_fraction =
            std::max(minimum_fraction, near_boundary.minimum);
        maximum_fraction =
            std::min(maximum_fraction, far_boundary.maximum);
        return minimum_fraction <= maximum_fraction;
    };

    return test_axis(start.x, end.x, bounds.minimum.x, bounds.maximum.x) &&
        test_axis(start.y, end.y, bounds.minimum.y, bounds.maximum.y) &&
        test_axis(start.z, end.z, bounds.minimum.z, bounds.maximum.z);
}

[[nodiscard]] assets::WorldBounds unrestricted_world_bounds() noexcept
{
    const auto maximum = (std::numeric_limits<float>::max)();
    return {{-maximum, -maximum, -maximum}, {maximum, maximum, maximum}};
}

struct OutwardAxisRange {
    float minimum{0.0F};
    float maximum{0.0F};
};

[[nodiscard]] std::optional<OutwardAxisRange> transform_axis_outward(
    const float translation,
    const std::array<float, 3U>& coefficients,
    const std::array<float, 3U>& local_minimum,
    const std::array<float, 3U>& local_maximum) noexcept
{
    auto minimum = static_cast<double>(translation);
    auto maximum = static_cast<double>(translation);
    for (std::size_t term = 0U; term < coefficients.size(); ++term) {
        const auto coefficient = static_cast<double>(coefficients[term]);
        const auto lower_source = coefficients[term] >= 0.0F
            ? local_minimum[term]
            : local_maximum[term];
        const auto upper_source = coefficients[term] >= 0.0F
            ? local_maximum[term]
            : local_minimum[term];
        minimum = std::nextafter(
            minimum + coefficient * static_cast<double>(lower_source),
            -(std::numeric_limits<double>::infinity)());
        maximum = std::nextafter(
            maximum + coefficient * static_cast<double>(upper_source),
            (std::numeric_limits<double>::infinity)());
    }
    const auto converted_minimum = outward_float(minimum, true);
    const auto converted_maximum = outward_float(maximum, false);
    if (!converted_minimum || !converted_maximum) {
        return std::nullopt;
    }
    return OutwardAxisRange{*converted_minimum, *converted_maximum};
}

[[nodiscard]] std::optional<assets::WorldBounds>
transform_proven_bounds_outward(
    const assets::WorldBounds& local_bounds,
    const brush::BrushRigidTransform& transform) noexcept
{
    if (!finite_bounds(local_bounds) ||
        !brush::valid_brush_rigid_transform(transform)) {
        return std::nullopt;
    }
    const std::array local_minimum{
        local_bounds.minimum.x,
        local_bounds.minimum.y,
        local_bounds.minimum.z,
    };
    const std::array local_maximum{
        local_bounds.maximum.x,
        local_bounds.maximum.y,
        local_bounds.maximum.z,
    };
    const auto& basis = transform.rotation_basis;
    const auto x = transform_axis_outward(transform.translation.x,
        {basis.local_x_in_world.x,
            basis.local_y_in_world.x,
            basis.local_z_in_world.x},
        local_minimum,
        local_maximum);
    const auto y = transform_axis_outward(transform.translation.y,
        {basis.local_x_in_world.y,
            basis.local_y_in_world.y,
            basis.local_z_in_world.y},
        local_minimum,
        local_maximum);
    const auto z = transform_axis_outward(transform.translation.z,
        {basis.local_x_in_world.z,
            basis.local_y_in_world.z,
            basis.local_z_in_world.z},
        local_minimum,
        local_maximum);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    const assets::WorldBounds output{
        {x->minimum, y->minimum, z->minimum},
        {x->maximum, y->maximum, z->maximum},
    };
    return finite_bounds(output)
        ? std::optional<assets::WorldBounds>{output}
        : std::nullopt;
}

[[nodiscard]] PreparedBroadPhase prepare_broad_phase(
    const BrushCollisionModel& model,
    const brush::BrushRigidTransform& transform,
    const ExplicitBrushCollisionTraceRequest& request) noexcept
{
    if (!core_collision::valid_collision_query_limits(request.query_limits) ||
        !core_collision::valid_collision_trace_tolerance_profile(
            request.tolerance)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_configuration,
            core_collision::CollisionQueryErrorCode::invalid_configuration,
            model.source_model_index);
    }
    if (!core_collision::supported_collision_contents_policy(
            request.contents_policy)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_configuration,
            core_collision::CollisionQueryErrorCode::
                unsupported_contents_policy,
            model.source_model_index);
    }
    if (request.trace_profile !=
        core_collision::CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_configuration,
            core_collision::CollisionQueryErrorCode::unsupported_trace_profile,
            model.source_model_index);
    }
    if (model.collision_world == nullptr ||
        !valid_package_shape(*model.collision_world)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_model,
            core_collision::CollisionQueryErrorCode::invalid_package,
            model.source_model_index);
    }
    const auto* retained =
        model.collision_world->model(model.source_model_index);
    if (retained == nullptr || !coherent_brush_model(model, *retained)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_model,
            core_collision::CollisionQueryErrorCode::invalid_model,
            model.source_model_index);
    }
    if (!brush::valid_brush_rigid_transform(transform) ||
        !zero_vector(model.source_origin) ||
        transform.source_model_origin.x != model.source_origin.x ||
        transform.source_model_origin.y != model.source_origin.y ||
        transform.source_model_origin.z != model.source_origin.z) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_transform,
            std::nullopt,
            model.source_model_index);
    }
    if (!finite_vector(request.start) || !finite_vector(request.end)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_segment,
            core_collision::CollisionQueryErrorCode::invalid_segment,
            model.source_model_index);
    }
    if (!core_collision::standard_collision_hull_profile(request.hull)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_hull,
            core_collision::CollisionQueryErrorCode::invalid_hull,
            model.source_model_index);
    }
    const auto* selected_hull = retained->hull(request.hull);
    if (selected_hull == nullptr) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_hull,
            core_collision::CollisionQueryErrorCode::invalid_hull,
            model.source_model_index);
    }

    // Validate the retained source-model transform, but never use dmodel
    // bounds to authorize rejection: arbitrary structurally valid trees are
    // not proven to be enclosed by those source metadata bounds.
    const auto transformed_source_bounds =
        brush::transform_brush_rigid_bounds(model.local_bounds, transform);
    if (!transformed_source_bounds || !transformed_source_bounds.bounds ||
        !finite_bounds(*transformed_source_bounds.bounds)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_bounds,
            std::nullopt,
            model.source_model_index);
    }

    const auto local_start =
        brush::brush_rigid_world_to_local_point(transform, request.start);
    const auto local_end =
        brush::brush_rigid_world_to_local_point(transform, request.end);
    if (!finite_vector(local_start) || !finite_vector(local_end)) {
        return broad_phase_failure(
            ExplicitBrushCollisionTraceErrorCode::invalid_segment,
            core_collision::CollisionQueryErrorCode::invalid_segment,
            model.source_model_index);
    }

    const auto unrestricted = unrestricted_world_bounds();
    const auto* proven_local_bounds =
        model.conservative_blocking_bounds(request.hull);
    if (proven_local_bounds == nullptr) {
        return {
            true,
            true,
            unrestricted,
            ExplicitBrushCollisionTraceErrorCode::invalid_model,
            std::nullopt,
        };
    }
    const auto proven_world_bounds =
        transform_proven_bounds_outward(*proven_local_bounds, transform);
    return {
        true,
        segment_intersects_bounds(
            local_start, local_end, *proven_local_bounds),
        proven_world_bounds.value_or(unrestricted),
        ExplicitBrushCollisionTraceErrorCode::invalid_model,
        std::nullopt,
    };
}

[[nodiscard]] assets::AssetVector3 point_at_fraction(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const double fraction) noexcept
{
    return {
        static_cast<float>(std::fma(fraction,
            static_cast<double>(end.x) - static_cast<double>(start.x),
            static_cast<double>(start.x))),
        static_cast<float>(std::fma(fraction,
            static_cast<double>(end.y) - static_cast<double>(start.y),
            static_cast<double>(start.y))),
        static_cast<float>(std::fma(fraction,
            static_cast<double>(end.z) - static_cast<double>(start.z),
            static_cast<double>(start.z))),
    };
}

[[nodiscard]] ExplicitBrushCollisionTraceQueryResult trace_explicit_impl(
    const BrushCollisionModel& model,
    const brush::BrushRigidTransform& transform,
    const ExplicitBrushCollisionTraceRequest& request,
    core_collision::CollisionQueryScratch& scratch,
    const PreparedBroadPhase& broad_phase)
{
    if (!broad_phase.valid) {
        return trace_failure(broad_phase.error, broad_phase.query_error);
    }

    ExplicitBrushCollisionTraceResult output;
    output.expanded_world_bounds = broad_phase.expanded_world_bounds;
    if (!broad_phase.intersects) {
        output.broad_phase_rejected = true;
        output.trace.fraction = 1.0;
        output.trace.end_position = request.end;
        output.trace.in_open = true;
        output.trace.start_contents =
            *core_collision::decode_goldsrc_contents({-1});
        output.trace.end_contents =
            *core_collision::decode_goldsrc_contents({-1});
        output.trace.trace_profile = request.trace_profile;
        output.trace.collision_profile =
            model.collision_world->compatibility_profile();
        return {std::move(output), std::nullopt};
    }

    const auto local_start =
        brush::brush_rigid_world_to_local_point(transform, request.start);
    const auto local_end =
        brush::brush_rigid_world_to_local_point(transform, request.end);
    if (!finite_vector(local_start) || !finite_vector(local_end)) {
        return trace_failure(
            ExplicitBrushCollisionTraceErrorCode::non_finite_result);
    }

    core_collision::CollisionWorldQuery query{model.collision_world};
    auto traced = query.trace_model_hull(
        core_collision::CollisionTraceRequest{
            local_start,
            local_end,
            model.source_model_index,
            request.hull,
            request.contents_policy,
            request.trace_profile,
            request.tolerance,
            request.query_limits,
        },
        scratch);
    if (!traced || !traced.result) {
        return trace_failure(
            ExplicitBrushCollisionTraceErrorCode::collision_query_failed,
            std::move(traced.error));
    }

    output.trace = std::move(*traced.result);
    if (!std::isfinite(output.trace.fraction) ||
        output.trace.fraction < 0.0 || output.trace.fraction > 1.0) {
        return trace_failure(
            ExplicitBrushCollisionTraceErrorCode::non_finite_result);
    }
    output.trace.end_position = point_at_fraction(
        request.start, request.end, output.trace.fraction);
    if (!finite_vector(output.trace.end_position)) {
        return trace_failure(
            ExplicitBrushCollisionTraceErrorCode::non_finite_result);
    }
    if (output.trace.collision_plane) {
        auto& plane = *output.trace.collision_plane;
        const auto world_normal = brush::brush_rigid_local_to_world_normal(
            transform, plane.normal);
        const auto world_distance = plane.distance +
            static_cast<double>(world_normal.x) * transform.translation.x +
            static_cast<double>(world_normal.y) * transform.translation.y +
            static_cast<double>(world_normal.z) * transform.translation.z;
        if (!finite_vector(world_normal) || !std::isfinite(world_distance)) {
            return trace_failure(
                ExplicitBrushCollisionTraceErrorCode::non_finite_result);
        }
        plane.normal = world_normal;
        plane.distance = world_distance;
    }
    if (output.trace.hit) {
        output.trace.hit->kind =
            core_collision::CollisionTraceHitKind::collision_model;
        output.trace.hit->source_model_index = model.source_model_index;
    }
    return {std::move(output), std::nullopt};
}

[[nodiscard]] BrushCollisionSceneBuildResult scene_build_failure(
    const BrushCollisionSceneBuildErrorCode code,
    std::optional<BrushCollisionInstanceIdentity> instance =
        std::nullopt) noexcept
{
    return {
        {},
        BrushCollisionSceneBuildError{code, std::move(instance)},
    };
}

[[nodiscard]] BrushCollisionSceneTraceQueryResult scene_query_failure(
    const BrushCollisionSceneQueryErrorCode code,
    std::optional<BrushCollisionInstanceIdentity> instance = std::nullopt,
    std::optional<core_collision::CollisionQueryError> query_error =
        std::nullopt,
    const std::optional<ExplicitBrushCollisionTraceErrorCode>
        brush_trace_error = std::nullopt) noexcept
{
    return {
        std::nullopt,
        BrushCollisionSceneQueryError{
            code,
            std::move(instance),
            std::move(query_error),
            brush_trace_error,
        },
    };
}

[[nodiscard]] bool blocking_trace(
    const core_collision::CollisionTraceResult& trace) noexcept
{
    return trace.start_solid || trace.all_solid || trace.hit.has_value() ||
        trace.blocking_contents.has_value() || trace.fraction < 1.0;
}

[[nodiscard]] bool candidate_precedes(
    const double candidate_fraction,
    const BrushCollisionInstanceIdentity& candidate,
    const double selected_fraction,
    const std::optional<BrushCollisionInstanceIdentity>& selected_brush,
    const bool selected_is_world,
    const double fraction_epsilon) noexcept
{
    if (candidate_fraction < selected_fraction - fraction_epsilon) {
        return true;
    }
    if (candidate_fraction > selected_fraction + fraction_epsilon) {
        return false;
    }
    // Fractions within the declared project tolerance are an explicit tie.
    // World wins first, then stable ordinal, then source model index.
    if (selected_is_world) {
        return false;
    }
    if (!selected_brush) {
        return true;
    }
    if (candidate.stable_instance_ordinal !=
        selected_brush->stable_instance_ordinal) {
        return candidate.stable_instance_ordinal <
            selected_brush->stable_instance_ordinal;
    }
    return candidate.source_model_index < selected_brush->source_model_index;
}

} // namespace

std::string_view to_string(const BrushCollisionRole role) noexcept
{
    switch (role) {
    case BrushCollisionRole::solid: return "solid";
    case BrushCollisionRole::non_solid: return "non_solid";
    case BrushCollisionRole::unsupported: return "unsupported";
    case BrushCollisionRole::evidence_pending: return "evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const BrushCollisionRoleProviderProfile profile) noexcept
{
    switch (profile) {
    case BrushCollisionRoleProviderProfile::
        explicit_synthetic_brush_solidity_v1:
        return "explicit_synthetic_brush_solidity_v1";
    case BrushCollisionRoleProviderProfile::
        stock_brush_solidity_evidence_pending:
        return "stock_brush_solidity_evidence_pending";
    }
    return "unknown";
}

ExplicitSyntheticBrushCollisionRoleProvider::
ExplicitSyntheticBrushCollisionRoleProvider(
    std::vector<SyntheticBrushCollisionRoleBinding> bindings) noexcept
    : bindings_{std::move(bindings)}
{
}

BrushCollisionRoleProviderProfile
ExplicitSyntheticBrushCollisionRoleProvider::profile() const noexcept
{
    return BrushCollisionRoleProviderProfile::
        explicit_synthetic_brush_solidity_v1;
}

BrushCollisionRole ExplicitSyntheticBrushCollisionRoleProvider::role_for(
    const BrushCollisionInstanceIdentity& identity) const noexcept
{
    std::optional<BrushCollisionRole> found;
    for (const auto& binding : bindings_) {
        if (binding.identity != identity) {
            continue;
        }
        if (found) {
            return BrushCollisionRole::unsupported;
        }
        found = binding.role;
    }
    return found.value_or(BrushCollisionRole::evidence_pending);
}

std::span<const SyntheticBrushCollisionRoleBinding>
ExplicitSyntheticBrushCollisionRoleProvider::bindings() const noexcept
{
    return bindings_;
}

BrushCollisionRoleProviderProfile
StockBrushCollisionRoleProvider::profile() const noexcept
{
    return BrushCollisionRoleProviderProfile::
        stock_brush_solidity_evidence_pending;
}

BrushCollisionRole StockBrushCollisionRoleProvider::role_for(
    const BrushCollisionInstanceIdentity&) const noexcept
{
    return BrushCollisionRole::evidence_pending;
}

const assets::WorldBounds* BrushCollisionModel::conservative_blocking_bounds(
    const core_collision::CollisionHullOrdinal hull) const noexcept
{
    const auto ordinal = static_cast<std::size_t>(hull);
    if (!core_collision::collision_hull_ordinal(ordinal) ||
        ordinal >= conservative_blocking_bounds_.size() ||
        collision_world == nullptr ||
        broad_phase_proof_source_ == nullptr ||
        collision_world.get() != broad_phase_proof_source_.get() ||
        !broad_phase_proof_model_index_ ||
        source_model_index != *broad_phase_proof_model_index_ ||
        !conservative_blocking_bounds_[ordinal]) {
        return nullptr;
    }
    return &*conservative_blocking_bounds_[ordinal];
}

std::string_view to_string(
    const BrushCollisionModelLibraryErrorCode code) noexcept
{
    switch (code) {
    case BrushCollisionModelLibraryErrorCode::invalid_package:
        return "invalid_package";
    case BrushCollisionModelLibraryErrorCode::world_model_missing:
        return "world_model_missing";
    case BrushCollisionModelLibraryErrorCode::invalid_model:
        return "invalid_model";
    case BrushCollisionModelLibraryErrorCode::duplicate_model:
        return "duplicate_model";
    case BrushCollisionModelLibraryErrorCode::unable_to_publish:
        return "unable_to_publish";
    }
    return "unknown";
}

BrushCollisionModelLibrary::BrushCollisionModelLibrary(
    std::shared_ptr<const core_collision::CollisionWorldPackage>
        collision_world,
    std::vector<BrushCollisionModel> models) noexcept
    : collision_world_{std::move(collision_world)},
      models_{std::move(models)}
{
}

const std::shared_ptr<const core_collision::CollisionWorldPackage>&
BrushCollisionModelLibrary::collision_world() const noexcept
{
    return collision_world_;
}

std::span<const BrushCollisionModel>
BrushCollisionModelLibrary::models() const noexcept
{
    return models_;
}

const BrushCollisionModel* BrushCollisionModelLibrary::model(
    const std::uint32_t source_model_index) const noexcept
{
    const BrushCollisionModel* found = nullptr;
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

BrushCollisionModelLibraryBuildResult build_brush_collision_model_library(
    std::shared_ptr<const core_collision::CollisionWorldPackage>
        collision_world) noexcept
{
    if (collision_world == nullptr || collision_world->models().empty()) {
        return library_failure(
            BrushCollisionModelLibraryErrorCode::invalid_package);
    }
    try {
        std::size_t world_model_count = 0U;
        std::size_t remaining_broad_phase_proof_steps =
            core_collision::kCollisionHardMaximumTraversalSteps;
        std::vector<BrushCollisionModel> models;
        models.reserve(collision_world->models().size() - 1U);
        for (const auto& source : collision_world->models()) {
            if (!finite_bounds(source.source_bounds) ||
                !finite_vector(source.source_origin) ||
                !valid_model_hulls(source)) {
                return library_failure(
                    BrushCollisionModelLibraryErrorCode::invalid_model,
                    source.source_model_index);
            }
            if (source.source_model_index == 0U) {
                ++world_model_count;
                continue;
            }
            if (std::ranges::any_of(models,
                    [&](const BrushCollisionModel& model) {
                        return model.source_model_index ==
                            source.source_model_index;
                    })) {
                return library_failure(
                    BrushCollisionModelLibraryErrorCode::duplicate_model,
                    source.source_model_index);
            }
            BrushCollisionModel model;
            model.source_model_index = source.source_model_index;
            model.local_bounds = source.source_bounds;
            model.source_origin = source.source_origin;
            for (std::size_t hull = 0U; hull < source.hulls.size(); ++hull) {
                model.hull_roots[hull] = source.hulls[hull].root;
            }
            model.collision_identity = collision_world->identity();
            model.collision_world = collision_world;
            model.broad_phase_proof_source_ = collision_world;
            model.broad_phase_proof_model_index_ =
                source.source_model_index;
            for (std::size_t hull = 0U; hull < source.hulls.size(); ++hull) {
                model.conservative_blocking_bounds_[hull] =
                    derive_conservative_blocking_bounds(
                        *collision_world,
                        source.hulls[hull],
                        remaining_broad_phase_proof_steps);
            }
            models.push_back(std::move(model));
        }
        if (world_model_count != 1U) {
            return library_failure(
                BrushCollisionModelLibraryErrorCode::world_model_missing);
        }
        std::ranges::sort(models, {}, &BrushCollisionModel::source_model_index);
        auto library = std::make_shared<const BrushCollisionModelLibrary>(
            std::move(collision_world), std::move(models));
        return {std::move(library), std::nullopt};
    } catch (const std::bad_alloc&) {
        return library_failure(
            BrushCollisionModelLibraryErrorCode::unable_to_publish);
    } catch (...) {
        return library_failure(
            BrushCollisionModelLibraryErrorCode::unable_to_publish);
    }
}

std::string_view to_string(
    const ExplicitBrushCollisionTraceErrorCode code) noexcept
{
    switch (code) {
    case ExplicitBrushCollisionTraceErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ExplicitBrushCollisionTraceErrorCode::invalid_model:
        return "invalid_model";
    case ExplicitBrushCollisionTraceErrorCode::invalid_transform:
        return "invalid_transform";
    case ExplicitBrushCollisionTraceErrorCode::invalid_segment:
        return "invalid_segment";
    case ExplicitBrushCollisionTraceErrorCode::invalid_hull:
        return "invalid_hull";
    case ExplicitBrushCollisionTraceErrorCode::invalid_bounds:
        return "invalid_bounds";
    case ExplicitBrushCollisionTraceErrorCode::collision_query_failed:
        return "collision_query_failed";
    case ExplicitBrushCollisionTraceErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

ExplicitBrushCollisionTraceQueryResult trace_explicit_brush_model(
    const BrushCollisionModel& model,
    const brush::BrushRigidTransform& transform,
    const ExplicitBrushCollisionTraceRequest& request,
    core_collision::CollisionQueryScratch& scratch)
{
    return trace_explicit_impl(
        model,
        transform,
        request,
        scratch,
        prepare_broad_phase(model, transform, request));
}

bool valid_brush_collision_scene_build_limits(
    const BrushCollisionSceneBuildLimits& limits) noexcept
{
    return limits.maximum_instances > 0U &&
        limits.maximum_instances <= kHardMaximumBrushCollisionSceneInstances;
}

bool valid_brush_collision_scene_query_limits(
    const BrushCollisionSceneQueryLimits& limits) noexcept
{
    return limits.maximum_brush_candidates > 0U &&
        limits.maximum_brush_candidates <=
            kHardMaximumBrushCollisionCandidates &&
        limits.maximum_model_traces > 0U &&
        limits.maximum_model_traces <=
            kHardMaximumBrushCollisionModelTraces;
}

std::string_view to_string(
    const BrushCollisionSceneBuildErrorCode code) noexcept
{
    switch (code) {
    case BrushCollisionSceneBuildErrorCode::invalid_configuration:
        return "invalid_configuration";
    case BrushCollisionSceneBuildErrorCode::invalid_library:
        return "invalid_library";
    case BrushCollisionSceneBuildErrorCode::instance_limit_exceeded:
        return "instance_limit_exceeded";
    case BrushCollisionSceneBuildErrorCode::invalid_instance_identity:
        return "invalid_instance_identity";
    case BrushCollisionSceneBuildErrorCode::duplicate_instance_identity:
        return "duplicate_instance_identity";
    case BrushCollisionSceneBuildErrorCode::model_not_found:
        return "model_not_found";
    case BrushCollisionSceneBuildErrorCode::invalid_transform:
        return "invalid_transform";
    case BrushCollisionSceneBuildErrorCode::invalid_transformed_bounds:
        return "invalid_transformed_bounds";
    case BrushCollisionSceneBuildErrorCode::unsupported_role_provider:
        return "unsupported_role_provider";
    case BrushCollisionSceneBuildErrorCode::invalid_role:
        return "invalid_role";
    case BrushCollisionSceneBuildErrorCode::unable_to_publish:
        return "unable_to_publish";
    }
    return "unknown";
}

BrushCollisionScene::BrushCollisionScene(
    std::shared_ptr<const BrushCollisionModelLibrary> model_library,
    std::vector<BrushCollisionSceneInstance> instances,
    const BrushCollisionRoleProviderProfile role_provider_profile) noexcept
    : model_library_{std::move(model_library)},
      instances_{std::move(instances)},
      role_provider_profile_{role_provider_profile}
{
}

const std::shared_ptr<const BrushCollisionModelLibrary>&
BrushCollisionScene::model_library() const noexcept
{
    return model_library_;
}

std::span<const BrushCollisionSceneInstance>
BrushCollisionScene::instances() const noexcept
{
    return instances_;
}

BrushCollisionRoleProviderProfile
BrushCollisionScene::role_provider_profile() const noexcept
{
    return role_provider_profile_;
}

BrushCollisionSceneBuildResult build_brush_collision_scene(
    std::shared_ptr<const BrushCollisionModelLibrary> model_library,
    const std::span<const BrushCollisionInstanceDefinition> instances,
    const IBrushCollisionRoleProvider& role_provider,
    const BrushCollisionSceneBuildLimits& limits) noexcept
{
    if (!valid_brush_collision_scene_build_limits(limits)) {
        return scene_build_failure(
            BrushCollisionSceneBuildErrorCode::invalid_configuration);
    }
    if (model_library == nullptr ||
        model_library->collision_world() == nullptr) {
        return scene_build_failure(
            BrushCollisionSceneBuildErrorCode::invalid_library);
    }
    if (instances.size() > limits.maximum_instances) {
        return scene_build_failure(
            BrushCollisionSceneBuildErrorCode::instance_limit_exceeded);
    }
    const auto provider_profile = role_provider.profile();
    if (!supported_provider_profile(provider_profile)) {
        return scene_build_failure(
            BrushCollisionSceneBuildErrorCode::unsupported_role_provider);
    }

    try {
        std::vector<BrushCollisionSceneInstance> retained;
        retained.reserve(instances.size());
        for (const auto& definition : instances) {
            const auto& identity = definition.identity;
            if (identity.source_model_index == 0U) {
                return scene_build_failure(
                    BrushCollisionSceneBuildErrorCode::
                        invalid_instance_identity,
                    identity);
            }
            if (std::ranges::any_of(retained,
                    [&](const BrushCollisionSceneInstance& candidate) {
                        return duplicate_stable_identity(
                            candidate.identity, identity);
                    })) {
                return scene_build_failure(
                    BrushCollisionSceneBuildErrorCode::
                        duplicate_instance_identity,
                    identity);
            }
            const auto* model =
                model_library->model(identity.source_model_index);
            if (model == nullptr) {
                return scene_build_failure(
                    BrushCollisionSceneBuildErrorCode::model_not_found,
                    identity);
            }
            if (!brush::valid_brush_rigid_transform(definition.transform) ||
                definition.transform.source_model_origin.x !=
                    model->source_origin.x ||
                definition.transform.source_model_origin.y !=
                    model->source_origin.y ||
                definition.transform.source_model_origin.z !=
                    model->source_origin.z) {
                return scene_build_failure(
                    BrushCollisionSceneBuildErrorCode::invalid_transform,
                    identity);
            }
            auto transformed = brush::transform_brush_rigid_bounds(
                model->local_bounds, definition.transform);
            if (!transformed || !transformed.bounds ||
                !finite_bounds(*transformed.bounds)) {
                return scene_build_failure(
                    BrushCollisionSceneBuildErrorCode::
                        invalid_transformed_bounds,
                    identity);
            }
            const auto role = role_provider.role_for(identity);
            if (!supported_role(role)) {
                return scene_build_failure(
                    BrushCollisionSceneBuildErrorCode::invalid_role,
                    identity);
            }
            retained.push_back(BrushCollisionSceneInstance{
                identity,
                definition.transform,
                *transformed.bounds,
                role,
                provider_profile,
            });
        }
        std::ranges::sort(retained,
            [](const BrushCollisionSceneInstance& left,
               const BrushCollisionSceneInstance& right) {
                return identity_less(left.identity, right.identity);
            });
        auto scene = std::make_shared<const BrushCollisionScene>(
            std::move(model_library),
            std::move(retained),
            provider_profile);
        return {std::move(scene), std::nullopt};
    } catch (const std::bad_alloc&) {
        return scene_build_failure(
            BrushCollisionSceneBuildErrorCode::unable_to_publish);
    } catch (...) {
        return scene_build_failure(
            BrushCollisionSceneBuildErrorCode::unable_to_publish);
    }
}

std::string_view to_string(
    const BrushCollisionSceneQueryErrorCode code) noexcept
{
    switch (code) {
    case BrushCollisionSceneQueryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case BrushCollisionSceneQueryErrorCode::invalid_scene:
        return "invalid_scene";
    case BrushCollisionSceneQueryErrorCode::world_query_failed:
        return "world_query_failed";
    case BrushCollisionSceneQueryErrorCode::invalid_instance:
        return "invalid_instance";
    case BrushCollisionSceneQueryErrorCode::candidate_limit_exceeded:
        return "candidate_limit_exceeded";
    case BrushCollisionSceneQueryErrorCode::model_trace_limit_exceeded:
        return "model_trace_limit_exceeded";
    case BrushCollisionSceneQueryErrorCode::brush_trace_failed:
        return "brush_trace_failed";
    case BrushCollisionSceneQueryErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

std::string_view to_string(
    const BrushCollisionSceneAllSolidClassification classification) noexcept
{
    switch (classification) {
    case BrushCollisionSceneAllSolidClassification::
        exact_without_model_trace:
        return "exact_without_model_trace";
    case BrushCollisionSceneAllSolidClassification::exact_from_world_only:
        return "exact_from_world_only";
    case BrushCollisionSceneAllSolidClassification::
        proven_true_by_single_object:
        return "proven_true_by_single_object";
    case BrushCollisionSceneAllSolidClassification::
        multi_object_interval_union_evidence_pending:
        return "multi_object_interval_union_evidence_pending";
    }
    return "unknown";
}

BrushCollisionSceneQuery::BrushCollisionSceneQuery(
    std::shared_ptr<const BrushCollisionScene> scene) noexcept
    : scene_{std::move(scene)}
{
}

const std::shared_ptr<const BrushCollisionScene>&
BrushCollisionSceneQuery::scene() const noexcept
{
    return scene_;
}

BrushCollisionSceneTraceQueryResult BrushCollisionSceneQuery::trace_hull(
    const BrushCollisionSceneTraceRequest& request,
    core_collision::CollisionQueryScratch& scratch) const
{
    if (!valid_brush_collision_scene_query_limits(request.scene_limits) ||
        !core_collision::valid_collision_query_limits(request.query_limits) ||
        !core_collision::valid_collision_trace_tolerance_profile(
            request.tolerance) ||
        !finite_vector(request.start) || !finite_vector(request.end) ||
        !core_collision::supported_collision_contents_policy(
            request.contents_policy) ||
        request.trace_profile !=
            core_collision::CollisionTraceCompatibilityProfile::
                project_deterministic_bsp_hull_trace_v1 ||
        !core_collision::standard_collision_hull_profile(request.hull) ||
        (request.explicit_brush_instances &&
            request.explicit_brush_instances->size() >
                kHardMaximumBrushCollisionSceneInstances)) {
        return scene_query_failure(
            BrushCollisionSceneQueryErrorCode::invalid_configuration);
    }
    if (scene_ == nullptr || scene_->model_library() == nullptr ||
        scene_->model_library()->collision_world() == nullptr) {
        return scene_query_failure(
            BrushCollisionSceneQueryErrorCode::invalid_scene);
    }

    const auto world = scene_->model_library()->collision_world();
    if (!valid_package_shape(*world)) {
        return scene_query_failure(
            BrushCollisionSceneQueryErrorCode::invalid_scene);
    }

    std::vector<BrushCollisionInstanceIdentity> explicit_brush_instances;
    if (request.explicit_brush_instances) {
        try {
            explicit_brush_instances.assign(
                request.explicit_brush_instances->begin(),
                request.explicit_brush_instances->end());
            std::ranges::sort(explicit_brush_instances, identity_less);
            const auto duplicates = std::ranges::unique(
                explicit_brush_instances);
            explicit_brush_instances.erase(
                duplicates.begin(), duplicates.end());
        } catch (const std::bad_alloc&) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::invalid_configuration);
        }
    }

    BrushCollisionSceneTraceResult output;
    output.statistics.instance_count = scene_->instances().size();
    if (request.include_world) {
        core_collision::CollisionWorldQuery world_query{world};
        auto world_trace = world_query.trace_hull(
            core_collision::CollisionTraceRequest{
                request.start,
                request.end,
                0U,
                request.hull,
                request.contents_policy,
                request.trace_profile,
                request.tolerance,
                request.query_limits,
            },
            scratch);
        if (!world_trace || !world_trace.result) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::world_query_failed,
                std::nullopt,
                std::move(world_trace.error));
        }
        output.trace = *world_trace.result;
    } else {
        output.trace.fraction = 1.0;
        output.trace.end_position = request.end;
        output.trace.in_open = true;
        output.trace.trace_profile = request.trace_profile;
        output.trace.trace_evidence_profile =
            core_collision::CollisionTraceEvidenceProfile::
                public_bsp_structure_and_independent_fixtures;
        output.trace.collision_profile = world->compatibility_profile();
        output.all_solid_classification =
            BrushCollisionSceneAllSolidClassification::
                exact_without_model_trace;
    }
    bool any_start_solid = output.trace.start_solid;
    bool any_all_solid = output.trace.all_solid;
    bool any_in_open = output.trace.in_open;
    bool any_in_liquid = output.trace.in_liquid;

    bool selected_blocking = blocking_trace(output.trace);
    bool selected_is_world = selected_blocking;
    std::optional<BrushCollisionInstanceIdentity> selected_brush;
    if (selected_blocking) {
        output.scene_hit = BrushCollisionSceneHit{
            core_collision::CollisionTraceHitKind::world,
            std::nullopt,
        };
    }

    for (const auto& instance : scene_->instances()) {
        if ((request.ignored_instance &&
                instance.identity == *request.ignored_instance) ||
            (request.explicit_brush_instances &&
                !std::ranges::binary_search(explicit_brush_instances,
                    instance.identity,
                    identity_less))) {
            continue;
        }
        switch (instance.role) {
        case BrushCollisionRole::non_solid:
            ++output.statistics.non_solid_instance_count;
            continue;
        case BrushCollisionRole::unsupported:
            ++output.statistics.unsupported_instance_count;
            continue;
        case BrushCollisionRole::evidence_pending:
            ++output.statistics.evidence_pending_instance_count;
            continue;
        case BrushCollisionRole::solid:
            ++output.statistics.solid_instance_count;
            break;
        }

        const auto* model = scene_->model_library()->model(
            instance.identity.source_model_index);
        if (model == nullptr ||
            !brush::valid_brush_rigid_transform(instance.transform)) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::invalid_instance,
                instance.identity);
        }
        const ExplicitBrushCollisionTraceRequest brush_request{
            request.start,
            request.end,
            request.hull,
            request.contents_policy,
            request.trace_profile,
            request.tolerance,
            request.query_limits,
        };
        ++output.statistics.broad_phase_test_count;
        const auto broad_phase =
            prepare_broad_phase(*model, instance.transform, brush_request);
        if (!broad_phase.valid) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::invalid_instance,
                instance.identity,
                broad_phase.query_error,
                broad_phase.error);
        }
        if (!broad_phase.intersects) {
            ++output.statistics.broad_phase_rejection_count;
            continue;
        }
        if (output.statistics.brush_candidate_count >=
            request.scene_limits.maximum_brush_candidates) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::candidate_limit_exceeded,
                instance.identity);
        }
        ++output.statistics.brush_candidate_count;
        if (output.statistics.brush_model_trace_count >=
            request.scene_limits.maximum_model_traces) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::model_trace_limit_exceeded,
                instance.identity);
        }
        ++output.statistics.brush_model_trace_count;

        auto traced = trace_explicit_impl(
            *model,
            instance.transform,
            brush_request,
            scratch,
            broad_phase);
        if (!traced || !traced.result) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::brush_trace_failed,
                instance.identity,
                traced.error ? std::move(traced.error->query_error)
                             : std::nullopt,
                traced.error ? std::optional{traced.error->code}
                             : std::nullopt);
        }
        const auto& candidate = traced.result->trace;
        if (!std::isfinite(candidate.fraction) ||
            candidate.fraction < 0.0 || candidate.fraction > 1.0) {
            return scene_query_failure(
                BrushCollisionSceneQueryErrorCode::non_finite_result,
                instance.identity);
        }
        any_start_solid = any_start_solid || candidate.start_solid;
        any_all_solid = any_all_solid || candidate.all_solid;
        any_in_open = any_in_open || candidate.in_open;
        any_in_liquid = any_in_liquid || candidate.in_liquid;
        if (!blocking_trace(candidate)) {
            continue;
        }

        const bool precedes = !selected_blocking || candidate_precedes(
            candidate.fraction,
            instance.identity,
            output.trace.fraction,
            selected_brush,
            selected_is_world,
            request.tolerance.fraction_epsilon);
        if (!precedes) {
            continue;
        }
        output.trace = candidate;
        output.scene_hit = BrushCollisionSceneHit{
            core_collision::CollisionTraceHitKind::collision_model,
            instance.identity,
        };
        selected_blocking = true;
        selected_is_world = false;
        selected_brush = instance.identity;
    }
    output.trace.start_solid = any_start_solid;
    output.trace.all_solid = any_all_solid;
    output.trace.in_open = any_in_open;
    output.trace.in_liquid = any_in_liquid;
    if (any_all_solid) {
        output.all_solid_classification =
            BrushCollisionSceneAllSolidClassification::
                proven_true_by_single_object;
    } else if (output.statistics.brush_model_trace_count != 0U) {
        output.all_solid_classification =
            BrushCollisionSceneAllSolidClassification::
                multi_object_interval_union_evidence_pending;
    }
    return {std::move(output), std::nullopt};
}

} // namespace hlclient::goldsrc::collision
