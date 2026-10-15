#include <hlclient/app/remote_movement_diagnostics.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace {
namespace app=hlclient::app;
namespace visual=hlclient::entity_visual;
using Recorder=app::RemoteMovementRecorder;
app::MovementFrameRecord remote_frame(std::uint64_t number,std::uint64_t time){
    app::MovementFrameRecord result;result.frame_number=number;result.monotonic_us=time;
    result.network_generation=3;result.map_generation=7;result.prediction_active=true;return result;
}
app::RemoteMovementObservation actor(std::uint32_t entity,double x){
    app::RemoteMovementObservation result;result.entity_index=entity;result.model_index=12;result.lifetime=4;
    result.previous_raw_origin=std::array<double,3>{x-4,2,3};result.current_raw_origin=std::array<double,3>{x+4,2,3};
    result.latest_raw_origin=std::array<double,3>{x+8,2,3};result.sampled_origin=std::array<double,3>{x,2,3};result.rendered_origin=result.sampled_origin;
    result.previous_source_ordinal=10;result.current_source_ordinal=11;result.latest_source_ordinal=12;
    result.previous_server_s=1;result.current_server_s=1.1;result.latest_server_s=1.2;result.sample_server_s=1.05;
    result.selected_main=2;result.selected_gait=3;result.selected_weapon=21;result.raw_main=2;result.raw_gait=3;result.raw_weapon=21;result.raw_effects=0;
    result.pose_submitted=true;result.visible.reset();result.presentation.decision=visual::PlayerPresentationDecision::interpolated;
    result.presentation.position_previous_record_ordinal=10;result.presentation.position_current_record_ordinal=11;result.presentation.alpha=.5;return result;
}
std::string_view member(std::string_view json,std::string_view name){
    const auto key='"'+std::string(name)+"\":";
    std::size_t found=json.npos;unsigned search_depth{};bool search_quoted{},search_escaped{};
    for(std::size_t i=0;i<json.size();++i){const auto c=json[i];
        if(search_quoted){if(search_escaped)search_escaped=false;else if(c=='\\')search_escaped=true;else if(c=='"')search_quoted=false;continue;}
        if(c=='"'){if(search_depth==1 && json.substr(i,key.size())==key){found=i;break;}search_quoted=true;continue;}
        if(c=='{' || c=='[')++search_depth;else if(c=='}' || c==']')--search_depth;}
    if(found==json.npos)return {};
    const auto begin=found+key.size();if(begin==json.size())return {};
    if(json[begin]!='{' && json[begin]!='['){const auto end=json.find_first_of(",}",begin);return json.substr(begin,end-begin);}
    unsigned depth{};bool quoted{},escaped{};
    for(std::size_t i=begin;i<json.size();++i){const auto c=json[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;continue;}if(c=='{' || c=='[')++depth;else if(c=='}' || c==']'){if(--depth==0)return json.substr(begin,i-begin+1);}}
    return {};
}
std::string_view array_element(std::string_view json,std::size_t index){
    if(json.size()<2 || json[0]!='[')return {};std::size_t start=1;unsigned depth{};bool quoted{},escaped{};
    for(std::size_t i=1;i<json.size();++i){const auto c=json[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;continue;}if(c=='{' || c=='[')++depth;else if(c=='}' || c==']'){
            if(c==']' && depth==0)return index==0?json.substr(start,i-start):std::string_view{};--depth;}
        if(c==',' && depth==0){if(index==0)return json.substr(start,i-start);--index;start=i+1;}}
    return {};
}
std::uint64_t count(std::string_view json,std::string_view name){const auto value=member(json,name);REQUIRE_FALSE(value.empty());return std::stoull(std::string(value));}
const app::SessionReceiptIdentity identity{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,123,456};
}
TEST_CASE("E32 remote actor diagnostics separate held actors from interpolated actors without inferred visibility",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder recorder{true};REQUIRE(recorder.enabled());std::array observed{actor(1,10),actor(2,20)};
    observed[0].presentation.decision=visual::PlayerPresentationDecision::held_floor;
    observed[0].presentation.floor_reasons=static_cast<std::uint32_t>(visual::PlayerContinuityReason::same_time_origin_correction);
    observed[0].presentation.new_boundary_reasons=observed[0].presentation.floor_reasons;
    observed[0].presentation.hold_reasons=static_cast<std::uint32_t>(visual::PlayerHoldReason::target_before_floor);
    REQUIRE(recorder.observe(remote_frame(1,1000),observed));
    observed[0].presentation.new_boundary_reasons=0;observed[1].sampled_origin=std::array<double,3>{22,2,3};observed[1].rendered_origin=observed[1].sampled_origin;
    REQUIRE(recorder.observe(remote_frame(2,11000),observed));
    const auto encoded=recorder.encode();CHECK(member(encoded,"availability")=="\"ready\"");
    const auto actors=member(encoded,"actors");const auto a=array_element(actors,0),b=array_element(actors,1);
    CHECK(member(a,"decision_counts")=="[0,0,0,0,0,0,2,0]");CHECK(member(b,"decision_counts")=="[2,0,0,0,0,0,0,0]");
    CHECK(count(a,"new_boundaries")==1);CHECK(member(b,"max_sampled_units")=="2");
    const auto sample=array_element(member(encoded,"recent_samples"),0);
    CHECK(member(sample,"current_raw_origin")=="[14,2,3]");CHECK(member(sample,"latest_raw_origin")=="[18,2,3]");
    CHECK(member(sample,"sampled_origin")=="[10,2,3]");CHECK(member(sample,"rendered_origin")=="[10,2,3]");
    CHECK(member(sample,"pose_submitted")=="true");CHECK(member(sample,"visible")=="null");
    CHECK(recorder.capacity_bytes()<=Recorder::maximum_capacity_bytes);
}
TEST_CASE("E32 remote displacement events preserve exact consecutive frames and distinguish raw source from render delta",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder recorder{true};std::array observed{actor(1,0)};REQUIRE(recorder.observe(remote_frame(1,1000),observed));
    observed[0].sampled_origin=std::array<double,3>{1,2,3};observed[0].rendered_origin=observed[0].sampled_origin;
    REQUIRE(recorder.observe(remote_frame(2,11000),observed));
    observed[0].sampled_origin=std::array<double,3>{10,2,3};observed[0].rendered_origin=std::array<double,3>{12,2,3};
    REQUIRE(recorder.observe(remote_frame(3,21000),observed));
    auto encoded=recorder.encode();auto event=array_element(member(encoded,"events"),1);
    CHECK(count(event,"trigger_mask")==128);CHECK(count(member(event,"before"),"frame_number")==2);CHECK(count(member(event,"current"),"frame_number")==3);
    auto delta=member(event,"delta");CHECK(count(delta,"elapsed_us")==10000);CHECK(member(delta,"raw_units")=="0");
    CHECK(member(delta,"sampled_units")=="9");CHECK(member(delta,"rendered_units")=="11");CHECK(member(delta,"sampled_speed")=="900");
    CHECK(member(delta,"rendered_speed")=="1100");CHECK(recorder.statistics().retained_samples==1);
    observed[0].sampled_origin=std::array<double,3>{20,2,3};observed[0].rendered_origin=observed[0].sampled_origin;
    REQUIRE(recorder.observe(remote_frame(4,31000),observed));CHECK(recorder.statistics().event_cooldown_suppressions==1);
    observed[0].presentation.decision=visual::PlayerPresentationDecision::held_current;
    REQUIRE(recorder.observe(remote_frame(5,41000),observed));CHECK(recorder.statistics().retained_events==3);
    CHECK(recorder.encode().find("observations_not_root_cause_proof")!=std::string::npos);
}
TEST_CASE("E32 remote diagnostics reject duplicates and unordered frames sanitize values and never invent disappearance from malformed input",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder recorder{true};std::array observed{actor(1,0),actor(2,10)};REQUIRE(recorder.observe(remote_frame(1,1000),observed));
    CHECK_FALSE(recorder.observe(remote_frame(1,1000),observed));CHECK_FALSE(recorder.observe(remote_frame(0,2000),observed));
    CHECK_FALSE(recorder.observe(remote_frame(2,500),observed));CHECK(recorder.statistics().duplicate_frames==1);CHECK(recorder.statistics().rejected_frames==2);
    std::array malformed{actor(1,0),actor(1,1),actor(33,0)};
    malformed[0].sampled_origin=std::array<double,3>{std::numeric_limits<double>::quiet_NaN(),0,0};
    malformed[0].current_server_s=std::numeric_limits<double>::infinity();malformed[0].presentation.alpha=-1;
    REQUIRE(recorder.observe(remote_frame(2,201000),malformed));CHECK(recorder.statistics().duplicate_actors==1);
    CHECK(recorder.statistics().invalid_observations==1);CHECK(recorder.statistics().invalid_values==3);CHECK(recorder.statistics().absences==0);
    const auto encoded=recorder.encode();CHECK(encoded.find("NaN")==std::string::npos);CHECK(encoded.find("Infinity")==std::string::npos);
    const auto sample=array_element(member(encoded,"recent_samples"),2);CHECK(member(sample,"sampled_origin")=="null");CHECK(member(sample,"current_server_s")=="null");
}
TEST_CASE("E32 remote disappearance reentry generation and lifetime boundaries retain evidence but never cross compare positions",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder recorder{true};std::array observed{actor(1,0)};REQUIRE(recorder.observe(remote_frame(1,1000),observed));
    REQUIRE(recorder.observe(remote_frame(2,11000),{}));REQUIRE(recorder.observe(remote_frame(3,21000),{}));
    observed[0]=actor(1,1000);REQUIRE(recorder.observe(remote_frame(4,31000),observed));
    observed[0].lifetime=5;observed[0].sampled_origin=std::array<double,3>{2000,2,3};REQUIRE(recorder.observe(remote_frame(5,41000),observed));
    auto next=remote_frame(6,51000);next.map_generation=8;observed[0].sampled_origin=std::array<double,3>{3000,2,3};REQUIRE(recorder.observe(next,observed));
    const auto stats=recorder.statistics();CHECK(stats.absences==1);CHECK(stats.reentries==1);CHECK(stats.lifetime_boundaries==1);CHECK(stats.generation_boundaries==1);
    const auto encoded=recorder.encode();const auto events=member(encoded,"events");
    const auto absent=array_element(events,1);CHECK(count(absent,"trigger_mask")==2);CHECK(member(member(absent,"current"),"present")=="false");
    CHECK(member(member(absent,"current"),"sampled_origin")=="null");
    for(std::size_t i=1;i<5;++i)CHECK(member(member(array_element(events,i),"delta"),"elapsed_us")=="null");
    CHECK(count(array_element(member(encoded,"actors"),0),"comparisons")==0);
}
TEST_CASE("E32 remote recording stays bounded in long sessions and manual capture survives local automatic exhaustion",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    app::MovementIncidentRecorder recorder{true};std::array<app::RemoteMovementObservation,32> observed;
    for(std::size_t i=0;i<observed.size();++i)observed[i]=actor(static_cast<std::uint32_t>(i+1),static_cast<double>(i));
    for(std::uint64_t n=1;n<=80;++n){auto frame=remote_frame(n,n*3'000'000);frame.frame_interval_ms=60;
        REQUIRE(recorder.observe(frame));for(auto& value:observed){value.presentation.decision=n%2?visual::PlayerPresentationDecision::held_floor:visual::PlayerPresentationDecision::interpolated;}
        REQUIRE(recorder.observe_remote(frame,observed));}
    CHECK(recorder.statistics().incident_limit_drops+recorder.statistics().manual_reservation_suppressions>0);
    REQUIRE(recorder.mark(app::MovementIncidentTrigger::manual,240'000'000));
    for(std::uint64_t n=81;n<=400;++n){auto frame=remote_frame(n,240'000'000+(n-80)*200'000);REQUIRE(recorder.observe(frame));REQUIRE(recorder.observe_remote(frame,observed));}
    REQUIRE(recorder.mark(app::MovementIncidentTrigger::manual,304'000'000));
    auto saturated_frame=remote_frame(401,305'000'000);REQUIRE(recorder.observe(saturated_frame));REQUIRE(recorder.observe_remote(saturated_frame,observed));
    CHECK_FALSE(recorder.mark(app::MovementIncidentTrigger::manual,306'000'000));
    const auto encoded=recorder.encode(identity);REQUIRE(encoded);CHECK(encoded->size()<=app::MovementIncidentRecorder::maximum_encoded_bytes);
    const auto remote=member(*encoded,"remote_players");CHECK(member(remote,"availability")=="\"ready\"");
    const auto stats=member(remote,"statistics");CHECK(count(stats,"retained_samples")<=640);CHECK(count(stats,"retained_events")<=128);
    CHECK(count(stats,"event_capacity_drops")>0);CHECK(count(stats,"retention_evictions")>0);CHECK(count(stats,"manual_captures")==2);
    const auto captured=array_element(member(remote,"manual_captures"),1);CHECK(count(captured,"marker_us")==306'000'000);
    CHECK(member(captured,"samples").find("\"monotonic_us\":305000000")!=std::string_view::npos);
    CHECK(recorder.statistics().incidents==8);CHECK(recorder.statistics().incident_limit_drops>0);
    const auto capacity=recorder.statistics();CHECK(capacity.allocated_capacity_bytes<=app::MovementIncidentRecorder::maximum_capacity_bytes);
    CHECK(capacity.remote_capacity_bytes<=Recorder::maximum_capacity_bytes);CHECK(capacity.remote_capacity_bytes>0);
    CHECK(capacity.total_capacity_bytes==capacity.allocated_capacity_bytes+capacity.session_capacity_bytes+capacity.remote_capacity_bytes);
}
TEST_CASE("E32 remote diagnostics disabled and constrained report budget stay explicit and preserve existing local output",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder off;std::array observed{actor(1,0)};CHECK_FALSE(off.observe(remote_frame(1,1000),observed));CHECK_FALSE(off.mark_manual(1000));
    CHECK(off.capacity_bytes()==0);CHECK(off.statistics().accepted_frames==0);CHECK(member(off.encode(),"availability")=="\"disabled\"");
    Recorder recorder{true};REQUIRE(recorder.observe(remote_frame(1,1000),observed));const auto constrained=recorder.encode(256);
    CHECK(constrained.size()<=256);CHECK(member(constrained,"availability")=="\"budget_exceeded\"");
    CHECK(member(constrained,"enabled")=="true");
    for(std::uint64_t n=1;n<=4;++n)REQUIRE(recorder.mark_manual(1000+n));CHECK(recorder.statistics().manual_captures==2);CHECK(recorder.statistics().manual_capture_drops==2);
    app::MovementIncidentRecorder normal{true};REQUIRE(normal.observe(remote_frame(1,1000)));const auto output=normal.encode(identity);REQUIRE(output);
    CHECK_FALSE(member(*output,"session_summary").empty());CHECK_FALSE(member(*output,"recent_window").empty());CHECK_FALSE(member(*output,"remote_players").empty());
    CHECK(member(member(*output,"remote_players"),"availability")=="\"awaiting_presentation\"");
}
TEST_CASE("E32 healthy remote endpoint alternation cannot evict a real displacement and F8 keeps its exact frame",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder recorder{true};std::array<app::RemoteMovementObservation,32> observed;
    for(std::size_t i=0;i<observed.size();++i)observed[i]=actor(static_cast<std::uint32_t>(i+1),static_cast<double>(i));
    REQUIRE(recorder.observe(remote_frame(1,1000),observed));
    observed[0].sampled_origin=std::array<double,3>{10,2,3};observed[0].rendered_origin=observed[0].sampled_origin;
    REQUIRE(recorder.observe(remote_frame(2,11000),observed));
    for(std::uint64_t n=3;n<=100;++n){
        for(auto& value:observed)value.presentation.decision=n%2?visual::PlayerPresentationDecision::exact_current:visual::PlayerPresentationDecision::interpolated;
        REQUIRE(recorder.observe(remote_frame(n,(n-1)*10'000+1000),observed));}
    CHECK(recorder.statistics().retained_events==33);CHECK(recorder.statistics().event_capacity_drops==0);
    REQUIRE(recorder.mark_manual(992000));
    for(std::uint64_t n=101;n<=500;++n){for(auto& value:observed)value.presentation.decision=n%2?visual::PlayerPresentationDecision::held_current:visual::PlayerPresentationDecision::interpolated;
        REQUIRE(recorder.observe(remote_frame(n,(n-1)*10'000+1000),observed));}
    const auto encoded=recorder.encode();const auto capture=array_element(member(encoded,"manual_captures"),0);
    CHECK(count(capture,"marker_us")==992000);CHECK(member(capture,"samples").find("\"frame_number\":100,")!=std::string_view::npos);
    CHECK(member(capture,"events").find("\"frame_number\":2,")!=std::string_view::npos);
    CHECK(recorder.statistics().event_capacity_drops>0);CHECK(encoded.size()<=Recorder::maximum_encoded_bytes);
    CHECK(member(encoded,"availability")=="\"ready\"");
}
TEST_CASE("E32 near report byte limit retains local session summaries and explicit remote availability instead of dropping the report",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    app::MovementIncidentRecorder recorder{true};auto prototype=remote_frame(1,1'000'000'000'000'000);
    for(auto* number:{&prototype.frame_interval_ms,&prototype.cpu_update_ms,&prototype.network_ms,&prototype.parse_commit_ms,&prototype.prediction_ms,
        &prototype.collision_ms,&prototype.studio_ms,&prototype.asset_ms,&prototype.render_ms,&prototype.present_ms,&prototype.report_ms,&prototype.gpu_ms,
        &prototype.svc_time_s,&prototype.command_time_s,&prototype.render_sample_time_s,&prototype.state_age_ms,&prototype.remote_previous_s,
        &prototype.remote_current_s,&prototype.remote_sample_s,&prototype.support_observation_s,&prototype.raw_correction_units,&prototype.camera_jump_units,
        &prototype.replay_duration_ms,&prototype.fallback_duration_ms,&prototype.support_velocity_z})*number=1.2345678901234567;
    prototype.remote_alpha=.12345678901234567;prototype.trace_fraction=.12345678901234567;
    for(auto* vector:{&prototype.raw_server_origin,&prototype.predicted_origin,&prototype.presented_eye,&prototype.velocity,&prototype.plane_normal,
        &prototype.basevelocity,&prototype.previous_valid_origin,&prototype.seed_origin,&prototype.collision_query_start,&prototype.collision_query_end})
        *vector=app::MovementDiagnosticVector3{123456789.12345678,-123456789.12345678,999999999.12345678};
    for(auto* vector:{&prototype.authoritative_velocity,&prototype.matched_origin_before_correction,&prototype.matched_velocity_before_correction,
        &prototype.camera_base_angles,&prototype.server_punch_angles,&prototype.local_punch_angles,&prototype.camera_final_angles})
        *vector=app::MovementDiagnosticFloat3{123456.789F,-234567.891F,345678.912F};
    for(auto* number:{&prototype.source_sequence,&prototype.correction_ordinal,&prototype.collision_revision,&prototype.datagrams_received,
        &prototype.rejected_sequences,&prototype.choke_markers_total,&prototype.authoritative_source_record_identity})*number=18'446'744'073'709'551'000ULL;
    for(auto* number:{&prototype.queue_depth,&prototype.queue_depth_after,&prototype.entities,&prototype.bytes,&prototype.committed_records,
        &prototype.replayed_commands,&prototype.observed_players,&prototype.blocking_entity,&prototype.support_entity,&prototype.support_model_index,
        &prototype.support_submodel,&prototype.support_physent,&prototype.hull,&prototype.anchor_command,&prototype.collision_plane_index,&prototype.authoritative_source_sequence})*number=4'294'967'000U;
    prototype.anchor_status=255;prototype.collision_plane_distance=-123456.789F;prototype.startsolid=false;prototype.allsolid=false;prototype.grounded=true;
    prototype.set_fallback_reason(std::string(95,'a'));prototype.set_correction_anchor_provenance(std::string(47,'b'));
    prototype.set_remote_sample_mode(std::string(31,'c'));prototype.set_blocker_kind(std::string(31,'d'));
    prototype.set_collision_query_purpose(std::string(23,'e'));prototype.set_authoritative_body_kind(std::string(23,'f'));prototype.set_presentation_reason(std::string(39,'g'));
    std::uint64_t number{},time=prototype.monotonic_us;
    for(unsigned incident=0;incident<8;++incident){
        prototype.frame_number=++number;prototype.monotonic_us=++time;REQUIRE(recorder.observe(prototype));
        REQUIRE(recorder.mark(app::MovementIncidentTrigger::manual,time));
        for(unsigned step=0;step<2100;++step){prototype.frame_number=++number;prototype.monotonic_us=(time+=1000);
            for(auto* stamp:{&prototype.socket_read_begin_us,&prototype.socket_read_us,&prototype.network_poll_begin_us,&prototype.network_poll_end_us,
                &prototype.parse_begin_us,&prototype.commit_us,&prototype.receipt_observed_us,&prototype.prediction_use_us,&prototype.presentation_use_us,&prototype.support_observation_us})*stamp=time;
            prototype.command=app::MovementDiagnosticCommand{time,4'294'967'000U,{65000,65001,65002},{32760,-32760,32759},65000,255};
            REQUIRE(recorder.observe(prototype));}}
    std::array observed{actor(1,0)};REQUIRE(recorder.observe_remote(prototype,observed));
    const auto encoded=recorder.encode(identity);REQUIRE(encoded);CHECK(encoded->size()<=app::MovementIncidentRecorder::maximum_encoded_bytes);
    CHECK(count(*encoded,"encoded_local_frames_omitted")>0);CHECK_FALSE(member(*encoded,"session_summary").empty());CHECK_FALSE(member(*encoded,"incidents").empty());
    CHECK_FALSE(member(*encoded,"remote_players").empty());CHECK(recorder.statistics().incidents==8);
    CHECK(recorder.statistics().allocated_capacity_bytes<=app::MovementIncidentRecorder::maximum_capacity_bytes);
}
TEST_CASE("E32 full 32 actor recent events and two manual windows remain ready within both hard bounds",
    "[e32-remote-recorder][movement-diagnostics][app][core]"){
    Recorder recorder{true};std::array<app::RemoteMovementObservation,32> observed;
    for(std::size_t i=0;i<observed.size();++i){observed[i]=actor(static_cast<std::uint32_t>(i+1),static_cast<double>(i)*12.125-4096.5);
        observed[i].previous_raw_origin=std::array<double,3>{-8192.125,4096.25,1024.5};observed[i].current_raw_origin=std::array<double,3>{-8188.125,4100.25,1028.5};
        observed[i].latest_raw_origin=std::array<double,3>{-8184.125,4104.25,1032.5};observed[i].visible=true;
        observed[i].presentation.floor_record_ordinal=10000;observed[i].presentation.resolved_floor_record_ordinal=10001;}
    for(std::uint64_t n=1;n<=2000;++n){for(auto& value:observed){
            value.presentation.decision=n%2?visual::PlayerPresentationDecision::held_floor:visual::PlayerPresentationDecision::interpolated;
            value.presentation.floor_reasons=1024;value.presentation.hold_reasons=n%2?4U:0U;
            value.previous_source_ordinal=10000+n;value.current_source_ordinal=10001+n;value.latest_source_ordinal=10002+n;}
        REQUIRE(recorder.observe(remote_frame(n,n*10'000),observed));
        if(n==1800 || n==1900)REQUIRE(recorder.mark_manual(n*10'000));}
    const auto stats=recorder.statistics();CHECK(stats.retained_samples==640);CHECK(stats.retained_events==128);CHECK(stats.manual_captures==2);
    CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    const auto encoded=recorder.encode();CHECK(encoded.size()<=Recorder::maximum_encoded_bytes);CHECK(member(encoded,"availability")=="\"ready\"");
    const auto captures=member(encoded,"manual_captures");
    CHECK(member(array_element(captures,0),"event_truncated")=="true");
    CHECK(member(array_element(captures,1),"event_truncated")=="true");
    CHECK_FALSE(array_element(member(array_element(captures,0),"events"),63).empty());
    CHECK_FALSE(array_element(member(array_element(captures,1),"events"),63).empty());
}
