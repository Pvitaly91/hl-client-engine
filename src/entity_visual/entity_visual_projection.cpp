#include <hlclient/entity_visual/entity_visual_projection.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::entity_visual {
namespace {

[[nodiscard]] std::string bounded_context(std::string_view context)
{
    return std::string{context.substr(
        0U, kEntityVisualProjectionDiagnosticTextLimit)};
}

[[nodiscard]] EntityVisualProjectionResult failure(
    const EntityVisualProjectionStatus status,
    const std::optional<std::uint32_t> entity_number,
    const std::string_view context)
{
    return EntityVisualProjectionResult{
        status, std::nullopt, entity_number, bounded_context(context)};
}

[[nodiscard]] bool finite(const EntityVisualVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finite(const EntityVisualRenderColor& value) noexcept
{
    return std::isfinite(value.red) && std::isfinite(value.green) &&
           std::isfinite(value.blue);
}

[[nodiscard]] bool within_magnitude(
    const EntityVisualVector3& value,
    const float maximum) noexcept
{
    return std::abs(value.x) <= maximum && std::abs(value.y) <= maximum &&
           std::abs(value.z) <= maximum;
}

template <std::size_t Size>
[[nodiscard]] bool finite_unit_values(
    const std::array<float, Size>& values) noexcept
{
    return std::ranges::all_of(values, [](const float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    });
}

[[nodiscard]] bool valid_render_mode(
    const EntityVisualRenderMode mode) noexcept
{
    switch (mode) {
    case EntityVisualRenderMode::source_asset_default:
    case EntityVisualRenderMode::opaque:
    case EntityVisualRenderMode::alpha_test:
    case EntityVisualRenderMode::additive:
        return true;
    }
    return false;
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

[[nodiscard]] EntityVisualProjectionStatus validate_input(
    const SyntheticEntityVisualInput& input,
    const EntityVisualProjectionLimits& limits) noexcept
{
    if (input.entity_number == 0U ||
        input.entity_number > limits.maximum_entity_number) {
        return EntityVisualProjectionStatus::invalid_entity_number;
    }
    if (input.model_reference.profile() !=
            EntityVisualModelReferenceProfile::
                synthetic_type_local_model_slot ||
        input.model_reference.value() > limits.maximum_model_slot) {
        return EntityVisualProjectionStatus::invalid_model_reference;
    }

    const auto origin = input.origin.value_or(EntityVisualVector3{});
    const auto angles = input.angles_degrees.value_or(EntityVisualVector3{});
    const auto color = input.render_color.value_or(EntityVisualRenderColor{});
    const auto frame = input.studio_frame_coordinate.value_or(0.0F);
    const auto amount = input.render_amount.value_or(1.0F);
    const auto scale = input.scale.value_or(1.0F);
    const auto mouth = input.mouth_value.value_or(0.0F);
    if (!finite(origin) || !finite(angles) || !finite(color) ||
        !std::isfinite(frame) || !std::isfinite(amount) ||
        !std::isfinite(scale) || !std::isfinite(mouth) ||
        (input.animation_start_time_seconds &&
         !std::isfinite(*input.animation_start_time_seconds)) ||
        (input.controller_values &&
         !std::ranges::all_of(*input.controller_values, [](const float value) {
             return std::isfinite(value);
         })) ||
        (input.blending_values &&
         !std::ranges::all_of(*input.blending_values, [](const float value) {
             return std::isfinite(value);
         }))) {
        return EntityVisualProjectionStatus::non_finite_value;
    }

    if (!within_magnitude(origin, limits.maximum_position_magnitude) ||
        !within_magnitude(angles, limits.maximum_angle_magnitude) ||
        color.red < 0.0F || color.red > 1.0F || color.green < 0.0F ||
        color.green > 1.0F || color.blue < 0.0F || color.blue > 1.0F ||
        frame < 0.0F || frame > limits.maximum_studio_frame_coordinate ||
        amount < 0.0F || amount > 1.0F || scale <= 0.0F ||
        scale > limits.maximum_scale || mouth < 0.0F || mouth > 1.0F ||
        input.sequence_index.value_or(0U) > limits.maximum_sequence_index ||
        input.body_value.value_or(0U) > limits.maximum_body_value ||
        input.skin_family_index.value_or(0U) >
            limits.maximum_skin_family_index ||
        input.sprite_frame_index.value_or(0U) >
            limits.maximum_sprite_frame_index ||
        (input.render_mode && !valid_render_mode(*input.render_mode)) ||
        (input.interpolation_mode &&
         !valid_interpolation_mode(*input.interpolation_mode)) ||
        (input.controller_values &&
         !finite_unit_values(*input.controller_values)) ||
        (input.blending_values && !finite_unit_values(*input.blending_values)) ||
        (input.animation_start_time_seconds &&
         std::abs(*input.animation_start_time_seconds) >
             limits.maximum_animation_time_seconds)) {
        return EntityVisualProjectionStatus::value_out_of_range;
    }
    return EntityVisualProjectionStatus::projected;
}

[[nodiscard]] bool synthetic_snapshot(
    const goldsrc::EntitySnapshotState& snapshot) noexcept
{
    return snapshot.compatibility_profile() ==
               goldsrc::EntitySnapshotCompatibilityProfile::
                   synthetic_neutral_v1 &&
           snapshot.evidence_profile() ==
               goldsrc::EntitySnapshotEvidenceProfile::
                   caller_supplied_typed_records;
}

} // namespace

EntityVisualModelReference::EntityVisualModelReference(
    const EntityVisualModelReferenceProfile profile,
    const std::uint32_t value) noexcept
    : profile_{profile}, value_{value}
{
}

EntityVisualModelReference EntityVisualModelReference::synthetic_model_slot(
    const std::uint32_t slot) noexcept
{
    return EntityVisualModelReference{
        EntityVisualModelReferenceProfile::synthetic_type_local_model_slot,
        slot};
}

EntityVisualModelReference
EntityVisualModelReference::stock_modelindex_evidence_pending(
    const std::uint32_t raw_value) noexcept
{
    return EntityVisualModelReference{
        EntityVisualModelReferenceProfile::
            stock_modelindex_mapping_evidence_pending,
        raw_value};
}

EntityVisualModelReferenceProfile EntityVisualModelReference::profile()
    const noexcept
{
    return profile_;
}

std::uint32_t EntityVisualModelReference::value() const noexcept
{
    return value_;
}

bool valid_entity_visual_projection_limits(
    const EntityVisualProjectionLimits& limits) noexcept
{
    return limits.maximum_entity_number > 0U &&
           limits.maximum_entity_number <=
               kHardMaximumSyntheticVisualEntityNumber &&
           limits.maximum_model_slot <= kHardMaximumSyntheticModelSlot &&
           limits.maximum_sequence_index <=
               kHardMaximumSyntheticSequenceIndex &&
           limits.maximum_body_value <= kHardMaximumSyntheticBodyValue &&
           limits.maximum_skin_family_index <=
               kHardMaximumSyntheticSkinFamilyIndex &&
           limits.maximum_sprite_frame_index <=
               kHardMaximumSyntheticSpriteFrameIndex &&
           std::isfinite(limits.maximum_position_magnitude) &&
           limits.maximum_position_magnitude > 0.0F &&
           limits.maximum_position_magnitude <=
               kHardMaximumSyntheticPositionMagnitude &&
           std::isfinite(limits.maximum_angle_magnitude) &&
           limits.maximum_angle_magnitude > 0.0F &&
           limits.maximum_angle_magnitude <=
               kHardMaximumSyntheticAngleMagnitude &&
           std::isfinite(limits.maximum_studio_frame_coordinate) &&
           limits.maximum_studio_frame_coordinate > 0.0F &&
           limits.maximum_studio_frame_coordinate <=
               kHardMaximumSyntheticStudioFrameCoordinate &&
           std::isfinite(limits.maximum_scale) && limits.maximum_scale > 0.0F &&
           limits.maximum_scale <= kHardMaximumSyntheticEntityScale &&
           std::isfinite(limits.maximum_animation_time_seconds) &&
           limits.maximum_animation_time_seconds > 0.0 &&
           limits.maximum_animation_time_seconds <=
               kHardMaximumSyntheticAnimationTimeSeconds &&
           limits.maximum_inputs > 0U &&
           limits.maximum_inputs <= kHardMaximumSyntheticVisualInputs;
}

EntityVisualProjectionState::EntityVisualProjectionState(
    const std::uint32_t entity_number,
    EntityVisualModelReference model_reference,
    const EntityVisualTransform transform,
    const EntityVisualStudioControls studio_controls,
    const EntityVisualSpriteControls sprite_controls,
    const EntityVisualRenderControls render_controls,
    const EntityInterpolationMode interpolation_mode,
    std::optional<double> animation_start_time_seconds,
    goldsrc::EntitySnapshotReference source_snapshot_reference) noexcept
    : entity_number_{entity_number},
      model_reference_{std::move(model_reference)},
      transform_{transform},
      studio_controls_{studio_controls},
      sprite_controls_{sprite_controls},
      render_controls_{render_controls},
      interpolation_mode_{interpolation_mode},
      animation_start_time_seconds_{animation_start_time_seconds},
      source_snapshot_reference_{std::move(source_snapshot_reference)}
{
}

std::uint32_t EntityVisualProjectionState::entity_number() const noexcept
{
    return entity_number_;
}

const EntityVisualModelReference& EntityVisualProjectionState::model_reference()
    const noexcept
{
    return model_reference_;
}

const EntityVisualTransform& EntityVisualProjectionState::transform()
    const noexcept
{
    return transform_;
}

const EntityVisualStudioControls&
EntityVisualProjectionState::studio_controls() const noexcept
{
    return studio_controls_;
}

const EntityVisualSpriteControls&
EntityVisualProjectionState::sprite_controls() const noexcept
{
    return sprite_controls_;
}

const EntityVisualRenderControls&
EntityVisualProjectionState::render_controls() const noexcept
{
    return render_controls_;
}

EntityInterpolationMode EntityVisualProjectionState::interpolation_mode()
    const noexcept
{
    return interpolation_mode_;
}

const std::optional<double>&
EntityVisualProjectionState::animation_start_time_seconds() const noexcept
{
    return animation_start_time_seconds_;
}

const goldsrc::EntitySnapshotReference&
EntityVisualProjectionState::source_snapshot_reference() const noexcept
{
    return source_snapshot_reference_;
}

EntityVisualProjectionCompatibilityProfile
EntityVisualProjectionState::compatibility_profile() const noexcept
{
    return EntityVisualProjectionCompatibilityProfile::
        synthetic_entity_visual_v1;
}

EntityVisualProjectionEvidenceProfile
EntityVisualProjectionState::evidence_profile() const noexcept
{
    return EntityVisualProjectionEvidenceProfile::
        caller_supplied_typed_synthetic_records;
}

SyntheticEntityVisualProjectionProvider::
    SyntheticEntityVisualProjectionProvider(
        std::vector<SyntheticEntityVisualInput> inputs,
        const EntityVisualProjectionLimits limits) noexcept
    : inputs_{std::move(inputs)}, limits_{limits}
{
}

SyntheticEntityVisualProjectionProviderCreateResult
SyntheticEntityVisualProjectionProvider::create(
    std::vector<SyntheticEntityVisualInput> inputs,
    const EntityVisualProjectionLimits limits) noexcept
{
    if (!valid_entity_visual_projection_limits(limits)) {
        return SyntheticEntityVisualProjectionProviderCreateResult{
            nullptr,
            EntityVisualProjectionStatus::invalid_configuration,
            std::nullopt,
            bounded_context("Invalid entity visual projection limits")};
    }
    if (inputs.size() > limits.maximum_inputs) {
        return SyntheticEntityVisualProjectionProviderCreateResult{
            nullptr,
            EntityVisualProjectionStatus::value_out_of_range,
            std::nullopt,
            bounded_context("Synthetic entity visual input limit exceeded")};
    }

    try {
        std::ranges::sort(inputs, {}, &SyntheticEntityVisualInput::entity_number);
        for (std::size_t index = 0U; index < inputs.size(); ++index) {
            const auto status = validate_input(inputs[index], limits);
            if (status != EntityVisualProjectionStatus::projected) {
                return SyntheticEntityVisualProjectionProviderCreateResult{
                    nullptr,
                    status,
                    inputs[index].entity_number,
                    bounded_context("Invalid typed synthetic visual input")};
            }
            if (index != 0U &&
                inputs[index - 1U].entity_number == inputs[index].entity_number) {
                return SyntheticEntityVisualProjectionProviderCreateResult{
                    nullptr,
                    EntityVisualProjectionStatus::duplicate_entity_input,
                    inputs[index].entity_number,
                    bounded_context("Duplicate typed synthetic visual input")};
            }
        }
        return SyntheticEntityVisualProjectionProviderCreateResult{
            std::unique_ptr<SyntheticEntityVisualProjectionProvider>{
                new SyntheticEntityVisualProjectionProvider{
                    std::move(inputs), limits}},
            std::nullopt,
            std::nullopt,
            {}};
    } catch (const std::bad_alloc&) {
        return SyntheticEntityVisualProjectionProviderCreateResult{
            nullptr,
            EntityVisualProjectionStatus::unable_to_retain_projection,
            std::nullopt,
            bounded_context("Unable to retain synthetic visual inputs")};
    } catch (...) {
        return SyntheticEntityVisualProjectionProviderCreateResult{
            nullptr,
            EntityVisualProjectionStatus::unable_to_retain_projection,
            std::nullopt,
            bounded_context("Unable to create synthetic projection provider")};
    }
}

EntityVisualProjectionResult SyntheticEntityVisualProjectionProvider::project(
    const goldsrc::EntitySnapshotState& snapshot,
    const goldsrc::EntitySnapshotEntityState& entity) const
{
    if (!synthetic_snapshot(snapshot)) {
        return failure(
            EntityVisualProjectionStatus::incompatible_snapshot_profile,
            entity.entity_number(),
            "Synthetic projection requires a synthetic snapshot profile");
    }
    if (snapshot.find_exact(entity.entity_number()) != &entity) {
        return failure(
            EntityVisualProjectionStatus::entity_snapshot_mismatch,
            entity.entity_number(),
            "Entity is not the exact state owned by the supplied snapshot");
    }
    const auto found = std::lower_bound(
        inputs_.begin(),
        inputs_.end(),
        entity.entity_number(),
        [](const SyntheticEntityVisualInput& candidate,
           const std::uint32_t entity_number) {
            return candidate.entity_number < entity_number;
        });
    if (found == inputs_.end() ||
        found->entity_number != entity.entity_number()) {
        return failure(
            EntityVisualProjectionStatus::missing_projection,
            entity.entity_number(),
            "No typed synthetic visual record exists for the entity");
    }

    try {
        EntityVisualProjectionState state{
            found->entity_number,
            found->model_reference,
            EntityVisualTransform{
                found->origin.value_or(EntityVisualVector3{}),
                found->angles_degrees.value_or(EntityVisualVector3{}),
                found->scale.value_or(1.0F)},
            EntityVisualStudioControls{
                found->sequence_index.value_or(0U),
                found->studio_frame_coordinate.value_or(0.0F),
                found->body_value.value_or(0U),
                found->skin_family_index.value_or(0U),
                found->controller_values.value_or(std::array<float, 4U>{}),
                found->blending_values.value_or(std::array<float, 2U>{}),
                found->mouth_value.value_or(0.0F)},
            EntityVisualSpriteControls{
                found->sprite_frame_index.value_or(0U)},
            EntityVisualRenderControls{
                found->render_mode.value_or(
                    EntityVisualRenderMode::source_asset_default),
                found->render_amount.value_or(1.0F),
                found->render_color.value_or(EntityVisualRenderColor{}),
                found->effects_metadata.value_or(0U)},
            found->interpolation_mode.value_or(
                EntityInterpolationMode::interpolate),
            found->animation_start_time_seconds,
            snapshot.reference()};
        return EntityVisualProjectionResult{
            EntityVisualProjectionStatus::projected,
            std::move(state),
            entity.entity_number(),
            {}};
    } catch (const std::bad_alloc&) {
        return failure(
            EntityVisualProjectionStatus::unable_to_retain_projection,
            entity.entity_number(),
            "Unable to retain entity visual projection");
    } catch (...) {
        return failure(
            EntityVisualProjectionStatus::unable_to_retain_projection,
            entity.entity_number(),
            "Unable to publish entity visual projection");
    }
}

std::span<const SyntheticEntityVisualInput>
SyntheticEntityVisualProjectionProvider::inputs() const noexcept
{
    return inputs_;
}

const EntityVisualProjectionLimits&
SyntheticEntityVisualProjectionProvider::limits() const noexcept
{
    return limits_;
}

EntityVisualProjectionResult
EvidencePendingStockEntityVisualProjectionProvider::project(
    const goldsrc::EntitySnapshotState& snapshot,
    const goldsrc::EntitySnapshotEntityState& entity) const
{
    if (snapshot.find_exact(entity.entity_number()) != &entity) {
        return failure(
            EntityVisualProjectionStatus::entity_snapshot_mismatch,
            entity.entity_number(),
            "Entity is not the exact state owned by the supplied snapshot");
    }
    return failure(
        EntityVisualProjectionStatus::visual_projection_evidence_pending,
        entity.entity_number(),
        "Stock Protocol 48 visual field semantics remain evidence pending");
}

} // namespace hlclient::entity_visual
