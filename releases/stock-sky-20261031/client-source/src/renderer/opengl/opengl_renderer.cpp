#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/screen_ui_font.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>

#include "opengl_entity_renderer.hpp"
#include "opengl_scene_target.hpp"
#include "sky_candidate_api.hpp"
#include "sky_depth_guard.hpp"

#include <hlclient/assets/world_lightmap_types.hpp>
#include "opengl_effect_lights.hpp"
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_render/sprite_render_asset.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/renderer/render_line_geometry.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
thread_local SkyCandidateStats background_sky_stats;
thread_local SkyCandidateDiagnostics background_sky_diagnostics;
// Written only on the application startup thread before any render work.
BackgroundSkyPolicy background_sky_policy{BackgroundSkyPolicy::original};
bool background_sky_policy_configured{};
bool background_sky_render_started{};
}
extern "C" bool hlc_background_sky_set_policy(BackgroundSkyPolicy policy) {
    if(background_sky_policy_configured || background_sky_render_started ||
       (policy!=BackgroundSkyPolicy::original && policy!=BackgroundSkyPolicy::depth &&
        policy!=BackgroundSkyPolicy::no_background && policy!=BackgroundSkyPolicy::no_sky_color &&
        policy!=BackgroundSkyPolicy::world_sky_non_occluding))return false;
    background_sky_policy=policy;background_sky_policy_configured=true;return true;
}
extern "C" BackgroundSkyPolicy hlc_background_sky_policy() { return background_sky_policy; }
extern "C" SkyCandidateStats hlc_background_sky_stats() { return background_sky_stats; }
extern "C" const SkyCandidateDiagnostics& hlc_background_sky_diagnostics() { return background_sky_diagnostics; }
extern "C" void hlc_background_sky_reset_stats() { background_sky_stats={};background_sky_diagnostics={}; }

namespace hlclient::renderer::opengl {
namespace {

inline constexpr std::size_t kMaximumDriverLogBytes = 4'096U;

[[noreturn]] void fail(
    const OpenGlRendererErrorCode code,
    std::string context)
{
    throw OpenGlRendererError{code, std::move(context)};
}

[[nodiscard]] std::string open_gl_string(const GLenum name)
{
    const GLubyte* value = glGetString(name);
    if (value == nullptr) {
        return "unavailable";
    }
    return reinterpret_cast<const char*>(value);
}

[[nodiscard]] std::string_view gl_error_name(const GLenum error) noexcept
{
    switch (error) {
    case GL_INVALID_ENUM:
        return "invalid_enum";
    case GL_INVALID_VALUE:
        return "invalid_value";
    case GL_INVALID_OPERATION:
        return "invalid_operation";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "invalid_framebuffer_operation";
    case GL_OUT_OF_MEMORY:
        return "out_of_memory";
    default:
        return "unknown_gl_error";
    }
}

void require_no_gl_error(
    const OpenGlRendererErrorCode code,
    const std::string_view operation)
{
    // One bounded query per critical operation. A failure aborts the current
    // frame/upload transaction, so there is no unbounded error-drain loop.
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fail(code,
            std::string{operation} + " failed with " +
                std::string{gl_error_name(error)}+" (GL error="+
                std::to_string(static_cast<unsigned>(error))+")");
    }
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] GLsizei to_gl_size(
    const std::size_t value,
    const OpenGlRendererErrorCode code,
    const std::string_view context)
{
    if (value > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        fail(code, std::string{context});
    }
    return static_cast<GLsizei>(value);
}

[[nodiscard]] GLsizeiptr to_gl_buffer_size(
    const std::size_t value,
    const std::string_view context)
{
    if (value > static_cast<std::size_t>(
                    std::numeric_limits<GLsizeiptr>::max())) {
        fail(OpenGlRendererErrorCode::buffer_upload_failed,
            std::string{context});
    }
    return static_cast<GLsizeiptr>(value);
}

enum class GlObjectKind {
    none,
    shader,
    program,
    vertex_array,
    buffer,
    texture,
};

class GlObject final {
public:
    GlObject() = default;
    GlObject(const GlObject&) = delete;
    GlObject& operator=(const GlObject&) = delete;

    GlObject(GlObject&& other) noexcept
        : name_{std::exchange(other.name_, 0U)},
          kind_{std::exchange(other.kind_, GlObjectKind::none)}
    {
    }

    GlObject& operator=(GlObject&& other) noexcept
    {
        if (this != &other) {
            reset();
            name_ = std::exchange(other.name_, 0U);
            kind_ = std::exchange(other.kind_, GlObjectKind::none);
        }
        return *this;
    }

    ~GlObject() noexcept
    {
        reset();
    }

    [[nodiscard]] static GlObject adopt(
        const GlObjectKind kind,
        const GLuint name) noexcept
    {
        return GlObject{kind, name};
    }

    [[nodiscard]] GLuint name() const noexcept
    {
        return name_;
    }

    void reset() noexcept
    {
        if (name_ == 0U) {
            kind_ = GlObjectKind::none;
            return;
        }
        switch (kind_) {
        case GlObjectKind::shader:
            glDeleteShader(name_);
            break;
        case GlObjectKind::program:
            glDeleteProgram(name_);
            break;
        case GlObjectKind::vertex_array:
            glDeleteVertexArrays(1, &name_);
            break;
        case GlObjectKind::buffer:
            glDeleteBuffers(1, &name_);
            break;
        case GlObjectKind::texture:
            glDeleteTextures(1, &name_);
            break;
        case GlObjectKind::none:
            break;
        }
        name_ = 0U;
        kind_ = GlObjectKind::none;
    }

private:
    GlObject(const GlObjectKind kind, const GLuint name) noexcept
        : name_{name}, kind_{kind}
    {
    }

    GLuint name_{0U};
    GlObjectKind kind_{GlObjectKind::none};
};

class HudGlStateGuard final {
public:
    HudGlStateGuard() {
        depth_ = glIsEnabled(GL_DEPTH_TEST); blend_ = glIsEnabled(GL_BLEND);
        cull_ = glIsEnabled(GL_CULL_FACE); scissor_ = glIsEnabled(GL_SCISSOR_TEST);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_);
        glGetIntegerv(GL_DEPTH_FUNC, &depth_function_);
        glGetIntegerv(GL_BLEND_SRC_RGB, &source_rgb_); glGetIntegerv(GL_BLEND_DST_RGB, &destination_rgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &source_alpha_); glGetIntegerv(GL_BLEND_DST_ALPHA, &destination_alpha_);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation_rgb_); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation_alpha_);
        glGetIntegerv(GL_SCISSOR_BOX, scissor_box_.data());
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_);
        glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_zero_);
        // GL3.3 queries the sampler on the active unit. The indexed form
        // requires ARB_direct_state_access / core4.5, not merely this function.
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler_zero_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer_);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment_);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length_);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack_skip_rows_);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack_skip_pixels_);
    }
    ~HudGlStateGuard() noexcept {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer_));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row_length_);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpack_skip_rows_);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpack_skip_pixels_);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_zero_));
        glBindSampler(0U, static_cast<GLuint>(sampler_zero_)); glActiveTexture(static_cast<GLenum>(active_texture_));
        glUseProgram(static_cast<GLuint>(program_)); glBindVertexArray(static_cast<GLuint>(array_));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer_));
        glBlendFuncSeparate(source_rgb_,destination_rgb_,source_alpha_,destination_alpha_);
        glBlendEquationSeparate(equation_rgb_,equation_alpha_);
        glScissor(scissor_box_[0],scissor_box_[1],scissor_box_[2],scissor_box_[3]);
        set_enabled(GL_SCISSOR_TEST,scissor_); set_enabled(GL_CULL_FACE,cull_);
        set_enabled(GL_BLEND,blend_); set_enabled(GL_DEPTH_TEST,depth_);
        glDepthMask(depth_mask_); glDepthFunc(static_cast<GLenum>(depth_function_));
    }
private:
    static void set_enabled(const GLenum name,const GLboolean enabled) noexcept {
        if(enabled) glEnable(name); else glDisable(name);
    }
    GLboolean depth_{},blend_{},cull_{},scissor_{},depth_mask_{};
    GLint depth_function_{},source_rgb_{},destination_rgb_{},source_alpha_{},destination_alpha_{};
    GLint equation_rgb_{},equation_alpha_{},program_{},array_{},buffer_{},active_texture_{},texture_zero_{},sampler_zero_{};
    GLint unpack_buffer_{},unpack_alignment_{},unpack_row_length_{},unpack_skip_rows_{},unpack_skip_pixels_{};
    std::array<GLint,4> scissor_box_{};
};

// One state snapshot for all contiguous world-transient runs in a frame.
// Unlike the old per-quad path this also restores bindings after an error.
class TransientBatchGlStateGuard final {
public:
    TransientBatchGlStateGuard() {
        cull_=glIsEnabled(GL_CULL_FACE);blend_=glIsEnabled(GL_BLEND);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write_);
        glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb_);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_alpha_);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_alpha_);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&array_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer_);
    }
    ~TransientBatchGlStateGuard() noexcept {
        glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(buffer_));
        glBindVertexArray(static_cast<GLuint>(array_));
        glUseProgram(static_cast<GLuint>(program_));
        glBlendFuncSeparate(src_rgb_,dst_rgb_,src_alpha_,dst_alpha_);
        if(blend_)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        glDepthMask(depth_write_);
        if(cull_)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    }
private:
    GLboolean cull_{},blend_{},depth_write_{};
    GLint src_rgb_{},dst_rgb_{},src_alpha_{},dst_alpha_{},program_{},array_{},buffer_{};
};

// Extra state touched by the scene-color copy. The existing HUD state guard
// already owns program/VAO, texture/sampler, blend/depth/cull and pixel unpack.
class ImageCopyGlStateGuard final {
public:
    ImageCopyGlStateGuard() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_framebuffer_);
        glGetIntegerv(GL_READ_BUFFER,&read_buffer_);
        glGetBooleanv(GL_COLOR_WRITEMASK,color_mask_.data());
        srgb_=glIsEnabled(GL_FRAMEBUFFER_SRGB); stencil_=glIsEnabled(GL_STENCIL_TEST);
    }
    ~ImageCopyGlStateGuard() noexcept {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(read_framebuffer_));
        glReadBuffer(static_cast<GLenum>(read_buffer_));
        glColorMask(color_mask_[0],color_mask_[1],color_mask_[2],color_mask_[3]);
        if(srgb_)glEnable(GL_FRAMEBUFFER_SRGB);else glDisable(GL_FRAMEBUFFER_SRGB);
        if(stencil_)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);
    }
private:
    GLint read_framebuffer_{},read_buffer_{};
    std::array<GLboolean,4> color_mask_{};
    GLboolean srgb_{},stencil_{};
};

[[nodiscard]] GlObject create_vertex_array()
{
    GLuint name = 0U;
    glGenVertexArrays(1, &name);
    require_no_gl_error(
        OpenGlRendererErrorCode::buffer_upload_failed,
        "OpenGL vertex-array creation");
    if (name == 0U) {
        fail(OpenGlRendererErrorCode::buffer_upload_failed,
            "OpenGL returned no vertex-array object");
    }
    return GlObject::adopt(GlObjectKind::vertex_array, name);
}

[[nodiscard]] GlObject create_buffer()
{
    GLuint name = 0U;
    glGenBuffers(1, &name);
    require_no_gl_error(
        OpenGlRendererErrorCode::buffer_upload_failed,
        "OpenGL buffer creation");
    if (name == 0U) {
        fail(OpenGlRendererErrorCode::buffer_upload_failed,
            "OpenGL returned no buffer object");
    }
    return GlObject::adopt(GlObjectKind::buffer, name);
}

[[nodiscard]] GlObject create_texture(
    const OpenGlRendererErrorCode error_code,
    const std::string_view context)
{
    GLuint name = 0U;
    glGenTextures(1, &name);
    require_no_gl_error(error_code, context);
    if (name == 0U) {
        fail(error_code, std::string{context} + " returned no texture object");
    }
    return GlObject::adopt(GlObjectKind::texture, name);
}

[[nodiscard]] std::string bounded_driver_log(
    const GLuint object,
    const bool shader)
{
    std::array<GLchar, kMaximumDriverLogBytes> storage{};
    GLsizei written = 0;
    if (shader) {
        glGetShaderInfoLog(
            object,
            static_cast<GLsizei>(storage.size()),
            &written,
            storage.data());
    } else {
        glGetProgramInfoLog(
            object,
            static_cast<GLsizei>(storage.size()),
            &written,
            storage.data());
    }
    const auto safe_written = std::clamp(
        written,
        static_cast<GLsizei>(0),
        static_cast<GLsizei>(storage.size() - 1U));
    std::string result;
    result.reserve(static_cast<std::size_t>(safe_written));
    for (GLsizei index = 0; index < safe_written; ++index) {
        const unsigned char character =
            static_cast<unsigned char>(storage[static_cast<std::size_t>(index)]);
        if (character == '\n' || character == '\r' || character == '\t' ||
            (character >= 0x20U && character <= 0x7eU)) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('?');
        }
    }
    return result;
}

inline constexpr char kWorldVertexShader[] = R"GLSL(#version 330 core
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_base_uv;
layout(location = 3) in vec2 in_lightmap_uv;

uniform mat4 u_model_view_projection;
uniform mat4 u_model_transform;

out vec2 fragment_base_uv;
out vec2 fragment_lightmap_uv;
out vec3 fragment_world_position;

void main()
{
    gl_Position = u_model_view_projection * vec4(in_position, 1.0);
    fragment_base_uv = in_base_uv;
    fragment_lightmap_uv = in_lightmap_uv;
    fragment_world_position = (u_model_transform * vec4(in_position, 1.0)).xyz;
}
)GLSL";

inline constexpr char kWorldFragmentShader[] = R"GLSL(#version 330 core
in vec2 fragment_base_uv;
in vec2 fragment_lightmap_uv;
in vec3 fragment_world_position;

uniform sampler2D u_base_texture;
uniform sampler2DArray u_lightmap_texture;
uniform int u_lightmap_enabled;
uniform int u_masked_alpha;
uniform int u_lightmap_layer;
uniform samplerCube u_sky_texture;
uniform int u_sky_surface;
uniform vec3 u_camera_position;
#ifndef HLC_NO_DYNAMIC_LIGHTS
uniform vec3 u_point_light_center;
uniform vec3 u_point_light_color;
uniform float u_point_light_radius;
uniform float u_point_light_intensity;
uniform int u_effect_light_count;
uniform vec4 u_effect_light_center_radius[8];
uniform vec4 u_effect_light_color_intensity[8];
#endif

out vec4 output_color;

void main()
{
    if (u_sky_surface != 0) {
        vec3 direction=fragment_world_position-u_camera_position;
        output_color=vec4(texture(u_sky_texture,direction.xzy).rgb,1.0);
        return;
    }
    vec4 base_sample = texture(u_base_texture, fragment_base_uv);
    if (u_masked_alpha != 0 && base_sample.a < 0.5) {
        discard;
    }
    vec3 light_factor = vec3(1.0);
    if (u_lightmap_enabled != 0) {
        light_factor = texture(
            u_lightmap_texture,
            vec3(fragment_lightmap_uv, float(u_lightmap_layer))).rgb;
    }
#ifndef HLC_NO_DYNAMIC_LIGHTS
    if (u_point_light_radius > 0.0) {
        float falloff = max(0.0, 1.0 - length(fragment_world_position -
            u_point_light_center) / u_point_light_radius);
        light_factor += u_point_light_color *
            (u_point_light_intensity * falloff * falloff);
    }
    for (int i=0; i<u_effect_light_count && i<8; ++i) {
        float falloff=max(0.0,1.0-length(fragment_world_position-
            u_effect_light_center_radius[i].xyz)/u_effect_light_center_radius[i].w);
        light_factor+=u_effect_light_color_intensity[i].rgb *
            (u_effect_light_color_intensity[i].w*falloff*falloff);
    }
#endif
    output_color = vec4(base_sample.rgb * light_factor, base_sample.a);
}
)GLSL";

// This program is used only for static-world opaque, non-sky materials when
// there are no dynamic lights. It is separate from the established world
// shaders so their source, compilation and fallback behaviour stay intact.
inline constexpr char kOpaqueUnlitWorldVertexShader[] = R"GLSL(#version 330 core
layout(location = 0) in vec3 in_position;
layout(location = 2) in vec2 in_base_uv;
layout(location = 3) in vec2 in_lightmap_uv;

uniform mat4 u_model_view_projection;

out vec2 fragment_base_uv;
out vec2 fragment_lightmap_uv;

void main()
{
    gl_Position = u_model_view_projection * vec4(in_position, 1.0);
    fragment_base_uv = in_base_uv;
    fragment_lightmap_uv = in_lightmap_uv;
}
)GLSL";

inline constexpr char kOpaqueUnlitWorldFragmentShader[] = R"GLSL(#version 330 core
in vec2 fragment_base_uv;
in vec2 fragment_lightmap_uv;

uniform sampler2D u_base_texture;
uniform sampler2DArray u_lightmap_texture;
uniform int u_lightmap_enabled;
uniform int u_lightmap_layer;

out vec4 output_color;

void main()
{
    vec4 base_sample = texture(u_base_texture, fragment_base_uv);
    vec3 light_factor = vec3(1.0);
    if (u_lightmap_enabled != 0) {
        light_factor = texture(
            u_lightmap_texture,
            vec3(fragment_lightmap_uv, float(u_lightmap_layer))).rgb;
    }
    output_color = vec4(base_sample.rgb * light_factor, base_sample.a);
}
)GLSL";

// Opaque static-world draws with dynamic lights need world position, but not
// the sky branch or masked-alpha discard. Keep the original light expressions
// and their order unchanged; masked materials and brushes retain the general
// program. This variant is prepared once with the immutable GPU resources.
inline constexpr char kOpaqueLitWorldFragmentShader[] = R"GLSL(#version 330 core
in vec2 fragment_base_uv;
in vec2 fragment_lightmap_uv;
in vec3 fragment_world_position;

uniform sampler2D u_base_texture;
uniform sampler2DArray u_lightmap_texture;
uniform int u_lightmap_enabled;
uniform int u_lightmap_layer;
uniform vec3 u_point_light_center;
uniform vec3 u_point_light_color;
uniform float u_point_light_radius;
uniform float u_point_light_intensity;
uniform int u_effect_light_count;
uniform vec4 u_effect_light_center_radius[8];
uniform vec4 u_effect_light_color_intensity[8];

out vec4 output_color;

#ifdef HLC_FULL_EIGHT_EFFECT_LIGHTS
vec3 effect_contribution(int i)
{
    float falloff=max(0.0,1.0-length(fragment_world_position-
        u_effect_light_center_radius[i].xyz)/u_effect_light_center_radius[i].w);
    return u_effect_light_color_intensity[i].rgb *
        (u_effect_light_color_intensity[i].w*falloff*falloff);
}
#endif

void main()
{
    vec4 base_sample = texture(u_base_texture, fragment_base_uv);
    vec3 light_factor = vec3(1.0);
    if (u_lightmap_enabled != 0) {
        light_factor = texture(
            u_lightmap_texture,
            vec3(fragment_lightmap_uv, float(u_lightmap_layer))).rgb;
    }
    if (u_point_light_radius > 0.0) {
        float falloff = max(0.0, 1.0 - length(fragment_world_position -
            u_point_light_center) / u_point_light_radius);
        light_factor += u_point_light_color *
            (u_point_light_intensity * falloff * falloff);
    }
#ifdef HLC_FULL_EIGHT_EFFECT_LIGHTS
    // Selected once per frame only for eight valid, positive sources. Retain
    // the count uniform contract and exactly the original accumulation order.
    if (u_effect_light_count == 8) {
        light_factor+=effect_contribution(0);
        light_factor+=effect_contribution(1);
        light_factor+=effect_contribution(2);
        light_factor+=effect_contribution(3);
        light_factor+=effect_contribution(4);
        light_factor+=effect_contribution(5);
        light_factor+=effect_contribution(6);
        light_factor+=effect_contribution(7);
    }
#else
    for (int i=0; i<u_effect_light_count && i<8; ++i) {
        float falloff=max(0.0,1.0-length(fragment_world_position-
            u_effect_light_center_radius[i].xyz)/u_effect_light_center_radius[i].w);
        light_factor+=u_effect_light_color_intensity[i].rgb *
            (u_effect_light_color_intensity[i].w*falloff*falloff);
    }
#endif
    output_color = vec4(base_sample.rgb * light_factor, base_sample.a);
}
)GLSL";

[[nodiscard]] GlObject compile_shader(
    const GLenum type,
    const char* const source,
    const std::string_view description)
{
    const GLuint name = glCreateShader(type);
    require_no_gl_error(
        OpenGlRendererErrorCode::shader_compile_failed,
        "OpenGL shader creation");
    if (name == 0U) {
        fail(OpenGlRendererErrorCode::shader_compile_failed,
            std::string{"Unable to create "} + std::string{description});
    }
    auto shader = GlObject::adopt(GlObjectKind::shader, name);
    glShaderSource(name, 1, &source, nullptr);
    glCompileShader(name);
    require_no_gl_error(
        OpenGlRendererErrorCode::shader_compile_failed,
        "OpenGL shader compilation");

    GLint compiled = GL_FALSE;
    glGetShaderiv(name, GL_COMPILE_STATUS, &compiled);
    require_no_gl_error(
        OpenGlRendererErrorCode::shader_compile_failed,
        "OpenGL shader-status query");
    if (compiled != GL_TRUE) {
        const auto log = bounded_driver_log(name, true);
        fail(OpenGlRendererErrorCode::shader_compile_failed,
            std::string{description} + " compilation failed" +
                (log.empty() ? std::string{} : ": " + log));
    }
    return shader;
}

struct ProgramState {
    GlObject program;
    bool omits_dynamic_lights{};
    bool ordinary_opaque_no_dynamic{};
    bool ordinary_opaque_dynamic{};
    GLint model_view_projection{-1};
    GLint model_transform{-1};
    GLint base_texture{-1};
    GLint lightmap_texture{-1};
    GLint lightmap_enabled{-1};
    GLint masked_alpha{-1};
    GLint lightmap_layer{-1};
    GLint sky_texture{-1},sky_surface{-1},camera_position{-1};
    GLint sky_clear_color{-1};
    GLint point_light_center{-1};
    GLint point_light_color{-1};
    GLint point_light_radius{-1};
    GLint point_light_intensity{-1};
    detail::EffectLightUniforms effect_lights;
};

[[nodiscard]] ProgramState create_world_program(
    const bool omit_dynamic_lights = false)
{
    auto vertex = compile_shader(
        GL_VERTEX_SHADER,
        kWorldVertexShader,
        "Built-in static-world vertex shader");
    // Both programs are built with the immutable GPU package, never on a
    // gameplay frame. The ordinary program retains the existing light logic.
    std::string unlit_fragment_source;
    const char* fragment_source=kWorldFragmentShader;
    if (omit_dynamic_lights) {
        constexpr std::string_view version="#version 330 core\n";
        const std::string_view original=kWorldFragmentShader;
        if (!original.starts_with(version)) {
            fail(OpenGlRendererErrorCode::shader_compile_failed,
                "Static-world shader has an unexpected version prelude");
        }
        unlit_fragment_source.reserve(original.size()+
            sizeof("#define HLC_NO_DYNAMIC_LIGHTS 1\n"));
        unlit_fragment_source.append(version);
        unlit_fragment_source.append("#define HLC_NO_DYNAMIC_LIGHTS 1\n");
        unlit_fragment_source.append(original.substr(version.size()));
        fragment_source=unlit_fragment_source.c_str();
    }
    auto fragment = compile_shader(
        GL_FRAGMENT_SHADER,
        fragment_source,
        "Built-in static-world fragment shader");

    const GLuint name = glCreateProgram();
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL program creation");
    if (name == 0U) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Unable to create the built-in static-world shader program");
    }
    auto program = GlObject::adopt(GlObjectKind::program, name);
    glAttachShader(name, vertex.name());
    glAttachShader(name, fragment.name());
    glLinkProgram(name);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL program link");

    GLint linked = GL_FALSE;
    glGetProgramiv(name, GL_LINK_STATUS, &linked);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL program-status query");
    if (linked != GL_TRUE) {
        const auto log = bounded_driver_log(name, false);
        fail(OpenGlRendererErrorCode::program_link_failed,
            std::string{"Built-in static-world shader link failed"} +
                (log.empty() ? std::string{} : ": " + log));
    }

    ProgramState result;
    result.program = std::move(program);
    result.omits_dynamic_lights=omit_dynamic_lights;
    result.model_view_projection =
        glGetUniformLocation(name, "u_model_view_projection");
    result.model_transform = glGetUniformLocation(name, "u_model_transform");
    result.base_texture = glGetUniformLocation(name, "u_base_texture");
    result.lightmap_texture = glGetUniformLocation(name, "u_lightmap_texture");
    result.lightmap_enabled = glGetUniformLocation(name, "u_lightmap_enabled");
    result.masked_alpha = glGetUniformLocation(name, "u_masked_alpha");
    result.lightmap_layer = glGetUniformLocation(name, "u_lightmap_layer");
    result.sky_texture=glGetUniformLocation(name,"u_sky_texture");
    result.sky_surface=glGetUniformLocation(name,"u_sky_surface");
    result.camera_position=glGetUniformLocation(name,"u_camera_position");
    if (!omit_dynamic_lights) {
        result.point_light_center = glGetUniformLocation(name, "u_point_light_center");
        result.point_light_color = glGetUniformLocation(name, "u_point_light_color");
        result.point_light_radius = glGetUniformLocation(name, "u_point_light_radius");
        result.point_light_intensity = glGetUniformLocation(name, "u_point_light_intensity");
        result.effect_lights=detail::EffectLightUniforms::locate(name);
    }
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL uniform lookup");
    if (result.model_view_projection < 0 || result.model_transform < 0 ||
        (!omit_dynamic_lights &&
            (result.point_light_center < 0 || result.point_light_color < 0 ||
             result.point_light_radius < 0 || result.point_light_intensity < 0 ||
             !result.effect_lights.valid())) ||
        result.base_texture < 0 ||
        result.lightmap_texture < 0 || result.lightmap_enabled < 0 ||
        result.masked_alpha < 0 || result.lightmap_layer < 0 ||
        result.sky_texture<0 || result.sky_surface<0 || result.camera_position<0) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Built-in static-world shader did not retain every required uniform");
    }

    glUseProgram(name);
    glUniform1i(result.base_texture, 0);
    glUniform1i(result.lightmap_texture, 1);
    glUniform1i(result.sky_texture, 2);
    glUseProgram(0U);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL sampler-uniform initialization");
    return result;
}

// Diagnostic-only finite BSP/brush sky program. The usual variant has no
// sampler/texture execution. A missing cube needs the second alpha-coverage
// variant: retain base-2D ALPHA without assuming opaque texel/sampler values.
// Both variants replace RGB only, preserving alpha-dependent sample coverage.
// The position expression is identical to the original world vertex shader.
[[nodiscard]] ProgramState create_constant_finite_sky_program(bool alpha_coverage=false)
{
    constexpr char vertex_source[]=R"GLSL(#version 330 core
layout(location = 0) in vec3 in_position;
uniform mat4 u_model_view_projection;
void main() {
    gl_Position = u_model_view_projection * vec4(in_position, 1.0);
}
)GLSL";
    constexpr char fragment_source[]=R"GLSL(#version 330 core
uniform vec4 u_sky_clear_color;
out vec4 output_color;
void main() {
    output_color = vec4(u_sky_clear_color.rgb, 1.0);
}
)GLSL";
    constexpr char coverage_fragment_source[]=R"GLSL(#version 330 core
in vec2 fragment_base_uv;
uniform sampler2D u_base_texture;
uniform int u_masked_alpha;
uniform vec4 u_sky_clear_color;
out vec4 output_color;
void main() {
    vec4 base_sample = texture(u_base_texture, fragment_base_uv);
    if (u_masked_alpha != 0 && base_sample.a < 0.5) {
        discard;
    }
    output_color = vec4(u_sky_clear_color.rgb, base_sample.a);
}
)GLSL";
    // Reuse the exact original vertex shader for the alpha edge, retaining
    // its UV interpolation and MVP expression without changing non-sky code.
    auto vertex=compile_shader(GL_VERTEX_SHADER,alpha_coverage?kWorldVertexShader:vertex_source,
        "Diagnostic finite-sky constant vertex shader");
    auto fragment=compile_shader(GL_FRAGMENT_SHADER,alpha_coverage?coverage_fragment_source:fragment_source,
        "Diagnostic finite-sky constant fragment shader");
    const GLuint name=glCreateProgram();
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky program creation");
    if(!name)fail(OpenGlRendererErrorCode::program_link_failed,"Unable to create diagnostic finite-sky program");
    auto program=GlObject::adopt(GlObjectKind::program,name);
    glAttachShader(name,vertex.name());glAttachShader(name,fragment.name());glLinkProgram(name);
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky program link");
    GLint linked=GL_FALSE;glGetProgramiv(name,GL_LINK_STATUS,&linked);
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky program status");
    if(linked!=GL_TRUE) {
        const auto log=bounded_driver_log(name,false);
        fail(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky shader link failed"+
            (log.empty()?std::string{}:": "+log));
    }
    ProgramState result;
    result.program=std::move(program);result.omits_dynamic_lights=true;
    result.model_view_projection=glGetUniformLocation(name,"u_model_view_projection");
    result.sky_clear_color=glGetUniformLocation(name,"u_sky_clear_color");
    if(alpha_coverage) {
        result.base_texture=glGetUniformLocation(name,"u_base_texture");
        result.masked_alpha=glGetUniformLocation(name,"u_masked_alpha");
    }
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky uniform lookup");
    if(result.model_view_projection<0 || result.sky_clear_color<0 ||
       (alpha_coverage && (result.base_texture<0 || result.masked_alpha<0)))
        fail(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky uniforms were not retained");
    if(alpha_coverage) {
        glUseProgram(name);glUniform1i(result.base_texture,0);glUseProgram(0U);
        require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky coverage sampler");
    }
    return result;
}

[[nodiscard]] ProgramState create_opaque_unlit_world_program()
{
    auto vertex = compile_shader(
        GL_VERTEX_SHADER, kOpaqueUnlitWorldVertexShader,
        "Opaque static-world vertex shader");
    auto fragment = compile_shader(
        GL_FRAGMENT_SHADER, kOpaqueUnlitWorldFragmentShader,
        "Opaque static-world fragment shader");

    const GLuint name = glCreateProgram();
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "Opaque static-world program creation");
    if (name == 0U) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Unable to create the opaque static-world shader program");
    }
    auto program = GlObject::adopt(GlObjectKind::program, name);
    glAttachShader(name, vertex.name());
    glAttachShader(name, fragment.name());
    glLinkProgram(name);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "Opaque static-world program link");
    GLint linked = GL_FALSE;
    glGetProgramiv(name, GL_LINK_STATUS, &linked);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "Opaque static-world program-status query");
    if (linked != GL_TRUE) {
        const auto log = bounded_driver_log(name, false);
        fail(OpenGlRendererErrorCode::program_link_failed,
            std::string{"Opaque static-world shader link failed"} +
                (log.empty() ? std::string{} : ": " + log));
    }

    ProgramState result;
    result.program = std::move(program);
    result.omits_dynamic_lights = true;
    result.ordinary_opaque_no_dynamic = true;
    result.model_view_projection =
        glGetUniformLocation(name, "u_model_view_projection");
    result.base_texture = glGetUniformLocation(name, "u_base_texture");
    result.lightmap_texture = glGetUniformLocation(name, "u_lightmap_texture");
    result.lightmap_enabled = glGetUniformLocation(name, "u_lightmap_enabled");
    result.lightmap_layer = glGetUniformLocation(name, "u_lightmap_layer");
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "Opaque static-world uniform lookup");
    if (result.model_view_projection < 0 || result.base_texture < 0 ||
        result.lightmap_texture < 0 || result.lightmap_enabled < 0 ||
        result.lightmap_layer < 0) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Opaque static-world shader did not retain every required uniform");
    }
    glUseProgram(name);
    glUniform1i(result.base_texture, 0);
    glUniform1i(result.lightmap_texture, 1);
    glUseProgram(0U);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "Opaque static-world sampler-uniform initialization");
    return result;
}

