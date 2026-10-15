#include "literal_movement_bsp_fixture.hpp"
#include "local_movement_test_fixture.hpp"
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>

namespace {
namespace g=hlclient::goldsrc;
namespace m=g::movement;
namespace p=hlclient::movement;
namespace c=g::collision;
namespace f=hlclient::tests::literal_movement_bsp;
namespace s=hlclient::tests;

std::shared_ptr<const hlclient::collision::CollisionWorldPackage> surface(float slope,float height,bool crossed=false) {
    std::vector<f::ConvexVolume> volumes{{{f::normalized_halfspace({slope,0,1},height,false)},-2}};
    if (crossed) volumes.push_back({{f::normalized_halfspace({0,slope,1},height,false)},-2});
    std::vector<s::SyntheticBspPlane> planes{{{0,0,1},0,2}};
    std::vector<s::SyntheticBspNode> nodes;
    std::vector<s::SyntheticBspClipnode> clips;
    s::SyntheticBspModel model;
    model.minimum={-4096,-4096,-4096};model.maximum={4096,4096,4096};
    model.headnodes={f::compile_point_tree(volumes,planes,nodes),
        f::compile_clip_tree(volumes,{16,16,36},planes,clips),-1,
        f::compile_clip_tree(volumes,{16,16,18},planes,clips)};
    model.face_count=1;model.visibility_leaf_count=1;
    const std::array leaves{s::SyntheticBspLeaf{-1,-1,{-4096,-4096,-4096},{4096,4096,4096},0,0,{}},
        s::SyntheticBspLeaf{-2,-1,{-4096,-4096,-4096},{4096,4096,4096},0,0,{}}};
    const auto bytes=s::SyntheticBspBuilder{}.set_planes(planes).set_nodes(nodes).set_clipnodes(clips)
        .set_leaves(leaves).set_models(std::span{&model,1U}).build();
    const auto parsed=g::bsp::GoldSrcBspParser::parse(bytes);REQUIRE(parsed);
    const auto built=c::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);REQUIRE(built);
    return built.package;
}
g::ReferenceBrushCollisionContext context(std::shared_ptr<const hlclient::collision::CollisionWorldPackage> world,
    std::vector<hlclient::client::RuntimePacketEntityObservation> entities={},
    std::optional<std::uint32_t> player_code={},
    const std::map<std::uint32_t,std::string>& names={},
    std::span<const m::ServerPointBoxExclusionRule> exclusions={}) {
    const auto library=c::build_brush_collision_model_library(std::move(world));REQUIRE(library);
    hlclient::client::RuntimeClientObservationState observation;
    observation.generation=1;observation.publication_revision=10;
    observation.entity_metadata={1,hlclient::client::RuntimeObservationFreshness::observed_in_record,
        hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,
        hlclient::client::RuntimeObservationSource{10,10}};
    observation.server_time_seconds=1.0;observation.server_time_metadata=observation.entity_metadata;
    observation.packet_entities=std::move(entities);
    auto result=g::build_reference_brush_collision(library.library,names,observation,1,2,13,true,player_code,exclusions);
    REQUIRE(result.scene);return result;
}
g::ReferencePredictionSeed seed(double z) {
    g::ReferencePredictionSeed result;
    result.generation=1;result.record_identity=10;result.command_boundary=*g::GoldSrcUserCmdSequence::create(7);
    result.origin={-496.859375,703.3828125,z};result.velocity={0,0,0};result.view_offset={0,0,28};
    result.base_velocity={0,0,0};result.flags=1U<<9U;result.move_type=3;result.use_hull=0;
    result.gravity_multiplier=1;result.friction_multiplier=1;return result;
}

