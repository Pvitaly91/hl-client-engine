#include <hlclient/goldsrc/resource_list.hpp>

#include "resource_list_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = resource_list_test_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] fixture::Message one_entry_message(
    const std::uint8_t type = 4U,
    std::string name = "resource",
    const std::uint16_t index = 1U,
    const std::uint32_t size_code = 123U,
    const std::uint8_t flags = 0U)
{
    return fixture::make_message({fixture::EntrySpec{
        type,
        std::move(name),
        index,
        size_code,
        flags,
    }});
}

[[nodiscard]] goldsrc::ResourceListErrorCode error_code(
    const goldsrc::ResourceListParseResult& result)
{
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.bits_consumed == 0U);
    CHECK(result.bytes_consumed == 0U);
    CHECK(result.next_byte_offset == 0U);
    CHECK(result.next_bit_offset == 0U);
    return result.error->code;
}

TEST_CASE("GoldSrc resource-list parser matches the independent sanitized literal",
          "[goldsrc][resource-list][fixture]")
{
    const auto result = goldsrc::ResourceListParser{}.parse(
        fixture::kExactResourceListMessage,
        0U);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK_FALSE(result.error);
    CHECK(result.bits_consumed == fixture::kExactMessageBits);
    CHECK(result.bytes_consumed == fixture::kExactResourceListMessage.size());
    CHECK(result.next_byte_offset == fixture::kExactResourceListMessage.size());
    CHECK(result.next_bit_offset == 0U);

    const auto& state = *result.state;
    CHECK(state.resource_count() == 3U);
    CHECK(state.total_name_byte_count() == fixture::kExactTotalNameBytes);
    CHECK(state.total_size_code_sum() == fixture::kExactRawSizeCodeSum);
    CHECK(state.source_opcode_byte_offset() == 0U);
    CHECK(state.source_payload_bit_length() == fixture::kExactMessageBits);
    CHECK(state.bits_consumed() == fixture::kExactMessageBits);
    CHECK(state.bytes_consumed() == fixture::kExactResourceListMessage.size());
    CHECK(state.next_byte_offset() == fixture::kExactResourceListMessage.size());
    CHECK(state.next_bit_offset() == 0U);
    CHECK(state.compatibility_profile() ==
          goldsrc::ResourceListCompatibilityProfile::
              stock_protocol_48_build_10210_standard);
    CHECK(state.evidence_profile() ==
          goldsrc::ResourceListEvidenceProfile::
              repeated_signed_stock_standard_resource_lists);

    const auto ordered_type_counts = state.type_summary().ordered_counts();
    REQUIRE(ordered_type_counts.size() == 5U);
    CHECK(ordered_type_counts[0U].type() == goldsrc::ResourceType::sound);
    CHECK(ordered_type_counts[0U].count() == 1U);
    CHECK(ordered_type_counts[1U].type() == goldsrc::ResourceType::model);
    CHECK(ordered_type_counts[1U].count() == 2U);
    CHECK(ordered_type_counts[2U].type() == goldsrc::ResourceType::decal);
    CHECK(ordered_type_counts[2U].count() == 0U);
    CHECK(ordered_type_counts[3U].type() == goldsrc::ResourceType::generic);
    CHECK(ordered_type_counts[3U].count() == 0U);
    CHECK(ordered_type_counts[4U].type() ==
          goldsrc::ResourceType::event_script);
    CHECK(ordered_type_counts[4U].count() == 0U);
    CHECK(state.type_summary().count(goldsrc::ResourceType::model) == 2U);
    CHECK(state.type_summary().count(goldsrc::ResourceType::sound) == 1U);

    REQUIRE(state.entries().size() == 3U);
    const auto& map = state.entries()[0U];
    CHECK(map.type() == goldsrc::ResourceType::model);
    CHECK(map.name().bytes() == "maps/test_map.bsp");
    CHECK(map.index().value() == 7U);
    CHECK(map.declared_size().raw_code() == 0x12'34'56U);
    CHECK(map.flags().wire_value() == 1U);
    CHECK(map.wire_ordinal() == 0U);
    CHECK(map.source_start_bit_offset() == 20U);
    CHECK(map.source_end_bit_offset() == 208U);

    const auto& model = state.entries()[1U];
    CHECK(model.type() == goldsrc::ResourceType::model);
    CHECK(model.name().bytes() == "models/test_model.mdl");
    CHECK(model.index().value() == 8U);
    CHECK(model.declared_size().raw_code() == 0x00ff'ffffU);
    CHECK(model.flags().wire_value() == 0U);
    CHECK(model.source_start_bit_offset() == 208U);
    CHECK(model.source_end_bit_offset() == 428U);

    const auto& sound = state.entries()[2U];
    CHECK(sound.type() == goldsrc::ResourceType::sound);
    CHECK(sound.name().bytes() == "sound/test_sound.wav");
    CHECK(sound.index().value() == 9U);
    CHECK(sound.declared_size().raw_code() == 0x456U);
    CHECK(sound.flags().wire_value() == 1U);
    CHECK(sound.source_start_bit_offset() == 428U);
    CHECK(sound.source_end_bit_offset() == 640U);

    CHECK(state.find_exact(goldsrc::ResourceType::model, 8U) == &model);
    CHECK(state.find_exact(goldsrc::ResourceType::model, 9U) == nullptr);
}

TEST_CASE("GoldSrc resource-list test writer independently reproduces the literal",
          "[goldsrc][resource-list][fixture]")
{
    const auto generated = fixture::make_message({
        {2U, "maps/test_map.bsp", 7U, 0x12'34'56U, 1U},
        {2U, "models/test_model.mdl", 8U, 0x00ff'ffffU, 0U},
        {0U, "sound/test_sound.wav", 9U, 0x456U, 1U},
    });

    CHECK(generated.bit_length == fixture::kExactMessageBits);
    CHECK(generated.padding_start_bit == 640U);
    CHECK(generated.padding_bit_count == 8U);
    CHECK(std::equal(
        generated.bytes.begin(),
        generated.bytes.end(),
        fixture::kExactResourceListMessage.begin(),
        fixture::kExactResourceListMessage.end()));
}

TEST_CASE("GoldSrc resource-list parser honors an exact supplied opcode cursor",
          "[goldsrc][resource-list][cursor]")
{
    constexpr std::array prefix{
        std::byte{45U}, std::byte{0x10U}, std::byte{0x20U},
        std::byte{0x30U}, std::byte{0x40U}, std::byte{0x50U},
        std::byte{0x60U}, std::byte{0x70U}, std::byte{0x80U},
    };
    const auto payload = fixture::with_prefix(
        fixture::kExactResourceListMessage,
        prefix);
    const auto result = goldsrc::ResourceListParser{}.parse(
        payload,
        prefix.size());

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->source_opcode_byte_offset() == prefix.size());
    CHECK(result.bits_consumed == fixture::kExactMessageBits);
    CHECK(result.bytes_consumed == fixture::kExactResourceListMessage.size());
    CHECK(result.next_byte_offset == payload.size());
    CHECK(result.next_bit_offset == 0U);
    CHECK(result.state->entries().front().source_start_bit_offset() ==
          prefix.size() * 8U + 20U);
}

TEST_CASE("GoldSrc resource-list parser rejects every truncation transactionally",
          "[goldsrc][resource-list][truncation]")
{
    const goldsrc::ResourceListParser parser;
    for (std::size_t byte_count = 0U;
         byte_count < fixture::kExactResourceListMessage.size();
         ++byte_count) {
        INFO("truncated byte count=" << byte_count);
        const auto result = parser.parse(
            std::span<const std::byte>{
                fixture::kExactResourceListMessage.data(),
                byte_count,
            },
            0U);
        CHECK_FALSE(result);
        CHECK_FALSE(result.state);
        CHECK(result.bits_consumed == 0U);
        CHECK(result.bytes_consumed == 0U);
    }

    for (std::size_t bit_length = 0U;
         bit_length < fixture::kExactMessageBits;
         ++bit_length) {
        INFO("truncated bit length=" << bit_length);
        const auto result = parser.parse(
            fixture::kExactResourceListMessage,
            0U,
            bit_length);
        CHECK_FALSE(result);
        CHECK_FALSE(result.state);
        CHECK(result.bits_consumed == 0U);
        CHECK(result.bytes_consumed == 0U);
        CHECK(result.next_byte_offset == 0U);
        CHECK(result.next_bit_offset == 0U);
    }
}

TEST_CASE("GoldSrc resource-list parser gates count and source geometry",
          "[goldsrc][resource-list][limits]")
{
    SECTION("wrong opcode")
    {
        auto bytes = one_entry_message().bytes;
        bytes[0U] = std::byte{42U};
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::wrong_opcode);
    }

    SECTION("invalid opcode cursor")
    {
        const auto bytes = one_entry_message().bytes;
        CHECK(error_code(
                  goldsrc::ResourceListParser{}.parse(bytes, bytes.size())) ==
              goldsrc::ResourceListErrorCode::invalid_input_geometry);
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  bytes,
                  0U,
                  bytes.size() * 8U + 1U)) ==
              goldsrc::ResourceListErrorCode::invalid_input_geometry);
    }

    SECTION("empty standard profile")
    {
        const auto empty = fixture::make_message({});
        CHECK(error_code(
                  goldsrc::ResourceListParser{}.parse(empty.bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::zero_resource_count);
    }

    SECTION("count exact limit and limit plus one")
    {
        goldsrc::ResourceListLimits limits;
        limits.maximum_resource_count = 2U;
        const goldsrc::ResourceListParser parser{limits};
        const auto exact = fixture::make_message({
            {0U, "a", 1U, 0U, 0U},
            {0U, "b", 2U, 0U, 0U},
        });
        REQUIRE(parser.parse(exact.bytes, 0U));

        const auto above = fixture::make_message({
            {0U, "a", 1U, 0U, 0U},
            {0U, "b", 2U, 0U, 0U},
            {0U, "c", 3U, 0U, 0U},
        });
        CHECK(error_code(parser.parse(above.bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::resource_count_limit_exceeded);
    }
}

TEST_CASE("GoldSrc resource-list parser accepts confirmed types and raw masks only",
          "[goldsrc][resource-list][profile]")
{
    constexpr std::array accepted{
        std::pair{0U, goldsrc::ResourceType::sound},
        std::pair{2U, goldsrc::ResourceType::model},
        std::pair{3U, goldsrc::ResourceType::decal},
        std::pair{4U, goldsrc::ResourceType::generic},
        std::pair{5U, goldsrc::ResourceType::event_script},
    };
    for (const auto& [wire_type, expected] : accepted) {
        INFO("accepted type=" << wire_type);
        const auto message = one_entry_message(
            static_cast<std::uint8_t>(wire_type));
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->entries().front().type() == expected);
    }

    for (const auto wire_type : {1U, 6U, 15U}) {
        INFO("unsupported type=" << wire_type);
        const auto message = one_entry_message(
            static_cast<std::uint8_t>(wire_type));
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U)) ==
              goldsrc::ResourceListErrorCode::unsupported_resource_type);
    }

    for (const auto flags : {0U, 1U}) {
        INFO("accepted flags=" << flags);
        const auto message = one_entry_message(
            4U,
            "resource",
            1U,
            0U,
            static_cast<std::uint8_t>(flags));
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->entries().front().flags().wire_value() == flags);
    }

    for (const auto unobserved_profile_value : {2U, 4U, 8U, 15U}) {
        INFO("unobserved flags/profile slot=" << unobserved_profile_value);
        const auto message = one_entry_message(
            4U,
            "x",
            1U,
            0U,
            static_cast<std::uint8_t>(unobserved_profile_value));
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U)) ==
              goldsrc::ResourceListErrorCode::unsupported_resource_profile);
    }

    auto flags_limit = goldsrc::ResourceListLimits{};
    flags_limit.maximum_resource_flags = 0U;
    const auto observed_but_bounded = one_entry_message(4U, "x", 1U, 0U, 1U);
    CHECK(error_code(goldsrc::ResourceListParser{flags_limit}.parse(
              observed_but_bounded.bytes,
              0U)) ==
          goldsrc::ResourceListErrorCode::unsupported_resource_flags);
}

