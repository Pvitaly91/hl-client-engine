#include <hlclient/goldsrc/resource_transition_request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

inline constexpr std::array<std::byte, 9U> kIndependentRequestFixture{
    std::byte{0x03U},
    std::byte{0x73U},
    std::byte{0x65U},
    std::byte{0x6eU},
    std::byte{0x64U},
    std::byte{0x72U},
    std::byte{0x65U},
    std::byte{0x73U},
    std::byte{0x00U},
};

template<typename Type>
concept HasCommandGetter = requires(const Type& value) {
    value.command();
};

template<typename Type>
concept HasRawStringGetter = requires(const Type& value) {
    value.raw_string();
};

template<typename Type>
concept AcceptsArbitraryCommand = requires(
    const Type& value,
    const std::string_view command) {
    value.build(command);
};

void check_error(
    const goldsrc::ResourceTransitionRequestParseResult& result,
    const goldsrc::ResourceTransitionRequestErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <=
          goldsrc::kResourceTransitionRequestDiagnosticTextLimit);
    CHECK_FALSE(result.request);
    CHECK(result.bytes_consumed == 0U);
    CHECK(result.next_byte_offset == 0U);
}

TEST_CASE(
    "Resource-transition builder matches the independent exact stock fixture",
    "[goldsrc][resource-transition][request][fixture]")
{
    const auto built = goldsrc::ResourceTransitionRequestBuilder{}.build();
    REQUIRE(built);
    REQUIRE(built.request);
    CHECK_FALSE(built.error);

    const auto& request = *built.request;
    CHECK(request.opcode() == goldsrc::ClientMessageOpcode::string_command);
    CHECK(request.message_bytes() == kIndependentRequestFixture.size());
    CHECK(std::ranges::equal(request.bytes(), kIndependentRequestFixture));
    CHECK(request.bytes().front() == std::byte{3U});
    CHECK(request.bytes().back() == std::byte{0U});
    CHECK(request.compatibility_profile() ==
          goldsrc::ResourceTransitionRequestCompatibilityProfile::
              stock_protocol_48_build_10210);
    CHECK(request.evidence_profile() ==
          goldsrc::ResourceTransitionRequestEvidenceProfile::
              repeated_signed_stock_capture);

    STATIC_CHECK_FALSE(
        std::is_default_constructible_v<goldsrc::ResourceTransitionRequest>);
    STATIC_CHECK_FALSE(
        std::is_copy_assignable_v<goldsrc::ResourceTransitionRequest>);
    STATIC_CHECK_FALSE(
        std::is_move_assignable_v<goldsrc::ResourceTransitionRequest>);
    STATIC_CHECK_FALSE(HasCommandGetter<goldsrc::ResourceTransitionRequest>);
    STATIC_CHECK_FALSE(HasRawStringGetter<goldsrc::ResourceTransitionRequest>);
    STATIC_CHECK_FALSE(
        AcceptsArbitraryCommand<goldsrc::ResourceTransitionRequestBuilder>);
}

TEST_CASE(
    "Resource-transition parser round-trips the exact request and owns it",
    "[goldsrc][resource-transition][request][roundtrip][ownership]")
{
    auto source = std::vector<std::byte>{
        kIndependentRequestFixture.begin(),
        kIndependentRequestFixture.end()};
    const auto parsed = goldsrc::ResourceTransitionRequestParser{}.parse(source);
    REQUIRE(parsed);
    REQUIRE(parsed.request);
    CHECK_FALSE(parsed.error);
    CHECK(parsed.bytes_consumed == kIndependentRequestFixture.size());
    CHECK(parsed.next_byte_offset == kIndependentRequestFixture.size());
    CHECK(parsed.request->source_message_offset() == 0U);

    std::ranges::fill(source, std::byte{0xa5U});
    CHECK(std::ranges::equal(
        parsed.request->bytes(),
        kIndependentRequestFixture));

    const auto rebuilt = goldsrc::ResourceTransitionRequestBuilder{}.build();
    REQUIRE(rebuilt);
    REQUIRE(rebuilt.request);
    CHECK(std::ranges::equal(
        rebuilt.request->bytes(),
        parsed.request->bytes()));
}

TEST_CASE(
    "Every truncated resource-transition request prefix fails transactionally",
    "[goldsrc][resource-transition][request][truncation]")
{
    const goldsrc::ResourceTransitionRequestParser parser;
    for (std::size_t size = 0U;
         size < kIndependentRequestFixture.size();
         ++size) {
        INFO("prefix size " << size);
        const auto result = parser.parse(
            std::span{kIndependentRequestFixture}.first(size));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->byte_offset <= size);
        CHECK_FALSE(result.request);
        CHECK(result.bytes_consumed == 0U);
        CHECK(result.next_byte_offset == 0U);
    }
}

