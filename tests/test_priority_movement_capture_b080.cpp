#include <hlclient/app/movement_diagnostics.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

namespace {
namespace app=hlclient::app;
using Recorder=app::MovementIncidentRecorder;
using Trigger=app::MovementIncidentTrigger;
app::MovementFrameRecord capture_frame(std::uint64_t number,std::uint64_t stamp) {
    app::MovementFrameRecord result;
    result.frame_number=number;result.monotonic_us=stamp;
    result.network_generation=3;result.map_generation=5;
    result.prediction_active=true;result.frame_interval_ms=10.;return result;
}
app::MovementFrameContext playing(std::uint64_t life=1) {return {true,true,true,true,life};}
bool has_trigger(const Recorder& recorder,Trigger trigger) {
    return std::ranges::any_of(recorder.incidents(),[&](const auto& incident) {
        return (incident.triggers&app::movement_incident_mask(trigger))!=0;
    });
}
const app::SessionReceiptIdentity capture_identity{
    "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,123,456};
app::MovementCorrectionWitness correction_witness(std::uint64_t ordinal,std::uint64_t stamp,double error) {
    app::MovementCorrectionWitness result;
    result.event_ordinal=ordinal;result.accepted_ordinal=ordinal;result.network_generation=3;result.map_generation=5;
    result.life_epoch=1;result.source_record_identity=1000+ordinal;result.source_record_ordinal=ordinal;
    result.source_sequence=static_cast<std::uint32_t>(ordinal+100);result.observed_us=stamp;result.observed_ns=stamp*1000;
    result.owner_time_ns=static_cast<std::int64_t>(stamp*1000);result.alive=true;result.control_active=true;
    result.prediction_owner_state=2;result.accepted=true;result.classification=2;
    result.raw_position_error=error;result.raw_error_basis=1;result.authoritative_origin={error,0.,0.};
    result.authoritative_origin_freshness=1;result.authoritative_velocity_freshness=1;
    result.matched_origin=app::MovementDiagnosticFloat3{};result.matched_velocity=app::MovementDiagnosticFloat3{};
    result.matched_freshness=2;result.boundary_command=42;result.history_anchor_command=42;result.history_latest_command=44;
    result.history_size=3;result.suffix_first_command=43;result.suffix_last_command=44;result.suffix_commands=2;
    result.suffix_milliseconds=40;result.replayed_commands=2;
    hlclient::core::set_movement_witness_token(result.reason,"ordinary_rebase");return result;
}
}

TEST_CASE("B080 respawn does not relabel a sticky correction as a new event", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};auto frame=capture_frame(1,1'000'000);
    frame.correction_ordinal=7;frame.raw_correction_units=32.;
    REQUIRE(recorder.observe(frame,playing(1)));REQUIRE(recorder.incidents().size()==1);
    frame.frame_number=2;frame.monotonic_us=4'000'000;frame.prediction_active=false;
    frame.set_fallback_reason("local_player_dead");auto dead=playing(1);dead.alive=false;
    REQUIRE(recorder.observe(frame,dead));
    frame.frame_number=3;frame.monotonic_us=7'000'000;frame.prediction_active=true;
    frame.set_fallback_reason("");REQUIRE(recorder.observe(frame,playing(2)));
    CHECK(recorder.incidents().size()==1);
    frame.frame_number=4;frame.monotonic_us=10'000'000;frame.correction_ordinal=8;
    REQUIRE(recorder.observe(frame,playing(2)));CHECK(recorder.incidents().size()==2);
    frame.frame_number=5;frame.monotonic_us=13'000'000;frame.network_generation=4;frame.correction_ordinal=7;
    REQUIRE(recorder.observe(frame,playing(2)));CHECK(recorder.incidents().size()==3);
}

