#include <hlclient/goldsrc/netchan_packet.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/byte_writer.hpp>
#include <hlclient/goldsrc/connectionless_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

struct DecodedPacketParts {
    NetchanHeader header;
    NetchanFragmentSlots fragments;
    std::vector<std::byte> payload;
    std::size_t fragment_payload_size{0U};
};

[[nodiscard]] NetchanPacketError make_error(
    const NetchanPacketErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    if (context.size() > kNetchanPacketDiagnosticTextLimit) {
        context.resize(kNetchanPacketDiagnosticTextLimit);
    }
    return NetchanPacketError{code, byte_offset, std::move(context)};
}

template<typename Packet>
[[nodiscard]] NetchanPacketDecodeResult<Packet> decode_failure(
    const NetchanPacketErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return NetchanPacketDecodeResult<Packet>{
        std::nullopt,
        make_error(code, byte_offset, std::move(context)),
    };
}

[[nodiscard]] NetchanPacketEncodeResult encode_failure(
    const NetchanPacketErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return NetchanPacketEncodeResult{
        std::nullopt,
        make_error(code, byte_offset, std::move(context)),
    };
}

[[nodiscard]] bool valid_limits(const NetchanPacketLimits limits) noexcept
{
    return limits.maximum_datagram_size >= kNetchanHeaderSize &&
           limits.maximum_datagram_size <= kMaximumNetchanDatagramSize;
}

