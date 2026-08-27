#include "goldsrc_usercmd_test_fixture.hpp"

#include <hlclient/goldsrc/usercmd_schema_binding.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::usercmd_fixture;

[[nodiscard]] goldsrc::DeltaFieldBaseType expected_base_type(
    const std::uint32_t type_flags)
{
    return static_cast<goldsrc::DeltaFieldBaseType>(type_flags & 0xffU);
}

constexpr std::array<goldsrc::GoldSrcUserCmdControlledEvidenceScenario, 15U>
    kExactControlledScenarios{{
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::timing_lerp_msec,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::timing_command_msec,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::view_yaw,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::view_pitch,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::button_mask,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::movement_forward,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::
            auxiliary_light_level,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::movement_side,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::movement_up,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::auxiliary_impulse,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::view_roll_policy,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::
            auxiliary_impact_index,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::
            auxiliary_impact_position_x,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::
            auxiliary_impact_position_y,
        goldsrc::GoldSrcUserCmdControlledEvidenceScenario::
            auxiliary_impact_position_z,
    }};

TEST_CASE("Usercmd schema binding covers the exact independent fifteen fields",
          "[goldsrc][usercmd][schema-binding]")
{
    auto registry = fixture::exact_registry();
    const auto* schema = registry.find_exact("usercmd_t");
    REQUIRE(schema != nullptr);
    REQUIRE(schema->field_count() == fixture::kExactFields.size());

    auto bound = goldsrc::bind_goldsrc_usercmd_schema(registry);
    REQUIRE(bound);
    REQUIRE(bound.binding);
    CHECK(bound.error == std::nullopt);
    CHECK(bound.binding->profile() ==
          goldsrc::GoldSrcUserCmdSchemaBindingProfile::
              synthetic_usercmd_schema_v1);
    REQUIRE(bound.binding->entries().size() == fixture::kExactFields.size());

    for (std::size_t index = 0U; index < fixture::kExactFields.size(); ++index) {
        const auto& independent = fixture::kExactFields[index];
        const auto& parsed = schema->fields()[index];
        const auto& entry = bound.binding->entries()[index];
        INFO("field index " << index);
        CHECK(parsed.wire_index() == index);
        CHECK(parsed.name() == independent.name);
        CHECK(parsed.type_flags().wire_value() == independent.type);
        CHECK(parsed.type_flags().base_type() ==
              expected_base_type(independent.type));
        CHECK(parsed.type_flags().signed_value() ==
              ((independent.type & goldsrc::kDeltaSignedModifier) != 0U));
        CHECK(parsed.offset() == independent.offset);
        CHECK(parsed.storage_size() == 1U);
        CHECK(parsed.significant_bits() == independent.significant_bits);
        CHECK(parsed.premultiply_wire_value() == independent.premultiply);
        CHECK(parsed.postmultiply_wire_value() == independent.postmultiply);

        CHECK(entry.wire_index == index);
        CHECK(entry.exact_name == independent.name);
        CHECK(entry.base_type == expected_base_type(independent.type));
        CHECK(entry.signed_value ==
              ((independent.type & goldsrc::kDeltaSignedModifier) != 0U));
        CHECK(entry.significant_bits == independent.significant_bits);
        CHECK(entry.premultiply_wire_value == independent.premultiply);
        CHECK(entry.postmultiply_wire_value == independent.postmultiply);
        CHECK(entry.description_offset == independent.offset);
        CHECK(entry.description_presence_mask ==
              (independent.offset == 0U ? 0x7bU : 0x7fU));
        CHECK(entry.semantic_field == fixture::kExactSemantics[index]);
        CHECK(entry.controlled_evidence_scenario ==
              kExactControlledScenarios[index]);
        CHECK(entry.evidence_confidence ==
              goldsrc::GoldSrcUserCmdFieldEvidenceConfidence::
                  accepted_descriptor_metadata_stock_runtime_pending);
        CHECK(entry.encode_support ==
              goldsrc::GoldSrcUserCmdFieldCodecSupport::synthetic_only);
        CHECK(entry.decode_support ==
              goldsrc::GoldSrcUserCmdFieldCodecSupport::synthetic_only);
    }

    CHECK(std::ranges::none_of(schema->fields(), [](const auto& field) {
        return field.name() == "weaponselect" ||
            field.name() == "weapon_select";
    }));
}

