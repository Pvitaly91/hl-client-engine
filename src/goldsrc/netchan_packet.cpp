#include <hlclient/goldsrc/netchan_packet.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/byte_writer.hpp>
#include <hlclient/goldsrc/connectionless_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::goldsrc {
namespace {

struct DecodedPacketParts {
    NetchanHeader header;
    NetchanFragmentSlots fragments;
    std::vector<std::byte> payload;
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
    const std::size_t error_offset)
{
    for (std::size_t slot = 0U; slot < fragments.size(); ++slot) {
        if (!fragments[slot]) {
            continue;
        }
        const auto& descriptor = *fragments[slot];
        const auto begin = static_cast<std::size_t>(descriptor.offset);
        const auto length = static_cast<std::size_t>(descriptor.length);
        if (begin > payload_size || length > payload_size - begin) {
            return make_error(
                NetchanPacketErrorCode::fragment_payload_out_of_bounds,
                error_offset,
                "Netchan fragment offset and length exceed the decoded payload area");
        }

        const auto end = begin + length;
        for (std::size_t previous = 0U; previous < slot; ++previous) {
            if (!fragments[previous]) {
                continue;
            }
            const auto previous_begin =
                static_cast<std::size_t>(fragments[previous]->offset);
            const auto previous_end =
                previous_begin + static_cast<std::size_t>(fragments[previous]->length);
            if (std::max(begin, previous_begin) < std::min(end, previous_end)) {
                return make_error(
                    NetchanPacketErrorCode::fragment_payload_overlap,
                    error_offset,
                    "Netchan fragment descriptor payload ranges overlap");
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<NetchanPacketError> parse_fragment_descriptors(
    ByteReader& reader,
    NetchanFragmentSlots& fragments,
    std::vector<std::byte>& payload)
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
        if (*length == 0U) {
            return make_error(
                NetchanPacketErrorCode::zero_fragment_length,
                descriptor_offset + sizeof(std::uint32_t) + sizeof(std::uint16_t),
                "Present netchan fragment descriptor has zero length");
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
            kNetchanHeaderSize + reader.position())) {
        return error;
    }
    return std::nullopt;
}

template<typename Packet>
[[nodiscard]] NetchanPacketDecodeResult<Packet> decode_packet(
    const std::span<const std::byte> datagram,
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
    };

    std::vector<std::byte> decoded_body{
        datagram.begin() + static_cast<std::ptrdiff_t>(kNetchanHeaderSize),
        datagram.end()};
    decode_netchan_payload(decoded_body, decoded.header.sequence.sequence);

    if (decoded.header.sequence.flags.fragmented) {
        ByteReader body_reader{decoded_body};
        if (auto error = parse_fragment_descriptors(
                body_reader,
                decoded.fragments,
                decoded.payload)) {
            return NetchanPacketDecodeResult<Packet>{std::nullopt, std::move(error)};
        }
    } else {
        decoded.payload = std::move(decoded_body);
    }

    Packet packet{
        decoded.header,
        std::move(decoded.fragments),
        std::move(decoded.payload),
    };
    return NetchanPacketDecodeResult<Packet>{std::move(packet), std::nullopt};
}

template<typename Packet>
[[nodiscard]] NetchanPacketEncodeResult encode_packet(
    const Packet& packet,
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
    if (packet.header.sequence.flags.fragmented) {
        return encode_failure(
            NetchanPacketErrorCode::fragmented_encode_pending_m2_3_3,
            kNetchanHeaderSize,
            "Fragmented packet construction is deferred to M2.3.3");
    }
    if (!packet.header.sequence.flags.fragmented && has_descriptors) {
        return encode_failure(
            NetchanPacketErrorCode::descriptors_without_fragment_flag,
            kNetchanHeaderSize,
            "Netchan packet has fragment descriptors without the fragment flag");
    }

    const auto maximum_body_size = limits.maximum_datagram_size - kNetchanHeaderSize;
    if (packet.payload.size() > maximum_body_size) {
        return encode_failure(
            NetchanPacketErrorCode::packet_too_large,
            limits.maximum_datagram_size,
            "Encoded netchan packet exceeds the configured project size bound");
    }
    const auto body_size = packet.payload.size();

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

    if (!writer.write_bytes(packet.payload)) {
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

ServerToClientNetchanDecodeResult decode_server_to_client_netchan_packet(
    const std::span<const std::byte> datagram,
    const NetchanPacketLimits limits)
{
    return decode_packet<ServerToClientNetchanPacket>(datagram, limits);
}

ClientToServerNetchanDecodeResult decode_client_to_server_netchan_packet(
    const std::span<const std::byte> datagram,
    const NetchanPacketLimits limits)
{
    return decode_packet<ClientToServerNetchanPacket>(datagram, limits);
}

NetchanPacketEncodeResult encode_server_to_client_netchan_packet(
    const ServerToClientNetchanPacket& packet,
    const NetchanPacketLimits limits)
{
    return encode_packet(packet, limits);
}

NetchanPacketEncodeResult encode_client_to_server_netchan_packet(
    const ClientToServerNetchanPacket& packet,
    const NetchanPacketLimits limits)
{
    return encode_packet(packet, limits);
}

} // namespace hlclient::goldsrc
