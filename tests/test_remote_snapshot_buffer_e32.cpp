#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
namespace g=hlclient::goldsrc;
namespace c=hlclient::client;
using State=c::RuntimeClientObservationState;
std::shared_ptr<const State> observation(double time,double x,std::size_t ordinal) {
    auto state=std::make_shared<State>();state->generation=1;state->publication_revision=ordinal;state->server_time_seconds=time;
    const c::RuntimeSubstateMetadata metadata{1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{ordinal,ordinal,1,0,8}};
    state->entity_metadata=state->server_time_metadata=metadata;state->client_metadata.generation=1;
    c::RuntimePacketEntityObservation player;
    player.entity_number=2;player.player_movement_schema=player.ordinary_visual_schema=true;
    player.origin={x,0,0};player.angles={0,0,0};player.model_index=7;player.sequence=0;
    player.frame=32;player.animation_time_seconds=time;player.frame_rate=1;player.gait_sequence=2;
    player.body=0;player.skin=0;player.controllers.fill(0);player.blending.fill(0);
    state->packet_entities.push_back(player);state->canonical_state_hash=c::runtime_observation_canonical_hash(*state);
    REQUIRE(c::valid_runtime_observation(*state));return state;
}
std::shared_ptr<const State> altered(std::shared_ptr<const State> source,auto change) {
    auto value=std::make_shared<State>(*source);change(*value);value->canonical_state_hash=c::runtime_observation_canonical_hash(*value);
    REQUIRE(c::valid_runtime_observation(*value));return value;
}
g::RuntimeEntityInterpolationOptions options() {
    g::RuntimeEntityInterpolationOptions value;value.limits.maximum_snapshot_gap_seconds=.25;
    value.teleport_distance=128;value.no_interpolation_effect_mask=32;return value;
}
constexpr std::array<std::uint32_t,1> players{2};
auto sample(g::RuntimePlayerSnapshotPresentation& owner,const State& source,double now) {
    auto result=owner.sample(source,now,options(),.1,players);
    INFO((result.presentation.error ? result.presentation.error->context : ""));REQUIRE(result);return result;
}
}

TEST_CASE("E32 buffered player clock advances steadily across jittered arrivals and packet bursts",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;auto current=observation(1.04,12.8,2);
    owner.observe(observation(1,0,1));owner.observe(current);
    auto first=sample(owner,*current,10);CHECK(*first.presentation.state->packet_entities[0].origin.x==0);
    double last=first.presentation.sample_seconds,last_legacy=1,legacy_jump=0,arrival_anchor=10;
    std::size_t next=3;constexpr std::array jitter{.012,-.008,.006,-.005,0.0};
    for(std::size_t frame=1;frame<=320;++frame) {
        const double now=10+static_cast<double>(frame)*.005;
        while(next<50 && now>=10+(static_cast<double>(next)-2)*.04+jitter[next%jitter.size()]) {
            const double time=1+(static_cast<double>(next)-1)*.04;
            current=observation(time,320*(time-1),next++);owner.observe(current);arrival_anchor=now;
        }
        const auto immutable=*current;auto value=sample(owner,*current,now);const double time=value.presentation.sample_seconds;
        CHECK(time>=last-1e-9);CHECK(time<=*current->server_time_seconds);
        CHECK(*value.presentation.state->packet_entities[0].origin.x==Catch::Approx(320*(time-1)).margin(1e-6));
        CHECK(*current==immutable);CHECK(owner.history_size()<=16);CHECK(owner.player_count()==1);
        if(frame>100)CHECK(time-last<=.005001);
        const double legacy=*current->server_time_seconds-.04+std::clamp(now-arrival_anchor,0.0,.04);
        if(frame>100)legacy_jump=std::max(legacy_jump,legacy-last_legacy);
        last=time;last_legacy=legacy;
    }
    CHECK(legacy_jump>.01); // Independent negative control: the old packet-restarted clock.
}

