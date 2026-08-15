#include <hlclient/goldsrc/netchan_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string_view>
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
    const std::uint32_t acknowledgement_value,
    const bool reliable_present = false,
    const bool reliable_acknowledgement = false,
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

void commit_incoming(
    goldsrc::NetchanSession& session,
    const goldsrc::NetchanHeader& header)
{
    auto inspected = session.inspect_incoming(header);
    REQUIRE(inspected);
    REQUIRE(inspected.inspection);
    REQUIRE(inspected.inspection->should_commit());
    REQUIRE(session.commit_incoming(std::move(*inspected.inspection)));
}

[[nodiscard]] goldsrc::NetchanTransmitPlan prepare(
    const goldsrc::NetchanSession& session,
    const std::span<const std::byte> unreliable = {})
{
    auto prepared = session.prepare_outgoing_packet(unreliable);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    return std::move(*prepared.plan);
}

void commit(goldsrc::NetchanSession& session, goldsrc::NetchanTransmitPlan& plan)
{
    REQUIRE(session.commit_outgoing_send(std::move(plan)));
}

struct SessionSnapshot {
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
    std::uint64_t send_count{0U};
    bool retransmission_requested{false};
};

[[nodiscard]] SessionSnapshot snapshot(const goldsrc::NetchanSession& session)
{
    SessionSnapshot result{
        session.state().next_outgoing_sequence.value(),
        session.state().last_outgoing_sequence.value(),
        session.state().incoming_sequence.value(),
        session.state().peer_acknowledgement.value(),
        session.state().incoming_reliable_acknowledgement,
        session.state().peer_reliable_acknowledgement,
        session.outgoing_reliable_toggle(),
        session.pending_reliable_payload(),
        session.in_flight_reliable_payload().has_value(),
        {},
        0U,
        false,
    };
    if (session.in_flight_reliable_payload()) {
        result.in_flight = session.in_flight_reliable_payload()->bytes;
        result.send_count = session.in_flight_reliable_payload()->send_count;
        result.retransmission_requested =
            session.in_flight_reliable_payload()->retransmission_requested;
    }
    return result;
}

void check_snapshot(
    const goldsrc::NetchanSession& session,
    const SessionSnapshot& expected)
{
    const auto actual = snapshot(session);
    CHECK(actual.next_outgoing == expected.next_outgoing);
    CHECK(actual.last_outgoing == expected.last_outgoing);
    CHECK(actual.incoming == expected.incoming);
    CHECK(actual.peer_acknowledgement == expected.peer_acknowledgement);
    CHECK(actual.incoming_reliable_acknowledgement ==
          expected.incoming_reliable_acknowledgement);
    CHECK(actual.peer_reliable_acknowledgement ==
          expected.peer_reliable_acknowledgement);
    CHECK(actual.outgoing_toggle == expected.outgoing_toggle);
    CHECK(actual.pending == expected.pending);
    CHECK(actual.has_in_flight == expected.has_in_flight);
    CHECK(actual.in_flight == expected.in_flight);
    CHECK(actual.send_count == expected.send_count);
    CHECK(actual.retransmission_requested == expected.retransmission_requested);
}

TEST_CASE("Reliable session limits are named bounded and validated",
          "[goldsrc][netchan][reliable][limits]")
{
    STATIC_CHECK(
        goldsrc::kDefaultMaximumUnfragmentedReliablePayload ==
        goldsrc::kDefaultNetchanDatagramSize - goldsrc::kNetchanHeaderSize);
    STATIC_CHECK(
        goldsrc::kMaximumPendingReliablePayload ==
        goldsrc::kMaximumNetchanDatagramSize - goldsrc::kNetchanHeaderSize);
    STATIC_CHECK_FALSE(std::is_copy_constructible_v<goldsrc::NetchanTransmitPlan>);
    STATIC_CHECK(std::is_move_constructible_v<goldsrc::NetchanTransmitPlan>);

    CHECK(goldsrc::NetchanSession{}.valid_configuration());
    CHECK_FALSE((goldsrc::NetchanSession{goldsrc::NetchanSessionLimits{
        15U,
        7U,
        7U,
    }}.valid_configuration()));
    CHECK((goldsrc::NetchanSession{goldsrc::NetchanSessionLimits{
        16U,
        8U,
        8U,
    }}.valid_configuration()));
    CHECK_FALSE((goldsrc::NetchanSession{goldsrc::NetchanSessionLimits{
        16U,
        0U,
        8U,
    }}.valid_configuration()));
    CHECK_FALSE((goldsrc::NetchanSession{goldsrc::NetchanSessionLimits{
        16U,
        8U,
        7U,
    }}.valid_configuration()));
    CHECK_FALSE((goldsrc::NetchanSession{goldsrc::NetchanSessionLimits{
        goldsrc::kMaximumNetchanDatagramSize,
        8U,
        goldsrc::kMaximumPendingReliablePayload + 1U,
    }}.valid_configuration()));
}

