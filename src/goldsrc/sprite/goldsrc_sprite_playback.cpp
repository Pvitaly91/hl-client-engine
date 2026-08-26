#include <hlclient/goldsrc/sprite/goldsrc_sprite_playback.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace hlclient::goldsrc::sprite {
namespace {

[[nodiscard]] SpriteFrameSelectionResult fail_selection(
    const SpritePlaybackErrorCode code, std::string context,
    const std::optional<std::uint32_t> top_level_entry = std::nullopt)
{
    return {std::nullopt,
            SpritePlaybackError{code, top_level_entry, std::move(context)}};
}

[[nodiscard]] SpriteBillboardBasisResult
fail_basis(const SpritePlaybackErrorCode code,
           const std::string_view context) noexcept
{
    try {
        return {std::nullopt,
                SpritePlaybackError{code, std::nullopt, std::string{context}}};
    } catch (...) {
        return {std::nullopt, SpritePlaybackError{code, std::nullopt, {}}};
    }
}

[[nodiscard]] SpriteQuadGeometryResult
fail_quad(const SpritePlaybackErrorCode code,
          const std::string_view context) noexcept
{
    try {
        return {std::nullopt,
                SpritePlaybackError{code, std::nullopt, std::string{context}}};
    } catch (...) {
        return {std::nullopt, SpritePlaybackError{code, std::nullopt, {}}};
    }
}

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float dot(const assets::AssetVector3& left,
                        const assets::AssetVector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] assets::AssetVector3
subtract(const assets::AssetVector3& left,
         const assets::AssetVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] assets::AssetVector3 scale(const assets::AssetVector3& value,
                                         const float factor) noexcept
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] assets::AssetVector3
add(const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] assets::AssetVector3
cross(const assets::AssetVector3& left,
      const assets::AssetVector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] std::optional<assets::AssetVector3>
normalize(const assets::AssetVector3& value) noexcept
{
    if (!finite(value)) {
        return std::nullopt;
    }
    const auto length_squared = static_cast<double>(value.x) * value.x +
                                static_cast<double>(value.y) * value.y +
                                static_cast<double>(value.z) * value.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12) {
        return std::nullopt;
    }
    const auto inverse_length = 1.0 / std::sqrt(length_squared);
    const assets::AssetVector3 result{
        static_cast<float>(value.x * inverse_length),
        static_cast<float>(value.y * inverse_length),
        static_cast<float>(value.z * inverse_length),
    };
    return finite(result) ? std::optional{result} : std::nullopt;
}

[[nodiscard]] std::optional<assets::AssetVector3>
orthogonalized(const assets::AssetVector3& candidate,
               const assets::AssetVector3& unit_axis) noexcept
{
    return normalize(
        subtract(candidate, scale(unit_axis, dot(candidate, unit_axis))));
}

[[nodiscard]] bool
group_entry_consistent(const assets::SpriteTopLevelEntry& entry,
                       const assets::SpriteFrameGroup& group,
                       const std::size_t flattened_frame_count) noexcept
{
    if (entry.flattened_frame_count == 0U ||
        group.source_top_level_entry != entry.source_top_level_entry ||
        group.flattened_frame_indices.size() != entry.flattened_frame_count) {
        return false;
    }

    const auto first = static_cast<std::uint64_t>(entry.first_flattened_frame);
    const auto end = first + entry.flattened_frame_count;
    if (end > flattened_frame_count) {
        return false;
    }
    return std::all_of(group.flattened_frame_indices.begin(),
                       group.flattened_frame_indices.end(),
                       [first, end](const std::uint32_t frame) {
                           return frame >= first && frame < end;
                       });
}

} // namespace

bool valid_sprite_playback_limits(const SpritePlaybackLimits& limits) noexcept
{
    return limits.maximum_top_level_entries > 0U &&
           limits.maximum_top_level_entries <=
               kHardMaximumSpriteTopLevelEntries &&
           limits.maximum_flattened_frames > 0U &&
           limits.maximum_flattened_frames <=
               kHardMaximumSpriteFlattenedFrames &&
           limits.maximum_group_frames > 0U &&
           limits.maximum_group_frames <= kHardMaximumSpriteGroupFrames &&
           std::isfinite(limits.maximum_elapsed_seconds) &&
           limits.maximum_elapsed_seconds > 0.0 &&
           limits.maximum_elapsed_seconds <= kHardMaximumSpriteElapsedSeconds;
}