// Project-owned floor/wall corridor around the observed binary32 coordinate
// range. The optional low ceiling is an independent contact control, not a
// claim that the installed map has this exact geometry. Its positive-facing
// BSP plane places equality in the solid child, so a generated upward contact
// must not be used directly as the speculative horizontal step's start.
std::shared_ptr<const hlclient::collision::CollisionWorldPackage> contact_corridor(
    const bool low_ceiling, const bool ceiling_front_solid=true) {
    std::vector<f::ConvexVolume> volumes{
        {{f::normalized_halfspace({0,0,1},-1752,false)},-2},
        {{f::normalized_halfspace({0,1,0},-1776,false)},-2}};
    if (low_ceiling) volumes.push_back({{ceiling_front_solid
        ? f::normalized_halfspace({0,0,1},-1668,true)
        : f::normalized_halfspace({0,0,-1},1668,false)},-2});
    std::vector<s::SyntheticBspPlane> planes{{{0,0,1},0,2}};
    std::vector<s::SyntheticBspNode> nodes;
    std::vector<s::SyntheticBspClipnode> clips;
    s::SyntheticBspModel model;
    model.minimum={-4096,-4096,-4096};model.maximum={4096,4096,4096};
    model.headnodes={f::compile_point_tree(volumes,planes,nodes),
        f::compile_clip_tree(volumes,{16,16,36},planes,clips),-1,
        f::compile_clip_tree(volumes,{16,16,18},planes,clips)};
    model.face_count=1;model.visibility_leaf_count=1;
    const std::array leaves{s::SyntheticBspLeaf{-1,-1,{-4096,-4096,-4096},{4096,4096,4096},0,0,{}},
        s::SyntheticBspLeaf{-2,-1,{-4096,-4096,-4096},{4096,4096,4096},0,0,{}}};
    const auto bytes=s::SyntheticBspBuilder{}.set_planes(planes).set_nodes(nodes).set_clipnodes(clips)
        .set_leaves(leaves).set_models(std::span{&model,1U}).build();
    const auto parsed=g::bsp::GoldSrcBspParser::parse(bytes);REQUIRE(parsed);
    const auto built=c::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);REQUIRE(built);
    return built.package;
}

// Geometry and ordinary queries remain the production BSP provider. Only a
// typed provider fault at one precisely selected new clearance query is
// injected: an unavailable or malformed query is not a geometric reason to
// silently select the otherwise valid direct route.
class RaisedEndpointFault final : public m::ILocalMovementCollision {
public:
    enum class Point { raw_position, adjusted_position, recovery_path };
    RaisedEndpointFault(const m::ILocalMovementCollision& source,const Point point,const bool malformed)
        : source_(source),point_(point),malformed_(malformed) {}
    m::LocalMovementCollisionProfile profile() const noexcept override {return source_.profile();}
    bool valid() const noexcept override {return source_.valid();}
    std::optional<m::LocalMovementCollisionSessionIdentity> session_identity() const noexcept override {return source_.session_identity();}
    m::LocalMovementPointContentsQueryResult point_contents(const hlclient::assets::AssetVector3& at,
        hlclient::collision::CollisionQueryScratch& scratch,const m::LocalMovementCollisionQueryConfig& config) const override {
        return source_.point_contents(at,scratch,config);
    }
    m::LocalMovementPositionQueryResult test_position(const hlclient::assets::AssetVector3& at,
        const p::PlayerMovementHull hull,hlclient::collision::CollisionQueryScratch& scratch,
        const m::LocalMovementCollisionQueryConfig& config) const override {
        const bool selected=(point_==Point::raw_position && at.z==-1704.F) ||
            (point_==Point::adjusted_position && at.z==adjusted_height());
        if (!selected) return source_.test_position(at,hull,scratch,config);
        ++fault_queries;
        if (!malformed_) return {{},m::LocalMovementCollisionError{m::LocalMovementCollisionErrorCode::world_query_failed,{},{}}};
        auto value=source_.test_position(at,hull,scratch,config);
        if (value.result) value.result->status=static_cast<m::LocalMovementPositionStatus>(255);
        return value;
    }
    m::LocalMovementTraceQueryResult trace_hull(const hlclient::assets::AssetVector3& from,
        const hlclient::assets::AssetVector3& to,const p::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,const m::LocalMovementCollisionQueryConfig& config) const override {
        const bool selected=point_==Point::recovery_path && to.z==adjusted_height() &&
            from.z<to.z && from.x==to.x && from.y==to.y;
        if (!selected) return source_.trace_hull(from,to,hull,scratch,config);
        ++fault_queries;
        if (!malformed_) return {{},m::LocalMovementCollisionError{m::LocalMovementCollisionErrorCode::world_query_failed,{},{}}};
        auto value=source_.trace_hull(from,to,hull,scratch,config);
        if (value.result) value.result->fraction=std::numeric_limits<double>::quiet_NaN();
        return value;
    }
    mutable unsigned fault_queries{};
private:
    static float adjusted_height() {return std::nextafter(-1704.F,-std::numeric_limits<float>::infinity());}
    const m::ILocalMovementCollision& source_;
    Point point_;
    bool malformed_;
};
}

