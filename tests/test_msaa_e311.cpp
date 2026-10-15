#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"
#include <hlclient/core/presentation_settings.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace render=hlclient::renderer;
namespace platform=hlclient::platform;
namespace glfixture=hlclient::tests::entity_opengl_fixture;
namespace worldfixture=hlclient::tests::world_render_fixture;
namespace assets=hlclient::assets;
constexpr render::RenderExtent extent{320,240};
std::string evidence_root() {
#ifdef _WIN32
    char* value=nullptr;std::size_t length{};
    if(::_dupenv_s(&value,&length,"HLCLIENT_E311_AA_EVIDENCE_ROOT")!=0) {
        std::free(value);return {};
    }
    const std::unique_ptr<char,decltype(&std::free)> owned{value,&std::free};
    return value?std::string{value}:std::string{};
#else
    const auto* value=std::getenv("HLCLIENT_E311_AA_EVIDENCE_ROOT");
    return value?std::string{value}:std::string{};
#endif
}

std::vector<std::uint8_t> draw(glfixture::HiddenContext& context,const render::RenderScene& scene,
        render::RenderExtent size=extent) {
    context.renderer().render(scene,size);
    auto result=context.renderer().observe_framebuffer(size,scene.clear_color,true);
    REQUIRE(result);REQUIRE(glGetError()==GL_NO_ERROR);return std::move(result.rgba8);
}
std::array<std::uint8_t,4> pixel(const std::vector<std::uint8_t>& bytes,int x,int y,
        render::RenderExtent size=extent) {
    const auto index=(static_cast<std::size_t>(size.height-1-y)*size.width+x)*4;
    REQUIRE(index+3<bytes.size());return {bytes[index],bytes[index+1],bytes[index+2],bytes[index+3]};
}
void evidence(std::string_view name,const std::vector<std::uint8_t>& rgba,render::RenderExtent size=extent) {
    // Optional project-owned artifact output; no installed assets are opened.
    // Root must already exist. The explicit path is not part of runtime logs.
    const auto supplied=evidence_root();if(supplied.empty())return;
    const std::filesystem::path root{supplied};REQUIRE(root.is_absolute());REQUIRE(std::filesystem::is_directory(root));
    std::ofstream out(root/(std::string{name}+".ppm"),std::ios::binary);REQUIRE(out);
    out<<"P6\n"<<size.width<<' '<<size.height<<"\n255\n";
    for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x) {
        const auto rgb=pixel(rgba,x,y,size);out.write(reinterpret_cast<const char*>(rgb.data()),3);
    }
    REQUIRE(out.good());
}
void evidence_statistics(std::string_view name,const render::opengl::OpenGlSceneTargetStatistics& stats,
        std::size_t off_edges,std::size_t aa_edges) {
    const auto supplied=evidence_root();if(supplied.empty())return;
    const std::filesystem::path root{supplied};REQUIRE(root.is_absolute());REQUIRE(std::filesystem::is_directory(root));
    std::ofstream out(root/(std::string{name}+".json"),std::ios::binary);REQUIRE(out);
    out<<"{\"schema\":\"hlclient.msaa-offline-evidence.v1\",\"requested_samples\":"<<stats.requested_samples
        <<",\"actual_samples\":"<<stats.actual_samples<<",\"maximum_samples\":"<<stats.maximum_samples
        <<",\"framebuffer_complete\":"<<(stats.framebuffer_complete?"true":"false")
        <<",\"width\":"<<stats.scene_extent.width<<",\"height\":"<<stats.scene_extent.height
        <<",\"fallback\":\""<<render::opengl::to_string(stats.fallback_reason)<<"\",\"off_fractional_pixels\":"<<off_edges
        <<",\"msaa_fractional_pixels\":"<<aa_edges<<"}\n";
    REQUIRE(out.good());
}
render::RenderScene world_scene() {
    auto world=worldfixture::make_world();world.vertices.clear();world.indices.clear();
    const std::array<assets::AssetVector3,11> positions{{
        {-5,-3,0},{-1,3,0},{0,-2,0},
        {1,-3,0},{5,3,0},{4.86F,3.1F,0},{.86F,-2.9F,0},
        {1.8F,-3,0},{5.8F,3,0},{5.68F,3.1F,0},{1.68F,-2.9F,0}}};
    for(const auto p:positions)world.vertices.push_back({p,{0,0,1},{p.x,p.y}});
    world.indices={0,1,2,3,4,5,3,5,6,7,8,9,7,9,10};
    world.bounds={{-5,-3,0},{5.8F,3.1F,0}};world.surfaces[0].bounds=world.bounds;
    world.surfaces[0].first_vertex=0;world.surfaces[0].vertex_count=11;world.surfaces[0].index_count=15;
    world.statistics.emitted_vertex_count=11;world.statistics.emitted_triangle_count=5;
    auto texture=worldfixture::make_texture(false);
    for(auto& mip:texture.mip_levels)for(std::size_t i=0;i<mip.rgba_pixels.size();i+=4)
        for(unsigned c=0;c<3;++c)mip.rgba_pixels[i+c]=std::byte{220};
    assets::WorldMaterialTextureBinding binding;binding.material_index=0;
    binding.status=assets::WorldMaterialTextureBindingStatus::resolved_embedded;
    binding.texture_asset_index=0;binding.source_bsp_texture_index=0;
    std::vector<assets::WorldTextureAsset> textures;textures.push_back(std::move(texture));
    auto set=assets::WorldTextureSet::create(std::move(textures),{binding},{},1);REQUIRE(set);
    worldfixture::FixtureOptions options;options.unlit=true;
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        assets::TexturedWorldAsset{std::move(world),std::move(*set.texture_set)},
        worldfixture::make_lightmap_set(options));REQUIRE(built);
    render::RenderScene scene;scene.clear_color={0,0,0,1};scene.static_world.emplace();
    scene.static_world->package=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
    scene.camera.position={0,0,12};scene.camera.target={0,0,0};scene.camera.up={0,1,0};
    scene.camera.near_plane=.1F;scene.camera.far_plane=64;
    return scene;
}
std::size_t coverage(const std::vector<std::uint8_t>& bytes,unsigned maximum,int begin_x,int end_x) {
    std::size_t count{};
    for(int y=0;y<extent.height;++y)for(int x=begin_x;x<end_x;++x) {
        const auto value=static_cast<unsigned>(pixel(bytes,x,y)[0]);if(value>2U && value+2U<maximum)++count;
    }
    return count;
}
unsigned maximum_red(const std::vector<std::uint8_t>& bytes) {
    unsigned maximum{};for(std::size_t i=0;i<bytes.size();i+=4)maximum=std::max(maximum,static_cast<unsigned>(bytes[i]));return maximum;
}
void require_msaa(const render::opengl::OpenGlRenderer& renderer) {
    const auto& stats=renderer.scene_target_statistics();
    if(!stats.actual_samples) {
        const auto reason=stats.fallback_reason;
        if(reason==render::opengl::OpenGlMsaaFallback::unsupported_count ||
           reason==render::opengl::OpenGlMsaaFallback::attachment_samples_mismatch)
            SKIP("Actual GL context has no supported scene MSAA attachment count: "<<render::opengl::to_string(reason));
        FAIL("Small scene AA allocation/completeness failure is not a capability skip: "<<render::opengl::to_string(reason));
    }
    REQUIRE(stats.framebuffer_complete);REQUIRE(stats.actual_samples>=2);
}
}

