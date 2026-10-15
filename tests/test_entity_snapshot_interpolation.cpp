#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
using Catch::Approx;

inline constexpr fixture::Field kFields[]{
    {"neutral_value", 0x0000'0001U, 0U, 8U},
};

[[nodiscard]] goldsrc::EntityBaselineRegistryState make_baselines()
{
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(
        fixture::schema("neutral_t", kFields), 0U);
    REQUIRE(parsed);
    const std::vector<goldsrc::DeltaScalarValue> values{1U};
    auto object =
        goldsrc::DeltaObjectBuilder{
            {}, goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1}
            .build(*parsed.schema, values);
    REQUIRE(object);

    goldsrc::DeltaSchemaRegistryBuilder schema_builder;
    REQUIRE(schema_builder.insert(*parsed.schema));
    auto schemas = std::move(schema_builder).publish();
    goldsrc::EntityBaselineRegistryBuilder builder{
        schemas,
        {},
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    for (std::uint32_t entity = 1U; entity <= 5U; ++entity) {
        REQUIRE(builder.insert(goldsrc::EntityBaselineKey::for_entity(entity),
                               goldsrc::EntitySchemaCategory::ordinary_entity,
                               *object.state));
    }
    auto published = std::move(builder).publish();
    REQUIRE(published);
    return std::move(*published.state);
}

[[nodiscard]] goldsrc::EntitySnapshotState
make_snapshot(const goldsrc::EntityBaselineRegistryState& baselines,
              const std::uint32_t reference,
              const std::span<const std::uint32_t> entities)
{
    std::vector<goldsrc::EntitySnapshotEntityInput> input;
    input.reserve(entities.size());
    for (const auto entity : entities) {
        input.push_back(goldsrc::EntitySnapshotEntityInput::from_baseline(
            entity, goldsrc::EntityBaselineKey::for_entity(entity)));
    }
    auto built =
        goldsrc::EntityFullSnapshotBuilder{
            {},
            goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
            .build(goldsrc::EntitySnapshotReference::synthetic(reference),
                   goldsrc::EntityServerTime::synthetic_raw(
                       static_cast<std::int64_t>(reference) * 137),
                   baselines, input);
    REQUIRE(built);
    return std::move(*built.state);
}

[[nodiscard]] goldsrc::EntitySnapshotHistoryState make_history()
{
    const auto baselines = make_baselines();
    constexpr std::array previous_entities{1U, 2U, 3U, 4U};
    constexpr std::array current_entities{1U, 2U, 3U, 5U};
    auto previous = make_snapshot(baselines, 10U, previous_entities);
    auto current = make_snapshot(baselines, 20U, current_entities);
    goldsrc::EntitySnapshotHistoryBuilder builder{
        {}, goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    REQUIRE(builder.insert(previous));
    REQUIRE(builder.insert(current));
    auto published = builder.publish();
    REQUIRE(published);
    return std::move(*published.state);
}

[[nodiscard]] std::vector<goldsrc::EntitySnapshotExplicitTime>
timeline(const goldsrc::EntitySnapshotHistoryState& history,
         const double previous, const double current)
{
    const auto snapshots = history.snapshots();
    REQUIRE(snapshots.size() == 2U);
    auto first = goldsrc::EntitySnapshotExplicitTime::bind_synthetic_seconds(
        snapshots[0U], previous);
    auto second = goldsrc::EntitySnapshotExplicitTime::bind_synthetic_seconds(
        snapshots[1U], current);
    REQUIRE(first);
    REQUIRE(second);
    std::vector<goldsrc::EntitySnapshotExplicitTime> result;
    result.push_back(std::move(*first));
    result.push_back(std::move(*second));
    return result;
}

[[nodiscard]] goldsrc::EntitySnapshotPairSelection
select_at(const goldsrc::EntitySnapshotHistoryState& history,
          const std::vector<goldsrc::EntitySnapshotExplicitTime>& times,
          const double seconds)
{
    const auto target =
        goldsrc::EntityInterpolationTime::synthetic_seconds(seconds);
    REQUIRE(target);
    const auto selected =
        goldsrc::EntitySnapshotPairSelector{}.select(history, times, *target);
    REQUIRE(selected);
    return *selected.selection;
}

[[nodiscard]] goldsrc::SyntheticEntityInterpolationState
state(const std::uint32_t entity,
      const hlclient::assets::AssetVector3 position = {},
      const hlclient::assets::AssetVector3 angles = {})
{
    goldsrc::SyntheticEntityInterpolationState result;
    result.entity_number = entity;
    result.position = position;
    result.angles_degrees = angles;
    result.model_reference =
        hlclient::entity_visual::EntityVisualModelReference::
            synthetic_model_slot(7U);
    result.discrete.model_reference = 7U;
    result.discrete.sequence_index = 3U;
    return result;
}

[[nodiscard]] std::vector<hlclient::entity_visual::EntityVisualProjectionState>
project_snapshot(const goldsrc::EntitySnapshotState& snapshot,
                 const float control_value,
                 const std::uint32_t first_entity_model_slot = 1U)
{
    namespace entity_visual = hlclient::entity_visual;
    std::vector<entity_visual::SyntheticEntityVisualInput> inputs;
    inputs.reserve(snapshot.entity_count());
    for (const auto& entity : snapshot.entities()) {
        entity_visual::SyntheticEntityVisualInput input;
        input.entity_number = entity.entity_number();
        input.model_reference =
            entity_visual::EntityVisualModelReference::synthetic_model_slot(
                entity.entity_number() == 1U ? first_entity_model_slot
                                             : entity.entity_number());
        input.origin = entity_visual::EntityVisualVector3{
            control_value, -control_value, control_value * 2.0F};
        input.angles_degrees = entity_visual::EntityVisualVector3{
            10.0F * control_value, 20.0F, 30.0F};
        input.sequence_index = 2U;
        input.studio_frame_coordinate = 4.0F + control_value;
        input.body_value = 3U;
        input.skin_family_index = 1U;
        input.controller_values =
            std::array{control_value, 0.2F, 0.3F, 0.4F};
        input.blending_values = std::array{control_value, 0.6F};
        input.mouth_value = control_value;
        input.sprite_frame_index = entity.entity_number() + 10U;
        input.render_mode = entity_visual::EntityVisualRenderMode::alpha_test;
        input.render_amount = control_value;
        input.render_color = entity_visual::EntityVisualRenderColor{
            control_value, 0.7F, 0.8F};
        input.scale = 1.0F + control_value;
        input.interpolation_mode =
            entity_visual::EntityInterpolationMode::interpolate;
        input.animation_start_time_seconds =
            100.0 + static_cast<double>(control_value);
        input.effects_metadata = 0x40U + entity.entity_number();
        inputs.push_back(std::move(input));
    }
    auto provider =
        entity_visual::SyntheticEntityVisualProjectionProvider::create(
            std::move(inputs));
    REQUIRE(provider);
    std::vector<entity_visual::EntityVisualProjectionState> projections;
    projections.reserve(snapshot.entity_count());
    for (const auto& entity : snapshot.entities()) {
        auto projected = provider.provider->project(snapshot, entity);
        REQUIRE(projected);
        projections.push_back(std::move(*projected.state));
    }
    return projections;
}

TEST_CASE(
    "Snapshot pair selection brackets explicit seconds without extrapolation",
    "[goldsrc][entity][interpolation][timeline]")
{
    const auto history = make_history();
    const auto times = timeline(history, 10.0, 11.0);

    CHECK(select_at(history, times, 10.0).status ==
          goldsrc::EntitySnapshotPairSelectionStatus::exact_previous);
    CHECK(select_at(history, times, 11.0).status ==
          goldsrc::EntitySnapshotPairSelectionStatus::exact_current);
    CHECK(select_at(history, times, 10.5).alpha == Approx(0.5));
    const auto held_oldest = select_at(history, times, 9.0);
    CHECK(held_oldest.status ==
          goldsrc::EntitySnapshotPairSelectionStatus::held_oldest);
    CHECK(held_oldest.target_seconds == Approx(9.0));
    CHECK(held_oldest.previous_seconds == Approx(10.0));
    CHECK(held_oldest.current_seconds == Approx(10.0));
    const auto held_newest = select_at(history, times, 12.0);
    CHECK(held_newest.status ==
          goldsrc::EntitySnapshotPairSelectionStatus::held_newest);
    CHECK(held_newest.target_seconds == Approx(12.0));
    CHECK(held_newest.previous_seconds == Approx(11.0));
    CHECK(held_newest.current_seconds == Approx(11.0));

    const std::array previous{
        state(1U), state(2U), state(3U), state(4U)};
    const auto held_frame = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        held_oldest, {10U, previous}, {10U, previous});
    REQUIRE(held_frame);
    CHECK(held_frame.frame->sample_seconds() == Approx(9.0));
    CHECK(held_frame.frame->previous_seconds() == Approx(10.0));
    CHECK(held_frame.frame->current_seconds() == Approx(10.0));
}

TEST_CASE("Snapshot pair selection rejects duplicate order gap and stock time",
          "[goldsrc][entity][interpolation][timeline][errors]")
{
    const auto history = make_history();
    const auto target =
        goldsrc::EntityInterpolationTime::synthetic_seconds(1.0);
    REQUIRE(target);

    const auto duplicate = goldsrc::EntitySnapshotPairSelector{}.select(
        history, timeline(history, 1.0, 1.0), *target);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error->code ==
          goldsrc::EntityInterpolationErrorCode::duplicate_snapshot_time);

    const auto reversed = goldsrc::EntitySnapshotPairSelector{}.select(
        history, timeline(history, 2.0, 1.0), *target);
    REQUIRE_FALSE(reversed);
    CHECK(reversed.error->code ==
          goldsrc::EntityInterpolationErrorCode::invalid_snapshot_time_order);

    const auto gap = goldsrc::EntitySnapshotPairSelector{}.select(
        history, timeline(history, 0.0, 2.0), *target);
    REQUIRE_FALSE(gap);
    CHECK(gap.error->code ==
          goldsrc::EntityInterpolationErrorCode::snapshot_gap_limit_exceeded);

    const auto stock = goldsrc::EntitySnapshotPairSelector{}.select(
        history, timeline(history, 0.0, 1.0),
        goldsrc::EntityInterpolationTime::stock_evidence_pending(),
        goldsrc::EntityInterpolationTimeDomain::
            stock_server_time_evidence_pending);
    REQUIRE_FALSE(stock);
    CHECK(stock.error->code ==
          goldsrc::EntityInterpolationErrorCode::evidence_pending);
}

TEST_CASE("Entity interpolation rejects forged snapshot pair metadata",
          "[goldsrc][entity][interpolation][timeline][errors][forged]")
{
    const auto history = make_history();
    const auto times = timeline(history, 10.0, 11.0);
    const auto valid = select_at(history, times, 10.5);
    const std::array previous{
        state(1U), state(2U), state(3U), state(4U)};
    const std::array current{
        state(1U), state(2U), state(3U), state(5U)};
    const auto rejects = [&](goldsrc::EntitySnapshotPairSelection forged) {
        const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate(
            forged, {10U, previous}, {20U, current});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::EntityInterpolationErrorCode::invalid_configuration);
    };

    SECTION("non-finite pair time")
    {
        auto forged = valid;
        forged.previous_seconds =
            std::numeric_limits<double>::quiet_NaN();
        rejects(forged);
    }
    SECTION("alpha does not describe the exact pair times")
    {
        auto forged = valid;
        forged.alpha = 0.25;
        rejects(forged);
    }
    SECTION("held status contradicts distinct snapshot pointers")
    {
        auto forged = valid;
        forged.status =
            goldsrc::EntitySnapshotPairSelectionStatus::held_only;
        rejects(forged);
    }
    SECTION("unknown status enum")
    {
        auto forged = valid;
        forged.status = static_cast<
            goldsrc::EntitySnapshotPairSelectionStatus>(0xFF);
        rejects(forged);
    }
}

TEST_CASE(
    "Projection adapter owns exact canonical controls and rejects mismatches",
    "[goldsrc][entity][interpolation][projection-adapter]")
{
    const auto history = make_history();
    const auto snapshots = history.snapshots();
    REQUIRE(snapshots.size() == 2U);
    const auto projections = project_snapshot(snapshots[0U], 0.25F);

    const auto adapted = goldsrc::EntityInterpolationProjectionAdapter{}.build(
        snapshots[0U], projections);
    REQUIRE(adapted);
    CHECK(adapted.frame->snapshot_reference() == 10U);
    const auto entities = adapted.frame->entities();
    REQUIRE(entities.size() == snapshots[0U].entity_count());
    CHECK(entities[0U].model_reference.profile() ==
          hlclient::entity_visual::EntityVisualModelReferenceProfile::
              synthetic_type_local_model_slot);
    CHECK(entities[0U].model_reference.value() == 1U);
    CHECK(entities[0U].discrete.model_reference == 1U);
    CHECK(entities[0U].controller_values[0U] == Approx(0.25F));
    CHECK(entities[0U].blending_values[0U] == Approx(0.25F));
    CHECK(entities[0U].mouth_value == Approx(0.25F));
    CHECK(entities[0U].discrete.sprite_frame_category == 11U);
    CHECK(entities[0U].discrete.render_mode ==
          static_cast<std::int32_t>(
              hlclient::entity_visual::EntityVisualRenderMode::alpha_test));
    CHECK(entities[0U].render_amount == Approx(0.25F));
    CHECK(entities[0U].render_color.red == Approx(0.25F));
    REQUIRE(entities[0U].animation_start_time_seconds);
    CHECK(*entities[0U].animation_start_time_seconds == Approx(100.25));
    CHECK(entities[0U].discrete.effects_flags == 0x41U);
    CHECK(adapted.frame->view().entities.data() == entities.data());

    const auto mismatched =
        goldsrc::EntityInterpolationProjectionAdapter{}.build(
            snapshots[1U], projections);
    REQUIRE_FALSE(mismatched);
    CHECK(mismatched.error->code ==
          goldsrc::EntityInterpolationErrorCode::invalid_projection);

    auto limits = goldsrc::EntityInterpolationLimits{};
    limits.maximum_entities = 3U;
    const auto bounded = goldsrc::EntityInterpolationProjectionAdapter{}.build(
        snapshots[0U], projections, limits);
    REQUIRE_FALSE(bounded);
    CHECK(bounded.error->code ==
          goldsrc::EntityInterpolationErrorCode::entity_limit_exceeded);
}

TEST_CASE(
    "Projection adapter and interpolation retain synthetic model slot zero",
    "[goldsrc][entity][interpolation][projection-adapter][slot-zero]")
{
    const auto history = make_history();
    const auto snapshots = history.snapshots();
    REQUIRE(snapshots.size() == 2U);
    const auto previous_projections =
        project_snapshot(snapshots[0U], 0.25F, 0U);
    const auto current_projections =
        project_snapshot(snapshots[1U], 0.75F, 0U);
    const auto previous =
        goldsrc::EntityInterpolationProjectionAdapter{}.build(
            snapshots[0U], previous_projections);
    const auto current =
        goldsrc::EntityInterpolationProjectionAdapter{}.build(
            snapshots[1U], current_projections);
    REQUIRE(previous);
    REQUIRE(current);
    REQUIRE_FALSE(previous.frame->entities().empty());
    CHECK(previous.frame->entities()[0U].model_reference.value() == 0U);
    CHECK(previous.frame->entities()[0U].discrete.model_reference == 0U);

    const auto times = timeline(history, 10.0, 11.0);
    const auto interpolated = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        select_at(history, times, 10.5),
        previous.frame->view(),
        current.frame->view());
    REQUIRE(interpolated);
    REQUIRE_FALSE(interpolated.frame->entities().empty());
    CHECK(interpolated.frame->entities()[0U].model_reference().profile() ==
          hlclient::entity_visual::EntityVisualModelReferenceProfile::
              synthetic_type_local_model_slot);
    CHECK(interpolated.frame->entities()[0U].model_reference().value() == 0U);
    CHECK(interpolated.frame->entities()[0U].discrete().model_reference == 0U);
}

