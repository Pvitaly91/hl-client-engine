#include <hlclient/app/movement_session_diagnostics.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#include <hlclient/core/external_launch_config.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>

namespace hlclient::app {
namespace {
constexpr std::size_t metric_count=18,cohort_count=4;
constexpr std::size_t gpu_metric=11,correction_metric=13,camera_metric=14,fallback_error_metric=17;
constexpr std::uint64_t initial_bucket_us=1'000'000,maximum_attributed_interval_us=250'000;
constexpr std::array<std::string_view,cohort_count> cohort_names{"controlled_alive","background","inactive_or_dead","unknown"};
constexpr std::array<std::string_view,metric_count> metric_names{
    "frame_interval_ms","cpu_update_ms","network_ms","parse_commit_ms","prediction_ms","collision_ms",
    "studio_ms","asset_ms","render_ms","present_ms","report_ms","gpu_ms","state_age_ms",
    "raw_correction_on_ordinal_sample_units","camera_jump_snapshot_on_ordinal_units","replay_duration_ms",
    "fallback_duration_snapshot_ms","raw_error_on_fallback_entry_units"};
constexpr std::array<double,31> histogram_bounds{0,.25,.5,1,2,4,6,8,10,12,14,16.667,20,25,33.334,40,50,
    66.667,100,150,200,300,500,1000,2000,5000,10000,30000,60000,1000000,1000000000};
using Record=MovementFrameRecord;
constexpr std::array<std::optional<double> Record::*,17> metric_members{
    &Record::frame_interval_ms,&Record::cpu_update_ms,&Record::network_ms,&Record::parse_commit_ms,
    &Record::prediction_ms,&Record::collision_ms,&Record::studio_ms,&Record::asset_ms,&Record::render_ms,
    &Record::present_ms,&Record::report_ms,&Record::gpu_ms,&Record::state_age_ms,&Record::raw_correction_units,
    &Record::camera_jump_units,&Record::replay_duration_ms,&Record::fallback_duration_ms};
std::uint64_t add(const std::uint64_t a,const std::uint64_t b) noexcept {
    return b>std::numeric_limits<std::uint64_t>::max()-a?std::numeric_limits<std::uint64_t>::max():a+b;
}
std::size_t cohort(const MovementFrameContext& context) noexcept {
    if(context.focused==false)return 1;
    if(context.captured==false || context.alive==false || context.control_active==false)return 2;
    if(context.focused==true && context.captured==true && context.alive==true && context.control_active==true)return 0;
    return 3;
}
struct Metric final {
    std::uint64_t observed{};
    double sum{},maximum{};
    void observe(const double value) noexcept {++observed;sum+=value;maximum=std::max(maximum,value);}
    void merge(const Metric& other) noexcept {observed=add(observed,other.observed);sum+=other.sum;maximum=std::max(maximum,other.maximum);}
};
struct Histogram final {
    Metric metric;
    std::uint64_t not_available{};
    std::array<std::uint64_t,histogram_bounds.size()> buckets{};
    void observe(const std::optional<double> value) noexcept {
        if(!value){++not_available;return;}
        metric.observe(*value);
        const auto it=std::lower_bound(histogram_bounds.begin(),histogram_bounds.end(),*value);
        ++buckets[static_cast<std::size_t>(it-histogram_bounds.begin())];
    }
    std::optional<double> quantile(const unsigned percent) const noexcept {
        if(!metric.observed)return {};
        const auto rank=(metric.observed/100)*percent+((metric.observed%100)*percent+99)/100;
        std::uint64_t count{};
        for(std::size_t i=0;i<buckets.size();++i){count=add(count,buckets[i]);if(count>=rank)return histogram_bounds[i];}
        return {};
    }
};
struct Counts final {
    std::uint64_t frames{},prediction_active_frames{},prediction_inactive_frames{},prediction_unknown_frames{};
    std::uint64_t fallback_entries{},correction_samples{},correction_ordinal_advances_unmeasured{};
    std::uint64_t frame_over20_ms{},frame_over33_3_ms{},frame_over50_ms{};
    std::uint64_t startsolid_frames{},allsolid_frames{},committed_records{},replayed_commands{},bytes{};
    std::uint64_t fallback_observed_interval_us{};
    void merge(const Counts& other) noexcept {
#define HLC_SUM(name) name=add(name,other.name)
        HLC_SUM(frames);HLC_SUM(prediction_active_frames);HLC_SUM(prediction_inactive_frames);HLC_SUM(prediction_unknown_frames);
        HLC_SUM(fallback_entries);HLC_SUM(correction_samples);HLC_SUM(correction_ordinal_advances_unmeasured);
        HLC_SUM(frame_over20_ms);HLC_SUM(frame_over33_3_ms);HLC_SUM(frame_over50_ms);HLC_SUM(startsolid_frames);HLC_SUM(allsolid_frames);
        HLC_SUM(committed_records);HLC_SUM(replayed_commands);HLC_SUM(bytes);HLC_SUM(fallback_observed_interval_us);
#undef HLC_SUM
    }
};
struct Aggregate final {Counts counts;std::array<Histogram,metric_count> metrics;};
struct Bin final {
    Counts counts,controlled;
    std::array<Metric,metric_count> metrics;
    std::array<std::uint64_t,cohort_count> cohort_frames{};
    std::uint64_t first_us{},last_us{},generation_boundaries{},datagrams_delta{},rejected_sequences_delta{},choke_markers_delta{};
    std::uint64_t manual_markers{},first_manual_marker_us{},last_manual_marker_us{};
    void merge(const Bin& other) noexcept {
        if(!other.counts.frames && !other.manual_markers)return;
        if(other.counts.frames){if(!counts.frames)first_us=other.first_us;
            else first_us=std::min(first_us,other.first_us);}
        if(other.manual_markers){if(!manual_markers)first_manual_marker_us=other.first_manual_marker_us;
            else first_manual_marker_us=std::min(first_manual_marker_us,other.first_manual_marker_us);
            last_manual_marker_us=std::max(last_manual_marker_us,other.last_manual_marker_us);}
        manual_markers=add(manual_markers,other.manual_markers);
        last_us=std::max(last_us,other.last_us);counts.merge(other.counts);controlled.merge(other.controlled);
        for(std::size_t i=0;i<metrics.size();++i)metrics[i].merge(other.metrics[i]);
        for(std::size_t i=0;i<cohort_frames.size();++i)cohort_frames[i]=add(cohort_frames[i],other.cohort_frames[i]);
        generation_boundaries=add(generation_boundaries,other.generation_boundaries);
        datagrams_delta=add(datagrams_delta,other.datagrams_delta);
        rejected_sequences_delta=add(rejected_sequences_delta,other.rejected_sequences_delta);
        choke_markers_delta=add(choke_markers_delta,other.choke_markers_delta);
    }
};
struct Counter final {
    std::optional<std::uint64_t> previous;
    std::uint64_t delta{},observed{},missing{},baselines{},resets{};
    std::uint64_t observe(const std::optional<std::uint64_t> value,const bool boundary) noexcept {
        if(boundary)previous.reset();
        if(!value){++missing;return 0;}
        ++observed;std::uint64_t increment{};
        if(!previous)++baselines;
        else if(*value<*previous)++resets;
        else increment=*value-*previous;
        previous=value;delta=add(delta,increment);return increment;
    }
};
struct Reason final {std::array<char,96> token{};std::uint64_t frames{},controlled_frames{};};
struct Reasons final {
    std::array<Reason,16> entries{};
    std::uint64_t overflow_frames{};
    template<std::size_t N>void observe(const std::array<char,N>& input,const std::size_t sample_cohort) noexcept {
        std::array<char,96> token{};std::size_t length{};
        for(const char c:input){if(!c || length==token.size()-1)break;
            if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-'))return;
            token[length++]=c;}
        if(!length){constexpr std::string_view absent="not_observed";std::copy(absent.begin(),absent.end(),token.begin());}
        for(auto& entry:entries)if(!entry.frames || entry.token==token){entry.token=token;++entry.frames;
            if(sample_cohort==0)++entry.controlled_frames;return;}
        ++overflow_frames;
    }
};
struct GpuIdentity final {std::uint64_t frame{},time{};std::uint8_t cohort{};bool occupied{},seen{};};
enum class PacingEnd : std::uint8_t {observing,recovered,context_boundary,generation_boundary,life_boundary,missing_interval,frame_gap};
struct PacingEpisode final {
    std::uint64_t ordinal{},begin_us{},end_us{},first_frame{},last_frame{},worst_frame{},network{},map{},slow_frames{};
    double total_frame_ms{},maximum_frame_ms{};
    std::array<std::optional<double>,12> worst_timings{};
    std::optional<std::uint32_t> players,entities;
    std::optional<bool> prediction_active;
    PacingEnd end{PacingEnd::observing};
};
struct FramePacing final {
    std::array<PacingEpisode,MovementSessionRecorder::maximum_frame_pacing_episodes> episodes{};
    PacingEpisode active{};
    std::size_t retained{};
    std::uint64_t started{},slow_frames{};
    double total_slow_frame_ms{};
    static bool worse(const PacingEpisode& a,const PacingEpisode& b) noexcept {
        if(a.maximum_frame_ms!=b.maximum_frame_ms)return a.maximum_frame_ms>b.maximum_frame_ms;
        if(a.total_frame_ms!=b.total_frame_ms)return a.total_frame_ms>b.total_frame_ms;
        return a.ordinal<b.ordinal;
    }
    void keep(const PacingEpisode& episode) noexcept {
        if(retained<episodes.size()){episodes[retained++]=episode;return;}
        auto least=std::min_element(episodes.begin(),episodes.end(),[](const auto& a,const auto& b){return worse(b,a);});
        if(worse(episode,*least))*least=episode;
    }
    void close(const PacingEnd reason) noexcept {
        if(!active.ordinal)return;active.end=reason;keep(active);active={};
    }
    void observe(const Record& frame,const bool eligible,const std::uint64_t previous_us,const PacingEnd boundary) noexcept {
        if(!eligible){close(boundary);return;}
        if(*frame.frame_interval_ms<=MovementSessionRecorder::frame_pacing_threshold_ms){close(PacingEnd::recovered);return;}
        ++slow_frames;total_slow_frame_ms+=*frame.frame_interval_ms;
        if(!active.ordinal){active.ordinal=++started;active.begin_us=previous_us;active.first_frame=frame.frame_number;
            active.network=frame.network_generation;active.map=frame.map_generation;}
        active.end_us=frame.monotonic_us;active.last_frame=frame.frame_number;++active.slow_frames;
        active.total_frame_ms+=*frame.frame_interval_ms;
        if(*frame.frame_interval_ms>active.maximum_frame_ms){
            active.maximum_frame_ms=*frame.frame_interval_ms;active.worst_frame=frame.frame_number;
            for(std::size_t i=0;i<active.worst_timings.size();++i)active.worst_timings[i]=frame.*metric_members[i];
            active.players=frame.observed_players;active.entities=frame.entities;active.prediction_active=frame.prediction_active;
        }
    }
    void gpu(const std::uint64_t frame,const double value) noexcept {
        if(active.ordinal && active.worst_frame==frame)active.worst_timings[gpu_metric]=value;
        for(std::size_t i=0;i<retained;++i)if(episodes[i].worst_frame==frame)episodes[i].worst_timings[gpu_metric]=value;
    }
};
template<typename T>std::string scalar(const T value) {
    std::array<char,64> buffer{};const auto result=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value);
    return result.ec==std::errc{}?std::string(buffer.data(),result.ptr):"null";
}
std::string scalar(const bool value){return value?"true":"false";}
template<typename T>std::string scalar(const std::optional<T> value){return value?scalar(*value):"null";}
template<typename T>void field(std::string& output,const std::string_view name,const T value) {
    output+=',';output+=core::external_json_string(name);output+=':';output+=scalar(value);
}
std::string encode_counts(const Counts& counts) {
    std::string output="{\"frames\":"+scalar(counts.frames);
#define HLC_FIELD(name) field(output,#name,counts.name)
    HLC_FIELD(prediction_active_frames);HLC_FIELD(prediction_inactive_frames);HLC_FIELD(prediction_unknown_frames);
    HLC_FIELD(fallback_entries);HLC_FIELD(correction_samples);HLC_FIELD(correction_ordinal_advances_unmeasured);
    HLC_FIELD(frame_over20_ms);HLC_FIELD(frame_over33_3_ms);HLC_FIELD(frame_over50_ms);
    HLC_FIELD(startsolid_frames);HLC_FIELD(allsolid_frames);HLC_FIELD(committed_records);HLC_FIELD(replayed_commands);HLC_FIELD(bytes);
    HLC_FIELD(fallback_observed_interval_us);
#undef HLC_FIELD
    output+='}';return output;
}
std::string encode_metric(const Metric& metric) {
    std::string output="{\"observed\":"+scalar(metric.observed);
    field(output,"mean",metric.observed?std::optional{metric.sum/static_cast<double>(metric.observed)}:std::optional<double>{});
    field(output,"maximum",metric.observed?std::optional{metric.maximum}:std::optional<double>{});return output;
}
std::string encode_aggregate(const Aggregate& aggregate) {
    std::string output="{\"counts\":"+encode_counts(aggregate.counts)+",\"metrics\":{";
    for(std::size_t i=0;i<metric_count;++i){if(i)output+=',';const auto& histogram=aggregate.metrics[i];
        output+=core::external_json_string(metric_names[i])+':'+encode_metric(histogram.metric);
        field(output,"not_available",histogram.not_available);field(output,"p50_upper_bound",histogram.quantile(50));
        field(output,"p95_upper_bound",histogram.quantile(95));field(output,"p99_upper_bound",histogram.quantile(99));output+='}';}
    output+="}}";return output;
}
std::string encode_counter(const Counter& counter) {
    std::string output="{\"delta\":"+scalar(counter.delta);field(output,"observed_frames",counter.observed);
    field(output,"missing_frames",counter.missing);field(output,"baselines",counter.baselines);field(output,"resets",counter.resets);
    output+='}';return output;
}
std::string encode_reasons(const Reasons& reasons) {
    std::string output="{\"maximum_categories\":16,\"overflow_frames\":"+scalar(reasons.overflow_frames)+",\"entries\":[";bool first=true;
    for(const auto& entry:reasons.entries){if(!entry.frames)continue;if(!first)output+=',';first=false;
        output+="{\"reason\":"+core::external_json_string(entry.token.data());field(output,"frames",entry.frames);
        field(output,"controlled_alive_frames",entry.controlled_frames);output+='}';}
    output+="]}";return output;
}
std::string encode_pacing(const FramePacing& source) {
    auto retained=source;retained.close(PacingEnd::observing);
    std::sort(retained.episodes.begin(),retained.episodes.begin()+retained.retained,FramePacing::worse);
    constexpr std::array<std::string_view,7> endings{"recording_end_or_still_active","recovered","context_boundary",
        "generation_boundary","life_boundary","missing_interval","frame_number_gap"};
    std::string output="{\"schema\":\"hlclient.frame-pacing.v1\",\"scope\":\"controlled_alive_contiguous_endpoint_context\",";
    output+="\"definition\":\"absolute_long_frames_not_drop_from_target_fps\",\"selection\":\"worst_peak_frame_then_duration_then_first_ordinal\"";
    field(output,"threshold_ms",MovementSessionRecorder::frame_pacing_threshold_ms);
    field(output,"maximum_episodes",MovementSessionRecorder::maximum_frame_pacing_episodes);
    field(output,"total_episodes",source.started);field(output,"retained_episodes",retained.retained);
    field(output,"omitted_episodes",source.started-retained.retained);field(output,"slow_frames",source.slow_frames);
    field(output,"slow_frame_interval_total_ms",source.total_slow_frame_ms);
    output+=",\"timing_semantics\":\"observed_frame_intervals_and_original_worst_frame_stages_overlap_do_not_sum\",";
    output+="\"causality\":\"observations_not_root_cause_proof\",\"episodes\":[";
    for(std::size_t i=0;i<retained.retained;++i){if(i)output+=',';const auto& episode=retained.episodes[i];
        output+="{\"ordinal\":"+scalar(episode.ordinal);field(output,"begin_us",episode.begin_us);field(output,"end_us",episode.end_us);
        field(output,"first_frame",episode.first_frame);field(output,"last_frame",episode.last_frame);field(output,"worst_frame",episode.worst_frame);
        field(output,"network_generation",episode.network);field(output,"map_generation",episode.map);field(output,"slow_frames",episode.slow_frames);
        field(output,"span_ms",static_cast<double>(episode.end_us-episode.begin_us)/1000.0);
        field(output,"frame_interval_total_ms",episode.total_frame_ms);field(output,"maximum_frame_ms",episode.maximum_frame_ms);
        field(output,"slow_frames_mean_fps_equivalent",1000.0*static_cast<double>(episode.slow_frames)/episode.total_frame_ms);
        field(output,"slowest_frame_fps_equivalent",1000.0/episode.maximum_frame_ms);
        field(output,"recovery_observed",episode.end==PacingEnd::recovered);
        output+=",\"end_status\":"+core::external_json_string(endings[static_cast<std::size_t>(episode.end)]);
        field(output,"observed_players",episode.players);field(output,"entities",episode.entities);field(output,"prediction_active",episode.prediction_active);
        output+=",\"worst_frame_timings\":{";
        for(std::size_t timing=0;timing<episode.worst_timings.size();++timing){if(timing)output+=',';
            output+=core::external_json_string(metric_names[timing])+':'+scalar(episode.worst_timings[timing]);}
        output+="}}";
    }
    output+="]}";return output;
}
} // namespace

