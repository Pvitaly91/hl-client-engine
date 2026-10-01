#include <hlclient/games/halflife/server_messages.hpp>
#include <hlclient/games/halflife/local_player_lifecycle.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::games::halflife {
namespace {
[[nodiscard]] std::optional<game_api::GameMessageFailure> apply_messages(
    game_api::GameRecordState& staged,
    std::span<const game_api::GameMessageView> messages)
{
    auto& hud = staged.weapon_hud;
    auto& life_events = staged.life_events;
    const auto byte = [](const std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    };
    const auto signed_byte = [&](const std::byte value) {
        return static_cast<std::int8_t>(byte(value));
    };
    for (const auto& message : messages) {
        const bool animation = message.kind == game_api::GameMessageKind::weapon_animation;
        const auto name = message.name;
        const auto body = message.body;
        const auto length = body.size();
        const auto& source = message.source;
        const auto reject = [&](game_api::GameMessageErrorCode code, std::string context,
                                std::optional<std::size_t> expected = {},
                                std::optional<std::size_t> body_offset = {}) {
            return game_api::GameMessageFailure{code,
                animation ? std::string{"svc_weaponanim"} : std::string{name},
                message.registration_id, source, expected, length, std::move(context), body_offset};
        };
        if (!animation && (name == "AmmoPickup" || name == "WeapPickup" || name == "ItemPickup")) {
            game_api::GameRecordState::Pickup pickup;
            pickup.source = source;
            if (name == "AmmoPickup") {
                if (length != 2U || byte(body[0]) >= hud.reserve_ammo.size())
                    return reject(length != 2U ? game_api::GameMessageErrorCode::invalid_body : game_api::GameMessageErrorCode::invalid_identifier, "AmmoPickup requires a bounded ammo ID and exact count byte", 2U);
                // Reference history ignores a zero-amount notification. Even a
                // positive notification never adds to the absolute AmmoX count.
                if (byte(body[1]) == 0U) continue;
                pickup.kind = game_api::GameRecordState::Pickup::Kind::ammunition;
                pickup.identifier = byte(body[0]);
                pickup.amount = byte(body[1]);
            } else if (name == "WeapPickup") {
                if (length != 1U || byte(body[0]) == 0U || byte(body[0]) >= 32U)
                    return reject(length != 1U ? game_api::GameMessageErrorCode::invalid_body : game_api::GameMessageErrorCode::invalid_identifier, "WeapPickup requires one bounded nonzero weapon ID", 1U);
                pickup.kind = game_api::GameRecordState::Pickup::Kind::weapon;
                pickup.identifier = byte(body[0]);
            } else {
                if (length < 2U || length > 64U || body.back() != std::byte{0})
                    return reject(game_api::GameMessageErrorCode::invalid_body, "ItemPickup requires one bounded terminated inert token");
                pickup.kind = game_api::GameRecordState::Pickup::Kind::item;
                for (std::size_t i = 0U; i + 1U < length; ++i) {
                    const auto c = byte(body[i]);
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_'))
                        return reject(game_api::GameMessageErrorCode::invalid_token, "ItemPickup token contains a non-inert character", {}, i);
                    pickup.item_token.push_back(static_cast<char>(c));
                }
            }
            if (staged.pickup_high_water &&
                (source.record_ordinal < staged.pickup_high_water->record_ordinal ||
                 (source.record_ordinal == staged.pickup_high_water->record_ordinal &&
                  source.end_bit_offset <= staged.pickup_high_water->end_bit_offset)))
                continue;
            if (staged.pickups.size() >= 64U)
                return reject(game_api::GameMessageErrorCode::capacity_exceeded, "Half-Life pickup event capacity exceeded");
            staged.pickup_high_water = source;
            staged.pickups.push_back(std::move(pickup));
            // Feedback alone cannot make a weapon/controller state fresh.
            continue;
        }
        if (animation && length != 2U)
            return reject(game_api::GameMessageErrorCode::invalid_body, "weapon animation requires two exact bytes", 2U);
        client::RuntimeLifeEvent life;
        life.source = source;
        if (name == "Damage") {
            if (length != 12U) return reject(game_api::GameMessageErrorCode::invalid_body, "Damage requires twelve exact bytes", 12U);
            life.kind = client::RuntimeLifeEventKind::damage;
            life.armor_saved = byte(body[0]);
            life.damage_taken = byte(body[1]);
            for (std::size_t i = 0U; i < 4U; ++i)
                life.damage_bits |= static_cast<std::uint32_t>(byte(body[2U + i])) << (8U * i);
            for (std::size_t i = 0U; i < 3U; ++i) {
                const auto coordinate = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(byte(body[6U + i * 2U])) |
                    static_cast<std::uint16_t>(byte(body[7U + i * 2U]) << 8U));
                life.damage_origin[i] = static_cast<double>(coordinate) / 8.0;
            }
            life_events.push_back(std::move(life));
        } else if (name == "DeathMsg") {
            if (length < 3U || length > 66U || body.back() != std::byte{0} ||
                std::find(body.begin() + 2, body.end() - 1, std::byte{0}) != body.end() - 1)
                return reject(game_api::GameMessageErrorCode::invalid_body, "HLDM DeathMsg requires two entity bytes and bounded terminated inert text");
            life.kind = client::RuntimeLifeEventKind::death_notice;
            life.killer_entity = byte(body[0]); // world/unknown 0 is legal
            life.victim_entity = byte(body[1]);
            for (std::size_t i = 2U; i + 1U < length; ++i)
                life.weapon_text.push_back(static_cast<char>(byte(body[i])));
            life_events.push_back(std::move(life));
        } else if (name == "InitHUD") {
            if (length != 0U) return reject(game_api::GameMessageErrorCode::invalid_body, "InitHUD requires an empty body", 0U);
            life.kind = client::RuntimeLifeEventKind::init_hud;
            life_events.push_back(std::move(life));
        }
        if (animation) {
            hud.animation_sequence = byte(body[0]);
            hud.animation_body = byte(body[1]);
            hud.animation_source = source;
        } else if (name == "WeaponList") {
            const auto terminator = std::find(body.begin(), body.end(), std::byte{0});
            const auto name_length = static_cast<std::size_t>(terminator - body.begin());
            if (terminator == body.end() || name_length == 0U ||
                name_length > 63U || length != name_length + 9U) return reject(game_api::GameMessageErrorCode::invalid_body, "WeaponList requires a bounded name and eight exact fields");
            const auto id = signed_byte(body[name_length + 7U]);
            if (id <= 0 || id >= 32 || hud.catalogue.size() > 64U) return reject(game_api::GameMessageErrorCode::invalid_identifier, "WeaponList weapon ID or catalogue bound is invalid");
            client::RuntimeWeaponTypeObservation type;
            type.id = static_cast<std::uint8_t>(id);
            type.command_name.reserve(name_length);
            for (std::size_t i = 0; i < name_length; ++i)
                type.command_name.push_back(static_cast<char>(byte(body[i])));
            type.primary_ammo_type = signed_byte(body[name_length + 1U]);
            type.secondary_ammo_type = signed_byte(body[name_length + 3U]);
            type.slot = byte(body[name_length + 5U]);
            type.position = byte(body[name_length + 6U]);
            type.flags = byte(body[name_length + 8U]);
            type.source = source;
            if (type.primary_ammo_type >= 64 || type.secondary_ammo_type >= 64 ||
                type.slot > 9U || type.position > 63U) return reject(game_api::GameMessageErrorCode::invalid_value, "WeaponList field exceeds supported HUD bounds");
            const auto existing = std::find_if(hud.catalogue.begin(), hud.catalogue.end(),
                [&](const auto& value) { return value.id == type.id; });
            if (existing == hud.catalogue.end()) hud.catalogue.push_back(std::move(type));
            else *existing = std::move(type);
        } else if (name == "CurWeapon") {
            if (length != 3U) return reject(game_api::GameMessageErrorCode::invalid_body, "CurWeapon requires three bytes", 3U);
            const auto state = byte(body[0]);
            const auto id = signed_byte(body[1]);
            const auto clip = signed_byte(body[2]);
            const std::int16_t clip_value = clip < -1
                ? static_cast<std::int16_t>(-clip)
                : static_cast<std::int16_t>(clip);
            if (id > 0 && id < 64) {
                hud.clips[static_cast<std::size_t>(id)] = clip_value;
                hud.clip_sources[static_cast<std::size_t>(id)] = source;
            }
            if (id <= 0) {
                hud.active_weapon_id = std::uint8_t{0U};
                hud.active_source = source;
            } else if (state != 0U) {
                if (id >= 64) return reject(game_api::GameMessageErrorCode::invalid_identifier, "CurWeapon active ID is outside supported bounds");
                hud.active_weapon_id = static_cast<std::uint8_t>(id);
                hud.active_source = source;
            }
        } else if (name == "AmmoX") {
            if (length != 2U || byte(body[0]) >= hud.reserve_ammo.size())
                return reject(length != 2U ? game_api::GameMessageErrorCode::invalid_body : game_api::GameMessageErrorCode::invalid_identifier, "AmmoX requires bounded ammo ID and count", 2U);
            hud.reserve_ammo[byte(body[0])] = byte(body[1]);
            hud.reserve_ammo_sources[byte(body[0])] = source;
        } else if (name == "Health") {
            if (length != 1U) return reject(game_api::GameMessageErrorCode::invalid_body, "Health requires one byte", 1U);
            hud.health = byte(body[0]);
            hud.health_source = source;
        } else if (name == "SetFOV") {
            if (length != 1U) return reject(game_api::GameMessageErrorCode::invalid_body, "SetFOV requires one byte", 1U);
            hud.field_of_view = byte(body[0]);
        } else if (name == "Battery") {
            if (length != 2U) return reject(game_api::GameMessageErrorCode::invalid_body, "Battery requires a short", 2U);
            hud.armor = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(byte(body[0])) |
                static_cast<std::uint16_t>(byte(body[1]) << 8U));
            hud.armor_source = source;
        } else if (name == "HideWeapon") {
            if (length != 1U) return reject(game_api::GameMessageErrorCode::invalid_body, "HideWeapon requires one byte", 1U);
            hud.hide_flags = byte(body[0]);
        } else if (name == "ResetHUD") {
            if (length != 1U) return reject(game_api::GameMessageErrorCode::invalid_body, "ResetHUD requires one byte", 1U);
            life.kind = client::RuntimeLifeEventKind::reset_hud;
            life_events.push_back(std::move(life));
            hud.active_weapon_id.reset();
            hud.active_source.reset();
            hud.reserve_ammo.fill(std::nullopt);
            hud.clips.fill(std::nullopt);
            hud.reserve_ammo_sources.fill(std::nullopt);
            hud.clip_sources.fill(std::nullopt);
            hud.armor.reset();
            hud.health.reset();
            hud.health_source.reset();
            hud.armor_source.reset();
            hud.hide_flags.reset();
            hud.field_of_view.reset();
            hud.animation_sequence.reset();
            hud.animation_body.reset();
            hud.animation_source.reset();
        } else {
            continue;
        }
        hud.last_message_source = source;
        ++hud.revision;
    }
    return {};
}
} // namespace

game_api::GameRecordResult stage_record(
    const game_api::GameRecordInput& input,
    const game_api::GameRecordState* previous_game)
{
    game_api::GameRecordState staged;
    if (previous_game) {
        staged.weapon_hud = previous_game->weapon_hud;
        staged.pickup_high_water = previous_game->pickup_high_water;
    }
    if (auto error = apply_messages(staged, input.messages))
        return {{}, error->context, std::move(error)};
    staged.lifecycle = advance_local_player_lifecycle(input.previous,
        input.observation, input.receiving_entity, staged.weapon_hud, staged.life_events,
        previous_game ? &previous_game->weapon_hud : nullptr,
        previous_game ? &previous_game->lifecycle : nullptr);
    return {std::move(staged), {}};
}
} // namespace hlclient::games::halflife
