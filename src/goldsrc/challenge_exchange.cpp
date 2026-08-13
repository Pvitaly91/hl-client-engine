#include <hlclient/goldsrc/challenge_exchange.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] std::string bounded_text(std::string text)
{
    if (text.size() > kChallengeDiagnosticTextLimit) {
        text.resize(kChallengeDiagnosticTextLimit);
    }
    return text;
}

[[nodiscard]] bool is_terminal(const ChallengeExchangeState state) noexcept
{
    switch (state) {
    case ChallengeExchangeState::challenge_received:
    case ChallengeExchangeState::timed_out:
    case ChallengeExchangeState::cancelled:
    case ChallengeExchangeState::network_error:
    case ChallengeExchangeState::protocol_error:
        return true;
    case ChallengeExchangeState::idle:
    case ChallengeExchangeState::sending_request:
    case ChallengeExchangeState::waiting_for_response:
        return false;
    }
    return true;
}

[[nodiscard]] bool can_add(
    const ChallengeExchangeTimePoint time,
    const std::chrono::milliseconds duration) noexcept
{
    return duration.count() >= 0 &&
           time <= ChallengeExchangeTimePoint::max() - duration;
}

} // namespace

std::string escape_challenge_datagram_preview(const std::span<const std::byte> datagram)
{
    const auto preview_size = std::min(datagram.size(), kChallengeTracePreviewByteLimit);
    std::string result;
    result.reserve(preview_size * 4U + (datagram.size() > preview_size ? 3U : 0U));

    for (std::size_t index = 0; index < preview_size; ++index) {
        const auto value = std::to_integer<unsigned char>(datagram[index]);
        if (value >= 0x20U && value <= 0x7eU && value != '\\') {
            result.push_back(static_cast<char>(value));
            continue;
        }
        if (value == '\\') {
            result += "\\\\";
            continue;
        }

        std::array<char, 5U> escaped{};
        static_cast<void>(std::snprintf(
            escaped.data(), escaped.size(), "\\x%02X", static_cast<unsigned int>(value)));
        result += escaped.data();
    }

    if (datagram.size() > preview_size) {
        result += "...";
    }
    return result;
}

ChallengeExchange::ChallengeExchange(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    ChallengeExchangeConfig config,
    ChallengeTraceCallback trace_callback)
    : transport_{transport},
      remote_endpoint_{remote_endpoint},
      config_{config},
      trace_callback_{std::move(trace_callback)}
{
}

bool ChallengeExchange::start(const ChallengeExchangeTimePoint now)
{
    if (trace_callback_active_ || state_ != ChallengeExchangeState::idle) {
        return false;
    }

    started_at_ = now;
    last_update_ = now;
    emit_trace(ChallengeTraceClassification::exchange_started, now, remote_endpoint_);
    if (!validate_start(now)) {
        return false;
    }

    deadline_ = now + config_.timeout;
    return send_request(now);
}

void ChallengeExchange::update(const ChallengeExchangeTimePoint now)
{
    if (trace_callback_active_ || state_ != ChallengeExchangeState::waiting_for_response) {
        return;
    }
    if (last_update_ && now < *last_update_) {
        fail(
            ChallengeExchangeState::protocol_error,
            ChallengeExchangeErrorCategory::invalid_configuration,
            std::nullopt,
            "Challenge exchange time moved backwards",
            now,
            ChallengeTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            {});
        return;
    }
    last_update_ = now;

    // A datagram observed at the deadline is late, even if the socket queue already
    // contains it. Check time before polling so injected/manual time is authoritative.
    if (deadline_ && now >= *deadline_) {
        state_ = ChallengeExchangeState::timed_out;
        next_retry_.reset();
        emit_trace(
            ChallengeTraceClassification::exchange_timed_out,
            now,
            remote_endpoint_,
            0U,
            {},
            "Challenge exchange deadline elapsed");
        return;
    }

    for (std::size_t processed = 0; processed < config_.maximum_datagrams_per_update; ++processed) {
        auto received = transport_.receive(config_.maximum_datagram_size);
        if (received.status == network::DatagramTransportReceiveStatus::would_block) {
            emit_trace(ChallengeTraceClassification::receive_would_block, now, remote_endpoint_);
            break;
        }
        if (!process_received(received, now) || is_terminal(state_)) {
            return;
        }
    }

    if (next_retry_ && now >= *next_retry_ && attempts_ < config_.maximum_attempts) {
        static_cast<void>(send_request(now));
    }
}

void ChallengeExchange::cancel(const ChallengeExchangeTimePoint now)
{
    if (trace_callback_active_ || state_ == ChallengeExchangeState::idle || is_terminal(state_)) {
        return;
    }
    state_ = ChallengeExchangeState::cancelled;
    next_retry_.reset();
    emit_trace(ChallengeTraceClassification::exchange_cancelled, now, remote_endpoint_);
}

ChallengeExchangeState ChallengeExchange::state() const noexcept
{
    return state_;
}

