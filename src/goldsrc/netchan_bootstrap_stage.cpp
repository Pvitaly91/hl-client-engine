#include <hlclient/goldsrc/netchan_bootstrap_stage.hpp>

#include <algorithm>
#include <exception>
#include <span>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool is_terminal_state(const NetchanBootstrapState state) noexcept
{
    switch (state) {
    case NetchanBootstrapState::complete:
    case NetchanBootstrapState::timed_out:
    case NetchanBootstrapState::cancelled:
    case NetchanBootstrapState::network_error:
    case NetchanBootstrapState::protocol_error:
    case NetchanBootstrapState::secondary_stream_pending_m3:
        return true;
    case NetchanBootstrapState::idle:
    case NetchanBootstrapState::sending_initial_packet:
    case NetchanBootstrapState::waiting_first:
    case NetchanBootstrapState::processing:
    case NetchanBootstrapState::waiting_fragments:
    case NetchanBootstrapState::ack_pending:
        return false;
    }
    return true;
}

[[nodiscard]] bool can_add(
    const NetchanBootstrapTimePoint time,
    const std::chrono::milliseconds duration) noexcept
{
    return duration.count() >= 0 && time <= NetchanBootstrapTimePoint::max() - duration;
}

[[nodiscard]] std::string bounded_text(std::string text)
{
    if (text.size() > kNetchanBootstrapDiagnosticTextLimit) {
        text.resize(kNetchanBootstrapDiagnosticTextLimit);
    }
    return text;
}

[[nodiscard]] bool is_padding_only(const std::span<const std::byte> payload) noexcept
{
    if (payload.empty()) {
        return true;
    }
    return payload.size() == kStockProtocol48MinimumDecodedPayloadSize &&
           std::ranges::all_of(payload, [](const std::byte value) {
               return value == kStockProtocol48NetchanPaddingByte;
           });
}

[[nodiscard]] bool valid_reassembly_limits(
    const NetchanNormalReassemblyLimits limits) noexcept
{
    return limits.maximum_reassembled_size > 0U &&
           limits.maximum_reassembled_size <= kMaximumNormalReassembledMessageSize &&
           limits.maximum_fragment_count > 0U &&
           limits.maximum_fragment_count <= kMaximumNormalFragmentsPerMessage &&
           limits.maximum_fragment_size > 0U &&
           limits.maximum_fragment_size <= kMaximumNormalFragmentPayloadSize;
}

[[nodiscard]] std::uint16_t fragment_index(const std::uint32_t fragment_id) noexcept
{
    return static_cast<std::uint16_t>(fragment_id >> 16U);
}

[[nodiscard]] std::uint16_t fragment_count(const std::uint32_t fragment_id) noexcept
{
    return static_cast<std::uint16_t>(fragment_id & 0xffffU);
}

} // namespace

NetchanBootstrapStage::NetchanBootstrapStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    NetchanBootstrapConfig config,
    NetchanBootstrapTraceCallback trace_callback)
    : transport_{transport},
      remote_endpoint_{remote_endpoint},
      config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      channel_{config_.channel_limits, config_.initial_state},
      reassembly_{config_.reassembly_limits}
{
}

