#include <hlclient/prediction/prediction_visual_correction.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace hlclient::prediction {
namespace {

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] double magnitude(const assets::AssetVector3& value) noexcept
{
    return std::sqrt(static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z);
}

[[nodiscard]] assets::AssetVector3 residual_at(
    const PredictionVisualCorrectionState& correction,
    const double sample_time) noexcept
{
    if (!correction.active() || correction.duration_seconds() <= 0.0) {
        return {};
    }
    const auto end_time = correction.start_monotonic_time_seconds() +
        correction.duration_seconds();
    const auto remaining_at_last = (std::max)(0.0,
        end_time - correction.last_sample_monotonic_time_seconds());
    const auto remaining_at_sample = (std::max)(0.0,
        end_time - (std::max)(sample_time,
            correction.last_sample_monotonic_time_seconds()));
    const auto factor = remaining_at_last > 0.0
        ? static_cast<float>(remaining_at_sample / remaining_at_last)
        : 0.0F;
    return {
        correction.current_residual_offset().x * factor,
        correction.current_residual_offset().y * factor,
        correction.current_residual_offset().z * factor,
    };
}

[[nodiscard]] gameplay_camera::GameplayCameraStateCreateInfo camera_info(
    const gameplay_camera::GameplayCameraState& camera) noexcept
{
    gameplay_camera::GameplayCameraStateCreateInfo info;
    info.position = camera.position();
    info.yaw_degrees = camera.yaw_degrees();
    info.pitch_degrees = camera.pitch_degrees();
    info.vertical_fov_radians = camera.vertical_fov_radians();
    info.near_plane = camera.near_plane();
    info.far_plane = camera.far_plane();
    info.mode = camera.mode();
    info.anchor_metadata = camera.anchor_metadata();
    info.revision = camera.revision();
    info.compatibility_profile = camera.compatibility_profile();
    info.evidence_profile = camera.evidence_profile();
    return info;
}

[[nodiscard]] bool same_camera_content(
    const gameplay_camera::GameplayCameraState& camera,
    const gameplay_camera::GameplayCameraStateCreateInfo& info) noexcept
{
    return camera.position().x == info.position.x &&
        camera.position().y == info.position.y &&
        camera.position().z == info.position.z &&
        camera.yaw_degrees() == info.yaw_degrees &&
        camera.pitch_degrees() == info.pitch_degrees &&
        camera.vertical_fov_radians() == info.vertical_fov_radians &&
        camera.near_plane() == info.near_plane &&
        camera.far_plane() == info.far_plane && camera.mode() == info.mode &&
        camera.anchor_metadata() == info.anchor_metadata &&
        camera.compatibility_profile() == info.compatibility_profile &&
        camera.evidence_profile() == info.evidence_profile;
}

} // namespace

PredictionVisualCorrectionState::PredictionVisualCorrectionState(
    const PredictionCorrectionClass correction_class,
    const assets::AssetVector3 initial_position_offset,
    const assets::AssetVector3 current_residual_offset,
    const double start_monotonic_time_seconds,
    const double last_sample_monotonic_time_seconds,
    const double duration_seconds,
    const std::uint64_t source_authority_update_ordinal,
    const std::uint64_t old_prediction_revision,
    const std::uint64_t new_prediction_revision,
    const std::uint64_t camera_publication_revision,
    std::optional<gameplay_camera::GameplayCameraState> last_published_camera,
    const bool collision_constrained,
    const PredictionVisualCorrectionProfile profile) noexcept
    : correction_class_{correction_class},
      initial_position_offset_{initial_position_offset},
      current_residual_offset_{current_residual_offset},
      start_monotonic_time_seconds_{start_monotonic_time_seconds},
      last_sample_monotonic_time_seconds_{
          last_sample_monotonic_time_seconds},
      duration_seconds_{duration_seconds},
      source_authority_update_ordinal_{source_authority_update_ordinal},
      old_prediction_revision_{old_prediction_revision},
      new_prediction_revision_{new_prediction_revision},
      camera_publication_revision_{camera_publication_revision},
      last_published_camera_{std::move(last_published_camera)},
      collision_constrained_{collision_constrained},
      profile_{profile}
{
}

