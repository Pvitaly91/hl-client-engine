#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/games/halflife/weapon_presentation_script.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace {
namespace app = hlclient::games::halflife;
namespace client = hlclient::client;

client::RuntimeClientObservationState weapon_state(
    const std::uint8_t id, const std::uint32_t model,
    const std::int32_t clip, const std::uint8_t reserve,
    const std::size_t ordinal) {
  client::RuntimeClientObservationState state;
  state.generation = 1U;
  state.publication_revision = ordinal;
  state.client_metadata.generation = 1U;
  state.client_metadata.freshness =
      client::RuntimeObservationFreshness::observed_in_record;
  state.client_metadata.source =
      client::RuntimeObservationSource{.record_identity = ordinal,
                                       .record_ordinal = ordinal};
  state.receiving_client.emplace();
  state.receiving_client->viewmodel_index = model;
  state.receiving_client->punch_angle.x = 0.0;
  state.receiving_client->punch_angle.y = 0.0;
  state.receiving_client->punch_angle.z = 0.0;
  state.weapon_hud.active_weapon_id = id;
  state.weapon_hud.catalogue.push_back(
      client::RuntimeWeaponTypeObservation{.id = id,
          .command_name = id == 2U ? "weapon_9mmhandgun" : "weapon_crowbar",
          .primary_ammo_type = id == 2U ? std::int8_t{1} : std::int8_t{-1}});
  state.weapon_hud.reserve_ammo[1U] = reserve;
  state.weapon_hud.clips[id] = static_cast<std::int16_t>(clip);
  state.weapon_slots.push_back(client::RuntimeWeaponSlotObservation{
      .wire_slot = id, .clip = clip, .in_reload = false,
      .next_primary_attack = 0.0, .weapon_id = id});
  return state;
}

app::LocalWeaponSubmittedCommand command(const std::uint32_t sequence,
    const std::uint16_t buttons, const double time) {
  return {1U, sequence, buttons, time};
}
app::LocalWeaponModelMetadata metadata(const std::uint8_t id, const std::uint32_t index) {
  app::LocalWeaponModelMetadata model;
  model.generation = 1U; model.model_index = index; model.resource_revision = 1U;
  model.resource_name = id == 2U ? "models/v_9mmhandgun.mdl" : "models/v_crowbar.mdl";
  model.supported_bodies.fill(true);
  model.sequences.resize(id == 2U ? 10U : 9U, {30.0, 16U, false});
  model.sequences[0U].looping = true;
  model.sequences[5U].frame_count = id == 2U ? 46U : 16U;
  if (id == 2U) model.sequences[6U].frame_count = 46U;
  return model;
}
void observe(app::LocalWeaponPresentationController& controller,
             const client::RuntimeClientObservationState& state, double at) {
  controller.bind_model(metadata(*state.weapon_hud.active_weapon_id,
                                *state.receiving_client->viewmodel_index));
  controller.observe(state, at);
}
} // namespace

TEST_CASE("Submitted Glock action predicts only visual state and fresh clip confirms it",
          "[weapon-presentation][glock]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U, 59U, 17, 68U, 1U);
  observe(controller, state, 0.0);
  controller.submit(command(1U, hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                            0.02), 0.02);
  auto predicted = controller.sample(0.02);
  REQUIRE(predicted.visual);
  CHECK(predicted.visual->sequence == 3U);
  CHECK(predicted.visual->body == 2U);
  CHECK(predicted.status == app::LocalWeaponActionStatus::predicted_pending);
  CHECK(predicted.local_punch_pitch_degrees == Catch::Approx(-2.0));
  CHECK(state.weapon_slots.front().clip == 17);
  const auto identity = predicted.visual->restart_identity;
  controller.submit(command(1U, hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                            0.02), 0.03); // backup/replay identity
  controller.submit(command(2U, hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                            0.04), 0.04); // held command, not another shot
  CHECK(controller.sample(0.04).actions_started == 1U);
  auto fresh = weapon_state(2U, 59U, 16, 68U, 2U);
  observe(controller, fresh, 0.07);
  auto confirmed = controller.sample(0.07);
  REQUIRE(confirmed.visual);
  CHECK(confirmed.visual->restart_identity == identity);
  CHECK(confirmed.status == app::LocalWeaponActionStatus::server_confirmed);
  CHECK(confirmed.actions_confirmed == 1U);
  CHECK(confirmed.duplicate_submissions == 1U);
  CHECK(controller.sample(0.1).local_punch_pitch_degrees < 0.0);
  CHECK(controller.sample(0.25).local_punch_pitch_degrees == 0.0);
  CHECK(fresh.weapon_slots.front().clip == 16);
}

