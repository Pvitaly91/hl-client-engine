#include <hlclient/entity_render/entity_scene_render.hpp>

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include "entity_render/entity_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
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
             assets::SpriteOrientation::facing_upright,
             assets::SpriteOrientation::view_parallel_oriented}) {
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

} // namespace
