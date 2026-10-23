#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/renderer/render_camera_math.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>

namespace {
namespace r = hlclient::renderer;
namespace glf = hlclient::tests::entity_opengl_fixture;
namespace wf = hlclient::tests::world_render_fixture;
constexpr r::RenderExtent extent{96,96};

// This is a dispatch seam over a real GL context, not a substitute renderer.
// A modern host accepts the post-3.3 indexed sampler query, so reject only that
// pname before reaching its driver. Every legal indexed query still reaches GL.
struct StrictGl33Dispatch final {
    inline static StrictGl33Dispatch* current{};
    PFNGLGETINTEGERI_VPROC indexed{glad_glGetIntegeri_v};
    PFNGLGETINTEGERVPROC integer{glad_glGetIntegerv};
    PFNGLGETERRORPROC error{glad_glGetError};
    PFNGLDRAWARRAYSPROC draw{glad_glDrawArrays};
    unsigned rejected_sampler_queries{}, scalar_sampler_queries{}, legal_indexed_queries{}, draws{};
    GLenum pending_error{GL_NO_ERROR};
    bool fail_next_draw{},fail_next_sampler_query{};

    StrictGl33Dispatch() {
        REQUIRE(current==nullptr);
        current=this;
        glad_glGetIntegeri_v=&get_indexed;
        glad_glGetIntegerv=&get_integer;
        glad_glGetError=&get_error;
        glad_glDrawArrays=&draw_arrays;
    }
    ~StrictGl33Dispatch() {
        glad_glGetIntegeri_v=indexed;
        glad_glGetIntegerv=integer;
        glad_glGetError=error;
        glad_glDrawArrays=draw;
        current=nullptr;
    }
    static void GLAD_API_PTR get_indexed(GLenum pname,GLuint index,GLint* value) {
        auto& self=*current;
        if(pname==GL_SAMPLER_BINDING) {
            ++self.rejected_sampler_queries;
            if(self.pending_error==GL_NO_ERROR)self.pending_error=GL_INVALID_ENUM;
            return;
        }
        ++self.legal_indexed_queries;
        self.indexed(pname,index,value);
    }
    static void GLAD_API_PTR get_integer(GLenum pname,GLint* value) {
        if(pname==GL_SAMPLER_BINDING)++current->scalar_sampler_queries;
        current->integer(pname,value);
        if(pname==GL_SAMPLER_BINDING && current->fail_next_sampler_query) {
            current->fail_next_sampler_query=false;
            glBindTexture(0xffffffffU,0);
        }
    }
    static GLenum GLAD_API_PTR get_error() {
        if(current->pending_error!=GL_NO_ERROR) {
            const auto result=current->pending_error;
            current->pending_error=GL_NO_ERROR;
            return result;
        }
        return current->error();
    }
    static void GLAD_API_PTR draw_arrays(GLenum mode,GLint first,GLsizei count) {
        auto& self=*current;++self.draws;
        self.draw(mode,first,count);
        if(self.fail_next_draw) {
            self.fail_next_draw=false;
            // Deliberately use the real driver's error flag. Production's
            // ordinary require_no_gl_error throws while its guards are alive.
            glBindTexture(0xffffffffU,0);
        }
    }
};

struct HostSamplerState final {
    std::array<GLuint,2> names{};
    HostSamplerState() {
        glGenSamplers(2,names.data());
        REQUIRE(names[0]!=0);REQUIRE(names[1]!=0);REQUIRE(names[0]!=names[1]);
        glBindSampler(0,names[0]);glBindSampler(2,names[1]);
        glActiveTexture(GL_TEXTURE5);
        REQUIRE(glGetError()==GL_NO_ERROR);
    }
    ~HostSamplerState() {
        glBindSampler(0,0);glBindSampler(2,0);glDeleteSamplers(2,names.data());
        glActiveTexture(GL_TEXTURE0);
    }
    void check() const {
        GLint active{},zero{},two{};
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        CHECK(active==GL_TEXTURE5);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_SAMPLER_BINDING,&zero);
        glActiveTexture(GL_TEXTURE2);glGetIntegerv(GL_SAMPLER_BINDING,&two);
        glActiveTexture(static_cast<GLenum>(active));
        CHECK(zero==static_cast<GLint>(names[0]));
        CHECK(two==static_cast<GLint>(names[1]));
        CHECK(glGetError()==GL_NO_ERROR);
    }
};

