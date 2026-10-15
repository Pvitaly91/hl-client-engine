#pragma once
#include <hlclient/game_api/hud_resources.hpp>
#include <span>
#include <string_view>
namespace hlclient::games::halflife {
[[nodiscard]] std::span<const game_api::HudManifestRequest> halflife_hud_resource_preparation();
[[nodiscard]] std::string_view halflife_hud_descriptor_name(std::string_view catalogue_name) noexcept;
} // namespace hlclient::games::halflife
