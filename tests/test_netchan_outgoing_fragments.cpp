#include <hlclient/goldsrc/netchan_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::byte> bytes(
    const std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(
        values,
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

[[nodiscard]] std::vector<std::byte> patterned(const std::size_t size)
{
    std::vector<std::byte> output(size);
    for (std::size_t index = 0U; index < output.size(); ++index) {
        output[index] = std::byte{static_cast<std::uint8_t>(index & 0xffU)};
    }
    return output;
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result);
    return *result;
}

[[nodiscard]] goldsrc::NetchanHeader server_header(
    const std::uint32_t sequence_value,
    const std::uint32_t acknowledgement_value,
    const bool reliable_acknowledgement,
    const bool reliable_present = false,
    const bool fragmented = false)
{
    return goldsrc::NetchanHeader{
        goldsrc::NetchanSequenceWord{
            sequence(sequence_value),
            goldsrc::NetchanSequenceFlags{reliable_present, fragmented},
        },
        goldsrc::NetchanAcknowledgementWord{
            sequence(acknowledgement_value),
            reliable_acknowledgement,
        },
    };
}

[[nodiscard]] goldsrc::NetchanSessionLimits fragment_limits(
    const std::size_t unfragmented_limit = 32U)
{
    return goldsrc::NetchanSessionLimits{
        goldsrc::kDefaultNetchanDatagramSize,
        unfragmented_limit,
        goldsrc::kMaximumPendingReliablePayload,
    };
}

[[nodiscard]] goldsrc::NetchanTransmitPlan prepare(
    const goldsrc::NetchanSession& session,
    const std::span<const std::byte> unreliable = {})
{
    auto prepared = session.prepare_outgoing_packet(unreliable);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    auto plan = std::move(*prepared.plan);
    const auto encoded =
        goldsrc::encode_client_to_server_netchan_packet(plan.packet());
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    CHECK(goldsrc::classify_netchan_datagram(*encoded.datagram).classification ==
          goldsrc::NetchanDatagramClassification::sequenced);
    return plan;
}

void commit(
    goldsrc::NetchanSession& session,
    goldsrc::NetchanTransmitPlan& plan)
{
    REQUIRE(session.commit_outgoing_send(std::move(plan)));
}

void commit_incoming(
    goldsrc::NetchanSession& session,
    const goldsrc::NetchanHeader& header,
    const goldsrc::NetchanIncomingReliableUnitClassification classification =
        goldsrc::NetchanIncomingReliableUnitClassification::unfragmented)
{
    auto inspected = session.inspect_incoming(header, classification);
    REQUIRE(inspected);
    REQUIRE(inspected.inspection);
    REQUIRE(inspected.inspection->should_commit());
    REQUIRE(session.commit_incoming(std::move(*inspected.inspection)));
}

struct FragmentSessionSnapshot {
    std::uint32_t next_outgoing{0U};
    std::uint32_t last_outgoing{0U};
    std::uint32_t incoming{0U};
    std::uint32_t peer_acknowledgement{0U};
    bool incoming_reliable_acknowledgement{false};
    bool peer_reliable_acknowledgement{false};
    bool outgoing_toggle{false};
    std::vector<std::byte> pending;
    bool has_in_flight{false};
    std::vector<std::byte> in_flight;
    std::uint32_t first_sent_sequence{0U};
    std::uint32_t most_recent_sent_sequence{0U};
    std::uint64_t send_count{0U};
    bool retransmission_requested{false};
    bool has_transfer{false};
    std::uint64_t transfer_id{0U};
    std::vector<std::byte> canonical_transfer;
    std::uint16_t fragment_count{0U};
    std::uint16_t current_fragment_index{0U};

    friend bool operator==(
        const FragmentSessionSnapshot& left,
        const FragmentSessionSnapshot& right) noexcept = default;
};

