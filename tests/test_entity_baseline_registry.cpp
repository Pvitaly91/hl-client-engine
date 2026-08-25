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

struct ThreeSchemaFixture {
    goldsrc::DeltaSchemaRegistryState registry;
    goldsrc::DeltaObjectState ordinary;
    goldsrc::DeltaObjectState player;
    goldsrc::DeltaObjectState custom;
};

[[nodiscard]] ThreeSchemaFixture make_three_schema_fixture()
{
    auto ordinary_schema = parse_schema("ordinary_t", kOrdinaryFields);
    auto player_schema = parse_schema("player_t", kPlayerFields);
    auto custom_schema = parse_schema("custom_t", kCustomFields);
    auto ordinary = make_object(ordinary_schema, std::uint32_t{17U});
    auto player = make_object(player_schema, std::uint32_t{311U});
    auto custom = make_object(custom_schema, std::uint32_t{701U});
    goldsrc::DeltaSchemaRegistryBuilder schemas;
    REQUIRE(schemas.insert(ordinary_schema));
    REQUIRE(schemas.insert(player_schema));
    REQUIRE(schemas.insert(custom_schema));
    return ThreeSchemaFixture{
        std::move(schemas).publish(),
        std::move(ordinary),
        std::move(player),
        std::move(custom),
    };
}

[[nodiscard]] goldsrc::EntityBaselineRegistryBuilder neutral_builder(
    const goldsrc::DeltaSchemaRegistryState& schemas,
    goldsrc::EntitySnapshotLimits limits = {})
{
    return goldsrc::EntityBaselineRegistryBuilder{
        schemas,
        limits,
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
}

TEST_CASE("Entity baseline registry owns one exact typed baseline",
          "[goldsrc][entity][baseline][neutral]")
{
    auto fixture_state = make_three_schema_fixture();
    auto builder = neutral_builder(fixture_state.registry);
    const goldsrc::EntitySourceGeometry geometry{3U, 5U, 8U, 17U};
    REQUIRE(builder.insert(
        goldsrc::EntityBaselineKey::for_entity(7U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary,
        geometry));
    auto published = std::move(builder).publish();
    REQUIRE(published);
    REQUIRE(published.state);

    const auto& registry = *published.state;
    REQUIRE(registry.baseline_count() == 1U);
    const auto* baseline = registry.find_exact(
        goldsrc::EntityBaselineKey::for_entity(7U));
    REQUIRE(baseline != nullptr);
    CHECK(baseline->schema_category() ==
          goldsrc::EntitySchemaCategory::ordinary_entity);
    CHECK(baseline->schema_name() == "ordinary_t");
    CHECK(baseline->source_geometry().message_ordinal == 3U);
    CHECK(baseline->source_geometry().bits_consumed == 17U);
    CHECK(baseline->compatibility_profile() ==
          goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1);
    CHECK(baseline->evidence_profile() ==
          goldsrc::EntitySnapshotEvidenceProfile::
              caller_supplied_typed_records);
    CHECK_FALSE(baseline->semantic_projection());
    REQUIRE(baseline->object().find_exact("neutral_value") != nullptr);
    CHECK(std::get<std::uint32_t>(
              baseline->object().find_exact("neutral_value")->value()) ==
          17U);
}

TEST_CASE("Baseline registry preserves explicit ordinary player custom selection",
          "[goldsrc][entity][baseline][schema-selection]")
{
    auto fixture_state = make_three_schema_fixture();
    auto builder = neutral_builder(fixture_state.registry);
    REQUIRE(builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary));
    REQUIRE(builder.insert(
        goldsrc::EntityBaselineKey::for_entity(2U),
        goldsrc::EntitySchemaCategory::player_entity,
        fixture_state.player));
    REQUIRE(builder.insert(
        goldsrc::EntityBaselineKey::for_alternate_slot(4U),
        goldsrc::EntitySchemaCategory::custom_entity,
        fixture_state.custom));
    auto published = std::move(builder).publish();
    REQUIRE(published);
    REQUIRE(published.state);
    REQUIRE(published.state->baseline_count() == 3U);
    CHECK(published.state->baselines()[0U].schema_name() == "ordinary_t");
    CHECK(published.state->baselines()[1U].schema_name() == "player_t");
    CHECK(published.state->baselines()[1U].schema_category() ==
          goldsrc::EntitySchemaCategory::player_entity);
    CHECK(published.state->baselines()[2U].key().kind() ==
          goldsrc::EntityBaselineKeyKind::alternate_slot);
    CHECK(published.state->baselines()[2U].schema_category() ==
          goldsrc::EntitySchemaCategory::custom_entity);
}

TEST_CASE("Duplicate baseline identity fails without partial insertion",
          "[goldsrc][entity][baseline][transactional]")
{
    auto fixture_state = make_three_schema_fixture();
    auto builder = neutral_builder(fixture_state.registry);
    const auto key = goldsrc::EntityBaselineKey::for_entity(9U);
    REQUIRE(builder.insert(
        key,
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary));
    const auto duplicate = builder.insert(
        key,
        goldsrc::EntitySchemaCategory::player_entity,
        fixture_state.player);
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
          goldsrc::EntityBaselineErrorCode::duplicate_baseline_identity);
    REQUIRE(builder.candidate_baselines().size() == 1U);
    CHECK(builder.candidate_baselines().front().schema_name() == "ordinary_t");
}

