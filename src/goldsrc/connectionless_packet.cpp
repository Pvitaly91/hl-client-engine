#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/byte_writer.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] ConnectionlessPacketParseResult parse_failure(
    const ConnectionlessPacketErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ConnectionlessPacketParseResult{
        std::nullopt,
        ConnectionlessPacketError{code, byte_offset, std::move(context)},
    };
}

[[nodiscard]] ConnectionlessPacketEncodeResult encode_failure(
    const ConnectionlessPacketErrorCode code,
    std::string context)
{
    return ConnectionlessPacketEncodeResult{
        std::nullopt,
        ConnectionlessPacketError{code, 0U, std::move(context)},
    };
}

} // namespace

ConnectionlessPacketParseResult parse_connectionless_packet(
    const std::span<const std::byte> datagram)
{
    return parse_connectionless_packet(
        datagram,
        kMaximumConnectionlessChallengeDatagramSize);
}

ConnectionlessPacketParseResult parse_connectionless_packet(
    const std::span<const std::byte> datagram,
    const std::size_t maximum_datagram_size)
{
    if (maximum_datagram_size < kConnectionlessPacketHeaderSize + 1U ||
        datagram.size() > maximum_datagram_size) {
        return parse_failure(
            ConnectionlessPacketErrorCode::datagram_too_large,
            maximum_datagram_size,
            "Datagram exceeds the configured connectionless packet size");
    }
    if (datagram.size() < kConnectionlessPacketHeaderSize) {
        return parse_failure(
            ConnectionlessPacketErrorCode::truncated_header,
            datagram.size(),
            "Connectionless packet header is truncated");
    }

    ByteReader reader{datagram};
    const auto header = reader.read_uint32_le();
    if (!header || *header != kConnectionlessPacketHeader) {
        return parse_failure(
            ConnectionlessPacketErrorCode::invalid_header,
            0U,
            "Datagram does not use the 0xFFFFFFFF connectionless packet header");
    }
    if (reader.remaining() == 0U) {
        return parse_failure(
            ConnectionlessPacketErrorCode::empty_payload,
            reader.position(),
            "Connectionless packet payload is empty");
    }

    const auto payload = reader.read_bytes(reader.remaining());
    if (!payload) {
        return parse_failure(
            ConnectionlessPacketErrorCode::payload_too_large,
            reader.position(),
            "Unable to read the bounded connectionless packet payload");
    }

    return ConnectionlessPacketParseResult{
        ConnectionlessPacket{std::vector<std::byte>{payload->begin(), payload->end()}},
        std::nullopt,
    };
}

ConnectionlessPacketEncodeResult encode_connectionless_packet(
    const std::span<const std::byte> payload)
{
    return encode_connectionless_packet(
        payload,
        kMaximumConnectionlessChallengeDatagramSize);
}

ConnectionlessPacketEncodeResult encode_connectionless_packet(
    const std::span<const std::byte> payload,
    const std::size_t maximum_datagram_size)
{
    if (payload.empty()) {
        return encode_failure(
            ConnectionlessPacketErrorCode::empty_payload,
            "Connectionless packet payload must not be empty");
    }
    if (maximum_datagram_size < kConnectionlessPacketHeaderSize + 1U ||
        payload.size() > maximum_datagram_size - kConnectionlessPacketHeaderSize) {
        return encode_failure(
            ConnectionlessPacketErrorCode::payload_too_large,
            "Connectionless payload exceeds the configured packet size");
    }

    std::vector<std::byte> datagram(kConnectionlessPacketHeaderSize + payload.size());
    ByteWriter writer{datagram};
    if (!writer.write_uint32_le(kConnectionlessPacketHeader) || !writer.write_bytes(payload)) {
        return encode_failure(
            ConnectionlessPacketErrorCode::payload_too_large,
            "Unable to encode the bounded connectionless packet");
    }

    return ConnectionlessPacketEncodeResult{std::move(datagram), std::nullopt};
}

} // namespace hlclient::goldsrc
