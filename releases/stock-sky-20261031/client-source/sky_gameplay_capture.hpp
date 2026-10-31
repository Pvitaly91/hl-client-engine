#pragma once
// Task-only adaptation of the existing b6 present_probe RAM recorder.
// No renderer/input/pose changes; serialization happens after the live loop.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sky_candidate_api.hpp"
#include "sky_disable_json.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace sky_gameplay_capture {
inline std::uint64_t qpc() noexcept { LARGE_INTEGER v{};QueryPerformanceCounter(&v);return static_cast<std::uint64_t>(v.QuadPart); }
struct Row {
    std::uint64_t frame{},after_swap_qpc{},render_begin_qpc{},render_end_qpc{},swap_begin_qpc{};
    std::array<float,3> eye{},target{};
    float fov{};int width{},height{};
    bool focused{},captured{},active{},alive{};
    std::uint64_t draw_calls{},triangles{},network_generation{},map_generation{};
    SkyCandidateStats sky;
    std::uint64_t guard_ns{},prepare_ns{},prepared_ranges{},range_tests{},surface_refinements{};
    std::optional<double> gpu_ms;
};
class Capture final {
public:
    static constexpr std::size_t capacity=200000;
    Capture() {
        std::array<wchar_t,32768> value{};
        const auto n=GetEnvironmentVariableW(L"HLC_SKY_GAMEPLAY_CAPTURE",value.data(),static_cast<DWORD>(value.size()));
        if(!n)return;
        if(n>=value.size())throw std::runtime_error{"Sky capture path too long"};
        path_=std::filesystem::path{std::wstring{value.data(),n}};
        if(!path_.is_absolute() || std::filesystem::exists(path_) || !std::filesystem::is_directory(path_.parent_path()))
            throw std::runtime_error{"Sky capture requires a new absolute output in an existing task directory"};
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);frequency_=static_cast<std::uint64_t>(frequency.QuadPart);
        rows_=std::make_unique<Row[]>(capacity);
        std::cout<<"[sky-gameplay-capture] enabled=true capacity="<<capacity<<" warmup_seconds=30 measure_seconds=20 drain_seconds=2\n"<<std::flush;
    }
    ~Capture() noexcept {try{save();}catch(...){std::cerr<<"[sky-gameplay-capture] save_failed=true\n";}}
    [[nodiscard]] bool enabled() const noexcept {return static_cast<bool>(rows_);}
    // True asks the existing live loop to finish normally after the fixed window.
    [[nodiscard]] bool observe(Row value) {
        if(!rows_)return false;
        rows_[value.frame%capacity]=std::move(value);++total_;
        const auto& row=rows_[value.frame%capacity];last_frame_=row.frame;
        if(!anchor_ && row.focused && row.captured && row.active && row.alive) {
            anchor_=row.after_swap_qpc;
            std::cout<<std::setprecision(9)<<"[sky-gameplay-capture] anchor_qpc="<<anchor_
                <<" eye="<<row.eye[0]<<','<<row.eye[1]<<','<<row.eye[2]
                <<" target="<<row.target[0]<<','<<row.target[1]<<','<<row.target[2]
                <<" fov="<<row.fov<<" extent="<<row.width<<'x'<<row.height<<'\n'<<std::flush;
        }
        return anchor_ && row.after_swap_qpc>=anchor_+52U*frequency_;
    }
    void gpu(std::uint64_t frame,double milliseconds) noexcept {
        if(!rows_ || !std::isfinite(milliseconds) || milliseconds<0.0)return;
        auto& row=rows_[frame%capacity];
        if(row.frame!=frame){++gpu_unmatched_;return;}
        if(row.gpu_ms){++gpu_duplicates_;return;}
        row.gpu_ms=milliseconds;++gpu_observed_;
    }
