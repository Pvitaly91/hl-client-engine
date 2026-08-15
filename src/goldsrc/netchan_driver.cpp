#include <hlclient/goldsrc/netchan_driver.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(const NetchanDriverState state) noexcept
{
    switch (state) {
    case NetchanDriverState::cancelled:
    case NetchanDriverState::timed_out:
    case NetchanDriverState::network_error:
    case NetchanDriverState::protocol_error:
    case NetchanDriverState::backpressure:
    case NetchanDriverState::closed:
        return true;
    case NetchanDriverState::idle:
    case NetchanDriverState::active:
        return false;
    }
    return true;
}

[[nodiscard]] bool valid_remote_endpoint(
    const network::NetworkAddress& endpoint) noexcept
{
    return endpoint.ipv4_host_order() != 0U && endpoint.port() != 0U;
}

[[nodiscard]] bool valid_local_endpoint(
    const network::NetworkAddress& endpoint) noexcept
{
    // A UDP socket may intentionally remain bound to INADDR_ANY after its
    // ephemeral port is assigned. Same-socket continuity is the exact address
    // object equality check below; only the local port must be concrete.
    return endpoint.port() != 0U;
}

[[nodiscard]] bool acknowledgement_error(const NetchanSessionErrorCode code) noexcept
{
    return code == NetchanSessionErrorCode::future_acknowledgement ||
           code == NetchanSessionErrorCode::acknowledgement_half_range_ambiguous;
}

[[nodiscard]] std::string bounded_context(std::string context)
{
    if (context.size() > kNetchanDriverDiagnosticTextLimit) {
        context.resize(kNetchanDriverDiagnosticTextLimit);
    }
    return context;
}

[[nodiscard]] NetchanDriverOperationResult operation_failure(
    const NetchanDriverErrorCode code,
    std::string context,
    const std::optional<NetchanPacketErrorCode> packet_code = std::nullopt,
    const std::optional<NetchanSessionErrorCode> session_code = std::nullopt)
{
    return NetchanDriverOperationResult{NetchanDriverError{
        code,
        packet_code,
        session_code,
        bounded_context(std::move(context)),
    }};
}

[[nodiscard]] bool headers_equal(
    const NetchanHeader& left,
    const NetchanHeader& right) noexcept
{
    return left.sequence.sequence == right.sequence.sequence &&
           left.sequence.flags.reliable == right.sequence.flags.reliable &&
           left.sequence.flags.fragmented == right.sequence.flags.fragmented &&
           left.acknowledgement.sequence == right.acknowledgement.sequence &&
           left.acknowledgement.reliable == right.acknowledgement.reliable;
}

class EventRing final {
public:
    explicit EventRing(const std::size_t capacity) : slots_(capacity) {}

    [[nodiscard]] bool can_push(const std::size_t count = 1U) const noexcept
    {
        return count <= slots_.size() - size_;
    }

    void push(NetchanDriverEvent event) noexcept
    {
        const auto index = (head_ + size_) % slots_.size();
        slots_[index].emplace(std::move(event));
        ++size_;
    }

    [[nodiscard]] std::optional<NetchanDriverEvent> pop() noexcept
    {
        if (size_ == 0U) {
            return std::nullopt;
        }
        auto event = std::move(slots_[head_]);
        slots_[head_].reset();
        head_ = (head_ + 1U) % slots_.size();
        --size_;
        return event;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::vector<std::optional<NetchanDriverEvent>> slots_;
    std::size_t head_{0U};
    std::size_t size_{0U};
};

static_assert(std::is_nothrow_move_constructible_v<NetchanDriverEvent>);

[[nodiscard]] NetchanSessionLimits session_limits_for(
    const NetchanDriverConfig& config) noexcept
{
    if (config.maximum_datagram_size < kNetchanHeaderSize ||
        config.maximum_datagram_size > kMaximumNetchanDatagramSize) {
        return {};
    }
    return NetchanSessionLimits{
        config.maximum_datagram_size,
        config.maximum_datagram_size - kNetchanHeaderSize,
        kMaximumPendingReliablePayload,
    };
}

[[nodiscard]] NetchanReassemblyLimits reassembly_limits_for(
    const NetchanDriverConfig& config) noexcept
{
    return NetchanReassemblyLimits{
        config.maximum_fragment_payload_size,
        config.maximum_normal_transfer_size,
        config.maximum_fragments_per_transfer,
        config.maximum_active_normal_transfers,
        config.maximum_fragment_ranges,
        config.fragment_transfer_timeout,
    };
}

[[nodiscard]] bool incoming_acknowledgement_clears_reliable(
    const NetchanSession& session,
    const NetchanIncomingInspection& inspection) noexcept
{
    const auto& in_flight = session.in_flight_reliable_payload();
    const auto& acknowledgement = inspection.acknowledgement();
    if (!in_flight || !acknowledgement ||
        acknowledgement->disposition !=
            NetchanAcknowledgementDisposition::advanced ||
        acknowledgement->reliable != in_flight->toggle) {
        return false;
    }
    const auto coverage = compare_sequences(
        acknowledgement->sequence,
        in_flight->most_recent_sent_sequence);
    return coverage == NetchanSequenceComparison::equal ||
           coverage == NetchanSequenceComparison::newer;
}

[[nodiscard]] bool incoming_acknowledgement_completes_reliable_payload(
    const NetchanSession& session,
    const NetchanIncomingInspection& inspection) noexcept
{
    if (!incoming_acknowledgement_clears_reliable(session, inspection)) {
        return false;
    }
    const auto& transfer = session.outgoing_fragment_transfer();
    return !transfer ||
           transfer->current_fragment_index == transfer->fragment_count;
}

} // namespace