TEST_CASE("GoldSrc resource-list parser bounds names without treating them as paths",
          "[goldsrc][resource-list][names][security]")
{
    SECTION("name length exact limit and limit plus one")
    {
        goldsrc::ResourceListLimits limits;
        limits.maximum_resource_name_length = 3U;
        const goldsrc::ResourceListParser parser{limits};
        const auto exact = one_entry_message(4U, "abc");
        REQUIRE(parser.parse(exact.bytes, 0U));

        const auto above = one_entry_message(4U, "abcd");
        CHECK(error_code(parser.parse(above.bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::resource_name_too_long);
    }

    SECTION("total name length exact limit and limit plus one")
    {
        goldsrc::ResourceListLimits limits;
        limits.maximum_resource_name_length = 3U;
        limits.maximum_resource_total_name_bytes = 4U;
        const goldsrc::ResourceListParser parser{limits};
        const auto exact = fixture::make_message({
            {4U, "ab", 1U, 0U, 0U},
            {4U, "cd", 2U, 0U, 0U},
        });
        REQUIRE(parser.parse(exact.bytes, 0U));

        const auto above = fixture::make_message({
            {4U, "ab", 1U, 0U, 0U},
            {4U, "cde", 2U, 0U, 0U},
        });
        CHECK(error_code(parser.parse(above.bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::
                  total_name_bytes_limit_exceeded);
    }

    SECTION("unterminated name")
    {
        const auto message = one_entry_message(4U, "abc");
        constexpr auto name_terminator_start = 8U + 12U + 4U + 3U * 8U;
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U,
                  name_terminator_start)) ==
              goldsrc::ResourceListErrorCode::unterminated_resource_name);
    }

    SECTION("empty byte string remains bounded metadata")
    {
        const auto message = one_entry_message(4U, "");
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->entries().front().name().bytes().empty());
    }
}

