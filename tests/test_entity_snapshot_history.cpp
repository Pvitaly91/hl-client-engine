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
#include <variant>
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

TEST_CASE("CPU history preflight preserves publications and exact values through repeated eviction",
          "[cpu-frame-1b7][history-preflight][goldsrc][entity][snapshot][history][ownership]")
{
    auto data=make_history_fixture();
    const auto one=make_full(data.baselines,1);
    auto limits=goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_history=3;
    limits.maximum_snapshot_total_value_bytes=one.statistics().accounted_value_bytes*3;
    auto builder=history_builder(limits);
    std::vector<goldsrc::EntitySnapshotHistoryState> publications;
    publications.reserve(24);
    for(std::uint32_t ordinal=1;ordinal<=24;++ordinal) {
        const auto incoming=ordinal%2==0 ? make_delta(data.baselines,data.changed,
            *publications.back().find_exact(goldsrc::EntitySnapshotReference::synthetic(ordinal-1)),
            ordinal-1,ordinal) : make_full(data.baselines,ordinal);
        REQUIRE(builder.insert(incoming));
        // Insertion must never consume a caller-owned input, even though the
        // builder's own retained transactional snapshots can move internally.
        CHECK(incoming.reference().value()==ordinal);
        REQUIRE(incoming.entities().size()==1);
        CHECK(std::get<std::uint32_t>(incoming.entities().front().object().find_exact("neutral_value")->value())==
              (ordinal%2==0 ? 2U : 1U));
        auto published=builder.publish();REQUIRE(published);REQUIRE(published.state);
        publications.push_back(std::move(*published.state));
        for(std::uint32_t accepted=1;accepted<=ordinal;++accepted) {
            const auto& history=publications[accepted-1];
            const auto count=accepted<3 ? accepted : 3U;
            CHECK(history.snapshot_count()==count);
            CHECK(history.accounted_value_bytes()==one.statistics().accounted_value_bytes*count);
            CHECK(history.oldest_reference()->value()==accepted-count+1);
            CHECK(history.newest_reference()->value()==accepted);
            CHECK(history.required_base_references().empty());
            if(accepted>3) {REQUIRE(history.evicted_through());CHECK(history.evicted_through()->value()==accepted-3);}
            else CHECK_FALSE(history.evicted_through());
            for(const auto& snapshot:history.snapshots()) {
                const auto reference=snapshot.reference().value();
                CHECK(snapshot.server_time().raw_value()==reference);
                CHECK(snapshot.kind()==(reference%2==0 ? goldsrc::EntitySnapshotKind::delta : goldsrc::EntitySnapshotKind::full));
                if(reference%2==0) {REQUIRE(snapshot.base_reference());CHECK(snapshot.base_reference()->value()==reference-1);}
                else CHECK_FALSE(snapshot.base_reference());
                REQUIRE(snapshot.entities().size()==1);
                CHECK(std::get<std::uint32_t>(snapshot.entities().front().object().find_exact("neutral_value")->value())==
                      (reference%2==0 ? 2U : 1U));
            }
        }
    }
}

TEST_CASE("CPU history preflight rejects aliased duplicate and bounded failures before consuming state",
          "[cpu-frame-1b7][history-preflight][goldsrc][entity][snapshot][history][transaction]")
{
    for(const bool required_base:{false,true}) {
        CAPTURE(required_base);
        auto data=make_history_fixture();
        const auto one=make_full(data.baselines,1),two=make_full(data.baselines,2),three=make_full(data.baselines,3);
        auto limits=goldsrc::EntitySnapshotLimits{};
        limits.maximum_snapshot_history=required_base ? 2U : 3U;
        limits.maximum_snapshot_total_value_bytes=one.statistics().accounted_value_bytes*2;
        auto original=history_builder(limits);REQUIRE(original.insert(one));REQUIRE(original.insert(two));
        const auto prior=original.publish();REQUIRE(prior);REQUIRE(prior.state);
        goldsrc::EntitySnapshotHistoryBuilder staged{*prior.state,limits};
        const auto first=goldsrc::EntitySnapshotReference::synthetic(1);
        if(required_base)REQUIRE(staged.retain_required_base(first));
        const auto* borrowed=prior.state->find_exact(first);REQUIRE(borrowed);
        const auto duplicate=staged.insert(*borrowed);REQUIRE_FALSE(duplicate);REQUIRE(duplicate.error);
        CHECK(duplicate.error->code==goldsrc::EntitySnapshotHistoryErrorCode::duplicate_snapshot);
        const auto nine=make_full(data.baselines,9);
        const auto missing=make_delta(data.baselines,data.changed,nine,9,10);
        const auto rejected_delta=staged.insert(missing);REQUIRE_FALSE(rejected_delta);REQUIRE(rejected_delta.error);
        CHECK(rejected_delta.error->code==goldsrc::EntitySnapshotHistoryErrorCode::future_base_reference);
        const auto rejected=staged.insert(three);REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
        CHECK(rejected.error->code==(required_base ? goldsrc::EntitySnapshotHistoryErrorCode::required_base_retention_limit :
            goldsrc::EntitySnapshotHistoryErrorCode::history_limit_exceeded));
        const auto preserved=staged.publish();REQUIRE(preserved);REQUIRE(preserved.state);
        for(const auto* history:{&*prior.state,&*preserved.state}) {
            CHECK(history->snapshot_count()==2);
            CHECK(history->oldest_reference()->value()==1);CHECK(history->newest_reference()->value()==2);
            CHECK(history->accounted_value_bytes()==one.statistics().accounted_value_bytes*2);
            CHECK_FALSE(history->evicted_through());
            for(const auto& snapshot:history->snapshots()) {
                REQUIRE(snapshot.entities().size()==1);
                CHECK(std::get<std::uint32_t>(snapshot.entities().front().object().find_exact("neutral_value")->value())==1);
            }
        }
        CHECK(preserved.state->required_base_references().size()==(required_base ? 1U : 0U));
        CHECK(prior.state->required_base_references().empty());
        if(required_base) {
            REQUIRE(staged.release_required_base(first));REQUIRE(staged.insert(three));
            const auto recovered=staged.publish();REQUIRE(recovered);REQUIRE(recovered.state);
            CHECK(recovered.state->snapshot_count()==2);CHECK(recovered.state->oldest_reference()->value()==2);
            CHECK(recovered.state->newest_reference()->value()==3);
        } else {
            auto consumed=std::move(staged).publish();REQUIRE(consumed);REQUIRE(consumed.state);
            REQUIRE(staged.insert(three));CHECK(staged.candidate_snapshot_count()==1);
        }
        CHECK(prior.state->snapshot_count()==2);CHECK(borrowed->reference().value()==1);
        REQUIRE(borrowed->entities().size()==1);
    }
}

