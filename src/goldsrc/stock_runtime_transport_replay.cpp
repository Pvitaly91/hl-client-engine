#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>

#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_response.hpp>
#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <ranges>
#include <utility>
#include <variant>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] StockRuntimeTransportReplayResult replay_failure(
    const StockRuntimeTransportReplayErrorCode code,
    const std::size_t delivery_ordinal,
    std::string context,
    const std::optional<NetchanPacketErrorCode> packet_code = std::nullopt,
    const std::optional<NetchanReassemblyErrorCode> reassembly_code = std::nullopt,
    const std::optional<ServicePayloadEnvelopeErrorCode> envelope_code = std::nullopt)
{
    return {
        std::nullopt,
        StockRuntimeTransportReplayError{
            code, delivery_ordinal, std::move(context), packet_code,
            reassembly_code, envelope_code},
    };
}

[[nodiscard]] bool valid_replay_limits(
    const StockRuntimeTransportReplayLimits& limits) noexcept
{
    return limits.maximum_delivered_datagrams > 0U &&
           limits.maximum_delivered_datagrams <=
               StockRuntimeCaptureHardCaps::maximum_datagrams * 2U &&
           limits.maximum_replayed_payloads > 0U &&
           limits.maximum_replayed_payloads <= 131'072U &&
           limits.maximum_datagram_bytes >= kNetchanHeaderSize &&
           limits.maximum_datagram_bytes <= kMaximumNetchanDatagramSize &&
           limits.maximum_total_delivered_datagram_bytes > 0U &&
           limits.maximum_total_delivered_datagram_bytes <=
               512U * 1'024U * 1'024U &&
           limits.maximum_reassembled_bytes > 0U &&
           limits.maximum_reassembled_bytes <= kMaximumNetchanNormalTransferSize &&
           limits.maximum_decompressed_bytes > 0U &&
           limits.maximum_decompressed_bytes <=
               kMaximumDecompressedServicePayloadSize &&
           limits.maximum_total_replayed_payload_bytes > 0U &&
           limits.maximum_total_replayed_payload_bytes <=
               512U * 1'024U * 1'024U &&
           limits.maximum_fragments_per_transfer > 0U &&
           limits.maximum_fragments_per_transfer <=
               kMaximumNetchanFragmentsPerTransfer;
}

[[nodiscard]] NetchanDirection netchan_direction(
    const StockRuntimeCaptureDirection direction) noexcept
{
    return direction == StockRuntimeCaptureDirection::client_to_server
               ? NetchanDirection::client_to_server
               : NetchanDirection::server_to_client;
}

[[nodiscard]] bool exact_connectionless_text(
    const std::span<const std::byte> bytes,
    const std::string_view text) noexcept
{
    const auto expected = std::as_bytes(std::span{text.data(), text.size()});
    return std::ranges::equal(bytes, expected);
}

struct ReplayDatagramView final {
    StockRuntimeCaptureDirection direction{
        StockRuntimeCaptureDirection::client_to_server};
    std::size_t delivery_ordinal{0U};
    std::size_t observed_ordinal{0U};
    std::size_t direction_ordinal{1U};
    std::uint64_t observed_relative_timestamp_us{0U};
    std::span<const std::byte> bytes;
};

} // namespace

class StockRuntimeTransportReplayEngine final {
public:
    StockRuntimeTransportReplayEngine(
        const StockRuntimeTransportReplayLimits& limits,
        const std::span<const ReplayDatagramView> delivered,
        const std::size_t dropped_observation_count)
        : limits_{limits},
          delivered_{delivered},
          dropped_observation_count_{dropped_observation_count},
          client_reassembler_{reassembly_limits()},
          server_reassembler_{reassembly_limits()},
          envelope_decoder_{ServicePayloadEnvelopeLimits{
              limits.maximum_decompressed_bytes}}
    {
    }