TEST_CASE(
    "Interpolated states preserve typed pose Sprite and render controls",
    "[goldsrc][entity][interpolation][projection-adapter][controls]")
{
    const auto history = make_history();
    const auto snapshots = history.snapshots();
    REQUIRE(snapshots.size() == 2U);
    const auto previous_projections = project_snapshot(snapshots[0U], 0.25F);
    const auto current_projections = project_snapshot(snapshots[1U], 0.75F);
    const auto previous =
        goldsrc::EntityInterpolationProjectionAdapter{}.build(
            snapshots[0U], previous_projections);
    const auto current = goldsrc::EntityInterpolationProjectionAdapter{}.build(
        snapshots[1U], current_projections);
    REQUIRE(previous);
    REQUIRE(current);
    const auto times = timeline(history, 10.0, 11.0);

    const auto halfway = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        select_at(history, times, 10.5),
        previous.frame->view(),
        current.frame->view());
    REQUIRE(halfway);
    REQUIRE_FALSE(halfway.frame->entities().empty());
    const auto& held_controls = halfway.frame->entities().front();
    CHECK(held_controls.model_reference().value() == 1U);
    CHECK(held_controls.controller_values()[0U] == Approx(0.25F));
    CHECK(held_controls.blending_values()[0U] == Approx(0.25F));
    CHECK(held_controls.mouth_value() == Approx(0.25F));
    CHECK(held_controls.sprite_frame_index() == 11U);
    CHECK(held_controls.render_mode() ==
          hlclient::entity_visual::EntityVisualRenderMode::alpha_test);
    CHECK(held_controls.render_amount() == Approx(0.25F));
    CHECK(held_controls.render_color().red == Approx(0.25F));
    REQUIRE(held_controls.animation_start_time_seconds());
    CHECK(*held_controls.animation_start_time_seconds() == Approx(100.25));
    CHECK(held_controls.effects_metadata() == 0x41U);

    const auto exact_current =
        goldsrc::EntitySnapshotInterpolator{}.interpolate(
            select_at(history, times, 11.0),
            previous.frame->view(),
            current.frame->view());
    REQUIRE(exact_current);
    REQUIRE_FALSE(exact_current.frame->entities().empty());
    const auto& current_controls = exact_current.frame->entities().front();
    CHECK(current_controls.controller_values()[0U] == Approx(0.75F));
    CHECK(current_controls.blending_values()[0U] == Approx(0.75F));
    CHECK(current_controls.mouth_value() == Approx(0.75F));
    CHECK(current_controls.render_amount() == Approx(0.75F));
    CHECK(current_controls.render_color().red == Approx(0.75F));
    REQUIRE(current_controls.animation_start_time_seconds());
    CHECK(*current_controls.animation_start_time_seconds() == Approx(100.75));
}

