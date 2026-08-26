#include "entity_render/entity_render_test_fixture.hpp"

#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace render = hlclient::entity_render;
namespace visual = hlclient::entity_visual;
namespace goldsrc = hlclient::goldsrc;
namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests::entity_render_fixture;
namespace visual_fixture = hlclient::tests::entity_visual_fixture;
using hlclient::tests::ScopedLocalResourceTestRoot;

struct ComposerPackage {
    std::shared_ptr<const assets::ModelAsset> model;
    std::shared_ptr<const assets::SpriteAsset> sprite;
    std::shared_ptr<const visual::EntityVisualAssetLibraryState> library;
    std::shared_ptr<const render::EntitySceneRenderPackage> package;
};

[[nodiscard]] std::shared_ptr<const assets::ModelAsset> animated_model()
{
    const auto base = fixture::model_asset({0U, 0U});
    REQUIRE(base->skeletal_data);
    auto skeletal =
        std::make_shared<assets::SkeletalModelAssetData>(*base->skeletal_data);
    skeletal->sequence_groups = {{"composer", {}, 0U, false}};
    skeletal->sequences.clear();

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
    track.channels[0U].runs.push_back({0U, 2U, 2U, {0, 2}});
    assets::ModelAnimationBlend blend;
    blend.source_blend_ordinal = 0U;
    blend.bone_tracks.push_back(std::move(track));
    assets::ModelSequence sequence;
    sequence.label = "composer";
    sequence.frames_per_second = 30.0F;
    sequence.frame_count = 2U;
    sequence.blend_count = 1U;
    sequence.motion_bone = 0;
    sequence.sequence_group_index = 0U;
    sequence.animation_blends.push_back(std::move(blend));
    skeletal->sequences.push_back(std::move(sequence));

    auto output = std::make_shared<assets::ModelAsset>(*base);
    output->identity.source_name = "composer-studio";
    output->skeletal_data = std::move(skeletal);
    return output;
}

[[nodiscard]] ComposerPackage make_package(
    std::shared_ptr<const assets::ModelAsset> model,
    std::shared_ptr<const assets::SpriteAsset> sprite)
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    std::vector<resource_list_test_fixture::EntrySpec> entries{
        {2U, "maps/test_map.bsp", 9U, 3U, 0U}};
    std::vector<std::uint32_t> entity_numbers;
    std::vector<visual::SyntheticEntityVisualInput> inputs;
    if (model) {
        root.write("valve", "models/composer.mdl", "model");
        entries.push_back({2U, "models/composer.mdl", 1U, 5U, 0U});
        entity_numbers.push_back(1U);
        visual::SyntheticEntityVisualInput input;
        input.entity_number = 1U;
        input.model_reference =
            visual::EntityVisualModelReference::synthetic_model_slot(1U);
        inputs.push_back(input);
    }
    if (sprite) {
        root.write("valve", "sprites/composer.spr", "sprite");
        entries.push_back({2U, "sprites/composer.spr", 2U, 6U, 0U});
        entity_numbers.push_back(2U);
        visual::SyntheticEntityVisualInput input;
        input.entity_number = 2U;
        input.model_reference =
            visual::EntityVisualModelReference::synthetic_model_slot(2U);
        inputs.push_back(input);
    }
    auto resources = visual_fixture::manifest(root, entries);
    const auto snapshot = visual_fixture::synthetic_snapshot(entity_numbers);
    const auto projections = visual_fixture::project(snapshot, std::move(inputs));
    visual::SyntheticModelSlotResolver resolver;
    visual::EntityVisualAssetLibraryBuilder library_builder;
    auto planned = library_builder.plan(
        0xC001U, {}, projections, resources.manifest, resolver);
    INFO((planned.error ? planned.error->context : std::string{}));
    REQUIRE(planned);
    REQUIRE(planned.plan);
    std::vector<visual::EntityVisualAssetImportCompletion> completions;
    for (const auto& request : planned.plan->requests()) {
        if (request.model_slot() == 1U) {
            auto candidate =
                visual::EntityVisualImportedAssetCandidate::studio_model(
                    request.source_key(),
                    model,
                    "model:composer",
                    request.source_key().main_source_byte_count(),
                    {{0xC001U, 0x1001U}});
            completions.push_back({request.request_index(),
                visual::EntityVisualAssetImportCompletionStatus::imported,
                std::move(candidate)});
        } else {
            REQUIRE(request.model_slot() == 2U);
            auto candidate =
                visual::EntityVisualImportedAssetCandidate::sprite(
                    request.source_key(),
                    sprite,
                    "sprite:composer",
                    request.source_key().main_source_byte_count(),
                    {{0xC001U, 0x2001U}});
            completions.push_back({request.request_index(),
                visual::EntityVisualAssetImportCompletionStatus::imported,
                std::move(candidate)});
        }
    }
    auto published = library_builder.publish(*planned.plan, completions);
    INFO((published.error ? published.error->context : std::string{}));
    REQUIRE(published);
    REQUIRE(published.library);

    render::EntitySceneRenderPackageCreateInfo scene_input;
    scene_input.asset_library = published.library;
    scene_input.asset_library_identity = {
        published.library->resource_id(), published.library->resource_revision()};
    scene_input.resource_id = 0xC002U;
    for (const auto& record : published.library->records()) {
        const render::EntityRenderResourceIdentity identity{
            record.resource_id(), record.resource_revision()};
        if (record.kind() == visual::EntityVisualAssetKind::studio_model) {
            auto built = render::StudioModelRenderAssetBuilder{}.build(
                *record.model_asset(), identity);
            INFO((built.error ? built.error->context : std::string{}));
            REQUIRE(built);
            scene_input.studio_assets.push_back(
                std::make_shared<const render::StudioModelRenderAsset>(
                    std::move(*built.asset)));
        } else {
            auto built = render::SpriteRenderAssetBuilder{}.build(
                *record.sprite_asset(), identity);
            INFO((built.error ? built.error->context : std::string{}));
            REQUIRE(built);
            scene_input.sprite_assets.push_back(
                std::make_shared<const render::SpriteRenderAsset>(
                    std::move(*built.asset)));
        }
    }
    auto scene = render::EntitySceneRenderPackageBuilder{}.build(
        std::move(scene_input));
    INFO((scene.error ? scene.error->context : std::string{}));
    REQUIRE(scene);
    return {std::move(model),
        std::move(sprite),
        std::move(published.library),
        std::make_shared<const render::EntitySceneRenderPackage>(
            std::move(*scene.package))};
}