[[nodiscard]] ProgramState create_opaque_lit_world_program(const bool full_eight = false)
{
    auto vertex = compile_shader(
        GL_VERTEX_SHADER, kWorldVertexShader,
        "Opaque dynamic-light static-world vertex shader");
    std::string fragment_source{kOpaqueLitWorldFragmentShader};
    if (full_eight) {
        // Preprocessor specialization preserves the legacy program for 0..7
        // sources: its hot fragment path pays no new uniform branch.
        fragment_source.insert(fragment_source.find('\n') + 1U,
            "#define HLC_FULL_EIGHT_EFFECT_LIGHTS\n");
    }
    auto fragment = compile_shader(
        GL_FRAGMENT_SHADER, fragment_source.c_str(),
        "Opaque dynamic-light static-world fragment shader");
    const GLuint name = glCreateProgram();
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,
        "Opaque dynamic-light static-world program creation");
    if (name == 0U) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Unable to create the opaque dynamic-light static-world program");
    }
    auto program = GlObject::adopt(GlObjectKind::program, name);
    glAttachShader(name, vertex.name());
    glAttachShader(name, fragment.name());
    glLinkProgram(name);
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,
        "Opaque dynamic-light static-world program link");
    GLint linked = GL_FALSE;
    glGetProgramiv(name, GL_LINK_STATUS, &linked);
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,
        "Opaque dynamic-light static-world program-status query");
    if (linked != GL_TRUE) {
        const auto log = bounded_driver_log(name, false);
        fail(OpenGlRendererErrorCode::program_link_failed,
            std::string{"Opaque dynamic-light static-world shader link failed"} +
                (log.empty() ? std::string{} : ": " + log));
    }
    ProgramState result;
    result.program = std::move(program);
    result.ordinary_opaque_dynamic = true;
    result.model_view_projection = glGetUniformLocation(name, "u_model_view_projection");
    result.model_transform = glGetUniformLocation(name, "u_model_transform");
    result.base_texture = glGetUniformLocation(name, "u_base_texture");
    result.lightmap_texture = glGetUniformLocation(name, "u_lightmap_texture");
    result.lightmap_enabled = glGetUniformLocation(name, "u_lightmap_enabled");
    result.lightmap_layer = glGetUniformLocation(name, "u_lightmap_layer");
    result.point_light_center = glGetUniformLocation(name, "u_point_light_center");
    result.point_light_color = glGetUniformLocation(name, "u_point_light_color");
    result.point_light_radius = glGetUniformLocation(name, "u_point_light_radius");
    result.point_light_intensity = glGetUniformLocation(name, "u_point_light_intensity");
    result.effect_lights = detail::EffectLightUniforms::locate(name);
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,
        "Opaque dynamic-light static-world uniform lookup");
    if (result.model_view_projection < 0 || result.model_transform < 0 ||
        result.base_texture < 0 || result.lightmap_texture < 0 ||
        result.lightmap_enabled < 0 || result.lightmap_layer < 0 ||
        result.point_light_center < 0 || result.point_light_color < 0 ||
        result.point_light_radius < 0 || result.point_light_intensity < 0 ||
        !result.effect_lights.valid()) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Opaque dynamic-light static-world shader did not retain every required uniform");
    }
    glUseProgram(name);
    glUniform1i(result.base_texture, 0);
    glUniform1i(result.lightmap_texture, 1);
    glUseProgram(0U);
    require_no_gl_error(OpenGlRendererErrorCode::program_link_failed,
        "Opaque dynamic-light static-world sampler-uniform initialization");
    return result;
}

struct GpuVertex {
    float position_x;
    float position_y;
    float position_z;
    float normal_x;
    float normal_y;
    float normal_z;
    float base_u;
    float base_v;
    float lightmap_u;
    float lightmap_v;
};

static_assert(sizeof(GpuVertex) == sizeof(float) * 10U);
static_assert(std::is_standard_layout_v<GpuVertex>);

[[nodiscard]] bool finite_vertex(
    const world_render::WorldRenderVertex& vertex) noexcept
{
    return std::isfinite(vertex.position.x) &&
        std::isfinite(vertex.position.y) &&
        std::isfinite(vertex.position.z) &&
        std::isfinite(vertex.normal.x) &&
        std::isfinite(vertex.normal.y) &&
        std::isfinite(vertex.normal.z) &&
        std::isfinite(vertex.base_texture_coordinate.x) &&
        std::isfinite(vertex.base_texture_coordinate.y) &&
        std::isfinite(vertex.lightmap_atlas_coordinate.x) &&
        std::isfinite(vertex.lightmap_atlas_coordinate.y);
}

void validate_rgba_dimensions(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::size_t byte_count,
    const OpenGlRendererErrorCode code,
    const std::string_view context)
{
    std::size_t pixel_count = 0U;
    std::size_t expected_bytes = 0U;
    if (width == 0U || height == 0U ||
        !checked_multiply(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            pixel_count) ||
        !checked_multiply(pixel_count, 4U, expected_bytes) ||
        expected_bytes != byte_count ||
        width > static_cast<std::uint32_t>(
                    std::numeric_limits<GLsizei>::max()) ||
        height > static_cast<std::uint32_t>(
                     std::numeric_limits<GLsizei>::max())) {
        fail(code, std::string{context});
    }
}

struct HardwareLimits {
    GLint maximum_texture_dimension{0};
    GLint maximum_array_layers{0};
};

[[nodiscard]] bool valid_alpha_mode(
    const assets::WorldTextureAlphaMode mode) noexcept
{
    return mode == assets::WorldTextureAlphaMode::opaque ||
        mode == assets::WorldTextureAlphaMode::masked_index_255;
}

[[nodiscard]] bool valid_lightmap_mode(
    const world_render::WorldRenderLightmapMode mode) noexcept
{
    return mode == world_render::WorldRenderLightmapMode::atlas ||
        mode == world_render::WorldRenderLightmapMode::unlit_white;
}

[[nodiscard]] HardwareLimits query_hardware_limits()
{
    HardwareLimits result;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &result.maximum_texture_dimension);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &result.maximum_array_layers);
    require_no_gl_error(
        OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL hardware-limit query");
    if (result.maximum_texture_dimension <= 0 ||
        result.maximum_array_layers < static_cast<GLint>(
            assets::kWorldLightmapStyleSlotCount)) {
        fail(OpenGlRendererErrorCode::lightmap_upload_failed,
            "OpenGL implementation cannot retain four lightmap layers");
    }
    return result;
}

void validate_package(
    const world_render::WorldRenderPackage& package,
    const HardwareLimits hardware)
{
    const auto vertices = package.vertices();
    const auto indices = package.indices();
    const auto materials = package.materials();
    const auto batches = package.draw_batches();
    const auto textures = package.textured_world().textures.textures();
    const auto pages = package.lightmaps().pages();

    if (vertices.empty() || indices.empty() || indices.size() % 3U != 0U ||
        materials.empty() || batches.empty() || textures.empty() ||
        !package.textured_world().textures.renderable_for_world_materials() ||
        !package.lightmaps().complete_for_world_surfaces()) {
        fail(OpenGlRendererErrorCode::invalid_world_package,
            "Static-world package is incomplete or has empty render data");
    }
    if (vertices.size() > static_cast<std::size_t>(
                              std::numeric_limits<GLsizei>::max()) ||
        indices.size() > static_cast<std::size_t>(
                             std::numeric_limits<std::uint32_t>::max())) {
        fail(OpenGlRendererErrorCode::buffer_upload_failed,
            "Static-world geometry exceeds OpenGL upload ranges");
    }
    for (const auto& vertex : vertices) {
        if (!finite_vertex(vertex)) {
            fail(OpenGlRendererErrorCode::invalid_world_package,
                "Static-world package contains a non-finite render vertex");
        }
    }
    for (const auto index : indices) {
        if (static_cast<std::size_t>(index) >= vertices.size()) {
            fail(OpenGlRendererErrorCode::invalid_world_package,
                "Static-world package contains an out-of-range vertex index");
        }
    }

    for (const auto& texture : textures) {
        if (texture.mip_levels.size() != assets::kWorldTextureMipLevelCount) {
            fail(OpenGlRendererErrorCode::texture_upload_failed,
                "Base texture does not expose exactly four mip levels");
        }
        for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
            const auto& mip = texture.mip_levels[level];
            if (mip.pixel_format != assets::WorldTexturePixelFormat::rgba8 ||
                mip.width != (texture.width >> level) ||
                mip.height != (texture.height >> level) ||
                mip.width > static_cast<std::uint32_t>(
                                hardware.maximum_texture_dimension) ||
                mip.height > static_cast<std::uint32_t>(
                                 hardware.maximum_texture_dimension)) {
                fail(OpenGlRendererErrorCode::texture_upload_failed,
                    "Base texture mip metadata is not uploadable as RGBA8");
            }
            validate_rgba_dimensions(
                mip.width,
                mip.height,
                mip.rgba_pixels.size(),
                OpenGlRendererErrorCode::texture_upload_failed,
                "Base texture mip RGBA byte count is invalid");
        }
    }

    for (const auto& page : pages) {
        if (page.width > static_cast<std::uint32_t>(
                             hardware.maximum_texture_dimension) ||
            page.height > static_cast<std::uint32_t>(
                              hardware.maximum_texture_dimension)) {
            fail(OpenGlRendererErrorCode::lightmap_upload_failed,
                "Lightmap atlas page exceeds the OpenGL texture limit");
        }
        for (const auto& image : page.style_slot_images) {
            if (image.pixel_format != assets::WorldLightmapPixelFormat::rgba8 ||
                image.width != page.width || image.height != page.height) {
                fail(OpenGlRendererErrorCode::lightmap_upload_failed,
                    "Lightmap atlas style layers do not share RGBA8 dimensions");
            }
            validate_rgba_dimensions(
                image.width,
                image.height,
                image.rgba_pixels.size(),
                OpenGlRendererErrorCode::lightmap_upload_failed,
                "Lightmap atlas RGBA byte count is invalid");
        }
    }

    for (std::size_t material_index = 0U;
         material_index < materials.size();
         ++material_index) {
        const auto& material = materials[material_index];
        if (material.material_index != material_index ||
            material.base_texture_asset_index >= textures.size() ||
            !valid_alpha_mode(material.base_texture_alpha_mode) ||
            !valid_lightmap_mode(material.lightmap_mode) ||
            material.compatibility_profile != world_render::
                                                  WorldRenderCompatibilityProfile::
                                                      goldsrc_static_world_v1 ||
            material.evidence_profile != world_render::WorldRenderEvidenceProfile::
                                             validated_geometry_texture_and_lightmap_bindings ||
            (material.lightmap_mode == world_render::WorldRenderLightmapMode::atlas) !=
                material.lightmap_atlas_page_index.has_value() ||
            (material.lightmap_atlas_page_index &&
                *material.lightmap_atlas_page_index >= pages.size())) {
            fail(OpenGlRendererErrorCode::invalid_world_package,
                "Static-world material binding is invalid");
        }
    }

    std::size_t covered_indices = 0U;
    for (const auto& batch : batches) {
        std::size_t batch_end = 0U;
        if (batch.render_material_index >= materials.size() ||
            batch.index_count == 0U || batch.index_count % 3U != 0U ||
            !valid_alpha_mode(batch.alpha_mode) ||
            !valid_lightmap_mode(batch.lightmap_mode) ||
            !checked_add(
                static_cast<std::size_t>(batch.first_index),
                static_cast<std::size_t>(batch.index_count),
                batch_end) ||
            batch_end > indices.size() ||
            static_cast<std::size_t>(batch.first_index) != covered_indices ||
            batch.alpha_mode !=
                materials[batch.render_material_index].base_texture_alpha_mode ||
            batch.lightmap_mode !=
                materials[batch.render_material_index].lightmap_mode ||
            batch.lightmap_atlas_page_index !=
                materials[batch.render_material_index].lightmap_atlas_page_index ||
            batch.index_count > static_cast<std::uint32_t>(
                                    std::numeric_limits<GLsizei>::max())) {
            fail(OpenGlRendererErrorCode::draw_range_invalid,
                "Static-world draw batch has an invalid index/material range");
        }
        covered_indices = batch_end;
    }
    if (covered_indices != indices.size()) {
        fail(OpenGlRendererErrorCode::draw_range_invalid,
            "Static-world draw batches do not cover the index buffer exactly");
    }
}

struct GeometryState {
    GlObject vertex_array;
    GlObject vertex_buffer;
    GlObject index_buffer;
};

[[nodiscard]] GeometryState upload_geometry(
    const world_render::WorldRenderPackage& package)
{
    std::vector<GpuVertex> upload_vertices;
    upload_vertices.reserve(package.vertices().size());
    for (const auto& vertex : package.vertices()) {
        upload_vertices.push_back(GpuVertex{
            vertex.position.x,
            vertex.position.y,
            vertex.position.z,
            vertex.normal.x,
            vertex.normal.y,
            vertex.normal.z,
            vertex.base_texture_coordinate.x,
            vertex.base_texture_coordinate.y,
            vertex.lightmap_atlas_coordinate.x,
            vertex.lightmap_atlas_coordinate.y,
        });
    }

    std::size_t vertex_bytes = 0U;
    std::size_t index_bytes = 0U;
    if (!checked_multiply(upload_vertices.size(), sizeof(GpuVertex), vertex_bytes) ||
        !checked_multiply(
            package.indices().size(),
            sizeof(std::uint32_t),
            index_bytes)) {
        fail(OpenGlRendererErrorCode::buffer_upload_failed,
            "Static-world buffer byte count overflowed");
    }

    GeometryState result;
    result.vertex_array = create_vertex_array();
    result.vertex_buffer = create_buffer();
    result.index_buffer = create_buffer();

    glBindVertexArray(result.vertex_array.name());
    glBindBuffer(GL_ARRAY_BUFFER, result.vertex_buffer.name());
    glBufferData(
        GL_ARRAY_BUFFER,
        to_gl_buffer_size(vertex_bytes, "Static-world vertex buffer is too large"),
        upload_vertices.data(),
        GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, result.index_buffer.name());
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        to_gl_buffer_size(index_bytes, "Static-world index buffer is too large"),
        package.indices().data(),
        GL_STATIC_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(GpuVertex));
    glEnableVertexAttribArray(0U);
    glVertexAttribPointer(
        0U, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(GpuVertex, position_x)));
    glEnableVertexAttribArray(1U);
    glVertexAttribPointer(
        1U, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(GpuVertex, normal_x)));
    glEnableVertexAttribArray(2U);
    glVertexAttribPointer(
        2U, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(GpuVertex, base_u)));
    glEnableVertexAttribArray(3U);
    glVertexAttribPointer(
        3U, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(GpuVertex, lightmap_u)));
    glBindVertexArray(0U);
    glBindBuffer(GL_ARRAY_BUFFER, 0U);
    require_no_gl_error(
        OpenGlRendererErrorCode::buffer_upload_failed,
        "Static-world VAO/VBO/EBO upload");
    return result;
}

[[nodiscard]] GlObject upload_base_texture(
    const assets::WorldTextureAsset& texture)
{
    auto result = create_texture(
        OpenGlRendererErrorCode::texture_upload_failed,
        "OpenGL base-texture creation");
    glBindTexture(GL_TEXTURE_2D, result.name());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAX_LEVEL,
        static_cast<GLint>(assets::kWorldTextureMipLevelCount - 1U));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
        const auto& mip = texture.mip_levels[level];
        glTexImage2D(
            GL_TEXTURE_2D,
            static_cast<GLint>(level),
            static_cast<GLint>(GL_RGBA8),
            static_cast<GLsizei>(mip.width),
            static_cast<GLsizei>(mip.height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            mip.rgba_pixels.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0U);
    require_no_gl_error(
        OpenGlRendererErrorCode::texture_upload_failed,
        "Four-level RGBA8 base-texture upload");
    return result;
}

[[nodiscard]] GlObject upload_lightmap_page(
    const assets::WorldLightmapAtlasPage& page)
{
    auto result = create_texture(
        OpenGlRendererErrorCode::lightmap_upload_failed,
        "OpenGL lightmap-array creation");
    glBindTexture(GL_TEXTURE_2D_ARRAY, result.name());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        static_cast<GLint>(GL_RGBA8),
        static_cast<GLsizei>(page.width),
        static_cast<GLsizei>(page.height),
        static_cast<GLsizei>(assets::kWorldLightmapStyleSlotCount),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    for (std::size_t layer = 0U;
         layer < page.style_slot_images.size();
         ++layer) {
        const auto& image = page.style_slot_images[layer];
        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0,
            0,
            static_cast<GLint>(layer),
            static_cast<GLsizei>(page.width),
            static_cast<GLsizei>(page.height),
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            image.rgba_pixels.data());
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
    require_no_gl_error(
        OpenGlRendererErrorCode::lightmap_upload_failed,
        "Four-layer RGBA8 lightmap-array upload");
    return result;
}

[[nodiscard]] GlObject upload_white_lightmap()
{
    constexpr std::array<std::uint8_t, 16U> white{
        255U, 255U, 255U, 255U,
        255U, 255U, 255U, 255U,
        255U, 255U, 255U, 255U,
        255U, 255U, 255U, 255U,
    };
    auto result = create_texture(
        OpenGlRendererErrorCode::lightmap_upload_failed,
        "OpenGL white-lightmap creation");
    glBindTexture(GL_TEXTURE_2D_ARRAY, result.name());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        static_cast<GLint>(GL_RGBA8),
        1,
        1,
        static_cast<GLsizei>(assets::kWorldLightmapStyleSlotCount),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        white.data());
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
    require_no_gl_error(
        OpenGlRendererErrorCode::lightmap_upload_failed,
        "White lightmap-array upload");
    return result;
}

struct SkyIndexedSurface {
    std::uint32_t first_index{},index_count{},source_surface_index{};
    assets::WorldBounds bounds{};
};

struct GpuWorldResources {
    std::shared_ptr<const world_render::WorldRenderPackage> package_instance;
    world_render::WorldRendererResourceIdentity identity{};
    ProgramState program;
    std::optional<ProgramState> no_dynamic_light_program;
    std::optional<ProgramState> ordinary_opaque_no_dynamic_program;
    std::optional<ProgramState> ordinary_opaque_dynamic_program;
    std::optional<ProgramState> ordinary_opaque_eight_lights_program;
    std::optional<ProgramState> constant_finite_sky_program;
    std::optional<ProgramState> constant_finite_sky_coverage_program;
    GeometryState geometry;
    std::vector<GlObject> base_textures;
    std::vector<GlObject> lightmap_pages;
    GlObject white_lightmap;
    // Prepared from exactly the uploaded indexed vertices, not source metadata.
    // Owned by this resource generation; never shared with another GL context.
    std::optional<assets::WorldBounds> backdrop_depth_bounds;
    // Exact indexed-vertex bounds, prepared once for this resource revision.
    // Sorted complete intervals allow guards to cover the actual draw without
    // inspecting vertices, allocating per draw, or including another model.
    std::vector<SkyIndexedSurface> backdrop_surfaces;
    bool backdrop_surface_coverage{};
    // Model0 provenance is already retained by the format-facing importer:
    // only model_index==0 has a nonzero world_model_source_face_count.
    // Immutable per-resource metadata, not a per-frame vertex/PVS scan.
    bool confirmed_static_bsp_world{};
};

struct GpuSceneResources {
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        scene_instance;
    world_scene_render::WorldSceneRendererResourceIdentity identity{};
    GpuWorldResources world;
    std::optional<GpuWorldResources> brushes;
    std::vector<GlObject> brush_alternate_textures;
};

[[nodiscard]] GpuWorldResources build_gpu_resources(
    const world_render::WorldRenderPackage& package,
    const bool prepare_no_dynamic_light_program)
{
    GpuWorldResources result;
    result.identity = package.resource_identity();
    if(background_sky_policy==BackgroundSkyPolicy::world_sky_non_occluding) {
        const auto& world=package.textured_world().world;
        result.confirmed_static_bsp_world=
            world.source_profile==assets::WorldGeometrySourceProfile::goldsrc_bsp_v30 &&
            world.statistics.source_version==30 &&
            world.statistics.world_model_source_face_count>0 &&
            world.statistics.world_model_source_face_count==world.surfaces.size();
    }
    result.program = create_world_program();
    if(background_sky_policy==BackgroundSkyPolicy::no_sky_color &&
       std::ranges::any_of(package.materials(),[](const auto& material){return material.sky_surface;})) {
        result.constant_finite_sky_program.emplace(create_constant_finite_sky_program());
        result.constant_finite_sky_coverage_program.emplace(create_constant_finite_sky_program(true));
    }
    if (prepare_no_dynamic_light_program) {
        result.no_dynamic_light_program.emplace(create_world_program(true));
        result.ordinary_opaque_no_dynamic_program.emplace(
            create_opaque_unlit_world_program());
        result.ordinary_opaque_dynamic_program.emplace(
            create_opaque_lit_world_program());
        result.ordinary_opaque_eight_lights_program.emplace(
            create_opaque_lit_world_program(true));
    }
    result.geometry = upload_geometry(package);
    if(background_sky_policy==BackgroundSkyPolicy::depth)for(const auto index:package.indices()) {
        const auto& p=package.vertices()[index].position;
        if(!result.backdrop_depth_bounds)result.backdrop_depth_bounds=assets::WorldBounds{p,p};
        else {
            auto& b=*result.backdrop_depth_bounds;
            b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
            b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
        }
    }
    if(background_sky_policy==BackgroundSkyPolicy::depth) {
        result.backdrop_surfaces.reserve(package.surface_ranges().size());
        bool valid=true;
        for(const auto& surface:package.surface_ranges()) {
            const auto first=static_cast<std::size_t>(surface.first_index);
            const auto count=static_cast<std::size_t>(surface.index_count);
            if(!count || first>package.indices().size() || count>package.indices().size()-first ||
               surface.source_world_surface_index>std::numeric_limits<std::uint32_t>::max()) {valid=false;break;}
            SkyIndexedSurface bound{surface.first_index,surface.index_count,
                static_cast<std::uint32_t>(surface.source_world_surface_index)};
            bool initialized=false;
            for(const auto index:package.indices().subspan(first,count)) {
                if(index>=package.vertices().size()){valid=false;break;}
                const auto& p=package.vertices()[index].position;
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)){valid=false;break;}
                if(!initialized){bound.bounds={p,p};initialized=true;}
                else {
                    auto& b=bound.bounds;
                    b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
                    b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
                }
            }
            if(!valid)break;
            result.backdrop_surfaces.push_back(bound);
        }
        std::ranges::sort(result.backdrop_surfaces,{},&SkyIndexedSurface::first_index);
        std::size_t covered{};
        for(const auto& bound:result.backdrop_surfaces) {
            if(bound.first_index!=covered){valid=false;break;}
            covered+=bound.index_count;
        }
        result.backdrop_surface_coverage=valid && covered==package.indices().size();
    }

    const auto textures = package.textured_world().textures.textures();
    result.base_textures.reserve(textures.size());
    for (const auto& texture : textures) {
        result.base_textures.push_back(upload_base_texture(texture));
    }

    const auto pages = package.lightmaps().pages();
    result.lightmap_pages.reserve(pages.size());
    for (const auto& page : pages) {
        result.lightmap_pages.push_back(upload_lightmap_page(page));
    }
    if (std::ranges::any_of(package.draw_batches(), [](const auto& batch) {
            return batch.lightmap_mode ==
                world_render::WorldRenderLightmapMode::unlit_white;
        })) {
        result.white_lightmap = upload_white_lightmap();
    }
    return result;
}

[[nodiscard]] std::uintptr_t checked_index_byte_offset(
    const std::uint32_t first_index)
{
    std::size_t byte_offset = 0U;
    if (!checked_multiply(
            static_cast<std::size_t>(first_index),
            sizeof(std::uint32_t),
            byte_offset) ||
        byte_offset > std::numeric_limits<std::uintptr_t>::max()) {
        fail(OpenGlRendererErrorCode::draw_range_invalid,
            "Static-world draw offset exceeds the OpenGL pointer range");
    }
    return static_cast<std::uintptr_t>(byte_offset);
}

struct FrameDrawCounters {
    std::uint64_t draw_calls{0U};
    std::uint64_t brush_draw_calls{0U};
    std::uint64_t triangles{0U};
    std::uint64_t base_texture_binds{0U};
    std::uint64_t lightmap_binds{0U};
    std::uint64_t material_uniform_uploads{0U};
    std::uint64_t program_binds{},vertex_array_binds{},sky_texture_binds{};
    std::uint64_t transform_uploads{},camera_uploads{},light_uploads{};
};

// Only the contiguous world/brush draw section owns this state. Its frame
// inputs cannot vary between material ranges; resource transitions invalidate
// the program-local uniforms. Never retain it across render() calls or other
// rendering passes that may alter GL bindings.
struct FrameDrawState {
    const std::optional<RenderPointLight>& light;
    const std::span<const RenderPointLight> effect_lights;
    const GLuint sky_texture;
    const assets::AssetVector3 sky_eye;
    const ClearColor background;
    bool use_constant_finite_sky{};
    bool skipped_static_sky{};
    bool use_no_dynamic_light_world_program{};
    bool use_full_eight_effect_lights{};
    const GpuWorldResources* bound_resources{};
    const ProgramState* bound_program{};
    RenderMatrix4 model_view_projection;
    RenderMatrix4 model_transform;
    bool transforms_valid{};
    // GL texture names and program-local material uniforms are valid only
    // inside this contiguous section. Resource/program changes clear all
    // cached values; no render-pass, context or frame boundary is crossed.
    GLuint base_texture{},lightmap_texture{};
    bool base_texture_valid{},lightmap_texture_valid{};
    std::optional<GLint> sky_surface{},lightmap_enabled{},masked_alpha{};
};

enum class PreparedBlockFrustumRelation {
    outside,
    intersecting,
    inside,
};

// The AABBs were built from the exact indexed vertices. Use a deliberately
// wider-than-roundoff reject margin: uncertain near/far/side-plane cases are
// submitted and clipped by OpenGL rather than becoming missing geometry.
[[nodiscard]] PreparedBlockFrustumRelation classify_prepared_world_bounds(
    const world_visibility::WorldViewFrustum& frustum,
    const assets::WorldBounds& bounds) noexcept
{
    bool entirely_inside = true;
    for (const auto& plane : frustum.planes()) {
        const auto positive_x = plane.normal.x >= 0.0F
            ? bounds.maximum.x : bounds.minimum.x;
        const auto positive_y = plane.normal.y >= 0.0F
            ? bounds.maximum.y : bounds.minimum.y;
        const auto positive_z = plane.normal.z >= 0.0F
            ? bounds.maximum.z : bounds.minimum.z;
        const auto negative_x = plane.normal.x >= 0.0F
            ? bounds.minimum.x : bounds.maximum.x;
        const auto negative_y = plane.normal.y >= 0.0F
            ? bounds.minimum.y : bounds.maximum.y;
        const auto negative_z = plane.normal.z >= 0.0F
            ? bounds.minimum.z : bounds.maximum.z;
        const auto distance = [&](const double x, const double y,
                                  const double z) noexcept {
            return static_cast<double>(plane.normal.x) * x +
                static_cast<double>(plane.normal.y) * y +
                static_cast<double>(plane.normal.z) * z +
                static_cast<double>(plane.signed_offset);
        };
        const auto positive_distance = distance(
            positive_x, positive_y, positive_z);
        const auto negative_distance = distance(
            negative_x, negative_y, negative_z);
        const auto magnitude =
            std::abs(static_cast<double>(plane.normal.x) * positive_x) +
            std::abs(static_cast<double>(plane.normal.y) * positive_y) +
            std::abs(static_cast<double>(plane.normal.z) * positive_z) +
            std::abs(static_cast<double>(plane.signed_offset));
        const auto tolerance = 1.0 + 64.0 *
            static_cast<double>(std::numeric_limits<float>::epsilon()) *
            magnitude;
        if (!std::isfinite(positive_distance) ||
            !std::isfinite(negative_distance) ||
            !std::isfinite(tolerance)) {
            return PreparedBlockFrustumRelation::intersecting;
        }
        if (positive_distance < -tolerance) {
            return PreparedBlockFrustumRelation::outside;
        }
        if (negative_distance < tolerance) {
            entirely_inside = false;
        }
    }
    return entirely_inside ? PreparedBlockFrustumRelation::inside
                           : PreparedBlockFrustumRelation::intersecting;
}