bool valid_configuration(const NetchanDriverConfig& config) noexcept
{
    if (config.channel_inactivity_timeout <= std::chrono::milliseconds::zero() ||
        config.channel_inactivity_timeout > kMaximumNetchanChannelInactivityTimeout ||
        config.fragment_transfer_timeout <= std::chrono::milliseconds::zero() ||
        config.fragment_transfer_timeout > kMaximumNetchanFragmentTransferTimeout ||
        config.maximum_datagram_size <
            kNetchanHeaderSize + kStockProtocol48MinimumDecodedPayloadSize ||
        config.maximum_datagram_size > kMaximumNetchanDatagramSize ||
        config.maximum_fragment_datagram_size <
            kNetchanHeaderSize + kStockProtocol48PresentFragmentDescriptorSize +
                kStockProtocol48FragmentPresenceSize + 1U ||
        config.maximum_fragment_datagram_size >
            kMaximumNetchanFragmentDatagramSize ||
        config.maximum_fragment_datagram_size > config.maximum_datagram_size ||
        config.maximum_fragment_payload_size !=
            kStockProtocol48NormalFragmentChunkSize ||
        config.maximum_fragment_payload_size + kNetchanHeaderSize +
                kStockProtocol48PresentFragmentDescriptorSize +
                kStockProtocol48FragmentPresenceSize >
            config.maximum_fragment_datagram_size ||
        config.maximum_normal_transfer_size == 0U ||
        config.maximum_normal_transfer_size >
            kMaximumNetchanNormalTransferSize ||
        config.maximum_fragments_per_transfer == 0U ||
        config.maximum_fragments_per_transfer >
            kMaximumNetchanFragmentsPerTransfer ||
        config.maximum_active_normal_transfers == 0U ||
        config.maximum_active_normal_transfers >
            kMaximumActiveNormalTransfers ||
        config.maximum_fragment_ranges == 0U ||
        config.maximum_fragment_ranges > kMaximumNetchanFragmentRanges ||
        config.maximum_fragment_ranges < config.maximum_fragments_per_transfer ||
        config.maximum_fragments_per_transfer >
            std::numeric_limits<std::size_t>::max() /
                kStockProtocol48NormalFragmentChunkSize ||
        config.maximum_opaque_payload_size == 0U ||
        config.maximum_opaque_payload_size > kMaximumNetchanDriverOpaquePayloadSize ||
        config.maximum_opaque_payload_size > config.maximum_normal_transfer_size ||
        config.maximum_unreliable_payload_size == 0U ||
        config.maximum_unreliable_payload_size >
            config.maximum_datagram_size - kNetchanHeaderSize ||
        config.maximum_datagrams_per_update == 0U ||
        config.maximum_datagrams_per_update > kMaximumNetchanDriverDatagramsPerUpdate ||
        config.maximum_outgoing_packets_per_update == 0U ||
        config.maximum_outgoing_packets_per_update >
            kMaximumNetchanDriverOutgoingPacketsPerUpdate ||
        config.maximum_events < kMinimumNetchanDriverEvents ||
        config.maximum_events > kMaximumNetchanDriverEvents) {
        return false;
    }
    if (config.maximum_normal_transfer_size >
        config.maximum_fragments_per_transfer *
            kStockProtocol48NormalFragmentChunkSize) {
        return false;
    }
    return true;
}