[[nodiscard]] goldsrc::InterpolatedEntityFrame make_frame(
    std::vector<visual::SyntheticEntityVisualInput> inputs,
    const double sample_seconds = 0.5)
{
    std::vector<std::uint32_t> entity_numbers;
    entity_numbers.reserve(inputs.size());
    for (const auto& input : inputs) {
        entity_numbers.push_back(input.entity_number);
    }
    const auto snapshot =
        visual_fixture::synthetic_snapshot(entity_numbers, 17U);
    const auto projections =
        visual_fixture::project(snapshot, std::move(inputs));
    const auto adapted = goldsrc::EntityInterpolationProjectionAdapter{}.build(
        snapshot, projections);
    INFO((adapted.error ? adapted.error->context : std::string{}));
    REQUIRE(adapted);
    REQUIRE(adapted.frame);
    goldsrc::EntitySnapshotPairSelection selection;
    selection.previous = &snapshot;
    selection.current = &snapshot;
    selection.previous_seconds = sample_seconds;
    selection.current_seconds = sample_seconds;
    selection.target_seconds = sample_seconds;
    selection.alpha = 0.0;
    selection.status = goldsrc::EntitySnapshotPairSelectionStatus::held_only;
    auto interpolated = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        selection, adapted.frame->view(), adapted.frame->view());
    INFO((interpolated.error ? interpolated.error->context : std::string{}));
    REQUIRE(interpolated);
    REQUIRE(interpolated.frame);
    return std::move(*interpolated.frame);
}

[[nodiscard]] visual::SyntheticEntityVisualInput studio_input(
    const std::uint32_t entity_number,
    const std::uint32_t model_slot = 1U)
{
    visual::SyntheticEntityVisualInput input;
    input.entity_number = entity_number;
    input.model_reference =
        visual::EntityVisualModelReference::synthetic_model_slot(model_slot);
    input.origin = visual::EntityVisualVector3{
        static_cast<float>(entity_number), 2.0F, 3.0F};
    input.angles_degrees = visual::EntityVisualVector3{10.0F, 20.0F, 30.0F};
    input.sequence_index = 0U;
    input.studio_frame_coordinate = 0.5F;
    input.body_value = 0U;
    input.skin_family_index = 0U;
    input.scale = 1.5F;
    return input;
}

[[nodiscard]] visual::SyntheticEntityVisualInput sprite_input(
    const visual::EntityVisualRenderMode mode =
        visual::EntityVisualRenderMode::source_asset_default,
    const std::uint32_t top_level_entry = 0U)
{
    visual::SyntheticEntityVisualInput input;
    input.entity_number = 2U;
    input.model_reference =
        visual::EntityVisualModelReference::synthetic_model_slot(2U);
    input.origin = visual::EntityVisualVector3{4.0F, 5.0F, 6.0F};
    input.sprite_frame_index = top_level_entry;
    input.render_mode = mode;
    input.animation_start_time_seconds = 0.0;
    return input;
}

