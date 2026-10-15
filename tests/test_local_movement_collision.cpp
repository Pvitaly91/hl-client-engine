#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include "collision_brush_test_fixture.hpp"
#include "literal_movement_bsp_fixture.hpp"
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include "local_movement_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush_models = hlclient::goldsrc::brush_models;
namespace collision = hlclient::collision;
namespace fixture = hlclient::tests::collision_brush_fixture;
namespace movement = hlclient::movement;
namespace movement_collision = hlclient::goldsrc::movement;
namespace scene_collision = hlclient::goldsrc::collision;

[[nodiscard]] std::shared_ptr<const scene_collision::BrushCollisionScene>
literal_box_tangent_scene(const assets::AssetVector3 translation,
    const bool with_wall, const bool with_box, const bool solid_front_wall = false)
{
    namespace literal = hlclient::tests::literal_movement_bsp;
    namespace synthetic = hlclient::tests;
    // The floor, wall and optional box are independently authored half-spaces,
    // then compiled into actual point/standing/duck BSP trees. No server-box
    // sweep or installed asset participates in this geometry oracle.
    std::vector<literal::ConvexVolume> volumes{
        literal::ConvexVolume{{literal::normalized_halfspace(
            {0, 0, 1}, translation.z, false)}, -2}};
    if (with_wall) {
        volumes.push_back({{solid_front_wall ? literal::normalized_halfspace(
            {1, 0, 0}, translation.x + 8, true) : literal::normalized_halfspace(
            {-1, 0, 0}, -(translation.x + 8), false)}, -2});
    }
    if (with_box) {
        volumes.push_back(literal::box(
            {translation.x - 8, translation.y - 8, translation.z + 24},
            {translation.x + 8, translation.y + 8, translation.z + 40}));
    }
    std::vector<synthetic::SyntheticBspPlane> planes{{{0, 0, 1}, 0, 2}};
    std::vector<synthetic::SyntheticBspNode> nodes;
    std::vector<synthetic::SyntheticBspClipnode> clips;
    synthetic::SyntheticBspModel model;
    model.minimum = {-4096, -4096, -4096};
    model.maximum = {4096, 4096, 4096};
    model.headnodes = {literal::compile_point_tree(volumes, planes, nodes),
        literal::compile_clip_tree(volumes, {16, 16, 36}, planes, clips), -1,
        literal::compile_clip_tree(volumes, {16, 16, 18}, planes, clips)};
    model.face_count = 1;
    model.visibility_leaf_count = 1;
    const std::array leaves{
        synthetic::SyntheticBspLeaf{-1, -1, {-4096, -4096, -4096},
            {4096, 4096, 4096}, 0, 0, {}},
        synthetic::SyntheticBspLeaf{-2, -1, {-4096, -4096, -4096},
            {4096, 4096, 4096}, 0, 0, {}}};
    const auto bytes = synthetic::SyntheticBspBuilder{}.set_planes(planes)
        .set_nodes(nodes).set_clipnodes(clips).set_leaves(leaves)
        .set_models(std::span{&model, 1U}).build();
    const auto parsed = hlclient::goldsrc::bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE(parsed);
    const auto built = scene_collision::GoldSrcCollisionWorldBuilder::build(
        parsed.document->collision_source);
    REQUIRE(built);
    const auto library = scene_collision::build_brush_collision_model_library(built.package);
    REQUIRE(library);
    const scene_collision::ExplicitSyntheticBrushCollisionRoleProvider roles{
        std::vector<scene_collision::SyntheticBrushCollisionRoleBinding>{}};
    const auto scene = scene_collision::build_brush_collision_scene(library.library, {}, roles);
    REQUIRE(scene);
    return scene.scene;
}

