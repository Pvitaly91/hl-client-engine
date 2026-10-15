#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace hlclient::goldsrc {

// Independently authored fixed Protocol 48 terminal operation. The public
// reference profile uses clc_stringcmd, "dropclient\n", then the string NUL.
// Netchan owns sequence, padding and payload transforms; no caller text exists.
class DisconnectRequestBuilder final {
public:
    [[nodiscard]] static constexpr std::span<const std::byte> build() noexcept
    {
        return bytes_;
    }

private:
    inline static constexpr std::array<std::byte, 13U> bytes_{
        std::byte{3}, std::byte{'d'}, std::byte{'r'}, std::byte{'o'},
        std::byte{'p'}, std::byte{'c'}, std::byte{'l'}, std::byte{'i'},
        std::byte{'e'}, std::byte{'n'}, std::byte{'t'}, std::byte{'\n'},
        std::byte{0}};
};

enum class DisconnectNoticeStatus { not_notice, observed, malformed };

// Only an exactly framed terminal control at a known service boundary counts.
// Arbitrary server text is never scanned, retained, executed or logged. A
// repeated notice is legal (a reliable prefix and final unreliable suffix).
[[nodiscard]] inline DisconnectNoticeStatus inspect_disconnect_notice(
    const std::span<const std::byte> bytes) noexcept
{
    std::size_t offset{};
    while (offset < bytes.size() && bytes[offset] == std::byte{1}) ++offset;
    if (offset == bytes.size() || bytes[offset] != std::byte{2})
        return DisconnectNoticeStatus::not_notice;
    bool observed{};
    while (offset < bytes.size()) {
        if (bytes[offset] == std::byte{1}) { ++offset; continue; }
        if (bytes[offset++] != std::byte{2})
            return DisconnectNoticeStatus::malformed;
        const auto start = offset;
        while (offset < bytes.size() && bytes[offset] != std::byte{0} &&
               offset - start < 1024U) ++offset;
        if (offset == bytes.size() || offset - start == 1024U)
            return DisconnectNoticeStatus::malformed;
        ++offset;
        observed = true;
    }
    return observed ? DisconnectNoticeStatus::observed
                    : DisconnectNoticeStatus::malformed;
}

} // namespace hlclient::goldsrc
