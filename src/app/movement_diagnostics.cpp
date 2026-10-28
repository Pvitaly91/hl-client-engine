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
std::optional<std::array<float,3>> multiplayer_vector(const std::optional<std::array<double,3>>& input) noexcept {
    if(!input)return {};std::array<float,3> result{};
    for(std::size_t i=0;i<3;++i){if(!std::isfinite((*input)[i]) || std::abs((*input)[i])>(std::numeric_limits<float>::max)())return {};result[i]=static_cast<float>((*input)[i]);}
    return result;
}
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
std::uint64_t manual_sampling_interval_for(const std::uint64_t now) noexcept {
    // Preserve the available four seconds before F8 and the full second after it
    // within the fixed 8 MiB incident budget, leaving room for exact witnesses.
    constexpr std::size_t exact_reserve=4;
    static_assert(MovementIncidentRecorder::incident_frame_capacity>exact_reserve);
    constexpr auto background_capacity=static_cast<std::uint64_t>(
        MovementIncidentRecorder::incident_frame_capacity-exact_reserve);
    const auto span=std::min(now,MovementIncidentRecorder::manual_pre_incident_us)+
        MovementIncidentRecorder::post_incident_us;
    const auto capacity_interval=span/background_capacity+static_cast<std::uint64_t>(span%background_capacity!=0);
    return std::max(MovementIncidentRecorder::manual_sampling_interval_us,capacity_interval);
}
void bounded_value(std::optional<double>& value,double low,double high,std::uint64_t& invalid) noexcept {
    if(value && (!std::isfinite(*value) || *value<low || *value>high)){value.reset();++invalid;}
}
bool valid_gpu_passes(const MovementGpuPassRecord& value) noexcept {
    if(value.status>MovementGpuPassStatus::capacity_dropped || (value.valid_mask&0xc0U)!=0 ||
        (value.valid_world_subpass_mask&0xf0U)!=0)return false;
    if(value.status!=MovementGpuPassStatus::ready && (value.valid_mask!=0 || value.valid_world_subpass_mask!=0))return false;
    if(value.valid_world_subpass_mask && !(value.valid_mask&1U))return false;
    for(std::size_t i=0;i<value.milliseconds.size();++i)
        if((value.valid_mask&(1U<<i))!=0 && (!std::isfinite(value.milliseconds[i]) ||
            value.milliseconds[i]<0 || value.milliseconds[i]>1e9F))return false;
    for(std::size_t i=0;i<value.world_subpass_milliseconds.size();++i)
        if((value.valid_world_subpass_mask&(1U<<i))!=0 &&
            (!std::isfinite(value.world_subpass_milliseconds[i]) ||
             value.world_subpass_milliseconds[i]<0 || value.world_subpass_milliseconds[i]>1e9F))return false;
    return true;
}
bool valid_prediction_work(const core::PredictionWorkDiagnostics& value) noexcept {
    for(const double duration:{value.command_ms,value.replay_ms,value.collision_query_ms})
        if(!std::isfinite(duration) || duration<0 || duration>1'000'000'000.0)return false;
    return true;
}
bool valid_static_world(const MovementStaticWorldFrameRecord& value) noexcept {
    if(value.path>MovementStaticWorldPath::prepared_bsp ||
        value.fallback_reason>MovementStaticWorldFallbackReason::pvs_conservative)return false;
    if(value.selected_blocks && value.considered_blocks &&
        *value.selected_blocks>*value.considered_blocks)return false;
    if(value.selected_surfaces && value.considered_surfaces &&
        *value.selected_surfaces>*value.considered_surfaces)return false;
    if(value.resource_id.has_value()!=value.resource_revision.has_value())return false;
    if(value.pvs_fallback_code && *value.pvs_fallback_code>5U)return false;
    if(value.pvs_index_upload_bytes && *value.pvs_index_upload_bytes>(std::uint64_t{1U}<<30U))return false;
    if(value.path==MovementStaticWorldPath::none &&
        (value.resource_id || value.considered_blocks || value.tested_bounds ||
         value.selected_blocks || value.considered_surfaces || value.selected_surfaces ||
         value.submitted_draws || value.submitted_triangles || value.prepared_rebuilds ||
         value.bsp_visited_nodes || value.bsp_pvs_rejected_nodes || value.bsp_frustum_rejected_nodes ||
         value.pvs_rebuilds || value.pvs_cache_hits || value.pvs_fallback_code || value.pvs_index_upload_bytes ||
         value.prepared_bytes || value.prepared_allocations ||
         value.submitted_surfaces || value.submitted_ranges || value.selection_preparation_ms ||
         value.command_preparation_ms || value.submission_ms ||
         value.fallback_reason!=MovementStaticWorldFallbackReason::none))return false;
    for(const auto* duration:{&value.selection_preparation_ms,&value.command_preparation_ms,&value.submission_ms})
        if(*duration && (!std::isfinite(**duration) || **duration<0 || **duration>1'000'000'000.0))return false;
    return true;
}
void sanitize(Record& value,std::uint64_t& invalid) noexcept {
    if(!valid_gpu_passes(value.gpu_passes)) {
        value.gpu_passes={};value.gpu_passes.status=MovementGpuPassStatus::invalid;++invalid;
    }
    if(value.prediction_work && !valid_prediction_work(*value.prediction_work))
        {value.prediction_work.reset();++invalid;}
    if(value.static_world && !valid_static_world(*value.static_world))
        {value.static_world.reset();++invalid;}
    for(auto* stamp:{&value.input_sample_us,&value.render_submit_us})if(*stamp>value.monotonic_us){*stamp=0;++invalid;}
    if(value.previous_iteration_waits_known && (value.limiter_wait_us>1'000'000'000'000ULL || value.yield_wait_us>1'000'000'000'000ULL)) {
        value.previous_iteration_waits_known=false;++invalid;
    }
    if (value.excluded_point_boxes!=kUnavailableExcludedPointBoxCount && value.excluded_point_boxes>8192U)
        {value.excluded_point_boxes=kUnavailableExcludedPointBoxCount;++invalid;}
    for(const auto& descriptor:metrics)bounded_value(value.*descriptor.member,0,1'000'000'000.0,invalid);
    for(auto* number:{&value.svc_time_s,&value.command_time_s,&value.render_sample_time_s,
        &value.remote_previous_s,&value.remote_current_s,&value.remote_sample_s,&value.support_observation_s})
        bounded_value(*number,0,1'000'000'000.0,invalid);
    bounded_value(value.remote_alpha,0,1,invalid);
    bounded_value(value.life_wait_duration_ms,0,1'000'000'000.0,invalid);
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
        &value.prediction_use_us,&value.presentation_use_us,&value.support_observation_us,&value.life_wait_started_us})
        if(*stamp && **stamp>value.monotonic_us){stamp->reset();++invalid;}
    set_token(value.fallback_reason,token(value.fallback_reason));
    set_token(value.correction_anchor_provenance,token(value.correction_anchor_provenance));
    set_token(value.remote_sample_mode,token(value.remote_sample_mode));
    set_token(value.blocker_kind,token(value.blocker_kind));
    set_token(value.collision_query_purpose,token(value.collision_query_purpose));
    set_token(value.authoritative_body_kind,token(value.authoritative_body_kind));
    set_token(value.presentation_reason,token(value.presentation_reason));
    set_token(value.recovery_wait_reason,token(value.recovery_wait_reason));
    if(value.candidate_anchor_command && *value.candidate_anchor_command==0U)
        {value.candidate_anchor_command.reset();++invalid;}
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
template<typename T> std::string scalar(const std::array<T,3>& value) {
    return "["+scalar(value[0])+','+scalar(value[1])+','+scalar(value[2])+']';
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
std::string encode_prediction_work(const std::optional<core::PredictionWorkDiagnostics>& value) {
    if(!value)return "null";
    std::string text="{\"command_ms\":"+scalar(value->command_ms);
    field(text,"replay_ms",value->replay_ms);field(text,"collision_query_ms",value->collision_query_ms);
    field(text,"trace_queries",value->trace_queries);field(text,"position_queries",value->position_queries);
    field(text,"contents_queries",value->contents_queries);field(text,"traversal_steps",value->traversal_steps);
    return text+'}';
}
std::string encode_static_world(const std::optional<MovementStaticWorldFrameRecord>& value) {
    if(!value)return "null";
    constexpr std::array paths{"none","original_batches","prepared_bypass","prepared_selected","visible_commands","prepared_bsp"};
    constexpr std::array reasons{"none","prepared_unavailable","insufficient_probe_rejections",
        "invalid_prepared_batch","fragmented_batch","bsp_unavailable","pvs_conservative"};
    std::string text="{\"path\":"+core::external_json_string(paths[static_cast<std::size_t>(value->path)]);
    text+=",\"fallback_reason\":"+core::external_json_string(reasons[static_cast<std::size_t>(value->fallback_reason)]);
#define STATIC_WORLD_FIELD(name) field(text,#name,value->name)
    STATIC_WORLD_FIELD(resource_id);STATIC_WORLD_FIELD(resource_revision);
    STATIC_WORLD_FIELD(considered_blocks);STATIC_WORLD_FIELD(tested_bounds);STATIC_WORLD_FIELD(selected_blocks);
    STATIC_WORLD_FIELD(considered_surfaces);STATIC_WORLD_FIELD(selected_surfaces);
    STATIC_WORLD_FIELD(submitted_draws);STATIC_WORLD_FIELD(submitted_triangles);STATIC_WORLD_FIELD(prepared_rebuilds);
    STATIC_WORLD_FIELD(selection_preparation_ms);
    STATIC_WORLD_FIELD(bsp_visited_nodes);STATIC_WORLD_FIELD(bsp_pvs_rejected_nodes);STATIC_WORLD_FIELD(bsp_frustum_rejected_nodes);
    STATIC_WORLD_FIELD(pvs_rebuilds);STATIC_WORLD_FIELD(pvs_cache_hits);STATIC_WORLD_FIELD(pvs_fallback_code);
    STATIC_WORLD_FIELD(pvs_index_upload_bytes);
    STATIC_WORLD_FIELD(prepared_bytes);STATIC_WORLD_FIELD(prepared_allocations);
    STATIC_WORLD_FIELD(submitted_surfaces);STATIC_WORLD_FIELD(submitted_ranges);
    STATIC_WORLD_FIELD(command_preparation_ms);STATIC_WORLD_FIELD(submission_ms);
#undef STATIC_WORLD_FIELD
    return text+'}';
}
bool valid_stop(const MovementVelocityStopRecord& value,const Record& frame) noexcept {
    const auto finite=[](const auto& vector) {
        return std::ranges::all_of(vector,[](float v){return std::isfinite(v)&&std::abs(v)<=1e9F;});
    };
    if(!value.capture_ordinal || !value.command_sequence || !value.command_msec || value.command_msec>50U ||
        value.network_generation!=frame.network_generation || value.map_generation!=frame.map_generation ||
        value.simulation_end_ns<=value.simulation_start_ns || (value.observed_us && *value.observed_us>frame.monotonic_us) ||
        (value.command_sample_us && *value.command_sample_us>value.observed_us.value_or(frame.monotonic_us)) ||
        value.clip_plane_count>value.clip_planes.size() || value.hull>1U || value.reason>3U || value.candidate>2U ||
        value.phase>7U || value.predecessor_decision>5U || !std::isfinite(value.trace_fraction) ||
        value.trace_fraction<0. || value.trace_fraction>1.)return false;
    for(const auto* vector:{&value.before_origin,&value.after_origin,&value.before_velocity,&value.after_velocity,
        &value.query_start,&value.query_end,&value.trace_end})if(!finite(*vector))return false;
    if(value.before_velocity==std::array<float,3>{} || value.after_velocity!=std::array<float,3>{})return false;
    const auto plane_valid=[&](const auto& plane){return finite(plane.normal)&&std::isfinite(plane.distance);};
    if(value.hit_plane && !plane_valid(*value.hit_plane))return false;
    for(std::size_t i=0;i<value.clip_plane_count;++i)if(!plane_valid(value.clip_planes[i]))return false;
    for(const auto& time:{value.source_server_time_s,value.scene_server_time_s})
        if(time && (!std::isfinite(*time)||*time<0.||*time>1e9))return false;
    return true;
}
std::string encode_stop_plane(const MovementStopPlane& plane) {
    std::string text="{\"normal\":"+scalar(plane.normal);
    field(text,"distance",plane.distance);field(text,"source_index",plane.source_index);return text+'}';
}
std::string encode_velocity_stop(const MovementVelocityStopRecord& value) {
    std::string text="{\"capture_ordinal\":"+scalar(value.capture_ordinal);
#define STOP_FIELD(name) field(text,#name,value.name)
    STOP_FIELD(frame_number);STOP_FIELD(observed_us);STOP_FIELD(network_generation);STOP_FIELD(map_generation);
    STOP_FIELD(prediction_epoch);STOP_FIELD(source_record_identity);STOP_FIELD(source_sequence);STOP_FIELD(source_freshness);
    STOP_FIELD(life_epoch);STOP_FIELD(life_deaths);STOP_FIELD(query_world_revision);STOP_FIELD(query_scene_signature);
    STOP_FIELD(source_server_time_s);STOP_FIELD(scene_record_identity);STOP_FIELD(scene_revision);STOP_FIELD(scene_server_time_s);
    STOP_FIELD(source_matches_scene);STOP_FIELD(command_sequence);STOP_FIELD(command_msec);STOP_FIELD(command_sample_us);
    STOP_FIELD(command_known);STOP_FIELD(command_buttons);STOP_FIELD(command_angle_turns);STOP_FIELD(command_movement);
    STOP_FIELD(simulation_start_ns);STOP_FIELD(simulation_end_ns);STOP_FIELD(history_anchor_command);STOP_FIELD(history_latest_command);
    STOP_FIELD(replay);STOP_FIELD(phase);STOP_FIELD(candidate);STOP_FIELD(hull);STOP_FIELD(predecessor_decision);
    STOP_FIELD(before_origin);STOP_FIELD(after_origin);STOP_FIELD(before_velocity);STOP_FIELD(after_velocity);
    STOP_FIELD(query_start);STOP_FIELD(query_end);STOP_FIELD(trace_end);STOP_FIELD(trace_fraction);
    STOP_FIELD(startsolid);STOP_FIELD(allsolid);STOP_FIELD(in_open);STOP_FIELD(in_liquid);STOP_FIELD(collision_profile);
    STOP_FIELD(start_contents);STOP_FIELD(end_contents);STOP_FIELD(blocking_contents);
    STOP_FIELD(hit_kind);STOP_FIELD(hit_entity);STOP_FIELD(hit_model);STOP_FIELD(hit_instance);
#undef STOP_FIELD
    constexpr std::array reasons{"opposing_planes","clip_solver","no_progress","tiny_residual"};
    constexpr std::array decisions{"not_a_rebase","hard_snap","no_active_support","small_pair_retained",
        "verified_support_pair_retained","incompatible_or_missing_predecessor"};
    text+=",\"reason\":"+core::external_json_string(reasons[value.reason]);
    text+=",\"predecessor_reason\":"+core::external_json_string(decisions[value.predecessor_decision]);
    text+=",\"query_purpose\":\"selected_slide\",\"observed_clock\":\"publication_observation_not_nested_query_walltime\"";
    text+=",\"hit_plane\":"+(value.hit_plane?encode_stop_plane(*value.hit_plane):"null");
    text+=",\"clip_planes\":[";
    for(std::size_t i=0;i<value.clip_plane_count;++i){if(i)text+=',';text+=encode_stop_plane(value.clip_planes[i]);}
    return text+"]}";
}
std::string encode_frame(const Record& value) {
    std::string text="{\"monotonic_us\":"+scalar(value.monotonic_us);
    field(text,"frame_number",value.frame_number);field(text,"network_generation",value.network_generation);
    field(text,"map_generation",value.map_generation);
    field(text,"input_sample_us",value.input_sample_us?std::optional{value.input_sample_us}:std::optional<std::uint64_t>{});
    field(text,"render_submit_us",value.render_submit_us?std::optional{value.render_submit_us}:std::optional<std::uint64_t>{});
    field(text,"limiter_wait_us",value.previous_iteration_waits_known?std::optional{value.limiter_wait_us}:std::optional<std::uint64_t>{});
    field(text,"yield_wait_us",value.previous_iteration_waits_known?std::optional{value.yield_wait_us}:std::optional<std::uint64_t>{});
    field(text,"previous_iteration_waits_known",value.previous_iteration_waits_known);
    for(const auto& descriptor:metrics)field(text,descriptor.name,value.*descriptor.member);
    text+=",\"prediction_work\":"+encode_prediction_work(value.prediction_work);
    text+=",\"static_world\":"+encode_static_world(value.static_world);
    if(value.gpu_passes.status!=MovementGpuPassStatus::not_observed) {
        constexpr std::array statuses{"not_observed","pending","ready","unsupported","disjoint","invalid","capacity_dropped"};
        constexpr std::array names{"world","studio_viewmodel","effects_decals","msaa_resolve","image_adjustments","hud_console"};
        text+=",\"gpu_passes\":{\"status\":"+core::external_json_string(statuses[static_cast<std::size_t>(value.gpu_passes.status)]);
        field(text,"valid_mask",value.gpu_passes.valid_mask);
        for(std::size_t i=0;i<names.size();++i)
            field(text,names[i],(value.gpu_passes.valid_mask&(1U<<i))!=0
                ?std::optional<float>{value.gpu_passes.milliseconds[i]}:std::nullopt);
        constexpr std::array world_names{"target_setup_clear","sky","static_world","runtime_brushes"};
        text+=",\"world_subpasses\":{\"valid_mask\":"+scalar(value.gpu_passes.valid_world_subpass_mask);
        for(std::size_t i=0;i<world_names.size();++i)
            field(text,world_names[i],(value.gpu_passes.valid_world_subpass_mask&(1U<<i))!=0
                ?std::optional<float>{value.gpu_passes.world_subpass_milliseconds[i]}:std::nullopt);
        text+='}';
        text+='}';
    }
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
    E32_FIELD(life_wait_started_us);E32_FIELD(life_wait_duration_ms);E32_FIELD(candidate_anchor_command);
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
    text+=",\"recovery_wait_reason\":"+core::external_json_string(token(value.recovery_wait_reason));
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
bool lifecycle_reason(const Record& frame,const MovementFrameContext& context) noexcept {
    if(context.prediction_owner_state==MovementCaptureOwnerState::failed ||
        context.prediction_owner_state==MovementCaptureOwnerState::active || frame.prediction_active==true)return false;
    const auto reason=token(frame.fallback_reason);
    return reason=="local_player_dead" || reason=="new_life_waiting_for_fresh_carrier" ||
        reason=="waiting_for_server_seed" || reason=="collision_attachment_pending";
}
std::uint8_t capture_context(const MovementFrameContext& context) noexcept {
    if(context.focused==false)return 1;
    if(context.alive==false || context.control_active==false || context.captured==false)return 2;
    return context.focused==true && context.captured==true && context.alive==true && context.control_active==true?0:3;
}
std::uint8_t capture_priority(const MovementIncidentTrigger trigger,const MovementFrameContext& context) noexcept {
    if(trigger==MovementIncidentTrigger::manual)return 100;
    if(context.prediction_owner_state==MovementCaptureOwnerState::failed)return 50;
    if(trigger==MovementIncidentTrigger::frame_stall)return capture_context(context)==0?20:10;
    return capture_context(context)==0?40:30;
}
bool correction_identity_equal(const MovementCorrectionWitness& a,const MovementCorrectionWitness& b) noexcept {
    return a.event_ordinal==b.event_ordinal && a.network_generation==b.network_generation &&
        a.map_generation==b.map_generation && a.life_epoch==b.life_epoch && a.life_deaths==b.life_deaths &&
        a.source_record_identity==b.source_record_identity;
}
bool better_correction(const MovementCorrectionWitness& a,const MovementCorrectionWitness& b) noexcept {
    const auto av=a.raw_position_error.value_or(-1.),bv=b.raw_position_error.value_or(-1.);
    if(av!=bv)return av>bv;
    // Receipt generations are monotonic. Keep the first equal maximum across
    // reconnect/map boundaries, matching the session maximum's strict-greater
    // update rule instead of letting each new event ordinal1 evict its proof.
    return std::array{a.network_generation,a.map_generation,a.event_ordinal,a.life_epoch,a.life_deaths,a.source_record_identity}<
        std::array{b.network_generation,b.map_generation,b.event_ordinal,b.life_epoch,b.life_deaths,b.source_record_identity};
}
bool valid_correction(const MovementCorrectionWitness& value) noexcept {
    if(!value.event_ordinal || value.prediction_owner_state>4 || value.classification>4 || value.camera_status>3 ||
        value.predecessor_decision>5 || value.command_msec>50 || value.hull>1 || value.raw_error_basis>2)return false;
    const auto finite=[](const auto& vector){return std::ranges::all_of(vector,[](const auto v){return std::isfinite(v)&&std::abs(v)<=1e9;});};
    if(!finite(value.authoritative_origin) || !finite(value.authoritative_velocity))return false;
    for(const auto* vector:{&value.matched_origin,&value.matched_velocity,&value.rebase_origin,&value.rebase_velocity,
        &value.position_delta,&value.velocity_delta,&value.rebase_position_delta,&value.before_eye,&value.after_eye,
        &value.query_start,&value.query_end,&value.plane_normal})if(*vector && !finite(**vector))return false;
    for(const auto number:{value.raw_position_error,value.raw_velocity_error,value.same_time_camera_delta,
        value.source_server_time_s,value.scene_server_time_s,value.residual_units})
        if(number && (!std::isfinite(*number) || *number<0 || *number>1e9))return false;
    if(value.support_velocity_z && (!std::isfinite(*value.support_velocity_z) || std::abs(*value.support_velocity_z)>1e9))return false;
    if(value.trace_fraction && (!std::isfinite(*value.trace_fraction) || *value.trace_fraction<0 || *value.trace_fraction>1))return false;
    return true;
}
std::string encode_correction_identity(const MovementCorrectionWitness& value) {
    std::string text="{\"event_ordinal\":"+scalar(value.event_ordinal);
    field(text,"network_generation",value.network_generation);field(text,"map_generation",value.map_generation);
    field(text,"life_epoch",value.life_epoch);field(text,"life_deaths",value.life_deaths);
    field(text,"source_record_identity",value.source_record_identity);return text+'}';
}
std::string encode_correction(const MovementCorrectionWitness& value) {
    std::string text="{\"identity\":"+encode_correction_identity(value);
#define WITNESS_FIELD(name) field(text,#name,value.name)
    WITNESS_FIELD(event_ordinal);WITNESS_FIELD(accepted_ordinal);WITNESS_FIELD(network_generation);WITNESS_FIELD(map_generation);
    WITNESS_FIELD(life_epoch);WITNESS_FIELD(life_deaths);WITNESS_FIELD(source_record_identity);WITNESS_FIELD(source_record_ordinal);
    WITNESS_FIELD(scene_record_identity);WITNESS_FIELD(scene_revision);WITNESS_FIELD(before_prediction_epoch);WITNESS_FIELD(after_prediction_epoch);
    WITNESS_FIELD(carrier_history_revision);WITNESS_FIELD(collision_world_revision);WITNESS_FIELD(collision_scene_signature);
    WITNESS_FIELD(observed_ns);WITNESS_FIELD(source_commit_ns);WITNESS_FIELD(observed_us);WITNESS_FIELD(owner_time_ns);
    WITNESS_FIELD(command_sample_ns);WITNESS_FIELD(presentation_requested_ns);WITNESS_FIELD(presentation_before_ns);WITNESS_FIELD(presentation_after_ns);
    WITNESS_FIELD(source_sequence);WITNESS_FIELD(boundary_command);WITNESS_FIELD(carrier_acknowledgement);WITNESS_FIELD(carrier_outgoing_sequence);
    WITNESS_FIELD(history_anchor_command);WITNESS_FIELD(history_latest_command);WITNESS_FIELD(history_size);
    WITNESS_FIELD(suffix_first_command);WITNESS_FIELD(suffix_last_command);WITNESS_FIELD(suffix_commands);WITNESS_FIELD(suffix_milliseconds);WITNESS_FIELD(replayed_commands);
    WITNESS_FIELD(matched_origin);WITNESS_FIELD(matched_velocity);
    WITNESS_FIELD(rebase_origin);WITNESS_FIELD(rebase_velocity);WITNESS_FIELD(position_delta);WITNESS_FIELD(velocity_delta);WITNESS_FIELD(rebase_position_delta);
    WITNESS_FIELD(before_eye);WITNESS_FIELD(after_eye);WITNESS_FIELD(query_start);WITNESS_FIELD(query_end);WITNESS_FIELD(plane_normal);
    WITNESS_FIELD(raw_position_error);WITNESS_FIELD(raw_velocity_error);WITNESS_FIELD(same_time_camera_delta);
    WITNESS_FIELD(source_server_time_s);WITNESS_FIELD(scene_server_time_s);WITNESS_FIELD(support_velocity_z);WITNESS_FIELD(trace_fraction);WITNESS_FIELD(residual_units);
    WITNESS_FIELD(support_entity);WITNESS_FIELD(support_model);WITNESS_FIELD(blocking_entity);WITNESS_FIELD(blocking_model);
    WITNESS_FIELD(collision_plane_index);WITNESS_FIELD(blocking_instance);WITNESS_FIELD(command_angle_turns);WITNESS_FIELD(command_movement);
    WITNESS_FIELD(command_buttons);WITNESS_FIELD(command_msec);WITNESS_FIELD(prediction_owner_state);WITNESS_FIELD(classification);
    WITNESS_FIELD(camera_status);WITNESS_FIELD(predecessor_decision);WITNESS_FIELD(source_freshness);WITNESS_FIELD(authoritative_origin_freshness);
    WITNESS_FIELD(authoritative_velocity_freshness);WITNESS_FIELD(matched_freshness);WITNESS_FIELD(scene_freshness);WITNESS_FIELD(support_freshness);WITNESS_FIELD(collision_freshness);
    WITNESS_FIELD(presentation_freshness);WITNESS_FIELD(raw_error_basis);WITNESS_FIELD(rebase_status);WITNESS_FIELD(anchor_status);
    WITNESS_FIELD(collision_stage);WITNESS_FIELD(blocking_kind);WITNESS_FIELD(presentation_before_kind);WITNESS_FIELD(presentation_after_kind);
    WITNESS_FIELD(accepted);WITNESS_FIELD(control_active);WITNESS_FIELD(source_matches_scene);WITNESS_FIELD(source_reliable);WITNESS_FIELD(reassembled);
    WITNESS_FIELD(contemporaneous_fragment_suffix);WITNESS_FIELD(alive);
#undef WITNESS_FIELD
    field(text,"startsolid",value.collision_freshness && value.trace_fraction?std::optional{value.startsolid}:std::optional<bool>{});
    field(text,"allsolid",value.collision_freshness && value.trace_fraction?std::optional{value.allsolid}:std::optional<bool>{});
    field(text,"grounded",value.rebase_origin?std::optional{value.grounded}:std::optional<bool>{});
    field(text,"hull",value.rebase_origin || value.collision_freshness?std::optional{value.hull}:std::optional<std::uint8_t>{});
    field(text,"mode",value.rebase_origin?std::optional{value.mode}:std::optional<std::uint8_t>{});
    field(text,"authoritative_origin",value.authoritative_origin_freshness?std::optional{value.authoritative_origin}:std::optional<MovementDiagnosticVector3>{});
    field(text,"authoritative_velocity",value.authoritative_velocity_freshness?std::optional{value.authoritative_velocity}:std::optional<MovementDiagnosticVector3>{});
    text+=",\"reason\":"+core::external_json_string(token(value.reason));
    text+=",\"presentation_reason\":"+core::external_json_string(token(value.presentation_reason));
    text+=",\"camera_measurement\":"+core::external_json_string(value.camera_status==1?"same_time_samples":value.camera_status==2?"different_time_samples":value.camera_status==3?"incompatible_context":"not_measured");
    text+=",\"raw_error_comparison\":"+core::external_json_string(value.raw_error_basis==2?"matched_to_derived_rebase_seed":value.raw_error_basis==1?"matched_to_authoritative_origin":"not_measured");
    text+=",\"support_velocity_semantics\":\"retained_derived_support_model_not_observed_server_velocity\"";
    return text+'}';
}
}

void MovementFrameRecord::set_fallback_reason(std::string_view value) noexcept {set_token(fallback_reason,value);}
void MovementFrameRecord::set_correction_anchor_provenance(std::string_view value) noexcept {set_token(correction_anchor_provenance,value);}
void MovementFrameRecord::set_remote_sample_mode(std::string_view value) noexcept {set_token(remote_sample_mode,value);}
void MovementFrameRecord::set_blocker_kind(std::string_view value) noexcept {set_token(blocker_kind,value);}
void MovementFrameRecord::set_collision_query_purpose(std::string_view value) noexcept {set_token(collision_query_purpose,value);}
void MovementFrameRecord::set_authoritative_body_kind(std::string_view value) noexcept {set_token(authoritative_body_kind,value);}
void MovementFrameRecord::set_presentation_reason(std::string_view value) noexcept {set_token(presentation_reason,value);}
void MovementFrameRecord::set_recovery_wait_reason(std::string_view value) noexcept {set_token(recovery_wait_reason,value);}
MovementIncidentRecorder::MovementIncidentRecorder(const bool enabled) noexcept:requested_{enabled},enabled_{enabled},session_{enabled},remote_{enabled},multiplayer_{enabled} {
    if(!enabled_)return;
    try {recent_.resize(maximum_recent_frames);incidents_.reserve(maximum_incidents);
        velocity_stops_.reserve(maximum_velocity_stops);decisions_.reserve(maximum_capture_decisions);
        correction_top_.reserve(maximum_top_correction_witnesses);correction_recent_.reserve(maximum_recent_correction_witnesses);}
    catch(...) {enabled_=false;++statistics_.allocation_failures;
        std::vector<Record>{}.swap(recent_);std::vector<MovementIncident>{}.swap(incidents_);
        std::vector<MovementVelocityStopRecord>{}.swap(velocity_stops_);std::vector<MovementCaptureDecision>{}.swap(decisions_);
        std::vector<MovementCorrectionWitness>{}.swap(correction_top_);std::vector<MovementCorrectionWitness>{}.swap(correction_recent_);}
}
void MovementIncidentRecorder::observe_correction_delivery(const std::uint64_t network,
    const std::uint64_t map,const std::uint64_t published,const std::uint64_t failures) noexcept {
    if(!enabled_ || !network || !map)return;
    auto& stats=statistics_;
    stats.correction_delivery_observations=saturated_add(stats.correction_delivery_observations,1U);
    const std::array generation{network,map};
    if(correction_delivery_generation_ && generation<*correction_delivery_generation_) {
        stats.correction_delivery_stale_generation_observations=saturated_add(stats.correction_delivery_stale_generation_observations,1U);
        return;
    }
    const bool new_generation=!correction_delivery_generation_ || generation!=*correction_delivery_generation_;
    if(!new_generation && published==correction_delivery_last_published_ && failures==correction_delivery_last_failures_) {
        stats.correction_delivery_duplicate_observations=saturated_add(stats.correction_delivery_duplicate_observations,1U);
        return;
    }
    const bool reset=!new_generation &&
        (published<correction_delivery_last_published_ || failures<correction_delivery_last_failures_);
    if(new_generation && correction_delivery_generation_)
        stats.correction_delivery_generation_boundaries=saturated_add(stats.correction_delivery_generation_boundaries,1U);
    if(reset)stats.correction_delivery_counter_resets=saturated_add(stats.correction_delivery_counter_resets,1U);
    stats.correction_delivery_published_events=saturated_add(stats.correction_delivery_published_events,
        new_generation || reset?published:published-correction_delivery_last_published_);
    stats.correction_delivery_sink_failures=saturated_add(stats.correction_delivery_sink_failures,
        new_generation || reset?failures:failures-correction_delivery_last_failures_);
    correction_delivery_generation_=generation;
    correction_delivery_last_published_=published;correction_delivery_last_failures_=failures;
}
bool MovementIncidentRecorder::observe_correction(const MovementCorrectionWitness& input) noexcept {
    if(!enabled_)return false;
    if(!valid_correction(input)){++statistics_.correction_witness_invalid;return false;}
    const std::array key{input.network_generation,input.map_generation,input.event_ordinal};
    if(last_correction_witness_identity_ && (*last_correction_witness_identity_)[0]==key[0] &&
        (*last_correction_witness_identity_)[1]==key[1] && (*last_correction_witness_identity_)[2]>=key[2]) {
        ++statistics_.correction_witness_duplicates;return false;
    }
    last_correction_witness_identity_=key;auto value=input;
    set_token(value.reason,token(value.reason));set_token(value.presentation_reason,token(value.presentation_reason));
    if(value.camera_status!=1 || !value.before_eye || !value.after_eye || !value.presentation_requested_ns ||
        value.presentation_before_ns!=value.presentation_requested_ns || value.presentation_after_ns!=value.presentation_requested_ns ||
        value.presentation_before_kind!=1 || value.presentation_after_kind!=1) {
        value.same_time_camera_delta.reset();if(value.camera_status==1)value.camera_status=0;
    }
    ++statistics_.correction_witnesses_accepted;if(value.raw_position_error)++correction_events_with_raw_;
    if(value.observed_ns && *value.observed_ns) {
        core::MultiplayerDiagnosticEvent event;event.identity.producer=core::MultiplayerDiagnosticProducer::prediction;
        event.identity.producer_event_id=++multiplayer_prediction_ordinal_;event.identity.network_generation=value.network_generation;
        event.identity.map_generation=value.map_generation;event.identity.local_life=value.life_epoch;
        event.identity.source_record=value.source_record_identity;event.identity.source_record_ordinal=value.source_record_ordinal;
        event.identity.source_sequence=value.source_sequence;event.identity.source_event=value.event_ordinal;
        event.identity.command_id=value.boundary_command;event.local_steady_ns=*value.observed_ns;
        // The accepted owner's prediction generation identifies a replay pass,
        // not a unique physics substep, render frame, or transport execution.
        // Rejected/unknown epochs must not manufacture a simulation identity.
        if(value.accepted && value.after_prediction_epoch)
            event.identity.simulation_iteration=value.after_prediction_epoch;
        event.provenance=core::MultiplayerProvenance::runtime_observed;event.edge=core::MultiplayerEdgeConfidence::exact;
        core::MultiplayerMovementPayload payload;payload.phase=core::MultiplayerPresentationPhase::correction;
        payload.raw_server_origin=multiplayer_vector(value.authoritative_origin);payload.predicted_origin=value.matched_origin;
        payload.presented_eye=value.after_eye;payload.velocity=value.matched_velocity;
        payload.raw_correction_units=value.raw_position_error;payload.camera_jump_units=value.same_time_camera_delta;
        payload.source_seconds=value.source_server_time_s;payload.support_entity=value.support_entity;payload.support_model=value.support_model;
        payload.grounded=value.grounded;payload.startsolid=value.startsolid;payload.allsolid=value.allsolid;
        event.payload=payload;static_cast<void>(multiplayer_.observe(event));
    }
    auto correction_context=update_context_known_?update_context_:context_;
    const bool context_disagreement=(correction_context.life_epoch && *correction_context.life_epoch!=value.life_epoch) ||
        (correction_context.alive && value.alive && correction_context.alive!=value.alive);
    if(context_disagreement)correction_context={};
    else {correction_context.alive=value.alive;correction_context.control_active=value.control_active;}
    correction_context.life_epoch=value.life_epoch;
    correction_context.prediction_owner_state=static_cast<MovementCaptureOwnerState>(value.prediction_owner_state);
    session_.observe_correction(value,correction_context);
    if(correction_top_.size()<maximum_top_correction_witnesses)correction_top_.push_back(value);
    else {
        auto least=std::min_element(correction_top_.begin(),correction_top_.end(),[](const auto& a,const auto& b){return better_correction(b,a);});
        if(better_correction(value,*least))*least=value;
    }
    if(correction_recent_.size()<maximum_recent_correction_witnesses)correction_recent_.push_back(value);
    else {correction_recent_[correction_recent_first_]=value;
        correction_recent_first_=(correction_recent_first_+1)%maximum_recent_correction_witnesses;++statistics_.correction_witness_evictions;}
    Record owning_frame;owning_frame.prediction_active=value.prediction_owner_state==2?std::optional{true}:std::optional<bool>{};
    owning_frame.fallback_reason=value.reason;
    const bool significant=value.raw_position_error && *value.raw_position_error>=correction_trigger_units;
    const bool rejected_failure=!value.accepted && !lifecycle_reason(owning_frame,correction_context);
    if((significant || rejected_failure) && value.observed_us &&
        (!last_time_ || *value.observed_us>=*last_time_)) {
        const auto saved_context=context_;context_=correction_context;
        correction_event_=&value;static_cast<void>(mark_impl(significant?MovementIncidentTrigger::correction:MovementIncidentTrigger::fallback,*value.observed_us,true));
        correction_event_=nullptr;context_=saved_context;
    }
    return true;
}
bool MovementIncidentRecorder::observe_velocity_stop(const Record& frame,const MovementVelocityStopRecord& input) noexcept {
    if(!enabled_)return false;
    const std::array identity{frame.network_generation,frame.map_generation,input.capture_ordinal};
    if(last_velocity_stop_identity_==identity){++statistics_.velocity_stop_duplicates;return false;}
    // An older sticky snapshot cannot become a new event in this generation.
    if(last_velocity_stop_identity_ && (*last_velocity_stop_identity_)[0]==identity[0] &&
        (*last_velocity_stop_identity_)[1]==identity[1] && (*last_velocity_stop_identity_)[2]>identity[2])
        {++statistics_.velocity_stop_invalid;return false;}
    last_velocity_stop_identity_=identity;
    if(!valid_stop(input,frame)){++statistics_.velocity_stop_invalid;return false;}
    auto value=input;value.frame_number=frame.frame_number;
    if(velocity_stops_.size()==maximum_velocity_stops) {
        velocity_stops_[velocity_stop_first_]=value;
        velocity_stop_first_=(velocity_stop_first_+1U)%maximum_velocity_stops;
        ++statistics_.velocity_stop_evictions;return true;
    }
    velocity_stops_.push_back(value);return true; // Fixed reserved capacity; no per-event allocation.
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
    const bool generation_boundary=last_time_ && (network_generation_!=value.network_generation || map_generation_!=value.map_generation);
    const bool life_boundary=last_time_ && context_.life_epoch && context.life_epoch && context_.life_epoch!=context.life_epoch;
    if(generation_boundary || life_boundary) {
        for(auto& incident:incidents_)if(!incident.complete && !incident.generation_boundary && !incident.life_boundary) {
            incident.generation_boundary=incident.network_generation!=value.network_generation || incident.map_generation!=value.map_generation;
            incident.life_boundary=incident.life_epoch && context.life_epoch && incident.life_epoch!=context.life_epoch;
        }
        first_=count_=0;previous_prediction_active_.reset();last_auto_.reset();
        // The producer's accepted-correction ordinal is generation-scoped,
        // not life-scoped. Respawn can retain the previous error snapshot.
        if(generation_boundary)last_correction_.reset();
        previous_fallback_reason_={};automatic_class_time_={};
        if(generation_boundary)++statistics_.generation_boundaries;if(life_boundary)++statistics_.life_boundaries;
    }
    const bool owner_failure_entry=context.prediction_owner_state==MovementCaptureOwnerState::failed &&
        (context_.prediction_owner_state!=MovementCaptureOwnerState::failed || previous_fallback_reason_!=value.fallback_reason);
    context_=context;
    last_observed_life_epoch_=context.life_epoch;
    network_generation_=value.network_generation;map_generation_=value.map_generation;
    append_recent(value);last_time_=value.monotonic_us;last_frame_=value.frame_number;++statistics_.accepted_frames;
    const bool correction_entry=value.correction_ordinal && (!last_correction_ || *last_correction_!=*value.correction_ordinal) &&
        value.raw_correction_units && *value.raw_correction_units>=correction_trigger_units;
    const bool fallback_entry=owner_failure_entry || (value.prediction_active==false &&
        (previous_prediction_active_.value_or(true) || previous_fallback_reason_!=value.fallback_reason));
    const bool stall=value.frame_interval_ms && *value.frame_interval_ms>=frame_stall_ms;
    for(auto& incident:incidents_)if(!incident.complete && !incident.generation_boundary && !incident.life_boundary) {
        if(value.monotonic_us<=incident.end_us) {
            append_incident(incident,value,correction_entry || fallback_entry || stall,false,
                value.monotonic_us==incident.end_us);
        } else if(count_>1) {
            // A frame need not land exactly on the requested endpoint. The
            // immediately preceding observed frame is the truthful last one.
            const auto& prior=*recent_frame(count_-2);
            const auto beginning=incident.trigger_us>=incident.requested_pre_us ?
                incident.trigger_us-incident.requested_pre_us : 0;
            if(prior.monotonic_us>=beginning && prior.monotonic_us<=incident.end_us &&
                prior.network_generation==incident.network_generation &&
                prior.map_generation==incident.map_generation)
                append_incident(incident,prior,false,false,true);
        }
        if(value.monotonic_us>=incident.end_us)incident.complete=true;
    }
    if(stall)
        static_cast<void>(mark_impl(MovementIncidentTrigger::frame_stall,value.monotonic_us,true));
    if(correction_entry)
        static_cast<void>(mark_impl(MovementIncidentTrigger::correction,value.monotonic_us,true));
    if(fallback_entry)
        static_cast<void>(mark_impl(MovementIncidentTrigger::fallback,value.monotonic_us,true));
    if(value.correction_ordinal)last_correction_=value.correction_ordinal;
    if(value.prediction_active)previous_prediction_active_=value.prediction_active;
    previous_fallback_reason_=value.fallback_reason;
    if(multiplayer_clock_origin_ns_ && value.monotonic_us<=((std::numeric_limits<std::uint64_t>::max)()-multiplayer_clock_origin_ns_)/1000U) {
        core::MultiplayerDiagnosticEvent event;event.identity.producer=core::MultiplayerDiagnosticProducer::prediction;
        event.identity.producer_event_id=++multiplayer_prediction_ordinal_;event.identity.network_generation=value.network_generation;
        event.identity.map_generation=value.map_generation;event.identity.local_life=context.life_epoch;
        event.identity.render_frame=value.frame_number;event.identity.source_record=value.authoritative_source_record_identity;
        event.identity.source_sequence=value.source_sequence;if(value.command)event.identity.command_id=value.command->sequence;
        event.local_steady_ns=multiplayer_clock_origin_ns_+value.monotonic_us*1000U;
        event.provenance=core::MultiplayerProvenance::runtime_observed;event.edge=core::MultiplayerEdgeConfidence::exact;
        core::MultiplayerMovementPayload payload;payload.raw_server_origin=multiplayer_vector(value.raw_server_origin);
        payload.predicted_origin=multiplayer_vector(value.predicted_origin);payload.presented_eye=multiplayer_vector(value.presented_eye);
        payload.velocity=multiplayer_vector(value.velocity);payload.basevelocity=multiplayer_vector(value.basevelocity);
        payload.camera_base_angles=value.camera_base_angles;payload.server_punch_angles=value.server_punch_angles;
        payload.local_punch_angles=value.local_punch_angles;payload.camera_final_angles=value.camera_final_angles;
        payload.raw_correction_units=value.raw_correction_units;payload.camera_jump_units=value.camera_jump_units;
        payload.replay_duration_ms=value.replay_duration_ms;payload.source_seconds=value.svc_time_s;payload.render_sample_seconds=value.render_sample_time_s;
        payload.support_entity=value.support_entity;payload.support_model=value.support_model_index;payload.hull=value.hull;
        payload.prediction_active=value.prediction_active;payload.grounded=value.grounded;payload.startsolid=value.startsolid;payload.allsolid=value.allsolid;
        event.payload=payload;static_cast<void>(multiplayer_.observe(event));
    }
    return true;
}
bool MovementIncidentRecorder::mark(MovementIncidentTrigger trigger,std::uint64_t monotonic_us) noexcept {
    // Keep optional user marks on the whole-session timeline even after all
    // eight detailed windows are occupied. Invalid/retrograde marks do not
    // acquire a different acceptance rule in the aggregate recorder.
    if(enabled_ && trigger==MovementIncidentTrigger::manual && last_time_ && monotonic_us>=*last_time_) {
        static_cast<void>(session_.mark_manual(monotonic_us));
        static_cast<void>(remote_.mark_manual(monotonic_us));
        static_cast<void>(multiplayer_.mark_manual_relative_us(monotonic_us));
    }
    return mark_impl(trigger,monotonic_us,trigger!=MovementIncidentTrigger::manual);
}
bool MovementIncidentRecorder::observe_remote(const MovementFrameRecord& frame,const std::span<const RemoteMovementObservation> actors) noexcept {
    if(!enabled_ || !remote_.observe(frame,actors))return false;
    if(!multiplayer_clock_origin_ns_ || frame.monotonic_us>((std::numeric_limits<std::uint64_t>::max)()-multiplayer_clock_origin_ns_)/1000U)return true;
    const auto stamp=multiplayer_clock_origin_ns_+frame.monotonic_us*1000U;
    for(std::size_t i=0;i<std::min(actors.size(),RemoteMovementRecorder::maximum_actors);++i) {
        const auto& actor=actors[i];if(!actor.entity_index || actor.entity_index>32 || !actor.lifetime)continue;
        core::MultiplayerDiagnosticEvent event;event.identity.producer=core::MultiplayerDiagnosticProducer::remote_presentation;
        event.identity.producer_event_id=++multiplayer_remote_ordinal_;event.identity.network_generation=frame.network_generation;
        event.identity.map_generation=frame.map_generation;event.identity.actor_entity=actor.entity_index;
        event.identity.actor_visual_lifetime=actor.lifetime;event.identity.render_frame=frame.frame_number;
        event.identity.source_record_ordinal=actor.current_source_ordinal;event.local_steady_ns=stamp;
        event.provenance=core::MultiplayerProvenance::runtime_observed;event.edge=core::MultiplayerEdgeConfidence::exact;
        core::MultiplayerRemotePayload payload;payload.previous_raw_origin=multiplayer_vector(actor.previous_raw_origin);
        payload.current_raw_origin=multiplayer_vector(actor.current_raw_origin);payload.latest_raw_origin=multiplayer_vector(actor.latest_raw_origin);
        payload.sampled_origin=multiplayer_vector(actor.sampled_origin);payload.rendered_origin=multiplayer_vector(actor.rendered_origin);
        payload.previous_source_seconds=actor.previous_server_s;payload.current_source_seconds=actor.current_server_s;
        payload.latest_source_seconds=actor.latest_server_s;payload.sample_seconds=actor.sample_server_s;payload.alpha=actor.presentation.alpha;
        payload.previous_source_ordinal=actor.previous_source_ordinal;payload.current_source_ordinal=actor.current_source_ordinal;payload.latest_source_ordinal=actor.latest_source_ordinal;
        payload.model_index=actor.model_index;payload.selected_main=actor.selected_main;payload.selected_gait=actor.selected_gait;payload.selected_weapon=actor.selected_weapon;
        payload.support_entity=actor.support_entity;payload.support_model=actor.support_model;payload.support_offset_z=actor.support_offset_z;
        payload.pose_submitted=actor.pose_submitted;payload.visible=actor.visible;payload.decision=static_cast<std::uint32_t>(actor.presentation.decision);
        payload.floor_reasons=actor.presentation.floor_reasons;payload.new_boundary_reasons=actor.presentation.new_boundary_reasons;
        payload.hold_reasons=actor.presentation.hold_reasons;payload.clock_reasons=actor.presentation.clock_reasons;
        event.payload=payload;static_cast<void>(multiplayer_.observe(event));
    }
    return true;
}
void MovementIncidentRecorder::capture_decision(const MovementIncidentTrigger trigger,const std::uint64_t now,const std::uint8_t priority,
    const MovementCaptureDecisionKind decision,const std::uint64_t incident,const std::uint64_t replacement) noexcept {
    MovementCaptureDecision value;value.ordinal=++decision_ordinal_;value.monotonic_us=now;
    value.trigger=movement_incident_mask(trigger);value.priority=priority;value.decision=decision;
    value.context=capture_context(context_);value.owner_state=static_cast<std::uint8_t>(context_.prediction_owner_state);
    value.network_generation=network_generation_;value.map_generation=map_generation_;value.life_epoch=context_.life_epoch;
    value.incident_ordinal=incident;value.replacement_ordinal=replacement;
    if(count_) {const auto& frame=*recent_frame(count_-1);value.frame_number=frame.frame_number;
        value.source_record_identity=frame.authoritative_source_record_identity;value.source_sequence=frame.source_sequence;
        value.correction_ordinal=frame.correction_ordinal;value.reason=frame.fallback_reason;}
    if(correction_event_) {value.network_generation=correction_event_->network_generation;value.map_generation=correction_event_->map_generation;
        value.source_record_identity=correction_event_->source_record_identity;value.source_sequence=correction_event_->source_sequence;
        value.correction_ordinal=correction_event_->event_ordinal;value.reason=correction_event_->reason;}
    if(decisions_.size()<maximum_capture_decisions)decisions_.push_back(value);
    else {decisions_[decision_first_]=value;decision_first_=(decision_first_+1)%maximum_capture_decisions;++statistics_.decision_evictions;}
}
void MovementIncidentRecorder::append_incident(MovementIncident& incident,const Record& frame,
    const bool exact,const bool marker,const bool coverage_endpoint) noexcept {
    if(!incident.frames.empty() && incident.frames.back().frame_number==frame.frame_number) {
        if(exact)incident.exact_samples[incident.frames.size()-1]=marker?2U:std::max<std::uint8_t>(1U,incident.exact_samples[incident.frames.size()-1]);return;
    }
    const auto beginning=incident.trigger_us>=incident.requested_pre_us?incident.trigger_us-incident.requested_pre_us:0;
    const auto bucket=(frame.monotonic_us-beginning)/incident.sampling_interval_us;
    if(!exact && !incident.frames.empty() && bucket==incident.last_sample_bucket) {
        if(coverage_endpoint && incident.frames.size()>1 && !incident.exact_samples[incident.frames.size()-1]) {
            // Keep the actual latest timestamp without consuming a slot or
            // misclassifying an ordinary endpoint as an exact event witness.
            incident.frames.back()=frame;
            ++incident.background_samples_omitted;++incident.omitted_samples;++statistics_.background_samples_omitted;
            return;
        }
        if(!coverage_endpoint) {
            ++incident.background_samples_omitted;++incident.omitted_samples;++statistics_.background_samples_omitted;return;
        }
    }
    if(incident.frames.size()==incident_frame_capacity) {
        if(exact || coverage_endpoint) {
            std::size_t victim=incident.frames.size();
            for(std::size_t i=1;i<incident.frames.size();++i)if(!incident.exact_samples[i]){victim=i;break;}
            if(victim==incident.frames.size() && marker)for(std::size_t i=1;i<incident.frames.size();++i)
                if(incident.exact_samples[i]==1U){victim=i;break;}
            if(victim<incident.frames.size()) {
                if(incident.exact_samples[victim]){++incident.exact_samples_omitted;++statistics_.exact_samples_omitted;}
                else {++incident.background_samples_omitted;++statistics_.background_samples_omitted;}
                incident.frames.erase(incident.frames.begin()+static_cast<std::ptrdiff_t>(victim));
                for(std::size_t i=victim;i+1<incident_frame_capacity;++i)incident.exact_samples[i]=incident.exact_samples[i+1];
                ++incident.omitted_samples;
            } else {
                ++incident.omitted_samples;++statistics_.incident_sample_drops;
                if(exact){++incident.exact_samples_omitted;++statistics_.exact_samples_omitted;}
                else {++incident.background_samples_omitted;++statistics_.background_samples_omitted;}
                return;
            }
        } else {++incident.background_samples_omitted;++incident.omitted_samples;++statistics_.background_samples_omitted;
            ++statistics_.incident_sample_drops;return;}
    }
    incident.frames.push_back(frame);incident.exact_samples[incident.frames.size()-1]=marker?2U:exact?1U:0U;incident.last_sample_bucket=bucket;
}
bool MovementIncidentRecorder::mark_impl(const MovementIncidentTrigger trigger,const std::uint64_t now,const bool automatic) noexcept {
    if(!enabled_)return false;
    const auto mask=movement_incident_mask(trigger);
    if((mask!=1 && mask!=2 && mask!=4 && mask!=8) || (last_time_ && now<*last_time_))
        {++statistics_.rejected_records;capture_decision(trigger,now,0,MovementCaptureDecisionKind::invalid_timestamp);return false;}
    const auto priority=capture_priority(trigger,context_);
    if(automatic && !correction_event_ && count_ && lifecycle_reason(*recent_frame(count_-1),context_)) {
        ++statistics_.lifecycle_summaries;capture_decision(trigger,now,priority,MovementCaptureDecisionKind::expected_lifecycle);return false;
    }
    const auto network=correction_event_?correction_event_->network_generation:network_generation_;
    const auto map=correction_event_?correction_event_->map_generation:map_generation_;
    for(auto& previous:incidents_) {
        if(!previous.complete && !previous.generation_boundary && !previous.life_boundary && previous.network_generation==network &&
            previous.map_generation==map && previous.life_epoch==context_.life_epoch && now>=previous.trigger_us && now-previous.trigger_us<=coalesce_us) {
            const bool promote=trigger==MovementIncidentTrigger::manual && (previous.triggers&1U)==0;
            std::array<std::uint64_t,maximum_incident_frames> promoted_exact_ids;
            std::size_t promoted_exact_count{};bool preservable=true;
            if(promote)for(std::size_t j=0;j<previous.frames.size();++j)if(previous.exact_samples[j]) {
                const auto identity=previous.frames[j].frame_number;bool retained{};
                for(std::size_t k=0;k<count_;++k)if(recent_frame(k)->frame_number==identity){retained=true;break;}
                if(!retained){preservable=false;break;}
                promoted_exact_ids[promoted_exact_count++]=identity;
            }
            // At extreme rates the recent ring may no longer own an earlier
            // exact automatic witness. Keep that automatic window intact and
            // use a reserved manual slot instead of losing its evidence.
            if(!preservable)continue;
            previous.triggers|=mask;
            if(correction_event_ && !correction_event_->accepted)previous.producer_rejection=true;
            previous.priority=std::max(previous.priority,priority);
            if(promote) {
                previous.trigger_us=now;previous.end_us=saturated_add(now,post_incident_us);
                previous.requested_pre_us=manual_pre_incident_us;previous.sampling_interval_us=manual_sampling_interval_for(now);
                previous.frames.clear();previous.exact_samples={};previous.omitted_samples=0;previous.background_samples_omitted=0;
                const auto oldest=now>=manual_pre_incident_us?now-manual_pre_incident_us:0;
                for(std::size_t i=0;i<count_;++i)if(recent_frame(i)->monotonic_us>=oldest && recent_frame(i)->monotonic_us<=now) {
                    const auto& frame=*recent_frame(i);
                    const bool retained_exact=std::find(promoted_exact_ids.begin(),promoted_exact_ids.begin()+promoted_exact_count,
                        frame.frame_number)!=promoted_exact_ids.begin()+promoted_exact_count;
                    append_incident(previous,frame,frame.monotonic_us==now || retained_exact);
                }
            }
            if(trigger==MovementIncidentTrigger::manual) {
                if(previous.manual_marker_count<previous.manual_marker_us.size())previous.manual_marker_us[previous.manual_marker_count++]=now;
                else ++previous.omitted_manual_markers;
            }
            if(count_ && recent_frame(count_-1)->monotonic_us==now)append_incident(previous,*recent_frame(count_-1),true,trigger==MovementIncidentTrigger::manual);
            capture_decision(trigger,now,priority,MovementCaptureDecisionKind::coalesced,previous.ordinal);
            if(automatic && !automatic_class_time_[priority/10])automatic_class_time_[priority/10]=now;
            if(automatic && !last_auto_)last_auto_=now;return true;
        }
    }
    const auto class_index=static_cast<std::size_t>(priority/10);
    if(automatic && automatic_class_time_[class_index] && now>=*automatic_class_time_[class_index] &&
        now-*automatic_class_time_[class_index]<automatic_cooldown_us) {
        ++statistics_.auto_cooldown_suppressions;capture_decision(trigger,now,priority,MovementCaptureDecisionKind::cooldown);return false;
    }
    const auto automatic_count=static_cast<std::size_t>(std::ranges::count_if(incidents_,[](const auto& value){return (value.triggers&1U)==0;}));
    std::optional<std::size_t> replacement;
    if(incidents_.size()==maximum_incidents || (automatic && automatic_count>=maximum_automatic_incidents)) {
        if(automatic)for(std::size_t i=0;i<incidents_.size();++i) {
            const auto& value=incidents_[i];if((value.triggers&1U)!=0 || (!value.complete && !value.generation_boundary && !value.life_boundary))continue;
            if(!replacement || value.priority<incidents_[*replacement].priority ||
                (value.priority==incidents_[*replacement].priority && value.ordinal<incidents_[*replacement].ordinal))replacement=i;
        }
        if(!replacement || incidents_[*replacement].priority>priority) {
            ++statistics_.incident_limit_drops;
            const auto reason=replacement?MovementCaptureDecisionKind::lower_priority:
                automatic && automatic_count>=maximum_automatic_incidents?MovementCaptureDecisionKind::manual_reservation:MovementCaptureDecisionKind::capacity;
            if(reason==MovementCaptureDecisionKind::manual_reservation)++statistics_.manual_reservation_suppressions;
            capture_decision(trigger,now,priority,reason);return false;
        }
    }
    MovementIncident incident;incident.ordinal=++incident_ordinal_;incident.priority=priority;incident.life_epoch=context_.life_epoch;
    incident.trigger_us=now;incident.initial_trigger_us=now;incident.end_us=saturated_add(now,post_incident_us);incident.triggers=mask;
    incident.network_generation=network;incident.map_generation=map;
    incident.producer_rejection=correction_event_ && !correction_event_->accepted;
    incident.requested_pre_us=automatic?pre_incident_us:manual_pre_incident_us;incident.requested_post_us=post_incident_us;
    incident.sampling_interval_us=automatic?automatic_sampling_interval_us:manual_sampling_interval_for(now);
    if(trigger==MovementIncidentTrigger::manual){incident.manual_marker_us[0]=now;incident.manual_marker_count=1;}
    // Replacement reuses its reserved backing store: no transient ninth
    // incident-frame allocation may break the strict high-water budget.
    if(replacement){incident.frames.swap(incidents_[*replacement].frames);incident.frames.clear();}
    try {incident.frames.reserve(incident_frame_capacity);}
    catch(...) {++statistics_.allocation_failures;capture_decision(trigger,now,priority,MovementCaptureDecisionKind::allocation_failed);return false;}
    const auto oldest=now>=incident.requested_pre_us?now-incident.requested_pre_us:0;
    for(std::size_t i=0;i<count_;++i) {
        const auto& frame=*recent_frame(i);
        if(frame.monotonic_us<oldest || frame.monotonic_us>now || frame.network_generation!=network || frame.map_generation!=map)continue;
        if(correction_event_ && last_observed_life_epoch_ && context_.life_epoch!=last_observed_life_epoch_)continue;
        append_incident(incident,frame,frame.monotonic_us==now,trigger==MovementIncidentTrigger::manual && frame.monotonic_us==now);
    }
    const auto ordinal=incident.ordinal;
    if(replacement) {const auto replaced=incidents_[*replacement].ordinal;incidents_[*replacement]=std::move(incident);
        ++statistics_.priority_replacements;capture_decision(trigger,now,priority,MovementCaptureDecisionKind::replaced,ordinal,replaced);}
    else {incidents_.push_back(std::move(incident));capture_decision(trigger,now,priority,MovementCaptureDecisionKind::accepted,ordinal);}
    if(automatic){last_auto_=now;automatic_class_time_[class_index]=now;}
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
bool MovementIncidentRecorder::observe_gpu_passes(const std::uint64_t frame_number,
    const MovementGpuPassRecord& passes) noexcept {
    if(!enabled_)return false;
    if(!valid_gpu_passes(passes)){++statistics_.gpu_invalid;return false;}
    bool matched{};
    const auto patch=[&](Record& frame) {
        matched=true;
        // A duplicate/late poll cannot replace an already completed result.
        if(frame.gpu_passes.status==MovementGpuPassStatus::not_observed ||
           frame.gpu_passes.status==MovementGpuPassStatus::pending)frame.gpu_passes=passes;
    };
    std::size_t low{},high=count_;
    while(low<high){const auto middle=low+(high-low)/2;if(recent_frame(middle)->frame_number<frame_number)low=middle+1;else high=middle;}
    if(low<count_ && recent_frame(low)->frame_number==frame_number)patch(recent_[(first_+low)%maximum_recent_frames]);
    for(auto& incident:incidents_) {
        const auto found=std::lower_bound(incident.frames.begin(),incident.frames.end(),frame_number,
            [](const Record& record,std::uint64_t identity){return record.frame_number<identity;});
        if(found!=incident.frames.end() && found->frame_number==frame_number)patch(*found);
    }
    return matched;
}
MovementDiagnosticsStatistics MovementIncidentRecorder::statistics() const noexcept {
    auto result=statistics_;result.retained_frames=count_;result.incidents=incidents_.size();
    result.allocated_capacity_bytes=recent_.capacity()*sizeof(Record)+incidents_.capacity()*sizeof(MovementIncident);
    result.allocated_capacity_bytes+=velocity_stops_.capacity()*sizeof(MovementVelocityStopRecord);
    result.allocated_capacity_bytes+=decisions_.capacity()*sizeof(MovementCaptureDecision);
    result.allocated_capacity_bytes+=(correction_top_.capacity()+correction_recent_.capacity())*sizeof(MovementCorrectionWitness);
    result.retained_decisions=decisions_.size();result.retained_correction_witnesses=correction_top_.size();
    for(const auto& witness:correction_recent_)if(!std::ranges::any_of(correction_top_,[&](const auto& top){return correction_identity_equal(witness,top);}))
        ++result.retained_correction_witnesses;
    result.velocity_stops=velocity_stops_.size();
    for(const auto& incident:incidents_)result.allocated_capacity_bytes+=incident.frames.capacity()*sizeof(Record);
    result.session_capacity_bytes=session_.capacity_bytes();
    result.remote_capacity_bytes=remote_.capacity_bytes();
    result.multiplayer_capacity_bytes=multiplayer_.capacity_bytes();
    result.total_capacity_bytes=result.allocated_capacity_bytes+result.session_capacity_bytes+result.remote_capacity_bytes+result.multiplayer_capacity_bytes;
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
        constexpr auto recorder_budget=maximum_encoded_bytes-publication_metadata_reserve;
        const bool has_velocity_stop_evidence=!velocity_stops_.empty() || statistics_.velocity_stop_duplicates ||
            statistics_.velocity_stop_evictions || statistics_.velocity_stop_invalid;
        std::string text;text.reserve(enabled_?maximum_encoded_bytes:std::size_t{4096});
        text="{\"schema\":\"hlclient.external-movement.v2\",\"run_id\":"+core::external_json_string(identity.run_id);
        field(text,"process_id",identity.process_id);field(text,"process_creation",identity.process_creation);
        field(text,"requested",requested_);field(text,"enabled",enabled_);
        field(text,"session_recording_enabled",session_.enabled());
        text+=",\"local_clock\":\"application_steady_microseconds\",\"quantiles\":\"nearest_rank_recorded_recent_window\"";
        text+=",\"gpu_policy\":\"delayed_identity_bound_nonblocking_or_not_available\",\"causality\":\"observations_not_root_cause_proof\"";
        text+=",\"prediction_work_semantics\":\"durations_overlap_prediction_ms_collision_query_ms_is_subset_counts_are_actual_calls_traversal_steps_are_successfully_reported\"";
        if(enabled_)text+=",\"static_world_receipt\":{\"schema\":\"hlclient.static-world-frame.v1\",\"scope\":\"completed_renderer_frame_in_recent_window_or_incident\",\"counter_semantics\":\"per_frame_not_cumulative\",\"considered_blocks_semantics\":\"eligible_immutable_pool\",\"tested_bounds_semantics\":\"frustum_calls_including_retests\",\"selected_blocks_semantics\":\"submitted_pool_blocks\",\"selection_preparation_scope\":\"visibility_classification_excludes_gl_submission_and_map_load\",\"command_preparation_scope\":\"selected_geometry_command_preparation\",\"submission_scope\":\"cpu_static_world_gl_submission_not_gpu_time\",\"duration_relationship\":\"separate_scopes_do_not_infer_additivity_with_frame_or_gpu\",\"prepared_bytes_semantics\":\"retained_prepared_resource_bytes_gauge_not_delta\",\"prepared_allocations_semantics\":\"per_frame_prepared_vector_storage_events_not_all_runtime_allocations\",\"submitted_ranges_semantics\":\"index_ranges_not_gl_api_call_count\",\"submitted_triangles_semantics\":\"geometry_not_visible_pixels\",\"unknown\":\"null\",\"omission\":\"bounded_recent_window_and_incident_sampling\"}";
        if(enabled_)text+=",\"pacing_wait_policy\":\"previous_completed_iteration_not_current_frame_execution\"";
        text+=",\"receive_counter_scope\":\"owned_peer\",\"rejected_sequence_is_packet_loss\":false,\"choke_marker_is_server_load_proof\":false";
        text+=",\"limits\":{\"recent_frames\":"+scalar(maximum_recent_frames);field(text,"retention_us",retention_us);
        field(text,"incidents",maximum_incidents);field(text,"incident_frames",maximum_incident_frames);
        field(text,"manual_reserved_incidents",manual_reserved_incidents);field(text,"automatic_incident_limit",maximum_automatic_incidents);
        field(text,"pre_incident_us",pre_incident_us);field(text,"post_incident_us",post_incident_us);
        if(enabled_) {
            field(text,"incident_frame_capacity",incident_frame_capacity);
            field(text,"manual_pre_incident_us",manual_pre_incident_us);field(text,"manual_sampling_interval_us",manual_sampling_interval_us);
            field(text,"automatic_sampling_interval_us",automatic_sampling_interval_us);
        }
        field(text,"maximum_capacity_bytes",maximum_capacity_bytes);field(text,"maximum_encoded_bytes",maximum_encoded_bytes);
        field(text,"maximum_session_capacity_bytes",MovementSessionRecorder::maximum_capacity_bytes);
        field(text,"maximum_remote_capacity_bytes",RemoteMovementRecorder::maximum_capacity_bytes);
        field(text,"maximum_multiplayer_capacity_bytes",MultiplayerTimeline::maximum_capacity_bytes);
        field(text,"maximum_multiplayer_encoded_bytes",MultiplayerTimeline::maximum_encoded_bytes);
        field(text,"maximum_total_capacity_bytes",maximum_capacity_bytes+MovementSessionRecorder::maximum_capacity_bytes+RemoteMovementRecorder::maximum_capacity_bytes+MultiplayerTimeline::maximum_capacity_bytes);
        field(text,"automatic_cooldown_us",automatic_cooldown_us);field(text,"frame_stall_ms",frame_stall_ms);
        field(text,"manual_markers_per_incident",std::size_t{8});
        if(has_velocity_stop_evidence)field(text,"selected_slide_velocity_stops",maximum_velocity_stops);
        field(text,"correction_trigger_units",correction_trigger_units);text+='}';
        const auto stats=statistics();text+=",\"statistics\":{\"accepted_frames\":"+scalar(stats.accepted_frames);
#define E32_STAT(name) field(text,#name,stats.name)
        E32_STAT(duplicate_frames);E32_STAT(rejected_records);E32_STAT(invalid_values);E32_STAT(ring_capacity_drops);
        E32_STAT(retention_evictions);E32_STAT(incident_limit_drops);E32_STAT(incident_sample_drops);E32_STAT(auto_cooldown_suppressions);
        E32_STAT(manual_reservation_suppressions);
        if(enabled_) {
            E32_STAT(lifecycle_summaries);E32_STAT(priority_replacements);E32_STAT(decision_evictions);E32_STAT(life_boundaries);
            E32_STAT(background_samples_omitted);E32_STAT(exact_samples_omitted);E32_STAT(retained_decisions);E32_STAT(retained_correction_witnesses);
            E32_STAT(correction_witnesses_accepted);E32_STAT(correction_witness_duplicates);E32_STAT(correction_witness_invalid);E32_STAT(correction_witness_evictions);
        }
        E32_STAT(generation_boundaries);E32_STAT(allocation_failures);E32_STAT(gpu_observations);E32_STAT(gpu_unmatched);E32_STAT(gpu_invalid);
        E32_STAT(retained_frames);E32_STAT(incidents);E32_STAT(allocated_capacity_bytes);
        E32_STAT(session_capacity_bytes);E32_STAT(remote_capacity_bytes);E32_STAT(multiplayer_capacity_bytes);E32_STAT(total_capacity_bytes);
        if(has_velocity_stop_evidence) {
            E32_STAT(velocity_stops);E32_STAT(velocity_stop_duplicates);E32_STAT(velocity_stop_evictions);E32_STAT(velocity_stop_invalid);
        }
#undef E32_STAT
        text+='}';
        if(enabled_) {
            text+=",\"critical_corrections\":{\"schema\":\"hlclient.critical-corrections.v1\",\"enabled\":true";
            field(text,"maximum_top_witnesses",maximum_top_correction_witnesses);field(text,"maximum_recent_witnesses",maximum_recent_correction_witnesses);
            field(text,"accepted_events",stats.correction_witnesses_accepted);field(text,"events_with_raw",correction_events_with_raw_);
            field(text,"retained_witnesses",stats.retained_correction_witnesses);field(text,"duplicates",stats.correction_witness_duplicates);
            field(text,"invalid",stats.correction_witness_invalid);field(text,"evictions",stats.correction_witness_evictions);
            text+=",\"delivery_status\":"+core::external_json_string(!stats.correction_delivery_observations?"not_observed":
                stats.correction_delivery_sink_failures || stats.correction_delivery_counter_resets ||
                stats.correction_delivery_stale_generation_observations?"partial":"complete");
            field(text,"delivery_observations",stats.correction_delivery_observations);
            field(text,"delivery_sink_failures",stats.correction_delivery_sink_failures);
            field(text,"delivery_published_events",stats.correction_delivery_published_events);
            field(text,"delivery_counter_resets",stats.correction_delivery_counter_resets);
            field(text,"delivery_generation_boundaries",stats.correction_delivery_generation_boundaries);
            field(text,"delivery_duplicate_observations",stats.correction_delivery_duplicate_observations);
            field(text,"delivery_stale_generation_observations",stats.correction_delivery_stale_generation_observations);
            text+=",\"delivery_counter_policy\":\"generation_qualified_cumulative_deltas_reset_baselines_mark_partial\"";
            const MovementCorrectionWitness* maximum{};
            for(const auto& witness:correction_top_)if(witness.raw_position_error && (!maximum || better_correction(witness,*maximum)))maximum=&witness;
            field(text,"maximum_raw_position_error",maximum?maximum->raw_position_error:std::optional<double>{});
            text+=",\"maximum_witness_identity\":"+(maximum?encode_correction_identity(*maximum):"null");
            text+=",\"maximum_witness_status\":"+core::external_json_string(maximum?"available":"not_measured");
            text+=",\"legacy_summary_witness_status\":\"not_linked_frame_snapshot\",\"ns_clock\":\"steady_epoch_not_receipt_clock\",\"owner_time_ns_clock\":\"logical_caller_argument\"";
            text+=",\"witnesses\":[";bool first=true;
            for(const auto& witness:correction_top_){if(!first)text+=',';text+=encode_correction(witness);first=false;}
            for(std::size_t i=0;i<correction_recent_.size();++i){const auto& witness=correction_recent_[(correction_recent_first_+i)%correction_recent_.size()];
                if(std::ranges::any_of(correction_top_,[&](const auto& top){return correction_identity_equal(witness,top);}))continue;
                if(!first)text+=',';text+=encode_correction(witness);first=false;}
            text+="]}";
            text+=",\"capture_policy\":{\"schema\":\"hlclient.priority-movement-capture.v1\",\"sampling\":\"first_observed_frame_per_time_bucket_plus_exact_witnesses\"";
            field(text,"maximum_decisions",maximum_capture_decisions);field(text,"decision_evictions",stats.decision_evictions);
            text+=",\"replacement\":\"completed_automatic_lowest_priority_then_oldest_ordinal_never_manual\",\"coalescing\":\"same_network_map_life_fixed_post_deadline\",\"decisions\":[";
            constexpr std::array names{"accepted","coalesced","expected_lifecycle","cooldown","manual_reservation","capacity","lower_priority","active_windows","allocation_failed","replaced","invalid_timestamp"};
            for(std::size_t i=0;i<decisions_.size();++i){const auto& decision=decisions_[(decision_first_+i)%decisions_.size()];if(i)text+=',';
                text+="{\"ordinal\":"+scalar(decision.ordinal);
#define DECISION_FIELD(name) field(text,#name,decision.name)
                DECISION_FIELD(monotonic_us);DECISION_FIELD(frame_number);DECISION_FIELD(network_generation);DECISION_FIELD(map_generation);
                DECISION_FIELD(life_epoch);DECISION_FIELD(source_record_identity);DECISION_FIELD(source_sequence);DECISION_FIELD(correction_ordinal);
                DECISION_FIELD(incident_ordinal);DECISION_FIELD(replacement_ordinal);DECISION_FIELD(trigger);DECISION_FIELD(priority);DECISION_FIELD(context);DECISION_FIELD(owner_state);
#undef DECISION_FIELD
                text+=",\"decision\":"+core::external_json_string(names[static_cast<std::size_t>(decision.decision)]);
                text+=",\"reason\":"+core::external_json_string(token(decision.reason))+'}';}
            text+="],\"exact_frames\":[";bool first_exact=true;
            for(const auto& incident:incidents_)for(std::size_t j=0;j<incident.frames.size();++j) {
                const auto& frame=incident.frames[j];
                const bool retained_marker=std::find(incident.manual_marker_us.begin(),incident.manual_marker_us.begin()+incident.manual_marker_count,
                    frame.monotonic_us)!=incident.manual_marker_us.begin()+incident.manual_marker_count;
                if(frame.monotonic_us!=incident.initial_trigger_us && !retained_marker)continue;
                if(!first_exact)text+=',';first_exact=false;
                text+="{\"incident_ordinal\":"+scalar(incident.ordinal)+",\"frame\":"+encode_frame(frame)+'}';
            }
            text+="]}";
        }
        std::vector<MovementCorrectionWitness> retained_witnesses;retained_witnesses.reserve(maximum_correction_witnesses);
        for(const auto& witness:correction_top_)retained_witnesses.push_back(witness);
        for(const auto& witness:correction_recent_)if(!std::ranges::any_of(correction_top_,[&](const auto& top){return correction_identity_equal(witness,top);}))
            retained_witnesses.push_back(witness);
        text+=",\"session_summary\":"+session_.encode(retained_witnesses).value_or("null");
        if(has_velocity_stop_evidence) {
            text+=",\"selected_slide_velocity_stops\":[";
            for(std::size_t i=0;i<velocity_stops_.size();++i){if(i)text+=',';
                text+=encode_velocity_stop(velocity_stops_[(velocity_stop_first_+i)%velocity_stops_.size()]);}
            text+=']';
        }
        // Reserve bounded structural room for all eight incident headers and
        // an explicit unavailable remote section. A nearly full legacy report
        // keeps its session/FPS summaries rather than becoming an absent file.
        const std::size_t final_structure_reserve=16U*1024U+(remote_.enabled()?RemoteMovementRecorder::maximum_encoded_bytes:0U)+MultiplayerTimeline::maximum_encoded_bytes;
        std::uint64_t encoded_local_frames_omitted{};
        const auto append=[&](const Record& frame,bool& first) {
            const auto encoded=encode_frame(frame);
            if(text.size()+encoded.size()+1>recorder_budget-final_structure_reserve){++encoded_local_frames_omitted;return false;}
            if(!first)text+=',';text+=encoded;first=false;return true;
        };
        std::array<std::size_t,maximum_incidents> order{};
        for(std::size_t i=0;i<incidents_.size();++i)order[i]=i;
        std::sort(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(incidents_.size()),[&](const auto a,const auto b) {
            const auto& left=incidents_[a];const auto& right=incidents_[b];
            if(left.priority!=right.priority)return left.priority>right.priority;
            return left.ordinal<right.ordinal;
        });
        text+=",\"incidents\":[";
        for(std::size_t i=0;i<incidents_.size();++i) {
            const auto& incident=incidents_[order[i]];if(i)text+=',';
            text+="{\"ordinal\":"+scalar(incident.ordinal);field(text,"trigger_us",incident.trigger_us);field(text,"initial_trigger_us",incident.initial_trigger_us);field(text,"planned_end_us",incident.end_us);
            field(text,"triggers",incident.triggers);field(text,"network_generation",incident.network_generation);field(text,"map_generation",incident.map_generation);
            field(text,"priority",incident.priority);field(text,"life_epoch",incident.life_epoch);
            field(text,"requested_pre_us",incident.requested_pre_us);field(text,"requested_post_us",incident.requested_post_us);
            field(text,"sampling_interval_us",incident.sampling_interval_us);field(text,"retained_samples",incident.frames.size());
            const auto actual_pre=!incident.frames.empty() && incident.frames.front().monotonic_us<=incident.trigger_us?
                incident.trigger_us-incident.frames.front().monotonic_us:0;
            const auto actual_post=!incident.frames.empty() && incident.frames.back().monotonic_us>=incident.trigger_us?
                incident.frames.back().monotonic_us-incident.trigger_us:0;
            field(text,"actual_pre_us",actual_pre);field(text,"actual_post_us",actual_post);
            field(text,"background_samples_omitted",incident.background_samples_omitted);field(text,"exact_samples_omitted",incident.exact_samples_omitted);
            text+=",\"coverage_reason\":"+core::external_json_string(incident.exact_samples_omitted?"capacity_detail_omitted":
                incident.generation_boundary?"generation_boundary":incident.life_boundary?"life_boundary":
                actual_pre<incident.requested_pre_us?"start_or_generation_boundary_or_recent_capacity":
                !incident.complete?"session_close_before_post_window":incident.background_samples_omitted?"time_bucket_sampling":"full_available_window");
            field(text,"complete",incident.complete);field(text,"omitted_samples",incident.omitted_samples);
            field(text,"omitted_manual_markers",incident.omitted_manual_markers);
            text+=",\"manual_marker_us\":[";
            for(std::size_t j=0;j<incident.manual_marker_count;++j){if(j)text+=',';text+=scalar(incident.manual_marker_us[j]);}text+=']';
            const auto automatic_mask=incident.triggers&14U;
            const auto primary=automatic_mask==2U?"long_frame_observed":automatic_mask==4U?"new_raw_correction_observed":
                automatic_mask==8U?incident.producer_rejection?"producer_correction_rejected":"prediction_fallback_entry_observed":automatic_mask?"multiple_observed_symptoms":"manual_marker_only";
            text+=",\"primary_evidence\":"+core::external_json_string(primary);
            text+=",\"classification_is_root_cause\":false,\"unmeasured_causes\":\"unknown\"";
            text+=",\"end_status\":"+core::external_json_string(incident.generation_boundary?"generation_boundary":incident.life_boundary?"life_boundary":incident.complete?"post_window_complete":"session_close_before_post_window");
            text+=",\"evidence_classes\":[";bool first=true;
            const auto category=[&](bool observed,std::string_view name){if(observed){if(!first)text+=',';text+=core::external_json_string(name);first=false;}};
            category((incident.triggers&movement_incident_mask(MovementIncidentTrigger::frame_stall))!=0,"A_frame_present");
            category((incident.triggers&(movement_incident_mask(MovementIncidentTrigger::correction)|movement_incident_mask(MovementIncidentTrigger::fallback)))!=0,"B_local_prediction_reconciliation");
            category(std::ranges::any_of(incident.frames,[](const Record& frame){return frame.state_age_ms && *frame.state_age_ms>=200.0;}),"C_network_or_remote_state_age");
            category(std::ranges::any_of(incident.frames,[](const Record& frame){return frame.startsolid.value_or(false) || frame.allsolid.value_or(false) || (frame.blocking_entity && frame.trace_fraction && *frame.trace_fraction<1.0);}),"D_collision_support");
            text+="],\"frames\":[";
            bool first_incident_frame=true;std::uint64_t omitted{};std::optional<std::uint64_t> encoded_first,encoded_last;
            for(const auto& frame:incident.frames) {
                if(append(frame,first_incident_frame)){if(!encoded_first)encoded_first=frame.monotonic_us;encoded_last=frame.monotonic_us;}
                else ++omitted;
            }
            text+=']';field(text,"encoded_samples_omitted",omitted);
            field(text,"encoded_actual_pre_us",encoded_first && *encoded_first<=incident.trigger_us?std::optional{incident.trigger_us-*encoded_first}:std::optional<std::uint64_t>{});
            field(text,"encoded_actual_post_us",encoded_last && *encoded_last>=incident.trigger_us?std::optional{*encoded_last-incident.trigger_us}:std::optional<std::uint64_t>{});
            text+='}';
        }
        text+="],\"recent_window\":{\"begin_us\":"+scalar(count_?std::optional{recent_frame(0)->monotonic_us}:std::optional<std::uint64_t>{});
        field(text,"end_us",last_time_);std::uint64_t long_frames{};
        for(std::size_t i=0;i<count_;++i)if(recent_frame(i)->frame_interval_ms && *recent_frame(i)->frame_interval_ms>=frame_stall_ms)++long_frames;
        field(text,"long_frames",long_frames);text+=",\"metrics\":{";
        // OFF has no metric observations or allocated history. Preserve the
        // schema's empty map, rather than repeating a full catalogue of null
        // quantiles that can dominate the bounded disabled receipt.
        if(enabled_)for(std::size_t i=0;i<metrics.size();++i){if(i)text+=',';text+=core::external_json_string(metrics[i].name)+":{\"unit\":"+
            core::external_json_string(metrics[i].unit)+",\"summary\":"+encode_summary(window_summary(static_cast<MovementTimingMetric>(i)))+'}';}
        text+="},\"frames\":[";bool first_recent=true;
        for(std::size_t i=0;i<count_;++i)static_cast<void>(append(*recent_frame(i),first_recent));
        text+="]}";field(text,"encoded_local_frames_omitted",encoded_local_frames_omitted);text+=",\"remote_players\":";
        const auto remaining=text.size()+2+MultiplayerTimeline::maximum_encoded_bytes<recorder_budget?
            recorder_budget-text.size()-2-MultiplayerTimeline::maximum_encoded_bytes:std::size_t{};
        text+=remote_.encode(remaining);text+=",\"multiplayer_timeline\":";
        const auto multiplayer=multiplayer_.encode();
        text+=multiplayer?*multiplayer:"{\"schema\":\"hlclient.multiplayer-timeline.v1\",\"encoding_failed\":true}";
        text+="}\n";if(text.size()>recorder_budget)return {};return text;
    } catch(...) {return {};}
}
} // namespace hlclient::app
