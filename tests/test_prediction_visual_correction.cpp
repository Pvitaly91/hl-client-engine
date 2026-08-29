#include <hlclient/prediction/prediction_state_comparison.hpp>
#include <hlclient/prediction/prediction_visual_correction.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace camera = hlclient::gameplay_camera;
namespace collision = hlclient::collision;
namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;

constexpr double kCorrectionDurationSeconds = 0.100;
constexpr float kVectorMargin = 1.0e-5F;

[[nodiscard]] collision::CollisionContents contents(const std::int32_t raw)
{
    const auto decoded = collision::decode_goldsrc_contents({raw});
    REQUIRE(decoded);
    return *decoded;
}

[[nodiscard]] std::array<collision::CollisionHull, 4U> hulls()
{
    std::array<collision::CollisionHull, 4U> output{};
    for (std::size_t index = 0U; index < output.size(); ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto profile = collision::standard_collision_hull_profile(
            *ordinal);
        REQUIRE(profile);
        output[index] = {
            *ordinal,
            index == 0U ? collision::CollisionHullTreeDomain::node_leaf
                        : collision::CollisionHullTreeDomain::clipnode,
            collision::CollisionHullRoot{
                index == 0U ? collision::CollisionHullRootKind::node
                            : collision::CollisionHullRootKind::clipnode,
                0U,
                contents(-1),
            },
            *profile,
        };
    }
    return output;
}

[[nodiscard]] collision::CollisionModel model()
{
    return {
        0U,
        {},
        assets::WorldBounds{
            {-128.0F, -128.0F, -128.0F},
            {128.0F, 128.0F, 128.0F}},
        0U,
        1U,
        hulls(),
    };
}

// A literal BSP half-space. Child zero is the front leaf and child one is the
// back leaf, matching the public BSP collision package representation.
[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
half_space_package(
    const assets::AssetVector3 normal,
    const double distance,
    const std::int32_t front_contents,
    const std::int32_t back_contents)
{
    const auto source_type = normal.x != 0.0F
        ? 0
        : normal.y != 0.0F ? 1 : 2;
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            {normal, distance, 0U, source_type}},
        std::vector<collision::CollisionNode>{
            {0U,
                {collision::CollisionNodeChild{
                     collision::CollisionNodeChildKind::leaf, 1U},
                    collision::CollisionNodeChild{
                        collision::CollisionNodeChildKind::leaf, 0U}}}},
        std::vector<collision::CollisionLeaf>{
            {0U, contents(back_contents)},
            {1U, contents(front_contents)}},
        std::vector<collision::CollisionClipnode>{
            {0U,
                {collision::CollisionClipnodeChild{
                     collision::CollisionClipnodeChildKind::terminal,
                     0U,
                     contents(front_contents)},
                    collision::CollisionClipnodeChild{
                        collision::CollisionClipnodeChildKind::terminal,
                        0U,
                        contents(back_contents)}}}},
        std::vector<collision::CollisionModel>{model()});
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
empty_package()
{
    return half_space_package({1.0F, 0.0F, 0.0F}, 0.0, -1, -1);
}

[[nodiscard]] camera::GameplayCameraState make_camera(
    const assets::AssetVector3 position,
    const std::uint64_t revision = 40U)
{
    camera::GameplayCameraStateCreateInfo info;
    info.position = position;
    info.mode = camera::GameplayCameraMode::player_walk;
    info.revision = revision;
    auto created = camera::GameplayCameraState::create(info);
    REQUIRE(created);
    REQUIRE(created.state);
    return std::move(*created.state);
}