    [[nodiscard]] StockRuntimeTransportReplayResult run()
    {
        if (!valid_replay_limits(limits_) ||
            !client_reassembler_.valid_configuration() ||
            !server_reassembler_.valid_configuration() ||
            !envelope_decoder_.valid_configuration()) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::invalid_configuration,
                0U, "offline transport replay limits are invalid");
        }
        if (delivered_.empty() ||
            delivered_.size() > limits_.maximum_delivered_datagrams) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::capture_incomplete,
                0U, "delivered corpus is empty or exceeds its bound");
        }
        try {
            payloads_.reserve((std::min)(
                delivered_.size(), limits_.maximum_replayed_payloads));
        } catch (...) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::allocation_failed,
                0U, "offline replay payload allocation failed");
        }
        for (std::size_t index = 0U; index < delivered_.size(); ++index) {
            if (delivered_[index].delivery_ordinal != index) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::delivery_ordinal_mismatch,
                    index, "delivered ordinals are not contiguous and zero based");
            }
            auto processed = process(delivered_[index]);
            if (!processed) return processed;
        }
        if (phase_ != ConnectionPhase::accepted) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::connection_not_established,
                delivered_.size(),
                "delivered corpus never reached an observed connection ACCEPT");
        }
        if (client_reassembler_.active_transfer() ||
            server_reassembler_.active_transfer()) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed,
                delivered_.size(),
                "delivered corpus ends with an incomplete normal fragment transfer");
        }
        return StockRuntimeTransportReplayResult{
            StockRuntimeTransportReplayState{
                std::move(payloads_), connectionless_count_, sequenced_c2s_,
                sequenced_s2c_, duplicate_count_, old_count_, fragment_count_,
                reassembled_count_, decompressed_count_,
                dropped_observation_count_},
            std::nullopt,
        };
    }

