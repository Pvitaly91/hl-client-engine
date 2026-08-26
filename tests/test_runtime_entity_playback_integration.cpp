#include "entity_render/entity_opengl_test_support.hpp"
#include "entity_snapshot_fake_hlds_test_support.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/entity_render/entity_scene_package_stage.hpp>
#include <hlclient/entity_visual/entity_interpolation_stage.hpp>
#include <hlclient/entity_visual/entity_visual_asset_stage.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace client = hlclient::client;
namespace entity_render = hlclient::entity_render;
namespace entity_visual = hlclient::entity_visual;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_visual = hlclient::goldsrc::visual_assets;
namespace opengl_fixture = hlclient::tests::entity_opengl_fixture;
namespace studio = hlclient::goldsrc::studio;
namespace sprite_fixture = hlclient::tests::sprite_fixture;
namespace visual_fixture = hlclient::tests::entity_visual_fixture;
namespace world_fixture = hlclient::tests::world_render_fixture;
using Catch::Approx;
using hlclient::tests::ScopedLocalResourceTestRoot;

enum class PlaybackKind {
    studio_only,
    sprite_only,
    mixed,
};

struct SharedLocalSources {
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
        environment;
    std::shared_ptr<const goldsrc::PrecacheManifestState> manifest;
    std::shared_ptr<const assets::AssetImporterRegistries> registries;
};

struct TestRootFileSnapshot {
    std::string relative_name;
    std::vector<char> bytes;
    std::filesystem::file_time_type write_time{};

    [[nodiscard]] friend bool operator==(
        const TestRootFileSnapshot&,
        const TestRootFileSnapshot&) = default;
};

struct PreparedPlayback {
    PlaybackKind kind{PlaybackKind::mixed};
    std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
        visual_stage;
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package;
    std::vector<entity_visual::EntityVisualProjectionState>
        previous_projections;
    std::vector<entity_visual::EntityVisualProjectionState>
        current_projections;
    std::size_t exact_import_count{0U};
};

struct BuiltPlaybackFrame {
    std::shared_ptr<const goldsrc::InterpolatedEntityFrame> interpolated;
    std::shared_ptr<const entity_render::EntityRenderFrame> render_frame;
    float entity_one_position_x{0.0F};
    float entity_one_pose_translation_x{0.0F};
    std::size_t sprite_selection_count{0U};
};

[[nodiscard]] std::shared_ptr<const assets::ModelAsset>
make_animated_model_asset(const assets::ModelAsset& source);

[[nodiscard]] std::vector<TestRootFileSnapshot> snapshot_test_root(
    const std::filesystem::path& root)
{
    std::vector<TestRootFileSnapshot> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{
             root}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream stream{entry.path(), std::ios::binary};
        if (!stream) {
            throw std::runtime_error{"Unable to snapshot synthetic test file"};
        }
        std::vector<char> bytes{
            std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
        result.push_back(TestRootFileSnapshot{
            std::filesystem::relative(entry.path(), root).generic_string(),
            std::move(bytes),
            entry.last_write_time()});
    }
    std::ranges::sort(result, {}, &TestRootFileSnapshot::relative_name);
    return result;
}

[[nodiscard]] SharedLocalSources make_local_sources(
    const ScopedLocalResourceTestRoot& root)
{
    const auto model_size = static_cast<std::uint32_t>(
        hlclient::tests::literal_minimal_goldsrc_studio_v10().size());
    const auto sprite_size = static_cast<std::uint32_t>(
        sprite_fixture::literal_single_sprite().size());
    auto resources = visual_fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/test.mdl", 1U, model_size, 0U},
        {2U, "sprites/test.spr", 2U, sprite_size, 0U},
    });
    auto registries = std::make_shared<assets::AssetImporterRegistries>();
    const auto registered = goldsrc::register_builtin_asset_importers(
        *registries);
    INFO((registered.error ? registered.error->context : std::string{}));
    REQUIRE(registered);
    return {
        std::shared_ptr<
            const hlclient::local_resources::LocalResourceEnvironment>{
            std::move(resources.environment)},
        std::make_shared<const goldsrc::PrecacheManifestState>(
            std::move(resources.manifest)),
        std::move(registries),
    };
}

[[nodiscard]] std::uint32_t model_slot_for(
    const PlaybackKind kind,
    const std::uint32_t entity_number) noexcept
{
    if (kind == PlaybackKind::studio_only) {
        return 1U;
    }
    if (kind == PlaybackKind::sprite_only) {
        return 2U;
    }
    return entity_number == 1U ? 1U : 2U;
}

