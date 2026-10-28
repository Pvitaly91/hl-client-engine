#include <hlclient/entity_render/entity_scene_render.hpp>

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include "entity_render/entity_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace render = hlclient::entity_render;
namespace fixture = hlclient::tests::entity_render_fixture;
namespace renderer = hlclient::renderer;
namespace spatial = hlclient::world_spatial;
namespace visibility = hlclient::world_visibility;

template <typename Type>
concept HasNativePathOrRawSnapshot =
    requires(const Type& value) { value.native_path; } ||
    requires(const Type& value) { value.filesystem_path; } ||
    requires(const Type& value) { value.raw_snapshot; } ||
    requires(const Type& value) { value.delta_object_state; };

template <typename Type>
concept HasOpenGlHandle = requires(const Type& value) {
    value.vao;
    value.vbo;
    value.ebo;
    value.gl_id;
};

[[nodiscard]] std::array<float, 16U> identity_matrix()
{
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] render::EntityRenderFrameBuildInput mixed_frame_input(
    const render::EntitySceneRenderPackage& package)
{
    render::EntityRenderFrameBuildInput input;
    input.resource_id = 0x8000U;
    input.resource_revision = 3U;
    input.interpolation = {
        0.5, 0.0, 1.0, 0.5F, 0x10U, 0x11U,
        render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    const auto& studio_asset = *package.studio_assets()[0U];
    input.studio_poses.push_back({
        studio_asset.source_identity(), {identity_matrix()}});

    render::StudioEntityRenderInstance studio;
    studio.entity_number = 2U;
    studio.studio_asset_index = 0U;
    studio.pose_index = 0U;
    studio.body_value = 0U;
    studio.skin_family_index = 0U;
    studio.interpolated_bounds = {
        {0.1F, -0.1F, -0.1F}, {0.5F, 0.1F, 0.1F}};
    input.studio_instances.push_back(studio);

    render::SpriteEntityRenderInstance sprite;
    sprite.entity_number = 3U;
    sprite.sprite_asset_index = 0U;
    sprite.selected_frame_index = 0U;
    sprite.orientation = package.sprite_assets()[0U]->orientation();
    sprite.bounds = {{-0.5F, -0.1F, -0.1F}, {-0.1F, 0.1F, 0.1F}};
    input.sprite_instances.push_back(sprite);

    input.unsupported_instances.push_back({
        1U,
        std::nullopt,
        render::UnsupportedEntityVisualReason::unsupported_asset_kind,
        render::RuntimeEntityVisibilityStatus::unsupported_visual,
    });
    return input;
}

[[nodiscard]] spatial::WorldSpatialPackage make_spatial_package()
{
    const assets::WorldBounds bounds{
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

TEST_CASE("Entity scene package publishes exact Studio Sprite and mixed assets",
    "[entity-render][scene][package]")
{
    SECTION("Studio only")
    {
        auto built = fixture::scene_package(fixture::render_assets(true, false));
        REQUIRE(built);
        CHECK(built.package->studio_assets().size() == 1U);
        CHECK(built.package->sprite_assets().empty());
        CHECK(built.package->statistics().visual_asset_count == 1U);
    }
    SECTION("Sprite only")
    {
        auto built = fixture::scene_package(fixture::render_assets(false, true));
        REQUIRE(built);
        CHECK(built.package->studio_assets().empty());
        CHECK(built.package->sprite_assets().size() == 1U);
        CHECK(built.package->statistics().visual_asset_count == 1U);
    }
    SECTION("mixed")
    {
        auto built = fixture::scene_package(fixture::render_assets());
        REQUIRE(built);
        CHECK(built.package->studio_assets().size() == 1U);
        CHECK(built.package->sprite_assets().size() == 1U);
        CHECK(built.package->asset_library());
        CHECK(built.package->resource_id() == 0x7000U);
        CHECK(built.package->resource_revision() != 0U);
        CHECK(built.package->statistics().model_gpu_source_bytes > 0U);
        CHECK(built.package->statistics().sprite_gpu_source_bytes > 0U);
    }
}

TEST_CASE("Entity scene package owns inputs and enforces exact limits",
    "[entity-render][scene][ownership][limits]")
{
    std::weak_ptr<const render::StudioModelRenderAsset> weak_studio;
    std::weak_ptr<const render::SpriteRenderAsset> weak_sprite;
    std::weak_ptr<const hlclient::entity_visual::EntityVisualAssetLibraryState>
        weak_library;
    auto built = [&] {
        auto assets = fixture::render_assets();
        weak_studio = assets.studio;
        weak_sprite = assets.sprite;
        weak_library = assets.sources.library;
        return fixture::scene_package(std::move(assets));
    }();
    REQUIRE(built);
    CHECK_FALSE(weak_studio.expired());
    CHECK_FALSE(weak_sprite.expired());
    CHECK_FALSE(weak_library.expired());
    built.package.reset();
    CHECK(weak_studio.expired());
    CHECK(weak_sprite.expired());
    CHECK(weak_library.expired());

    auto limits = render::RuntimeEntityVisualLimits{};
    limits.maximum_visual_assets = 1U;
    const auto rejected = fixture::scene_package(
        fixture::render_assets(), limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        render::EntitySceneRenderErrorCode::source_limit_exceeded);

    const auto invalid_world_identity = fixture::scene_package(
        fixture::render_assets(), {}, render::EntityRenderResourceIdentity{});
    REQUIRE_FALSE(invalid_world_identity);
    REQUIRE(invalid_world_identity.error);
    CHECK(invalid_world_identity.error->code ==
        render::EntitySceneRenderErrorCode::invalid_resource_identity);
}

TEST_CASE("Entity render frame owns ordered instances poses and draw buckets",
    "[entity-render][frame][ordering]")
{
    auto scene = fixture::scene_package(fixture::render_assets());
    REQUIRE(scene);
    auto built = render::EntityRenderFrameBuilder{}.build(
        *scene.package, mixed_frame_input(*scene.package));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.frame);
    const auto& frame = *built.frame;

    REQUIRE(frame.studio_poses().size() == 1U);
    CHECK(frame.studio_poses()[0U].bone_matrices.size() == 1U);
    REQUIRE(frame.studio_instances().size() == 1U);
    REQUIRE(frame.sprite_instances().size() == 1U);
    REQUIRE(frame.unsupported_instances().size() == 1U);
    CHECK(frame.unsupported_instances()[0U].entity_number == 1U);
    CHECK(frame.studio_instances()[0U].entity_number == 2U);
    CHECK(frame.sprite_instances()[0U].entity_number == 3U);
    CHECK(frame.studio_instances()[0U].material_support_status ==
        render::StudioEntityMaterialSupportStatus::
            supported_opaque_and_masked);

    REQUIRE(frame.draw_commands().size() == 3U);
    CHECK(frame.draw_commands()[0U].draw_class ==
        render::EntityDrawClass::studio_opaque);
    CHECK(frame.draw_commands()[1U].draw_class ==
        render::EntityDrawClass::studio_masked);
    CHECK(frame.draw_commands()[2U].draw_class ==
        render::EntityDrawClass::sprite_normal);
    CHECK(frame.statistics().candidate_count == 3U);
    CHECK(frame.statistics().visible_count == 2U);
    CHECK(frame.statistics().unsupported_visual_count == 1U);
    CHECK(frame.resource_id() == 0x8000U);
    CHECK(frame.resource_revision() == 3U);
    CHECK((frame.scene_package_identity() ==
        render::EntityRenderResourceIdentity{
            scene.package->resource_id(), scene.package->resource_revision()}));
    CHECK(frame.frame_signature() != 0U);

    auto limits = render::RuntimeEntityVisualLimits{};
    limits.maximum_entity_draws = 2U;
    const auto rejected = render::EntityRenderFrameBuilder{}.build(
        *scene.package, mixed_frame_input(*scene.package), limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        render::EntityRenderFrameErrorCode::source_limit_exceeded);
}

TEST_CASE("Entity render frame rejects forged interpolation metadata",
    "[entity-render][frame][interpolation][errors][forged]")
{
    auto scene = fixture::scene_package(fixture::render_assets());
    REQUIRE(scene);

    SECTION("evidence-pending profile")
    {
        auto input = mixed_frame_input(*scene.package);
        input.interpolation.profile = render::EntityRenderInterpolationProfile::
            stock_server_time_evidence_pending;
        const auto rejected = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == render::EntityRenderFrameErrorCode::
            invalid_interpolation_metadata);
    }
    SECTION("unknown profile enum")
    {
        auto input = mixed_frame_input(*scene.package);
        input.interpolation.profile = static_cast<
            render::EntityRenderInterpolationProfile>(0xFF);
        const auto rejected = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == render::EntityRenderFrameErrorCode::
            invalid_interpolation_metadata);
    }
    SECTION("alpha does not describe sample time")
    {
        auto input = mixed_frame_input(*scene.package);
        input.interpolation.alpha = 0.25F;
        const auto rejected = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == render::EntityRenderFrameErrorCode::
            invalid_interpolation_metadata);
    }
    SECTION("held frame preserves an outside-range sample time")
    {
        auto input = mixed_frame_input(*scene.package);
        input.interpolation.sample_time_seconds = -1.0;
        input.interpolation.previous_time_seconds = 0.0;
        input.interpolation.current_time_seconds = 0.0;
        input.interpolation.alpha = 0.0F;
        input.interpolation.current_state_identity =
            input.interpolation.previous_state_identity;
        const auto built = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        INFO((built.error ? built.error->context : std::string{}));
        REQUIRE(built);
        REQUIRE(built.frame);
        CHECK(built.frame->interpolation().sample_time_seconds == -1.0);
    }
}