TEST_CASE("E32 buffered player startup retains the established two-snapshot 20ms cadence",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;owner.observe(observation(1,0,1));
    const auto current=observation(1.02,20,2);owner.observe(current);
    for(const auto [elapsed,x]:std::array{std::pair{0.0,0.0},std::pair{.005,5.0},std::pair{.01,10.0},
        std::pair{.015,15.0},std::pair{.02,20.0},std::pair{.05,20.0}}) {
        const auto value=sample(owner,*current,100+elapsed);
        CHECK(*value.presentation.state->packet_entities[0].origin.x==Catch::Approx(x).margin(.0001));
        REQUIRE(value.players.size()==1);CHECK(value.players[0].current->entity_metadata.source->record_identity==2);
    }
    CHECK(sample(owner,*current,100.05).presentation.selection_status==g::EntitySnapshotPairSelectionStatus::exact_current);
}

TEST_CASE("E32 buffered player boundaries cannot resurrect a removed or reused slot",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    for(int mode=0;mode<5;++mode) {
        CAPTURE(mode);g::RuntimePlayerSnapshotPresentation owner;const auto first=observation(1,0,1);
        owner.observe(first);REQUIRE(sample(owner,*first,10));auto next=observation(1.04,20,3);
        if(mode==0)owner.observe(altered(observation(1.02,10,2),[](auto& state){state.packet_entities.clear();}));
        if(mode==1)next=altered(next,[](auto& state){state.packet_entities[0].model_index=8;});
        if(mode==2)next=altered(next,[](auto& state){state.packet_entities[0].effects=32;});
        if(mode==3)next=observation(1.04,200,3);
        if(mode==4)owner.observe(observation(1.02,10,2));owner.observe(next);
        const std::array boundaries{g::RuntimePlayerContinuityBoundary{2,3}};
        auto value=owner.sample(*next,10.04,options(),.1,players,
            mode==4 ? std::span<const g::RuntimePlayerContinuityBoundary>{boundaries} : std::span<const g::RuntimePlayerContinuityBoundary>{});
        REQUIRE(value);CHECK(value.presentation.state->packet_entities[0].origin.x==next->packet_entities[0].origin.x);
        REQUIRE(value.players.size()==1);CHECK(value.players[0].current->entity_metadata.source->record_ordinal==3);
        for(double now:{10.045,10.05,10.06})CHECK(sample(owner,*next,now).players[0].current->entity_metadata.source->record_ordinal>=3);
    }
}

TEST_CASE("E32 buffered player equal-time corrections preserve anchors without resetting the session clock",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;owner.observe(observation(1,0,1));
    auto current=observation(1.04,20,2);owner.observe(current);REQUIRE(sample(owner,*current,10));
    current=observation(1.04,30,3);owner.observe(current);const auto corrected=sample(owner,*current,10.01);
    CHECK(corrected.presentation.state->packet_entities[0].origin.x==30);CHECK(owner.history_size()==3);
    current=observation(1.08,40,4);owner.observe(current);const auto after=sample(owner,*current,10.02);
    CHECK(*after.presentation.state->packet_entities[0].origin.x>=30);CHECK(after.players[0].current->entity_metadata.source->record_ordinal>=3);
}

TEST_CASE("E32 railing animation-only same-time publications cannot advance a delayed player position",
    "[e32-remote-buffer][rail-regression][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;owner.observe(observation(1,0,1));
    auto current=altered(observation(1.04,12.8,2),[](auto& state){state.packet_entities[0].origin.z=48;});
    owner.observe(current);REQUIRE(sample(owner,*current,10));
    current=altered(observation(1.04,12.8,3),[](auto& state){
        auto& player=state.packet_entities[0];player.origin.z=48;player.gait_sequence=0;
        player.sequence=3;player.frame=64;player.animation_time_seconds=1.04;
    });
    owner.observe(current);const auto updated=sample(owner,*current,10.01);
    REQUIRE(updated.players.size()==1);
    CHECK_FALSE(updated.players[0].discontinuity);
    CHECK(*updated.presentation.state->packet_entities[0].origin.x==Catch::Approx(3.2).margin(.00001));
    CHECK(*updated.presentation.state->packet_entities[0].origin.z==Catch::Approx(12).margin(.00001));
    CHECK(updated.players[0].sample_seconds==Catch::Approx(1.01));
    CHECK(updated.players[0].current->entity_metadata.source->record_ordinal==2);
    const auto immutable=*current;
    for(double now:{10.015,10.02,10.03}) {
        const auto next=sample(owner,*current,now);
        CHECK(*next.presentation.state->packet_entities[0].origin.x==Catch::Approx(
            320*(next.presentation.sample_seconds-1)).margin(.00001));
        CHECK(next.players[0].sample_seconds==next.presentation.sample_seconds);
    }
    CHECK(*current==immutable);
}