TEST_CASE("GoldSrc resource-list state preserves untrusted names byte for byte",
          "[goldsrc][resource-list][names][security]")
{
    std::vector<std::string> names{
        "../evil",
        R"(..\evil)",
        R"(C:\evil)",
        R"(\\server\share)",
        "/absolute",
        "maps//double.bsp",
        "maps/./dot.bsp",
        "maps/%2e%2e/encoded",
    };
    std::string control{"control"};
    control.push_back(static_cast<char>(0x1bU));
    control.push_back(static_cast<char>(0x01U));
    names.push_back(control);
    std::string non_ascii{"bytes"};
    non_ascii.push_back(static_cast<char>(0x80U));
    non_ascii.push_back(static_cast<char>(0xffU));
    names.push_back(non_ascii);

    std::vector<fixture::EntrySpec> specs;
    specs.reserve(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        specs.push_back(fixture::EntrySpec{
            4U,
            names[index],
            static_cast<std::uint16_t>(index),
            0U,
            0U,
        });
    }
    const auto message = fixture::make_message(specs);
    const auto result = goldsrc::ResourceListParser{}.parse(
        message.bytes,
        0U);

    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.state->entries().size() == names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        INFO("untrusted metadata index=" << index);
        CHECK(result.state->entries()[index].name().bytes() == names[index]);
    }
}