SpriteFrameSelection::SpriteFrameSelection(
    const std::uint32_t top_level_entry_index,
    const std::uint32_t flattened_frame_index,
    std::optional<std::uint32_t> group_ordinal,
    std::optional<std::uint32_t> group_frame_ordinal,
    const double wrapped_elapsed_seconds,
    const SpriteFrameSelectionStatus status,
    const SpritePlaybackCompatibilityProfile compatibility_profile) noexcept
    : top_level_entry_index_(top_level_entry_index),
      flattened_frame_index_(flattened_frame_index),
      group_ordinal_(std::move(group_ordinal)),
      group_frame_ordinal_(std::move(group_frame_ordinal)),
      wrapped_elapsed_seconds_(wrapped_elapsed_seconds), status_(status),
      compatibility_profile_(compatibility_profile)
{
}

std::uint32_t SpriteFrameSelection::top_level_entry_index() const noexcept
{
    return top_level_entry_index_;
}

std::uint32_t SpriteFrameSelection::flattened_frame_index() const noexcept
{
    return flattened_frame_index_;
}

const std::optional<std::uint32_t>&
SpriteFrameSelection::group_ordinal() const noexcept
{
    return group_ordinal_;
}

const std::optional<std::uint32_t>&
SpriteFrameSelection::group_frame_ordinal() const noexcept
{
    return group_frame_ordinal_;
}

double SpriteFrameSelection::wrapped_elapsed_seconds() const noexcept
{
    return wrapped_elapsed_seconds_;
}

SpriteFrameSelectionStatus SpriteFrameSelection::status() const noexcept
{
    return status_;
}

SpritePlaybackCompatibilityProfile
SpriteFrameSelection::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