r::RenderScene scene_with_world() {
    r::RenderScene scene;
    scene.camera.position={8,8,16};scene.camera.target={8,8,0};scene.camera.up={0,1,0};
    auto built=wf::make_package({.unlit=true});REQUIRE(built);REQUIRE(built.package);
    scene.static_world.emplace();
    scene.static_world->package=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
    return scene;
}

r::RenderWorldDecals decal() {
    r::RenderWorldDecals result;
    auto texture=wf::make_texture(false);
    texture.alpha_mode=hlclient::assets::WorldTextureAlphaMode::straight_alpha;
    result.texture=std::make_shared<const hlclient::assets::WorldTextureAsset>(std::move(texture));
    result.vertices=std::make_shared<const std::vector<r::RenderDecalVertex>>(
        std::initializer_list<r::RenderDecalVertex>{{{1,1,.02F},{0,0}},{{15,1,.02F},{1,0}},{{1,15,.02F},{0,1}}});
    result.revision=1;
    result.model_transform=r::RenderMatrix4{};
    return result;
}

std::string injected_draw_failure(glf::HiddenContext& context,const r::RenderScene& scene) {
    try {context.renderer().render(scene,extent);}
    catch(const r::opengl::OpenGlRendererError& error) {
        const std::string message=error.what();
        CHECK(message.find("invalid_enum")!=std::string::npos);
        CHECK(message.find("resolve")==std::string::npos);
        return message;
    }
    FAIL("Injected actual-driver draw error did not fail at the production draw boundary");
}

void add_ui(r::RenderScene& scene,unsigned path) {
    if(path==0) {
        scene.basic_hud.emplace();
        scene.basic_hud->rectangles.push_back({4,4,12,8,{1,0,0,1},10});
    }else if(path==1) {
        scene.screen_ui.emplace();
        scene.screen_ui->commands.emplace_back(r::ScreenUiText{"GL 3.3",4,4});
    }else if(path==2) {
        scene.image_adjustments={.1F,.8F,1.2F};
    }else if(path==3) {
        scene.screen_ui.emplace();
        auto pixels=std::make_shared<hlclient::assets::ImageAsset>();
        pixels->width=pixels->height=2;pixels->pixels.assign(16,std::byte{0xff});
        scene.screen_ui->commands.emplace_back(r::ScreenUiImage{pixels,4,4,20,20});
    }else {
        auto sky=std::make_shared<hlclient::assets::SkyboxAsset>();
        for(auto& face:sky->faces){face.width=face.height=2;face.pixels.assign(16,std::byte{0x80});}
        scene.skybox=sky;
    }
}
}

TEST_CASE("Strict GL33 dispatch rejects only indexed sampler queries and forwards legal UBO queries",
    "[renderer][opengl][gl33-sampler][contract][no-stock]") {
    auto context=glf::try_context();
    REQUIRE(context);REQUIRE(glf::capable_context());context->initialize_renderer();
    HostSamplerState host;
    StrictGl33Dispatch strict;
    GLint invalid=-77;
    glGetIntegeri_v(GL_SAMPLER_BINDING,0,&invalid);
    CHECK(invalid==-77);CHECK(glGetError()==GL_INVALID_ENUM);
    CHECK(strict.rejected_sampler_queries==1);
    GLint binding=-1;glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_SAMPLER_BINDING,&binding);
    CHECK(binding==static_cast<GLint>(host.names[0]));CHECK(glGetError()==GL_NO_ERROR);
    glActiveTexture(GL_TEXTURE5);
    GLuint buffer{};glGenBuffers(1,&buffer);glBindBuffer(GL_UNIFORM_BUFFER,buffer);
    glBufferData(GL_UNIFORM_BUFFER,64,nullptr,GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,buffer);
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&binding);
    CHECK(binding==static_cast<GLint>(buffer));CHECK(strict.legal_indexed_queries==1);
    CHECK(glGetError()==GL_NO_ERROR);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,0);glBindBuffer(GL_UNIFORM_BUFFER,0);glDeleteBuffers(1,&buffer);
    host.check();
}

