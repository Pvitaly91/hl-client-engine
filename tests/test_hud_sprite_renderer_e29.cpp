#include "entity_render/entity_opengl_test_support.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include <hlclient/entity_render/sprite_render_asset.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {
namespace api=hlclient::game_api;
namespace renderer=hlclient::renderer;
namespace entity=hlclient::entity_render;
namespace fixture=hlclient::tests::entity_opengl_fixture;
namespace wire=hlclient::tests::sprite_fixture;

std::shared_ptr<const entity::SpriteRenderAsset> sheet(std::uint64_t id=29U,
        std::uint64_t revision=1U,std::int32_t format=1) {
    auto bytes=wire::literal_header_and_palette(2,format,2,0,8,4);
    const auto palette=[&](const unsigned index,const std::array<unsigned,3> rgb) {
        for(unsigned channel=0;channel<3;++channel)
            bytes[wire::kPaletteOffset+index*3U+channel]=std::byte{static_cast<std::uint8_t>(rgb[channel])};
    };
    palette(0,{0,0,0}); palette(1,{200,100,40}); palette(2,{40,160,220});
    palette(3,{255,255,255}); palette(4,{255,0,0}); palette(255,{200,100,40});
    for(unsigned frame=0;frame<2;++frame) {
        std::vector<std::byte> pixels(32,std::byte{4});
        const std::array<unsigned,8> crop{frame?2U:1U,format==3?255U:0U,2U,0U,0U,3U,0U,2U};
        for(unsigned row=0;row<2;++row) for(unsigned col=0;col<4;++col)
            pixels[(row+1)*8+col+2]=std::byte{static_cast<std::uint8_t>(crop[row*4+col])};
        wire::append_single_entry(bytes,{0,4,8,4,std::move(pixels)});
    }
    auto parsed=hlclient::goldsrc::sprite::GoldSrcSpriteParser::parse(bytes);
    REQUIRE(parsed);
    hlclient::assets::SpriteAsset source;
    source.frames=std::move(parsed.document->compatibility_frames);
    source.source_data=std::move(parsed.document->source_data);
    auto built=entity::SpriteRenderAssetBuilder{}.build(source,{id,revision});
    REQUIRE(built);
    return std::make_shared<const entity::SpriteRenderAsset>(std::move(*built.asset));
}
api::HudSprite command(std::uint64_t id=29U,std::uint64_t revision=1U) {
    api::HudSprite sprite;
    sprite.resource_id=id; sprite.resource_revision=revision;
    sprite.source={2,1,4,2}; sprite.x=20; sprite.y=20; sprite.width=40; sprite.height=20;
    sprite.color={1,1,1,1}; sprite.blend=api::HudSpriteBlend::additive;
    return sprite;
}
renderer::RenderScene scene(std::shared_ptr<const entity::SpriteRenderAsset> asset=sheet()) {
    renderer::RenderScene result;
    result.clear_color={20.0F/255,30.0F/255,40.0F/255,1};
    result.basic_hud.emplace(); result.basic_hud->sprites.push_back(command());
    result.hud_sprite_bindings.push_back({asset->source_identity().resource_id,
        asset->source_identity().revision,std::move(asset)});
    return result;
}
std::array<unsigned,3> pixel(const renderer::opengl::OpenGlFramebufferObservation& image,
        renderer::RenderExtent extent,int x,int y) {
    const auto at=4U*(static_cast<std::size_t>(extent.height-1-y)*extent.width+x);
    REQUIRE(at+2U<image.rgba8.size());
    return {image.rgba8[at],image.rgba8[at+1],image.rgba8[at+2]};
}
void assert_hud_pixel_near(const std::array<unsigned,3> actual,const std::array<unsigned,3> expected) {
    for(unsigned c=0;c<3;++c) CHECK(std::abs(static_cast<int>(actual[c])-static_cast<int>(expected[c]))<=2);
}
auto read(fixture::HiddenContext& context,const renderer::RenderScene& render,
        renderer::RenderExtent extent={96,96}) {
    context.renderer().render(render,extent);
    auto pixels=context.renderer().observe_framebuffer(extent,render.clear_color,true);
    REQUIRE(pixels); CHECK(glGetError()==GL_NO_ERROR);
    return pixels;
}

