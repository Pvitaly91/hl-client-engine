#include <hlclient/goldsrc/netchan_bootstrap_stage.hpp>

#include <exception>
#include <span>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool is_terminal_state(const NetchanBootstrapState state) noexcept
{
    switch (state) {
    case NetchanBootstrapState::complete:
    case NetchanBootstrapState::fragmented_payload_pending_m2_3_3:
    case NetchanBootstrapState::timed_out:
    case NetchanBootstrapState::cancelled:
    case NetchanBootstrapState::network_error:
    case NetchanBootstrapState::protocol_error:
        return true;
    case NetchanBootstrapState::idle:
    case NetchanBootstrapState::waiting_first:
    case NetchanBootstrapState::processing:
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

[[nodiscard]] bool headers_equal(
    const NetchanHeader& left,
    const NetchanHeader& right) noexcept
{
    return left.sequence.sequence == right.sequence.sequence &&
           left.sequence.flags == right.sequence.flags &&
           left.acknowledgement.sequence == right.acknowledgement.sequence &&
           left.acknowledgement.reliable == right.acknowledgement.reliable;
}

[[nodiscard]] bool acknowledgement_error(
    const NetchanSessionErrorCode code) noexcept
{
    return code == NetchanSessionErrorCode::future_acknowledgement ||
           code == NetchanSessionErrorCode::acknowledgement_half_range_ambiguous;
}

} // namespace

NetchanBootstrapStage::NetchanBootstrapStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    NetchanBootstrapConfig config,
    NetchanBootstrapTraceCallback trace_callback)
    : transport_{transport},
      remote_endpoint_{remote_endpoint},
      config_{config},
      trace_callback_{std::move(trace_callback)}
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
    state_ = NetchanBootstrapState::waiting_first;
    emit_trace(
        NetchanBootstrapTraceClassification::bootstrap_started,
        now,
        remote_endpoint_);
    return true;
}

void NetchanBootstrapStage::update(const NetchanBootstrapTimePoint now)
{
    if (trace_callback_active_ || state_ != NetchanBootstrapState::waiting_first) {
        return;
    }
    if (last_update_ && now < *last_update_) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::time_moved_backwards,
            std::nullopt,
            std::nullopt,
            "Netchan bootstrap time moved backwards",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_);
        return;
    }
    last_update_ = now;

    // A packet queued exactly at the deadline is late. The injected clock is
    // authoritative, so timeout is checked before polling the transport.
    if (first_packet_deadline_ && now >= *first_packet_deadline_) {
        state_ = NetchanBootstrapState::timed_out;
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
    }
}

void NetchanBootstrapStage::cancel(const NetchanBootstrapTimePoint now)
{
    if (trace_callback_active_ || state_ == NetchanBootstrapState::idle || terminal()) {
        return;
    }
    state_ = NetchanBootstrapState::cancelled;
    emit_trace(
        NetchanBootstrapTraceClassification::bootstrap_cancelled,
        now,
        remote_endpoint_);
}

NetchanBootstrapState NetchanBootstrapStage::state() const noexcept
{
    return state_;
}

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

std::size_t NetchanBootstrapStage::transmitted_packet_count() const noexcept
{
    return transmitted_packet_count_;
}

const NetchanSession& NetchanBootstrapStage::session() const noexcept
{
    return session_;
}

NetchanSession* NetchanBootstrapStage::persistent_session() noexcept
{
    return state_ == NetchanBootstrapState::complete ? &session_ : nullptr;
}

const NetchanSession* NetchanBootstrapStage::persistent_session() const noexcept
{
    return state_ == NetchanBootstrapState::complete ? &session_ : nullptr;
}

