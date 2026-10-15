#include <hlclient/app/session_receipt.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <span>
#include <utility>

namespace hlclient::app {
namespace {

constexpr std::string_view absent = "not_observed";
constexpr std::string_view hex_digits = "0123456789abcdef";

[[nodiscard]] bool valid_identity(const SessionReceiptIdentity& identity) noexcept
{
    return identity.run_id.size() == 32U &&
        std::ranges::all_of(identity.run_id, [](const char value) {
            return hex_digits.find(value) != std::string_view::npos;
        }) && (identity.role == SessionReceiptRole::A ||
               identity.role == SessionReceiptRole::B) &&
        identity.process_id != 0U && identity.process_creation != 0U;
}

[[nodiscard]] bool valid_map(const std::string_view map) noexcept
{
    if (!map.starts_with("maps/") || !map.ends_with(".bsp") ||
        map.size() <= 9U || map.size() > 73U) return false;
    const auto basename = map.substr(5U, map.size() - 9U);
    return std::ranges::all_of(basename, [](const char value) {
        return (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-';
    });
}

[[nodiscard]] bool valid_generation(
    const SessionReceiptGeneration& generation) noexcept
{
    if (generation.network_generation == 0U)
        return generation.map_generation == 0U && generation.map_identifier == absent;
    if (generation.map_generation == 0U)
        return generation.map_identifier == absent;
    return valid_map(generation.map_identifier);
}

[[nodiscard]] bool has(const SessionReceiptSnapshot& snapshot,
                       const std::uint64_t mask) noexcept
{
    return (snapshot.fact_mask & mask) == mask;
}

[[nodiscard]] std::size_t stage_ordinal(const SessionReceiptStage stage) noexcept
{
    return static_cast<std::size_t>(stage);
}

[[nodiscard]] bool valid_close(const SessionCloseReceipt& close) noexcept
{
    if (to_string(close.reason) == "invalid" ||
        to_string(close.outcome) == "invalid" ||
        to_string(close.transport_error) == "invalid" ||
        close.send_attempt_count > 3U || close.support_send_attempt_count > 3U ||
        close.successful_send_count > close.send_attempt_count ||
        close.successful_support_send_count > close.support_send_attempt_count ||
        (close.queued && (!close.requested || !close.eligible)) ||
        (close.send_attempt_count != 0U && !close.queued) ||
        (close.successful_send_count != 0U &&
         (!close.first_send_process_ms || !close.last_send_process_ms)) ||
        (close.ack_observed && close.successful_send_count == 0U) ||
        (close.requested && (close.operation_identity == 0U ||
                             !close.requested_process_ms)) ||
        (!close.requested && (close.queued || close.send_attempt_count != 0U ||
                              close.requested_process_ms.has_value()))) return false;
    if (close.outcome == SessionCloseOutcome::sent_but_unconfirmed &&
        (close.successful_send_count == 0U || close.ack_observed ||
         close.server_disconnect_observed)) return false;
    if (close.outcome == SessionCloseOutcome::transport_acknowledged &&
        !close.ack_observed) return false;
    if (close.outcome == SessionCloseOutcome::server_disconnect_confirmed &&
        !close.server_disconnect_observed) return false;
    if (close.requested_process_ms) {
        for (const auto time : {close.deadline_process_ms, close.first_send_process_ms,
                 close.last_send_process_ms, close.transport_closed_process_ms}) {
            if (time && *time < *close.requested_process_ms) return false;
        }
    }
    if (close.first_send_process_ms && close.last_send_process_ms &&
        *close.last_send_process_ms < *close.first_send_process_ms) return false;
    if (close.requested_host_qpc_ticks && close.transport_closed_host_qpc_ticks &&
        *close.transport_closed_host_qpc_ticks < *close.requested_host_qpc_ticks)
        return false;
    return true;
}

[[nodiscard]] bool stage_ready(const SessionReceiptSnapshot& snapshot,
                               const SessionReceiptStage stage) noexcept
{
    switch (stage) {
    case SessionReceiptStage::generation_started: return true;
    case SessionReceiptStage::connection_established:
        return has(snapshot, kSessionReceiptConnectionFacts);
    case SessionReceiptStage::signon_complete:
        return has(snapshot, kSessionReceiptSignonFacts);
    case SessionReceiptStage::map_entry_committed:
    case SessionReceiptStage::world_ready:
        return has(snapshot, receipt_fact(SessionReceiptFact::world_published));
    case SessionReceiptStage::first_gameplay_update:
        return has(snapshot, receipt_fact(SessionReceiptFact::gameplay_committed)) &&
            has(snapshot, receipt_fact(SessionReceiptFact::usercmd_transmitted)) &&
            snapshot.counters.new_usercmd_count.value_or(0U) > 0U &&
            snapshot.counters.transmitted_usercmd_packet_count.value_or(0U) > 0U &&
            snapshot.counters.server_sample_count.value_or(0U) > 0U;
    case SessionReceiptStage::terminal: return true;
    }
    return false;
}

void retain_counter(std::optional<std::uint64_t>& current,
                    const std::optional<std::uint64_t>& observed) noexcept
{
    if (observed && (!current || *observed > *current)) current = observed;
}

[[nodiscard]] std::string optional_number(
    const std::optional<std::uint64_t>& value)
{
    return value ? std::to_string(*value) : std::string{absent};
}

[[nodiscard]] bool number(const std::string_view text, std::uint64_t& value) noexcept
{
    if (text.empty() || (text.size() > 1U && text.front() == '0')) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] bool optional_number(const std::string_view text,
                                  std::optional<std::uint64_t>& value) noexcept
{
    if (text == absent) { value.reset(); return true; }
    std::uint64_t decoded{};
    if (!number(text, decoded)) return false;
    value = decoded;
    return true;
}

[[nodiscard]] bool boolean(const std::string_view text, bool& value) noexcept
{
    if (text != "0" && text != "1") return false;
    value = text == "1";
    return true;
}

class ReceiptReader final {
public:
    explicit ReceiptReader(const std::string_view text) : remaining_{text} {}
    [[nodiscard]] std::optional<std::string_view> take(const std::string_view key)
    {
        const auto newline = remaining_.find('\n');
        if (newline == std::string_view::npos) return std::nullopt;
        const auto line = remaining_.substr(0U, newline);
        if (!line.starts_with(key) || line.size() <= key.size() ||
            line[key.size()] != '=') return std::nullopt;
        remaining_.remove_prefix(newline + 1U);
        return line.substr(key.size() + 1U);
    }
    [[nodiscard]] bool done() const noexcept { return remaining_.empty(); }
private:
    std::string_view remaining_;
};

[[nodiscard]] std::string stamp_text(const std::optional<SessionReceiptStamp>& stamp)
{
    if (!stamp) return std::string{absent};
    return std::to_string(stamp->process_ms) + ":" +
        optional_number(stamp->source_sequence) + ":" +
        optional_number(stamp->publication_ordinal) + ":" +
        optional_number(stamp->host_qpc_ticks);
}

[[nodiscard]] bool stamp_value(const std::string_view text,
    std::optional<SessionReceiptStamp>& output) noexcept
{
    if (text == absent) { output.reset(); return true; }
    auto remaining = text;
    std::array<std::string_view, 4U> values{};
    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto colon = remaining.find(':');
        if (colon == std::string_view::npos) return false;
        values[index] = remaining.substr(0U, colon);
        remaining.remove_prefix(colon + 1U);
    }
    values[3U] = remaining;
    SessionReceiptStamp stamp;
    if (!number(values[0U], stamp.process_ms) ||
        !optional_number(values[1U], stamp.source_sequence) ||
        !optional_number(values[2U], stamp.publication_ordinal) ||
        !optional_number(values[3U], stamp.host_qpc_ticks)) return false;
    output = stamp;
    return true;
}

void append_hex_byte(std::string& output, const std::uint8_t value)
{
    output.push_back(hex_digits[value >> 4U]);
    output.push_back(hex_digits[value & 15U]);
}

void append_hex_uint64(std::string& output, const std::uint64_t value)
{
    for (std::size_t index = 0U; index < 8U; ++index)
        append_hex_byte(output, static_cast<std::uint8_t>(value >> (index * 8U)));
}

[[nodiscard]] std::string peer_text(const SessionReceiptSnapshot& snapshot)
{
    if (snapshot.peer_removal_count == 0U) return std::string{absent};
    std::string result;
    result.reserve(snapshot.peer_removal_count * 50U);
    for (std::size_t index = 0U; index < snapshot.peer_removal_count; ++index) {
        const auto& peer = snapshot.peer_removals[index];
        append_hex_byte(result, peer.entity);
        append_hex_uint64(result, peer.source_ordinal);
        append_hex_uint64(result, peer.host_qpc_ticks);
        append_hex_uint64(result, peer.process_ms);
    }
    return result;
}

[[nodiscard]] bool hex_byte(const std::string_view text, std::uint8_t& byte) noexcept
{
    if (text.size() != 2U) return false;
    const auto high = hex_digits.find(text[0U]);
    const auto low = hex_digits.find(text[1U]);
    if (high == std::string_view::npos || low == std::string_view::npos) return false;
    byte = static_cast<std::uint8_t>(high * 16U + low);
    return true;
}

[[nodiscard]] bool hex_uint64(const std::string_view text,
                              std::uint64_t& value) noexcept
{
    if (text.size() != 16U) return false;
    value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        std::uint8_t byte{};
        if (!hex_byte(text.substr(index * 2U, 2U), byte)) return false;
        value |= static_cast<std::uint64_t>(byte) << (index * 8U);
    }
    return true;
}

[[nodiscard]] bool peer_value(const std::string_view text,
                              SessionReceiptSnapshot& snapshot) noexcept
{
    if (text == absent) return true;
    if (text.empty() || text.size() % 50U != 0U ||
        text.size() / 50U > kSessionReceiptMaximumPeerRemovals) return false;
    snapshot.peer_removal_count = text.size() / 50U;
    for (std::size_t index = 0U; index < snapshot.peer_removal_count; ++index) {
        const auto part = text.substr(index * 50U, 50U);
        auto& peer = snapshot.peer_removals[index];
        if (!hex_byte(part.substr(0U, 2U), peer.entity) ||
            !hex_uint64(part.substr(2U, 16U), peer.source_ordinal) ||
            !hex_uint64(part.substr(18U, 16U), peer.host_qpc_ticks) ||
            !hex_uint64(part.substr(34U, 16U), peer.process_ms)) return false;
    }
    return true;
}

template<class Enum, std::size_t Size>
[[nodiscard]] bool enum_value(const std::string_view text, Enum& value,
                              const std::array<Enum, Size>& values) noexcept
{
    for (const auto candidate : values) {
        if (to_string(candidate) == text) { value = candidate; return true; }
    }
    return false;
}

} // namespace

