#pragma once
#include <hlclient/assets/asset_types.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>

namespace hlclient::goldsrc {
// A console .lmp is width/height u32le, indexed pixels, u16le palette count256
// and256 RGB triples. This explicitly is not a WAD miptexture or a BMP.
[[nodiscard]] std::optional<assets::ImageAsset> decode_console_lmp_image(std::span<const std::byte>);
enum class ConsoleBackgroundStatus { ready, missing, invalid_source, rejected, cancelled };
struct PreparedConsoleBackground {
    ConsoleBackgroundStatus status{ConsoleBackgroundStatus::missing};
    std::shared_ptr<const assets::ImageAsset> image;
};
[[nodiscard]] constexpr std::string_view to_string(ConsoleBackgroundStatus s) noexcept {
    switch(s) {
    case ConsoleBackgroundStatus::ready:return "ready";
    case ConsoleBackgroundStatus::missing:return "missing";
    case ConsoleBackgroundStatus::invalid_source:return "invalid_source";
    case ConsoleBackgroundStatus::rejected:return "rejected";
    case ConsoleBackgroundStatus::cancelled:return "cancelled";
    }return "rejected";
}
// Uses the existing exact-root resolver/locator and verified source opener;
// safe to run in the startup worker, independent of a map or network manifest.
[[nodiscard]] PreparedConsoleBackground prepare_console_background(
    const std::filesystem::path& selected_base,std::string_view game_directory,
    std::string_view virtual_name,std::stop_token stop = {});
}
