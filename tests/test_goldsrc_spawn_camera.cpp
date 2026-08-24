#include <hlclient/goldsrc/brush_models/goldsrc_spawn_camera.hpp>

#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace bsp = hlclient::goldsrc::bsp;

constexpr float kMargin = 1.0e-5F;

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] bsp::GoldSrcEntityDocumentParseResult parse_entities(
    const std::string_view text)
{
    return bsp::GoldSrcEntityDocumentParser::parse(bytes_of(text));
}

void check_vector(
    const assets::AssetVector3& actual,
    const assets::AssetVector3& expected)
{
    CHECK(actual.x == Catch::Approx(expected.x).margin(kMargin));
    CHECK(actual.y == Catch::Approx(expected.y).margin(kMargin));
    CHECK(actual.z == Catch::Approx(expected.z).margin(kMargin));
}

TEST_CASE("Spawn camera prioritizes first valid single-player start globally",
    "[goldsrc-spawn-camera][priority]")
{
    const auto document = parse_entities(
        R"({"classname" "info_player_deathmatch" "origin" "1 2 3" "angle" "10"})"
        R"({"classname" "info_player_start" "origin" "4 5 6" "angle" "20"})"
        R"({"classname" "info_player_start" "origin" "7 8 9" "angle" "30"})");
    REQUIRE(document);
    const auto extracted =
        brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
    REQUIRE(extracted);
    REQUIRE(extracted.descriptor);
    CHECK(extracted.status == brush::GoldSrcSpawnCameraExtractionStatus::selected);
    CHECK(extracted.descriptor->source_class ==
        brush::GoldSrcSpawnCameraSourceClass::info_player_start);
    CHECK(extracted.descriptor->source_entity_ordinal == 1U);
    CHECK(extracted.descriptor->status ==
        brush::GoldSrcSpawnCameraDescriptorStatus::
            supported_diagnostic_initial_pose);
    CHECK(brush::to_string(extracted.descriptor->status) ==
        "supported_diagnostic_initial_pose");
    check_vector(extracted.descriptor->position, {4.0F, 5.0F, 6.0F});
    CHECK(extracted.statistics.source_entity_count == 3U);
    CHECK(extracted.statistics.supported_class_candidate_count == 3U);
}

TEST_CASE("Deathmatch spawn is used only when no valid player-start exists",
    "[goldsrc-spawn-camera][priority][fallback]")
{
    SECTION("no player start")
    {
        const auto document = parse_entities(
            R"({"classname" "worldspawn"}{"classname" "info_player_deathmatch" "origin" "8 9 10"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        CHECK(extracted.descriptor->source_class ==
            brush::GoldSrcSpawnCameraSourceClass::info_player_deathmatch);
        CHECK(extracted.descriptor->source_entity_ordinal == 1U);
    }
    SECTION("all player starts are invalid")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start"})"
            R"({"classname" "info_player_start" "origin" "bad"})"
            R"({"classname" "info_player_deathmatch" "origin" "1 2 3"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        CHECK(extracted.descriptor->source_class ==
            brush::GoldSrcSpawnCameraSourceClass::info_player_deathmatch);
        CHECK(extracted.descriptor->source_entity_ordinal == 2U);
        CHECK(extracted.statistics.skipped_invalid_transform_count == 2U);
    }
}

TEST_CASE("Invalid candidate is skipped and source-order scan continues",
    "[goldsrc-spawn-camera][invalid]")
{
    const auto document = parse_entities(
        R"({"classname" "info_player_start" "origin" "1 2"})"
        R"({"classname" "info_player_start" "origin" "10 20 30"})");
    REQUIRE(document);
    const auto extracted =
        brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
    REQUIRE(extracted);
    CHECK(extracted.descriptor->source_entity_ordinal == 1U);
    check_vector(extracted.descriptor->position, {10.0F, 20.0F, 30.0F});
    CHECK(extracted.statistics.skipped_invalid_transform_count == 1U);
}

