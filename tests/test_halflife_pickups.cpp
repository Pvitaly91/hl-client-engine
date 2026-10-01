#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <catch2/catch_test_macros.hpp>
#include "entity_render/entity_opengl_test_support.hpp"
#include <functional>
#include <limits>

namespace {
namespace api = hlclient::game_api;
namespace client = hlclient::client;
struct Message {
  std::string name;
  std::vector<std::byte> body;
};
Message bytes(std::string name, std::initializer_list<std::uint8_t> values) {
  Message out{std::move(name), {}};
  for (auto value : values) out.body.push_back(std::byte{value});
  return out;
}
Message item(std::string token) {
  Message out{"ItemPickup", {}};
  for (unsigned char c : token) out.body.push_back(std::byte{c});
  out.body.push_back(std::byte{0});
  return out;
}
Message catalogue(std::uint8_t id, std::string name, std::uint8_t slot = 1U) {
  auto out = item(std::move(name));
  out.name = "WeaponList";
  for (auto value : {1U, 250U, 255U, 255U, static_cast<unsigned>(slot), 0U,
                     static_cast<unsigned>(id), 0U})
    out.body.push_back(std::byte{static_cast<std::uint8_t>(value)});
  return out;
}
struct Harness {
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  client::RuntimeClientObservationState state;
  std::size_t ordinal{};
  std::string error;
  Harness() {
    host.reset({1U, 1U, 1U});
    state.generation = 1U;
    state.receiving_client.emplace();
    state.receiving_client->health = 100.0;
    state.receiving_client->origin = {0.0, 0.0, 0.0};
    state.receiving_client->view_offset.z = 28.0;
    state.receiving_client->dead_flag = 0U;
    state.receiving_client->owned_weapon_bits = 0U;
    state.receiving_client->viewmodel_index = 59U;
    state.receiving_client->punch_angle = {0.0, 0.0, 0.0};
    state.weapon_slots.push_back({.wire_slot=2U,.clip=8,.in_reload=false,
        .next_primary_attack=0.0,.weapon_id=2U});
  }
  bool publish(std::vector<Message> messages,
      std::function<void(client::RuntimeClientObservationState&)> edit = {}) {
    auto candidate = state;
    ++ordinal;
    candidate.publication_revision = ordinal;
    candidate.client_metadata = {1U,client::RuntimeObservationFreshness::observed_in_record,
        client::RuntimeObservationCompleteness::complete_reconstruction,
        client::RuntimeObservationSource{ordinal,ordinal,static_cast<std::uint32_t>(ordinal),0U,8U}};
    if (edit) edit(candidate);
    std::vector<api::GameMessageView> views;
    std::size_t cursor = 8U;
    for (const auto& message : messages) {
      const auto end = cursor + 8U + message.body.size() * 8U;
      views.push_back({api::GameMessageKind::user_message,message.name,message.body,
          {ordinal,ordinal,static_cast<std::uint32_t>(ordinal),cursor,end}});
      cursor = end;
    }
    auto result = host.stage_record({state.publication_revision ? &state : nullptr,
        candidate,views,1U});
    if (!result) { error = result.error.value_or("missing result"); return false; }
    candidate.weapon_hud = result.state->weapon_hud;
    candidate.lifecycle = result.state->lifecycle;
    candidate.life_events = result.state->life_events;
    host.commit_record(std::move(*result.state));
    state = std::move(candidate);
    return true;
  }
  api::HudState hud(double time = 1.0) { return host.hud(state,time); }
  void glock() {
    REQUIRE(publish({catalogue(2U,"weapon_9mmhandgun"),bytes("CurWeapon",{1U,2U,8U}),
        bytes("AmmoX",{1U,50U})},[](auto& s){s.receiving_client->owned_weapon_bits = 1U << 2U;}));
  }
};
api::LocalWeaponModelMetadata glock_model() {
  api::LocalWeaponModelMetadata m;
  m.generation=1U; m.model_index=59U; m.resource_revision=1U;
  m.resource_name="models/v_9mmhandgun.mdl";
  m.sequences.resize(10U,{30.0,46U,false});
  m.sequences[0U].looping=true;
  m.supported_bodies.fill(true); m.selectable_bodies.fill(true);
  return m;
}
}

