#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace {
namespace hl = hlclient::games::halflife;
namespace api = hlclient::game_api;
namespace client = hlclient::client;
namespace wire = hlclient::goldsrc;
api::LocalWeaponModelMetadata shotgun_model() {
  api::LocalWeaponModelMetadata model;
  model.generation=1;model.model_index=70;model.resource_revision=1;
  model.resource_name="models/v_shotgun.mdl";
  model.supported_bodies.fill(true);
  // Project-owned numeric fixture, independently fixed to inspected role
  // metadata. No installed model is a prerequisite for these regressions.
  constexpr std::array labels{"sm_idle","shoot","shoot_big","reload","pump",
      "start_reload","draw","reholster","idle4","deepidle"};
  constexpr std::array frames{21U,21U,21U,10U,11U,9U,13U,11U,21U,61U};
  constexpr std::array fps{9.0,20.0,13.0,16.0,13.0,12.0,24.0,30.0,9.0,12.0};
  for(std::size_t i=0;i<labels.size();++i)
    model.sequences.push_back({fps[i],frames[i],i==0 || i==8 || i==9,{},labels[i]});
  return model;
}
client::RuntimeClientObservationState shotgun_state(std::int32_t clip,
    std::uint8_t reserve,std::size_t record,std::int32_t stage=0) {
  client::RuntimeClientObservationState state;
  state.generation=1;state.publication_revision=record;
  state.client_metadata={.generation=1,
      .freshness=client::RuntimeObservationFreshness::observed_in_record,
      .source=client::RuntimeObservationSource{.record_identity=record,.record_ordinal=record}};
  state.receiving_client.emplace();
  state.receiving_client->viewmodel_index=70;state.receiving_client->health=100;
  state.receiving_client->water_level=0;state.receiving_client->next_weapon_attack=0;
  state.receiving_client->punch_angle={0.0,0.0,0.0};
  state.weapon_hud.active_weapon_id=7;
  state.weapon_hud.catalogue.push_back({.id=7,.command_name="weapon_shotgun",.primary_ammo_type=2});
  state.weapon_hud.reserve_ammo[2]=reserve;state.weapon_hud.clips[7]=static_cast<std::int16_t>(clip);
  state.weapon_slots.push_back({.wire_slot=7,.clip=clip,.in_reload=false,
      .next_reload=0.0,.next_primary_attack=0.0,.weapon_id=7,
      .next_secondary_attack=0.0,.time_weapon_idle=0.0,
      .pump_time=std::nullopt,.special_reload_state=stage});
  return state;
}
api::LocalWeaponSubmittedCommand command(std::uint32_t sequence,std::uint16_t buttons,double at) {
  return {.generation=1,.sequence=sequence,.buttons=buttons,.sample_end_seconds=at};
}
void initial(hl::LocalWeaponPresentationController& controller,
    client::RuntimeClientObservationState state=shotgun_state(8,32,1)) {
  controller.bind_model(shotgun_model());controller.observe(state,0);
}
api::ScriptedEventBinding binding(std::uint16_t slot,std::string_view name) {
  api::ScriptedEventBinding result;result.index=slot;
  std::copy(name.begin(),name.end(),result.name.begin());return result;
}
api::CommittedScriptedEvent echo(std::uint16_t slot,std::uint64_t record,double at) {
  return {.generation=1,.record=record,.message_bit_offset=19,.entry_ordinal=0,
      .emitter_entity=1,.event_index=slot,.received_at_seconds=at,.record_ordinal=record};
}
}

TEST_CASE("E17 catalogue and validated model roles jointly authorize local shotgun",
          "[e17][shotgun][weapon-presentation][profile]") {
  CHECK(hl::shotgun_sequences(shotgun_model()));
  for(unsigned invalid=0;invalid<5;++invalid) {
    CAPTURE(invalid);hl::LocalWeaponPresentationController controller;
    auto state=shotgun_state(8,32,1);auto model=shotgun_model();
    if(invalid==0) state.weapon_hud.catalogue.front().command_name="weapon_other";
    if(invalid==1) model.resource_name="models/custom_shotgun.mdl";
    if(invalid==2) model.sequences[2].label="shoot";
    if(invalid==3) model.sequences[3].fps=0;
    if(invalid==4) model.supported_bodies[2]=false;
    controller.bind_model(model);controller.observe(state,0);
    controller.submit(command(1,wire::kReferenceGoldSrcButtonAttack,0.02),0.02);
    CHECK(controller.sample(0.02).actions_started==0);
    CHECK(controller.sample(0.02).error==api::LocalWeaponPresentationError::unsupported_model);
  }
}

