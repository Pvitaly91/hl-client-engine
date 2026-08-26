#pragma once

#include <hlclient/entity_render/sprite_render_asset.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::entity_render {

struct SpritePlaybackInput {
    std::uint32_t top_level_entry_index{0U};
    double elapsed_seconds{0.0};
    std::optional<std::uint32_t> flattened_frame_override;
    std::optional<std::uint64_t> sync_seed;
};

struct SpriteFrameSelectorLimits {
    double maximum_elapsed_seconds{604'800.0};
};

inline constexpr double kSpriteFrameSelectorHardMaximumElapsedSeconds =
    315'576'000.0;

enum class SpriteFrameSelectionStatus {
    selected_single,
    selected_synchronized_group,
    selected_explicit_override,
    invalid_top_level_entry,
    invalid_elapsed_time,
    invalid_flattened_frame_override,
    malformed_group,
    random_sync_evidence_pending,
    limit_exceeded,
};

[[nodiscard]] std::string_view to_string(
    SpriteFrameSelectionStatus status) noexcept;

struct SpriteFrameSelectionResult {
    std::optional<std::uint32_t> flattened_frame_index;
    SpriteFrameSelectionStatus status{
        SpriteFrameSelectionStatus::invalid_top_level_entry};
    std::optional<std::uint32_t> group_frame_ordinal;
    double wrapped_elapsed_seconds{0.0};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return flattened_frame_index.has_value();
    }
};

class SpriteFrameSelector final {
public:
    [[nodiscard]] SpriteFrameSelectionResult select(
        const SpriteRenderAsset& asset,
        const SpritePlaybackInput& input,
        const SpriteFrameSelectorLimits& limits = {}) const noexcept;
};

} // namespace hlclient::entity_render