bool NetchanBootstrapStage::validate_start(
    const NetchanBootstrapTimePoint now,
    const network::NetworkAddress& expected_local_endpoint)
{
    const bool valid_timeout = config_.first_packet_timeout.count() > 0 &&
                               config_.first_packet_timeout <=
                                   kMaximumNetchanBootstrapTimeout &&
                               can_add(now, config_.first_packet_timeout);
    const bool valid_datagram_limit =
        config_.maximum_datagram_size >= kMinimumNetchanBootstrapDatagramSize &&
        config_.maximum_datagram_size <= kMaximumNetchanDatagramSize;
    const auto maximum_body_size = valid_datagram_limit
                                       ? config_.maximum_datagram_size - kNetchanHeaderSize
                                       : 0U;
    const bool valid_opaque_limit = config_.maximum_opaque_payload_size > 0U &&
                                    config_.maximum_opaque_payload_size <=
                                        kMaximumNetchanBootstrapOpaquePayloadSize &&
                                    config_.maximum_opaque_payload_size <= maximum_body_size;
    const bool valid_poll_limits =
        config_.maximum_datagrams_per_update > 0U &&
        config_.maximum_datagrams_per_update <=
            kMaximumNetchanBootstrapDatagramsPerUpdate &&
        config_.maximum_outgoing_packets_per_update > 0U &&
        config_.maximum_outgoing_packets_per_update <=
            kMaximumNetchanBootstrapOutgoingPacketsPerUpdate;
    const bool valid_endpoints = remote_endpoint_.ipv4_host_order() != 0U &&
                                 remote_endpoint_.port() != 0U &&
                                 expected_local_endpoint.port() != 0U;
    if (!valid_timeout || !valid_datagram_limit || !valid_opaque_limit ||
        !valid_poll_limits || !valid_endpoints || !session_.valid_configuration()) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_configuration,
            std::nullopt,
            session_.valid_configuration()
                ? std::nullopt
                : std::optional{NetchanSessionErrorCode::invalid_configuration},
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
            "Datagram transport local endpoint changed during netchan bootstrap",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_);
        return false;
    }
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
            "Datagram transport omitted the packet source endpoint",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_,
            received.payload_size);
        return false;
    }
    const auto source = *received.source;

    if (received.status == network::DatagramTransportReceiveStatus::received) {
        if (!received.datagram ||
            received.payload_size != received.datagram->payload.size() ||
            !received.error.empty()) {
            fail(
                NetchanBootstrapState::network_error,
                NetchanBootstrapErrorCode::inconsistent_receive_result,
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
            "Datagram transport returned an inconsistent receive status",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            source,
            received.payload_size);
        return false;
    }

    // Exact endpoint filtering deliberately precedes truncation and parsing.
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
            "Malformed datagram cannot contain a complete netchan header",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size);
        return false;
    }

    const auto peeked = peek_netchan_header(datagram);
    if (!peeked || !peeked.packet) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            peeked.error ? std::optional{peeked.error->code}
                         : std::optional{NetchanPacketErrorCode::datagram_too_short},
            std::nullopt,
            "Unable to peek the complete bounded netchan header",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size);
        return false;
    }
    const auto& header = peeked.packet->header;
    const auto encoded_body_size = datagram_size - kNetchanHeaderSize;
    emit_trace(
        NetchanBootstrapTraceClassification::sequenced_packet_received,
        now,
        remote_endpoint_,
        datagram_size,
        &header,
        encoded_body_size);

    const auto sequence_disposition = compare_sequences(
        header.sequence.sequence,
        session_.state().incoming_sequence);
    if (sequence_disposition == NetchanSequenceComparison::half_range_ambiguous) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_sequence,
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
    if (sequence_disposition != NetchanSequenceComparison::newer) {
        const auto ignored = sequence_disposition == NetchanSequenceComparison::equal
                                 ? NetchanBootstrapTraceClassification::
                                       duplicate_sequence_ignored
                                 : NetchanBootstrapTraceClassification::
                                       older_sequence_ignored;
        state_ = NetchanBootstrapState::waiting_first;
        emit_trace(
            ignored,
            now,
            remote_endpoint_,
            datagram_size,
            &header,
            encoded_body_size);
        return true;
    }

    // Only an admitted newer sequence reaches the strict transform/body codec.
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
    if (!headers_equal(packet.header, header)) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::malformed_packet,
            std::nullopt,
            std::nullopt,
            "Strict netchan decode disagreed with the bounded header peek",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &header,
            packet.payload.size());
        return false;
    }

    if (packet.header.sequence.flags.fragmented) {
        state_ = NetchanBootstrapState::fragmented_payload_pending_m2_3_3;
        error_ = NetchanBootstrapError{
            NetchanBootstrapErrorCode::fragmented_payload_pending_m2_3_3,
            std::nullopt,
            std::nullopt,
            "Fragmented netchan payload is deferred to M2.3.3",
        };
        emit_trace(
            NetchanBootstrapTraceClassification::fragmented_payload_pending_m2_3_3,
            now,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            packet.payload.size());
        return false;
    }

    // Fragmented traffic is a strict M2.3.3 boundary and must not enter ACK or
    // reliable lifecycle validation. Only a fully decoded newer unfragmented
    // packet may inspect and later commit persistent session state.
    auto inspected = session_.inspect_incoming(packet.header);
    if (!inspected || !inspected.inspection) {
        const auto session_code = inspected.error
                                      ? std::optional{inspected.error->code}
                                      : std::nullopt;
        const auto code = session_code && acknowledgement_error(*session_code)
                              ? NetchanBootstrapErrorCode::invalid_acknowledgement
                              : NetchanBootstrapErrorCode::invalid_sequence;
        fail(
            NetchanBootstrapState::protocol_error,
            code,
            std::nullopt,
            session_code,
            code == NetchanBootstrapErrorCode::invalid_acknowledgement
                ? "Netchan packet carries an impossible acknowledgement"
                : "Netchan session rejected the incoming header",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            packet.payload.size());
        return false;
    }
    auto inspection = std::move(*inspected.inspection);
    if (!inspection.should_commit()) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_sequence,
            std::nullopt,
            NetchanSessionErrorCode::incoming_not_newer,
            "Decoded netchan sequence changed before session inspection",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            packet.payload.size());
        return false;
    }

    if (packet.payload.size() > config_.maximum_opaque_payload_size) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::opaque_payload_too_large,
            std::nullopt,
            std::nullopt,
            "Decoded netchan payload exceeds the configured opaque payload bound",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            packet.payload.size());
        return false;
    }

    NetchanBootstrapResult candidate{
        OwnedNetchanPayload{
            std::move(packet.payload),
            packet.header.sequence.sequence,
            packet.header.acknowledgement.sequence,
            packet.header.sequence.flags,
            packet.header.acknowledgement.reliable,
            NetchanDirection::server_to_client,
            now,
        },
    };
    emit_trace(
        NetchanBootstrapTraceClassification::payload_ready,
        now,
        remote_endpoint_,
        datagram_size,
        &packet.header,
        candidate.payload.bytes.size());

    const auto committed = session_.commit_incoming(std::move(inspection));
    if (!committed) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_sequence,
            std::nullopt,
            committed.error ? std::optional{committed.error->code} : std::nullopt,
            "Netchan session could not commit the admitted first packet",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            candidate.payload.bytes.size());
        return false;
    }

    state_ = NetchanBootstrapState::ack_pending;
    if (!send_first_acknowledgement(
            now,
            outgoing_packets_this_update,
            packet.header,
            candidate.payload.bytes.size())) {
        return false;
    }

    result_ = std::move(candidate);
    state_ = NetchanBootstrapState::complete;
    emit_trace(
        NetchanBootstrapTraceClassification::bootstrap_complete,
        now,
        remote_endpoint_,
        datagram_size,
        &packet.header,
        result_->payload.bytes.size());
    return false;
}