class NetchanDriver::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        NetchanDriverConfig config,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime,
        NetchanDriverTraceCallback trace_callback)
        : transport_{transport},
          remote_endpoint_{remote_endpoint},
          config_{std::move(config)},
          connection_lifetime_{std::move(connection_lifetime)},
          trace_callback_{std::move(trace_callback)},
          session_{session_limits_for(config_)},
          reassembler_{reassembly_limits_for(config_)},
          events_{::hlclient::goldsrc::valid_configuration(config_)
                      ? config_.maximum_events
                      : 0U},
          configuration_valid_{
              ::hlclient::goldsrc::valid_configuration(config_) &&
              session_.valid_configuration() &&
              reassembler_.valid_configuration() &&
              valid_remote_endpoint(remote_endpoint_)}
    {
    }

    ~Implementation() { cleanup(); }

    [[nodiscard]] bool start(
        const NetchanDriverTimePoint now,
        const network::NetworkAddress& expected_local_endpoint)
    {
        if (trace_callback_active_ || state_ != NetchanDriverState::idle) {
            return false;
        }
        if (!configuration_valid_ ||
            !valid_local_endpoint(expected_local_endpoint)) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_configuration,
                "Netchan driver configuration or endpoint is invalid",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }

        network::DatagramLocalAddressResult local;
        try {
            local = transport_.local_address();
        } catch (...) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::local_endpoint_unavailable,
                "Datagram transport threw while querying its local endpoint",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        if (!local || !local.address || !local.error.empty()) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::local_endpoint_unavailable,
                local.error.empty() ? "Datagram transport has no bound local endpoint"
                                    : std::move(local.error),
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        if (*local.address != expected_local_endpoint) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::local_endpoint_changed,
                "Datagram transport local endpoint changed before driver start",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }

        local_endpoint_ = *local.address;
        started_at_ = now;
        last_update_ = now;
        last_valid_packet_time_ = now;
        state_ = NetchanDriverState::active;
        emit_trace(
            NetchanDriverTraceClassification::driver_started,
            remote_endpoint_);
        return true;
    }

    void update(const NetchanDriverTimePoint now)
    {
        if (trace_callback_active_ || state_ != NetchanDriverState::active) {
            return;
        }
        if (!last_update_ || now < *last_update_) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::time_moved_backwards,
                "Netchan driver update time moved backwards",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return;
        }
        last_update_ = now;
        if (!validate_local_continuity(now)) {
            return;
        }
        if (events_.size() == config_.maximum_events) {
            enter_backpressure(now);
            return;
        }

        std::size_t outgoing_packets_this_update = 0U;
        for (std::size_t received_count = 0U;
             received_count < config_.maximum_datagrams_per_update &&
             state_ == NetchanDriverState::active &&
             outgoing_packets_this_update <
                 config_.maximum_outgoing_packets_per_update;
             ++received_count) {
            network::DatagramTransportReceiveResult received;
            try {
                received = transport_.receive(config_.maximum_datagram_size);
            } catch (...) {
                enter_terminal(
                    NetchanDriverState::network_error,
                    NetchanDriverErrorCode::receive_failed,
                    "Datagram transport threw while polling netchan traffic",
                    now,
                    NetchanDriverTraceClassification::network_failure,
                    NetchanDriverEventType::network_error);
                return;
            }
            if (!process_receive_result(
                    std::move(received),
                    now,
                    outgoing_packets_this_update)) {
                break;
            }
        }

        if (state_ != NetchanDriverState::active) {
            return;
        }
        if (!check_fragment_timeout(now)) {
            return;
        }
        if (last_valid_packet_time_ &&
            now - *last_valid_packet_time_ >= config_.channel_inactivity_timeout) {
            enter_terminal(
                NetchanDriverState::timed_out,
                NetchanDriverErrorCode::channel_inactivity_timed_out,
                "Netchan channel inactivity timeout elapsed",
                now,
                NetchanDriverTraceClassification::channel_timed_out,
                NetchanDriverEventType::channel_timed_out);
            return;
        }

        send_pending(now, outgoing_packets_this_update);
    }

    void cancel(const NetchanDriverTimePoint now)
    {
        if (trace_callback_active_ || state_ != NetchanDriverState::active) {
            return;
        }
        enter_terminal(
            NetchanDriverState::cancelled,
            std::nullopt,
            {},
            now,
            NetchanDriverTraceClassification::driver_cancelled,
            NetchanDriverEventType::cancelled);
    }

    void close(const NetchanDriverTimePoint now)
    {
        if (trace_callback_active_ || terminal_state(state_)) {
            return;
        }
        state_ = NetchanDriverState::closed;
        cleanup();
        emit_trace(NetchanDriverTraceClassification::driver_closed, remote_endpoint_);
        static_cast<void>(now);
    }

    [[nodiscard]] NetchanDriverOperationResult queue_reliable(
        const std::span<const std::byte> payload)
    {
        if (trace_callback_active_) {
            return operation_failure(
                NetchanDriverErrorCode::reentrant_operation,
                "Netchan trace callbacks cannot mutate driver state");
        }
        if (state_ != NetchanDriverState::active) {
            return operation_failure(
                NetchanDriverErrorCode::not_active,
                "Reliable payload requires an active netchan driver");
        }
        const auto queued = session_.queue_reliable(payload);
        if (!queued) {
            return operation_failure(
                NetchanDriverErrorCode::reliable_queue_failed,
                "Netchan session rejected the bounded reliable payload",
                std::nullopt,
                queued.error ? std::optional{queued.error->code} : std::nullopt);
        }
        return {};
    }

    [[nodiscard]] NetchanDriverOperationResult submit_unreliable(
        const std::span<const std::byte> payload)
    {
        if (trace_callback_active_) {
            return operation_failure(
                NetchanDriverErrorCode::reentrant_operation,
                "Netchan trace callbacks cannot mutate driver state");
        }
        if (state_ != NetchanDriverState::active) {
            return operation_failure(
                NetchanDriverErrorCode::not_active,
                "Unreliable payload requires an active netchan driver");
        }
        if (payload.empty()) {
            return {};
        }
        if (payload.size() > config_.maximum_unreliable_payload_size) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_payload_too_large,
                "Unreliable payload exceeds the configured one-shot bound");
        }
        if (!pending_unreliable_payload_.empty()) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_payload_pending,
                "Only one owning unreliable payload may be pending");
        }
        pending_unreliable_payload_.assign(payload.begin(), payload.end());
        return {};
    }

    [[nodiscard]] std::optional<NetchanDriverEvent> poll_event() noexcept
    {
        return events_.pop();
    }

    [[nodiscard]] bool valid_configuration() const noexcept
    {
        return configuration_valid_;
    }

    [[nodiscard]] NetchanDriverState state() const noexcept { return state_; }
    [[nodiscard]] bool terminal() const noexcept { return terminal_state(state_); }
    [[nodiscard]] const std::optional<NetchanDriverError>& last_error() const noexcept
    {
        return last_error_;
    }
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept
    {
        return remote_endpoint_;
    }
    [[nodiscard]] const std::optional<network::NetworkAddress>&
    local_endpoint() const noexcept
    {
        return local_endpoint_;
    }
    [[nodiscard]] const std::optional<NetchanDriverTimePoint>&
    last_valid_packet_time() const noexcept
    {
        return last_valid_packet_time_;
    }
    [[nodiscard]] std::size_t pending_event_count() const noexcept
    {
        return events_.size();
    }
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept
    {
        return transmitted_packet_count_;
    }
    [[nodiscard]] std::size_t cleanup_count() const noexcept { return cleanup_count_; }
    [[nodiscard]] const NetchanSession& session() const noexcept { return session_; }
    [[nodiscard]] NetchanSession& compatibility_session() noexcept { return session_; }
    [[nodiscard]] const NetchanNormalReassembler& normal_reassembler() const noexcept
    {
        return reassembler_;
    }