TEST_CASE("B080 lifecycle noise cannot exhaust later alive movement capture", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};std::uint64_t number{};
    auto value=capture_frame(++number,1'000'000);value.prediction_active=false;
    value.set_fallback_reason("waiting_for_server_seed");value.frame_interval_ms=55.;
    auto context=playing();context.alive=false;context.control_active=false;
    REQUIRE(recorder.observe(value,context));
    value=capture_frame(++number,4'000'000);value.prediction_active=false;
    value.set_fallback_reason("collision_attachment_pending");value.frame_interval_ms=55.;
    REQUIRE(recorder.observe(value,context));
    for(std::uint64_t death=0;death<4;++death) {
        const auto stamp=7'000'000+death*4'000'000;
        REQUIRE(recorder.observe(capture_frame(++number,stamp),playing(death+1)));
        value=capture_frame(++number,stamp+10'000);value.prediction_active=false;
        value.set_fallback_reason("local_player_dead");context=playing(death+1);context.alive=false;
        REQUIRE(recorder.observe(value,context));
    }
    value=capture_frame(++number,24'000'000);value.correction_ordinal=1;value.raw_correction_units=31.562;
    REQUIRE(recorder.observe(value,playing(5)));
    CHECK(has_trigger(recorder,Trigger::correction));
    CHECK(recorder.incidents().size()<Recorder::maximum_incidents);
}

TEST_CASE("B080 late manual marker includes available correction time without a faster reaction", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};std::uint64_t number{};
    for(std::uint64_t i=0;i<=700;++i) {
        auto value=capture_frame(++number,1'000'000+i*10'000);
        if(i==250){value.correction_ordinal=1;value.raw_correction_units=31.562;}
        else if(i>250)value.correction_ordinal=1;
        REQUIRE(recorder.observe(value,playing()));
        if(i==500)REQUIRE(recorder.mark(Trigger::manual,value.monotonic_us));
    }
    const auto manual=std::ranges::find_if(recorder.incidents(),[](const auto& incident) {
        return (incident.triggers&app::movement_incident_mask(Trigger::manual))!=0;
    });
    REQUIRE(manual!=recorder.incidents().end());REQUIRE_FALSE(manual->frames.empty());
    CHECK(manual->frames.front().monotonic_us<=2'020'000);
    CHECK(manual->frames.back().monotonic_us>=7'000'000);
    CHECK(std::ranges::any_of(manual->frames,[](const auto& frame){return frame.monotonic_us==6'000'000;}));
}

TEST_CASE("B080 important late movement replaces completed performance detail and preserves accepted manual windows", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};std::uint64_t number{},stamp{};
    for(std::size_t i=0;i<Recorder::maximum_automatic_incidents;++i) {
        stamp=1'000'000+static_cast<std::uint64_t>(i)*3'000'000;
        auto value=capture_frame(++number,stamp);value.frame_interval_ms=55.;
        REQUIRE(recorder.observe(value,playing()));
    }
    stamp+=3'000'000;REQUIRE(recorder.observe(capture_frame(++number,stamp),playing()));
    REQUIRE(recorder.mark(Trigger::manual,stamp));const auto first_marker=stamp;
    stamp+=3'000'000;REQUIRE(recorder.observe(capture_frame(++number,stamp),playing()));
    REQUIRE(recorder.mark(Trigger::manual,stamp));const auto second_marker=stamp;
    stamp+=3'000'000;auto important=capture_frame(++number,stamp);
    important.correction_ordinal=77;important.raw_correction_units=32.;
    REQUIRE(recorder.observe(important,playing()));
    CHECK(has_trigger(recorder,Trigger::correction));
    CHECK(recorder.incidents().size()<=Recorder::maximum_incidents);
    for(const auto marker:{first_marker,second_marker})CHECK(std::ranges::any_of(recorder.incidents(),[&](const auto& incident) {
        return std::find(incident.manual_marker_us.begin(),incident.manual_marker_us.begin()+incident.manual_marker_count,marker)!=
            incident.manual_marker_us.begin()+incident.manual_marker_count;
    }));
}