TEST_CASE("E29 neutral HUD commands reject bad coordinates materials and bounded overflow",
          "[e29][hud][renderer][no-stock]") {
    api::HudDrawCommands draw; draw.sprites.push_back(command());
    CHECK(api::valid_hud_draw_commands(draw));
    draw.sprites[0].x=-20; CHECK(api::valid_hud_draw_commands(draw));
    for(const auto invalid:{0.0F,-1.0F,std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),32769.0F}) {
        auto bad=draw; bad.sprites[0].width=invalid; CHECK_FALSE(api::valid_hud_draw_commands(bad));
    }
    auto bad=draw; bad.sprites[0].resource_revision=0; CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=draw; bad.sprites[0].source.width=0; CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=draw; bad.sprites[0].blend=static_cast<api::HudSpriteBlend>(255); CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=draw; bad.sprites[0].color[2]=1.1F; CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=draw; bad.sprites[0].clip=api::HudRectangle{0,0,0,10,{}}; CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=draw; bad.sprites.resize(api::kMaximumHudSprites,command()); CHECK(api::valid_hud_draw_commands(bad));
    bad.sprites.push_back(command()); CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad={}; bad.rectangles.resize(64); CHECK(api::valid_hud_draw_commands(bad));
    bad.rectangles.resize(65); CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=draw; bad.sprites[0].width=16384; bad.sprites[0].height=16384;
    CHECK_FALSE(api::valid_hud_draw_commands(bad));
    renderer::null::NullRenderer null;
    null.initialize();
    null.render(scene(),{1280,720}); CHECK(null.statistics().rendered_frames==1);
}

TEST_CASE("E29 production HUD subrect keeps palette additive background and atlas neighbors correct",
          "[e29][hud][renderer][opengl][actual-context]") {
    auto context=fixture::try_context();
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer(); auto render=scene();
    const auto first=read(*context,render);
    assert_hud_pixel_near(pixel(first,{96,96},25,25),{220,130,80});
    assert_hud_pixel_near(pixel(first,{96,96},35,25),{20,30,40}); // black inside sprite rectangle is neutral
    assert_hud_pixel_near(pixel(first,{96,96},45,25),{60,190,255});
    assert_hud_pixel_near(pixel(first,{96,96},35,35),{255,255,255});
    assert_hud_pixel_near(pixel(first,{96,96},19,25),{20,30,40});
    assert_hud_pixel_near(pixel(first,{96,96},60,25),{20,30,40});
    // Last source texel stays cyan, not the adjacent red atlas tile.
    assert_hud_pixel_near(pixel(first,{96,96},59,39),{60,190,255});
    CHECK(context->renderer().hud_statistics().texture_upload_count==1);
    CHECK(context->renderer().hud_statistics().last_rendered_sprite_count==1);
    const auto repeated=read(*context,render);
    CHECK(repeated.color_signature==first.color_signature);
    CHECK(context->renderer().hud_statistics().texture_upload_count==1);
    render.basic_hud->sprites[0].frame=1;
    const auto second=read(*context,render);
    assert_hud_pixel_near(pixel(second,{96,96},25,25),{60,190,255});
    CHECK(second.color_signature!=first.color_signature);
    CHECK(context->renderer().hud_statistics().texture_upload_count==2);
    render.basic_hud.reset();
    const auto removed=read(*context,render);
    CHECK(removed.non_clear_pixel_count==0);
}

TEST_CASE("E29 production HUD tint masked coverage and index alpha do not produce opaque backgrounds",
          "[e29][hud][renderer][opengl][actual-context]") {
    auto context=fixture::try_context();
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer(); auto render=scene(sheet(29,1,3));
    auto& sprite=render.basic_hud->sprites[0]; sprite.blend=api::HudSpriteBlend::alpha_test;
    sprite.color={0.5F,1,1,0.5F};
    const auto masked=read(*context,render);
    assert_hud_pixel_near(pixel(masked,{96,96},25,25),{60,65,40});
    assert_hud_pixel_near(pixel(masked,{96,96},35,25),{20,30,40}); // index255 coverage survives GPU upload
    render=scene(sheet(29,2,2));
    render.basic_hud->sprites[0]=command(29,2);
    render.basic_hud->sprites[0].blend=api::HudSpriteBlend::straight_alpha;
    const auto alpha=read(*context,render);
    assert_hud_pixel_near(pixel(alpha,{96,96},35,25),{20,30,40}); // index0 alpha is transparent, not opaque
    CHECK(context->renderer().hud_statistics().texture_upload_count==2);
}

