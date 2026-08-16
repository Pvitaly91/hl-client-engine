#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/delta_description.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::DeltaSchema parsed_schema(
    const std::string& name,
    const std::span<const fixture::Field> fields)
{
    const auto encoded = fixture::schema(name, fields);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    return std::move(*parsed.schema);
}

TEST_CASE("Delta schema registry preserves order and exact case-sensitive lookup",
          "[goldsrc][delta][registry]")
{
    auto alpha = parsed_schema("alpha_t", fixture::kSchemaAlphaFields);
    auto bravo = parsed_schema("bravo_t", fixture::kSchemaBravoFields);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(alpha));
    REQUIRE(builder.insert(bravo));
    auto registry = std::move(builder).publish();

    REQUIRE(registry.schema_count() == 2U);
    CHECK(registry.schemas()[0U].name() == "alpha_t");
    CHECK(registry.schemas()[1U].name() == "bravo_t");
    REQUIRE(registry.schemas()[0U].field_count() == 2U);
    CHECK(registry.schemas()[0U].fields()[0U].name() == "alpha");
    CHECK(registry.schemas()[0U].fields()[1U].name() == "origin[0]");
    CHECK(registry.find_exact("alpha_t") == &registry.schemas()[0U]);
    CHECK(registry.find_exact("ALPHA_T") == nullptr);
    CHECK(registry.find_exact("missing_t") == nullptr);
}

TEST_CASE("Delta schema registry enforces schema count transactionally",
          "[goldsrc][delta][registry][limit]")
{
    auto limits = goldsrc::DeltaDescriptionLimits{};
    limits.maximum_schema_count = 1U;
    goldsrc::DeltaSchemaRegistryBuilder builder{limits};
    auto alpha = parsed_schema("alpha_t", fixture::kSchemaAlphaFields);
    auto bravo = parsed_schema("bravo_t", fixture::kSchemaBravoFields);
    REQUIRE(builder.insert(alpha));
    const auto rejected = builder.insert(bravo);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::DeltaRegistryErrorCode::schema_count_limit_exceeded);
    REQUIRE(builder.candidate_schemas().size() == 1U);
    CHECK(builder.candidate_schemas().front().name() == "alpha_t");
}

TEST_CASE("Delta schema registry enforces total name and accounted byte bounds",
          "[goldsrc][delta][registry][limit]")
{
    auto alpha = parsed_schema("alpha_t", fixture::kSchemaAlphaFields);

    auto name_limits = goldsrc::DeltaDescriptionLimits{};
    name_limits.maximum_total_name_bytes = 1U;
    goldsrc::DeltaSchemaRegistryBuilder name_builder{name_limits};
    const auto names = name_builder.insert(alpha);
    REQUIRE_FALSE(names);
    REQUIRE(names.error);
    CHECK(names.error->code ==
          goldsrc::DeltaRegistryErrorCode::total_name_byte_limit_exceeded);
    CHECK(name_builder.candidate_schemas().empty());

    auto byte_limits = goldsrc::DeltaDescriptionLimits{};
    byte_limits.maximum_registry_bytes = 1U;
    goldsrc::DeltaSchemaRegistryBuilder byte_builder{byte_limits};
    const auto bytes = byte_builder.insert(alpha);
    REQUIRE_FALSE(bytes);
    REQUIRE(bytes.error);
    CHECK(bytes.error->code ==
          goldsrc::DeltaRegistryErrorCode::registry_byte_limit_exceeded);
    CHECK(byte_builder.candidate_schemas().empty());
}

TEST_CASE("Published delta registry remains owning after source schemas die",
          "[goldsrc][delta][registry][ownership]")
{
    auto registry = [] {
        auto alpha = parsed_schema("owned_t", fixture::kSchemaAlphaFields);
        goldsrc::DeltaSchemaRegistryBuilder builder;
        REQUIRE(builder.insert(alpha));
        return std::move(builder).publish();
    }();
    REQUIRE(registry.find_exact("owned_t") != nullptr);
    CHECK(registry.find_exact("owned_t")->fields()[1U].name() == "origin[0]");
}

} // namespace