TEST_CASE("GoldSrc resource-list parser enforces the confirmed identity key",
          "[goldsrc][resource-list][duplicates]")
{
    const auto duplicate = fixture::make_message({
        {2U, "models/a.mdl", 7U, 10U, 0U},
        {2U, "models/b.mdl", 7U, 20U, 0U},
    });
    CHECK(error_code(goldsrc::ResourceListParser{}.parse(
              duplicate.bytes,
              0U)) ==
          goldsrc::ResourceListErrorCode::duplicate_resource_identity);

    const auto type_scoped = fixture::make_message({
        {2U, "same", 7U, 10U, 0U},
        {0U, "same", 7U, 20U, 0U},
        {2U, "Same", 8U, 30U, 0U},
    });
    const auto accepted = goldsrc::ResourceListParser{}.parse(
        type_scoped.bytes,
        0U);
    REQUIRE(accepted);
    REQUIRE(accepted.state);
    CHECK(accepted.state->resource_count() == 3U);
    CHECK(accepted.state->entries()[0U].name().bytes() == "same");
    CHECK(accepted.state->entries()[1U].name().bytes() == "same");
    CHECK(accepted.state->entries()[2U].name().bytes() == "Same");
}

TEST_CASE("GoldSrc resource-list parser treats the 24-bit size field as raw metadata",
          "[goldsrc][resource-list][size]")
{
    SECTION("all-ones code is accepted without sentinel interpretation")
    {
        const auto message = one_entry_message(
            4U,
            "opaque",
            0x0fffU,
            0x00ff'ffffU,
            0U);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->entries().front().index().value() == 0x0fffU);
        CHECK(result.state->entries().front().declared_size().raw_code() ==
              0x00ff'ffffU);
        CHECK(result.state->total_size_code_sum() == 0x00ff'ffffU);
    }

    SECTION("per-entry code exact limit and limit plus one")
    {
        goldsrc::ResourceListLimits limits;
        limits.maximum_resource_declared_size = 100U;
        const goldsrc::ResourceListParser parser{limits};
        const auto exact = one_entry_message(4U, "x", 1U, 100U);
        REQUIRE(parser.parse(exact.bytes, 0U));
        const auto above = one_entry_message(4U, "x", 1U, 101U);
        CHECK(error_code(parser.parse(above.bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::
                  resource_declared_size_limit_exceeded);
    }

    SECTION("raw-code sum exact limit and limit plus one")
    {
        goldsrc::ResourceListLimits limits;
        limits.maximum_resource_declared_size = 101U;
        limits.maximum_resource_total_declared_size = 200U;
        const goldsrc::ResourceListParser parser{limits};
        const auto exact = fixture::make_message({
            {4U, "a", 1U, 100U, 0U},
            {4U, "b", 2U, 100U, 0U},
        });
        const auto exact_result = parser.parse(exact.bytes, 0U);
        REQUIRE(exact_result);
        REQUIRE(exact_result.state);
        CHECK(exact_result.state->total_size_code_sum() == 200U);

        const auto above = fixture::make_message({
            {4U, "a", 1U, 100U, 0U},
            {4U, "b", 2U, 101U, 0U},
        });
        CHECK(error_code(parser.parse(above.bytes, 0U)) ==
              goldsrc::ResourceListErrorCode::
                  total_declared_size_limit_exceeded);
    }
}

TEST_CASE("GoldSrc resource-list parser requires exact terminal zero fill and EOP",
          "[goldsrc][resource-list][padding]")
{
    SECTION("aligned list consumes eight zero bits")
    {
        auto message = one_entry_message(4U, "x");
        REQUIRE(message.padding_bit_count == 8U);
        REQUIRE(goldsrc::ResourceListParser{}.parse(message.bytes, 0U));

        fixture::set_bit(message.bytes, message.padding_start_bit, true);
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U)) ==
              goldsrc::ResourceListErrorCode::nonzero_padding);
    }

    SECTION("two entries consume four zero bits")
    {
        auto message = fixture::make_message({
            {4U, "x", 1U, 0U, 0U},
            {4U, "y", 2U, 0U, 0U},
        });
        REQUIRE(message.padding_bit_count == 4U);
        REQUIRE(goldsrc::ResourceListParser{}.parse(message.bytes, 0U));

        fixture::set_bit(message.bytes, message.padding_start_bit + 3U, true);
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U)) ==
              goldsrc::ResourceListErrorCode::nonzero_padding);
    }

    SECTION("terminal fill is mandatory")
    {
        const auto message = one_entry_message(4U, "x");
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U,
                  message.padding_start_bit)) ==
              goldsrc::ResourceListErrorCode::truncated_padding);
    }

    SECTION("trailing bits are not a second message or resynchronization area")
    {
        auto message = one_entry_message(4U, "x");
        message.bytes.push_back(std::byte{0U});
        CHECK(error_code(goldsrc::ResourceListParser{}.parse(
                  message.bytes,
                  0U)) ==
              goldsrc::ResourceListErrorCode::unexpected_trailing_data);
    }
}

