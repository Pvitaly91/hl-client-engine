#include "local_movement_test_fixture.hpp"
#include "literal_movement_bsp_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"
#include "collision_brush_test_fixture.hpp"

#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace literal = hlclient::tests::literal_movement_bsp;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_bsp = hlclient::goldsrc::bsp;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;
namespace brush_fixture = hlclient::tests::collision_brush_fixture;

[[nodiscard]] std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
literal_world_package();

[[nodiscard]] goldsrc::ReferencePredictionSeed reference_floor_seed(
    const double z = 36.0, const double velocity_z = 0.0,
    const std::uint32_t flags = 1U << 9U)
{
    goldsrc::ReferencePredictionSeed seed;
    seed.generation = 1U;
    seed.record_identity = 10U;
    seed.command_boundary = *goldsrc::GoldSrcUserCmdSequence::create(7U);
    seed.origin = {0.0, 0.0, z};
    seed.velocity = {0.0, 0.0, velocity_z};
    seed.view_offset = {0.0, 0.0, 28.0};
    seed.base_velocity = {0.0, 0.0, 0.0};
    seed.flags = flags;
    seed.move_type = 3U;
    seed.use_hull = 0U;
    seed.water_level = 0U;
    seed.old_buttons = 0U;
    seed.gravity_multiplier = 1.0;
    seed.friction_multiplier = 1.0;
    return seed;
}

TEST_CASE("Reference seed categorizes literal world ground without moving server origin",
    "[goldsrc][movement][prediction][reference-ground]")
{
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    const goldsrc::GoldSrcWireUserCmd neutral{};
    const auto grounded = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(), neutral, collision, scratch);
    INFO("ground status=" << goldsrc::to_string(grounded.status));
    REQUIRE(grounded.state);
    REQUIRE(grounded.ground_evidence);
    CHECK(grounded.state->origin().z == 36.0F);
    CHECK(grounded.state->ground_state().grounded());
    CHECK(grounded.ground_evidence->hit.has_value());
    CHECK(grounded.ground_evidence->plane.normal.z == Catch::Approx(1.0F));
    CHECK(grounded.collision_identity == collision.session_identity());

    const auto air = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(60.0, 0.0, 0U), neutral, collision, scratch);
    REQUIRE(air.state);
    CHECK_FALSE(air.state->ground_state().grounded());
    CHECK(air.state->origin().z == 60.0F);

    const auto upward = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(36.0, 181.0, 0U), neutral, collision, scratch);
    REQUIRE(upward.state);
    CHECK_FALSE(upward.state->ground_state().grounded());

    const auto solid = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(0.0), neutral, collision, scratch);
    CHECK(solid.status == goldsrc::ReferencePredictionGroundStatus::solid_start);
    CHECK_FALSE(solid.state);

    const auto disagreement = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(36.0, 0.0, 0U), neutral, collision, scratch);
    CHECK(disagreement.status ==
        goldsrc::ReferencePredictionGroundStatus::ground_flag_disagreement);
}

TEST_CASE("D4 reference uphill walk keeps horizontal wish speed and clears grounded vertical velocity",
    "[d4][movement][prediction][reference-ground]")
{
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    auto seed = reference_floor_seed();
    // Authored slope z=.5*x+48, standing support adds .5*16+36.
    seed.origin = {-72.0, -90.0, 56.0001};
    auto ground = goldsrc::derive_reference_prediction_ground(seed, {}, collision, scratch, {},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(ground.state);
    goldsrc::GoldSrcWireUserCmd wire;
    wire.msec = 20U;
    wire.forward = 100;
    auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U), wire);
    REQUIRE(command);
    auto moved = movement::GoldSrcLocalMovementKernel::simulate(*ground.state,
        *command.state, fixture::make_environment(), collision, scratch);
    REQUIRE(moved);
    // Independent PM_WalkMove rule: a=10, wish=100, dt=.02 -> vx=20;
    // unobstructed raised route advances .4 horizontally, .2 vertically.
    CHECK(moved.state->origin().x == Catch::Approx(-71.6F).margin(.001));
    CHECK(moved.state->origin().z == Catch::Approx(56.2F).margin(.001));
    CHECK(moved.state->velocity().x == Catch::Approx(20.0F).margin(.001));
    CHECK(moved.state->velocity().z == 0.0F);
    CHECK(moved.state->ground_state().grounded());
}

TEST_CASE("D4 analytic slopes preserve horizontal acceleration and stationary ground",
    "[d4][movement][prediction][slopes]")
{
    for (float slope : {0.0F, 0.125F, 0.5F, 0.9F, -0.5F, 2.0F}) {
        CAPTURE(slope);
        const auto parsed = goldsrc_bsp::GoldSrcBspParser::parse(literal::make_slope_bsp_v30(slope));
        REQUIRE(parsed);
        const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);
        REQUIRE(built);
        movement::WorldOnlyMovementCollision collision{built.package};
        movement::GoldSrcLocalMovementScratch scratch;
        const double height = 36.0 + std::abs(slope)*16.0;
        auto seed = reference_floor_seed(height + 0.001,0.0,slope > 1.0F ? 0U : 1U<<9U);
        auto ground = goldsrc::derive_reference_prediction_ground(seed,{},collision,scratch,{},
            player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
        REQUIRE(ground.state);
        if (slope > 1.0F) { CHECK_FALSE(ground.state->ground_state().grounded()); continue; }
        // Clientdata origin's 1/128 grid is not a license to accept penetration.
        // A grid point on the free side remains canonical and unshifted.
        const auto wire_z=std::ceil((height+.0001)*128.0)/128.0;
        auto wire_seed=reference_floor_seed(wire_z);
        const auto wire_ground=goldsrc::derive_reference_prediction_ground(wire_seed,{},collision,scratch);
        REQUIRE(wire_ground.state);
        CHECK(wire_ground.state->origin().z == static_cast<float>(wire_z));
        wire_seed.origin.z=wire_z-2.0/128.0;
        CHECK_FALSE(goldsrc::derive_reference_prediction_ground(wire_seed,{},collision,scratch).state);
        for (const int side : {0,100}) {
            goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.forward=side ? 0 : 100; wire.side=static_cast<std::int16_t>(side);
            const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(8),wire);
            REQUIRE(command);
            auto moved = movement::GoldSrcLocalMovementKernel::simulate(*ground.state,*command.state,fixture::make_environment(),collision,scratch);
            REQUIRE(moved);
            CHECK(moved.state->origin().x == Catch::Approx(side ? 0.0 : 0.4).margin(.001));
            CHECK(std::abs(moved.state->origin().y) == Catch::Approx(side ? 0.4 : 0.0).margin(.001));
            CHECK(moved.state->origin().z == Catch::Approx(height + (side ? 0.0 : slope*.4)).margin(.001));
            CHECK(moved.state->velocity().z == 0.0F);
        }
        std::optional<player::LocalPlayerMovementState> state{*ground.state};
        float settled{};
        for (unsigned id=8; id<108; ++id) {
            goldsrc::GoldSrcWireUserCmd wire; wire.msec=20;
            const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(id),wire);
            auto moved = movement::GoldSrcLocalMovementKernel::simulate(*state,*command.state,fixture::make_environment(),collision,scratch);
            REQUIRE(moved);
            state.emplace(*moved.state);
            if (id==8) settled=state->origin().z;
            CHECK(state->origin().z == settled);
            CHECK(state->velocity().z == 0.0F);
            CHECK(state->ground_state().grounded());
        }
    }
}

TEST_CASE("R1 adjacent crest commands admit a continuous camera path",
    "[r1-terrain][prediction]")
{
    const auto parsed=goldsrc_bsp::GoldSrcBspParser::parse(
        literal::make_seamed_slope_bsp_v30(0.5F,-0.5F));
    REQUIRE(parsed);
    const auto built=goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        parsed.document->collision_source);
    REQUIRE(built);
    movement::WorldOnlyMovementCollision collision{built.package};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto floor=collision.trace_hull({-18,0,128},{-18,0,-128},
        player::PlayerMovementHull::standing,scratch.collision);
    REQUIRE(floor);
    REQUIRE(floor.result->collision_plane);
    auto seed=reference_floor_seed(floor.result->end_position.z+0.001);
    seed.origin.x=-18;
    auto initial=goldsrc::derive_reference_prediction_ground(seed,{},collision,scratch,{},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    INFO(goldsrc::to_string(initial.status));
    REQUIRE(initial.state);
    std::optional<player::LocalPlayerMovementState> state{*initial.state};
    bool crossed=false;
    unsigned recovered_count=0;
    for(unsigned id=8;id<108;++id) {
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.forward=60;
        const auto command=goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(id),wire);
        REQUIRE(command);
        const auto moved=movement::GoldSrcLocalMovementKernel::simulate(
            *state,*command.state,fixture::make_environment(),collision,scratch);
        const auto movement_context=moved.error ? moved.error->context : "ready";
        INFO(movement_context);
        REQUIRE(moved);
        const auto& next=*moved.state;
        INFO("ground from=" << state->origin().x << "," << state->origin().z
            << " next=" << next.origin().x << "," << next.origin().z
            << " vertical=" << next.velocity().z);
        REQUIRE(next.ground_state().grounded());
        const auto& a=state->origin(), &b=next.origin();
        const hlclient::assets::AssetVector3 midway{
            (a.x+b.x)*.5F,(a.y+b.y)*.5F,(a.z+b.z)*.5F};
        const auto path=collision.trace_hull(a,midway,next.hull(),scratch.collision);
        REQUIRE(path);
        const auto midway_position=collision.test_position(midway,next.hull(),scratch.collision);
        REQUIRE(midway_position);
        INFO("from=" << a.x << "," << a.z << " to=" << b.x << "," << b.z
            << " mid=" << midway.x << "," << midway.z
            << " fraction=" << path.result->fraction
            << " candidate=" << static_cast<int>(midway_position.result->status));
        CHECK_FALSE(path.result->start_solid);
        if(path.result->fraction<1.0) {
            const auto recovered=goldsrc::recover_reference_ground_presentation(
                *state,next,.5,*path.result,collision,scratch.collision,{});
            REQUIRE(recovered);
            CHECK(recovered->x==Catch::Approx(midway.x));
            CHECK(recovered->y==Catch::Approx(midway.y));
            CHECK(std::abs(recovered->z-midway.z)<=2.0F);
            ++recovered_count;
        }
        crossed=crossed || a.x<0 && b.x>=0;
        state.emplace(next);
        if(b.x>18) break;
    }
    CHECK(crossed);
    CHECK(recovered_count>=3);
}