TEST_CASE("B080 real fallback errors survive loading and death boundary context", "[b080][capture-policy][core][app]") {
    for(const auto reason:{"movement_trace_failed","prediction_collision_allocation_failed","solid_start"}) {
        Recorder recorder{true};REQUIRE(recorder.observe(capture_frame(1,1'000'000),playing()));
        auto failure=capture_frame(2,1'010'000);failure.prediction_active=false;failure.set_fallback_reason(reason);
        auto context=playing(2);context.alive=false;context.control_active=false;
        REQUIRE(recorder.observe(failure,context));CHECK(has_trigger(recorder,Trigger::fallback));
    }
}

TEST_CASE("B080 coalescing never crosses life identity and never grows a flood window", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};auto value=capture_frame(1,1'000'000);value.frame_interval_ms=55.;
    REQUIRE(recorder.observe(value,playing(1)));REQUIRE(recorder.incidents().size()==1);
    const auto end=recorder.incidents().front().end_us;
    for(std::uint64_t i=2;i<20;++i){value=capture_frame(i,1'000'000+i*10'000);value.frame_interval_ms=55.;
        REQUIRE(recorder.observe(value,playing(1)));}
    CHECK(recorder.incidents().front().end_us==end);
    value=capture_frame(20,1'210'000);value.correction_ordinal=1;value.raw_correction_units=32.;
    REQUIRE(recorder.observe(value,playing(2)));
    REQUIRE(recorder.incidents().size()==2);
    CHECK(recorder.incidents().front().frames.back().monotonic_us<value.monotonic_us);
}

TEST_CASE("B080 manual temporal coverage stays honest at every supported sampling rate", "[b080][capture-policy][core][app]") {
    for(const std::uint64_t rate:{30U,60U,100U,144U,240U}) {
        CAPTURE(rate);Recorder recorder{true};
        for(std::uint64_t i=0;i<=rate*7;++i) {
            const auto stamp=1'000'000+(i*1'000'000)/rate;
            REQUIRE(recorder.observe(capture_frame(i+1,stamp),playing()));
            if(i==rate*5)REQUIRE(recorder.mark(Trigger::manual,stamp));
        }
        REQUIRE(recorder.incidents().size()==1);const auto& incident=recorder.incidents().front();
        REQUIRE_FALSE(incident.frames.empty());CHECK(incident.complete);
        CHECK(incident.frames.front().monotonic_us<=2'000'000+1'000'000/rate);
        CHECK(incident.frames.back().monotonic_us>=7'000'000-1'000'000/rate);
        CHECK(std::ranges::any_of(incident.frames,[](const auto& frame){return frame.monotonic_us==6'000'000;}));
        CHECK(incident.frames.size()<=Recorder::maximum_incident_frames);
        CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
        const auto text=recorder.encode(capture_identity);REQUIRE(text);CHECK(text->size()<=Recorder::maximum_encoded_bytes);
    }
}

TEST_CASE("B080 manual window retains the real frame before an irregular endpoint crossing", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};
    for(std::uint64_t i=0;i<600;++i) {
        const auto stamp=1'000'000+i*10'000;
        REQUIRE(recorder.observe(capture_frame(i+1,stamp),playing()));
        if(i==500)REQUIRE(recorder.mark(Trigger::manual,stamp));
    }
    REQUIRE(recorder.observe(capture_frame(601,7'005'000),playing()));
    REQUIRE(recorder.incidents().size()==1);
    const auto& incident=recorder.incidents().front();
    REQUIRE(incident.complete);
    REQUIRE_FALSE(incident.frames.empty());
    CHECK(incident.frames.back().monotonic_us==6'990'000);
    CHECK(incident.exact_samples[incident.frames.size()-1]==0U);
    CHECK(std::ranges::any_of(incident.frames,[](const auto& frame){return frame.monotonic_us==6'000'000;}));
    CHECK(incident.frames.size()<=Recorder::incident_frame_capacity);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
}

TEST_CASE("B080 manual availability is not reduced to two total markers", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};
    for(std::size_t i=0;i<Recorder::maximum_incidents;++i) {
        const auto stamp=1'000'000+static_cast<std::uint64_t>(i)*6'000'000;
        REQUIRE(recorder.observe(capture_frame(i+1,stamp),playing()));REQUIRE(recorder.mark(Trigger::manual,stamp));
    }
    CHECK(recorder.incidents().size()==Recorder::maximum_incidents);
    CHECK_FALSE(recorder.mark(Trigger::manual,50'000'000));
}