private:
    [[nodiscard]] bool check_fragment_timeout(const NetchanDriverTimePoint now)
    {
        const auto& active = reassembler_.active_transfer();
        if (!active || now < active->deadline) {
            return true;
        }
        if (!ensure_event_capacity(1U, now)) {
            return false;
        }
        const auto expired = reassembler_.expire(now);
        if (!expired) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::fragment_reassembly_failed,
                "Normal fragment reassembler could not expire its bounded state",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                std::nullopt,
                expired.error ? std::optional{expired.error->code} : std::nullopt);
            return false;
        }
        if (!expired.timed_out_transfer) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::fragment_reassembly_failed,
                "Expired normal transfer did not report its owning identity",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        enter_terminal(
            NetchanDriverState::timed_out,
            NetchanDriverErrorCode::fragment_transfer_timed_out,
            "Incomplete normal fragment transfer reached its fixed deadline",
            now,
            NetchanDriverTraceClassification::normal_transfer_timed_out,
            NetchanDriverEventType::normal_transfer_timed_out);
        return false;
    }

    [[nodiscard]] bool validate_local_continuity(const NetchanDriverTimePoint now)
    {
        network::DatagramLocalAddressResult local;
        try {
            local = transport_.local_address();
        } catch (...) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::local_endpoint_unavailable,
                "Datagram transport threw while checking same-socket continuity",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        if (!local || !local.address || !local.error.empty()) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::local_endpoint_unavailable,
                local.error.empty() ? "Datagram transport lost its local endpoint"
                                    : std::move(local.error),
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        if (!local_endpoint_ || *local.address != *local_endpoint_) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::local_endpoint_changed,
                "Datagram transport local endpoint changed during netchan",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool process_receive_result(
        network::DatagramTransportReceiveResult received,
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        using Status = network::DatagramTransportReceiveStatus;
        if (received.status == Status::would_block) {
            if (received.datagram || received.source || received.payload_size != 0U ||
                !received.error.empty()) {
                enter_terminal(
                    NetchanDriverState::network_error,
                    NetchanDriverErrorCode::inconsistent_receive_result,
                    "Would-block receive result carried unexpected data",
                    now,
                    NetchanDriverTraceClassification::network_failure,
                    NetchanDriverEventType::network_error);
                return false;
            }
            emit_trace(
                NetchanDriverTraceClassification::receive_would_block,
                remote_endpoint_);
            return false;
        }

        if (received.status == Status::error) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::receive_failed,
                received.error.empty() ? "Datagram transport receive failed"
                                       : std::move(received.error),
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }

        if (!received.source) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::inconsistent_receive_result,
                "Datagram receive result omitted its source endpoint",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        if (*received.source != remote_endpoint_) {
            emit_trace(
                NetchanDriverTraceClassification::wrong_endpoint_ignored,
                *received.source,
                received.payload_size);
            return true;
        }

        if (received.status == Status::truncated) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::datagram_truncated,
                received.error.empty() ? "Exact-endpoint netchan datagram was truncated"
                                       : std::move(received.error),
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (received.status != Status::received || !received.datagram ||
            received.datagram->source != *received.source ||
            received.payload_size != received.datagram->payload.size() ||
            !received.error.empty()) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::inconsistent_receive_result,
                "Received datagram result is internally inconsistent",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        return process_target_datagram(
            std::move(received.datagram->payload),
            now,
            outgoing_packets_this_update);
    }

    [[nodiscard]] bool process_target_datagram(
        std::vector<std::byte> datagram,
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        const auto datagram_size = datagram.size();
        const auto classification = classify_netchan_datagram(datagram);
        if (classification.classification == NetchanDatagramClassification::connectionless) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::unexpected_connectionless_packet,
                "Unexpected connectionless packet in active netchan",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                NetchanPacketErrorCode::connectionless_packet);
            return false;
        }
        if (classification.classification ==
            NetchanDatagramClassification::unsupported_special) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::unsupported_special_packet,
                "Unsupported split/special packet in active netchan",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                NetchanPacketErrorCode::unsupported_special_packet);
            return false;
        }
        if (classification.classification != NetchanDatagramClassification::sequenced) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                "Malformed datagram cannot contain a complete netchan header",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                NetchanPacketErrorCode::datagram_too_short);
            return false;
        }

        NetchanHeaderPeekResult peeked;
        try {
            peeked = peek_netchan_header(datagram);
        } catch (...) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                "Netchan header decoder threw for a bounded datagram",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (!peeked || !peeked.packet) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                peeked.error ? peeked.error->context
                             : "Unable to decode the complete netchan header",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                peeked.error ? std::optional{peeked.error->code} : std::nullopt);
            return false;
        }
        const auto header = peeked.packet->header;
        emit_trace(
            NetchanDriverTraceClassification::sequenced_packet_received,
            remote_endpoint_,
            datagram_size,
            &header,
            datagram_size - kNetchanHeaderSize);

        const auto sequence_disposition = compare_sequences(
            header.sequence.sequence,
            session_.state().incoming_sequence);
        if (sequence_disposition == NetchanSequenceComparison::half_range_ambiguous) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_sequence,
                "Netchan sequence is exactly half the wrap range from current state",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (sequence_disposition != NetchanSequenceComparison::newer) {
            emit_trace(
                sequence_disposition == NetchanSequenceComparison::equal
                    ? NetchanDriverTraceClassification::duplicate_sequence_ignored
                    : NetchanDriverTraceClassification::older_sequence_ignored,
                remote_endpoint_,
                datagram_size,
                &header,
                datagram_size - kNetchanHeaderSize);
            return true;
        }

        ServerToClientNetchanDecodeResult decoded;
        try {
            decoded = decode_server_to_client_netchan_packet(
                datagram,
                NetchanPacketLimits{
                    header.sequence.flags.fragmented
                        ? config_.maximum_fragment_datagram_size
                        : config_.maximum_datagram_size});
        } catch (...) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                "Netchan packet decoder threw for a bounded datagram",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (!decoded || !decoded.packet) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                decoded.error ? decoded.error->context : "Unable to decode netchan packet",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                decoded.error ? std::optional{decoded.error->code} : std::nullopt);
            return false;
        }
        auto& packet = *decoded.packet;
        if (!headers_equal(packet.header, header)) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                "Strict packet decode disagreed with the bounded header peek",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (packet.header.sequence.flags.fragmented) {
            return process_fragment_packet(
                packet,
                datagram_size,
                now,
                outgoing_packets_this_update);
        }
        if (packet.payload.size() > config_.maximum_opaque_payload_size) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::opaque_payload_too_large,
                "Decoded netchan payload exceeds the configured owning bound",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }

        auto inspected = session_.inspect_incoming(packet.header);
        if (!inspected || !inspected.inspection) {
            const auto session_code = inspected.error
                                          ? std::optional{inspected.error->code}
                                          : std::nullopt;
            enter_terminal(
                NetchanDriverState::protocol_error,
                session_code && acknowledgement_error(*session_code)
                    ? NetchanDriverErrorCode::invalid_acknowledgement
                    : NetchanDriverErrorCode::invalid_sequence,
                "Netchan session rejected the incoming header",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                session_code);
            return false;
        }
        auto inspection = std::move(*inspected.inspection);
        if (!inspection.should_commit()) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_sequence,
                "Decoded netchan sequence changed before session inspection",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                NetchanSessionErrorCode::incoming_not_newer);
            return false;
        }

        const bool completes_reliable =
            incoming_acknowledgement_completes_reliable_payload(
                session_, inspection);
        const std::size_t required_events = completes_reliable ? 2U : 1U;
        if (!ensure_event_capacity(required_events, now)) {
            return false;
        }

        OwnedNetchanPayload payload{
            std::move(packet.payload),
            packet.header.sequence.sequence,
            packet.header.acknowledgement.sequence,
            packet.header.sequence.flags,
            packet.header.acknowledgement.reliable,
            NetchanDirection::server_to_client,
            now,
        };
        const auto committed = session_.commit_incoming(std::move(inspection));
        if (!committed) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_sequence,
                "Netchan session could not commit an admitted packet",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                committed.error ? std::optional{committed.error->code} : std::nullopt);
            return false;
        }

        if (!session_.first_acknowledgement_sent()) {
            if (!send_first_acknowledgement(now, outgoing_packets_this_update)) {
                return false;
            }
        } else if (packet.header.sequence.flags.reliable) {
            if (!send_transport_acknowledgement(
                    now,
                    outgoing_packets_this_update)) {
                return false;
            }
        }

        last_valid_packet_time_ = now;
        if (completes_reliable) {
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::reliable_payload_acknowledged,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                now,
            });
        }
        const auto payload_size = payload.bytes.size();
        events_.push(NetchanDriverEvent{
            NetchanDriverEventType::payload_ready,
            std::move(payload),
            payload_size,
            0U,
            0U,
            payload_size,
            now,
        });
        emit_trace(
            NetchanDriverTraceClassification::payload_ready,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            payload_size);
        // An unfragmented datagram contributes at most one owning payload.
        // A completed fragment unit may separately contribute one completed
        // transfer and one contemporaneous suffix.
        return !config_.yield_after_owning_payload &&
               outgoing_packets_this_update <
                   config_.maximum_outgoing_packets_per_update;
    }

    [[nodiscard]] bool process_fragment_packet(
        ServerToClientNetchanPacket& packet,
        const std::size_t datagram_size,
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        if (packet.fragments[1U]) {
            if (!ensure_event_capacity(1U, now)) {
                return false;
            }
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::secondary_stream_pending_m3,
                "Strict slot-1 fragment bytes are outside the confirmed M2.3.3 profile",
                now,
                NetchanDriverTraceClassification::secondary_stream_pending_m3,
                NetchanDriverEventType::secondary_stream_pending_m3,
                std::nullopt,
                std::nullopt,
                NetchanReassemblyErrorCode::secondary_stream_pending_m3);
            return false;
        }
        if (!packet.fragments[0U]) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::fragment_reassembly_failed,
                "Fragmented packet has no supported normal-stream descriptor",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                NetchanPacketErrorCode::fragment_flag_without_descriptor);
            return false;
        }

        const auto& descriptor = *packet.fragments[0U];
        const auto range_offset = descriptor.packet_payload_offset();
        const auto range_length = descriptor.packet_payload_length();
        if (range_offset > packet.fragment_payload_size ||
            range_length > packet.fragment_payload_size - range_offset) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::malformed_packet,
                "Normal descriptor range exceeds the strictly decoded fragment prefix",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                NetchanPacketErrorCode::fragment_payload_out_of_bounds);
            return false;
        }

        const auto suffix_size =
            packet.payload.size() - packet.fragment_payload_size;
        if (suffix_size > config_.maximum_opaque_payload_size) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::opaque_payload_too_large,
                "Contemporaneous fragment suffix exceeds the configured owning payload bound",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }

        NetchanFragmentInsertPrepareResult prepared;
        try {
            prepared = reassembler_.prepare_insert(
                descriptor,
                std::span<const std::byte>{packet.payload}.subspan(
                    range_offset,
                    range_length),
                now);
        } catch (...) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::fragment_reassembly_failed,
                "Normal fragment reassembler threw while preparing a bounded insert",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (!prepared || !prepared.plan) {
            const auto reassembly_code = prepared.error
                                             ? std::optional{prepared.error->code}
                                             : std::nullopt;
            if (reassembly_code ==
                NetchanReassemblyErrorCode::transfer_deadline_expired) {
                return check_fragment_timeout(now);
            }
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::fragment_reassembly_failed,
                "Normal fragment reassembler rejected the validated descriptor range",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                std::nullopt,
                reassembly_code);
            return false;
        }

        const auto reliable_classification =
            prepared.plan->exact_retransmission()
                ? NetchanIncomingReliableUnitClassification::
                      exact_fragment_retransmission
                : NetchanIncomingReliableUnitClassification::new_fragment_unit;
        auto inspected = session_.inspect_incoming(
            packet.header,
            reliable_classification);
        if (!inspected || !inspected.inspection) {
            const auto session_code = inspected.error
                                          ? std::optional{inspected.error->code}
                                          : std::nullopt;
            enter_terminal(
                NetchanDriverState::protocol_error,
                session_code && acknowledgement_error(*session_code)
                    ? NetchanDriverErrorCode::invalid_acknowledgement
                    : NetchanDriverErrorCode::invalid_sequence,
                "Netchan session rejected a transactionally prepared fragment",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                session_code);
            return false;
        }
        auto inspection = std::move(*inspected.inspection);
        if (!inspection.should_commit()) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_sequence,
                "Fragment packet sequence changed before transactional inspection",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                NetchanSessionErrorCode::incoming_not_newer);
            return false;
        }

        const bool completes_reliable =
            incoming_acknowledgement_completes_reliable_payload(
                session_, inspection);
        const bool has_completion = prepared.plan->completion_size().has_value();
        const auto prepared_completion_size =
            prepared.plan->completion_size().value_or(0U);
        if (has_completion &&
            *prepared.plan->completion_size() >
                config_.maximum_opaque_payload_size) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::opaque_payload_too_large,
                "Completed normal transfer would exceed the configured owning payload bound",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        std::size_t required_events = prepared.plan->required_event_count();
        required_events += has_completion ? 1U : 0U;
        required_events += suffix_size != 0U ? 1U : 0U;
        required_events += completes_reliable ? 1U : 0U;
        if (!ensure_event_capacity(required_events, now)) {
            return false;
        }

        const bool emits_owning_payload = has_completion || suffix_size != 0U;
        std::optional<OwnedNetchanPayload> ordinary_suffix;
        if (suffix_size != 0U) {
            try {
                ordinary_suffix.emplace(OwnedNetchanPayload{
                    std::vector<std::byte>{
                        packet.payload.begin() +
                            static_cast<std::ptrdiff_t>(packet.fragment_payload_size),
                        packet.payload.end()},
                    packet.header.sequence.sequence,
                    packet.header.acknowledgement.sequence,
                    packet.header.sequence.flags,
                    packet.header.acknowledgement.reliable,
                    NetchanDirection::server_to_client,
                    now,
                });
            } catch (...) {
                enter_terminal(
                    NetchanDriverState::protocol_error,
                    NetchanDriverErrorCode::opaque_payload_too_large,
                    "Unable to retain bounded contemporaneous fragment suffix",
                    now,
                    NetchanDriverTraceClassification::protocol_failure,
                    NetchanDriverEventType::protocol_error);
                return false;
            }
        }

        auto inserted = reassembler_.commit_insert(std::move(*prepared.plan));
        if (!inserted || !inserted.receipt) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::fragment_reassembly_failed,
                "Prepared normal fragment insert could not be committed",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                std::nullopt,
                inserted.error ? std::optional{inserted.error->code} : std::nullopt);
            return false;
        }
        const auto committed = session_.commit_incoming(std::move(inspection));
        if (!committed) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_sequence,
                "Prepared fragment session inspection could not be committed",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                committed.error ? std::optional{committed.error->code} : std::nullopt);
            return false;
        }

        if (!session_.first_acknowledgement_sent()) {
            if (!send_first_acknowledgement(now, outgoing_packets_this_update)) {
                return false;
            }
        } else if (!send_transport_acknowledgement(
                       now,
                       outgoing_packets_this_update)) {
            return false;
        }

        last_valid_packet_time_ = now;
        const auto& receipt = *inserted.receipt;
        if (completes_reliable) {
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::reliable_payload_acknowledged,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                now,
            });
        }
        if (receipt.started_transfer) {
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::normal_transfer_started,
                std::nullopt,
                0U,
                receipt.range.transfer_offset,
                receipt.range.length,
                receipt.covered_size,
                now,
            });
        }
        emit_trace(
            NetchanDriverTraceClassification::fragment_received,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            range_length,
            &receipt,
            prepared_completion_size);
        if (inserted.completion) {
            const auto completion_size = inserted.completion->payload.size();
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::normal_transfer_completed,
                std::nullopt,
                completion_size,
                receipt.range.transfer_offset,
                receipt.range.length,
                receipt.covered_size,
                now,
            });
            OwnedNetchanPayload completion{
                std::move(inserted.completion->payload),
                packet.header.sequence.sequence,
                packet.header.acknowledgement.sequence,
                packet.header.sequence.flags,
                packet.header.acknowledgement.reliable,
                NetchanDirection::server_to_client,
                now,
            };
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::payload_ready,
                std::move(completion),
                completion_size,
                0U,
                0U,
                completion_size,
                now,
            });
            emit_trace(
                NetchanDriverTraceClassification::normal_transfer_completed,
                remote_endpoint_,
                datagram_size,
                &packet.header,
                completion_size,
                &receipt,
                completion_size);
        }
        if (ordinary_suffix) {
            const auto payload_size = ordinary_suffix->bytes.size();
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::payload_ready,
                std::move(ordinary_suffix),
                payload_size,
                0U,
                0U,
                payload_size,
                now,
            });
        }
        return !(config_.yield_after_owning_payload && emits_owning_payload) &&
               outgoing_packets_this_update <
                   config_.maximum_outgoing_packets_per_update;
    }

    [[nodiscard]] bool send_first_acknowledgement(
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        if (outgoing_packets_this_update >= config_.maximum_outgoing_packets_per_update) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_configuration,
                "First acknowledgement exceeds the bounded TX update budget",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        NetchanFirstAcknowledgementPrepareResult prepared;
        try {
            prepared = session_.prepare_first_acknowledgement();
        } catch (...) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Netchan session threw while preparing the first acknowledgement",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        if (!prepared || !prepared.transaction) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Netchan session could not prepare the first acknowledgement",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                prepared.error ? std::optional{prepared.error->code} : std::nullopt);
            return false;
        }
        const auto& packet = prepared.transaction->packet();
        auto encoded = encode_packet(packet, now);
        if (!encoded) {
            return false;
        }
        if (!send_datagram(*encoded, packet.header, packet.payload.size(), now)) {
            return false;
        }
        ++outgoing_packets_this_update;
        const auto committed = session_.commit_first_acknowledgement(
            std::move(*prepared.transaction));
        if (!committed) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Sent first acknowledgement could not be committed",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                committed.error ? std::optional{committed.error->code} : std::nullopt);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool send_transport_acknowledgement(
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        if (outgoing_packets_this_update >=
            config_.maximum_outgoing_packets_per_update) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_configuration,
                "Required fragment acknowledgement exceeds the bounded TX budget",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }

        auto prepared = session_.prepare_outgoing_packet();
        if (!prepared || !prepared.plan) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Netchan session could not prepare the required transport acknowledgement",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                prepared.error ? std::optional{prepared.error->code} : std::nullopt);
            return false;
        }

        const auto& packet = prepared.plan->packet();
        auto encoded = encode_packet(packet, now);
        if (!encoded) {
            static_cast<void>(session_.abandon_outgoing_packet(
                std::move(*prepared.plan)));
            return false;
        }
        if (!send_datagram(*encoded, packet.header, packet.payload.size(), now)) {
            static_cast<void>(session_.abandon_outgoing_packet(
                std::move(*prepared.plan)));
            return false;
        }
        ++outgoing_packets_this_update;
        const auto committed = session_.commit_outgoing_send(
            std::move(*prepared.plan));
        if (!committed) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Sent transport acknowledgement could not be committed",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                committed.error ? std::optional{committed.error->code} : std::nullopt);
            return false;
        }
        acknowledgement_pending_ = false;
        return true;
    }

    void send_pending(
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        if (!session_.first_acknowledgement_sent()) {
            return;
        }
        while (state_ == NetchanDriverState::active &&
               outgoing_packets_this_update < config_.maximum_outgoing_packets_per_update) {
            const auto& in_flight = session_.in_flight_reliable_payload();
            const bool reliable_work = !session_.pending_reliable_payload().empty() ||
                                       (in_flight && in_flight->retransmission_requested) ||
                                       session_.has_outgoing_fragment_send_work();
            if (!acknowledgement_pending_ && pending_unreliable_payload_.empty() &&
                !reliable_work) {
                return;
            }

            auto prepared = session_.prepare_outgoing_packet(pending_unreliable_payload_);
            if (!prepared || !prepared.plan) {
                const auto session_code = prepared.error
                                              ? std::optional{prepared.error->code}
                                              : std::nullopt;
                enter_terminal(
                    NetchanDriverState::protocol_error,
                    NetchanDriverErrorCode::packet_encode_failed,
                    "Netchan session could not prepare an outgoing packet",
                    now,
                    NetchanDriverTraceClassification::protocol_failure,
                    NetchanDriverEventType::protocol_error,
                    std::nullopt,
                    session_code);
                return;
            }

            const auto unreliable_size = prepared.plan->unreliable_payload_size();
            const auto* const fragment_plan =
                prepared.plan->fragment_plan()
                    ? &*prepared.plan->fragment_plan()
                    : nullptr;
            std::size_t fragment_transfer_size = 0U;
            if (fragment_plan != nullptr) {
                if (const auto& active_transfer =
                        session_.outgoing_fragment_transfer()) {
                    fragment_transfer_size =
                        active_transfer->canonical_bytes.size();
                } else {
                    fragment_transfer_size =
                        session_.pending_reliable_payload().size();
                }
            }
            const auto& packet = prepared.plan->packet();
            auto encoded = encode_packet(packet, now);
            if (!encoded) {
                static_cast<void>(session_.abandon_outgoing_packet(
                    std::move(*prepared.plan)));
                return;
            }
            if (!send_datagram(
                    *encoded,
                    packet.header,
                    packet.payload.size(),
                    now,
                    fragment_plan,
                    fragment_transfer_size)) {
                static_cast<void>(session_.abandon_outgoing_packet(
                    std::move(*prepared.plan)));
                return;
            }
            ++outgoing_packets_this_update;
            const auto committed = session_.commit_outgoing_send(std::move(*prepared.plan));
            if (!committed) {
                enter_terminal(
                    NetchanDriverState::protocol_error,
                    NetchanDriverErrorCode::packet_encode_failed,
                    "Sent outgoing packet could not be committed",
                    now,
                    NetchanDriverTraceClassification::protocol_failure,
                    NetchanDriverEventType::protocol_error,
                    std::nullopt,
                    committed.error ? std::optional{committed.error->code} : std::nullopt);
                return;
            }
            acknowledgement_pending_ = false;
            if (unreliable_size != 0U) {
                pending_unreliable_payload_.clear();
            }
        }
    }

    [[nodiscard]] std::optional<std::vector<std::byte>> encode_packet(
        const ClientToServerNetchanPacket& packet,
        const NetchanDriverTimePoint now)
    {
        NetchanPacketEncodeResult encoded;
        try {
            encoded = encode_client_to_server_netchan_packet(
                packet,
                NetchanPacketLimits{
                    packet.header.sequence.flags.fragmented
                        ? config_.maximum_fragment_datagram_size
                        : config_.maximum_datagram_size});
        } catch (...) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Netchan packet encoder threw for a bounded packet",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return std::nullopt;
        }
        if (!encoded || !encoded.datagram) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                encoded.error ? encoded.error->context : "Unable to encode netchan packet",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                encoded.error ? std::optional{encoded.error->code} : std::nullopt);
            return std::nullopt;
        }
        return std::move(*encoded.datagram);
    }

    [[nodiscard]] bool send_datagram(
        const std::vector<std::byte>& datagram,
        const NetchanHeader& header,
        const std::size_t payload_size,
        const NetchanDriverTimePoint now,
        const NetchanFragmentBuildPlan* const fragment_plan = nullptr,
        const std::size_t fragment_transfer_size = 0U)
    {
        network::DatagramSendResult sent;
        try {
            sent = transport_.send_to(remote_endpoint_, datagram);
        } catch (...) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::send_failed,
                "Datagram transport threw while sending netchan traffic",
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        if (!sent || !sent.error.empty()) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::send_failed,
                sent.error.empty() ? "Datagram transport failed to send netchan traffic"
                                   : std::move(sent.error),
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error);
            return false;
        }
        ++transmitted_packet_count_;
        emit_trace(
            NetchanDriverTraceClassification::packet_sent,
            remote_endpoint_,
            datagram.size(),
            &header,
            payload_size,
            nullptr,
            fragment_transfer_size,
            fragment_plan);
        return true;
    }

    [[nodiscard]] bool ensure_event_capacity(
        const std::size_t count,
        const NetchanDriverTimePoint now)
    {
        if (events_.can_push(count)) {
            return true;
        }
        enter_backpressure(now);
        return false;
    }

    void enter_backpressure(const NetchanDriverTimePoint now)
    {
        enter_terminal(
            NetchanDriverState::backpressure,
            NetchanDriverErrorCode::event_backpressure,
            "Netchan event consumer did not drain the bounded queue",
            now,
            NetchanDriverTraceClassification::protocol_failure,
            std::nullopt);
    }

    void enter_terminal(
        const NetchanDriverState state,
        const std::optional<NetchanDriverErrorCode> code,
        std::string context,
        const NetchanDriverTimePoint now,
        const NetchanDriverTraceClassification trace_classification,
        const std::optional<NetchanDriverEventType> event_type,
        const std::optional<NetchanPacketErrorCode> packet_code = std::nullopt,
        const std::optional<NetchanSessionErrorCode> session_code = std::nullopt,
        const std::optional<NetchanReassemblyErrorCode> reassembly_code =
            std::nullopt)
    {
        if (terminal_state(state_)) {
            return;
        }
        state_ = state;
        if (code) {
            last_error_ = NetchanDriverError{
                *code,
                packet_code,
                session_code,
                bounded_context(std::move(context)),
                reassembly_code,
            };
        }
        if (event_type && events_.can_push()) {
            events_.push(NetchanDriverEvent{
                *event_type,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                now,
            });
        }
        cleanup();
        emit_trace(trace_classification, remote_endpoint_);
    }

    void cleanup() noexcept
    {
        if (cleanup_done_) {
            return;
        }
        cleanup_done_ = true;
        ++cleanup_count_;
        session_.clear_reliable_state();
        reassembler_.clear();
        std::vector<std::byte>{}.swap(pending_unreliable_payload_);
        acknowledgement_pending_ = false;
        connection_lifetime_.reset();
    }

    void emit_trace(
        const NetchanDriverTraceClassification classification,
        const network::NetworkAddress endpoint,
        const std::size_t datagram_size = 0U,
        const NetchanHeader* const header = nullptr,
        const std::size_t payload_size = 0U,
        const NetchanFragmentReceipt* const fragment_receipt = nullptr,
        const std::size_t transfer_size = 0U,
        const NetchanFragmentBuildPlan* const fragment_plan = nullptr)
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        NetchanDriverTraceEvent event;
        event.classification = classification;
        event.state = state_;
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
        if (fragment_receipt != nullptr) {
            event.fragment_stream = NetchanFragmentStream::normal;
            event.local_transfer_id =
                fragment_receipt->transfer_id.value();
            event.fragment_offset =
                fragment_receipt->range.transfer_offset;
            event.fragment_length = fragment_receipt->range.length;
            event.covered_size = fragment_receipt->covered_size;
            event.transfer_size = transfer_size;
        } else if (fragment_plan != nullptr) {
            event.fragment_stream = NetchanFragmentStream::normal;
            event.local_transfer_id = fragment_plan->transfer_id.value();
            event.fragment_offset = fragment_plan->canonical_offset;
            event.fragment_length = fragment_plan->canonical_length;
            event.covered_size = fragment_plan->canonical_offset +
                fragment_plan->canonical_length;
            event.transfer_size = transfer_size;
        }
        trace_callback_active_ = true;
        try {
            trace_callback_(event);
        } catch (...) {
        }
        trace_callback_active_ = false;
    }

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    NetchanDriverConfig config_;
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime_;
    NetchanDriverTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    NetchanSession session_;
    NetchanNormalReassembler reassembler_;
    EventRing events_;
    bool configuration_valid_{false};
    NetchanDriverState state_{NetchanDriverState::idle};
    std::optional<NetchanDriverError> last_error_;
    std::optional<network::NetworkAddress> local_endpoint_;
    std::optional<NetchanDriverTimePoint> started_at_;
    std::optional<NetchanDriverTimePoint> last_update_;
    std::optional<NetchanDriverTimePoint> last_valid_packet_time_;
    std::vector<std::byte> pending_unreliable_payload_;
    bool acknowledgement_pending_{false};
    bool cleanup_done_{false};
    std::size_t transmitted_packet_count_{0U};
    std::size_t cleanup_count_{0U};
};

