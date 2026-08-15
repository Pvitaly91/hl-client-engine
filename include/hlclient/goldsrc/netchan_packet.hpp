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
// accepting it as a normal sequence word. The current profile does not decode
// split packets.
inline constexpr std::uint32_t kUnsupportedNetchanSplitPacketMarker = 0xffff'fffeU;
inline constexpr std::size_t kNetchanFragmentSlotCount = 2U;
// Stock-capture facts for the decoded fragment descriptor boundary. These are
// wire-profile values, not configurable project safety maxima.
inline constexpr std::size_t kStockProtocol48FragmentPresenceSize = 1U;
inline constexpr std::size_t kStockProtocol48FragmentIdSize = 4U;
inline constexpr std::size_t kStockProtocol48FragmentOffsetSize = 2U;
inline constexpr std::size_t kStockProtocol48FragmentLengthSize = 2U;
inline constexpr std::size_t kStockProtocol48PresentFragmentDescriptorSize =
    kStockProtocol48FragmentPresenceSize + kStockProtocol48FragmentIdSize +
    kStockProtocol48FragmentOffsetSize + kStockProtocol48FragmentLengthSize;
// Repeated fresh stock captures confirm 1,024-byte non-final normal chunks.
// This is not a claim about the stock engine's absolute hard maximum.
inline constexpr std::size_t kStockProtocol48NormalFragmentChunkSize = 1'024U;
inline constexpr std::size_t kNetchanPacketDiagnosticTextLimit = 256U;

enum class NetchanDatagramClassification {
    connectionless,
    sequenced,
    unsupported_special,
    malformed,
};

enum class NetchanDirection {
    server_to_client,
    client_to_server,
};

enum class NetchanFragmentStream : std::uint8_t {
    normal = 0U,
    unconfirmed_slot_1 = 1U,
    slot_0 = normal,
    slot_1 = unconfirmed_slot_1,
};

// Immutable wire facts established by two accepted signed-stock Protocol 48
// baselines. Slot 1/file semantics, compression conventions, and client
// multi-fragment scheduling remain outside this confirmed profile.
struct StockProtocol48FragmentProfile final {
    static constexpr std::size_t stream_count = kNetchanFragmentSlotCount;
    static constexpr NetchanFragmentStream normal_stream =
        NetchanFragmentStream::normal;
    static constexpr std::size_t presence_width =
        kStockProtocol48FragmentPresenceSize;
    static constexpr std::size_t packed_id_width =
        kStockProtocol48FragmentIdSize;
    static constexpr std::size_t packet_payload_offset_width =
        kStockProtocol48FragmentOffsetSize;
    static constexpr std::size_t packet_payload_length_width =
        kStockProtocol48FragmentLengthSize;
    static constexpr std::uint32_t fragment_sequence_flag_mask =
        kNetchanFragmentSequenceFlag;
    static constexpr std::uint32_t reliable_sequence_flag_mask =
        kNetchanReliableSequenceFlag;
    static constexpr std::size_t normal_fragment_chunk_size =
        kStockProtocol48NormalFragmentChunkSize;
    static constexpr bool has_stable_wire_transfer_id = false;
    static constexpr bool reliable_admission_is_per_fragment = true;
    static constexpr bool allows_contemporaneous_payload_suffix = true;
    static constexpr bool completion_requires_all_packed_ordinals = true;
    static constexpr bool retransmission_preserves_packed_ordinal = true;
    static constexpr bool retransmission_preserves_packet_payload_range = true;
    static constexpr bool retransmission_uses_fresh_packet_sequence = true;
};

