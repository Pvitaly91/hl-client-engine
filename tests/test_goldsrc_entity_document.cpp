#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace brush = hlclient::goldsrc::brush_models;
namespace bsp = hlclient::goldsrc::bsp;

[[nodiscard]] std::vector<std::byte> entity_bytes(
    const std::string_view text,
    const bool terminal_nul = false)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() + static_cast<std::size_t>(terminal_nul));
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    if (terminal_nul) {
        bytes.push_back(std::byte{0});
    }
    return bytes;
}

[[nodiscard]] bsp::GoldSrcEntityDocumentParseResult parse_document(
    const std::string_view text,
    const bsp::GoldSrcEntityDocumentLimits& limits = {})
{
    return bsp::GoldSrcEntityDocumentParser::parse(entity_bytes(text), limits);
}

TEST_CASE("GoldSrc entity document owns all ordered entities and pairs",
    "[goldsrc-entity][document]")
{
    auto source = entity_bytes(
        "{\n\"classname\" \"worldspawn\"\n\"wad\" \"halflife.wad\"\n}\n"
        "{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
        "\"origin\" \"16 32 48\"\n}\n",
        true);
    auto parsed = bsp::GoldSrcEntityDocumentParser::parse(source);
    REQUIRE(parsed);
    REQUIRE(parsed.document->size() == 2U);
    REQUIRE(parsed.document->total_pair_count() == 5U);
    REQUIRE(parsed.document->entities()[0U].size() == 2U);
    CHECK(parsed.document->entities()[0U].pairs()[0U].key == "classname");
    CHECK(parsed.document->entities()[0U].pairs()[0U].value == "worldspawn");
    CHECK(parsed.document->entities()[0U].pairs()[1U].key == "wad");
    REQUIRE(parsed.document->entities()[1U].size() == 3U);
    CHECK(parsed.document->entities()[1U].pairs()[1U].value == "*1");

    source.assign(16U, std::byte{'X'});
    CHECK(parsed.document->entities()[1U].pairs()[2U].value == "16 32 48");
}

TEST_CASE("Entity parser accepts only quoted inert grammar and one final NUL",
    "[goldsrc-entity][document][grammar]")
{
    SECTION("empty and whitespace-only documents are inert")
    {
        REQUIRE(parse_document(""));
        const auto whitespace = parse_document(" \t\r\n");
        REQUIRE(whitespace);
        CHECK(whitespace.document->empty());
    }
    SECTION("terminal compiler NUL is accepted")
    {
        const auto parsed = bsp::GoldSrcEntityDocumentParser::parse(
            entity_bytes("{\"classname\" \"worldspawn\"}\n", true));
        REQUIRE(parsed);
        CHECK(parsed.document->size() == 1U);
    }
    SECTION("bytes after NUL are rejected")
    {
        auto bytes = entity_bytes("{}", true);
        bytes.push_back(std::byte{'X'});
        const auto parsed = bsp::GoldSrcEntityDocumentParser::parse(bytes);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::trailing_bytes_after_nul);
    }
    SECTION("unquoted tokens are rejected")
    {
        const auto parsed = parse_document("{ classname \"worldspawn\" }");
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::unexpected_token);
    }
    SECTION("unterminated key/value quotes are rejected")
    {
        const auto parsed = parse_document("{ \"classname\" \"worldspawn }");
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::unterminated_quote);
    }
    SECTION("missing value and missing brace are distinct")
    {
        auto parsed = parse_document("{ \"classname\" }");
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::missing_value);

        parsed = parse_document("{ \"classname\" \"worldspawn\"");
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::missing_closing_brace);
    }
    SECTION("NUL in a quoted token is rejected")
    {
        auto bytes = entity_bytes("{\"key\" \"value\"}");
        bytes.insert(bytes.begin() + 10, std::byte{0});
        const auto parsed = bsp::GoldSrcEntityDocumentParser::parse(bytes);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::nul_in_key_or_value);
    }
}

TEST_CASE("Entity values remain literal and are never executed or path-normalized",
    "[goldsrc-entity][document][inert]")
{
    const auto parsed = parse_document(
        R"ENTITY({"unknown" "$(danger)" "path" "C:\compiler\n\textures.wad" "slashes" "../maps/test.bsp"})ENTITY");
    REQUIRE(parsed);
    const auto pairs = parsed.document->entities()[0U].pairs();
    REQUIRE(pairs.size() == 3U);
    CHECK(pairs[0U].value == "$(danger)");
    CHECK(pairs[1U].value == R"(C:\compiler\n\textures.wad)");
    CHECK(pairs[2U].value == "../maps/test.bsp");
}

