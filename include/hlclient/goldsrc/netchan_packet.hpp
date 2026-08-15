#pragma once

#include <hlclient/goldsrc/netchan_sequence.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Pure codec minimum. The current stock capture observed 16-byte or larger
// datagrams, but the confirmed direction-independent header itself is 8 bytes.
inline constexpr std::size_t kNetchanHeaderSize = 8U;
inline constexpr std::size_t kDefaultNetchanDatagramSize = 4'096U;
inline constexpr std::size_t kMaximumNetchanDatagramSize = 16'384U;
inline constexpr std::uint32_t kNetchanReliableAcknowledgementFlag = 0x8000'0000U;
inline constexpr std::uint32_t kNetchanReservedAcknowledgementFlag = 0x4000'0000U;
// Inferred safety boundary: isolate the conventional split marker rather than
// accepting it as a normal sequence word. M2.3 does not decode split packets.
inline constexpr std::uint32_t kUnsupportedNetchanSplitPacketMarker = 0xffff'fffeU;
inline constexpr std::size_t kNetchanFragmentSlotCount = 2U;
inline constexpr std::size_t kNetchanFragmentDescriptorFieldSize =
    sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t);
// A secondary stream is never retained or persisted in M2.3. These named
// observation ceilings are the largest decoded bytes that can follow one
// present descriptor inside the configured default/hard datagram bounds.
inline constexpr std::size_t kDefaultNetchanSecondaryStreamObservationBytes =
    kDefaultNetchanDatagramSize - kNetchanHeaderSize -
    kNetchanFragmentSlotCount - kNetchanFragmentDescriptorFieldSize;
inline constexpr std::size_t kMaximumNetchanSecondaryStreamObservationBytes =
    kMaximumNetchanDatagramSize - kNetchanHeaderSize -
    kNetchanFragmentSlotCount - kNetchanFragmentDescriptorFieldSize;
inline constexpr std::size_t kNetchanPacketDiagnosticTextLimit = 256U;

enum class NetchanDatagramClassification {
    connectionless,
    sequenced,
    unsupported_special,
    malformed,
};

struct NetchanDatagramClassificationResult {
    NetchanDatagramClassification classification{NetchanDatagramClassification::malformed};
    std::size_t byte_offset{0U};
};

struct NetchanAcknowledgementWord {
    NetchanSequence sequence;
    bool reliable{false};
};

struct NetchanHeader {
    NetchanSequenceWord sequence;
    NetchanAcknowledgementWord acknowledgement;
};

// A bounded, body-agnostic view used to apply numeric sequence policy before
// parsing a retransmitted body. The strict direction-specific decoders still
// reject the unconfirmed acknowledgement bit 30 for admitted newer packets.
struct NetchanHeaderPeek {
    NetchanHeader header;
    bool reserved_acknowledgement_flag{false};
};

// Slot numbers are deliberately neutral. Stock capture confirms two ordered
// descriptor slots, but M2.3 does not assign normal/file semantics to either.
struct NetchanFragmentDescriptor {
    std::uint8_t slot_index{0U};
    std::uint32_t fragment_id{0U};
    std::uint16_t offset{0U};
    std::uint16_t length{0U};
    std::size_t payload_offset{0U};
};

using NetchanFragmentSlots =
    std::array<std::optional<NetchanFragmentDescriptor>, kNetchanFragmentSlotCount>;

struct ServerToClientNetchanPacket {
    NetchanHeader header;
    NetchanFragmentSlots fragments;
    std::vector<std::byte> payload;
};

struct ClientToServerNetchanPacket {
    NetchanHeader header;
    NetchanFragmentSlots fragments;
    std::vector<std::byte> payload;
};

struct NetchanPacketLimits {
    std::size_t maximum_datagram_size{kDefaultNetchanDatagramSize};
};

