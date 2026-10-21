#pragma once

#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace hlclient::app {

// Count-only evidence from the renderer instance owned by one application
// attempt. No GL queries, publication, or filesystem work occurs in observe().
class ExternalP1BranchObservation final {
public:
    static constexpr std::size_t segment_limit=16U;

    void observe(const renderer::opengl::OpenGlSceneTargetStatistics& scene,
            const renderer::opengl::OpenGlImageAdjustmentStatistics& image,
            std::uint64_t network_generation=0U,std::uint64_t map_generation=0U) noexcept {
        const Counters current{scene.direct_adjustment_frames,scene.output_color_blits,
            image.resolved_texture_reads,image.framebuffer_color_copies,
            image.adjusted_frames,image.neutral_frames,scene.resolved_frames};
        const bool reset=observed_ && current.decreased_from(previous_);
        if(reset) {
            ++counter_reset_epoch_;
            ++resets_observed_;
            previous_={};
        }
        const auto delta=current.minus(previous_);
        previous_=current;
        observed_=true;
        ++observed_render_calls_;
        totals_.add(delta);

        const SegmentKey key{scene.requested_samples,scene.actual_samples,
            scene.scene_extent,scene.resolve_extent,image.last_extent,
            scene.cached_default_back_output_compatible,network_generation,map_generation};
        if(!segment_count_ || reset || !(segments_[segment_count_-1U].key==key)) {
            if(segment_count_==segment_limit) {
                for(std::size_t i=1U;i<segment_count_;++i)segments_[i-1U]=segments_[i];
                --segment_count_;
                ++omitted_segments_;
            }
            segments_[segment_count_++]={key,counter_reset_epoch_,{}};
        }
        auto& segment=segments_[segment_count_-1U];
        ++segment.observed_render_calls;
        segment.counters.add(delta);
    }

    [[nodiscard]] std::string encode(std::string_view run_id,bool include_segments=true) const {
        std::ostringstream out;
        out<<"{\"schema\":\"hlclient.external-p1-branch-counters.v1\",\"run_id\":"
            <<core::external_json_string(run_id)
            <<",\"counter_scope\":\"one_OpenGlRenderer_instance_within_one_application_attempt;counter_epoch_0_after_attempt_reset;segments_carry_network_and_map_generation\""
            <<",\"observation_status\":\""<<(observed_?"observed":"unknown")<<'"'
            <<",\"counter_reset_epoch\":"<<counter_reset_epoch_
            <<",\"resets_observed\":"<<resets_observed_
            <<",\"reset_epoch_semantics\":\"increments_when_renderer_cumulative_counters_decrease\""
            <<",\"observation_semantics\":\"one_completed_external_render_call_per_observation_not_unique_GL_frame_id\""
            <<",\"observed_render_calls\":"<<observed_render_calls_
            <<",\"segment_limit\":"<<segment_limit
            <<",\"omitted_segments\":"<<omitted_segments_
            <<",\"totals\":";
        if(observed_)encode_counters(out,totals_);
        else out<<"null";
        out<<",\"current_mode\":";
        if(observed_) {
            const auto& key=segments_[segment_count_-1U].key;
            out<<"{\"requested_samples\":"<<key.requested_samples
                <<",\"actual_samples\":"<<key.actual_samples
                <<",\"network_generation\":"<<key.network_generation
                <<",\"map_generation\":"<<key.map_generation;
            encode_extent(out,"scene",key.scene);
            encode_extent(out,"resolve",key.resolve);
            encode_extent(out,"postprocess",key.postprocess);
            out<<",\"cached_default_back_output_compatible\":";
            if(key.cached_default_back_output_compatible)
                out<<(*key.cached_default_back_output_compatible?"true":"false");
            else out<<"null";
            out<<'}';
        } else out<<"null";
        if(include_segments) {
            out<<",\"segments\":[";
            for(std::size_t i=0U;i<segment_count_;++i) {
                if(i)out<<',';
                const auto& segment=segments_[i];
                out<<"{\"counter_reset_epoch\":"<<segment.counter_reset_epoch
                    <<",\"requested_samples\":"<<segment.key.requested_samples
                    <<",\"actual_samples\":"<<segment.key.actual_samples
                    <<",\"network_generation\":"<<segment.key.network_generation
                    <<",\"map_generation\":"<<segment.key.map_generation;
                encode_extent(out,"scene",segment.key.scene);
                encode_extent(out,"resolve",segment.key.resolve);
                encode_extent(out,"postprocess",segment.key.postprocess);
                out<<",\"cached_default_back_output_compatible\":";
                if(segment.key.cached_default_back_output_compatible)
                    out<<(*segment.key.cached_default_back_output_compatible?"true":"false");
                else out<<"null";
                out<<",\"observed_render_calls\":"<<segment.observed_render_calls
                    <<",\"counters\":";
                encode_counters(out,segment.counters);
                out<<'}';
            }
            out<<']';
        }
        out<<'}';
        return out.str();
    }

private:
    struct Counters {
        std::uint64_t direct_adjustment_frames{},output_color_blits{};
        std::uint64_t resolved_texture_reads{},framebuffer_copies{};
        std::uint64_t adjusted_frames{},neutral_frames{},resolved_frames{};