TEST_CASE("Baseline schema must exist in the exact supplied registry",
          "[goldsrc][entity][baseline][unsupported-schema]")
{
    auto registered_schema = parse_schema("registered_t", kOrdinaryFields);
    auto foreign_schema = parse_schema("foreign_t", kOrdinaryFields);
    auto foreign_object = make_object(foreign_schema, std::uint32_t{4U});
    goldsrc::DeltaSchemaRegistryBuilder schemas;
    REQUIRE(schemas.insert(registered_schema));
    auto registry = std::move(schemas).publish();
    auto builder = neutral_builder(registry);
    const auto rejected = builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        foreign_object);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::EntityBaselineErrorCode::unsupported_schema);
    CHECK(builder.candidate_baselines().empty());
}

TEST_CASE("Baseline object must match the exact registry schema descriptor",
          "[goldsrc][entity][baseline][schema-identity]")
{
    auto registered_schema =
        parse_schema("ordinary_t", kOrdinaryFields);
    auto foreign_schema =
        parse_schema("ordinary_t", kOrdinaryNarrowFields);
    auto foreign_object = make_object(foreign_schema, std::uint32_t{17U});
    goldsrc::DeltaSchemaRegistryBuilder schemas;
    REQUIRE(schemas.insert(registered_schema));
    auto registry = std::move(schemas).publish();
    auto builder = neutral_builder(registry);

    const auto rejected = builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        foreign_object);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::EntityBaselineErrorCode::unsupported_schema);
    CHECK(builder.candidate_baselines().empty());
}

TEST_CASE("Baseline key and registry limits accept exact limit and reject limit plus one",
          "[goldsrc][entity][baseline][limit]")
{
    auto fixture_state = make_three_schema_fixture();
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_entity_number = 2U;
    limits.maximum_baselines = 1U;
    auto builder = neutral_builder(fixture_state.registry, limits);
    REQUIRE(builder.insert(
        goldsrc::EntityBaselineKey::for_entity(2U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary));

    const auto count_failure = builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary);
    REQUIRE_FALSE(count_failure);
    REQUIRE(count_failure.error);
    CHECK(count_failure.error->code ==
          goldsrc::EntityBaselineErrorCode::baseline_limit_exceeded);

    limits.maximum_baselines = 2U;
    auto number_builder = neutral_builder(fixture_state.registry, limits);
    const auto number_failure = number_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(3U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary);
    REQUIRE_FALSE(number_failure);
    REQUIRE(number_failure.error);
    CHECK(number_failure.error->code ==
          goldsrc::EntityBaselineErrorCode::entity_number_limit_exceeded);
}

