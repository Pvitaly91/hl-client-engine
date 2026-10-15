#include "local_movement_test_fixture.hpp"
#include "goldsrc_usercmd_test_fixture.hpp"
#include "collision_brush_test_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"

#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/prediction/prediction_history.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <span>

namespace post_effect_fixture {
namespace g=hlclient::goldsrc;
namespace gm=hlclient::goldsrc::movement;
namespace p=hlclient::movement;
namespace pred=hlclient::prediction;
namespace f=hlclient::tests::local_movement;
using V=hlclient::assets::AssetVector3;

void same_origin(const V a,const V b) {
    CHECK(a.x==b.x);CHECK(a.y==b.y);CHECK(a.z==b.z);
}
void velocity_delta(const V a,const V b,const std::array<float,3> delta) {
    CHECK(a.x==Catch::Approx(b.x+delta[0]).margin(.0001));
    CHECK(a.y==Catch::Approx(b.y+delta[1]).margin(.0001));
    CHECK(a.z==Catch::Approx(b.z+delta[2]).margin(.0001));
}
pred::PredictionSessionIdentity session(const p::LocalPlayerMovementState& initial) {
    pred::PredictionSessionIdentity result;
    result.session_generation=2;result.prediction_generation=6;
    result.collision_world_primary=0x1234;result.collision_world_secondary=0x5678;
    result.collision_world_revision=3;result.collision_scene_signature=0x9012;
    result.movement_environment_signature=0x3456;result.movement_config_signature=0x7890;
    result.spawn_initial_state_signature=p::local_player_movement_state_signature(initial);
    return result;
}
std::shared_ptr<const pred::LocalPredictionHistoryState> appended_history(
    const p::LocalPlayerMovementState& before,const p::LocalPlayerMovementState& after,
    const g::GoldSrcUserCmdState& command) {
    const auto initial=pred::LocalPredictionHistoryState::create_initial(before,session(before));
    REQUIRE(initial);
    const pred::PredictedCommandAppend row{
        std::make_shared<const g::GoldSrcUserCmdState>(command),
        std::make_shared<const p::LocalPlayerMovementState>(before),
        std::make_shared<const p::LocalPlayerMovementState>(after)};
    const auto result=pred::append_local_prediction_commands(*initial.history,std::span{&row,1U});
    REQUIRE(result);return result.history;
}
}

TEST_CASE("E32 post movement effect is an immutable bounded command-local sidecar",
          "[e32-post-effect][core-only][local-movement][usercmd]") {
    namespace t=post_effect_fixture;
    const auto original=t::f::make_command(1,20,100,20,1);
    const auto before=original.with_velocity_impulse({10,20,30});REQUIRE(before);
    const auto after=before.state->with_post_velocity_impulse({100,-50,200});REQUIRE(after);
    CHECK(original.velocity_impulse()==std::array<float,3>{});
    CHECK(original.post_velocity_impulse()==std::array<float,3>{});
    CHECK(before.state->post_velocity_impulse()==std::array<float,3>{});
    CHECK(after.state->velocity_impulse()==std::array<float,3>{10,20,30});
    CHECK(after.state->post_velocity_impulse()==std::array<float,3>{100,-50,200});
    CHECK(after.state->buttons()==original.buttons());
    CHECK(after.state->forward_move()==original.forward_move());
    CHECK(after.state->sample_time_nanoseconds()==original.sample_time_nanoseconds());
    CHECK(after.state->sample_duration_nanoseconds()==original.sample_duration_nanoseconds());
    const auto replaced=after.state->with_post_velocity_impulse({0,0,500});REQUIRE(replaced);
    CHECK(after.state->post_velocity_impulse()==std::array<float,3>{100,-50,200});
    CHECK(replaced.state->velocity_impulse()==before.state->velocity_impulse());
    CHECK(replaced.state->post_velocity_impulse()==std::array<float,3>{0,0,500});
    CHECK_FALSE(original.with_post_velocity_impulse({2001,0,0}));
    CHECK_FALSE(original.with_post_velocity_impulse({1500,1500,0}));
    CHECK_FALSE(original.with_post_velocity_impulse({std::numeric_limits<float>::quiet_NaN(),0,0}));
    CHECK_FALSE(original.with_post_velocity_impulse({0,std::numeric_limits<float>::infinity(),0}));
    CHECK(original.with_post_velocity_impulse({2000,0,0}));
}

