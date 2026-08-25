#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

inline constexpr fixture::Field kOrdinaryFields[]{
    {"neutral_value", 0x0000'0001U, 0U, 8U},
    {"neutral_aux", 0x0000'0002U, 4U, 11U},
};
inline constexpr fixture::Field kPlayerFields[]{
    {"player_value", 0x0000'0001U, 0U, 8U},
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
    std::vector<goldsrc::DeltaScalarValue> values)
{
    auto built = goldsrc::DeltaObjectBuilder{
        {}, goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1}
                     .build(schema, values);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

struct DeltaFixture {
    goldsrc::DeltaSchemaRegistryState schemas;
    goldsrc::EntityBaselineRegistryState baselines;
    goldsrc::DeltaObjectState ordinary_base;
    goldsrc::DeltaObjectState ordinary_changed;
    goldsrc::DeltaObjectState ordinary_changed_again;
    goldsrc::DeltaObjectState player_changed;
};

[[nodiscard]] DeltaFixture make_delta_fixture()
{
    auto ordinary_schema = parse_schema("ordinary_t", kOrdinaryFields);
    auto player_schema = parse_schema("player_t", kPlayerFields);
    auto ordinary_base = make_object(
        ordinary_schema,
        {std::uint32_t{1U}, std::uint32_t{10U}});
    auto ordinary_changed = make_object(
        ordinary_schema,
        {std::uint32_t{7U}, std::uint32_t{77U}});
    auto ordinary_changed_again = make_object(
        ordinary_schema,
        {std::uint32_t{8U}, std::uint32_t{88U}});
    auto player_base =
        make_object(player_schema, {std::uint32_t{2U}});
    auto player_changed =
        make_object(player_schema, {std::uint32_t{3U}});

    goldsrc::DeltaSchemaRegistryBuilder schema_builder;
    REQUIRE(schema_builder.insert(ordinary_schema));
    REQUIRE(schema_builder.insert(player_schema));
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
        goldsrc::EntitySchemaCategory::alternate_explicit_schema,
        ordinary_base));
    auto published = std::move(baseline_builder).publish();
    REQUIRE(published);
    REQUIRE(published.state);
    return DeltaFixture{
        std::move(schemas),
        std::move(*published.state),
        std::move(ordinary_base),
        std::move(ordinary_changed),
        std::move(ordinary_changed_again),
        std::move(player_changed),
    };
}

[[nodiscard]] goldsrc::EntitySnapshotState make_full(
    const goldsrc::EntityBaselineRegistryState& baselines,
    const std::uint32_t reference = 10U)
{
    const std::vector<goldsrc::EntitySnapshotEntityInput> entities{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            2U, goldsrc::EntityBaselineKey::for_entity(2U)),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            4U, goldsrc::EntityBaselineKey::for_alternate_slot(7U)),
    };
    auto built = goldsrc::EntityFullSnapshotBuilder{
        {}, goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
                     .build(
                         goldsrc::EntitySnapshotReference::synthetic(reference),
                         goldsrc::EntityServerTime::synthetic_raw(100),
                         baselines,
                         entities);
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] goldsrc::EntitySnapshotHistoryState history_with(
    const goldsrc::EntitySnapshotState& snapshot,
    goldsrc::EntitySnapshotLimits limits = {})
{
    goldsrc::EntitySnapshotHistoryBuilder history{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    REQUIRE(history.insert(snapshot));
    auto published = history.publish();
    REQUIRE(published);
    REQUIRE(published.state);
    return std::move(*published.state);
}

[[nodiscard]] goldsrc::EntityDeltaSnapshotBuilder delta_builder(
    goldsrc::EntitySnapshotLimits limits = {})
{
    return goldsrc::EntityDeltaSnapshotBuilder{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
}

TEST_CASE("Delta snapshot changes one entity and preserves multiple typed fields",
          "[goldsrc][entity][snapshot][delta][change]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed),
    };
    const auto built = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(101),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        {});
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->kind() == goldsrc::EntitySnapshotKind::delta);
    REQUIRE(built.state->base_reference());
    CHECK(built.state->base_reference()->value() == 10U);
    REQUIRE(built.state->find_exact(1U));
    const auto& object = built.state->find_exact(1U)->object();
    REQUIRE(object.find_exact("neutral_value"));
    REQUIRE(object.find_exact("neutral_aux"));
    CHECK(std::get<std::uint32_t>(
              object.find_exact("neutral_value")->value()) == 7U);
    CHECK(std::get<std::uint32_t>(
              object.find_exact("neutral_aux")->value()) == 77U);
    CHECK(built.state->statistics().changed_count == 1U);
    CHECK(built.state->statistics().added_count == 0U);
    CHECK(built.state->statistics().removed_count == 0U);
}

