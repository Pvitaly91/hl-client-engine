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
app::MovementVelocityStopRecord selected_stop(std::uint64_t ordinal=1U) {
    app::MovementVelocityStopRecord value;
    value.capture_ordinal=ordinal;value.observed_us=990'000;value.command_sample_us=980'000;
    value.network_generation=3;value.map_generation=5;value.life_epoch=2;value.query_world_revision=7;
    value.command_sequence=1289;value.command_msec=20;
    value.simulation_start_ns=1'000'000'000;value.simulation_end_ns=1'020'000'000;
    value.source_record_identity=702;value.source_sequence=717;value.scene_record_identity=702;
    value.scene_revision=702;value.source_server_time_s=1.;value.scene_server_time_s=1.;
    value.source_matches_scene=true;value.replay=true;value.predecessor_decision=2;
    value.history_anchor_command=1288;value.history_latest_command=1289;
    value.before_origin={1,2,36};value.after_origin={1,2.125F,36};
    value.before_velocity={0,320,0};value.query_start=value.before_origin;value.query_end={1,8.4F,36};
    value.trace_end=value.after_origin;value.trace_fraction=.02;value.in_open=true;
    value.hit_kind=0;value.hit_model=0;value.hit_plane=app::MovementStopPlane{{0,-1,0},-2.125,19};
    value.clip_planes[0]=*value.hit_plane;value.clip_plane_count=1;return value;
}
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
TEST_CASE("Selected slide stop evidence owns its query and deduplicates sticky snapshots",
    "[e32][movement-diagnostics][velocity-stop][core][app]") {
    Recorder recorder{true};const auto observed=frame(11,1'000'000);REQUIRE(recorder.observe(observed));
    auto stop=selected_stop();REQUIRE(recorder.observe_velocity_stop(observed,stop));
    CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    stop.before_velocity={999,999,999};stop.hit_plane->normal={1,0,0};stop.clip_planes[0].source_index=777;
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"selected_slide_velocity_stops\":[{\"capture_ordinal\":1")!=output->npos);
    CHECK(output->find("\"before_velocity\":[0,320,0]")!=output->npos);
    CHECK(output->find("\"after_velocity\":[0,0,0]")!=output->npos);
    CHECK(output->find("\"query_purpose\":\"selected_slide\"")!=output->npos);
    CHECK(output->find("\"history_anchor_command\":1288")!=output->npos);
    CHECK(output->find("\"history_latest_command\":1289")!=output->npos);
    CHECK(output->find("\"predecessor_reason\":\"no_active_support\"")!=output->npos);
    CHECK(output->find("\"source_sequence\":717")!=output->npos);
    CHECK(output->find("\"hit_plane\":{\"normal\":[0,-1,0],\"distance\":-2.125,\"source_index\":19}")!=output->npos);
    CHECK(output->find("nested_query_walltime")!=output->npos);
    CHECK(output->find("[999,999,999]")==output->npos);
    CHECK(recorder.statistics().velocity_stops==1);CHECK(recorder.statistics().velocity_stop_duplicates==1);
    CHECK(output->size()<=Recorder::maximum_encoded_bytes);
}
TEST_CASE("Absent selected slide evidence adds no zero-valued report section",
    "[e32][movement-diagnostics][velocity-stop][core][app]") {
    Recorder disabled;
    const auto output=disabled.encode(identity);REQUIRE(output);
    CHECK(output->size()<4096);
    CHECK(output->find("selected_slide_velocity_stops")==output->npos);
    CHECK(output->find("velocity_stop_duplicates")==output->npos);
    CHECK(output->find("velocity_stop_evictions")==output->npos);
    CHECK(output->find("velocity_stop_invalid")==output->npos);
    Recorder enabled{true};REQUIRE(enabled.observe(frame(1,1'000'000)));
    const auto ordinary=enabled.encode(identity);REQUIRE(ordinary);
    CHECK(ordinary->find("selected_slide_velocity_stops")==ordinary->npos);
}
TEST_CASE("Selected slide stop pool keeps the latest bounded events without enlarging the byte budget",
    "[e32][movement-diagnostics][velocity-stop][core][app]") {
    Recorder recorder{true};const auto observed=frame(1,1'000'000);REQUIRE(recorder.observe(observed));
    for(std::size_t i=1;i<=Recorder::maximum_velocity_stops;++i)
        REQUIRE(recorder.observe_velocity_stop(observed,selected_stop(i)));
    const auto excess=selected_stop(Recorder::maximum_velocity_stops+1U);
    REQUIRE(recorder.observe_velocity_stop(observed,excess));
    CHECK_FALSE(recorder.observe_velocity_stop(observed,excess));
    const auto stats=recorder.statistics();CHECK(stats.velocity_stops==Recorder::maximum_velocity_stops);CHECK(stats.velocity_stop_evictions==1);
    CHECK(stats.velocity_stop_duplicates==1);CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    CHECK(Recorder::maximum_recent_frames==961);CHECK(Recorder::maximum_incident_frames==480);
    const auto output=recorder.encode(identity);REQUIRE(output);CHECK(output->size()<=Recorder::maximum_encoded_bytes);
    CHECK(output->find("\"selected_slide_velocity_stops\":[{\"capture_ordinal\":2")!=output->npos);
    CHECK(output->find("\"capture_ordinal\":17")!=output->npos);
    std::cout<<"[selected-slide-stop-bound] record_bytes="<<sizeof(app::MovementVelocityStopRecord)
        <<" reserved_pool_bytes="<<sizeof(app::MovementVelocityStopRecord)*Recorder::maximum_velocity_stops
        <<" retained_capacity_bytes="<<stats.allocated_capacity_bytes<<'\n';
}
TEST_CASE("Selected slide stop diagnostics reject invalid or nonzero successors and are absent when off",
    "[e32][movement-diagnostics][velocity-stop][core][app]") {
    const auto observed=frame(1,1'000'000);auto stop=selected_stop();Recorder disabled;
    CHECK_FALSE(disabled.observe_velocity_stop(observed,stop));CHECK(disabled.statistics().allocated_capacity_bytes==0);
    Recorder recorder{true};REQUIRE(recorder.observe(observed));
    stop.before_velocity[0]=std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    stop=selected_stop(2);stop.after_velocity={0,1,0};CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    stop=selected_stop(3);stop.observed_us=1'000'001;CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    stop=selected_stop(4);stop.clip_plane_count=6;CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    stop=selected_stop(5);stop.simulation_end_ns=stop.simulation_start_ns;
    CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    stop=selected_stop(6);stop.map_generation=6;CHECK_FALSE(recorder.observe_velocity_stop(observed,stop));
    CHECK(recorder.statistics().velocity_stop_invalid==6);CHECK(recorder.statistics().velocity_stops==0);
    CHECK(recorder.recent_frame(0)->prediction_active==true);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"selected_slide_velocity_stops\":[]")!=output->npos);
    stop=selected_stop(7);stop.observed_us.reset();stop.command_sample_us.reset();
    REQUIRE(recorder.observe_velocity_stop(observed,stop));
    const auto unknown_clock=recorder.encode(identity);REQUIRE(unknown_clock);
    CHECK(unknown_clock->find("\"observed_us\":null")!=unknown_clock->npos);
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
TEST_CASE("Alive-life recovery diagnostics preserve candidate and accepted anchors separately",
    "[e3c-life-wait][movement-diagnostics][core][app]") {
    Recorder recorder{true};auto value=frame(1,1'000'000);
    value.prediction_active=false;
    value.anchor_command=40U;value.candidate_anchor_command=42U;
    value.fallback_duration_ms=2'000.0;
    value.life_wait_started_us=980'000U;value.life_wait_duration_ms=20.0;
    value.set_recovery_wait_reason("carrier_before_life_floor");
    REQUIRE(recorder.observe(value));
    const auto* retained=recorder.recent_frame(0);REQUIRE(retained);
    CHECK(retained->anchor_command==40U);CHECK(retained->candidate_anchor_command==42U);
    CHECK(retained->life_wait_duration_ms==20.0);CHECK(retained->fallback_duration_ms==2'000.0);
    CHECK_FALSE(retained->prediction_active.value_or(true));
    value.candidate_anchor_command=999U;value.set_recovery_wait_reason("none");
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"anchor_command\":40")!=output->npos);
    CHECK(output->find("\"candidate_anchor_command\":42")!=output->npos);
    CHECK(output->find("\"candidate_anchor_command\":999")==output->npos);
    CHECK(output->find("\"life_wait_started_us\":980000")!=output->npos);
    CHECK(output->find("\"life_wait_duration_ms\":20")!=output->npos);
    CHECK(output->find("\"recovery_wait_reason\":\"carrier_before_life_floor\"")!=output->npos);
    CHECK(output->find("\"fallback_duration\":2000")!=output->npos);
    CHECK(recorder.statistics().invalid_values==0U);
}
TEST_CASE("Alive-life recovery diagnostics keep unknown values null and reject invalid metadata",
    "[e3c-life-wait][movement-diagnostics][core][app]") {
    Recorder recorder{true};REQUIRE(recorder.observe(frame(1,1'000'000)));
    const auto empty=recorder.encode(identity);REQUIRE(empty);
    CHECK(empty->find("\"life_wait_started_us\":null")!=empty->npos);
    CHECK(empty->find("\"life_wait_duration_ms\":null")!=empty->npos);
    CHECK(empty->find("\"candidate_anchor_command\":null")!=empty->npos);
    CHECK(empty->find("\"recovery_wait_reason\":\"none\"")!=empty->npos);
    auto invalid=frame(2,1'010'000);
    invalid.life_wait_started_us=1'010'001;
    invalid.life_wait_duration_ms=std::numeric_limits<double>::quiet_NaN();
    invalid.candidate_anchor_command=0U;
    invalid.set_recovery_wait_reason("PRIVATE/server text");
    REQUIRE(recorder.observe(invalid));
    const auto* retained=recorder.recent_frame(1);REQUIRE(retained);
    CHECK_FALSE(retained->life_wait_started_us);CHECK_FALSE(retained->life_wait_duration_ms);
    CHECK_FALSE(retained->candidate_anchor_command);CHECK(recorder.statistics().invalid_values==3U);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("PRIVATE/server text")==output->npos);
    CHECK(output->find("\"recovery_wait_reason\":\"rejected_diagnostic\"")!=output->npos);
    Recorder disabled;CHECK_FALSE(disabled.observe(invalid));
    CHECK(disabled.statistics().allocated_capacity_bytes==0U);
}
TEST_CASE("Prediction work diagnostics retain measured calls and reject invalid durations atomically",
    "[prediction-work][movement-diagnostics][core][app]") {
    Recorder recorder{true};auto value=frame(1,1'000'000);
    value.prediction_work=hlclient::core::PredictionWorkDiagnostics{
        1.25,2.5,.75,11U,12U,13U,14U};
    REQUIRE(recorder.observe(value));
    value.prediction_work->command_ms=99.0;
    const auto* retained=recorder.recent_frame(0);REQUIRE(retained);
    REQUIRE(retained->prediction_work);CHECK(retained->prediction_work->command_ms==1.25);
    CHECK(retained->prediction_work->replay_ms==2.5);
    CHECK(retained->prediction_work->collision_query_ms==.75);
    CHECK(retained->prediction_work->trace_queries==11U);
    CHECK(retained->prediction_work->position_queries==12U);
    CHECK(retained->prediction_work->contents_queries==13U);
    CHECK(retained->prediction_work->traversal_steps==14U);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"prediction_work\":{\"command_ms\":1.25,\"replay_ms\":2.5,\"collision_query_ms\":0.75,")!=output->npos);
    CHECK(output->find("\"trace_queries\":11,\"position_queries\":12,\"contents_queries\":13,\"traversal_steps\":14}")!=output->npos);

    for(std::uint64_t index=0;index<3;++index) {
        auto invalid=frame(index+2U,1'010'000U+index*10'000U);
        invalid.prediction_work=hlclient::core::PredictionWorkDiagnostics{1,2,3,4,5,6,7};
        if(index==0U)invalid.prediction_work->command_ms=std::numeric_limits<double>::quiet_NaN();
        if(index==1U)invalid.prediction_work->replay_ms=-1.0;
        if(index==2U)invalid.prediction_work->collision_query_ms=std::numeric_limits<double>::infinity();
        REQUIRE(recorder.observe(invalid));
        const auto* sanitized=recorder.recent_frame(static_cast<std::size_t>(index+1U));REQUIRE(sanitized);
        CHECK_FALSE(sanitized->prediction_work);
    }
    CHECK(recorder.statistics().invalid_values==3U);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    const auto sanitized=recorder.encode(identity);REQUIRE(sanitized);
    CHECK(sanitized->find("\"prediction_work\":null")!=sanitized->npos);
    CHECK(sanitized->size()<=Recorder::maximum_encoded_bytes);
}
TEST_CASE("Static-world frame receipts preserve typed path, per-frame counts and unknowns",
    "[static-world-receipt][movement-diagnostics][core][app]") {
    Recorder recorder{true};auto value=frame(1,1'000'000);
    value.static_world=app::MovementStaticWorldFrameRecord{};
    value.static_world->path=app::MovementStaticWorldPath::prepared_selected;
    value.static_world->fallback_reason=app::MovementStaticWorldFallbackReason::fragmented_batch;
    value.static_world->resource_id=81U;value.static_world->resource_revision=7U;
    value.static_world->considered_blocks=272U;value.static_world->tested_bounds=63U;
    value.static_world->selected_blocks=180U;
    value.static_world->submitted_draws=19U;
    value.static_world->submitted_triangles=1600U;value.static_world->selection_preparation_ms=.125;
    REQUIRE(recorder.observe(value));
    value.static_world->selected_blocks=0U;
    const auto* retained=recorder.recent_frame(0);REQUIRE(retained);REQUIRE(retained->static_world);
    CHECK(retained->static_world->selected_blocks==180U);
    CHECK(retained->static_world->selection_preparation_ms==.125);
    const auto report=recorder.encode(identity);REQUIRE(report);
    CHECK(report->find("\"static_world_receipt\":{\"schema\":\"hlclient.static-world-frame.v1\"")!=report->npos);
    CHECK(report->find("\"static_world\":{\"path\":\"prepared_selected\",\"fallback_reason\":\"fragmented_batch\"")!=report->npos);
    CHECK(report->find("\"selected_blocks\":180")!=report->npos);
    CHECK(report->find("\"tested_bounds\":63")!=report->npos);
    CHECK(report->find("\"submitted_triangles\":1600")!=report->npos);
    CHECK(report->find("\"selection_preparation_ms\":0.125")!=report->npos);
    CHECK(report->find("\"submitted_triangles_semantics\":\"geometry_not_visible_pixels\"")!=report->npos);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    auto missing=frame(2,1'010'000);REQUIRE(recorder.observe(missing));
    const auto unknown=recorder.encode(identity);REQUIRE(unknown);
    CHECK(unknown->find("\"static_world\":null")!=unknown->npos);
    auto invalid=frame(3,1'020'000);invalid.static_world=app::MovementStaticWorldFrameRecord{};
    invalid.static_world->selection_preparation_ms=std::numeric_limits<double>::quiet_NaN();
    REQUIRE(recorder.observe(invalid));
    CHECK_FALSE(recorder.recent_frame(2)->static_world);
    CHECK(recorder.statistics().invalid_values==1U);
    auto contradictory=frame(4,1'030'000);
    contradictory.static_world=app::MovementStaticWorldFrameRecord{};
    contradictory.static_world->path=app::MovementStaticWorldPath::prepared_selected;
    contradictory.static_world->considered_blocks=2U;
    contradictory.static_world->selected_blocks=3U;
    REQUIRE(recorder.observe(contradictory));
    CHECK_FALSE(recorder.recent_frame(3)->static_world);
    CHECK(recorder.statistics().invalid_values==2U);
}
TEST_CASE("Prepared BSP receipts preserve new counters and distinguish ranges, draws and unknowns",
    "[static-world-receipt][bsp-pvs-receipt][movement-diagnostics][core][app]") {
    // Existing diagnostic enum values are stable; new values are appended.
    static_assert(static_cast<std::uint8_t>(app::MovementStaticWorldPath::visible_commands)==4U);
    static_assert(static_cast<std::uint8_t>(app::MovementStaticWorldFallbackReason::fragmented_batch)==4U);
    Recorder recorder{true};auto value=frame(1,1'000'000);
    value.static_world=app::MovementStaticWorldFrameRecord{};
    auto& world=*value.static_world;
    world.path=app::MovementStaticWorldPath::prepared_bsp;
    world.resource_id=81U;world.resource_revision=9U;
    world.considered_surfaces=200U;world.selected_surfaces=80U;
    world.submitted_surfaces=80U;world.submitted_ranges=32U;world.submitted_draws=4U;
    world.bsp_visited_nodes=100U;world.bsp_pvs_rejected_nodes=30U;
    world.bsp_frustum_rejected_nodes=20U;
    world.pvs_rebuilds=0U;world.pvs_cache_hits=1U;world.pvs_fallback_code=0U;
    world.pvs_index_upload_bytes=0U;
    world.prepared_bytes=65536U;world.prepared_allocations=0U;
    world.selection_preparation_ms=.125;world.command_preparation_ms=.0625;world.submission_ms=.25;
    REQUIRE(recorder.observe(value));
    world.prepared_bytes=1U;world.command_preparation_ms=99.0;
    const auto* retained=recorder.recent_frame(0);REQUIRE(retained);REQUIRE(retained->static_world);
    CHECK(retained->static_world->prepared_bytes==65536U);
    CHECK(retained->static_world->command_preparation_ms==.0625);
    const auto output=recorder.encode(identity);REQUIRE(output);
    for(const auto fragment:{"\"path\":\"prepared_bsp\"","\"bsp_visited_nodes\":100",
        "\"bsp_pvs_rejected_nodes\":30","\"bsp_frustum_rejected_nodes\":20",
        "\"pvs_rebuilds\":0","\"pvs_cache_hits\":1","\"pvs_fallback_code\":0","\"pvs_index_upload_bytes\":0","\"prepared_bytes\":65536",
        "\"prepared_allocations\":0","\"submitted_surfaces\":80","\"submitted_ranges\":32",
        "\"submitted_draws\":4","\"command_preparation_ms\":0.0625","\"submission_ms\":0.25"})
        CHECK(output->find(fragment)!=output->npos);
    CHECK(output->find("retained_prepared_resource_bytes_gauge_not_delta")!=output->npos);
    CHECK(output->find("per_frame_prepared_vector_storage_events_not_all_runtime_allocations")!=output->npos);
    CHECK(output->find("cpu_static_world_gl_submission_not_gpu_time")!=output->npos);
    CHECK(output->size()<=Recorder::maximum_encoded_bytes);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);

    Recorder unknowns{true};auto unknown=frame(1,1'000'000);
    unknown.static_world=app::MovementStaticWorldFrameRecord{};
    unknown.static_world->path=app::MovementStaticWorldPath::original_batches;
    unknown.static_world->fallback_reason=app::MovementStaticWorldFallbackReason::bsp_unavailable;
    REQUIRE(unknowns.observe(unknown));
    const auto unknownText=unknowns.encode(identity);REQUIRE(unknownText);
    CHECK(unknownText->find("\"fallback_reason\":\"bsp_unavailable\"")!=unknownText->npos);
    for(const auto fragment:{"\"bsp_visited_nodes\":null","\"pvs_rebuilds\":null","\"pvs_fallback_code\":null","\"pvs_index_upload_bytes\":null",
        "\"prepared_bytes\":null","\"prepared_allocations\":null","\"submitted_surfaces\":null",
        "\"submitted_ranges\":null","\"command_preparation_ms\":null","\"submission_ms\":null"})
        CHECK(unknownText->find(fragment)!=unknownText->npos);
    unknown.frame_number=2U;unknown.monotonic_us+=10'000U;
    unknown.static_world->path=app::MovementStaticWorldPath::prepared_bsp;
    unknown.static_world->fallback_reason=app::MovementStaticWorldFallbackReason::pvs_conservative;
    unknown.static_world->pvs_fallback_code=5U;
    unknown.static_world->pvs_index_upload_bytes=std::uint64_t{1U}<<30U;
    REQUIRE(unknowns.observe(unknown));
    const auto conservative=unknowns.encode(identity);REQUIRE(conservative);
    CHECK(conservative->find("\"fallback_reason\":\"pvs_conservative\"")!=conservative->npos);
    CHECK(conservative->find("\"pvs_fallback_code\":5")!=conservative->npos);
    CHECK(conservative->find("\"pvs_index_upload_bytes\":1073741824")!=conservative->npos);
    CHECK(unknowns.statistics().invalid_values==0U);
}
TEST_CASE("Prepared BSP receipt invalid timings and no-path counters are rejected atomically",
    "[static-world-receipt][bsp-pvs-receipt][movement-diagnostics][core][app]") {
    using World=app::MovementStaticWorldFrameRecord;
    Recorder recorder{true};std::uint64_t sequence=1U;
    const auto reject=[&](const World& world) {
        auto value=frame(sequence,1'000'000U+sequence*10'000U);value.static_world=world;
        REQUIRE(recorder.observe(value));
        const auto* retained=recorder.recent_frame(static_cast<std::size_t>(sequence-1U));
        REQUIRE(retained);CHECK_FALSE(retained->static_world);++sequence;
    };
    for(const auto member:{&World::selection_preparation_ms,&World::command_preparation_ms,&World::submission_ms}) {
        for(const auto duration:{std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),-1.0,1'000'000'001.0}) {
            World world;world.path=app::MovementStaticWorldPath::prepared_bsp;
            world.*member=duration;reject(world);
        }
    }
    // Even a known zero counter must not appear under a none/not-rendered path.
    for(const auto member:{&World::bsp_visited_nodes,&World::bsp_pvs_rejected_nodes,
        &World::bsp_frustum_rejected_nodes,&World::pvs_rebuilds,&World::pvs_cache_hits,&World::pvs_fallback_code,&World::pvs_index_upload_bytes,
        &World::prepared_bytes,&World::prepared_allocations,&World::submitted_surfaces,&World::submitted_ranges}) {
        World world;world.*member=0U;reject(world);
    }
    for(const auto member:{&World::command_preparation_ms,&World::submission_ms}) {
        World world;world.*member=0.0;reject(world);
    }
    World invalidPath;invalidPath.path=static_cast<app::MovementStaticWorldPath>(255U);reject(invalidPath);
    World invalidReason;invalidReason.path=app::MovementStaticWorldPath::prepared_bsp;
    invalidReason.fallback_reason=static_cast<app::MovementStaticWorldFallbackReason>(255U);reject(invalidReason);
    for(const auto code:{6U,255U}) {
        World invalidCode;invalidCode.path=app::MovementStaticWorldPath::prepared_bsp;
        invalidCode.pvs_fallback_code=code;reject(invalidCode);
    }
    for(const auto bytes:{(std::uint64_t{1U}<<30U)+1U,std::numeric_limits<std::uint64_t>::max()}) {
        World invalidUpload;invalidUpload.path=app::MovementStaticWorldPath::prepared_bsp;
        invalidUpload.pvs_index_upload_bytes=bytes;reject(invalidUpload);
    }
    CHECK(recorder.statistics().invalid_values==sequence-1U);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    const auto text=recorder.encode(identity);REQUIRE(text);CHECK(text->size()<=Recorder::maximum_encoded_bytes);
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
    CHECK(stats.total_capacity_bytes==stats.allocated_capacity_bytes+stats.session_capacity_bytes+stats.remote_capacity_bytes+stats.multiplayer_capacity_bytes);
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
TEST_CASE("E32 manual incident preserves available temporal coverage with explicit background sampling", "[e32][app][movement-diagnostics][core]") {
    Recorder recorder{true};REQUIRE(recorder.enabled());
    for(std::uint64_t i=0;i<=250;++i){REQUIRE(recorder.observe(frame(i,i*10'000)));
        if(i==150)REQUIRE(recorder.mark(Trigger::manual,i*10'000));}
    REQUIRE(recorder.incidents().size()==1);const auto& incident=recorder.incidents().front();
    CHECK(incident.trigger_us==1'500'000);CHECK(incident.end_us==2'500'000);CHECK(incident.complete);
    CHECK_FALSE(incident.generation_boundary);CHECK(incident.omitted_samples==50);
    CHECK(incident.background_samples_omitted==50);CHECK(incident.exact_samples_omitted==0);
    CHECK(incident.requested_pre_us==4'000'000);CHECK(incident.sampling_interval_us==12'500);
    REQUIRE(incident.frames.size()==201);CHECK(incident.frames.front().monotonic_us==0);
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
    REQUIRE(recorder.mark(Trigger::manual,1'000'000));REQUIRE(recorder.observe(frame(11,1'020'000)));
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
TEST_CASE("71b97 GPU pass observations retain frame identity and explicit unavailable states", "[71b97][app][movement-diagnostics][core]") {
    Recorder recorder{true};auto first=frame(10,1'000'000);
    first.gpu_passes.status=app::MovementGpuPassStatus::pending;
    REQUIRE(recorder.observe(first));REQUIRE(recorder.mark(Trigger::manual,first.monotonic_us));
    auto second=frame(11,1'020'000);second.gpu_passes.status=app::MovementGpuPassStatus::unsupported;
    REQUIRE(recorder.observe(second));
    app::MovementGpuPassRecord passes{{1.F,2.F,3.F,4.F,5.F,6.F},0x3fU,app::MovementGpuPassStatus::ready};
    passes.world_subpass_milliseconds={0.1F,0.2F,0.3F,0.4F};
    passes.valid_world_subpass_mask=0x0fU;
    REQUIRE(recorder.observe_gpu_passes(10,passes));
    CHECK(recorder.recent_frame(0)->gpu_passes.milliseconds[2]==3.F);
    CHECK(recorder.recent_frame(0)->gpu_passes.world_subpass_milliseconds[2]==0.3F);
    CHECK(recorder.incidents().front().frames[0].gpu_passes.valid_mask==0x3fU);
    CHECK(recorder.incidents().front().frames[0].gpu_passes.valid_world_subpass_mask==0x0fU);
    CHECK(recorder.recent_frame(1)->gpu_passes.status==app::MovementGpuPassStatus::unsupported);
    passes.milliseconds[2]=99.F;REQUIRE(recorder.observe_gpu_passes(10,passes));
    CHECK(recorder.recent_frame(0)->gpu_passes.milliseconds[2]==3.F);
    CHECK_FALSE(recorder.observe_gpu_passes(999,passes));
    passes.milliseconds[0]=std::numeric_limits<float>::infinity();
    CHECK_FALSE(recorder.observe_gpu_passes(10,passes));
    passes={{},0x80U,app::MovementGpuPassStatus::ready};
    CHECK_FALSE(recorder.observe_gpu_passes(10,passes));
    passes={{},1U,app::MovementGpuPassStatus::pending};
    CHECK_FALSE(recorder.observe_gpu_passes(10,passes));
    passes={{1.F},1U,app::MovementGpuPassStatus::ready};
    passes.valid_world_subpass_mask=0x10U;
    CHECK_FALSE(recorder.observe_gpu_passes(10,passes));
    passes.valid_world_subpass_mask=1U;
    passes.world_subpass_milliseconds[0]=std::numeric_limits<float>::infinity();
    CHECK_FALSE(recorder.observe_gpu_passes(10,passes));
    const auto text=recorder.encode(identity);REQUIRE(text);
    CHECK(text->find("\"gpu_passes\":{\"status\":\"ready\",\"valid_mask\":63,\"world\":1")!=text->npos);
    CHECK(text->find("\"world_subpasses\":{\"valid_mask\":15,\"target_setup_clear\":0.1")!=text->npos);
    CHECK(text->find("\"status\":\"unsupported\",\"valid_mask\":0,\"world\":null")!=text->npos);
    CHECK(recorder.statistics().allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    REQUIRE(recorder.observe(frame(12,6'000'000)));
    app::MovementGpuPassRecord expired{{},0,app::MovementGpuPassStatus::disjoint};
    REQUIRE(recorder.observe_gpu_passes(10,expired));
    CHECK(recorder.incidents().front().frames[0].gpu_passes.status==app::MovementGpuPassStatus::ready);
    CHECK(recorder.recent_frame(0)->gpu_passes.status==app::MovementGpuPassStatus::not_observed);
    Recorder disabled;CHECK_FALSE(disabled.observe_gpu_passes(10,expired));
    CHECK(disabled.statistics().allocated_capacity_bytes==0);
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
    CHECK(recorder.statistics().manual_reservation_suppressions==0);
    CHECK(recorder.statistics().priority_replacements==6);
    time+=500'000;REQUIRE(recorder.observe(frame(++number,time)));
    REQUIRE(recorder.mark(Trigger::manual,time));CHECK(recorder.incidents().size()==7);
    CHECK(recorder.incidents().back().manual_marker_us[0]==time);
    time+=3'000'000;REQUIRE(recorder.observe(frame(++number,time)));
    REQUIRE(recorder.mark(Trigger::manual,time));CHECK(recorder.incidents().size()==Recorder::maximum_incidents);
    CHECK(recorder.incidents().back().manual_marker_us[0]==time);
    const auto output=recorder.encode(identity);REQUIRE(output);
    CHECK(output->find("\"manual_reserved_incidents\":2")!=output->npos);
    CHECK(output->find("\"automatic_incident_limit\":6")!=output->npos);
    CHECK(output->find("\"manual_reservation_suppressions\":0")!=output->npos);
    CHECK(output->find("\"priority_replacements\":6")!=output->npos);
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
    CHECK(stats.background_samples_omitted>0);CHECK(stats.allocated_capacity_bytes<=Recorder::maximum_capacity_bytes);
    CHECK(Recorder::maximum_recent_frames==961U);CHECK(Recorder::maximum_incident_frames==480U);
    CHECK(Recorder::incident_frame_capacity<=Recorder::maximum_incident_frames);
    CHECK(Recorder::reserved_non_incident_capacity_bytes+
        Recorder::maximum_incidents*Recorder::incident_frame_capacity*sizeof(app::MovementFrameRecord)<=Recorder::maximum_capacity_bytes);
    for(const auto& incident:recorder.incidents()){
        CHECK(incident.complete);CHECK(incident.frames.size()<=Recorder::incident_frame_capacity);
        CHECK(incident.frames.capacity()<=Recorder::incident_frame_capacity);
    }
    const auto output=recorder.encode(identity);REQUIRE(output);CHECK(output->size()<=Recorder::maximum_encoded_bytes);
    CHECK(output->find("\"incident_frame_capacity\":"+std::to_string(Recorder::incident_frame_capacity))!=output->npos);
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