TEST_CASE("Reference prediction keeps quantized tunnel wall contacts active without freeing penetration",
    "[e32-tunnel][movement][prediction][collision]") {
    namespace g = hlclient::goldsrc;
    const auto scene = literal_box_tangent_scene({-152, 0, -1856}, true, false, true);
    const movement_collision::WorldOnlyMovementCollision provider{scene->model_library()->collision_world()};
    movement_collision::GoldSrcLocalMovementScratch scratch;
    g::ReferencePredictionSeed source;
    source.generation = 1U; source.record_identity = 1U;
    source.command_boundary = *g::GoldSrcUserCmdSequence::create(7U);
    source.origin = {-160.0, -173.2109375, -1819.96875};
    source.velocity = {0.0, 320.0, 0.0}; source.view_offset = {0.0, 0.0, 28.0};
    source.base_velocity = {0.0, 0.0, 0.0}; source.flags = 1U << 9U;
    source.move_type = 3U; source.use_hull = 0U;
    source.gravity_multiplier = 1.0; source.friction_multiplier = 1.0;
    const auto original = provider.test_position({-160, -173.2109375F, -1819.96875F},
        movement::PlayerMovementHull::standing, scratch.collision);
    REQUIRE(original); REQUIRE(original.result->status == movement_collision::LocalMovementPositionStatus::blocking);
    const auto derived = g::derive_reference_prediction_ground(source, {}, provider, scratch, {},
        movement::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    INFO("ground status=" << g::to_string(derived.status));
    const auto boundary = movement_collision::resolve_movement_boundary_position(
        {-160, -173.2109375F, -1819.96875F}, movement::PlayerMovementHull::standing, provider, scratch.collision);
    INFO("resolved=" << static_cast<bool>(boundary));
    REQUIRE(derived.state);
    CHECK(*source.origin.x == -160.0); // canonical observation was not edited
    CHECK(derived.state->origin().x == std::nextafter(-160.F, -std::numeric_limits<float>::infinity()));
    auto state = std::make_shared<const movement::LocalPlayerMovementState>(*derived.state);
    // A previous movement endpoint can also round back onto that same plane.
    auto rounded = movement::local_player_movement_state_create_info(*state);
    rounded.origin.x = -160.F;
    const auto rounded_state = movement::LocalPlayerMovementState::create(rounded);
    REQUIRE(rounded_state);
    state = std::make_shared<const movement::LocalPlayerMovementState>(*rounded_state.state);
    const auto environment = hlclient::tests::local_movement::make_environment();
    for (std::uint32_t sequence = 8U; sequence < 208U; ++sequence) {
        g::GoldSrcWireUserCmd wire; wire.msec = 20U; wire.angle_turns[1] = 16384U; wire.forward = 400;
        const auto command = g::reference_jump_duck_weapon_use_movement_command(*g::GoldSrcUserCmdSequence::create(sequence), wire);
        REQUIRE(command);
        const auto next = g::simulate_reference_movement(*state, *command.state, environment, provider, scratch, {});
        REQUIRE(next); state = std::make_shared<const movement::LocalPlayerMovementState>(*next.state);
        CHECK(state->origin().x < -160.F); CHECK(state->ground_state().grounded());
    }
    CHECK(state->origin().y > 1000.F);
    source.origin.x = -159.99;
    const auto embedded = g::derive_reference_prediction_ground(source, {}, provider, scratch, {},
        movement::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    CHECK_FALSE(embedded.state); CHECK(embedded.status == g::ReferencePredictionGroundStatus::solid_start);
    const movement_collision::ServerEntityCollisionBox box{17U, {{-176.F, -190.F, -1856.F}, {-144.F, -158.F, -1784.F}}};
    const movement_collision::BrushSceneMovementCollision with_box{scene, std::span{&box, 1U}};
    CHECK_FALSE(movement_collision::resolve_movement_boundary_position(
        {-160.F, -173.2109375F, -1819.96875F}, movement::PlayerMovementHull::standing, with_box, scratch.collision));
}

TEST_CASE("Server box corner tangents stay free without suppressing real penetration",
    "[e26][bbox-tangent][server-box][movement]")
{
    struct Shape {
        const char* name;
        std::optional<movement::PlayerMovementHull> hull;
        float half_width;
        float half_height;
    };
    const std::array shapes{
        Shape{"point", std::nullopt, 0, 0},
        Shape{"standing", movement::PlayerMovementHull::standing, 16, 36},
        Shape{"ducked", movement::PlayerMovementHull::ducked, 16, 18}};
    // Exact 1/8 coordinates also exercise realistic nonzero world magnitudes.
    const std::array translations{assets::AssetVector3{},
        assets::AssetVector3{-512.125F, 1024.375F, -4.25F},
        assets::AssetVector3{1000.125F, -512.375F, .125F}};
    for (const auto translation : translations) {
        for (const bool with_wall : {false, true}) {
            INFO("translation=" << translation.x << "," << translation.y << ","
                << translation.z << " wall=" << with_wall);
            const std::array boxes{movement_collision::ServerEntityCollisionBox{
                40, {{translation.x - 8, translation.y - 8, translation.z + 24},
                    {translation.x + 8, translation.y + 8, translation.z + 40}}}};
            const movement_collision::BrushSceneMovementCollision query{
                literal_box_tangent_scene(translation, with_wall, false), boxes};
            const movement_collision::BrushSceneMovementCollision oracle{
                literal_box_tangent_scene(translation, with_wall, true)};
            REQUIRE(query.valid());
            REQUIRE(oracle.valid());
            const auto identity = query.session_identity();
            REQUIRE(identity);
            collision::CollisionQueryScratch scratch;
            for (const auto& shape : shapes) {
                INFO("shape=" << shape.name);
                const auto trace = [&](const auto& adapter, const auto start, const auto end) {
                    return shape.hull ? adapter.trace_hull(start, end, *shape.hull, scratch)
                                      : adapter.trace_point(start, end, scratch);
                };
                const auto check_free = [&](const auto& adapter, const auto start, const auto end) {
                    const auto clear = trace(adapter, start, end);
                    REQUIRE(clear);
                    REQUIRE(clear.result);
                    CHECK_FALSE(clear.result->start_solid);
                    CHECK_FALSE(clear.result->all_solid);
                    CHECK(clear.result->fraction == 1);
                    CHECK_FALSE(clear.result->hit);
                    CHECK_FALSE(clear.result->collision_plane);
                    CHECK(clear.result->end_position.x == end.x);
                    CHECK(clear.result->end_position.y == end.y);
                    CHECK(clear.result->end_position.z == end.z);
                };
                const float left = translation.x - 8 - shape.half_width;
                const float near_y = translation.y - 8 - shape.half_width;
                const float top = translation.z + 40 + shape.half_height;
                const assets::AssetVector3 rising_start{left - 1, translation.y, top - 1};
                const assets::AssetVector3 rising_end{left + 1, translation.y, top + 1};
                const assets::AssetVector3 corner{left, translation.y, top};
                // Each route touches only a free edge/corner; none has a
                // positive-duration interval inside the expanded box.
                const std::array tangents{
                    std::pair{rising_start, rising_end},
                    std::pair{rising_end, rising_start},
                    std::pair{corner, rising_end},
                    std::pair{corner, rising_start},
                    std::pair{assets::AssetVector3{left - 1, near_y + 1, top - 2},
                        assets::AssetVector3{left + 1, near_y - 1, top - 2}},
                    std::pair{assets::AssetVector3{left, near_y, top - 2},
                        assets::AssetVector3{left + 1, near_y - 1, top - 2}}};
                for (const auto& [start, end] : tangents) {
                    check_free(oracle, start, end);
                    check_free(query, start, end);
                    for (const float alpha : {0.F, .25F, .5F, .75F, 1.F}) {
                        const assets::AssetVector3 sample{start.x + (end.x - start.x) * alpha,
                            start.y + (end.y - start.y) * alpha,
                            start.z + (end.z - start.z) * alpha};
                        check_free(oracle, sample, sample);
                        check_free(query, sample, sample);
                        if (shape.hull) {
                            const auto position = query.test_position(sample, *shape.hull, scratch);
                            REQUIRE(position);
                            CHECK(position.result->status == movement_collision::LocalMovementPositionStatus::free);
                        }
                    }
                }
                auto outside_start = rising_start, outside_end = rising_end;
                outside_start.z = std::nextafter(outside_start.z, std::numeric_limits<float>::infinity());
                outside_end.z = std::nextafter(outside_end.z, std::numeric_limits<float>::infinity());
                check_free(oracle, outside_start, outside_end);
                check_free(query, outside_start, outside_end);
                auto inside_start = rising_start, inside_end = rising_end;
                inside_start.z = std::nextafter(inside_start.z, -std::numeric_limits<float>::infinity());
                inside_end.z = std::nextafter(inside_end.z, -std::numeric_limits<float>::infinity());
                const std::array penetrations{
                    std::pair{inside_start, inside_end},
                    std::pair{assets::AssetVector3{left - 2, translation.y, top - 2},
                        assets::AssetVector3{left + 2, translation.y, top - 2}}};
                for (const auto& [start, end] : penetrations) {
                    const auto expected = trace(oracle, start, end);
                    const auto actual = trace(query, start, end);
                    REQUIRE(expected);
                    REQUIRE(actual);
                    REQUIRE(expected.result->collision_plane);
                    REQUIRE(actual.result->collision_plane);
                    REQUIRE(actual.result->hit);
                    CHECK_FALSE(actual.result->start_solid);
                    CHECK_FALSE(actual.result->all_solid);
                    CHECK(actual.result->fraction < 1);
                    CHECK(actual.result->fraction == Catch::Approx(expected.result->fraction).margin(.00001));
                    CHECK(actual.result->collision_plane->normal.x == -1);
                    CHECK(actual.result->hit->kind == movement::PlayerMovementHitKind::server_entity_box);
                    CHECK(actual.result->hit->source_entity_index == 40);
                }
                // A boundary-to-interior downward probe must still support the
                // player, including with the wall behind the physical box.
                const assets::AssetVector3 support{left + 2, translation.y, top};
                const assets::AssetVector3 below{support.x, support.y, top - 2};
                const auto landed = trace(query, support, below);
                REQUIRE(landed);
                REQUIRE(landed.result->collision_plane);
                REQUIRE(landed.result->hit);
                CHECK(landed.result->fraction == 0);
                CHECK(landed.result->collision_plane->normal.z == 1);
                CHECK(landed.result->hit->source_entity_index == 40);
            }
            REQUIRE(query.entity_boxes().size() == 1);
            CHECK(query.entity_boxes()[0].entity == boxes[0].entity);
            CHECK(query.entity_boxes()[0].bounds.minimum.x == boxes[0].bounds.minimum.x);
            CHECK(query.entity_boxes()[0].bounds.minimum.y == boxes[0].bounds.minimum.y);
            CHECK(query.entity_boxes()[0].bounds.minimum.z == boxes[0].bounds.minimum.z);
            CHECK(query.entity_boxes()[0].bounds.maximum.x == boxes[0].bounds.maximum.x);
            CHECK(query.entity_boxes()[0].bounds.maximum.y == boxes[0].bounds.maximum.y);
            CHECK(query.entity_boxes()[0].bounds.maximum.z == boxes[0].bounds.maximum.z);
            CHECK(query.session_identity() == identity);
        }
    }
}

TEST_CASE("Zero-width server box is free for points but blocks expanded player hulls",
    "[e26][bbox-tangent][server-box][movement]")
{
    // A point never occupies the strict interior of a zero-width box. The
    // player hull still expands this same committed shape into real volume.
    const std::array boxes{movement_collision::ServerEntityCollisionBox{
        55, {{0, -1, 50}, {0, 1, 52}}}};
    const movement_collision::BrushSceneMovementCollision query{
        literal_box_tangent_scene({}, false, false), boxes};
    REQUIRE(query.valid());
    collision::CollisionQueryScratch scratch;
    const auto point = query.trace_point({-1, 0, 51}, {1, 0, 51}, scratch);
    REQUIRE(point);
    REQUIRE(point.result);
    CHECK_FALSE(point.result->start_solid);
    CHECK_FALSE(point.result->all_solid);
    CHECK(point.result->fraction == 1);
    CHECK_FALSE(point.result->hit);
    CHECK_FALSE(point.result->collision_plane);
    CHECK(point.result->end_position.x == 1);
    for (const float x : {-1.F, 0.F, 1.F}) {
        const assets::AssetVector3 position{x, 0, 51};
        const auto occupancy = query.trace_point(position, position, scratch);
        REQUIRE(occupancy);
        CHECK_FALSE(occupancy.result->start_solid);
        CHECK_FALSE(occupancy.result->all_solid);
        CHECK(occupancy.result->fraction == 1);
        CHECK_FALSE(occupancy.result->hit);
    }
    for (const auto hull : {movement::PlayerMovementHull::standing,
             movement::PlayerMovementHull::ducked}) {
        const auto expanded = query.trace_hull({-32, 0, 51}, {32, 0, 51}, hull, scratch);
        REQUIRE(expanded);
        REQUIRE(expanded.result->hit);
        REQUIRE(expanded.result->collision_plane);
        CHECK_FALSE(expanded.result->start_solid);
        CHECK_FALSE(expanded.result->all_solid);
        CHECK(expanded.result->fraction == Catch::Approx(.25).margin(.000001));
        CHECK(expanded.result->collision_plane->normal.x == -1);
        CHECK(expanded.result->hit->kind == movement::PlayerMovementHitKind::server_entity_box);
        CHECK(expanded.result->hit->source_entity_index == 55);
        const auto occupancy = query.test_position({0, 0, 51}, hull, scratch);
        REQUIRE(occupancy);
        CHECK(occupancy.result->status == movement_collision::LocalMovementPositionStatus::blocking);
    }
    REQUIRE(query.entity_boxes().size() == 1);
    CHECK(query.entity_boxes()[0].entity == 55);
    CHECK(query.entity_boxes()[0].bounds.minimum.x == 0);
    CHECK(query.entity_boxes()[0].bounds.maximum.x == 0);
}

TEST_CASE("Neutral presentation point query uses the same bounded brush box union without player expansion",
          "[e26][placement-query][movement][game-api]") {
    const auto library=scene_collision::build_brush_collision_model_library(fixture::package(false));REQUIRE(library);
    const scene_collision::ExplicitSyntheticBrushCollisionRoleProvider roles{
        std::vector<scene_collision::SyntheticBrushCollisionRoleBinding>{}};
    const auto scene=scene_collision::build_brush_collision_scene(library.library,{},roles);REQUIRE(scene);
    const std::array boxes{movement_collision::ServerEntityCollisionBox{40,{{-4,-4,0},{4,4,8}}}};
    const movement_collision::BrushSceneMovementCollision query{scene.scene,boxes};collision::CollisionQueryScratch scratch;
    const auto point=query.trace_point({-8,0,4},{8,0,4},scratch);REQUIRE(point);REQUIRE(point.result->hit);
    CHECK(point.result->hit->source_entity_index==40);CHECK(point.result->fraction==Catch::Approx(.25).margin(.003));
    CHECK_FALSE(point.result->start_solid);
    const auto expanded=query.trace_hull({-8,0,4},{8,0,4},movement::PlayerMovementHull::standing,scratch);
    REQUIRE(expanded);CHECK(expanded.result->start_solid);
    const auto invalid=query.trace_point({std::numeric_limits<float>::quiet_NaN(),0,0},{8,0,4},scratch);
    CHECK_FALSE(invalid);REQUIRE(invalid.error);CHECK(invalid.error->code==movement_collision::LocalMovementCollisionErrorCode::invalid_segment);
}

TEST_CASE("Box step eye keeps a free hull route and never smooths a blocked wall or ceiling",
          "[e25][cluster-step-camera][movement][game-api]") {
    namespace g=hlclient::goldsrc;namespace player=hlclient::movement;
    const auto parsed=g::bsp::GoldSrcBspParser::parse(hlclient::tests::literal_movement_bsp::make_slope_bsp_v30(0));REQUIRE(parsed);
    const auto built=g::collision::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);REQUIRE(built);
    const auto library=scene_collision::build_brush_collision_model_library(built.package);REQUIRE(library);
    const scene_collision::ExplicitSyntheticBrushCollisionRoleProvider roles{std::vector<scene_collision::SyntheticBrushCollisionRoleBinding>{}};
    const auto scene=scene_collision::build_brush_collision_scene(library.library,{},roles);REQUIRE(scene);
    const std::array boxes{movement_collision::ServerEntityCollisionBox{40,{{36,-4,0},{44,4,8}}},
      movement_collision::ServerEntityCollisionBox{41,{{40,-4,0},{48,4,8}}},
      movement_collision::ServerEntityCollisionBox{42,{{44,-4,0},{52,4,8}}}};
    movement_collision::BrushSceneMovementCollision collision{scene.scene,boxes};collision::CollisionQueryScratch scratch;
    player::LocalPlayerMovementStateCreateInfo info;info.origin={19.6F,0,36};info.view_offset={0,0,28};
    info.source_command_sequence=1;info.mode=player::PlayerMovementMode::walking;
    info.ground.grounded=info.ground.walkable=true;info.ground.hit=player::PlayerMovementHitIdentity{};
    info.ground.plane={{0,0,1},36,{}};info.ground.contact_position=info.origin;info.ground.probe_fraction=0;
    const auto from=player::LocalPlayerMovementState::create(info);REQUIRE(from.state);
    info.origin={22,0,44};info.source_command_sequence=2;
    info.ground.hit=player::PlayerMovementHitIdentity{player::PlayerMovementHitKind::server_entity_box,0,40,40};
    info.ground.plane.distance=44;info.ground.contact_position=info.origin;
    const auto to=player::LocalPlayerMovementState::create(info);REQUIRE(to.state);
    const auto before=to.state->origin();
    for(const double alpha:{.001,.05,.5,.95,.999}) {
      const auto sample=g::sample_reference_box_step_eye(*from.state,*to.state,alpha,18,collision,scratch,{});REQUIRE(sample);
      CHECK(sample->origin.z==44);CHECK(sample->origin.z+sample->view_offset.z==Catch::Approx(64+8*alpha).margin(.00001));
      const auto free=collision.test_position(sample->origin,to.state->hull(),scratch);REQUIRE(free);
      CHECK(free.result->status==movement_collision::LocalMovementPositionStatus::free);
      const auto reverse=g::sample_reference_box_step_eye(*to.state,*from.state,alpha,18,collision,scratch,{});
      REQUIRE(reverse);
      CHECK(reverse->origin.z+reverse->view_offset.z==Catch::Approx(72-8*alpha).margin(.00001));
    }
    CHECK(to.state->origin().z==before.z); // presentation never edits history/physics
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,.5,0,collision,scratch,{}));
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,.5,7,collision,scratch,{}));
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,0,18,collision,scratch,{}));
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,1,18,collision,scratch,{}));
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,std::numeric_limits<double>::quiet_NaN(),18,collision,scratch,{}));
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,.5,65,collision,scratch,{}));
    info.ground={};
    info.mode=player::PlayerMovementMode::airborne;
    const auto airborne=player::LocalPlayerMovementState::create(info);REQUIRE(airborne.state);
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*airborne.state,.5,18,collision,scratch,{}));
    auto blocked=std::vector<movement_collision::ServerEntityCollisionBox>{boxes.begin(),boxes.end()};
    blocked.push_back({80,{{19,-100,73},{21,100,100}}}); // raised route enters this ceiling, although original endpoint is free
    movement_collision::BrushSceneMovementCollision ceiling{scene.scene,blocked};
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,.5,18,ceiling,scratch,{}));
    blocked.back()={80,{{20,-100,0},{21,100,200}}};
    movement_collision::BrushSceneMovementCollision wall{scene.scene,blocked};
    CHECK_FALSE(g::sample_reference_box_step_eye(*from.state,*to.state,.5,18,wall,scratch,{}));
  }

