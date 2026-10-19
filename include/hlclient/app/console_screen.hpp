#pragma once
#include <hlclient/game_api/console_theme.hpp>
#include <hlclient/renderer/screen_ui.hpp>

namespace hlclient::app {
struct ConsoleOutputLayout {
    float scale{},advance{},line_height{},margin{},panel_height{},editor_y{};
    renderer::ScreenUiClip output;
    std::size_t columns{},rows{};
};
// All dimensions are actual framebuffer pixels. Layout uses a stable full or
// half panel; animation translates that completed panel once afterwards.
[[nodiscard]] ConsoleOutputLayout console_output_layout(unsigned framebuffer_width,
    unsigned framebuffer_height,float panel_height) noexcept;
void translate_console_panel(renderer::ScreenUiFrame&,float offset_y,
    unsigned framebuffer_width,float visible_height);
[[nodiscard]] std::shared_ptr<const assets::ImageAsset> make_console_theme_background(
    const game_api::ConsoleVisualTheme&);
// visible_fraction is monotonic-controller output0..0.5 in game. An idle or
// loading backdrop is always full size. The full image slides behind a clip;
// neither its logo nor its pixels are vertically squeezed into half height.
[[nodiscard]] renderer::ScreenUiFrame build_console_theme(
    const game_api::ConsoleVisualTheme&,unsigned width,unsigned height,
    double visible_fraction,bool full_background,
    std::shared_ptr<const assets::ImageAsset> background = {});
}