TEST_CASE("E17 single and double retain distinct command actions recoil poses and server clip costs",
          "[e17][shotgun][weapon-presentation][cooldown]") {
  for(const bool double_shot:{false,true}) {
    CAPTURE(double_shot);hl::LocalWeaponPresentationController controller;
    const auto state=shotgun_state(8,32,1);initial(controller,state);
    const auto buttons=double_shot ? wire::kReferenceGoldSrcButtonAttack2 : wire::kReferenceGoldSrcButtonAttack;
    const auto submitted=command(1,buttons,0.02);controller.submit(submitted,0.02);
    const auto shot=controller.sample(0.02);REQUIRE(shot.identity);REQUIRE(shot.visual);
    CHECK(shot.action==(double_shot ? api::LocalWeaponAction::secondary_fire : api::LocalWeaponAction::primary_fire));
    CHECK(shot.visual->sequence==(double_shot ? 2U : 1U));CHECK(shot.visual->body==2);
    CHECK(shot.local_punch_pitch_degrees==Catch::Approx(double_shot ? -10.0 : -5.0));
    CHECK(shot.secondary_launch_starts==0);CHECK(state.weapon_slots.front().clip==8);
    controller.submit(submitted,0.03);controller.submit(command(2,buttons,0.04),0.04);
    CHECK(controller.sample(0.04).actions_started==1);CHECK(controller.sample(0.04).duplicate_submissions==1);
    const auto fresh=shotgun_state(double_shot ? 6 : 7,32,2);
    controller.observe(fresh,0.06);const auto confirmed=controller.sample(0.06);
    REQUIRE(confirmed.visual);CHECK(confirmed.visual->restart_identity==shot.visual->restart_identity);
    CHECK(confirmed.actions_confirmed==1);CHECK(confirmed.status==api::LocalWeaponActionStatus::server_confirmed);
    const double cycle=double_shot ? 1.5 : .75;
    controller.submit(command(3,buttons,0.02+cycle-.001),0.02+cycle-.001);
    CHECK(controller.sample(0.02+cycle-.001).actions_started==1);
    controller.submit(command(4,buttons,0.02+cycle),0.02+cycle);
    CHECK(controller.sample(0.02+cycle).actions_started==2);
    CHECK(fresh.weapon_slots.front().clip==(double_shot ? 6 : 7));
  }
}

TEST_CASE("E17 successful local fire rejects one-shell double empty fullwater and unready timers",
          "[e17][shotgun][weapon-presentation][eligibility]") {
  for(unsigned gate=0;gate<6;++gate) {
    CAPTURE(gate);hl::LocalWeaponPresentationController controller;auto state=shotgun_state(8,0,1);
    if(gate==0) state.weapon_slots.front().clip=1;
    if(gate==1) state.weapon_slots.front().clip=0;
    if(gate==2) state.receiving_client->water_level=3;
    if(gate==3) state.weapon_slots.front().next_secondary_attack=.5;
    if(gate==4) state.receiving_client->next_weapon_attack=.5;
    if(gate==5) state.weapon_slots.front().in_reload=true;
    initial(controller,state);
    controller.submit(command(1,wire::kReferenceGoldSrcButtonAttack |
        wire::kReferenceGoldSrcButtonAttack2,.02),.02);
    const auto result=controller.sample(.02);
    CHECK(result.primary_fire_starts==0);CHECK(result.secondary_fire_starts==0);
    CHECK(result.local_punch_pitch_degrees==0);CHECK(controller.drain_audio().count==0);
  }
  hl::LocalWeaponPresentationController priority;initial(priority);
  priority.submit(command(1,wire::kReferenceGoldSrcButtonAttack |
      wire::kReferenceGoldSrcButtonAttack2,.02),.02);
  CHECK(priority.sample(.02).action==api::LocalWeaponAction::secondary_fire);
}

TEST_CASE("E17 special reload follows committed stages without filling ammo or per-frame restart",
          "[e17][shotgun][weapon-presentation][special-reload]") {
  hl::LocalWeaponPresentationController controller;initial(controller,shotgun_state(6,2,1));
  controller.submit(command(1,wire::kReferenceGoldSrcButtonReload,.02),.02);
  const auto start=controller.sample(.02);REQUIRE(start.visual);CHECK(start.visual->sequence==5);
  CHECK(controller.drain_audio().count==0); // Reload and pump use server sound, not a duplicate marker.
  const auto stage1=shotgun_state(6,2,2,1);controller.observe(stage1,.04);
  const auto confirmed=controller.sample(.04);REQUIRE(confirmed.visual);
  CHECK(confirmed.reload_confirmed==1);CHECK(confirmed.visual->restart_identity==start.visual->restart_identity);
  CHECK(controller.sample(.70).visual->sequence==5); // terminal start pose waits for observed insert
  auto stage2=shotgun_state(6,2,3,2);stage2.weapon_slots.front().next_reload=.5;
  controller.observe(stage2,.72);const auto insert=controller.sample(.72);REQUIRE(insert.visual);
  CHECK(insert.visual->sequence==3);CHECK(insert.visual->restart_identity!=start.visual->restart_identity);
  controller.observe(stage2,.73);CHECK(controller.sample(.73).visual->restart_identity==insert.visual->restart_identity);
  const auto transfer=shotgun_state(7,1,4,1);controller.observe(transfer,1.24);
  CHECK(controller.sample(1.24).visual->restart_identity==insert.visual->restart_identity);
  CHECK(controller.sample(1.30).visual->sequence==3); // no idle blink between inserts
  const auto second=shotgun_state(7,1,5,2);controller.observe(second,1.32);
  const auto inserted_again=controller.sample(1.32);REQUIRE(inserted_again.visual);
  CHECK(inserted_again.visual->sequence==3);CHECK(inserted_again.visual->restart_identity!=insert.visual->restart_identity);
  controller.observe(shotgun_state(8,0,6,1),1.84);
  const auto finish_state=shotgun_state(8,0,7,0);controller.observe(finish_state,1.86);
  const auto finish=controller.sample(1.86);REQUIRE(finish.visual);CHECK(finish.visual->sequence==4);
  CHECK(controller.sample(2.70).visual->sequence==0);CHECK(controller.drain_audio().count==0);
  CHECK(stage1.weapon_slots.front().clip==6);CHECK(stage2.weapon_slots.front().clip==6);
  CHECK(transfer.weapon_slots.front().clip==7);CHECK(finish_state.weapon_slots.front().clip==8);
  CHECK(finish_state.weapon_hud.reserve_ammo[2]==0);
}

