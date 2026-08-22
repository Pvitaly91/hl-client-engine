#include <hlclient/goldsrc/resource_transition_control.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

// Independently authored sanitized fixture: exact opcode-45 body followed by
// the confirmed boundary opcode and one synthetic, deliberately unparsed byte.
inline constexpr std::array<std::byte, 11U> kIndependentControlFixture{
    std::byte{45U},
    std::byte{1U},
    std::byte{0U},
    std::byte{0U},
    std::byte{0U},
    std::byte{0U},
    std::byte{0U},
    std::byte{0U},
    std::byte{0U},
    std::byte{43U},
    std::byte{0xa5U},
};

template<typename Type>
concept HasOpaqueCounterGetter = requires(const Type& value) {
    value.opaque_counter();
};

template<typename Type>
concept HasReservedGetter = requires(const Type& value) {
    value.reserved_value();
};

void overwrite_u32_le(
    std::array<std::byte, 11U>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    REQUIRE(offset + 4U <= bytes.size());
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

void check_error(
    const goldsrc::ResourceTransitionControlParseResult& result,
    const goldsrc::ResourceTransitionControlErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <=
          goldsrc::kResourceTransitionControlDiagnosticTextLimit);
    CHECK_FALSE(result.state);
    CHECK_FALSE(result.boundary);
    CHECK(result.bytes_consumed == 0U);
    CHECK(result.next_byte_offset == 0U);
}

TEST_CASE(
    "Transition-control parser consumes the exact eight-byte body and stops before opcode 43",
    "[goldsrc][resource-transition][control][fixture][cursor]")
{
    const auto result = goldsrc::ResourceTransitionControlParser{}.parse(
        kIndependentControlFixture);
    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.boundary);
    CHECK_FALSE(result.error);

    CHECK(result.state->opcode() == 45U);
    CHECK(result.state->source_message_offset() == 0U);
    CHECK(result.state->body_bytes() == 8U);
    CHECK(result.state->message_bytes() == 9U);
    CHECK(result.state->compatibility_profile() ==
          goldsrc::ResourceTransitionControlCompatibilityProfile::
              stock_protocol_48_build_10210);
    CHECK(result.state->evidence_profile() ==
          goldsrc::ResourceTransitionControlEvidenceProfile::
              repeated_signed_stock_capture_opaque_semantics);

    CHECK(result.bytes_consumed == 9U);
    CHECK(result.next_byte_offset == 9U);
    CHECK(kIndependentControlFixture[result.next_byte_offset] ==
          std::byte{43U});
    CHECK(kIndependentControlFixture[result.next_byte_offset + 1U] ==
          std::byte{0xa5U});

    CHECK(result.boundary->opcode() == 43U);
    CHECK(result.boundary->byte_offset() == result.next_byte_offset);
    CHECK(result.boundary->remaining_byte_count() == 1U);
    CHECK(result.boundary->source_payload_size() ==
          kIndependentControlFixture.size());
    CHECK(result.boundary->compatibility_profile() ==
          goldsrc::ResourceTransitionControlCompatibilityProfile::
              stock_protocol_48_build_10210);

    STATIC_CHECK_FALSE(std::is_default_constructible_v<
                       goldsrc::ResourceTransitionControlState>);
    STATIC_CHECK_FALSE(std::is_copy_assignable_v<
                       goldsrc::ResourceTransitionControlState>);
    STATIC_CHECK_FALSE(std::is_move_assignable_v<
                       goldsrc::ResourceTransitionControlState>);
    STATIC_CHECK_FALSE(
        HasOpaqueCounterGetter<goldsrc::ResourceTransitionControlState>);
    STATIC_CHECK_FALSE(HasReservedGetter<
                       goldsrc::ResourceTransitionControlState>);
}

TEST_CASE(
    "Every truncated transition-control payload prefix fails without publication",
    "[goldsrc][resource-transition][control][truncation]")
{
    const goldsrc::ResourceTransitionControlParser parser;
    for (std::size_t size = 0U;
         size < kIndependentControlFixture.size();
         ++size) {
        INFO("prefix size " << size);
        const auto result = parser.parse(
            std::span{kIndependentControlFixture}.first(size));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->byte_offset <= size);
        CHECK_FALSE(result.state);
        CHECK_FALSE(result.boundary);
        CHECK(result.bytes_consumed == 0U);
        CHECK(result.next_byte_offset == 0U);
    }
}

