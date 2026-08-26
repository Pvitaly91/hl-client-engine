#pragma once

#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::entity_visual {

inline constexpr std::uint32_t kDefaultMaximumSyntheticVisualEntityNumber =
    65'535U;
inline constexpr std::uint32_t kHardMaximumSyntheticVisualEntityNumber =
    goldsrc::kMaximumEntityNumber;
inline constexpr std::uint32_t kDefaultMaximumSyntheticModelSlot = 4'095U;
inline constexpr std::uint32_t kHardMaximumSyntheticModelSlot = 65'535U;
inline constexpr std::uint32_t kDefaultMaximumSyntheticSequenceIndex =
    2'047U;
inline constexpr std::uint32_t kHardMaximumSyntheticSequenceIndex = 65'535U;
inline constexpr std::uint32_t kDefaultMaximumSyntheticBodyValue =
    1'048'575U;
inline constexpr std::uint32_t kHardMaximumSyntheticBodyValue =
    16'777'215U;
inline constexpr std::uint32_t kDefaultMaximumSyntheticSkinFamilyIndex =
    4'095U;
inline constexpr std::uint32_t kHardMaximumSyntheticSkinFamilyIndex =
    65'535U;
inline constexpr std::uint32_t kDefaultMaximumSyntheticSpriteFrameIndex =
    16'383U;
inline constexpr std::uint32_t kHardMaximumSyntheticSpriteFrameIndex =
    65'535U;
inline constexpr float kDefaultMaximumSyntheticPositionMagnitude =
    1'048'576.0F;
inline constexpr float kHardMaximumSyntheticPositionMagnitude =
    16'777'216.0F;
inline constexpr float kDefaultMaximumSyntheticAngleMagnitude =
    36'000.0F;
inline constexpr float kHardMaximumSyntheticAngleMagnitude = 1'000'000.0F;
inline constexpr float kDefaultMaximumSyntheticStudioFrameCoordinate =
    1'048'576.0F;
inline constexpr float kHardMaximumSyntheticStudioFrameCoordinate =
    16'777'216.0F;
inline constexpr float kDefaultMaximumSyntheticEntityScale = 1'024.0F;
inline constexpr float kHardMaximumSyntheticEntityScale = 65'536.0F;
inline constexpr double kDefaultMaximumSyntheticAnimationTimeSeconds =
    86'400.0;
inline constexpr double kHardMaximumSyntheticAnimationTimeSeconds =
    31'536'000.0;
inline constexpr std::size_t kDefaultMaximumSyntheticVisualInputs = 4'096U;
inline constexpr std::size_t kHardMaximumSyntheticVisualInputs = 16'384U;
inline constexpr std::size_t kEntityVisualProjectionDiagnosticTextLimit =
    256U;

enum class EntityVisualProjectionCompatibilityProfile {
    synthetic_entity_visual_v1,
    stock_protocol_48_evidence_pending,
};

enum class EntityVisualProjectionEvidenceProfile {
    caller_supplied_typed_synthetic_records,
    stock_runtime_field_semantics_pending,
};

enum class EntityVisualModelReferenceProfile {
    synthetic_type_local_model_slot,
    stock_modelindex_mapping_evidence_pending,
};

class EntityVisualModelReference final {
public:
    [[nodiscard]] static EntityVisualModelReference synthetic_model_slot(
        std::uint32_t slot) noexcept;
    [[nodiscard]] static EntityVisualModelReference
    stock_modelindex_evidence_pending(std::uint32_t raw_value) noexcept;

    [[nodiscard]] EntityVisualModelReferenceProfile profile() const noexcept;
    [[nodiscard]] std::uint32_t value() const noexcept;

    [[nodiscard]] friend bool operator==(
        const EntityVisualModelReference&,
        const EntityVisualModelReference&) noexcept = default;

private:
    EntityVisualModelReference(
        EntityVisualModelReferenceProfile profile,
        std::uint32_t value) noexcept;

    EntityVisualModelReferenceProfile profile_{
        EntityVisualModelReferenceProfile::synthetic_type_local_model_slot};
    std::uint32_t value_{0U};
};

struct EntityVisualVector3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};

    [[nodiscard]] friend bool operator==(
        const EntityVisualVector3&,
        const EntityVisualVector3&) noexcept = default;
};

struct EntityVisualRenderColor {
    float red{1.0F};
    float green{1.0F};
    float blue{1.0F};

    [[nodiscard]] friend bool operator==(
        const EntityVisualRenderColor&,
        const EntityVisualRenderColor&) noexcept = default;
};

enum class EntityVisualRenderMode {
    source_asset_default,
    opaque,
    alpha_test,
    additive,
};

enum class EntityInterpolationMode {
    interpolate,
    step,
    teleport,
};

