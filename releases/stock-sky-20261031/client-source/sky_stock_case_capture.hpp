#pragma once
// Explicit untimed task verification only. Disabled by default. Retain at most
// two normal application's completed back buffers, then serialize after the
// live loop. No camera/input/scene/callback/lifecycle change or benchmark hook.
#include "sky_candidate_api.hpp"
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sky_stock_case_capture {
class Capture final {
    struct Shot {
        std::array<float,3> eye{},target{};
        float fov{};int width{},height{};
        std::uint64_t frame{};SkyCandidateStats sky;
        bool controlled{};
        std::vector<std::uint8_t> rgba;
    };
    std::filesystem::path directory_;
    std::array<std::optional<Shot>,2> shots_;
    std::array<unsigned,2> stable_{};
    std::array<std::optional<std::chrono::steady_clock::time_point>,2> stable_since_{};
    static constexpr std::array<const char*,2> names{"main","side"};
    static void need(bool good,const char* message){if(!good)throw std::runtime_error{message};}
    void save() {
        if(directory_.empty())return;
        need(!std::filesystem::exists(directory_/"capture.json"),"Preserve existing stock capture receipt");
        std::ofstream report(directory_/"capture.json",std::ios::binary);need(bool(report),"Cannot create stock capture receipt");
        report<<std::setprecision(17)<<"{\"schema\":\"hlc.stock-sky-correction.native-case-capture.v1\",\"policy\":\""
            <<background_sky_policy_name(hlc_background_sky_policy())<<"\",\"scope\":\"two_explicit_untimed_normal_application_default_backbuffer_observations; not_performance\",\"shots\":[";
        for(unsigned i=0;i<shots_.size();++i) {
            if(i)report<<',';report<<"{\"case\":\""<<names[i]<<"\",\"captured\":"<<(shots_[i]?"true":"false");
            if(shots_[i]) {
                const auto& s=*shots_[i];const auto path=directory_/(std::string{names[i]}+".ppm");
                need(!std::filesystem::exists(path),"Preserve existing native stock screenshot");
                std::ofstream image(path,std::ios::binary);need(bool(image),"Cannot create native stock screenshot");
                image<<"P6\n"<<s.width<<' '<<s.height<<"\n255\n";
                for(int y=s.height-1;y>=0;--y)for(int x=0;x<s.width;++x) {
                    const auto p=(static_cast<std::size_t>(y)*s.width+x)*4U;
                    image.write(reinterpret_cast<const char*>(s.rgba.data()+p),3);
                }
                need(bool(image),"Native stock screenshot write failed");
                report<<",\"file\":\""<<names[i]<<".ppm\",\"frame\":"<<s.frame<<",\"width\":"<<s.width<<",\"height\":"<<s.height
                    <<",\"eye\":["<<s.eye[0]<<','<<s.eye[1]<<','<<s.eye[2]<<"],\"target\":["<<s.target[0]<<','<<s.target[1]<<','<<s.target[2]
                    <<"],\"vertical_fov_radians\":"<<s.fov<<",\"controlled_alive\":"<<(s.controlled?"true":"false")
                    <<",\"fullscreen_background_draws_cumulative\":"<<s.sky.fullscreen_sky_draws
                    <<",\"static_world_sky_skipped_draws_cumulative\":"<<s.sky.static_world_sky_skipped_draws;
            }
            report<<'}';
        }
        report<<"],\"readback_count\":"<<static_cast<unsigned>(shots_[0].has_value())+static_cast<unsigned>(shots_[1].has_value())
            <<",\"serialization_after_live_loop\":true,\"readbacks_in_performance_windows\":false}";
        need(bool(report),"Native stock capture receipt write failed");
    }
public:
    Capture() {
#ifdef _WIN32
        wchar_t* raw{};std::size_t count{};
        if(::_wdupenv_s(&raw,&count,L"HLC_STOCK_SKY_CASE_CAPTURE")!=0)throw std::runtime_error{"Cannot read stock-case capture opt-in"};
        const std::unique_ptr<wchar_t,decltype(&std::free)> owner{raw,&std::free};
        if(!raw)return;directory_=raw;
#else
        const char* raw=std::getenv("HLC_STOCK_SKY_CASE_CAPTURE");if(!raw)return;directory_=raw;
#endif
        need(directory_.is_absolute() && std::filesystem::is_directory(directory_) && std::filesystem::is_empty(directory_),
            "Stock-case screenshots require a new empty absolute task directory");
    }
    ~Capture() noexcept {try{save();}catch(const std::exception& e){std::cerr<<"[stock-case-capture] save_failed="<<e.what()<<'\n';}}
    void observe(hlclient::renderer::opengl::OpenGlRenderer& renderer,
        const hlclient::renderer::RenderScene& scene,hlclient::renderer::RenderExtent extent,
        std::uint64_t frame,bool world_ready,bool controlled_alive) {
        if(directory_.empty() || !world_ready || !scene.static_world || extent.width!=1920 || extent.height!=1080)return;
        const auto& c=scene.camera;
        const double dx=double(c.target.x)-c.position.x,dy=double(c.target.y)-c.position.y,dz=double(c.target.z)-c.position.z;
        const double length=std::sqrt(dx*dx+dy*dy+dz*dz);if(!(length>0))return;
        for(unsigned i=0;i<shots_.size();++i) {
            if(shots_[i])continue;
            const double eye_x=i?640.:0.,fx=i?-576.:0.,fy=i?448.:1.;
            const double norm=std::sqrt(fx*fx+fy*fy);
            const bool match=std::abs(c.position.x-eye_x)<.05 && std::abs(c.position.y)<.05 && std::abs(c.position.z-128.)<.05 &&
                (dx*fx+dy*fy)/(length*norm)>.999999 && std::abs(dz/length)<.0001;
            if(!match){stable_[i]=0;stable_since_[i].reset();continue;}
            const auto now=std::chrono::steady_clock::now();
            if(!stable_since_[i])stable_since_[i]=now;
            // Untimed observation only: let the normal connection/console
            // transition finish before retaining an acceptance screenshot.
            if(++stable_[i]<16 || now-*stable_since_[i]<std::chrono::seconds{10})continue;
            const auto pixels=renderer.observe_framebuffer(extent,scene.clear_color,true);
            need(bool(pixels) && pixels.rgba8.size()==static_cast<std::size_t>(extent.width)*extent.height*4U,
                "Native stock-case backbuffer observation failed");
            Shot s;s.eye={c.position.x,c.position.y,c.position.z};s.target={c.target.x,c.target.y,c.target.z};s.fov=c.vertical_field_of_view_radians;
            s.width=extent.width;s.height=extent.height;s.frame=frame;s.sky=hlc_background_sky_stats();s.controlled=controlled_alive;s.rgba=pixels.rgba8;
            shots_[i]=std::move(s);
        }
    }
};
} // namespace sky_stock_case_capture
