#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {
namespace api = hlclient::game_api;
namespace client = hlclient::client;

client::RuntimeClientObservationState module_weapon_state(
    std::size_t ordinal, std::int32_t clip, std::uint8_t reserve = 68U) {
    client::RuntimeClientObservationState state;
    state.generation = 1U;
    state.publication_revision = ordinal;
    state.client_metadata = {1U, client::RuntimeObservationFreshness::observed_in_record,
        client::RuntimeObservationCompleteness::complete_reconstruction,
        client::RuntimeObservationSource{ordinal, ordinal, static_cast<std::uint32_t>(ordinal), 0U, 8U}};
    state.receiving_client.emplace();
    state.receiving_client->health = 100.0;
    state.receiving_client->owned_weapon_bits = 1U << 2U;
    state.receiving_client->viewmodel_index = 59U;
    state.receiving_client->punch_angle = {0.0, 0.0, 0.0};
    state.weapon_hud.active_weapon_id = std::uint8_t{2U};
    state.weapon_hud.health = std::uint8_t{77U};
    state.weapon_hud.armor = std::int16_t{72};
    state.weapon_hud.catalogue.push_back(
        {.id = 2U, .command_name = "weapon_9mmhandgun", .primary_ammo_type = 1,
         .source = *state.client_metadata.source});
    state.weapon_hud.clips[2U] = static_cast<std::int16_t>(clip);
    state.weapon_hud.reserve_ammo[1U] = reserve;
    state.weapon_hud.revision = ordinal;
    state.weapon_slots.push_back({.wire_slot = 2U, .clip = clip, .in_reload = false,
        .next_primary_attack = 0.0, .weapon_id = 2U});
    return state;
}
api::LocalWeaponModelMetadata module_weapon_model() {
    api::LocalWeaponModelMetadata model;
    model.generation = 1U;
    model.model_index = 59U;
    model.resource_revision = 1U;
    model.resource_name = "models/v_9mmhandgun.mdl";
    model.supported_bodies.fill(true);
    model.selectable_bodies.fill(true);
    model.sequences.resize(10U, {30.0, 16U, false});
    model.sequences[0U].looping = true;
    model.sequences[5U].frame_count = 46U;
    model.sequences[6U].frame_count = 46U;
    return model;
}
} // namespace

TEST_CASE("Half-Life factory routes unchanged input HUD and Glock presentation through GameClientHost",
          "[game-module][halflife][weapon-presentation]") {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1U, 1U, 1U});
    const auto& policy = host.movement_policy();
    REQUIRE(api::valid_game_movement_policy(policy));
    CHECK(policy.movement_speeds.forward_speed == 400.0F);
    CHECK(policy.movement_speeds.backward_speed == 400.0F);
    CHECK(policy.movement_speeds.side_speed == 400.0F);
    CHECK(policy.speed_key_multiplier == 0.3F);
    CHECK(policy.mouse_look.degrees_per_pixel_x == 0.10);
    CHECK(policy.mouse_look.degrees_per_pixel_y == 0.10);
    CHECK(policy.button_policy ==
        hlclient::goldsrc::GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use);
    CHECK(policy.bindings->actions_for_key(hlclient::input::PhysicalKey::w) ==
        hlclient::gameplay_input::gameplay_input_action_mask(
            hlclient::gameplay_input::GameplayInputAction::move_forward));

    auto state = module_weapon_state(1U, 17);
    const auto canonical_before = client::runtime_observation_canonical_hash(state);
    const auto model = module_weapon_model();
    host.bind_model(model);
    host.observe(state, 0.0);
    const auto initial = host.viewmodel(state, model, 0.0);
    CHECK(initial.status == api::ViewmodelStatus::ready);
    CHECK(initial.sequence == 0U);
    const auto hud = host.hud(state, 0.0);
    REQUIRE(hud.draw.texts.size() == 1U);
    REQUIRE(hud.draw.rectangles.size() == 1U);
    CHECK(hud.draw.texts[0].text == "HP 77  ARM 72\nweapon_9mmhandgun  CLIP 17  AMMO 68");
    CHECK(hud.draw.texts[0].x == 18.0F);
    CHECK(hud.draw.texts[0].y == 16.0F);
    CHECK(hud.draw.rectangles[0].width == 590.0F);
    CHECK(hud.draw.rectangles[0].height == 64.0F);
    const auto select = host.inventory_selection(state, 2U);
    REQUIRE(select);
    CHECK(select->kind == api::GameCommandKind::inventory_selection);
    CHECK(select->token == "weapon_9mmhandgun");
    CHECK_FALSE(host.inventory_selection(state, 3U));

    const api::LocalWeaponSubmittedCommand command{
        1U, 1U, hlclient::goldsrc::kReferenceGoldSrcButtonAttack, 0.02};
    host.submit(command, 0.02);
    const auto predicted = host.sample(0.02);
    REQUIRE(predicted.visual);
    CHECK(predicted.visual->sequence == 3U);
    CHECK(predicted.visual->body == 2U);
    CHECK(predicted.visual->restart_identity == 2U);
    CHECK(predicted.local_punch_pitch_degrees == Catch::Approx(-2.0));
    CHECK(predicted.status == api::LocalWeaponActionStatus::predicted_pending);
    host.submit(command, 0.03);
    host.observe(state, 0.03);
    const auto repeated = host.sample(0.12);
    CHECK(repeated.actions_started == 1U);
    CHECK(repeated.duplicate_submissions == 1U);
    REQUIRE(repeated.visual);
    CHECK(repeated.visual->restart_identity == predicted.visual->restart_identity);
    const auto pose = host.viewmodel(state, model, 0.12, repeated.visual);
    CHECK(pose.status == api::ViewmodelStatus::ready);
    CHECK(pose.sequence == 3U);
    CHECK(pose.body == 2U);
    CHECK(pose.frame_coordinate == Catch::Approx(3.0));
    CHECK(host.hud(state, 0.12).clip == 17);
    CHECK(client::runtime_observation_canonical_hash(state) == canonical_before);

    auto confirmed = module_weapon_state(2U, 16);
    host.observe(confirmed, 0.15);
    const auto accepted = host.sample(0.15);
    CHECK(accepted.actions_confirmed == 1U);
    CHECK(accepted.status == api::LocalWeaponActionStatus::server_confirmed);
    REQUIRE(accepted.visual);
    CHECK(accepted.visual->restart_identity == predicted.visual->restart_identity);
    CHECK(host.hud(confirmed, 0.15).clip == 16);
}

