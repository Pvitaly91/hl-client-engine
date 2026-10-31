#pragma once
#include "sky_candidate_api.hpp"
#include <ostream>

// Bounded counter-only observations. Last-frame fields are snapshots, not sums.
inline void accumulate_sky_disable_delta(SkyCandidateStats& total,
    const SkyCandidateStats& after,const SkyCandidateStats& before) noexcept {
    const auto delta=[](std::uint64_t a,std::uint64_t b){return a>=b?a-b:0;};
    total.fullscreen_sky_draws+=delta(after.fullscreen_sky_draws,before.fullscreen_sky_draws);
    total.fullscreen_sky_skipped_frames+=delta(after.fullscreen_sky_skipped_frames,before.fullscreen_sky_skipped_frames);
    total.finite_sky_textured_draws+=delta(after.finite_sky_textured_draws,before.finite_sky_textured_draws);
    total.finite_sky_constant_draws+=delta(after.finite_sky_constant_draws,before.finite_sky_constant_draws);
    total.finite_sky_coverage_alpha_draws+=delta(after.finite_sky_coverage_alpha_draws,before.finite_sky_coverage_alpha_draws);
    total.non_sky_world_draws+=delta(after.non_sky_world_draws,before.non_sky_world_draws);
    total.static_world_sky_skipped_draws+=delta(after.static_world_sky_skipped_draws,before.static_world_sky_skipped_draws);
    total.static_world_sky_skipped_frames+=delta(after.static_world_sky_skipped_frames,before.static_world_sky_skipped_frames);
    total.unsupported_brush_sky_draws+=delta(after.unsupported_brush_sky_draws,before.unsupported_brush_sky_draws);
    total.static_world_sky_unsupported_draws+=delta(after.static_world_sky_unsupported_draws,before.static_world_sky_unsupported_draws);
    total.static_world_sky_missing_background_frames+=delta(after.static_world_sky_missing_background_frames,before.static_world_sky_missing_background_frames);
    total.last_frame_world_draws=after.last_frame_world_draws;
    total.world_draw_order_hash=after.world_draw_order_hash;
    total.last_frame_clear_color=after.last_frame_clear_color;
}
inline void write_sky_disable_json(std::ostream& out,const SkyCandidateStats& s) {
    out<<"{\"schema\":\"hlc.sky-disable.counters.v1\""
       <<",\"fullscreen_sky_draws\":"<<s.fullscreen_sky_draws
       <<",\"fullscreen_sky_skipped_frames\":"<<s.fullscreen_sky_skipped_frames
       <<",\"finite_sky_textured_draws\":"<<s.finite_sky_textured_draws
       <<",\"finite_sky_constant_draws\":"<<s.finite_sky_constant_draws
       <<",\"finite_sky_coverage_alpha_draws\":"<<s.finite_sky_coverage_alpha_draws
       <<",\"non_sky_world_draws\":"<<s.non_sky_world_draws
       <<",\"static_world_sky_skipped_draws\":"<<s.static_world_sky_skipped_draws
       <<",\"static_world_sky_skipped_frames\":"<<s.static_world_sky_skipped_frames
       <<",\"unsupported_brush_sky_draws\":"<<s.unsupported_brush_sky_draws
       <<",\"static_world_sky_unsupported_draws\":"<<s.static_world_sky_unsupported_draws
       <<",\"static_world_sky_missing_background_frames\":"<<s.static_world_sky_missing_background_frames
       <<",\"last_frame_world_draws\":"<<s.last_frame_world_draws
       <<",\"world_draw_order_hash\":"<<s.world_draw_order_hash
       <<",\"last_frame_clear_color\":[";
    for(std::size_t i=0;i<s.last_frame_clear_color.size();++i){if(i)out<<',';out<<s.last_frame_clear_color[i];}
    out<<"],\"last_frame_fields_are_not_cumulative\":true}";
}
