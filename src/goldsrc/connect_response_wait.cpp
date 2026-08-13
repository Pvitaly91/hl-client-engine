#include <hlclient/goldsrc/connect_response_wait.hpp>

#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <variant>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool is_terminal(const ConnectResponseWaitState state) noexcept
{
    switch (state) {
    case ConnectResponseWaitState::accepted:
    case ConnectResponseWaitState::rejected:
    case ConnectResponseWaitState::timed_out:
    case ConnectResponseWaitState::cancelled:
    case ConnectResponseWaitState::network_error:
    case ConnectResponseWaitState::protocol_error:
        return true;
    case ConnectResponseWaitState::idle:
    case ConnectResponseWaitState::waiting:
        return false;
    }
    return true;
}

[[nodiscard]] bool can_add(
    const ConnectResponseWaitTimePoint time,
    const std::chrono::milliseconds duration) noexcept
{
    return duration.count() >= 0 && time <= ConnectResponseWaitTimePoint::max() - duration;
}

[[nodiscard]] std::string bounded_text(std::string text)
{
    if (text.size() > kConnectResponseWaitDiagnosticTextLimit) {
        text.resize(kConnectResponseWaitDiagnosticTextLimit);
    }
    return text;
}

[[nodiscard]] bool has_connectionless_header(const std::span<const std::byte> bytes) noexcept
{
    constexpr std::array header{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    return bytes.size() >= header.size() &&
           std::equal(header.begin(), header.end(), bytes.begin());
}

[[nodiscard]] bool is_connect_response_type(const std::byte type) noexcept
{
    return type == kConnectAcceptedResponseClass || type == kConnectRejectedResponseClass;
}

} // namespace

ConnectResponseWaitStage::ConnectResponseWaitStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    const ConnectResponseWaitConfig config,
    ConnectResponseTraceCallback trace_callback)
    : transport_{transport},
      remote_endpoint_{remote_endpoint},
      config_{config},
      trace_callback_{std::move(trace_callback)}
{
}

bool ConnectResponseWaitStage::start(
    const ConnectResponseWaitTimePoint now,
    const network::NetworkAddress& expected_local_endpoint)
{
    if (trace_callback_active_ || state_ != ConnectResponseWaitState::idle) {
        return false;
    }

    started_at_ = now;
    last_update_ = now;
    if (!validate_start(now, expected_local_endpoint)) {
        return false;
    }

    deadline_ = now + config_.timeout;
    state_ = ConnectResponseWaitState::waiting;
    emit_trace(
        ConnectResponseTraceClassification::wait_started,
        now,
        remote_endpoint_);
    return true;
}

void ConnectResponseWaitStage::update(const ConnectResponseWaitTimePoint now)
{
    if (trace_callback_active_ || state_ != ConnectResponseWaitState::waiting) {
        return;
    }
    if (last_update_ && now < *last_update_) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::time_moved_backwards,
            std::nullopt,
            "Connect-response wait time moved backwards",
            now,
            ConnectResponseTraceClassification::protocol_failure,
            remote_endpoint_,
            0U);
        return;
    }
    last_update_ = now;

    // A queued datagram observed exactly at the deadline is late. Time is
    // checked before the socket so deterministic/manual clocks are authoritative.
    if (deadline_ && now >= *deadline_) {
        state_ = ConnectResponseWaitState::timed_out;
        emit_trace(
            ConnectResponseTraceClassification::wait_timed_out,
            now,
            remote_endpoint_);
        return;
    }

    for (std::size_t processed = 0U;
         processed < config_.maximum_datagrams_per_update;
         ++processed) {
        const auto received = transport_.receive(config_.maximum_datagram_size);
        if (received.status == network::DatagramTransportReceiveStatus::would_block) {
            if (received.datagram || received.source || received.payload_size != 0U ||
                !received.error.empty()) {
                fail(
                    ConnectResponseWaitState::network_error,
                    ConnectResponseWaitErrorCode::inconsistent_receive_result,
                    std::nullopt,
                    "Would-block receive result carried datagram metadata",
                    now,
                    ConnectResponseTraceClassification::network_failure,
                    remote_endpoint_,
                    received.payload_size);
                return;
            }
            emit_trace(
                ConnectResponseTraceClassification::receive_would_block,
                now,
                remote_endpoint_);
            return;
        }
        if (!process_received(received, now) || is_terminal(state_)) {
            return;
        }
    }
}

