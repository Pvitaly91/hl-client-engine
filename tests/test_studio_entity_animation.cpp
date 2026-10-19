#include <hlclient/goldsrc/studio/goldsrc_studio_entity_animation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace {
namespace a = hlclient::assets;
namespace c = hlclient::client;
namespace s = hlclient::goldsrc::studio;
using Catch::Approx;

struct AnimationFixture {
    a::SkeletalModelAssetData model;
    c::RuntimePacketEntityObservation current, previous;
    s::StudioEntityAnimationPresentation presentation;
    AnimationFixture() {
        a::ModelSequence sequence;
        sequence.frame_count = 11U; sequence.frames_per_second = 10.0F;
        sequence.blend_count = 1U;
        model.sequences = {sequence, sequence};
        current.entity_number = 42U; current.model_index = 7U;
        current.ordinary_visual_schema = true; current.brush_move_type = 0U;
        current.sequence = 0U; current.frame = 0.0;
        current.animation_time_seconds = 1.0; current.frame_rate = 1.0;
        current.effects = 0U;
        current.controllers.fill(0U); current.blending.fill(0U);
        previous = current;
    }
    s::StudioEntityAnimationContext context(double sample = 1.0, double server = 1.0) {
        s::StudioEntityAnimationContext result{model, current};
        result.generation = 1U; result.model_resource_id = 7U;
        result.model_revision = {1U, 2U}; result.record_ordinal = 1U;
        result.current_server_seconds = server; result.sample_server_seconds = sample;
        result.base.compatibility_profile = s::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
        return result;
    }
};
}

TEST_CASE("Ordinary Studio playback advances between unchanged snapshots for every movetype", "[studio][animation]") {
    for (const auto move : {0U, 4U, 5U, 6U, 9U, 10U, 11U}) {
        AnimationFixture fixture; fixture.current.brush_move_type = move;
        for (unsigned i = 0; i <= 20U; ++i) {
            auto context = fixture.context(1.0 + i * 0.01);
            const auto sample = fixture.presentation.sample(context);
            REQUIRE(sample);
            CHECK(sample->main.frame_coordinate == Approx(i * 0.1));
            CHECK_FALSE(sample->previous);
        }
    }
}

TEST_CASE("Ordinary Studio frame clock uses authored fps and signed rate with stock loop bounds", "[studio][animation]") {
    AnimationFixture fixture;
    SECTION("Forward rate and authored fps") {
        fixture.model.sequences[0].frames_per_second = 20.0F;
        fixture.current.frame = 128.0; fixture.current.frame_rate = 0.5;
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(6.0));
    }
    SECTION("Reverse nonloop reaches its first key") {
        fixture.current.frame = 25.6; fixture.current.frame_rate = -1.0;
        const auto sample = fixture.presentation.sample(fixture.context(1.2));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == 0.0);
    }
    SECTION("Reverse looping playback wraps rather than holding") {
        fixture.model.sequences[0].source_flags = 1U;
        fixture.current.frame = 25.6; fixture.current.frame_rate = -1.0;
        const auto sample = fixture.presentation.sample(fixture.context(1.2));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(9.0));
    }
    SECTION("Forward looping playback wraps at numframes minus one") {
        fixture.model.sequences[0].source_flags = 1U;
        fixture.current.frame = 243.2;
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(0.5));
    }
    SECTION("Nonloop clamps like StudioEstimateFrame") {
        fixture.current.frame = 255.0;
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(9.999));
    }
    SECTION("Single-key sequence never addresses a second frame") {
        fixture.model.sequences[0].frame_count = 1U;
        fixture.model.sequences[0].source_flags = 1U;
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == 0.0);
    }
    SECTION("Future animtime does not predict backwards") {
        fixture.current.frame = 128.0; fixture.current.animation_time_seconds = 1.2;
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(5.0));
    }
    SECTION("An explicitly stopped rate stays stopped") {
        fixture.current.frame = 128.0; fixture.current.frame_rate = 0.0;
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(5.0));
    }
}