TEST_CASE("GoldSrc resource-list project bounds validate and gate message geometry",
          "[goldsrc][resource-list][limits]")
{
    const auto message = one_entry_message(4U, "x");

    goldsrc::ResourceListLimits exact;
    exact.maximum_resource_message_bits = message.bit_length;
    exact.maximum_resource_message_bytes = message.bytes.size();
    REQUIRE(goldsrc::ResourceListParser{exact}.parse(message.bytes, 0U));

    auto one_byte_above = message.bytes;
    one_byte_above.push_back(std::byte{0U});
    CHECK(error_code(goldsrc::ResourceListParser{exact}.parse(
              one_byte_above,
              0U)) ==
          goldsrc::ResourceListErrorCode::message_too_large);

    auto below = exact;
    --below.maximum_resource_message_bits;
    CHECK(error_code(goldsrc::ResourceListParser{below}.parse(
              message.bytes,
              0U)) ==
          goldsrc::ResourceListErrorCode::message_too_large);

    auto invalid = goldsrc::ResourceListLimits{};
    invalid.maximum_resource_count = 0U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(invalid));
    CHECK(error_code(goldsrc::ResourceListParser{invalid}.parse(
              message.bytes,
              0U)) ==
          goldsrc::ResourceListErrorCode::invalid_configuration);

    invalid = goldsrc::ResourceListLimits{};
    invalid.maximum_resource_flags =
        static_cast<std::uint8_t>(goldsrc::kMaximumResourceFlags + 1U);
    CHECK_FALSE(goldsrc::valid_resource_list_limits(invalid));

    invalid = goldsrc::ResourceListLimits{};
    invalid.maximum_resource_total_name_bytes =
        invalid.maximum_resource_name_length - 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(invalid));

    invalid = goldsrc::ResourceListLimits{};
    invalid.maximum_resource_total_declared_size =
        static_cast<std::uint64_t>(invalid.maximum_resource_declared_size) - 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(invalid));
}

