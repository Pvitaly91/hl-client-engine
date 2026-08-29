#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::movement {
enum class LocalMovementCollisionProfile : std::uint8_t;
}

namespace hlclient::prediction {

inline constexpr std::size_t kDefaultMaximumPredictionHistoryEntries = 64U;
inline constexpr std::size_t kHardMaximumPredictionHistoryEntries = 256U;
inline constexpr std::size_t kDefaultMaximumPredictionReplayCommands = 64U;
inline constexpr std::size_t kHardMaximumPredictionReplayCommands = 256U;

enum class PredictionCompatibilityProfile : std::uint8_t {
    synthetic_authoritative_reconciliation_v1,
    stock_protocol_48_authoritative_reconciliation_evidence_pending,
};

enum class PredictionEvidenceProfile : std::uint8_t {
    project_typed_authoritative_states_and_independent_fixtures,
    stock_ack_and_player_state_semantics_pending,
};

enum class PredictionAcknowledgementProfile : std::uint8_t {
    synthetic_uint32_non_wrapping_v1,
    stock_usercmd_acknowledgement_evidence_pending,
};

enum class AuthoritativePlayerDiscontinuity : std::uint8_t {
    normal,
    teleport,
    respawn_or_hard_reset,
};

enum class PredictionCorrectionClass : std::uint8_t {
    exact,
    replay_without_visual_offset,
    small_visual_correction,
    large_snap,
    teleport_snap,
    hard_reset,
};

enum class PredictionVisualCorrectionProfile : std::uint8_t {
    project_linear_decay_collision_constrained_v1,
    no_smoothing_snap_v1,
    stock_visual_correction_evidence_pending,
};

enum class PredictionErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_session_identity,
    prediction_session_mismatch,
    stock_evidence_pending,
    invalid_authoritative_state,
    authoritative_state_blocking,
    invalid_authority_update_ordinal,
    authoritative_acknowledgement_missing,
    future_acknowledgement,
    acknowledgement_missing,
    acknowledgement_evicted,
    stale_authoritative_update,
    conflicting_authoritative_update,
    prediction_command_gap,
    duplicate_predicted_command,
    out_of_order_predicted_command,
    prediction_history_full,
    prediction_history_backpressure,
    prediction_replay_limit_exceeded,
    prediction_replay_failed,
    movement_environment_mismatch,
    collision_world_mismatch,
    movement_config_mismatch,
    hard_reset_generation_required,
    authoritative_update_backpressure,
    visual_correction_failed,
    visual_correction_collision_failed,
    revision_exhausted,
    allocation_failed,
    non_finite_result,
};

struct PredictionError {
    PredictionErrorCode code{PredictionErrorCode::invalid_configuration};
    std::optional<goldsrc::GoldSrcUserCmdSequence> command_sequence;
    std::string_view context;
};

// Scalar-only identity. It deliberately carries no paths, sockets, credentials
// or native handles, so it is safe to retain in prediction history and evidence.
struct PredictionSessionIdentity {
    std::uint64_t session_generation{0U};
    std::uint64_t prediction_generation{0U};
    std::uint64_t collision_world_primary{0U};
    std::uint64_t collision_world_secondary{0U};
    std::uint64_t collision_world_revision{0U};
    std::uint64_t collision_scene_signature{0U};
    goldsrc::movement::LocalMovementCollisionProfile collision_profile{};
    std::uint64_t movement_environment_signature{0U};
    std::uint64_t movement_config_signature{0U};
    std::uint64_t spawn_initial_state_signature{0U};
    movement::GoldSrcMovementCommandProfile command_profile{
        movement::GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1};
    PredictionCompatibilityProfile prediction_profile{
        PredictionCompatibilityProfile::
            synthetic_authoritative_reconciliation_v1};
    PredictionAcknowledgementProfile acknowledgement_profile{
        PredictionAcknowledgementProfile::synthetic_uint32_non_wrapping_v1};

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] friend bool operator==(
        const PredictionSessionIdentity&,
        const PredictionSessionIdentity&) = default;
};

class AuthoritativeCommandAcknowledgement final {
public:
    [[nodiscard]] static constexpr AuthoritativeCommandAcknowledgement none()
        noexcept
    {
        return AuthoritativeCommandAcknowledgement{};
    }

    [[nodiscard]] static constexpr AuthoritativeCommandAcknowledgement
    for_sequence(const goldsrc::GoldSrcUserCmdSequence sequence) noexcept
    {
        return sequence.valid()
            ? AuthoritativeCommandAcknowledgement{sequence}
            : AuthoritativeCommandAcknowledgement{};
    }

