#include <hlclient/app/movement_session_diagnostics.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {
namespace app=hlclient::app;
using Recorder=app::MovementSessionRecorder;
app::MovementFrameRecord frame(const std::uint64_t number,const std::uint64_t time) {
    app::MovementFrameRecord result;result.frame_number=number;result.monotonic_us=time;
    result.network_generation=1;result.map_generation=2;result.prediction_active=true;
    result.frame_interval_ms=10;result.state_age_ms=20;result.correction_ordinal=0;return result;
}
app::MovementFrameContext controlled(){return {true,true,true,true,1};}
// Tiny read-only selector for project-generated JSON; assertions below test
// semantic sections instead of relying on unrelated repeated key substrings.
std::string_view member(const std::string_view json,const std::string_view name) {
    const auto key='"'+std::string(name)+"\":";const auto found=json.find(key);
    if(found==json.npos)return {};const auto begin=found+key.size();
    if(begin==json.size())return {};
    if(json[begin]!='{' && json[begin]!='['){const auto end=json.find_first_of(",}",begin);return json.substr(begin,end-begin);}
    unsigned depth{};bool quoted{},escaped{};
    for(std::size_t i=begin;i<json.size();++i){const char c=json[i];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;continue;}if(c=='{' || c=='[')++depth;
        else if(c=='}' || c==']'){if(--depth==0)return json.substr(begin,i-begin+1);}}
    return {};
}
std::uint64_t count(const std::string_view json,const std::string_view name){const auto text=member(json,name);REQUIRE_FALSE(text.empty());return std::stoull(std::string(text));}
double value(const std::string_view json,const std::string_view name){const auto text=member(json,name);REQUIRE_FALSE(text.empty());return std::stod(std::string(text));}
std::string_view metric(const std::string_view aggregate,const std::string_view name){return member(member(aggregate,"metrics"),name);}
}