TEST_CASE("Reliable transmit decision gives active in-flight state priority",
          "[goldsrc][netchan][reliable][decision]")
{
    using Decision = goldsrc::ReliableTransmitDecision;
    CHECK(goldsrc::decide_reliable_transmit({}) == Decision::none);
    CHECK(goldsrc::decide_reliable_transmit({true, false, false, false}) ==
          Decision::send_new);
    CHECK(goldsrc::decide_reliable_transmit({true, false, false, true}) ==
          Decision::requires_fragmentation_pending_m2_3_3);
    CHECK(goldsrc::decide_reliable_transmit({true, true, false, true}) ==
          Decision::blocked_waiting_for_ack);
    CHECK(goldsrc::decide_reliable_transmit({true, true, true, true}) ==
          Decision::retransmit);
}

TEST_CASE("Reliable queue appends chunks and rejects overflow atomically",
          "[goldsrc][netchan][reliable][queue]")
{
    goldsrc::NetchanSession session{goldsrc::NetchanSessionLimits{32U, 16U, 20U}};
    const auto initial = snapshot(session);
    REQUIRE(session.queue_reliable({}));
    check_snapshot(session, initial);

    const auto first = bytes({0x10U, 0x11U});
    const auto second = bytes({0x12U, 0x13U, 0x14U});
    REQUIRE(session.queue_reliable(first));
    REQUIRE(session.queue_reliable(second));
    CHECK(session.pending_reliable_payload() ==
          bytes({0x10U, 0x11U, 0x12U, 0x13U, 0x14U}));

    const auto to_limit_minus_one =
        std::vector<std::byte>(14U, std::byte{0x55});
    REQUIRE(session.queue_reliable(to_limit_minus_one));
    REQUIRE(session.pending_reliable_payload().size() == 19U);
    REQUIRE(session.queue_reliable(bytes({0x56U})));
    REQUIRE(session.pending_reliable_payload().size() == 20U);
    const auto full = snapshot(session);
    const auto overflow = bytes({0xffU});
    const auto rejected = session.queue_reliable(overflow);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::NetchanSessionErrorCode::reliable_queue_overflow);
    check_snapshot(session, full);

    const auto requires_fragmentation = session.prepare_outgoing_packet();
    REQUIRE_FALSE(requires_fragmentation);
    REQUIRE(requires_fragmentation.error);
    CHECK(requires_fragmentation.error->code == goldsrc::NetchanSessionErrorCode::
                                                      reliable_payload_requires_fragmentation_pending_m2_3_3);
    check_snapshot(session, full);
}

TEST_CASE("Unfragmented reliable boundary accepts max and defers max plus one",
          "[goldsrc][netchan][reliable][limits][fragment]")
{
    constexpr goldsrc::NetchanSessionLimits limits{32U, 16U, 20U};

    for (const auto accepted_size : std::array<std::size_t, 2U>{15U, 16U}) {
        CAPTURE(accepted_size);
        goldsrc::NetchanSession session{limits};
        const auto payload =
            std::vector<std::byte>(accepted_size, std::byte{0x57});
        REQUIRE(session.queue_reliable(payload));
        const auto prepared = session.prepare_outgoing_packet();
        REQUIRE(prepared);
        REQUIRE(prepared.plan);
        CHECK(prepared.plan->reliable_decision() ==
              goldsrc::ReliableTransmitDecision::send_new);
        CHECK(session.pending_reliable_payload() == payload);
    }

    goldsrc::NetchanSession deferred{limits};
    const auto plus_one = std::vector<std::byte>(17U, std::byte{0x58});
    REQUIRE(deferred.queue_reliable(plus_one));
    const auto before = snapshot(deferred);
    const auto prepared = deferred.prepare_outgoing_packet();
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error);
    CHECK(prepared.error->code == goldsrc::NetchanSessionErrorCode::
                                      reliable_payload_requires_fragmentation_pending_m2_3_3);
    check_snapshot(deferred, before);
}

