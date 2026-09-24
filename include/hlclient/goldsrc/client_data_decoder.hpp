#pragma once

#include <hlclient/goldsrc/delta_value_decoder.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>
#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kGoldSrcSvcClientDataOpcode = 15U;
inline constexpr std::size_t kGoldSrcClientDataBasePresentBits = 1U;
inline constexpr std::size_t kGoldSrcClientDataBaseTagBits = 8U;
inline constexpr std::size_t kGoldSrcWeaponContinuationBits = 1U;
inline constexpr std::size_t kGoldSrcWeaponIndexBits = 6U;
inline constexpr std::size_t kGoldSrcWeaponSlotCount = 64U;
// The ordinary client message uses the same acknowledged 64-frame server
// history as packet entities. Distances zero and >=63 are not usable bases.
inline constexpr std::uint32_t kGoldSrcClientDataMaximumDeltaLookback = 62U;

enum class ClientDataCompatibilityProfile : std::uint8_t {
    public_goldsrc48_clientdata_v1,
};

enum class ClientDataSpecificationSource : std::uint8_t {
    public_protocol_reference,
};

enum class ClientDataStockVerification : std::uint8_t {
    not_verified_against_stock_runtime_payload,
};

enum class ClientDataReceiverMode : std::uint8_t {
    ordinary_game_client,
    proxy_or_hltv,
};

enum class ClientDataRecoveryStatus : std::uint8_t {
    none,
    no_base_message_required,
};

struct ClientDataWireDeltaBaseTag final {
    std::uint8_t value{0U};

    [[nodiscard]] friend bool operator==(
        const ClientDataWireDeltaBaseTag&,
        const ClientDataWireDeltaBaseTag&) noexcept = default;
};

struct ClientDataResolvedFrameReference final {
    std::uint32_t source_transport_sequence{0U};

    [[nodiscard]] friend bool operator==(
        const ClientDataResolvedFrameReference&,
        const ClientDataResolvedFrameReference&) noexcept = default;
};

class ClientDataFrameReference final {
public:
    [[nodiscard]] static std::optional<ClientDataFrameReference>
    from_transport_sequence(std::uint32_t value) noexcept;

    [[nodiscard]] std::uint32_t source_transport_sequence() const noexcept;

    [[nodiscard]] friend bool operator==(
        const ClientDataFrameReference&,
        const ClientDataFrameReference&) noexcept = default;

private:
    explicit ClientDataFrameReference(std::uint32_t value) noexcept;

    std::uint32_t source_transport_sequence_{0U};
};

struct ClientDataDecodeLimits final {
    GoldSrcDeltaValueLimits delta_values{};
    std::size_t maximum_payload_bytes{65'536U};
    std::size_t maximum_weapon_records{kGoldSrcWeaponSlotCount};
    std::size_t maximum_history_frames{64U};
    std::size_t maximum_frame_value_bytes{1U << 20U};
    std::size_t maximum_history_value_bytes{16U << 20U};

    [[nodiscard]] friend bool operator==(
        const ClientDataDecodeLimits&,
        const ClientDataDecodeLimits&) noexcept = default;
};

inline constexpr std::size_t kMaximumClientDataPayloadBytes = 1U << 20U;
inline constexpr std::size_t kMaximumClientDataHistoryFrames = 64U;
inline constexpr std::size_t kMaximumClientDataFrameValueBytes = 8U << 20U;
inline constexpr std::size_t kMaximumClientDataHistoryValueBytes = 64U << 20U;

[[nodiscard]] bool valid_client_data_profile(
    ClientDataCompatibilityProfile profile) noexcept;
[[nodiscard]] bool valid_client_data_decode_limits(
    const ClientDataDecodeLimits& limits) noexcept;

struct ClientDataFrameProvenance final {
    std::uint64_t source_generation{0U};
    std::uint32_t source_transport_sequence{0U};
    std::size_t payload_ordinal{0U};
    std::size_t source_payload_bytes{0U};
    StockRuntimeSourceCursor start_cursor{};
    StockRuntimeSourceCursor end_cursor{};