TEST_CASE(
    "Transition-control parser requires exact opcode and does not scan for opcode 43",
    "[goldsrc][resource-transition][control][opcode][no-scan]")
{
    auto wrong_opcode = kIndependentControlFixture;
    wrong_opcode[0U] = std::byte{44U};
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(wrong_opcode),
        goldsrc::ResourceTransitionControlErrorCode::wrong_opcode,
        wrong_opcode.size());

    auto later_candidate = kIndependentControlFixture;
    later_candidate[9U] = std::byte{99U};
    later_candidate[10U] = std::byte{43U};
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(later_candidate),
        goldsrc::ResourceTransitionControlErrorCode::
            wrong_next_boundary_opcode,
        later_candidate.size());

    const std::array outside{std::byte{45U}};
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(
            outside,
            outside.size()),
        goldsrc::ResourceTransitionControlErrorCode::invalid_input_geometry,
        outside.size());
}

TEST_CASE(
    "Transition-control opaque u32 is little-endian and bounded by project policy",
    "[goldsrc][resource-transition][control][endian][limits]")
{
    const goldsrc::ResourceTransitionControlParser bounded{{9U, 2U}};

    auto at_limit = kIndependentControlFixture;
    overwrite_u32_le(at_limit, 1U, 2U);
    const auto accepted = bounded.parse(at_limit);
    REQUIRE(accepted);

    auto limit_plus_one = kIndependentControlFixture;
    overwrite_u32_le(limit_plus_one, 1U, 3U);
    check_error(
        bounded.parse(limit_plus_one),
        goldsrc::ResourceTransitionControlErrorCode::
            opaque_counter_limit_exceeded,
        limit_plus_one.size());

    auto wrong_endian = kIndependentControlFixture;
    std::reverse(wrong_endian.begin() + 1, wrong_endian.begin() + 5);
    check_error(
        bounded.parse(wrong_endian),
        goldsrc::ResourceTransitionControlErrorCode::
            opaque_counter_limit_exceeded,
        wrong_endian.size());

    const goldsrc::ResourceTransitionControlParser default_bound;
    auto at_default = kIndependentControlFixture;
    overwrite_u32_le(
        at_default,
        1U,
        static_cast<std::uint32_t>(
            goldsrc::kDefaultMaximumOpaqueCounterValue));
    REQUIRE(default_bound.parse(at_default));

    auto default_plus_one = kIndependentControlFixture;
    overwrite_u32_le(
        default_plus_one,
        1U,
        static_cast<std::uint32_t>(
            goldsrc::kDefaultMaximumOpaqueCounterValue + 1U));
    check_error(
        default_bound.parse(default_plus_one),
        goldsrc::ResourceTransitionControlErrorCode::
            opaque_counter_limit_exceeded,
        default_plus_one.size());

    const goldsrc::ResourceTransitionControlParser hard_bound{{
        goldsrc::kMaximumTransitionControlSize,
        goldsrc::kMaximumOpaqueCounterValue,
    }};
    auto at_hard_bound = kIndependentControlFixture;
    overwrite_u32_le(
        at_hard_bound,
        1U,
        (std::numeric_limits<std::uint32_t>::max)());
    REQUIRE(hard_bound.parse(at_hard_bound));
}

TEST_CASE(
    "Transition-control reserved u32 and opcode-43 boundary are strict",
    "[goldsrc][resource-transition][control][reserved][boundary]")
{
    auto reserved_nonzero = kIndependentControlFixture;
    overwrite_u32_le(reserved_nonzero, 5U, 1U);
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(reserved_nonzero),
        goldsrc::ResourceTransitionControlErrorCode::invalid_reserved_value,
        reserved_nonzero.size());

    const auto missing_boundary = std::span{kIndependentControlFixture}.first(
        goldsrc::kResourceTransitionControlMessageSize);
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(missing_boundary),
        goldsrc::ResourceTransitionControlErrorCode::missing_next_boundary,
        missing_boundary.size());

    auto wrong_boundary = kIndependentControlFixture;
    wrong_boundary[9U] = std::byte{42U};
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(wrong_boundary),
        goldsrc::ResourceTransitionControlErrorCode::
            wrong_next_boundary_opcode,
        wrong_boundary.size());

    const auto boundary_without_body =
        std::span{kIndependentControlFixture}.first(10U);
    check_error(
        goldsrc::ResourceTransitionControlParser{}.parse(
            boundary_without_body),
        goldsrc::ResourceTransitionControlErrorCode::
            truncated_next_boundary_body,
        boundary_without_body.size());
}