TEST_CASE("Interpreted key lookup rejects duplicates without affecting unknown keys",
    "[goldsrc-entity][keys]")
{
    SECTION("exact duplicate")
    {
        const auto parsed = parse_document(
            R"({"model" "*1" "model" "*2" "unknown" "a" "unknown" "b"})");
        REQUIRE(parsed);
        const auto lookup = bsp::find_interpreted_key(
            parsed.document->entities()[0U],
            bsp::GoldSrcInterpretedEntityKey::model);
        CHECK(lookup.status ==
            bsp::GoldSrcInterpretedKeyStatus::exact_duplicate);
        CHECK(lookup.first_pair_index == 0U);
        CHECK(lookup.conflicting_pair_index == 1U);
        CHECK(lookup.unique_pair(parsed.document->entities()[0U]) == nullptr);
    }
    SECTION("ASCII-case collision")
    {
        const auto parsed = parse_document(R"({"Model" "*1" "model" "*1"})");
        REQUIRE(parsed);
        const auto lookup = bsp::find_interpreted_key(
            parsed.document->entities()[0U],
            bsp::GoldSrcInterpretedEntityKey::model);
        CHECK(lookup.status ==
            bsp::GoldSrcInterpretedKeyStatus::ascii_case_collision);
    }
    SECTION("single differently-cased key is unique")
    {
        const auto parsed = parse_document(R"({"MODEL" "*1"})");
        REQUIRE(parsed);
        const auto lookup = bsp::find_interpreted_key(
            parsed.document->entities()[0U],
            bsp::GoldSrcInterpretedEntityKey::model);
        REQUIRE(lookup.status == bsp::GoldSrcInterpretedKeyStatus::unique);
        REQUIRE(lookup.unique_pair(parsed.document->entities()[0U]) != nullptr);
        CHECK(lookup.unique_pair(parsed.document->entities()[0U])->value == "*1");
    }
}

TEST_CASE("Entity-document limits accept exact boundaries and reject plus one",
    "[goldsrc-entity][document][limits]")
{
    SECTION("entity and total-pair count")
    {
        auto limits = bsp::GoldSrcEntityDocumentLimits{};
        limits.maximum_entities = 1U;
        limits.maximum_total_pairs = 1U;
        REQUIRE(parse_document(R"({"k" "v"})", limits));

        auto over = parse_document(R"({"k" "v"}{})", limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::entity_count_limit_exceeded);

        over = parse_document(R"({"k" "v" "x" "y"})", limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::total_pair_count_limit_exceeded);
    }
    SECTION("pairs per entity")
    {
        auto limits = bsp::GoldSrcEntityDocumentLimits{};
        limits.maximum_pairs_per_entity = 1U;
        REQUIRE(parse_document(R"({"k" "v"})", limits));
        const auto over = parse_document(R"({"k" "v" "x" "y"})", limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::pair_count_limit_exceeded);
    }
    SECTION("key and value bytes")
    {
        auto limits = bsp::GoldSrcEntityDocumentLimits{};
        limits.maximum_key_bytes = 1U;
        limits.maximum_value_bytes = 1U;
        REQUIRE(parse_document(R"({"k" "v"})", limits));

        auto over = parse_document(R"({"kk" "v"})", limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::key_length_limit_exceeded);
        over = parse_document(R"({"k" "vv"})", limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::value_length_limit_exceeded);
    }
    SECTION("lump bytes")
    {
        const auto text = std::string{R"({"k" "v"})"};
        auto limits = bsp::GoldSrcEntityDocumentLimits{};
        limits.maximum_entity_lump_bytes = text.size();
        REQUIRE(parse_document(text, limits));
        --limits.maximum_entity_lump_bytes;
        const auto over = parse_document(text, limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::entity_lump_too_large);
    }
    SECTION("hard-limit configuration cannot be expanded")
    {
        auto limits = bsp::GoldSrcEntityDocumentLimits{};
        ++limits.maximum_entities;
        const auto invalid = parse_document("{}", limits);
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error->code ==
            bsp::GoldSrcEntityDocumentErrorCode::invalid_configuration);
    }
}

TEST_CASE("Brush model references use exact bounded star-decimal grammar",
    "[goldsrc-entity][brush-reference]")
{
    auto parsed = brush::parse_brush_model_reference("*1", 3U);
    REQUIRE(parsed);
    CHECK(parsed.source_model_index == 1U);
    parsed = brush::parse_brush_model_reference("*2", 3U);
    REQUIRE(parsed);
    CHECK(parsed.source_model_index == 2U);

    const auto check = [](const std::string_view value,
                           const brush::GoldSrcBrushModelReferenceErrorCode error) {
        const auto result = brush::parse_brush_model_reference(value, 3U);
        REQUIRE_FALSE(result);
        CHECK(result.error == error);
    };
    check("models/door.mdl",
        brush::GoldSrcBrushModelReferenceErrorCode::not_brush_reference);
    check("*0", brush::GoldSrcBrushModelReferenceErrorCode::world_model_reference);
    check("*", brush::GoldSrcBrushModelReferenceErrorCode::invalid_syntax);
    check("*-1", brush::GoldSrcBrushModelReferenceErrorCode::invalid_syntax);
    check("*+1", brush::GoldSrcBrushModelReferenceErrorCode::invalid_syntax);
    check("*1 ", brush::GoldSrcBrushModelReferenceErrorCode::invalid_syntax);
    check("*3", brush::GoldSrcBrushModelReferenceErrorCode::index_out_of_range);
    check("*42949672960",
        brush::GoldSrcBrushModelReferenceErrorCode::index_overflow);
}