TEST_CASE("R1 completed Glock action enters one continuous new idle timeline",
          "[r1-viewmodel-transition][weapon-presentation]") {
  app::LocalWeaponPresentationController controller;
  auto state=weapon_state(2U,59U,17,68U,1U);
  observe(controller,state,0.0);
  const auto idle_before=controller.sample(0.0);
  REQUIRE(idle_before.visual);
  controller.submit(command(1U,hlclient::goldsrc::kReferenceGoldSrcButtonAttack,0.02),0.02);
  const auto firing=controller.sample(0.02);
  REQUIRE(firing.visual);
  CHECK(firing.visual->sequence==3U);
  auto fresh=weapon_state(2U,59U,16,68U,2U);
  observe(controller,fresh,0.07);
  CHECK(controller.sample(0.07).visual->restart_identity==firing.visual->restart_identity);
  const auto transition=controller.sample(0.55); // fire reaches its terminal frame
  REQUIRE(transition.visual);
  CHECK(transition.visual->sequence==0U);
  CHECK(transition.visual->body==0U);
  CHECK(transition.visual->restart_identity!=idle_before.visual->restart_identity);
  CHECK(transition.frame_coordinate==0.0);
  const auto next=controller.sample(0.56);
  REQUIRE(next.visual);
  CHECK(next.visual->restart_identity==transition.visual->restart_identity);
  CHECK(next.frame_coordinate==Catch::Approx(0.3));
  const auto repeated=controller.sample(0.56);
  REQUIRE(repeated.visual);
  CHECK(repeated.visual->restart_identity==next.visual->restart_identity);
  CHECK(repeated.frame_coordinate==next.frame_coordinate);
}

TEST_CASE("R1 both Glock reload variants enter idle once at irregular render cadence",
          "[r1-viewmodel-transition][weapon-presentation][reload]") {
  for (const std::int32_t initial_clip : {0,10}) {
    for (const int fps : {30,60,144}) {
      CAPTURE(initial_clip,fps);
      app::LocalWeaponPresentationController controller;
      auto state=weapon_state(2U,59U,initial_clip,68U,1U);
      observe(controller,state,0.0);
      const auto old_idle=controller.sample(0.0);
      REQUIRE(old_idle.visual);
      controller.submit(command(1U,hlclient::goldsrc::kReferenceGoldSrcButtonReload,0.02),0.02);
      const auto reload=controller.sample(0.02);
      REQUIRE(reload.visual);
      CHECK(reload.visual->sequence==(initial_clip==0 ? 5U : 6U));
      auto server=weapon_state(2U,59U,initial_clip,68U,2U);
      server.weapon_slots.front().in_reload=true;
      observe(controller,server,0.08);
      CHECK(controller.sample(0.08).actions_confirmed==1U);
      const double finish=1.6;
      const auto ended=controller.sample(finish);
      REQUIRE(ended.visual);
      CHECK(ended.visual->sequence==0U);
      CHECK(ended.visual->restart_identity!=old_idle.visual->restart_identity);
      for (const double at : {finish+1.0/fps,finish+2.0/fps,finish+0.073}) {
        const auto idle=controller.sample(at);
        REQUIRE(idle.visual);
        CHECK(idle.visual->restart_identity==ended.visual->restart_identity);
        CHECK(idle.frame_coordinate==Catch::Approx((at-finish)*30.0));
      }
      CHECK(controller.sample(finish+0.073).actions_confirmed==1U);
    }
  }
}

