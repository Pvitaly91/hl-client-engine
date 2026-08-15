#include <hlclient/goldsrc/netchan_channel.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result.has_value());
    return *result;
}

[[nodiscard]] std::vector<std::byte> opaque_bytes(const std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

[[nodiscard]] goldsrc::NetchanHeader server_header(
    const std::uint32_t incoming_sequence,
    const bool contains_reliable = false,
    const std::uint32_t acknowledgement = 0U,
    const bool reliable_acknowledgement = false,
    const bool fragmented = false)
{
    return goldsrc::NetchanHeader{
        goldsrc::NetchanSequenceWord{
            sequence(incoming_sequence),
            goldsrc::NetchanSequenceFlags{contains_reliable, fragmented},
        },
        goldsrc::NetchanAcknowledgementWord{
            sequence(acknowledgement),
            reliable_acknowledgement,
        },
    };
}

void commit_server_packet(
    goldsrc::NetchanChannel& channel,
    const goldsrc::NetchanHeader& header,
    const goldsrc::NetchanIncomingReliableUnit reliable_unit)
{
    auto inspected = channel.inspect_incoming(header);
    REQUIRE(inspected);
    REQUIRE(inspected.inspection->should_commit());
    channel.commit_incoming(std::move(*inspected.inspection), reliable_unit);
}

TEST_CASE("Stock Protocol 48 channel starts at the captured no-qport baseline",
          "[goldsrc][netchan][channel][capture]")
{
    STATIC_CHECK_FALSE(goldsrc::kStockProtocol48NetchanHasQport);
    STATIC_CHECK(goldsrc::kStockProtocol48MinimumDecodedPayloadSize == 8U);
    STATIC_CHECK(
        goldsrc::kStockProtocol48NetchanPaddingByte == std::byte{0x01});

    goldsrc::NetchanChannel channel;
    REQUIRE(channel.valid_configuration());
    CHECK(channel.next_outgoing_sequence().value() == 1U);
    CHECK(channel.last_outgoing_sequence().value() == 0U);
    CHECK(channel.incoming_sequence().value() == 0U);
    CHECK(channel.peer_acknowledgement().value() == 0U);
    CHECK_FALSE(channel.outgoing_reliable_toggle());
    CHECK_FALSE(channel.incoming_reliable_toggle());
    CHECK_FALSE(channel.peer_reliable_acknowledgement());
    CHECK_FALSE(channel.has_reliable_payload());

    const auto prepared = channel.prepare_outgoing();
    REQUIRE(prepared);
    const auto& transaction = *prepared.transaction;
    const auto& packet = transaction.packet();
    CHECK(packet.header.sequence.sequence.value() == 1U);
    CHECK_FALSE(packet.header.sequence.flags.reliable);
    CHECK_FALSE(packet.header.sequence.flags.fragmented);
    CHECK(packet.header.acknowledgement.sequence.value() == 0U);
    CHECK_FALSE(packet.header.acknowledgement.reliable);
    CHECK(transaction.reliable_payload_size() == 0U);
    CHECK(transaction.unreliable_payload_size() == 0U);
    CHECK(transaction.padding_size() == 8U);
    CHECK_FALSE(transaction.is_reliable_retransmission());
    REQUIRE(packet.payload.size() == 8U);
    CHECK(std::ranges::all_of(packet.payload, [](const std::byte value) {
        return value == std::byte{0x01};
    }));

    REQUIRE(channel.commit_outgoing(transaction));
    CHECK(channel.last_outgoing_sequence().value() == 1U);
    CHECK(channel.next_outgoing_sequence().value() == 2U);
}

TEST_CASE("Outgoing prepare and commit preserve state across codec or send failure",
          "[goldsrc][netchan][channel][transaction]")
{
    goldsrc::NetchanChannel channel;
    const auto first = channel.prepare_outgoing();
    REQUIRE(first);

    // A caller can fail encoding/sending and discard the transaction. No
    // sequence is consumed until a successful transport operation commits it.
    CHECK(channel.next_outgoing_sequence().value() == 1U);
    const auto retry = channel.prepare_outgoing();
    REQUIRE(retry);
    CHECK(retry.transaction->packet().header.sequence.sequence.value() == 1U);
    REQUIRE(channel.commit_outgoing(*retry.transaction));
    CHECK(channel.next_outgoing_sequence().value() == 2U);

    const auto stale = channel.commit_outgoing(*first.transaction);
    REQUIRE_FALSE(stale);
    REQUIRE(stale.error.has_value());
    CHECK(
        stale.error->code ==
        goldsrc::NetchanChannelErrorCode::stale_outgoing_transaction);
    CHECK(channel.next_outgoing_sequence().value() == 2U);
}

TEST_CASE("Reliable queue is bounded and retransmits only when caller builds again",
          "[goldsrc][netchan][reliable]")
{
    const auto reliable = opaque_bytes("RELIABLE_TEST_PAYLOAD");
    goldsrc::NetchanChannel channel;

    REQUIRE(channel.queue_reliable_payload(reliable));
    CHECK(channel.has_reliable_payload());
    CHECK_FALSE(channel.has_reliable_in_flight());
    CHECK(channel.reliable_payload_size() == reliable.size());

    const auto occupied = channel.queue_reliable_payload(reliable);
    REQUIRE_FALSE(occupied);
    REQUIRE(occupied.error.has_value());
    CHECK(
        occupied.error->code ==
        goldsrc::NetchanChannelErrorCode::reliable_slot_occupied);

    const auto first = channel.prepare_outgoing();
    REQUIRE(first);
    CHECK(first.transaction->packet().header.sequence.flags.reliable);
    CHECK(first.transaction->expected_reliable_acknowledgement());
    CHECK_FALSE(first.transaction->is_reliable_retransmission());
    CHECK(first.transaction->reliable_payload_size() == reliable.size());
    CHECK(std::equal(
        reliable.begin(),
        reliable.end(),
        first.transaction->packet().payload.begin()));
    REQUIRE(channel.commit_outgoing(*first.transaction));
    CHECK(channel.has_reliable_in_flight());
    CHECK(channel.outgoing_reliable_toggle());

    // The pure channel owns no timer/update loop. A fresh retry exists only
    // because the caller explicitly requests a later outgoing packet.
    CHECK(channel.next_outgoing_sequence().value() == 2U);
    const auto retransmission = channel.prepare_outgoing();
    REQUIRE(retransmission);
    CHECK(retransmission.transaction->is_reliable_retransmission());
    CHECK(
        retransmission.transaction->packet().header.sequence.sequence.value() == 2U);
    CHECK(std::equal(
        reliable.begin(),
        reliable.end(),
        retransmission.transaction->packet().payload.begin()));
    CHECK(channel.next_outgoing_sequence().value() == 2U);
}

TEST_CASE("Reliable mismatch followed by a corrected same numeric ACK clears in-flight",
          "[goldsrc][netchan][reliable][ack]")
{
    const auto reliable = opaque_bytes("RELIABLE_TEST_PAYLOAD");
    goldsrc::NetchanChannel channel;
    REQUIRE(channel.queue_reliable_payload(reliable));
    const auto outgoing = channel.prepare_outgoing();
    REQUIRE(outgoing);
    REQUIRE(channel.commit_outgoing(*outgoing.transaction));

    auto mismatch = channel.inspect_incoming(server_header(1U, false, 1U, false));
    REQUIRE(mismatch);
    REQUIRE(mismatch.inspection->acknowledgement().has_value());
    CHECK(
        *mismatch.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced_reliable_unconfirmed);
    channel.commit_incoming(
        std::move(*mismatch.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK(channel.has_reliable_in_flight());
    CHECK(channel.peer_acknowledgement().value() == 1U);
    CHECK_FALSE(channel.peer_reliable_acknowledgement());

    auto corrected = channel.inspect_incoming(server_header(2U, false, 1U, true));
    REQUIRE(corrected);
    REQUIRE(corrected.inspection->acknowledgement().has_value());
    CHECK(
        *corrected.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced_reliable_confirmed);
    channel.commit_incoming(
        std::move(*corrected.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK_FALSE(channel.has_reliable_payload());
    CHECK(channel.peer_acknowledgement().value() == 1U);
    CHECK(channel.peer_reliable_acknowledgement());
}

TEST_CASE("Duplicate acknowledgement cannot clear the next reliable payload",
          "[goldsrc][netchan][reliable][ack]")
{
    const auto first_payload = opaque_bytes("RELIABLE_TEST_PAYLOAD");
    const auto second_payload = opaque_bytes("SECOND_RELIABLE_PAYLOAD");
    goldsrc::NetchanChannel channel;

    REQUIRE(channel.queue_reliable_payload(first_payload));
    const auto first = channel.prepare_outgoing();
    REQUIRE(first);
    REQUIRE(channel.commit_outgoing(*first.transaction));
    commit_server_packet(
        channel,
        server_header(1U, false, 1U, true),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK_FALSE(channel.has_reliable_payload());

    REQUIRE(channel.queue_reliable_payload(second_payload));
    const auto second = channel.prepare_outgoing();
    REQUIRE(second);
    CHECK_FALSE(second.transaction->expected_reliable_acknowledgement());
    REQUIRE(channel.commit_outgoing(*second.transaction));
    CHECK(channel.has_reliable_in_flight());

    auto duplicate = channel.inspect_incoming(server_header(2U, false, 1U, true));
    REQUIRE(duplicate);
    REQUIRE(duplicate.inspection->acknowledgement().has_value());
    CHECK(
        *duplicate.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::duplicate);
    channel.commit_incoming(
        std::move(*duplicate.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK(channel.has_reliable_in_flight());

    auto confirmed = channel.inspect_incoming(server_header(3U, false, 2U, false));
    REQUIRE(confirmed);
    CHECK(
        *confirmed.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced_reliable_confirmed);
    channel.commit_incoming(
        std::move(*confirmed.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK_FALSE(channel.has_reliable_payload());
}

TEST_CASE("ACK before the reliable first-send sequence never clears by toggle coincidence",
          "[goldsrc][netchan][reliable][ack]")
{
    goldsrc::NetchanChannel channel;
    for (std::uint32_t expected = 1U; expected <= 2U; ++expected) {
        const auto outgoing = channel.prepare_outgoing();
        REQUIRE(outgoing);
        CHECK(outgoing.transaction->packet().header.sequence.sequence.value() == expected);
        REQUIRE(channel.commit_outgoing(*outgoing.transaction));
    }

    const auto payload = opaque_bytes("RELIABLE_TEST_PAYLOAD");
    REQUIRE(channel.queue_reliable_payload(payload));
    const auto reliable = channel.prepare_outgoing();
    REQUIRE(reliable);
    CHECK(reliable.transaction->packet().header.sequence.sequence.value() == 3U);
    REQUIRE(channel.commit_outgoing(*reliable.transaction));

    auto old_sequence = channel.inspect_incoming(server_header(1U, false, 2U, true));
    REQUIRE(old_sequence);
    CHECK(
        *old_sequence.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced_reliable_unconfirmed);
    channel.commit_incoming(
        std::move(*old_sequence.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK(channel.has_reliable_in_flight());

    auto correct = channel.inspect_incoming(server_header(2U, false, 3U, true));
    REQUIRE(correct);
    CHECK(
        *correct.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced_reliable_confirmed);
    channel.commit_incoming(
        std::move(*correct.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK_FALSE(channel.has_reliable_payload());
}

TEST_CASE("Unreliable payload is one-shot and overflow cannot lose reliable state",
          "[goldsrc][netchan][reliable][unreliable]")
{
    constexpr goldsrc::NetchanChannelLimits limits{
        16U,
        24U,
    };
    goldsrc::NetchanChannel channel{limits};
    REQUIRE(channel.valid_configuration());

    const auto empty = channel.queue_reliable_payload({});
    REQUIRE_FALSE(empty);
    CHECK(
        empty.error->code == goldsrc::NetchanChannelErrorCode::empty_reliable_payload);

    const std::vector<std::byte> too_large_reliable(17U, std::byte{0x51});
    const auto oversized_reliable = channel.queue_reliable_payload(too_large_reliable);
    REQUIRE_FALSE(oversized_reliable);
    CHECK(
        oversized_reliable.error->code ==
        goldsrc::NetchanChannelErrorCode::reliable_payload_too_large);

    const auto reliable = opaque_bytes("REL");
    REQUIRE(channel.queue_reliable_payload(reliable));
    const std::vector<std::byte> oversized_unreliable(22U, std::byte{0x55});
    const auto overflow = channel.prepare_outgoing(oversized_unreliable);
    REQUIRE_FALSE(overflow);
    CHECK(
        overflow.error->code ==
        goldsrc::NetchanChannelErrorCode::outgoing_payload_too_large);
    CHECK(channel.has_reliable_payload());
    CHECK_FALSE(channel.has_reliable_in_flight());
    CHECK(channel.next_outgoing_sequence().value() == 1U);

    const auto unreliable = opaque_bytes("UNRELIABLE_TEST_DATA");
    REQUIRE(reliable.size() + unreliable.size() <= limits.maximum_packet_payload_size);
    const auto combined = channel.prepare_outgoing(unreliable);
    REQUIRE(combined);
    CHECK(combined.transaction->reliable_payload_size() == reliable.size());
    CHECK(combined.transaction->unreliable_payload_size() == unreliable.size());
    CHECK(std::equal(
        reliable.begin(),
        reliable.end(),
        combined.transaction->packet().payload.begin()));
    CHECK(std::equal(
        unreliable.begin(),
        unreliable.end(),
        combined.transaction->packet().payload.begin() +
            static_cast<std::ptrdiff_t>(reliable.size())));
    REQUIRE(channel.commit_outgoing(*combined.transaction));
    commit_server_packet(
        channel,
        server_header(1U, false, 1U, true),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK_FALSE(channel.has_reliable_payload());

    const auto next = channel.prepare_outgoing();
    REQUIRE(next);
    CHECK_FALSE(next.transaction->packet().header.sequence.flags.reliable);
    CHECK(next.transaction->unreliable_payload_size() == 0U);
    CHECK(next.transaction->packet().payload.size() == 8U);
    CHECK(std::ranges::none_of(
        next.transaction->packet().payload,
        [&](const std::byte value) {
            return std::ranges::find(unreliable, value) != unreliable.end() &&
                   value != std::byte{0x01};
        }));
}

TEST_CASE("Incoming duplicate older and half-range packets are filtered before ACK state",
          "[goldsrc][netchan][channel][ordering]")
{
    goldsrc::NetchanChannel channel;

    auto skipped = channel.inspect_incoming(server_header(2U, false, 0U, false));
    REQUIRE(skipped);
    CHECK(
        skipped.inspection->disposition() ==
        goldsrc::NetchanIncomingSequenceDisposition::newer);
    CHECK(skipped.inspection->skipped_sequences() == 1U);
    channel.commit_incoming(
        std::move(*skipped.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK(channel.incoming_sequence().value() == 2U);

    // These deliberately contain future/mismatched ACK metadata. Equal/older
    // sequence classification prevents that metadata from being evaluated.
    const auto duplicate = channel.inspect_incoming(server_header(2U, true, 99U, true));
    REQUIRE(duplicate);
    CHECK(
        duplicate.inspection->disposition() ==
        goldsrc::NetchanIncomingSequenceDisposition::duplicate);
    CHECK_FALSE(duplicate.inspection->acknowledgement().has_value());

    const auto older = channel.inspect_incoming(server_header(1U, true, 99U, true));
    REQUIRE(older);
    CHECK(
        older.inspection->disposition() ==
        goldsrc::NetchanIncomingSequenceDisposition::older);
    CHECK_FALSE(older.inspection->should_commit());

    const auto ambiguous_value =
        (channel.incoming_sequence().value() + goldsrc::kNetchanSequenceHalfRange) &
        goldsrc::kNetchanSequenceMask;
    const auto ambiguous = channel.inspect_incoming(
        server_header(ambiguous_value, true, 99U, true));
    REQUIRE(ambiguous);
    CHECK(
        ambiguous.inspection->disposition() ==
        goldsrc::NetchanIncomingSequenceDisposition::half_range_ambiguous);
    CHECK_FALSE(ambiguous.inspection->should_commit());
    CHECK(channel.incoming_sequence().value() == 2U);
    CHECK_FALSE(channel.incoming_reliable_toggle());
}

TEST_CASE("Incoming future and ambiguous ACKs fail inspection without mutation",
          "[goldsrc][netchan][channel][ack]")
{
    goldsrc::NetchanChannel channel;

    const auto future = channel.inspect_incoming(server_header(1U, false, 1U, false));
    REQUIRE_FALSE(future);
    REQUIRE(future.error.has_value());
    CHECK(
        future.error->code ==
        goldsrc::NetchanChannelErrorCode::future_acknowledgement);
    CHECK(channel.incoming_sequence().value() == 0U);

    const auto ambiguous = channel.inspect_incoming(server_header(
        1U,
        false,
        goldsrc::kNetchanSequenceHalfRange,
        false));
    REQUIRE_FALSE(ambiguous);
    REQUIRE(ambiguous.error.has_value());
    CHECK(
        ambiguous.error->code ==
        goldsrc::NetchanChannelErrorCode::acknowledgement_half_range_ambiguous);
    CHECK(channel.incoming_sequence().value() == 0U);
}

TEST_CASE("Stale and duplicate acknowledgements are typed and do not regress state",
          "[goldsrc][netchan][channel][ack]")
{
    goldsrc::NetchanChannel channel;
    for (std::uint32_t expected = 1U; expected <= 2U; ++expected) {
        const auto outgoing = channel.prepare_outgoing();
        REQUIRE(outgoing);
        REQUIRE(channel.commit_outgoing(*outgoing.transaction));
    }

    auto advanced = channel.inspect_incoming(server_header(1U, false, 2U, false));
    REQUIRE(advanced);
    CHECK(
        *advanced.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced);
    channel.commit_incoming(
        std::move(*advanced.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);

    auto stale = channel.inspect_incoming(server_header(2U, false, 1U, true));
    REQUIRE(stale);
    CHECK(
        *stale.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::stale);
    channel.commit_incoming(
        std::move(*stale.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK(channel.peer_acknowledgement().value() == 2U);
    CHECK_FALSE(channel.peer_reliable_acknowledgement());

    auto duplicate = channel.inspect_incoming(server_header(3U, false, 2U, false));
    REQUIRE(duplicate);
    CHECK(
        *duplicate.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::duplicate);
    channel.commit_incoming(
        std::move(*duplicate.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);

    const auto invalid_toggle = channel.inspect_incoming(
        server_header(4U, false, 2U, true));
    REQUIRE_FALSE(invalid_toggle);
    REQUIRE(invalid_toggle.error.has_value());
    CHECK(
        invalid_toggle.error->code ==
        goldsrc::NetchanChannelErrorCode::invalid_reliable_acknowledgement_toggle);
    CHECK(channel.incoming_sequence().value() == 3U);
}

TEST_CASE("Reliable receive state toggles once per unique unit and not for retries",
          "[goldsrc][netchan][reliable][incoming][capture]")
{
    STATIC_CHECK_FALSE(std::is_copy_constructible_v<goldsrc::NetchanIncomingInspection>);
    STATIC_CHECK(std::is_move_constructible_v<goldsrc::NetchanIncomingInspection>);
    goldsrc::NetchanChannel channel;

    auto first = channel.inspect_incoming(server_header(1U, true, 0U, false, true));
    REQUIRE(first);
    REQUIRE(first.inspection->reliable_resolution_required());
    auto first_token = std::move(*first.inspection);
    channel.commit_incoming(
        std::move(first_token),
        goldsrc::NetchanIncomingReliableUnit::new_unit);
    CHECK(channel.incoming_reliable_toggle());

    // A consumed public token is harmless when accidentally submitted again.
    channel.commit_incoming(
        std::move(first_token),
        goldsrc::NetchanIncomingReliableUnit::new_unit);
    CHECK(channel.incoming_reliable_toggle());

    const auto duplicate = channel.inspect_incoming(
        server_header(1U, true, 0U, false, true));
    REQUIRE(duplicate);
    CHECK(
        duplicate.inspection->disposition() ==
        goldsrc::NetchanIncomingSequenceDisposition::duplicate);
    CHECK(channel.incoming_reliable_toggle());

    auto retry = channel.inspect_incoming(server_header(2U, true, 0U, false, true));
    REQUIRE(retry);
    channel.commit_incoming(
        std::move(*retry.inspection),
        goldsrc::NetchanIncomingReliableUnit::exact_retransmission);
    CHECK(channel.incoming_sequence().value() == 2U);
    CHECK(channel.incoming_reliable_toggle());

    auto next_unique = channel.inspect_incoming(
        server_header(3U, true, 0U, false, true));
    REQUIRE(next_unique);
    channel.commit_incoming(
        std::move(*next_unique.inspection),
        goldsrc::NetchanIncomingReliableUnit::new_unit);
    CHECK_FALSE(channel.incoming_reliable_toggle());
}

TEST_CASE("Five captured-style unique fragments alternate reliable ACK state",
          "[goldsrc][netchan][reliable][fragment][capture]")
{
    goldsrc::NetchanChannel channel;
    constexpr bool expected_toggles[]{true, false, true, false, true};
    for (std::uint32_t fragment = 1U; fragment <= 5U; ++fragment) {
        auto inspected = channel.inspect_incoming(
            server_header(fragment, true, 0U, false, true));
        REQUIRE(inspected);
        channel.commit_incoming(
            std::move(*inspected.inspection),
            goldsrc::NetchanIncomingReliableUnit::new_unit);
        CHECK(channel.incoming_reliable_toggle() == expected_toggles[fragment - 1U]);
    }
}

TEST_CASE("Capture-derived reorder ignores old reliable before toggle and reassembly",
          "[goldsrc][netchan][reliable][ordering][capture]")
{
    goldsrc::NetchanChannel channel;

    auto newer_padding = channel.inspect_incoming(server_header(2U, false, 0U, false));
    REQUIRE(newer_padding);
    CHECK(newer_padding.inspection->skipped_sequences() == 1U);
    channel.commit_incoming(
        std::move(*newer_padding.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);

    const auto held_old_fragment = channel.inspect_incoming(
        server_header(1U, true, 0U, false, true));
    REQUIRE(held_old_fragment);
    CHECK(
        held_old_fragment.inspection->disposition() ==
        goldsrc::NetchanIncomingSequenceDisposition::older);
    CHECK_FALSE(held_old_fragment.inspection->should_commit());
    CHECK_FALSE(channel.incoming_reliable_toggle());

    auto retransmitted_fragment = channel.inspect_incoming(
        server_header(5U, true, 0U, false, true));
    REQUIRE(retransmitted_fragment);
    channel.commit_incoming(
        std::move(*retransmitted_fragment.inspection),
        goldsrc::NetchanIncomingReliableUnit::new_unit);
    CHECK(channel.incoming_sequence().value() == 5U);
    CHECK(channel.incoming_reliable_toggle());
}

TEST_CASE("Reliable sequence and acknowledgement survive low-30-bit wrap",
          "[goldsrc][netchan][reliable][wrap]")
{
    auto state = goldsrc::NetchanInitialState::stock_protocol48();
    state.next_outgoing_sequence = sequence(goldsrc::kNetchanSequenceMask);
    state.last_outgoing_sequence = sequence(goldsrc::kNetchanSequenceMask - 1U);
    state.incoming_sequence = sequence(goldsrc::kNetchanSequenceMask);
    state.peer_acknowledgement = sequence(goldsrc::kNetchanSequenceMask - 1U);

    goldsrc::NetchanChannel channel{{}, state};
    REQUIRE(channel.valid_configuration());
    const auto payload = opaque_bytes("RELIABLE_TEST_PAYLOAD");
    REQUIRE(channel.queue_reliable_payload(payload));

    const auto maximum = channel.prepare_outgoing();
    REQUIRE(maximum);
    CHECK(
        maximum.transaction->packet().header.sequence.sequence.value() ==
        goldsrc::kNetchanSequenceMask);
    REQUIRE(channel.commit_outgoing(*maximum.transaction));
    CHECK(channel.next_outgoing_sequence().value() == 0U);

    const auto wrapped_retry = channel.prepare_outgoing();
    REQUIRE(wrapped_retry);
    CHECK(wrapped_retry.transaction->is_reliable_retransmission());
    CHECK(wrapped_retry.transaction->packet().header.sequence.sequence.value() == 0U);
    REQUIRE(channel.commit_outgoing(*wrapped_retry.transaction));
    CHECK(channel.next_outgoing_sequence().value() == 1U);

    auto wrapped_ack = channel.inspect_incoming(server_header(0U, false, 0U, true));
    REQUIRE(wrapped_ack);
    CHECK(
        *wrapped_ack.inspection->acknowledgement() ==
        goldsrc::NetchanAcknowledgementDisposition::advanced_reliable_confirmed);
    channel.commit_incoming(
        std::move(*wrapped_ack.inspection),
        goldsrc::NetchanIncomingReliableUnit::none);
    CHECK_FALSE(channel.has_reliable_payload());
    CHECK(channel.incoming_sequence().value() == 0U);
}

TEST_CASE("Invalid channel bounds and inconsistent initial sequence are rejected",
          "[goldsrc][netchan][channel][bounds]")
{
    const goldsrc::NetchanChannel too_small{{8U, 7U}};
    CHECK_FALSE(too_small.valid_configuration());
    const auto invalid_prepare = too_small.prepare_outgoing();
    REQUIRE_FALSE(invalid_prepare);
    CHECK(
        invalid_prepare.error->code ==
        goldsrc::NetchanChannelErrorCode::invalid_configuration);

    const goldsrc::NetchanChannel reliable_exceeds_packet{{9U, 8U}};
    CHECK_FALSE(reliable_exceeds_packet.valid_configuration());

    auto inconsistent = goldsrc::NetchanInitialState::stock_protocol48();
    inconsistent.next_outgoing_sequence = sequence(7U);
    const goldsrc::NetchanChannel invalid_initial{{}, inconsistent};
    CHECK_FALSE(invalid_initial.valid_configuration());
}

} // namespace