// The original selector is shared, not replayed for eligibility. The default
// policy emits immediate draws; depth records the same emitted ranges once.
template<class Emit>
void select_static_world_ranges(const world_render::WorldRenderPackage& world_package,
    const RenderMatrix4& view_projection,OpenGlWorldRendererStatistics& statistics,
    std::optional<OpenGlStaticWorldFrameReceipt>& static_receipt,Emit&& emit)
{
            const auto batches = world_package.draw_batches();
            const auto prepared_batches =
                world_package.prepared_draw_batches();
            const auto prepared_blocks =
                world_package.prepared_draw_blocks();
            using SelectionClock=std::chrono::steady_clock;
            const auto selection_start=static_receipt ?
                SelectionClock::now() : SelectionClock::time_point{};
            SelectionClock::duration draw_duration{};
            const auto prepared_frustum =
                prepared_batches.size() == batches.size() &&
                    !prepared_blocks.empty()
                ? world_visibility::WorldViewFrustum::from_view_projection(
                      view_projection)
                : world_visibility::WorldViewFrustumCreateResult{};
            if (static_receipt) {
                std::uint64_t candidate_surfaces=0U;
                for (const auto& batch : batches)
                    candidate_surfaces+=batch.source_surface_count;
                static_receipt->considered_surfaces=candidate_surfaces;
                static_receipt->selected_surfaces=0U;
                if (prepared_frustum.frustum) {
                    static_receipt->path=OpenGlStaticWorldPath::prepared_selected;
                    static_receipt->considered_blocks=prepared_blocks.size();
                    static_receipt->tested_bounds=0U;
                    static_receipt->selected_blocks=0U;
                } else {
                    static_receipt->path=OpenGlStaticWorldPath::original_batches;
                    static_receipt->fallback=
                        OpenGlStaticWorldFallback::prepared_unavailable;
                }
            }
            // Near-all-visible views spend more CPU testing every block than
            // the rejected geometry can repay. Probe fixed, evenly spaced
            // immutable blocks; a positive probe only enables the detailed
            // selector. A negative result submits the original full batches,
            // so neither sampling nor camera motion can omit a triangle.
            bool all_batch_bypass = false;
            if (prepared_frustum.frustum) {
                constexpr std::size_t maximum_probes = 16U;
                const auto probe_count = std::min(
                    maximum_probes, prepared_blocks.size());
                std::size_t outside_probes = 0U;
                for (std::size_t probe = 0U; probe < probe_count; ++probe) {
                    const auto block_index =
                        (probe * prepared_blocks.size()) / probe_count;
                    ++statistics.prepared_world_probed_block_count;
                    if (static_receipt)++*static_receipt->tested_bounds;
                    if (classify_prepared_world_bounds(
                            *prepared_frustum.frustum,
                            prepared_blocks[block_index].bounds) ==
                        PreparedBlockFrustumRelation::outside) {
                        ++outside_probes;
                        if (outside_probes >= 2U) {
                            break;
                        }
                    }
                }
                all_batch_bypass = outside_probes < 2U;
                if (all_batch_bypass) {
                    ++statistics.prepared_world_all_batch_bypass_frame_count;
                    if (static_receipt) {
                        static_receipt->path=OpenGlStaticWorldPath::prepared_bypass;
                        static_receipt->fallback=OpenGlStaticWorldFallback::
                            insufficient_probe_rejections;
                    }
                }
            }
            constexpr std::size_t maximum_runs_per_batch = 4U;
            for (std::size_t batch_index = 0U; batch_index < batches.size();
                 ++batch_index) {
                const auto& batch = batches[batch_index];
                const auto draw_range = [&](const std::uint32_t first,
                                            const std::uint32_t count) {
                    const auto draw_start=static_receipt ?
                        SelectionClock::now() : SelectionClock::time_point{};
                    emit(first,count,batch.render_material_index);
                    if (static_receipt)
                        draw_duration+=SelectionClock::now()-draw_start;
                };
                if (!prepared_frustum.frustum || all_batch_bypass) {
                    ++statistics.prepared_world_full_batch_fallback_count;
                    if (static_receipt) {
                        *static_receipt->selected_surfaces+=
                            batch.source_surface_count;
                        if (static_receipt->selected_blocks &&
                            batch_index<prepared_batches.size())
                            *static_receipt->selected_blocks+=
                                prepared_batches[batch_index].block_count;
                    }
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                const auto& prepared = prepared_batches[batch_index];
                if (prepared.block_count == 0U ||
                    prepared.first_block > prepared_blocks.size() ||
                    prepared.block_count >
                        prepared_blocks.size() - prepared.first_block) {
                    ++statistics.prepared_world_full_batch_fallback_count;
                    if (static_receipt) {
                        static_receipt->fallback=OpenGlStaticWorldFallback::
                            invalid_prepared_batch;
                        static_receipt->selected_blocks.reset();
                        *static_receipt->selected_surfaces+=
                            batch.source_surface_count;
                    }
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                if (static_receipt)++*static_receipt->tested_bounds;
                const auto relation = classify_prepared_world_bounds(
                    *prepared_frustum.frustum, prepared.bounds);
                if (relation == PreparedBlockFrustumRelation::outside) {
                    statistics.prepared_world_rejected_block_count +=
                        prepared.block_count;
                    continue;
                }
                if (relation == PreparedBlockFrustumRelation::inside) {
                    statistics.prepared_world_selected_block_count +=
                        prepared.block_count;
                    if (static_receipt) {
                        if (static_receipt->selected_blocks)
                            *static_receipt->selected_blocks+=prepared.block_count;
                        *static_receipt->selected_surfaces+=
                            batch.source_surface_count;
                    }
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                const auto blocks = prepared_blocks.subspan(
                    prepared.first_block, prepared.block_count);
                std::size_t selected_blocks = 0U;
                std::size_t selected_surfaces = 0U;
                std::size_t run_count = 0U;
                struct RetainedRun {
                    std::uint32_t first_index;
                    std::uint32_t end_index;
                };
                std::array<RetainedRun, maximum_runs_per_batch> runs{};
                bool in_run = false;
                for (const auto& block : blocks) {
                    if (static_receipt)++*static_receipt->tested_bounds;
                    const bool selected = classify_prepared_world_bounds(
                        *prepared_frustum.frustum, block.bounds) !=
                        PreparedBlockFrustumRelation::outside;
                    selected_blocks += static_cast<std::size_t>(selected);
                    if (static_receipt && selected)
                        selected_surfaces+=block.source_surface_count;
                    if (selected && !in_run) {
                        ++run_count;
                        if (run_count > maximum_runs_per_batch) {
                            break;
                        }
                        runs[run_count - 1U].first_index = block.first_index;
                    }
                    if (selected) {
                        runs[run_count - 1U].end_index =
                            block.first_index + block.index_count;
                    }
                    in_run = selected;
                }
                if (run_count > maximum_runs_per_batch) {
                    // Avoid trading one material submission for a large number
                    // of tiny draws in a fragmented nearly-visible batch.
                    ++statistics.prepared_world_full_batch_fallback_count;
                    statistics.prepared_world_selected_block_count +=
                        prepared.block_count;
                    if (static_receipt) {
                        if (static_receipt->fallback==OpenGlStaticWorldFallback::none)
                            static_receipt->fallback=OpenGlStaticWorldFallback::
                                fragmented_batch;
                        if (static_receipt->selected_blocks)
                            *static_receipt->selected_blocks+=prepared.block_count;
                        *static_receipt->selected_surfaces+=
                            batch.source_surface_count;
                    }
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                statistics.prepared_world_selected_block_count +=
                    selected_blocks;
                statistics.prepared_world_rejected_block_count +=
                    prepared.block_count - selected_blocks;
                if (static_receipt) {
                    if (static_receipt->selected_blocks)
                        *static_receipt->selected_blocks+=selected_blocks;
                    *static_receipt->selected_surfaces+=selected_surfaces;
                }
                for (std::size_t run = 0U; run < run_count; ++run) {
                    draw_range(runs[run].first_index,
                        runs[run].end_index - runs[run].first_index);
                }
            }
            if (static_receipt) {
                const auto selection_duration=
                    SelectionClock::now()-selection_start-draw_duration;
                static_receipt->selection_preparation_ms=
                    std::chrono::duration<double,std::milli>(
                        selection_duration).count();
            }
}
template<class Emit>
void select_runtime_brush_ranges(const world_scene_render::RuntimeBrushRenderFrame& runtime,
    const world_scene_render::WorldSceneRenderPackage& map,const RenderCamera& camera,
    const RenderMatrix4& view_projection,OpenGlWorldRendererStatistics& statistics,Emit&& emit)
{
    if (!world_scene_render::valid_runtime_brush_frame(runtime, map))
        fail(OpenGlRendererErrorCode::draw_range_invalid,
            "Runtime brush frame does not belong to this map");
    const auto& library=map.brush_library();
    const auto frustum=world_visibility::WorldViewFrustum::from_view_projection(view_projection);
    const auto camera_leaf=world_spatial::WorldSpatialQuery::locate_point(map.spatial_package(),camera.position);
    for(const auto& instance:runtime.instances) {
        if(frustum) {
            const auto classified=frustum.frustum->classify(instance.transformed_bounds);
            if(classified && *classified.classification==world_visibility::WorldBoundsClassification::outside) {
                ++statistics.runtime_brush_culled_count;continue;
            }
        }
        if(camera_leaf && camera_leaf.result->pvs_available && !camera_leaf.result->solid_or_special &&
           !instance.touched_leaf_indices.empty()) {
            bool visible=false;
            for(const auto leaf:instance.touched_leaf_indices) {
                if(leaf==0U || leaf>=map.spatial_package().leaves().size() ||
                   !map.spatial_package().leaves()[leaf].pvs_bit_addressable){visible=true;break;}
                const auto result=map.spatial_package().pvs_table().leaf_is_visible_from(camera_leaf.result->leaf_index,leaf);
                if(!result || *result){visible=true;break;}
            }
            if(!visible){++statistics.runtime_brush_culled_count;continue;}
        }
        const auto models=library.models();
        const auto model=std::ranges::find_if(models,[&](const auto& value){return value.source_model_index()==instance.source_model_index;});
        // valid_runtime_brush_frame has validated this reference, just as in
        // the original loop. The exact float product is retained by depth.
        const auto mvp=renderer::multiply(view_projection,instance.model_transform);
        for(const auto& surface:model->surfaces())emit(surface,instance,mvp);
        ++statistics.runtime_brush_submitted_count;
    }
}

struct SkyPreparedRange {
    GpuWorldResources* resources{};
    std::uint32_t first_index{},index_count{};
    std::size_t material_index{},first_surface{},surface_count{};
    RenderMatrix4 mvp{},transform{};
    assets::WorldBounds bounds{};
    bool brush{},coverage{};
    GLuint alternate_texture{};
    std::uint32_t object_kind{},model_index{0xffffffffU},instance_index{0xffffffffU};
};

struct SkyPreparedSubmission {
    // Capacity is retained between frames; no per-draw owning containers.
    std::vector<SkyPreparedRange> ranges;
    std::optional<OpenGlStaticWorldFrameReceipt> static_receipt;
    std::size_t static_range_count{};
    bool ready{},has_runtime{};
    void reset() noexcept {ranges.clear();static_receipt.reset();static_range_count=0;ready=false;has_runtime=false;}
    void append(GpuWorldResources* gpu,std::uint32_t first,std::uint32_t count,
        std::size_t material,const RenderMatrix4& mvp,const RenderMatrix4& transform,
        bool brush,unsigned kind,unsigned model=0xffffffffU,unsigned instance=0xffffffffU,GLuint alternate=0)
    {
        SkyPreparedRange range;
        range.resources=gpu;range.first_index=first;range.index_count=count;range.material_index=material;
        range.mvp=mvp;range.transform=transform;range.brush=brush;range.object_kind=kind;
        range.model_index=model;range.instance_index=instance;range.alternate_texture=alternate;
        if(gpu && gpu->backdrop_surface_coverage && count && count%3U==0U &&
           first<=gpu->package_instance->indices().size() && count<=gpu->package_instance->indices().size()-first) {
            const auto& surfaces=gpu->backdrop_surfaces;
            const auto begin=std::ranges::lower_bound(surfaces,first,{},&SkyIndexedSurface::first_index);
            const std::size_t end=static_cast<std::size_t>(first)+count;
            std::size_t next=first;
            range.first_surface=static_cast<std::size_t>(begin-surfaces.begin());
            for(auto it=begin;it!=surfaces.end() && next<end;++it) {
                if(it->first_index!=next || it->index_count>end-next)break;
                if(!range.surface_count)range.bounds=it->bounds;
                else {
                    auto& b=range.bounds;const auto& p=it->bounds;
                    b.minimum={std::min(b.minimum.x,p.minimum.x),std::min(b.minimum.y,p.minimum.y),std::min(b.minimum.z,p.minimum.z)};
                    b.maximum={std::max(b.maximum.x,p.maximum.x),std::max(b.maximum.y,p.maximum.y),std::max(b.maximum.z,p.maximum.z)};
                }
                ++range.surface_count;next+=it->index_count;
            }
            range.coverage=next==end;
        }
        ranges.push_back(range);
    }
};

struct SkyScopeNanoseconds {
    std::uint64_t& value;
    const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    ~SkyScopeNanoseconds(){value+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-started).count());}
};

void draw_package_range(
    GpuWorldResources& resources,
    const world_render::WorldRenderPackage& package,
    const std::uint32_t first_index,
    const std::uint32_t index_count,
    const std::size_t material_index,
    const RenderMatrix4& model_view_projection,
    const RenderMatrix4& model_transform,
    const bool brush,
    FrameDrawCounters& counters,
    FrameDrawState& state,
    const GLuint alternate_texture = 0U)
{
    const auto materials = package.materials();
    const auto indices = package.indices();
    const std::size_t first = first_index;
    const std::size_t count = index_count;
    if (index_count == 0U || index_count % 3U != 0U ||
        first > indices.size() || count > indices.size() - first ||
        material_index >= materials.size() ||
        !renderer::is_finite(model_view_projection) || !renderer::is_finite(model_transform)) {
        fail(OpenGlRendererErrorCode::draw_range_invalid,
            "Visible draw command has an invalid package range");
    }
    const auto& material = materials[material_index];
    bool unsupported_static_sky=false,unsupported_brush_sky=false;
    if(background_sky_policy==BackgroundSkyPolicy::world_sky_non_occluding && material.sky_surface) {
        if(brush) {
            // SKY on arbitrary inline/runtime brush entities has no verified
            // stock contract here. Keep its original material/state/draw.
            unsupported_brush_sky=true;
        } else if(resources.confirmed_static_bsp_world &&
                  material.base_texture_alpha_mode==assets::WorldTextureAlphaMode::opaque) {
            // All actual static-world paths converge here with homogeneous
            // material ranges. Return BEFORE any GL/program/cache mutation;
            // neither the finite color nor its depth obstacle is submitted.
            ++background_sky_stats.static_world_sky_skipped_draws;
            if(!state.skipped_static_sky) {
                state.skipped_static_sky=true;
                ++background_sky_stats.static_world_sky_skipped_frames;
                if(!state.sky_texture)++background_sky_stats.static_world_sky_missing_background_frames;
            }
            return;
        } else {
            unsupported_static_sky=true;
        }
    }
    const auto submit_draw=[&] {
        const auto byte_offset=checked_index_byte_offset(first_index);
        glDrawElements(GL_TRIANGLES,to_gl_size(count,OpenGlRendererErrorCode::draw_range_invalid,
            "Visible draw command exceeds GLsizei"),GL_UNSIGNED_INT,reinterpret_cast<const void*>(byte_offset));
        ++counters.draw_calls;
        if(brush)++counters.brush_draw_calls;
        counters.triangles+=static_cast<std::uint64_t>(index_count/3U);
        auto& diagnostic=background_sky_stats;
        if(unsupported_brush_sky)++diagnostic.unsupported_brush_sky_draws;
        if(unsupported_static_sky)++diagnostic.static_world_sky_unsupported_draws;
        ++diagnostic.last_frame_world_draws;
        // Ordered word-wise FNV mixing: no GL query, vertices, allocation or
        // retained draw list. Same range sequence has the same fingerprint
        // across all diagnostic policies, independent of shader selection.
        for(const std::uint64_t word:{static_cast<std::uint64_t>(first_index),
                static_cast<std::uint64_t>(index_count),static_cast<std::uint64_t>(material_index),
                static_cast<std::uint64_t>(brush)}) {
            diagnostic.world_draw_order_hash^=word;
            diagnostic.world_draw_order_hash*=1099511628211ULL;
        }
        if(!material.sky_surface)++diagnostic.non_sky_world_draws;
        else if(state.use_constant_finite_sky)++diagnostic.finite_sky_constant_draws;
        else ++diagnostic.finite_sky_textured_draws;
    };
    if(state.use_constant_finite_sky && material.sky_surface) {
        // With a cube the original sky branch outputs alpha1 before discard.
        // Without one it uses base alpha, even for an opaque material; retain
        // that exact input to inherited sample-alpha-to-coverage and masking.
        const bool alpha_coverage=!state.sky_texture;
        const auto& selected=alpha_coverage?resources.constant_finite_sky_coverage_program:
            resources.constant_finite_sky_program;
        if(!selected)
            fail(OpenGlRendererErrorCode::program_link_failed,"Diagnostic finite-sky program is unavailable");
        const auto& program=*selected;
        if(state.bound_resources!=&resources || state.bound_program!=&program) {
            glUseProgram(program.program.name());++counters.program_binds;
            glBindVertexArray(resources.geometry.vertex_array.name());++counters.vertex_array_binds;
            // Exactly the value selected for this frame's glClearColor,
            // including a package flat_sky_background override. The shader
            // replaces RGB only: original alpha remains a coverage input.
            glUniform4f(program.sky_clear_color,state.background.red,state.background.green,
                state.background.blue,state.background.alpha);
            ++counters.material_uniform_uploads;
            state.bound_resources=&resources;state.bound_program=&program;
            state.transforms_valid=false;state.base_texture_valid=false;state.lightmap_texture_valid=false;
            state.sky_surface.reset();state.lightmap_enabled.reset();state.masked_alpha.reset();
        }
        if(!state.transforms_valid || state.model_view_projection!=model_view_projection) {
            glUniformMatrix4fv(program.model_view_projection,1,GL_FALSE,model_view_projection.values.data());
            state.model_view_projection=model_view_projection;++counters.transform_uploads;
        }
        state.transforms_valid=true;
        if(alpha_coverage) {
            const GLint masked=material.base_texture_alpha_mode==assets::WorldTextureAlphaMode::masked_index_255?1:0;
            if(state.masked_alpha!=masked) {
                glUniform1i(program.masked_alpha,masked);state.masked_alpha=masked;
                ++counters.material_uniform_uploads;
            }
            const GLuint texture=alternate_texture?alternate_texture:
                resources.base_textures[material.base_texture_asset_index].name();
            if(!state.base_texture_valid || state.base_texture!=texture) {
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
                state.base_texture=texture;state.base_texture_valid=true;++counters.base_texture_binds;
            }
        }
        // Geometry, coverage, cull/depth state and submission order are
        // untouched. Both variants have NO cubemap sampler. Only the narrow
        // missing-cube edge samples 2D alpha to preserve coverage/discard holes.
        submit_draw();
        if(alpha_coverage)++background_sky_stats.finite_sky_coverage_alpha_draws;
        return;
    }
    const bool use_ordinary_opaque_no_dynamic =
        state.use_no_dynamic_light_world_program &&
        resources.ordinary_opaque_no_dynamic_program &&
        material.base_texture_alpha_mode ==
            assets::WorldTextureAlphaMode::opaque &&
        !material.sky_surface;
    const ProgramState& program = use_ordinary_opaque_no_dynamic
        ? *resources.ordinary_opaque_no_dynamic_program
        : !state.use_no_dynamic_light_world_program &&
              resources.ordinary_opaque_dynamic_program &&
              material.base_texture_alpha_mode ==
                  assets::WorldTextureAlphaMode::opaque &&
              !material.sky_surface
            ? (state.use_full_eight_effect_lights &&
                  resources.ordinary_opaque_eight_lights_program
                ? *resources.ordinary_opaque_eight_lights_program
                : *resources.ordinary_opaque_dynamic_program)
        :
        state.use_no_dynamic_light_world_program &&
        resources.no_dynamic_light_program
            ? *resources.no_dynamic_light_program : resources.program;
    const bool ordinary_opaque = program.ordinary_opaque_no_dynamic ||
        program.ordinary_opaque_dynamic;

    if(state.bound_resources!=&resources || state.bound_program!=&program) {
        glUseProgram(program.program.name());
        ++counters.program_binds;
        glBindVertexArray(resources.geometry.vertex_array.name());
        ++counters.vertex_array_binds;
        if (!ordinary_opaque) {
            glUniform3f(program.camera_position,state.sky_eye.x,state.sky_eye.y,state.sky_eye.z);
            ++counters.camera_uploads;
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_CUBE_MAP,state.sky_texture);
        ++counters.sky_texture_binds;
        if (!program.omits_dynamic_lights) {
            const auto& light=state.light;
            if(light && valid_render_point_light(*light) && light->intensity>0.0F) {
                glUniform3f(program.point_light_center,
                    light->center.x,light->center.y,light->center.z);
                glUniform3f(program.point_light_color,
                    light->color[0],light->color[1],light->color[2]);
                glUniform1f(program.point_light_radius,light->radius_units);
                glUniform1f(program.point_light_intensity,light->intensity);
            } else {
                glUniform1f(program.point_light_radius,0.0F);
                glUniform1f(program.point_light_intensity,0.0F);
            }
            program.effect_lights.upload(state.effect_lights);
            ++counters.light_uploads;
        }
        glUniform1i(program.lightmap_layer,0);
        state.bound_resources=&resources;
        state.bound_program=&program;
        state.transforms_valid=false;
        state.base_texture_valid=false;
        state.lightmap_texture_valid=false;
        state.sky_surface.reset();
        state.lightmap_enabled.reset();
        state.masked_alpha.reset();
    }
    const GLint sky_surface=state.sky_texture && material.sky_surface ? 1 : 0;
    if(state.sky_surface!=sky_surface) {
        if (!ordinary_opaque) {
            glUniform1i(program.sky_surface,sky_surface);
            ++counters.material_uniform_uploads;
        }
        state.sky_surface=sky_surface;
    }
    if(!state.transforms_valid || state.model_view_projection!=model_view_projection) {
        glUniformMatrix4fv(program.model_view_projection,1,GL_FALSE,
            model_view_projection.values.data());
        state.model_view_projection=model_view_projection;
        ++counters.transform_uploads;
    }
    if(!state.transforms_valid || state.model_transform!=model_transform) {
        if (!program.ordinary_opaque_no_dynamic) {
            glUniformMatrix4fv(program.model_transform,1,GL_FALSE,
                model_transform.values.data());
            ++counters.transform_uploads;
        }
        state.model_transform=model_transform;
    }
    state.transforms_valid=true;

    const GLuint base_texture=alternate_texture ? alternate_texture :
        resources.base_textures[material.base_texture_asset_index].name();
    if(!state.base_texture_valid || state.base_texture!=base_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,base_texture);
        state.base_texture=base_texture;
        state.base_texture_valid=true;
        ++counters.base_texture_binds;
    }

    const bool atlas=material.lightmap_mode==world_render::WorldRenderLightmapMode::atlas;
    const GLuint lightmap_texture=atlas ?
        resources.lightmap_pages[*material.lightmap_atlas_page_index].name() :
        resources.white_lightmap.name();
    if(!state.lightmap_texture_valid || state.lightmap_texture!=lightmap_texture) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY,lightmap_texture);
        state.lightmap_texture=lightmap_texture;
        state.lightmap_texture_valid=true;
        ++counters.lightmap_binds;
    }
    const GLint lightmap_enabled=atlas ? 1 : 0;
    if(state.lightmap_enabled!=lightmap_enabled) {
        glUniform1i(program.lightmap_enabled,lightmap_enabled);
        state.lightmap_enabled=lightmap_enabled;
        ++counters.material_uniform_uploads;
    }
    const GLint masked_alpha=material.base_texture_alpha_mode==
        assets::WorldTextureAlphaMode::masked_index_255 ? 1 : 0;
    if(state.masked_alpha!=masked_alpha) {
        if (!ordinary_opaque) {
            glUniform1i(program.masked_alpha,masked_alpha);
            ++counters.material_uniform_uploads;
        }
        state.masked_alpha=masked_alpha;
    }

    submit_draw();
}

} // namespace

std::string_view to_string(const OpenGlRendererErrorCode code) noexcept
{
    switch (code) {
    case OpenGlRendererErrorCode::shader_compile_failed:
        return "shader_compile_failed";
    case OpenGlRendererErrorCode::program_link_failed:
        return "program_link_failed";
    case OpenGlRendererErrorCode::invalid_world_package:
        return "invalid_world_package";
    case OpenGlRendererErrorCode::buffer_upload_failed:
        return "buffer_upload_failed";
    case OpenGlRendererErrorCode::texture_upload_failed:
        return "texture_upload_failed";
    case OpenGlRendererErrorCode::lightmap_upload_failed:
        return "lightmap_upload_failed";
    case OpenGlRendererErrorCode::camera_invalid:
        return "camera_invalid";
    case OpenGlRendererErrorCode::draw_range_invalid:
        return "draw_range_invalid";
    case OpenGlRendererErrorCode::gl_operation_failed:
        return "gl_operation_failed";
    case OpenGlRendererErrorCode::unable_to_retain_resources:
        return "unable_to_retain_resources";
    case OpenGlRendererErrorCode::invalid_entity_scene:
        return "invalid_entity_scene";
    case OpenGlRendererErrorCode::entity_capability_unsupported:
        return "entity_capability_unsupported";
    case OpenGlRendererErrorCode::entity_buffer_upload_failed:
        return "entity_buffer_upload_failed";
    case OpenGlRendererErrorCode::entity_texture_upload_failed:
        return "entity_texture_upload_failed";
    case OpenGlRendererErrorCode::entity_draw_invalid:
        return "entity_draw_invalid";
    }
    return "unknown";
}

OpenGlRendererError::OpenGlRendererError(
    const OpenGlRendererErrorCode code,
    std::string context)
    : std::runtime_error{std::move(context)}, code_{code}
{
}

OpenGlRendererErrorCode OpenGlRendererError::code() const noexcept
{
    return code_;
}

