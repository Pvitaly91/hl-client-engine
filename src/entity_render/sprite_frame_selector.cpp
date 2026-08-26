#include <hlclient/entity_render/sprite_frame_selector.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::entity_render {

std::string_view to_string(const SpriteFrameSelectionStatus status) noexcept
{
    switch (status) {
    case SpriteFrameSelectionStatus::selected_single: return "selected_single";
    case SpriteFrameSelectionStatus::selected_synchronized_group:
        return "selected_synchronized_group";
    case SpriteFrameSelectionStatus::selected_explicit_override:
        return "selected_explicit_override";
    case SpriteFrameSelectionStatus::invalid_top_level_entry:
        return "invalid_top_level_entry";
    case SpriteFrameSelectionStatus::invalid_elapsed_time:
        return "invalid_elapsed_time";
    case SpriteFrameSelectionStatus::invalid_flattened_frame_override:
        return "invalid_flattened_frame_override";
    case SpriteFrameSelectionStatus::malformed_group: return "malformed_group";
    case SpriteFrameSelectionStatus::random_sync_evidence_pending:
        return "random_sync_evidence_pending";
    case SpriteFrameSelectionStatus::limit_exceeded: return "limit_exceeded";
    }
    return "unknown";
}

SpriteFrameSelectionResult SpriteFrameSelector::select(
    const SpriteRenderAsset& asset,
    const SpritePlaybackInput& input,
    const SpriteFrameSelectorLimits& limits) const noexcept
{
    if (!std::isfinite(limits.maximum_elapsed_seconds) ||
        limits.maximum_elapsed_seconds <= 0.0 ||
        limits.maximum_elapsed_seconds >
            kSpriteFrameSelectorHardMaximumElapsedSeconds) {
        return {std::nullopt, SpriteFrameSelectionStatus::limit_exceeded};
    }
    if (!std::isfinite(input.elapsed_seconds) || input.elapsed_seconds < 0.0) {
        return {std::nullopt, SpriteFrameSelectionStatus::invalid_elapsed_time};
    }
    if (input.elapsed_seconds > limits.maximum_elapsed_seconds) {
        return {std::nullopt, SpriteFrameSelectionStatus::limit_exceeded};
    }
    if (static_cast<std::size_t>(input.top_level_entry_index) >=
        asset.top_level_entries().size()) {
        return {std::nullopt,
            SpriteFrameSelectionStatus::invalid_top_level_entry};
    }

    const auto& entry =
        asset.top_level_entries()[input.top_level_entry_index];
    if (input.flattened_frame_override) {
        const auto override_index = *input.flattened_frame_override;
        const auto entry_end = static_cast<std::uint64_t>(
            entry.first_flattened_frame) + entry.flattened_frame_count;
        if (override_index < entry.first_flattened_frame ||
            override_index >= entry_end ||
            static_cast<std::size_t>(override_index) >= asset.frames().size()) {
            return {std::nullopt,
                SpriteFrameSelectionStatus::invalid_flattened_frame_override};
        }
        return {override_index,
            SpriteFrameSelectionStatus::selected_explicit_override,
            std::nullopt,
            input.elapsed_seconds};
    }

    if (entry.kind == assets::SpriteTopLevelEntryKind::single) {
        if (entry.flattened_frame_count != 1U ||
            static_cast<std::size_t>(entry.first_flattened_frame) >=
                asset.frames().size()) {
            return {std::nullopt, SpriteFrameSelectionStatus::malformed_group};
        }
        return {entry.first_flattened_frame,
            SpriteFrameSelectionStatus::selected_single,
            std::nullopt,
            0.0};
    }

    if (asset.sync_type() == assets::SpriteSyncType::random) {
        // A seed is retained in the input for a future evidence-backed policy,
        // but M4.5.3 deliberately does not invent a pseudo-random algorithm.
        static_cast<void>(input.sync_seed);
        return {std::nullopt,
            SpriteFrameSelectionStatus::random_sync_evidence_pending};
    }
    if (!entry.group_ordinal ||
        static_cast<std::size_t>(*entry.group_ordinal) >= asset.groups().size()) {
        return {std::nullopt, SpriteFrameSelectionStatus::malformed_group};
    }
    const auto& group = asset.groups()[*entry.group_ordinal];
    if (group.cumulative_intervals_seconds.empty() ||
        group.cumulative_intervals_seconds.size() !=
            group.flattened_frame_indices.size()) {
        return {std::nullopt, SpriteFrameSelectionStatus::malformed_group};
    }
    const auto period =
        static_cast<double>(group.cumulative_intervals_seconds.back());
    if (!std::isfinite(period) || period <= 0.0) {
        return {std::nullopt, SpriteFrameSelectionStatus::malformed_group};
    }
    auto wrapped = std::fmod(input.elapsed_seconds, period);
    if (!std::isfinite(wrapped)) {
        return {std::nullopt, SpriteFrameSelectionStatus::invalid_elapsed_time};
    }
    if (wrapped < 0.0) {
        wrapped += period;
    }
    const auto interval = std::upper_bound(
        group.cumulative_intervals_seconds.begin(),
        group.cumulative_intervals_seconds.end(),
        static_cast<float>(wrapped));
    const auto ordinal = static_cast<std::size_t>(
        interval - group.cumulative_intervals_seconds.begin());
    if (ordinal >= group.flattened_frame_indices.size()) {
        return {std::nullopt, SpriteFrameSelectionStatus::malformed_group};
    }
    const auto frame_index = group.flattened_frame_indices[ordinal];
    if (static_cast<std::size_t>(frame_index) >= asset.frames().size()) {
        return {std::nullopt, SpriteFrameSelectionStatus::malformed_group};
    }
    return {frame_index,
        SpriteFrameSelectionStatus::selected_synchronized_group,
        static_cast<std::uint32_t>(ordinal),
        wrapped};
}

} // namespace hlclient::entity_render
