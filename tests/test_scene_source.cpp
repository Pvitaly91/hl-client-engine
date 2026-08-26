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