struct EntityVisualProjectionLimits {
    std::uint32_t maximum_entity_number{
        kDefaultMaximumSyntheticVisualEntityNumber};
    std::uint32_t maximum_model_slot{kDefaultMaximumSyntheticModelSlot};
    std::uint32_t maximum_sequence_index{
        kDefaultMaximumSyntheticSequenceIndex};
    std::uint32_t maximum_body_value{kDefaultMaximumSyntheticBodyValue};
    std::uint32_t maximum_skin_family_index{
        kDefaultMaximumSyntheticSkinFamilyIndex};
    std::uint32_t maximum_sprite_frame_index{
        kDefaultMaximumSyntheticSpriteFrameIndex};
    float maximum_position_magnitude{
        kDefaultMaximumSyntheticPositionMagnitude};
    float maximum_angle_magnitude{kDefaultMaximumSyntheticAngleMagnitude};
    float maximum_studio_frame_coordinate{
        kDefaultMaximumSyntheticStudioFrameCoordinate};
    float maximum_scale{kDefaultMaximumSyntheticEntityScale};
    double maximum_animation_time_seconds{
        kDefaultMaximumSyntheticAnimationTimeSeconds};
    std::size_t maximum_inputs{kDefaultMaximumSyntheticVisualInputs};
};

[[nodiscard]] bool valid_entity_visual_projection_limits(
    const EntityVisualProjectionLimits& limits) noexcept;

// Explicit caller-owned synthetic evidence. No member is populated from a
// DeltaObjectState name or value. Optional controls use deterministic neutral
// defaults when the projection is published.
struct SyntheticEntityVisualInput {
    std::uint32_t entity_number{0U};
    EntityVisualModelReference model_reference{
        EntityVisualModelReference::synthetic_model_slot(0U)};
    std::optional<EntityVisualVector3> origin;
    std::optional<EntityVisualVector3> angles_degrees;
    std::optional<std::uint32_t> sequence_index;
    std::optional<float> studio_frame_coordinate;
    std::optional<std::uint32_t> body_value;
    std::optional<std::uint32_t> skin_family_index;
    std::optional<std::array<float, 4U>> controller_values;
    std::optional<std::array<float, 2U>> blending_values;
    std::optional<float> mouth_value;
    std::optional<std::uint32_t> sprite_frame_index;
    std::optional<EntityVisualRenderMode> render_mode;
    std::optional<float> render_amount;
    std::optional<EntityVisualRenderColor> render_color;
    std::optional<float> scale;
    std::optional<EntityInterpolationMode> interpolation_mode;
    std::optional<double> animation_start_time_seconds;
    std::optional<std::uint32_t> effects_metadata;
};

struct EntityVisualTransform {
    EntityVisualVector3 origin{};
    EntityVisualVector3 angles_degrees{};
    float scale{1.0F};
};

struct EntityVisualStudioControls {
    std::uint32_t sequence_index{0U};
    float frame_coordinate{0.0F};
    std::uint32_t body_value{0U};
    std::uint32_t skin_family_index{0U};
    std::array<float, 4U> controller_values{};
    std::array<float, 2U> blending_values{};
    float mouth_value{0.0F};
};

struct EntityVisualSpriteControls {
    std::uint32_t frame_index{0U};
};

struct EntityVisualRenderControls {
    EntityVisualRenderMode mode{EntityVisualRenderMode::source_asset_default};
    float amount{1.0F};
    EntityVisualRenderColor color{};
    std::uint32_t effects_metadata{0U};
};

class EntityVisualProjectionState final {
public:
    EntityVisualProjectionState(const EntityVisualProjectionState&) = default;
    EntityVisualProjectionState(EntityVisualProjectionState&&) noexcept =
        default;
    EntityVisualProjectionState& operator=(
        const EntityVisualProjectionState&) = delete;
    EntityVisualProjectionState& operator=(
        EntityVisualProjectionState&&) noexcept = delete;
    ~EntityVisualProjectionState() = default;

    [[nodiscard]] std::uint32_t entity_number() const noexcept;
    [[nodiscard]] const EntityVisualModelReference& model_reference()
        const noexcept;
    [[nodiscard]] const EntityVisualTransform& transform() const noexcept;
    [[nodiscard]] const EntityVisualStudioControls& studio_controls()
        const noexcept;
    [[nodiscard]] const EntityVisualSpriteControls& sprite_controls()
        const noexcept;
    [[nodiscard]] const EntityVisualRenderControls& render_controls()
        const noexcept;
    [[nodiscard]] EntityInterpolationMode interpolation_mode() const noexcept;
    [[nodiscard]] const std::optional<double>& animation_start_time_seconds()
        const noexcept;
    [[nodiscard]] const goldsrc::EntitySnapshotReference&
    source_snapshot_reference() const noexcept;
    [[nodiscard]] EntityVisualProjectionCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] EntityVisualProjectionEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class SyntheticEntityVisualProjectionProvider;

    EntityVisualProjectionState(
        std::uint32_t entity_number,
        EntityVisualModelReference model_reference,
        EntityVisualTransform transform,
        EntityVisualStudioControls studio_controls,
        EntityVisualSpriteControls sprite_controls,
        EntityVisualRenderControls render_controls,
        EntityInterpolationMode interpolation_mode,
        std::optional<double> animation_start_time_seconds,
        goldsrc::EntitySnapshotReference source_snapshot_reference) noexcept;

    std::uint32_t entity_number_{0U};
    EntityVisualModelReference model_reference_;
    EntityVisualTransform transform_{};
    EntityVisualStudioControls studio_controls_{};
    EntityVisualSpriteControls sprite_controls_{};
    EntityVisualRenderControls render_controls_{};
    EntityInterpolationMode interpolation_mode_{
        EntityInterpolationMode::interpolate};
    std::optional<double> animation_start_time_seconds_;
    goldsrc::EntitySnapshotReference source_snapshot_reference_;
};