[[nodiscard]] movement::LocalPlayerMovementState make_simulation_state(
    const movement::PlayerMovementHull hull =
        movement::PlayerMovementHull::standing)
{
    movement::LocalPlayerMovementStateCreateInfo info;
    info.origin = {20.0F, -5.0F, 0.0F};
    info.velocity = {80.0F, 0.0F, 0.0F};
    info.hull = hull;
    info.view_offset = hull == movement::PlayerMovementHull::ducked
        ? assets::AssetVector3{0.0F, 0.0F, 12.0F}
        : assets::AssetVector3{0.0F, 0.0F, 28.0F};
    info.source_command_sequence = 12U;
    info.simulation_time_nanoseconds = 300'000'000U;
    info.state_revision = 17U;
    auto created = movement::LocalPlayerMovementState::create(info);
    REQUIRE(created);
    REQUIRE(created.state);
    return std::move(*created.state);
}

[[nodiscard]] prediction::PredictionVisualCorrectionState begin_correction(
    const assets::AssetVector3 old_physical_eye,
    const assets::AssetVector3 corrected_eye,
    const prediction::PredictionCorrectionClass correction_class,
    const double start_time,
    const prediction::PredictionVisualCorrectionConfig& config = {},
    const std::optional<prediction::PredictionVisualCorrectionState>& previous =
        std::nullopt,
    const std::uint64_t authority_ordinal = 1U,
    const std::uint64_t old_revision = 1U,
    const std::uint64_t new_revision = 2U)
{
    auto begun = prediction::begin_prediction_visual_correction(previous,
        old_physical_eye, corrected_eye, correction_class, start_time,
        authority_ordinal, old_revision, new_revision, config);
    REQUIRE(begun);
    REQUIRE(begun.correction);
    return std::move(*begun.correction);
}

void check_vector(
    const assets::AssetVector3& actual,
    const assets::AssetVector3& expected,
    const float margin = kVectorMargin)
{
    CHECK(actual.x == Catch::Approx(expected.x).margin(margin));
    CHECK(actual.y == Catch::Approx(expected.y).margin(margin));
    CHECK(actual.z == Catch::Approx(expected.z).margin(margin));
}

void check_camera_endpoint_is_free(
    const collision::CollisionWorldQuery& query,
    const assets::AssetVector3& start,
    const assets::AssetVector3& endpoint,
    collision::CollisionQueryScratch& scratch)
{
    collision::CollisionTraceRequest trace_request;
    trace_request.start = start;
    trace_request.end = endpoint;
    const auto traced = query.trace_line(trace_request, scratch);
    REQUIRE(traced);
    REQUIRE(traced.result);
    CHECK_FALSE(traced.result->start_solid);
    CHECK_FALSE(traced.result->all_solid);

    collision::CollisionPointContentsRequest position_request;
    position_request.point = endpoint;
    const auto tested = query.test_position(position_request, scratch);
    REQUIRE(tested);
    REQUIRE(tested.result);
    CHECK(tested.result->status == collision::CollisionPositionStatus::free);
}

struct VisualTimelineOutcome {
    assets::AssetVector3 residual{};
    assets::AssetVector3 displayed_eye{};
    std::uint64_t simulation_state_signature{0U};
    std::uint64_t authoritative_updates{0U};
    std::uint64_t replay_count{0U};
    std::uint64_t replayed_command_count{0U};
};

[[nodiscard]] std::vector<double> uniform_sample_times(
    const double frames_per_second,
    const double target_time)
{
    std::vector<double> output;
    for (std::size_t frame = 1U;; ++frame) {
        const auto time = static_cast<double>(frame) / frames_per_second;
        if (time >= target_time) {
            break;
        }
        output.push_back(time);
    }
    output.push_back(target_time);
    return output;
}