TEST_CASE("Server entity boxes share the world movement query and standing duck hulls",
          "[movement][collision][server-box][e25]") {
    const auto library=scene_collision::build_brush_collision_model_library(fixture::package(false));
    REQUIRE(library);
    const scene_collision::ExplicitSyntheticBrushCollisionRoleProvider roles{
        std::vector<scene_collision::SyntheticBrushCollisionRoleBinding>{}};
    const auto scene=scene_collision::build_brush_collision_scene(library.library,{},roles);
    REQUIRE(scene);
    const std::array boxes{movement_collision::ServerEntityCollisionBox{40,{{-4,-4,0},{4,4,8}}}};
    const movement_collision::BrushSceneMovementCollision with_box{scene.scene,boxes}, without_box{scene.scene};
    REQUIRE(with_box.valid());
    CHECK(with_box.session_identity()->scene_signature!=without_box.session_identity()->scene_signature);
    collision::CollisionQueryScratch scratch;
    for (const auto hull:{movement::PlayerMovementHull::standing,movement::PlayerMovementHull::ducked}) {
        const float z=hull==movement::PlayerMovementHull::standing ? 36.F : 18.F;
        const auto hit=with_box.trace_hull({-50,0,z},{50,0,z},hull,scratch);
        REQUIRE(hit); REQUIRE(hit.result->hit); REQUIRE(hit.result->collision_plane);
        CHECK(hit.result->fraction==Catch::Approx(.3).margin(1e-6));
        CHECK(hit.result->collision_plane->normal.x==-1.F);
        CHECK(hit.result->hit->kind==movement::PlayerMovementHitKind::server_entity_box);
        CHECK(hit.result->hit->source_entity_index==40U);
        const auto clear=without_box.trace_hull({-50,0,z},{50,0,z},hull,scratch);
        REQUIRE(clear); CHECK(clear.result->fraction==1);
        // Boundary is a free player position, downward probe is its support.
        const auto top=with_box.test_position({0,0,z+8},hull,scratch);
        REQUIRE(top); CHECK(top.result->status==movement_collision::LocalMovementPositionStatus::free);
        const auto support=with_box.trace_hull({0,0,z+8},{0,0,z+6},hull,scratch);
        REQUIRE(support); REQUIRE(support.result->collision_plane);
        CHECK(support.result->fraction==0); CHECK(support.result->collision_plane->normal.z==1.F);
        const auto embedded=with_box.test_position({0,0,z},hull,scratch);
        REQUIRE(embedded); CHECK(embedded.result->status==movement_collision::LocalMovementPositionStatus::blocking);
    }
    auto invalid=boxes;invalid[0].bounds.minimum.x=10;
    CHECK_FALSE(movement_collision::BrushSceneMovementCollision{scene.scene,invalid}.valid());
    std::vector<movement_collision::ServerEntityCollisionBox> too_many(129,boxes[0]);
    CHECK_FALSE(movement_collision::BrushSceneMovementCollision{scene.scene,too_many}.valid());
    CHECK(boxes[0].bounds.minimum.x==-4); // queries never mutate committed shapes
}

