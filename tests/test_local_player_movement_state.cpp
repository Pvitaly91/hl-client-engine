#include <hlclient/movement/local_player_movement_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <type_traits>

namespace {

namespace movement = hlclient::movement;

[[nodiscard]] movement::LocalPlayerMovementStateCreateInfo valid_state()
{
    movement::LocalPlayerMovementStateCreateInfo info;
    info.origin = {10.0F, 20.0F, 36.0F};
    info.velocity = {1.0F, 2.0F, 3.0F};
    info.view_angles = {5.0F, 90.0F, -2.0F};
    info.hull = movement::PlayerMovementHull::standing;
    info.mode = movement::PlayerMovementMode::walking;
    info.ground.grounded = true;
    info.ground.walkable = true;
    info.ground.hit = movement::PlayerMovementHitIdentity{
        movement::PlayerMovementHitKind::explicit_synthetic_brush,
        3U,
        11U,
        42U,
    };
    info.ground.plane.normal = {0.0F, 0.0F, 1.0F};
    info.ground.plane.distance = 4.5;
    info.ground.plane.source_plane_index = 19U;
    info.ground.contact_position = {10.0F, 20.0F, 35.5F};
    info.ground.probe_fraction = 0.25;
    info.view_offset = {0.0F, 0.0F, 28.0F};
    info.old_buttons = 0x0022U;
    info.source_command_sequence = 77U;
    info.simulation_time_nanoseconds = 123'456'789U;
    info.last_valid_contents = movement::PlayerMovementContents::empty;
    info.gravity_multiplier = 0.75F;
    info.friction_multiplier = 0.5F;
    info.state_revision = 9U;
    return info;
}

void check_error(
    const movement::LocalPlayerMovementState::CreationResult& result,
    const movement::LocalPlayerMovementStateErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
}

} // namespace

static_assert(std::is_copy_constructible_v<
    movement::LocalPlayerMovementState>);
static_assert(std::is_move_constructible_v<
    movement::LocalPlayerMovementState>);
static_assert(!std::is_copy_assignable_v<
    movement::LocalPlayerMovementState>);
static_assert(!std::is_move_assignable_v<
    movement::LocalPlayerMovementState>);

TEST_CASE("Local player movement state retains immutable simulation metadata",
    "[movement][state]")
{
    const auto created =
        movement::LocalPlayerMovementState::create(valid_state());
    REQUIRE(created);
    REQUIRE(created.state);
    CHECK_FALSE(created.error);
    const auto& state = *created.state;

    CHECK(state.origin().x == 10.0F);
    CHECK(state.origin().y == 20.0F);
    CHECK(state.origin().z == 36.0F);
    CHECK(state.velocity().x == 1.0F);
    CHECK(state.velocity().y == 2.0F);
    CHECK(state.velocity().z == 3.0F);
    CHECK(state.view_angles().x == 5.0F);
    CHECK(state.view_angles().y == 90.0F);
    CHECK(state.view_angles().z == -2.0F);
    CHECK(state.hull() == movement::PlayerMovementHull::standing);
    CHECK(state.mode() == movement::PlayerMovementMode::walking);

    const auto& ground = state.ground_state();
    CHECK(ground.grounded());
    CHECK(ground.walkable());
    REQUIRE(ground.hit());
    CHECK(ground.hit()->kind ==
        movement::PlayerMovementHitKind::explicit_synthetic_brush);
    CHECK(ground.hit()->source_model_index == 3U);
    REQUIRE(ground.hit()->stable_instance_ordinal);
    CHECK(*ground.hit()->stable_instance_ordinal == 11U);
    REQUIRE(ground.hit()->source_entity_index);
    CHECK(*ground.hit()->source_entity_index == 42U);
    CHECK(ground.plane().normal.z == 1.0F);
    CHECK(ground.plane().distance == 4.5);
    REQUIRE(ground.plane().source_plane_index);
    CHECK(*ground.plane().source_plane_index == 19U);
    CHECK(ground.contact_position().z == 35.5F);
    CHECK(ground.probe_fraction() == 0.25);
    CHECK(ground.evidence_profile() == movement::
        PlayerGroundEvidenceProfile::deterministic_collision_trace_v1);

    CHECK(state.view_offset().z == 28.0F);
    CHECK(state.old_buttons() == 0x0022U);
    CHECK(state.source_command_sequence() == 77U);
    CHECK(state.simulation_time_nanoseconds() == 123'456'789U);
    CHECK(state.last_valid_contents() ==
        movement::PlayerMovementContents::empty);
    CHECK(state.gravity_multiplier() == 0.75F);
    CHECK(state.friction_multiplier() == 0.5F);
    CHECK(state.state_revision() == 9U);
    CHECK(state.compatibility_profile() == movement::
        GoldSrcMovementCompatibilityProfile::
            public_valve_pm_shared_dry_walk_subset_v1);
    CHECK(state.evidence_profile() == movement::
        GoldSrcMovementEvidenceProfile::
            public_valve_pm_shared_and_independent_fixtures);
    CHECK(state.command_profile() == movement::
        GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1);

    const auto signature =
        movement::local_player_movement_state_signature(state);
    CHECK(signature != 0U);
    const auto copied = state;
    CHECK(movement::local_player_movement_state_signature(copied) == signature);
}

