#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

inline constexpr fixture::Field kOrdinaryFields[]{
    {"neutral_value", 0x0000'0001U, 0U, 8U},
};
inline constexpr fixture::Field kOrdinaryNarrowFields[]{
    {"neutral_value", 0x0000'0001U, 0U, 7U},
};
inline constexpr fixture::Field kPlayerFields[]{
    {"player_value", 0x0000'0002U, 0U, 11U},
};
inline constexpr fixture::Field kCustomFields[]{
    {"custom_value", 0x0000'0008U, 0U, 16U},
};

[[nodiscard]] goldsrc::DeltaSchema parse_schema(
    const std::string& name,
    const std::span<const fixture::Field> fields)
{
    const auto encoded = fixture::schema(name, fields);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    return std::move(*parsed.schema);
}

[[nodiscard]] goldsrc::DeltaObjectState make_object(
    const goldsrc::DeltaSchema& schema,
    goldsrc::DeltaScalarValue value)
{
    const std::vector<goldsrc::DeltaScalarValue> values{std::move(value)};
    auto built = goldsrc::DeltaObjectBuilder{
        {}, goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1}
                     .build(schema, values);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

struct SnapshotFixture {
    goldsrc::EntityBaselineRegistryState baselines;
    goldsrc::DeltaObjectState ordinary_changed;
    goldsrc::DeltaObjectState player_changed;
};

[[nodiscard]] SnapshotFixture make_snapshot_fixture()
{
    auto ordinary_schema = parse_schema("ordinary_t", kOrdinaryFields);
    auto player_schema = parse_schema("player_t", kPlayerFields);
    auto custom_schema = parse_schema("custom_t", kCustomFields);
    auto ordinary_base = make_object(ordinary_schema, std::uint32_t{1U});
    auto player_base = make_object(player_schema, std::uint32_t{2U});
    auto custom_base = make_object(custom_schema, std::uint32_t{3U});
    auto ordinary_changed =
        make_object(ordinary_schema, std::uint32_t{41U});
    auto player_changed = make_object(player_schema, std::uint32_t{42U});

    goldsrc::DeltaSchemaRegistryBuilder schema_builder;
    REQUIRE(schema_builder.insert(ordinary_schema));
    REQUIRE(schema_builder.insert(player_schema));
    REQUIRE(schema_builder.insert(custom_schema));
    auto schemas = std::move(schema_builder).publish();
    goldsrc::EntityBaselineRegistryBuilder baseline_builder{
        schemas,
        {},
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    REQUIRE(baseline_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        ordinary_base));
    REQUIRE(baseline_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(2U),
        goldsrc::EntitySchemaCategory::player_entity,
        player_base));
    REQUIRE(baseline_builder.insert(
        goldsrc::EntityBaselineKey::for_alternate_slot(7U),
        goldsrc::EntitySchemaCategory::custom_entity,
        custom_base));
    auto published = std::move(baseline_builder).publish();
    REQUIRE(published);
    REQUIRE(published.state);
    return SnapshotFixture{
        std::move(*published.state),
        std::move(ordinary_changed),
        std::move(player_changed),
    };
}

[[nodiscard]] goldsrc::EntityFullSnapshotBuilder full_builder(
    goldsrc::EntitySnapshotLimits limits = {})
{
    return goldsrc::EntityFullSnapshotBuilder{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
}

TEST_CASE("Synthetic neutral full snapshot has an explicit empty policy",
          "[goldsrc][entity][snapshot][full][empty]")
{
    auto fixture_state = make_snapshot_fixture();
    const auto built = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(10U),
        goldsrc::EntityServerTime::synthetic_raw(500),
        fixture_state.baselines,
        {});
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->kind() == goldsrc::EntitySnapshotKind::full);
    CHECK(built.state->entity_count() == 0U);
    CHECK_FALSE(built.state->base_reference());
    CHECK(built.state->removed_entity_numbers().empty());
    CHECK(built.state->statistics().entity_count == 0U);
}

TEST_CASE("Full snapshot can initialize one entity from an exact baseline",
          "[goldsrc][entity][snapshot][full][baseline]")
{
    auto fixture_state = make_snapshot_fixture();
    const std::vector<goldsrc::EntitySnapshotEntityInput> inputs{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const auto built = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(501),
        fixture_state.baselines,
        inputs);
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entity_count() == 1U);
    const auto* entity = built.state->find_exact(1U);
    REQUIRE(entity != nullptr);
    const auto* baseline = fixture_state.baselines.find_exact(
        goldsrc::EntityBaselineKey::for_entity(1U));
    REQUIRE(baseline != nullptr);
    CHECK(&entity->object() == &baseline->object());
    CHECK(entity->schema_category() ==
          goldsrc::EntitySchemaCategory::ordinary_entity);
    CHECK(built.state->statistics().added_count == 1U);
    CHECK(built.state->statistics().unchanged_shared_count == 1U);
}

TEST_CASE("Full snapshot preserves explicit ordinary player custom order",
          "[goldsrc][entity][snapshot][full][schema-selection]")
{
    auto fixture_state = make_snapshot_fixture();
    const std::vector<goldsrc::EntitySnapshotEntityInput> inputs{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed),
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            2U,
            goldsrc::EntityBaselineKey::for_entity(2U),
            fixture_state.player_changed),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            9U, goldsrc::EntityBaselineKey::for_alternate_slot(7U)),
    };
    const goldsrc::EntitySourceGeometry geometry{8U, 9U, 5U, 51U};
    const auto built = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(12U),
        goldsrc::EntityServerTime::synthetic_raw(777),
        fixture_state.baselines,
        inputs,
        geometry);
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entity_count() == 3U);
    CHECK(built.state->entities()[0U].entity_number() == 1U);
    CHECK(built.state->entities()[1U].entity_number() == 2U);
    CHECK(built.state->entities()[2U].entity_number() == 9U);
    CHECK(built.state->entities()[1U].schema_category() ==
          goldsrc::EntitySchemaCategory::player_entity);
    CHECK(built.state->entities()[2U].schema_category() ==
          goldsrc::EntitySchemaCategory::custom_entity);
    CHECK(built.state->server_time().raw_value() == 777);
    CHECK(built.state->source_geometry().start_bit_offset == 5U);
    CHECK(built.state->source_geometry().bits_consumed == 51U);
    CHECK(built.state->statistics().changed_count == 2U);
    CHECK(built.state->statistics().added_count == 3U);
}

