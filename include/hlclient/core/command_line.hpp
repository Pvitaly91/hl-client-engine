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
};

struct CommandLineOptions {
    bool show_help{false};
    bool show_version{false};
    bool net_trace{false};
    std::optional<std::string> base_directory;
    std::string game_directory{"valve"};
    std::optional<std::string> connect_endpoint;
    ConnectionStopPoint stop_after{ConnectionStopPoint::challenge};
    std::optional<std::string> authentication_material_file;
    std::string player_name{"Player"};
    std::string player_model{"ivan"};
    RendererBackend renderer{RendererBackend::opengl};
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
[[nodiscard]] std::string_view command_line_help() noexcept;

} // namespace hlclient::core
