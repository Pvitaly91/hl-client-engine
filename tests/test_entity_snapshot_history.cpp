#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
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

inline constexpr fixture::Field kFields[]{
    {"neutral_value", 0x0000'0001U, 0U, 8U},
};

[[nodiscard]] goldsrc::DeltaSchema parse_schema()
{
    const auto encoded = fixture::schema("neutral_t", kFields);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    return std::move(*parsed.schema);
}

[[nodiscard]] goldsrc::DeltaObjectState make_object(
    const goldsrc::DeltaSchema& schema,
    const std::uint32_t value)
{
    const std::vector<goldsrc::DeltaScalarValue> values{value};
    auto built = goldsrc::DeltaObjectBuilder{
        {}, goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1}
                     .build(schema, values);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

struct HistoryFixture {
    goldsrc::EntityBaselineRegistryState baselines;
    goldsrc::DeltaObjectState changed;
};

[[nodiscard]] HistoryFixture make_history_fixture()
{
    auto schema = parse_schema();
    auto baseline = make_object(schema, 1U);
    auto changed = make_object(schema, 2U);
    goldsrc::DeltaSchemaRegistryBuilder schema_builder;
    REQUIRE(schema_builder.insert(schema));
    auto schemas = std::move(schema_builder).publish();
    goldsrc::EntityBaselineRegistryBuilder baseline_builder{
        schemas,
        {},
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    REQUIRE(baseline_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        baseline));
    auto published = std::move(baseline_builder).publish();
    REQUIRE(published);
    REQUIRE(published.state);
    return HistoryFixture{
        std::move(*published.state), std::move(changed)};
}

[[nodiscard]] goldsrc::EntitySnapshotState make_full(
    const goldsrc::EntityBaselineRegistryState& baselines,
    const std::uint32_t reference)
{
    const std::vector<goldsrc::EntitySnapshotEntityInput> input{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    auto built = goldsrc::EntityFullSnapshotBuilder{
        {}, goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
                     .build(
                         goldsrc::EntitySnapshotReference::synthetic(reference),
                         goldsrc::EntityServerTime::synthetic_raw(reference),
                         baselines,
                         input);
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] goldsrc::EntitySnapshotState make_delta(
    const goldsrc::EntityBaselineRegistryState& baselines,
    const goldsrc::DeltaObjectState& changed,
    const goldsrc::EntitySnapshotState& resolved_base,
    const std::uint32_t declared_base,
    const std::uint32_t reference)
{
    const std::vector<goldsrc::EntitySnapshotEntityInput> updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            1U,
            goldsrc::EntityBaselineKey::for_entity(1U),
            changed),
    };
    auto built = goldsrc::EntityDeltaSnapshotBuilder{
        {}, goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
                     .build_with_resolved_base(
                         goldsrc::EntitySnapshotReference::synthetic(reference),
                         goldsrc::EntityServerTime::synthetic_raw(reference),
                         goldsrc::EntitySnapshotReference::synthetic(
                             declared_base),
                         resolved_base,
                         baselines,
                         updates,
                         {});
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] goldsrc::EntitySnapshotHistoryBuilder history_builder(
    goldsrc::EntitySnapshotLimits limits = {})
{
    return goldsrc::EntitySnapshotHistoryBuilder{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
}

TEST_CASE("History inserts full and delta snapshots with exact lookup",
          "[goldsrc][entity][snapshot][history]")
{
    auto fixture_state = make_history_fixture();
    auto full = make_full(fixture_state.baselines, 1U);
    auto delta = make_delta(
        fixture_state.baselines,
        fixture_state.changed,
        full,
        1U,
        2U);
    auto builder = history_builder();
    REQUIRE(builder.insert(full));
    REQUIRE(builder.insert(delta));
    auto published = builder.publish();
    REQUIRE(published);
    REQUIRE(published.state);
    const auto& history = *published.state;
    REQUIRE(history.snapshot_count() == 2U);
    REQUIRE(history.find_exact(goldsrc::EntitySnapshotReference::synthetic(1U)));
    REQUIRE(history.find_exact(goldsrc::EntitySnapshotReference::synthetic(2U)));
    CHECK(history.find_exact(goldsrc::EntitySnapshotReference::synthetic(2U))
              ->kind() == goldsrc::EntitySnapshotKind::delta);
    CHECK(history.oldest_reference()->value() == 1U);
    CHECK(history.newest_reference()->value() == 2U);
    CHECK(history.classify(goldsrc::EntitySnapshotReference::synthetic(3U)) ==
          goldsrc::EntitySnapshotHistoryReferenceStatus::future);
}

TEST_CASE("History rejects duplicate and old non-wrapping references",
          "[goldsrc][entity][snapshot][history][ordering]")
{
    auto fixture_state = make_history_fixture();
    auto one = make_full(fixture_state.baselines, 1U);
    auto two = make_full(fixture_state.baselines, 2U);
    auto builder = history_builder();
    REQUIRE(builder.insert(one));
    REQUIRE(builder.insert(two));

    const auto duplicate = builder.insert(two);
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::duplicate_snapshot);

    auto zero = make_full(fixture_state.baselines, 0U);
    const auto old = builder.insert(zero);
    REQUIRE_FALSE(old);
    REQUIRE(old.error);
    CHECK(old.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::old_snapshot);
    CHECK(builder.candidate_snapshot_count() == 2U);
}

TEST_CASE("History rejects a delta whose exact base is future or missing",
          "[goldsrc][entity][snapshot][history][base]")
{
    auto fixture_state = make_history_fixture();
    auto one = make_full(fixture_state.baselines, 1U);
    auto two = make_full(fixture_state.baselines, 2U);
    auto delta_from_two = make_delta(
        fixture_state.baselines,
        fixture_state.changed,
        two,
        2U,
        3U);
    auto builder = history_builder();
    REQUIRE(builder.insert(one));
    const auto future = builder.insert(delta_from_two);
    REQUIRE_FALSE(future);
    REQUIRE(future.error);
    CHECK(future.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::future_base_reference);

    auto five = make_full(fixture_state.baselines, 5U);
    auto delta_from_five = make_delta(
        fixture_state.baselines,
        fixture_state.changed,
        five,
        5U,
        11U);
    auto sparse_builder = history_builder();
    REQUIRE(sparse_builder.insert(one));
    auto nine = make_full(fixture_state.baselines, 9U);
    REQUIRE(sparse_builder.insert(nine));
    const auto missing = sparse_builder.insert(delta_from_five);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::missing_snapshot_base);
}

TEST_CASE("Bounded history evicts the oldest unrequired snapshot",
          "[goldsrc][entity][snapshot][history][eviction]")
{
    auto fixture_state = make_history_fixture();
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_history = 2U;
    auto builder = history_builder(limits);
    auto one = make_full(fixture_state.baselines, 1U);
    auto two = make_full(fixture_state.baselines, 2U);
    auto three = make_full(fixture_state.baselines, 3U);
    REQUIRE(builder.insert(one));
    REQUIRE(builder.insert(two));
    REQUIRE(builder.insert(three));
    auto published = builder.publish();
    REQUIRE(published);
    REQUIRE(published.state);
    REQUIRE(published.state->snapshot_count() == 2U);
    CHECK(published.state->find_exact(
              goldsrc::EntitySnapshotReference::synthetic(1U)) == nullptr);
    CHECK(published.state->find_exact(
              goldsrc::EntitySnapshotReference::synthetic(2U)) != nullptr);
    REQUIRE(published.state->evicted_through());
    CHECK(published.state->evicted_through()->value() == 1U);
    CHECK(published.state->classify(
              goldsrc::EntitySnapshotReference::synthetic(1U)) ==
          goldsrc::EntitySnapshotHistoryReferenceStatus::evicted);
}

TEST_CASE("History enforces its aggregate retained value-byte budget transactionally",
          "[goldsrc][entity][snapshot][history][memory-limit]")
{
    auto fixture_state = make_history_fixture();
    auto one = make_full(fixture_state.baselines, 1U);
    auto two = make_full(fixture_state.baselines, 2U);
    REQUIRE(one.statistics().accounted_value_bytes != 0U);
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_total_value_bytes =
        one.statistics().accounted_value_bytes;
    auto builder = history_builder(limits);
    REQUIRE(builder.insert(one));
    const auto overflow = builder.insert(two);
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error);
    CHECK(overflow.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::history_limit_exceeded);
    CHECK(builder.candidate_snapshot_count() == 1U);
    auto published = builder.publish();
    REQUIRE(published);
    REQUIRE(published.state);
    CHECK(published.state->accounted_value_bytes() ==
          one.statistics().accounted_value_bytes);
}

TEST_CASE("Required exact base blocks eviction until explicitly released",
          "[goldsrc][entity][snapshot][history][required-base]")
{
    auto fixture_state = make_history_fixture();
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_history = 2U;
    auto builder = history_builder(limits);
    auto one = make_full(fixture_state.baselines, 1U);
    auto two = make_full(fixture_state.baselines, 2U);
    auto three = make_full(fixture_state.baselines, 3U);
    REQUIRE(builder.insert(one));
    REQUIRE(builder.insert(two));
    const auto one_reference = goldsrc::EntitySnapshotReference::synthetic(1U);
    const auto retained = builder.retain_required_base(one_reference);
    REQUIRE(retained);
    CHECK(retained.changed);
    const auto blocked = builder.insert(three);
    REQUIRE_FALSE(blocked);
    REQUIRE(blocked.error);
    CHECK(blocked.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::
              required_base_retention_limit);
    CHECK(builder.candidate_snapshot_count() == 2U);
    REQUIRE(builder.release_required_base(one_reference));
    REQUIRE(builder.insert(three));
    auto published = builder.publish();
    REQUIRE(published);
    REQUIRE(published.state);
    CHECK(published.state->required_base_references().empty());
    CHECK(published.state->snapshot_count() == 2U);
}

TEST_CASE("Delta builder distinguishes an evicted exact base",
          "[goldsrc][entity][snapshot][history][evicted-base]")
{
    auto fixture_state = make_history_fixture();
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_history = 2U;
    auto builder = history_builder(limits);
    auto one = make_full(fixture_state.baselines, 1U);
    auto two = make_full(fixture_state.baselines, 2U);
    auto three = make_full(fixture_state.baselines, 3U);
    REQUIRE(builder.insert(one));
    REQUIRE(builder.insert(two));
    REQUIRE(builder.insert(three));
    auto published = builder.publish();
    REQUIRE(published);
    REQUIRE(published.state);

    const auto result = goldsrc::EntityDeltaSnapshotBuilder{
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
                            .build(
                                goldsrc::EntitySnapshotReference::synthetic(4U),
                                goldsrc::EntityServerTime::synthetic_raw(4),
                                goldsrc::EntitySnapshotReference::synthetic(1U),
                                *published.state,
                                fixture_state.baselines,
                                {},
                                {});
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          goldsrc::EntitySnapshotErrorCode::evicted_delta_snapshot_base);
}

TEST_CASE("Synthetic history never wraps uint32 references",
          "[goldsrc][entity][snapshot][history][wrap]")
{
    auto fixture_state = make_history_fixture();
    auto builder = history_builder();
    auto one = make_full(fixture_state.baselines, 1U);
    auto maximum = make_full(
        fixture_state.baselines,
        (std::numeric_limits<std::uint32_t>::max)());
    auto wrapped = make_full(fixture_state.baselines, 0U);
    REQUIRE(builder.insert(one));
    REQUIRE(builder.insert(maximum));
    const auto rejected = builder.insert(wrapped);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::old_snapshot);
}

