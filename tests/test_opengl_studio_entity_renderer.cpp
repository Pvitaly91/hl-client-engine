#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
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
namespace studio_pose = hlclient::goldsrc::studio;
namespace assets = hlclient::assets;

// Project-owned lower/upper geometry with numeric bone assignments; no game
// model, bone-name policy or external resource is needed by this pixel proof.
[[nodiscard]] std::shared_ptr<const assets::ModelAsset> e10_three_bone_model()
{
    auto model = std::make_shared<assets::ModelAsset>(
        *hlclient::tests::entity_render_fixture::model_asset({0U, 0U}, true));
    auto skeleton = std::make_shared<assets::SkeletalModelAssetData>(
        *model->skeletal_data);
    skeleton->bones.resize(3U);
    skeleton->bones[0U].parent_index = -1;
    skeleton->bones[1U].parent_index = 0;
    skeleton->bones[2U].parent_index = 0;
    skeleton->bodyparts = {{"fixture", 1, {0U}}};
    skeleton->submodels.resize(1U);
    auto& mesh = skeleton->submodels[0U];
    mesh.vertices.clear();
    mesh.indices.clear();
    for (std::uint8_t bone = 1U; bone <= 2U; ++bone) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto position : std::array<assets::AssetVector3, 3U>{
                 assets::AssetVector3{-0.4F, 0.0F, 0.0F},
                 assets::AssetVector3{0.4F, 0.0F, 0.0F},
                 assets::AssetVector3{0.0F, 1.0F, 0.0F}}) {
            assets::ModelSkinnedVertex vertex;
            vertex.source_position = position;
            vertex.source_normal = {0.0F, 0.0F, 1.0F};
            vertex.position_bone_index = bone;
            vertex.normal_bone_index = bone;
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.insert(mesh.indices.end(), {first, first + 1U, first + 2U,
            first, first + 2U, first + 1U});
    }
    mesh.meshes = {{0U, 12U, 0U, 0U, 4U, 0U, 0U}};
    mesh.bounds = {{-2.0F, -1.0F, -0.1F}, {2.0F, 4.0F, 0.1F}};
    skeleton->source_clipping_bounds = mesh.bounds;
    skeleton->sequence_groups = {{"fixture", {}, 0U, false}};
    skeleton->sequences.clear();
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        assets::ModelSequence sequence;
        sequence.label = "fixture-" + std::to_string(index);
        sequence.frames_per_second = 10.0F;
        sequence.frame_count = 2U;
        sequence.blend_count = 1U;
        sequence.motion_bone = 0;
        assets::ModelAnimationBlend blend;
        for (std::uint32_t bone = 0U; bone < 3U; ++bone) {
            assets::ModelBoneAnimationTrack track;
            track.bone_index = bone;
            for (std::size_t axis = 0U; axis < track.channels.size(); ++axis) {
                track.channels[axis].semantic =
                    static_cast<assets::ModelAnimationChannelSemantic>(axis);
                track.channels[axis].frame_coverage = 2U;
            }
            if (bone == 1U) track.channels[0U].source_default =
                index == 0U ? -0.9F : 0.9F;
            if (bone == 2U) {
                track.channels[1U].source_default = 1.8F;
                track.channels[0U].source_default = static_cast<float>(index) * 0.6F;
            }
            blend.bone_tracks.push_back(std::move(track));
        }
        sequence.animation_blends.push_back(std::move(blend));
        skeleton->sequences.push_back(std::move(sequence));
    }
    model->skeletal_data = std::move(skeleton);
    return model;
}

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame> e10_pose_frame(
    const entity::EntitySceneRenderPackage& package,
    const studio_pose::StudioPoseState& pose,
    const std::uint64_t revision,
    const std::array<float, 3U> light,
    const entity::EntityRenderTransform transform = {
        {}, {90.0F, 0.0F, 0.0F}, 3.0F})
{
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0xE100U;
    input.resource_revision = revision;
    input.interpolation = {0.5, 0.0, 1.0, 0.5F, revision, revision + 1U,
        entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    entity::StudioRenderPose render_pose;
    render_pose.model_resource_identity = package.studio_assets()[0U]->source_identity();
    for (const auto& bone : pose.world_bones()) {
        const auto& m = bone.transform.values;
        render_pose.bone_matrices.push_back({
            m[0U], m[4U], m[8U], 0.0F,
            m[1U], m[5U], m[9U], 0.0F,
            m[2U], m[6U], m[10U], 0.0F,
            m[3U], m[7U], m[11U], 1.0F});
    }
    input.studio_poses.push_back(std::move(render_pose));
    entity::StudioEntityRenderInstance instance;
    instance.entity_number = 1U;
    instance.transform = transform;
    instance.interpolated_bounds = {{-8.0F, -8.0F, -8.0F}, {16.0F, 16.0F, 16.0F}};
    instance.static_light_rgb = light;
    input.studio_instances.push_back(instance);
    auto built = entity::EntityRenderFrameBuilder{}.build(package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const entity::EntityRenderFrame>(std::move(*built.frame));
}

[[nodiscard]] std::uint64_t e10_rgb_sum(const std::vector<std::byte>& pixels)
{
    std::uint64_t result = 0U;
    for (std::size_t pixel = 0U; pixel < pixels.size(); pixel += 4U) {
        result += std::to_integer<std::uint8_t>(pixels[pixel]);
        result += std::to_integer<std::uint8_t>(pixels[pixel + 1U]);
        result += std::to_integer<std::uint8_t>(pixels[pixel + 2U]);
    }
    return result;
}

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame>
single_studio_frame(
    const entity::EntitySceneRenderPackage& package,
    const std::uint64_t revision,
    const hlclient::assets::AssetVector3 origin,
    const hlclient::assets::AssetVector3 rotation,
    const float scale,
    const hlclient::assets::WorldBounds bounds,
    const std::uint32_t skin_family_index = 0U,
    const visibility::WorldViewFrustum* view_frustum = nullptr,
    const spatial::WorldSpatialPackage* spatial_package = nullptr,
    const std::optional<std::uint32_t> camera_leaf_index = std::nullopt)
{
    REQUIRE(package.studio_assets().size() == 1U);
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0xE510U;
    input.resource_revision = revision;
    input.interpolation = {
        0.5,
        0.0,
        1.0,
        0.5F,
        revision,
        revision + 1U,
        entity::EntityRenderInterpolationProfile::synthetic_seconds_v1,
    };
    input.studio_poses.push_back({
        package.studio_assets()[0U]->source_identity(),
        {fixture::pose_matrix()},
    });
    entity::StudioEntityRenderInstance instance;
    instance.entity_number = 1U;
    instance.studio_asset_index = 0U;
    instance.pose_index = 0U;
    instance.transform.origin = origin;
    instance.transform.rotation_degrees = rotation;
    instance.transform.uniform_scale = scale;
    instance.body_value = 0U;
    instance.skin_family_index = skin_family_index;
    instance.interpolated_bounds = bounds;
    input.studio_instances.push_back(instance);
    input.view_frustum = view_frustum;
    input.spatial_package = spatial_package;
    input.camera_leaf_index = camera_leaf_index;
    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

[[nodiscard]] spatial::WorldSpatialPackage visibility_spatial_package()
{
    const hlclient::assets::WorldBounds bounds{
        {-4.0F, -4.0F, -4.0F}, {4.0F, 4.0F, 4.0F}};
    spatial::WorldSpatialNode node;
    node.plane_index = 0U;
    node.children = {
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, 1U},
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, 2U},
    };
    node.bounds = bounds;

    spatial::WorldSpatialLeaf solid;
    solid.source_leaf_index = 0U;
    solid.bounds = bounds;
    solid.surface_membership.source_leaf_index = 0U;
    solid.solid_or_special = true;
    spatial::WorldSpatialLeaf visible;
    visible.source_leaf_index = 1U;
    visible.bounds = {{0.0F, -4.0F, -4.0F}, {4.0F, 4.0F, 4.0F}};
    visible.pvs_row_index = 0U;
    visible.pvs_bit_addressable = true;
    visible.surface_membership.source_leaf_index = 1U;
    spatial::WorldSpatialLeaf hidden;
    hidden.source_leaf_index = 2U;
    hidden.bounds = {{-4.0F, -4.0F, -4.0F}, {0.0F, 4.0F, 4.0F}};
    hidden.pvs_row_index = 1U;
    hidden.pvs_bit_addressable = true;
    hidden.surface_membership.source_leaf_index = 2U;

    return spatial::WorldSpatialPackage{
        {{{1.0F, 0.0F, 0.0F}, 0.0F, 0}},
        {node},
        {solid, visible, hidden},
        spatial::WorldPvsTable{
            1U,
            2U,
            {{std::byte{0x01U}}, {std::byte{0x02U}}},
            {std::nullopt, 0U, 1U},
            0U},
        spatial::WorldSpatialModelMetadata{0U, 2U, bounds},
        spatial::WorldSpatialStatistics{1U, 1U, 3U, 0U, 0U, 2U, 2U},
        spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

TEST_CASE("E10 production OpenGL renders three-bone transition mask lighting and reuse",
    "[renderer][opengl][entity-render][studio][e10][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();
    const auto model = e10_three_bone_model();
    const auto& skeleton = *model->skeletal_data;
    auto package_result = hlclient::tests::entity_render_fixture::scene_package(
        hlclient::tests::entity_render_fixture::render_assets(true, false,
            assets::SpriteTextureFormat::normal, false,
            assets::SpriteOrientation::view_parallel, model));
    INFO((package_result.error ? package_result.error->context : std::string{}));
    REQUIRE(package_result);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    const studio_pose::StudioPoseModelIdentity identity{
        "models/fixture.mdl", {0xE10U, 0x11U}};
    studio_pose::StudioPoseCompositionInput composition;
    composition.previous = studio_pose::StudioPoseInput{};
    composition.previous->sequence_index = 1U;
    auto evaluate = [&] {
        auto result = studio_pose::StudioPoseEvaluator{}.compose(identity, skeleton, composition);
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        return std::move(*result.pose);
    };
    const auto first = evaluate();
    composition.previous_weight = 1.0F;
    const auto last = evaluate();
    composition.previous_weight = 0.5F;
    const auto middle = evaluate();
    std::uint64_t revision = 0U;
    auto pixels_for = [&](const studio_pose::StudioPoseState& pose,
                          const std::array<float, 3U> light) {
        const fixture::SceneAndFrame entities{
            package, e10_pose_frame(*package, pose, ++revision, light)};
        auto scene = fixture::render_scene(entities);
        scene.camera.position = {0.0F, -12.0F, 4.0F};
        scene.camera.target = {0.0F, 0.0F, 3.0F};
        context->renderer().render(scene, {96, 96});
        return fixture::framebuffer();
    };
    const auto first_pixels = pixels_for(first, {0.85F, 0.85F, 0.85F});
    const auto last_pixels = pixels_for(last, {0.85F, 0.85F, 0.85F});
    const auto middle_pixels = pixels_for(middle, {0.85F, 0.85F, 0.85F});
    REQUIRE(fixture::has_non_clear_pixel(first_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
    CHECK(first_pixels != last_pixels);
    CHECK(middle_pixels != first_pixels);
    CHECK(middle_pixels != last_pixels);
    composition.previous.reset();
    composition.previous_weight = 0.0F;
    composition.layer = studio_pose::StudioPoseInput{};
    composition.layer->sequence_index = 2U;
    composition.lower_bone_indices = {1U};
    const auto masked = evaluate();
    const auto masked_pixels = pixels_for(masked, {0.85F, 0.85F, 0.85F});
    CHECK(masked_pixels != first_pixels);
    // Independent numeric upper bone is unchanged while lower geometry moves.
    std::size_t unchanged_upper_colored_pixels = 0U;
    for (std::size_t row = 58U; row < 96U; ++row)
        for (std::size_t column = 0U; column < 96U; ++column) {
            const auto offset = (row * 96U + column) * 4U;
            if (first_pixels[offset] != std::byte{5U} &&
                first_pixels[offset] == masked_pixels[offset] &&
                first_pixels[offset + 1U] == masked_pixels[offset + 1U] &&
                first_pixels[offset + 2U] == masked_pixels[offset + 2U])
                ++unchanged_upper_colored_pixels;
        }
    CHECK(unchanged_upper_colored_pixels > 5U);
    const auto dark_pixels = pixels_for(masked, {0.10F, 0.15F, 0.20F});
    const auto bright_pixels = pixels_for(masked, {0.90F, 0.95F, 1.0F});
    CHECK(dark_pixels != bright_pixels);
    CHECK(e10_rgb_sum(bright_pixels) > e10_rgb_sum(dark_pixels) + 500U);
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count == 1U);
    CHECK(context->renderer().entity_statistics().pose_ubo_update_count > 0U);
    // Lighting and composed pose upload still participate in the retained
    // production world depth buffer, not a later depth-free overlay.
    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));
    const fixture::SceneAndFrame behind_world{package,
        e10_pose_frame(*package, masked, ++revision, {1.0F, 1.0F, 1.0F},
            {{8.0F, 12.0F, -3.5F}, {90.0F, 0.0F, 0.0F}, 1.0F})};
    auto entity_scene = fixture::render_scene(behind_world);
    entity_scene.camera.position = {8.0F, -12.0F, 6.0F};
    entity_scene.camera.target = {8.5F, 12.0F, -2.0F};
    auto clear_scene = entity_scene;
    clear_scene.dynamic_entities.reset();
    context->renderer().render(entity_scene, {96, 96});
    CHECK(fixture::has_non_clear_pixel(fixture::framebuffer(),
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
    auto world_scene = clear_scene;
    world_scene.static_world.emplace(renderer::RenderStaticWorld{world,
        renderer::RenderCullMode::none,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero});
    auto combined_scene = world_scene;
    combined_scene.dynamic_entities = entity_scene.dynamic_entities;
    context->renderer().render(combined_scene, {96, 96});
    const auto combined_pixels = fixture::framebuffer();
    // Submit the baseline last: no-entity frames intentionally release the
    // entity GPU owner, so they must not intervene in the immutable reuse proof.
    context->renderer().render(world_scene, {96, 96});
    CHECK(combined_pixels == fixture::framebuffer());
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count == 1U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("E10 production OpenGL keeps wrapped lower-bone animation on the visible reference side",
    "[renderer][opengl][entity-render][studio][e10][actual-context][rotation-wrap]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context())
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    context->initialize_renderer();
    auto model = std::make_shared<assets::ModelAsset>(*e10_three_bone_model());
    auto skeleton = std::make_shared<assets::SkeletalModelAssetData>(*model->skeletal_data);
    auto& wrapped = skeleton->sequences[1U].animation_blends[0U].bone_tracks[1U].channels[5U];
    constexpr float radians_per_degree = 3.14159265358979323846F / 180.0F;
    wrapped.source_scale = radians_per_degree;
    wrapped.runs = {{0U, 2U, 2U, {179, -179}}};
    // Independent fixed-orientation palette and the previous scalar-Euler
    // midpoint are visible controls using the same geometry and upper pose.
    auto reference = skeleton->sequences[1U];
    reference.label = "project-owned-constant-180";
    auto& reference_rotation = reference.animation_blends[0U].bone_tracks[1U].channels[5U];
    reference_rotation.source_default = 180.0F * radians_per_degree;
    reference_rotation.source_scale = 0.0F;
    reference_rotation.runs.clear();
    skeleton->sequences[2U] = reference;
    auto wrong = reference;
    wrong.label = "project-owned-wrong-euler-zero";
    wrong.animation_blends[0U].bone_tracks[1U].channels[5U].source_default = 0.0F;
    skeleton->sequences.push_back(std::move(wrong));
    model->skeletal_data = skeleton;
    auto built_package = hlclient::tests::entity_render_fixture::scene_package(
        hlclient::tests::entity_render_fixture::render_assets(true, false,
            assets::SpriteTextureFormat::normal, false,
            assets::SpriteOrientation::view_parallel, model));
    INFO((built_package.error ? built_package.error->context : std::string{}));
    REQUIRE(built_package);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*built_package.package));
    studio_pose::StudioPoseCompositionInput composition;
    composition.layer.emplace();
    composition.lower_bone_indices = {1U};
    const studio_pose::StudioPoseModelIdentity identity{
        "models/fixture.mdl", {0xE10U, 0x12U}};
    std::uint64_t revision{};
    auto pixels_for = [&](const std::uint32_t sequence, const double coordinate) {
        composition.layer->sequence_index = sequence;
        composition.layer->frame_coordinate = coordinate;
        auto posed = studio_pose::StudioPoseEvaluator{}.compose(identity, *skeleton, composition);
        INFO((posed.error ? posed.error->context : std::string{}));
        REQUIRE(posed);
        const fixture::SceneAndFrame entities{
            package, e10_pose_frame(*package, *posed.pose, ++revision, {0.85F, 0.85F, 0.85F})};
        auto scene = fixture::render_scene(entities);
        scene.camera.position = {0.0F, -14.0F, 2.0F};
        scene.camera.target = {0.0F, 0.0F, 2.0F};
        context->renderer().render(scene, {128, 128});
        return fixture::framebuffer();
    };
    const auto reference_pixels = pixels_for(2U, 0.0);
    const auto wrong_pixels = pixels_for(3U, 0.0);
    const auto different_pixels = [](const std::vector<std::byte>& left,
                                     const std::vector<std::byte>& right) {
        REQUIRE(left.size() == right.size());
        std::size_t count{};
        for (std::size_t offset = 0U; offset < left.size(); offset += 4U)
            if (left[offset] != right[offset] || left[offset + 1U] != right[offset + 1U] ||
                left[offset + 2U] != right[offset + 2U]) ++count;
        return count;
    };
    const auto wrong_difference = different_pixels(reference_pixels, wrong_pixels);
    REQUIRE(wrong_difference > 40U);
    for (const double coordinate : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        INFO("lower-bone fractional coordinate=" << coordinate);
        const auto pixels = pixels_for(1U, coordinate);
        REQUIRE(fixture::has_non_clear_pixel(pixels,
            {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
        // All samples remain near the 180-degree control. A zero-degree flip,
        // missing lower geometry or wrong palette fails this comparison.
        CHECK(different_pixels(pixels, reference_pixels) * 3U < wrong_difference);
        CHECK(different_pixels(pixels, wrong_pixels) > wrong_difference / 2U);
        CHECK(pixels_for(1U, coordinate) == pixels);
    }
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count == 1U);
    CHECK(context->renderer().entity_statistics().pose_ubo_update_count > 0U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Studio entities share uploads and update bounded poses",
    "[renderer][opengl][entity-render][studio][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));

    auto first = fixture::studio_scene(1U, 0.0F, 0U);
    auto first_scene = fixture::render_scene(first);
    first_scene.static_world.emplace(hlclient::renderer::RenderStaticWorld{
        world,
        hlclient::renderer::RenderCullMode::none,
        hlclient::renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    context->renderer().render(first_scene, {96, 96});
    const auto first_pixels = fixture::framebuffer();
    const auto first_statistics = context->renderer().entity_statistics();
    CHECK(first_statistics.studio_asset_upload_count == 1U);
    CHECK(first_statistics.sprite_asset_upload_count == 0U);
    CHECK(first_statistics.entity_frame_revision == 1U);
    CHECK(first_statistics.studio_draw_count > 0U);
    CHECK(first_statistics.pose_ubo_update_count >= 2U);
    CHECK(first_statistics.visible_entity_count == 2U);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(fixture::has_non_clear_pixel(first_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));

    auto second = fixture::SceneAndFrame{
        first.package,
        fixture::make_frame(*first.package, 2U, 2U, 0U, 1.25F, 0U, 1U),
    };
    auto second_scene = fixture::render_scene(second);
    second_scene.static_world.emplace(hlclient::renderer::RenderStaticWorld{
        world,
        hlclient::renderer::RenderCullMode::none,
        hlclient::renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    context->renderer().render(second_scene, {96, 96});
    const auto second_pixels = fixture::framebuffer();
    const auto second_statistics = context->renderer().entity_statistics();
    CHECK(second_statistics.studio_asset_upload_count == 1U);
    CHECK(second_statistics.entity_frame_revision == 2U);
    CHECK(second_statistics.studio_draw_count >
        first_statistics.studio_draw_count);
    CHECK(second_statistics.pose_ubo_update_count >
        first_statistics.pose_ubo_update_count);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(second_pixels != first_pixels);
    CHECK(glGetError() == GL_NO_ERROR);

    context->renderer().render({}, {96, 96});
    CHECK_FALSE(context->renderer().entity_statistics().active_entity_resources);
    CHECK(context->renderer()
              .entity_statistics()
              .entity_resource_release_count == 1U);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK_FALSE(context->renderer().statistics().active_world_resources);
    context->release_renderer();
}

TEST_CASE("OpenGL entity scene revisions reuse retained assets and preserve world",
    "[renderer][opengl][entity-render][cache][incremental][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));

    auto studio_only_assets =
        hlclient::tests::entity_render_fixture::render_assets(true, false);
    auto mixed_assets =
        hlclient::tests::entity_render_fixture::render_assets(true, true);
    REQUIRE(studio_only_assets.studio);
    REQUIRE(mixed_assets.studio);
    REQUIRE(studio_only_assets.studio->resource_id() ==
        mixed_assets.studio->resource_id());
    REQUIRE(studio_only_assets.studio->resource_revision() ==
        mixed_assets.studio->resource_revision());
    auto studio_package_result =
        hlclient::tests::entity_render_fixture::scene_package(
            std::move(studio_only_assets));
    REQUIRE(studio_package_result);
    auto studio_package =
        std::make_shared<const hlclient::entity_render::EntitySceneRenderPackage>(
            std::move(*studio_package_result.package));
    fixture::SceneAndFrame first{
        studio_package,
        fixture::make_frame(*studio_package, 1U, 1U, 0U),
    };
    auto first_scene = fixture::render_scene(first);
    first_scene.static_world.emplace(hlclient::renderer::RenderStaticWorld{
        world,
        hlclient::renderer::RenderCullMode::none,
        hlclient::renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    context->renderer().render(first_scene, {96, 96});
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count ==
        1U);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count ==
        0U);
    CHECK(context->renderer().statistics().upload_count == 1U);

    auto mixed_package_result = hlclient::tests::entity_render_fixture::
        scene_package(std::move(mixed_assets));
    REQUIRE(mixed_package_result);
    auto mixed_package =
        std::make_shared<const hlclient::entity_render::EntitySceneRenderPackage>(
            std::move(*mixed_package_result.package));
    fixture::SceneAndFrame second{
        mixed_package,
        fixture::make_frame(*mixed_package, 2U, 1U, 1U),
    };
    auto second_scene = fixture::render_scene(second);
    second_scene.static_world = first_scene.static_world;
    context->renderer().render(second_scene, {96, 96});

    const auto statistics = context->renderer().entity_statistics();
    CHECK(statistics.studio_asset_upload_count == 1U);
    CHECK(statistics.sprite_asset_upload_count == 1U);
    CHECK(statistics.entity_frame_revision == 2U);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(glGetError() == GL_NO_ERROR);

    context->release_renderer();
}

TEST_CASE("OpenGL Studio selects a nonzero skin family without reupload",
    "[renderer][opengl][entity-render][studio][skin][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto skin_zero = fixture::studio_scene(1U, 0.0F, 0U, 0U, true);
    context->renderer().render(fixture::render_scene(skin_zero), {96, 96});
    const auto zero_pixels = fixture::framebuffer();
    const auto first_statistics = context->renderer().entity_statistics();
    CHECK(fixture::has_non_clear_pixel(zero_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
    const auto skin_zero_material =
        skin_zero.package->studio_assets()[0U]->select_material(0U, 0U);
    const auto skin_one_material =
        skin_zero.package->studio_assets()[0U]->select_material(0U, 1U);
    REQUIRE(skin_zero_material);
    REQUIRE(skin_one_material);
    CHECK(*skin_zero_material.material_index !=
        *skin_one_material.material_index);

    fixture::SceneAndFrame skin_one{
        skin_zero.package,
        fixture::make_frame(
            *skin_zero.package, 2U, 2U, 0U, 0.0F, 0U, 0U, 1U),
    };
    context->renderer().render(fixture::render_scene(skin_one), {96, 96});
    const auto one_pixels = fixture::framebuffer();
    const auto second_statistics = context->renderer().entity_statistics();
    CHECK(fixture::has_non_clear_pixel(one_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
    CHECK(one_pixels != zero_pixels);
    CHECK(first_statistics.studio_asset_upload_count == 1U);
    CHECK(second_statistics.studio_asset_upload_count == 1U);
    CHECK(second_statistics.entity_frame_revision == 2U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Studio depth testing preserves an occluding world surface",
    "[renderer][opengl][entity-render][studio][world-depth][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));
    auto entities = fixture::studio_scene();
    entities.frame = single_studio_frame(*entities.package,
        1U,
        {8.0F, 12.0F, -3.0F},
        {90.0F, 0.0F, 0.0F},
        2.0F,
        {{7.5F, 11.9F, -3.5F}, {10.5F, 12.1F, -0.5F}});
    auto entity_scene = fixture::render_scene(entities);
    entity_scene.camera.position = {8.0F, -12.0F, 6.0F};
    entity_scene.camera.target = {8.5F, 12.0F, -2.0F};

    auto clear_scene = entity_scene;
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();
    context->renderer().render(entity_scene, {96, 96});
    const auto entity_pixels = fixture::framebuffer();
    CHECK(entity_pixels != clear_pixels);

    auto world_scene = entity_scene;
    world_scene.dynamic_entities.reset();
    world_scene.static_world.emplace(renderer::RenderStaticWorld{
        world,
        renderer::RenderCullMode::none,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    auto combined_scene = world_scene;
    combined_scene.dynamic_entities = entity_scene.dynamic_entities;
    context->renderer().render(combined_scene, {96, 96});
    const auto combined_pixels = fixture::framebuffer();

    context->renderer().render(world_scene, {96, 96});
    const auto world_pixels = fixture::framebuffer();
    CHECK(world_pixels != clear_pixels);
    CHECK(combined_pixels == world_pixels);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count ==
        1U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Studio consumes CPU PVS and frustum culling",
    "[renderer][opengl][entity-render][studio][visibility][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto entities = fixture::studio_scene();
    auto spatial_package = visibility_spatial_package();
    entities.frame = single_studio_frame(*entities.package,
        1U,
        {},
        {90.0F, 0.0F, 0.0F},
        1.0F,
        {{-0.5F, -0.1F, -0.1F}, {-0.1F, 0.1F, 0.1F}},
        0U,
        nullptr,
        &spatial_package,
        1U);
    CHECK(entities.frame->statistics().culled_by_pvs_count == 1U);
    CHECK(entities.frame->statistics().visible_count == 0U);
    CHECK(entities.frame->draw_commands().empty());

    auto clear_scene = fixture::render_scene(entities);
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();
    context->renderer().render(fixture::render_scene(entities), {96, 96});
    CHECK(fixture::framebuffer() == clear_pixels);
    CHECK(context->renderer().entity_statistics().studio_draw_count == 0U);

    renderer::RenderMatrix4 identity;
    auto made_frustum = visibility::WorldViewFrustum::from_view_projection(
        identity);
    REQUIRE(made_frustum);
    entities.frame = single_studio_frame(*entities.package,
        2U,
        {},
        {90.0F, 0.0F, 0.0F},
        1.0F,
        {{2.0F, 2.0F, 2.0F}, {3.0F, 3.0F, 3.0F}},
        0U,
        &*made_frustum.frustum);
    CHECK(entities.frame->statistics().culled_by_frustum_count == 1U);
    CHECK(entities.frame->statistics().visible_count == 0U);
    CHECK(entities.frame->draw_commands().empty());
    context->renderer().render(fixture::render_scene(entities), {96, 96});
    CHECK(fixture::framebuffer() == clear_pixels);
    CHECK(context->renderer().entity_statistics().studio_draw_count == 0U);
    CHECK(context->renderer().entity_statistics().entity_frame_revision == 2U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("First-person Studio and basic HUD change pixels over a nonempty world",
    "[renderer][opengl][entity-render][first-person][actual-context]")
{
    auto context = fixture::try_context();
    if (!context) { SKIP("OpenGL 3.3 Core context unavailable"); }
    context->initialize_renderer();
    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));
    auto entities = fixture::studio_scene();
    entities.frame = single_studio_frame(*entities.package, 1U,
        {8.0F, 12.0F, -3.0F}, {90.0F, 0.0F, 0.0F}, 2.0F,
        {{7.5F, 11.9F, -3.5F}, {10.5F, 12.1F, -0.5F}});
    auto scene = fixture::render_scene(entities);
    scene.camera.position = {8.0F, -12.0F, 6.0F};
    scene.camera.target = {8.5F, 12.0F, -2.0F};
    auto first_person_payload = scene.dynamic_entities;
    first_person_payload->frame = single_studio_frame(*entities.package, 2U,
        {4.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F}, 1.0F,
        {{3.5F, -0.5F, -0.5F}, {4.5F, 0.5F, 0.5F}});
    scene.dynamic_entities.reset();
    scene.static_world.emplace(renderer::RenderStaticWorld{
        world, renderer::RenderCullMode::none,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero});
    context->renderer().render(scene, {96, 96});
    const auto world_pixels = fixture::framebuffer();
    scene.first_person_entities = first_person_payload;
    context->renderer().render(scene, {96, 96});
    const auto first_person_pixels = fixture::framebuffer();
    const bool model_pixels_distinct = first_person_pixels != world_pixels;
    CHECK(model_pixels_distinct);
    const auto uploads = context->renderer().entity_statistics().studio_asset_upload_count;
    context->renderer().render(scene, {96, 96});
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count == uploads);
    scene.first_person_entities.reset();
    scene.basic_hud = renderer::RenderBasicHud{
        {{8.0F,8.0F,590.0F,64.0F,{0.01F,0.02F,0.03F,0.78F}}},
        {{"HP 87  ARM 13\nweapon_crowbar  CLIP -  AMMO ?",
          18.0F,16.0F,2.0F,12.0F,20.0F,{0.78F,0.92F,0.64F,1.0F}}}};
    context->renderer().render(scene, {96, 96});
    const auto hud_pixels = fixture::framebuffer();
    CHECK(hud_pixels != world_pixels);
    scene.basic_hud.reset();
    scene.static_world.reset();
    scene.first_person_entities = first_person_payload;
    context->renderer().render(scene, {96, 96});
    const auto attached_pixels = fixture::framebuffer();
    const std::array<hlclient::assets::AssetVector3, 8U> directions{{
        {0.8660254F, 0.0F, 0.5F}, {0.8660254F, 0.0F, -0.5F},
        {0.1736482F, 0.0F, 0.9848078F},
        {0.1736482F, 0.0F, -0.9848078F},
        {0.0F, 1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F},
        {0.0F, 0.8660254F, 0.5F},
        {-0.1736482F, 0.0F, -0.9848078F}}};
    for (const auto direction : directions) {
        scene.camera.position = {12.0F, -7.0F, 3.0F};
        scene.camera.target = {
            scene.camera.position.x + direction.x,
            scene.camera.position.y + direction.y,
            scene.camera.position.z + direction.z};
        context->renderer().render(scene, {96, 96});
        const bool camera_attached = fixture::framebuffer() == attached_pixels;
        CHECK(camera_attached);
    }
    // The same Studio frame in the world pass does respond to world camera
    // translation; it is not accidentally using the first-person matrix.
    scene.first_person_entities.reset();
    scene.dynamic_entities = first_person_payload;
    scene.camera.position = {0.0F, 0.0F, 0.0F};
    scene.camera.target = {1.0F, 0.0F, 0.0F};
    context->renderer().render(scene, {96, 96});
    const auto world_studio_near = fixture::framebuffer();
    scene.camera.position = {12.0F, -7.0F, 3.0F};
    scene.camera.target = {13.0F, -7.0F, 3.0F};
    context->renderer().render(scene, {96, 96});
    const bool world_studio_moved =
        fixture::framebuffer() != world_studio_near;
    CHECK(world_studio_moved);
    CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    GLint depth_function = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
    CHECK(depth_function == GL_LEQUAL);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("Camera-local Studio projection remains visible at wide and 4:3 extents",
    "[renderer][opengl][entity-render][first-person][actual-context]")
{
    auto entities = fixture::studio_scene();
    entities.frame = single_studio_frame(*entities.package, 3U,
        {4.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F}, 1.0F,
        {{3.5F, -0.5F, -0.5F}, {4.5F, 0.5F, 0.5F}});
    for (const renderer::RenderExtent extent :
         {renderer::RenderExtent{1280, 720},
          renderer::RenderExtent{1920, 1080},
          renderer::RenderExtent{800, 600}}) {
        auto context = fixture::try_context(extent.width, extent.height);
        if (!context) { SKIP("OpenGL 3.3 Core context unavailable"); }
        context->initialize_renderer();
        auto scene = fixture::render_scene(entities);
        scene.dynamic_entities.reset();
        scene.first_person_entities = renderer::RenderDynamicEntities{
            entities.package, entities.frame, {1U, 1U, 1U, 0U, 0U}};
        context->renderer().render(scene, extent);
        const auto baseline = context->renderer().observe_framebuffer(
            extent, scene.clear_color);
        REQUIRE(baseline);
        CHECK(baseline.non_clear_pixel_count > 0U);
        CHECK(baseline.has_non_clear_bounds);
        CHECK(baseline.maximum_x < extent.width);
        CHECK(baseline.maximum_y < extent.height);
        scene.camera.position = {55.0F, -81.0F, 29.0F};
        scene.camera.target = {55.1736482F, -81.0F, 29.9848078F};
        context->renderer().render(scene, extent);
        const auto pitched = context->renderer().observe_framebuffer(
            extent, scene.clear_color);
        REQUIRE(pitched);
        CHECK(pitched.color_signature == baseline.color_signature);
        CHECK(pitched.minimum_x == baseline.minimum_x);
        CHECK(pitched.maximum_x == baseline.maximum_x);
        CHECK(glGetError() == GL_NO_ERROR);
        context->release_renderer();
    }
}


} // namespace