    [[nodiscard]] friend bool operator==(
        const ClientDataFrameProvenance&,
        const ClientDataFrameProvenance&) noexcept = default;
};

struct ClientDataFrameStatistics final {
    bool client_fields_changed{false};
    std::size_t wire_weapon_record_count{0U};
    std::size_t changed_weapon_slot_count{0U};
    std::size_t unchanged_weapon_slot_count{kGoldSrcWeaponSlotCount};
    std::size_t omitted_weapon_slot_count{kGoldSrcWeaponSlotCount};
    std::size_t accounted_value_bytes{0U};

    [[nodiscard]] friend bool operator==(
        const ClientDataFrameStatistics&,
        const ClientDataFrameStatistics&) noexcept = default;
};

class ClientWeaponSlotState final {
public:
    ClientWeaponSlotState(const ClientWeaponSlotState&) = default;
    ClientWeaponSlotState& operator=(const ClientWeaponSlotState&) = default;
    ClientWeaponSlotState(ClientWeaponSlotState&&) noexcept = default;
    ClientWeaponSlotState& operator=(ClientWeaponSlotState&&) noexcept = default;
    ~ClientWeaponSlotState() = default;

    [[nodiscard]] std::uint8_t wire_index() const noexcept;
    [[nodiscard]] const DeltaObjectState& object() const noexcept;
    [[nodiscard]] bool shares_object_with(
        const ClientWeaponSlotState& other) const noexcept;

private:
    friend class GoldSrcClientDataDecoder;

    ClientWeaponSlotState(
        std::uint8_t wire_index,
        std::shared_ptr<const DeltaObjectState> object) noexcept;

    std::uint8_t wire_index_{0U};
    std::shared_ptr<const DeltaObjectState> object_;
};

class ClientDataFrameState final {
public:
    ClientDataFrameState(const ClientDataFrameState&) = default;
    ClientDataFrameState& operator=(const ClientDataFrameState&) = delete;
    ClientDataFrameState(ClientDataFrameState&&) noexcept = default;
    ClientDataFrameState& operator=(ClientDataFrameState&&) noexcept = delete;
    ~ClientDataFrameState() = default;

    [[nodiscard]] const ClientDataFrameReference& reference() const noexcept;
    [[nodiscard]] const std::optional<ClientDataFrameReference>&
    base_reference() const noexcept;
    [[nodiscard]] double server_time_seconds() const noexcept;
    [[nodiscard]] const DeltaObjectState& client_data() const noexcept;
    [[nodiscard]] std::span<const ClientWeaponSlotState> weapon_slots()
        const noexcept;
    [[nodiscard]] const ClientWeaponSlotState* find_weapon_slot(
        std::uint8_t wire_index) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> wire_updated_weapon_indices()
        const noexcept;
    [[nodiscard]] const ClientDataFrameProvenance& provenance() const noexcept;
    [[nodiscard]] const ClientDataFrameStatistics& statistics() const noexcept;
    [[nodiscard]] ClientDataCompatibilityProfile profile() const noexcept;
    [[nodiscard]] bool shares_client_object_with(
        const ClientDataFrameState& other) const noexcept;

private:
    friend class GoldSrcClientDataDecoder;

    ClientDataFrameState(
        ClientDataFrameReference reference,
        std::optional<ClientDataFrameReference> base_reference,
        double server_time_seconds,
        std::shared_ptr<const DeltaObjectState> client_data,
        std::vector<ClientWeaponSlotState> weapon_slots,
        std::vector<std::uint8_t> wire_updated_weapon_indices,
        ClientDataFrameProvenance provenance,
        ClientDataFrameStatistics statistics,
        ClientDataCompatibilityProfile profile) noexcept;