TEST_CASE("Prepare is read-only and successful commit promotes exactly once",
          "[goldsrc][netchan][reliable][transaction]")
{
    goldsrc::NetchanSession session;
    const auto reliable = bytes({0xa1U, 0xa2U});
    const auto unreliable = bytes({0xb1U, 0xb2U});
    REQUIRE(session.queue_reliable(reliable));
    const auto queued = snapshot(session);

    auto plan = prepare(session, unreliable);
    check_snapshot(session, queued);
    CHECK(plan.reliable_decision() == goldsrc::ReliableTransmitDecision::send_new);
    CHECK(plan.packet().header.sequence.sequence == sequence(1U));
    CHECK(plan.packet().header.sequence.flags.reliable);
    CHECK_FALSE(plan.packet().header.sequence.flags.fragmented);
    REQUIRE(plan.packet().payload.size() == 8U);
    CHECK(std::ranges::equal(
        std::span<const std::byte>{plan.packet().payload}.first(4U),
        bytes({0xa1U, 0xa2U, 0xb1U, 0xb2U})));
    CHECK(std::ranges::all_of(
        std::span<const std::byte>{plan.packet().payload}.subspan(4U),
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));

    commit(session, plan);
    CHECK(session.state().last_outgoing_sequence == sequence(1U));
    CHECK(session.state().next_outgoing_sequence == sequence(2U));
    CHECK(session.pending_reliable_payload().empty());
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->bytes == reliable);
    CHECK(session.in_flight_reliable_payload()->toggle);
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence == sequence(1U));
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence == sequence(1U));
    CHECK(session.in_flight_reliable_payload()->send_count == 1U);
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);
    CHECK(session.outgoing_reliable_toggle());

    const auto committed = snapshot(session);
    const auto duplicate_commit = session.commit_outgoing_send(std::move(plan));
    REQUIRE_FALSE(duplicate_commit);
    REQUIRE(duplicate_commit.error);
    CHECK(duplicate_commit.error->code ==
          goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
    check_snapshot(session, committed);
}

TEST_CASE("Abandon destruction queue and clear preserve transaction atomicity",
          "[goldsrc][netchan][reliable][transaction]")
{
    SECTION("packet build failure leaves the prepared state untouched")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0x20U});
        REQUIRE(session.queue_reliable(payload));
        const auto queued = snapshot(session);
        auto plan = prepare(session);

        // The payload transform itself is a total noexcept operation over a
        // bounded owned span. Exercise the encoder's pre-transform failure
        // boundary by allowing only the fixed header.
        const auto encoded = goldsrc::encode_client_to_server_netchan_packet(
            plan.packet(),
            goldsrc::NetchanPacketLimits{goldsrc::kNetchanHeaderSize});
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(encoded.error->code ==
              goldsrc::NetchanPacketErrorCode::packet_too_large);
        check_snapshot(session, queued);
        REQUIRE(session.abandon_outgoing_packet(std::move(plan)));
        check_snapshot(session, queued);
    }

    SECTION("explicit abandon and plan destruction consume no sequence")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0x21U});
        REQUIRE(session.queue_reliable(payload));
        const auto queued = snapshot(session);
        {
            auto destroyed = prepare(session);
            CHECK(destroyed.packet().header.sequence.sequence == sequence(1U));
        }
        check_snapshot(session, queued);

        auto abandoned = prepare(session);
        REQUIRE(session.abandon_outgoing_packet(std::move(abandoned)));
        check_snapshot(session, queued);
        const auto double_abandon =
            session.abandon_outgoing_packet(std::move(abandoned));
        REQUIRE_FALSE(double_abandon);
        REQUIRE(double_abandon.error);
        CHECK(double_abandon.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
    }

    SECTION("queue after prepare invalidates the stale snapshot")
    {
        goldsrc::NetchanSession session;
        const auto first = bytes({0x31U});
        const auto second = bytes({0x32U});
        REQUIRE(session.queue_reliable(first));
        auto stale = prepare(session);
        REQUIRE(session.queue_reliable(second));
        const auto queued = snapshot(session);
        const auto rejected = session.commit_outgoing_send(std::move(stale));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
        check_snapshot(session, queued);
    }

    SECTION("clear releases both buffers and invalidates prepared plans")
    {
        goldsrc::NetchanSession session;
        const auto first = bytes({0x41U});
        const auto second = bytes({0x42U});
        REQUIRE(session.queue_reliable(first));
        auto sent = prepare(session);
        commit(session, sent);
        REQUIRE(session.queue_reliable(second));
        auto stale = prepare(session);
        session.clear_reliable_state();
        CHECK(session.pending_reliable_payload().empty());
        CHECK_FALSE(session.in_flight_reliable_payload());
        const auto rejected = session.commit_outgoing_send(std::move(stale));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
        CHECK(session.state().next_outgoing_sequence == sequence(2U));
    }

    SECTION("abandoning a failed retransmission preserves active in-flight metadata")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0x43U, 0x44U});
        REQUIRE(session.queue_reliable(payload));
        auto first = prepare(session);
        commit(session, first); // reliable sequence 1 was locally sent

        auto gap = prepare(session);
        commit(session, gap); // plain sequence 2
        commit_incoming(session, server_header(1U, 2U, false, false));
        REQUIRE(session.in_flight_reliable_payload());
        REQUIRE(session.in_flight_reliable_payload()->retransmission_requested);

        auto failed_retry = prepare(session);
        REQUIRE(failed_retry.reliable_decision() ==
                goldsrc::ReliableTransmitDecision::retransmit);
        const auto before_failed_send = snapshot(session);
        REQUIRE(session.abandon_outgoing_packet(std::move(failed_retry)));
        check_snapshot(session, before_failed_send);
        REQUIRE(session.in_flight_reliable_payload());
        CHECK(session.in_flight_reliable_payload()->bytes == payload);
        CHECK(session.in_flight_reliable_payload()->first_sent_sequence ==
              sequence(1U));
        CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence ==
              sequence(1U));
        CHECK(session.in_flight_reliable_payload()->send_count == 1U);
    }
}