TEST_CASE("E17 special reload allows server eligible fire and retires on death model switch and generation",
          "[e17][shotgun][weapon-presentation][special-reload][lifecycle]") {
  hl::LocalWeaponPresentationController controller;initial(controller,shotgun_state(2,8,1,1));
  CHECK(controller.sample(0).visual->sequence==5);
  controller.observe(shotgun_state(3,7,2,2),.6);CHECK(controller.sample(.6).visual->sequence==3);
  controller.submit(command(1,wire::kReferenceGoldSrcButtonAttack,.62),.62);
  CHECK(controller.sample(.62).action==api::LocalWeaponAction::primary_fire);
  CHECK(controller.sample(.62).visual->sequence==1);
  controller.observe(shotgun_state(2,7,3,0),.64);
  CHECK(controller.sample(.64).status==api::LocalWeaponActionStatus::server_confirmed);
  auto dead=shotgun_state(2,7,4,2);dead.receiving_client->health=0;
  dead.lifecycle={.state=client::LocalPlayerLifeState::dead,.life_epoch=1,.deaths=1};
  controller.observe(dead,.7);CHECK_FALSE(controller.sample(.7).action);
  CHECK(controller.sample(.7).local_punch_pitch_degrees==0);CHECK(controller.drain_audio().count==0);
  auto new_life=shotgun_state(8,32,5,0);new_life.lifecycle={.state=client::LocalPlayerLifeState::alive,
      .life_epoch=2,.deaths=1,.respawns=1};
  controller.observe(new_life,.9);controller.submit(command(2,wire::kReferenceGoldSrcButtonReload,.92),.92);
  CHECK_FALSE(controller.sample(.92).action);
  controller.submit(command(3,0,.94),.94);controller.submit(command(4,wire::kReferenceGoldSrcButtonAttack,.96),.96);
  CHECK(controller.sample(.96).action==api::LocalWeaponAction::primary_fire);
  controller.bind_model(std::nullopt);CHECK_FALSE(controller.sample(1).action);
  CHECK(controller.drain_audio().count==0);
}

TEST_CASE("E17 assigned shotgun event echoes confirm without duplicate fire audio animation or recoil",
          "[e17][shotgun][weapon-presentation][echo]") {
  for(const bool double_shot:{false,true}) {
    CAPTURE(double_shot);hl::LocalWeaponPresentationController controller;
    const std::array bindings{binding(29,"events/shotgun1.sc"),binding(47,"events/shotgun2.sc")};
    controller.configure_scripted_events(bindings);initial(controller);
    controller.submit(command(1,double_shot ? wire::kReferenceGoldSrcButtonAttack2 :
        wire::kReferenceGoldSrcButtonAttack,.02),.02);
    const auto before=controller.sample(.02);REQUIRE(before.visual);CHECK(controller.drain_audio().count==1);
    const auto event=echo(double_shot ? 47 : 29,2,.04);controller.committed_scripted_event(event,1);
    const auto after=controller.sample(.04);REQUIRE(after.visual);
    CHECK(after.visual->restart_identity==before.visual->restart_identity);
    CHECK(after.visual->started_at_seconds==before.visual->started_at_seconds);
    CHECK(after.actions_started==1);CHECK(after.actions_confirmed==1);CHECK(after.recoil_starts==1);
    controller.committed_scripted_event(event,1);CHECK(controller.sample(.04).duplicate_events==1);
    CHECK(controller.drain_audio().count==0);
    auto remote=echo(double_shot ? 47 : 29,3,.05);remote.emitter_entity=2;
    controller.committed_scripted_event(remote,1);CHECK(controller.sample(.05).actions_started==1);
  }
}