std::string_view to_string(const SessionReceiptStage stage) noexcept
{
    switch (stage) {
    case SessionReceiptStage::generation_started: return "generation_started";
    case SessionReceiptStage::connection_established: return "connection_established";
    case SessionReceiptStage::signon_complete: return "signon_complete";
    case SessionReceiptStage::map_entry_committed: return "map_entry_committed";
    case SessionReceiptStage::world_ready: return "world_ready";
    case SessionReceiptStage::first_gameplay_update: return "first_gameplay_update";
    case SessionReceiptStage::terminal: return "terminal";
    }
    return "invalid";
}
std::string_view to_string(const SessionCloseReason reason) noexcept
{
    switch (reason) {
    case SessionCloseReason::not_observed: return "not_observed";
    case SessionCloseReason::window_close: return "window_close";
    case SessionCloseReason::duration_limit: return "duration_limit";
    case SessionCloseReason::server_disconnect: return "server_disconnect";
    case SessionCloseReason::startup_cancel: return "startup_cancel";
    case SessionCloseReason::fatal_failure: return "fatal_failure";
    case SessionCloseReason::runner_termination: return "runner_termination";
    }
    return "invalid";
}
std::string_view to_string(const SessionCloseOutcome outcome) noexcept
{
    switch (outcome) {
    case SessionCloseOutcome::not_requested: return "not_requested";
    case SessionCloseOutcome::pending: return "pending";
    case SessionCloseOutcome::cancel_without_protocol_disconnect:
        return "cancel_without_protocol_disconnect";
    case SessionCloseOutcome::sent_but_unconfirmed: return "sent_but_unconfirmed";
    case SessionCloseOutcome::transport_acknowledged: return "transport_acknowledged";
    case SessionCloseOutcome::server_disconnect_confirmed:
        return "server_disconnect_confirmed";
    case SessionCloseOutcome::send_failed: return "send_failed";
    case SessionCloseOutcome::cancelled: return "cancelled";
    case SessionCloseOutcome::fatal_failure: return "fatal_failure";
    }
    return "invalid";
}
std::string_view to_string(const SessionCloseTransportError error) noexcept
{
    switch (error) {
    case SessionCloseTransportError::not_observed: return "not_observed";
    case SessionCloseTransportError::would_block: return "would_block";
    case SessionCloseTransportError::send_error: return "send_error";
    case SessionCloseTransportError::poll_error: return "poll_error";
    case SessionCloseTransportError::invalid_state: return "invalid_state";
    case SessionCloseTransportError::cancelled: return "cancelled";
    case SessionCloseTransportError::transport_unavailable: return "transport_unavailable";
    }
    return "invalid";
}