[[nodiscard]] std::vector<entity_visual::EntityVisualProjectionState>
project_snapshot(
    const goldsrc::EntitySnapshotState& snapshot,
    const PlaybackKind kind,
    const bool current)
{
    std::vector<entity_visual::SyntheticEntityVisualInput> inputs;
    inputs.reserve(snapshot.entity_count());
    for (const auto& snapshot_entity : snapshot.entities()) {
        entity_visual::SyntheticEntityVisualInput input;
        input.entity_number = snapshot_entity.entity_number();
        input.model_reference =
            entity_visual::EntityVisualModelReference::synthetic_model_slot(
                model_slot_for(kind, input.entity_number));
        input.origin = entity_visual::EntityVisualVector3{
            input.entity_number == 1U ? (current ? 2.0F : 0.0F)
                : input.entity_number == 2U ? -2.0F
                                             : 2.0F,
            0.0F,
            0.5F,
        };
        input.angles_degrees = entity_visual::EntityVisualVector3{
            0.0F, current && input.entity_number == 1U ? 20.0F : 0.0F, 0.0F};
        input.sequence_index = 0U;
        input.studio_frame_coordinate =
            current && input.entity_number == 1U ? 1.0F : 0.0F;
        input.body_value = 0U;
        input.skin_family_index = 0U;
        input.sprite_frame_index = 0U;
        input.scale = 1.0F;
        input.interpolation_mode =
            entity_visual::EntityInterpolationMode::interpolate;
        input.animation_start_time_seconds = 0.0;
        inputs.push_back(std::move(input));
    }
    auto provider = entity_visual::SyntheticEntityVisualProjectionProvider::
        create(std::move(inputs));
    INFO(provider.context);
    REQUIRE(provider);
    REQUIRE(provider.provider);

    std::vector<entity_visual::EntityVisualProjectionState> output;
    output.reserve(snapshot.entity_count());
    for (const auto& snapshot_entity : snapshot.entities()) {
        auto projected = provider.provider->project(snapshot, snapshot_entity);
        INFO(projected.context);
        REQUIRE(projected);
        REQUIRE(projected.state);
        output.push_back(std::move(*projected.state));
    }
    return output;
}

[[nodiscard]] std::shared_ptr<const assets::ModelAsset>
make_animated_model_asset(const assets::ModelAsset& source)
{
    REQUIRE(source.skeletal_data);
    auto model = *source.skeletal_data;
    REQUIRE(model.bones.size() == 1U);
    model.sequence_groups = {{"integration", {}, 0U, false}};
    model.sequences.clear();

    assets::ModelBoneAnimationTrack track;
    track.bone_index = 0U;
    for (std::size_t channel_index = 0U;
         channel_index < track.channels.size();
         ++channel_index) {
        auto& channel = track.channels[channel_index];
        channel.semantic =
            static_cast<assets::ModelAnimationChannelSemantic>(channel_index);
        channel.frame_coverage = 2U;
        channel.source_default = 0.0F;
        channel.source_scale = 0.0F;
    }
    track.channels[0U].source_scale = 1.0F;
    track.channels[0U].runs.push_back(
        {0U, 2U, 2U, {0, 2}});

    assets::ModelAnimationBlend blend;
    blend.source_blend_ordinal = 0U;
    blend.bone_tracks.push_back(std::move(track));
    assets::ModelSequence sequence;
    sequence.label = "integration";
    sequence.frames_per_second = 30.0F;
    sequence.frame_count = 2U;
    sequence.blend_count = 1U;
    sequence.motion_bone = 0;
    sequence.sequence_group_index = 0U;
    sequence.animation_blends.push_back(std::move(blend));
    model.sequences.push_back(std::move(sequence));
    model.statistics.sequence_count = 1U;
    model.statistics.sequence_group_count = 1U;
    model.statistics.animation_run_count = 1U;
    model.statistics.animation_value_bytes = 2U * sizeof(std::int16_t);
    auto output = std::make_shared<assets::ModelAsset>(source);
    output->skeletal_data =
        std::make_shared<const assets::SkeletalModelAssetData>(
            std::move(model));
    return output;
}