TEST_CASE("GoldSrc resource-list named safety limits validate defaults and hard caps",
          "[goldsrc][resource-list][limits]")
{
    const goldsrc::ResourceListLimits defaults;
    CHECK(defaults.maximum_resource_message_bits ==
          goldsrc::kDefaultMaximumResourceMessageBits);
    CHECK(defaults.maximum_resource_message_bytes ==
          goldsrc::kDefaultMaximumResourceMessageBytes);
    CHECK(defaults.maximum_resource_count ==
          goldsrc::kDefaultMaximumResourceCount);
    CHECK(defaults.maximum_resource_name_length ==
          goldsrc::kDefaultMaximumResourceNameLength);
    CHECK(defaults.maximum_resource_total_name_bytes ==
          goldsrc::kDefaultMaximumResourceTotalNameBytes);
    CHECK(defaults.maximum_resource_declared_size ==
          goldsrc::kDefaultMaximumResourceDeclaredSize);
    CHECK(defaults.maximum_resource_total_declared_size ==
          goldsrc::kDefaultMaximumResourceTotalDeclaredSize);
    CHECK(defaults.maximum_resource_flags ==
          goldsrc::kDefaultMaximumResourceFlags);
    CHECK(goldsrc::valid_resource_list_limits(defaults));

    goldsrc::ResourceListLimits hard;
    hard.maximum_resource_message_bits =
        goldsrc::kMaximumResourceMessageBits;
    hard.maximum_resource_message_bytes =
        goldsrc::kMaximumResourceMessageBytes;
    hard.maximum_resource_count = goldsrc::kMaximumResourceCount;
    hard.maximum_resource_name_length =
        goldsrc::kMaximumResourceNameLength;
    hard.maximum_resource_total_name_bytes =
        goldsrc::kMaximumResourceTotalNameBytes;
    hard.maximum_resource_declared_size =
        goldsrc::kMaximumResourceDeclaredSize;
    hard.maximum_resource_total_declared_size =
        goldsrc::kMaximumResourceTotalDeclaredSize;
    hard.maximum_resource_flags = goldsrc::kMaximumResourceFlags;
    CHECK(goldsrc::valid_resource_list_limits(hard));

    auto above = hard;
    above.maximum_resource_message_bits =
        goldsrc::kMaximumResourceMessageBits + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_message_bytes =
        goldsrc::kMaximumResourceMessageBytes + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_count = goldsrc::kMaximumResourceCount + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_name_length =
        goldsrc::kMaximumResourceNameLength + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_total_name_bytes =
        goldsrc::kMaximumResourceTotalNameBytes + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_declared_size =
        goldsrc::kMaximumResourceDeclaredSize + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_total_declared_size =
        goldsrc::kMaximumResourceTotalDeclaredSize + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));

    above = hard;
    above.maximum_resource_flags = static_cast<std::uint8_t>(
        goldsrc::kMaximumResourceFlags + 1U);
    CHECK_FALSE(goldsrc::valid_resource_list_limits(above));
}