struct MovementSessionRecorder::Impl final {
    Aggregate totals;
    std::array<Aggregate,cohort_count> cohorts;
    std::array<Bin,maximum_timeline_bins> bins;
    std::array<GpuIdentity,maximum_gpu_identities> gpu_identities;
    FramePacing pacing;
    Reasons fallback_reasons,presentation_reasons;
    Counter datagrams,rejected_sequences,choke_markers;
    std::optional<std::uint64_t> first_us,last_us,last_frame,correction_ordinal,life_epoch;
    std::optional<bool> previous_prediction_active;
    std::uint64_t network_generation{},map_generation{},bucket_width_us{initial_bucket_us},compactions{};
    std::uint64_t generation_boundaries{},life_boundaries{},unattributed_interval_us{};
    std::uint64_t correction_ordinal_resets{},correction_identity_missing_frames{},initial_nonzero_correction_ordinals{};
    std::uint64_t initial_inactive_observations{},gpu_observations{},gpu_duplicates{},gpu_unmatched{},gpu_invalid{};
    std::uint64_t committed_records_observed_frames{},replayed_commands_observed_frames{},bytes_observed_frames{};
    std::uint64_t duplicate_or_unordered_frames{};
    std::uint64_t manual_markers{},manual_markers_rejected{};
    std::uint64_t frame_interval_context_unattributed{};
    std::size_t previous_cohort{},gpu_next{};

