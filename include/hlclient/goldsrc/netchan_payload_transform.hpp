#pragma once

#include <hlclient/goldsrc/netchan_sequence.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace hlclient::goldsrc {

inline constexpr std::size_t kNetchanPayloadTransformWordSize = sizeof(std::uint32_t);

[[nodiscard]] std::uint8_t netchan_payload_transform_key(
    NetchanSequence outgoing_sequence) noexcept;

// These symmetric-direction operations transform complete little-endian
// 32-bit words only. A trailing range of zero to three bytes is left intact.
// The caller supplies only the bytes following the untransformed 8-byte
// sequence/acknowledgement header.
void encode_netchan_payload(
    std::span<std::byte> payload,
    NetchanSequence outgoing_sequence) noexcept;

void decode_netchan_payload(
    std::span<std::byte> payload,
    NetchanSequence outgoing_sequence) noexcept;

} // namespace hlclient::goldsrc