bool NetchanBootstrapStage::start(
    const NetchanBootstrapTimePoint now,
    const network::NetworkAddress& expected_local_endpoint)
{
    if (trace_callback_active_ || state_ != NetchanBootstrapState::idle) {
        return false;
    }

    started_at_ = now;
    last_update_ = now;
    if (!validate_start(now, expected_local_endpoint)) {
        return false;
    }
    first_packet_deadline_ = now + config_.first_packet_timeout;

    if (!config_.initial_reliable_payload.empty()) {
        NetchanChannelOperationResult queued;
        try {
            queued = channel_.queue_reliable_payload(config_.initial_reliable_payload);
        } catch (...) {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::initial_reliable_payload_rejected,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Unable to copy the bounded initial reliable payload",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_);
            return false;
        }
        if (!queued) {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::initial_reliable_payload_rejected,
                std::nullopt,
                queued.error ? std::optional{queued.error->code} : std::nullopt,
                std::nullopt,
                "Netchan channel rejected the initial reliable payload",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_);
            return false;
        }
    }

    state_ = NetchanBootstrapState::sending_initial_packet;
    emit_trace(
        NetchanBootstrapTraceClassification::bootstrap_started,
        now,
        remote_endpoint_);

    NetchanOutgoingPrepareResult prepared;
    try {
        prepared = channel_.prepare_outgoing();
    } catch (...) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Unable to prepare the bounded initial netchan packet",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return false;
    }
    if (!prepared || !prepared.transaction) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            prepared.error ? std::optional{prepared.error->code} : std::nullopt,
            std::nullopt,
            "Netchan channel could not prepare the initial packet",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return false;
    }
    if (!send_prepared_packet(*prepared.transaction, now, true)) {
        return false;
    }

    state_ = NetchanBootstrapState::waiting_first;
    return true;
}

void NetchanBootstrapStage::update(const NetchanBootstrapTimePoint now)
{
    if (trace_callback_active_ ||
        (state_ != NetchanBootstrapState::waiting_first &&
         state_ != NetchanBootstrapState::waiting_fragments)) {
        return;
    }
    if (last_update_ && now < *last_update_) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::time_moved_backwards,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan bootstrap time moved backwards",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return;
    }
    last_update_ = now;

    const auto active_deadline = fragment_deadline_ ? fragment_deadline_
                                                    : first_packet_deadline_;
    // Deterministic/manual clocks are authoritative. A datagram queued exactly
    // at the active deadline is late, so time is checked before the transport.
    if (active_deadline && now >= *active_deadline) {
        state_ = NetchanBootstrapState::timed_out;
        reassembly_.reset();
        fragment_deadline_.reset();
        emit_trace(
            NetchanBootstrapTraceClassification::bootstrap_timed_out,
            now,
            remote_endpoint_);
        return;
    }
    if (!validate_local_continuity(now)) {
        return;
    }

    std::size_t outgoing_packets_this_update = 0U;
    for (std::size_t processed = 0U;
         processed < config_.maximum_datagrams_per_update;
         ++processed) {
        network::DatagramTransportReceiveResult received;
        try {
            received = transport_.receive(config_.maximum_datagram_size);
        } catch (...) {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::receive_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Datagram transport threw while receiving a netchan packet",
                now,
                NetchanBootstrapTraceClassification::network_failure,
                remote_endpoint_);
            return;
        }
        if (!process_receive_result(
                std::move(received),
                now,
                outgoing_packets_this_update)) {
            return;
        }
        if (outgoing_packets_this_update >=
            config_.maximum_outgoing_packets_per_update) {
            return;
        }
    }
}

void NetchanBootstrapStage::cancel(const NetchanBootstrapTimePoint now)
{
    if (trace_callback_active_ || state_ == NetchanBootstrapState::idle || terminal()) {
        return;
    }
    state_ = NetchanBootstrapState::cancelled;
    reassembly_.reset();
    fragment_deadline_.reset();
    emit_trace(
        NetchanBootstrapTraceClassification::bootstrap_cancelled,
        now,
        remote_endpoint_);
}

NetchanBootstrapState NetchanBootstrapStage::state() const noexcept { return state_; }

bool NetchanBootstrapStage::terminal() const noexcept
{
    return is_terminal_state(state_);
}

const network::NetworkAddress& NetchanBootstrapStage::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}

const std::optional<network::NetworkAddress>&
NetchanBootstrapStage::local_endpoint() const noexcept
{
    return local_endpoint_;
}

const std::optional<NetchanBootstrapResult>& NetchanBootstrapStage::result() const noexcept
{
    return result_;
}

const std::optional<NetchanBootstrapError>& NetchanBootstrapStage::error() const noexcept
{
    return error_;
}

