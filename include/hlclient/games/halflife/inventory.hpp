#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/goldsrc/client_message.hpp>
namespace hlclient::games::halflife {
[[nodiscard]] goldsrc::ClientMessageBuildResult build_weapon_selection_request(
    const client::RuntimeClientObservationState&, std::uint8_t);
[[nodiscard]] goldsrc::ClientMessageBuildResult build_self_kill_request();
[[nodiscard]] std::optional<std::uint8_t> select_owned_weapon_group(
    const client::RuntimeClientObservationState&, std::uint8_t, std::optional<std::uint8_t>);
[[nodiscard]] std::optional<std::uint8_t> cycle_owned_weapon(
    const client::RuntimeClientObservationState&, std::optional<std::uint8_t>, int);
}