NetchanDriver::NetchanDriver(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    NetchanDriverConfig config,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime,
    NetchanDriverTraceCallback trace_callback)
    : implementation_{std::make_unique<Implementation>(
          transport,
          remote_endpoint,
          std::move(config),
          std::move(connection_lifetime),
          std::move(trace_callback))}
{
}

NetchanDriver::~NetchanDriver() = default;

bool NetchanDriver::start(
    const NetchanDriverTimePoint now,
    const network::NetworkAddress& expected_local_endpoint)
{
    return implementation_->start(now, expected_local_endpoint);
}

void NetchanDriver::update(const NetchanDriverTimePoint now)
{
    implementation_->update(now);
}

void NetchanDriver::cancel(const NetchanDriverTimePoint now)
{
    implementation_->cancel(now);
}

void NetchanDriver::close(const NetchanDriverTimePoint now)
{
    implementation_->close(now);
}

NetchanDriverOperationResult NetchanDriver::queue_reliable(
    const std::span<const std::byte> payload)
{
    return implementation_->queue_reliable(payload);
}

NetchanDriverOperationResult NetchanDriver::submit_unreliable(
    const std::span<const std::byte> payload)
{
    return implementation_->submit_unreliable(payload);
}

std::optional<NetchanDriverEvent> NetchanDriver::poll_event()
{
    return implementation_->poll_event();
}