TEST_CASE("R1 valley and flat slope boundaries keep grounded physical endpoints",
    "[r1-terrain][prediction]") {
    struct TerrainCase { int kind; float x; std::uint16_t yaw; bool duck; std::int16_t forward; };
    const std::array cases{
        TerrainCase{0,-18.0F,0U,false,60}, // valley, downhill then uphill
        TerrainCase{0,-18.0F,8192U,false,60}, // diagonal crossing
        TerrainCase{1,-18.0F,0U,false,60}, // slope to flat
        TerrainCase{1,18.0F,32768U,false,60}, // flat to slope, reverse
        TerrainCase{1,-4.0F,0U,true,60}, // crouch-walk
        TerrainCase{1,-4.0F,0U,false,18}}; // Shift-equivalent quantized axis
    for (const auto terrain : cases) {
        CAPTURE(terrain.kind,terrain.x,terrain.yaw,terrain.duck);
        const auto bytes=terrain.kind==0 ? literal::make_valley_bsp_v30() :
            literal::make_seamed_slope_bsp_v30(0.5F,0.0F);
        const auto parsed=goldsrc_bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE(parsed);
        const auto built=goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
            parsed.document->collision_source);
        REQUIRE(built);
        movement::WorldOnlyMovementCollision collision{built.package};
        movement::GoldSrcLocalMovementScratch scratch;
        const auto hull=terrain.duck ? player::PlayerMovementHull::ducked :
            player::PlayerMovementHull::standing;
        const auto floor=collision.trace_hull({terrain.x,0,128},
            {terrain.x,0,-128},hull,scratch.collision);
        REQUIRE(floor);
        REQUIRE(floor.result->collision_plane);
        auto seed=reference_floor_seed(floor.result->end_position.z+0.001);
        seed.origin.x=terrain.x;
        if (terrain.duck) {
            seed.use_hull=1U; seed.flags|=1U<<14U;
            seed.view_offset.z=12.0;
        }
        const goldsrc::GoldSrcWireUserCmd boundary{};
        const auto initial=goldsrc::derive_reference_prediction_ground(seed,boundary,
            collision,scratch,{},player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
        INFO(goldsrc::to_string(initial.status));
        REQUIRE(initial.state);
        std::optional<player::LocalPlayerMovementState> current{*initial.state};
        bool crossed=false;
        for (unsigned sequence=8U;sequence<108U;++sequence) {
            goldsrc::GoldSrcWireUserCmd wire;
            wire.msec=20U; wire.forward=terrain.forward; wire.angle_turns[1U]=terrain.yaw;
            wire.buttons=goldsrc::kReferenceGoldSrcButtonForward |
                (terrain.duck ? goldsrc::kReferenceGoldSrcButtonDuck : 0U);
            const auto command=goldsrc::reference_jump_duck_weapon_use_movement_command(
                *goldsrc::GoldSrcUserCmdSequence::create(sequence),wire);
            REQUIRE(command);
            const auto moved=movement::GoldSrcLocalMovementKernel::simulate(*current,
                *command.state,fixture::make_environment(),collision,scratch);
            INFO(std::string{moved.error ? moved.error->context : "ready"});
            REQUIRE(moved);
            REQUIRE(moved.state->ground_state().grounded());
            const auto& a=current->origin(); const auto& b=moved.state->origin();
            crossed=crossed || (a.x<0.0F && b.x>=0.0F) || (a.x>0.0F && b.x<=0.0F);
            const auto middle=hlclient::assets::AssetVector3{
                (a.x+b.x)*.5F,(a.y+b.y)*.5F,(a.z+b.z)*.5F};
            const auto path=collision.trace_hull(a,middle,moved.state->hull(),scratch.collision);
            REQUIRE(path);
            if (path.result->fraction<1.0 && !path.result->start_solid) {
                const auto recovered=goldsrc::recover_reference_ground_presentation(
                    *current,*moved.state,.5,*path.result,collision,scratch.collision,{});
                REQUIRE(recovered);
            }
            current.emplace(*moved.state);
            if (terrain.x<0 ? b.x>18.0F : b.x< -18.0F) break;
        }
        INFO("final x=" << current->origin().x << " z=" << current->origin().z);
        CHECK(crossed);
    }
}

TEST_CASE("D4 reference steps respect MoveVars limit and leave their edge without snap-down",
    "[d4][movement][prediction][step]")
{
    for (const float height : {12.0F,18.0F,18.125F}) {
        CAPTURE(height);
        const auto parsed=goldsrc_bsp::GoldSrcBspParser::parse(literal::make_bsp_v30(height));
        REQUIRE(parsed);
        const auto built=goldsrc_collision::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);
        REQUIRE(built);
        movement::WorldOnlyMovementCollision collision{built.package};
        movement::GoldSrcLocalMovementScratch scratch;
        auto seed=reference_floor_seed(); seed.origin.x=15.0;
        const auto initial=goldsrc::derive_reference_prediction_ground(seed,{},collision,scratch,{},
            player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
        REQUIRE(initial.state);
        std::optional<player::LocalPlayerMovementState> state{*initial.state};
        float maximum_z=state->origin().z;
        bool airborne=false;
        for (unsigned id=8; id<73; ++id) {
            goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.forward=100;
            const auto command=goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(id),wire);
            const auto moved=movement::GoldSrcLocalMovementKernel::simulate(*state,*command.state,fixture::make_environment(),collision,scratch);
            INFO((moved.error ? moved.error->context : "success"));
            REQUIRE(moved);
            state.emplace(*moved.state);
            maximum_z=std::max(maximum_z,state->origin().z);
            airborne=airborne || !state->ground_state().grounded();
        }
        if (height<=18.0F) {
            CHECK(maximum_z == Catch::Approx(36.0F+height).margin(.001));
            CHECK(airborne);
            CHECK(state->origin().x>80.0F);
        } else {
            CHECK(maximum_z == 36.0F);
            CHECK(state->origin().x == Catch::Approx(16.0F).margin(.001));
        }
        CHECK(state->origin().z == Catch::Approx(36.0F).margin(.001));
        CHECK(state->ground_state().grounded());
    }
}

TEST_CASE("D4 current server brush context owns hulls and never infers solidity from rendering",
    "[d4][movement][prediction][brush]")
{
    namespace col = hlclient::collision;
    namespace client = hlclient::client;
    const auto original = literal_world_package();
    std::vector<col::CollisionModel> models{original->models().begin(), original->models().end()};
    auto brush = models.front();
    brush.source_model_index = 1U;
    // Reuse authored independent point/standing/duck trees as one brush in an
    // empty world. This is not a mock collision implementation.
    for (auto& hull : models.front().hulls) {
        hull.root.kind = col::CollisionHullRootKind::terminal;
        hull.root.terminal = *col::decode_goldsrc_contents({-1});
    }
    std::vector<col::CollisionNode> nodes{original->nodes().begin(), original->nodes().end()};
    std::vector<col::CollisionLeaf> leaves{original->leaves().begin(), original->leaves().end()};
    const auto empty_leaf = static_cast<std::uint32_t>(leaves.size());
    leaves.push_back({empty_leaf, *col::decode_goldsrc_contents({-1})});
    auto& point_root = models.front().hulls.front().root;
    point_root.kind = col::CollisionHullRootKind::node;
    point_root.index = static_cast<std::uint32_t>(nodes.size());
    nodes.push_back({0U, {{{col::CollisionNodeChildKind::leaf, empty_leaf},
                          {col::CollisionNodeChildKind::leaf, empty_leaf}}}});
    models.push_back(brush);
    const auto package = std::make_shared<const col::CollisionWorldPackage>(
        std::vector<col::CollisionPlane>{original->planes().begin(), original->planes().end()},
        nodes, leaves,
        std::vector<col::CollisionClipnode>{original->clipnodes().begin(), original->clipnodes().end()},
        models, original->identity(), original->statistics());
    const auto library = goldsrc_collision::build_brush_collision_model_library(package);
    REQUIRE(library);
    client::RuntimeClientObservationState observation;
    observation.generation = 1U; observation.publication_revision = 10U;
    observation.entity_metadata.generation = 1U;
    observation.entity_metadata.freshness = client::RuntimeObservationFreshness::observed_in_record;
    observation.entity_metadata.completeness = client::RuntimeObservationCompleteness::complete_reconstruction;
    observation.entity_metadata.source.emplace();
    observation.entity_metadata.source->record_identity = 10U;
    client::RuntimePacketEntityObservation entity;
    entity.entity_number = 42U; entity.model_index = 27U;
    entity.origin = {100.0, 200.0, 50.0}; entity.angles = {0.0, 90.0, 0.0};
    entity.solid = 4U; entity.brush_move_type = 7U;
    entity.effects = 128U; entity.render_mode = 5U; // invisible/blended is still solid
    observation.packet_entities.push_back(entity);
    const std::map<std::uint32_t, std::string> names{{27U,"*1"}};
    auto context = goldsrc::build_reference_brush_collision(library.library, names, observation, 1U);
    REQUIRE(context.scene);
    CHECK(context.solid_count == 1U);
    movement::BrushSceneMovementCollision collision{context.scene};
    REQUIRE(collision.valid());
    movement::GoldSrcLocalMovementScratch scratch;
    auto seed = reference_floor_seed(86.0);
    seed.origin = {100.0,200.0,86.0};
    auto ground = goldsrc::derive_reference_prediction_ground(seed, {}, collision, scratch, {},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(ground.state);
    REQUIRE(ground.state->ground_state().hit());
    CHECK(ground.state->ground_state().hit()->kind == player::PlayerMovementHitKind::brush_entity);
    CHECK(ground.state->ground_state().hit()->source_entity_index == 42U);
    CHECK(ground.state->ground_state().hit()->source_model_index == 1U);
    SECTION("D4 lift continues between sparse observations") {
        hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
        host.reset({1U,1U,1U});
        REQUIRE(host.movement_policy().derived_vertical_support);
        auto previous = context;
        previous.record = 9U; previous.server_time = 0.0;
        auto earlier_observation = observation;
        earlier_observation.packet_entities.front().origin.z = 40.0;
        previous.scene = goldsrc::build_reference_brush_collision(library.library,names,earlier_observation,1U).scene;
        auto current = context; current.server_time = .1;
        goldsrc::ReferenceVerticalSupportMotion support{previous,current};
        REQUIRE(support.active());
        auto timed = player::local_player_movement_state_create_info(*ground.state);
        timed.simulation_time_nanoseconds = 100'000'000U;
        const auto initial = player::LocalPlayerMovementState::create(timed);
        REQUIRE(initial);
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20;
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(8),wire);
        REQUIRE(command);
        // Independent trajectory: z(t)=50+100*t. The server seed already
        // contains displacement through t=0; only the next 20 ms is missing.
        const auto moved = goldsrc::simulate_reference_movement(
            *initial.state,*command.state,fixture::make_environment(),collision,
            scratch,host.movement_policy().movement_config,&support);
        REQUIRE(moved);
        CHECK(moved.state->origin().z == Catch::Approx(88.0).margin(.001));
        CHECK(moved.state->origin().z - 52.0 == Catch::Approx(36.0).margin(.001));
    }
    std::optional<player::LocalPlayerMovementState> state{*ground.state};
    for (unsigned id=8; id<30; ++id) {
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.buttons=4U; // grounded crouch completion
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(id),wire);
        REQUIRE(command);
        auto moved = movement::GoldSrcLocalMovementKernel::simulate(*state,*command.state,
            fixture::make_environment(),collision,scratch);
        INFO((moved.error ? moved.error->context : "success"));
        REQUIRE(moved);
        state.emplace(*moved.state);
    }
    CHECK(state->hull() == player::PlayerMovementHull::ducked);
    CHECK(state->origin().z == Catch::Approx(68.0));
    CHECK(state->ground_state().grounded());
    // One immutable production scene for a delayed command-tail replay. The
    // independent first endpoint is a*wish*dt^2=.4, on the translated floor.
    auto session = hlclient::prediction::create_prediction_session_identity(1U,1U,
        collision,fixture::make_environment(),{},*ground.state,
        hlclient::prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        hlclient::prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);
    REQUIRE(session);
    auto history = hlclient::prediction::LocalPredictionHistoryState::create_initial(*ground.state,*session.session).history;
    REQUIRE(history);
    std::shared_ptr<const player::LocalPlayerMovementState> boundary;
    for (unsigned id=8; id<=14; ++id) {
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.forward=100;
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(id),wire);
        REQUIRE(command);
        const auto before = history->current_predicted_state();
        auto moved = movement::GoldSrcLocalMovementKernel::simulate(*before,*command.state,fixture::make_environment(),collision,scratch);
        REQUIRE(moved);
        if (id==8) CHECK(moved.state->origin().x == Catch::Approx(100.4F).margin(.001));
        CHECK(moved.state->origin().z == Catch::Approx(86.0F).margin(.001));
        auto after = std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
        if (id==10) boundary=after;
        const hlclient::prediction::PredictedCommandAppend append{
            std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),before,after,moved.statistics,
            hlclient::prediction::summarize_prediction_touches(moved.touches,false,false)};
        auto published = hlclient::prediction::append_local_prediction_commands(*history,std::span{&append,1U});
        REQUIRE(published); history=published.history;
    }
    REQUIRE(boundary);
    const auto endpoint = player::local_player_movement_state_signature(*history->current_predicted_state());
    for (unsigned repeat=0; repeat<4; ++repeat) {
        const auto replay = goldsrc::rebase_reference_prediction(*history,*boundary,fixture::make_environment(),collision,scratch,{});
        REQUIRE(replay.history);
        CHECK(replay.replayed_commands == 4U);
        REQUIRE(replay.raw_position_error);
        CHECK(*replay.raw_position_error == 0.0);
        CHECK(player::local_player_movement_state_signature(*replay.history->current_predicted_state()) == endpoint);
        CHECK(history->size() == 7U);
    }
    // Jump detaches from a stationary support and lands without a sticky
    // transform offset; held jump cannot cause another launch on landing.
    state.emplace(*ground.state);
    for (unsigned id=8; id<70; ++id) {
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.buttons=2U;
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(id),wire);
        auto moved = movement::GoldSrcLocalMovementKernel::simulate(*state,*command.state,fixture::make_environment(),collision,scratch);
        REQUIRE(moved);
        CHECK(moved.statistics.jump_count == (id==8 ? 1U : 0U));
        if (id==8) CHECK(moved.state->origin().z == Catch::Approx(86.0+(268.3281573-8.0)*.02).margin(.001));
        state.emplace(*moved.state);
    }
    CHECK(state->ground_state().grounded());
    CHECK(state->origin().z == Catch::Approx(86.0).margin(.001));
    // Explicit limited moving-context contract: current server frame only.
    // Up/down/stop seeds incorporate authoritative pusher displacement once;
    // no local carry is inferred from a transform difference or render FPS.
    for (const double height : {50.0,52.0,54.0,54.0,52.0,50.0}) {
        observation.packet_entities.front().origin.z=height;
        ++observation.publication_revision;
        ++observation.entity_metadata.source->record_identity;
        const auto frame = goldsrc::build_reference_brush_collision(library.library,names,observation,1U);
        REQUIRE(frame.scene);
        movement::BrushSceneMovementCollision frozen{frame.scene};
        seed.origin.z=height+36.0;
        auto standing = goldsrc::derive_reference_prediction_ground(seed,{},frozen,scratch,{},
            player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
        REQUIRE(standing.state);
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20;
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(8),wire);
        const auto idle = movement::GoldSrcLocalMovementKernel::simulate(*standing.state,*command.state,fixture::make_environment(),frozen,scratch);
        REQUIRE(idle);
        CHECK(idle.state->origin().z == Catch::Approx(height+36.0).margin(.001));
        CHECK(idle.state->velocity().z == 0.0F);
        CHECK(context.scene->instances().front().transform.translation.z == 50.0F);
    }
    for (const unsigned solid : {0U,1U}) {
        observation.packet_entities.front().solid = solid;
        auto absent = goldsrc::build_reference_brush_collision(library.library,names,observation,1U);
        REQUIRE(absent.scene);
        movement::BrushSceneMovementCollision nonblocking{absent.scene};
        auto traced = nonblocking.trace_hull({100,200,100},{100,200,0},player::PlayerMovementHull::standing,scratch.collision);
        REQUIRE(traced);
        CHECK(traced.result->fraction == 1.0);
        CHECK_FALSE(traced.result->hit);
    }
    // Ownership: original context cannot change when the observation is reused.
    CHECK(context.scene->instances().size() == 1U);
    observation.packet_entities.front().solid.reset();
    CHECK_FALSE(goldsrc::build_reference_brush_collision(library.library,names,observation,1U).scene);
    CHECK_FALSE(goldsrc::build_reference_brush_collision(library.library,names,observation,2U).scene);
    observation.packet_entities.front().solid=4U;
    observation.packet_entities.front().model_index=28U;
    CHECK_FALSE(goldsrc::build_reference_brush_collision(library.library,names,observation,1U).scene);
    observation.packet_entities.front().model_index=27U;
    observation.entity_metadata.completeness=client::RuntimeObservationCompleteness::unavailable;
    CHECK_FALSE(goldsrc::build_reference_brush_collision(library.library,names,observation,1U).scene);
    observation.entity_metadata.completeness=client::RuntimeObservationCompleteness::complete_reconstruction;
    observation.packet_entities.clear();
    auto removed = goldsrc::build_reference_brush_collision(library.library,names,observation,1U);
    REQUIRE(removed.scene);
    CHECK(removed.scene->instances().empty());
    // Reuse is a fresh committed transform, never an old cached support offset.
    entity.origin.x=1000.0;
    observation.packet_entities.push_back(entity);
    auto reused=goldsrc::build_reference_brush_collision(library.library,names,observation,1U);
    REQUIRE(reused.scene);
    CHECK(reused.scene->instances().front().transform.translation.x==1000.0F);
    CHECK(context.scene->instances().front().transform.translation.x==100.0F);
}