TEST_CASE("E31.1 MSAA resolves fractional coverage on world diagonals and thin rails", "[e311][msaa][opengl][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();glDisable(GL_DITHER);
    auto scene=world_scene();const auto off=draw(*context,scene);const auto maximum=maximum_red(off);REQUIRE(maximum>100);
    scene.requested_msaa_samples=4;const auto aa=draw(*context,scene);require_msaa(context->renderer());
    // Uniform source material makes Off strictly binary. New interior values
    // on both separated ROIs prove sample coverage, not blur or any change.
    CHECK(coverage(off,maximum,0,extent.width/2)==0);
    CHECK(coverage(off,maximum,extent.width/2,extent.width)==0);
    CHECK(coverage(aa,maximum,0,extent.width/2)>30);
    CHECK(coverage(aa,maximum,extent.width/2,extent.width)>30);
    CHECK(maximum_red(aa)==maximum);
    CHECK(context->renderer().scene_target_statistics().scene_extent==extent);
    CHECK(context->renderer().scene_target_statistics().resolve_extent==extent);
    CHECK(context->renderer().scene_target_statistics().viewport==std::array<int,4>{0,0,extent.width,extent.height});
    evidence("msaa-world-off",off);evidence("msaa-world-4",aa);
    evidence_statistics("msaa-world",context->renderer().scene_target_statistics(),
        coverage(off,maximum,0,extent.width),coverage(aa,maximum,0,extent.width));
}