TEST_CASE(
    "Entity interpolation handles transforms angles and lifecycle explicitly",
    "[goldsrc][entity][interpolation][transform][lifecycle]")
{
    const auto history = make_history();
    const auto times = timeline(history, 10.0, 11.0);
    const auto selection = select_at(history, times, 10.5);

    std::array previous{
        state(1U, {-10.0F, 4.0F, -2.0F}, {350.0F, 10.0F, 0.0F}),
        state(2U, {0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}),
        state(3U, {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F}),
        state(4U, {4.0F, 0.0F, 0.0F}),
    };
    std::array current{
        state(1U, {10.0F, -4.0F, 2.0F}, {10.0F, 350.0F, 180.0F}),
        state(2U, {2.0F, 0.0F, 0.0F}, {350.0F, 0.0F, 0.0F}),
        state(3U, {3.0F, 4.0F, 5.0F}, {180.0F, 0.0F, 0.0F}),
        state(5U, {5.0F, 0.0F, 0.0F}),
    };
    previous[0U].scale = {1.0F, 2.0F, 3.0F};
    current[0U].scale = {3.0F, 4.0F, 5.0F};
    previous[0U].studio_frame_coordinate = 2.0F;
    current[0U].studio_frame_coordinate = 4.0F;

    const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        selection, {10U, previous}, {20U, current});
    REQUIRE(result);
    CHECK(result.frame->previous_seconds() == Approx(10.0));
    CHECK(result.frame->current_seconds() == Approx(11.0));
    const auto entities = result.frame->entities();
    REQUIRE(entities.size() == 4U);
    CHECK(entities[0U].entity_number() == 1U);
    CHECK(entities[0U].position().x == Approx(0.0F));
    CHECK(entities[0U].position().y == Approx(0.0F));
    CHECK(entities[0U].angles_degrees().x == Approx(360.0F));
    CHECK(entities[0U].angles_degrees().y == Approx(0.0F));
    CHECK(entities[0U].angles_degrees().z == Approx(-90.0F));
    CHECK(entities[0U].scale().x == Approx(2.0F));
    CHECK(entities[0U].studio_frame_coordinate() == Approx(3.0F));
    CHECK(entities[3U].entity_number() == 4U);
    CHECK(entities[3U].interpolation_class() ==
          goldsrc::InterpolatedEntityClass::previous_only);
    CHECK(result.frame->statistics().interpolated_count == 3U);
    CHECK(result.frame->statistics().added_count == 1U);
    CHECK(result.frame->statistics().removed_count == 1U);
}

TEST_CASE("Entity interpolation steps mode model and sequence transitions",
          "[goldsrc][entity][interpolation][step]")
{
    const auto history = make_history();
    const auto times = timeline(history, 10.0, 11.0);
    const auto selection = select_at(history, times, 10.5);
    std::array previous{state(1U), state(2U), state(3U), state(4U)};
    std::array current{state(1U), state(2U), state(3U), state(5U)};
    current[0U].mode = goldsrc::EntityInterpolationMode::step;
    current[1U].mode = goldsrc::EntityInterpolationMode::teleport;
    current[1U].model_reference =
        hlclient::entity_visual::EntityVisualModelReference::
            synthetic_model_slot(8U);
    current[1U].discrete.model_reference = 8U;
    current[2U].discrete.sequence_index = 4U;

    const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        selection, {10U, previous}, {20U, current});
    REQUIRE(result);
    const auto entities = result.frame->entities();
    REQUIRE(entities.size() == 4U);
    CHECK(entities[0U].interpolation_class() ==
          goldsrc::InterpolatedEntityClass::stepped);
    CHECK(entities[0U].mode() == goldsrc::EntityInterpolationMode::step);
    CHECK(entities[1U].mode() == goldsrc::EntityInterpolationMode::teleport);
    CHECK(entities[1U].discrete().model_reference == 7U);
    CHECK(entities[2U].discrete().sequence_index == 3U);
    CHECK(result.frame->statistics().stepped_count == 3U);

    auto mismatched_reference = current;
    mismatched_reference[0U].model_reference =
        hlclient::entity_visual::EntityVisualModelReference::
            synthetic_model_slot(9U);
    const auto invalid_reference =
        goldsrc::EntitySnapshotInterpolator{}.interpolate(
            selection, {10U, previous}, {20U, mismatched_reference});
    REQUIRE_FALSE(invalid_reference);
    CHECK(invalid_reference.error->code ==
          goldsrc::EntityInterpolationErrorCode::invalid_projection);

    current[0U].position.x = std::numeric_limits<float>::infinity();
    const auto invalid = goldsrc::EntitySnapshotInterpolator{}.interpolate(
        selection, {10U, previous}, {20U, current});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code ==
          goldsrc::EntityInterpolationErrorCode::position_limit_exceeded);
}