TEST_CASE("E32 post movement effect cannot move the origin in its firing command",
          "[e32-post-effect][core-only][local-movement][prediction]") {
    namespace t=post_effect_fixture;
    constexpr std::array<float,3> kick{100,-50,200};
    for(const bool grounded:{false,true})for(const unsigned duration:{10U,20U,50U}) {
        CAPTURE(grounded,duration);
        t::f::DeterministicLocalMovementCollision collision(grounded);
        const auto initial=t::f::make_state(grounded ? t::V{0,0,36} : t::V{0,0,128},
            {80,-10,0},grounded ? t::p::PlayerMovementMode::walking : t::p::PlayerMovementMode::airborne);
        const auto command=t::f::make_command(1,static_cast<std::uint8_t>(duration),100,25);
        const auto with=command.with_post_velocity_impulse(kick);REQUIRE(with);
        const auto baseline=t::f::simulate(initial,command,collision);REQUIRE(baseline);
        const auto kicked=t::f::simulate(initial,*with.state,collision);REQUIRE(kicked);
        t::same_origin(kicked.state->origin(),baseline.state->origin());
        t::velocity_delta(kicked.state->velocity(),baseline.state->velocity(),kick);
        CHECK(kicked.state->source_command_sequence()==baseline.state->source_command_sequence());
        CHECK(kicked.state->simulation_time_nanoseconds()==baseline.state->simulation_time_nanoseconds());
        CHECK(kicked.statistics.substep_count==baseline.statistics.substep_count);
        const auto repeated=t::f::simulate(initial,*with.state,collision);REQUIRE(repeated);
        CHECK(repeated.deterministic_state_signature==kicked.deterministic_state_signature);
        const auto legacy=command.with_velocity_impulse(kick);REQUIRE(legacy);
        const auto pre=t::f::simulate(initial,*legacy.state,collision);REQUIRE(pre);
        CHECK(std::hypot(pre.state->origin().x-baseline.state->origin().x,
            pre.state->origin().y-baseline.state->origin().y,
            pre.state->origin().z-baseline.state->origin().z)>.5F);
    }
}

TEST_CASE("E32 pre and post effects compose once and only later commands integrate post velocity",
          "[e32-post-effect][core-only][local-movement][prediction]") {
    namespace t=post_effect_fixture;
    t::f::DeterministicLocalMovementCollision collision(false);
    const auto initial=t::f::make_state({0,0,128},{},t::p::PlayerMovementMode::airborne);
    const auto command=t::f::make_command(1,20);
    const auto pre=command.with_velocity_impulse({10,20,30});REQUIRE(pre);
    const auto both=pre.state->with_post_velocity_impulse({100,-50,200});REQUIRE(both);
    const auto pre_only=t::f::simulate(initial,*pre.state,collision);REQUIRE(pre_only);
    const auto applied=t::f::simulate(initial,*both.state,collision);REQUIRE(applied);
    t::same_origin(applied.state->origin(),pre_only.state->origin());
    t::velocity_delta(applied.state->velocity(),pre_only.state->velocity(),{100,-50,200});
    const auto next=t::f::make_command(2,20);
    CHECK(next.post_velocity_impulse()==std::array<float,3>{});
    const auto next_plain=t::f::simulate(*pre_only.state,next,collision);REQUIRE(next_plain);
    const auto next_applied=t::f::simulate(*applied.state,next,collision);REQUIRE(next_applied);
    CHECK(next_applied.state->origin().x-next_plain.state->origin().x==Catch::Approx(2));
    CHECK(next_applied.state->origin().y-next_plain.state->origin().y==Catch::Approx(-1));
    CHECK(next_applied.state->origin().z-next_plain.state->origin().z==Catch::Approx(4));
    t::velocity_delta(next_applied.state->velocity(),next_plain.state->velocity(),{100,-50,200});
}

TEST_CASE("E32 post movement effect follows a completed world sweep and cannot publish a failed transaction",
          "[e32-post-effect][core-only][local-movement][collision]") {
    namespace t=post_effect_fixture;
    t::f::DeterministicLocalMovementCollision collision(false);collision.add_positive_x_wall(40);
    const auto initial=t::f::make_state({23,0,128},{100,0,0},t::p::PlayerMovementMode::airborne);
    const auto command=t::f::make_command(1,20);
    const auto with=command.with_post_velocity_impulse({100,0,200});REQUIRE(with);
    const auto plain=t::f::simulate(initial,command,collision);REQUIRE(plain);
    const auto kicked=t::f::simulate(initial,*with.state,collision);REQUIRE(kicked);
    CHECK(plain.state->origin().x==Catch::Approx(24));
    CHECK(plain.state->velocity().x==Catch::Approx(0));
    t::same_origin(kicked.state->origin(),plain.state->origin());
    t::velocity_delta(kicked.state->velocity(),plain.state->velocity(),{100,0,200});
    collision.fail_traces();const auto failed=t::f::simulate(initial,*with.state,collision);
    REQUIRE_FALSE(failed);CHECK_FALSE(failed.state);REQUIRE(failed.error);
    CHECK(initial.origin().x==23);CHECK(initial.velocity().x==100);
    CHECK(with.state->post_velocity_impulse()==std::array<float,3>{100,0,200});
}