TEST_CASE("Delta changed-field limit counts actual value differences",
          "[goldsrc][entity][snapshot][delta][changed-field-limit]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed),
    };
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_changed_fields_per_entity = 2U;
    REQUIRE(delta_builder(limits).build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(101),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        {}));

    limits.maximum_changed_fields_per_entity = 1U;
    const auto rejected = delta_builder(limits).build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(101),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        {});
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::EntitySnapshotErrorCode::field_limit_exceeded);
}

TEST_CASE("Delta snapshot adds from exact baseline and removes explicitly",
          "[goldsrc][entity][snapshot][delta][add][remove]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            3U, goldsrc::EntityBaselineKey::for_alternate_slot(7U)),
    };
    const std::array<std::uint32_t, 1U> removals{2U};
    const auto built = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(102),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        removals);
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entity_count() == 3U);
    CHECK(built.state->entities()[0U].entity_number() == 1U);
    CHECK(built.state->entities()[1U].entity_number() == 3U);
    CHECK(built.state->entities()[2U].entity_number() == 4U);
    CHECK(built.state->find_exact(2U) == nullptr);
    REQUIRE(built.state->removed_entity_numbers().size() == 1U);
    CHECK(built.state->removed_entity_numbers().front() == 2U);
    CHECK(built.state->statistics().added_count == 1U);
    CHECK(built.state->statistics().removed_count == 1U);
}

TEST_CASE("Unchanged delta entities structurally share immutable objects",
          "[goldsrc][entity][snapshot][delta][sharing]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed),
    };
    const auto built = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(103),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        {});
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->find_exact(1U));
    REQUIRE(built.state->find_exact(2U));
    REQUIRE(built.state->find_exact(4U));
    CHECK_FALSE(built.state->find_exact(1U)->shares_object_with(
        *full.find_exact(1U)));
    CHECK(built.state->find_exact(2U)->shares_object_with(
        *full.find_exact(2U)));
    CHECK(built.state->find_exact(4U)->shares_object_with(
        *full.find_exact(4U)));
    CHECK(built.state->statistics().unchanged_shared_count == 2U);
}

TEST_CASE("Identical explicit delta update remains a shared no-op",
          "[goldsrc][entity][snapshot][delta][sharing][no-op]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_base),
    };
    const auto built = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(103),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        {});
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->find_exact(1U));
    REQUIRE(full.find_exact(1U));
    CHECK(built.state->find_exact(1U)->shares_object_with(
        *full.find_exact(1U)));
    CHECK(built.state->statistics().changed_count == 0U);
    CHECK(built.state->statistics().added_count == 0U);
    CHECK(built.state->statistics().unchanged_shared_count == 3U);
}