const network::NetworkAddress& ChallengeExchange::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}

const std::optional<network::NetworkAddress>& ChallengeExchange::local_endpoint() const noexcept
{
    return local_endpoint_;
}

std::uint32_t ChallengeExchange::attempts() const noexcept
{
    return attempts_;
}

const std::optional<ChallengeResponse>& ChallengeExchange::challenge() const noexcept
{
    return challenge_;
}

const std::optional<ChallengeExchangeError>& ChallengeExchange::error() const noexcept
{
    return error_;
}

const std::optional<ChallengeExchangeTimePoint>& ChallengeExchange::next_retry() const noexcept
{
    return next_retry_;
}

const std::optional<ChallengeExchangeTimePoint>& ChallengeExchange::deadline() const noexcept
{
    return deadline_;
}

bool ChallengeExchange::validate_start(const ChallengeExchangeTimePoint now)
{
    const bool valid_durations = config_.retry_interval.count() > 0 &&
                                 config_.retry_interval <= kMaximumChallengeRetryInterval &&
                                 config_.timeout.count() > 0 &&
                                 config_.timeout <= kMaximumChallengeTimeout &&
                                 config_.retry_interval <= config_.timeout;
    const bool valid_limits = config_.maximum_attempts > 0U &&
                              config_.maximum_attempts <= kMaximumChallengeAttempts &&
                              config_.maximum_datagrams_per_update > 0U &&
                              config_.maximum_datagrams_per_update <=
                                  kMaximumChallengeDatagramsPerUpdate &&
                              config_.maximum_datagram_size > 0U &&
                              config_.maximum_datagram_size <=
                                  kMaximumConnectionlessChallengeDatagramSize;
    const bool safe_deadline = can_add(now, config_.timeout);
    if (!valid_durations || !valid_limits || !safe_deadline ||
        remote_endpoint_.ipv4_host_order() == 0U || remote_endpoint_.port() == 0U) {
        fail(
            ChallengeExchangeState::protocol_error,
            ChallengeExchangeErrorCategory::invalid_configuration,
            std::nullopt,
            "Invalid challenge exchange configuration or remote endpoint",
            now,
            ChallengeTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            {});
        return false;
    }

    auto local = transport_.local_address();
    if (!local) {
        fail(
            ChallengeExchangeState::network_error,
            ChallengeExchangeErrorCategory::network,
            std::nullopt,
            local.error.empty() ? "Unable to query the local datagram endpoint" : local.error,
            now,
            ChallengeTraceClassification::network_failure,
            remote_endpoint_,
            0U,
            {});
        return false;
    }
    local_endpoint_ = *local.address;
    if (local_endpoint_->port() == 0U) {
        fail(
            ChallengeExchangeState::network_error,
            ChallengeExchangeErrorCategory::network,
            std::nullopt,
            "Datagram transport has no bound local endpoint",
            now,
            ChallengeTraceClassification::network_failure,
            remote_endpoint_,
            0U,
            {});
        return false;
    }
    return true;
}

bool ChallengeExchange::send_request(const ChallengeExchangeTimePoint now)
{
    state_ = ChallengeExchangeState::sending_request;
    emit_trace(ChallengeTraceClassification::request_send_started, now, remote_endpoint_);

    auto request = build_getchallenge_request();
    if (!request) {
        const auto protocol_code = request.error
                                       ? std::optional{request.error->code}
                                       : std::nullopt;
        fail(
            ChallengeExchangeState::protocol_error,
            ChallengeExchangeErrorCategory::protocol,
            protocol_code,
            request.error ? request.error->context : "Unable to encode getchallenge request",
            now,
            ChallengeTraceClassification::protocol_failure,
            remote_endpoint_,
            0U,
            {});
        return false;
    }

    ++attempts_;
    auto sent = transport_.send_to(remote_endpoint_, *request.datagram);
    if (!sent) {
        fail(
            ChallengeExchangeState::network_error,
            ChallengeExchangeErrorCategory::network,
            std::nullopt,
            sent.error.empty() ? "Unable to send getchallenge request" : sent.error,
            now,
            ChallengeTraceClassification::network_failure,
            remote_endpoint_,
            request.datagram->size(),
            *request.datagram);
        return false;
    }

    state_ = ChallengeExchangeState::waiting_for_response;
    if (attempts_ < config_.maximum_attempts && can_add(now, config_.retry_interval)) {
        next_retry_ = now + config_.retry_interval;
    } else {
        next_retry_.reset();
    }
    emit_trace(
        ChallengeTraceClassification::request_sent,
        now,
        remote_endpoint_,
        request.datagram->size(),
        *request.datagram);
    return true;
}

