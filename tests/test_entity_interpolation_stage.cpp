#include <hlclient/entity_visual/entity_interpolation_stage.hpp>

#include <hlclient/entity_render/entity_render_frame_composer.hpp>

#include "entity_visual/entity_pipeline_stage_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace entity = hlclient::entity_visual;
namespace render = hlclient::entity_render;
namespace fixture = hlclient::tests::entity_pipeline_stage_fixture;
namespace studio = hlclient::goldsrc::studio;
using hlclient::tests::ScopedLocalResourceTestRoot;

[[nodiscard]] std::shared_ptr<const render::EntityRenderFrame>
compose_render_frame(
    const render::EntitySceneRenderPackage& package,
    const hlclient::goldsrc::InterpolatedEntityFrame& interpolated_frame)
{
    render::EntityRenderFrameCompositionInput input;
    input.expected_scene_package_identity = {
        package.resource_id(), package.resource_revision()};
    input.frame_identity = {0x8'300U, 1U};
    input.previous_time_seconds = 10.0;
    input.current_time_seconds = 11.0;
    studio::StudioPoseCache pose_cache;
    auto composed = render::EntityRenderFrameComposer{}.compose(
        package, interpolated_frame, input, pose_cache);
    INFO((composed.error ? composed.error->context : std::string{}));
    REQUIRE(composed);
    REQUIRE(composed.frame);
    return std::make_shared<const render::EntityRenderFrame>(
        std::move(*composed.frame));
}

[[nodiscard]] std::shared_ptr<const render::EntityRenderFrame>
fabricate_render_frame(
    const render::EntitySceneRenderPackage& package,
    const hlclient::goldsrc::InterpolatedEntityFrame& interpolated_frame,
    const std::uint32_t entity_number,
    const std::uint64_t previous_state_identity)
{
    render::EntityRenderFrameBuildInput input;
    input.resource_id = 0x8'301U;
    input.resource_revision = 1U;
    input.interpolation = {
        interpolated_frame.sample_seconds(),
        10.0,
        11.0,
        static_cast<float>(interpolated_frame.alpha()),
        previous_state_identity,
        interpolated_frame.current_snapshot_reference(),
        render::EntityRenderInterpolationProfile::synthetic_seconds_v1,
    };
    input.unsupported_instances.push_back({
        entity_number,
        std::nullopt,
        render::UnsupportedEntityVisualReason::missing_asset,
        render::RuntimeEntityVisibilityStatus::asset_unavailable,
    });
    auto built = render::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.frame);
    return std::make_shared<const render::EntityRenderFrame>(
        std::move(*built.frame));
}

void advance_to_frame_build(
    entity::EntityInterpolationStage& stage,
    const std::shared_ptr<const entity::EntityVisualAssetStageResult>&
        visual_assets)
{
    stage.begin(fixture::kStartTime);
    REQUIRE(stage.provide_visual_assets(visual_assets));
    REQUIRE(stage.snapshot_pair_selected());
    REQUIRE(stage.entities_projected());
    REQUIRE(stage.entities_interpolated());
    REQUIRE(stage.studio_poses_evaluated());
    REQUIRE(stage.sprite_frames_selected());
    REQUIRE(stage.state() ==
        entity::EntityInterpolationStageState::building_entity_frame);
}