void ConnectResponseWaitStage::cancel(const ConnectResponseWaitTimePoint now)
{
    if (trace_callback_active_ || state_ != ConnectResponseWaitState::waiting) {
        return;
    }
    state_ = ConnectResponseWaitState::cancelled;
    emit_trace(
        ConnectResponseTraceClassification::wait_cancelled,
        now,
        remote_endpoint_);
}

ConnectResponseWaitState ConnectResponseWaitStage::state() const noexcept { return state_; }

bool ConnectResponseWaitStage::terminal() const noexcept { return is_terminal(state_); }

const network::NetworkAddress& ConnectResponseWaitStage::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}

const std::optional<network::NetworkAddress>& ConnectResponseWaitStage::local_endpoint() const noexcept
{
    return local_endpoint_;
}

const std::optional<ConnectResponse>& ConnectResponseWaitStage::response() const noexcept
{
    return response_;
}

const std::optional<ConnectResponseWaitError>& ConnectResponseWaitStage::error() const noexcept
{
    return error_;
}

const std::optional<ConnectResponseWaitTimePoint>& ConnectResponseWaitStage::deadline() const noexcept
{
    return deadline_;
}

bool ConnectResponseWaitStage::validate_start(
    const ConnectResponseWaitTimePoint now,
    const network::NetworkAddress& expected_local_endpoint)
{
    const bool valid_timeout = config_.timeout.count() > 0 &&
                               config_.timeout <= kMaximumConnectResponseWaitTimeout;
    const bool valid_limits = config_.maximum_datagrams_per_update > 0U &&
                              config_.maximum_datagrams_per_update <=
                                  kMaximumConnectResponseDatagramsPerUpdate &&
                              config_.maximum_datagram_size > 0U &&
                              config_.maximum_datagram_size <=
                                  kMaximumConnectResponseWaitDatagramSize;
    const bool valid_endpoints = remote_endpoint_.ipv4_host_order() != 0U &&
                                 remote_endpoint_.port() != 0U &&
                                 expected_local_endpoint.port() != 0U;
    if (!valid_timeout || !valid_limits || !valid_endpoints ||
        !can_add(now, config_.timeout)) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::invalid_configuration,
            std::nullopt,
            "Invalid connect-response wait configuration or endpoint",
            now,
            ConnectResponseTraceClassification::protocol_failure,
            remote_endpoint_,
            0U);
        return false;
    }

    auto local = transport_.local_address();
    if (!local || !local.address) {
        fail(
            ConnectResponseWaitState::network_error,
            ConnectResponseWaitErrorCode::local_endpoint_unavailable,
            std::nullopt,
            local.error.empty() ? "Unable to query the local datagram endpoint"
                                : std::move(local.error),
            now,
            ConnectResponseTraceClassification::network_failure,
            remote_endpoint_,
            0U);
        return false;
    }
    if (*local.address != expected_local_endpoint) {
        fail(
            ConnectResponseWaitState::network_error,
            ConnectResponseWaitErrorCode::local_endpoint_changed,
            std::nullopt,
            "Datagram transport local endpoint changed before response wait",
            now,
            ConnectResponseTraceClassification::network_failure,
            remote_endpoint_,
            0U);
        return false;
    }

    local_endpoint_ = *local.address;
    return true;
}

