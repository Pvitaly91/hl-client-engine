#pragma once
#include "sky_candidate_api.hpp"
#include "sky_disable_json.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

// An application-session view over renderer process counters. No sampling flag,
// timer, allocation, environment lookup, or file I/O is required per frame.
// Call begin/end immediately around the same renderer.render invocation.
class SkySessionStats final {
public:
    struct Totals {
        std::uint64_t attempts{},late{},early{},original{},fallback{},bounds_tested{},gl_state_queries{};
        std::array<std::uint64_t,5> reasons{};
        std::array<std::uint64_t,static_cast<std::size_t>(SkyStateFailure::count)> state_reasons{};
        std::uint64_t guard_ns{},prepare_ns{},prepared_frames{},prepared_ranges{},range_tests{},surface_refinements{};
        SkyCandidateStats sky_disable{};
    };
    void begin() noexcept {
        if(pending_)++unpaired_begins_;
        before_=hlc_background_sky_stats();
        const auto& d=hlc_background_sky_diagnostics();
        state_before_=d.state_failures;
        cost_before_={d.guard_ns,d.prepare_ns,d.prepared_frames,d.prepared_ranges,d.range_tests,d.surface_refinements};
        state_witness_begin_=d.state_witness_count;depth_witness_begin_=d.depth_witness_count;
        recovered_witness_begin_=d.recovered_depth_witness_count;
        pending_=true;
    }
    void end(unsigned scope) noexcept {
        if(!pending_){++unpaired_ends_;return;}
        pending_=false;
        if(scope>=totals_.size()){++invalid_scopes_;scope=2;}
        const auto after=hlc_background_sky_stats();
        const auto& d=hlc_background_sky_diagnostics();
        if(after.frames<before_.frames || after.early_frames<before_.early_frames ||
            after.original_frames<before_.original_frames || after.late_frames<before_.late_frames) {
            ++counter_reset_intervals_;return;
        }
        const auto delta=[](std::uint64_t a,std::uint64_t b){return a>=b?a-b:0;};
        auto& t=totals_[scope];
        accumulate_sky_disable_delta(t.sky_disable,after,before_);
        t.attempts+=after.frames-before_.frames;t.late+=after.late_frames-before_.late_frames;
        const auto early=after.early_frames-before_.early_frames;
        const auto original=after.original_frames-before_.original_frames;
        t.early+=early;t.original+=original;
        // Only the explicitly requested legacy depth policy can fall back.
        // Diagnostic omissions occupy the early stage but are not failures.
        if(hlc_background_sky_policy()==BackgroundSkyPolicy::depth)t.fallback+=delta(early,original);
        t.bounds_tested+=delta(after.bounds_tested,before_.bounds_tested);
        t.gl_state_queries+=delta(after.gl_state_queries,before_.gl_state_queries);
        for(std::size_t i=0;i<t.reasons.size();++i)t.reasons[i]+=delta(after.fallback_counts[i],before_.fallback_counts[i]);
        for(std::size_t i=0;i<t.state_reasons.size();++i)t.state_reasons[i]+=delta(d.state_failures[i],state_before_[i]);
        t.guard_ns+=delta(d.guard_ns,cost_before_[0]);t.prepare_ns+=delta(d.prepare_ns,cost_before_[1]);
        t.prepared_frames+=delta(d.prepared_frames,cost_before_[2]);t.prepared_ranges+=delta(d.prepared_ranges,cost_before_[3]);
        t.range_tests+=delta(d.range_tests,cost_before_[4]);t.surface_refinements+=delta(d.surface_refinements,cost_before_[5]);
        // These maps reference immutable first-witness slots in the diagnostics
        // object. Earlier console draws not wrapped here remain unobserved.
        for(auto i=state_witness_begin_;i<d.state_witness_count && i<state_scopes_.size();++i) {
            state_scopes_[i]=scope;state_frames_[i]=d.state_witnesses[i].frame;
        }
        for(auto i=depth_witness_begin_;i<d.depth_witness_count && i<depth_scopes_.size();++i) {
            depth_scopes_[i]=scope;depth_frames_[i]=d.depth_witnesses[i].frame;
        }
        for(auto i=recovered_witness_begin_;i<d.recovered_depth_witness_count && i<recovered_scopes_.size();++i) {
            recovered_scopes_[i]=scope;recovered_frames_[i]=d.recovered_depth_witnesses[i].frame;
        }
    }
    [[nodiscard]] const std::array<Totals,3>& totals() const noexcept {return totals_;}
    void write_json(std::ostream& out) const {
        static constexpr const char* scope_names[]{"loading_console_no_world","controlled_alive","world_not_controlled_alive"};
        constexpr const char* reasons[]{"sky_resource","world_resource_identity","depth_attachment","gl_state","far_depth_bounds"};
        constexpr const char* state_names[]{"depth_write_mask","depth_range","inherited_depth_function","polygon_mode",
            "color_write_mask","enabled_capability","enabled_clip_distance","query_error"};
        out<<"{\"schema\":\"hlc.sky-session-scopes.v1\",\"scope_semantics\":\"one_completed_render_call_classified_by_application_state_not_sampling\""
           <<",\"fallback_semantics\":\"only_explicit_legacy_depth_policy; diagnostic_omission_is_not_fallback\""
           <<",\"pending_render\":"<<(pending_?"true":"false")<<",\"unpaired_begins\":"<<unpaired_begins_
           <<",\"unpaired_ends\":"<<unpaired_ends_<<",\"invalid_scopes\":"<<invalid_scopes_
           <<",\"counter_reset_intervals\":"<<counter_reset_intervals_<<",\"scopes\":{";
        for(std::size_t s=0;s<totals_.size();++s){if(s)out<<',';const auto& t=totals_[s];
            out<<'\"'<<scope_names[s]<<"\":{\"render_attempts\":"<<t.attempts<<",\"late_frames\":"<<t.late
               <<",\"early_frames\":"<<t.early<<",\"original_frames\":"<<t.original<<",\"fallback_frames\":"<<t.fallback
               <<",\"bounds_tested\":"<<t.bounds_tested<<",\"gl_state_queries\":"<<t.gl_state_queries
               <<",\"guard_ns_inclusive\":"<<t.guard_ns<<",\"prepare_ns_nested\":"<<t.prepare_ns
               <<",\"prepared_frames\":"<<t.prepared_frames<<",\"prepared_ranges\":"<<t.prepared_ranges
               <<",\"range_tests\":"<<t.range_tests<<",\"surface_refinements\":"<<t.surface_refinements<<",\"fallback_reasons\":{";
            for(std::size_t i=0;i<t.reasons.size();++i){if(i)out<<',';out<<'\"'<<reasons[i]<<"\":"<<t.reasons[i];}
            out<<"},\"state_subreasons\":{";
            for(std::size_t i=0;i<t.state_reasons.size();++i){if(i)out<<',';out<<'\"'<<state_names[i]<<"\":"<<t.state_reasons[i];}
            out<<"},\"sky_disable\":";write_sky_disable_json(out,t.sky_disable);out<<'}';
        }
        const auto process=hlc_background_sky_stats();
        std::uint64_t observed{},observed_late{},observed_early{};
        for(const auto& t:totals_){observed+=t.attempts;observed_late+=t.late;observed_early+=t.early;}
        out<<"},\"outside_wrapped_live_loop\":{\"classification\":\"process_minus_current_session; may_include_console_startup_teardown_or_previous_sessions; not_claimed_controlled_gameplay\""
           <<",\"render_attempts\":"<<(process.frames>=observed?process.frames-observed:0)
           <<",\"late_frames\":"<<(process.late_frames>=observed_late?process.late_frames-observed_late:0)
           <<",\"early_frames\":"<<(process.early_frames>=observed_early?process.early_frames-observed_early:0)
           <<"},\"witness_scopes\":{";
        const auto emit=[&](const char* name,const auto& scopes,const auto& frames){
            out<<'\"'<<name<<"\":[";bool first=true;
            for(std::size_t i=0;i<scopes.size();++i){if(scopes[i]>=totals_.size())continue;
                if(!first)out<<',';first=false;
                out<<"{\"index\":"<<i<<",\"frame\":"<<frames[i]<<",\"scope\":\""<<scope_names[scopes[i]]<<"\"}";
            }out<<']';
        };
        emit("state",state_scopes_,state_frames_);out<<',';emit("depth",depth_scopes_,depth_frames_);
        out<<',';emit("recovered",recovered_scopes_,recovered_frames_);out<<"}}";
    }
private:
    template<std::size_t N> static constexpr std::array<unsigned,N> unobserved() noexcept {
        std::array<unsigned,N> result{};for(auto& i:result)i=3;return result;
    }
    std::array<Totals,3> totals_{};
    SkyCandidateStats before_{};
    std::array<std::uint64_t,static_cast<std::size_t>(SkyStateFailure::count)> state_before_{};
    std::array<std::uint64_t,6> cost_before_{};
    std::array<unsigned,8> state_scopes_=unobserved<8>();
    std::array<unsigned,16> depth_scopes_=unobserved<16>();
    std::array<unsigned,8> recovered_scopes_=unobserved<8>();
    std::array<std::uint64_t,8> state_frames_{};
    std::array<std::uint64_t,16> depth_frames_{};
    std::array<std::uint64_t,8> recovered_frames_{};
    std::uint32_t state_witness_begin_{},depth_witness_begin_{},recovered_witness_begin_{};
    std::uint64_t unpaired_begins_{},unpaired_ends_{},invalid_scopes_{},counter_reset_intervals_{};
    bool pending_{};
};