bool ChallengeExchange::process_received(
    const network::DatagramTransportReceiveResult& received,
    const ChallengeExchangeTimePoint now)
{
    if (received.status == network::DatagramTransportReceiveStatus::error) {
        fail(
            ChallengeExchangeState::network_error,
            ChallengeExchangeErrorCategory::network,
            std::nullopt,
            received.error.empty() ? "Datagram receive failed" : received.error,
            now,
            ChallengeTraceClassification::network_failure,
            received.source.value_or(remote_endpoint_),
            received.payload_size,
            {});
        return false;
    }

    if (!received.source && !received.datagram) {
        fail(
            ChallengeExchangeState::network_error,
            ChallengeExchangeErrorCategory::network,
            std::nullopt,
            "Datagram transport did not report a source endpoint",
            now,
            ChallengeTraceClassification::network_failure,
            remote_endpoint_,
            received.payload_size,
            {});
        return false;
    }

    auto source = received.datagram ? received.datagram->source : *received.source;
    if (received.source) {
        source = *received.source;
    } else if (received.datagram) {
        source = received.datagram->source;
    }
    if (received.source && *received.source != remote_endpoint_) {
        emit_trace(
            ChallengeTraceClassification::wrong_endpoint_ignored,
            now,
            *received.source,
            received.payload_size,
            received.datagram ? std::span<const std::byte>{received.datagram->payload}
                              : std::span<const std::byte>{});
        return true;
    }
    if (received.datagram && received.datagram->source != remote_endpoint_) {
        emit_trace(
            ChallengeTraceClassification::wrong_endpoint_ignored,
            now,
            received.datagram->source,
            received.datagram->payload.size(),
            received.datagram->payload);
        return true;
    }

    if (received.status == network::DatagramTransportReceiveStatus::truncated) {
        fail(
            ChallengeExchangeState::protocol_error,
            ChallengeExchangeErrorCategory::protocol,
            ChallengeProtocolErrorCode::payload_too_large,
            received.error.empty() ? "Challenge response exceeds the configured size limit"
                                   : received.error,
            now,
            ChallengeTraceClassification::response_truncated,
            source,
            received.payload_size,
            {});
        return false;
    }
    if (received.status != network::DatagramTransportReceiveStatus::received ||
        !received.datagram) {
        fail(
            ChallengeExchangeState::network_error,
            ChallengeExchangeErrorCategory::network,
            std::nullopt,
            "Datagram transport returned an inconsistent receive result",
            now,
            ChallengeTraceClassification::network_failure,
            source,
            received.payload_size,
            {});
        return false;
    }

    auto parsed = parse_challenge_response(received.datagram->payload);
    if (!parsed) {
        const auto protocol_code = parsed.error
                                       ? std::optional{parsed.error->code}
                                       : std::nullopt;
        fail(
            ChallengeExchangeState::protocol_error,
            ChallengeExchangeErrorCategory::protocol,
            protocol_code,
            parsed.error ? parsed.error->context : "Challenge parser returned no result",
            now,
            ChallengeTraceClassification::response_rejected,
            source,
            received.datagram->payload.size(),
            received.datagram->payload);
        return false;
    }

    challenge_ = std::move(*parsed.response);
    state_ = ChallengeExchangeState::challenge_received;
    next_retry_.reset();
    emit_trace(
        ChallengeTraceClassification::challenge_accepted,
        now,
        source,
        received.datagram->payload.size(),
        received.datagram->payload);
    return false;
}

void ChallengeExchange::fail(
    const ChallengeExchangeState state,
    const ChallengeExchangeErrorCategory category,
    const std::optional<ChallengeProtocolErrorCode> protocol_code,
    std::string context,
    const ChallengeExchangeTimePoint now,
    const ChallengeTraceClassification classification,
    const network::NetworkAddress endpoint,
    const std::size_t datagram_size,
    const std::span<const std::byte> preview)
{
    state_ = state;
    next_retry_.reset();
    context = bounded_text(std::move(context));
    error_ = ChallengeExchangeError{category, protocol_code, context};
    emit_trace(classification, now, endpoint, datagram_size, preview, std::move(context));
}

void ChallengeExchange::emit_trace(
    const ChallengeTraceClassification classification,
    const ChallengeExchangeTimePoint now,
    const network::NetworkAddress endpoint,
    const std::size_t datagram_size,
    const std::span<const std::byte> preview,
    std::string context)
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }

    const ChallengeTraceEvent event{
        classification,
        state_,
        elapsed(now),
        endpoint,
        datagram_size,
        attempts_,
        escape_challenge_datagram_preview(preview),
        bounded_text(std::move(context)),
    };

    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
        // Tracing is best-effort diagnostics and must not corrupt protocol state.
    }
    trace_callback_active_ = false;
}

std::chrono::milliseconds ChallengeExchange::elapsed(
    const ChallengeExchangeTimePoint now) const noexcept
{
    if (!started_at_ || now <= *started_at_) {
        return std::chrono::milliseconds{0};
    }
    if (deadline_ && now >= *deadline_) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(*deadline_ - *started_at_);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *started_at_);
}

} // namespace hlclient::goldsrc