[[nodiscard]] std::optional<NetchanPacketError> validate_fragment_payload_ranges(
    const NetchanFragmentSlots& fragments,
    const std::size_t payload_size,
    const std::size_t error_offset,
    const bool decoding,
    std::size_t* const fragment_payload_size = nullptr)
{
    struct PayloadRange {
        std::size_t begin{0U};
        std::size_t end{0U};
    };

    std::array<PayloadRange, kNetchanFragmentSlotCount> ranges{};
    std::size_t range_count = 0U;
    for (std::size_t slot = 0U; slot < fragments.size(); ++slot) {
        if (!fragments[slot]) {
            continue;
        }
        const auto& descriptor = *fragments[slot];
        if (descriptor.slot_index != slot || !descriptor.stream()) {
            return make_error(
                NetchanPacketErrorCode::unsupported_fragment_descriptor_variant,
                error_offset,
                "Fragment descriptor slot identity does not match its ordered wire slot");
        }
        if (descriptor.fragment_id == 0U) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_id,
                error_offset,
                "Fragment descriptor packed identity is zero");
        }
        const auto fragment_index =
            static_cast<std::uint16_t>(descriptor.fragment_id >> 16U);
        const auto fragment_count =
            static_cast<std::uint16_t>(descriptor.fragment_id & 0xffffU);
        if (fragment_count == 0U) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_count,
                error_offset,
                "Fragment descriptor declares zero fragments");
        }
        if (fragment_index == 0U || fragment_index > fragment_count) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_index,
                error_offset,
                "Fragment descriptor index is outside its declared one-based count");
        }
        if (descriptor.length == 0U) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_length,
                error_offset,
                "Present fragment descriptor has zero payload length");
        }
        if (descriptor.offset >
            std::numeric_limits<std::uint16_t>::max() - descriptor.length) {
            return make_error(
                NetchanPacketErrorCode::fragment_range_overflow,
                error_offset,
                "Fragment descriptor offset plus length exceeds the 16-bit range");
        }
        if (descriptor.payload_offset != descriptor.offset) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_offset,
                error_offset,
                "Fragment descriptor payload offset is inconsistent with its wire offset");
        }
        const auto begin = static_cast<std::size_t>(descriptor.offset);
        const auto length = static_cast<std::size_t>(descriptor.length);
        if (begin > payload_size) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_offset,
                error_offset,
                "Netchan fragment offset is outside the decoded payload area");
        }
        if (length > payload_size - begin) {
            return make_error(
                decoding ? NetchanPacketErrorCode::fragment_payload_truncated
                         : NetchanPacketErrorCode::fragment_payload_out_of_bounds,
                error_offset,
                "Netchan fragment offset and length exceed the decoded payload area");
        }
        ranges[range_count++] = PayloadRange{begin, begin + length};
    }

    std::ranges::sort(
        std::span<PayloadRange>{ranges}.first(range_count),
        {},
        &PayloadRange::begin);
    if (range_count == 0U) {
        return make_error(
            NetchanPacketErrorCode::fragment_flag_without_descriptor,
            error_offset,
            "Fragmented netchan packet has no present fragment descriptor");
    }
    if (ranges[0].begin != 0U) {
        return make_error(
            NetchanPacketErrorCode::invalid_fragment_offset,
            error_offset,
            "First fragment descriptor range does not begin at payload offset zero");
    }
    for (std::size_t index = 1U; index < range_count; ++index) {
        if (ranges[index].begin < ranges[index - 1U].end) {
            return make_error(
                NetchanPacketErrorCode::fragment_payload_overlap,
                error_offset,
                "Netchan fragment descriptor payload ranges overlap");
        }
        if (ranges[index].begin != ranges[index - 1U].end) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_offset,
                error_offset,
                "Fragment descriptor ranges do not form one contiguous payload prefix");
        }
    }
    if (fragment_payload_size != nullptr) {
        *fragment_payload_size = ranges[range_count - 1U].end;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<NetchanPacketError> parse_fragment_descriptors(
    ByteReader& reader,
    NetchanFragmentSlots& fragments,
    std::vector<std::byte>& payload,
    std::size_t& fragment_payload_size)
{
    bool any_present = false;
    for (std::size_t slot = 0U; slot < fragments.size(); ++slot) {
        const auto presence_offset = kNetchanHeaderSize + reader.position();
        const auto present = reader.read_uint8();
        if (!present) {
            return make_error(
                NetchanPacketErrorCode::fragment_descriptor_truncated,
                presence_offset,
                "Netchan fragment descriptor presence byte is truncated");
        }
        if (*present > 1U) {
            return make_error(
                NetchanPacketErrorCode::invalid_fragment_presence,
                presence_offset,
                "Netchan fragment descriptor presence must be zero or one");
        }
        if (*present == 0U) {
            continue;
        }

        const auto descriptor_offset = kNetchanHeaderSize + reader.position();
        const auto fragment_id = reader.read_uint32_le();
        const auto offset = reader.read_uint16_le();
        const auto length = reader.read_uint16_le();
        if (!fragment_id || !offset || !length) {
            return make_error(
                NetchanPacketErrorCode::fragment_descriptor_truncated,
                descriptor_offset,
                "Netchan fragment descriptor fields are truncated");
        }
        fragments[slot] = NetchanFragmentDescriptor{
            static_cast<std::uint8_t>(slot),
            *fragment_id,
            *offset,
            *length,
            static_cast<std::size_t>(*offset),
        };
        any_present = true;
    }

    if (!any_present) {
        return make_error(
            NetchanPacketErrorCode::fragment_flag_without_descriptor,
            kNetchanHeaderSize,
            "Fragmented netchan packet has no present fragment descriptor");
    }

    const auto payload_wire_offset = kNetchanHeaderSize + reader.position();
    const auto remaining = reader.read_bytes(reader.remaining());
    if (!remaining) {
        return make_error(
            NetchanPacketErrorCode::fragment_payload_out_of_bounds,
            kNetchanHeaderSize + reader.position(),
            "Unable to read bounded netchan fragment payload bytes");
    }
    payload.assign(remaining->begin(), remaining->end());
    if (auto error = validate_fragment_payload_ranges(
            fragments,
            payload.size(),
            payload_wire_offset,
            true,
            &fragment_payload_size)) {
        return error;
    }
    return std::nullopt;
}

template<typename Packet>
[[nodiscard]] NetchanPacketDecodeResult<Packet> decode_packet(
    const std::span<const std::byte> datagram,
    const NetchanDirection direction,
    const NetchanPacketLimits limits)
{
    if (!valid_limits(limits)) {
        return decode_failure<Packet>(
            NetchanPacketErrorCode::invalid_configuration,
            0U,
            "Invalid netchan datagram size configuration");
    }
    if (datagram.size() > limits.maximum_datagram_size) {
        return decode_failure<Packet>(
            NetchanPacketErrorCode::datagram_too_large,
            limits.maximum_datagram_size,
            "Netchan packet exceeds the configured project size bound");
    }

    auto peeked = peek_netchan_header(datagram);
    if (!peeked || !peeked.packet) {
        return NetchanPacketDecodeResult<Packet>{
            std::nullopt,
            std::move(peeked.error),
        };
    }
    if (peeked.packet->reserved_acknowledgement_flag) {
        return decode_failure<Packet>(
            NetchanPacketErrorCode::reserved_acknowledgement_flag,
            sizeof(std::uint32_t),
            "Netchan acknowledgement uses the unconfirmed reserved bit 30");
    }

    DecodedPacketParts decoded{
        peeked.packet->header,
        {},
        {},
        0U,
    };

    std::vector<std::byte> decoded_body{
        datagram.begin() + static_cast<std::ptrdiff_t>(kNetchanHeaderSize),
        datagram.end()};
    decode_netchan_payload(decoded_body, decoded.header.sequence.sequence);

    if (decoded.header.sequence.flags.fragmented) {
        auto fragment = decode_netchan_fragment_body(
            direction,
            decoded.header,
            decoded_body,
            limits);
        if (!fragment || !fragment.packet) {
            return NetchanPacketDecodeResult<Packet>{
                std::nullopt,
                std::move(fragment.error),
            };
        }
        decoded.fragments = std::move(fragment.packet->fragments);
        decoded.payload = std::move(fragment.packet->payload);
        decoded.fragment_payload_size = fragment.packet->fragment_payload_size;
    } else {
        decoded.payload = std::move(decoded_body);
    }

    Packet packet{
        decoded.header,
        std::move(decoded.fragments),
        std::move(decoded.payload),
        decoded.fragment_payload_size,
    };
    return NetchanPacketDecodeResult<Packet>{std::move(packet), std::nullopt};
}

template<typename Packet>
[[nodiscard]] NetchanPacketEncodeResult encode_packet(
    const Packet& packet,
    const NetchanDirection direction,
    const NetchanPacketLimits limits)
{
    if (!valid_limits(limits)) {
        return encode_failure(
            NetchanPacketErrorCode::invalid_configuration,
            0U,
            "Invalid netchan datagram size configuration");
    }

    const auto sequence_wire = encode_netchan_sequence_word(
        packet.header.sequence.sequence,
        packet.header.sequence.flags);
    if (sequence_wire == kConnectionlessPacketHeader ||
        sequence_wire == kUnsupportedNetchanSplitPacketMarker) {
        return encode_failure(
            NetchanPacketErrorCode::reserved_sequence_word,
            0U,
            "Netchan sequence word collides with a reserved packet classifier marker");
    }

    const bool has_descriptors = std::ranges::any_of(
        packet.fragments,
        [](const auto& descriptor) { return descriptor.has_value(); });
    if (!packet.header.sequence.flags.fragmented && has_descriptors) {
        return encode_failure(
            NetchanPacketErrorCode::descriptors_without_fragment_flag,
            kNetchanHeaderSize,
            "Netchan packet has fragment descriptors without the fragment flag");
    }

    std::vector<std::byte> decoded_body;
    if (packet.header.sequence.flags.fragmented) {
        auto encoded_fragment = encode_netchan_fragment_body(
            direction,
            packet.header,
            packet.fragments,
            packet.payload,
            limits);
        if (!encoded_fragment || !encoded_fragment.decoded_body) {
            return NetchanPacketEncodeResult{
                std::nullopt,
                std::move(encoded_fragment.error),
            };
        }
        decoded_body = std::move(*encoded_fragment.decoded_body);
    } else {
        const auto maximum_body_size =
            limits.maximum_datagram_size - kNetchanHeaderSize;
        if (packet.payload.size() > maximum_body_size) {
            return encode_failure(
                NetchanPacketErrorCode::packet_too_large,
                limits.maximum_datagram_size,
                "Encoded netchan packet exceeds the configured project size bound");
        }
        decoded_body = packet.payload;
    }
    const auto body_size = decoded_body.size();

    std::vector<std::byte> datagram(kNetchanHeaderSize + body_size);
    ByteWriter writer{datagram};
    auto acknowledgement_wire = packet.header.acknowledgement.sequence.value();
    if (packet.header.acknowledgement.reliable) {
        acknowledgement_wire |= kNetchanReliableAcknowledgementFlag;
    }
    if (!writer.write_uint32_le(sequence_wire) ||
        !writer.write_uint32_le(acknowledgement_wire)) {
        return encode_failure(
            NetchanPacketErrorCode::packet_too_large,
            writer.position(),
            "Unable to encode the bounded netchan header");
    }

    if (!writer.write_bytes(decoded_body)) {
        return encode_failure(
            NetchanPacketErrorCode::packet_too_large,
            writer.position(),
            "Unable to encode bounded netchan payload bytes");
    }

    encode_netchan_payload(
        std::span<std::byte>{datagram}.subspan(kNetchanHeaderSize),
        packet.header.sequence.sequence);
    return NetchanPacketEncodeResult{std::move(datagram), std::nullopt};
}

} // namespace