bool ConnectResponseWaitStage::process_received(
    const network::DatagramTransportReceiveResult& received,
    const ConnectResponseWaitTimePoint now)
{
    if (received.source && received.datagram &&
        *received.source != received.datagram->source) {
        fail(
            ConnectResponseWaitState::network_error,
            ConnectResponseWaitErrorCode::inconsistent_receive_result,
            std::nullopt,
            "Datagram transport reported conflicting source endpoints",
            now,
            ConnectResponseTraceClassification::network_failure,
            *received.source,
            received.payload_size);
        return false;
    }

    if (received.status == network::DatagramTransportReceiveStatus::error) {
        if (received.datagram || received.source || received.payload_size != 0U) {
            fail(
                ConnectResponseWaitState::network_error,
                ConnectResponseWaitErrorCode::inconsistent_receive_result,
                std::nullopt,
                "Failed receive result carried datagram payload metadata",
                now,
                ConnectResponseTraceClassification::network_failure,
                received.source.value_or(remote_endpoint_),
                received.payload_size);
            return false;
        }
        fail(
            ConnectResponseWaitState::network_error,
            ConnectResponseWaitErrorCode::receive_failed,
            std::nullopt,
            received.error.empty() ? "Datagram receive failed" : received.error,
            now,
            ConnectResponseTraceClassification::network_failure,
            received.source.value_or(remote_endpoint_),
            received.payload_size);
        return false;
    }

    if (!received.source) {
        fail(
            ConnectResponseWaitState::network_error,
            ConnectResponseWaitErrorCode::inconsistent_receive_result,
            std::nullopt,
            "Datagram transport omitted the response source endpoint",
            now,
            ConnectResponseTraceClassification::network_failure,
            remote_endpoint_,
            received.payload_size);
        return false;
    }
    const auto source = *received.source;

    if (received.status == network::DatagramTransportReceiveStatus::received) {
        if (!received.datagram || received.payload_size != received.datagram->payload.size() ||
            !received.error.empty()) {
            fail(
                ConnectResponseWaitState::network_error,
                ConnectResponseWaitErrorCode::inconsistent_receive_result,
                std::nullopt,
                "Datagram transport returned inconsistent payload metadata",
                now,
                ConnectResponseTraceClassification::network_failure,
                source,
                received.payload_size);
            return false;
        }
    } else if (received.status == network::DatagramTransportReceiveStatus::truncated) {
        if (received.datagram) {
            fail(
                ConnectResponseWaitState::network_error,
                ConnectResponseWaitErrorCode::inconsistent_receive_result,
                std::nullopt,
                "Truncated receive result unexpectedly contained a datagram",
                now,
                ConnectResponseTraceClassification::network_failure,
                source,
                received.payload_size);
            return false;
        }
    } else {
        fail(
            ConnectResponseWaitState::network_error,
            ConnectResponseWaitErrorCode::inconsistent_receive_result,
            std::nullopt,
            "Datagram transport returned an inconsistent receive status",
            now,
            ConnectResponseTraceClassification::network_failure,
            source,
            received.payload_size);
        return false;
    }

    // Source validation deliberately precedes truncation handling. Oversized
    // traffic from a spoofed endpoint cannot terminate the real exchange.
    if (source != remote_endpoint_) {
        emit_trace(
            ConnectResponseTraceClassification::wrong_endpoint_ignored,
            now,
            source,
            received.payload_size);
        return true;
    }

    if (received.status == network::DatagramTransportReceiveStatus::truncated) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::response_truncated,
            std::nullopt,
            "Connect response exceeds the configured datagram limit",
            now,
            ConnectResponseTraceClassification::response_truncated,
            source,
            received.payload_size);
        return false;
    }

    const auto& payload = received.datagram->payload;
    if (payload.size() > config_.maximum_datagram_size) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::response_truncated,
            std::nullopt,
            "Connect response exceeds the configured datagram limit",
            now,
            ConnectResponseTraceClassification::response_truncated,
            source,
            payload.size());
        return false;
    }

    if (payload.size() < kConnectionlessPacketHeaderSize) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::malformed_connect_response,
            ConnectResponseErrorCode::packet_too_short,
            "Connect response is shorter than the connectionless header",
            now,
            ConnectResponseTraceClassification::protocol_failure,
            source,
            payload.size());
        return false;
    }
    if (!has_connectionless_header(payload)) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::unexpected_sequenced_packet_pending_m2_3,
            std::nullopt,
            "Received a sequenced or nonconnectionless packet before M2.3",
            now,
            ConnectResponseTraceClassification::unexpected_sequenced_packet_pending_m2_3,
            source,
            payload.size());
        return false;
    }
    if (payload.size() == 4U) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::malformed_connect_response,
            std::nullopt,
            "Connectionless response omitted its type byte",
            now,
            ConnectResponseTraceClassification::protocol_failure,
            source,
            payload.size());
        return false;
    }
    if (!is_connect_response_type(payload[4])) {
        emit_trace(
            ConnectResponseTraceClassification::unrelated_connectionless_ignored,
            now,
            source,
            payload.size());
        return true;
    }

    auto parsed = parse_connect_response(payload);
    if (!parsed) {
        fail(
            ConnectResponseWaitState::protocol_error,
            ConnectResponseWaitErrorCode::malformed_connect_response,
            parsed.error ? std::optional{parsed.error->code} : std::nullopt,
            parsed.error ? parsed.error->context : "Unable to parse connect response",
            now,
            ConnectResponseTraceClassification::protocol_failure,
            source,
            payload.size());
        return false;
    }

    response_ = std::move(*parsed.response);
    if (std::holds_alternative<ConnectAccepted>(*response_)) {
        state_ = ConnectResponseWaitState::accepted;
        emit_trace(
            ConnectResponseTraceClassification::connect_accepted,
            now,
            source,
            payload.size());
        return false;
    }
    if (std::holds_alternative<ConnectRejected>(*response_)) {
        state_ = ConnectResponseWaitState::rejected;
        emit_trace(
            ConnectResponseTraceClassification::connect_rejected,
            now,
            source,
            payload.size());
        return false;
    }

    fail(
        ConnectResponseWaitState::protocol_error,
        ConnectResponseWaitErrorCode::malformed_connect_response,
        std::nullopt,
        "Connect-response parser returned an unsupported result",
        now,
        ConnectResponseTraceClassification::protocol_failure,
        source,
        payload.size());
    return false;
}

