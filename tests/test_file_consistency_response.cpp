#include <hlclient/goldsrc/file_consistency_response.hpp>
#include <hlclient/goldsrc/bit_reader.hpp>

#include "local_resource_readiness_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace gs = hlclient::goldsrc;
namespace fixture = hlclient::tests::readiness_fixture;
using hlclient::tests::ScopedLocalResourceTestRoot;
using resource_list_test_fixture::EntrySpec;

[[nodiscard]] gs::ResourceListState requested_list(
    const std::vector<EntrySpec>& entries,
    const std::vector<std::uint16_t>& ordinals, const bool required = true)
{
    resource_list_test_fixture::LsbBitWriter writer;
    writer.write(43U, 8U);
    writer.write(static_cast<std::uint32_t>(entries.size()), 12U);
    for (const auto& entry : entries) {
        writer.write(entry.type, 4U);
        for (const unsigned char character : entry.name) writer.write(character, 8U);
        writer.write(0U, 8U);
        writer.write(entry.index, 12U);
        writer.write(entry.size_code, 24U);
        writer.write(entry.flags, 4U);
    }
    writer.write(required ? 1U : 0U, 1U);
    std::uint16_t previous = 0U;
    if (required) {
        for (const auto ordinal : ordinals) {
            writer.write(1U, 1U);
            const auto difference = static_cast<unsigned>(ordinal - previous);
            const bool delta = difference <= 31U;
            writer.write(delta ? 1U : 0U, 1U);
            REQUIRE((delta || ordinal < 1024U));
            writer.write(delta ? difference : ordinal, delta ? 5U : 10U);
            previous = ordinal;
        }
        writer.write(0U, 1U);
    }
    const auto padding = (8U - (writer.bit_length() & 7U)) & 7U;
    writer.write(0U, padding);
    const auto bits = writer.bit_length();
    const auto bytes = writer.take_bytes();
    auto parsed = gs::ResourceListParser{}.parse(bytes, 0U, bits);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    return std::move(*parsed.state);
}

// Independently expressed COM_UnMunge: XOR spawn count, per-byte table,
// byte-swap the 32-bit word, XOR complement. No production encoder/helper.
[[nodiscard]] std::vector<std::byte> decoded_body(
    const std::vector<std::byte>& semantic, const std::uint32_t count)
{
    REQUIRE(semantic.size() >= 4U);
    REQUIRE(semantic[0U] == std::byte{7U});
    const auto length = std::to_integer<unsigned>(semantic[1U]) |
        (std::to_integer<unsigned>(semantic[2U]) << 8U);
    REQUIRE(length == semantic.size() - 3U);
    REQUIRE(length <= 32767U);
    std::vector<std::byte> body(semantic.begin() + 3, semantic.end());
    constexpr std::array<unsigned, 16U> table{
        0x7aU, 0x64U, 0x05U, 0xf1U, 0x1bU, 0x9bU, 0xa0U, 0xb5U,
        0xcaU, 0xedU, 0x61U, 0x0dU, 0x4aU, 0xdfU, 0x8eU, 0xc7U};
    for (std::size_t word = 0U; word < body.size() / 4U; ++word) {
        std::uint32_t value = 0U;
        for (unsigned byte = 0U; byte < 4U; ++byte)
            value |= std::to_integer<std::uint32_t>(body[word * 4U + byte]) << (8U * byte);
        value ^= count;
        for (unsigned byte = 0U; byte < 4U; ++byte)
            value ^= (0xa5U | (byte << byte) | byte | table[(word + byte) & 15U]) << (byte * 8U);
        value = ((value & 0xffU) << 24U) | ((value & 0xff00U) << 8U) |
            ((value >> 8U) & 0xff00U) | (value >> 24U);
        value ^= ~count;
        for (unsigned byte = 0U; byte < 4U; ++byte)
            body[word * 4U + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xffU);
    }
    return body;
}