class OpenGlRenderer::Implementation final {
public:
    void record_startup_context(const GLint profile) {
        // The constructor has queried this actual context value and checked
        // GL errors already. Cache for this renderer/context lifetime only.
        context_profile_mask_=profile;
        context_profile_query_checked_=true;
        const auto fact=[&](const char* key,const std::string& value){
            std::fprintf(stderr,"[opengl-startup] context=%llu %s=%s\n",
                static_cast<unsigned long long>(startup_context_),key,value.c_str());
        };
        const auto bounded_string=[](GLenum name){
            const auto* raw=reinterpret_cast<const unsigned char*>(glGetString(name));
            std::string out;
            if(!raw)return std::string{"unavailable"};
            for(std::size_t i=0;i<256U && raw[i];++i)out+=raw[i]>=32U && raw[i]!=127U?static_cast<char>(raw[i]):'_';
            return out;
        };
        fact("vendor",bounded_string(GL_VENDOR));fact("renderer",bounded_string(GL_RENDERER));
        fact("version",bounded_string(GL_VERSION));fact("glsl",bounded_string(GL_SHADING_LANGUAGE_VERSION));
        GLint major{},minor{},extensions{};
        glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
        glGetIntegerv(GL_NUM_EXTENSIONS,&extensions);
        context_major_=major;context_minor_=minor;
        bool dsa=false;
        if(extensions>=0 && extensions<=16384)for(GLint i=0;i<extensions;++i){
            const auto* name=reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,static_cast<GLuint>(i)));
            if(name && std::strncmp(name,"GL_ARB_direct_state_access",sizeof("GL_ARB_direct_state_access"))==0){dsa=true;break;}
        }
        fact("actual_version",std::to_string(major)+"."+std::to_string(minor));
        fact("profile_mask",std::to_string(profile));
        fact("indexed_sampler_query_capability",(major>4 || (major==4 && minor>=5) || dsa)?"supported":"not_advertised");
        fact("sampler_query_path","GL33_active_unit_GetIntegerv");
        std::fflush(stderr);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"OpenGL startup context facts");
    }
    void begin_startup_frame() noexcept {
        startup_checks_=startup_frame_<3U;
        if(startup_checks_)++startup_frame_;
    }
    void startup_boundary(const std::string_view stage) const {
        if(startup_checks_)require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "OpenGL startup context="+std::to_string(startup_context_)+" frame="+
            std::to_string(startup_frame_)+" stage="+std::string{stage});
    }
    GLuint prepare_sky(const std::shared_ptr<const assets::SkyboxAsset>& sky) {
        if(!sky) {sky_asset_.reset();sky_texture_.reset();return 0;}
        if(sky_asset_==sky)return sky_texture_.name();
        sky_asset_.reset();sky_texture_.reset();
        const auto size=sky->faces[0].width;
        if(!size || size>1024)return 0;
        for(const auto& face:sky->faces)if(face.width!=size || face.height!=size ||
            face.pixel_format!=assets::ImagePixelFormat::rgba8 || face.pixels.size()!=std::size_t(size)*size*4)return 0;
        HudGlStateGuard state;
        startup_boundary("sky_upload_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        glActiveTexture(GL_TEXTURE0);
        GLint old_cube{};glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&old_cube);
        struct RestoreCube {GLint old;~RestoreCube(){glBindTexture(GL_TEXTURE_CUBE_MAP,static_cast<GLuint>(old));}} restore{old_cube};
        auto texture=create_texture(OpenGlRendererErrorCode::texture_upload_failed,"Environment cube texture");
        glBindTexture(GL_TEXTURE_CUBE_MAP,texture.name());glBindSampler(0,0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        for(unsigned i=0;i<6;++i)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i,0,GL_RGBA8,
            static_cast<GLsizei>(size),static_cast<GLsizei>(size),0,GL_RGBA,GL_UNSIGNED_BYTE,sky->faces[i].pixels.data());
        // Cubemap edges are sampled seamlessly. One linear level retains the
        // original sky panels without unrelated atlas borders or mip fringes.
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAX_LEVEL,0);
        require_no_gl_error(OpenGlRendererErrorCode::texture_upload_failed,"Environment cube upload");
        sky_texture_=std::move(texture);sky_asset_=sky;return sky_texture_.name();
    }
    void render_sky(GLuint texture,const RenderCamera& camera,RenderExtent extent,bool behind_opaque=false) {
        if(!texture)return;
        if(!is_valid(camera))fail(OpenGlRendererErrorCode::camera_invalid,"Environment camera invalid");
        const auto forward=normalize(assets::AssetVector3{camera.target.x-camera.position.x,
            camera.target.y-camera.position.y,camera.target.z-camera.position.z});
        // Match the canonical look-at contract: accepted up vectors need not
        // be unit length. Crossing the raw up vector can spuriously underflow
        // the normalization threshold even after the camera passed validation.
        const auto normalized_up=normalize(camera.up);
        if(!forward || !normalized_up)fail(OpenGlRendererErrorCode::camera_invalid,"Environment camera basis invalid");
        const auto right=normalize(cross(*forward,*normalized_up));
        if(!right)fail(OpenGlRendererErrorCode::camera_invalid,"Environment camera side invalid");
        const auto up=cross(*right,*forward);
        HudGlStateGuard state;
        startup_boundary("sky_draw_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        glActiveTexture(GL_TEXTURE0);GLint old_cube{};glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&old_cube);
        const auto seamless=glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        struct RestoreCube {GLint old;GLboolean seamless;~RestoreCube(){glBindTexture(GL_TEXTURE_CUBE_MAP,static_cast<GLuint>(old));if(!seamless)glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);}} restore{old_cube,seamless};
        if(!sky_program_.name()) {
            constexpr char vertex[]=R"GLSL(#version 330 core
out vec2 screen;
void main() {
    vec2 points[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    screen=points[gl_VertexID];gl_Position=vec4(screen,1,1);
}
)GLSL";
            constexpr char fragment[]=R"GLSL(#version 330 core
in vec2 screen;
uniform samplerCube u_cube;
uniform vec3 u_forward,u_right,u_up;
uniform vec2 u_scale;
out vec4 output_color;
void main() {
    vec3 direction=u_forward+u_right*screen.x*u_scale.x+u_up*screen.y*u_scale.y;
    output_color=vec4(texture(u_cube,direction.xzy).rgb,1);
}
)GLSL";
            auto vs=compile_shader(GL_VERTEX_SHADER,vertex,"Environment vertex shader");
            auto fs=compile_shader(GL_FRAGMENT_SHADER,fragment,"Environment fragment shader");
            auto program=GlObject::adopt(GlObjectKind::program,glCreateProgram());
            glAttachShader(program.name(),vs.name());glAttachShader(program.name(),fs.name());glLinkProgram(program.name());
            GLint linked{};glGetProgramiv(program.name(),GL_LINK_STATUS,&linked);
            if(linked!=GL_TRUE)fail(OpenGlRendererErrorCode::program_link_failed,"Environment shader link failed");
            sky_forward_=glGetUniformLocation(program.name(),"u_forward");sky_right_=glGetUniformLocation(program.name(),"u_right");
            sky_up_=glGetUniformLocation(program.name(),"u_up");sky_scale_=glGetUniformLocation(program.name(),"u_scale");
            glUseProgram(program.name());glUniform1i(glGetUniformLocation(program.name(),"u_cube"),0);
            sky_array_=create_vertex_array();sky_program_=std::move(program);
        }
        glUseProgram(sky_program_.name());glBindVertexArray(sky_array_.name());
        glUniform3f(sky_forward_,forward->x,forward->y,forward->z);
        glUniform3f(sky_right_,right->x,right->y,right->z);glUniform3f(sky_up_,up.x,up.y,up.z);
        const auto scale=std::tan(camera.vertical_field_of_view_radians*.5F);
        glUniform2f(sky_scale_,scale*static_cast<float>(extent.width)/static_cast<float>(extent.height),scale);
        glBindSampler(0,0);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        if(behind_opaque){glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);}else glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
        glDrawArrays(GL_TRIANGLES,0,3);
        ++background_sky_stats.fullscreen_sky_draws;
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Environment backdrop draw");
    }
    ~Implementation() noexcept {
        if(gpu_timing_.allocated_queries) glDeleteQueries(8,gpu_queries_.data());
        if(gpu_timing_.allocated_pass_queries)
            glDeleteQueries(static_cast<GLsizei>(gpu_pass_queries_.size()),gpu_pass_queries_.data());
    }
    void reset_session_resources() noexcept {
        // Called outside render(), never while a query owned by another
        // component is active. Pending old-generation timings are discarded.
        end_gpu_timing();
        if(gpu_timing_.allocated_queries)glDeleteQueries(8,gpu_queries_.data());
        if(gpu_timing_.allocated_pass_queries)
            glDeleteQueries(static_cast<GLsizei>(gpu_pass_queries_.size()),gpu_pass_queries_.data());
        gpu_queries_={};gpu_frames_={};gpu_pending_={};gpu_active_.reset();
        gpu_pass_queries_={};gpu_pass_frames_={};
        gpu_timing_={};gpu_timing_failed_=false;gpu_timing_unsupported_=false;
        release_world_resources();entity_renderer_.reset_session_resources();
        statistics_={};decal_statistics_={};decal_frame_serial_=0;
        hud_textures_.clear();hud_vertices_.clear();hud_batches_.clear();
        hud_commands_.reset();hud_extent_={};hud_cache_clock_=0;hud_statistics_={};
        sky_asset_.reset();sky_texture_.reset();
        flash_batch_vertices_.clear();flash_batch_gpu_cursor_=0;
        flash_batch_gpu_orphaned_this_frame_=false;
        flash_batch_frame_vertex_capacity_=0;
        image_texture_.reset();image_program_.reset();image_array_.reset();
        image_adjustments_=-1;image_extent_={};image_statistics_={};
        scene_target.reset_session_resources();
        // Immutable shaders, font geometry facilities and screen_ui_textures_
        // belong to the application, not to a server/map generation.
    }
    bool begin_gpu_timing(const std::uint64_t frame_number) noexcept {
        if(gpu_timing_failed_) {
            ++gpu_timing_.unavailable;
            gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::invalid;
            return false;
        }
        if(gpu_timing_unsupported_ || !glGenQueries || !glBeginQuery || !glEndQuery || !glGetQueryObjectiv || !glGetQueryObjectui64v) {
            ++gpu_timing_.unavailable;
            gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::unsupported;
            return false;
        }
        GLint foreign{};
        glGetQueryiv(GL_TIME_ELAPSED,GL_CURRENT_QUERY,&foreign);
        if(!gpu_call_succeeded())return false;
        if(foreign || gpu_active_) {
            ++gpu_timing_.unavailable;
            gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::invalid;
            return false;
        }
        if(!gpu_timing_.allocated_queries) {
            GLint bits{};glGetQueryiv(GL_TIME_ELAPSED,GL_QUERY_COUNTER_BITS,&bits);
            if(!gpu_call_succeeded())return false;
            if(!bits){gpu_timing_unsupported_=true;++gpu_timing_.unavailable;
                gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::unsupported;return false;}
            glGenQueries(8,gpu_queries_.data());
            gpu_timing_.allocated_queries=8U;
            if(!gpu_call_succeeded())return false;
            GLint timestamp_bits{};
            if(glQueryCounter)glGetQueryiv(GL_TIMESTAMP,GL_QUERY_COUNTER_BITS,&timestamp_bits);
            if(!gpu_call_succeeded())return false;
            gpu_timing_.pass_timestamps_supported=timestamp_bits>0;
            if(gpu_timing_.pass_timestamps_supported) {
                glGenQueries(static_cast<GLsizei>(gpu_pass_queries_.size()),gpu_pass_queries_.data());
                gpu_timing_.allocated_pass_queries=gpu_pass_queries_.size();
                if(!gpu_call_succeeded())return false;
            }
            GLint extensions{};glGetIntegerv(GL_NUM_EXTENSIONS,&extensions);
            for(GLint extension=0;extension<extensions;++extension) {
                const auto* name=glGetStringi(GL_EXTENSIONS,static_cast<GLuint>(extension));
                if(name && std::string_view{reinterpret_cast<const char*>(name)}=="GL_EXT_disjoint_timer_query")
                    gpu_timing_.disjoint_detection_supported=true;
            }
            if(!gpu_call_succeeded())return false;
        }
        for(std::size_t i=0;i<gpu_queries_.size();++i) if(!gpu_pending_[i] && gpu_queries_[i]) {
            glBeginQuery(GL_TIME_ELAPSED,gpu_queries_[i]);
            if(!gpu_call_succeeded())return false;
            gpu_frames_[i]=frame_number;gpu_pending_[i]=true;gpu_active_=i;
            gpu_pass_frames_[i]={};
            ++gpu_timing_.submitted;++gpu_timing_.pending;
            gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::pending;
            // glBeginQuery was just checked, so there is no prior render
            // operation whose error could be confused with this timestamp.
            gpu_pass_boundary_query(OpenGlGpuPass::world,
                OpenGlWorldGpuSubpass::target_setup_clear);
            return true;
        }
        ++gpu_timing_.capacity_drops;
        gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::capacity_dropped;
        return false;
    }
    void gpu_pass_boundary(const OpenGlGpuPass next) {
        if(!gpu_active_ || !gpu_timing_.pass_timestamps_supported || gpu_timing_failed_)return;
        const auto& frame=gpu_pass_frames_[*gpu_active_];
        if(frame.count && frame.passes[frame.count-1U]==next)return;
        // Errors from the preceding render pass are authoritative. Only an
        // error created after this clean boundary may disable optional timing.
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "Render pass before GPU timestamp");
        gpu_pass_boundary_query(next);
    }
    void gpu_world_subpass_boundary(const OpenGlWorldGpuSubpass next) {
        if(!gpu_active_ || !gpu_timing_.pass_timestamps_supported || gpu_timing_failed_)return;
        const auto& frame=gpu_pass_frames_[*gpu_active_];
        if(!frame.count || frame.passes[frame.count-1U]!=OpenGlGpuPass::world ||
            frame.world_subpasses[frame.count-1U]==next)return;
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "World subpass before GPU timestamp");
        gpu_pass_boundary_query(OpenGlGpuPass::world,next);
    }
    void gpu_pass_boundary_query(const OpenGlGpuPass next,
        const std::optional<OpenGlWorldGpuSubpass> world_subpass={}) noexcept {
        if(!gpu_active_ || !gpu_timing_.pass_timestamps_supported || gpu_timing_failed_)return;
        auto& frame=gpu_pass_frames_[*gpu_active_];
        if(frame.count && frame.passes[frame.count-1U]==next &&
            frame.world_subpasses[frame.count-1U]==world_subpass)return;
        // Reserve the final timestamp. Overflow invalidates observations, not
        // the frame or its rendering. The ordered path uses <=14 including
        // three optional world-detail boundaries, plus the final timestamp.
        if(frame.count>=kGpuPassBoundaryLimit-1U) {frame.invalid=true;return;}
        const auto index=*gpu_active_*kGpuPassBoundaryLimit+frame.count;
        glQueryCounter(gpu_pass_queries_[index],GL_TIMESTAMP);
        if(!gpu_call_succeeded()){frame.invalid=true;return;}
        frame.passes[frame.count]=next;
        frame.world_subpasses[frame.count]=world_subpass;
        ++frame.count;
    }
    void end_gpu_timing(const bool frame_completed=true) noexcept {
        if(gpu_active_) {
            auto& frame=gpu_pass_frames_[*gpu_active_];
            if(gpu_timing_.pass_timestamps_supported && frame.count && !gpu_timing_failed_) {
                const auto index=*gpu_active_*kGpuPassBoundaryLimit+frame.count;
                glQueryCounter(gpu_pass_queries_[index],GL_TIMESTAMP);
                if(!gpu_call_succeeded())frame.invalid=true;
                ++frame.count;
            }
            glEndQuery(GL_TIME_ELAPSED);
            if(!gpu_call_succeeded() || !frame_completed) {
                gpu_pending_[*gpu_active_]=false;--gpu_timing_.pending;
                gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::invalid;
            }
            gpu_active_.reset();
        }
    }
    std::optional<OpenGlGpuFrameTiming> consume_invalid_gpu_timing() noexcept {
        // A failed optional query disables further submissions, but every
        // already-submitted frame still receives a bounded terminal result.
        // No GL call is needed to drain these at-most-eight identities.
        for(std::size_t i=0;i<gpu_pending_.size();++i)
            if(gpu_pending_[i] && gpu_active_!=i) {
                gpu_pending_[i]=false;--gpu_timing_.pending;
                ++gpu_timing_.completed;++gpu_timing_.invalid_results;
                OpenGlGpuFrameTiming result;
                result.frame_number=gpu_frames_[i];
                result.elapsed_ms=std::numeric_limits<double>::quiet_NaN();
                result.status=OpenGlGpuTimingStatus::invalid;
                return result;
            }
        return {};
    }
    std::optional<OpenGlGpuFrameTiming> poll_gpu_timing() noexcept {
        if(gpu_timing_failed_)return consume_invalid_gpu_timing();
        if(gpu_timing_.disjoint_detection_supported) {
            GLboolean disjoint{};
            // EXT exposes a reset-on-read sticky flag. Mark every outstanding
            // slot together; a later poll must not accidentally bless one.
            glGetBooleanv(0x8FBB /* GL_GPU_DISJOINT_EXT */,&disjoint);
            if(!gpu_call_succeeded())return consume_invalid_gpu_timing();
            if(disjoint==GL_TRUE)
                for(std::size_t i=0;i<gpu_pending_.size();++i)
                    if(gpu_pending_[i])gpu_pass_frames_[i].disjoint=true;
        }
        // Availability is checked first; never wait/flush for a pending query.
        for(std::size_t i=0;i<gpu_queries_.size();++i) if(gpu_pending_[i] && gpu_active_!=i) {
            GLint available{};glGetQueryObjectiv(gpu_queries_[i],GL_QUERY_RESULT_AVAILABLE,&available);
            if(!gpu_call_succeeded())return consume_invalid_gpu_timing();
            if(available!=GL_TRUE) continue;
            const auto& passes=gpu_pass_frames_[i];
            if(passes.count) {
                GLint final_available{};
                glGetQueryObjectiv(gpu_pass_queries_[i*kGpuPassBoundaryLimit+passes.count-1U],
                    GL_QUERY_RESULT_AVAILABLE,&final_available);
                if(!gpu_call_succeeded())return consume_invalid_gpu_timing();
                if(final_available!=GL_TRUE)continue;
            }
            GLuint64 elapsed{};glGetQueryObjectui64v(gpu_queries_[i],GL_QUERY_RESULT,&elapsed);
            if(!gpu_call_succeeded())return consume_invalid_gpu_timing();
            OpenGlGpuFrameTiming result;
            result.frame_number=gpu_frames_[i];
            result.elapsed_ms=static_cast<double>(elapsed)/1'000'000.0;
            result.status=gpu_timing_.pass_timestamps_supported ?
                OpenGlGpuTimingStatus::ready:OpenGlGpuTimingStatus::unsupported;
            if(passes.disjoint) {
                result.status=OpenGlGpuTimingStatus::disjoint;
                result.elapsed_ms=std::numeric_limits<double>::quiet_NaN();
                ++gpu_timing_.disjoint_results;
                gpu_pending_[i]=false;--gpu_timing_.pending;++gpu_timing_.completed;
                return result;
            }
            if(passes.invalid) {
                result.status=OpenGlGpuTimingStatus::invalid;++gpu_timing_.invalid_results;
                gpu_pending_[i]=false;--gpu_timing_.pending;++gpu_timing_.completed;
                return result;
            }
            if(passes.count>1U) {
                std::array<GLuint64,kGpuPassBoundaryLimit> timestamps{};
                // Completion of the final timestamp implies prior timestamp
                // completion in this same GL stream. No flush or blocking read
                // is used when either the total or final query is pending.
                for(std::size_t stamp=0;stamp<passes.count;++stamp) {
                    glGetQueryObjectui64v(gpu_pass_queries_[i*kGpuPassBoundaryLimit+stamp],
                        GL_QUERY_RESULT,&timestamps[stamp]);
                    if(!gpu_call_succeeded())return consume_invalid_gpu_timing();
                }
                for(std::size_t stamp=1U;stamp<passes.count;++stamp) {
                    if(timestamps[stamp]<timestamps[stamp-1U]) {
                        result.status=OpenGlGpuTimingStatus::disjoint;
                        result.elapsed_ms=std::numeric_limits<double>::quiet_NaN();
                        result.pass_elapsed_ms={};result.valid_pass_mask=0;
                        result.world_subpass_elapsed_ms={};result.valid_world_subpass_mask=0;
                        ++gpu_timing_.disjoint_results;
                        gpu_pending_[i]=false;--gpu_timing_.pending;++gpu_timing_.completed;
                        return result;
                    }
                    const auto pass=static_cast<std::size_t>(passes.passes[stamp-1U]);
                    result.pass_elapsed_ms[pass]+=static_cast<float>(
                        static_cast<double>(timestamps[stamp]-timestamps[stamp-1U])/1'000'000.0);
                    result.valid_pass_mask|=static_cast<std::uint8_t>(1U<<pass);
                    if(const auto subpass=passes.world_subpasses[stamp-1U]) {
                        const auto detail=static_cast<std::size_t>(*subpass);
                        result.world_subpass_elapsed_ms[detail]+=static_cast<float>(
                            static_cast<double>(timestamps[stamp]-timestamps[stamp-1U])/1'000'000.0);
                        result.valid_world_subpass_mask|=static_cast<std::uint8_t>(1U<<detail);
                    }
                }
            }
            gpu_pending_[i]=false;--gpu_timing_.pending;++gpu_timing_.completed;
            return result;
        }
        return {};
    }
    bool gpu_call_succeeded() noexcept {
        if(glGetError()==GL_NO_ERROR)return true;
        // The wrapper checked pre-existing errors before entering this scope.
        // Only optional query-operation errors are consumed here, and disable
        // timing rather than poisoning the ordinary renderer/gameplay path.
        for(std::size_t error=0;error<8U;++error)if(glGetError()==GL_NO_ERROR)break;
        gpu_timing_failed_=true;++gpu_timing_.unavailable;
        gpu_timing_.last_submission_status=OpenGlGpuTimingStatus::invalid;return false;
    }
    const OpenGlGpuTimingStatistics& gpu_timing_statistics() const noexcept {return gpu_timing_;}
    void adjust_image(const core::ImageAdjustments& settings,const RenderExtent extent,
        const std::optional<detail::SceneTarget::ResolvedColorInput>& resolved_color) {
        image_statistics_.last_extent=extent;
        if(settings==core::ImageAdjustments{}) {
            ++image_statistics_.neutral_frames;
            return;
        }
        // This operates on encoded framebuffer RGB exactly once. The existing
        // E30 upload/lightmap pipeline uses RGBA8, not sRGB textures. No
        // desktop/palette gamma or implicit sRGB conversion is introduced.
        HudGlStateGuard state;
        ImageCopyGlStateGuard copy_state;
        startup_boundary("image_adjustment_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        if(!image_program_.name()) {
            constexpr char vertex[]=R"GLSL(#version 330 core
void main() {
    vec2 points[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(points[gl_VertexID],0,1);
}
)GLSL";
            constexpr char fragment[]=R"GLSL(#version 330 core
uniform sampler2D u_scene;
uniform vec3 u_adjustments;
out vec4 output_color;
void main() {
    vec4 source=texelFetch(u_scene,ivec2(gl_FragCoord.xy),0);
    vec3 adjusted=clamp((source.rgb-vec3(0.5))*u_adjustments.z+
        vec3(0.5+u_adjustments.x),vec3(0),vec3(1));
    output_color=vec4(pow(adjusted,vec3(1.0/u_adjustments.y)),source.a);
}
)GLSL";
            auto vs=compile_shader(GL_VERTEX_SHADER,vertex,"Scene color vertex shader");
            auto fs=compile_shader(GL_FRAGMENT_SHADER,fragment,"Scene color fragment shader");
            const auto name=glCreateProgram();
            if(!name)fail(OpenGlRendererErrorCode::program_link_failed,"Unable to create scene color program");
            auto program=GlObject::adopt(GlObjectKind::program,name);
            glAttachShader(name,vs.name());glAttachShader(name,fs.name());glLinkProgram(name);
            GLint linked{};glGetProgramiv(name,GL_LINK_STATUS,&linked);
            if(linked!=GL_TRUE)fail(OpenGlRendererErrorCode::program_link_failed,
                "Scene color program link failed: "+bounded_driver_log(name,false));
            image_adjustments_=glGetUniformLocation(name,"u_adjustments");
            const auto sampler=glGetUniformLocation(name,"u_scene");
            if(image_adjustments_<0 || sampler<0)
                fail(OpenGlRendererErrorCode::program_link_failed,"Scene color uniforms unavailable");
            glUseProgram(name);glUniform1i(sampler,0);
            image_array_=create_vertex_array();
            image_program_=std::move(program);
        }
        glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
        if(resolved_color) {
            // The scene target owns this immutable-in-frame RGBA8 input; the
            // caller output is bound only as a draw destination, never read.
            if(resolved_color->extent!=extent || !resolved_color->texture)
                fail(OpenGlRendererErrorCode::gl_operation_failed,"Resolved scene color input is invalid");
            glBindTexture(GL_TEXTURE_2D,resolved_color->texture);
        } else {
            if(!image_texture_.name()) {
                image_texture_=create_texture(OpenGlRendererErrorCode::texture_upload_failed,
                    "Scene color copy texture");
                image_statistics_.active_texture_count=1;
            }
            glBindTexture(GL_TEXTURE_2D,image_texture_.name());
            if(image_extent_!=extent) {
                GLint maximum{};glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
                if(extent.width>maximum || extent.height>maximum ||
                    static_cast<std::uint64_t>(extent.width)*static_cast<std::uint64_t>(extent.height)>32U*1024U*1024U)
                    fail(OpenGlRendererErrorCode::texture_upload_failed,"Scene color framebuffer exceeds bounded texture size");
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent.width,extent.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                require_no_gl_error(OpenGlRendererErrorCode::texture_upload_failed,"Scene color storage resize");
                image_extent_=extent;++image_statistics_.storage_reallocations;
            }
            GLint draw_framebuffer{},draw_buffer{};
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_framebuffer);
            glGetIntegerv(GL_DRAW_BUFFER0,&draw_buffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(draw_framebuffer));
            glReadBuffer(static_cast<GLenum>(draw_buffer));
            glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,extent.width,extent.height);
        }
        glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_FALSE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glUseProgram(image_program_.name());glBindVertexArray(image_array_.name());
        glUniform3f(image_adjustments_,settings.brightness,settings.gamma,settings.contrast);
        glDrawArrays(GL_TRIANGLES,0,3);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Scene color adjustment pass");
        ++image_statistics_.adjusted_frames;
        if(resolved_color)++image_statistics_.resolved_texture_reads;
        else ++image_statistics_.framebuffer_color_copies;
    }
    void begin_decals() noexcept {
        ++decal_frame_serial_;
        decal_statistics_.last_submitted=decal_statistics_.last_rendered=decal_statistics_.last_rejected=0;
    }
    void reject_decal_batches(std::size_t count) noexcept {
        decal_statistics_.submitted_batch_count+=count;decal_statistics_.last_submitted+=count;
        decal_statistics_.rejected_batch_count+=count;decal_statistics_.last_rejected+=count;
        decal_statistics_.batch_limit+=count;
    }
    void reject_unavailable_world_decals(std::size_t count) noexcept {
        decal_statistics_.submitted_batch_count+=count;decal_statistics_.last_submitted+=count;
        decal_statistics_.rejected_batch_count+=count;decal_statistics_.last_rejected+=count;
        decal_statistics_.world_unavailable+=count;
    }
    void render_decals(const RenderWorldDecals& decals,
        const RenderMatrix4& view_projection, FrameDrawCounters& frame)
    {
        ++decal_statistics_.submitted_batch_count;++decal_statistics_.last_submitted;
        const auto reject=[&](std::uint64_t& reason) {
            ++reason;++decal_statistics_.rejected_batch_count;++decal_statistics_.last_rejected;
        };
        if(!decals.texture || !decals.vertices){reject(decal_statistics_.missing_resource);return;}
        if((decals.texture->alpha_mode!=assets::WorldTextureAlphaMode::masked_index_255 &&
            decals.texture->alpha_mode!=assets::WorldTextureAlphaMode::straight_alpha) ||
            decals.texture->mip_levels[0].width==0 ||
            decals.texture->mip_levels[0].height==0) {reject(decal_statistics_.invalid_texture);return;}
        const auto& pixels=decals.texture->mip_levels[0];
        if (pixels.width>256U || pixels.height>256U || pixels.width!=decals.texture->width ||
            pixels.height!=decals.texture->height ||
            pixels.rgba_pixels.size()!=std::size_t(pixels.width)*pixels.height*4U) {
            reject(decal_statistics_.invalid_texture);return;
        }
        if(decals.material_mode!=game_api::LocalDecalMaterialMode::straight_alpha &&
            decals.material_mode!=game_api::LocalDecalMaterialMode::white_neutral_modulate &&
            decals.material_mode!=game_api::LocalDecalMaterialMode::lit_straight_alpha) {
            reject(decal_statistics_.unsupported_material);return;
        }
        if(!is_finite(decals.model_transform) || !is_finite(view_projection)) {
            reject(decal_statistics_.invalid_transform);return;
        }
        const auto combined=multiply(view_projection,decals.model_transform);
        if(!is_finite(combined)) {reject(decal_statistics_.invalid_transform);return;}
        if(decals.vertices->empty() || decals.vertices->size()>64U*96U || decals.vertices->size()%3U!=0U ||
            std::any_of(decals.vertices->begin(),decals.vertices->end(),[](const auto& v) {
            return !std::isfinite(v.position.x)||!std::isfinite(v.position.y)||
                !std::isfinite(v.position.z)||!std::isfinite(v.uv.x)||
                !std::isfinite(v.uv.y)||v.uv.x< -0.001F||v.uv.x>1.001F||
                v.uv.y< -0.001F||v.uv.y>1.001F ||
                std::any_of(v.surface_light_rgb.begin(),v.surface_light_rgb.end(),
                    [](float c){return !std::isfinite(c)||c<0||c>1;});
        })) {reject(decal_statistics_.invalid_geometry);return;}
        // Upload and draw restore the complete state they touch, including on
        // resource errors. No later world/Studio/sprite/HUD pass inherits it.
        struct Restore {
            GLboolean cull{},blend{},offset{},depth{},depth_write{};
            GLfloat factor{},units{};
            GLint src_rgb{},dst_rgb{},src_alpha{},dst_alpha{},equation_rgb{},equation_alpha{},
                cull_mode{},front_face{},program{},array{},buffer{},active{},texture{};
            GLint sampler{},unpack_buffer{},unpack_alignment{},unpack_row_length{},unpack_skip_rows{},unpack_skip_pixels{};
            Restore() {
                cull=glIsEnabled(GL_CULL_FACE);blend=glIsEnabled(GL_BLEND);
                offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);depth=glIsEnabled(GL_DEPTH_TEST);
                glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);
                glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);
                glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);
                glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_alpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_alpha);
                glGetIntegerv(GL_BLEND_EQUATION_RGB,&equation_rgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&equation_alpha);
                glGetIntegerv(GL_CULL_FACE_MODE,&cull_mode);glGetIntegerv(GL_FRONT_FACE,&front_face);
                glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&array);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
                glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
                glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer);
                glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack_alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpack_row_length);
                glGetIntegerv(GL_UNPACK_SKIP_ROWS,&unpack_skip_rows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&unpack_skip_pixels);
                glBindSampler(0U,0U);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0U);
                glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
                glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
            }
            ~Restore() noexcept {
                glBlendFuncSeparate(src_rgb,dst_rgb,src_alpha,dst_alpha);
                glBlendEquationSeparate(equation_rgb,equation_alpha);
                glCullFace(cull_mode);glFrontFace(front_face);glPolygonOffset(factor,units);glDepthMask(depth_write);
                const auto enable=[](GLenum capability,GLboolean value){if(value)glEnable(capability);else glDisable(capability);};
                enable(GL_CULL_FACE,cull);enable(GL_BLEND,blend);enable(GL_POLYGON_OFFSET_FILL,offset);enable(GL_DEPTH_TEST,depth);
                glUseProgram(program);glBindVertexArray(array);glBindBuffer(GL_ARRAY_BUFFER,buffer);
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(active);
                glBindSampler(0U,sampler);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack_buffer);
                glPixelStorei(GL_UNPACK_ALIGNMENT,unpack_alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,unpack_row_length);
                glPixelStorei(GL_UNPACK_SKIP_ROWS,unpack_skip_rows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,unpack_skip_pixels);
            }
        } restore;
        startup_boundary("decal_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        if (!decal_program_.name()) create_decal_program();
        auto texture_cache=std::find_if(decal_textures_.begin(),decal_textures_.end(),[&](const auto& e) {
            return e && e->asset==decals.texture;
        });
        if(texture_cache==decal_textures_.end()) {
            texture_cache=std::find_if(decal_textures_.begin(),decal_textures_.end(),[](const auto& e){return !e;});
            if(texture_cache==decal_textures_.end()) {
                texture_cache=std::min_element(decal_textures_.begin(),decal_textures_.end(),[](const auto& a,const auto& b) {
                    return a->last_used<b->last_used;
                });
                if((*texture_cache)->last_used==decal_frame_serial_) {reject(decal_statistics_.resource_budget);return;}
            }
            auto candidate=create_texture(OpenGlRendererErrorCode::texture_upload_failed,
                "World decal texture creation");
            glBindTexture(GL_TEXTURE_2D,candidate.name());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,pixels.width,pixels.height,0,
                GL_RGBA,GL_UNSIGNED_BYTE,pixels.rgba_pixels.data());
            require_no_gl_error(OpenGlRendererErrorCode::texture_upload_failed,
                "Masked world decal upload");
            *texture_cache=DecalTextureCache{decals.texture,std::move(candidate),decal_frame_serial_};
            ++decal_statistics_.texture_upload_count;
        }
        (*texture_cache)->last_used=decal_frame_serial_;
        auto geometry_cache=std::find_if(decal_geometries_.begin(),decal_geometries_.end(),[&](const auto& e) {
            return e && e->vertices==decals.vertices && e->revision==decals.revision;
        });
        if(geometry_cache==decal_geometries_.end()) {
            // Same immutable pointer with a newer owner revision replaces its
            // old entry, rather than retaining duplicate geometry indefinitely.
            geometry_cache=std::find_if(decal_geometries_.begin(),decal_geometries_.end(),[&](const auto& e) {
                return e && e->vertices==decals.vertices;
            });
            if(geometry_cache==decal_geometries_.end())
                geometry_cache=std::find_if(decal_geometries_.begin(),decal_geometries_.end(),[](const auto& e){return !e;});
            if(geometry_cache==decal_geometries_.end()) {
                geometry_cache=std::min_element(decal_geometries_.begin(),decal_geometries_.end(),[](const auto& a,const auto& b) {
                    return a->last_used<b->last_used;
                });
                if((*geometry_cache)->last_used==decal_frame_serial_) {reject(decal_statistics_.resource_budget);return;}
            }
            std::vector<float> buffer;
            buffer.reserve(decals.vertices->size()*8U);
            for(const auto& v:*decals.vertices) {
                buffer.insert(buffer.end(),{v.position.x,v.position.y,v.position.z,v.uv.x,v.uv.y,
                    v.surface_light_rgb[0],v.surface_light_rgb[1],v.surface_light_rgb[2]});
            }
            auto array=create_vertex_array(),gpu_buffer=create_buffer();
            glBindVertexArray(array.name());glBindBuffer(GL_ARRAY_BUFFER,gpu_buffer.name());
            glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(buffer.size()*sizeof(float)),
                buffer.data(),GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0U);glVertexAttribPointer(0U,3,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);
            glEnableVertexAttribArray(1U);glVertexAttribPointer(1U,2,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<const void*>(3*sizeof(float)));
            glEnableVertexAttribArray(2U);glVertexAttribPointer(2U,3,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<const void*>(5*sizeof(float)));
            require_no_gl_error(OpenGlRendererErrorCode::buffer_upload_failed,
                "Clipped world decal geometry upload");
            *geometry_cache=DecalGeometryCache{decals.vertices,decals.revision,std::move(array),std::move(gpu_buffer),decal_frame_serial_};
            ++decal_statistics_.geometry_upload_count;
        }
        (*geometry_cache)->last_used=decal_frame_serial_;
        decal_statistics_.cached_texture_count=std::count_if(decal_textures_.begin(),decal_textures_.end(),[](const auto& e){return e.has_value();});
        decal_statistics_.cached_geometry_count=std::count_if(decal_geometries_.begin(),decal_geometries_.end(),[](const auto& e){return e.has_value();});
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0F,-1.0F);
        glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);
        const bool modulate=decals.material_mode==
            game_api::LocalDecalMaterialMode::white_neutral_modulate;
        glEnable(GL_BLEND);
        // Preserve framebuffer alpha for later sprite/HUD passes while the
        // color blend changes only the already-lit world material.
        if(modulate) glBlendFuncSeparate(GL_DST_COLOR,GL_ZERO,GL_ZERO,GL_ONE);
        else glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
        glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        glUseProgram(decal_program_.name());
        glUniformMatrix4fv(decal_matrix_,1,GL_FALSE,combined.values.data());
        glUniform1i(decal_material_mode_,modulate ? 1 :
            (decals.material_mode==game_api::LocalDecalMaterialMode::lit_straight_alpha ? 2 : 0));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,(*texture_cache)->texture.name());
        glUniform1i(decal_sampler_,0);
        glBindVertexArray((*geometry_cache)->array.name());
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(decals.vertices->size()));
        ++decal_statistics_.rendered_batch_count;++decal_statistics_.last_rendered;
        ++frame.draw_calls;
        frame.triangles+=decals.vertices->size()/3U;
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "Clipped world decal draw");
    }
    void create_decal_program()
    {
        constexpr char vertex_source[]=R"GLSL(#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec2 uv;
layout(location=2) in vec3 light_rgb;
uniform mat4 view_projection;
out vec2 texcoord;
out vec3 surface_light;
void main(){gl_Position=view_projection*vec4(p,1.0);texcoord=uv;surface_light=light_rgb;}
)GLSL";
        constexpr char fragment_source[]=R"GLSL(#version 330 core
