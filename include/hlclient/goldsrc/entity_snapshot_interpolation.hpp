#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/entity_visual/entity_visual_projection.hpp>
#include <hlclient/entity_visual/player_presentation_diagnostics.hpp>
#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumInterpolatedEntities = 4'096U;
inline constexpr std::size_t kHardMaximumInterpolatedEntities = 16'384U;
inline constexpr double kDefaultMaximumSnapshotGapSeconds = 1.0;
inline constexpr double kHardMaximumSnapshotGapSeconds = 60.0;
inline constexpr float kDefaultMaximumInterpolatedPositionMagnitude =
    1'048'576.0F;
inline constexpr float kHardMaximumInterpolatedPositionMagnitude =
    16'777'216.0F;
inline constexpr float kDefaultMaximumInterpolatedScale = 1'024.0F;
inline constexpr float kHardMaximumInterpolatedScale = 65'536.0F;
inline constexpr std::size_t kDefaultMaximumInterpolationEvents = 4'096U;
inline constexpr std::size_t kHardMaximumInterpolationEvents = 65'536U;
inline constexpr std::size_t kDefaultMaximumInterpolatedResultBytes =
    8U * 1'024U * 1'024U;
inline constexpr std::size_t kHardMaximumInterpolatedResultBytes =
    64U * 1'024U * 1'024U;

enum class EntityInterpolationTimeDomain {
    synthetic_seconds_v1,
    stock_server_time_evidence_pending,
};

enum class EntityInterpolationEvidenceProfile {
    synthetic_explicit_projection_v1,
    stock_projection_evidence_pending,
};

class EntityInterpolationTime final {
  public:
    EntityInterpolationTime(const EntityInterpolationTime&) = default;
    EntityInterpolationTime(EntityInterpolationTime&&) noexcept = default;
    EntityInterpolationTime& operator=(const EntityInterpolationTime&) = delete;
    EntityInterpolationTime&
    operator=(EntityInterpolationTime&&) noexcept = delete;
    ~EntityInterpolationTime() = default;

    [[nodiscard]] static std::optional<EntityInterpolationTime>
    synthetic_seconds(double seconds) noexcept;
    [[nodiscard]] static EntityInterpolationTime
    stock_evidence_pending() noexcept;

    [[nodiscard]] EntityInterpolationTimeDomain domain() const noexcept;
    [[nodiscard]] std::optional<double> finite_seconds() const noexcept;

  private:
    EntityInterpolationTime(EntityInterpolationTimeDomain domain,
                            std::optional<double> seconds) noexcept;

    EntityInterpolationTimeDomain domain_{
        EntityInterpolationTimeDomain::stock_server_time_evidence_pending};
    std::optional<double> seconds_;
};

// Explicit adapter record. It binds caller-supplied finite seconds to both the
// immutable snapshot identity and its opaque synthetic raw-time value. The
// pair selector never treats EntityServerTime::raw_value() as seconds.
class EntitySnapshotExplicitTime final {
  public:
    EntitySnapshotExplicitTime(const EntitySnapshotExplicitTime&) = default;
    EntitySnapshotExplicitTime(EntitySnapshotExplicitTime&&) noexcept = default;
    EntitySnapshotExplicitTime&
    operator=(const EntitySnapshotExplicitTime&) = delete;
    EntitySnapshotExplicitTime&
    operator=(EntitySnapshotExplicitTime&&) noexcept = delete;
    ~EntitySnapshotExplicitTime() = default;

    [[nodiscard]] static std::optional<EntitySnapshotExplicitTime>
    bind_synthetic_seconds(const EntitySnapshotState& snapshot,
                           double seconds) noexcept;

    [[nodiscard]] std::uint32_t snapshot_reference() const noexcept;
    [[nodiscard]] std::int64_t opaque_raw_server_time() const noexcept;
    [[nodiscard]] double seconds() const noexcept;

  private:
    EntitySnapshotExplicitTime(std::uint32_t snapshot_reference,
                               std::int64_t opaque_raw_server_time,
                               double seconds) noexcept;

    std::uint32_t snapshot_reference_{0U};
    std::int64_t opaque_raw_server_time_{0};
    double seconds_{0.0};
};