void check_row(gs::BitReader& reader, const std::uint16_t ordinal,
               const std::uint32_t expected_prefix)
{
    CHECK(reader.read_bits(1U).value == 1U);
    CHECK(reader.read_bits(12U).value == ordinal);
    CHECK(reader.read_bits(32U).value == expected_prefix);
}

void check_end(gs::BitReader& reader)
{
    CHECK(reader.read_bits(1U).value == 0U);
    CHECK(reader.align_to_byte_zero_padding() == gs::BitReaderError::none);
    CHECK(reader.remaining_bits() == 0U);
}

TEST_CASE("File consistency distinguishes no request from required empty reply",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    auto environment = fixture::make_environment(root);
    const auto absent = requested_list({{4U, "private.cfg", 0U, 0U, 0U}}, {}, false);
    const auto no_reply = gs::build_file_consistency_response(absent, *environment, 23U);
    REQUIRE(no_reply);
    CHECK(no_reply.semantic_bytes.empty());

    const auto empty = requested_list({{3U, "{metadata", 4095U, 0U, 0U}}, {});
    const auto reply = gs::build_file_consistency_response(empty, *environment, 0xffffffffU);
    REQUIRE(reply);
    const std::vector<std::byte> expected{std::byte{7U}, std::byte{1U}, std::byte{0U}, std::byte{0U}};
    CHECK(reply.semantic_bytes == expected);
}

TEST_CASE("File consistency matches independent abc COM_Munge vector and ordinal",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/test.mdl", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {3U, "{metadata", 4095U, 0U, 0U},
        {2U, "models/test.mdl", 99U, 0x00ffffffU, 1U}}, {1U});
    const auto reply = gs::build_file_consistency_response(list, *environment, 0x12345678U);
    REQUIRE(reply);
    // MD5("abc") starts 90 01 50 98. Plain row is 03 00 32 00 0a 13;
    // wordwise COM_Munge with key0x12345678 gives 6a 48 32 69 0a 13.
    const std::vector<std::byte> expected{
        std::byte{0x07U}, std::byte{0x06U}, std::byte{0x00U}, std::byte{0x6aU},
        std::byte{0x48U}, std::byte{0x32U}, std::byte{0x69U}, std::byte{0x0aU}, std::byte{0x13U}};
    CHECK(reply.semantic_bytes == expected);
    const auto body = decoded_body(reply.semantic_bytes, 0x12345678U);
    gs::BitReader reader{body};
    check_row(reader, 1U, 0x98500190U);
    check_end(reader);
}

TEST_CASE("File consistency hashes actual empty files and full high-byte spawn count",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "sound/weapons/empty.wav", "");
    root.write("valve", "models/test.mdl", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {0U, "weapons/empty.wav", 7U, 10U, 0U},
        {2U, "models/test.mdl", 7U, 0U, 0U}}, {0U, 1U});
    for (const auto count : {0U, 0x12345678U, 0xffffffffU}) {
        CAPTURE(count);
        const auto reply = gs::build_file_consistency_response(list, *environment, count);
        REQUIRE(reply);
        const auto body = decoded_body(reply.semantic_bytes, count);
        gs::BitReader reader{body};
        check_row(reader, 0U, 0xd98c1dd4U); // Real MD5 of empty data, not an invented zero.
        check_row(reader, 1U, 0x98500190U);
        check_end(reader);
    }
}

TEST_CASE("File consistency rejects a missing later file transactionally",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/test.mdl", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {2U, "models/test.mdl", 1U, 0U, 0U},
        {0U, "private_missing.wav", 4U, 0U, 0U}}, {0U, 1U});
    const auto reply = gs::build_file_consistency_response(list, *environment, 5U);
    REQUIRE_FALSE(reply);
    CHECK(reply.semantic_bytes.empty());
    REQUIRE(reply.error);
    CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::local_file_unavailable);
    CHECK(reply.error->ordinal == 1U);
    CHECK(reply.error->context.starts_with("ordinal=1 type=0 name=\"private_missing.wav\""));
}

