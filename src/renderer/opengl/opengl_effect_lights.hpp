#pragma once
#include <hlclient/renderer/render_scene.hpp>
#include <glad/gl.h>
#include <array>
#include <span>

namespace hlclient::renderer::opengl::detail {
// Shared bounded upload for world and Studio; invalid entries cannot leak NaNs
// into the shader, and every draw resets count, including an empty next frame.
struct EffectLightUniforms {
    GLint count{-1}, center_radius{-1}, color_intensity{-1};
    [[nodiscard]] static EffectLightUniforms locate(GLuint program) noexcept {
        return {glGetUniformLocation(program,"u_effect_light_count"),
            glGetUniformLocation(program,"u_effect_light_center_radius[0]"),
            glGetUniformLocation(program,"u_effect_light_color_intensity[0]")};
    }
    [[nodiscard]] bool valid() const noexcept { return count>=0 && center_radius>=0 && color_intensity>=0; }
    void upload(std::span<const RenderPointLight> lights) const noexcept {
        std::array<float,8U*4U> centers{},colors{};
        GLsizei used=0;
        for(const auto& light:lights.first(std::min(lights.size(),std::size_t{8}))) {
            if(!valid_render_point_light(light) || light.intensity<=0) continue;
            const auto offset=static_cast<std::size_t>(used++)*4U;
            centers[offset]=light.center.x;centers[offset+1]=light.center.y;
            centers[offset+2]=light.center.z;centers[offset+3]=light.radius_units;
            std::copy(light.color.begin(),light.color.end(),colors.begin()+offset);
            colors[offset+3]=light.intensity;
        }
        glUniform1i(count,used);
        if(used) {
            glUniform4fv(center_radius,used,centers.data());
            glUniform4fv(color_intensity,used,colors.data());
        }
    }
};
} // namespace hlclient::renderer::opengl::detail