TEST_CASE("E29 HUD interleaves draw order and top-left clip without leaving GL state behind",
          "[e29][hud][renderer][opengl][actual-context]") {
    auto context=fixture::try_context();
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer(); auto render=scene();
    render.basic_hud->sprites[0].clip=api::HudRectangle{20,20,20,20,{}};
    render.basic_hud->sprites[0].draw_order=1;
    render.basic_hud->rectangles.push_back({20,20,10,10,{0,1,0,1},2});
    render.basic_hud->rectangles.push_back({20,20,40,20,{0.1F,0.1F,0.1F,1},0});
    glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE); glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glBlendEquationSeparate(GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT);
    glBlendFuncSeparate(GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_ONE,GL_ZERO);
    glScissor(3,4,5,6); glActiveTexture(GL_TEXTURE3);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8); glPixelStorei(GL_UNPACK_ROW_LENGTH,9);
    const auto clipped=read(*context,render);
    assert_hud_pixel_near(pixel(clipped,{96,96},25,25),{0,255,0});
    assert_hud_pixel_near(pixel(clipped,{96,96},45,25),{26,26,26}); // clip prohibits sprite but not backdrop
    CHECK(glIsEnabled(GL_DEPTH_TEST)==GL_TRUE); CHECK(glIsEnabled(GL_CULL_FACE)==GL_TRUE);
    CHECK(glIsEnabled(GL_BLEND)==GL_FALSE); CHECK(glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE);
    GLboolean depth_mask{}; glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask); CHECK(depth_mask==GL_FALSE);
    GLint value{}; glGetIntegerv(GL_DEPTH_FUNC,&value); CHECK(value==GL_GREATER);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,&value); CHECK(value==GL_FUNC_SUBTRACT);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&value); CHECK(value==GL_FUNC_REVERSE_SUBTRACT);
    glGetIntegerv(GL_BLEND_SRC_RGB,&value); CHECK(value==GL_DST_COLOR);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&value); CHECK(value==GL_TEXTURE3);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&value); CHECK(value==8);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value); CHECK(value==9);
    std::array<GLint,4> clip{}; glGetIntegerv(GL_SCISSOR_BOX,clip.data());
    CHECK(clip==std::array<GLint,4>{3,4,5,6});
    // Restore explicit test-only state before the next ordinary frame.
    glDepthMask(GL_TRUE); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
    glBlendEquation(GL_FUNC_ADD); glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4); glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    render.basic_hud->sprites[0].clip.reset();
    render.basic_hud->sprites[0].draw_order=3;
    const auto reordered=read(*context,render);
    assert_hud_pixel_near(pixel(reordered,{96,96},25,25),{200,255,40});
    CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("E29 HUD rejects missing stale crop and frame bindings before texture upload",
          "[e29][hud][renderer][opengl][actual-context]") {
    auto context=fixture::try_context();
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer(); auto render=scene();
    render.basic_hud->sprites[0].source={7,1,2,2};
    CHECK(read(*context,render).non_clear_pixel_count==0);
    CHECK(context->renderer().hud_statistics().last_invalid_crop==1);
    render.basic_hud->sprites[0]=command(); render.basic_hud->sprites[0].frame=2;
    CHECK(read(*context,render).non_clear_pixel_count==0);
    CHECK(context->renderer().hud_statistics().last_invalid_frame==1);
    render.basic_hud->sprites[0]=command(29,9);
    CHECK(read(*context,render).non_clear_pixel_count==0);
    CHECK(context->renderer().hud_statistics().last_revision_mismatch==1);
    render.basic_hud->sprites[0]=command(999,1);
    CHECK(read(*context,render).non_clear_pixel_count==0);
    CHECK(context->renderer().hud_statistics().last_missing_binding==1);
    CHECK(context->renderer().hud_statistics().texture_upload_count==0);
    render.basic_hud->sprites[0]=command();
    read(*context,render); CHECK(context->renderer().hud_statistics().texture_upload_count==1);
    render=scene(sheet(29,2)); render.basic_hud->sprites[0]=command(29,2);
    CHECK(read(*context,render).non_clear_pixel_count>0);
    CHECK(context->renderer().hud_statistics().texture_upload_count==2);
    render.hud_sprite_bindings.push_back(render.hud_sprite_bindings[0]);
    CHECK(read(*context,render).non_clear_pixel_count==0);
    CHECK(context->renderer().hud_statistics().last_invalid_commands==1);
    render.hud_sprite_bindings.pop_back();
    render.hud_sprite_bindings.resize(renderer::kMaximumHudSpriteBindings+1);
    CHECK(read(*context,render).non_clear_pixel_count==0);
    CHECK(context->renderer().hud_statistics().last_invalid_commands==1);
    CHECK(context->renderer().hud_statistics().texture_upload_count==2);
}

