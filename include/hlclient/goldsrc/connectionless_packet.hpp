#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint32_t kConnectionlessPacketHeader = 0xffff'ffffU;
inline constexpr std::size_t kConnectionlessPacketHeaderSize = sizeof(std::uint32_t);
inline constexpr std::size_t kMaximumConnectionlessChallengeDatagramSize = 1'024U;
inline constexpr std::size_t kMaximumConnectionlessChallengePayloadSize =
    kMaximumConnectionlessChallengeDatagramSize - kConnectionlessPacketHeaderSize;

struct ConnectionlessPacket {
    std::vector<std::byte> payload;
};

enum class ConnectionlessPacketErrorCode {
    datagram_too_large,
    truncated_header,
    invalid_header,
    empty_payload,
    payload_too_large,
};

struct ConnectionlessPacketError {
    ConnectionlessPacketErrorCode code{ConnectionlessPacketErrorCode::invalid_header};
    std::size_t byte_offset{0};
    std::string context;
};

struct ConnectionlessPacketParseResult {
    std::optional<ConnectionlessPacket> packet;
    std::optional<ConnectionlessPacketError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return packet.has_value();
    }
};

struct ConnectionlessPacketEncodeResult {
    std::optional<std::vector<std::byte>> datagram;
    std::optional<ConnectionlessPacketError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return datagram.has_value();
    }
};

[[nodiscard]] ConnectionlessPacketParseResult parse_connectionless_packet(
    std::span<const std::byte> datagram);
[[nodiscard]] ConnectionlessPacketParseResult parse_connectionless_packet(
    std::span<const std::byte> datagram,
    std::size_t maximum_datagram_size);

[[nodiscard]] ConnectionlessPacketEncodeResult encode_connectionless_packet(
    std::span<const std::byte> payload);
[[nodiscard]] ConnectionlessPacketEncodeResult encode_connectionless_packet(
    std::span<const std::byte> payload,
    std::size_t maximum_datagram_size);

} // namespace hlclient::goldsrc
