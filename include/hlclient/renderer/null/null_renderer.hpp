#pragma once

#include <hlclient/renderer/renderer.hpp>

#include <cstdint>
#include <optional>

namespace hlclient::renderer::null {

struct NullRendererStatistics {
    bool initialized{false};
    bool shutdown{false};
    std::uint64_t rendered_frames{0};
    std::optional<RenderScene> last_scene;
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