TEST_CASE("E32 buffered player current-list visibility missingness and nonplayer authority remain exact",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;owner.observe(observation(1,0,1));
    auto current=altered(observation(1.04,20,2),[](auto& state){
        auto receiver=state.packet_entities[0];receiver.entity_number=1;receiver.origin.x=777;
        auto ordinary=receiver;ordinary.entity_number=40;ordinary.player_movement_schema=false;ordinary.origin.x=888;
        state.packet_entities.insert(state.packet_entities.begin(),receiver);state.packet_entities.push_back(ordinary);
        state.packet_entities[1].origin.y.reset();state.packet_entities[1].effects=128;
    });owner.observe(current);const auto value=sample(owner,*current,10);
    CHECK(value.presentation.state->packet_entities[0]==current->packet_entities[0]);
    CHECK(value.presentation.state->packet_entities[2]==current->packet_entities[2]);
    CHECK_FALSE(value.presentation.state->packet_entities[1].origin.y);CHECK(value.presentation.state->packet_entities[1].effects==128);
    current=altered(observation(1.08,40,3),[](auto& state){state.packet_entities.clear();});owner.observe(current);
    CHECK(sample(owner,*current,10.04).presentation.state->packet_entities.empty());CHECK(owner.player_count()==0);
}

TEST_CASE("E32 buffered player recovery stays bounded without extrapolation or source reordering",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;auto current=observation(1,0,1);owner.observe(current);REQUIRE(sample(owner,*current,10));
    for(std::size_t ordinal=2;ordinal<40;++ordinal) {
        current=altered(observation(1+.02*static_cast<double>(ordinal-1),static_cast<double>(ordinal),ordinal),[&](auto& state){
            state.entity_metadata.source->record_identity=1000-ordinal;state.server_time_metadata=state.entity_metadata;
        });owner.observe(current);REQUIRE(sample(owner,*current,10+.02*static_cast<double>(ordinal-1)));
    }
    CHECK(owner.history_size()==16);owner.observe(current);CHECK(owner.history_size()==16);
    owner.observe(observation(1,0,1));CHECK(owner.history_size()==16);
    const auto held=sample(owner,*current,10.8);CHECK(held.presentation.sample_seconds<=*current->server_time_seconds);
    CHECK(sample(owner,*current,10.7).presentation.state->packet_entities[0].origin.x==held.presentation.state->packet_entities[0].origin.x);
    CHECK(sample(owner,*current,20).presentation.state->packet_entities[0].origin.x==current->packet_entities[0].origin.x);
    CHECK(owner.history_size()==1);auto invalid=options();invalid.limits.maximum_result_bytes=1;
    CHECK_FALSE(owner.sample(*current,20,invalid,.1,players));constexpr std::array<std::uint32_t,2> duplicate{2,2};
    CHECK_FALSE(owner.sample(*current,20,options(),.1,duplicate));owner.reset();CHECK(owner.history_size()==0);CHECK(owner.player_count()==0);
}

TEST_CASE("E32 buffered player exact delayed endpoint selects the new discrete weapon after clock cancellation",
    "[e32-remote-buffer][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;
    const auto draw=[&](const State& state,double now) {
        auto policy=options();policy.limits.maximum_snapshot_gap_seconds=.2;
        auto value=owner.sample(state,now,policy,.1,players);REQUIRE(value);return value;
    };
    auto first=altered(observation(1,0,1),[](auto& state){state.packet_entities[0].weapon_model_index=21;});
    owner.observe(first);REQUIRE(draw(*first,100));REQUIRE(draw(*first,100.11));
    auto second=altered(observation(1.1,0,2),[](auto& state){state.packet_entities[0].weapon_model_index=22;});
    owner.observe(second);REQUIRE(draw(*second,100.2));REQUIRE(draw(*second,100.31));
    auto third=altered(observation(1.2,0,3),[](auto& state){state.packet_entities[0].weapon_model_index=21;});
    owner.observe(third);REQUIRE(draw(*third,100.4));const auto endpoint=draw(*third,100.51);
    CHECK(endpoint.presentation.sample_seconds==1.2);
    CHECK(endpoint.presentation.state->packet_entities[0].weapon_model_index==21);
}

