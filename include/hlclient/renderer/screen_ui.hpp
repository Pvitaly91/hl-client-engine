#pragma once
#include <hlclient/assets/asset_types.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <type_traits>
#include <vector>

namespace hlclient::renderer {

// Neutral top-left framebuffer-pixel commands. The producer owns layout,
// theme, text and animation; the renderer owns no console or session state.
struct ScreenUiClip { float x{}, y{}, width{}, height{}; };
struct ScreenUiRectangle {
    float x{}, y{}, width{}, height{};
    std::array<float,4> color{1,1,1,1};
    std::optional<ScreenUiClip> clip;
};
struct ScreenUiText {
    std::string text;
    float x{}, y{}, pixel_size{2}, advance{12}, line_height{18};
    std::array<float,4> color{1,1,1,1};
    std::optional<ScreenUiClip> clip;
};
using ScreenUiImagePixels = assets::ImageAsset;
struct ScreenUiImage {
    std::shared_ptr<const ScreenUiImagePixels> pixels;
    float x{}, y{}, width{}, height{};
    std::array<float,4> color{1,1,1,1};
    std::optional<ScreenUiClip> clip;
};
using ScreenUiCommand = std::variant<ScreenUiRectangle,ScreenUiText,ScreenUiImage>;
struct ScreenUiFrame { std::vector<ScreenUiCommand> commands; };

[[nodiscard]] inline bool valid_screen_ui(const ScreenUiFrame& frame) noexcept {
    if(frame.commands.size()>1024U)return false;
    std::size_t text_bytes{}, image_bytes{}, glyphs{};
    const auto coordinate=[](float v){return std::isfinite(v)&&std::abs(v)<=65536.0F;};
    const auto positive=[&](float v){return coordinate(v)&&v>0;};
    for(const auto& command:frame.commands) {
        const bool valid=std::visit([&](const auto& item) {
            if(!coordinate(item.x)||!coordinate(item.y))return false;
            for(float c:item.color)if(!std::isfinite(c)||c<0||c>1)return false;
            if(item.clip&&(!coordinate(item.clip->x)||!coordinate(item.clip->y)||
                !positive(item.clip->width)||!positive(item.clip->height)))return false;
            using Type=std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<Type,ScreenUiText>) {
                if(item.text.size()>8192U)return false;
                text_bytes+=item.text.size();
                for(unsigned char c:item.text)if((c&0xc0U)!=0x80U)++glyphs;
                return item.text.size()<=8192U&&positive(item.pixel_size)&&item.pixel_size<=32&&
                    positive(item.advance)&&positive(item.line_height);
            } else {
                if(!positive(item.width)||!positive(item.height))return false;
                if constexpr(std::is_same_v<Type,ScreenUiImage>) {
                    if(!item.pixels||!item.pixels->width||!item.pixels->height||
                        item.pixels->width>4096U||item.pixels->height>4096U)return false;
                    const auto bytes=std::size_t(item.pixels->width)*item.pixels->height*4U;
                    if(item.pixels->pixel_format!=assets::ImagePixelFormat::rgba8||
                        item.pixels->pixels.size()!=bytes)return false;
                    image_bytes+=bytes;
                }
                return true;
            }
        },command);
        if(!valid||text_bytes>32768U||glyphs>16384U||image_bytes>32U*1024U*1024U)return false;
    }
    return true;
}

} // namespace hlclient::renderer