std::optional<std::string> normalized_session_map_identifier(std::string_view input)
{
    if (input.starts_with("maps/")) input.remove_prefix(5U);
    if (input.ends_with(".bsp")) input.remove_suffix(4U);
    if (input.empty() || input.size() > 64U) return std::nullopt;
    std::string result{"maps/"};
    for (char value : input) {
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
        result.push_back(value);
    }
    result += ".bsp";
    return valid_map(result) ? std::optional<std::string>{std::move(result)} : std::nullopt;
}

bool valid_session_receipt(const SessionReceiptSnapshot& snapshot) noexcept
{
    if (!valid_identity(snapshot.identity) || !valid_generation(snapshot.generation) ||
        snapshot.generation_ordinal == 0U ||
        snapshot.generation_ordinal > kSessionReceiptMaximumGenerations ||
        stage_ordinal(snapshot.stage) > stage_ordinal(SessionReceiptStage::terminal) ||
        (snapshot.fact_mask & ~kSessionReceiptKnownFacts) != 0U ||
        (snapshot.receiving_entity &&
         (*snapshot.receiving_entity == 0U || *snapshot.receiving_entity > 32U ||
          snapshot.generation.network_generation == 0U)) ||
        (snapshot.host_qpc_frequency && *snapshot.host_qpc_frequency == 0U) ||
        snapshot.peer_removal_count > kSessionReceiptMaximumPeerRemovals ||
        !valid_close(snapshot.close)) return false;
    if (snapshot.close.eligible &&
        (snapshot.generation.network_generation == 0U ||
         snapshot.close.session_generation != snapshot.generation.network_generation))
        return false;
    std::size_t present{};
    std::optional<SessionReceiptStamp> previous;
    for (std::size_t index = 0U; index < snapshot.milestones.size(); ++index) {
        const auto& stamp = snapshot.milestones[index];
        if (!stamp) continue;
        // Receipts form one ordered, owning prefix; no world/ready marker can
        // bypass an earlier sign-on commit or borrow a prior generation.
        if (index != present || snapshot.generation.network_generation == 0U ||
            (index >= 1U && snapshot.generation.map_generation == 0U) ||
            !stage_ready(snapshot, static_cast<SessionReceiptStage>(index + 1U)))
            return false;
        if ((previous && stamp->process_ms < previous->process_ms) ||
            (stamp->host_qpc_ticks && (!snapshot.host_qpc_frequency ||
                                      *stamp->host_qpc_ticks == 0U)) ||
            (previous && previous->host_qpc_ticks && stamp->host_qpc_ticks &&
             *stamp->host_qpc_ticks < *previous->host_qpc_ticks)) return false;
        if (index >= 2U && stamp->publication_ordinal.value_or(0U) == 0U) return false;
        previous = stamp;
        ++present;
    }
    if (snapshot.stage != SessionReceiptStage::terminal &&
        present != stage_ordinal(snapshot.stage)) return false;
    if (snapshot.stage != SessionReceiptStage::terminal &&
        snapshot.close != SessionCloseReceipt{}) return false;
    if ((snapshot.close.requested_host_qpc_ticks ||
         snapshot.close.transport_closed_host_qpc_ticks) && !snapshot.host_qpc_frequency)
        return false;
    for (std::size_t index = 0U; index < snapshot.peer_removal_count; ++index) {
        const auto& peer = snapshot.peer_removals[index];
        if (peer.entity == 0U || peer.entity > 32U || peer.source_ordinal == 0U ||
            snapshot.generation.network_generation == 0U ||
            (peer.host_qpc_ticks != 0U && !snapshot.host_qpc_frequency)) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier)
            if (snapshot.peer_removals[earlier].entity == peer.entity) return false;
    }
    return true;
}

