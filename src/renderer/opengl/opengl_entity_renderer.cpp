#include "opengl_entity_renderer.hpp"

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_render/sprite_billboard_basis.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::renderer::opengl::detail {
namespace {

inline constexpr std::size_t kBonePaletteCapacity = 128U;
inline constexpr GLuint kBonePaletteBindingPoint = 0U;
inline constexpr std::size_t kMaximumEntityDriverLogBytes = 4'096U;

[[noreturn]] void fail_entity(
    const OpenGlRendererErrorCode code,
    const std::string_view context)
{
    throw OpenGlRendererError{code, std::string{context}};
}

void require_gl(const std::string_view context)
{
    if (glGetError() != GL_NO_ERROR) {
        fail_entity(OpenGlRendererErrorCode::gl_operation_failed, context);
    }
}

[[nodiscard]] std::string bounded_driver_log(
    const GLuint object,
    const bool shader)
{
    std::array<GLchar, kMaximumEntityDriverLogBytes> storage{};
    GLsizei written = 0;
    if (shader) {
        glGetShaderInfoLog(object,
            static_cast<GLsizei>(storage.size()),
            &written,
            storage.data());
    } else {
        glGetProgramInfoLog(object,
            static_cast<GLsizei>(storage.size()),
            &written,
            storage.data());
    }
    const auto safe_written = std::clamp(written,
        static_cast<GLsizei>(0),
        static_cast<GLsizei>(storage.size() - 1U));
    std::string result;
    result.reserve(static_cast<std::size_t>(safe_written));
    for (GLsizei index = 0; index < safe_written; ++index) {
        const auto character = static_cast<unsigned char>(
            storage[static_cast<std::size_t>(index)]);
        if (character == '\n' || character == '\r' || character == '\t' ||
            (character >= 0x20U && character <= 0x7eU)) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('?');
        }
    }
    return result;
}

[[nodiscard]] GLsizeiptr buffer_size(
    const std::size_t bytes,
    const std::string_view context)
{
    if (bytes > static_cast<std::size_t>(
                    std::numeric_limits<GLsizeiptr>::max())) {
        fail_entity(OpenGlRendererErrorCode::entity_buffer_upload_failed,
            context);
    }
    return static_cast<GLsizeiptr>(bytes);
}

enum class ObjectKind {
    none,
    vertex_array,
    buffer,
    texture,
    shader,
    program,
};

class Object final {
public:
    Object() = default;
    Object(ObjectKind kind, const GLuint name) noexcept : kind_{kind}, name_{name}
    {
    }
    ~Object() noexcept { reset(); }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept
        : kind_{std::exchange(other.kind_, ObjectKind::none)},
          name_{std::exchange(other.name_, 0U)}
    {
    }
    Object& operator=(Object&& other) noexcept
    {
        if (this != &other) {
            reset();
            kind_ = std::exchange(other.kind_, ObjectKind::none);
            name_ = std::exchange(other.name_, 0U);
        }
        return *this;
    }
    [[nodiscard]] GLuint name() const noexcept { return name_; }

private:
    void reset() noexcept
    {
        if (name_ == 0U) {
            return;
        }
        switch (kind_) {
        case ObjectKind::vertex_array: glDeleteVertexArrays(1, &name_); break;
        case ObjectKind::buffer: glDeleteBuffers(1, &name_); break;
        case ObjectKind::texture: glDeleteTextures(1, &name_); break;
        case ObjectKind::shader: glDeleteShader(name_); break;
        case ObjectKind::program: glDeleteProgram(name_); break;
        case ObjectKind::none: break;
        }
        kind_ = ObjectKind::none;
        name_ = 0U;
    }

    ObjectKind kind_{ObjectKind::none};
    GLuint name_{0U};
};

[[nodiscard]] Object create_vertex_array()
{
    GLuint name = 0U;
    glGenVertexArrays(1, &name);
    require_gl("Entity vertex-array creation failed");
    if (name == 0U) {
        fail_entity(OpenGlRendererErrorCode::entity_buffer_upload_failed,
            "Entity vertex-array name is zero");
    }
    return {ObjectKind::vertex_array, name};
}

[[nodiscard]] Object create_buffer()
{
    GLuint name = 0U;
    glGenBuffers(1, &name);
    require_gl("Entity buffer creation failed");
    if (name == 0U) {
        fail_entity(OpenGlRendererErrorCode::entity_buffer_upload_failed,
            "Entity buffer name is zero");
    }
    return {ObjectKind::buffer, name};
}

[[nodiscard]] Object create_texture()
{
    GLuint name = 0U;
    glGenTextures(1, &name);
    require_gl("Entity texture creation failed");
    if (name == 0U) {
        fail_entity(OpenGlRendererErrorCode::entity_texture_upload_failed,
            "Entity texture name is zero");
    }
    return {ObjectKind::texture, name};
}

[[nodiscard]] Object compile_shader(
    const GLenum type,
    const char* const source,
    const std::string_view description)
{
    const GLuint name = glCreateShader(type);
    if (name == 0U) {
        fail_entity(OpenGlRendererErrorCode::shader_compile_failed,
            description);
    }
    Object shader{ObjectKind::shader, name};
    glShaderSource(name, 1, &source, nullptr);
    glCompileShader(name);
    GLint compiled = GL_FALSE;
    glGetShaderiv(name, GL_COMPILE_STATUS, &compiled);
    require_gl("Entity shader compilation failed");
    if (compiled != GL_TRUE) {
        const auto log = bounded_driver_log(name, true);
        fail_entity(OpenGlRendererErrorCode::shader_compile_failed,
            std::string{description} + " compilation failed" +
                (log.empty() ? std::string{} : ": " + log));
    }
    return shader;
}

[[nodiscard]] Object link_program(
    const char* const vertex_source,
    const char* const fragment_source,
    const std::string_view description)
{
    auto vertex = compile_shader(GL_VERTEX_SHADER, vertex_source, description);
    auto fragment =
        compile_shader(GL_FRAGMENT_SHADER, fragment_source, description);
    const GLuint name = glCreateProgram();
    if (name == 0U) {
        fail_entity(OpenGlRendererErrorCode::program_link_failed, description);
    }
    Object program{ObjectKind::program, name};
    glAttachShader(name, vertex.name());
    glAttachShader(name, fragment.name());
    glLinkProgram(name);
    GLint linked = GL_FALSE;
    glGetProgramiv(name, GL_LINK_STATUS, &linked);
    require_gl("Entity shader program link failed");
    if (linked != GL_TRUE) {
        const auto log = bounded_driver_log(name, false);
        fail_entity(OpenGlRendererErrorCode::program_link_failed,
            std::string{description} + " link failed" +
                (log.empty() ? std::string{} : ": " + log));
    }
    return program;
}

inline constexpr char kStudioVertexShader[] = R"GLSL(#version 330 core
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_raw_texture_st;
layout(location = 3) in uint in_position_bone;
layout(location = 4) in uint in_normal_bone;

layout(std140) uniform BonePalette {
    mat4 bone_matrices[128];
};

uniform mat4 u_view_projection;
uniform mat4 u_entity_transform;
uniform vec2 u_texture_dimensions;

out vec2 fragment_uv;
out float fragment_light;
out vec3 fragment_world_normal;

void main()
{
    vec4 posed_position = bone_matrices[in_position_bone] *
        vec4(in_position, 1.0);
    mat3 entity_rotation = mat3(
        normalize(u_entity_transform[0].xyz),
        normalize(u_entity_transform[1].xyz),
        normalize(u_entity_transform[2].xyz));
    vec3 world_normal = entity_rotation *
        mat3(bone_matrices[in_normal_bone]) * in_normal;
    float squared_normal_length = dot(world_normal, world_normal);
    gl_Position = u_view_projection * u_entity_transform * posed_position;
    fragment_uv = in_raw_texture_st / u_texture_dimensions;
    fragment_light = 0.85;
    fragment_world_normal = squared_normal_length > 1e-12
        ? world_normal * inversesqrt(squared_normal_length)
        : vec3(0.0);
}
)GLSL";

