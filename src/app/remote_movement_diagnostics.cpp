#include <hlclient/app/remote_movement_diagnostics.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace hlclient::app {
namespace {
using Observation=RemoteMovementObservation;
using Position=std::array<double,3>;
struct Sample final {
    std::uint64_t monotonic_us{},frame_number{},network_generation{},map_generation{};
    bool present{};
    Observation observation;
};
struct Delta final {
    std::optional<std::uint64_t> elapsed_us;
    std::optional<double> raw_units,sampled_units,rendered_units,sampled_speed,rendered_speed;
};
struct Event final {
    std::uint64_t ordinal{};
    unsigned trigger_mask{};
    std::optional<Sample> before;
    Sample current;
    Delta delta;
};
struct ManualCapture final {
    std::uint64_t marker_us{};
    bool truncated{},event_truncated{};
    std::size_t count{},event_count{};
    std::array<Sample,RemoteMovementRecorder::maximum_manual_samples> samples;
    std::array<Event,RemoteMovementRecorder::maximum_manual_events> events;
};
struct Actor final {
    std::optional<Sample> last;
    std::optional<std::uint64_t> last_displacement_event;
    std::uint64_t samples{},comparisons{},absences{},reentries{},lifetime_boundaries{},generation_boundaries{};
    std::uint64_t raw_comparisons{},sampled_comparisons{},rendered_comparisons{};
    std::uint64_t displacements{},new_boundaries{},decision_changes{},event_cooldown_suppressions{};
    double raw_distance_sum{},sampled_distance_sum{},rendered_distance_sum{};
    double max_raw_units{},max_sampled_units{},max_rendered_units{},max_sampled_speed{},max_rendered_speed{};
    std::array<std::uint64_t,8> decision_counts{};
    std::array<std::uint64_t,13> boundary_counts{};
    std::array<std::uint64_t,7> hold_counts{};
};
bool finite_position(const Position& value) noexcept {
    return std::ranges::all_of(value,[](double x){return std::isfinite(x) && std::abs(x)<=1'000'000'000.0;});
}
void sanitize(Observation& value,std::uint64_t& invalid) noexcept {
    for(auto* vector:{&value.previous_raw_origin,&value.current_raw_origin,&value.latest_raw_origin,&value.sampled_origin,&value.rendered_origin})
        if(*vector && !finite_position(**vector)){vector->reset();++invalid;}
    for(auto* number:{&value.previous_server_s,&value.current_server_s,&value.latest_server_s,&value.sample_server_s})
        if(*number && (!std::isfinite(**number) || **number<0 || **number>1'000'000'000.0)){number->reset();++invalid;}
    if(value.rendered_opacity && (!std::isfinite(*value.rendered_opacity) ||
        *value.rendered_opacity<0 || *value.rendered_opacity>1)) {
        value.rendered_opacity.reset();++invalid;
    }
    if(value.support_offset_z && (!std::isfinite(*value.support_offset_z) || std::abs(*value.support_offset_z)>128.0)) {
        value.support_offset_z.reset();++invalid;
    }
    auto& p=value.presentation;
    if(!std::isfinite(p.alpha) || p.alpha<0 || p.alpha>1){p.alpha=0;++invalid;}
    if((p.floor_reasons&~8191U)!=0){p.floor_reasons&=8191U;++invalid;}
    if((p.new_boundary_reasons&~8191U)!=0){p.new_boundary_reasons&=8191U;++invalid;}
    if((p.hold_reasons&~127U)!=0){p.hold_reasons&=127U;++invalid;}
    if((p.clock_reasons&~2047U)!=0){p.clock_reasons&=2047U;++invalid;}
}
std::optional<double> distance(const std::optional<Position>& before,const std::optional<Position>& after) noexcept {
    if(!before || !after)return {};
    return std::hypot((*after)[0]-(*before)[0],(*after)[1]-(*before)[1],(*after)[2]-(*before)[2]);
}
Delta difference(const Sample& before,const Sample& after) noexcept {
    Delta result;
    if(!before.present || !after.present || before.network_generation!=after.network_generation ||
        before.map_generation!=after.map_generation || before.observation.lifetime!=after.observation.lifetime ||
        after.monotonic_us<=before.monotonic_us)return result;
    result.elapsed_us=after.monotonic_us-before.monotonic_us;
    result.raw_units=distance(before.observation.current_raw_origin,after.observation.current_raw_origin);
    result.sampled_units=distance(before.observation.sampled_origin,after.observation.sampled_origin);
    result.rendered_units=distance(before.observation.rendered_origin,after.observation.rendered_origin);
    const double seconds=static_cast<double>(*result.elapsed_us)/1'000'000.0;
    if(result.sampled_units)result.sampled_speed=*result.sampled_units/seconds;
    if(result.rendered_units)result.rendered_speed=*result.rendered_units/seconds;
    return result;
}
void accumulate(const std::optional<double>& value,double& sum,double& maximum) noexcept {
    if(value){sum+=*value;maximum=std::max(maximum,*value);}
}
template<typename T> std::string scalar(const T value) {
    if constexpr(std::is_same_v<T,bool>)return value?"true":"false";
    else {std::array<char,48> buffer{};const auto converted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value);
        return converted.ec==std::errc{}?std::string{buffer.data(),converted.ptr}:"null";}
}
template<typename T> std::string scalar(const std::optional<T>& value){return value?scalar(*value):"null";}
std::string scalar(const Position& value){return '['+scalar(value[0])+','+scalar(value[1])+','+scalar(value[2])+']';}
std::string scalar(const std::optional<Position>& value){return value?scalar(*value):"null";}
template<typename T> void field(std::string& text,std::string_view name,const T& value){text+=",\"";text+=name;text+="\":";text+=scalar(value);}
std::string encode_sample(const Sample& sample) {
    std::string text="{\"monotonic_us\":"+scalar(sample.monotonic_us);
    field(text,"frame_number",sample.frame_number);field(text,"network_generation",sample.network_generation);
    field(text,"map_generation",sample.map_generation);field(text,"present",sample.present);
    const auto& v=sample.observation;
#define REMOTE_FIELD(name) field(text,#name,v.name)
    REMOTE_FIELD(entity_index);REMOTE_FIELD(model_index);REMOTE_FIELD(lifetime);
    REMOTE_FIELD(previous_raw_origin);REMOTE_FIELD(current_raw_origin);REMOTE_FIELD(latest_raw_origin);REMOTE_FIELD(sampled_origin);REMOTE_FIELD(rendered_origin);
    REMOTE_FIELD(previous_source_ordinal);REMOTE_FIELD(current_source_ordinal);REMOTE_FIELD(latest_source_ordinal);
    REMOTE_FIELD(previous_server_s);REMOTE_FIELD(current_server_s);REMOTE_FIELD(latest_server_s);REMOTE_FIELD(sample_server_s);
    REMOTE_FIELD(selected_main);REMOTE_FIELD(selected_gait);REMOTE_FIELD(selected_weapon);REMOTE_FIELD(raw_main);REMOTE_FIELD(raw_gait);REMOTE_FIELD(raw_weapon);REMOTE_FIELD(raw_effects);
    REMOTE_FIELD(raw_render_mode);REMOTE_FIELD(raw_render_amount);REMOTE_FIELD(raw_render_effect);REMOTE_FIELD(rendered_opacity);
    REMOTE_FIELD(support_entity);REMOTE_FIELD(support_model);REMOTE_FIELD(support_offset_z);
    REMOTE_FIELD(pose_submitted);REMOTE_FIELD(visible);
#undef REMOTE_FIELD
    const auto& p=v.presentation;
    text+=",\"decision\":\"";text+=entity_visual::to_string(p.decision);text+='"';
#define REMOTE_FIELD(name) field(text,#name,p.name)
    REMOTE_FIELD(floor_reasons);REMOTE_FIELD(new_boundary_reasons);REMOTE_FIELD(hold_reasons);REMOTE_FIELD(clock_reasons);
    REMOTE_FIELD(floor_record_ordinal);REMOTE_FIELD(resolved_floor_record_ordinal);REMOTE_FIELD(position_previous_record_ordinal);REMOTE_FIELD(position_current_record_ordinal);REMOTE_FIELD(alpha);
#undef REMOTE_FIELD
    return text+'}';
}
std::string encode_delta(const Delta& value){
    std::string text="{\"elapsed_us\":"+scalar(value.elapsed_us);
    field(text,"raw_units",value.raw_units);field(text,"sampled_units",value.sampled_units);field(text,"rendered_units",value.rendered_units);
    field(text,"sampled_speed",value.sampled_speed);field(text,"rendered_speed",value.rendered_speed);return text+'}';
}
std::string encode_event(const Event& event){
    std::string text="{\"ordinal\":"+scalar(event.ordinal);field(text,"trigger_mask",event.trigger_mask);
    return text+",\"before\":"+(event.before?encode_sample(*event.before):"null")+",\"current\":"+encode_sample(event.current)+",\"delta\":"+encode_delta(event.delta)+'}';
}
template<std::size_t N> void counts(std::string& text,std::string_view name,const std::array<std::uint64_t,N>& values){
    text+=",\"";text+=name;text+="\":[";for(std::size_t i=0;i<N;++i){if(i)text+=',';text+=scalar(values[i]);}text+=']';
}
std::string unavailable(const bool requested,const bool enabled,const std::string_view reason){
    std::string text="{\"schema\":\"hlclient.remote-players.v1\",\"requested\":"+scalar(requested);
    field(text,"enabled",enabled);text+=",\"availability\":\"";text+=reason;
    return text+"\",\"causality\":\"observations_not_root_cause_proof\"}";
}
}
struct RemoteMovementRecorder::Impl final {
    std::array<Actor,maximum_actors> actors;
    std::array<Sample,maximum_recent_samples> recent;
    std::array<Event,maximum_event_records> events;
    std::array<ManualCapture,maximum_manual_captures> manual;
    std::size_t recent_first{},recent_count{},event_first{},event_count{},manual_first{},manual_count{};
    std::optional<std::uint64_t> last_time,last_frame,last_recent_time;
    std::uint64_t network_generation{},map_generation{},event_ordinal{};
    RemoteMovementStatistics statistics;
    void append_recent(const Sample& sample) noexcept {
        if(recent_count==maximum_recent_samples){recent_first=(recent_first+1)%maximum_recent_samples;--recent_count;++statistics.recent_capacity_drops;}
        recent[(recent_first+recent_count)%maximum_recent_samples]=sample;++recent_count;
    }
    void append_event(const Actor& actor,const Sample& sample,const Delta& delta,unsigned mask) noexcept {
        if(event_count==maximum_event_records){event_first=(event_first+1)%maximum_event_records;--event_count;++statistics.event_capacity_drops;}
        auto& event=events[(event_first+event_count)%maximum_event_records];
        event.ordinal=++event_ordinal;event.trigger_mask=mask;event.before=actor.last;event.current=sample;event.delta=delta;++event_count;
    }
};
RemoteMovementRecorder::RemoteMovementRecorder(const bool enabled) noexcept:requested_{enabled}{
    static_assert(sizeof(Impl)<=maximum_capacity_bytes);
    if(enabled)try{impl_=std::make_unique<Impl>();}catch(...){}
}
RemoteMovementRecorder::~RemoteMovementRecorder()=default;
RemoteMovementRecorder::RemoteMovementRecorder(RemoteMovementRecorder&&) noexcept=default;
RemoteMovementRecorder& RemoteMovementRecorder::operator=(RemoteMovementRecorder&&) noexcept=default;
bool RemoteMovementRecorder::enabled() const noexcept{return static_cast<bool>(impl_);}
std::size_t RemoteMovementRecorder::capacity_bytes() const noexcept{return impl_?sizeof(Impl):0;}
RemoteMovementStatistics RemoteMovementRecorder::statistics() const noexcept {
    if(!impl_)return {};auto result=impl_->statistics;result.retained_samples=impl_->recent_count;result.retained_events=impl_->event_count;
    result.manual_captures=impl_->manual_count;result.allocated_capacity_bytes=sizeof(Impl);return result;
}
bool RemoteMovementRecorder::observe(const MovementFrameRecord& frame,const std::span<const Observation> observations) noexcept {
    if(!impl_)return false;
    auto& r=*impl_;
    if(r.last_time && r.last_frame && frame.monotonic_us==*r.last_time && frame.frame_number==*r.last_frame){++r.statistics.duplicate_frames;return false;}
    if((r.last_time && frame.monotonic_us<=*r.last_time) || (r.last_frame && frame.frame_number<=*r.last_frame) || observations.size()>maximum_actors){++r.statistics.rejected_frames;return false;}
    const bool generation=r.last_time && (frame.network_generation!=r.network_generation || frame.map_generation!=r.map_generation);
    if(generation)++r.statistics.generation_boundaries;
    while(r.recent_count && frame.monotonic_us-r.recent[r.recent_first].monotonic_us>retention_us){
        r.recent_first=(r.recent_first+1)%maximum_recent_samples;--r.recent_count;++r.statistics.retention_evictions;}
    const bool retain=!r.last_recent_time || frame.monotonic_us-*r.last_recent_time>=sample_interval_us;
    std::array<const Observation*,maximum_actors> selected{};
    bool incomplete_input{};
    for(const auto& observation:observations){
        if(observation.entity_index<1 || observation.entity_index>maximum_actors ||
            static_cast<unsigned>(observation.presentation.decision)>7U){++r.statistics.invalid_observations;incomplete_input=true;continue;}
        auto& destination=selected[observation.entity_index-1];
        if(destination){++r.statistics.duplicate_actors;incomplete_input=true;continue;}destination=&observation;
    }
    for(std::size_t i=0;i<maximum_actors;++i){
        auto& actor=r.actors[i];
        if(!selected[i] && (incomplete_input || !actor.last || !actor.last->present))continue;
        Sample sample;sample.monotonic_us=frame.monotonic_us;sample.frame_number=frame.frame_number;
        sample.network_generation=frame.network_generation;sample.map_generation=frame.map_generation;sample.present=selected[i]!=nullptr;
        if(selected[i]){sample.observation=*selected[i];sanitize(sample.observation,r.statistics.invalid_values);}
        else {sample.observation.entity_index=static_cast<std::uint32_t>(i+1);sample.observation.lifetime=actor.last->observation.lifetime;sample.observation.model_index=actor.last->observation.model_index;}
        unsigned mask{};Delta delta;
        if(!actor.last)mask|=1U;
        else {
            if(!sample.present){mask|=2U;++actor.absences;++r.statistics.absences;}
            else if(!actor.last->present){mask|=4U;++actor.reentries;++r.statistics.reentries;}
            if(sample.network_generation!=actor.last->network_generation || sample.map_generation!=actor.last->map_generation){mask|=8U;++actor.generation_boundaries;actor.last_displacement_event.reset();}
            if(sample.present && sample.observation.lifetime!=actor.last->observation.lifetime){mask|=16U;++actor.lifetime_boundaries;++r.statistics.lifetime_boundaries;actor.last_displacement_event.reset();}
            delta=difference(*actor.last,sample);
            if(delta.elapsed_us){
                if(sample.observation.raw_render_mode!=actor.last->observation.raw_render_mode ||
                    sample.observation.raw_render_amount!=actor.last->observation.raw_render_amount ||
                    sample.observation.raw_render_effect!=actor.last->observation.raw_render_effect)
                    mask|=256U;
                if(sample.observation.support_entity!=actor.last->observation.support_entity ||
                    sample.observation.support_model!=actor.last->observation.support_model)
                    mask|=512U;
                ++actor.comparisons;
                if(delta.raw_units)++actor.raw_comparisons;
                if(delta.sampled_units)++actor.sampled_comparisons;
                if(delta.rendered_units)++actor.rendered_comparisons;
                accumulate(delta.raw_units,actor.raw_distance_sum,actor.max_raw_units);
                accumulate(delta.sampled_units,actor.sampled_distance_sum,actor.max_sampled_units);
                accumulate(delta.rendered_units,actor.rendered_distance_sum,actor.max_rendered_units);
                if(delta.sampled_speed)actor.max_sampled_speed=std::max(actor.max_sampled_speed,*delta.sampled_speed);
                if(delta.rendered_speed)actor.max_rendered_speed=std::max(actor.max_rendered_speed,*delta.rendered_speed);
                if(sample.observation.presentation.decision!=actor.last->observation.presentation.decision){
                    ++actor.decision_changes;
                    // Exact endpoints and interior interpolation belong to one
                    // healthy mode family; their ordinary per-tick alternation
                    // must not evict actual discontinuities from the event ring.
                    if(static_cast<unsigned>(sample.observation.presentation.decision)>=3U ||
                        static_cast<unsigned>(actor.last->observation.presentation.decision)>=3U)mask|=64U;
                }
                if(delta.sampled_units.value_or(0)>=displacement_trigger_units || delta.rendered_units.value_or(0)>=displacement_trigger_units){
                    ++actor.displacements;
                    if(mask || !actor.last_displacement_event || frame.monotonic_us-*actor.last_displacement_event>=event_cooldown_us){mask|=128U;actor.last_displacement_event=frame.monotonic_us;}
                    else {++actor.event_cooldown_suppressions;++r.statistics.event_cooldown_suppressions;}
                }
            }
        }
        if(sample.present){
            ++actor.samples;const auto& p=sample.observation.presentation;
            ++actor.decision_counts[static_cast<std::size_t>(p.decision)];
            if(p.new_boundary_reasons){mask|=32U;++actor.new_boundaries;}
            for(std::size_t bit=0;bit<actor.boundary_counts.size();++bit)if((p.new_boundary_reasons&(1U<<bit))!=0)++actor.boundary_counts[bit];
            for(std::size_t bit=0;bit<actor.hold_counts.size();++bit)if((p.hold_reasons&(1U<<bit))!=0)++actor.hold_counts[bit];
        }
        if(mask)r.append_event(actor,sample,delta,mask);
        if(retain)r.append_recent(sample);
        actor.last=sample;
    }
    r.last_time=frame.monotonic_us;r.last_frame=frame.frame_number;r.network_generation=frame.network_generation;r.map_generation=frame.map_generation;
    if(retain)r.last_recent_time=frame.monotonic_us;++r.statistics.accepted_frames;return true;
}
bool RemoteMovementRecorder::mark_manual(const std::uint64_t now) noexcept {
    if(!impl_ || !impl_->last_time || now<*impl_->last_time)return false;
    auto& r=*impl_;
    if(r.manual_count && r.manual[(r.manual_first+r.manual_count-1)%maximum_manual_captures].marker_us==now)return true;
    if(r.manual_count==maximum_manual_captures){r.manual_first=(r.manual_first+1)%maximum_manual_captures;--r.manual_count;++r.statistics.manual_capture_drops;}
    auto& capture=r.manual[(r.manual_first+r.manual_count)%maximum_manual_captures];capture.marker_us=now;capture.count=0;capture.truncated=false;capture.event_count=0;capture.event_truncated=false;
    const auto begin=now>manual_pre_us?now-manual_pre_us:0;
    std::size_t eligible{},latest_count{};
    for(const auto& actor:r.actors)if(actor.last && actor.last->frame_number==*r.last_frame && actor.last->monotonic_us>=begin)++latest_count;
    for(std::size_t i=0;i<r.recent_count;++i){const auto& sample=r.recent[(r.recent_first+i)%maximum_recent_samples];
        if(sample.monotonic_us>=begin && sample.frame_number!=*r.last_frame)++eligible;}
    const auto omit=eligible+latest_count>maximum_manual_samples?eligible+latest_count-maximum_manual_samples:0;capture.truncated=omit!=0;
    std::size_t skipped{};
    for(std::size_t i=0;i<r.recent_count;++i){const auto& sample=r.recent[(r.recent_first+i)%maximum_recent_samples];
        if(sample.monotonic_us<begin || sample.frame_number==*r.last_frame)continue;if(skipped++<omit)continue;capture.samples[capture.count++]=sample;}
    // The marker preserves the exact most recent actor observations as well
    // as the downsampled prehistory; no invented frame at marker time.
    for(const auto& actor:r.actors)if(actor.last && actor.last->frame_number==*r.last_frame && actor.last->monotonic_us>=begin)
        capture.samples[capture.count++]=*actor.last;
    std::size_t event_eligible{};
    for(std::size_t i=0;i<r.event_count;++i)if(r.events[(r.event_first+i)%maximum_event_records].current.monotonic_us>=begin)++event_eligible;
    const auto event_omit=event_eligible>maximum_manual_events?event_eligible-maximum_manual_events:0;
    capture.event_truncated=event_omit!=0 || (r.statistics.event_capacity_drops!=0 && r.event_count!=0 &&
        r.events[r.event_first].current.monotonic_us>begin);
    skipped=0;
    for(std::size_t i=0;i<r.event_count;++i){const auto& event=r.events[(r.event_first+i)%maximum_event_records];
        if(event.current.monotonic_us<begin)continue;if(skipped++<event_omit)continue;capture.events[capture.event_count++]=event;}
    ++r.manual_count;return true;
}
std::string RemoteMovementRecorder::encode(const std::size_t requested_budget) const {
    const auto budget=std::min(requested_budget,maximum_encoded_bytes);
    if(!impl_)return unavailable(requested_,false,requested_?"allocation_failed":"disabled");
    const auto& r=*impl_;const auto stats=statistics();
    if(stats.accepted_frames==0)return unavailable(requested_,true,"awaiting_presentation");
    const auto fallback=[&]{return unavailable(requested_,true,"budget_exceeded");};
    try {
        std::string text="{\"schema\":\"hlclient.remote-players.v1\",\"requested\":true,\"enabled\":true,\"availability\":\"ready\",\"causality\":\"observations_not_root_cause_proof\"";
        text+=",\"limits\":{\"max_actors\":"+scalar(maximum_actors);
        field(text,"recent_samples",maximum_recent_samples);field(text,"event_records",maximum_event_records);field(text,"manual_captures",maximum_manual_captures);field(text,"manual_samples",maximum_manual_samples);field(text,"manual_events",maximum_manual_events);
        field(text,"retention_us",retention_us);field(text,"sample_interval_us",sample_interval_us);field(text,"event_cooldown_us",event_cooldown_us);field(text,"manual_pre_us",manual_pre_us);
        field(text,"displacement_trigger_units",displacement_trigger_units);field(text,"maximum_capacity_bytes",maximum_capacity_bytes);field(text,"maximum_encoded_bytes",maximum_encoded_bytes);text+='}';
        text+=",\"statistics\":{\"accepted_frames\":"+scalar(stats.accepted_frames);
#define REMOTE_STAT(name) field(text,#name,stats.name)
        REMOTE_STAT(duplicate_frames);REMOTE_STAT(rejected_frames);REMOTE_STAT(invalid_observations);REMOTE_STAT(invalid_values);REMOTE_STAT(duplicate_actors);
        REMOTE_STAT(absences);REMOTE_STAT(reentries);REMOTE_STAT(generation_boundaries);REMOTE_STAT(lifetime_boundaries);REMOTE_STAT(event_capacity_drops);REMOTE_STAT(recent_capacity_drops);
        REMOTE_STAT(retention_evictions);REMOTE_STAT(event_cooldown_suppressions);REMOTE_STAT(manual_capture_drops);REMOTE_STAT(retained_samples);REMOTE_STAT(retained_events);REMOTE_STAT(manual_captures);REMOTE_STAT(allocated_capacity_bytes);
#undef REMOTE_STAT
        text+="},\"actors\":[";bool first=true;
        for(std::size_t i=0;i<maximum_actors;++i){const auto& actor=r.actors[i];if(!actor.last)continue;if(!first)text+=',';first=false;
            text+="{\"entity_index\":"+scalar(i+1);
#define REMOTE_ACTOR(name) field(text,#name,actor.name)
            REMOTE_ACTOR(samples);REMOTE_ACTOR(comparisons);
            REMOTE_ACTOR(raw_comparisons);REMOTE_ACTOR(sampled_comparisons);REMOTE_ACTOR(rendered_comparisons);
            REMOTE_ACTOR(absences);REMOTE_ACTOR(reentries);REMOTE_ACTOR(lifetime_boundaries);REMOTE_ACTOR(generation_boundaries);
            REMOTE_ACTOR(displacements);REMOTE_ACTOR(new_boundaries);REMOTE_ACTOR(decision_changes);REMOTE_ACTOR(event_cooldown_suppressions);
#undef REMOTE_ACTOR
            const auto measured=[](const double value,const std::uint64_t comparisons){return comparisons?std::optional{value}:std::optional<double>{};};
            field(text,"raw_distance_sum",measured(actor.raw_distance_sum,actor.raw_comparisons));
            field(text,"sampled_distance_sum",measured(actor.sampled_distance_sum,actor.sampled_comparisons));
            field(text,"rendered_distance_sum",measured(actor.rendered_distance_sum,actor.rendered_comparisons));
            field(text,"max_raw_units",measured(actor.max_raw_units,actor.raw_comparisons));
            field(text,"max_sampled_units",measured(actor.max_sampled_units,actor.sampled_comparisons));
            field(text,"max_rendered_units",measured(actor.max_rendered_units,actor.rendered_comparisons));
            field(text,"max_sampled_speed",measured(actor.max_sampled_speed,actor.sampled_comparisons));
            field(text,"max_rendered_speed",measured(actor.max_rendered_speed,actor.rendered_comparisons));
            counts(text,"decision_counts",actor.decision_counts);counts(text,"boundary_counts",actor.boundary_counts);counts(text,"hold_counts",actor.hold_counts);text+='}';
        }
        text+="],\"recent_samples\":[";
        for(std::size_t i=0;i<r.recent_count;++i){if(i)text+=',';text+=encode_sample(r.recent[(r.recent_first+i)%maximum_recent_samples]);if(text.size()>budget)return fallback();}
        text+="],\"events\":[";
        for(std::size_t i=0;i<r.event_count;++i){if(i)text+=',';text+=encode_event(r.events[(r.event_first+i)%maximum_event_records]);
            if(text.size()>budget)return fallback();}
        text+="],\"manual_captures\":[";
        for(std::size_t i=0;i<r.manual_count;++i){if(i)text+=',';const auto& capture=r.manual[(r.manual_first+i)%maximum_manual_captures];
            text+="{\"marker_us\":"+scalar(capture.marker_us);field(text,"truncated",capture.truncated);text+=",\"samples\":[";
            for(std::size_t j=0;j<capture.count;++j){if(j)text+=',';text+=encode_sample(capture.samples[j]);if(text.size()>budget)return fallback();}text+=']';
            field(text,"event_truncated",capture.event_truncated);text+=",\"events\":[";
            for(std::size_t j=0;j<capture.event_count;++j){if(j)text+=',';text+=encode_event(capture.events[j]);if(text.size()>budget)return fallback();}text+="]}";}
        text+="]}";return text.size()<=budget?text:fallback();
    }catch(...){return unavailable(requested_,true,"encode_failed");}
}
} // namespace hlclient::app