TEST_CASE("Ambiguous interpreted spawn keys never use last value wins",
    "[goldsrc-spawn-camera][duplicates]")
{
    SECTION("exact duplicate classname")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "classname" "info_player_start" "origin" "1 2 3"})"
            R"({"classname" "info_player_start" "origin" "4 5 6"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        CHECK(extracted.descriptor->source_entity_ordinal == 1U);
        CHECK(extracted.statistics.skipped_ambiguous_metadata_count == 1U);
    }
    SECTION("ASCII-case origin collision")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3" "Origin" "4 5 6"})"
            R"({"classname" "info_player_deathmatch" "origin" "7 8 9"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        CHECK(extracted.descriptor->source_class ==
            brush::GoldSrcSpawnCameraSourceClass::info_player_deathmatch);
        CHECK(extracted.statistics.skipped_ambiguous_metadata_count == 1U);
    }
    SECTION("duplicate angles and angle keys are both ambiguous")
    {
        auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3" "angles" "0 0 0" "angles" "0 90 0"})");
        REQUIRE(document);
        auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE_FALSE(extracted);

        document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3" "Angle" "0" "angle" "90"})");
        REQUIRE(document);
        extracted = brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE_FALSE(extracted);
    }
    SECTION("unknown duplicates remain inert")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3" "targetname" "a" "targetname" "b"})");
        REQUIRE(document);
        REQUIRE(brush::GoldSrcSpawnCameraExtractor::extract(*document.document));
    }
}

TEST_CASE("Spawn camera uses strict origin and angles priority",
    "[goldsrc-spawn-camera][transform]")
{
    SECTION("angles overrides even a malformed angle value")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "10 20 30" "angles" "0 90 0" "angle" "not-used"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        check_vector(extracted.descriptor->angles_degrees, {0.0F, 90.0F, 0.0F});
        check_vector(extracted.descriptor->forward, {0.0F, 1.0F, 0.0F});
        check_vector(extracted.descriptor->target, {10.0F, 21.0F, 30.0F});
        check_vector(extracted.descriptor->up, {0.0F, 0.0F, 1.0F});
    }
    SECTION("malformed angles does not fall back to angle")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3" "angles" "bad" "angle" "90"})"
            R"({"classname" "info_player_deathmatch" "origin" "4 5 6"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        CHECK(extracted.descriptor->source_class ==
            brush::GoldSrcSpawnCameraSourceClass::info_player_deathmatch);
    }
    SECTION("absent angles is exact zero rotation")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        check_vector(extracted.descriptor->angles_degrees, {});
        check_vector(extracted.descriptor->forward, {1.0F, 0.0F, 0.0F});
        check_vector(extracted.descriptor->target, {2.0F, 2.0F, 3.0F});
        check_vector(extracted.descriptor->up, {0.0F, 0.0F, 1.0F});
    }
    SECTION("ANGLE_UP shorthand derives an orthogonal camera basis")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "1 2 3" "angle" "-1"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        check_vector(extracted.descriptor->angles_degrees, {-90.0F, 0.0F, 0.0F});
        check_vector(extracted.descriptor->forward, {0.0F, 0.0F, 1.0F});
        check_vector(extracted.descriptor->target, {1.0F, 2.0F, 4.0F});
        check_vector(extracted.descriptor->up, {-1.0F, 0.0F, 0.0F});
    }
}

TEST_CASE("Unsupported or malformed spawn documents yield caller fallback",
    "[goldsrc-spawn-camera][none]")
{
    SECTION("no supported classname")
    {
        const auto document = parse_entities(
            R"({"classname" "worldspawn"}{"classname" "INFO_PLAYER_START" "origin" "1 2 3"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE_FALSE(extracted);
        CHECK(extracted.status ==
            brush::GoldSrcSpawnCameraExtractionStatus::
                no_valid_supported_candidate);
    }
    SECTION("non-finite and comma origins are invalid")
    {
        const auto document = parse_entities(
            R"({"classname" "info_player_start" "origin" "nan 0 0"})"
            R"({"classname" "info_player_deathmatch" "origin" "1,2,3"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE_FALSE(extracted);
        CHECK(extracted.statistics.skipped_invalid_transform_count == 2U);
    }
    SECTION("single ASCII-case variants of interpreted keys are accepted")
    {
        const auto document = parse_entities(
            R"({"CLASSNAME" "info_player_start" "ORIGIN" "1 2 3" "ANGLE" "90"})");
        REQUIRE(document);
        const auto extracted =
            brush::GoldSrcSpawnCameraExtractor::extract(*document.document);
        REQUIRE(extracted);
        check_vector(extracted.descriptor->forward, {0.0F, 1.0F, 0.0F});
    }
}

} // namespace
