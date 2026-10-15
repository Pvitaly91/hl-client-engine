#include "local_movement_test_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"

#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace {
namespace g = hlclient::goldsrc;
namespace m = hlclient::goldsrc::movement;
namespace p = hlclient::movement;
namespace c = hlclient::collision;
namespace bc = hlclient::goldsrc::collision;

std::shared_ptr<const c::CollisionWorldPackage> boundary_package(const bool ceiling = false)
{
    const auto authored = hlclient::tests::vertical_lift::package(ceiling);
    std::vector<c::CollisionPlane> planes(authored->planes().begin(), authored->planes().end());
    // Retain the existing finite platform hulls; author the world floor at
    // -2048 so the reported large negative coordinate also remains empty.
    // A ceiling control has its physical underside at z122: standing origin86.
    for (const auto& hull : authored->models().front().hulls) {
        const auto root = hull.root.index;
        const auto root_plane = authored->nodes()[root].plane_index;
        if (ceiling) {
            planes[root_plane].distance -= 38.0;
            const auto floor_root = authored->nodes()[root].children[1U].index;
            planes[authored->nodes()[floor_root].plane_index].distance -= 1920.0;
        } else planes[root_plane].distance -= 1920.0;
    }
    return std::make_shared<const c::CollisionWorldPackage>(std::move(planes),
        std::vector<c::CollisionNode>(authored->nodes().begin(), authored->nodes().end()),
        std::vector<c::CollisionLeaf>(authored->leaves().begin(), authored->leaves().end()),
        std::vector<c::CollisionClipnode>(authored->clipnodes().begin(), authored->clipnodes().end()),
        std::vector<c::CollisionModel>(authored->models().begin(), authored->models().end()),
        c::CollisionWorldIdentity{hlclient::assets::AssetSourceFingerprint{0x45333242U, 0x51554244U}, 1U});
}

g::ReferenceBrushCollisionContext boundary_frame(
    const std::shared_ptr<const bc::BrushCollisionModelLibrary>& library,
    const hlclient::assets::AssetVector3 translation, const bool overlapping = false,
    const bool include_lift = true, const double time = 1.0, const std::uint64_t record = 2U)
{
    hlclient::client::RuntimeClientObservationState observation;
    observation.generation = 1U; observation.publication_revision = record;
    observation.entity_metadata = {1U, hlclient::client::RuntimeObservationFreshness::observed_in_record,
        hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,
        hlclient::client::RuntimeObservationSource{record}};
    observation.server_time_seconds = time; observation.server_time_metadata = observation.entity_metadata;
    if (include_lift) {
        hlclient::client::RuntimePacketEntityObservation lift;
        lift.entity_number = 67U; lift.model_index = 35U; lift.solid = 4U;
        lift.brush_move_type = 7U;
        lift.origin = {translation.x, translation.y, translation.z};
        lift.angles = {0.0, 0.0, 0.0}; observation.packet_entities.push_back(lift);
        if (overlapping) {
            lift.entity_number = 68U; lift.origin.z = static_cast<double>(translation.z) + 1.0;
            observation.packet_entities.push_back(lift);
        }
    }
    // Advertised slot35 is not the BSP ordinal. This project-owned fixture
    // has one finite submodel; no installed Crossfire geometry is copied.
    return g::build_reference_brush_collision(library, {{35U, "*1"}}, observation, 1U);
}

g::ReferencePredictionSeed boundary_seed(const hlclient::assets::AssetVector3 translation,
    const double depth = 1.0 / 128.0, const bool duck = false)
{
    g::ReferencePredictionSeed seed;
    seed.generation = 1U; seed.record_identity = 2U;
    seed.command_boundary = *g::GoldSrcUserCmdSequence::create(7U);
    seed.origin = {translation.x, translation.y, static_cast<double>(translation.z) + (duck ? 18.0 : 36.0) - depth};
    seed.velocity = {0.0, 0.0, 0.0}; seed.view_offset = {0.0, 0.0, duck ? 12.0 : 28.0};
    seed.base_velocity = {0.0, 0.0, 0.0}; seed.flags = (1U << 9U) | (duck ? 1U << 14U : 0U);
    seed.move_type = 3U; seed.use_hull = duck ? 1U : 0U;
    seed.gravity_multiplier = 1.0; seed.friction_multiplier = 1.0;
    return seed;
}