TEST_CASE("Every persistent-driver terminal outcome clears reliable ownership",
          "[goldsrc][netchan][reliable][terminal]")
{
    constexpr std::array terminal_outcomes{
        std::string_view{"timeout"},
        std::string_view{"cancel"},
        std::string_view{"network_error"},
        std::string_view{"protocol_error"},
    };

    for (const auto outcome : terminal_outcomes) {
        CAPTURE(outcome);
        goldsrc::NetchanSession session;
        const auto message_a = bytes({0x45U});
        const auto message_b = bytes({0x46U});
        REQUIRE(session.queue_reliable(message_a));
        auto sent = prepare(session);
        commit(session, sent);
        REQUIRE(session.queue_reliable(message_b));
        auto outstanding = prepare(session);
        const auto sequence_state = session.state();

        // An embedding transport owner maps timeout, explicit cancellation,
        // receive/network failure, and protocol failure to this one bounded
        // terminal cleanup primitive.
        session.clear_reliable_state();
        CHECK(session.pending_reliable_payload().empty());
        CHECK_FALSE(session.in_flight_reliable_payload());
        CHECK(session.state().next_outgoing_sequence ==
              sequence_state.next_outgoing_sequence);
        CHECK(session.state().last_outgoing_sequence ==
              sequence_state.last_outgoing_sequence);
        const auto stale = session.commit_outgoing_send(std::move(outstanding));
        REQUIRE_FALSE(stale);
        REQUIRE(stale.error);
        CHECK(stale.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
    }
}

TEST_CASE("Foreign and competing transaction tokens are rejected",
          "[goldsrc][netchan][reliable][transaction][identity]")
{
    SECTION("foreign outgoing plan remains valid for its owner")
    {
        goldsrc::NetchanSession owner;
        goldsrc::NetchanSession foreign;
        const auto payload = bytes({0x51U});
        REQUIRE(owner.queue_reliable(payload));
        REQUIRE(foreign.queue_reliable(payload));
        auto plan = prepare(owner);
        const auto foreign_before = snapshot(foreign);
        const auto rejected = foreign.commit_outgoing_send(std::move(plan));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::foreign_outgoing_transaction);
        check_snapshot(foreign, foreign_before);
        REQUIRE(owner.commit_outgoing_send(std::move(plan)));
    }

    SECTION("only the first equal-revision plan may commit")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0x52U});
        REQUIRE(session.queue_reliable(payload));
        auto first = prepare(session);
        auto stale = prepare(session);
        commit(session, first);
        const auto committed = snapshot(session);
        const auto rejected = session.commit_outgoing_send(std::move(stale));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
        check_snapshot(session, committed);
    }

    SECTION("foreign incoming inspection cannot mutate an equal-revision session")
    {
        goldsrc::NetchanSession owner;
        goldsrc::NetchanSession foreign;
        auto inspected = owner.inspect_incoming(server_header(1U, 0U, true));
        REQUIRE(inspected);
        const auto foreign_before = snapshot(foreign);
        const auto rejected = foreign.commit_incoming(
            std::move(*inspected.inspection));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::foreign_incoming_inspection);
        check_snapshot(foreign, foreign_before);
        REQUIRE(owner.commit_incoming(std::move(*inspected.inspection)));
    }
}

