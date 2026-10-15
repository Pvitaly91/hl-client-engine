#include "convex_corner_test_fixture.hpp"
#include "local_movement_test_fixture.hpp"
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <hlclient/prediction/prediction_visual_correction.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {
namespace corner=hlclient::tests::convex_corner;
namespace movement=hlclient::goldsrc::movement;
namespace player=hlclient::movement;
namespace goldsrc=hlclient::goldsrc;
namespace prediction=hlclient::prediction;
namespace fixture=hlclient::tests::local_movement;
double distance(hlclient::assets::AssetVector3 a,hlclient::assets::AssetVector3 b) {
    return std::hypot(static_cast<double>(a.x)-b.x,static_cast<double>(a.y)-b.y,static_cast<double>(a.z)-b.z);
}
const std::array<movement::ServerEntityCollisionBox,3> remote_actor_boxes{{
    {71,{{-220,-220,0},{-204,-204,72}}},
    {72,{{-188,-220,0},{-172,-204,72}}},
    {73,{{-220,-188,0},{-204,-172,72}}}}};
std::shared_ptr<const goldsrc::collision::BrushCollisionScene> world_scene(
    const std::shared_ptr<const hlclient::collision::CollisionWorldPackage>& package) {
    const auto library=goldsrc::collision::build_brush_collision_model_library(package);if(!library)return {};
    hlclient::client::RuntimeClientObservationState observed;observed.generation=1;observed.publication_revision=1;
    observed.entity_metadata={1,hlclient::client::RuntimeObservationFreshness::observed_in_record,
        hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,hlclient::client::RuntimeObservationSource{1,1,0,0,8}};
    return goldsrc::build_reference_brush_collision(library.library,{},observed,1).scene;
}
}
TEST_CASE("E321 finite external corner preserves reference tangential progress and immutable replay",
    "[e321][convex-corner][movement][prediction][literal-bsp][no-stock]") {
    bool remote_actors=false;
    SECTION("no remote collision actors") {}
    SECTION("three remote actors remain present while commands include IN_ATTACK") {remote_actors=true;}
    const auto environment=fixture::make_environment();
    for(const bool hole:{false,true})for(const bool duck:{false,true})
    for(const bool mirrored:{false,true})for(const unsigned route:{0U,1U,2U}) {
        CAPTURE(hole,duck,mirrored,route,remote_actors);
        const auto package=corner::package(hole);REQUIRE(package);
        movement::WorldOnlyMovementCollision world_collision{package};movement::GoldSrcLocalMovementScratch scratch;
        const auto scene=world_scene(package);REQUIRE(scene);
        movement::BrushSceneMovementCollision actor_collision{scene,remote_actor_boxes};
        const movement::ILocalMovementCollision& collision=remote_actors?
            static_cast<const movement::ILocalMovementCollision&>(actor_collision):world_collision;
        const auto hull=duck?player::PlayerMovementHull::ducked:player::PlayerMovementHull::standing;
        const auto edge=route==2?-40.0F:-16.0625F;
        const hlclient::assets::AssetVector3 origin=mirrored?hlclient::assets::AssetVector3{48,edge,duck?18.0625F:36.0625F}:
            hlclient::assets::AssetVector3{edge,48,duck?18.0625F:36.0625F};
        const auto initial=corner::seed(collision,scratch,origin,hull);REQUIRE(initial);
        const auto identity=prediction::create_prediction_session_identity(1,1,collision,environment,{},*initial,
            prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
            prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);REQUIRE(identity);
        auto history=prediction::LocalPredictionHistoryState::create_initial(*initial,*identity.session).history;REQUIRE(history);
        bool turned=false;std::uint64_t zero_progress{},contacts{};double traveled{};
        std::shared_ptr<const player::LocalPlayerMovementState> correction;
        for(std::uint32_t command_index=0;command_index<64;++command_index) {
            const auto before=history->current_predicted_state();
            if((mirrored?before->origin().x:before->origin().y)<-24.0F)turned=true;
            const double yaw=turned?(mirrored?90.0:0.0):(mirrored?180.0:270.0);
            const auto side=route==1?static_cast<std::int16_t>(mirrored?(turned?-160:160):(turned?160:-160)):std::int16_t{};
            const auto command=corner::command(8+command_index,yaw,320,side,duck,command_index%3==0);REQUIRE(command);
            const auto moved=goldsrc::simulate_reference_movement(*before,*command.state,environment,collision,scratch,{});
            INFO((moved.error?moved.error->context:"ready"));REQUIRE(moved);REQUIRE(moved.state);
            const auto delta=distance(before->origin(),moved.state->origin());traveled+=delta;if(delta<.001)++zero_progress;
            contacts+=moved.statistics.collision_hit_count;
            const auto clear=collision.test_position(moved.state->origin(),hull,scratch.collision);REQUIRE(clear);
            CHECK(clear.result->status==movement::LocalMovementPositionStatus::free);
            CHECK(moved.state->hull()==hull);
            const auto after=std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
            const prediction::PredictedCommandAppend append{std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),before,after,
                moved.statistics,prediction::summarize_prediction_touches(moved.touches,false,false)};
            const auto next=prediction::append_local_prediction_commands(*history,std::span{&append,1U});REQUIRE(next);history=next.history;
            if(command_index==61)correction=after;
        }
        REQUIRE(turned);CHECK(traveled>80.0);CHECK(zero_progress==0);
        CHECK((mirrored?history->current_predicted_state()->origin().y:history->current_predicted_state()->origin().x)>0);
        REQUIRE(correction);const auto before_replay=history->current_predicted_state();
        const auto replay=goldsrc::rebase_reference_prediction(*history,*correction,environment,collision,scratch);REQUIRE(replay.history);
        CHECK(replay.raw_position_error==Catch::Approx(0).margin(.001));CHECK(replay.replayed_commands==2);
        CHECK(distance(before_replay->origin(),replay.history->current_predicted_state()->origin())<.001);
        CHECK(before_replay->velocity().x==replay.history->current_predicted_state()->velocity().x);
        CHECK(before_replay->velocity().y==replay.history->current_predicted_state()->velocity().y);
        CHECK(before_replay->velocity().z==replay.history->current_predicted_state()->velocity().z);
        std::cout<<"[e321-corner-control] remote_actors="<<remote_actors<<" hole="<<hole<<" duck="<<duck<<" mirrored="<<mirrored<<" route="<<route
            <<" distance="<<traveled<<" zero_progress="<<zero_progress<<" contacts="<<contacts<<'\n';
    }
}
TEST_CASE("E321 small matched-boundary correction has valid outer-corner endpoints but an invalid chord",
    "[e321][convex-corner][prediction][presentation][literal-bsp][no-stock]") {
    const auto package=corner::package();REQUIRE(package);
    movement::WorldOnlyMovementCollision collision{package};movement::GoldSrcLocalMovementScratch scratch;
    const auto environment=fixture::make_environment();
    const auto initial=corner::seed(collision,scratch,{-16.0625F,-15.0F,36.0625F},player::PlayerMovementHull::standing,{0,-100,0});REQUIRE(initial);
    const auto identity=prediction::create_prediction_session_identity(1,1,collision,environment,{},*initial,
        prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);REQUIRE(identity);
    auto history=prediction::LocalPredictionHistoryState::create_initial(*initial,*identity.session).history;REQUIRE(history);
    const auto command=corner::command(8,270,100,0);REQUIRE(command);
    const auto moved=goldsrc::simulate_reference_movement(*initial,*command.state,environment,collision,scratch,{});REQUIRE(moved);REQUIRE(moved.state);
    const auto post=std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
    const prediction::PredictedCommandAppend append{std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),history->current_predicted_state(),post,
        moved.statistics,prediction::summarize_prediction_touches(moved.touches,false,false)};
    const auto next=prediction::append_local_prediction_commands(*history,std::span{&append,1U});REQUIRE(next);history=next.history;
    auto corrected=player::local_player_movement_state_create_info(*post);corrected.origin.x=-15.875F;
    const auto corrected_state=player::LocalPlayerMovementState::create(corrected);REQUIRE(corrected_state);
    const auto replay=goldsrc::rebase_reference_prediction(*history,*corrected_state.state,environment,collision,scratch);REQUIRE(replay.history);
    REQUIRE(replay.raw_position_error);CHECK(*replay.raw_position_error==Catch::Approx(.1875).margin(.001));
    CHECK(*replay.raw_position_error<.25);CHECK(replay.replayed_commands==0);
    const auto& endpoint=*replay.history->current_predicted_state();
    for(const auto& position:{initial->origin(),endpoint.origin()}) {
        const auto clear=collision.test_position(position,player::PlayerMovementHull::standing,scratch.collision);REQUIRE(clear);
        CHECK(clear.result->status==movement::LocalMovementPositionStatus::free);
    }
    const auto chord=goldsrc::sample_reference_support_presentation(*initial,endpoint,.375,nullptr,scratch.collision);
    const auto linear=collision.test_position(chord.origin,player::PlayerMovementHull::standing,scratch.collision);REQUIRE(linear);
    CHECK(linear.result->status==movement::LocalMovementPositionStatus::blocking);
    const auto swept=collision.trace_hull(initial->origin(),chord.origin,player::PlayerMovementHull::standing,scratch.collision);REQUIRE(swept);
    CHECK(swept.result->fraction<1);CHECK_FALSE(swept.result->start_solid);
    // Independent complete outside route: no wall penetration, no hole crossing.
    const hlclient::assets::AssetVector3 bend{-16.0625F,-16.0625F,initial->origin().z};
    for(const auto& segment:std::array{std::pair{initial->origin(),bend},std::pair{bend,endpoint.origin()}}) {
        const auto clear=collision.trace_hull(segment.first,segment.second,player::PlayerMovementHull::standing,scratch.collision);REQUIRE(clear);
        CHECK(clear.result->fraction==1);CHECK_FALSE(clear.result->start_solid);CHECK_FALSE(clear.result->all_solid);
    }
    std::cout<<"[e321-corner-chord] raw_error="<<*replay.raw_position_error<<" candidate_blocked="
        <<(linear.result->status==movement::LocalMovementPositionStatus::blocking)<<" trace_fraction="<<swept.result->fraction<<'\n';
}

