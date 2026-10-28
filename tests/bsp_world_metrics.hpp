#pragma once
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>

namespace hlclient::tests {
// Same harness builds against frozen B6 and the candidate. Missing baseline
// counters remain unknown, never a fabricated zero. Output is after timing.
struct BspWorldMetricSample {
    std::array<std::optional<std::uint64_t>,22> counts;
    std::array<std::optional<double>,4> cpu_ms;
    std::uint64_t path{},fallback{};
    template<class Receipt> void capture(const Receipt& r) {
        path=static_cast<std::uint64_t>(r.path);fallback=static_cast<std::uint64_t>(r.fallback);
        counts[0]=r.considered_surfaces;counts[1]=r.selected_surfaces;
        counts[2]=r.api_draws;counts[3]=r.submitted_triangles;counts[4]=r.tested_bounds;
        cpu_ms[0]=r.selection_preparation_ms;
        if constexpr(requires {r.bsp_visited_nodes;}) {
            counts[5]=r.bsp_visited_nodes;counts[6]=r.bsp_pvs_rejected_nodes;
            counts[7]=r.bsp_frustum_rejected_nodes;counts[8]=r.pvs_rebuilds;
            counts[9]=r.pvs_cache_hits;counts[10]=r.prepared_bytes;
            counts[11]=r.prepared_allocations;counts[12]=r.submitted_surfaces;
            counts[13]=r.submitted_ranges;counts[14]=r.prepared_rebuilds;
            counts[15]=r.pvs_fallback_code;
            cpu_ms[1]=r.command_preparation_ms;cpu_ms[2]=r.submission_ms;
        }
        if constexpr(requires {r.pvs_index_upload_bytes;}) {
            counts[16]=r.pvs_index_upload_bytes;
        }
        if constexpr(requires {r.command_cache_hits;r.command_rebuilds;}) {
            counts[17]=r.command_cache_hits;counts[18]=r.command_rebuilds;
        }
        if constexpr(requires {r.sky_order_moved_batches;r.sky_order_blocked_batches;}) {
            counts[19]=r.sky_order_moved_batches;counts[20]=r.sky_order_blocked_batches;
        }
        if constexpr(requires {r.sky_order_preparation_ms;})cpu_ms[3]=r.sky_order_preparation_ms;
        if constexpr(requires {r.sky_submitted_triangles;})counts[21]=r.sky_submitted_triangles;
    }
    void print(std::size_t frame) const {
        constexpr std::array names{"considered_surfaces","selected_surfaces","api_draws","triangles",
            "tested_bounds","visited_nodes","pvs_rejected_nodes","frustum_rejected_nodes",
            "pvs_rebuilds","pvs_cache_hits","prepared_bytes","prepared_allocations",
            "submitted_surfaces","submitted_ranges","prepared_rebuilds","pvs_fallback_code","pvs_index_upload_bytes",
            "command_cache_hits","command_rebuilds","sky_order_moved_batches","sky_order_blocked_batches","sky_submitted_triangles"};
        constexpr std::array times{"selection_ms","command_ms","submission_ms","sky_order_ms"};
        std::cout<<"[bsp-pvs-sample] {\"sample\":"<<frame<<",\"path\":"<<path<<",\"fallback\":"<<fallback;
        for(std::size_t i=0;i<names.size();++i) {
            std::cout<<",\""<<names[i]<<"\":";
            if(counts[i])std::cout<<*counts[i];else std::cout<<"null";
        }
        for(std::size_t i=0;i<times.size();++i) {
            std::cout<<",\""<<times[i]<<"\":";
            if(cpu_ms[i])std::cout<<*cpu_ms[i];else std::cout<<"null";
        }
        std::cout<<"}\n";
    }
};
}