TEST_CASE("E10 public runtime time provenance retains bounded interpolation validation",
    "[entity-render][frame][interpolation][e10]")
{
    auto scene = fixture::scene_package(fixture::render_assets());
    REQUIRE(scene);
    auto input = mixed_frame_input(*scene.package);
    input.interpolation.profile = render::EntityRenderInterpolationProfile::
        public_runtime_server_seconds_v1;
    const auto built = render::EntityRenderFrameBuilder{}.build(
        *scene.package, input);
    REQUIRE(built);
    CHECK(built.frame->interpolation().alpha == 0.5F);
    CHECK(built.frame->interpolation().profile ==
        render::EntityRenderInterpolationProfile::public_runtime_server_seconds_v1);

    SECTION("mismatched alpha") { input.interpolation.alpha = 1.0F; }
    SECTION("unordered times") { input.interpolation.previous_time_seconds = 2.0; }
    SECTION("nonfinite sample") {
        input.interpolation.sample_time_seconds =
            std::numeric_limits<double>::infinity();
    }
    SECTION("sample outside selected pair") {
        input.interpolation.sample_time_seconds = 2.0;
    }
    SECTION("unconfirmed identity") {
        input.interpolation.current_state_identity = 0U;
    }
    const auto rejected = render::EntityRenderFrameBuilder{}.build(
        *scene.package, std::move(input));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == render::EntityRenderFrameErrorCode::
        invalid_interpolation_metadata);
}

