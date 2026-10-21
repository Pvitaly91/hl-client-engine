#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/screen_ui_font.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>

#include "opengl_entity_renderer.hpp"
#include "opengl_scene_target.hpp"

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
#include <cmath>
#include <cstddef>
#include <cstdint>
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
                std::string{gl_error_name(error)});
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
        glGetIntegeri_v(GL_SAMPLER_BINDING, 0U, &sampler_zero_);
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
uniform vec3 u_point_light_center;
uniform vec3 u_point_light_color;
uniform float u_point_light_radius;
uniform float u_point_light_intensity;
uniform int u_effect_light_count;
uniform vec4 u_effect_light_center_radius[8];
uniform vec4 u_effect_light_color_intensity[8];

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
    GLint model_view_projection{-1};
    GLint model_transform{-1};
    GLint base_texture{-1};
    GLint lightmap_texture{-1};
    GLint lightmap_enabled{-1};
    GLint masked_alpha{-1};
    GLint lightmap_layer{-1};
    GLint sky_texture{-1},sky_surface{-1},camera_position{-1};
    GLint point_light_center{-1};
    GLint point_light_color{-1};
    GLint point_light_radius{-1};
    GLint point_light_intensity{-1};
    detail::EffectLightUniforms effect_lights;
};

[[nodiscard]] ProgramState create_world_program()
{
    auto vertex = compile_shader(
        GL_VERTEX_SHADER,
        kWorldVertexShader,
        "Built-in static-world vertex shader");
    auto fragment = compile_shader(
        GL_FRAGMENT_SHADER,
        kWorldFragmentShader,
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
    result.point_light_center = glGetUniformLocation(name, "u_point_light_center");
    result.point_light_color = glGetUniformLocation(name, "u_point_light_color");
    result.point_light_radius = glGetUniformLocation(name, "u_point_light_radius");
    result.point_light_intensity = glGetUniformLocation(name, "u_point_light_intensity");
    result.effect_lights=detail::EffectLightUniforms::locate(name);
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL uniform lookup");
    if (result.model_view_projection < 0 || result.model_transform < 0 ||
        result.point_light_center < 0 || result.point_light_color < 0 ||
        result.point_light_radius < 0 || result.point_light_intensity < 0 || !result.effect_lights.valid() ||
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

struct GpuWorldResources {
    std::shared_ptr<const world_render::WorldRenderPackage> package_instance;
    world_render::WorldRendererResourceIdentity identity{};
    ProgramState program;
    GeometryState geometry;
    std::vector<GlObject> base_textures;
    std::vector<GlObject> lightmap_pages;
    GlObject white_lightmap;
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
    const world_render::WorldRenderPackage& package)
{
    GpuWorldResources result;
    result.identity = package.resource_identity();
    result.program = create_world_program();
    result.geometry = upload_geometry(package);

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
    const GpuWorldResources* bound_resources{};
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

    if(state.bound_resources!=&resources) {
        glUseProgram(resources.program.program.name());
        ++counters.program_binds;
        glBindVertexArray(resources.geometry.vertex_array.name());
        ++counters.vertex_array_binds;
        glUniform3f(resources.program.camera_position,state.sky_eye.x,state.sky_eye.y,state.sky_eye.z);
        ++counters.camera_uploads;
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_CUBE_MAP,state.sky_texture);
        ++counters.sky_texture_binds;
        const auto& light=state.light;
        if(light && valid_render_point_light(*light) && light->intensity>0.0F) {
            glUniform3f(resources.program.point_light_center,
                light->center.x,light->center.y,light->center.z);
            glUniform3f(resources.program.point_light_color,
                light->color[0],light->color[1],light->color[2]);
            glUniform1f(resources.program.point_light_radius,light->radius_units);
            glUniform1f(resources.program.point_light_intensity,light->intensity);
        } else {
            glUniform1f(resources.program.point_light_radius,0.0F);
            glUniform1f(resources.program.point_light_intensity,0.0F);
        }
        resources.program.effect_lights.upload(state.effect_lights);
        ++counters.light_uploads;
        glUniform1i(resources.program.lightmap_layer,0);
        state.bound_resources=&resources;
        state.transforms_valid=false;
        state.base_texture_valid=false;
        state.lightmap_texture_valid=false;
        state.sky_surface.reset();
        state.lightmap_enabled.reset();
        state.masked_alpha.reset();
    }
    const GLint sky_surface=state.sky_texture && material.sky_surface ? 1 : 0;
    if(state.sky_surface!=sky_surface) {
        glUniform1i(resources.program.sky_surface,sky_surface);
        state.sky_surface=sky_surface;
        ++counters.material_uniform_uploads;
    }
    if(!state.transforms_valid || state.model_view_projection!=model_view_projection) {
        glUniformMatrix4fv(resources.program.model_view_projection,1,GL_FALSE,
            model_view_projection.values.data());
        state.model_view_projection=model_view_projection;
        ++counters.transform_uploads;
    }
    if(!state.transforms_valid || state.model_transform!=model_transform) {
        glUniformMatrix4fv(resources.program.model_transform,1,GL_FALSE,
            model_transform.values.data());
        state.model_transform=model_transform;
        ++counters.transform_uploads;
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
        glUniform1i(resources.program.lightmap_enabled,lightmap_enabled);
        state.lightmap_enabled=lightmap_enabled;
        ++counters.material_uniform_uploads;
    }
    const GLint masked_alpha=material.base_texture_alpha_mode==
        assets::WorldTextureAlphaMode::masked_index_255 ? 1 : 0;
    if(state.masked_alpha!=masked_alpha) {
        glUniform1i(resources.program.masked_alpha,masked_alpha);
        state.masked_alpha=masked_alpha;
        ++counters.material_uniform_uploads;
    }

    const auto byte_offset = checked_index_byte_offset(first_index);
    glDrawElements(
        GL_TRIANGLES,
        to_gl_size(
            count,
            OpenGlRendererErrorCode::draw_range_invalid,
            "Visible draw command exceeds GLsizei"),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(byte_offset));
    ++counters.draw_calls;
    if (brush) {
        ++counters.brush_draw_calls;
    }
    counters.triangles += static_cast<std::uint64_t>(index_count / 3U);
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
    GLuint prepare_sky(const std::shared_ptr<const assets::SkyboxAsset>& sky) {
        if(!sky) {sky_asset_.reset();sky_texture_.reset();return 0;}
        if(sky_asset_==sky)return sky_texture_.name();
        sky_asset_.reset();sky_texture_.reset();
        const auto size=sky->faces[0].width;
        if(!size || size>1024)return 0;
        for(const auto& face:sky->faces)if(face.width!=size || face.height!=size ||
            face.pixel_format!=assets::ImagePixelFormat::rgba8 || face.pixels.size()!=std::size_t(size)*size*4)return 0;
        HudGlStateGuard state;
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
    void render_sky(GLuint texture,const RenderCamera& camera,RenderExtent extent) {
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
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
        glDrawArrays(GL_TRIANGLES,0,3);
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
                glGetIntegeri_v(GL_SAMPLER_BINDING,0U,&sampler);
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
            auto candidate = build_gpu_resources(*package);
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
            candidate.world = build_gpu_resources(*world_package);
            candidate.world.package_instance = world_package;

            const auto& brush_package =
                scene->brush_library().render_package();
            if (brush_package) {
                validate_package(*brush_package, hardware);
                candidate.brushes.emplace(
                    build_gpu_resources(*brush_package));
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
    const std::uint64_t frame_number)
{
    require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,"GPU timing prior GL state");
    const bool active=implementation_->begin_gpu_timing(frame_number);
    try {
        render(scene,extent);
        if(active)require_no_gl_error(OpenGlRendererErrorCode::gl_operation_failed,
            "Rendered frame before GPU timing completion");
    } catch(...) {
        if(active)implementation_->end_gpu_timing(false);
        throw;
    }
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
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    require_no_gl_error(
        OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL frame clear");

    if (width == 0 || height == 0) {
        ++statistics.rendered_frame_count;
        return;
    }
    implementation_->gpu_world_subpass_boundary(OpenGlWorldGpuSubpass::sky);
    const auto sky_texture=implementation_->prepare_sky(scene.skybox);
    implementation_->render_sky(sky_texture,view_camera,{width,height});
    // Unit2 and seamless cube filtering are owned for this frame. Portal sky
    // polygons keep their actual depth: entities behind a sky brush stay hidden.
    struct SkyDrawState {
        GLint active{},binding{},sampler{};GLboolean seamless{};
        SkyDrawState() {
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE2);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&binding);glGetIntegeri_v(GL_SAMPLER_BINDING,2,&sampler);
            seamless=glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            glBindSampler(2,0);glActiveTexture(active);
        }
        ~SkyDrawState() {
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_CUBE_MAP,static_cast<GLuint>(binding));
            glBindSampler(2,static_cast<GLuint>(sampler));glActiveTexture(active);
            if(!seamless)glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        }
    } sky_draw_state;
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
    FrameDrawState draw_state{scene.transient_world_light,scene.effect_lights(),
        sky_texture,view_camera.position};
    if (scene.static_world->scene_package) {
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
        if (scene.static_world->visible_draw_list) {
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
            const auto batches = world_package.draw_batches();
            const auto prepared_batches =
                world_package.prepared_draw_batches();
            const auto prepared_blocks =
                world_package.prepared_draw_blocks();
            const auto prepared_frustum =
                prepared_batches.size() == batches.size() &&
                    !prepared_blocks.empty()
                ? world_visibility::WorldViewFrustum::from_view_projection(
                      *matrix.matrix)
                : world_visibility::WorldViewFrustumCreateResult{};
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
                }
            }
            constexpr std::size_t maximum_runs_per_batch = 4U;
            for (std::size_t batch_index = 0U; batch_index < batches.size();
                 ++batch_index) {
                const auto& batch = batches[batch_index];
                const auto draw_range = [&](const std::uint32_t first,
                                            const std::uint32_t count) {
                    draw_package_range(resources.world, world_package,
                        first, count, batch.render_material_index,
                        *matrix.matrix, RenderMatrix4{}, false,
                        frame, draw_state);
                };
                if (!prepared_frustum.frustum || all_batch_bypass) {
                    ++statistics.prepared_world_full_batch_fallback_count;
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                const auto& prepared = prepared_batches[batch_index];
                if (prepared.block_count == 0U ||
                    prepared.first_block > prepared_blocks.size() ||
                    prepared.block_count >
                        prepared_blocks.size() - prepared.first_block) {
                    ++statistics.prepared_world_full_batch_fallback_count;
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
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
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                const auto blocks = prepared_blocks.subspan(
                    prepared.first_block, prepared.block_count);
                std::size_t selected_blocks = 0U;
                std::size_t run_count = 0U;
                struct RetainedRun {
                    std::uint32_t first_index;
                    std::uint32_t end_index;
                };
                std::array<RetainedRun, maximum_runs_per_batch> runs{};
                bool in_run = false;
                for (const auto& block : blocks) {
                    const bool selected = classify_prepared_world_bounds(
                        *prepared_frustum.frustum, block.bounds) !=
                        PreparedBlockFrustumRelation::outside;
                    selected_blocks += static_cast<std::size_t>(selected);
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
                    draw_range(batch.first_index, batch.index_count);
                    continue;
                }
                statistics.prepared_world_selected_block_count +=
                    selected_blocks;
                statistics.prepared_world_rejected_block_count +=
                    prepared.block_count - selected_blocks;
                for (std::size_t run = 0U; run < run_count; ++run) {
                    draw_range(runs[run].first_index,
                        runs[run].end_index - runs[run].first_index);
                }
            }
        }
        if (scene.static_world->runtime_brushes) {
            implementation_->gpu_world_subpass_boundary(
                OpenGlWorldGpuSubpass::runtime_brushes);
            const auto& runtime = *scene.static_world->runtime_brushes;
            const auto& map = *scene.static_world->scene_package;
            if (!world_scene_render::valid_runtime_brush_frame(runtime, map))
                fail(OpenGlRendererErrorCode::draw_range_invalid,
                    "Runtime brush frame does not belong to this map");
            const auto& library = map.brush_library();
            const auto frustum = world_visibility::WorldViewFrustum::from_view_projection(*matrix.matrix);
            const auto camera_leaf = world_spatial::WorldSpatialQuery::locate_point(
                map.spatial_package(), view_camera.position);
            for (const auto& instance : runtime.instances) {
                if (frustum) {
                    const auto classified = frustum.frustum->classify(instance.transformed_bounds);
                    if (classified && *classified.classification ==
                            world_visibility::WorldBoundsClassification::outside) {
                        ++statistics.runtime_brush_culled_count;
                        continue;
                    }
                }
                if (camera_leaf && camera_leaf.result->pvs_available && !camera_leaf.result->solid_or_special &&
                    !instance.touched_leaf_indices.empty()) {
                    bool visible = false;
                    for (const auto leaf : instance.touched_leaf_indices) {
                        if (leaf == 0U || leaf >= map.spatial_package().leaves().size() ||
                            !map.spatial_package().leaves()[leaf].pvs_bit_addressable) {
                            visible = true; break;
                        }
                        const auto result = map.spatial_package().pvs_table().leaf_is_visible_from(
                            camera_leaf.result->leaf_index, leaf);
                        // Missing/special membership cannot safely reject geometry.
                        if (!result || *result) { visible = true; break; }
                    }
                    if (!visible) { ++statistics.runtime_brush_culled_count; continue; }
                }
                if (!resources.brushes || !library.render_package())
                    fail(OpenGlRendererErrorCode::draw_range_invalid, "Runtime brush library absent");
                const auto models = library.models();
                const auto model = std::ranges::find_if(models, [&](const auto& value) {
                    return value.source_model_index() == instance.source_model_index;
                });
                const auto mvp = renderer::multiply(*matrix.matrix, instance.model_transform);
                for (const auto& surface : model->surfaces()) {
                    GLuint alternate = 0U;
                    const auto mapping = library.alternate_by_material();
                    if (instance.alternate_texture && !mapping.empty() && mapping[surface.render_material_index])
                        alternate = resources.brush_alternate_textures[*mapping[surface.render_material_index]].name();
                    draw_package_range(*resources.brushes, *library.render_package(),
                        surface.first_index, surface.index_count, surface.render_material_index,
                        mvp, instance.model_transform,
                        true, frame, draw_state,alternate);
                }
                ++statistics.runtime_brush_submitted_count;
            }
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