TEST_CASE("Ordinary Studio delayed playback selects coherent raw anchors instead of mixed frames", "[studio][animation]") {
    AnimationFixture fixture;
    fixture.current.animation_time_seconds = 1.1; fixture.current.frame = 25.6;
    auto context = fixture.context(1.05, 1.1);
    context.previous = &fixture.previous; context.previous_server_seconds = 1.0;
    SECTION("Nonloop playback does not freeze at a future anchor") {
        const auto sample = fixture.presentation.sample(context);
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(0.5));
    }
    SECTION("Loop wrap uses prior real anchor") {
        fixture.model.sequences[0].source_flags = 1U;
        fixture.previous.frame = 243.2; fixture.current.frame = 12.8;
        const auto sample = fixture.presentation.sample(context);
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(0.0).margin(1e-10));
    }
    SECTION("Reverse playback uses signed rate between packets") {
        fixture.previous.frame = 51.2; fixture.previous.frame_rate = -1.0;
        fixture.current.frame_rate = -1.0;
        const auto sample = fixture.presentation.sample(context);
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(1.5));
    }
    SECTION("The new raw anchor takes over without double counting") {
        context.sample_server_seconds = 1.15;
        const auto sample = fixture.presentation.sample(context);
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(1.5));
    }
    SECTION("An interval beyond the bounded gap cannot contribute old pose") {
        context.previous_server_seconds = 0.0;
        const auto sample = fixture.presentation.sample(context);
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(1.0));
    }
}

TEST_CASE("Ordinary Studio sequence blend does not restart on each server publication", "[studio][animation]") {
    AnimationFixture fixture;
    REQUIRE(fixture.presentation.sample(fixture.context()));
    fixture.current.sequence = 1U; fixture.current.animation_time_seconds = 1.1;
    auto context = fixture.context(1.1, 1.1);
    context.record_ordinal = 2U; context.previous = &fixture.previous; context.previous_server_seconds = 1.0;
    auto sample = fixture.presentation.sample(context);
    REQUIRE(sample); REQUIRE(sample->previous);
    CHECK(sample->previous->sequence_index == 0U);
    CHECK(sample->main.sequence_index == 1U);
    CHECK(sample->previous_weight == Approx(1.0));
    context.sample_server_seconds = 1.15;
    sample = fixture.presentation.sample(context);
    REQUIRE(sample); REQUIRE(sample->previous);
    CHECK(sample->previous_weight == Approx(0.75));
    fixture.previous = fixture.current;
    fixture.current.animation_time_seconds = 1.2; fixture.current.frame = 25.6;
    context.current_server_seconds = 1.2; context.previous_server_seconds = 1.1;
    context.sample_server_seconds = 1.2; context.record_ordinal = 3U;
    sample = fixture.presentation.sample(context);
    REQUIRE(sample); REQUIRE(sample->previous);
    CHECK(sample->previous_weight == Approx(0.5));
    CHECK(sample->main.frame_coordinate == Approx(1.0));
    context.sample_server_seconds = 1.31;
    sample = fixture.presentation.sample(context);
    REQUIRE(sample); CHECK_FALSE(sample->previous);
}

TEST_CASE("Ordinary Studio delayed sequence switch retains old sequence only until its exact boundary", "[studio][animation]") {
    AnimationFixture fixture;
    REQUIRE(fixture.presentation.sample(fixture.context()));
    fixture.current.sequence = 1U; fixture.current.animation_time_seconds = 1.1;
    auto context = fixture.context(1.05, 1.1);
    context.record_ordinal = 2U; context.previous = &fixture.previous; context.previous_server_seconds = 1.0;
    auto sample = fixture.presentation.sample(context);
    REQUIRE(sample); CHECK(sample->main.sequence_index == 0U); CHECK_FALSE(sample->previous);
    CHECK(sample->main.frame_coordinate == Approx(0.5));
    context.sample_server_seconds = 1.1;
    sample = fixture.presentation.sample(context);
    REQUIRE(sample); REQUIRE(sample->previous);
    CHECK(sample->main.sequence_index == 1U);
    CHECK(sample->previous->frame_coordinate == Approx(0.5));
}

TEST_CASE("Ordinary Studio discontinuities never blend an earlier lifetime", "[studio][animation]") {
    AnimationFixture fixture;
    REQUIRE(fixture.presentation.sample(fixture.context()));
    fixture.current.sequence = 1U; fixture.current.animation_time_seconds = 1.1;
    auto context = fixture.context(1.1, 1.1); context.record_ordinal = 2U;
    SECTION("Explicit boundary") { context.discontinuity = true; }
    SECTION("EF_NOINTERP") { fixture.current.effects = 32U; }
    SECTION("Caller disables interpolation") { context.no_interpolation = true; }
    SECTION("Generation") { context.generation = 2U; }
    SECTION("Model resource") { context.model_resource_id = 8U; }
    SECTION("Model revision") { context.model_revision.secondary = 3U; }
    SECTION("Model slot") { fixture.current.model_index = 8U; }
    SECTION("Movement kind") { fixture.current.brush_move_type = 4U; }
    SECTION("Removal and reuse") { fixture.presentation.retain_entities({}); }
    SECTION("Explicit reset") { fixture.presentation.reset(); }
    SECTION("Source clock rollback") { context.current_server_seconds = 0.9; context.sample_server_seconds = 0.9; }
    SECTION("Render clock rollback") { context.sample_server_seconds = 0.95; }
    SECTION("Source ordinal rollback") { context.record_ordinal = 0U; }
    SECTION("Long gap") { context.current_server_seconds = 2.0; context.sample_server_seconds = 2.0; }
    auto sample = fixture.presentation.sample(context);
    if (!context.record_ordinal) CHECK_FALSE(sample);
    else { REQUIRE(sample); CHECK_FALSE(sample->previous); }
}