[[nodiscard]] VisualTimelineOutcome sample_visual_timeline(
    const std::vector<double>& sample_times,
    const collision::CollisionWorldQuery& query,
    const movement::LocalPlayerMovementState& corrected_simulation_state,
    const prediction::LocalPredictionStatistics& reconciliation_statistics)
{
    const auto corrected_eye = assets::AssetVector3{20.0F, -5.0F, 28.0F};
    const auto corrected_camera = make_camera(corrected_eye);
    auto current = std::optional<prediction::PredictionVisualCorrectionState>{
        begin_correction({24.0F, -5.0F, 28.0F}, corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.0)};
    collision::CollisionQueryScratch scratch;
    assets::AssetVector3 displayed_eye = corrected_eye;

    for (const auto sample_time : sample_times) {
        auto sampled = prediction::sample_prediction_visual_correction(
            *current, corrected_camera, sample_time, &query, scratch);
        REQUIRE(sampled);
        REQUIRE(sampled.camera);
        REQUIRE(sampled.correction);
        displayed_eye = sampled.camera->position();
        current.emplace(std::move(*sampled.correction));
    }

    return {
        current->current_residual_offset(),
        displayed_eye,
        movement::local_player_movement_state_signature(
            corrected_simulation_state),
        reconciliation_statistics.authoritative_updates,
        reconciliation_statistics.replay_count,
        reconciliation_statistics.replayed_command_count,
    };
}

} // namespace

TEST_CASE("Prediction visual correction uses a bounded 100 ms linear decay",
    "[prediction][visual-correction][smoothing]")
{
    const auto package = empty_package();
    const collision::CollisionWorldQuery query{package};
    const auto corrected_eye = assets::AssetVector3{10.0F, 2.0F, 3.0F};
    const auto corrected_camera = make_camera(corrected_eye);
    const auto start = 2.0;
    const auto correction = begin_correction({14.0F, 2.0F, 3.0F},
        corrected_eye,
        prediction::PredictionCorrectionClass::small_visual_correction,
        start);

    REQUIRE(correction.active());
    CHECK(correction.duration_seconds() ==
        Catch::Approx(kCorrectionDurationSeconds));
    check_vector(correction.initial_position_offset(), {4.0F, 0.0F, 0.0F});

    const auto check_sample = [&](const double sample_time,
                                  const float expected_x,
                                  const bool expected_completed) {
        collision::CollisionQueryScratch scratch;
        const auto sampled = prediction::sample_prediction_visual_correction(
            correction, corrected_camera, sample_time, &query, scratch);
        REQUIRE(sampled);
        REQUIRE(sampled.camera);
        REQUIRE(sampled.correction);
        CHECK(sampled.completed == expected_completed);
        CHECK_FALSE(sampled.constrained);
        CHECK(sampled.camera->position().x ==
            Catch::Approx(expected_x).margin(kVectorMargin));
        CHECK(sampled.camera->position().y == corrected_eye.y);
        CHECK(sampled.camera->position().z == corrected_eye.z);
        check_vector(sampled.correction->current_residual_offset(),
            {expected_x - corrected_eye.x, 0.0F, 0.0F});
        if (expected_completed) {
            CHECK_FALSE(sampled.correction->active());
            CHECK(sampled.correction->duration_seconds() == 0.0);
        }
    };

    SECTION("time before start and t zero retain the full offset")
    {
        check_sample(start - 0.050, 14.0F, false);
        check_sample(start, 14.0F, false);
    }

    SECTION("midpoint has half of the original residual")
    {
        check_sample(start + kCorrectionDurationSeconds * 0.5, 12.0F,
            false);
    }

    SECTION("completion and time after completion remove the residual")
    {
        check_sample(start + kCorrectionDurationSeconds, 10.0F, true);
        check_sample(start + kCorrectionDurationSeconds + 0.250, 10.0F,
            true);
    }
}

