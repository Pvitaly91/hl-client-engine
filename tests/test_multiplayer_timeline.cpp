#include <hlclient/app/movement_diagnostics.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>

namespace {
namespace core=hlclient::core;
namespace app=hlclient::app;
using Timeline=app::MultiplayerTimeline;
using Event=core::MultiplayerDiagnosticEvent;
Event command(std::uint64_t ordinal,std::uint64_t time) {
    Event e;e.identity.producer=core::MultiplayerDiagnosticProducer::command_scheduler;
    e.identity.producer_event_id=ordinal;e.identity.network_generation=1;e.identity.map_generation=1;
    e.identity.command_id=ordinal+100;e.identity.input_sequence=ordinal+200;
    e.local_steady_ns=time;e.provenance=core::MultiplayerProvenance::runtime_observed;
    e.edge=core::MultiplayerEdgeConfidence::exact;
    core::MultiplayerCommandPayload payload;payload.phase=core::MultiplayerCommandPhase::built;
    payload.msec=10;payload.lerp_msec=100;payload.buttons=1;payload.angle_turns={100,200,300};
    e.payload=payload;return e;
}
Event action(std::uint64_t ordinal,std::uint64_t time) {
    auto e=command(ordinal,time);e.identity.producer=core::MultiplayerDiagnosticProducer::game_host;
    e.identity.action_id=ordinal;e.identity.local_life=1;e.knowledge=core::MultiplayerKnowledge::local_action_accepted;
    core::MultiplayerActionPayload payload;payload.kind=core::MultiplayerActionKind::fire;
    payload.phase=core::MultiplayerActionPhase::accepted;payload.weapon_id=4;payload.mode=1;
    payload.clip_before=17;payload.clip_after=16;payload.eligible=true;
    payload.origin=std::array<float,3>{1,2,3};payload.direction=std::array<float,3>{1,0,0};
    e.payload=payload;return e;
}
const app::SessionReceiptIdentity receipt{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,123,456};
}