g::ReferencePredictionGroundResult derive(const g::ReferencePredictionSeed& seed,
    const m::ILocalMovementCollision& provider, m::GoldSrcLocalMovementScratch& scratch,
    const p::GoldSrcMovementCommandProfile profile = p::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2)
{
    return g::derive_reference_prediction_ground(seed, {}, provider, scratch, {}, profile);
}

// All real occupancy and hull geometry still run through the production
// immutable finite-brush provider. Only typed malformed witness controls are
// injected, to prove that entry/support metadata cannot authorize recovery.
class InvalidWitness final : public m::ILocalMovementCollision {
public:
    enum class Fault { entry_identity, support_identity, steep_entry, liquid_entry, query_failure, missing_position_identity };
    InvalidWitness(const m::ILocalMovementCollision& source, const Fault fault) : source_(source), fault_(fault) {}
    m::LocalMovementCollisionProfile profile() const noexcept override { return source_.profile(); }
    bool valid() const noexcept override { return source_.valid(); }
    std::optional<m::LocalMovementCollisionSessionIdentity> session_identity() const noexcept override { return source_.session_identity(); }
    m::LocalMovementPointContentsQueryResult point_contents(const hlclient::assets::AssetVector3& at,
        c::CollisionQueryScratch& scratch, const m::LocalMovementCollisionQueryConfig& config) const override {
        return source_.point_contents(at, scratch, config);
    }
    m::LocalMovementPositionQueryResult test_position(const hlclient::assets::AssetVector3& at,
        const p::PlayerMovementHull hull, c::CollisionQueryScratch& scratch,
        const m::LocalMovementCollisionQueryConfig& config) const override {
        auto value = source_.test_position(at, hull, scratch, config);
        if (fault_ == Fault::missing_position_identity && value.result && value.result->hit)
            value.result->hit->source_entity_index.reset();
        return value;
    }
    m::LocalMovementTraceQueryResult trace_hull(const hlclient::assets::AssetVector3& from,
        const hlclient::assets::AssetVector3& to, const p::PlayerMovementHull hull,
        c::CollisionQueryScratch& scratch, const m::LocalMovementCollisionQueryConfig& config) const override {
        auto value = source_.trace_hull(from, to, hull, scratch, config);
        const bool entry = from.z - to.z < .5F;
        if (fault_ == Fault::query_failure)
            return {{}, m::LocalMovementCollisionError{m::LocalMovementCollisionErrorCode::world_query_failed, {}, {}}};
        if (!value.result) return value;
        if ((entry && fault_ == Fault::entry_identity) || (!entry && fault_ == Fault::support_identity)) {
            if (value.result->hit) value.result->hit->source_entity_index = 77U;
        }
        if (entry && fault_ == Fault::steep_entry && value.result->collision_plane)
            value.result->collision_plane->normal = {0.8F, 0.0F, 0.6F};
        if (entry && fault_ == Fault::liquid_entry) value.result->in_liquid = true;
        return value;
    }
private:
    const m::ILocalMovementCollision& source_;
    Fault fault_;
};
} // namespace

