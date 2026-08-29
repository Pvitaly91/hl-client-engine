#include "local_movement_test_fixture.hpp"

#include <hlclient/prediction/local_prediction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

namespace {

namespace goldsrc_movement = hlclient::goldsrc::movement;
namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;
namespace fixture = hlclient::tests::local_movement;

[[nodiscard]] goldsrc_movement::LocalMovementCollisionSessionIdentity
valid_collision_identity() noexcept
{
    goldsrc_movement::LocalMovementCollisionSessionIdentity identity;
    identity.profile = goldsrc_movement::LocalMovementCollisionProfile::
        explicit_synthetic_static_brush_v1;
    identity.collision_world_primary = 0x1111U;
    identity.collision_world_secondary = 0x2222U;
    identity.collision_world_revision = 7U;
    identity.scene_signature = 0x3333U;
    return identity;
}

class IdentityCollision final : public goldsrc_movement::ILocalMovementCollision {
public:
    explicit IdentityCollision(
        goldsrc_movement::LocalMovementCollisionSessionIdentity identity =
            valid_collision_identity()) noexcept
        : identity_{identity}
    {
    }

    void set_valid(const bool value) noexcept { collision_.set_valid(value); }

    [[nodiscard]] goldsrc_movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return collision_.profile();
    }

    [[nodiscard]] bool valid() const noexcept override
    {
        return collision_.valid();
    }

    [[nodiscard]] std::optional<
        goldsrc_movement::LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override
    {
        return identity_;
    }

    [[nodiscard]] goldsrc_movement::LocalMovementPointContentsQueryResult
    point_contents(
        const hlclient::assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const goldsrc_movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return collision_.point_contents(point, scratch, config);
    }

    [[nodiscard]] goldsrc_movement::LocalMovementPositionQueryResult
    test_position(
        const hlclient::assets::AssetVector3& origin,
        const movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const goldsrc_movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return collision_.test_position(origin, hull, scratch, config);
    }

    [[nodiscard]] goldsrc_movement::LocalMovementTraceQueryResult trace_hull(
        const hlclient::assets::AssetVector3& start,
        const hlclient::assets::AssetVector3& end,
        const movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const goldsrc_movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return collision_.trace_hull(start, end, hull, scratch, config);
    }

private:
    fixture::DeterministicLocalMovementCollision collision_;
    goldsrc_movement::LocalMovementCollisionSessionIdentity identity_;
};

void check_session_error(
    const prediction::PredictionSessionCreateResult& result,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.session);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
}

[[nodiscard]] prediction::PredictionSessionCreateResult create_session(
    const goldsrc_movement::ILocalMovementCollision& collision,
    const std::uint64_t session_generation = 4U,
    const std::uint64_t prediction_generation = 9U,
    const goldsrc_movement::GoldSrcLocalMovementConfig& config = {})
{
    const auto environment = fixture::make_environment();
    const auto initial_state = fixture::make_state();
    return prediction::create_prediction_session_identity(
        session_generation, prediction_generation, collision, environment,
        config, initial_state);
}

} // namespace

TEST_CASE("Prediction session identity captures every deterministic dependency",
    "[prediction][session][identity]")
{
    const IdentityCollision collision;
    const auto first = create_session(collision);
    const auto second = create_session(collision);

    REQUIRE(first);
    REQUIRE(first.session);
    CHECK_FALSE(first.error);
    REQUIRE(second);
    REQUIRE(second.session);
    CHECK(*first.session == *second.session);

    const auto expected_collision = valid_collision_identity();
    CHECK(first.session->session_generation == 4U);
    CHECK(first.session->prediction_generation == 9U);
    CHECK(first.session->collision_world_primary ==
        expected_collision.collision_world_primary);
    CHECK(first.session->collision_world_secondary ==
        expected_collision.collision_world_secondary);
    CHECK(first.session->collision_world_revision ==
        expected_collision.collision_world_revision);
    CHECK(first.session->collision_scene_signature ==
        expected_collision.scene_signature);
    CHECK(first.session->collision_profile == expected_collision.profile);
    CHECK(first.session->movement_environment_signature != 0U);
    CHECK(first.session->movement_config_signature != 0U);
    CHECK(first.session->spawn_initial_state_signature != 0U);
    CHECK(first.session->command_profile == movement::
        GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1);
    CHECK(first.session->prediction_profile == prediction::
        PredictionCompatibilityProfile::
            synthetic_authoritative_reconciliation_v1);
    CHECK(first.session->acknowledgement_profile == prediction::
        PredictionAcknowledgementProfile::synthetic_uint32_non_wrapping_v1);
    CHECK(first.session->valid());
}