SpriteFrameSelectionResult
SpriteFrameSelector::select(const assets::SpriteAsset& asset,
                            const SpritePlaybackInput& input,
                            const SpritePlaybackLimits& limits) const
{
    if (!valid_sprite_playback_limits(limits)) {
        return fail_selection(SpritePlaybackErrorCode::invalid_configuration,
                              "Sprite playback limits are invalid");
    }
    if (input.compatibility_profile ==
        SpritePlaybackCompatibilityProfile::
            stock_entity_projection_evidence_pending) {
        return fail_selection(
            SpritePlaybackErrorCode::evidence_pending,
            "Stock entity-to-sprite playback time mapping is evidence-pending",
            input.top_level_entry_index);
    }
    if (!std::isfinite(input.elapsed_seconds) || input.elapsed_seconds < 0.0 ||
        input.elapsed_seconds > limits.maximum_elapsed_seconds) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_time,
            "Explicit elapsed seconds are non-finite, negative, "
            "or exceed the configured bound",
            input.top_level_entry_index);
    }
    if (!asset.source_data) {
        return fail_selection(SpritePlaybackErrorCode::missing_source_data,
                              "Sprite has no retained source playback metadata",
                              input.top_level_entry_index);
    }

    const auto& source = *asset.source_data;
    if (source.top_level_entries.size() > limits.maximum_top_level_entries ||
        source.indexed_frames.size() > limits.maximum_flattened_frames) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_configuration,
            "Sprite playback metadata exceeds the configured bounds",
            input.top_level_entry_index);
    }
    if (input.top_level_entry_index >= source.top_level_entries.size()) {
        return fail_selection(SpritePlaybackErrorCode::invalid_entry,
                              "Top-level sprite entry is out of range",
                              input.top_level_entry_index);
    }

    const auto& entry = source.top_level_entries[input.top_level_entry_index];
    if (entry.source_top_level_entry != input.top_level_entry_index) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_entry,
            "Top-level sprite entry retained a mismatched source ordinal",
            input.top_level_entry_index);
    }
    const auto entry_end =
        static_cast<std::uint64_t>(entry.first_flattened_frame) +
        entry.flattened_frame_count;
    if (entry.flattened_frame_count == 0U ||
        entry_end > source.indexed_frames.size()) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_entry,
            "Top-level sprite entry has an invalid flattened-frame range",
            input.top_level_entry_index);
    }

    if (input.flattened_frame_override) {
        const auto selected = *input.flattened_frame_override;
        if (selected < entry.first_flattened_frame || selected >= entry_end) {
            return fail_selection(
                SpritePlaybackErrorCode::invalid_frame_override,
                "Flattened-frame override is outside its top-level entry",
                input.top_level_entry_index);
        }
        return {SpriteFrameSelection{
                    input.top_level_entry_index, selected, entry.group_ordinal,
                    std::nullopt, input.elapsed_seconds,
                    SpriteFrameSelectionStatus::explicit_override,
                    input.compatibility_profile},
                std::nullopt};
    }

    if (entry.kind == assets::SpriteTopLevelEntryKind::single) {
        if (entry.flattened_frame_count != 1U || entry.group_ordinal) {
            return fail_selection(
                SpritePlaybackErrorCode::invalid_entry,
                "Single sprite entry does not describe exactly one frame",
                input.top_level_entry_index);
        }
        return {SpriteFrameSelection{input.top_level_entry_index,
                                     entry.first_flattened_frame, std::nullopt,
                                     std::nullopt, 0.0,
                                     SpriteFrameSelectionStatus::single,
                                     input.compatibility_profile},
                std::nullopt};
    }

    if (source.sync_type == assets::SpriteSyncType::random) {
        static_cast<void>(input.sync_seed);
        return fail_selection(
            SpritePlaybackErrorCode::unsupported_random_sync,
            "Random sprite synchronization policy is evidence-pending",
            input.top_level_entry_index);
    }
    if (!entry.group_ordinal || *entry.group_ordinal >= source.groups.size()) {
        return fail_selection(SpritePlaybackErrorCode::invalid_group,
                              "Grouped sprite entry has no valid group ordinal",
                              input.top_level_entry_index);
    }

    const auto& group = source.groups[*entry.group_ordinal];
    const auto count = group.flattened_frame_indices.size();
    if (group.source_group_ordinal != *entry.group_ordinal || count == 0U ||
        count > limits.maximum_group_frames ||
        group.cumulative_intervals_seconds.size() != count ||
        group.frame_durations_seconds.size() != count ||
        !group_entry_consistent(entry, group, source.indexed_frames.size())) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_group,
            "Grouped sprite metadata is inconsistent or exceeds "
            "the configured bound",
            input.top_level_entry_index);
    }

    float previous = 0.0F;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto cumulative = group.cumulative_intervals_seconds[index];
        const auto duration = group.frame_durations_seconds[index];
        if (!std::isfinite(cumulative) || !std::isfinite(duration) ||
            cumulative <= previous || duration <= 0.0F) {
            return fail_selection(
                SpritePlaybackErrorCode::invalid_group,
                "Sprite group intervals are not finite and strictly increasing",
                input.top_level_entry_index);
        }
        previous = cumulative;
    }

    const auto period = static_cast<double>(previous);
    auto wrapped = std::fmod(input.elapsed_seconds, period);
    if (!std::isfinite(wrapped)) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_time,
            "Sprite group wrapping produced a non-finite time",
            input.top_level_entry_index);
    }
    if (wrapped < 0.0) {
        wrapped += period;
    }

    const auto boundary = std::upper_bound(
        group.cumulative_intervals_seconds.begin(),
        group.cumulative_intervals_seconds.end(), static_cast<float>(wrapped));
    const auto ordinal = static_cast<std::size_t>(
        boundary - group.cumulative_intervals_seconds.begin());
    if (ordinal >= count) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_group,
            "Sprite group interval lookup escaped its bounded frame list",
            input.top_level_entry_index);
    }
    const auto selected = group.flattened_frame_indices[ordinal];
    if (selected >= source.indexed_frames.size()) {
        return fail_selection(
            SpritePlaybackErrorCode::invalid_group,
            "Sprite group references an unavailable flattened frame",
            input.top_level_entry_index);
    }

    return {SpriteFrameSelection{input.top_level_entry_index, selected,
                                 entry.group_ordinal,
                                 static_cast<std::uint32_t>(ordinal), wrapped,
                                 SpriteFrameSelectionStatus::synchronized_group,
                                 input.compatibility_profile},
            std::nullopt};
}