TEST_CASE("Origin and angle metadata use strict finite decimal parsing",
    "[goldsrc-entity][brush-transform-metadata]")
{
    auto vector = brush::parse_entity_vector3(" 1.5\t-2 +3e1 ");
    REQUIRE(vector);
    CHECK(vector.value->x == 1.5F);
    CHECK(vector.value->y == -2.0F);
    CHECK(vector.value->z == 30.0F);

    CHECK_FALSE(brush::parse_entity_vector3("1 2"));
    CHECK_FALSE(brush::parse_entity_vector3("1 2 3 4"));
    CHECK_FALSE(brush::parse_entity_vector3("1,2,3"));
    CHECK_FALSE(brush::parse_entity_vector3("nan 2 3"));
    CHECK_FALSE(brush::parse_entity_vector3("inf 2 3"));
    CHECK_FALSE(brush::parse_entity_vector3("1e999 2 3"));

    auto angles = brush::parse_entity_angles("10 20 30", "90");
    REQUIRE(angles);
    CHECK(angles.source == brush::GoldSrcEntityAnglesSource::angles_vector);
    CHECK(angles.degrees->x == 10.0F);
    CHECK(angles.degrees->y == 20.0F);
    CHECK(angles.degrees->z == 30.0F);

    angles = brush::parse_entity_angles(std::nullopt, "90");
    REQUIRE(angles);
    CHECK(angles.source == brush::GoldSrcEntityAnglesSource::angle_yaw);
    CHECK(angles.degrees->y == 90.0F);
    angles = brush::parse_entity_angles(std::nullopt, "-1");
    REQUIRE(angles);
    CHECK(angles.source == brush::GoldSrcEntityAnglesSource::angle_up);
    CHECK(angles.degrees->x == -90.0F);
    angles = brush::parse_entity_angles(std::nullopt, "-2");
    REQUIRE(angles);
    CHECK(angles.source == brush::GoldSrcEntityAnglesSource::angle_down);
    CHECK(angles.degrees->x == 90.0F);
}

TEST_CASE("Brush entity interpretation retains typed unsupported outcomes",
    "[goldsrc-entity][brush-instance]")
{
    const auto interpret = [](const std::string_view text) {
        const auto document = parse_document(text);
        REQUIRE(document);
        REQUIRE(document.document->size() == 1U);
        return brush::interpret_brush_entity(
            document.document->entities()[0U], 7U, 3U);
    };

    SECTION("supported opaque initial metadata")
    {
        const auto result = interpret(
            R"({"classname" "func_door" "model" "*1" "origin" "1 2 3" "angle" "90" "rendermode" "0" "renderamt" "128"})");
        REQUIRE(result.metadata);
        CHECK(result.metadata->source_entity_ordinal == 7U);
        CHECK(result.metadata->source_model_index == 1U);
        CHECK(result.metadata->classname_category ==
            brush::GoldSrcBrushClassnameCategory::function_entity);
        CHECK(result.metadata->origin.x == 1.0F);
        CHECK(result.metadata->angles_degrees.y == 90.0F);
        CHECK(result.metadata->render_amount == 128.0F);
        CHECK(result.metadata->status ==
            brush::BrushSubmodelInstanceStatus::supported_static_opaque);
    }
    SECTION("non-brush model remains outside M4.4")
    {
        CHECK_FALSE(interpret(R"({"model" "models/barney.mdl"})").metadata);
        CHECK_FALSE(interpret(R"({"classname" "light"})").metadata);
    }
    SECTION("invalid model reference is retained")
    {
        const auto result = interpret(R"({"model" "*0"})");
        REQUIRE(result.metadata);
        CHECK(result.metadata->status ==
            brush::BrushSubmodelInstanceStatus::invalid_model_reference);
    }
    SECTION("ambiguous interpreted keys are retained as invalid metadata")
    {
        auto result = interpret(R"({"model" "*1" "model" "*2"})");
        REQUIRE(result.metadata);
        CHECK(result.metadata->status ==
            brush::BrushSubmodelInstanceStatus::invalid_entity_metadata);
        result = interpret(R"({"model" "*1" "Origin" "0 0 0" "origin" "1 2 3"})");
        REQUIRE(result.metadata);
        CHECK(result.metadata->status ==
            brush::BrushSubmodelInstanceStatus::invalid_entity_metadata);
    }
    SECTION("invalid transform and nonzero rendermode are typed")
    {
        auto result = interpret(R"({"model" "*1" "origin" "1 2"})");
        REQUIRE(result.metadata);
        CHECK(result.metadata->status ==
            brush::BrushSubmodelInstanceStatus::unsupported_transform);
        result = interpret(R"({"model" "*1" "rendermode" "5"})");
        REQUIRE(result.metadata);
        CHECK(result.metadata->status ==
            brush::BrushSubmodelInstanceStatus::unsupported_rendermode);
    }
}

} // namespace