bool NetchanBootstrapStage::send_first_acknowledgement(
    const NetchanBootstrapTimePoint now,
    std::size_t& outgoing_packets_this_update,
    const NetchanHeader& incoming_header,
    const std::size_t payload_size)
{
    if (outgoing_packets_this_update >= config_.maximum_outgoing_packets_per_update) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::invalid_configuration,
            std::nullopt,
            std::nullopt,
            "Netchan acknowledgement would exceed the bounded update TX budget",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &incoming_header,
            payload_size);
        return false;
    }

    NetchanFirstAcknowledgementPrepareResult prepared;
    try {
        prepared = session_.prepare_first_acknowledgement();
    } catch (...) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            std::nullopt,
            "Unable to prepare the bounded first netchan acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &incoming_header,
            payload_size);
        return false;
    }
    if (!prepared || !prepared.transaction) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            prepared.error ? std::optional{prepared.error->code} : std::nullopt,
            "Netchan session could not prepare the first acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &incoming_header,
            payload_size);
        return false;
    }

    const auto& acknowledgement = prepared.transaction->packet();
    NetchanPacketEncodeResult encoded;
    try {
        encoded = encode_client_to_server_netchan_packet(
            acknowledgement,
            NetchanPacketLimits{config_.maximum_datagram_size});
    } catch (...) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            std::nullopt,
            "Netchan encoder threw while producing the bounded first acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &acknowledgement.header,
            acknowledgement.payload.size());
        return false;
    }
    if (!encoded || !encoded.datagram) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            encoded.error ? std::optional{encoded.error->code} : std::nullopt,
            std::nullopt,
            encoded.error ? encoded.error->context
                          : "Unable to encode the first netchan acknowledgement",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            &acknowledgement.header,
            acknowledgement.payload.size());
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
            "Datagram transport threw while sending the first netchan acknowledgement",
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_,
            encoded.datagram->size(),
            &acknowledgement.header,
            acknowledgement.payload.size());
        return false;
    }
    if (!sent || !sent.error.empty()) {
        fail(
            NetchanBootstrapState::network_error,
            NetchanBootstrapErrorCode::send_failed,
            std::nullopt,
            std::nullopt,
            sent.error.empty() ? "Datagram transport failed to send the first ACK"
                               : std::move(sent.error),
            now,
            NetchanBootstrapTraceClassification::network_failure,
            remote_endpoint_,
            encoded.datagram->size(),
            &acknowledgement.header,
            acknowledgement.payload.size());
        return false;
    }
    ++transmitted_packet_count_;
    ++outgoing_packets_this_update;

    const auto committed = session_.commit_first_acknowledgement(
        std::move(*prepared.transaction));
    if (!committed) {
        fail(
            NetchanBootstrapState::protocol_error,
            NetchanBootstrapErrorCode::packet_encode_failed,
            std::nullopt,
            committed.error ? std::optional{committed.error->code} : std::nullopt,
            "Sent first netchan acknowledgement could not be committed",
            now,
            NetchanBootstrapTraceClassification::protocol_failure,
            remote_endpoint_,
            encoded.datagram->size(),
            &acknowledgement.header,
            acknowledgement.payload.size());
        return false;
    }

    emit_trace(
        NetchanBootstrapTraceClassification::acknowledgement_sent,
        now,
        remote_endpoint_,
        encoded.datagram->size(),
        &acknowledgement.header,
        acknowledgement.payload.size());
    return true;
}

void NetchanBootstrapStage::fail(
    const NetchanBootstrapState state,
    const NetchanBootstrapErrorCode code,
    const std::optional<NetchanPacketErrorCode> packet_code,
    const std::optional<NetchanSessionErrorCode> session_code,
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
    error_ = NetchanBootstrapError{
        code,
        packet_code,
        session_code,
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
    const std::size_t payload_size)
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