[[nodiscard]] entity_visual::EntityVisualAssetImportCompletion
import_visual_request(
    const entity_visual::EntityVisualAssetImportRequest& request,
    const SharedLocalSources& sources)
{
    REQUIRE(sources.environment);
    REQUIRE(sources.manifest);
    REQUIRE(sources.registries);
    const auto* manifest_entry = sources.manifest->find(
        goldsrc::ResourceType::model, request.model_slot());
    REQUIRE(manifest_entry != nullptr);
    auto dispatch = goldsrc::AssetDispatchPlanBuilder{}.build(
        *sources.manifest, *manifest_entry);
    INFO((dispatch.error ? dispatch.error->context : std::string{}));
    REQUIRE(dispatch);
    REQUIRE(dispatch.plan);

    auto opened = goldsrc::ApprovedAssetSourceOpener{}.begin(
        *dispatch.plan, sources.environment);
    INFO((opened.error ? opened.error->context : std::string{}));
    REQUIRE(opened);
    REQUIRE(opened.operation);
    auto source_operation = std::move(*opened.operation);
    for (std::size_t update = 0U; update < 64U; ++update) {
        if (source_operation.state() ==
            goldsrc::ApprovedAssetSourceOpenState::source_ready) {
            break;
        }
        source_operation.update(
            goldsrc::ApprovedAssetSourceOpenTimePoint{} +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    INFO(to_string(source_operation.state()));
    REQUIRE(source_operation.state() ==
        goldsrc::ApprovedAssetSourceOpenState::source_ready);
    auto approved = source_operation.take_result();
    REQUIRE(approved);

    auto started = goldsrc_visual::GoldSrcVisualAssetImportOperation::begin(
        *approved, sources.environment, *sources.registries);
    INFO((started.error ? started.error->context : std::string{}));
    REQUIRE(started);
    REQUIRE(started.operation);
    auto import_operation = std::move(*started.operation);
    for (std::size_t update = 0U;
         update < 64U && !import_operation.terminal();
         ++update) {
        import_operation.update(
            goldsrc_visual::GoldSrcVisualAssetImportTimePoint{} +
            std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    INFO(to_string(import_operation.state()));
    INFO((import_operation.error()
            ? import_operation.error()->context
            : std::string{}));
    REQUIRE(import_operation.state() ==
        goldsrc_visual::GoldSrcVisualAssetImportState::asset_ready);
    auto imported = import_operation.take_result();
    REQUIRE(imported);
    REQUIRE(imported->resource_index() == request.model_slot());

    std::vector<assets::AssetSourceFingerprint> fingerprints{
        imported->source_fingerprints().begin(),
        imported->source_fingerprints().end()};
    const auto total_source_bytes =
        imported->dependency_statistics().total_source_bytes;
    const auto importer_id = std::string{imported->selected_importer_id()};
    std::optional<entity_visual::EntityVisualImportedAssetCandidate> candidate;
    if (const auto* model =
            std::get_if<assets::ModelAsset>(&imported->asset())) {
        candidate.emplace(
            entity_visual::EntityVisualImportedAssetCandidate::studio_model(
                request.source_key(),
                make_animated_model_asset(*model),
                importer_id,
                total_source_bytes,
                std::move(fingerprints)));
    } else if (const auto* sprite =
                   std::get_if<assets::SpriteAsset>(&imported->asset())) {
        candidate.emplace(
            entity_visual::EntityVisualImportedAssetCandidate::sprite(
                request.source_key(),
                std::make_shared<const assets::SpriteAsset>(*sprite),
                importer_id,
                total_source_bytes,
                std::move(fingerprints)));
    }
    REQUIRE(candidate);
    return {request.request_index(),
        entity_visual::EntityVisualAssetImportCompletionStatus::imported,
        std::move(candidate)};
}

[[nodiscard]] PreparedPlayback prepare_playback(
    const std::shared_ptr<const goldsrc::EntitySnapshotHistoryState>& history,
    const SharedLocalSources& sources,
    const PlaybackKind kind,
    const std::uint64_t resource_id)
{
    REQUIRE(history);
    REQUIRE(history->snapshots().size() == 2U);
    auto previous = project_snapshot(history->snapshots()[0U], kind, false);
    auto current = project_snapshot(history->snapshots()[1U], kind, true);
    std::vector<entity_visual::EntityVisualProjectionState> all;
    all.reserve(previous.size() + current.size());
    for (const auto& projection : previous) {
        all.push_back(projection);
    }
    for (const auto& projection : current) {
        all.push_back(projection);
    }

    entity_visual::SyntheticModelSlotResolver resolver;
    entity_visual::EntityVisualAssetLibraryBuilder library_builder;
    auto planned = library_builder.plan(
        resource_id, {}, all, *sources.manifest, resolver);
    INFO((planned.error ? planned.error->context : std::string{}));
    REQUIRE(planned);
    REQUIRE(planned.plan);
    const auto expected_imports = kind == PlaybackKind::mixed ? 2U : 1U;
    REQUIRE(planned.plan->requests().size() == expected_imports);

    std::vector<entity_visual::EntityVisualAssetImportCompletion> completions;
    completions.reserve(planned.plan->requests().size());
    for (const auto& request : planned.plan->requests()) {
        REQUIRE((request.model_slot() == 1U || request.model_slot() == 2U));
        completions.push_back(import_visual_request(request, sources));
    }
    auto published = library_builder.publish(*planned.plan, completions);
    INFO((published.error ? published.error->context : std::string{}));
    REQUIRE(published);
    REQUIRE(published.library);
    CHECK(published.library->statistics().cumulative_import_request_count ==
        expected_imports);
    std::size_t real_studio_imports = 0U;
    std::size_t real_sprite_imports = 0U;
    for (const auto& record : published.library->records()) {
        REQUIRE_FALSE(record.source_fingerprints().empty());
        if (record.kind() ==
            entity_visual::EntityVisualAssetKind::studio_model) {
            CHECK(record.importer_id() == "model:goldsrc-studio-mdl-v10");
            ++real_studio_imports;
        } else {
            CHECK(record.kind() ==
                entity_visual::EntityVisualAssetKind::sprite);
            CHECK(record.importer_id() == "sprite:goldsrc-sprite-v2");
            ++real_sprite_imports;
        }
    }
    CHECK(real_studio_imports ==
        (kind == PlaybackKind::sprite_only ? 0U : 1U));
    CHECK(real_sprite_imports ==
        (kind == PlaybackKind::studio_only ? 0U : 1U));

    auto library = published.library;
    entity_visual::EntityVisualAssetStage visual_stage;
    visual_stage.begin(entity_visual::EntityPipelineStageTimePoint{});
    REQUIRE(visual_stage.provide_snapshot_history(
        history, sources.environment, sources.manifest));
    REQUIRE(visual_stage.visual_references_collected());
    REQUIRE(visual_stage.model_slots_resolved());
    REQUIRE(visual_stage.publish_library(
        library, std::move(published.bindings)));
    REQUIRE(visual_stage.result());
    auto visual_result = visual_stage.result();

    std::vector<std::shared_ptr<const entity_render::StudioModelRenderAsset>>
        studio_assets;
    std::vector<std::shared_ptr<const entity_render::SpriteRenderAsset>>
        sprite_assets;
    for (const auto& record : library->records()) {
        const entity_render::EntityRenderResourceIdentity identity{
            record.resource_id(), record.resource_revision()};
        if (record.kind() ==
            entity_visual::EntityVisualAssetKind::studio_model) {
            auto built = entity_render::StudioModelRenderAssetBuilder{}.build(
                *record.model_asset(), identity);
            INFO((built.error ? built.error->context : std::string{}));
            REQUIRE(built);
            studio_assets.push_back(
                std::make_shared<const entity_render::StudioModelRenderAsset>(
                    std::move(*built.asset)));
        } else {
            auto built = entity_render::SpriteRenderAssetBuilder{}.build(
                *record.sprite_asset(), identity);
            INFO((built.error ? built.error->context : std::string{}));
            REQUIRE(built);
            sprite_assets.push_back(
                std::make_shared<const entity_render::SpriteRenderAsset>(
                    std::move(*built.asset)));
        }
    }

    entity_render::EntitySceneRenderPackageCreateInfo scene_input;
    scene_input.asset_library = library;
    scene_input.asset_library_identity = {
        library->resource_id(), library->resource_revision()};
    scene_input.resource_id = resource_id + 0x1000U;
    scene_input.studio_assets = std::move(studio_assets);
    scene_input.sprite_assets = std::move(sprite_assets);
    auto scene = entity_render::EntitySceneRenderPackageBuilder{}.build(
        std::move(scene_input));
    INFO((scene.error ? scene.error->context : std::string{}));
    REQUIRE(scene);
    auto package =
        std::make_shared<const entity_render::EntitySceneRenderPackage>(
            std::move(*scene.package));

    entity_render::EntityScenePackageStage scene_stage;
    scene_stage.begin(entity_visual::EntityPipelineStageTimePoint{});
    REQUIRE(scene_stage.provide_visual_assets(visual_result));
    REQUIRE(scene_stage.studio_render_assets_built());
    REQUIRE(scene_stage.sprite_render_assets_built());
    REQUIRE(scene_stage.publish_scene_package(package));
    REQUIRE(scene_stage.result());
    CHECK(scene_stage.result()->scene_package() == package);

    return {kind,
        std::move(visual_result),
        std::move(package),
        std::move(previous),
        std::move(current),
        completions.size()};
}

[[nodiscard]] BuiltPlaybackFrame build_playback_frame(
    const PreparedPlayback& prepared,
    const double target_seconds,
    const std::uint64_t frame_revision)
{
    const auto history = prepared.visual_stage->snapshot_history();
    REQUIRE(history);
    const auto snapshots = history->snapshots();
    REQUIRE(snapshots.size() == 2U);
    auto first_time = goldsrc::EntitySnapshotExplicitTime::
        bind_synthetic_seconds(snapshots[0U], 0.0);
    auto second_time = goldsrc::EntitySnapshotExplicitTime::
        bind_synthetic_seconds(snapshots[1U], 1.0);
    REQUIRE(first_time);
    REQUIRE(second_time);
    std::vector<goldsrc::EntitySnapshotExplicitTime> times;
    times.push_back(std::move(*first_time));
    times.push_back(std::move(*second_time));
    const auto target =
        goldsrc::EntityInterpolationTime::synthetic_seconds(target_seconds);
    REQUIRE(target);
    const auto selection = goldsrc::EntitySnapshotPairSelector{}.select(
        *history, times, *target);
    REQUIRE(selection);
    REQUIRE(selection.selection);

    const auto previous = goldsrc::EntityInterpolationProjectionAdapter{}.build(
        snapshots[0U], prepared.previous_projections);
    INFO((previous.error ? previous.error->context : std::string{}));
    REQUIRE(previous);
    REQUIRE(previous.frame);
    const auto current = goldsrc::EntityInterpolationProjectionAdapter{}.build(
        snapshots[1U], prepared.current_projections);
    INFO((current.error ? current.error->context : std::string{}));
    REQUIRE(current);
    REQUIRE(current.frame);
    auto interpolated = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        *selection.selection,
        previous.frame->view(),
        current.frame->view());
    INFO((interpolated.error ? interpolated.error->context : std::string{}));
    REQUIRE(interpolated);
    auto interpolated_frame =
        std::make_shared<const goldsrc::InterpolatedEntityFrame>(
            std::move(*interpolated.frame));

    entity_visual::EntityInterpolationStage interpolation_stage;
    interpolation_stage.begin(entity_visual::EntityPipelineStageTimePoint{});
    REQUIRE(interpolation_stage.provide_visual_assets(prepared.visual_stage));
    REQUIRE(interpolation_stage.snapshot_pair_selected());
    REQUIRE(interpolation_stage.entities_projected());
    REQUIRE(interpolation_stage.entities_interpolated());

    entity_render::EntityRenderFrameCompositionInput composition_input;
    composition_input.expected_scene_package_identity = {
        prepared.package->resource_id(), prepared.package->resource_revision()};
    composition_input.frame_identity = {
        prepared.package->resource_id() + 0x1000U, frame_revision};
    composition_input.previous_time_seconds =
        selection.selection->previous_seconds;
    composition_input.current_time_seconds =
        selection.selection->current_seconds;
    studio::StudioPoseCache pose_cache;
    auto composed = entity_render::EntityRenderFrameComposer{}.compose(
        *prepared.package,
        *interpolated_frame,
        composition_input,
        pose_cache);
    INFO((composed.error ? composed.error->context : std::string{}));
    REQUIRE(composed);
    REQUIRE(composed.frame);
    auto render_frame =
        std::make_shared<const entity_render::EntityRenderFrame>(
            std::move(*composed.frame));
    REQUIRE(interpolation_stage.studio_poses_evaluated());
    REQUIRE(interpolation_stage.sprite_frames_selected());
    REQUIRE(interpolation_stage.publish_entity_frame(
        interpolated_frame, render_frame));
    REQUIRE(interpolation_stage.result());
    CHECK(interpolation_stage.result()->interpolated_frame() ==
        interpolated_frame);
    CHECK(interpolation_stage.result()->render_frame() == render_frame);

    float entity_one_position_x = 0.0F;
    float entity_one_pose_translation_x = 0.0F;
    for (const auto& state : interpolated_frame->entities()) {
        if (state.entity_number() == 1U) {
            entity_one_position_x = state.position().x;
        }
    }
    const auto studio_one = std::ranges::find_if(
        render_frame->studio_instances(), [](const auto& instance) {
            return instance.entity_number == 1U;
        });
    if (studio_one != render_frame->studio_instances().end()) {
        REQUIRE(static_cast<std::size_t>(studio_one->pose_index) <
            render_frame->studio_poses().size());
        const auto& pose =
            render_frame->studio_poses()[studio_one->pose_index];
        REQUIRE(pose.bone_matrices.size() == 1U);
        entity_one_pose_translation_x = pose.bone_matrices[0U][12U];
    }
    const auto sprite_selection_count =
        render_frame->statistics().sprite_instance_count;
    return {std::move(interpolated_frame),
        std::move(render_frame),
        entity_one_position_x,
        entity_one_pose_translation_x,
        sprite_selection_count};
}

void require_client_and_null_composition(
    const PreparedPlayback& prepared,
    const BuiltPlaybackFrame& frame,
    const std::shared_ptr<const hlclient::world_render::WorldRenderPackage>&
        world)
{
    client::ClientWorldState state;
    state.set_static_world(world);
    REQUIRE(state.set_dynamic_entities(prepared.package, frame.render_frame));
    const auto scene = client::build_render_scene(state);
    REQUIRE(scene.static_world);
    REQUIRE(scene.dynamic_entities);
    CHECK(scene.static_world->package == world);
    CHECK(scene.dynamic_entities->package == prepared.package);
    CHECK(scene.dynamic_entities->frame == frame.render_frame);

    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    renderer.render(scene, {96, 96});
    const auto statistics = renderer.statistics();
    CHECK(statistics.static_world_present);
    CHECK(statistics.dynamic_entity_package_present);
    CHECK(statistics.entity_scene_revision ==
        prepared.package->resource_revision());
    CHECK(statistics.entity_frame_revision ==
        frame.render_frame->resource_revision());
    CHECK(statistics.visible_entity_count ==
        frame.render_frame->statistics().visible_count);
}

[[nodiscard]] std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
make_world_package()
{
    auto built = world_fixture::make_package();
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}

void require_network_proof(
    const hlclient::test_support::EntitySnapshotHappyRouteProof& proof,
    const std::size_t expected_request_count)
{
    REQUIRE(proof.snapshot_history);
    CHECK(proof.snapshot_history->snapshot_count() == 2U);
    CHECK(proof.network_endpoint_count == 1U);
    CHECK(proof.semantic_entity_request_count == expected_request_count);
    CHECK(proof.cleanup_count == 1U);
    CHECK(proof.authentication_release_count == 1U);
    CHECK(proof.consistency_release_count == 1U);
    CHECK(proof.transmitted_packet_count_after_cleanup_checks ==
        proof.transmitted_packet_count_at_success);
}

TEST_CASE(
    "Fake-HLDS cleanup continues into bounded synthetic entity playback",
    "[entity-render][integration][fake-hlds][cpu][repeat-20]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl",
        hlclient::tests::literal_minimal_goldsrc_studio_v10());
    root.write("valve", "sprites/test.spr",
        sprite_fixture::literal_single_sprite());
    root.write("valve", "tempdecal.wad", "synthetic-wad-placeholder");
    const auto root_before = snapshot_test_root(root.path());
    const auto sources = make_local_sources(root);
    const auto world = make_world_package();

    const auto exercise = [&](const PlaybackKind kind,
                              const std::size_t expected_studio,
                              const std::size_t expected_sprites,
                              const std::uint64_t resource_id) {
        std::optional<std::uint64_t> signature;
        for (std::size_t run = 0U; run < 20U; ++run) {
            INFO("runtime entity CPU playback " << run + 1U << "/20");
            const auto network = hlclient::test_support::
                acquire_entity_snapshot_happy_route_proof();
            require_network_proof(network, 1U);
            auto prepared = prepare_playback(
                network.snapshot_history, sources, kind, resource_id);
            const auto frame = build_playback_frame(prepared, 0.5, 1U);
            CHECK(prepared.exact_import_count ==
                (kind == PlaybackKind::mixed ? 2U : 1U));
            CHECK(frame.interpolated->statistics().added_count == 1U);
            CHECK(frame.interpolated->statistics().removed_count == 1U);
            CHECK(frame.entity_one_position_x == Approx(1.0F));
            if (expected_studio != 0U) {
                CHECK(frame.entity_one_pose_translation_x ==
                    Approx(1.0F).margin(0.001F));
            }
            CHECK(frame.render_frame->statistics().studio_instance_count ==
                expected_studio);
            CHECK(frame.render_frame->statistics().sprite_instance_count ==
                expected_sprites);
            CHECK(frame.sprite_selection_count == expected_sprites);
            require_client_and_null_composition(prepared, frame, world);
            if (signature) {
                CHECK(frame.render_frame->frame_signature() == *signature);
            } else {
                signature = frame.render_frame->frame_signature();
            }
            CHECK(snapshot_test_root(root.path()) == root_before);
        }
    };

    exercise(PlaybackKind::studio_only, 2U, 0U, 0x4531'0001U);
    exercise(PlaybackKind::sprite_only, 0U, 2U, 0x4531'0002U);
    exercise(PlaybackKind::mixed, 1U, 1U, 0x4531'0003U);

    std::optional<std::uint64_t> exact_current_signature;
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("runtime entity add-remove playback " << run + 1U << "/20");
        const auto network =
            hlclient::test_support::acquire_entity_snapshot_happy_route_proof();
        require_network_proof(network, 1U);
        auto prepared = prepare_playback(network.snapshot_history,
            sources,
            PlaybackKind::mixed,
            0x4531'0004U);
        const auto frame = build_playback_frame(prepared, 1.0, 2U);
        CHECK(frame.interpolated->selection_status() ==
            goldsrc::EntitySnapshotPairSelectionStatus::exact_current);
        CHECK(frame.interpolated->statistics().added_count == 1U);
        CHECK(frame.interpolated->statistics().removed_count == 1U);
        REQUIRE(frame.interpolated->entities().size() == 2U);
        CHECK(frame.interpolated->entities()[0U].entity_number() == 1U);
        CHECK(frame.interpolated->entities()[1U].entity_number() == 3U);
        CHECK(frame.render_frame->statistics().studio_instance_count == 1U);
        CHECK(frame.render_frame->statistics().sprite_instance_count == 1U);
        require_client_and_null_composition(prepared, frame, world);
        if (exact_current_signature) {
            CHECK(frame.render_frame->frame_signature() ==
                *exact_current_signature);
        } else {
            exact_current_signature = frame.render_frame->frame_signature();
        }
        CHECK(snapshot_test_root(root.path()) == root_before);
    }

    const auto exercise_recovered_transport = [&]<typename AcquireProof>(
                                                  const char* const label,
                                                  const std::uint64_t
                                                      resource_id,
                                                  AcquireProof&&
                                                      acquire_proof) {
        std::optional<std::uint64_t> midpoint_signature;
        std::optional<std::uint64_t> current_signature;
        for (std::size_t run = 0U; run < 20U; ++run) {
            INFO(label << " " << run + 1U << "/20");
            const auto recovered = acquire_proof(run);
            require_network_proof(recovered, 2U);

            auto prepared = prepare_playback(
                recovered.snapshot_history,
                sources,
                PlaybackKind::mixed,
                resource_id);
            CHECK(prepared.exact_import_count == 2U);
            const auto midpoint = build_playback_frame(prepared, 0.5, 1U);
            const auto current = build_playback_frame(prepared, 1.0, 2U);
            CHECK(prepared.exact_import_count == 2U);
            CHECK(midpoint.interpolated->statistics().added_count == 1U);
            CHECK(midpoint.interpolated->statistics().removed_count == 1U);
            CHECK(midpoint.entity_one_position_x == Approx(1.0F));
            CHECK(midpoint.entity_one_pose_translation_x ==
                Approx(1.0F).margin(0.001F));
            CHECK(midpoint.render_frame->statistics().studio_instance_count ==
                1U);
            CHECK(midpoint.render_frame->statistics().sprite_instance_count ==
                1U);
            CHECK(current.interpolated->selection_status() ==
                goldsrc::EntitySnapshotPairSelectionStatus::exact_current);
            REQUIRE(current.interpolated->entities().size() == 2U);
            CHECK(current.interpolated->entities()[0U].entity_number() == 1U);
            CHECK(current.interpolated->entities()[1U].entity_number() == 3U);
            CHECK(current.render_frame->statistics().studio_instance_count ==
                1U);
            CHECK(current.render_frame->statistics().sprite_instance_count ==
                1U);
            CHECK(midpoint.render_frame->resource_id() ==
                current.render_frame->resource_id());
            CHECK(midpoint.render_frame->resource_revision() == 1U);
            CHECK(current.render_frame->resource_revision() == 2U);
            require_client_and_null_composition(prepared, midpoint, world);
            require_client_and_null_composition(prepared, current, world);

            if (midpoint_signature) {
                CHECK(midpoint.render_frame->frame_signature() ==
                    *midpoint_signature);
                CHECK(current.render_frame->frame_signature() ==
                    *current_signature);
            } else {
                midpoint_signature = midpoint.render_frame->frame_signature();
                current_signature = current.render_frame->frame_signature();
            }
            CHECK(snapshot_test_root(root.path()) == root_before);
        }
    };

    exercise_recovered_transport(
        "runtime entity dropped-request recovery",
        0x4531'0005U,
        [](const std::size_t run) {
            return hlclient::test_support::
                acquire_entity_snapshot_dropped_request_route_proof(run);
        });
    exercise_recovered_transport(
        "runtime entity dropped-ACK recovery",
        0x4531'0006U,
        [](const std::size_t run) {
            return hlclient::test_support::
                acquire_entity_snapshot_dropped_acknowledgement_route_proof(
                    run);
        });
    CHECK(snapshot_test_root(root.path()) == root_before);
}

TEST_CASE(
    "Fake-HLDS playback renders world Studio and Sprite across two OpenGL frames",
    "[entity-render][integration][fake-hlds][opengl][actual-context]")
{
    auto context = opengl_fixture::try_context();
    if (!context || !opengl_fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto network =
        hlclient::test_support::acquire_entity_snapshot_happy_route_proof();
    REQUIRE(network.snapshot_history);
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl",
        hlclient::tests::literal_minimal_goldsrc_studio_v10());
    root.write("valve", "sprites/test.spr",
        sprite_fixture::literal_single_sprite());
    root.write("valve", "tempdecal.wad", "synthetic-wad-placeholder");
    const auto root_before = snapshot_test_root(root.path());
    const auto sources = make_local_sources(root);
    const auto world = make_world_package();
    auto prepared = prepare_playback(network.snapshot_history,
        sources,
        PlaybackKind::mixed,
        0x4532'0001U);
    const auto first = build_playback_frame(prepared, 0.5, 1U);
    const auto second = build_playback_frame(prepared, 1.0, 2U);

    context->initialize_renderer();
    client::ClientWorldState state;
    state.set_static_world(world);
    state.set_camera({{0.0F, -12.0F, 3.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, 1.0F},
        1.0471975512F,
        0.1F,
        128.0F});
    REQUIRE(state.set_dynamic_entities(prepared.package, first.render_frame));
    context->renderer().render(client::build_render_scene(state), {96, 96});
    const auto first_pixels = opengl_fixture::framebuffer();
    const auto first_world = context->renderer().statistics();
    const auto first_entities = context->renderer().entity_statistics();
    CHECK(first_world.upload_count == 1U);
    CHECK(first_entities.studio_asset_upload_count == 1U);
    CHECK(first_entities.sprite_asset_upload_count == 1U);
    CHECK(first_entities.studio_draw_count > 0U);
    CHECK(first_entities.sprite_draw_count > 0U);
    CHECK(first_entities.entity_frame_revision == 1U);
    CHECK(opengl_fixture::has_non_clear_pixel(first_pixels,
        {std::byte{9U}, std::byte{14U}, std::byte{22U}, std::byte{255U}}));

    REQUIRE(state.set_dynamic_entities(prepared.package, second.render_frame));
    context->renderer().render(client::build_render_scene(state), {96, 96});
    const auto second_world = context->renderer().statistics();
    const auto second_entities = context->renderer().entity_statistics();
    CHECK(second_world.upload_count == 1U);
    CHECK(second_entities.studio_asset_upload_count == 1U);
    CHECK(second_entities.sprite_asset_upload_count == 1U);
    CHECK(second_entities.entity_frame_revision == 2U);
    CHECK(second_entities.pose_ubo_update_count >
        first_entities.pose_ubo_update_count);
    CHECK(second_entities.studio_draw_count > first_entities.studio_draw_count);
    CHECK(second_entities.sprite_draw_count > first_entities.sprite_draw_count);
    CHECK(glGetError() == GL_NO_ERROR);

    const auto exercise_recovered_transport = [&]<typename AcquireProof>(
                                                  const char* const label,
                                                  AcquireProof&&
                                                      acquire_proof) {
        std::optional<std::uint64_t> midpoint_signature;
        std::optional<std::uint64_t> current_signature;
        for (std::size_t run = 0U; run < 20U; ++run) {
            INFO(label << " " << run + 1U << "/20");
            const auto recovered = acquire_proof(run);
            require_network_proof(recovered, 2U);
            auto recovered_playback = prepare_playback(
                recovered.snapshot_history,
                sources,
                PlaybackKind::mixed,
                0x4532'0001U);
            CHECK(recovered_playback.exact_import_count == 2U);
            const auto midpoint =
                build_playback_frame(recovered_playback, 0.5, 1U);
            const auto current =
                build_playback_frame(recovered_playback, 1.0, 2U);
            CHECK(recovered_playback.exact_import_count == 2U);
            CHECK(midpoint.interpolated->statistics().added_count == 1U);
            CHECK(midpoint.interpolated->statistics().removed_count == 1U);
            CHECK(current.interpolated->selection_status() ==
                goldsrc::EntitySnapshotPairSelectionStatus::exact_current);
            REQUIRE(current.interpolated->entities().size() == 2U);
            CHECK(current.interpolated->entities()[0U].entity_number() == 1U);
            CHECK(current.interpolated->entities()[1U].entity_number() == 3U);
            CHECK(current.render_frame->statistics().studio_instance_count ==
                1U);
            CHECK(current.render_frame->statistics().sprite_instance_count ==
                1U);

            REQUIRE(state.set_dynamic_entities(
                recovered_playback.package, midpoint.render_frame));
            context->renderer().render(
                client::build_render_scene(state), {96, 96});
            const auto midpoint_world = context->renderer().statistics();
            const auto midpoint_entities =
                context->renderer().entity_statistics();
            CHECK(midpoint_world.upload_count == 1U);
            CHECK(midpoint_entities.studio_asset_upload_count == 1U);
            CHECK(midpoint_entities.sprite_asset_upload_count == 1U);
            CHECK(midpoint_entities.entity_frame_revision == 1U);

            REQUIRE(state.set_dynamic_entities(
                recovered_playback.package, current.render_frame));
            context->renderer().render(
                client::build_render_scene(state), {96, 96});
            const auto current_world = context->renderer().statistics();
            const auto current_entities =
                context->renderer().entity_statistics();
            CHECK(current_world.upload_count == 1U);
            CHECK(current_entities.studio_asset_upload_count == 1U);
            CHECK(current_entities.sprite_asset_upload_count == 1U);
            CHECK(current_entities.entity_frame_revision == 2U);
            CHECK(current_entities.pose_ubo_update_count >
                midpoint_entities.pose_ubo_update_count);
            CHECK(current_entities.studio_draw_count >
                midpoint_entities.studio_draw_count);
            CHECK(current_entities.sprite_draw_count >
                midpoint_entities.sprite_draw_count);
            CHECK(glGetError() == GL_NO_ERROR);

            if (midpoint_signature) {
                REQUIRE(current_signature);
                CHECK(midpoint.render_frame->frame_signature() ==
                    *midpoint_signature);
                CHECK(current.render_frame->frame_signature() ==
                    *current_signature);
            } else {
                midpoint_signature = midpoint.render_frame->frame_signature();
                current_signature = current.render_frame->frame_signature();
            }
            CHECK(snapshot_test_root(root.path()) == root_before);
        }
    };

    exercise_recovered_transport(
        "OpenGL dropped-request entity playback",
        [](const std::size_t run) {
            return hlclient::test_support::
                acquire_entity_snapshot_dropped_request_route_proof(run);
        });
    exercise_recovered_transport(
        "OpenGL dropped-ACK entity playback",
        [](const std::size_t run) {
            return hlclient::test_support::
                acquire_entity_snapshot_dropped_acknowledgement_route_proof(
                    run);
        });

    const auto exercise_baseline_rendering = [&](const char* const label,
                                                  const PlaybackKind kind,
                                                  const std::size_t
                                                      expected_studio,
                                                  const std::size_t
                                                      expected_sprites,
                                                  const std::uint64_t
                                                      resource_id) {
        const auto uploads_before = context->renderer().entity_statistics();
        const auto expected_studio_uploads =
            uploads_before.studio_asset_upload_count +
            (kind == PlaybackKind::sprite_only ? 0U : 1U);
        const auto expected_sprite_uploads =
            uploads_before.sprite_asset_upload_count +
            (kind == PlaybackKind::studio_only ? 0U : 1U);
        std::optional<std::uint64_t> midpoint_signature;
        std::optional<std::uint64_t> current_signature;
        for (std::size_t run = 0U; run < 20U; ++run) {
            INFO(label << " " << run + 1U << "/20");
            const auto completed = hlclient::test_support::
                acquire_entity_snapshot_happy_route_proof();
            require_network_proof(completed, 1U);
            auto playback = prepare_playback(
                completed.snapshot_history, sources, kind, resource_id);
            CHECK(playback.exact_import_count ==
                (kind == PlaybackKind::mixed ? 2U : 1U));
            const auto midpoint = build_playback_frame(playback, 0.5, 1U);
            const auto current = build_playback_frame(playback, 1.0, 2U);
            CHECK(midpoint.interpolated->statistics().added_count == 1U);
            CHECK(midpoint.interpolated->statistics().removed_count == 1U);
            CHECK(midpoint.entity_one_position_x == Approx(1.0F));
            if (expected_studio != 0U) {
                CHECK(midpoint.entity_one_pose_translation_x ==
                    Approx(1.0F).margin(0.001F));
            }
            CHECK(midpoint.render_frame->statistics().studio_instance_count ==
                expected_studio);
            CHECK(midpoint.render_frame->statistics().sprite_instance_count ==
                expected_sprites);
            CHECK(current.interpolated->selection_status() ==
                goldsrc::EntitySnapshotPairSelectionStatus::exact_current);
            REQUIRE(current.interpolated->entities().size() == 2U);
            CHECK(current.interpolated->entities()[0U].entity_number() == 1U);
            CHECK(current.interpolated->entities()[1U].entity_number() == 3U);
            CHECK(current.render_frame->statistics().studio_instance_count ==
                expected_studio);
            CHECK(current.render_frame->statistics().sprite_instance_count ==
                expected_sprites);

            REQUIRE(state.set_dynamic_entities(
                playback.package, midpoint.render_frame));
            context->renderer().render(
                client::build_render_scene(state), {96, 96});
            const auto midpoint_world = context->renderer().statistics();
            const auto midpoint_entities =
                context->renderer().entity_statistics();
            CHECK(midpoint_world.upload_count == 1U);
            CHECK(midpoint_entities.studio_asset_upload_count ==
                expected_studio_uploads);
            CHECK(midpoint_entities.sprite_asset_upload_count ==
                expected_sprite_uploads);
            CHECK(midpoint_entities.entity_frame_revision == 1U);

            REQUIRE(state.set_dynamic_entities(
                playback.package, current.render_frame));
            context->renderer().render(
                client::build_render_scene(state), {96, 96});
            const auto current_world = context->renderer().statistics();
            const auto current_entities =
                context->renderer().entity_statistics();
            CHECK(current_world.upload_count == 1U);
            CHECK(current_entities.studio_asset_upload_count ==
                expected_studio_uploads);
            CHECK(current_entities.sprite_asset_upload_count ==
                expected_sprite_uploads);
            CHECK(current_entities.entity_frame_revision == 2U);
            if (expected_studio != 0U) {
                CHECK(current_entities.pose_ubo_update_count >
                    midpoint_entities.pose_ubo_update_count);
                CHECK(current_entities.studio_draw_count >
                    midpoint_entities.studio_draw_count);
            }
            if (expected_sprites != 0U) {
                CHECK(current_entities.sprite_draw_count >
                    midpoint_entities.sprite_draw_count);
            }
            CHECK(glGetError() == GL_NO_ERROR);

            if (midpoint_signature) {
                REQUIRE(current_signature);
                CHECK(midpoint.render_frame->frame_signature() ==
                    *midpoint_signature);
                CHECK(current.render_frame->frame_signature() ==
                    *current_signature);
            } else {
                midpoint_signature = midpoint.render_frame->frame_signature();
                current_signature = current.render_frame->frame_signature();
            }
            CHECK(snapshot_test_root(root.path()) == root_before);
        }
    };

    exercise_baseline_rendering(
        "OpenGL Studio-only entity playback",
        PlaybackKind::studio_only,
        2U,
        0U,
        0x4532'0002U);
    exercise_baseline_rendering(
        "OpenGL Sprite-only entity playback",
        PlaybackKind::sprite_only,
        0U,
        2U,
        0x4532'0003U);
    exercise_baseline_rendering(
        "OpenGL mixed entity playback",
        PlaybackKind::mixed,
        1U,
        1U,
        0x4532'0004U);
    exercise_baseline_rendering(
        "OpenGL interpolation add-remove playback",
        PlaybackKind::mixed,
        1U,
        1U,
        0x4532'0005U);

    state.clear_dynamic_entities();
    context->renderer().render(client::build_render_scene(state), {96, 96});
    CHECK_FALSE(context->renderer().entity_statistics().active_entity_resources);
    CHECK(context->renderer().statistics().upload_count == 1U);
    context->release_renderer();
    CHECK(snapshot_test_root(root.path()) == root_before);
}

} // namespace