private:
    enum class ConnectionPhase {
        awaiting_getchallenge,
        awaiting_challenge_response,
        awaiting_connect,
        awaiting_accept,
        accepted,
    };

    [[nodiscard]] NetchanReassemblyLimits reassembly_limits() const noexcept
    {
        return NetchanReassemblyLimits{
            kStockProtocol48NormalFragmentChunkSize,
            limits_.maximum_reassembled_bytes,
            limits_.maximum_fragments_per_transfer,
            1U,
            limits_.maximum_fragments_per_transfer,
            kMaximumNetchanFragmentTimeout,
        };
    }

    [[nodiscard]] StockRuntimeTransportReplayResult process(
        const ReplayDatagramView& datagram)
    {
        if (datagram.bytes.size() > limits_.maximum_datagram_bytes) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::datagram_too_large,
                datagram.delivery_ordinal,
                "delivered datagram exceeds the offline replay bound");
        }
        const auto classification = classify_netchan_datagram(datagram.bytes);
        switch (classification.classification) {
        case NetchanDatagramClassification::connectionless:
            return process_connectionless(datagram);
        case NetchanDatagramClassification::sequenced:
            return process_sequenced(datagram);
        case NetchanDatagramClassification::unsupported_special:
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::unsupported_special_datagram,
                datagram.delivery_ordinal,
                "offline replay does not admit split or special datagrams");
        case NetchanDatagramClassification::malformed:
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::malformed_datagram,
                datagram.delivery_ordinal,
                "delivered datagram has no valid Protocol 48 classifier");
        }
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::malformed_datagram,
            datagram.delivery_ordinal, "datagram classification is invalid");
    }

    [[nodiscard]] StockRuntimeTransportReplayResult process_connectionless(
        const ReplayDatagramView& datagram)
    {
        ++connectionless_count_;
        const auto packet = parse_connectionless_packet(
            datagram.bytes, limits_.maximum_datagram_bytes);
        if (!packet || !packet.packet) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::unexpected_connectionless_message,
                datagram.delivery_ordinal,
                "connectionless datagram failed the existing bounded codec");
        }

        switch (phase_) {
        case ConnectionPhase::awaiting_getchallenge:
            if (datagram.direction !=
                    StockRuntimeCaptureDirection::client_to_server ||
                !exact_connectionless_text(
                    packet.packet->payload, "getchallenge steam\n")) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::unexpected_connectionless_message,
                    datagram.delivery_ordinal,
                    "first connectionless message is not exact getchallenge");
            }
            phase_ = ConnectionPhase::awaiting_challenge_response;
            return success_marker();

        case ConnectionPhase::awaiting_challenge_response: {
            if (datagram.direction !=
                StockRuntimeCaptureDirection::server_to_client) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::challenge_response_invalid,
                    datagram.delivery_ordinal,
                    "challenge response has the wrong direction");
            }
            const auto parsed = parse_challenge_response(datagram.bytes);
            if (!parsed || !parsed.response) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::challenge_response_invalid,
                    datagram.delivery_ordinal,
                    "challenge response failed the existing codec");
            }
            challenge_ = parsed.response->challenge;
            phase_ = ConnectionPhase::awaiting_connect;
            return success_marker();
        }

        case ConnectionPhase::awaiting_connect: {
            if (datagram.direction !=
                StockRuntimeCaptureDirection::client_to_server) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::connect_request_invalid,
                    datagram.delivery_ordinal,
                    "connect request has the wrong direction");
            }
            auto parsed = parse_connect_request(datagram.bytes);
            if (!parsed || !parsed.request) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::connect_request_invalid,
                    datagram.delivery_ordinal,
                    "connect request failed the existing bounded codec");
            }
            if (!challenge_ || parsed.request->challenge() != *challenge_) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::challenge_mismatch,
                    datagram.delivery_ordinal,
                    "connect request does not reference the observed challenge");
            }
            // parsed.request owns opaque authentication only for this lexical
            // validation scope. No replay state, diagnostic, or hash retains it.
            phase_ = ConnectionPhase::awaiting_accept;
            return success_marker();
        }

        case ConnectionPhase::awaiting_accept: {
            if (datagram.direction !=
                StockRuntimeCaptureDirection::server_to_client) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::connect_response_invalid,
                    datagram.delivery_ordinal,
                    "connect response has the wrong direction");
            }
            const auto parsed = parse_connect_response(datagram.bytes);
            if (!parsed || !parsed.response) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::connect_response_invalid,
                    datagram.delivery_ordinal,
                    "connect response failed the existing bounded codec");
            }
            if (std::holds_alternative<ConnectRejected>(*parsed.response)) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::connection_rejected,
                    datagram.delivery_ordinal,
                    "observed connection response was REJECT");
            }
            phase_ = ConnectionPhase::accepted;
            return success_marker();
        }

        case ConnectionPhase::accepted:
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::unexpected_connectionless_message,
                datagram.delivery_ordinal,
                "connectionless message appeared after sequenced session acceptance");
        }
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::unexpected_connectionless_message,
            datagram.delivery_ordinal, "connection replay phase is invalid");
    }

    [[nodiscard]] StockRuntimeTransportReplayResult process_sequenced(
        const ReplayDatagramView& datagram)
    {
        if (phase_ != ConnectionPhase::accepted) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::connection_not_established,
                datagram.delivery_ordinal,
                "sequenced datagram precedes observed connection acceptance");
        }
        const auto header = peek_netchan_header(datagram.bytes);
        if (!header || !header.packet) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::netchan_header_invalid,
                datagram.delivery_ordinal,
                "sequenced header failed the existing pure codec",
                header.error ? std::optional{header.error->code} : std::nullopt);
        }
        auto& last_sequence =
            datagram.direction == StockRuntimeCaptureDirection::client_to_server
                ? last_c2s_sequence_
                : last_s2c_sequence_;
        if (last_sequence) {
            switch (compare_sequences(
                header.packet->header.sequence.sequence, *last_sequence)) {
            case NetchanSequenceComparison::equal:
                ++duplicate_count_;
                return success_marker();
            case NetchanSequenceComparison::older:
                ++old_count_;
                return success_marker();
            case NetchanSequenceComparison::half_range_ambiguous:
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::sequence_half_range_ambiguous,
                    datagram.delivery_ordinal,
                    "sequenced packet is exactly half a sequence range away");
            case NetchanSequenceComparison::newer:
                break;
            }
        }

        if (datagram.direction == StockRuntimeCaptureDirection::client_to_server) {
            const auto decoded = decode_client_to_server_netchan_packet(
                datagram.bytes, NetchanPacketLimits{limits_.maximum_datagram_bytes});
            if (!decoded || !decoded.packet) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::netchan_packet_invalid,
                    datagram.delivery_ordinal,
                    "C-to-S packet failed transform/descriptor decoding",
                    decoded.error ? std::optional{decoded.error->code} : std::nullopt);
            }
            auto processed = process_packet(
                *decoded.packet, datagram, client_reassembler_);
            if (!processed) return processed;
            ++sequenced_c2s_;
        } else {
            const auto decoded = decode_server_to_client_netchan_packet(
                datagram.bytes, NetchanPacketLimits{limits_.maximum_datagram_bytes});
            if (!decoded || !decoded.packet) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::netchan_packet_invalid,
                    datagram.delivery_ordinal,
                    "S-to-C packet failed transform/descriptor decoding",
                    decoded.error ? std::optional{decoded.error->code} : std::nullopt);
            }
            auto processed = process_packet(
                *decoded.packet, datagram, server_reassembler_);
            if (!processed) return processed;
            ++sequenced_s2c_;
        }
        last_sequence = header.packet->header.sequence.sequence;
        return success_marker();
    }

    template<typename Packet>
    [[nodiscard]] StockRuntimeTransportReplayResult process_packet(
        const Packet& packet,
        const ReplayDatagramView& datagram,
        NetchanNormalReassembler& reassembler)
    {
        if (!packet.header.sequence.flags.fragmented) {
            return publish_payload(
                datagram, packet.header, false, false,
                StockRuntimeReplayedPayloadKind::ordinary, 0U,
                packet.payload);
        }
        ++fragment_count_;
        if (packet.fragments[1U]) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::unsupported_secondary_stream,
                datagram.delivery_ordinal,
                "fragment slot 1 remains secondary_stream_pending");
        }
        if (!packet.fragments[0U]) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed,
                datagram.delivery_ordinal,
                "fragmented packet lacks a normal-stream descriptor",
                NetchanPacketErrorCode::fragment_flag_without_descriptor);
        }
        const auto& descriptor = *packet.fragments[0U];
        const auto offset = descriptor.packet_payload_offset();
        const auto length = descriptor.packet_payload_length();
        if (offset > packet.fragment_payload_size ||
            length > packet.fragment_payload_size - offset) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed,
                datagram.delivery_ordinal,
                "fragment descriptor exceeds its decoded owning prefix",
                NetchanPacketErrorCode::fragment_payload_out_of_bounds);
        }
        const auto logical_time = NetchanFragmentTimePoint{} +
            std::chrono::microseconds{static_cast<std::int64_t>(
                datagram.delivery_ordinal + 1U)};
        auto prepared = reassembler.prepare_insert(
            descriptor,
            std::span<const std::byte>{packet.payload}.subspan(offset, length),
            logical_time);
        if (!prepared || !prepared.plan) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed,
                datagram.delivery_ordinal,
                "existing normal-stream reassembler rejected the fragment",
                std::nullopt,
                prepared.error ? std::optional{prepared.error->code} : std::nullopt);
        }
        auto inserted = reassembler.commit_insert(std::move(*prepared.plan));
        if (!inserted || !inserted.receipt) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed,
                datagram.delivery_ordinal,
                "prepared fragment insertion failed transactionally",
                std::nullopt,
                inserted.error ? std::optional{inserted.error->code} : std::nullopt);
        }
        const auto packed = descriptor.packed_id();
        const auto fragment_count = packed ? packed->fragment_count() : 0U;
        if (packet.payload.size() > packet.fragment_payload_size) {
            auto suffix = std::span<const std::byte>{packet.payload}.subspan(
                packet.fragment_payload_size);
            auto published = publish_payload(
                datagram, packet.header, true, false,
                StockRuntimeReplayedPayloadKind::contemporaneous_fragment_suffix,
                fragment_count, suffix);
            if (!published) return published;
        }
        if (inserted.completion) {
            ++reassembled_count_;
            auto published = publish_payload(
                datagram, packet.header, true, true,
                StockRuntimeReplayedPayloadKind::completed_normal_fragment_transfer,
                fragment_count, inserted.completion->payload);
            if (!published) return published;
        }
        return success_marker();
    }

    [[nodiscard]] StockRuntimeTransportReplayResult publish_payload(
        const ReplayDatagramView& datagram,
        const NetchanHeader& header,
        const bool fragmented,
        const bool reassembled,
        const StockRuntimeReplayedPayloadKind kind,
        const std::size_t source_fragment_count,
        const std::span<const std::byte> source_bytes)
    {
        if (payloads_.size() >= limits_.maximum_replayed_payloads ||
            source_bytes.size() > limits_.maximum_reassembled_bytes ||
            source_bytes.size() >
                limits_.maximum_total_replayed_payload_bytes -
                    retained_payload_bytes_) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::replay_payload_limit_exceeded,
                datagram.delivery_ordinal,
                "replayed payload exceeds its count, per-payload, or aggregate byte bound");
        }
        std::vector<std::byte> bytes;
        try {
            bytes.assign(source_bytes.begin(), source_bytes.end());
        } catch (...) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::allocation_failed,
                datagram.delivery_ordinal,
                "replayed payload allocation failed");
        }
        bool decompressed = false;
        if (bytes.size() >= kBzip2ServicePayloadEnvelopeMagic.size() &&
            std::ranges::equal(
                std::span<const std::byte>{bytes}.first(
                    kBzip2ServicePayloadEnvelopeMagic.size()),
                kBzip2ServicePayloadEnvelopeMagic)) {
            OwnedServicePayload payload;
            payload.bytes = std::move(bytes);
            payload.source_sequence = header.sequence.sequence.value();
            payload.source_acknowledgement =
                header.acknowledgement.sequence.value();
            payload.source_reliable = header.sequence.flags.reliable;
            payload.reassembled = reassembled;
            payload.decompressed = false;
            payload.acknowledgement_reliable = header.acknowledgement.reliable;
            payload.direction = netchan_direction(datagram.direction);
            const auto decoded = envelope_decoder_.decode(std::move(payload));
            if (!decoded || !decoded.envelope) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::decompression_failed,
                    datagram.delivery_ordinal,
                    "existing bounded in-memory BZip2 decoder rejected the payload",
                    std::nullopt, std::nullopt,
                    decoded.error ? std::optional{decoded.error->code} : std::nullopt);
            }
            bytes = std::move(decoded.envelope->payload.bytes);
            decompressed = true;
        }
        if (bytes.size() >
            limits_.maximum_total_replayed_payload_bytes -
                retained_payload_bytes_) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::replay_payload_limit_exceeded,
                datagram.delivery_ordinal,
                "decoded payload exceeds the aggregate retained-byte bound");
        }
        try {
            payloads_.push_back(StockRuntimeReplayedPayload{
                netchan_direction(datagram.direction),
                header.sequence.sequence.value(),
                header.acknowledgement.sequence.value(),
                header.acknowledgement.reliable,
                header.sequence.flags.reliable,
                fragmented, reassembled, decompressed, kind,
                datagram.observed_ordinal, datagram.delivery_ordinal,
                source_fragment_count, std::move(bytes)});
        } catch (...) {
            return replay_failure(
                StockRuntimeTransportReplayErrorCode::allocation_failed,
                datagram.delivery_ordinal,
                "replayed payload publication failed");
        }
        retained_payload_bytes_ += payloads_.back().bytes().size();
        if (decompressed) ++decompressed_count_;
        return success_marker();
    }

    [[nodiscard]] static StockRuntimeTransportReplayResult success_marker()
    {
        // Internal success marker; callers inspect only the absence of error.
        return StockRuntimeTransportReplayResult{
            StockRuntimeTransportReplayState{
                {}, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
            std::nullopt,
        };
    }

    const StockRuntimeTransportReplayLimits& limits_;
    std::span<const ReplayDatagramView> delivered_;
    std::size_t dropped_observation_count_{0U};
    ConnectionPhase phase_{ConnectionPhase::awaiting_getchallenge};
    std::optional<ChallengeToken> challenge_;
    std::optional<NetchanSequence> last_c2s_sequence_;
    std::optional<NetchanSequence> last_s2c_sequence_;
    NetchanNormalReassembler client_reassembler_;
    NetchanNormalReassembler server_reassembler_;
    ServicePayloadEnvelopeDecoder envelope_decoder_;
    std::vector<StockRuntimeReplayedPayload> payloads_;
    std::size_t retained_payload_bytes_{0U};
    std::size_t connectionless_count_{0U};
    std::size_t sequenced_c2s_{0U};
    std::size_t sequenced_s2c_{0U};
    std::size_t duplicate_count_{0U};
    std::size_t old_count_{0U};
    std::size_t fragment_count_{0U};
    std::size_t reassembled_count_{0U};
    std::size_t decompressed_count_{0U};
};

