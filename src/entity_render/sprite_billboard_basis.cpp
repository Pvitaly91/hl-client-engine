#include <hlclient/entity_render/sprite_billboard_basis.hpp>

#include <cmath>
#include <optional>

namespace hlclient::entity_render {
namespace {

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] float dot(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] assets::AssetVector3 cross(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] std::optional<assets::AssetVector3> normalize(
    const assets::AssetVector3& value) noexcept
{
    if (!finite(value)) {
        return std::nullopt;
    }
    const auto length_squared = static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12) {
        return std::nullopt;
    }
    const auto inverse = 1.0 / std::sqrt(length_squared);
    const assets::AssetVector3 result{
        static_cast<float>(value.x * inverse),
        static_cast<float>(value.y * inverse),
        static_cast<float>(value.z * inverse),
    };
    return finite(result) ? std::optional{result} : std::nullopt;
}

[[nodiscard]] bool nearly_orthogonal(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return std::fabs(dot(left, right)) <= 1.0e-4F;
}

} // namespace

std::string_view to_string(const SpriteBillboardBasisStatus status) noexcept
{
    switch (status) {
    case SpriteBillboardBasisStatus::supported_view_parallel:
        return "supported_view_parallel";
    case SpriteBillboardBasisStatus::supported_view_parallel_upright:
        return "supported_view_parallel_upright";
    case SpriteBillboardBasisStatus::supported_oriented:
        return "supported_oriented";
    case SpriteBillboardBasisStatus::unsupported_facing_upright_evidence_pending:
        return "unsupported_facing_upright_evidence_pending";
    case SpriteBillboardBasisStatus::
        unsupported_view_parallel_oriented_evidence_pending:
        return "unsupported_view_parallel_oriented_evidence_pending";
    case SpriteBillboardBasisStatus::non_finite_input:
        return "non_finite_input";
    case SpriteBillboardBasisStatus::degenerate_camera_basis:
        return "degenerate_camera_basis";
    case SpriteBillboardBasisStatus::degenerate_upright_basis:
        return "degenerate_upright_basis";
    case SpriteBillboardBasisStatus::degenerate_oriented_basis:
        return "degenerate_oriented_basis";
    }
    return "unknown";
}

SpriteBillboardBasisResult SpriteBillboardBasis::calculate(
    const SpriteBillboardInput& input) noexcept
{
    if (input.orientation == assets::SpriteOrientation::facing_upright) {
        return {{},
            {},
            {},
            SpriteBillboardBasisStatus::
                unsupported_facing_upright_evidence_pending};
    }
    if (input.orientation ==
        assets::SpriteOrientation::view_parallel_oriented) {
        return {{},
            {},
            {},
            SpriteBillboardBasisStatus::
                unsupported_view_parallel_oriented_evidence_pending};
    }

    if (input.orientation == assets::SpriteOrientation::view_parallel) {
        if (!finite(input.camera_right) || !finite(input.camera_up)) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::non_finite_input};
        }
        const auto right = normalize(input.camera_right);
        const auto up = normalize(input.camera_up);
        if (!right || !up || !nearly_orthogonal(*right, *up)) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::degenerate_camera_basis};
        }
        const auto normal = normalize(cross(*right, *up));
        if (!normal) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::degenerate_camera_basis};
        }
        return {*right,
            *up,
            *normal,
            SpriteBillboardBasisStatus::supported_view_parallel};
    }

    if (input.orientation ==
        assets::SpriteOrientation::view_parallel_upright) {
        if (!finite(input.camera_forward)) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::non_finite_input};
        }
        const auto forward = normalize(input.camera_forward);
        constexpr assets::AssetVector3 world_up{0.0F, 0.0F, 1.0F};
        const auto right = forward
            ? normalize(cross(*forward, world_up))
            : std::nullopt;
        if (!right) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::degenerate_upright_basis};
        }
        const auto normal = normalize(cross(*right, world_up));
        if (!normal) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::degenerate_upright_basis};
        }
        return {*right,
            world_up,
            *normal,
            SpriteBillboardBasisStatus::supported_view_parallel_upright};
    }

    if (input.orientation == assets::SpriteOrientation::oriented) {
        if (!finite(input.oriented_right) || !finite(input.oriented_up) ||
            !finite(input.oriented_forward)) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::non_finite_input};
        }
        const auto right = normalize(input.oriented_right);
        const auto up = normalize(input.oriented_up);
        const auto normal = normalize(input.oriented_forward);
        if (!right || !up || !normal || !nearly_orthogonal(*right, *up) ||
            !nearly_orthogonal(*right, *normal) ||
            !nearly_orthogonal(*up, *normal)) {
            return {{},
                {},
                {},
                SpriteBillboardBasisStatus::degenerate_oriented_basis};
        }
        return {*right,
            *up,
            *normal,
            SpriteBillboardBasisStatus::supported_oriented};
    }

    return {{},
        {},
        {},
        SpriteBillboardBasisStatus::degenerate_camera_basis};
}

} // namespace hlclient::entity_render
