#include <hlclient/games/halflife/inventory.hpp>
#include <hlclient/game_api/command.hpp>
#include <algorithm>
#include <ranges>
#include <tuple>
#include <utility>
namespace hlclient::games::halflife {
using goldsrc::ClientMessageBuildResult;
using goldsrc::ClientMessageError;
using goldsrc::ClientMessageErrorCode;
ClientMessageBuildResult build_weapon_selection_request(
    const client::RuntimeClientObservationState& observation,
    const std::uint8_t weapon_id)
{
    const auto rejected = [](std::string context) {
        return ClientMessageBuildResult{std::nullopt,
            ClientMessageError{ClientMessageErrorCode::unsupported_command_variant,
                0U, std::move(context)}};
    };
    if (observation.lifecycle.dead() ||
        (observation.lifecycle.last_death_source &&
         (!observation.client_metadata.source ||
          observation.client_metadata.source->record_ordinal <=
              observation.lifecycle.last_death_source->record_ordinal)) ||
        weapon_id == 0U || weapon_id >= 32U ||
        !observation.receiving_client ||
        !observation.receiving_client->owned_weapon_bits ||
        ((*observation.receiving_client->owned_weapon_bits >> weapon_id) & 1U) == 0U)
        return rejected("weapon ID is not confirmed in current clientdata inventory");
    const auto found = std::find_if(observation.weapon_hud.catalogue.begin(),
        observation.weapon_hud.catalogue.end(),
        [&](const auto& value) { return value.id == weapon_id; });
    if (found == observation.weapon_hud.catalogue.end() ||
        found->source.record_identity == 0U ||
        found->command_name.size() < 8U ||
        found->command_name.size() > 63U ||
        !found->command_name.starts_with("weapon_"))
        return rejected("weapon name lacks a current bounded catalogue entry");
    for (const unsigned char character : found->command_name) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_'))
            return rejected("weapon name is not one inert ASCII token");
    }
    return goldsrc::encode_game_command({game_api::GameCommandKind::inventory_selection, found->command_name});
}

namespace {
std::vector<std::pair<std::uint8_t, std::uint8_t>> owned_weapon_order(
    const client::RuntimeClientObservationState& observation)
{
    std::vector<std::pair<std::uint8_t, std::uint8_t>> result;
    for (const auto& type : observation.weapon_hud.catalogue) {
        if (build_weapon_selection_request(observation, type.id))
            result.emplace_back(type.slot, type.id);
    }
    std::ranges::sort(result, [&](const auto& left, const auto& right) {
        const auto position = [&](const auto& value) {
            const auto found = std::find_if(observation.weapon_hud.catalogue.begin(),
                observation.weapon_hud.catalogue.end(),
                [&](const auto& type) { return type.id == value.second; });
            return found->position;
        };
        return std::tuple{left.first, position(left), left.second} <
            std::tuple{right.first, position(right), right.second};
    });
    return result;
}
}

std::optional<std::uint8_t> select_owned_weapon_group(
    const client::RuntimeClientObservationState& observation,
    const std::uint8_t group_digit,
    const std::optional<std::uint8_t> current_or_pending)
{
    if (group_digit < 1U || group_digit > 5U) return std::nullopt;
    const auto ordered = owned_weapon_order(observation);
    std::vector<std::uint8_t> in_group;
    for (const auto [slot, id] : ordered)
        if (slot == group_digit - 1U) in_group.push_back(id);
    if (in_group.empty()) return std::nullopt;
    const auto found = current_or_pending
        ? std::find(in_group.begin(), in_group.end(), *current_or_pending)
        : in_group.end();
    return found == in_group.end() || found + 1 == in_group.end()
        ? in_group.front() : *(found + 1);
}

std::optional<std::uint8_t> cycle_owned_weapon(
    const client::RuntimeClientObservationState& observation,
    const std::optional<std::uint8_t> current_or_pending,
    const int direction)
{
    if (direction == 0) return std::nullopt;
    const auto ordered = owned_weapon_order(observation);
    if (ordered.empty()) return std::nullopt;
    const auto found = current_or_pending
        ? std::find_if(ordered.begin(), ordered.end(), [&](const auto& value) {
              return value.second == *current_or_pending;
          }) : ordered.end();
    if (found == ordered.end()) return direction > 0
        ? ordered.front().second : ordered.back().second;
    const auto offset = static_cast<std::size_t>(found - ordered.begin());
    return ordered[(offset + ordered.size() + (direction > 0 ? 1U : ordered.size() - 1U))
        % ordered.size()].second;
}

ClientMessageBuildResult build_self_kill_request() { return goldsrc::encode_game_command({game_api::GameCommandKind::self_kill, "kill"}); }
}