enum class EntityVisualProjectionStatus {
    projected,
    missing_projection,
    visual_projection_evidence_pending,
    invalid_configuration,
    incompatible_snapshot_profile,
    entity_snapshot_mismatch,
    duplicate_entity_input,
    invalid_entity_number,
    invalid_model_reference,
    non_finite_value,
    value_out_of_range,
    unable_to_retain_projection,
};

struct EntityVisualProjectionResult {
    EntityVisualProjectionStatus status{
        EntityVisualProjectionStatus::invalid_configuration};
    std::optional<EntityVisualProjectionState> state;
    std::optional<std::uint32_t> entity_number;
    std::string context;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return status == EntityVisualProjectionStatus::projected &&
               state.has_value();
    }
};

class IEntityVisualProjectionProvider {
public:
    virtual ~IEntityVisualProjectionProvider() = default;

    [[nodiscard]] virtual EntityVisualProjectionResult project(
        const goldsrc::EntitySnapshotState& snapshot,
        const goldsrc::EntitySnapshotEntityState& entity) const = 0;
};

class SyntheticEntityVisualProjectionProvider;

struct SyntheticEntityVisualProjectionProviderCreateResult {
    std::unique_ptr<SyntheticEntityVisualProjectionProvider> provider;
    std::optional<EntityVisualProjectionStatus> error_status;
    std::optional<std::uint32_t> entity_number;
    std::string context;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return provider != nullptr;
    }
};

class SyntheticEntityVisualProjectionProvider final
    : public IEntityVisualProjectionProvider {
public:
    [[nodiscard]] static SyntheticEntityVisualProjectionProviderCreateResult
    create(
        std::vector<SyntheticEntityVisualInput> inputs,
        EntityVisualProjectionLimits limits = {}) noexcept;

    [[nodiscard]] EntityVisualProjectionResult project(
        const goldsrc::EntitySnapshotState& snapshot,
        const goldsrc::EntitySnapshotEntityState& entity) const override;

    [[nodiscard]] std::span<const SyntheticEntityVisualInput> inputs()
        const noexcept;
    [[nodiscard]] const EntityVisualProjectionLimits& limits() const noexcept;

private:
    SyntheticEntityVisualProjectionProvider(
        std::vector<SyntheticEntityVisualInput> inputs,
        EntityVisualProjectionLimits limits) noexcept;

    std::vector<SyntheticEntityVisualInput> inputs_;
    EntityVisualProjectionLimits limits_;
};

class EvidencePendingStockEntityVisualProjectionProvider final
    : public IEntityVisualProjectionProvider {
public:
    [[nodiscard]] EntityVisualProjectionResult project(
        const goldsrc::EntitySnapshotState& snapshot,
        const goldsrc::EntitySnapshotEntityState& entity) const override;
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityVisualProjectionStatus status) noexcept
{
    switch (status) {
    case EntityVisualProjectionStatus::projected: return "projected";
    case EntityVisualProjectionStatus::missing_projection:
        return "missing_projection";
    case EntityVisualProjectionStatus::visual_projection_evidence_pending:
        return "visual_projection_evidence_pending";
    case EntityVisualProjectionStatus::invalid_configuration:
        return "invalid_configuration";
    case EntityVisualProjectionStatus::incompatible_snapshot_profile:
        return "incompatible_snapshot_profile";
    case EntityVisualProjectionStatus::entity_snapshot_mismatch:
        return "entity_snapshot_mismatch";
    case EntityVisualProjectionStatus::duplicate_entity_input:
        return "duplicate_entity_input";
    case EntityVisualProjectionStatus::invalid_entity_number:
        return "invalid_entity_number";
    case EntityVisualProjectionStatus::invalid_model_reference:
        return "invalid_model_reference";
    case EntityVisualProjectionStatus::non_finite_value:
        return "non_finite_value";
    case EntityVisualProjectionStatus::value_out_of_range:
        return "value_out_of_range";
    case EntityVisualProjectionStatus::unable_to_retain_projection:
        return "unable_to_retain_projection";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual
