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

} // namespace hlclient::goldsrc