[[nodiscard]] brush_models::BrushRigidTransform rigid(
    const assets::AssetVector3 origin,
    const assets::AssetVector3 angles = {})
{
    const auto built = brush_models::make_brush_rigid_transform(origin, angles);
    REQUIRE(built);
    REQUIRE(built.transform);
    return *built.transform;
}

[[nodiscard]] std::shared_ptr<const scene_collision::BrushCollisionScene>
single_solid_brush_scene(
    const assets::AssetVector3 origin = {10.0F, 0.0F, 0.0F},
    const assets::AssetVector3 angles = {})
{
    auto package = fixture::package(false);
    const auto library =
        scene_collision::build_brush_collision_model_library(package);
    REQUIRE(library);
    REQUIRE(library.library);

    const scene_collision::BrushCollisionInstanceIdentity identity{
        7U, 1U, 42U};
    const std::array definitions{
        scene_collision::BrushCollisionInstanceDefinition{
            identity, rigid(origin, angles)},
    };
    const scene_collision::ExplicitSyntheticBrushCollisionRoleProvider provider{
        std::vector<scene_collision::SyntheticBrushCollisionRoleBinding>{
            {identity, scene_collision::BrushCollisionRole::solid},
        }};
    const auto built = scene_collision::build_brush_collision_scene(
        library.library, definitions, provider);
    REQUIRE(built);
    REQUIRE(built.scene);
    return built.scene;
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
repackaged_world(
    const std::shared_ptr<const collision::CollisionWorldPackage>& source,
    collision::CollisionWorldIdentity identity,
    const double first_plane_distance_adjustment = 0.0)
{
    REQUIRE(source);
    std::vector<collision::CollisionPlane> planes{
        source->planes().begin(), source->planes().end()};
    REQUIRE_FALSE(planes.empty());
    planes.front().distance += first_plane_distance_adjustment;
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::move(planes),
        std::vector<collision::CollisionNode>{
            source->nodes().begin(), source->nodes().end()},
        std::vector<collision::CollisionLeaf>{
            source->leaves().begin(), source->leaves().end()},
        std::vector<collision::CollisionClipnode>{
            source->clipnodes().begin(), source->clipnodes().end()},
        std::vector<collision::CollisionModel>{
            source->models().begin(), source->models().end()},
        std::move(identity), source->statistics(),
        source->compatibility_profile(), source->evidence_profile());
}