bool session_receipt_keyboard_ready(const SessionReceiptSnapshot& snapshot) noexcept
{
    const auto required = kSessionReceiptSignonFacts |
        receipt_fact(SessionReceiptFact::keyboard_mouse) |
        receipt_fact(SessionReceiptFact::live_visual_verified) |
        receipt_fact(SessionReceiptFact::usercmd_transmitted);
    return valid_session_receipt(snapshot) && has(snapshot, required) &&
        snapshot.milestones[1U].has_value() &&
        snapshot.milestones[4U].has_value() &&
        !snapshot.counters.usercmd_zero_observed &&
        snapshot.counters.new_usercmd_count.value_or(0U) > 0U &&
        snapshot.counters.transmitted_usercmd_packet_count.value_or(0U) > 0U &&
        snapshot.counters.server_sample_count.value_or(0U) > 0U;
}

SessionReceiptJournal::SessionReceiptJournal(SessionReceiptIdentity identity,
    const std::optional<std::uint64_t> host_qpc_frequency)
    : identity_{std::move(identity)}, host_qpc_frequency_{host_qpc_frequency} {}

bool SessionReceiptJournal::begin_generation(SessionReceiptGeneration generation)
{
    if (!valid_identity(identity_) || !valid_generation(generation) ||
        (host_qpc_frequency_ && *host_qpc_frequency_ == 0U)) return false;
    if (snapshot_) {
        const auto& old = snapshot_->generation;
        if (snapshot_->stage == SessionReceiptStage::terminal ||
            snapshot_->generation_ordinal >= kSessionReceiptMaximumGenerations ||
            generation.network_generation < old.network_generation ||
            (generation.network_generation == old.network_generation &&
             generation.map_generation <= old.map_generation)) return false;
    }
    const auto ordinal = snapshot_ ? snapshot_->generation_ordinal + 1U : 1U;
    SessionReceiptSnapshot next;
    next.identity = identity_;
    next.generation = std::move(generation);
    next.generation_ordinal = ordinal;
    next.host_qpc_frequency = host_qpc_frequency_;
    snapshot_ = std::move(next);
    return true;
}