void ConnectResponseWaitStage::fail(
    const ConnectResponseWaitState state,
    const ConnectResponseWaitErrorCode code,
    const std::optional<ConnectResponseErrorCode> protocol_code,
    std::string context,
    const ConnectResponseWaitTimePoint now,
    const ConnectResponseTraceClassification classification,
    const network::NetworkAddress endpoint,
    const std::size_t datagram_size)
{
    state_ = state;
    error_ = ConnectResponseWaitError{
        code,
        protocol_code,
        bounded_text(std::move(context)),
    };
    emit_trace(classification, now, endpoint, datagram_size);
}

void ConnectResponseWaitStage::emit_trace(
    const ConnectResponseTraceClassification classification,
    const ConnectResponseWaitTimePoint now,
    const network::NetworkAddress endpoint,
    const std::size_t datagram_size)
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }

    const ConnectResponseTraceEvent event{
        classification,
        state_,
        elapsed(now),
        endpoint,
        datagram_size,
    };
    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

std::chrono::milliseconds ConnectResponseWaitStage::elapsed(
    const ConnectResponseWaitTimePoint now) const noexcept
{
    if (!started_at_ || now <= *started_at_) {
        return std::chrono::milliseconds{0};
    }
    if (deadline_ && now >= *deadline_) {
        return config_.timeout;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *started_at_);
}

} // namespace hlclient::goldsrc