TEST_CASE("Synthetic usercmd schema factory reproduces one exact bounded schema",
          "[goldsrc][usercmd][schema-binding][factory]")
{
    auto result = goldsrc::make_synthetic_usercmd_schema_registry();
    REQUIRE(result);
    REQUIRE(result.registry);
    CHECK(result.error == std::nullopt);
    CHECK(result.registry->schema_count() == 1U);
    CHECK(result.registry->total_field_count() == 15U);

    const auto* schema = result.registry->find_exact("usercmd_t");
    REQUIRE(schema != nullptr);
    CHECK(schema->field_count() == 15U);
    CHECK(schema->message_bits() == 3'632U);
    CHECK(schema->message_bytes() == 454U);
    auto bound = goldsrc::bind_goldsrc_usercmd_schema(*result.registry);
    REQUIRE(bound);
    REQUIRE(bound.binding);

    // Binding owns a schema copy and remains valid after the registry dies.
    auto owning_binding = [&] {
        auto temporary = fixture::exact_registry();
        auto temporary_result = goldsrc::bind_goldsrc_usercmd_schema(temporary);
        REQUIRE(temporary_result);
        REQUIRE(temporary_result.binding);
        return std::move(*temporary_result.binding);
    }();
    CHECK(owning_binding.schema().name() == "usercmd_t");
    CHECK(owning_binding.schema().field_count() == 15U);
}

TEST_CASE("Usercmd binding rejects every structural descriptor mismatch",
          "[goldsrc][usercmd][schema-binding][malformed]")
{
    SECTION("missing exact schema")
    {
        auto registry = fixture::registry_from_fields(
            fixture::kExactFields, "candidate_t");
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcUserCmdSchemaBindingErrorCode::schema_not_found);
    }

    SECTION("field count")
    {
        auto registry = fixture::registry_from_fields(
            std::span{fixture::kExactFields}.first(14U));
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::
              GoldSrcUserCmdSchemaBindingErrorCode::field_count_mismatch);
    }

    SECTION("wire order and exact name")
    {
        auto fields = fixture::kExactFields;
        std::swap(fields[0U], fields[1U]);
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::
              GoldSrcUserCmdSchemaBindingErrorCode::field_definition_mismatch);
        CHECK(result.error->field_index == 0U);
    }

    SECTION("base type")
    {
        auto fields = fixture::kExactFields;
        fields[0U].type = 0x0000'0008U;
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->field_index == 0U);
    }

    SECTION("signed modifier")
    {
        auto fields = fixture::kExactFields;
        fields[5U].type = 0x0000'0004U;
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->field_index == 5U);
    }

    SECTION("description offset")
    {
        auto fields = fixture::kExactFields;
        fields[5U].offset = 18U;
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::
              GoldSrcUserCmdSchemaBindingErrorCode::field_definition_mismatch);
        CHECK(result.error->field_index == 5U);
    }

    SECTION("significant bits")
    {
        auto fields = fixture::kExactFields;
        fields[14U].significant_bits = 15U;
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->field_index == 14U);
    }

    SECTION("premultiply")
    {
        auto fields = fixture::kExactFields;
        fields[12U].premultiply = 31'999U;
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->field_index == 12U);
    }

    SECTION("postmultiply")
    {
        auto fields = fixture::kExactFields;
        fields[7U].postmultiply = 4'001U;
        auto registry = fixture::registry_from_fields(fields);
        const auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->field_index == 7U);
    }
}

TEST_CASE("Stock usercmd binding profiles fail before exact-name lookup",
          "[goldsrc][usercmd][schema-binding][evidence]")
{
    goldsrc::DeltaSchemaRegistryBuilder builder;
    auto empty_registry = std::move(builder).publish();

    for (const auto profile : {
             goldsrc::GoldSrcUserCmdSchemaBindingProfile::
                 stock_protocol_48_build_10210_schema_only,
             goldsrc::GoldSrcUserCmdSchemaBindingProfile::
                 stock_protocol_48_evidence_pending}) {
        const auto result =
            goldsrc::bind_goldsrc_usercmd_schema(empty_registry, profile);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::
              GoldSrcUserCmdSchemaBindingErrorCode::stock_evidence_pending);
        CHECK(result.error->field_index == std::nullopt);
    }

    const auto invalid = goldsrc::bind_goldsrc_usercmd_schema(
        empty_registry,
        static_cast<goldsrc::GoldSrcUserCmdSchemaBindingProfile>(0xffU));
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
          goldsrc::GoldSrcUserCmdSchemaBindingErrorCode::invalid_profile);
}

} // namespace
