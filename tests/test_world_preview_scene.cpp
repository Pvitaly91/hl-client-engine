#include "world_render_test_fixture.hpp"

#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

namespace client = hlclient::client;
namespace fixture = hlclient::tests::world_render_fixture;
namespace renderer = hlclient::renderer;
namespace world_preview = hlclient::world_preview;
namespace world_render = hlclient::world_render;

template <typename Type>
concept HasGoldSrcOrNetworkState = requires(Type value) {
    value.bsp;
    value.netchan;
    value.resource_list;
};

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage> make_shared_package()
{
    auto built = fixture::make_package();
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to create synthetic render package"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

TEST_CASE("World preview assigns an immutable package and bounds-derived static camera",
          "[world-preview][scene]")
{
    STATIC_REQUIRE_FALSE(HasGoldSrcOrNetworkState<renderer::RenderScene>);
    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.minimum_radius = 1.0F;
    options.camera_mode = world_preview::WorldPreviewCameraMode::static_camera;
    world_preview::WorldPreviewSceneSource source{package, options};

    CHECK(source.world_state().static_world() == package);
    CHECK(source.world_state().world_revision() == package->resource_revision());
    CHECK(source.world_center().x == 8.0F);
    CHECK(source.world_center().y == 8.0F);
    CHECK(source.world_center().z == 0.0F);
    CHECK(source.world_radius() == Catch::Approx(std::sqrt(128.0F)));
    CHECK_FALSE(source.world_state().connection_requested());

    const auto initial = source.world_state().camera();
    REQUIRE(source.update(std::chrono::duration<double>{20.0}));
    const auto after = source.world_state().camera();
    CHECK(after.position.x == initial.position.x);
    CHECK(after.position.y == initial.position.y);
    CHECK(after.position.z == initial.position.z);
    CHECK(after.target.x == source.world_center().x);
    CHECK(after.target.y == source.world_center().y);
    CHECK(after.target.z == source.world_center().z);
    CHECK_FALSE(source.world_state().connection_requested());
}

TEST_CASE("World preview orbit is deterministic, bounded and input-independent",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.camera_mode = world_preview::WorldPreviewCameraMode::orbit;
    options.orbit_angular_velocity_radians_per_second = 0.25F;
    world_preview::WorldPreviewSceneSource first{package, options};
    world_preview::WorldPreviewSceneSource second{package, options};
    const auto initial = first.world_state().camera().position;

    REQUIRE(first.update(std::chrono::duration<double>{2.0}));
    REQUIRE(second.update(std::chrono::duration<double>{2.0}));
    const auto first_camera = first.world_state().camera();
    const auto second_camera = second.world_state().camera();
    CHECK(first_camera.position.x == Catch::Approx(second_camera.position.x));
    CHECK(first_camera.position.y == Catch::Approx(second_camera.position.y));
    CHECK(first_camera.position.z == Catch::Approx(second_camera.position.z));
    CHECK(first_camera.position.x != initial.x);
    CHECK(first_camera.position.y != initial.y);
    CHECK(first_camera.position.z == Catch::Approx(initial.z));
    CHECK(first_camera.target.x == second_camera.target.x);
    CHECK(first.world_state().elapsed_seconds() == 2.0);
    CHECK_FALSE(first.world_state().connection_requested());
}

TEST_CASE("Client world state converts exactly to renderer-neutral static world scene",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.cull_mode = client::PreviewWorldCullMode::back;
    world_preview::WorldPreviewSceneSource source{package, options};
    const auto scene = client::build_render_scene(source.world_state());

    CHECK(scene.clear_color.red == 0.035F);
    CHECK(scene.clear_color.green == 0.055F);
    CHECK(scene.clear_color.blue == 0.085F);
    CHECK(scene.camera.position.x == source.world_state().camera().position.x);
    CHECK(scene.camera.position.y == source.world_state().camera().position.y);
    CHECK(scene.camera.position.z == source.world_state().camera().position.z);
    REQUIRE(scene.static_world);
    CHECK(scene.static_world->package == package);
    CHECK(scene.static_world->cull_mode == renderer::RenderCullMode::back);
    CHECK(scene.static_world->light_style_policy ==
        renderer::RenderBaselineLightStylePolicy::source_slot_zero);

    renderer::null::NullRenderer null_renderer;
    null_renderer.initialize();
    null_renderer.render(scene, {800, 600});
    const auto statistics = null_renderer.statistics();
    CHECK(statistics.rendered_frames == 1U);
    CHECK(statistics.static_world_present);
    CHECK(statistics.package_resource_id == package->resource_id());
    CHECK(statistics.package_revision == package->resource_revision());
    CHECK(statistics.camera_valid);
}

TEST_CASE("World preview and RenderScene preserve shared package lifetime",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    std::weak_ptr<const world_render::WorldRenderPackage> weak = package;
    {
        world_preview::WorldPreviewSceneSource source{package};
        package.reset();
        REQUIRE_FALSE(weak.expired());
        {
            auto scene = client::build_render_scene(source.world_state());
            REQUIRE(scene.static_world);
            REQUIRE(scene.static_world->package);
            CHECK(scene.static_world->package->vertices().size() == 4U);
        }
        REQUIRE_FALSE(weak.expired());
    }
    CHECK(weak.expired());
}

TEST_CASE("World preview rejects invalid diagnostic camera prerequisites",
          "[world-preview][scene]")
{
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{
            std::shared_ptr<const world_render::WorldRenderPackage>{}}),
        std::invalid_argument);

    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.isometric_direction = {};
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.orbit_angular_velocity_radians_per_second = 2.0F;
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.camera_mode = static_cast<world_preview::WorldPreviewCameraMode>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.cull_mode = static_cast<client::PreviewWorldCullMode>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    client::ClientWorldState state;
    CHECK_FALSE(state.set_preview_render_options(
        client::PreviewRenderOptions{options.cull_mode}));
    CHECK(state.preview_render_options().cull_mode ==
        client::PreviewWorldCullMode::none);
}

TEST_CASE("Client static-world state can be cleared without retaining the package",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    std::weak_ptr<const world_render::WorldRenderPackage> weak = package;
    client::ClientWorldState state;
    state.set_static_world(package);
    package.reset();
    REQUIRE_FALSE(weak.expired());
    CHECK(state.world_revision() != 0U);
    state.clear_static_world();
    CHECK_FALSE(state.static_world());
    CHECK(state.world_revision() == 0U);
    CHECK(weak.expired());
}

} // namespace