TEST_CASE("Grounded brush-grid reseed recovers a witnessed finite lift top and allows walk and jump escape",
    "[e32-brush-boundary][e32][movement][prediction][brush][no-stock]")
{
    const auto library = bc::build_brush_collision_model_library(boundary_package()); REQUIRE(library);
    const auto environment = hlclient::tests::local_movement::make_environment();
    for (const bool duck : {false, true})
    for (const float height : {50.F, -1696.F, 70.F})
    for (const double depth : {1.0 / 128.0, 1.0 / 16.0, 127.0 / 1024.0, 1.0 / 8.0})
    for (const bool translated : {false, true}) {
        CAPTURE(duck, height, depth, translated);
        const hlclient::assets::AssetVector3 translation{translated ? -827.359375F : 0.F,
            translated ? 1311.96875F : 0.F, height};
        const auto frame = boundary_frame(library.library, translation); REQUIRE(frame.scene);
        const m::BrushSceneMovementCollision provider{frame.scene};
        const auto source = boundary_seed(translation, depth, duck);
        const hlclient::assets::AssetVector3 original{static_cast<float>(*source.origin.x),
            static_cast<float>(*source.origin.y), static_cast<float>(*source.origin.z)};
        const auto hull = duck ? p::PlayerMovementHull::ducked : p::PlayerMovementHull::standing;
        m::GoldSrcLocalMovementScratch scratch;
        const auto blocked = provider.test_position(original, hull, scratch.collision);
        REQUIRE(blocked); REQUIRE(blocked.result->status == m::LocalMovementPositionStatus::blocking);
        REQUIRE(blocked.result->hit); CHECK(blocked.result->hit->source_entity_index == 67U);
        // Negative control proves the old single-ULP recovery cannot free this seed.
        CHECK_FALSE(m::resolve_movement_boundary_position(original, hull, provider, scratch.collision));
        const auto recovered = derive(source, provider, scratch);
        INFO("status=" << g::to_string(recovered.status)); REQUIRE(recovered.state);
        REQUIRE(recovered.queried_origin); REQUIRE(recovered.ground_evidence); REQUIRE(recovered.ground_evidence->hit);
        const auto expected_z = height + (duck ? 18.F : 36.F);
        CHECK(recovered.queried_origin->x == original.x); CHECK(recovered.queried_origin->y == original.y);
        CHECK(recovered.queried_origin->z == expected_z);
        CHECK(recovered.state->origin().z == expected_z);
        CHECK(static_cast<double>(recovered.queried_origin->z) - *source.origin.z <= .125);
        CHECK(recovered.ground_evidence->hit == blocked.result->hit);
        CHECK(recovered.ground_evidence->plane.normal.z == 1.F);
        CHECK(*source.origin.z == static_cast<double>(height) + (duck ? 18.0 : 36.0) - depth);
        CHECK(recovered.state->ground_state().grounded());
        const auto duplicate = derive(source, provider, scratch); REQUIRE(duplicate.state);
        CHECK(p::local_player_movement_state_signature(*duplicate.state) ==
            p::local_player_movement_state_signature(*recovered.state));
        std::optional<p::LocalPlayerMovementState> state{*recovered.state};
        for (std::uint32_t sequence = 8U; sequence < 72U; ++sequence) {
            g::GoldSrcWireUserCmd wire; wire.msec = 20U; wire.forward = 400;
            wire.buttons = static_cast<std::uint16_t>(duck ? g::kReferenceGoldSrcButtonDuck : 0U);
            const auto command = g::reference_jump_duck_weapon_use_movement_command(
                *g::GoldSrcUserCmdSequence::create(sequence), wire); REQUIRE(command);
            const auto moved = g::simulate_reference_movement(*state, *command.state,
                environment, provider, scratch, {}); REQUIRE(moved);
            CHECK(moved.statistics.start_solid_count == 0U); CHECK(moved.statistics.all_solid_count == 0U);
            CHECK(moved.state->origin().x > state->origin().x);
            const auto clear = provider.test_position(moved.state->origin(), moved.state->hull(), scratch.collision);
            REQUIRE(clear); CHECK(clear.result->status == m::LocalMovementPositionStatus::free);
            state.emplace(*moved.state);
        }
        CHECK(state->origin().x > original.x + 80.F);
        g::GoldSrcWireUserCmd jump; jump.msec = 20U;
        jump.buttons = static_cast<std::uint16_t>(g::kReferenceGoldSrcButtonJump |
            (duck ? g::kReferenceGoldSrcButtonDuck : 0U));
        const auto command = g::reference_jump_duck_weapon_use_movement_command(
            *g::GoldSrcUserCmdSequence::create(8U), jump); REQUIRE(command);
        const auto jumped = g::simulate_reference_movement(*recovered.state, *command.state,
            environment, provider, scratch, {}); REQUIRE(jumped);
        CHECK(jumped.statistics.jump_count == 1U);
        CHECK(jumped.state->origin().z > expected_z); CHECK(jumped.state->velocity().z > 0.F);
    }
}

