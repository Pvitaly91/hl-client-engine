#include <hlclient/core/command_line.hpp>

#include <charconv>
#include <limits>
#include <utility>

namespace hlclient::core {
namespace {

[[nodiscard]] CommandLineParseResult failure(std::string message)
{
    return CommandLineParseResult{std::nullopt, std::move(message)};
}

[[nodiscard]] bool needs_value(const std::string_view argument) noexcept
{
    return argument == "--basedir" || argument == "--game" || argument == "--connect" ||
           argument == "+connect" || argument == "--renderer" ||
           argument == "--stop-after" || argument == "--auth-provider" ||
           argument == "--auth-material-file" || argument == "--steam-api-runtime" ||
           argument == "--resource-consistency-provider" ||
           argument == "--name" || argument == "--model" ||
           argument == "--visibility" || argument == "--brush-submodels" ||
           argument == "--camera" ||
           argument == "--runtime-replay-fixture" ||
           argument == "--runtime-replay-capture" ||
           argument == "--runtime-replay-visuals" ||
           argument == "--runtime-replay-screenshot" ||
           argument == "--runtime-replay-record-budget" ||
           argument == "--runtime-replay-byte-budget" ||
           argument == "--live-input" || argument == "--prediction" ||
           argument == "--live-session-seconds";
}

[[nodiscard]] std::optional<std::size_t> positive_size(
    const std::string_view text,
    const std::size_t maximum) noexcept
{
    std::uint64_t parsed = 0U;
    const auto converted = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (converted.ec != std::errc{} ||
        converted.ptr != text.data() + text.size() || parsed == 0U ||
        parsed > maximum ||
        parsed > (std::numeric_limits<std::size_t>::max)()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(parsed);
}

} // namespace

CommandLineParseResult parse_command_line(const std::span<const std::string_view> arguments)
{
    CommandLineOptions options;
    bool stop_after_seen = false;
    bool connect_request_setting_seen = false;
    bool resource_consistency_provider_seen = false;
    bool visibility_seen = false;
    bool brush_submodels_seen = false;
    bool camera_seen = false;
    bool runtime_replay_fixture_seen = false;
    bool runtime_replay_capture_seen = false;
    bool runtime_replay_visuals_seen = false;
    bool runtime_replay_record_budget_seen = false;
    bool runtime_replay_byte_budget_seen = false;
    bool live_input_seen = false;
    bool prediction_seen = false;
    bool live_session_seconds_seen = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }
        if (argument == "--version") {
            options.show_version = true;
            continue;
        }
        if (argument == "--net-trace") {
            options.net_trace = true;
            continue;
        }
        if (argument == "--view-world") {
            if (options.view_world) {
                return failure("--view-world may be specified only once");
            }
            options.view_world = true;
            continue;
        }
        if (argument == "--view-entity-snapshot") {
            if (options.view_entity_snapshot) {
                return failure(
                    "--view-entity-snapshot may be specified only once");
            }
            options.view_entity_snapshot = true;
            continue;
        }
        if (!needs_value(argument)) {
            return failure("Unknown command-line argument: " + std::string{argument});
        }
        if (index + 1 >= arguments.size()) {
            return failure("Missing value after " + std::string{argument});
        }

        const auto value = arguments[++index];
        if (value.empty()) {
            return failure("Empty value after " + std::string{argument});
        }

