#include <hlclient/app/console_screen.hpp>
#include <algorithm>
#include <cmath>

namespace hlclient::app {
ConsoleOutputLayout console_output_layout(const unsigned width,const unsigned height,const float panel) noexcept {
    ConsoleOutputLayout result;
    if(!width||!height||width>16384||height>16384||!std::isfinite(panel))return result;
    result.scale=std::clamp(std::floor(float(height)/540.0F),1.0F,4.0F);
    result.advance=6*result.scale;result.line_height=11*result.scale;result.margin=16*result.scale;
    result.panel_height=std::clamp(panel,0.0F,float(height));
    result.editor_y=std::max(0.0F,result.panel_height-result.margin-result.line_height);
    const float output_bottom=std::max(0.0F,result.editor_y-result.line_height);
    const float output_top=std::min(64*result.scale,output_bottom);
    result.output={result.margin,output_top,std::max(0.0F,float(width)-2*result.margin),output_bottom-output_top};
    result.columns=static_cast<std::size_t>(std::floor(result.output.width/result.advance));
    result.rows=static_cast<std::size_t>(std::floor(result.output.height/result.line_height));
    return result;
}
void translate_console_panel(renderer::ScreenUiFrame& frame,const float offset,
    const unsigned width,const float visible_height) {
    if(!width||!std::isfinite(offset)||!std::isfinite(visible_height)||visible_height<=0) {
        frame.commands.clear();return;
    }
    frame.commands.erase(std::remove_if(frame.commands.begin(),frame.commands.end(),[&](auto& command) {
        return std::visit([&](auto& item) {
            item.y+=offset;
            auto clip=item.clip.value_or(renderer::ScreenUiClip{0,-offset,float(width),visible_height});
            clip.y+=offset;
            const auto left=std::clamp(clip.x,0.0F,float(width));
            const auto right=std::clamp(clip.x+clip.width,0.0F,float(width));
            const auto top=std::clamp(clip.y,0.0F,visible_height);
            const auto bottom=std::clamp(clip.y+clip.height,0.0F,visible_height);
            if(right<=left||bottom<=top)return true;
            item.clip=renderer::ScreenUiClip{left,top,right-left,bottom-top};return false;
        },command);
    }),frame.commands.end());
}
std::shared_ptr<const assets::ImageAsset> make_console_theme_background(const game_api::ConsoleVisualTheme& theme) {
    auto image=std::make_shared<assets::ImageAsset>();image->width=800;image->height=600;
    image->pixels.resize(std::size_t(image->width)*image->height*4U);
    for(unsigned y=0;y<image->height;++y)for(unsigned x=0;x<image->width;++x) {
        const float ex=(float(x)-570)/142,ey=(float(y)-210)/142;
        const float radius=std::sqrt(ex*ex+ey*ey);
        float mark=theme.emblem_circle?std::clamp((0.027F-std::abs(radius-.74F))*55,0.0F,1.0F):0;
        for(const auto& stroke:theme.emblem) {
            if(stroke.width<=0)continue;
            const float dx=stroke.x2-stroke.x1,dy=stroke.y2-stroke.y1;
            const float length=dx*dx+dy*dy;if(length<=0)continue;
            const float t=std::clamp(((ex-stroke.x1)*dx+(ey-stroke.y1)*dy)/length,0.0F,1.0F);
            const float qx=ex-stroke.x1-t*dx,qy=ey-stroke.y1-t*dy;
            mark=std::max(mark,std::clamp((stroke.width*.5F-std::sqrt(qx*qx+qy*qy))*90,0.0F,1.0F));
        }
        const float glow=0.09F*std::exp(-radius*radius*.9F);
        const float stripe=(y%4==0?.008F:0)+(x%100==0||y%100==0?.014F:0);
        const float grain=float((x*17U+y*29U+(x*y)%31U)%13U)/1800;
        const auto at=(std::size_t(y)*image->width+x)*4U;
        for(unsigned c=0;c<3;++c) {
            const float base=theme.backdrop[c]+theme.accent[c]*glow+stripe+grain;
            const float color=base*(1-mark)+theme.accent[c]*mark*.61F;
            image->pixels[at+c]=std::byte{static_cast<unsigned char>(std::clamp(color,0.0F,1.0F)*255)};
        }
        image->pixels[at+3]=std::byte{255};
    }
    return image;
}

renderer::ScreenUiFrame build_console_theme(const game_api::ConsoleVisualTheme& theme,
    const unsigned width,const unsigned height,const double fraction,const bool full,
    std::shared_ptr<const assets::ImageAsset> background) {
    renderer::ScreenUiFrame result;
    if(!width||!height||width>16384||height>16384||!std::isfinite(fraction))return result;
    const float shown=full?float(height):float(height)*float(std::clamp(fraction,0.0,.5));
    if(shown<=0)return result;
    if(!background) {
        // Small application-owned cache. It retains no source, network or GL
        // handles and the fallback is available before any approved-asset job.
        static thread_local std::string title;
        static thread_local std::shared_ptr<const assets::ImageAsset> cached;
        if(!cached||title!=theme.title){title=theme.title;cached=make_console_theme_background(theme);}
        background=cached;
    }
    if(!background->width||!background->height)return result;
    const float w=float(width),h=float(height),slide=full?0:shown-h*.5F;
    const renderer::ScreenUiClip clip{0,0,w,shown};
    const float scale=std::max(w/background->width,h/background->height);
    const float iw=background->width*scale,ih=background->height*scale;
    result.commands.emplace_back(renderer::ScreenUiImage{background,(w-iw)*.5F,
        (h-ih)*.5F+slide,iw,ih,{1,1,1,1},clip});
    // The lower edge and text layer travel with the panel, independently of
    // aspect-preserving backdrop sampling.
    result.commands.emplace_back(renderer::ScreenUiRectangle{0,0,w,shown,{0,0,0,.25F},clip});
    const float pixel=std::max(1.0F,std::floor(h/540));
    const float margin=16*pixel;
    result.commands.emplace_back(renderer::ScreenUiText{std::string{theme.title},margin,
        18*pixel+slide,pixel*2,pixel*12,pixel*20,theme.accent,clip});
    if(shown>2)result.commands.emplace_back(renderer::ScreenUiRectangle{0,shown-2,w,2,theme.accent,clip});
    return result;
}
}
