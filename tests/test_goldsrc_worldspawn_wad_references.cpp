#include <hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;

[[nodiscard]] std::vector<std::byte> entity_bytes(const std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] bsp::GoldSrcWadReferenceParseResult parse_entity(
    const std::string_view text,
    const bsp::GoldSrcWorldspawnParseLimits& limits = {})
{
    return bsp::GoldSrcEntityLumpParser::parse_worldspawn_wad_references(
        entity_bytes(text), limits);
}

TEST_CASE("Minimal inert worldspawn parser applies _wad then wad precedence",
    "[goldsrc-worldspawn][entity]")
{
    SECTION("minimal worldspawn has no declarations")
    {
        const auto result = parse_entity(R"({ "classname" "worldspawn" })");
        REQUIRE(result);
        CHECK(result.references->empty());
    }
    SECTION("legacy wad key")
    {
        const auto result = parse_entity(
            R"({ "classname" "worldspawn" "wad" "halflife.wad" })");
        REQUIRE(result);
        REQUIRE(result.references->size() == 1U);
        CHECK(result.references->references()[0U].basename == "halflife.wad");
    }
    SECTION("preferred _wad key")
    {
        const auto result = parse_entity(
            R"({ "classname" "worldspawn" "_wad" "decals.wad" })");
        REQUIRE(result);
        REQUIRE(result.references->size() == 1U);
        CHECK(result.references->references()[0U].basename == "decals.wad");
    }
    SECTION("nonempty _wad wins and empty _wad falls back")
    {
        auto result = parse_entity(
            R"({ "classname" "worldspawn" "wad" "fallback.wad" "_wad" "preferred.wad" })");
        REQUIRE(result);
        CHECK(result.references->references()[0U].basename == "preferred.wad");

        result = parse_entity(
            R"({ "classname" "worldspawn" "wad" "fallback.wad" "_wad" "" })");
        REQUIRE(result);
        CHECK(result.references->references()[0U].basename == "fallback.wad");
    }
    SECTION("parser stops at validated first-entity boundary")
    {
        const auto result = parse_entity(
            R"({ "classname" "worldspawn" } second entity bytes are inert)");
        REQUIRE(result);
        CHECK(result.references->empty());
    }
}

TEST_CASE("Compiler paths yield basename-only ordered WAD declarations",
    "[goldsrc-worldspawn][wad-references][sanitization]")
{
    const auto result = parse_entity(
        R"({ "classname" "worldspawn" "_wad" " C:\tools\wad\halflife.wad ;/home/user/decals.wad;my textures.WAD;HALFLIFE.WAD;" })");
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto references = result.references->references();
    REQUIRE(references.size() == 3U);
    CHECK(references[0U].basename == "halflife.wad");
    CHECK(references[0U].normalized_basename == "HALFLIFE.WAD");
    CHECK(references[0U].declaration_ordinal == 0U);
    CHECK(references[1U].basename == "decals.wad");
    CHECK(references[1U].declaration_ordinal == 1U);
    CHECK(references[2U].basename == "my textures.WAD");
    CHECK(references[2U].declaration_ordinal == 2U);
    for (const auto& reference : references) {
        CHECK(reference.basename.find('/') == std::string::npos);
        CHECK(reference.basename.find('\\') == std::string::npos);
        CHECK(reference.basename.find(':') == std::string::npos);
    }
}

TEST_CASE("Backslash is a literal entity byte and only a source-reference separator",
    "[goldsrc-worldspawn][backslash]")
{
    const auto result = parse_entity(
        R"({ "classname" "worldspawn" "_wad" "C:\compiler\n\textures.wad" })");
    REQUIRE(result);
    REQUIRE(result.references->size() == 1U);
    CHECK(result.references->references()[0U].basename == "textures.wad");
}

TEST_CASE("Worldspawn entity grammar rejects ambiguous or truncated input",
    "[goldsrc-worldspawn][entity][mutation]")
{
    SECTION("unterminated quote")
    {
        const auto result = parse_entity(R"({ "classname" "worldspawn })");
        REQUIRE_FALSE(result);
        CHECK(result.error->code == bsp::GoldSrcWorldspawnErrorCode::unterminated_quote);
    }
    SECTION("missing closing brace")
    {
        const auto result = parse_entity(R"({ "classname" "worldspawn")");
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            bsp::GoldSrcWorldspawnErrorCode::missing_closing_brace);
    }
    SECTION("first entity is not worldspawn")
    {
        const auto result = parse_entity(R"({ "classname" "light" })");
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            bsp::GoldSrcWorldspawnErrorCode::first_entity_not_worldspawn);
    }
    SECTION("duplicate key is rejected ASCII-case-insensitively")
    {
        const auto result = parse_entity(
            R"({ "classname" "worldspawn" "WAD" "a.wad" "wad" "b.wad" })");
        REQUIRE_FALSE(result);
        CHECK(result.error->code == bsp::GoldSrcWorldspawnErrorCode::duplicate_key);
    }
    SECTION("unexpected unquoted token")
    {
        const auto result = parse_entity(R"({ classname "worldspawn" })");
        REQUIRE_FALSE(result);
        CHECK(result.error->code == bsp::GoldSrcWorldspawnErrorCode::unexpected_token);
    }
    SECTION("NUL inside value")
    {
        auto bytes = entity_bytes(R"({ "classname" "worldspawn" })");
        bytes.insert(bytes.begin() + 22, std::byte{0});
        const auto result =
            bsp::GoldSrcEntityLumpParser::parse_worldspawn_wad_references(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            bsp::GoldSrcWorldspawnErrorCode::nul_in_key_or_value);
    }
}