TEST_CASE("C death cancels reload/recoil and respawn presentation requires a local release",
          "[weapon-presentation][damage-respawn]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2,59,8,68,1);
  state.lifecycle.life_epoch = 1;
  state.lifecycle.state = client::LocalPlayerLifeState::alive;
  state.receiving_client->health = 100;
  observe(controller,state,0);
  controller.submit(command(1,1,0.02),0.02);
  CHECK(controller.sample(0.02).local_punch_pitch_degrees < 0);
  auto dead = weapon_state(2,59,7,68,2);
  dead.receiving_client->health = 0;
  dead.lifecycle = {.state=client::LocalPlayerLifeState::dead,.life_epoch=1,.deaths=1};
  dead.lifecycle.last_death_source = dead.client_metadata.source;
  observe(controller,dead,0.1);
  controller.submit(command(2,1U<<13U,0.12),0.12);
  CHECK_FALSE(controller.sample(0.12).action);
  CHECK(controller.sample(0.12).local_punch_pitch_degrees == 0);
  auto respawned = weapon_state(2,59,17,68,3);
  respawned.receiving_client->health = 100;
  respawned.lifecycle = dead.lifecycle;
  respawned.lifecycle.state = client::LocalPlayerLifeState::alive;
  respawned.lifecycle.life_epoch = 2;
  respawned.lifecycle.respawns = 1;
  respawned.weapon_hud.active_source = respawned.client_metadata.source;
  observe(controller,respawned,0.2);
  controller.submit(command(3,1,0.22),0.22); // held respawn press, visual barrier
  CHECK_FALSE(controller.sample(0.22).action);
  controller.submit(command(4,0,0.24),0.24);
  controller.submit(command(5,1,0.8),0.8);
  CHECK(controller.sample(0.8).action == app::LocalWeaponAction::primary_fire);
  CHECK(controller.sample(0.8).primary_fire_starts == 2);
}

TEST_CASE("C old reload animation cannot resurrect after a server-confirmed new-life binding",
          "[weapon-presentation][damage-respawn]") {
  app::LocalWeaponPresentationController controller;
  auto alive = weapon_state(2,59,8,68,1);
  alive.lifecycle.state = client::LocalPlayerLifeState::alive;
  alive.lifecycle.life_epoch = 1;
  alive.receiving_client->health = 97;
  observe(controller,alive,0);
  controller.submit(command(1,hlclient::goldsrc::kReferenceGoldSrcButtonReload,0.02),0.02);
  REQUIRE(controller.sample(0.02).action == app::LocalWeaponAction::reload);
  auto dead = weapon_state(2,59,8,68,2);
  dead.lifecycle = {.state=client::LocalPlayerLifeState::dead,.life_epoch=1,.deaths=1};
  dead.lifecycle.last_death_source = dead.client_metadata.source;
  dead.receiving_client->health = 0;
  observe(controller,dead,0.1);
  CHECK_FALSE(controller.sample(0.1).action);
  auto next = weapon_state(2,59,17,68,3);
  next.lifecycle = dead.lifecycle;
  next.lifecycle.state = client::LocalPlayerLifeState::alive;
  next.lifecycle.life_epoch = 2;
  next.receiving_client->health = 73;
  next.receiving_client->weapon_animation = 0;
  next.weapon_hud.active_source = next.client_metadata.source;
  observe(controller,next,0.2);
  const auto restored = controller.sample(0.2);
  CHECK_FALSE(restored.action);
  REQUIRE(restored.visual);
  CHECK(restored.visual->sequence == 0);
}