bool SessionReceiptJournal::observe_facts(const std::uint64_t facts,
    const SessionReceiptCounters& counters) noexcept
{
    if (!snapshot_ || snapshot_->stage == SessionReceiptStage::terminal ||
        (facts & ~kSessionReceiptKnownFacts) != 0U) return false;
    snapshot_->fact_mask |= facts;
    retain_counter(snapshot_->counters.new_usercmd_count, counters.new_usercmd_count);
    retain_counter(snapshot_->counters.transmitted_usercmd_packet_count,
                   counters.transmitted_usercmd_packet_count);
    retain_counter(snapshot_->counters.server_sample_count, counters.server_sample_count);
    snapshot_->counters.usercmd_zero_observed |= counters.usercmd_zero_observed;
    return true;
}

bool SessionReceiptJournal::observe_receiving_entity(const std::uint8_t entity) noexcept
{
    if (!snapshot_ || snapshot_->stage == SessionReceiptStage::terminal ||
        snapshot_->generation.network_generation == 0U || entity == 0U || entity > 32U ||
        (snapshot_->receiving_entity && *snapshot_->receiving_entity != entity)) return false;
    snapshot_->receiving_entity = entity;
    return true;
}

bool SessionReceiptJournal::record(const SessionReceiptStage stage,
    const SessionReceiptStamp& stamp) noexcept
{
    if (!snapshot_ || stage == SessionReceiptStage::generation_started ||
        stage == SessionReceiptStage::terminal ||
        stage_ordinal(stage) != stage_ordinal(snapshot_->stage) + 1U ||
        !stage_ready(*snapshot_, stage)) return false;
    const auto previous = snapshot_->stage;
    const auto index = stage_ordinal(stage) - 1U;
    snapshot_->stage = stage;
    snapshot_->milestones[index] = stamp;
    if (!valid_session_receipt(*snapshot_)) {
        snapshot_->stage = previous;
        snapshot_->milestones[index].reset();
        return false;
    }
    return true;
}

bool SessionReceiptJournal::observe_peer_removal(
    const SessionPeerRemovalReceipt& observation) noexcept
{
    if (!snapshot_ || snapshot_->stage == SessionReceiptStage::terminal ||
        observation.entity == 0U || observation.entity > 32U ||
        observation.source_ordinal == 0U ||
        snapshot_->generation.network_generation == 0U ||
        (observation.host_qpc_ticks != 0U && !host_qpc_frequency_)) return false;
    for (std::size_t index = 0U; index < snapshot_->peer_removal_count; ++index) {
        auto& current = snapshot_->peer_removals[index];
        if (current.entity == observation.entity) {
            if (observation.source_ordinal < current.source_ordinal) return false;
            // Several validated vacancy controls can share one committed
            // record ordinal. Preserve the first observed removal time;
            // duplicate cursor/sub-message delivery is not a report failure.
            if (observation.source_ordinal == current.source_ordinal) return true;
            if (observation.process_ms < current.process_ms) return false;
            current = observation;
            return true;
        }
    }
    if (snapshot_->peer_removal_count >= kSessionReceiptMaximumPeerRemovals) return false;
    snapshot_->peer_removals[snapshot_->peer_removal_count++] = observation;
    return true;
}