TEST_CASE("Delta and history revalidate retained snapshots under current limits",
          "[goldsrc][entity][snapshot][delta][history][cross-limit]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);

    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_entities_per_snapshot = 2U;
    const auto entity_limit = delta_builder(limits).build_with_resolved_base(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(104),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        full,
        fixture_state.baselines,
        {},
        {});
    REQUIRE_FALSE(entity_limit);
    REQUIRE(entity_limit.error);
    CHECK(entity_limit.error->code ==
          goldsrc::EntitySnapshotErrorCode::entity_limit_exceeded);

    limits.maximum_entities_per_snapshot =
        goldsrc::kDefaultMaximumEntitiesPerSnapshot;
    limits.maximum_fields_per_entity = 1U;
    const auto field_limit = delta_builder(limits).build_with_resolved_base(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(104),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        full,
        fixture_state.baselines,
        {},
        {});
    REQUIRE_FALSE(field_limit);
    REQUIRE(field_limit.error);
    CHECK(field_limit.error->code ==
          goldsrc::EntitySnapshotErrorCode::field_limit_exceeded);

    limits.maximum_fields_per_entity =
        goldsrc::kDefaultMaximumEntityFields;
    limits.maximum_entity_number = 4U;
    const auto key_limit = delta_builder(limits).build_with_resolved_base(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(104),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        full,
        fixture_state.baselines,
        {},
        {});
    REQUIRE_FALSE(key_limit);
    REQUIRE(key_limit.error);
    CHECK(key_limit.error->code ==
          goldsrc::EntitySnapshotErrorCode::entity_number_limit_exceeded);

    goldsrc::EntityBaselineRegistryBuilder empty_builder{
        fixture_state.schemas,
        {},
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    auto empty_baselines = std::move(empty_builder).publish();
    REQUIRE(empty_baselines);
    REQUIRE(empty_baselines.state);
    const auto missing_current_baseline =
        delta_builder().build_with_resolved_base(
            goldsrc::EntitySnapshotReference::synthetic(11U),
            goldsrc::EntityServerTime::synthetic_raw(104),
            goldsrc::EntitySnapshotReference::synthetic(10U),
            full,
            *empty_baselines.state,
            {},
            {});
    REQUIRE_FALSE(missing_current_baseline);
    REQUIRE(missing_current_baseline.error);
    CHECK(missing_current_baseline.error->code ==
          goldsrc::EntitySnapshotErrorCode::missing_baseline);

    limits = {};
    limits.maximum_entities_per_snapshot = 2U;
    goldsrc::EntitySnapshotHistoryBuilder count_history{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    const auto history_count_limit = count_history.insert(full);
    REQUIRE_FALSE(history_count_limit);
    REQUIRE(history_count_limit.error);
    CHECK(history_count_limit.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::history_limit_exceeded);
    CHECK(count_history.candidate_snapshot_count() == 0U);

    limits.maximum_entities_per_snapshot =
        goldsrc::kDefaultMaximumEntitiesPerSnapshot;
    limits.maximum_fields_per_entity = 1U;
    goldsrc::EntitySnapshotHistoryBuilder field_history{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    const auto history_field_limit = field_history.insert(full);
    REQUIRE_FALSE(history_field_limit);
    REQUIRE(history_field_limit.error);
    CHECK(history_field_limit.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::history_limit_exceeded);
    CHECK(field_history.candidate_snapshot_count() == 0U);

    limits.maximum_fields_per_entity =
        goldsrc::kDefaultMaximumEntityFields;
    limits.maximum_entity_number = 4U;
    goldsrc::EntitySnapshotHistoryBuilder key_history{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    const auto history_key_limit = key_history.insert(full);
    REQUIRE_FALSE(history_key_limit);
    REQUIRE(history_key_limit.error);
    CHECK(history_key_limit.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::history_limit_exceeded);
    CHECK(key_history.candidate_snapshot_count() == 0U);
}

TEST_CASE("Multiple removals remain sorted metadata and do not mutate baselines",
          "[goldsrc][entity][snapshot][delta][remove][immutable]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const auto baseline_count = fixture_state.baselines.baseline_count();
    const std::array<std::uint32_t, 2U> removals{1U, 4U};
    const auto built = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(104),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        {},
        removals);
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entity_count() == 1U);
    CHECK(built.state->entities().front().entity_number() == 2U);
    CHECK(built.state->removed_entity_numbers()[0U] == 1U);
    CHECK(built.state->removed_entity_numbers()[1U] == 4U);
    CHECK(fixture_state.baselines.baseline_count() == baseline_count);
    CHECK(full.entity_count() == 3U);
}

TEST_CASE("Delta snapshot distinguishes missing future and wrong exact bases",
          "[goldsrc][entity][snapshot][delta][base]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    goldsrc::EntitySnapshotHistoryBuilder empty_builder{
        {}, goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    auto empty_published = empty_builder.publish();
    REQUIRE(empty_published);
    REQUIRE(empty_published.state);

    const auto missing = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(105),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        *empty_published.state,
        fixture_state.baselines,
        {},
        {});
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::EntitySnapshotErrorCode::missing_delta_snapshot_base);

    auto history = history_with(full);
    const auto future = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(106),
        goldsrc::EntitySnapshotReference::synthetic(11U),
        history,
        fixture_state.baselines,
        {},
        {});
    REQUIRE_FALSE(future);
    REQUIRE(future.error);
    CHECK(future.error->code ==
          goldsrc::EntitySnapshotErrorCode::future_delta_snapshot_base);

    const auto wrong = delta_builder().build_with_resolved_base(
        goldsrc::EntitySnapshotReference::synthetic(12U),
        goldsrc::EntityServerTime::synthetic_raw(107),
        goldsrc::EntitySnapshotReference::synthetic(9U),
        full,
        fixture_state.baselines,
        {},
        {});
    REQUIRE_FALSE(wrong);
    REQUIRE(wrong.error);
    CHECK(wrong.error->code ==
          goldsrc::EntitySnapshotErrorCode::wrong_delta_snapshot_base);
}

