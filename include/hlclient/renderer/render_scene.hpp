#pragma once

namespace hlclient::renderer {

struct ClearColor {
    float red{0.035F};
    float green{0.055F};
    float blue{0.085F};
    float alpha{1.0F};
};

struct RenderScene {
    ClearColor clear_color{};
};

struct RenderExtent {
    int width{0};
    int height{0};
};

} // namespace hlclient::renderer
