#include <hlclient/interactive_preview/interactive_entity_visibility.hpp>

#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include "entity_render/entity_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace entity = hlclient::entity_render;
namespace fixture = hlclient::tests::entity_render_fixture;
namespace preview = hlclient::interactive_preview;
namespace renderer = hlclient::renderer;
namespace spatial = hlclient::world_spatial;
namespace visibility = hlclient::world_visibility;

template <typename Type>
concept HasOpenGlHandle = requires(const Type& value) {
    value.vao;
    value.vbo;
    value.ebo;
    value.gl_id;
};

template <typename Type>
concept HasExternalIoState =
    requires(const Type& value) { value.native_path; } ||
    requires(const Type& value) { value.socket; } ||
    requires(const Type& value) { value.sdl_window; };

[[nodiscard]] std::array<float, 16U> identity_matrix()
{
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] renderer::RenderCamera camera_along_x(const float direction)
{
    renderer::RenderCamera camera;
    camera.position = {0.0F, 0.0F, 0.0F};
    camera.target = {direction, 0.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.vertical_field_of_view_radians = 1.57079632679F;
    camera.near_plane = 0.1F;
    camera.far_plane = 16.0F;
    return camera;
}

[[nodiscard]] std::shared_ptr<const entity::EntitySceneRenderPackage>
make_scene_package(const std::uint64_t resource_id = 0x7100U)
{
    auto assets = fixture::render_assets();
    entity::EntitySceneRenderPackageCreateInfo input;
    input.asset_library = assets.sources.library;
    input.asset_library_identity = {
        assets.sources.library->resource_id(),
        assets.sources.library->resource_revision()};
    input.resource_id = resource_id;
    input.studio_assets.push_back(std::move(assets.studio));
    input.sprite_assets.push_back(std::move(assets.sprite));
    auto built = entity::EntitySceneRenderPackageBuilder{}.build(
        std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*built.package));
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

    spatial::WorldSpatialLeaf positive_x;
    positive_x.source_leaf_index = 1U;
    positive_x.bounds = {{0.0F, -4.0F, -4.0F}, {4.0F, 4.0F, 4.0F}};
    positive_x.pvs_row_index = 0U;
    positive_x.pvs_bit_addressable = true;
    positive_x.surface_membership.source_leaf_index = 1U;

    spatial::WorldSpatialLeaf negative_x;
    negative_x.source_leaf_index = 2U;
    negative_x.bounds = {{-4.0F, -4.0F, -4.0F}, {0.0F, 4.0F, 4.0F}};
    negative_x.pvs_row_index = 1U;
    negative_x.pvs_bit_addressable = true;
    negative_x.surface_membership.source_leaf_index = 2U;

    return spatial::WorldSpatialPackage{
        {{{1.0F, 0.0F, 0.0F}, 0.0F, 0}},
        {node},
        {solid, positive_x, negative_x},
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

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame> make_source_frame(
    const entity::EntitySceneRenderPackage& package,
    const renderer::RenderCamera& camera,
    const spatial::WorldSpatialPackage* spatial_package = nullptr,
    const std::optional<std::uint32_t> camera_leaf_index = std::nullopt)
{
    auto made_frustum = visibility::WorldViewFrustum::from_camera(
        camera, {320, 200});
    REQUIRE(made_frustum);
    REQUIRE(made_frustum.frustum);

    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0x8100U;
    input.resource_revision = 7U;
    input.interpolation = {
        0.5, 0.0, 1.0, 0.5F, 0x10U, 0x11U,
        entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back({
        package.studio_assets()[0U]->source_identity(), {identity_matrix()}});

    entity::StudioEntityRenderInstance studio;
    studio.entity_number = 1U;
    studio.studio_asset_index = 0U;
    studio.pose_index = 0U;
    studio.transform.origin = {3.0F, 0.0F, 0.0F};
    studio.interpolated_bounds = {
        {2.5F, -0.25F, -0.25F}, {3.5F, 0.25F, 0.25F}};
    input.studio_instances.push_back(studio);

    entity::SpriteEntityRenderInstance sprite;
    sprite.entity_number = 2U;
    sprite.sprite_asset_index = 0U;
    sprite.selected_frame_index = 0U;
    sprite.transform.origin = {-3.0F, 0.0F, 0.0F};
    sprite.orientation = package.sprite_assets()[0U]->orientation();
    sprite.bounds = {
        {-3.5F, -0.25F, -0.25F}, {-2.5F, 0.25F, 0.25F}};
    input.sprite_instances.push_back(sprite);

    input.unsupported_instances.push_back({
        3U,
        std::nullopt,
        entity::UnsupportedEntityVisualReason::unsupported_asset_kind,
        entity::RuntimeEntityVisibilityStatus::unsupported_visual,
    });
    input.view_frustum = &*made_frustum.frustum;
    input.spatial_package = spatial_package;
    input.camera_leaf_index = camera_leaf_index;

    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.frame);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

[[nodiscard]] preview::InteractiveEntityVisibilityRefilterInput refilter_input(
    const renderer::RenderCamera& camera,
    const std::uint64_t revision = 8U)
{
    preview::InteractiveEntityVisibilityRefilterInput input;
    input.camera = camera;
    input.extent = {320, 200};
    input.output_frame_revision = revision;
    return input;
}

TEST_CASE("Interactive entity visibility rebuilds from reset frustum candidates",
    "[interactive-preview][entity-visibility][frustum]")
{
    const auto package = make_scene_package();
    const auto source = make_source_frame(*package, camera_along_x(1.0F));
    REQUIRE(source->studio_instances().size() == 1U);
    REQUIRE(source->sprite_instances().size() == 1U);
    CHECK(source->studio_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::visible);
    CHECK(source->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_frustum);

    const auto source_signature = source->frame_signature();
    const auto result = preview::InteractiveEntityVisibilityRefilter{}.refilter(
        *package, *source, refilter_input(camera_along_x(-1.0F), 0x22U));
    INFO((result.error ? result.error->context : std::string_view{}));
    REQUIRE(result);
    REQUIRE(result.frame);

    CHECK(result.frame->resource_id() == source->resource_id());
    CHECK(result.frame->resource_revision() == 0x22U);
    CHECK(result.frame->scene_package_identity() ==
        source->scene_package_identity());
    CHECK(result.frame->interpolation().sample_time_seconds ==
        source->interpolation().sample_time_seconds);
    CHECK(result.frame->studio_instances()[0U].entity_number == 1U);
    CHECK(result.frame->studio_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_frustum);
    CHECK(result.frame->sprite_instances()[0U].entity_number == 2U);
    CHECK(result.frame->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::visible);
    CHECK(result.frame->unsupported_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::unsupported_visual);
    CHECK(result.frame->statistics().visible_count == 1U);
    CHECK(result.frame->frame_signature() != source_signature);
    CHECK(result.statistics.source_candidate_count == 3U);
    CHECK(result.statistics.reset_culled_by_pvs_count == 0U);
    CHECK(result.statistics.reset_culled_by_frustum_count == 1U);
    CHECK(result.statistics.result_visible_count == 1U);

    // Refiltering is functional: the source remains the published old view.
    CHECK(source->resource_revision() == 7U);
    CHECK(source->frame_signature() == source_signature);
    CHECK(source->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_frustum);
}

TEST_CASE("Interactive entity visibility resets old PVS results before a new leaf",
    "[interactive-preview][entity-visibility][pvs]")
{
    const auto package = make_scene_package();
    const auto spatial_package = make_spatial_package();
    const auto source = make_source_frame(
        *package, camera_along_x(1.0F), &spatial_package, 1U);
    REQUIRE(source->sprite_instances().size() == 1U);
    CHECK(source->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_pvs);

    auto input = refilter_input(camera_along_x(-1.0F), 0x23U);
    input.spatial_package = &spatial_package;
    input.camera_leaf_index = 2U;
    const auto result = preview::InteractiveEntityVisibilityRefilter{}.refilter(
        *package, *source, input);
    INFO((result.error ? result.error->context : std::string_view{}));
    REQUIRE(result);
    REQUIRE(result.frame);
    CHECK(result.statistics.reset_culled_by_pvs_count == 1U);
    CHECK(result.statistics.reset_culled_by_frustum_count == 0U);
    CHECK(result.frame->studio_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_pvs);
    CHECK(result.frame->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::visible);
    CHECK(source->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_pvs);
}

TEST_CASE("Interactive entity visibility retains one synthetic camera anchor",
    "[interactive-preview][entity-visibility][controlled-entity][frustum]")
{
    const auto package = make_scene_package();
    const auto source = make_source_frame(*package, camera_along_x(1.0F));
    const auto source_signature = source->frame_signature();

    auto input = refilter_input(camera_along_x(-1.0F), 0x24U);
    input.retained_entity_number = 1U;
    const auto result = preview::InteractiveEntityVisibilityRefilter{}.refilter(
        *package, *source, input);
    INFO((result.error ? result.error->context : std::string_view{}));
    REQUIRE(result);
    REQUIRE(result.frame);
    REQUIRE(result.frame->studio_instances().size() == 1U);
    REQUIRE(result.frame->sprite_instances().size() == 1U);
    CHECK(result.frame->studio_instances()[0U].entity_number == 1U);
    CHECK(result.frame->studio_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::visible);
    CHECK(result.frame->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::visible);
    CHECK(result.frame->statistics().visible_count == 2U);
    CHECK(result.frame->statistics().draw_count == 3U);
    CHECK(result.frame->frame_signature() != source_signature);

    CHECK(source->frame_signature() == source_signature);
    CHECK(source->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_frustum);
}

TEST_CASE("Interactive entity visibility failures are typed and transactional",
    "[interactive-preview][entity-visibility][errors]")
{
    const auto package = make_scene_package(0x7100U);
    const auto source = make_source_frame(*package, camera_along_x(1.0F));
    const auto source_signature = source->frame_signature();
    const preview::InteractiveEntityVisibilityRefilter refilter;

    SECTION("zero output revision")
    {
        const auto result = refilter.refilter(
            *package, *source, refilter_input(camera_along_x(-1.0F), 0U));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                invalid_output_frame_revision);
    }

    SECTION("retained camera-anchor entity number must be nonzero")
    {
        auto input = refilter_input(camera_along_x(-1.0F), 0x24U);
        input.retained_entity_number = 0U;
        const auto result = refilter.refilter(*package, *source, input);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                invalid_retained_entity_number);
    }

    SECTION("output revision must advance beyond its source")
    {
        const auto equal = refilter.refilter(*package,
            *source,
            refilter_input(
                camera_along_x(-1.0F), source->resource_revision()));
        REQUIRE_FALSE(equal);
        REQUIRE(equal.error);
        CHECK_FALSE(equal.frame);
        CHECK(equal.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                invalid_output_frame_revision);

        const auto lower = refilter.refilter(*package,
            *source,
            refilter_input(
                camera_along_x(-1.0F), source->resource_revision() - 1U));
        REQUIRE_FALSE(lower);
        REQUIRE(lower.error);
        CHECK_FALSE(lower.frame);
        CHECK(lower.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                invalid_output_frame_revision);
    }

    SECTION("scene package mismatch")
    {
        const auto other_package = make_scene_package(0x7200U);
        const auto result = refilter.refilter(*other_package,
            *source,
            refilter_input(camera_along_x(-1.0F), 0x24U));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                scene_package_mismatch);
    }

    SECTION("camera cannot produce a frustum")
    {
        auto invalid_camera = camera_along_x(-1.0F);
        invalid_camera.target = invalid_camera.position;
        const auto result = refilter.refilter(
            *package, *source, refilter_input(invalid_camera, 0x24U));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                frustum_creation_failed);
        CHECK(result.error->frustum_error ==
            visibility::WorldViewFrustumErrorCode::invalid_camera);
    }

    SECTION("spatial package and leaf are an exact pair")
    {
        auto input = refilter_input(camera_along_x(-1.0F), 0x24U);
        input.camera_leaf_index = 1U;
        const auto result = refilter.refilter(*package, *source, input);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                invalid_world_spatial_context);
    }

    SECTION("invalid camera leaf is rejected before rebuilding")
    {
        const auto spatial_package = make_spatial_package();
        auto input = refilter_input(camera_along_x(-1.0F), 0x24U);
        input.spatial_package = &spatial_package;
        input.camera_leaf_index = 999U;
        const auto result = refilter.refilter(*package, *source, input);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                invalid_world_spatial_context);
    }

    SECTION("frame builder rejection publishes nothing")
    {
        auto input = refilter_input(camera_along_x(-1.0F), 0x24U);
        input.limits.maximum_entities = 1U;
        input.limits.maximum_studio_instances = 1U;
        input.limits.maximum_sprite_instances = 1U;
        input.limits.maximum_pose_count = 1U;
        input.limits.maximum_pose_evaluations_per_update = 1U;
        const auto result = refilter.refilter(*package, *source, input);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK_FALSE(result.frame);
        CHECK(result.error->code == preview::
            InteractiveEntityVisibilityRefilterErrorCode::
                frame_rebuild_failed);
        CHECK(result.error->frame_builder_error ==
            entity::EntityRenderFrameErrorCode::source_limit_exceeded);
        CHECK(result.statistics.source_candidate_count == 0U);
        CHECK(result.statistics.reset_culled_by_frustum_count == 0U);
    }

    CHECK(source->frame_signature() == source_signature);
    CHECK(source->resource_revision() == 7U);
    CHECK(source->sprite_instances()[0U].visibility_status ==
        entity::RuntimeEntityVisibilityStatus::culled_by_frustum);
}

TEST_CASE("Interactive entity visibility boundary is renderer and IO neutral",
    "[interactive-preview][entity-visibility][boundary]")
{
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<
        preview::InteractiveEntityVisibilityRefilterInput>);
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<
        preview::InteractiveEntityVisibilityRefilterResult>);
    STATIC_REQUIRE_FALSE(HasExternalIoState<
        preview::InteractiveEntityVisibilityRefilterInput>);
    STATIC_REQUIRE_FALSE(HasExternalIoState<
        preview::InteractiveEntityVisibilityRefilterResult>);
    STATIC_REQUIRE(std::is_same_v<
        decltype(preview::InteractiveEntityVisibilityRefilterResult{}.frame),
        std::shared_ptr<const entity::EntityRenderFrame>>);

    CHECK(preview::to_string(preview::
        InteractiveEntityVisibilityRefilterErrorCode::frame_rebuild_failed) ==
        std::string_view{"frame_rebuild_failed"});
    CHECK(preview::to_string(static_cast<preview::
        InteractiveEntityVisibilityRefilterErrorCode>(0xFF)) ==
        std::string_view{"unknown"});
}

} // namespace