TEST_CASE("E10 neutral Studio static light is optional finite and bounded",
    "[entity-render][frame][lighting][e10]")
{
    auto scene = fixture::scene_package(fixture::render_assets());
    REQUIRE(scene);
    auto input = mixed_frame_input(*scene.package);
    const auto fallback = render::EntityRenderFrameBuilder{}.build(
        *scene.package, input);
    REQUIRE(fallback);
    CHECK_FALSE(fallback.frame->studio_instances()[0U].static_light_rgb);
    const std::array<float, 3U> expected_light{0.1F, 0.4F, 0.9F};
    input.studio_instances[0U].static_light_rgb = expected_light;
    const auto lit = render::EntityRenderFrameBuilder{}.build(*scene.package, input);
    REQUIRE(lit);
    REQUIRE(lit.frame->studio_instances()[0U].static_light_rgb);
    CHECK(*lit.frame->studio_instances()[0U].static_light_rgb == expected_light);
    SECTION("negative") { (*input.studio_instances[0U].static_light_rgb)[0U] = -0.1F; }
    SECTION("above neutral range") { (*input.studio_instances[0U].static_light_rgb)[0U] = 1.1F; }
    SECTION("nonfinite") {
        (*input.studio_instances[0U].static_light_rgb)[0U] =
            std::numeric_limits<float>::quiet_NaN();
    }
    const auto rejected = render::EntityRenderFrameBuilder{}.build(
        *scene.package, std::move(input));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == render::EntityRenderFrameErrorCode::invalid_instance);
}

TEST_CASE("Entity render frame applies PVS and frustum-only visibility",
    "[entity-render][frame][visibility]")
{
    auto scene = fixture::scene_package(fixture::render_assets());
    REQUIRE(scene);

    SECTION("PVS uses any visible non-solid touched leaf")
    {
        auto spatial_package = make_spatial_package();
        auto input = mixed_frame_input(*scene.package);
        input.spatial_package = &spatial_package;
        input.camera_leaf_index = 1U;
        auto built = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        REQUIRE(built);
        CHECK(built.frame->studio_instances()[0U].visibility_status ==
            render::RuntimeEntityVisibilityStatus::visible);
        CHECK(built.frame->sprite_instances()[0U].visibility_status ==
            render::RuntimeEntityVisibilityStatus::culled_by_pvs);
        CHECK(built.frame->statistics().culled_by_pvs_count == 1U);
    }

    SECTION("absence of a spatial package falls back to frustum only")
    {
        renderer::RenderMatrix4 identity;
        auto made_frustum = visibility::WorldViewFrustum::from_view_projection(
            identity);
        REQUIRE(made_frustum);
        auto input = mixed_frame_input(*scene.package);
        input.sprite_instances[0U].bounds = {
            {2.0F, 2.0F, 2.0F}, {3.0F, 3.0F, 3.0F}};
        input.view_frustum = &*made_frustum.frustum;
        auto built = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        REQUIRE(built);
        CHECK(built.frame->sprite_instances()[0U].visibility_status ==
            render::RuntimeEntityVisibilityStatus::culled_by_frustum);
        CHECK(built.frame->statistics().culled_by_frustum_count == 1U);
    }
}

