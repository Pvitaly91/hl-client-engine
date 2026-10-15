#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc {
namespace {

constexpr double kAlphaBoundaryTolerance = 1.0e-12;

[[nodiscard]] EntitySnapshotPairSelectionResult
fail_selection(const EntityInterpolationErrorCode code, std::string context,
               const std::optional<std::uint32_t> reference = std::nullopt)
{
    return {
        std::nullopt,
        EntityInterpolationError{code, reference, std::nullopt,
                                 std::move(context)},
    };
}

[[nodiscard]] InterpolatedEntityFrameResult
fail_frame(const EntityInterpolationErrorCode code, std::string context,
           const std::optional<std::uint32_t> reference = std::nullopt,
           const std::optional<std::uint32_t> entity_number = std::nullopt)
{
    return {
        std::nullopt,
        EntityInterpolationError{code, reference, entity_number,
                                 std::move(context)},
    };
}

[[nodiscard]] EntityInterpolationProjectionAdapterResult
fail_projection_adapter(
    const EntityInterpolationErrorCode code,
    std::string context,
    const std::optional<std::uint32_t> reference = std::nullopt,
    const std::optional<std::uint32_t> entity_number = std::nullopt)
{
    return {
        std::nullopt,
        EntityInterpolationError{
            code, reference, entity_number, std::move(context)},
    };
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool position_within_limit(const assets::AssetVector3& value,
                                         const float limit) noexcept
{
    return finite_vector(value) && std::abs(value.x) <= limit &&
           std::abs(value.y) <= limit && std::abs(value.z) <= limit;
}

[[nodiscard]] bool scale_within_limit(const assets::AssetVector3& value,
                                      const float limit) noexcept
{
    return finite_vector(value) && value.x > 0.0F && value.y > 0.0F &&
           value.z > 0.0F && value.x <= limit && value.y <= limit &&
           value.z <= limit;
}

template <std::size_t Size>
[[nodiscard]] bool finite_unit_values(
    const std::array<float, Size>& values) noexcept
{
    return std::ranges::all_of(values, [](const float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    });
}

[[nodiscard]] bool finite_unit_color(
    const entity_visual::EntityVisualRenderColor& color) noexcept
{
    return std::isfinite(color.red) && color.red >= 0.0F && color.red <= 1.0F &&
           std::isfinite(color.green) && color.green >= 0.0F &&
           color.green <= 1.0F && std::isfinite(color.blue) &&
           color.blue >= 0.0F && color.blue <= 1.0F;
}

[[nodiscard]] bool valid_interpolation_mode(
    const EntityInterpolationMode mode) noexcept
{
    switch (mode) {
    case EntityInterpolationMode::interpolate:
    case EntityInterpolationMode::step:
    case EntityInterpolationMode::teleport:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_render_mode(const std::int32_t mode) noexcept
{
    switch (static_cast<entity_visual::EntityVisualRenderMode>(mode)) {
    case entity_visual::EntityVisualRenderMode::source_asset_default:
    case entity_visual::EntityVisualRenderMode::opaque:
    case entity_visual::EntityVisualRenderMode::alpha_test:
    case entity_visual::EntityVisualRenderMode::additive:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_selection_status(
    const EntitySnapshotPairSelectionStatus status) noexcept
{
    switch (status) {
    case EntitySnapshotPairSelectionStatus::bracketed:
    case EntitySnapshotPairSelectionStatus::exact_previous:
    case EntitySnapshotPairSelectionStatus::exact_current:
    case EntitySnapshotPairSelectionStatus::held_oldest:
    case EntitySnapshotPairSelectionStatus::held_newest:
    case EntitySnapshotPairSelectionStatus::held_only:
        return true;
    }
    return false;
}

[[nodiscard]] bool coherent_pair_selection(
    const EntitySnapshotPairSelection& selection,
    const EntityInterpolationLimits& limits) noexcept
{
    if (selection.previous == nullptr || selection.current == nullptr ||
        !std::isfinite(selection.previous_seconds) ||
        !std::isfinite(selection.current_seconds) ||
        !std::isfinite(selection.target_seconds) ||
        !std::isfinite(selection.alpha) || selection.alpha < 0.0 ||
        selection.alpha > 1.0 || !valid_selection_status(selection.status)) {
        return false;
    }

    const bool held = selection.previous == selection.current;
    if (held) {
        if (selection.previous_seconds != selection.current_seconds ||
            selection.alpha != 0.0) {
            return false;
        }
        switch (selection.status) {
        case EntitySnapshotPairSelectionStatus::held_oldest:
            return selection.target_seconds <= selection.previous_seconds;
        case EntitySnapshotPairSelectionStatus::held_newest:
            return selection.target_seconds >= selection.current_seconds;
        case EntitySnapshotPairSelectionStatus::held_only:
            return true;
        case EntitySnapshotPairSelectionStatus::bracketed:
        case EntitySnapshotPairSelectionStatus::exact_previous:
        case EntitySnapshotPairSelectionStatus::exact_current:
            return false;
        }
        return false;
    }

    if (selection.previous->reference().value() ==
            selection.current->reference().value() ||
        !(selection.previous_seconds < selection.current_seconds)) {
        return false;
    }
    const auto duration =
        selection.current_seconds - selection.previous_seconds;
    if (!std::isfinite(duration) ||
        duration > limits.maximum_snapshot_gap_seconds ||
        selection.target_seconds < selection.previous_seconds ||
        selection.target_seconds > selection.current_seconds) {
        return false;
    }
    const auto expected_alpha =
        (selection.target_seconds - selection.previous_seconds) / duration;
    if (!std::isfinite(expected_alpha) ||
        std::abs(expected_alpha - selection.alpha) >
            kAlphaBoundaryTolerance) {
        return false;
    }
    if (selection.alpha == 0.0) {
        return selection.status ==
            EntitySnapshotPairSelectionStatus::exact_previous;
    }
    if (selection.alpha == 1.0) {
        return selection.status ==
            EntitySnapshotPairSelectionStatus::exact_current;
    }
    return selection.status == EntitySnapshotPairSelectionStatus::bracketed;
}

[[nodiscard]] std::optional<EntityInterpolationMode>
adapt_interpolation_mode(
    const entity_visual::EntityInterpolationMode mode) noexcept
{
    switch (mode) {
    case entity_visual::EntityInterpolationMode::interpolate:
        return EntityInterpolationMode::interpolate;
    case entity_visual::EntityInterpolationMode::step:
        return EntityInterpolationMode::step;
    case entity_visual::EntityInterpolationMode::teleport:
        return EntityInterpolationMode::teleport;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::int32_t>
adapt_render_mode(const entity_visual::EntityVisualRenderMode mode) noexcept
{
    switch (mode) {
    case entity_visual::EntityVisualRenderMode::source_asset_default:
    case entity_visual::EntityVisualRenderMode::opaque:
    case entity_visual::EntityVisualRenderMode::alpha_test:
    case entity_visual::EntityVisualRenderMode::additive:
        return static_cast<std::int32_t>(mode);
    }
    return std::nullopt;
}

[[nodiscard]] float lerp_float(const float previous, const float current,
                               const double alpha) noexcept
{
    return static_cast<float>(static_cast<double>(previous) +
                              alpha *
                                  (static_cast<double>(current) - previous));
}

[[nodiscard]] assets::AssetVector3
lerp_vector(const assets::AssetVector3& previous,
            const assets::AssetVector3& current, const double alpha) noexcept
{
    return {
        lerp_float(previous.x, current.x, alpha),
        lerp_float(previous.y, current.y, alpha),
        lerp_float(previous.z, current.z, alpha),
    };
}

// The exact +180-degree tie maps to -180, matching the requested half-open
// interval [-180, 180).
[[nodiscard]] double shortest_degree_delta(const double previous,
                                           const double current) noexcept
{
    auto delta = std::fmod(current - previous, 360.0);
    if (delta < -180.0) {
        delta += 360.0;
    } else if (delta >= 180.0) {
        delta -= 360.0;
    }
    return delta;
}

[[nodiscard]] assets::AssetVector3
interpolate_angles(const assets::AssetVector3& previous,
                   const assets::AssetVector3& current,
                   const double alpha) noexcept
{
    return {
        static_cast<float>(
            previous.x + alpha * shortest_degree_delta(previous.x, current.x)),
        static_cast<float>(
            previous.y + alpha * shortest_degree_delta(previous.y, current.y)),
        static_cast<float>(
            previous.z + alpha * shortest_degree_delta(previous.z, current.z)),
    };
}

[[nodiscard]] bool checked_multiply(const std::size_t left,
                                    const std::size_t right,
                                    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right,
                               std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] std::optional<EntityInterpolationError>
validate_projection(const EntitySnapshotState& snapshot,
                    const EntityInterpolationProjectionFrame& projection,
                    const EntityInterpolationLimits& limits)
{
    if (projection.snapshot_reference != snapshot.reference().value() ||
        projection.entities.size() != snapshot.entity_count()) {
        return EntityInterpolationError{
            EntityInterpolationErrorCode::invalid_projection,
            snapshot.reference().value(),
            std::nullopt,
            "Explicit projection does not match its immutable snapshot",
        };
    }

    std::size_t event_count = 0U;
    const auto snapshot_entities = snapshot.entities();
    for (std::size_t index = 0U; index < projection.entities.size(); ++index) {
        const auto& state = projection.entities[index];
        if (snapshot_entities[index].entity_number() != state.entity_number ||
            (index != 0U && projection.entities[index - 1U].entity_number >=
                                state.entity_number)) {
            return EntityInterpolationError{
                EntityInterpolationErrorCode::invalid_projection,
                snapshot.reference().value(),
                state.entity_number,
                "Explicit projection identities must exactly match sorted "
                "snapshot "
                "entities",
            };
        }
        if (!position_within_limit(state.position,
                                   limits.maximum_position_magnitude)) {
            return EntityInterpolationError{
                EntityInterpolationErrorCode::position_limit_exceeded,
                snapshot.reference().value(),
                state.entity_number,
                "Entity position is non-finite or exceeds the configured "
                "magnitude",
            };
        }
        if (!finite_vector(state.angles_degrees) ||
            !std::isfinite(state.studio_frame_coordinate)) {
            return EntityInterpolationError{
                EntityInterpolationErrorCode::invalid_projection,
                snapshot.reference().value(),
                state.entity_number,
                "Entity angles and Studio frame coordinate must be finite",
            };
        }
        if (!scale_within_limit(state.scale, limits.maximum_scale)) {
            return EntityInterpolationError{
                EntityInterpolationErrorCode::scale_limit_exceeded,
                snapshot.reference().value(),
                state.entity_number,
                "Entity scale is non-finite, non-positive, or exceeds its "
                "limit",
            };
        }
        const auto typed_reference = state.model_reference;
        if (state.entity_number == 0U ||
            typed_reference.profile() != entity_visual::
                EntityVisualModelReferenceProfile::
                    synthetic_type_local_model_slot ||
            typed_reference.value() != state.discrete.model_reference ||
            !valid_interpolation_mode(state.mode) ||
            !valid_render_mode(state.discrete.render_mode) ||
            !finite_unit_values(state.controller_values) ||
            !finite_unit_values(state.blending_values) ||
            !std::isfinite(state.mouth_value) || state.mouth_value < 0.0F ||
            state.mouth_value > 1.0F || !std::isfinite(state.render_amount) ||
            state.render_amount < 0.0F || state.render_amount > 1.0F ||
            !finite_unit_color(state.render_color) ||
            (state.animation_start_time_seconds &&
             (!std::isfinite(*state.animation_start_time_seconds) ||
              std::abs(*state.animation_start_time_seconds) >
                  entity_visual::
                      kHardMaximumSyntheticAnimationTimeSeconds))) {
            return EntityInterpolationError{
                EntityInterpolationErrorCode::invalid_projection,
                snapshot.reference().value(),
                state.entity_number,
                "Entity projection controls or typed model reference are invalid",
            };
        }
        if (state.inert_event_count >
            limits.maximum_events -
                std::min(event_count, limits.maximum_events)) {
            return EntityInterpolationError{
                EntityInterpolationErrorCode::event_limit_exceeded,
                snapshot.reference().value(),
                state.entity_number,
                "Explicit inert event count exceeds the interpolation limit",
            };
        }
        event_count += state.inert_event_count;
    }
    return std::nullopt;
}

} // namespace

EntityInterpolationTime::EntityInterpolationTime(
    const EntityInterpolationTimeDomain domain,
    std::optional<double> seconds) noexcept
    : domain_{domain}, seconds_{std::move(seconds)}
{
}

std::optional<EntityInterpolationTime>
EntityInterpolationTime::synthetic_seconds(const double seconds) noexcept
{
    if (!std::isfinite(seconds)) {
        return std::nullopt;
    }
    return EntityInterpolationTime{
        EntityInterpolationTimeDomain::synthetic_seconds_v1, seconds};
}

EntityInterpolationTime
EntityInterpolationTime::stock_evidence_pending() noexcept
{
    return EntityInterpolationTime{
        EntityInterpolationTimeDomain::stock_server_time_evidence_pending,
        std::nullopt};
}

EntityInterpolationTimeDomain EntityInterpolationTime::domain() const noexcept
{
    return domain_;
}

std::optional<double> EntityInterpolationTime::finite_seconds() const noexcept
{
    return seconds_;
}

EntitySnapshotExplicitTime::EntitySnapshotExplicitTime(
    const std::uint32_t snapshot_reference,
    const std::int64_t opaque_raw_server_time, const double seconds) noexcept
    : snapshot_reference_{snapshot_reference},
      opaque_raw_server_time_{opaque_raw_server_time}, seconds_{seconds}
{
}

std::optional<EntitySnapshotExplicitTime>
EntitySnapshotExplicitTime::bind_synthetic_seconds(
    const EntitySnapshotState& snapshot, const double seconds) noexcept
{
    if (!std::isfinite(seconds) ||
        snapshot.compatibility_profile() !=
            EntitySnapshotCompatibilityProfile::synthetic_neutral_v1 ||
        snapshot.reference().policy() !=
            EntitySnapshotReferencePolicy::synthetic_uint32_non_wrapping ||
        snapshot.server_time().evidence_profile() !=
            EntitySnapshotEvidenceProfile::caller_supplied_typed_records) {
        return std::nullopt;
    }
    return EntitySnapshotExplicitTime{snapshot.reference().value(),
                                      snapshot.server_time().raw_value(),
                                      seconds};
}

std::uint32_t EntitySnapshotExplicitTime::snapshot_reference() const noexcept
{
    return snapshot_reference_;
}

std::int64_t EntitySnapshotExplicitTime::opaque_raw_server_time() const noexcept
{
    return opaque_raw_server_time_;
}

double EntitySnapshotExplicitTime::seconds() const noexcept
{
    return seconds_;
}

bool valid_entity_interpolation_limits(
    const EntityInterpolationLimits& limits) noexcept
{
    return limits.maximum_entities > 0U &&
           limits.maximum_entities <= kHardMaximumInterpolatedEntities &&
           std::isfinite(limits.maximum_snapshot_gap_seconds) &&
           limits.maximum_snapshot_gap_seconds > 0.0 &&
           limits.maximum_snapshot_gap_seconds <=
               kHardMaximumSnapshotGapSeconds &&
           std::isfinite(limits.maximum_position_magnitude) &&
           limits.maximum_position_magnitude > 0.0F &&
           limits.maximum_position_magnitude <=
               kHardMaximumInterpolatedPositionMagnitude &&
           std::isfinite(limits.maximum_scale) && limits.maximum_scale > 0.0F &&
           limits.maximum_scale <= kHardMaximumInterpolatedScale &&
           limits.maximum_events <= kHardMaximumInterpolationEvents &&
           limits.maximum_result_bytes > 0U &&
           limits.maximum_result_bytes <= kHardMaximumInterpolatedResultBytes;
}

EntityInterpolationProjectionFrameState::
    EntityInterpolationProjectionFrameState(
        const std::uint32_t snapshot_reference,
        std::vector<SyntheticEntityInterpolationState> entities) noexcept
    : snapshot_reference_{snapshot_reference}, entities_{std::move(entities)}
{
}

std::uint32_t
EntityInterpolationProjectionFrameState::snapshot_reference() const noexcept
{
    return snapshot_reference_;
}

std::span<const SyntheticEntityInterpolationState>
EntityInterpolationProjectionFrameState::entities() const noexcept
{
    return entities_;
}

EntityInterpolationProjectionFrame
EntityInterpolationProjectionFrameState::view() const noexcept
{
    return {snapshot_reference_, entities_};
}

EntityInterpolationProjectionAdapterResult
EntityInterpolationProjectionAdapter::build(
    const EntitySnapshotState& snapshot,
    const std::span<const entity_visual::EntityVisualProjectionState>
        projections,
    const EntityInterpolationLimits& limits) const noexcept
{
    try {
        if (!valid_entity_interpolation_limits(limits)) {
            return fail_projection_adapter(
                EntityInterpolationErrorCode::invalid_configuration,
                "Projection adapter limits are invalid or exceed hard caps");
        }
        if (snapshot.compatibility_profile() !=
                EntitySnapshotCompatibilityProfile::synthetic_neutral_v1 ||
            snapshot.evidence_profile() !=
                EntitySnapshotEvidenceProfile::caller_supplied_typed_records) {
            return fail_projection_adapter(
                EntityInterpolationErrorCode::evidence_pending,
                "Only explicit synthetic visual projections may enter interpolation",
                snapshot.reference().value());
        }
        if (projections.size() != snapshot.entity_count()) {
            return fail_projection_adapter(
                EntityInterpolationErrorCode::invalid_projection,
                "Projection count does not exactly match the immutable snapshot",
                snapshot.reference().value());
        }
        if (projections.size() > limits.maximum_entities) {
            return fail_projection_adapter(
                EntityInterpolationErrorCode::entity_limit_exceeded,
                "Projection count exceeds the interpolation entity limit",
                snapshot.reference().value());
        }
        std::size_t accounted_bytes = 0U;
        if (!checked_multiply(
                projections.size(),
                sizeof(SyntheticEntityInterpolationState),
                accounted_bytes) ||
            accounted_bytes > limits.maximum_result_bytes) {
            return fail_projection_adapter(
                EntityInterpolationErrorCode::result_byte_limit_exceeded,
                "Owning projection frame exceeds its accounted byte limit",
                snapshot.reference().value());
        }

        std::vector<SyntheticEntityInterpolationState> entities;
        entities.reserve(projections.size());
        const auto snapshot_entities = snapshot.entities();
        for (std::size_t index = 0U; index < projections.size(); ++index) {
            const auto& projection = projections[index];
            const auto entity_number = projection.entity_number();
            const auto& reference = projection.model_reference();
            const auto interpolation_mode =
                adapt_interpolation_mode(projection.interpolation_mode());
            const auto render_mode =
                adapt_render_mode(projection.render_controls().mode);
            if (snapshot_entities[index].entity_number() != entity_number ||
                projection.source_snapshot_reference() != snapshot.reference() ||
                projection.compatibility_profile() != entity_visual::
                    EntityVisualProjectionCompatibilityProfile::
                        synthetic_entity_visual_v1 ||
                projection.evidence_profile() != entity_visual::
                    EntityVisualProjectionEvidenceProfile::
                        caller_supplied_typed_synthetic_records ||
                reference.profile() != entity_visual::
                    EntityVisualModelReferenceProfile::
                        synthetic_type_local_model_slot ||
                !interpolation_mode || !render_mode) {
                return fail_projection_adapter(
                    EntityInterpolationErrorCode::invalid_projection,
                    "Projection identity, evidence or typed controls do not match the snapshot",
                    snapshot.reference().value(),
                    entity_number);
            }

            const auto& transform = projection.transform();
            const auto& studio = projection.studio_controls();
            const auto& sprite = projection.sprite_controls();
            const auto& render = projection.render_controls();
            SyntheticEntityInterpolationState state;
            state.entity_number = entity_number;
            state.model_reference = reference;
            state.position = {
                transform.origin.x, transform.origin.y, transform.origin.z};
            state.angles_degrees = {
                transform.angles_degrees.x,
                transform.angles_degrees.y,
                transform.angles_degrees.z};
            state.scale = {transform.scale, transform.scale, transform.scale};
            state.studio_frame_coordinate = studio.frame_coordinate;
            state.controller_values = studio.controller_values;
            state.blending_values = studio.blending_values;
            state.mouth_value = studio.mouth_value;
            state.render_amount = render.amount;
            state.render_color = render.color;
            state.animation_start_time_seconds =
                projection.animation_start_time_seconds();
            state.mode = *interpolation_mode;
            state.discrete.model_reference = reference.value();
            state.discrete.sequence_index = studio.sequence_index;
            state.discrete.body_value =
                static_cast<std::int32_t>(studio.body_value);
            state.discrete.skin_family_index = studio.skin_family_index;
            state.discrete.render_mode = *render_mode;
            state.discrete.sprite_frame_category = sprite.frame_index;
            state.discrete.effects_flags = render.effects_metadata;
            entities.push_back(std::move(state));
        }

        EntityInterpolationProjectionFrameState frame{
            snapshot.reference().value(), std::move(entities)};
        if (const auto error = validate_projection(snapshot, frame.view(), limits)) {
            return {std::nullopt, std::move(*error)};
        }
        return {std::move(frame), std::nullopt};
    } catch (const std::bad_alloc&) {
        return fail_projection_adapter(
            EntityInterpolationErrorCode::unable_to_retain_result,
            "Unable to allocate the owning interpolation projection frame",
            snapshot.reference().value());
    } catch (const std::length_error&) {
        return fail_projection_adapter(
            EntityInterpolationErrorCode::unable_to_retain_result,
            "Owning interpolation projection container length is invalid",
            snapshot.reference().value());
    } catch (...) {
        return {
            std::nullopt,
            EntityInterpolationError{
                EntityInterpolationErrorCode::unable_to_retain_result,
                snapshot.reference().value(),
                std::nullopt,
                {}},
        };
    }
}

EntitySnapshotPairSelectionResult EntitySnapshotPairSelector::select(
    const EntitySnapshotHistoryState& history,
    const std::span<const EntitySnapshotExplicitTime> explicit_times,
    const EntityInterpolationTime& target,
    const EntityInterpolationTimeDomain profile,
    const EntityInterpolationLimits& limits) const
{
    if (!valid_entity_interpolation_limits(limits) ||
        target.domain() != profile) {
        return fail_selection(
            EntityInterpolationErrorCode::invalid_configuration,
            "Interpolation limits and target time domain must be valid");
    }
    if (profile ==
            EntityInterpolationTimeDomain::stock_server_time_evidence_pending ||
        history.compatibility_profile() ==
            EntitySnapshotCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) {
        return fail_selection(EntityInterpolationErrorCode::evidence_pending,
                              "Stock server-time conversion and projection "
                              "remain evidence-pending");
    }
    const auto target_seconds = target.finite_seconds();
    if (!target_seconds || !std::isfinite(*target_seconds)) {
        return fail_selection(
            EntityInterpolationErrorCode::invalid_configuration,
            "Synthetic interpolation requires explicit finite seconds");
    }

    const auto snapshots = history.snapshots();
    if (snapshots.empty()) {
        return fail_selection(EntityInterpolationErrorCode::empty_history,
                              "Snapshot history is empty");
    }
    if (snapshots.size() != explicit_times.size()) {
        return fail_selection(
            EntityInterpolationErrorCode::time_adapter_mismatch,
            "Explicit timeline must contain one time per retained snapshot");
    }

    for (std::size_t index = 0U; index < snapshots.size(); ++index) {
        const auto& snapshot = snapshots[index];
        const auto& adapted = explicit_times[index];
        if (snapshot.reference().value() != adapted.snapshot_reference() ||
            snapshot.server_time().raw_value() !=
                adapted.opaque_raw_server_time() ||
            !std::isfinite(adapted.seconds())) {
            return fail_selection(
                EntityInterpolationErrorCode::time_adapter_mismatch,
                "Explicit time is not bound to the corresponding snapshot",
                snapshot.reference().value());
        }
        if (index == 0U) {
            continue;
        }
        const auto previous = explicit_times[index - 1U].seconds();
        const auto current = adapted.seconds();
        if (current == previous) {
            return fail_selection(
                EntityInterpolationErrorCode::duplicate_snapshot_time,
                "Two different snapshots have the same explicit time",
                snapshot.reference().value());
        }
        if (current < previous) {
            return fail_selection(
                EntityInterpolationErrorCode::invalid_snapshot_time_order,
                "Explicit snapshot seconds are not strictly increasing",
                snapshot.reference().value());
        }
        if (current - previous > limits.maximum_snapshot_gap_seconds) {
            return fail_selection(
                EntityInterpolationErrorCode::snapshot_gap_limit_exceeded,
                "Snapshot gap exceeds the configured interpolation bound",
                snapshot.reference().value());
        }
    }

    const auto make_held = [&](const std::size_t index,
                               const EntitySnapshotPairSelectionStatus status) {
        return EntitySnapshotPairSelectionResult{
            EntitySnapshotPairSelection{
                &snapshots[index],
                &snapshots[index],
                explicit_times[index].seconds(),
                explicit_times[index].seconds(),
                *target_seconds,
                0.0,
                status,
                EntityInterpolationEvidenceProfile::
                    synthetic_explicit_projection_v1,
            },
            std::nullopt,
        };
    };

    if (snapshots.size() == 1U) {
        return make_held(0U, EntitySnapshotPairSelectionStatus::held_only);
    }
    if (*target_seconds < explicit_times.front().seconds()) {
        return make_held(0U, EntitySnapshotPairSelectionStatus::held_oldest);
    }
    if (*target_seconds > explicit_times.back().seconds()) {
        return make_held(snapshots.size() - 1U,
                         EntitySnapshotPairSelectionStatus::held_newest);
    }

    std::size_t current_index = 1U;
    while (current_index < explicit_times.size() &&
           explicit_times[current_index].seconds() < *target_seconds) {
        ++current_index;
    }
    if (current_index >= explicit_times.size()) {
        current_index = explicit_times.size() - 1U;
    }
    const auto previous_index = current_index - 1U;
    const auto previous_seconds = explicit_times[previous_index].seconds();
    const auto current_seconds = explicit_times[current_index].seconds();
    const auto denominator = current_seconds - previous_seconds;
    if (!(denominator > 0.0) || !std::isfinite(denominator)) {
        return fail_selection(
            EntityInterpolationErrorCode::invalid_snapshot_time_order,
            "Selected snapshot bracket has no positive finite duration",
            snapshots[current_index].reference().value());
    }
    auto alpha = (*target_seconds - previous_seconds) / denominator;
    if (!std::isfinite(alpha) || alpha < -kAlphaBoundaryTolerance ||
        alpha > 1.0 + kAlphaBoundaryTolerance) {
        return fail_selection(
            EntityInterpolationErrorCode::non_finite_result,
            "Interpolation alpha is outside its finite bracket");
    }
    if (alpha < 0.0) {
        alpha = 0.0;
    } else if (alpha > 1.0) {
        alpha = 1.0;
    }
    auto status = EntitySnapshotPairSelectionStatus::bracketed;
    if (alpha == 0.0) {
        status = EntitySnapshotPairSelectionStatus::exact_previous;
    } else if (alpha == 1.0) {
        status = EntitySnapshotPairSelectionStatus::exact_current;
    }
    return {
        EntitySnapshotPairSelection{
            &snapshots[previous_index],
            &snapshots[current_index],
            previous_seconds,
            current_seconds,
            *target_seconds,
            alpha,
            status,
            EntityInterpolationEvidenceProfile::
                synthetic_explicit_projection_v1,
        },
        std::nullopt,
    };
}

InterpolatedEntityState::InterpolatedEntityState(
    SyntheticEntityInterpolationState state,
    const InterpolatedEntityClass interpolation_class) noexcept
    : state_{std::move(state)}, interpolation_class_{interpolation_class}
{
}

std::uint32_t InterpolatedEntityState::entity_number() const noexcept
{
    return state_.entity_number;
}

const assets::AssetVector3& InterpolatedEntityState::position() const noexcept
{
    return state_.position;
}

const assets::AssetVector3&
InterpolatedEntityState::angles_degrees() const noexcept
{
    return state_.angles_degrees;
}

const assets::AssetVector3& InterpolatedEntityState::scale() const noexcept
{
    return state_.scale;
}

float InterpolatedEntityState::studio_frame_coordinate() const noexcept
{
    return state_.studio_frame_coordinate;
}

const entity_visual::EntityVisualModelReference&
InterpolatedEntityState::model_reference() const noexcept
{
    return state_.model_reference;
}

const std::array<float, 4U>&
InterpolatedEntityState::controller_values() const noexcept
{
    return state_.controller_values;
}

const std::array<float, 2U>&
InterpolatedEntityState::blending_values() const noexcept
{
    return state_.blending_values;
}

float InterpolatedEntityState::mouth_value() const noexcept
{
    return state_.mouth_value;
}

std::uint32_t InterpolatedEntityState::sprite_frame_index() const noexcept
{
    return state_.discrete.sprite_frame_category;
}

entity_visual::EntityVisualRenderMode
InterpolatedEntityState::render_mode() const noexcept
{
    return static_cast<entity_visual::EntityVisualRenderMode>(
        state_.discrete.render_mode);
}

float InterpolatedEntityState::render_amount() const noexcept
{
    return state_.render_amount;
}

const entity_visual::EntityVisualRenderColor&
InterpolatedEntityState::render_color() const noexcept
{
    return state_.render_color;
}

const std::optional<double>&
InterpolatedEntityState::animation_start_time_seconds() const noexcept
{
    return state_.animation_start_time_seconds;
}

std::uint32_t InterpolatedEntityState::effects_metadata() const noexcept
{
    return state_.discrete.effects_flags;
}

EntityInterpolationMode InterpolatedEntityState::mode() const noexcept
{
    return state_.mode;
}

InterpolatedEntityClass
InterpolatedEntityState::interpolation_class() const noexcept
{
    return interpolation_class_;
}

const EntityInterpolationDiscreteState&
InterpolatedEntityState::discrete() const noexcept
{
    return state_.discrete;
}

std::size_t InterpolatedEntityState::inert_event_count() const noexcept
{
    return state_.inert_event_count;
}

InterpolatedEntityFrame::InterpolatedEntityFrame(
    const double sample_seconds,
    const double previous_seconds,
    const double current_seconds,
    const std::uint32_t previous_snapshot_reference,
    const std::uint32_t current_snapshot_reference, const double alpha,
    const EntitySnapshotPairSelectionStatus selection_status,
    std::vector<InterpolatedEntityState> entities,
    const EntityInterpolationStatistics statistics,
    const EntityInterpolationEvidenceProfile evidence_profile) noexcept
    : sample_seconds_{sample_seconds}, previous_seconds_{previous_seconds},
      current_seconds_{current_seconds},
      previous_snapshot_reference_{previous_snapshot_reference},
      current_snapshot_reference_{current_snapshot_reference}, alpha_{alpha},
      selection_status_{selection_status}, entities_{std::move(entities)},
      statistics_{statistics}, evidence_profile_{evidence_profile}
{
}

double InterpolatedEntityFrame::sample_seconds() const noexcept
{
    return sample_seconds_;
}

double InterpolatedEntityFrame::previous_seconds() const noexcept
{
    return previous_seconds_;
}

double InterpolatedEntityFrame::current_seconds() const noexcept
{
    return current_seconds_;
}

std::uint32_t
InterpolatedEntityFrame::previous_snapshot_reference() const noexcept
{
    return previous_snapshot_reference_;
}

std::uint32_t
InterpolatedEntityFrame::current_snapshot_reference() const noexcept
{
    return current_snapshot_reference_;
}

double InterpolatedEntityFrame::alpha() const noexcept
{
    return alpha_;
}

EntitySnapshotPairSelectionStatus
InterpolatedEntityFrame::selection_status() const noexcept
{
    return selection_status_;
}

std::span<const InterpolatedEntityState>
InterpolatedEntityFrame::entities() const noexcept
{
    return entities_;
}

const EntityInterpolationStatistics&
InterpolatedEntityFrame::statistics() const noexcept
{
    return statistics_;
}

EntityInterpolationEvidenceProfile
InterpolatedEntityFrame::evidence_profile() const noexcept
{
    return evidence_profile_;
}

InterpolatedEntityFrameResult EntitySnapshotInterpolator::interpolate(
    const EntitySnapshotPairSelection& selection,
    const EntityInterpolationProjectionFrame& previous_projection,
    const EntityInterpolationProjectionFrame& current_projection,
    const EntityInterpolationLimits& limits) const
{
    if (!valid_entity_interpolation_limits(limits) ||
        !coherent_pair_selection(selection, limits) ||
        selection.evidence_profile != EntityInterpolationEvidenceProfile::
                                          synthetic_explicit_projection_v1) {
        return fail_frame(EntityInterpolationErrorCode::invalid_configuration,
                          "Pair selection and interpolation limits must be "
                          "valid and synthetic");
    }
    if (selection.previous->compatibility_profile() !=
            EntitySnapshotCompatibilityProfile::synthetic_neutral_v1 ||
        selection.current->compatibility_profile() !=
            EntitySnapshotCompatibilityProfile::synthetic_neutral_v1) {
        return fail_frame(EntityInterpolationErrorCode::evidence_pending,
                          "Stock entity projection remains evidence-pending");
    }
    if (const auto error = validate_projection(*selection.previous,
                                               previous_projection, limits)) {
        return {std::nullopt, std::move(*error)};
    }
    if (const auto error = validate_projection(*selection.current,
                                               current_projection, limits)) {
        return {std::nullopt, std::move(*error)};
    }

    try {
        const bool held = selection.previous == selection.current;
        const bool at_current = selection.alpha == 1.0;
        std::vector<InterpolatedEntityState> result;
        result.reserve(std::min<std::size_t>(
            limits.maximum_entities, previous_projection.entities.size() +
                                         current_projection.entities.size()));
        EntityInterpolationStatistics statistics;

        auto append = [&](SyntheticEntityInterpolationState state,
                          const InterpolatedEntityClass entity_class) -> bool {
            if (result.size() >= limits.maximum_entities) {
                return false;
            }
            if (state.inert_event_count >
                limits.maximum_events - std::min(statistics.inert_event_count,
                                                 limits.maximum_events)) {
                return false;
            }
            statistics.inert_event_count += state.inert_event_count;
            auto entity =
                InterpolatedEntityState{std::move(state), entity_class};
            result.push_back(std::move(entity));
            return true;
        };

        if (held) {
            for (const auto& state : current_projection.entities) {
                if (!append(state, InterpolatedEntityClass::held)) {
                    return fail_frame(
                        statistics.inert_event_count >= limits.maximum_events
                            ? EntityInterpolationErrorCode::event_limit_exceeded
                            : EntityInterpolationErrorCode::
                                  entity_limit_exceeded,
                        "Held interpolation frame exceeds its configured "
                        "limits",
                        selection.current->reference().value(),
                        state.entity_number);
                }
                ++statistics.held_count;
            }
        } else {
            std::size_t previous_index = 0U;
            std::size_t current_index = 0U;
            while (previous_index < previous_projection.entities.size() ||
                   current_index < current_projection.entities.size()) {
                const bool has_previous =
                    previous_index < previous_projection.entities.size();
                const bool has_current =
                    current_index < current_projection.entities.size();
                const auto previous_number =
                    has_previous ? previous_projection.entities[previous_index]
                                       .entity_number
                                 : std::numeric_limits<std::uint32_t>::max();
                const auto current_number =
                    has_current ? current_projection.entities[current_index]
                                      .entity_number
                                : std::numeric_limits<std::uint32_t>::max();

                if (has_previous &&
                    (!has_current || previous_number < current_number)) {
                    const auto& previous =
                        previous_projection.entities[previous_index++];
                    ++statistics.removed_count;
                    if (!at_current &&
                        !append(previous,
                                InterpolatedEntityClass::previous_only)) {
                        return fail_frame(
                            EntityInterpolationErrorCode::entity_limit_exceeded,
                            "Previous-only entity exceeds the result bound",
                            selection.previous->reference().value(),
                            previous.entity_number);
                    }
                    continue;
                }
                if (has_current &&
                    (!has_previous || current_number < previous_number)) {
                    const auto& current =
                        current_projection.entities[current_index++];
                    ++statistics.added_count;
                    if (at_current &&
                        !append(current,
                                InterpolatedEntityClass::current_only)) {
                        return fail_frame(
                            EntityInterpolationErrorCode::entity_limit_exceeded,
                            "Current-only entity exceeds the result bound",
                            selection.current->reference().value(),
                            current.entity_number);
                    }
                    continue;
                }

                const auto& previous =
                    previous_projection.entities[previous_index++];
                const auto& current =
                    current_projection.entities[current_index++];
                const bool requires_step =
                    current.mode != EntityInterpolationMode::interpolate ||
                    previous.model_reference != current.model_reference ||
                    previous.discrete.sequence_index !=
                        current.discrete.sequence_index;
                if (requires_step) {
                    auto state = at_current ? current : previous;
                    state.mode = current.mode;
                    if (!at_current) {
                        state.inert_event_count = 0U;
                    }
                    if (!append(std::move(state),
                                InterpolatedEntityClass::stepped)) {
                        return fail_frame(
                            EntityInterpolationErrorCode::entity_limit_exceeded,
                            "Stepped entity exceeds the result bound",
                            selection.current->reference().value(),
                            current.entity_number);
                    }
                    ++statistics.stepped_count;
                    continue;
                }

                auto state = previous;
                state.position = lerp_vector(previous.position,
                                             current.position, selection.alpha);
                state.angles_degrees =
                    interpolate_angles(previous.angles_degrees,
                                       current.angles_degrees, selection.alpha);
                state.scale =
                    lerp_vector(previous.scale, current.scale, selection.alpha);
                state.studio_frame_coordinate = lerp_float(
                    previous.studio_frame_coordinate,
                    current.studio_frame_coordinate, selection.alpha);
                state.mode = current.mode;
                state.discrete =
                    at_current ? current.discrete : previous.discrete;
                state.model_reference = at_current
                    ? current.model_reference
                    : previous.model_reference;
                state.controller_values = at_current
                    ? current.controller_values
                    : previous.controller_values;
                state.blending_values = at_current
                    ? current.blending_values
                    : previous.blending_values;
                state.mouth_value =
                    at_current ? current.mouth_value : previous.mouth_value;
                state.render_amount = at_current
                    ? current.render_amount
                    : previous.render_amount;
                state.render_color = at_current
                    ? current.render_color
                    : previous.render_color;
                state.animation_start_time_seconds = at_current
                    ? current.animation_start_time_seconds
                    : previous.animation_start_time_seconds;
                state.inert_event_count =
                    at_current ? current.inert_event_count : 0U;
                if (!position_within_limit(state.position,
                                           limits.maximum_position_magnitude) ||
                    !finite_vector(state.angles_degrees) ||
                    !scale_within_limit(state.scale, limits.maximum_scale) ||
                    !std::isfinite(state.studio_frame_coordinate)) {
                    return fail_frame(
                        EntityInterpolationErrorCode::non_finite_result,
                        "Interpolated transform is non-finite or exceeds its "
                        "bounds",
                        selection.current->reference().value(),
                        current.entity_number);
                }
                if (!append(std::move(state),
                            InterpolatedEntityClass::interpolated)) {
                    return fail_frame(
                        EntityInterpolationErrorCode::entity_limit_exceeded,
                        "Interpolated entity exceeds the result bound",
                        selection.current->reference().value(),
                        current.entity_number);
                }
                ++statistics.interpolated_count;
            }
        }

        std::size_t entity_bytes = 0U;
        std::size_t event_bytes = 0U;
        std::size_t total_bytes = 0U;
        if (!checked_multiply(result.size(), sizeof(InterpolatedEntityState),
                              entity_bytes) ||
            !checked_multiply(statistics.inert_event_count,
                              sizeof(std::uint32_t), event_bytes) ||
            !checked_add(entity_bytes, event_bytes, total_bytes) ||
            total_bytes > limits.maximum_result_bytes) {
            return fail_frame(
                EntityInterpolationErrorCode::result_byte_limit_exceeded,
                "Interpolated frame exceeds its accounted byte limit");
        }
        statistics.entity_count = result.size();
        statistics.accounted_result_bytes = total_bytes;
        return {
            InterpolatedEntityFrame{
                selection.target_seconds,
                selection.previous_seconds,
                selection.current_seconds,
                selection.previous->reference().value(),
                selection.current->reference().value(),
                selection.alpha,
                selection.status,
                std::move(result),
                statistics,
                selection.evidence_profile,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail_frame(EntityInterpolationErrorCode::unable_to_retain_result,
                          "Unable to allocate the interpolated frame");
    } catch (const std::length_error&) {
        return fail_frame(EntityInterpolationErrorCode::unable_to_retain_result,
                          "Interpolated frame container length is invalid");
    }
}

std::uint32_t runtime_animation_only_effect_mask(const RuntimeEntityInterpolationOptions& options,
    const std::uint32_t entity,const std::size_t ordinal) noexcept {
    const auto key=std::pair{entity,ordinal};
    const auto found=std::lower_bound(options.animation_only_effect_exceptions.begin(),
        options.animation_only_effect_exceptions.end(),key,[](const auto& row,const auto& value) {
            return std::pair{row.entity,row.record_ordinal}<value;
        });
    return found!=options.animation_only_effect_exceptions.end() && found->entity==entity &&
        found->record_ordinal==ordinal ? found->ignored_effect_mask : 0U;
}

namespace {
[[nodiscard]] bool bounded_runtime_origin(const client::RuntimeVector3Observation& value,
    const double maximum_position) noexcept {
    const auto component=[&](const std::optional<double>& v) {
        return !v || (std::isfinite(*v) && std::abs(*v)<=maximum_position);
    };
    return component(value.x) && component(value.y) && component(value.z);
}

// The buffered presentation owners need the identical input and working-set
// checks, but no validation-only copy of every entity, HUD row and life event.
// Keep this shared with the public interpolator so validation never drifts.
[[nodiscard]] std::optional<EntityInterpolationError> validate_runtime_interpolation(
    const client::RuntimeClientObservationState& previous,
    const client::RuntimeClientObservationState& current, const double target,
    const RuntimeEntityInterpolationOptions& options)
{
    const auto fail = [](const EntityInterpolationErrorCode code) {
        return EntityInterpolationError{code, {}, {}, "Runtime presentation sample rejected"};
    };
    const auto& limits = options.limits;
    if (!valid_entity_interpolation_limits(limits) || !std::isfinite(target) ||
        options.animation_only_effect_exceptions.size()>RuntimePlayerSnapshotPresentation::maximum_history*
            RuntimePlayerSnapshotPresentation::maximum_players ||
        std::ranges::any_of(options.animation_only_effect_exceptions,[&](const auto& row) {
            return row.entity==0U || row.entity>RuntimePlayerSnapshotPresentation::maximum_players ||
                row.record_ordinal==0U || row.ignored_effect_mask==0U ||
                (row.ignored_effect_mask&~options.no_interpolation_effect_mask)!=0U;
        }) ||
        std::adjacent_find(options.animation_only_effect_exceptions.begin(),options.animation_only_effect_exceptions.end(),
            [](const auto& a,const auto& b){return std::pair{a.entity,a.record_ordinal}>=std::pair{b.entity,b.record_ordinal};})
            !=options.animation_only_effect_exceptions.end() ||
        options.ordinary_entities.size() > limits.maximum_entities ||
        (!options.ordinary_entities.empty() && options.ordinary_entities.front() == 0U) ||
        std::adjacent_find(options.ordinary_entities.begin(), options.ordinary_entities.end(),
            [](auto a, auto b) { return a >= b; }) != options.ordinary_entities.end() ||
        (options.teleport_distance && (!std::isfinite(*options.teleport_distance) ||
                                      *options.teleport_distance <= 0.0)) ||
        (options.ordinary_teleport_distance && (!std::isfinite(*options.ordinary_teleport_distance) ||
                                               *options.ordinary_teleport_distance <= 0.0))) {
        return fail(EntityInterpolationErrorCode::invalid_configuration);
    }
    if (!client::valid_runtime_observation(previous) ||
        (&previous!=&current && !client::valid_runtime_observation(current)) ||
        !current.server_time_seconds) {
        return fail(EntityInterpolationErrorCode::invalid_projection);
    }
    if (current.packet_entities.size() > limits.maximum_entities ||
        previous.packet_entities.size() > limits.maximum_entities) {
        return fail(EntityInterpolationErrorCode::entity_limit_exceeded);
    }
    std::size_t bytes = sizeof(current), count_bytes{};
    const auto account = [&](const std::size_t count, const std::size_t size) {
        return checked_multiply(count, size, count_bytes) &&
               checked_add(bytes, count_bytes, bytes) && bytes <= limits.maximum_result_bytes;
    };
    if (!account(current.packet_entities.size(), sizeof(client::RuntimePacketEntityObservation)) ||
        !account(current.weapon_slots.size(), sizeof(client::RuntimeWeaponSlotObservation)) ||
        !account(current.life_events.size(), sizeof(client::RuntimeLifeEvent)) ||
        !account(current.weapon_hud.catalogue.size(), sizeof(client::RuntimeWeaponTypeObservation))) {
        return fail(EntityInterpolationErrorCode::result_byte_limit_exceeded);
    }
    for (const auto& entry : current.weapon_hud.catalogue) {
        if (!account(entry.command_name.size(), 1U)) return fail(EntityInterpolationErrorCode::result_byte_limit_exceeded);
    }
    for (const auto& event : current.life_events) {
        if (!account(event.weapon_text.size(), 1U)) return fail(EntityInterpolationErrorCode::result_byte_limit_exceeded);
    }
    for (const auto& entity : current.packet_entities) {
        if (!bounded_runtime_origin(entity.origin,limits.maximum_position_magnitude))
            return fail(EntityInterpolationErrorCode::position_limit_exceeded);
    }
    const double latest = *current.server_time_seconds;
    const bool new_generation = previous.generation != current.generation || !previous.server_time_seconds;
    const double oldest = new_generation ? latest : *previous.server_time_seconds;
    if (oldest > latest) return fail(EntityInterpolationErrorCode::invalid_snapshot_time_order);
    return {};
}
} // namespace

RuntimeEntityInterpolationResult EntitySnapshotInterpolator::interpolate_runtime(
    const client::RuntimeClientObservationState& previous,
    const client::RuntimeClientObservationState& current, const double target,
    const RuntimeEntityInterpolationOptions& options) const
{
    const auto fail = [](const EntityInterpolationErrorCode code) {
        RuntimeEntityInterpolationResult result;
        result.error = EntityInterpolationError{code, {}, {}, "Runtime presentation sample rejected"};
        return result;
    };
    if(auto error=validate_runtime_interpolation(previous,current,target,options)) {
        RuntimeEntityInterpolationResult result;result.error=std::move(error);return result;
    }
    const auto& limits=options.limits;
    const double latest=*current.server_time_seconds;
    const bool new_generation=previous.generation!=current.generation || !previous.server_time_seconds;
    const double oldest=new_generation ? latest : *previous.server_time_seconds;
    const bool held = new_generation || latest == oldest ||
                      latest - oldest > limits.maximum_snapshot_gap_seconds;
    try {
        RuntimeEntityInterpolationResult result;
        result.sample_seconds = held ? latest : std::clamp(target, oldest, latest);
        result.alpha = held ? 1.0 : (result.sample_seconds - oldest) / (latest - oldest);
        result.selection_status = held ? EntitySnapshotPairSelectionStatus::held_only :
            target < oldest ? EntitySnapshotPairSelectionStatus::held_oldest :
            target > latest ? EntitySnapshotPairSelectionStatus::held_newest :
            result.alpha == 0.0 ? EntitySnapshotPairSelectionStatus::exact_previous :
            result.alpha == 1.0 ? EntitySnapshotPairSelectionStatus::exact_current :
            EntitySnapshotPairSelectionStatus::bracketed;
        auto sampled = std::make_shared<client::RuntimeClientObservationState>(current);
        std::size_t old_index{};
        for (auto& entity : sampled->packet_entities) {
            // Non-player visuals opt in by neutral identity; no model filename,
            // projectile simulation, or brush ownership is inferred here.
            const bool ordinary = !entity.player_movement_schema && entity.ordinary_visual_schema &&
                std::binary_search(options.ordinary_entities.begin(), options.ordinary_entities.end(), entity.entity_number);
            if (!entity.player_movement_schema && !ordinary) continue;
            while (old_index < previous.packet_entities.size() &&
                   previous.packet_entities[old_index].entity_number < entity.entity_number) ++old_index;
            const auto* old = old_index < previous.packet_entities.size() &&
                              previous.packet_entities[old_index].entity_number == entity.entity_number
                                  ? &previous.packet_entities[old_index] : nullptr;
            bool reset = held || options.no_interpolation || !old ||
                         std::ranges::find(options.discontinuous_entities,entity.entity_number)!=options.discontinuous_entities.end() ||
                         (entity.effects.value_or(0U) & options.no_interpolation_effect_mask &
                          ~(entity.player_movement_schema && current.entity_metadata.source ?
                            runtime_animation_only_effect_mask(options,entity.entity_number,
                                current.entity_metadata.source->record_ordinal) : 0U)) != 0U;
            if (old) {
                if (!bounded_runtime_origin(old->origin,limits.maximum_position_magnitude))
                    return fail(EntityInterpolationErrorCode::position_limit_exceeded);
                reset = reset || old->model_index != entity.model_index ||
                        old->player_movement_schema != entity.player_movement_schema ||
                        old->ordinary_visual_schema != entity.ordinary_visual_schema;
                if (ordinary) reset = reset || old->sequence != entity.sequence ||
                    old->brush_move_type != entity.brush_move_type;
                const auto distance_limit = ordinary && options.ordinary_teleport_distance ?
                    options.ordinary_teleport_distance : options.teleport_distance;
                if (distance_limit && old->origin.complete() && entity.origin.complete()) {
                    reset = reset || std::hypot(*entity.origin.x - *old->origin.x,
                        *entity.origin.y - *old->origin.y, *entity.origin.z - *old->origin.z) > *distance_limit;
                }
            }
            if (reset) { ++result.reset_count; continue; }
            if (result.alpha >= 1.0) continue;
            const auto linear = [&](std::optional<double>& now, const std::optional<double>& before) {
                if (now && before) now = *before + (*now - *before) * result.alpha;
            };
            linear(entity.origin.x, old->origin.x); linear(entity.origin.y, old->origin.y); linear(entity.origin.z, old->origin.z);
            const auto angle = [&](std::optional<double>& now, const std::optional<double>& before) {
                if (now && before) now = *before + shortest_degree_delta(*before, *now) * result.alpha;
            };
            angle(entity.angles.x, old->angles.x); angle(entity.angles.y, old->angles.y); angle(entity.angles.z, old->angles.z);
            const auto bytes_lerp = [&](auto& now, const auto& before) {
                for (std::size_t i = 0; i < now.size(); ++i) if (now[i] && before[i])
                    now[i] = static_cast<std::uint32_t>(std::lround(
                        static_cast<double>(*before[i]) + (static_cast<double>(*now[i]) - *before[i]) * result.alpha));
            };
            bytes_lerp(entity.controllers, old->controllers); bytes_lerp(entity.blending, old->blending);
            const auto step = [](auto& now, const auto& before) { if (now && before) now = before; };
            step(entity.sequence, old->sequence); step(entity.body, old->body); step(entity.skin, old->skin);
            if (ordinary) linear(entity.frame, old->frame);
            else step(entity.frame, old->frame);
            step(entity.animation_time_seconds, old->animation_time_seconds);
            step(entity.frame_rate, old->frame_rate); step(entity.gait_sequence, old->gait_sequence);
            step(entity.weapon_model_index, old->weapon_model_index);
            ++result.interpolated_count;
        }
        sampled->canonical_state_hash = client::runtime_observation_canonical_hash(*sampled);
        if(!client::valid_runtime_observation(*sampled)) return fail(EntityInterpolationErrorCode::non_finite_result);
        result.state = std::move(sampled);
        return result;
    } catch (const std::bad_alloc&) { return fail(EntityInterpolationErrorCode::unable_to_retain_result); }
      catch (const std::length_error&) { return fail(EntityInterpolationErrorCode::unable_to_retain_result); }
}

void RuntimeOrdinarySnapshotPresentation::reset() noexcept {
    history_.clear(); lifetimes_.clear();
    application_anchor_.reset(); server_anchor_.reset(); last_application_.reset(); cadence_delay_.reset();
}

void RuntimeOrdinarySnapshotPresentation::observe(
    std::shared_ptr<const client::RuntimeClientObservationState> observation) {
    if (!observation || !observation->entity_metadata.source ||
        !client::valid_runtime_observation(*observation)) return;
    if (!observation->server_time_seconds) {
        // An otherwise valid newer entity publication can authoritatively
        // remove/reuse a slot without supplying a render clock. Keep its
        // current discrete publication, but never bridge unknown continuity.
        // Invalid, duplicate and older untimed input cannot erase a live sample.
        if (history_.empty() || history_.back()->generation<observation->generation ||
            (history_.back()->generation==observation->generation &&
             history_.back()->entity_metadata.source->record_ordinal<
                observation->entity_metadata.source->record_ordinal)) reset();
        return;
    }
    if (!history_.empty()) {
        const auto& previous=*history_.back();
        if (previous.generation==observation->generation &&
            previous.entity_metadata.source==observation->entity_metadata.source) return;
        if (previous.generation!=observation->generation ||
            *previous.server_time_seconds>=*observation->server_time_seconds ||
            previous.entity_metadata.source->record_ordinal>=observation->entity_metadata.source->record_ordinal)
            reset(); // Same/backward clocks or source ordering are not a new interpolation interval.
    }
    auto retained=history_;
    if (retained.size()==maximum_history) retained.erase(retained.begin());
    retained.push_back(std::move(observation));
    history_=std::move(retained);
}

RuntimeEntityInterpolationResult RuntimeOrdinarySnapshotPresentation::sample(
    const client::RuntimeClientObservationState& current, double application_seconds,
    const RuntimeEntityInterpolationOptions& options, double selected_delay) {
    const auto fail=[](EntityInterpolationErrorCode code) {
        RuntimeEntityInterpolationResult result;
        result.error=EntityInterpolationError{code,{}, {},"Ordinary presentation sample rejected"};
        return result;
    };
    if (!std::isfinite(application_seconds) || !std::isfinite(selected_delay) || selected_delay<0.0 ||
        selected_delay>maximum_delay_seconds || selected_delay>options.limits.maximum_snapshot_gap_seconds)
        return fail(EntityInterpolationErrorCode::invalid_configuration);
    // With no history the original discrete result is still the public result.
    // Otherwise validate without allocating a full state that would be discarded.
    if(history_.empty())return EntitySnapshotInterpolator{}.interpolate_runtime(current,current,
        current.server_time_seconds.value_or(0.0),options);
    RuntimeEntityInterpolationResult result;
    if(auto error=validate_runtime_interpolation(current,current,
            current.server_time_seconds.value_or(0.0),options)) {
        result.error=std::move(error);return result;
    }
    const auto& latest=*history_.back();
    if (latest.generation!=current.generation || latest.entity_metadata.source!=current.entity_metadata.source)
        return fail(EntityInterpolationErrorCode::invalid_projection);
    try {
        auto history=history_;
        auto lifetimes=lifetimes_;
        auto app_anchor=application_anchor_, server_anchor=server_anchor_, cadence=cadence_delay_;
        const double latest_seconds=*latest.server_time_seconds;
        const auto gap_limit=options.limits.maximum_snapshot_gap_seconds;
        bool clock_reset=!app_anchor || !server_anchor;
        // A backwards render clock holds the last sample; it cannot replay or rewind.
        const double app=last_application_ ? std::max(application_seconds,*last_application_) : application_seconds;
        const double pair_gap=history.size()>1U ? latest_seconds-*history[history.size()-2U]->server_time_seconds : 0.0;
        const bool source_gap=pair_gap>gap_limit;
        clock_reset=clock_reset || source_gap || (last_application_ && app-*last_application_>gap_limit);
        if (!clock_reset) {
            const auto mapped=*server_anchor+(app-*app_anchor);
            clock_reset=!std::isfinite(mapped) || std::abs(mapped-latest_seconds)>gap_limit;
        }
        if (clock_reset) {
            app_anchor=app;server_anchor=latest_seconds;
            if (application_anchor_) lifetimes.clear();
            if (source_gap) {history={history.back()};cadence.reset();}
        }
        if (!cadence && pair_gap>0.0 && !source_gap) cadence=std::min(selected_delay,pair_gap);
        // One observed cadence, rather than a full100ms spawn delay. The actual
        // retained time span is also a bound when RX cadence exceeds sixteen slots.
        const double delay=std::min({selected_delay,cadence.value_or(0.0),
            latest_seconds-*history.front()->server_time_seconds});
        const double mapped=*server_anchor+(app-*app_anchor);
        const double desired=std::min(latest_seconds,mapped-delay);
        if (!std::isfinite(desired)) return fail(EntityInterpolationErrorCode::non_finite_result);
        auto sampled=std::make_shared<client::RuntimeClientObservationState>(current);
        result.reset_count=0U;result.interpolated_count=0U;result.sample_seconds=desired;
        result.alpha=0.0;result.selection_status=EntitySnapshotPairSelectionStatus::held_newest;
        const auto find=[](const auto& state,std::uint32_t number) -> const client::RuntimePacketEntityObservation* {
            const auto row=std::lower_bound(state.packet_entities.begin(),state.packet_entities.end(),number,
                [](const auto& entity,auto id){return entity.entity_number<id;});
            return row!=state.packet_entities.end() && row->entity_number==number ? &*row : nullptr;
        };
        const auto discontinuity=[&](const auto& before,const auto& now) {
            if (before.model_index!=now.model_index || before.sequence!=now.sequence ||
                before.brush_move_type!=now.brush_move_type || before.ordinary_visual_schema!=now.ordinary_visual_schema ||
                before.player_movement_schema!=now.player_movement_schema ||
                !before.origin.complete() || !now.origin.complete() || !before.angles.complete() || !now.angles.complete() ||
                ((before.effects.value_or(0U)|now.effects.value_or(0U)) & options.no_interpolation_effect_mask)!=0U)
                return true;
            const auto distance_limit = options.ordinary_teleport_distance ?
                options.ordinary_teleport_distance : options.teleport_distance;
            return distance_limit && std::hypot(*now.origin.x-*before.origin.x,
                *now.origin.y-*before.origin.y,*now.origin.z-*before.origin.z)>*distance_limit;
        };
        const auto apply_pose=[](auto& destination,const auto& before,const auto& after,double alpha) {
            const auto linear=[&](auto& value,const auto& a,const auto& b) {
                if (value && a && b) value=*a+(*b-*a)*alpha;
            };
            linear(destination.origin.x,before.origin.x,after.origin.x);
            linear(destination.origin.y,before.origin.y,after.origin.y);
            linear(destination.origin.z,before.origin.z,after.origin.z);
            const auto angle=[&](auto& value,const auto& a,const auto& b) {
                if (value && a && b) value=*a+shortest_degree_delta(*a,*b)*alpha;
            };
            angle(destination.angles.x,before.angles.x,after.angles.x);
            angle(destination.angles.y,before.angles.y,after.angles.y);
            angle(destination.angles.z,before.angles.z,after.angles.z);
            // A wrapped frame cannot be interpolated backwards without imported
            // loop metadata; its current discrete value is preserved instead.
            if (destination.frame && before.frame && after.frame &&
                *before.frame<=*after.frame && *destination.frame>=*after.frame)
                linear(destination.frame,before.frame,after.frame);
            const auto bytes=[&](auto& output,const auto& a,const auto& b) {
                for(std::size_t i=0U;i<output.size();++i) if(output[i] && a[i] && b[i])
                    output[i]=static_cast<std::uint32_t>(std::lround(*a[i]+(static_cast<double>(*b[i])-*a[i])*alpha));
            };
            bytes(destination.controllers,before.controllers,after.controllers);
            bytes(destination.blending,before.blending,after.blending);
        };
        std::erase_if(lifetimes,[&](const auto& row) {
            return !std::binary_search(options.ordinary_entities.begin(),options.ordinary_entities.end(),row.first) ||
                !find(latest,row.first);
        });
        for (auto& entity:sampled->packet_entities) {
            if (entity.player_movement_schema || !entity.ordinary_visual_schema ||
                !std::binary_search(options.ordinary_entities.begin(),options.ordinary_entities.end(),entity.entity_number)) continue;
            const auto* committed=find(latest,entity.entity_number);
            if (!committed) continue;
            auto found=lifetimes.find(entity.entity_number);
            bool reset_life=options.no_interpolation || found==lifetimes.end() ||
                std::ranges::find(options.discontinuous_entities,entity.entity_number)!=options.discontinuous_entities.end() ||
                (committed->effects.value_or(0U)&options.no_interpolation_effect_mask)!=0U;
            if (!reset_life) {
                const auto& life=found->second;
                // If a whole unseen interval was evicted, absence/reuse cannot
                // be disproved. Do not bridge it merely because the slot matches.
                reset_life=life.record_ordinal<history.front()->entity_metadata.source->record_ordinal;
                auto before=&life.committed;
                double before_seconds=life.committed_seconds;
                for (const auto& snapshot:history) if (!reset_life && snapshot->entity_metadata.source->record_ordinal>life.record_ordinal) {
                    const auto* after=find(*snapshot,entity.entity_number);
                    if (!after || *snapshot->server_time_seconds-before_seconds>gap_limit || discontinuity(*before,*after)) {
                        reset_life=true;break;
                    }
                    before=after;before_seconds=*snapshot->server_time_seconds;
                }
            }
            if (reset_life) {
                lifetimes[entity.entity_number]={latest.entity_metadata.source->record_ordinal,latest_seconds,
                    latest_seconds,latest_seconds,app,*committed,*committed};
                ++result.reset_count;continue;
            }
            auto& life=found->second;
            // The stable session clock may lag a later birth by a valid phase
            // below the recovery threshold. Its newest birth floor would then
            // freeze the first pose until that whole phase elapsed. A lifetime
            // anchor advances from its own birth after only the observed cadence;
            // it never samples beyond the latest committed server observation.
            const double birth_desired=std::min(latest_seconds,life.birth_server_seconds+
                (app-life.birth_application_seconds)-delay);
            if (!std::isfinite(birth_desired)) return fail(EntityInterpolationErrorCode::non_finite_result);
            const double target=std::max({life.target_seconds,desired,birth_desired});
            // Startup/underflow holds the last authoritative rendered pose.
            // A newborn never jumps from a newest fallback into older history.
            if (target<=life.target_seconds) apply_pose(entity,life.rendered,life.rendered,0.0);
            else {
                const client::RuntimePacketEntityObservation* before=nullptr;const client::RuntimePacketEntityObservation* after=nullptr;
                double before_seconds{},after_seconds{};
                for (const auto& snapshot:history) {
                    const auto* row=find(*snapshot,entity.entity_number);if (!row) continue;
                    const auto time=*snapshot->server_time_seconds;
                    if (time<=target) {before=row;before_seconds=time;}
                    if (time>=target) {after=row;after_seconds=time;break;}
                }
                if (before && after && after_seconds-before_seconds<=gap_limit) {
                    const double alpha=after_seconds==before_seconds ? 1.0 : (target-before_seconds)/(after_seconds-before_seconds);
                    apply_pose(entity,*before,*after,alpha);life.target_seconds=target;
                    result.alpha=alpha;result.selection_status=EntitySnapshotPairSelectionStatus::bracketed;
                    ++result.interpolated_count;
                } else if (before && target>=latest_seconds) {
                    apply_pose(entity,*committed,*committed,0.0);life.target_seconds=latest_seconds;
                } else apply_pose(entity,life.rendered,life.rendered,0.0);
            }
            life.record_ordinal=latest.entity_metadata.source->record_ordinal;
            life.committed_seconds=latest_seconds;life.committed=*committed;life.rendered=entity;
        }
        sampled->canonical_state_hash=client::runtime_observation_canonical_hash(*sampled);
        if (!client::valid_runtime_observation(*sampled)) return fail(EntityInterpolationErrorCode::non_finite_result);
        for (const auto& entity:sampled->packet_entities)
            for(const auto* coordinate:{&entity.origin.x,&entity.origin.y,&entity.origin.z})
                if (*coordinate && std::abs(**coordinate)>options.limits.maximum_position_magnitude)
                    return fail(EntityInterpolationErrorCode::position_limit_exceeded);
        result.state=std::move(sampled);
        history_=std::move(history);lifetimes_=std::move(lifetimes);
        application_anchor_=app_anchor;server_anchor_=server_anchor;cadence_delay_=cadence;last_application_=app;
        return result;
    } catch (const std::bad_alloc&) {return fail(EntityInterpolationErrorCode::unable_to_retain_result);}
      catch (const std::length_error&) {return fail(EntityInterpolationErrorCode::unable_to_retain_result);}
}

void RuntimePlayerSnapshotPresentation::reset() noexcept {
    history_.clear();players_.clear();application_anchor_.reset();server_anchor_.reset();
    last_application_.reset();last_target_.reset();delay_.reset();
    pending_clock_reasons_=static_cast<std::uint32_t>(entity_visual::PlayerClockReason::explicit_reset);
}

void RuntimePlayerSnapshotPresentation::observe(
    std::shared_ptr<const client::RuntimeClientObservationState> value) {
    if (!value || !value->entity_metadata.source || !client::valid_runtime_observation(*value)) return;
    if (!history_.empty()) {
        const auto& old=*history_.back();
        if (value->generation<old.generation || (value->generation==old.generation &&
            value->entity_metadata.source->record_ordinal<=old.entity_metadata.source->record_ordinal)) return;
        if (value->generation!=old.generation || !value->server_time_seconds ||
            *value->server_time_seconds<*old.server_time_seconds) {
            std::uint32_t reason{};
            if(value->generation!=old.generation)reason|=static_cast<std::uint32_t>(entity_visual::PlayerClockReason::generation_changed);
            if(!value->server_time_seconds)reason|=static_cast<std::uint32_t>(entity_visual::PlayerClockReason::server_time_missing);
            else if(*value->server_time_seconds<*old.server_time_seconds)
                reason|=static_cast<std::uint32_t>(entity_visual::PlayerClockReason::server_time_regressed);
            reset();pending_clock_reasons_=reason;
        }
    }
    if (!value->server_time_seconds) {
        const auto reasons=pending_clock_reasons_;
        reset();pending_clock_reasons_=reasons|static_cast<std::uint32_t>(entity_visual::PlayerClockReason::server_time_missing);return;
    }
    // Equal-time publications are retained: removal/reuse and game continuity
    // boundaries must not disappear merely because no render occurred there.
    auto next=history_;
    if(next.size()==maximum_history)next.erase(next.begin());
    next.push_back(std::move(value));history_=std::move(next);
}

RuntimePlayerSnapshotPresentationResult RuntimePlayerSnapshotPresentation::sample(
    const client::RuntimeClientObservationState& current, const double application_seconds,
    const RuntimeEntityInterpolationOptions& options, const double selected_delay,
    const std::span<const std::uint32_t> selected,
    const std::span<const RuntimePlayerContinuityBoundary> boundaries) {
    const auto fail=[](EntityInterpolationErrorCode code) {
        RuntimePlayerSnapshotPresentationResult out;
        out.presentation.error=EntityInterpolationError{code,{}, {},"Buffered player presentation rejected"};return out;
    };
    if (!std::isfinite(application_seconds) || !std::isfinite(selected_delay) || selected_delay<0.0 ||
        selected_delay>maximum_delay_seconds || selected_delay>options.limits.maximum_snapshot_gap_seconds ||
        selected.size()>maximum_players || boundaries.size()>maximum_players ||
        (!selected.empty() && selected.front()==0U) ||
        std::adjacent_find(selected.begin(),selected.end(),[](auto a,auto b){return a>=b;})!=selected.end())
        return fail(EntityInterpolationErrorCode::invalid_configuration);
    const auto anchor_bytes=selected.size()*sizeof(RuntimePlayerInterpolationAnchor);
    if(anchor_bytes>=options.limits.maximum_result_bytes)
        return fail(EntityInterpolationErrorCode::result_byte_limit_exceeded);
    auto bounded_options=options;bounded_options.limits.maximum_result_bytes-=anchor_bytes;
    if(auto error=validate_runtime_interpolation(current,current,
            current.server_time_seconds.value_or(0.0),bounded_options)) {
        RuntimePlayerSnapshotPresentationResult out;out.presentation.error=std::move(error);return out;
    }
    if(history_.empty() || history_.back()->generation!=current.generation ||
        history_.back()->entity_metadata.source!=current.entity_metadata.source)
        return fail(EntityInterpolationErrorCode::invalid_projection);
    for(const auto& value:boundaries)if(!value.entity ||
        value.record_ordinal>current.entity_metadata.source->record_ordinal)
        return fail(EntityInterpolationErrorCode::invalid_configuration);
    try {
        using Continuity=entity_visual::PlayerContinuityReason;
        using Hold=entity_visual::PlayerHoldReason;
        using Clock=entity_visual::PlayerClockReason;
        using Decision=entity_visual::PlayerPresentationDecision;
        const auto bit=[](auto value){return static_cast<std::uint32_t>(value);};
        auto history=history_;auto tracks=players_;
        auto app_anchor=application_anchor_,server_anchor=server_anchor_,delay=delay_;
        const auto latest=history.back();const double latest_seconds=*latest->server_time_seconds;
        const auto raw_previous=history.size()>1U ? history[history.size()-2U] : latest;
        const double gap=latest_seconds-*raw_previous->server_time_seconds;
        const auto gap_limit=options.limits.maximum_snapshot_gap_seconds;
        const double app=last_application_ ? std::max(application_seconds,*last_application_) : application_seconds;
        bool reset_clock=!app_anchor || !server_anchor;
        std::uint32_t clock_reasons=pending_clock_reasons_;
        if(reset_clock)clock_reasons|=bit(Clock::initial_anchor);
        if (!reset_clock) {
            const auto mapped=*server_anchor+app-*app_anchor;
            if(gap>gap_limit)clock_reasons|=bit(Clock::snapshot_gap);
            if(last_application_ && app-*last_application_>gap_limit)clock_reasons|=bit(Clock::application_gap);
            if(!std::isfinite(mapped))clock_reasons|=bit(Clock::mapped_time_invalid);
            if(std::abs(mapped-latest_seconds)>gap_limit)clock_reasons|=bit(Clock::mapped_time_drift);
            reset_clock=gap>gap_limit || (last_application_ && app-*last_application_>gap_limit) ||
                !std::isfinite(mapped) || std::abs(mapped-latest_seconds)>gap_limit;
        }
        if(reset_clock) {
            app_anchor=app;server_anchor=latest_seconds;
            if(application_anchor_) {tracks.clear();history={latest};}
            delay=std::min(selected_delay,gap<=gap_limit ? gap : 0.0);
        } else if(history.size()>2U && last_application_) {
            // Local-compatible startup fill: slow only the render clock by at
            // most25% until its requested (<=100ms) reserve fits the history.
            // Never subtract a new packet's arrival phase or rewind a sample.
            const double available=latest_seconds-*history.front()->server_time_seconds;
            const double wanted=std::min(selected_delay,available);
            delay=std::min(wanted,delay.value_or(0.0)+.25*(app-*last_application_));
        }
        const double mapped=*server_anchor+app-*app_anchor;
        // Match the existing endpoint snapping tolerance below: cancellation
        // noise at an exact endpoint is not missing history coverage.
        if(*history.front()->server_time_seconds-(mapped-delay.value_or(0.0))>1e-9)
            clock_reasons|=bit(Clock::oldest_underflow);
        if(mapped-delay.value_or(0.0)-latest_seconds>1e-9)clock_reasons|=bit(Clock::newest_underflow);
        double target=std::clamp(mapped-delay.value_or(0.0),*history.front()->server_time_seconds,latest_seconds);
        if(!reset_clock && last_target_)target=std::max(target,std::min(*last_target_,latest_seconds));
        // Cancelling large application/server clock offsets can leave a sample
        // infinitesimally before an exact endpoint. Preserve discrete endpoint
        // identity without admitting a gameplay/collision epsilon.
        for(const auto& source:history)if(std::abs(target-*source->server_time_seconds)<=1e-9)
            target=*source->server_time_seconds;
        std::shared_ptr<const client::RuntimeClientObservationState> before=history.front(),after=history.back();
        for(const auto& source:history) {
            const auto seconds=*source->server_time_seconds;
            if(seconds<=target)before=source;
            if(seconds>target) {after=source;break;}
        }
        // At an exact newest/duplicate timestamp retain the actual preceding
        // interval when possible. Zero-time corrections themselves stay exact.
        if(before==after && history.size()>1U)before=history[history.size()-2U];
        for(std::size_t i=1;i<history.size();++i)if(*history[i]->server_time_seconds==target)
            {before=history[i-1];after=history[i];}
        auto mixed=EntitySnapshotInterpolator{}.interpolate_runtime(*before,*after,target,bounded_options);
        if(!mixed) {RuntimePlayerSnapshotPresentationResult out;out.presentation=std::move(mixed);return out;}
        auto output=std::make_shared<client::RuntimeClientObservationState>(current);
        RuntimePlayerSnapshotPresentationResult result;
        result.previous=before;result.current=after;result.presentation=mixed;
        result.players.reserve(selected.size());
        const auto find=[](const auto& source,std::uint32_t id) -> const client::RuntimePacketEntityObservation* {
            const auto row=std::lower_bound(source.packet_entities.begin(),source.packet_entities.end(),id,
                [](const auto& entity,auto number){return entity.entity_number<number;});
            return row!=source.packet_entities.end() && row->entity_number==id ? &*row : nullptr;
        };
        const auto motion_effects=[&](const auto& row,std::size_t ordinal) {
            return row.effects.value_or(0U)&options.no_interpolation_effect_mask&
                ~runtime_animation_only_effect_mask(options,row.entity_number,ordinal);
        };
        const auto boundary_reasons=[&](const auto& a,std::size_t a_ordinal,const auto& b,std::size_t b_ordinal) {
            std::uint32_t reasons{};
            if(a.model_index!=b.model_index)reasons|=bit(Continuity::model_changed);
            if(a.player_movement_schema!=b.player_movement_schema)reasons|=bit(Continuity::schema_changed);
            if((motion_effects(a,a_ordinal)|motion_effects(b,b_ordinal))!=0U)
                reasons|=bit(Continuity::nointerp_effect);
            if(options.teleport_distance && a.origin.complete() && b.origin.complete() &&
                std::hypot(*a.origin.x-*b.origin.x,*a.origin.y-*b.origin.y,*a.origin.z-*b.origin.z)>*options.teleport_distance)
                reasons|=bit(Continuity::teleport_distance);
            return reasons;
        };
        const auto boundary=[&](const auto& a,std::size_t a_ordinal,const auto& b,std::size_t b_ordinal) {
            return boundary_reasons(a,a_ordinal,b,b_ordinal)!=0U;
        };
        const auto pose=[](auto& destination,const auto& source) {
            const auto field=[](auto& to,const auto& from){if(to && from)to=from;};
            field(destination.origin.x,source.origin.x);field(destination.origin.y,source.origin.y);field(destination.origin.z,source.origin.z);
            field(destination.angles.x,source.angles.x);field(destination.angles.y,source.angles.y);field(destination.angles.z,source.angles.z);
            field(destination.sequence,source.sequence);field(destination.frame,source.frame);
            field(destination.body,source.body);field(destination.skin,source.skin);
            field(destination.animation_time_seconds,source.animation_time_seconds);field(destination.frame_rate,source.frame_rate);
            field(destination.gait_sequence,source.gait_sequence);field(destination.weapon_model_index,source.weapon_model_index);
            for(std::size_t i=0;i<destination.controllers.size();++i)field(destination.controllers[i],source.controllers[i]);
            for(std::size_t i=0;i<destination.blending.size();++i)field(destination.blending[i],source.blending[i]);
        };
        std::erase_if(tracks,[&](const auto& item){return !std::binary_search(selected.begin(),selected.end(),item.first) || !find(current,item.first);});
        bool common_bracket=false;
        for(auto& entity:output->packet_entities) {
            if(!entity.player_movement_schema || !std::binary_search(selected.begin(),selected.end(),entity.entity_number))continue;
            auto [entry,inserted]=tracks.try_emplace(entity.entity_number);
            auto& life=entry->second;
            const auto old_floor=life.floor;
            const auto old_floor_reasons=life.floor_reasons;
            std::uint32_t causes=inserted ? bit(Continuity::first_observation) : 0U;
            if(reset_clock)causes|=bit(Continuity::clock_reset);
            if(options.no_interpolation)causes|=bit(Continuity::no_interpolation_option);
            if(std::ranges::find(options.discontinuous_entities,entity.entity_number)!=options.discontinuous_entities.end())
                causes|=bit(Continuity::explicit_discontinuity);
            bool reset=options.no_interpolation ||
                std::ranges::find(options.discontinuous_entities,entity.entity_number)!=options.discontinuous_entities.end();
            if(!inserted && life.seen<history.front()->entity_metadata.source->record_ordinal) {
                life.floor=latest->entity_metadata.source->record_ordinal;reset=true;
                causes|=bit(Continuity::history_stale);
            }
            const client::RuntimePacketEntityObservation* prior=nullptr;
            bool observed_absence=false;
            double prior_time{};
            std::size_t prior_ordinal{};
            for(const auto& source:history) {
                const auto ordinal=source->entity_metadata.source->record_ordinal;
                const auto* row=find(*source,entity.entity_number);
                if(inserted && !life.floor && row)life.floor=ordinal;
                if(row && observed_absence && ordinal>life.seen)causes|=bit(Continuity::reentry);
                if(ordinal>life.seen && prior) {
                    if(!row || boundary(*prior,prior_ordinal,*row,ordinal)) {
                        life.floor=ordinal;reset=true;
                        causes|=!row ? bit(Continuity::removal) : boundary_reasons(*prior,prior_ordinal,*row,ordinal);
                    }
                    else if(*source->server_time_seconds==prior_time && prior->origin!=row->origin) {
                        life.floor=ordinal; // Exact position correction, not an animation-only publication.
                        causes|=bit(Continuity::same_time_origin_correction);
                    }
                }
                observed_absence=!row;
                prior=row;prior_time=*source->server_time_seconds;prior_ordinal=ordinal;
            }
            for(const auto& value:boundaries)if(value.entity==entity.entity_number && value.record_ordinal>life.floor)
                {life.floor=value.record_ordinal;reset=true;causes|=bit(Continuity::game_boundary);}
            if(reset)life.floor=std::max(life.floor,latest->entity_metadata.source->record_ordinal);
            if(life.floor!=old_floor)life.floor_reasons=causes;
            else life.floor_reasons|=causes;
            auto floor=latest;
            for(const auto& source:history)if(source->entity_metadata.source->record_ordinal>=life.floor) {floor=source;break;}
            const auto* born=find(*floor,entity.entity_number);
            // While held at an actual position/life boundary, consume a newer
            // same-position publication at that boundary's timestamp. This
            // preserves exact frame/anchor identity without advancing a
            // delayed actor merely because its animation was updated.
            for(const auto& source:history) if(source->entity_metadata.source->record_ordinal>
                floor->entity_metadata.source->record_ordinal &&
                *source->server_time_seconds==*floor->server_time_seconds) {
                const auto* row=find(*source,entity.entity_number);
                if(born && row && born->origin==row->origin && !boundary(*born,floor->entity_metadata.source->record_ordinal,
                    *row,source->entity_metadata.source->record_ordinal)) {floor=source;born=row;}
            }
            const auto* selected_pose=find(*mixed.state,entity.entity_number);
            RuntimePlayerInterpolationAnchor anchor{entity.entity_number,before,after,mixed.sample_seconds,reset};
            auto& diagnostic=anchor.diagnostics;
            diagnostic.floor_record_ordinal=life.floor;diagnostic.floor_reasons=life.floor_reasons;
            diagnostic.resolved_floor_record_ordinal=floor->entity_metadata.source->record_ordinal;
            diagnostic.new_boundary_reasons=life.floor!=old_floor ? causes : causes&~old_floor_reasons;
            diagnostic.clock_reasons=clock_reasons;
            if(!born)diagnostic.hold_reasons|=bit(Hold::floor_actor_missing);
            if(!selected_pose)diagnostic.hold_reasons|=bit(Hold::bracket_actor_missing);
            if(*floor->server_time_seconds>target)diagnostic.hold_reasons|=bit(Hold::target_before_floor);
            if(after->entity_metadata.source->record_ordinal<life.floor)diagnostic.hold_reasons|=bit(Hold::bracket_before_floor);
            if(selected_pose && selected_pose->model_index!=entity.model_index)diagnostic.hold_reasons|=bit(Hold::selected_model_mismatch);
            if(after->entity_metadata.source->record_ordinal<life.presented)diagnostic.hold_reasons|=bit(Hold::bracket_regression);
            if(!born || !selected_pose || *floor->server_time_seconds>target ||
                after->entity_metadata.source->record_ordinal<life.floor ||
                selected_pose->model_index!=entity.model_index || after->entity_metadata.source->record_ordinal<life.presented) {
                const auto* held=born ? born : &entity;
                pose(entity,*held);anchor.current=floor;anchor.previous=floor;
                anchor.sample_seconds=*floor->server_time_seconds;
                // Same-time updates preserve exact previous raw anchors at the
                // game API; render metadata alone is a held-current pair.
                if(floor==latest && raw_previous!=latest)anchor.previous=raw_previous;
                diagnostic.decision=born ? Decision::held_floor : Decision::held_current;
                diagnostic.position_previous_record_ordinal=diagnostic.position_current_record_ordinal=
                    (born ? floor : latest)->entity_metadata.source->record_ordinal;
            } else {
                pose(entity,*selected_pose);common_bracket=true;
                // Reconstruct only the already-selected neutral interpolation
                // decision. A global bracket can still hold one actor at its
                // newest row; it must not be reported as interpolated.
                const auto* older=find(*before,entity.entity_number);
                const auto* newer=find(*after,entity.entity_number);
                const bool held_interval=before->generation!=after->generation ||
                    *before->server_time_seconds==*after->server_time_seconds ||
                    *after->server_time_seconds-*before->server_time_seconds>gap_limit;
                const bool actor_reset=held_interval || options.no_interpolation || !older ||
                    std::ranges::find(options.discontinuous_entities,entity.entity_number)!=options.discontinuous_entities.end() ||
                    (newer && motion_effects(*newer,after->entity_metadata.source->record_ordinal)!=0U) ||
                    (older && newer && (older->model_index!=newer->model_index ||
                        older->player_movement_schema!=newer->player_movement_schema ||
                        older->ordinary_visual_schema!=newer->ordinary_visual_schema ||
                        (options.teleport_distance && older->origin.complete() && newer->origin.complete() &&
                            std::hypot(*older->origin.x-*newer->origin.x,*older->origin.y-*newer->origin.y,
                                *older->origin.z-*newer->origin.z)>*options.teleport_distance)));
                diagnostic.position_previous_record_ordinal=before->entity_metadata.source->record_ordinal;
                diagnostic.position_current_record_ordinal=after->entity_metadata.source->record_ordinal;
                diagnostic.alpha=mixed.alpha;
                if(actor_reset) {
                    diagnostic.hold_reasons|=bit(Hold::per_actor_reset);
                    diagnostic.decision=Decision::held_current;diagnostic.alpha=0.0;
                    diagnostic.position_previous_record_ordinal=diagnostic.position_current_record_ordinal;
                } else switch(mixed.selection_status) {
                case EntitySnapshotPairSelectionStatus::bracketed:diagnostic.decision=Decision::interpolated;break;
                case EntitySnapshotPairSelectionStatus::exact_previous:diagnostic.decision=Decision::exact_previous;break;
                case EntitySnapshotPairSelectionStatus::exact_current:diagnostic.decision=Decision::exact_current;break;
                case EntitySnapshotPairSelectionStatus::held_oldest:diagnostic.decision=Decision::held_oldest;break;
                case EntitySnapshotPairSelectionStatus::held_newest:diagnostic.decision=Decision::held_newest;break;
                case EntitySnapshotPairSelectionStatus::held_only:diagnostic.decision=Decision::held_only;break;
                }
            }
            life.seen=latest->entity_metadata.source->record_ordinal;
            life.presented=anchor.current->entity_metadata.source->record_ordinal;
            result.players.push_back(std::move(anchor));
        }
        if(!common_bracket && !result.players.empty()) {
            const auto& anchor=result.players.front();result.previous=anchor.current;result.current=anchor.current;
            result.presentation.sample_seconds=anchor.sample_seconds;result.presentation.alpha=0.0;
            result.presentation.selection_status=EntitySnapshotPairSelectionStatus::held_only;
        }
        output->canonical_state_hash=client::runtime_observation_canonical_hash(*output);
        if(!client::valid_runtime_observation(*output))return fail(EntityInterpolationErrorCode::non_finite_result);
        result.presentation.state=std::move(output);
        history_=std::move(history);players_=std::move(tracks);
        application_anchor_=app_anchor;server_anchor_=server_anchor;delay_=delay;
        last_application_=app;last_target_=target;
        pending_clock_reasons_=0U;
        return result;
    }catch(const std::bad_alloc&){return fail(EntityInterpolationErrorCode::unable_to_retain_result);}
     catch(const std::length_error&){return fail(EntityInterpolationErrorCode::unable_to_retain_result);}
}

} // namespace hlclient::goldsrc
