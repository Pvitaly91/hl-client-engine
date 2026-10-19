#pragma once
#include <hlclient/game_api/game_client_module.hpp>

namespace hlclient::games::halflife {
// All views are borrowed for this synchronous call. Returned catalogue/text,
// events and state own their data; publication belongs to the host transaction.
[[nodiscard]] game_api::GameRecordResult stage_record(
    const game_api::GameRecordInput& input,
    const game_api::GameRecordState* previous_game,
    std::span<const game_api::TextMessageEntry> catalogue = {});
// Existing neutral HUD primitives provide the separate notify/talk/center
// destinations. Their bounded visual lifetime never owns console publication.
class ServerTextDisplay final {
public:
    void reset() noexcept {entries_={};high_water_.reset();}
    void committed(const game_api::GameRecordState&);
    void compose(game_api::HudState&,game_api::HudPresentationContext,double);
private:
    struct Entry {std::string text;std::optional<double> time;};
    std::array<Entry,9> entries_{}; // four notify, four talk, one center
    std::optional<client::RuntimeObservationSource> high_water_;
};
}
