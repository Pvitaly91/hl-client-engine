#include <hlclient/goldsrc/stock_runtime_first_observation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::StockPostResourceResponseCursor cursor(
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::size_t payload_bytes,
    const std::size_t replay_ordinal = 4U)
{
    const auto payload_bits = payload_bytes * 8U;
    const auto consumed = byte_offset * 8U + bit_offset;
    return {
        replay_ordinal,
        replay_ordinal + 10U,
        replay_ordinal + 20U,
        byte_offset,
        bit_offset,
        17U,
        payload_bytes,
        payload_bits,
        payload_bits - consumed,
        true,
        true,
    };
}

[[nodiscard]] goldsrc::StockRuntimeFirstObservationInput input(
    std::string run_id,
    const goldsrc::StockPostResourceResponseCursor value_cursor,
    const std::span<const std::byte> payload,
    std::string version = "stock-1.1.1.1-hlds-4.1.1.1-build-10210")
{
    return {
        std::move(run_id), std::move(version), value_cursor, payload, true, true};
}

TEST_CASE("First observation reads one aligned byte and preserves the cursor",
          "[goldsrc][stock-runtime][first-observation][aligned]")
{
    std::array payload{
        std::byte{0x10U}, std::byte{0x42U}, std::byte{0xfeU}};
    const auto exact = cursor(1U, 0U, payload.size());
    const std::array observations{
        input("00000000000000000000000000000001", exact, payload)};

    const auto built =
        goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->neutral_candidate_name() ==
          "first_post_resource_runtime_candidate");
    CHECK(built.state->candidate_bit_width() == 8U);
    REQUIRE(built.state->numeric_candidate());
    CHECK(*built.state->numeric_candidate() == 0x42U);
    CHECK_FALSE(built.state->bounded_bit_prefix());
    CHECK(built.state->byte_aligned());
    CHECK(built.state->stability() ==
          goldsrc::StockRuntimeFirstCandidateStability::single_observation);
    CHECK(built.state->exact_cursor().byte_offset == exact.byte_offset);
    CHECK(built.state->exact_cursor().bit_offset == exact.bit_offset);
    CHECK(built.state->exact_cursor().next_unconsumed_bit_count ==
          exact.next_unconsumed_bit_count);
    CHECK_FALSE(built.state->body_consumed());
    CHECK_FALSE(built.state->semantic_category_assigned());

    payload[1U] = std::byte{0x99U};
    CHECK(*built.state->numeric_candidate() == 0x42U);
}

TEST_CASE("Nonaligned observation retains only a bounded LSB-first prefix",
          "[goldsrc][stock-runtime][first-observation][bits]")
{
    const std::array payload{
        std::byte{0xacU}, std::byte{0x03U}};
    const std::array observations{
        input("00000000000000000000000000000002",
              cursor(0U, 3U, payload.size()), payload)};

    const auto built =
        goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK_FALSE(built.state->byte_aligned());
    CHECK(built.state->candidate_bit_width() == 8U);
    CHECK_FALSE(built.state->numeric_candidate());
    REQUIRE(built.state->bounded_bit_prefix());
    CHECK(*built.state->bounded_bit_prefix() == 0x75U);
    REQUIRE(built.state->occurrences().size() == 1U);
    CHECK(built.state->occurrences()[0].bit_offset == 3U);
    CHECK_FALSE(built.state->occurrences()[0].byte_aligned);
}

TEST_CASE("First candidate becomes stable only across matching accepted runs",
          "[goldsrc][stock-runtime][first-observation][stability]")
{
    const std::array first_payload{std::byte{0x2aU}, std::byte{0x99U}};
    const std::array second_payload{std::byte{0x2aU}, std::byte{0x00U}};
    const std::array observations{
        input("00000000000000000000000000000003",
              cursor(0U, 0U, first_payload.size(), 7U), first_payload),
        input("00000000000000000000000000000004",
              cursor(0U, 0U, second_payload.size(), 9U), second_payload),
    };

    const auto built =
        goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->stability() ==
          goldsrc::StockRuntimeFirstCandidateStability::stable_observation);
    CHECK(built.state->recurrence_count() == 2U);
    REQUIRE(built.state->numeric_candidate());
    CHECK(*built.state->numeric_candidate() == 0x2aU);
    CHECK(built.state->occurrences()[0].replay_payload_ordinal == 7U);
    CHECK(built.state->occurrences()[1].replay_payload_ordinal == 9U);
}

