#include <hlclient/renderer/opengl/opengl_renderer.hpp>

#include <hlclient/assets/world_lightmap_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>

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

out vec2 fragment_base_uv;
out vec2 fragment_lightmap_uv;

void main()
{
    gl_Position = u_model_view_projection * vec4(in_position, 1.0);
    fragment_base_uv = in_base_uv;
    fragment_lightmap_uv = in_lightmap_uv;
}
)GLSL";

inline constexpr char kWorldFragmentShader[] = R"GLSL(#version 330 core
in vec2 fragment_base_uv;
in vec2 fragment_lightmap_uv;

uniform sampler2D u_base_texture;
uniform sampler2DArray u_lightmap_texture;
uniform int u_lightmap_enabled;
uniform int u_masked_alpha;
uniform int u_lightmap_layer;

out vec4 output_color;

void main()
{
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
    GLint base_texture{-1};
    GLint lightmap_texture{-1};
    GLint lightmap_enabled{-1};
    GLint masked_alpha{-1};
    GLint lightmap_layer{-1};
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
    result.base_texture = glGetUniformLocation(name, "u_base_texture");
    result.lightmap_texture = glGetUniformLocation(name, "u_lightmap_texture");
    result.lightmap_enabled = glGetUniformLocation(name, "u_lightmap_enabled");
    result.masked_alpha = glGetUniformLocation(name, "u_masked_alpha");
    result.lightmap_layer = glGetUniformLocation(name, "u_lightmap_layer");
    require_no_gl_error(
        OpenGlRendererErrorCode::program_link_failed,
        "OpenGL uniform lookup");
    if (result.model_view_projection < 0 || result.base_texture < 0 ||
        result.lightmap_texture < 0 || result.lightmap_enabled < 0 ||
        result.masked_alpha < 0 || result.lightmap_layer < 0) {
        fail(OpenGlRendererErrorCode::program_link_failed,
            "Built-in static-world shader did not retain every required uniform");
    }

    glUseProgram(name);
    glUniform1i(result.base_texture, 0);
    glUniform1i(result.lightmap_texture, 1);
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
        !package.textured_world().textures.complete_for_world_materials() ||
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
    void release_world_resources() noexcept
    {
        if (resources_) {
            resources_.reset();
            ++statistics_.world_resource_release_count;
        }
        statistics_.active_world_resources = false;
        statistics_.package_revision = 0U;
        statistics_.uploaded_base_texture_count = 0U;
        statistics_.uploaded_base_mip_level_count = 0U;
        statistics_.uploaded_lightmap_page_count = 0U;
        statistics_.uploaded_lightmap_layer_count = 0U;
        statistics_.uploaded_white_lightmap_count = 0U;
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

    [[nodiscard]] OpenGlWorldRendererStatistics& statistics() noexcept
    {
        return statistics_;
    }

    [[nodiscard]] const OpenGlWorldRendererStatistics& statistics() const noexcept
    {
        return statistics_;
    }

private:
    std::optional<GpuWorldResources> resources_;
    OpenGlWorldRendererStatistics statistics_{};
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

void OpenGlRenderer::render(const RenderScene& scene, const RenderExtent extent)
{
    auto& statistics = implementation_->statistics();
    const int width = std::max(extent.width, 0);
    const int height = std::max(extent.height, 0);
    statistics.last_extent = RenderExtent{width, height};
    statistics.world_present = scene.static_world.has_value();

    glViewport(0, 0, width, height);
    glClearColor(
        scene.clear_color.red,
        scene.clear_color.green,
        scene.clear_color.blue,
        scene.clear_color.alpha);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    require_no_gl_error(
        OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL frame clear");

    if (!scene.static_world || width == 0 || height == 0) {
        ++statistics.rendered_frame_count;
        return;
    }
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

    const auto matrix = camera_view_projection(
        scene.camera,
        RenderExtent{width, height});
    if (!matrix || !matrix.matrix) {
        const auto detail = matrix.error
                                ? std::string{renderer::to_string(*matrix.error)}
                                : std::string{"unknown_camera_error"};
        fail(OpenGlRendererErrorCode::camera_invalid,
            "Static-world camera is invalid: " + detail);
    }

    auto& resources =
        implementation_->ensure_resources(scene.static_world->package);
    const auto& package = *scene.static_world->package;
    const auto materials = package.materials();
    const auto batches = package.draw_batches();

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

    glUseProgram(resources.program.program.name());
    glBindVertexArray(resources.geometry.vertex_array.name());
    glUniformMatrix4fv(
        resources.program.model_view_projection,
        1,
        GL_FALSE,
        matrix.matrix->values.data());
    glUniform1i(resources.program.lightmap_layer, 0);

    std::uint64_t frame_draw_calls = 0U;
    std::uint64_t frame_triangles = 0U;
    std::uint64_t frame_base_binds = 0U;
    std::uint64_t frame_lightmap_binds = 0U;
    for (const auto& batch : batches) {
        const auto& material = materials[batch.render_material_index];

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(
            GL_TEXTURE_2D,
            resources.base_textures[material.base_texture_asset_index].name());
        ++frame_base_binds;

        glActiveTexture(GL_TEXTURE1);
        if (material.lightmap_mode ==
            world_render::WorldRenderLightmapMode::atlas) {
            glBindTexture(
                GL_TEXTURE_2D_ARRAY,
                resources.lightmap_pages[
                    *material.lightmap_atlas_page_index]
                    .name());
            glUniform1i(resources.program.lightmap_enabled, 1);
        } else {
            glBindTexture(
                GL_TEXTURE_2D_ARRAY,
                resources.white_lightmap.name());
            glUniform1i(resources.program.lightmap_enabled, 0);
        }
        ++frame_lightmap_binds;
        glUniform1i(
            resources.program.masked_alpha,
            material.base_texture_alpha_mode ==
                    assets::WorldTextureAlphaMode::masked_index_255
                ? 1
                : 0);

        const auto byte_offset = checked_index_byte_offset(batch.first_index);
        glDrawElements(
            GL_TRIANGLES,
            to_gl_size(
                static_cast<std::size_t>(batch.index_count),
                OpenGlRendererErrorCode::draw_range_invalid,
                "Static-world draw count exceeds GLsizei"),
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(byte_offset));
        ++frame_draw_calls;
        frame_triangles += static_cast<std::uint64_t>(batch.index_count / 3U);
    }

    require_no_gl_error(
        OpenGlRendererErrorCode::gl_operation_failed,
        "OpenGL static-world draw");
    glBindVertexArray(0U);
    glUseProgram(0U);

    ++statistics.rendered_frame_count;
    statistics.draw_call_count += frame_draw_calls;
    statistics.triangle_count += frame_triangles;
    statistics.base_texture_bind_count += frame_base_binds;
    statistics.lightmap_bind_count += frame_lightmap_binds;
}

const OpenGlWorldRendererStatistics& OpenGlRenderer::statistics() const noexcept
{
    return implementation_->statistics();
}

} // namespace hlclient::renderer::opengl