TEST_CASE("Outgoing reliable and one-shot unreliable bytes compose without truncation",
          "[goldsrc][netchan][reliable][unreliable][limits]")
{
    goldsrc::NetchanSession session{goldsrc::NetchanSessionLimits{24U, 12U, 16U}};
    const auto reliable = std::vector<std::byte>(12U, std::byte{0x61});
    const auto exact_unreliable = std::vector<std::byte>(4U, std::byte{0x62});
    REQUIRE(session.queue_reliable(reliable));
    const auto queued = snapshot(session);
    auto exact = prepare(session, exact_unreliable);
    REQUIRE(exact.packet().payload.size() == 16U);
    CHECK(std::ranges::equal(
        std::span<const std::byte>{exact.packet().payload}.first(12U),
        reliable));
    CHECK(std::ranges::equal(
        std::span<const std::byte>{exact.packet().payload}.subspan(12U),
        exact_unreliable));
    check_snapshot(session, queued);

    const auto too_much_unreliable =
        std::vector<std::byte>(5U, std::byte{0x63});
    const auto overflow = session.prepare_outgoing_packet(too_much_unreliable);
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error);
    CHECK(overflow.error->code ==
          goldsrc::NetchanSessionErrorCode::combined_payload_does_not_fit);
    check_snapshot(session, queued);

    const auto oversized_unreliable =
        std::vector<std::byte>(17U, std::byte{0x64});
    const auto oversized = session.prepare_outgoing_packet(oversized_unreliable);
    REQUIRE_FALSE(oversized);
    REQUIRE(oversized.error);
    CHECK(oversized.error->code ==
          goldsrc::NetchanSessionErrorCode::unreliable_payload_does_not_fit);
    check_snapshot(session, queued);
}

TEST_CASE("Unreliable-only packets are padded and never retained",
          "[goldsrc][netchan][reliable][unreliable]")
{
    goldsrc::NetchanSession session;
    const auto unreliable = bytes({0x71U, 0x72U, 0x73U});
    auto plan = prepare(session, unreliable);
    CHECK(plan.reliable_decision() == goldsrc::ReliableTransmitDecision::none);
    CHECK_FALSE(plan.packet().header.sequence.flags.reliable);
    REQUIRE(plan.packet().payload.size() == 8U);
    CHECK(std::ranges::equal(
        std::span<const std::byte>{plan.packet().payload}.first(3U),
        unreliable));
    commit(session, plan);
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload().empty());

    auto acknowledgement_only = prepare(session);
    CHECK(acknowledgement_only.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::none);
    CHECK(std::ranges::all_of(
        acknowledgement_only.packet().payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
}

TEST_CASE("Legacy first ACK stays outside the reliable lifecycle",
          "[goldsrc][netchan][reliable][acknowledgement][regression]")
{
    goldsrc::NetchanSession session;
    commit_incoming(session, server_header(1U, 0U, true));
    const auto reliable = bytes({0x79U, 0x7aU});
    REQUIRE(session.queue_reliable(reliable));
    auto first_acknowledgement = session.prepare_first_acknowledgement();
    REQUIRE(first_acknowledgement);
    REQUIRE(first_acknowledgement.transaction);
    CHECK_FALSE(
        first_acknowledgement.transaction->packet().header.sequence.flags.reliable);
    CHECK(std::ranges::all_of(
        first_acknowledgement.transaction->packet().payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    REQUIRE(session.commit_first_acknowledgement(
        std::move(*first_acknowledgement.transaction)));
    CHECK(session.pending_reliable_payload() == reliable);
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.outgoing_reliable_toggle());

    auto reliable_send = prepare(session);
    CHECK(reliable_send.packet().header.sequence.sequence == sequence(2U));
    CHECK(reliable_send.packet().header.sequence.flags.reliable);
    commit(session, reliable_send);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->toggle);
}

TEST_CASE("ACK gap requests retransmission with canonical bytes and fresh key",
          "[goldsrc][netchan][reliable][retransmission][capture]")
{
    goldsrc::NetchanSession session;
    const auto reliable = bytes({0x81U, 0x82U, 0x83U, 0x84U});
    const auto first_unreliable = bytes({0x91U});
    REQUIRE(session.queue_reliable(reliable));
    auto first = prepare(session, first_unreliable);
    const auto first_encoded =
        goldsrc::encode_client_to_server_netchan_packet(first.packet());
    REQUIRE(first_encoded);
    auto same_body_with_fresh_key = first.packet();
    same_body_with_fresh_key.header.sequence.sequence = sequence(3U);
    const auto fresh_key_encoded =
        goldsrc::encode_client_to_server_netchan_packet(same_body_with_fresh_key);
    REQUIRE(fresh_key_encoded);
    CHECK_FALSE(std::ranges::equal(
        std::span<const std::byte>{*first_encoded.datagram}.subspan(
            goldsrc::kNetchanHeaderSize),
        std::span<const std::byte>{*fresh_key_encoded.datagram}.subspan(
            goldsrc::kNetchanHeaderSize)));
    const auto decoded_first = goldsrc::decode_client_to_server_netchan_packet(
        *first_encoded.datagram);
    const auto decoded_fresh_key =
        goldsrc::decode_client_to_server_netchan_packet(
            *fresh_key_encoded.datagram);
    REQUIRE(decoded_first);
    REQUIRE(decoded_fresh_key);
    CHECK(decoded_first.packet->payload == decoded_fresh_key.packet->payload);
    CHECK(decoded_first.packet->payload == first.packet().payload);
    commit(session, first);

    // Matching numeric coverage with the old generation is not an ACK gap.
    commit_incoming(session, server_header(1U, 1U, false, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);

    auto intervening = prepare(session);
    CHECK(intervening.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
    CHECK_FALSE(intervening.packet().header.sequence.flags.reliable);
    commit(session, intervening);

    // Two stock drop-first runs confirm this exact advanced ACK-gap trigger.
    commit_incoming(session, server_header(2U, 2U, false, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->retransmission_requested);
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence == sequence(1U));

    const auto next_pending = bytes({0xa1U});
    const auto new_unreliable = bytes({0xa2U});
    REQUIRE(session.queue_reliable(next_pending));
    auto retransmission = prepare(session, new_unreliable);
    CHECK(retransmission.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::retransmit);
    CHECK(retransmission.packet().header.sequence.sequence == sequence(3U));
    CHECK(retransmission.packet().header.sequence.flags.reliable);
    CHECK(retransmission.reliable_payload_size() == reliable.size());
    CHECK(std::ranges::equal(
        std::span<const std::byte>{retransmission.packet().payload}.first(
            reliable.size()),
        reliable));
    CHECK(retransmission.packet().payload[reliable.size()] == new_unreliable[0]);

    const auto retry_encoded =
        goldsrc::encode_client_to_server_netchan_packet(retransmission.packet());
    REQUIRE(retry_encoded);
    const auto decoded_retry = goldsrc::decode_client_to_server_netchan_packet(
        *retry_encoded.datagram);
    REQUIRE(decoded_retry);
    CHECK(decoded_retry.packet->payload == retransmission.packet().payload);

    commit(session, retransmission);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->bytes == reliable);
    CHECK(session.in_flight_reliable_payload()->toggle);
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence == sequence(1U));
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence == sequence(3U));
    CHECK(session.in_flight_reliable_payload()->send_count == 2U);
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);
    CHECK(session.pending_reliable_payload() == next_pending);

    // A matching-bit ACK numerically between first and latest is deliberately
    // fail-closed because bounded stock capture cannot isolate that state.
    commit_incoming(session, server_header(3U, 2U, false, true));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence == sequence(3U));
    commit_incoming(session, server_header(4U, 3U, false, true));
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload() == next_pending);
}