[[nodiscard]] movement_collision::LocalMovementCollisionQueryConfig
invalid_query_config()
{
    movement_collision::LocalMovementCollisionQueryConfig config;
    config.query_limits.maximum_traversal_steps = 0U;
    return config;
}

[[nodiscard]] collision::CollisionContents decoded(const std::int32_t raw)
{
    const auto value = collision::decode_goldsrc_contents({raw});
    REQUIRE(value);
    return *value;
}

} // namespace

TEST_CASE("Local movement contents normalization is exact and exhaustive",
    "[goldsrc][movement][collision][contents]")
{
    struct Case {
        std::int32_t raw;
        movement::PlayerMovementContents expected;
    };
    constexpr std::array cases{
        Case{-1, movement::PlayerMovementContents::empty},
        Case{-2, movement::PlayerMovementContents::solid},
        Case{-3, movement::PlayerMovementContents::water},
        Case{-4, movement::PlayerMovementContents::slime},
        Case{-5, movement::PlayerMovementContents::lava},
        Case{-6, movement::PlayerMovementContents::sky},
        Case{-7, movement::PlayerMovementContents::special},
        Case{-8, movement::PlayerMovementContents::special},
        Case{-9, movement::PlayerMovementContents::current},
        Case{-10, movement::PlayerMovementContents::current},
        Case{-11, movement::PlayerMovementContents::current},
        Case{-12, movement::PlayerMovementContents::current},
        Case{-13, movement::PlayerMovementContents::current},
        Case{-14, movement::PlayerMovementContents::current},
        Case{-15, movement::PlayerMovementContents::special},
    };

    for (const auto& test : cases) {
        INFO("GoldSrc contents=" << test.raw);
        const auto normalized =
            movement_collision::normalize_local_movement_contents(
                decoded(test.raw));
        REQUIRE(normalized);
        CHECK(normalized->category == test.expected);
        CHECK(normalized->source_goldsrc_code == test.raw);
    }

    const collision::CollisionContents invalid{
        {-999}, static_cast<collision::CollisionContentsCategory>(255U)};
    CHECK_FALSE(
        movement_collision::normalize_local_movement_contents(invalid));
}

