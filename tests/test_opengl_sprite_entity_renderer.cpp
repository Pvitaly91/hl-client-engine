#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {

namespace fixture = hlclient::tests::entity_opengl_fixture;
namespace entity = hlclient::entity_render;
namespace renderer = hlclient::renderer;
namespace visibility = hlclient::world_visibility;
namespace world_fixture = hlclient::tests::world_render_fixture;

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame>
single_sprite_frame(
    const entity::EntitySceneRenderPackage& package,
    const std::uint64_t revision,
    const hlclient::assets::AssetVector3 origin,
    const float scale,
    const hlclient::assets::WorldBounds bounds,
    const visibility::WorldViewFrustum* view_frustum = nullptr,
    entity::SpriteBlendMode blend = entity::SpriteBlendMode::source_asset_default,
    const float opacity = 1.0F,
    const std::uint32_t selected_frame = 0U)
{
    REQUIRE(package.sprite_assets().size() == 1U);
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0xE520U;
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
    entity::SpriteEntityRenderInstance instance;
    instance.entity_number = 1U;
    instance.sprite_asset_index = 0U;
    instance.selected_frame_index = selected_frame;
    instance.transform.origin = origin;
    instance.transform.uniform_scale = scale;
    instance.orientation = package.sprite_assets()[0U]->orientation();
    instance.texture_format_support =
        package.sprite_assets()[0U]->texture_support_status();
    instance.bounds = bounds;
    instance.blend_mode = blend;
    instance.opacity = opacity;
    input.sprite_instances.push_back(instance);
    input.view_frustum = view_frustum;
    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

TEST_CASE("OpenGL Sprite entities share textures and retain frame uploads",
    "[renderer][opengl][entity-render][sprite][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto first = fixture::sprite_scene(1U, 0U);
    context->renderer().render(fixture::render_scene(first), {96, 96});
    const auto first_pixels = fixture::framebuffer();
    const auto first_statistics = context->renderer().entity_statistics();
    CHECK(first_statistics.sprite_asset_upload_count == 1U);
    CHECK(first_statistics.studio_asset_upload_count == 0U);
    CHECK(first_statistics.sprite_draw_count == 2U);
    CHECK(first_statistics.visible_entity_count == 2U);
    CHECK(fixture::has_non_clear_pixel(first_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));

    auto second = fixture::SceneAndFrame{
        first.package,
        fixture::make_frame(*first.package, 2U, 0U, 2U, 0.0F, 1U),
    };
    context->renderer().render(fixture::render_scene(second), {96, 96});
    const auto second_statistics = context->renderer().entity_statistics();
    CHECK(second_statistics.sprite_asset_upload_count == 1U);
    CHECK(second_statistics.entity_frame_revision == 2U);
    CHECK(second_statistics.sprite_draw_count == 4U);
    CHECK(second_statistics.sprite_texture_bind_count == 4U);
    CHECK(glGetError() == GL_NO_ERROR);

    context->renderer().render({}, {96, 96});
    CHECK_FALSE(context->renderer().entity_statistics().active_entity_resources);
    CHECK(context->renderer()
              .entity_statistics()
              .entity_resource_release_count == 1U);
    context->release_renderer();
}

TEST_CASE("OpenGL alpha-test Sprite discards transparent texels",
    "[renderer][opengl][entity-render][sprite][alpha-test][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto masked = fixture::sprite_scene(1U,
        0U,
        hlclient::assets::SpriteTextureFormat::alpha_test);
    auto clear_scene = fixture::render_scene(masked);
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();

    context->renderer().render(fixture::render_scene(masked), {96, 96});
    const auto masked_pixels = fixture::framebuffer();
    const auto statistics = context->renderer().entity_statistics();

    CHECK(masked.package->sprite_assets()[0U]->render_profile() ==
        hlclient::entity_render::SpriteRenderTextureProfile::alpha_test_masked);
    CHECK(statistics.sprite_asset_upload_count == 1U);
    CHECK(statistics.sprite_draw_count == 2U);
    CHECK(masked_pixels == clear_pixels);
    CHECK(glGetError() == GL_NO_ERROR);

    context->release_renderer();
}

