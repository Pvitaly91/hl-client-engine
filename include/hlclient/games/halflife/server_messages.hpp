#pragma once
#include <hlclient/game_api/game_client_module.hpp>

namespace hlclient::games::halflife {
// All views are borrowed for this synchronous call. Returned catalogue/text,
// events and state own their data; publication belongs to the host transaction.
[[nodiscard]] game_api::GameRecordResult stage_record(
    const game_api::GameRecordInput& input,
    const game_api::GameRecordState* previous_game);
}