struct EntityInterpolationLimits {
    std::size_t maximum_entities{kDefaultMaximumInterpolatedEntities};
    double maximum_snapshot_gap_seconds{kDefaultMaximumSnapshotGapSeconds};
    float maximum_position_magnitude{
        kDefaultMaximumInterpolatedPositionMagnitude};
    float maximum_scale{kDefaultMaximumInterpolatedScale};
    std::size_t maximum_events{kDefaultMaximumInterpolationEvents};
    std::size_t maximum_result_bytes{kDefaultMaximumInterpolatedResultBytes};
};

[[nodiscard]] bool valid_entity_interpolation_limits(
    const EntityInterpolationLimits& limits) noexcept;

enum class EntitySnapshotPairSelectionStatus {
    bracketed,
    exact_previous,
    exact_current,
    held_oldest,
    held_newest,
    held_only,
};

enum class EntityInterpolationErrorCode {
    invalid_configuration,
    evidence_pending,
    empty_history,
    time_adapter_mismatch,
    duplicate_snapshot_time,
    invalid_snapshot_time_order,
    snapshot_gap_limit_exceeded,
    invalid_projection,
    entity_limit_exceeded,
    position_limit_exceeded,
    scale_limit_exceeded,
    event_limit_exceeded,
    result_byte_limit_exceeded,
    non_finite_result,
    unable_to_retain_result,
};

struct EntityInterpolationError {
    EntityInterpolationErrorCode code{
        EntityInterpolationErrorCode::invalid_configuration};
    std::optional<std::uint32_t> snapshot_reference;
    std::optional<std::uint32_t> entity_number;
    std::string context;
};

struct EntitySnapshotPairSelection {
    const EntitySnapshotState* previous{nullptr};
    const EntitySnapshotState* current{nullptr};
    double previous_seconds{0.0};
    double current_seconds{0.0};
    double target_seconds{0.0};
    double alpha{0.0};
    EntitySnapshotPairSelectionStatus status{
        EntitySnapshotPairSelectionStatus::bracketed};
    EntityInterpolationEvidenceProfile evidence_profile{
        EntityInterpolationEvidenceProfile::synthetic_explicit_projection_v1};
};

struct EntitySnapshotPairSelectionResult {
    std::optional<EntitySnapshotPairSelection> selection;
    std::optional<EntityInterpolationError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return selection.has_value();
    }
};

class EntitySnapshotPairSelector final {
  public:
    [[nodiscard]] EntitySnapshotPairSelectionResult
    select(const EntitySnapshotHistoryState& history,
           std::span<const EntitySnapshotExplicitTime> explicit_times,
           const EntityInterpolationTime& target,
           EntityInterpolationTimeDomain profile =
               EntityInterpolationTimeDomain::synthetic_seconds_v1,
           const EntityInterpolationLimits& limits = {}) const;
};

enum class EntityInterpolationMode {
    interpolate,
    step,
    teleport,
};

// Every semantic value is supplied explicitly by the synthetic composition
// boundary. No DeltaObjectState field name or stock projection is inspected.
struct EntityInterpolationDiscreteState {
    // Exact numeric mirror of SyntheticEntityInterpolationState::
    // model_reference. The typed reference is authoritative, including the
    // valid synthetic model-slot value zero.
    std::uint32_t model_reference{0U};
    std::uint32_t sequence_index{0U};
    std::int32_t body_value{0};
    std::uint32_t skin_family_index{0U};
    std::int32_t render_mode{0};
    std::uint32_t sprite_frame_category{0U};
    std::uint32_t effects_flags{0U};

    [[nodiscard]] friend bool
    operator==(const EntityInterpolationDiscreteState&,
               const EntityInterpolationDiscreteState&) = default;
};

struct SyntheticEntityInterpolationState {
    std::uint32_t entity_number{0U};
    // The typed reference is authoritative for every value. In particular,
    // synthetic model slot zero is valid and is never treated as a sentinel.
    // The legacy numeric mirror in `discrete` must agree exactly.
    entity_visual::EntityVisualModelReference model_reference{
        entity_visual::EntityVisualModelReference::synthetic_model_slot(0U)};
    assets::AssetVector3 position{};
    assets::AssetVector3 angles_degrees{};
    assets::AssetVector3 scale{1.0F, 1.0F, 1.0F};
    float studio_frame_coordinate{0.0F};
    std::array<float, 4U> controller_values{};
    std::array<float, 2U> blending_values{};
    float mouth_value{0.0F};
    float render_amount{1.0F};
    entity_visual::EntityVisualRenderColor render_color{};
    std::optional<double> animation_start_time_seconds;
    EntityInterpolationMode mode{EntityInterpolationMode::interpolate};
    EntityInterpolationDiscreteState discrete{};
    std::size_t inert_event_count{0U};
};