const std::optional<NetchanBootstrapTimePoint>&
NetchanBootstrapStage::first_packet_deadline() const noexcept
{
    return first_packet_deadline_;
}

const std::optional<NetchanBootstrapTimePoint>&
NetchanBootstrapStage::fragment_deadline() const noexcept
{
    return fragment_deadline_;
}

std::size_t NetchanBootstrapStage::transmitted_packet_count() const noexcept
{
    return transmitted_packet_count_;
}

const NetchanChannel& NetchanBootstrapStage::channel() const noexcept
{
    return channel_;
}

bool NetchanBootstrapStage::validate_start(
    const NetchanBootstrapTimePoint now,
    const network::NetworkAddress& expected_local_endpoint)
{
    const bool valid_timeouts = config_.first_packet_timeout.count() > 0 &&
                                config_.first_packet_timeout <=
                                    kMaximumNetchanBootstrapTimeout &&
                                config_.fragment_completion_timeout.count() > 0 &&
                                config_.fragment_completion_timeout <=
                                    kMaximumNetchanBootstrapTimeout;
    const bool valid_poll_limits = config_.maximum_datagrams_per_update > 0U &&
                                   config_.maximum_datagrams_per_update <=
                                       kMaximumNetchanBootstrapDatagramsPerUpdate &&
                                   config_.maximum_outgoing_packets_per_update > 0U &&
                                   config_.maximum_outgoing_packets_per_update <=
                                       kMaximumNetchanBootstrapOutgoingPacketsPerUpdate;
    const bool valid_datagram_limit =
        config_.maximum_datagram_size >= kNetchanHeaderSize &&
        config_.maximum_datagram_size <= kMaximumNetchanDatagramSize;
    const auto maximum_body_size = valid_datagram_limit
                                       ? config_.maximum_datagram_size - kNetchanHeaderSize
                                       : 0U;
    const bool valid_channel_limits = channel_.valid_configuration() &&
                                      config_.channel_limits.maximum_packet_payload_size <=
                                          maximum_body_size &&
                                      config_.channel_limits.maximum_reliable_payload_size <=
                                          maximum_body_size &&
                                      config_.initial_reliable_payload.size() <=
                                          config_.channel_limits.maximum_reliable_payload_size;
    const bool valid_endpoints = remote_endpoint_.ipv4_host_order() != 0U &&
                                 remote_endpoint_.port() != 0U &&
                                 expected_local_endpoint.port() != 0U;
    if (!valid_timeouts || !valid_poll_limits || !valid_datagram_limit ||
        !valid_channel_limits || !valid_reassembly_limits(config_.reassembly_limits) ||
        !valid_endpoints || !can_add(now, config_.first_packet_timeout)) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_configuration,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Invalid netchan bootstrap configuration, state, time, or endpoint",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return false;
    }

    network::DatagramLocalAddressResult local;
    try {
        local = transport_.local_address();
    } catch (...) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::local_endpoint_unavailable,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport threw while querying the local endpoint",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
    if (!local || !local.address) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::local_endpoint_unavailable,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            local.error.empty() ? "Unable to query the local datagram endpoint"
                                : std::move(local.error),
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
    if (*local.address != expected_local_endpoint) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::local_endpoint_changed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport local endpoint changed before netchan bootstrap",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
    local_endpoint_ = *local.address;
    return true;
}

bool NetchanBootstrapStage::validate_local_continuity(
    const NetchanBootstrapTimePoint now)
{
    network::DatagramLocalAddressResult local;
    try {
        local = transport_.local_address();
    } catch (...) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::local_endpoint_unavailable,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport threw while checking local endpoint continuity",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
    if (!local || !local.address) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::local_endpoint_unavailable,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            local.error.empty() ? "Unable to verify the local datagram endpoint"
                                : std::move(local.error),
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
    if (!local_endpoint_ || *local.address != *local_endpoint_) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::local_endpoint_changed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport local endpoint changed during netchan bootstrap",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
    return true;
}