TEST_CASE("Glock empty shot, reload and server event use distinct presentation sources",
          "[weapon-presentation][glock][reload]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U, 59U, 1, 68U, 1U);
  observe(controller, state, 0.0);
  controller.submit(command(1U, hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                            0.02), 0.02);
  REQUIRE(controller.sample(0.02).visual);
  CHECK(controller.sample(0.02).visual->sequence == 4U);
  auto shot = weapon_state(2U, 59U, 0, 68U, 2U);
  observe(controller, shot, 0.08);
  CHECK(controller.sample(0.08).actions_confirmed == 1U);
  controller.submit(command(2U, hlclient::goldsrc::kReferenceGoldSrcButtonReload,
                            0.1), 0.1);
  auto reload = controller.sample(0.1);
  REQUIRE(reload.visual);
  CHECK(reload.visual->sequence == 5U);
  CHECK(shot.weapon_slots.front().clip == 0);
  auto loading = weapon_state(2U, 59U, 0, 68U, 3U);
  loading.weapon_slots.front().in_reload = true;
  observe(controller, loading, 0.18);
  CHECK(controller.sample(0.18).status ==
        app::LocalWeaponActionStatus::server_confirmed);
  auto done = weapon_state(2U, 59U, 17, 51U, 4U);
  observe(controller, done, 1.7);
  CHECK(done.weapon_slots.front().clip == 17);
  CHECK(done.weapon_hud.reserve_ammo[1U] == 51U);
  done.weapon_hud.animation_sequence = std::uint8_t{0U};
  done.weapon_hud.animation_body = std::uint8_t{2U};
  done.weapon_hud.animation_source =
      client::RuntimeObservationSource{.record_identity = 5U,
                                       .record_ordinal = 5U};
  observe(controller, done, 1.8);
  auto exact = controller.sample(1.8);
  REQUIRE(exact.visual);
  CHECK(exact.visual->sequence == 0U);
  CHECK(exact.visual->source == app::LocalWeaponAnimationSource::service_event);
  const auto identity = exact.visual->restart_identity;
  observe(controller, done, 1.9);
  CHECK(controller.sample(1.9).visual->restart_identity == identity);
}

TEST_CASE("Crowbar miss cycle is bounded and never claims hit",
          "[weapon-presentation][crowbar]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(1U, 56U, -1, 0U, 1U);
  observe(controller, state, 0.0);
  for (std::uint32_t index = 0U; index < 3U; ++index) {
    const double at = index * 0.6 + 0.02;
    if (index > 0U) {
      auto ready = weapon_state(1U, 56U, -1, 0U, index * 2U + 1U);
      observe(controller, ready, at - 0.01);
    }
    controller.submit(command(index + 1U,
        hlclient::goldsrc::kReferenceGoldSrcButtonAttack, at), at);
    auto visual = controller.sample(at);
    REQUIRE(visual.visual);
    CHECK(visual.visual->sequence == (index == 2U ? 7U : 4U + index));
    CHECK(visual.visual->body == 1U);
    CHECK(visual.action == app::LocalWeaponAction::melee_swing);
    auto next = weapon_state(1U, 56U, -1, 0U, index * 2U + 2U);
    next.weapon_slots.front().next_primary_attack = 0.5;
    observe(controller, next, at + 0.05);
    CHECK(controller.sample(at + 0.05).actions_confirmed == index + 1U);
    state = next;
  }
  CHECK(controller.sample(1.4).melee_swing_starts == 3U);
}

TEST_CASE("Unconfirmed action times out and server punch suppresses duplicate visual recoil",
          "[weapon-presentation][recoil]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U, 59U, 17, 68U, 1U);
  observe(controller, state, 0.0);
  controller.submit(command(1U, hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                            0.02), 0.02);
  const auto at_one_tenth = controller.sample(0.1).local_punch_pitch_degrees;
  CHECK(at_one_tenth < 0.0);
  CHECK(controller.sample(0.1).local_punch_pitch_degrees ==
        Catch::Approx(at_one_tenth)); // independent of render-call count
  auto server = weapon_state(2U, 59U, 17, 68U, 2U);
  server.receiving_client->punch_angle.x = -1.0;
  observe(controller, server, 0.12);
  CHECK(controller.sample(0.12).local_punch_pitch_degrees == 0.0);
  CHECK(controller.sample(0.8).actions_rejected == 1U);
  REQUIRE(controller.sample(0.8).visual);
  CHECK(controller.sample(0.8).visual->sequence == 0U);
}