TEST_CASE("B080 invalid timestamps and partial shutdown do not fabricate completed coverage", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};const auto value=capture_frame(1,1'000'000);
    REQUIRE(recorder.observe(value,playing()));REQUIRE(recorder.mark(Trigger::manual,value.monotonic_us));
    CHECK_FALSE(recorder.observe(capture_frame(2,999'999),playing()));
    CHECK_FALSE(recorder.mark(Trigger::manual,999'999));
    REQUIRE(recorder.incidents().size()==1);CHECK_FALSE(recorder.incidents().front().complete);
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("session_close_before_post_window")!=text->npos);
}

TEST_CASE("B080 owning failure state takes precedence over an expected lifecycle reason", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};auto value=capture_frame(1,1'000'000);value.prediction_active=false;
    value.set_fallback_reason("local_player_dead");auto context=playing();context.alive=false;
    context.prediction_owner_state=app::MovementCaptureOwnerState::failed;
    REQUIRE(recorder.observe(value,context));REQUIRE(recorder.incidents().size()==1);
    CHECK(recorder.incidents().front().priority==50);
    REQUIRE_FALSE(recorder.capture_decisions().empty());CHECK(recorder.capture_decisions().front().decision==app::MovementCaptureDecisionKind::accepted);
    Recorder loading{true};value.prediction_active.reset();context.control_active=false;
    value.set_fallback_reason("prediction_collision_allocation_failed");REQUIRE(loading.observe(value,context));
    CHECK(has_trigger(loading,Trigger::fallback));
    Recorder active{true};value.prediction_active=true;value.frame_interval_ms=55.;value.set_fallback_reason("local_player_dead");
    context=playing();context.prediction_owner_state=app::MovementCaptureOwnerState::active;
    REQUIRE(active.observe(value,context));CHECK(has_trigger(active,Trigger::frame_stall));
}

TEST_CASE("B080 correction producer witnesses retain multiple events in one update independently of the last snapshot", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};REQUIRE(recorder.observe(capture_frame(1,1'000'000),playing()));recorder.set_update_context(playing());
    auto first=correction_witness(1,1'010'000,31.562);auto last=correction_witness(2,1'011'000,.25);
    REQUIRE(recorder.observe_correction(first));REQUIRE(recorder.observe_correction(last));
    CHECK_FALSE(recorder.observe_correction(last));
    auto frame=capture_frame(2,1'020'000);frame.correction_ordinal=2;frame.raw_correction_units=.25;
    REQUIRE(recorder.observe(frame,playing()));CHECK(has_trigger(recorder,Trigger::correction));
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"accepted_events\":2")!=text->npos);
    CHECK(text->find("\"maximum_raw_position_error\":31.562")!=text->npos);
    CHECK(text->find("\"maximum_witness_identity\":{\"event_ordinal\":1")!=text->npos);
    CHECK(text->find("\"source_record_identity\":1001")!=text->npos);
    CHECK(text->find("\"event_ordinal\":2")!=text->npos);
    CHECK(text->find("\"same_time_camera_delta\":null")!=text->npos);
    CHECK(recorder.statistics().correction_witness_duplicates==1);
    CHECK(recorder.statistics().retained_correction_witnesses==2);
}

TEST_CASE("B080 correction maximum survives a long detail flood while missing time stays unavailable", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};recorder.set_update_context(playing());
    for(std::uint64_t i=1;i<=80;++i){auto witness=correction_witness(i,1'000'000+i*10'000,i==1?128.:.125);
        if(i==80)witness.observed_us.reset();REQUIRE(recorder.observe_correction(witness));}
    const auto stats=recorder.statistics();CHECK(stats.correction_witnesses_accepted==80);
    CHECK(stats.retained_correction_witnesses<=Recorder::maximum_correction_witnesses);
    CHECK(stats.correction_witness_evictions==64);CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"maximum_raw_position_error\":128")!=text->npos);
    CHECK(text->find("\"maximum_witness_status\":\"available\"")!=text->npos);
    CHECK(text->find("\"source_record_identity\":1001")!=text->npos);
    CHECK(text->find("\"observed_us\":null")!=text->npos);
    CHECK(text->size()<=Recorder::maximum_encoded_bytes);
}

