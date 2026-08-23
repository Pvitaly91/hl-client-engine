#include <hlclient/goldsrc/local_resource_mapping.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

namespace goldsrc = hlclient::goldsrc;

void check_file_mapping(
    const goldsrc::ResourceType type,
    const std::string_view wire_name,
    const std::string_view expected_virtual_name)
{
    const auto mapped =
        goldsrc::GoldSrcResourceNameMapper{}.classify(type, wire_name);
    CHECK(mapped.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::mapped_file);
    CHECK(mapped.issue() == goldsrc::GoldSrcResourceNameIssue::none);
    CHECK(mapped.original_name_byte_length() == wire_name.size());
    REQUIRE(mapped.safe_virtual_name());
    CHECK(mapped.safe_virtual_name()->value() == expected_virtual_name);
    CHECK(mapped.file_backed());
}

TEST_CASE("GoldSrc local resource mapper uses only evidence-backed type mappings",
          "[goldsrc][local-resource][mapping]")
{
    check_file_mapping(
        goldsrc::ResourceType::sound,
        "weapons/357_shot1.wav",
        "sound/weapons/357_shot1.wav");
    check_file_mapping(
        goldsrc::ResourceType::model,
        "models/barney.mdl",
        "models/barney.mdl");
    check_file_mapping(
        goldsrc::ResourceType::generic,
        "sprites/muzzleflash.spr",
        "sprites/muzzleflash.spr");
    check_file_mapping(
        goldsrc::ResourceType::event_script,
        "events/glock1.sc",
        "events/glock1.sc");
}

TEST_CASE("GoldSrc model mapping preserves the map virtual path",
          "[goldsrc][local-resource][mapping][map]")
{
    check_file_mapping(
        goldsrc::ResourceType::model,
        "maps/crossfire.bsp",
        "maps/crossfire.bsp");
}

TEST_CASE("GoldSrc decal mapping remains metadata-only",
          "[goldsrc][local-resource][mapping][decal]")
{
    const auto result = goldsrc::GoldSrcResourceNameMapper{}.classify(
        goldsrc::ResourceType::decal,
        "{lambda");

    CHECK(result.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::metadata_only);
    CHECK(result.issue() == goldsrc::GoldSrcResourceNameIssue::none);
    CHECK(result.original_name_byte_length() == 7U);
    CHECK_FALSE(result.safe_virtual_name());
    CHECK_FALSE(result.file_backed());
}

TEST_CASE("GoldSrc local resource mapper preserves ASCII case",
          "[goldsrc][local-resource][mapping][case]")
{
    check_file_mapping(
        goldsrc::ResourceType::model,
        "Models/Player/Gordon.MDL",
        "Models/Player/Gordon.MDL");
    check_file_mapping(
        goldsrc::ResourceType::sound,
        "Weapons/357_Shot1.WAV",
        "sound/Weapons/357_Shot1.WAV");
}

TEST_CASE("GoldSrc local resource mapper rejects unsafe names before mapping",
          "[goldsrc][local-resource][mapping][unsafe]")
{
    const auto result = goldsrc::GoldSrcResourceNameMapper{}.classify(
        goldsrc::ResourceType::generic,
        "../outside.wad");

    CHECK(result.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::unsafe_name);
    CHECK(result.issue() == goldsrc::GoldSrcResourceNameIssue::parent_component);
    CHECK_FALSE(result.safe_virtual_name());
}

TEST_CASE("GoldSrc local resource mapper reports unsupported types explicitly",
          "[goldsrc][local-resource][mapping][unsupported]")
{
    const auto result = goldsrc::GoldSrcResourceNameMapper{}.classify(
        static_cast<goldsrc::ResourceType>(1U),
        "models/player.mdl");

    CHECK(result.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::unsupported_mapping);
    CHECK(result.issue() ==
          goldsrc::GoldSrcResourceNameIssue::unsupported_resource_type);
    CHECK(result.original_name_byte_length() == 17U);
    CHECK_FALSE(result.safe_virtual_name());
}

TEST_CASE("GoldSrc local resource mapper does not infer type from extension",
          "[goldsrc][local-resource][mapping][type]")
{
    check_file_mapping(
        goldsrc::ResourceType::sound,
        "models/not-a-model.mdl",
        "sound/models/not-a-model.mdl");
    check_file_mapping(
        goldsrc::ResourceType::model,
        "weapons/not-a-sound.wav",
        "weapons/not-a-sound.wav");
    check_file_mapping(
        goldsrc::ResourceType::generic,
        "events/not-an-event.sc",
        "events/not-an-event.sc");

    const auto decal_with_file_extension =
        goldsrc::GoldSrcResourceNameMapper{}.classify(
            goldsrc::ResourceType::decal,
            "models/still-metadata.mdl");
    CHECK(decal_with_file_extension.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::metadata_only);
    CHECK_FALSE(decal_with_file_extension.safe_virtual_name());
}

TEST_CASE("GoldSrc sound prefix is subject to the mapped-name bound",
          "[goldsrc][local-resource][mapping][limits]")
{
    const goldsrc::GoldSrcResourceNameMapper mapper{
        goldsrc::GoldSrcLocalResourceMappingProfile::
            stock_protocol_48_standard,
        {5U, 10U},
    };

    const auto result = mapper.classify(goldsrc::ResourceType::sound, "abcde");
    CHECK(result.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::unsafe_name);
    CHECK(result.issue() == goldsrc::GoldSrcResourceNameIssue::path_too_long);
    CHECK(result.original_name_byte_length() == 5U);
    CHECK_FALSE(result.safe_virtual_name());
}

TEST_CASE("GoldSrc local resource mapper exposes its bounded evidence profile",
          "[goldsrc][local-resource][mapping][profile]")
{
    const goldsrc::GoldSrcResourceNameMapper mapper;
    CHECK(mapper.profile() ==
          goldsrc::GoldSrcLocalResourceMappingProfile::
              stock_protocol_48_standard);
    CHECK(mapper.evidence_profile() ==
          goldsrc::GoldSrcLocalResourceMappingEvidenceProfile::
              repeated_stock_names_installation_layout_and_valve_header_cross_check);
    CHECK(mapper.limits().maximum_component_bytes ==
          goldsrc::kDefaultMaximumLocalResourceComponentBytes);
    CHECK(mapper.limits().maximum_virtual_name_bytes ==
          goldsrc::kDefaultMaximumLocalResourceVirtualNameBytes);
    CHECK(mapper.limits().maximum_components ==
          goldsrc::kDefaultMaximumLocalResourceComponents);

    const goldsrc::GoldSrcResourceNameMapper unsupported_profile{
        static_cast<goldsrc::GoldSrcLocalResourceMappingProfile>(255U)};
    const auto result = unsupported_profile.classify(
        goldsrc::ResourceType::model,
        "models/barney.mdl");
    CHECK(result.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::unsupported_mapping);
    CHECK(result.issue() == goldsrc::GoldSrcResourceNameIssue::unsupported_profile);
    CHECK_FALSE(result.safe_virtual_name());
}

} // namespace
