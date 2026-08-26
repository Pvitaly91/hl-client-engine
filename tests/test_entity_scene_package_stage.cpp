#include <hlclient/entity_render/entity_scene_package_stage.hpp>

#include "entity_visual/entity_pipeline_stage_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace entity_render = hlclient::entity_render;
namespace entity_visual = hlclient::entity_visual;
namespace fixture = hlclient::tests::entity_pipeline_stage_fixture;
using hlclient::tests::ScopedLocalResourceTestRoot;

TEST_CASE("Entity scene package stage publishes exact immutable ownership",
    "[entity-render][pipeline-stage][scene-package][ready]")
{
    ScopedLocalResourceTestRoot root;
    const auto inputs = fixture::make_visual_pipeline_inputs(root);
    const auto visual_assets = fixture::publish_visual_stage_result(inputs);
    const auto package = fixture::make_scene_package(inputs.library);

    entity_render::EntityScenePackageStage stage;
    CHECK(stage.state() == entity_render::EntityScenePackageStageState::
        waiting_for_visual_assets);
    stage.begin(fixture::kStartTime);
    REQUIRE(stage.provide_visual_assets(visual_assets));
    CHECK(stage.state() == entity_render::EntityScenePackageStageState::
        building_studio_render_assets);
    REQUIRE(stage.studio_render_assets_built());
    CHECK(stage.state() == entity_render::EntityScenePackageStageState::
        building_sprite_render_assets);
    REQUIRE(stage.sprite_render_assets_built());
    CHECK(stage.state() == entity_render::EntityScenePackageStageState::
        building_entity_scene_package);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.publish_scene_package(package));
    CHECK(stage.state() == entity_render::EntityScenePackageStageState::
        entity_scene_package_ready);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->visual_assets() == visual_assets);
    CHECK(stage.result()->visual_assets()->snapshot_history() == inputs.history);
    CHECK(stage.result()->visual_assets()->environment() == inputs.environment);
    CHECK(stage.result()->visual_assets()->manifest() == inputs.manifest);
    CHECK(stage.result()->visual_assets()->library() == inputs.library);
    CHECK(stage.result()->scene_package() == package);
    CHECK_FALSE(stage.result()->world_scene());
}

TEST_CASE("Entity scene package stage has exact named contract states",
    "[entity-render][pipeline-stage][scene-package][states]")
{
    using State = entity_render::EntityScenePackageStageState;
    constexpr std::array expected{
        std::pair{State::waiting_for_visual_assets,
            std::string_view{"waiting_for_visual_assets"}},
        std::pair{State::building_studio_render_assets,
            std::string_view{"building_studio_render_assets"}},
        std::pair{State::building_sprite_render_assets,
            std::string_view{"building_sprite_render_assets"}},
        std::pair{State::building_entity_scene_package,
            std::string_view{"building_entity_scene_package"}},
        std::pair{State::entity_scene_package_ready,
            std::string_view{"entity_scene_package_ready"}},
        std::pair{State::render_asset_failed,
            std::string_view{"render_asset_failed"}},
        std::pair{State::cancelled, std::string_view{"cancelled"}},
        std::pair{State::timed_out, std::string_view{"timed_out"}},
        std::pair{State::backpressure, std::string_view{"backpressure"}},
    };
    for (const auto& [state, name] : expected) {
        CHECK(entity_render::to_string(state) == name);
    }
}

TEST_CASE("Entity scene package stage bounds cancellation timeout and pressure",
    "[entity-render][pipeline-stage][scene-package][control]")
{
    using namespace std::chrono_literals;

    SECTION("explicit backpressure resumes exact work state")
    {
        entity_render::EntityScenePackageStage stage;
        stage.begin(fixture::kStartTime);
        stage.signal_backpressure();
        CHECK(stage.state() ==
            entity_render::EntityScenePackageStageState::backpressure);
        REQUIRE(stage.resume_from_backpressure());
        CHECK(stage.state() == entity_render::EntityScenePackageStageState::
            waiting_for_visual_assets);
        stage.cancel();
        CHECK(stage.state() ==
            entity_render::EntityScenePackageStageState::cancelled);
        CHECK_FALSE(stage.result());
    }

    SECTION("timeout is terminal")
    {
        entity_render::EntityScenePackageStage stage{{64U, 4ms}};
        stage.begin(fixture::kStartTime);
        stage.update(fixture::kStartTime + 4ms);
        CHECK(stage.state() ==
            entity_render::EntityScenePackageStageState::timed_out);
        REQUIRE(stage.error());
    }

    SECTION("transition cap is deterministic and non-resumable")
    {
        ScopedLocalResourceTestRoot root;
        const auto inputs = fixture::make_visual_pipeline_inputs(root);
        const auto visual_assets =
            fixture::publish_visual_stage_result(inputs);
        entity_render::EntityScenePackageStage stage{{2U, std::nullopt}};
        stage.begin(fixture::kStartTime);
        REQUIRE(stage.provide_visual_assets(visual_assets));
        REQUIRE(stage.studio_render_assets_built());
        CHECK_FALSE(stage.sprite_render_assets_built());
        CHECK(stage.state() ==
            entity_render::EntityScenePackageStageState::backpressure);
        CHECK_FALSE(stage.resume_from_backpressure());
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code == entity_visual::
            EntityPipelineStageErrorCode::transition_limit_reached);
    }
}

TEST_CASE("Entity scene package stage rejects mismatched ownership and failures",
    "[entity-render][pipeline-stage][scene-package][failure]")
{
    SECTION("explicit render asset failure is typed")
    {
        entity_render::EntityScenePackageStage stage;
        stage.begin(fixture::kStartTime);
        stage.finish_render_asset_failed();
        CHECK(stage.state() == entity_render::EntityScenePackageStageState::
            render_asset_failed);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
            entity_visual::EntityPipelineStageErrorCode::operation_failed);
    }

    SECTION("scene association cannot outlive unretained world ownership")
    {
        ScopedLocalResourceTestRoot root;
        const auto inputs = fixture::make_visual_pipeline_inputs(root);
        const auto visual_assets =
            fixture::publish_visual_stage_result(inputs);
        const auto package = fixture::make_scene_package(
            inputs.library,
            entity_render::EntityRenderResourceIdentity{0x77U, 1U});
        entity_render::EntityScenePackageStage stage;
        stage.begin(fixture::kStartTime);
        REQUIRE(stage.provide_visual_assets(visual_assets));
        REQUIRE(stage.studio_render_assets_built());
        REQUIRE(stage.sprite_render_assets_built());
        CHECK_FALSE(stage.publish_scene_package(package));
        CHECK(stage.state() == entity_render::EntityScenePackageStageState::
            render_asset_failed);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
            entity_visual::EntityPipelineStageErrorCode::invalid_input);
    }

    SECTION("work cannot enter before begin")
    {
        entity_render::EntityScenePackageStage stage;
        CHECK_FALSE(stage.provide_visual_assets({}));
        CHECK(stage.state() == entity_render::EntityScenePackageStageState::
            render_asset_failed);
    }
}

using SceneResultPointer = std::remove_cvref_t<decltype(
    std::declval<const entity_render::EntityScenePackageStage&>().result())>;
static_assert(std::is_const_v<typename SceneResultPointer::element_type>);
static_assert(
    !std::is_copy_assignable_v<entity_render::EntityScenePackageStageResult>);

} // namespace
