#pragma once

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/goldsrc/packet_entity_decoder.hpp>
#include <hlclient/goldsrc/move_vars.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

enum class RuntimeReplayCompatibilityProfile : std::uint8_t {
    public_goldsrc48_runtime_replay_v1,
};

enum class RuntimeReplaySpecificationSource : std::uint8_t {
    public_protocol_reference,
};

enum class RuntimeReplayStockVerification : std::uint8_t {
    not_verified_against_stock_runtime_payload,
};

enum class RuntimeReplayRecoveryStatus : std::uint8_t {
    none,
    entity_full_snapshot_required,
    clientdata_no_base_required,
};

enum class RuntimeReplaySessionStatus : std::uint8_t {
    active,
    finished,
};

struct RuntimeReplayLimits final {
    PacketEntityDecodeLimits decoder{};
    std::size_t maximum_projected_entities{4'096U};
    std::size_t maximum_record_fingerprints{65'536U};

    [[nodiscard]] friend bool operator==(
        const RuntimeReplayLimits&,
        const RuntimeReplayLimits&) noexcept = default;
};

inline constexpr std::size_t kMaximumRuntimeReplayProjectedEntities = 16'384U;
inline constexpr std::size_t kMaximumRuntimeReplayRecordFingerprints =
    1'048'576U;

struct RuntimeReplaySchemaBindings final {
    std::string ordinary_entity{"entity_state_t"};
    std::string player_entity{"entity_state_player_t"};
    std::string custom_entity{"custom_entity_state_t"};
    std::string client_data{"clientdata_t"};
    std::string weapon_data{"weapon_data_t"};
};

struct RuntimeReplayInitialization final {
    std::uint64_t generation{0U};
    std::uint32_t max_clients{0U};
    std::shared_ptr<const DeltaSchemaRegistryState> schemas;
    std::shared_ptr<const EntityBaselineRegistryState> baselines;
    std::vector<PostMoveVarsUserMessageDefinition> user_message_definitions;
    RuntimeReplaySchemaBindings schema_bindings{};
    RuntimeReplayLimits limits{};
    RuntimeReplayCompatibilityProfile profile{
        RuntimeReplayCompatibilityProfile::
            public_goldsrc48_runtime_replay_v1};
};

// Project-owned replay envelope. `payload.bytes` is exactly one already
// extracted, owning, reassembled and decompressed server service payload.
// This is not a DEM/PCAP/netchan format.
struct RuntimeReplayRecord final {
    std::uint64_t generation{0U};
    std::uint64_t record_identity{0U};
    std::size_t record_ordinal{0U};
    OwnedServicePayload payload;
    StockRuntimeSourceCursor initial_cursor{};
    RuntimeReplayCompatibilityProfile profile{
        RuntimeReplayCompatibilityProfile::
            public_goldsrc48_runtime_replay_v1};
};

enum class RuntimeReplayErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_profile,
    target_already_has_runtime_observation,
    session_finished,
    generation_mismatch,
    invalid_record_identity,
    invalid_record_ordinal,
    old_record,
    duplicate_record,
    conflicting_record,
    record_identity_limit_exceeded,
    client_world_state_changed,
    decoder_failed,
    semantic_schema_mismatch,
    semantic_value_mismatch,
    non_finite_semantic_value,
    projection_limit_exceeded,
    bridge_rejected_candidate,
    publication_revision_overflow,
    unable_to_retain_candidate,
};

struct RuntimeReplayError final {
    RuntimeReplayErrorCode code{RuntimeReplayErrorCode::invalid_configuration};
    RuntimeReplayRecoveryStatus recovery{RuntimeReplayRecoveryStatus::none};
    std::optional<PacketEntityDecodeErrorCode> decoder_error;
    std::optional<ClientDataDecodeErrorCode> clientdata_error;
    std::optional<DeltaValueErrorCode> delta_error;
    std::optional<StockRuntimeSourceCursor> decoder_cursor;
    std::optional<std::uint8_t> decoder_wire_opcode;
    std::optional<std::uint64_t> record_identity;
    std::optional<std::size_t> record_ordinal;
    std::string context;
};

struct RuntimeReplayApplyEvent final {
    std::uint64_t record_identity{0U};
    std::size_t record_ordinal{0U};
    std::uint32_t source_transport_sequence{0U};
    bool server_time_observed{false};
    bool clientdata_observed{false};
    bool entities_observed{false};
    std::uint64_t publication_revision{0U};
    std::uint64_t canonical_state_hash{0U};
    PacketEntityDecodedBatch decoded_batch;
};

struct RuntimeReplayApplyResult final {
    std::optional<RuntimeReplayApplyEvent> event;
    std::optional<RuntimeReplayError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return event.has_value() && !error.has_value();
    }
};

struct RuntimeReplayInitializeResult final {
    std::unique_ptr<class RuntimeReplaySession> session;
    std::optional<RuntimeReplayError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return session != nullptr && !error.has_value();
    }
};

class RuntimeReplaySession final {
public:
    RuntimeReplaySession(const RuntimeReplaySession&) = delete;
    RuntimeReplaySession& operator=(const RuntimeReplaySession&) = delete;
    RuntimeReplaySession(RuntimeReplaySession&&) = delete;
    RuntimeReplaySession& operator=(RuntimeReplaySession&&) = delete;
    ~RuntimeReplaySession() = default;

    [[nodiscard]] static RuntimeReplayInitializeResult initialize(
        RuntimeReplayInitialization initialization,
        client::ClientWorldState& target);

    [[nodiscard]] RuntimeReplayApplyResult apply_record(
        const RuntimeReplayRecord& record);
    [[nodiscard]] std::optional<RuntimeReplayError> reset_generation(
        RuntimeReplayInitialization initialization);
    void finish() noexcept;

    [[nodiscard]] RuntimeReplaySessionStatus status() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uint64_t publication_revision() const noexcept;
    [[nodiscard]] const PacketEntitySnapshotState& decoder_state()
        const noexcept;
    [[nodiscard]] const client::ClientWorldState& read_committed_state()
        const noexcept;

private:
    struct RecordFingerprint final {
        std::uint64_t identity{0U};
        std::uint64_t hash{0U};
    };

    RuntimeReplaySession(
        RuntimeReplayInitialization initialization,
        PacketEntitySnapshotState decoder_state,
        GoldSrcPacketEntityDecoder decoder,
        client::ClientWorldState& target,
        std::uint64_t publication_revision,
        std::uint64_t committed_state_hash) noexcept;

    RuntimeReplayInitialization initialization_;
    PacketEntitySnapshotState decoder_state_;
    GoldSrcPacketEntityDecoder decoder_;
    client::ClientWorldState* target_{nullptr};
    std::vector<RecordFingerprint> record_fingerprints_;
    std::size_t last_record_ordinal_{0U};
    std::uint64_t publication_revision_{0U};
    std::uint64_t committed_state_hash_{0U};
    RuntimeReplaySessionStatus status_{RuntimeReplaySessionStatus::active};
};

[[nodiscard]] bool valid_runtime_replay_limits(
    const RuntimeReplayLimits& limits) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplaySpecificationSource source) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayStockVerification verification) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayRecoveryStatus recovery) noexcept;
[[nodiscard]] std::string_view to_string(RuntimeReplayErrorCode code) noexcept;

} // namespace hlclient::goldsrc