TEST_CASE("Delta update and removal ordering failures publish no state",
          "[goldsrc][entity][snapshot][delta][ordering][transactional]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> duplicates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed),
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed_again),
    };
    const auto duplicate = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(108),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        duplicates,
        {});
    REQUIRE_FALSE(duplicate);
    CHECK_FALSE(duplicate.state);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
          goldsrc::EntitySnapshotErrorCode::duplicate_entity);

    const std::array<std::uint32_t, 2U> descending{4U, 1U};
    const auto out_of_order = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(109),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        {},
        descending);
    REQUIRE_FALSE(out_of_order);
    REQUIRE(out_of_order.error);
    CHECK(out_of_order.error->code ==
          goldsrc::EntitySnapshotErrorCode::out_of_order_removal);
}

TEST_CASE("Delta rejects nonexistent removal missing added baseline and schema mismatch",
          "[goldsrc][entity][snapshot][delta][validation]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    auto history = history_with(full);
    const std::array<std::uint32_t, 1U> nonexistent{3U};
    const auto removal = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(110),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        {},
        nonexistent);
    REQUIRE_FALSE(removal);
    REQUIRE(removal.error);
    CHECK(removal.error->code ==
          goldsrc::EntitySnapshotErrorCode::remove_nonexistent_entity);

    const std::vector<goldsrc::EntitySnapshotEntityInput> missing_baseline{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            3U, goldsrc::EntityBaselineKey::for_entity(3U)),
    };
    const auto missing = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(111),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        missing_baseline,
        {});
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::EntitySnapshotErrorCode::missing_baseline);

    const std::vector<goldsrc::EntitySnapshotEntityInput> aliased_identity{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            3U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const auto identity = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(112),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        aliased_identity,
        {});
    REQUIRE_FALSE(identity);
    REQUIRE(identity.error);
    CHECK(identity.error->code ==
          goldsrc::EntitySnapshotErrorCode::baseline_identity_mismatch);

    const std::vector<goldsrc::EntitySnapshotEntityInput> wrong_schema{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.player_changed),
    };
    const auto schema = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(113),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        wrong_schema,
        {});
    REQUIRE_FALSE(schema);
    REQUIRE(schema.error);
    CHECK(schema.error->code ==
          goldsrc::EntitySnapshotErrorCode::schema_mismatch);
}

TEST_CASE("Delta merge keeps exact order and leaves the base immutable",
          "[goldsrc][entity][snapshot][delta][order][immutable]")
{
    auto fixture_state = make_delta_fixture();
    auto full = make_full(fixture_state.baselines);
    const auto original_base_value = std::get<std::uint32_t>(
        full.find_exact(1U)->object().find_exact("neutral_value")->value());
    auto history = history_with(full);
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            fixture_state.ordinary_changed),
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            3U, goldsrc::EntityBaselineKey::for_alternate_slot(7U)),
    };
    const std::array<std::uint32_t, 1U> removals{2U};
    const auto built = delta_builder().build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(113),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        history,
        fixture_state.baselines,
        updates,
        removals);
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entity_count() == 3U);
    CHECK(built.state->entities()[0U].entity_number() == 1U);
    CHECK(built.state->entities()[1U].entity_number() == 3U);
    CHECK(built.state->entities()[2U].entity_number() == 4U);
    CHECK(full.entity_count() == 3U);
    CHECK(std::get<std::uint32_t>(
              full.find_exact(1U)
                  ->object()
                  .find_exact("neutral_value")
                  ->value()) == original_base_value);
}

} // namespace