        if (argument == "--basedir") {
            options.base_directory = std::string{value};
        } else if (argument == "--game") {
            options.game_directory = std::string{value};
        } else if (argument == "--renderer") {
            if (value == "opengl") {
                options.renderer = RendererBackend::opengl;
            } else if (value == "null") {
                options.renderer = RendererBackend::null;
            } else {
                return failure("Unsupported renderer: " + std::string{value} +
                               " (expected opengl or null)");
            }
        } else if (argument == "--stop-after") {
            stop_after_seen = true;
            if (value == "challenge") {
                options.stop_after = ConnectionStopPoint::challenge;
            } else if (value == "connect-request") {
                options.stop_after = ConnectionStopPoint::connect_request;
            } else if (value == "connect-response") {
                options.stop_after = ConnectionStopPoint::connect_response;
            } else if (value == "netchan-bootstrap") {
                options.stop_after = ConnectionStopPoint::netchan_bootstrap;
            } else if (value == "signon-boundary") {
                options.stop_after = ConnectionStopPoint::signon_boundary;
            } else if (value == "pre-resource") {
                options.stop_after = ConnectionStopPoint::pre_resource;
            } else if (value == "delta-schemas") {
                options.stop_after = ConnectionStopPoint::delta_schemas;
            } else if (value == "movevars") {
                options.stop_after = ConnectionStopPoint::movevars;
            } else if (value == "user-info") {
                options.stop_after = ConnectionStopPoint::user_info;
            } else if (value == "resource-list-boundary") {
                options.stop_after = ConnectionStopPoint::resource_list_boundary;
            } else if (value == "resource-list") {
                options.stop_after = ConnectionStopPoint::resource_list;
            } else if (value == "resource-response-boundary") {
                options.stop_after = ConnectionStopPoint::resource_response_boundary;
            } else if (value == "server-baselines") {
                options.stop_after = ConnectionStopPoint::server_baselines;
            } else if (value == "entity-snapshot") {
                options.stop_after = ConnectionStopPoint::entity_snapshot;
            } else if (value == "live-runtime-state") {
                options.stop_after = ConnectionStopPoint::live_runtime_state;
            } else if (value == "live-usercmd-check") {
                options.stop_after = ConnectionStopPoint::live_usercmd_check;
            } else if (value == "live-visual-control") {
                options.stop_after = ConnectionStopPoint::live_visual_control;
            } else if (value == "usercmd-boundary") {
                options.stop_after = ConnectionStopPoint::usercmd_boundary;
            } else if (value == "precache-manifest") {
                options.stop_after = ConnectionStopPoint::precache_manifest;
            } else if (value == "asset-dispatch") {
                options.stop_after = ConnectionStopPoint::asset_dispatch;
            } else if (value == "world-geometry") {
                options.stop_after = ConnectionStopPoint::world_geometry;
            } else if (value == "collision-world") {
                options.stop_after = ConnectionStopPoint::collision_world;
            } else if (value == "world-textures") {
                options.stop_after = ConnectionStopPoint::world_textures;
            } else if (value == "world-render-package") {
                options.stop_after = ConnectionStopPoint::world_render_package;
            } else if (value == "world-spatial-scene") {
                options.stop_after = ConnectionStopPoint::world_spatial_scene;
            } else if (value == "entity-visual-scene") {
                options.stop_after = ConnectionStopPoint::entity_visual_scene;
            } else {
                return failure("Unsupported --stop-after value: " + std::string{value} +
                               " (expected challenge, connect-request, connect-response, "
                               "netchan-bootstrap, signon-boundary, pre-resource, "
                               "delta-schemas, movevars, user-info, or "
                               "resource-list-boundary, resource-list, or "
                               "resource-response-boundary, server-baselines, "
                                "entity-snapshot, live-runtime-state, live-usercmd-check, live-visual-control, usercmd-boundary, precache-manifest, or "
                               "asset-dispatch, world-geometry, collision-world, world-textures, or "
                               "world-render-package, or world-spatial-scene)");
            }
        } else if (argument == "--prediction") {
            if (prediction_seen)
                return failure("--prediction may be specified only once");
            prediction_seen = true;
            if (value == "reference")
                options.reference_prediction = true;
            else if (value != "off")
                return failure("Unsupported --prediction value (expected off or reference)");
        } else if (argument == "--live-input") {
            if (live_input_seen) {
                return failure("--live-input may be specified only once");
            }
            live_input_seen = true;
            if (value == "keyboard-mouse") {
                options.live_input = LiveInputMode::keyboard_mouse;
            } else if (value == "scripted-check") {
                options.live_input = LiveInputMode::scripted_check;
            } else if (value == "scripted-side-check") {
                options.live_input = LiveInputMode::scripted_side_check;
            } else if (value == "scripted-jump-duck-check") {
                options.live_input = LiveInputMode::scripted_jump_duck_check;
            } else if (value == "scripted-speed-check") {
                options.live_input = LiveInputMode::scripted_speed_check;
            } else {
                return failure(
                    "Unsupported --live-input value: " + std::string{value} +
                    " (expected keyboard-mouse, scripted-check, scripted-side-check, scripted-jump-duck-check or scripted-speed-check)");
            }
        } else if (argument == "--live-session-seconds") {
            if (live_session_seconds_seen) {
                return failure(
                    "--live-session-seconds may be specified only once");
            }
            live_session_seconds_seen = true;
            options.live_session_seconds = positive_size(value, 300U);
            if (!options.live_session_seconds) {
                return failure(
                    "--live-session-seconds must be in range 1..300");
            }
        } else if (argument == "--auth-provider") {
            connect_request_setting_seen = true;
            if (value == "file") {
                options.authentication_provider = AuthenticationProviderKind::file;
            } else if (value == "steam") {
                options.authentication_provider = AuthenticationProviderKind::steam;
            } else {
                return failure("Unsupported authentication provider: " + std::string{value} +
                               " (expected file or steam)");
            }
        } else if (argument == "--auth-material-file") {
            connect_request_setting_seen = true;
            options.authentication_material_file = std::string{value};
        } else if (argument == "--steam-api-runtime") {
            connect_request_setting_seen = true;
            options.steam_api_runtime = std::string{value};
        } else if (argument == "--resource-consistency-provider") {
            resource_consistency_provider_seen = true;
            if (value != "local") {
                return failure(
                    "Unsupported resource-consistency provider: " +
                    std::string{value} + " (expected local)");
            }
            options.resource_consistency_provider =
                ResourceConsistencyProviderKind::local;
        } else if (argument == "--name") {
            connect_request_setting_seen = true;
            options.player_name = std::string{value};
        } else if (argument == "--model") {
            connect_request_setting_seen = true;
            options.player_model = std::string{value};
        } else if (argument == "--visibility") {
            if (visibility_seen) {
                return failure("--visibility may be specified only once");
            }
            visibility_seen = true;
            if (value == "all") {
                options.world_visibility = WorldVisibilityOption::all;
            } else if (value == "frustum") {
                options.world_visibility = WorldVisibilityOption::frustum;
            } else if (value == "pvs") {
                options.world_visibility = WorldVisibilityOption::pvs;
            } else if (value == "pvs-frustum") {
                options.world_visibility = WorldVisibilityOption::pvs_frustum;
            } else {
                return failure(
                    "Unsupported --visibility value: " + std::string{value} +
                    " (expected all, frustum, pvs, or pvs-frustum)");
            }
        } else if (argument == "--brush-submodels") {
            if (brush_submodels_seen) {
                return failure("--brush-submodels may be specified only once");
            }
            brush_submodels_seen = true;
            if (value == "off") {
                options.brush_submodels = BrushSubmodelsOption::off;
            } else if (value == "static") {
                options.brush_submodels = BrushSubmodelsOption::static_initial;
            } else {
                return failure(
                    "Unsupported --brush-submodels value: " +
                    std::string{value} + " (expected off or static)");
            }
        } else if (argument == "--camera") {
            if (camera_seen) {
                return failure("--camera may be specified only once");
            }
            camera_seen = true;
            if (value == "static") {
                options.world_camera = WorldCameraOption::static_camera;
            } else if (value == "orbit") {
                options.world_camera = WorldCameraOption::orbit;
            } else if (value == "spawn") {
                options.world_camera = WorldCameraOption::spawn;
            } else {
                return failure(
                    "Unsupported --camera value: " + std::string{value} +
                    " (expected static, orbit, or spawn)");
            }
        } else if (argument == "--runtime-replay-fixture") {
            if (runtime_replay_fixture_seen) {
                return failure(
                    "--runtime-replay-fixture may be specified only once");
            }
            runtime_replay_fixture_seen = true;
            if (value == "basic-mixed") {
                options.runtime_replay_fixture =
                    RuntimeReplayFixtureOption::basic_mixed;
            } else if (value == "missing-entity-base") {
                options.runtime_replay_fixture =
                    RuntimeReplayFixtureOption::missing_entity_base;
            } else if (value == "visual-entities") {
                options.runtime_replay_fixture =
                    RuntimeReplayFixtureOption::visual_entities;
            } else {
                return failure(
                    "Unsupported --runtime-replay-fixture value: " +
                    std::string{value} +
                    " (expected basic-mixed, missing-entity-base, or visual-entities)");
            }
        } else if (argument == "--runtime-replay-capture") {
            if (runtime_replay_capture_seen) {
                return failure(
                    "--runtime-replay-capture may be specified only once");
            }
            runtime_replay_capture_seen = true;
            if (value.empty()) {
                return failure("--runtime-replay-capture requires a run path");
            }
            options.runtime_replay_capture = std::string{value};
        } else if (argument == "--runtime-replay-screenshot") {
            if (options.runtime_replay_screenshot) { return failure("Replay screenshot output may be specified only once"); }
            options.runtime_replay_screenshot=std::string{value};
        } else if (argument == "--runtime-replay-visuals") {
            if (runtime_replay_visuals_seen) {
                return failure(
                    "--runtime-replay-visuals may be specified only once");
            }
            runtime_replay_visuals_seen = true;
            if (value != "diagnostic" && value != "local-assets") {
                return failure(
                    "Unsupported --runtime-replay-visuals value: " +
                    std::string{value} + " (expected diagnostic or local-assets)");
            }
            options.runtime_replay_visuals =
                value == "diagnostic" ? RuntimeReplayVisualOption::diagnostic : RuntimeReplayVisualOption::local_assets;
        } else if (argument == "--runtime-replay-record-budget") {
            if (runtime_replay_record_budget_seen) {
                return failure(
                    "--runtime-replay-record-budget may be specified only once");
            }
            runtime_replay_record_budget_seen = true;
            const auto parsed = positive_size(value, 1'024U);
            if (!parsed) {
                return failure(
                    "Invalid --runtime-replay-record-budget value "
                    "(expected 1..1024)");
            }
            options.runtime_replay_record_budget = *parsed;
        } else if (argument == "--runtime-replay-byte-budget") {
            if (runtime_replay_byte_budget_seen) {
                return failure(
                    "--runtime-replay-byte-budget may be specified only once");
            }
            runtime_replay_byte_budget_seen = true;
            const auto parsed = positive_size(value, 16U * 1'024U * 1'024U);
            if (!parsed) {
                return failure(
                    "Invalid --runtime-replay-byte-budget value "
                    "(expected 1..16777216)");
            }
            options.runtime_replay_byte_budget = *parsed;
        } else {
            options.connect_endpoint = std::string{value};
        }
    }