StockRuntimeReplayedPayload::StockRuntimeReplayedPayload(
    const NetchanDirection direction,
    const std::uint32_t source_sequence,
    const std::uint32_t source_acknowledgement,
    const bool acknowledgement_reliable,
    const bool reliable,
    const bool fragmented,
    const bool reassembled,
    const bool decompressed,
    const StockRuntimeReplayedPayloadKind kind,
    const std::size_t corpus_observed_ordinal,
    const std::size_t delivery_ordinal,
    const std::size_t source_fragment_count,
    std::vector<std::byte> bytes) noexcept
    : direction_{direction}, source_sequence_{source_sequence},
      source_acknowledgement_{source_acknowledgement},
      acknowledgement_reliable_{acknowledgement_reliable}, reliable_{reliable},
      fragmented_{fragmented}, reassembled_{reassembled},
      decompressed_{decompressed}, kind_{kind},
      corpus_observed_ordinal_{corpus_observed_ordinal},
      delivery_ordinal_{delivery_ordinal},
      source_fragment_count_{source_fragment_count}, bytes_{std::move(bytes)}
{
}

NetchanDirection StockRuntimeReplayedPayload::direction() const noexcept { return direction_; }
std::uint32_t StockRuntimeReplayedPayload::source_sequence() const noexcept { return source_sequence_; }
std::uint32_t StockRuntimeReplayedPayload::source_acknowledgement() const noexcept { return source_acknowledgement_; }
bool StockRuntimeReplayedPayload::acknowledgement_reliable() const noexcept { return acknowledgement_reliable_; }
bool StockRuntimeReplayedPayload::reliable() const noexcept { return reliable_; }
bool StockRuntimeReplayedPayload::fragmented() const noexcept { return fragmented_; }
bool StockRuntimeReplayedPayload::reassembled() const noexcept { return reassembled_; }
bool StockRuntimeReplayedPayload::decompressed() const noexcept { return decompressed_; }
StockRuntimeReplayedPayloadKind StockRuntimeReplayedPayload::kind() const noexcept { return kind_; }
std::size_t StockRuntimeReplayedPayload::corpus_observed_ordinal() const noexcept { return corpus_observed_ordinal_; }
std::size_t StockRuntimeReplayedPayload::delivery_ordinal() const noexcept { return delivery_ordinal_; }
std::size_t StockRuntimeReplayedPayload::source_fragment_count() const noexcept { return source_fragment_count_; }
std::span<const std::byte> StockRuntimeReplayedPayload::bytes() const noexcept { return bytes_; }

