#include <hlclient/goldsrc/initial_signon_stage.hpp>

#include <algorithm>
#include <exception>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(const InitialSignonState state) noexcept
{
    switch (state) {
    case InitialSignonState::signon_boundary_reached:
    case InitialSignonState::timed_out:
    case InitialSignonState::cancelled:
    case InitialSignonState::network_error:
    case InitialSignonState::protocol_error:
    case InitialSignonState::unsupported_service_message:
    case InitialSignonState::backpressure:
    case InitialSignonState::secondary_stream_pending_m3:
        return true;
    case InitialSignonState::idle:
    case InitialSignonState::waiting_for_request_transmit:
    case InitialSignonState::waiting_for_request_ack:
    case InitialSignonState::waiting_for_server_payload:
    case InitialSignonState::decoding_service_stream:
        return false;
    }
    return true;
}

[[nodiscard]] std::string bounded_context(std::string context)
{
    if (context.size() > kInitialSignonDiagnosticTextLimit) {
        context.resize(kInitialSignonDiagnosticTextLimit);
    }
    return context;
}

class InitialSignonEventRing final {
public:
    explicit InitialSignonEventRing(const std::size_t capacity)
        : slots_(capacity)
    {
    }

    [[nodiscard]] bool can_push(const std::size_t count = 1U) const noexcept
    {
        return count <= slots_.size() - size_;
    }

    void push(InitialSignonEvent event) noexcept
    {
        const auto index = (head_ + size_) % slots_.size();
        slots_[index].emplace(std::move(event));
        ++size_;
    }

    [[nodiscard]] std::optional<InitialSignonEvent> pop() noexcept
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
    std::vector<std::optional<InitialSignonEvent>> slots_;
    std::size_t head_{0U};
    std::size_t size_{0U};
};

static_assert(std::is_nothrow_move_constructible_v<InitialSignonEvent>);

[[nodiscard]] InitialSignonErrorCode map_driver_error(
    const NetchanDriverErrorCode code) noexcept
{
    switch (code) {
    case NetchanDriverErrorCode::channel_inactivity_timed_out:
        return InitialSignonErrorCode::channel_inactivity_timed_out;
    case NetchanDriverErrorCode::fragment_transfer_timed_out:
        return InitialSignonErrorCode::fragment_transfer_timed_out;
    case NetchanDriverErrorCode::secondary_stream_pending_m3:
        return InitialSignonErrorCode::secondary_stream_pending_m3;
    case NetchanDriverErrorCode::receive_failed:
    case NetchanDriverErrorCode::inconsistent_receive_result:
    case NetchanDriverErrorCode::local_endpoint_unavailable:
    case NetchanDriverErrorCode::local_endpoint_changed:
    case NetchanDriverErrorCode::send_failed:
        return InitialSignonErrorCode::driver_network_error;
    case NetchanDriverErrorCode::time_moved_backwards:
        return InitialSignonErrorCode::time_moved_backwards;
    case NetchanDriverErrorCode::event_backpressure:
        return InitialSignonErrorCode::service_event_backpressure;
    case NetchanDriverErrorCode::invalid_configuration:
    case NetchanDriverErrorCode::not_active:
    case NetchanDriverErrorCode::reentrant_operation:
    case NetchanDriverErrorCode::datagram_truncated:
    case NetchanDriverErrorCode::unexpected_connectionless_packet:
    case NetchanDriverErrorCode::unsupported_special_packet:
    case NetchanDriverErrorCode::malformed_packet:
    case NetchanDriverErrorCode::invalid_sequence:
    case NetchanDriverErrorCode::invalid_acknowledgement:
    case NetchanDriverErrorCode::opaque_payload_too_large:
    case NetchanDriverErrorCode::packet_encode_failed:
    case NetchanDriverErrorCode::reliable_queue_failed:
    case NetchanDriverErrorCode::unreliable_payload_too_large:
    case NetchanDriverErrorCode::unreliable_payload_pending:
    case NetchanDriverErrorCode::fragment_reassembly_failed:
        return InitialSignonErrorCode::driver_protocol_error;
    }
    return InitialSignonErrorCode::driver_protocol_error;
}