TEST_CASE("E321 bounded exterior sampling validates full body legs and refuses void traps and blockers",
    "[e321][convex-corner][prediction][presentation][literal-bsp][no-stock]") {
    bool remote_actors=false;
    SECTION("world-only render path") {}
    SECTION("three remote actors stay in the collision scene for all render cadences") {remote_actors=true;}
    const auto package=corner::package();REQUIRE(package);movement::WorldOnlyMovementCollision world_collision{package};
    const auto scene=world_scene(package);REQUIRE(scene);movement::BrushSceneMovementCollision actor_collision{scene,remote_actor_boxes};
    const movement::ILocalMovementCollision& collision=remote_actors?
        static_cast<const movement::ILocalMovementCollision&>(actor_collision):world_collision;
    hlclient::collision::CollisionQueryScratch scratch;
    const hlclient::assets::AssetVector3 from{-16.0625F,-15,36.0625F},to{-15.875F,-17,36.0625F};
    for(const auto hull:{player::PlayerMovementHull::standing,player::PlayerMovementHull::ducked}) {
        auto a=from,b=to;if(hull==player::PlayerMovementHull::ducked)a.z=b.z=18.0625F;
        for(const double alpha:{0.0,.025,.05,.1,.375,.5,.9,1.0}) {
            std::size_t queries{};const auto route=goldsrc::sample_reference_exterior_corner_presentation(a,b,alpha,hull,collision,scratch,{},4,&queries);
            REQUIRE(route);CHECK(route->route_length<=4);CHECK(queries==8);
            const auto clear=collision.test_position(route->sampled_origin,hull,scratch);REQUIRE(clear);
            CHECK(clear.result->status==movement::LocalMovementPositionStatus::free);
            CHECK(distance(route->sampled_origin,a)<=route->route_length+.001);
            if(alpha==0)CHECK(distance(route->sampled_origin,a)==0);
            if(alpha==1)CHECK(distance(route->sampled_origin,b)<.00001);
        }
        for(const double fps:{30.0,60.0,100.0,144.0,240.0,0.0}) {
            std::vector<double> samples{0};
            if(fps==0)samples={0,.001,.003,.004,.009,.014,.019,.020};
            else for(double seconds=1.0/fps;seconds<.025;seconds+=1.0/fps)samples.push_back(seconds);
            samples.push_back(.025);
            for(const auto seconds:samples) {
                CAPTURE(remote_actors,hull,fps,seconds);
                const auto alpha=std::min(1.0,seconds/.020);
                const auto route=goldsrc::sample_reference_exterior_corner_presentation(a,b,alpha,hull,collision,scratch);REQUIRE(route);
                const auto clear=collision.test_position(route->sampled_origin,hull,scratch);REQUIRE(clear);
                CHECK(clear.result->status==movement::LocalMovementPositionStatus::free);
                if(seconds>=.020)CHECK(distance(route->sampled_origin,b)<.00001);
            }
        }
    }
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(from,to,-.01,player::PlayerMovementHull::standing,collision,scratch));
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(from,to,1.01,player::PlayerMovementHull::standing,collision,scratch));
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(from,to,.5,player::PlayerMovementHull::standing,collision,scratch,{},.5));
    auto invalid=from;invalid.x=std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(invalid,to,.5,player::PlayerMovementHull::standing,collision,scratch));
    const auto void_package=corner::package(true,-48);REQUIRE(void_package);movement::WorldOnlyMovementCollision empty_floor{void_package};
    const hlclient::assets::AssetVector3 void_a{-64.0625F,-63,36.0625F},void_b{-63.875F,-65,36.0625F};
    for(const auto& p:{void_a,void_b}) {const auto free=empty_floor.test_position(p,player::PlayerMovementHull::standing,scratch);REQUIRE(free);
        CHECK(free.result->status==movement::LocalMovementPositionStatus::free);}
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(void_a,void_b,.5,player::PlayerMovementHull::standing,empty_floor,scratch));
    const auto trap_package=corner::package(false,0,true);REQUIRE(trap_package);movement::WorldOnlyMovementCollision trap{trap_package};
    const auto blocked=trap.test_position(to,player::PlayerMovementHull::standing,scratch);REQUIRE(blocked);
    CHECK(blocked.result->status==movement::LocalMovementPositionStatus::blocking);
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(from,to,.5,player::PlayerMovementHull::standing,trap,scratch));
    const auto library=hlclient::goldsrc::collision::build_brush_collision_model_library(package);REQUIRE(library);
    hlclient::client::RuntimeClientObservationState observed;observed.generation=1;observed.publication_revision=1;
    observed.entity_metadata={1,hlclient::client::RuntimeObservationFreshness::observed_in_record,
        hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,hlclient::client::RuntimeObservationSource{1,1,0,0,8}};
    const auto context=goldsrc::build_reference_brush_collision(library.library,{},observed,1);REQUIRE(context.scene);
    const std::array boxes{movement::ServerEntityCollisionBox{77,{{-32,-31.6F,0},{-31.9F,-31.4F,72}}}};
    movement::BrushSceneMovementCollision with_blocker{context.scene,boxes};
    for(const auto& p:{from,to}) {const auto free=with_blocker.test_position(p,player::PlayerMovementHull::standing,scratch);REQUIRE(free);
        CHECK(free.result->status==movement::LocalMovementPositionStatus::free);}
    CHECK_FALSE(goldsrc::sample_reference_exterior_corner_presentation(from,to,.5,player::PlayerMovementHull::standing,with_blocker,scratch));
}