TEST_CASE("E17 committed-only successive shotgun events are separate actions not duplicate confirmations",
          "[e17][shotgun][weapon-presentation][echo]") {
  hl::LocalWeaponPresentationController controller;
  const std::array bindings{binding(81,"events/shotgun1.sc"),binding(12,"events/shotgun2.sc")};
  controller.configure_scripted_events(bindings);initial(controller);
  controller.committed_scripted_event(echo(81,2,.02),1);
  const auto first=controller.sample(.02);REQUIRE(first.identity);
  CHECK(first.actions_started==1);CHECK(first.primary_fire_confirmed==1);
  CHECK(controller.drain_audio().count==1);
  (void)controller.sample(.8);controller.committed_scripted_event(echo(81,3,.8),1);
  const auto second=controller.sample(.8);REQUIRE(second.identity);
  CHECK(second.actions_started==2);CHECK(second.primary_fire_confirmed==2);
  CHECK(second.identity->started_at_seconds!=first.identity->started_at_seconds);
  CHECK(controller.drain_audio().count==1);
}

TEST_CASE("E17 recoil and firing cadence do not depend on render frequency",
          "[e17][shotgun][weapon-presentation][recoil]") {
  for(const bool double_shot:{false,true}) for(const unsigned fps:{30U,60U,144U,240U}) {
    CAPTURE(double_shot,fps);hl::LocalWeaponPresentationController controller;initial(controller);
    const auto buttons=double_shot ? wire::kReferenceGoldSrcButtonAttack2 : wire::kReferenceGoldSrcButtonAttack;
    controller.submit(command(1,buttons,.02),.02);
    controller.observe(shotgun_state(double_shot ? 6 : 7,32,2),.04);
    for(unsigned i=1;i<fps;++i) {
      const double at=.04+static_cast<double>(i)/fps;
      if(at>=.22) break;
      (void)controller.sample(at);
    }
    const auto sample=controller.sample(.22);
    const double expected=-std::max(0.0,(double_shot ? 30.0 : 25.0)*std::pow(.99,10.0)-20.0);
    CHECK(sample.local_punch_pitch_degrees==Catch::Approx(expected));
    CHECK(sample.actions_started==1);
    const auto repeated=controller.sample(.22);
    CHECK(repeated.local_punch_pitch_degrees==sample.local_punch_pitch_degrees);
    CHECK(repeated.visual->restart_identity==sample.visual->restart_identity);
  }
}

TEST_CASE("E17 missing special reload fields do not manufacture shell insert stages",
          "[e17][shotgun][weapon-presentation][special-reload][missing]") {
  hl::LocalWeaponPresentationController controller;auto state=shotgun_state(0,8,1);
  state.weapon_slots.front().special_reload_state.reset();initial(controller,state);
  controller.submit(command(1,wire::kReferenceGoldSrcButtonReload,.02),.02);
  CHECK(controller.sample(.02).visual->sequence==5);
  auto later=shotgun_state(0,8,2);later.weapon_slots.front().special_reload_state.reset();
  controller.observe(later,.3);const auto result=controller.sample(.7);REQUIRE(result.visual);
  CHECK(result.visual->sequence==0);CHECK(later.weapon_slots.front().clip==0);
  CHECK(later.weapon_hud.reserve_ammo[2]==8);CHECK(controller.drain_audio().count==0);
}

TEST_CASE("E17 unknown special reload stages cannot authorize provisional successful effects",
          "[e17][shotgun][weapon-presentation][special-reload][eligibility]") {
  for(const std::int32_t stage:{-1,3}) for(const auto buttons:{
      wire::kReferenceGoldSrcButtonAttack,wire::kReferenceGoldSrcButtonAttack2,
      wire::kReferenceGoldSrcButtonReload}) {
    CAPTURE(stage,buttons);hl::LocalWeaponPresentationController controller;
    initial(controller,shotgun_state(6,8,1,stage));
    controller.submit(command(1,buttons,.02),.02);
    const auto result=controller.sample(.02);
    CHECK(result.actions_started==0);CHECK_FALSE(result.identity);
    CHECK(result.error==api::LocalWeaponPresentationError::unsupported_pose);
    CHECK(result.local_punch_pitch_degrees==0);CHECK(controller.drain_audio().count==0);
  }
}

