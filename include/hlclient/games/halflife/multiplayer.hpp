#pragma once
#include <hlclient/game_api/game_client_module.hpp>
namespace hlclient::games::halflife {
[[nodiscard]] std::optional<game_api::GameMessageFailure>
stage_multiplayer(game_api::GameRecordState &,
                  const game_api::GameRecordInput &);
void compose_multiplayer_hud(game_api::MultiplayerHudState &,
                             game_api::HudState &,
                             game_api::HudPresentationContext, double,
                             const game_api::HudResourceDescriptor* base_hud = nullptr,
                             std::uint32_t hud_tier = 0U);
} // namespace hlclient::games::halflife