in vec2 texcoord;
in vec3 surface_light;
uniform sampler2D decal_texture;
uniform int material_mode;
out vec4 output_color;
void main(){
  vec4 c=texture(decal_texture,texcoord);
  if(material_mode==1) output_color=vec4(mix(vec3(1.0),c.rgb,c.a),1.0);
  else if(material_mode==2) output_color=vec4(c.rgb*surface_light,c.a);
  else output_color=c;
}
)GLSL";
        auto vertex=compile_shader(GL_VERTEX_SHADER,vertex_source,"World decal vertex");
        auto fragment=compile_shader(GL_FRAGMENT_SHADER,fragment_source,"World decal fragment");
        const auto name=glCreateProgram();
        if(name==0U) fail(OpenGlRendererErrorCode::program_link_failed,"World decal program creation");
        decal_program_=GlObject::adopt(GlObjectKind::program,name);
        glAttachShader(name,vertex.name());glAttachShader(name,fragment.name());glLinkProgram(name);
        GLint linked=GL_FALSE;glGetProgramiv(name,GL_LINK_STATUS,&linked);
        if(linked!=GL_TRUE) fail(OpenGlRendererErrorCode::program_link_failed,"World decal link");
        decal_matrix_=glGetUniformLocation(name,"view_projection");
        decal_sampler_=glGetUniformLocation(name,"decal_texture");
        decal_material_mode_=glGetUniformLocation(name,"material_mode");
    }
    [[nodiscard]] static bool valid_flash(const RenderMuzzleFlash& flash,
                                          const float head_taper_fraction) noexcept {
        const auto& p=flash.center;
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
            std::isfinite(flash.radius_units)&&flash.radius_units>0.0F&&
            flash.radius_units<=16.0F&&std::isfinite(head_taper_fraction)&&
            head_taper_fraction>=0&&head_taper_fraction<=1&&
            std::none_of(flash.color.begin(),flash.color.end(),[](float c){
                return !std::isfinite(c)||c<0.0F||c>1.0F;
            });
    }
    void render_flash(const RenderMuzzleFlash& flash, const RenderMatrix4& view_projection,
                      assets::AssetVector3 right={0,1,0}, assets::AssetVector3 up={0,0,1},
                      int shape=0, float head_taper_fraction=0)
    {
        if (!valid_flash(flash,head_taper_fraction)) return;
        const auto& p=flash.center;
        if (!flash_program_.name()) create_flash_program();
        const float r=flash.radius_units;
        // Camera-local +X forward: billboard in YZ at the evaluated Studio
        // attachment, not a screen-space crosshair overlay.
        std::array<float,30> vertices{};
        constexpr std::array<std::array<float,2>,6> corners{{
            {-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}};
        for(std::size_t i=0;i<corners.size();++i) {
            const auto x=corners[i][0], y=corners[i][1];
            vertices[i*5]=p.x+r*(right.x*x+up.x*y);
            vertices[i*5+1]=p.y+r*(right.y*x+up.y*y);
            vertices[i*5+2]=p.z+r*(right.z*x+up.z*y);
            vertices[i*5+3]=x;vertices[i*5+4]=y;
        }
        glBindBuffer(GL_ARRAY_BUFFER,flash_buffer_.name());
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_DYNAMIC_DRAW);
        const auto cull=glIsEnabled(GL_CULL_FACE);
        const auto blend=glIsEnabled(GL_BLEND);
        GLboolean depth_write=GL_TRUE;
        GLint blend_src_rgb=GL_ONE, blend_dst_rgb=GL_ZERO;
        GLint blend_src_alpha=GL_ONE, blend_dst_alpha=GL_ZERO;
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);
        glGetIntegerv(GL_BLEND_SRC_RGB,&blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB,&blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA,&blend_dst_alpha);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,shape==1 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
        glUseProgram(flash_program_.name());
        glUniformMatrix4fv(flash_matrix_,1,GL_FALSE,view_projection.values.data());
        glUniform4fv(flash_color_,1,flash.color.data());
        glUniform1i(flash_shape_,shape);
        glUniform1f(flash_head_taper_,head_taper_fraction);
        glBindVertexArray(flash_array_.name());
        glDrawArrays(GL_TRIANGLES,0,6);
        glBindVertexArray(0);
        glUseProgram(0);
        glBlendFuncSeparate(blend_src_rgb,blend_dst_rgb,blend_src_alpha,blend_dst_alpha);
        if(!blend) glDisable(GL_BLEND);
        glDepthMask(depth_write);
        if(cull) glEnable(GL_CULL_FACE);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Transient flash draw");
    }

    void render_solid_line(const RenderWorldLine& line,const RenderMatrix4& matrix,
                           assets::AssetVector3 eye,const RenderLineMesh& mesh) {
        if(!mesh.vertex_count || !is_finite(eye)) return;
        if(!solid_line_program_.name()) create_solid_line_program();
        glBindBuffer(GL_ARRAY_BUFFER,solid_line_buffer_.name());
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(mesh.vertex_count*sizeof(RenderLineVertex)),
            mesh.vertices.data(),GL_DYNAMIC_DRAW);
        const auto cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);
        GLboolean depth_write=GL_TRUE;
        GLint cull_mode=GL_BACK,front_face=GL_CCW;
        GLint src_rgb=GL_ONE,dst_rgb=GL_ZERO,src_alpha=GL_ONE,dst_alpha=GL_ZERO;
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);
        glGetIntegerv(GL_CULL_FACE_MODE,&cull_mode);glGetIntegerv(GL_FRONT_FACE,&front_face);
        glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_alpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_alpha);
        // Draw only outward-facing faces: additive back faces must not double
        // the old gain. Scene depth still occludes the volume, without writing it.
        glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);
        glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);
        glUseProgram(solid_line_program_.name());
        glUniformMatrix4fv(solid_line_matrix_,1,GL_FALSE,matrix.values.data());
        glUniform4fv(solid_line_color_,1,line.color.data());
        glUniform1f(solid_line_gain_,line.emission_gain);
        glUniform3f(solid_line_eye_,eye.x,eye.y,eye.z);
        glBindVertexArray(solid_line_array_.name());
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(mesh.vertex_count));
        glBindVertexArray(0);glUseProgram(0);
        glBlendFuncSeparate(src_rgb,dst_rgb,src_alpha,dst_alpha);
        if(!blend) glDisable(GL_BLEND);
        glDepthMask(depth_write);glCullFace(static_cast<GLenum>(cull_mode));
        glFrontFace(static_cast<GLenum>(front_face));if(!cull) glDisable(GL_CULL_FACE);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Solid transient line draw");
    }
    void create_solid_line_program() {
        constexpr char vertex_source[]=R"GLSL(#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec3 n;
uniform mat4 view_projection;
out vec3 world_position;
out vec3 world_normal;
void main(){gl_Position=view_projection*vec4(p,1.0);world_position=p;world_normal=n;}
)GLSL";
        constexpr char fragment_source[]=R"GLSL(#version 330 core
in vec3 world_position;
in vec3 world_normal;
uniform vec3 eye;
uniform vec4 tint;
uniform float emission_gain;
out vec4 output_color;
void main(){
  vec3 view_direction=eye-world_position;
  view_direction/=max(length(view_direction),0.0001);
  // Emissive core, with a mild round-section falloff. No game/material light
  // or additional halo: the existing head flash/light stay separate outputs.
  float facing=clamp(dot(normalize(world_normal),view_direction),0.0,1.0);
  float round_gain=0.65+0.35*sqrt(facing);
  output_color=vec4(tint.rgb*round_gain*emission_gain,tint.a);
}
)GLSL";
        auto vertex=compile_shader(GL_VERTEX_SHADER,vertex_source,"Solid line vertex");
        auto fragment=compile_shader(GL_FRAGMENT_SHADER,fragment_source,"Solid line fragment");
        const GLuint name=glCreateProgram();
        if(!name) fail(OpenGlRendererErrorCode::program_link_failed,"Solid line program creation failed");
        solid_line_program_=GlObject::adopt(GlObjectKind::program,name);
        glAttachShader(name,vertex.name());glAttachShader(name,fragment.name());glLinkProgram(name);
        GLint linked=GL_FALSE;glGetProgramiv(name,GL_LINK_STATUS,&linked);
        if(linked!=GL_TRUE) fail(OpenGlRendererErrorCode::program_link_failed,"Solid line program link failed");
        solid_line_matrix_=glGetUniformLocation(name,"view_projection");
        solid_line_color_=glGetUniformLocation(name,"tint");solid_line_eye_=glGetUniformLocation(name,"eye");
        solid_line_gain_=glGetUniformLocation(name,"emission_gain");
        solid_line_array_=create_vertex_array();solid_line_buffer_=create_buffer();
        glBindVertexArray(solid_line_array_.name());glBindBuffer(GL_ARRAY_BUFFER,solid_line_buffer_.name());
        static_assert(sizeof(RenderLineVertex)==6*sizeof(float));
        glEnableVertexAttribArray(0U);
        glVertexAttribPointer(0U,3,GL_FLOAT,GL_FALSE,sizeof(RenderLineVertex),nullptr);
        glEnableVertexAttribArray(1U);
        glVertexAttribPointer(1U,3,GL_FLOAT,GL_FALSE,sizeof(RenderLineVertex),
            reinterpret_cast<const void*>(offsetof(RenderLineVertex,normal)));
        glBindVertexArray(0);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Solid line GPU initialization");
    }

    struct FlashBatchVertex {
        float x,y,z,u,v,r,g,b,a,shape,taper;
    };
    static_assert(sizeof(FlashBatchVertex)==11*sizeof(float));
    static constexpr std::size_t kMaximumFlashBatchVertices=(32U+256U+64U)*6U;

    [[nodiscard]] static std::size_t valid_world_flash_quads(const RenderScene& scene) {
        std::size_t quads{};
        for(std::size_t i=0;i<std::min(scene.world_flash_count,scene.world_flashes.size());++i)
            quads+=valid_flash(scene.world_flashes[i],0.0F);
        for(std::size_t i=0;i<std::min(scene.world_particle_count,scene.world_particles.size());++i) {
            const auto& particle=scene.world_particles[i];
            quads+=valid_flash({particle.center,particle.radius_units,particle.color},0.0F);
        }
        for(std::size_t i=0;i<std::min(scene.world_line_count,scene.world_lines.size());++i) {
            const auto& line=scene.world_lines[i];
            if(line.geometry!=game_api::WorldLineGeometry::flat_billboard ||
               !std::isfinite(line.emission_gain) || line.emission_gain<0 || line.emission_gain>4)
                continue;
            const auto axis=assets::AssetVector3{(line.end.x-line.start.x)*0.5F,
                (line.end.y-line.start.y)*0.5F,(line.end.z-line.start.z)*0.5F};
            const auto n=std::hypot(axis.x,axis.y,axis.z);
            if(!std::isfinite(n)||n<0.001F||n>5000 || !std::isfinite(line.width_units)||
               line.width_units<=0 || line.width_units>4 || !std::isfinite(line.head_taper_fraction)||
               line.head_taper_fraction<0 || line.head_taper_fraction>1)continue;
            const RenderMuzzleFlash flash{{(line.start.x+line.end.x)*0.5F,
                (line.start.y+line.end.y)*0.5F,(line.start.z+line.end.z)*0.5F},
                1.0F,line.color};
            quads+=valid_flash(flash,line.head_taper_fraction);
        }
        return quads;
    }

    void append_flash_batch(const RenderMuzzleFlash& flash,assets::AssetVector3 right,
                            assets::AssetVector3 up,int shape,float taper) {
        if(!valid_flash(flash,taper))return;
        constexpr std::array<std::array<float,2>,6> corners{{
            {-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}};
        const auto& p=flash.center;
        const float radius=flash.radius_units;
        for(const auto& corner:corners) {
            const float x=corner[0],y=corner[1];
            flash_batch_vertices_.push_back({
                p.x+radius*(right.x*x+up.x*y),
                p.y+radius*(right.y*x+up.y*y),
                p.z+radius*(right.z*x+up.z*y),
                x,y,flash.color[0],flash.color[1],flash.color[2],flash.color[3],
                static_cast<float>(shape),taper});
        }
    }

    void draw_flash_batch(const RenderMatrix4& matrix,bool alpha_blend) {
        if(flash_batch_vertices_.empty())return;
        if(!flash_batch_program_.name())create_flash_batch_program();
        glBindBuffer(GL_ARRAY_BUFFER,flash_batch_buffer_.name());
        if(!flash_batch_gpu_orphaned_this_frame_) {
            // The one bounded storage orphan for this nonempty frame avoids a
            // driver wait when a prior frame still reads the same buffer.
            const auto storage_bytes=flash_batch_frame_vertex_capacity_*sizeof(FlashBatchVertex);
            glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(storage_bytes),nullptr,GL_DYNAMIC_DRAW);
            require_no_gl_error(OpenGlRendererErrorCode::buffer_upload_failed,
                                "World transient batch allocation");
            flash_batch_gpu_orphaned_this_frame_=true;
            ++statistics_.transient_flash_storage_orphan_count;
            statistics_.transient_flash_storage_bytes_high_water=std::max(
                statistics_.transient_flash_storage_bytes_high_water,storage_bytes);
        }
        const auto bytes=static_cast<GLsizeiptr>(flash_batch_vertices_.size()*sizeof(FlashBatchVertex));
        const auto first=flash_batch_gpu_cursor_;
        if(first+flash_batch_vertices_.size()>flash_batch_frame_vertex_capacity_)
            fail(OpenGlRendererErrorCode::buffer_upload_failed,
                 "World transient batch exceeds its finite scene capacity");
        glBufferSubData(GL_ARRAY_BUFFER,static_cast<GLintptr>(first*sizeof(FlashBatchVertex)),
                        bytes,flash_batch_vertices_.data());
        require_no_gl_error(OpenGlRendererErrorCode::buffer_upload_failed,
                            "World transient batch upload");
        glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,alpha_blend?GL_ONE_MINUS_SRC_ALPHA:GL_ONE);
        glUseProgram(flash_batch_program_.name());
        glUniformMatrix4fv(flash_batch_matrix_,1,GL_FALSE,matrix.values.data());
        glBindVertexArray(flash_batch_array_.name());
        glDrawArrays(GL_TRIANGLES,static_cast<GLint>(first),
                     static_cast<GLsizei>(flash_batch_vertices_.size()));
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
                            "World transient batch draw");
        ++statistics_.transient_flash_upload_count;
        ++statistics_.transient_flash_draw_count;
        statistics_.transient_flash_quad_count+=flash_batch_vertices_.size()/6U;
        flash_batch_gpu_cursor_+=flash_batch_vertices_.size();
        flash_batch_vertices_.clear();
    }

    void create_flash_batch_program() {
        constexpr char vertex_source[]=R"GLSL(#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in float shape;
layout(location=4) in float taper;
uniform mat4 view_projection;
out vec2 offset;
flat out vec4 tint;
flat out float soft_particle;
flat out float head_taper_fraction;
void main(){gl_Position=view_projection*vec4(p,1.0);offset=uv;
  tint=color;soft_particle=shape;head_taper_fraction=taper;}
)GLSL";
        constexpr char fragment_source[]=R"GLSL(#version 330 core
in vec2 offset;
flat in vec4 tint;
flat in float soft_particle;
flat in float head_taper_fraction;
out vec4 output_color;
void main(){
  if(soft_particle==2.0 && head_taper_fraction>0.0){
    float half_width=clamp((1.0-offset.x)/(2.0*head_taper_fraction),0.0,1.0);
    if(abs(offset.y)>half_width) discard;
  }
  float core=max(0.0,1.0-length(offset));
  float rays=max(max(0.0,1.0-abs(offset.x)*5.0),max(0.0,1.0-abs(offset.y)*5.0));
  float alpha=(soft_particle==2.0 ? 1.0 : (soft_particle==1.0 || soft_particle==3.0) ? core*core : clamp(core*core+rays*0.35,0.0,1.0))*tint.a;
  output_color=vec4(tint.rgb,alpha);
}
)GLSL";
        auto vertex=compile_shader(GL_VERTEX_SHADER,vertex_source,"World transient batch vertex");
        auto fragment=compile_shader(GL_FRAGMENT_SHADER,fragment_source,"World transient batch fragment");
        const GLuint name=glCreateProgram();
        if(!name)fail(OpenGlRendererErrorCode::program_link_failed,
                      "World transient batch program creation failed");
        flash_batch_program_=GlObject::adopt(GlObjectKind::program,name);
        glAttachShader(name,vertex.name());glAttachShader(name,fragment.name());glLinkProgram(name);
        GLint linked=GL_FALSE;glGetProgramiv(name,GL_LINK_STATUS,&linked);
        if(linked!=GL_TRUE)fail(OpenGlRendererErrorCode::program_link_failed,
                              "World transient batch program link failed");
        flash_batch_matrix_=glGetUniformLocation(name,"view_projection");
        flash_batch_array_=create_vertex_array();flash_batch_buffer_=create_buffer();
        glBindVertexArray(flash_batch_array_.name());
        glBindBuffer(GL_ARRAY_BUFFER,flash_batch_buffer_.name());
        constexpr auto stride=sizeof(FlashBatchVertex);
        glEnableVertexAttribArray(0U);
        glVertexAttribPointer(0U,3,GL_FLOAT,GL_FALSE,stride,nullptr);
        glEnableVertexAttribArray(1U);
        glVertexAttribPointer(1U,2,GL_FLOAT,GL_FALSE,stride,
            reinterpret_cast<const void*>(offsetof(FlashBatchVertex,u)));
        glEnableVertexAttribArray(2U);
        glVertexAttribPointer(2U,4,GL_FLOAT,GL_FALSE,stride,
            reinterpret_cast<const void*>(offsetof(FlashBatchVertex,r)));
        glEnableVertexAttribArray(3U);
        glVertexAttribPointer(3U,1,GL_FLOAT,GL_FALSE,stride,
            reinterpret_cast<const void*>(offsetof(FlashBatchVertex,shape)));
        glEnableVertexAttribArray(4U);
        glVertexAttribPointer(4U,1,GL_FLOAT,GL_FALSE,stride,
            reinterpret_cast<const void*>(offsetof(FlashBatchVertex,taper)));
        glBindVertexArray(0U);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
                            "World transient batch GPU initialization");
    }

    void render_world_flashes(const RenderScene& scene, const RenderMatrix4& matrix,
                             const RenderCamera& camera) {
        if(!scene.world_flash_count&&!scene.world_particle_count&&!scene.world_line_count)return;
        auto f=assets::AssetVector3{camera.target.x-camera.position.x,
            camera.target.y-camera.position.y,camera.target.z-camera.position.z};
        const auto length=std::sqrt(f.x*f.x+f.y*f.y+f.z*f.z);
        if(!std::isfinite(length)||length<0.0001F) return;
        f={f.x/length,f.y/length,f.z/length};
        const auto a=camera.up;
        auto r=assets::AssetVector3{f.y*a.z-f.z*a.y,f.z*a.x-f.x*a.z,f.x*a.y-f.y*a.x};
        const auto rl=std::sqrt(r.x*r.x+r.y*r.y+r.z*r.z);
        if(!std::isfinite(rl)||rl<0.0001F) return;
        r={r.x/rl,r.y/rl,r.z/rl};
        const auto u=assets::AssetVector3{r.y*f.z-r.z*f.y,r.z*f.x-r.x*f.z,r.x*f.y-r.y*f.x};
        flash_batch_vertices_.clear();
        flash_batch_gpu_cursor_=0;
        flash_batch_gpu_orphaned_this_frame_=false;
        const auto valid_quads=valid_world_flash_quads(scene);
        flash_batch_frame_vertex_capacity_=valid_quads*6U;
        if(flash_batch_frame_vertex_capacity_>kMaximumFlashBatchVertices)
            fail(OpenGlRendererErrorCode::draw_range_invalid,
                 "World transient scene capacity exceeded");
        const auto line_count=std::min(scene.world_line_count,scene.world_lines.size());
        const bool contains_solid_line=std::any_of(scene.world_lines.begin(),
            scene.world_lines.begin()+line_count,[](const RenderWorldLine& line){
                return line.geometry==game_api::WorldLineGeometry::solid_3d;
            });
        // A one-quad scene is common for a muzzle flash or one particle. The
        // legacy draw is cheaper than preparing/orphaning a batch with no
        // adjacent work to combine. Keep solid-line ordering on the batch path.
        const bool single_quad_fallback=valid_quads==1U && !contains_solid_line;
        std::optional<TransientBatchGlStateGuard> batch_state;
        bool alpha_blend=false;
        const auto flush=[&] {
            if(flash_batch_vertices_.empty())return;
            if(!batch_state)batch_state.emplace();
            draw_flash_batch(matrix,alpha_blend);
        };
        const auto queue=[&](const RenderMuzzleFlash& flash,assets::AssetVector3 right,
                             assets::AssetVector3 up,int shape,float taper=0.0F) {
            if(!valid_flash(flash,taper))return;
            if(single_quad_fallback) {
                render_flash(flash,matrix,right,up,shape,taper);
                return;
            }
            if(flash_batch_vertices_.capacity()<flash_batch_frame_vertex_capacity_)
                flash_batch_vertices_.reserve(flash_batch_frame_vertex_capacity_);
            const bool next_alpha=shape==1;
            if(!flash_batch_vertices_.empty() && (next_alpha!=alpha_blend ||
               flash_batch_vertices_.size()+6U>kMaximumFlashBatchVertices))flush();
            alpha_blend=next_alpha;
            append_flash_batch(flash,right,up,shape,taper);
        };
        for(std::size_t i=0;i<std::min(scene.world_flash_count,scene.world_flashes.size());++i)
            queue(scene.world_flashes[i],r,u,0);
        for(std::size_t i=0;i<std::min(scene.world_particle_count,scene.world_particles.size());++i) {
            const auto& particle=scene.world_particles[i];
            queue({particle.center,particle.radius_units,particle.color},r,u,particle.emissive ? 3 : 1);
        }
        for(std::size_t i=0;i<std::min(scene.world_line_count,scene.world_lines.size());++i) {
            const auto& line=scene.world_lines[i];
            if(!std::isfinite(line.emission_gain) || line.emission_gain<0 || line.emission_gain>4) continue;
            if(line.geometry==game_api::WorldLineGeometry::solid_3d) {
                const auto mesh=solid_line_mesh(line);
                if(mesh.vertex_count && is_finite(camera.position)) {
                    flush();render_solid_line(line,matrix,camera.position,mesh);
                }
                continue;
            }
            if(line.geometry!=game_api::WorldLineGeometry::flat_billboard) continue;
            auto axis=assets::AssetVector3{(line.end.x-line.start.x)*0.5F,
                (line.end.y-line.start.y)*0.5F,(line.end.z-line.start.z)*0.5F};
            const auto n=std::hypot(axis.x,axis.y,axis.z);
            if(!std::isfinite(n)||n<0.001F||n>5000 || !std::isfinite(line.width_units)||
                line.width_units<=0 || line.width_units>4 || !std::isfinite(line.head_taper_fraction) ||
                line.head_taper_fraction<0 || line.head_taper_fraction>1) continue;
            auto side=assets::AssetVector3{axis.y*f.z-axis.z*f.y,axis.z*f.x-axis.x*f.z,axis.x*f.y-axis.y*f.x};
            const auto sl=std::hypot(side.x,side.y,side.z);
            if(sl<0.0001F) side=r;
            else side={side.x/sl,side.y/sl,side.z/sl};
            side={side.x*line.width_units*0.5F,side.y*line.width_units*0.5F,side.z*line.width_units*0.5F};
            queue({{(line.start.x+line.end.x)*0.5F,(line.start.y+line.end.y)*0.5F,
                (line.start.z+line.end.z)*0.5F},1.0F,line.color},axis,side,2,line.head_taper_fraction);
        }
        flush();
    }

    void create_flash_program()
    {
        constexpr char vertex_source[]=R"GLSL(#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in vec2 uv;
uniform mat4 view_projection;
out vec2 offset;
void main(){gl_Position=view_projection*vec4(p,1.0);offset=uv;}
)GLSL";
        constexpr char fragment_source[]=R"GLSL(#version 330 core
in vec2 offset;
uniform vec4 tint;
uniform int soft_particle;
uniform float head_taper_fraction;
out vec4 output_color;
void main(){
  // Line offset.x runs from -1 at its tail to +1 at its leading endpoint.
  // Keep the body wide; only its final selected fraction narrows to a point.
  if(soft_particle==2 && head_taper_fraction>0.0){
    float half_width=clamp((1.0-offset.x)/(2.0*head_taper_fraction),0.0,1.0);
    if(abs(offset.y)>half_width) discard;
  }
  float core=max(0.0,1.0-length(offset));
  float rays=max(max(0.0,1.0-abs(offset.x)*5.0),max(0.0,1.0-abs(offset.y)*5.0));
  float alpha=(soft_particle==2 ? 1.0 : (soft_particle==1 || soft_particle==3) ? core*core : clamp(core*core+rays*0.35,0.0,1.0))*tint.a;
  output_color=vec4(tint.rgb,alpha);
}
)GLSL";
        auto vertex=compile_shader(GL_VERTEX_SHADER,vertex_source,"Flash vertex");
        auto fragment=compile_shader(GL_FRAGMENT_SHADER,fragment_source,"Flash fragment");
        const GLuint name=glCreateProgram();
        if(name==0U) fail(OpenGlRendererErrorCode::program_link_failed,"Flash program creation failed");
        flash_program_=GlObject::adopt(GlObjectKind::program,name);
        glAttachShader(name,vertex.name());glAttachShader(name,fragment.name());glLinkProgram(name);
        GLint linked=GL_FALSE;glGetProgramiv(name,GL_LINK_STATUS,&linked);
        if(linked!=GL_TRUE) fail(OpenGlRendererErrorCode::program_link_failed,"Flash program link failed");
        flash_matrix_=glGetUniformLocation(name,"view_projection");
        flash_color_=glGetUniformLocation(name,"tint");
        flash_shape_=glGetUniformLocation(name,"soft_particle");
        flash_head_taper_=glGetUniformLocation(name,"head_taper_fraction");
        flash_array_=create_vertex_array();flash_buffer_=create_buffer();
        glBindVertexArray(flash_array_.name());glBindBuffer(GL_ARRAY_BUFFER,flash_buffer_.name());
        glEnableVertexAttribArray(0U);
        glVertexAttribPointer(0U,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);
        glEnableVertexAttribArray(1U);
        glVertexAttribPointer(1U,2,GL_FLOAT,GL_FALSE,5*sizeof(float),
            reinterpret_cast<const void*>(3*sizeof(float)));
        glBindVertexArray(0U);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Flash GPU initialization");
    }

    void render_screen_overlay(const game_api::ScreenEffectsFrame& effects) {
        const auto& color=effects.overlay_rgba;
        if(color[3]<=0.0F)return;
        const HudGlStateGuard state;
        const ImageCopyGlStateGuard extra_state;
        startup_boundary("screen_overlay_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        if(!hud_program_.name())create_hud_program();
        if(!screen_overlay_array_.name()) {
            screen_overlay_array_=create_vertex_array();screen_overlay_buffer_=create_buffer();
            glBindVertexArray(screen_overlay_array_.name());
            glBindBuffer(GL_ARRAY_BUFFER,screen_overlay_buffer_.name());
            glEnableVertexAttribArray(0U);glEnableVertexAttribArray(1U);
            glVertexAttribPointer(0U,2,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
            glVertexAttribPointer(1U,4,GL_FLOAT,GL_FALSE,6*sizeof(float),
                reinterpret_cast<const void*>(2*sizeof(float)));
        }
        auto tint=color;
        if(effects.modulate) {
            for(std::size_t i=0;i<3U;++i)tint[i]=1.0F+color[3]*(color[i]-1.0F);
            tint[3]=1.0F;
        }
        // Same neutral color shader as HUD, but independent bounded geometry:
        // changing the fade cannot invalidate or overwrite cached HUD vertices.
        const std::array<float,18U> vertices{
            -1,-1,tint[0],tint[1],tint[2],tint[3],
             3,-1,tint[0],tint[1],tint[2],tint[3],
            -1, 3,tint[0],tint[1],tint[2],tint[3]};
        glBindVertexArray(screen_overlay_array_.name());
        glBindBuffer(GL_ARRAY_BUFFER,screen_overlay_buffer_.name());
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_DYNAMIC_DRAW);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        glBlendFuncSeparate(effects.modulate ? GL_DST_COLOR : GL_SRC_ALPHA,
            effects.modulate ? GL_ZERO : GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
        glUseProgram(hud_program_.name());glDrawArrays(GL_TRIANGLES,0,3);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Screen overlay draw");
    }

    void render_screen_ui(const ScreenUiFrame& ui,const RenderExtent extent) {
        if(!valid_screen_ui(ui))throw std::invalid_argument{"Screen UI command budget or geometry is invalid"};
        if(extent.width<=0||extent.height<=0||ui.commands.empty())return;
        const HudGlStateGuard state;
        const ImageCopyGlStateGuard extra_state;
        startup_boundary("console_UI_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        if(!hud_program_.name())create_hud_program();
        if(!screen_ui_array_.name()) {
            screen_ui_array_=create_vertex_array();screen_ui_buffer_=create_buffer();
            glBindVertexArray(screen_ui_array_.name());glBindBuffer(GL_ARRAY_BUFFER,screen_ui_buffer_.name());
            glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
            glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,6*sizeof(float),
                reinterpret_cast<const void*>(2*sizeof(float)));
        }
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        std::vector<float> vertices;
        const auto rectangle=[&](float x,float y,float w,float h,const std::array<float,4>& color) {
            const float x0=2*x/extent.width-1,x1=2*(x+w)/extent.width-1;
            const float y0=1-2*y/extent.height,y1=1-2*(y+h)/extent.height;
            for(const auto p:std::array<std::array<float,2>,6>{{{x0,y0},{x0,y1},{x1,y1},{x0,y0},{x1,y1},{x1,y0}}})
                vertices.insert(vertices.end(),{p[0],p[1],color[0],color[1],color[2],color[3]});
        };
        for(const auto& command:ui.commands)std::visit([&](const auto& item) {
            int left=0,top=0,right=extent.width,bottom=extent.height;
            if(item.clip) {
                left=std::clamp(static_cast<int>(std::floor(item.clip->x)),0,extent.width);
                top=std::clamp(static_cast<int>(std::floor(item.clip->y)),0,extent.height);
                right=std::clamp(static_cast<int>(std::ceil(item.clip->x+item.clip->width)),0,extent.width);
                bottom=std::clamp(static_cast<int>(std::ceil(item.clip->y+item.clip->height)),0,extent.height);
            }
            if(right<=left||bottom<=top)return;
            glEnable(GL_SCISSOR_TEST);glScissor(left,extent.height-bottom,right-left,bottom-top);
            using Type=std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<Type,ScreenUiImage>) {
                auto found=std::find_if(screen_ui_textures_.begin(),screen_ui_textures_.end(),
                    [&](const auto& cached){return cached.pixels==item.pixels;});
                GLuint texture{};
                if(found!=screen_ui_textures_.end())texture=found->texture.name();
                else {
                    // Owning immutable pixels prevent an address-reuse cache hit.
                    std::size_t retained{};for(const auto& cached:screen_ui_textures_)retained+=cached.pixels->pixels.size();
                    while(!screen_ui_textures_.empty()&&(screen_ui_textures_.size()>=8||
                        retained+item.pixels->pixels.size()>32U*1024U*1024U)) {
                        retained-=screen_ui_textures_.front().pixels->pixels.size();
                        screen_ui_textures_.erase(screen_ui_textures_.begin());
                    }
                    auto uploaded=create_texture(OpenGlRendererErrorCode::texture_upload_failed,"Screen UI image");
                    glBindTexture(GL_TEXTURE_2D,uploaded.name());
                    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,static_cast<GLsizei>(item.pixels->width),
                        static_cast<GLsizei>(item.pixels->height),0,GL_RGBA,GL_UNSIGNED_BYTE,item.pixels->pixels.data());
                    texture=uploaded.name();screen_ui_textures_.push_back({item.pixels,std::move(uploaded)});
                }
                if(!hud_sprite_program_.name())create_hud_sprite_program();
                const float x0=2*item.x/extent.width-1,x1=2*(item.x+item.width)/extent.width-1;
                const float y0=1-2*item.y/extent.height,y1=1-2*(item.y+item.height)/extent.height;
                const std::array<float,24> quad{x0,y0,0,0,x0,y1,0,1,x1,y1,1,1,
                    x0,y0,0,0,x1,y1,1,1,x1,y0,1,0};
                glBindTexture(GL_TEXTURE_2D,texture);glUseProgram(hud_sprite_program_.name());
                glUniform4fv(hud_sprite_color_,1,item.color.data());glUniform1i(hud_sprite_masked_,0);
                glBindVertexArray(hud_sprite_array_.name());glBindBuffer(GL_ARRAY_BUFFER,hud_sprite_buffer_.name());
                glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
            } else {
                vertices.clear();
                if constexpr(std::is_same_v<Type,ScreenUiRectangle>)rectangle(item.x,item.y,item.width,item.height,item.color);
                else {
                    float x=item.x,y=item.y;std::size_t offset{};
                    while(offset<item.text.size()) {
                        const auto codepoint=next_screen_ui_codepoint(item.text,offset);
                        if(codepoint==U'\n'){x=item.x;y+=item.line_height;continue;}
                        if(codepoint==U'\r')continue;
                        if(x+5*item.pixel_size>left&&x<right&&y+9*item.pixel_size>top&&y<bottom) {
                            const auto rows=screen_ui_glyph(codepoint);
                            for(std::size_t row=0;row<rows.size();++row)for(unsigned col=0;col<5;++col)
                                if((rows[row]&(1U<<(4-col)))!=0U)
                                    rectangle(x+col*item.pixel_size,y+static_cast<float>(row)*item.pixel_size,
                                        item.pixel_size,item.pixel_size,item.color);
                        }
                        x+=item.advance;
                    }
                }
                if(vertices.empty())return;
                glUseProgram(hud_program_.name());glBindVertexArray(screen_ui_array_.name());
                glBindBuffer(GL_ARRAY_BUFFER,screen_ui_buffer_.name());
                glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(float)),vertices.data(),GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()/6));
            }
        },command);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Screen UI rendering");
    }

    void render_hud(const RenderBasicHud& hud, const RenderExtent extent,
                    const std::span<const RenderHudSpriteBinding> bindings)
    {
        hud_statistics_.last_sprite_command_count = hud.sprites.size();
        hud_statistics_.last_rendered_sprite_count = 0U;
        hud_statistics_.last_invalid_commands = 0U;
        hud_statistics_.last_missing_binding = 0U;
        hud_statistics_.last_revision_mismatch = 0U;
        hud_statistics_.last_invalid_frame = 0U;
        hud_statistics_.last_invalid_crop = 0U;
        hud_statistics_.last_unsupported_material = 0U;
        hud_statistics_.last_resource_budget = 0U;
        hud_statistics_.last_empty_clip = 0U;
        if (!game_api::valid_hud_draw_commands(hud) || extent.width <= 0 || extent.height <= 0 ||
            bindings.size() > kMaximumHudSpriteBindings) {
            ++hud_statistics_.last_invalid_commands;
            return;
        }
        const HudGlStateGuard state_guard;
        startup_boundary("HUD_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
        ++hud_cache_clock_;
        if (!hud_commands_ || *hud_commands_ != hud || extent != hud_extent_) {
            hud_commands_ = hud;
            hud_extent_ = extent;
            build_hud_vertices(hud, extent);
            if (!hud_program_.name()) create_hud_program();
            glBindBuffer(GL_ARRAY_BUFFER, hud_buffer_.name());
            glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(hud_vertices_.size() * sizeof(float)),
                hud_vertices_.data(), GL_DYNAMIC_DRAW);
            require_no_gl_error(OpenGlRendererErrorCode::buffer_upload_failed,
                "HUD geometry upload");
        }
        if (hud_batches_.empty()) return;
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glBlendEquation(GL_FUNC_ADD);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0U);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0U,0U);
        for(const auto& batch : hud_batches_) {
            if(batch.sprite) {
                render_hud_sprite(hud.sprites[batch.index],extent,bindings);
            } else if(batch.count) {
                glDisable(GL_SCISSOR_TEST);
                glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
                glUseProgram(hud_program_.name()); glBindVertexArray(hud_array_.name());
                glDrawArrays(GL_TRIANGLES,static_cast<GLint>(batch.first),static_cast<GLsizei>(batch.count));
            }
        }
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "HUD draw");
    }