// The captured 32-bit descriptor field packs a one-based fragment index in its
// high 16 bits and the declared fragment count in its low 16 bits. It changes
// for each fragment and therefore must not be confused with a stable, separate
// on-wire message identifier.
class NetchanPackedFragmentId final {
public:
    [[nodiscard]] static constexpr std::optional<NetchanPackedFragmentId> from_wire(
        const std::uint32_t value) noexcept
    {
        const auto index = static_cast<std::uint16_t>(value >> 16U);
        const auto count = static_cast<std::uint16_t>(value & 0xffffU);
        if (index == 0U || count == 0U || index > count) {
            return std::nullopt;
        }
        return NetchanPackedFragmentId{index, count};
    }

    [[nodiscard]] constexpr std::uint16_t fragment_index() const noexcept
    {
        return fragment_index_;
    }

    [[nodiscard]] constexpr std::uint16_t fragment_count() const noexcept
    {
        return fragment_count_;
    }

    [[nodiscard]] constexpr std::uint32_t wire_value() const noexcept
    {
        return (static_cast<std::uint32_t>(fragment_index_) << 16U) |
               static_cast<std::uint32_t>(fragment_count_);
    }

    friend constexpr bool operator==(
        const NetchanPackedFragmentId& left,
        const NetchanPackedFragmentId& right) noexcept = default;

private:
    constexpr NetchanPackedFragmentId(
        const std::uint16_t fragment_index,
        const std::uint16_t fragment_count) noexcept
        : fragment_index_{fragment_index}, fragment_count_{fragment_count}
    {
    }

    std::uint16_t fragment_index_{1U};
    std::uint16_t fragment_count_{1U};
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
// parsing a duplicate or old body. The strict direction-specific decoders still
// reject the unconfirmed acknowledgement bit 30 for admitted newer packets.
struct NetchanHeaderPeek {
    NetchanHeader header;
    bool reserved_acknowledgement_flag{false};
};

// Slot numbers are deliberately neutral. Stock capture confirms two ordered
// descriptor slots, but M2.3.1 only recognizes their strict wire boundary.
struct NetchanFragmentDescriptor {
    std::uint8_t slot_index{0U};
    std::uint32_t fragment_id{0U};
    std::uint16_t offset{0U};
    std::uint16_t length{0U};
    std::size_t payload_offset{0U};

    [[nodiscard]] constexpr std::optional<NetchanFragmentStream> stream() const noexcept
    {
        if (slot_index >= kNetchanFragmentSlotCount) {
            return std::nullopt;
        }
        return static_cast<NetchanFragmentStream>(slot_index);
    }

    [[nodiscard]] constexpr std::optional<NetchanPackedFragmentId>
    packed_id() const noexcept
    {
        return NetchanPackedFragmentId::from_wire(fragment_id);
    }

    [[nodiscard]] constexpr std::size_t packet_payload_offset() const noexcept
    {
        return static_cast<std::size_t>(offset);
    }

