#pragma once

#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace hlclient::app {

struct ExternalHudObservationMetrics final {
    bool observed{};
    std::uint32_t tier{},width{},height{};
    float automatic{},user{},effective{},font{},icon{},row_width{};
    friend bool operator==(const ExternalHudObservationMetrics& a,
                           const ExternalHudObservationMetrics& b) noexcept {
        // row_width is the current kill-notice text, not a video/layout change.
        // It remains present in final/current receipts but must not consume the
        // bounded video-observation budget every time a new kill is announced.
        return std::tie(a.observed,a.tier,a.width,a.height,a.automatic,a.user,a.effective,a.font,a.icon)==
            std::tie(b.observed,b.tier,b.width,b.height,b.automatic,b.user,b.effective,b.font,b.icon);
    }
};

// Compare bounded scalar/string views before allocating JSON or invoking an
// observational sink. Failed publication never advances the retained key.
template<class Key> class ExternalObservationGate final {
public:
    template<class View> [[nodiscard]] bool unchanged(const View& next) const {
        return key_ && *key_ == next;
    }
    template<class View, class Publish> bool publish(const View& next, Publish&& sink) {
        if (unchanged(next)) return true;
        Key owned{next}; // Allocation failure must precede publication.
        if (!std::forward<Publish>(sink)()) return false;
        key_ = std::move(owned);
        return true;
    }
private:
    std::optional<Key> key_;
};

using ExternalAppearanceKey = std::tuple<
    std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t, std::uint64_t,
    std::string, std::string, std::string, game_api::PlayerAppearanceStatus,
    std::optional<std::uint16_t>, std::optional<std::uint16_t>,
    std::uint64_t, bool>;

// Every field serialized into the appearance observation participates. Motion,
// pose and lighting are intentionally absent: they are not appearance receipts.
[[nodiscard]] inline auto external_appearance_key_view(
    const ReplayLocalVisualSummary::RemotePlayer& player) noexcept {
    const auto& a = player.appearance;
    return std::tuple{player.entity, player.model_slot, player.network_generation,
        player.map_generation, player.entity_lifetime, a.occupant_epoch,
        a.appearance_revision, std::string_view{a.fields.model},
        std::string_view{a.model_name}, std::string_view{player.resolved_visual_model},
        a.status, a.fields.top_color, a.fields.bottom_color,
        player.visual_model_resource_id, player.palette_remapped};
}

} // namespace hlclient::app
