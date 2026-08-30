#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::string_view kStockRuntimeReconnectObservationSchema =
    "hlclient.stock-runtime-reconnect-observation.v1";
inline constexpr std::string_view kStockRuntimeReconnectTransportObservationSchema =
    "hlclient.stock-runtime-reconnect-transport-observation.v1";
inline constexpr std::string_view kStockRuntimeReconnectOrchestrationAttestationSchema =
    "hlclient.stock-runtime-reconnect-orchestration.v1";
inline constexpr std::string_view kStockActiveCapturePreflightAttestationSchema =
    "hlclient.stock-active-capture-preflight-attestation.v1";
inline constexpr std::string_view kStockRuntimeGenerationAEndpointRole =
    "research_client_generation_a";
inline constexpr std::string_view kStockRuntimeGenerationBEndpointRole =
    "research_client_generation_b";
inline constexpr std::string_view kStockRuntimeGenerationAProcessRole =
    "owned_client_generation_a";
inline constexpr std::string_view kStockRuntimeGenerationBProcessRole =
    "owned_client_generation_b";

// This is a public, bounded attestation. It intentionally cannot contain a
// native path, binary digest, endpoint, user identity, or raw WFP identifier.
struct StockActiveCapturePreflightAttestation final {
    bool elevated{false};
    bool binary_profile_valid{false};
    bool app_manifest_valid{false};
    bool dynamic_wfp_session{false};
    bool ipv4_loopback_allowed{false};
    bool ipv6_loopback_allowed{false};
    bool ipv6_capability_available{false};
    bool non_loopback_denied_by_os{false};
    bool isolation_cleanup_exact{false};
    std::string timestamp_category;
    bool success{false};
};

[[nodiscard]] bool validate_stock_active_capture_preflight_attestation(
    const StockActiveCapturePreflightAttestation& attestation) noexcept;

// Pure relay-transition state used by the active capture and fake fixtures.
// It classifies only connectionless/connect/ACCEPT envelope facts. It never
// consumes a sequenced payload body or assigns a runtime semantic category.
enum class StockRuntimeReconnectServerRoute {
    reject,
    retired_generation_a,
    generation_b,
};

class StockRuntimeReconnectRelayTransition final {
public:
    // Prepare the alternate A route while the owned A process is still alive.
    // The active relay creates its send-only tail emitter before calling this,
    // then acknowledges readiness to the orchestrator before A is stopped.
    [[nodiscard]] bool prepare_generation_a_retirement(
        bool generation_a_transport_complete,
        bool held_datagrams_empty) noexcept;

    // A second orchestrator capability signal is accepted only after the first
    // readiness ACK and exact owned-process shutdown proof.
    [[nodiscard]] bool confirm_generation_a_shutdown(
        bool controlled_shutdown_observed,
        bool owned_client_process_absent) noexcept;

    [[nodiscard]] bool begin_generation_b_relearn(
        bool generation_a_transport_complete,
        bool generation_a_client_source_quiet,
        bool held_datagrams_empty) noexcept;

    // The caller proves the first endpoint is loopback, new and distinct, and
    // proves later calls originate from that exact learned endpoint.
    [[nodiscard]] bool observe_generation_b_client_datagram(
        bool endpoint_source_valid,
        bool connectionless,
        bool connect_observed) noexcept;

    [[nodiscard]] StockRuntimeReconnectServerRoute route_server_datagram(
        bool exact_server_source,
        bool connectionless,
        bool accept_observed) noexcept;

    [[nodiscard]] constexpr bool generation_b_endpoint_learned() const noexcept
    {
        return generation_b_endpoint_learned_;
    }
    [[nodiscard]] constexpr bool
    generation_a_retirement_prepared() const noexcept
    {
        return generation_a_retirement_prepared_;
    }
    [[nodiscard]] constexpr bool
    generation_a_shutdown_confirmed() const noexcept
    {
        return generation_a_shutdown_confirmed_;
    }
    [[nodiscard]] constexpr bool generation_b_relearn_ready() const noexcept
    {
        return generation_b_relearn_ready_;
    }
    [[nodiscard]] constexpr bool generation_b_connect_observed() const noexcept
    {
        return generation_b_connect_observed_;
    }
    [[nodiscard]] constexpr bool generation_b_accept_observed() const noexcept
    {
        return generation_b_accept_observed_;
    }
    [[nodiscard]] constexpr std::size_t
    retired_generation_a_server_tail_packet_count() const noexcept
    {
        return retired_generation_a_server_tail_packet_count_;
    }

private:
    bool generation_a_retirement_prepared_{false};
    bool generation_a_shutdown_confirmed_{false};
    bool generation_b_relearn_ready_{false};
    bool generation_b_endpoint_learned_{false};
    bool generation_b_connect_observed_{false};
    bool generation_b_accept_observed_{false};
    std::size_t retired_generation_a_server_tail_packet_count_{0U};
};

