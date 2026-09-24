#pragma once

#include <cstddef>
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
    live_runtime_state,
    live_usercmd_check,
    live_visual_control,
    usercmd_boundary,
    precache_manifest,
    asset_dispatch,
    world_geometry,
    collision_world,
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
    steam,
};

enum class LiveInputMode {
    keyboard_mouse,
    scripted_check,
    scripted_side_check,
    scripted_jump_duck_check,
    scripted_speed_check,
};

enum class ResourceConsistencyProviderKind {
    local,
};

enum class RuntimeReplayFixtureOption {
    basic_mixed,
    missing_entity_base,
    visual_entities,
};

enum class RuntimeReplayVisualOption {
    diagnostic,
    local_assets,
};

struct CommandLineOptions {
    bool show_help{false};
    bool show_version{false};
    bool net_trace{false};
    bool view_world{false};
    bool view_entity_snapshot{false};
    std::optional<RuntimeReplayFixtureOption> runtime_replay_fixture;
    std::optional<std::string> runtime_replay_capture;
    std::optional<RuntimeReplayVisualOption> runtime_replay_visuals;
    std::optional<std::string> runtime_replay_screenshot;
    std::size_t runtime_replay_record_budget{2U};
    std::size_t runtime_replay_byte_budget{1U * 1'024U * 1'024U};
    std::optional<std::string> base_directory;
    std::string game_directory{"valve"};
    std::optional<std::string> connect_endpoint;
    ConnectionStopPoint stop_after{ConnectionStopPoint::challenge};
    std::optional<LiveInputMode> live_input;
    bool reference_prediction{false};
    std::optional<std::size_t> live_session_seconds;
    std::optional<AuthenticationProviderKind> authentication_provider;
    std::optional<std::string> authentication_material_file;
    std::optional<std::string> steam_api_runtime;
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