TEST_CASE(
    "Resource-transition request enforces opcode command case and terminator",
    "[goldsrc][resource-transition][request][strict]")
{
    auto wrong_opcode = kIndependentRequestFixture;
    wrong_opcode[0U] = std::byte{2U};
    check_error(
        goldsrc::ResourceTransitionRequestParser{}.parse(wrong_opcode),
        goldsrc::ResourceTransitionRequestErrorCode::wrong_opcode,
        wrong_opcode.size());

    for (std::size_t index = 1U;
         index <= goldsrc::kResourceTransitionRequestCommandLength;
         ++index) {
        auto wrong_command = kIndependentRequestFixture;
        wrong_command[index] ^= std::byte{0x20U};
        INFO("command byte " << index);
        check_error(
            goldsrc::ResourceTransitionRequestParser{}.parse(wrong_command),
            goldsrc::ResourceTransitionRequestErrorCode::
                unsupported_command_variant,
            wrong_command.size());
    }

    auto early_terminator = kIndependentRequestFixture;
    early_terminator[4U] = std::byte{0U};
    check_error(
        goldsrc::ResourceTransitionRequestParser{}.parse(early_terminator),
        goldsrc::ResourceTransitionRequestErrorCode::unexpected_terminator,
        early_terminator.size());

    const auto missing_terminator = std::span{kIndependentRequestFixture}.first(
        kIndependentRequestFixture.size() - 1U);
    check_error(
        goldsrc::ResourceTransitionRequestParser{}.parse(missing_terminator),
        goldsrc::ResourceTransitionRequestErrorCode::missing_terminator,
        missing_terminator.size());

    auto non_nul_terminator = kIndependentRequestFixture;
    non_nul_terminator.back() = std::byte{'x'};
    check_error(
        goldsrc::ResourceTransitionRequestParser{}.parse(non_nul_terminator),
        goldsrc::ResourceTransitionRequestErrorCode::missing_terminator,
        non_nul_terminator.size());
}

TEST_CASE(
    "Resource-transition parser leaves duplicate terminators and trailing messages unconsumed",
    "[goldsrc][resource-transition][request][cursor][padding]")
{
    std::vector<std::byte> duplicate_terminator{
        kIndependentRequestFixture.begin(),
        kIndependentRequestFixture.end()};
    duplicate_terminator.push_back(std::byte{0U});

    const auto duplicate =
        goldsrc::ResourceTransitionRequestParser{}.parse(duplicate_terminator);
    REQUIRE(duplicate);
    CHECK(duplicate.bytes_consumed == kIndependentRequestFixture.size());
    CHECK(duplicate.next_byte_offset == kIndependentRequestFixture.size());
    CHECK(duplicate_terminator[duplicate.next_byte_offset] == std::byte{0U});

    std::vector<std::byte> piggybacked{
        std::byte{0xa5U},
        std::byte{0x5aU},
    };
    piggybacked.insert(
        piggybacked.end(),
        kIndependentRequestFixture.begin(),
        kIndependentRequestFixture.end());
    const std::array trailing_message{
        std::byte{3U},
        std::byte{'x'},
        std::byte{0U},
    };
    piggybacked.insert(
        piggybacked.end(),
        trailing_message.begin(),
        trailing_message.end());

    const auto result =
        goldsrc::ResourceTransitionRequestParser{}.parse(piggybacked, 2U);
    REQUIRE(result);
    REQUIRE(result.request);
    CHECK(result.request->source_message_offset() == 2U);
    CHECK(result.bytes_consumed == kIndependentRequestFixture.size());
    CHECK(result.next_byte_offset == 2U + kIndependentRequestFixture.size());
    CHECK(piggybacked[result.next_byte_offset] == trailing_message.front());
    CHECK(std::ranges::equal(
        std::span{piggybacked}.subspan(
            result.next_byte_offset,
            trailing_message.size()),
        trailing_message));
}

TEST_CASE(
    "Resource-transition request project size limit is exact and hard capped",
    "[goldsrc][resource-transition][request][limits]")
{
    STATIC_CHECK(
        goldsrc::kDefaultMaximumResourceTransitionRequestSize ==
        goldsrc::kResourceTransitionRequestSize);
    STATIC_CHECK(
        goldsrc::kMaximumResourceTransitionRequestSize ==
        goldsrc::kResourceTransitionRequestSize);
    CHECK(goldsrc::valid_resource_transition_request_limits({}));
    CHECK(goldsrc::valid_resource_transition_request_limits({
        goldsrc::kMaximumResourceTransitionRequestSize,
    }));
    CHECK_FALSE(goldsrc::valid_resource_transition_request_limits({0U}));
    CHECK_FALSE(goldsrc::valid_resource_transition_request_limits({
        goldsrc::kMaximumResourceTransitionRequestSize - 1U,
    }));
    CHECK_FALSE(goldsrc::valid_resource_transition_request_limits({
        goldsrc::kMaximumResourceTransitionRequestSize + 1U,
    }));

    const goldsrc::ResourceTransitionRequestBuilder invalid_builder{{0U}};
    const auto build = invalid_builder.build();
    REQUIRE_FALSE(build);
    REQUIRE(build.error);
    CHECK(build.error->code ==
          goldsrc::ResourceTransitionRequestErrorCode::invalid_configuration);
    CHECK_FALSE(build.request);

    const goldsrc::ResourceTransitionRequestParser invalid_parser{{0U}};
    check_error(
        invalid_parser.parse(kIndependentRequestFixture),
        goldsrc::ResourceTransitionRequestErrorCode::invalid_configuration,
        kIndependentRequestFixture.size());

    const goldsrc::ResourceTransitionRequestParser unsupported_profile{
        {},
        static_cast<goldsrc::ResourceTransitionRequestCompatibilityProfile>(
            0xffU),
    };
    CHECK_FALSE(unsupported_profile.valid_configuration());
    check_error(
        unsupported_profile.parse(kIndependentRequestFixture),
        goldsrc::ResourceTransitionRequestErrorCode::invalid_configuration,
        kIndependentRequestFixture.size());
}

} // namespace
