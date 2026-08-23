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
           argument == "--name" || argument == "--model";
}

} // namespace

CommandLineParseResult parse_command_line(const std::span<const std::string_view> arguments)
{
    CommandLineOptions options;
    bool stop_after_seen = false;
    bool connect_request_setting_seen = false;
    bool resource_consistency_provider_seen = false;

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
            } else if (value == "precache-manifest") {
                options.stop_after = ConnectionStopPoint::precache_manifest;
            } else if (value == "asset-dispatch") {
                options.stop_after = ConnectionStopPoint::asset_dispatch;
            } else {
                return failure("Unsupported --stop-after value: " + std::string{value} +
                               " (expected challenge, connect-request, connect-response, "
                               "netchan-bootstrap, signon-boundary, pre-resource, "
                               "delta-schemas, movevars, user-info, or "
                               "resource-list-boundary, resource-list, or "
                               "resource-response-boundary, precache-manifest, or "
                               "asset-dispatch)");
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
        } else {
            options.connect_endpoint = std::string{value};
        }
    }

    if ((stop_after_seen || connect_request_setting_seen) && !options.connect_endpoint) {
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
                       "resource-response-boundary/precache-manifest/"
                       "asset-dispatch stop point");
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
         options.stop_after == ConnectionStopPoint::precache_manifest ||
         options.stop_after == ConnectionStopPoint::asset_dispatch) &&
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
    if (options.stop_after == ConnectionStopPoint::asset_dispatch &&
        options.resource_consistency_provider !=
            ResourceConsistencyProviderKind::local) {
        return failure(
            "The asset-dispatch stop point requires "
            "--resource-consistency-provider local");
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
            options.stop_after == ConnectionStopPoint::precache_manifest ||
            options.stop_after == ConnectionStopPoint::asset_dispatch);
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
                       resource-response-boundary, precache-manifest, or
                       asset-dispatch
                       (default: challenge)
  --auth-provider <name>
                      Authentication provider for connect stages: file
  --auth-material-file <path>
                      Local 245-byte auth input for file provider; never logged
  --resource-consistency-provider <name>
                      Explicit read-only response provider: local; requires
                      --basedir and is prepared only for resource-response-boundary,
                      precache-manifest, or asset-dispatch
  --name <name>       Player name, max 31 printable ASCII bytes (default: Player)
  --model <model>     Player model, max 31 printable ASCII bytes (default: ivan)
  --net-trace         Log bounded diagnostics; connect payload/auth bytes are redacted
  --renderer <name>   Renderer backend: opengl or null (default: opengl)

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
and stops before renderer work. No matching importer is an expected boundary
until the BSP importer milestone.
No mode implements authentication generation.
)";
}

} // namespace hlclient::core