SpriteBillboardBasisResult
make_sprite_billboard_basis(const SpriteBillboardInput& input) noexcept
{
    if (input.orientation == assets::SpriteOrientation::facing_upright ||
        input.orientation ==
            assets::SpriteOrientation::view_parallel_oriented) {
        return fail_basis(
            SpritePlaybackErrorCode::unsupported_orientation,
            "Exact public orientation semantics remain evidence-pending");
    }

    if (input.orientation == assets::SpriteOrientation::view_parallel) {
        const auto right = normalize(input.camera_right);
        if (!right) {
            return fail_basis(
                SpritePlaybackErrorCode::degenerate_billboard_basis,
                "View-parallel camera-right vector is invalid");
        }
        const auto up = orthogonalized(input.camera_up, *right);
        const auto normal = up ? normalize(cross(*right, *up)) : std::nullopt;
        if (!up || !normal) {
            return fail_basis(
                SpritePlaybackErrorCode::degenerate_billboard_basis,
                "View-parallel camera basis is degenerate");
        }
        return {SpriteBillboardBasis{*right, *up, *normal, input.orientation,
                                     SpriteBillboardEvidenceProfile::
                                         public_valve_orientation_profile},
                std::nullopt};
    }

    if (input.orientation == assets::SpriteOrientation::view_parallel_upright) {
        const auto forward = normalize(input.camera_forward);
        constexpr assets::AssetVector3 world_up{0.0F, 0.0F, 1.0F};
        const auto right =
            forward ? normalize(cross(*forward, world_up)) : std::nullopt;
        const auto normal =
            right ? normalize(cross(*right, world_up)) : std::nullopt;
        if (!right || !normal) {
            return fail_basis(
                SpritePlaybackErrorCode::degenerate_billboard_basis,
                "View-parallel-upright camera direction is parallel to world "
                "up");
        }
        return {SpriteBillboardBasis{*right, world_up, *normal,
                                     input.orientation,
                                     SpriteBillboardEvidenceProfile::
                                         public_valve_orientation_profile},
                std::nullopt};
    }

    if (input.orientation == assets::SpriteOrientation::oriented) {
        const auto forward = normalize(input.oriented_forward);
        const auto right = normalize(input.oriented_right);
        const auto up =
            right ? orthogonalized(input.oriented_up, *right) : std::nullopt;
        const auto normal =
            (right && up) ? normalize(cross(*right, *up)) : std::nullopt;
        if (!forward || !right || !up || !normal ||
            std::fabs(dot(*forward, *normal)) < 1.0F - 1.0e-4F) {
            return fail_basis(
                SpritePlaybackErrorCode::degenerate_billboard_basis,
                "Explicit oriented-sprite basis is non-finite, "
                "degenerate, or inconsistent");
        }
        const auto oriented_normal =
            dot(*forward, *normal) < 0.0F ? scale(*normal, -1.0F) : *normal;
        return {SpriteBillboardBasis{*right, *up, oriented_normal,
                                     input.orientation,
                                     SpriteBillboardEvidenceProfile::
                                         public_valve_orientation_profile},
                std::nullopt};
    }

    return fail_basis(SpritePlaybackErrorCode::unsupported_orientation,
                      "Sprite orientation value is unsupported");
}

SpriteQuadGeometryResult
make_sprite_quad_geometry(const assets::SpriteIndexedFrame& frame,
                          const SpriteBillboardBasis& basis,
                          const assets::AssetVector3& entity_origin) noexcept
{
    if (!finite(entity_origin) || !finite(basis.right) || !finite(basis.up)) {
        return fail_quad(SpritePlaybackErrorCode::non_finite_result,
                         "Sprite quad input contains a non-finite vector");
    }
    const auto left = static_cast<float>(frame.origin.x);
    const auto right = left + static_cast<float>(frame.width);
    const auto top = static_cast<float>(frame.origin.y);
    const auto bottom = top - static_cast<float>(frame.height);

    const auto position = [&](const float horizontal, const float vertical) {
        return add(entity_origin, add(scale(basis.right, horizontal),
                                      scale(basis.up, vertical)));
    };

    SpriteQuadGeometry geometry;
    geometry.vertices[0U] = {position(left, bottom), {0.0F, 1.0F}};
    geometry.vertices[1U] = {position(right, bottom), {1.0F, 1.0F}};
    geometry.vertices[2U] = {position(right, top), {1.0F, 0.0F}};
    geometry.vertices[3U] = {position(left, top), {0.0F, 0.0F}};
    if (!std::all_of(geometry.vertices.begin(), geometry.vertices.end(),
                     [](const SpriteQuadVertex& vertex) {
                         return finite(vertex.position) &&
                                std::isfinite(vertex.texture_coordinate.x) &&
                                std::isfinite(vertex.texture_coordinate.y);
                     })) {
        return fail_quad(SpritePlaybackErrorCode::non_finite_result,
                         "Sprite quad generation produced a non-finite vertex");
    }
    return {geometry, std::nullopt};
}

} // namespace hlclient::goldsrc::sprite
