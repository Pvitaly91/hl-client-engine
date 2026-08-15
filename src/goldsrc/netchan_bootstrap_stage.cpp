#include <hlclient/goldsrc/netchan_bootstrap_stage.hpp>

#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(const NetchanBootstrapState state) noexcept
{
    switch (state) {
    case NetchanBootstrapState::complete:
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

[[nodiscard]] NetchanBootstrapErrorCode map_error_code(
    const NetchanDriverErrorCode code) noexcept
{
    switch (code) {
    case NetchanDriverErrorCode::invalid_configuration:
    case NetchanDriverErrorCode::not_active:
    case NetchanDriverErrorCode::reentrant_operation:
    case NetchanDriverErrorCode::reliable_queue_failed:
    case NetchanDriverErrorCode::unreliable_payload_too_large:
    case NetchanDriverErrorCode::unreliable_payload_pending:
        return NetchanBootstrapErrorCode::invalid_configuration;
    case NetchanDriverErrorCode::time_moved_backwards:
        return NetchanBootstrapErrorCode::time_moved_backwards;
    case NetchanDriverErrorCode::local_endpoint_unavailable:
        return NetchanBootstrapErrorCode::local_endpoint_unavailable;
    case NetchanDriverErrorCode::local_endpoint_changed:
        return NetchanBootstrapErrorCode::local_endpoint_changed;
    case NetchanDriverErrorCode::receive_failed:
        return NetchanBootstrapErrorCode::receive_failed;
    case NetchanDriverErrorCode::inconsistent_receive_result:
        return NetchanBootstrapErrorCode::inconsistent_receive_result;
    case NetchanDriverErrorCode::datagram_truncated:
        return NetchanBootstrapErrorCode::datagram_truncated;
    case NetchanDriverErrorCode::unexpected_connectionless_packet:
        return NetchanBootstrapErrorCode::unexpected_connectionless_packet;
    case NetchanDriverErrorCode::unsupported_special_packet:
        return NetchanBootstrapErrorCode::unsupported_special_packet;
    case NetchanDriverErrorCode::malformed_packet:
        return NetchanBootstrapErrorCode::malformed_packet;
    case NetchanDriverErrorCode::invalid_sequence:
        return NetchanBootstrapErrorCode::invalid_sequence;
    case NetchanDriverErrorCode::invalid_acknowledgement:
        return NetchanBootstrapErrorCode::invalid_acknowledgement;
    case NetchanDriverErrorCode::opaque_payload_too_large:
        return NetchanBootstrapErrorCode::opaque_payload_too_large;
    case NetchanDriverErrorCode::packet_encode_failed:
        return NetchanBootstrapErrorCode::packet_encode_failed;
    case NetchanDriverErrorCode::send_failed:
        return NetchanBootstrapErrorCode::send_failed;
    case NetchanDriverErrorCode::fragment_reassembly_failed:
        return NetchanBootstrapErrorCode::fragment_reassembly_failed;
    case NetchanDriverErrorCode::secondary_stream_pending_m3:
        return NetchanBootstrapErrorCode::secondary_stream_pending_m3;
    case NetchanDriverErrorCode::fragment_transfer_timed_out:
        return NetchanBootstrapErrorCode::fragment_transfer_timed_out;
    case NetchanDriverErrorCode::channel_inactivity_timed_out:
        return NetchanBootstrapErrorCode::channel_inactivity_timed_out;
    case NetchanDriverErrorCode::event_backpressure:
        return NetchanBootstrapErrorCode::event_backpressure;
    }
    return NetchanBootstrapErrorCode::invalid_configuration;
}

[[nodiscard]] NetchanBootstrapState map_driver_state(
    const NetchanDriverState state) noexcept
{
    switch (state) {
    case NetchanDriverState::idle:
        return NetchanBootstrapState::idle;
    case NetchanDriverState::active:
        return NetchanBootstrapState::waiting_first;
    case NetchanDriverState::cancelled:
        return NetchanBootstrapState::cancelled;
    case NetchanDriverState::timed_out:
        return NetchanBootstrapState::timed_out;
    case NetchanDriverState::network_error:
        return NetchanBootstrapState::network_error;
    case NetchanDriverState::protocol_error:
    case NetchanDriverState::backpressure:
    case NetchanDriverState::closed:
        return NetchanBootstrapState::protocol_error;
    }
    return NetchanBootstrapState::protocol_error;
}

[[nodiscard]] std::optional<NetchanBootstrapTraceClassification>
map_trace_classification(const NetchanDriverTraceClassification classification) noexcept
{
    switch (classification) {
    case NetchanDriverTraceClassification::driver_started:
        return NetchanBootstrapTraceClassification::bootstrap_started;
    case NetchanDriverTraceClassification::receive_would_block:
        return NetchanBootstrapTraceClassification::receive_would_block;
    case NetchanDriverTraceClassification::wrong_endpoint_ignored:
        return NetchanBootstrapTraceClassification::wrong_endpoint_ignored;
    case NetchanDriverTraceClassification::duplicate_sequence_ignored:
        return NetchanBootstrapTraceClassification::duplicate_sequence_ignored;
    case NetchanDriverTraceClassification::older_sequence_ignored:
        return NetchanBootstrapTraceClassification::older_sequence_ignored;
    case NetchanDriverTraceClassification::sequenced_packet_received:
        return NetchanBootstrapTraceClassification::sequenced_packet_received;
    case NetchanDriverTraceClassification::fragment_received:
        return NetchanBootstrapTraceClassification::fragment_received;
    case NetchanDriverTraceClassification::normal_transfer_completed:
        return NetchanBootstrapTraceClassification::normal_transfer_completed;
    case NetchanDriverTraceClassification::payload_ready:
        // The facade emits this exactly once after selecting and owning the
        // bootstrap result. Driver payload events can also be contemporaneous
        // fragment suffixes that are not the bootstrap result.
        return std::nullopt;
    case NetchanDriverTraceClassification::packet_sent:
        return NetchanBootstrapTraceClassification::acknowledgement_sent;
    case NetchanDriverTraceClassification::normal_transfer_timed_out:
        return NetchanBootstrapTraceClassification::normal_transfer_timed_out;
    case NetchanDriverTraceClassification::secondary_stream_pending_m3:
        return NetchanBootstrapTraceClassification::secondary_stream_pending_m3;
    case NetchanDriverTraceClassification::channel_timed_out:
        return NetchanBootstrapTraceClassification::bootstrap_timed_out;
    case NetchanDriverTraceClassification::driver_cancelled:
        return NetchanBootstrapTraceClassification::bootstrap_cancelled;
    case NetchanDriverTraceClassification::network_failure:
        return NetchanBootstrapTraceClassification::network_failure;
    case NetchanDriverTraceClassification::protocol_failure:
        return NetchanBootstrapTraceClassification::protocol_failure;
    case NetchanDriverTraceClassification::driver_closed:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool can_add_timeout(
    const NetchanBootstrapTimePoint now,
    const std::chrono::milliseconds timeout) noexcept
{
    const auto duration =
        std::chrono::duration_cast<NetchanBootstrapClock::duration>(timeout);
    return duration > NetchanBootstrapClock::duration::zero() &&
           now <= NetchanBootstrapTimePoint::max() - duration;
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
      trace_callback_{std::move(trace_callback)}
{
}

bool NetchanBootstrapStage::start(
    const NetchanBootstrapTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != NetchanBootstrapState::idle) {
        return false;
    }

    started_at_ = now;
    last_update_ = now;
    local_endpoint_ = expected_local_endpoint;
    const auto configured_driver = driver_config();
    if (config_.first_packet_timeout <= std::chrono::milliseconds::zero() ||
        config_.first_packet_timeout > kMaximumNetchanBootstrapTimeout ||
        !can_add_timeout(now, config_.first_packet_timeout) ||
        !::hlclient::goldsrc::valid_configuration(configured_driver)) {
        state_ = NetchanBootstrapState::protocol_error;
        error_ = NetchanBootstrapError{
            NetchanBootstrapErrorCode::invalid_configuration,
            std::nullopt,
            std::nullopt,
            "Invalid bounded netchan bootstrap/driver configuration",
            std::nullopt,
        };
        emit_trace(
            NetchanBootstrapTraceClassification::protocol_failure,
            now,
            remote_endpoint_);
        return false;
    }
    first_packet_deadline_ = now +
        std::chrono::duration_cast<NetchanBootstrapClock::duration>(
            config_.first_packet_timeout);
    state_ = NetchanBootstrapState::waiting_first;

    try {
        driver_ = std::make_unique<NetchanDriver>(
            transport_,
            remote_endpoint_,
            configured_driver,
            std::move(connection_lifetime),
            [this](const NetchanDriverTraceEvent& event) {
                handle_driver_trace(event);
            });
    } catch (...) {
        state_ = NetchanBootstrapState::protocol_error;
        error_ = NetchanBootstrapError{
            NetchanBootstrapErrorCode::invalid_configuration,
            std::nullopt,
            std::nullopt,
            "Unable to create the bounded persistent netchan driver",
            std::nullopt,
        };
        emit_trace(
            NetchanBootstrapTraceClassification::protocol_failure,
            now,
            remote_endpoint_);
        return false;
    }

    if (!driver_->start(now, expected_local_endpoint)) {
        fail_from_driver(now);
        return false;
    }
    local_endpoint_ = driver_->local_endpoint();
    return true;
}

void NetchanBootstrapStage::update(const NetchanBootstrapTimePoint now)
{
    if (trace_callback_active_ || state_ != NetchanBootstrapState::waiting_first ||
        !driver_) {
        return;
    }
    if (last_update_ && now < *last_update_) {
        driver_->update(now);
        fail_from_driver(now);
        return;
    }
    last_update_ = now;

    if (!driver_->session().first_incoming_committed() &&
        first_packet_deadline_ && now >= *first_packet_deadline_) {
        driver_->close(now);
        state_ = NetchanBootstrapState::timed_out;
        error_ = NetchanBootstrapError{
            NetchanBootstrapErrorCode::channel_inactivity_timed_out,
            std::nullopt,
            std::nullopt,
            "Netchan first-packet deadline elapsed",
            std::nullopt,
        };
        emit_trace(
            NetchanBootstrapTraceClassification::bootstrap_timed_out,
            now,
            remote_endpoint_);
        return;
    }

    driver_->update(now);
    synchronize_from_driver(now);
}

void NetchanBootstrapStage::cancel(const NetchanBootstrapTimePoint now)
{
    if (trace_callback_active_ || state_ == NetchanBootstrapState::idle ||
        terminal()) {
        return;
    }
    if (driver_) {
        driver_->cancel(now);
        synchronize_from_driver(now);
    } else {
        state_ = NetchanBootstrapState::cancelled;
        emit_trace(
            NetchanBootstrapTraceClassification::bootstrap_cancelled,
            now,
            remote_endpoint_);
    }
}

NetchanBootstrapState NetchanBootstrapStage::state() const noexcept
{
    return state_;
}

bool NetchanBootstrapStage::terminal() const noexcept
{
    return terminal_state(state_);
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

const std::optional<NetchanBootstrapResult>&
NetchanBootstrapStage::result() const noexcept
{
    return result_;
}

const std::optional<NetchanBootstrapError>&
NetchanBootstrapStage::error() const noexcept
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
    return driver_ ? driver_->transmitted_packet_count() : 0U;
}

const NetchanSession& NetchanBootstrapStage::session() const noexcept
{
    return driver_ ? driver_->session() : idle_session_;
}

NetchanSession* NetchanBootstrapStage::persistent_session() noexcept
{
    return state_ == NetchanBootstrapState::complete && driver_
               ? &driver_->compatibility_session()
               : nullptr;
}

const NetchanSession* NetchanBootstrapStage::persistent_session() const noexcept
{
    return state_ == NetchanBootstrapState::complete && driver_
               ? &driver_->session()
               : nullptr;
}

NetchanDriverConfig NetchanBootstrapStage::driver_config() const noexcept
{
    NetchanDriverConfig driver;
    driver.channel_inactivity_timeout = config_.first_packet_timeout;
    driver.fragment_transfer_timeout = config_.fragment_transfer_timeout;
    driver.maximum_datagram_size = config_.maximum_datagram_size;
    driver.maximum_fragment_datagram_size =
        config_.maximum_fragment_datagram_size;
    driver.maximum_fragment_payload_size = config_.maximum_fragment_payload_size;
    driver.maximum_normal_transfer_size = config_.maximum_normal_transfer_size;
    driver.maximum_fragments_per_transfer =
        config_.maximum_fragments_per_transfer;
    driver.maximum_active_normal_transfers =
        config_.maximum_active_normal_transfers;
    driver.maximum_fragment_ranges = config_.maximum_fragment_ranges;
    driver.maximum_opaque_payload_size = config_.maximum_opaque_payload_size;
    driver.maximum_unreliable_payload_size =
        config_.maximum_datagram_size > kNetchanHeaderSize
            ? config_.maximum_datagram_size - kNetchanHeaderSize
            : 0U;
    driver.maximum_datagrams_per_update = config_.maximum_datagrams_per_update;
    driver.maximum_outgoing_packets_per_update =
        config_.maximum_outgoing_packets_per_update;
    driver.maximum_events = config_.maximum_events;
    driver.yield_after_owning_payload = true;
    return driver;
}

void NetchanBootstrapStage::synchronize_from_driver(
    const NetchanBootstrapTimePoint now)
{
    if (!driver_) {
        return;
    }

    bool reassembled_payload_pending = false;
    while (auto event = driver_->poll_event()) {
        if (event->type == NetchanDriverEventType::normal_transfer_completed) {
            reassembled_payload_pending = true;
            continue;
        }
        if (event->type != NetchanDriverEventType::payload_ready ||
            !event->payload) {
            continue;
        }
        if (event->payload->sequence_flags.fragmented &&
            !reassembled_payload_pending) {
            // A contemporaneous suffix is a separate owning driver event. The
            // compatibility bootstrap waits for the complete normal transfer.
            continue;
        }

        const auto payload_size = event->payload->bytes.size();
        NetchanHeader header{
            NetchanSequenceWord{
                event->payload->source_sequence,
                event->payload->sequence_flags,
            },
            NetchanAcknowledgementWord{
                event->payload->source_acknowledgement,
                event->payload->acknowledgement_reliable,
            },
        };
        result_ = NetchanBootstrapResult{std::move(*event->payload)};
        emit_trace(
            NetchanBootstrapTraceClassification::payload_ready,
            now,
            remote_endpoint_,
            0U,
            &header,
            payload_size);
        driver_->close(now);
        state_ = NetchanBootstrapState::complete;
        emit_trace(
            NetchanBootstrapTraceClassification::bootstrap_complete,
            now,
            remote_endpoint_,
            0U,
            &header,
            payload_size);
        return;
    }

    if (driver_->terminal()) {
        fail_from_driver(now);
    }
}

void NetchanBootstrapStage::fail_from_driver(const NetchanBootstrapTimePoint now)
{
    if (!driver_ || state_ == NetchanBootstrapState::complete) {
        return;
    }
    state_ = map_driver_state(driver_->state());
    if (const auto& driver_error = driver_->last_error()) {
        error_ = NetchanBootstrapError{
            map_error_code(driver_error->code),
            driver_error->packet_code,
            driver_error->session_code,
            driver_error->context,
            driver_error->reassembly_code,
        };
    }
    static_cast<void>(now);
}

void NetchanBootstrapStage::handle_driver_trace(
    const NetchanDriverTraceEvent& driver_event)
{
    const auto classification =
        map_trace_classification(driver_event.classification);
    if (!classification || !trace_callback_ || trace_callback_active_) {
        return;
    }

    NetchanBootstrapTraceEvent event;
    event.classification = *classification;
    event.state = map_driver_state(driver_event.state);
    event.elapsed = elapsed(last_update_.value_or(
        started_at_.value_or(NetchanBootstrapTimePoint{})));
    event.endpoint = driver_event.endpoint;
    event.datagram_size = driver_event.datagram_size;
    event.payload_size = driver_event.payload_size;
    event.sequence = driver_event.sequence;
    event.acknowledgement = driver_event.acknowledgement;
    event.reliable = driver_event.reliable;
    event.fragmented = driver_event.fragmented;
    event.reliable_acknowledgement = driver_event.reliable_acknowledgement;
    event.fragment_stream = driver_event.fragment_stream;
    event.local_transfer_id = driver_event.local_transfer_id;
    event.fragment_offset = driver_event.fragment_offset;
    event.fragment_length = driver_event.fragment_length;
    event.covered_size = driver_event.covered_size;
    event.transfer_size = driver_event.transfer_size;
    event.transmitted_packet_count = driver_event.transmitted_packet_count;

    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
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
    event.transmitted_packet_count = transmitted_packet_count();
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
    if (!started_at_ || now < *started_at_) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - *started_at_);
}

} // namespace hlclient::goldsrc