inline constexpr char kEntityFragmentShader[] = R"GLSL(#version 330 core
in vec2 fragment_uv;
in float fragment_light;
uniform sampler2D u_texture;
uniform int u_masked;
out vec4 output_color;

void main()
{
    vec4 sampled = texture(u_texture, fragment_uv);
    if (u_masked != 0 && sampled.a < 0.5) {
        discard;
    }
    output_color = vec4(sampled.rgb * fragment_light, sampled.a);
}
)GLSL";

inline constexpr char kSpriteVertexShader[] = R"GLSL(#version 330 core
layout(location = 0) in vec2 in_corner;
layout(location = 1) in vec2 in_uv;
uniform mat4 u_view_projection;
uniform vec3 u_origin;
uniform vec3 u_right;
uniform vec3 u_up;
uniform vec2 u_corner_min;
uniform vec2 u_corner_size;
uniform float u_scale;
out vec2 fragment_uv;
out float fragment_light;

void main()
{
    vec2 local = u_corner_min + in_corner * u_corner_size;
    vec3 world = u_origin + (u_right * local.x + u_up * local.y) * u_scale;
    gl_Position = u_view_projection * vec4(world, 1.0);
    fragment_uv = in_uv;
    fragment_light = 1.0;
}
)GLSL";

struct StudioProgram {
    Object program;
    GLint view_projection{-1};
    GLint entity_transform{-1};
    GLint texture_dimensions{-1};
    GLint masked{-1};
};