TEST_CASE("Brush-grid seed recovery rejects deep airborne wall ceiling overlapping and unrelated witnesses",
    "[e32-brush-boundary][e32][movement][prediction][brush][no-stock]")
{
    const auto library = bc::build_brush_collision_model_library(boundary_package()); REQUIRE(library);
    constexpr hlclient::assets::AssetVector3 translation{0.F, 0.F, 50.F};
    const auto frame = boundary_frame(library.library, translation); REQUIRE(frame.scene);
    const m::BrushSceneMovementCollision provider{frame.scene}; m::GoldSrcLocalMovementScratch scratch;
    const auto rejected = [&](const g::ReferencePredictionSeed& source, const m::ILocalMovementCollision& collision) {
        const auto result = derive(source, collision, scratch);
        CHECK_FALSE(result.state); CHECK(result.status == g::ReferencePredictionGroundStatus::solid_start);
    };
    for (const double depth : {.1328125, .5, 8.0}) rejected(boundary_seed(translation, depth), provider);
    auto airborne = boundary_seed(translation); airborne.flags = 0U; rejected(airborne, provider);
    auto ascending = boundary_seed(translation); ascending.velocity.z = 181.0; rejected(ascending, provider);
    const auto strict = derive(boundary_seed(translation), provider, scratch,
        p::GoldSrcMovementCommandProfile::reference_wire_dry_walk_v1);
    CHECK_FALSE(strict.state); CHECK(strict.status == g::ReferencePredictionGroundStatus::solid_start);
    auto side = boundary_seed(translation); side.origin = {79.9921875, 0.0, 50.0}; rejected(side, provider);
    const auto overlap = boundary_frame(library.library, translation, true); REQUIRE(overlap.scene);
    rejected(boundary_seed(translation), m::BrushSceneMovementCollision{overlap.scene});
    const auto ceiling_library = bc::build_brush_collision_model_library(boundary_package(true)); REQUIRE(ceiling_library);
    const auto ceiling = boundary_frame(ceiling_library.library, translation); REQUIRE(ceiling.scene);
    rejected(boundary_seed(translation), m::BrushSceneMovementCollision{ceiling.scene});
    for (const auto fault : {InvalidWitness::Fault::entry_identity, InvalidWitness::Fault::support_identity,
        InvalidWitness::Fault::steep_entry, InvalidWitness::Fault::liquid_entry,
        InvalidWitness::Fault::query_failure, InvalidWitness::Fault::missing_position_identity}) {
        CAPTURE(static_cast<unsigned>(fault)); rejected(boundary_seed(translation), InvalidWitness{provider, fault});
    }
    const m::WorldOnlyMovementCollision world{boundary_package()};
    auto world_seed = boundary_seed({0.F, 0.F, -2048.F}); rejected(world_seed, world);
}