TEST_CASE("E32 tangent wall movement under a low BSP ceiling never starts a speculative step in solid",
    "[e32-contact-recovery][e32-uneven][movement][literal-bsp][no-stock]") {
    for (const bool low_ceiling:{false,true}) for (const bool front_solid:{false,true})
    for (const bool duck:{false,true}) {
        CAPTURE(low_ceiling,front_solid,duck);
        const m::WorldOnlyMovementCollision collision{contact_corridor(low_ceiling,front_solid)};
        m::GoldSrcLocalMovementScratch scratch;
        const float floor_center=duck?-1734.F:-1716.F;
        auto input=seed(floor_center+1.0/32.0);input.origin={465.390625,-1759.96875,floor_center+1.0/32.0};
        input.velocity={-232.5,0,0};
        if (duck) {
            input.use_hull=1;input.in_duck=false;input.flags=(1U<<9U)|(1U<<14U);
            input.old_buttons=g::kReferenceGoldSrcButtonDuck;input.view_offset={0,0,12};
        }
        const auto derived=g::derive_reference_prediction_ground(input,{},collision,scratch,{},
            p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
        REQUIRE(derived.state);REQUIRE(derived.state->ground_state().grounded());
        auto state=std::make_shared<const p::LocalPlayerMovementState>(*derived.state);
        const auto initial_x=state->origin().x;
        const auto upward=collision.trace_hull(state->origin(),
            {state->origin().x,state->origin().y,state->origin().z+18.F},state->hull(),scratch.collision);
        REQUIRE(upward);CHECK_FALSE(upward.result->start_solid);CHECK_FALSE(upward.result->all_solid);
        if (low_ceiling && !duck) {
            REQUIRE(upward.result->collision_plane);CHECK(upward.result->collision_plane->normal.z==-1.F);
            CHECK(upward.result->end_position.z==-1704.F);
            const auto endpoint=collision.test_position(upward.result->end_position,state->hull(),scratch.collision);REQUIRE(endpoint);
            CHECK((endpoint.result->status==m::LocalMovementPositionStatus::blocking)==front_solid);
        } else CHECK(upward.result->fraction==1.0);
        for (std::uint32_t index=0;index<36;++index) {
            // The first headings and forward input are observed command values;
            // repetition exercises independent grounded commands, not a replay
            // of the user's incomplete per-frame incident history.
            const std::array<std::uint16_t,3> headings{42598,39522,33387};
            g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;wire.buttons=8;
            if (duck) wire.buttons|=g::kReferenceGoldSrcButtonDuck;
            wire.angle_turns[1]=headings[(index/12)%headings.size()];
            const auto command=g::reference_jump_duck_weapon_use_movement_command(
                *g::GoldSrcUserCmdSequence::create(8+index),wire);REQUIRE(command);
            const auto moved=g::simulate_reference_movement(*state,*command.state,
                s::local_movement::make_environment(),collision,scratch,{});
            INFO((moved.error?moved.error->context:"ready"));CAPTURE(index,state->origin().x,state->origin().y,state->origin().z);
            REQUIRE(moved);REQUIRE(moved.state);
            CHECK(moved.state->origin().x<state->origin().x);
            CHECK(moved.state->origin().y>=-1760.F);
            CHECK(moved.state->origin().z==Catch::Approx(floor_center).margin(.001));
            CHECK(moved.state->hull()==state->hull());
            CHECK(moved.state->ground_state().grounded());
            const auto free=collision.test_position(moved.state->origin(),moved.state->hull(),scratch.collision);REQUIRE(free);
            CHECK(free.result->status==m::LocalMovementPositionStatus::free);
            CHECK_FALSE(scratch.last_reference_collision_trace);
            state=std::make_shared<const p::LocalPlayerMovementState>(*moved.state);
        }
        CHECK(initial_x-state->origin().x>(duck?40.F:100.F));
    }
}

TEST_CASE("E32 low-ceiling contact recovery cannot free a genuinely embedded player",
    "[e32-contact-recovery][e32-uneven][movement][literal-bsp][no-stock]") {
    const m::WorldOnlyMovementCollision collision{contact_corridor(true)};
    m::GoldSrcLocalMovementScratch scratch;
    auto input=seed(-1715.96875);input.origin={465.390625,-1759.96875,-1715.96875};
    const auto derived=g::derive_reference_prediction_ground(input,{},collision,scratch,{},
        p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);REQUIRE(derived.state);
    for (const auto embedded:std::array{hlclient::assets::AssetVector3{465,-1760.5F,-1716},
            hlclient::assets::AssetVector3{465,-1759,-1716.5F},
            hlclient::assets::AssetVector3{465,-1759,-1703.5F}}) {
        auto info=p::local_player_movement_state_create_info(*derived.state);info.origin=embedded;
        const auto previous=p::LocalPlayerMovementState::create(info);REQUIRE(previous);
        const auto blocked=collision.test_position(embedded,previous.state->hull(),scratch.collision);REQUIRE(blocked);
        REQUIRE(blocked.result->status==m::LocalMovementPositionStatus::blocking);
        g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;wire.buttons=8;wire.angle_turns[1]=42598;
        const auto command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(8),wire);REQUIRE(command);
        const auto moved=g::simulate_reference_movement(*previous.state,*command.state,
            s::local_movement::make_environment(),collision,scratch,{});
        REQUIRE_FALSE(moved);REQUIRE(moved.error);
        CHECK(moved.error->code==m::LocalMovementSimulationErrorCode::player_allsolid);
        REQUIRE(scratch.last_reference_collision_trace);
        CHECK(scratch.last_reference_collision_trace->all_solid);
        CHECK(scratch.last_reference_collision_trace->start_solid);
        CHECK(scratch.last_reference_collision_hull==p::PlayerMovementHull::standing);
        REQUIRE(scratch.last_reference_collision_start);REQUIRE(scratch.last_reference_collision_end);
        CHECK(scratch.last_reference_collision_start->x==embedded.x);
        CHECK(scratch.last_reference_collision_start->y==embedded.y);
        CHECK(scratch.last_reference_collision_start->z==embedded.z);
        CHECK(scratch.last_reference_collision_end->x==embedded.x);
        CHECK(scratch.last_reference_collision_end->y==embedded.y);
        CHECK(scratch.last_reference_collision_end->z==embedded.z);
        const auto valid=g::simulate_reference_movement(*derived.state,*command.state,
            s::local_movement::make_environment(),collision,scratch,{});REQUIRE(valid);
        CHECK_FALSE(scratch.last_reference_collision_trace);
        CHECK_FALSE(scratch.last_reference_collision_position);
        CHECK_FALSE(scratch.last_reference_collision_hull);
        CHECK_FALSE(scratch.last_reference_collision_start);
        CHECK_FALSE(scratch.last_reference_collision_end);
    }
}

