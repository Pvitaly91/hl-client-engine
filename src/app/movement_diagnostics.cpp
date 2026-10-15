#include <hlclient/app/movement_diagnostics.hpp>
#include <hlclient/core/external_launch_config.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace hlclient::app {
namespace {
using Record=MovementFrameRecord;
using MetricMember=std::optional<double> Record::*;
struct MetricDescriptor {std::string_view name,unit;MetricMember member;};
constexpr std::array<MetricDescriptor,static_cast<std::size_t>(MovementTimingMetric::count)> metrics{{
    {"frame_interval","ms",&Record::frame_interval_ms},{"cpu_update","ms",&Record::cpu_update_ms},
    {"network","ms",&Record::network_ms},{"parse_commit","ms",&Record::parse_commit_ms},
    {"prediction","ms",&Record::prediction_ms},{"collision","ms",&Record::collision_ms},
    {"studio","ms",&Record::studio_ms},{"asset","ms",&Record::asset_ms},
    {"render_submission","ms",&Record::render_ms},{"present_wait","ms",&Record::present_ms},
    {"log_report","ms",&Record::report_ms},{"gpu","ms",&Record::gpu_ms},
    {"state_age","ms",&Record::state_age_ms},{"raw_correction","world_units",&Record::raw_correction_units},
    {"camera_jump","world_units",&Record::camera_jump_units},{"replay_duration","ms",&Record::replay_duration_ms},
    {"fallback_duration","ms",&Record::fallback_duration_ms}}};
bool safe_token(const std::string_view value) noexcept {
    return !value.empty() && std::ranges::all_of(value,[](unsigned char c) {
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-';
    });
}
template<std::size_t N> void set_token(std::array<char,N>& target,std::string_view value) noexcept {
    if(value.empty())value="not_observed";
    if(value.size()>=N || !safe_token(value))value="rejected_diagnostic";
    std::array<char,N> clean{};std::copy(value.begin(),value.end(),clean.begin());target=clean;
}
template<std::size_t N> std::string_view token(const std::array<char,N>& value) noexcept {
    const auto end=std::find(value.begin(),value.end(),'\0');
    const std::string_view result{value.data(),static_cast<std::size_t>(end-value.begin())};
    return result.empty()?std::string_view{"not_observed"}:safe_token(result)?result:std::string_view{"rejected_diagnostic"};
}
std::uint64_t saturated_add(std::uint64_t a,std::uint64_t b) noexcept {
    return a>(std::numeric_limits<std::uint64_t>::max)()-b?(std::numeric_limits<std::uint64_t>::max)():a+b;
}
void bounded_value(std::optional<double>& value,double low,double high,std::uint64_t& invalid) noexcept {
    if(value && (!std::isfinite(*value) || *value<low || *value>high)){value.reset();++invalid;}
}
void sanitize(Record& value,std::uint64_t& invalid) noexcept {
    if (value.excluded_point_boxes!=kUnavailableExcludedPointBoxCount && value.excluded_point_boxes>8192U)
        {value.excluded_point_boxes=kUnavailableExcludedPointBoxCount;++invalid;}
    for(const auto& descriptor:metrics)bounded_value(value.*descriptor.member,0,1'000'000'000.0,invalid);
    for(auto* number:{&value.svc_time_s,&value.command_time_s,&value.render_sample_time_s,
        &value.remote_previous_s,&value.remote_current_s,&value.remote_sample_s,&value.support_observation_s})
        bounded_value(*number,0,1'000'000'000.0,invalid);
    bounded_value(value.remote_alpha,0,1,invalid);
    bounded_value(value.trace_fraction,0,1,invalid);
    bounded_value(value.support_velocity_z,-1'000'000'000.0,1'000'000'000.0,invalid);
    for(auto* vector:{&value.raw_server_origin,&value.predicted_origin,&value.presented_eye,
        &value.velocity,&value.plane_normal,&value.basevelocity,&value.previous_valid_origin,&value.seed_origin,
        &value.collision_query_start,&value.collision_query_end}) {
        if(*vector && std::ranges::any_of(**vector,[](double number){return !std::isfinite(number) || std::abs(number)>1'000'000'000.0;}))
            {vector->reset();++invalid;}
    }
    for(auto* stamp:{&value.socket_read_begin_us,&value.socket_read_us,&value.network_poll_begin_us,
        &value.network_poll_end_us,&value.parse_begin_us,&value.commit_us,&value.receipt_observed_us,
        &value.prediction_use_us,&value.presentation_use_us,&value.support_observation_us})
        if(*stamp && **stamp>value.monotonic_us){stamp->reset();++invalid;}
    set_token(value.fallback_reason,token(value.fallback_reason));
    set_token(value.correction_anchor_provenance,token(value.correction_anchor_provenance));
    set_token(value.remote_sample_mode,token(value.remote_sample_mode));
    set_token(value.blocker_kind,token(value.blocker_kind));
    set_token(value.collision_query_purpose,token(value.collision_query_purpose));
    set_token(value.authoritative_body_kind,token(value.authoritative_body_kind));
    set_token(value.presentation_reason,token(value.presentation_reason));
    for(auto* vector:{&value.authoritative_velocity,&value.matched_origin_before_correction,&value.matched_velocity_before_correction,
        &value.camera_base_angles,&value.server_punch_angles,&value.local_punch_angles,&value.camera_final_angles}) {
        if(*vector && std::ranges::any_of(**vector,[](float number){return !std::isfinite(number) || std::abs(number)>1e9F;}))
            {vector->reset();++invalid;}
    }
    if(value.collision_plane_distance && !std::isfinite(*value.collision_plane_distance))
        {value.collision_plane_distance.reset();++invalid;}
    if(value.command && (value.command->sample_us>value.monotonic_us || value.command->msec==0))
        {value.command.reset();++invalid;}
}
template<typename T> std::string scalar(const T value) {
    if constexpr(std::is_same_v<T,bool>)return value?"true":"false";
    else {std::array<char,48> buffer{};const auto converted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value);
        return converted.ec==std::errc{}?std::string{buffer.data(),converted.ptr}:"null";}
}
template<typename T> std::string scalar(const std::optional<T>& value) {return value?scalar(*value):"null";}
std::string scalar(const std::optional<MovementDiagnosticVector3>& value) {
    return value?"["+scalar((*value)[0])+','+scalar((*value)[1])+','+scalar((*value)[2])+']':"null";
}
std::string scalar(const std::optional<MovementDiagnosticFloat3>& value) {
    return value?"["+scalar((*value)[0])+','+scalar((*value)[1])+','+scalar((*value)[2])+']':"null";
}
std::string scalar(const std::optional<MovementDiagnosticCommand>& value) {
    if(!value)return "null";
    return "{\"sequence\":"+scalar(value->sequence)+",\"sample_us\":"+scalar(value->sample_us)+
        ",\"msec\":"+scalar(value->msec)+",\"buttons\":"+scalar(value->buttons)+
        ",\"angle_turns\":["+scalar(value->angle_turns[0])+','+scalar(value->angle_turns[1])+','+scalar(value->angle_turns[2])+
        "],\"movement\":["+scalar(value->movement[0])+','+scalar(value->movement[1])+','+scalar(value->movement[2])+ "]}";
}
template<typename T> void field(std::string& text,std::string_view name,const T& value) {
    text+=",\"";text+=name;text+="\":";text+=scalar(value);
}
std::string encode_frame(const Record& value) {
    std::string text="{\"monotonic_us\":"+scalar(value.monotonic_us);
    field(text,"frame_number",value.frame_number);field(text,"network_generation",value.network_generation);
    field(text,"map_generation",value.map_generation);
    for(const auto& descriptor:metrics)field(text,descriptor.name,value.*descriptor.member);
#define E32_FIELD(name) field(text,#name,value.name)
    E32_FIELD(socket_read_begin_us);E32_FIELD(socket_read_us);E32_FIELD(network_poll_begin_us);E32_FIELD(network_poll_end_us);
    E32_FIELD(parse_begin_us);E32_FIELD(commit_us);E32_FIELD(receipt_observed_us);E32_FIELD(prediction_use_us);E32_FIELD(presentation_use_us);
    E32_FIELD(support_observation_us);E32_FIELD(svc_time_s);E32_FIELD(command_time_s);E32_FIELD(render_sample_time_s);
    E32_FIELD(remote_previous_s);E32_FIELD(remote_current_s);E32_FIELD(remote_sample_s);E32_FIELD(remote_alpha);
    E32_FIELD(support_observation_s);
    E32_FIELD(source_sequence);E32_FIELD(correction_ordinal);E32_FIELD(collision_revision);E32_FIELD(queue_depth);E32_FIELD(queue_depth_after);
    field(text,"excluded_point_boxes",value.excluded_point_boxes==kUnavailableExcludedPointBoxCount
        ? std::optional<std::uint16_t>{} : std::optional<std::uint16_t>{value.excluded_point_boxes});
    E32_FIELD(datagrams_received);E32_FIELD(rejected_sequences);E32_FIELD(choke_markers_total);
    E32_FIELD(entities);E32_FIELD(observed_players);E32_FIELD(bytes);E32_FIELD(committed_records);E32_FIELD(replayed_commands);
    E32_FIELD(blocking_entity);E32_FIELD(support_entity);E32_FIELD(support_model_index);E32_FIELD(support_submodel);E32_FIELD(support_physent);E32_FIELD(hull);
    E32_FIELD(prediction_active);E32_FIELD(startsolid);E32_FIELD(allsolid);E32_FIELD(grounded);E32_FIELD(trace_fraction);E32_FIELD(support_velocity_z);
    E32_FIELD(raw_server_origin);E32_FIELD(predicted_origin);E32_FIELD(presented_eye);E32_FIELD(velocity);E32_FIELD(plane_normal);E32_FIELD(basevelocity);
    E32_FIELD(previous_valid_origin);E32_FIELD(seed_origin);E32_FIELD(collision_query_start);E32_FIELD(collision_query_end);
    E32_FIELD(command);E32_FIELD(authoritative_source_record_identity);E32_FIELD(anchor_command);E32_FIELD(authoritative_source_sequence);E32_FIELD(anchor_status);
    E32_FIELD(authoritative_velocity);E32_FIELD(matched_origin_before_correction);E32_FIELD(matched_velocity_before_correction);
    E32_FIELD(camera_base_angles);E32_FIELD(server_punch_angles);E32_FIELD(local_punch_angles);E32_FIELD(camera_final_angles);
    E32_FIELD(collision_plane_index);E32_FIELD(collision_plane_distance);
#undef E32_FIELD
    text+=",\"fallback_reason\":"+core::external_json_string(token(value.fallback_reason));
    text+=",\"correction_anchor_provenance\":"+core::external_json_string(token(value.correction_anchor_provenance));
    text+=",\"remote_sample_mode\":"+core::external_json_string(token(value.remote_sample_mode));
    text+=",\"blocker_kind\":"+core::external_json_string(token(value.blocker_kind));
    text+=",\"collision_query_purpose\":"+core::external_json_string(token(value.collision_query_purpose));
    text+=",\"authoritative_body_kind\":"+core::external_json_string(token(value.authoritative_body_kind));
    text+=",\"presentation_reason\":"+core::external_json_string(token(value.presentation_reason));
    return text+'}';
}
std::string encode_summary(const MovementMetricSummary& summary) {
    std::string text="{\"observed\":"+scalar(summary.observed);
    field(text,"not_available",summary.not_available);field(text,"p50",summary.p50);
    field(text,"p95",summary.p95);field(text,"p99",summary.p99);field(text,"max",summary.maximum);
    return text+'}';
}
bool valid_identity(const SessionReceiptIdentity& identity) noexcept {
    return identity.run_id.size()==32 && identity.process_id!=0 && identity.process_creation!=0 &&
        (identity.role==SessionReceiptRole::A || identity.role==SessionReceiptRole::B) &&
        std::ranges::all_of(identity.run_id,[](char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f');});
}
}

void MovementFrameRecord::set_fallback_reason(std::string_view value) noexcept {set_token(fallback_reason,value);}
void MovementFrameRecord::set_correction_anchor_provenance(std::string_view value) noexcept {set_token(correction_anchor_provenance,value);}
void MovementFrameRecord::set_remote_sample_mode(std::string_view value) noexcept {set_token(remote_sample_mode,value);}
void MovementFrameRecord::set_blocker_kind(std::string_view value) noexcept {set_token(blocker_kind,value);}
void MovementFrameRecord::set_collision_query_purpose(std::string_view value) noexcept {set_token(collision_query_purpose,value);}
void MovementFrameRecord::set_authoritative_body_kind(std::string_view value) noexcept {set_token(authoritative_body_kind,value);}
void MovementFrameRecord::set_presentation_reason(std::string_view value) noexcept {set_token(presentation_reason,value);}
MovementIncidentRecorder::MovementIncidentRecorder(const bool enabled) noexcept:requested_{enabled},enabled_{enabled},session_{enabled},remote_{enabled} {
    if(!enabled_)return;
    try {recent_.resize(maximum_recent_frames);incidents_.reserve(maximum_incidents);}
    catch(...) {enabled_=false;++statistics_.allocation_failures;
        std::vector<Record>{}.swap(recent_);std::vector<MovementIncident>{}.swap(incidents_);}
}
const Record* MovementIncidentRecorder::recent_frame(const std::size_t ordinal) const noexcept {
    return ordinal<count_?&recent_[(first_+ordinal)%maximum_recent_frames]:nullptr;
}
void MovementIncidentRecorder::append_recent(const Record& value) noexcept {
    while(count_ && value.monotonic_us-recent_[first_].monotonic_us>retention_us) {
        first_=(first_+1)%maximum_recent_frames;--count_;++statistics_.retention_evictions;
    }
    if(count_==maximum_recent_frames) {first_=(first_+1)%maximum_recent_frames;--count_;++statistics_.ring_capacity_drops;}
    recent_[(first_+count_)%maximum_recent_frames]=value;++count_;
}
bool MovementIncidentRecorder::observe(const Record& observation,const MovementFrameContext& context) noexcept {
    if(!enabled_)return false;
    if(last_time_ && last_frame_ && observation.monotonic_us==*last_time_ && observation.frame_number==*last_frame_)
        {++statistics_.duplicate_frames;return false;}
    if((last_time_ && observation.monotonic_us<=*last_time_) || (last_frame_ && observation.frame_number<=*last_frame_))
        {++statistics_.rejected_records;return false;}
    Record value=observation;sanitize(value,statistics_.invalid_values);
    session_.observe(value,context);
    if(last_time_ && (network_generation_!=value.network_generation || map_generation_!=value.map_generation)) {
        for(auto& incident:incidents_)if(!incident.complete && !incident.generation_boundary)incident.generation_boundary=true;
        first_=count_=0;last_correction_.reset();previous_prediction_active_.reset();last_auto_.reset();++statistics_.generation_boundaries;
    }
    network_generation_=value.network_generation;map_generation_=value.map_generation;
    append_recent(value);last_time_=value.monotonic_us;last_frame_=value.frame_number;++statistics_.accepted_frames;
    for(auto& incident:incidents_)if(!incident.complete && !incident.generation_boundary) {
        if(value.monotonic_us<=incident.end_us) {
            if(incident.frames.size()<maximum_incident_frames)incident.frames.push_back(value);
            else {++incident.omitted_samples;++statistics_.incident_sample_drops;}
        }
        if(value.monotonic_us>=incident.end_us)incident.complete=true;
    }
    if(value.frame_interval_ms && *value.frame_interval_ms>=frame_stall_ms)
        static_cast<void>(mark_impl(MovementIncidentTrigger::frame_stall,value.monotonic_us,true));
    if(value.correction_ordinal && (!last_correction_ || *last_correction_!=*value.correction_ordinal) &&
        value.raw_correction_units && *value.raw_correction_units>=correction_trigger_units)
        static_cast<void>(mark_impl(MovementIncidentTrigger::correction,value.monotonic_us,true));
    if(value.prediction_active && !*value.prediction_active && previous_prediction_active_.value_or(true))
        static_cast<void>(mark_impl(MovementIncidentTrigger::fallback,value.monotonic_us,true));
    if(value.correction_ordinal)last_correction_=value.correction_ordinal;
    if(value.prediction_active)previous_prediction_active_=value.prediction_active;
    return true;
}
bool MovementIncidentRecorder::mark(MovementIncidentTrigger trigger,std::uint64_t monotonic_us) noexcept {
    // Keep optional user marks on the whole-session timeline even after all
    // eight detailed windows are occupied. Invalid/retrograde marks do not
    // acquire a different acceptance rule in the aggregate recorder.
    if(enabled_ && trigger==MovementIncidentTrigger::manual && last_time_ && monotonic_us>=*last_time_) {
        static_cast<void>(session_.mark_manual(monotonic_us));
        static_cast<void>(remote_.mark_manual(monotonic_us));
    }
    return mark_impl(trigger,monotonic_us,trigger!=MovementIncidentTrigger::manual);
}
bool MovementIncidentRecorder::observe_remote(const MovementFrameRecord& frame,const std::span<const RemoteMovementObservation> actors) noexcept {
    return enabled_ && remote_.observe(frame,actors);
}
bool MovementIncidentRecorder::mark_impl(const MovementIncidentTrigger trigger,const std::uint64_t now,const bool automatic) noexcept {
    if(!enabled_)return false;
    const auto mask=movement_incident_mask(trigger);
    if((mask!=1 && mask!=2 && mask!=4 && mask!=8) || (last_time_ && now<*last_time_))
        {++statistics_.rejected_records;return false;}
    if(!incidents_.empty()) {
        auto& previous=incidents_.back();
        if(!previous.complete && !previous.generation_boundary && now>=previous.trigger_us && now-previous.trigger_us<=coalesce_us) {
            previous.triggers|=mask;
            if(trigger==MovementIncidentTrigger::manual) {
                if(previous.manual_marker_count<previous.manual_marker_us.size())previous.manual_marker_us[previous.manual_marker_count++]=now;
                else ++previous.omitted_manual_markers;
            }
            if(automatic && !last_auto_)last_auto_=now;return true;
        }
    }
    if(automatic && last_auto_ && now>=*last_auto_ && now-*last_auto_<automatic_cooldown_us)
        {++statistics_.auto_cooldown_suppressions;return false;}
    if(incidents_.size()==maximum_incidents){++statistics_.incident_limit_drops;return false;}
    if(automatic && incidents_.size()>=maximum_automatic_incidents) {
        ++statistics_.incident_limit_drops;++statistics_.manual_reservation_suppressions;return false;
    }
    MovementIncident incident;incident.ordinal=static_cast<std::uint64_t>(incidents_.size()+1);
    incident.trigger_us=now;incident.end_us=saturated_add(now,post_incident_us);incident.triggers=mask;
    incident.network_generation=network_generation_;incident.map_generation=map_generation_;
    if(trigger==MovementIncidentTrigger::manual){incident.manual_marker_us[0]=now;incident.manual_marker_count=1;}
    try {incident.frames.reserve(maximum_incident_frames);}
    catch(...) {++statistics_.allocation_failures;return false;}
    const auto oldest=now>=pre_incident_us?now-pre_incident_us:0;
    // Keep the newest pre-window data if a high frame rate exceeds the cap;
    // reserve half for post-window context rather than filling it before F8.
    constexpr auto maximum_pre=maximum_incident_frames/2;
    std::size_t eligible{};
    for(std::size_t i=0;i<count_;++i)if(recent_frame(i)->monotonic_us>=oldest && recent_frame(i)->monotonic_us<=now)++eligible;
    std::size_t skip=eligible>maximum_pre?eligible-maximum_pre:0;
    incident.omitted_samples=static_cast<std::uint64_t>(skip);statistics_.incident_sample_drops+=incident.omitted_samples;
    for(std::size_t i=0;i<count_;++i) {
        const auto& frame=*recent_frame(i);
        if(frame.monotonic_us<oldest || frame.monotonic_us>now)continue;
        if(skip){--skip;continue;}incident.frames.push_back(frame);
    }
    incidents_.push_back(std::move(incident));if(automatic)last_auto_=now;
    return true;
}
bool MovementIncidentRecorder::observe_gpu(const std::uint64_t frame_number,const double gpu_ms) noexcept {
    if(!enabled_)return false;
    if(!std::isfinite(gpu_ms) || gpu_ms<0 || gpu_ms>1'000'000'000.0){
        ++statistics_.gpu_invalid;
        static_cast<void>(session_.observe_gpu(frame_number,gpu_ms));
        return false;
    }
    bool matched{},new_observation{};
    const auto patch=[&](Record& frame) {matched=true;if(!frame.gpu_ms){frame.gpu_ms=gpu_ms;new_observation=true;}};
    std::size_t low{},high=count_;
    while(low<high){const auto middle=low+(high-low)/2;if(recent_frame(middle)->frame_number<frame_number)low=middle+1;else high=middle;}
    if(low<count_ && recent_frame(low)->frame_number==frame_number)patch(recent_[(first_+low)%maximum_recent_frames]);
    for(auto& incident:incidents_) {
        const auto found=std::lower_bound(incident.frames.begin(),incident.frames.end(),frame_number,
            [](const Record& record,std::uint64_t identity){return record.frame_number<identity;});
        if(found!=incident.frames.end() && found->frame_number==frame_number)patch(*found);
    }
    if(!matched)++statistics_.gpu_unmatched;
    if(new_observation)++statistics_.gpu_observations;
    const bool session_matched=session_.observe_gpu(frame_number,gpu_ms);
    return matched || session_matched;
}
MovementDiagnosticsStatistics MovementIncidentRecorder::statistics() const noexcept {
    auto result=statistics_;result.retained_frames=count_;result.incidents=incidents_.size();
    result.allocated_capacity_bytes=recent_.capacity()*sizeof(Record)+incidents_.capacity()*sizeof(MovementIncident);
    for(const auto& incident:incidents_)result.allocated_capacity_bytes+=incident.frames.capacity()*sizeof(Record);
    result.session_capacity_bytes=session_.capacity_bytes();
    result.remote_capacity_bytes=remote_.capacity_bytes();
    result.total_capacity_bytes=result.allocated_capacity_bytes+result.session_capacity_bytes+result.remote_capacity_bytes;
    return result;
}
MovementMetricSummary MovementIncidentRecorder::window_summary(const MovementTimingMetric metric) const {
    MovementMetricSummary result;const auto index=static_cast<std::size_t>(metric);if(index>=metrics.size())return result;
    std::vector<double> values;values.reserve(count_);
    for(std::size_t i=0;i<count_;++i){const auto& observed=recent_frame(i)->*metrics[index].member;
        if(observed)values.push_back(*observed);else ++result.not_available;}
    result.observed=values.size();if(values.empty())return result;
    std::sort(values.begin(),values.end());
    const auto percentile=[&](const double p){const auto rank=static_cast<std::size_t>(std::ceil(p*static_cast<double>(values.size())));return values[std::max(std::size_t{1},rank)-1];};
    result.p50=percentile(.5);result.p95=percentile(.95);result.p99=percentile(.99);result.maximum=values.back();return result;
}
std::optional<std::string> MovementIncidentRecorder::encode(const SessionReceiptIdentity& identity) const {
    if(!valid_identity(identity))return {};
    try {
        std::string text;text.reserve(enabled_?maximum_encoded_bytes:std::size_t{4096});
        text="{\"schema\":\"hlclient.external-movement.v1\",\"run_id\":"+core::external_json_string(identity.run_id);
        field(text,"process_id",identity.process_id);field(text,"process_creation",identity.process_creation);
        field(text,"requested",requested_);field(text,"enabled",enabled_);
        field(text,"session_recording_enabled",session_.enabled());
        text+=",\"local_clock\":\"application_steady_microseconds\",\"quantiles\":\"nearest_rank_recorded_recent_window\"";
        text+=",\"gpu_policy\":\"delayed_identity_bound_nonblocking_or_not_available\",\"causality\":\"observations_not_root_cause_proof\"";
        text+=",\"receive_counter_scope\":\"owned_peer\",\"rejected_sequence_is_packet_loss\":false,\"choke_marker_is_server_load_proof\":false";
        text+=",\"limits\":{\"recent_frames\":"+scalar(maximum_recent_frames);field(text,"retention_us",retention_us);
        field(text,"incidents",maximum_incidents);field(text,"incident_frames",maximum_incident_frames);
        field(text,"manual_reserved_incidents",manual_reserved_incidents);field(text,"automatic_incident_limit",maximum_automatic_incidents);
        field(text,"pre_incident_us",pre_incident_us);field(text,"post_incident_us",post_incident_us);
        field(text,"maximum_capacity_bytes",maximum_capacity_bytes);field(text,"maximum_encoded_bytes",maximum_encoded_bytes);
        field(text,"maximum_session_capacity_bytes",MovementSessionRecorder::maximum_capacity_bytes);
        field(text,"maximum_remote_capacity_bytes",RemoteMovementRecorder::maximum_capacity_bytes);
        field(text,"maximum_total_capacity_bytes",maximum_capacity_bytes+MovementSessionRecorder::maximum_capacity_bytes+RemoteMovementRecorder::maximum_capacity_bytes);
        field(text,"automatic_cooldown_us",automatic_cooldown_us);field(text,"frame_stall_ms",frame_stall_ms);
        field(text,"manual_markers_per_incident",std::size_t{8});
        field(text,"correction_trigger_units",correction_trigger_units);text+='}';
        const auto stats=statistics();text+=",\"statistics\":{\"accepted_frames\":"+scalar(stats.accepted_frames);
#define E32_STAT(name) field(text,#name,stats.name)
        E32_STAT(duplicate_frames);E32_STAT(rejected_records);E32_STAT(invalid_values);E32_STAT(ring_capacity_drops);
        E32_STAT(retention_evictions);E32_STAT(incident_limit_drops);E32_STAT(incident_sample_drops);E32_STAT(auto_cooldown_suppressions);
        E32_STAT(manual_reservation_suppressions);
        E32_STAT(generation_boundaries);E32_STAT(allocation_failures);E32_STAT(gpu_observations);E32_STAT(gpu_unmatched);E32_STAT(gpu_invalid);
        E32_STAT(retained_frames);E32_STAT(incidents);E32_STAT(allocated_capacity_bytes);
        E32_STAT(session_capacity_bytes);E32_STAT(remote_capacity_bytes);E32_STAT(total_capacity_bytes);
#undef E32_STAT
        text+='}';text+=",\"session_summary\":"+session_.encode().value_or("null");
        text+=",\"recent_window\":{\"begin_us\":"+scalar(count_?std::optional{recent_frame(0)->monotonic_us}:std::optional<std::uint64_t>{});
        field(text,"end_us",last_time_);std::uint64_t long_frames{};
        for(std::size_t i=0;i<count_;++i)if(recent_frame(i)->frame_interval_ms && *recent_frame(i)->frame_interval_ms>=frame_stall_ms)++long_frames;
        field(text,"long_frames",long_frames);text+=",\"metrics\":{";
        for(std::size_t i=0;i<metrics.size();++i){if(i)text+=',';text+=core::external_json_string(metrics[i].name)+":{\"unit\":"+
            core::external_json_string(metrics[i].unit)+",\"summary\":"+encode_summary(window_summary(static_cast<MovementTimingMetric>(i)))+'}';}
        text+="},\"frames\":[";
        // Reserve bounded structural room for all eight incident headers and
        // an explicit unavailable remote section. A nearly full legacy report
        // keeps its session/FPS summaries rather than becoming an absent file.
        const std::size_t final_structure_reserve=16U*1024U+(remote_.enabled()?RemoteMovementRecorder::maximum_encoded_bytes:0U);
        std::uint64_t encoded_local_frames_omitted{};
        const auto append=[&](const Record& frame,bool& first) {
            const auto encoded=encode_frame(frame);
            if(text.size()+encoded.size()+1>maximum_encoded_bytes-final_structure_reserve){++encoded_local_frames_omitted;return;}
            if(!first)text+=',';text+=encoded;first=false;
        };
        bool first_recent=true;
        for(std::size_t i=0;i<count_;++i)append(*recent_frame(i),first_recent);
        text+=" ]},\"incidents\":[";
        for(std::size_t i=0;i<incidents_.size();++i) {
            const auto& incident=incidents_[i];if(i)text+=',';
            text+="{\"ordinal\":"+scalar(incident.ordinal);field(text,"trigger_us",incident.trigger_us);field(text,"planned_end_us",incident.end_us);
            field(text,"triggers",incident.triggers);field(text,"network_generation",incident.network_generation);field(text,"map_generation",incident.map_generation);
            field(text,"complete",incident.complete);field(text,"omitted_samples",incident.omitted_samples);
            field(text,"omitted_manual_markers",incident.omitted_manual_markers);
            text+=",\"manual_marker_us\":[";
            for(std::size_t j=0;j<incident.manual_marker_count;++j){if(j)text+=',';text+=scalar(incident.manual_marker_us[j]);}text+=']';
            const auto automatic_mask=incident.triggers&14U;
            const auto primary=automatic_mask==2U?"long_frame_observed":automatic_mask==4U?"new_raw_correction_observed":
                automatic_mask==8U?"prediction_fallback_entry_observed":automatic_mask?"multiple_observed_symptoms":"manual_marker_only";
            text+=",\"primary_evidence\":"+core::external_json_string(primary);
            text+=",\"classification_is_root_cause\":false,\"unmeasured_causes\":\"unknown\"";
            text+=",\"end_status\":"+core::external_json_string(incident.generation_boundary?"generation_boundary":incident.complete?"post_window_complete":"session_close_before_post_window");
            text+=",\"evidence_classes\":[";bool first=true;
            const auto category=[&](bool observed,std::string_view name){if(observed){if(!first)text+=',';text+=core::external_json_string(name);first=false;}};
            category((incident.triggers&movement_incident_mask(MovementIncidentTrigger::frame_stall))!=0,"A_frame_present");
            category((incident.triggers&(movement_incident_mask(MovementIncidentTrigger::correction)|movement_incident_mask(MovementIncidentTrigger::fallback)))!=0,"B_local_prediction_reconciliation");
            category(std::ranges::any_of(incident.frames,[](const Record& frame){return frame.state_age_ms && *frame.state_age_ms>=200.0;}),"C_network_or_remote_state_age");
            category(std::ranges::any_of(incident.frames,[](const Record& frame){return frame.startsolid.value_or(false) || frame.allsolid.value_or(false) || (frame.blocking_entity && frame.trace_fraction && *frame.trace_fraction<1.0);}),"D_collision_support");
            text+="],\"frames\":[";
            bool first_incident_frame=true;
            for(std::size_t j=0;j<incident.frames.size();++j)append(incident.frames[j],first_incident_frame);
            text+="]}";
        }
        text+=']';field(text,"encoded_local_frames_omitted",encoded_local_frames_omitted);text+=",\"remote_players\":";
        const auto remaining=text.size()+2<maximum_encoded_bytes?maximum_encoded_bytes-text.size()-2:std::size_t{};
        text+=remote_.encode(remaining);text+="}\n";if(text.size()>maximum_encoded_bytes)return {};return text;
    } catch(...) {return {};}
}
} // namespace hlclient::app
