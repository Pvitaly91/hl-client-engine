#include <hlclient/core/command_line.hpp>

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
           argument == "--auth-material-file" ||
           argument == "--resource-consistency-provider" ||
           argument == "--name" || argument == "--model" ||
           argument == "--visibility" || argument == "--brush-submodels" ||
           argument == "--camera";
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
            } else if (value == "precache-manifest") {
                options.stop_after = ConnectionStopPoint::precache_manifest;
            } else if (value == "asset-dispatch") {
                options.stop_after = ConnectionStopPoint::asset_dispatch;
            } else if (value == "world-geometry") {
                options.stop_after = ConnectionStopPoint::world_geometry;
            } else if (value == "world-textures") {
                options.stop_after = ConnectionStopPoint::world_textures;
            } else if (value == "world-render-package") {
                options.stop_after = ConnectionStopPoint::world_render_package;
            } else if (value == "world-spatial-scene") {
                options.stop_after = ConnectionStopPoint::world_spatial_scene;
            } else {
                return failure("Unsupported --stop-after value: " + std::string{value} +
                               " (expected challenge, connect-request, connect-response, "
                               "netchan-bootstrap, signon-boundary, pre-resource, "
                               "delta-schemas, movevars, user-info, or "
                               "resource-list-boundary, resource-list, or "
                               "resource-response-boundary, server-baselines, "
                               "entity-snapshot, precache-manifest, or "
                               "asset-dispatch, world-geometry, world-textures, or "
                               "world-render-package, or world-spatial-scene)");
            }
        } else if (argument == "--auth-provider") {
            connect_request_setting_seen = true;
            if (value != "file") {
                return failure("Unsupported authentication provider: " + std::string{value} +
                               " (expected file)");
            }
            options.authentication_provider = AuthenticationProviderKind::file;
        } else if (argument == "--auth-material-file") {
            connect_request_setting_seen = true;
            options.authentication_material_file = std::string{value};
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
        } else {
            options.connect_endpoint = std::string{value};
        }
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
    if ((visibility_seen || brush_submodels_seen || camera_seen) &&
        !options.view_world &&
        options.stop_after != ConnectionStopPoint::world_spatial_scene) {
        return failure(
            "--visibility, --brush-submodels, and --camera require "
            "--view-world or --stop-after world-spatial-scene");
    }
    if ((stop_after_seen || options.view_world || connect_request_setting_seen) &&
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
                       "entity-snapshot/precache-manifest/"
                       "asset-dispatch/world-geometry/world-textures/"
                       "world-render-package/world-spatial-scene stop point or "
                       "--view-world");
    }
    if (options.authentication_provider && !options.authentication_material_file) {
        return failure("The file authentication provider requires --auth-material-file");
    }
    if (options.stop_after != ConnectionStopPoint::challenge &&
        !options.authentication_material_file) {
        return failure(
            "Connect request, response, netchan, and sign-on modes require "
            "--auth-material-file");
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
         options.stop_after == ConnectionStopPoint::precache_manifest ||
         options.stop_after == ConnectionStopPoint::asset_dispatch ||
         options.stop_after == ConnectionStopPoint::world_geometry ||
         options.stop_after == ConnectionStopPoint::world_textures ||
         options.stop_after == ConnectionStopPoint::world_render_package ||
         options.stop_after == ConnectionStopPoint::world_spatial_scene) &&
        !options.authentication_provider) {
        return failure(
            "Netchan bootstrap and sign-on require the explicit "
            "--auth-provider file selection");
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
         options.stop_after == ConnectionStopPoint::entity_snapshot) &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The server-baselines and entity-snapshot stop points require "
            "--resource-consistency-provider local");
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
    if (options.view_world && options.renderer != RendererBackend::opengl) {
        return failure("--view-world requires --renderer opengl");
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
            options.stop_after == ConnectionStopPoint::precache_manifest ||
            options.stop_after == ConnectionStopPoint::asset_dispatch ||
            options.stop_after == ConnectionStopPoint::world_geometry ||
            options.stop_after == ConnectionStopPoint::world_textures ||
            options.stop_after == ConnectionStopPoint::world_render_package ||
            options.stop_after == ConnectionStopPoint::world_spatial_scene);
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
                       entity-snapshot, precache-manifest, or
                       asset-dispatch, world-geometry, world-textures, or
                       world-render-package, or world-spatial-scene
                       (default: challenge)
  --auth-provider <name>
                      Authentication provider for connect stages: file
  --auth-material-file <path>
                      Local 245-byte auth input for file provider; never logged
  --resource-consistency-provider <name>
                      Explicit read-only response provider: local; requires
                      --basedir and is prepared only for resource-response-boundary,
                      server-baselines, entity-snapshot, precache-manifest,
                      asset-dispatch, world-geometry, or
                      world-textures, world-render-package, or
                      world-spatial-scene
  --name <name>       Player name, max 31 printable ASCII bytes (default: Player)
  --model <model>     Player model, max 31 printable ASCII bytes (default: ivan)
  --net-trace         Log bounded diagnostics; connect payload/auth bytes are redacted
  --renderer <name>   Renderer backend: opengl or null (default: opengl)
  --view-world        Build the world render package, disconnect, then run the
                      local diagnostic OpenGL preview
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
World-textures continues only from an imported CPU world, decodes embedded and
declared WAD3 textures into owning RGBA mip levels, and stops before lightmaps,
renderer, or GPU work.
World-render-package continues locally through RGB lightmaps and a neutral CPU
render package without initializing SDL or uploading GPU resources. View-world
uses that same validated package only after network cleanup and requires the
OpenGL renderer.
No mode implements authentication generation.
)";
}

} // namespace hlclient::core