bool NetchanBootstrapStage::send_prepared_packet(
    const NetchanOutgoingTransaction& transaction,
    const NetchanBootstrapTimePoint now,
    const bool initial_packet)
{
    NetchanPacketEncodeResult encoded;
    try {
        encoded = encode_client_to_server_netchan_packet(
            transaction.packet(),
            NetchanPacketLimits{config_.maximum_datagram_size});
    } catch (...) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan packet encoder threw while producing a bounded datagram",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &transaction.packet().header,
            transaction.packet().payload.size());
        return false;
    }
    if (!encoded || !encoded.datagram) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            encoded.error ? std::optional{encoded.error->code} : std::nullopt,
            std::nullopt,
            std::nullopt,
            encoded.error ? encoded.error->context : "Unable to encode netchan packet",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &transaction.packet().header,
            transaction.packet().payload.size());
        return false;
    }

    network::DatagramSendResult sent;
    try {
        sent = transport_.send_to(remote_endpoint_, *encoded.datagram);
    } catch (...) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::send_failed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport threw while sending a netchan packet",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_,
            encoded.datagram->size(),
            &transaction.packet().header,
            transaction.packet().payload.size());
        return false;
    }
    if (!sent || !sent.error.empty()) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::send_failed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            sent.error.empty() ? "Datagram transport failed to send a netchan packet"
                               : std::move(sent.error),
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_,
            encoded.datagram->size(),
            &transaction.packet().header,
            transaction.packet().payload.size());
        return false;
    }
    ++transmitted_packet_count_;

    const auto committed = channel_.commit_outgoing(transaction);
    if (!committed) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            committed.error ? std::optional{committed.error->code} : std::nullopt,
            std::nullopt,
            "Sent netchan transaction could not be committed",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            encoded.datagram->size(),
            &transaction.packet().header,
            transaction.packet().payload.size());
        return false;
    }

    emit_trace(
        initial_packet ? NetchanBootstrapTraceClassification::initial_packet_sent
                       : NetchanBootstrapTraceClassification::acknowledgement_sent,
        now,
        remote_endpoint_,
        encoded.datagram->size(),
        &transaction.packet().header,
        transaction.packet().payload.size());
    return true;
}