TEST_CASE("Brush-grid seed recovery preserves exact clear and one-ULP seeds and existing server-box recovery",
    "[e32-brush-boundary][e32][movement][prediction][brush][no-stock]")
{
    const auto library = bc::build_brush_collision_model_library(boundary_package()); REQUIRE(library);
    constexpr hlclient::assets::AssetVector3 translation{0.F, 0.F, 50.F};
    const auto frame = boundary_frame(library.library, translation); REQUIRE(frame.scene);
    const m::BrushSceneMovementCollision provider{frame.scene}; m::GoldSrcLocalMovementScratch scratch;
    const auto exact = derive(boundary_seed(translation, 0.0), provider, scratch); REQUIRE(exact.state);
    CHECK(exact.state->origin().z == 86.F);
    auto single_ulp = boundary_seed(translation, 0.0);
    single_ulp.origin.z = std::nextafter(86.F, -std::numeric_limits<float>::infinity());
    const auto recovered_ulp = derive(single_ulp, provider, scratch); REQUIRE(recovered_ulp.state);
    CHECK(recovered_ulp.state->origin().z == 86.F);
    const auto empty = boundary_frame(library.library, translation, false, false); REQUIRE(empty.scene);
    const m::ServerEntityCollisionBox box{17U, {{-64.F, -64.F, 42.F}, {64.F, 64.F, 50.F}}};
    const m::BrushSceneMovementCollision boxed{empty.scene, std::span{&box, 1U}};
    const auto box_seed = derive(boundary_seed(translation), boxed, scratch); REQUIRE(box_seed.state);
    CHECK(box_seed.state->origin().z == 86.F);
    REQUIRE(box_seed.ground_evidence); REQUIRE(box_seed.ground_evidence->hit);
    CHECK(box_seed.ground_evidence->hit->kind == p::PlayerMovementHitKind::server_entity_box);
    CHECK(box_seed.ground_evidence->hit->source_entity_index == 17U);
}

TEST_CASE("Recovered brush-grid seed rides rising falling and stopped supports exactly once",
    "[e32-brush-boundary][e32][e32-lift][movement][prediction][brush][no-stock]")
{
    const auto library = bc::build_brush_collision_model_library(boundary_package()); REQUIRE(library);
    const auto environment = hlclient::tests::local_movement::make_environment();
    for (const double speed : {-100.0, 0.0, 100.0}) {
        CAPTURE(speed);
        const auto previous = boundary_frame(library.library, {0.F, 0.F, 50.F}, false, true, 1.0, 1U);
        const float current_height = static_cast<float>(50.0 + speed * .1);
        const auto current = boundary_frame(library.library, {0.F, 0.F, current_height}, false, true, 1.1, 2U);
        REQUIRE(previous.scene); REQUIRE(current.scene);
        const g::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
        const m::BrushSceneMovementCollision provider{current.scene}; m::GoldSrcLocalMovementScratch scratch;
        const auto recovered = derive(boundary_seed({0.F, 0.F, current_height}), provider, scratch);
        REQUIRE(recovered.state);
        auto info = p::local_player_movement_state_create_info(*recovered.state);
        info.simulation_time_nanoseconds = 1'100'000'000ULL;
        const auto timed = p::LocalPlayerMovementState::create(info); REQUIRE(timed);
        std::optional<p::LocalPlayerMovementState> state{*timed.state};
        for (std::uint32_t index = 1U; index <= 10U; ++index) {
            g::GoldSrcWireUserCmd wire; wire.msec = 20U;
            const auto command = g::reference_jump_duck_weapon_use_movement_command(
                *g::GoldSrcUserCmdSequence::create(7U + index), wire); REQUIRE(command);
            const auto moved = g::simulate_reference_movement(*state, *command.state,
                environment, provider, scratch, {}, &support); REQUIRE(moved);
            CHECK(moved.state->origin().z == Catch::Approx(86.0 + speed * (.1 + .02 * index)).margin(.001));
            CHECK(moved.state->velocity().z == 0.F);
            CHECK(moved.statistics.start_solid_count == 0U); CHECK(moved.statistics.all_solid_count == 0U);
            REQUIRE(moved.state->ground_state().hit());
            CHECK(moved.state->ground_state().hit()->source_entity_index == 67U);
            const auto scene = support.sample(1.1 + .02 * index); REQUIRE(scene);
            const m::BrushSceneMovementCollision actual{scene};
            const auto clear = actual.test_position(moved.state->origin(), moved.state->hull(), scratch.collision);
            REQUIRE(clear); CHECK(clear.result->status == m::LocalMovementPositionStatus::free);
            state.emplace(*moved.state);
        }
        CHECK(state->simulation_time_nanoseconds() == 1'300'000'000ULL);
    }
}