[[nodiscard]] FragmentSessionSnapshot snapshot(
    const goldsrc::NetchanSession& session)
{
    FragmentSessionSnapshot result{
        session.state().next_outgoing_sequence.value(),
        session.state().last_outgoing_sequence.value(),
        session.state().incoming_sequence.value(),
        session.state().peer_acknowledgement.value(),
        session.state().incoming_reliable_acknowledgement,
        session.state().peer_reliable_acknowledgement,
        session.outgoing_reliable_toggle(),
        session.pending_reliable_payload(),
    };
    if (session.in_flight_reliable_payload()) {
        result.has_in_flight = true;
        result.in_flight = session.in_flight_reliable_payload()->bytes;
        result.first_sent_sequence =
            session.in_flight_reliable_payload()->first_sent_sequence.value();
        result.most_recent_sent_sequence =
            session.in_flight_reliable_payload()->most_recent_sent_sequence.value();
        result.send_count = session.in_flight_reliable_payload()->send_count;
        result.retransmission_requested =
            session.in_flight_reliable_payload()->retransmission_requested;
    }
    if (session.outgoing_fragment_transfer()) {
        const auto& transfer = *session.outgoing_fragment_transfer();
        result.has_transfer = true;
        result.transfer_id = transfer.transfer_id.value();
        result.canonical_transfer = transfer.canonical_bytes;
        result.fragment_count = transfer.fragment_count;
        result.current_fragment_index = transfer.current_fragment_index;
    }
    return result;
}

template<typename Result>
void check_session_error(
    const Result& result,
    const goldsrc::NetchanSessionErrorCode expected)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

TEST_CASE("Outgoing fragment boundaries are deterministic and hard bounded",
          "[goldsrc][netchan][fragment][outgoing][limits]")
{
    STATIC_CHECK(goldsrc::kMaximumOutgoingNormalFragmentTransferSize > 0U);
    STATIC_CHECK(goldsrc::kMaximumOutgoingNormalFragments > 0U);

    for (const auto payload_size : {
             33U,
             1'024U,
             1'025U,
             3'079U,
             goldsrc::kMaximumOutgoingNormalFragmentTransferSize,
         }) {
        INFO("payload_size=" << payload_size);
        goldsrc::NetchanSession session{fragment_limits()};
        const auto payload = patterned(payload_size);
        REQUIRE(session.queue_reliable(payload));
        auto plan = prepare(session);
        REQUIRE(plan.fragment_plan());
        const auto expected_count =
            1U + (payload_size - 1U) /
                     goldsrc::kStockProtocol48NormalFragmentChunkSize;
        CHECK(plan.fragment_plan()->fragment_index == 1U);
        CHECK(plan.fragment_plan()->fragment_count == expected_count);
        CHECK(plan.fragment_plan()->canonical_offset == 0U);
        CHECK(plan.fragment_plan()->canonical_length ==
              std::min(
                  payload_size,
                  goldsrc::kStockProtocol48NormalFragmentChunkSize));
        CHECK(plan.packet().header.sequence.flags.fragmented);
        CHECK(plan.packet().header.sequence.flags.reliable);
    }

    goldsrc::NetchanSession over_limit{fragment_limits()};
    check_session_error(
        over_limit.queue_reliable(patterned(
            goldsrc::kMaximumOutgoingNormalFragmentTransferSize + 1U)),
        goldsrc::NetchanSessionErrorCode::reliable_queue_overflow);

    auto small_datagram_limits = fragment_limits();
    small_datagram_limits.maximum_datagram_size = 64U;
    small_datagram_limits.maximum_pending_reliable_payload = 128U;
    goldsrc::NetchanSession small_datagram{small_datagram_limits};
    REQUIRE(small_datagram.valid_configuration());
    REQUIRE(small_datagram.queue_reliable(patterned(60U)));
    check_session_error(
        small_datagram.prepare_outgoing_packet(),
        goldsrc::NetchanSessionErrorCode::
            outgoing_fragment_datagram_does_not_fit);
}