TEST_CASE("B080 correction evidence rejects invalid numbers and never reuses a sticky camera measurement", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};auto witness=correction_witness(1,1'000'000,10.);
    witness.raw_position_error=std::numeric_limits<double>::quiet_NaN();CHECK_FALSE(recorder.observe_correction(witness));
    witness=correction_witness(2,1'010'000,11.);witness.camera_status=2;witness.same_time_camera_delta=123.;
    REQUIRE(recorder.observe_correction(witness));
    witness=correction_witness(3,1'020'000,12.);witness.camera_status=1;witness.same_time_camera_delta=2.;
    witness.before_eye=app::MovementDiagnosticFloat3{1,2,3};witness.after_eye=app::MovementDiagnosticFloat3{3,2,3};
    witness.presentation_requested_ns=1'000;witness.presentation_before_ns=1'000;witness.presentation_after_ns=1'001;
    witness.presentation_before_kind=1;witness.presentation_after_kind=2;REQUIRE(recorder.observe_correction(witness));
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"same_time_camera_delta\":123")==text->npos);
    CHECK(text->find("\"same_time_camera_delta\":2")==text->npos);
    CHECK(text->find("\"camera_measurement\":\"different_time_samples\"")!=text->npos);
    CHECK(recorder.statistics().correction_witness_invalid==1);
    Recorder disabled;CHECK_FALSE(disabled.observe_correction(witness));CHECK(disabled.statistics().allocated_capacity_bytes==0);
}

TEST_CASE("B080 suppression and replacement retain bounded owning event metadata", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};
    for(std::uint64_t i=1;i<=200;++i) {
        auto value=capture_frame(i,i*3'000'000);value.frame_interval_ms=55.;value.source_sequence=1000+i;
        value.authoritative_source_record_identity=2000+i;
        REQUIRE(recorder.observe(value,playing()));
    }
    CHECK(recorder.capture_decisions().size()==Recorder::maximum_capture_decisions);
    CHECK(recorder.statistics().decision_evictions==200-Recorder::maximum_capture_decisions);
    CHECK(recorder.statistics().priority_replacements==200-Recorder::maximum_automatic_incidents);
    for(const auto& decision:recorder.capture_decisions()) {
        CHECK(decision.source_sequence.has_value());CHECK(decision.source_record_identity.has_value());
        CHECK(decision.trigger==app::movement_incident_mask(Trigger::frame_stall));
    }
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"decision\":\"replaced\"")!=text->npos);
    CHECK(text->find("\"replacement_ordinal\":0")==text->npos);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
}

TEST_CASE("B080 frame gap generation and manual markers have explicit partial coverage", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};REQUIRE(recorder.observe(capture_frame(1,1'000'000),playing()));
    REQUIRE(recorder.observe(capture_frame(2,1'010'000),playing()));REQUIRE(recorder.mark(Trigger::manual,1'010'000));
    auto next=capture_frame(3,2'000'000);next.map_generation=6;REQUIRE(recorder.observe(next,playing()));
    REQUIRE(recorder.mark(Trigger::manual,2'000'000));
    REQUIRE(recorder.incidents().size()==2);CHECK(recorder.incidents().front().generation_boundary);
    CHECK(recorder.incidents().back().frames.front().monotonic_us==2'000'000);
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"coverage_reason\":\"generation_boundary\"")!=text->npos);
    CHECK(text->find("\"coverage_reason\":\"start_or_generation_boundary_or_recent_capacity\"")!=text->npos);
    CHECK(text->find("\"exact_frames\":[")!=text->npos);
}