TEST_CASE("Same-action exact confirmation preserves restart and conflicts publish nothing",
          "[weapon-presentation][duplicates][reload]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U,59U,15,68U,1U);
  observe(controller,state,0.0);
  controller.submit(command(1U,hlclient::goldsrc::kReferenceGoldSrcButtonReload,0.02),0.02);
  const auto before = controller.sample(0.02);
  REQUIRE(before.visual);
  auto fresh = weapon_state(2U,59U,15,68U,2U);
  fresh.weapon_slots.front().in_reload = true;
  fresh.weapon_hud.animation_sequence = std::uint8_t{6U}; fresh.weapon_hud.animation_body = std::uint8_t{0U};
  fresh.weapon_hud.animation_source = fresh.client_metadata.source;
  observe(controller,fresh,0.08);
  const auto confirmed = controller.sample(0.08);
  REQUIRE(confirmed.visual);
  CHECK(confirmed.visual->restart_identity == before.visual->restart_identity);
  CHECK(confirmed.actions_confirmed == 1U);
  observe(controller,fresh,0.1);
  CHECK(controller.sample(0.1).visual->restart_identity == before.visual->restart_identity);
  auto conflict = fresh;
  conflict.weapon_hud.animation_sequence = std::uint8_t{5U};
  controller.observe(conflict,0.12);
  const auto rejected = controller.sample(0.12);
  CHECK(rejected.error == app::LocalWeaponPresentationError::conflicting_event);
  CHECK(rejected.visual->sequence == 6U);
  CHECK(rejected.actions_confirmed == 1U);
  controller.submit(command(2U,hlclient::goldsrc::kReferenceGoldSrcButtonReload,0.14),0.14);
  CHECK(controller.sample(0.14).reload_starts == 1U);
}
TEST_CASE("Exact correction wins once including crowbar hit pose without a damage claim",
          "[weapon-presentation][crowbar][precedence]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(1U,56U,-1,0U,1U);
  observe(controller,state,0.0);
  controller.submit(command(1U,hlclient::goldsrc::kReferenceGoldSrcButtonAttack,0.02),0.02);
  const auto predicted = controller.sample(0.02);
  auto fresh = weapon_state(1U,56U,-1,0U,2U);
  fresh.weapon_slots.front().next_primary_attack = 0.25;
  fresh.receiving_client->weapon_animation = 4U;
  fresh.weapon_hud.animation_sequence = std::uint8_t{3U}; fresh.weapon_hud.animation_body = std::uint8_t{1U};
  fresh.weapon_hud.animation_source = fresh.client_metadata.source;
  observe(controller,fresh,0.07);
  auto corrected = controller.sample(0.07);
  REQUIRE(corrected.visual);
  CHECK(corrected.visual->sequence == 3U);
  CHECK(corrected.visual->restart_identity != predicted.visual->restart_identity);
  CHECK(corrected.visual->source == app::LocalWeaponAnimationSource::service_event);
  CHECK(corrected.status == app::LocalWeaponActionStatus::server_corrected);
  CHECK(corrected.actions_corrected == 1U);
  CHECK_FALSE(corrected.hit_status_available);
  observe(controller,fresh,0.08);
  CHECK(controller.sample(0.08).visual->restart_identity == corrected.visual->restart_identity);
}
TEST_CASE("Model contract fails closed, revalidates revision, and sequence metadata ends actions",
          "[weapon-presentation][timeline][profile]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U,59U,17,68U,1U);
  controller.observe(state,0.0);
  controller.submit(command(1U,1U,0.02),0.02);
  CHECK(controller.sample(0.02).actions_started == 0U);
  auto model = metadata(2U,59U);
  controller.bind_model(model); controller.observe(state,0.03);
  controller.submit(command(2U,1U,0.04),0.04);
  REQUIRE(controller.sample(0.04).identity);
  CHECK(controller.sample(0.04).identity->resource_revision == 1U);
  auto confirmed = weapon_state(2U,59U,16,68U,2U);
  controller.observe(confirmed,0.08);
  auto ended = controller.sample(0.55);
  REQUIRE(ended.visual);
  CHECK(ended.animation_completed);
  CHECK(ended.visual->sequence == 0U);
  const auto idle_restart = ended.visual->restart_identity;
  CHECK(controller.sample(0.7).visual->restart_identity == idle_restart);
  model.resource_revision = 2U;
  model.sequences.resize(3U);
  controller.bind_model(model); controller.observe(confirmed,0.71);
  controller.submit(command(3U,1U,0.72),0.72);
  CHECK(controller.sample(0.72).actions_started == 1U);
  CHECK(controller.sample(0.72).error == app::LocalWeaponPresentationError::unsupported_model);
}
TEST_CASE("Focus loss cancels pending, retains confirmed, and switches cancel old reload",
          "[weapon-presentation][focus][reload]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U,59U,15,68U,1U);
  observe(controller,state,0.0);
  controller.submit(command(1U,1U,0.02),0.02);
  controller.cancel_uncommitted();
  CHECK(controller.sample(0.03).actions_rejected == 1U);
  CHECK(controller.sample(0.03).local_punch_pitch_degrees == 0.0);
  controller.submit(command(2U,1U << 13U,0.04),0.04);
  auto loading = weapon_state(2U,59U,15,68U,2U);
  loading.weapon_slots.front().in_reload = true;
  observe(controller,loading,0.08);
  controller.cancel_uncommitted();
  CHECK(controller.sample(0.09).status == app::LocalWeaponActionStatus::server_confirmed);
  auto crowbar = weapon_state(1U,56U,-1,0U,3U);
  observe(controller,crowbar,0.1);
  CHECK_FALSE(controller.sample(0.1).action);
  CHECK(loading.weapon_slots.front().clip == 15);
}
TEST_CASE("Command replay and conflicting retry never restart animation or punch",
          "[weapon-presentation][replay]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U,59U,17,68U,1U);
  observe(controller,state,0.0);
  auto submitted = command(1U,1U,0.02);
  controller.submit(submitted,0.02);
  const auto first = controller.sample(0.02);
  controller.submit(submitted,0.03);
  submitted.buttons = 1U << 13U;
  controller.submit(submitted,0.04);
  auto result = controller.sample(0.04);
  CHECK(result.actions_started == 1U);
  CHECK(result.recoil_starts == 1U);
  CHECK(result.visual->restart_identity == first.visual->restart_identity);
  CHECK(result.error == app::LocalWeaponPresentationError::conflicting_command);
  CHECK(result.conflicting_duplicates == 1U);
}
TEST_CASE("Reference recoil recurrence is bounded and independent of 30 60 144 render sampling",
          "[weapon-presentation][recoil][fps]") {
  for (const int fps : {30,60,144}) {
    app::LocalWeaponPresentationController controller;
    auto state = weapon_state(2U,59U,17,68U,1U);
    const auto canonical = state;
    observe(controller,state,0.0);
    const auto submitted = command(1U,1U,0.02);
    controller.submit(submitted,0.02);
    CHECK(controller.sample(0.02).local_punch_pitch_degrees == Catch::Approx(-2.0));
    for (int frame = 1; frame < fps / 10; ++frame)
      (void)controller.sample(0.02 + static_cast<double>(frame) / fps);
    CHECK(controller.sample(0.12).local_punch_pitch_degrees ==
      Catch::Approx(-(22.0 * std::pow(0.99,5.0) - 20.0)));
    CHECK(state == canonical);
    CHECK(submitted == command(1U,1U,0.02));
    CHECK(controller.sample(0.22).local_punch_pitch_degrees == 0.0);
  }
}
TEST_CASE("Held cadence ages supported timers without mutating canonical cooldown or ammo",
          "[weapon-presentation][cadence]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U,59U,17,68U,1U);
  observe(controller,state,0.0);
  controller.submit(command(1U,1U,0.02),0.02);
  auto shot = weapon_state(2U,59U,16,68U,2U);
  shot.weapon_slots.front().next_primary_attack = 0.3;
  observe(controller,shot,0.07);
  for (std::uint32_t seq = 2; seq < 16; ++seq) {
    const double time = 0.07 + 0.02 * seq;
    controller.submit(command(seq,1U,time),time);
  }
  CHECK(controller.sample(0.37).actions_started == 2U);
  CHECK(shot.weapon_slots.front().clip == 16);
  CHECK(shot.weapon_slots.front().next_primary_attack == 0.3);
}

