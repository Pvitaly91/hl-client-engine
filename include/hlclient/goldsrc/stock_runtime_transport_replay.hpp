#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_reassembly.hpp>
#include <hlclient/goldsrc/service_payload_envelope.hpp>
#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class StockRuntimeTransportReplayEngine;

struct StockRuntimeTransportReplayLimits final {
    std::size_t maximum_delivered_datagrams{131'072U};
    std::size_t maximum_replayed_payloads{65'536U};
    std::size_t maximum_datagram_bytes{kMaximumNetchanDatagramSize};
    std::size_t maximum_total_delivered_datagram_bytes{
        128U * 1'024U * 1'024U};
    std::size_t maximum_reassembled_bytes{1'048'576U};
    std::size_t maximum_decompressed_bytes{1'048'576U};
    // Bounds the sum of all owning payload buffers retained by one replay.
    // The per-payload limits above are not a substitute for this aggregate
    // budget when a corpus contains many individually valid payloads.
    std::size_t maximum_total_replayed_payload_bytes{64U * 1'024U * 1'024U};
    std::size_t maximum_fragments_per_transfer{1'024U};
};

struct StockRuntimeTransportReplayDatagram final {
    StockRuntimeCaptureDirection direction{
        StockRuntimeCaptureDirection::client_to_server};
    std::size_t delivery_ordinal{0U};
    std::size_t observed_ordinal{0U};
    std::size_t direction_ordinal{1U};
    std::uint64_t observed_relative_timestamp_us{0U};
    std::vector<std::byte> bytes;
};

enum class StockRuntimeConnectionReplayState {
    accepted,
};

enum class StockRuntimeReplayedPayloadKind {
    ordinary,
    contemporaneous_fragment_suffix,
    completed_normal_fragment_transfer,
};

// An owning offline-only payload. It carries exact delivery provenance but no
// endpoint, socket, timer, authentication material, or native filesystem path.
class StockRuntimeReplayedPayload final {
public:
    [[nodiscard]] NetchanDirection direction() const noexcept;
    [[nodiscard]] std::uint32_t source_sequence() const noexcept;
    [[nodiscard]] std::uint32_t source_acknowledgement() const noexcept;
    [[nodiscard]] bool acknowledgement_reliable() const noexcept;
    [[nodiscard]] bool reliable() const noexcept;
    [[nodiscard]] bool fragmented() const noexcept;
    [[nodiscard]] bool reassembled() const noexcept;
    [[nodiscard]] bool decompressed() const noexcept;
    [[nodiscard]] StockRuntimeReplayedPayloadKind kind() const noexcept;
    [[nodiscard]] std::size_t corpus_observed_ordinal() const noexcept;
    [[nodiscard]] std::size_t delivery_ordinal() const noexcept;
    [[nodiscard]] std::size_t source_fragment_count() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    friend class StockRuntimeTransportReplay;
    friend class StockRuntimeTransportReplayEngine;

    StockRuntimeReplayedPayload(
        NetchanDirection direction,
        std::uint32_t source_sequence,
        std::uint32_t source_acknowledgement,
        bool acknowledgement_reliable,
        bool reliable,
        bool fragmented,
        bool reassembled,
        bool decompressed,
        StockRuntimeReplayedPayloadKind kind,
        std::size_t corpus_observed_ordinal,
        std::size_t delivery_ordinal,
        std::size_t source_fragment_count,
        std::vector<std::byte> bytes) noexcept;

    NetchanDirection direction_{NetchanDirection::server_to_client};
    std::uint32_t source_sequence_{0U};
    std::uint32_t source_acknowledgement_{0U};
    bool acknowledgement_reliable_{false};
    bool reliable_{false};
    bool fragmented_{false};
    bool reassembled_{false};
    bool decompressed_{false};
    StockRuntimeReplayedPayloadKind kind_{
        StockRuntimeReplayedPayloadKind::ordinary};
    std::size_t corpus_observed_ordinal_{0U};
    std::size_t delivery_ordinal_{0U};
    std::size_t source_fragment_count_{0U};
    std::vector<std::byte> bytes_;
};

class StockRuntimeTransportReplayState final {
public:
    StockRuntimeTransportReplayState(const StockRuntimeTransportReplayState&) = default;
    StockRuntimeTransportReplayState& operator=(
        const StockRuntimeTransportReplayState&) = delete;
    StockRuntimeTransportReplayState(StockRuntimeTransportReplayState&&) noexcept = default;
    StockRuntimeTransportReplayState& operator=(
        StockRuntimeTransportReplayState&&) noexcept = delete;
    ~StockRuntimeTransportReplayState() = default;

    [[nodiscard]] StockRuntimeConnectionReplayState connection_state() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeReplayedPayload>& payloads() const noexcept;
    [[nodiscard]] std::size_t connectionless_datagram_count() const noexcept;
    [[nodiscard]] std::size_t sequenced_client_to_server_count() const noexcept;
    [[nodiscard]] std::size_t sequenced_server_to_client_count() const noexcept;
    [[nodiscard]] std::size_t duplicate_packet_count() const noexcept;
    [[nodiscard]] std::size_t old_packet_count() const noexcept;
    [[nodiscard]] std::size_t fragment_packet_count() const noexcept;
    [[nodiscard]] std::size_t reassembled_payload_count() const noexcept;
    [[nodiscard]] std::size_t decompressed_payload_count() const noexcept;
    [[nodiscard]] std::size_t dropped_observation_count() const noexcept;

private:
    friend class StockRuntimeTransportReplay;
    friend class StockRuntimeTransportReplayEngine;

    StockRuntimeTransportReplayState(
        std::vector<StockRuntimeReplayedPayload> payloads,
        std::size_t connectionless_datagram_count,
        std::size_t sequenced_client_to_server_count,
        std::size_t sequenced_server_to_client_count,
        std::size_t duplicate_packet_count,
        std::size_t old_packet_count,
        std::size_t fragment_packet_count,
        std::size_t reassembled_payload_count,
        std::size_t decompressed_payload_count,
        std::size_t dropped_observation_count) noexcept;

    std::vector<StockRuntimeReplayedPayload> payloads_;
    std::size_t connectionless_datagram_count_{0U};
    std::size_t sequenced_client_to_server_count_{0U};
    std::size_t sequenced_server_to_client_count_{0U};
    std::size_t duplicate_packet_count_{0U};
    std::size_t old_packet_count_{0U};
    std::size_t fragment_packet_count_{0U};
    std::size_t reassembled_payload_count_{0U};
    std::size_t decompressed_payload_count_{0U};
    std::size_t dropped_observation_count_{0U};
};

enum class StockRuntimeTransportReplayErrorCode {
    invalid_configuration,
    capture_incomplete,
    delivery_ordinal_mismatch,
    datagram_too_large,
    delivered_datagram_budget_exceeded,
    malformed_datagram,
    unsupported_special_datagram,
    unexpected_connectionless_message,
    challenge_response_invalid,
    connect_request_invalid,
    challenge_mismatch,
    connect_response_invalid,
    connection_rejected,
    connection_not_established,
    netchan_header_invalid,
    sequence_half_range_ambiguous,
    netchan_packet_invalid,
    unsupported_secondary_stream,
    fragment_reassembly_failed,
    decompression_failed,
    replay_payload_limit_exceeded,
    allocation_failed,
};

struct StockRuntimeTransportReplayError final {
    StockRuntimeTransportReplayErrorCode code{
        StockRuntimeTransportReplayErrorCode::invalid_configuration};
    std::size_t delivery_ordinal{0U};
    std::string context;
    std::optional<NetchanPacketErrorCode> packet_code;
    std::optional<NetchanReassemblyErrorCode> reassembly_code;
    std::optional<ServicePayloadEnvelopeErrorCode> envelope_code;
};

struct StockRuntimeTransportReplayResult final {
    std::optional<StockRuntimeTransportReplayState> state;
    std::optional<StockRuntimeTransportReplayError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class StockRuntimeTransportReplay final {
public:
    explicit StockRuntimeTransportReplay(
        StockRuntimeTransportReplayLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockRuntimeTransportReplayLimits& limits() const noexcept;

    [[nodiscard]] StockRuntimeTransportReplayResult replay(
        const StockRuntimeCaptureCorpusState& corpus) const;

    // Test/research adapter. It is still pure offline replay: no socket, timer,
    // ACK generation, request generation, or network write exists in this API.
    [[nodiscard]] StockRuntimeTransportReplayResult replay(
        std::span<const StockRuntimeTransportReplayDatagram> delivered_datagrams,
        std::size_t dropped_observation_count = 0U) const;

private:
    StockRuntimeTransportReplayLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeTransportReplayErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeTransportReplayErrorCode::invalid_configuration: return "invalid_configuration";
    case StockRuntimeTransportReplayErrorCode::capture_incomplete: return "capture_incomplete";
    case StockRuntimeTransportReplayErrorCode::delivery_ordinal_mismatch: return "delivery_ordinal_mismatch";
    case StockRuntimeTransportReplayErrorCode::datagram_too_large: return "datagram_too_large";
    case StockRuntimeTransportReplayErrorCode::delivered_datagram_budget_exceeded: return "delivered_datagram_budget_exceeded";
    case StockRuntimeTransportReplayErrorCode::malformed_datagram: return "malformed_datagram";
    case StockRuntimeTransportReplayErrorCode::unsupported_special_datagram: return "unsupported_special_datagram";
    case StockRuntimeTransportReplayErrorCode::unexpected_connectionless_message: return "unexpected_connectionless_message";
    case StockRuntimeTransportReplayErrorCode::challenge_response_invalid: return "challenge_response_invalid";
    case StockRuntimeTransportReplayErrorCode::connect_request_invalid: return "connect_request_invalid";
    case StockRuntimeTransportReplayErrorCode::challenge_mismatch: return "challenge_mismatch";
    case StockRuntimeTransportReplayErrorCode::connect_response_invalid: return "connect_response_invalid";
    case StockRuntimeTransportReplayErrorCode::connection_rejected: return "connection_rejected";
    case StockRuntimeTransportReplayErrorCode::connection_not_established: return "connection_not_established";
    case StockRuntimeTransportReplayErrorCode::netchan_header_invalid: return "netchan_header_invalid";
    case StockRuntimeTransportReplayErrorCode::sequence_half_range_ambiguous: return "sequence_half_range_ambiguous";
    case StockRuntimeTransportReplayErrorCode::netchan_packet_invalid: return "netchan_packet_invalid";
    case StockRuntimeTransportReplayErrorCode::unsupported_secondary_stream: return "unsupported_secondary_stream";
    case StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed: return "fragment_reassembly_failed";
    case StockRuntimeTransportReplayErrorCode::decompression_failed: return "decompression_failed";
    case StockRuntimeTransportReplayErrorCode::replay_payload_limit_exceeded: return "replay_payload_limit_exceeded";
    case StockRuntimeTransportReplayErrorCode::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