TEST_CASE("Local movement hull mapping uses exact compiler hull ordinals",
    "[goldsrc][movement][collision][hull]")
{
    CHECK(movement_collision::local_movement_collision_hull(
              movement::PlayerMovementHull::standing) ==
        collision::CollisionHullOrdinal::standing_32x32x72);
    CHECK(movement_collision::local_movement_collision_hull(
              movement::PlayerMovementHull::ducked) ==
        collision::CollisionHullOrdinal::duck_32x32x36);
    CHECK_FALSE(movement_collision::local_movement_collision_hull(
        static_cast<movement::PlayerMovementHull>(255U)));
}

TEST_CASE("World-only movement collision preserves world query evidence",
    "[goldsrc][movement][collision][world]")
{
    const auto package = fixture::package(true, 0.0);
    const movement_collision::WorldOnlyMovementCollision adapter{package};
    collision::CollisionQueryScratch scratch;

    REQUIRE(adapter.valid());
    CHECK(adapter.package() == package);
    CHECK(adapter.profile() ==
        movement_collision::LocalMovementCollisionProfile::world_only_v1);

    const auto open = adapter.point_contents({1.0F, 0.0F, 0.0F}, scratch);
    REQUIRE(open);
    REQUIRE(open.result);
    CHECK_FALSE(open.error);
    CHECK(open.result->contents.category ==
        movement::PlayerMovementContents::empty);
    CHECK(open.result->contents.source_goldsrc_code == -1);
    CHECK(open.result->traversal_depth == 1U);

    const auto solid = adapter.point_contents({-1.0F, 0.0F, 0.0F}, scratch);
    REQUIRE(solid);
    CHECK(solid.result->contents.category ==
        movement::PlayerMovementContents::solid);
    CHECK(solid.result->contents.source_goldsrc_code == -2);

    const auto standing_free = adapter.test_position(
        {1.0F, 0.0F, 0.0F}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_free);
    CHECK(standing_free.result->status ==
        movement_collision::LocalMovementPositionStatus::free);
    CHECK_FALSE(standing_free.result->hit);

    const auto ducked_blocked = adapter.test_position(
        {-1.0F, 0.0F, 0.0F}, movement::PlayerMovementHull::ducked, scratch);
    REQUIRE(ducked_blocked);
    CHECK(ducked_blocked.result->status ==
        movement_collision::LocalMovementPositionStatus::blocking);
    CHECK(ducked_blocked.result->contents.category ==
        movement::PlayerMovementContents::solid);

    const auto traced = adapter.trace_hull(
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F},
        movement::PlayerMovementHull::standing, scratch);
    REQUIRE(traced);
    REQUIRE(traced.result);
    CHECK(traced.result->collision_profile ==
        movement_collision::LocalMovementCollisionProfile::world_only_v1);
    CHECK_FALSE(traced.result->all_solid);
    CHECK_FALSE(traced.result->start_solid);
    CHECK(traced.result->in_open);
    CHECK_FALSE(traced.result->in_liquid);
    CHECK(traced.result->fraction == Catch::Approx(0.5));
    CHECK(traced.result->end_position.x == Catch::Approx(0.0F));
    REQUIRE(traced.result->collision_plane);
    CHECK(traced.result->collision_plane->normal.x == 1.0F);
    CHECK(traced.result->collision_plane->distance == 0.0);
    REQUIRE(traced.result->collision_plane->source_plane_index);
    CHECK(*traced.result->collision_plane->source_plane_index == 10U);
    REQUIRE(traced.result->hit);
    CHECK(traced.result->hit->kind ==
        movement::PlayerMovementHitKind::world);
    CHECK(traced.result->hit->source_model_index == 0U);
    CHECK_FALSE(traced.result->hit->stable_instance_ordinal);
    CHECK_FALSE(traced.result->hit->source_entity_index);
    CHECK(traced.result->start_contents.category ==
        movement::PlayerMovementContents::empty);
    CHECK(traced.result->end_contents.category ==
        movement::PlayerMovementContents::solid);
    REQUIRE(traced.result->blocking_contents);
    CHECK(traced.result->blocking_contents->category ==
        movement::PlayerMovementContents::solid);
    CHECK(traced.result->traversal_statistics.traversal_steps > 0U);
    CHECK(traced.result->traversal_statistics.terminal_interval_count > 0U);
}