TEST_CASE("Prediction session signatures change with captured dependencies",
    "[prediction][session][signature]")
{
    const IdentityCollision original_collision;
    auto changed_identity = valid_collision_identity();
    ++changed_identity.scene_signature;
    const IdentityCollision changed_collision{changed_identity};

    const auto original = create_session(original_collision);
    const auto collision_changed = create_session(changed_collision);
    auto config = goldsrc_movement::GoldSrcLocalMovementConfig{};
    config.maximum_substeps_per_command += 1U;
    const auto config_changed = create_session(original_collision, 4U, 9U, config);
    const auto generation_changed = create_session(original_collision, 5U, 10U);

    REQUIRE(original.session);
    REQUIRE(collision_changed.session);
    REQUIRE(config_changed.session);
    REQUIRE(generation_changed.session);
    CHECK(*original.session != *collision_changed.session);
    CHECK(original.session->collision_scene_signature !=
        collision_changed.session->collision_scene_signature);
    CHECK(*original.session != *config_changed.session);
    CHECK(original.session->movement_config_signature !=
        config_changed.session->movement_config_signature);
    CHECK(*original.session != *generation_changed.session);
}

TEST_CASE("Prediction movement config signature covers every behavior input",
    "[prediction][session][signature][config]")
{
    const auto baseline = prediction::prediction_movement_config_signature({});
    const auto check_changed = [baseline](const auto mutate) {
        auto config = goldsrc_movement::GoldSrcLocalMovementConfig{};
        mutate(config);
        CHECK(prediction::prediction_movement_config_signature(config) !=
            baseline);
    };

    check_changed([](auto& value) {
        value.maximum_command_duration_seconds += 0.001;
    });
    check_changed([](auto& value) {
        value.maximum_substep_duration_seconds += 0.001;
    });
    check_changed([](auto& value) { ++value.maximum_substeps_per_command; });
    check_changed([](auto& value) { value.ground_probe_distance += 0.5; });
    check_changed([](auto& value) { value.minimum_walkable_normal_z += 0.01; });
    check_changed([](auto& value) {
        value.maximum_ground_snap_upward_velocity += 1.0;
    });
    check_changed([](auto& value) { ++value.maximum_slide_bumps; });
    check_changed([](auto& value) { ++value.maximum_clip_planes; });
    check_changed([](auto& value) { ++value.maximum_touches_per_command; });
    check_changed([](auto& value) { value.stop_epsilon += 0.01; });
    check_changed([](auto& value) { value.air_wish_speed_cap += 1.0; });
    check_changed([](auto& value) { value.jump_impulse += 1.0; });
    check_changed([](auto& value) { value.standing_view_offset.x += 1.0F; });
    check_changed([](auto& value) { value.standing_view_offset.y += 1.0F; });
    check_changed([](auto& value) { value.standing_view_offset.z += 1.0F; });
    check_changed([](auto& value) { value.duck_view_offset.x += 1.0F; });
    check_changed([](auto& value) { value.duck_view_offset.y += 1.0F; });
    check_changed([](auto& value) { value.duck_view_offset.z += 1.0F; });
    check_changed([](auto& value) {
        value.collision_query.query_limits.maximum_traversal_steps -= 1U;
    });
    check_changed([](auto& value) {
        value.collision_query.query_limits.maximum_stack_entries -= 1U;
    });
    check_changed([](auto& value) {
        value.collision_query.query_limits.maximum_fraction_splits -= 1U;
    });
    check_changed([](auto& value) {
        value.collision_query.query_limits.maximum_query_scratch_bytes -= 1U;
    });
    check_changed([](auto& value) {
        value.collision_query.trace_tolerance.plane_distance_epsilon *= 2.0;
    });
    check_changed([](auto& value) {
        value.collision_query.trace_tolerance.fraction_epsilon *= 2.0;
    });
    check_changed([](auto& value) {
        value.collision_query.trace_tolerance.minimum_progress_fraction *= 2.0;
    });
    check_changed([](auto& value) {
        --value.collision_query.scene_limits.maximum_brush_candidates;
    });
    check_changed([](auto& value) {
        --value.collision_query.scene_limits.maximum_model_traces;
    });
    check_changed([](auto& value) {
        value.state_limits.maximum_coordinate_magnitude -= 1.0F;
    });
    check_changed([](auto& value) {
        value.state_limits.maximum_velocity_magnitude -= 1.0F;
    });
    check_changed([](auto& value) {
        value.state_limits.maximum_angle_magnitude -= 1.0F;
    });
    check_changed([](auto& value) {
        --value.state_limits.maximum_state_revision;
    });
    check_changed([](auto& value) {
        value.gravity_profile = static_cast<
            goldsrc_movement::GoldSrcMovementGravityProfile>(1U);
    });
    check_changed([](auto& value) {
        value.jump_profile = static_cast<
            goldsrc_movement::GoldSrcMovementJumpProfile>(1U);
    });
    check_changed([](auto& value) {
        value.duck_profile = static_cast<
            goldsrc_movement::GoldSrcMovementDuckProfile>(1U);
    });
    check_changed([](auto& value) {
        value.air_profile = static_cast<
            goldsrc_movement::GoldSrcMovementAirProfile>(1U);
    });
    check_changed([](auto& value) {
        value.airborne_duck_policy = static_cast<
            goldsrc_movement::GoldSrcMovementAirborneDuckPolicy>(1U);
    });
}

