#pragma once
#include <hlclient/game_api/text_messages.hpp>
#include <span>

namespace hlclient::goldsrc {
// Filesystem-free bounded titles.txt subset. Only the negative effect field
// changes a TextMsg destination; visual title commands remain inert metadata.
[[nodiscard]] std::optional<game_api::TextMessageCatalogue> parse_text_message_catalogue(std::span<const std::byte>);
}
