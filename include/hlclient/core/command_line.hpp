#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::core {

enum class RendererBackend {
    opengl,
    null,
};

enum class ConnectionStopPoint {
    challenge,
    connect_request,
    connect_response,
    netchan_bootstrap,
    signon_boundary,
    pre_resource,
    delta_schemas,
    movevars,
    user_info,
    resource_list_boundary,
    resource_list,
    resource_response_boundary,
    server_baselines,
    entity_snapshot,
    usercmd_boundary,
    precache_manifest,
    asset_dispatch,
    world_geometry,
    world_textures,
    world_render_package,
    world_spatial_scene,
    entity_visual_scene,
};

enum class WorldVisibilityOption {
    all,
    frustum,
    pvs,
    pvs_frustum,
};

enum class BrushSubmodelsOption {
    off,
    static_initial,
};

enum class WorldCameraOption {
    static_camera,
    orbit,
    spawn,
};

enum class AuthenticationProviderKind {
    file,
};

enum class ResourceConsistencyProviderKind {
    local,
};

struct CommandLineOptions {
    bool show_help{false};
    bool show_version{false};
    bool net_trace{false};
    bool view_world{false};
    bool view_entity_snapshot{false};
    std::optional<std::string> base_directory;
    std::string game_directory{"valve"};
    std::optional<std::string> connect_endpoint;
    ConnectionStopPoint stop_after{ConnectionStopPoint::challenge};
    std::optional<AuthenticationProviderKind> authentication_provider;
    std::optional<std::string> authentication_material_file;
    std::optional<ResourceConsistencyProviderKind>
        resource_consistency_provider;
    std::string player_name{"Player"};
    std::string player_model{"ivan"};
    RendererBackend renderer{RendererBackend::opengl};
    WorldVisibilityOption world_visibility{WorldVisibilityOption::all};
    BrushSubmodelsOption brush_submodels{BrushSubmodelsOption::off};
    WorldCameraOption world_camera{WorldCameraOption::static_camera};
};

struct CommandLineParseResult {
    std::optional<CommandLineOptions> options;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return options.has_value();
    }
};

[[nodiscard]] CommandLineParseResult parse_command_line(
    std::span<const std::string_view> arguments);
[[nodiscard]] bool requires_local_resource_consistency_preparation(
    const CommandLineOptions& options) noexcept;
[[nodiscard]] std::string_view command_line_help() noexcept;

} // namespace hlclient::core