TEST_CASE("Prediction visual correction publishes monotonic camera revisions",
    "[prediction][visual-correction][revision]")
{
    const collision::CollisionWorldQuery query{empty_package()};
    collision::CollisionQueryScratch scratch;
    const auto corrected_eye = assets::AssetVector3{10.0F, 2.0F, 3.0F};
    const auto corrected_camera = make_camera(corrected_eye, 40U);
    const auto initial = begin_correction({14.0F, 2.0F, 3.0F},
        corrected_eye,
        prediction::PredictionCorrectionClass::small_visual_correction,
        2.0);

    SECTION("start midpoint and completion are strictly increasing")
    {
        const auto start = prediction::sample_prediction_visual_correction(
            initial, corrected_camera, 2.0, &query, scratch);
        REQUIRE(start);
        REQUIRE(start.camera);
        REQUIRE(start.correction);
        CHECK(start.camera->revision() == 41U);
        CHECK(start.correction->camera_publication_revision() ==
            start.camera->revision());

        const auto repeated = prediction::sample_prediction_visual_correction(
            *start.correction, corrected_camera, 2.0, &query, scratch);
        REQUIRE(repeated);
        REQUIRE(repeated.camera);
        REQUIRE(repeated.correction);
        CHECK(repeated.camera->revision() == start.camera->revision());

        const auto midpoint = prediction::sample_prediction_visual_correction(
            *repeated.correction, corrected_camera, 2.050, &query, scratch);
        REQUIRE(midpoint);
        REQUIRE(midpoint.camera);
        REQUIRE(midpoint.correction);
        CHECK(start.camera->revision() < midpoint.camera->revision());
        CHECK(midpoint.correction->camera_publication_revision() ==
            midpoint.camera->revision());

        const auto completion =
            prediction::sample_prediction_visual_correction(
                *midpoint.correction, corrected_camera, 2.101, &query,
                scratch);
        REQUIRE(completion);
        REQUIRE(completion.camera);
        REQUIRE(completion.correction);
        REQUIRE(completion.completed);
        CHECK(midpoint.camera->revision() < completion.camera->revision());
        CHECK(completion.correction->camera_publication_revision() ==
            completion.camera->revision());
        CHECK_FALSE(completion.correction->active());

        const auto inactive = prediction::sample_prediction_visual_correction(
            *completion.correction, corrected_camera, 2.200, nullptr, scratch);
        REQUIRE(inactive);
        REQUIRE(inactive.camera);
        REQUIRE(inactive.correction);
        CHECK_FALSE(inactive.completed);
        CHECK(inactive.camera->revision() == completion.camera->revision());
        CHECK(inactive.correction->camera_publication_revision() ==
            completion.camera->revision());
    }

    SECTION("the configured maximum fails before a revision wraps")
    {
        constexpr std::uint64_t maximum_camera_revision = 41U;
        const auto at_limit = prediction::sample_prediction_visual_correction(
            initial, corrected_camera, 2.0, &query, scratch, {},
            maximum_camera_revision);
        REQUIRE(at_limit);
        REQUIRE(at_limit.camera);
        REQUIRE(at_limit.correction);
        CHECK(at_limit.camera->revision() == maximum_camera_revision);

        const auto exhausted =
            prediction::sample_prediction_visual_correction(
                *at_limit.correction, corrected_camera, 2.050, &query,
                scratch, {}, maximum_camera_revision);
        REQUIRE_FALSE(exhausted);
        CHECK_FALSE(exhausted.camera);
        CHECK_FALSE(exhausted.correction);
        REQUIRE(exhausted.error);
        CHECK(exhausted.error->code ==
            prediction::PredictionErrorCode::revision_exhausted);
        CHECK(at_limit.correction->camera_publication_revision() ==
            maximum_camera_revision);
    }
}