struct EntityInterpolationProjectionFrame {
    std::uint32_t snapshot_reference{0U};
    std::span<const SyntheticEntityInterpolationState> entities;
};

// Owning renderer-neutral projection input for EntitySnapshotInterpolator.
// It retains no snapshot object, protocol bytes, filesystem state or native
// paths. `view()` is valid for the lifetime of this immutable state.
class EntityInterpolationProjectionFrameState final {
  public:
    EntityInterpolationProjectionFrameState(
        const EntityInterpolationProjectionFrameState&) = default;
    EntityInterpolationProjectionFrameState(
        EntityInterpolationProjectionFrameState&&) noexcept = default;
    EntityInterpolationProjectionFrameState& operator=(
        const EntityInterpolationProjectionFrameState&) = delete;
    EntityInterpolationProjectionFrameState& operator=(
        EntityInterpolationProjectionFrameState&&) noexcept = delete;
    ~EntityInterpolationProjectionFrameState() = default;

    [[nodiscard]] std::uint32_t snapshot_reference() const noexcept;
    [[nodiscard]] std::span<const SyntheticEntityInterpolationState>
    entities() const noexcept;
    [[nodiscard]] EntityInterpolationProjectionFrame view() const noexcept;

  private:
    friend class EntityInterpolationProjectionAdapter;

    EntityInterpolationProjectionFrameState(
        std::uint32_t snapshot_reference,
        std::vector<SyntheticEntityInterpolationState> entities) noexcept;

    std::uint32_t snapshot_reference_{0U};
    std::vector<SyntheticEntityInterpolationState> entities_;
};

struct EntityInterpolationProjectionAdapterResult {
    std::optional<EntityInterpolationProjectionFrameState> frame;
    std::optional<EntityInterpolationError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frame.has_value();
    }
};

// The only production conversion from canonical visual projections to the
// interpolation input. It verifies exact snapshot ownership/order and copies
// every pose, Sprite and render control without inspecting wire field names.
class EntityInterpolationProjectionAdapter final {
  public:
    [[nodiscard]] EntityInterpolationProjectionAdapterResult
    build(const EntitySnapshotState& snapshot,
          std::span<const entity_visual::EntityVisualProjectionState>
              projections,
          const EntityInterpolationLimits& limits = {}) const noexcept;
};

enum class InterpolatedEntityClass {
    held,
    interpolated,
    stepped,
    previous_only,
    current_only,
};

class InterpolatedEntityState final {
  public:
    InterpolatedEntityState(const InterpolatedEntityState&) = default;
    InterpolatedEntityState(InterpolatedEntityState&&) noexcept = default;
    InterpolatedEntityState& operator=(const InterpolatedEntityState&) = delete;
    InterpolatedEntityState&
    operator=(InterpolatedEntityState&&) noexcept = delete;
    ~InterpolatedEntityState() = default;

    [[nodiscard]] std::uint32_t entity_number() const noexcept;
    [[nodiscard]] const assets::AssetVector3& position() const noexcept;
    [[nodiscard]] const assets::AssetVector3& angles_degrees() const noexcept;
    [[nodiscard]] const assets::AssetVector3& scale() const noexcept;
    [[nodiscard]] float studio_frame_coordinate() const noexcept;
    [[nodiscard]] const entity_visual::EntityVisualModelReference&
    model_reference() const noexcept;
    [[nodiscard]] const std::array<float, 4U>&
    controller_values() const noexcept;
    [[nodiscard]] const std::array<float, 2U>&
    blending_values() const noexcept;
    [[nodiscard]] float mouth_value() const noexcept;
    [[nodiscard]] std::uint32_t sprite_frame_index() const noexcept;
    [[nodiscard]] entity_visual::EntityVisualRenderMode
    render_mode() const noexcept;
    [[nodiscard]] float render_amount() const noexcept;
    [[nodiscard]] const entity_visual::EntityVisualRenderColor&
    render_color() const noexcept;
    [[nodiscard]] const std::optional<double>&
    animation_start_time_seconds() const noexcept;
    [[nodiscard]] std::uint32_t effects_metadata() const noexcept;
    [[nodiscard]] EntityInterpolationMode mode() const noexcept;
    [[nodiscard]] InterpolatedEntityClass interpolation_class() const noexcept;
    [[nodiscard]] const EntityInterpolationDiscreteState&
    discrete() const noexcept;
    [[nodiscard]] std::size_t inert_event_count() const noexcept;