TEST_CASE("Local player movement state accepts explicit ungrounded modes",
    "[movement][state][mode]")
{
    constexpr std::array modes{
        movement::PlayerMovementMode::airborne,
        movement::PlayerMovementMode::unsupported_liquid,
        movement::PlayerMovementMode::unsupported_ladder,
        movement::PlayerMovementMode::invalid_or_stuck,
    };
    for (const auto mode : modes) {
        INFO("mode=" << movement::to_string(mode));
        auto info = valid_state();
        info.hull = movement::PlayerMovementHull::ducked;
        info.mode = mode;
        info.ground = {};
        info.view_offset = {0.0F, 0.0F, 12.0F};
        info.friction_multiplier = 0.0F;
        const auto created = movement::LocalPlayerMovementState::create(info);
        REQUIRE(created);
        CHECK(created.state->mode() == mode);
        CHECK(created.state->hull() == movement::PlayerMovementHull::ducked);
        CHECK_FALSE(created.state->ground_state().grounded());
        CHECK_FALSE(created.state->ground_state().walkable());
        CHECK_FALSE(created.state->ground_state().hit());
        CHECK(created.state->friction_multiplier() == 0.0F);
    }
}

TEST_CASE("Local player movement state limits are finite positive and bounded",
    "[movement][state][limits]")
{
    CHECK(movement::valid_local_player_movement_state_limits({}));
    CHECK_FALSE(movement::valid_local_player_movement_state_limits(
        {0.0F, 1.0F, 1.0F, 1U}));
    CHECK_FALSE(movement::valid_local_player_movement_state_limits(
        {1.0F, -1.0F, 1.0F, 1U}));
    CHECK_FALSE(movement::valid_local_player_movement_state_limits(
        {1.0F, 1.0F, 0.0F, 1U}));
    CHECK_FALSE(movement::valid_local_player_movement_state_limits(
        {1.0F, 1.0F, 1.0F, 0U}));
    CHECK_FALSE(movement::valid_local_player_movement_state_limits(
        {std::numeric_limits<float>::infinity(), 1.0F, 1.0F, 1U}));

    check_error(movement::LocalPlayerMovementState::create(
                    valid_state(), {0.0F, 1.0F, 1.0F, 1U}),
        movement::LocalPlayerMovementStateErrorCode::invalid_limits);
}

TEST_CASE("Local player movement state rejects non-finite vectors",
    "[movement][state][validation][finite]")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    struct Case {
        void (*mutate)(movement::LocalPlayerMovementStateCreateInfo&, float);
        movement::LocalPlayerMovementStateErrorCode expected;
    };
    const std::array cases{
        Case{[](auto& info, const float value) { info.origin.x = value; },
            movement::LocalPlayerMovementStateErrorCode::non_finite_origin},
        Case{[](auto& info, const float value) { info.velocity.y = value; },
            movement::LocalPlayerMovementStateErrorCode::non_finite_velocity},
        Case{[](auto& info, const float value) { info.view_angles.z = value; },
            movement::LocalPlayerMovementStateErrorCode::non_finite_angles},
        Case{[](auto& info, const float value) { info.view_offset.z = value; },
            movement::LocalPlayerMovementStateErrorCode::non_finite_view_offset},
    };

    for (const auto& test : cases) {
        auto info = valid_state();
        test.mutate(info, nan);
        check_error(movement::LocalPlayerMovementState::create(info),
            test.expected);
    }
}