TEST_CASE("R1 runtime ladder skin retains a nonblocking brush hull",
    "[r1-ladder][brush][prediction]") {
    const auto library=goldsrc_collision::build_brush_collision_model_library(
        brush_fixture::bounded_brush_package());
    REQUIRE(library);
    hlclient::client::RuntimeClientObservationState observation;
    observation.generation=1U;
    observation.publication_revision=11U;
    observation.entity_metadata.generation=1U;
    observation.entity_metadata.freshness=hlclient::client::RuntimeObservationFreshness::observed_in_record;
    observation.entity_metadata.completeness=hlclient::client::RuntimeObservationCompleteness::complete_reconstruction;
    observation.entity_metadata.source.emplace();
    observation.entity_metadata.source->record_identity=11U;
    hlclient::client::RuntimePacketEntityObservation entity;
    entity.entity_number=42U;
    entity.model_index=27U;
    entity.solid=0U;
    entity.brush_move_type=7U;
    entity.skin=-16;
    entity.origin={0.0,0.0,0.0};
    entity.angles={0.0,0.0,0.0};
    observation.packet_entities.push_back(entity);
    auto context=goldsrc::build_reference_brush_collision(
        library.library,{{27U,"*1"}},observation,1U);
    REQUIRE(context.scene);
    REQUIRE(context.scene->instances().size()==1U);
    CHECK(context.scene->instances().front().role==goldsrc_collision::BrushCollisionRole::non_solid);
    CHECK(context.solid_count==0U);
    CHECK(context.non_solid_count==1U);
    movement::BrushSceneMovementCollision collision{context.scene};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto through=collision.trace_hull({-5,0,0},{5,0,0},
        player::PlayerMovementHull::standing,scratch.collision);
    REQUIRE(through);
    CHECK(through.result->fraction==1.0);
    observation.packet_entities.front().skin=0;
    const auto ordinary=goldsrc::build_reference_brush_collision(
        library.library,{{27U,"*1"}},observation,1U);
    REQUIRE(ordinary.scene);
    CHECK(ordinary.scene->instances().empty());
}

TEST_CASE("R1 player inside a server-marked ladder climbs without ordinary blockage",
    "[r1-ladder-climb][prediction][brush]") {
    hlclient::game_api::GameClientHost host{
        hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1U,1U,1U});
    REQUIRE(host.movement_policy().reference_ladder);
    const auto* ladder_policy=&*host.movement_policy().reference_ladder;
    const auto library=goldsrc_collision::build_brush_collision_model_library(
        brush_fixture::bounded_brush_package(1U,100.0F));
    REQUIRE(library);
    hlclient::client::RuntimeClientObservationState observation;
    observation.generation=1U;
    observation.publication_revision=11U;
    observation.entity_metadata.generation=1U;
    observation.entity_metadata.freshness=hlclient::client::RuntimeObservationFreshness::observed_in_record;
    observation.entity_metadata.completeness=hlclient::client::RuntimeObservationCompleteness::complete_reconstruction;
    observation.entity_metadata.source.emplace();
    observation.entity_metadata.source->record_identity=11U;
    hlclient::client::RuntimePacketEntityObservation entity;
    entity.entity_number=42U; entity.model_index=27U;
    entity.solid=0U; entity.brush_move_type=7U; entity.skin=-16;
    entity.origin={0.0,0.0,0.0}; entity.angles={0.0,0.0,0.0};
    observation.packet_entities.push_back(entity);
    const auto context=goldsrc::build_reference_brush_collision(
        library.library,{{27U,"*1"}},observation,1U);
    REQUIRE(context.scene);
    REQUIRE(context.ladders.size()==1U);
    movement::BrushSceneMovementCollision collision{context.scene};
    movement::GoldSrcLocalMovementScratch scratch;
    auto seed=reference_floor_seed(0.0,0.0,0U);
    seed.origin={0.75,0.0,0.0};
    const auto contact=goldsrc::query_reference_ladder_contact(context,
        {0.75F,0.0F,0.0F},player::PlayerMovementHull::standing,scratch.collision);
    REQUIRE(contact);
    CHECK(contact->identity==context.ladders.front());
    CHECK(contact->outward_normal.x>0.9F);
    goldsrc::GoldSrcWireUserCmd wire;
    wire.msec=20U;
    wire.angle_turns[1U]=32768U; // look toward the ladder's +X face
    wire.buttons=goldsrc::kReferenceGoldSrcButtonForward;
    const auto initial=goldsrc::derive_reference_prediction_ground(seed,wire,
        collision,scratch,{},player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(initial.state);
    const auto command=goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U),wire);
    REQUIRE(command);
    const auto moved=goldsrc::simulate_reference_movement(*initial.state,
        *command.state,fixture::make_environment(),collision,scratch,{},nullptr,&context,ladder_policy);
    REQUIRE(moved);
    CHECK(moved.state->origin().z>initial.state->origin().z+0.25F);
    CHECK(moved.state->mode()==player::PlayerMovementMode::ladder);
    std::optional<player::LocalPlayerMovementState> current{*moved.state};
    const auto first_height=current->origin().z;
    for (std::uint32_t sequence=9U;sequence<13U;++sequence) {
        wire.buttons=0U;
        const auto idle=goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(sequence),wire);
        REQUIRE(idle);
        const auto stopped=goldsrc::simulate_reference_movement(*current,*idle.state,
            fixture::make_environment(),collision,scratch,{},nullptr,&context,ladder_policy);
        REQUIRE(stopped);
        CHECK(stopped.state->origin().z==first_height);
        current.emplace(*stopped.state);
    }
    wire.buttons=goldsrc::kReferenceGoldSrcButtonBack;
    const auto backward=goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(13U),wire);
    REQUIRE(backward);
    const auto descended=goldsrc::simulate_reference_movement(*current,*backward.state,
        fixture::make_environment(),collision,scratch,{},nullptr,&context,ladder_policy);
    REQUIRE(descended);
    CHECK(descended.state->origin().z<first_height-0.25F);
    current.emplace(*descended.state);
    wire.buttons=goldsrc::kReferenceGoldSrcButtonJump;
    const auto jump=goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(14U),wire);
    REQUIRE(jump);
    const auto jumped=goldsrc::simulate_reference_movement(*current,*jump.state,
        fixture::make_environment(),collision,scratch,{},nullptr,&context,ladder_policy);
    REQUIRE(jumped);
    CHECK(jumped.state->mode()==player::PlayerMovementMode::airborne);
    CHECK(jumped.state->origin().x>current->origin().x+1.0F);
    // Jump-away must not reacquire the same ladder on the very next command
    // while the player's expanded hull still overlaps its trigger volume.
    wire.buttons=0U;
    const auto after_jump=goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(15U),wire);
    REQUIRE(after_jump);
    const auto clearing=goldsrc::simulate_reference_movement(*jumped.state,
        *after_jump.state,fixture::make_environment(),collision,scratch,{},
        nullptr,&context,ladder_policy);
    REQUIRE(clearing);
    CHECK(clearing.state->mode()==player::PlayerMovementMode::airborne);
    CHECK(clearing.state->origin().x>jumped.state->origin().x);
    // No scene contact cannot retain the ladder mode or its gravity exemption.
    auto detached_context=context;
    detached_context.ladders.clear();
    wire.buttons=0U;
    const auto released=goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(14U),wire);
    REQUIRE(released);
    const auto falling=goldsrc::simulate_reference_movement(*current,*released.state,
        fixture::make_environment(),collision,scratch,{},nullptr,&detached_context,ladder_policy);
    INFO(std::string{falling.error ? movement::to_string(falling.error->code) : "no error"});
    REQUIRE(falling);
    CHECK(falling.state->mode()==player::PlayerMovementMode::airborne);
    CHECK(falling.state->origin().z<current->origin().z);
    auto ladder_seed=seed;
    ladder_seed.move_type=5U;
    const auto server_ladder=goldsrc::derive_reference_prediction_ground(
        ladder_seed,wire,collision,scratch,{},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2,&context);
    REQUIRE(server_ladder.state);
    CHECK(server_ladder.state->mode()==player::PlayerMovementMode::ladder);
    const auto no_ladder=goldsrc::derive_reference_prediction_ground(
        ladder_seed,wire,collision,scratch,{},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    CHECK_FALSE(no_ladder.state);
    const auto environment=fixture::make_environment();
    const auto session=hlclient::prediction::create_prediction_session_identity(
        1U,1U,collision,environment,{},*initial.state,
        hlclient::prediction::PredictionCompatibilityProfile::
            reference_carrier_jump_duck_weapon_use_v4,
        hlclient::prediction::PredictionAcknowledgementProfile::
            reference_sent_carrier_boundary_v1);
    REQUIRE(session);
    auto history=hlclient::prediction::LocalPredictionHistoryState::create_initial(
        *initial.state,*session.session).history;
    REQUIRE(history);
    std::shared_ptr<const player::LocalPlayerMovementState> correction;
    for (std::uint32_t sequence : {8U,9U}) {
        goldsrc::GoldSrcWireUserCmd replay_wire;
        replay_wire.msec=20U; replay_wire.angle_turns[1U]=32768U;
        replay_wire.buttons=sequence==8U ? goldsrc::kReferenceGoldSrcButtonForward : 0U;
        const auto cmd=goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(sequence),replay_wire);
        REQUIRE(cmd);
        const auto before=history->current_predicted_state();
        const auto step=goldsrc::simulate_reference_movement(*before,*cmd.state,
            environment,collision,scratch,{},nullptr,&context,ladder_policy);
        REQUIRE(step);
        auto after=std::make_shared<const player::LocalPlayerMovementState>(*step.state);
        if (sequence==8U) correction=after;
        const hlclient::prediction::PredictedCommandAppend append{
            std::make_shared<const goldsrc::GoldSrcUserCmdState>(*cmd.state),
            before,after,step.statistics,
            hlclient::prediction::summarize_prediction_touches(step.touches,false,false)};
        const auto published=hlclient::prediction::append_local_prediction_commands(
            *history,std::span{&append,1U});
        REQUIRE(published); history=published.history;
    }
    REQUIRE(correction);
    const auto predicted_signature=player::local_player_movement_state_signature(
        *history->current_predicted_state());
    const auto rebased=goldsrc::rebase_reference_prediction(*history,*correction,
        environment,collision,scratch,{},nullptr,&context,ladder_policy);
    REQUIRE(rebased.history);
    CHECK(rebased.replayed_commands==1U);
    CHECK(player::local_player_movement_state_signature(
        *rebased.history->current_predicted_state())==predicted_signature);
}