TEST_CASE("Baseline source geometry and total value bytes are bounded",
          "[goldsrc][entity][baseline][geometry][limit]")
{
    auto fixture_state = make_three_schema_fixture();
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_source_payload_bytes = 1U;
    auto geometry_builder = neutral_builder(fixture_state.registry, limits);
    const auto bad_geometry = geometry_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary,
        goldsrc::EntitySourceGeometry{0U, 1U, 7U, 2U});
    REQUIRE_FALSE(bad_geometry);
    REQUIRE(bad_geometry.error);
    CHECK(bad_geometry.error->code ==
          goldsrc::EntityBaselineErrorCode::invalid_source_geometry);

    limits.maximum_source_payload_bytes =
        goldsrc::kDefaultMaximumEntitySourcePayloadBytes;
    limits.maximum_snapshot_total_value_bytes =
        fixture_state.ordinary.accounted_value_bytes();
    auto byte_builder = neutral_builder(fixture_state.registry, limits);
    REQUIRE(byte_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary));
    const auto byte_failure = byte_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(2U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary);
    REQUIRE_FALSE(byte_failure);
    REQUIRE(byte_failure.error);
    CHECK(byte_failure.error->code ==
          goldsrc::EntityBaselineErrorCode::total_value_bytes_exceeded);
}

TEST_CASE("Published baseline registry remains owning after all inputs die",
          "[goldsrc][entity][baseline][ownership]")
{
    auto registry = [] {
        auto fixture_state = make_three_schema_fixture();
        auto builder = neutral_builder(fixture_state.registry);
        REQUIRE(builder.insert(
            goldsrc::EntityBaselineKey::for_entity(5U),
            goldsrc::EntitySchemaCategory::ordinary_entity,
            fixture_state.ordinary));
        auto published = std::move(builder).publish();
        REQUIRE(published);
        REQUIRE(published.state);
        return std::move(*published.state);
    }();
    const auto copy = registry;
    REQUIRE(registry.find_exact(goldsrc::EntityBaselineKey::for_entity(5U)));
    REQUIRE(copy.find_exact(goldsrc::EntityBaselineKey::for_entity(5U)));
    CHECK(registry.baselines().data() != copy.baselines().data());
    CHECK(copy.baselines().front().schema_name() == "ordinary_t");
}

TEST_CASE("Stock baseline profile remains a typed evidence-pending boundary",
          "[goldsrc][entity][baseline][evidence]")
{
    auto fixture_state = make_three_schema_fixture();
    goldsrc::EntityBaselineRegistryBuilder builder{fixture_state.registry};
    const auto inserted = builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        fixture_state.ordinary);
    REQUIRE_FALSE(inserted);
    REQUIRE(inserted.error);
    CHECK(inserted.error->code ==
          goldsrc::EntityBaselineErrorCode::evidence_pending);
    CHECK(builder.candidate_baselines().empty());
    const auto published = std::move(builder).publish();
    REQUIRE_FALSE(published);
    REQUIRE(published.error);
    CHECK(published.error->code ==
          goldsrc::EntityBaselineErrorCode::evidence_pending);
}

