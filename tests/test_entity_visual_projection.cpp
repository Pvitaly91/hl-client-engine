#include <hlclient/entity_visual/entity_visual_projection.hpp>

#include "entity_visual/entity_visual_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

namespace {

namespace entity = hlclient::entity_visual;
namespace fixture = hlclient::tests::entity_visual_fixture;

TEST_CASE("Typed synthetic entity visual projection is complete and immutable",
          "[entity-visual][projection][synthetic]")
{
    const std::vector<std::uint32_t> numbers{1U};
    const auto snapshot = fixture::synthetic_snapshot(numbers, 17U);
    entity::SyntheticEntityVisualInput input;
    input.entity_number = 1U;
    input.model_reference =
        entity::EntityVisualModelReference::synthetic_model_slot(7U);
    input.origin = entity::EntityVisualVector3{1.0F, -2.0F, 3.0F};
    input.angles_degrees = entity::EntityVisualVector3{10.0F, 350.0F, 20.0F};
    input.sequence_index = 3U;
    input.studio_frame_coordinate = 2.5F;
    input.body_value = 4U;
    input.skin_family_index = 2U;
    input.controller_values = std::array{0.0F, 0.25F, 0.5F, 1.0F};
    input.blending_values = std::array{0.25F, 0.75F};
    input.mouth_value = 0.5F;
    input.sprite_frame_index = 9U;
    input.render_mode = entity::EntityVisualRenderMode::alpha_test;
    input.render_amount = 0.75F;
    input.render_color = entity::EntityVisualRenderColor{0.1F, 0.2F, 0.3F};
    input.scale = 2.0F;
    input.interpolation_mode = entity::EntityInterpolationMode::teleport;
    input.animation_start_time_seconds = 12.5;
    input.effects_metadata = 0x1234U;

    auto created = entity::SyntheticEntityVisualProjectionProvider::create(
        {input});
    INFO(created.context);
    REQUIRE(created);
    const auto result = created.provider->project(
        snapshot, snapshot.entities().front());
    INFO(result.context);
    REQUIRE(result);
    REQUIRE(result.state);
    const auto& state = *result.state;
    CHECK(state.entity_number() == 1U);
    CHECK(state.model_reference().value() == 7U);
    CHECK(state.transform().origin == *input.origin);
    CHECK(state.transform().angles_degrees == *input.angles_degrees);
    CHECK(state.transform().scale == 2.0F);
    CHECK(state.studio_controls().sequence_index == 3U);
    CHECK(state.studio_controls().frame_coordinate == 2.5F);
    CHECK(state.studio_controls().controller_values[3U] == 1.0F);
    CHECK(state.sprite_controls().frame_index == 9U);
    CHECK(state.render_controls().mode ==
          entity::EntityVisualRenderMode::alpha_test);
    CHECK(state.interpolation_mode() ==
          entity::EntityInterpolationMode::teleport);
    CHECK(state.source_snapshot_reference().value() == 17U);
    CHECK(state.compatibility_profile() ==
          entity::EntityVisualProjectionCompatibilityProfile::
              synthetic_entity_visual_v1);
}

TEST_CASE("Projection defaults and missing records are explicit",
          "[entity-visual][projection][missing]")
{
    const std::vector<std::uint32_t> numbers{1U, 2U};
    const auto snapshot = fixture::synthetic_snapshot(numbers);
    entity::SyntheticEntityVisualInput input;
    input.entity_number = 1U;
    input.model_reference =
        entity::EntityVisualModelReference::synthetic_model_slot(1U);
    auto created = entity::SyntheticEntityVisualProjectionProvider::create(
        {input});
    REQUIRE(created);

    const auto projected = created.provider->project(
        snapshot, snapshot.entities()[0U]);
    REQUIRE(projected);
    REQUIRE(projected.state);
    CHECK(projected.state->transform().scale == 1.0F);
    CHECK(projected.state->studio_controls().sequence_index == 0U);
    CHECK(projected.state->render_controls().amount == 1.0F);

    const auto missing = created.provider->project(
        snapshot, snapshot.entities()[1U]);
    CHECK_FALSE(missing);
    CHECK(missing.status ==
          entity::EntityVisualProjectionStatus::missing_projection);
}

TEST_CASE("Stock visual projection never infers similarly named delta fields",
          "[entity-visual][projection][stock][evidence]")
{
    const std::vector<std::uint32_t> numbers{1U};
    const auto snapshot = fixture::synthetic_snapshot(numbers);
    // The fixture deliberately names its sole DeltaObjectState field
    // "modelindex". The stock provider does not inspect it.
    entity::EvidencePendingStockEntityVisualProjectionProvider provider;
    const auto result = provider.project(snapshot, snapshot.entities().front());
    CHECK_FALSE(result);
    CHECK(result.status == entity::EntityVisualProjectionStatus::
                               visual_projection_evidence_pending);
    CHECK_FALSE(result.state);
}

TEST_CASE("Synthetic visual input validation is bounded and finite",
          "[entity-visual][projection][validation]")
{
    entity::SyntheticEntityVisualInput input;
    input.entity_number = 1U;
    input.model_reference =
        entity::EntityVisualModelReference::synthetic_model_slot(1U);

    SECTION("model reference profile")
    {
        input.model_reference = entity::EntityVisualModelReference::
            stock_modelindex_evidence_pending(1U);
        const auto result =
            entity::SyntheticEntityVisualProjectionProvider::create({input});
        CHECK_FALSE(result);
        CHECK(result.error_status ==
              entity::EntityVisualProjectionStatus::invalid_model_reference);
    }
    SECTION("NaN and infinity")
    {
        input.origin = entity::EntityVisualVector3{
            std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        auto nan = entity::SyntheticEntityVisualProjectionProvider::create(
            {input});
        CHECK_FALSE(nan);
        CHECK(nan.error_status ==
              entity::EntityVisualProjectionStatus::non_finite_value);
        input.origin.reset();
        input.angles_degrees = entity::EntityVisualVector3{
            0.0F, std::numeric_limits<float>::infinity(), 0.0F};
        auto infinity =
            entity::SyntheticEntityVisualProjectionProvider::create({input});
        CHECK_FALSE(infinity);
        CHECK(infinity.error_status ==
              entity::EntityVisualProjectionStatus::non_finite_value);
    }
    SECTION("sequence body skin controller blend and scale ranges")
    {
        input.sequence_index =
            entity::kDefaultMaximumSyntheticSequenceIndex + 1U;
        CHECK_FALSE(entity::SyntheticEntityVisualProjectionProvider::create(
            {input}));
        input.sequence_index.reset();
        input.body_value = entity::kDefaultMaximumSyntheticBodyValue + 1U;
        CHECK_FALSE(entity::SyntheticEntityVisualProjectionProvider::create(
            {input}));
        input.body_value.reset();
        input.skin_family_index =
            entity::kDefaultMaximumSyntheticSkinFamilyIndex + 1U;
        CHECK_FALSE(entity::SyntheticEntityVisualProjectionProvider::create(
            {input}));
        input.skin_family_index.reset();
        input.controller_values = std::array{0.0F, 0.0F, 0.0F, 1.01F};
        CHECK_FALSE(entity::SyntheticEntityVisualProjectionProvider::create(
            {input}));
        input.controller_values.reset();
        input.blending_values = std::array{-0.01F, 0.0F};
        CHECK_FALSE(entity::SyntheticEntityVisualProjectionProvider::create(
            {input}));
        input.blending_values.reset();
        input.scale = 0.0F;
        CHECK_FALSE(entity::SyntheticEntityVisualProjectionProvider::create(
            {input}));
    }
    SECTION("forged enum values")
    {
        input.render_mode =
            static_cast<entity::EntityVisualRenderMode>(0xffU);
        const auto render_mode =
            entity::SyntheticEntityVisualProjectionProvider::create({input});
        CHECK_FALSE(render_mode);
        CHECK(render_mode.error_status ==
              entity::EntityVisualProjectionStatus::value_out_of_range);

        input.render_mode.reset();
        input.interpolation_mode =
            static_cast<entity::EntityInterpolationMode>(0xffU);
        const auto interpolation_mode =
            entity::SyntheticEntityVisualProjectionProvider::create({input});
        CHECK_FALSE(interpolation_mode);
        CHECK(interpolation_mode.error_status ==
              entity::EntityVisualProjectionStatus::value_out_of_range);
    }
    SECTION("duplicate explicit input")
    {
        auto duplicate =
            entity::SyntheticEntityVisualProjectionProvider::create(
                {input, input});
        CHECK_FALSE(duplicate);
        CHECK(duplicate.error_status ==
              entity::EntityVisualProjectionStatus::duplicate_entity_input);
    }
}

} // namespace