        [[nodiscard]] bool decreased_from(const Counters& other) const noexcept {
            return direct_adjustment_frames<other.direct_adjustment_frames ||
                output_color_blits<other.output_color_blits ||
                resolved_texture_reads<other.resolved_texture_reads ||
                framebuffer_copies<other.framebuffer_copies ||
                adjusted_frames<other.adjusted_frames ||
                neutral_frames<other.neutral_frames ||
                resolved_frames<other.resolved_frames;
        }
        [[nodiscard]] Counters minus(const Counters& other) const noexcept {
            return {direct_adjustment_frames-other.direct_adjustment_frames,
                output_color_blits-other.output_color_blits,
                resolved_texture_reads-other.resolved_texture_reads,
                framebuffer_copies-other.framebuffer_copies,
                adjusted_frames-other.adjusted_frames,
                neutral_frames-other.neutral_frames,
                resolved_frames-other.resolved_frames};
        }
        void add(const Counters& other) noexcept {
            direct_adjustment_frames+=other.direct_adjustment_frames;
            output_color_blits+=other.output_color_blits;
            resolved_texture_reads+=other.resolved_texture_reads;
            framebuffer_copies+=other.framebuffer_copies;
            adjusted_frames+=other.adjusted_frames;
            neutral_frames+=other.neutral_frames;
            resolved_frames+=other.resolved_frames;
        }
    };
    struct SegmentKey {
        unsigned requested_samples{},actual_samples{};
        renderer::RenderExtent scene{},resolve{},postprocess{};
        std::optional<bool> cached_default_back_output_compatible{};
        std::uint64_t network_generation{},map_generation{};
        friend bool operator==(const SegmentKey&,const SegmentKey&)=default;
    };
    struct Segment {
        SegmentKey key{};
        std::uint64_t counter_reset_epoch{};
        Counters counters{};
        std::uint64_t observed_render_calls{};
    };
    static void encode_extent(std::ostringstream& out,std::string_view name,
            renderer::RenderExtent extent) {
        out<<",\""<<name<<"_width\":"<<extent.width
            <<",\""<<name<<"_height\":"<<extent.height;
    }
    static void encode_counters(std::ostringstream& out,const Counters& counters) {
        out<<"{\"direct_adjustment_frames\":"<<counters.direct_adjustment_frames
            <<",\"output_color_blits\":"<<counters.output_color_blits
            <<",\"resolved_texture_reads\":"<<counters.resolved_texture_reads
            <<",\"framebuffer_copies\":"<<counters.framebuffer_copies
            <<",\"adjusted_frames\":"<<counters.adjusted_frames
            <<",\"neutral_frames\":"<<counters.neutral_frames
            <<",\"resolved_frames\":"<<counters.resolved_frames<<'}';
    }

    bool observed_{};
    Counters previous_{},totals_{};
    std::array<Segment,segment_limit> segments_{};
    std::size_t segment_count_{},omitted_segments_{};
    std::uint64_t counter_reset_epoch_{},resets_observed_{},observed_render_calls_{};
};

// Movement report metadata is bounded by its existing reserve. Prefer full
// per-mode attribution; on overflow retain totals/scope and mark omissions.
[[nodiscard]] inline std::string external_p1_movement_metadata(
        const ExternalP1BranchObservation& observation,std::string_view run_id,
        std::string_view diagnostic_capabilities,std::size_t budget) {
    const auto full=observation.encode(run_id);
    const auto compact=observation.encode(run_id,false);
    std::string metadata=",\"p1_branch_counters\":"+full;
    if(!diagnostic_capabilities.empty())
        metadata+=",\"game_diagnostic_capabilities\":"+std::string{diagnostic_capabilities}+
            ",\"launch_manifest_reference\":\"launch-manifest.json\",\"process_manifest_reference\":\"process-manifest.json\"";
    if(metadata.size()<=budget)return metadata;
    metadata=",\"p1_branch_counters\":"+full;
    if(!diagnostic_capabilities.empty())
        metadata+=",\"game_diagnostic_capabilities_omitted\":\"publication_metadata_budget_exceeded\"";
    if(metadata.size()<=budget)return metadata;
    metadata=",\"p1_branch_counters\":"+compact+
        ",\"p1_branch_segments_omitted\":\"publication_metadata_budget_exceeded\"";
    if(metadata.size()<=budget)return metadata;
    metadata=",\"p1_branch_counters_omitted\":\"publication_metadata_budget_exceeded\"";
    return metadata.size()<=budget ? metadata : std::string{};
}

} // namespace hlclient::app