TEST_CASE("E32 post movement sidecar changes history identity but never public command bytes",
          "[e32-post-effect][core-only][prediction][history][wire]") {
    namespace t=post_effect_fixture;namespace uf=hlclient::test::usercmd_fixture;
    const auto original=t::f::make_command(1,20);
    const auto zero=original.with_post_velocity_impulse({});REQUIRE(zero);
    const auto after=original.with_post_velocity_impulse({100,0,0});REQUIRE(after);
    const auto before=original.with_velocity_impulse({100,0,0});REQUIRE(before);
    const auto initial=t::f::make_state();t::f::DeterministicLocalMovementCollision collision;
    const auto step=t::f::simulate(initial,original,collision);REQUIRE(step);
    // Use identical valid endpoints to isolate hashing of the command sidecar.
    const auto plain_history=t::appended_history(initial,*step.state,original);
    const auto zero_history=t::appended_history(initial,*step.state,*zero.state);
    const auto post_history=t::appended_history(initial,*step.state,*after.state);
    const auto pre_history=t::appended_history(initial,*step.state,*before.state);
    const auto signature=t::pred::local_prediction_history_signature(*plain_history);
    CHECK(signature==t::pred::local_prediction_history_signature(*zero_history));
    CHECK(signature!=t::pred::local_prediction_history_signature(*post_history));
    CHECK(signature!=t::pred::local_prediction_history_signature(*pre_history));
    CHECK(t::pred::local_prediction_history_signature(*pre_history)!=
          t::pred::local_prediction_history_signature(*post_history));
    REQUIRE(post_history->entries().size()==1);
    CHECK(post_history->entries()[0].command()->post_velocity_impulse()==after.state->post_velocity_impulse());
    CHECK(original.post_velocity_impulse()==std::array<float,3>{});
    const auto wire_base=uf::default_state(1),wire_plain=uf::default_state(2);
    const auto wire_post=wire_plain.with_post_velocity_impulse({100,0,400});REQUIRE(wire_post);
    const t::g::GoldSrcUserCmdDeltaCodec codec{{},t::g::GoldSrcUserCmdDeltaCompatibilityProfile::synthetic_usercmd_delta_v1};
    const auto binding=uf::exact_binding();
    const auto ordinary=codec.encode_delta(wire_base,wire_plain,binding);REQUIRE(ordinary);
    const auto modified=codec.encode_delta(wire_base,*wire_post.state,binding);REQUIRE(modified);
    CHECK(modified.delta->bytes==ordinary.delta->bytes);
    CHECK(modified.delta->bit_length==ordinary.delta->bit_length);
    CHECK(modified.delta->meaningful_bit_length==ordinary.delta->meaningful_bit_length);
    t::g::GoldSrcUserCmdDeltaDecodeContext decode;
    decode.bytes=modified.delta->bytes;decode.bit_length=modified.delta->bit_length;
    decode.end_policy=t::g::GoldSrcUserCmdDeltaEndPolicy::require_exact_end;
    decode.command_sequence=wire_plain.command_sequence();
    const auto decoded=codec.decode_delta(wire_base,binding,decode);REQUIRE(decoded);
    CHECK(decoded.state->velocity_impulse()==std::array<float,3>{});
    CHECK(decoded.state->post_velocity_impulse()==std::array<float,3>{});
}

