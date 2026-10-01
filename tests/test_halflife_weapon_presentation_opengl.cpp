#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::tests::entity_opengl_fixture;
namespace world_fixture = hlclient::tests::world_render_fixture;
namespace entity = hlclient::entity_render;
namespace renderer = hlclient::renderer;
namespace spatial = hlclient::world_spatial;
namespace visibility = hlclient::world_visibility;

TEST_CASE("Owned Studio weapon sequences change framebuffer and preserve A1 camera space",
          "[weapon-presentation][opengl][first-person][actual-context]") {
    namespace studio = hlclient::goldsrc::studio;
    namespace app = hlclient::games::halflife;
    namespace client = hlclient::client;
    auto parsed = studio::GoldSrcStudioParser::parse(
        {hlclient::tests::synthetic_weapon_presentation_studio(), {}, {}});
    INFO((parsed.error ? parsed.error->context : ""));
    REQUIRE(parsed);
    auto owned = std::make_shared<hlclient::assets::ModelAsset>(
        *hlclient::tests::entity_render_fixture::model_asset());
    owned->skeletal_data = std::make_shared<const hlclient::assets::SkeletalModelAssetData>(
        std::move(parsed.document->skeletal_model));
    auto assets = hlclient::tests::entity_render_fixture::render_assets(
        true, false, hlclient::assets::SpriteTextureFormat::normal, false,
        hlclient::assets::SpriteOrientation::view_parallel, owned);
    auto built = hlclient::tests::entity_render_fixture::scene_package(std::move(assets));
    REQUIRE(built);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(std::move(*built.package));
    auto context = fixture::try_context(640,480);
    if (!context) { SKIP("OpenGL 3.3 Core context unavailable"); }
    context->initialize_renderer();
    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*world_result.package));
    std::uint64_t frame_revision = 0U;
    const auto& model = *owned->skeletal_data;
    app::HalfLifePresentation hud_presentation;
    const auto render_visual = [&](const app::LocalWeaponPresentationSnapshot& snapshot, int clip, int reserve) {
        REQUIRE(snapshot.visual);
        studio::StudioPoseInput pose_input;
        pose_input.compatibility_profile = studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
        pose_input.sequence_index = snapshot.visual->sequence;
        pose_input.body_value = snapshot.visual->body;
        pose_input.frame_coordinate = snapshot.frame_coordinate;
        auto pose = studio::StudioPoseEvaluator{}.evaluate({"models/owned.mdl",{0x1234U,0x5678U}},model,pose_input);
        REQUIRE(pose);
        entity::EntityRenderFrameBuildInput input;
        input.resource_id = 0xE511U; input.resource_revision = ++frame_revision;
        input.interpolation = {0.0,0.0,0.0,0.0F,1U,1U,entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
        entity::StudioRenderPose rendered_pose{package->studio_assets()[0U]->source_identity(),{}};
        for (const auto& bone : pose.pose->world_bones()) {
            const auto& m = bone.transform.values;
            rendered_pose.bone_matrices.push_back({m[0],m[4],m[8],0,m[1],m[5],m[9],0,m[2],m[6],m[10],0,m[3],m[7],m[11],1});
        }
        input.studio_poses.push_back(std::move(rendered_pose));
        entity::StudioEntityRenderInstance instance;
        instance.entity_number = 1U; instance.pose_index = 0U; instance.studio_asset_index = 0U;
        instance.body_value = snapshot.visual->body;
        instance.interpolated_bounds = {package->studio_assets()[0U]->bounds().minimum,package->studio_assets()[0U]->bounds().maximum};
        input.studio_instances.push_back(instance);
        auto frame = entity::EntityRenderFrameBuilder{}.build(*package,std::move(input));
        REQUIRE(frame);
        renderer::RenderScene scene;
        scene.camera.position = {8,-12,6}; scene.camera.target = {8.5F,12,-2};
        scene.first_person_entities = renderer::RenderDynamicEntities{package,
            std::make_shared<const entity::EntityRenderFrame>(std::move(*frame.frame)),{}};
        scene.static_world = renderer::RenderStaticWorld{world,renderer::RenderCullMode::none,
            renderer::RenderBaselineLightStylePolicy::source_slot_zero};
        client::RuntimeClientObservationState hud_observation;
        hud_observation.generation = 1U;
        hud_observation.weapon_hud.health = std::uint8_t{100U};
        hud_observation.weapon_hud.armor = std::int16_t{13};
        hud_observation.weapon_hud.active_weapon_id = std::uint8_t{2U};
        hud_observation.weapon_hud.clips[2U] = static_cast<std::int16_t>(clip);
        hud_observation.weapon_hud.reserve_ammo[1U] = static_cast<std::uint8_t>(reserve);
        hud_observation.weapon_hud.catalogue.push_back(
            {.id=2U,.command_name="weapon_9mmhandgun",.primary_ammo_type=1});
        const auto hud = hud_presentation.hud(hud_observation,0.0);
        scene.basic_hud = hud.draw;
        context->renderer().render(scene,{640,480});
        CHECK(hud.clip == clip);
        CHECK(hud.primary_reserve == reserve);
        REQUIRE(context->renderer().observe_framebuffer({640,480},scene.clear_color));
        CHECK(glGetError() == GL_NO_ERROR);
        // Isolate only the camera-local pass for exact pixel and pitch proof.
        scene.static_world.reset(); scene.basic_hud.reset();
        context->renderer().render(scene,{640,480});
        auto baseline = context->renderer().observe_framebuffer({640,480},scene.clear_color);
        REQUIRE(baseline); CHECK(baseline.non_clear_pixel_count > 0U);
        for (const float pitch : {80.0F,-80.0F}) {
            constexpr float radians = 0.01745329252F;
            scene.camera.position = {123,-77,28};
            scene.camera.target = {123 + std::cos(pitch*radians),-77,28 + std::sin(pitch*radians)};
            context->renderer().render(scene,{640,480});
            auto turned = context->renderer().observe_framebuffer({640,480},scene.clear_color);
            REQUIRE(turned); CHECK(turned.color_signature == baseline.color_signature);
            CHECK(turned.minimum_x == baseline.minimum_x); CHECK(turned.maximum_y == baseline.maximum_y);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        return baseline.color_signature;
    };
    for (const std::uint8_t weapon : {std::uint8_t{2U},std::uint8_t{1U}}) {
        app::LocalWeaponModelMetadata metadata;
        metadata.generation = 1U; metadata.model_index = 7U; metadata.resource_revision = 1U;
        metadata.resource_name = weapon == 2U ? "models/v_9mmhandgun.mdl" : "models/v_crowbar.mdl";
        metadata.supported_bodies.fill(true);
        for (const auto& seq : model.sequences)
            metadata.sequences.push_back({seq.frames_per_second,seq.frame_count,(seq.source_flags & 1U)!=0U});
        client::RuntimeClientObservationState state;
        state.generation = 1U; state.publication_revision = 1U;
        state.client_metadata = {1U,client::RuntimeObservationFreshness::observed_in_record,
            client::RuntimeObservationCompleteness::complete_reconstruction,client::RuntimeObservationSource{1U,1U}};
        state.receiving_client.emplace(); state.receiving_client->viewmodel_index = 7U;
        state.weapon_hud.active_weapon_id = weapon;
        state.weapon_hud.catalogue.push_back({weapon,"owned",1});
        state.weapon_hud.reserve_ammo[1U] = std::uint8_t{68U};
        state.weapon_slots.push_back({.clip=17,.in_reload=false,.next_primary_attack=0.0,.weapon_id=weapon});
        app::LocalWeaponPresentationController controller;
        controller.bind_model(metadata); controller.observe(state,0.0);
        const auto idle_signature = render_visual(controller.sample(0.0),17,68);
        controller.submit({1U,1U,1U,0.02},0.02);
        (void)render_visual(controller.sample(0.02),17,68); // HUD never provisionally decrements
        const auto action_signature = render_visual(controller.sample(0.17),17,68);
        CHECK(action_signature != idle_signature);
        state.publication_revision = 2U; state.client_metadata.source = client::RuntimeObservationSource{2U,2U};
        if (weapon == 2U) state.weapon_slots[0U].clip = 16;
        else state.weapon_slots[0U].next_primary_attack = 0.5;
        controller.observe(state,0.18);
        CHECK(controller.sample(0.18).actions_confirmed == 1U);
        CHECK_FALSE(controller.sample(0.18).hit_status_available);
        CHECK(render_visual(controller.sample(0.55),16,68) == idle_signature);
        (void)render_visual(controller.sample(0.56),16,68);
        if (weapon == 2U) {
            controller.submit({1U,2U,std::uint16_t{1U<<13U},0.6},0.6);
            const auto reload_signature = render_visual(controller.sample(0.75),16,68);
            CHECK(reload_signature != idle_signature); CHECK(reload_signature != action_signature);
            state.publication_revision = 3U; state.client_metadata.source = client::RuntimeObservationSource{3U,3U};
            state.weapon_slots[0U].in_reload = true; controller.observe(state,0.8);
            state.publication_revision = 4U; state.client_metadata.source = client::RuntimeObservationSource{4U,4U};
            state.weapon_slots[0U].clip = 17; state.weapon_slots[0U].in_reload = false;
            state.weapon_hud.reserve_ammo[1U] = std::uint8_t{67U}; controller.observe(state,2.15);
            CHECK(render_visual(controller.sample(2.15),17,67) == idle_signature);
        }
    }
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count == 1U);
    context->release_renderer();
}
} // namespace