TEST_CASE("E321 validated camera publication retains correction identity and rejects stale generations and times",
    "[e321][convex-corner][prediction][visual-correction][core-api]") {
    namespace camera=hlclient::gameplay_camera;
    camera::GameplayCameraStateCreateInfo info;info.position={1,2,64};info.mode=camera::GameplayCameraMode::player_walk;info.revision=2;
    const auto base=camera::GameplayCameraState::create(info);REQUIRE(base);
    prediction::PredictionVisualCorrectionConfig config;config.duration_seconds=.020;config.maximum_offset_magnitude=4;
    const auto created=prediction::begin_prediction_visual_correction({}, {0,3,64},info.position,
        prediction::PredictionCorrectionClass::small_visual_correction,10,7,1,2,config);REQUIRE(created);
    const hlclient::assets::AssetVector3 actual{.25F,2.75F,64};
    const auto published=prediction::publish_validated_prediction_visual_camera(*created.correction,*base.state,actual,10.005,2,7);REQUIRE(published);
    CHECK(published.correction->start_monotonic_time_seconds()==10);CHECK(published.correction->duration_seconds()==.020);
    CHECK(published.correction->source_authority_update_ordinal()==7);CHECK(published.correction->new_prediction_revision()==2);
    CHECK(published.correction->initial_position_offset().x==-1);CHECK(published.correction->current_residual_offset().x==-.75F);
    REQUIRE(published.correction->last_published_camera());CHECK(published.correction->last_published_camera()->position().x==actual.x);
    CHECK_FALSE(prediction::publish_validated_prediction_visual_camera(*created.correction,*base.state,actual,9.9,2,7));
    CHECK_FALSE(prediction::publish_validated_prediction_visual_camera(*published.correction,*base.state,actual,10.004,2,7));
    CHECK_FALSE(prediction::publish_validated_prediction_visual_camera(*created.correction,*base.state,actual,10.021,2,7));
    CHECK_FALSE(prediction::publish_validated_prediction_visual_camera(*created.correction,*base.state,actual,10.005,3,7));
    CHECK_FALSE(prediction::publish_validated_prediction_visual_camera(*created.correction,*base.state,actual,10.005,2,8));
    CHECK_FALSE(prediction::publish_validated_prediction_visual_camera(*created.correction,*base.state,{100,100,64},10.005,2,7));
    info.position.x+=.1F;const auto moved_base=camera::GameplayCameraState::create(info);REQUIRE(moved_base);
    const auto moving=prediction::publish_validated_prediction_visual_camera(*published.correction,*moved_base.state,actual,10.006,2,7);REQUIRE(moving);
    CHECK(moving.correction->current_residual_offset().x==Catch::Approx(-.85F));CHECK(moved_base.state->position().x==info.position.x);
    CHECK(created.correction->last_sample_monotonic_time_seconds()==10);
}