PredictionVisualCorrectionState PredictionVisualCorrectionState::inactive()
    noexcept
{
    return PredictionVisualCorrectionState{PredictionCorrectionClass::exact,
        {}, {}, 0.0, 0.0, 0.0, 0U, 0U, 0U, 0U, std::nullopt, false,
        PredictionVisualCorrectionProfile::no_smoothing_snap_v1};
}

PredictionCorrectionClass PredictionVisualCorrectionState::correction_class()
    const noexcept { return correction_class_; }
const assets::AssetVector3&
PredictionVisualCorrectionState::initial_position_offset() const noexcept
{ return initial_position_offset_; }
const assets::AssetVector3&
PredictionVisualCorrectionState::current_residual_offset() const noexcept
{ return current_residual_offset_; }
double PredictionVisualCorrectionState::start_monotonic_time_seconds()
    const noexcept { return start_monotonic_time_seconds_; }
double PredictionVisualCorrectionState::last_sample_monotonic_time_seconds()
    const noexcept { return last_sample_monotonic_time_seconds_; }
double PredictionVisualCorrectionState::duration_seconds() const noexcept
{ return duration_seconds_; }
std::uint64_t
PredictionVisualCorrectionState::source_authority_update_ordinal() const noexcept
{ return source_authority_update_ordinal_; }
std::uint64_t PredictionVisualCorrectionState::old_prediction_revision()
    const noexcept { return old_prediction_revision_; }
std::uint64_t PredictionVisualCorrectionState::new_prediction_revision()
    const noexcept { return new_prediction_revision_; }
std::uint64_t PredictionVisualCorrectionState::camera_publication_revision()
    const noexcept { return camera_publication_revision_; }
const std::optional<gameplay_camera::GameplayCameraState>&
PredictionVisualCorrectionState::last_published_camera() const noexcept
{ return last_published_camera_; }
bool PredictionVisualCorrectionState::collision_constrained() const noexcept
{ return collision_constrained_; }
PredictionVisualCorrectionProfile PredictionVisualCorrectionState::profile()
    const noexcept { return profile_; }
bool PredictionVisualCorrectionState::active() const noexcept
{
    return correction_class_ ==
            PredictionCorrectionClass::small_visual_correction &&
        duration_seconds_ > 0.0 &&
        magnitude(current_residual_offset_) > 0.0;
}

bool valid_prediction_visual_correction_config(
    const PredictionVisualCorrectionConfig& config) noexcept
{
    return std::isfinite(config.duration_seconds) &&
        std::isfinite(config.maximum_duration_seconds) &&
        std::isfinite(config.maximum_offset_magnitude) &&
        config.duration_seconds >= 0.0 &&
        config.maximum_duration_seconds >= 0.0 &&
        config.maximum_duration_seconds <= 1.0 &&
        config.duration_seconds <= config.maximum_duration_seconds &&
        config.maximum_offset_magnitude > 0.0 &&
        config.profile != PredictionVisualCorrectionProfile::
            stock_visual_correction_evidence_pending;
}