TEST_CASE(
    "Transition-control cursor supports a prefixed payload and leaves all boundary bytes untouched",
    "[goldsrc][resource-transition][control][cursor][ownership]")
{
    std::vector<std::byte> payload{
        std::byte{0x5aU},
        std::byte{0xa5U},
    };
    payload.insert(
        payload.end(),
        kIndependentControlFixture.begin(),
        kIndependentControlFixture.end());
    payload.push_back(std::byte{0x7eU});

    const auto result =
        goldsrc::ResourceTransitionControlParser{}.parse(payload, 2U);
    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.boundary);
    CHECK(result.state->source_message_offset() == 2U);
    CHECK(result.bytes_consumed == 9U);
    CHECK(result.next_byte_offset == 11U);
    CHECK(payload[result.next_byte_offset] == std::byte{43U});
    CHECK(payload[result.next_byte_offset + 1U] == std::byte{0xa5U});
    CHECK(result.boundary->byte_offset() == result.next_byte_offset);
    CHECK(result.boundary->remaining_byte_count() == 2U);
    CHECK(result.boundary->source_payload_size() == payload.size());

    std::ranges::fill(payload, std::byte{0U});
    CHECK(result.state->source_message_offset() == 2U);
    CHECK(result.boundary->byte_offset() == 11U);
    CHECK(result.boundary->remaining_byte_count() == 2U);
}

TEST_CASE(
    "Transition-control project limits have defaults hard caps and invalid-configuration failures",
    "[goldsrc][resource-transition][control][configuration]")
{
    STATIC_CHECK(
        goldsrc::kDefaultMaximumTransitionControlSize ==
        goldsrc::kResourceTransitionControlMessageSize);
    STATIC_CHECK(
        goldsrc::kMaximumTransitionControlSize ==
        goldsrc::kResourceTransitionControlMessageSize);
    STATIC_CHECK(goldsrc::kDefaultMaximumOpaqueCounterValue == 1'000'000U);
    STATIC_CHECK(
        goldsrc::kMaximumOpaqueCounterValue ==
        (std::numeric_limits<std::uint32_t>::max)());

    CHECK(goldsrc::valid_resource_transition_control_limits({}));
    CHECK(goldsrc::valid_resource_transition_control_limits({
        goldsrc::kMaximumTransitionControlSize,
        goldsrc::kMaximumOpaqueCounterValue,
    }));
    CHECK_FALSE(goldsrc::valid_resource_transition_control_limits({
        goldsrc::kMaximumTransitionControlSize - 1U,
        1U,
    }));
    CHECK_FALSE(goldsrc::valid_resource_transition_control_limits({
        goldsrc::kMaximumTransitionControlSize + 1U,
        1U,
    }));
    CHECK_FALSE(goldsrc::valid_resource_transition_control_limits({
        goldsrc::kMaximumTransitionControlSize,
        0U,
    }));
    CHECK_FALSE(goldsrc::valid_resource_transition_control_limits({
        goldsrc::kMaximumTransitionControlSize,
        goldsrc::kMaximumOpaqueCounterValue + 1U,
    }));

    const goldsrc::ResourceTransitionControlParser invalid{{
        goldsrc::kMaximumTransitionControlSize,
        goldsrc::kMaximumOpaqueCounterValue + 1U,
    }};
    CHECK_FALSE(invalid.valid_configuration());
    check_error(
        invalid.parse(kIndependentControlFixture),
        goldsrc::ResourceTransitionControlErrorCode::invalid_configuration,
        kIndependentControlFixture.size());

    const goldsrc::ResourceTransitionControlParser unsupported_profile{
        {},
        static_cast<goldsrc::ResourceTransitionControlCompatibilityProfile>(
            0xffU),
    };
    CHECK_FALSE(unsupported_profile.valid_configuration());
    check_error(
        unsupported_profile.parse(kIndependentControlFixture),
        goldsrc::ResourceTransitionControlErrorCode::invalid_configuration,
        kIndependentControlFixture.size());
}

} // namespace