TEST_CASE("First candidate stability retains the exact bit alignment",
          "[goldsrc][stock-runtime][first-observation][stability][bits]")
{
    // Both payloads expose the same eight-bit LSB-first prefix (0x5a), but at
    // different source bit offsets.  The representation alone is therefore
    // insufficient cross-run evidence.
    const std::array first_payload{std::byte{0xb4U}, std::byte{0x00U}};
    const std::array second_payload{std::byte{0x68U}, std::byte{0x01U}};
    const std::array observations{
        input("0000000000000000000000000000000a",
              cursor(0U, 1U, first_payload.size(), 7U), first_payload),
        input("0000000000000000000000000000000b",
              cursor(0U, 2U, second_payload.size(), 9U), second_payload),
    };

    const auto built =
        goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->stability() ==
          goldsrc::StockRuntimeFirstCandidateStability::candidate_conflicting);
    CHECK_FALSE(built.state->numeric_candidate());
    CHECK_FALSE(built.state->bounded_bit_prefix());
    REQUIRE(built.state->occurrences().size() == 2U);
    CHECK(built.state->occurrences()[0].bit_offset == 1U);
    CHECK(built.state->occurrences()[1].bit_offset == 2U);
    REQUIRE(built.state->occurrences()[0].bounded_bit_prefix);
    REQUIRE(built.state->occurrences()[1].bounded_bit_prefix);
    CHECK(*built.state->occurrences()[0].bounded_bit_prefix == 0x5aU);
    CHECK(*built.state->occurrences()[1].bounded_bit_prefix == 0x5aU);
}

TEST_CASE("Contradictory complete runs stay neutral and conflicting",
          "[goldsrc][stock-runtime][first-observation][conflict]")
{
    const std::array first_payload{std::byte{0x20U}};
    const std::array second_payload{std::byte{0x21U}};
    const std::array observations{
        input("00000000000000000000000000000005",
              cursor(0U, 0U, first_payload.size()), first_payload),
        input("00000000000000000000000000000006",
              cursor(0U, 0U, second_payload.size()), second_payload),
    };

    const auto built =
        goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->stability() ==
          goldsrc::StockRuntimeFirstCandidateStability::candidate_conflicting);
    CHECK_FALSE(built.state->numeric_candidate());
    CHECK_FALSE(built.state->bounded_bit_prefix());
    REQUIRE(built.state->occurrences().size() == 2U);
    CHECK(built.state->occurrences()[0].numeric_candidate == 0x20U);
    CHECK(built.state->occurrences()[1].numeric_candidate == 0x21U);
}

TEST_CASE("Missing or untrusted candidate evidence fails transactionally",
          "[goldsrc][stock-runtime][first-observation][mutation]")
{
    const std::array payload{std::byte{0x33U}};

    SECTION("exact end of payload has no candidate")
    {
        const std::array observations{
            input("00000000000000000000000000000007",
                  cursor(1U, 0U, payload.size()), payload)};
        const auto rejected =
            goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeFirstObservationErrorCode::missing_candidate);
    }

    SECTION("unaccepted run cannot participate")
    {
        auto observation = input(
            "00000000000000000000000000000008",
            cursor(0U, 0U, payload.size()), payload);
        observation.accepted_evidence_run = false;
        const std::array observations{observation};
        const auto rejected =
            goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeFirstObservationErrorCode::unaccepted_run);
    }

    SECTION("cursor geometry mutation is typed")
    {
        auto invalid_cursor = cursor(0U, 0U, payload.size());
        invalid_cursor.next_unconsumed_bit_count = 7U;
        const std::array observations{
            input("00000000000000000000000000000009",
                  invalid_cursor, payload)};
        const auto rejected =
            goldsrc::StockRuntimeFirstObservationBuilder{}.build(observations);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeFirstObservationErrorCode::
                  payload_geometry_mismatch);
    }
}

} // namespace