StockRuntimeTransportReplayState::StockRuntimeTransportReplayState(
    std::vector<StockRuntimeReplayedPayload> payloads,
    const std::size_t connectionless_datagram_count,
    const std::size_t sequenced_client_to_server_count,
    const std::size_t sequenced_server_to_client_count,
    const std::size_t duplicate_packet_count,
    const std::size_t old_packet_count,
    const std::size_t fragment_packet_count,
    const std::size_t reassembled_payload_count,
    const std::size_t decompressed_payload_count,
    const std::size_t dropped_observation_count) noexcept
    : payloads_{std::move(payloads)},
      connectionless_datagram_count_{connectionless_datagram_count},
      sequenced_client_to_server_count_{sequenced_client_to_server_count},
      sequenced_server_to_client_count_{sequenced_server_to_client_count},
      duplicate_packet_count_{duplicate_packet_count}, old_packet_count_{old_packet_count},
      fragment_packet_count_{fragment_packet_count},
      reassembled_payload_count_{reassembled_payload_count},
      decompressed_payload_count_{decompressed_payload_count},
      dropped_observation_count_{dropped_observation_count}
{
}

StockRuntimeConnectionReplayState
StockRuntimeTransportReplayState::connection_state() const noexcept
{
    return StockRuntimeConnectionReplayState::accepted;
}
const std::vector<StockRuntimeReplayedPayload>&
StockRuntimeTransportReplayState::payloads() const noexcept { return payloads_; }
std::size_t StockRuntimeTransportReplayState::connectionless_datagram_count() const noexcept { return connectionless_datagram_count_; }
std::size_t StockRuntimeTransportReplayState::sequenced_client_to_server_count() const noexcept { return sequenced_client_to_server_count_; }
std::size_t StockRuntimeTransportReplayState::sequenced_server_to_client_count() const noexcept { return sequenced_server_to_client_count_; }
std::size_t StockRuntimeTransportReplayState::duplicate_packet_count() const noexcept { return duplicate_packet_count_; }
std::size_t StockRuntimeTransportReplayState::old_packet_count() const noexcept { return old_packet_count_; }
std::size_t StockRuntimeTransportReplayState::fragment_packet_count() const noexcept { return fragment_packet_count_; }
std::size_t StockRuntimeTransportReplayState::reassembled_payload_count() const noexcept { return reassembled_payload_count_; }
std::size_t StockRuntimeTransportReplayState::decompressed_payload_count() const noexcept { return decompressed_payload_count_; }
std::size_t StockRuntimeTransportReplayState::dropped_observation_count() const noexcept { return dropped_observation_count_; }