NetchanDatagramClassificationResult classify_netchan_datagram(
    const std::span<const std::byte> datagram) noexcept
{
    if (datagram.size() >= kConnectionlessPacketHeaderSize &&
        datagram[0] == std::byte{0xff} && datagram[1] == std::byte{0xff} &&
        datagram[2] == std::byte{0xff} && datagram[3] == std::byte{0xff}) {
        return NetchanDatagramClassificationResult{
            NetchanDatagramClassification::connectionless,
            0U,
        };
    }
    if (datagram.size() >= kConnectionlessPacketHeaderSize &&
        datagram[0] == std::byte{0xfe} && datagram[1] == std::byte{0xff} &&
        datagram[2] == std::byte{0xff} && datagram[3] == std::byte{0xff}) {
        // Conservative safety classification inferred from the established
        // split-packet marker. It is intentionally unsupported by this stock
        // bootstrap profile and is never treated as a normal sequence word.
        return NetchanDatagramClassificationResult{
            NetchanDatagramClassification::unsupported_special,
            0U,
        };
    }
    if (datagram.size() < kNetchanHeaderSize) {
        return NetchanDatagramClassificationResult{
            NetchanDatagramClassification::malformed,
            datagram.size(),
        };
    }

    return NetchanDatagramClassificationResult{
        NetchanDatagramClassification::sequenced,
        0U,
    };
}