    ClientDataFrameReference reference_;
    std::optional<ClientDataFrameReference> base_reference_;
    double server_time_seconds_{0.0};
    std::shared_ptr<const DeltaObjectState> client_data_;
    std::vector<ClientWeaponSlotState> weapon_slots_;
    std::vector<std::uint8_t> wire_updated_weapon_indices_;
    ClientDataFrameProvenance provenance_{};
    ClientDataFrameStatistics statistics_{};
    ClientDataCompatibilityProfile profile_{
        ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1};
};

enum class ClientDataHistoryReferenceStatus : std::uint8_t {
    retained,
    evicted,
    missing,
    future,
};

class ClientDataHistoryState final {
public:
    [[nodiscard]] std::span<const std::shared_ptr<const ClientDataFrameState>>
    frames() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] const ClientDataFrameState* find_exact(
        const ClientDataFrameReference& reference) const noexcept;
    [[nodiscard]] ClientDataHistoryReferenceStatus classify(
        const ClientDataFrameReference& reference) const noexcept;
    [[nodiscard]] std::optional<ClientDataFrameReference>
    newest_reference() const noexcept;
    [[nodiscard]] std::optional<ClientDataFrameReference>
    evicted_through() const noexcept;
    [[nodiscard]] std::size_t accounted_value_bytes() const noexcept;
    [[nodiscard]] std::uint64_t source_generation() const noexcept;
    [[nodiscard]] ClientDataCompatibilityProfile profile() const noexcept;

private:
    friend class ClientDataSnapshotState;
    friend class GoldSrcClientDataDecoder;

    ClientDataHistoryState(
        std::vector<std::shared_ptr<const ClientDataFrameState>> frames,
        std::optional<ClientDataFrameReference> evicted_through,
        std::size_t accounted_value_bytes,
        std::uint64_t source_generation,
        ClientDataCompatibilityProfile profile) noexcept;

    std::vector<std::shared_ptr<const ClientDataFrameState>> frames_;
    std::optional<ClientDataFrameReference> evicted_through_;
    std::size_t accounted_value_bytes_{0U};
    std::uint64_t source_generation_{0U};
    ClientDataCompatibilityProfile profile_{
        ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1};
};

class ClientDataSnapshotState final {
public:
    ClientDataSnapshotState(
        std::uint64_t source_generation,
        std::shared_ptr<const DeltaSchemaRegistryState> schemas,
        ClientDataDecodeLimits limits = {},
        ClientDataCompatibilityProfile profile =
            ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1);

    ClientDataSnapshotState(const ClientDataSnapshotState&) = default;
    ClientDataSnapshotState& operator=(const ClientDataSnapshotState&) = default;
    ClientDataSnapshotState(ClientDataSnapshotState&&) noexcept = default;
    ClientDataSnapshotState& operator=(ClientDataSnapshotState&&) noexcept = default;
    ~ClientDataSnapshotState() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool reset_source_generation(
        std::uint64_t source_generation,
        std::shared_ptr<const DeltaSchemaRegistryState> schemas);
    [[nodiscard]] std::uint64_t source_generation() const noexcept;
    [[nodiscard]] const DeltaSchemaRegistryState& schemas() const noexcept;
    [[nodiscard]] const ClientDataHistoryState& history() const noexcept;
    [[nodiscard]] const std::shared_ptr<const ClientDataFrameState>&
    current_frame() const noexcept;
    [[nodiscard]] const ClientDataDecodeLimits& limits() const noexcept;
    [[nodiscard]] ClientDataCompatibilityProfile profile() const noexcept;

private:
    friend class GoldSrcClientDataDecoder;

    struct FrameFingerprint final {
        std::uint32_t sequence{0U};
        std::uint64_t payload_hash{0U};
    };

    std::uint64_t source_generation_{0U};
    std::shared_ptr<const DeltaSchemaRegistryState> schemas_;
    std::shared_ptr<const ClientDataHistoryState> history_;
    std::shared_ptr<const ClientDataFrameState> current_frame_;
    ClientDataDecodeLimits limits_{};
    ClientDataCompatibilityProfile profile_{
        ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1};
    std::vector<FrameFingerprint> fingerprints_;
};