TEST_CASE("GoldSrc resource-list differential fixtures change only typed list metadata",
          "[goldsrc][resource-list][differential]")
{
    const std::vector<fixture::EntrySpec> baseline_specs{
        {2U, "maps/test_map.bsp", 7U, 100U, 1U},
        {2U, "models/test_model.mdl", 8U, 200U, 0U},
        {0U, "sound/test_sound.wav", 9U, 300U, 1U},
    };
    const auto baseline_message = fixture::make_message(baseline_specs);
    const auto baseline_result = goldsrc::ResourceListParser{}.parse(
        baseline_message.bytes,
        0U);
    REQUIRE(baseline_result);
    REQUIRE(baseline_result.state);
    const auto& baseline = *baseline_result.state;
    REQUIRE(baseline.resource_count() == 3U);
    CHECK(baseline.entries()[0U].type() == goldsrc::ResourceType::model);

    SECTION("map resource name changes while its confirmed model type remains stable")
    {
        auto specs = baseline_specs;
        specs[0U].name = "maps/test_map_changed.bsp";
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        CHECK(changed.resource_count() == baseline.resource_count());
        CHECK(changed.entries()[0U].type() == goldsrc::ResourceType::model);
        CHECK(changed.entries()[0U].name().bytes() ==
              "maps/test_map_changed.bsp");
        CHECK(changed.entries()[0U].index().value() ==
              baseline.entries()[0U].index().value());
        CHECK(changed.entries()[0U].declared_size().raw_code() ==
              baseline.entries()[0U].declared_size().raw_code());
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 2U);
        CHECK(changed.total_size_code_sum() == baseline.total_size_code_sum());
    }

    SECTION("adding a model changes count, order tail, and model summary only")
    {
        auto specs = baseline_specs;
        specs.push_back({2U, "models/test_added.mdl", 10U, 400U, 0U});
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        REQUIRE(changed.resource_count() == baseline.resource_count() + 1U);
        CHECK(changed.entries().back().type() == goldsrc::ResourceType::model);
        CHECK(changed.entries().back().wire_ordinal() == 3U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 3U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::sound) == 1U);
        CHECK(changed.total_size_code_sum() ==
              baseline.total_size_code_sum() + 400U);
    }

    SECTION("adding a sound changes count, order tail, and sound summary only")
    {
        auto specs = baseline_specs;
        specs.push_back({0U, "sound/test_added.wav", 10U, 500U, 0U});
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        REQUIRE(changed.resource_count() == baseline.resource_count() + 1U);
        CHECK(changed.entries().back().type() == goldsrc::ResourceType::sound);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 2U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::sound) == 2U);
        CHECK(changed.total_size_code_sum() ==
              baseline.total_size_code_sum() + 500U);
    }

    SECTION("changing the opaque 24-bit size code changes only that value and sum")
    {
        auto specs = baseline_specs;
        specs[1U].size_code = 777U;
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        CHECK(changed.entries()[1U].type() == baseline.entries()[1U].type());
        CHECK(changed.entries()[1U].name().bytes() ==
              baseline.entries()[1U].name().bytes());
        CHECK(changed.entries()[1U].index().value() ==
              baseline.entries()[1U].index().value());
        CHECK(changed.entries()[1U].declared_size().raw_code() == 777U);
        CHECK(changed.total_size_code_sum() ==
              baseline.total_size_code_sum() - 200U + 777U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 2U);
    }

    SECTION("changing a numeric type changes only the typed category and summary")
    {
        auto specs = baseline_specs;
        specs[1U].type = 4U;
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        CHECK(changed.entries()[1U].type() == goldsrc::ResourceType::generic);
        CHECK(changed.entries()[1U].name().bytes() ==
              baseline.entries()[1U].name().bytes());
        CHECK(changed.entries()[1U].index().value() ==
              baseline.entries()[1U].index().value());
        CHECK(changed.entries()[1U].declared_size().raw_code() ==
              baseline.entries()[1U].declared_size().raw_code());
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 1U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::generic) == 1U);
        CHECK(changed.total_size_code_sum() == baseline.total_size_code_sum());
    }

    SECTION("reordering preserves aggregates and publishes exact wire order")
    {
        auto specs = baseline_specs;
        std::swap(specs[0U], specs[2U]);
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        REQUIRE(changed.resource_count() == baseline.resource_count());
        CHECK(changed.entries()[0U].name().bytes() ==
              baseline.entries()[2U].name().bytes());
        CHECK(changed.entries()[1U].name().bytes() ==
              baseline.entries()[1U].name().bytes());
        CHECK(changed.entries()[2U].name().bytes() ==
              baseline.entries()[0U].name().bytes());
        CHECK(changed.entries()[0U].wire_ordinal() == 0U);
        CHECK(changed.entries()[2U].wire_ordinal() == 2U);
        CHECK(changed.total_size_code_sum() == baseline.total_size_code_sum());
        CHECK(changed.total_name_byte_count() ==
              baseline.total_name_byte_count());
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 2U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::sound) == 1U);
    }

    SECTION("a shorter count removes only the omitted tail entry and aggregates")
    {
        auto specs = baseline_specs;
        specs.pop_back();
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        const auto& changed = *result.state;
        REQUIRE(changed.resource_count() == 2U);
        CHECK(changed.entries()[0U].name().bytes() ==
              baseline.entries()[0U].name().bytes());
        CHECK(changed.entries()[1U].name().bytes() ==
              baseline.entries()[1U].name().bytes());
        CHECK(changed.type_summary().count(goldsrc::ResourceType::model) == 2U);
        CHECK(changed.type_summary().count(goldsrc::ResourceType::sound) == 0U);
        CHECK(changed.total_size_code_sum() ==
              baseline.total_size_code_sum() - 300U);
    }

    SECTION("profile or optional-field presence remains typed unsupported and unpublished")
    {
        auto specs = baseline_specs;
        specs[1U].flags = 2U;
        const auto message = fixture::make_message(specs);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        CHECK(error_code(result) ==
              goldsrc::ResourceListErrorCode::unsupported_resource_profile);
        REQUIRE(result.error);
        CHECK(result.error->entry_index == 1U);
    }
}