struct SpriteProgram {
    Object program;
    GLint view_projection{-1};
    GLint origin{-1};
    GLint right{-1};
    GLint up{-1};
    GLint corner_min{-1};
    GLint corner_size{-1};
    GLint scale{-1};
    GLint masked{-1};
};

[[nodiscard]] StudioProgram create_studio_program()
{
    StudioProgram result;
    result.program = link_program(
        kStudioVertexShader,
        kEntityFragmentShader,
        "Built-in Studio entity shader");
    const auto name = result.program.name();
    result.view_projection = glGetUniformLocation(name, "u_view_projection");
    result.entity_transform = glGetUniformLocation(name, "u_entity_transform");
    result.texture_dimensions =
        glGetUniformLocation(name, "u_texture_dimensions");
    result.masked = glGetUniformLocation(name, "u_masked");
    const GLint texture = glGetUniformLocation(name, "u_texture");
    const GLuint block = glGetUniformBlockIndex(name, "BonePalette");
    if (result.view_projection < 0 || result.entity_transform < 0 ||
        result.texture_dimensions < 0 || result.masked < 0 || texture < 0 ||
        block == GL_INVALID_INDEX) {
        fail_entity(OpenGlRendererErrorCode::program_link_failed,
            "Studio entity shader did not retain required inputs");
    }
    glUniformBlockBinding(name, block, kBonePaletteBindingPoint);
    glUseProgram(name);
    glUniform1i(texture, 0);
    glUseProgram(0U);
    require_gl("Studio entity shader initialization failed");
    return result;
}

[[nodiscard]] SpriteProgram create_sprite_program()
{
    SpriteProgram result;
    result.program = link_program(
        kSpriteVertexShader,
        kEntityFragmentShader,
        "Built-in Sprite entity shader");
    const auto name = result.program.name();
    result.view_projection = glGetUniformLocation(name, "u_view_projection");
    result.origin = glGetUniformLocation(name, "u_origin");
    result.right = glGetUniformLocation(name, "u_right");
    result.up = glGetUniformLocation(name, "u_up");
    result.corner_min = glGetUniformLocation(name, "u_corner_min");
    result.corner_size = glGetUniformLocation(name, "u_corner_size");
    result.scale = glGetUniformLocation(name, "u_scale");
    result.masked = glGetUniformLocation(name, "u_masked");
    const GLint texture = glGetUniformLocation(name, "u_texture");
    if (result.view_projection < 0 || result.origin < 0 || result.right < 0 ||
        result.up < 0 || result.corner_min < 0 || result.corner_size < 0 ||
        result.scale < 0 || result.masked < 0 || texture < 0) {
        fail_entity(OpenGlRendererErrorCode::program_link_failed,
            "Sprite entity shader did not retain required inputs");
    }
    glUseProgram(name);
    glUniform1i(texture, 0);
    glUseProgram(0U);
    require_gl("Sprite entity shader initialization failed");
    return result;
}

struct EntityCapabilities {
    GLint uniform_block_size{0};
    GLint vertex_uniform_blocks{0};
    GLint texture_units{0};
    GLint vertex_attributes{0};
    GLint texture_size{0};
};

[[nodiscard]] EntityCapabilities query_capabilities()
{
    EntityCapabilities result;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &result.uniform_block_size);
    glGetIntegerv(
        GL_MAX_VERTEX_UNIFORM_BLOCKS, &result.vertex_uniform_blocks);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &result.texture_units);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &result.vertex_attributes);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &result.texture_size);
    require_gl("Entity renderer capability query failed");
    constexpr auto required_palette_bytes =
        kBonePaletteCapacity * sizeof(float) * 16U;
    if (result.uniform_block_size <
            static_cast<GLint>(required_palette_bytes) ||
        result.vertex_uniform_blocks < 1 || result.texture_units < 1 ||
        result.vertex_attributes < 5 || result.texture_size < 1) {
        fail_entity(OpenGlRendererErrorCode::entity_capability_unsupported,
            "OpenGL context cannot support the bounded entity renderer");
    }
    return result;
}

[[nodiscard]] Object upload_rgba_texture(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> rgba,
    const EntityCapabilities& capabilities)
{
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(capabilities.texture_size) ||
        height > static_cast<std::uint32_t>(capabilities.texture_size) ||
        pixels > std::numeric_limits<std::size_t>::max() / 4U ||
        rgba.size() != static_cast<std::size_t>(pixels * 4U)) {
        fail_entity(OpenGlRendererErrorCode::entity_texture_upload_failed,
            "Entity RGBA texture dimensions are invalid");
    }
    auto texture = create_texture();
    glBindTexture(GL_TEXTURE_2D, texture.name());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(GL_RGBA8),
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0U);
    require_gl("Entity RGBA texture upload failed");
    return texture;
}