TEST_CASE("E32 speculative step clearance propagates provider errors and malformed observations",
    "[e32-contact-recovery][e32-uneven][movement][literal-bsp][no-stock]") {
    const m::WorldOnlyMovementCollision geometry{contact_corridor(true)};
    for (const auto point:{RaisedEndpointFault::Point::raw_position,RaisedEndpointFault::Point::adjusted_position,
            RaisedEndpointFault::Point::recovery_path}) for (const bool malformed:{false,true}) {
        CAPTURE(static_cast<unsigned>(point),malformed);
        const RaisedEndpointFault collision{geometry,point,malformed};
        m::GoldSrcLocalMovementScratch scratch;
        auto input=seed(-1715.96875);input.origin={465.390625,-1759.96875,-1715.96875};input.velocity={-232.5,0,0};
        const auto derived=g::derive_reference_prediction_ground(input,{},collision,scratch,{},
            p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);REQUIRE(derived.state);
        g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;wire.buttons=8;wire.angle_turns[1]=42598;
        const auto command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(8),wire);REQUIRE(command);
        const auto moved=g::simulate_reference_movement(*derived.state,*command.state,
            s::local_movement::make_environment(),collision,scratch,{});
        REQUIRE(collision.fault_queries==1U);
        REQUIRE_FALSE(moved);REQUIRE(moved.error);
        CHECK(moved.error->code==m::LocalMovementSimulationErrorCode::collision_query_failed);
    }
}