TEST_CASE("Prediction visual correction handles zero invalid and snap cases",
    "[prediction][visual-correction][snap]")
{
    const auto corrected_eye = assets::AssetVector3{3.0F, 4.0F, 5.0F};
    const auto corrected_camera = make_camera(corrected_eye);
    collision::CollisionQueryScratch scratch;

    SECTION("zero correction does not require collision or revise the camera")
    {
        const auto correction = begin_correction(corrected_eye, corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            1.0);
        CHECK_FALSE(correction.active());
        check_vector(correction.current_residual_offset(), {});

        const auto sampled = prediction::sample_prediction_visual_correction(
            correction, corrected_camera, 1.0, nullptr, scratch);
        REQUIRE(sampled);
        REQUIRE(sampled.camera);
        REQUIRE(sampled.correction);
        check_vector(sampled.camera->position(), corrected_eye);
        CHECK(sampled.camera->revision() == corrected_camera.revision());
        CHECK_FALSE(sampled.completed);
    }

    SECTION("duration zero snaps a nominally small correction")
    {
        prediction::PredictionVisualCorrectionConfig config;
        config.duration_seconds = 0.0;
        const auto correction = begin_correction({5.0F, 4.0F, 5.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            1.0, config);
        CHECK_FALSE(correction.active());
        CHECK(correction.duration_seconds() == 0.0);
        CHECK(correction.profile() ==
            prediction::PredictionVisualCorrectionProfile::
                no_smoothing_snap_v1);
        check_vector(correction.current_residual_offset(), {});
    }

    SECTION("large correction snaps")
    {
        const auto correction = begin_correction({12.0F, 4.0F, 5.0F},
            corrected_eye, prediction::PredictionCorrectionClass::large_snap,
            1.0);
        CHECK(correction.correction_class() ==
            prediction::PredictionCorrectionClass::large_snap);
        CHECK_FALSE(correction.active());
        check_vector(correction.current_residual_offset(), {});
    }

    SECTION("teleport snaps")
    {
        const auto correction = begin_correction({4.0F, 4.0F, 5.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::teleport_snap, 1.0);
        CHECK(correction.correction_class() ==
            prediction::PredictionCorrectionClass::teleport_snap);
        CHECK_FALSE(correction.active());
        check_vector(correction.current_residual_offset(), {});
    }

    SECTION("a hull mismatch preclassified as a large correction snaps")
    {
        const auto standing = make_simulation_state(
            movement::PlayerMovementHull::standing);
        const auto ducked = make_simulation_state(
            movement::PlayerMovementHull::ducked);
        const auto compared =
            prediction::compare_prediction_states(standing, ducked);
        REQUIRE(compared);
        REQUIRE(compared.metrics);
        REQUIRE(compared.metrics->hull_mismatch);

        // Reconciliation converts this metric to large_snap before it reaches
        // the camera-only boundary; the visual API must preserve that class.
        const auto correction = begin_correction({4.0F, 4.0F, 5.0F},
            corrected_eye, prediction::PredictionCorrectionClass::large_snap,
            1.0);
        CHECK(correction.correction_class() ==
            prediction::PredictionCorrectionClass::large_snap);
        CHECK(correction.profile() ==
            prediction::PredictionVisualCorrectionProfile::
                no_smoothing_snap_v1);
        CHECK_FALSE(correction.active());
    }

    SECTION("non-finite sample times fail closed")
    {
        const auto correction = begin_correction({4.0F, 4.0F, 5.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            1.0);
        for (const auto sample_time : {
                 std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity()}) {
            const auto sampled =
                prediction::sample_prediction_visual_correction(correction,
                    corrected_camera, sample_time, nullptr, scratch);
            REQUIRE_FALSE(sampled);
            CHECK_FALSE(sampled.camera);
            CHECK_FALSE(sampled.correction);
            REQUIRE(sampled.error);
            CHECK(sampled.error->code ==
                prediction::PredictionErrorCode::visual_correction_failed);
        }
    }
}

TEST_CASE("Repeated prediction visual corrections evaluate and bound residuals",
    "[prediction][visual-correction][repeated]")
{
    prediction::PredictionVisualCorrectionConfig config;
    config.maximum_offset_magnitude = 4.0;

    const auto first = begin_correction({4.0F, 0.0F, 0.0F}, {},
        prediction::PredictionCorrectionClass::small_visual_correction, 0.0,
        config);
    const auto previous =
        std::optional<prediction::PredictionVisualCorrectionState>{first};

    SECTION("a new correction combines the evaluated current residual")
    {
        const auto combined = begin_correction({1.0F, 0.0F, 0.0F}, {},
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.025, config, previous, 2U, 2U, 3U);
        REQUIRE(combined.active());
        // First residual is 3 at 25 ms; the new physical delta is 1.
        check_vector(combined.initial_position_offset(), {4.0F, 0.0F, 0.0F});
        check_vector(combined.current_residual_offset(), {4.0F, 0.0F, 0.0F});
        CHECK(combined.source_authority_update_ordinal() == 2U);
    }

    SECTION("exact authority does not prematurely clear an active residual")
    {
        const collision::CollisionWorldQuery query{empty_package()};
        collision::CollisionQueryScratch scratch;
        const auto camera_state = make_camera({});
        const auto published = prediction::sample_prediction_visual_correction(
            first, camera_state, 0.0, &query, scratch);
        REQUIRE(published);
        REQUIRE(published.correction);

        const auto continued = begin_correction({}, {},
            prediction::PredictionCorrectionClass::exact, 0.025, config,
            published.correction, 2U, 2U, 3U);
        REQUIRE(continued.active());
        CHECK(continued.correction_class() ==
            prediction::PredictionCorrectionClass::small_visual_correction);
        check_vector(continued.current_residual_offset(),
            {3.0F, 0.0F, 0.0F});
        CHECK(continued.start_monotonic_time_seconds() ==
            Catch::Approx(0.0));
        CHECK(continued.last_sample_monotonic_time_seconds() ==
            Catch::Approx(0.025));
        CHECK(continued.camera_publication_revision() ==
            published.correction->camera_publication_revision());

        const auto midpoint = prediction::sample_prediction_visual_correction(
            continued, camera_state, 0.050, &query, scratch);
        REQUIRE(midpoint);
        REQUIRE(midpoint.correction);
        REQUIRE(midpoint.correction->active());
        REQUIRE(midpoint.camera);
        CHECK(midpoint.camera->revision() >
            published.correction->camera_publication_revision());
        check_vector(midpoint.correction->current_residual_offset(),
            {2.0F, 0.0F, 0.0F});
        const auto completed = prediction::sample_prediction_visual_correction(
            *midpoint.correction, camera_state, 0.100, &query, scratch);
        REQUIRE(completed);
        REQUIRE(completed.correction);
        CHECK(completed.completed);
        CHECK_FALSE(completed.correction->active());
        check_vector(completed.correction->current_residual_offset(), {});
    }

    SECTION("combined offset above the configured threshold snaps")
    {
        const auto snapped = begin_correction({2.0F, 0.0F, 0.0F}, {},
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.025, config, previous, 2U, 2U, 3U);
        CHECK(snapped.correction_class() ==
            prediction::PredictionCorrectionClass::large_snap);
        CHECK_FALSE(snapped.active());
        check_vector(snapped.current_residual_offset(), {});
    }

    SECTION("many overlapping updates never accumulate past the bound")
    {
        std::optional<prediction::PredictionVisualCorrectionState> current;
        current.emplace(prediction::PredictionVisualCorrectionState::inactive());
        auto observed_snap = false;

        for (std::uint64_t update = 1U; update <= 64U; ++update) {
            auto begun = prediction::begin_prediction_visual_correction(current,
                {1.0F, 0.0F, 0.0F}, {},
                prediction::PredictionCorrectionClass::small_visual_correction,
                static_cast<double>(update - 1U) * 0.010, update, update,
                update + 1U, config);
            REQUIRE(begun);
            REQUIRE(begun.correction);
            const auto& correction = *begun.correction;
            const auto& offset = correction.current_residual_offset();
            const auto magnitude = std::sqrt(
                static_cast<double>(offset.x) * offset.x +
                static_cast<double>(offset.y) * offset.y +
                static_cast<double>(offset.z) * offset.z);
            CHECK(magnitude <= config.maximum_offset_magnitude);
            if (correction.correction_class() ==
                prediction::PredictionCorrectionClass::large_snap) {
                observed_snap = true;
                CHECK(magnitude == 0.0);
            }
            current.emplace(std::move(*begun.correction));
        }
        CHECK(observed_snap);
    }
}

TEST_CASE("Prediction visual correction constrains the displayed camera",
    "[prediction][visual-correction][collision]")
{
    SECTION("free visual offset reaches its desired point")
    {
        const collision::CollisionWorldQuery query{empty_package()};
        const auto corrected_eye = assets::AssetVector3{2.0F, 0.0F, 1.0F};
        const auto corrected_camera = make_camera(corrected_eye);
        const auto correction = begin_correction({4.0F, 0.0F, 1.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.0);
        collision::CollisionQueryScratch scratch;
        const auto sampled = prediction::sample_prediction_visual_correction(
            correction, corrected_camera, 0.0, &query, scratch);
        REQUIRE(sampled);
        REQUIRE(sampled.camera);
        REQUIRE(sampled.correction);
        CHECK_FALSE(sampled.constrained);
        CHECK_FALSE(sampled.correction->collision_constrained());
        check_vector(sampled.camera->position(), {4.0F, 0.0F, 1.0F});
        check_vector(corrected_camera.position(), corrected_eye);
        check_camera_endpoint_is_free(query, corrected_eye,
            sampled.camera->position(), scratch);
    }

    SECTION("offset through a wall is trace-clamped to a free point")
    {
        // x >= 0 is empty and x < 0 is solid.
        const collision::CollisionWorldQuery query{half_space_package(
            {1.0F, 0.0F, 0.0F}, 0.0, -1, -2)};
        const auto corrected_eye = assets::AssetVector3{1.0F, 0.0F, 1.0F};
        const auto corrected_camera = make_camera(corrected_eye);
        const auto correction = begin_correction({-3.0F, 0.0F, 1.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.0);
        collision::CollisionQueryScratch scratch;
        const auto sampled = prediction::sample_prediction_visual_correction(
            correction, corrected_camera, 0.0, &query, scratch);
        REQUIRE(sampled);
        REQUIRE(sampled.camera);
        REQUIRE(sampled.correction);
        CHECK(sampled.constrained);
        CHECK(sampled.correction->collision_constrained());
        CHECK(sampled.camera->position().x >= 0.0F);
        CHECK(sampled.camera->position().x < corrected_eye.x);
        check_vector(corrected_camera.position(), corrected_eye);
        check_camera_endpoint_is_free(query, corrected_eye,
            sampled.camera->position(), scratch);
    }

    SECTION("a constrained residual decays without re-expanding")
    {
        const collision::CollisionWorldQuery query{half_space_package(
            {1.0F, 0.0F, 0.0F}, 0.0, -1, -2)};
        const auto corrected_eye = assets::AssetVector3{1.0F, 0.0F, 1.0F};
        const auto corrected_camera = make_camera(corrected_eye);
        auto correction =
            std::optional<prediction::PredictionVisualCorrectionState>{
                begin_correction({-3.0F, 0.0F, 1.0F}, corrected_eye,
                    prediction::PredictionCorrectionClass::
                        small_visual_correction,
                    0.0)};
        collision::CollisionQueryScratch scratch;
        std::optional<float> previous_magnitude;
        for (const auto sample_time : {0.0, 0.025, 0.050, 0.075}) {
            const auto sampled =
                prediction::sample_prediction_visual_correction(*correction,
                    corrected_camera, sample_time, &query, scratch);
            REQUIRE(sampled);
            REQUIRE(sampled.correction);
            const auto residual = sampled.correction->current_residual_offset();
            const auto residual_magnitude = std::sqrt(residual.x * residual.x +
                residual.y * residual.y + residual.z * residual.z);
            if (previous_magnitude) {
                CHECK(residual_magnitude < *previous_magnitude);
            }
            previous_magnitude = residual_magnitude;
            correction.emplace(*sampled.correction);
        }
    }

    SECTION("offset toward a ceiling is trace-clamped to a free point")
    {
        // z <= 2 is empty and z > 2 is solid. The empty side is the front
        // child so the exact trace-clamp point remains collision-valid.
        const collision::CollisionWorldQuery query{half_space_package(
            {0.0F, 0.0F, -1.0F}, -2.0, -1, -2)};
        const auto corrected_eye = assets::AssetVector3{0.0F, 0.0F, 1.0F};
        const auto corrected_camera = make_camera(corrected_eye);
        const auto correction = begin_correction({0.0F, 0.0F, 4.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.0);
        collision::CollisionQueryScratch scratch;
        const auto sampled = prediction::sample_prediction_visual_correction(
            correction, corrected_camera, 0.0, &query, scratch);
        REQUIRE(sampled);
        REQUIRE(sampled.camera);
        REQUIRE(sampled.correction);
        CHECK(sampled.constrained);
        CHECK(sampled.correction->collision_constrained());
        CHECK(sampled.camera->position().z > corrected_eye.z);
        CHECK(sampled.camera->position().z <= 2.0F);
        check_vector(corrected_camera.position(), corrected_eye);
        check_camera_endpoint_is_free(query, corrected_eye,
            sampled.camera->position(), scratch);
    }

    SECTION("a corrected eye that starts in solid fails closed")
    {
        const collision::CollisionWorldQuery query{half_space_package(
            {1.0F, 0.0F, 0.0F}, 0.0, -1, -2)};
        const auto corrected_eye = assets::AssetVector3{-1.0F, 0.0F, 1.0F};
        const auto corrected_camera = make_camera(corrected_eye);
        const auto correction = begin_correction({1.0F, 0.0F, 1.0F},
            corrected_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,
            0.0);
        collision::CollisionQueryScratch scratch;
        const auto sampled = prediction::sample_prediction_visual_correction(
            correction, corrected_camera, 0.0, &query, scratch);
        REQUIRE_FALSE(sampled);
        CHECK_FALSE(sampled.camera);
        REQUIRE(sampled.error);
        CHECK(sampled.error->code == prediction::PredictionErrorCode::
            visual_correction_collision_failed);
        check_vector(corrected_camera.position(), corrected_eye);
    }
}

TEST_CASE("Prediction visual correction is independent of render FPS",
    "[prediction][visual-correction][fps-independence]")
{
    constexpr double target_time = 0.075;
    const collision::CollisionWorldQuery query{empty_package()};
    const auto corrected_simulation_state = make_simulation_state();
    prediction::LocalPredictionStatistics reconciliation_statistics;
    reconciliation_statistics.authoritative_updates = 1U;
    reconciliation_statistics.replay_count = 1U;
    reconciliation_statistics.replayed_command_count = 3U;

    const auto fps_30 = sample_visual_timeline(
        uniform_sample_times(30.0, target_time), query,
        corrected_simulation_state, reconciliation_statistics);
    const auto fps_60 = sample_visual_timeline(
        uniform_sample_times(60.0, target_time), query,
        corrected_simulation_state, reconciliation_statistics);
    const auto fps_144 = sample_visual_timeline(
        uniform_sample_times(144.0, target_time), query,
        corrected_simulation_state, reconciliation_statistics);
    const auto irregular = sample_visual_timeline(
        {0.007, 0.019, 0.041, 0.058, 0.071, target_time}, query,
        corrected_simulation_state, reconciliation_statistics);

    const auto check_equal_timeline = [&](const VisualTimelineOutcome& actual) {
        check_vector(actual.residual, fps_30.residual);
        check_vector(actual.displayed_eye, fps_30.displayed_eye);
        CHECK(actual.simulation_state_signature ==
            fps_30.simulation_state_signature);
        CHECK(actual.authoritative_updates == fps_30.authoritative_updates);
        CHECK(actual.replay_count == fps_30.replay_count);
        CHECK(actual.replayed_command_count ==
            fps_30.replayed_command_count);
    };

    check_equal_timeline(fps_60);
    check_equal_timeline(fps_144);
    check_equal_timeline(irregular);
    check_vector(fps_30.residual, {1.0F, 0.0F, 0.0F});
    check_vector(fps_30.displayed_eye, {21.0F, -5.0F, 28.0F});
    CHECK(fps_30.simulation_state_signature ==
        movement::local_player_movement_state_signature(
            corrected_simulation_state));
    CHECK(fps_30.authoritative_updates == 1U);
    CHECK(fps_30.replay_count == 1U);
    CHECK(fps_30.replayed_command_count == 3U);
}