enum class NetchanPacketErrorCode {
    invalid_configuration,
    datagram_too_short,
    datagram_too_large,
    connectionless_packet,
    unsupported_special_packet,
    reserved_sequence_word,
    reserved_acknowledgement_flag,
    invalid_fragment_presence,
    fragment_descriptor_truncated,
    fragment_flag_without_descriptor,
    descriptors_without_fragment_flag,
    invalid_fragment_slot,
    zero_fragment_length,
    fragment_payload_out_of_bounds,
    fragment_payload_overlap,
    fragment_payload_size_mismatch,
    packet_too_large,
};

struct NetchanPacketError {
    NetchanPacketErrorCode code{NetchanPacketErrorCode::datagram_too_short};
    std::size_t byte_offset{0U};
    std::string context;
};

template<typename Packet>
struct NetchanPacketDecodeResult {
    std::optional<Packet> packet;
    std::optional<NetchanPacketError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return packet.has_value();
    }
};

struct NetchanPacketEncodeResult {
    std::optional<std::vector<std::byte>> datagram;
    std::optional<NetchanPacketError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return datagram.has_value();
    }
};

using ServerToClientNetchanDecodeResult =
    NetchanPacketDecodeResult<ServerToClientNetchanPacket>;
using ClientToServerNetchanDecodeResult =
    NetchanPacketDecodeResult<ClientToServerNetchanPacket>;
using NetchanHeaderPeekResult = NetchanPacketDecodeResult<NetchanHeaderPeek>;

[[nodiscard]] NetchanDatagramClassificationResult classify_netchan_datagram(
    std::span<const std::byte> datagram) noexcept;

[[nodiscard]] NetchanHeaderPeekResult peek_netchan_header(
    std::span<const std::byte> datagram);

[[nodiscard]] ServerToClientNetchanDecodeResult decode_server_to_client_netchan_packet(
    std::span<const std::byte> datagram,
    NetchanPacketLimits limits = {});

[[nodiscard]] ClientToServerNetchanDecodeResult decode_client_to_server_netchan_packet(
    std::span<const std::byte> datagram,
    NetchanPacketLimits limits = {});

[[nodiscard]] NetchanPacketEncodeResult encode_server_to_client_netchan_packet(
    const ServerToClientNetchanPacket& packet,
    NetchanPacketLimits limits = {});

[[nodiscard]] NetchanPacketEncodeResult encode_client_to_server_netchan_packet(
    const ClientToServerNetchanPacket& packet,
    NetchanPacketLimits limits = {});

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanPacketErrorCode code) noexcept
{
    switch (code) {
    case NetchanPacketErrorCode::invalid_configuration:
        return "invalid_configuration";
    case NetchanPacketErrorCode::datagram_too_short:
        return "datagram_too_short";
    case NetchanPacketErrorCode::datagram_too_large:
        return "datagram_too_large";
    case NetchanPacketErrorCode::connectionless_packet:
        return "connectionless_packet";
    case NetchanPacketErrorCode::unsupported_special_packet:
        return "unsupported_special_packet";
    case NetchanPacketErrorCode::reserved_sequence_word:
        return "reserved_sequence_word";
    case NetchanPacketErrorCode::reserved_acknowledgement_flag:
        return "reserved_acknowledgement_flag";
    case NetchanPacketErrorCode::invalid_fragment_presence:
        return "invalid_fragment_presence";
    case NetchanPacketErrorCode::fragment_descriptor_truncated:
        return "fragment_descriptor_truncated";
    case NetchanPacketErrorCode::fragment_flag_without_descriptor:
        return "fragment_flag_without_descriptor";
    case NetchanPacketErrorCode::descriptors_without_fragment_flag:
        return "descriptors_without_fragment_flag";
    case NetchanPacketErrorCode::invalid_fragment_slot:
        return "invalid_fragment_slot";
    case NetchanPacketErrorCode::zero_fragment_length:
        return "zero_fragment_length";
    case NetchanPacketErrorCode::fragment_payload_out_of_bounds:
        return "fragment_payload_out_of_bounds";
    case NetchanPacketErrorCode::fragment_payload_overlap:
        return "fragment_payload_overlap";
    case NetchanPacketErrorCode::fragment_payload_size_mismatch:
        return "fragment_payload_size_mismatch";
    case NetchanPacketErrorCode::packet_too_large:
        return "packet_too_large";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