bool SessionReceiptJournal::finish(const SessionCloseReceipt& close) noexcept
{
    if (!snapshot_ || snapshot_->stage == SessionReceiptStage::terminal) return false;
    const auto previous = snapshot_->stage;
    snapshot_->stage = SessionReceiptStage::terminal;
    snapshot_->close = close;
    if (!valid_session_receipt(*snapshot_)) {
        snapshot_->stage = previous;
        snapshot_->close = {};
        return false;
    }
    return true;
}

const SessionReceiptSnapshot* SessionReceiptJournal::snapshot() const noexcept
{
    return snapshot_ ? &*snapshot_ : nullptr;
}

std::wstring session_receipt_leaf(const SessionReceiptRole role,
    const std::uint32_t generation_ordinal, const SessionReceiptStage stage)
{
    if ((role != SessionReceiptRole::A && role != SessionReceiptRole::B) ||
        generation_ordinal == 0U || generation_ordinal > kSessionReceiptMaximumGenerations ||
        to_string(stage) == "invalid") return {};
    std::wstring result{L"session-receipt-"};
    result += role == SessionReceiptRole::A ? L'A' : L'B';
    if (stage != SessionReceiptStage::terminal) {
        result += L"-" + std::to_wstring(generation_ordinal);
    }
    result += L"-";
    const auto token = to_string(stage);
    result.append(token.begin(), token.end());
    result += L".txt";
    return result;
}

