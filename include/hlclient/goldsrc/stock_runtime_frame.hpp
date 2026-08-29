#pragma once

#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/stock_authoritative_movement.hpp>
#include <hlclient/goldsrc/stock_local_player_identity.hpp>
#include <hlclient/goldsrc/stock_server_time.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumStockRuntimeFrameSources = 64U;
inline constexpr std::size_t kHardMaximumStockRuntimeFrameSources = 512U;
inline constexpr std::size_t kDefaultMaximumStockRuntimeFrameComponents = 16U;
inline constexpr std::size_t kHardMaximumStockRuntimeFrameComponents = 128U;
inline constexpr std::size_t kHardMaximumStockRuntimeComponentEntries = 65'536U;
inline constexpr std::size_t kHardMaximumStockRuntimeFrameHistory = 256U;

struct StockRuntimeFrameLimits {
    std::size_t maximum_source_records{
        kDefaultMaximumStockRuntimeFrameSources};
    std::size_t maximum_component_references{
        kDefaultMaximumStockRuntimeFrameComponents};
    std::size_t maximum_accounted_metadata_bytes{1U * 1024U * 1024U};
};

struct StockRuntimeFrameHistoryLimits {
    std::size_t maximum_frames{64U};
    std::size_t maximum_accounted_metadata_bytes{8U * 1024U * 1024U};
    std::uint64_t maximum_history_revision{UINT64_MAX};
};

[[nodiscard]] bool valid_stock_runtime_frame_limits(
    const StockRuntimeFrameLimits& limits) noexcept;
[[nodiscard]] bool valid_stock_runtime_frame_history_limits(
    const StockRuntimeFrameHistoryLimits& limits) noexcept;

enum class StockRuntimeComponentKind : std::uint8_t {
    client_local_data,
    weapon_data,
    visual_projection,
};

// Metadata-only seam for component decoders that remain evidence-pending.
// No decoded field value or packet body is retained here.
struct StockRuntimeComponentReference {
    StockRuntimeComponentKind kind{StockRuntimeComponentKind::client_local_data};
    StockRuntimeMessageCategory source_message_category{
        StockRuntimeMessageCategory::unsupported_runtime_message};
    std::size_t source_record_index{0U};
    std::size_t source_message_ordinal{0U};
    StockRuntimeSourceCursor source_start_cursor{};
    StockRuntimeSourceCursor source_end_cursor{};
    std::size_t entry_count{0U};
    std::size_t accounted_metadata_bytes{0U};
    StockRuntimeCandidateConfidence confidence{
        StockRuntimeCandidateConfidence::evidence_pending};

    [[nodiscard]] friend bool operator==(
        const StockRuntimeComponentReference&,
        const StockRuntimeComponentReference&) = default;
};

enum class StockRuntimeFrameStatus : std::uint8_t {
    evidence_pending,
    partial_evidence_pending,
    component_conflict,
};

enum class StockRuntimeFrameErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_runtime_generation,
    invalid_frame_ordinal,
    invalid_source_metadata,
    invalid_component_reference,
    source_limit_exceeded,
    component_limit_exceeded,
    metadata_limit_exceeded,
    duplicate_component_reference,
    profile_mismatch,
    runtime_generation_mismatch,
    local_player_candidate_mismatch,
    acknowledgement_evidence_mismatch,
    server_time_observation_mismatch,
    incompatible_entity_snapshot,
    stock_evidence_pending,
    allocation_failed,
    duplicate_frame,
    out_of_order_frame,
    history_limit_exceeded,
    history_backpressure,
    history_revision_exhausted,
};

struct StockRuntimeFrameError {
    StockRuntimeFrameErrorCode code{
        StockRuntimeFrameErrorCode::invalid_configuration};
    std::string_view context;
};