    [[nodiscard]] constexpr bool has_sequence() const noexcept
    {
        return sequence_.has_value();
    }

    [[nodiscard]] constexpr const std::optional<goldsrc::GoldSrcUserCmdSequence>&
    sequence() const noexcept
    {
        return sequence_;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const AuthoritativeCommandAcknowledgement&,
        const AuthoritativeCommandAcknowledgement&) noexcept = default;

private:
    constexpr AuthoritativeCommandAcknowledgement() noexcept = default;
    explicit constexpr AuthoritativeCommandAcknowledgement(
        const goldsrc::GoldSrcUserCmdSequence sequence) noexcept
        : sequence_{sequence}
    {
    }

    std::optional<goldsrc::GoldSrcUserCmdSequence> sequence_;
};

struct PredictionTouchSummary {
    std::size_t touch_count{0U};
    std::optional<movement::PlayerMovementHitKind> first_hit_kind;
    std::optional<movement::PlayerMovementHitKind> last_hit_kind;
    bool start_solid{false};
    bool all_solid{false};
    std::uint64_t deterministic_signature{0U};
    std::size_t accounted_bytes{0U};
};

struct LocalPredictionHistoryLimits {
    std::size_t maximum_entries{kDefaultMaximumPredictionHistoryEntries};
    std::size_t maximum_retained_state_bytes{512U * 1024U};
    std::size_t maximum_retained_command_bytes{128U * 1024U};
    std::size_t maximum_authority_delay_commands{64U};
    std::size_t maximum_replay_commands{kDefaultMaximumPredictionReplayCommands};
    std::uint64_t maximum_history_revision{UINT64_MAX};
    std::size_t maximum_touch_summary_bytes{16U * 1024U};
};

struct PredictionReconciliationLimits {
    std::size_t maximum_replay_commands{kDefaultMaximumPredictionReplayCommands};
    std::size_t maximum_replay_substeps{8'192U};
    std::size_t maximum_replay_traces{65'536U};
    std::size_t maximum_replay_touches{16'384U};
    std::uint64_t maximum_authoritative_updates{UINT64_MAX};
    std::size_t maximum_pending_authority_updates{64U};
    std::size_t maximum_reconciliation_bytes{2U * 1024U * 1024U};
    std::size_t maximum_reconciliations_per_update{8U};
    std::uint64_t maximum_replay_time_nanoseconds{5'000'000'000ULL};
    std::uint64_t maximum_prediction_revision{UINT64_MAX};
};

struct PredictionReplayStatistics {
    std::size_t replayed_command_count{0U};
    std::uint64_t substep_count{0U};
    std::uint64_t trace_count{0U};
    std::uint64_t touch_count{0U};
    std::optional<goldsrc::GoldSrcUserCmdSequence> first_sequence;
    std::optional<goldsrc::GoldSrcUserCmdSequence> last_sequence;
};

struct LocalPredictionStatistics {
    std::uint64_t predicted_commands{0U};
    std::uint64_t authoritative_updates{0U};
    std::uint64_t accepted_acknowledgements{0U};
    std::uint64_t stale_updates{0U};
    std::uint64_t duplicate_updates{0U};
    std::uint64_t conflicts{0U};
    std::uint64_t reconciliation_count{0U};
    std::uint64_t exact_match_count{0U};
    std::uint64_t replay_count{0U};
    std::uint64_t replayed_command_count{0U};
    std::size_t maximum_replay_depth{0U};
    std::uint64_t corrected_commands{0U};
    std::uint64_t small_corrections{0U};
    std::uint64_t large_snaps{0U};
    std::uint64_t teleports{0U};
    std::uint64_t hard_resets{0U};
    std::uint64_t constrained_camera_corrections{0U};
    std::size_t history_high_water_mark{0U};
    std::uint64_t history_backpressure_count{0U};
    std::uint64_t replay_failures{0U};
};

[[nodiscard]] bool valid_local_prediction_history_limits(
    const LocalPredictionHistoryLimits& limits) noexcept;
[[nodiscard]] bool valid_prediction_reconciliation_limits(
    const PredictionReconciliationLimits& limits) noexcept;

[[nodiscard]] std::string_view to_string(
    PredictionCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    PredictionEvidenceProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    PredictionAcknowledgementProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    AuthoritativePlayerDiscontinuity discontinuity) noexcept;
[[nodiscard]] std::string_view to_string(
    PredictionCorrectionClass correction) noexcept;
[[nodiscard]] std::string_view to_string(
    PredictionVisualCorrectionProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(PredictionErrorCode code) noexcept;

} // namespace hlclient::prediction