TEST_CASE("E17 delayed committed event provenance can be inspected without rewinding the render clock",
          "[e17][shotgun][weapon-presentation][echo][time]") {
  hl::LocalWeaponPresentationController controller;
  const std::array bindings{binding(81,"events/shotgun1.sc"),binding(12,"events/shotgun2.sc")};
  controller.configure_scripted_events(bindings);initial(controller);
  CHECK_FALSE(controller.current_action_identity());
  controller.observe(shotgun_state(8,32,2),1.0);
  controller.committed_scripted_event(echo(12,3,.9),1);
  const auto identity=controller.current_action_identity();REQUIRE(identity);
  CHECK(identity->started_at_seconds==.9);CHECK(identity->command_sequence==0);
  CHECK(identity->kind==api::LocalWeaponAction::secondary_fire);
  CHECK(controller.current_action_identity()==identity);
  const auto committed_audio=controller.drain_audio();REQUIRE(committed_audio.count==1);
  CHECK(committed_audio.cues[0].scheduled_seconds==.9);
  const auto sample=controller.sample(1.0);
  CHECK(sample.error==api::LocalWeaponPresentationError::none);CHECK(sample.identity==identity);
  CHECK(sample.secondary_fire_starts==1);CHECK(sample.secondary_fire_confirmed==1);
  CHECK(controller.drain_audio().count==0); // Accessor/sample do not republish the accepted event's voice.
  controller.committed_scripted_event(echo(12,4,.7),1); // Outside the existing 250ms acceptance bound.
  CHECK(controller.current_action_identity()==identity);
  CHECK(controller.sample(1.0).actions_started==1);CHECK(controller.drain_audio().count==0);
}

TEST_CASE("E17 zero health retires confirmed shotgun action before lifecycle counters change",
          "[e17][shotgun][weapon-presentation][lifecycle]") {
  for(const bool reload:{false,true}) {
    CAPTURE(reload);hl::LocalWeaponPresentationController controller;
    initial(controller,shotgun_state(6,8,1));
    controller.submit(command(1,reload ? wire::kReferenceGoldSrcButtonReload :
        wire::kReferenceGoldSrcButtonAttack2,.02),.02);
    controller.observe(shotgun_state(reload ? 6 : 4,8,2,reload ? 1 : 0),.04);
    const auto confirmed=controller.sample(.04);
    CHECK(confirmed.status==api::LocalWeaponActionStatus::server_confirmed);
    REQUIRE(confirmed.action);(void)controller.drain_audio();
    auto zero_health=shotgun_state(reload ? 6 : 4,8,3,reload ? 2 : 0);
    zero_health.receiving_client->health=0;
    // Deliberately retain identical lifecycle counters: health itself is
    // committed authoritative evidence that blocks new successful effects.
    CHECK(zero_health.lifecycle.life_epoch==0);CHECK(zero_health.lifecycle.deaths==0);
    controller.observe(zero_health,.06);const auto retired=controller.sample(.06);
    CHECK_FALSE(retired.action);CHECK_FALSE(retired.identity);CHECK_FALSE(retired.visual);
    CHECK(retired.local_punch_pitch_degrees==0);
    const auto retired_audio=controller.drain_audio();CHECK(retired_audio.count==0);
    auto repeated_dead=zero_health;repeated_dead.publication_revision=4;
    repeated_dead.client_metadata.source=client::RuntimeObservationSource{.record_identity=4,.record_ordinal=4};
    controller.observe(repeated_dead,.07);
    CHECK(controller.drain_audio().scope==retired_audio.scope); // no per-record scope churn
    controller.submit(command(2,wire::kReferenceGoldSrcButtonAttack,.08),.08);
    CHECK_FALSE(controller.sample(.08).action);
  }
}