bool NetchanBootstrapStage::process_receive_result(
    network::DatagramTransportReceiveResult received,
    const NetchanBootstrapTimePoint now,
    std::size_t& outgoing_packets_this_update)
{
    if (received.source && received.datagram &&
        *received.source != received.datagram->source) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::inconsistent_receive_result,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport reported conflicting source endpoints",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            *received.source,
            received.payload_size);
        return false;
    }

    if (received.status == network::DatagramTransportReceiveStatus::would_block) {
        if (received.datagram || received.source || received.payload_size != 0U ||
            !received.error.empty()) {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::inconsistent_receive_result,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Would-block receive result carried datagram metadata",
                now,
                NetchanBootstrapTraceClassification::network_failure,
                remote_endpoint_,
                received.payload_size);
            return false;
        }
        emit_trace(
            NetchanBootstrapTraceClassification::receive_would_block,
            now,
            remote_endpoint_);
        return false;
    }

    if (received.status == network::DatagramTransportReceiveStatus::error) {
        if (received.datagram || received.source || received.payload_size != 0U) {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::inconsistent_receive_result,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Failed receive result carried datagram metadata",
                now,
                NetchanBootstrapTraceClassification::network_failure,
                received.source.value_or(remote_endpoint_),
                received.payload_size);
        } else {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::receive_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                received.error.empty() ? "Datagram receive failed"
                                       : std::move(received.error),
                now,
                NetchanBootstrapTraceClassification::network_failure,
                remote_endpoint_);
        }
        return false;
    }

    if (!received.source) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::inconsistent_receive_result,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport omitted the packet source endpoint",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_,
            received.payload_size);
        return false;
    }
    const auto source = *received.source;

    if (received.status == network::DatagramTransportReceiveStatus::received) {
        if (!received.datagram || received.payload_size != received.datagram->payload.size() ||
            !received.error.empty()) {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::inconsistent_receive_result,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Datagram transport returned inconsistent payload metadata",
                now,
                NetchanBootstrapTraceClassification::network_failure,
                source,
                received.payload_size);
            return false;
        }
    } else if (received.status == network::DatagramTransportReceiveStatus::truncated) {
        if (received.datagram) {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::inconsistent_receive_result,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Truncated receive result unexpectedly contained a datagram",
                now,
                NetchanBootstrapTraceClassification::network_failure,
                source,
                received.payload_size);
            return false;
        }
    } else {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::inconsistent_receive_result,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Datagram transport returned an inconsistent receive status",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            source,
            received.payload_size);
        return false;
    }

    // Endpoint validation deliberately precedes truncation and parsing. An
    // oversized spoofed datagram cannot terminate or mutate the real channel.
    if (source != remote_endpoint_) {
        emit_trace(
            NetchanBootstrapTraceClassification::wrong_endpoint_ignored,
            now,
            source,
            received.payload_size);
        return true;
    }

    if (received.status == network::DatagramTransportReceiveStatus::truncated) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::datagram_truncated,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan datagram exceeds the configured project limit",
            now,
            NetchanBootstrapTraceClassification::datagram_truncated,
            source,
            received.payload_size);
        return false;
    }

    if (!received.datagram) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::inconsistent_receive_result,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Received status omitted the datagram payload",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            source,
            received.payload_size);
        return false;
    }
    if (received.datagram->payload.size() > config_.maximum_datagram_size) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::datagram_truncated,
            NetchanPacketErrorCode::datagram_too_large,
            std::nullopt,
            std::nullopt,
            "Transport returned a netchan datagram above its requested size bound",
            now,
            NetchanBootstrapTraceClassification::datagram_truncated,
            source,
            received.datagram->payload.size());
        return false;
    }
    return process_target_datagram(
        std::move(received.datagram->payload),
        now,
        outgoing_packets_this_update);
}