TEST_CASE("Pickup notification and absolute ammo records remain independent without double addition",
          "[pickups][game-module][halflife]") {
  Harness h;
  h.glock();
  REQUIRE(h.publish({bytes("AmmoPickup",{1U,17U})}));
  auto hud = h.hud();
  CHECK(hud.primary_reserve == 50);
  CHECK(h.state.weapon_hud.reserve_ammo_sources[1U]->record_ordinal == 1U);
  CHECK(h.state.weapon_hud.clip_sources[2U]->record_ordinal == 1U);
  REQUIRE(hud.inventory_feedback.size() == 1U);
  CHECK(hud.inventory_feedback[0] == "PICKUP GLOCK AMMO +17");
  CHECK(hud.inventory_notifications_received == 1U);
  const auto revision = hud.inventory_feedback_revision;
  REQUIRE(h.publish({bytes("AmmoX",{1U,67U})}));
  CHECK(h.hud(1.1).primary_reserve == 67);
  CHECK(h.state.weapon_hud.reserve_ammo_sources[1U]->record_ordinal == 3U);
  CHECK(h.hud(1.1).inventory_notifications_received == 1U);
  CHECK(h.hud(1.1).inventory_feedback_revision == revision);
  for (int i = 0; i < 20; ++i) {
    h.host.observe(h.state,1.2);
    CHECK(h.hud(1.2).inventory_notifications_received == 1U);
    CHECK(h.hud(1.2).inventory_feedback_revision == revision);
  }
  REQUIRE(h.publish({},[](auto& s){
    s.client_metadata.freshness=client::RuntimeObservationFreshness::retained;
  }));
  CHECK(h.hud(1.3).primary_reserve == 67);
  REQUIRE(h.publish({bytes("AmmoPickup",{1U,0U})}));
  CHECK(h.hud(1.4).inventory_notifications_received == 1U);
  REQUIRE(h.publish({bytes("AmmoPickup",{1U,2U})}));
  CHECK(h.hud(1.5).inventory_notifications_received == 2U);
  CHECK(h.hud(1.5).primary_reserve == 67);
  CHECK(h.hud(6.51).inventory_feedback.empty());
  CHECK(h.hud(1.0).inventory_feedback.empty()); // a backward clock cannot revive
}

TEST_CASE("D2 Use has no local health armor or pickup effect and server absolute messages commit atomically",
          "[use][pickups][game-module][halflife]") {
  Harness h;
  REQUIRE(h.publish({bytes("Health",{61U}),bytes("Battery",{23U,0U})}));
  const auto before = client::runtime_observation_canonical_hash(h.state);
  h.host.observe_action_evidence(h.state,api::GameActionTraffic{});
  for (unsigned id=1; id<=5; ++id) h.host.submit({1U,id,32U,id*0.02},id*0.02);
  CHECK(client::runtime_observation_canonical_hash(h.state) == before);
  CHECK(h.hud().health == 61); CHECK(h.hud().armor == 23);
  CHECK(h.host.sample(0.1).actions_started == 0U);
  api::GameActionTraffic traffic; traffic.use_new_submission_count=5U;
  h.host.observe_action_evidence(h.state,traffic);
  auto window = h.host.action_evidence();
  CHECK(window.use_health_before == 61); CHECK(window.use_health_after == 61);
  CHECK(window.use_armor_before == 23); CHECK(window.use_armor_after == 23);
  REQUIRE(h.publish({bytes("Health",{66U}),bytes("Battery",{28U,0U})}));
  h.host.observe_action_evidence(h.state,traffic);
  CHECK(h.hud().health == 66); CHECK(h.hud().armor == 28);
  CHECK(h.host.action_evidence().use_health_after == 66);
  CHECK(h.host.action_evidence().use_armor_after == 28);
  CHECK(h.hud().draw.texts.size() == 1U); // no synthesized pickup feedback
  const auto accepted = client::runtime_observation_canonical_hash(h.state);
  CHECK_FALSE(h.publish({bytes("Health",{70U}),bytes("Battery",{1U})}));
  CHECK(client::runtime_observation_canonical_hash(h.state) == accepted);
  CHECK(h.hud().health == 66); CHECK(h.hud().armor == 28);
  h.host.observe_action_evidence(h.state,traffic);
  CHECK(h.host.action_evidence().use_health_after == 66);
}

