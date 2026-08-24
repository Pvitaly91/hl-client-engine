#pragma once

#include <hlclient/renderer/renderer.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace hlclient::renderer::null {

struct NullRendererStatistics {
    bool initialized{false};
    bool shutdown{false};
    std::uint64_t rendered_frames{0};
    ClearColor last_clear_color{};
    bool static_world_present{false};
    std::optional<std::uint64_t> package_resource_id;
    std::optional<std::uint64_t> package_revision;
    bool scene_package_present{false};
    std::optional<std::uint64_t> scene_resource_id;
    std::optional<std::uint64_t> scene_revision;
    bool visibility_present{false};
    bool visible_draw_list_present{false};
    std::optional<std::uint64_t> visibility_revision;
    std::size_t visible_world_surface_count{0U};
    std::size_t visible_brush_instance_count{0U};
    std::size_t visible_draw_command_count{0U};
    bool camera_valid{false};
    RenderExtent last_extent{};

    [[nodiscard]] friend bool operator==(
        const NullRendererStatistics& left,
        const NullRendererStatistics& right) = default;
};

class NullRenderer final : public IRenderer {
public:
    NullRenderer() = default;
    ~NullRenderer() noexcept override;

    NullRenderer(const NullRenderer&) = delete;
    NullRenderer& operator=(const NullRenderer&) = delete;
    NullRenderer(NullRenderer&&) = delete;
    NullRenderer& operator=(NullRenderer&&) = delete;

    [[nodiscard]] const RendererInfo& information() const noexcept override;
    void render(const RenderScene& scene, RenderExtent extent) override;

    void initialize() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] NullRendererStatistics statistics() const noexcept;

private:
    RendererInfo information_{"HL Client Engine", "Null Renderer", "1"};
    NullRendererStatistics statistics_;
};

} // namespace hlclient::renderer::null
