#pragma once

#include <hlclient/goldsrc/bit_reader.hpp>
#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>
#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kGoldSrcSvcSpawnBaselineOpcode = 22U;
inline constexpr std::size_t kGoldSrcBaselineEntityNumberBits = 11U;
inline constexpr std::size_t kGoldSrcBaselineTerminatorBits = 16U;
inline constexpr std::uint32_t kGoldSrcBaselineTerminator = 0xffffU;
inline constexpr double kGoldSrcInitialBaselineTimeBaseSeconds = 1.0;

struct EntityBaselineDecodeLimits {
    EntitySnapshotLimits snapshot{};
    std::size_t maximum_message_bits{kDefaultMaximumDeltaMessageBits};
    std::size_t maximum_instanced_baselines{63U};
};

enum class EntityBaselineDecodeErrorCode {
    invalid_configuration,
    payload_not_decompressed,
    wrong_direction,
    invalid_cursor,
    invalid_generation,
    message_too_large,
    truncated_terminator,
    truncated_entity_number,
    truncated_entity_type,
    unknown_schema,
    delta_decode_failed,
    truncated_instanced_count,
    instanced_count_limit_exceeded,
    duplicate_baseline,
    malformed_padding,
    registry_publish_failed,
    size_overflow,
};

struct EntityBaselineDecodeError {
    EntityBaselineDecodeErrorCode code{EntityBaselineDecodeErrorCode::invalid_configuration};
    std::size_t bit_offset{0U};
    std::optional<std::uint32_t> entity_number;
    std::optional<EntitySchemaCategory> schema_category;
    std::optional<DeltaValueErrorCode> delta_error;
    std::string context;
};

struct EntityBaselineDecodeInput {
    const OwnedServicePayload* payload{nullptr};
    StockRuntimeSourceCursor start_cursor{};
    std::size_t payload_ordinal{0U};
    std::uint64_t source_generation{0U};
    std::uint32_t max_clients{0U};
    std::string_view ordinary_schema_name{"entity_state_t"};
    std::string_view player_schema_name{"entity_state_player_t"};
    std::string_view custom_schema_name{"custom_entity_state_t"};
    std::size_t message_bit_length{static_cast<std::size_t>(-1)};
};

struct EntityBaselineDecodeResult {
    std::optional<EntityBaselineRegistryState> registry;
    std::optional<EntityBaselineDecodeError> error;
    StockRuntimeSourceCursor start_cursor{};
    StockRuntimeSourceCursor end_cursor{};
    std::size_t entity_count{0U};
    std::size_t instanced_count{0U};
    std::size_t bits_consumed{0U};
    StockRuntimeSourceMetadata source_metadata{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return registry.has_value();
    }
};

class GoldSrcEntityBaselineDecoder final {
public:
    explicit GoldSrcEntityBaselineDecoder(
        EntityBaselineDecodeLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const EntityBaselineDecodeLimits& limits() const noexcept;
    [[nodiscard]] EntityBaselineDecodeResult decode(
        const EntityBaselineDecodeInput& input,
        const DeltaSchemaRegistryState& schemas) const;

private:
    EntityBaselineDecodeLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const EntityBaselineDecodeErrorCode code) noexcept
{
    switch (code) {
    case EntityBaselineDecodeErrorCode::invalid_configuration: return "invalid_configuration";
    case EntityBaselineDecodeErrorCode::payload_not_decompressed: return "payload_not_decompressed";
    case EntityBaselineDecodeErrorCode::wrong_direction: return "wrong_direction";
    case EntityBaselineDecodeErrorCode::invalid_cursor: return "invalid_cursor";
    case EntityBaselineDecodeErrorCode::invalid_generation: return "invalid_generation";
    case EntityBaselineDecodeErrorCode::message_too_large: return "message_too_large";
    case EntityBaselineDecodeErrorCode::truncated_terminator: return "truncated_terminator";
    case EntityBaselineDecodeErrorCode::truncated_entity_number: return "truncated_entity_number";
    case EntityBaselineDecodeErrorCode::truncated_entity_type: return "truncated_entity_type";
    case EntityBaselineDecodeErrorCode::unknown_schema: return "unknown_schema";
    case EntityBaselineDecodeErrorCode::delta_decode_failed: return "delta_decode_failed";
    case EntityBaselineDecodeErrorCode::truncated_instanced_count: return "truncated_instanced_count";
    case EntityBaselineDecodeErrorCode::instanced_count_limit_exceeded: return "instanced_count_limit_exceeded";
    case EntityBaselineDecodeErrorCode::duplicate_baseline: return "duplicate_baseline";
    case EntityBaselineDecodeErrorCode::malformed_padding: return "malformed_padding";
    case EntityBaselineDecodeErrorCode::registry_publish_failed: return "registry_publish_failed";
    case EntityBaselineDecodeErrorCode::size_overflow: return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