TEST_CASE("D4 vertical support carries once across replay and render cadence",
    "[d4-lift][d4][movement][prediction][brush][game-module]") {
    hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1}); REQUIRE(host.movement_policy().derived_vertical_support);
    const auto config=host.movement_policy().movement_config;
    const auto environment=fixture::make_environment();
    const auto library=goldsrc_collision::build_brush_collision_model_library(hlclient::tests::vertical_lift::package());
    REQUIRE(library);
    auto context = [&](double t,double z,std::uint64_t record) {
        hlclient::client::RuntimeClientObservationState o;
        o.generation=1; o.publication_revision=record;
        o.entity_metadata={1,hlclient::client::RuntimeObservationFreshness::observed_in_record,
            hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,
            hlclient::client::RuntimeObservationSource{record}};
        o.server_time_seconds=t; o.server_time_metadata=o.entity_metadata;
        hlclient::client::RuntimePacketEntityObservation entity;
        entity.entity_number=42; entity.model_index=27; entity.solid=4; entity.brush_move_type=7;
        entity.origin={0.,0.,z}; entity.angles={0.,0.,0.}; o.packet_entities.push_back(entity);
        return goldsrc::build_reference_brush_collision(library.library,{{27,"*1"}},o,1);
    };
    for (const double speed : {100.,-100.,0.}) for (const double fps : {30.,60.,144.,91.}) {
        CAPTURE(speed,fps);
        auto old=context(1.,50.,1), current=context(1.1,50.+speed*.1,2);
        goldsrc::ReferenceVerticalSupportMotion support{old,current};
        REQUIRE(support.active());
        movement::BrushSceneMovementCollision collision{current.scene};
        movement::GoldSrcLocalMovementScratch scratch;
        auto seed=reference_floor_seed(86.+speed*.1);
        auto grounded=goldsrc::derive_reference_prediction_ground(seed,{},collision,scratch,config,
            player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
        REQUIRE(grounded.state);
        auto info=player::local_player_movement_state_create_info(*grounded.state);
        info.simulation_time_nanoseconds=1'100'000'000ULL;
        auto initial=player::LocalPlayerMovementState::create(info); REQUIRE(initial);
        auto identity=hlclient::prediction::create_prediction_session_identity(1,1,collision,environment,config,*initial.state,
            hlclient::prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
            hlclient::prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);
        REQUIRE(identity);
        auto history=hlclient::prediction::LocalPredictionHistoryState::create_initial(*initial.state,*identity.session).history;
        REQUIRE(history);
        unsigned commands=0;
        double next_frame=1.1;
        std::uint64_t record=2;
        for (unsigned id=8;id<=57;++id) {
            goldsrc::GoldSrcWireUserCmd wire; wire.msec=20;
            const auto command=goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(id),wire);
            REQUIRE(command);
            const auto before=history->current_predicted_state();
            auto moved=goldsrc::simulate_reference_movement(*before,*command.state,environment,collision,scratch,config,&support);
            INFO((moved.error ? moved.error->context : "ready")); REQUIRE(moved);
            ++commands;
            const double t=1.1+commands*.02;
            CHECK(moved.state->origin().z == Catch::Approx(86.+speed*(t-1.)).margin(.001));
            CHECK(moved.state->velocity().z==0.F);
            REQUIRE(moved.state->ground_state().hit());
            CHECK(moved.state->ground_state().hit()->source_entity_index==42);
            auto after=std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
            const hlclient::prediction::PredictedCommandAppend append{
                std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),before,after,moved.statistics,
                hlclient::prediction::summarize_prediction_touches(moved.touches,false,false)};
            const auto appended=hlclient::prediction::append_local_prediction_commands(*history,std::span{&append,1U});
            REQUIRE(appended); history=appended.history;
            while(next_frame<=t) {
                const auto sample=goldsrc::sample_reference_support_presentation(*before,*after,(next_frame-(t-.02))/.02,&support,scratch.collision);
                REQUIRE(sample.scene);
                const auto platform=sample.scene->instances().front().transform.translation.z;
                CHECK(sample.origin.z-platform == Catch::Approx(36.).margin(.001));
                movement::BrushSceneMovementCollision presented{sample.scene};
                const auto swept=presented.trace_hull(sample.trace_start,sample.origin,after->hull(),scratch.collision);
                REQUIRE(swept); CHECK_FALSE(swept.result->start_solid); CHECK(swept.result->fraction==1.);
                next_frame+= fps==91. ? (id%3==0 ? .007 : .019) : 1./fps;
            }
            // Uneven, delayed observations; two already-generated commands
            // are replayed. Correction origin/time are independently specified.
            if (commands>=4 && (commands%4==0 || commands%7==0)) {
                const auto boundary=id-2;
                const auto* matched=history->find_exact(*goldsrc::GoldSrcUserCmdSequence::create(boundary)); REQUIRE(matched);
                auto correction=player::local_player_movement_state_create_info(*matched->post_command_state());
                const double source_time=1.1+(commands-2)*.02;
                correction.origin.z=static_cast<float>(86.+speed*(source_time-1.));
                correction.simulation_time_nanoseconds=static_cast<std::uint64_t>(std::llround(source_time*1e9));
                auto corrected=player::LocalPlayerMovementState::create(correction); REQUIRE(corrected);
                auto next=context(source_time,50.+speed*(source_time-1.),++record);
                goldsrc::ReferenceVerticalSupportMotion next_support{current,next};
                REQUIRE(next_support.active());
                movement::BrushSceneMovementCollision next_collision{next.scene};
                const auto rebased=goldsrc::rebase_reference_prediction(*history,*corrected.state,environment,next_collision,scratch,config,&next_support);
                REQUIRE(rebased.history); REQUIRE(rebased.raw_position_error);
                CHECK(*rebased.raw_position_error<=.001);
                CHECK(rebased.replayed_commands==2);
                CHECK(rebased.history->current_predicted_state()->origin().z==Catch::Approx(after->origin().z).margin(.001));
                CHECK(history->current_predicted_state()->source_command_sequence()==id);
                history=rebased.history; old=current; current=next; support=next_support;
                const auto repeated=goldsrc::rebase_reference_prediction(*history,*corrected.state,environment,next_collision,scratch,config,&support);
                REQUIRE(repeated.history); REQUIRE(repeated.raw_position_error);
                CHECK(*repeated.raw_position_error<=.001);
                CHECK(repeated.history->current_predicted_state()->origin().z==Catch::Approx(after->origin().z).margin(.001));
                if(commands==4) {
                    correction.origin.x+=.125F;
                    const auto small=player::LocalPlayerMovementState::create(correction); REQUIRE(small);
                    const auto adjusted=goldsrc::rebase_reference_prediction(*history,*small.state,environment,next_collision,scratch,config,&support);
                    REQUIRE(adjusted.history); REQUIRE(adjusted.raw_position_error);
                    CHECK(*adjusted.raw_position_error==Catch::Approx(.125).margin(.001));
                    CHECK(adjusted.history->current_predicted_state()->origin().z==Catch::Approx(after->origin().z).margin(.001));
                    CHECK(adjusted.history->current_predicted_state()->origin().x==Catch::Approx(.125).margin(.001));
                    // Deliberately do not publish this separate correction into
                    // the independent vertical-only reference trajectory.
                }
            }
        }
        CHECK(commands==50);
        CHECK(history->current_predicted_state()->source_command_sequence()==57);
    }
}

TEST_CASE("D4 finite lift contact detaches and respects clearance and bounded evidence",
    "[d4-lift][d4][movement][prediction][brush]") {
    namespace lift=hlclient::tests::vertical_lift;
    hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1});
    const auto config=host.movement_policy().movement_config;
    bool ceiling=false,ledge=false; unsigned buttons=0; int forward=0; double speed=20.;
    SECTION("jump and landing") { buttons=2; }
    SECTION("walk off edge and land on world") { forward=200; speed=0.; }
    SECTION("step onto stationary world ledge") { forward=200; speed=0.; ledge=true; }
    SECTION("duck and stand") { buttons=4; }
    SECTION("ceiling rejects whole carry transaction") { ceiling=true; speed=100.; }
    SECTION("start stop and small correction") { speed=0.; }
    const auto library=goldsrc_collision::build_brush_collision_model_library(lift::package(ceiling,ledge)); REQUIRE(library);
    double height=ledge ? 0. : 50.;
    auto old=lift::context(library.library,.9,height-speed*.1,1), current=lift::context(library.library,1.,height,2);
    goldsrc::ReferenceVerticalSupportMotion support{old,current}; REQUIRE(support.active());
    movement::BrushSceneMovementCollision collision{current.scene};
    movement::GoldSrcLocalMovementScratch scratch;
    auto seed=reference_floor_seed(height+36);
    auto ground=goldsrc::derive_reference_prediction_ground(seed,{},collision,scratch,config,
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2); REQUIRE(ground.state);
    auto info=player::local_player_movement_state_create_info(*ground.state);
    info.simulation_time_nanoseconds=1'000'000'000ULL;
    auto start=player::LocalPlayerMovementState::create(info); REQUIRE(start);
    std::optional<player::LocalPlayerMovementState> state{*start.state};
    bool detached=false,landed=false,ducked=false,blocked=false,world_ground=false;
    for(unsigned i=1;i<=60;++i) {
        CAPTURE(i,buttons,forward,speed);
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.buttons=static_cast<std::uint16_t>(i<35 ? buttons : 0); wire.forward=static_cast<std::int16_t>(forward);
        const auto cmd=goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(7+i),wire); REQUIRE(cmd);
        const auto moved=goldsrc::simulate_reference_movement(*state,*cmd.state,fixture::make_environment(),collision,scratch,config,&support);
        if(!moved && ceiling) { blocked=true; CHECK(state->origin().z<=124.F); break; }
        INFO((moved.error ? moved.error->context : "ready")); REQUIRE(moved);
        if(!moved.state->ground_state().grounded()) detached=true;
        if(detached && moved.state->ground_state().grounded()) landed=true;
        if(moved.state->hull()==player::PlayerMovementHull::ducked) ducked=true;
        if(moved.state->ground_state().hit() && moved.state->ground_state().hit()->kind==player::PlayerMovementHitKind::world) world_ground=true;
        if(buttons==2 && i==1) {
            CHECK(moved.statistics.jump_count==1);
            CHECK(moved.state->origin().z==Catch::Approx(height+36+(268.3281573-8)*.02).margin(.002));
        }
        state.emplace(*moved.state);
        if(i%4==0) {
            old=current;
            current=lift::context(library.library,1.+i*.02,height+speed*i*.02,2+i);
            support=goldsrc::ReferenceVerticalSupportMotion{old,current}; REQUIRE(support.active());
        }
    }
    if(buttons==2) { CHECK(detached); CHECK(landed); }
    if(forward!=0) CHECK(world_ground);
    if(buttons==4) { CHECK(ducked); CHECK(state->hull()==player::PlayerMovementHull::standing); }
    if(ceiling) CHECK(blocked);
    // Independently specified start/stop boundary and finite horizon. A stop
    // cannot be foreseen; only a pair showing no displacement establishes it.
    auto a=lift::context(library.library,2.,50.,100), b=lift::context(library.library,2.1,60.,101);
    goldsrc::ReferenceVerticalSupportMotion moving{a,b}; REQUIRE(moving.active());
    CHECK(moving.sample(3.)->instances().front().transform.translation.z==Catch::Approx(85.));
    auto c=lift::context(library.library,2.2,60.,102);
    goldsrc::ReferenceVerticalSupportMotion stop{b,c}; REQUIRE(stop.active());
    CHECK(stop.sample(2.3)->instances().front().transform.translation.z==60.F);
    auto foreign=c; foreign.generation=2;
    CHECK_FALSE(goldsrc::ReferenceVerticalSupportMotion{b,foreign}.active());
    foreign=c; foreign.pushers.clear();
    CHECK_FALSE(goldsrc::ReferenceVerticalSupportMotion{b,foreign}.active());
    foreign=c; foreign.scene=std::make_shared<const goldsrc_collision::BrushCollisionScene>(library.library,
        std::vector<goldsrc_collision::BrushCollisionSceneInstance>{},goldsrc_collision::BrushCollisionRoleProviderProfile::reference_entity_solid_bsp_v1);
    goldsrc::ReferenceVerticalSupportMotion removed{b,foreign}; CHECK_FALSE(removed.active());
    CHECK(removed.sample(2.3)->instances().empty());
}