TEST_CASE("Correct covering ACK clears while stale future and wrong ACKs do not",
          "[goldsrc][netchan][reliable][acknowledgement]")
{
    SECTION("matching bit plus first-send coverage clears")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0xb1U});
        REQUIRE(session.queue_reliable(payload));
        auto outgoing = prepare(session);
        commit(session, outgoing);
        commit_incoming(session, server_header(1U, 1U, false, true));
        CHECK_FALSE(session.in_flight_reliable_payload());

        // A duplicate correct ACK in a newer peer packet is harmless.
        commit_incoming(session, server_header(2U, 1U, false, true));
        CHECK_FALSE(session.in_flight_reliable_payload());
    }

    SECTION("future acknowledgement is fail-closed and mutation-free")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0xb3U});
        REQUIRE(session.queue_reliable(payload));
        auto outgoing = prepare(session);
        commit(session, outgoing);
        const auto before = snapshot(session);
        const auto inspected = session.inspect_incoming(
            server_header(1U, 2U, false, true));
        REQUIRE_FALSE(inspected);
        REQUIRE(inspected.error);
        CHECK(inspected.error->code ==
              goldsrc::NetchanSessionErrorCode::future_acknowledgement);
        check_snapshot(session, before);
    }

    SECTION("half-range acknowledgement versus latest is fail-closed")
    {
        goldsrc::NetchanSession session;
        const auto payload = bytes({0xb4U});
        REQUIRE(session.queue_reliable(payload));
        auto outgoing = prepare(session);
        commit(session, outgoing);
        const auto before = snapshot(session);
        const auto ambiguous_value =
            (1U + goldsrc::kNetchanSequenceHalfRange) &
            goldsrc::kNetchanSequenceMask;
        const auto inspected = session.inspect_incoming(
            server_header(1U, ambiguous_value, false, true));
        REQUIRE_FALSE(inspected);
        REQUIRE(inspected.error);
        CHECK(inspected.error->code == goldsrc::NetchanSessionErrorCode::
                                             acknowledgement_half_range_ambiguous);
        check_snapshot(session, before);
    }

    SECTION("stale and wrong-generation acknowledgements preserve in-flight")
    {
        goldsrc::NetchanSequenceState initial{
            sequence(11U),
            sequence(10U),
            sequence(0U),
            sequence(9U),
            false,
            false,
        };
        goldsrc::NetchanSession session{initial};
        const auto payload = bytes({0xb2U});
        REQUIRE(session.queue_reliable(payload));
        auto outgoing = prepare(session);
        commit(session, outgoing); // first reliable send is 11, toggle 1
        commit_incoming(session, server_header(1U, 8U, false, true));
        REQUIRE(session.in_flight_reliable_payload());
        CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);

        commit_incoming(session, server_header(2U, 11U, false, false));
        REQUIRE(session.in_flight_reliable_payload());
        CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);
    }
}