TEST_CASE("Stock history exposes typed wrap-policy evidence pending",
          "[goldsrc][entity][snapshot][history][evidence]")
{
    auto fixture_state = make_history_fixture();
    auto one = make_full(fixture_state.baselines, 1U);
    goldsrc::EntitySnapshotHistoryBuilder stock;
    const auto insert = stock.insert(one);
    REQUIRE_FALSE(insert);
    REQUIRE(insert.error);
    CHECK(insert.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::
              wrap_policy_evidence_pending);
    const auto published = stock.publish();
    REQUIRE_FALSE(published);
    REQUIRE(published.error);
    CHECK(published.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::
              wrap_policy_evidence_pending);
}

TEST_CASE("Published snapshot history is an immutable owning copy",
          "[goldsrc][entity][snapshot][history][ownership]")
{
    auto fixture_state = make_history_fixture();
    auto builder = history_builder();
    auto one = make_full(fixture_state.baselines, 1U);
    REQUIRE(builder.insert(one));
    auto first = builder.publish();
    REQUIRE(first);
    REQUIRE(first.state);
    auto two = make_full(fixture_state.baselines, 2U);
    REQUIRE(builder.insert(two));
    auto second = builder.publish();
    REQUIRE(second);
    REQUIRE(second.state);
    CHECK(first.state->snapshot_count() == 1U);
    CHECK(second.state->snapshot_count() == 2U);
    CHECK(first.state->snapshots().data() != second.state->snapshots().data());
    const auto copied = *second.state;
    CHECK(copied.snapshots().data() != second.state->snapshots().data());
    CHECK(copied.find_exact(goldsrc::EntitySnapshotReference::synthetic(2U)) !=
          nullptr);
}

static_assert(
    goldsrc::kDefaultMaximumEntitySnapshotHistory == 64U);
static_assert(goldsrc::kMaximumEntitySnapshotHistory == 256U);
static_assert(
    std::is_copy_constructible_v<goldsrc::EntitySnapshotHistoryState>);
static_assert(
    !std::is_copy_assignable_v<goldsrc::EntitySnapshotHistoryState>);

} // namespace