TEST_CASE("GoldSrc resource-list parser covers every observed standard count profile",
          "[goldsrc][resource-list][stock-profile]")
{
    for (const auto count : {540U, 607U, 532U}) {
        INFO("observed entry count=" << count);
        std::vector<fixture::EntrySpec> entries;
        entries.reserve(count);
        for (std::uint16_t index = 0U; index < count; ++index) {
            entries.push_back(fixture::EntrySpec{
                static_cast<std::uint8_t>(index % 5U == 1U ? 2U : 4U),
                "r",
                index,
                index,
                static_cast<std::uint8_t>(index & 1U),
            });
        }
        const auto message = fixture::make_message(entries);
        const auto result = goldsrc::ResourceListParser{}.parse(
            message.bytes,
            0U);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->resource_count() == count);
    }
}

TEST_CASE("GoldSrc resource-list state owns storage and exposes immutable publication",
          "[goldsrc][resource-list][state]")
{
    static_assert(std::is_copy_constructible_v<goldsrc::ResourceListState>);
    static_assert(std::is_move_constructible_v<goldsrc::ResourceListState>);
    static_assert(!std::is_copy_assignable_v<goldsrc::ResourceListState>);
    static_assert(!std::is_move_assignable_v<goldsrc::ResourceListState>);
    static_assert(!std::is_constructible_v<goldsrc::ResourceName, std::string>);
    static_assert(std::is_copy_constructible_v<goldsrc::ResourceTypeSummary>);
    static_assert(std::is_move_constructible_v<goldsrc::ResourceTypeSummary>);
    static_assert(!std::is_copy_assignable_v<goldsrc::ResourceTypeSummary>);
    static_assert(!std::is_move_assignable_v<goldsrc::ResourceTypeSummary>);
    static_assert(!std::is_copy_assignable_v<goldsrc::ResourceTypeCount>);
    static_assert(!std::is_move_assignable_v<goldsrc::ResourceTypeCount>);

    std::optional<goldsrc::ResourceListState> published;
    {
        auto source = one_entry_message(
            4U,
            "maps/owned_metadata.bsp",
            7U,
            0x00ff'ffffU,
            1U).bytes;
        auto parsed = goldsrc::ResourceListParser{}.parse(source, 0U);
        REQUIRE(parsed);
        REQUIRE(parsed.state);
        published.emplace(std::move(*parsed.state));
        std::fill(source.begin(), source.end(), std::byte{0U});
    }

    REQUIRE(published);
    REQUIRE(published->resource_count() == 1U);
    CHECK(published->entries().front().name().bytes() ==
          "maps/owned_metadata.bsp");
    CHECK(published->entries().front().declared_size().raw_code() ==
          0x00ff'ffffU);
}

} // namespace