TEST_CASE("E29 screen-space HUD preserves the independent world sprite upload and subsequent frame",
          "[e29][hud][renderer][opengl][actual-context][sprite]") {
    auto context=fixture::try_context();
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    auto render=fixture::render_scene(fixture::sprite_scene());
    const auto baseline=read(*context,render);
    CHECK(baseline.non_clear_pixel_count>0);
    const auto world_uploads=context->renderer().entity_statistics().sprite_asset_upload_count;
    render.basic_hud.emplace(); render.basic_hud->sprites.push_back(command());
    render.hud_sprite_bindings.push_back({29,1,sheet()});
    const auto overlay=read(*context,render);
    CHECK(overlay.color_signature!=baseline.color_signature);
    CHECK(context->renderer().hud_statistics().last_rendered_sprite_count==1);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count==world_uploads);
    render.basic_hud.reset(); render.hud_sprite_bindings.clear();
    const auto after=read(*context,render);
    CHECK(after.color_signature==baseline.color_signature);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count==world_uploads);
}

TEST_CASE("E29 HUD texture cache remains bounded across immutable resource revisions",
          "[e29][hud][renderer][opengl][actual-context]") {
    auto context=fixture::try_context();
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    for(std::uint64_t id=1;id<=130;++id) {
        auto render=scene(sheet(id,1)); render.basic_hud->sprites[0]=command(id,1);
        context->renderer().render(render,{96,96});
        CHECK(context->renderer().hud_statistics().cached_texture_count<=renderer::kMaximumHudSpriteBindings);
        CHECK(context->renderer().hud_statistics().cached_texture_bytes<=renderer::kMaximumHudSpriteTextureBytes);
    }
    CHECK(context->renderer().hud_statistics().texture_upload_count==130);
    CHECK(context->renderer().hud_statistics().cached_texture_count==128);
    CHECK(context->renderer().hud_statistics().cached_texture_bytes==128*8*4*4);
    CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("E29 HUD uses framebuffer pixel space at legacy HD and ultrawide extents",
          "[e29][hud][renderer][opengl][actual-context][resize]") {
    for(const renderer::RenderExtent extent : {renderer::RenderExtent{640,480},{1280,720},
            {1920,1080},{3440,1440},{1279,719}}) {
        INFO(extent.width); INFO(extent.height);
        auto context=fixture::try_context(extent.width,extent.height);
        if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
        context->initialize_renderer(); auto render=scene();
        auto& sprite=render.basic_hud->sprites[0];
        sprite.x=std::floor((extent.width-sprite.width)/2.0F);
        sprite.y=std::floor((extent.height-sprite.height)/2.0F);
        const auto centered=read(*context,render,extent);
        assert_hud_pixel_near(pixel(centered,extent,static_cast<int>(sprite.x)+5,static_cast<int>(sprite.y)+5),{220,130,80});
        assert_hud_pixel_near(pixel(centered,extent,static_cast<int>(sprite.x)+15,static_cast<int>(sprite.y)+5),{20,30,40});
        assert_hud_pixel_near(pixel(centered,extent,static_cast<int>(sprite.x)-1,static_cast<int>(sprite.y)+5),{20,30,40});
        CHECK(context->renderer().hud_statistics().last_rendered_sprite_count==1);
        CHECK(centered.minimum_x>=static_cast<int>(sprite.x));
        CHECK(centered.maximum_x<static_cast<int>(sprite.x+sprite.width));
        // Device extent changes recompute neutral coordinates, not sprite source tier.
        const renderer::RenderExtent smaller{extent.width/2,extent.height/2};
        sprite.x=10; sprite.y=12;
        const auto resized=read(*context,render,smaller);
        assert_hud_pixel_near(pixel(resized,smaller,15,17),{220,130,80});
        CHECK(context->renderer().hud_statistics().texture_upload_count==1);
    }
}
} // namespace
