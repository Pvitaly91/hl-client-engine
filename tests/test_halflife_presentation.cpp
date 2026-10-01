#include <hlclient/games/halflife/presentation.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
namespace game = hlclient::games::halflife;
namespace api = hlclient::game_api;
namespace client = hlclient::client;

client::RuntimeClientObservationState visible_state() {
  client::RuntimeClientObservationState state;
  state.generation = 4U; state.publication_revision = 7U;
  state.client_metadata.source = client::RuntimeObservationSource{7U,7U,31U,0U,8U};
  state.receiving_client.emplace();
  state.receiving_client->viewmodel_index = 12U;
  state.receiving_client->health = 95.0;
  state.weapon_hud.health = 87U; state.weapon_hud.armor = 13;
  state.weapon_hud.active_weapon_id = 2U;
  state.weapon_hud.clips[2U] = 17;
  state.weapon_hud.reserve_ammo[1U] = 68U;
  state.weapon_hud.catalogue.push_back(
      {.id=2U,.command_name="weapon_9mmhandgun",.primary_ammo_type=1});
  return state;
}
api::LocalWeaponModelMetadata model() {
  api::LocalWeaponModelMetadata result;
  result.generation = 4U; result.model_index = 12U;
  result.resource_revision = 3U; result.resource_name = "models/v_9mmhandgun.mdl";
  result.sequences.resize(10U,{10.0,11U,false});
  result.supported_bodies.fill(true);
  result.selectable_bodies.fill(true);
  return result;
}
}

TEST_CASE("Half-Life HUD composes identical server values and owning neutral primitives",
          "[game-module][halflife][hud][weapon-presentation]") {
  game::HalfLifePresentation presentation;
  auto state = visible_state();
  const auto hud = presentation.hud(state,1.0);
  CHECK(hud.health == 87); CHECK(hud.armor == 13);
  CHECK(hud.clip == 17); CHECK(hud.primary_reserve == 68);
  REQUIRE(hud.draw.texts.size() == 1U);
  CHECK(hud.draw.texts[0].text == "HP 87  ARM 13\nweapon_9mmhandgun  CLIP 17  AMMO 68");
  CHECK(hud.draw.texts[0].x == 18.0F); CHECK(hud.draw.texts[0].y == 16.0F);
  CHECK(hud.draw.texts[0].pixel_size == 2.0F);
  CHECK(hud.draw.texts[0].advance == 12.0F); CHECK(hud.draw.texts[0].line_height == 20.0F);
  REQUIRE(hud.draw.rectangles.size() == 1U);
  CHECK(hud.draw.rectangles[0].width == 590.0F); CHECK(hud.draw.rectangles[0].height == 64.0F);
  state.weapon_hud.catalogue[0].command_name = "changed";
  CHECK(hud.draw.texts[0].text == "HP 87  ARM 13\nweapon_9mmhandgun  CLIP 17  AMMO 68");
  state.weapon_hud.health.reset();
  CHECK(presentation.hud(state,1.1).health == 95);
  state.receiving_client->health.reset(); state.weapon_hud.armor.reset();
  state.weapon_hud.active_weapon_id.reset();
  const auto absent = presentation.hud(state,1.2);
  CHECK_FALSE(absent.health); CHECK_FALSE(absent.clip);
  CHECK(absent.draw.texts[0].text == "HP ?  ARM ?\nWEAPON ?  CLIP ?  AMMO ?");
}

TEST_CASE("Half-Life HUD feedback advances once per committed revision and clears at new life",
          "[game-module][halflife][hud][damage-respawn]") {
  game::HalfLifePresentation presentation;
  auto state = visible_state();
  state.lifecycle.life_epoch = 1U; state.lifecycle.feedback_revision = 1U;
  CHECK(presentation.hud(state,2.0).lifecycle_indicator == "DAMAGE");
  CHECK(presentation.hud(state,2.5).lifecycle_indicator == "DAMAGE");
  CHECK(presentation.hud(state,2.61).lifecycle_indicator.empty());
  state.lifecycle.state = client::LocalPlayerLifeState::dead;
  CHECK(presentation.hud(state,3.0).lifecycle_indicator == "DEAD - RELEASE THEN SPACE OR LMB");
  state.lifecycle.state = client::LocalPlayerLifeState::alive;
  state.lifecycle.life_epoch = 2U; state.lifecycle.respawns = 1U;
  CHECK(presentation.hud(state,3.1).lifecycle_indicator.empty());
  state.weapon_hud.hide_flags = 12U;
  const auto hidden = presentation.hud(state,3.2);
  CHECK_FALSE(hidden.health); CHECK_FALSE(hidden.armor); CHECK_FALSE(hidden.clip);
  presentation.reset();
  state = visible_state();
  CHECK(presentation.hud(state,0.0).health == 87);
}

TEST_CASE("Half-Life viewmodel selects sequence and restart timeline before neutral materialization",
          "[game-module][halflife][first-person][weapon-presentation]") {
  game::HalfLifePresentation presentation;
  auto state = visible_state();
  auto metadata = model();
  auto initial = presentation.viewmodel(state,metadata,1.0);
  REQUIRE(initial.status == api::ViewmodelStatus::ready);
  CHECK(initial.model_index == 12U); CHECK(initial.generation == 4U);
  CHECK(initial.publication_revision == 7U); CHECK(initial.sequence == 0U);
  CHECK(initial.frame_coordinate == 0.0);
  const api::LocalWeaponVisual action{3U,2U,1U,1.05};
  const auto started = presentation.viewmodel(state,metadata,1.1,action);
  CHECK(started.sequence == 3U); CHECK(started.body == 2U);
  CHECK(started.frame_coordinate == Catch::Approx(0.5));
  CHECK(presentation.viewmodel(state,metadata,1.1,action) == started);
  CHECK(presentation.viewmodel(state,metadata,1.2,action).frame_coordinate == Catch::Approx(1.5));
  state.weapon_hud.animation_source = client::RuntimeObservationSource{8U,8U,32U,0U,8U};
  state.weapon_hud.animation_sequence = 255U; state.weapon_hud.animation_body = 0U;
  const auto invalid = presentation.viewmodel(state,metadata,1.3);
  CHECK(invalid.sequence == 3U); CHECK(invalid.body == 2U);
  // Changing from a local action to a service context follows the preserved
  // existing restart rule; invalid sequence never replaces the valid pose.
  state.receiving_client->viewmodel_index = 13U;
  metadata.model_index = 13U;
  CHECK(presentation.viewmodel(state,metadata,1.4).status == api::ViewmodelStatus::hidden);
  state.weapon_hud.active_weapon_id = 1U;
  metadata.resource_name = "models/v_crowbar.mdl";
  state.weapon_hud.animation_source.reset(); state.weapon_hud.animation_sequence.reset();
  CHECK(presentation.viewmodel(state,metadata,1.5).status == api::ViewmodelStatus::ready);
  state.lifecycle.state = client::LocalPlayerLifeState::dead; state.lifecycle.deaths = 1U;
  CHECK(presentation.viewmodel(state,metadata,1.6).status == api::ViewmodelStatus::hidden);
  presentation.reset(); state = visible_state(); metadata = model();
  CHECK(presentation.viewmodel(state,metadata,0.0).frame_coordinate == 0.0);
}