  private:
    friend class EntitySnapshotInterpolator;

    InterpolatedEntityState(
        SyntheticEntityInterpolationState state,
        InterpolatedEntityClass interpolation_class) noexcept;

    SyntheticEntityInterpolationState state_;
    InterpolatedEntityClass interpolation_class_{InterpolatedEntityClass::held};
};

struct EntityInterpolationStatistics {
    std::size_t entity_count{0U};
    std::size_t interpolated_count{0U};
    std::size_t stepped_count{0U};
    std::size_t added_count{0U};
    std::size_t removed_count{0U};
    std::size_t held_count{0U};
    std::size_t inert_event_count{0U};
    std::size_t accounted_result_bytes{0U};
};

class InterpolatedEntityFrame final {
  public:
    InterpolatedEntityFrame(const InterpolatedEntityFrame&) = default;
    InterpolatedEntityFrame(InterpolatedEntityFrame&&) noexcept = default;
    InterpolatedEntityFrame& operator=(const InterpolatedEntityFrame&) = delete;
    InterpolatedEntityFrame&
    operator=(InterpolatedEntityFrame&&) noexcept = delete;
    ~InterpolatedEntityFrame() = default;

    [[nodiscard]] double sample_seconds() const noexcept;
    [[nodiscard]] double previous_seconds() const noexcept;
    [[nodiscard]] double current_seconds() const noexcept;
    [[nodiscard]] std::uint32_t previous_snapshot_reference() const noexcept;
    [[nodiscard]] std::uint32_t current_snapshot_reference() const noexcept;
    [[nodiscard]] double alpha() const noexcept;
    [[nodiscard]] EntitySnapshotPairSelectionStatus
    selection_status() const noexcept;
    [[nodiscard]] std::span<const InterpolatedEntityState>
    entities() const noexcept;
    [[nodiscard]] const EntityInterpolationStatistics&
    statistics() const noexcept;
    [[nodiscard]] EntityInterpolationEvidenceProfile
    evidence_profile() const noexcept;

  private:
    friend class EntitySnapshotInterpolator;

    InterpolatedEntityFrame(
        double sample_seconds, double previous_seconds, double current_seconds,
        std::uint32_t previous_snapshot_reference,
        std::uint32_t current_snapshot_reference, double alpha,
        EntitySnapshotPairSelectionStatus selection_status,
        std::vector<InterpolatedEntityState> entities,
        EntityInterpolationStatistics statistics,
        EntityInterpolationEvidenceProfile evidence_profile) noexcept;

    double sample_seconds_{0.0};
    double previous_seconds_{0.0};
    double current_seconds_{0.0};
    std::uint32_t previous_snapshot_reference_{0U};
    std::uint32_t current_snapshot_reference_{0U};
    double alpha_{0.0};
    EntitySnapshotPairSelectionStatus selection_status_{
        EntitySnapshotPairSelectionStatus::bracketed};
    std::vector<InterpolatedEntityState> entities_;
    EntityInterpolationStatistics statistics_;
    EntityInterpolationEvidenceProfile evidence_profile_{
        EntityInterpolationEvidenceProfile::synthetic_explicit_projection_v1};
};

struct InterpolatedEntityFrameResult {
    std::optional<InterpolatedEntityFrame> frame;
    std::optional<EntityInterpolationError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frame.has_value();
    }
};