    [[nodiscard]] constexpr std::size_t packet_payload_length() const noexcept
    {
        return static_cast<std::size_t>(length);
    }
};

using NetchanFragmentSlots =
    std::array<std::optional<NetchanFragmentDescriptor>, kNetchanFragmentSlotCount>;

struct ServerToClientNetchanPacket {
    NetchanHeader header;
    NetchanFragmentSlots fragments;
    std::vector<std::byte> payload;
    // For fragmented packets descriptor ranges consume the contiguous prefix
    // [0, fragment_payload_size). Fresh stock C2S capture proves that the owning
    // suffix [fragment_payload_size, payload.size()) may carry contemporaneous
    // non-fragment bytes and must be preserved.
    std::size_t fragment_payload_size{0U};
};

struct ClientToServerNetchanPacket {
    NetchanHeader header;
    NetchanFragmentSlots fragments;
    std::vector<std::byte> payload;
    std::size_t fragment_payload_size{0U};
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
    fragment_flag_required,
    invalid_fragment_presence,
    fragment_descriptor_truncated,
    fragment_flag_without_descriptor,
    descriptors_without_fragment_flag,
    invalid_fragment_id,
    invalid_fragment_count,
    invalid_fragment_index,
    invalid_fragment_offset,
    invalid_fragment_length,
    fragment_range_overflow,
    fragment_payload_truncated,
    fragment_payload_out_of_bounds,
    fragment_payload_overlap,
    unsupported_fragment_descriptor_variant,
    unsupported_fragment_compression,
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

struct NetchanDecodedFragmentPacket {
    NetchanFragmentSlots fragments;
    // Complete decoded bytes after the ordered descriptor area. Descriptor
    // offsets index this owning area.
    std::vector<std::byte> payload;
    // End of the validated contiguous descriptor-owned prefix. The remaining
    // owning suffix is contemporaneous non-fragment payload.
    std::size_t fragment_payload_size{0U};
};

using NetchanFragmentBodyDecodeResult =
    NetchanPacketDecodeResult<NetchanDecodedFragmentPacket>;

struct NetchanFragmentBodyEncodeResult {
    std::optional<std::vector<std::byte>> decoded_body;
    std::optional<NetchanPacketError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return decoded_body.has_value();
    }
};

[[nodiscard]] NetchanDatagramClassificationResult classify_netchan_datagram(
    std::span<const std::byte> datagram) noexcept;

[[nodiscard]] NetchanHeaderPeekResult peek_netchan_header(
    std::span<const std::byte> datagram);

// Pure post-transform descriptor codec. The caller retains responsibility for
// endpoint, datagram classification, header sequence/ACK inspection, and the
// offset-8 payload transform. These functions never mutate NetchanSession.
[[nodiscard]] NetchanFragmentBodyDecodeResult decode_netchan_fragment_body(
    NetchanDirection direction,
    const NetchanHeader& header,
    std::span<const std::byte> decoded_body,
    NetchanPacketLimits limits = {});

[[nodiscard]] NetchanFragmentBodyEncodeResult encode_netchan_fragment_body(
    NetchanDirection direction,
    const NetchanHeader& header,
    const NetchanFragmentSlots& fragments,
    std::span<const std::byte> payload,
    NetchanPacketLimits limits = {});

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
    case NetchanPacketErrorCode::fragment_flag_required:
        return "fragment_flag_required";
    case NetchanPacketErrorCode::invalid_fragment_presence:
        return "invalid_fragment_presence";
    case NetchanPacketErrorCode::fragment_descriptor_truncated:
        return "fragment_descriptor_truncated";
    case NetchanPacketErrorCode::fragment_flag_without_descriptor:
        return "fragment_flag_without_descriptor";
    case NetchanPacketErrorCode::descriptors_without_fragment_flag:
        return "descriptors_without_fragment_flag";
    case NetchanPacketErrorCode::invalid_fragment_id:
        return "invalid_fragment_id";
    case NetchanPacketErrorCode::invalid_fragment_count:
        return "invalid_fragment_count";
    case NetchanPacketErrorCode::invalid_fragment_index:
        return "invalid_fragment_index";
    case NetchanPacketErrorCode::invalid_fragment_offset:
        return "invalid_fragment_offset";
    case NetchanPacketErrorCode::invalid_fragment_length:
        return "invalid_fragment_length";
    case NetchanPacketErrorCode::fragment_range_overflow:
        return "fragment_range_overflow";
    case NetchanPacketErrorCode::fragment_payload_truncated:
        return "fragment_payload_truncated";
    case NetchanPacketErrorCode::fragment_payload_out_of_bounds:
        return "fragment_payload_out_of_bounds";
    case NetchanPacketErrorCode::fragment_payload_overlap:
        return "fragment_payload_overlap";
    case NetchanPacketErrorCode::unsupported_fragment_descriptor_variant:
        return "unsupported_fragment_descriptor_variant";
    case NetchanPacketErrorCode::unsupported_fragment_compression:
        return "unsupported_fragment_compression";
    case NetchanPacketErrorCode::packet_too_large:
        return "packet_too_large";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
