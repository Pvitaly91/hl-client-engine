#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

namespace {

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

TEST_CASE("Null renderer owns its last scene and records lifecycle statistics", "[renderer][null]")
{
    hlclient::renderer::null::NullRenderer renderer;
    CHECK_FALSE(renderer.statistics().initialized);
    CHECK_FALSE(renderer.statistics().shutdown);
    CHECK(renderer.statistics().rendered_frames == 0U);
    CHECK_FALSE(renderer.statistics().last_scene.has_value());

    CHECK_THROWS_AS(renderer.render({}, {}), std::logic_error);
    renderer.initialize();
    CHECK(renderer.statistics().initialized);
    CHECK_FALSE(renderer.statistics().shutdown);

    {
        hlclient::renderer::RenderScene scene;
        scene.clear_color.red = 0.75F;
        renderer.render(scene, hlclient::renderer::RenderExtent{640, 480});
        scene.clear_color.red = 0.0F;
    }

    REQUIRE(renderer.statistics().last_scene.has_value());
    CHECK(renderer.statistics().rendered_frames == 1U);
    CHECK(renderer.statistics().last_scene->clear_color.red == 0.75F);
    CHECK(renderer.statistics().last_extent.width == 640);
    CHECK(renderer.statistics().last_extent.height == 480);
    CHECK(renderer.information().device == "Null Renderer");

    const auto submitted_statistics = renderer.statistics();
    renderer.shutdown();
    renderer.shutdown();
    CHECK_FALSE(renderer.statistics().initialized);
    CHECK(renderer.statistics().shutdown);
    CHECK(submitted_statistics.rendered_frames == 1U);
    CHECK(submitted_statistics.last_scene.has_value());
    CHECK_THROWS_AS(renderer.render({}, {}), std::logic_error);
}

} // namespace