TEST_CASE("E31.1 MSAA covers production Studio silhouettes rather than only world geometry", "[e311][msaa][opengl][studio][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();glDisable(GL_DITHER);
    auto scene=glfixture::render_scene(glfixture::studio_scene());scene.clear_color={0,0,0,1};
    const auto off=draw(*context,scene);const auto maximum=maximum_red(off);REQUIRE(maximum>30);
    scene.requested_msaa_samples=4;const auto aa=draw(*context,scene);require_msaa(context->renderer());
    CHECK(coverage(aa,maximum,0,extent.width)>coverage(off,maximum,0,extent.width)+10);
    CHECK(maximum_red(aa)==maximum);
    evidence("msaa-studio-off",off);evidence("msaa-studio-4",aa);
    evidence_statistics("msaa-studio",context->renderer().scene_target_statistics(),
        coverage(off,maximum,0,extent.width),coverage(aa,maximum,0,extent.width));
}

TEST_CASE("E31.1 MSAA precedes one image adjustment and sharp native HUD", "[e311][msaa][opengl][hud][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();render::RenderScene scene;scene.clear_color={.25F,.36F,.49F,1};
    scene.basic_hud.emplace();scene.basic_hud->rectangles.push_back({10,10,1,18,{1,1,1,1}});
    scene.basic_hud->rectangles.push_back({20,10,15,1,{1,1,1,1}});
    const auto off=draw(*context,scene);
    scene.requested_msaa_samples=4;const auto neutral=draw(*context,scene);require_msaa(context->renderer());
    CHECK(off==neutral);
    scene.image_adjustments={.08F,1.7F,1.2F};const auto adjusted=draw(*context,scene);
    const auto original=pixel(neutral,80,80),result=pixel(adjusted,80,80);
    const auto expected=hlclient::core::adjusted_image_color({original[0]/255.F,original[1]/255.F,original[2]/255.F,1},scene.image_adjustments);
    for(unsigned c=0;c<3;++c)CHECK(std::abs(result[c]-expected[c]*255.F)<=2);
    CHECK(pixel(adjusted,10,15)==pixel(off,10,15));CHECK(pixel(adjusted,25,10)==pixel(off,25,10));
    CHECK(pixel(adjusted,9,15)==result);CHECK(pixel(adjusted,11,15)==result);
    CHECK(pixel(adjusted,25,9)==result);CHECK(pixel(adjusted,25,11)==result);
    CHECK(context->renderer().image_adjustment_statistics().adjusted_frames==1);
    scene.image_adjustments={};CHECK(draw(*context,scene)==off);
}

TEST_CASE("E31.1 scene sample settings and resizes keep bounded resources and native extents", "[e311][msaa][opengl][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();render::RenderScene scene;scene.clear_color={.1F,.2F,.3F,1};
    for(const auto size:std::array<render::RenderExtent,3>{{{320,240},{256,192},{320,240}}}) {
        REQUIRE(SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),size.width,size.height));REQUIRE(SDL_SyncWindow(SDL_GL_GetCurrentWindow()));
        int w{},h{};REQUIRE(SDL_GetWindowSizeInPixels(SDL_GL_GetCurrentWindow(),&w,&h));const render::RenderExtent pixels{w,h};
        for(const unsigned request:{0U,2U,4U,8U,4U,0U}) {
            scene.requested_msaa_samples=request;const auto first=draw(*context,scene,pixels);
            const auto stats=context->renderer().scene_target_statistics();
            CHECK(stats.requested_samples==request);CHECK(stats.actual_samples<=request);
            CHECK(hlclient::core::valid_msaa_samples(stats.actual_samples));
            CHECK(stats.scene_extent==pixels);CHECK(stats.resolve_extent==pixels);CHECK(stats.hud_extent==pixels);
            CHECK(stats.active_framebuffers==(stats.actual_samples?2U:0U));CHECK(stats.active_attachments==(stats.actual_samples?3U:0U));
            CHECK(stats.framebuffer_complete==(stats.actual_samples!=0));
            CHECK(stats.storage_allocations-stats.storage_releases==(stats.actual_samples?1U:0U));
            if(stats.actual_samples<request)CHECK(stats.fallback_reason!=render::opengl::OpenGlMsaaFallback::none);
            if(stats.actual_samples==request && request)CHECK(stats.fallback_reason==render::opengl::OpenGlMsaaFallback::none);
            CHECK(draw(*context,scene,pixels)==first);
            CHECK(context->renderer().scene_target_statistics().storage_allocations==stats.storage_allocations);
        }
    }
    context->release_renderer();context->initialize_renderer();CHECK_FALSE(draw(*context,scene).empty());
    CHECK(context->renderer().scene_target_statistics().active_attachments==0);
}