hlclient::client::RuntimeClientObservationState runtime_player_sample(double time, double x, std::uint64_t revision)
{
    namespace c = hlclient::client;
    c::RuntimeClientObservationState state;
    state.generation = 1U; state.publication_revision = revision;
    state.server_time_seconds = time;
    const c::RuntimeSubstateMetadata metadata{1U, c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,
        c::RuntimeObservationSource{revision, static_cast<std::size_t>(revision), static_cast<std::uint32_t>(revision), 0U, 8U}};
    state.server_time_metadata = metadata; state.entity_metadata = metadata;
    state.client_metadata.generation = 1U;
    c::RuntimePacketEntityObservation entity;
    entity.entity_number = 2U; entity.ordinary_visual_schema = true; entity.player_movement_schema = true;
    entity.model_index = 7U; entity.sequence = 3U; entity.body = 1U; entity.skin = 0;
    entity.origin = {x, 0.0, 0.0}; entity.angles = {0.0, 350.0, 0.0};
    entity.frame = 64.0; entity.animation_time_seconds = time; entity.frame_rate = 1.0; entity.gait_sequence = 2U;
    entity.controllers.fill(0U); entity.blending.fill(0U); state.packet_entities.push_back(entity);
    state.canonical_state_hash = c::runtime_observation_canonical_hash(state);
    REQUIRE(c::valid_runtime_observation(state));
    return state;
}

TEST_CASE("E10 runtime player interpolation owns bounded samples and preserves committed state",
          "[e10][entity-interpolation][runtime-replay]")
{
    auto old = runtime_player_sample(1.0, 0.0, 1U);
    auto now = runtime_player_sample(2.0, 100.0, 2U);
    now.packet_entities[0].angles.y = 10.0;
    now.packet_entities[0].controllers.fill(200U); now.packet_entities[0].blending.fill(255U);
    now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    const auto committed = now;
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.limits.maximum_snapshot_gap_seconds = 2.0;
    for (const double alpha : {0.0, .25, .5, .75, 1.0}) {
        const auto sampled = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.0 + alpha, options);
        REQUIRE(sampled); const auto& player = sampled.state->packet_entities.front();
        CHECK(sampled.alpha == Approx(alpha)); CHECK(*player.origin.x == Approx(100.0 * alpha));
        CHECK(*player.angles.y == Approx(alpha == 1.0 ? 10.0 : 350.0 + 20.0 * alpha));
        CHECK(*player.controllers[0] == static_cast<std::uint32_t>(std::lround(200.0 * alpha)));
        CHECK(*player.blending[0] == static_cast<std::uint32_t>(std::lround(255.0 * alpha)));
        CHECK(player.animation_time_seconds == (alpha == 1.0 ? 2.0 : 1.0));
        CHECK(sampled.state->server_time_seconds == 2.0);
        CHECK(hlclient::client::valid_runtime_observation(*sampled.state));
    }
    CHECK(now == committed);
    CHECK(*goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 10.0, options).state->packet_entities.front().origin.x == 100.0);
    now.packet_entities.clear(); now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    REQUIRE(goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options));
    CHECK(goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options).state->packet_entities.empty());
}

TEST_CASE("E10 runtime player resets and failures never fabricate interpolation",
          "[e10][entity-interpolation][runtime-replay]")
{
    auto old = runtime_player_sample(1.0, 0.0, 1U);
    auto now = runtime_player_sample(2.0, 100.0, 2U);
    goldsrc::RuntimeEntityInterpolationOptions options; options.limits.maximum_snapshot_gap_seconds = 2.0;
    SECTION("explicit no interpolation") { options.no_interpolation = true; }
    SECTION("teleport") { options.teleport_distance = 50.0; }
    SECTION("model change") { now.packet_entities[0].model_index = 8U; }
    SECTION("new entity") { now.packet_entities[0].entity_number = 3U; }
    SECTION("excessive gap") { options.limits.maximum_snapshot_gap_seconds = .25; }
    SECTION("effect flag") { now.packet_entities[0].effects = 32U; options.no_interpolation_effect_mask = 32U; }
    now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE(result); CHECK(result.reset_count == 1U); CHECK(*result.state->packet_entities[0].origin.x == 100.0);
}

TEST_CASE("E10 runtime interpolation retains missingness current visibility and non-player owners",
          "[e10][entity-interpolation][runtime-replay]")
{
    auto old = runtime_player_sample(1.0, 0.0, 1U); auto now = runtime_player_sample(2.0, 100.0, 2U);
    now.packet_entities[0].origin.y.reset(); now.packet_entities[0].frame_rate.reset(); now.packet_entities[0].effects = 128U;
    now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    goldsrc::RuntimeEntityInterpolationOptions options; options.limits.maximum_snapshot_gap_seconds = 2.0;
    auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE(result); CHECK_FALSE(result.state->packet_entities[0].origin.y); CHECK_FALSE(result.state->packet_entities[0].frame_rate);
    CHECK(result.state->packet_entities[0].effects == 128U);
    now.packet_entities[0].player_movement_schema = false; now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE(result); CHECK(*result.state->packet_entities[0].origin.x == 100.0); CHECK(result.interpolated_count == 0U);
    options.limits.maximum_result_bytes = 1U;
    result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE_FALSE(result); CHECK(result.error->code == goldsrc::EntityInterpolationErrorCode::result_byte_limit_exceeded);
    options.limits.maximum_result_bytes = goldsrc::EntityInterpolationLimits{}.maximum_result_bytes;
    now.server_time_seconds = .5; now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE_FALSE(result); CHECK(result.error->code == goldsrc::EntityInterpolationErrorCode::invalid_snapshot_time_order);
}

TEST_CASE("E16 selected ordinary visuals interpolate without simulating server projectiles",
          "[e16][entity-interpolation][runtime-replay][projectile]")
{
    auto old = runtime_player_sample(1.0, 0.0, 1U);
    auto now = runtime_player_sample(2.0, 100.0, 2U);
    old.packet_entities.front().player_movement_schema = false;
    now.packet_entities.front().player_movement_schema = false;
    old.packet_entities.front().entity_number = now.packet_entities.front().entity_number = 40U;
    old.packet_entities.front().frame = 64.0;
    now.packet_entities.front().frame = 128.0;
    now.packet_entities.front().angles.y = 10.0;
    for (auto* state : {&old, &now}) {
        auto independent = state->packet_entities.front();
        independent.entity_number = 41U;
        independent.origin.y = 20.0;
        state->packet_entities.push_back(independent);
        auto separately_owned_brush = independent;
        separately_owned_brush.entity_number = 80U;
        state->packet_entities.push_back(separately_owned_brush);
        state->canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(*state);
    }
    const auto committed = now;
    const std::array<std::uint32_t, 2U> selected{40U,41U};
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.limits.maximum_snapshot_gap_seconds = 2.0;
    options.ordinary_entities = selected;
    options.no_interpolation_effect_mask = 32U;
    for (const auto alpha : {0.0, .25, .5, .75, 1.0}) {
        const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.0 + alpha, options);
        REQUIRE(result);
        const auto& a = result.state->packet_entities[0];
        const auto& b = result.state->packet_entities[1];
        CHECK(*a.origin.x == Approx(100.0 * alpha));
        CHECK(*b.origin.x == Approx(100.0 * alpha));
        CHECK(*b.origin.y == 20.0);
        CHECK(*a.angles.y == Approx(alpha == 1.0 ? 10.0 : 350.0 + 20.0 * alpha));
        CHECK(*a.frame == Approx(64.0 + 64.0 * alpha));
        CHECK(result.state->packet_entities[2].origin.x == 100.0);
        CHECK(hlclient::client::valid_runtime_observation(*result.state));
    }
    CHECK(now == committed);
    now.packet_entities.erase(now.packet_entities.begin());
    now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    const auto removed = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE(removed);
    REQUIRE(removed.state->packet_entities.size() == 2U);
    CHECK(removed.state->packet_entities.front().entity_number == 41U);
    CHECK(removed.state->packet_entities.front().origin.x == 50.0);
    // A removed server identity is never resurrected by the interpolation bracket.
    CHECK(std::ranges::none_of(removed.state->packet_entities, [](const auto& entity) { return entity.entity_number == 40U; }));
}