struct StudioGpuVertex {
    float position[3];
    float normal[3];
    float raw_st[2];
    std::uint32_t position_bone;
    std::uint32_t normal_bone;
};

struct StudioGpuAsset {
    std::shared_ptr<const entity_render::StudioModelRenderAsset> owner;
    Object vertex_array;
    Object vertex_buffer;
    Object index_buffer;
    std::vector<Object> textures;
};

struct SpriteGpuAsset {
    std::shared_ptr<const entity_render::SpriteRenderAsset> owner;
    std::vector<Object> textures;
};

[[nodiscard]] std::shared_ptr<StudioGpuAsset> upload_studio_asset(
    std::shared_ptr<const entity_render::StudioModelRenderAsset> asset,
    const EntityCapabilities& capabilities)
{
    if (!asset || asset->vertices().empty() || asset->indices().empty() ||
        asset->statistics().bone_count == 0U ||
        asset->statistics().bone_count > kBonePaletteCapacity) {
        fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
            "Studio render asset is incomplete or exceeds 128 bones");
    }
    std::vector<StudioGpuVertex> vertices;
    vertices.reserve(asset->vertices().size());
    for (const auto& source : asset->vertices()) {
        vertices.push_back(StudioGpuVertex{
            {source.bone_local_position.x,
                source.bone_local_position.y,
                source.bone_local_position.z},
            {source.bone_local_normal.x,
                source.bone_local_normal.y,
                source.bone_local_normal.z},
            {static_cast<float>(source.raw_texture_s),
                static_cast<float>(source.raw_texture_t)},
            source.position_bone_index,
            source.normal_bone_index,
        });
    }
    auto result = std::make_shared<StudioGpuAsset>();
    result->owner = std::move(asset);
    result->vertex_array = create_vertex_array();
    result->vertex_buffer = create_buffer();
    result->index_buffer = create_buffer();
    glBindVertexArray(result->vertex_array.name());
    glBindBuffer(GL_ARRAY_BUFFER, result->vertex_buffer.name());
    glBufferData(
        GL_ARRAY_BUFFER,
        buffer_size(vertices.size() * sizeof(StudioGpuVertex),
            "Studio vertex buffer is too large"),
        vertices.data(),
        GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, result->index_buffer.name());
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        buffer_size(result->owner->indices().size() * sizeof(std::uint32_t),
            "Studio index buffer is too large"),
        result->owner->indices().data(),
        GL_STATIC_DRAW);
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(StudioGpuVertex));
    glEnableVertexAttribArray(0U);
    glVertexAttribPointer(0U, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(StudioGpuVertex, position)));
    glEnableVertexAttribArray(1U);
    glVertexAttribPointer(1U, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(StudioGpuVertex, normal)));
    glEnableVertexAttribArray(2U);
    glVertexAttribPointer(2U, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(StudioGpuVertex, raw_st)));
    glEnableVertexAttribArray(3U);
    glVertexAttribIPointer(3U, 1, GL_UNSIGNED_INT, stride,
        reinterpret_cast<const void*>(
            offsetof(StudioGpuVertex, position_bone)));
    glEnableVertexAttribArray(4U);
    glVertexAttribIPointer(4U, 1, GL_UNSIGNED_INT, stride,
        reinterpret_cast<const void*>(offsetof(StudioGpuVertex, normal_bone)));
    glBindVertexArray(0U);

    result->textures.reserve(result->owner->materials().size());
    for (const auto& material : result->owner->materials()) {
        if (material.profile ==
            entity_render::StudioRenderMaterialProfile::unsupported) {
            result->textures.emplace_back();
            continue;
        }
        result->textures.push_back(upload_rgba_texture(
            material.width,
            material.height,
            material.rgba8_level_zero,
            capabilities));
    }
    require_gl("Studio entity asset upload failed");
    return result;
}

[[nodiscard]] std::shared_ptr<SpriteGpuAsset> upload_sprite_asset(
    std::shared_ptr<const entity_render::SpriteRenderAsset> asset,
    const EntityCapabilities& capabilities)
{
    if (!asset || asset->frames().empty()) {
        fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
            "Sprite render asset has no frames");
    }
    auto result = std::make_shared<SpriteGpuAsset>();
    result->owner = std::move(asset);
    result->textures.reserve(result->owner->frames().size());
    for (const auto& frame : result->owner->frames()) {
        if (frame.profile == entity_render::SpriteRenderTextureProfile::unsupported) {
            result->textures.emplace_back();
            continue;
        }
        result->textures.push_back(upload_rgba_texture(
            frame.geometry.width,
            frame.geometry.height,
            frame.rgba8,
            capabilities));
    }
    return result;
}

struct SpriteQuadVertex {
    float corner[2];
    float uv[2];
};

struct SpriteQuad {
    Object vertex_array;
    Object vertex_buffer;
    Object index_buffer;
};

