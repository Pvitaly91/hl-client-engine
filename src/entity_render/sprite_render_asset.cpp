#include <hlclient/entity_render/sprite_render_asset.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::entity_render {
namespace {

class StableHasher final {
public:
    void add(const std::uint64_t value) noexcept
    {
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            value_ ^= static_cast<std::uint8_t>(value >> (index * 8U));
            value_ *= 1'099'511'628'211ULL;
        }
    }

    void add(const std::uint32_t value) noexcept
    {
        add(static_cast<std::uint64_t>(value));
    }

    void add(const std::int32_t value) noexcept
    {
        add(static_cast<std::uint32_t>(value));
    }

    void add(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    void add_bytes(const std::span<const std::byte> bytes) noexcept
    {
        for (const auto byte : bytes) {
            value_ ^= std::to_integer<std::uint8_t>(byte);
            value_ *= 1'099'511'628'211ULL;
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? 1U : value_;
    }

private:
    std::uint64_t value_{14'695'981'039'346'656'037ULL};
};

[[nodiscard]] SpriteRenderAssetBuildResult fail(
    const SpriteRenderAssetErrorCode code,
    const std::optional<std::size_t> element_index,
    std::string context)
{
    return {
        std::nullopt,
        SpriteRenderAssetError{code, element_index, std::move(context)},
    };
}

[[nodiscard]] bool add_size(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool multiply_size(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] SpriteRenderTextureSupportStatus support_status(
    const assets::SpriteTextureFormat format) noexcept
{
    switch (format) {
    case assets::SpriteTextureFormat::normal:
        return SpriteRenderTextureSupportStatus::supported_normal_opaque;
    case assets::SpriteTextureFormat::alpha_test:
        return SpriteRenderTextureSupportStatus::supported_alpha_test_masked;
    case assets::SpriteTextureFormat::additive:
        return SpriteRenderTextureSupportStatus::
            unsupported_additive_evidence_pending;
    case assets::SpriteTextureFormat::index_alpha:
        return SpriteRenderTextureSupportStatus::
            unsupported_index_alpha_evidence_pending;
    }
    return SpriteRenderTextureSupportStatus::
        unsupported_index_alpha_evidence_pending;
}

[[nodiscard]] bool renderable(
    const SpriteRenderTextureSupportStatus status) noexcept
{
    return status ==
            SpriteRenderTextureSupportStatus::supported_normal_opaque ||
        status ==
            SpriteRenderTextureSupportStatus::supported_alpha_test_masked;
}

} // namespace

std::string_view to_string(const SpriteRenderAssetErrorCode code) noexcept
{
    switch (code) {
    case SpriteRenderAssetErrorCode::invalid_configuration:
        return "invalid_configuration";
    case SpriteRenderAssetErrorCode::invalid_source_identity:
        return "invalid_source_identity";
    case SpriteRenderAssetErrorCode::missing_source_metadata:
        return "missing_source_metadata";
    case SpriteRenderAssetErrorCode::invalid_frame: return "invalid_frame";
    case SpriteRenderAssetErrorCode::invalid_top_level_entry:
        return "invalid_top_level_entry";
    case SpriteRenderAssetErrorCode::invalid_group: return "invalid_group";
    case SpriteRenderAssetErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case SpriteRenderAssetErrorCode::unable_to_retain_asset:
        return "unable_to_retain_asset";
    }
    return "unknown";
}

SpriteRenderAsset::SpriteRenderAsset(
    const EntityRenderResourceIdentity source_identity,
    const std::uint64_t render_revision,
    const assets::SpriteOrientation orientation,
    const assets::SpriteTextureFormat texture_format,
    const assets::SpriteSyncType sync_type,
    const SpriteRenderTextureProfile render_profile,
    const SpriteRenderTextureSupportStatus texture_support_status,
    std::vector<SpriteRenderFrame> frames,
    std::vector<assets::SpriteTopLevelEntry> top_level_entries,
    std::vector<assets::SpriteFrameGroup> groups,
    const assets::WorldBounds bounds,
    const float bounding_radius,
    const SpriteRenderStatistics statistics) noexcept
    : source_identity_(source_identity),
      render_revision_(render_revision),
      orientation_(orientation),
      texture_format_(texture_format),
      sync_type_(sync_type),
      render_profile_(render_profile),
      texture_support_status_(texture_support_status),
      frames_(std::move(frames)),
      top_level_entries_(std::move(top_level_entries)),
      groups_(std::move(groups)),
      bounds_(bounds),
      bounding_radius_(bounding_radius),
      statistics_(statistics)
{
}

EntityRenderResourceIdentity SpriteRenderAsset::source_identity() const noexcept
{
    return source_identity_;
}

std::uint64_t SpriteRenderAsset::resource_id() const noexcept
{
    return source_identity_.resource_id;
}

std::uint64_t SpriteRenderAsset::resource_revision() const noexcept
{
    return render_revision_;
}

assets::SpriteOrientation SpriteRenderAsset::orientation() const noexcept
{
    return orientation_;
}

assets::SpriteTextureFormat SpriteRenderAsset::texture_format() const noexcept
{
    return texture_format_;
}

assets::SpriteSyncType SpriteRenderAsset::sync_type() const noexcept
{
    return sync_type_;
}

SpriteRenderTextureProfile SpriteRenderAsset::render_profile() const noexcept
{
    return render_profile_;
}

SpriteRenderTextureSupportStatus SpriteRenderAsset::texture_support_status()
    const noexcept
{
    return texture_support_status_;
}

std::span<const SpriteRenderFrame> SpriteRenderAsset::frames() const noexcept
{
    return frames_;
}

std::span<const assets::SpriteTopLevelEntry>
SpriteRenderAsset::top_level_entries() const noexcept
{
    return top_level_entries_;
}

std::span<const assets::SpriteFrameGroup> SpriteRenderAsset::groups() const noexcept
{
    return groups_;
}

const assets::WorldBounds& SpriteRenderAsset::bounds() const noexcept
{
    return bounds_;
}

float SpriteRenderAsset::bounding_radius() const noexcept
{
    return bounding_radius_;
}

const SpriteRenderStatistics& SpriteRenderAsset::statistics() const noexcept
{
    return statistics_;
}

SpriteRenderAssetBuildResult SpriteRenderAssetBuilder::build(
    const assets::SpriteAsset& source,
    const EntityRenderResourceIdentity source_identity,
    const RuntimeEntityVisualLimits& limits) const
{
    if (!valid_runtime_entity_visual_limits(limits)) {
        return fail(SpriteRenderAssetErrorCode::invalid_configuration,
            std::nullopt,
            "Runtime entity visual limits are invalid or exceed hard caps");
    }
    if (source_identity.resource_id == 0U || source_identity.revision == 0U) {
        return fail(SpriteRenderAssetErrorCode::invalid_source_identity,
            std::nullopt,
            "Sprite source identity and revision must both be nonzero");
    }
    if (!source.source_data) {
        return fail(SpriteRenderAssetErrorCode::missing_source_metadata,
            std::nullopt,
            "Sprite asset has no immutable source metadata");
    }
    const auto& metadata = *source.source_data;
    if (metadata.indexed_frames.empty() ||
        metadata.top_level_entries.empty() ||
        !std::isfinite(metadata.bounding_radius) ||
        metadata.bounding_radius < 0.0F) {
        return fail(SpriteRenderAssetErrorCode::missing_source_metadata,
            std::nullopt,
            "Sprite source metadata is incomplete or non-finite");
    }

    try {
        const auto texture_support = support_status(metadata.texture_format);
        const auto is_renderable = renderable(texture_support);
        const auto render_profile = !is_renderable
            ? SpriteRenderTextureProfile::unsupported
            : metadata.texture_format == assets::SpriteTextureFormat::alpha_test
            ? SpriteRenderTextureProfile::alpha_test_masked
            : SpriteRenderTextureProfile::opaque;

        std::vector<SpriteRenderFrame> frames;
        frames.reserve(metadata.indexed_frames.size());
        std::size_t rgba_bytes = 0U;
        bool have_bounds = false;
        assets::WorldBounds bounds{};
        for (std::size_t index = 0U; index < metadata.indexed_frames.size(); ++index) {
            const auto& frame = metadata.indexed_frames[index];
            std::size_t pixel_count = 0U;
            std::size_t expected_bytes = 0U;
            if (frame.width == 0U || frame.height == 0U ||
                !multiply_size(frame.width, frame.height, pixel_count) ||
                !multiply_size(pixel_count, 4U, expected_bytes) ||
                frame.indexed_pixels.size() != pixel_count ||
                (!frame.derived_rgba8.empty() &&
                    frame.derived_rgba8.size() != expected_bytes) ||
                (is_renderable && frame.derived_rgba8.size() != expected_bytes) ||
                !add_size(rgba_bytes, frame.derived_rgba8.size(), rgba_bytes)) {
                return fail(SpriteRenderAssetErrorCode::invalid_frame,
                    index,
                    "Sprite frame dimensions or owned pixel byte counts are invalid");
            }

            const auto left = static_cast<float>(frame.origin.x);
            const auto right = left + static_cast<float>(frame.width);
            const auto up = static_cast<float>(frame.origin.y);
            const auto down = up - static_cast<float>(frame.height);
            if (!std::isfinite(left) || !std::isfinite(right) ||
                !std::isfinite(up) || !std::isfinite(down)) {
                return fail(SpriteRenderAssetErrorCode::invalid_frame,
                    index,
                    "Sprite local frame geometry is non-finite");
            }
            SpriteRenderFrame output;
            output.geometry = {
                frame.origin,
                frame.width,
                frame.height,
                {{{left, down}, {right, down}, {right, up}, {left, up}}},
            };
            output.rgba8 = frame.derived_rgba8;
            output.source_top_level_entry = frame.source_top_level_entry;
            output.source_group_ordinal = frame.source_group_ordinal;
            output.source_group_frame_ordinal =
                frame.source_group_frame_ordinal;
            output.profile = render_profile;
            output.support_status = texture_support;
            frames.push_back(std::move(output));

            if (!have_bounds) {
                bounds.minimum = {left, down, 0.0F};
                bounds.maximum = {right, up, 0.0F};
                have_bounds = true;
            } else {
                bounds.minimum.x = std::min(bounds.minimum.x, left);
                bounds.minimum.y = std::min(bounds.minimum.y, down);
                bounds.maximum.x = std::max(bounds.maximum.x, right);
                bounds.maximum.y = std::max(bounds.maximum.y, up);
            }
        }

        std::vector<assets::SpriteFrameGroup> groups = metadata.groups;
        for (std::size_t index = 0U; index < groups.size(); ++index) {
            const auto& group = groups[index];
            const auto count = group.flattened_frame_indices.size();
            if (count == 0U || group.cumulative_intervals_seconds.size() != count ||
                group.frame_durations_seconds.size() != count) {
                return fail(SpriteRenderAssetErrorCode::invalid_group,
                    index,
                    "Sprite group tables have mismatched cardinalities");
            }
            float previous_interval = 0.0F;
            for (std::size_t frame_index = 0U; frame_index < count; ++frame_index) {
                const auto interval = group.cumulative_intervals_seconds[frame_index];
                const auto duration = group.frame_durations_seconds[frame_index];
                if (!std::isfinite(interval) || !std::isfinite(duration) ||
                    interval <= previous_interval || duration <= 0.0F ||
                    static_cast<std::size_t>(
                        group.flattened_frame_indices[frame_index]) >= frames.size()) {
                    return fail(SpriteRenderAssetErrorCode::invalid_group,
                        index,
                        "Sprite group interval or flattened-frame reference is invalid");
                }
                previous_interval = interval;
            }
        }

        std::vector<assets::SpriteTopLevelEntry> top_level_entries =
            metadata.top_level_entries;
        for (std::size_t index = 0U; index < top_level_entries.size(); ++index) {
            const auto& entry = top_level_entries[index];
            const auto frame_end = static_cast<std::uint64_t>(
                entry.first_flattened_frame) + entry.flattened_frame_count;
            if (entry.source_top_level_entry != index ||
                entry.flattened_frame_count == 0U || frame_end > frames.size()) {
                return fail(SpriteRenderAssetErrorCode::invalid_top_level_entry,
                    index,
                    "Sprite top-level entry range is invalid");
            }
            if (entry.kind == assets::SpriteTopLevelEntryKind::single) {
                if (entry.flattened_frame_count != 1U || entry.group_ordinal) {
                    return fail(SpriteRenderAssetErrorCode::invalid_top_level_entry,
                        index,
                        "Single sprite entry must reference exactly one frame and no group");
                }
            } else {
                if (!entry.group_ordinal ||
                    static_cast<std::size_t>(*entry.group_ordinal) >= groups.size()) {
                    return fail(SpriteRenderAssetErrorCode::invalid_top_level_entry,
                        index,
                        "Grouped sprite entry has no exact group reference");
                }
                const auto& group = groups[*entry.group_ordinal];
                if (group.source_top_level_entry != index ||
                    group.flattened_frame_indices.size() !=
                        entry.flattened_frame_count) {
                    return fail(SpriteRenderAssetErrorCode::invalid_top_level_entry,
                        index,
                        "Grouped sprite entry disagrees with its retained group metadata");
                }
                for (std::size_t ordinal = 0U;
                     ordinal < group.flattened_frame_indices.size();
                     ++ordinal) {
                    if (group.flattened_frame_indices[ordinal] !=
                        entry.first_flattened_frame + ordinal) {
                        return fail(SpriteRenderAssetErrorCode::invalid_top_level_entry,
                            index,
                            "Grouped sprite entry does not own one exact contiguous flattened range");
                    }
                }
            }
        }

        std::size_t static_geometry_bytes = 0U;
        if (!multiply_size(frames.size(),
                sizeof(SpriteRenderFrameGeometry),
                static_geometry_bytes)) {
            return fail(SpriteRenderAssetErrorCode::source_limit_exceeded,
                std::nullopt,
                "Sprite geometry byte count overflows");
        }
        std::size_t total_gpu_source_bytes = 0U;
        if (!add_size(
                rgba_bytes, static_geometry_bytes, total_gpu_source_bytes) ||
            total_gpu_source_bytes > limits.maximum_sprite_gpu_bytes) {
            return fail(SpriteRenderAssetErrorCode::source_limit_exceeded,
                std::nullopt,
                "Sprite immutable GPU-source bytes exceed the configured limit");
        }

        StableHasher revision_hash;
        revision_hash.add(source_identity.resource_id);
        revision_hash.add(source_identity.revision);
        revision_hash.add(static_cast<std::uint32_t>(metadata.orientation));
        revision_hash.add(static_cast<std::uint32_t>(metadata.texture_format));
        revision_hash.add(static_cast<std::uint32_t>(metadata.sync_type));
        revision_hash.add(metadata.bounding_radius);
        for (const auto& frame : frames) {
            revision_hash.add(frame.geometry.source_origin.x);
            revision_hash.add(frame.geometry.source_origin.y);
            revision_hash.add(frame.geometry.width);
            revision_hash.add(frame.geometry.height);
            revision_hash.add(frame.source_top_level_entry);
            revision_hash.add_bytes(frame.rgba8);
        }
        for (const auto& group : groups) {
            revision_hash.add(group.source_top_level_entry);
            revision_hash.add(group.source_group_ordinal);
            for (const auto interval : group.cumulative_intervals_seconds) {
                revision_hash.add(interval);
            }
            for (const auto frame_index : group.flattened_frame_indices) {
                revision_hash.add(frame_index);
            }
        }

        const auto renderable_frame_count =
            is_renderable ? frames.size() : 0U;
        const SpriteRenderStatistics statistics{
            top_level_entries.size(),
            groups.size(),
            frames.size(),
            renderable_frame_count,
            frames.size() - renderable_frame_count,
            rgba_bytes,
            static_geometry_bytes,
            total_gpu_source_bytes,
        };
        return {
            SpriteRenderAsset{
                source_identity,
                revision_hash.value(),
                metadata.orientation,
                metadata.texture_format,
                metadata.sync_type,
                render_profile,
                texture_support,
                std::move(frames),
                std::move(top_level_entries),
                std::move(groups),
                bounds,
                metadata.bounding_radius,
                statistics,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(SpriteRenderAssetErrorCode::unable_to_retain_asset,
            std::nullopt,
            "Unable to retain immutable Sprite render asset");
    } catch (const std::length_error&) {
        return fail(SpriteRenderAssetErrorCode::source_limit_exceeded,
            std::nullopt,
            "Sprite render asset exceeds an owning container limit");
    }
}

} // namespace hlclient::entity_render