private:
    struct HudBatch {
        bool sprite{};
        std::size_t index{},first{},count{};
        std::uint32_t order{};
    };
    struct HudGpuTexture {
        entity_render::EntityRenderResourceIdentity source_identity;
        std::uint64_t render_revision{};
        std::uint32_t frame{};
        GlObject texture;
        std::size_t bytes{};
        std::uint64_t used_at{};
    };

    [[nodiscard]] GLuint hud_texture(const RenderHudSpriteBinding& binding,
                                    const std::uint32_t selected_frame) {
        const auto& frame = binding.asset->frames()[selected_frame];
        for(auto& cached : hud_textures_) {
            if(cached.source_identity == binding.asset->source_identity() &&
               cached.render_revision == binding.asset->resource_revision() &&
               cached.frame == selected_frame) {
                cached.used_at = hud_cache_clock_;
                return cached.texture.name();
            }
        }
        std::size_t pixels{},bytes{};
        GLint maximum_size{}; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum_size);
        if(!frame.geometry.width || !frame.geometry.height || maximum_size <= 0 ||
           frame.geometry.width > static_cast<std::uint32_t>(maximum_size) ||
           frame.geometry.height > static_cast<std::uint32_t>(maximum_size) ||
           !checked_multiply(frame.geometry.width,frame.geometry.height,pixels) ||
           !checked_multiply(pixels,4U,bytes) || frame.rgba8.size()!=bytes ||
           bytes > kMaximumHudSpriteTextureBytes) {
            ++hud_statistics_.last_resource_budget;
            return 0U;
        }
        // Retain immutable textures across layout/weapon changes, within a hard
        // count/byte budget. Pool reuse is not a resource identity.
        while(!hud_textures_.empty() && (hud_textures_.size() >= kMaximumHudSpriteBindings ||
              bytes > kMaximumHudSpriteTextureBytes-hud_statistics_.cached_texture_bytes)) {
            const auto oldest=std::min_element(hud_textures_.begin(),hud_textures_.end(),
                [](const auto& a,const auto& b){return a.used_at < b.used_at;});
            hud_statistics_.cached_texture_bytes-=oldest->bytes;
            hud_textures_.erase(oldest);
        }
        auto texture=create_texture(OpenGlRendererErrorCode::texture_upload_failed,"HUD sprite texture creation");
        glBindTexture(GL_TEXTURE_2D,texture.name());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,static_cast<GLsizei>(frame.geometry.width),
            static_cast<GLsizei>(frame.geometry.height),0,GL_RGBA,GL_UNSIGNED_BYTE,frame.rgba8.data());
        require_no_gl_error(OpenGlRendererErrorCode::texture_upload_failed,"HUD sprite upload");
        const auto name=texture.name();
        hud_textures_.push_back({binding.asset->source_identity(),binding.asset->resource_revision(),
            selected_frame,std::move(texture),bytes,hud_cache_clock_});
        ++hud_statistics_.texture_upload_count;
        hud_statistics_.cached_texture_bytes+=bytes;
        hud_statistics_.cached_texture_count=hud_textures_.size();
        return name;
    }

    void render_hud_sprite(const game_api::HudSprite& command,const RenderExtent extent,
                           const std::span<const RenderHudSpriteBinding> bindings) {
        const RenderHudSpriteBinding* binding=nullptr;
        bool saw_resource=false;
        for(const auto& candidate : bindings) {
            if(candidate.resource_id!=command.resource_id) continue;
            saw_resource=true;
            if(candidate.resource_revision!=command.resource_revision) continue;
            if(binding) {++hud_statistics_.last_invalid_commands;return;}
            binding=&candidate;
        }
        if(!binding) {
            if(saw_resource) ++hud_statistics_.last_revision_mismatch;
            else ++hud_statistics_.last_missing_binding;
            return;
        }
        if(!binding->asset || binding->asset->source_identity()!=entity_render::EntityRenderResourceIdentity{
            binding->resource_id,binding->resource_revision}) {
            ++hud_statistics_.last_revision_mismatch;return;
        }
        if(command.frame>=binding->asset->frames().size()) {++hud_statistics_.last_invalid_frame;return;}
        const auto& frame=binding->asset->frames()[command.frame];
        const auto& crop=command.source;
        if(crop.x>=frame.geometry.width || crop.y>=frame.geometry.height ||
           crop.width>frame.geometry.width-crop.x || crop.height>frame.geometry.height-crop.y) {
            ++hud_statistics_.last_invalid_crop;return;
        }
        if(frame.profile==entity_render::SpriteRenderTextureProfile::unsupported) {
            ++hud_statistics_.last_unsupported_material;return;
        }
        if(command.clip) {
            const auto& clip=*command.clip;
            const auto left=std::clamp(static_cast<int>(std::ceil(clip.x)),0,extent.width);
            const auto top=std::clamp(static_cast<int>(std::ceil(clip.y)),0,extent.height);
            const auto right=std::clamp(static_cast<int>(std::ceil(clip.x+clip.width)),0,extent.width);
            const auto bottom=std::clamp(static_cast<int>(std::ceil(clip.y+clip.height)),0,extent.height);
            if(right<=left || bottom<=top) {++hud_statistics_.last_empty_clip;return;}
            glEnable(GL_SCISSOR_TEST); glScissor(left,extent.height-bottom,right-left,bottom-top);
        } else glDisable(GL_SCISSOR_TEST);
        const auto texture=hud_texture(*binding,command.frame);
        if(!texture) return;
        if(!hud_sprite_program_.name()) create_hud_sprite_program();
        const float x0=2.0F*command.x/extent.width-1.0F;
        const float x1=2.0F*(command.x+command.width)/extent.width-1.0F;
        const float y0=1.0F-2.0F*command.y/extent.height;
        const float y1=1.0F-2.0F*(command.y+command.height)/extent.height;
        const float u0=static_cast<float>(crop.x)/frame.geometry.width;
        const float v0=static_cast<float>(crop.y)/frame.geometry.height;
        const float u1=static_cast<float>(crop.x+crop.width)/frame.geometry.width;
        const float v1=static_cast<float>(crop.y+crop.height)/frame.geometry.height;
        const std::array<float,24> vertices{x0,y0,u0,v0,x0,y1,u0,v1,x1,y1,u1,v1,
            x0,y0,u0,v0,x1,y1,u1,v1,x1,y0,u1,v0};
        glBindVertexArray(hud_sprite_array_.name()); glBindBuffer(GL_ARRAY_BUFFER,hud_sprite_buffer_.name());
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_DYNAMIC_DRAW);
        if(command.blend==game_api::HudSpriteBlend::opaque) glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,command.blend==game_api::HudSpriteBlend::additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        }
        glUseProgram(hud_sprite_program_.name());
        glUniform4fv(hud_sprite_color_,1,command.color.data());
        glUniform1i(hud_sprite_masked_,command.blend==game_api::HudSpriteBlend::alpha_test ||
            frame.profile==entity_render::SpriteRenderTextureProfile::alpha_test_masked?1:0);
        glBindTexture(GL_TEXTURE_2D,texture);
        glDrawArrays(GL_TRIANGLES,0,6);
        ++hud_statistics_.sprite_draw_count; ++hud_statistics_.last_rendered_sprite_count;
    }

    void create_hud_sprite_program() {
        constexpr char vertex_source[]=R"GLSL(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 source_uv;
out vec2 uv;
void main(){gl_Position=vec4(position,0,1);uv=source_uv;}
)GLSL";
        constexpr char fragment_source[]=R"GLSL(#version 330 core
in vec2 uv;
uniform sampler2D image;
uniform vec4 tint;
uniform int masked;
out vec4 output_color;
void main(){vec4 texel=texture(image,uv);if(masked!=0&&texel.a<0.5)discard;output_color=texel*tint;}
)GLSL";
        auto vertex=compile_shader(GL_VERTEX_SHADER,vertex_source,"HUD sprite vertex");
        auto fragment=compile_shader(GL_FRAGMENT_SHADER,fragment_source,"HUD sprite fragment");
        const auto name=glCreateProgram();
        if(!name) fail(OpenGlRendererErrorCode::program_link_failed,"HUD sprite program creation failed");
        hud_sprite_program_=GlObject::adopt(GlObjectKind::program,name);
        glAttachShader(name,vertex.name()); glAttachShader(name,fragment.name()); glLinkProgram(name);
        GLint linked{}; glGetProgramiv(name,GL_LINK_STATUS,&linked);
        if(linked!=GL_TRUE) fail(OpenGlRendererErrorCode::program_link_failed,"HUD sprite shader link failed");
        glUseProgram(name);
        hud_sprite_color_=glGetUniformLocation(name,"tint");
        hud_sprite_masked_=glGetUniformLocation(name,"masked");
        const auto sampler=glGetUniformLocation(name,"image");
        if(hud_sprite_color_<0 || hud_sprite_masked_<0 || sampler<0)
            fail(OpenGlRendererErrorCode::program_link_failed,"HUD sprite shader inputs missing");
        glUniform1i(sampler,0);
        hud_sprite_array_=create_vertex_array(); hud_sprite_buffer_=create_buffer();
        glBindVertexArray(hud_sprite_array_.name()); glBindBuffer(GL_ARRAY_BUFFER,hud_sprite_buffer_.name());
        glEnableVertexAttribArray(0U); glVertexAttribPointer(0U,2,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
        glEnableVertexAttribArray(1U); glVertexAttribPointer(1U,2,GL_FLOAT,GL_FALSE,4*sizeof(float),
            reinterpret_cast<const void*>(2*sizeof(float)));
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"HUD sprite GPU initialization");
    }

    [[nodiscard]] static std::array<std::uint8_t, 7U> glyph(char character) noexcept
    {
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
        switch (character) {
        case 'A': return {14,17,17,31,17,17,17};
        case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {15,16,16,16,16,16,15};
        case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31};
        case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {15,16,16,23,17,17,15};
        case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {31,4,4,4,4,4,31};
        case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17};
        case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17};
        case 'N': return {17,25,21,19,17,17,17};
        case 'O': return {14,17,17,17,17,17,14};
        case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13};
        case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30};
        case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14};
        case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10};
        case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4};
        case 'Z': return {31,1,2,4,8,16,31};
        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};
        case '_': return {0,0,0,0,0,0,31};
        case '-': return {0,0,0,31,0,0,0};
        case '/': return {1,1,2,4,8,16,16};
        case '[': return {14,8,8,8,8,8,14};
        case ']': return {14,2,2,2,2,2,14};
        case ' ': return {};
        default: return {14,17,1,2,4,0,4};
        }
    }

    void build_hud_vertices(const RenderBasicHud& hud, const RenderExtent extent)
    {
        hud_vertices_.clear();
        hud_batches_.clear();
        if (extent.width <= 0 || extent.height <= 0) return;
        const auto rectangle = [&](const float x, const float y,
                                   const float width, const float height,
                                   const std::array<float, 4U> color) {
            const float x0 = 2.0F * x / extent.width - 1.0F;
            const float x1 = 2.0F * (x + width) / extent.width - 1.0F;
            const float y0 = 1.0F - 2.0F * y / extent.height;
            const float y1 = 1.0F - 2.0F * (y + height) / extent.height;
            const auto vertex = [&](const float px, const float py) {
                hud_vertices_.insert(hud_vertices_.end(),
                    {px, py, color[0], color[1], color[2], color[3]});
            };
            vertex(x0,y0); vertex(x0,y1); vertex(x1,y1);
            vertex(x0,y0); vertex(x1,y1); vertex(x1,y0);
        };
        const auto finite_color = [](const std::array<float, 4U>& value) {
            return std::all_of(value.begin(), value.end(),
                [](float c) { return std::isfinite(c) && c >= 0.0F && c <= 1.0F; });
        };
        // Bounded neutral geometry. Layout, labels, visibility and colors belong
        // to the producer; this pass only rasterizes rectangles and glyphs.
        if (!game_api::valid_hud_draw_commands(hud)) return;
        for (const auto& item : hud.rectangles) {
            if (!std::isfinite(item.x) || !std::isfinite(item.y) ||
                !std::isfinite(item.width) || !std::isfinite(item.height) ||
                item.width < 0.0F || item.height < 0.0F || !finite_color(item.color)) continue;
            const auto first=hud_vertices_.size()/6U;
            rectangle(item.x,item.y,item.width,item.height,item.color);
            hud_batches_.push_back({false,0U,first,hud_vertices_.size()/6U-first,item.draw_order});
        }
        for(std::size_t index=0U;index<hud.sprites.size();++index)
            hud_batches_.push_back({true,index,0U,0U,hud.sprites[index].draw_order});
        for (const auto& item : hud.texts) {
            if (item.text.size() > 160U || !std::isfinite(item.x) || !std::isfinite(item.y) ||
                !std::isfinite(item.pixel_size) || !std::isfinite(item.advance) ||
                !std::isfinite(item.line_height) || item.pixel_size <= 0.0F ||
                item.advance < 0.0F || item.line_height < 0.0F || !finite_color(item.color)) continue;
            float x = item.x;
            float y = item.y;
            const auto first=hud_vertices_.size()/6U;
            for (const char character : item.text) {
                if (character == '\n') { x = item.x; y += item.line_height; continue; }
                const auto rows = glyph(character);
                for (std::size_t row = 0U; row < rows.size(); ++row)
                    for (std::size_t column = 0U; column < 5U; ++column)
                        if ((rows[row] & (1U << (4U - column))) != 0U)
                            rectangle(x + static_cast<float>(column) * item.pixel_size,
                                y + static_cast<float>(row) * item.pixel_size,
                                item.pixel_size,item.pixel_size,item.color);
                x += item.advance;
            }
            hud_batches_.push_back({false,0U,first,hud_vertices_.size()/6U-first,item.draw_order});
        }
        std::stable_sort(hud_batches_.begin(),hud_batches_.end(),
            [](const auto& a,const auto& b){return a.order<b.order;});
    }


    void create_hud_program()
    {
        constexpr char vertex_source[] = R"GLSL(#version 330 core
layout(location=0) in vec2 p;
layout(location=1) in vec4 c;
out vec4 color;
void main(){gl_Position=vec4(p,0,1);color=c;}
)GLSL";
        constexpr char fragment_source[] = R"GLSL(#version 330 core
in vec4 color;
out vec4 output_color;
void main(){output_color=color;}
)GLSL";
        auto vertex = compile_shader(GL_VERTEX_SHADER, vertex_source, "HUD vertex");
        auto fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source,
            "HUD fragment");
        const GLuint name = glCreateProgram();
        if (name == 0U) fail(OpenGlRendererErrorCode::program_link_failed,
            "HUD program creation failed");
        hud_program_ = GlObject::adopt(GlObjectKind::program, name);
        glAttachShader(name, vertex.name());
        glAttachShader(name, fragment.name());
        glLinkProgram(name);
        GLint linked = GL_FALSE;
        glGetProgramiv(name, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) fail(OpenGlRendererErrorCode::program_link_failed,
            "HUD shader link failed");
        hud_array_ = create_vertex_array();
        hud_buffer_ = create_buffer();
        glBindVertexArray(hud_array_.name());
        glBindBuffer(GL_ARRAY_BUFFER, hud_buffer_.name());
        glEnableVertexAttribArray(0U);
        glVertexAttribPointer(0U, 2, GL_FLOAT, GL_FALSE,
            6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1U);
        glVertexAttribPointer(1U, 4, GL_FLOAT, GL_FALSE,
            6 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
        glBindVertexArray(0U);
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "HUD GPU initialization");
    }