TEST_CASE("Entity builders reject invalid compatibility and schema-category enums",
          "[goldsrc][entity][configuration][security]")
{
    auto fixture_state = make_three_schema_fixture();
    const auto invalid_profile =
        static_cast<goldsrc::EntitySnapshotCompatibilityProfile>(0xffU);
    goldsrc::EntityBaselineRegistryBuilder baseline_builder{
        fixture_state.registry, {}, invalid_profile};
    goldsrc::EntitySnapshotHistoryBuilder history_builder{
        {}, invalid_profile};
    goldsrc::EntityFullSnapshotBuilder full_builder{{}, invalid_profile};
    goldsrc::EntityDeltaSnapshotBuilder delta_builder{{}, invalid_profile};
    CHECK_FALSE(baseline_builder.valid_configuration());
    CHECK_FALSE(history_builder.valid_configuration());
    CHECK_FALSE(full_builder.valid_configuration());
    CHECK_FALSE(delta_builder.valid_configuration());

    auto category_builder = neutral_builder(fixture_state.registry);
    const auto inserted = category_builder.insert(
        goldsrc::EntityBaselineKey::for_entity(1U),
        static_cast<goldsrc::EntitySchemaCategory>(0xffU),
        fixture_state.ordinary);
    REQUIRE_FALSE(inserted);
    REQUIRE(inserted.error);
    CHECK(inserted.error->code ==
          goldsrc::EntityBaselineErrorCode::unsupported_schema);
    CHECK(category_builder.candidate_baselines().empty());
}

TEST_CASE("Entity snapshot limit validation distinguishes defaults and hard caps",
          "[goldsrc][entity][limits]")
{
    CHECK(goldsrc::valid_entity_snapshot_limits({}));
    auto limits = goldsrc::EntitySnapshotLimits{};
    limits.maximum_snapshot_history = goldsrc::kMaximumEntitySnapshotHistory;
    limits.maximum_baselines = goldsrc::kMaximumEntityBaselines;
    limits.maximum_entities_per_snapshot =
        goldsrc::kMaximumEntitiesPerSnapshot;
    limits.maximum_entity_number = goldsrc::kMaximumEntityNumber;
    limits.maximum_fields_per_entity = goldsrc::kMaximumEntityFields;
    limits.maximum_changed_fields_per_entity =
        goldsrc::kMaximumChangedFieldsPerEntity;
    limits.maximum_snapshot_total_value_bytes =
        goldsrc::kMaximumEntitySnapshotValueBytes;
    limits.maximum_source_payload_bytes =
        goldsrc::kMaximumEntitySourcePayloadBytes;
    CHECK(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_snapshot_history =
        goldsrc::kMaximumEntitySnapshotHistory + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_snapshot_history =
        goldsrc::kMaximumEntitySnapshotHistory;

    limits.maximum_baselines = goldsrc::kMaximumEntityBaselines + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_baselines = goldsrc::kMaximumEntityBaselines;

    limits.maximum_entities_per_snapshot =
        goldsrc::kMaximumEntitiesPerSnapshot + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_entities_per_snapshot =
        goldsrc::kMaximumEntitiesPerSnapshot;

    limits.maximum_entity_number = goldsrc::kMaximumEntityNumber + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_entity_number = goldsrc::kMaximumEntityNumber;

    limits.maximum_fields_per_entity =
        goldsrc::kMaximumEntityFields + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_fields_per_entity = goldsrc::kMaximumEntityFields;

    limits.maximum_changed_fields_per_entity =
        goldsrc::kMaximumChangedFieldsPerEntity + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_changed_fields_per_entity =
        goldsrc::kMaximumChangedFieldsPerEntity;

    limits.maximum_snapshot_total_value_bytes =
        goldsrc::kMaximumEntitySnapshotValueBytes + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
    limits.maximum_snapshot_total_value_bytes =
        goldsrc::kMaximumEntitySnapshotValueBytes;

    limits.maximum_source_payload_bytes =
        goldsrc::kMaximumEntitySourcePayloadBytes + 1U;
    CHECK_FALSE(goldsrc::valid_entity_snapshot_limits(limits));
}

static_assert(std::is_copy_constructible_v<goldsrc::EntityBaselineState>);
static_assert(
    !std::is_copy_assignable_v<goldsrc::EntityBaselineRegistryState>);
static_assert(
    !std::is_default_constructible_v<goldsrc::EntityBaselineRegistryState>);

} // namespace
