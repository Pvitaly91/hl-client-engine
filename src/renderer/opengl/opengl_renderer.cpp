#include <hlclient/renderer/opengl/opengl_renderer.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace hlclient::renderer::opengl {
namespace {

[[nodiscard]] std::string open_gl_string(const GLenum name)
{
    const GLubyte* value = glGetString(name);
    if (value == nullptr) {
        return "unavailable";
    }
    return reinterpret_cast<const char*>(value);
}

} // namespace

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

        information_.vendor = open_gl_string(GL_VENDOR);
        information_.device = open_gl_string(GL_RENDERER);
        information_.version = open_gl_string(GL_VERSION);
    } catch (...) {
        gladLoaderUnloadGL();
        loader_initialized_ = false;
        throw;
    }
}

OpenGlRenderer::~OpenGlRenderer() noexcept
{
    if (loader_initialized_) {
        gladLoaderUnloadGL();
    }
}

const RendererInfo& OpenGlRenderer::information() const noexcept
{
    return information_;
}

void OpenGlRenderer::render(const RenderScene& scene, const RenderExtent extent)
{
    const int width = std::max(extent.width, 0);
    const int height = std::max(extent.height, 0);
    glViewport(0, 0, width, height);
    glClearColor(
        scene.clear_color.red,
        scene.clear_color.green,
        scene.clear_color.blue,
        scene.clear_color.alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

} // namespace hlclient::renderer::opengl