TEST_CASE("E32 uneven landing consumes the witnessed sloped contact instead of retaining pre-hit altitude",
    "[e32-uneven][movement][landing]") {
    const m::WorldOnlyMovementCollision collision{surface(1.F/12.F,-1851.833333F)};
    auto info=p::local_player_movement_state_create_info(s::local_movement::make_state());
    info.origin={36.70006F,761.57965F,-1808.4961F};info.velocity={-291.625F,-131.875F,-579.625F};
    info.ground={};info.mode=p::PlayerMovementMode::airborne;
    info.command_profile=p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2;
    info.compatibility_profile=p::GoldSrcMovementCompatibilityProfile::public_valve_pm_shared_dry_actions_subset_v2;
    const auto previous=p::LocalPlayerMovementState::create(info);REQUIRE(previous);
    g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;wire.angle_turns[1]=34934;wire.buttons=8;
    const auto command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(1),wire);REQUIRE(command);
    m::GoldSrcLocalMovementScratch scratch;
    const auto result=g::simulate_reference_movement(*previous.state,*command.state,s::local_movement::make_environment(),collision,scratch,{});
    REQUIRE(result);
    CHECK(result.state->ground_state().grounded());
    CHECK(std::abs(result.state->origin().z-(-1814.5F-result.state->origin().x/12.F))<.05F);
    const auto occupancy=collision.test_position(result.state->origin(),result.state->hull(),scratch.collision);REQUIRE(occupancy);
    CHECK(occupancy.result->status==m::LocalMovementPositionStatus::free);
    auto state=std::make_shared<const p::LocalPlayerMovementState>(*result.state);
    for (std::uint32_t sequence=2;sequence<242;++sequence) {
        wire.angle_turns[1]=sequence<122 ? 34934 : 2166;
        const auto next_command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(sequence),wire);REQUIRE(next_command);
        const auto next=g::simulate_reference_movement(*state,*next_command.state,s::local_movement::make_environment(),collision,scratch,{});REQUIRE(next);
        CHECK(next.state->ground_state().grounded());
        CHECK(std::abs(next.state->origin().z-(-1814.5F-next.state->origin().x/12.F))<.05F);
        state=std::make_shared<const p::LocalPlayerMovementState>(*next.state);
    }
}

TEST_CASE("E32 shallow intersecting world slopes preserve height across both seam directions",
    "[e32-uneven][movement][ground-seam]") {
    const m::WorldOnlyMovementCollision collision{surface(1.F/12.F,-1851.833333F,true)};
    auto source=seed(-1814.5-800.0/12.0+1.0/32.0);source.origin.x=900;source.origin.y=800;
    m::GoldSrcLocalMovementScratch scratch;
    const auto derived=g::derive_reference_prediction_ground(source,{},collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);REQUIRE(derived.state);
    auto state=std::make_shared<const p::LocalPlayerMovementState>(*derived.state);
    for (std::uint32_t sequence=8;sequence<208;++sequence) {
        g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;wire.buttons=8;wire.angle_turns[1]=sequence<108 ? 32768 : 0;
        const auto command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(sequence),wire);REQUIRE(command);
        const auto next=g::simulate_reference_movement(*state,*command.state,s::local_movement::make_environment(),collision,scratch,{});REQUIRE(next);
        CHECK(next.state->ground_state().grounded());
        CHECK(std::abs(next.state->origin().z-(-1814.5F-std::min(next.state->origin().x,next.state->origin().y)/12.F))<.05F);
        const auto free=collision.test_position(next.state->origin(),next.state->hull(),scratch.collision);REQUIRE(free);
        CHECK(free.result->status==m::LocalMovementPositionStatus::free);
        state=std::make_shared<const p::LocalPlayerMovementState>(*next.state);
    }
}