TEST_CASE("E17 special reload weapon switch returns to fresh Shotgun inventory instead of old inserts",
          "[e17][shotgun][weapon-presentation][special-reload][lifecycle][switch]") {
  for(const std::uint8_t other:std::array<std::uint8_t,2>{2U,4U}) {
    CAPTURE(other);hl::LocalWeaponPresentationController controller;
    initial(controller,shotgun_state(2,6,1));
    controller.submit(command(1,wire::kReferenceGoldSrcButtonReload,.02),.02);
    controller.observe(shotgun_state(2,6,2,1),.04);
    controller.observe(shotgun_state(2,6,3,2),.70);
    const auto inserting=controller.sample(.70);REQUIRE(inserting.visual);
    CHECK(inserting.visual->sequence==3U);CHECK(inserting.reload_confirmed==1U);
    controller.observe(shotgun_state(3,5,4,1),1.22);
    CHECK(controller.sample(1.22).visual->restart_identity==inserting.visual->restart_identity);

    api::LocalWeaponModelMetadata model;
    model.generation=1;model.model_index=other==2U ? 59U : 21U;
    model.resource_revision=1;model.supported_bodies.fill(true);
    model.resource_name=other==2U ? "models/v_9mmhandgun.mdl" : "models/v_9mmAR.mdl";
    if(other==2U) model.sequences.resize(10U,{30.0,16U,false});
    else {
      constexpr std::array labels{"longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"};
      for(const auto* label:labels) model.sequences.push_back({30.0,16U,false,{},label});
    }
    model.sequences[0].looping=true;
    auto switched=shotgun_state(3,5,5,1); // The inactive slot legitimately retains its old values.
    switched.receiving_client->viewmodel_index=model.model_index;
    switched.weapon_hud.active_weapon_id=other;
    switched.weapon_hud.catalogue.push_back({.id=other,
        .command_name=other==2U ? "weapon_9mmhandgun" : "weapon_9mmAR",.primary_ammo_type=1});
    switched.weapon_hud.clips[other]=other==2U ? 17 : 50;
    switched.weapon_hud.reserve_ammo[1]=25;
    switched.weapon_slots.push_back({.wire_slot=other,.clip=other==2U ? 17 : 50,
        .in_reload=false,.next_primary_attack=0.0,.weapon_id=other,.next_secondary_attack=0.0});
    const auto switched_before=switched;
    controller.bind_model(model);controller.observe(switched,1.24);
    const auto away=controller.sample(1.24);REQUIRE(away.visual);
    CHECK_FALSE(away.action);CHECK(away.visual->sequence==0U);
    CHECK(controller.drain_audio().count==0U);CHECK(switched==switched_before);
    CHECK(controller.sample(2.0).visual->restart_identity==away.visual->restart_identity);

    controller.bind_model(shotgun_model());
    const auto returned=shotgun_state(1,0,6,0);controller.observe(returned,2.1);
    const auto ready=controller.sample(2.1);REQUIRE(ready.visual);
    CHECK_FALSE(ready.action);CHECK(ready.visual->sequence==0U);
    CHECK(ready.visual->restart_identity!=inserting.visual->restart_identity);
    CHECK(ready.reload_starts==1U);CHECK(ready.reload_confirmed==1U);
    controller.submit(command(2,wire::kReferenceGoldSrcButtonAttack2,2.12),2.12);
    controller.submit(command(3,wire::kReferenceGoldSrcButtonReload,2.14),2.14);
    const auto blocked=controller.sample(2.14);CHECK_FALSE(blocked.action);
    CHECK(blocked.actions_started==1U);CHECK(blocked.secondary_fire_starts==0U);
    CHECK(blocked.visual->restart_identity==ready.visual->restart_identity);
    CHECK(controller.drain_audio().count==0U); // Old reserve five cannot authorize another reload.
    controller.submit(command(4,wire::kReferenceGoldSrcButtonAttack,2.16),2.16);
    CHECK(controller.sample(2.16).primary_fire_starts==1U);
    CHECK(returned.weapon_slots[0].clip==1);CHECK(returned.weapon_hud.reserve_ammo[2]==0U);
    auto new_stage=shotgun_state(4,2,7,2);controller.observe(new_stage,2.18);
    const auto fresh_insert=controller.sample(2.18);REQUIRE(fresh_insert.visual);
    CHECK_FALSE(fresh_insert.action);CHECK(fresh_insert.visual->sequence==3U);
    CHECK(fresh_insert.visual->restart_identity!=inserting.visual->restart_identity);
    CHECK(fresh_insert.visual->restart_identity!=ready.visual->restart_identity);
    controller.observe(new_stage,2.19);
    CHECK(controller.sample(2.19).visual->restart_identity==fresh_insert.visual->restart_identity);
    CHECK(new_stage.weapon_slots[0].clip==4);CHECK(new_stage.weapon_hud.reserve_ammo[2]==2U);
  }
}

TEST_CASE("E17 retained and missing slot observations cannot manufacture a special reload occurrence",
          "[e17][shotgun][weapon-presentation][special-reload][missing][freshness]") {
  hl::LocalWeaponPresentationController controller;
  SECTION("Retained numeric changes do not replace the last committed stage") {
    const auto start=shotgun_state(2,6,1,1);initial(controller,start);
    const auto before=controller.sample(.02);REQUIRE(before.visual);
    CHECK(before.visual->sequence==5U);CHECK_FALSE(before.action);
    auto retained=shotgun_state(3,5,2,2);
    retained.client_metadata=start.client_metadata;
    retained.client_metadata.freshness=client::RuntimeObservationFreshness::retained;
    const auto original=retained;controller.observe(retained,.04);
    const auto unchanged=controller.sample(.04);REQUIRE(unchanged.visual);
    CHECK(unchanged.visual->sequence==5U);
    CHECK(unchanged.visual->restart_identity==before.visual->restart_identity);
    CHECK_FALSE(unchanged.action);CHECK(unchanged.actions_started==0U);
    CHECK(retained==original);CHECK(controller.drain_audio().count==0U);
    const auto inserting=shotgun_state(2,6,3,2);controller.observe(inserting,.72);
    const auto accepted=controller.sample(.72);REQUIRE(accepted.visual);
    CHECK(accepted.visual->sequence==3U);
    CHECK(accepted.visual->restart_identity!=before.visual->restart_identity);
    CHECK_FALSE(accepted.action);CHECK(accepted.actions_started==0U);
    controller.observe(inserting,.73);
    CHECK(controller.sample(.73).visual->restart_identity==accepted.visual->restart_identity);
  }
  SECTION("Fresh record missing Shotgun slot cannot advance retained insert state") {
    initial(controller,shotgun_state(2,6,1,2));
    const auto before=controller.sample(.02);REQUIRE(before.visual);
    CHECK(before.visual->sequence==3U);CHECK_FALSE(before.action);
    auto missing=shotgun_state(8,0,2,1);missing.weapon_slots.clear();
    const auto original=missing;controller.observe(missing,.04);
    const auto unchanged=controller.sample(.04);REQUIRE(unchanged.visual);
    CHECK(unchanged.visual->sequence==3U);
    CHECK(unchanged.visual->restart_identity==before.visual->restart_identity);
    CHECK_FALSE(unchanged.action);CHECK(unchanged.actions_started==0U);
    CHECK(missing==original);CHECK(controller.drain_audio().count==0U);
    const auto completed=controller.sample(.70);REQUIRE(completed.visual);
    CHECK(completed.visual->sequence==0U);CHECK_FALSE(completed.action);
    CHECK(completed.actions_started==0U);CHECK(controller.drain_audio().count==0U);
    const auto current=shotgun_state(4,2,3,2);controller.observe(current,.72);
    const auto accepted=controller.sample(.72);REQUIRE(accepted.visual);
    CHECK(accepted.visual->sequence==3U);
    CHECK(accepted.visual->restart_identity!=before.visual->restart_identity);
    CHECK_FALSE(accepted.action);CHECK(accepted.actions_started==0U);
    CHECK(current.weapon_slots[0].clip==4);CHECK(current.weapon_hud.reserve_ammo[2]==2U);
    controller.observe(current,.73);
    CHECK(controller.sample(.73).visual->restart_identity==accepted.visual->restart_identity);
  }
}