TEST_CASE("Shared multiplayer timeline stays allocation-free and inert when disabled","[multiplayer-timeline][core][app]") {
    Timeline timeline;CHECK_FALSE(timeline.enabled());CHECK(timeline.capacity_bytes()==0);
    CHECK_FALSE(timeline.observe(action(1,100)));CHECK_FALSE(timeline.mark_manual_relative_us(1));
    CHECK(timeline.statistics().observed==0);const auto text=timeline.encode();REQUIRE(text);
    CHECK(text->find("\"enabled\":false")!=text->npos);
    CHECK(text->find("\"transport_ack_proves_execution\":false")!=text->npos);
    CHECK_FALSE(timeline.encode(1024));
    app::MovementIncidentRecorder recorder;
    const auto receipt_text=recorder.encode(receipt);REQUIRE(receipt_text);
    CHECK(receipt_text->size()<4096);
    CHECK(receipt_text->find("\"long_frames\":0,\"metrics\":{},\"frames\":[]")!=receipt_text->npos);
    CHECK(receipt_text->find("\"multiplayer_timeline\":")!=receipt_text->npos);
    CHECK(recorder.statistics().total_capacity_bytes==0);
}
TEST_CASE("Shared multiplayer timeline records exact action transitions without an F8 mark","[multiplayer-timeline][core][app]") {
    Timeline timeline{true};REQUIRE(timeline.enabled());
    auto e=action(1,1'000'000'000);REQUIRE(timeline.observe(e));
    e.identity.producer_event_id=2;e.identity.action_id=2;REQUIRE(timeline.observe(e));
    CHECK_FALSE(timeline.observe(e)); // Distinct actions at equal time are not duplicates.
    auto next=e;next.identity.producer_event_id=1;next.identity.network_generation=2;REQUIRE(timeline.observe(next));
    CHECK_FALSE(timeline.observe(e)); // Late prior generation is not rebound.
    const auto stats=timeline.statistics();CHECK(stats.accepted==3);CHECK(stats.duplicates==1);CHECK(stats.stale_generation==1);
    const auto text=timeline.encode();REQUIRE(text);
    CHECK(text->find("\"action_id\":1")!=text->npos);CHECK(text->find("\"action_id\":2")!=text->npos);
    CHECK(text->find("\"exact_transitions\":[{")!=text->npos);
    CHECK(text->find("\"observations\":3")!=text->npos);
    CHECK(timeline.capacity_bytes()<=Timeline::maximum_capacity_bytes);
}
TEST_CASE("Shared presentation timing keeps delayed GPU source frames separate from poll frames",
          "[multiplayer-timeline][presentation-timing][core][app]") {
    Timeline timeline{true};
    auto frame=command(1,1'000'000'000);
    frame.identity.producer=core::MultiplayerDiagnosticProducer::renderer;
    frame.identity.command_id.reset();frame.identity.input_sequence.reset();frame.identity.render_frame=12;
    core::MultiplayerPresentationPayload presented;
    presented.phase=core::MultiplayerPresentationPhase::swap_end;
    presented.frame_ms=8.5;presented.present_ms=1.25;
    frame.payload=presented;REQUIRE(timeline.observe(frame));
    frame.identity.producer_event_id=2;frame.identity.render_frame=13;frame.local_steady_ns+=8'000'000;
    presented.frame_ms=8.0;frame.payload=presented;REQUIRE(timeline.observe(frame));
    auto result=frame;result.identity.producer_event_id=3;result.identity.render_frame=12;
    result.local_steady_ns+=1'000'000;
    core::MultiplayerPresentationPayload gpu;gpu.phase=core::MultiplayerPresentationPhase::gpu_result;gpu.gpu_ms=27.5;
    result.payload=gpu;REQUIRE(timeline.observe(result));
    // An unavailable/disjoint result is not a measured zero duration.
    result.identity.producer_event_id=4;result.identity.render_frame=13;
    result.local_steady_ns+=60'000'000;gpu.gpu_ms.reset();result.payload=gpu;
    REQUIRE(timeline.observe(result));
    const auto text=timeline.encode();REQUIRE(text);
    const auto measured=text->find("\"gpu_ms\":27.5");REQUIRE(measured!=text->npos);
    const auto begin=text->rfind("\"local_steady_ns\":",measured);REQUIRE(begin!=text->npos);
    CHECK(text->substr(begin,measured-begin).find("\"render_frame\":12")!=text->npos);
    CHECK(text->find("\"frame_ms\":8.5")!=text->npos);
    CHECK(text->find("\"gpu_ms\":null")!=text->npos);
    CHECK(text->find("\"frame_ms\":null")!=text->npos);
    auto late=result;late.identity.producer_event_id=5;late.identity.network_generation=2;
    late.identity.map_generation=2;late.local_steady_ns+=1;
    REQUIRE(timeline.observe(late));
    result.identity.producer_event_id=6;result.local_steady_ns=late.local_steady_ns+1;
    CHECK_FALSE(timeline.observe(result)); // A late old-map result is not rebound.
}
TEST_CASE("Shared diagnostics reject forged authority malformed clocks and unbounded packet membership","[multiplayer-timeline][negative][core][app]") {
    Timeline timeline{true};auto e=command(1,1'000'000'000);
    core::MultiplayerNetworkPayload net;net.phase=core::MultiplayerNetworkPhase::transport_ack;
    e.payload=net;e.knowledge=core::MultiplayerKnowledge::server_damage_observed;
    CHECK_FALSE(timeline.observe(e)); // ACK must never become a hit witness.
    e.knowledge=core::MultiplayerKnowledge::transport_acknowledged;REQUIRE(timeline.observe(e));
    e.identity.producer_event_id=2;net.inclusion_count=65;e.payload=net;CHECK_FALSE(timeline.observe(e));
    net.inclusion_count=0;net.phase=core::MultiplayerNetworkPhase::rollback;net.atomic_commit=true;
    e.payload=net;e.knowledge=core::MultiplayerKnowledge::unknown;CHECK_FALSE(timeline.observe(e));
    core::MultiplayerGeometryPayload geometry;geometry.intersects=false;e.payload=geometry;
    e.knowledge=core::MultiplayerKnowledge::server_miss_observed;CHECK_FALSE(timeline.observe(e));
    e.knowledge=core::MultiplayerKnowledge::local_proxy;e.server_seconds=100;
    CHECK_FALSE(timeline.observe(e)); // Unknown clock cannot acquire seconds accidentally.
    e.server_time_domain=core::MultiplayerTimeDomain::server_simulation_seconds;
    REQUIRE(timeline.observe(e));
    e.identity.producer_event_id=3;geometry.fraction=std::numeric_limits<double>::quiet_NaN();e.payload=geometry;
    CHECK_FALSE(timeline.observe(e));
    auto bad=action(5,1'100'000'000);auto& p=std::get<core::MultiplayerActionPayload>(bad.payload);
    p.reason[0]='"';CHECK_FALSE(timeline.observe(bad));
    CHECK(timeline.statistics().invalid==7);
}
TEST_CASE("Shared correction identity uses only the accepted owner replay pass",
    "[multiplayer-timeline][multiplayer-diagnostics][clock-ownership][core][app]") {
    app::MovementIncidentRecorder recorder{true};
    recorder.set_multiplayer_clock_origin_ns(1'000'000'000);
    core::MovementCorrectionWitness correction;
    correction.event_ordinal=1;correction.network_generation=1;correction.map_generation=1;
    correction.life_epoch=2;correction.source_record_identity=40;correction.source_record_ordinal=41;
    correction.source_sequence=42;correction.boundary_command=43;
    correction.observed_ns=1'100'000'000;correction.before_prediction_epoch=19;
    correction.after_prediction_epoch=37;correction.accepted=true;
    bool expected=true;
    SECTION("Accepted owner generation is not replaced with command or record identity") {}
    SECTION("Rejected correction does not claim the retained previous generation") {
        correction.accepted=false;expected=false;
    }
    SECTION("Unknown accepted epoch remains unavailable instead of generation zero") {
        correction.after_prediction_epoch=0;expected=false;
    }
    REQUIRE(recorder.observe_correction(correction));
    const auto encoded=recorder.encode(receipt);REQUIRE(encoded);
    const auto begin=encoded->find("\"multiplayer_timeline\":");REQUIRE(begin!=encoded->npos);
    const auto shared=encoded->substr(begin);
    CHECK(shared.find(expected ? "\"simulation_iteration\":37" : "\"simulation_iteration\":null")!=shared.npos);
    if(!expected)CHECK(shared.find("\"simulation_iteration\":37")==shared.npos);
    CHECK(shared.find("\"command_id\":43")!=shared.npos);
    CHECK(shared.find("\"source_record\":40")!=shared.npos);
}

TEST_CASE("One existing F8 mark includes shared pre and post context across normal death","[multiplayer-timeline][manual][core][app]") {
    app::MovementIncidentRecorder recorder{true};recorder.set_multiplayer_clock_origin_ns(1'000'000'000);
    app::MovementFrameRecord frame;frame.frame_number=1;frame.monotonic_us=4'000'000;frame.network_generation=1;frame.map_generation=1;
    REQUIRE(recorder.observe(frame));
    auto early=action(1,1'100'000'000);REQUIRE(recorder.observe(early));
    REQUIRE(recorder.mark(app::MovementIncidentTrigger::manual,4'000'000));
    auto death=action(2,5'250'000'000);death.identity.local_life=2;
    core::MultiplayerLifecyclePayload payload;payload.kind=core::MultiplayerLifecycleKind::death;payload.alive=false;
    death.payload=payload;death.knowledge=core::MultiplayerKnowledge::unknown;REQUIRE(recorder.observe(death));
    auto later=action(3,6'000'000'000);later.identity.local_life=3;REQUIRE(recorder.observe(later));
    const auto text=recorder.encode(receipt);REQUIRE(text);
    const auto begin=text->find("\"multiplayer_timeline\":");REQUIRE(begin!=text->npos);
    const auto manual=text->find("\"manual_windows\":[",begin);REQUIRE(manual!=text->npos);
    const auto end=text->find("\"exact_transitions\":",manual);REQUIRE(end!=text->npos);
    const auto window=text->substr(manual,end-manual);
    CHECK(window.find("\"trigger_ns\":5000000000")!=window.npos);
    CHECK(window.find("\"begin_ns\":1000000000")!=window.npos);
    CHECK(window.find("\"end_ns\":6000000000")!=window.npos);
    CHECK(window.find("\"post_complete\":true")!=window.npos);
    CHECK(window.find("\"local_life\":1")!=window.npos);
    CHECK(window.find("\"local_life\":2")!=window.npos);
    CHECK(window.find("\"local_life\":3")!=window.npos);
    const auto stats=recorder.statistics();CHECK(stats.multiplayer_capacity_bytes>0);
    CHECK(stats.total_capacity_bytes==stats.allocated_capacity_bytes+stats.session_capacity_bytes+stats.remote_capacity_bytes+stats.multiplayer_capacity_bytes);
    CHECK(text->size()<=app::MovementIncidentRecorder::maximum_encoded_bytes);
    std::string fixture;
#ifdef _MSC_VER
    char* environment{};std::size_t length{};
    REQUIRE(::_dupenv_s(&environment,&length,"HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_OUTPUT")==0);
    if(environment)fixture=environment;std::free(environment);
#else
    if(const auto* environment=std::getenv("HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_OUTPUT"))fixture=environment;
#endif
    if(!fixture.empty()) {
        REQUIRE_FALSE(std::filesystem::exists(fixture));std::ofstream output(fixture,std::ios::binary);
        REQUIRE(output.good());output<<*text;output.close();REQUIRE(output.good());
    }
}
TEST_CASE("Timeline limits preserve summaries and disclose every bounded detail omission","[multiplayer-timeline][limits][core][app]") {
    Timeline timeline{true};timeline.set_clock_origin_ns(1'000'000'000);
    for(std::uint64_t i=1;i<=2'000;++i)REQUIRE(timeline.observe(action(i,1'000'000'000+i*1'000'000)));
    for(std::uint64_t i=0;i<Timeline::maximum_manual_windows;++i)REQUIRE(timeline.mark_manual_relative_us(3'000'000+i));
    CHECK_FALSE(timeline.mark_manual_relative_us(4'000'000));
    for(std::uint64_t i=1;i<=100;++i)REQUIRE(timeline.observe(command(i,4'000'000'000+i)));
    const auto stats=timeline.statistics();CHECK(stats.accepted==2'100);
    CHECK(stats.exact_evictions==2'000-Timeline::maximum_action_exact_events);
    CHECK(stats.recent_evictions>0);CHECK(stats.manual_omissions>0);CHECK(stats.sampled_out==99);
    CHECK(stats.manual_marks==Timeline::maximum_manual_windows);CHECK(stats.manual_marks_dropped==1);
    const auto output=timeline.encode(65'536);REQUIRE(output);CHECK(output->size()<=65'536);
    CHECK(output->find("\"encoded_events_omitted\":0")==output->npos);
    CHECK(output->find("\"observations\":2000")!=output->npos);
    CHECK(timeline.capacity_bytes()<=Timeline::maximum_capacity_bytes);
}
TEST_CASE("Timeline serializes distinct clocks command identities exact packet membership and input ownership","[multiplayer-timeline][schema][core][app]") {
    Timeline timeline{true};auto e=command(1,1'000'000'000);
    core::MultiplayerNetworkPayload p;p.phase=core::MultiplayerNetworkPhase::packet_sent;
    p.command_count=2;p.inclusion_count=2;p.command_ids[0]=55;p.command_ids[1]=59;p.acknowledgement=12;
    e.identity.carrier_packet=99;e.identity.source_record=700;e.identity.source_record_ordinal=31;
    e.identity.source_message_bit_offset=128;e.identity.source_entry_ordinal=2;e.payload=p;
    e.knowledge=core::MultiplayerKnowledge::command_sent;REQUIRE(timeline.observe(e));
    e.identity.producer_event_id=2;e.knowledge=core::MultiplayerKnowledge::input_intent;
    core::MultiplayerInputPayload input;input.key=0;input.console_owned=true;input.gameplay_routed=false;
    input.sdl_timestamp_ns=125;input.dequeue_steady_ns=1'000'000'001;e.local_steady_ns=1'000'000'001;e.payload=input;
    REQUIRE(timeline.observe(e));const auto text=timeline.encode();REQUIRE(text);
    CHECK(text->find("\"command_ids\":[55,59]")!=text->npos);
    CHECK(text->find("\"carrier_packet\":99")!=text->npos);
    CHECK(text->find("\"source_record\":700")!=text->npos);
    CHECK(text->find("\"source_record_ordinal\":31")!=text->npos);
    CHECK(text->find("\"source_message_bit_offset\":128")!=text->npos);
    CHECK(text->find("\"sdl_timestamp_ns\":125")!=text->npos);
    CHECK(text->find("\"gameplay_routed\":false")!=text->npos);
}
TEST_CASE("Warm shared timeline on and off observation overhead is measured without I/O inside update","[multiplayer-timeline][overhead][core][app]") {
    constexpr std::uint64_t count=20'000;Timeline off, on{true};
    for(std::uint64_t i=1;i<500;++i)static_cast<void>(on.observe(command(i,1'000'000'000+i)));
    const auto run=[&](Timeline& timeline){const auto start=std::chrono::steady_clock::now();
        for(std::uint64_t i=500;i<count+500;++i){const auto e=command(i,1'000'000'000+i*1'000'000);static_cast<void>(timeline.observe(e));}
        return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/static_cast<double>(count);};
    const auto off_ns=run(off), on_ns=run(on);
    std::cout<<"[multiplayer-overhead] fixture=warm_immutable_command count="<<count<<" off_ns_per_event="<<off_ns<<" on_ns_per_event="<<on_ns<<'\n';
    CHECK(off.statistics().observed==0);CHECK(on.statistics().accepted==count+499);
    CHECK(on_ns<50'000.); // Deterministic bounded-work guard, not a live FPS verdict.
}

TEST_CASE("Ordinary Studio timeline keeps raw animation anchors separate from evaluated pose","[multiplayer-timeline][schema][studio][core][app]") {
    Timeline timeline{true};auto e=command(1,1'000'000'000);
    e.identity.producer=core::MultiplayerDiagnosticProducer::ordinary_studio;
    core::MultiplayerPresentationPayload pose;pose.phase=core::MultiplayerPresentationPhase::pose;
    pose.sequence=3;pose.animation_frame=7.5F;pose.pose_seconds=12.25;
    pose.raw_animation_sequence=3;pose.raw_animation_frame=128.;pose.raw_animtime_seconds=12.;pose.raw_framerate=-.5;
    pose.raw_controllers=std::array<std::uint8_t,4>{0,64,128,255};pose.raw_blending=std::array<std::uint8_t,2>{12,240};
    pose.suppressed_repetitions=4;pose.diagnostic_cache_evictions=2;e.payload=pose;
    REQUIRE(timeline.observe(e));const auto encoded=timeline.encode();REQUIRE(encoded);
    CHECK(encoded->find("\"producer\":11")!=encoded->npos);
    CHECK(encoded->find("\"animation_frame\":7.5")!=encoded->npos);
    CHECK(encoded->find("\"raw_animation_frame\":128")!=encoded->npos);
    CHECK(encoded->find("\"raw_framerate\":-0.5")!=encoded->npos);
    CHECK(encoded->find("\"raw_controllers\":[0,64,128,255]")!=encoded->npos);
    CHECK(encoded->find("\"raw_blending\":[12,240]")!=encoded->npos);
    CHECK(encoded->find("\"suppressed_repetitions\":4")!=encoded->npos);
    CHECK(encoded->find("\"diagnostic_cache_evictions\":2")!=encoded->npos);
    e.identity.producer_event_id=2;pose.raw_framerate=std::numeric_limits<double>::quiet_NaN();e.payload=pose;
    CHECK_FALSE(timeline.observe(e));CHECK(timeline.statistics().invalid==1);
}

TEST_CASE("Delayed F8 recovers protected local action and command after cosmetic recent-ring washout","[multiplayer-timeline][manual][priority][core][app]") {
    Timeline timeline{true};timeline.set_clock_origin_ns(1'000'000'000);
    auto accepted=action(1,1'100'000'000);REQUIRE(timeline.observe(accepted));
    auto cmd=command(1,1'100'000'000);
    std::get<core::MultiplayerCommandPayload>(cmd.payload).phase=core::MultiplayerCommandPhase::history_inserted;
    REQUIRE(timeline.observe(cmd));
    for(std::uint64_t i=1;i<=5000;++i) {
        auto noise=action(i+1,1'100'000'000+i*779'600);
        auto& value=std::get<core::MultiplayerActionPayload>(noise.payload);
        value.weapon_id=static_cast<std::uint32_t>(i+100);
        // Distinct sampling keys force real recent-ring eviction. Even a
        // cosmetic acceptance with an action-like ID is not a local action.
        if(i%3==0){value.phase=core::MultiplayerActionPhase::server_observed;noise.knowledge=core::MultiplayerKnowledge::server_event_observed;}
        else {value.phase=i%3==1?core::MultiplayerActionPhase::feedback:core::MultiplayerActionPhase::accepted;
            noise.knowledge=core::MultiplayerKnowledge::cosmetic_feedback;}
        REQUIRE(timeline.observe(noise));
    }
    REQUIRE(timeline.statistics().recent_evictions>3000);
    CHECK(timeline.statistics().exact_evictions==0);
    REQUIRE(timeline.mark_manual_relative_us(4'000'000)); // accepted shot was 3.9 seconds earlier
    REQUIRE(timeline.observe(action(5002,6'000'000'000)));
    const auto encoded=timeline.encode();REQUIRE(encoded);
    const auto begin=encoded->find("\"manual_windows\":[");const auto end=encoded->find("\"exact_transitions\":[");
    REQUIRE(begin!=encoded->npos);REQUIRE(end>begin);
    const auto window=encoded->substr(begin,end-begin);
    const std::string action_token="\"action_id\":1,";
    const auto action_position=window.find(action_token);REQUIRE(action_position!=window.npos);
    CHECK(window.find(action_token,action_position+action_token.size())==window.npos);
    CHECK(window.find("\"producer\":2,\"producer_event_id\":1,")!=window.npos);
    CHECK(window.find("\"action_id\":5002,")!=window.npos);
    CHECK(window.find("\"post_complete\":true")!=window.npos);
    CHECK(timeline.statistics().manual_omissions>0);
}

TEST_CASE("Full F8 priority window keeps nearest observations and stable equal-distance ties","[multiplayer-timeline][manual][priority][core][app]") {
    Timeline timeline{true};timeline.set_clock_origin_ns(1'000'000'000);
    for(std::uint64_t i=1;i<=70;++i)REQUIRE(timeline.observe(action(i,1'000'000'000+i*10'000'000)));
    REQUIRE(timeline.mark_manual_relative_us(1'000'000));
    // Each event exists in both exact and recent storage. Seed dedup must not
    // count the same six omissions twice or duplicate retained identities.
    CHECK(timeline.statistics().manual_omissions==6);
    REQUIRE(timeline.observe(action(71,2'100'000'000))); // closer: replaces action7
    REQUIRE(timeline.observe(action(72,2'920'000'000))); // ties action8: existing wins
    REQUIRE(timeline.observe(action(73,2'930'000'000))); // farther: omitted
    const auto encoded=timeline.encode();REQUIRE(encoded);
    const auto begin=encoded->find("\"manual_windows\":[");const auto end=encoded->find("\"exact_transitions\":[");
    REQUIRE(begin!=encoded->npos);REQUIRE(end>begin);const auto window=encoded->substr(begin,end-begin);
    for(std::uint64_t i=1;i<=73;++i) {
        const auto token="\"action_id\":"+std::to_string(i)+",";
        const auto position=window.find(token);
        if(i>=8 && i<=71) {
            REQUIRE(position!=window.npos);CHECK(window.find(token,position+token.size())==window.npos);
        } else CHECK(position==window.npos);
    }
    CHECK(timeline.statistics().manual_omissions==9);
}

TEST_CASE("Immutable command and action evidence survives rendering cadence stalls death and delayed F8","[multiplayer-timeline][cadence][manual][core][app]") {
    constexpr std::uint64_t origin=1'000'000'000ULL, duration=3'000'000'000ULL;
    constexpr std::array<std::uint64_t,5> accepted_commands{20,70,120,220,270};
    // One immutable command/action stream, constructed once. Rendering only
    // adds observations: it must never invoke or resubmit an accepted action.
    const std::vector<Event> immutable=[&] {
        std::vector<Event> events;
        for(std::uint64_t i=1;i<=300;++i) {
            const auto stamp=origin+i*10'000'000ULL;
            auto cmd=command(i,stamp);cmd.identity.local_life=i<180?1U:2U;
            cmd.provenance=core::MultiplayerProvenance::synthetic;
            auto& values=std::get<core::MultiplayerCommandPayload>(cmd.payload);
            values.phase=core::MultiplayerCommandPhase::history_inserted;
            values.buttons=static_cast<std::uint16_t>((i%50U==20U)?1U:0U);values.history_insert_ns=stamp;
            events.push_back(cmd);
            auto sent=cmd;sent.identity.producer=core::MultiplayerDiagnosticProducer::transport;
            sent.identity.carrier_packet=i+1000;sent.local_steady_ns=stamp+1000;
            sent.knowledge=core::MultiplayerKnowledge::command_sent;
            core::MultiplayerNetworkPayload packet;packet.phase=core::MultiplayerNetworkPhase::packet_sent;
            packet.command_count=1;packet.inclusion_count=1;packet.command_ids[0]=static_cast<std::uint32_t>(*cmd.identity.command_id);
            sent.payload=packet;events.push_back(sent);
            if(std::ranges::find(accepted_commands,i)!=accepted_commands.end()) {
                auto accepted=action(i,stamp+2000);accepted.identity.local_life=cmd.identity.local_life;
                accepted.provenance=core::MultiplayerProvenance::synthetic;events.push_back(accepted);
            }
        }
        for(std::uint64_t i=1;i<=2;++i) {
            auto life=command(i,origin+(i==1?1'500'000'000ULL:1'800'000'000ULL));
            life.identity.producer=core::MultiplayerDiagnosticProducer::application;
            life.identity.command_id.reset();life.identity.input_sequence.reset();life.identity.local_life=i;
            life.provenance=core::MultiplayerProvenance::synthetic;
            core::MultiplayerLifecyclePayload state;state.kind=i==1?core::MultiplayerLifecycleKind::death:core::MultiplayerLifecycleKind::respawn;
            state.alive=i==2;life.payload=state;events.push_back(life);
        }
        return events;
    }();
    struct Cadence {std::uint64_t hz;bool jitter;bool stall;};
    constexpr std::array<Cadence,7> cadences{{{30,false,false},{60,false,false},{100,false,false},{144,false,false},{240,false,false},{100,true,false},{144,true,true}}};
    for(const auto cadence:cadences) {
        INFO("render_hz="<<cadence.hz<<" jitter="<<cadence.jitter<<" stall="<<cadence.stall);
        Timeline timeline{true};timeline.set_clock_origin_ns(origin);
        auto events=immutable;
        std::uint64_t elapsed{},frames{};
        while(elapsed<duration) {
            auto step=1'000'000'000ULL/cadence.hz;
            if(cadence.jitter)step=frames%2==0?step/2:step+step/2;
            if(cadence.stall && frames==60)step+=220'000'000ULL;
            elapsed=std::min(duration,elapsed+step);++frames;
            Event rendered;rendered.identity.producer=core::MultiplayerDiagnosticProducer::renderer;
            rendered.identity.producer_event_id=frames;rendered.identity.network_generation=1;rendered.identity.map_generation=1;
            rendered.identity.render_frame=frames;rendered.local_steady_ns=origin+elapsed;
            rendered.provenance=core::MultiplayerProvenance::synthetic;
            core::MultiplayerPresentationPayload presentation;presentation.phase=core::MultiplayerPresentationPhase::swap_end;
            presentation.frame_ms=static_cast<double>(step)/1e6;rendered.payload=presentation;events.push_back(rendered);
        }
        std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b){return a.local_steady_ns<b.local_steady_ns;});
        bool marked=false;
        for(const auto& event:events) {
            if(!marked && event.local_steady_ns>=origin+2'000'000'000ULL) {
                REQUIRE(timeline.mark_manual_relative_us(2'000'000));marked=true;
            }
            REQUIRE(timeline.observe(event));
        }
        const auto stats=timeline.statistics();CHECK(stats.accepted==immutable.size()+frames);
        CHECK(stats.payload_observations[0]==300);CHECK(stats.payload_observations[1]==300);
        CHECK(stats.payload_observations[2]==accepted_commands.size());CHECK(stats.payload_observations[3]==2);
        CHECK(stats.payload_observations[4]==frames);CHECK(stats.manual_marks==1);
        CHECK(stats.exact_evictions==600-Timeline::maximum_chain_exact_events);
        CHECK(stats.manual_omissions>0); // A bounded window never claims a complete dense chain.
        const auto encoded=timeline.encode();REQUIRE(encoded);
        const auto exact_begin=encoded->find("\"exact_transitions\":[");REQUIRE(exact_begin!=encoded->npos);
        const auto exact_end=encoded->find("\"recent_events\":[",exact_begin);REQUIRE(exact_end!=encoded->npos);
        const auto exact=encoded->substr(exact_begin,exact_end-exact_begin);
        const auto manual_begin=encoded->find("\"manual_windows\":[");REQUIRE(manual_begin!=encoded->npos);
        const auto manual=encoded->substr(manual_begin,exact_begin-manual_begin);
        for(const auto command_id:accepted_commands) {
            const auto identity="\"action_id\":"+std::to_string(command_id)+",";
            CHECK(exact.find(identity)!=exact.npos);CHECK(manual.find(identity)!=manual.npos);
            CHECK(exact.find("\"command_id\":"+std::to_string(command_id+100)+",")!=exact.npos);
        }
        CHECK(manual.find("\"post_complete\":true")!=manual.npos);
        CHECK(manual.find("\"local_life\":1")!=manual.npos);CHECK(manual.find("\"local_life\":2")!=manual.npos);
        CHECK(encoded->find("\"observations\":5")!=encoded->npos);
        CHECK(encoded->find("\"transport_ack_proves_execution\":false")!=encoded->npos);
        CHECK(encoded->size()<=Timeline::maximum_encoded_bytes);
    }
}