TEST_CASE("World-only collision identity derives a complete package fallback",
    "[goldsrc][movement][collision][world][identity]")
{
    const auto source = fixture::package(true, 0.0);
    const movement_collision::WorldOnlyMovementCollision sourced{source};
    const auto sourced_identity = sourced.session_identity();
    REQUIRE(sourced_identity);
    CHECK(sourced_identity->collision_world_primary == 0x1234U);
    CHECK(sourced_identity->collision_world_secondary == 0x5678U);

    const auto without_fingerprint = repackaged_world(
        source, collision::CollisionWorldIdentity{std::nullopt, 30U});
    const auto zero_fingerprint = repackaged_world(source,
        collision::CollisionWorldIdentity{
            assets::AssetSourceFingerprint{0U, 0U}, 30U});
    const auto changed_collision = repackaged_world(
        source, collision::CollisionWorldIdentity{std::nullopt, 30U}, 1.0);

    const movement_collision::WorldOnlyMovementCollision missing{
        without_fingerprint};
    const movement_collision::WorldOnlyMovementCollision zero{
        zero_fingerprint};
    const movement_collision::WorldOnlyMovementCollision changed{
        changed_collision};
    const auto missing_identity = missing.session_identity();
    const auto zero_identity = zero.session_identity();
    const auto changed_identity = changed.session_identity();
    REQUIRE(missing_identity);
    REQUIRE(zero_identity);
    REQUIRE(changed_identity);
    CHECK(missing_identity->valid());
    CHECK(zero_identity->valid());
    CHECK(changed_identity->valid());
    CHECK((missing_identity->collision_world_primary != 0U ||
        missing_identity->collision_world_secondary != 0U));
    CHECK((zero_identity->collision_world_primary != 0U ||
        zero_identity->collision_world_secondary != 0U));
    CHECK(*missing_identity != *changed_identity);
}

TEST_CASE("World-only movement collision rejects malformed requests before query",
    "[goldsrc][movement][collision][world][security]")
{
    const movement_collision::WorldOnlyMovementCollision null_adapter{nullptr};
    collision::CollisionQueryScratch scratch;
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    CHECK_FALSE(null_adapter.valid());

    auto result = null_adapter.point_contents({}, scratch);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);
    CHECK_FALSE(result.result);

    result = null_adapter.point_contents(
        {}, scratch, invalid_query_config());
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_configuration);

    result = null_adapter.point_contents({nan, 0.0F, 0.0F}, scratch);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_point);

    const auto invalid_position = null_adapter.test_position(
        {}, static_cast<movement::PlayerMovementHull>(255U), scratch);
    REQUIRE_FALSE(invalid_position);
    REQUIRE(invalid_position.error);
    CHECK(invalid_position.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_hull);

    const auto invalid_segment = null_adapter.trace_hull(
        {nan, 0.0F, 0.0F}, {}, movement::PlayerMovementHull::standing,
        scratch);
    REQUIRE_FALSE(invalid_segment);
    REQUIRE(invalid_segment.error);
    CHECK(invalid_segment.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_segment);

    const auto invalid_hull = null_adapter.trace_hull(
        {}, {}, static_cast<movement::PlayerMovementHull>(255U), scratch);
    REQUIRE_FALSE(invalid_hull);
    REQUIRE(invalid_hull.error);
    CHECK(invalid_hull.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_hull);

    const auto empty_package =
        std::make_shared<const collision::CollisionWorldPackage>();
    const movement_collision::WorldOnlyMovementCollision malformed{
        empty_package};
    CHECK_FALSE(malformed.valid());
    const auto rejected = malformed.trace_hull(
        {}, {}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);
}