TEST_CASE("E16 ordinary visuals keep current discontinuities and missing fields",
          "[e16][entity-interpolation][runtime-replay][projectile]")
{
    auto old = runtime_player_sample(1.0, 0.0, 1U);
    auto now = runtime_player_sample(2.0, 100.0, 2U);
    old.packet_entities.front().player_movement_schema = now.packet_entities.front().player_movement_schema = false;
    const std::array<std::uint32_t,1U> selected{2U};
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.limits.maximum_snapshot_gap_seconds = 2.0;
    options.ordinary_entities = selected;
    options.no_interpolation_effect_mask = 32U;
    SECTION("no interpolation flag") { now.packet_entities.front().effects = 32U; }
    SECTION("model replacement") { now.packet_entities.front().model_index = 8U; }
    SECTION("sequence discontinuity") { now.packet_entities.front().sequence = 4U; }
    SECTION("teleport") { options.teleport_distance = 50.0; }
    SECTION("schema switch") { old.packet_entities.front().player_movement_schema = true; }
    SECTION("explicit disabled interpolation") { options.no_interpolation = true; }
    old.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(old);
    now.canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(now);
    const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE(result);
    CHECK(result.reset_count == 1U);
    CHECK(result.state->packet_entities.front() == now.packet_entities.front());
}

TEST_CASE("E16 ordinary interpolation opt-in list has finite explicit ownership",
          "[e16][entity-interpolation][runtime-replay][projectile]")
{
    auto old = runtime_player_sample(1.0, 0.0, 1U);
    auto now = runtime_player_sample(2.0, 100.0, 2U);
    old.packet_entities.front().player_movement_schema = now.packet_entities.front().player_movement_schema = false;
    now.packet_entities.front().origin.y.reset();
    for (auto* state : {&old,&now}) state->canonical_state_hash = hlclient::client::runtime_observation_canonical_hash(*state);
    const std::array<std::uint32_t,1U> selected{2U};
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.limits.maximum_snapshot_gap_seconds = 2.0;
    options.ordinary_entities = selected;
    const auto result = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
    REQUIRE(result);
    CHECK_FALSE(result.state->packet_entities.front().origin.y);
    CHECK(result.state->packet_entities.front().origin.x == 50.0);
    const std::array<std::uint32_t,2U> duplicates{2U,2U}, unsorted{3U,2U}, invalid{0U,2U};
    for (const auto list : {std::span{duplicates},std::span{unsorted},std::span{invalid}}) {
        options.ordinary_entities = list;
        const auto rejected = goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old, now, 1.5, options);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error->code == goldsrc::EntityInterpolationErrorCode::invalid_configuration);
    }
}

std::shared_ptr<const hlclient::client::RuntimeClientObservationState> buffered_ordinary_sample(
    double time,double x,std::uint64_t record) {
    auto state=runtime_player_sample(time,x,record);
    auto& entity=state.packet_entities.front();
    entity.entity_number=40U;entity.player_movement_schema=false;entity.brush_move_type=10U;
    state.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(state);
    return std::make_shared<const hlclient::client::RuntimeClientObservationState>(std::move(state));
}

TEST_CASE("E20 guided ordinary flight turns across yaw wrap without extrapolation or local rocket physics",
          "[e20][entity-interpolation][projectile]") {
    auto old=*buffered_ordinary_sample(1.0,0.0,1U);
    auto now=*buffered_ordinary_sample(1.1,200.0,2U);
    bool discontinuity=false;
    SECTION("before ignition BOUNCE") {}
    SECTION("guided FLY") {old.packet_entities[0].brush_move_type=now.packet_entities[0].brush_move_type=5U;}
    SECTION("ignition changes movetype") {now.packet_entities[0].brush_move_type=5U;discontinuity=true;}
    SECTION("NOINTERP") {now.packet_entities[0].effects=32U;discontinuity=true;}
    old.packet_entities[0].angles.y=359.0;now.packet_entities[0].angles.y=1.0;
    now.packet_entities[0].origin.y=50.0;
    old.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(old);
    now.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(now);
    const auto committed=now;
    const std::array<std::uint32_t,1> selected{40};goldsrc::RuntimeEntityInterpolationOptions options;
    options.ordinary_entities=selected;options.ordinary_teleport_distance=512.0;
    options.limits.maximum_snapshot_gap_seconds=.25;options.no_interpolation_effect_mask=32;
    const auto mid=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old,now,1.05,options);REQUIRE(mid);
    const auto& entity=mid.state->packet_entities[0];
    if(discontinuity) {CHECK(entity.origin==now.packet_entities[0].origin);CHECK(entity.angles==now.packet_entities[0].angles);}
    else {CHECK(*entity.origin.x==Approx(100.0));CHECK(*entity.origin.y==Approx(25.0));CHECK(*entity.angles.y==Approx(360.0));}
    CHECK(now==committed);
    const auto latest=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old,now,2.0,options);REQUIRE(latest);
    CHECK(latest.state->packet_entities[0].origin==now.packet_entities[0].origin);
}

TEST_CASE("E19 fast kinetic continuity is independent of the player teleport policy",
          "[e19][entity-interpolation][runtime-replay][projectile]") {
    namespace c=hlclient::client;
    auto old=runtime_player_sample(1.0,0.0,1U);
    auto now=runtime_player_sample(1.1,200.0,2U);
    for(auto* state:{&old,&now}) {
        auto kinetic=state->packet_entities.front();
        kinetic.entity_number=40U;kinetic.player_movement_schema=false;kinetic.brush_move_type=5U;
        state->packet_entities.push_back(kinetic);
        state->canonical_state_hash=c::runtime_observation_canonical_hash(*state);
    }
    const auto committed=now;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.ordinary_entities=selected;options.teleport_distance=128.0;
    options.ordinary_teleport_distance=512.0;options.limits.maximum_snapshot_gap_seconds=.25;
    const auto mid=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old,now,1.05,options);
    REQUIRE(mid);CHECK(mid.reset_count==1U);CHECK(mid.interpolated_count==1U);
    CHECK(mid.state->packet_entities[0].origin.x==200.0); // player still resets
    CHECK(*mid.state->packet_entities[1].origin.x==Approx(100.0));
    CHECK(now==committed);
    const auto latest=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old,now,5.0,options);
    REQUIRE(latest);CHECK(latest.state->packet_entities[1].origin.x==200.0); // no extrapolation
    options.ordinary_teleport_distance.reset();
    const auto legacy=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old,now,1.05,options);
    REQUIRE(legacy);CHECK(legacy.reset_count==2U); // explicit opt-in, no silent default
    for(const double invalid:{0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        options.ordinary_teleport_distance=invalid;
        const auto rejected=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(old,now,1.05,options);
        REQUIRE_FALSE(rejected);CHECK(rejected.error->code==goldsrc::EntityInterpolationErrorCode::invalid_configuration);
    }
}

TEST_CASE("E19 fast kinetic history smooths authoritative flight but keeps bounded discontinuities",
          "[e19][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.ordinary_entities=selected;options.teleport_distance=128.0;
    options.ordinary_teleport_distance=512.0;options.limits.maximum_snapshot_gap_seconds=.25;
    options.no_interpolation_effect_mask=32U;
    const auto first=buffered_ordinary_sample(1.0,0.0,1U);owner.observe(first);
    REQUIRE(owner.sample(*first,10.0,options,.1));
    auto now=*buffered_ordinary_sample(1.1,200.0,2U);
    bool discontinuity=true;
    SECTION("fast continuous") {discontinuity=false;}
    SECTION("finite displacement guard") {now.packet_entities.front().origin.x=600.0;}
    SECTION("model reuse") {now.packet_entities.front().model_index=8U;}
    SECTION("NOINTERP") {now.packet_entities.front().effects=32U;}
    SECTION("snapshot gap") {now.server_time_seconds=1.3;}
    now.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(now);
    owner.observe(std::make_shared<const hlclient::client::RuntimeClientObservationState>(now));
    const auto start=owner.sample(now,10.1,options,.1);REQUIRE(start);
    const auto mid=owner.sample(now,10.15,options,.1);REQUIRE(mid);
    if(discontinuity) CHECK(mid.state->packet_entities.front()==now.packet_entities.front());
    else {
        CHECK(start.state->packet_entities.front().origin.x==0.0);
        CHECK(*mid.state->packet_entities.front().origin.x==Approx(100.0));
        const auto repeated=owner.sample(now,10.15,options,.1);REQUIRE(repeated);
        CHECK(repeated.state->packet_entities.front().origin.x==mid.state->packet_entities.front().origin.x);
        const auto held=owner.sample(now,10.24,options,.1);REQUIRE(held);
        CHECK(held.state->packet_entities.front().origin.x==200.0);
    }
}