TEST_CASE("E31.1 oversized AA allocation budget falls back to working Off", "[e311][msaa][opengl][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();render::RenderScene scene;scene.requested_msaa_samples=8;
    context->renderer().render(scene,{8192,4096});REQUIRE(glGetError()==GL_NO_ERROR);
    const auto stats=context->renderer().scene_target_statistics();CHECK(stats.actual_samples==0);
    CHECK(stats.active_framebuffers==0);CHECK(stats.active_attachments==0);
    CHECK(stats.fallback_reason!=render::opengl::OpenGlMsaaFallback::none);
    CHECK_FALSE(draw(*context,scene).empty());
}

TEST_CASE("E31.1 inherited stencil and scissor cannot corrupt fresh scene attachments", "[e311][msaa][opengl][state][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();auto scene=world_scene();scene.requested_msaa_samples=4;
    glEnable(GL_STENCIL_TEST);glStencilFunc(GL_NEVER,7,0xFF);glStencilMask(0);
    glEnable(GL_SCISSOR_TEST);glScissor(1,1,1,1);
    glDepthMask(GL_FALSE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    const auto pixels=draw(*context,scene);
    CHECK(maximum_red(pixels)>100);CHECK(glIsEnabled(GL_STENCIL_TEST)==GL_TRUE);
    CHECK(glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE);std::array<GLint,4> box{};glGetIntegerv(GL_SCISSOR_BOX,box.data());
    CHECK(box==std::array<GLint,4>{1,1,1,1});GLint function{};glGetIntegerv(GL_STENCIL_FUNC,&function);CHECK(function==GL_NEVER);
    GLboolean depth_mask{};std::array<GLboolean,4> color_mask{};
    glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);glGetBooleanv(GL_COLOR_WRITEMASK,color_mask.data());
    CHECK(depth_mask==GL_FALSE);CHECK(color_mask==std::array<GLboolean,4>{GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE});
    glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glStencilMask(0xFF);
    glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
}

TEST_CASE("E31.1 scene target restores caller framebuffer MRT and texture state", "[e311][msaa][opengl][state][no-stock]") {
    auto context=glfixture::try_context(extent.width,extent.height);if(!context)SKIP("OpenGL unavailable");
    context->initialize_renderer();
    std::array<GLuint,2> fbos{};std::array<GLuint,3> colors{};GLuint texture{},unpack{};
    glGenFramebuffers(2,fbos.data());glGenRenderbuffers(3,colors.data());glGenTextures(1,&texture);glGenBuffers(1,&unpack);
    for(unsigned i=0;i<3;++i){glBindRenderbuffer(GL_RENDERBUFFER,colors[i]);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,extent.width,extent.height);}
    glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,colors[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_RENDERBUFFER,colors[1]);
    const std::array<GLenum,2> destinations{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,destinations.data());
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER,fbos[1]);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,colors[2]);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[0]);glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindRenderbuffer(GL_RENDERBUFFER,colors[2]);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glDisable(GL_MULTISAMPLE);
    render::RenderScene scene;scene.clear_color={.25F,.5F,.75F,1};scene.requested_msaa_samples=4;
    context->renderer().render(scene,extent);REQUIRE(glGetError()==GL_NO_ERROR);
    GLint actual{};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&actual);CHECK(actual==static_cast<GLint>(fbos[0]));
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&actual);CHECK(actual==static_cast<GLint>(fbos[1]));
    glGetIntegerv(GL_DRAW_BUFFER0,&actual);CHECK(actual==GL_COLOR_ATTACHMENT0);
    glGetIntegerv(GL_DRAW_BUFFER1,&actual);CHECK(actual==GL_COLOR_ATTACHMENT1);
    glGetIntegerv(GL_READ_BUFFER,&actual);CHECK(actual==GL_COLOR_ATTACHMENT0);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&actual);CHECK(actual==static_cast<GLint>(colors[2]));
    glGetIntegerv(GL_ACTIVE_TEXTURE,&actual);CHECK(actual==GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&actual);CHECK(actual==static_cast<GLint>(texture));
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&actual);CHECK(actual==static_cast<GLint>(unpack));
    CHECK(glIsEnabled(GL_MULTISAMPLE)==GL_FALSE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glBindRenderbuffer(GL_RENDERBUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glBindTexture(GL_TEXTURE_2D,0);glDeleteTextures(1,&texture);glDeleteBuffers(1,&unpack);glDeleteRenderbuffers(3,colors.data());glDeleteFramebuffers(2,fbos.data());
}