TEST_CASE("Entity interpolation stage publishes one immutable owning frame",
    "[entity-visual][pipeline-stage][interpolation][ready]")
{
    ScopedLocalResourceTestRoot root;
    const auto inputs = fixture::make_visual_pipeline_inputs(root);
    const auto visual_assets = fixture::publish_visual_stage_result(inputs);
    const auto frame = fixture::make_interpolated_frame(*inputs.history);
    const auto package = fixture::make_scene_package(inputs.library);
    REQUIRE_FALSE(frame->entities().empty());
    CHECK(frame->entities()[0U].model_reference().value() == 1U);

    entity::EntityInterpolationStage stage;
    stage.begin(fixture::kStartTime);
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::waiting_for_visual_assets);
    REQUIRE(stage.provide_visual_assets(visual_assets));
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::selecting_snapshot_pair);
    REQUIRE(stage.snapshot_pair_selected());
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::projecting_entities);
    REQUIRE(stage.entities_projected());
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::interpolating_entities);
    REQUIRE(stage.entities_interpolated());
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::evaluating_studio_poses);
    const auto render_frame = compose_render_frame(*package, *frame);
    REQUIRE(stage.studio_poses_evaluated());
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::selecting_sprite_frames);
    REQUIRE(stage.sprite_frames_selected());
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::building_entity_frame);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.publish_entity_frame(frame, render_frame));
    CHECK(stage.state() ==
        entity::EntityInterpolationStageState::entity_frame_ready);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->visual_assets() == visual_assets);
    CHECK(stage.result()->interpolated_frame() == frame);
    CHECK(stage.result()->render_frame() == render_frame);
    CHECK(stage.result()->render_frame()->statistics().candidate_count ==
        stage.result()->interpolated_frame()->entities().size());
}

TEST_CASE("Entity interpolation stage rejects mismatched terminal frame evidence",
    "[entity-visual][pipeline-stage][interpolation][failure]")
{
    ScopedLocalResourceTestRoot root;
    const auto inputs = fixture::make_visual_pipeline_inputs(root);
    const auto visual_assets = fixture::publish_visual_stage_result(inputs);
    const auto interpolated =
        fixture::make_interpolated_frame(*inputs.history);
    const auto package = fixture::make_scene_package(inputs.library);

    SECTION("candidate identity does not match the interpolated entity set")
    {
        const auto fabricated = fabricate_render_frame(*package,
            *interpolated,
            2U,
            interpolated->previous_snapshot_reference());
        entity::EntityInterpolationStage stage;
        advance_to_frame_build(stage, visual_assets);
        CHECK_FALSE(stage.publish_entity_frame(interpolated, fabricated));
        CHECK(stage.state() ==
            entity::EntityInterpolationStageState::failed);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
            entity::EntityPipelineStageErrorCode::invalid_input);
    }

    SECTION("snapshot identity does not match interpolation provenance")
    {
        const auto fabricated = fabricate_render_frame(*package,
            *interpolated,
            1U,
            interpolated->previous_snapshot_reference() + 1U);
        entity::EntityInterpolationStage stage;
        advance_to_frame_build(stage, visual_assets);
        CHECK_FALSE(stage.publish_entity_frame(interpolated, fabricated));
        CHECK(stage.state() ==
            entity::EntityInterpolationStageState::failed);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
            entity::EntityPipelineStageErrorCode::invalid_input);
    }
}

TEST_CASE("Entity interpolation stage has exact named contract states",
    "[entity-visual][pipeline-stage][interpolation][states]")
{
    using State = entity::EntityInterpolationStageState;
    constexpr std::array expected{
        std::pair{State::idle, std::string_view{"idle"}},
        std::pair{State::waiting_for_visual_assets,
            std::string_view{"waiting_for_visual_assets"}},
        std::pair{State::selecting_snapshot_pair,
            std::string_view{"selecting_snapshot_pair"}},
        std::pair{State::projecting_entities,
            std::string_view{"projecting_entities"}},
        std::pair{State::interpolating_entities,
            std::string_view{"interpolating_entities"}},
        std::pair{State::evaluating_studio_poses,
            std::string_view{"evaluating_studio_poses"}},
        std::pair{State::selecting_sprite_frames,
            std::string_view{"selecting_sprite_frames"}},
        std::pair{State::building_entity_frame,
            std::string_view{"building_entity_frame"}},
        std::pair{State::entity_frame_ready,
            std::string_view{"entity_frame_ready"}},
        std::pair{State::timeline_error, std::string_view{"timeline_error"}},
        std::pair{State::pose_error, std::string_view{"pose_error"}},
        std::pair{
            State::unsupported_visual, std::string_view{"unsupported_visual"}},
        std::pair{State::cancelled, std::string_view{"cancelled"}},
        std::pair{State::timed_out, std::string_view{"timed_out"}},
        std::pair{State::backpressure, std::string_view{"backpressure"}},
        std::pair{State::failed, std::string_view{"failed"}},
    };
    for (const auto& [state, name] : expected) {
        CHECK(entity::to_string(state) == name);
    }
}