TEST_CASE("D2 G1 Use ground cap precedes duck once without changing wire MoveVars or Use-off physics",
    "[use][game-module][movement][prediction]") {
    hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1U,1U,1U});
    const auto config = host.movement_policy().movement_config;
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto environment = fixture::make_environment();
    auto seed = reference_floor_seed();
    auto ground = goldsrc::derive_reference_prediction_ground(seed, {}, collision, scratch, config,
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(ground.state);
    auto step = [&](const player::LocalPlayerMovementState& state, unsigned id,
                    unsigned buttons, int forward, const movement::GoldSrcLocalMovementConfig& cfg) {
        goldsrc::GoldSrcWireUserCmd wire;
        wire.msec = 20U; wire.buttons = static_cast<std::uint16_t>(buttons);
        wire.forward = static_cast<std::int16_t>(forward);
        auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(id), wire);
        REQUIRE(command);
        auto simulated = movement::GoldSrcLocalMovementKernel::simulate(state, *command.state,
            environment, collision, scratch, cfg);
        INFO((simulated.error ? simulated.error->context : "success"));
        REQUIRE(simulated);
        CHECK(wire.forward == forward);
        CHECK(command.state->forward_move() == forward);
        return *simulated.state;
    };
    const auto ordinary = step(*ground.state,8,0,400,config);
    const auto old_config = step(*ground.state,8,0,400,{});
    CHECK(player::local_player_movement_state_signature(ordinary) == player::local_player_movement_state_signature(old_config));
    const auto use = step(*ground.state,8,32,400,config);
    // From rest: a * dt * capped wishspeed, followed by dt integration.
    const auto cap = environment.maximum_speed() / 3.0F;
    const auto expected_speed = environment.acceleration() * 0.020F * cap;
    CHECK(use.velocity().x == Catch::Approx(expected_speed).margin(0.001));
    CHECK(use.origin().x == Catch::Approx(expected_speed * 0.020F).margin(0.001));
    CHECK(environment.maximum_speed() == fixture::make_environment().maximum_speed());
    const auto repeated = step(*ground.state,8,32,400,config);
    CHECK(player::local_player_movement_state_signature(repeated) == player::local_player_movement_state_signature(use));
    const auto shifted = step(*ground.state,8,32,120,config);
    CHECK(shifted.velocity().x == Catch::Approx(environment.acceleration() * 0.020F * std::min(120.0F, cap)).margin(0.001));
    const auto release = step(use,9,0,400,config);
    CHECK(release.velocity().x > use.velocity().x);
    CHECK(release.old_buttons() == 0U);
    const auto use_jump = step(*ground.state,8,32U|2U,400,config);
    CHECK(use_jump.mode() == player::PlayerMovementMode::airborne);
    CHECK(use_jump.old_buttons() == (32U|2U));
    auto airborne = goldsrc::derive_reference_prediction_ground(reference_floor_seed(60.0,0.0,0U),{},collision,scratch,config,
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(airborne.state);
    const auto air = step(*airborne.state,8,32,400,config);
    const auto air_off = step(*airborne.state,8,0,400,config);
    CHECK(air.origin().x == air_off.origin().x); CHECK(air.velocity().x == air_off.velocity().x);
    CHECK(air.origin().z == air_off.origin().z); CHECK(air.velocity().z == air_off.velocity().z);
    // Complete crouch using the existing 400 ms H4 path, then apply cap BEFORE duck /3.
    auto ducked = std::make_shared<const player::LocalPlayerMovementState>(*ground.state);
    for (unsigned id=8; id<=28; ++id) ducked = std::make_shared<const player::LocalPlayerMovementState>(step(*ducked,id,4,0,config));
    REQUIRE(ducked->hull() == player::PlayerMovementHull::ducked);
    auto duck_use = step(*ducked,29,32U|4U,400,config);
    CHECK(duck_use.velocity().x == Catch::Approx(expected_speed / 3.0F).margin(0.001));
    CHECK(duck_use.origin().x == Catch::Approx(expected_speed / 3.0F * 0.020F).margin(0.001));
    auto bad = goldsrc::GoldSrcWireUserCmd{}; bad.msec=20; bad.buttons=32U|2048U;
    CHECK_FALSE(goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(8),bad));
    bad.buttons=32U;
    CHECK_FALSE(goldsrc::reference_jump_duck_weapon_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(8),bad));
    const auto signature = hlclient::prediction::prediction_movement_config_signature(config);
    CHECK(signature != hlclient::prediction::prediction_movement_config_signature({}));
    const auto session = hlclient::prediction::create_prediction_session_identity(1U,1U,collision,environment,config,*ground.state,
        hlclient::prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        hlclient::prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);
    REQUIRE(session); CHECK(session.session->valid());
    auto history = hlclient::prediction::LocalPredictionHistoryState::create_initial(*ground.state,*session.session).history;
    REQUIRE(history);
    std::shared_ptr<const player::LocalPlayerMovementState> boundary;
    for (unsigned id=8; id<=14; ++id) {
        goldsrc::GoldSrcWireUserCmd wire; wire.msec=20; wire.forward=400; wire.buttons = id<12 ? 32U : 0U;
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(*goldsrc::GoldSrcUserCmdSequence::create(id),wire);
        REQUIRE(command);
        const auto before = history->current_predicted_state();
        auto simulated = movement::GoldSrcLocalMovementKernel::simulate(*before,*command.state,environment,collision,scratch,config);
        REQUIRE(simulated);
        auto after = std::make_shared<const player::LocalPlayerMovementState>(*simulated.state);
        if (id==10) boundary=after;
        const hlclient::prediction::PredictedCommandAppend append{
            std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),before,after,simulated.statistics,
            hlclient::prediction::summarize_prediction_touches(simulated.touches,false,false)};
        auto published = hlclient::prediction::append_local_prediction_commands(*history,std::span{&append,1U});
        REQUIRE(published); history=published.history;
    }
    REQUIRE(boundary);
    const auto endpoint = player::local_player_movement_state_signature(*history->current_predicted_state());
    const auto replay = goldsrc::rebase_reference_prediction(*history,*boundary,environment,collision,scratch,config);
    REQUIRE(replay.history); CHECK(replay.replayed_commands == 4U);
    CHECK(player::local_player_movement_state_signature(*replay.history->current_predicted_state()) == endpoint);
    CHECK(history->size() == 7U);
}

TEST_CASE("Reference action profile predicts jump and complete crouch cycle",
    "[goldsrc][movement][prediction][reference-actions]")
{
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto environment = fixture::make_environment();
    const auto seed = reference_floor_seed();
    const auto initial = goldsrc::derive_reference_prediction_ground(
        seed, {}, collision, scratch, {},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(initial.state);
    auto current = std::make_shared<const player::LocalPlayerMovementState>(
        *initial.state);
    auto step = [&](const std::uint32_t sequence,
                    const std::uint16_t buttons,
                    const std::int16_t forward = 0) {
        goldsrc::GoldSrcWireUserCmd wire;
        wire.msec = 20U;
        wire.buttons = buttons;
        wire.forward = forward;
        const auto command = goldsrc::reference_jump_duck_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(sequence), wire);
        REQUIRE(command);
        const auto result = movement::GoldSrcLocalMovementKernel::simulate(
            *current, *command.state, environment, collision, scratch);
        REQUIRE(result);
        current = std::make_shared<const player::LocalPlayerMovementState>(
            *result.state);
        return result.statistics;
    };
    const auto jump = step(8U, goldsrc::kReferenceGoldSrcButtonJump, 400);
    CHECK(jump.jump_count == 1U);
    CHECK(current->mode() == player::PlayerMovementMode::airborne);
    CHECK(current->origin().z > initial.state->origin().z);
    // Valve's 20 ms jump step moves at impulse minus half of gravity,
    // then retains velocity after the second gravity half-step.
    constexpr double impulse = 268.32815729997475; // sqrt(2 * 800 * 45)
    CHECK(current->origin().z - initial.state->origin().z ==
        Catch::Approx((impulse - 8.0) * 0.020).margin(0.03));
    CHECK(current->velocity().z ==
        Catch::Approx(impulse - 16.0).margin(0.03));
    const auto held = step(9U, goldsrc::kReferenceGoldSrcButtonJump, 400);
    CHECK(held.jump_count == 0U);
    CHECK(current->old_buttons() == goldsrc::kReferenceGoldSrcButtonJump);
    step(10U, 0U, 400);
    CHECK(current->mode() == player::PlayerMovementMode::airborne);
    CHECK(step(11U, goldsrc::kReferenceGoldSrcButtonJump).jump_count == 0U);
    bool landed_while_held = false;
    for (std::uint32_t sequence = 12U; sequence <= 70U; ++sequence) {
        CHECK(step(sequence, goldsrc::kReferenceGoldSrcButtonJump).jump_count ==
            0U);
        landed_while_held = landed_while_held ||
            current->mode() == player::PlayerMovementMode::walking;
    }
    CHECK(landed_while_held);
    step(71U, 0U);
    CHECK(step(72U, goldsrc::kReferenceGoldSrcButtonJump).jump_count == 1U);
    // A separate grounded seed exercises the full 400 ms duck transition.
    current = std::make_shared<const player::LocalPlayerMovementState>(
        *initial.state);
    step(8U, goldsrc::kReferenceGoldSrcButtonDuck);
    CHECK(current->hull() == player::PlayerMovementHull::standing);
    CHECK(current->in_duck_transition());
    CHECK(current->duck_time_milliseconds() == 1000U);
    step(9U, goldsrc::kReferenceGoldSrcButtonDuck);
    CHECK(current->view_offset().z < 28.0F);
    for (std::uint32_t sequence = 10U; sequence <= 27U; ++sequence)
        step(sequence, goldsrc::kReferenceGoldSrcButtonDuck);
    const auto before_completed_duck_eye =
        current->origin().z + current->view_offset().z;
    const auto before_completed_duck_origin = current->origin().z;
    step(28U, goldsrc::kReferenceGoldSrcButtonDuck);
    CHECK(current->hull() == player::PlayerMovementHull::ducked);
    CHECK_FALSE(current->in_duck_transition());
    CHECK(current->view_offset().z == 12.0F);
    CHECK(current->origin().z ==
        Catch::Approx(before_completed_duck_origin - 18.0F).margin(0.05));
    CHECK(std::abs(current->origin().z + current->view_offset().z -
                   before_completed_duck_eye) < 1.0F);
    const auto crouched_x = current->origin().x;
    step(29U, goldsrc::kReferenceGoldSrcButtonDuck, 400);
    CHECK(current->origin().x > crouched_x);
    step(30U, 0U);
    CHECK(current->hull() == player::PlayerMovementHull::standing);
    CHECK(current->view_offset().z == 28.0F);
    CHECK(current->duck_time_milliseconds() == 0U);
}