PredictionVisualCorrectionCreateResult begin_prediction_visual_correction(
    const std::optional<PredictionVisualCorrectionState>& previous,
    const assets::AssetVector3& old_physical_eye,
    const assets::AssetVector3& corrected_eye,
    const PredictionCorrectionClass correction_class,
    const double start_monotonic_time_seconds,
    const std::uint64_t source_authority_update_ordinal,
    const std::uint64_t old_prediction_revision,
    const std::uint64_t new_prediction_revision,
    const PredictionVisualCorrectionConfig& config) noexcept
{
    if (!valid_prediction_visual_correction_config(config) ||
        !finite_vector(old_physical_eye) || !finite_vector(corrected_eye) ||
        !std::isfinite(start_monotonic_time_seconds) ||
        start_monotonic_time_seconds < 0.0 ||
        source_authority_update_ordinal == 0U ||
        old_prediction_revision == 0U || new_prediction_revision == 0U) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::visual_correction_failed,
                std::nullopt,
                "visual correction input or configuration is invalid"}};
    }
    const auto smooth = correction_class ==
            PredictionCorrectionClass::small_visual_correction &&
        config.profile == PredictionVisualCorrectionProfile::
            project_linear_decay_collision_constrained_v1 &&
        config.duration_seconds > 0.0;
    const auto camera_publication_revision = previous
        ? previous->camera_publication_revision()
        : 0U;
    const auto last_published_camera = previous
        ? previous->last_published_camera()
        : std::optional<gameplay_camera::GameplayCameraState>{};
    if (!smooth) {
        const auto continue_previous = previous && previous->active() &&
            (correction_class == PredictionCorrectionClass::exact ||
                correction_class == PredictionCorrectionClass::
                    replay_without_visual_offset);
        if (continue_previous) {
            const auto residual = residual_at(
                *previous, start_monotonic_time_seconds);
            if (magnitude(residual) > 0.0) {
                return {PredictionVisualCorrectionState{
                            PredictionCorrectionClass::
                                small_visual_correction,
                            previous->initial_position_offset(), residual,
                            previous->start_monotonic_time_seconds(),
                            start_monotonic_time_seconds,
                            previous->duration_seconds(),
                            previous->source_authority_update_ordinal(),
                            previous->old_prediction_revision(),
                            new_prediction_revision,
                            camera_publication_revision,
                            last_published_camera,
                            previous->collision_constrained(),
                            previous->profile()},
                    std::nullopt};
            }
        }
        return {PredictionVisualCorrectionState{correction_class, {}, {},
                    start_monotonic_time_seconds, start_monotonic_time_seconds,
                    0.0,
                    source_authority_update_ordinal, old_prediction_revision,
                    new_prediction_revision, camera_publication_revision,
                    last_published_camera, false,
                    PredictionVisualCorrectionProfile::no_smoothing_snap_v1},
            std::nullopt};
    }
    auto combined = assets::AssetVector3{
        old_physical_eye.x - corrected_eye.x,
        old_physical_eye.y - corrected_eye.y,
        old_physical_eye.z - corrected_eye.z,
    };
    if (previous && previous->active()) {
        const auto prior_residual = residual_at(*previous,
            start_monotonic_time_seconds);
        combined.x += prior_residual.x;
        combined.y += prior_residual.y;
        combined.z += prior_residual.z;
    }
    const auto combined_magnitude = magnitude(combined);
    if (!finite_vector(combined) || !std::isfinite(combined_magnitude)) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::non_finite_result,
                std::nullopt, "combined visual correction is non-finite"}};
    }
    if (combined_magnitude > config.maximum_offset_magnitude) {
        return {PredictionVisualCorrectionState{
                    PredictionCorrectionClass::large_snap, {}, {},
                    start_monotonic_time_seconds, start_monotonic_time_seconds,
                    0.0,
                    source_authority_update_ordinal, old_prediction_revision,
                    new_prediction_revision, camera_publication_revision,
                    last_published_camera, false,
                    PredictionVisualCorrectionProfile::no_smoothing_snap_v1},
            std::nullopt};
    }
    return {PredictionVisualCorrectionState{correction_class, combined,
                combined, start_monotonic_time_seconds,
                start_monotonic_time_seconds,
                config.duration_seconds, source_authority_update_ordinal,
                old_prediction_revision, new_prediction_revision,
                camera_publication_revision, last_published_camera, false,
                config.profile},
        std::nullopt};
}