    if (options.runtime_replay_fixture || options.runtime_replay_capture) {
        if (options.runtime_replay_fixture && options.runtime_replay_capture) {
            return failure(
                "--runtime-replay-fixture and --runtime-replay-capture are mutually exclusive");
        }
        const bool local_assets=options.runtime_replay_visuals==RuntimeReplayVisualOption::local_assets;
        if (options.runtime_replay_screenshot && (!local_assets || options.renderer!=RendererBackend::opengl)) {
            return failure("Replay screenshot requires local-assets OpenGL replay");
        }
        if (local_assets && (!options.runtime_replay_capture || !options.base_directory || options.game_directory!="valve")) {
            return failure("local-assets requires capture replay, explicit --basedir, and --game valve");
        }
        if (options.runtime_replay_capture && options.runtime_replay_visuals && !local_assets) {
            return failure(
                "--runtime-replay-visuals is not enabled for capture-backed replay");
        }
        if (options.runtime_replay_visuals && !local_assets &&
            (!options.runtime_replay_fixture ||
             *options.runtime_replay_fixture !=
                RuntimeReplayFixtureOption::visual_entities)) {
            return failure(
                "--runtime-replay-visuals diagnostic requires the visual-entities fixture");
        }
        if (options.runtime_replay_fixture &&
            *options.runtime_replay_fixture ==
                RuntimeReplayFixtureOption::visual_entities &&
            !options.runtime_replay_visuals) {
            return failure(
                "The visual-entities fixture requires --runtime-replay-visuals diagnostic");
        }
        if (!options.runtime_replay_visuals &&
            options.renderer != RendererBackend::null) {
            return failure("Non-visual runtime replay requires --renderer null");
        }
        if (options.connect_endpoint || (options.base_directory && !local_assets) ||
            options.authentication_provider ||
            options.authentication_material_file ||
            options.resource_consistency_provider || stop_after_seen ||
            options.view_world || options.view_entity_snapshot ||
            connect_request_setting_seen || resource_consistency_provider_seen ||
            visibility_seen || brush_submodels_seen || camera_seen ||
            live_input_seen || live_session_seconds_seen) {
            return failure(
                "Offline runtime replay is incompatible with connect, asset, "
                "authentication, stop, view, visibility, brush, and camera options");
        }
        return CommandLineParseResult{std::move(options), {}};
    }
    if (options.runtime_replay_screenshot) { return failure("Replay screenshot requires capture replay"); }
    if (runtime_replay_visuals_seen) {
        return failure(
            "--runtime-replay-visuals requires --runtime-replay-fixture");
    }
    if (runtime_replay_record_budget_seen || runtime_replay_byte_budget_seen) {
        return failure(
            "Runtime replay budgets require --runtime-replay-fixture or "
            "--runtime-replay-capture");
    }
    if (options.stop_after == ConnectionStopPoint::live_visual_control) {
        if (!options.live_input) {
            return failure(
                "--stop-after live-visual-control requires --live-input");
        }
        if (options.renderer != RendererBackend::opengl) {
            return failure("live-visual-control requires --renderer opengl");
        }
        if (!options.base_directory || options.game_directory != "valve") {
            return failure(
                "live-visual-control requires explicit --basedir and --game valve");
        }
        if (options.live_session_seconds &&
            options.live_input != LiveInputMode::keyboard_mouse) {
            return failure(
                "--live-session-seconds requires keyboard-mouse live input");
        }
    } else if (live_input_seen || live_session_seconds_seen || prediction_seen) {
        return failure(
            "live input options require --stop-after live-visual-control");
    }