public:
    void release_entity_resources() noexcept
    {
        entity_renderer_.release_resources();
    }

    void render_entities(
        const RenderDynamicEntities& entities,
        const RenderCamera& camera,
        const RenderMatrix4& view_projection,
        const std::optional<RenderPointLight>& light = std::nullopt,
        std::span<const RenderPointLight> effect_lights = {},
        const assets::AssetVector3& light_direction = {0.0F,0.0F,-1.0F})
    {
        entity_renderer_.render(entities, camera, view_projection, light,effect_lights,light_direction);
    }

    [[nodiscard]] const OpenGlEntityRendererStatistics& entity_statistics()
        const noexcept
    {
        return entity_renderer_.statistics();
    }

    void clear_visibility_tracking() noexcept
    {
        visibility_scene_identity_.reset();
        visibility_result_signature_.reset();
        visibility_revision_ = 0U;
        statistics_.visibility_revision = 0U;
    }

    void note_visibility(
        const world_visibility::WorldVisibilitySceneIdentity scene_identity,
        const std::uint64_t revision,
        const world_visibility::WorldVisibilityResultSignature
            result_signature) noexcept
    {
        if (!visibility_scene_identity_ ||
            *visibility_scene_identity_ != scene_identity ||
            visibility_revision_ != revision ||
            !visibility_result_signature_ ||
            *visibility_result_signature_ != result_signature) {
            ++statistics_.visibility_update_count;
            visibility_scene_identity_ = scene_identity;
            visibility_revision_ = revision;
            visibility_result_signature_ = result_signature;
        }
        statistics_.visibility_revision = revision;
    }

    void release_world_resources() noexcept
    {
        backdrop_submission_.reset();
        for(auto& texture:decal_textures_)texture.reset();
        for(auto& geometry:decal_geometries_)geometry.reset();
        decal_statistics_.cached_texture_count=decal_statistics_.cached_geometry_count=0;
        if (resources_ || scene_resources_) {
            resources_.reset();
            scene_resources_.reset();
            ++statistics_.world_resource_release_count;
        }
        statistics_.active_world_resources = false;
        statistics_.package_revision = 0U;
        statistics_.scene_revision = 0U;
        statistics_.uploaded_base_texture_count = 0U;
        statistics_.uploaded_base_mip_level_count = 0U;
        statistics_.uploaded_lightmap_page_count = 0U;
        statistics_.uploaded_lightmap_layer_count = 0U;
        statistics_.uploaded_white_lightmap_count = 0U;
        clear_visibility_tracking();
    }

    [[nodiscard]] const SkyPreparedSubmission& backdrop_submission() const noexcept {return backdrop_submission_;}

    bool prepare_backdrop_submission(const RenderScene& scene,const RenderCamera& camera,
        const RenderMatrix4& vp,GpuWorldResources* world_gpu,GpuWorldResources* brush_gpu,
        const std::optional<std::uint64_t> receipt_frame)
    {
        SkyScopeNanoseconds preparation{background_sky_diagnostics.prepare_ns};
        auto& plan=backdrop_submission_;
        const auto& world=*scene.static_world;
        if(world.scene_package && world.runtime_brushes && !brush_gpu &&
           !world.runtime_brushes->instances.empty())return false;
        if(world.scene_package && receipt_frame) {
            plan.static_receipt.emplace();
            plan.static_receipt->frame_number=*receipt_frame;
            plan.static_receipt->resource_id=world.package->resource_id();
            plan.static_receipt->resource_revision=world.package->resource_revision();
        }
        if(world.visible_draw_list) {
            if(!world.scene_package)return false;
            if(plan.static_receipt)plan.static_receipt->path=OpenGlStaticWorldPath::visible_commands;
            for(const auto& command:world.visible_draw_list->commands()) {
                const bool brush=command.object_kind==world_visibility::WorldVisibleObjectKind::brush_instance_surface;
                if((command.object_kind!=world_visibility::WorldVisibleObjectKind::world_surface && !brush) ||
                   (brush && !brush_gpu))return false;
                if((brush && (!command.source_model_index || !command.source_instance_index)) ||
                   (!brush && (command.source_model_index || command.source_instance_index)) ||
                   !renderer::is_finite(command.model_transform))return false;
                const auto& package=*(brush?brush_gpu:world_gpu)->package_instance;
                if(command.render_material_index>=package.materials().size())return false;
                const auto& material=package.materials()[command.render_material_index];
                if(command.alpha_mode!=material.base_texture_alpha_mode || command.lightmap_mode!=material.lightmap_mode ||
                   command.lightmap_atlas_page_index!=material.lightmap_atlas_page_index)return false;
                plan.append(brush?brush_gpu:world_gpu,command.first_index,command.index_count,
                    command.render_material_index,renderer::multiply(vp,command.model_transform),command.model_transform,
                    brush,static_cast<unsigned>(command.object_kind),command.source_model_index.value_or(0xffffffffU),
                    command.source_instance_index.value_or(0xffffffffU));
            }
        } else if(world.scene_package) {
            select_static_world_ranges(*world.package,vp,statistics_,plan.static_receipt,
                [&](std::uint32_t first,std::uint32_t count,std::size_t material){
                    plan.append(world_gpu,first,count,material,vp,RenderMatrix4{},false,0);
                });
        } else {
            for(const auto& batch:world.package->draw_batches())
                plan.append(world_gpu,batch.first_index,batch.index_count,batch.render_material_index,
                    vp,RenderMatrix4{},false,0);
        }
        plan.static_range_count=plan.ranges.size();
        if(world.scene_package && world.runtime_brushes) {
            plan.has_runtime=true;
            select_runtime_brush_ranges(*world.runtime_brushes,*world.scene_package,camera,vp,statistics_,
                [&](const auto& surface,const auto& instance,const auto& mvp){
                    GLuint alternate=0;
                    const auto mapping=world.scene_package->brush_library().alternate_by_material();
                    if(instance.alternate_texture && !mapping.empty() && mapping[surface.render_material_index])
                        alternate=scene_resources_->brush_alternate_textures[*mapping[surface.render_material_index]].name();
                    plan.append(brush_gpu,surface.first_index,surface.index_count,surface.render_material_index,
                        mvp,instance.model_transform,true,2,instance.source_model_index,instance.entity_number,
                        alternate);
                });
        }
        plan.ready=true;
        ++background_sky_diagnostics.prepared_frames;
        background_sky_diagnostics.prepared_ranges+=plan.ranges.size();
        return true;
    }

    // Return zero only when all pre-backdrop draws are independent color
    // replacements with no sample allowed to round to the depth clear value.
    // The original early path is retained for unknown resources/state. This
    // guard does not mutate GL state or rely on runtime transformed metadata.
    [[nodiscard]] std::uint64_t backdrop_fallback(
        const RenderScene& scene,const RenderCamera& camera,RenderExtent extent,
        const std::optional<std::uint64_t> receipt_frame) {
        SkyScopeNanoseconds guard{background_sky_diagnostics.guard_ns};
        backdrop_submission_.reset();
        if(!scene.skybox || sky_asset_!=scene.skybox || !sky_texture_.name() ||
           !scene.static_world || !scene.static_world->package)return 1;
        const auto& world=*scene.static_world;
        GpuWorldResources* world_gpu{};
        GpuWorldResources* brush_gpu{};
        if(world.scene_package) {
            if(!scene_resources_ || scene_resources_->scene_instance!=world.scene_package ||
               scene_resources_->world.package_instance!=world.package)return 2;
            world_gpu=&scene_resources_->world;
            if(scene_resources_->brushes)brush_gpu=&*scene_resources_->brushes;
        }else {
            if(!resources_ || resources_->package_instance!=world.package)return 2;
            world_gpu=&*resources_;
        }
        GLint bits{},function{},polygon[2]{sky_polygon_query_unwritten_first,sky_polygon_query_unwritten_second};
        GLboolean mask{},color[4]{};GLdouble range[2]{};
        glGetIntegerv(GL_DEPTH_FUNC,&function);
        glGetIntegerv(GL_POLYGON_MODE,polygon);glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
        glGetBooleanv(GL_COLOR_WRITEMASK,color);glGetDoublev(GL_DEPTH_RANGE,range);
        background_sky_stats.gl_state_queries+=5;
        GLint framebuffer{},component{},object{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
        const auto depth_attachment=framebuffer?GL_DEPTH_ATTACHMENT:GL_DEPTH;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
            depth_attachment,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&object);
        background_sky_stats.gl_state_queries+=2;
        if(object==GL_NONE)return 4;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
            depth_attachment,GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,&bits);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,depth_attachment,
            GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,&component);
        background_sky_stats.gl_state_queries+=2;
        // Core removed separate front/back modes (GL 3.3 core sections 3.6.3,
        // E.2.2); POLYGON_MODE is one shared enum, not a required legacy pair.
        // Some drivers still write a redundant pair. Accept either shape only
        // when the actual, error-checked context profile establishes its meaning.
        // An unwritten slot is NOT inferred from zero and contradictions fail closed.
        auto polygon_shape=SkyPolygonQueryShape::unknown_profile;
        const auto polygon_enum=[](GLint value){return value==GL_FILL || value==GL_LINE || value==GL_POINT;};
        if(context_profile_query_checked_ && context_profile_mask_==GL_CONTEXT_CORE_PROFILE_BIT){
            polygon_shape=SkyPolygonQueryShape::invalid_or_contradictory;
            if(polygon_enum(polygon[0])){
                if(polygon[1]==sky_polygon_query_unwritten_second)polygon_shape=SkyPolygonQueryShape::core_one_value;
                else if(polygon[1]==polygon[0])polygon_shape=SkyPolygonQueryShape::core_redundant_pair;
            }
        }else if(context_profile_query_checked_ && context_profile_mask_==GL_CONTEXT_COMPATIBILITY_PROFILE_BIT){
            polygon_shape=polygon_enum(polygon[0]) && polygon_enum(polygon[1])?
                SkyPolygonQueryShape::compatibility_pair:SkyPolygonQueryShape::invalid_or_contradictory;
        }
        const bool polygon_fill=polygon[0]==GL_FILL &&
            (polygon_shape==SkyPolygonQueryShape::core_one_value ||
             polygon_shape==SkyPolygonQueryShape::core_redundant_pair ||
             (polygon_shape==SkyPolygonQueryShape::compatibility_pair && polygon[1]==GL_FILL));
        const auto state_failure=[&](SkyStateFailure reason,unsigned capability=0U,unsigned error=0U){
            auto& d=background_sky_diagnostics;
            ++d.state_failures[static_cast<std::size_t>(reason)];
            bool known=false;
            for(std::uint32_t i=0;i<d.state_witness_count;++i){const auto& old=d.state_witnesses[i];
                if(old.failure==reason && old.depth_function==function && old.depth_write_mask==mask &&
                   old.polygon_front==polygon[0] && old.polygon_back==polygon[1] &&
                   old.context==startup_context_ && old.context_profile_mask==context_profile_mask_ &&
                   old.failed_capability==capability && old.api_error==error &&
                   old.depth_range[0]==range[0] && old.depth_range[1]==range[1] &&
                   std::equal(old.color_write_mask.begin(),old.color_write_mask.end(),color)){known=true;break;}}
            if(!known && d.state_witness_count<d.state_witnesses.size()){
                auto& w=d.state_witnesses[d.state_witness_count++];w.frame=background_sky_stats.frames+1;
                w.failure=reason;w.depth_function=function;w.depth_write_mask=mask;
                w.polygon_front=polygon[0];w.polygon_back=polygon[1];
                for(unsigned i=0;i<4;++i)w.color_write_mask[i]=color[i];
                w.depth_range={range[0],range[1]};w.framebuffer=framebuffer;w.depth_bits=bits;
                w.depth_component=component;w.depth_object=object;w.failed_capability=capability;w.api_error=error;
                w.context=startup_context_;w.context_profile_mask=context_profile_mask_;
                w.context_major=context_major_;w.context_minor=context_minor_;
                w.context_profile_query_checked=context_profile_query_checked_;w.polygon_query_shape=polygon_shape;
            }else if(!known)++d.state_witness_overflow;
            return std::uint64_t{8};
        };
        // Keep the legacy nine queried state/attachment calls count comparable.
        // Errors are fatal as before the frame-clear error boundary, not safe values.
        const auto query_error=glGetError();
        auto& diagnostic=background_sky_diagnostics;
        bool polygon_known=false;
        for(std::uint32_t i=0;i<diagnostic.polygon_query_witness_count;++i){const auto& old=diagnostic.polygon_query_witnesses[i];
            if(old.context==startup_context_ && old.raw_first==polygon[0] && old.raw_second==polygon[1] &&
               old.query_group_error==query_error){polygon_known=true;break;}}
        if(!polygon_known && diagnostic.polygon_query_witness_count<diagnostic.polygon_query_witnesses.size()){
            auto& w=diagnostic.polygon_query_witnesses[diagnostic.polygon_query_witness_count++];
            w.frame=background_sky_stats.frames+1;w.context=startup_context_;
            w.context_profile_mask=context_profile_mask_;w.context_major=context_major_;w.context_minor=context_minor_;
            w.context_profile_query_checked=context_profile_query_checked_;
            w.raw_first=polygon[0];w.raw_second=polygon[1];w.shape=polygon_shape;
            w.query_group_error=query_error;w.fill_eligible=query_error==GL_NO_ERROR && polygon_fill;
        }
        if(query_error!=GL_NO_ERROR){
            state_failure(SkyStateFailure::query_error,0,query_error);
            fail(OpenGlRendererErrorCode::gl_operation_failed,"Sky eligibility state query failed");
        }
        if(bits<16)return 4;
        if(!((component==GL_UNSIGNED_NORMALIZED && (bits==16 || bits==24 || bits==32)) ||
             (component==GL_FLOAT && bits==32)))return 4;
        if(mask!=GL_TRUE)return state_failure(SkyStateFailure::depth_write_mask);
        if(range[0]!=0.0 || range[1]!=1.0)return state_failure(SkyStateFailure::depth_range);
        if(function!=GL_LESS && function!=GL_LEQUAL)return state_failure(SkyStateFailure::inherited_depth_function);
        if(!polygon_fill)return state_failure(SkyStateFailure::polygon_mode);
        if(std::ranges::any_of(color,[](const auto c){return c!=GL_TRUE;}))return state_failure(SkyStateFailure::color_write_mask);
        for(const auto state:{GL_SCISSOR_TEST,GL_STENCIL_TEST,GL_COLOR_LOGIC_OP,
                GL_POLYGON_OFFSET_FILL,GL_RASTERIZER_DISCARD,GL_DEPTH_CLAMP,
                GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK}) {
            ++background_sky_stats.gl_state_queries;if(glIsEnabled(state))return state_failure(SkyStateFailure::enabled_capability,state);
        }
        GLint clip_distances{};glGetIntegerv(GL_MAX_CLIP_DISTANCES,&clip_distances);
        ++background_sky_stats.gl_state_queries;
        for(GLint i=0;i<clip_distances;++i){++background_sky_stats.gl_state_queries;if(glIsEnabled(GL_CLIP_DISTANCE0+i))return state_failure(SkyStateFailure::enabled_clip_distance,GL_CLIP_DISTANCE0+i);}
        const auto vp=camera_view_projection(camera,extent);if(!vp || !vp.matrix)return 16;
        if(!prepare_backdrop_submission(scene,camera,*vp.matrix,world_gpu,brush_gpu,receipt_frame))return 16;
        const auto annotate=[&](SkyDepthWitness numeric,const SkyPreparedRange& draw,
            const assets::WorldBounds& bounds,unsigned first,unsigned count,unsigned surface,bool whole) {
            auto& w=numeric;w.frame=background_sky_stats.frames+1;
            w.resource_id=draw.resources->identity.resource_id;w.resource_revision=draw.resources->identity.revision;
            w.object_kind=draw.object_kind;w.model_index=draw.model_index;w.instance_index=draw.instance_index;
            w.first_index=first;w.index_count=count;w.source_surface_index=surface;
            w.actual_range_first=draw.first_index;w.actual_range_count=draw.index_count;
            w.bounds={bounds.minimum.x,bounds.minimum.y,bounds.minimum.z,bounds.maximum.x,bounds.maximum.y,bounds.maximum.z};
            w.mvp=draw.mvp.values;w.camera_position={camera.position.x,camera.position.y,camera.position.z};
            w.camera_target={camera.target.x,camera.target.y,camera.target.z};w.camera_up={camera.up.x,camera.up.y,camera.up.z};
            w.camera_fov=camera.vertical_field_of_view_radians;w.camera_near=camera.near_plane;w.camera_far=camera.far_plane;
            w.width=extent.width;w.height=extent.height;w.depth_bits=bits;w.depth_component=component;w.framebuffer=framebuffer;
            w.whole_package_bounds=whole;w.exact_submitted_geometry=!whole && draw.coverage;
            return w;
        };
        const auto record_failure=[&](const SkyDepthWitness& witness) {
            auto& d=background_sky_diagnostics;++d.depth_failures;
            bool known=false;
            for(std::uint32_t i=0;i<d.depth_witness_count;++i){const auto& old=d.depth_witnesses[i];
                if(old.resource_id==witness.resource_id && old.resource_revision==witness.resource_revision &&
                   old.object_kind==witness.object_kind && old.model_index==witness.model_index &&
                   old.instance_index==witness.instance_index && old.first_index==witness.first_index &&
                   old.index_count==witness.index_count && old.corner==witness.corner &&
                   old.failure==witness.failure){known=true;break;}}
            if(!known && d.depth_witness_count<d.depth_witnesses.size())
                d.depth_witnesses[d.depth_witness_count++]=witness;
            else if(!known)++d.depth_witness_overflow;
        };
        std::optional<SkyDepthWitness> recovered;
        for(const auto& draw:backdrop_submission_.ranges) {
            ++background_sky_diagnostics.range_tests;
            SkyDepthWitness numeric;
            if(!draw.coverage) {
                numeric.failure=6;
                record_failure(annotate(numeric,draw,draw.bounds,draw.first_index,draw.index_count,0xffffffffU,false));
                return 16;
            }
            // Narrow old-predicate witness, bounded by the retained witness set.
            // It never controls eligibility and is not a second submission plan.
            auto& d=background_sky_diagnostics;
            if(!recovered && d.recovered_depth_witness_count<d.recovered_depth_witnesses.size() &&
               draw.resources->backdrop_depth_bounds) {
                bool known=false;
                for(std::uint32_t i=0;i<d.recovered_depth_witness_count;++i){const auto& old=d.recovered_depth_witnesses[i];
                    if(old.resource_id==draw.resources->identity.resource_id &&
                       old.resource_revision==draw.resources->identity.revision &&
                       old.object_kind==draw.object_kind && old.model_index==draw.model_index){known=true;break;}}
                if(!known) {
                    SkyDepthWitness old;
                    if(!sky_depth_guard(*draw.resources->backdrop_depth_bounds,draw.mvp,bits,&old))
                        recovered=annotate(old,draw,*draw.resources->backdrop_depth_bounds,0,
                            static_cast<unsigned>(draw.resources->package_instance->indices().size()),0xffffffffU,true);
                }
            }
            ++background_sky_stats.bounds_tested;
            if(sky_depth_guard(draw.bounds,draw.mvp,bits,&numeric))continue;
            // A material range may unite distant surfaces: test the exact
            // immutable source-surface intervals, not fictitious union corners.
            // This keeps every submitted index, including full-batch fallbacks.
            ++background_sky_diagnostics.surface_refinements;
            const auto surfaces=std::span{draw.resources->backdrop_surfaces}.subspan(draw.first_surface,draw.surface_count);
            for(const auto& surface:surfaces) {
                ++background_sky_stats.bounds_tested;
                SkyDepthWitness precise;
                if(sky_depth_guard(surface.bounds,draw.mvp,bits,&precise))continue;
                record_failure(annotate(precise,draw,surface.bounds,surface.first_index,surface.index_count,
                    surface.source_surface_index,false));
                return 16;
            }
        }
        if(recovered) {
            recovered->recovered_by_actual_submission=true;
            auto& d=background_sky_diagnostics;
            d.recovered_depth_witnesses[d.recovered_depth_witness_count++]=*recovered;
        }
        return 0;
    }

    [[nodiscard]] GpuWorldResources& ensure_resources(
        const std::shared_ptr<const world_render::WorldRenderPackage>& package)
    {
        const auto identity = package->resource_identity();
        if (resources_ && resources_->package_instance == package &&
            resources_->identity == identity) {
            return *resources_;
        }

        release_world_resources();
        try {
            const auto hardware = query_hardware_limits();
            validate_package(*package, hardware);
            auto candidate = build_gpu_resources(*package, true);
            candidate.package_instance = package;
            resources_.emplace(std::move(candidate));
            ++statistics_.upload_count;
            statistics_.package_revision = identity.revision;
            statistics_.uploaded_base_texture_count =
                static_cast<std::uint64_t>(resources_->base_textures.size());
            statistics_.uploaded_base_mip_level_count =
                static_cast<std::uint64_t>(resources_->base_textures.size()) *
                static_cast<std::uint64_t>(assets::kWorldTextureMipLevelCount);
            statistics_.uploaded_lightmap_page_count =
                static_cast<std::uint64_t>(resources_->lightmap_pages.size());
            statistics_.uploaded_lightmap_layer_count =
                static_cast<std::uint64_t>(resources_->lightmap_pages.size()) *
                static_cast<std::uint64_t>(assets::kWorldLightmapStyleSlotCount);
            statistics_.uploaded_white_lightmap_count =
                resources_->white_lightmap.name() == 0U ? 0U : 1U;
            statistics_.active_world_resources = true;
            return *resources_;
        } catch (const OpenGlRendererError&) {
            ++statistics_.failed_upload_count;
            release_world_resources();
            throw;
        } catch (const std::bad_alloc&) {
            ++statistics_.failed_upload_count;
            release_world_resources();
            fail(OpenGlRendererErrorCode::unable_to_retain_resources,
                "Unable to retain static-world OpenGL resources");
        } catch (const std::length_error&) {
            ++statistics_.failed_upload_count;
            release_world_resources();
            fail(OpenGlRendererErrorCode::unable_to_retain_resources,
                "Static-world OpenGL resource containers exceed their limits");
        }
    }

    [[nodiscard]] GpuSceneResources& ensure_scene_resources(
        const std::shared_ptr<
            const world_scene_render::WorldSceneRenderPackage>& scene)
    {
        const auto identity = scene->resource_identity();
        if (scene_resources_ && scene_resources_->identity == identity) {
            // The immutable scene identity/revision is the renderer cache key.
            // Refresh the retained owners so an equivalent republished package
            // does not keep an otherwise-dead CPU package alive or trigger a
            // visibility-only GPU upload.
            scene_resources_->scene_instance = scene;
            scene_resources_->world.package_instance =
                scene->world_package();
            if (scene_resources_->brushes) {
                scene_resources_->brushes->package_instance =
                    scene->brush_library().render_package();
            }
            return *scene_resources_;
        }

        release_world_resources();
        try {
            const auto& world_package = scene->world_package();
            if (!world_package) {
                fail(OpenGlRendererErrorCode::invalid_world_package,
                    "World scene contains no world render package");
            }
            const auto hardware = query_hardware_limits();
            validate_package(*world_package, hardware);

            GpuSceneResources candidate;
            candidate.scene_instance = scene;
            candidate.identity = identity;
            candidate.world = build_gpu_resources(*world_package, true);
            candidate.world.package_instance = world_package;

            const auto& brush_package =
                scene->brush_library().render_package();
            if (brush_package) {
                validate_package(*brush_package, hardware);
                candidate.brushes.emplace(
                    build_gpu_resources(*brush_package, false));
                candidate.brushes->package_instance = brush_package;
                if (const auto& bank = scene->brush_library().alternate_textures())
                    for (const auto& texture : bank->textures())
                        candidate.brush_alternate_textures.push_back(upload_base_texture(texture));
            }

            scene_resources_.emplace(std::move(candidate));
            ++statistics_.upload_count;
            ++statistics_.scene_upload_count;
            if (scene_resources_->brushes) {
                ++statistics_.brush_upload_count;
            }
            statistics_.package_revision =
                world_package->resource_revision();
            statistics_.scene_revision = identity.revision;

            const auto accumulate = [this](const GpuWorldResources& value) {
                statistics_.uploaded_base_texture_count +=
                    static_cast<std::uint64_t>(value.base_textures.size());
                statistics_.uploaded_base_mip_level_count +=
                    static_cast<std::uint64_t>(value.base_textures.size()) *
                    static_cast<std::uint64_t>(
                        assets::kWorldTextureMipLevelCount);
                statistics_.uploaded_lightmap_page_count +=
                    static_cast<std::uint64_t>(value.lightmap_pages.size());
                statistics_.uploaded_lightmap_layer_count +=
                    static_cast<std::uint64_t>(value.lightmap_pages.size()) *
                    static_cast<std::uint64_t>(
                        assets::kWorldLightmapStyleSlotCount);
                statistics_.uploaded_white_lightmap_count +=
                    value.white_lightmap.name() == 0U ? 0U : 1U;
            };
            accumulate(scene_resources_->world);
            if (scene_resources_->brushes) {
                accumulate(*scene_resources_->brushes);
            }
            statistics_.uploaded_base_texture_count += scene_resources_->brush_alternate_textures.size();
            statistics_.uploaded_base_mip_level_count += scene_resources_->brush_alternate_textures.size() * assets::kWorldTextureMipLevelCount;
            statistics_.active_world_resources = true;
            return *scene_resources_;
        } catch (const OpenGlRendererError&) {
            ++statistics_.failed_upload_count;
            release_world_resources();
            throw;
        } catch (const std::bad_alloc&) {
            ++statistics_.failed_upload_count;
            release_world_resources();
            fail(OpenGlRendererErrorCode::unable_to_retain_resources,
                "Unable to retain world-scene OpenGL resources");
        } catch (const std::length_error&) {
            ++statistics_.failed_upload_count;
            release_world_resources();
            fail(OpenGlRendererErrorCode::unable_to_retain_resources,
                "World-scene OpenGL resource containers exceed their limits");
        }
    }

    [[nodiscard]] OpenGlWorldRendererStatistics& statistics() noexcept
    {
        return statistics_;
    }

    [[nodiscard]] const OpenGlWorldRendererStatistics& statistics() const noexcept
    {
        return statistics_;
    }

    [[nodiscard]] const OpenGlHudRendererStatistics& hud_statistics() const noexcept {
        return hud_statistics_;
    }
    [[nodiscard]] const OpenGlDecalRendererStatistics& decal_statistics() const noexcept {
        return decal_statistics_;
    }
    [[nodiscard]] const OpenGlImageAdjustmentStatistics& image_adjustment_statistics() const noexcept {
        return image_statistics_;
    }
    detail::SceneTarget scene_target;

private:
    GlObject image_program_,image_texture_,image_array_;
    GLint image_adjustments_{-1};
    RenderExtent image_extent_{};
    OpenGlImageAdjustmentStatistics image_statistics_;
    GlObject decal_program_;
    GLint decal_matrix_{-1},decal_sampler_{-1},decal_material_mode_{-1};
    struct DecalTextureCache {
        std::shared_ptr<const assets::WorldTextureAsset> asset;
        GlObject texture;
        std::uint64_t last_used{};
    };
    struct DecalGeometryCache {
        std::shared_ptr<const std::vector<RenderDecalVertex>> vertices;
        std::uint64_t revision{};
        GlObject array,buffer;
        std::uint64_t last_used{};
    };
    std::array<std::optional<DecalTextureCache>,16U> decal_textures_;
    std::array<std::optional<DecalGeometryCache>,66U> decal_geometries_;
    std::uint64_t decal_frame_serial_{};
    OpenGlDecalRendererStatistics decal_statistics_;
    GlObject flash_program_;
    GLint flash_shape_{-1};
    GLint flash_head_taper_{-1};
    GlObject solid_line_program_,solid_line_array_,solid_line_buffer_;
    GLint solid_line_matrix_{-1},solid_line_color_{-1},solid_line_eye_{-1},solid_line_gain_{-1};
    GlObject flash_array_;
    GlObject flash_buffer_;
    GLint flash_matrix_{-1}, flash_color_{-1};
    GlObject flash_batch_program_,flash_batch_array_,flash_batch_buffer_;
    GLint flash_batch_matrix_{-1};
    bool flash_batch_gpu_orphaned_this_frame_{};
    std::size_t flash_batch_gpu_cursor_{};
    std::size_t flash_batch_frame_vertex_capacity_{};
    std::vector<FlashBatchVertex> flash_batch_vertices_;
    GlObject hud_program_;
    GlObject hud_array_;
    GlObject hud_buffer_;
    GlObject screen_overlay_array_,screen_overlay_buffer_;
    GlObject screen_ui_array_,screen_ui_buffer_;
    struct ScreenUiTexture {std::shared_ptr<const ScreenUiImagePixels> pixels;GlObject texture;};
    std::vector<ScreenUiTexture> screen_ui_textures_;
    std::vector<float> hud_vertices_;
    std::vector<HudBatch> hud_batches_;
    std::optional<RenderBasicHud> hud_commands_;
    RenderExtent hud_extent_{};
    GlObject hud_sprite_program_,hud_sprite_array_,hud_sprite_buffer_;
    GLint hud_sprite_color_{-1},hud_sprite_masked_{-1};
    std::vector<HudGpuTexture> hud_textures_;
    std::uint64_t hud_cache_clock_{};
    OpenGlHudRendererStatistics hud_statistics_{};
    std::shared_ptr<const assets::SkyboxAsset> sky_asset_;
    GlObject sky_texture_,sky_program_,sky_array_;
    GLint sky_forward_{-1},sky_right_{-1},sky_up_{-1},sky_scale_{-1};
    detail::OpenGlEntityRendererBackend entity_renderer_;
    std::optional<GpuWorldResources> resources_;
    std::optional<GpuSceneResources> scene_resources_;
    SkyPreparedSubmission backdrop_submission_;
    std::optional<world_visibility::WorldVisibilitySceneIdentity>
        visibility_scene_identity_;
    std::optional<world_visibility::WorldVisibilityResultSignature>
        visibility_result_signature_;
    std::uint64_t visibility_revision_{0U};
    OpenGlWorldRendererStatistics statistics_{};
    std::array<GLuint,8> gpu_queries_{};
    std::array<std::uint64_t,8> gpu_frames_{};
    std::array<bool,8> gpu_pending_{};
    static constexpr std::size_t kGpuPassBoundaryLimit=16U;
    struct GpuPassFrame {
        std::array<OpenGlGpuPass,kGpuPassBoundaryLimit> passes{};
        std::array<std::optional<OpenGlWorldGpuSubpass>,kGpuPassBoundaryLimit> world_subpasses{};
        std::size_t count{};
        bool invalid{},disjoint{};
    };
    std::array<GLuint,8U*kGpuPassBoundaryLimit> gpu_pass_queries_{};
    std::array<GpuPassFrame,8> gpu_pass_frames_{};
    std::optional<std::size_t> gpu_active_;
    OpenGlGpuTimingStatistics gpu_timing_;
    bool gpu_timing_failed_{};
    bool gpu_timing_unsupported_{};
    inline static std::atomic_uint64_t next_startup_context_{1U};
    const std::uint64_t startup_context_{next_startup_context_.fetch_add(1U)};
    GLint context_profile_mask_{},context_major_{},context_minor_{};
    bool context_profile_query_checked_{};
    unsigned startup_frame_{};
    bool startup_checks_{};
};

OpenGlRenderer::OpenGlRenderer()
{
    const int loaded_version = gladLoaderLoadGL();
    if (loaded_version == 0) {
        throw std::runtime_error{"glad2 failed to load OpenGL functions"};
    }
    loader_initialized_ = true;

    try {
        const int major = GLAD_VERSION_MAJOR(loaded_version);
        const int minor = GLAD_VERSION_MINOR(loaded_version);
        if (major < 3 || (major == 3 && minor < 3)) {
            throw std::runtime_error{
                "OpenGL 3.3 Core is required, but glad2 loaded OpenGL " +
                std::to_string(major) + '.' + std::to_string(minor)};
        }
        require_no_gl_error(
            OpenGlRendererErrorCode::gl_operation_failed,
            "OpenGL renderer initialization");

        GLint context_profile_mask = 0;
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &context_profile_mask);
        require_no_gl_error(
            OpenGlRendererErrorCode::gl_operation_failed,
            "OpenGL context profile query");
        if ((context_profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
            throw std::runtime_error{
                "OpenGL 3.3 Core is required, but the current context is not "
                "a Core profile"};
        }

        information_.vendor = open_gl_string(GL_VENDOR);
        information_.device = open_gl_string(GL_RENDERER);
        information_.version = open_gl_string(GL_VERSION);
        implementation_ = std::make_unique<Implementation>();
        implementation_->record_startup_context(context_profile_mask);
    } catch (...) {
        implementation_.reset();
        gladLoaderUnloadGL();
        loader_initialized_ = false;
        throw;
    }
}

OpenGlRenderer::~OpenGlRenderer() noexcept
{
    // Release every object while the caller-owned OpenGL context is still
    // current, then release the loader. SdlWindow is deliberately declared
    // before OpenGlRenderer by both application runtimes.
    if (implementation_) {
        implementation_->release_entity_resources();
        implementation_->release_world_resources();
        implementation_.reset();
    }
    if (loader_initialized_) {
        gladLoaderUnloadGL();
        loader_initialized_ = false;
    }
}

const RendererInfo& OpenGlRenderer::information() const noexcept
{
    return information_;
}

void OpenGlRenderer::reset_session_resources()
{
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Session reset prior GL state");
    implementation_->reset_session_resources();
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Session resource reset");
}

void OpenGlRenderer::render_with_gpu_timing(const RenderScene& scene,const RenderExtent extent,
    const std::uint64_t frame_number,const bool capture_static_world_receipt)
{
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"GPU timing prior GL state");
    const bool active=implementation_->begin_gpu_timing(frame_number);
    static_world_receipt_frame_=capture_static_world_receipt ?
        std::optional{frame_number} : std::nullopt;
    try {
        render(scene,extent);
        if(active)require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "Rendered frame before GPU timing completion");
    } catch(...) {
        static_world_receipt_frame_.reset();
        implementation_->statistics().last_static_world_frame.reset();
        if(active)implementation_->end_gpu_timing(false);
        throw;
    }
    static_world_receipt_frame_.reset();
    if(active)implementation_->end_gpu_timing();
}
std::optional<OpenGlGpuFrameTiming> OpenGlRenderer::poll_gpu_timing() {
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"GPU timing poll prior GL state");
    return implementation_->poll_gpu_timing();
}
const OpenGlGpuTimingStatistics& OpenGlRenderer::gpu_timing_statistics() const noexcept {
    return implementation_->gpu_timing_statistics();
}