TEST_CASE("Weapon notifications never manufacture ownership or equate models with IDs",
          "[pickups][game-module][halflife]") {
  Harness h;
  REQUIRE(h.publish({catalogue(2U,"weapon_9mmhandgun"),catalogue(1U,"weapon_crowbar",0U),
      bytes("WeapPickup",{2U})}));
  CHECK_FALSE(h.host.inventory_selection(h.state,2U));
  CHECK_FALSE(h.host.inventory_selection(h.state,59U));
  REQUIRE(h.publish({},[](auto& s){s.receiving_client->owned_weapon_bits = (1U<<2U)|(1U<<1U);}));
  REQUIRE(h.host.inventory_selection(h.state,2U));
  CHECK(h.host.inventory_selection(h.state,2U)->token == "weapon_9mmhandgun");
  CHECK(h.host.select_group(h.state,1U,{}) == 1U);
  CHECK(h.host.cycle_inventory(h.state,std::uint8_t{1U},1) == 2U);
  REQUIRE(h.publish({bytes("WeapPickup",{2U}),bytes("AmmoX",{1U,90U})}));
  CHECK(h.state.weapon_hud.catalogue.size() == 2U);
  CHECK(h.state.receiving_client->owned_weapon_bits == ((1U<<2U)|(1U<<1U)));
  CHECK(h.hud().inventory_notifications_received == 2U);
  REQUIRE(h.publish({catalogue(7U,"weapon_unknown"),bytes("WeapPickup",{7U})},
      [](auto& s){s.receiving_client->owned_weapon_bits = 1U<<7U;}));
  CHECK(h.hud().inventory_feedback.back() == "PICKUP WEAPON 7 (UNSUPPORTED)");
  REQUIRE(h.host.inventory_selection(h.state,7U)); // safe catalogue request, no firing profile
}

TEST_CASE("Health battery and missing fields are absolute server values, not guessed pickup gains",
          "[pickups][game-module][halflife]") {
  Harness h;
  REQUIRE(h.publish({bytes("Health",{75U}),bytes("Battery",{80U,0U})}));
  CHECK(h.hud().health == 75); CHECK(h.hud().armor == 80);
  CHECK(h.hud().inventory_feedback.empty());
  REQUIRE(h.publish({item("item_healthkit"),item("item_battery")}));
  CHECK(h.hud().health == 75); CHECK(h.hud().armor == 80);
  REQUIRE(h.hud().inventory_feedback.size() == 2U);
  CHECK(h.hud().inventory_feedback[0] == "PICKUP HEALTH KIT");
  CHECK(h.hud().inventory_feedback[1] == "PICKUP BATTERY");
  REQUIRE(h.publish({bytes("Health",{100U}),bytes("Battery",{100U,0U})}));
  CHECK(h.hud().health == 100); CHECK(h.hud().armor == 100);
  CHECK(h.hud().inventory_notifications_received == 2U);
  CHECK(h.state.weapon_hud.health_source->record_ordinal == h.ordinal);
  CHECK(h.state.weapon_hud.armor_source->record_ordinal == h.ordinal);
  REQUIRE(h.publish({bytes("Health",{100U}),bytes("Battery",{100U,0U})}));
  CHECK(h.hud().inventory_notifications_received == 2U); // full/refused touch has no event
  Harness missing;
  REQUIRE(missing.publish({item("item_battery")},[](auto& s){s.receiving_client->health.reset();}));
  CHECK_FALSE(missing.hud().health); CHECK_FALSE(missing.hud().armor);
}

TEST_CASE("Pickup staging owns text, ignores unknown messages, and commits no malformed suffix",
          "[pickups][game-module][halflife]") {
  Harness h;
  REQUIRE(h.publish({item("item_healthkit")}));
  CHECK(h.hud().inventory_feedback[0] == "PICKUP HEALTH KIT"); // RX storage is already destroyed
  for (auto invalid : {item("item_healthkit;quit"),item("../native_path"),
       bytes("ItemPickup",{0U}),bytes("ItemPickup",{'a',0U,'b',0U}),
       bytes("ItemPickup",{'a'}),bytes("AmmoPickup",{64U,1U}),
       bytes("WeapPickup",{0U}),bytes("WeapPickup",{32U})}) {
    const auto hash = client::runtime_observation_weapon_hud_hash(h.state);
    CHECK_FALSE(h.publish({bytes("Health",{12U}),item("item_battery"),invalid}));
    CHECK_FALSE(h.error.empty());
    CHECK(client::runtime_observation_weapon_hud_hash(h.state) == hash);
    CHECK(h.hud().inventory_notifications_received == 1U);
  }
  REQUIRE(h.publish({bytes("ProjectUnknown",{1U,2U}),item("item_project_owned")}));
  CHECK(h.hud().inventory_feedback.back() == "PICKUP ITEM item_project_owned (UNSUPPORTED)");
}