NetchanHeaderPeekResult peek_netchan_header(
    const std::span<const std::byte> datagram)
{
    const auto classification = classify_netchan_datagram(datagram);
    if (classification.classification == NetchanDatagramClassification::connectionless) {
        return decode_failure<NetchanHeaderPeek>(
            NetchanPacketErrorCode::connectionless_packet,
            0U,
            "Connectionless packet must not enter the netchan header decoder");
    }
    if (classification.classification == NetchanDatagramClassification::unsupported_special) {
        return decode_failure<NetchanHeaderPeek>(
            NetchanPacketErrorCode::unsupported_special_packet,
            classification.byte_offset,
            "Unsupported split/special packet has no normal netchan header");
    }
    if (classification.classification != NetchanDatagramClassification::sequenced) {
        return decode_failure<NetchanHeaderPeek>(
            NetchanPacketErrorCode::datagram_too_short,
            classification.byte_offset,
            "Datagram is shorter than the confirmed 8-byte netchan header");
    }

    ByteReader reader{datagram.first(kNetchanHeaderSize)};
    const auto sequence_wire = reader.read_uint32_le();
    const auto acknowledgement_wire = reader.read_uint32_le();
    if (!sequence_wire || !acknowledgement_wire) {
        return decode_failure<NetchanHeaderPeek>(
            NetchanPacketErrorCode::datagram_too_short,
            reader.position(),
            "Unable to decode the complete bounded netchan header");
    }

    const auto acknowledgement = NetchanSequence::from_numeric(
        *acknowledgement_wire & kNetchanSequenceMask);
    if (!acknowledgement) {
        return decode_failure<NetchanHeaderPeek>(
            NetchanPacketErrorCode::reserved_sequence_word,
            sizeof(std::uint32_t),
            "Netchan acknowledgement is outside the numeric sequence range");
    }

    return NetchanHeaderPeekResult{
        NetchanHeaderPeek{
            NetchanHeader{
                decode_netchan_sequence_word(*sequence_wire),
                NetchanAcknowledgementWord{
                    *acknowledgement,
                    (*acknowledgement_wire &
                     kNetchanReliableAcknowledgementFlag) != 0U,
                },
            },
            (*acknowledgement_wire & kNetchanReservedAcknowledgementFlag) != 0U,
        },
        std::nullopt,
    };
}