TEST_CASE("Full snapshot rejects duplicate and out-of-order input without sorting",
          "[goldsrc][entity][snapshot][full][ordering][transactional]")
{
    auto fixture_state = make_snapshot_fixture();
    const std::vector<goldsrc::EntitySnapshotEntityInput> duplicate{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const auto duplicate_result = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(20U),
        goldsrc::EntityServerTime::synthetic_raw(1),
        fixture_state.baselines,
        duplicate);
    REQUIRE_FALSE(duplicate_result);
    CHECK_FALSE(duplicate_result.state);
    REQUIRE(duplicate_result.error);
    CHECK(duplicate_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::duplicate_entity);

    const std::vector<goldsrc::EntitySnapshotEntityInput> descending{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            2U, goldsrc::EntityBaselineKey::for_entity(2U)),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const auto descending_result = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(21U),
        goldsrc::EntityServerTime::synthetic_raw(2),
        fixture_state.baselines,
        descending);
    REQUIRE_FALSE(descending_result);
    CHECK_FALSE(descending_result.state);
    REQUIRE(descending_result.error);
    CHECK(descending_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::out_of_order_entity);
}

TEST_CASE("Full snapshot requires exact baseline and schema",
          "[goldsrc][entity][snapshot][full][baseline][schema]")
{
    auto fixture_state = make_snapshot_fixture();
    const std::vector<goldsrc::EntitySnapshotEntityInput> missing{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            3U, goldsrc::EntityBaselineKey::for_entity(3U)),
    };
    const auto missing_result = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(30U),
        goldsrc::EntityServerTime::synthetic_raw(3),
        fixture_state.baselines,
        missing);
    REQUIRE_FALSE(missing_result);
    REQUIRE(missing_result.error);
    CHECK(missing_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::missing_baseline);

    const std::vector<goldsrc::EntitySnapshotEntityInput> aliased_identity{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            2U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const auto identity_result = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(31U),
        goldsrc::EntityServerTime::synthetic_raw(4),
        fixture_state.baselines,
        aliased_identity);
    REQUIRE_FALSE(identity_result);
    REQUIRE(identity_result.error);
    CHECK(identity_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::baseline_identity_mismatch);

    const std::vector<goldsrc::EntitySnapshotEntityInput> mismatched{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.player_changed),
    };
    const auto mismatch_result = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(32U),
        goldsrc::EntityServerTime::synthetic_raw(5),
        fixture_state.baselines,
        mismatched);
    REQUIRE_FALSE(mismatch_result);
    REQUIRE(mismatch_result.error);
    CHECK(mismatch_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::baseline_schema_mismatch);

    auto foreign_schema =
        parse_schema("ordinary_t", kOrdinaryNarrowFields);
    auto foreign_object =
        make_object(foreign_schema, std::uint32_t{17U});
    const std::vector<goldsrc::EntitySnapshotEntityInput>
        same_name_foreign_descriptor{
            goldsrc::EntitySnapshotEntityInput::with_decoded_state(
                1U,
                goldsrc::EntityBaselineKey::for_entity(1U),
                foreign_object),
        };
    const auto descriptor_result = full_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(33U),
        goldsrc::EntityServerTime::synthetic_raw(6),
        fixture_state.baselines,
        same_name_foreign_descriptor);
    REQUIRE_FALSE(descriptor_result);
    REQUIRE(descriptor_result.error);
    CHECK(descriptor_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::baseline_schema_mismatch);
}

