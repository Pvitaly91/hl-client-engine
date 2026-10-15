#pragma once

#include <hlclient/core/command_line.hpp>
#include <hlclient/renderer/render_scene.hpp>

namespace hlclient::app {

// The normal application and offline composition fixtures use the same final
// handoff. This does not import assets, select a game profile or alter HUD data.
inline void apply_application_scene_settings(renderer::RenderScene& scene,
        const core::CommandLineOptions& options) noexcept {
    scene.image_adjustments=options.image;
    scene.requested_msaa_samples=options.video.msaa_samples;
}

// Existing application percent -> existing mixer listener gain. There is one
// backend and no independent launcher audio subsystem.
[[nodiscard]] inline float application_audio_master_volume(
        const core::CommandLineOptions& options) noexcept {
    return static_cast<float>(options.audio_volume)/100.0F;
}

} // namespace hlclient::app
