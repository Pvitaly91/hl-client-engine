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
           argument == "--name" || argument == "--model";
}

} // namespace

CommandLineParseResult parse_command_line(const std::span<const std::string_view> arguments)
{
    CommandLineOptions options;
    bool stop_after_seen = false;
    bool connect_request_setting_seen = false;

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
            } else {
                return failure("Unsupported --stop-after value: " + std::string{value} +
                               " (expected challenge, connect-request, connect-response, "
                               "netchan-bootstrap, or signon-boundary)");
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
    if (options.stop_after == ConnectionStopPoint::challenge &&
        connect_request_setting_seen) {
        return failure("--auth-provider, --auth-material-file, --name, and --model require "
                       "a connect-request, connect-response, netchan-bootstrap, or "
                       "signon-boundary stop point");
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
         options.stop_after == ConnectionStopPoint::signon_boundary) &&
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

    return CommandLineParseResult{std::move(options), {}};
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
                       netchan-bootstrap, or signon-boundary (default: challenge)
  --auth-provider <name>
                      Authentication provider for connect stages: file
  --auth-material-file <path>
                      Local 245-byte auth input for file provider; never logged
  --name <name>       Player name, max 31 printable ASCII bytes (default: Player)
  --model <model>     Player model, max 31 printable ASCII bytes (default: ivan)
  --net-trace         Log bounded diagnostics; connect payload/auth bytes are redacted
  --renderer <name>   Renderer backend: opengl or null (default: opengl)

Connect-request mode sends once without waiting. Connect-response mode waits
boundedly for the immediate connectionless accept/reject only. Netchan-bootstrap
stops on the first owning opaque payload. Signon-boundary sends the one typed
initial request and stops before the first confirmed complex service-message body.
No mode implements authentication generation.
)";
}

} // namespace hlclient::core
