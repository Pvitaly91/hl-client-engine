#pragma once

#include <hlclient/goldsrc/client_message.hpp>

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kMaximumStockSpawnCommandLength = 64U;
inline constexpr std::size_t kMaximumStockSpawnRequestSize =
    1U + kMaximumStockSpawnCommandLength + 1U;
inline constexpr std::size_t kStockSpawnDiagnosticTextLimit = 256U;
inline constexpr std::size_t kStockSendEntitiesCommandLength = 8U;
inline constexpr std::size_t kStockSendEntitiesRequestSize =
    1U + kStockSendEntitiesCommandLength + 1U;

// ServerInfo carries the map CRC after the stock COM_Munge3 transform.  The
// client must recover the original value using the assigned player slot before
// constructing the later spawn command.
[[nodiscard]] std::uint32_t decode_stock_server_world_map_crc(
    std::uint32_t munged_crc,
    std::uint8_t client_slot) noexcept;

enum class StockSpawnRequestErrorCode : std::uint8_t {
    invalid_server_count,
    command_too_large,
    encoding_failed,
};

struct StockSpawnRequestError final {
    StockSpawnRequestErrorCode code{
        StockSpawnRequestErrorCode::invalid_server_count};
    std::string context;
};

class EncodedStockSpawnRequest final {
public:
    EncodedStockSpawnRequest(const EncodedStockSpawnRequest&) = default;
    EncodedStockSpawnRequest& operator=(const EncodedStockSpawnRequest&) = default;
    EncodedStockSpawnRequest(EncodedStockSpawnRequest&&) noexcept = default;
    EncodedStockSpawnRequest& operator=(EncodedStockSpawnRequest&&) noexcept = default;
    ~EncodedStockSpawnRequest() = default;

    [[nodiscard]] std::uint32_t server_count() const noexcept;
    [[nodiscard]] std::uint32_t world_map_crc() const noexcept;
    [[nodiscard]] std::span<const std::byte> semantic_bytes() const noexcept;

private:
    friend class StockSpawnRequestBuilder;

    EncodedStockSpawnRequest(
        std::uint32_t server_count,
        std::uint32_t world_map_crc,
        std::vector<std::byte> bytes) noexcept;

    std::uint32_t server_count_{0U};
    std::uint32_t world_map_crc_{0U};
    std::vector<std::byte> bytes_;
};

struct StockSpawnRequestBuildResult final {
    std::optional<EncodedStockSpawnRequest> encoding;
    std::optional<StockSpawnRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return encoding.has_value();
    }
};

class StockSpawnRequestBuilder final {
public:
    // No arbitrary command string is accepted. Both values come from the
    // current connection's decoded ServerInfo.
    [[nodiscard]] static StockSpawnRequestBuildResult build(
        std::uint32_t server_count,
        std::uint32_t world_map_crc);
};

// GoldSrc answers svc_signonnum 1 with this fixed client string command.  The
// type deliberately exposes no arbitrary command-string input.
class EncodedStockSendEntitiesRequest final {
public:
    [[nodiscard]] constexpr ClientMessageOpcode opcode() const noexcept
    {
        return ClientMessageOpcode::string_command;
    }

    [[nodiscard]] constexpr std::string_view command() const noexcept
    {
        return "sendents";
    }

    [[nodiscard]] std::span<const std::byte> semantic_bytes() const noexcept;

private:
    friend class StockSendEntitiesRequestBuilder;
    EncodedStockSendEntitiesRequest() = default;
};

class StockSendEntitiesRequestBuilder final {
public:
    [[nodiscard]] static EncodedStockSendEntitiesRequest build() noexcept;
};

[[nodiscard]] constexpr std::string_view to_string(
    const StockSpawnRequestErrorCode code) noexcept
{
    switch (code) {
    case StockSpawnRequestErrorCode::invalid_server_count:
        return "invalid_server_count";
    case StockSpawnRequestErrorCode::command_too_large:
        return "command_too_large";
    case StockSpawnRequestErrorCode::encoding_failed:
        return "encoding_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