[[nodiscard]] fixture::SceneAndFrame e16_controlled_sprite(
    const hlclient::assets::SpriteTextureFormat format,
    const entity::SpriteBlendMode blend = entity::SpriteBlendMode::source_asset_default,
    const float opacity = 1.0F,
    const std::uint32_t selected_frame = 0U,
    const hlclient::assets::SpriteOrientation orientation = hlclient::assets::SpriteOrientation::view_parallel)
{
    namespace source_fixture = hlclient::tests::entity_render_fixture;
    auto render_assets = source_fixture::render_assets(false,true,format,false,orientation);
    auto source = *render_assets.sources.sprite;
    auto& metadata = *source.source_data;
    metadata.palette.colors[255U] = {200U,100U,50U};
    for (std::size_t ordinal=0U;ordinal<metadata.indexed_frames.size();++ordinal) {
        auto& frame = metadata.indexed_frames[ordinal];
        for (std::size_t pixel=0U;pixel<frame.indexed_pixels.size();++pixel) {
            const bool colored = pixel % frame.width >= frame.width/2U;
            frame.indexed_pixels[pixel] = colored ? std::byte{128U} : std::byte{0U};
            if (format!=hlclient::assets::SpriteTextureFormat::index_alpha) {
                const auto color = ordinal==0U ? std::array{100U,50U,25U} : std::array{25U,100U,50U};
                for (std::size_t channel=0U;channel<3U;++channel)
                    frame.derived_rgba8[pixel*4U+channel] = static_cast<std::byte>(colored ? color[channel] : 0U);
                frame.derived_rgba8[pixel*4U+3U] = std::byte{255U};
            }
        }
    }
    auto built = entity::SpriteRenderAssetBuilder{}.build(source,render_assets.sprite->source_identity());
    REQUIRE(built);
    render_assets.sprite = std::make_shared<const entity::SpriteRenderAsset>(std::move(*built.asset));
    auto package_result = source_fixture::scene_package(std::move(render_assets));
    REQUIRE(package_result);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(std::move(*package_result.package));
    auto frame = single_sprite_frame(*package,1U,{},1.0F,{{-2,0,-3},{2,0,3}},nullptr,blend,opacity,selected_frame);
    return {std::move(package),std::move(frame)};
}

[[nodiscard]] std::size_t e16_pixel_at(const renderer::RenderCamera& camera,
    const hlclient::assets::AssetVector3 world)
{
    const auto matrix = renderer::camera_view_projection(camera,{96,96});
    REQUIRE(matrix);
    const auto clip = renderer::transform(*matrix.matrix,{world.x,world.y,world.z,1.0F});
    REQUIRE(clip.w>0.0F);
    const auto x = static_cast<int>(std::floor((clip.x/clip.w+1.0F)*48.0F));
    const auto y = static_cast<int>(std::floor((clip.y/clip.w+1.0F)*48.0F));
    REQUIRE(x>=0);REQUIRE(x<96);REQUIRE(y>=0);REQUIRE(y<96);
    return (static_cast<std::size_t>(y)*96U+static_cast<std::size_t>(x))*4U;
}

TEST_CASE("E16 OpenGL Sprite blends additive and index-alpha coverage without background quads",
    "[e16][renderer][opengl][entity-render][sprite][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    for (const auto format : {
             hlclient::assets::SpriteTextureFormat::additive,
             hlclient::assets::SpriteTextureFormat::index_alpha}) {
        const auto orientation=format==hlclient::assets::SpriteTextureFormat::index_alpha
            ? hlclient::assets::SpriteOrientation::view_parallel_oriented : hlclient::assets::SpriteOrientation::view_parallel;
        auto controlled = e16_controlled_sprite(format,entity::SpriteBlendMode::source_asset_default,.5F,0U,orientation);
        auto scene = fixture::render_scene(controlled);
        scene.camera.position={0,-12,0};scene.camera.target={0,0,0};
        scene.clear_color={.08F,.10F,.12F,1.0F};
        auto clear_scene = scene;
        clear_scene.dynamic_entities.reset();
        context->renderer().render(clear_scene, {96, 96});
        const auto clear_pixels = fixture::framebuffer();

        context->renderer().render(
            scene, {96, 96});
        const auto pixels = fixture::framebuffer();
        const auto neutral = e16_pixel_at(scene.camera,{-1.5F,0,0});
        const auto colored = e16_pixel_at(scene.camera,{1.5F,0,0});
        CHECK(controlled.frame->statistics().unsupported_visual_count == 0U);
        REQUIRE(controlled.frame->draw_commands().size() == 1U);
        for (std::size_t channel=0U;channel<3U;++channel) {
            CHECK(pixels[neutral+channel] == clear_pixels[neutral+channel]);
            const auto background=std::to_integer<int>(clear_pixels[colored+channel]);
            const double expected=format==hlclient::assets::SpriteTextureFormat::additive
                ? background + std::array{100.0,50.0,25.0}[channel]*.5
                : background*(1.0-128.0/255.0*.5) + std::array{200.0,100.0,50.0}[channel]*128.0/255.0*.5;
            CHECK(std::abs(std::to_integer<int>(pixels[colored+channel])-expected)<2.0);
        }
        GLboolean depth_write{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);
        CHECK(depth_write==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
        CHECK(glGetError() == GL_NO_ERROR);
        context->renderer().render(clear_scene,{96,96});
        CHECK(std::ranges::equal(fixture::framebuffer(),clear_pixels));
    }
    CHECK(context->renderer().entity_statistics().sprite_draw_count == 2U);

    context->release_renderer();
}

