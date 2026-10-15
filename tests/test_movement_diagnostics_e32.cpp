#include <hlclient/app/movement_diagnostics.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
namespace app=hlclient::app;
using Recorder=app::MovementIncidentRecorder;
using Trigger=app::MovementIncidentTrigger;
using Metric=app::MovementTimingMetric;
app::MovementFrameRecord frame(std::uint64_t number,std::uint64_t time) {
    app::MovementFrameRecord value;value.frame_number=number;value.monotonic_us=time;
    value.network_generation=3;value.map_generation=5;value.frame_interval_ms=10.0;
    value.prediction_active=true;return value;
}
const app::SessionReceiptIdentity identity{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,123,456};
void export_session_fixture(const std::string& text) {
    std::string path;
#ifdef _MSC_VER
    char* value{};std::size_t length{};
    REQUIRE(::_dupenv_s(&value,&length,"HLCLIENT_MOVEMENT_SESSION_FIXTURE_OUTPUT")==0);
    if(value)path=value;std::free(value);
#else
    if(const auto* value=std::getenv("HLCLIENT_MOVEMENT_SESSION_FIXTURE_OUTPUT"))path=value;
#endif
    if(path.empty())return;
    REQUIRE_FALSE(std::filesystem::exists(path));
    std::ofstream output(path,std::ios::binary);REQUIRE(output.good());
    output<<text;output.close();REQUIRE(output.good());
}
}
TEST_CASE("E32.1 causal recorder keeps wire command matched correction and camera rotation separate",
    "[e321][movement-diagnostics][core][app]") {
    Recorder recorder{true};auto value=frame(1,1'000'000);
    value.command=app::MovementDiagnosticCommand{900'000,42,{65500,123,0},{320,-160,0},5,20};
    value.authoritative_source_record_identity=73;value.authoritative_source_sequence=99;value.anchor_command=40;
    value.anchor_status=std::uint8_t{8};
    value.authoritative_velocity=app::MovementDiagnosticFloat3{320,0,-12};
    value.matched_origin_before_correction=app::MovementDiagnosticFloat3{2,3,4};
    value.matched_velocity_before_correction=app::MovementDiagnosticFloat3{300,-12,2};
    value.raw_server_origin=app::MovementDiagnosticVector3{2.125,3,4};
    value.camera_base_angles=app::MovementDiagnosticFloat3{10,20,0};
    value.server_punch_angles=app::MovementDiagnosticFloat3{-2,0,0};
    value.local_punch_angles=app::MovementDiagnosticFloat3{-.5F,0,0};
    value.camera_final_angles=app::MovementDiagnosticFloat3{11.5F,20,0};
    value.collision_plane_index=12;value.collision_plane_distance=70.0F;
    value.excluded_point_boxes=6;
    value.set_collision_query_purpose("replay_suffix");value.set_authoritative_body_kind("fragment_suffix");
    value.set_presentation_reason("visual_correction_blocked");
    REQUIRE(recorder.observe(value));const auto* observed=recorder.recent_frame(0);REQUIRE(observed);
    CHECK(observed->command->sequence==42);CHECK(observed->command->buttons==5);
    CHECK(observed->camera_final_angles==value.camera_final_angles);
    CHECK(observed->camera_base_angles!=observed->camera_final_angles);
    CHECK(observed->matched_origin_before_correction==value.matched_origin_before_correction);
    REQUIRE(recorder.mark(Trigger::manual,value.monotonic_us));
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"command\":{\"sequence\":42,\"sample_us\":900000,\"msec\":20,\"buttons\":5")!=output->npos);
    CHECK(output->find("\"movement\":[320,-160,0]")!=output->npos);
    CHECK(output->find("\"camera_final_angles\":[11.5,20,0]")!=output->npos);
    CHECK(output->find("\"authoritative_body_kind\":\"fragment_suffix\"")!=output->npos);
    CHECK(output->find("\"authoritative_source_record_identity\":73")!=output->npos);
    CHECK(output->find("\"authoritative_source_sequence\":99")!=output->npos);
    CHECK(output->find("\"excluded_point_boxes\":6")!=output->npos);
    CHECK(output->find("\"anchor_status\":8")!=output->npos);
    CHECK(output->find("\"matched_velocity_before_correction\":[300,-12,2]")!=output->npos);
    CHECK(output->find("\"collision_query_purpose\":\"replay_suffix\"")!=output->npos);
    value=frame(2,1'010'000);value.command=app::MovementDiagnosticCommand{2'000'000,43,{},{},0,20};
    value.camera_final_angles=app::MovementDiagnosticFloat3{std::numeric_limits<float>::quiet_NaN(),0,0};
    REQUIRE(recorder.observe(value));CHECK_FALSE(recorder.recent_frame(1)->command);
    CHECK_FALSE(recorder.recent_frame(1)->camera_final_angles);
    CHECK_FALSE(recorder.recent_frame(1)->authoritative_velocity);
    CHECK(recorder.statistics().invalid_values==2);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
}
TEST_CASE("E32 normal recorder publishes bounded whole-session evidence alongside detailed windows",
    "[e32-session][movement-diagnostics][core][app]") {
    Recorder recorder{true};
    const app::MovementFrameContext playing{true,true,true,true,1};
    auto value=frame(1,1'000'000);
    value.frame_interval_ms=std::numeric_limits<double>::quiet_NaN();
    value.set_fallback_reason("PRIVATE path should not be retained");
    REQUIRE(recorder.observe(value,playing));
    CHECK_FALSE(recorder.observe(value,playing));
    REQUIRE(recorder.observe_gpu(1,4.25));
    REQUIRE(recorder.observe_gpu(1,99.0));
    CHECK_FALSE(recorder.observe_gpu(1,std::numeric_limits<double>::infinity()));
    for(std::uint64_t i=2;i<=1400;++i) {
        value=frame(i,i*10'000+1'000'000);
        REQUIRE(recorder.observe(value,playing));
    }
    const auto stats=recorder.statistics();
    CHECK(stats.accepted_frames==1400);
    CHECK(stats.duplicate_frames==1);
    CHECK(stats.invalid_values==1);
    CHECK(stats.retained_frames<stats.accepted_frames);
    CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    CHECK(stats.session_capacity_bytes>0);
    CHECK(stats.session_capacity_bytes<=app::MovementSessionRecorder::maximum_capacity_bytes);
    CHECK(stats.remote_capacity_bytes>0);
    CHECK(stats.remote_capacity_bytes<=app::RemoteMovementRecorder::maximum_capacity_bytes);
    CHECK(stats.total_capacity_bytes==stats.allocated_capacity_bytes+stats.session_capacity_bytes+stats.remote_capacity_bytes);
    const auto text=recorder.encode(identity);REQUIRE(text);
    CHECK(text->find("\"session_summary\":{\"schema\":\"hlclient.movement-session.v1\"")!=text->npos);
    CHECK(text->find("\"session_recording_enabled\":true")!=text->npos);
    CHECK(text->find("controlled_alive")!=text->npos);
    CHECK(text->find("\"invalid\":1")!=text->npos);
    CHECK(text->find("PRIVATE")==text->npos);
    CHECK(text->size()<=Recorder::maximum_encoded_bytes);
    export_session_fixture(*text);
    Recorder disabled;
    CHECK(disabled.statistics().session_capacity_bytes==0);
    CHECK(disabled.statistics().remote_capacity_bytes==0);
    CHECK(disabled.statistics().total_capacity_bytes==0);
}
TEST_CASE("E32 disabled incident recorder performs no collection allocation or gameplay mutation", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder;
    auto value=frame(1,1'000'000);value.predicted_origin=app::MovementDiagnosticVector3{1,2,3};
    for(std::uint64_t i=1;i<10000;++i){value.frame_number=i;value.monotonic_us=i*1000;
        CHECK_FALSE(recorder.observe(value));}
    CHECK(value.predicted_origin==app::MovementDiagnosticVector3{1,2,3});
    CHECK_FALSE(recorder.mark(Trigger::manual,value.monotonic_us));CHECK_FALSE(recorder.observe_gpu(1,2));
    const auto stats=recorder.statistics();CHECK(stats.allocated_capacity_bytes==0);CHECK(stats.accepted_frames==0);
    CHECK(stats.incidents==0);CHECK(stats.retained_frames==0);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"enabled\":false")!=output->npos);
    CHECK(output->size()<4096);CHECK_FALSE(recorder.recent_frame(0));
}
TEST_CASE("E32 manual incident preserves exact prior and following frames with a documented window", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};REQUIRE(recorder.enabled());
    for(std::uint64_t i=0;i<=250;++i){REQUIRE(recorder.observe(frame(i,i*10'000)));
        if(i==150)REQUIRE(recorder.mark(Trigger::manual,i*10'000));}
    REQUIRE(recorder.incidents().size()==1);const auto& incident=recorder.incidents().front();
    CHECK(incident.trigger_us==1'500'000);CHECK(incident.end_us==2'500'000);CHECK(incident.complete);
    CHECK_FALSE(incident.generation_boundary);CHECK(incident.omitted_samples==0);
    REQUIRE(incident.frames.size()==201);CHECK(incident.frames.front().monotonic_us==500'000);
    CHECK(incident.frames.back().monotonic_us==2'500'000);
    CHECK(incident.triggers==app::movement_incident_mask(Trigger::manual));
    CHECK(incident.manual_marker_count==1);CHECK(incident.manual_marker_us[0]==1'500'000);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("post_window_complete")!=output->npos);
    CHECK(output->find("\"gpu_policy\":\"delayed_identity_bound_nonblocking_or_not_available\"")!=output->npos);
}
TEST_CASE("E32 automatic triggers coalesce causal evidence and avoid repeated correction fallback incidents", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};REQUIRE(recorder.observe(frame(1,1'000'000)));
    auto observed=frame(2,1'100'000);observed.frame_interval_ms=55;observed.raw_correction_units=20;
    observed.correction_ordinal=7;observed.prediction_active=false;
    observed.set_fallback_reason("solid_start");REQUIRE(recorder.observe(observed));
    REQUIRE(recorder.incidents().size()==1);
    CHECK(recorder.incidents().front().triggers==14);
    REQUIRE(recorder.mark(Trigger::manual,1'100'000));CHECK(recorder.incidents().front().triggers==15);
    REQUIRE(recorder.mark(Trigger::manual,1'150'000));
    CHECK(recorder.incidents().front().manual_marker_count==2);
    CHECK(recorder.incidents().front().manual_marker_us[1]==1'150'000);
    observed=frame(3,1'400'000);observed.raw_correction_units=20;observed.correction_ordinal=7;observed.prediction_active=false;
    observed.set_fallback_reason("solid_start");REQUIRE(recorder.observe(observed));
    CHECK(recorder.incidents().size()==1); // Same correction identity, still in fallback.
    observed=frame(4,1'500'000);observed.raw_correction_units=30;observed.correction_ordinal=8;
    REQUIRE(recorder.observe(observed));CHECK(recorder.incidents().size()==1);
    CHECK(recorder.statistics().auto_cooldown_suppressions==1);
    observed=frame(5,3'200'000);observed.raw_correction_units=30;observed.correction_ordinal=9;
    REQUIRE(recorder.observe(observed));CHECK(recorder.incidents().size()==2);
    CHECK(recorder.incidents().front().complete);
    // An unknown correction identity is not fabricated from repeated error values.
    observed=frame(6,5'300'000);observed.raw_correction_units=40;REQUIRE(recorder.observe(observed));
    CHECK(recorder.incidents().size()==2);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("multiple_observed_symptoms")!=output->npos);
    CHECK(output->find("A_frame_present")!=output->npos);CHECK(output->find("B_local_prediction_reconciliation")!=output->npos);
    CHECK(output->find("\"classification_is_root_cause\":false")!=output->npos);
}
TEST_CASE("E32 generation boundary prevents stale incident context and duplicate frame replay", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};const auto before=frame(1,1'000'000);REQUIRE(recorder.observe(before));
    REQUIRE(recorder.mark(Trigger::manual,before.monotonic_us));
    CHECK_FALSE(recorder.observe(before));CHECK(recorder.statistics().duplicate_frames==1);
    auto older=frame(0,900'000);CHECK_FALSE(recorder.observe(older));
    auto after=frame(2,1'100'000);after.map_generation=6;REQUIRE(recorder.observe(after));
    REQUIRE(recorder.incidents().size()==1);CHECK(recorder.incidents().front().generation_boundary);
    CHECK_FALSE(recorder.incidents().front().complete);CHECK(recorder.incidents().front().frames.size()==1);
    CHECK(recorder.statistics().retained_frames==1);CHECK(recorder.recent_frame(0)->map_generation==6);
    CHECK_FALSE(recorder.mark(Trigger::manual,1'000'000));
    REQUIRE(recorder.mark(Trigger::manual,after.monotonic_us));REQUIRE(recorder.incidents().size()==2);
    CHECK(recorder.incidents().back().map_generation==6);
    auto invalid=identity;invalid.run_id="arbitrary identity";CHECK_FALSE(recorder.encode(invalid));
    invalid=identity;invalid.process_id=0;CHECK_FALSE(recorder.encode(invalid));
    const auto output=recorder.encode(identity);REQUIRE(output);CHECK(output->find("generation_boundary")!=output->npos);
}
TEST_CASE("E32 finite typed observations preserve unknowns and distinct support identities", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};auto value=frame(1,1'000'000);
    value.cpu_update_ms=std::numeric_limits<double>::quiet_NaN();value.gpu_ms=-1;
    value.socket_read_us=1'000'001;value.state_age_ms=-2;value.trace_fraction=1.01;
    value.raw_server_origin=app::MovementDiagnosticVector3{1,2,3};value.predicted_origin=app::MovementDiagnosticVector3{4,5,6};
    value.velocity=app::MovementDiagnosticVector3{0,0,std::numeric_limits<double>::infinity()};
    value.basevelocity=app::MovementDiagnosticVector3{0,0,0}; // Explicit observed zero, not absent.
    value.support_entity=71;value.support_model_index=22;value.support_submodel=9;value.support_physent=4;value.hull=2;
    value.set_fallback_reason("C:/private/auth-data");value.set_correction_anchor_provenance("acknowledged_move_packet_boundary");
    value.remote_previous_s=4;value.remote_current_s=4.1;value.remote_sample_s=4.05;value.remote_alpha=.5;
    value.set_remote_sample_mode("bracketed");
    REQUIRE(recorder.observe(value));const auto* actual=recorder.recent_frame(0);REQUIRE(actual);
    CHECK_FALSE(actual->cpu_update_ms);CHECK_FALSE(actual->gpu_ms);CHECK_FALSE(actual->socket_read_us);
    CHECK_FALSE(actual->state_age_ms);CHECK_FALSE(actual->trace_fraction);CHECK_FALSE(actual->velocity);
    REQUIRE(actual->basevelocity);CHECK(*actual->basevelocity==app::MovementDiagnosticVector3{0,0,0});
    CHECK(actual->support_entity==71);CHECK(actual->support_model_index==22);CHECK(actual->support_submodel==9);CHECK(actual->support_physent==4);
    CHECK(recorder.statistics().invalid_values==6);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("auth-data")==output->npos);CHECK(output->find("C:/private")==output->npos);
    CHECK(output->find("rejected_diagnostic")!=output->npos);
    CHECK(output->find("acknowledged_move_packet_boundary")!=output->npos);
    CHECK(output->find("\"basevelocity\":[0,0,0]")!=output->npos);
    CHECK(output->find("\"gpu\":null")!=output->npos);
    CHECK(actual->remote_previous_s==4);CHECK(actual->remote_current_s==4.1);
    CHECK(actual->remote_sample_s==4.05);CHECK(actual->remote_alpha==.5);
    CHECK(output->find("\"remote_sample_mode\":\"bracketed\"")!=output->npos);
}
TEST_CASE("E32 delayed GPU observation updates matching retained identities without current frame substitution", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};REQUIRE(recorder.observe(frame(10,1'000'000)));
    REQUIRE(recorder.mark(Trigger::manual,1'000'000));REQUIRE(recorder.observe(frame(11,1'010'000)));
    REQUIRE(recorder.observe_gpu(10,7.25));CHECK(recorder.recent_frame(0)->gpu_ms==7.25);
    CHECK_FALSE(recorder.recent_frame(1)->gpu_ms);
    REQUIRE(recorder.incidents().front().frames.size()==2);CHECK(recorder.incidents().front().frames[0].gpu_ms==7.25);
    CHECK_FALSE(recorder.incidents().front().frames[1].gpu_ms);
    REQUIRE(recorder.observe_gpu(10,99));CHECK(recorder.recent_frame(0)->gpu_ms==7.25);
    CHECK(recorder.statistics().gpu_observations==1);
    CHECK_FALSE(recorder.observe_gpu(999,3));CHECK(recorder.statistics().gpu_unmatched==1);
    CHECK_FALSE(recorder.observe_gpu(11,std::numeric_limits<double>::quiet_NaN()));CHECK(recorder.statistics().gpu_invalid==1);
    const auto summary=recorder.window_summary(Metric::gpu);CHECK(summary.observed==1);CHECK(summary.not_available==1);CHECK(summary.maximum==7.25);
    REQUIRE(recorder.observe(frame(12,6'000'000)));CHECK(recorder.statistics().retained_frames==1);
    REQUIRE(recorder.observe_gpu(11,6.0));CHECK_FALSE(recorder.recent_frame(0)->gpu_ms);
    CHECK(recorder.incidents().front().frames[1].gpu_ms==6.0);
    CHECK(recorder.window_summary(Metric::gpu).observed==0);
}
TEST_CASE("E32 manual context labels network and collision evidence without asserting a cause", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};auto value=frame(1,1'000'000);value.state_age_ms=350;
    value.startsolid=true;value.blocking_entity=73;value.trace_fraction=.5;
    value.plane_normal=app::MovementDiagnosticVector3{0,0,1};REQUIRE(recorder.observe(value));
    REQUIRE(recorder.mark(Trigger::manual,value.monotonic_us));
    for(std::uint64_t i=1;i<=10;++i)REQUIRE(recorder.mark(Trigger::manual,value.monotonic_us+i*1000));
    REQUIRE(recorder.incidents().size()==1);CHECK(recorder.incidents()[0].manual_marker_count==8);
    CHECK(recorder.incidents()[0].omitted_manual_markers==3);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("manual_marker_only")!=output->npos);
    CHECK(output->find("C_network_or_remote_state_age")!=output->npos);CHECK(output->find("D_collision_support")!=output->npos);
    CHECK(output->find("\"classification_is_root_cause\":false")!=output->npos);
    CHECK(output->find("session_close_before_post_window")!=output->npos);
}
TEST_CASE("E32 automatic incident storms preserve two slots for late manual lift markers", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};std::uint64_t number{},time{};
    for(std::uint64_t i=0;i<12;++i) {
        time=1'000'000+i*3'000'000;auto value=frame(++number,time);value.frame_interval_ms=55;
        REQUIRE(recorder.observe(value));
        if(i==5) {REQUIRE(recorder.mark(Trigger::correction,time+1));
            CHECK(recorder.incidents().size()==Recorder::maximum_automatic_incidents);}
    }
    CHECK(recorder.incidents().size()==Recorder::maximum_automatic_incidents);
    CHECK(recorder.statistics().manual_reservation_suppressions==6);
    REQUIRE(recorder.mark(Trigger::manual,time));CHECK(recorder.incidents().size()==7);
    CHECK(recorder.incidents().back().manual_marker_us[0]==time);
    time+=3'000'000;REQUIRE(recorder.observe(frame(++number,time)));
    REQUIRE(recorder.mark(Trigger::manual,time));CHECK(recorder.incidents().size()==Recorder::maximum_incidents);
    CHECK(recorder.incidents().back().manual_marker_us[0]==time);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"manual_reserved_incidents\":2")!=output->npos);
    CHECK(output->find("\"automatic_incident_limit\":6")!=output->npos);
    CHECK(output->find("\"manual_reservation_suppressions\":6")!=output->npos);
}
TEST_CASE("E32 recent window quantiles are measured finite values and retention is time bounded", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};
    for(std::uint64_t i=1;i<=100;++i){auto value=frame(i,i*10'000);value.cpu_update_ms=static_cast<double>(101-i);REQUIRE(recorder.observe(value));}
    const auto summary=recorder.window_summary(Metric::cpu_update);
    CHECK(summary.observed==100);CHECK(summary.not_available==0);CHECK(summary.p50==50);CHECK(summary.p95==95);CHECK(summary.p99==99);CHECK(summary.maximum==100);
    const auto gpu=recorder.window_summary(Metric::gpu);CHECK(gpu.observed==0);CHECK(gpu.not_available==100);CHECK_FALSE(gpu.p50);
    REQUIRE(recorder.observe(frame(101,5'500'000)));CHECK(recorder.statistics().retained_frames==1);CHECK(recorder.statistics().retention_evictions==100);
    CHECK_FALSE(recorder.recent_frame(1));CHECK(recorder.recent_frame(0)->frame_number==101);
}
TEST_CASE("E32 frame completion clock retains ordered read commit use and explicit blocker preconditions", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};auto value=frame(1,1'020'000);
    value.network_poll_begin_us=1'000'000;value.socket_read_begin_us=1'000'010;value.socket_read_us=1'000'020;
    value.parse_begin_us=1'000'030;value.commit_us=1'000'040;value.prediction_use_us=1'000'050;
    value.presentation_use_us=1'000'060;value.network_poll_end_us=1'000'070;
    value.support_observation_s=4.25;value.support_observation_us=999'000;
    value.previous_valid_origin=app::MovementDiagnosticVector3{2,3,4};value.seed_origin=app::MovementDiagnosticVector3{2,3,5};
    value.collision_query_start=app::MovementDiagnosticVector3{2,3,5};value.collision_query_end=app::MovementDiagnosticVector3{2,3,6};
    value.blocking_entity=88;value.support_entity=88;value.startsolid=true;value.set_blocker_kind("brush");
    value.datagrams_received=500;value.rejected_sequences=3;value.choke_markers_total=2;
    value.observed_players=16;
    REQUIRE(recorder.observe(value));const auto* actual=recorder.recent_frame(0);REQUIRE(actual);
    CHECK(recorder.statistics().invalid_values==0);CHECK(actual->socket_read_us==1'000'020);CHECK(actual->commit_us==1'000'040);
    CHECK(actual->prediction_use_us==1'000'050);CHECK(actual->presentation_use_us==1'000'060);CHECK(actual->support_observation_s==4.25);
    CHECK(actual->datagrams_received==500);CHECK(actual->rejected_sequences==3);CHECK(actual->choke_markers_total==2);
    CHECK(actual->observed_players==16);
    REQUIRE(actual->previous_valid_origin);CHECK(*actual->previous_valid_origin==app::MovementDiagnosticVector3{2,3,4});
    const auto output=recorder.encode(identity);REQUIRE(output);CHECK(output->find("\"blocker_kind\":\"brush\"")!=output->npos);
    CHECK(output->find("\"collision_query_start\":[2,3,5]")!=output->npos);
    CHECK(output->find("\"collision_query_end\":[2,3,6]")!=output->npos);
    CHECK(output->find("\"datagrams_received\":500")!=output->npos);
    CHECK(output->find("\"observed_players\":16")!=output->npos);
    CHECK(output->find("\"rejected_sequence_is_packet_loss\":false")!=output->npos);
    CHECK(output->find("\"choke_marker_is_server_load_proof\":false")!=output->npos);
}
TEST_CASE("E32 high-rate rings incident count samples and encoded output obey hard limits", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};std::uint64_t sequence{},time{};
    for(unsigned incident=0;incident<Recorder::maximum_incidents;++incident) {
        REQUIRE(recorder.observe(frame(++sequence,++time)));
        REQUIRE(recorder.mark(Trigger::manual,time));
        for(unsigned i=0;i<4000;++i){time+=100;REQUIRE(recorder.observe(frame(++sequence,time)));}
    }
    CHECK_FALSE(recorder.mark(Trigger::manual,time));CHECK(recorder.statistics().incident_limit_drops==1);
    for(unsigned i=0;i<2000;++i){time+=1000;REQUIRE(recorder.observe(frame(++sequence,time)));}
    const auto stats=recorder.statistics();CHECK(stats.incidents==Recorder::maximum_incidents);
    CHECK(stats.retained_frames<=Recorder::maximum_recent_frames);CHECK(stats.ring_capacity_drops>0);
    CHECK(stats.incident_sample_drops>0);CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    for(const auto& incident:recorder.incidents()){CHECK(incident.complete);CHECK(incident.frames.size()<=Recorder::maximum_incident_frames);}
    const auto output=recorder.encode(identity);REQUIRE(output);CHECK(output->size()<=Recorder::maximum_encoded_bytes);
    std::cout<<"[e32-diagnostics-bound] frame_record_bytes="<<sizeof(app::MovementFrameRecord)
        <<" retained_capacity_bytes="<<stats.allocated_capacity_bytes<<" encoded_bytes="<<output->size()<<'\n';
}
TEST_CASE("E32 diagnostic collection On Off cost is recorded separately from game performance", "[e32][app][movement-diagnostics][benchmark]") {
    using Clock=std::chrono::steady_clock;
    constexpr std::size_t samples=4000,per_run=1000;std::array<std::array<double,samples>,2> measured{};
    // Recreate and warm both collectors identically. Alternate sample order
    // across runs so monotonic machine/load drift is not attributed to On.
    for(std::size_t run=0;run<samples/per_run;++run) {
        std::array<Recorder,2> recorders{Recorder{false},Recorder{true}};std::uint64_t number{};
        for(std::size_t i=0;i<1000;++i){++number;const auto value=frame(number,number*1000);
            for(auto& recorder:recorders)static_cast<void>(recorder.observe(value));}
        for(std::size_t i=0;i<per_run;++i) {
            ++number;const auto value=frame(number,number*1000);
            for(unsigned ordinal=0;ordinal<2;++ordinal) {
                const auto mode=static_cast<unsigned>((run+i+ordinal)%2);const auto begin=Clock::now();
                static_cast<void>(recorders[mode].observe(value));const auto end=Clock::now();
                measured[mode][run*per_run+i]=std::chrono::duration<double,std::micro>(end-begin).count();
            }
        }
        for(unsigned mode=0;mode<2;++mode){CHECK(recorders[mode].statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
            CHECK(recorders[mode].statistics().accepted_frames==(mode==1?2000U:0U));}
    }
    for(unsigned mode=0;mode<2;++mode) {
        std::sort(measured[mode].begin(),measured[mode].end());
        std::cout<<"[e32-diagnostics-cost] scope=collection_microbenchmark mode="<<(mode?"on":"off")
            <<" samples="<<samples<<" p50_us="<<measured[mode][1999]<<" p95_us="<<measured[mode][3799]
            <<" p99_us="<<measured[mode][3959]<<" max_us="<<measured[mode].back()<<'\n';
        for(const auto duration:measured[mode])CHECK(std::isfinite(duration));
    }
}
