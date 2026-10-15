#pragma once
#include <hlclient/renderer/render_scene.hpp>
#include <array>
#include <cstddef>

namespace hlclient::renderer {
struct RenderLineVertex final {
    assets::AssetVector3 position{}, normal{};
};
// Twelve sides, four-ring leading cap and optional four-ring narrowed rear cap.
// At most 216 triangles; omitted end diameter preserves the sharp/flat rear.
// Owning, allocation-free and independent of camera/time/game/asset identity.
struct RenderLineMesh final {
    std::array<RenderLineVertex,648U> vertices{};
    std::size_t vertex_count{};
};
// Invalid/flat/degenerate inputs return an empty mesh; no silent fallback.
// Width is the full diameter. Geometry stays between start and end, with
// outward CCW winding. Taper zero is a front-rounded cylinder; one is a closed
// rear-pointing cone unless a positive terminal diameter selects a frustum and
// rounded cap. Cap equator diameter is width * taper_end_diameter_fraction;
// cap axial length is min(equator radius, half taper length), so short clipped
// segments use a half-ellipsoid. Historical head_taper_fraction is taper length.
// Only mesh orientation is reversed, not start -> end flight: rounding
// stays inside the segment and the flat-billboard variant is unchanged.
[[nodiscard]] RenderLineMesh solid_line_mesh(const RenderWorldLine&) noexcept;
} // namespace hlclient::renderer