TEST_CASE("B080 full bounded storage includes metadata witnesses and every retained buffer", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};std::uint64_t number{},stamp{};
    for(std::size_t incident=0;incident<Recorder::maximum_incidents;++incident) {
        for(std::uint64_t i=0;i<=1200;++i) {
            stamp=1'000'000+static_cast<std::uint64_t>(incident)*6'000'000+(i*1'000'000)/240;
            auto value=capture_frame(++number,stamp);value.correction_ordinal=number;value.raw_correction_units=.125;
            value.gpu_passes={{1.F,2.F,3.F,4.F,5.F,6.F},0x3fU,app::MovementGpuPassStatus::ready};
            REQUIRE(recorder.observe(value,playing()));
            if(i==960)REQUIRE(recorder.mark(Trigger::manual,stamp));
        }
    }
    for(std::uint64_t i=1;i<=40;++i)REQUIRE(recorder.observe_correction(correction_witness(i,stamp+i,.125)));
    const auto stats=recorder.statistics();CHECK(stats.incidents==8);CHECK(stats.retained_frames<=961);
    CHECK(stats.background_samples_omitted>0);CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    const auto text=recorder.encode(capture_identity);REQUIRE(text);CHECK(text->size()<=Recorder::maximum_encoded_bytes);
    std::cout<<"[b080-capture-bound] frame_record_bytes="<<sizeof(app::MovementFrameRecord)
        <<" incident_header_bytes="<<sizeof(app::MovementIncident)<<" witness_bytes="<<sizeof(app::MovementCorrectionWitness)
        <<" decision_bytes="<<sizeof(app::MovementCaptureDecision)<<" retained_capacity_bytes="<<stats.allocated_capacity_bytes
        <<" encoded_bytes="<<text->size()<<'\n';
}

TEST_CASE("B080 rejected alive correction owns its capture before a later death snapshot", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};REQUIRE(recorder.observe(capture_frame(1,1'000'000),playing()));recorder.set_update_context(playing());
    auto witness=correction_witness(1,1'010'000,.25);witness.accepted=false;witness.classification=0;
    witness.prediction_owner_state=3;hlclient::core::set_movement_witness_token(witness.reason,"reference_rebase_or_suffix_failed");
    REQUIRE(recorder.observe_correction(witness));
    auto death=capture_frame(2,1'020'000);death.prediction_active=false;death.set_fallback_reason("local_player_dead");
    auto context=playing();context.alive=false;context.prediction_owner_state=app::MovementCaptureOwnerState::suspended;
    REQUIRE(recorder.observe(death,context));CHECK(has_trigger(recorder,Trigger::fallback));
    REQUIRE_FALSE(recorder.capture_decisions().empty());
    CHECK(std::ranges::any_of(recorder.capture_decisions(),[](const auto& decision) {
        return decision.source_record_identity==1001 && decision.decision==app::MovementCaptureDecisionKind::accepted;
    }));
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("reference_rebase_or_suffix_failed")!=text->npos);
    Recorder expected{true};witness=correction_witness(1,1'010'000,.25);witness.accepted=false;witness.classification=0;
    witness.prediction_owner_state=1;hlclient::core::set_movement_witness_token(witness.reason,"waiting_for_server_seed");
    REQUIRE(expected.observe_correction(witness));CHECK(expected.incidents().empty());
}

TEST_CASE("B080 manual marker flood cannot inflate the protected encoding prefix", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};REQUIRE(recorder.observe(capture_frame(1,1'000'000),playing()));
    REQUIRE(recorder.mark(Trigger::manual,1'000'000));
    for(std::uint64_t i=1;i<=1000;++i) {
        const auto stamp=1'000'000+i*100;REQUIRE(recorder.observe(capture_frame(i+1,stamp),playing()));
        REQUIRE(recorder.mark(Trigger::manual,stamp));
    }
    REQUIRE(recorder.incidents().size()==1);CHECK(recorder.incidents().front().manual_marker_count==8);
    CHECK(recorder.incidents().front().omitted_manual_markers==993);
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    const auto begin=text->find("\"exact_frames\":[");REQUIRE(begin!=text->npos);
    const auto end=text->find("\"session_summary\":",begin);REQUIRE(end!=text->npos);
    std::size_t count{},position=begin;
    while((position=text->find("\"incident_ordinal\":",position))!=text->npos && position<end){++count;++position;}
    CHECK(count==8);CHECK(text->size()<=Recorder::maximum_encoded_bytes);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
}