TEST_CASE("Local player movement state enforces safety bounds",
    "[movement][state][validation][bounds]")
{
    const movement::LocalPlayerMovementStateLimits limits{
        100.0F, 50.0F, 180.0F, 10U};

    SECTION("origin")
    {
        auto info = valid_state();
        info.origin.x = 100.01F;
        check_error(movement::LocalPlayerMovementState::create(info, limits),
            movement::LocalPlayerMovementStateErrorCode::
                coordinate_limit_exceeded);
    }
    SECTION("view offset")
    {
        auto info = valid_state();
        info.view_offset.z = -100.01F;
        check_error(movement::LocalPlayerMovementState::create(info, limits),
            movement::LocalPlayerMovementStateErrorCode::
                coordinate_limit_exceeded);
    }
    SECTION("velocity")
    {
        auto info = valid_state();
        info.velocity.y = -50.01F;
        check_error(movement::LocalPlayerMovementState::create(info, limits),
            movement::LocalPlayerMovementStateErrorCode::
                velocity_limit_exceeded);
    }
    SECTION("view angle")
    {
        auto info = valid_state();
        info.view_angles.y = 180.01F;
        check_error(movement::LocalPlayerMovementState::create(info, limits),
            movement::LocalPlayerMovementStateErrorCode::angle_limit_exceeded);
    }
    SECTION("revision")
    {
        auto info = valid_state();
        info.state_revision = 11U;
        check_error(movement::LocalPlayerMovementState::create(info, limits),
            movement::LocalPlayerMovementStateErrorCode::invalid_revision);
    }
}

TEST_CASE("Local player movement state rejects invalid enums and multipliers",
    "[movement][state][validation]")
{
    SECTION("hull")
    {
        auto info = valid_state();
        info.hull = static_cast<movement::PlayerMovementHull>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_hull);
    }
    SECTION("mode")
    {
        auto info = valid_state();
        info.mode = static_cast<movement::PlayerMovementMode>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_mode);
    }
    SECTION("contents")
    {
        auto info = valid_state();
        info.last_valid_contents =
            static_cast<movement::PlayerMovementContents>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_mode);
    }
    SECTION("gravity multiplier")
    {
        auto info = valid_state();
        info.gravity_multiplier = 0.0F;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_multiplier);
    }
    SECTION("friction multiplier")
    {
        auto info = valid_state();
        info.friction_multiplier = -0.01F;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_multiplier);
    }
    SECTION("non-finite multiplier")
    {
        auto info = valid_state();
        info.gravity_multiplier = std::numeric_limits<float>::infinity();
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_multiplier);
    }
    SECTION("zero revision")
    {
        auto info = valid_state();
        info.state_revision = 0U;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_revision);
    }
}