StockRuntimeTransportReplay::StockRuntimeTransportReplay(
    StockRuntimeTransportReplayLimits limits) noexcept
    : limits_{std::move(limits)}
{
}

bool StockRuntimeTransportReplay::valid_configuration() const noexcept
{
    return valid_replay_limits(limits_);
}
const StockRuntimeTransportReplayLimits&
StockRuntimeTransportReplay::limits() const noexcept { return limits_; }

StockRuntimeTransportReplayResult StockRuntimeTransportReplay::replay(
    const StockRuntimeCaptureCorpusState& corpus) const
{
    if (!valid_configuration()) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::invalid_configuration, 0U,
            "offline transport replay limits are invalid");
    }
    if (!corpus.capture_metadata().bounded_transport_complete ||
        corpus.publication_state() ==
            StockRuntimeCaptureCorpusPublicationState::published_incomplete) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::capture_incomplete, 0U,
            "corpus is not bounded-transport complete");
    }
    if (corpus.delivered_datagrams().size() >
        limits_.maximum_delivered_datagrams) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::capture_incomplete, 0U,
            "delivered corpus exceeds its datagram-count bound");
    }
    std::vector<ReplayDatagramView> delivered;
    std::size_t delivered_bytes = 0U;
    try {
        delivered.reserve(corpus.delivered_datagrams().size());
        for (const auto& datagram : corpus.delivered_datagrams()) {
            const auto datagram_bytes = datagram.bytes();
            if (datagram_bytes.size() >
                limits_.maximum_total_delivered_datagram_bytes -
                    delivered_bytes) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::
                        delivered_datagram_budget_exceeded,
                    datagram.delivery_ordinal(),
                    "delivered corpus exceeds its aggregate raw-byte bound");
            }
            delivered.push_back(ReplayDatagramView{
                datagram.direction(), datagram.delivery_ordinal(),
                datagram.observed_ordinal(), datagram.direction_ordinal(),
                datagram.observed_relative_timestamp_us(),
                datagram_bytes,
            });
            delivered_bytes += datagram_bytes.size();
        }
    } catch (...) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::allocation_failed, 0U,
            "corpus-to-replay adapter allocation failed");
    }
    const auto dropped = std::ranges::count_if(
        corpus.observed_datagrams(), [](const auto& datagram) {
            return !datagram.journal().delivered;
        });
    StockRuntimeTransportReplayEngine engine{
        limits_, delivered, static_cast<std::size_t>(dropped)};
    return engine.run();
}