TEST_CASE("B080 real v2 encoder publishes an analyzer roundtrip fixture", "[b080][b080-encode-fixture][capture-policy][core][app]") {
    Recorder recorder{true};
    for(std::uint64_t i=0;i<=180;++i) {
        const auto stamp=1'000'000+(i*1'000'000)/30;auto value=capture_frame(i+1,stamp);
        if(i>=75){value.correction_ordinal=1;value.raw_correction_units=31.562;}
        recorder.set_update_context(playing());REQUIRE(recorder.observe(value,playing()));
        if(i==75)REQUIRE(recorder.observe_correction(correction_witness(1,stamp,31.562)));
        if(i==150)REQUIRE(recorder.mark(Trigger::manual,stamp));
    }
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"schema\":\"hlclient.external-movement.v2\"")!=text->npos);
    CHECK(text->find("\"actual_pre_us\":4000000")!=text->npos);
    CHECK(text->find("\"maximum_witness_status\":\"available\"")!=text->npos);
    std::cout<<"[b080-v2-fixture] "<<*text;
}

TEST_CASE("B080 manual promotion preserves earlier exact automatic trigger frames", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};REQUIRE(recorder.observe(capture_frame(1,1'000'000),playing()));
    auto trigger=capture_frame(2,1'010'000);trigger.frame_interval_ms=55.;REQUIRE(recorder.observe(trigger,playing()));
    REQUIRE(recorder.observe(capture_frame(3,1'020'000),playing()));REQUIRE(recorder.mark(Trigger::manual,1'020'000));
    REQUIRE(recorder.incidents().size()==1);const auto& incident=recorder.incidents().front();
    CHECK(incident.initial_trigger_us==1'010'000);CHECK(incident.trigger_us==1'020'000);
    CHECK(std::ranges::any_of(incident.frames,[](const auto& frame){return frame.frame_number==2;}));
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    const auto begin=text->find("\"exact_frames\":[");const auto end=text->find("\"session_summary\":",begin);
    REQUIRE(begin!=text->npos);REQUIRE(end!=text->npos);
    CHECK(text->substr(begin,end-begin).find("\"frame_number\":2")!=text->npos);
    Recorder extreme{true};trigger=capture_frame(1,1'000'000);trigger.frame_interval_ms=55.;REQUIRE(extreme.observe(trigger,playing()));
    for(std::uint64_t i=1;i<=1000;++i)REQUIRE(extreme.observe(capture_frame(i+1,1'000'000+i*100),playing()));
    REQUIRE(extreme.mark(Trigger::manual,1'100'000));REQUIRE(extreme.incidents().size()==2);
    CHECK(extreme.incidents().front().frames.front().frame_number==1);
    CHECK((extreme.incidents().front().triggers&1U)==0);
    CHECK((extreme.incidents().back().triggers&1U)!=0);
}

TEST_CASE("B080 equal correction maxima keep the original witness across reconnect ordinals", "[b080][capture-policy][core][app]") {
    Recorder recorder{true};recorder.set_update_context(playing());
    auto original=correction_witness(100,1'000'000,32.);REQUIRE(recorder.observe_correction(original));
    for(std::uint64_t generation=4;generation<36;++generation) {
        auto equal=correction_witness(1,generation*1'000'000,32.);equal.network_generation=generation;
        equal.map_generation=generation+1;REQUIRE(recorder.observe_correction(equal));
    }
    const auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"maximum_witness_identity\":{\"event_ordinal\":100,\"network_generation\":3")!=text->npos);
    CHECK(text->find("\"maximum_witness_status\":\"available\"")!=text->npos);
    CHECK(text->find("\"source_record_identity\":1100")!=text->npos);
    CHECK(text->find("\"status\":\"witness_missing\"")==text->npos);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
}