TEST_CASE("Ordinary Studio nointerp snaps frame and does not leak transition after clearing", "[studio][animation]") {
    AnimationFixture fixture;
    fixture.current.frame = 128.0; fixture.current.effects = 32U;
    auto sample = fixture.presentation.sample(fixture.context(1.1));
    REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(5.0));
    fixture.current.effects = 0U; fixture.current.sequence = 1U;
    sample = fixture.presentation.sample(fixture.context(1.15));
    REQUIRE(sample); CHECK_FALSE(sample->previous);
}

TEST_CASE("Ordinary Studio channel interpolation respects authored circular controllers", "[studio][animation]") {
    AnimationFixture fixture;
    a::ModelBoneController circular; circular.controller_index = 0; circular.source_type = 0x8008U;
    fixture.model.bone_controllers.push_back(circular);
    fixture.previous.controllers = {250U, 0U, 10U, 200U};
    fixture.current.controllers = {6U, 200U, 110U, 100U};
    fixture.previous.blending = {0U, 200U}; fixture.current.blending = {200U, 0U};
    fixture.current.animation_time_seconds = 1.1;
    auto context = fixture.context(1.05, 1.1);
    context.previous = &fixture.previous; context.previous_server_seconds = 1.0;
    const auto sample = fixture.presentation.sample(context);
    REQUIRE(sample);
    CHECK((sample->main.controller_values == std::array<std::uint8_t, 4>{0U, 100U, 60U, 150U}));
    CHECK((sample->main.blending_values == std::array<std::uint8_t, 2>{100U, 100U}));
}

TEST_CASE("Ordinary Studio missing descriptors are discrete and malformed anchors are rejected", "[studio][animation]") {
    AnimationFixture fixture;
    fixture.current.frame = 128.0;
    SECTION("No animtime") {
        fixture.current.animation_time_seconds.reset();
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(5.0));
    }
    SECTION("No framerate") {
        fixture.current.frame_rate.reset();
        const auto sample = fixture.presentation.sample(fixture.context(1.1));
        REQUIRE(sample); CHECK(sample->main.frame_coordinate == Approx(5.0));
    }
    SECTION("Invalid sequence") {
        fixture.current.sequence = 12U;
        CHECK_FALSE(fixture.presentation.sample(fixture.context()));
    }
    SECTION("Nonfinite time") {
        fixture.current.animation_time_seconds = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(fixture.presentation.sample(fixture.context()));
    }
    SECTION("Nonfinite rate") {
        fixture.current.frame_rate = std::numeric_limits<double>::infinity();
        CHECK_FALSE(fixture.presentation.sample(fixture.context()));
    }
    SECTION("Overflowing frame product") {
        fixture.current.frame_rate = std::numeric_limits<double>::max();
        CHECK_FALSE(fixture.presentation.sample(fixture.context(1.2)));
    }
    SECTION("Invalid channel byte") {
        fixture.current.controllers[0] = 256U;
        CHECK_FALSE(fixture.presentation.sample(fixture.context()));
    }
    SECTION("Outside bounded presentation clock") {
        CHECK_FALSE(fixture.presentation.sample(fixture.context(2.0)));
    }
    SECTION("Player path remains separate") {
        fixture.current.player_movement_schema = true;
        CHECK_FALSE(fixture.presentation.sample(fixture.context()));
    }
}

TEST_CASE("Ordinary Studio accepted presence preserves transition history but removal clears it", "[studio][animation]") {
    AnimationFixture fixture;
    REQUIRE(fixture.presentation.sample(fixture.context()));
    const std::array present{fixture.current};
    fixture.presentation.retain_entities(present);
    fixture.current.sequence = 1U; fixture.current.animation_time_seconds = 1.1;
    const auto sample = fixture.presentation.sample(fixture.context(1.1, 1.1));
    REQUIRE(sample); REQUIRE(sample->previous);
}