TEST_CASE("Pickup ring capacity clock and lifecycle do not restore previous-life ownership",
          "[pickups][game-module][halflife][damage-respawn]") {
  Harness h;
  h.glock();
  std::vector<Message> burst(64U,item("item_battery"));
  REQUIRE(h.publish(burst));
  CHECK(h.hud().inventory_feedback.size() == 4U);
  CHECK(h.hud().draw.texts.size() == 5U);
  CHECK(h.hud().inventory_notifications_received == 64U);
  burst.push_back(item("item_battery"));
  CHECK_FALSE(h.publish(burst));
  CHECK(h.hud().inventory_notifications_received == 64U);
  REQUIRE(h.publish({},[](auto& s){
    s.receiving_client->health=0.0; s.receiving_client->dead_flag=1U;
  }));
  CHECK(h.state.lifecycle.dead());
  CHECK(h.hud().inventory_feedback.empty());
  CHECK_FALSE(h.host.inventory_selection(h.state,2U));
  REQUIRE(h.publish({},[](auto& s){
    s.receiving_client->health=100.0; s.receiving_client->dead_flag=0U;
    s.receiving_client->owned_weapon_bits = 1U<<1U;
  }));
  CHECK(h.state.lifecycle.respawns == 1U);
  CHECK(h.hud().inventory_feedback.empty());
  CHECK_FALSE(h.host.inventory_selection(h.state,2U));
  REQUIRE(h.publish({item("item_healthkit")}));
  CHECK(h.hud().inventory_feedback.size() == 1U);
  h.host.teardown();
  CHECK(h.hud().inventory_feedback.empty());
  h.host.reset({2U,2U,1U});
  CHECK(h.hud().inventory_notifications_received == 0U);
}

TEST_CASE("Ammo pickups neither confirm reload nor cancel a pending fire animation through the host",
          "[pickups][game-module][halflife][weapon-presentation]") {
  for (bool reload : {false,true}) {
    Harness h;
    h.glock();
    h.host.bind_model(glock_model());
    h.host.observe(h.state,1.0);
    h.host.submit({1U,1U,reload ? hlclient::goldsrc::kReferenceGoldSrcButtonReload :
        hlclient::goldsrc::kReferenceGoldSrcButtonAttack,1.02},1.02);
    const auto action = h.host.sample(1.03);
    REQUIRE(action.identity); REQUIRE(action.visual);
    REQUIRE(h.publish({bytes("AmmoPickup",{1U,17U}),bytes("AmmoX",{1U,67U})}));
    h.host.observe(h.state,1.1);
    const auto after = h.host.sample(1.1);
    REQUIRE(after.identity); REQUIRE(after.visual);
    CHECK(after.actions_started == 1U); CHECK(after.actions_confirmed == 0U);
    CHECK(after.visual->restart_identity == action.visual->restart_identity);
    CHECK(after.identity->command_sequence == action.identity->command_sequence);
    CHECK(after.recoil_starts == action.recoil_starts);
  }
}

TEST_CASE("Committed pickup HUD renders through the actual neutral GL pass once and expires",
          "[pickups][game-module][halflife][opengl][hud]") {
  auto gl = hlclient::tests::entity_opengl_fixture::try_context(640,480);
  if (!gl) SKIP("Actual OpenGL context unavailable");
  gl->initialize_renderer();
  Harness h;
  h.glock();
  hlclient::renderer::RenderScene scene;
  scene.basic_hud = h.hud(1.0).draw;
  gl->renderer().render(scene,{640,480});
  const auto before = gl->renderer().observe_framebuffer({640,480},scene.clear_color);
  REQUIRE(before);
  REQUIRE(h.publish({bytes("AmmoPickup",{1U,17U}),item("item_battery")}));
  const auto hud = h.hud(1.1);
  CHECK(hud.inventory_feedback.size() == 2U);
  CHECK(hud.draw.rectangles.size() <= 8U); CHECK(hud.draw.texts.size() <= 8U);
  scene.basic_hud = hud.draw;
  gl->renderer().render(scene,{640,480});
  const auto shown = gl->renderer().observe_framebuffer({640,480},scene.clear_color);
  REQUIRE(shown); CHECK(shown.color_signature != before.color_signature);
  scene.basic_hud = h.hud(1.1).draw;
  gl->renderer().render(scene,{640,480});
  const auto repeat = gl->renderer().observe_framebuffer({640,480},scene.clear_color);
  REQUIRE(repeat); CHECK(repeat.color_signature == shown.color_signature);
  CHECK(h.hud(1.1).inventory_notifications_received == 2U);
  scene.basic_hud = h.hud(6.11).draw;
  gl->renderer().render(scene,{640,480});
  const auto expired = gl->renderer().observe_framebuffer({640,480},scene.clear_color);
  REQUIRE(expired); CHECK(expired.color_signature == before.color_signature);
  CHECK(glGetError() == GL_NO_ERROR);
  gl->release_renderer();
}
