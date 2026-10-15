#pragma once
#include "literal_movement_bsp_fixture.hpp"
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <memory>

namespace hlclient::tests::convex_corner {
namespace literal=hlclient::tests::literal_movement_bsp;
// Authored fixture coordinates only. They do not locate the user's screenshot.
// One finite 128x128 wall has its outside corner at (0,0). Independent BSP
// point/standing/duck hulls each expand the six source planes exactly once.
[[nodiscard]] inline std::shared_ptr<const collision::CollisionWorldPackage> package(bool hole=false,float corner_offset=0,bool concave=false) {
    std::vector<literal::ConvexVolume> volumes;
    if(hole) {
        volumes.push_back(literal::box({-256,-256,-128},{-96,256,0}));
        volumes.push_back(literal::box({-32,-256,-128},{256,256,0}));
        volumes.push_back(literal::box({-96,-256,-128},{-32,-96,0}));
        volumes.push_back(literal::box({-96,-32,-128},{-32,256,0}));
    } else volumes.push_back(literal::box({-256,-256,-128},{256,256,0}));
    if(concave) {
        volumes.push_back(literal::box({0,-128,0},{128,128,128}));
        volumes.push_back(literal::box({-128,0,0},{128,128,128}));
    } else volumes.push_back(literal::box({corner_offset,corner_offset,0},{corner_offset+128,corner_offset+128,128}));
    using namespace hlclient::tests;
    std::vector<SyntheticBspPlane> planes{SyntheticBspPlane{{0,0,1},0,2}};
    std::vector<SyntheticBspNode> nodes;std::vector<SyntheticBspClipnode> clipnodes;
    const auto point=literal::compile_point_tree(volumes,planes,nodes);
    const auto standing=literal::compile_clip_tree(volumes,{16,16,36},planes,clipnodes);
    const auto duck=literal::compile_clip_tree(volumes,{16,16,18},planes,clipnodes);
    const std::array leaves{
        SyntheticBspLeaf{-1,-1,{-256,-256,-128},{256,256,128},0,0,{}},
        SyntheticBspLeaf{-2,-1,{-256,-256,-128},{256,256,128},0,0,{}}};
    SyntheticBspModel model;model.minimum={-256,-256,-128};model.maximum={256,256,128};
    model.headnodes={point,standing,-1,duck};model.visibility_leaf_count=1;model.first_face=0;model.face_count=1;
    SyntheticBspBuilder builder;
    const auto bytes=builder.set_planes(planes).set_nodes(nodes).set_leaves(leaves)
        .set_clipnodes(clipnodes).set_models(std::span{&model,1U}).build();
    const auto parsed=goldsrc::bsp::GoldSrcBspParser::parse(bytes);if(!parsed || !parsed.document)return {};
    const auto built=goldsrc::collision::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);
    return built?built.package:std::shared_ptr<const collision::CollisionWorldPackage>{};
}
[[nodiscard]] inline std::optional<movement::LocalPlayerMovementState> seed(
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,assets::AssetVector3 origin,
    movement::PlayerMovementHull hull=movement::PlayerMovementHull::standing,
    assets::AssetVector3 velocity={}) {
    goldsrc::ReferencePredictionSeed input;
    input.generation=1;input.record_identity=1;input.receiving_entity_number=1;
    input.command_boundary=*goldsrc::GoldSrcUserCmdSequence::create(7);
    input.origin={origin.x,origin.y,origin.z};input.velocity={velocity.x,velocity.y,velocity.z};
    input.view_offset={0,0,hull==movement::PlayerMovementHull::standing?28.0:12.0};
    input.base_velocity={0,0,0};input.flags=1U<<9U;input.move_type=3;
    input.use_hull=hull==movement::PlayerMovementHull::standing?0U:1U;
    // bInDuck is the transition flag. A completed duck has hull1/FL_DUCKING
    // but no transition, exactly as the production seed contract requires.
    input.in_duck=false;
    if(hull==movement::PlayerMovementHull::ducked){input.flags|=1U<<14U;input.old_buttons=goldsrc::kReferenceGoldSrcButtonDuck;}
    input.gravity_multiplier=1;input.friction_multiplier=1;
    const auto derived=goldsrc::derive_reference_prediction_ground(input,{},collision,scratch,{},
        movement::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    if(!derived.state)return {};
    auto info=movement::local_player_movement_state_create_info(*derived.state);
    info.simulation_time_nanoseconds=1'000'000'000;
    return movement::LocalPlayerMovementState::create(info).state;
}
[[nodiscard]] inline goldsrc::GoldSrcUserCmdState::CreationResult command(
    std::uint32_t sequence,double yaw,std::int16_t forward,std::int16_t side,
    bool duck=false,bool attack=false) {
    goldsrc::GoldSrcWireUserCmd wire;wire.msec=20;wire.forward=forward;wire.side=side;
    const auto angle=goldsrc::quantize_wire_angle(yaw);if(!angle)return {};
    wire.angle_turns[1]=*angle;
    wire.buttons=goldsrc::kReferenceGoldSrcButtonForward;
    if(side>0)wire.buttons|=goldsrc::kReferenceGoldSrcButtonMoveRight;
    if(side<0)wire.buttons|=goldsrc::kReferenceGoldSrcButtonMoveLeft;
    if(duck)wire.buttons|=goldsrc::kReferenceGoldSrcButtonDuck;
    if(attack)wire.buttons|=goldsrc::kReferenceGoldSrcButtonAttack;
    const auto identity=goldsrc::GoldSrcUserCmdSequence::create(sequence);if(!identity)return {};
    return goldsrc::reference_jump_duck_weapon_use_movement_command(*identity,wire);
}
} // namespace hlclient::tests::convex_corner