[[nodiscard]] SpriteQuad create_sprite_quad()
{
    constexpr std::array vertices{
        SpriteQuadVertex{{0.0F, 0.0F}, {0.0F, 1.0F}},
        SpriteQuadVertex{{1.0F, 0.0F}, {1.0F, 1.0F}},
        SpriteQuadVertex{{1.0F, 1.0F}, {1.0F, 0.0F}},
        SpriteQuadVertex{{0.0F, 1.0F}, {0.0F, 0.0F}},
    };
    constexpr std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 0U, 2U, 3U};
    SpriteQuad result;
    result.vertex_array = create_vertex_array();
    result.vertex_buffer = create_buffer();
    result.index_buffer = create_buffer();
    glBindVertexArray(result.vertex_array.name());
    glBindBuffer(GL_ARRAY_BUFFER, result.vertex_buffer.name());
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, result.index_buffer.name());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0U);
    glVertexAttribPointer(0U, 2, GL_FLOAT, GL_FALSE,
        static_cast<GLsizei>(sizeof(SpriteQuadVertex)),
        reinterpret_cast<const void*>(offsetof(SpriteQuadVertex, corner)));
    glEnableVertexAttribArray(1U);
    glVertexAttribPointer(1U, 2, GL_FLOAT, GL_FALSE,
        static_cast<GLsizei>(sizeof(SpriteQuadVertex)),
        reinterpret_cast<const void*>(offsetof(SpriteQuadVertex, uv)));
    glBindVertexArray(0U);
    require_gl("Shared Sprite quad upload failed");
    return result;
}

