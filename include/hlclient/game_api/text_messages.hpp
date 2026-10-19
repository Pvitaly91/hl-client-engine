#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::game_api {
enum class TextDestination : std::uint8_t { notify=1, console=2, talk=3, center=4 };
struct TextMessageEntry final {
    std::string key,text;
    std::optional<TextDestination> destination;
};
using TextMessageCatalogue=std::vector<TextMessageEntry>;
inline constexpr std::size_t kMaximumTextMessageBytes=256U*1024U;
inline constexpr std::size_t kMaximumTextMessageEntries=1024U;
}