TEST_CASE("A second retry waits for an ACK gap past the most recent send",
          "[goldsrc][netchan][reliable][retransmission][capture]")
{
    goldsrc::NetchanSession session;
    const auto payload = bytes({0xaaU, 0xbbU});
    REQUIRE(session.queue_reliable(payload));
    auto first = prepare(session);
    commit(session, first); // reliable sequence 1

    auto gap_one = prepare(session);
    commit(session, gap_one); // plain sequence 2
    commit_incoming(session, server_header(1U, 2U, false, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->retransmission_requested);

    auto retry_one = prepare(session);
    commit(session, retry_one); // reliable sequence 3
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence == sequence(3U));
    CHECK(session.in_flight_reliable_payload()->send_count == 2U);

    // The old-generation ACK is not past latest=3, so it cannot request retry.
    commit_incoming(session, server_header(2U, 2U, false, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);

    auto gap_two = prepare(session);
    commit(session, gap_two); // plain sequence 4
    commit_incoming(session, server_header(3U, 4U, false, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->retransmission_requested);

    auto retry_two = prepare(session);
    CHECK(retry_two.packet().header.sequence.sequence == sequence(5U));
    CHECK(retry_two.packet().header.sequence.flags.reliable);
    CHECK(std::ranges::equal(
        std::span<const std::byte>{retry_two.packet().payload}.first(payload.size()),
        payload));
    commit(session, retry_two);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence == sequence(1U));
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence == sequence(5U));
    CHECK(session.in_flight_reliable_payload()->send_count == 3U);
}

TEST_CASE("Pending next message survives and receives the alternating generation",
          "[goldsrc][netchan][reliable][toggle]")
{
    goldsrc::NetchanSession session;
    const auto message_a = bytes({0xc1U});
    const auto message_b = bytes({0xc2U});
    REQUIRE(session.queue_reliable(message_a));
    auto send_a = prepare(session);
    CHECK(send_a.packet().header.sequence.flags.reliable);
    commit(session, send_a);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->toggle);

    REQUIRE(session.queue_reliable(message_b));
    auto blocked = prepare(session);
    CHECK(blocked.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
    CHECK_FALSE(blocked.packet().header.sequence.flags.reliable);
    CHECK(session.pending_reliable_payload() == message_b);

    commit_incoming(session, server_header(1U, 1U, false, true));
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload() == message_b);

    auto send_b = prepare(session);
    CHECK(send_b.reliable_decision() == goldsrc::ReliableTransmitDecision::send_new);
    CHECK(send_b.packet().header.sequence.flags.reliable);
    commit(session, send_b);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.in_flight_reliable_payload()->toggle);
    CHECK(session.in_flight_reliable_payload()->bytes == message_b);

    // Delayed generation-1 ACK for A cannot clear generation-0 message B.
    commit_incoming(session, server_header(2U, 1U, false, true));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->bytes == message_b);
    commit_incoming(session, server_header(3U, 2U, false, false));
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload().empty());
}

TEST_CASE("Incoming reliable presence toggles ACK generation only on committed newer packets",
          "[goldsrc][netchan][reliable][incoming]")
{
    goldsrc::NetchanSession session;
    auto first = session.inspect_incoming(server_header(1U, 0U, true));
    REQUIRE(first);
    REQUIRE(first.inspection);
    CHECK(first.inspection->contains_new_reliable_data());
    CHECK(first.inspection->incoming_reliable_acknowledgement_after_commit());
    REQUIRE(session.commit_incoming(std::move(*first.inspection)));
    CHECK(session.state().incoming_reliable_acknowledgement);

    const auto after_first = snapshot(session);
    auto duplicate = session.inspect_incoming(server_header(1U, 0U, true));
    REQUIRE(duplicate);
    CHECK_FALSE(duplicate.inspection->should_commit());
    CHECK_FALSE(duplicate.inspection->contains_new_reliable_data());
    const auto duplicate_commit = session.commit_incoming(
        std::move(*duplicate.inspection));
    REQUIRE_FALSE(duplicate_commit);
    check_snapshot(session, after_first);

    auto old = session.inspect_incoming(
        server_header(goldsrc::kNetchanSequenceMask, 0U, true));
    REQUIRE(old);
    CHECK(old.inspection->disposition() ==
          goldsrc::NetchanIncomingSequenceDisposition::older);
    CHECK_FALSE(old.inspection->contains_new_reliable_data());
    check_snapshot(session, after_first);

    commit_incoming(session, server_header(2U, 0U, false));
    CHECK(session.state().incoming_reliable_acknowledgement);
    commit_incoming(session, server_header(3U, 0U, true));
    CHECK_FALSE(session.state().incoming_reliable_acknowledgement);

    auto acknowledgement = prepare(session);
    CHECK(acknowledgement.packet().header.acknowledgement.sequence == sequence(3U));
    CHECK_FALSE(acknowledgement.packet().header.acknowledgement.reliable);
}

