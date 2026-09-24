#pragma once

#include <hlclient/goldsrc/client_data_decoder.hpp>
#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/runtime_control_decoder.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kGoldSrcSvcPacketEntitiesOpcode = 40U;
inline constexpr std::uint8_t kGoldSrcSvcDeltaPacketEntitiesOpcode = 41U;
inline constexpr std::size_t kGoldSrcPacketEntityCountBits = 16U;
inline constexpr std::size_t kGoldSrcPacketEntityDeltaBaseTagBits = 8U;
inline constexpr std::size_t kGoldSrcPacketEntityNumberBits = 11U;
inline constexpr std::size_t kGoldSrcPacketEntityRelativeNumberBits = 6U;
inline constexpr std::size_t kGoldSrcPacketEntityTerminatorBits = 16U;
inline constexpr std::uint32_t kGoldSrcPacketEntityTerminator = 0U;
// GoldSrc multiplayer keeps 64 frame slots. The referenced client validation
// rejects modular distances 0 and >= UPDATE_MASK (63), hence 1..62.
inline constexpr std::uint32_t kGoldSrcPacketEntityMaximumDeltaLookback = 62U;

enum class PacketEntityCompatibilityProfile : std::uint8_t {
    public_goldsrc48_packet_entities_v1,
};

enum class PacketEntitySpecificationSource : std::uint8_t {
    public_protocol_reference,
};

enum class PacketEntityStockVerification : std::uint8_t {
    not_verified_against_stock_runtime_payload,
};

enum class PacketEntityMessageKind : std::uint8_t {
    full,
    delta,
};

enum class PacketEntityRecoveryStatus : std::uint8_t {
    none,
    full_snapshot_required,
    clientdata_no_base_required,
};

struct PacketEntityWireDeltaBaseTag final {
    std::uint8_t value{0U};

    [[nodiscard]] friend bool operator==(
        const PacketEntityWireDeltaBaseTag&,
        const PacketEntityWireDeltaBaseTag&) noexcept = default;
};

struct PacketEntityResolvedFrameReference final {
    std::uint32_t source_transport_sequence{0U};

    [[nodiscard]] friend bool operator==(
        const PacketEntityResolvedFrameReference&,
        const PacketEntityResolvedFrameReference&) noexcept = default;
};

struct PacketEntityDecodeLimits final {
    EntitySnapshotLimits snapshots{};
    RuntimeControlDecodeLimits controls{};
    std::size_t maximum_messages_per_payload{256U};
    std::size_t maximum_wire_records{4'096U};
    ClientDataDecodeLimits client_data{};
};

inline constexpr std::size_t kMaximumPacketEntityMessagesPerPayload = 512U;
inline constexpr std::size_t kMaximumPacketEntityWireRecords = 16'384U;

[[nodiscard]] bool valid_packet_entity_profile(
    PacketEntityCompatibilityProfile profile) noexcept;
[[nodiscard]] bool valid_packet_entity_decode_limits(
    const PacketEntityDecodeLimits& limits) noexcept;

struct PacketEntityMessageEvent final {
    PacketEntityMessageKind kind{PacketEntityMessageKind::full};
    std::uint16_t wire_entity_count{0U};
    std::size_t wire_record_count{0U};
    std::optional<PacketEntityWireDeltaBaseTag> wire_delta_base_tag;
    std::optional<PacketEntityResolvedFrameReference> resolved_base;
    std::shared_ptr<const EntitySnapshotState> snapshot;
    StockRuntimeSourceCursor start_cursor{};
    StockRuntimeSourceCursor end_cursor{};
    std::size_t message_ordinal{0U};
};

using PacketEntityStreamEvent =
    std::variant<RuntimeControlEvent, ClientDataMessageEvent,
                 PacketEntityMessageEvent>;

struct PacketEntityDecodedBatch final {
    std::vector<PacketEntityStreamEvent> events;
    StockRuntimeSourceCursor start_cursor{};
    StockRuntimeSourceCursor end_cursor{};
    std::size_t consumed_byte_count{0U};
    std::size_t consumed_bit_count{0U};
    std::uint64_t source_generation{0U};
    std::size_t payload_ordinal{0U};
    PacketEntityCompatibilityProfile profile{
        PacketEntityCompatibilityProfile::public_goldsrc48_packet_entities_v1};
    PacketEntitySpecificationSource specification_source{
        PacketEntitySpecificationSource::public_protocol_reference};
    PacketEntityStockVerification stock_verification{
        PacketEntityStockVerification::
            not_verified_against_stock_runtime_payload};
};

enum class PacketEntityDecodeErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_profile,
    payload_not_decompressed,
    wrong_direction,
    payload_too_large,
    invalid_cursor,
    unsupported_alignment,
    source_generation_mismatch,
    invalid_source_sequence,
    truncated_opcode,
    unsupported_opcode,
    truncated_header,
    invalid_entity_count,
    truncated_terminator,
    truncated_entity_header,
    invalid_entity_number,
    duplicate_entity_record,
    out_of_order_entity_record,
    wire_record_limit_exceeded,
    invalid_baseline_reference,
    schema_mismatch,
    unknown_schema,
    missing_server_time,
    delta_decode_failed,
    invalid_delta_base_lookback,
    missing_delta_base,
    evicted_delta_base,
    incompatible_delta_base,
    remove_nonexistent_entity,
    malformed_padding,
    entity_limit_exceeded,
    total_value_bytes_exceeded,
    duplicate_source_frame,
    conflicting_source_frame,
    old_source_frame,
    message_limit_exceeded,
    history_publish_failed,
    runtime_control_failed,
    clientdata_failed,
    size_overflow,
    unable_to_retain_output,
};