NetchanFragmentBodyDecodeResult decode_netchan_fragment_body(
    const NetchanDirection direction,
    const NetchanHeader& header,
    const std::span<const std::byte> decoded_body,
    const NetchanPacketLimits limits)
{
    if (!valid_limits(limits)) {
        return decode_failure<NetchanDecodedFragmentPacket>(
            NetchanPacketErrorCode::invalid_configuration,
            0U,
            "Invalid netchan datagram size configuration");
    }
    if (direction != NetchanDirection::server_to_client &&
        direction != NetchanDirection::client_to_server) {
        return decode_failure<NetchanDecodedFragmentPacket>(
            NetchanPacketErrorCode::unsupported_fragment_descriptor_variant,
            0U,
            "Unsupported netchan fragment packet direction");
    }
    if (!header.sequence.flags.fragmented) {
        return decode_failure<NetchanDecodedFragmentPacket>(
            NetchanPacketErrorCode::fragment_flag_required,
            0U,
            "Fragment descriptor decoder requires sequence bit 30");
    }

    const auto maximum_body_size = limits.maximum_datagram_size - kNetchanHeaderSize;
    if (decoded_body.size() > maximum_body_size) {
        return decode_failure<NetchanDecodedFragmentPacket>(
            NetchanPacketErrorCode::datagram_too_large,
            limits.maximum_datagram_size,
            "Decoded fragment body exceeds the configured datagram bound");
    }

    NetchanDecodedFragmentPacket packet{};
    ByteReader reader{decoded_body};
    if (auto error = parse_fragment_descriptors(
            reader,
            packet.fragments,
            packet.payload,
            packet.fragment_payload_size)) {
        return NetchanFragmentBodyDecodeResult{
            std::nullopt,
            std::move(error),
        };
    }
    return NetchanFragmentBodyDecodeResult{
        std::move(packet),
        std::nullopt,
    };
}