TEST_CASE("E32 actor diagnostics distinguish a correction floor from another player's shared bracket",
    "[e32-remote-buffer][e32-remote-diagnostics][entity-interpolation][core-only]") {
    namespace v=hlclient::entity_visual;
    const auto with_peer=[](auto state,double x){return altered(state,[&](auto& value){
        auto peer=value.packet_entities.front();peer.entity_number=3;peer.origin.x=x;value.packet_entities.push_back(peer);
    });};
    g::RuntimePlayerSnapshotPresentation owner;const std::array<std::uint32_t,2> selected{2,3};
    owner.observe(with_peer(observation(1,0,1),100));auto current=with_peer(observation(1.04,20,2),120);owner.observe(current);
    REQUIRE(owner.sample(*current,10,options(),.1,selected));
    current=with_peer(observation(1.04,30,3),120);owner.observe(current);
    const auto immutable=*current;
    for(const double now:{10.01,10.02,10.03}) {
        const auto result=owner.sample(*current,now,options(),.1,selected);REQUIRE(result);REQUIRE(result.players.size()==2);
        const auto& corrected=result.players[0];const auto& moving=result.players[1];
        CHECK_FALSE(corrected.discontinuity);CHECK(corrected.previous->entity_metadata.source->record_ordinal==2);
        CHECK(corrected.current->entity_metadata.source->record_ordinal==3);
        CHECK(corrected.diagnostics.decision==v::PlayerPresentationDecision::held_floor);
        CHECK(corrected.diagnostics.floor_record_ordinal==3);
        CHECK(corrected.diagnostics.resolved_floor_record_ordinal==3);
        CHECK(corrected.diagnostics.position_previous_record_ordinal==3);
        CHECK(corrected.diagnostics.position_current_record_ordinal==3);CHECK(corrected.diagnostics.alpha==0);
        CHECK((corrected.diagnostics.floor_reasons&static_cast<std::uint32_t>(v::PlayerContinuityReason::same_time_origin_correction))!=0);
        CHECK((corrected.diagnostics.hold_reasons&static_cast<std::uint32_t>(v::PlayerHoldReason::target_before_floor))!=0);
        if(now==10.01)CHECK(corrected.diagnostics.new_boundary_reasons==static_cast<std::uint32_t>(v::PlayerContinuityReason::same_time_origin_correction));
        else CHECK(corrected.diagnostics.new_boundary_reasons==0);
        CHECK(moving.diagnostics.decision==v::PlayerPresentationDecision::interpolated);
        CHECK(moving.diagnostics.position_previous_record_ordinal==1);
        CHECK(moving.diagnostics.position_current_record_ordinal==2);
        CHECK(moving.diagnostics.floor_record_ordinal==1);CHECK(moving.diagnostics.new_boundary_reasons==0);
        CHECK(moving.diagnostics.hold_reasons==0);
        CHECK(moving.diagnostics.alpha==Catch::Approx((now-10)/.04).margin(1e-8));
        CHECK(*result.presentation.state->packet_entities[0].origin.x==30);
        CHECK(*result.presentation.state->packet_entities[1].origin.x==Catch::Approx(100+500*(now-10)).margin(1e-8));
        CHECK(*current==immutable);
    }
}