TEST_CASE("Reference weapon bits leave movement physics active and unchanged",
    "[goldsrc][movement][prediction][reference-actions][weapon-bits]")
{
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto initial = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(), {}, collision, scratch, {},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(initial.state);
    const auto step = [&](const std::uint16_t buttons,
                          const bool weapon_profile) {
        goldsrc::GoldSrcWireUserCmd wire;
        wire.msec = 20U;
        wire.forward = 400;
        wire.buttons = buttons;
        const auto sequence = *goldsrc::GoldSrcUserCmdSequence::create(8U);
        const auto command = weapon_profile
            ? goldsrc::reference_jump_duck_weapon_movement_command(sequence, wire)
            : goldsrc::reference_jump_duck_movement_command(sequence, wire);
        REQUIRE(command);
        const auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
            *initial.state, *command.state, fixture::make_environment(),
            collision, scratch);
        INFO((simulated.error ? simulated.error->context
                              : std::string{"simulation succeeded"}));
        REQUIRE(simulated);
        return *simulated.state;
    };
    const auto ordinary = step(0U, false);
    const auto firing = step(goldsrc::kReferenceGoldSrcButtonAttack, true);
    const auto reloading = step(goldsrc::kReferenceGoldSrcButtonReload, true);
    const auto same_motion = [](const auto& a, const auto& b) {
        return a.origin().x == b.origin().x &&
               a.origin().y == b.origin().y &&
               a.origin().z == b.origin().z &&
               a.velocity().x == b.velocity().x &&
               a.velocity().y == b.velocity().y &&
               a.velocity().z == b.velocity().z;
    };
    CHECK(same_motion(firing, ordinary));
    CHECK(same_motion(reloading, ordinary));
    CHECK(firing.old_buttons() == goldsrc::kReferenceGoldSrcButtonAttack);
    CHECK(reloading.old_buttons() == goldsrc::kReferenceGoldSrcButtonReload);
    const auto jump = step(goldsrc::kReferenceGoldSrcButtonJump, false);
    const auto jump_and_fire = step(
        goldsrc::kReferenceGoldSrcButtonJump |
        goldsrc::kReferenceGoldSrcButtonAttack, true);
    CHECK(same_motion(jump_and_fire, jump));
    const auto duck = step(goldsrc::kReferenceGoldSrcButtonDuck, false);
    const auto duck_and_reload = step(
        goldsrc::kReferenceGoldSrcButtonDuck |
        goldsrc::kReferenceGoldSrcButtonReload, true);
    CHECK(same_motion(duck_and_reload, duck));
    CHECK(duck_and_reload.hull() == duck.hull());
    CHECK(duck_and_reload.duck_time_milliseconds() ==
          duck.duck_time_milliseconds());
}

TEST_CASE("Reference crouch rebase retains exact command phase and suffix",
    "[goldsrc][movement][prediction][reference-actions][reference-rebase]")
{
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto environment = fixture::make_environment();
    const auto initial = goldsrc::derive_reference_prediction_ground(
        reference_floor_seed(), {}, collision, scratch, {},
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(initial.state);
    const auto session = hlclient::prediction::create_prediction_session_identity(
        1U, 1U, collision, environment, {}, *initial.state,
        hlclient::prediction::PredictionCompatibilityProfile::
            reference_carrier_jump_duck_v2,
        hlclient::prediction::PredictionAcknowledgementProfile::
            reference_sent_carrier_boundary_v1);
    REQUIRE(session);
    const auto created = hlclient::prediction::LocalPredictionHistoryState::
        create_initial(*initial.state, *session.session);
    REQUIRE(created);
    auto history = created.history;
    std::shared_ptr<const player::LocalPlayerMovementState> boundary;
    for (std::uint32_t number = 8U; number <= 30U; ++number) {
        goldsrc::GoldSrcWireUserCmd wire;
        wire.msec = 20U;
        wire.buttons = goldsrc::kReferenceGoldSrcButtonDuck;
        wire.forward = 400;
        const auto command = goldsrc::reference_jump_duck_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(number), wire);
        REQUIRE(command);
        const auto before = history->current_predicted_state();
        const auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
            *before, *command.state, environment, collision, scratch);
        REQUIRE(simulated);
        auto after = std::make_shared<const player::LocalPlayerMovementState>(
            *simulated.state);
        if (number == 18U) boundary = after;
        const hlclient::prediction::PredictedCommandAppend append{
            std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),
            before, after, simulated.statistics,
            hlclient::prediction::summarize_prediction_touches(
                simulated.touches, false, false)};
        const auto published = hlclient::prediction::append_local_prediction_commands(
            *history, std::span{&append, 1U});
        REQUIRE(published);
        history = published.history;
    }
    REQUIRE(boundary);
    CHECK(boundary->in_duck_transition());
    CHECK(boundary->duck_time_milliseconds() == 800U);
    const auto before_signature = player::local_player_movement_state_signature(
        *history->current_predicted_state());
    const auto rebased = goldsrc::rebase_reference_prediction(
        *history, *boundary, environment, collision, scratch, {});
    REQUIRE(rebased.history);
    CHECK(rebased.replayed_commands == 12U);
    CHECK(rebased.history->current_predicted_state()->hull() ==
        player::PlayerMovementHull::ducked);
    CHECK(rebased.history->current_predicted_state()->duck_time_milliseconds() ==
        560U);
    CHECK(player::local_player_movement_state_signature(
        *rebased.history->current_predicted_state()) == before_signature);
    CHECK(history->size() == 23U); // old publication remains intact
}

TEST_CASE("Reference correction rebuilds exact suffix and leaves old publication intact",
    "[goldsrc][movement][prediction][reference-rebase]")
{
    const movement::WorldOnlyMovementCollision collision{literal_world_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto environment = fixture::make_environment();
    goldsrc::GoldSrcWireUserCmd wire;
    wire.msec = 20U;
    wire.forward = 400;
    const auto seed = reference_floor_seed();
    const auto ground = goldsrc::derive_reference_prediction_ground(
        seed, wire, collision, scratch);
    INFO("ground status=" << goldsrc::to_string(ground.status));
    REQUIRE(ground.state);
    const auto identity = hlclient::prediction::create_prediction_session_identity(
        1U, 1U, collision, environment, {}, *ground.state,
        hlclient::prediction::PredictionCompatibilityProfile::
            reference_carrier_dry_walk_v1,
        hlclient::prediction::PredictionAcknowledgementProfile::
            reference_sent_carrier_boundary_v1);
    REQUIRE(identity);
    const auto initial = hlclient::prediction::LocalPredictionHistoryState::
        create_initial(*ground.state, *identity.session);
    REQUIRE(initial);
    auto history = initial.history;
    std::shared_ptr<const player::LocalPlayerMovementState> first_post;
    for (const auto number : {8U, 9U}) {
        const auto command = goldsrc::reference_dry_walk_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(number), wire);
        REQUIRE(command);
        const auto before = history->current_predicted_state();
        const auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
            *before, *command.state, environment, collision, scratch);
        REQUIRE(simulated);
        auto after = std::make_shared<const player::LocalPlayerMovementState>(
            *simulated.state);
        if (number == 8U) first_post = after;
        const auto owned_command =
            std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state);
        const hlclient::prediction::PredictedCommandAppend append{
            owned_command, before, after, simulated.statistics,
            hlclient::prediction::summarize_prediction_touches(
                simulated.touches, false, false)};
        const auto published =
            hlclient::prediction::append_local_prediction_commands(
                *history, std::span{&append, 1U});
        REQUIRE(published);
        history = published.history;
    }
    REQUIRE(first_post);
    const auto old_latest_x = history->current_predicted_state()->origin().x;
    auto correction_seed = reference_floor_seed(
        first_post->origin().z, first_post->velocity().z);
    correction_seed.command_boundary =
        *goldsrc::GoldSrcUserCmdSequence::create(8U);
    correction_seed.origin.x = first_post->origin().x + 0.5;
    correction_seed.velocity.x = first_post->velocity().x;
    const auto correction = goldsrc::derive_reference_prediction_ground(
        correction_seed, wire, collision, scratch);
    REQUIRE(correction.state);
    const auto rebased = goldsrc::rebase_reference_prediction(
        *history, *correction.state, environment, collision, scratch);
    REQUIRE(rebased.history);
    CHECK(rebased.replayed_commands == 1U);
    REQUIRE(rebased.raw_position_error);
    CHECK(*rebased.raw_position_error == Catch::Approx(0.5).margin(0.001));
    CHECK(rebased.history->anchor().movement_state()->source_command_sequence()
        == 8U);
    CHECK(rebased.history->current_predicted_state()->source_command_sequence()
        == 9U);
    CHECK(history->current_predicted_state()->origin().x == old_latest_x);
    CHECK(rebased.history->current_predicted_state()->origin().x != old_latest_x);
    const auto repeated_anchor = goldsrc::rebase_reference_prediction(
        *rebased.history, *correction.state, environment, collision, scratch);
    REQUIRE(repeated_anchor.history);
    CHECK(repeated_anchor.replayed_commands == 1U);
    CHECK(repeated_anchor.raw_position_error == Catch::Approx(0.0));
    correction_seed.origin.x = *correction_seed.origin.x + 0.25;
    const auto later_record_same_anchor =
        goldsrc::derive_reference_prediction_ground(
            correction_seed, wire, collision, scratch);
    REQUIRE(later_record_same_anchor.state);
    const auto revised = goldsrc::rebase_reference_prediction(
        *rebased.history, *later_record_same_anchor.state,
        environment, collision, scratch);
    REQUIRE(revised.history);
    CHECK(revised.replayed_commands == 1U);
    CHECK(*revised.raw_position_error == Catch::Approx(0.25).margin(0.001));

    auto missing_info = player::local_player_movement_state_create_info(
        *correction.state);
    missing_info.source_command_sequence = 15U;
    const auto missing_state = player::LocalPlayerMovementState::create(missing_info);
    REQUIRE(missing_state);
    const auto missing = goldsrc::rebase_reference_prediction(
        *history, *missing_state.state, environment, collision, scratch);
    CHECK(missing.status == goldsrc::ReferenceRebaseStatus::boundary_missing);
    CHECK_FALSE(missing.history);
    CHECK(history->current_predicted_state()->origin().x == old_latest_x);
}

[[nodiscard]] std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
literal_world_package()
{
    const auto parsed = goldsrc_bsp::GoldSrcBspParser::parse(
        literal::make_bsp_v30());
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        parsed.document->collision_source);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return built.package;
}

enum class RepeatedCampaign : std::uint8_t {
    settle,
    walk_slide,
    jump_land,
    step,
    duck_stand,
};

struct RepeatedCampaignSummary {
    std::uint64_t signature{0U};
    std::uint64_t trace_count{0U};
    std::uint64_t collision_count{0U};
    std::uint64_t step_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
    std::uint32_t command_count{0U};
    bool grounded{false};
    player::PlayerMovementHull hull{player::PlayerMovementHull::standing};

    [[nodiscard]] friend bool operator==(
        const RepeatedCampaignSummary&,
        const RepeatedCampaignSummary&) = default;
};

