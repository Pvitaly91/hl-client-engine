#include <hlclient/goldsrc/info_string.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <locale>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::string containing_byte(const unsigned char value)
{
    std::string text{"before"};
    text.push_back(static_cast<char>(value));
    text += "after";
    return text;
}

void check_build_error(
    std::vector<goldsrc::InfoStringEntry> entries,
    const goldsrc::InfoStringErrorCode expected)
{
    const auto result = goldsrc::build_info_string(entries, goldsrc::InfoStringLimits{});
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

void check_parse_error(
    const std::string_view serialized,
    const goldsrc::InfoStringErrorCode expected)
{
    const auto result = goldsrc::parse_info_string(serialized, goldsrc::InfoStringLimits{});
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

class HostileCType final : public std::ctype<char> {
public:
    HostileCType()
        : std::ctype<char>{nullptr, false, 0U}
    {
    }

protected:
    [[nodiscard]] char do_tolower(const char character) const override
    {
        return character == 'N' ? 'x' : character;
    }

    const char* do_tolower(char* first, const char* last) const override
    {
        for (auto* current = first; current != last; ++current) {
            *current = do_tolower(*current);
        }
        return last;
    }
};

class GlobalLocaleGuard final {
public:
    explicit GlobalLocaleGuard(std::locale replacement)
        : previous_{std::locale::global(std::move(replacement))}
    {
    }

    ~GlobalLocaleGuard()
    {
        std::locale::global(previous_);
    }

    GlobalLocaleGuard(const GlobalLocaleGuard&) = delete;
    GlobalLocaleGuard& operator=(const GlobalLocaleGuard&) = delete;

private:
    std::locale previous_;
};

TEST_CASE("Info string has an explicit empty representation", "[goldsrc][info-string]")
{
    const std::vector<goldsrc::InfoStringEntry> no_entries;
    const auto built = goldsrc::build_info_string(no_entries, goldsrc::InfoStringLimits{});

    REQUIRE(built);
    REQUIRE(built.value);
    CHECK(built.value->empty());
    CHECK(built.value->entries().empty());
    CHECK(built.value->serialized().empty());
    CHECK(built.value->serialized_size() == 0U);

    const auto parsed = goldsrc::parse_info_string({}, goldsrc::InfoStringLimits{});
    REQUIRE(parsed);
    REQUIRE(parsed.value);
    CHECK(parsed.value->empty());
    CHECK(parsed.value->serialized().empty());
}

TEST_CASE("Info string serializes one key and value exactly", "[goldsrc][info-string]")
{
    const std::array entries{goldsrc::InfoStringEntry{"name", "TestPlayer"}};
    const auto result = goldsrc::build_info_string(entries, goldsrc::InfoStringLimits{});

    REQUIRE(result);
    REQUIRE(result.value);
    CHECK(result.value->serialized() == "\\name\\TestPlayer");
    CHECK(result.value->entries().size() == 1U);
    CHECK(result.value->entries().front() == entries.front());
}

TEST_CASE("Info string preserves ordered entries deterministically", "[goldsrc][info-string]")
{
    const std::array entries{
        goldsrc::InfoStringEntry{"model", "gordon"},
        goldsrc::InfoStringEntry{"name", "TestPlayer"},
        goldsrc::InfoStringEntry{"rate", "25000"},
    };

    const auto first = goldsrc::build_info_string(entries, goldsrc::InfoStringLimits{});
    const auto second = goldsrc::build_info_string(entries, goldsrc::InfoStringLimits{});

    REQUIRE(first);
    REQUIRE(first.value);
    REQUIRE(second);
    REQUIRE(second.value);
    CHECK(first.value->serialized() ==
          "\\model\\gordon\\name\\TestPlayer\\rate\\25000");
    CHECK(second.value->serialized() == first.value->serialized());
    CHECK(std::ranges::equal(first.value->entries(), entries));
}

TEST_CASE("Info string build and parse round-trip without reordering", "[goldsrc][info-string]")
{
    const std::array entries{
        goldsrc::InfoStringEntry{"prot", "3"},
        goldsrc::InfoStringEntry{"unique", "-1"},
        goldsrc::InfoStringEntry{"raw", "TEST_AUTH_MATERIAL"},
    };
    const auto built = goldsrc::build_info_string(entries, goldsrc::InfoStringLimits{});
    REQUIRE(built);
    REQUIRE(built.value);

    const auto parsed = goldsrc::parse_info_string(
        built.value->serialized(), goldsrc::InfoStringLimits{});

    REQUIRE(parsed);
    REQUIRE(parsed.value);
    CHECK(parsed.value->serialized() == built.value->serialized());
    CHECK(std::ranges::equal(parsed.value->entries(), entries));
}

TEST_CASE("Info string rejects exact and ASCII case-folded duplicate keys",
          "[goldsrc][info-string]")
{
    SECTION("exact duplicate with the same value")
    {
        check_build_error(
            {{"name", "one"}, {"name", "one"}},
            goldsrc::InfoStringErrorCode::duplicate_key);
    }

    SECTION("exact duplicate with a different value")
    {
        check_build_error(
            {{"name", "one"}, {"name", "two"}},
            goldsrc::InfoStringErrorCode::duplicate_key);
    }

    SECTION("ASCII case variant")
    {
        check_build_error(
            {{"name", "one"}, {"NAME", "two"}},
            goldsrc::InfoStringErrorCode::duplicate_key);
        check_parse_error(
            "\\name\\one\\NAME\\two",
            goldsrc::InfoStringErrorCode::duplicate_key);
    }
}

TEST_CASE("Info string rejects empty keys and values", "[goldsrc][info-string]")
{
    check_build_error({{"", "value"}}, goldsrc::InfoStringErrorCode::empty_key);
    check_build_error({{"key", ""}}, goldsrc::InfoStringErrorCode::empty_value);

    check_parse_error("\\\\value", goldsrc::InfoStringErrorCode::empty_key);
    check_parse_error("\\key\\", goldsrc::InfoStringErrorCode::empty_value);
}

TEST_CASE("Info string rejects structural and control-byte injection",
          "[goldsrc][info-string]")
{
    const std::array forbidden{
        static_cast<unsigned char>('\\'),
        static_cast<unsigned char>('\"'),
        static_cast<unsigned char>('\n'),
        static_cast<unsigned char>('\r'),
        static_cast<unsigned char>('\0'),
        static_cast<unsigned char>(0x01U),
        static_cast<unsigned char>(0x7fU),
        static_cast<unsigned char>(0x80U),
    };

    for (const auto byte : forbidden) {
        INFO("forbidden byte " << static_cast<unsigned int>(byte));
        check_build_error(
            {{containing_byte(byte), "value"}},
            goldsrc::InfoStringErrorCode::invalid_key_character);
        check_build_error(
            {{"key", containing_byte(byte)}},
            goldsrc::InfoStringErrorCode::invalid_value_character);
    }
}

TEST_CASE("Info string semicolon policy rejects command separators",
          "[goldsrc][info-string]")
{
    check_build_error(
        {{"na;me", "player"}}, goldsrc::InfoStringErrorCode::invalid_key_character);
    check_build_error(
        {{"name", "player;quit"}}, goldsrc::InfoStringErrorCode::invalid_value_character);
    check_parse_error(
        "\\na;me\\player", goldsrc::InfoStringErrorCode::invalid_key_character);
    check_parse_error(
        "\\name\\player;quit", goldsrc::InfoStringErrorCode::invalid_value_character);
}

TEST_CASE("Info string space policy rejects key spaces and preserves value spaces",
          "[goldsrc][info-string]")
{
    check_build_error(
        {{"display name", "player"}},
        goldsrc::InfoStringErrorCode::invalid_key_character);

    const std::array entries{
        goldsrc::InfoStringEntry{"name", " Test Player "},
        goldsrc::InfoStringEntry{"label", " "},
    };
    const auto built = goldsrc::build_info_string(entries, goldsrc::InfoStringLimits{});

    REQUIRE(built);
    REQUIRE(built.value);
    CHECK(built.value->serialized() == "\\name\\ Test Player \\label\\ ");

    const auto parsed = goldsrc::parse_info_string(
        built.value->serialized(), goldsrc::InfoStringLimits{});
    REQUIRE(parsed);
    REQUIRE(parsed.value);
    CHECK(std::ranges::equal(parsed.value->entries(), entries));
}

TEST_CASE("Info string parser enforces leading and trailing separator policy",
          "[goldsrc][info-string]")
{
    check_parse_error(
        "name\\player", goldsrc::InfoStringErrorCode::missing_leading_separator);
    check_parse_error(
        "\\name\\player\\", goldsrc::InfoStringErrorCode::trailing_separator);
    check_parse_error("\\", goldsrc::InfoStringErrorCode::trailing_separator);
}

TEST_CASE("Info string parser rejects odd or incomplete components",
          "[goldsrc][info-string]")
{
    check_parse_error("\\name", goldsrc::InfoStringErrorCode::missing_value);
    check_parse_error("\\name\\", goldsrc::InfoStringErrorCode::empty_value);
    check_parse_error(
        "\\name\\player\\model", goldsrc::InfoStringErrorCode::missing_value);
    check_parse_error(
        "\\name\\player\\\\value", goldsrc::InfoStringErrorCode::empty_key);
}

TEST_CASE("Info string accepts its maximum key length and rejects one byte more",
          "[goldsrc][info-string]")
{
    const goldsrc::InfoStringLimits limits{};
    const auto at_limit = goldsrc::build_info_string(
        std::array{goldsrc::InfoStringEntry{
            std::string(limits.maximum_key_length, 'k'), "v"}},
        limits);
    REQUIRE(at_limit);

    const auto over_limit = goldsrc::build_info_string(
        std::array{goldsrc::InfoStringEntry{
            std::string(limits.maximum_key_length + 1U, 'k'), "v"}},
        limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error);
    CHECK(over_limit.error->code == goldsrc::InfoStringErrorCode::key_too_long);
}

TEST_CASE("Info string accepts its maximum value length and rejects one byte more",
          "[goldsrc][info-string]")
{
    const goldsrc::InfoStringLimits limits{};
    const auto at_limit = goldsrc::build_info_string(
        std::array{goldsrc::InfoStringEntry{
            "key", std::string(limits.maximum_value_length, 'v')}},
        limits);
    REQUIRE(at_limit);

    const auto over_limit = goldsrc::build_info_string(
        std::array{goldsrc::InfoStringEntry{
            "key", std::string(limits.maximum_value_length + 1U, 'v')}},
        limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error);
    CHECK(over_limit.error->code == goldsrc::InfoStringErrorCode::value_too_long);
}

TEST_CASE("Info string enforces its exact serialized-size limit", "[goldsrc][info-string]")
{
    const goldsrc::InfoStringLimits limits{};
    const std::array at_limit_entries{
        goldsrc::InfoStringEntry{
            std::string(limits.maximum_key_length, 'k'),
            std::string(limits.maximum_value_length, 'v')},
        goldsrc::InfoStringEntry{"x", std::string(60U, 'z')},
    };
    const auto at_limit = goldsrc::build_info_string(at_limit_entries, limits);

    REQUIRE(at_limit);
    REQUIRE(at_limit.value);
    REQUIRE(at_limit.value->serialized_size() == limits.maximum_serialized_length);
    const auto parsed_at_limit = goldsrc::parse_info_string(
        at_limit.value->serialized(), limits);
    REQUIRE(parsed_at_limit);

    auto over_limit_entries = at_limit_entries;
    over_limit_entries.back().value.push_back('z');
    const auto over_limit = goldsrc::build_info_string(over_limit_entries, limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error);
    CHECK(over_limit.error->code ==
          goldsrc::InfoStringErrorCode::serialized_size_exceeded);

    auto over_limit_wire = std::string{at_limit.value->serialized()};
    over_limit_wire.push_back('z');
    check_parse_error(
        over_limit_wire, goldsrc::InfoStringErrorCode::serialized_size_exceeded);
}

TEST_CASE("Info string enforces its exact entry-count limit", "[goldsrc][info-string]")
{
    const goldsrc::InfoStringLimits limits{};
    std::vector<goldsrc::InfoStringEntry> entries;
    entries.reserve(limits.maximum_entry_count + 1U);
    for (std::size_t index = 0U; index < limits.maximum_entry_count; ++index) {
        entries.push_back(goldsrc::InfoStringEntry{
            "k" + std::to_string(index), "v"});
    }

    const auto at_limit = goldsrc::build_info_string(entries, limits);
    REQUIRE(at_limit);
    REQUIRE(at_limit.value);
    const auto parsed_at_limit = goldsrc::parse_info_string(
        at_limit.value->serialized(), limits);
    REQUIRE(parsed_at_limit);

    entries.push_back(goldsrc::InfoStringEntry{"overflow", "v"});
    const auto over_limit = goldsrc::build_info_string(entries, limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error);
    CHECK(over_limit.error->code == goldsrc::InfoStringErrorCode::too_many_entries);
}

TEST_CASE("Info string checks every truncated byte prefix and permits standalone values",
          "[goldsrc][info-string]")
{
    constexpr std::string_view complete{"\\a\\one\\b\\two"};

    for (std::size_t length = 0U; length < complete.size(); ++length) {
        const auto prefix = complete.substr(0U, length);
        const auto separator_count = static_cast<std::size_t>(
            std::ranges::count(prefix, '\\'));

        // Info strings carry no terminator. The empty prefix and a prefix ending
        // inside a non-empty value are therefore valid standalone info strings.
        const bool valid_standalone_prefix = prefix.empty() ||
            (separator_count % 2U == 0U && prefix.back() != '\\');
        const auto parsed = goldsrc::parse_info_string(prefix, goldsrc::InfoStringLimits{});

        INFO("truncation length " << length);
        if (valid_standalone_prefix) {
            REQUIRE(parsed);
            REQUIRE(parsed.value);
            CHECK(parsed.value->serialized() == prefix);
        } else {
            CHECK_FALSE(parsed);
            CHECK(parsed.error.has_value());
        }
    }
}

TEST_CASE("Info string validation is independent of the process locale",
          "[goldsrc][info-string]")
{
    const GlobalLocaleGuard locale_guard{
        std::locale{std::locale::classic(), new HostileCType{}}};
    CHECK(std::tolower('N', std::locale{}) == 'x');

    const std::array valid_entries{
        goldsrc::InfoStringEntry{"Name", "Player"},
        goldsrc::InfoStringEntry{"MODEL", "gordon"},
    };
    const auto valid = goldsrc::build_info_string(
        valid_entries, goldsrc::InfoStringLimits{});
    REQUIRE(valid);
    REQUIRE(valid.value);
    CHECK(valid.value->serialized() == "\\Name\\Player\\MODEL\\gordon");

    check_build_error(
        {{"name", "one"}, {"NAME", "two"}},
        goldsrc::InfoStringErrorCode::duplicate_key);
}

TEST_CASE("Info string values own source and parsed storage", "[goldsrc][info-string]")
{
    std::vector<goldsrc::InfoStringEntry> source{
        {"name", "Owned Player"},
        {"model", "gordon"},
    };
    auto built = goldsrc::build_info_string(source, goldsrc::InfoStringLimits{});
    REQUIRE(built);
    REQUIRE(built.value);

    source.front().key.assign("changed");
    source.front().value.assign("changed");
    source.clear();
    CHECK(built.value->serialized() == "\\name\\Owned Player\\model\\gordon");
    REQUIRE(built.value->entries().size() == 2U);
    CHECK(built.value->entries().front().key == "name");
    CHECK(built.value->entries().front().value == "Owned Player");

    std::string serialized{"\\name\\Parsed Player\\model\\gordon"};
    auto parsed = goldsrc::parse_info_string(serialized, goldsrc::InfoStringLimits{});
    REQUIRE(parsed);
    REQUIRE(parsed.value);
    serialized.assign(serialized.size(), 'x');
    CHECK(parsed.value->serialized() == "\\name\\Parsed Player\\model\\gordon");
    REQUIRE(parsed.value->entries().size() == 2U);
    CHECK(parsed.value->entries().front().value == "Parsed Player");
}

} // namespace
