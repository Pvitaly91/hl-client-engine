// Task-only, path-free synthetic fixture. No gameplay, authentication or installed settings.
// Shared normal-client diagnostic entry against frozen B6 ABI/libraries.
#include "../sky_candidate_api.hpp"
#include "../sky_depth_guard.hpp"
#include "world_render_test_fixture.hpp"
#include "sky_advanced_scenes.hpp"
#include "sky_gl_call_counts.hpp"
#include "sky_gpu_queries.hpp"
#include "resource_list_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/goldsrc/server_info.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>
#include <glad/gl.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace r = hlclient::renderer;
namespace a = hlclient::assets;
namespace er = hlclient::entity_render;
namespace wf = hlclient::tests::world_render_fixture;
namespace wr = hlclient::world_render;
namespace gl = hlclient::renderer::opengl;
using Clock = std::chrono::steady_clock;
using Path = std::filesystem::path;
void need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void gl_ok(const char* message) { need(glGetError() == GL_NO_ERROR, message); }
std::string json(std::string_view s) {
    std::string out{"\""};
    for (const auto c : s) {
        if (c == '\\' || c == '"') out += '\\';
        if (static_cast<unsigned char>(c) < 32) out += '?'; else out += c;
    }
    return out + '"';
}
template<class T> void binary(const Path& path, const std::vector<T>& values) {
    std::ofstream f(path, std::ios::binary); need(bool(f), "Cannot create artifact");
    f.write(reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T)));
    need(bool(f), "Artifact write failed");
}
double us(Clock::duration d) { return std::chrono::duration<double,std::micro>(d).count(); }
void stats_json(std::ostream& o, const SkyCandidateStats& s) {
    o << "{\"frames\":" << s.frames << ",\"late_frames\":" << s.late_frames
      << ",\"early_frames\":" << s.early_frames << ",\"fallback_bits\":" << s.fallback_bits
      << ",\"bounds_tested\":" << s.bounds_tested << ",\"gl_state_queries\":" << s.gl_state_queries
      << ",\"original_frames\":" << s.original_frames
      << ",\"fullscreen_sky_draws\":" << s.fullscreen_sky_draws
      << ",\"fullscreen_sky_skipped_frames\":" << s.fullscreen_sky_skipped_frames
      << ",\"finite_sky_textured_draws\":" << s.finite_sky_textured_draws
      << ",\"finite_sky_constant_draws\":" << s.finite_sky_constant_draws
      << ",\"finite_sky_coverage_alpha_draws\":" << s.finite_sky_coverage_alpha_draws
      << ",\"static_world_sky_skipped_draws\":" << s.static_world_sky_skipped_draws
      << ",\"static_world_sky_skipped_frames\":" << s.static_world_sky_skipped_frames
      << ",\"static_world_sky_unsupported_draws\":" << s.static_world_sky_unsupported_draws
      << ",\"static_world_sky_missing_background_frames\":" << s.static_world_sky_missing_background_frames
      << ",\"unsupported_brush_sky_draws\":" << s.unsupported_brush_sky_draws
      << ",\"non_sky_world_draws\":" << s.non_sky_world_draws
      << ",\"last_frame_world_draws\":" << s.last_frame_world_draws
      << ",\"world_draw_order_hash\":" << json(std::to_string(s.world_draw_order_hash))
      << ",\"last_frame_clear_color\":[";
    for(std::size_t i=0;i<s.last_frame_clear_color.size();++i){if(i)o<<',';o<<s.last_frame_clear_color[i];}
    o << "],\"policy\":" << json(background_sky_policy_name(hlc_background_sky_policy())) << '}';
}

