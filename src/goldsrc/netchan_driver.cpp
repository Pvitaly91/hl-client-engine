#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/goldsrc/disconnect_request.hpp>
#include <hlclient/core/multiplayer_diagnostics.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {

class NetchanDriverContextIdentity final {
};

class NetchanOutgoingContextPlan::Implementation final {
public:
    Implementation(
        NetchanTransmitPlan session_probe,
        std::shared_ptr<const NetchanDriverContextIdentity> driver_identity,
        const std::uint64_t context_revision,
        const std::uint64_t plan_identity,
        NetchanOutgoingReliableComposition reliable_composition,
        const std::size_t maximum_unreliable_payload_size)
        : next_outgoing_sequence_{
              session_probe.packet().header.sequence.sequence},
          expected_header_{session_probe.packet().header},
          session_probe_{std::move(session_probe)},
          driver_identity_{std::move(driver_identity)},
          context_revision_{context_revision},
          plan_identity_{plan_identity},
          reliable_composition_{std::move(reliable_composition)},
          maximum_unreliable_payload_size_{maximum_unreliable_payload_size}
    {
    }

    NetchanSequence next_outgoing_sequence_;
    NetchanHeader expected_header_;
    NetchanTransmitPlan session_probe_;
    std::shared_ptr<const NetchanDriverContextIdentity> driver_identity_;
    std::uint64_t context_revision_{0U};
    std::uint64_t plan_identity_{0U};
    NetchanOutgoingReliableComposition reliable_composition_;
    std::size_t maximum_unreliable_payload_size_{0U};
    bool consumable_{true};
};