struct LiteralKernelCampaign {
    std::optional<player::LocalPlayerMovementState> state;
    std::vector<player::PlayerMovementTouch> touches;
    std::optional<movement::LocalMovementSimulationErrorCode> error;
    std::uint32_t failure_sequence{0U};
    hlclient::assets::AssetVector3 last_origin{};
    std::uint64_t collision_count{0U};
    std::uint64_t step_attempt_count{0U};
    std::uint64_t step_success_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    std::uint64_t stand_blocked_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
};

[[nodiscard]] LiteralKernelCampaign run_literal_commands(
    player::LocalPlayerMovementState initial,
    const movement::WorldOnlyMovementCollision& collision,
    const std::span<const goldsrc::GoldSrcUserCmdState> commands)
{
    LiteralKernelCampaign campaign;
    campaign.state.emplace(std::move(initial));
    campaign.last_origin = campaign.state->origin();
    for (const auto& command : commands) {
        auto simulated = fixture::simulate(*campaign.state, command, collision);
        campaign.collision_count += simulated.statistics.collision_hit_count;
        campaign.step_attempt_count += simulated.statistics.step_attempt_count;
        campaign.step_success_count += simulated.statistics.step_success_count;
        campaign.jump_count += simulated.statistics.jump_count;
        campaign.duck_enter_count += simulated.statistics.duck_enter_count;
        campaign.duck_exit_count += simulated.statistics.duck_exit_count;
        campaign.stand_blocked_count += simulated.statistics.stand_blocked_count;
        campaign.start_solid_count += simulated.statistics.start_solid_count;
        campaign.all_solid_count += simulated.statistics.all_solid_count;
        campaign.touches.insert(
            campaign.touches.end(), simulated.touches.begin(),
            simulated.touches.end());
        if (!simulated || !simulated.state) {
            if (simulated.error) {
                campaign.error = simulated.error->code;
            }
            campaign.failure_sequence = simulated.command_sequence;
            campaign.state.reset();
            return campaign;
        }
        campaign.state.emplace(std::move(*simulated.state));
        campaign.last_origin = campaign.state->origin();
    }
    return campaign;
}

[[nodiscard]] std::vector<goldsrc::GoldSrcUserCmdState> literal_commands(
    const std::uint32_t count,
    const float forward,
    const float side = 0.0F,
    const std::uint16_t buttons = 0U,
    const float yaw = 0.0F)
{
    std::vector<goldsrc::GoldSrcUserCmdState> commands;
    commands.reserve(count);
    for (std::uint32_t sequence = 1U; sequence <= count; ++sequence) {
        commands.push_back(fixture::make_command(
            sequence, 10U, forward, side, buttons, yaw));
    }
    return commands;
}

TEST_CASE("A literal movement BSP owns distinct point standing and duck trees",
    "[goldsrc][movement][integration][literal-bsp][collision]")
{
    const auto bytes = literal::make_bsp_v30();
    REQUIRE(bytes.size() > hlclient::tests::kSyntheticBspHeaderSize);
    const auto parsed = goldsrc_bsp::GoldSrcBspParser::parse(bytes);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.document);

    const auto& source = parsed.document->collision_source;
    REQUIRE(source.models.size() == 1U);
    CHECK(source.planes.size() > 100U);
    CHECK(source.nodes.size() > 40U);
    CHECK(source.clipnodes.size() > 80U);
    CHECK(source.leaves.size() == 3U);
    CHECK(source.leaves[0U].contents.value == -1);
    CHECK(source.leaves[1U].contents.value == -2);
    CHECK(source.leaves[2U].contents.value == -3);

    const auto& model = source.models[0U];
    REQUIRE(std::holds_alternative<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.standing_hull.root));
    REQUIRE(std::holds_alternative<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.duck_hull.root));
    const auto standing_root = std::get<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.standing_hull.root).source_clipnode_index;
    const auto duck_root = std::get<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.duck_hull.root).source_clipnode_index;
    CHECK(standing_root != duck_root);

    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        source);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    CHECK(built.package->models().size() == 1U);
}

