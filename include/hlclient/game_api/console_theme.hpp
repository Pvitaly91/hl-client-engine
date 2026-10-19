#pragma once
#include <array>
#include <string_view>

namespace hlclient::game_api {
struct ConsoleThemeStroke {float x1{},y1{},x2{},y2{},width{};};
// Compiled module-owned visual policy. No renderer, native handle, filesystem
// or session lifetime is required to ask a module for its idle theme.
struct ConsoleVisualTheme {
    std::string_view title{"CLIENT"};
    std::string_view background_resource{};
    std::array<float,3> backdrop{0.085F,0.075F,0.06F};
    std::array<float,4> accent{0.92F,0.63F,0.22F,1};
    std::array<float,4> text{0.90F,0.86F,0.75F,1};
    std::array<ConsoleThemeStroke,4> emblem{};
    bool emblem_circle{};
};
}