// Geometry only. No source payload or candidate body is retained here.
struct StockRuntimeGenerationBoundaryObservation final {
    bool observed{false};
    std::size_t replay_payload_ordinal{0U};
    std::size_t corpus_observed_ordinal{0U};
    std::size_t delivery_ordinal{0U};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::size_t source_payload_byte_count{0U};
    std::size_t source_payload_bit_count{0U};
    std::size_t next_unconsumed_bit_count{0U};
};

// Neutral first-prefix observation only. `body_consumed` and
// `semantic_category_assigned` are explicit fail-closed invariants rather than
// capabilities offered by this milestone.
struct StockRuntimeGenerationCandidateObservation final {
    bool observed{false};
    std::size_t candidate_bit_width{0U};
    std::optional<std::uint8_t> numeric_candidate;
    std::optional<std::uint8_t> bounded_bit_prefix;
    bool byte_aligned{false};
    bool body_consumed{false};
    bool semantic_category_assigned{false};
};

// Sanitized generation identity. Endpoint and process roles are fixed tokens;
// actual UDP ports, PIDs, process handles, native paths and private identities
// never enter this object.
struct StockRuntimeConnectionGenerationObservation final {
    std::size_t generation_ordinal{0U};
    std::string learned_client_endpoint_role_identity;
    std::string owned_client_process_role_identity;
    bool owned_client_process_observed{false};
    bool fresh_owned_client_process{false};
    bool learned_client_endpoint_observed{false};
    bool learned_client_endpoint_distinct_from_previous{false};
    std::size_t first_observed_ordinal{0U};
    std::size_t last_observed_ordinal{0U};
    std::size_t connectionless_exchange_count{0U};
    bool connect_observed{false};
    bool accept_observed{false};
    std::optional<std::size_t> first_sequenced_packet_ordinal;
    StockRuntimeGenerationBoundaryObservation exact_post_resource_boundary;
    StockRuntimeGenerationCandidateObservation candidate_observation;
    std::size_t client_to_server_packet_count{0U};
    std::size_t server_to_client_packet_count{0U};
    std::string profile_identity;
    bool controlled_client_shutdown_observed{false};
    bool retired_client_endpoint_quiet{false};
};

struct StockRuntimeReconnectLifecycleInput final {
    std::span<const StockRuntimeConnectionGenerationObservation> generations;
    bool guard_continuous_between_generations{false};
    bool server_continuous_between_generations{false};
    bool relay_continuous_between_generations{false};
    bool cleanup_exact{false};
    bool restoration_exact{false};
    bool transactional_publication_ready{false};
};

enum class StockRuntimeReconnectLifecycleErrorCode {
    invalid_configuration,
    wrong_generation_count,
    invalid_generation_ordinal,
    invalid_role_identity,
    invalid_profile_identity,
    profile_identity_mismatch,
    owned_client_process_not_observed,
    fresh_client_process_not_proven,
    learned_client_endpoint_not_observed,
    endpoint_generation_not_proven,
    invalid_observed_ordinal_range,
    missing_connectionless_exchange,
    missing_connect,
    missing_accept,
    missing_first_sequenced_packet,
    invalid_packet_counters,
    missing_post_resource_boundary,
    invalid_boundary_geometry,
    missing_candidate,
    invalid_candidate_geometry,
    candidate_body_consumed,
    candidate_semantic_name_assigned,
    candidate_conflicting,
    generation_order_invalid,
    generation_a_shutdown_not_proven,
    generation_a_endpoint_not_retired,
    guard_lost_between_generations,
    server_exited_between_generations,
    relay_exited_between_generations,
    cleanup_inexact,
    restoration_inexact,
    transactional_publication_not_ready,
    allocation_failed,
};

struct StockRuntimeReconnectLifecycleError final {
    StockRuntimeReconnectLifecycleErrorCode code{
        StockRuntimeReconnectLifecycleErrorCode::invalid_configuration};
    std::size_t generation_ordinal{0U};
    std::string context;
};

class StockRuntimeReconnectLifecycleState final {
public:
    StockRuntimeReconnectLifecycleState(
        const StockRuntimeReconnectLifecycleState&) = default;
    StockRuntimeReconnectLifecycleState& operator=(
        const StockRuntimeReconnectLifecycleState&) = delete;
    StockRuntimeReconnectLifecycleState(
        StockRuntimeReconnectLifecycleState&&) noexcept = default;
    StockRuntimeReconnectLifecycleState& operator=(
        StockRuntimeReconnectLifecycleState&&) noexcept = delete;
    ~StockRuntimeReconnectLifecycleState() = default;

