#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::app {

inline constexpr std::string_view kSessionReceiptSchema =
    "hlclient.session-receipt.v1";
inline constexpr std::size_t kSessionReceiptMaximumBytes = 4'096U;
inline constexpr std::size_t kSessionReceiptMaximumGenerations = 8U;
inline constexpr std::size_t kSessionReceiptMaximumPeerRemovals = 32U;
inline constexpr std::size_t kSessionReceiptMilestoneCount = 5U;

enum class SessionReceiptRole { A, B };
enum class SessionReceiptStage {
    generation_started,
    connection_established,
    signon_complete,
    map_entry_committed,
    world_ready,
    first_gameplay_update,
    terminal,
};
enum class SessionReceiptFact : std::uint64_t {
    steam_api_initialized = 1ULL << 0U,
    fresh_material_acquired = 1ULL << 1U,
    connect_sent = 1ULL << 2U,
    connection_accepted = 1ULL << 3U,
    serverinfo_received = 1ULL << 4U,
    schema_registry_received = 1ULL << 5U,
    resource_continuation_sent = 1ULL << 6U,
    spawn_request_transmitted = 1ULL << 7U,
    spawn_request_acknowledged = 1ULL << 8U,
    signon_reply_transmitted = 1ULL << 9U,
    signon_reply_acknowledged = 1ULL << 10U,
    live_service_payloads_received = 1ULL << 11U,
    keyboard_mouse = 1ULL << 12U,
    live_visual_verified = 1ULL << 13U,
    usercmd_transmitted = 1ULL << 14U,
    world_published = 1ULL << 15U,
    gameplay_committed = 1ULL << 16U,
};
[[nodiscard]] constexpr std::uint64_t receipt_fact(
    const SessionReceiptFact fact) noexcept
{
    return static_cast<std::uint64_t>(fact);
}
inline constexpr std::uint64_t kSessionReceiptConnectionFacts = (1ULL << 4U) - 1U;
inline constexpr std::uint64_t kSessionReceiptSignonFacts = (1ULL << 12U) - 1U;
inline constexpr std::uint64_t kSessionReceiptKnownFacts = (1ULL << 17U) - 1U;

struct SessionReceiptIdentity final {
    std::string run_id;
    SessionReceiptRole role{SessionReceiptRole::A};
    std::uint32_t process_id{};
    // FILETIME from the process's own/owned process handle. Identity, not a
    // milestone timestamp and never substituted with the current wall time.
    std::uint64_t process_creation{};
    friend bool operator==(const SessionReceiptIdentity&,
                           const SessionReceiptIdentity&) = default;
};
struct SessionReceiptGeneration final {
    std::uint64_t network_generation{};
    std::uint64_t map_generation{};
    // Actual validated map only. Before serverinfo use "not_observed".
    std::string map_identifier{"not_observed"};
    friend bool operator==(const SessionReceiptGeneration&,
                           const SessionReceiptGeneration&) = default;
};
struct SessionReceiptStamp final {
    // Relative to the application's steady-clock origin, not cross-process
    // subtractable. Optional QPC ticks use the separately recorded frequency.
    std::uint64_t process_ms{};
    std::optional<std::uint64_t> source_sequence;
    std::optional<std::uint64_t> publication_ordinal;
    std::optional<std::uint64_t> host_qpc_ticks;
    friend bool operator==(const SessionReceiptStamp&,
                           const SessionReceiptStamp&) = default;
};
struct SessionReceiptCounters final {
    std::optional<std::uint64_t> new_usercmd_count;
    std::optional<std::uint64_t> transmitted_usercmd_packet_count;
    std::optional<std::uint64_t> server_sample_count;
    bool usercmd_zero_observed{};
    friend bool operator==(const SessionReceiptCounters&,
                           const SessionReceiptCounters&) = default;
};
enum class SessionCloseReason {
    not_observed, window_close, duration_limit, server_disconnect,
    startup_cancel, fatal_failure, runner_termination,
};
enum class SessionCloseOutcome {
    not_requested, pending, cancel_without_protocol_disconnect,
    sent_but_unconfirmed, transport_acknowledged, server_disconnect_confirmed,
    send_failed, cancelled, fatal_failure,
};
enum class SessionCloseTransportError {
    not_observed, would_block, send_error, poll_error, invalid_state,
    cancelled, transport_unavailable,
};
struct SessionCloseReceipt final {
    SessionCloseReason reason{SessionCloseReason::not_observed};
    SessionCloseOutcome outcome{SessionCloseOutcome::not_requested};
    SessionCloseTransportError transport_error{
        SessionCloseTransportError::not_observed};
    std::uint64_t operation_identity{};
    std::uint64_t session_generation{};
    bool requested{};
    bool eligible{};
    bool queued{};
    std::uint32_t send_attempt_count{};
    std::uint32_t successful_send_count{};
    std::uint32_t support_send_attempt_count{};
    std::uint32_t successful_support_send_count{};
    bool ack_observed{};
    bool server_disconnect_observed{};
    bool deadline_reached{};
    std::optional<std::uint64_t> requested_process_ms;
    std::optional<std::uint64_t> deadline_process_ms;
    std::optional<std::uint64_t> first_send_process_ms;
    std::optional<std::uint64_t> last_send_process_ms;
    std::optional<std::uint64_t> transport_closed_process_ms;
    std::optional<std::uint64_t> requested_host_qpc_ticks;
    std::optional<std::uint64_t> transport_closed_host_qpc_ticks;
    friend bool operator==(const SessionCloseReceipt&,
                           const SessionCloseReceipt&) = default;
};
struct SessionPeerRemovalReceipt final {
    std::uint8_t entity{};
    // These observations belong to the snapshot's network/map generations.
    std::uint64_t source_ordinal{};
    std::uint64_t host_qpc_ticks{}; // zero means unavailable, not an invented time
    std::uint64_t process_ms{};
    friend bool operator==(const SessionPeerRemovalReceipt&,
                           const SessionPeerRemovalReceipt&) = default;
};
struct SessionReceiptSnapshot final {
    SessionReceiptIdentity identity;
    SessionReceiptGeneration generation;
    std::uint32_t generation_ordinal{};
    SessionReceiptStage stage{SessionReceiptStage::generation_started};
    std::uint64_t fact_mask{};
    // Serverinfo's actual receiving player slot + 1, never inferred from A/B.
    std::optional<std::uint8_t> receiving_entity;
    SessionReceiptCounters counters;
    std::optional<std::uint64_t> host_qpc_frequency;
    std::array<std::optional<SessionReceiptStamp>,
               kSessionReceiptMilestoneCount> milestones;
    SessionCloseReceipt close;
    std::array<SessionPeerRemovalReceipt,
               kSessionReceiptMaximumPeerRemovals> peer_removals{};
    std::size_t peer_removal_count{};
    friend bool operator==(const SessionReceiptSnapshot&,
                           const SessionReceiptSnapshot&) = default;
};