TEST_CASE("E17 rejected Shotgun fire releases only its own provisional shell reservation",
          "[e17][shotgun][weapon-presentation][special-reload][rejection][eligibility]") {
  for(const bool twice:{false,true}) {
    CAPTURE(twice);hl::LocalWeaponPresentationController controller;
    const std::int32_t initial_clip=twice ? 2 : 1;
    initial(controller,shotgun_state(initial_clip,0,1));
    controller.submit(command(1,twice ? wire::kReferenceGoldSrcButtonAttack2 :
        wire::kReferenceGoldSrcButtonAttack,.02),.02);
    CHECK(controller.sample(.02).actions_started==1U);
    const auto rejected=shotgun_state(initial_clip,0,2,2);controller.observe(rejected,.04);
    const auto stage=controller.sample(.04);REQUIRE(stage.visual);
    CHECK_FALSE(stage.action);CHECK(stage.visual->sequence==3U);
    CHECK(stage.actions_rejected==1U);CHECK(controller.drain_audio().count==0U);
    // Single retains the same clip. Double loses only one canonical shell,
    // so stale provisional reservations cannot be hidden by a full 2-round
    // canonical decrement. Both return to exactly one authoritative shell.
    const auto available=shotgun_state(1,0,3,0);controller.observe(available,.06);
    const double at=.03+(twice ? 1.5 : .75);
    controller.submit(command(2,wire::kReferenceGoldSrcButtonAttack,at),at);
    const auto next=controller.sample(at);REQUIRE(next.identity);
    CHECK(next.action==api::LocalWeaponAction::primary_fire);
    CHECK(next.actions_started==2U);CHECK(next.actions_rejected==1U);
    CHECK(next.identity->command_sequence==2U);CHECK(controller.drain_audio().count==1U);
    CHECK(available.weapon_slots[0].clip==1);CHECK(available.weapon_hud.reserve_ammo[2]==0U);
  }
}

TEST_CASE("E17 Shotgun timeout keeps a different unconfirmed shell reserved until its own deadline",
          "[e17][shotgun][weapon-presentation][timeout][eligibility]") {
  hl::LocalWeaponPresentationController controller;initial(controller,shotgun_state(2,0,1));
  controller.submit(command(1,wire::kReferenceGoldSrcButtonAttack,.02),.02);
  controller.submit(command(2,wire::kReferenceGoldSrcButtonAttack,.77),.77);
  const auto second=controller.sample(.77);REQUIRE(second.identity);
  CHECK(second.actions_started==2U);CHECK(second.identity->command_sequence==2U);
  (void)controller.drain_audio();
  const auto older_timeout=controller.sample(1.28);REQUIRE(older_timeout.identity);
  CHECK(older_timeout.actions_rejected==1U);CHECK(older_timeout.identity->command_sequence==2U);
  // Only command one's reservation expired. Releasing command two as well
  // would allow a false successful double against this unchanged clip=2.
  controller.submit(command(3,wire::kReferenceGoldSrcButtonAttack2,1.52),1.52);
  const auto blocked=controller.sample(1.52);
  CHECK(blocked.actions_started==2U);CHECK(blocked.secondary_fire_starts==0U);
  CHECK(controller.drain_audio().count==0U);
  const auto final_timeout=controller.sample(2.03);
  CHECK_FALSE(final_timeout.identity);CHECK(final_timeout.actions_rejected==2U);
  controller.submit(command(4,wire::kReferenceGoldSrcButtonAttack2,2.04),2.04);
  const auto fresh=controller.sample(2.04);REQUIRE(fresh.identity);
  CHECK(fresh.actions_started==3U);CHECK(fresh.secondary_fire_starts==1U);
  CHECK(fresh.identity->command_sequence==4U);CHECK(controller.drain_audio().count==1U);
}

