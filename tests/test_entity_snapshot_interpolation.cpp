#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
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

static_assert(!std::is_copy_assignable_v<goldsrc::InterpolatedEntityState>);
static_assert(!std::is_copy_assignable_v<goldsrc::InterpolatedEntityFrame>);
static_assert(!std::is_copy_assignable_v<
              goldsrc::EntityInterpolationProjectionFrameState>);

} // namespace
