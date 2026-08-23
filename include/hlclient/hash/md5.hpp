#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace hlclient::hash {

inline constexpr std::size_t kMd5DigestSize = 16U;
inline constexpr std::size_t kMd5BlockSize = 64U;
inline constexpr std::uint64_t kMd5MaximumMessageBytes =
    (std::numeric_limits<std::uint64_t>::max)() / 8U;

using Md5Digest = std::array<std::byte, kMd5DigestSize>;

// MD5 is provided exclusively for GoldSrc wire compatibility. It must not be
// used to establish security, authenticity, integrity, or trust.
class Md5Hasher final {
public:
    Md5Hasher() noexcept;

    // Returns false without changing the hasher when the message-length bound
    // would be exceeded or when finalize() has already been called.
    [[nodiscard]] bool update(std::span<const std::byte> bytes) noexcept;

    // Finalization is idempotent. Repeated calls return the same digest.
    [[nodiscard]] std::optional<Md5Digest> finalize() noexcept;

    void reset() noexcept;

    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] std::uint64_t byte_count() const noexcept;

private:
    void transform(std::span<const std::byte, kMd5BlockSize> block) noexcept;

    std::array<std::uint32_t, 4U> state_{};
    std::array<std::byte, kMd5BlockSize> buffer_{};
    Md5Digest digest_{};
    std::uint64_t byte_count_{0U};
    std::size_t buffered_byte_count_{0U};
    bool finalized_{false};
};

} // namespace hlclient::hash