TEST_CASE("E32 quantized shallow box top has a witnessed support without bypassing deeper penetration",
    "[e32-uneven][movement][prediction-seed][server-box]") {
    hlclient::client::RuntimePacketEntityObservation box;
    box.entity_number=347;box.model_index=156;box.solid=2;box.owner_entity_reference=0;
    box.origin={-496.75,703.375,-1695.875};box.collision_mins={0,0,0};box.collision_maxs={0,0,0};
    const auto frame=context(surface(0,-1696),{box});
    const m::BrushSceneMovementCollision collision{frame.scene,frame.boxes};
    m::GoldSrcLocalMovementScratch scratch;
    const auto source=seed(-1659.96875);
    const auto result=g::derive_reference_prediction_ground(source,{},collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(result.state);CHECK(result.state->ground_state().grounded());
    REQUIRE(result.state->ground_state().hit());CHECK(result.state->ground_state().hit()->source_entity_index==347);
    CHECK(result.state->origin().z==Catch::Approx(-1659.875F).margin(.001));
    CHECK(*source.origin.z==-1659.96875);
    const auto deeper=g::derive_reference_prediction_ground(seed(-1660.2),{},collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    CHECK_FALSE(deeper.state);
    const auto lower_floor=context(surface(0,-1700),{box});
    const m::BrushSceneMovementCollision deep_collision{lower_floor.scene,lower_floor.boxes};
    const auto deep_box=g::derive_reference_prediction_ground(seed(-1660.2),{},deep_collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    CHECK_FALSE(deep_box.state);
    auto airborne=source;airborne.flags=0;
    CHECK_FALSE(g::derive_reference_prediction_ground(airborne,{},collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2).state);
    auto overhead=box;overhead.entity_number=348;overhead.origin.z=-1695.75;
    const auto obstructed=context(surface(0,-1696),{box,overhead});
    const m::BrushSceneMovementCollision obstructed_collision{obstructed.scene,obstructed.boxes};
    CHECK_FALSE(g::derive_reference_prediction_ground(source,{},obstructed_collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2).state);
}

TEST_CASE("E32 airborne source remains airborne despite a valid local proximity probe",
    "[e32-uneven][movement][prediction-seed]") {
    const m::WorldOnlyMovementCollision collision{surface(0,-1696)};
    auto source=seed(-1659.96875);source.flags=0;
    m::GoldSrcLocalMovementScratch scratch;
    const auto strict=g::derive_reference_prediction_ground(source,{},collision,scratch);
    CHECK(strict.status==g::ReferencePredictionGroundStatus::ground_flag_disagreement);
    const auto result=g::derive_reference_prediction_ground(source,{},collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(result.state);CHECK_FALSE(result.state->ground_state().grounded());
    CHECK(result.state->mode()==p::PlayerMovementMode::airborne);
    CHECK(result.state->origin().z==static_cast<float>(*source.origin.z));
    CHECK(source.flags==0);
}

TEST_CASE("E32 remote player collision uses exact negotiated slidebox bounds with explicit policy",
    "[e32-uneven][movement][server-box][player-collision]") {
    hlclient::client::RuntimePacketEntityObservation player;
    player.entity_number=2;player.player_movement_schema=true;player.solid=3;
    player.origin={0,0,36};player.collision_mins={-16,-16,-36};player.collision_maxs={16,16,36};
    auto self=player;self.entity_number=13;self.collision_mins={};self.collision_maxs={};
    auto ordinary=player;ordinary.entity_number=3;ordinary.player_movement_schema=false;
    const auto off=context(surface(0,0),{player,self,ordinary});CHECK(off.boxes.empty());
    const auto frame=context(surface(0,0),{player,self,ordinary},3);
    REQUIRE(frame.boxes.size()==1);CHECK(frame.boxes[0].entity==2);
    const m::BrushSceneMovementCollision collision{frame.scene,frame.boxes};
    hlclient::collision::CollisionQueryScratch scratch;
    const auto trace=collision.trace_hull({-64,0,36},{64,0,36},p::PlayerMovementHull::standing,scratch);
    REQUIRE(trace);REQUIRE(trace.result->hit);
    CHECK(trace.result->hit->source_entity_index==2);CHECK(trace.result->fraction<.3);
    const auto center=collision.test_position({0,0,36},p::PlayerMovementHull::standing,scratch);
    REQUIRE(center);CHECK(center.result->status==m::LocalMovementPositionStatus::blocking);
    const auto moved=context(surface(0,0),{[&]{auto next=player;next.origin.x=128;return next;}()},3);
    const m::BrushSceneMovementCollision after{moved.scene,moved.boxes};
    const auto old_center=after.test_position({0,0,36},p::PlayerMovementHull::standing,scratch);
    REQUIRE(old_center);CHECK(old_center.result->status==m::LocalMovementPositionStatus::free);
    auto source=seed(36);source.origin={-31.96875,0,36};
    m::GoldSrcLocalMovementScratch movement_scratch;
    const auto recovered=g::derive_reference_prediction_ground(source,{},collision,movement_scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(recovered.state);CHECK(recovered.state->origin().x==-32.F);CHECK(*source.origin.x==-31.96875);
    source.origin.x=-31;
    CHECK_FALSE(g::derive_reference_prediction_ground(source,{},collision,movement_scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2).state);
    g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;
    const auto command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(8),wire);REQUIRE(command);
    const auto blocked=g::simulate_reference_movement(*recovered.state,*command.state,s::local_movement::make_environment(),collision,movement_scratch,{});
    REQUIRE(blocked);CHECK(blocked.state->origin().x<=-32.F);CHECK(blocked.state->velocity().x==0);
}

TEST_CASE("E32 selected point debris cannot lift block or invalidate a slope seed",
    "[e32-gibs][movement][server-box][core-only]") {
    const std::array rules{m::ServerPointBoxExclusionRule{"models/fixture_debris.mdl",10}};
    const std::map<std::uint32_t,std::string> names{{156,"models/fixture_debris.mdl"}};
    const auto world=surface(1.F/12.F,-1851.833333F);
    hlclient::client::RuntimePacketEntityObservation debris;
    debris.entity_number=297;debris.solid=2;debris.brush_move_type=10;
    debris.model_index=156;debris.owner_entity_reference=0;
    debris.collision_mins={0,0,0};debris.collision_maxs={0,0,0};
    debris.origin={-173,594,-1851.833333+173.0/12.0+2.0};
    const auto off=context(world,{debris},{},names);
    REQUIRE(off.boxes.size()==1);
    const auto on=context(world,{debris},{},names,rules);
    REQUIRE(on.boxes.empty());CHECK(on.excluded_point_box_count==1);
    const m::BrushSceneMovementCollision collision{on.scene,on.boxes};
    const m::WorldOnlyMovementCollision baseline{world};
    auto source=seed(-1814.5+173.0/12.0+1.0/32.0);source.origin={-173,594,*source.origin.z};
    m::GoldSrcLocalMovementScratch scratch,control_scratch;
    const auto rejected=g::derive_reference_prediction_ground(source,{},
        m::BrushSceneMovementCollision{off.scene,off.boxes},scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    CHECK_FALSE(rejected.state);
    const auto derived=g::derive_reference_prediction_ground(source,{},collision,scratch,{},p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(derived.state);CHECK(derived.state->ground_state().grounded());
    REQUIRE(derived.state->ground_state().hit());
    CHECK(derived.state->ground_state().hit()->kind==p::PlayerMovementHitKind::world);
    auto state=std::make_shared<const p::LocalPlayerMovementState>(*derived.state);
    auto control=state;
    for (std::uint32_t sequence=8; sequence<168; ++sequence) {
        // Current-frame debris moves/reuses the same slot, including directly
        // inside the predicted hull. Never retain its old support or shape.
        debris.origin={state->origin().x,state->origin().y,state->origin().z-34.0};
        const auto frame=context(world,{debris},{},names,rules);
        REQUIRE(frame.boxes.empty());
        const m::BrushSceneMovementCollision current{frame.scene,frame.boxes};
        g::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=400;
        wire.angle_turns[1]=sequence<88 ? 0 : 32768;
        if (sequence==48 || sequence==128) wire.buttons=2;
        const auto command=g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(sequence),wire);REQUIRE(command);
        const auto next=g::simulate_reference_movement(*state,*command.state,s::local_movement::make_environment(),current,scratch,{});REQUIRE(next);
        const auto expected=g::simulate_reference_movement(*control,*command.state,s::local_movement::make_environment(),baseline,control_scratch,{});REQUIRE(expected);
        CHECK(next.state->origin().x==expected.state->origin().x);
        CHECK(next.state->origin().y==expected.state->origin().y);
        CHECK(next.state->origin().z==expected.state->origin().z);
        CHECK(next.state->velocity().z==expected.state->velocity().z);
        CHECK(next.state->ground_state().grounded()==expected.state->ground_state().grounded());
        state=std::make_shared<const p::LocalPlayerMovementState>(*next.state);
        control=std::make_shared<const p::LocalPlayerMovementState>(*expected.state);
    }
}

TEST_CASE("E32 point-box exclusion requires exact current model movetype bounds and nonplayer schema",
    "[e32-gibs][movement][server-box][core-only]") {
    const auto world=surface(0,0);
    const std::array rules{m::ServerPointBoxExclusionRule{"models/fixture_debris.mdl",10}};
    const std::map<std::uint32_t,std::string> names{{156,"models/fixture_debris.mdl"}};
    hlclient::client::RuntimePacketEntityObservation entity;
    entity.entity_number=297;entity.solid=2;entity.brush_move_type=10;entity.model_index=156;
    entity.owner_entity_reference=0;entity.origin={0,0,2};
    entity.collision_mins={0,0,0};entity.collision_maxs={0,0,0};
    CHECK(context(world,{entity},{},names,rules).boxes.empty());
    auto finite_box=entity;finite_box.collision_mins={-4,-4,-2};finite_box.collision_maxs={4,4,2};
    CHECK(context(world,{finite_box},{},names,rules).boxes.size()==1);
    auto tiny_box=entity;tiny_box.collision_maxs.x=1e-50;
    CHECK(context(world,{tiny_box},{},names,rules).boxes.size()==1);
    auto other_type=entity;other_type.brush_move_type=6;
    CHECK(context(world,{other_type},{},names,rules).boxes.size()==1);
    auto absent_type=entity;absent_type.brush_move_type.reset();
    CHECK(context(world,{absent_type},{},names,rules).boxes.size()==1);
    CHECK(context(world,{entity},{},{},rules).boxes.size()==1);
    CHECK(context(world,{entity},{},{{156,"models/fixture_charge.mdl"}},rules).boxes.size()==1);
    auto player=entity;player.entity_number=2;player.player_movement_schema=true;player.solid=3;
    CHECK(context(world,{player},3,names,rules).boxes.size()==1);
    // Even an explicit BBOX player cannot be classified as point debris.
    player.solid=2;
    CHECK(context(world,{player},3,names,rules).boxes.size()==1);
    auto own=finite_box;own.owner_entity_reference=13;
    CHECK(context(world,{own},{},names,rules).boxes.empty());
    auto removed=entity;removed.solid=0;
    CHECK(context(world,{removed},{},names,rules).excluded_point_box_count==0);
    // Reusing the old slot for a real box cannot inherit an exclusion.
    const auto reused=context(world,{finite_box},{},{{156,"models/fixture_charge.mdl"}},rules);
    REQUIRE(reused.boxes.size()==1);CHECK(reused.excluded_point_box_count==0);
    const m::BrushSceneMovementCollision collision{reused.scene,reused.boxes};
    hlclient::collision::CollisionQueryScratch scratch;
    const auto blocked=collision.trace_hull({-64,0,36},{64,0,36},p::PlayerMovementHull::standing,scratch);
    REQUIRE(blocked);REQUIRE(blocked.result->hit);CHECK(blocked.result->hit->source_entity_index==297);
}

TEST_CASE("E32 bounded point-box rules reject malformed profiles without admitting a partial scene",
    "[e32-gibs][movement][core-only]") {
    std::vector<m::ServerPointBoxExclusionRule> rules{{"models/fixture_debris.mdl",10}};
    CHECK(m::valid_server_point_box_exclusion_rules(rules));
    rules.push_back(rules.front());CHECK_FALSE(m::valid_server_point_box_exclusion_rules(rules));
    rules.resize(1);rules[0].move_type=256;CHECK_FALSE(m::valid_server_point_box_exclusion_rules(rules));
    rules[0].move_type=10;rules[0].model_name=std::string(129,'x');CHECK_FALSE(m::valid_server_point_box_exclusion_rules(rules));
    rules[0].model_name="";CHECK_FALSE(m::valid_server_point_box_exclusion_rules(rules));
    rules[0].model_name=std::string("a\0b",3);CHECK_FALSE(m::valid_server_point_box_exclusion_rules(rules));
    rules.resize(9);CHECK_FALSE(m::valid_server_point_box_exclusion_rules(rules));
    const auto library=c::build_brush_collision_model_library(surface(0,0));REQUIRE(library);
    const auto rejected=g::build_reference_brush_collision(library.library,{}, {},1,2,13,true,3,rules);
    CHECK_FALSE(rejected.scene);CHECK(rejected.reason=="collision_point_box_policy_invalid");
}