TEST_CASE("E32 reference ladder integrates movement before post effect and replays its immutable command",
          "[e32-post-effect][core-only][prediction][ladder][replay]") {
    namespace t=post_effect_fixture;namespace b=hlclient::tests::collision_brush_fixture;
    namespace gc=hlclient::goldsrc::collision;namespace c=hlclient::client;
    const auto library=gc::build_brush_collision_model_library(b::bounded_brush_package(1,100));REQUIRE(library);
    c::RuntimeClientObservationState observation;
    observation.generation=1;observation.publication_revision=11;
    observation.entity_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{11}};
    c::RuntimePacketEntityObservation entity;
    entity.entity_number=42;entity.model_index=27;entity.solid=0;entity.brush_move_type=7;entity.skin=-16;
    entity.origin={0,0,0};entity.angles={0,0,0};observation.packet_entities.push_back(entity);
    const auto context=t::g::build_reference_brush_collision(library.library,{{27,"*1"}},observation,1);
    REQUIRE(context.scene);REQUIRE(context.ladders.size()==1);
    t::gm::BrushSceneMovementCollision collision{context.scene};t::gm::GoldSrcLocalMovementScratch scratch;
    const auto environment=t::f::make_environment();
    // Explicit project-owned neutral parameters, not hidden game defaults.
    const t::gm::ReferenceLadderMovementPolicy policy{100,.3F,100};
    t::g::ReferencePredictionSeed seed;
    seed.generation=1;seed.record_identity=10;seed.command_boundary=*t::g::GoldSrcUserCmdSequence::create(7);
    seed.origin={.75,0,0};seed.velocity={0,0,0};seed.view_offset={0,0,28};seed.base_velocity={0,0,0};
    seed.move_type=3;seed.gravity_multiplier=seed.friction_multiplier=1;
    t::g::GoldSrcWireUserCmd wire;wire.msec=20;wire.angle_turns[1]=32768;
    wire.buttons=t::g::kReferenceGoldSrcButtonForward;
    const auto initial=t::g::derive_reference_prediction_ground(seed,wire,collision,scratch,{},
        t::p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);REQUIRE(initial.state);
    const auto original=t::g::reference_jump_duck_weapon_use_movement_command(
        *t::g::GoldSrcUserCmdSequence::create(8),wire);REQUIRE(original);
    const auto fired=original.state->with_post_velocity_impulse({100,0,300});REQUIRE(fired);
    const auto plain=t::g::simulate_reference_movement(*initial.state,*original.state,environment,
        collision,scratch,{},nullptr,&context,&policy);REQUIRE(plain);
    const auto kicked=t::g::simulate_reference_movement(*initial.state,*fired.state,environment,
        collision,scratch,{},nullptr,&context,&policy);REQUIRE(kicked);
    CHECK(plain.state->mode()==t::p::PlayerMovementMode::ladder);
    t::same_origin(kicked.state->origin(),plain.state->origin());
    t::velocity_delta(kicked.state->velocity(),plain.state->velocity(),{100,0,300});
    CHECK(kicked.state->mode()==plain.state->mode());
    const auto identity=t::pred::create_prediction_session_identity(1,1,collision,environment,{},*initial.state,
        t::pred::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        t::pred::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);REQUIRE(identity);
    auto history=t::pred::LocalPredictionHistoryState::create_initial(*initial.state,*identity.session).history;REQUIRE(history);
    std::shared_ptr<const t::p::LocalPlayerMovementState> correction;
    for(const unsigned sequence:{8U,9U}) {
        const auto next=t::g::reference_jump_duck_weapon_use_movement_command(
            *t::g::GoldSrcUserCmdSequence::create(sequence),wire);REQUIRE(next);
        const auto command=sequence==9 ? next.state->with_post_velocity_impulse({100,0,300}) :
            next.state->with_post_velocity_impulse({});REQUIRE(command);
        const auto before=history->current_predicted_state();
        const auto step=t::g::simulate_reference_movement(*before,*command.state,environment,
            collision,scratch,{},nullptr,&context,&policy);REQUIRE(step);
        const auto after=std::make_shared<const t::p::LocalPlayerMovementState>(*step.state);
        if(sequence==8)correction=after;
        const t::pred::PredictedCommandAppend row{std::make_shared<const t::g::GoldSrcUserCmdState>(*command.state),
            before,after,step.statistics,t::pred::summarize_prediction_touches(step.touches,false,false)};
        const auto added=t::pred::append_local_prediction_commands(*history,std::span{&row,1U});REQUIRE(added);history=added.history;
    }
    REQUIRE(correction);const auto signature=t::p::local_player_movement_state_signature(*history->current_predicted_state());
    const auto rebased=t::g::rebase_reference_prediction(*history,*correction,environment,collision,scratch,{},nullptr,&context,&policy);
    REQUIRE(rebased.history);CHECK(rebased.replayed_commands==1);
    CHECK(t::p::local_player_movement_state_signature(*rebased.history->current_predicted_state())==signature);
    REQUIRE(rebased.history->entries().size()==1);
    CHECK(rebased.history->entries()[0].command()->post_velocity_impulse()==std::array<float,3>{100,0,300});
}