PredictionVisualCorrectionSampleResult sample_prediction_visual_correction(
    const PredictionVisualCorrectionState& correction,
    const gameplay_camera::GameplayCameraState& corrected_camera,
    const double sample_monotonic_time_seconds,
    const collision::CollisionWorldQuery* const collision_query,
    collision::CollisionQueryScratch& scratch,
    const collision::CollisionQueryLimits& query_limits,
    const std::uint64_t maximum_camera_revision)
{
    if (!std::isfinite(sample_monotonic_time_seconds) ||
        sample_monotonic_time_seconds < 0.0 ||
        !collision::valid_collision_query_limits(query_limits) ||
        maximum_camera_revision == 0U) {
        return {std::nullopt, std::nullopt,
            PredictionError{PredictionErrorCode::visual_correction_failed,
                std::nullopt,
                "visual correction sample time or collision limits are invalid"}};
    }
    if (corrected_camera.revision() > maximum_camera_revision ||
        correction.camera_publication_revision() > maximum_camera_revision) {
        return {std::nullopt, std::nullopt,
            PredictionError{PredictionErrorCode::revision_exhausted,
                std::nullopt,
                "visual correction camera revision is exhausted"}};
    }
    auto residual = residual_at(correction, sample_monotonic_time_seconds);
    const auto completed = correction.active() && magnitude(residual) == 0.0;
    auto desired = assets::AssetVector3{
        corrected_camera.position().x + residual.x,
        corrected_camera.position().y + residual.y,
        corrected_camera.position().z + residual.z,
    };
    auto constrained = false;
    if (magnitude(residual) > 0.0) {
        if (collision_query == nullptr || collision_query->package() == nullptr) {
            return {std::nullopt, std::nullopt,
                PredictionError{
                    PredictionErrorCode::visual_correction_collision_failed,
                    std::nullopt,
                    "active visual correction requires a collision query"}};
        }
        collision::CollisionTraceRequest request;
        request.start = corrected_camera.position();
        request.end = desired;
        request.source_model_index = 0U;
        request.hull = collision::CollisionHullOrdinal::point;
        request.limits = query_limits;
        const auto traced = collision_query->trace_line(request, scratch);
        if (!traced || !traced.result || traced.result->start_solid ||
            traced.result->all_solid ||
            !finite_vector(traced.result->end_position)) {
            return {std::nullopt, std::nullopt,
                PredictionError{
                    PredictionErrorCode::visual_correction_collision_failed,
                    std::nullopt,
                    "camera correction point trace failed or began blocking"}};
        }
        if (traced.result->fraction < 1.0) {
            desired = traced.result->end_position;
            residual = {
                desired.x - corrected_camera.position().x,
                desired.y - corrected_camera.position().y,
                desired.z - corrected_camera.position().z,
            };
            constrained = true;
        }
        collision::CollisionPointContentsRequest position_request;
        position_request.point = desired;
        position_request.source_model_index = 0U;
        position_request.hull = collision::CollisionHullOrdinal::point;
        position_request.limits = query_limits;
        const auto tested =
            collision_query->test_position(position_request, scratch);
        if (!tested || !tested.result ||
            tested.result->status == collision::CollisionPositionStatus::blocking) {
            return {std::nullopt, std::nullopt,
                PredictionError{
                    PredictionErrorCode::visual_correction_collision_failed,
                    std::nullopt,
                    "camera correction endpoint is blocking"}};
        }
    }
    auto info = camera_info(corrected_camera);
    info.position = desired;
    const auto& previous_camera = correction.last_published_camera();
    const auto presentation_changed = previous_camera
        ? !same_camera_content(*previous_camera, info)
        : !same_camera_content(corrected_camera, info);
    if (presentation_changed) {
        const auto presentation_matches_newer_physical_camera =
            same_camera_content(corrected_camera, info) &&
            corrected_camera.revision() >
                correction.camera_publication_revision();
        if (presentation_matches_newer_physical_camera) {
            info.revision = corrected_camera.revision();
        } else {
            const auto publication_revision = (std::max)(
                corrected_camera.revision(),
                correction.camera_publication_revision());
            if (publication_revision >= maximum_camera_revision) {
                return {std::nullopt, std::nullopt,
                    PredictionError{PredictionErrorCode::revision_exhausted,
                        std::nullopt,
                        "visual correction camera revision is exhausted"}};
            }
            info.revision = publication_revision + 1U;
        }
    } else {
        info.revision = previous_camera
            ? previous_camera->revision()
            : corrected_camera.revision();
    }
    const auto created = gameplay_camera::GameplayCameraState::create(info);
    if (!created || !created.state) {
        return {std::nullopt, std::nullopt,
            PredictionError{PredictionErrorCode::visual_correction_failed,
                std::nullopt, "visual correction camera publication failed"}};
    }
    auto next = PredictionVisualCorrectionState{correction.correction_class(),
        correction.initial_position_offset(), residual,
        correction.start_monotonic_time_seconds(),
        sample_monotonic_time_seconds,
        completed ? 0.0 : correction.duration_seconds(),
        correction.source_authority_update_ordinal(),
        correction.old_prediction_revision(),
        correction.new_prediction_revision(), info.revision, *created.state,
        constrained,
        completed ? PredictionVisualCorrectionProfile::no_smoothing_snap_v1
                  : correction.profile()};
    return {std::move(*created.state), std::move(next), std::nullopt,
        completed, constrained};
}

} // namespace hlclient::prediction