StockRuntimeTransportReplayResult StockRuntimeTransportReplay::replay(
    const std::span<const StockRuntimeTransportReplayDatagram> delivered_datagrams,
    const std::size_t dropped_observation_count) const
{
    if (!valid_configuration()) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::invalid_configuration, 0U,
            "offline transport replay limits are invalid");
    }
    if (delivered_datagrams.size() > limits_.maximum_delivered_datagrams) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::capture_incomplete, 0U,
            "delivered adapter exceeds its datagram-count bound");
    }
    std::vector<ReplayDatagramView> delivered;
    std::size_t delivered_bytes = 0U;
    try {
        delivered.reserve(delivered_datagrams.size());
        for (const auto& datagram : delivered_datagrams) {
            if (datagram.bytes.size() >
                limits_.maximum_total_delivered_datagram_bytes -
                    delivered_bytes) {
                return replay_failure(
                    StockRuntimeTransportReplayErrorCode::
                        delivered_datagram_budget_exceeded,
                    datagram.delivery_ordinal,
                    "delivered adapter exceeds its aggregate raw-byte bound");
            }
            delivered.push_back(ReplayDatagramView{
                datagram.direction, datagram.delivery_ordinal,
                datagram.observed_ordinal, datagram.direction_ordinal,
                datagram.observed_relative_timestamp_us, datagram.bytes});
            delivered_bytes += datagram.bytes.size();
        }
    } catch (...) {
        return replay_failure(
            StockRuntimeTransportReplayErrorCode::allocation_failed, 0U,
            "replay datagram-view allocation failed");
    }
    StockRuntimeTransportReplayEngine engine{
        limits_, delivered, dropped_observation_count};
    return engine.run();
}

} // namespace hlclient::goldsrc