std::optional<std::string> encode_session_receipt(const SessionReceiptSnapshot& snapshot)
{
    if (!valid_session_receipt(snapshot)) return std::nullopt;
    std::string output;
    output.reserve(2'048U);
    const auto add = [&output](const std::string_view key, const std::string_view value) {
        output.append(key); output.push_back('='); output.append(value); output.push_back('\n');
    };
    const auto count = [&add](const std::string_view key, const std::uint64_t value) {
        add(key, std::to_string(value));
    };
    const auto flag = [&add](const std::string_view key, const bool value) {
        add(key, value ? "1" : "0");
    };
    const auto optional = [&add](const std::string_view key,
                                const std::optional<std::uint64_t>& value) {
        add(key, optional_number(value));
    };
    add("schema", kSessionReceiptSchema);
    add("run_id", snapshot.identity.run_id);
    add("role", snapshot.identity.role == SessionReceiptRole::A ? "A" : "B");
    count("process_id", snapshot.identity.process_id);
    count("process_creation", snapshot.identity.process_creation);
    count("generation_ordinal", snapshot.generation_ordinal);
    count("network_generation", snapshot.generation.network_generation);
    count("map_generation", snapshot.generation.map_generation);
    add("map_identifier", snapshot.generation.map_identifier);
    add("stage", to_string(snapshot.stage));
    count("fact_mask", snapshot.fact_mask);
    optional("receiving_entity", snapshot.receiving_entity
        ? std::optional<std::uint64_t>{*snapshot.receiving_entity} : std::nullopt);
    optional("host_qpc_frequency", snapshot.host_qpc_frequency);
    for (std::size_t index = 0U; index < snapshot.milestones.size(); ++index)
        add(to_string(static_cast<SessionReceiptStage>(index + 1U)),
            stamp_text(snapshot.milestones[index]));
    optional("new_usercmd_count", snapshot.counters.new_usercmd_count);
    optional("transmitted_usercmd_packet_count", snapshot.counters.transmitted_usercmd_packet_count);
    optional("server_sample_count", snapshot.counters.server_sample_count);
    flag("usercmd_zero_observed", snapshot.counters.usercmd_zero_observed);
    add("close_reason", to_string(snapshot.close.reason));
    add("shutdown_outcome", to_string(snapshot.close.outcome));
    add("transport_error", to_string(snapshot.close.transport_error));
    count("close_operation_identity", snapshot.close.operation_identity);
    count("close_session_generation", snapshot.close.session_generation);
    flag("close_requested", snapshot.close.requested);
    flag("disconnect_eligible", snapshot.close.eligible);
    flag("disconnect_queued", snapshot.close.queued);
    count("send_attempt_count", snapshot.close.send_attempt_count);
    count("successful_send_count", snapshot.close.successful_send_count);
    count("support_send_attempt_count", snapshot.close.support_send_attempt_count);
    count("successful_support_send_count", snapshot.close.successful_support_send_count);
    flag("ack_observed", snapshot.close.ack_observed);
    flag("server_disconnect_observed", snapshot.close.server_disconnect_observed);
    flag("deadline_reached", snapshot.close.deadline_reached);
    optional("requested_process_ms", snapshot.close.requested_process_ms);
    optional("deadline_process_ms", snapshot.close.deadline_process_ms);
    optional("first_send_process_ms", snapshot.close.first_send_process_ms);
    optional("last_send_process_ms", snapshot.close.last_send_process_ms);
    optional("transport_closed_process_ms", snapshot.close.transport_closed_process_ms);
    optional("requested_host_qpc_ticks", snapshot.close.requested_host_qpc_ticks);
    optional("transport_closed_host_qpc_ticks", snapshot.close.transport_closed_host_qpc_ticks);
    // Compact fixed-width typed metadata, not packet bytes. Each 25-byte
    // entry is entity/u64 source ordinal/u64 QPC/u64 process ms, little endian.
    add("peer_removals_hex", peer_text(snapshot));
    add("end", kSessionReceiptSchema);
    if (output.size() > kSessionReceiptMaximumBytes) return std::nullopt;
    return output;
}

std::optional<SessionReceiptSnapshot> decode_session_receipt(const std::string_view text)
{
    if (text.empty() || text.size() > kSessionReceiptMaximumBytes ||
        text.find('\0') != std::string_view::npos || text.find('\r') != std::string_view::npos)
        return std::nullopt;
    ReceiptReader reader{text};
    SessionReceiptSnapshot snapshot;
    const auto exact = [&reader](const std::string_view key, const std::string_view expected) {
        const auto value = reader.take(key); return value && *value == expected;
    };
    const auto integer = [&reader](const std::string_view key, std::uint64_t& target) {
        const auto value = reader.take(key); return value && number(*value, target);
    };
    const auto integer32 = [&integer](const std::string_view key, std::uint32_t& target) {
        std::uint64_t decoded{};
        if (!integer(key, decoded) || decoded > (std::numeric_limits<std::uint32_t>::max)())
            return false;
        target = static_cast<std::uint32_t>(decoded); return true;
    };
    const auto optional = [&reader](const std::string_view key,
                                    std::optional<std::uint64_t>& target) {
        const auto value = reader.take(key); return value && optional_number(*value, target);
    };
    const auto flag = [&reader](const std::string_view key, bool& target) {
        const auto value = reader.take(key); return value && boolean(*value, target);
    };
    if (!exact("schema", kSessionReceiptSchema)) return std::nullopt;
    const auto run = reader.take("run_id");
    const auto role = reader.take("role");
    if (!run || !role || (*role != "A" && *role != "B")) return std::nullopt;
    snapshot.identity.run_id = *run;
    snapshot.identity.role = *role == "A" ? SessionReceiptRole::A : SessionReceiptRole::B;
    if (!integer32("process_id", snapshot.identity.process_id) ||
        !integer("process_creation", snapshot.identity.process_creation) ||
        !integer32("generation_ordinal", snapshot.generation_ordinal) ||
        !integer("network_generation", snapshot.generation.network_generation) ||
        !integer("map_generation", snapshot.generation.map_generation)) return std::nullopt;
    const auto map = reader.take("map_identifier");
    const auto stage = reader.take("stage");
    if (!map || !stage) return std::nullopt;
    snapshot.generation.map_identifier = *map;
    if (!enum_value(*stage, snapshot.stage, std::array{
            SessionReceiptStage::generation_started, SessionReceiptStage::connection_established,
            SessionReceiptStage::signon_complete, SessionReceiptStage::map_entry_committed,
            SessionReceiptStage::world_ready, SessionReceiptStage::first_gameplay_update,
            SessionReceiptStage::terminal}) ||
        !integer("fact_mask", snapshot.fact_mask)) return std::nullopt;
    std::optional<std::uint64_t> receiving_entity;
    if (!optional("receiving_entity", receiving_entity) ||
        (receiving_entity && (*receiving_entity == 0U || *receiving_entity > 32U)) ||
        !optional("host_qpc_frequency", snapshot.host_qpc_frequency)) return std::nullopt;
    if (receiving_entity) snapshot.receiving_entity = static_cast<std::uint8_t>(*receiving_entity);
    for (std::size_t index = 0U; index < snapshot.milestones.size(); ++index) {
        const auto value = reader.take(to_string(static_cast<SessionReceiptStage>(index + 1U)));
        if (!value || !stamp_value(*value, snapshot.milestones[index])) return std::nullopt;
    }
    if (!optional("new_usercmd_count", snapshot.counters.new_usercmd_count) ||
        !optional("transmitted_usercmd_packet_count", snapshot.counters.transmitted_usercmd_packet_count) ||
        !optional("server_sample_count", snapshot.counters.server_sample_count) ||
        !flag("usercmd_zero_observed", snapshot.counters.usercmd_zero_observed)) return std::nullopt;
    const auto reason = reader.take("close_reason");
    const auto outcome = reader.take("shutdown_outcome");
    const auto error = reader.take("transport_error");
    if (!reason || !outcome || !error ||
        !enum_value(*reason, snapshot.close.reason, std::array{
            SessionCloseReason::not_observed, SessionCloseReason::window_close,
            SessionCloseReason::duration_limit, SessionCloseReason::server_disconnect,
            SessionCloseReason::startup_cancel, SessionCloseReason::fatal_failure,
            SessionCloseReason::runner_termination}) ||
        !enum_value(*outcome, snapshot.close.outcome, std::array{
            SessionCloseOutcome::not_requested, SessionCloseOutcome::pending,
            SessionCloseOutcome::cancel_without_protocol_disconnect,
            SessionCloseOutcome::sent_but_unconfirmed, SessionCloseOutcome::transport_acknowledged,
            SessionCloseOutcome::server_disconnect_confirmed, SessionCloseOutcome::send_failed,
            SessionCloseOutcome::cancelled, SessionCloseOutcome::fatal_failure}) ||
        !enum_value(*error, snapshot.close.transport_error, std::array{
            SessionCloseTransportError::not_observed, SessionCloseTransportError::would_block,
            SessionCloseTransportError::send_error, SessionCloseTransportError::poll_error,
            SessionCloseTransportError::invalid_state, SessionCloseTransportError::cancelled,
            SessionCloseTransportError::transport_unavailable})) return std::nullopt;
    if (!integer("close_operation_identity", snapshot.close.operation_identity) ||
        !integer("close_session_generation", snapshot.close.session_generation) ||
        !flag("close_requested", snapshot.close.requested) ||
        !flag("disconnect_eligible", snapshot.close.eligible) ||
        !flag("disconnect_queued", snapshot.close.queued) ||
        !integer32("send_attempt_count", snapshot.close.send_attempt_count) ||
        !integer32("successful_send_count", snapshot.close.successful_send_count) ||
        !integer32("support_send_attempt_count", snapshot.close.support_send_attempt_count) ||
        !integer32("successful_support_send_count", snapshot.close.successful_support_send_count) ||
        !flag("ack_observed", snapshot.close.ack_observed) ||
        !flag("server_disconnect_observed", snapshot.close.server_disconnect_observed) ||
        !flag("deadline_reached", snapshot.close.deadline_reached) ||
        !optional("requested_process_ms", snapshot.close.requested_process_ms) ||
        !optional("deadline_process_ms", snapshot.close.deadline_process_ms) ||
        !optional("first_send_process_ms", snapshot.close.first_send_process_ms) ||
        !optional("last_send_process_ms", snapshot.close.last_send_process_ms) ||
        !optional("transport_closed_process_ms", snapshot.close.transport_closed_process_ms) ||
        !optional("requested_host_qpc_ticks", snapshot.close.requested_host_qpc_ticks) ||
        !optional("transport_closed_host_qpc_ticks", snapshot.close.transport_closed_host_qpc_ticks))
        return std::nullopt;
    const auto peers = reader.take("peer_removals_hex");
    if (!peers || !peer_value(*peers, snapshot) || !exact("end", kSessionReceiptSchema) ||
        !reader.done() || !valid_session_receipt(snapshot)) return std::nullopt;
    // A canonical closed grammar rejects alternate whitespace, ordering,
    // duplicated keys, integer spellings or a valid prefix followed by garbage.
    const auto encoded = encode_session_receipt(snapshot);
    if (!encoded || *encoded != text) return std::nullopt;
    return snapshot;
}

} // namespace hlclient::app