NetchanOutgoingContextPlan::NetchanOutgoingContextPlan(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

NetchanOutgoingContextPlan::~NetchanOutgoingContextPlan() = default;
NetchanOutgoingContextPlan::NetchanOutgoingContextPlan(
    NetchanOutgoingContextPlan&&) noexcept = default;
NetchanOutgoingContextPlan& NetchanOutgoingContextPlan::operator=(
    NetchanOutgoingContextPlan&&) noexcept = default;

NetchanSequence NetchanOutgoingContextPlan::next_outgoing_sequence() const noexcept
{
    return implementation_->next_outgoing_sequence_;
}

std::uint64_t NetchanOutgoingContextPlan::context_revision() const noexcept
{
    return implementation_->context_revision_;
}

std::uint64_t NetchanOutgoingContextPlan::plan_identity() const noexcept
{
    return implementation_->plan_identity_;
}

const NetchanOutgoingReliableComposition&
NetchanOutgoingContextPlan::reliable_composition() const noexcept
{
    return implementation_->reliable_composition_;
}

std::size_t
NetchanOutgoingContextPlan::maximum_unreliable_payload_size() const noexcept
{
    return implementation_->maximum_unreliable_payload_size_;
}

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

[[nodiscard]] std::optional<network::SocketNativeError> native_failure(
    const network::SocketNativeError error) noexcept
{
    return error ? std::optional{error} : std::nullopt;
}

[[nodiscard]] bool received_packet_failure(const NetchanDriverErrorCode code) noexcept
{
    switch (code) {
    case NetchanDriverErrorCode::receive_failed:
    case NetchanDriverErrorCode::inconsistent_receive_result:
    case NetchanDriverErrorCode::datagram_truncated:
    case NetchanDriverErrorCode::unexpected_connectionless_packet:
    case NetchanDriverErrorCode::unsupported_special_packet:
    case NetchanDriverErrorCode::malformed_packet:
    case NetchanDriverErrorCode::invalid_sequence:
    case NetchanDriverErrorCode::invalid_acknowledgement:
    case NetchanDriverErrorCode::opaque_payload_too_large:
    case NetchanDriverErrorCode::fragment_reassembly_failed:
    case NetchanDriverErrorCode::secondary_stream_pending_m3:
    case NetchanDriverErrorCode::event_backpressure:
        return true;
    default:
        return false;
    }
}

class ReceiveFailureScope final {
public:
    explicit ReceiveFailureScope(
        std::optional<NetchanFailurePacketMetadata>& metadata) noexcept
        : metadata_{metadata}
    {
        metadata_.reset();
    }
    ~ReceiveFailureScope() { metadata_.reset(); }

    ReceiveFailureScope(const ReceiveFailureScope&) = delete;
    ReceiveFailureScope& operator=(const ReceiveFailureScope&) = delete;

private:
    std::optional<NetchanFailurePacketMetadata>& metadata_;
};

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

[[nodiscard]] NetchanOutgoingContextPrepareResult context_prepare_failure(
    const NetchanDriverErrorCode code,
    std::string context,
    const std::optional<NetchanSessionErrorCode> session_code = std::nullopt)
{
    return NetchanOutgoingContextPrepareResult{
        std::nullopt,
        NetchanDriverError{
            code,
            std::nullopt,
            session_code,
            bounded_context(std::move(context)),
        },
    };
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

[[nodiscard]] bool fragment_plans_equal(
    const std::optional<NetchanFragmentBuildPlan>& left,
    const std::optional<NetchanFragmentBuildPlan>& right) noexcept
{
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left) {
        return true;
    }
    return left->transfer_id == right->transfer_id &&
           left->fragment_index == right->fragment_index &&
           left->fragment_count == right->fragment_count &&
           left->canonical_offset == right->canonical_offset &&
           left->canonical_length == right->canonical_length &&
           left->retransmission == right->retransmission;
}

[[nodiscard]] std::size_t unreliable_capacity_for_context(
    const NetchanDriverConfig& config,
    const NetchanTransmitPlan& plan) noexcept
{
    const auto& packet = plan.packet();
    const auto maximum_datagram_size = packet.header.sequence.flags.fragmented
                                           ? config.maximum_fragment_datagram_size
                                           : config.maximum_datagram_size;
    if (maximum_datagram_size < kNetchanHeaderSize) {
        return 0U;
    }
    auto maximum_body_size = maximum_datagram_size - kNetchanHeaderSize;
    if (packet.header.sequence.flags.fragmented) {
        constexpr auto descriptor_size =
            kStockProtocol48PresentFragmentDescriptorSize +
            kStockProtocol48FragmentPresenceSize;
        if (maximum_body_size < descriptor_size) {
            return 0U;
        }
        maximum_body_size -= descriptor_size;
    }
    if (plan.reliable_payload_size() > maximum_body_size) {
        return 0U;
    }
    return std::min(
        config.maximum_unreliable_payload_size,
        maximum_body_size - plan.reliable_payload_size());
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

    void push_front(NetchanDriverEvent event) noexcept
    {
        head_ = (head_ + slots_.size() - 1U) % slots_.size();
        slots_[head_].emplace(std::move(event));
        ++size_;
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
    const auto maximum_body_size =
        config.maximum_datagram_size - kNetchanHeaderSize;
    return NetchanSessionLimits{
        config.maximum_datagram_size,
        config.maximum_unfragmented_reliable_payload.value_or(
            maximum_body_size),
        config.maximum_pending_reliable_payload.value_or(
            kMaximumPendingReliablePayload),
    };
}

[[nodiscard]] bool valid_session_limits_for(
    const NetchanDriverConfig& config) noexcept
{
    const auto limits = session_limits_for(config);
    if (limits.maximum_datagram_size <
            kNetchanHeaderSize + kStockProtocol48MinimumDecodedPayloadSize ||
        limits.maximum_datagram_size > kMaximumNetchanDatagramSize) {
        return false;
    }
    const auto maximum_body_size =
        limits.maximum_datagram_size - kNetchanHeaderSize;
    return limits.maximum_unfragmented_reliable_payload > 0U &&
           limits.maximum_unfragmented_reliable_payload <= maximum_body_size &&
           limits.maximum_pending_reliable_payload >=
               limits.maximum_unfragmented_reliable_payload &&
           limits.maximum_pending_reliable_payload <=
               kMaximumPendingReliablePayload;
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
    if (!valid_session_limits_for(config) ||
        config.channel_inactivity_timeout <= std::chrono::milliseconds::zero() ||
        config.channel_inactivity_timeout > kMaximumNetchanChannelInactivityTimeout ||
        (config.idle_poll_interval != std::chrono::milliseconds::zero() &&
         (config.idle_poll_interval < std::chrono::milliseconds{100} ||
          config.idle_poll_interval > std::chrono::milliseconds{1'000} ||
          config.idle_poll_interval >= config.channel_inactivity_timeout)) ||
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
        ++receive_statistics_.updates;
        expire_split_packets(now);
        if (!validate_local_continuity(now)) {
            return;
        }
        if (graceful_close_.requested) {
            update_graceful_close(now);
            return;
        }
        if (events_.size() == config_.maximum_events) {
            enter_backpressure(now);
            return;
        }

        std::size_t outgoing_packets_this_update = 0U;
        bool contextual_packet_sent = false;
        if (pending_contextual_plan_) {
            // A committed context owns the exact current sequence/reliable
            // composition. Send it before RX can admit an ACK and invalidate
            // that accepted checksum context. Legacy unreliable work keeps the
            // historical receive-first scheduling below.
            if (!check_fragment_timeout(now)) {
                return;
            }
            if (last_valid_packet_time_ &&
                now - *last_valid_packet_time_ >=
                    config_.channel_inactivity_timeout) {
                enter_terminal(
                    NetchanDriverState::timed_out,
                    NetchanDriverErrorCode::channel_inactivity_timed_out,
                    "Netchan channel inactivity timeout elapsed",
                    now,
                    NetchanDriverTraceClassification::channel_timed_out,
                    NetchanDriverEventType::channel_timed_out);
                return;
            }
            if (!send_contextual_pending(now, outgoing_packets_this_update)) {
                return;
            }
            contextual_packet_sent = true;
        }
        for (std::size_t received_count = 0U;
             received_count < config_.maximum_datagrams_per_update &&
             state_ == NetchanDriverState::active &&
             (outgoing_packets_this_update <
                  config_.maximum_outgoing_packets_per_update ||
              (contextual_packet_sent &&
               session_.first_acknowledgement_sent()));
             ++received_count) {
            network::DatagramTransportReceiveResult received;
            try {
                ++receive_statistics_.receive_polls;
                if (movement_diagnostics_enabled_ || multiplayer_enabled())
                    socket_read_begin_ns_ = actual_socket_read_ns();
                received = transport_.receive(config_.maximum_datagram_size);
                if (movement_diagnostics_enabled_ || multiplayer_enabled())
                    socket_read_end_ns_ = actual_socket_read_ns();
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
        if (graceful_close_.outcome == GracefulCloseOutcome::pending) {
            graceful_close_.outcome = GracefulCloseOutcome::cancelled;
            graceful_close_.netchan_closed_at = now;
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
        if (graceful_close_.requested && !graceful_close_.netchan_closed_at)
            graceful_close_.netchan_closed_at = now;
        cleanup();
        emit_trace(NetchanDriverTraceClassification::driver_closed, remote_endpoint_);
        static_cast<void>(now);
    }

    [[nodiscard]] bool request_graceful_close(const NetchanDriverTimePoint now)
    {
        if (trace_callback_active_) return false;
        if (graceful_close_.requested) return true;
        graceful_close_.requested = true;
        graceful_close_.operation_identity = 1U;
        graceful_close_.session_generation = 1U;
        graceful_close_.requested_at = now;
        if (graceful_close_.server_disconnect_observed && !last_error_) {
            // Goodbye after an already observed server close is ineligible;
            // retain the exact notice receipt rather than reopen the channel.
            graceful_close_.eligible = false;
            graceful_close_.outcome = GracefulCloseOutcome::server_disconnect_confirmed;
            return true;
        }
        if (state_ != NetchanDriverState::active) {
            graceful_close_.outcome = last_error_ ? GracefulCloseOutcome::fatal_failure
                : GracefulCloseOutcome::cancel_without_protocol_disconnect;
            graceful_close_.transport_error = last_error_
                ? std::optional{last_error_->code} : std::nullopt;
            graceful_close_.netchan_closed_at = now;
            close(now);
            return true;
        }
        graceful_close_.eligible = true;
        graceful_close_.queued = true;
        graceful_close_.outcome = GracefulCloseOutcome::pending;
        if (now > NetchanDriverTimePoint::max() - kGracefulCloseBudget) {
            enter_terminal(NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_configuration,
                "Graceful close deadline exceeds the monotonic clock domain", now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return true;
        }
        graceful_close_.deadline_at = now + kGracefulCloseBudget;
        if (last_update_ && now < *last_update_) {
            enter_terminal(NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::time_moved_backwards,
                "Graceful close intent time moved backwards", now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return true;
        }
        // A previously accepted but unsent movement capability is read-only
        // session preparation; abandonment does not rewrite reliable state.
        if (pending_contextual_plan_)
            static_cast<void>(session_.abandon_outgoing_packet(std::move(*pending_contextual_plan_)));
        pending_contextual_plan_.reset();
        pending_contextual_plan_identity_.reset();
        pending_unreliable_payload_.clear();
        return true;
    }

    void confirm_server_disconnect(const NetchanDriverTimePoint now)
    {
        if (trace_callback_active_) return;
        graceful_close_.server_disconnect_observed = true;
        if (!graceful_close_.session_generation) graceful_close_.session_generation = 1U;
        if (!last_error_ && !graceful_close_.transport_error) {
            graceful_close_.outcome = GracefulCloseOutcome::server_disconnect_confirmed;
            graceful_close_.netchan_closed_at = now;
            close(now);
        }
    }

    [[nodiscard]] GracefulCloseSummary graceful_close_snapshot() const noexcept
    {
        return graceful_close_;
    }

    [[nodiscard]] NetchanDriverOperationResult queue_reliable(
        const std::span<const std::byte> payload)
    {
        if (trace_callback_active_) {
            return operation_failure(
                NetchanDriverErrorCode::reentrant_operation,
                "Netchan trace callbacks cannot mutate driver state");
        }
        if (state_ != NetchanDriverState::active || graceful_close_.requested) {
            return operation_failure(
                NetchanDriverErrorCode::not_active,
                "Reliable payload requires an active netchan driver");
        }
        if (!payload.empty() && pending_contextual_plan_) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_payload_pending,
                "A committed sequence-bound unreliable payload must send before reliable state changes");
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
        if (state_ != NetchanDriverState::active || graceful_close_.requested) {
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
        if (!pending_unreliable_payload_.empty() || pending_contextual_plan_) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_payload_pending,
                "Only one owning unreliable payload may be pending");
        }
        if (outgoing_context_revision_ ==
            std::numeric_limits<std::uint64_t>::max()) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_context_revision_overflow,
                "Unreliable context revision is exhausted");
        }
        pending_unreliable_payload_.assign(payload.begin(), payload.end());
        ++outgoing_context_revision_;
        return {};
    }

    [[nodiscard]] NetchanOutgoingContextPrepareResult
    prepare_unreliable_context()
    {
        if (trace_callback_active_) {
            return context_prepare_failure(
                NetchanDriverErrorCode::reentrant_operation,
                "Netchan trace callbacks cannot prepare outgoing context");
        }
        if (state_ != NetchanDriverState::active || graceful_close_.requested) {
            return context_prepare_failure(
                NetchanDriverErrorCode::not_active,
                "Outgoing context requires an active netchan driver");
        }
        if (!session_.first_acknowledgement_sent()) {
            return context_prepare_failure(
                NetchanDriverErrorCode::unreliable_context_unavailable,
                "Sequence-bound unreliable context is unavailable before the first channel acknowledgement");
        }
        if (!pending_unreliable_payload_.empty() || pending_contextual_plan_) {
            return context_prepare_failure(
                NetchanDriverErrorCode::unreliable_payload_pending,
                "Only one owning unreliable payload may be pending");
        }
        if (next_outgoing_context_plan_identity_ ==
            std::numeric_limits<std::uint64_t>::max()) {
            return context_prepare_failure(
                NetchanDriverErrorCode::unreliable_context_identity_overflow,
                "Outgoing context plan identity is exhausted");
        }

        NetchanTransmitPrepareResult prepared;
        try {
            prepared = session_.prepare_outgoing_packet();
        } catch (...) {
            return context_prepare_failure(
                NetchanDriverErrorCode::packet_encode_failed,
                "Netchan session threw while preparing outgoing context");
        }
        if (!prepared || !prepared.plan) {
            return context_prepare_failure(
                NetchanDriverErrorCode::unreliable_context_unavailable,
                "Netchan session could not prepare outgoing context",
                prepared.error ? std::optional{prepared.error->code}
                               : std::nullopt);
        }
        if (prepared.plan->reliable_decision() ==
            ReliableTransmitDecision::advance_fragment_sequence) {
            static_cast<void>(session_.abandon_outgoing_packet(
                std::move(*prepared.plan)));
            return context_prepare_failure(
                NetchanDriverErrorCode::unreliable_context_unavailable,
                "The next sequence is reserved for a fragment classifier advance");
        }

        const auto capacity = unreliable_capacity_for_context(config_, *prepared.plan);
        if (capacity == 0U) {
            static_cast<void>(session_.abandon_outgoing_packet(
                std::move(*prepared.plan)));
            return context_prepare_failure(
                NetchanDriverErrorCode::unreliable_context_unavailable,
                "Current reliable composition leaves no unreliable payload capacity");
        }

        const auto& packet = prepared.plan->packet();
        NetchanOutgoingReliableComposition composition{
            prepared.plan->reliable_decision(),
            prepared.plan->reliable_payload_size(),
            packet.header.sequence.flags.reliable,
            packet.header.sequence.flags.fragmented,
            prepared.plan->fragment_plan(),
        };
        const auto plan_identity = next_outgoing_context_plan_identity_;
        auto implementation =
            std::make_unique<NetchanOutgoingContextPlan::Implementation>(
                std::move(*prepared.plan),
                outgoing_context_identity_,
                outgoing_context_revision_,
                plan_identity,
                std::move(composition),
                capacity);
        ++next_outgoing_context_plan_identity_;
        return NetchanOutgoingContextPrepareResult{
            NetchanOutgoingContextPlan{std::move(implementation)},
            std::nullopt,
        };
    }

    [[nodiscard]] NetchanDriverOperationResult commit_unreliable(
        NetchanOutgoingContextPlan&& plan,
        const std::span<const std::byte> payload)
    {
        if (trace_callback_active_) {
            return operation_failure(
                NetchanDriverErrorCode::reentrant_operation,
                "Netchan trace callbacks cannot commit outgoing context");
        }
        if (!plan.implementation_) {
            return operation_failure(
                NetchanDriverErrorCode::stale_unreliable_context,
                "Outgoing context was already moved or consumed");
        }
        auto& context = *plan.implementation_;
        if (context.driver_identity_ != outgoing_context_identity_) {
            return operation_failure(
                NetchanDriverErrorCode::foreign_unreliable_context,
                "Outgoing context belongs to another netchan driver");
        }
        if (!context.consumable_) {
            return operation_failure(
                NetchanDriverErrorCode::stale_unreliable_context,
                "Outgoing context was already consumed");
        }
        if (state_ != NetchanDriverState::active || graceful_close_.requested) {
            return operation_failure(
                NetchanDriverErrorCode::not_active,
                "Outgoing context requires an active netchan driver");
        }
        if (context.context_revision_ != outgoing_context_revision_) {
            context.consumable_ = false;
            return operation_failure(
                NetchanDriverErrorCode::stale_unreliable_context,
                "Driver unreliable state changed after context preparation");
        }
        if (payload.empty()) {
            return abandon_unreliable(std::move(plan));
        }
        if (payload.size() > context.maximum_unreliable_payload_size_ ||
            payload.size() > config_.maximum_unreliable_payload_size) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_payload_too_large,
                "Unreliable payload exceeds the prepared context capacity");
        }
        if (!pending_unreliable_payload_.empty() || pending_contextual_plan_) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_payload_pending,
                "Only one owning unreliable payload may be pending");
        }
        if (outgoing_context_revision_ ==
            std::numeric_limits<std::uint64_t>::max()) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_context_revision_overflow,
                "Unreliable context revision is exhausted");
        }

        const auto abandoned = session_.abandon_outgoing_packet(
            std::move(context.session_probe_));
        context.consumable_ = false;
        if (!abandoned) {
            const auto session_code = abandoned.error
                                          ? std::optional{abandoned.error->code}
                                          : std::nullopt;
            const auto driver_code =
                session_code == NetchanSessionErrorCode::foreign_outgoing_transaction
                    ? NetchanDriverErrorCode::foreign_unreliable_context
                    : NetchanDriverErrorCode::stale_unreliable_context;
            return operation_failure(
                driver_code,
                "Netchan session context changed after outgoing preparation",
                std::nullopt,
                session_code);
        }

        NetchanTransmitPrepareResult prepared;
        try {
            prepared = session_.prepare_outgoing_packet(payload);
        } catch (...) {
            return operation_failure(
                NetchanDriverErrorCode::packet_encode_failed,
                "Netchan session threw while binding unreliable payload");
        }
        if (!prepared || !prepared.plan) {
            return operation_failure(
                NetchanDriverErrorCode::unreliable_context_mismatch,
                "Netchan session rejected payload for a validated outgoing context",
                std::nullopt,
                prepared.error ? std::optional{prepared.error->code}
                               : std::nullopt);
        }

        const auto& actual_packet = prepared.plan->packet();
        const auto& expected = context.reliable_composition_;
        const bool metadata_matches =
            actual_packet.header.sequence.sequence ==
                context.next_outgoing_sequence_ &&
            headers_equal(actual_packet.header, context.expected_header_) &&
            prepared.plan->reliable_decision() == expected.decision &&
            prepared.plan->reliable_payload_size() ==
                expected.reliable_payload_size &&
            actual_packet.header.sequence.flags.reliable ==
                expected.reliable_sequence &&
            actual_packet.header.sequence.flags.fragmented ==
                expected.fragmented_sequence &&
            prepared.plan->unreliable_payload_size() == payload.size() &&
            fragment_plans_equal(
                prepared.plan->fragment_plan(), expected.fragment);
        if (!metadata_matches) {
            static_cast<void>(session_.abandon_outgoing_packet(
                std::move(*prepared.plan)));
            return operation_failure(
                NetchanDriverErrorCode::unreliable_context_mismatch,
                "Prepared outgoing packet does not match its validated context");
        }

        pending_contextual_plan_.emplace(std::move(*prepared.plan));
        pending_contextual_plan_identity_ = context.plan_identity_;
        ++outgoing_context_revision_;
        return {};
    }

    [[nodiscard]] NetchanDriverOperationResult abandon_unreliable(
        NetchanOutgoingContextPlan&& plan)
    {
        if (trace_callback_active_) {
            return operation_failure(
                NetchanDriverErrorCode::reentrant_operation,
                "Netchan trace callbacks cannot abandon outgoing context");
        }
        if (!plan.implementation_) {
            return operation_failure(
                NetchanDriverErrorCode::stale_unreliable_context,
                "Outgoing context was already moved or consumed");
        }
        auto& context = *plan.implementation_;
        if (context.driver_identity_ != outgoing_context_identity_) {
            return operation_failure(
                NetchanDriverErrorCode::foreign_unreliable_context,
                "Outgoing context belongs to another netchan driver");
        }
        if (!context.consumable_) {
            return operation_failure(
                NetchanDriverErrorCode::stale_unreliable_context,
                "Outgoing context was already consumed");
        }
        if (state_ != NetchanDriverState::active) {
            return operation_failure(
                NetchanDriverErrorCode::not_active,
                "Outgoing context requires an active netchan driver");
        }
        if (context.context_revision_ != outgoing_context_revision_) {
            context.consumable_ = false;
            return operation_failure(
                NetchanDriverErrorCode::stale_unreliable_context,
                "Driver unreliable state changed after context preparation");
        }

        const auto abandoned = session_.abandon_outgoing_packet(
            std::move(context.session_probe_));
        context.consumable_ = false;
        if (!abandoned) {
            const auto session_code = abandoned.error
                                          ? std::optional{abandoned.error->code}
                                          : std::nullopt;
            const auto driver_code =
                session_code == NetchanSessionErrorCode::foreign_outgoing_transaction
                    ? NetchanDriverErrorCode::foreign_unreliable_context
                    : NetchanDriverErrorCode::stale_unreliable_context;
            return operation_failure(
                driver_code,
                "Netchan session context changed before abandonment",
                std::nullopt,
                session_code);
        }
        return {};
    }

    [[nodiscard]] std::optional<NetchanDriverEvent> poll_event() noexcept
    {
        return events_.pop();
    }

    [[nodiscard]] bool prepend_retained_payloads(
        const std::span<OwnedNetchanPayload> payloads) noexcept
    {
        if (trace_callback_active_ || state_ != NetchanDriverState::active ||
            !events_.can_push(payloads.size())) {
            return false;
        }
        // These payloads were produced and sequence-validated by this driver.
        // Only trusted owning stage facades can restore them; preflight the
        // complete batch before moving anything or changing event order.
        for (const auto& payload : payloads) {
            if (payload.bytes.empty() ||
                payload.bytes.size() > config_.maximum_opaque_payload_size ||
                payload.direction != NetchanDirection::server_to_client) {
                return false;
            }
        }
        for (auto it = payloads.rbegin(); it != payloads.rend(); ++it) {
            const auto bytes = it->bytes.size();
            const auto occurred_at = it->received_at;
            events_.push_front(NetchanDriverEvent{
                NetchanDriverEventType::payload_ready, std::move(*it),
                bytes, 0U, 0U, bytes, occurred_at});
        }
        return true;
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
    [[nodiscard]] NetchanDriverReceiveStatistics receive_statistics() const noexcept
    {
        return receive_statistics_;
    }
    void set_movement_diagnostics_enabled(const bool enabled) noexcept
    {
        movement_diagnostics_enabled_ = enabled;
        if (!enabled && !multiplayer_enabled()) {
            socket_read_begin_ns_.reset();
            socket_read_end_ns_.reset();
        }
    }
    void set_multiplayer_diagnostics(core::MultiplayerDiagnosticSink* sink,
        const std::uint64_t network_generation, const std::uint64_t map_generation) noexcept
    {
        multiplayer_sink_=sink;
        multiplayer_network_generation_=network_generation;
        multiplayer_map_generation_=map_generation;
    }
    [[nodiscard]] bool multiplayer_enabled() const noexcept {
        return multiplayer_sink_ && multiplayer_sink_->enabled();
    }
    void observe_multiplayer_network(const core::MultiplayerNetworkPhase phase,
        const NetchanHeader* header, const std::size_t bytes,
        const std::optional<std::uint64_t> begin={},
        const std::optional<std::uint64_t> end={}) noexcept {
        if(!multiplayer_enabled()) return;
        core::MultiplayerDiagnosticEvent event;
        event.identity.producer=core::MultiplayerDiagnosticProducer::transport;
        event.identity.producer_event_id=++multiplayer_event_id_;
        event.identity.network_generation=multiplayer_network_generation_;
        event.identity.map_generation=multiplayer_map_generation_;
        event.local_steady_ns=core::multiplayer_diagnostic_now_ns();
        event.provenance=core::MultiplayerProvenance::runtime_observed;
        event.edge=core::MultiplayerEdgeConfidence::exact;
        core::MultiplayerNetworkPayload value;
        value.phase=phase;value.bytes=static_cast<std::uint32_t>(bytes);
        value.begin_ns=begin;value.end_ns=end;
        if(header) {
            if(phase==core::MultiplayerNetworkPhase::packet_sent)
                event.identity.carrier_packet=header->sequence.sequence.value();
            else event.identity.source_sequence=header->sequence.sequence.value();
            value.acknowledgement=header->acknowledgement.sequence.value();
            value.reliable=header->sequence.flags.reliable;
        }
        if(phase==core::MultiplayerNetworkPhase::transport_ack)
            event.knowledge=core::MultiplayerKnowledge::transport_acknowledged;
        event.payload=value;
        static_cast<void>(multiplayer_sink_->observe(event));
    }
    [[nodiscard]] std::optional<std::uint64_t>
    last_sent_unreliable_context_identity() const noexcept
    {
        return last_sent_unreliable_context_identity_;
    }
    [[nodiscard]] std::size_t cleanup_count() const noexcept { return cleanup_count_; }
    [[nodiscard]] const NetchanSession& session() const noexcept { return session_; }
    [[nodiscard]] NetchanSession& compatibility_session() noexcept { return session_; }
    [[nodiscard]] const NetchanNormalReassembler& normal_reassembler() const noexcept
    {
        return reassembler_;
    }

private:
    [[nodiscard]] static std::optional<std::uint64_t> actual_socket_read_ns() noexcept
    {
        const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (value < 0) return std::nullopt;
        return static_cast<std::uint64_t>(value);
    }
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
        const ReceiveFailureScope failure_scope{active_receive_metadata_};
        using Status = network::DatagramTransportReceiveStatus;
        if (received.status == Status::would_block) {
            ++receive_statistics_.would_block;
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
                NetchanDriverEventType::network_error,
                std::nullopt, std::nullopt, std::nullopt,
                native_failure(received.native_error));
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
            ++receive_statistics_.wrong_endpoint;
            emit_trace(
                NetchanDriverTraceClassification::wrong_endpoint_ignored,
                *received.source,
                received.payload_size);
            return true;
        }

        if (received.status == Status::truncated) {
            active_receive_metadata_.emplace();
            active_receive_metadata_->datagram_size = received.payload_size;
            active_receive_metadata_->datagram_size_is_lower_bound = true;
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::datagram_truncated,
                received.error.empty() ? "Exact-endpoint netchan datagram was truncated"
                                       : std::move(received.error),
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt, std::nullopt, std::nullopt,
                native_failure(received.native_error));
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
        ++receive_statistics_.owning_datagrams;
        return process_target_datagram(
            std::move(received.datagram->payload),
            now,
            outgoing_packets_this_update);
    }

    struct SplitTransfer {
        bool active{};
        std::uint32_t identity{};
        std::uint8_t count{};
        NetchanDriverTimePoint started{};
        std::size_t retained_bytes{};
        std::array<std::vector<std::byte>, 15U> parts;
    };

    void expire_split_packets(const NetchanDriverTimePoint now) {
        for (auto& transfer : split_transfers_) {
            if (transfer.active && now - transfer.started >= std::chrono::seconds{1}) {
                transfer = {};
                ++receive_statistics_.split_expired;
            }
        }
    }

    // marker:u32(-2), identity:u32, index/count:u8 (high/low nibble).
    // Keep the inner netchan datagram munged and opaque until EVERY part is
    // present. Partial/duplicate/expired UDP envelopes never alter ACKs,
    // reliable generations, command receipts, or application publications.
    [[nodiscard]] std::optional<std::vector<std::byte>> accept_split_part(
        const std::span<const std::byte> datagram, const NetchanDriverTimePoint now) {
        constexpr std::size_t header_size = 9U;
        ++receive_statistics_.split_parts;
        const auto reject = [&]() {
            ++receive_statistics_.split_rejected;
            return std::optional<std::vector<std::byte>>{};
        };
        if (datagram.size() <= header_size || datagram.size() > config_.maximum_datagram_size)
            return reject();
        std::uint32_t identity{};
        for (unsigned i = 0; i < 4U; ++i)
            identity |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[4U + i])) << (8U * i);
        const auto packed = std::to_integer<std::uint8_t>(datagram[8U]);
        const auto count = static_cast<std::uint8_t>(packed & 15U);
        const auto index = static_cast<std::uint8_t>(packed >> 4U);
        if (count < 2U || index >= count) return reject();
        SplitTransfer* transfer = nullptr;
        for (auto& candidate : split_transfers_)
            if (candidate.active && candidate.identity == identity) transfer = &candidate;
        if (!transfer) {
            for (auto& candidate : split_transfers_)
                if (!candidate.active) { transfer = &candidate; break; }
            if (!transfer) {
                transfer = &*std::min_element(split_transfers_.begin(), split_transfers_.end(),
                    [](const auto& a, const auto& b) { return a.started < b.started; });
                ++receive_statistics_.split_evicted;
            }
            *transfer = {};
            transfer->active = true; transfer->identity = identity;
            transfer->count = count; transfer->started = now;
        }
        if (transfer->count != count) { *transfer = {}; return reject(); }
        const auto payload = datagram.subspan(header_size);
        auto& destination = transfer->parts[index];
        if (!destination.empty()) {
            if (destination.size() != payload.size() ||
                !std::equal(destination.begin(), destination.end(), payload.begin())) {
                *transfer = {}; return reject();
            }
            ++receive_statistics_.split_duplicates;
            return std::nullopt;
        }
        if (payload.size() > kMaximumNetchanDatagramSize - transfer->retained_bytes) {
            *transfer = {}; return reject();
        }
        destination.assign(payload.begin(), payload.end());
        transfer->retained_bytes += payload.size();
        for (std::size_t i = 0; i < count; ++i)
            if (transfer->parts[i].empty()) return std::nullopt;
        const auto stride = transfer->parts[0].size();
        for (std::size_t i = 1; i + 1U < count; ++i)
            if (transfer->parts[i].size() != stride) { *transfer = {}; return reject(); }
        if (transfer->parts[count - 1U].size() > stride) { *transfer = {}; return reject(); }
        std::vector<std::byte> joined;
        joined.reserve(transfer->retained_bytes);
        for (std::size_t i = 0; i < count; ++i)
            joined.insert(joined.end(), transfer->parts[i].begin(), transfer->parts[i].end());
        *transfer = {};
        const auto decoded = decode_server_to_client_netchan_packet(joined,
            NetchanPacketLimits{kMaximumNetchanDatagramSize});
        if (!decoded || !decoded.packet) return reject();
        ++receive_statistics_.split_completed;
        return joined;
    }

    [[nodiscard]] bool process_target_datagram(
        std::vector<std::byte> datagram,
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update,
        const bool reassembled_split = false)
    {
        const auto datagram_size = datagram.size();
        const auto classification = classify_netchan_datagram(datagram);
        active_receive_metadata_.emplace();
        active_receive_metadata_->datagram_size = datagram_size;
        active_receive_metadata_->classification = classification.classification;
        if (classification.classification == NetchanDatagramClassification::connectionless) {
            // Classification confirms the four-byte OOB marker. Keep only the
            // next opcode byte; its remaining server/auth text stays opaque.
            constexpr std::size_t marker_size = sizeof(std::uint32_t);
            if (datagram_size > marker_size)
                active_receive_metadata_->connectionless_opcode =
                    std::to_integer<std::uint8_t>(datagram[marker_size]);
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
            if (config_.accept_udp_split_packets && !reassembled_split) {
                std::optional<std::vector<std::byte>> joined;
                try {
                    joined = accept_split_part(datagram, now);
                } catch (...) {
                    // A UDP split transfer is expendable; preserve the existing
                    // bounded channel and allow the next ordinary snapshot.
                    split_transfers_ = {};
                    ++receive_statistics_.split_rejected;
                    return true;
                }
                if (joined) return process_target_datagram(std::move(*joined), now,
                    outgoing_packets_this_update, true);
                return true;
            }
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
        active_receive_metadata_->sequence = header.sequence.sequence.value();
        active_receive_metadata_->acknowledgement = header.acknowledgement.sequence.value();
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
                        : reassembled_split ? kMaximumNetchanDatagramSize
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
        const auto acknowledgement = inspection.acknowledgement();
        std::optional<std::uint64_t> completed_reliable_generation;
        if (completes_reliable) {
            const auto& transfer = session_.outgoing_fragment_transfer();
            if (transfer && transfer->transfer_id.valid()) {
                completed_reliable_generation = transfer->transfer_id.value();
            }
        }
        std::optional<NetchanCompletedFragmentTransfer> completion_witness;
        if (!retain_completion_witness(completes_reliable, completion_witness, now)) {
            return false;
        }
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
            socket_read_begin_ns_,
            socket_read_end_ns_,
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

        if(acknowledgement && acknowledgement->disposition==NetchanAcknowledgementDisposition::advanced)
            observe_multiplayer_network(core::MultiplayerNetworkPhase::transport_ack,
                &packet.header,0U);
        if (graceful_close_.requested) {
            // Terminal scheduling owns the next ordinary packet/ACK. Never
            // introduce an extra send (or new gameplay payload) from RX.
            acknowledgement_pending_ = true;
        } else if (!session_.first_acknowledgement_sent()) {
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
        ++receive_statistics_.accepted_sequences;
        if (!receive_statistics_.first_accepted_sequence)
            receive_statistics_.first_accepted_sequence = packet.header.sequence.sequence.value();
        receive_statistics_.last_accepted_sequence = packet.header.sequence.sequence.value();
        if (completes_reliable) {
            events_.push(NetchanDriverEvent{
                NetchanDriverEventType::reliable_payload_acknowledged,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                now,
                acknowledgement,
                completed_reliable_generation,
                std::move(completion_witness),
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
        ++receive_statistics_.payloads_created;
        if (!receive_statistics_.first_payload_sequence)
            receive_statistics_.first_payload_sequence = packet.header.sequence.sequence.value();
        receive_statistics_.last_payload_sequence = packet.header.sequence.sequence.value();
        emit_trace(
            NetchanDriverTraceClassification::payload_ready,
            remote_endpoint_,
            datagram_size,
            &packet.header,
            payload_size);
        // An unfragmented datagram contributes at most one owning payload.
        // A completed fragment unit may separately contribute one completed
        // transfer and one contemporaneous suffix.
        return !config_.yield_after_owning_payload;
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
        const auto acknowledgement = inspection.acknowledgement();
        std::optional<std::uint64_t> completed_reliable_generation;
        if (completes_reliable) {
            const auto& transfer = session_.outgoing_fragment_transfer();
            if (transfer && transfer->transfer_id.valid()) {
                completed_reliable_generation = transfer->transfer_id.value();
            }
        }
        std::optional<NetchanCompletedFragmentTransfer> completion_witness;
        if (!retain_completion_witness(completes_reliable, completion_witness, now)) {
            return false;
        }
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
                    socket_read_begin_ns_,
                    socket_read_end_ns_,
                    NetchanPayloadBodyKind::contemporaneous_fragment_suffix,
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

        if(acknowledgement && acknowledgement->disposition==NetchanAcknowledgementDisposition::advanced)
            observe_multiplayer_network(core::MultiplayerNetworkPhase::transport_ack,
                &packet.header,0U);
        if (graceful_close_.requested) {
            acknowledgement_pending_ = true;
        } else if (!session_.first_acknowledgement_sent()) {
            if (!send_first_acknowledgement(now, outgoing_packets_this_update)) {
                return false;
            }
        } else if (!send_transport_acknowledgement(
                       now,
                       outgoing_packets_this_update)) {
            return false;
        }

        last_valid_packet_time_ = now;
        ++receive_statistics_.accepted_sequences;
        if (!receive_statistics_.first_accepted_sequence)
            receive_statistics_.first_accepted_sequence = packet.header.sequence.sequence.value();
        receive_statistics_.last_accepted_sequence = packet.header.sequence.sequence.value();
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
                acknowledgement,
                completed_reliable_generation,
                std::move(completion_witness),
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
                socket_read_begin_ns_,
                socket_read_end_ns_,
                NetchanPayloadBodyKind::normal_fragment_completion,
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
            ++receive_statistics_.payloads_created;
            if (!receive_statistics_.first_payload_sequence)
                receive_statistics_.first_payload_sequence = packet.header.sequence.sequence.value();
            receive_statistics_.last_payload_sequence = packet.header.sequence.sequence.value();
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
        return !(config_.yield_after_owning_payload && emits_owning_payload);
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
            // The contextual packet already consumed this update's TX slot.
            // Its next sequence (or a later bounded send) can acknowledge the
            // committed incoming reliable generation without dropping RX.
            acknowledgement_pending_ = true;
            return true;
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

    [[nodiscard]] bool send_contextual_pending(
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        if (!pending_contextual_plan_) {
            return true;
        }
        if (outgoing_packets_this_update >=
            config_.maximum_outgoing_packets_per_update) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_configuration,
                "Committed outgoing context exceeds the bounded TX update budget",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }

        auto& plan = *pending_contextual_plan_;
        if (!pending_contextual_plan_identity_) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::unreliable_context_mismatch,
                "Committed outgoing context has no retained plan identity",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        const auto sent_plan_identity = *pending_contextual_plan_identity_;
        const auto* const fragment_plan =
            plan.fragment_plan() ? &*plan.fragment_plan() : nullptr;
        std::size_t fragment_transfer_size = 0U;
        if (fragment_plan != nullptr) {
            if (const auto& active_transfer =
                    session_.outgoing_fragment_transfer()) {
                fragment_transfer_size = active_transfer->canonical_bytes.size();
            } else {
                fragment_transfer_size =
                    session_.pending_reliable_payload().size();
            }
        }

        const auto& packet = plan.packet();
        auto encoded = encode_packet(packet, now);
        if (!encoded) {
            return false;
        }
        if (!send_datagram(
                *encoded,
                packet.header,
                packet.payload.size(),
                now,
                fragment_plan,
                fragment_transfer_size, true)) {
            return false;
        }
        last_sent_unreliable_context_identity_ = sent_plan_identity;
        ++outgoing_packets_this_update;
        const auto committed = session_.commit_outgoing_send(std::move(plan));
        pending_contextual_plan_.reset();
        pending_contextual_plan_identity_.reset();
        if (!committed) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::packet_encode_failed,
                "Sent sequence-bound outgoing packet could not be committed",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error,
                std::nullopt,
                committed.error ? std::optional{committed.error->code}
                                : std::nullopt);
            return false;
        }
        acknowledgement_pending_ = false;
        return true;
    }

    void update_graceful_close(const NetchanDriverTimePoint now)
    {
        if (!graceful_close_.deadline_at || now >= *graceful_close_.deadline_at) {
            graceful_close_.deadline_reached = true;
            graceful_close_.outcome = graceful_close_.successful_send_count
                ? GracefulCloseOutcome::sent_but_unconfirmed
                : GracefulCloseOutcome::send_failed;
            close(now);
            return;
        }
        if (events_.size() == config_.maximum_events) {
            enter_backpressure(now);
            return;
        }
        const bool due = !last_close_attempt_at_ ||
            now - *last_close_attempt_at_ >= kGracefulCloseRetryInterval;
        if (due && graceful_close_.send_attempt_count < kGracefulCloseMaximumSendAttempts) {
            auto prepared = session_.prepare_outgoing_packet(DisconnectRequestBuilder::build());
            bool terminal_payload = prepared && prepared.plan &&
                prepared.plan->unreliable_payload_size() == DisconnectRequestBuilder::build().size();
            if (!prepared && prepared.error &&
                (prepared.error->code == NetchanSessionErrorCode::combined_payload_does_not_fit ||
                 prepared.error->code == NetchanSessionErrorCode::outgoing_fragment_datagram_does_not_fit) &&
                graceful_close_.support_send_attempt_count < kGracefulCloseMaximumSupportSendAttempts) {
                // Flush an existing complete reliable unit, never rewrite or
                // drop it to make room for the terminal suffix.
                prepared = session_.prepare_outgoing_packet();
            }
            if (prepared && prepared.plan &&
                (terminal_payload || graceful_close_.support_send_attempt_count <
                    kGracefulCloseMaximumSupportSendAttempts)) {
                const auto& packet = prepared.plan->packet();
                const auto sequence = packet.header.sequence.sequence;
                auto encoded = encode_packet(packet, now);
                if (!encoded) return;
                last_close_attempt_at_ = now;
                if (terminal_payload) ++graceful_close_.send_attempt_count;
                else ++graceful_close_.support_send_attempt_count;
                const auto* fragment = prepared.plan->fragment_plan()
                    ? &*prepared.plan->fragment_plan() : nullptr;
                if (!send_datagram(*encoded, packet.header, packet.payload.size(), now,
                                   fragment, 0U, true)) {
                    static_cast<void>(session_.abandon_outgoing_packet(std::move(*prepared.plan)));
                } else {
                    const auto committed = session_.commit_outgoing_send(std::move(*prepared.plan));
                    if (!committed) {
                        enter_terminal(NetchanDriverState::protocol_error,
                            NetchanDriverErrorCode::packet_encode_failed,
                            "Sent terminal packet could not be committed", now,
                            NetchanDriverTraceClassification::protocol_failure,
                            NetchanDriverEventType::protocol_error, std::nullopt,
                            committed.error ? std::optional{committed.error->code} : std::nullopt);
                        return;
                    }
                    if (terminal_payload) {
                        ++graceful_close_.successful_send_count;
                        if (!first_close_sent_sequence_) first_close_sent_sequence_ = sequence;
                        if (!graceful_close_.first_send_at) graceful_close_.first_send_at = now;
                        graceful_close_.last_send_at = now;
                    } else ++graceful_close_.successful_support_send_count;
                }
            } else if (!prepared && prepared.error &&
                prepared.error->code != NetchanSessionErrorCode::combined_payload_does_not_fit &&
                prepared.error->code != NetchanSessionErrorCode::outgoing_fragment_datagram_does_not_fit) {
                enter_terminal(NetchanDriverState::protocol_error,
                    NetchanDriverErrorCode::packet_encode_failed,
                    "Netchan rejected the typed terminal packet", now,
                    NetchanDriverTraceClassification::protocol_failure,
                    NetchanDriverEventType::protocol_error, std::nullopt, prepared.error->code);
                return;
            }
        }
        if (state_ != NetchanDriverState::active) return;
        // Exactly the existing endpoint/sequence/fragment validators, with a
        // bounded RX budget. Deadline is not moved by any traffic.
        std::size_t no_automatic_tx = config_.maximum_outgoing_packets_per_update;
        for (std::size_t count{}; count < config_.maximum_datagrams_per_update &&
             state_ == NetchanDriverState::active; ++count) {
            network::DatagramTransportReceiveResult received;
            try {
                ++receive_statistics_.receive_polls;
                if (movement_diagnostics_enabled_ || multiplayer_enabled())
                    socket_read_begin_ns_ = actual_socket_read_ns();
                received = transport_.receive(config_.maximum_datagram_size);
                if (movement_diagnostics_enabled_ || multiplayer_enabled())
                    socket_read_end_ns_ = actual_socket_read_ns();
            } catch (...) {
                enter_terminal(NetchanDriverState::network_error,
                    NetchanDriverErrorCode::receive_failed,
                    "Datagram transport threw during graceful close", now,
                    NetchanDriverTraceClassification::network_failure,
                    NetchanDriverEventType::network_error);
                return;
            }
            if (!process_receive_result(std::move(received), now, no_automatic_tx)) break;
        }
        if (state_ != NetchanDriverState::active || !first_close_sent_sequence_) return;
        const auto covering = compare_sequences(session_.state().peer_acknowledgement,
                                                *first_close_sent_sequence_);
        if (covering == NetchanSequenceComparison::equal ||
            covering == NetchanSequenceComparison::newer) {
            graceful_close_.ack_observed = true;
            graceful_close_.outcome = GracefulCloseOutcome::transport_acknowledged;
            close(now);
        }
    }

    void send_pending(
        const NetchanDriverTimePoint now,
        std::size_t& outgoing_packets_this_update)
    {
        while (state_ == NetchanDriverState::active &&
               outgoing_packets_this_update < config_.maximum_outgoing_packets_per_update) {
            const auto& in_flight = session_.in_flight_reliable_payload();
            const bool reliable_work = !session_.pending_reliable_payload().empty() ||
                                       (in_flight && in_flight->retransmission_requested) ||
                                       session_.has_outgoing_fragment_send_work();
            const bool before_first_acknowledgement =
                !session_.first_acknowledgement_sent();
            // Protocol 48 may begin with client reliable data immediately after
            // ACCEPT. Before the first server packet is admitted, permit only
            // that bounded reliable work; keep unrelated unreliable bytes
            // pending until the ordinary channel acknowledgement is established.
            // With no queued reliable data the legacy receive-first bootstrap
            // remains byte-for-byte unchanged and sends nothing here.
            if (before_first_acknowledgement && !reliable_work) {
                return;
            }
            const bool idle_poll_due =
                config_.idle_poll_interval > std::chrono::milliseconds::zero() &&
                last_transport_send_time_ &&
                now - *last_transport_send_time_ >= config_.idle_poll_interval;
            if (!acknowledgement_pending_ && pending_unreliable_payload_.empty() &&
                !reliable_work && !idle_poll_due) {
                return;
            }

            const auto unreliable_payload = before_first_acknowledgement
                                                ? std::span<const std::byte>{}
                                                : std::span<const std::byte>{
                                                      pending_unreliable_payload_};
            auto prepared = session_.prepare_outgoing_packet(unreliable_payload);
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
                    fragment_transfer_size,
                    true)) {
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
        const std::size_t fragment_transfer_size = 0U,
        const bool retain_on_would_block = false)
    {
        if (transmitted_packet_count_ ==
            std::numeric_limits<std::size_t>::max()) {
            enter_terminal(
                NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::transmitted_packet_count_overflow,
                "Netchan transmitted-packet counter is exhausted",
                now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        network::DatagramSendResult sent;
        const auto send_begin=multiplayer_enabled()
            ? std::optional{core::multiplayer_diagnostic_now_ns()} : std::nullopt;
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
        if (retain_on_would_block && sent.status == network::DatagramSendStatus::would_block && sent.error.empty()) {
            if (graceful_close_.requested) ++graceful_close_.would_block_count;
            return false; // Context remains owned; stop before RX can revise it.
        }
        if (!sent || !sent.error.empty()) {
            enter_terminal(
                NetchanDriverState::network_error,
                NetchanDriverErrorCode::send_failed,
                sent.error.empty() ? "Datagram transport failed to send netchan traffic"
                                   : std::move(sent.error),
                now,
                NetchanDriverTraceClassification::network_failure,
                NetchanDriverEventType::network_error,
                std::nullopt, std::nullopt, std::nullopt,
                native_failure(sent.native_error));
            return false;
        }
        ++transmitted_packet_count_;
        observe_multiplayer_network(core::MultiplayerNetworkPhase::packet_sent,
            &header,datagram.size(),send_begin,
            multiplayer_enabled() ? std::optional{core::multiplayer_diagnostic_now_ns()} : std::nullopt);
        // Only a successful send advances the idle poll clock. A blocked send
        // retains its sequence and can retry without a burst/catch-up schedule.
        last_transport_send_time_ = now;
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

    [[nodiscard]] bool retain_completion_witness(
        const bool completes_reliable,
        std::optional<NetchanCompletedFragmentTransfer>& result,
        const NetchanDriverTimePoint now)
    {
        const auto& transfer = session_.outgoing_fragment_transfer();
        if (!completes_reliable || !transfer) return true;
        const auto& in_flight = session_.in_flight_reliable_payload();
        if (!in_flight || !transfer->transfer_id.valid() ||
            transfer->canonical_bytes.empty() ||
            transfer->canonical_bytes.size() > kMaximumOutgoingNormalFragmentTransferSize ||
            in_flight->bytes.empty() ||
            in_flight->bytes.size() > kStockProtocol48NormalFragmentChunkSize ||
            transfer->fragment_count == 0U ||
            transfer->fragment_count > kMaximumOutgoingNormalFragments ||
            transfer->current_fragment_index != transfer->fragment_count) {
            enter_terminal(NetchanDriverState::protocol_error,
                NetchanDriverErrorCode::invalid_acknowledgement,
                "Reliable completion has no bounded final-fragment witness", now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
        try {
            result.emplace(NetchanCompletedFragmentTransfer{*transfer, *in_flight});
            return true;
        } catch (...) {
            // Nothing has been committed or acknowledged to the consumer yet.
            enter_terminal(NetchanDriverState::backpressure,
                NetchanDriverErrorCode::event_backpressure,
                "Unable to retain bounded reliable-completion witness", now,
                NetchanDriverTraceClassification::protocol_failure,
                NetchanDriverEventType::protocol_error);
            return false;
        }
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
            std::nullopt,
        const std::optional<network::SocketNativeError> native_error = std::nullopt)
    {
        if (terminal_state(state_)) {
            return;
        }
        state_ = state;
        if (graceful_close_.requested && graceful_close_.outcome == GracefulCloseOutcome::pending) {
            graceful_close_.transport_error = code;
            graceful_close_.netchan_closed_at = now;
            graceful_close_.outcome = code == NetchanDriverErrorCode::send_failed
                ? GracefulCloseOutcome::send_failed : GracefulCloseOutcome::fatal_failure;
        }
        if (code) {
            last_error_ = NetchanDriverError{
                *code,
                packet_code,
                session_code,
                bounded_context(std::move(context)),
                reassembly_code,
                native_error,
                received_packet_failure(*code) ? active_receive_metadata_ : std::nullopt,
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
        split_transfers_ = {};
        std::vector<std::byte>{}.swap(pending_unreliable_payload_);
        pending_contextual_plan_.reset();
        pending_contextual_plan_identity_.reset();
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
        if(multiplayer_enabled()) {
            using P=core::MultiplayerNetworkPhase;
            std::optional<P> phase;
            switch(classification) {
            case NetchanDriverTraceClassification::sequenced_packet_received: phase=P::socket_read;break;
            case NetchanDriverTraceClassification::duplicate_sequence_ignored: phase=P::duplicate;break;
            case NetchanDriverTraceClassification::older_sequence_ignored: phase=P::reordered;break;
            case NetchanDriverTraceClassification::wrong_endpoint_ignored:
            case NetchanDriverTraceClassification::protocol_failure: phase=P::rejected;break;
            default:break;
            }
            if(phase) observe_multiplayer_network(*phase,header,datagram_size,
                socket_read_begin_ns_,socket_read_end_ns_);
        }
        if (classification == NetchanDriverTraceClassification::duplicate_sequence_ignored ||
                   classification == NetchanDriverTraceClassification::older_sequence_ignored) {
            ++receive_statistics_.rejected_sequences;
        }
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
    std::shared_ptr<const NetchanDriverContextIdentity>
        outgoing_context_identity_{
            std::make_shared<const NetchanDriverContextIdentity>()};
    NetchanNormalReassembler reassembler_;
    std::array<SplitTransfer, 2U> split_transfers_;
    EventRing events_;
    bool configuration_valid_{false};
    NetchanDriverState state_{NetchanDriverState::idle};
    std::optional<NetchanDriverError> last_error_;
    std::optional<NetchanFailurePacketMetadata> active_receive_metadata_;
    std::optional<network::NetworkAddress> local_endpoint_;
    std::optional<NetchanDriverTimePoint> started_at_;
    std::optional<NetchanDriverTimePoint> last_update_;
    std::optional<NetchanDriverTimePoint> last_valid_packet_time_;
    std::optional<NetchanDriverTimePoint> last_transport_send_time_;
    std::vector<std::byte> pending_unreliable_payload_;
    std::optional<NetchanTransmitPlan> pending_contextual_plan_;
    std::optional<std::uint64_t> pending_contextual_plan_identity_;
    std::optional<std::uint64_t> last_sent_unreliable_context_identity_;
    bool acknowledgement_pending_{false};
    bool cleanup_done_{false};
    std::size_t transmitted_packet_count_{0U};
    NetchanDriverReceiveStatistics receive_statistics_{};
    bool movement_diagnostics_enabled_{false};
    core::MultiplayerDiagnosticSink* multiplayer_sink_{};
    std::uint64_t multiplayer_event_id_{},multiplayer_network_generation_{},multiplayer_map_generation_{};
    std::optional<std::uint64_t> socket_read_begin_ns_;
    std::optional<std::uint64_t> socket_read_end_ns_;
    std::size_t cleanup_count_{0U};
    std::uint64_t outgoing_context_revision_{0U};
    std::uint64_t next_outgoing_context_plan_identity_{1U};
    GracefulCloseSummary graceful_close_;
    std::optional<NetchanDriverTimePoint> last_close_attempt_at_;
    std::optional<NetchanSequence> first_close_sent_sequence_;
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

bool NetchanDriver::request_graceful_close(const NetchanDriverTimePoint now)
{
    return implementation_->request_graceful_close(now);
}

void NetchanDriver::confirm_server_disconnect(const NetchanDriverTimePoint now)
{
    implementation_->confirm_server_disconnect(now);
}

GracefulCloseSummary NetchanDriver::graceful_close_snapshot() const noexcept
{
    return implementation_->graceful_close_snapshot();
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

NetchanOutgoingContextPrepareResult
NetchanDriver::prepare_unreliable_context()
{
    return implementation_->prepare_unreliable_context();
}

NetchanDriverOperationResult NetchanDriver::commit_unreliable(
    NetchanOutgoingContextPlan&& plan,
    const std::span<const std::byte> payload)
{
    return implementation_->commit_unreliable(
        std::move(plan), payload);
}

NetchanDriverOperationResult NetchanDriver::abandon_unreliable(
    NetchanOutgoingContextPlan&& plan)
{
    return implementation_->abandon_unreliable(std::move(plan));
}

std::optional<NetchanDriverEvent> NetchanDriver::poll_event()
{
    return implementation_->poll_event();
}

bool NetchanDriver::prepend_retained_payloads(
    const std::span<OwnedNetchanPayload> payloads) noexcept
{
    return implementation_->prepend_retained_payloads(payloads);
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

NetchanDriverReceiveStatistics NetchanDriver::receive_statistics() const noexcept
{
    return implementation_->receive_statistics();
}

void NetchanDriver::set_movement_diagnostics_enabled(const bool enabled) noexcept
{
    implementation_->set_movement_diagnostics_enabled(enabled);
}
void NetchanDriver::set_multiplayer_diagnostics(core::MultiplayerDiagnosticSink* sink,
    const std::uint64_t network_generation,const std::uint64_t map_generation) noexcept
{
    implementation_->set_multiplayer_diagnostics(sink,network_generation,map_generation);
}

std::optional<std::uint64_t>
NetchanDriver::last_sent_unreliable_context_identity() const noexcept
{
    return implementation_->last_sent_unreliable_context_identity();
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
