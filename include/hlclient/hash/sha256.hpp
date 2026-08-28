#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace hlclient::hash {

inline constexpr std::size_t kSha256DigestSize = 32U;
using Sha256Digest = std::array<std::byte, kSha256DigestSize>;

// Bounded, allocation-free one-shot SHA-256 used for deterministic evidence
// identities. Returns nullopt only when the input length cannot be represented
// by SHA-256's 64-bit bit-count field or padding arithmetic would overflow.
[[nodiscard]] std::optional<Sha256Digest> sha256(
    std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

} // namespace hlclient::hash