TEST_CASE("B080 producer delivery failures remain explicit across cumulative polls and resets",
    "[b080][capture-policy][correction-delivery][core][app]") {
    Recorder recorder{true};const auto capacity=recorder.statistics().allocated_capacity_bytes;
    auto text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"delivery_status\":\"not_observed\"")!=text->npos);
    recorder.observe_correction_delivery(0,0,0,0); // No established source generation.
    CHECK(recorder.statistics().correction_delivery_observations==0);
    recorder.observe_correction_delivery(3,5,0,0);
    text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"delivery_status\":\"complete\"")!=text->npos);
    recorder.observe_correction_delivery(3,5,5,2);
    recorder.observe_correction_delivery(3,5,5,2); // Sticky cumulative snapshot, not two more failures.
    auto stats=recorder.statistics();
    CHECK(stats.correction_delivery_published_events==5);
    CHECK(stats.correction_delivery_sink_failures==2);
    CHECK(stats.correction_delivery_duplicate_observations==1);
    recorder.observe_correction_delivery(3,5,7,3);
    recorder.observe_correction_delivery(3,5,1,0); // Same-generation reset: known new baseline, unknown lost gap.
    recorder.observe_correction_delivery(3,5,1,0);
    stats=recorder.statistics();
    CHECK(stats.correction_delivery_published_events==8);
    CHECK(stats.correction_delivery_sink_failures==3);
    CHECK(stats.correction_delivery_counter_resets==1);
    CHECK(stats.correction_delivery_duplicate_observations==2);
    recorder.observe_correction_delivery(4,1,4,1); // New source counter lifetime.
    recorder.observe_correction_delivery(3,5,99,99); // An older generation cannot contaminate totals.
    stats=recorder.statistics();
    CHECK(stats.correction_delivery_published_events==12);
    CHECK(stats.correction_delivery_sink_failures==4);
    CHECK(stats.correction_delivery_generation_boundaries==1);
    CHECK(stats.correction_delivery_stale_generation_observations==1);
    text=recorder.encode(capture_identity);REQUIRE(text);
    CHECK(text->find("\"delivery_status\":\"partial\"")!=text->npos);
    CHECK(text->find("\"delivery_sink_failures\":4")!=text->npos);
    CHECK(text->find("\"delivery_published_events\":12")!=text->npos);
    CHECK(text->find("\"delivery_counter_resets\":1")!=text->npos);
    CHECK(recorder.statistics().allocated_capacity_bytes==capacity);
    CHECK(text->size()<=Recorder::maximum_encoded_bytes);
}

TEST_CASE("B080 producer delivery coverage does not invent completeness or allocate while disabled",
    "[b080][capture-policy][correction-delivery][core][app]") {
    Recorder disabled{false};disabled.observe_correction_delivery(3,5,12,4);
    const auto stats=disabled.statistics();
    CHECK(stats.correction_delivery_observations==0);
    CHECK(stats.correction_delivery_sink_failures==0);
    CHECK(stats.allocated_capacity_bytes==0);
    const auto text=disabled.encode(capture_identity);REQUIRE(text);
    CHECK(text->size()<4096);
    CHECK(text->find("\"critical_corrections\"")==text->npos);
    Recorder reset{true};reset.observe_correction_delivery(3,5,7,0);
    reset.observe_correction_delivery(3,5,0,0);
    const auto reset_text=reset.encode(capture_identity);REQUIRE(reset_text);
    CHECK(reset.statistics().correction_delivery_sink_failures==0);
    CHECK(reset_text->find("\"delivery_status\":\"partial\"")!=reset_text->npos);
    Recorder saturated{true};
    saturated.observe_correction_delivery(3,5,UINT64_MAX,UINT64_MAX);
    saturated.observe_correction_delivery(4,1,UINT64_MAX,UINT64_MAX);
    CHECK(saturated.statistics().correction_delivery_published_events==UINT64_MAX);
    CHECK(saturated.statistics().correction_delivery_sink_failures==UINT64_MAX);
}