TEST_CASE("E16 OpenGL transient sprite overrides retain opaque flags and distinct animated frames",
    "[e16][renderer][opengl][entity-render][sprite][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable on this host");
    context->initialize_renderer();
    auto controlled=e16_controlled_sprite(hlclient::assets::SpriteTextureFormat::additive,
        entity::SpriteBlendMode::opaque);
    auto scene=fixture::render_scene(controlled);
    scene.camera.position={0,-12,0};scene.camera.target={0,0,0};
    context->renderer().render(scene,{96,96});
    const auto first=fixture::framebuffer();
    const auto neutral=e16_pixel_at(scene.camera,{-1.5F,0,0});
    const auto colored=e16_pixel_at(scene.camera,{1.5F,0,0});
    CHECK(first[neutral]==std::byte{0U});
    CHECK(std::to_integer<int>(first[colored])==100);
    auto next=single_sprite_frame(*controlled.package,2U,{},1.0F,{{-2,0,-3},{4,0,3}},nullptr,
        entity::SpriteBlendMode::opaque,1.0F,1U);
    scene.dynamic_entities->frame=next;
    context->renderer().render(scene,{96,96});
    const auto second=fixture::framebuffer();
    CHECK(second!=first);
    CHECK(std::to_integer<int>(second[colored+1U])==100);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count==1U);
    GLboolean depth_write{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);
    CHECK(depth_write==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);CHECK(glGetError()==GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Sprite depth testing preserves an occluding world surface",
    "[renderer][opengl][entity-render][sprite][world-depth][world-glow-occlusion][actual-context]")
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
    auto format=hlclient::assets::SpriteTextureFormat::normal;
    auto blend=entity::SpriteBlendMode::source_asset_default;
    SECTION("opaque") {}
    SECTION("additive") {format=hlclient::assets::SpriteTextureFormat::additive;}
    SECTION("index coverage") {format=hlclient::assets::SpriteTextureFormat::index_alpha;}
    SECTION("world glow") {
        format=hlclient::assets::SpriteTextureFormat::additive;
        blend=entity::SpriteBlendMode::glow;
    }
    auto entities = e16_controlled_sprite(format);
    entities.frame = single_sprite_frame(*entities.package,
        1U,
        {8.0F, 12.0F, -4.0F},
        0.10F,
        {{6.5F, 10.5F, -5.5F}, {9.5F, 13.5F, -2.5F}},nullptr,blend);
    auto entity_scene = fixture::render_scene(entities);
    entity_scene.camera.position = {8.0F, -12.0F, 6.0F};
    entity_scene.camera.target = {8.0F, 12.0F, -4.0F};

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
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count ==
        1U);
    // The same sprite in front of the surface must remain visible, then its
    // removal must restore the untouched world color and draw state.
    combined_scene.dynamic_entities->frame=single_sprite_frame(*entities.package,
        2U,{8.0F,0.0F,1.0F},0.10F,
        {{6.5F,-1.5F,-0.5F},{9.5F,1.5F,2.5F}},nullptr,blend);
    context->renderer().render(combined_scene,{96,96});
    CHECK(fixture::framebuffer()!=world_pixels);
    GLboolean depth_write{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);
    CHECK(depth_write==GL_TRUE);
    CHECK(glIsEnabled(GL_DEPTH_TEST)==GL_TRUE);
    CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    context->renderer().render(world_scene,{96,96});
    CHECK(fixture::framebuffer()==world_pixels);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Sprite consumes CPU frustum suppression",
    "[renderer][opengl][entity-render][sprite][frustum][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    renderer::RenderMatrix4 identity;
    auto made_frustum = visibility::WorldViewFrustum::from_view_projection(
        identity);
    REQUIRE(made_frustum);
    auto entities = fixture::sprite_scene();
    entities.frame = single_sprite_frame(*entities.package,
        1U,
        {},
        1.0F,
        {{2.0F, 2.0F, 2.0F}, {3.0F, 3.0F, 3.0F}},
        &*made_frustum.frustum);
    CHECK(entities.frame->statistics().culled_by_frustum_count == 1U);
    CHECK(entities.frame->statistics().visible_count == 0U);
    CHECK(entities.frame->draw_commands().empty());

    auto clear_scene = fixture::render_scene(entities);
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();
    context->renderer().render(fixture::render_scene(entities), {96, 96});
    CHECK(fixture::framebuffer() == clear_pixels);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count ==
        1U);
    CHECK(context->renderer().entity_statistics().sprite_draw_count == 0U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("E16 neutral sprite frame validates and hashes finite blend overrides",
    "[e16][entity-render][sprite][frame]")
{
    const auto scene=e16_controlled_sprite(hlclient::assets::SpriteTextureFormat::additive);
    entity::EntityRenderFrameBuildInput input;
    input.resource_id=0xE16U;input.resource_revision=1U;
    input.interpolation={0,0,0,0,1,1,entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.sprite_instances.assign(scene.frame->sprite_instances().begin(),scene.frame->sprite_instances().end());
    const auto baseline=entity::EntityRenderFrameBuilder{}.build(*scene.package,input);
    REQUIRE(baseline);
    input.sprite_instances.front().blend_mode=entity::SpriteBlendMode::opaque;
    const auto opaque=entity::EntityRenderFrameBuilder{}.build(*scene.package,input);
    REQUIRE(opaque);
    CHECK(opaque.frame->frame_signature()!=baseline.frame->frame_signature());
    input.sprite_instances.front().opacity=.5F;
    const auto translucent=entity::EntityRenderFrameBuilder{}.build(*scene.package,input);
    REQUIRE(translucent);
    CHECK(translucent.frame->frame_signature()!=opaque.frame->frame_signature());
    for (const auto opacity : {-1.0F,1.1F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        input.sprite_instances.front().opacity=opacity;
        const auto rejected=entity::EntityRenderFrameBuilder{}.build(*scene.package,input);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error->code==entity::EntityRenderFrameErrorCode::invalid_instance);
    }
    input.sprite_instances.front().opacity=1.0F;
    input.sprite_instances.front().blend_mode=static_cast<entity::SpriteBlendMode>(255U);
    const auto rejected=entity::EntityRenderFrameBuilder{}.build(*scene.package,input);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error->code==entity::EntityRenderFrameErrorCode::invalid_instance);
}

TEST_CASE("OpenGL Sprite rejects unsupported and degenerate billboard bases",
    "[renderer][opengl][entity-render][sprite][orientation][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto unsupported = fixture::sprite_scene(1U,
        0U,
        hlclient::assets::SpriteTextureFormat::normal,
        hlclient::assets::SpriteOrientation::facing_upright);
    CHECK(unsupported.frame->statistics().unsupported_visual_count == 2U);
    CHECK(unsupported.frame->draw_commands().empty());
    auto clear_scene = fixture::render_scene(unsupported);
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();
    context->renderer().render(fixture::render_scene(unsupported), {96, 96});
    CHECK(fixture::framebuffer() == clear_pixels);
    CHECK(context->renderer().entity_statistics().sprite_draw_count == 0U);

    auto package_result = hlclient::tests::entity_render_fixture::scene_package(
        hlclient::tests::entity_render_fixture::render_assets(true,
            true,
            hlclient::assets::SpriteTextureFormat::normal,
            false,
            hlclient::assets::SpriteOrientation::view_parallel_upright));
    REQUIRE(package_result);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    fixture::SceneAndFrame degenerate{
        package,
        fixture::make_frame(*package, 2U, 1U, 1U),
    };
    REQUIRE(degenerate.frame->draw_commands().size() == 3U);
    CHECK(degenerate.frame->draw_commands().front().visual_kind ==
        entity::RuntimeEntityVisualKind::studio_model);
    CHECK(degenerate.frame->draw_commands()[1U].visual_kind ==
        entity::RuntimeEntityVisualKind::studio_model);
    CHECK(degenerate.frame->draw_commands().back().visual_kind ==
        entity::RuntimeEntityVisualKind::sprite);

    const auto valid_scene = fixture::render_scene(degenerate);
    context->renderer().render(valid_scene, {96, 96});
    const auto retained = context->renderer().entity_statistics();

    auto degenerate_scene = valid_scene;
    degenerate_scene.camera.position = {0.0F, 0.0F, 0.0F};
    degenerate_scene.camera.target = {0.0F, 0.0F, 1.0F};
    degenerate_scene.camera.up = {1.0F, 0.0F, 0.0F};
    try {
        context->renderer().render(degenerate_scene, {96, 96});
        FAIL("Degenerate upright Sprite basis was not rejected");
    } catch (const hlclient::renderer::opengl::OpenGlRendererError& error) {
        CHECK(error.code() == hlclient::renderer::opengl::
            OpenGlRendererErrorCode::entity_draw_invalid);
    }

    const auto after_failure = context->renderer().entity_statistics();
    CHECK(after_failure.entity_scene_revision == retained.entity_scene_revision);
    CHECK(after_failure.entity_frame_revision == retained.entity_frame_revision);
    CHECK(after_failure.studio_asset_upload_count ==
        retained.studio_asset_upload_count);
    CHECK(after_failure.sprite_asset_upload_count ==
        retained.sprite_asset_upload_count);
    CHECK(after_failure.entity_frame_count == retained.entity_frame_count);
    CHECK(after_failure.studio_draw_count == retained.studio_draw_count);
    CHECK(after_failure.sprite_draw_count == retained.sprite_draw_count);
    CHECK(after_failure.model_texture_bind_count ==
        retained.model_texture_bind_count);
    CHECK(after_failure.sprite_texture_bind_count ==
        retained.sprite_texture_bind_count);
    CHECK(after_failure.pose_ubo_update_count == retained.pose_ubo_update_count);
    CHECK(after_failure.visible_entity_count == retained.visible_entity_count);
    CHECK(after_failure.entity_resource_release_count ==
        retained.entity_resource_release_count);
    CHECK(after_failure.entity_scene_present == retained.entity_scene_present);
    CHECK(after_failure.active_entity_resources ==
        retained.active_entity_resources);
    CHECK(glGetError() == GL_NO_ERROR);

    context->renderer().render(valid_scene, {96, 96});
    const auto recovered = context->renderer().entity_statistics();
    CHECK(recovered.entity_scene_revision == retained.entity_scene_revision);
    CHECK(recovered.entity_frame_revision == retained.entity_frame_revision);
    CHECK(recovered.studio_asset_upload_count ==
        retained.studio_asset_upload_count);
    CHECK(recovered.sprite_asset_upload_count ==
        retained.sprite_asset_upload_count);
    CHECK(recovered.entity_frame_count == retained.entity_frame_count + 1U);
    CHECK(recovered.studio_draw_count == retained.studio_draw_count + 4U);
    CHECK(recovered.sprite_draw_count == retained.sprite_draw_count + 1U);
    CHECK(recovered.model_texture_bind_count ==
        retained.model_texture_bind_count + 4U);
    CHECK(recovered.sprite_texture_bind_count ==
        retained.sprite_texture_bind_count + 1U);
    CHECK(recovered.pose_ubo_update_count ==
        retained.pose_ubo_update_count + 1U);
    CHECK(recovered.visible_entity_count == retained.visible_entity_count);
    CHECK(recovered.entity_resource_release_count ==
        retained.entity_resource_release_count);
    CHECK(recovered.entity_scene_present);
    CHECK(recovered.active_entity_resources);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

} // namespace
