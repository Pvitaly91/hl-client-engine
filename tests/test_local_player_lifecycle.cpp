#include <hlclient/games/halflife/local_player_lifecycle.hpp>
#include <hlclient/games/halflife/damage_respawn_script.hpp>
#include <hlclient/games/halflife/inventory.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
namespace client = hlclient::client;
client::RuntimeClientObservationState observed(std::size_t ordinal, double hp,
                                              std::uint32_t deadflag = 0U) {
    client::RuntimeClientObservationState state;
    state.generation = 1; state.publication_revision = ordinal;
    state.client_metadata = {1, client::RuntimeObservationFreshness::observed_in_record,
        client::RuntimeObservationCompleteness::complete_reconstruction,
        client::RuntimeObservationSource{ordinal,ordinal,static_cast<std::uint32_t>(ordinal),0,8}};
    state.receiving_client.emplace();
    state.receiving_client->health = hp; state.receiving_client->dead_flag = deadflag;
    state.receiving_client->origin = {1,2,3}; state.receiving_client->view_offset.z = 28;
    state.receiving_client->viewmodel_index = 59;
    state.receiving_client->owned_weapon_bits = 6;
    state.weapon_hud.active_weapon_id = 2;
    state.weapon_hud.catalogue = {{.id=2,.command_name="weapon_9mmhandgun"},
                                 {.id=1,.command_name="weapon_crowbar"}};
    return state;
}
}
TEST_CASE("C lifecycle remains unknown without receiving context and accepts ordered forced respawn",
          "[damage-respawn][lifecycle]") {
    client::RuntimeClientObservationState unknown;
    CHECK(hlclient::games::halflife::advance_local_player_lifecycle(nullptr,unknown,1).state ==
          client::LocalPlayerLifeState::unknown);
    auto alive = observed(1,100);
    alive.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(nullptr,alive,1);
    CHECK(alive.lifecycle.life_epoch == 1);
    auto zero = observed(2,0);
    zero.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(&alive,zero,1);
    CHECK_FALSE(zero.lifecycle.dead());
    auto dead = observed(3,0,1);
    dead.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(&zero,dead,1);
    REQUIRE(dead.lifecycle.dead());
    auto respawnable = observed(4,0,3);
    respawnable.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(&dead,respawnable,1);
    CHECK(respawnable.lifecycle.state == client::LocalPlayerLifeState::awaiting_respawn);
    CHECK(respawnable.lifecycle.deaths == 1);
    auto new_life = observed(5,63); // no InitHUD/ResetHUD required; not hardcoded 100
    new_life.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(&respawnable,new_life,1);
    CHECK(new_life.lifecycle.respawns == 1);
    CHECK(new_life.lifecycle.life_epoch == 2);
    CHECK(hlclient::games::halflife::advance_local_player_lifecycle(&new_life,new_life,1).life_epoch == 2);
    auto delayed = observed(6,90);
    delayed.client_metadata.source->reassembled = true;
    CHECK(hlclient::games::halflife::advance_local_player_lifecycle(&dead,delayed,1).dead());
}
TEST_CASE("C ordered positive clientdata retires a stale pending notice but not a later same-record notice",
          "[damage-respawn][lifecycle]") {
    auto initial = observed(1,71);
    client::RuntimeLifeEvent notice;
    notice.kind=client::RuntimeLifeEventKind::death_notice;
    notice.victim_entity=1;
    notice.source={1,1,1,16,48}; // after the positive clientdata cursor in this record
    initial.life_events.push_back(notice);
    initial.lifecycle=hlclient::games::halflife::advance_local_player_lifecycle(nullptr,initial,1);
    REQUIRE(initial.lifecycle.pending_death);
    auto later_alive=observed(2,71);
    later_alive.lifecycle=hlclient::games::halflife::advance_local_player_lifecycle(&initial,later_alive,1);
    CHECK_FALSE(later_alive.lifecycle.pending_death);
    auto zero=observed(3,0);
    zero.lifecycle=hlclient::games::halflife::advance_local_player_lifecycle(&later_alive,zero,1);
    CHECK_FALSE(zero.lifecycle.dead());
    auto death=observed(2,0);
    death.lifecycle=hlclient::games::halflife::advance_local_player_lifecycle(&initial,death,1);
    CHECK(death.lifecycle.dead()); // preceding positive sample cannot erase a later notice
}