TEST_CASE("B080 consuming disposable history preserves owning state and clears its source",
          "[b080-history-transfer][goldsrc][entity][snapshot][history][ownership]")
{
    auto data=make_history_fixture();auto limits=goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_history=2;
    auto builder=history_builder(limits);
    for(const auto ordinal:{1U,2U,3U})REQUIRE(builder.insert(make_full(data.baselines,ordinal)));
    REQUIRE(builder.retain_required_base(goldsrc::EntitySnapshotReference::synthetic(2)));
    const auto copied=builder.publish();REQUIRE(copied);REQUIRE(copied.state);
    auto consumed=std::move(builder).publish();REQUIRE(consumed);REQUIRE(consumed.state);
    const auto& expected=*copied.state;const auto& actual=*consumed.state;
    CHECK(actual.snapshot_count()==expected.snapshot_count());
    CHECK(actual.accounted_value_bytes()==expected.accounted_value_bytes());
    CHECK(actual.oldest_reference()==expected.oldest_reference());CHECK(actual.newest_reference()==expected.newest_reference());
    CHECK(actual.evicted_through()==expected.evicted_through());CHECK(actual.source_generation()==expected.source_generation());
    CHECK(actual.compatibility_profile()==expected.compatibility_profile());
    REQUIRE(actual.required_base_references().size()==1);
    CHECK(actual.required_base_references().front()==expected.required_base_references().front());
    REQUIRE(actual.snapshots().size()==2);
    for(std::size_t index=0;index<actual.snapshots().size();++index) {
        const auto& a=actual.snapshots()[index];const auto& b=expected.snapshots()[index];
        CHECK(a.reference()==b.reference());CHECK(a.server_time().raw_value()==b.server_time().raw_value());
        CHECK(a.kind()==b.kind());CHECK(a.base_reference()==b.base_reference());
        REQUIRE(a.entities().size()==1);REQUIRE(b.entities().size()==1);
        CHECK(a.entities().front().entity_number()==b.entities().front().entity_number());
        CHECK(a.entities().front().shares_object_with(b.entities().front()));
        CHECK(a.entities().front().object().find_exact("neutral_value")->value()==
              b.entities().front().object().find_exact("neutral_value")->value());
    }
    // Red before the consuming overload: std::move formerly selected const
    // publication and left every owning snapshot/vector in the discarded owner.
    CHECK(builder.candidate_snapshot_count()==0);
    const auto empty=builder.publish();REQUIRE(empty);REQUIRE(empty.state);
    CHECK(empty.state->snapshot_count()==0);CHECK(empty.state->accounted_value_bytes()==0);
    CHECK(empty.state->required_base_references().empty());CHECK_FALSE(empty.state->evicted_through());
    REQUIRE(builder.insert(make_full(data.baselines,4)));
    CHECK(actual.snapshot_count()==2);CHECK(actual.newest_reference()->value()==3);
}

static_assert(
    goldsrc::kDefaultMaximumEntitySnapshotHistory == 64U);
static_assert(goldsrc::kMaximumEntitySnapshotHistory == 256U);
static_assert(
    std::is_copy_constructible_v<goldsrc::EntitySnapshotHistoryState>);
static_assert(
    !std::is_copy_assignable_v<goldsrc::EntitySnapshotHistoryState>);

} // namespace