// Every readback changes only state explicitly saved here. The renderer's binding
// cache remains valid when its interrupted glClear call resumes.
struct State {
    GLint read{},draw{},read_buffer{},program{},vao{},active{},renderbuffer{},pack{},unpack{};
    GLint viewport[4]{},polygon[2]{},pack_alignment{},pack_row{},pack_skip_rows{},pack_skip_pixels{},pack_swap{},pack_lsb{};
    GLboolean color[4]{},depth_mask{};
    GLint tex2d[2]{},texms[2]{},sampler[2]{};
    static constexpr std::array<GLenum,12> caps{GL_DEPTH_TEST,GL_STENCIL_TEST,GL_BLEND,
        GL_CULL_FACE,GL_SCISSOR_TEST,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,
        GL_SAMPLE_MASK,GL_RASTERIZER_DISCARD,GL_FRAMEBUFFER_SRGB,GL_DITHER,GL_COLOR_LOGIC_OP};
    std::array<GLboolean,caps.size()> enabled{};
    State() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        glGetIntegerv(GL_READ_BUFFER,&read_buffer); glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer); glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_POLYGON_MODE,polygon); glGetBooleanv(GL_COLOR_WRITEMASK,color);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack); glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
        glGetIntegerv(GL_PACK_ALIGNMENT,&pack_alignment); glGetIntegerv(GL_PACK_ROW_LENGTH,&pack_row);
        glGetIntegerv(GL_PACK_SKIP_ROWS,&pack_skip_rows); glGetIntegerv(GL_PACK_SKIP_PIXELS,&pack_skip_pixels);
        glGetIntegerv(GL_PACK_SWAP_BYTES,&pack_swap); glGetIntegerv(GL_PACK_LSB_FIRST,&pack_lsb);
        for (std::size_t i=0;i<caps.size();++i) enabled[i]=glIsEnabled(caps[i]);
        for (unsigned i=0;i<2;++i) {
            glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex2d[i]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&texms[i]); glGetIntegerv(GL_SAMPLER_BINDING,&sampler[i]);
        }
    }
    ~State() {
        for(unsigned i=0;i<2;++i) {
            glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(tex2d[i]));
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,static_cast<GLuint>(texms[i])); glBindSampler(i,static_cast<GLuint>(sampler[i]));
        }
        glActiveTexture(static_cast<GLenum>(active)); glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao)); glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(renderbuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(read)); glReadBuffer(static_cast<GLenum>(read_buffer));
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]); glPolygonMode(GL_FRONT_AND_BACK,static_cast<GLenum>(polygon[0]));
        glColorMask(color[0],color[1],color[2],color[3]); glDepthMask(depth_mask);
        for(std::size_t i=0;i<caps.size();++i) if(enabled[i]) glEnable(caps[i]); else glDisable(caps[i]);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(pack)); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(unpack));
        glPixelStorei(GL_PACK_ALIGNMENT,pack_alignment); glPixelStorei(GL_PACK_ROW_LENGTH,pack_row);
        glPixelStorei(GL_PACK_SKIP_ROWS,pack_skip_rows); glPixelStorei(GL_PACK_SKIP_PIXELS,pack_skip_pixels);
        glPixelStorei(GL_PACK_SWAP_BYTES,pack_swap); glPixelStorei(GL_PACK_LSB_FIRST,pack_lsb);
    }
    void neutral() {
        for(auto cap:caps) glDisable(cap);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE); glPixelStorei(GL_PACK_LSB_FIRST,GL_FALSE);
        glBindSampler(0,0); glBindSampler(1,0);
    }
};
struct Samples {
    unsigned samples{};
    bool depth_available{true};
    std::vector<std::uint8_t> color;
    std::vector<std::uint32_t> depth_bits;
};
void sample_summary(std::ostream& out,const Samples& s) {
    const auto pixels=s.color.size()/(4*s.samples);
    std::size_t mixed_color{},mixed_depth{},clear_depth{};
    for(const auto d:s.depth_bits)if(d==std::bit_cast<std::uint32_t>(1.0F))++clear_depth;
    for(std::size_t p=0;p<pixels;++p) {
        bool color_diff{},depth_diff{};
        for(unsigned n=1;n<s.samples;++n) {
            if(s.depth_available && s.depth_bits[p]!=s.depth_bits[p+pixels*n])depth_diff=true;
            for(std::size_t c=0;c<4;++c)if(s.color[p*4+c]!=s.color[(p+pixels*n)*4+c])color_diff=true;
        }
        if(color_diff)++mixed_color;if(depth_diff)++mixed_depth;
    }
    out<<"{\"sample_count\":"<<s.samples<<",\"depth_available\":"<<(s.depth_available?"true":"false")<<",\"total_depth_samples\":"<<s.depth_bits.size()
       <<",\"depth_equal_clear_samples\":"<<clear_depth<<",\"mixed_color_pixels\":"<<mixed_color
       <<",\"mixed_depth_pixels\":"<<mixed_depth<<'}';
}
struct Extractor {
    GLuint program{},vao{},copy_fbo{},copy_color{},copy_depth{},read_fbo{},read_color{},read_depth{};
    int width{},height{}; unsigned samples{};
    static GLuint shader(GLenum type, const char* source) {
        const auto id=glCreateShader(type); glShaderSource(id,1,&source,nullptr); glCompileShader(id);
        GLint ok{}; glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
        if(!ok) { char log[2048]{};glGetShaderInfoLog(id,2048,nullptr,log);glDeleteShader(id);throw std::runtime_error(log); }
        return id;
    }
    Extractor() {
        State state; state.neutral();
        const auto vs=shader(GL_VERTEX_SHADER,R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);})");
        const auto fs=shader(GL_FRAGMENT_SHADER,R"(#version 330 core
uniform sampler2DMS colors; uniform sampler2DMS depths; uniform int sample_index;
layout(location=0) out uvec4 rgba; layout(location=1) out uint depth_bits;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);rgba=uvec4(round(clamp(texelFetch(colors,p,sample_index),0.,1.)*255.));
depth_bits=floatBitsToUint(texelFetch(depths,p,sample_index).r);})");
        program=glCreateProgram(); glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        glDeleteShader(vs);glDeleteShader(fs); GLint ok{};glGetProgramiv(program,GL_LINK_STATUS,&ok);need(ok!=0,"Extraction program link failed");
        glGenVertexArrays(1,&vao);glGenFramebuffers(1,&copy_fbo);glGenFramebuffers(1,&read_fbo);
        glGenTextures(1,&copy_color);glGenTextures(1,&copy_depth);glGenTextures(1,&read_color);glGenTextures(1,&read_depth);
        gl_ok("Extraction initialization failed");
    }
    ~Extractor() {
        glDeleteProgram(program);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&copy_fbo);glDeleteFramebuffers(1,&read_fbo);
        glDeleteTextures(1,&copy_color);glDeleteTextures(1,&copy_depth);glDeleteTextures(1,&read_color);glDeleteTextures(1,&read_depth);
    }
    void allocate(int w,int h,unsigned n) {
        if(w==width && h==height && n==samples) return;
        width=w;height=h;samples=n;
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,copy_color);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,static_cast<GLsizei>(n),GL_RGBA8,w,h,GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,copy_depth);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,static_cast<GLsizei>(n),GL_DEPTH24_STENCIL8,w,h,GL_TRUE);
        glBindFramebuffer(GL_FRAMEBUFFER,copy_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D_MULTISAMPLE,copy_color,0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D_MULTISAMPLE,copy_depth,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        need(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Multisample copy framebuffer incomplete");
        glBindTexture(GL_TEXTURE_2D,read_color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8UI,w,h,0,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,nullptr);
        glBindTexture(GL_TEXTURE_2D,read_depth);glTexImage2D(GL_TEXTURE_2D,0,GL_R32UI,w,h,0,GL_RED_INTEGER,GL_UNSIGNED_INT,nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER,read_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,read_color,0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,read_depth,0);
        constexpr GLenum buffers[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,buffers);
        need(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Integer extraction framebuffer incomplete");
        gl_ok("Extraction allocation failed");
    }
    Samples capture(GLuint source,int w,int h,unsigned expected,bool allow_missing_depth=false) {
        State state; state.neutral();
        glBindFramebuffer(GL_FRAMEBUFFER,source);
        GLint n{};glGetIntegerv(GL_SAMPLES,&n);need(n==static_cast<int>(expected),"Actual source samples do not match request");
        GLint depth_size{},depth_type{};glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,source?GL_DEPTH_ATTACHMENT:GL_DEPTH,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&depth_type);
        if(depth_type!=GL_NONE)glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,source?GL_DEPTH_ATTACHMENT:GL_DEPTH,
            GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,&depth_size);
        need(depth_size>0 || (allow_missing_depth && expected==0),"Readback source has no depth attachment");
        Samples result; result.samples=expected?expected:1;result.depth_available=depth_size>0;
        const auto pixels=static_cast<std::size_t>(w)*static_cast<std::size_t>(h);
        result.color.resize(pixels*4*result.samples);if(result.depth_available)result.depth_bits.resize(pixels*result.samples);
        if(!expected) {
            glReadBuffer(source?GL_COLOR_ATTACHMENT0:GL_BACK);
            glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,result.color.data());
            // Stored as IEEE754 float bits, never textual epsilon comparisons.
            if(result.depth_available)glReadPixels(0,0,w,h,GL_DEPTH_COMPONENT,GL_FLOAT,result.depth_bits.data());
        } else {
            need(source!=0,"Expected renderer-owned MSAA framebuffer");
            GLint color_name{},depth_name{},type{};
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
            need(type==GL_RENDERBUFFER,"Unexpected color attachment type");
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&color_name);
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
            need(type==GL_RENDERBUFFER,"Unexpected depth attachment type");
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&depth_name);
            GLint format{};glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(color_name));
            glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_INTERNAL_FORMAT,&format);need(format==GL_RGBA8,"Unsupported color storage");
            glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(depth_name));
            glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_INTERNAL_FORMAT,&format);need(format==GL_DEPTH24_STENCIL8,"Unsupported depth storage");
            allocate(w,h,expected);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,source);glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,copy_fbo);
            // Same dimensions/formats/sample count: preserves samples; no resolve.
            glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
            gl_ok("Multisample per-sample copy failed");
            glBindFramebuffer(GL_FRAMEBUFFER,read_fbo);glViewport(0,0,w,h);glUseProgram(program);glBindVertexArray(vao);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,copy_color);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,copy_depth);
            glUniform1i(glGetUniformLocation(program,"colors"),0);glUniform1i(glGetUniformLocation(program,"depths"),1);
            for(unsigned sample=0;sample<expected;++sample) {
                glUniform1i(glGetUniformLocation(program,"sample_index"),static_cast<GLint>(sample));glDrawArrays(GL_TRIANGLES,0,3);
                glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,w,h,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,result.color.data()+pixels*4*sample);
                glReadBuffer(GL_COLOR_ATTACHMENT1);glReadPixels(0,0,w,h,GL_RED_INTEGER,GL_UNSIGNED_INT,result.depth_bits.data()+pixels*sample);
            }
        }
        gl_ok("Exact sample extraction failed");return result;
    }
};