TEST_CASE("Local player movement ground metadata is internally consistent",
    "[movement][state][ground][validation]")
{
    SECTION("walking requires a grounded contact")
    {
        auto info = valid_state();
        info.ground = {};
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("airborne cannot retain a ground hit")
    {
        auto info = valid_state();
        info.mode = movement::PlayerMovementMode::airborne;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("grounded requires walkable")
    {
        auto info = valid_state();
        info.ground.walkable = false;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("grounded requires a hit identity")
    {
        auto info = valid_state();
        info.ground.hit.reset();
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("hit kind is closed")
    {
        auto info = valid_state();
        info.ground.hit->kind =
            static_cast<movement::PlayerMovementHitKind>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("world hit identity has only the world model")
    {
        auto info = valid_state();
        info.ground.hit = movement::PlayerMovementHitIdentity{
            movement::PlayerMovementHitKind::world,
            1U,
            std::nullopt,
            std::nullopt,
        };
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);

        info.ground.hit = movement::PlayerMovementHitIdentity{
            movement::PlayerMovementHitKind::world,
            0U,
            0U,
            std::nullopt,
        };
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);

        info.ground.hit = movement::PlayerMovementHitIdentity{
            movement::PlayerMovementHitKind::world,
            0U,
            std::nullopt,
            1U,
        };
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("synthetic brush hit identity is stable and non-world")
    {
        auto info = valid_state();
        info.ground.hit->source_model_index = 0U;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);

        info = valid_state();
        info.ground.hit->stable_instance_ordinal.reset();
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("canonical world identity is accepted")
    {
        auto info = valid_state();
        info.ground.hit = movement::PlayerMovementHitIdentity{
            movement::PlayerMovementHitKind::world,
            0U,
            std::nullopt,
            std::nullopt,
        };
        const auto created = movement::LocalPlayerMovementState::create(info);
        REQUIRE(created);
        REQUIRE(created.state);
        REQUIRE(created.state->ground_state().hit());
        CHECK(created.state->ground_state().hit()->kind ==
            movement::PlayerMovementHitKind::world);
    }
    SECTION("normal must be unit length")
    {
        auto info = valid_state();
        info.ground.plane.normal = {0.0F, 0.0F, 0.99F};
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("walkable normal must point upward")
    {
        auto info = valid_state();
        info.ground.plane.normal = {1.0F, 0.0F, 0.0F};
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);

        info = valid_state();
        info.ground.plane.normal = {0.0F, 0.0F, -1.0F};
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);

        info = valid_state();
        info.ground.plane.normal = {0.8F, 0.0F, 0.6F};
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("probe fraction must be closed-unit")
    {
        auto info = valid_state();
        info.ground.probe_fraction = 1.01;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("contact must remain bounded")
    {
        auto info = valid_state();
        info.ground.contact_position.x = 1'000'001.0F;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("ground evidence profile is closed")
    {
        auto info = valid_state();
        info.ground.evidence_profile =
            static_cast<movement::PlayerGroundEvidenceProfile>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
    SECTION("ungrounded metadata cannot claim walkable")
    {
        auto info = valid_state();
        info.mode = movement::PlayerMovementMode::airborne;
        info.ground.grounded = false;
        info.ground.hit.reset();
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::invalid_ground_state);
    }
}

TEST_CASE("Movement execution profiles fail closed until evidence exists",
    "[movement][state][profile]")
{
    SECTION("pending movement compatibility")
    {
        auto info = valid_state();
        info.compatibility_profile = movement::
            GoldSrcMovementCompatibilityProfile::
                stock_pm_move_full_compatibility_evidence_pending;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::stock_evidence_pending);
    }
    SECTION("pending movement evidence")
    {
        auto info = valid_state();
        info.evidence_profile = movement::GoldSrcMovementEvidenceProfile::
            stock_pm_move_full_compatibility_evidence_pending;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::stock_evidence_pending);
    }
    SECTION("pending command semantics")
    {
        auto info = valid_state();
        info.command_profile = movement::GoldSrcMovementCommandProfile::
            stock_usercmd_semantics_evidence_pending;
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::stock_evidence_pending);
    }
    SECTION("unknown compatibility")
    {
        auto info = valid_state();
        info.compatibility_profile =
            static_cast<movement::GoldSrcMovementCompatibilityProfile>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::unsupported_profile);
    }
    SECTION("unknown evidence")
    {
        auto info = valid_state();
        info.evidence_profile =
            static_cast<movement::GoldSrcMovementEvidenceProfile>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::unsupported_profile);
    }
    SECTION("unknown command semantics")
    {
        auto info = valid_state();
        info.command_profile =
            static_cast<movement::GoldSrcMovementCommandProfile>(255U);
        check_error(movement::LocalPlayerMovementState::create(info),
            movement::LocalPlayerMovementStateErrorCode::unsupported_profile);
    }
}

TEST_CASE("Movement state signatures include command and hit identity",
    "[movement][state][signature]")
{
    const auto first =
        movement::LocalPlayerMovementState::create(valid_state());
    REQUIRE(first);
    const auto baseline =
        movement::local_player_movement_state_signature(*first.state);

    auto changed_info = valid_state();
    changed_info.old_buttons ^= 1U;
    const auto buttons =
        movement::LocalPlayerMovementState::create(changed_info);
    REQUIRE(buttons);
    CHECK(movement::local_player_movement_state_signature(*buttons.state) !=
        baseline);

    changed_info = valid_state();
    changed_info.ground.hit->stable_instance_ordinal = 12U;
    const auto identity =
        movement::LocalPlayerMovementState::create(changed_info);
    REQUIRE(identity);
    CHECK(movement::local_player_movement_state_signature(*identity.state) !=
        baseline);

    changed_info = valid_state();
    changed_info.ground.plane.distance = 4.75;
    const auto plane =
        movement::LocalPlayerMovementState::create(changed_info);
    REQUIRE(plane);
    CHECK(movement::local_player_movement_state_signature(*plane.state) !=
        baseline);
}

TEST_CASE("Movement state profile labels are explicit",
    "[movement][state][profile][label]")
{
    CHECK(movement::to_string(movement::GoldSrcMovementCompatibilityProfile::
              public_valve_pm_shared_dry_walk_subset_v1) ==
        "public_valve_pm_shared_dry_walk_subset_v1");
    CHECK(movement::to_string(movement::GoldSrcMovementEvidenceProfile::
              public_valve_pm_shared_and_independent_fixtures) ==
        "public_valve_pm_shared_and_independent_fixtures");
    CHECK(movement::to_string(movement::GoldSrcMovementCommandProfile::
              synthetic_usercmd_semantics_v1) ==
        "synthetic_usercmd_semantics_v1");
    CHECK(movement::to_string(movement::PlayerMovementHull::standing) ==
        "standing");
    CHECK(movement::to_string(movement::PlayerMovementMode::airborne) ==
        "airborne");
    CHECK(movement::to_string(movement::PlayerMovementContents::water) ==
        "water");
    CHECK(movement::to_string(
              static_cast<movement::PlayerMovementHull>(255U)) ==
        "unknown");
}