[[nodiscard]] InitialSignonState state_for_error(
    const InitialSignonErrorCode code) noexcept
{
    switch (code) {
    case InitialSignonErrorCode::channel_inactivity_timed_out:
    case InitialSignonErrorCode::fragment_transfer_timed_out:
        return InitialSignonState::timed_out;
    case InitialSignonErrorCode::secondary_stream_pending_m3:
        return InitialSignonState::secondary_stream_pending_m3;
    case InitialSignonErrorCode::driver_network_error:
        return InitialSignonState::network_error;
    case InitialSignonErrorCode::unsupported_service_opcode:
        return InitialSignonState::unsupported_service_message;
    case InitialSignonErrorCode::service_event_backpressure:
        return InitialSignonState::backpressure;
    case InitialSignonErrorCode::invalid_configuration:
    case InitialSignonErrorCode::driver_start_failed:
    case InitialSignonErrorCode::initial_request_build_failed:
    case InitialSignonErrorCode::initial_request_queue_failed:
    case InitialSignonErrorCode::time_moved_backwards:
    case InitialSignonErrorCode::service_payload_before_ack_overflow:
    case InitialSignonErrorCode::service_payload_envelope_decode_failed:
    case InitialSignonErrorCode::service_message_decode_failed:
    case InitialSignonErrorCode::driver_protocol_error:
        return InitialSignonState::protocol_error;
    }
    return InitialSignonState::protocol_error;
}

[[nodiscard]] InitialSignonTraceClassification trace_for_error(
    const InitialSignonErrorCode code) noexcept
{
    switch (code) {
    case InitialSignonErrorCode::channel_inactivity_timed_out:
    case InitialSignonErrorCode::fragment_transfer_timed_out:
        return InitialSignonTraceClassification::stage_timed_out;
    case InitialSignonErrorCode::secondary_stream_pending_m3:
        return InitialSignonTraceClassification::secondary_stream_pending_m3;
    case InitialSignonErrorCode::driver_network_error:
        return InitialSignonTraceClassification::network_failure;
    case InitialSignonErrorCode::service_event_backpressure:
        return InitialSignonTraceClassification::backpressure;
    case InitialSignonErrorCode::invalid_configuration:
    case InitialSignonErrorCode::driver_start_failed:
    case InitialSignonErrorCode::initial_request_build_failed:
    case InitialSignonErrorCode::initial_request_queue_failed:
    case InitialSignonErrorCode::time_moved_backwards:
    case InitialSignonErrorCode::service_payload_before_ack_overflow:
    case InitialSignonErrorCode::service_payload_envelope_decode_failed:
    case InitialSignonErrorCode::service_message_decode_failed:
    case InitialSignonErrorCode::unsupported_service_opcode:
    case InitialSignonErrorCode::driver_protocol_error:
        return InitialSignonTraceClassification::protocol_failure;
    }
    return InitialSignonTraceClassification::protocol_failure;
}

} // namespace

bool valid_initial_signon_configuration(const InitialSignonConfig& config) noexcept
{
    return valid_configuration(config.driver) &&
           valid_service_payload_envelope_limits(config.service_payload_envelope) &&
           valid_service_message_limits(config.service_messages) &&
           config.maximum_events > 0U &&
           config.maximum_events <= kMaximumInitialSignonEvents &&
           config.maximum_driver_events_per_update > 0U &&
           config.maximum_driver_events_per_update <=
               kMaximumInitialSignonDriverEventsPerUpdate;
}