bool NetchanBootstrapStage::process_target_datagram(
    std::vector<std::byte> datagram,
    const NetchanBootstrapTimePoint now,
    std::size_t& outgoing_packets_this_update)
{
    const auto datagram_size = datagram.size();
    state_ = NetchanBootstrapState::processing;
    const auto classification = classify_netchan_datagram(datagram);
    if (classification.classification == NetchanDatagramClassification::connectionless) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::unexpected_connectionless_packet,
            NetchanPacketErrorCode::connectionless_packet,
            std::nullopt,
            std::nullopt,
            "Unexpected connectionless packet after ACCEPT",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size);
        return false;
    }
    if (classification.classification ==
        NetchanDatagramClassification::unsupported_special) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::unsupported_special_packet,
            NetchanPacketErrorCode::unsupported_special_packet,
            std::nullopt,
            std::nullopt,
            "Unsupported split/special packet during netchan bootstrap",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size);
        return false;
    }
    if (classification.classification != NetchanDatagramClassification::sequenced) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            NetchanPacketErrorCode::datagram_too_short,
            std::nullopt,
            std::nullopt,
            "Malformed datagram cannot contain a complete netchan header",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size);
        return false;
    }

    const auto peeked_header = peek_netchan_header(datagram);
    if (!peeked_header || !peeked_header.packet) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            peeked_header.error
                ? std::optional{peeked_header.error->code}
                : std::optional{NetchanPacketErrorCode::datagram_too_short},
            std::nullopt,
            std::nullopt,
            "Unable to peek the complete bounded netchan header",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size);
        return false;
    }
    const auto& header = peeked_header.packet->header;
    const auto encoded_body_size = datagram_size - kNetchanHeaderSize;
    emit_trace(
        NetchanBootstrapTraceClassification::sequenced_packet_received,
        now,
        remote_endpoint_,
        datagram_size,
        &header,
        encoded_body_size);

    auto inspected = channel_.inspect_incoming(header);
    if (!inspected || !inspected.inspection) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_acknowledgement,
            std::nullopt,
            inspected.error ? std::optional{inspected.error->code} : std::nullopt,
            std::nullopt,
            "Netchan packet carries an invalid acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return false;
    }
    auto inspection = std::move(*inspected.inspection);
    if (inspection.disposition() ==
        NetchanIncomingSequenceDisposition::half_range_ambiguous) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_sequence,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan sequence is exactly half the wrap range from current state",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return false;
    }
    if (!inspection.should_commit()) {
        const auto ignored = inspection.disposition() ==
                                     NetchanIncomingSequenceDisposition::duplicate
                                 ? NetchanBootstrapTraceClassification::
                                       duplicate_sequence_ignored
                                 : NetchanBootstrapTraceClassification::older_sequence_ignored;
        state_ = reassembly_.active() ? NetchanBootstrapState::waiting_fragments
                                      : NetchanBootstrapState::waiting_first;
        emit_trace(
            ignored,
            now,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return true;
    }

    // Only newer sequences reach the strict body transform/fragment parser.
    // The inspection token remains uncommitted until every downstream check
    // and bounded reassembly operation succeeds.
    ServerToClientNetchanDecodeResult decoded;
    try {
        decoded = decode_server_to_client_netchan_packet(
            datagram,
            NetchanPacketLimits{config_.maximum_datagram_size});
    } catch (...) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan decoder threw while processing a bounded datagram",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return false;
    }
    if (!decoded || !decoded.packet) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            decoded.error ? std::optional{decoded.error->code} : std::nullopt,
            std::nullopt,
            std::nullopt,
            decoded.error ? decoded.error->context : "Unable to decode netchan packet",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return false;
    }
    auto& packet = *decoded.packet;
    const bool header_matches_peek =
        packet.header.sequence.sequence == header.sequence.sequence &&
        packet.header.sequence.flags == header.sequence.flags &&
        packet.header.acknowledgement.sequence ==
            header.acknowledgement.sequence &&
        packet.header.acknowledgement.reliable ==
            header.acknowledgement.reliable;
    if (!header_matches_peek) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Strict netchan decode disagreed with the bounded header peek",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return false;
    }

    const auto incoming_toggle_before = channel_.incoming_reliable_toggle();
    auto reliable_unit = packet.header.sequence.flags.reliable
                             ? NetchanIncomingReliableUnit::new_unit
                             : NetchanIncomingReliableUnit::none;
    std::optional<NetchanBootstrapResult> candidate;
    std::optional<std::uint16_t> packet_fragment_count;
    std::optional<NetchanBootstrapTraceClassification> fragment_trace;

    if (packet.header.sequence.flags.fragmented) {
        if (packet.fragments[1U]) {
            state_ = NetchanBootstrapState::secondary_stream_pending_m3;
            error_ = NetchanBootstrapError{
                NetchanBootstrapErrorCode::secondary_stream_pending_m3,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Secondary netchan fragment stream is deferred to M3",
            };
            reassembly_.reset();
            fragment_deadline_.reset();
            emit_trace(
                NetchanBootstrapTraceClassification::secondary_stream_pending_m3,
                now,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                packet.payload.size());
            return false;
        }
        if (!packet.fragments[0U] || packet.fragments[0U]->offset != 0U ||
            packet.fragments[0U]->payload_offset != 0U ||
            static_cast<std::size_t>(packet.fragments[0U]->length) !=
                packet.payload.size()) {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::invalid_fragment_layout,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Stock bootstrap profile requires offset-zero slot0 to cover the entire payload",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                packet.payload.size());
            return false;
        }

        const auto& descriptor = *packet.fragments[0U];
        const auto index = fragment_index(descriptor.fragment_id);
        const auto count = fragment_count(descriptor.fragment_id);
        packet_fragment_count = count;
        const auto fragment = std::span<const std::byte>{packet.payload}.subspan(
            descriptor.payload_offset,
            descriptor.length);
        NetchanNormalReassemblyResult assembled;
        try {
            assembled = reassembly_.add_fragment(index, count, fragment);
        } catch (...) {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::fragment_reassembly_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Normal fragment reassembly threw at a bounded allocation boundary",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                packet.payload.size());
            return false;
        }
        if (!assembled) {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::fragment_reassembly_failed,
                std::nullopt,
                std::nullopt,
                assembled.error ? std::optional{assembled.error->code} : std::nullopt,
                assembled.error ? assembled.error->context
                                : "Normal fragment reassembly failed",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                packet.payload.size());
            return false;
        }

        if (assembled.status == NetchanNormalReassemblyStatus::duplicate_ignored) {
            if (packet.header.sequence.flags.reliable) {
                reliable_unit = NetchanIncomingReliableUnit::exact_retransmission;
            }
            fragment_trace =
                NetchanBootstrapTraceClassification::fragment_duplicate_ignored;
        } else if (assembled.status ==
                   NetchanNormalReassemblyStatus::fragment_accepted) {
            if (!fragment_deadline_) {
                if (!can_add(now, config_.fragment_completion_timeout)) {
                    fail(
                        NetchanBootstrapState::protocol_error,
                        NetchanBootstrapErrorCode::invalid_configuration,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        "Fragment deadline cannot be represented by the supplied clock",
                        now,
                        NetchanBootstrapTraceClassification::protocol_failure,
                        remote_endpoint_,
                        datagram_size,
                        &packet.header,
                        packet.payload.size());
                    return false;
                }
                fragment_deadline_ = now + config_.fragment_completion_timeout;
            }
            fragment_trace = NetchanBootstrapTraceClassification::fragment_accepted;
        } else if (assembled.status == NetchanNormalReassemblyStatus::complete &&
                   assembled.payload) {
            candidate = NetchanBootstrapResult{
                OwnedNetchanPayload{
                    std::move(assembled.payload->bytes),
                    packet.header.sequence.sequence,
                    packet.header.sequence.flags.reliable,
                    true,
                    assembled.payload->fragment_count,
                    now,
                },
            };
            fragment_deadline_.reset();
        } else {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::fragment_reassembly_failed,
                std::nullopt,
                std::nullopt,
                NetchanNormalReassemblyErrorCode::internal_invariant,
                "Normal fragment reassembler returned an inconsistent result",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                packet.payload.size());
            return false;
        }
    } else {
        if (reassembly_.active() && !is_padding_only(packet.payload)) {
            fail(
                NetchanBootstrapState::protocol_error,
                NetchanBootstrapErrorCode::invalid_fragment_layout,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Unfragmented payload interrupted an active normal transfer",
                now,
                NetchanBootstrapTraceClassification::protocol_failure,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                packet.payload.size());
            return false;
        }
        if (!is_padding_only(packet.payload)) {
            candidate = NetchanBootstrapResult{
                OwnedNetchanPayload{
                    std::move(packet.payload),
                    packet.header.sequence.sequence,
                    packet.header.sequence.flags.reliable,
                    false,
                    0U,
                    now,
                },
            };
        }
    }

    channel_.commit_incoming(std::move(inspection), reliable_unit);
    const auto expected_toggle =
        reliable_unit == NetchanIncomingReliableUnit::new_unit
            ? !incoming_toggle_before
            : incoming_toggle_before;
    if (channel_.incoming_sequence() != packet.header.sequence.sequence ||
        channel_.incoming_reliable_toggle() != expected_toggle) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_sequence,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan incoming transaction did not commit atomically",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            packet.payload.size());
        return false;
    }

    if (fragment_trace) {
        emit_trace(
            *fragment_trace,
            now,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            packet.payload.size(),
            packet_fragment_count);
    }
    if (candidate) {
        emit_trace(
            NetchanBootstrapTraceClassification::payload_ready,
            now,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            candidate->payload.bytes.size(),
            packet_fragment_count);
    }

    state_ = NetchanBootstrapState::ack_pending;
    if (!send_acknowledgement(now, outgoing_packets_this_update)) {
        return false;
    }

    if (candidate) {
        result_ = std::move(candidate);
        state_ = NetchanBootstrapState::complete;
        emit_trace(
            NetchanBootstrapTraceClassification::bootstrap_complete,
            now,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            result_->payload.bytes.size(),
            packet_fragment_count);
        return false;
    }

    state_ = reassembly_.active() ? NetchanBootstrapState::waiting_fragments
                                  : NetchanBootstrapState::waiting_first;
    return true;
}