bool NetchanDriver::valid_configuration() const noexcept
{
    return implementation_->valid_configuration();
}

NetchanDriverState NetchanDriver::state() const noexcept
{
    return implementation_->state();
}

bool NetchanDriver::terminal() const noexcept
{
    return implementation_->terminal();
}

const std::optional<NetchanDriverError>& NetchanDriver::last_error() const noexcept
{
    return implementation_->last_error();
}

const network::NetworkAddress& NetchanDriver::remote_endpoint() const noexcept
{
    return implementation_->remote_endpoint();
}

const std::optional<network::NetworkAddress>&
NetchanDriver::local_endpoint() const noexcept
{
    return implementation_->local_endpoint();
}

const std::optional<NetchanDriverTimePoint>&
NetchanDriver::last_valid_packet_time() const noexcept
{
    return implementation_->last_valid_packet_time();
}

std::size_t NetchanDriver::pending_event_count() const noexcept
{
    return implementation_->pending_event_count();
}

std::size_t NetchanDriver::transmitted_packet_count() const noexcept
{
    return implementation_->transmitted_packet_count();
}

std::size_t NetchanDriver::cleanup_count() const noexcept
{
    return implementation_->cleanup_count();
}

const NetchanSession& NetchanDriver::session() const noexcept
{
    return implementation_->session();
}

NetchanSession& NetchanDriver::compatibility_session() noexcept
{
    return implementation_->compatibility_session();
}

const NetchanNormalReassembler&
NetchanDriver::normal_reassembler() const noexcept
{
    return implementation_->normal_reassembler();
}

} // namespace hlclient::goldsrc