TEST_CASE("Half-Life record staging evolves its committed game state and publishes only on commit",
          "[game-module][halflife][weapon-hud]") {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1U,1U,1U});
    auto observation = module_weapon_state(1U,17);
    const std::array body{std::byte{74}};
    const std::array messages{api::GameMessageView{
        api::GameMessageKind::user_message, "Health", body,
        *observation.client_metadata.source}};
    auto first = host.stage_record({nullptr,observation,messages,1U});
    REQUIRE(first);
    CHECK(first.state->weapon_hud.health == 74U);
    host.commit_record(std::move(*first.state));
    // The previous protocol view is borrowed for receiving-client provenance,
    // but its exposed game projection does not replace the module's owner.
    observation.weapon_hud.health = std::uint8_t{99U};
    auto retained = host.stage_record({&observation,observation,{},1U});
    REQUIRE(retained);
    CHECK(retained.state->weapon_hud.health == 74U);
    const std::array changed_body{std::byte{44}};
    const std::array changed{api::GameMessageView{
        api::GameMessageKind::user_message, "Health", changed_body,
        *observation.client_metadata.source}};
    auto uncommitted = host.stage_record({&observation,observation,changed,1U});
    REQUIRE(uncommitted);
    CHECK(uncommitted.state->weapon_hud.health == 44U);
    auto still_owned = host.stage_record({&observation,observation,{},1U});
    REQUIRE(still_owned);
    CHECK(still_owned.state->weapon_hud.health == 74U);
    observation.generation = 2U;
    auto resetting = host.stage_record({nullptr,observation,{},1U});
    REQUIRE(resetting);
    CHECK_FALSE(resetting.state->weapon_hud.health);
    host.reset({2U,2U,1U});
    host.commit_record(std::move(*resetting.state));
    auto empty = host.stage_record({&observation,observation,{},1U});
    REQUIRE(empty);
    CHECK_FALSE(empty.state->weapon_hud.health);
}

TEST_CASE("Half-Life action evidence preserves source deduplication and server ammo authority",
          "[game-module][halflife][weapon-presentation]") {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1U, 1U, 1U});
    auto initial = module_weapon_state(1U, 17);
    api::GameActionTraffic traffic;
    traffic.generated_by_phase[0U] = 1U;
    host.observe_action_evidence(initial, traffic);
    auto shot = module_weapon_state(2U, 16);
    shot.receiving_client->punch_angle.x = -2.0;
    shot.weapon_hud.animation_source = shot.client_metadata.source;
    traffic.attack_new_submission_count = 1U;
    traffic.generated_by_phase[1U] = 1U;
    host.observe_action_evidence(shot, traffic);
    host.observe_action_evidence(shot, traffic);
    auto evidence = host.action_evidence();
    CHECK(evidence.server_confirmed_shots == 1U);
    CHECK(evidence.clip_before_fire == 17);
    CHECK(evidence.clip_after_fire == 16);
    CHECK(evidence.service_animation_count == 1U);
    CHECK(evidence.primary_animation_count == 1U);
    CHECK(evidence.server_punch_observations == 1U);
    CHECK(evidence.maximum_server_punch_degrees == 2.0);
    CHECK(evidence.last_animation_kind == 0U);

    auto loading = module_weapon_state(3U, 16);
    loading.weapon_slots[0].in_reload = true;
    loading.weapon_hud.animation_source = loading.client_metadata.source;
    traffic.reload_new_submission_count = 1U;
    traffic.generated_by_phase[2U] = 1U;
    host.observe_action_evidence(loading, traffic);
    auto loaded = module_weapon_state(4U, 17, 67U);
    host.observe_action_evidence(loaded, traffic);
    evidence = host.action_evidence();
    CHECK(evidence.server_confirmed_reload_starts == 1U);
    CHECK(evidence.server_confirmed_reload_completions == 1U);
    CHECK(evidence.clip_after_reload == 17);
    CHECK(evidence.reserve_before_reload == 68U);
    CHECK(evidence.reserve_after_reload == 67U);
    CHECK(evidence.reload_animation_count == 1U);

    auto melee = module_weapon_state(5U, -1, 0U);
    melee.weapon_hud.active_weapon_id = std::uint8_t{1U};
    melee.weapon_hud.catalogue = {{.id = 1U, .command_name = "weapon_crowbar"}};
    melee.weapon_hud.animation_source = melee.client_metadata.source;
    traffic.attack_new_submission_count = 2U;
    traffic.generated_by_phase[3U] = 1U;
    host.observe_action_evidence(melee, traffic);
    evidence = host.action_evidence();
    CHECK(evidence.melee_animation_count == 1U);
    CHECK(evidence.last_animation_kind == 2U);
    CHECK(evidence.last_animation_source == melee.client_metadata.source);
    CHECK(evidence.server_confirmed_shots == 1U);
    host.reset({1U, 2U, 1U});
    CHECK(host.action_evidence().server_confirmed_shots == 0U);
    CHECK_FALSE(host.action_evidence().last_animation_source);
}
