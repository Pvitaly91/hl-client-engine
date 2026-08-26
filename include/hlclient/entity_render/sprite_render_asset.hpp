#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/entity_render/entity_render_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::entity_render {

enum class SpriteRenderTextureProfile {
    opaque,
    alpha_test_masked,
    unsupported,
};

enum class SpriteRenderTextureSupportStatus {
    supported_normal_opaque,
    supported_alpha_test_masked,
    unsupported_additive_evidence_pending,
    unsupported_index_alpha_evidence_pending,
};

struct SpriteRenderFrameGeometry {
    assets::SpriteFrameOrigin source_origin{};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    // GoldSrc source origin is (left, up). The corners retain that exact
    // anchor: left/right = x/x+width and down/up = y-height/y.
    std::array<assets::AssetVector2, 4U> local_corners{};
};

struct SpriteRenderFrame {
    SpriteRenderFrameGeometry geometry{};
    std::vector<std::byte> rgba8;
    std::uint32_t source_top_level_entry{0U};
    std::optional<std::uint32_t> source_group_ordinal;
    std::optional<std::uint32_t> source_group_frame_ordinal;
    SpriteRenderTextureProfile profile{SpriteRenderTextureProfile::opaque};
    SpriteRenderTextureSupportStatus support_status{
        SpriteRenderTextureSupportStatus::supported_normal_opaque};
};

struct SpriteRenderStatistics {
    std::size_t top_level_entry_count{0U};
    std::size_t group_count{0U};
    std::size_t frame_count{0U};
    std::size_t renderable_frame_count{0U};
    std::size_t non_renderable_frame_count{0U};
    std::size_t texture_rgba_bytes{0U};
    std::size_t static_geometry_bytes{0U};
    std::size_t total_gpu_source_bytes{0U};
};

enum class SpriteRenderAssetErrorCode {
    invalid_configuration,
    invalid_source_identity,
    missing_source_metadata,
    invalid_frame,
    invalid_top_level_entry,
    invalid_group,
    source_limit_exceeded,
    unable_to_retain_asset,
};

[[nodiscard]] std::string_view to_string(
    SpriteRenderAssetErrorCode code) noexcept;

struct SpriteRenderAssetError {
    SpriteRenderAssetErrorCode code{
        SpriteRenderAssetErrorCode::invalid_configuration};
    std::optional<std::size_t> element_index;
    std::string context;
};

class SpriteRenderAsset;
struct SpriteRenderAssetBuildResult;

class SpriteRenderAsset final {
public:
    SpriteRenderAsset(const SpriteRenderAsset&) = delete;
    SpriteRenderAsset& operator=(const SpriteRenderAsset&) = delete;
    SpriteRenderAsset(SpriteRenderAsset&&) noexcept = default;
    SpriteRenderAsset& operator=(SpriteRenderAsset&&) = delete;
    ~SpriteRenderAsset() = default;

    [[nodiscard]] EntityRenderResourceIdentity source_identity() const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;
    [[nodiscard]] assets::SpriteOrientation orientation() const noexcept;
    [[nodiscard]] assets::SpriteTextureFormat texture_format() const noexcept;
    [[nodiscard]] assets::SpriteSyncType sync_type() const noexcept;
    [[nodiscard]] SpriteRenderTextureProfile render_profile() const noexcept;
    [[nodiscard]] SpriteRenderTextureSupportStatus texture_support_status()
        const noexcept;
    [[nodiscard]] std::span<const SpriteRenderFrame> frames() const noexcept;
    [[nodiscard]] std::span<const assets::SpriteTopLevelEntry>
    top_level_entries() const noexcept;
    [[nodiscard]] std::span<const assets::SpriteFrameGroup> groups() const noexcept;
    [[nodiscard]] const assets::WorldBounds& bounds() const noexcept;
    [[nodiscard]] float bounding_radius() const noexcept;
    [[nodiscard]] const SpriteRenderStatistics& statistics() const noexcept;

private:
    friend class SpriteRenderAssetBuilder;

    SpriteRenderAsset(
        EntityRenderResourceIdentity source_identity,
        std::uint64_t render_revision,
        assets::SpriteOrientation orientation,
        assets::SpriteTextureFormat texture_format,
        assets::SpriteSyncType sync_type,
        SpriteRenderTextureProfile render_profile,
        SpriteRenderTextureSupportStatus texture_support_status,
        std::vector<SpriteRenderFrame> frames,
        std::vector<assets::SpriteTopLevelEntry> top_level_entries,
        std::vector<assets::SpriteFrameGroup> groups,
        assets::WorldBounds bounds,
        float bounding_radius,
        SpriteRenderStatistics statistics) noexcept;

    EntityRenderResourceIdentity source_identity_{};
    std::uint64_t render_revision_{0U};
    assets::SpriteOrientation orientation_{
        assets::SpriteOrientation::view_parallel};
    assets::SpriteTextureFormat texture_format_{
        assets::SpriteTextureFormat::normal};
    assets::SpriteSyncType sync_type_{assets::SpriteSyncType::synchronized};
    SpriteRenderTextureProfile render_profile_{SpriteRenderTextureProfile::opaque};
    SpriteRenderTextureSupportStatus texture_support_status_{
        SpriteRenderTextureSupportStatus::supported_normal_opaque};
    std::vector<SpriteRenderFrame> frames_;
    std::vector<assets::SpriteTopLevelEntry> top_level_entries_;
    std::vector<assets::SpriteFrameGroup> groups_;
    assets::WorldBounds bounds_{};
    float bounding_radius_{0.0F};
    SpriteRenderStatistics statistics_{};
};

struct SpriteRenderAssetBuildResult {
    std::optional<SpriteRenderAsset> asset;
    std::optional<SpriteRenderAssetError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return asset.has_value();
    }
};

class SpriteRenderAssetBuilder final {
public:
    [[nodiscard]] SpriteRenderAssetBuildResult build(
        const assets::SpriteAsset& source,
        EntityRenderResourceIdentity source_identity,
        const RuntimeEntityVisualLimits& limits = {}) const;
};

} // namespace hlclient::entity_render