struct RuntimePlayerAnimationEffectException final {
    std::uint32_t entity{};
    std::size_t record_ordinal{};
    std::uint32_t ignored_effect_mask{};
};
struct RuntimeEntityInterpolationOptions {
    EntityInterpolationLimits limits{};
    bool no_interpolation{false};
    std::uint32_t no_interpolation_effect_mask{};
    std::span<const std::uint32_t> discontinuous_entities;
    std::optional<double> teleport_distance;
    // Sorted unique non-player IDs selected by the asset owner. Empty retains
    // the established player-only behavior; brush owners never enter this list.
    std::span<const std::uint32_t> ordinary_entities;
    // Explicit kinetic presentation continuity, independent of player policy.
    // Absent retains the caller's existing teleport_distance behavior. This
    // only selects observation brackets; it never integrates/extrapolates.
    std::optional<double> ordinary_teleport_distance;
    // Game-selected, exact committed-source qualifications. Sorted unique by
    // (entity, ordinal), at most16 histories x32 players. Defaults stay strict.
    std::span<const RuntimePlayerAnimationEffectException> animation_only_effect_exceptions;
};
[[nodiscard]] std::uint32_t runtime_animation_only_effect_mask(
    const RuntimeEntityInterpolationOptions&,std::uint32_t entity,std::size_t record_ordinal) noexcept;
struct RuntimeEntityInterpolationResult {
    std::shared_ptr<const client::RuntimeClientObservationState> state;
    double sample_seconds{}, alpha{};
    EntitySnapshotPairSelectionStatus selection_status{EntitySnapshotPairSelectionStatus::held_only};
    std::size_t interpolated_count{}, reset_count{};
    std::optional<EntityInterpolationError> error;
    [[nodiscard]] explicit operator bool() const noexcept { return state && !error; }
};

class EntitySnapshotInterpolator final {
  public:
    [[nodiscard]] InterpolatedEntityFrameResult
    interpolate(const EntitySnapshotPairSelection& selection,
                const EntityInterpolationProjectionFrame& previous_projection,
                const EntityInterpolationProjectionFrame& current_projection,
                const EntityInterpolationLimits& limits = {}) const;
    // Presentation-only owning sample; never publish back to RX/game/audio.
    [[nodiscard]] RuntimeEntityInterpolationResult interpolate_runtime(
        const client::RuntimeClientObservationState& previous,
        const client::RuntimeClientObservationState& current,
        double target_server_seconds,
        const RuntimeEntityInterpolationOptions& options = {}) const;
};

// Render-only ordinary-entity jitter buffer. The caller retains current-list
// authority and chooses ready kinetic Studio / server glow-sprite identities;
// no velocity, gravity, collision or game rule is executed here. Observations
// are immutable owning references, limited to sixteen. A bounded recent window
// adapts the presentation reserve to authoritative cadence and inferred
// presentation-arrival jitter (never a claimed wire-receipt timestamp); it is
// never an extrapolation or a local projectile simulation.
// Per-lifetime retained values are visual samples only, bounded by
// maximum_entities; they never enter RX or replay.
class RuntimeOrdinarySnapshotPresentation final {
  public:
    static constexpr std::size_t maximum_history = 16U;
    static constexpr double maximum_delay_seconds = 0.1;
    static constexpr double default_continuity_distance_units = 512.0;
    void reset() noexcept;
    void observe(std::shared_ptr<const client::RuntimeClientObservationState>);
    [[nodiscard]] RuntimeEntityInterpolationResult sample(
        const client::RuntimeClientObservationState& current_presentation,
        double application_seconds, const RuntimeEntityInterpolationOptions&,
        double maximum_selected_delay_seconds);
    [[nodiscard]] std::size_t history_size() const noexcept { return history_.size(); }
    [[nodiscard]] std::size_t lifetime_count() const noexcept { return lifetimes_.size(); }
    [[nodiscard]] std::optional<double> cadence_delay_seconds() const noexcept { return cadence_delay_; }
  private:
    struct Lifetime {
        std::size_t record_ordinal{};
        double committed_seconds{}, target_seconds{};
        double birth_server_seconds{}, birth_application_seconds{};
        client::RuntimePacketEntityObservation committed, rendered;
    };
    std::vector<std::shared_ptr<const client::RuntimeClientObservationState>> history_;
    std::map<std::uint32_t,Lifetime> lifetimes_;
    std::array<double,maximum_history> arrival_intervals_{};
    std::size_t arrival_interval_count_{}, arrival_interval_next_{};
    std::optional<std::size_t> arrival_record_ordinal_;
    std::optional<double> arrival_application_seconds_, arrival_server_seconds_;
    double maximum_render_gap_since_arrival_{};
    std::optional<double> application_anchor_, server_anchor_, last_application_, cadence_delay_;
};