TEST_CASE("E32 actor diagnostics retain typed continuity causes without replaying the boundary",
    "[e32-remote-buffer][e32-remote-diagnostics][entity-interpolation][core-only]") {
    namespace v=hlclient::entity_visual;
    constexpr std::array reasons{v::PlayerContinuityReason::removal,v::PlayerContinuityReason::model_changed,
        v::PlayerContinuityReason::nointerp_effect,v::PlayerContinuityReason::teleport_distance,
        v::PlayerContinuityReason::game_boundary,v::PlayerContinuityReason::no_interpolation_option,
        v::PlayerContinuityReason::explicit_discontinuity,v::PlayerContinuityReason::schema_changed};
    for(std::size_t mode=0;mode<reasons.size();++mode) {
        CAPTURE(mode);g::RuntimePlayerSnapshotPresentation owner;const auto first=observation(1,0,1);
        owner.observe(first);REQUIRE(sample(owner,*first,10));auto next=observation(1.04,20,3);
        if(mode==0)owner.observe(altered(observation(1.02,10,2),[](auto& state){state.packet_entities.clear();}));
        if(mode==1)next=altered(next,[](auto& state){state.packet_entities[0].model_index=8;});
        if(mode==2)next=altered(next,[](auto& state){state.packet_entities[0].effects=32;});
        if(mode==3)next=observation(1.04,200,3);
        if(mode==7)owner.observe(altered(observation(1.02,10,2),[](auto& state){state.packet_entities[0].player_movement_schema=false;}));
        owner.observe(next);auto policy=options();if(mode==5)policy.no_interpolation=true;
        if(mode==6)policy.discontinuous_entities=players;
        const std::array boundaries{g::RuntimePlayerContinuityBoundary{2,3}};
        const auto event_span=mode==4 ? std::span<const g::RuntimePlayerContinuityBoundary>{boundaries} : std::span<const g::RuntimePlayerContinuityBoundary>{};
        for(const double now:{10.01,10.02}) {
            const auto result=owner.sample(*next,now,policy,.1,players,event_span);REQUIRE(result);REQUIRE(result.players.size()==1);
            const auto& diagnostic=result.players.front().diagnostics;const auto expected=static_cast<std::uint32_t>(reasons[mode]);
            CHECK((diagnostic.floor_reasons&expected)!=0);CHECK(diagnostic.floor_record_ordinal==3);
            if(now==10.01)CHECK((diagnostic.new_boundary_reasons&expected)!=0);
            else CHECK(diagnostic.new_boundary_reasons==0);
            if(mode==0)CHECK((diagnostic.floor_reasons&static_cast<std::uint32_t>(v::PlayerContinuityReason::reentry))!=0);
            CHECK(*result.presentation.state->packet_entities.front().origin.x==*next->packet_entities.front().origin.x);
        }
    }
}

TEST_CASE("E32 actor diagnostics expose stale history clock recovery and underflow without changing poses",
    "[e32-remote-buffer][e32-remote-diagnostics][entity-interpolation][core-only]") {
    namespace v=hlclient::entity_visual;
    const auto has=[](std::uint32_t value,auto reason){return (value&static_cast<std::uint32_t>(reason))!=0;};
    g::RuntimePlayerSnapshotPresentation owner;auto current=observation(1,0,1);owner.observe(current);
    const auto initial=sample(owner,*current,10);CHECK(has(initial.players[0].diagnostics.clock_reasons,v::PlayerClockReason::initial_anchor));
    for(std::size_t ordinal=2;ordinal<=20;++ordinal) {
        current=observation(1+.005*static_cast<double>(ordinal-1),static_cast<double>(ordinal),ordinal);owner.observe(current);
    }
    const auto stale=sample(owner,*current,10.1);CHECK(owner.history_size()==16);
    CHECK(has(stale.players[0].diagnostics.floor_reasons,v::PlayerContinuityReason::history_stale));
    CHECK(stale.players[0].diagnostics.floor_record_ordinal==20);
    CHECK(*stale.presentation.state->packet_entities[0].origin.x==20);
    const auto underflow=sample(owner,*current,10.2);
    CHECK(has(underflow.players[0].diagnostics.clock_reasons,v::PlayerClockReason::newest_underflow));
    CHECK(*underflow.presentation.state->packet_entities[0].origin.x==20);
    const auto recovered=sample(owner,*current,11);
    CHECK(has(recovered.players[0].diagnostics.clock_reasons,v::PlayerClockReason::application_gap));
    CHECK(has(recovered.players[0].diagnostics.floor_reasons,v::PlayerContinuityReason::clock_reset));
    CHECK(owner.history_size()==1);CHECK(*recovered.presentation.state->packet_entities[0].origin.x==20);
    const auto repeat=sample(owner,*current,11);
    CHECK(repeat.players[0].diagnostics.new_boundary_reasons==0);
    CHECK_FALSE(has(repeat.players[0].diagnostics.clock_reasons,v::PlayerClockReason::application_gap));
    owner.reset();owner.observe(current);const auto explicit_reset=sample(owner,*current,11);
    CHECK(has(explicit_reset.players[0].diagnostics.clock_reasons,v::PlayerClockReason::explicit_reset));
    current=observation(.9,7,21);owner.observe(current);const auto regressed=sample(owner,*current,11.01);
    CHECK(has(regressed.players[0].diagnostics.clock_reasons,v::PlayerClockReason::server_time_regressed));
    CHECK(*regressed.presentation.state->packet_entities[0].origin.x==7);
}

