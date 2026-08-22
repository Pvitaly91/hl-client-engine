#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace resource_list_test_fixture {

struct EntrySpec {
    std::uint8_t type{0U};
    std::string name;
    std::uint16_t index{0U};
    std::uint32_t size_code{0U};
    std::uint8_t flags{0U};
};

struct Message {
    std::vector<std::byte> bytes;
    std::size_t bit_length{0U};
    std::size_t padding_start_bit{0U};
    std::size_t padding_bit_count{0U};
};

// Independently authored, sanitized literal. It is deliberately not produced
// by a production encoder (none exists). Layout: opcode 43, count 3, model
// map/model plus sound entries, then the mandatory aligned zero byte.
inline constexpr std::array<std::byte, 81U> kExactResourceListMessage{
    std::byte{0x2bU}, std::byte{0x03U}, std::byte{0x20U},
    std::byte{0x6dU}, std::byte{0x61U}, std::byte{0x70U},
    std::byte{0x73U}, std::byte{0x2fU}, std::byte{0x74U},
    std::byte{0x65U}, std::byte{0x73U}, std::byte{0x74U},
    std::byte{0x5fU}, std::byte{0x6dU}, std::byte{0x61U},
    std::byte{0x70U}, std::byte{0x2eU}, std::byte{0x62U},
    std::byte{0x73U}, std::byte{0x70U}, std::byte{0x00U},
    std::byte{0x07U}, std::byte{0x60U}, std::byte{0x45U},
    std::byte{0x23U}, std::byte{0x11U}, std::byte{0xd2U},
    std::byte{0xf6U}, std::byte{0x46U}, std::byte{0x56U},
    std::byte{0xc6U}, std::byte{0x36U}, std::byte{0xf7U},
    std::byte{0x42U}, std::byte{0x57U}, std::byte{0x36U},
    std::byte{0x47U}, std::byte{0xf7U}, std::byte{0xd5U},
    std::byte{0xf6U}, std::byte{0x46U}, std::byte{0x56U},
    std::byte{0xc6U}, std::byte{0xe6U}, std::byte{0xd2U},
    std::byte{0x46U}, std::byte{0xc6U}, std::byte{0x06U},
    std::byte{0x80U}, std::byte{0x00U}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0x00U},
    std::byte{0x73U}, std::byte{0x6fU}, std::byte{0x75U},
    std::byte{0x6eU}, std::byte{0x64U}, std::byte{0x2fU},
    std::byte{0x74U}, std::byte{0x65U}, std::byte{0x73U},
    std::byte{0x74U}, std::byte{0x5fU}, std::byte{0x73U},
    std::byte{0x6fU}, std::byte{0x75U}, std::byte{0x6eU},
    std::byte{0x64U}, std::byte{0x2eU}, std::byte{0x77U},
    std::byte{0x61U}, std::byte{0x76U}, std::byte{0x00U},
    std::byte{0x09U}, std::byte{0x60U}, std::byte{0x45U},
    std::byte{0x00U}, std::byte{0x10U}, std::byte{0x00U},
};

inline constexpr std::size_t kExactMessageBits =
    kExactResourceListMessage.size() * 8U;
inline constexpr std::uint64_t kExactRawSizeCodeSum = 17'971'371U;
inline constexpr std::size_t kExactTotalNameBytes = 58U;

class LsbBitWriter final {
public:
    void write(const std::uint32_t value, const std::size_t width)
    {
        for (std::size_t bit = 0U; bit < width; ++bit) {
            if ((bit_length_ & 7U) == 0U) {
                bytes_.push_back(std::byte{0U});
            }
            if (((value >> bit) & 1U) != 0U) {
                const auto byte_index = bit_length_ >> 3U;
                const auto byte_value =
                    std::to_integer<std::uint8_t>(bytes_[byte_index]);
                bytes_[byte_index] = static_cast<std::byte>(
                    byte_value | static_cast<std::uint8_t>(
                                     1U << (bit_length_ & 7U)));
            }
            ++bit_length_;
        }
    }

    [[nodiscard]] std::size_t bit_length() const noexcept
    {
        return bit_length_;
    }

    [[nodiscard]] std::vector<std::byte> take_bytes() noexcept
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
    std::size_t bit_length_{0U};
};

[[nodiscard]] inline Message make_message(
    const std::span<const EntrySpec> entries)
{
    LsbBitWriter writer;
    writer.write(43U, 8U);
    writer.write(static_cast<std::uint32_t>(entries.size()), 12U);
    for (const auto& entry : entries) {
        writer.write(entry.type, 4U);
        for (const auto character : entry.name) {
            writer.write(
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(character)),
                8U);
        }
        writer.write(0U, 8U);
        writer.write(entry.index, 12U);
        writer.write(entry.size_code, 24U);
        writer.write(entry.flags, 4U);
    }

    const auto padding_start_bit = writer.bit_length();
    const auto padding_bit_count = 8U - (padding_start_bit & 7U);
    writer.write(0U, padding_bit_count);
    const auto bit_length = writer.bit_length();
    return Message{
        writer.take_bytes(),
        bit_length,
        padding_start_bit,
        padding_bit_count,
    };
}

[[nodiscard]] inline Message make_message(
    const std::initializer_list<EntrySpec> entries)
{
    return make_message(std::span<const EntrySpec>{entries.begin(), entries.size()});
}

[[nodiscard]] inline std::vector<std::byte> with_prefix(
    const std::span<const std::byte> message,
    const std::span<const std::byte> prefix)
{
    std::vector<std::byte> result;
    result.reserve(prefix.size() + message.size());
    result.insert(result.end(), prefix.begin(), prefix.end());
    result.insert(result.end(), message.begin(), message.end());
    return result;
}

inline void set_bit(
    std::vector<std::byte>& bytes,
    const std::size_t bit_offset,
    const bool value)
{
    const auto byte_index = bit_offset >> 3U;
    const auto mask = static_cast<std::uint8_t>(1U << (bit_offset & 7U));
    const auto current = std::to_integer<std::uint8_t>(bytes[byte_index]);
    bytes[byte_index] = static_cast<std::byte>(
        value ? static_cast<std::uint8_t>(current | mask)
              : static_cast<std::uint8_t>(current & ~mask));
}

} // namespace resource_list_test_fixture
