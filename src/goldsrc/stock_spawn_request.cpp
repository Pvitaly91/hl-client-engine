#include <hlclient/goldsrc/stock_spawn_request.hpp>

#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

constexpr std::array<std::uint8_t, 16U> kMungifyTable2{
    0x05U, 0x61U, 0x7aU, 0xedU, 0x1bU, 0xcaU, 0x0dU, 0x9bU,
    0x4aU, 0xf1U, 0x64U, 0xc7U, 0xb5U, 0x8eU, 0xdfU, 0xa0U};

constexpr std::array<std::uint8_t, 16U> kMungifyTable3{
    0x20U, 0x07U, 0x13U, 0x61U, 0x03U, 0x45U, 0x17U, 0x72U,
    0x0aU, 0x2dU, 0x48U, 0x0cU, 0x4aU, 0x12U, 0xa9U, 0xb5U};

constexpr std::array<std::byte, kStockSendEntitiesRequestSize>
    kStockSendEntitiesRequestBytes{
        std::byte{static_cast<std::uint8_t>(ClientMessageOpcode::string_command)},
        std::byte{'s'}, std::byte{'e'}, std::byte{'n'}, std::byte{'d'},
        std::byte{'e'}, std::byte{'n'}, std::byte{'t'}, std::byte{'s'},
        std::byte{0U}};

[[nodiscard]] constexpr std::uint32_t byte_swap_32(
    const std::uint32_t value) noexcept
{
    return ((value & 0x0000'00ffU) << 24U) |
           ((value & 0x0000'ff00U) << 8U) |
           ((value & 0x00ff'0000U) >> 8U) |
           ((value & 0xff00'0000U) >> 24U);
}

[[nodiscard]] constexpr std::uint32_t xor_mungify_bytes(
    std::uint32_t value,
    const std::array<std::uint8_t, 16U>& table) noexcept
{
    for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        const auto mask = static_cast<std::uint8_t>(
            0xa5U | (byte_index << byte_index) | byte_index |
            table[byte_index]);
        value ^= static_cast<std::uint32_t>(mask) << (byte_index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint32_t munge2_word(
    std::uint32_t value,
    const std::uint8_t sequence) noexcept
{
    const auto key = static_cast<std::uint32_t>(sequence);
    value ^= ~key;
    value = byte_swap_32(value);
    value = xor_mungify_bytes(value, kMungifyTable2);
    value ^= key;
    return value;
}

[[nodiscard]] constexpr std::uint32_t unmunge3_word(
    std::uint32_t value,
    const std::uint8_t sequence) noexcept
{
    const auto key = static_cast<std::uint32_t>(sequence);
    value ^= key;
    value = xor_mungify_bytes(value, kMungifyTable3);
    value = byte_swap_32(value);
    value ^= ~key;
    return value;
}

[[nodiscard]] StockSpawnRequestBuildResult failure(
    const StockSpawnRequestErrorCode code,
    std::string context)
{
    return StockSpawnRequestBuildResult{
        std::nullopt,
        StockSpawnRequestError{code, std::move(context)}};
}

} // namespace

std::uint32_t decode_stock_server_world_map_crc(
    const std::uint32_t munged_crc,
    const std::uint8_t client_slot) noexcept
{
    return unmunge3_word(
        munged_crc, static_cast<std::uint8_t>(0xffU - client_slot));
}

EncodedStockSpawnRequest::EncodedStockSpawnRequest(
    const std::uint32_t server_count,
    const std::uint32_t world_map_crc,
    std::vector<std::byte> bytes) noexcept
    : server_count_{server_count}, world_map_crc_{world_map_crc},
      bytes_{std::move(bytes)}
{
}

std::uint32_t EncodedStockSpawnRequest::server_count() const noexcept
{
    return server_count_;
}

std::uint32_t EncodedStockSpawnRequest::world_map_crc() const noexcept
{
    return world_map_crc_;
}

std::span<const std::byte> EncodedStockSpawnRequest::semantic_bytes() const noexcept
{
    return bytes_;
}

StockSpawnRequestBuildResult StockSpawnRequestBuilder::build(
    const std::uint32_t server_count,
    const std::uint32_t world_map_crc)
{
    if (server_count == 0U) {
        return failure(
            StockSpawnRequestErrorCode::invalid_server_count,
            "Stock spawn request requires the current nonzero server count");
    }

    const auto sequence = static_cast<std::uint8_t>(0xffU - server_count);
    const auto munged_crc = munge2_word(world_map_crc, sequence);
    const auto signed_munged_crc = std::bit_cast<std::int32_t>(munged_crc);

    std::array<char, kMaximumStockSpawnCommandLength + 1U> command{};
    auto* cursor = command.data();
    auto* const end = command.data() + command.size();
    constexpr std::string_view prefix{"spawn "};
    for (const char character : prefix) {
        *cursor++ = character;
    }
    auto converted = std::to_chars(cursor, end, server_count);
    if (converted.ec != std::errc{}) {
        return failure(
            StockSpawnRequestErrorCode::encoding_failed,
            "Stock spawn server count could not be formatted");
    }
    cursor = converted.ptr;
    if (cursor == end) {
        return failure(
            StockSpawnRequestErrorCode::command_too_large,
            "Stock spawn command exceeds its bounded buffer");
    }
    *cursor++ = ' ';
    converted = std::to_chars(cursor, end, signed_munged_crc);
    if (converted.ec != std::errc{}) {
        return failure(
            StockSpawnRequestErrorCode::encoding_failed,
            "Stock spawn map CRC could not be formatted");
    }
    cursor = converted.ptr;
    const auto command_length = static_cast<std::size_t>(cursor - command.data());
    if (command_length == 0U ||
        command_length > kMaximumStockSpawnCommandLength) {
        return failure(
            StockSpawnRequestErrorCode::command_too_large,
            "Stock spawn command is outside its project bound");
    }

    std::vector<std::byte> bytes;
    try {
        bytes.reserve(1U + command_length + 1U);
        bytes.push_back(static_cast<std::byte>(ClientMessageOpcode::string_command));
        const auto command_bytes = std::as_bytes(
            std::span{command.data(), command_length});
        bytes.insert(bytes.end(), command_bytes.begin(), command_bytes.end());
        bytes.push_back(std::byte{0U});
    } catch (...) {
        return failure(
            StockSpawnRequestErrorCode::encoding_failed,
            "Stock spawn request allocation failed");
    }
    return StockSpawnRequestBuildResult{
        EncodedStockSpawnRequest{server_count, world_map_crc, std::move(bytes)},
        std::nullopt};
}

std::span<const std::byte>
EncodedStockSendEntitiesRequest::semantic_bytes() const noexcept
{
    return kStockSendEntitiesRequestBytes;
}

EncodedStockSendEntitiesRequest
StockSendEntitiesRequestBuilder::build() noexcept
{
    return EncodedStockSendEntitiesRequest{};
}

} // namespace hlclient::goldsrc