TEST_CASE("E32 actor diagnostics do not label an ordinary schema reset as interpolated",
    "[e32-remote-buffer][e32-remote-diagnostics][entity-interpolation][core-only]") {
    namespace v=hlclient::entity_visual;
    g::RuntimePlayerSnapshotPresentation owner;owner.observe(observation(1,0,1));
    auto current=altered(observation(1.04,20,2),[](auto& state){
        auto& player=state.packet_entities[0];player.ordinary_visual_schema=false;
        player.animation_time_seconds.reset();player.frame_rate.reset();player.gait_sequence.reset();
    });
    owner.observe(current);const auto result=sample(owner,*current,10.01);REQUIRE(result.players.size()==1);
    const auto& diagnostic=result.players[0].diagnostics;
    CHECK(diagnostic.decision==v::PlayerPresentationDecision::held_current);
    CHECK((diagnostic.hold_reasons&static_cast<std::uint32_t>(v::PlayerHoldReason::per_actor_reset))!=0);
    CHECK(diagnostic.position_previous_record_ordinal==2);CHECK(diagnostic.position_current_record_ordinal==2);
    CHECK(diagnostic.alpha==0);CHECK(*result.presentation.state->packet_entities[0].origin.x==20);
}

TEST_CASE("E32 actor diagnostics distinguish the logical continuity floor from its retained snapshot",
    "[e32-remote-buffer][e32-remote-diagnostics][entity-interpolation][core-only]") {
    g::RuntimePlayerSnapshotPresentation owner;auto current=observation(1,0,1);owner.observe(current);
    REQUIRE(sample(owner,*current,10));
    for(std::size_t ordinal=2;ordinal<=20;++ordinal) {
        current=observation(1+.02*static_cast<double>(ordinal-1),static_cast<double>(ordinal),ordinal);owner.observe(current);
        const auto result=sample(owner,*current,10+.02*static_cast<double>(ordinal-1));
        const auto& diagnostic=result.players[0].diagnostics;
        CHECK(diagnostic.floor_record_ordinal==1);
        CHECK(diagnostic.resolved_floor_record_ordinal==(ordinal>16 ? ordinal-15 : 1));
        CHECK(diagnostic.new_boundary_reasons==0);
    }
}

TEST_CASE("E32 actor diagnostic clock underflow ignores exact endpoint cancellation noise",
    "[e32-remote-buffer][e32-remote-diagnostics][entity-interpolation][core-only]") {
    namespace v=hlclient::entity_visual;
    const auto mask=static_cast<std::uint32_t>(v::PlayerClockReason::oldest_underflow)|
        static_cast<std::uint32_t>(v::PlayerClockReason::newest_underflow);
    g::RuntimePlayerSnapshotPresentation owner;owner.observe(observation(1,0,1));
    const auto current=observation(1.04,20,2);owner.observe(current);
    const auto oldest=sample(owner,*current,100);
    CHECK((oldest.players[0].diagnostics.clock_reasons&mask)==0);
    const auto newest=sample(owner,*current,100.04);
    CHECK((newest.players[0].diagnostics.clock_reasons&mask)==0);
    CHECK(newest.players[0].diagnostics.decision==v::PlayerPresentationDecision::exact_current);
    CHECK(*newest.presentation.state->packet_entities[0].origin.x==20);
    const auto underflow=sample(owner,*current,100.0401);
    CHECK((underflow.players[0].diagnostics.clock_reasons&static_cast<std::uint32_t>(v::PlayerClockReason::newest_underflow))!=0);
    CHECK(*underflow.presentation.state->packet_entities[0].origin.x==20);
}