    if (options.view_world) {
        if (stop_after_seen &&
            options.stop_after != ConnectionStopPoint::world_spatial_scene) {
            return failure(
                "--view-world is compatible only with --stop-after "
                "world-spatial-scene");
        }
        options.stop_after = ConnectionStopPoint::world_spatial_scene;
    }
    if (options.view_world && options.view_entity_snapshot) {
        return failure(
            "--view-world and --view-entity-snapshot are mutually exclusive");
    }
    if (options.view_entity_snapshot) {
        if (stop_after_seen &&
            options.stop_after != ConnectionStopPoint::entity_visual_scene) {
            return failure(
                "--view-entity-snapshot is compatible only with --stop-after "
                "entity-visual-scene");
        }
        options.stop_after = ConnectionStopPoint::entity_visual_scene;
    }
    if ((visibility_seen || brush_submodels_seen || camera_seen) &&
        !options.view_world && !options.view_entity_snapshot &&
        options.stop_after != ConnectionStopPoint::world_spatial_scene &&
        options.stop_after != ConnectionStopPoint::entity_visual_scene) {
        return failure(
            "--visibility, --brush-submodels, and --camera require "
            "--view-world, --view-entity-snapshot, --stop-after "
            "world-spatial-scene, or --stop-after entity-visual-scene");
    }
    if ((stop_after_seen || options.view_world || options.view_entity_snapshot ||
         connect_request_setting_seen) &&
        !options.connect_endpoint) {
        return failure("Connect-request options require --connect <ip:port>");
    }
    if (resource_consistency_provider_seen && !options.connect_endpoint) {
        return failure(
            "--resource-consistency-provider requires --connect <ip:port>");
    }
    if (options.resource_consistency_provider && !options.base_directory) {
        return failure(
            "The local resource-consistency provider requires explicit "
            "--basedir <Half-Life root>");
    }
    if (options.stop_after == ConnectionStopPoint::challenge &&
        connect_request_setting_seen) {
        return failure("--auth-provider, --auth-material-file, --name, and --model require "
                       "a connect-request, connect-response, netchan-bootstrap, or "
                       "signon-boundary/pre-resource/delta-schemas/movevars/"
                       "user-info/resource-list-boundary/resource-list/"
                       "resource-response-boundary/server-baselines/"
                        "entity-snapshot/live-runtime-state/live-usercmd-check/live-visual-control/usercmd-boundary/precache-manifest/"
                       "asset-dispatch/world-geometry/collision-world/world-textures/"
                       "world-render-package/world-spatial-scene/"
                       "entity-visual-scene stop point or a preview option");
    }
    if (options.authentication_provider == AuthenticationProviderKind::file &&
        !options.authentication_material_file) {
        return failure("The file authentication provider requires --auth-material-file");
    }
    if (options.authentication_provider == AuthenticationProviderKind::steam &&
        !options.steam_api_runtime) {
        return failure("The Steam authentication provider requires --steam-api-runtime");
    }
    if (options.authentication_provider == AuthenticationProviderKind::steam &&
        options.authentication_material_file) {
        return failure("The Steam authentication provider does not accept --auth-material-file");
    }
    if (options.authentication_provider != AuthenticationProviderKind::steam &&
        options.steam_api_runtime) {
        return failure("--steam-api-runtime requires --auth-provider steam");
    }
    if (options.stop_after != ConnectionStopPoint::challenge &&
        !options.authentication_provider && !options.authentication_material_file) {
        return failure(
            "Connect request, response, netchan, and sign-on modes require "
            "an explicit authentication provider");
    }
    if ((options.stop_after == ConnectionStopPoint::netchan_bootstrap ||
         options.stop_after == ConnectionStopPoint::signon_boundary ||
         options.stop_after == ConnectionStopPoint::pre_resource ||
         options.stop_after == ConnectionStopPoint::delta_schemas ||
         options.stop_after == ConnectionStopPoint::movevars ||
         options.stop_after == ConnectionStopPoint::user_info ||
         options.stop_after == ConnectionStopPoint::resource_list_boundary ||
         options.stop_after == ConnectionStopPoint::resource_list ||
         options.stop_after == ConnectionStopPoint::resource_response_boundary ||
         options.stop_after == ConnectionStopPoint::server_baselines ||
          options.stop_after == ConnectionStopPoint::entity_snapshot ||
          options.stop_after == ConnectionStopPoint::live_runtime_state ||
          options.stop_after == ConnectionStopPoint::live_usercmd_check ||
          options.stop_after == ConnectionStopPoint::live_visual_control ||
          options.stop_after == ConnectionStopPoint::usercmd_boundary ||
         options.stop_after == ConnectionStopPoint::precache_manifest ||
         options.stop_after == ConnectionStopPoint::asset_dispatch ||
         options.stop_after == ConnectionStopPoint::world_geometry ||
         options.stop_after == ConnectionStopPoint::collision_world ||
         options.stop_after == ConnectionStopPoint::world_textures ||
         options.stop_after == ConnectionStopPoint::world_render_package ||
         options.stop_after == ConnectionStopPoint::world_spatial_scene ||
         options.stop_after == ConnectionStopPoint::entity_visual_scene) &&
        !options.authentication_provider) {
        return failure(
            "Netchan bootstrap and sign-on require the explicit "
            "--auth-provider file or steam selection");
    }
    if (options.authentication_material_file && !options.authentication_provider) {
        // Preserve the M2.1/M2.2 spelling where the explicit material path
        // selected the only available provider implicitly.
        options.authentication_provider = AuthenticationProviderKind::file;
    }
    if (options.stop_after == ConnectionStopPoint::precache_manifest &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The precache-manifest stop point requires "
            "--resource-consistency-provider local");
    }
    if ((options.stop_after == ConnectionStopPoint::server_baselines ||
         options.stop_after == ConnectionStopPoint::entity_snapshot ||
         options.stop_after == ConnectionStopPoint::usercmd_boundary) &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The server-baselines, entity-snapshot, and usercmd-boundary stop points require "
            "--resource-consistency-provider local");
    }
    if ((options.stop_after == ConnectionStopPoint::live_runtime_state ||
         options.stop_after == ConnectionStopPoint::live_usercmd_check ||
         options.stop_after == ConnectionStopPoint::live_visual_control) &&
        options.resource_consistency_provider) {
        return failure(
            "The live runtime stops advertise no client custom resource "
            "and does not accept --resource-consistency-provider");
    }
    if (options.stop_after == ConnectionStopPoint::asset_dispatch &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The asset-dispatch stop point requires "
            "--resource-consistency-provider local");
    }
    if (options.stop_after == ConnectionStopPoint::world_geometry &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The world-geometry stop point requires "
            "--resource-consistency-provider local");
    }
    if (options.stop_after == ConnectionStopPoint::collision_world &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The collision-world stop point requires "
            "--resource-consistency-provider local");
    }
    if (options.stop_after == ConnectionStopPoint::world_textures &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The world-textures stop point requires "
            "--resource-consistency-provider local");
    }
    if (options.stop_after == ConnectionStopPoint::world_render_package &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The world-render-package boundary requires "
            "--resource-consistency-provider local");
    }
    if (options.stop_after == ConnectionStopPoint::world_spatial_scene &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The world-spatial-scene boundary requires "
            "--resource-consistency-provider local");
    }
    if (options.stop_after == ConnectionStopPoint::entity_visual_scene &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The entity-visual-scene boundary requires "
            "--resource-consistency-provider local");
    }
    if (options.view_world && options.renderer != RendererBackend::opengl) {
        return failure("--view-world requires --renderer opengl");
    }
    if (options.view_entity_snapshot &&
        options.renderer != RendererBackend::opengl) {
        return failure("--view-entity-snapshot requires --renderer opengl");
    }

    return CommandLineParseResult{std::move(options), {}};
}

