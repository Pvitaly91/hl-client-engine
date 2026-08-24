#pragma once

#include <hlclient/world_render/world_render_types.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::world_render {

struct WorldRenderPackageLimits {
    std::size_t maximum_vertices{4U * 1024U * 1024U};
    std::size_t maximum_indices{12U * 1024U * 1024U};
    std::size_t maximum_materials{8'192U};
    std::size_t maximum_batches{65'535U};
    std::size_t maximum_base_texture_bytes{256U * 1024U * 1024U};
    std::size_t maximum_lightmap_bytes{256U * 1024U * 1024U};
    std::size_t maximum_total_cpu_render_bytes{768U * 1024U * 1024U};
};

enum class WorldRenderPackageErrorCode {
    invalid_prerequisite,
    texture_set_incomplete,
    lightmap_binding_mismatch,
    invalid_surface_range,
    invalid_material_binding,
    invalid_texture_dimensions,
    invalid_render_coordinate,
    invalid_atlas_binding,
    batch_limit_exceeded,
    output_limit_exceeded,
    unable_to_retain_package,
};

[[nodiscard]] std::string_view to_string(WorldRenderPackageErrorCode code) noexcept;

struct WorldRenderPackageError {
    WorldRenderPackageErrorCode code{WorldRenderPackageErrorCode::invalid_prerequisite};
    std::optional<std::size_t> element_index;
    std::string context;
};

struct WorldRenderPackageBuildResult {
    std::optional<WorldRenderPackage> package;
    std::optional<WorldRenderPackageError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return package.has_value();
    }
};

class WorldRenderPackageBuilder final {
public:
    [[nodiscard]] WorldRenderPackageBuildResult build(
        assets::TexturedWorldAsset textured_world,
        assets::WorldLightmapSet lightmaps,
        const WorldRenderPackageLimits& limits = {}) const;
};

} // namespace hlclient::world_render
