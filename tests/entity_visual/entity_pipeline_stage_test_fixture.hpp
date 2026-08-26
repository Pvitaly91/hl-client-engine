#pragma once

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_visual/entity_interpolation_stage.hpp>
#include <hlclient/entity_visual/entity_visual_asset_stage.hpp>

#include "entity_visual_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::tests::entity_pipeline_stage_fixture {

namespace entity_render = hlclient::entity_render;
namespace entity_visual = hlclient::entity_visual;
namespace goldsrc = hlclient::goldsrc;
namespace visual_fixture = hlclient::tests::entity_visual_fixture;

inline constexpr entity_visual::EntityPipelineStageTimePoint kStartTime{};

struct VisualPipelineInputs {
    std::shared_ptr<const goldsrc::EntitySnapshotHistoryState> history;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment;
    std::shared_ptr<const goldsrc::PrecacheManifestState> manifest;
    std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>
        library;
};

[[nodiscard]] inline VisualPipelineInputs make_visual_pipeline_inputs(
    ScopedLocalResourceTestRoot& root)
{
    root.write("valve", "maps/test_map.bsp", "map");
    auto resources = visual_fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
    });
    auto environment = std::shared_ptr<
        const local_resources::LocalResourceEnvironment>{
        std::move(resources.environment)};
    auto manifest = std::make_shared<const goldsrc::PrecacheManifestState>(
        std::move(resources.manifest));

    constexpr std::array entity_numbers{1U};
    auto previous = visual_fixture::synthetic_snapshot(entity_numbers, 10U);
    auto current = visual_fixture::synthetic_snapshot(entity_numbers, 20U);
    goldsrc::EntitySnapshotHistoryBuilder history_builder{
        {}, goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    REQUIRE(history_builder.insert(previous));
    REQUIRE(history_builder.insert(current));
    auto history_result = history_builder.publish();
    INFO((history_result.error ? history_result.error->context
                               : std::string{}));
    REQUIRE(history_result);
    REQUIRE(history_result.state);
    auto history = std::make_shared<const goldsrc::EntitySnapshotHistoryState>(
        std::move(*history_result.state));

    entity_visual::SyntheticModelSlotResolver resolver;
    entity_visual::EntityVisualAssetLibraryBuilder library_builder;
    auto plan = library_builder.plan(
        0x8'100U,
        {},
        {},
        *manifest,
        resolver);
    INFO((plan.error ? plan.error->context : std::string{}));
    REQUIRE(plan);
    REQUIRE(plan.plan);
    auto published = library_builder.publish(
        *plan.plan,
        std::span<
            const entity_visual::EntityVisualAssetImportCompletion>{});
    INFO((published.error ? published.error->context : std::string{}));
    REQUIRE(published);
    REQUIRE(published.library);

    return {
        std::move(history),
        std::move(environment),
        std::move(manifest),
        std::move(published.library),
    };
}

[[nodiscard]] inline std::shared_ptr<
    const entity_visual::EntityVisualAssetStageResult>
publish_visual_stage_result(const VisualPipelineInputs& inputs)
{
    entity_visual::EntityVisualAssetStage stage;
    stage.begin(kStartTime);
    REQUIRE(stage.provide_snapshot_history(
        inputs.history, inputs.environment, inputs.manifest));
    REQUIRE(stage.visual_references_collected());
    REQUIRE(stage.model_slots_resolved());
    REQUIRE(stage.publish_library(inputs.library, {}));
    REQUIRE(stage.result());
    return stage.result();
}

[[nodiscard]] inline std::shared_ptr<const goldsrc::InterpolatedEntityFrame>
make_interpolated_frame(
    const goldsrc::EntitySnapshotHistoryState& history)
{
    const auto snapshots = history.snapshots();
    REQUIRE(snapshots.size() == 2U);
    auto previous_time =
        goldsrc::EntitySnapshotExplicitTime::bind_synthetic_seconds(
            snapshots[0U], 10.0);
    auto current_time =
        goldsrc::EntitySnapshotExplicitTime::bind_synthetic_seconds(
            snapshots[1U], 11.0);
    REQUIRE(previous_time);
    REQUIRE(current_time);
    std::vector<goldsrc::EntitySnapshotExplicitTime> times;
    times.push_back(std::move(*previous_time));
    times.push_back(std::move(*current_time));
    const auto sample_time =
        goldsrc::EntityInterpolationTime::synthetic_seconds(10.5);
    REQUIRE(sample_time);
    auto selected = goldsrc::EntitySnapshotPairSelector{}.select(
        history, times, *sample_time);
    INFO((selected.error ? selected.error->context : std::string{}));
    REQUIRE(selected);
    REQUIRE(selected.selection);

    goldsrc::SyntheticEntityInterpolationState previous;
    previous.entity_number = 1U;
    previous.model_reference =
        entity_visual::EntityVisualModelReference::synthetic_model_slot(1U);
    previous.discrete.model_reference = 1U;
    auto current = previous;
    current.position.x = 8.0F;
    const std::array previous_states{previous};
    const std::array current_states{current};
    auto interpolated = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        *selected.selection,
        {10U, previous_states},
        {20U, current_states});
    INFO((interpolated.error ? interpolated.error->context : std::string{}));
    REQUIRE(interpolated);
    REQUIRE(interpolated.frame);
    return std::make_shared<const goldsrc::InterpolatedEntityFrame>(
        std::move(*interpolated.frame));
}

[[nodiscard]] inline std::shared_ptr<const entity_render::EntitySceneRenderPackage>
make_scene_package(
    std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState> library,
    std::optional<entity_render::EntityRenderResourceIdentity>
        world_scene_association = std::nullopt)
{
    entity_render::EntitySceneRenderPackageCreateInfo input;
    input.asset_library = library;
    input.asset_library_identity = {
        library->resource_id(), library->resource_revision()};
    input.resource_id = 0x8'200U;
    input.world_scene_association = world_scene_association;
    auto built = entity_render::EntitySceneRenderPackageBuilder{}.build(
        std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return std::make_shared<const entity_render::EntitySceneRenderPackage>(
        std::move(*built.package));
}

} // namespace hlclient::tests::entity_pipeline_stage_fixture
