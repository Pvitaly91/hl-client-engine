#pragma once
#include <hlclient/core/multiplayer_diagnostics.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/game_api/beam_effects.hpp>

namespace hlclient::app {
// One last swap-completed CPU scene proxy, not a display scanout timestamp.
// No rendering, GPU readback, gameplay mutation or independent event storage.
class MultiplayerPresentationDiagnostics final : public core::MultiplayerDiagnosticSink {
public:
    explicit MultiplayerPresentationDiagnostics(core::MultiplayerDiagnosticSink& sink) noexcept : sink_(sink) {}
    bool enabled() const noexcept override { return sink_.enabled(); }
    bool observe(const core::MultiplayerDiagnosticEvent&) noexcept override;
    void completed_frame(const renderer::RenderScene&, std::uint64_t frame,
        std::uint64_t network, std::uint64_t map, std::uint64_t completed_ns,
        std::uint32_t width, std::uint32_t height) noexcept;
    void set_world_query(const game_api::IStaticEffectTrace* query) noexcept { world_query_=query; }
    void invalidate() noexcept;
private:
    void query_action(const core::MultiplayerDiagnosticEvent&, const core::MultiplayerActionPayload&) noexcept;
    core::MultiplayerDiagnosticSink& sink_;
    const game_api::IStaticEffectTrace* world_query_{};
    renderer::RenderCamera camera_;
    std::optional<renderer::RenderDynamicEntities> entities_;
    std::uint64_t frame_{},network_{},map_{},completed_ns_{},event_id_{},query_id_{};
    std::uint32_t width_{},height_{};
    std::uint32_t last_pose_omissions_{};
};
// Exact affine-transformed local box intersection. Used by production queries
// and offline controls; absence is invalid geometry, not a negative hit result.
std::optional<double> diagnostic_ray_box(const std::array<float,3>& origin,
    const std::array<float,3>& direction, const std::array<float,16>& transform,
    const std::array<float,3>& minimum, const std::array<float,3>& maximum) noexcept;
}