TEST_CASE("E32 session diagnostics disabled allocates nothing and long sessions retain earlier evidence",
    "[e32][movement-session][movement-diagnostics][app][core]") {
    Recorder disabled;for(std::uint64_t i=1;i<1000;++i)disabled.observe(frame(i,i*1000),controlled());
    CHECK_FALSE(disabled.enabled());CHECK(disabled.capacity_bytes()==0);CHECK(disabled.statistics().accepted_frames==0);
    CHECK_FALSE(disabled.observe_gpu(1,3));CHECK_FALSE(disabled.mark_manual(1000));
    const auto off=disabled.encode();REQUIRE(off);CHECK(off->find("\"availability\":\"disabled\"")!=off->npos);
    Recorder recorder{true};REQUIRE(recorder.enabled());
    for(std::uint64_t i=0;i<36010;++i){auto observed=frame(i,i*100'000);if(i==1000)observed.frame_interval_ms=120;
        if(i>=5000 && i<5010)observed.prediction_active=false;recorder.observe(observed,controlled());}
    const auto statistics=recorder.statistics();CHECK(statistics.accepted_frames==36010);CHECK(statistics.timeline_bins<=512);
    CHECK(statistics.timeline_bucket_width_us==8'000'000);CHECK(statistics.timeline_compactions==3);CHECK(statistics.fallback_entries==1);
    CHECK(statistics.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    const auto output=recorder.encode();REQUIRE(output);CHECK(output->size()<=Recorder::maximum_encoded_bytes);
    const auto totals=member(*output,"totals");CHECK(count(member(totals,"counts"),"frames")==36010);
    CHECK(value(metric(totals,"frame_interval_ms"),"maximum")==120);
    CHECK(count(member(totals,"counts"),"fallback_observed_interval_us")==900000);
    CHECK(count(member(*output,"coverage"),"first_us")==0);CHECK(count(member(*output,"coverage"),"last_us")==3'600'900'000ULL);
    CHECK(output->find("approximate_upper_bounds")!=output->npos);
}

TEST_CASE("B080 producer maxima retain exact identities for every correction and resolve retained or missing evidence",
    "[b080][movement-session][correction-witness]") {
    Recorder recorder{true};recorder.observe(frame(1,0),controlled());
    hlclient::core::MovementCorrectionWitness first;
    first.event_ordinal=1;first.network_generation=1;first.map_generation=2;
    first.life_epoch=1;first.source_record_identity=11;first.observed_us=1'000'000;
    first.raw_position_error=30.025;
    auto second=first;second.event_ordinal=2;second.source_record_identity=12;second.raw_position_error=.125;
    recorder.observe_correction(first,controlled());recorder.observe_correction(second,controlled());
    recorder.observe_correction(second,controlled()); // Same producer identity is not a third event.
    const std::array retained{first,second};
    auto output=recorder.encode(retained);REQUIRE(output);
    const auto maximum=member(member(*output,"totals"),"producer_correction_maximum");
    CHECK(count(maximum,"events")==2);CHECK(value(maximum,"raw_error")==Catch::Approx(30.025));
    CHECK(count(member(maximum,"witness_identity"),"event_ordinal")==1);
    CHECK(member(maximum,"status")=="\"available\"");
    const auto cohort_max=member(member(member(*output,"cohorts"),"controlled_alive"),"producer_correction_maximum");
    CHECK(count(cohort_max,"events")==2);
    CHECK(count(member(*output,"observations"),"producer_duplicates")==1);
    // Later frame snapshots may expose only the last small correction. They
    // cannot silently overwrite the independent producer maximum.
    auto observed=frame(2,1'010'000);observed.raw_correction_units=.125;observed.correction_ordinal=2;
    recorder.observe(observed,controlled());
    for(std::uint64_t i=3;i<620;++i)recorder.observe(frame(i,i*1'000'000),controlled());
    output=recorder.encode(retained);REQUIRE(output);
    CHECK(count(member(member(*output,"totals"),"producer_correction_maximum"),"events")==2);
    CHECK(output->find("\"producer_correction_maximum\":{\"events\":2,\"raw_error\":30.025")!=output->npos);
    const auto missing=recorder.encode();REQUIRE(missing);
    CHECK(member(member(member(*missing,"totals"),"producer_correction_maximum"),"status")=="\"witness_missing\"");
    CHECK(recorder.capacity_bytes()<=Recorder::maximum_capacity_bytes);
    Recorder disabled;disabled.observe_correction(first,controlled());CHECK(disabled.capacity_bytes()==0);
}

TEST_CASE("E32 FPS logging groups actual slow intervals and retains stages from the original worst frame",
    "[e32][fps-diagnostics][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};recorder.observe(frame(1,0),controlled());
    auto first=frame(2,25'000);first.frame_interval_ms=25;first.cpu_update_ms=9;
    first.prediction_work=hlclient::core::PredictionWorkDiagnostics{9,8,7,90,80,70,60};
    recorder.observe(first,controlled());
    auto worst=frame(3,65'000);worst.frame_interval_ms=40;worst.cpu_update_ms=27;
    worst.studio_ms=12;worst.render_ms=4;worst.observed_players=16;worst.entities=240;
    worst.prediction_work=hlclient::core::PredictionWorkDiagnostics{3,4,5,6,7,8,9};
    recorder.observe(worst,controlled());worst.prediction_work->command_ms=99;
    recorder.observe(frame(4,75'000),controlled());
    REQUIRE(recorder.observe_gpu(3,18));CHECK(recorder.observe_gpu(3,100)); // Duplicate cannot replace the original timing.
    const auto output=recorder.encode();REQUIRE(output);const auto pacing=member(*output,"frame_pacing");
    CHECK(value(pacing,"threshold_ms")==20);CHECK(count(pacing,"total_episodes")==1);
    CHECK(count(pacing,"slow_frames")==2);CHECK(value(pacing,"slow_frame_interval_total_ms")==65);
    const auto episodes=member(pacing,"episodes");CHECK(count(episodes,"begin_us")==0);CHECK(count(episodes,"end_us")==65'000);
    CHECK(count(episodes,"first_frame")==2);CHECK(count(episodes,"last_frame")==3);CHECK(count(episodes,"worst_frame")==3);
    CHECK(value(episodes,"slowest_frame_fps_equivalent")==25);
    CHECK(value(episodes,"slow_frames_mean_fps_equivalent")==Catch::Approx(2000.0/65));
    CHECK(member(episodes,"recovery_observed")=="true");CHECK(episodes.find("\"recovered\"")!=episodes.npos);
    const auto timings=member(episodes,"worst_frame_timings");CHECK(value(timings,"cpu_update_ms")==27);
    CHECK(value(timings,"studio_ms")==12);CHECK(value(timings,"gpu_ms")==18);
    const auto work=member(episodes,"prediction_work");CHECK(value(work,"command_ms")==3);
    CHECK(value(work,"replay_ms")==4);CHECK(value(work,"collision_query_ms")==5);
    CHECK(count(work,"trace_queries")==6);CHECK(count(work,"position_queries")==7);
    CHECK(count(work,"contents_queries")==8);CHECK(count(work,"traversal_steps")==9);
    CHECK(count(episodes,"observed_players")==16);CHECK(count(episodes,"entities")==240);
    CHECK(recorder.capacity_bytes()<=Recorder::maximum_capacity_bytes);
    auto exact=frame(5,95'000);exact.frame_interval_ms=20;recorder.observe(exact,controlled());
    CHECK(count(member(*recorder.encode(),"frame_pacing"),"total_episodes")==1);
}

TEST_CASE("E32 FPS episodes exclude context transitions and distinguish missing intervals life and generation boundaries",
    "[e32][fps-diagnostics][movement-session][movement-diagnostics][app][core]") {
    for(int boundary=0;boundary<6;++boundary) {
        Recorder recorder{true};auto slow=frame(1,0);slow.frame_interval_ms=1000;recorder.observe(slow,controlled());
        CHECK(count(member(*recorder.encode(),"frame_pacing"),"total_episodes")==0);
        slow=frame(2,30'000);slow.frame_interval_ms=30;recorder.observe(slow,controlled());
        auto changed=frame(3,1'030'000);changed.frame_interval_ms=1000;auto context=controlled();
        const std::array<std::string_view,6> reasons{"context_boundary","context_boundary","life_boundary",
            "generation_boundary","missing_interval","frame_number_gap"};
        if(boundary==0)context.focused=false;
        if(boundary==1)context.alive=false;
        if(boundary==2)context.life_epoch=2;
        if(boundary==3)changed.map_generation=3;
        if(boundary==4)changed.frame_interval_ms.reset();
        if(boundary==5)changed.frame_number=4;
        recorder.observe(changed,context);const auto output=recorder.encode();REQUIRE(output);
        const auto pacing=member(*output,"frame_pacing");CHECK(count(pacing,"total_episodes")==1);
        CHECK(count(pacing,"slow_frames")==1);CHECK(value(pacing,"slow_frame_interval_total_ms")==30);
        const auto episodes=member(pacing,"episodes");CHECK(member(episodes,"recovery_observed")=="false");
        CHECK(episodes.find(reasons[boundary])!=episodes.npos);
    }
}

TEST_CASE("E32 FPS logging keeps the worst episodes across a long session without exhausting detailed incident slots",
    "[e32][fps-diagnostics][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};std::uint64_t stamp{},number=1;recorder.observe(frame(number,stamp),controlled());
    for(std::uint64_t episode=0;episode<100;++episode) {
        stamp+=30'000;auto slow=frame(++number,stamp);slow.frame_interval_ms=30+static_cast<double>(episode);
        recorder.observe(slow,controlled());stamp+=10'000;recorder.observe(frame(++number,stamp),controlled());
    }
    for(std::uint64_t i=0;i<40'000;++i){stamp+=100'000;recorder.observe(frame(++number,stamp),controlled());}
    const auto output=recorder.encode();REQUIRE(output);const auto pacing=member(*output,"frame_pacing");
    CHECK(count(pacing,"total_episodes")==100);CHECK(count(pacing,"retained_episodes")==64);
    CHECK(count(pacing,"omitted_episodes")==36);CHECK(count(pacing,"slow_frames")==100);
    CHECK(value(member(pacing,"episodes"),"maximum_frame_ms")==129);
    CHECK(count(member(pacing,"episodes"),"ordinal")==100);
    CHECK(recorder.statistics().timeline_compactions>0);CHECK(recorder.capacity_bytes()<=Recorder::maximum_capacity_bytes);
    CHECK(output->size()<=Recorder::maximum_encoded_bytes);
    app::MovementIncidentRecorder detailed{true};stamp=0;number=1;
    for(int i=0;i<8;++i){auto stalled=frame(number++,stamp);stalled.frame_interval_ms=70;
        REQUIRE(detailed.observe(stalled,controlled()));stamp+=3'000'000;}
    CHECK(detailed.statistics().incidents==app::MovementIncidentRecorder::maximum_automatic_incidents);
    auto normal=frame(number++,stamp);REQUIRE(detailed.observe(normal,controlled()));
    auto late=frame(number++,stamp+45'000);late.frame_interval_ms=45;REQUIRE(detailed.observe(late,controlled()));
    const app::SessionReceiptIdentity identity{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,1,1};
    const auto combined=detailed.encode(identity);REQUIRE(combined);
    CHECK(combined->find("\"maximum_frame_ms\":45")!=combined->npos);
}

TEST_CASE("E32 FPS logging active episodes encode without mutating capture and includes a full second stall",
    "[e32][fps-diagnostics][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};recorder.observe(frame(1,0),controlled());
    auto stalled=frame(2,1'000'000);stalled.frame_interval_ms=1000;recorder.observe(stalled,controlled());
    const auto first=recorder.encode();REQUIRE(first);CHECK(*recorder.encode()==*first);
    const auto active=member(member(*first,"frame_pacing"),"episodes");
    CHECK(member(active,"recovery_observed")=="false");CHECK(value(active,"span_ms")==1000);
    CHECK(value(active,"slowest_frame_fps_equivalent")==1);
    CHECK(member(member(active,"worst_frame_timings"),"gpu_ms")=="null");
    CHECK(member(active,"prediction_work")=="null");
    recorder.observe(frame(3,1'010'000),controlled());const auto done=recorder.encode();REQUIRE(done);
    const auto pacing=member(*done,"frame_pacing");CHECK(count(pacing,"total_episodes")==1);
    CHECK(member(member(pacing,"episodes"),"recovery_observed")=="true");
    CHECK(member(member(*first,"frame_pacing"),"episodes").find("recording_end_or_still_active")!=std::string::npos);
}

TEST_CASE("E32 session correction snapshots do not replay and rejected errors remain separately observable",
    "[e32][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};auto observed=frame(1,0);observed.raw_correction_units=40;observed.camera_jump_units=5;
    recorder.observe(observed,controlled());
    observed=frame(2,10'000);observed.correction_ordinal=1;observed.raw_correction_units=2;observed.camera_jump_units=6;
    recorder.observe(observed,controlled());
    observed.frame_number=3;observed.monotonic_us=20'000;observed.prediction_active=false;observed.raw_correction_units=80;
    observed.set_fallback_reason("solid_start");recorder.observe(observed,controlled());
    observed.frame_number=4;observed.monotonic_us=30'000;observed.correction_ordinal=4;observed.raw_correction_units=3;
    recorder.observe(observed,controlled());
    auto next_life=controlled();next_life.life_epoch=2;observed.frame_number=5;observed.monotonic_us=40'000;
    recorder.observe(observed,next_life); // The stage-lifetime ordinal is unchanged across respawn.
    observed.frame_number=6;observed.monotonic_us=50'000;observed.correction_ordinal.reset();observed.raw_correction_units=100;
    recorder.observe(observed,next_life);
    const auto statistics=recorder.statistics();CHECK(statistics.correction_samples==2);
    CHECK(statistics.correction_ordinal_advances_unmeasured==2);CHECK(statistics.fallback_entries==1);CHECK(statistics.life_boundaries==1);
    const auto output=recorder.encode();REQUIRE(output);const auto totals=member(*output,"totals");
    CHECK(count(metric(totals,"raw_correction_on_ordinal_sample_units"),"observed")==2);
    CHECK(value(metric(totals,"raw_correction_on_ordinal_sample_units"),"maximum")==3);
    CHECK(count(metric(totals,"raw_error_on_fallback_entry_units"),"observed")==1);
    CHECK(value(metric(totals,"raw_error_on_fallback_entry_units"),"maximum")==80);
    CHECK(output->find("sticky_snapshot_on_ordinal_observation_not_exact_per_correction_delta")!=output->npos);
}

TEST_CASE("E32 session separates uncontrolled and unknown contexts including foreground transition intervals",
    "[e32][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};recorder.observe(frame(1,0),controlled());
    auto background=controlled();background.focused=false;auto observed=frame(2,1'000'000);observed.frame_interval_ms=1000;
    observed.prediction_active=false;recorder.observe(observed,background);
    observed=frame(3,2'000'000);observed.frame_interval_ms=1000;recorder.observe(observed,controlled());
    observed=frame(4,2'010'000);observed.frame_interval_ms=10;recorder.observe(observed,controlled());
    auto dead=controlled();dead.alive=false;observed=frame(5,2'020'000);observed.prediction_active=false;recorder.observe(observed,dead);
    recorder.observe(frame(6,2'030'000),{});
    const auto output=recorder.encode();REQUIRE(output);const auto cohorts=member(*output,"cohorts");
    const auto active=member(cohorts,"controlled_alive");CHECK(count(member(active,"counts"),"frames")==3);
    CHECK(count(member(active,"counts"),"frame_over50_ms")==0);
    CHECK(count(metric(active,"frame_interval_ms"),"observed")==1);
    CHECK(count(metric(active,"frame_interval_ms"),"not_available")==2);CHECK(value(metric(active,"frame_interval_ms"),"maximum")==10);
    CHECK(count(member(member(cohorts,"background"),"counts"),"frames")==1);
    CHECK(count(member(member(cohorts,"inactive_or_dead"),"counts"),"frames")==1);
    CHECK(count(member(member(cohorts,"unknown"),"counts"),"frames")==1);
    CHECK(count(member(member(*output,"totals"),"counts"),"frame_over50_ms")==2);
    CHECK(count(member(*output,"observations"),"frame_interval_context_unattributed")==5);
}

TEST_CASE("E32 session delayed GPU observations keep original time and context through compaction and generation changes",
    "[e32][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};recorder.observe(frame(1,0),controlled());
    auto background=controlled();background.focused=false;
    auto observed=frame(2,600'000'000);observed.network_generation=2;observed.map_generation=3;
    recorder.observe(observed,background);REQUIRE(recorder.observe_gpu(1,7.5));REQUIRE(recorder.observe_gpu(1,99));
    CHECK_FALSE(recorder.observe_gpu(200,3));CHECK_FALSE(recorder.observe_gpu(2,std::numeric_limits<double>::quiet_NaN()));
    observed=frame(3,600'010'000);observed.network_generation=2;observed.map_generation=3;observed.gpu_ms=4;
    recorder.observe(observed,background);REQUIRE(recorder.observe_gpu(3,5));
    const auto stats=recorder.statistics();CHECK(stats.gpu_observations==2);CHECK(stats.gpu_duplicates==2);
    CHECK(stats.gpu_unmatched==1);CHECK(stats.gpu_invalid==1);CHECK(stats.timeline_compactions==1);
    const auto output=recorder.encode();REQUIRE(output);const auto cohorts=member(*output,"cohorts");
    const auto first_gpu=metric(member(cohorts,"controlled_alive"),"gpu_ms");CHECK(count(first_gpu,"observed")==1);
    CHECK(value(first_gpu,"maximum")==7.5);CHECK(count(first_gpu,"not_available")==0);
    const auto background_gpu=metric(member(cohorts,"background"),"gpu_ms");CHECK(count(background_gpu,"observed")==1);
    CHECK(value(background_gpu,"maximum")==4);CHECK(count(background_gpu,"not_available")==1);
    const auto bins=member(member(*output,"timeline"),"bins");
    const auto second=bins.find("\"begin_us\":600000000");REQUIRE(second!=bins.npos);
    const auto first_bin=bins.substr(0,second);CHECK(value(metric(first_bin,"gpu_ms"),"maximum")==7.5);
    for(std::uint64_t i=4;i<=1028;++i)recorder.observe(frame(i,600'010'000+i*1000),controlled());
    CHECK_FALSE(recorder.observe_gpu(1,1));CHECK(recorder.statistics().gpu_observations==2);
}

TEST_CASE("E32 session network counters and correction generations preserve deltas without sticky overcount",
    "[e32][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};auto observed=frame(1,0);observed.datagrams_received=100;observed.rejected_sequences=5;
    observed.choke_markers_total=2;observed.correction_ordinal=7;observed.raw_correction_units=3;recorder.observe(observed,controlled());
    observed.frame_number=2;observed.monotonic_us=10'000;observed.datagrams_received=110;observed.rejected_sequences=6;
    observed.map_generation=3;recorder.observe(observed,controlled());
    observed.frame_number=3;observed.monotonic_us=20'000;observed.datagrams_received.reset();observed.choke_markers_total=3;
    recorder.observe(observed,controlled());
    observed.frame_number=4;observed.monotonic_us=30'000;observed.datagrams_received=115;observed.correction_ordinal=9;
    recorder.observe(observed,controlled());
    observed.frame_number=5;observed.monotonic_us=40'000;observed.network_generation=2;observed.datagrams_received=500;
    observed.correction_ordinal=9;recorder.observe(observed,controlled());
    observed.frame_number=6;observed.monotonic_us=50'000;observed.datagrams_received=2;observed.correction_ordinal=1;
    recorder.observe(observed,controlled());
    const auto stats=recorder.statistics();CHECK(stats.correction_samples==1);CHECK(stats.correction_ordinal_advances_unmeasured==1);
    CHECK(stats.generation_boundaries==2);const auto output=recorder.encode();REQUIRE(output);
    const auto deltas=member(*output,"counter_deltas");const auto datagrams=member(deltas,"datagrams_received");
    CHECK(count(datagrams,"delta")==15);CHECK(count(datagrams,"missing_frames")==1);CHECK(count(datagrams,"baselines")==2);
    CHECK(count(datagrams,"resets")==1);CHECK(count(member(deltas,"rejected_sequences"),"delta")==1);
    CHECK(count(member(deltas,"choke_markers"),"delta")==1);
    CHECK(count(member(*output,"observations"),"correction_ordinal_resets")==1);
    CHECK(output->find("\"rejected_sequence_is_packet_loss\":false")!=output->npos);
}

TEST_CASE("E32 session sparse huge gaps manual markers and populated encoding remain bounded",
    "[e32][movement-session][movement-diagnostics][app][core]") {
    Recorder recorder{true};auto observed=frame(1,100'000);recorder.observe(observed,controlled());
    REQUIRE(recorder.mark_manual(1'100'000)); // Marker-only bucket merged with a real first sample.
    observed=frame(2,512'100'000);recorder.observe(observed,controlled());
    REQUIRE(recorder.mark_manual(512'100'000));CHECK_FALSE(recorder.mark_manual(0));
    auto output=recorder.encode();REQUIRE(output);CHECK(count(member(*output,"observations"),"manual_markers")==2);
    const auto bins=member(member(*output,"timeline"),"bins");CHECK(count(bins,"first_sample_us")==100'000);
    CHECK(count(bins,"first_manual_marker_us")==1'100'000);
    for(std::uint64_t i=3;i<=600;++i){observed=frame(i,i*1'000'000);
        if(observed.monotonic_us<=512'100'000)observed.monotonic_us=512'100'000+i*1000;
        if(i>=513)observed.monotonic_us=512'700'000+i*1'000'000;
        recorder.observe(observed,controlled());}
    observed=frame(601,std::numeric_limits<std::uint64_t>::max()-1);recorder.observe(observed,controlled());
    CHECK(recorder.statistics().timeline_bins<=512);output=recorder.encode();REQUIRE(output);
    CHECK(output->size()<=Recorder::maximum_encoded_bytes);

    Recorder populated{true};std::uint64_t counter{};
    for(std::uint64_t i=0;i<512;++i){observed=frame(i,100'000+i*1'000'000);observed.correction_ordinal=i;
        observed.frame_interval_ms=999999999.0;observed.cpu_update_ms=999999998.0;observed.network_ms=999999997.0;
        observed.parse_commit_ms=999999996.0;observed.prediction_ms=999999995.0;observed.collision_ms=999999994.0;
        observed.studio_ms=999999993.0;observed.asset_ms=999999992.0;observed.render_ms=999999991.0;
        observed.present_ms=999999990.0;observed.report_ms=999999989.0;observed.gpu_ms=999999988.0;
        observed.state_age_ms=999999987.0;observed.raw_correction_units=999999986.0;observed.camera_jump_units=999999985.0;
        observed.replay_duration_ms=999999984.0;observed.fallback_duration_ms=999999983.0;
        observed.prediction_active=(i%2)==0;observed.bytes=std::numeric_limits<std::uint32_t>::max();
        observed.committed_records=std::numeric_limits<std::uint32_t>::max();observed.replayed_commands=std::numeric_limits<std::uint32_t>::max();
        observed.datagrams_received=++counter;observed.rejected_sequences=counter;observed.choke_markers_total=counter;
        populated.observe(observed,controlled());REQUIRE(populated.mark_manual(observed.monotonic_us));}
    const auto dense=populated.encode();REQUIRE(dense);CHECK(dense->size()<=Recorder::maximum_encoded_bytes);
    CHECK(populated.capacity_bytes()<=Recorder::maximum_capacity_bytes);
    std::cout<<"[movement-session] capacity_bytes="<<populated.capacity_bytes()<<" encoded_dense_bytes="<<dense->size()<<'\n';
}