TEST_CASE("Fragment and stale receive transactions leave all reliable state unchanged",
          "[goldsrc][netchan][reliable][incoming][fragment]")
{
    SECTION("newer fragment returns the M2.3.3 boundary without commit")
    {
        goldsrc::NetchanSession session;
        auto fragment = session.inspect_incoming(
            server_header(1U, 0U, true, false, true));
        REQUIRE(fragment);
        const auto before = snapshot(session);
        const auto rejected = session.commit_incoming(
            std::move(*fragment.inspection));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::fragmented_payload_pending_m2_3_3);
        check_snapshot(session, before);
    }

    SECTION("receive commit after another state mutation is stale")
    {
        goldsrc::NetchanSession session;
        auto stale = session.inspect_incoming(server_header(1U, 0U, true));
        REQUIRE(stale);
        const auto queued = bytes({0xd1U});
        REQUIRE(session.queue_reliable(queued));
        const auto before = snapshot(session);
        const auto rejected = session.commit_incoming(std::move(*stale.inspection));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_incoming_inspection);
        check_snapshot(session, before);
    }
}

TEST_CASE("Reliable lifecycle crosses the 30-bit sequence wrap",
          "[goldsrc][netchan][reliable][wrap]")
{
    const goldsrc::NetchanSequenceState initial{
        sequence(goldsrc::kNetchanSequenceMask),
        sequence(goldsrc::kNetchanSequenceMask - 1U),
        sequence(0U),
        sequence(goldsrc::kNetchanSequenceMask - 1U),
        false,
        false,
    };
    goldsrc::NetchanSession session{initial};
    const auto payload = bytes({0xe1U, 0xe2U});
    REQUIRE(session.queue_reliable(payload));
    auto first = prepare(session);
    CHECK(first.packet().header.sequence.sequence ==
          sequence(goldsrc::kNetchanSequenceMask));
    commit(session, first);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence ==
          sequence(goldsrc::kNetchanSequenceMask));
    CHECK(session.state().next_outgoing_sequence == sequence(0U));

    auto gap_packet = prepare(session);
    commit(session, gap_packet);
    CHECK(session.state().next_outgoing_sequence == sequence(1U));
    commit_incoming(session, server_header(1U, 0U, false, false));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->retransmission_requested);

    auto retry = prepare(session);
    CHECK(retry.packet().header.sequence.sequence == sequence(1U));
    CHECK(retry.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::retransmit);
    commit(session, retry);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence ==
          sequence(goldsrc::kNetchanSequenceMask));
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence ==
          sequence(1U));

    const auto message_b = bytes({0xe3U});
    REQUIRE(session.queue_reliable(message_b));
    const auto before_future = snapshot(session);
    const auto future = session.inspect_incoming(
        server_header(2U, 2U, false, true));
    REQUIRE_FALSE(future);
    REQUIRE(future.error);
    CHECK(future.error->code ==
          goldsrc::NetchanSessionErrorCode::future_acknowledgement);
    check_snapshot(session, before_future);

    commit_incoming(
        session,
        server_header(2U, goldsrc::kNetchanSequenceMask, false, true));
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence ==
          sequence(1U));
    commit_incoming(session, server_header(3U, 1U, false, true));
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload() == message_b);

    auto second = prepare(session);
    CHECK(second.packet().header.sequence.sequence == sequence(2U));
    CHECK(second.packet().header.sequence.flags.reliable);
    commit(session, second);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.in_flight_reliable_payload()->toggle);
    CHECK(session.in_flight_reliable_payload()->bytes == message_b);
    commit_incoming(session, server_header(4U, 2U, false, false));
    CHECK_FALSE(session.in_flight_reliable_payload());
    CHECK(session.pending_reliable_payload().empty());
}

} // namespace
