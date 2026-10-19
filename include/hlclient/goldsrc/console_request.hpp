#pragma once

#include <hlclient/goldsrc/client_message.hpp>

#include <cstdint>
#include <string>

namespace hlclient::goldsrc {

enum class ConsoleServerCommandKind : std::uint8_t { say, say_team, status, kill, drop };

// A typed local request, not a generic string-command forwarding capability.
// Chat has an explicit printable-ASCII-only wire profile until a separate
// legacy server text encoding is established. Drop optionally names one
// bounded weapon_ token; the server remains the authority for every action.
struct ConsoleServerCommandRequest {
    ConsoleServerCommandKind kind{ConsoleServerCommandKind::status};
    std::string argument;
};

[[nodiscard]] ClientMessageBuildResult encode_console_server_command(
    const ConsoleServerCommandRequest& request);

} // namespace hlclient::goldsrc