void OpenGlRenderer::render(const RenderScene& scene, const RenderExtent extent)
{
    implementation_->begin_startup_frame();
    if(!core::valid_image_adjustments(scene.image_adjustments))
        throw std::invalid_argument{"Image adjustments are outside the finite bounded contract"};
    if(!valid_render_screen_effects(scene.screen_effects))
        throw std::invalid_argument{"Screen effects are outside the finite bounded contract"};
    auto view_camera=scene.camera;
    if(scene.screen_effects.roll_degrees!=0.0 ||
        std::any_of(scene.screen_effects.translation.begin(),scene.screen_effects.translation.end(),
            [](float value){return value!=0.0F;})) {
        const auto adjusted=apply_screen_effects(scene.camera,scene.screen_effects);
        if(!adjusted)fail(OpenGlRendererErrorCode::camera_invalid,"Screen effect camera is invalid");
        view_camera=*adjusted;
    }
    if(scene.requested_msaa_samples!=0 && scene.requested_msaa_samples!=2 &&
       scene.requested_msaa_samples!=4 && scene.requested_msaa_samples!=8)
        throw std::invalid_argument{"Scene MSAA request must be Off, 2, 4 or 8"};
    implementation_->begin_decals();
    auto& statistics = implementation_->statistics();
    statistics.last_static_world_frame.reset();
    const int width = std::max(extent.width, 0);
    const int height = std::max(extent.height, 0);
    statistics.last_extent = RenderExtent{width, height};
    statistics.world_present = scene.static_world.has_value();
    statistics.scene_present = scene.static_world &&
        scene.static_world->scene_package != nullptr;
    statistics.visible_world_surface_count = 0U;
    statistics.visible_brush_instance_count = 0U;

    if (scene.dynamic_entities &&
        (!scene.dynamic_entities->package ||
            !scene.dynamic_entities->frame)) {
        fail(OpenGlRendererErrorCode::invalid_entity_scene,
            "Dynamic entity scene requires both a package and a frame");
    }
    if (scene.dynamic_entities &&
        scene.dynamic_entities->frame->scene_package_identity() !=
            entity_render::EntityRenderResourceIdentity{
                scene.dynamic_entities->package->resource_id(),
                scene.dynamic_entities->package->resource_revision()}) {
        fail(OpenGlRendererErrorCode::invalid_entity_scene,
            "Dynamic entity frame does not belong to its scene package");
    }
    for(const auto* sprites:{&scene.first_person_muzzle_entities,&scene.world_muzzle_entities})
        if(*sprites && (!(*sprites)->package || !(*sprites)->frame ||
            (*sprites)->frame->scene_package_identity()!=entity_render::EntityRenderResourceIdentity{
                (*sprites)->package->resource_id(),(*sprites)->package->resource_revision()}))
            fail(OpenGlRendererErrorCode::invalid_world_package,"Muzzle sprite package/frame identity is invalid");
    if (scene.first_person_entities &&
        (!scene.first_person_entities->package ||
         !scene.first_person_entities->frame ||
         scene.first_person_entities->frame->scene_package_identity() !=
             entity_render::EntityRenderResourceIdentity{
                 scene.first_person_entities->package->resource_id(),
                 scene.first_person_entities->package->resource_revision()})) {
        fail(OpenGlRendererErrorCode::invalid_entity_scene,
            "First-person Studio frame does not match its asset package");
    }
    if (scene.transient_world_entities &&
        (!scene.transient_world_entities->package ||
         !scene.transient_world_entities->frame ||
         scene.transient_world_entities->frame->scene_package_identity() !=
             entity_render::EntityRenderResourceIdentity{
                 scene.transient_world_entities->package->resource_id(),
                 scene.transient_world_entities->package->resource_revision()})) {
        fail(OpenGlRendererErrorCode::invalid_entity_scene,
            "Transient world entity package/frame identity is invalid");
    }
    if (scene.dynamic_entities &&
        scene.dynamic_entities->package->world_scene_association()) {
        const auto association =
            *scene.dynamic_entities->package->world_scene_association();
        if (!scene.static_world || !scene.static_world->scene_package ||
            association.resource_id !=
                scene.static_world->scene_package->resource_id() ||
            association.revision !=
                scene.static_world->scene_package->resource_revision()) {
            fail(OpenGlRendererErrorCode::invalid_entity_scene,
                "Dynamic entity scene does not match its associated world scene");
        }
    }
    if(scene.transient_sprite_entities &&
       (!scene.transient_sprite_entities->package || !scene.transient_sprite_entities->frame ||
        scene.transient_sprite_entities->frame->scene_package_identity()!=
          entity_render::EntityRenderResourceIdentity{scene.transient_sprite_entities->package->resource_id(),
              scene.transient_sprite_entities->package->resource_revision()}))
        fail(OpenGlRendererErrorCode::invalid_entity_scene,"Transient sprite package/frame identity is invalid");
    if(scene.follow_trail_entities &&
       (!scene.follow_trail_entities->package || !scene.follow_trail_entities->frame ||
        scene.follow_trail_entities->frame->scene_package_identity()!=
          entity_render::EntityRenderResourceIdentity{scene.follow_trail_entities->package->resource_id(),
              scene.follow_trail_entities->package->resource_revision()}))
        fail(OpenGlRendererErrorCode::invalid_entity_scene,"Follow trail package/frame identity is invalid");

    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Before scene target selection");
    const detail::SceneTargetFrame target_frame;
    implementation_->scene_target.begin(scene.requested_msaa_samples,{width,height},target_frame);
    glViewport(0, 0, width, height);
    implementation_->scene_target.observe_viewport();
    auto background = scene.clear_color;
    if (scene.static_world && scene.static_world->package &&
        scene.static_world->package->flat_sky_background()) {
        const auto& rgb = scene.static_world->package->flat_sky_background()->rgb;
        background = {rgb[0U] / 255.0F, rgb[1U] / 255.0F, rgb[2U] / 255.0F, 1.0F};
    }
    glClearColor(
        background.red,
        background.green,
        background.blue,
        background.alpha);
    background_sky_stats.last_frame_world_draws=0;
    background_sky_stats.world_draw_order_hash=14695981039346656037ULL;
    background_sky_stats.last_frame_clear_color={background.red,background.green,background.blue,background.alpha};
    glClearDepth(1.0);
    // Observe eligibility BEFORE the clear: a scissored/masked clear must not
    // be treated as a fresh depth buffer. All checks are inside submission CPU.
    background_sky_render_started=true;
    const bool depth_requested=background_sky_policy==BackgroundSkyPolicy::depth;
    const bool omit_fullscreen=background_sky_policy==BackgroundSkyPolicy::no_background ||
        background_sky_policy==BackgroundSkyPolicy::no_sky_color;
    // The default/original path does not enter any new per-frame bounds,
    // resource, far-depth or GL-state guard. Opt-in keeps every existing guard.
    const auto backdrop_fallback=depth_requested
        ? implementation_->backdrop_fallback(scene,view_camera,{width,height},static_world_receipt_frame_) : 0U;
    const bool late_backdrop=depth_requested && backdrop_fallback==0;
    ++background_sky_stats.frames;
    if(late_backdrop)++background_sky_stats.late_frames;
    else {
        ++background_sky_stats.early_frames;
        if(background_sky_policy==BackgroundSkyPolicy::original)++background_sky_stats.original_frames;
        else if(depth_requested) {
            background_sky_stats.fallback_bits|=backdrop_fallback;
            for(std::size_t bit=0;bit<background_sky_stats.fallback_counts.size();++bit)
                if(backdrop_fallback&(std::uint64_t{1}<<bit))++background_sky_stats.fallback_counts[bit];
        }
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    require_no_gl_error(
        OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL frame clear");

    if (width == 0 || height == 0) {
        ++statistics.rendered_frame_count;
        return;
    }
    if(!late_backdrop)implementation_->gpu_world_subpass_boundary(OpenGlWorldGpuSubpass::sky);
    const auto sky_texture=implementation_->prepare_sky(scene.skybox);
    implementation_->startup_boundary("sky_prepared");
    if(omit_fullscreen && sky_texture)++background_sky_stats.fullscreen_sky_skipped_frames;
    if(!late_backdrop && !omit_fullscreen){implementation_->render_sky(sky_texture,view_camera,{width,height});
        implementation_->startup_boundary("sky_drawn");}
    // Unit2 and seamless cube filtering are owned for this frame. Original
    // finite SKY keeps its historical depth; the explicit correction policy
    // omits only supported static model0 SKY at the shared range submitter.
    struct SkyDrawState {
        GLint active{},binding{},sampler{};GLboolean seamless{};
        SkyDrawState() {
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE2);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&binding);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
            seamless=glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            glBindSampler(2,0);glActiveTexture(active);
        }
        ~SkyDrawState() {
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_CUBE_MAP,static_cast<GLuint>(binding));
            glBindSampler(2,static_cast<GLuint>(sampler));glActiveTexture(active);
            if(!seamless)glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        }
    } sky_draw_state;
    implementation_->startup_boundary("SkyDrawState_sampler_snapshot_GL_SAMPLER_BINDING_0x8919");
    if (!scene.static_world) {
        implementation_->release_world_resources();
        const auto additional=std::min<std::size_t>(scene.additional_world_decals.size(),64U);
        implementation_->reject_unavailable_world_decals(additional+
            static_cast<std::size_t>(scene.world_decals.has_value())+
            static_cast<std::size_t>(scene.secondary_world_decals.has_value()));
        if(scene.additional_world_decals.size()>additional)
            implementation_->reject_decal_batches(scene.additional_world_decals.size()-additional);
        implementation_->gpu_pass_boundary(OpenGlGpuPass::studio_viewmodel);
        if (scene.dynamic_entities) {
            const auto matrix = camera_view_projection(
                view_camera, RenderExtent{width, height});
            if (!matrix || !matrix.matrix) {
                fail(OpenGlRendererErrorCode::camera_invalid,
                    "Dynamic-entity camera is invalid");
            }
            implementation_->render_entities(
                *scene.dynamic_entities, view_camera, *matrix.matrix,
                scene.transient_world_light,scene.effect_lights());
        } else if (!scene.first_person_entities && !scene.transient_world_entities && !scene.transient_sprite_entities && !scene.follow_trail_entities && !scene.world_muzzle_entities && !scene.first_person_muzzle_entities) {
            implementation_->release_entity_resources();
        }
        if (scene.transient_world_entities) {
            const auto matrix=camera_view_projection(view_camera,RenderExtent{width,height});
            if (!matrix || !matrix.matrix) fail(OpenGlRendererErrorCode::camera_invalid,
                "Transient entity camera is invalid");
            implementation_->render_entities(*scene.transient_world_entities,
                view_camera,*matrix.matrix,scene.transient_world_light,scene.effect_lights());
        }
        implementation_->gpu_pass_boundary(OpenGlGpuPass::effects_decals);
        if(scene.transient_sprite_entities) {
            const auto matrix=camera_view_projection(view_camera,RenderExtent{width,height});
            if(!matrix || !matrix.matrix) fail(OpenGlRendererErrorCode::camera_invalid,"Transient sprite camera is invalid");
            implementation_->render_entities(*scene.transient_sprite_entities,
                view_camera,*matrix.matrix,scene.transient_world_light,scene.effect_lights());
        }
        if(scene.follow_trail_entities) {
            const auto matrix=camera_view_projection(view_camera,RenderExtent{width,height});
            if(!matrix || !matrix.matrix) fail(OpenGlRendererErrorCode::camera_invalid,"Follow trail camera is invalid");
            implementation_->render_entities(*scene.follow_trail_entities,
                view_camera,*matrix.matrix,scene.transient_world_light,scene.effect_lights());
        }
        if(scene.world_muzzle_entities) {
            const auto matrix=camera_view_projection(view_camera,RenderExtent{width,height});
            if(!matrix || !matrix.matrix)fail(OpenGlRendererErrorCode::camera_invalid,"Muzzle sprite camera is invalid");
            implementation_->render_entities(*scene.world_muzzle_entities,view_camera,*matrix.matrix);
        }
        if (scene.world_flash_count || scene.world_particle_count || scene.world_line_count) {
            const auto matrix=camera_view_projection(view_camera,RenderExtent{width,height});
            if(matrix && matrix.matrix) implementation_->render_world_flashes(scene,*matrix.matrix,view_camera);
        }
        if (scene.first_person_entities) {
            implementation_->gpu_pass_boundary(OpenGlGpuPass::studio_viewmodel);
            const auto first_person_camera =
                camera_local_first_person_camera(scene.camera);
            const auto matrix = camera_view_projection(
                first_person_camera, RenderExtent{width, height});
            if (!matrix || !matrix.matrix)
                fail(OpenGlRendererErrorCode::camera_invalid,
                    "First-person camera is invalid");
            glClear(GL_DEPTH_BUFFER_BIT);
            const auto light_direction=camera_local_direction(scene.camera,{0.0F,0.0F,-1.0F});
            if (!light_direction)
                fail(OpenGlRendererErrorCode::camera_invalid,"First-person lighting camera is invalid");
            implementation_->render_entities(*scene.first_person_entities,
                first_person_camera, *matrix.matrix,scene.transient_first_person_light,{},*light_direction);
            implementation_->gpu_pass_boundary(OpenGlGpuPass::effects_decals);
            if(scene.first_person_muzzle_entities)
                implementation_->render_entities(*scene.first_person_muzzle_entities,first_person_camera,*matrix.matrix);
            if (scene.first_person_flash)
                implementation_->render_flash(*scene.first_person_flash,*matrix.matrix);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
        }
        implementation_->gpu_pass_boundary(OpenGlGpuPass::msaa_resolve);
        implementation_->startup_boundary("before_scene_resolve");
        const auto resolved_color=implementation_->scene_target.resolve(
            target_frame,scene.image_adjustments!=core::ImageAdjustments{});
        require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Native scene resolve");
        implementation_->gpu_pass_boundary(OpenGlGpuPass::image_adjustments);
        implementation_->adjust_image(scene.image_adjustments,{width,height},resolved_color);
        implementation_->gpu_pass_boundary(OpenGlGpuPass::hud_console);
        implementation_->render_screen_overlay(scene.screen_effects);
        if (scene.basic_hud)
            implementation_->render_hud(*scene.basic_hud, {width, height},scene.hud_sprite_bindings);
        if(scene.screen_ui)implementation_->render_screen_ui(*scene.screen_ui,{width,height});
        implementation_->startup_boundary("HUD_console_state_restored");
        ++statistics.rendered_frame_count;
        return;
    }
    implementation_->gpu_world_subpass_boundary(OpenGlWorldGpuSubpass::static_world);
    if (!scene.static_world->package) {
        fail(OpenGlRendererErrorCode::invalid_world_package,
            "RenderScene contains a static-world entry without a package");
    }
    if (scene.static_world->light_style_policy !=
        RenderBaselineLightStylePolicy::source_slot_zero) {
        fail(OpenGlRendererErrorCode::invalid_world_package,
            "Static-world scene requested an unsupported light-style policy");
    }
    if (scene.static_world->cull_mode != RenderCullMode::none &&
        scene.static_world->cull_mode != RenderCullMode::back) {
        fail(OpenGlRendererErrorCode::invalid_world_package,
            "Static-world scene requested an unsupported culling mode");
    }
    if (scene.static_world->visible_draw_list &&
        !scene.static_world->scene_package) {
        fail(OpenGlRendererErrorCode::invalid_world_package,
            "Visible draw commands require an immutable world scene package");
    }
    if (scene.static_world->scene_package &&
        scene.static_world->scene_package->world_package() !=
            scene.static_world->package) {
        fail(OpenGlRendererErrorCode::invalid_world_package,
            "RenderScene world and scene packages do not share one resource");
    }
    if (scene.static_world->visible_draw_list) {
        const auto expected_scene_identity = scene.static_world
            ->scene_package->visibility_scene_identity();
        const auto draw_scene_identity =
            scene.static_world->visible_draw_list->scene_identity();
        const auto draw_result_signature =
            scene.static_world->visible_draw_list->result_signature();
        if (!scene.static_world->visibility_summary ||
            scene.static_world->visibility_summary->revision == 0U ||
            scene.static_world->visibility_summary->revision !=
                scene.static_world->visible_draw_list->visibility_revision() ||
            scene.static_world->visibility_summary->scene_resource_id !=
                expected_scene_identity.resource_id ||
            scene.static_world->visibility_summary->scene_resource_revision !=
                expected_scene_identity.revision ||
            scene.static_world->visibility_summary
                    ->visibility_input_signature !=
                expected_scene_identity.visibility_input_signature ||
            scene.static_world->visibility_summary->draw_input_signature !=
                expected_scene_identity.draw_input_signature ||
            scene.static_world->visibility_summary->result_signature_first !=
                draw_result_signature.first ||
            scene.static_world->visibility_summary->result_signature_second !=
                draw_result_signature.second ||
            !draw_result_signature ||
            draw_scene_identity != expected_scene_identity) {
            fail(OpenGlRendererErrorCode::draw_range_invalid,
                "Visible draw-list identity/revision does not match its scene");
        }
        statistics.visible_world_surface_count = static_cast<std::uint64_t>(
            scene.static_world->visibility_summary
                ->visible_world_surface_count);
        statistics.visible_brush_instance_count = static_cast<std::uint64_t>(
            scene.static_world->visibility_summary
                ->visible_brush_instance_count);
    } else {
        implementation_->clear_visibility_tracking();
    }

    const auto matrix = camera_view_projection(
        view_camera,
        RenderExtent{width, height});
    if (!matrix || !matrix.matrix) {
        const auto detail = matrix.error
                                ? std::string{renderer::to_string(*matrix.error)}
                                : std::string{"unknown_camera_error"};
        fail(OpenGlRendererErrorCode::camera_invalid,
            "Static-world camera is invalid: " + detail);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (scene.static_world->cull_mode == RenderCullMode::back) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }

    FrameDrawCounters frame;
    const auto effect_lights=scene.effect_lights();
    const bool no_positive_transient_light=
        !scene.transient_world_light ||
        !valid_render_point_light(*scene.transient_world_light) ||
        scene.transient_world_light->intensity<=0.0F;
    FrameDrawState draw_state{scene.transient_world_light,effect_lights,
        sky_texture,view_camera.position,background};
    draw_state.use_constant_finite_sky=background_sky_policy==BackgroundSkyPolicy::no_sky_color;
    draw_state.use_no_dynamic_light_world_program=
        no_positive_transient_light && effect_lights.empty();
    draw_state.use_full_eight_effect_lights = effect_lights.size() == 8U &&
        std::ranges::all_of(effect_lights, [](const auto& light) {
            return valid_render_point_light(light) && light.intensity > 0.0F;
        });
    const auto& prepared_submission=implementation_->backdrop_submission();
    if(depth_requested && prepared_submission.ready) {
        // The depth guard and these draws consume one plan: no second frustum,
        // PVS, block selection, model search, or MVP multiplication is run here.
        if(scene.static_world->visible_draw_list)
            implementation_->note_visibility(scene.static_world->visible_draw_list->scene_identity(),
                scene.static_world->visibility_summary->revision,
                scene.static_world->visible_draw_list->result_signature());
        const auto draw=[&](const SkyPreparedRange& range) {
            draw_package_range(*range.resources,*range.resources->package_instance,
                range.first_index,range.index_count,range.material_index,range.mvp,range.transform,
                range.brush,frame,draw_state,range.alternate_texture);
        };
        for(std::size_t i=0;i<prepared_submission.static_range_count;++i)draw(prepared_submission.ranges[i]);
        if(scene.static_world->visible_draw_list)
            statistics.rendered_command_count+=scene.static_world->visible_draw_list->commands().size();
        if(prepared_submission.static_receipt) {
            auto receipt=*prepared_submission.static_receipt;
            receipt.api_draws=frame.draw_calls-frame.brush_draw_calls;
            if(receipt.path!=OpenGlStaticWorldPath::visible_commands)receipt.submitted_triangles=frame.triangles;
            statistics.last_static_world_frame=std::move(receipt);
        }
        if(prepared_submission.has_runtime)
            implementation_->gpu_world_subpass_boundary(OpenGlWorldGpuSubpass::runtime_brushes);
        for(std::size_t i=prepared_submission.static_range_count;i<prepared_submission.ranges.size();++i)
            draw(prepared_submission.ranges[i]);
    } else if (scene.static_world->scene_package) {
        auto& resources = implementation_->ensure_scene_resources(
            scene.static_world->scene_package);
        if (scene.static_world->visible_draw_list) {
            implementation_->note_visibility(
                scene.static_world->visible_draw_list->scene_identity(),
                scene.static_world->visibility_summary->revision,
                scene.static_world->visible_draw_list->result_signature());
        }
        const auto& world_package =
            *scene.static_world->scene_package->world_package();
        std::optional<OpenGlStaticWorldFrameReceipt> static_receipt;
        if (static_world_receipt_frame_) {
            static_receipt.emplace();
            static_receipt->frame_number=*static_world_receipt_frame_;
            static_receipt->resource_id=world_package.resource_id();
            static_receipt->resource_revision=world_package.resource_revision();
        }
        const auto static_draws_before=frame.draw_calls;
        const auto static_brush_draws_before=frame.brush_draw_calls;
        const auto static_triangles_before=frame.triangles;
        if (scene.static_world->visible_draw_list) {
            if (static_receipt)
                static_receipt->path=OpenGlStaticWorldPath::visible_commands;
            const auto commands =
                scene.static_world->visible_draw_list->commands();
            for (const auto& command : commands) {
                const bool brush = command.object_kind ==
                    world_visibility::WorldVisibleObjectKind::
                        brush_instance_surface;
                if (command.object_kind !=
                        world_visibility::WorldVisibleObjectKind::world_surface &&
                    !brush) {
                    fail(OpenGlRendererErrorCode::draw_range_invalid,
                        "Visible command contains an unknown object kind");
                }
                const world_render::WorldRenderPackage* package =
                    &world_package;
                GpuWorldResources* package_resources = &resources.world;
                if (brush) {
                    const auto& brush_package = scene.static_world
                        ->scene_package->brush_library().render_package();
                    if (!brush_package || !resources.brushes ||
                        !command.source_model_index ||
                        !command.source_instance_index) {
                        fail(OpenGlRendererErrorCode::draw_range_invalid,
                            "Brush draw command has no cached model resource");
                    }
                    package = brush_package.get();
                    package_resources = &*resources.brushes;
                } else if (command.source_model_index ||
                           command.source_instance_index) {
                    fail(OpenGlRendererErrorCode::draw_range_invalid,
                        "World draw command contains brush identity");
                }
                if (!renderer::is_finite(command.model_transform)) {
                    fail(OpenGlRendererErrorCode::draw_range_invalid,
                        "Visible command contains a non-finite model transform");
                }
                const auto materials = package->materials();
                if (command.render_material_index >= materials.size()) {
                    fail(OpenGlRendererErrorCode::draw_range_invalid,
                        "Visible command material is outside its package");
                }
                const auto& material =
                    materials[command.render_material_index];
                if (command.alpha_mode !=
                        material.base_texture_alpha_mode ||
                    command.lightmap_mode != material.lightmap_mode ||
                    command.lightmap_atlas_page_index !=
                        material.lightmap_atlas_page_index) {
                    fail(OpenGlRendererErrorCode::draw_range_invalid,
                        "Visible command material profile does not match package");
                }
                const auto mvp = renderer::multiply(
                    *matrix.matrix, command.model_transform);
                draw_package_range(
                    *package_resources,
                    *package,
                    command.first_index,
                    command.index_count,
                    command.render_material_index,
                    mvp,
                    command.model_transform,
                    brush,
                    frame,draw_state);
            }
            statistics.rendered_command_count +=
                static_cast<std::uint64_t>(commands.size());
        } else {
            select_static_world_ranges(world_package,*matrix.matrix,statistics,static_receipt,
                [&](std::uint32_t first,std::uint32_t count,std::size_t material){
                    draw_package_range(resources.world,world_package,first,count,material,
                        *matrix.matrix,RenderMatrix4{},false,frame,draw_state);
                });
        }
        if (static_receipt) {
            static_receipt->api_draws=frame.draw_calls-static_draws_before-
                (frame.brush_draw_calls-static_brush_draws_before);
            if (static_receipt->path!=OpenGlStaticWorldPath::visible_commands)
                static_receipt->submitted_triangles=
                    frame.triangles-static_triangles_before;
            statistics.last_static_world_frame=std::move(static_receipt);
        }
        if (scene.static_world->runtime_brushes) {
            implementation_->gpu_world_subpass_boundary(
                OpenGlWorldGpuSubpass::runtime_brushes);
            const auto& runtime=*scene.static_world->runtime_brushes;
            const auto& map=*scene.static_world->scene_package;
            const auto& library=map.brush_library();
            select_runtime_brush_ranges(runtime,map,view_camera,*matrix.matrix,statistics,
                [&](const auto& surface,const auto& instance,const auto& mvp){
                    if(!resources.brushes || !library.render_package())
                        fail(OpenGlRendererErrorCode::draw_range_invalid,"Runtime brush library absent");
                    GLuint alternate=0U;
                    const auto mapping=library.alternate_by_material();
                    if(instance.alternate_texture && !mapping.empty() && mapping[surface.render_material_index])
                        alternate=resources.brush_alternate_textures[*mapping[surface.render_material_index]].name();
                    draw_package_range(*resources.brushes,*library.render_package(),
                        surface.first_index,surface.index_count,surface.render_material_index,
                        mvp,instance.model_transform,true,frame,draw_state,alternate);
                });
        }
    } else {
        auto& resources = implementation_->ensure_resources(
            scene.static_world->package);
        const auto& package = *scene.static_world->package;
        for (const auto& batch : package.draw_batches()) {
            draw_package_range(
                resources,
                package,
                batch.first_index,
                batch.index_count,
                batch.render_material_index,
                *matrix.matrix,
                RenderMatrix4{},
                false,
                frame,draw_state);
        }
    }

    // The former per-range path always left unit 1 active. Preserve that
    // observable state for the following passes without repeating the call
    // for every draw command.
    glActiveTexture(GL_TEXTURE1);
    require_no_gl_error(
        OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL static-world draw");
    glBindVertexArray(0U);
    glUseProgram(0U);

    // FrameDrawState has ended: no later draw consumes its cached bindings.
    // Finite sky geometry has already run in its ORIGINAL order/depth. Decals
    // and every blend-dependent pass below now see the completed background.
    if(late_backdrop) {
        implementation_->gpu_world_subpass_boundary(OpenGlWorldGpuSubpass::sky);
        implementation_->render_sky(sky_texture,view_camera,{width,height},true);
        implementation_->startup_boundary("sky_drawn");
    }
    implementation_->gpu_pass_boundary(OpenGlGpuPass::effects_decals);
    if (scene.world_decals)
        implementation_->render_decals(*scene.world_decals,*matrix.matrix,frame);
    if (scene.secondary_world_decals)
        implementation_->render_decals(*scene.secondary_world_decals,*matrix.matrix,frame);
    const auto decal_batches=std::min<std::size_t>(scene.additional_world_decals.size(),64U);
    for(std::size_t i=0;i<decal_batches;++i)
        implementation_->render_decals(scene.additional_world_decals[i],*matrix.matrix,frame);
    if(scene.additional_world_decals.size()>decal_batches)
        implementation_->reject_decal_batches(scene.additional_world_decals.size()-decal_batches);

    implementation_->gpu_pass_boundary(OpenGlGpuPass::studio_viewmodel);
    if (scene.dynamic_entities) {
        implementation_->render_entities(
            *scene.dynamic_entities, view_camera, *matrix.matrix,
            scene.transient_world_light,scene.effect_lights());
    } else if (!scene.first_person_entities && !scene.transient_world_entities && !scene.transient_sprite_entities && !scene.follow_trail_entities && !scene.world_muzzle_entities && !scene.first_person_muzzle_entities) {
        implementation_->release_entity_resources();
    }
    if (scene.transient_world_entities)
        implementation_->render_entities(*scene.transient_world_entities,
            view_camera,*matrix.matrix,scene.transient_world_light,scene.effect_lights());
    implementation_->gpu_pass_boundary(OpenGlGpuPass::effects_decals);
    if(scene.transient_sprite_entities)
        implementation_->render_entities(*scene.transient_sprite_entities,
            view_camera,*matrix.matrix,scene.transient_world_light,scene.effect_lights());
    if(scene.follow_trail_entities)
        implementation_->render_entities(*scene.follow_trail_entities,
            view_camera,*matrix.matrix,scene.transient_world_light,scene.effect_lights());

    if(scene.world_muzzle_entities)
        implementation_->render_entities(*scene.world_muzzle_entities,view_camera,*matrix.matrix);
    implementation_->render_world_flashes(scene,*matrix.matrix,view_camera);

    if (scene.first_person_entities) {
        implementation_->gpu_pass_boundary(OpenGlGpuPass::studio_viewmodel);
        // The world retains its color; this pass receives independent depth
        // so near walls cannot erase the first-person Studio model. The
        // model and its view are camera-local; the world view/projection is
        // never applied to the weapon. Internal model depth remains active.
        const auto first_person_camera =
            camera_local_first_person_camera(scene.camera);
        const auto first_person_matrix = camera_view_projection(
            first_person_camera, RenderExtent{width, height});
        if (!first_person_matrix || !first_person_matrix.matrix)
            fail(OpenGlRendererErrorCode::camera_invalid,
                "First-person camera is invalid");
        glClear(GL_DEPTH_BUFFER_BIT);
        const auto light_direction=camera_local_direction(scene.camera,{0.0F,0.0F,-1.0F});
        if (!light_direction)
            fail(OpenGlRendererErrorCode::camera_invalid,"First-person lighting camera is invalid");
        implementation_->render_entities(*scene.first_person_entities,
            first_person_camera, *first_person_matrix.matrix,
            scene.transient_first_person_light,{},*light_direction);
        implementation_->gpu_pass_boundary(OpenGlGpuPass::effects_decals);
        if(scene.first_person_muzzle_entities)
            implementation_->render_entities(*scene.first_person_muzzle_entities,first_person_camera,*first_person_matrix.matrix);
        if (scene.first_person_flash)
            implementation_->render_flash(*scene.first_person_flash,*first_person_matrix.matrix);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    }
    implementation_->gpu_pass_boundary(OpenGlGpuPass::msaa_resolve);
    implementation_->startup_boundary("before_scene_resolve");
    const auto resolved_color=implementation_->scene_target.resolve(
        target_frame,scene.image_adjustments!=core::ImageAdjustments{});
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"Native scene resolve");
    implementation_->gpu_pass_boundary(OpenGlGpuPass::image_adjustments);
    implementation_->adjust_image(scene.image_adjustments,{width,height},resolved_color);
    implementation_->gpu_pass_boundary(OpenGlGpuPass::hud_console);
    implementation_->render_screen_overlay(scene.screen_effects);
    if (scene.basic_hud) {
        implementation_->render_hud(*scene.basic_hud, {width, height},scene.hud_sprite_bindings);
        if (scene.static_world->cull_mode == RenderCullMode::back) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
        }
    }

    if(scene.screen_ui)implementation_->render_screen_ui(*scene.screen_ui,{width,height});
    implementation_->startup_boundary("HUD_console_state_restored");
    ++statistics.rendered_frame_count;
    statistics.draw_call_count += frame.draw_calls;
    statistics.brush_draw_call_count += frame.brush_draw_calls;
    statistics.triangle_count += frame.triangles;
    statistics.base_texture_bind_count += frame.base_texture_binds;
    statistics.lightmap_bind_count += frame.lightmap_binds;
    statistics.program_bind_count += frame.program_binds;
    statistics.vertex_array_bind_count += frame.vertex_array_binds;
    statistics.sky_texture_bind_count += frame.sky_texture_binds;
    statistics.material_uniform_upload_count += frame.material_uniform_uploads;
    statistics.transform_upload_count += frame.transform_uploads;
    statistics.camera_upload_count += frame.camera_uploads;
    statistics.light_upload_count += frame.light_uploads;
}

std::string_view to_string(const OpenGlGpuTimingStatus status) noexcept {
    switch(status) {
    case OpenGlGpuTimingStatus::not_observed:return "not_observed";
    case OpenGlGpuTimingStatus::pending:return "pending";
    case OpenGlGpuTimingStatus::ready:return "ready";
    case OpenGlGpuTimingStatus::unsupported:return "unsupported";
    case OpenGlGpuTimingStatus::disjoint:return "disjoint";
    case OpenGlGpuTimingStatus::invalid:return "invalid";
    case OpenGlGpuTimingStatus::capacity_dropped:return "capacity_dropped";
    }
    return "invalid";
}

std::string_view to_string(const OpenGlGpuPass pass) noexcept {
    switch(pass) {
    case OpenGlGpuPass::world:return "world";
    case OpenGlGpuPass::studio_viewmodel:return "studio_viewmodel";
    case OpenGlGpuPass::effects_decals:return "effects_decals";
    case OpenGlGpuPass::msaa_resolve:return "msaa_resolve";
    case OpenGlGpuPass::image_adjustments:return "image_adjustments";
    case OpenGlGpuPass::hud_console:return "hud_console";
    }
    return "invalid";
}

std::string_view to_string(const OpenGlWorldGpuSubpass pass) noexcept {
    switch(pass) {
    case OpenGlWorldGpuSubpass::target_setup_clear:return "target_setup_clear";
    case OpenGlWorldGpuSubpass::sky:return "sky";
    case OpenGlWorldGpuSubpass::static_world:return "static_world";
    case OpenGlWorldGpuSubpass::runtime_brushes:return "runtime_brushes";
    }
    return "invalid";
}

const OpenGlWorldRendererStatistics& OpenGlRenderer::statistics() const noexcept
{
    return implementation_->statistics();
}

const OpenGlImageAdjustmentStatistics& OpenGlRenderer::image_adjustment_statistics() const noexcept {
    return implementation_->image_adjustment_statistics();
}
const OpenGlSceneTargetStatistics& OpenGlRenderer::scene_target_statistics() const noexcept {
    return implementation_->scene_target.statistics();
}

const OpenGlEntityRendererStatistics& OpenGlRenderer::entity_statistics()
    const noexcept
{
    return implementation_->entity_statistics();
}

const OpenGlHudRendererStatistics& OpenGlRenderer::hud_statistics() const noexcept
{
    return implementation_->hud_statistics();
}
const OpenGlDecalRendererStatistics& OpenGlRenderer::decal_statistics() const noexcept
{
    return implementation_->decal_statistics();
}

std::string_view to_string(
    const OpenGlFramebufferObservationStatus status) noexcept
{
    switch (status) {
    case OpenGlFramebufferObservationStatus::valid:
        return "valid";
    case OpenGlFramebufferObservationStatus::default_back_buffer_unavailable:
        return "default_back_buffer_unavailable";
    }
    return "unknown";
}

OpenGlFramebufferObservation OpenGlRenderer::observe_framebuffer(
    const RenderExtent extent,
    const ClearColor expected_clear_color, const bool retain_pixels) const
{
    constexpr std::uint64_t maximum_pixels = 16U * 1'024U * 1'024U;
    if (extent.width <= 0 || extent.height <= 0) {
        throw std::invalid_argument{
            "Framebuffer observation requires a positive extent"};
    }
    const auto width = static_cast<std::uint64_t>(extent.width);
    const auto height = static_cast<std::uint64_t>(extent.height);
    if (height > maximum_pixels / width) {
        throw std::length_error{
            "Framebuffer observation exceeds the bounded pixel limit"};
    }
    const auto pixel_count = width * height;
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(pixel_count * 4U));

    GLboolean double_buffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &double_buffered);
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL replay framebuffer capability query");
    if (double_buffered != GL_TRUE) {
        return OpenGlFramebufferObservation{
            .status = OpenGlFramebufferObservationStatus::
                default_back_buffer_unavailable};
    }

    GLint previous_read_framebuffer = 0;
    GLint previous_pack_buffer = 0;
    GLint previous_pack_alignment = 0;
    GLint previous_pack_row_length = 0;
    GLint previous_pack_skip_pixels = 0;
    GLint previous_pack_skip_rows = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_framebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pack_buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &previous_pack_row_length);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &previous_pack_skip_pixels);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &previous_pack_skip_rows);
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL replay framebuffer state query");

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0U);
    GLint previous_default_read_buffer = GL_BACK;
    glGetIntegerv(GL_READ_BUFFER, &previous_default_read_buffer);
    glReadBuffer(GL_BACK);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glReadPixels(0, 0, extent.width, extent.height, GL_RGBA,
        GL_UNSIGNED_BYTE, rgba.data());
    const GLenum read_error = glGetError();

    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, previous_pack_row_length);
    glPixelStorei(GL_PACK_SKIP_PIXELS, previous_pack_skip_pixels);
    glPixelStorei(GL_PACK_SKIP_ROWS, previous_pack_skip_rows);
    glBindBuffer(
        GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pack_buffer));
    glReadBuffer(static_cast<GLenum>(previous_default_read_buffer));
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previous_read_framebuffer));
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL replay framebuffer state restoration");
    if (read_error != GL_NO_ERROR) {
        fail(OpenGlRendererErrorCode::gl_operation_failed,
            "OpenGL replay framebuffer observation failed: " +
                std::string{gl_error_name(read_error)});
    }

    const auto channel = [](const float value) {
        return static_cast<std::uint8_t>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    const std::array clear{
        channel(expected_clear_color.red),
        channel(expected_clear_color.green),
        channel(expected_clear_color.blue),
        channel(expected_clear_color.alpha)};
    OpenGlFramebufferObservation result;
    result.status = OpenGlFramebufferObservationStatus::valid;
    result.read_framebuffer = 0U;
    result.read_buffer = GL_BACK;
    result.sampled_pixel_count = pixel_count;
    std::uint64_t signature = 14'695'981'039'346'656'037ULL;
    const auto mix = [&signature](const std::uint8_t value) {
        signature ^= value;
        signature *= 1'099'511'628'211ULL;
    };
    for (std::uint64_t index = 0U; index < pixel_count; ++index) {
        const auto offset = static_cast<std::size_t>(index * 4U);
        bool differs = false;
        for (std::size_t component = 0U; component < 3U; ++component) {
            const auto actual = rgba[offset + component];
            const auto expected = clear[component];
            const auto difference = actual > expected
                ? actual - expected
                : expected - actual;
            differs = differs || difference > 2U;
        }
        if (!differs) {
            continue;
        }
        const int x = static_cast<int>(index % width);
        const int y = static_cast<int>(index / width);
        if (!result.has_non_clear_bounds) {
            result.minimum_x = x;
            result.maximum_x = x;
            result.minimum_y = y;
            result.maximum_y = y;
            result.has_non_clear_bounds = true;
        } else {
            result.minimum_x = std::min(result.minimum_x, x);
            result.maximum_x = std::max(result.maximum_x, x);
            result.minimum_y = std::min(result.minimum_y, y);
            result.maximum_y = std::max(result.maximum_y, y);
        }
        ++result.non_clear_pixel_count;
        mix(static_cast<std::uint8_t>(x & 0xff));
        mix(static_cast<std::uint8_t>((x >> 8) & 0xff));
        mix(static_cast<std::uint8_t>(y & 0xff));
        mix(static_cast<std::uint8_t>((y >> 8) & 0xff));
        mix(rgba[offset]);
        mix(rgba[offset + 1U]);
        mix(rgba[offset + 2U]);
        mix(rgba[offset + 3U]);
    }
    result.color_signature = signature == 0U ? 1U : signature;
    if (retain_pixels) { result.rgba8=std::move(rgba); }
    return result;
}

} // namespace hlclient::renderer::opengl