TEST_CASE("C zero-amount Damage is a bounded feedback event, not a fabricated health delta",
          "[damage-respawn][lifecycle]") {
    auto before = observed(1,71);
    before.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(nullptr,before,1);
    auto event_state = observed(2,71);
    client::RuntimeLifeEvent event;
    event.kind = client::RuntimeLifeEventKind::damage;
    event.damage_bits = 0x80000000U;
    event.source = {2,2,2,0,104};
    event_state.life_events.push_back(event);
    event_state.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(&before,event_state,1);
    CHECK(event_state.lifecycle.feedback_revision == 1);
    CHECK(event_state.receiving_client->health == 71);
    CHECK(hlclient::games::halflife::advance_local_player_lifecycle(&event_state,event_state,1).feedback_revision == 1);
}

TEST_CASE("C local DeathMsg can corroborate retained zero HP without a new clientdata header",
          "[damage-respawn][lifecycle]") {
    auto alive = observed(1,100);
    alive.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(nullptr,alive,1);
    auto zero = observed(2,0);
    zero.lifecycle = hlclient::games::halflife::advance_local_player_lifecycle(&alive,zero,1);
    auto notice = zero;
    notice.client_metadata.freshness = client::RuntimeObservationFreshness::retained;
    client::RuntimeLifeEvent event;
    event.kind = client::RuntimeLifeEventKind::death_notice;
    event.victim_entity=1;
    event.source = {3,3,3,0,32};
    notice.life_events.push_back(event);
    notice.lifecycle=hlclient::games::halflife::advance_local_player_lifecycle(&zero,notice,1);
    REQUIRE(notice.lifecycle.dead());
    CHECK(notice.lifecycle.boundary_source == zero.client_metadata.source);
    CHECK(hlclient::games::halflife::advance_local_player_lifecycle(&notice,notice,1).deaths == 1);
}

TEST_CASE("C script waits for real death/new life, emits release-press and finishes with both bindings",
          "[damage-respawn][script]") {
    hlclient::games::halflife::DamageRespawnScript script;
    auto alive = observed(1,100);
    alive.lifecycle.state = client::LocalPlayerLifeState::alive;
    alive.lifecycle.life_epoch = 1;
    CHECK(script.command(&alive,0).request_self_kill);
    CHECK_FALSE(script.command(&alive,0.1).request_self_kill);
    auto dead = observed(2,0,2);
    dead.lifecycle = { .state=client::LocalPlayerLifeState::dead,.life_epoch=1,.deaths=1 };
    CHECK(script.command(&dead,0.2).buttons == 0);
    CHECK(script.command(&dead,0.4).buttons == 0);
    CHECK(script.command(&dead,0.52).buttons == 1);
    CHECK(script.command(&dead,0.7).buttons == 0);
    CHECK_FALSE(script.snapshot().server_alive);
    auto new_life = observed(3,88);
    new_life.lifecycle = { .state=client::LocalPlayerLifeState::alive,.life_epoch=2,.deaths=1,.respawns=1 };
    CHECK(script.command(&new_life,0.8).buttons == 0);
    std::optional<std::uint8_t> selected;
    for (std::size_t i = 0; i < 40; ++i) {
        ++new_life.client_metadata.source->record_identity;
        ++new_life.client_metadata.source->record_ordinal;
        const auto directive = script.command(&new_life,0.82 + static_cast<double>(i)*0.02);
        if (directive.select_weapon) selected = directive.select_weapon;
    }
    auto request = script.command(&new_life,1.64);
    if (!request.select_weapon) request = script.command(&new_life,1.66);
    if (request.select_weapon) selected = request.select_weapon;
    CHECK(selected == 1);
    new_life.weapon_hud.active_weapon_id = 1;
    new_life.receiving_client->viewmodel_index = 60;
    (void)script.command(&new_life,1.7);
    (void)script.command(&new_life,2.12);
    CHECK(script.snapshot().phase == hlclient::games::halflife::DamageRespawnPhase::complete);
    CHECK(script.snapshot().glock_bound);
    CHECK(script.snapshot().crowbar_bound);
    CHECK(script.snapshot().post_respawn_commands > 10);
    const auto kill = hlclient::games::halflife::build_self_kill_request();
    REQUIRE(kill);
    CHECK(kill.bytes->size() == 6);
    CHECK(kill.bytes->front() == std::byte{3});
}
TEST_CASE("C script has a precise deadline and cannot mistake elapsed time for respawn",
          "[damage-respawn][script]") {
    hlclient::games::halflife::DamageRespawnScript script;
    auto alive = observed(1,100);
    alive.lifecycle.state = client::LocalPlayerLifeState::alive;
    (void)script.command(&alive,0);
    (void)script.command(&alive,7.1);
    CHECK(script.snapshot().phase == hlclient::games::halflife::DamageRespawnPhase::blocked);
    CHECK(script.snapshot().blocker == "awaiting_own_death");
    CHECK_FALSE(script.snapshot().server_alive);
}