// An owning committed bracket for one rendered player. The game layer borrows
// these exact anchors only during pose composition; no RX/gameplay publication.
struct RuntimePlayerInterpolationAnchor final {
    std::uint32_t entity{};
    std::shared_ptr<const client::RuntimeClientObservationState> previous, current;
    double sample_seconds{};
    bool discontinuity{};
    entity_visual::PlayerPresentationDiagnostics diagnostics{};
};
struct RuntimePlayerContinuityBoundary final {
    std::uint32_t entity{};
    std::size_t record_ordinal{};
};
struct RuntimePlayerSnapshotPresentationResult final {
    RuntimeEntityInterpolationResult presentation;
    std::shared_ptr<const client::RuntimeClientObservationState> previous, current;
    std::vector<RuntimePlayerInterpolationAnchor> players;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(presentation); }
};

// The same runtime interpolator and immutable RX observations, with a stable
// player presentation clock. Ordinary projectiles keep their distinct verified
// birth/cadence profile above. Both owners share references, never asset data.
class RuntimePlayerSnapshotPresentation final {
  public:
    static constexpr std::size_t maximum_history=16U, maximum_players=32U;
    static constexpr double maximum_delay_seconds=.1;
    void reset() noexcept;
    void observe(std::shared_ptr<const client::RuntimeClientObservationState>);
    [[nodiscard]] std::span<const std::shared_ptr<const client::RuntimeClientObservationState>>
        observations() const noexcept { return history_; }
    [[nodiscard]] RuntimePlayerSnapshotPresentationResult sample(
        const client::RuntimeClientObservationState& latest, double application_seconds,
        const RuntimeEntityInterpolationOptions&, double selected_delay_seconds,
        std::span<const std::uint32_t> players,
        std::span<const RuntimePlayerContinuityBoundary> boundaries={});
    [[nodiscard]] std::size_t history_size() const noexcept { return history_.size(); }
    [[nodiscard]] std::size_t player_count() const noexcept { return players_.size(); }
    // Actual global buffer reserve after bounded startup fill. This is not an
    // actor's held age, packet latency, or latest-source-minus-sample distance.
    [[nodiscard]] std::optional<double> effective_delay_seconds() const noexcept { return delay_; }
  private:
    struct PlayerContinuity {
        std::size_t floor{}, seen{}, presented{};
        std::uint32_t floor_reasons{};
    };
    std::vector<std::shared_ptr<const client::RuntimeClientObservationState>> history_;
    std::map<std::uint32_t,PlayerContinuity> players_;
    std::optional<double> application_anchor_, server_anchor_, last_application_, last_target_, delay_;
    std::uint32_t pending_clock_reasons_{};
};

[[nodiscard]] constexpr std::string_view
to_string(EntityInterpolationErrorCode code) noexcept
{
    switch (code) {
    case EntityInterpolationErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntityInterpolationErrorCode::evidence_pending:
        return "evidence_pending";
    case EntityInterpolationErrorCode::empty_history:
        return "empty_history";
    case EntityInterpolationErrorCode::time_adapter_mismatch:
        return "time_adapter_mismatch";
    case EntityInterpolationErrorCode::duplicate_snapshot_time:
        return "duplicate_snapshot_time";
    case EntityInterpolationErrorCode::invalid_snapshot_time_order:
        return "invalid_snapshot_time_order";
    case EntityInterpolationErrorCode::snapshot_gap_limit_exceeded:
        return "snapshot_gap_limit_exceeded";
    case EntityInterpolationErrorCode::invalid_projection:
        return "invalid_projection";
    case EntityInterpolationErrorCode::entity_limit_exceeded:
        return "entity_limit_exceeded";
    case EntityInterpolationErrorCode::position_limit_exceeded:
        return "position_limit_exceeded";
    case EntityInterpolationErrorCode::scale_limit_exceeded:
        return "scale_limit_exceeded";
    case EntityInterpolationErrorCode::event_limit_exceeded:
        return "event_limit_exceeded";
    case EntityInterpolationErrorCode::result_byte_limit_exceeded:
        return "result_byte_limit_exceeded";
    case EntityInterpolationErrorCode::non_finite_result:
        return "non_finite_result";
    case EntityInterpolationErrorCode::unable_to_retain_result:
        return "unable_to_retain_result";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