TEST_CASE("Repeated oldest-window eviction cannot pin a remote player clock to packet arrivals",
    "[f990-oldest-underflow][e32-remote-buffer][entity-interpolation][core-only]") {
    namespace v=hlclient::entity_visual;
    constexpr std::array rates{30U,60U,100U,144U,240U};
    constexpr std::array jitter{0.55,1.45,0.8,1.2,1.0,0.65,1.35};
    constexpr auto oldest_bit=static_cast<std::uint32_t>(v::PlayerClockReason::oldest_underflow);
    constexpr auto reset_bits=static_cast<std::uint32_t>(v::PlayerClockReason::mapped_time_drift)|
        static_cast<std::uint32_t>(v::PlayerClockReason::application_gap)|
        static_cast<std::uint32_t>(v::PlayerClockReason::snapshot_gap);
    for(const auto rate:rates)for(const bool jittered:{false,true}) {
        CAPTURE(rate,jittered);
        g::RuntimePlayerSnapshotPresentation owner;
        owner.observe(observation(1.0,0.0,1));
        auto current=observation(1.02,6.4,2);owner.observe(current);
        auto previous=sample(owner,*current,100.0);
        double elapsed=0.0,previous_target=previous.presentation.sample_seconds;
        double maximum_step_ratio=0.0;
        std::size_t frame=0,next_packet=1,checked=0,holds=0,underflows=0,legacy_pins=0;
        while(elapsed<4.0) {
            const double dt=(jittered ? jitter[frame%jitter.size()] : 1.0)/rate;
            elapsed+=dt;++frame;
            while(static_cast<double>(next_packet)/60.0<=elapsed+1e-12) {
                const double arrival=static_cast<double>(next_packet)/60.0;
                // A legal bounded source/local clock phase change, not a
                // teleport or packet loss: all source gaps remain <21 ms.
                // The immutable actor still follows exactly 320 units/s in
                // source time. Once established, the +180 ms phase persists.
                const double phase=.18*std::clamp(arrival-.5,0.0,1.0);
                const double seconds=1.02+arrival+phase;
                current=observation(seconds,320.0*(seconds-1.0),next_packet+2U);
                owner.observe(current);++next_packet;
            }
            const auto immutable=*current;
            const auto value=sample(owner,*current,100.0+elapsed);
            REQUIRE(value.players.size()==1U);
            const auto target=value.presentation.sample_seconds;
            CHECK(target>=previous_target-1e-9);
            CHECK(target<=*current->server_time_seconds);
            CHECK(*value.presentation.state->packet_entities[0].origin.x==
                Catch::Approx(320.0*(target-1.0)).margin(1e-6));
            CHECK(*current==immutable);
            CHECK(owner.history_size()<=g::RuntimePlayerSnapshotPresentation::maximum_history);
            if(elapsed>2.5) {
                ++checked;
                const auto& diagnostics=value.players[0].diagnostics;
                underflows+=(diagnostics.clock_reasons&oldest_bit)!=0U;
                holds+=target-previous_target<1e-9;
                maximum_step_ratio=std::max(maximum_step_ratio,(target-previous_target)/dt);
                CHECK((diagnostics.clock_reasons&reset_bits)==0U);
                CHECK(diagnostics.new_boundary_reasons==0U);
                CHECK_FALSE(value.players[0].discontinuity);
                REQUIRE(owner.effective_delay_seconds());
                CHECK(*owner.effective_delay_seconds()==Catch::Approx(.1).margin(1e-9));
                // Independent negative control: the former immutable anchor
                // requests a time older than the actual bounded history.
                const double old_requested=1.02+elapsed-.1;
                legacy_pins+=old_requested<*owner.observations().front()->server_time_seconds-1e-9;
            }
            previous_target=target;
        }
        REQUIRE(checked>30U);
        CHECK(legacy_pins>checked/2U);
        CHECK(holds==0U);
        CHECK(underflows<checked/4U+1U);
        CHECK(maximum_step_ratio<=1.5+1e-7);
    }
}