struct StockRuntimeFrameCreateInfo {
    std::uint64_t runtime_generation{0U};
    std::uint64_t frame_ordinal{0U};
    std::optional<StockServerTimeObservation> server_time;
    std::optional<StockRuntimeSnapshotReferenceCandidate> snapshot_reference;
    std::shared_ptr<const EntitySnapshotState> entity_snapshot;
    std::vector<StockRuntimeComponentReference> component_references;
    std::shared_ptr<const StockRuntimeMessageCatalogState> message_catalog;
    std::shared_ptr<const StockLocalPlayerIdentityState> local_player_identity;
    std::shared_ptr<const StockCommandAcknowledgementEvidenceState>
        command_acknowledgement_evidence;
    std::shared_ptr<const StockAuthoritativeMovementObservation>
        authoritative_observation;
    std::vector<StockRuntimeSourceMetadata> source_messages;
    StockRuntimeCompatibilityProfile compatibility_profile{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

class StockRuntimeFrameState final {
public:
    struct CreationResult;

    StockRuntimeFrameState(const StockRuntimeFrameState&) = default;
    StockRuntimeFrameState(StockRuntimeFrameState&&) noexcept = default;
    StockRuntimeFrameState& operator=(const StockRuntimeFrameState&) = delete;
    StockRuntimeFrameState& operator=(StockRuntimeFrameState&&) = delete;
    ~StockRuntimeFrameState() = default;

    [[nodiscard]] static CreationResult create(
        const StockRuntimeFrameCreateInfo& create_info,
        const StockRuntimeFrameLimits& limits = {}) noexcept;

    [[nodiscard]] std::uint64_t runtime_generation() const noexcept;
    [[nodiscard]] std::uint64_t frame_ordinal() const noexcept;
    [[nodiscard]] const std::optional<StockServerTimeObservation>&
    server_time() const noexcept;
    [[nodiscard]] const std::optional<StockRuntimeSnapshotReferenceCandidate>&
    snapshot_reference() const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntitySnapshotState>&
    entity_snapshot() const noexcept;
    [[nodiscard]] std::span<const StockRuntimeComponentReference>
    component_references() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const StockRuntimeMessageCatalogState>&
    message_catalog() const noexcept;
    [[nodiscard]] const std::shared_ptr<const StockLocalPlayerIdentityState>&
    local_player_identity() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const StockCommandAcknowledgementEvidenceState>&
    command_acknowledgement_evidence() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const StockAuthoritativeMovementObservation>&
    authoritative_observation() const noexcept;
    [[nodiscard]] std::span<const StockRuntimeSourceMetadata> source_messages()
        const noexcept;
    [[nodiscard]] std::size_t accounted_metadata_bytes() const noexcept;
    [[nodiscard]] StockRuntimeFrameStatus status() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    StockRuntimeFrameState(
        StockRuntimeFrameCreateInfo create_info,
        std::size_t accounted_metadata_bytes,
        StockRuntimeFrameStatus status) noexcept;

    std::uint64_t runtime_generation_{0U};
    std::uint64_t frame_ordinal_{0U};
    std::optional<StockServerTimeObservation> server_time_;
    std::optional<StockRuntimeSnapshotReferenceCandidate> snapshot_reference_;
    std::shared_ptr<const EntitySnapshotState> entity_snapshot_;
    std::vector<StockRuntimeComponentReference> component_references_;
    std::shared_ptr<const StockRuntimeMessageCatalogState> message_catalog_;
    std::shared_ptr<const StockLocalPlayerIdentityState> local_player_identity_;
    std::shared_ptr<const StockCommandAcknowledgementEvidenceState>
        command_acknowledgement_evidence_;
    std::shared_ptr<const StockAuthoritativeMovementObservation>
        authoritative_observation_;
    std::vector<StockRuntimeSourceMetadata> source_messages_;
    std::size_t accounted_metadata_bytes_{0U};
    StockRuntimeFrameStatus status_{StockRuntimeFrameStatus::evidence_pending};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

struct StockRuntimeFrameState::CreationResult {
    std::optional<StockRuntimeFrameState> frame;
    std::optional<StockRuntimeFrameError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frame.has_value() && !error.has_value();
    }
};

class StockRuntimeFrameHistoryState final {
public:
    struct CreationResult;
    struct AppendResult;

    StockRuntimeFrameHistoryState(const StockRuntimeFrameHistoryState&) =
        default;
    StockRuntimeFrameHistoryState(StockRuntimeFrameHistoryState&&) noexcept =
        default;
    StockRuntimeFrameHistoryState& operator=(
        const StockRuntimeFrameHistoryState&) = delete;
    StockRuntimeFrameHistoryState& operator=(
        StockRuntimeFrameHistoryState&&) = delete;
    ~StockRuntimeFrameHistoryState() = default;

    [[nodiscard]] static CreationResult create(
        std::uint64_t runtime_generation,
        StockRuntimeFrameHistoryLimits limits = {},
        StockRuntimeCompatibilityProfile compatibility_profile =
            StockRuntimeCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending,
        StockRuntimeEvidenceProfile evidence_profile =
            StockRuntimeEvidenceProfile::
                controlled_signed_stock_transcript_pending) noexcept;

    [[nodiscard]] AppendResult append(
        const StockRuntimeFrameState& frame) const noexcept;
    [[nodiscard]] std::span<const StockRuntimeFrameState> frames()
        const noexcept;
    [[nodiscard]] const StockRuntimeFrameState* find_exact(
        std::uint64_t frame_ordinal) const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] std::size_t accounted_metadata_bytes() const noexcept;
    [[nodiscard]] std::uint64_t history_revision() const noexcept;
    [[nodiscard]] std::uint64_t runtime_generation() const noexcept;
    [[nodiscard]] const StockRuntimeFrameHistoryLimits& limits() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    StockRuntimeFrameHistoryState(
        std::uint64_t runtime_generation,
        StockRuntimeFrameHistoryLimits limits,
        std::vector<StockRuntimeFrameState> frames,
        std::size_t accounted_metadata_bytes,
        std::uint64_t history_revision,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile) noexcept;

    std::uint64_t runtime_generation_{0U};
    StockRuntimeFrameHistoryLimits limits_{};
    std::vector<StockRuntimeFrameState> frames_;
    std::size_t accounted_metadata_bytes_{0U};
    std::uint64_t history_revision_{1U};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

struct StockRuntimeFrameHistoryState::CreationResult {
    std::optional<StockRuntimeFrameHistoryState> history;
    std::optional<StockRuntimeFrameError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return history.has_value() && !error.has_value();
    }
};

struct StockRuntimeFrameHistoryState::AppendResult {
    std::optional<StockRuntimeFrameHistoryState> history;
    std::optional<StockRuntimeFrameError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return history.has_value() && !error.has_value();
    }
};

[[nodiscard]] std::string_view to_string(StockRuntimeFrameStatus status)
    noexcept;
[[nodiscard]] std::string_view to_string(StockRuntimeFrameErrorCode code)
    noexcept;

} // namespace hlclient::goldsrc
