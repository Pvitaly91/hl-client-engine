#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/remote_player.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace {
namespace c = hlclient::client;

c::RuntimePacketEntityObservation player_at(std::size_t ordinal, double x, double z,
                                            std::uint32_t effects = 0U)
{
    c::RuntimePacketEntityObservation player;
    player.entity_number = 2U;
    player.player_movement_schema = player.ordinary_visual_schema = true;
    player.origin = {x, 0.0, z};
    player.angles = {0.0, 0.0, 0.0};
    player.model_index = 7U;
    player.sequence = 2U;
    player.frame = effects ? 0.0 : 32.0;
    player.animation_time_seconds = 10.0 + .04 * static_cast<double>(ordinal - 1U);
    player.frame_rate = 1.0;
    player.gait_sequence = 1U;
    player.weapon_model_index = 21U;
    player.effects = effects;
    player.body = 0U;
    player.skin = 0;
    player.controllers.fill(127U);
    player.blending.fill(127U);
    return player;
}
} // namespace

namespace {
namespace api = hlclient::game_api;
namespace hl = hlclient::games::halflife;
namespace a = hlclient::assets;

a::SkeletalModelAssetData approved_model()
{
    a::SkeletalModelAssetData model;
    constexpr std::array names{"Bip01", "Bip01 Pelvis", "Bip01 Spine",
        "Bip01 Spine1", "Bip01 Spine2", "Bip01 Spine3"};
    for (std::size_t i = 0U; i < names.size(); ++i) {
        a::ModelBone bone;
        bone.name = names[i];
        bone.parent_index = i ? static_cast<std::int32_t>(i - 1U) : -1;
        if (i >= 2U) bone.controller_indices[5U] = static_cast<std::int32_t>(i - 2U);
        model.bones.push_back(bone);
    }
    for (std::int32_t i = 0; i < 4; ++i)
        model.bone_controllers.push_back({i + 2, 0x20U, -30.0F, 30.0F, 127, i, false});
    for (std::size_t i = 0U; i < 3U; ++i) {
        a::ModelSequence sequence;
        sequence.label = i == 0U ? "ref_aim_onehanded" :
            i == 1U ? "project_owned_walk" : "ref_shoot_onehanded";
        sequence.frame_count = 11U;
        sequence.frames_per_second = 10.0F;
        sequence.source_flags = i == 2U ? 0U : 1U;
        sequence.blend_count = 1U;
        sequence.blend_types = {0x8, 0};
        sequence.blend_start = {-45.0F, 0.0F};
        sequence.blend_end = {45.0F, 0.0F};
        sequence.motion_bone = 0;
        if (i == 1U) sequence.linear_movement.x = 120.0F;
        model.sequences.push_back(sequence);
    }
    model.skin_families.push_back({{0U}});
    REQUIRE(hl::HalfLifeRemotePlayerPresentation::compatible_model(model));
    return model;
}

api::RemotePlayerPresentationContext pose_context(
    const a::SkeletalModelAssetData& model,
    const c::RuntimePacketEntityObservation& rendered,
    const c::RuntimePacketEntityObservation& current,
    const c::RuntimePacketEntityObservation* previous,
    std::size_t ordinal, double current_time, double previous_time,
    double sample_time)
{
    return {1U, 1U, 1U, 27U, {19U, 31U}, model, rendered, current, previous,
        32U, 1U, ordinal, previous ? ordinal - 1U : 0U, current_time,
        previous_time, sample_time, false, ordinal, previous ? ordinal - 1U : 0U};
}
} // namespace

TEST_CASE("E32 Half Life approves only verified nonloop attack pulses with continuous player anchors",
          "[e32-nointerp-motion][e10][game][remote-player]")
{
    api::GameClientHost host{hl::make_half_life_client_module()};
    host.reset({1U, 1U, 1U});
    for (std::size_t mode = 0U; mode < 28U; ++mode) {
        CAPTURE(mode);
        auto model = approved_model();
        auto previous = player_at(1U, 0.0, 64.0);
        previous.sequence = 0U;
        auto current = player_at(2U, 12.8, 68.8, 34U);
        std::array rows{current};
        api::WorldEntityPresentationContext input{current, &previous, rows, 8U,
            &model, 10.0, 10.04};
        switch (mode) {
        case 0U: break;
        case 1U: previous.effects = 34U; previous.sequence = 2U; break;
        case 2U: model.sequences[2].label = "crouch_shoot_onehanded"; break;
        case 3U: input.approved_player_model = nullptr; break;
        case 4U: current.effects = 32U; break;
        case 5U: current.effects = 38U; break;
        case 6U: previous.effects.reset(); break;
        case 7U: previous.model_index = 8U; break;
        case 8U: previous.weapon_model_index = 22U; break;
        case 9U: current.weapon_model_index = 0U; break;
        case 10U: current.gait_sequence = 0U; break;
        case 11U: previous.gait_sequence = 0U; break;
        case 12U: current.gait_sequence = 99U; break;
        case 13U: model.sequences[2].source_flags = 1U; break;
        case 14U: model.sequences[2].label = "death_onehanded"; break;
        case 15U: model.sequences[2].label = "project_unknown"; break;
        case 16U: input.current_server_seconds.reset(); break;
        case 17U: input.previous_server_seconds = 10.04; break;
        case 18U: input.previous_server_seconds = 10.05; break;
        case 19U: input.previous_server_seconds = 9.0; break;
        case 20U: current.origin.x = 256.0; break;
        case 21U: current.player_movement_schema = false; break;
        case 22U: current.spectator = true; break;
        case 23U: model.bones[2].parent_index = 99; break;
        case 24U: current.origin.x = std::numeric_limits<double>::quiet_NaN(); break;
        case 25U: input.previous = nullptr; break;
        case 26U: current.effects = 0U; break;
        case 27U: previous.effects = 32U; break;
        }
        const auto result = host.world_entity(input);
        CHECK(result.animation_only_effect_mask == (mode < 3U ? 32U : 0U));
    }
}

