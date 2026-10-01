#pragma once
#include <cstdint>
#include <string>
namespace hlclient::game_api {
enum class GameCommandKind : std::uint8_t { inventory_selection, self_kill };
// A module-selected bounded single token. The host validates its wire encoding;
// this is never constructed from server console/stufftext or arbitrary UI text.
struct GameCommandRequest final {
    GameCommandKind kind{};
    std::string token;
};
}
