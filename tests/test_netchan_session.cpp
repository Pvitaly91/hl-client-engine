#include <hlclient/goldsrc/netchan_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result);
    return *result;
}

[[nodiscard]] goldsrc::NetchanHeader server_header(
    const std::uint32_t sequence_value,
    const std::uint32_t acknowledgement_value = 0U,
    const bool reliable = false,
    const bool reliable_acknowledgement = false,
    const bool fragmented = false)
{
    return goldsrc::NetchanHeader{
        goldsrc::NetchanSequenceWord{
            sequence(sequence_value),
            goldsrc::NetchanSequenceFlags{reliable, fragmented},
        },
        goldsrc::NetchanAcknowledgementWord{
            sequence(acknowledgement_value),
            reliable_acknowledgement,
        },
    };
}

template<std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(
    const std::array<std::uint8_t, Size>& values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(
        values,
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

[[nodiscard]] goldsrc::NetchanIncomingInspection inspect_new(
    goldsrc::NetchanSession& session,
    const goldsrc::NetchanHeader& header)
{
    auto inspected = session.inspect_incoming(header);
    REQUIRE(inspected);
    REQUIRE(inspected.inspection);
    CHECK(inspected.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::newer);
    CHECK(inspected.inspection->should_commit());
    return std::move(*inspected.inspection);
}

TEST_CASE("Stock Protocol 48 session starts at the capture-confirmed baseline",
          "[goldsrc][netchan][session][capture]")
{
    STATIC_CHECK_FALSE(goldsrc::kStockProtocol48NetchanHasQport);
    STATIC_CHECK(
        goldsrc::kStockProtocol48MinimumDecodedPayloadSize == 8U);
    STATIC_CHECK(
        goldsrc::kStockProtocol48NetchanPaddingByte == std::byte{0x01});
    STATIC_CHECK_FALSE(std::is_copy_constructible_v<goldsrc::NetchanIncomingInspection>);
    STATIC_CHECK(std::is_move_constructible_v<goldsrc::NetchanIncomingInspection>);
    STATIC_CHECK_FALSE(
        std::is_copy_constructible_v<goldsrc::NetchanFirstAcknowledgementTransaction>);

    const goldsrc::NetchanSession session;
    REQUIRE(session.valid_configuration());
    CHECK(session.state().next_outgoing_sequence == sequence(1U));
    CHECK(session.state().last_outgoing_sequence == sequence(0U));
    CHECK(session.state().incoming_sequence == sequence(0U));
    CHECK(session.state().peer_acknowledgement == sequence(0U));
    CHECK_FALSE(session.state().incoming_reliable_acknowledgement);
    CHECK_FALSE(session.state().peer_reliable_acknowledgement);
    CHECK_FALSE(session.first_incoming_committed());
    CHECK_FALSE(session.first_acknowledgement_prepared());
    CHECK_FALSE(session.first_acknowledgement_sent());
}

TEST_CASE("Session classifies duplicate old and half-range input without mutation",
          "[goldsrc][netchan][session][sequence]")
{
    goldsrc::NetchanSession session;

    const auto duplicate = session.inspect_incoming(server_header(0U));
    REQUIRE(duplicate);
    CHECK(duplicate.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::duplicate);
    CHECK_FALSE(duplicate.inspection->should_commit());
    CHECK_FALSE(duplicate.inspection->acknowledgement());

    const auto older = session.inspect_incoming(
        server_header(goldsrc::kNetchanSequenceMask));
    REQUIRE(older);
    CHECK(older.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::older);
    CHECK_FALSE(older.inspection->acknowledgement());

    const auto ambiguous = session.inspect_incoming(
        server_header(goldsrc::kNetchanSequenceHalfRange));
    REQUIRE(ambiguous);
    CHECK(ambiguous.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::half_range_ambiguous);
    CHECK_FALSE(ambiguous.inspection->acknowledgement());

    const auto skipped = session.inspect_incoming(
        server_header(3U, 0U, true, true, true));
    REQUIRE(skipped);
    CHECK(skipped.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::newer);
    CHECK(skipped.inspection->skipped_sequences() == 2U);
    CHECK(session.state().incoming_sequence == sequence(0U));
    CHECK_FALSE(session.first_incoming_committed());
}

TEST_CASE("Session observes acknowledgements without implementing reliable clearing",
          "[goldsrc][netchan][session][acknowledgement]")
{
    SECTION("initial acknowledgement is a typed duplicate observation")
    {
        goldsrc::NetchanSession session;
        auto inspected = session.inspect_incoming(server_header(1U, 0U, true, true));
        REQUIRE(inspected);
        REQUIRE(inspected.inspection->acknowledgement());
        const auto& acknowledgement = *inspected.inspection->acknowledgement();
        CHECK(acknowledgement.sequence == sequence(0U));
        CHECK(acknowledgement.reliable);
        CHECK(acknowledgement.disposition ==
              goldsrc::NetchanAcknowledgementDisposition::duplicate);

        REQUIRE(session.commit_incoming(std::move(*inspected.inspection)));
        CHECK(session.state().peer_acknowledgement == sequence(0U));
        CHECK(session.state().peer_reliable_acknowledgement);
    }

    SECTION("future acknowledgement is rejected without state mutation")
    {
        goldsrc::NetchanSession session;
        const auto inspected = session.inspect_incoming(server_header(1U, 1U));
        REQUIRE_FALSE(inspected);
        REQUIRE(inspected.error);
        CHECK(inspected.error->code ==
              goldsrc::NetchanSessionErrorCode::future_acknowledgement);
        CHECK(session.state().incoming_sequence == sequence(0U));
        CHECK_FALSE(session.first_incoming_committed());
    }

    SECTION("half-range acknowledgement is rejected explicitly")
    {
        goldsrc::NetchanSession session;
        const auto inspected = session.inspect_incoming(
            server_header(1U, goldsrc::kNetchanSequenceHalfRange));
        REQUIRE_FALSE(inspected);
        REQUIRE(inspected.error);
        CHECK(inspected.error->code == goldsrc::NetchanSessionErrorCode::
                                           acknowledgement_half_range_ambiguous);
    }

    SECTION("stale observation does not regress the stored peer acknowledgement")
    {
        const goldsrc::NetchanSequenceState initial{
            sequence(11U),
            sequence(10U),
            sequence(0U),
            sequence(5U),
            false,
            true,
        };
        goldsrc::NetchanSession session{initial};
        auto inspected = session.inspect_incoming(server_header(1U, 4U));
        REQUIRE(inspected);
        REQUIRE(inspected.inspection->acknowledgement());
        CHECK(inspected.inspection->acknowledgement()->disposition ==
              goldsrc::NetchanAcknowledgementDisposition::stale);
        REQUIRE(session.commit_incoming(std::move(*inspected.inspection)));
        CHECK(session.state().peer_acknowledgement == sequence(5U));
        CHECK(session.state().peer_reliable_acknowledgement);
    }
}

TEST_CASE("First incoming commit is transactional and one-shot",
          "[goldsrc][netchan][session][transaction]")
{
    goldsrc::NetchanSession session;
    auto first = inspect_new(session, server_header(1U, 0U, true));
    auto stale = inspect_new(session, server_header(2U));

    REQUIRE(session.commit_incoming(std::move(first)));
    CHECK(session.first_incoming_committed());
    CHECK(session.state().incoming_sequence == sequence(1U));
    CHECK(session.state().incoming_reliable_acknowledgement);

    const auto stale_commit = session.commit_incoming(std::move(stale));
    REQUIRE_FALSE(stale_commit);
    REQUIRE(stale_commit.error);
    CHECK(stale_commit.error->code ==
          goldsrc::NetchanSessionErrorCode::stale_incoming_inspection);
    CHECK(session.state().incoming_sequence == sequence(1U));

    auto duplicate = session.inspect_incoming(server_header(1U));
    REQUIRE(duplicate);
    CHECK(duplicate.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::duplicate);
    const auto second_commit = session.commit_incoming(
        std::move(*duplicate.inspection));
    REQUIRE_FALSE(second_commit);
    REQUIRE(second_commit.error);
    CHECK(second_commit.error->code ==
          goldsrc::NetchanSessionErrorCode::first_incoming_already_committed);

    const auto newer = session.inspect_incoming(server_header(2U));
    REQUIRE_FALSE(newer);
    REQUIRE(newer.error);
    CHECK(newer.error->code ==
          goldsrc::NetchanSessionErrorCode::first_incoming_already_committed);
}

TEST_CASE("First ACK is exact transport-only sequence one and commits once",
          "[goldsrc][netchan][session][acknowledgement][wire]")
{
    goldsrc::NetchanSession session;

    const auto too_early = session.prepare_first_acknowledgement();
    REQUIRE_FALSE(too_early);
    REQUIRE(too_early.error);
    CHECK(too_early.error->code ==
          goldsrc::NetchanSessionErrorCode::first_acknowledgement_before_incoming);

    auto incoming = inspect_new(session, server_header(1U, 0U, true));
    REQUIRE(session.commit_incoming(std::move(incoming)));
    auto prepared = session.prepare_first_acknowledgement();
    REQUIRE(prepared);
    REQUIRE(prepared.transaction);
    CHECK(session.first_acknowledgement_prepared());
    CHECK_FALSE(session.first_acknowledgement_sent());

    const auto& packet = prepared.transaction->packet();
    CHECK(packet.header.sequence.sequence == sequence(1U));
    CHECK_FALSE(packet.header.sequence.flags.reliable);
    CHECK_FALSE(packet.header.sequence.flags.fragmented);
    CHECK(packet.header.acknowledgement.sequence == sequence(1U));
    CHECK(packet.header.acknowledgement.reliable);
    CHECK(std::ranges::none_of(
        packet.fragments,
        [](const auto& descriptor) { return descriptor.has_value(); }));
    REQUIRE(packet.payload.size() ==
            goldsrc::kStockProtocol48MinimumDecodedPayloadSize);
    CHECK(std::ranges::all_of(packet.payload, [](const std::byte value) {
        return value == goldsrc::kStockProtocol48NetchanPaddingByte;
    }));

    const auto encoded = goldsrc::encode_client_to_server_netchan_packet(packet);
    REQUIRE(encoded);
    // Independent key-1 golden. The decoded body is eight padding bytes, not a
    // sign-on `new` command or any other application payload.
    const auto expected = bytes(std::array<std::uint8_t, 16U>{
        0x01U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x80U,
        0x5aU, 0x19U, 0x01U, 0x00U,
        0x1aU, 0x01U, 0x11U, 0x40U,
    });
    CHECK(*encoded.datagram == expected);

    REQUIRE(session.commit_first_acknowledgement(
        std::move(*prepared.transaction)));
    CHECK(session.first_acknowledgement_sent());
    CHECK(session.state().last_outgoing_sequence == sequence(1U));
    CHECK(session.state().next_outgoing_sequence == sequence(2U));

    const auto duplicate_prepare = session.prepare_first_acknowledgement();
    REQUIRE_FALSE(duplicate_prepare);
    REQUIRE(duplicate_prepare.error);
    CHECK(duplicate_prepare.error->code == goldsrc::NetchanSessionErrorCode::
                                                first_acknowledgement_already_prepared);
}

TEST_CASE("First ACK leaves reliable acknowledgement clear for plain input",
          "[goldsrc][netchan][session][acknowledgement]")
{
    goldsrc::NetchanSession session;
    auto incoming = inspect_new(session, server_header(1U));
    REQUIRE(session.commit_incoming(std::move(incoming)));
    auto prepared = session.prepare_first_acknowledgement();
    REQUIRE(prepared);
    CHECK_FALSE(prepared.transaction->packet().header.acknowledgement.reliable);
}

TEST_CASE("Sequence and one-shot ACK state wrap without signed arithmetic",
          "[goldsrc][netchan][session][wrap]")
{
    const goldsrc::NetchanSequenceState initial{
        sequence(goldsrc::kNetchanSequenceMask),
        sequence(goldsrc::kNetchanSequenceMask - 1U),
        sequence(goldsrc::kNetchanSequenceMask),
        sequence(goldsrc::kNetchanSequenceMask - 1U),
        false,
        false,
    };
    goldsrc::NetchanSession session{initial};
    REQUIRE(session.valid_configuration());
    auto incoming = inspect_new(
        session,
        server_header(0U, goldsrc::kNetchanSequenceMask - 1U, true));
    REQUIRE(session.commit_incoming(std::move(incoming)));
    auto prepared = session.prepare_first_acknowledgement();
    REQUIRE(prepared);
    CHECK(prepared.transaction->packet().header.sequence.sequence ==
          sequence(goldsrc::kNetchanSequenceMask));
    CHECK(prepared.transaction->packet().header.acknowledgement.sequence == sequence(0U));
    REQUIRE(session.commit_first_acknowledgement(
        std::move(*prepared.transaction)));
    CHECK(session.state().next_outgoing_sequence == sequence(0U));
}

TEST_CASE("Inconsistent initial sequence state is rejected",
          "[goldsrc][netchan][session][configuration]")
{
    auto invalid = goldsrc::NetchanSequenceState::stock_protocol48();
    invalid.next_outgoing_sequence = sequence(2U);
    goldsrc::NetchanSession session{invalid};
    CHECK_FALSE(session.valid_configuration());
    const auto inspected = session.inspect_incoming(server_header(1U));
    REQUIRE_FALSE(inspected);
    REQUIRE(inspected.error);
    CHECK(inspected.error->code ==
          goldsrc::NetchanSessionErrorCode::invalid_configuration);
}

} // namespace