TEST_CASE("World-only queries hit every literal movement BSP feature",
    "[goldsrc][movement][integration][literal-bsp][world-only]")
{
    const movement::WorldOnlyMovementCollision collision{
        literal_world_package()};
    hlclient::collision::CollisionQueryScratch scratch;
    REQUIRE(collision.valid());

    const auto open = collision.point_contents({0.0F, 0.0F, 36.0F}, scratch);
    REQUIRE(open);
    CHECK(open.result->contents.category ==
        player::PlayerMovementContents::empty);

    const auto floor_contents = collision.point_contents(
        {0.0F, 0.0F, -1.0F}, scratch);
    REQUIRE(floor_contents);
    CHECK(floor_contents.result->contents.category ==
        player::PlayerMovementContents::solid);

    const auto water = collision.point_contents(
        {-112.0F, 80.0F, 16.0F}, scratch);
    REQUIRE(water);
    CHECK(water.result->contents.category ==
        player::PlayerMovementContents::water);
    CHECK(water.result->contents.source_goldsrc_code == -3);

    const auto corner = collision.point_contents(
        {100.0F, 132.0F, 40.0F}, scratch);
    REQUIRE(corner);
    CHECK(corner.result->contents.category ==
        player::PlayerMovementContents::solid);

    const auto standing_floor = collision.trace_hull(
        {0.0F, 0.0F, 60.0F}, {0.0F, 0.0F, 20.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_floor);
    REQUIRE(standing_floor.result->collision_plane);
    CHECK(standing_floor.result->fraction == Catch::Approx(0.6));
    CHECK(standing_floor.result->end_position.z == Catch::Approx(36.0F));
    CHECK(standing_floor.result->collision_plane->normal.z ==
        Catch::Approx(1.0F));

    const auto duck_floor = collision.trace_hull(
        {0.0F, 0.0F, 60.0F}, {0.0F, 0.0F, 0.0F},
        player::PlayerMovementHull::ducked, scratch);
    REQUIRE(duck_floor);
    CHECK(duck_floor.result->fraction == Catch::Approx(0.7));
    CHECK(duck_floor.result->end_position.z == Catch::Approx(18.0F));

    const auto ceiling = collision.trace_hull(
        {0.0F, 0.0F, 60.0F}, {0.0F, 0.0F, 110.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(ceiling);
    REQUIRE(ceiling.result->collision_plane);
    CHECK(ceiling.result->fraction == Catch::Approx(0.64));
    CHECK(ceiling.result->end_position.z == Catch::Approx(92.0F));
    CHECK(ceiling.result->collision_plane->normal.z ==
        Catch::Approx(-1.0F));

    const auto wall = collision.trace_hull(
        {140.0F, 0.0F, 36.0F}, {190.0F, 0.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(wall);
    REQUIRE(wall.result->collision_plane);
    CHECK(wall.result->fraction == Catch::Approx(0.72));
    CHECK(wall.result->end_position.x == Catch::Approx(176.0F));
    CHECK(wall.result->collision_plane->normal.x == Catch::Approx(-1.0F));

    const auto corner_leg = collision.trace_hull(
        {100.0F, 100.0F, 36.0F}, {100.0F, 140.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(corner_leg);
    REQUIRE(corner_leg.result->collision_plane);
    CHECK(corner_leg.result->fraction == Catch::Approx(0.3));
    CHECK(corner_leg.result->end_position.y == Catch::Approx(112.0F));
    CHECK(corner_leg.result->collision_plane->normal.y ==
        Catch::Approx(-1.0F));

    const auto valid_step = collision.trace_hull(
        {0.0F, 0.0F, 36.0F}, {32.0F, 0.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(valid_step);
    REQUIRE(valid_step.result->collision_plane);
    CHECK(valid_step.result->fraction == Catch::Approx(0.5));
    CHECK(valid_step.result->end_position.x == Catch::Approx(16.0F));
    CHECK(valid_step.result->collision_plane->normal.x ==
        Catch::Approx(-1.0F));

    const auto above_valid_step = collision.trace_hull(
        {0.0F, 0.0F, 49.0F}, {90.0F, 0.0F, 49.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(above_valid_step);
    CHECK(above_valid_step.result->fraction == Catch::Approx(1.0));

    const auto high_step = collision.trace_hull(
        {0.0F, 40.0F, 36.0F}, {64.0F, 40.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(high_step);
    CHECK(high_step.result->fraction == Catch::Approx(0.25));
    CHECK(high_step.result->end_position.x == Catch::Approx(16.0F));

    const auto walkable_ramp = collision.trace_hull(
        {-72.0F, -90.0F, 80.0F}, {-72.0F, -90.0F, 40.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(walkable_ramp);
    REQUIRE(walkable_ramp.result->collision_plane);
    CHECK(walkable_ramp.result->end_position.z == Catch::Approx(56.0F));
    CHECK(walkable_ramp.result->collision_plane->normal.z ==
        Catch::Approx(0.89442719F));
    CHECK(walkable_ramp.result->collision_plane->normal.z >= 0.7F);

    const auto steep_ramp = collision.trace_hull(
        {-16.0F, 90.0F, 100.0F}, {-16.0F, 90.0F, 50.0F},
        player::PlayerMovementHull::ducked, scratch);
    REQUIRE(steep_ramp);
    REQUIRE(steep_ramp.result->collision_plane);
    CHECK(steep_ramp.result->end_position.z == Catch::Approx(82.0F));
    CHECK(steep_ramp.result->collision_plane->normal.z ==
        Catch::Approx(0.44721359F));
    CHECK(steep_ramp.result->collision_plane->normal.z < 0.7F);

    const auto standing_free = collision.test_position(
        {0.0F, 0.0F, 36.0F}, player::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_free);
    CHECK(standing_free.result->status ==
        movement::LocalMovementPositionStatus::free);
    const auto standing_below_floor = collision.test_position(
        {0.0F, 0.0F, 35.0F}, player::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_below_floor);
    CHECK(standing_below_floor.result->status ==
        movement::LocalMovementPositionStatus::blocking);
}

TEST_CASE("The movement kernel traverses the parsed literal BSP deterministically",
    "[goldsrc][movement][kernel][integration][literal-bsp][trajectory]")
{
    const movement::WorldOnlyMovementCollision collision{
        literal_world_package()};
    REQUIRE(collision.valid());

    SECTION("settle on the floor")
    {
        const auto commands = literal_commands(100U, 0.0F);
        const auto first = run_literal_commands(
            fixture::make_state(
                {0.0F, 0.0F, 80.0F}, {},
                player::PlayerMovementMode::airborne),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(
                {0.0F, 0.0F, 80.0F}, {},
                player::PlayerMovementMode::airborne),
            collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK_FALSE(first.error);
        CHECK(first.state->ground_state().grounded());
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("walk up the twelve-unit step")
    {
        const auto commands = literal_commands(35U, 240.0F);
        const auto first = run_literal_commands(
            fixture::make_state(), collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(), collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.step_attempt_count > 0U);
        CHECK(first.step_success_count > 0U);
        CHECK(first.state->ground_state().grounded());
        CHECK(first.state->origin().x > literal::kValidStepMinimumX);
        CHECK(first.state->origin().z == Catch::Approx(48.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
        CHECK(first.step_success_count == second.step_success_count);
    }

    SECTION("the twenty-eight-unit step blocks a floor approach")
    {
        // Repeated commands deliberately exercise binary32 boundary re-entry;
        // retaining forward input at the too-high face is a normal wall event.
        const auto commands = literal_commands(100U, 240.0F);
        const auto first = run_literal_commands(
            fixture::make_state(
                {15.0F, 40.0F, 36.0F}, {240.0F, 0.0F, 0.0F}),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(
                {15.0F, 40.0F, 36.0F}, {240.0F, 0.0F, 0.0F}),
            collision, commands);
        const auto diagnostic_error = first.error
            ? static_cast<unsigned int>(*first.error)
            : std::numeric_limits<unsigned int>::max();
        INFO(first.failure_sequence);
        INFO(diagnostic_error);
        INFO(first.last_origin.x);
        INFO(first.last_origin.y);
        INFO(first.last_origin.z);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.step_attempt_count > 0U);
        CHECK(first.step_success_count == 0U);
        CHECK(first.collision_count > 0U);
        CHECK(first.state->origin().x == Catch::Approx(16.0F));
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("diagonal input slides along the east wall")
    {
        const auto commands = literal_commands(35U, 240.0F, 120.0F);
        const auto first = run_literal_commands(
            fixture::make_state({140.0F, 0.0F, 36.0F}),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state({140.0F, 0.0F, 36.0F}),
            collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.collision_count > 0U);
        CHECK(first.state->origin().x == Catch::Approx(176.0F));
        CHECK(std::abs(first.state->origin().y) > 1.0F);
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("jump and land on the literal floor")
    {
        std::vector<goldsrc::GoldSrcUserCmdState> commands;
        commands.reserve(120U);
        commands.push_back(fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump));
        for (std::uint32_t sequence = 2U; sequence <= 120U; ++sequence) {
            commands.push_back(fixture::make_command(sequence));
        }
        const auto first = run_literal_commands(
            fixture::make_state(), collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(), collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.jump_count == 1U);
        CHECK(first.state->ground_state().grounded());
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("the low ceiling blocks stand until the ducked player exits")
    {
        std::vector<goldsrc::GoldSrcUserCmdState> commands;
        commands.push_back(fixture::make_command(1U));
        for (std::uint32_t sequence = 2U; sequence <= 31U; ++sequence) {
            commands.push_back(fixture::make_command(
                sequence, 10U, -240.0F, 0.0F,
                goldsrc::kSyntheticGoldSrcButtonDuck));
        }
        commands.push_back(fixture::make_command(32U));
        const auto first = run_literal_commands(
            fixture::make_state(
                {-40.0F, 32.0F, 18.0F}, {},
                player::PlayerMovementMode::walking,
                player::PlayerMovementHull::ducked,
                0U, goldsrc::kSyntheticGoldSrcButtonDuck),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(
                {-40.0F, 32.0F, 18.0F}, {},
                player::PlayerMovementMode::walking,
                player::PlayerMovementHull::ducked,
                0U, goldsrc::kSyntheticGoldSrcButtonDuck),
            collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.stand_blocked_count == 1U);
        CHECK(first.duck_exit_count == 1U);
        CHECK(first.state->hull() == player::PlayerMovementHull::standing);
        CHECK(first.state->origin().x < -80.0F);
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("water fails typed without publishing partial movement")
    {
        const auto initial = fixture::make_state(
            {-112.0F, 80.0F, 36.0F}, {},
            player::PlayerMovementMode::airborne);
        const auto initial_signature =
            player::local_player_movement_state_signature(initial);
        const std::array commands{fixture::make_command(1U)};
        const auto result = run_literal_commands(
            initial, collision, commands);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(*result.error == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK(player::local_player_movement_state_signature(initial) ==
            initial_signature);
    }
}

[[nodiscard]] std::optional<RepeatedCampaignSummary> run_repeated_campaign(
    const RepeatedCampaign campaign)
{
    fixture::DeterministicLocalMovementCollision collision;
    std::optional<player::LocalPlayerMovementState> state;
    std::uint32_t command_count = 0U;
    switch (campaign) {
    case RepeatedCampaign::settle:
        state.emplace(fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {},
            player::PlayerMovementMode::airborne));
        command_count = 100U;
        break;
    case RepeatedCampaign::walk_slide:
        collision.add_positive_x_wall(48.0F);
        state.emplace(fixture::make_state());
        command_count = 80U;
        break;
    case RepeatedCampaign::jump_land:
        state.emplace(fixture::make_state());
        command_count = 120U;
        break;
    case RepeatedCampaign::step:
        collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 12.0F);
        state.emplace(fixture::make_state());
        command_count = 50U;
        break;
    case RepeatedCampaign::duck_stand:
        state.emplace(fixture::make_state());
        command_count = 20U;
        break;
    }

    RepeatedCampaignSummary summary;
    for (std::uint32_t sequence = 1U; sequence <= command_count; ++sequence) {
        float forward = 0.0F;
        float side = 0.0F;
        std::uint16_t buttons = 0U;
        if (campaign == RepeatedCampaign::walk_slide) {
            forward = 240.0F;
            side = 160.0F;
        } else if (campaign == RepeatedCampaign::step) {
            forward = sequence <= 30U ? 240.0F : 0.0F;
        } else if (campaign == RepeatedCampaign::jump_land && sequence == 1U) {
            buttons = goldsrc::kSyntheticGoldSrcButtonJump;
        } else if (campaign == RepeatedCampaign::duck_stand &&
            sequence <= 10U) {
            buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
        }
        auto simulated = fixture::simulate(
            *state,
            fixture::make_command(
                sequence, 10U, forward, side, buttons),
            collision);
        if (!simulated || !simulated.state) {
            return std::nullopt;
        }
        summary.trace_count += simulated.statistics.trace_count;
        summary.collision_count +=
            simulated.statistics.collision_hit_count;
        summary.step_count += simulated.statistics.step_success_count;
        summary.jump_count += simulated.statistics.jump_count;
        summary.duck_enter_count += simulated.statistics.duck_enter_count;
        summary.duck_exit_count += simulated.statistics.duck_exit_count;
        summary.start_solid_count +=
            simulated.statistics.start_solid_count;
        summary.all_solid_count += simulated.statistics.all_solid_count;
        state.emplace(std::move(*simulated.state));
    }
    summary.signature =
        player::local_player_movement_state_signature(*state);
    summary.command_count = command_count;
    summary.grounded = state->ground_state().grounded();
    summary.hull = state->hull();
    return summary;
}

TEST_CASE("A local movement command campaign is deterministic end to end",
    "[goldsrc][movement][kernel][integration][determinism]")
{
    const auto run_campaign = [] {
        fixture::DeterministicLocalMovementCollision collision;
        std::optional<player::LocalPlayerMovementState> state{
            fixture::make_state()};
        const std::vector<goldsrc::GoldSrcUserCmdState> commands{
            fixture::make_command(1U, 50U, 200.0F),
            fixture::make_command(
                2U, 10U, 200.0F, 0.0F,
                goldsrc::kSyntheticGoldSrcButtonJump),
            fixture::make_command(
                3U, 20U, 200.0F, -50.0F,
                goldsrc::kSyntheticGoldSrcButtonJump),
            fixture::make_command(4U, 20U, 200.0F, -50.0F),
        };
        std::vector<std::uint64_t> signatures;
        for (const auto& command : commands) {
            auto result = fixture::simulate(*state, command, collision);
            if (!result || !result.state) {
                return std::pair{
                    std::vector<std::uint64_t>{},
                    std::optional<player::LocalPlayerMovementState>{}};
            }
            signatures.push_back(result.deterministic_state_signature);
            state.emplace(std::move(*result.state));
        }
        return std::pair{std::move(signatures), std::move(state)};
    };

    auto [first_signatures, first_state] = run_campaign();
    auto [second_signatures, second_state] = run_campaign();
    REQUIRE(first_state);
    REQUIRE(second_state);
    REQUIRE(first_signatures.size() == 4U);
    CHECK(first_signatures == second_signatures);
    CHECK(player::local_player_movement_state_signature(*first_state) ==
        player::local_player_movement_state_signature(*second_state));
    CHECK(first_state->source_command_sequence() == 4U);
    CHECK(first_state->state_revision() == 5U);
    CHECK(first_state->simulation_time_nanoseconds() == 100'000'000ULL);
    CHECK(first_state->old_buttons() == 0U);
}

TEST_CASE("Repeated local movement campaigns pass 20 out of 20",
    "[goldsrc][movement][kernel][integration][campaign][determinism]")
{
    constexpr std::array campaigns{
        RepeatedCampaign::settle,
        RepeatedCampaign::walk_slide,
        RepeatedCampaign::jump_land,
        RepeatedCampaign::step,
        RepeatedCampaign::duck_stand,
    };
    for (const auto campaign : campaigns) {
        std::optional<RepeatedCampaignSummary> expected;
        for (std::size_t iteration = 0U; iteration < 20U; ++iteration) {
            INFO(static_cast<unsigned int>(campaign));
            INFO(iteration);
            const auto summary = run_repeated_campaign(campaign);
            REQUIRE(summary);
            CHECK(summary->trace_count <=
                static_cast<std::uint64_t>(summary->command_count) * 32U);
            CHECK(summary->start_solid_count == 0U);
            CHECK(summary->all_solid_count == 0U);
            CHECK(summary->grounded);
            CHECK(summary->hull == player::PlayerMovementHull::standing);
            if (expected) {
                CHECK(*summary == *expected);
            } else {
                expected = summary;
            }
        }
        REQUIRE(expected);
        switch (campaign) {
        case RepeatedCampaign::settle: break;
        case RepeatedCampaign::walk_slide:
            CHECK(expected->collision_count > 0U);
            break;
        case RepeatedCampaign::jump_land:
            CHECK(expected->jump_count == 1U);
            break;
        case RepeatedCampaign::step:
            CHECK(expected->step_count > 0U);
            break;
        case RepeatedCampaign::duck_stand:
            CHECK(expected->duck_enter_count == 1U);
            CHECK(expected->duck_exit_count == 1U);
            break;
        }
    }
}

TEST_CASE("Liquid and collision-query failures do not publish partial state",
    "[goldsrc][movement][kernel][integration][transaction]")
{
    const auto initial = fixture::make_state();
    const auto initial_signature =
        player::local_player_movement_state_signature(initial);

    SECTION("initial liquid contents")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_liquid(
            {-8.0F, -8.0F, 30.0F}, {8.0F, 8.0F, 42.0F});
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK_FALSE(result.state);
        CHECK(result.deterministic_state_signature == 0U);
    }

    SECTION("point-contents query error")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.fail_point_contents();
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::collision_query_failed);
        REQUIRE(result.error->collision_error);
        CHECK_FALSE(result.state);
    }

    SECTION("allsolid start")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_box(
            {-32.0F, -32.0F, -16.0F}, {32.0F, 32.0F, 96.0F});
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::player_allsolid);
        CHECK(result.statistics.all_solid_count == 1U);
        CHECK_FALSE(result.state);
    }

    SECTION("startsolid without allsolid")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_box(
            {-32.0F, -32.0F, -16.0F}, {32.0F, 32.0F, 96.0F});
        collision.zero_length_blocking_is_all_solid(false);
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::player_startsolid);
        CHECK(result.statistics.start_solid_count == 1U);
        CHECK_FALSE(result.state);
    }

    CHECK(player::local_player_movement_state_signature(initial) ==
        initial_signature);
}

TEST_CASE("Every dry-walk liquid category and ladder mode fails typed",
    "[goldsrc][movement][kernel][integration][unsupported]")
{
    const auto check_liquid = [](const player::PlayerMovementContents contents) {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_liquid(
            {-8.0F, -8.0F, 30.0F}, {8.0F, 8.0F, 42.0F}, contents);
        return fixture::simulate(
            fixture::make_state(), fixture::make_command(1U), collision);
    };

    for (const auto contents : {
             player::PlayerMovementContents::water,
             player::PlayerMovementContents::slime,
             player::PlayerMovementContents::lava,
             player::PlayerMovementContents::current}) {
        const auto result = check_liquid(contents);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK_FALSE(result.state);
    }

    fixture::DeterministicLocalMovementCollision collision{false};
    const auto ladder = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {},
            player::PlayerMovementMode::unsupported_ladder),
        fixture::make_command(1U), collision);
    REQUIRE_FALSE(ladder);
    REQUIRE(ladder.error);
    CHECK(ladder.error->code == movement::
        LocalMovementSimulationErrorCode::ladder_movement_unsupported);
    CHECK_FALSE(ladder.state);
}

TEST_CASE("Sequence revision and simulation-time bounds fail closed",
    "[goldsrc][movement][kernel][integration][bounds]")
{
    fixture::DeterministicLocalMovementCollision collision;

    SECTION("non-contiguous command")
    {
        const auto result = fixture::simulate(
            fixture::make_state(), fixture::make_command(2U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::invalid_command_sequence);
    }

    SECTION("revision exhaustion")
    {
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {}, player::PlayerMovementMode::walking,
            player::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F, 0U,
            std::numeric_limits<std::uint64_t>::max());
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::state_revision_exhausted);
    }

    SECTION("simulation clock overflow")
    {
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {}, player::PlayerMovementMode::walking,
            player::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F,
            std::numeric_limits<std::uint64_t>::max() - 5'000'000ULL);
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::simulation_time_overflow);
    }
}

} // namespace