bool NetchanBootstrapStage::send_acknowledgement(
    const NetchanBootstrapTimePoint now,
    std::size_t& outgoing_packets_this_update)
{
    if (outgoing_packets_this_update >=
        config_.maximum_outgoing_packets_per_update) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_configuration,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Netchan acknowledgement would exceed the bounded update TX budget",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return false;
    }

    NetchanOutgoingPrepareResult prepared;
    try {
        prepared = channel_.prepare_outgoing();
    } catch (...) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Unable to prepare bounded netchan acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return false;
    }
    if (!prepared || !prepared.transaction) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            prepared.error ? std::optional{prepared.error->code} : std::nullopt,
            std::nullopt,
            "Netchan channel could not prepare acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return false;
    }
    if (!send_prepared_packet(*prepared.transaction, now, false)) {
        return false;
    }
    ++outgoing_packets_this_update;
    return true;
}

void NetchanBootstrapStage::fail(
    const NetchanBootstrapState state,
    const NetchanBootstrapErrorCode code,
    const std::optional<NetchanPacketErrorCode> packet_code,
    const std::optional<NetchanChannelErrorCode> channel_code,
    const std::optional<NetchanNormalReassemblyErrorCode> reassembly_code,
    std::string context,
    const NetchanBootstrapTimePoint now,
    const NetchanBootstrapTraceClassification classification,
    const network::NetworkAddress endpoint,
    const std::size_t datagram_size,
    const NetchanHeader* const header,
    const std::size_t payload_size)
{
    state_ = state;
    result_.reset();
    reassembly_.reset();
    fragment_deadline_.reset();
    error_ = NetchanBootstrapError{
        code,
        packet_code,
        channel_code,
        reassembly_code,
        bounded_text(std::move(context)),
    };
    emit_trace(
        classification,
        now,
        endpoint,
        datagram_size,
        header,
        payload_size);
}