TEST_CASE("Production empty startup frame honors GL33 sampler query contract",
    "[renderer][opengl][gl33-sampler][startup][no-stock]") {
    auto context=glf::try_context();
    REQUIRE(context);REQUIRE(glf::capable_context());context->initialize_renderer();
    HostSamplerState host;
    StrictGl33Dispatch strict;
    const r::RenderScene empty;
    REQUIRE_NOTHROW(context->renderer().render(empty,extent));
    CHECK(strict.rejected_sampler_queries==0);
    CHECK(strict.scalar_sampler_queries>=1);
    host.check();
}

TEST_CASE("First frame sampler-query error is attributed before scene resolve with state restored",
    "[renderer][opengl][gl33-sampler][startup][first-error][no-stock]") {
    auto context=glf::try_context();
    REQUIRE(context);REQUIRE(glf::capable_context());context->initialize_renderer();
    HostSamplerState host;
    StrictGl33Dispatch strict;
    strict.fail_next_sampler_query=true;
    try {
        context->renderer().render(r::RenderScene{},extent);
        FAIL("Injected scalar sampler-query error was not reported");
    }catch(const r::opengl::OpenGlRendererError& error) {
        const std::string message=error.what();INFO(message);
        CHECK(message.find("SkyDrawState_sampler_snapshot")!=std::string::npos);
        CHECK(message.find("frame=1")!=std::string::npos);
        CHECK(message.find("context=")!=std::string::npos);
        CHECK(message.find("invalid_enum")!=std::string::npos);
        CHECK(message.find("error=1280")!=std::string::npos);
        CHECK(message.find("Native scene resolve")==std::string::npos);
    }
    CHECK_FALSE(strict.fail_next_sampler_query);
    CHECK(strict.rejected_sampler_queries==0);
    host.check();
}

TEST_CASE("Production HUD font image adjustments and sky restore samplers on normal and failing draws",
    "[renderer][opengl][gl33-sampler][state][no-stock]") {
    auto context=glf::try_context();
    REQUIRE(context);REQUIRE(glf::capable_context());context->initialize_renderer();
    for(unsigned path=0;path<5;++path) {
        CAPTURE(path);
        r::RenderScene scene;add_ui(scene,path);
        HostSamplerState host;
        StrictGl33Dispatch strict;
        REQUIRE_NOTHROW(context->renderer().render(scene,extent));
        CHECK(strict.rejected_sampler_queries==0);
        CHECK(strict.scalar_sampler_queries>=2);
        host.check();
        strict.fail_next_draw=true;
        const auto message=injected_draw_failure(*context,scene);
        INFO(message);
        constexpr std::array<const char*,5> expected{"HUD draw","Screen UI rendering",
            "Scene color adjustment pass","Screen UI rendering","Environment backdrop draw"};
        CHECK(message.find(expected[path])!=std::string::npos);
        CHECK_FALSE(strict.fail_next_draw);
        host.check();
        REQUIRE_NOTHROW(context->renderer().render(scene,extent));
        CHECK(strict.rejected_sampler_queries==0);
        host.check();
    }
}

TEST_CASE("Production projective decal guard queries selected sampler and restores bindings on error",
    "[renderer][opengl][gl33-sampler][decal][no-stock]") {
    auto context=glf::try_context();
    REQUIRE(context);REQUIRE(glf::capable_context());context->initialize_renderer();
    auto scene=scene_with_world();scene.world_decals=decal();
    HostSamplerState host;
    StrictGl33Dispatch strict;
    REQUIRE_NOTHROW(context->renderer().render(scene,extent));
    CHECK(strict.draws>0);
    CHECK(context->renderer().decal_statistics().rendered_batch_count>0);
    CHECK(strict.rejected_sampler_queries==0);
    CHECK(strict.scalar_sampler_queries>=2);
    host.check();
    strict.fail_next_draw=true;
    CHECK(injected_draw_failure(*context,scene).find("decal")!=std::string::npos);
    CHECK_FALSE(strict.fail_next_draw);
    host.check();
}