    std::size_t bin_index(const std::uint64_t time) noexcept {
        const auto elapsed=time-*first_us;
        while(elapsed/bucket_width_us>=maximum_timeline_bins) {
            for(std::size_t i=0;i<maximum_timeline_bins/2;++i){
                if(i==0)bins[i].merge(bins[1]);
                else {bins[i]=bins[i*2];bins[i].merge(bins[i*2+1]);}
            }
            for(std::size_t i=maximum_timeline_bins/2;i<maximum_timeline_bins;++i)bins[i]=Bin{};
            bucket_width_us*=2;++compactions;
        }
        return static_cast<std::size_t>(elapsed/bucket_width_us);
    }
    void metric(const std::size_t index,const std::optional<double> value,const std::size_t sample_cohort,Bin& bin) noexcept {
        totals.metrics[index].observe(value);cohorts[sample_cohort].metrics[index].observe(value);
        if(value)bin.metrics[index].observe(*value);
    }
};

MovementSessionRecorder::MovementSessionRecorder(const bool enabled) noexcept:requested_(enabled) {
    static_assert(sizeof(Impl)<=maximum_capacity_bytes);
    if(enabled)impl_.reset(new(std::nothrow) Impl{});
}
MovementSessionRecorder::~MovementSessionRecorder()=default;
MovementSessionRecorder::MovementSessionRecorder(MovementSessionRecorder&&) noexcept=default;
MovementSessionRecorder& MovementSessionRecorder::operator=(MovementSessionRecorder&&) noexcept=default;
bool MovementSessionRecorder::enabled() const noexcept{return impl_!=nullptr;}
std::size_t MovementSessionRecorder::capacity_bytes() const noexcept{return impl_?sizeof(Impl):0;}
void MovementSessionRecorder::observe(const Record& frame,const MovementFrameContext& context) noexcept {
    if(!impl_)return;auto& state=*impl_;
    if(state.last_us && (frame.monotonic_us<*state.last_us || frame.frame_number<=*state.last_frame)){
        ++state.duplicate_or_unordered_frames;return;}
    const auto sample_cohort=cohort(context);
    const bool network_boundary=state.last_us && state.network_generation!=frame.network_generation;
    const bool generation_boundary=network_boundary || (state.last_us && state.map_generation!=frame.map_generation);
    const bool life_boundary=state.life_epoch && context.life_epoch && state.life_epoch!=context.life_epoch;
    if(generation_boundary)++state.generation_boundaries;
    if(life_boundary)++state.life_boundaries;
    if(network_boundary)state.correction_ordinal.reset();
    if(generation_boundary || life_boundary)state.previous_prediction_active.reset();
    if(!state.first_us)state.first_us=frame.monotonic_us;
    auto& bin=state.bins[state.bin_index(frame.monotonic_us)];
    if(!bin.counts.frames)bin.first_us=frame.monotonic_us;bin.last_us=frame.monotonic_us;
    if(generation_boundary)++bin.generation_boundaries;
    Counts counts;counts.frames=1;
    if(frame.prediction_active==true)counts.prediction_active_frames=1;
    else if(frame.prediction_active==false)counts.prediction_inactive_frames=1;
    else counts.prediction_unknown_frames=1;
    const bool fallback_entry=frame.prediction_active==false && state.previous_prediction_active==true && !generation_boundary && !life_boundary;
    if(fallback_entry)counts.fallback_entries=1;
    if(frame.prediction_active==false && !state.previous_prediction_active)++state.initial_inactive_observations;
    if(state.last_us){const auto interval=frame.monotonic_us-*state.last_us;
        if(!generation_boundary && !life_boundary && sample_cohort==state.previous_cohort && interval<=maximum_attributed_interval_us) {
            if(frame.prediction_active==false && state.previous_prediction_active==false)counts.fallback_observed_interval_us=interval;
        } else state.unattributed_interval_us=add(state.unattributed_interval_us,interval);}
    bool correction_sample{};
    if(frame.correction_ordinal) {
        if(!state.correction_ordinal){if(*frame.correction_ordinal)++state.initial_nonzero_correction_ordinals;}
        else if(*frame.correction_ordinal>*state.correction_ordinal){correction_sample=true;
            counts.correction_ordinal_advances_unmeasured=*frame.correction_ordinal-*state.correction_ordinal-1;}
        else if(*frame.correction_ordinal<*state.correction_ordinal)++state.correction_ordinal_resets;
        state.correction_ordinal=frame.correction_ordinal;
    } else ++state.correction_identity_missing_frames;
    counts.correction_samples=correction_sample?1U:0U;
    counts.frame_over20_ms=frame.frame_interval_ms && *frame.frame_interval_ms>20?1U:0U;
    counts.frame_over33_3_ms=frame.frame_interval_ms && *frame.frame_interval_ms>33.334?1U:0U;
    counts.frame_over50_ms=frame.frame_interval_ms && *frame.frame_interval_ms>50?1U:0U;
    counts.startsolid_frames=frame.startsolid==true?1U:0U;counts.allsolid_frames=frame.allsolid==true?1U:0U;
    counts.committed_records=frame.committed_records.value_or(0);counts.replayed_commands=frame.replayed_commands.value_or(0);counts.bytes=frame.bytes.value_or(0);
    if(frame.committed_records)++state.committed_records_observed_frames;
    if(frame.replayed_commands)++state.replayed_commands_observed_frames;if(frame.bytes)++state.bytes_observed_frames;
    const bool interval_context_known=state.last_us && !generation_boundary && !life_boundary && sample_cohort==state.previous_cohort;
    const bool consecutive=state.last_frame && *state.last_frame<std::numeric_limits<std::uint64_t>::max() &&
        frame.frame_number==*state.last_frame+1;
    const bool pacing_eligible=interval_context_known && sample_cohort==0 && consecutive && frame.frame_interval_ms;
    const auto pacing_boundary=generation_boundary ? PacingEnd::generation_boundary : life_boundary ? PacingEnd::life_boundary :
        !interval_context_known || sample_cohort!=0 ? PacingEnd::context_boundary :
        !consecutive ? PacingEnd::frame_gap : PacingEnd::missing_interval;
    state.pacing.observe(frame,pacing_eligible,state.last_us.value_or(frame.monotonic_us),pacing_boundary);
    auto cohort_counts=counts;
    if(!interval_context_known){cohort_counts.frame_over20_ms=0;cohort_counts.frame_over33_3_ms=0;cohort_counts.frame_over50_ms=0;
        if(frame.frame_interval_ms)++state.frame_interval_context_unattributed;}
    state.totals.counts.merge(counts);state.cohorts[sample_cohort].counts.merge(cohort_counts);bin.counts.merge(counts);
    if(sample_cohort==0)bin.controlled.merge(cohort_counts);++bin.cohort_frames[sample_cohort];
    for(std::size_t i=0;i<metric_members.size();++i){
        if((i==correction_metric || i==camera_metric) && !correction_sample)continue;
        if(i==gpu_metric){state.metric(i,{},sample_cohort,bin);continue;}
        if(i==0 && !interval_context_known){state.totals.metrics[i].observe(frame.frame_interval_ms);
            state.cohorts[sample_cohort].metrics[i].observe({});if(frame.frame_interval_ms)bin.metrics[i].observe(*frame.frame_interval_ms);continue;}
        state.metric(i,frame.*metric_members[i],sample_cohort,bin);
    }
    if(fallback_entry)state.metric(fallback_error_metric,frame.raw_correction_units,sample_cohort,bin);
    if(frame.prediction_active==false)state.fallback_reasons.observe(frame.fallback_reason,sample_cohort);
    state.presentation_reasons.observe(frame.presentation_reason,sample_cohort);
    bin.datagrams_delta=add(bin.datagrams_delta,state.datagrams.observe(frame.datagrams_received,network_boundary));
    bin.rejected_sequences_delta=add(bin.rejected_sequences_delta,state.rejected_sequences.observe(frame.rejected_sequences,network_boundary));
    bin.choke_markers_delta=add(bin.choke_markers_delta,state.choke_markers.observe(frame.choke_markers_total,network_boundary));
    state.gpu_identities[state.gpu_next]={frame.frame_number,frame.monotonic_us,static_cast<std::uint8_t>(sample_cohort),true,false};
    state.gpu_next=(state.gpu_next+1)%maximum_gpu_identities;
    state.last_us=frame.monotonic_us;state.last_frame=frame.frame_number;state.network_generation=frame.network_generation;state.map_generation=frame.map_generation;
    state.previous_cohort=sample_cohort;state.previous_prediction_active=frame.prediction_active;state.life_epoch=context.life_epoch;
    if(frame.gpu_ms)static_cast<void>(observe_gpu(frame.frame_number,*frame.gpu_ms));
}
bool MovementSessionRecorder::observe_gpu(const std::uint64_t frame_number,const double gpu_ms) noexcept {
    if(!impl_)return false;auto& state=*impl_;
    if(!std::isfinite(gpu_ms) || gpu_ms<0 || gpu_ms>histogram_bounds.back()){++state.gpu_invalid;return false;}
    for(auto& identity:state.gpu_identities){if(!identity.occupied || identity.frame!=frame_number)continue;
        if(identity.seen){++state.gpu_duplicates;return true;}identity.seen=true;++state.gpu_observations;
        auto& bin=state.bins[state.bin_index(identity.time)];
        --state.totals.metrics[gpu_metric].not_available;--state.cohorts[identity.cohort].metrics[gpu_metric].not_available;
        state.metric(gpu_metric,gpu_ms,identity.cohort,bin);state.pacing.gpu(frame_number,gpu_ms);return true;
    }
    ++state.gpu_unmatched;return false;
}
bool MovementSessionRecorder::mark_manual(const std::uint64_t monotonic_us) noexcept {
    if(!impl_)return false;auto& state=*impl_;
    if(!state.last_us || monotonic_us<*state.last_us){++state.manual_markers_rejected;return false;}
    auto& bin=state.bins[state.bin_index(monotonic_us)];
    if(!bin.manual_markers)bin.first_manual_marker_us=monotonic_us;
    bin.last_manual_marker_us=monotonic_us;++bin.manual_markers;++state.manual_markers;return true;
}
MovementSessionStatistics MovementSessionRecorder::statistics() const noexcept {
    MovementSessionStatistics result;if(!impl_)return result;const auto& state=*impl_;
    result.accepted_frames=state.totals.counts.frames;result.generation_boundaries=state.generation_boundaries;result.life_boundaries=state.life_boundaries;
    result.correction_samples=state.totals.counts.correction_samples;
    result.correction_ordinal_advances_unmeasured=state.totals.counts.correction_ordinal_advances_unmeasured;
    result.fallback_entries=state.totals.counts.fallback_entries;
    result.gpu_observations=state.gpu_observations;result.gpu_duplicates=state.gpu_duplicates;result.gpu_unmatched=state.gpu_unmatched;result.gpu_invalid=state.gpu_invalid;
    result.timeline_bucket_width_us=state.bucket_width_us;result.timeline_compactions=state.compactions;result.allocated_capacity_bytes=sizeof(Impl);
    for(const auto& bin:state.bins)if(bin.counts.frames || bin.manual_markers)++result.timeline_bins;return result;
}
std::optional<std::string> MovementSessionRecorder::encode() const {
    try {
        std::string output="{\"schema\":\"hlclient.movement-session.v1\",\"requested\":"+scalar(requested_);
        field(output,"enabled",enabled());field(output,"allocated_capacity_bytes",capacity_bytes());
        output+=",\"availability\":"+core::external_json_string(impl_?"ready":requested_?"allocation_failed":"disabled");
        field(output,"maximum_capacity_bytes",maximum_capacity_bytes);field(output,"maximum_encoded_bytes",maximum_encoded_bytes);
        output+=",\"causality\":\"observations_not_root_cause_proof\"";
        if(!impl_){output+='}';return output;}const auto& state=*impl_;
        output+=",\"coverage\":{\"first_us\":"+scalar(state.first_us);field(output,"last_us",state.last_us);
        field(output,"elapsed_us",state.first_us?std::optional{*state.last_us-*state.first_us}:std::optional<std::uint64_t>{});
        field(output,"accepted_frames",state.totals.counts.frames);field(output,"generation_boundaries",state.generation_boundaries);
        field(output,"life_boundaries",state.life_boundaries);field(output,"unattributed_interval_us",state.unattributed_interval_us);
        output+=",\"interval_policy\":\"adjacent_same_context_generation_at_most_250ms\",\"all_accepted_frames_aggregated\":true}";
        output+=",\"histogram\":{\"quantiles\":\"approximate_upper_bounds\",\"maximum_is_exact_observed_sample\":true,\"bounds\":[";
        for(std::size_t i=0;i<histogram_bounds.size();++i){if(i)output+=',';output+=scalar(histogram_bounds[i]);}
        output+="],\"input_policy\":\"accepted_sanitized_0_to_1000000000\"}";
        output+=",\"metric_semantics\":{\"cpu_update\":\"inclusive_overlaps_other_stages_do_not_sum\",\"state_age\":\"coherent_commit_age_not_latency_or_loss\","
            "\"correction\":\"latest_raw_error_snapshot_on_observed_ordinal_advance_not_all_corrections\","
            "\"camera_jump\":\"sticky_snapshot_on_ordinal_observation_not_exact_per_correction_delta\","
            "\"fallback_duration\":\"sticky_current_fallback_duration_snapshot_not_additive\","
            "\"cohort_frame_intervals\":\"cross_context_life_generation_and_first_intervals_only_in_totals\","
            "\"threshold_counts\":\"absolute_frame_intervals_not_cause_or_refresh_target_verdict\"}";
        output+=",\"totals\":"+encode_aggregate(state.totals)+",\"cohorts\":{";
        for(std::size_t i=0;i<cohort_count;++i){if(i)output+=',';output+=core::external_json_string(cohort_names[i])+':'+encode_aggregate(state.cohorts[i]);}
        output+="},\"counter_deltas\":{\"scope\":\"owned_peer\",\"first_observation\":\"baseline_not_delta\",\"rejected_sequence_is_packet_loss\":false,"
            "\"choke_marker_is_server_load_proof\":false,\"datagrams_received\":"+encode_counter(state.datagrams)+",\"rejected_sequences\":"+
            encode_counter(state.rejected_sequences)+",\"choke_markers\":"+encode_counter(state.choke_markers)+'}';
        output+=",\"observations\":{\"initial_inactive_observations\":"+scalar(state.initial_inactive_observations);
        field(output,"initial_nonzero_correction_ordinals",state.initial_nonzero_correction_ordinals);
        field(output,"correction_ordinal_resets",state.correction_ordinal_resets);field(output,"correction_identity_missing_frames",state.correction_identity_missing_frames);
        field(output,"committed_records_observed_frames",state.committed_records_observed_frames);
        field(output,"replayed_commands_observed_frames",state.replayed_commands_observed_frames);field(output,"bytes_observed_frames",state.bytes_observed_frames);
        field(output,"duplicate_or_unordered_frames",state.duplicate_or_unordered_frames);
        field(output,"manual_markers",state.manual_markers);field(output,"manual_markers_rejected",state.manual_markers_rejected);
        field(output,"frame_interval_context_unattributed",state.frame_interval_context_unattributed);
        output+="},\"gpu\":{\"policy\":\"delayed_frame_identity_original_cohort_and_bucket\"";
        field(output,"identity_capacity",maximum_gpu_identities);field(output,"observations",state.gpu_observations);field(output,"duplicates",state.gpu_duplicates);
        field(output,"unmatched",state.gpu_unmatched);field(output,"invalid",state.gpu_invalid);output+='}';
        output+=",\"frame_pacing\":"+encode_pacing(state.pacing);
        output+=",\"fallback_reasons\":"+encode_reasons(state.fallback_reasons)+",\"presentation_reasons\":"+encode_reasons(state.presentation_reasons);
        output+=",\"timeline\":{\"bucket_width_us\":"+scalar(state.bucket_width_us);field(output,"compactions",state.compactions);
        field(output,"maximum_bins",maximum_timeline_bins);output+=",\"policy\":\"whole_session_power_of_two_time_buckets_empty_buckets_omitted\",\"bins\":[";bool first=true;
        for(std::size_t index=0;index<state.bins.size();++index){const auto& bin=state.bins[index];if(!bin.counts.frames && !bin.manual_markers)continue;
            if(!first)output+=',';first=false;const auto begin=add(*state.first_us,static_cast<std::uint64_t>(index)*state.bucket_width_us);
            output+="{\"begin_us\":"+scalar(begin);field(output,"end_us",add(begin,state.bucket_width_us));
            field(output,"first_sample_us",bin.counts.frames?std::optional{bin.first_us}:std::optional<std::uint64_t>{});
            field(output,"last_sample_us",bin.counts.frames?std::optional{bin.last_us}:std::optional<std::uint64_t>{});field(output,"frames",bin.counts.frames);
            field(output,"manual_markers",bin.manual_markers);
            field(output,"first_manual_marker_us",bin.manual_markers?std::optional{bin.first_manual_marker_us}:std::optional<std::uint64_t>{});
            field(output,"last_manual_marker_us",bin.manual_markers?std::optional{bin.last_manual_marker_us}:std::optional<std::uint64_t>{});
            output+=",\"counts\":"+encode_counts(bin.counts)+",\"controlled_alive_counts\":"+encode_counts(bin.controlled)+",\"cohort_frames\":{";
            for(std::size_t i=0;i<cohort_count;++i){if(i)output+=',';output+=core::external_json_string(cohort_names[i])+':'+scalar(bin.cohort_frames[i]);}
            output+="},\"metrics\":{";
            for(std::size_t i=0;i<metric_count;++i){if(i)output+=',';output+=core::external_json_string(metric_names[i])+':'+encode_metric(bin.metrics[i])+'}';}
            output+='}';field(output,"generation_boundaries",bin.generation_boundaries);field(output,"datagrams_delta",bin.datagrams_delta);
            field(output,"rejected_sequences_delta",bin.rejected_sequences_delta);field(output,"choke_markers_delta",bin.choke_markers_delta);output+='}';
        }
        output+="]}}";if(output.size()>maximum_encoded_bytes)return {};return output;
    } catch(...) {return {};}
}
} // namespace hlclient::app