TEST_CASE("Full snapshot entity and value-byte limits are transactional",
          "[goldsrc][entity][snapshot][full][limit]")
{
    auto fixture_state = make_snapshot_fixture();
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_entities_per_snapshot = 1U;
    const std::vector<goldsrc::EntitySnapshotEntityInput> two{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            2U, goldsrc::EntityBaselineKey::for_entity(2U)),
    };
    const auto count_result = full_builder(limits).build(
        goldsrc::EntitySnapshotReference::synthetic(40U),
        goldsrc::EntityServerTime::synthetic_raw(5),
        fixture_state.baselines,
        two);
    REQUIRE_FALSE(count_result);
    CHECK_FALSE(count_result.state);
    REQUIRE(count_result.error);
    CHECK(count_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::entity_limit_exceeded);

    limits.maximum_entities_per_snapshot = 2U;
    limits.maximum_entity_number = 1U;
    const auto number_result = full_builder(limits).build(
        goldsrc::EntitySnapshotReference::synthetic(41U),
        goldsrc::EntityServerTime::synthetic_raw(6),
        fixture_state.baselines,
        two);
    REQUIRE_FALSE(number_result);
    REQUIRE(number_result.error);
    CHECK(number_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::entity_number_limit_exceeded);

    limits.maximum_entity_number =
        goldsrc::kDefaultMaximumEntityNumber;
    limits.maximum_snapshot_total_value_bytes = 4U;
    const auto byte_result = full_builder(limits).build(
        goldsrc::EntitySnapshotReference::synthetic(42U),
        goldsrc::EntityServerTime::synthetic_raw(7),
        fixture_state.baselines,
        two);
    REQUIRE_FALSE(byte_result);
    CHECK_FALSE(byte_result.state);
    REQUIRE(byte_result.error);
    CHECK(byte_result.error->code ==
          goldsrc::EntitySnapshotErrorCode::total_value_bytes_exceeded);
}

TEST_CASE("Full snapshot is owning and generic DeltaObjectState stays authoritative",
          "[goldsrc][entity][snapshot][full][ownership]")
{
    auto fixture_state = make_snapshot_fixture();
    auto snapshot = [&] {
        const std::vector<goldsrc::EntitySnapshotEntityInput> transient{
            goldsrc::EntitySnapshotEntityInput::with_decoded_state(
                1U,
                goldsrc::EntityBaselineKey::for_entity(1U),
                fixture_state.ordinary_changed),
        };
        auto built = full_builder().build(
            goldsrc::EntitySnapshotReference::synthetic(50U),
            goldsrc::EntityServerTime::synthetic_raw(7),
            fixture_state.baselines,
            transient);
        REQUIRE(built);
        REQUIRE(built.state);
        return std::move(*built.state);
    }();
    REQUIRE(snapshot.entity_count() == 1U);
    const auto* field = snapshot.entities().front().object().find_exact(
        "neutral_value");
    REQUIRE(field != nullptr);
    CHECK(std::get<std::uint32_t>(field->value()) == 41U);
    CHECK_FALSE(snapshot.entities().front().semantic_projection());
    const auto independent = snapshot;
    CHECK(independent.entities().data() != snapshot.entities().data());
    CHECK(independent.find_exact(1U) != nullptr);
}

TEST_CASE("Wire-only full snapshot cases remain stock evidence pending",
          "[goldsrc][entity][snapshot][full][evidence]")
{
    auto fixture_state = make_snapshot_fixture();
    const std::vector<goldsrc::EntitySnapshotEntityInput> input{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const auto stock = goldsrc::EntityFullSnapshotBuilder{}.build(
        goldsrc::EntitySnapshotReference::synthetic(60U),
        goldsrc::EntityServerTime::synthetic_raw(8),
        fixture_state.baselines,
        input,
        goldsrc::EntitySourceGeometry{0U, 1U, 0U, 8U});
    REQUIRE_FALSE(stock);
    CHECK_FALSE(stock.state);
    REQUIRE(stock.error);
    CHECK(stock.error->code == goldsrc::EntitySnapshotErrorCode::evidence_pending);
}

static_assert(std::is_copy_constructible_v<goldsrc::EntitySnapshotState>);
static_assert(!std::is_copy_assignable_v<goldsrc::EntitySnapshotState>);
static_assert(!std::is_default_constructible_v<goldsrc::EntitySnapshotState>);

} // namespace