[[nodiscard]] render::EntityRenderFrameCompositionInput composition_input(
    const render::EntitySceneRenderPackage& package,
    const std::uint64_t revision = 1U,
    const double sample_seconds = 0.5)
{
    return {
        {package.resource_id(), package.resource_revision()},
        {0xC003U, revision},
        sample_seconds,
        sample_seconds,
        nullptr,
        nullptr,
        std::nullopt,
    };
}

TEST_CASE(
    "Entity frame composer shares exact Studio poses and derives finite bounds",
    "[entity-render][composer][studio][cache]")
{
    const auto assets = make_package(animated_model(), {});
    const auto frame = make_frame({studio_input(1U), studio_input(2U)});
    studio::StudioPoseCache cache;
    const auto composed = render::EntityRenderFrameComposer{}.compose(
        *assets.package,
        frame,
        composition_input(*assets.package),
        cache);
    INFO((composed.error ? composed.error->context : std::string{}));
    REQUIRE(composed);
    REQUIRE(composed.frame);
    REQUIRE(composed.frame->studio_instances().size() == 2U);
    REQUIRE(composed.frame->studio_poses().size() == 1U);
    CHECK(composed.frame->studio_instances()[0U].pose_index == 0U);
    CHECK(composed.frame->studio_instances()[1U].pose_index == 0U);
    CHECK(render::finite_entity_render_bounds(
        composed.frame->studio_instances()[0U].interpolated_bounds));
    CHECK(render::finite_entity_render_bounds(
        composed.frame->studio_instances()[1U].interpolated_bounds));
    CHECK(cache.statistics().instance_request_count == 2U);
    CHECK(cache.statistics().entry_count == 1U);
    CHECK(cache.statistics().cache_miss_count == 1U);
    CHECK(cache.statistics().cache_hit_count == 1U);
    CHECK(composed.frame->scene_package_identity() ==
        render::EntityRenderResourceIdentity{
            assets.package->resource_id(), assets.package->resource_revision()});
}

TEST_CASE(
    "Entity frame composer rejects package mismatch and retains missing assets atomically",
    "[entity-render][composer][identity][missing]")
{
    const auto assets = make_package(animated_model(), {});

    SECTION("package identity mismatch")
    {
        const auto frame = make_frame({studio_input(1U)});
        auto input = composition_input(*assets.package);
        ++input.expected_scene_package_identity.revision;
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *assets.package, frame, input, cache);
        REQUIRE_FALSE(composed);
        CHECK_FALSE(composed.frame);
        REQUIRE(composed.error);
        CHECK(composed.error->code ==
            render::EntityRenderFrameComposerErrorCode::
                scene_package_mismatch);
        CHECK(cache.statistics().frame_token == 0U);
    }

    SECTION("composition pair times mismatch")
    {
        const auto frame = make_frame({studio_input(1U)});
        auto input = composition_input(*assets.package);
        input.current_time_seconds = 0.75;
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *assets.package, frame, input, cache);
        REQUIRE_FALSE(composed);
        CHECK_FALSE(composed.frame);
        REQUIRE(composed.error);
        CHECK(composed.error->code ==
            render::EntityRenderFrameComposerErrorCode::
                invalid_configuration);
        CHECK(cache.statistics().frame_token == 0U);
    }

    SECTION("missing typed library reference")
    {
        const auto frame = make_frame({studio_input(1U, 3U)});
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *assets.package,
            frame,
            composition_input(*assets.package),
            cache);
        INFO((composed.error ? composed.error->context : std::string{}));
        REQUIRE(composed);
        REQUIRE(composed.frame);
        REQUIRE(composed.frame->unsupported_instances().size() == 1U);
        CHECK(composed.frame->unsupported_instances()[0U].reason ==
            render::UnsupportedEntityVisualReason::missing_asset);
        CHECK_FALSE(composed.frame->unsupported_instances()[0U].visual_asset_index);
        CHECK(composed.frame->unsupported_instances()[0U].visibility_status ==
            render::RuntimeEntityVisibilityStatus::asset_unavailable);
        CHECK(composed.frame->statistics().unavailable_count == 1U);
        CHECK(cache.statistics().instance_request_count == 0U);
    }
}

TEST_CASE(
    "Entity frame composer propagates typed Studio pose failures without a frame",
    "[entity-render][composer][studio][pose-error]")
{
    const auto assets = make_package(fixture::model_asset({0U, 0U}), {});
    const auto frame = make_frame({studio_input(1U)});
    studio::StudioPoseCache cache;
    const auto composed = render::EntityRenderFrameComposer{}.compose(
        *assets.package,
        frame,
        composition_input(*assets.package),
        cache);
    REQUIRE_FALSE(composed);
    CHECK_FALSE(composed.frame);
    REQUIRE(composed.error);
    CHECK(composed.error->code ==
        render::EntityRenderFrameComposerErrorCode::studio_pose_failed);
    REQUIRE(composed.error->studio_pose_error);
    CHECK(*composed.error->studio_pose_error ==
        studio::StudioPoseErrorCode::invalid_sequence);
}