TEST_CASE("Entity render frame suppresses evidence-pending Sprite orientations",
    "[entity-render][frame][sprite][orientation][unsupported]")
{
    for (const auto orientation : {
             assets::SpriteOrientation::facing_upright}) {
        auto scene = fixture::scene_package(fixture::render_assets(false,
            true,
            assets::SpriteTextureFormat::normal,
            false,
            orientation));
        REQUIRE(scene);
        render::EntityRenderFrameBuildInput input;
        input.resource_id = 0x8001U;
        input.resource_revision = 1U;
        input.interpolation = {
            0.5, 0.0, 1.0, 0.5F, 0x10U, 0x11U,
            render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
        render::SpriteEntityRenderInstance sprite;
        sprite.entity_number = 1U;
        sprite.sprite_asset_index = 0U;
        sprite.selected_frame_index = 0U;
        sprite.orientation = orientation;
        sprite.texture_format_support =
            scene.package->sprite_assets()[0U]->texture_support_status();
        sprite.bounds = {{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
        input.sprite_instances.push_back(sprite);

        auto built = render::EntityRenderFrameBuilder{}.build(
            *scene.package, std::move(input));
        REQUIRE(built);
        REQUIRE(built.frame);
        REQUIRE(built.frame->sprite_instances().size() == 1U);
        CHECK(built.frame->sprite_instances()[0U].visibility_status ==
            render::RuntimeEntityVisibilityStatus::unsupported_visual);
        CHECK(built.frame->statistics().unsupported_visual_count == 1U);
        CHECK(built.frame->statistics().visible_count == 0U);
        CHECK(built.frame->draw_commands().empty());
    }
}

TEST_CASE("E16 entity render frame draws verified camera-parallel oriented sprites",
    "[e16][entity-render][frame][sprite][orientation]")
{
    auto scene = fixture::scene_package(fixture::render_assets(false,
        true, assets::SpriteTextureFormat::normal, false,
        assets::SpriteOrientation::view_parallel_oriented));
    REQUIRE(scene);
    render::EntityRenderFrameBuildInput input;
    input.resource_id = 0x8001U;
    input.resource_revision = 1U;
    input.interpolation = {
        0.5, 0.0, 1.0, 0.5F, 0x10U, 0x11U,
        render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    render::SpriteEntityRenderInstance sprite;
    sprite.entity_number = 1U;
    sprite.sprite_asset_index = 0U;
    sprite.selected_frame_index = 0U;
    sprite.orientation = assets::SpriteOrientation::view_parallel_oriented;
    sprite.texture_format_support =
        scene.package->sprite_assets()[0U]->texture_support_status();
    sprite.transform.rotation_degrees.x = 45.0F;
    sprite.bounds = {{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
    input.sprite_instances.push_back(sprite);
    const auto built = render::EntityRenderFrameBuilder{}.build(
        *scene.package, std::move(input));
    REQUIRE(built);
    REQUIRE(built.frame);
    REQUIRE(built.frame->sprite_instances().size() == 1U);
    CHECK(built.frame->sprite_instances()[0U].orientation ==
        assets::SpriteOrientation::view_parallel_oriented);
    CHECK(built.frame->sprite_instances()[0U].visibility_status ==
        render::RuntimeEntityVisibilityStatus::visible);
    CHECK(built.frame->statistics().unsupported_visual_count == 0U);
    CHECK(built.frame->statistics().visible_count == 1U);
    REQUIRE(built.frame->draw_commands().size() == 1U);
    CHECK(built.frame->draw_commands()[0U].draw_class ==
        render::EntityDrawClass::sprite_normal);
}

TEST_CASE("E21 surface-aligned sprite execution is explicit narrow and signature-owned",
    "[e21][e21-2][entity-render][frame][sprite][orientation][no-stock]")
{
    auto scene = fixture::scene_package(fixture::render_assets(true, true,
        assets::SpriteTextureFormat::additive));
    REQUIRE(scene);
    auto input = mixed_frame_input(*scene.package);
    auto& sprite = input.sprite_instances[0U];
    CHECK_FALSE(sprite.surface_aligned);
    REQUIRE(scene.package->sprite_assets()[0U]->orientation() ==
        assets::SpriteOrientation::view_parallel);
    sprite.blend_mode = render::SpriteBlendMode::additive;
    sprite.transform.rotation_degrees = {0.0F, 90.0F, 0.0F};
    const auto ordinary = render::EntityRenderFrameBuilder{}.build(*scene.package, input);
    REQUIRE(ordinary);

    sprite.surface_aligned = true;
    sprite.orientation = assets::SpriteOrientation::oriented;
    const auto aligned = render::EntityRenderFrameBuilder{}.build(*scene.package, input);
    REQUIRE(aligned);
    REQUIRE(aligned.frame->sprite_instances().size() == 1U);
    CHECK(aligned.frame->sprite_instances()[0U].surface_aligned);
    CHECK(aligned.frame->sprite_instances()[0U].orientation ==
        assets::SpriteOrientation::oriented);
    CHECK(aligned.frame->sprite_instances()[0U].visibility_status ==
        render::RuntimeEntityVisibilityStatus::visible);
    CHECK(aligned.frame->frame_signature() != ordinary.frame->frame_signature());
    const auto repeated = render::EntityRenderFrameBuilder{}.build(*scene.package, input);
    REQUIRE(repeated);
    CHECK(repeated.frame->frame_signature() == aligned.frame->frame_signature());
    CHECK(scene.package->sprite_assets()[0U]->orientation() ==
        assets::SpriteOrientation::view_parallel); // immutable source metadata

    SECTION("unflagged orientation mismatch remains rejected") { sprite.surface_aligned = false; }
    SECTION("flag cannot preserve the source billboard orientation") {
        sprite.orientation = assets::SpriteOrientation::view_parallel;
    }
    SECTION("flag cannot select another billboard orientation") {
        sprite.orientation = assets::SpriteOrientation::view_parallel_oriented;
    }
    SECTION("flag cannot bypass additive blend requirement") {
        sprite.blend_mode = render::SpriteBlendMode::alpha_blended;
    }
    SECTION("flag cannot disable depth through the glow mode") {
        sprite.blend_mode = render::SpriteBlendMode::glow;
    }
    SECTION("flag cannot select opaque blending") { sprite.blend_mode = render::SpriteBlendMode::opaque; }
    SECTION("flag cannot orient a ribbon") { sprite.ribbon_end = assets::AssetVector3{1.0F, 0.0F, 0.0F}; }
    SECTION("flag cannot bypass finite transform validation") {
        sprite.transform.rotation_degrees.y = std::numeric_limits<float>::quiet_NaN();
    }
    const auto rejected = render::EntityRenderFrameBuilder{}.build(*scene.package, input);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
}

TEST_CASE("E21 surface alignment cannot override other source sprite orientations",
    "[e21][e21-2][entity-render][frame][sprite][orientation][no-stock]")
{
    for (const auto source_orientation : {assets::SpriteOrientation::oriented,
             assets::SpriteOrientation::view_parallel_oriented,
             assets::SpriteOrientation::view_parallel_upright,
             assets::SpriteOrientation::facing_upright}) {
        CAPTURE(source_orientation);
        auto scene = fixture::scene_package(fixture::render_assets(true, true,
            assets::SpriteTextureFormat::additive, false, source_orientation));
        REQUIRE(scene);
        auto input = mixed_frame_input(*scene.package);
        auto& sprite = input.sprite_instances[0U];
        sprite.surface_aligned = true;
        sprite.orientation = assets::SpriteOrientation::oriented;
        sprite.blend_mode = render::SpriteBlendMode::additive;
        const auto rejected = render::EntityRenderFrameBuilder{}.build(*scene.package, input);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == render::EntityRenderFrameErrorCode::invalid_asset_reference);
    }
}

TEST_CASE("E11 generic frame keeps sixteen owner-child pairs and rejects orphan duplicate attachments",
          "[e11][entity-render][frame][bounds]") {
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    auto input=mixed_frame_input(*scene.package);
    const auto body=input.studio_instances[0];
    input.studio_instances.clear();input.sprite_instances.clear();input.unsupported_instances.clear();
    for(unsigned player=1U;player<=16U;++player) {
        auto primary=body;primary.entity_number=player;
        auto child=primary;child.subinstance_ordinal=1U;
        input.studio_instances.push_back(child);input.studio_instances.push_back(primary);
    }
    const auto built=render::EntityRenderFrameBuilder{}.build(*scene.package,input);REQUIRE(built);
    REQUIRE(built.frame->studio_instances().size()==32U);REQUIRE(built.frame->draw_commands().size()==64U);
    for(std::size_t i=0;i<32U;i+=2U) {
        const auto& a=built.frame->studio_instances()[i];const auto& b=built.frame->studio_instances()[i+1U];
        CHECK(a.entity_number==b.entity_number);CHECK(a.subinstance_ordinal==0U);CHECK(b.subinstance_ordinal==1U);
        // This fixture has opaque and masked meshes: BOTH parent passes must
        // precede BOTH child passes, including mixed-material attachments.
        CHECK(built.frame->draw_commands()[i*2U].instance_index==i);
        CHECK(built.frame->draw_commands()[i*2U+1U].instance_index==i);
        CHECK(built.frame->draw_commands()[i*2U+2U].instance_index==i+1U);
        CHECK(built.frame->draw_commands()[i*2U+3U].instance_index==i+1U);
    }
    auto duplicate=input;duplicate.studio_instances.push_back(input.studio_instances[0]);
    CHECK_FALSE(render::EntityRenderFrameBuilder{}.build(*scene.package,std::move(duplicate)));
    auto orphan=input;orphan.studio_instances[1].entity_number=900U;
    CHECK_FALSE(render::EntityRenderFrameBuilder{}.build(*scene.package,std::move(orphan)));
    auto illegal=input;illegal.studio_instances[0].subinstance_ordinal=2U;
    CHECK_FALSE(render::EntityRenderFrameBuilder{}.build(*scene.package,std::move(illegal)));
}

TEST_CASE("Sorted frame identity admission preserves duplicate errors across all candidate streams",
          "[entity-render][frame][duplicates][optimization-p5]") {
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    const auto base=mixed_frame_input(*scene.package);
    const auto expect_duplicate=[&](render::EntityRenderFrameBuildInput input,
                                    const std::uint32_t number,
                                    const char* context) {
        const auto rejected=render::EntityRenderFrameBuilder{}.build(*scene.package,std::move(input));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code==render::EntityRenderFrameErrorCode::duplicate_entity_number);
        CHECK(rejected.error->entity_number==number);
        CHECK(rejected.error->context==context);
    };
    constexpr auto studio_error="Invalid primary/attached Studio instance identity";
    constexpr auto other_error="Entity number is zero or repeated across frame candidates";

    SECTION("all Studio errors take precedence over later candidate streams") {
        auto duplicate=base;
        duplicate.studio_instances.push_back(duplicate.studio_instances[0]);
        duplicate.sprite_instances[0].entity_number=0U;
        duplicate.unsupported_instances[0].entity_number=0U;
        expect_duplicate(std::move(duplicate),2U,studio_error);

        auto orphan=base;
        orphan.studio_instances[0].subinstance_ordinal=1U;
        expect_duplicate(std::move(orphan),2U,studio_error);

        auto zero_child=base;
        zero_child.studio_instances[0].entity_number=0U;
        zero_child.studio_instances[0].subinstance_ordinal=1U;
        expect_duplicate(std::move(zero_child),0U,studio_error);

        auto repeated_child=base;
        auto child=repeated_child.studio_instances[0];child.subinstance_ordinal=1U;
        repeated_child.studio_instances.push_back(child);
        repeated_child.studio_instances.push_back(child);
        expect_duplicate(std::move(repeated_child),2U,studio_error);

        auto illegal_ordinal=base;
        illegal_ordinal.studio_instances[0].subinstance_ordinal=2U;
        expect_duplicate(std::move(illegal_ordinal),2U,studio_error);
    }
    SECTION("sorted sprites reject zero, repeat and Studio collisions before unsupported IDs") {
        auto zero=base;
        zero.sprite_instances[0].entity_number=0U;
        expect_duplicate(std::move(zero),0U,other_error);

        auto repeated=base;
        repeated.sprite_instances.push_back(repeated.sprite_instances[0]);
        repeated.unsupported_instances[0].entity_number=0U;
        expect_duplicate(std::move(repeated),3U,other_error);

        auto cross_kind=base;
        cross_kind.sprite_instances[0].entity_number=2U;
        cross_kind.unsupported_instances[0].entity_number=0U;
        expect_duplicate(std::move(cross_kind),2U,other_error);
    }
    SECTION("unsupported candidates retain sorted first-error precedence") {
        auto zero=base;
        zero.unsupported_instances[0].entity_number=0U;
        expect_duplicate(std::move(zero),0U,other_error);

        auto repeated=base;
        repeated.unsupported_instances.push_back(repeated.unsupported_instances[0]);
        expect_duplicate(std::move(repeated),1U,other_error);

        auto cross_kind=base;
        auto studio_collision=cross_kind.unsupported_instances[0];
        studio_collision.entity_number=2U;
        cross_kind.unsupported_instances.push_back(studio_collision);
        auto sprite_collision=studio_collision;sprite_collision.entity_number=3U;
        cross_kind.unsupported_instances.push_back(sprite_collision);
        expect_duplicate(std::move(cross_kind),2U,other_error);

        auto sprite_only=base;
        sprite_only.unsupported_instances[0].entity_number=3U;
        expect_duplicate(std::move(sprite_only),3U,other_error);
    }
}

TEST_CASE("Large sparse IDs and unsorted owner-child streams retain exact frame order and identity",
          "[entity-render][frame][duplicates][optimization-p5]") {
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    auto input=mixed_frame_input(*scene.package);
    const auto body=input.studio_instances[0];
    const auto sprite=input.sprite_instances[0];
    const auto unsupported=input.unsupported_instances[0];
    input.studio_instances.clear();
    input.sprite_instances.clear();
    input.unsupported_instances.clear();
    constexpr std::uint32_t pairs=64U;
    for(std::uint32_t index=0U;index<pairs;++index) {
        const auto number=std::numeric_limits<std::uint32_t>::max()-3U-index*3U;
        auto parent=body;parent.entity_number=number;
        auto child=parent;child.subinstance_ordinal=1U;
        input.studio_instances.push_back(child);
        input.studio_instances.push_back(parent);
        auto next_sprite=sprite;next_sprite.entity_number=number+1U;
        input.sprite_instances.push_back(next_sprite);
        auto next_unsupported=unsupported;next_unsupported.entity_number=number+2U;
        input.unsupported_instances.push_back(next_unsupported);
    }
    const auto first=render::EntityRenderFrameBuilder{}.build(*scene.package,input);
    INFO((first.error?first.error->context:std::string{}));
    REQUIRE(first);REQUIRE(first.frame);
    CHECK(first.frame->studio_instances().size()==pairs*2U);
    CHECK(first.frame->sprite_instances().size()==pairs);
    CHECK(first.frame->unsupported_instances().size()==pairs);
    CHECK(first.frame->draw_commands().size()==pairs*5U);
    for(std::size_t index=0U;index<pairs;++index) {
        const auto parent=index*2U;
        CHECK(first.frame->studio_instances()[parent].subinstance_ordinal==0U);
        CHECK(first.frame->studio_instances()[parent+1U].subinstance_ordinal==1U);
        CHECK(first.frame->draw_commands()[index*4U].instance_index==parent);
        CHECK(first.frame->draw_commands()[index*4U+1U].instance_index==parent);
        CHECK(first.frame->draw_commands()[index*4U+2U].instance_index==parent+1U);
        CHECK(first.frame->draw_commands()[index*4U+3U].instance_index==parent+1U);
    }
    std::reverse(input.studio_instances.begin(),input.studio_instances.end());
    std::reverse(input.sprite_instances.begin(),input.sprite_instances.end());
    std::reverse(input.unsupported_instances.begin(),input.unsupported_instances.end());
    const auto reordered=render::EntityRenderFrameBuilder{}.build(*scene.package,std::move(input));
    INFO((reordered.error?reordered.error->context:std::string{}));
    REQUIRE(reordered);REQUIRE(reordered.frame);
    CHECK(reordered.frame->frame_signature()==first.frame->frame_signature());
}

TEST_CASE("Opt-in sorted frame identity admission benchmark excludes source construction",
          "[optimization-p5][benchmark][.]") {
    // This fixture intentionally uses only the unchanged public builder API,
    // so the identical test source can be compiled against baseline and
    // candidate production sources. The input clone is outside the timer.
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    const auto source=mixed_frame_input(*scene.package);
    constexpr std::size_t warmup=32U,samples=256U;
    for(const std::size_t pairs:{16U,64U,4096U}) {
        auto input=source;
        input.studio_instances.clear();
        input.sprite_instances.clear();
        input.unsupported_instances.clear();
        input.studio_instances.reserve(pairs*2U);
        for(std::size_t index=0U;index<pairs;++index) {
            auto primary=source.studio_instances[0U];
            primary.entity_number=static_cast<std::uint32_t>(pairs-index);
            auto child=primary;child.subinstance_ordinal=1U;
            input.studio_instances.push_back(child);
            input.studio_instances.push_back(primary);
        }
        auto limits=render::RuntimeEntityVisualLimits{};
        limits.maximum_entities=std::max(limits.maximum_entities,pairs*2U);
        limits.maximum_studio_instances=std::max(limits.maximum_studio_instances,pairs*2U);
        render::EntityRenderFrameBuilder builder;
        std::optional<std::uint64_t> expected_signature;
        std::vector<double> microseconds;
        microseconds.reserve(samples);
        for(std::size_t iteration=0U;iteration<warmup+samples;++iteration) {
            auto staged=input;
            const auto start=std::chrono::steady_clock::now();
            const auto built=builder.build(*scene.package,std::move(staged),limits);
            const auto stop=std::chrono::steady_clock::now();
            INFO((built.error?built.error->context:std::string{}));
            REQUIRE(built);REQUIRE(built.frame);
            REQUIRE(built.frame->studio_instances().size()==pairs*2U);
            REQUIRE(built.frame->draw_commands().size()==pairs*4U);
            if(!expected_signature) expected_signature=built.frame->frame_signature();
            CHECK(built.frame->frame_signature()==*expected_signature);
            if(iteration>=warmup) microseconds.push_back(
                std::chrono::duration<double,std::micro>(stop-start).count());
        }
        std::sort(microseconds.begin(),microseconds.end());
        const auto quantile=[&](const std::size_t numerator,const std::size_t denominator) {
            return microseconds[(samples*numerator+denominator-1U)/denominator-1U];
        };
        std::cout<<std::setprecision(9)
            <<"[optimization-p5-bench] {\"fixture\":\"synthetic_studio_parent_child_v1\","
            <<"\"pairs\":"<<pairs<<",\"warmup\":"<<warmup<<",\"n\":"<<samples
            <<",\"unit\":\"us\",\"source_copy_timed\":false,\"p50\":"<<quantile(1U,2U)
            <<",\"p95\":"<<quantile(95U,100U)<<",\"p99\":"<<quantile(99U,100U)
            <<",\"max\":"<<microseconds.back()<<",\"frame_signature\":"<<*expected_signature
            <<",\"allocations_per_build\":null}\n";
    }
}
TEST_CASE("Entity render boundary exposes no paths snapshots or GL handles",
    "[entity-render][scene][boundary]")
{
    STATIC_REQUIRE_FALSE(
        HasNativePathOrRawSnapshot<render::EntitySceneRenderPackage>);
    STATIC_REQUIRE_FALSE(
        HasNativePathOrRawSnapshot<render::EntityRenderFrame>);
    STATIC_REQUIRE_FALSE(
        HasNativePathOrRawSnapshot<render::StudioEntityRenderInstance>);
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<render::EntitySceneRenderPackage>);
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<render::EntityRenderFrame>);
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<render::StudioRenderPose>);
}

TEST_CASE("Frame signature preserves scalar bits errors and delayed immutable readers",
    "[frame-hash-padding][entity-render][frame][ownership]")
{
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    std::uint32_t random=0x73A49D21U;
    const auto next=[&] {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    constexpr std::array<std::uint32_t,12> edge_bits{0U,0x80000000U,1U,0x80000001U,
        0x00800000U,0x80800000U,0x3F800000U,0x3F800001U,0x3F7FFFFFU,0x7F7FFFFFU,0xFF7FFFFFU,0xBF800000U};
    std::array<std::shared_ptr<const render::EntityRenderFrame>,5> readers;
    std::array<std::uint64_t,5> identities{};
    std::size_t accepted=0,rejected=0;
    for(std::uint32_t ordinal=0;ordinal<1024;++ordinal) {
        auto input=mixed_frame_input(*scene.package);
        input.resource_id=0x8000ULL+(static_cast<std::uint64_t>(next())<<16U);
        input.resource_revision=ordinal+1U;
        for(std::size_t i=0;i<16;++i) {
            auto bits=ordinal%3==0 ? edge_bits[(ordinal+i)%edge_bits.size()] : next();
            if((bits&0x7F800000U)==0x7F800000U)bits&=0xFF7FFFFFU;
            input.studio_poses[0].bone_matrices[0][i]=std::bit_cast<float>(bits);
        }
        input.studio_instances[0].body_value=next();
        input.studio_instances[0].skin_family_index=ordinal%2;
        const auto original_bits=std::bit_cast<std::uint32_t>(input.studio_poses[0].bone_matrices[0][0]);
        switch(ordinal%20) {
        case 0:input.studio_poses[0].bone_matrices[0][0]=std::numeric_limits<float>::quiet_NaN();break;
        case 1:input.studio_poses[0].bone_matrices[0][7]=std::numeric_limits<float>::infinity();break;
        case 2:input.studio_instances[0].opacity=std::numeric_limits<float>::quiet_NaN();break;
        case 3:input.studio_instances[0].entity_number=0;break;
        case 4:input.studio_instances[0].pose_index=999;break;
        case 5:input.resource_revision=0;break;
        case 6:++input.studio_poses[0].model_resource_identity.revision;break;
        case 7:input.studio_instances[0].skin_family_index=999;break;
        case 8:input.resource_id=0;break;
        case 9:input.studio_instances[0].blend_mode=static_cast<render::StudioBlendMode>(255);break;
        default:break;
        }
        auto result=render::EntityRenderFrameBuilder{}.build(*scene.package,input);
        if(result) {
            ++accepted;
            const auto index=ordinal%readers.size();
            identities[index]=result.frame->frame_signature();
            readers[index]=std::make_shared<const render::EntityRenderFrame>(std::move(*result.frame));
            input.studio_poses[0].bone_matrices[0][0]=17;
            input.studio_instances.clear();
            CHECK(std::bit_cast<std::uint32_t>(readers[index]->studio_poses()[0].bone_matrices[0][0])==original_bits);
            std::cout<<"[frame-hash-oracle] {\"case\":"<<ordinal<<",\"accepted\":true,\"signature\":\""<<identities[index]<<"\",\"draws\":"<<readers[index]->draw_commands().size()<<"}\n";
        } else {
            ++rejected;REQUIRE(result.error);
            std::cout<<"[frame-hash-oracle] {\"case\":"<<ordinal<<",\"accepted\":false,\"error\":"<<static_cast<unsigned>(result.error->code)<<",\"entity\":"<<result.error->entity_number.value_or(0)<<"}\n";
        }
        for(std::size_t i=0;i<readers.size();++i)if(readers[i])CHECK(readers[i]->frame_signature()==identities[i]);
    }
    CHECK(accepted>0);CHECK(rejected>0);
}

TEST_CASE("Early Studio omission is an explicit absent pose with two outside bounds",
    "[studio-early-selection][entity-render][frame]")
{
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    auto made_frustum=visibility::WorldViewFrustum::from_view_projection(renderer::RenderMatrix4{identity_matrix()});
    REQUIRE(made_frustum);REQUIRE(made_frustum.frustum);
    auto input=mixed_frame_input(*scene.package);
    input.sprite_instances.clear();input.unsupported_instances.clear();input.studio_poses.clear();
    auto& instance=input.studio_instances[0];
    instance.pose_index=std::numeric_limits<std::uint32_t>::max();
    instance.preparation_status=render::StudioRenderPreparationStatus::frustum_rejected_before_pose;
    instance.interpolated_bounds={{4.F,-.1F,-.1F},{5.F,.1F,.1F}};
    instance.early_frustum_bounds=instance.interpolated_bounds;
    input.view_frustum=&*made_frustum.frustum;
    bool accepted_case=false;

    SECTION("accepted omission retains identity but publishes no fabricated palette or draw") {
        const auto built=render::EntityRenderFrameBuilder{}.build(*scene.package,input);
        INFO((built.error?built.error->context:std::string{}));REQUIRE(built);REQUIRE(built.frame);
        REQUIRE(built.frame->studio_instances().size()==1U);
        CHECK(built.frame->studio_instances()[0].entity_number==2U);
        CHECK(built.frame->studio_instances()[0].pose_index==std::numeric_limits<std::uint32_t>::max());
        CHECK(built.frame->studio_instances()[0].visibility_status==render::RuntimeEntityVisibilityStatus::culled_by_frustum);
        CHECK(built.frame->studio_poses().empty());CHECK(built.frame->draw_commands().empty());
        CHECK(built.frame->statistics().total_bone_matrix_count==0U);
        const render::EntityRenderResourceIdentity package_identity{
            scene.package->resource_id(),scene.package->resource_revision()};
        CHECK(built.frame->scene_package_identity()==package_identity);
        const auto signature=built.frame->frame_signature();
        input.studio_instances[0].early_frustum_bounds->maximum.x=6.F;
        const auto changed=render::EntityRenderFrameBuilder{}.build(*scene.package,input);REQUIRE(changed);
        CHECK(changed.frame->frame_signature()!=signature);
        accepted_case=true;
    }
    SECTION("absent early envelope") {instance.early_frustum_bounds.reset();}
    SECTION("absent current final frustum") {input.view_frustum=nullptr;}
    SECTION("early envelope intersects while late bounds are outside") {
        instance.early_frustum_bounds=assets::WorldBounds{{.5F,-.1F,-.1F},{2.F,.1F,.1F}};
    }
    SECTION("early envelope is inside while late bounds are outside") {
        instance.early_frustum_bounds=assets::WorldBounds{{-.1F,-.1F,-.1F},{.1F,.1F,.1F}};
    }
    SECTION("late bounds inside cannot be hidden by different early envelope") {
        instance.interpolated_bounds={{-.1F,-.1F,-.1F},{.1F,.1F,.1F}};
    }
    SECTION("invalid early envelope") {instance.early_frustum_bounds->minimum.x=std::numeric_limits<float>::quiet_NaN();}
    SECTION("omission must not reference any palette") {instance.pose_index=0U;}
    SECTION("camera-exempt model cannot omit pose") {input.camera_cull_exempt_entity_number=2U;}
    SECTION("asset identity is still validated") {instance.studio_asset_index=999U;}
    SECTION("skin selection is still validated") {instance.skin_family_index=999U;}
    SECTION("invalid preparation enum") {instance.preparation_status=static_cast<render::StudioRenderPreparationStatus>(255U);}
    SECTION("complete path still rejects absent pose") {
        instance.preparation_status=render::StudioRenderPreparationStatus::complete;instance.early_frustum_bounds.reset();
    }
    SECTION("output resource identity must remain valid") {input.resource_revision=0U;}
    // Discover every sibling SECTION before leaving the accepted branch.
    if(accepted_case)return;
    const auto rejected=render::EntityRenderFrameBuilder{}.build(*scene.package,input);
    REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
}

TEST_CASE("Complete Studio preparation preserves established signature and palette validation",
    "[studio-early-selection][entity-render][frame][frame-hash-padding]")
{
    auto scene=fixture::scene_package(fixture::render_assets());REQUIRE(scene);
    auto input=mixed_frame_input(*scene.package);
    const auto original=render::EntityRenderFrameBuilder{}.build(*scene.package,input);REQUIRE(original);
    input.studio_instances[0].preparation_status=render::StudioRenderPreparationStatus::complete;
    const auto explicit_complete=render::EntityRenderFrameBuilder{}.build(*scene.package,input);REQUIRE(explicit_complete);
    CHECK(explicit_complete.frame->frame_signature()==original.frame->frame_signature());
    CHECK(explicit_complete.frame->studio_poses()[0].bone_matrices==original.frame->studio_poses()[0].bone_matrices);
    SECTION("complete instances cannot smuggle unhashed early bounds") {
        input.studio_instances[0].early_frustum_bounds=input.studio_instances[0].interpolated_bounds;
    }
    SECTION("complete pose keeps resource revision check") {++input.studio_poses[0].model_resource_identity.revision;}
    const auto rejected=render::EntityRenderFrameBuilder{}.build(*scene.package,input);
    REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
}

} // namespace