TEST_CASE("Entity interpolation stage bounds cancellation timeout and pressure",
    "[entity-visual][pipeline-stage][interpolation][control]")
{
    using namespace std::chrono_literals;

    SECTION("explicit backpressure resumes exact work state")
    {
        entity::EntityInterpolationStage stage;
        stage.begin(fixture::kStartTime);
        stage.signal_backpressure();
        CHECK(stage.state() ==
            entity::EntityInterpolationStageState::backpressure);
        REQUIRE(stage.resume_from_backpressure());
        CHECK(stage.state() == entity::EntityInterpolationStageState::
            waiting_for_visual_assets);
        stage.cancel();
        CHECK(stage.state() ==
            entity::EntityInterpolationStageState::cancelled);
        CHECK_FALSE(stage.result());
    }

    SECTION("timeout is terminal")
    {
        entity::EntityInterpolationStage stage{{64U, 3ms}};
        stage.begin(fixture::kStartTime);
        stage.update(fixture::kStartTime + 3ms);
        CHECK(stage.state() ==
            entity::EntityInterpolationStageState::timed_out);
        REQUIRE(stage.error());
    }

    SECTION("transition cap does not publish a partial result")
    {
        ScopedLocalResourceTestRoot root;
        const auto inputs = fixture::make_visual_pipeline_inputs(root);
        const auto visual_assets =
            fixture::publish_visual_stage_result(inputs);
        entity::EntityInterpolationStage stage{{2U, std::nullopt}};
        stage.begin(fixture::kStartTime);
        REQUIRE(stage.provide_visual_assets(visual_assets));
        CHECK_FALSE(stage.snapshot_pair_selected());
        CHECK(stage.state() ==
            entity::EntityInterpolationStageState::backpressure);
        CHECK_FALSE(stage.resume_from_backpressure());
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
            entity::EntityPipelineStageErrorCode::transition_limit_reached);
    }
}

TEST_CASE("Entity interpolation stage preserves typed failures",
    "[entity-visual][pipeline-stage][interpolation][failure]")
{
    entity::EntityInterpolationStage timeline;
    timeline.begin(fixture::kStartTime);
    timeline.finish_timeline_error();
    CHECK(timeline.state() ==
        entity::EntityInterpolationStageState::timeline_error);

    entity::EntityInterpolationStage pose;
    pose.begin(fixture::kStartTime);
    pose.finish_pose_error();
    CHECK(pose.state() == entity::EntityInterpolationStageState::pose_error);

    entity::EntityInterpolationStage unsupported;
    unsupported.begin(fixture::kStartTime);
    unsupported.finish_unsupported_visual();
    CHECK(unsupported.state() ==
        entity::EntityInterpolationStageState::unsupported_visual);

    entity::EntityInterpolationStage invalid_order;
    invalid_order.begin(fixture::kStartTime);
    CHECK_FALSE(invalid_order.entities_projected());
    CHECK(invalid_order.state() ==
        entity::EntityInterpolationStageState::failed);
    REQUIRE(invalid_order.error());
    CHECK(invalid_order.error()->code ==
        entity::EntityPipelineStageErrorCode::invalid_transition);
}

using InterpolationResultPointer = std::remove_cvref_t<decltype(
    std::declval<const entity::EntityInterpolationStage&>().result())>;
static_assert(
    std::is_const_v<typename InterpolationResultPointer::element_type>);
static_assert(
    !std::is_copy_assignable_v<entity::EntityInterpolationStageResult>);

} // namespace