TEST_CASE("E32 exact upward post-step boundary preserves canonical grounded recoil through rebase",
          "[e32-post-effect][core-only][prediction][reference-ground][replay]") {
    namespace t=post_effect_fixture;
    constexpr auto reference_profile=t::p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2;
    // Reuse the independently authored BSP floor: point z=-128, standing
    // Minkowski plane z=-92. There is no synthetic movement implementation.
    const t::gm::WorldOnlyMovementCollision collision{hlclient::tests::vertical_lift::package()};
    const auto environment=t::f::make_environment();t::gm::GoldSrcLocalMovementScratch scratch;
    t::g::GoldSrcWireUserCmd wire;wire.msec=20;wire.buttons=t::g::kReferenceGoldSrcButtonAttack2;
    t::g::ReferencePredictionSeed initial_seed;
    initial_seed.generation=1;initial_seed.record_identity=10;
    initial_seed.command_boundary=*t::g::GoldSrcUserCmdSequence::create(7);
    initial_seed.origin={0,0,-92};initial_seed.velocity={0,0,0};initial_seed.view_offset={0,0,28};
    initial_seed.base_velocity={0,0,0};initial_seed.flags=1U<<9U;initial_seed.move_type=3;
    initial_seed.gravity_multiplier=initial_seed.friction_multiplier=1;
    const auto initial=t::g::derive_reference_prediction_ground(initial_seed,wire,collision,scratch,{},reference_profile);
    REQUIRE(initial.state);CHECK(initial.state->ground_state().grounded());
    for(const float vertical_kick:{400.0F,1000.0F}) {
        CAPTURE(vertical_kick);
        const auto ordinary=t::g::reference_jump_duck_secondary_weapon_use_movement_command(
            *t::g::GoldSrcUserCmdSequence::create(8),wire);REQUIRE(ordinary);
        const auto fired_command=ordinary.state->with_post_velocity_impulse({0,0,vertical_kick});REQUIRE(fired_command);
        const auto fired=t::g::simulate_reference_movement(*initial.state,*fired_command.state,
            environment,collision,scratch,{});REQUIRE(fired);
        t::same_origin(fired.state->origin(),initial.state->origin());
        REQUIRE(fired.state->ground_state().grounded());CHECK(fired.state->velocity().z==vertical_kick);
        auto canonical=initial_seed;canonical.record_identity=11;canonical.command_boundary=ordinary.state->command_sequence();
        canonical.origin={fired.state->origin().x,fired.state->origin().y,fired.state->origin().z};
        canonical.velocity={fired.state->velocity().x,fired.state->velocity().y,fired.state->velocity().z};
        canonical.old_buttons=fired.state->old_buttons();
        const auto untouched=canonical;
        const auto denied=t::g::derive_reference_prediction_ground(canonical,wire,collision,scratch,{},reference_profile);
        CHECK_FALSE(denied.state);CHECK(denied.status==t::g::ReferencePredictionGroundStatus::ground_flag_disagreement);
        const auto grounded=t::g::derive_reference_prediction_ground(canonical,wire,collision,scratch,{},
            reference_profile,nullptr,&*fired_command.state);REQUIRE(grounded.state);REQUIRE(grounded.trace_evidence);
        CHECK(grounded.state->ground_state().grounded());t::same_origin(grounded.state->origin(),fired.state->origin());
        CHECK(grounded.state->velocity().z==vertical_kick);
        CHECK(canonical.origin==untouched.origin);CHECK(canonical.velocity==untouched.velocity);CHECK(canonical.flags==untouched.flags);
        // Canonical timing comes from the accepted source boundary, as in the
        // application owner. Ground derivation itself never guesses that clock.
        auto timed_info=t::p::local_player_movement_state_create_info(*grounded.state);
        timed_info.simulation_time_nanoseconds=fired.state->simulation_time_nanoseconds();
        timed_info.state_revision=fired.state->state_revision();
        const auto correction=t::p::LocalPlayerMovementState::create(timed_info);REQUIRE(correction);
        const auto identity=t::pred::create_prediction_session_identity(1,1,collision,environment,{},*initial.state,
            t::pred::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
            t::pred::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);REQUIRE(identity);
        auto history=t::pred::LocalPredictionHistoryState::create_initial(*initial.state,*identity.session).history;REQUIRE(history);
        const auto append=[&](const t::g::GoldSrcUserCmdState& command,const t::gm::LocalMovementSimulationResult& step) {
            const t::pred::PredictedCommandAppend row{
                std::make_shared<const t::g::GoldSrcUserCmdState>(command),history->current_predicted_state(),
                std::make_shared<const t::p::LocalPlayerMovementState>(*step.state),step.statistics,
                t::pred::summarize_prediction_touches(step.touches,false,false)};
            const auto added=t::pred::append_local_prediction_commands(*history,std::span{&row,1U});
            REQUIRE(added);history=added.history;
        };
        append(*fired_command.state,fired);
        const auto next_command=t::g::reference_jump_duck_secondary_weapon_use_movement_command(
            *t::g::GoldSrcUserCmdSequence::create(9),wire);REQUIRE(next_command);
        CHECK(next_command.state->post_velocity_impulse()==std::array<float,3>{});
        const auto next=t::g::simulate_reference_movement(*fired.state,*next_command.state,
            environment,collision,scratch,{});REQUIRE(next);
        CHECK_FALSE(next.state->ground_state().grounded());CHECK(next.state->mode()==t::p::PlayerMovementMode::airborne);
        CHECK(next.state->origin().z>fired.state->origin().z);CHECK(next.state->velocity().z<vertical_kick);
        append(*next_command.state,next);
        const auto rebased=t::g::rebase_reference_prediction(*history,*correction.state,
            environment,collision,scratch);REQUIRE(rebased.history);CHECK(rebased.replayed_commands==1);
        CHECK(rebased.raw_position_error==0);
        t::same_origin(rebased.history->current_predicted_state()->origin(),next.state->origin());
        CHECK(rebased.history->current_predicted_state()->velocity().z==next.state->velocity().z);
        CHECK_FALSE(rebased.history->current_predicted_state()->ground_state().grounded());
        CHECK(t::p::local_player_movement_state_signature(*rebased.history->current_predicted_state())==
            t::p::local_player_movement_state_signature(*next.state));
        // Only the exact validated reference value authorizes that one probe.
        const auto denied_with=[&](const t::g::GoldSrcUserCmdState* witness,
            const t::g::GoldSrcWireUserCmd& boundary) {
            const auto result=t::g::derive_reference_prediction_ground(canonical,boundary,collision,scratch,{},
                reference_profile,nullptr,witness);
            CHECK_FALSE(result.state);CHECK(result.status==t::g::ReferencePredictionGroundStatus::ground_flag_disagreement);
        };
        denied_with(&*ordinary.state,wire);
        const auto wrong_sequence=next_command.state->with_post_velocity_impulse({0,0,vertical_kick});REQUIRE(wrong_sequence);
        denied_with(&*wrong_sequence.state,wire);
        const auto synthetic=t::f::make_command(8,20).with_post_velocity_impulse({0,0,vertical_kick});REQUIRE(synthetic);
        denied_with(&*synthetic.state,wire);
        const auto pre_only=ordinary.state->with_velocity_impulse({0,0,vertical_kick});REQUIRE(pre_only);
        denied_with(&*pre_only.state,wire);
        auto changed_wire=wire;changed_wire.angle_turns[1]=1;denied_with(&*fired_command.state,changed_wire);
        const auto strict=t::g::derive_reference_prediction_ground(canonical,wire,collision,scratch,{},
            t::p::GoldSrcMovementCommandProfile::reference_wire_dry_walk_v1,nullptr,&*fired_command.state);
        CHECK_FALSE(strict.state);CHECK(strict.status==t::g::ReferencePredictionGroundStatus::ground_flag_disagreement);
        auto off_floor=canonical;off_floor.origin.z=-80;
        const auto unsupported=t::g::derive_reference_prediction_ground(off_floor,wire,collision,scratch,{},
            reference_profile,nullptr,&*fired_command.state);
        CHECK_FALSE(unsupported.state);CHECK(unsupported.status==t::g::ReferencePredictionGroundStatus::ground_flag_disagreement);
        auto airborne=canonical;airborne.flags=0;
        const auto ungrounded=t::g::derive_reference_prediction_ground(airborne,wire,collision,scratch,{},
            reference_profile,nullptr,&*fired_command.state);REQUIRE(ungrounded.state);
        CHECK_FALSE(ungrounded.state->ground_state().grounded());CHECK_FALSE(ungrounded.trace_evidence);
    }
}
