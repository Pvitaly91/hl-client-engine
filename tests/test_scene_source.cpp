#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>

#include "entity_render/entity_render_test_fixture.hpp"
#include "world_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {

namespace entity_fixture = hlclient::tests::entity_render_fixture;
namespace entity_render = hlclient::entity_render;
namespace world_fixture = hlclient::tests::world_render_fixture;

struct DynamicEntityFixture {
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package;
    std::shared_ptr<const entity_render::EntityRenderFrame> frame;
};

[[nodiscard]] DynamicEntityFixture make_dynamic_entity_fixture(
    const std::optional<entity_render::EntityRenderResourceIdentity>
        world_scene_association = std::nullopt)
{
    auto package_result =
        entity_fixture::scene_package(
            entity_fixture::render_assets(), {}, world_scene_association);
    REQUIRE(package_result);
    auto package =
        std::make_shared<const entity_render::EntitySceneRenderPackage>(
            std::move(*package_result.package));

    entity_render::EntityRenderFrameBuildInput input;
    input.resource_id = 0x9000U;
    input.resource_revision = 7U;
    input.interpolation = {
        0.5,
        0.0,
        1.0,
        0.5F,
        0x10U,
        0x11U,
        entity_render::EntityRenderInterpolationProfile::synthetic_seconds_v1,
    };
    const std::array<float, 16U> identity{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    input.studio_poses.push_back(
        {package->studio_assets()[0U]->source_identity(), {identity}});
    entity_render::StudioEntityRenderInstance studio;
    studio.entity_number = 1U;
    studio.pose_index = 0U;
    studio.studio_asset_index = 0U;
    studio.interpolated_bounds = {
        {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    input.studio_instances.push_back(studio);
    entity_render::SpriteEntityRenderInstance sprite;
    sprite.entity_number = 2U;
    sprite.sprite_asset_index = 0U;
    sprite.selected_frame_index = 0U;
    sprite.orientation = package->sprite_assets()[0U]->orientation();
    sprite.bounds = {{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};
    input.sprite_instances.push_back(sprite);

    auto frame_result = entity_render::EntityRenderFrameBuilder{}.build(
        *package, std::move(input));
    REQUIRE(frame_result);
    return {
        std::move(package),
        std::make_shared<const entity_render::EntityRenderFrame>(
            std::move(*frame_result.frame)),
    };
}

class FakeSceneSource final : public hlclient::client::IClientSceneSource {
public:
    [[nodiscard]] hlclient::client::SceneUpdateResult update(
        const hlclient::client::FrameTime elapsed) override
    {
        ++updates_;
        world_state_.advance(elapsed);
        return {};
    }

    [[nodiscard]] const hlclient::client::ClientWorldState& world_state() const noexcept override
    {
        return world_state_;
    }

    [[nodiscard]] std::size_t updates() const noexcept
    {
        return updates_;
    }

    void request_connection() noexcept
    {
        world_state_.set_connection_requested(true);
    }

private:
    hlclient::client::ClientWorldState world_state_;
    std::size_t updates_{0};
};

TEST_CASE("Client scene source updates state independently of render conversion", "[client][renderer]")
{
    FakeSceneSource scene_source;

    const auto update = scene_source.update(hlclient::client::FrameTime{0.25});
    REQUIRE(update);
    CHECK(scene_source.updates() == 1U);
    CHECK(scene_source.world_state().elapsed_seconds() == 0.25);

    const auto disconnected = hlclient::client::build_render_scene(scene_source.world_state());
    CHECK(disconnected.clear_color.blue == 0.085F);

    scene_source.request_connection();
    const auto connected = hlclient::client::build_render_scene(scene_source.world_state());
    CHECK(connected.clear_color.blue == 0.14F);
}

TEST_CASE("Client world publishes interactive camera metadata transactionally",
    "[client][camera][input]")
{
    using hlclient::client::ControlledEntityCameraStatus;
    using hlclient::client::InteractiveCameraMetadata;
    using hlclient::client::InteractiveCameraMode;

    hlclient::client::ClientWorldState state;
    const hlclient::client::RenderCameraState free_flight{
        {10.0F, 20.0F, 30.0F},
        {11.0F, 20.0F, 30.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        8'192.0F,
    };
    const InteractiveCameraMetadata metadata{
        7U,
        3U,
        InteractiveCameraMode::free_flight_world,
        std::nullopt,
        ControlledEntityCameraStatus::not_applicable,
    };
    REQUIRE(state.set_interactive_camera(free_flight, metadata));
    REQUIRE(state.interactive_camera_metadata());
    CHECK(state.input_revision() == 7U);
    CHECK(state.camera_revision() == 3U);

    const auto scene = hlclient::client::build_render_scene(state);
    CHECK(scene.camera.position.x == free_flight.position.x);
    CHECK(scene.camera.position.y == free_flight.position.y);
    CHECK(scene.camera.position.z == free_flight.position.z);
    CHECK(scene.camera.target.x == free_flight.target.x);
    CHECK(scene.camera.target.y == free_flight.target.y);
    CHECK(scene.camera.target.z == free_flight.target.z);
    CHECK(scene.camera.up.x == free_flight.up.x);
    CHECK(scene.camera.up.y == free_flight.up.y);
    CHECK(scene.camera.up.z == free_flight.up.z);

    auto invalid_metadata = metadata;
    invalid_metadata.input_revision = 6U;
    CHECK_FALSE(state.set_interactive_camera({}, invalid_metadata));
    CHECK(state.camera().position.x == free_flight.position.x);
    CHECK(state.camera().position.y == free_flight.position.y);
    CHECK(state.camera().position.z == free_flight.position.z);
    CHECK(state.input_revision() == 7U);

    state.set_camera(free_flight);
    CHECK_FALSE(state.interactive_camera_metadata());
    CHECK(state.input_revision() == 0U);
    CHECK(state.camera_revision() == 0U);

    REQUIRE(state.set_interactive_camera(free_flight, metadata));
    state.reset();
    CHECK_FALSE(state.interactive_camera_metadata());
}

TEST_CASE("Client world rejects unrenderable cameras and stale revisions transactionally",
    "[client][camera][input][validation][transaction]")
{
    using hlclient::client::ControlledEntityCameraStatus;
    using hlclient::client::InteractiveCameraMetadata;
    using hlclient::client::InteractiveCameraMode;

    auto entities = make_dynamic_entity_fixture();
    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));
    hlclient::client::ClientWorldState state;
    state.set_static_world(world);
    REQUIRE(state.set_dynamic_entities(entities.package, entities.frame));

    const hlclient::client::RenderCameraState camera{
        {10.0F, 20.0F, 30.0F},
        {11.0F, 20.0F, 30.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        8'192.0F,
    };
    const InteractiveCameraMetadata first{
        7U,
        3U,
        InteractiveCameraMode::free_flight_world,
        std::nullopt,
        ControlledEntityCameraStatus::not_applicable,
    };
    REQUIRE(state.set_interactive_camera(camera, first));
    const auto retained_world = state.static_world();
    const auto retained_entities = state.entity_scene();
    const auto retained_frame = state.entity_frame();
    const auto retained_world_revision = state.world_revision();
    const auto retained_entity_revision = state.entity_frame_revision();

    const auto rejected_without_mutation = [&](const auto& invalid_camera,
                                               const auto& metadata) {
        CHECK_FALSE(state.set_interactive_camera(invalid_camera, metadata));
        CHECK(state.camera() == camera);
        CHECK(state.input_revision() == 7U);
        CHECK(state.camera_revision() == 3U);
        CHECK(state.static_world() == retained_world);
        CHECK(state.entity_scene() == retained_entities);
        CHECK(state.entity_frame() == retained_frame);
        CHECK(state.world_revision() == retained_world_revision);
        CHECK(state.entity_frame_revision() == retained_entity_revision);
    };

    auto invalid_camera = camera;
    invalid_camera.target = invalid_camera.position;
    rejected_without_mutation(invalid_camera, InteractiveCameraMetadata{8U, 4U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable});
    invalid_camera = camera;
    invalid_camera.up = {};
    rejected_without_mutation(invalid_camera, InteractiveCameraMetadata{8U, 4U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable});
    invalid_camera = camera;
    invalid_camera.up = {1.0F, 0.0F, 0.0F};
    rejected_without_mutation(invalid_camera, InteractiveCameraMetadata{8U, 4U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable});
    invalid_camera = camera;
    invalid_camera.vertical_field_of_view_radians = 3.2F;
    rejected_without_mutation(invalid_camera, InteractiveCameraMetadata{8U, 4U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable});
    rejected_without_mutation(camera, InteractiveCameraMetadata{8U, 4U,
        InteractiveCameraMode::entity_first_person, 0U,
        ControlledEntityCameraStatus::anchored});
    rejected_without_mutation(camera, first);

    REQUIRE(state.set_interactive_camera(camera, {8U, 3U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable}));
    CHECK(state.input_revision() == 8U);
    CHECK(state.camera_revision() == 3U);

    auto moved_camera = camera;
    moved_camera.position.x += 1.0F;
    moved_camera.target.x += 1.0F;
    CHECK_FALSE(state.set_interactive_camera(moved_camera, {9U, 3U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable}));
    REQUIRE(state.set_interactive_camera(moved_camera, {9U, 4U,
        InteractiveCameraMode::free_flight_world, std::nullopt,
        ControlledEntityCameraStatus::not_applicable}));
}

TEST_CASE("Null renderer records compact scene metadata and lifecycle statistics", "[renderer][null]")
{
    hlclient::renderer::null::NullRenderer renderer;
    CHECK_FALSE(renderer.statistics().initialized);
    CHECK_FALSE(renderer.statistics().shutdown);
    CHECK(renderer.statistics().rendered_frames == 0U);
    CHECK_FALSE(renderer.statistics().static_world_present);
    CHECK_FALSE(renderer.statistics().package_revision.has_value());
    CHECK_FALSE(renderer.statistics().scene_package_present);
    CHECK_FALSE(renderer.statistics().visibility_present);
    CHECK_FALSE(renderer.statistics().visible_draw_list_present);
    CHECK_FALSE(renderer.statistics().scene_revision.has_value());
    CHECK_FALSE(renderer.statistics().visibility_revision.has_value());
    CHECK(renderer.statistics().visible_world_surface_count == 0U);
    CHECK(renderer.statistics().visible_brush_instance_count == 0U);
    CHECK(renderer.statistics().visible_draw_command_count == 0U);

    CHECK_THROWS_AS(renderer.render({}, {}), std::logic_error);
    renderer.initialize();
    CHECK(renderer.statistics().initialized);
    CHECK_FALSE(renderer.statistics().shutdown);

    hlclient::renderer::RenderScene partial_entities;
    partial_entities.dynamic_entities.emplace();
    CHECK_THROWS_AS(
        renderer.render(partial_entities, {640, 480}),
        std::invalid_argument);

    {
        hlclient::renderer::RenderScene scene;
        scene.clear_color.red = 0.75F;
        renderer.render(scene, hlclient::renderer::RenderExtent{640, 480});
        scene.clear_color.red = 0.0F;
    }

    CHECK(renderer.statistics().rendered_frames == 1U);
    CHECK(renderer.statistics().last_clear_color.red == 0.75F);
    CHECK_FALSE(renderer.statistics().static_world_present);
    CHECK_FALSE(renderer.statistics().scene_package_present);
    CHECK_FALSE(renderer.statistics().visibility_present);
    CHECK_FALSE(renderer.statistics().visible_draw_list_present);
    CHECK(renderer.statistics().visible_draw_command_count == 0U);
    CHECK(renderer.statistics().camera_valid);
    CHECK(renderer.statistics().last_extent.width == 640);
    CHECK(renderer.statistics().last_extent.height == 480);
    CHECK(renderer.information().device == "Null Renderer");

    const auto submitted_statistics = renderer.statistics();
    renderer.shutdown();
    renderer.shutdown();
    CHECK_FALSE(renderer.statistics().initialized);
    CHECK(renderer.statistics().shutdown);
    CHECK(submitted_statistics.rendered_frames == 1U);
    CHECK_FALSE(submitted_statistics.static_world_present);
    CHECK_THROWS_AS(renderer.render({}, {}), std::logic_error);
}

TEST_CASE("Null renderer accepts both interactive camera modes without input ownership",
    "[renderer][null][camera][input]")
{
    using hlclient::client::ControlledEntityCameraStatus;
    using hlclient::client::InteractiveCameraMetadata;
    using hlclient::client::InteractiveCameraMode;

    const hlclient::client::RenderCameraState free_flight{
        {4.0F, 5.0F, 6.0F},
        {5.0F, 5.0F, 6.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        4'096.0F,
    };
    const hlclient::client::RenderCameraState entity_first_person{
        {32.0F, 48.0F, 60.0F},
        {32.0F, 49.0F, 60.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        4'096.0F,
    };

    hlclient::client::ClientWorldState state;
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();

    REQUIRE(state.set_interactive_camera(
        free_flight,
        InteractiveCameraMetadata{
            1U,
            1U,
            InteractiveCameraMode::free_flight_world,
            std::nullopt,
            ControlledEntityCameraStatus::not_applicable,
        }));
    renderer.render(hlclient::client::build_render_scene(state), {640, 480});

    REQUIRE(state.set_interactive_camera(
        entity_first_person,
        InteractiveCameraMetadata{
            2U,
            2U,
            InteractiveCameraMode::entity_first_person,
            1U,
            ControlledEntityCameraStatus::anchored,
        }));
    renderer.render(hlclient::client::build_render_scene(state), {640, 480});

    REQUIRE(state.interactive_camera_metadata());
    CHECK(state.interactive_camera_metadata()->mode ==
        InteractiveCameraMode::entity_first_person);
    CHECK(state.interactive_camera_metadata()->controlled_entity == 1U);
    const auto statistics = renderer.statistics();
    CHECK(statistics.rendered_frames == 2U);
    CHECK(statistics.camera_valid);
    REQUIRE(statistics.last_camera);
    CHECK(statistics.last_camera->position.x == entity_first_person.position.x);
    CHECK(statistics.last_camera->position.y == entity_first_person.position.y);
    CHECK(statistics.last_camera->position.z == entity_first_person.position.z);
    CHECK(statistics.last_camera->target.x == entity_first_person.target.x);
    CHECK(statistics.last_camera->target.y == entity_first_person.target.y);
    CHECK(statistics.last_camera->target.z == entity_first_person.target.z);
    CHECK(statistics.last_camera->up.x == entity_first_person.up.x);
    CHECK(statistics.last_camera->up.y == entity_first_person.up.y);
    CHECK(statistics.last_camera->up.z == entity_first_person.up.z);
    CHECK(statistics.last_camera->vertical_field_of_view_radians ==
        entity_first_person.vertical_field_of_view_radians);
    CHECK(statistics.last_camera->near_plane == entity_first_person.near_plane);
    CHECK(statistics.last_camera->far_plane == entity_first_person.far_plane);
    CHECK((statistics.last_extent ==
        hlclient::renderer::RenderExtent{640, 480}));
    CHECK_FALSE(statistics.static_world_present);
    CHECK_FALSE(statistics.dynamic_entity_package_present);
}

TEST_CASE("Client and Null renderer carry protocol-neutral dynamic entities",
    "[client][renderer][entity-render][null]")
{
    auto entities = make_dynamic_entity_fixture();
    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));

    hlclient::client::ClientWorldState state;
    state.set_static_world(world);
    REQUIRE(state.set_dynamic_entities(entities.package, entities.frame));
    CHECK(state.static_world() == world);
    CHECK(state.entity_scene_revision() ==
        entities.package->resource_revision());
    CHECK(state.entity_frame_revision() == entities.frame->resource_revision());
    CHECK((entities.frame->scene_package_identity() ==
        entity_render::EntityRenderResourceIdentity{
            entities.package->resource_id(),
            entities.package->resource_revision()}));

    const auto retained_world_revision = state.world_revision();
    const auto retained_entity_scene_revision = state.entity_scene_revision();
    const auto retained_entity_frame_revision = state.entity_frame_revision();
    const auto retained_world = state.static_world();
    const auto retained_entity_package = state.entity_scene();
    const auto retained_entity_frame = state.entity_frame();
    const hlclient::client::RenderCameraState interactive_camera{
        {12.0F, 24.0F, 36.0F},
        {13.0F, 24.0F, 36.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        4'096.0F,
    };
    const hlclient::client::InteractiveCameraMetadata interactive_metadata{
        1U,
        1U,
        hlclient::client::InteractiveCameraMode::free_flight_world,
        std::nullopt,
        hlclient::client::ControlledEntityCameraStatus::not_applicable,
    };
    REQUIRE(state.set_interactive_camera(
        interactive_camera, interactive_metadata));
    CHECK(state.world_revision() == retained_world_revision);
    CHECK(state.entity_scene_revision() == retained_entity_scene_revision);
    CHECK(state.entity_frame_revision() == retained_entity_frame_revision);
    CHECK(state.static_world() == retained_world);
    CHECK(state.entity_scene() == retained_entity_package);
    CHECK(state.entity_frame() == retained_entity_frame);

    auto other_package_result = entity_fixture::scene_package(
        entity_fixture::render_assets(true, false));
    REQUIRE(other_package_result);
    auto other_package =
        std::make_shared<const entity_render::EntitySceneRenderPackage>(
            std::move(*other_package_result.package));
    REQUIRE(other_package->resource_revision() !=
        entities.package->resource_revision());
    CHECK_FALSE(state.set_dynamic_entities(other_package, entities.frame));
    CHECK(state.entity_scene() == entities.package);
    CHECK(state.entity_frame() == entities.frame);

    auto associated = make_dynamic_entity_fixture(
        entity_render::EntityRenderResourceIdentity{0x7700U, 1U});
    hlclient::client::ClientWorldState unassociated_world;
    CHECK_FALSE(unassociated_world.set_dynamic_entities(
        associated.package, associated.frame));
    CHECK_FALSE(unassociated_world.entity_scene());

    const auto scene = hlclient::client::build_render_scene(state);
    REQUIRE(scene.static_world);
    REQUIRE(scene.dynamic_entities);
    CHECK(scene.static_world->package == world);
    CHECK(scene.dynamic_entities->package == entities.package);
    CHECK(scene.dynamic_entities->frame == entities.frame);
    CHECK(scene.dynamic_entities->visibility_summary.visible_count == 2U);
    CHECK(scene.dynamic_entities->visibility_summary.studio_instance_count ==
        1U);
    CHECK(scene.dynamic_entities->visibility_summary.sprite_instance_count ==
        1U);

    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    renderer.render(scene, {640, 480});
    const auto statistics = renderer.statistics();
    CHECK(statistics.static_world_present);
    CHECK(statistics.dynamic_entity_package_present);
    CHECK(statistics.entity_scene_resource_id ==
        entities.package->resource_id());
    CHECK(statistics.entity_scene_revision ==
        entities.package->resource_revision());
    CHECK(statistics.entity_frame_revision ==
        entities.frame->resource_revision());
    CHECK(statistics.studio_entity_instance_count == 1U);
    CHECK(statistics.sprite_entity_instance_count == 1U);
    CHECK(statistics.visible_entity_count == 2U);

    auto mismatched_scene = scene;
    mismatched_scene.dynamic_entities->package = std::move(other_package);
    CHECK_THROWS_AS(
        renderer.render(mismatched_scene, {640, 480}),
        std::invalid_argument);

    const auto world_revision = state.world_revision();
    state.clear_dynamic_entities();
    CHECK(state.world_revision() == world_revision);
    CHECK(state.static_world() == world);
    CHECK_FALSE(state.entity_scene());
    CHECK_FALSE(state.entity_frame());
    CHECK(state.entity_scene_revision() == 0U);
    CHECK(state.entity_frame_revision() == 0U);
}

} // namespace