void NetchanBootstrapStage::emit_trace(
    const NetchanBootstrapTraceClassification classification,
    const NetchanBootstrapTimePoint now,
    const network::NetworkAddress endpoint,
    const std::size_t datagram_size,
    const NetchanHeader* const header,
    const std::size_t payload_size,
    const std::optional<std::uint16_t> fragment_count_value)
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }

    NetchanBootstrapTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.elapsed = elapsed(now);
    event.endpoint = endpoint;
    event.datagram_size = datagram_size;
    event.payload_size = payload_size;
    event.received_fragment_count = reassembly_.received_fragment_count();
    event.fragment_count = fragment_count_value;
    event.transmitted_packet_count = transmitted_packet_count_;
    if (header != nullptr) {
        event.sequence = header->sequence.sequence.value();
        event.acknowledgement = header->acknowledgement.sequence.value();
        event.reliable = header->sequence.flags.reliable;
        event.fragmented = header->sequence.flags.fragmented;
        event.reliable_acknowledgement = header->acknowledgement.reliable;
    }

    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

std::chrono::milliseconds NetchanBootstrapStage::elapsed(
    const NetchanBootstrapTimePoint now) const noexcept
{
    if (!started_at_ || now <= *started_at_) {
        return std::chrono::milliseconds{0};
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *started_at_);
}

} // namespace hlclient::goldsrc