TEST_CASE("Explicit presentation script waits for each canonical shot and reload before swing",
          "[weapon-presentation][script]") {
  hlclient::games::halflife::WeaponPresentationScript script;
  auto state = weapon_state(2U,59U,17,68U,1U);
  CHECK(script.buttons(&state,0U,0.5) == 0U);
  CHECK(script.buttons(&state,1U,1.5) == 1U);
  CHECK(script.buttons(&state,1U,2.3) == 0U); // no blind retry
  state = weapon_state(2U,59U,16,68U,2U);
  CHECK(script.buttons(&state,1U,2.32) == 1U);
  CHECK(script.shots_confirmed() == 1U);
  CHECK(script.buttons(&state,2U,4.5) == 0U); // second still unconfirmed
  state = weapon_state(2U,59U,15,68U,3U);
  CHECK(script.buttons(&state,2U,4.52) == (1U << 13U));
  CHECK(script.shots_confirmed() == 2U);
  CHECK(script.buttons(&state,2U,4.54) == 0U);
  auto crowbar = weapon_state(1U,56U,-1,0U,4U);
  CHECK(script.buttons(&crowbar,3U,8.0) == 0U);
  state = weapon_state(2U,59U,17,66U,5U);
  CHECK(script.buttons(&state,2U,8.02) == 0U);
  CHECK(script.reload_completed());
  crowbar.publication_revision = 6U;
  crowbar.client_metadata.source = client::RuntimeObservationSource{6U,6U};
  CHECK(script.buttons(&crowbar,3U,8.04) == 0U);
  CHECK(script.buttons(&crowbar,3U,8.06) == 0U);
  CHECK(script.buttons(&crowbar,3U,8.8) == 1U);
  CHECK(script.buttons(&crowbar,3U,8.82) == 0U);
}

TEST_CASE("Fresh client attack timer gates local eligibility and confirms reload start",
          "[weapon-presentation][timer][authority]") {
  app::LocalWeaponPresentationController controller;
  auto state = weapon_state(2U,59U,15,68U,1U);
  state.receiving_client->next_weapon_attack = 0.4;
  observe(controller,state,0.0);
  controller.submit(command(1U,1U,0.02),0.02);
  CHECK(controller.sample(0.02).actions_started == 0U);
  controller.submit(command(2U,1U<<13U,0.42),0.42);
  REQUIRE(controller.sample(0.42).visual);
  CHECK(controller.sample(0.42).visual->sequence == 6U);
  auto loading = weapon_state(2U,59U,15,68U,2U);
  loading.weapon_slots.front().in_reload.reset();
  loading.receiving_client->next_weapon_attack = 1.5;
  observe(controller,loading,0.48);
  CHECK(controller.sample(0.48).reload_confirmed == 1U);
  CHECK(state.receiving_client->next_weapon_attack == 0.4);
}