bool requires_local_resource_consistency_preparation(
    const CommandLineOptions& options) noexcept
{
    return options.resource_consistency_provider ==
               ResourceConsistencyProviderKind::local &&
           (options.stop_after ==
                ConnectionStopPoint::resource_response_boundary ||
            options.stop_after == ConnectionStopPoint::server_baselines ||
            options.stop_after == ConnectionStopPoint::entity_snapshot ||
            options.stop_after == ConnectionStopPoint::usercmd_boundary ||
            options.stop_after == ConnectionStopPoint::precache_manifest ||
            options.stop_after == ConnectionStopPoint::asset_dispatch ||
            options.stop_after == ConnectionStopPoint::world_geometry ||
            options.stop_after == ConnectionStopPoint::collision_world ||
            options.stop_after == ConnectionStopPoint::world_textures ||
            options.stop_after == ConnectionStopPoint::world_render_package ||
            options.stop_after == ConnectionStopPoint::world_spatial_scene ||
            options.stop_after == ConnectionStopPoint::entity_visual_scene);
}

std::string_view command_line_help() noexcept
{
    return R"(Usage: hlclient [options]

Options:
  --help, -h          Show this help text and exit
  --version           Show version information and exit
  --basedir <path>    Half-Life installation directory
  --game <directory>  Game directory below basedir (default: valve)
  --connect <ip:port> Start a GoldSrc handshake (challenge-only by default)
  +connect <ip:port>  GoldSrc-style alias for --connect
  --stop-after <stage> Stop after challenge, connect-request, connect-response,
                       netchan-bootstrap, signon-boundary, pre-resource,
                       delta-schemas, movevars, user-info, or
                       resource-list-boundary, resource-list, or
                       resource-response-boundary, server-baselines,
                       entity-snapshot, live-runtime-state, live-usercmd-check,
                       live-visual-control,
                       usercmd-boundary, precache-manifest, or
                       asset-dispatch, world-geometry, collision-world,
                       world-textures, or
                       world-render-package, world-spatial-scene, or
                       entity-visual-scene
                       (default: challenge)
  --auth-provider <name>
                      Authentication provider for connect stages: file or steam
  --auth-material-file <path>
                      Local 245-byte auth input for file provider; never logged
  --steam-api-runtime <path>
                      Absolute path to the user's compatible 32-bit steam_api.dll;
                      used only with --auth-provider steam and never copied
  --resource-consistency-provider <name>
                      Explicit read-only response provider: local; requires
                      --basedir and is prepared only for resource-response-boundary,
                      server-baselines, entity-snapshot, usercmd-boundary,
                      precache-manifest, asset-dispatch, world-geometry,
                      collision-world, world-textures, world-render-package,
                      world-spatial-scene, or entity-visual-scene
  --name <name>       Player name, max 31 printable ASCII bytes (default: Player)
  --model <model>     Player model, max 31 printable ASCII bytes (default: ivan)
  --net-trace         Log bounded diagnostics; connect payload/auth bytes are redacted
  --renderer <name>   Renderer backend: opengl or null (default: opengl)
  --live-input <mode> Input for live-visual-control: keyboard-mouse,
  --prediction <mode> Live visual movement view: off (default) or reference
                      scripted-check, scripted-side-check,
                      scripted-jump-duck-check or scripted-speed-check
  --live-session-seconds <seconds>
                      Optional 1..300 second bound for keyboard-mouse mode;
                      expiry reports timed_session_complete and shuts down
  --runtime-replay-fixture <name>
                      Run an owning offline replay in the normal application
                      update loop: basic-mixed, missing-entity-base, or
                      visual-entities; non-visual replay requires --renderer null
                      and all replay modes are incompatible with live/resource modes
  --runtime-replay-capture <run-root>
                      Replay one structurally validated functional stock capture
                      through the normal application loop; headless requires null
                      and is incompatible with fixture/live/authentication modes
  --runtime-replay-visuals <mode>
                      diagnostic: project-generated visual-entities fixture
                      local-assets: capture + explicit --basedir + --game valve
  --runtime-replay-screenshot <png>
                      Save final local-assets OpenGL backbuffer to this output
  --runtime-replay-record-budget <count>
                      Application records per update, 1..1024 (default: 2)
  --runtime-replay-byte-budget <bytes>
                      Total payload bytes per update, 1..16777216
                      (default: 1048576)
  --view-world        Build the world render package, disconnect, then run the
                      local diagnostic OpenGL preview
  --view-entity-snapshot
                      Complete the bounded snapshot stage, close networking,
                      then stop at the typed stock visual-evidence boundary
  --visibility <mode> World visibility: all, frustum, pvs, or pvs-frustum
                      (default: all)
  --brush-submodels <mode>
                      Brush submodels: off or static (default: off)
  --camera <mode>     Diagnostic camera: static, orbit, or spawn
                      (default: static)

Connect-request mode sends once without waiting. Connect-response mode waits
boundedly for the immediate connectionless accept/reject only. Netchan-bootstrap
stops on the first owning opaque payload. Signon-boundary sends the one typed
initial request and stops before the first confirmed complex service-message body.
Pre-resource continues the same retained stream through typed server-info and one
confirmed simple control, then stops at the confirmed complex-message boundary.
It does not send a resource request or parse that boundary body.
Delta-schemas continues at that exact cursor, publishes an immutable metadata
registry for the confirmed opcode-14 sequence, and stops before consuming the
following post-delta body. It sends no resource response.
Movevars decodes the confirmed opcode-44 movement/environment metadata and
confirmed simple controls, then stops at the exact neutral post-movevars
boundary without consuming its body or sending a resource response.
User-info decodes the bounded opcode-13 sequence and stops at the exact end of
the first service batch without sending a transition request.
Resource-list-boundary: queue only the fixed transition request, wait for its
ACK, decode opcode 45, and stop before parsing opcode-43 body.
Resource-list: parse the bounded owning standard list and stop before the
required client response or any resource resolution; no response is sent.
Resource-response-boundary: continue on the same retained channel through the
typed opcode-5 response and its covering ACK when path-free provider material is
available, then stop at the first opcode of the following complete server
payload. The local provider validates explicit roots and prepares fixed-target
material read-only before networking. Without provider selection it exits with
a typed provider-required outcome and sends no incomplete or captured response.
Live-runtime-state continues that same connection through a typed zero-entry
client-resource advertisement because this headless project client has no
custom-logo feature. It uses no captured or fabricated tempdecal material,
decodes the exact post-resource controls and baseline registry, then feeds owning
live service payloads through the existing runtime dispatcher into the
application-owned ClientWorldState. It requires observed time, clientdata and
entities to remain valid for a bounded two-second interval, sends no usercmd,
and closes the retained connection at the selected stop.
Precache-manifest continues from that exact retained boundary without sending a
new packet. It correlates path-free local metadata, selects the exact ServerInfo
map entry, and publishes a bounded immutable metadata-only manifest. It does not
download, cache, open asset contents, parse assets, or integrate with a renderer.
Asset-dispatch continues on the same retained session, securely opens the
selected world source through its verified locator, runs importer dispatch,
and stops before renderer work. A valid BSP v30 source is imported by the
production GoldSrc world importer.
World-geometry follows that same retained route, requires a non-empty owning
CPU WorldAsset, reports bounded geometry counts, and stops before texture,
lightmap, renderer, or GPU work.
Collision-world builds an immutable CPU collision package from that canonical
BSP import, runs bounded deterministic probes, and stops without texture,
lightmap, SDL, OpenGL, renderer, movement, or prediction work.
World-textures continues only from an imported CPU world, decodes embedded and
declared WAD3 textures into owning RGBA mip levels, and stops before lightmaps,
renderer, or GPU work.
World-render-package continues locally through RGB lightmaps and a neutral CPU
render package without initializing SDL or uploading GPU resources. View-world
uses that same validated package only after network cleanup and requires the
OpenGL renderer.
Entity-visual-scene and view-entity-snapshot preserve the stock evidence gate:
after the bounded snapshot stage, stock visual-field and model-index mapping
return a typed evidence-pending outcome. Evidence-ready synthetic playback is
provided by the network-free entity viewer and project-owned integration
fixtures; it closes every network owner before local import or rendering.
Steam mode obtains fresh legacy game-server material after the owning server's
challenge; file mode remains the explicit historical-profile adapter.
Usercmd-boundary retains the explicit post-resource-response usercmd handoff on
that same session, reports the exact runtime/checksum evidence-pending status,
sends zero usercmd packets, and exits nonzero. Synthetic usercmd transmission
is available only in tests/fake peers and the offline hlclient_usercmd_check
tool.
)";
}

} // namespace hlclient::core