TEST_CASE("E32 an approved firing animation restarts exactly while gait remains continuous through the pulse",
          "[e32-nointerp-motion][e10][game][remote-player]")
{
    const auto model = approved_model();
    api::GameClientHost host{hl::make_half_life_client_module()};
    host.reset({1U, 1U, 1U});
    auto first = player_at(1U, 0.0, 0.0);
    first.sequence = 0U; first.frame = 0.0;
    REQUIRE(host.remote_player(pose_context(model, first, first, nullptr,
        1U, 10.0, 10.0, 10.0)).status == api::RemotePlayerPresentationStatus::ready);
    auto run = first;
    run.origin.x = 4.8; run.animation_time_seconds = 10.04;
    const auto walking = host.remote_player(pose_context(model, run, run, &first,
        2U, 10.04, 10.0, 10.04));
    REQUIRE(walking.gait_sample);
    CHECK(walking.gait_sample->frame_coordinate == Catch::Approx(.44).margin(1e-7));
    auto shot = run;
    shot.origin.x = 9.6; shot.sequence = 2U; shot.frame = 0.0;
    shot.animation_time_seconds = 10.08; shot.effects = 34U;
    std::array current_rows{shot};
    const auto approved = host.world_entity({shot, &run, current_rows, 8U,
        &model, 10.04, 10.08});
    REQUIRE(approved.animation_only_effect_mask == 32U);
    auto rendered = run;
    rendered.origin.x = 7.2;
    auto input = pose_context(model, rendered, shot, &run, 3U, 10.08, 10.04, 10.06);
    input.current_animation_only_effect_mask = approved.animation_only_effect_mask;
    const auto onset = host.remote_player(input);
    REQUIRE(onset.status == api::RemotePlayerPresentationStatus::ready);
    REQUIRE(onset.gait_sample);
    CHECK(onset.gait_sample->frame_coordinate == Catch::Approx(.66).margin(1e-7));
    CHECK(onset.sample.sequence == 2U);
    CHECK(onset.sample.frame_coordinate == 0.0); // Raw main restart, not a blended old aim.
    CHECK_FALSE(onset.previous_sample);
    CHECK(onset.previous_weight == 0.0);
    CHECK(onset.transition_identity == 0U);
    CHECK(host.remote_player(input).gait_sample == onset.gait_sample);
    auto released = shot;
    released.origin.x = 14.4; released.effects = 0U;
    released.frame = 25.6;
    rendered = shot; rendered.origin.x = 12.0;
    auto falling = pose_context(model, rendered, released, &shot,
        4U, 10.12, 10.08, 10.10);
    falling.previous_animation_only_effect_mask = approved.animation_only_effect_mask;
    const auto release = host.remote_player(falling);
    REQUIRE(release.status == api::RemotePlayerPresentationStatus::ready);
    REQUIRE(release.gait_sample);
    CHECK(release.gait_sample->frame_coordinate == Catch::Approx(1.10).margin(1e-7));
    CHECK_FALSE(release.previous_sample);
    CHECK(release.transition_identity == 0U);
    CHECK(host.remote_player(falling).gait_sample == release.gait_sample);
    // No approval: the standalone bit retains the established strict pose guard.
    hl::HalfLifeRemotePlayerPresentation strict;
    REQUIRE(strict.sample(pose_context(model, first, first, nullptr,
        1U, 10.0, 10.0, 10.0)).status == api::RemotePlayerPresentationStatus::ready);
    REQUIRE(strict.sample(pose_context(model, run, run, &first,
        2U, 10.04, 10.0, 10.04)).gait_sample);
    shot.effects = 32U;
    auto blocked = pose_context(model, shot, shot, &run, 3U, 10.08, 10.04, 10.06);
    const auto legacy = strict.sample(blocked);
    REQUIRE(legacy.gait_sample);
    CHECK(legacy.gait_sample->frame_coordinate == 0.0);
    CHECK_FALSE(legacy.previous_sample);
}