NetchanFragmentBodyEncodeResult encode_netchan_fragment_body(
    const NetchanDirection direction,
    const NetchanHeader& header,
    const NetchanFragmentSlots& fragments,
    const std::span<const std::byte> payload,
    const NetchanPacketLimits limits)
{
    if (!valid_limits(limits)) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            make_error(
                NetchanPacketErrorCode::invalid_configuration,
                0U,
                "Invalid netchan datagram size configuration"),
        };
    }
    if (direction != NetchanDirection::server_to_client &&
        direction != NetchanDirection::client_to_server) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            make_error(
                NetchanPacketErrorCode::unsupported_fragment_descriptor_variant,
                0U,
                "Unsupported netchan fragment packet direction"),
        };
    }
    if (!header.sequence.flags.fragmented) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            make_error(
                NetchanPacketErrorCode::fragment_flag_required,
                0U,
                "Fragment descriptor encoder requires sequence bit 30"),
        };
    }

    const auto present_count = static_cast<std::size_t>(std::ranges::count_if(
        fragments,
        [](const auto& descriptor) { return descriptor.has_value(); }));
    if (present_count == 0U) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            make_error(
                NetchanPacketErrorCode::fragment_flag_without_descriptor,
                kNetchanHeaderSize,
                "Fragmented netchan packet has no present fragment descriptor"),
        };
    }

    const auto descriptor_size =
        kNetchanFragmentSlotCount * kStockProtocol48FragmentPresenceSize +
        present_count * (kStockProtocol48PresentFragmentDescriptorSize -
                         kStockProtocol48FragmentPresenceSize);
    const auto maximum_body_size = limits.maximum_datagram_size - kNetchanHeaderSize;
    if (descriptor_size > maximum_body_size ||
        payload.size() > maximum_body_size - descriptor_size) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            make_error(
                NetchanPacketErrorCode::packet_too_large,
                limits.maximum_datagram_size,
                "Encoded fragment descriptor and payload exceed the configured datagram bound"),
        };
    }
    if (auto error = validate_fragment_payload_ranges(
            fragments,
            payload.size(),
            kNetchanHeaderSize + descriptor_size,
            false)) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            std::move(error),
        };
    }

    std::vector<std::byte> decoded_body(descriptor_size + payload.size());
    ByteWriter writer{decoded_body};
    for (const auto& descriptor : fragments) {
        if (!writer.write_uint8(descriptor ? 1U : 0U)) {
            return NetchanFragmentBodyEncodeResult{
                std::nullopt,
                make_error(
                    NetchanPacketErrorCode::packet_too_large,
                    kNetchanHeaderSize + writer.position(),
                    "Unable to encode bounded fragment presence"),
            };
        }
        if (!descriptor) {
            continue;
        }
        if (!writer.write_uint32_le(descriptor->fragment_id) ||
            !writer.write_uint16_le(descriptor->offset) ||
            !writer.write_uint16_le(descriptor->length)) {
            return NetchanFragmentBodyEncodeResult{
                std::nullopt,
                make_error(
                    NetchanPacketErrorCode::packet_too_large,
                    kNetchanHeaderSize + writer.position(),
                    "Unable to encode bounded fragment descriptor fields"),
            };
        }
    }
    if (!writer.write_bytes(payload)) {
        return NetchanFragmentBodyEncodeResult{
            std::nullopt,
            make_error(
                NetchanPacketErrorCode::packet_too_large,
                kNetchanHeaderSize + writer.position(),
                "Unable to encode bounded fragment payload"),
        };
    }
    return NetchanFragmentBodyEncodeResult{
        std::move(decoded_body),
        std::nullopt,
    };
}

ServerToClientNetchanDecodeResult decode_server_to_client_netchan_packet(
    const std::span<const std::byte> datagram,
    const NetchanPacketLimits limits)
{
    return decode_packet<ServerToClientNetchanPacket>(
        datagram,
        NetchanDirection::server_to_client,
        limits);
}

ClientToServerNetchanDecodeResult decode_client_to_server_netchan_packet(
    const std::span<const std::byte> datagram,
    const NetchanPacketLimits limits)
{
    return decode_packet<ClientToServerNetchanPacket>(
        datagram,
        NetchanDirection::client_to_server,
        limits);
}

NetchanPacketEncodeResult encode_server_to_client_netchan_packet(
    const ServerToClientNetchanPacket& packet,
    const NetchanPacketLimits limits)
{
    return encode_packet(packet, NetchanDirection::server_to_client, limits);
}

NetchanPacketEncodeResult encode_client_to_server_netchan_packet(
    const ClientToServerNetchanPacket& packet,
    const NetchanPacketLimits limits)
{
    return encode_packet(packet, NetchanDirection::client_to_server, limits);
}

} // namespace hlclient::goldsrc