struct ClearHook {
    inline static ClearHook* active{};
    PFNGLCLEARPROC original{}; Extractor& extractor; int width{},height{}; unsigned samples{};
    bool allow_missing_depth{};
    GLuint source{}; unsigned world_count{}; std::optional<Samples> world; std::exception_ptr error;
    ClearHook(Extractor& e,int w,int h,unsigned n,bool missing=false):original(glad_glClear),extractor(e),width(w),height(h),samples(n),allow_missing_depth(missing) {
        need(!active,"Nested capture hook");active=this;glad_glClear=&invoke;
    }
    ~ClearHook(){glad_glClear=original;active=nullptr;}
    static void GLAD_API_PTR invoke(GLbitfield mask) {
        auto& self=*active;
        sky_fixture::CallCounter::Pause exclude_observer_calls;
        if((mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))==(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)) {
            GLint fbo{};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);self.source=static_cast<GLuint>(fbo);
        } else if(mask==GL_DEPTH_BUFFER_BIT) {
            ++self.world_count;
            try { self.world=self.extractor.capture(self.source,self.width,self.height,self.samples,self.allow_missing_depth); }
            catch(...) { self.error=std::current_exception(); }
        }
        self.original(mask);
    }
};

struct Quad { float x0{-1},x1{1},z0{-1},z1{1},y{}; bool masked{},sky{}; unsigned char red{64}; };
struct ColorOnlyTarget {
    GLuint framebuffer{},color{};
    ColorOnlyTarget(int w,int h) {
        State state;state.neutral();glGenFramebuffers(1,&framebuffer);glGenTextures(1,&color);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        need(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Color-only fixture FBO incomplete");
        gl_ok("Color-only target creation failed");
    }
    ~ColorOnlyTarget(){glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&color);}
};
std::shared_ptr<const wr::WorldRenderPackage> package(const std::vector<Quad>& quads,bool shared_material=false) {
    need(!quads.empty(),"Empty fixture geometry");auto world=wf::make_world();
    world.vertices.clear();world.indices.clear();world.surfaces.clear();world.materials.clear();
    world.bounds={{1e9F,1e9F,1e9F},{-1e9F,-1e9F,-1e9F}};
    std::vector<a::WorldTextureAsset> textures;std::vector<a::WorldMaterialTextureBinding> bindings;
    std::vector<a::WorldSurfaceLightmapBinding> lightmaps;
    for(std::size_t i=0;i<quads.size();++i) {
        const auto& q=quads[i]; const auto base=static_cast<std::uint32_t>(world.vertices.size());
        world.vertices.insert(world.vertices.end(),{
            {{q.x0,q.y,q.z0},{0,-1,0},{0,0}},{{q.x1,q.y,q.z0},{0,-1,0},{16,0}},
            {{q.x1,q.y,q.z1},{0,-1,0},{16,16}},{{q.x0,q.y,q.z1},{0,-1,0},{0,16}}});
        const auto first=static_cast<std::uint32_t>(world.indices.size());
        for(auto k:std::array<std::uint32_t,6>{0,1,2,0,2,3})world.indices.push_back(base+k);
        a::WorldSurface surface;surface.first_index=first;surface.index_count=6;surface.first_vertex=base;surface.vertex_count=4;
        surface.material_index=shared_material?0U:static_cast<std::uint32_t>(i);surface.source_surface_ordinal=static_cast<std::uint32_t>(i);
        surface.bounds={{q.x0,q.y,q.z0},{q.x1,q.y,q.z1}};surface.special_surface=q.sky;
        surface.light_styles={0,255,255,255};world.surfaces.push_back(surface);
        world.bounds.minimum.x=std::min(world.bounds.minimum.x,q.x0);world.bounds.maximum.x=std::max(world.bounds.maximum.x,q.x1);
        world.bounds.minimum.y=std::min(world.bounds.minimum.y,q.y);world.bounds.maximum.y=std::max(world.bounds.maximum.y,q.y);
        world.bounds.minimum.z=std::min(world.bounds.minimum.z,q.z0);world.bounds.maximum.z=std::max(world.bounds.maximum.z,q.z1);
        if(!shared_material || i==0) {
        auto texture=wf::make_texture(q.masked);texture.name=q.sky?"sky":q.masked?"{MASKED":"STONE";
        texture.source_bsp_texture_index=static_cast<std::uint32_t>(i);
        for(auto& mip:texture.mip_levels)for(std::uint32_t y=0;y<mip.height;++y)for(std::uint32_t x=0;x<mip.width;++x) {
            const auto p=(static_cast<std::size_t>(y)*mip.width+x)*4;
            mip.rgba_pixels[p]=std::byte{q.red};mip.rgba_pixels[p+1]=std::byte{96};mip.rgba_pixels[p+2]=std::byte{40};
            mip.rgba_pixels[p+3]=std::byte{static_cast<unsigned char>(q.masked && ((x+y)%4<2)?0:255)};
        }
        a::WorldMaterialReference material;material.texture_name=texture.name;material.width=material.height=16;
        material.texture_storage=a::WorldTextureStorage::embedded;material.source_texture_index=static_cast<std::uint32_t>(i);
        world.materials.push_back(material);textures.push_back(std::move(texture));
        a::WorldMaterialTextureBinding binding;binding.material_index=i;binding.status=a::WorldMaterialTextureBindingStatus::resolved_embedded;
        binding.texture_asset_index=i;binding.source_bsp_texture_index=static_cast<std::uint32_t>(i);bindings.push_back(binding);
        }
        a::WorldSurfaceLightmapBinding lm;lm.surface_index=i;lm.status=a::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
        lm.sample_width=lm.sample_height=2;lightmaps.push_back(lm);
    }
    world.statistics.emitted_vertex_count=world.vertices.size();world.statistics.emitted_triangle_count=world.indices.size()/3;
    world.statistics.emitted_surface_count=world.surfaces.size();world.statistics.material_count=world.materials.size();
    auto ts=a::WorldTextureSet::create(std::move(textures),std::move(bindings),{},world.materials.size());need(bool(ts),"Texture fixture rejected");
    auto ls=a::WorldLightmapSet::create({},std::move(lightmaps),quads.size());need(bool(ls),"Lightmap fixture rejected");
    auto built=wr::WorldRenderPackageBuilder{}.build({std::move(world),std::move(*ts.texture_set)},std::move(*ls.lightmap_set));
    need(bool(built),"World fixture rejected");return std::make_shared<const wr::WorldRenderPackage>(std::move(*built.package));
}
std::shared_ptr<const a::SkyboxAsset> sky(unsigned variant=0) {
    auto result=std::make_shared<a::SkyboxAsset>();
    for(unsigned f=0;f<6;++f) {
        auto& face=result->faces[f];face.width=face.height=8;face.pixels.resize(8*8*4);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            const auto p=(y*8+x)*4;face.pixels[p]=std::byte{static_cast<unsigned char>(30+f*25+variant*7)};
            face.pixels[p+1]=std::byte{static_cast<unsigned char>(50+x*15)};
            face.pixels[p+2]=std::byte{static_cast<unsigned char>(70+y*13)};face.pixels[p+3]=std::byte{255};
        }
    }return result;
}
r::RenderScene scene_for(std::string_view name,unsigned samples,bool first_person) {
    r::RenderScene s;s.camera.position={0,-2,0};s.camera.target={0,0,0};s.camera.up={0,0,1};
    s.camera.vertical_field_of_view_radians=1.0471975512F;s.camera.near_plane=.1F;s.camera.far_plane=20;
    s.clear_color={.035F,.055F,.14F,1};s.skybox=sky();s.requested_msaa_samples=samples;
    std::vector<Quad> geometry;
    if(name=="room")geometry={{-8,8,-8,8,0,false,false,85}};
    else if(name=="open" || name=="no_sky" || name=="replacement" || name=="invalid_sky" || name=="scissor_state" || name=="depthmask_state" || name=="depthrange_state" || name=="missing_depth" || name=="runtime_brushes" || name=="translucent_studio" || name=="decals" || name=="water")geometry={{-.5F,.5F,-.5F,.5F,0,false,false,90}};
    else if(name=="masked_edges")geometry={{-1.127F,.783F,-.793F,.917F,0,true,false,180}};
    else if(name=="portal_front")geometry={{-1,1,-1,1,0,false,true,224},{-.42F,.42F,-.42F,.42F,-.25F,false,false,48}};
    else if(name=="portal_behind")geometry={{-1,1,-1,1,0,false,true,224},{-.42F,.42F,-.42F,.42F,.25F,false,false,48}};
    else if(name=="far_plane" || name=="far_rounding") {
        s.camera.far_plane=10;const float y=name=="far_plane"?8.0F:std::nextafter(8.0F,0.0F);
        geometry={{-8,8,-8,8,y,false,false,200}};
    } else if(name=="roll_aspect") {
        geometry={{-1.127F,.783F,-.793F,.917F,0,true,false,180}};
        s.camera.vertical_field_of_view_radians=1.4F;s.screen_effects.translation={.07F,-.04F,.05F};s.screen_effects.roll_degrees=13;
    } else if(name=="effects") {
        geometry={{-.7F,.7F,-.7F,.7F,0,false,false,90}};
        s.world_flashes[0]={{.7F,-.5F,.2F},.6F,{.6F,.2F,.1F,.65F}};s.world_flash_count=1;
        s.world_particles[0]={{-.7F,-.5F,0},.3F,{.1F,.6F,.9F,.5F},false};s.world_particle_count=1;
    } else throw std::runtime_error("Unknown scene");
    s.static_world=r::RenderStaticWorld{package(geometry),r::RenderCullMode::none,r::RenderBaselineLightStylePolicy::source_slot_zero};
    if(name=="no_sky")s.skybox.reset();if(name=="replacement")s.skybox=sky(1);
    if(name=="runtime_brushes")sky_advanced::add_runtime_brush(s);
    if(name=="translucent_studio")sky_advanced::add_translucent_entities(s);
    if(name=="decals")sky_advanced::add_decals(s);
    if(name=="water")s.static_world->package=sky_advanced::water_package();
    if(first_person) {s.first_person_entities=sky_advanced::first_person();s.first_person_flash=r::RenderMuzzleFlash{{2,0,0},.12F,{.9F,.5F,.2F,.8F}};}
    return s;
}