TEST_CASE("E16 render-only ordinary history is continuous across irregular arrivals and short flight",
          "[e16][grenade-smoothing][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;options.teleport_distance=128.0;
    const std::array arrivals{0.0,.025,.045,.055,.085};
    std::size_t next{};std::shared_ptr<const hlclient::client::RuntimeClientObservationState> current;
    double previous_x{};
    // Independent authoritative linear samples, not a locally integrated path.
    // A third early-arriving sample needs an older bracket, not a pair-clock reset.
    for(unsigned frame=0U;frame<=20U;++frame) {
        const double application=static_cast<double>(frame)*.005;
        while(next<arrivals.size() && arrivals[next]<=application+1e-9) {
            const double time=1.0+static_cast<double>(next)*.02;
            current=buffered_ordinary_sample(time,static_cast<double>(next)*16.0,next+1U);
            owner.observe(current);++next;
        }
        REQUIRE(current);
        const auto immutable=*current;
        const auto result=owner.sample(*current,100.0+application,options,.1);REQUIRE(result);
        const auto x=*result.state->packet_entities.front().origin.x;
        const double expected=std::min(*current->packet_entities.front().origin.x,
            std::max(0.0,application-.02)*800.0);
        CHECK(x==Approx(expected).margin(.00001));
        CHECK(x>=previous_x);CHECK(x-previous_x<=4.00001);
        if(frame==8U) CHECK(x==Approx(16.0)); // An80ms flight is moving after one cadence, not held100ms.
        const auto repeated=owner.sample(*current,100.0+application,options,.1);REQUIRE(repeated);
        CHECK(*repeated.state->packet_entities.front().origin.x==x);
        CHECK(*current==immutable);previous_x=x;
    }
    REQUIRE(owner.cadence_delay_seconds());CHECK(*owner.cadence_delay_seconds()==Approx(.02));
    auto removed=*buffered_ordinary_sample(1.12,96.0,6U);removed.packet_entities.clear();
    removed.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(removed);
    owner.observe(std::make_shared<const hlclient::client::RuntimeClientObservationState>(removed));
    const auto absent=owner.sample(removed,100.105,options,.1);REQUIRE(absent);
    CHECK(absent.state->packet_entities.empty());CHECK(owner.lifetime_count()==0U);
    owner.reset();CHECK(owner.history_size()==0U);CHECK_FALSE(owner.cadence_delay_seconds());
}

TEST_CASE("E16 ordinary spawn history underflow cannot rewind or resurrect a reused server slot",
          "[e16][grenade-smoothing][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;options.teleport_distance=128.0;
    auto first=buffered_ordinary_sample(1.0,0.0,1U);owner.observe(first);
    REQUIRE(owner.sample(*first,10.0,options,.1));
    auto second=buffered_ordinary_sample(1.02,16.0,2U);owner.observe(second);
    const auto held=owner.sample(*second,10.01,options,.1);REQUIRE(held);
    CHECK(held.state->packet_entities.front().origin.x==0.0);
    const auto moving=owner.sample(*second,10.03,options,.1);REQUIRE(moving);
    CHECK(*moving.state->packet_entities.front().origin.x==Approx(8.0));
    const auto backwards=owner.sample(*second,10.025,options,.1);REQUIRE(backwards);
    CHECK(backwards.state->packet_entities.front().origin.x==moving.state->packet_entities.front().origin.x);
    auto absent=*buffered_ordinary_sample(1.04,32.0,3U);absent.packet_entities.clear();
    absent.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(absent);
    owner.observe(std::make_shared<const hlclient::client::RuntimeClientObservationState>(absent));
    // Both records can arrive before the next render; absence still breaks lifetime.
    auto reused=buffered_ordinary_sample(1.06,48.0,4U);owner.observe(reused);
    const auto fresh=owner.sample(*reused,10.04,options,.1);REQUIRE(fresh);
    CHECK(fresh.state->packet_entities.front().origin.x==48.0);CHECK(fresh.reset_count==1U);
    const auto no_rewind=owner.sample(*reused,10.05,options,.1);REQUIRE(no_rewind);
    CHECK(no_rewind.state->packet_entities.front().origin.x==48.0);
}

TEST_CASE("E16 ordinary buffered lifetime discontinuities current visibility and frame guards remain explicit",
          "[e16][grenade-smoothing][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;options.teleport_distance=128.0;
    options.no_interpolation_effect_mask=32U;
    auto first=buffered_ordinary_sample(1.0,0.0,1U);owner.observe(first);
    REQUIRE(owner.sample(*first,10.0,options,.1));
    auto current=*buffered_ordinary_sample(1.02,16.0,2U);
    SECTION("model") {current.packet_entities.front().model_index=8U;}
    SECTION("movetype") {current.packet_entities.front().brush_move_type=5U;}
    SECTION("sequence") {current.packet_entities.front().sequence=4U;}
    SECTION("NOINTERP") {current.packet_entities.front().effects=32U;}
    SECTION("teleport") {current.packet_entities.front().origin.x=500.0;}
    SECTION("missing component") {current.packet_entities.front().origin.y.reset();}
    SECTION("explicit boundary") {options.discontinuous_entities=selected;}
    SECTION("disabled") {options.no_interpolation=true;}
    current.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(current);
    owner.observe(std::make_shared<const hlclient::client::RuntimeClientObservationState>(current));
    const auto result=owner.sample(current,10.025,options,.1);REQUIRE(result);
    CHECK(result.state->packet_entities.front()==current.packet_entities.front());CHECK(result.reset_count==1U);
}

TEST_CASE("E16 ordinary buffered clock and ownership bounds never extrapolate or change player state",
          "[e16][grenade-smoothing][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;
    for(unsigned i=0U;i<40U;++i) {
        const auto current=buffered_ordinary_sample(1.0+i*.02,i*16.0,i+1U);owner.observe(current);
        const auto result=owner.sample(*current,10.0+i*.02,options,.1);REQUIRE(result);
        CHECK(*result.state->packet_entities.front().origin.x<=*current->packet_entities.front().origin.x);
        CHECK(owner.history_size()<=16U);CHECK(owner.lifetime_count()==1U);
    }
    const auto same_time=buffered_ordinary_sample(1.78,625.0,41U);owner.observe(same_time);
    CHECK(owner.history_size()==1U);CHECK(owner.lifetime_count()==0U);
    auto result=owner.sample(*same_time,10.8,options,.1);REQUIRE(result);
    CHECK(result.state->packet_entities.front().origin.x==625.0);
    const auto back_time=buffered_ordinary_sample(.5,20.0,42U);owner.observe(back_time);
    CHECK(owner.history_size()==1U);REQUIRE(owner.sample(*back_time,10.82,options,.1));
    auto stalled=buffered_ordinary_sample(.52,36.0,43U);owner.observe(stalled);
    result=owner.sample(*stalled,12.0,options,.1);REQUIRE(result);
    CHECK(result.state->packet_entities.front().origin.x==36.0);
    // An independently already-presented player is not processed by this owner.
    auto player=runtime_player_sample(.54,12.25,44U);owner.observe(std::make_shared<const decltype(player)>(player));
    result=owner.sample(player,12.02,options,.1);REQUIRE(result);CHECK(*result.state==player);
    CHECK(owner.lifetime_count()==0U);
    const auto invalid=owner.sample(player,std::numeric_limits<double>::quiet_NaN(),options,.1);
    CHECK_FALSE(invalid);
}