private:
    void save() {
        if(!rows_)return;
        std::ofstream out{path_,std::ios::binary|std::ios::out};
        if(!out)throw std::runtime_error{"Cannot save sky gameplay capture"};
        const auto stored=(std::min)(total_,static_cast<std::uint64_t>(capacity));
        out<<std::setprecision(15)<<"{\"schema\":\"hlc.sky-real-client.native-capture.v1\",\"policy\":\""
            <<background_sky_policy_name(hlc_background_sky_policy())<<"\",\"process_id\":"<<GetCurrentProcessId()
            <<",\"qpc_frequency\":"<<frequency_<<",\"anchor_qpc\":"<<anchor_
            <<",\"analysis_start_qpc\":"<<(anchor_?anchor_+30U*frequency_:0)
            <<",\"analysis_end_qpc\":"<<(anchor_?anchor_+50U*frequency_:0)
            <<",\"total_observed\":"<<total_<<",\"overwritten_rows\":"<<(total_-stored)
            <<",\"gpu_observed\":"<<gpu_observed_<<",\"gpu_unmatched\":"<<gpu_unmatched_
            <<",\"gpu_duplicates\":"<<gpu_duplicates_<<",\"timed_file_io\":false"
            <<",\"primary_metric\":\"normal_client_post_swap_return_qpc_cadence\""
            <<",\"cpu_render_scope\":\"entire_existing_render_block_including_guards_and_existing_diagnostic_accounting\""
            <<",\"gpu_scope\":\"existing_bounded_pool_total_elapsed_sparse_not_full_frame_population\",\"rows\":[";
        bool first=true;
        for(std::uint64_t frame=last_frame_>=stored?last_frame_-stored+1:1;frame<=last_frame_;++frame) {
            const auto& r=rows_[frame%capacity];if(r.frame!=frame)continue;
            if(!first)out<<',';first=false;
            out<<"\n{\"frame\":"<<r.frame<<",\"after_swap_qpc\":"<<r.after_swap_qpc
                <<",\"render_begin_qpc\":"<<r.render_begin_qpc<<",\"render_end_qpc\":"<<r.render_end_qpc
                <<",\"swap_begin_qpc\":"<<r.swap_begin_qpc<<",\"eye\":["<<r.eye[0]<<','<<r.eye[1]<<','<<r.eye[2]
                <<"],\"target\":["<<r.target[0]<<','<<r.target[1]<<','<<r.target[2]<<"],\"fov\":"<<r.fov
                <<",\"width\":"<<r.width<<",\"height\":"<<r.height
                <<",\"focused\":"<<(r.focused?"true":"false")<<",\"captured\":"<<(r.captured?"true":"false")
                <<",\"active\":"<<(r.active?"true":"false")<<",\"alive\":"<<(r.alive?"true":"false")
                <<",\"draw_calls_cumulative\":"<<r.draw_calls<<",\"triangles_cumulative\":"<<r.triangles
                <<",\"network_generation\":"<<r.network_generation<<",\"map_generation\":"<<r.map_generation
                <<",\"sky_frames\":"<<r.sky.frames<<",\"sky_late\":"<<r.sky.late_frames
                <<",\"sky_early\":"<<r.sky.early_frames<<",\"sky_original\":"<<r.sky.original_frames
                <<",\"sky_bounds\":"<<r.sky.bounds_tested<<",\"sky_gl_queries\":"<<r.sky.gl_state_queries
                <<",\"guard_ns_inclusive\":"<<r.guard_ns<<",\"prepare_ns_nested\":"<<r.prepare_ns
                <<",\"prepared_ranges\":"<<r.prepared_ranges<<",\"range_tests\":"<<r.range_tests
                <<",\"surface_refinements\":"<<r.surface_refinements
                <<",\"fallback_counts\":[";
            for(std::size_t i=0;i<r.sky.fallback_counts.size();++i){if(i)out<<',';out<<r.sky.fallback_counts[i];}
            out<<"],\"sky_disable\":";write_sky_disable_json(out,r.sky);
            out<<",\"gpu_ms\":";if(r.gpu_ms)out<<*r.gpu_ms;else out<<"null";out<<'}';
        }
        out<<"\n]}\n";out.close();if(!out)throw std::runtime_error{"Incomplete sky capture output"};
    }
    std::filesystem::path path_;std::unique_ptr<Row[]> rows_;
    std::uint64_t frequency_{},anchor_{},total_{},last_frame_{},gpu_observed_{},gpu_unmatched_{},gpu_duplicates_{};
};
}
