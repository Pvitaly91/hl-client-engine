#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <array>

namespace hlclient::game_api {
// Read-only counts from existing scheduler receipts. These neither acknowledge
// execution nor authorize local damage. Phase counts belong to the bounded
// pre-existing action diagnostic scenario.
struct GameActionTraffic final {
    std::size_t attack_new_submission_count{}, reload_new_submission_count{};
    std::array<std::size_t, 5U> generated_by_phase{};
    std::size_t use_new_submission_count{};
};
struct GameActionEvidenceSnapshot final {
    // Available absolute server values around first submitted Use. A delta
    // is observation only: no target, success ACK or causal attribution.
    std::optional<std::int32_t> use_health_before, use_health_after;
    std::optional<std::int32_t> use_armor_before, use_armor_after;
    std::optional<std::int32_t> clip_before_fire, clip_after_fire, clip_after_reload;
    std::optional<std::uint8_t> reserve_before_reload, reserve_after_reload;
    std::size_t server_confirmed_shots{}, server_confirmed_reload_starts{};
    std::size_t server_confirmed_reload_completions{}, service_animation_count{};
    std::size_t primary_animation_count{}, reload_animation_count{}, melee_animation_count{};
    std::size_t server_punch_observations{};
    double maximum_server_punch_degrees{};
    std::optional<std::size_t> last_animation_kind;
    std::optional<client::RuntimeObservationSource> last_animation_source;
};
}