TEST_CASE("File consistency never hashes arbitrary config executable or unsafe paths",
          "[goldsrc][file-consistency][security]")
{
    ScopedLocalResourceTestRoot root;
    auto environment = fixture::make_environment(root);
    const std::vector<EntrySpec> cases{
        {4U, "config.cfg", 1U, 0U, 0U}, {4U, "engine.dll", 1U, 0U, 0U},
        {4U, "hl.exe", 1U, 0U, 0U}, {4U, "models/private.cfg", 1U, 0U, 0U},
        {4U, "secrets/test.mdl", 1U, 0U, 0U}, {2U, "models/test.exe", 1U, 0U, 0U},
        {0U, "weapons/private.cfg", 1U, 0U, 0U}, {5U, "events/private.cfg", 1U, 0U, 0U},
        {3U, "{lambda", 1U, 0U, 0U}, {2U, "*1", 1U, 0U, 0U},
        {2U, "models/../private.mdl", 1U, 0U, 0U},
        {4U, "C:/private.mdl", 1U, 0U, 0U},
        {4U, "models/test.mdl:stream", 1U, 0U, 0U},
    };
    for (const auto& entry : cases) {
        CAPTURE(entry.type, entry.name);
        const auto list = requested_list({entry}, {0U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 1U);
        REQUIRE_FALSE(reply);
        CHECK(reply.semantic_bytes.empty());
        REQUIRE(reply.error);
        CHECK((reply.error->code == gs::FileConsistencyResponseErrorCode::unsupported_asset ||
               reply.error->code == gs::FileConsistencyResponseErrorCode::unsafe_name));
        CHECK(reply.error->ordinal == 0U);
        CHECK(reply.error->context.starts_with("ordinal=0 type=" + std::to_string(entry.type)));
    }
}

TEST_CASE("File consistency accepts only mapped game asset families and case folding",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    const std::vector<EntrySpec> entries{
        {2U, "models/test.mdl", 1U, 0U, 0U}, {2U, "maps/test.bsp", 2U, 0U, 0U},
        {2U, "sprites/test.spr", 3U, 0U, 0U}, {0U, "test.wav", 4U, 0U, 0U},
        {5U, "events/test.sc", 5U, 0U, 0U}, {4U, "gfx/env/test.tga", 6U, 0U, 0U},
        {4U, "decals.wad", 7U, 0U, 0U}, {4U, "MODELS/UPPER.MDL", 8U, 0U, 0U},
    };
    std::vector<std::uint16_t> ordinals;
    for (std::size_t i = 0U; i < entries.size(); ++i) {
        root.write("valve", entries[i].type == 0U ? "sound/" + entries[i].name : entries[i].name, "abc");
        ordinals.push_back(static_cast<std::uint16_t>(i));
    }
    auto environment = fixture::make_environment(root);
    const auto list = requested_list(entries, ordinals);
    const auto reply = gs::build_file_consistency_response(list, *environment, 36U);
    REQUIRE(reply);
    const auto body = decoded_body(reply.semantic_bytes, 36U);
    gs::BitReader reader{body};
    for (const auto ordinal : ordinals) check_row(reader, ordinal, 0x98500190U);
    check_end(reader);
}

TEST_CASE("File consistency supports response lengths beyond the movement byte limit",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/empty.mdl", "");
    auto environment = fixture::make_environment(root);
    std::vector<EntrySpec> entries;
    std::vector<std::uint16_t> ordinals;
    for (std::uint16_t i = 0U; i < 80U; ++i) {
        entries.push_back({2U, "models/empty.mdl", static_cast<std::uint16_t>(100U + i), 0U, 0U});
        ordinals.push_back(i);
    }
    const auto list = requested_list(entries, ordinals);
    const auto reply = gs::build_file_consistency_response(list, *environment, 0xc1234567U);
    REQUIRE(reply);
    REQUIRE(reply.semantic_bytes.size() > 258U);
    CHECK(reply.semantic_bytes[2U] != std::byte{0U});
    const auto body = decoded_body(reply.semantic_bytes, 0xc1234567U);
    gs::BitReader reader{body};
    for (const auto ordinal : ordinals) check_row(reader, ordinal, 0xd98c1dd4U);
    check_end(reader);
}

TEST_CASE("File consistency byte-aligned terminator adds no stray byte",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/test.mdl", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {2U, "models/test.mdl", 9U, 0U, 0U},
        {2U, "models/test.mdl", 8U, 0U, 0U},
        {2U, "models/test.mdl", 7U, 0U, 0U}}, {0U, 1U, 2U});
    const auto reply = gs::build_file_consistency_response(list, *environment, 0U);
    REQUIRE(reply);
    REQUIRE(reply.semantic_bytes.size() == 20U); // 3 header + (3 * 45 + 1) / 8.
    const auto body = decoded_body(reply.semantic_bytes, 0U);
    gs::BitReader reader{body};
    for (std::uint16_t ordinal = 0U; ordinal < 3U; ++ordinal)
        check_row(reader, ordinal, 0x98500190U);
    check_end(reader);
}