TEST_CASE("E16 ordinary current hidden fields and wrapped frames do not resurrect historical visibility",
          "[e16][grenade-smoothing][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;
    auto first=*buffered_ordinary_sample(1.0,0.0,1U);first.packet_entities.front().frame=250.0;
    first.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(first);
    owner.observe(std::make_shared<const decltype(first)>(first));REQUIRE(owner.sample(first,10.0,options,.1));
    auto now=*buffered_ordinary_sample(1.02,16.0,2U);
    now.packet_entities.front().effects=128U;now.packet_entities.front().render_amount=0U;
    now.packet_entities.front().frame=5.0;
    now.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(now);
    owner.observe(std::make_shared<const decltype(now)>(now));
    const auto result=owner.sample(now,10.03,options,.1);REQUIRE(result);
    CHECK(*result.state->packet_entities.front().origin.x==Approx(8.0));
    CHECK(result.state->packet_entities.front().effects==128U);
    CHECK(result.state->packet_entities.front().render_amount==0U);
    CHECK(result.state->packet_entities.front().frame==5.0);
}

TEST_CASE("E16 valid untimed entity removal breaks ordinary continuity without accepting stale input",
          "[e16][grenade-smoothing][entity-interpolation][runtime-replay][projectile]") {
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;
    const auto first=buffered_ordinary_sample(1.0,0.0,1U);owner.observe(first);
    REQUIRE(owner.sample(*first,10.0,options,.1));
    const auto second=buffered_ordinary_sample(1.02,16.0,2U);owner.observe(second);
    const auto moving=owner.sample(*second,10.03,options,.1);REQUIRE(moving);
    CHECK(*moving.state->packet_entities.front().origin.x==Approx(8.0));
    const auto untimed=[](auto state) {
        state.server_time_seconds.reset();
        state.server_time_metadata={};state.server_time_metadata.generation=state.generation;
        state.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(state);
        return state;
    };
    auto older=untimed(*first);REQUIRE(hlclient::client::valid_runtime_observation(older));
    owner.observe(std::make_shared<const decltype(older)>(older));
    auto duplicate=untimed(*second);REQUIRE(hlclient::client::valid_runtime_observation(duplicate));
    owner.observe(std::make_shared<const decltype(duplicate)>(duplicate));
    auto invalid=untimed(*buffered_ordinary_sample(1.04,32.0,3U));invalid.generation=0U;
    REQUIRE_FALSE(hlclient::client::valid_runtime_observation(invalid));
    owner.observe(std::make_shared<const decltype(invalid)>(invalid));
    CHECK(owner.history_size()==2U);CHECK(owner.lifetime_count()==1U);
    const auto held=owner.sample(*second,10.03,options,.1);REQUIRE(held);
    CHECK(held.state->packet_entities.front().origin.x==moving.state->packet_entities.front().origin.x);
    auto removed=untimed(*buffered_ordinary_sample(1.04,32.0,3U));removed.packet_entities.clear();
    removed.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(removed);
    REQUIRE(hlclient::client::valid_runtime_observation(removed));
    owner.observe(std::make_shared<const decltype(removed)>(removed));
    CHECK(owner.history_size()==0U);CHECK(owner.lifetime_count()==0U);
    CHECK_FALSE(owner.cadence_delay_seconds());
    const auto reused=buffered_ordinary_sample(1.06,48.0,4U);owner.observe(reused);
    const auto fresh=owner.sample(*reused,10.04,options,.1);REQUIRE(fresh);
    CHECK(fresh.state->packet_entities.front().origin.x==48.0);CHECK(fresh.reset_count==1U);
    const auto repeated=owner.sample(*reused,10.05,options,.1);REQUIRE(repeated);
    CHECK(repeated.state->packet_entities.front().origin.x==48.0);
}

TEST_CASE("E16 late ordinary birth cannot inherit a session clock lag as a frozen first frame",
          "[e16][grenade-smoothing][grenade-first-frame][entity-interpolation][runtime-replay][projectile]") {
    unsigned warm_steps=48U;
    double application_step=.015, gap_limit=.25;
    SECTION("current Half-Life250ms recovery bound") {}
    SECTION("generic1s recovery bound") {warm_steps=96U;application_step=.01;gap_limit=1.0;}
    goldsrc::RuntimeOrdinarySnapshotPresentation owner;
    const std::array<std::uint32_t,1U> selected{40U};
    goldsrc::RuntimeEntityInterpolationOptions options;options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=gap_limit;options.teleport_distance=128.0;
    // Valid moving server clocks can acquire positive phase below the recovery
    // threshold while no projectile exists. This is not a gap or teleport.
    for(unsigned i=0U;i<warm_steps;++i) {
        auto empty=*buffered_ordinary_sample(1.0+i*.02,0.0,i+1U);empty.packet_entities.clear();
        empty.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(empty);
        owner.observe(std::make_shared<const decltype(empty)>(empty));
        const auto sampled=owner.sample(empty,100.0+i*application_step,options,.1);REQUIRE(sampled);
        REQUIRE(sampled.state->packet_entities.empty());
    }
    const double birth_time=1.0+warm_steps*.02,birth_application=100.0+warm_steps*application_step;
    const double phase=warm_steps*(.02-application_step);CAPTURE(phase,gap_limit);
    REQUIRE(phase<gap_limit);REQUIRE(owner.cadence_delay_seconds());
    CHECK(*owner.cadence_delay_seconds()==Approx(.02));
    auto current=buffered_ordinary_sample(birth_time,0.0,warm_steps+1U);owner.observe(current);
    const auto born=owner.sample(*current,birth_application,options,.1);REQUIRE(born);
    CHECK(born.state->packet_entities.front().origin.x==0.0);CHECK(born.reset_count==1U);
    current=buffered_ordinary_sample(birth_time+.02,16.0,warm_steps+2U);owner.observe(current);
    const auto held=owner.sample(*current,birth_application+.02,options,.1);REQUIRE(held);
    CHECK(*held.state->packet_entities.front().origin.x==Approx(0.0).margin(.00001));
    const auto held_snapshot=*held.state;
    const auto moving=owner.sample(*current,birth_application+.03,options,.1);REQUIRE(moving);
    // Existing implementation instead holdsx0 for phase+20ms (260/980ms).
    // A newborn must move after its own one-cadence buffer, not that whole phase.
    CHECK(*moving.state->packet_entities.front().origin.x==Approx(8.0).margin(.00001));
    const auto repeated=owner.sample(*current,birth_application+.03,options,.1);REQUIRE(repeated);
    CHECK(repeated.state->packet_entities.front().origin.x==moving.state->packet_entities.front().origin.x);
    const auto backwards=owner.sample(*current,birth_application+.029,options,.1);REQUIRE(backwards);
    CHECK(backwards.state->packet_entities.front().origin.x==moving.state->packet_entities.front().origin.x);
    const auto original_current=current;const auto immutable=*original_current;
    for(unsigned i=2U;i<=3U;++i) {
        current=buffered_ordinary_sample(birth_time+i*.02,i*16.0,warm_steps+i+1U);owner.observe(current);
        const auto sampled=owner.sample(*current,birth_application+i*.02,options,.1);REQUIRE(sampled);
        CHECK(*sampled.state->packet_entities.front().origin.x==Approx((i-1U)*16.0).margin(.00001));
        CHECK(*sampled.state->packet_entities.front().origin.x<=*current->packet_entities.front().origin.x);
    }
    CHECK(*held.state==held_snapshot); // Retained result remains owning across later observations.
    CHECK(*original_current==immutable);
    auto removed=*buffered_ordinary_sample(birth_time+.08,64.0,warm_steps+5U);removed.packet_entities.clear();
    removed.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(removed);
    owner.observe(std::make_shared<const decltype(removed)>(removed));
    const auto absent=owner.sample(removed,birth_application+.08,options,.1);REQUIRE(absent);
    CHECK(absent.state->packet_entities.empty());CHECK(owner.lifetime_count()==0U);
    CHECK(owner.history_size()<=16U);
    owner.reset();CHECK(owner.history_size()==0U);CHECK_FALSE(owner.cadence_delay_seconds());
}

