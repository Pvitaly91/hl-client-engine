#pragma once
#include "sky_candidate_api.hpp"
#include <cmath>
#include <ostream>

// Called only by the existing final-report sink or an untimed correctness readback.
inline void write_sky_diagnostics_json(std::ostream& out) {
    const auto& d=hlc_background_sky_diagnostics();
    constexpr const char* names[]{"depth_write_mask","depth_range","inherited_depth_function",
        "polygon_mode","color_write_mask","enabled_capability","enabled_clip_distance","query_error"};
    constexpr const char* polygon_shapes[]{"unknown_profile","invalid_or_contradictory",
        "core_one_value","core_redundant_pair","compatibility_pair"};
    const auto number=[&](double n){if(std::isfinite(n))out<<n;else out<<"null";};
    const auto values=[&](const auto& a){out<<'[';bool first=true;for(const auto n:a){if(!first)out<<',';first=false;number(n);}out<<']';};
    out<<"{\"state_failures\":{";
    for(std::size_t i=0;i<d.state_failures.size();++i){if(i)out<<',';out<<'\"'<<names[i]<<"\":"<<d.state_failures[i];}
    out<<"},\"depth_failures\":"<<d.depth_failures<<",\"state_witness_overflow\":"<<d.state_witness_overflow
       <<",\"depth_witness_overflow\":"<<d.depth_witness_overflow
       <<",\"state_witness_capacity\":"<<d.state_witnesses.size()<<",\"depth_witness_capacity\":"<<d.depth_witnesses.size()
       <<",\"state_witnesses\":[";
    for(std::uint32_t i=0;i<d.state_witness_count;++i){if(i)out<<',';const auto& w=d.state_witnesses[i];
        out<<"{\"frame\":"<<w.frame<<",\"failed_predicate\":\""<<names[static_cast<std::size_t>(w.failure)]
           <<"\",\"stage\":\"pre_clear_after_scene_target_begin\",\"depth_function\":"<<w.depth_function
           <<",\"depth_function_expected_old\":[513,515],\"depth_write_mask\":"<<w.depth_write_mask
           <<",\"depth_write_mask_expected\":1,\"color_write_mask\":";values(w.color_write_mask);
        out<<",\"color_write_mask_expected\":[1,1,1,1],\"depth_range\":";values(w.depth_range);
        out<<",\"depth_range_expected\":[0,1],\"polygon_mode\":["<<w.polygon_front<<','<<w.polygon_back
           <<"],\"polygon_mode_initial\":["<<sky_polygon_query_unwritten_first<<','<<sky_polygon_query_unwritten_second
           <<"],\"polygon_mode_interpretation\":\""<<polygon_shapes[static_cast<std::size_t>(w.polygon_query_shape)]
           <<"\",\"context\":"<<w.context<<",\"context_profile_mask\":"<<w.context_profile_mask
           <<",\"context_profile_query_checked\":"<<(w.context_profile_query_checked?"true":"false")
           <<",\"context_version\":["<<w.context_major<<','<<w.context_minor<<"],\"framebuffer\":"<<w.framebuffer
           <<",\"depth_bits\":"<<w.depth_bits<<",\"depth_component\":"<<w.depth_component
           <<",\"depth_object\":"<<w.depth_object<<",\"failed_capability\":"<<w.failed_capability
           <<",\"failed_capability_expected_enabled\":false,\"api_error\":"<<w.api_error<<'}';
    }
    out<<"],\"polygon_query_witness_capacity\":"<<d.polygon_query_witnesses.size()
       <<",\"polygon_query_witnesses\":[";
    for(std::uint32_t i=0;i<d.polygon_query_witness_count;++i){if(i)out<<',';const auto& w=d.polygon_query_witnesses[i];
        out<<"{\"frame\":"<<w.frame<<",\"context\":"<<w.context
           <<",\"context_profile_mask\":"<<w.context_profile_mask
           <<",\"context_profile_query_checked\":"<<(w.context_profile_query_checked?"true":"false")
           <<",\"context_version\":["<<w.context_major<<','<<w.context_minor
           <<"],\"raw\":["<<w.raw_first<<','<<w.raw_second
           <<"],\"initialized\":["<<sky_polygon_query_unwritten_first<<','<<sky_polygon_query_unwritten_second
           <<"],\"second_written\":"<<(w.raw_second!=sky_polygon_query_unwritten_second?"true":"false")
           <<",\"interpretation\":\""<<polygon_shapes[static_cast<std::size_t>(w.shape)]
           <<"\",\"query_group_error\":"<<w.query_group_error
           <<",\"fill_eligible\":"<<(w.fill_eligible?"true":"false")<<'}';
    }
    out<<"],\"polygon_fill_requirement\":\"known_profile; core_shared_mode_fill_and_unwritten_or_identical_second; compatibility_both_fill\""
       <<",\"depth_witnesses\":[";
    const auto depth_witness=[&](const SkyDepthWitness& w){
        out<<"{\"frame\":"<<w.frame<<",\"resource_id\":"<<w.resource_id<<",\"resource_revision\":"<<w.resource_revision
           <<",\"object_kind\":"<<w.object_kind<<",\"model_index\":"<<w.model_index<<",\"instance_index\":"<<w.instance_index
           <<",\"first_index\":"<<w.first_index<<",\"index_count\":"<<w.index_count
           <<",\"actual_range_first\":"<<w.actual_range_first<<",\"actual_range_count\":"<<w.actual_range_count
           <<",\"source_surface_index\":"<<w.source_surface_index
           <<",\"recovered_by_actual_submission\":"<<(w.recovered_by_actual_submission?"true":"false")
           <<",\"corner\":"<<w.corner<<",\"failure\":"<<w.failure
           <<",\"whole_package_bounds\":"<<(w.whole_package_bounds?"true":"false")
           <<",\"exact_submitted_geometry\":"<<(w.exact_submitted_geometry?"true":"false")<<",\"bounds\":";values(w.bounds);
        out<<",\"float_mvp_column_major\":";values(w.mvp);out<<",\"camera_position\":";values(w.camera_position);
        out<<",\"camera_target\":";values(w.camera_target);out<<",\"camera_up\":";values(w.camera_up);
        out<<",\"camera_fov\":";number(w.camera_fov);out<<",\"camera_near\":";number(w.camera_near);
        out<<",\"camera_far\":";number(w.camera_far);out<<",\"width\":"<<w.width<<",\"height\":"<<w.height
           <<",\"depth_bits\":"<<w.depth_bits<<",\"depth_component\":"<<w.depth_component<<",\"framebuffer\":"<<w.framebuffer;
        out<<",\"z\":";number(w.z);out<<",\"w\":";number(w.w);out<<",\"q\":";number(w.q);
        out<<",\"roundoff\":";number(w.roundoff);out<<",\"delta\":";number(w.delta);out<<",\"scale\":";number(w.scale);
        out<<",\"z_magnitude\":";number(w.z_magnitude);out<<",\"w_magnitude\":";number(w.w_magnitude);out<<'}';
    };
    for(std::uint32_t i=0;i<d.depth_witness_count;++i){if(i)out<<',';depth_witness(d.depth_witnesses[i]);}
    out<<"],\"recovered_depth_witnesses\":[";
    for(std::uint32_t i=0;i<d.recovered_depth_witness_count;++i){if(i)out<<',';depth_witness(d.recovered_depth_witnesses[i]);}
    out<<"],\"range_tests\":"<<d.range_tests<<",\"surface_refinements\":"<<d.surface_refinements
       <<",\"prepared_ranges\":"<<d.prepared_ranges<<",\"prepared_frames\":"<<d.prepared_frames
       <<",\"guard_ns\":"<<d.guard_ns<<",\"prepare_ns\":"<<d.prepare_ns
       <<",\"cost_scope\":\"inclusive_render_submission_guard_includes_preparation_do_not_sum\"}";
}