TEST_CASE("E17 Shotgun DeathMsg and ScoreInfo remain separate server owned E14 transactions",
          "[e17][shotgun][e14][game-module][deathmsg][scoreinfo]") {
  api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
  const auto observation=shotgun_state(8,32,1);
  const std::array players{api::PlayerDisplayUpdate{1,true,true,"Shooter"},
      api::PlayerDisplayUpdate{2,true,true,"Victim"}};
  auto initial_record=host.stage_record({nullptr,observation,{},1U,2U,players,0.0});
  REQUIRE(initial_record);host.commit_record(std::move(*initial_record.state));
  host.bind_model(shotgun_model());host.observe(observation,0.0);
  host.submit(command(1,wire::kReferenceGoldSrcButtonAttack2,.02),.02);
  CHECK(host.sample(.02).secondary_fire_starts==1U);
  auto no_message=host.stage_record({&observation,observation,{},1U,2U,{},.02});
  REQUIRE(no_message);CHECK(no_message.state->multiplayer.players[0].frags==0);
  CHECK(no_message.state->multiplayer.players[1].deaths==0);
  CHECK(std::none_of(no_message.state->multiplayer.notices.begin(),
      no_message.state->multiplayer.notices.end(),[](const auto& notice){return notice.has_value();}));
  const auto life_before=no_message.state->lifecycle;
  host.commit_record(std::move(*no_message.state));

  const std::array death_body{std::byte{1},std::byte{2},std::byte{'s'},std::byte{'h'},
      std::byte{'o'},std::byte{'t'},std::byte{'g'},std::byte{'u'},std::byte{'n'},std::byte{0}};
  const client::RuntimeObservationSource death_source{2,2,2,0,80};
  const std::array death_messages{api::GameMessageView{api::GameMessageKind::user_message,
      "DeathMsg",death_body,death_source,std::uint8_t{93}}};
  auto killed=host.stage_record({&observation,observation,death_messages,1U,2U,{},.04});
  REQUIRE(killed);REQUIRE(killed.state->multiplayer.notices[0]);
  CHECK(killed.state->multiplayer.notices[0]->weapon=="shotgun");
  CHECK(killed.state->multiplayer.notices[0]->killer_name=="Shooter");
  CHECK(killed.state->multiplayer.notices[0]->victim_name=="Victim");
  CHECK(killed.state->multiplayer.players[0].frags==0);
  CHECK(killed.state->multiplayer.players[1].deaths==0);
  CHECK(killed.state->lifecycle.state==life_before.state);
  CHECK(killed.state->lifecycle.life_epoch==life_before.life_epoch);
  host.commit_record(std::move(*killed.state));
  const auto hud=host.hud(observation,.04);
  CHECK(std::any_of(hud.draw.texts.begin(),hud.draw.texts.end(),[](const auto& text) {
    return text.text.find("Shooter [shotgun] Victim")!=std::string::npos;
  }));
  auto duplicate=host.stage_record({&observation,observation,death_messages,1U,2U,{},.05});
  REQUIRE(duplicate);
  CHECK(std::count_if(duplicate.state->multiplayer.notices.begin(),
      duplicate.state->multiplayer.notices.end(),[](const auto& notice){return notice.has_value();})==1);
  host.commit_record(std::move(*duplicate.state));

  const std::array killer_score{std::byte{1},std::byte{1},std::byte{0},std::byte{0},
      std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
  const std::array victim_score{std::byte{2},std::byte{0},std::byte{0},std::byte{1},
      std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
  const std::array scores{
      api::GameMessageView{api::GameMessageKind::user_message,"ScoreInfo",killer_score,{3,3,3,0,72},std::uint8_t{94}},
      api::GameMessageView{api::GameMessageKind::user_message,"ScoreInfo",victim_score,{3,3,3,72,144},std::uint8_t{94}}};
  auto scored=host.stage_record({&observation,observation,scores,1U,2U,{},.06});
  REQUIRE(scored);CHECK(scored.state->multiplayer.players[0].frags==1);
  CHECK(scored.state->multiplayer.players[1].deaths==1);
  REQUIRE(scored.state->multiplayer.notices[0]);
  CHECK(scored.state->multiplayer.notices[0]->weapon=="shotgun");
  CHECK(observation.weapon_slots[0].clip==8);CHECK(observation.weapon_hud.reserve_ammo[2]==32U);
  host.commit_record(std::move(*scored.state));host.teardown();
}