TEST_CASE("E32 presentation sampling keeps immutable mixed populations during repeated render frames",
          "[e32][fps-presentation-work][entity-interpolation][runtime-replay]") {
    namespace c=hlclient::client;
    std::vector<std::uint32_t> players,ordinary;
    for(std::uint32_t id=1;id<=16;++id)players.push_back(id);
    for(std::uint32_t id=17;id<=64;++id)ordinary.push_back(id);
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.limits.maximum_snapshot_gap_seconds=.25;
    options.teleport_distance=128.0;options.ordinary_teleport_distance=512.0;
    options.ordinary_entities=ordinary;
    goldsrc::RuntimePlayerSnapshotPresentation player_owner;
    goldsrc::RuntimeOrdinarySnapshotPresentation ordinary_owner;
    std::shared_ptr<const c::RuntimeClientObservationState> latest;
    std::vector<std::shared_ptr<const c::RuntimeClientObservationState>> sources;
    std::vector<c::RuntimeClientObservationState> originals;
    for(std::uint64_t record=1;record<=16;++record) {
        auto state=runtime_player_sample(1.0+.02*static_cast<double>(record-1),0.0,record);
        const auto prototype=state.packet_entities.front();state.packet_entities.clear();
        for(std::uint32_t id=1;id<=64;++id) {
            auto entity=prototype;entity.entity_number=id;
            entity.origin={static_cast<double>(record*2+id),static_cast<double>(id),8.0};
            entity.player_movement_schema=id<=16;
            if(id>16)entity.brush_move_type=10U;
            state.packet_entities.push_back(entity);
        }
        state.canonical_state_hash=c::runtime_observation_canonical_hash(state);
        REQUIRE(c::valid_runtime_observation(state));originals.push_back(state);
        latest=std::make_shared<const c::RuntimeClientObservationState>(std::move(state));
        sources.push_back(latest);player_owner.observe(latest);ordinary_owner.observe(latest);
    }
    // Timings are observational only; correctness never depends on machine speed.
    // Same input/history, full player and projectile owner composition each frame.
    constexpr std::size_t frames=1200;
    double sampling_us{};
    std::shared_ptr<const c::RuntimeClientObservationState> retained;
    std::optional<c::RuntimeClientObservationState> retained_value;
    for(std::size_t frame=0;frame<frames;++frame) {
        const double now=100.0+.2*static_cast<double>(frame)/frames;
        const auto started=std::chrono::steady_clock::now();
        const auto player=player_owner.sample(*latest,now,options,.1,players);
        REQUIRE(player);
        const auto result=ordinary_owner.sample(*player.presentation.state,now,options,.1);
        sampling_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
        REQUIRE(result);REQUIRE(result.state->packet_entities.size()==64);
        CHECK(c::valid_runtime_observation(*result.state));
        for(std::size_t i=0;i<players.size();++i)
            CHECK(result.state->packet_entities[i]==player.presentation.state->packet_entities[i]);
        CHECK(*result.state->packet_entities.back().origin.x<=*latest->packet_entities.back().origin.x);
        if(frame==0) {retained=result.state;retained_value=*result.state;}
    }
    REQUIRE(retained);REQUIRE(retained_value);CHECK(*retained==*retained_value);
    for(std::size_t i=0;i<sources.size();++i)CHECK(*sources[i]==originals[i]);
    std::cout<<"[fps-presentation-work] frames="<<frames<<" players=16 entities=64 mean_sample_us="
        <<sampling_us/frames<<" source=project_owned device=false timing_assertion=false\n";
}

TEST_CASE("E32 buffered sampling retains the shared input rejection contract without disposable frames",
          "[e32][fps-presentation-validation][entity-interpolation][runtime-replay]") {
    namespace c=hlclient::client;
    auto valid=runtime_player_sample(1.0,0.0,1);
    auto ordinary=valid.packet_entities.front();ordinary.entity_number=40;
    ordinary.player_movement_schema=false;ordinary.brush_move_type=10;
    valid.packet_entities.push_back(ordinary);
    valid.canonical_state_hash=c::runtime_observation_canonical_hash(valid);
    const auto committed=std::make_shared<const c::RuntimeClientObservationState>(valid);
    const std::array<std::uint32_t,1> players{2},ordinaries{40};
    goldsrc::RuntimePlayerSnapshotPresentation player_owner;
    goldsrc::RuntimeOrdinarySnapshotPresentation ordinary_owner;
    player_owner.observe(committed);ordinary_owner.observe(committed);
    auto invalid=valid;
    goldsrc::RuntimeEntityInterpolationOptions options;
    options.ordinary_entities=ordinaries;
    std::vector<goldsrc::RuntimePlayerAnimationEffectException> exceptions;
    bool update_hash=true;
    SECTION("canonical checksum") {invalid.canonical_state_hash^=1;update_hash=false;}
    SECTION("nonfinite un-hashed Studio frame") {invalid.packet_entities.front().frame=std::numeric_limits<double>::quiet_NaN();}
    SECTION("invalid controller byte") {invalid.packet_entities.front().controllers[0]=256;}
    SECTION("invalid source identity") {invalid.entity_metadata.source->record_identity=0;}
    SECTION("duplicate entity") {invalid.packet_entities.back().entity_number=2;}
    SECTION("missing server clock") {invalid.server_time_seconds.reset();invalid.server_time_metadata={};invalid.server_time_metadata.generation=1;}
    SECTION("maximum entities") {options.limits.maximum_entities=1;}
    SECTION("result allocation bound") {options.limits.maximum_result_bytes=sizeof(valid);}
    SECTION("position bound") {options.limits.maximum_position_magnitude=1;invalid.packet_entities.front().origin.x=2.0;}
    SECTION("nonfinite teleport bound") {options.teleport_distance=std::numeric_limits<double>::infinity();}
    SECTION("invalid ordinary continuity bound") {options.ordinary_teleport_distance=0.0;}
    SECTION("unapproved effect exception") {exceptions={{2,1,32}};options.animation_only_effect_exceptions=exceptions;}
    SECTION("duplicate effect exception") {exceptions={{2,1,32},{2,1,32}};options.no_interpolation_effect_mask=32;options.animation_only_effect_exceptions=exceptions;}
    if(update_hash)invalid.canonical_state_hash=c::runtime_observation_canonical_hash(invalid);
    const auto oracle=goldsrc::EntitySnapshotInterpolator{}.interpolate_runtime(invalid,invalid,
        invalid.server_time_seconds.value_or(0.0),options);
    REQUIRE_FALSE(oracle);REQUIRE(oracle.error);
    const auto player=player_owner.sample(invalid,100.0,options,.1,players);
    const auto projectile=ordinary_owner.sample(invalid,100.0,options,.1);
    REQUIRE_FALSE(player);REQUIRE(player.presentation.error);
    REQUIRE_FALSE(projectile);REQUIRE(projectile.error);
    CHECK(player.presentation.error->code==oracle.error->code);
    CHECK(projectile.error->code==oracle.error->code);
    CHECK(*committed==valid);
    // Failed validation cannot advance clocks, consume history or alter sources.
    goldsrc::RuntimeEntityInterpolationOptions recovered_options;
    recovered_options.ordinary_entities=ordinaries;
    const auto recovered_player=player_owner.sample(valid,100.0,recovered_options,.1,players);
    const auto recovered_projectile=ordinary_owner.sample(valid,100.0,recovered_options,.1);
    REQUIRE(recovered_player);REQUIRE(recovered_projectile);
    CHECK(*recovered_player.presentation.state==valid);
    CHECK(*recovered_projectile.state==valid);
    CHECK(player_owner.history_size()==1);CHECK(ordinary_owner.history_size()==1);
}

static_assert(!std::is_copy_assignable_v<goldsrc::InterpolatedEntityState>);
static_assert(!std::is_copy_assignable_v<goldsrc::InterpolatedEntityFrame>);
static_assert(!std::is_copy_assignable_v<
              goldsrc::EntityInterpolationProjectionFrameState>);

} // namespace