struct ClientDataDecodeInput final {
    const OwnedServicePayload& payload;
    StockRuntimeSourceCursor start_cursor{};
    std::uint64_t source_generation{0U};
    std::size_t payload_ordinal{0U};
    std::size_t message_ordinal{0U};
    std::optional<double> server_time_seconds;
    ClientDataReceiverMode receiver_mode{
        ClientDataReceiverMode::ordinary_game_client};
    std::string_view client_schema_name{"clientdata_t"};
    std::string_view weapon_schema_name{"weapon_data_t"};
};

struct ClientDataMessageEvent final {
    std::optional<ClientDataWireDeltaBaseTag> wire_delta_base_tag;
    std::optional<ClientDataResolvedFrameReference> resolved_base;
    std::shared_ptr<const ClientDataFrameState> frame;
    StockRuntimeSourceCursor start_cursor{};
    StockRuntimeSourceCursor end_cursor{};
    std::size_t message_ordinal{0U};
    ClientDataSpecificationSource specification_source{
        ClientDataSpecificationSource::public_protocol_reference};
    ClientDataStockVerification stock_verification{
        ClientDataStockVerification::
            not_verified_against_stock_runtime_payload};
};

enum class ClientDataDecodeErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_profile,
    unsupported_receiver_mode,
    payload_not_decompressed,
    wrong_direction,
    payload_too_large,
    invalid_cursor,
    unsupported_alignment,
    source_generation_mismatch,
    invalid_source_sequence,
    wrong_opcode,
    truncated_base_flag,
    truncated_base_tag,
    invalid_delta_base_lookback,
    missing_delta_base,
    evicted_delta_base,
    incompatible_delta_base,
    missing_server_time,
    unknown_schema,
    default_state_failed,
    client_delta_failed,
    truncated_weapon_flag,
    truncated_weapon_index,
    duplicate_weapon_index,
    out_of_order_weapon_index,
    weapon_record_limit_exceeded,
    weapon_delta_failed,
    malformed_padding,
    frame_value_limit_exceeded,
    history_value_limit_exceeded,
    duplicate_source_frame,
    conflicting_source_frame,
    old_source_frame,
    history_publish_failed,
    size_overflow,
    unable_to_retain_output,
};

struct ClientDataDecodeError final {
    ClientDataDecodeErrorCode code{
        ClientDataDecodeErrorCode::invalid_configuration};
    ClientDataRecoveryStatus recovery{ClientDataRecoveryStatus::none};
    std::optional<StockRuntimeSourceCursor> cursor;
    std::optional<std::uint8_t> weapon_index;
    std::optional<ClientDataWireDeltaBaseTag> wire_delta_base_tag;
    std::optional<ClientDataResolvedFrameReference> resolved_base;
    std::optional<DeltaValueErrorCode> delta_error;
    std::string context;
};

struct ClientDataDecodeResult final {
    std::optional<ClientDataMessageEvent> event;
    std::optional<ClientDataDecodeError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return event.has_value() && !error.has_value();
    }
};

class GoldSrcClientDataDecoder final {
public:
    explicit GoldSrcClientDataDecoder(
        ClientDataDecodeLimits limits = {},
        ClientDataCompatibilityProfile profile =
            ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1)
        noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ClientDataDecodeLimits& limits() const noexcept;
    [[nodiscard]] ClientDataCompatibilityProfile profile() const noexcept;
    [[nodiscard]] ClientDataDecodeResult decode_one_and_apply(
        const ClientDataDecodeInput& input,
        ClientDataSnapshotState& state) const;

private:
    ClientDataDecodeLimits limits_{};
    ClientDataCompatibilityProfile profile_{
        ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1};
};

[[nodiscard]] std::string_view to_string(
    ClientDataCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    ClientDataSpecificationSource source) noexcept;
[[nodiscard]] std::string_view to_string(
    ClientDataStockVerification verification) noexcept;
[[nodiscard]] std::string_view to_string(ClientDataReceiverMode mode) noexcept;
[[nodiscard]] std::string_view to_string(ClientDataRecoveryStatus status) noexcept;
[[nodiscard]] std::string_view to_string(ClientDataDecodeErrorCode code) noexcept;

} // namespace hlclient::goldsrc