TEST_CASE("Oldest clock repair preserves actor boundaries without changing an unaffected peer clock",
    "[f990-oldest-underflow][e32-remote-buffer][entity-interpolation][core-only][negative]") {
    namespace v=hlclient::entity_visual;
    constexpr std::array<std::uint32_t,2> selected{2,3};
    constexpr auto oldest_bit=static_cast<std::uint32_t>(v::PlayerClockReason::oldest_underflow);
    constexpr std::array expected_reasons{v::PlayerContinuityReason::removal,v::PlayerContinuityReason::model_changed,
        v::PlayerContinuityReason::nointerp_effect,v::PlayerContinuityReason::teleport_distance,
        v::PlayerContinuityReason::game_boundary,v::PlayerContinuityReason::explicit_discontinuity};
    const auto pair=[](double seconds,std::size_t ordinal) {
        return altered(observation(seconds,320.0*(seconds-1.0),ordinal),[](auto& state) {
            auto peer=state.packet_entities.front();peer.entity_number=3;peer.origin.y=1000.0;
            state.packet_entities.push_back(peer);
        });
    };
    for(std::size_t mode=0;mode<expected_reasons.size();++mode) {
        CAPTURE(mode);g::RuntimePlayerSnapshotPresentation owner,control;
        owner.observe(pair(1.0,1));control.observe(pair(1.0,1));
        auto current=pair(1.02,2);owner.observe(current);control.observe(current);
        REQUIRE(owner.sample(*current,100.0,options(),.1,selected));
        REQUIRE(control.sample(*current,100.0,options(),.1,selected));
        for(std::size_t packet=1;packet<=180;++packet) {
            const double elapsed=static_cast<double>(packet)/60.0;
            current=pair(1.02+elapsed+.18*std::clamp(elapsed-.5,0.0,1.0),packet+2U);
            owner.observe(current);control.observe(current);
            REQUIRE(owner.sample(*current,100.0+elapsed,options(),.1,selected));
            REQUIRE(control.sample(*current,100.0+elapsed,options(),.1,selected));
        }
        const auto original=pair(4.22,183);
        const auto changed=altered(original,[&](auto& state) {
            if(mode==0)state.packet_entities.erase(state.packet_entities.begin());
            if(mode==1)state.packet_entities.front().model_index=8;
            if(mode==2)state.packet_entities.front().effects=32;
            if(mode==3)*state.packet_entities.front().origin.x+=256.0;
        });
        auto policy=options();if(mode==5)policy.discontinuous_entities=players;
        const std::array boundary{g::RuntimePlayerContinuityBoundary{2,183}};
        owner.observe(changed);control.observe(original);
        const auto actual=owner.sample(*changed,103.02,policy,.1,selected,
            mode==4 ? std::span<const g::RuntimePlayerContinuityBoundary>{boundary} : std::span<const g::RuntimePlayerContinuityBoundary>{});
        const auto expected=control.sample(*original,103.02,options(),.1,selected);
        REQUIRE(actual);REQUIRE(expected);
        CHECK(actual.presentation.sample_seconds==expected.presentation.sample_seconds);
        CHECK(actual.presentation.state->packet_entities.back()==expected.presentation.state->packet_entities.back());
        CHECK(owner.effective_delay_seconds()==control.effective_delay_seconds());
        CHECK(actual.players.back().diagnostics.clock_reasons==expected.players.back().diagnostics.clock_reasons);
        CHECK((actual.players.back().diagnostics.clock_reasons&oldest_bit)==0U);
        if(mode==0) {
            CHECK(actual.presentation.state->packet_entities.size()==1U);
            const auto reentry=pair(4.24,184);owner.observe(reentry);control.observe(reentry);
            const auto returned=owner.sample(*reentry,103.04,options(),.1,selected);
            const auto peer_control=control.sample(*reentry,103.04,options(),.1,selected);
            REQUIRE(returned);REQUIRE(peer_control);REQUIRE(returned.players.size()==2U);
            CHECK(returned.players.front().diagnostics.floor_record_ordinal==184U);
            CHECK(returned.presentation.state->packet_entities.front().origin==reentry->packet_entities.front().origin);
            CHECK(returned.presentation.state->packet_entities.back()==peer_control.presentation.state->packet_entities.back());
            CHECK(returned.presentation.sample_seconds==peer_control.presentation.sample_seconds);
        } else {
            REQUIRE(actual.players.size()==2U);
            CHECK(actual.players.front().discontinuity);
            CHECK((actual.players.front().diagnostics.floor_reasons&static_cast<std::uint32_t>(expected_reasons[mode]))!=0U);
            CHECK(actual.presentation.state->packet_entities.front().origin==changed->packet_entities.front().origin);
        }
    }
}
