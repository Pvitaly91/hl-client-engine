#include <hlclient/goldsrc/studio/goldsrc_studio_entity_animation.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::studio {
namespace {
constexpr std::uint32_t kNoInterpolation = 32U;
constexpr double kTransitionSeconds = 0.2;

bool valid_time(double value) noexcept {
    return std::isfinite(value) && std::abs(value) <= 1.0e9;
}
bool valid_anchor(const client::RuntimePacketEntityObservation& entity,
                  const assets::SkeletalModelAssetData& model) noexcept {
    if (!entity.sequence || *entity.sequence >= model.sequences.size() || !entity.frame ||
        !std::isfinite(*entity.frame) || *entity.frame < 0.0 || *entity.frame > 256.0)
        return false;
    const auto& sequence = model.sequences[*entity.sequence];
    if (!sequence.frame_count || sequence.frame_count > 1'000'000U ||
        !std::isfinite(sequence.frames_per_second) || sequence.frames_per_second < 0.0F)
        return false;
    if (entity.animation_time_seconds && !valid_time(*entity.animation_time_seconds)) return false;
    if (entity.frame_rate && !std::isfinite(*entity.frame_rate)) return false;
    for (const auto& value : entity.controllers) if (value && *value > 255U) return false;
    for (const auto& value : entity.blending) if (value && *value > 255U) return false;
    return true;
}

std::optional<StudioPoseInput> frame_sample(const StudioEntityAnimationContext& input,
    const client::RuntimePacketEntityObservation& anchor, bool interpolate) noexcept {
    auto result = input.base;
    result.sequence_index = *anchor.sequence;
    const auto& sequence = input.model.sequences[result.sequence_index];
    const double last = static_cast<double>(sequence.frame_count - 1U);
    double frame = *anchor.frame * last / 256.0;
    // Valve SDK CStudioModelRenderer::StudioEstimateFrame. Missing negotiated
    // descriptors remain a discrete pose, never an invented default rate.
    if (interpolate && anchor.animation_time_seconds && anchor.frame_rate)
        frame += std::max(0.0, input.sample_server_seconds - *anchor.animation_time_seconds) *
            *anchor.frame_rate * sequence.frames_per_second;
    if (!std::isfinite(frame)) return {};
    if (last == 0.0) frame = 0.0;
    else if ((sequence.source_flags & 1U) != 0U) {
        frame = std::fmod(frame, last);
        if (frame < 0.0) frame += last;
    } else {
        // The SDK leaves a tiny fraction before the final key, preventing its
        // animation decoder from addressing a nonexistent following frame.
        frame = std::clamp(frame, 0.0, std::max(0.0, last - 0.001));
    }
    result.frame_coordinate = frame;
    for (std::size_t i = 0; i < result.controller_values.size(); ++i)
        if (anchor.controllers[i]) result.controller_values[i] = static_cast<std::uint8_t>(*anchor.controllers[i]);
    for (std::size_t i = 0; i < result.blending_values.size(); ++i)
        if (anchor.blending[i]) result.blending_values[i] = static_cast<std::uint8_t>(*anchor.blending[i]);
    return result;
}

void blend_channels(StudioPoseInput& sample, const StudioEntityAnimationContext& input) noexcept {
    const auto& before = *input.previous;
    const auto& after = input.current;
    const double alpha = std::clamp((input.sample_server_seconds - input.previous_server_seconds) /
        (input.current_server_seconds - input.previous_server_seconds), 0.0, 1.0);
    for (std::size_t i = 0; i < sample.controller_values.size(); ++i) {
        if (!before.controllers[i] || !after.controllers[i]) continue;
        double from = *before.controllers[i], delta = static_cast<double>(*after.controllers[i]) - from;
        // A controller byte only wraps if the imported controller declares
        // STUDIO_RLOOP. Linear controls and sequence blend values do not wrap.
        const bool circular = std::ranges::any_of(input.model.bone_controllers, [i](const auto& controller) {
            return controller.controller_index == static_cast<std::int32_t>(i) &&
                (controller.source_type & 0x8000U) != 0U;
        });
        if (circular && std::abs(delta) > 128.0) delta += delta > 0.0 ? -256.0 : 256.0;
        auto value = std::lround(from + delta * alpha);
        if (circular) value = (value % 256L + 256L) % 256L;
        sample.controller_values[i] = static_cast<std::uint8_t>(std::clamp(value, 0L, 255L));
    }
    for (std::size_t i = 0; i < sample.blending_values.size(); ++i) {
        if (!before.blending[i] || !after.blending[i]) continue;
        sample.blending_values[i] = static_cast<std::uint8_t>(std::clamp(std::lround(
            *before.blending[i] + (static_cast<double>(*after.blending[i]) - *before.blending[i]) * alpha), 0L, 255L));
    }
}
} // namespace

