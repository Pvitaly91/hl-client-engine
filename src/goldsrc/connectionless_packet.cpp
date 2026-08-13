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
    if (datagram.size() > kMaximumConnectionlessChallengeDatagramSize) {
        return parse_failure(
            ConnectionlessPacketErrorCode::datagram_too_large,
            kMaximumConnectionlessChallengeDatagramSize,
            "Datagram exceeds the bounded M1 connectionless challenge size");
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
    if (payload.empty()) {
        return encode_failure(
            ConnectionlessPacketErrorCode::empty_payload,
            "Connectionless packet payload must not be empty");
    }
    if (payload.size() > kMaximumConnectionlessChallengePayloadSize) {
        return encode_failure(
            ConnectionlessPacketErrorCode::payload_too_large,
            "Connectionless payload exceeds the bounded M1 challenge size");
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