// This journal accepts typed observations only from its owning application.
// It does not parse log/server text, packets, names or planned configuration.
// File publication is separate and never changes committed facts.
class SessionReceiptJournal final {
public:
    explicit SessionReceiptJournal(SessionReceiptIdentity identity,
        std::optional<std::uint64_t> host_qpc_frequency = std::nullopt);
    [[nodiscard]] bool begin_generation(SessionReceiptGeneration generation);
    [[nodiscard]] bool observe_facts(std::uint64_t facts,
        const SessionReceiptCounters& counters = {}) noexcept;
    [[nodiscard]] bool observe_receiving_entity(std::uint8_t entity) noexcept;
    [[nodiscard]] bool record(SessionReceiptStage stage,
        const SessionReceiptStamp& stamp) noexcept;
    [[nodiscard]] bool observe_peer_removal(
        const SessionPeerRemovalReceipt& observation) noexcept;
    [[nodiscard]] bool finish(const SessionCloseReceipt& close) noexcept;
    [[nodiscard]] const SessionReceiptSnapshot* snapshot() const noexcept;
private:
    SessionReceiptIdentity identity_;
    std::optional<std::uint64_t> host_qpc_frequency_;
    std::optional<SessionReceiptSnapshot> snapshot_;
};

[[nodiscard]] bool valid_session_receipt(const SessionReceiptSnapshot&) noexcept;
[[nodiscard]] bool session_receipt_keyboard_ready(
    const SessionReceiptSnapshot&) noexcept;
[[nodiscard]] std::optional<std::string> normalized_session_map_identifier(
    std::string_view);
[[nodiscard]] std::optional<std::string> encode_session_receipt(
    const SessionReceiptSnapshot&);
[[nodiscard]] std::optional<SessionReceiptSnapshot> decode_session_receipt(
    std::string_view);
// Deterministic leaf whitelist. Six snapshots per generation plus one
// process-final leaf. Terminal is write-once for the complete process.
[[nodiscard]] std::wstring session_receipt_leaf(
    SessionReceiptRole, std::uint32_t generation_ordinal, SessionReceiptStage);
[[nodiscard]] std::string_view to_string(SessionReceiptStage) noexcept;
[[nodiscard]] std::string_view to_string(SessionCloseReason) noexcept;
[[nodiscard]] std::string_view to_string(SessionCloseOutcome) noexcept;
[[nodiscard]] std::string_view to_string(SessionCloseTransportError) noexcept;

} // namespace hlclient::app