[[nodiscard]] RenderMatrix4 rotation_matrix(
    const assets::AssetVector3& degrees) noexcept
{
    constexpr float radians = 0.01745329251994329577F;
    const float x = degrees.x * radians;
    const float y = degrees.y * radians;
    const float z = degrees.z * radians;
    const float cx = std::cos(x);
    const float sx = std::sin(x);
    const float cy = std::cos(y);
    const float sy = std::sin(y);
    const float cz = std::cos(z);
    const float sz = std::sin(z);
    RenderMatrix4 rx;
    rx.values = {1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, cx, sx, 0.0F,
        0.0F, -sx, cx, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    RenderMatrix4 ry;
    ry.values = {cy, 0.0F, -sy, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        sy, 0.0F, cy, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    RenderMatrix4 rz;
    rz.values = {cz, sz, 0.0F, 0.0F,
        -sz, cz, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    return multiply(rz, multiply(ry, rx));
}

[[nodiscard]] RenderMatrix4 entity_matrix(
    const entity_render::EntityRenderTransform& transform) noexcept
{
    auto result = rotation_matrix(transform.rotation_degrees);
    for (std::size_t column = 0U; column < 3U; ++column) {
        for (std::size_t row = 0U; row < 3U; ++row) {
            result.values[column * 4U + row] *= transform.uniform_scale;
        }
    }
    result.values[12U] = transform.origin.x;
    result.values[13U] = transform.origin.y;
    result.values[14U] = transform.origin.z;
    return result;
}

[[nodiscard]] assets::AssetVector3 subtract(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

void bind_neutral_entity_state() noexcept
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0U);
    glBindVertexArray(0U);
    glBindBuffer(GL_UNIFORM_BUFFER, 0U);
    glUseProgram(0U);
}

void discard_gl_errors_bounded() noexcept
{
    constexpr std::size_t maximum_error_count = 16U;
    for (std::size_t index = 0U; index < maximum_error_count; ++index) {
        if (glGetError() == GL_NO_ERROR) {
            return;
        }
    }
}

struct EntitySceneGpu {
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> owner;
    std::vector<std::shared_ptr<StudioGpuAsset>> studio;
    std::vector<std::shared_ptr<SpriteGpuAsset>> sprites;
};

template <typename GpuAsset, typename CpuAsset>
[[nodiscard]] std::shared_ptr<GpuAsset> find_cached(
    const std::span<const std::shared_ptr<GpuAsset>> cached,
    const CpuAsset& wanted)
{
    const auto found = std::ranges::find_if(cached, [&wanted](const auto& item) {
        return item && item->owner &&
            item->owner->resource_id() == wanted.resource_id() &&
            item->owner->resource_revision() == wanted.resource_revision();
    });
    return found == cached.end() ? std::shared_ptr<GpuAsset>{} : *found;
}

} // namespace

class OpenGlEntityRendererBackend::Implementation final {
public:
    Implementation()
        : capabilities_{query_capabilities()},
          studio_program_{create_studio_program()},
          sprite_program_{create_sprite_program()},
          sprite_quad_{create_sprite_quad()},
          pose_buffer_{create_buffer()}
    {
        glBindBuffer(GL_UNIFORM_BUFFER, pose_buffer_.name());
        glBufferData(
            GL_UNIFORM_BUFFER,
            static_cast<GLsizeiptr>(kBonePaletteCapacity * sizeof(float) * 16U),
            nullptr,
            GL_DYNAMIC_DRAW);
        glBindBufferBase(
            GL_UNIFORM_BUFFER, kBonePaletteBindingPoint, pose_buffer_.name());
        glBindBuffer(GL_UNIFORM_BUFFER, 0U);
        require_gl("Studio pose UBO allocation failed");
    }

    void release_resources() noexcept
    {
        if (scene_) {
            scene_.reset();
            ++statistics_.entity_resource_release_count;
        }
        statistics_.entity_scene_present = false;
        statistics_.active_entity_resources = false;
        statistics_.entity_scene_revision = 0U;
        statistics_.entity_frame_revision = 0U;
    }

    [[nodiscard]] EntitySceneGpu& ensure_scene(
        const std::shared_ptr<const entity_render::EntitySceneRenderPackage>& package)
    {
        if (!package || package->resource_id() == 0U ||
            package->resource_revision() == 0U) {
            fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
                "Dynamic entity scene has an invalid identity");
        }
        if (scene_ && scene_->owner->resource_id() == package->resource_id() &&
            scene_->owner->resource_revision() == package->resource_revision()) {
            scene_->owner = package;
            return *scene_;
        }

        EntitySceneGpu candidate;
        std::uint64_t candidate_studio_upload_count = 0U;
        std::uint64_t candidate_sprite_upload_count = 0U;
        candidate.owner = package;
        candidate.studio.reserve(package->studio_assets().size());
        for (const auto& asset : package->studio_assets()) {
            if (!asset) {
                fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
                    "Dynamic entity scene contains a null Studio asset");
            }
            auto cached = scene_ ? find_cached<StudioGpuAsset>(scene_->studio, *asset)
                                 : std::shared_ptr<StudioGpuAsset>{};
            if (!cached) {
                cached = upload_studio_asset(asset, capabilities_);
                ++candidate_studio_upload_count;
            }
            candidate.studio.push_back(std::move(cached));
        }
        candidate.sprites.reserve(package->sprite_assets().size());
        for (const auto& asset : package->sprite_assets()) {
            if (!asset) {
                fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
                    "Dynamic entity scene contains a null Sprite asset");
            }
            auto cached = scene_ ? find_cached<SpriteGpuAsset>(scene_->sprites, *asset)
                                 : std::shared_ptr<SpriteGpuAsset>{};
            if (!cached) {
                cached = upload_sprite_asset(asset, capabilities_);
                ++candidate_sprite_upload_count;
            }
            candidate.sprites.push_back(std::move(cached));
        }
        const bool replacing_scene = scene_.has_value();
        scene_.emplace(std::move(candidate));
        if (replacing_scene) {
            ++statistics_.entity_resource_release_count;
        }
        statistics_.studio_asset_upload_count +=
            candidate_studio_upload_count;
        statistics_.sprite_asset_upload_count +=
            candidate_sprite_upload_count;
        statistics_.entity_scene_revision = package->resource_revision();
        statistics_.entity_scene_present = true;
        statistics_.active_entity_resources = true;
        return *scene_;
    }

    void upload_pose(const entity_render::StudioRenderPose& pose)
    {
        if (pose.bone_matrices.empty() ||
            pose.bone_matrices.size() > kBonePaletteCapacity) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Studio pose matrix count is outside the UBO capacity");
        }
        glBindBuffer(GL_UNIFORM_BUFFER, pose_buffer_.name());
        glBufferSubData(
            GL_UNIFORM_BUFFER,
            0,
            buffer_size(pose.bone_matrices.size() * sizeof(pose.bone_matrices[0]),
                "Studio pose upload is too large"),
            pose.bone_matrices.data());
        glBindBufferBase(
            GL_UNIFORM_BUFFER, kBonePaletteBindingPoint, pose_buffer_.name());
        ++statistics_.pose_ubo_update_count;
    }

    void draw_studio(
        const EntitySceneGpu& gpu,
        const entity_render::EntityRenderFrame& frame,
        const entity_render::EntityDrawCommand& command,
        const RenderMatrix4& view_projection)
    {
        if (command.instance_index >= frame.studio_instances().size()) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Studio draw command has an invalid instance index");
        }
        const auto& instance = frame.studio_instances()[command.instance_index];
        if (instance.visibility_status !=
            entity_render::RuntimeEntityVisibilityStatus::visible) {
            return;
        }
        if (instance.studio_asset_index >= gpu.studio.size() ||
            instance.pose_index >= frame.studio_poses().size()) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Studio instance has an invalid asset or pose index");
        }
        const auto& resource = *gpu.studio[instance.studio_asset_index];
        const auto& asset = *resource.owner;
        const auto& pose = frame.studio_poses()[instance.pose_index];
        if (pose.model_resource_identity != asset.source_identity()) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Studio pose identity does not match its render asset");
        }
        upload_pose(pose);
        const auto transform = entity_matrix(instance.transform);
        if (!is_finite(transform)) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Studio entity transform is non-finite");
        }

        const auto submodels = asset.select_submodels(instance.body_value);
        if (!submodels) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Studio bodypart selection failed at draw time");
        }
        glUseProgram(studio_program_.program.name());
        glUniformMatrix4fv(studio_program_.view_projection, 1, GL_FALSE,
            view_projection.values.data());
        glUniformMatrix4fv(studio_program_.entity_transform, 1, GL_FALSE,
            transform.values.data());
        glBindVertexArray(resource.vertex_array.name());
        for (const auto submodel_index : submodels.submodel_indices) {
            if (submodel_index >= asset.submodels().size()) {
                fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                    "Studio bodypart selected an invalid submodel");
            }
            const auto& submodel = asset.submodels()[submodel_index];
            const auto mesh_end = static_cast<std::size_t>(submodel.first_mesh) +
                submodel.mesh_count;
            if (mesh_end > asset.meshes().size()) {
                fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                    "Studio submodel mesh range is invalid");
            }
            for (std::size_t mesh_index = submodel.first_mesh;
                 mesh_index < mesh_end;
                 ++mesh_index) {
                const auto material = asset.select_material(
                    static_cast<std::uint32_t>(mesh_index),
                    instance.skin_family_index);
                if (!material || *material.material_index >= asset.materials().size()) {
                    fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                        "Studio skin/material selection failed at draw time");
                }
                const auto& selected = asset.materials()[*material.material_index];
                const bool masked = selected.profile ==
                    entity_render::StudioRenderMaterialProfile::masked;
                const bool requested_masked = command.draw_class ==
                    entity_render::EntityDrawClass::studio_masked;
                if (selected.profile ==
                        entity_render::StudioRenderMaterialProfile::unsupported ||
                    masked != requested_masked) {
                    continue;
                }
                const auto texture_index = *material.material_index;
                if (texture_index >= resource.textures.size() ||
                    resource.textures[texture_index].name() == 0U) {
                    fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                        "Studio selected texture is not renderable");
                }
                const auto& mesh = asset.meshes()[mesh_index];
                glUniform2f(studio_program_.texture_dimensions,
                    static_cast<float>(selected.width),
                    static_cast<float>(selected.height));
                glUniform1i(studio_program_.masked, masked ? 1 : 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, resource.textures[texture_index].name());
                glDrawElements(GL_TRIANGLES,
                    static_cast<GLsizei>(mesh.index_count),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(mesh.first_index) *
                        sizeof(std::uint32_t)));
                ++statistics_.studio_draw_count;
                ++statistics_.model_texture_bind_count;
            }
        }
    }

    void draw_sprite(
        const EntitySceneGpu& gpu,
        const entity_render::EntityRenderFrame& frame,
        const entity_render::EntityDrawCommand& command,
        const RenderCamera& camera,
        const RenderMatrix4& view_projection)
    {
        if (command.instance_index >= frame.sprite_instances().size()) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Sprite draw command has an invalid instance index");
        }
        const auto& instance = frame.sprite_instances()[command.instance_index];
        if (instance.visibility_status !=
            entity_render::RuntimeEntityVisibilityStatus::visible) {
            return;
        }
        if (instance.sprite_asset_index >= gpu.sprites.size()) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Sprite instance has an invalid asset index");
        }
        const auto& resource = *gpu.sprites[instance.sprite_asset_index];
        const auto& asset = *resource.owner;
        if (instance.selected_frame_index >= asset.frames().size() ||
            instance.selected_frame_index >= resource.textures.size()) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Sprite instance has an invalid frame index");
        }
        const auto& selected = asset.frames()[instance.selected_frame_index];
        if (selected.profile == entity_render::SpriteRenderTextureProfile::unsupported ||
            resource.textures[instance.selected_frame_index].name() == 0U) {
            return;
        }

        const auto camera_forward = normalize(subtract(camera.target, camera.position));
        const auto camera_up = normalize(camera.up);
        if (!camera_forward || !camera_up) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Sprite camera basis is invalid");
        }
        const auto camera_right = normalize(cross(*camera_forward, *camera_up));
        if (!camera_right) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Sprite camera right vector is invalid");
        }
        const auto corrected_up = cross(*camera_right, *camera_forward);
        const auto rotation = rotation_matrix(instance.transform.rotation_degrees);
        const entity_render::SpriteBillboardInput basis_input{
            instance.orientation,
            *camera_forward,
            *camera_right,
            corrected_up,
            {rotation.values[8U], rotation.values[9U], rotation.values[10U]},
            {rotation.values[0U], rotation.values[1U], rotation.values[2U]},
            {rotation.values[4U], rotation.values[5U], rotation.values[6U]},
        };
        const auto basis = entity_render::SpriteBillboardBasis::calculate(basis_input);
        if (!basis) {
            fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                "Sprite billboard basis is unsupported or degenerate");
        }
        const auto& geometry = selected.geometry;
        const float corner_x = static_cast<float>(geometry.source_origin.x);
        const float corner_y = static_cast<float>(geometry.source_origin.y) -
            static_cast<float>(geometry.height);
        const bool masked = selected.profile ==
            entity_render::SpriteRenderTextureProfile::alpha_test_masked;
        glUseProgram(sprite_program_.program.name());
        glUniformMatrix4fv(sprite_program_.view_projection, 1, GL_FALSE,
            view_projection.values.data());
        glUniform3f(sprite_program_.origin,
            instance.transform.origin.x,
            instance.transform.origin.y,
            instance.transform.origin.z);
        glUniform3f(sprite_program_.right, basis.right.x, basis.right.y, basis.right.z);
        glUniform3f(sprite_program_.up, basis.up.x, basis.up.y, basis.up.z);
        glUniform2f(sprite_program_.corner_min, corner_x, corner_y);
        glUniform2f(sprite_program_.corner_size,
            static_cast<float>(geometry.width),
            static_cast<float>(geometry.height));
        glUniform1f(sprite_program_.scale, instance.transform.uniform_scale);
        glUniform1i(sprite_program_.masked, masked ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,
            resource.textures[instance.selected_frame_index].name());
        glBindVertexArray(sprite_quad_.vertex_array.name());
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        ++statistics_.sprite_draw_count;
        ++statistics_.sprite_texture_bind_count;
    }

    void render(
        const RenderDynamicEntities& entities,
        const RenderCamera& camera,
        const RenderMatrix4& view_projection)
    {
        if (!entities.package || !entities.frame) {
            fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
                "RenderDynamicEntities is missing its package or frame");
        }
        if (entities.frame->scene_package_identity() !=
            entity_render::EntityRenderResourceIdentity{
                entities.package->resource_id(),
                entities.package->resource_revision()}) {
            fail_entity(OpenGlRendererErrorCode::invalid_entity_scene,
                "Dynamic entity frame does not belong to its scene package");
        }
        auto& gpu = ensure_scene(entities.package);
        const auto& frame = *entities.frame;
        const auto retained_statistics = statistics_;
        try {
            statistics_.entity_frame_revision = frame.resource_revision();
            statistics_.visible_entity_count =
                static_cast<std::uint64_t>(frame.statistics().visible_count);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            for (const auto& command : frame.draw_commands()) {
                switch (command.visual_kind) {
                case entity_render::RuntimeEntityVisualKind::studio_model:
                    draw_studio(gpu, frame, command, view_projection);
                    break;
                case entity_render::RuntimeEntityVisualKind::sprite:
                    draw_sprite(gpu, frame, command, camera, view_projection);
                    break;
                case entity_render::RuntimeEntityVisualKind::unsupported:
                    fail_entity(OpenGlRendererErrorCode::entity_draw_invalid,
                        "Unsupported entity kind reached the draw list");
                }
            }
            bind_neutral_entity_state();
            require_gl("OpenGL entity draw failed");
            ++statistics_.entity_frame_count;
        } catch (...) {
            statistics_ = retained_statistics;
            bind_neutral_entity_state();
            discard_gl_errors_bounded();
            throw;
        }
    }

    [[nodiscard]] const OpenGlEntityRendererStatistics& statistics() const noexcept
    {
        return statistics_;
    }

private:
    EntityCapabilities capabilities_{};
    StudioProgram studio_program_;
    SpriteProgram sprite_program_;
    SpriteQuad sprite_quad_;
    Object pose_buffer_;
    std::optional<EntitySceneGpu> scene_;
    OpenGlEntityRendererStatistics statistics_{};
};

OpenGlEntityRendererBackend::OpenGlEntityRendererBackend()
    : implementation_{std::make_unique<Implementation>()}
{
}

OpenGlEntityRendererBackend::~OpenGlEntityRendererBackend() noexcept = default;

void OpenGlEntityRendererBackend::render(
    const RenderDynamicEntities& entities,
    const RenderCamera& camera,
    const RenderMatrix4& view_projection)
{
    implementation_->render(entities, camera, view_projection);
}

void OpenGlEntityRendererBackend::release_resources() noexcept
{
    implementation_->release_resources();
}

const OpenGlEntityRendererStatistics& OpenGlEntityRendererBackend::statistics()
    const noexcept
{
    return implementation_->statistics();
}

} // namespace hlclient::renderer::opengl::detail