std::optional<StudioPoseCompositionInput> StudioEntityAnimationPresentation::sample(
    const StudioEntityAnimationContext& input) noexcept {
    try {
        const auto& entity = input.current;
        if (!input.generation || !input.model_resource_id || !input.record_ordinal ||
            !entity.entity_number || entity.player_movement_schema || !entity.model_index ||
            !valid_time(input.current_server_seconds) || !valid_time(input.sample_server_seconds) ||
            !std::isfinite(input.maximum_gap_seconds) || input.maximum_gap_seconds <= 0.0 ||
            input.maximum_gap_seconds > 2.0 ||
            std::abs(input.sample_server_seconds - input.current_server_seconds) > input.maximum_gap_seconds ||
            !valid_anchor(entity, input.model)) return {};
        const bool nointerp = input.no_interpolation || (entity.effects.value_or(0U) & kNoInterpolation) != 0U;
        const double interval = input.current_server_seconds - input.previous_server_seconds;
        const bool pair = !input.discontinuity && !nointerp && input.previous &&
            input.previous->entity_number == entity.entity_number &&
            input.previous->model_index == entity.model_index && !input.previous->player_movement_schema &&
            input.previous->brush_move_type == entity.brush_move_type &&
            (input.previous->effects.value_or(0U) & kNoInterpolation) == 0U &&
            valid_time(input.previous_server_seconds) && interval > 0.0 && interval <= input.maximum_gap_seconds &&
            valid_anchor(*input.previous, input.model);
        // A delayed render clock needs the earlier real animation anchor until
        // the new anchor's time, especially for loop wrap and reverse playback.
        const double change_time = pair ? std::clamp(entity.animation_time_seconds.value_or(input.current_server_seconds),
            input.previous_server_seconds, input.current_server_seconds) : input.current_server_seconds;
        const auto& visible = pair && input.sample_server_seconds < change_time ? *input.previous : entity;
        auto main = frame_sample(input, visible, !nointerp);
        if (!main) return {};
        if (pair && input.previous->sequence == entity.sequence) blend_channels(*main, input);

        auto found = states_.find(entity.entity_number);
        bool reset_state = found == states_.end() || input.discontinuity || nointerp;
        if (!reset_state) {
            const auto& old = found->second;
            reset_state = old.generation != input.generation || old.model_resource_id != input.model_resource_id ||
                old.model_revision != input.model_revision || old.model_slot != entity.model_index ||
                old.move_type != entity.brush_move_type || old.no_interpolation ||
                input.record_ordinal < old.record_ordinal || input.current_server_seconds < old.server_seconds ||
                input.current_server_seconds - old.server_seconds > input.maximum_gap_seconds ||
                input.sample_server_seconds < old.sample_seconds ||
                input.sample_server_seconds - old.sample_seconds > input.maximum_gap_seconds;
        }
        State next;
        if (!reset_state) {
            next = found->second;
            if (next.last.sequence_index != main->sequence_index) {
                next.transition_previous = next.last;
                next.transition_seconds = change_time;
            }
        }
        next.generation = input.generation; next.model_resource_id = input.model_resource_id;
        next.model_revision = input.model_revision; next.model_slot = entity.model_index;
        next.move_type = entity.brush_move_type; next.record_ordinal = input.record_ordinal;
        next.server_seconds = input.current_server_seconds; next.sample_seconds = input.sample_server_seconds;
        next.no_interpolation = nointerp; next.last = *main;
        StudioPoseCompositionInput result; result.main = *main;
        if (next.transition_previous) {
            const double age = input.sample_server_seconds - next.transition_seconds;
            if (age >= 0.0 && age < kTransitionSeconds) {
                result.previous = next.transition_previous;
                result.previous_weight = static_cast<float>(1.0 - age / kTransitionSeconds);
            } else next.transition_previous.reset();
        }
        if (found != states_.end()) found->second = std::move(next);
        else if (states_.size() < maximum_entities) states_.emplace(entity.entity_number, std::move(next));
        // At the bound playback remains correct; only optional transition
        // history is unavailable. The caller never needs an unbounded cache.
        return result;
    } catch (const std::bad_alloc&) { return {}; }
      catch (const std::length_error&) { return {}; }
}

void StudioEntityAnimationPresentation::reset() noexcept { states_.clear(); }
void StudioEntityAnimationPresentation::retain_entities(
    std::span<const client::RuntimePacketEntityObservation> entities) noexcept {
    const bool sorted = std::ranges::is_sorted(entities, {},
        &client::RuntimePacketEntityObservation::entity_number);
    std::erase_if(states_, [&](const auto& row) {
        if (sorted) {
            const auto found = std::ranges::lower_bound(entities, row.first, {},
                &client::RuntimePacketEntityObservation::entity_number);
            return found == entities.end() || found->entity_number != row.first;
        }
        return std::ranges::none_of(entities, [&](const auto& entity) { return entity.entity_number == row.first; });
    });
}
} // namespace hlclient::goldsrc::studio