TEST_CASE(
    "Entity frame composer retains unsupported render profiles as typed metadata",
    "[entity-render][composer][unsupported][sprite]")
{
    SECTION("supported alpha-test render mode")
    {
        const auto package_assets = make_package({},
            fixture::sprite_asset(assets::SpriteTextureFormat::alpha_test));
        const auto frame = make_frame(
            {sprite_input(visual::EntityVisualRenderMode::alpha_test)});
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *package_assets.package,
            frame,
            composition_input(*package_assets.package),
            cache);
        INFO((composed.error ? composed.error->context : std::string{}));
        REQUIRE(composed);
        REQUIRE(composed.frame);
        REQUIRE(composed.frame->sprite_instances().size() == 1U);
        CHECK(composed.frame->unsupported_instances().empty());
        REQUIRE(composed.frame->draw_commands().size() == 1U);
        CHECK(composed.frame->draw_commands()[0U].draw_class ==
            render::EntityDrawClass::sprite_alpha_test);
    }

    SECTION("additive render mode")
    {
        const auto assets = make_package({}, fixture::sprite_asset());
        const auto frame = make_frame(
            {sprite_input(visual::EntityVisualRenderMode::additive)});
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *assets.package,
            frame,
            composition_input(*assets.package),
            cache);
        INFO((composed.error ? composed.error->context : std::string{}));
        REQUIRE(composed);
        REQUIRE(composed.frame);
        CHECK(composed.frame->sprite_instances().empty());
        REQUIRE(composed.frame->unsupported_instances().size() == 1U);
        CHECK(composed.frame->unsupported_instances()[0U].reason ==
            render::UnsupportedEntityVisualReason::unsupported_render_mode);
        CHECK(composed.frame->draw_commands().empty());
    }

    SECTION("unsupported index-alpha source format")
    {
        const auto package_assets = make_package({},
            fixture::sprite_asset(assets::SpriteTextureFormat::index_alpha));
        const auto frame = make_frame({sprite_input()});
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *package_assets.package,
            frame,
            composition_input(*package_assets.package),
            cache);
        INFO((composed.error ? composed.error->context : std::string{}));
        REQUIRE(composed);
        REQUIRE(composed.frame);
        REQUIRE(composed.frame->unsupported_instances().size() == 1U);
        CHECK(composed.frame->unsupported_instances()[0U].reason ==
            render::UnsupportedEntityVisualReason::unsupported_sprite_format);
    }

    SECTION("unsupported billboard orientation")
    {
        const auto package_assets = make_package({},
            fixture::sprite_asset(assets::SpriteTextureFormat::normal,
                assets::SpriteSyncType::synchronized,
                assets::SpriteOrientation::facing_upright));
        const auto frame = make_frame({sprite_input()});
        studio::StudioPoseCache cache;
        const auto composed = render::EntityRenderFrameComposer{}.compose(
            *package_assets.package,
            frame,
            composition_input(*package_assets.package),
            cache);
        INFO((composed.error ? composed.error->context : std::string{}));
        REQUIRE(composed);
        REQUIRE(composed.frame);
        CHECK(composed.frame->sprite_instances().empty());
        REQUIRE(composed.frame->unsupported_instances().size() == 1U);
        CHECK(composed.frame->unsupported_instances()[0U].reason ==
            render::UnsupportedEntityVisualReason::
                unsupported_sprite_orientation);
        CHECK(composed.frame->draw_commands().empty());
    }
}

TEST_CASE(
    "Entity frame composer propagates typed Sprite selection failures",
    "[entity-render][composer][sprite][selection-error]")
{
    const auto assets = make_package({}, fixture::sprite_asset());
    const auto frame = make_frame({sprite_input(
        visual::EntityVisualRenderMode::source_asset_default, 99U)});
    studio::StudioPoseCache cache;
    const auto composed = render::EntityRenderFrameComposer{}.compose(
        *assets.package,
        frame,
        composition_input(*assets.package),
        cache);
    REQUIRE_FALSE(composed);
    CHECK_FALSE(composed.frame);
    REQUIRE(composed.error);
    CHECK(composed.error->code ==
        render::EntityRenderFrameComposerErrorCode::sprite_selection_failed);
    REQUIRE(composed.error->sprite_playback_error);
    CHECK(*composed.error->sprite_playback_error ==
        goldsrc::sprite::SpritePlaybackErrorCode::invalid_entry);
}

} // namespace
