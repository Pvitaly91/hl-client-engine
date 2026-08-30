#pragma once

#include <hlclient/goldsrc/resource_client_response.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

struct StockCapturedSignonReplayLimits final {
    std::size_t maximum_replayed_payloads{65'536U};
    std::size_t maximum_payload_bytes{1'048'576U};
    std::size_t maximum_client_request_candidates{4'096U};
};

// Exact cursor into one owning offline replay payload. The byte and bit offsets
// point at the first unconsumed post-resource bit; no runtime body is consumed.
struct StockPostResourceResponseCursor final {
    std::size_t replay_payload_ordinal{0U};
    std::size_t corpus_observed_ordinal{0U};
    std::size_t delivery_ordinal{0U};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::uint32_t source_netchan_sequence{0U};
    std::size_t source_payload_byte_count{0U};
    std::size_t source_payload_bit_count{0U};
    std::size_t next_unconsumed_bit_count{0U};
    bool reassembled{false};
    bool decompressed{false};
};

class StockCapturedSignonReplayState final {
public:
    StockCapturedSignonReplayState(const StockCapturedSignonReplayState&) = default;
    StockCapturedSignonReplayState& operator=(
        const StockCapturedSignonReplayState&) = delete;
    StockCapturedSignonReplayState(StockCapturedSignonReplayState&&) noexcept = default;
    StockCapturedSignonReplayState& operator=(
        StockCapturedSignonReplayState&&) noexcept = delete;
    ~StockCapturedSignonReplayState() = default;

    [[nodiscard]] const PostResourceResponseBoundary& boundary() const noexcept;
    [[nodiscard]] const StockPostResourceResponseCursor& cursor() const noexcept;
    [[nodiscard]] std::size_t observed_client_request_count() const noexcept;
    [[nodiscard]] std::size_t decoded_server_signon_payload_count() const noexcept;
    [[nodiscard]] bool known_signon_validated() const noexcept;
    [[nodiscard]] bool observed_initial_new() const noexcept;
    [[nodiscard]] bool observed_sendres() const noexcept;
    [[nodiscard]] bool observed_opcode5_resource_response() const noexcept;
    [[nodiscard]] constexpr bool generated_ack() const noexcept { return false; }
    [[nodiscard]] constexpr bool generated_client_request() const noexcept { return false; }

private:
    friend class StockCapturedSignonReplay;

    StockCapturedSignonReplayState(
        PostResourceResponseBoundary boundary,
        StockPostResourceResponseCursor cursor,
        std::size_t observed_client_request_count,
        std::size_t decoded_server_signon_payload_count,
        bool known_signon_validated) noexcept;

    PostResourceResponseBoundary boundary_;
    StockPostResourceResponseCursor cursor_;
    std::size_t observed_client_request_count_{0U};
    std::size_t decoded_server_signon_payload_count_{0U};
    bool known_signon_validated_{false};
};

enum class StockCapturedSignonReplayErrorCode {
    invalid_configuration,
    connection_not_established,
    netchan_replay_failed,
    fragment_reassembly_failed,
    decompression_failed,
    signon_sequence_incomplete,
    initial_request_not_observed,
    initial_service_decode_failed,
    pre_resource_decode_failed,
    delta_description_decode_failed,
    movevars_decode_failed,
    user_info_decode_failed,
    resource_transition_request_not_observed,
    resource_transition_decode_failed,
    resource_list_decode_failed,
    resource_response_not_observed,
    resource_response_invalid,
    post_resource_cursor_unavailable,
    unsupported_secondary_stream,
    capture_incomplete,
    payload_limit_exceeded,
    allocation_failed,
};

struct StockCapturedSignonReplayError final {
    StockCapturedSignonReplayErrorCode code{
        StockCapturedSignonReplayErrorCode::invalid_configuration};
    std::size_t replay_payload_ordinal{0U};
    std::string context;
    std::optional<StockRuntimeTransportReplayErrorCode> transport_code;
};

struct StockCapturedSignonReplayResult final {
    std::optional<StockCapturedSignonReplayState> state;
    std::optional<StockCapturedSignonReplayError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class StockCapturedSignonReplay final {
public:
    explicit StockCapturedSignonReplay(
        StockCapturedSignonReplayLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockCapturedSignonReplayLimits& limits() const noexcept;

    [[nodiscard]] StockCapturedSignonReplayResult replay(
        const StockRuntimeTransportReplayResult& transport) const;
    [[nodiscard]] StockCapturedSignonReplayResult replay(
        const StockRuntimeTransportReplayState& transport) const;

    // Pure exact-boundary adapter used by mutation tests and by the full
    // sign-on replay after an observed opcode-5 response. It reads byte zero at
    // most through the existing neutral boundary parser and never scans.
    [[nodiscard]] StockCapturedSignonReplayResult reconstruct_post_resource_boundary(
        const StockRuntimeTransportReplayState& transport,
        std::size_t first_post_response_server_payload_ordinal,
        std::size_t observed_client_request_count,
        std::size_t decoded_server_signon_payload_count) const;

private:
    [[nodiscard]] StockCapturedSignonReplayResult reconstruct_boundary(
        const StockRuntimeTransportReplayState& transport,
        std::size_t first_post_response_server_payload_ordinal,
        std::size_t observed_client_request_count,
        std::size_t decoded_server_signon_payload_count,
        bool known_signon_validated) const;

    StockCapturedSignonReplayLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    StockCapturedSignonReplayErrorCode code) noexcept
{
    switch (code) {
    case StockCapturedSignonReplayErrorCode::invalid_configuration: return "invalid_configuration";
    case StockCapturedSignonReplayErrorCode::connection_not_established: return "connection_not_established";
    case StockCapturedSignonReplayErrorCode::netchan_replay_failed: return "netchan_replay_failed";
    case StockCapturedSignonReplayErrorCode::fragment_reassembly_failed: return "fragment_reassembly_failed";
    case StockCapturedSignonReplayErrorCode::decompression_failed: return "decompression_failed";
    case StockCapturedSignonReplayErrorCode::signon_sequence_incomplete: return "signon_sequence_incomplete";
    case StockCapturedSignonReplayErrorCode::initial_request_not_observed: return "initial_request_not_observed";
    case StockCapturedSignonReplayErrorCode::initial_service_decode_failed: return "initial_service_decode_failed";
    case StockCapturedSignonReplayErrorCode::pre_resource_decode_failed: return "pre_resource_decode_failed";
    case StockCapturedSignonReplayErrorCode::delta_description_decode_failed: return "delta_description_decode_failed";
    case StockCapturedSignonReplayErrorCode::movevars_decode_failed: return "movevars_decode_failed";
    case StockCapturedSignonReplayErrorCode::user_info_decode_failed: return "user_info_decode_failed";
    case StockCapturedSignonReplayErrorCode::resource_transition_request_not_observed: return "resource_transition_request_not_observed";
    case StockCapturedSignonReplayErrorCode::resource_transition_decode_failed: return "resource_transition_decode_failed";
    case StockCapturedSignonReplayErrorCode::resource_list_decode_failed: return "resource_list_decode_failed";
    case StockCapturedSignonReplayErrorCode::resource_response_not_observed: return "resource_response_not_observed";
    case StockCapturedSignonReplayErrorCode::resource_response_invalid: return "resource_response_invalid";
    case StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable: return "post_resource_cursor_unavailable";
    case StockCapturedSignonReplayErrorCode::unsupported_secondary_stream: return "unsupported_secondary_stream";
    case StockCapturedSignonReplayErrorCode::capture_incomplete: return "capture_incomplete";
    case StockCapturedSignonReplayErrorCode::payload_limit_exceeded: return "payload_limit_exceeded";
    case StockCapturedSignonReplayErrorCode::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
