#pragma once

#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <optional>

namespace hlclient::gameplay_camera {

// Narrow renderer-facing adapter. GameplayCameraState itself remains a
// renderer-independent value that networking code may consume without a
// transitive renderer dependency.
struct RenderCameraBuildResult {
    std::optional<renderer::RenderCamera> camera;
    std::optional<GameplayCameraError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return camera.has_value();
    }
};

[[nodiscard]] RenderCameraBuildResult build_render_camera(
    const GameplayCameraState& state) noexcept;

} // namespace hlclient::gameplay_camera