class InitialSignonStage::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        InitialSignonConfig config,
        InitialSignonTraceCallback trace_callback)
        : transport_{transport},
          remote_endpoint_{remote_endpoint},
          config_{std::move(config)},
          trace_callback_{std::move(trace_callback)},
          configuration_valid_{valid_initial_signon_configuration(config_)},
          events_{configuration_valid_ ? config_.maximum_events : 0U}
    {
        if (configuration_valid_) {
            accumulated_messages_.reserve(config_.maximum_events);
        }
    }

    ~Implementation()
    {
        cleanup(last_update_.value_or(InitialSignonTimePoint{}));
    }

    [[nodiscard]] bool start(
        const InitialSignonTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
    {
        if (trace_callback_active_ || state_ != InitialSignonState::idle ||
            driver_) {
            return false;
        }
        error_.reset();
        if (!configuration_valid_) {
            error_ = InitialSignonError{
                InitialSignonErrorCode::invalid_configuration,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Initial sign-on configuration is outside project bounds",
            };
            return false;
        }

        ClientMessageBuildResult built;
        try {
            built = InitialSignonRequestBuilder::build();
        } catch (...) {
            // Keep the exceptional allocation path allocation-free: the typed
            // code is authoritative and an empty diagnostic is preferable to
            // allowing a second allocation failure to cross the public API.
            error_.emplace();
            error_->code =
                InitialSignonErrorCode::initial_request_build_failed;
            return false;
        }
        if (!built || !built.bytes) {
            error_ = InitialSignonError{
                InitialSignonErrorCode::initial_request_build_failed,
                built.error ? std::optional{built.error->code} : std::nullopt,
                std::nullopt,
                std::nullopt,
                built.error ? bounded_context(built.error->context)
                            : "Unable to build the exact initial sign-on request",
            };
            return false;
        }
        if (built.bytes->empty()) {
            error_ = InitialSignonError{
                InitialSignonErrorCode::initial_request_build_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Exact initial sign-on request must not be empty",
            };
            return false;
        }

        std::unique_ptr<NetchanDriver> candidate;
        try {
            candidate = std::make_unique<NetchanDriver>(
                transport_,
                remote_endpoint_,
                config_.driver,
                std::move(connection_lifetime));
        } catch (...) {
            // Preserve the public no-throw boundary when allocation of the
            // bounded driver itself fails. The typed code is authoritative;
            // constructing a diagnostic string here could throw again.
            error_.emplace();
            error_->code = InitialSignonErrorCode::driver_start_failed;
            return false;
        }
        if (!candidate->start(now, expected_local_endpoint)) {
            const auto& driver_error = candidate->last_error();
            error_ = InitialSignonError{
                InitialSignonErrorCode::driver_start_failed,
                std::nullopt,
                std::nullopt,
                driver_error ? std::optional{driver_error->code} : std::nullopt,
                driver_error ? bounded_context(driver_error->context)
                             : "Persistent netchan driver rejected start",
            };
            return false;
        }

        NetchanDriverOperationResult queued;
        try {
            queued = candidate->queue_reliable(*built.bytes);
        } catch (...) {
            candidate->close(now);
            // queue_reliable can only throw here through allocation. Avoid a
            // second allocation while translating it to the typed outcome.
            error_.emplace();
            error_->code =
                InitialSignonErrorCode::initial_request_queue_failed;
            return false;
        }
        if (!queued) {
            candidate->close(now);
            error_ = InitialSignonError{
                InitialSignonErrorCode::initial_request_queue_failed,
                std::nullopt,
                std::nullopt,
                queued.error ? std::optional{queued.error->code} : std::nullopt,
                queued.error ? bounded_context(queued.error->context)
                             : "Persistent driver rejected the initial request",
            };
            return false;
        }

        driver_ = std::move(candidate);
        local_endpoint_ = expected_local_endpoint;
        last_update_ = now;
        request_size_ = built.bytes->size();
        request_queue_count_ = 1U;
        state_ = InitialSignonState::waiting_for_request_transmit;
        events_.push(InitialSignonEvent{
            InitialSignonEventType::initial_request_queued,
            std::nullopt,
            0U,
            request_size_,
            0U,
            now,
        });
        emit_trace(InitialSignonTraceClassification::stage_started);
        emit_trace(InitialSignonTraceClassification::initial_request_queued);
        return true;
    }

    void update(const InitialSignonTimePoint now)
    {
        if (trace_callback_active_ || state_ == InitialSignonState::idle ||
            terminal_state(state_) || !driver_) {
            return;
        }
        if (!last_update_ || now < *last_update_) {
            fail(
                InitialSignonErrorCode::time_moved_backwards,
                "Initial sign-on update time moved backwards",
                now);
            return;
        }
        last_update_ = now;

        if (pending_decode_payload_) {
            decode_pending_payload(now);
            if (terminal_state(state_) || pending_decode_payload_) {
                return;
            }
        }

        std::size_t processed_events = 0U;
        drain_driver_events(now, processed_events);
        if (terminal_state(state_) || pending_decode_payload_ ||
            processed_events >= config_.maximum_driver_events_per_update) {
            if (pending_decode_payload_) {
                decode_pending_payload(now);
            }
            return;
        }

        // Reserve metadata capacity before a network mutation that may make the
        // request in-flight for the first time.
        if (!request_transmitted_ && !events_.can_push()) {
            fail(
                InitialSignonErrorCode::service_event_backpressure,
                "No bounded sign-on event slot remains for request transmission",
                now);
            return;
        }

        driver_->update(now);
        observe_request_transmit(now);
        if (terminal_state(state_)) {
            return;
        }
        drain_driver_events(now, processed_events);
        if (terminal_state(state_)) {
            return;
        }
        if (pending_decode_payload_) {
            decode_pending_payload(now);
            if (terminal_state(state_)) {
                return;
            }
        }
        if (driver_->terminal()) {
            fail_from_driver(now);
        }
    }

    void cancel(const InitialSignonTimePoint now)
    {
        if (trace_callback_active_ || state_ == InitialSignonState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (driver_ && !driver_->terminal()) {
            driver_->cancel(now);
        }
        state_ = InitialSignonState::cancelled;
        cleanup(now);
        emit_trace(InitialSignonTraceClassification::stage_cancelled);
    }

    [[nodiscard]] std::optional<InitialSignonEvent> poll_event() noexcept
    {
        return events_.pop();
    }

    [[nodiscard]] InitialSignonState state() const noexcept { return state_; }
    [[nodiscard]] bool terminal() const noexcept { return terminal_state(state_); }
    [[nodiscard]] const std::optional<InitialSignonResult>& result() const noexcept
    {
        return result_;
    }
    [[nodiscard]] const std::optional<InitialSignonError>& error() const noexcept
    {
        return error_;
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
    [[nodiscard]] std::size_t pending_event_count() const noexcept
    {
        return events_.size();
    }
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept
    {
        return driver_ ? driver_->transmitted_packet_count() : 0U;
    }
    [[nodiscard]] std::size_t cleanup_count() const noexcept { return cleanup_count_; }
    [[nodiscard]] std::size_t request_queue_count() const noexcept
    {
        return request_queue_count_;
    }
    [[nodiscard]] const NetchanDriver* driver() const noexcept { return driver_.get(); }

private:
    void observe_request_transmit(const InitialSignonTimePoint now)
    {
        if (request_transmitted_ || !driver_) {
            return;
        }
        const auto& in_flight = driver_->session().in_flight_reliable_payload();
        if (!in_flight) {
            return;
        }
        if (!events_.can_push()) {
            fail(
                InitialSignonErrorCode::service_event_backpressure,
                "No bounded sign-on event slot remains for request transmission",
                now);
            return;
        }
        request_transmitted_ = true;
        state_ = InitialSignonState::waiting_for_request_ack;
        events_.push(InitialSignonEvent{
            InitialSignonEventType::initial_request_transmitted,
            std::nullopt,
            0U,
            request_size_,
            0U,
            now,
        });
        emit_trace(InitialSignonTraceClassification::initial_request_transmitted);
    }

    void drain_driver_events(
        const InitialSignonTimePoint now,
        std::size_t& processed_events)
    {
        while (driver_ &&
               processed_events < config_.maximum_driver_events_per_update &&
               !terminal_state(state_) && !pending_decode_payload_) {
            auto event = driver_->poll_event();
            if (!event) {
                return;
            }
            ++processed_events;
            handle_driver_event(std::move(*event), now);
        }
    }

    void handle_driver_event(
        NetchanDriverEvent event,
        const InitialSignonTimePoint now)
    {
        switch (event.type) {
        case NetchanDriverEventType::payload_ready:
            if (!event.payload) {
                fail(
                    InitialSignonErrorCode::driver_protocol_error,
                    "Driver payload-ready event did not own a payload",
                    now);
                return;
            }
            handle_payload(std::move(*event.payload), now);
            return;
        case NetchanDriverEventType::reliable_payload_acknowledged:
            handle_request_acknowledgement(now);
            return;
        case NetchanDriverEventType::normal_transfer_started:
        case NetchanDriverEventType::normal_transfer_completed:
            return;
        case NetchanDriverEventType::normal_transfer_timed_out:
            fail(
                InitialSignonErrorCode::fragment_transfer_timed_out,
                "Normal fragment transfer timed out before sign-on decoding",
                now,
                std::nullopt,
                std::nullopt,
                NetchanDriverErrorCode::fragment_transfer_timed_out);
            return;
        case NetchanDriverEventType::secondary_stream_pending_m3:
            fail(
                InitialSignonErrorCode::secondary_stream_pending_m3,
                "Secondary fragment stream is outside M2.4.1",
                now,
                std::nullopt,
                std::nullopt,
                NetchanDriverErrorCode::secondary_stream_pending_m3);
            return;
        case NetchanDriverEventType::channel_timed_out:
            fail(
                InitialSignonErrorCode::channel_inactivity_timed_out,
                "Netchan channel timed out before the sign-on boundary",
                now,
                std::nullopt,
                std::nullopt,
                NetchanDriverErrorCode::channel_inactivity_timed_out);
            return;
        case NetchanDriverEventType::cancelled:
            state_ = InitialSignonState::cancelled;
            cleanup(now);
            emit_trace(InitialSignonTraceClassification::stage_cancelled);
            return;
        case NetchanDriverEventType::network_error:
        case NetchanDriverEventType::protocol_error:
            fail_from_driver(now);
            return;
        }
    }

    void handle_request_acknowledgement(const InitialSignonTimePoint now)
    {
        if (!request_transmitted_ || request_acknowledged_) {
            fail(
                InitialSignonErrorCode::driver_protocol_error,
                "Unexpected reliable acknowledgement in initial sign-on stage",
                now);
            return;
        }
        if (!events_.can_push()) {
            fail(
                InitialSignonErrorCode::service_event_backpressure,
                "No bounded sign-on event slot remains for request acknowledgement",
                now);
            return;
        }
        request_acknowledged_ = true;
        state_ = InitialSignonState::waiting_for_server_payload;
        events_.push(InitialSignonEvent{
            InitialSignonEventType::initial_request_acknowledged,
            std::nullopt,
            0U,
            request_size_,
            0U,
            now,
        });
        emit_trace(InitialSignonTraceClassification::initial_request_acknowledged);
        if (pre_ack_payload_) {
            pending_decode_payload_.emplace(std::move(*pre_ack_payload_));
            pre_ack_payload_.reset();
            state_ = InitialSignonState::decoding_service_stream;
        }
    }

    void handle_payload(
        OwnedNetchanPayload payload,
        const InitialSignonTimePoint now)
    {
        if (!request_acknowledged_) {
            if (pre_ack_payload_) {
                fail(
                    InitialSignonErrorCode::service_payload_before_ack_overflow,
                    "More than one owning service payload arrived before request ACK",
                    now);
                return;
            }
            emit_trace(
                InitialSignonTraceClassification::service_payload_received,
                payload.bytes.size());
            pre_ack_payload_.emplace(std::move(payload));
            return;
        }
        if (pending_decode_payload_) {
            fail(
                InitialSignonErrorCode::service_event_backpressure,
                "A second service payload arrived before bounded decode commit",
                now);
            return;
        }
        emit_trace(
            InitialSignonTraceClassification::service_payload_received,
            payload.bytes.size());
        pending_decode_payload_.emplace(std::move(payload));
        state_ = InitialSignonState::decoding_service_stream;
    }

    void decode_pending_payload(const InitialSignonTimePoint now)
    {
        if (!pending_decode_payload_ || terminal_state(state_)) {
            return;
        }
        const auto payload_size = pending_decode_payload_->bytes.size();
        ServicePayloadEnvelopeDecodeResult envelope;
        try {
            ServicePayloadEnvelopeDecoder envelope_decoder{
                config_.service_payload_envelope};
            envelope = envelope_decoder.decode(std::move(*pending_decode_payload_));
        } catch (...) {
            pending_decode_payload_.reset();
            fail(
                InitialSignonErrorCode::service_payload_envelope_decode_failed,
                "Bounded in-memory service envelope decoder threw",
                now);
            return;
        }
        pending_decode_payload_.reset();
        if (!envelope || !envelope.envelope) {
            fail(
                InitialSignonErrorCode::service_payload_envelope_decode_failed,
                envelope.error ? bounded_context(envelope.error->context)
                               : "Unable to decode the bounded service envelope",
                now,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                envelope.error ? std::optional{envelope.error->code} : std::nullopt);
            return;
        }

        ServiceMessageDecodeResult decoded;
        try {
            ServiceMessageStreamDecoder decoder{config_.service_messages};
            decoded = decoder.decode(std::move(envelope.envelope->payload));
        } catch (...) {
            fail(
                InitialSignonErrorCode::service_message_decode_failed,
                "Bounded service decoder threw",
                now);
            return;
        }
        if (!decoded || !decoded.stream) {
            const bool unsupported = decoded.error &&
                decoded.error->code ==
                    ServiceMessageErrorCode::unsupported_service_opcode;
            fail(
                unsupported ? InitialSignonErrorCode::unsupported_service_opcode
                            : InitialSignonErrorCode::service_message_decode_failed,
                decoded.error ? bounded_context(decoded.error->context)
                              : "Unable to decode the bounded service payload",
                now,
                std::nullopt,
                decoded.error ? std::optional{decoded.error->code} : std::nullopt);
            return;
        }

        auto& stream = *decoded.stream;
        const auto expected_event_count = stream.messages.size() +
            (stream.boundary ? 1U : 0U);
        if (stream.required_event_count != expected_event_count) {
            fail(
                InitialSignonErrorCode::service_message_decode_failed,
                "Service decoder returned inconsistent bounded event metadata",
                now);
            return;
        }
        const std::size_t required_events = 1U + stream.required_event_count;
        if (stream.required_event_count > config_.maximum_events ||
            !events_.can_push(required_events) ||
            stream.messages.size() >
                config_.maximum_events - accumulated_messages_.size()) {
            fail(
                InitialSignonErrorCode::service_event_backpressure,
                "Decoded service batch exceeds bounded sign-on event/state capacity",
                now);
            return;
        }

        std::vector<DecodedServiceMessage> candidate;
        try {
            candidate = accumulated_messages_;
            candidate.reserve(config_.maximum_events);
            candidate.insert(
                candidate.end(),
                std::make_move_iterator(stream.messages.begin()),
                std::make_move_iterator(stream.messages.end()));
        } catch (...) {
            fail(
                InitialSignonErrorCode::service_event_backpressure,
                "Unable to retain the bounded owning early-message state",
                now);
            return;
        }

        events_.push(InitialSignonEvent{
            InitialSignonEventType::service_payload_received,
            std::nullopt,
            0U,
            0U,
            payload_size,
            now,
        });
        for (const auto& message : candidate | std::views::drop(accumulated_messages_.size())) {
            events_.push(InitialSignonEvent{
                InitialSignonEventType::service_message_decoded,
                message.opcode,
                message.byte_offset,
                message.byte_count,
                payload_size,
                now,
            });
            emit_trace(
                InitialSignonTraceClassification::service_message_decoded,
                payload_size,
                message.opcode,
                message.byte_offset,
                message.byte_count);
        }
        ++service_payload_count_;

        if (stream.boundary) {
            const auto boundary = *stream.boundary;
            events_.push(InitialSignonEvent{
                InitialSignonEventType::signon_boundary_reached,
                boundary.opcode,
                boundary.byte_offset,
                boundary.remaining_byte_count,
                payload_size,
                now,
            });
            result_.emplace(InitialSignonResult{
                std::move(candidate),
                std::move(stream.payload),
                boundary,
                service_payload_count_,
            });
            accumulated_messages_.clear();
            state_ = InitialSignonState::signon_boundary_reached;
            cleanup(now);
            emit_trace(
                InitialSignonTraceClassification::signon_boundary_reached,
                payload_size,
                boundary.opcode,
                boundary.byte_offset,
                boundary.remaining_byte_count);
            return;
        }

        accumulated_messages_.swap(candidate);
        state_ = InitialSignonState::waiting_for_server_payload;
    }

    void fail_from_driver(const InitialSignonTimePoint now)
    {
        if (!driver_) {
            fail(
                InitialSignonErrorCode::driver_protocol_error,
                "Persistent netchan driver is unavailable",
                now);
            return;
        }
        const auto& driver_error = driver_->last_error();
        const auto code = driver_error
                              ? map_driver_error(driver_error->code)
                              : InitialSignonErrorCode::driver_protocol_error;
        fail(
            code,
            driver_error ? bounded_context(driver_error->context)
                         : "Persistent netchan driver terminated unexpectedly",
            now,
            std::nullopt,
            std::nullopt,
            driver_error ? std::optional{driver_error->code} : std::nullopt);
    }

    void fail(
        const InitialSignonErrorCode code,
        std::string context,
        const InitialSignonTimePoint now,
        const std::optional<ClientMessageErrorCode> client_code = std::nullopt,
        const std::optional<ServiceMessageErrorCode> service_code = std::nullopt,
        const std::optional<NetchanDriverErrorCode> driver_code = std::nullopt,
        const std::optional<ServicePayloadEnvelopeErrorCode> envelope_code =
            std::nullopt)
    {
        if (terminal_state(state_)) {
            return;
        }
        state_ = state_for_error(code);
        error_ = InitialSignonError{
            code,
            client_code,
            service_code,
            driver_code,
            bounded_context(std::move(context)),
            envelope_code,
        };
        cleanup(now);
        emit_trace(trace_for_error(code));
    }

    void cleanup(const InitialSignonTimePoint now) noexcept
    {
        if (cleanup_done_ || !driver_) {
            return;
        }
        cleanup_done_ = true;
        ++cleanup_count_;
        if (!driver_->terminal()) {
            driver_->close(now);
        }
        pre_ack_payload_.reset();
        pending_decode_payload_.reset();
        std::vector<DecodedServiceMessage>{}.swap(accumulated_messages_);
    }

    void emit_trace(
        const InitialSignonTraceClassification classification,
        const std::size_t payload_size = 0U,
        const std::optional<ServiceMessageOpcode> opcode = std::nullopt,
        const std::size_t byte_offset = 0U,
        const std::size_t byte_count = 0U)
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        InitialSignonTraceEvent event;
        event.classification = classification;
        event.state = state_;
        event.endpoint = remote_endpoint_;
        event.request_size = request_size_;
        event.payload_size = payload_size;
        event.opcode = opcode;
        event.byte_offset = byte_offset;
        event.byte_count = byte_count;
        event.service_payload_count = service_payload_count_;
        event.transmitted_packet_count = transmitted_packet_count();
        trace_callback_active_ = true;
        try {
            trace_callback_(event);
        } catch (...) {
        }
        trace_callback_active_ = false;
    }

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    InitialSignonConfig config_;
    InitialSignonTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    InitialSignonEventRing events_;
    std::unique_ptr<NetchanDriver> driver_;
    InitialSignonState state_{InitialSignonState::idle};
    std::optional<InitialSignonResult> result_;
    std::optional<InitialSignonError> error_;
    std::optional<network::NetworkAddress> local_endpoint_;
    std::optional<InitialSignonTimePoint> last_update_;
    std::optional<OwnedNetchanPayload> pre_ack_payload_;
    std::optional<OwnedNetchanPayload> pending_decode_payload_;
    std::vector<DecodedServiceMessage> accumulated_messages_;
    std::size_t request_size_{0U};
    std::size_t request_queue_count_{0U};
    std::size_t service_payload_count_{0U};
    std::size_t cleanup_count_{0U};
    bool request_transmitted_{false};
    bool request_acknowledged_{false};
    bool cleanup_done_{false};
};

InitialSignonStage::InitialSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    InitialSignonConfig config,
    InitialSignonTraceCallback trace_callback)
    : implementation_{std::make_unique<Implementation>(
          transport,
          remote_endpoint,
          std::move(config),
          std::move(trace_callback))}
{
}