struct Options {bool correctness{},regressions{},diagnostic_correctness{},diagnostic_benchmark{},stock_sky_correctness{},crossfire_correctness{},crossfire_benchmark{};std::string scene{"room"};unsigned frames{600},warm{120},msaa{4};Path output{L"sky-fixture-output"};};
Options options(int argc,wchar_t** argv) {
    Options o;bool mode{};
    for(int i=1;i<argc;++i) {
        const std::wstring k=argv[i];
        if(k==L"--correctness" || k==L"--regressions" || k==L"--diagnostic-correctness" || k==L"--diagnostic-benchmark" || k==L"--benchmark" || k==L"--stock-sky-correctness" || k==L"--crossfire-preflight" || k==L"--crossfire-benchmark") {
            need(!mode,"Choose exactly one mode");mode=true;o.regressions=k==L"--regressions";o.diagnostic_correctness=k==L"--diagnostic-correctness";
            o.diagnostic_benchmark=k==L"--diagnostic-benchmark";o.stock_sky_correctness=k==L"--stock-sky-correctness";
            o.crossfire_correctness=k==L"--crossfire-preflight";o.crossfire_benchmark=k==L"--crossfire-benchmark";
            o.correctness=k!=L"--benchmark" && !o.diagnostic_benchmark && !o.crossfire_benchmark;continue;}
        need(i+1<argc,"Missing argument");const std::wstring v=argv[++i];
        if(k==L"--out")o.output=v;
        else if(k==L"--scene") {need(v==L"room"||v==L"open","Benchmark scene must be room or open");o.scene=v==L"room"?"room":"open";}
        else if(k==L"--frames" || k==L"--warm" || k==L"--msaa") {
            need(!v.empty() && v.find_first_not_of(L"0123456789")==std::wstring::npos,"Invalid numeric argument");
            const auto n=std::stoul(v);need(n<=100000,"Argument exceeds bound");
            if(k==L"--frames")o.frames=static_cast<unsigned>(n);else if(k==L"--warm")o.warm=static_cast<unsigned>(n);else o.msaa=static_cast<unsigned>(n);
        } else throw std::runtime_error("Unknown argument");
    }
    need(mode,"Use --correctness, --regressions, --diagnostic-correctness, --stock-sky-correctness, --diagnostic-benchmark or --benchmark");need(o.frames>0 && o.warm>0,"Frames and warmup must be positive");
    need(o.msaa==0 || o.msaa==4,"Only AAOff/MSAA4 supported");
    if(o.diagnostic_benchmark)need(o.msaa==4 && o.warm==120 && o.frames==600,"Diagnostic benchmark uses the fixed MSAA4/120-warm/600-measured protocol");
    need(!std::filesystem::exists(o.output),"Output directory already exists; never overwrite evidence");
    return o;
}
void hardware(std::ostream& o,const gl::OpenGlRenderer& gpu) {
    const auto& h=gpu.information();const bool target=h.vendor.find("Intel")!=std::string::npos && h.device.find("HD Graphics 4600")!=std::string::npos;
    o<<"\"gl_vendor\":"<<json(h.vendor)<<",\"gl_renderer\":"<<json(h.device)<<",\"gl_version\":"<<json(h.version)
     <<",\"actual_hd4600_string_match\":"<<(target?"true":"false");
}
void save_samples(const Path& root,std::string_view boundary,const Samples& s) {
    binary(root/(std::string(boundary)+".rgba8.samples.bin"),s.color);
    binary(root/(std::string(boundary)+".depth-f32bits.samples.bin"),s.depth_bits);
}
void guard_assertions() {
    const auto near=scene_for("room",0,false),far=scene_for("far_plane",0,false),rounding=scene_for("far_rounding",0,false);
    const auto near_vp=r::camera_view_projection(near.camera,{96,96});
    const auto far_vp=r::camera_view_projection(far.camera,{96,96});
    const auto rounding_vp=r::camera_view_projection(rounding.camera,{96,96});
    need(near_vp && far_vp && rounding_vp,"Fixture camera MVP invalid");
    need(gl::sky_depth_guard(near.static_world->package->bounds(),*near_vp.matrix,24),"Guard must accept near room bounds");
    need(!gl::sky_depth_guard(far.static_world->package->bounds(),*far_vp.matrix,24),"Guard must reject far-plane contact");
    need(!gl::sky_depth_guard(rounding.static_world->package->bounds(),*rounding_vp.matrix,24),"Guard must reject near-far rounding risk");
    need(!gl::sky_depth_guard(near.static_world->package->bounds(),*near_vp.matrix,0),"Guard must reject missing depth precision");
    auto invalid=*near_vp.matrix;invalid.values[2]=std::numeric_limits<float>::infinity();
    need(!gl::sky_depth_guard(near.static_world->package->bounds(),invalid,24),"Guard must reject nonfinite matrix");
}
void correctness(const Options& o,gl::OpenGlRenderer& gpu) {
    guard_assertions();Extractor extractor;
    std::ofstream manifest(o.output/L"correctness.json");manifest<<std::setprecision(17);
    manifest<<"{\"schema\":\"hlc.sky.correctness.v1\",\"status\":\"ARTIFACTS_ONLY_REQUIRE_EXACT_AB_COMPARISON\",";hardware(manifest,gpu);
    manifest<<",\"layout\":\"sample-major; each sample bottom-row-first; RGBA8 and IEEE754 depth float bits little-endian\","
        "\"depth_storage\":\"MSAA D24S8 copied without resolve and fetched by sampler2DMS; color-only fallback has explicitly absent depth\","
        "\"pure_guard_assertions\":[\"actual_room_MVP_accept\",\"actual_far_MVP_reject\",\"actual_roundnear_MVP_reject\",\"zero_depth_bits_reject\",\"nonfinite_MVP_reject\"],\"cases\":[";
    constexpr std::array names{"room","open","masked_edges","portal_front","portal_behind","far_plane","far_rounding","roll_aspect","effects","no_sky","replacement","invalid_sky","scissor_state","depthmask_state","depthrange_state","runtime_brushes","translucent_studio","decals","water","missing_depth"};
    bool comma{};
    for(const unsigned samples:std::array<unsigned,2>{0,4})for(const auto* name:names) {
        const bool missing_depth=std::string_view(name)=="missing_depth";
        if(missing_depth && samples!=0)continue; // MSAA4 creates real scene depth, so it cannot exercise missing-scene-depth.
        const int w=std::string_view(name)=="roll_aspect"?127:96,h=96;
        auto s=scene_for(name,samples,true);const auto dir=o.output/(std::string(name)+"-msaa"+std::to_string(samples));
        std::filesystem::create_directory(dir);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glDisable(GL_SCISSOR_TEST);
        glDepthRange(0,1);glBindFramebuffer(GL_FRAMEBUFFER,0);
        std::unique_ptr<ColorOnlyTarget> depthless;
        if(missing_depth){depthless=std::make_unique<ColorOnlyTarget>(w,h);glBindFramebuffer(GL_FRAMEBUFFER,depthless->framebuffer);}
        // Defined prior target contents for deliberately partial/disabled clears.
        // Priming is untimed and excluded from candidate counters.
        gpu.render(s,{w,h});hlc_background_sky_reset_stats();
        if(std::string_view(name)=="replacement") {
            // First frame after BOTH immutable owners change; do not warm the replacements.
            s.skybox=sky(2);s.static_world->package=package({Quad{-.5F,.5F,-.5F,.5F,0,false,false,135}});
        }
        if(std::string_view(name)=="invalid_sky") {auto invalid=std::make_shared<a::SkyboxAsset>(*sky());invalid->faces[0].width=0;s.skybox=std::move(invalid);}
        if(std::string_view(name)=="scissor_state") {glEnable(GL_SCISSOR_TEST);glScissor(11,7,w-25,h-19);}
        if(std::string_view(name)=="depthmask_state")glDepthMask(GL_FALSE);
        if(std::string_view(name)=="depthrange_state")glDepthRange(.2,.8);
        GLuint source{};
        Samples world;
        const auto before=gpu.statistics();const auto entities_before=gpu.entity_statistics();const auto decals_before=gpu.decal_statistics();
        sky_fixture::Calls calls;
        {sky_fixture::CallCounter counter;counter.tracking=true;
            ClearHook hook(extractor,w,h,samples,missing_depth);gpu.render(s,{w,h});counter.tracking=false;calls=counter.count;
            if(hook.error)std::rethrow_exception(hook.error);
            need(hook.world_count==1 && hook.world.has_value(),"Expected exactly one real pre-viewmodel depth clear");
            source=hook.source;world=std::move(*hook.world);save_samples(dir,"world-before-viewmodel",world);}
        need(gpu.scene_target_statistics().actual_samples==samples,"Renderer MSAA fallback is not correctness pass");
        if(std::string_view(name)=="room")need(std::find(world.depth_bits.begin(),world.depth_bits.end(),std::bit_cast<std::uint32_t>(1.0F))==world.depth_bits.end(),"Room did not cover every depth sample");
        if(std::string_view(name)=="open") {
            const auto clear=static_cast<std::size_t>(std::count(world.depth_bits.begin(),world.depth_bits.end(),std::bit_cast<std::uint32_t>(1.0F)));
            need(clear>0 && clear<world.depth_bits.size(),"Open scene lacks both background and opaque samples");
        }
        if(samples==4 && std::string_view(name)=="masked_edges") {
            const auto pixels=static_cast<std::size_t>(w)*static_cast<std::size_t>(h);bool mixed{};
            for(unsigned n=1;n<4;++n)if(!std::equal(world.depth_bits.begin(),world.depth_bits.begin()+static_cast<std::ptrdiff_t>(pixels),world.depth_bits.begin()+static_cast<std::ptrdiff_t>(pixels*n)))mixed=true;
            need(mixed,"MSAA masked-edge case did not exercise partial sample coverage");
        }
        const auto final_scene=extractor.capture(source,w,h,samples,missing_depth);save_samples(dir,"final-scene",final_scene);
        if(missing_depth){need(!world.depth_available && !final_scene.depth_available,"Depthless target acquired unexpected depth");binary(dir/L"final-color-only.rgba8.bin",final_scene.color);}
        else {const auto final=gpu.observe_framebuffer({w,h},s.clear_color,true);need(bool(final),"Final default framebuffer unavailable");binary(dir/L"final-default.rgba8.bin",final.rgba8);}
        const auto candidate=hlc_background_sky_stats();
        need(candidate.frames==1 && candidate.late_frames+candidate.early_frames==1,"Candidate counters do not identify one frame");
        if(hlc_background_sky_policy()==BackgroundSkyPolicy::depth) {
            need(candidate.frames==1 && candidate.late_frames+candidate.early_frames==1,"Candidate counters do not identify one frame");
            if(std::string_view(name)=="room" || std::string_view(name)=="open")need(candidate.late_frames==1,"Safe warmed room/open did not use candidate path");
            if(std::string_view(name)=="far_plane" || std::string_view(name)=="far_rounding")need(candidate.early_frames==1 && (candidate.fallback_bits&16)!=0,"Far-depth case did not take real bounds fallback");
            if(missing_depth)need(candidate.early_frames==1 && (candidate.fallback_bits&4)!=0,"Missing depth did not take real target fallback");
            if(std::string_view(name)=="depthrange_state")need(candidate.early_frames==1 && (candidate.fallback_bits&8)!=0,"Nonstandard depth range did not fallback");
            if(std::string_view(name)=="replacement" || std::string_view(name)=="invalid_sky")need(candidate.early_frames==1 && (candidate.fallback_bits&1)!=0,"Sky replacement/invalid resource did not fallback");
        } else {
            need(candidate.original_frames==1 && candidate.early_frames==1 && candidate.late_frames==0,
                "Original policy did not use the accepted early order");
            need(candidate.fallback_bits==0 && candidate.bounds_tested==0 && candidate.gl_state_queries==0,
                "Original policy executed candidate guards");
            for(const auto n:candidate.fallback_counts)need(n==0,"Original policy counted a depth fallback");
        }
        const auto studio_draws=gpu.entity_statistics().studio_draw_count-entities_before.studio_draw_count;
        need(studio_draws==(std::string_view(name)=="translucent_studio"?3U:1U),"Real Studio expected draw count not executed");
        const auto brush_draws=gpu.statistics().brush_draw_call_count-before.brush_draw_call_count;
        const auto brush_instances=gpu.statistics().runtime_brush_submitted_count-before.runtime_brush_submitted_count;
        const auto decal_draws=gpu.decal_statistics().rendered_batch_count-decals_before.rendered_batch_count;
        if(std::string_view(name)=="runtime_brushes")need(brush_draws==2 && brush_instances==2,"Runtime brush geometry not actually submitted");
        if(std::string_view(name)=="decals")need(decal_draws==1,"Decal control not actually rendered");
        gl_ok("Correctness capture left GL error");
        if(comma)manifest<<',';comma=true;
        manifest<<"{\"name\":"<<json(name)<<",\"width\":"<<w<<",\"height\":"<<h<<",\"requested_samples\":"<<samples
         <<",\"actual_samples\":"<<gpu.scene_target_statistics().actual_samples
         <<",\"depth_attachment_expected\":"<<(missing_depth?"false":"true")
         <<",\"output_target\":"<<json(missing_depth?"fixture_owned_RGBA8_color_only_FBO":"native_default_back_after_scene_resolve")
         <<",\"target_storage_allocations_cumulative\":"<<gpu.scene_target_statistics().storage_allocations<<",\"candidate\":";stats_json(manifest,candidate);
        manifest<<",\"world_samples\":";sample_summary(manifest,world);manifest<<",\"final_samples\":";sample_summary(manifest,final_scene);
        manifest<<",\"source_geometry\":{\"world_vertices\":"<<s.static_world->package->vertices().size()<<",\"world_indices\":"<<s.static_world->package->indices().size()
         <<",\"world_batches\":"<<s.static_world->package->draw_batches().size()<<",\"world_surfaces\":"<<s.static_world->package->surface_ranges().size()<<'}'
         <<",\"actual_draw_deltas\":{\"world_draws\":"<<gpu.statistics().draw_call_count-before.draw_call_count<<",\"world_triangles\":"<<gpu.statistics().triangle_count-before.triangle_count
         <<",\"brush_draws\":"<<brush_draws<<",\"brush_instances\":"<<brush_instances<<",\"studio_draws\":"<<studio_draws<<",\"decal_batches\":"<<decal_draws<<'}'
         <<",\"render_gl_calls_excluding_readback\":";sky_fixture::json(manifest,calls);manifest<<'}';
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    manifest<<"],\"explicit_coverage_gaps\":[],\"water_scope\":\"accepted B6 supported non-sky special !water material; not a claim of animated/refraction semantics absent in B6\","
        "\"state_scope\":\"scissor/depth mask inputs are normalized by accepted SceneTargetFrame; nonstandard depth range is the actual injected state fallback\","
        "\"target_performance\":\"NOT_MEASURED\"}";need(bool(manifest),"Manifest write failed");
}
#include "sky_eligibility_regressions.hpp"
#include "sky_disable_diagnostic_correctness.hpp"
#include "sky_stock_correction_correctness.hpp"
#include "sky_crossfire_control.hpp"
struct Row {std::uint64_t frame{};double render_us{},interval_us{},swap_us{};SkyCandidateStats candidate{};gl::OpenGlWorldRendererStatistics work{};sky_fixture::Calls calls{};std::uint64_t guard_ns{},prepare_ns{};};
void benchmark(const Options& o,hlclient::platform::SdlWindow& window,gl::OpenGlRenderer& gpu) {
    // Resolve startup show/resize events before recording the same-context SDL flags.
    hlclient::platform::WindowEvent startup_event;
    while(window.poll_event(startup_event)) {
        need(startup_event.type!=hlclient::platform::WindowEventType::quit_requested,"Benchmark cancelled during startup");
        need(startup_event.type!=hlclient::platform::WindowEventType::native_event_limit_exceeded,"Startup native event overflow");
    }
    auto* native_window=SDL_GL_GetCurrentWindow();need(native_window!=nullptr,"Current GL context has no SDL window");
    const auto native_flags=SDL_GetWindowFlags(native_window);
    need((native_flags&SDL_WINDOW_HIDDEN)==0,"Native SDL benchmark window is hidden");
    need((native_flags&SDL_WINDOW_MINIMIZED)==0,"Native SDL benchmark window is minimized");
    constexpr r::RenderExtent extent{1920,1080};const auto pixels=window.pixel_extent();
    const auto video=window.video_state();
    const auto video_description=std::string{"actual_mode="}+std::string{hlclient::core::to_string(video.actual_mode)}+
        " desktop="+std::to_string(video.desktop_extent.width)+"x"+std::to_string(video.desktop_extent.height)+
        " window="+std::to_string(video.window_extent.width)+"x"+std::to_string(video.window_extent.height)+
        " framebuffer="+std::to_string(pixels.width)+"x"+std::to_string(pixels.height)+
        " swap_interval="+(video.actual_swap_interval?std::to_string(*video.actual_swap_interval):"unavailable");
    if(pixels.width!=extent.width || pixels.height!=extent.height)
        throw std::runtime_error("Actual framebuffer is not 1920x1080; "+video_description);
    if(video.actual_mode!=hlclient::core::VideoWindowMode::borderless)
        throw std::runtime_error("Actual benchmark mode is not borderless; "+video_description);
    if(!video.actual_swap_interval || *video.actual_swap_interval!=0)
        throw std::runtime_error("Swap interval is not confirmed zero; "+video_description);
    auto s=o.diagnostic_benchmark?sky_disable_diagnostic::benchmark_scene(o.scene,o.msaa):scene_for(o.scene,o.msaa,false);std::vector<Row> rows;rows.reserve(o.frames);
    const bool expect_fullscreen=!sky_disable_diagnostic::disabled();
    const unsigned expected_world_draws=o.diagnostic_benchmark?2:1,required_world_subpass_mask=expect_fullscreen?6:4;
    std::optional<Clock::time_point> previous;
    sky_fixture::CallCounter counter;
    need(static_cast<std::uint64_t>(o.warm)+o.frames<=720,"Per-frame GPU query ownership is bounded to 720 total frames");
    // Hook ordering is intentional: timer forwards to counter, then real GL;
    // destruction restores timer hooks before restoring the count-only hooks.
    sky_fixture::GpuQueries queries(o.warm+o.frames,o.msaa,expect_fullscreen,expected_world_draws);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glDisable(GL_SCISSOR_TEST);
    for(std::uint64_t frame=1;frame<=static_cast<std::uint64_t>(o.warm)+o.frames;++frame) {
        hlclient::platform::WindowEvent event;while(window.poll_event(event)) {
            need(event.type!=hlclient::platform::WindowEventType::quit_requested,"Benchmark cancelled by user");
            need(event.type!=hlclient::platform::WindowEventType::native_event_limit_exceeded,"Native event overflow");
            if(event.type==hlclient::platform::WindowEventType::resized)
                need(event.extent.width==extent.width && event.extent.height==extent.height,"Benchmark framebuffer resized");
        }
        if(frame==static_cast<std::uint64_t>(o.warm)+1){hlc_background_sky_reset_stats();counter.count={};}
        counter.tracking=true;
        const auto begin=Clock::now();queries.begin(frame);gpu.render(s,extent);queries.end();const auto rendered=Clock::now();
        counter.tracking=false;
        window.swap_buffers();const auto completed=Clock::now();
        if(frame>o.warm) {
            const auto& cost=hlc_background_sky_diagnostics();
            rows.push_back({frame,us(rendered-begin),previous?us(completed-*previous):0.0,us(completed-rendered),hlc_background_sky_stats(),gpu.statistics(),counter.count,cost.guard_ns,cost.prepare_ns});
        }
        previous=completed;
        need(gpu.scene_target_statistics().actual_samples==o.msaa,"MSAA fallback invalidates benchmark");
    }
    // No GPU result/availability read, fence or artificial queue wait occurred
    // in the loop. Each frame retains its own query objects until this drain.
    std::map<std::uint64_t,gl::OpenGlGpuFrameTiming> timings;
    for(const auto& timing:queries.drain(std::chrono::seconds(30)))timings.emplace(timing.frame_number,timing);
    gl_ok("Benchmark GL error");
    const auto timing_fields=std::string{"\"timing_source\":\"fixture_owned_per_frame_queries_v1\",\"gpu_scope_definitions\":{"}
        +"\"total\":\"GL_TIME_ELAPSED around complete gpu.render call, excluding SDL swap\","
        +"\"world\":"+json(o.msaa?"timestamp immediately before gpu.render to immediately before first MSAA resolve blit":"timestamp immediately before gpu.render to end of render; AAOff synthetic scene has no additional populated passes")+','
        +"\"sky\":"+json(expect_fullscreen?"timestamps around the actual fullscreen draw":"not drawn by requested diagnostic policy; mask absent and duration null")+','
        +"\"static_world\":"+json(o.diagnostic_benchmark?"timestamps from immediately before first opaque draw through immediately after second finite-sky draw; includes between-draw setup":"timestamps around the one actual world draw")+'}';
    std::ofstream raw(o.output/L"samples.jsonl");raw<<std::setprecision(17);
    std::size_t valid{},missing{};
    for(const auto& row:rows) {
        raw<<'{'<<timing_fields<<",\"frame\":"<<row.frame<<",\"cpu_render_us\":"<<row.render_us<<",\"swap_completion_interval_us\":"<<row.interval_us
           <<",\"swap_call_us\":"<<row.swap_us<<",\"guard_ns_inclusive_cumulative\":"<<row.guard_ns
           <<",\"prepare_ns_nested_cumulative\":"<<row.prepare_ns<<",\"candidate_cumulative\":";stats_json(raw,row.candidate);
        raw<<",\"draws_cumulative\":"<<row.work.draw_call_count<<",\"triangles_cumulative\":"<<row.work.triangle_count
           <<",\"program_binds_cumulative\":"<<row.work.program_bind_count<<",\"vao_binds_cumulative\":"<<row.work.vertex_array_bind_count
           <<",\"sky_binds_cumulative\":"<<row.work.sky_texture_bind_count<<",\"render_gl_calls_cumulative_measured\":";sky_fixture::json(raw,row.calls);
        const auto& observed=queries.frames().at(static_cast<std::size_t>(row.frame-1));
        raw<<",\"query_observation\":{\"sky_draws\":"<<observed.sky_draws<<",\"static_draws\":"<<observed.static_draws
           <<",\"other_draws\":"<<observed.other_draws<<",\"blits\":"<<observed.blits
           <<",\"world_end_at_first_blit\":"<<(observed.world_end_at_blit?"true":"false")
           <<",\"world_end_at_render\":"<<(observed.world_end_at_render?"true":"false")<<'}';
        const auto it=timings.find(row.frame);
        if(it==timings.end()) {++missing;raw<<",\"gpu\":null";}
        else {const auto& t=it->second;if(t.status==gl::OpenGlGpuTimingStatus::ready && t.valid_pass_mask==1 && t.valid_world_subpass_mask==required_world_subpass_mask)++valid;
            raw<<",\"gpu\":{\"status\":"<<json(gl::to_string(t.status))<<",\"total_ms\":"<<t.elapsed_ms<<",\"pass_mask\":"<<unsigned(t.valid_pass_mask)
               <<",\"world_subpass_mask\":"<<unsigned(t.valid_world_subpass_mask)<<",\"passes_ms\":[";
            for(std::size_t i=0;i<t.pass_elapsed_ms.size();++i){if(i)raw<<',';raw<<t.pass_elapsed_ms[i];}raw<<"],\"world_subpasses_ms\":[";
            for(std::size_t i=0;i<t.world_subpass_elapsed_ms.size();++i){if(i)raw<<',';if(o.diagnostic_benchmark && (t.valid_world_subpass_mask&(1U<<i))==0)raw<<"null";else raw<<t.world_subpass_elapsed_ms[i];}raw<<"]}";
        }raw<<"}\n";
    }
    constexpr std::array<double,5> long_threshold_us{10000.0,1000000.0/60.0,1000000.0/30.0,50000.0,100000.0};
    std::array<std::uint64_t,5> long_cpu{},long_interval{},long_gpu{};
    for(const auto& row:rows)for(std::size_t i=0;i<long_threshold_us.size();++i) {
        if(row.render_us>long_threshold_us[i])++long_cpu[i];if(row.interval_us>long_threshold_us[i])++long_interval[i];
        const auto timing=timings.find(row.frame);if(timing!=timings.end() && timing->second.status==gl::OpenGlGpuTimingStatus::ready && timing->second.elapsed_ms*1000.0>long_threshold_us[i])++long_gpu[i];
    }
    const auto& ts=queries.statistics();std::ofstream report(o.output/L"benchmark.json");report<<std::setprecision(17);
    report<<"{\"schema\":\"hlc.sky.benchmark.v1\",";hardware(report,gpu);
    report<<",\"requested_window_mode\":\"borderless\",\"actual_window_mode\":"<<json(hlclient::core::to_string(video.actual_mode))
        <<",\"native_window_flags_start\":"<<static_cast<std::uint64_t>(native_flags)
        <<",\"native_window_hidden\":false,\"native_window_minimized\":false,\"visibility_scope\":\"same-context SDL flags after startup event pump; external occlusion not measured\""
        <<",\"desktop_width\":"<<video.desktop_extent.width<<",\"desktop_height\":"<<video.desktop_extent.height
        <<",\"actual_window_width\":"<<video.window_extent.width<<",\"actual_window_height\":"<<video.window_extent.height
        <<",\"actual_framebuffer_width\":"<<pixels.width<<",\"actual_framebuffer_height\":"<<pixels.height;
    report<<','<<timing_fields<<",\"scene\":"<<json(o.scene)<<",\"diagnostic_benchmark\":"<<(o.diagnostic_benchmark?"true":"false")
        <<",\"expected_world_draws\":"<<expected_world_draws<<",\"expected_fullscreen_sky_draws\":"<<(expect_fullscreen?1:0)<<",\"gpu_required_world_subpass_mask\":"<<required_world_subpass_mask
        <<",\"scene_provenance\":"<<json(o.diagnostic_benchmark?"synthetic_fixed_opaque_plus_finite_sky_geometry_camera_time_no_network_NOT_Crossfire_FPS":"synthetic_fixed_geometry_camera_time_no_network")<<",\"visible_window\":true,"
        "\"frame_interval_semantics\":\"consecutive SDL swap completion; includes event pump/CPU/render/swap; not gameplay FPS or scanout\","
        "\"cpu_scope\":\"entire gpu.render call plus test-owned timer begin/end and draw timestamp hooks including candidate guards; excludes event pump and swap\","
        "\"gpu_masks_required\":true,\"gpu_pass_order\":[\"world\",\"studio_viewmodel\",\"effects_decals\",\"msaa_resolve\",\"image_adjustments\",\"hud_console\"],"
        "\"world_subpass_order\":[\"target_setup_clear\",\"sky\",\"static_world\",\"runtime_brushes\"],"
        "\"width\":1920,\"height\":1080,\"requested_samples\":"<<o.msaa<<",\"actual_samples\":"<<gpu.scene_target_statistics().actual_samples
        <<",\"warm_frames\":"<<o.warm<<",\"measured_frames\":"<<rows.size()<<",\"actual_swap_interval\":0,\"gpu_ready_measured\":"<<valid<<",\"gpu_missing_measured\":"<<missing
        <<",\"gpu_capacity_drops_all_frames\":"<<ts.capacity_drops<<",\"gpu_unavailable_all_frames\":"<<ts.unavailable<<",\"gpu_pending_final\":"<<ts.pending
        <<",\"gpu_invalid_all_frames\":"<<ts.invalid_results<<",\"gpu_timeout_all_frames\":"<<ts.timeout_results
        <<",\"gpu_query_result_gl_error\":"<<static_cast<unsigned>(ts.query_result_gl_error)
        <<",\"gpu_disjoint_detection_supported\":false,\"gpu_disjoint_all_frames\":null"
        <<",\"gpu_owned_query_objects\":"<<ts.allocated_queries<<",\"gpu_submitted_all_frames\":"<<ts.submitted<<",\"gpu_completed_all_frames\":"<<ts.completed
        <<",\"elapsed_query_counter_bits\":"<<ts.elapsed_counter_bits<<",\"timestamp_query_counter_bits\":"<<ts.timestamp_counter_bits
        <<",\"sky_draws_all_frames\":"<<ts.sky_draws<<",\"static_draws_all_frames\":"<<ts.static_draws<<",\"unexpected_draws_all_frames\":"<<ts.other_draws
        <<",\"world_end_at_first_blit_all_frames\":"<<ts.world_end_at_blit<<",\"world_end_at_render_all_frames\":"<<ts.world_end_at_render
        <<",\"candidate\":";stats_json(report,hlc_background_sky_stats());
    report<<",\"render_gl_calls_measured\":";sky_fixture::json(report,counter.count);
    report<<",\"long_frame_counts\":{\"comparison\":\"strictly_greater_than; every measured row retained\",\"threshold_ms\":[";
    for(std::size_t i=0;i<long_threshold_us.size();++i){if(i)report<<',';report<<long_threshold_us[i]/1000.0;}report<<"],\"cpu_render\":[";
    for(std::size_t i=0;i<long_cpu.size();++i){if(i)report<<',';report<<long_cpu[i];}report<<"],\"swap_completion_interval\":[";
    for(std::size_t i=0;i<long_interval.size();++i){if(i)report<<',';report<<long_interval[i];}report<<"],\"gpu_total\":[";
    for(std::size_t i=0;i<long_gpu.size();++i){if(i)report<<',';report<<long_gpu[i];}report<<"]}";
    report<<",\"gl_counter_scope\":\"count-only wrappers inside render CPU timing, identical A/B; enable/disable and setters are calls not necessarily effective transitions; excludes observer/poll/swap\","
        "\"readback_or_fences_in_timed_loop\":false,\"query_results_or_availability_reads_in_timed_loop\":false,\"acceptance\":\"NOT_DECIDED_SINGLE_LEG\"}";
    need(bool(raw)&&bool(report),"Benchmark output failed");
    need(valid==rows.size() && missing==0,"All measured GPU rows with exact scope masks are mandatory");
    need(ts.pending==0 && ts.unavailable==0 && ts.invalid_results==0 && ts.timeout_results==0 && ts.capacity_drops==0,
        "GPU query ownership and validity must hold for warmup and measured frames");
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    try {
        need(parse_background_sky_policy("original")==BackgroundSkyPolicy::original,"Original policy parse failed");
        need(parse_background_sky_policy("depth")==BackgroundSkyPolicy::depth,"Depth policy parse failed");
        need(parse_background_sky_policy("no_background")==BackgroundSkyPolicy::no_background,"No-background policy parse failed");
        need(parse_background_sky_policy("no_sky_color")==BackgroundSkyPolicy::no_sky_color,"No-sky-color policy parse failed");
        for(const std::string_view bad:{"","DEPTH"," original","original ","auto","depth\n"})
            need(!parse_background_sky_policy(bad),"Invalid policy was accepted");
        const auto policy=read_background_sky_policy();
        need(hlc_background_sky_set_policy(policy),"Initial startup policy rejected");
        need(!hlc_background_sky_set_policy(policy==BackgroundSkyPolicy::depth
            ? BackgroundSkyPolicy::original : BackgroundSkyPolicy::depth),"Second policy write accepted");
        need(hlc_background_sky_policy()==policy,"Rejected second write changed startup policy");
        const auto o=options(argc,argv);std::filesystem::create_directories(o.output);
        hlclient::platform::SdlRuntime runtime;
        const auto execute=[&](const Options& requested) {
            const bool small_correctness=requested.correctness && !requested.crossfire_correctness;
            hlclient::platform::SdlWindowConfig config;config.title="Background sky isolated fixture";config.width=small_correctness?160:1920;
            config.height=small_correctness?128:1080;config.hidden=requested.correctness;config.vsync=false;config.msaa_samples=0;
            if(!small_correctness)config.mode=hlclient::core::VideoWindowMode::borderless;
            hlclient::platform::SdlWindow window(config);gl::OpenGlRenderer gpu;
            GLint major{},minor{},profile{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
            need((major>3 || (major==3 && minor>=3)) && (profile&GL_CONTEXT_CORE_PROFILE_BIT),"OpenGL 3.3 Core required");
            if(requested.crossfire_correctness || requested.crossfire_benchmark)sky_crossfire_control::run(requested,window,gpu);
            else if(requested.stock_sky_correctness)sky_stock_correction::run(requested,gpu);
            else if(requested.diagnostic_correctness)sky_disable_diagnostic::run(requested,gpu);
            else if(requested.regressions)eligibility_regressions::run(requested,gpu);
            else if(requested.correctness)correctness(requested,gpu);else benchmark(requested,window,gpu);
        };
        execute(o);
        if(o.regressions || o.diagnostic_correctness || o.stock_sky_correctness) {
            // Destroy all renderer/context owners before creating fresh ones.
            // This catches stale GPU-bound resource/range/state caches while
            // preserving process startup policy and the unchanged comparison.
            auto next=o;next.output=o.output/L"context-recreated";
            std::filesystem::create_directory(next.output);execute(next);
        }
        std::cout<<"Artifacts complete; no automatic A/B or target-performance acceptance.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FIXTURE_FAILED: "<<e.what()<<'\n';return 1;}
}