TEST_CASE("Synthetic movement collision publishes stable brush identity",
    "[goldsrc][movement][collision][synthetic]")
{
    const auto scene = single_solid_brush_scene();
    const movement_collision::SyntheticBrushMovementCollision adapter{scene};
    collision::CollisionQueryScratch scratch;

    REQUIRE(adapter.valid());
    CHECK(adapter.scene() == scene);
    CHECK(adapter.profile() == movement_collision::
        LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1);

    const auto traced = adapter.trace_hull(
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F},
        movement::PlayerMovementHull::ducked, scratch);
    REQUIRE(traced);
    REQUIRE(traced.result);
    CHECK(traced.result->collision_profile == movement_collision::
        LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1);
    CHECK(traced.result->fraction == Catch::Approx(0.25));
    REQUIRE(traced.result->hit);
    CHECK(traced.result->hit->kind ==
        movement::PlayerMovementHitKind::explicit_synthetic_brush);
    CHECK(traced.result->hit->source_model_index == 1U);
    REQUIRE(traced.result->hit->stable_instance_ordinal);
    CHECK(*traced.result->hit->stable_instance_ordinal == 7U);
    REQUIRE(traced.result->hit->source_entity_index);
    CHECK(*traced.result->hit->source_entity_index == 42U);

    // Synthetic brushes participate in traces, but never masquerade as world
    // point-contents providers.
    const auto contents =
        adapter.point_contents({9.0F, 0.0F, 0.0F}, scratch);
    REQUIRE(contents);
    CHECK(contents.result->contents.category ==
        movement::PlayerMovementContents::empty);
}

TEST_CASE("Synthetic collision identity covers transforms and query bounds",
    "[goldsrc][movement][collision][synthetic][identity]")
{
    const auto original_scene = single_solid_brush_scene();
    const auto translated_scene =
        single_solid_brush_scene({11.0F, 0.0F, 0.0F});
    const auto rotated_scene = single_solid_brush_scene(
        {10.0F, 0.0F, 0.0F}, {0.0F, 90.0F, 0.0F});

    std::vector<scene_collision::BrushCollisionSceneInstance> instances{
        original_scene->instances().begin(), original_scene->instances().end()};
    REQUIRE_FALSE(instances.empty());
    instances.front().transformed_bounds.maximum.x += 0.25F;
    const auto changed_bounds_scene =
        std::make_shared<const scene_collision::BrushCollisionScene>(
            original_scene->model_library(), std::move(instances),
            original_scene->role_provider_profile());

    const movement_collision::SyntheticBrushMovementCollision original{
        original_scene};
    const movement_collision::SyntheticBrushMovementCollision translated{
        translated_scene};
    const movement_collision::SyntheticBrushMovementCollision rotated{
        rotated_scene};
    const movement_collision::SyntheticBrushMovementCollision changed_bounds{
        changed_bounds_scene};
    const auto original_identity = original.session_identity();
    const auto translated_identity = translated.session_identity();
    const auto rotated_identity = rotated.session_identity();
    const auto changed_bounds_identity = changed_bounds.session_identity();
    REQUIRE(original_identity);
    REQUIRE(translated_identity);
    REQUIRE(rotated_identity);
    REQUIRE(changed_bounds_identity);
    CHECK(original_identity->valid());
    CHECK(original_identity->scene_signature !=
        translated_identity->scene_signature);
    CHECK(original_identity->scene_signature !=
        rotated_identity->scene_signature);
    CHECK(original_identity->scene_signature !=
        changed_bounds_identity->scene_signature);
}

TEST_CASE("Synthetic movement collision fails closed without explicit roles",
    "[goldsrc][movement][collision][synthetic][security]")
{
    collision::CollisionQueryScratch scratch;
    const movement_collision::SyntheticBrushMovementCollision null_adapter{
        nullptr};
    CHECK_FALSE(null_adapter.valid());

    const auto point = null_adapter.point_contents({}, scratch);
    REQUIRE_FALSE(point);
    REQUIRE(point.error);
    CHECK(point.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);

    const auto traced = null_adapter.trace_hull(
        {}, {}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE_FALSE(traced);
    REQUIRE(traced.error);
    CHECK(traced.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);

    const scene_collision::StockBrushCollisionRoleProvider stock_provider;
    const auto library = scene_collision::build_brush_collision_model_library(
        fixture::package(false));
    REQUIRE(library);
    const std::array definitions{
        scene_collision::BrushCollisionInstanceDefinition{
            {1U, 1U, std::nullopt}, rigid({10.0F, 0.0F, 0.0F})},
    };
    const auto stock_scene = scene_collision::build_brush_collision_scene(
        library.library, definitions, stock_provider);
    REQUIRE(stock_scene);
    const movement_collision::SyntheticBrushMovementCollision unsupported{
        stock_scene.scene};
    CHECK_FALSE(unsupported.valid());
    const auto rejected = unsupported.trace_hull(
        {}, {}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == movement_collision::
        LocalMovementCollisionErrorCode::unsupported_collision_profile);
}

TEST_CASE("Local movement collision configuration and labels are stable",
    "[goldsrc][movement][collision][profile]")
{
    CHECK(movement_collision::valid_local_movement_collision_query_config({}));
    CHECK_FALSE(movement_collision::valid_local_movement_collision_query_config(
        invalid_query_config()));
    CHECK(movement_collision::to_string(
              movement_collision::LocalMovementCollisionProfile::world_only_v1) ==
        "world_only_v1");
    CHECK(movement_collision::to_string(
              movement_collision::LocalMovementCollisionProfile::
                  explicit_synthetic_static_brush_v1) ==
        "explicit_synthetic_static_brush_v1");
    CHECK(movement_collision::to_string(
              movement_collision::LocalMovementCollisionErrorCode::
                  invalid_collision_source) ==
        "invalid_collision_source");
    CHECK(movement_collision::to_string(
              static_cast<movement_collision::LocalMovementCollisionProfile>(
                  255U)) ==
        "unknown");
}
