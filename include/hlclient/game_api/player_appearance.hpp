#pragma once
#include <hlclient/assets/model_asset_types.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::game_api {
// Owning display projection only. Authentication identifiers and raw info
// strings do not cross this boundary. A rejected token is never a path.
struct PlayerAppearanceFields final {
    std::string model;
    bool model_advertised{}, model_token_rejected{};
    std::optional<std::uint16_t> top_color, bottom_color;
    friend bool operator==(const PlayerAppearanceFields&, const PlayerAppearanceFields&) = default;
};
enum class PlayerAppearanceStatus : std::uint8_t {
    model_not_advertised, default_model_requested, model_token_rejected,
    approved_asset_missing, asset_import_failed, pose_incompatible,
    asset_pending, selected_model_ready, explicit_server_model, resource_limit,
    not_authorized
};
[[nodiscard]] constexpr std::string_view to_string(PlayerAppearanceStatus s) noexcept {
    switch(s) {
    case PlayerAppearanceStatus::model_not_advertised:return "model_not_advertised";
    case PlayerAppearanceStatus::default_model_requested:return "default_model_requested";
    case PlayerAppearanceStatus::model_token_rejected:return "model_token_rejected";
    case PlayerAppearanceStatus::approved_asset_missing:return "approved_asset_missing";
    case PlayerAppearanceStatus::asset_import_failed:return "asset_import_failed";
    case PlayerAppearanceStatus::pose_incompatible:return "pose_incompatible";
    case PlayerAppearanceStatus::asset_pending:return "asset_pending";
    case PlayerAppearanceStatus::selected_model_ready:return "selected_model_ready";
    case PlayerAppearanceStatus::explicit_server_model:return "explicit_server_model";
    case PlayerAppearanceStatus::resource_limit:return "resource_limit";
    case PlayerAppearanceStatus::not_authorized:return "not_authorized";
    }
    return "not_authorized";
}
struct PlayerAppearanceSelection final {
    PlayerAppearanceFields fields;
    std::uint64_t occupant_epoch{}, appearance_revision{};
    PlayerAppearanceStatus status{PlayerAppearanceStatus::model_not_advertised};
    std::string model_name; // Compiled game policy constructs a narrow virtual token.
};
// Neutral indexed-palette operation. Game code alone selects material/ranges;
// renderer sees only immutable RGBA material variants, never player colors.
struct PlayerPaletteRange final {
    std::uint32_t texture{};
    std::uint8_t first{}, last{};
    std::uint16_t hue_degrees{};
};
} // namespace hlclient::game_api
