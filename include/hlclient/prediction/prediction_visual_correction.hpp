#pragma once

#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/prediction/prediction_types.hpp>

#include <cstdint>
#include <optional>

namespace hlclient::prediction {

struct PredictionVisualCorrectionCreateResult;
struct PredictionVisualCorrectionSampleResult;

struct PredictionVisualCorrectionConfig {
    double duration_seconds{0.100};
    double maximum_duration_seconds{1.000};
    double maximum_offset_magnitude{16.0};
    PredictionVisualCorrectionProfile profile{
        PredictionVisualCorrectionProfile::
            project_linear_decay_collision_constrained_v1};
};

class PredictionVisualCorrectionState final {
public:
    PredictionVisualCorrectionState(const PredictionVisualCorrectionState&) =
        default;
    PredictionVisualCorrectionState(
        PredictionVisualCorrectionState&&) noexcept = default;
    PredictionVisualCorrectionState& operator=(
        const PredictionVisualCorrectionState&) = delete;
    PredictionVisualCorrectionState& operator=(
        PredictionVisualCorrectionState&&) = delete;
    ~PredictionVisualCorrectionState() = default;

    [[nodiscard]] static PredictionVisualCorrectionState inactive() noexcept;

    [[nodiscard]] PredictionCorrectionClass correction_class() const noexcept;
    [[nodiscard]] const assets::AssetVector3& initial_position_offset()
        const noexcept;
    [[nodiscard]] const assets::AssetVector3& current_residual_offset()
        const noexcept;
    [[nodiscard]] double start_monotonic_time_seconds() const noexcept;
    [[nodiscard]] double last_sample_monotonic_time_seconds() const noexcept;
    [[nodiscard]] double duration_seconds() const noexcept;
    [[nodiscard]] std::uint64_t source_authority_update_ordinal()
        const noexcept;
    [[nodiscard]] std::uint64_t old_prediction_revision() const noexcept;
    [[nodiscard]] std::uint64_t new_prediction_revision() const noexcept;
    [[nodiscard]] std::uint64_t camera_publication_revision() const noexcept;
    [[nodiscard]] const std::optional<
        gameplay_camera::GameplayCameraState>& last_published_camera()
        const noexcept;
    [[nodiscard]] bool collision_constrained() const noexcept;
    [[nodiscard]] PredictionVisualCorrectionProfile profile() const noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    friend struct PredictionVisualCorrectionCreateResult;
    friend struct PredictionVisualCorrectionSampleResult;
    friend PredictionVisualCorrectionCreateResult
    begin_prediction_visual_correction(
        const std::optional<PredictionVisualCorrectionState>&,
        const assets::AssetVector3&,
        const assets::AssetVector3&,
        PredictionCorrectionClass,
        double,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        const PredictionVisualCorrectionConfig&) noexcept;
    friend PredictionVisualCorrectionSampleResult
    sample_prediction_visual_correction(
        const PredictionVisualCorrectionState&,
        const gameplay_camera::GameplayCameraState&,
        double,
        const collision::CollisionWorldQuery*,
        collision::CollisionQueryScratch&,
        const collision::CollisionQueryLimits&,
        std::uint64_t);
    friend PredictionVisualCorrectionSampleResult
    publish_validated_prediction_visual_camera(
        const PredictionVisualCorrectionState&,
        const gameplay_camera::GameplayCameraState&,
        assets::AssetVector3,double,std::uint64_t,std::uint64_t,double) noexcept;

    PredictionVisualCorrectionState(
        PredictionCorrectionClass correction_class,
        assets::AssetVector3 initial_position_offset,
        assets::AssetVector3 current_residual_offset,
        double start_monotonic_time_seconds,
        double last_sample_monotonic_time_seconds,
        double duration_seconds,
        std::uint64_t source_authority_update_ordinal,
        std::uint64_t old_prediction_revision,
        std::uint64_t new_prediction_revision,
        std::uint64_t camera_publication_revision,
        std::optional<gameplay_camera::GameplayCameraState>
            last_published_camera,
        bool collision_constrained,
        PredictionVisualCorrectionProfile profile) noexcept;

    PredictionCorrectionClass correction_class_{
        PredictionCorrectionClass::exact};
    assets::AssetVector3 initial_position_offset_{};
    assets::AssetVector3 current_residual_offset_{};
    double start_monotonic_time_seconds_{0.0};
    double last_sample_monotonic_time_seconds_{0.0};
    double duration_seconds_{0.0};
    std::uint64_t source_authority_update_ordinal_{0U};
    std::uint64_t old_prediction_revision_{0U};
    std::uint64_t new_prediction_revision_{0U};
    std::uint64_t camera_publication_revision_{0U};
    std::optional<gameplay_camera::GameplayCameraState> last_published_camera_;
    bool collision_constrained_{false};
    PredictionVisualCorrectionProfile profile_{
        PredictionVisualCorrectionProfile::no_smoothing_snap_v1};
};

struct PredictionVisualCorrectionCreateResult {
    std::optional<PredictionVisualCorrectionState> correction;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return correction.has_value() && !error.has_value();
    }
};

struct PredictionVisualCorrectionSampleResult {
    std::optional<gameplay_camera::GameplayCameraState> camera;
    std::optional<PredictionVisualCorrectionState> correction;
    std::optional<PredictionError> error;
    bool completed{false};
    bool constrained{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return camera.has_value() && correction.has_value() &&
            !error.has_value();
    }
};

[[nodiscard]] bool valid_prediction_visual_correction_config(
    const PredictionVisualCorrectionConfig& config) noexcept;

[[nodiscard]] PredictionVisualCorrectionCreateResult
begin_prediction_visual_correction(
    const std::optional<PredictionVisualCorrectionState>& previous,
    const assets::AssetVector3& old_physical_eye,
    const assets::AssetVector3& corrected_eye,
    PredictionCorrectionClass correction_class,
    double start_monotonic_time_seconds,
    std::uint64_t source_authority_update_ordinal,
    std::uint64_t old_prediction_revision,
    std::uint64_t new_prediction_revision,
    const PredictionVisualCorrectionConfig& config = {}) noexcept;

[[nodiscard]] PredictionVisualCorrectionSampleResult
sample_prediction_visual_correction(
    const PredictionVisualCorrectionState& correction,
    const gameplay_camera::GameplayCameraState& corrected_camera,
    double sample_monotonic_time_seconds,
    const collision::CollisionWorldQuery* collision_query,
    collision::CollisionQueryScratch& scratch,
    const collision::CollisionQueryLimits& query_limits = {},
    std::uint64_t maximum_camera_revision = UINT64_MAX);

// The owning presentation layer must already have validated its complete body
// and eye path. This commits only that camera/residual within the existing
// correction's identity and deadline; it cannot start or extend a correction.
[[nodiscard]] PredictionVisualCorrectionSampleResult
publish_validated_prediction_visual_camera(
    const PredictionVisualCorrectionState& correction,
    const gameplay_camera::GameplayCameraState& corrected_camera,
    assets::AssetVector3 validated_eye,double sample_monotonic_time_seconds,
    std::uint64_t expected_prediction_revision,
    std::uint64_t expected_authority_update_ordinal,
    double maximum_offset_magnitude = 4.0) noexcept;

} // namespace hlclient::prediction