TEST_CASE("File consistency cancellation and actual local size bounds publish no reply",
          "[goldsrc][file-consistency]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "models/test.mdl", "");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({{2U, "models/test.mdl", 1U, 0U, 0U}}, {0U});
    SECTION("pre-cancelled") {
        std::stop_source stop;
        REQUIRE(stop.request_stop());
        const auto reply = gs::build_file_consistency_response(list, *environment, 1U, stop.get_token());
        REQUIRE_FALSE(reply);
        CHECK(reply.semantic_bytes.empty());
        REQUIRE(reply.error);
        CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::cancelled);
    }
    SECTION("actual size not untrusted declared size") {
        std::filesystem::resize_file(root.game_path("valve") / "models/test.mdl",
                                    gs::kMaximumFileConsistencyFileBytes + 1U);
        const auto reply = gs::build_file_consistency_response(list, *environment, 1U);
        REQUIRE_FALSE(reply);
        CHECK(reply.semantic_bytes.empty());
        REQUIRE(reply.error);
        CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::local_file_too_large);
        CHECK(reply.error->ordinal == 0U);
    }
}

TEST_CASE("Recognized ReChecker hashes actual schema and assets and reports only verified absence",
          "[goldsrc][file-consistency][rechecker]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "delta.lst", "abc");
    root.write("valve", "models/present.mdl", "");
    root.write("valve", "sound/test.wav", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {3U, "delta.lst", 4095U, 0U, 0U},
        {3U, "models/absent.mdl", 4095U, 0U, 0U},
        {3U, "models/present.mdl", 4095U, 0U, 0U},
        {3U, "sprites/absent.spr", 4095U, 0U, 0U},
        {0U, "test.wav", 2U, 0U, 0U},
        {3U, "maps/absent.bsp", 4095U, 0U, 0U}}, {0U, 1U, 2U, 3U, 4U, 5U});
    const auto reply = gs::build_file_consistency_response(list, *environment, 0x12345678U);
    INFO((reply.error ? reply.error->context : std::string{}));
    REQUIRE(reply);
    const auto body = decoded_body(reply.semantic_bytes, 0x12345678U);
    gs::BitReader reader{body};
    check_row(reader, 0U, 0x98500190U); // Actual baseline MD5("abc").
    check_row(reader, 1U, 0x98500190U); // Verified missing: previous response.
    check_row(reader, 2U, 0xd98c1dd4U); // Present zero-byte file: its actual MD5.
    check_row(reader, 3U, 0xd98c1dd4U);
    check_row(reader, 4U, 0x98500190U); // Ordinary row also updates previous response.
    check_row(reader, 5U, 0x98500190U);
    check_end(reader);
}

TEST_CASE("ReChecker profile never turns forbidden requests or failed reads into absence",
          "[goldsrc][file-consistency][rechecker][security]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "delta.lst", "abc");
    root.write("valve", "models/directory.mdl/child", "abc");
    auto environment = fixture::make_environment(root);
    const std::vector<std::string> forbidden{
        "../demoplayer.dll", "../models/escape.mdl", "config.cfg", "engine.dll",
        "models/private.cfg", "C:/models/escape.mdl", "models/test.mdl:stream",
        "delta.lst", "models/directory.mdl"};
    for (const auto& name : forbidden) {
        CAPTURE(name);
        const auto list = requested_list({
            {3U, "delta.lst", 4095U, 0U, 0U},
            {3U, name, 4095U, 0U, 0U}}, {0U, 1U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 12U);
        REQUIRE_FALSE(reply);
        CHECK(reply.semantic_bytes.empty());
        REQUIRE(reply.error);
        CHECK(reply.error->ordinal == 1U);
        CHECK(reply.error->context.find(" rechecker=1") != std::string::npos);
        if (name == "models/directory.mdl")
            CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::local_file_unavailable);
    }
    const auto ordinary_missing = requested_list({
        {3U, "delta.lst", 4095U, 0U, 0U},
        {2U, "models/absent.mdl", 2U, 0U, 0U}}, {0U, 1U});
    const auto reply = gs::build_file_consistency_response(ordinary_missing, *environment, 12U);
    REQUIRE_FALSE(reply);
    REQUIRE(reply.error);
    CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::local_file_unavailable);
    CHECK(reply.semantic_bytes.empty());
}

TEST_CASE("ReChecker schema admission requires the exact requested head and a real file",
          "[goldsrc][file-consistency][rechecker][security]")
{
    ScopedLocalResourceTestRoot root;
    auto environment = fixture::make_environment(root);
    SECTION("missing baseline never receives an invented response") {
        const auto list = requested_list({{3U, "delta.lst", 4095U, 0U, 0U}}, {0U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 3U);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        CHECK(reply.error->ordinal == 0U);
        CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::local_file_unavailable);
        CHECK(reply.semantic_bytes.empty());
    }
    SECTION("similar names and ordinary indices do not authorize schema reads") {
        root.write("valve", "delta.lst", "abc");
        for (const auto& head : std::vector<EntrySpec>{
                 {4U, "delta.lst", 4095U, 0U, 0U},
                 {3U, "delta.lst", 4094U, 0U, 0U},
                 {3U, "DELTA.LST", 4095U, 0U, 0U}}) {
            const auto list = requested_list({head}, {0U});
            const auto reply = gs::build_file_consistency_response(list, *environment, 3U);
            REQUIRE_FALSE(reply);
            REQUIRE(reply.error);
            CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::unsupported_asset);
            CHECK(reply.semantic_bytes.empty());
        }
    }
    SECTION("an unrequested head does not enable absence reporting") {
        root.write("valve", "delta.lst", "abc");
        const auto list = requested_list({
            {3U, "delta.lst", 4095U, 0U, 0U},
            {3U, "models/absent.mdl", 4095U, 0U, 0U}}, {1U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 3U);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::unsupported_asset);
        CHECK(reply.semantic_bytes.empty());
    }
}

TEST_CASE("File consistency failure classifies the full requested list without exposing local data",
          "[goldsrc][file-consistency][security]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "delta.lst", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {3U, "delta.lst", 4095U, 0U, 0U},
        {3U, "../private.dll", 4095U, 0U, 0U},
        {3U, "C:/private.dll", 4095U, 0U, 0U},
        {3U, "models/test.mdl", 4095U, 0U, 0U},
        {3U, "private.cfg", 4095U, 0U, 0U},
        {0U, "weapons/test.wav", 2U, 0U, 0U},
        {4U, "other.cfg", 3U, 0U, 0U}}, {0U, 1U, 2U, 3U, 4U, 5U, 6U});
    const auto reply = gs::build_file_consistency_response(list, *environment, 5U);
    REQUIRE_FALSE(reply);
    REQUIRE(reply.error);
    CHECK(reply.error->ordinal == 1U);
    CHECK(reply.error->context.find(" name=\"../private.dll\" cut=0") != std::string::npos);
    CHECK(reply.error->context.find(
        " count=7 delta0=1 parent=1 abs=1 schema=1 assets=2 other=2 rechecker=1") != std::string::npos);
    CHECK(reply.error->context.size() <= 200U);
    CHECK(reply.error->context.find(root.path().string()) == std::string::npos);
    CHECK(reply.semantic_bytes.empty());
}

TEST_CASE("File consistency diagnostic escapes and bounds untrusted requested names",
          "[goldsrc][file-consistency][security]")
{
    ScopedLocalResourceTestRoot root;
    auto environment = fixture::make_environment(root);
    std::string name{"models/evil\"\\\r\n\t\x1b"};
    name.push_back(static_cast<char>(0x80U));
    name += ".cfg";
    SECTION("control and non-ASCII bytes are printable escaped data") {
        const auto list = requested_list({{4U, name, 1U, 0U, 0U}}, {0U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 1U);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        constexpr std::string_view expected_name{
            R"( name="models/evil\"\\\x0D\x0A\x09\x1B\x80.cfg" cut=0)"};
        CHECK(reply.error->context.find(expected_name) != std::string::npos);
        for (const unsigned char c : reply.error->context) CHECK((c >= 0x20U && c < 0x7fU));
        CHECK(reply.semantic_bytes.empty());
    }
    SECTION("expanded names leave all numeric classifications intact") {
        const auto list = requested_list({{4U, std::string(100U, '\x01'), 1U, 0U, 0U}}, {0U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 1U);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        CHECK(reply.error->context.size() <= 200U);
        CHECK(reply.error->context.find("\" cut=1 count=1") != std::string::npos);
        CHECK(reply.error->context.ends_with("other=1 rechecker=0"));
        for (const unsigned char c : reply.error->context) CHECK((c >= 0x20U && c < 0x7fU));
        CHECK(reply.semantic_bytes.empty());
    }
}

TEST_CASE("Approved installation checks hash real DLL and config bytes only after opt-in",
          "[goldsrc][file-consistency][rechecker][security]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "delta.lst", "abc");
    root.write("", "checker.dll", "");
    root.write("valve", "checker.cfg", "abc");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {3U, "delta.lst", 4095U, 0U, 0U},
        {3U, "../checker.dll", 4095U, 0U, 0U},
        {3U, "checker.cfg", 4095U, 0U, 0U},
        {3U, "../absent.dll", 4095U, 0U, 0U},
        {3U, "absent.cfg", 4095U, 0U, 0U}}, {0U, 1U, 2U, 3U, 4U});
    const auto strict = gs::build_file_consistency_response(list, *environment, 0x12345678U);
    REQUIRE_FALSE(strict);
    REQUIRE(strict.error);
    CHECK(strict.error->ordinal == 1U);
    CHECK(strict.semantic_bytes.empty());

    const auto approved = gs::build_file_consistency_response(list, *environment, 0x12345678U,
        {}, gs::FileConsistencyReadPolicy::approved_installation_files);
    INFO((approved.error ? approved.error->context : std::string{}));
    REQUIRE(approved);
    const auto body = decoded_body(approved.semantic_bytes, 0x12345678U);
    gs::BitReader reader{body};
    check_row(reader, 0U, 0x98500190U);
    check_row(reader, 1U, 0xd98c1dd4U); // Actual empty installation-base DLL.
    check_row(reader, 2U, 0x98500190U); // Actual game-root config.
    check_row(reader, 3U, 0x98500190U); // Genuine missing inside approved base.
    check_row(reader, 4U, 0x98500190U); // Genuine missing inside selected game.
    check_end(reader);
}

TEST_CASE("Approved installation checks cannot escape or disguise failed reads as missing",
          "[goldsrc][file-consistency][rechecker][security]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "delta.lst", "abc");
    root.write("valve", "first.cfg", "");
    root.write("", "directory.dll/child", "abc");
    auto environment = fixture::make_environment(root);
    for (const auto& name : std::vector<std::string>{
            "../../outside.dll", "../nested/../../outside.cfg", "/absolute.cfg",
            "C:/outside.dll", "../checker.dll:secret", "..\\..\\outside.dll",
            "../directory.dll"}) {
        CAPTURE(name);
        const auto list = requested_list({
            {3U, "delta.lst", 4095U, 0U, 0U},
            {3U, "first.cfg", 4095U, 0U, 0U},
            {3U, name, 4095U, 0U, 0U}}, {0U, 1U, 2U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 9U,
            {}, gs::FileConsistencyReadPolicy::approved_installation_files);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        CHECK(reply.error->ordinal == 2U);
        CHECK(reply.semantic_bytes.empty()); // Even the actual earlier hashes are not published.
    }
}

TEST_CASE("Approved installation policy does not expand ordinary resource checks",
          "[goldsrc][file-consistency][rechecker][security]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "delta.lst", "abc");
    root.write("valve", "ordinary.cfg", "");
    root.write("valve", "models/test.mdl", "abc");
    auto environment = fixture::make_environment(root);
    SECTION("normal game asset bytes remain identical") {
        const auto list = requested_list({{2U, "models/test.mdl", 1U, 0U, 0U}}, {0U});
        const auto strict = gs::build_file_consistency_response(list, *environment, 7U);
        const auto approved = gs::build_file_consistency_response(list, *environment, 7U,
            {}, gs::FileConsistencyReadPolicy::approved_installation_files);
        REQUIRE(strict);
        REQUIRE(approved);
        CHECK(strict.semantic_bytes == approved.semantic_bytes);
    }
    SECTION("ordinary config is not admitted without recognized profile") {
        const auto list = requested_list({{4U, "ordinary.cfg", 1U, 0U, 0U}}, {0U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 7U,
            {}, gs::FileConsistencyReadPolicy::approved_installation_files);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::unsupported_asset);
        CHECK(reply.semantic_bytes.empty());
    }
    SECTION("even a recognized list does not change ordinary rows") {
        const auto list = requested_list({
            {3U, "delta.lst", 4095U, 0U, 0U},
            {4U, "ordinary.cfg", 1U, 0U, 0U}}, {0U, 1U});
        const auto reply = gs::build_file_consistency_response(list, *environment, 7U,
            {}, gs::FileConsistencyReadPolicy::approved_installation_files);
        REQUIRE_FALSE(reply);
        REQUIRE(reply.error);
        CHECK(reply.error->ordinal == 1U);
        CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::unsupported_asset);
        CHECK(reply.semantic_bytes.empty());
    }
}

TEST_CASE("Approved installation policy still requires an actual schema baseline",
          "[goldsrc][file-consistency][rechecker][security]")
{
    ScopedLocalResourceTestRoot root;
    root.write("", "delta.lst", "abc"); // A base file must not replace missing selected-game schema.
    root.write("", "checker.dll", "");
    auto environment = fixture::make_environment(root);
    const auto list = requested_list({
        {3U, "delta.lst", 4095U, 0U, 0U},
        {3U, "../checker.dll", 4095U, 0U, 0U}}, {0U, 1U});
    const auto reply = gs::build_file_consistency_response(list, *environment, 1U,
        {}, gs::FileConsistencyReadPolicy::approved_installation_files);
    REQUIRE_FALSE(reply);
    REQUIRE(reply.error);
    CHECK(reply.error->ordinal == 0U);
    CHECK(reply.error->code == gs::FileConsistencyResponseErrorCode::local_file_unavailable);
    CHECK(reply.semantic_bytes.empty());
}

} // namespace