    [[nodiscard]] const std::vector<StockRuntimeConnectionGenerationObservation>&
    generations() const noexcept;
    [[nodiscard]] std::size_t connection_generation_count() const noexcept;
    [[nodiscard]] std::size_t exact_boundary_count() const noexcept;
    [[nodiscard]] std::size_t runtime_candidate_count() const noexcept;
    [[nodiscard]] std::size_t client_to_server_packet_count() const noexcept;
    [[nodiscard]] std::size_t server_to_client_packet_count() const noexcept;
    [[nodiscard]] bool generation_distinct() const noexcept;
    [[nodiscard]] constexpr bool candidate_conflict() const noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool candidate_body_consumed() const noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool semantic_category_assigned() const noexcept
    {
        return false;
    }

private:
    friend struct StockRuntimeReconnectLifecycleStateFactory;

    explicit StockRuntimeReconnectLifecycleState(
        std::vector<StockRuntimeConnectionGenerationObservation> generations,
        std::size_t client_to_server_packet_count,
        std::size_t server_to_client_packet_count) noexcept;

    std::vector<StockRuntimeConnectionGenerationObservation> generations_;
    std::size_t client_to_server_packet_count_{0U};
    std::size_t server_to_client_packet_count_{0U};
};

struct StockRuntimeReconnectLifecycleValidationResult final {
    std::optional<StockRuntimeReconnectLifecycleState> state;
    std::optional<StockRuntimeReconnectLifecycleError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

[[nodiscard]] StockRuntimeReconnectLifecycleValidationResult
validate_stock_runtime_reconnect_lifecycle(
    const StockRuntimeReconnectLifecycleInput& input);

[[nodiscard]] constexpr std::string_view to_string(
    const StockRuntimeReconnectLifecycleErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeReconnectLifecycleErrorCode::invalid_configuration: return "invalid_configuration";
    case StockRuntimeReconnectLifecycleErrorCode::wrong_generation_count: return "wrong_generation_count";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_generation_ordinal: return "invalid_generation_ordinal";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_role_identity: return "invalid_role_identity";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_profile_identity: return "invalid_profile_identity";
    case StockRuntimeReconnectLifecycleErrorCode::profile_identity_mismatch: return "profile_identity_mismatch";
    case StockRuntimeReconnectLifecycleErrorCode::owned_client_process_not_observed: return "owned_client_process_not_observed";
    case StockRuntimeReconnectLifecycleErrorCode::fresh_client_process_not_proven: return "fresh_client_process_not_proven";
    case StockRuntimeReconnectLifecycleErrorCode::learned_client_endpoint_not_observed: return "learned_client_endpoint_not_observed";
    case StockRuntimeReconnectLifecycleErrorCode::endpoint_generation_not_proven: return "reconnect_generation_not_proven";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_observed_ordinal_range: return "invalid_observed_ordinal_range";
    case StockRuntimeReconnectLifecycleErrorCode::missing_connectionless_exchange: return "missing_connectionless_exchange";
    case StockRuntimeReconnectLifecycleErrorCode::missing_connect: return "missing_connect";
    case StockRuntimeReconnectLifecycleErrorCode::missing_accept: return "missing_accept";
    case StockRuntimeReconnectLifecycleErrorCode::missing_first_sequenced_packet: return "missing_first_sequenced_packet";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_packet_counters: return "invalid_packet_counters";
    case StockRuntimeReconnectLifecycleErrorCode::missing_post_resource_boundary: return "missing_second_boundary";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_boundary_geometry: return "invalid_boundary_geometry";
    case StockRuntimeReconnectLifecycleErrorCode::missing_candidate: return "missing_candidate";
    case StockRuntimeReconnectLifecycleErrorCode::invalid_candidate_geometry: return "invalid_candidate_geometry";
    case StockRuntimeReconnectLifecycleErrorCode::candidate_body_consumed: return "candidate_body_consumed";
    case StockRuntimeReconnectLifecycleErrorCode::candidate_semantic_name_assigned: return "candidate_semantic_name_assigned";
    case StockRuntimeReconnectLifecycleErrorCode::candidate_conflicting: return "candidate_conflicting";
    case StockRuntimeReconnectLifecycleErrorCode::generation_order_invalid: return "generation_order_invalid";
    case StockRuntimeReconnectLifecycleErrorCode::generation_a_shutdown_not_proven: return "generation_a_shutdown_not_proven";
    case StockRuntimeReconnectLifecycleErrorCode::generation_a_endpoint_not_retired: return "generation_a_endpoint_not_retired";
    case StockRuntimeReconnectLifecycleErrorCode::guard_lost_between_generations: return "guard_lost_between_generations";
    case StockRuntimeReconnectLifecycleErrorCode::server_exited_between_generations: return "server_exited_between_generations";
    case StockRuntimeReconnectLifecycleErrorCode::relay_exited_between_generations: return "relay_exited_between_generations";
    case StockRuntimeReconnectLifecycleErrorCode::cleanup_inexact: return "cleanup_inexact";
    case StockRuntimeReconnectLifecycleErrorCode::restoration_inexact: return "restoration_inexact";
    case StockRuntimeReconnectLifecycleErrorCode::transactional_publication_not_ready: return "transactional_publication_not_ready";
    case StockRuntimeReconnectLifecycleErrorCode::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