TEST_CASE("Prediction session creation rejects incomplete inputs transactionally",
    "[prediction][session][validation]")
{
    const IdentityCollision valid_collision;

    SECTION("zero session generation")
    {
        check_session_error(create_session(valid_collision, 0U, 1U),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("zero prediction generation")
    {
        check_session_error(create_session(valid_collision, 1U, 0U),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("identity-less collision provider")
    {
        const fixture::DeterministicLocalMovementCollision collision;
        check_session_error(create_session(collision),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("invalid collision provider")
    {
        IdentityCollision collision;
        collision.set_valid(false);
        check_session_error(create_session(collision),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("invalid collision identity")
    {
        auto identity = valid_collision_identity();
        identity.scene_signature = 0U;
        check_session_error(create_session(IdentityCollision{identity}),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("invalid collision profile")
    {
        auto identity = valid_collision_identity();
        identity.profile = static_cast<
            goldsrc_movement::LocalMovementCollisionProfile>(255U);
        check_session_error(create_session(IdentityCollision{identity}),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("collision identity profile disagrees with provider")
    {
        auto identity = valid_collision_identity();
        identity.profile =
            goldsrc_movement::LocalMovementCollisionProfile::world_only_v1;
        check_session_error(create_session(IdentityCollision{identity}),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("zero collision fingerprint")
    {
        auto identity = valid_collision_identity();
        identity.collision_world_primary = 0U;
        identity.collision_world_secondary = 0U;
        check_session_error(create_session(IdentityCollision{identity}),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("invalid movement configuration")
    {
        auto config = goldsrc_movement::GoldSrcLocalMovementConfig{};
        config.maximum_substeps_per_command = 0U;
        check_session_error(create_session(valid_collision, 1U, 1U, config),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
    SECTION("initial state is not a spawn anchor")
    {
        const auto environment = fixture::make_environment();
        const auto initial_state = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {}, movement::PlayerMovementMode::walking,
            movement::PlayerMovementHull::standing, 1U);
        const auto result = prediction::create_prediction_session_identity(
            1U, 1U, valid_collision, environment, {}, initial_state);
        check_session_error(result,
            prediction::PredictionErrorCode::invalid_session_identity);
    }
}

TEST_CASE("Prediction session stock profiles remain evidence-gated",
    "[prediction][session][stock-evidence]")
{
    const IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto initial_state = fixture::make_state();

    const auto stock_prediction =
        prediction::create_prediction_session_identity(
            1U, 1U, collision, environment, {}, initial_state,
            prediction::PredictionCompatibilityProfile::
                stock_protocol_48_authoritative_reconciliation_evidence_pending);
    check_session_error(stock_prediction,
        prediction::PredictionErrorCode::stock_evidence_pending);

    const auto stock_acknowledgement =
        prediction::create_prediction_session_identity(
            1U, 1U, collision, environment, {}, initial_state,
            prediction::PredictionCompatibilityProfile::
                synthetic_authoritative_reconciliation_v1,
            prediction::PredictionAcknowledgementProfile::
                stock_usercmd_acknowledgement_evidence_pending);
    check_session_error(stock_acknowledgement,
        prediction::PredictionErrorCode::stock_evidence_pending);
}

TEST_CASE("Prediction session scalar identity validates exact required fields",
    "[prediction][session][scalar-validation]")
{
    const IdentityCollision collision;
    const auto created = create_session(collision);
    REQUIRE(created.session);
    const auto valid = *created.session;
    REQUIRE(valid.valid());

    auto check_invalid = [&valid](const auto mutate) {
        auto candidate = valid;
        mutate(candidate);
        CHECK_FALSE(candidate.valid());
    };
    check_invalid([](auto& value) { value.session_generation = 0U; });
    check_invalid([](auto& value) { value.prediction_generation = 0U; });
    check_invalid([](auto& value) {
        value.collision_world_primary = 0U;
        value.collision_world_secondary = 0U;
    });
    check_invalid([](auto& value) { value.collision_world_revision = 0U; });
    check_invalid([](auto& value) { value.collision_scene_signature = 0U; });
    check_invalid([](auto& value) {
        value.collision_profile = static_cast<
            goldsrc_movement::LocalMovementCollisionProfile>(255U);
    });
    check_invalid([](auto& value) {
        value.movement_environment_signature = 0U;
    });
    check_invalid([](auto& value) { value.movement_config_signature = 0U; });
    check_invalid([](auto& value) { value.spawn_initial_state_signature = 0U; });
    check_invalid([](auto& value) {
        value.command_profile = movement::GoldSrcMovementCommandProfile::
            stock_usercmd_semantics_evidence_pending;
    });
    check_invalid([](auto& value) {
        value.prediction_profile = prediction::PredictionCompatibilityProfile::
            stock_protocol_48_authoritative_reconciliation_evidence_pending;
    });
    check_invalid([](auto& value) {
        value.acknowledgement_profile =
            prediction::PredictionAcknowledgementProfile::
                stock_usercmd_acknowledgement_evidence_pending;
    });

    auto copy = valid;
    CHECK(copy == valid);
    ++copy.collision_world_primary;
    CHECK(copy != valid);
}