TEST_CASE("Outgoing fragments are stop-and-wait per reliable generation",
          "[goldsrc][netchan][fragment][outgoing][lifecycle]")
{
    goldsrc::NetchanSession session{fragment_limits()};
    const auto payload = patterned(2'050U);
    const auto suffix = bytes({
        0xf1U, 0xf2U, 0xf3U, 0xf4U, 0xf5U, 0xf6U,
        0xf7U, 0xf8U, 0xf9U, 0xfaU, 0xfbU,
    });
    const auto pending_b = bytes({0xb1U, 0xb2U, 0xb3U});
    REQUIRE(session.queue_reliable(payload));

    auto first = prepare(session, suffix);
    REQUIRE(first.fragment_plan());
    CHECK(first.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::send_new_fragment);
    CHECK(first.fragment_plan()->fragment_count == 3U);
    CHECK(first.fragment_plan()->canonical_length == 1'024U);
    CHECK(first.packet().fragment_payload_size == 1'024U);
    CHECK(first.packet().payload.size() == 1'035U);
    CHECK(std::ranges::equal(
        std::span<const std::byte>{first.packet().payload}.first(1'024U),
        std::span<const std::byte>{payload}.first(1'024U)));
    CHECK(std::ranges::equal(
        std::span<const std::byte>{first.packet().payload}.subspan(1'024U),
        suffix));

    const auto first_encoded =
        goldsrc::encode_client_to_server_netchan_packet(first.packet());
    REQUIRE(first_encoded);
    commit(session, first);
    REQUIRE(session.outgoing_fragment_transfer());
    const auto transfer_id =
        session.outgoing_fragment_transfer()->transfer_id;
    CHECK(session.outgoing_fragment_transfer()->canonical_bytes == payload);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->toggle);
    CHECK(session.in_flight_reliable_payload()->bytes ==
          std::vector<std::byte>(payload.begin(), payload.begin() + 1'024));
    CHECK_FALSE(session.has_outgoing_fragment_send_work());

    REQUIRE(session.queue_reliable(pending_b));
    CHECK(session.pending_reliable_payload() == pending_b);
    auto blocked = prepare(session);
    CHECK(blocked.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
    CHECK_FALSE(blocked.fragment_plan());
    commit(session, blocked);
    // The ordinary ACK-only packet consumed sequence 2; fragment 1 therefore
    // requires a latest-covering matching ACK, not merely the original seq 1.
    // Its reliable metadata still records seq1, so ACK1 is sufficient.
    commit_incoming(session, server_header(1U, 1U, true));
    REQUIRE(session.outgoing_fragment_transfer());
    CHECK(session.outgoing_fragment_transfer()->current_fragment_index == 2U);
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.has_outgoing_fragment_send_work());

    auto second = prepare(session);
    REQUIRE(second.fragment_plan());
    CHECK(second.fragment_plan()->transfer_id == transfer_id);
    CHECK(second.fragment_plan()->fragment_index == 2U);
    CHECK(second.fragment_plan()->canonical_offset == 1'024U);
    CHECK(second.packet().header.sequence.flags.reliable);
    CHECK(second.packet().header.sequence.flags.fragmented);
    commit(session, second);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.in_flight_reliable_payload()->toggle);
    const auto second_sequence =
        session.in_flight_reliable_payload()->most_recent_sent_sequence.value();
    commit_incoming(session, server_header(2U, second_sequence, false));

    auto final = prepare(session);
    REQUIRE(final.fragment_plan());
    CHECK(final.fragment_plan()->fragment_index == 3U);
    CHECK(final.fragment_plan()->canonical_offset == 2'048U);
    CHECK(final.fragment_plan()->canonical_length == 2U);
    CHECK(final.packet().header.sequence.flags.reliable);
    commit(session, final);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->toggle);
    CHECK(session.outgoing_fragment_transfer());
    const auto final_sequence =
        session.in_flight_reliable_payload()->most_recent_sent_sequence.value();
    commit_incoming(session, server_header(3U, final_sequence, true));

    CHECK_FALSE(session.outgoing_fragment_transfer());
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload() == pending_b);
    auto next_message = prepare(session);
    CHECK_FALSE(next_message.fragment_plan());
    CHECK(next_message.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::send_new);

    const auto first_decoded =
        goldsrc::decode_client_to_server_netchan_packet(*first_encoded.datagram);
    REQUIRE(first_decoded);
    REQUIRE(first_decoded.packet);
    CHECK(first_decoded.packet->payload.size() == 1'035U);
    CHECK(first_decoded.packet->fragment_payload_size == 1'024U);
}

TEST_CASE("Fragment send plans survive encode and send failure transactionally",
          "[goldsrc][netchan][fragment][outgoing][transaction]")
{
    goldsrc::NetchanSession session{fragment_limits()};
    const auto payload = patterned(1'025U);
    REQUIRE(session.queue_reliable(payload));
    const auto before = snapshot(session);

    {
        auto destroyed = prepare(session);
        REQUIRE(destroyed.fragment_plan());
    }
    CHECK(snapshot(session) == before);

    auto encoder_failure = prepare(session);
    const auto failed_encode = goldsrc::encode_client_to_server_netchan_packet(
        encoder_failure.packet(), goldsrc::NetchanPacketLimits{16U});
    REQUIRE_FALSE(failed_encode);
    REQUIRE(session.abandon_outgoing_packet(std::move(encoder_failure)));
    CHECK(snapshot(session) == before);

    auto send_failure = prepare(session);
    REQUIRE(session.abandon_outgoing_packet(std::move(send_failure)));
    CHECK(snapshot(session) == before);

    auto winner = prepare(session);
    auto stale = prepare(session);
    commit(session, winner);
    check_session_error(
        session.commit_outgoing_send(std::move(stale)),
        goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
}

TEST_CASE("ACK-gap retries only the current fragment with fresh sequence key",
          "[goldsrc][netchan][fragment][outgoing][retransmission]")
{
    goldsrc::NetchanSession session{fragment_limits()};
    REQUIRE(session.queue_reliable(patterned(1'025U)));
    auto first = prepare(session);
    const auto first_packet = first.packet();
    const auto first_encoded =
        goldsrc::encode_client_to_server_netchan_packet(first_packet);
    REQUIRE(first_encoded);
    commit(session, first);

    auto ordinary = prepare(session);
    CHECK(ordinary.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
    commit(session, ordinary);
    commit_incoming(session, server_header(1U, 2U, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->retransmission_requested);
    CHECK(session.has_outgoing_fragment_send_work());

    auto retry = prepare(session);
    REQUIRE(retry.fragment_plan());
    CHECK(retry.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::retransmit_fragment);
    CHECK(retry.fragment_plan()->retransmission);
    CHECK(retry.fragment_plan()->fragment_index == 1U);
    CHECK(retry.fragment_plan()->canonical_offset == 0U);
    CHECK(retry.fragment_plan()->canonical_length == 1'024U);
    CHECK(retry.packet().payload == first_packet.payload);
    REQUIRE(retry.packet().fragments[0]);
    REQUIRE(first_packet.fragments[0]);
    CHECK_FALSE(retry.packet().fragments[1]);
    CHECK_FALSE(first_packet.fragments[1]);
    CHECK(retry.packet().fragments[0]->fragment_id ==
          first_packet.fragments[0]->fragment_id);
    CHECK(retry.packet().fragments[0]->offset ==
          first_packet.fragments[0]->offset);
    CHECK(retry.packet().fragments[0]->length ==
          first_packet.fragments[0]->length);
    CHECK(retry.packet().header.sequence.sequence !=
          first_packet.header.sequence.sequence);
    const auto retry_encoded =
        goldsrc::encode_client_to_server_netchan_packet(retry.packet());
    REQUIRE(retry_encoded);
    CHECK(*retry_encoded.datagram != *first_encoded.datagram);
    commit(session, retry);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->send_count == 2U);
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);

    // A duplicate old-generation ACK below the latest retransmission does not
    // immediately request another copy.
    commit_incoming(session, server_header(2U, 2U, false));
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);

    auto gap_packet = prepare(session);
    commit(session, gap_packet);
    commit_incoming(session, server_header(3U, 4U, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->retransmission_requested);
    auto second_retry = prepare(session);
    commit(session, second_retry);
    const auto latest =
        session.in_flight_reliable_payload()->most_recent_sent_sequence.value();
    commit_incoming(session, server_header(4U, latest, true));
    REQUIRE(session.outgoing_fragment_transfer());
    CHECK(session.outgoing_fragment_transfer()->current_fragment_index == 2U);
    CHECK_FALSE(session.in_flight_reliable_payload());
}

TEST_CASE("Fragment ACKs are stale/future safe and cross numeric wrap",
          "[goldsrc][netchan][fragment][outgoing][ack][wrap]")
{
    SECTION("stale and future ACK do not complete")
    {
        goldsrc::NetchanSession session{fragment_limits()};
        REQUIRE(session.queue_reliable(patterned(33U)));
        auto fragment = prepare(session);
        commit(session, fragment);
        const auto active = snapshot(session);

        commit_incoming(session, server_header(1U, 0U, true));
        CHECK(session.outgoing_fragment_transfer());
        CHECK(session.in_flight_reliable_payload());

        const auto before_future = snapshot(session);
        check_session_error(
            session.inspect_incoming(server_header(2U, 2U, true)),
            goldsrc::NetchanSessionErrorCode::future_acknowledgement);
        CHECK(snapshot(session) == before_future);

        commit_incoming(session, server_header(2U, 1U, true));
        CHECK_FALSE(session.outgoing_fragment_transfer());
        CHECK_FALSE(session.in_flight_reliable_payload());
        CHECK(active.has_transfer);
    }

    SECTION("reserved classifier sequences advance transactionally before fragments")
    {
        const auto maximum = goldsrc::kNetchanSequenceMask;
        goldsrc::NetchanSequenceState state{
            sequence(maximum - 1U),
            sequence(maximum - 2U),
            sequence(0U),
            sequence(maximum - 2U),
            false,
            false,
        };
        goldsrc::NetchanSession session{state, fragment_limits()};
        REQUIRE(session.valid_configuration());
        REQUIRE(session.queue_reliable(patterned(1'025U)));
        const auto suffix = bytes({0xa1U, 0xa2U, 0xa3U});
        const auto before = snapshot(session);

        auto abandoned = prepare(session, suffix);
        CHECK(abandoned.reliable_decision() ==
              goldsrc::ReliableTransmitDecision::advance_fragment_sequence);
        CHECK_FALSE(abandoned.fragment_plan());
        CHECK(abandoned.reliable_payload_size() == 0U);
        CHECK(abandoned.unreliable_payload_size() == 0U);
        REQUIRE(session.abandon_outgoing_packet(std::move(abandoned)));
        CHECK(snapshot(session) == before);

        auto first_advance = prepare(session, suffix);
        auto stale_advance = prepare(session, suffix);
        CHECK(first_advance.packet().header.sequence.sequence ==
              sequence(maximum - 1U));
        CHECK_FALSE(first_advance.packet().header.sequence.flags.reliable);
        CHECK_FALSE(first_advance.packet().header.sequence.flags.fragmented);
        CHECK_FALSE(first_advance.fragment_plan());
        CHECK(first_advance.packet().payload ==
              std::vector<std::byte>(
                  goldsrc::kStockProtocol48MinimumDecodedPayloadSize,
                  goldsrc::kStockProtocol48NetchanPaddingByte));
        const auto first_advance_encoded =
            goldsrc::encode_client_to_server_netchan_packet(
                first_advance.packet());
        REQUIRE(first_advance_encoded);
        REQUIRE(first_advance_encoded.datagram);
        const auto first_advance_wire = *first_advance_encoded.datagram;
        const auto first_advance_decoded =
            goldsrc::decode_client_to_server_netchan_packet(
                first_advance_wire);
        REQUIRE(first_advance_decoded);
        REQUIRE(first_advance_decoded.packet);
        CHECK(first_advance_decoded.packet->payload ==
              std::vector<std::byte>(
                  goldsrc::kStockProtocol48MinimumDecodedPayloadSize,
                  goldsrc::kStockProtocol48NetchanPaddingByte));
        commit(session, first_advance);
        check_session_error(
            session.commit_outgoing_send(std::move(stale_advance)),
            goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);

        auto expected = before;
        expected.last_outgoing = maximum - 1U;
        expected.next_outgoing = maximum;
        CHECK(snapshot(session) == expected);

        auto second_advance = prepare(session, suffix);
        CHECK(second_advance.reliable_decision() ==
              goldsrc::ReliableTransmitDecision::advance_fragment_sequence);
        CHECK(second_advance.packet().header.sequence.sequence ==
              sequence(maximum));
        CHECK(second_advance.unreliable_payload_size() == 0U);
        const auto second_advance_encoded =
            goldsrc::encode_client_to_server_netchan_packet(
                second_advance.packet());
        REQUIRE(second_advance_encoded);
        REQUIRE(second_advance_encoded.datagram);
        const auto second_advance_wire = *second_advance_encoded.datagram;
        CHECK_FALSE(std::ranges::equal(
            std::span<const std::byte>{first_advance_wire}.subspan(
                goldsrc::kNetchanHeaderSize),
            std::span<const std::byte>{second_advance_wire}.subspan(
                goldsrc::kNetchanHeaderSize)));
        const auto second_advance_decoded =
            goldsrc::decode_client_to_server_netchan_packet(
                second_advance_wire);
        REQUIRE(second_advance_decoded);
        REQUIRE(second_advance_decoded.packet);
        CHECK(second_advance_decoded.packet->payload ==
              std::vector<std::byte>(
                  goldsrc::kStockProtocol48MinimumDecodedPayloadSize,
                  goldsrc::kStockProtocol48NetchanPaddingByte));
        commit(session, second_advance);
        expected.last_outgoing = maximum;
        expected.next_outgoing = 0U;
        CHECK(snapshot(session) == expected);

        auto first = prepare(session, suffix);
        CHECK(first.packet().header.sequence.sequence == sequence(0U));
        CHECK(first.reliable_decision() ==
              goldsrc::ReliableTransmitDecision::send_new_fragment);
        REQUIRE(first.fragment_plan());
        CHECK(first.unreliable_payload_size() == suffix.size());
        CHECK(std::ranges::equal(
            std::span<const std::byte>{first.packet().payload}.last(
                suffix.size()),
            suffix));
        commit(session, first);
        REQUIRE(session.in_flight_reliable_payload());
        CHECK(session.in_flight_reliable_payload()->toggle);
        commit_incoming(session, server_header(1U, 0U, true));

        auto second = prepare(session);
        CHECK(second.packet().header.sequence.sequence == sequence(1U));
        commit(session, second);
        REQUIRE(session.in_flight_reliable_payload());
        CHECK_FALSE(session.in_flight_reliable_payload()->toggle);
        commit_incoming(session, server_header(2U, 1U, false));
        CHECK_FALSE(session.outgoing_fragment_transfer());
    }
}

TEST_CASE("Fragment retry crosses both reserved classifier sequences without mutation",
          "[goldsrc][netchan][fragment][outgoing][retransmission][wrap]")
{
    const auto maximum = goldsrc::kNetchanSequenceMask;
    goldsrc::NetchanSequenceState state{
        sequence(maximum - 3U),
        sequence(maximum - 4U),
        sequence(0U),
        sequence(maximum - 4U),
        false,
        false,
    };
    goldsrc::NetchanSession session{state, fragment_limits()};
    REQUIRE(session.valid_configuration());
    REQUIRE(session.queue_reliable(patterned(1'025U)));

    auto first = prepare(session);
    REQUIRE(first.fragment_plan());
    const auto canonical_fragment = first.packet().payload;
    CHECK(first.packet().header.sequence.sequence == sequence(maximum - 3U));
    commit(session, first);

    auto gap_carrier = prepare(session);
    CHECK(gap_carrier.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
    CHECK(gap_carrier.packet().header.sequence.sequence ==
          sequence(maximum - 2U));
    commit(session, gap_carrier);
    commit_incoming(
        session,
        server_header(1U, maximum - 2U, false));
    REQUIRE(session.in_flight_reliable_payload());
    REQUIRE(session.in_flight_reliable_payload()->retransmission_requested);
    const auto before_advances = snapshot(session);
    const auto suffix = bytes({0xb1U, 0xb2U, 0xb3U, 0xb4U});

    auto failed_send = prepare(session, suffix);
    CHECK(failed_send.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::advance_fragment_sequence);
    REQUIRE(session.abandon_outgoing_packet(std::move(failed_send)));
    CHECK(snapshot(session) == before_advances);

    auto first_advance = prepare(session, suffix);
    CHECK(first_advance.packet().header.sequence.sequence ==
          sequence(maximum - 1U));
    CHECK(first_advance.unreliable_payload_size() == 0U);
    commit(session, first_advance);
    auto expected = before_advances;
    expected.last_outgoing = maximum - 1U;
    expected.next_outgoing = maximum;
    CHECK(snapshot(session) == expected);

    auto second_advance = prepare(session, suffix);
    CHECK(second_advance.packet().header.sequence.sequence == sequence(maximum));
    CHECK(second_advance.unreliable_payload_size() == 0U);
    commit(session, second_advance);
    expected.last_outgoing = maximum;
    expected.next_outgoing = 0U;
    CHECK(snapshot(session) == expected);

    auto retry = prepare(session, suffix);
    CHECK(retry.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::retransmit_fragment);
    REQUIRE(retry.fragment_plan());
    CHECK(retry.fragment_plan()->retransmission);
    CHECK(retry.packet().header.sequence.sequence == sequence(0U));
    CHECK(retry.reliable_payload_size() == canonical_fragment.size());
    CHECK(retry.unreliable_payload_size() == suffix.size());
    CHECK(std::ranges::equal(
        std::span<const std::byte>{retry.packet().payload}.first(
            canonical_fragment.size()),
        canonical_fragment));
    CHECK(std::ranges::equal(
        std::span<const std::byte>{retry.packet().payload}.subspan(
            canonical_fragment.size()),
        suffix));
    commit(session, retry);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence ==
          sequence(maximum - 3U));
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence ==
          sequence(0U));
    CHECK(session.in_flight_reliable_payload()->send_count == 2U);
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);
}

TEST_CASE("Incoming fragment admission distinguishes new units and retransmissions",
          "[goldsrc][netchan][fragment][incoming][classification]")
{
    goldsrc::NetchanSession session;
    const auto first = server_header(1U, 0U, false, true, true);
    check_session_error(
        session.inspect_incoming(first),
        goldsrc::NetchanSessionErrorCode::
            fragment_reliable_classification_required);
    CHECK(session.state().incoming_sequence == sequence(0U));
    CHECK_FALSE(session.state().incoming_reliable_acknowledgement);

    auto admitted = session.inspect_incoming(
        first,
        goldsrc::NetchanIncomingReliableUnitClassification::new_fragment_unit);
    REQUIRE(admitted);
    REQUIRE(admitted.inspection);
    CHECK(admitted.inspection->contains_new_reliable_data());
    CHECK(admitted.inspection->incoming_reliable_acknowledgement_after_commit());
    REQUIRE(session.commit_incoming(std::move(*admitted.inspection)));
    CHECK(session.state().incoming_sequence == sequence(1U));
    CHECK(session.state().incoming_reliable_acknowledgement);

    const auto retry = server_header(2U, 0U, false, true, true);
    auto exact = session.inspect_incoming(
        retry,
        goldsrc::NetchanIncomingReliableUnitClassification::
            exact_fragment_retransmission);
    REQUIRE(exact);
    REQUIRE(exact.inspection);
    CHECK_FALSE(exact.inspection->contains_new_reliable_data());
    CHECK(exact.inspection->incoming_reliable_acknowledgement_after_commit());
    REQUIRE(session.commit_incoming(std::move(*exact.inspection)));
    CHECK(session.state().incoming_sequence == sequence(2U));
    CHECK(session.state().incoming_reliable_acknowledgement);

    auto next = session.inspect_incoming(
        server_header(3U, 0U, false, true, true),
        goldsrc::NetchanIncomingReliableUnitClassification::new_fragment_unit);
    REQUIRE(next);
    REQUIRE(next.inspection);
    CHECK(next.inspection->contains_new_reliable_data());
    CHECK_FALSE(next.inspection->incoming_reliable_acknowledgement_after_commit());
    REQUIRE(session.commit_incoming(std::move(*next.inspection)));
    CHECK_FALSE(session.state().incoming_reliable_acknowledgement);

    // Same-sequence replay is rejected before body/reassembly classification.
    const auto duplicate = session.inspect_incoming(
        server_header(3U, 0U, false, true, true));
    REQUIRE(duplicate);
    REQUIRE(duplicate.inspection);
    CHECK(duplicate.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::duplicate);
    CHECK_FALSE(duplicate.inspection->should_commit());

    check_session_error(
        session.inspect_incoming(
            server_header(4U, 0U, false, false, true),
            goldsrc::NetchanIncomingReliableUnitClassification::new_fragment_unit),
        goldsrc::NetchanSessionErrorCode::fragment_reliable_flag_required);
    check_session_error(
        session.inspect_incoming(
            server_header(4U, 0U, false),
            goldsrc::NetchanIncomingReliableUnitClassification::new_fragment_unit),
        goldsrc::NetchanSessionErrorCode::
            fragment_reliable_classification_mismatch);
}

} // namespace
