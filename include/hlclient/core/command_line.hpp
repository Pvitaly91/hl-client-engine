#pragma once

#include <cstddef>
#include <cstdint>
#include <hlclient/core/presentation_settings.hpp>
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
    scripted_weapon_check,
    scripted_fire_reload_check,
    scripted_fire_reload_presentation_check,
    scripted_damage_respawn_check,
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
    // Direct external client configuration is distinct from research CLI.
    bool external_server_client{};
    // Application idle/console startup, not a connection or research stop.
    bool open_client{};
    std::optional<std::string> external_config;
    std::optional<std::uintptr_t> external_cancel_event;
    VideoSettings video;
    ImageAdjustments image;
    HudScaleSettings hud_scale;
    bool show_help{false};
    bool show_version{false};
    bool net_trace{false};
    unsigned audio_volume{35}; // application percent; zero is mute
    bool audio_on_focus_loss{false}; // explicit manual listener, default unchanged
    bool mute_glock_fire_sound{false}; // manual audio-isolation diagnostic, not gameplay
    bool hud_draw{true}, crosshair{true}, hud_fastswitch{}, hud_diagnostics{};
    bool movement_diagnostics{}; // bounded, opt-in movement incident recording
    // Optional session presentation evidence from the owned server query;
    // absence remains unknown and never supplies a server cvar assignment.
    std::optional<bool> server_blood_presentation;
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
    bool test_start_health{false}; // owned-server manual harness; never a health override
    std::optional<std::size_t> live_session_seconds;
    bool live_session_unlimited{false}; // explicit user-ended keyboard session
    // Owned runner output identity; all three are required together. These
    // configure structured publication only, never network/game commands.
    std::optional<std::string> session_receipt_root;
    std::optional<std::string> session_run_id;
    std::optional<std::string> session_role;
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
    bool debug_freeze_tracer{false}; // opt-in manual geometry inspection, not gameplay
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