struct PacketEntityDecodeError final {
    PacketEntityDecodeErrorCode code{
        PacketEntityDecodeErrorCode::invalid_configuration};
    PacketEntityRecoveryStatus recovery{PacketEntityRecoveryStatus::none};
    std::optional<StockRuntimeSourceCursor> cursor;
    std::optional<std::uint8_t> wire_opcode;
    std::optional<std::uint32_t> entity_number;
    std::optional<PacketEntityWireDeltaBaseTag> wire_delta_base_tag;
    std::optional<PacketEntityResolvedFrameReference> resolved_base;
    std::optional<DeltaValueErrorCode> delta_error;
    std::optional<ClientDataDecodeErrorCode> clientdata_error;
    std::string context;
};

struct PacketEntityDecodeResult final {
    std::optional<PacketEntityDecodedBatch> batch;
    std::optional<PacketEntityDecodeError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return batch.has_value() && !error.has_value();
    }
};

class PacketEntitySnapshotState final {
public:
    PacketEntitySnapshotState(
        std::uint64_t source_generation,
        std::uint32_t max_clients,
        std::shared_ptr<const DeltaSchemaRegistryState> schemas,
        std::shared_ptr<const EntityBaselineRegistryState> baselines,
        EntitySnapshotLimits limits = {},
        ClientDataDecodeLimits client_data_limits = {});

    PacketEntitySnapshotState(const PacketEntitySnapshotState&) = default;
    PacketEntitySnapshotState& operator=(const PacketEntitySnapshotState&) = default;
    PacketEntitySnapshotState(PacketEntitySnapshotState&&) noexcept = default;
    PacketEntitySnapshotState& operator=(PacketEntitySnapshotState&&) noexcept = default;
    ~PacketEntitySnapshotState() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool reset_source_generation(
        std::uint64_t source_generation,
        std::uint32_t max_clients,
        std::shared_ptr<const DeltaSchemaRegistryState> schemas,
        std::shared_ptr<const EntityBaselineRegistryState> baselines);
    [[nodiscard]] std::uint64_t source_generation() const noexcept;
    [[nodiscard]] std::uint32_t max_clients() const noexcept;
    [[nodiscard]] const RuntimeControlState& control_state() const noexcept;
    [[nodiscard]] const EntityBaselineRegistryState& baselines() const noexcept;
    [[nodiscard]] const EntitySnapshotHistoryState& history() const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntitySnapshotState>&
    current_snapshot() const noexcept;
    [[nodiscard]] const ClientDataSnapshotState& client_data_state()
        const noexcept;

private:
    friend class GoldSrcPacketEntityDecoder;

    struct FrameFingerprint final {
        std::uint32_t sequence{0U};
        std::uint64_t payload_hash{0U};
    };

    std::uint64_t source_generation_{0U};
    std::uint32_t max_clients_{0U};
    EntitySnapshotLimits limits_{};
    RuntimeControlState control_state_;
    ClientDataSnapshotState client_data_state_;
    std::shared_ptr<const DeltaSchemaRegistryState> schemas_;
    std::shared_ptr<const EntityBaselineRegistryState> baselines_;
    std::shared_ptr<const EntitySnapshotHistoryState> history_;
    std::shared_ptr<const EntitySnapshotState> current_snapshot_;
    std::vector<FrameFingerprint> fingerprints_;
};

struct PacketEntityDecodeInput final {
    const OwnedServicePayload& payload;
    StockRuntimeSourceCursor initial_cursor{};
    std::uint64_t source_generation{0U};
    std::size_t payload_ordinal{0U};
    std::string_view ordinary_schema_name{"entity_state_t"};
    std::string_view player_schema_name{"entity_state_player_t"};
    std::string_view custom_schema_name{"custom_entity_state_t"};
    std::string_view client_schema_name{"clientdata_t"};
    std::string_view weapon_schema_name{"weapon_data_t"};
    ClientDataReceiverMode client_receiver_mode{
        ClientDataReceiverMode::ordinary_game_client};
    std::span<const PostMoveVarsUserMessageDefinition>
        user_message_definitions{};
};

class GoldSrcPacketEntityDecoder final {
public:
    explicit GoldSrcPacketEntityDecoder(
        PacketEntityDecodeLimits limits = {},
        PacketEntityCompatibilityProfile profile =
            PacketEntityCompatibilityProfile::
                public_goldsrc48_packet_entities_v1) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const PacketEntityDecodeLimits& limits() const noexcept;
    [[nodiscard]] PacketEntityCompatibilityProfile profile() const noexcept;
    [[nodiscard]] PacketEntityDecodeResult decode_and_apply(
        const PacketEntityDecodeInput& input,
        PacketEntitySnapshotState& state) const;

private:
    PacketEntityDecodeLimits limits_{};
    PacketEntityCompatibilityProfile profile_{
        PacketEntityCompatibilityProfile::public_goldsrc48_packet_entities_v1};
};

[[nodiscard]] std::string_view to_string(
    PacketEntityCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    PacketEntitySpecificationSource source) noexcept;
[[nodiscard]] std::string_view to_string(
    PacketEntityStockVerification verification) noexcept;
[[nodiscard]] std::string_view to_string(PacketEntityMessageKind kind) noexcept;
[[nodiscard]] std::string_view to_string(PacketEntityRecoveryStatus status) noexcept;
[[nodiscard]] std::string_view to_string(PacketEntityDecodeErrorCode code) noexcept;

} // namespace hlclient::goldsrc