TEST_CASE("Worldspawn pair and value limits accept the boundary and reject plus one",
    "[goldsrc-worldspawn][limits]")
{
    SECTION("pair count")
    {
        auto limits = bsp::GoldSrcWorldspawnParseLimits{};
        limits.maximum_pair_count = 2U;
        REQUIRE(parse_entity(
            R"({ "classname" "worldspawn" "wad" "a.wad" })", limits));
        const auto over = parse_entity(
            R"({ "classname" "worldspawn" "wad" "a.wad" "other" "x" })",
            limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcWorldspawnErrorCode::pair_count_limit_exceeded);
    }
    SECTION("value length")
    {
        auto limits = bsp::GoldSrcWorldspawnParseLimits{};
        limits.maximum_value_bytes = 10U; // exact length of worldspawn
        REQUIRE(parse_entity(R"({ "classname" "worldspawn" })", limits));
        const auto over = parse_entity(
            R"({ "classname" "worldspawnX" })", limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcWorldspawnErrorCode::value_length_limit_exceeded);
    }
    SECTION("entity lump")
    {
        const auto minimal = entity_bytes(R"({"classname" "worldspawn"})");
        auto limits = bsp::GoldSrcWorldspawnParseLimits{};
        limits.maximum_entity_lump_bytes = minimal.size();
        REQUIRE(bsp::GoldSrcEntityLumpParser::parse_worldspawn_wad_references(
            minimal, limits));
        --limits.maximum_entity_lump_bytes;
        const auto over =
            bsp::GoldSrcEntityLumpParser::parse_worldspawn_wad_references(
                minimal, limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcWorldspawnErrorCode::entity_lump_too_large);
    }
}

TEST_CASE("WAD reference splitting rejects unsafe basename profiles",
    "[goldsrc-worldspawn][wad-references][safety]")
{
    const auto check_error = [](const std::string_view value,
                                 const bsp::GoldSrcWorldspawnErrorCode code) {
        const auto result = bsp::GoldSrcWadReferenceParser::parse(value);
        INFO(value.size());
        REQUIRE_FALSE(result);
        CHECK(result.error->code == code);
        CHECK(result.error->context.find(value) == std::string::npos);
    };

    SECTION("internal empty segment")
    {
        check_error("a.wad;;b.wad",
            bsp::GoldSrcWorldspawnErrorCode::empty_wad_reference);
    }
    SECTION("trailing source separator")
    {
        check_error(R"(C:\tools\wad\)",
            bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
    }
    SECTION("dot and dot-dot")
    {
        check_error(".", bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
        check_error("..", bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
    }
    SECTION("alternate data stream colon")
    {
        check_error("safe:stream.wad",
            bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
    }
    SECTION("reserved Windows device")
    {
        check_error(R"(C:\compiler\CON.wad)",
            bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
        check_error("lpt9.wad",
            bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
    }
    SECTION("non-ASCII")
    {
        std::string value{"bad"};
        value.push_back(static_cast<char>(0xFF));
        value += ".wad";
        check_error(value,
            bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
    }
    SECTION("wrong extension")
    {
        check_error("texture.pak",
            bsp::GoldSrcWorldspawnErrorCode::unsupported_wad_extension);
    }
    SECTION("trailing dot")
    {
        check_error("texture.wad.",
            bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
    }
}

TEST_CASE("WAD reference limits and case-insensitive deduplication are exact",
    "[goldsrc-worldspawn][wad-references][limits][deduplication]")
{
    auto limits = bsp::GoldSrcWorldspawnParseLimits{};
    limits.maximum_wad_reference_count = 2U;
    auto result = bsp::GoldSrcWadReferenceParser::parse(
        "a.wad;B.WAD;A.WAD;", limits);
    REQUIRE_FALSE(result); // Work/declaration count is bounded before deduplication.
    CHECK(result.error->code ==
        bsp::GoldSrcWorldspawnErrorCode::wad_reference_count_limit_exceeded);

    result = bsp::GoldSrcWadReferenceParser::parse("a.wad;A.WAD;", {});
    REQUIRE(result);
    REQUIRE(result.references->size() == 1U);
    CHECK(result.references->references()[0U].basename == "a.wad");

    limits = bsp::GoldSrcWorldspawnParseLimits{};
    limits.maximum_wad_basename_bytes = 5U;
    REQUIRE(bsp::GoldSrcWadReferenceParser::parse("a.wad", limits));
    const auto over = bsp::GoldSrcWadReferenceParser::parse("ab.wad", limits);
    REQUIRE_FALSE(over);
    CHECK(over.error->code ==
        bsp::GoldSrcWorldspawnErrorCode::unsafe_wad_basename);
}

} // namespace