InitialSignonStage::~InitialSignonStage() = default;

bool InitialSignonStage::start(
    const InitialSignonTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    return implementation_->start(
        now,
        expected_local_endpoint,
        std::move(connection_lifetime));
}

void InitialSignonStage::update(const InitialSignonTimePoint now)
{
    implementation_->update(now);
}

void InitialSignonStage::cancel(const InitialSignonTimePoint now)
{
    implementation_->cancel(now);
}

std::optional<InitialSignonEvent> InitialSignonStage::poll_event()
{
    return implementation_->poll_event();
}

InitialSignonState InitialSignonStage::state() const noexcept
{
    return implementation_->state();
}

bool InitialSignonStage::terminal() const noexcept
{
    return implementation_->terminal();
}

const std::optional<InitialSignonResult>& InitialSignonStage::result() const noexcept
{
    return implementation_->result();
}

const std::optional<InitialSignonError>& InitialSignonStage::error() const noexcept
{
    return implementation_->error();
}

const network::NetworkAddress& InitialSignonStage::remote_endpoint() const noexcept
{
    return implementation_->remote_endpoint();
}

const std::optional<network::NetworkAddress>&
InitialSignonStage::local_endpoint() const noexcept
{
    return implementation_->local_endpoint();
}

std::size_t InitialSignonStage::pending_event_count() const noexcept
{
    return implementation_->pending_event_count();
}

std::size_t InitialSignonStage::transmitted_packet_count() const noexcept
{
    return implementation_->transmitted_packet_count();
}

std::size_t InitialSignonStage::cleanup_count() const noexcept
{
    return implementation_->cleanup_count();
}

std::size_t InitialSignonStage::request_queue_count() const noexcept
{
    return implementation_->request_queue_count();
}

const NetchanDriver* InitialSignonStage::driver() const noexcept
{
    return implementation_->driver();
}

} // namespace hlclient::goldsrc
