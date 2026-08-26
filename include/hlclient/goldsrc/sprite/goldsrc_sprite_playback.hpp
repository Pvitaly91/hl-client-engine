#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/sprite_asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::sprite {

inline constexpr std::size_t kHardMaximumSpriteTopLevelEntries = 1'048'576U;
inline constexpr std::size_t kHardMaximumSpriteFlattenedFrames = 1'048'576U;
inline constexpr std::size_t kHardMaximumSpriteGroupFrames = 1'048'576U;
inline constexpr double kHardMaximumSpriteElapsedSeconds = 315'576'000.0;

enum class SpritePlaybackCompatibilityProfile {
    synthetic_explicit_seconds_v1,
    stock_entity_projection_evidence_pending,
};

struct SpritePlaybackInput {
    std::uint32_t top_level_entry_index{0U};
    double elapsed_seconds{0.0};
    std::optional<std::uint32_t> flattened_frame_override;
    std::optional<std::uint64_t> sync_seed;
    SpritePlaybackCompatibilityProfile compatibility_profile{
        SpritePlaybackCompatibilityProfile::synthetic_explicit_seconds_v1};
};

struct SpritePlaybackLimits {
    std::size_t maximum_top_level_entries{65'536U};
    std::size_t maximum_flattened_frames{65'536U};
    std::size_t maximum_group_frames{65'536U};
    double maximum_elapsed_seconds{86'400.0};
};

[[nodiscard]] bool
valid_sprite_playback_limits(const SpritePlaybackLimits& limits) noexcept;

enum class SpritePlaybackErrorCode {
    invalid_configuration,
    evidence_pending,
    missing_source_data,
    invalid_entry,
    invalid_group,
    invalid_frame_override,
    invalid_time,
    unsupported_random_sync,
    unsupported_orientation,
    degenerate_billboard_basis,
    non_finite_result,
};

struct SpritePlaybackError {
    SpritePlaybackErrorCode code{
        SpritePlaybackErrorCode::invalid_configuration};
    std::optional<std::uint32_t> top_level_entry;
    std::string context;
};

enum class SpriteFrameSelectionStatus {
    single,
    synchronized_group,
    explicit_override,
};

class SpriteFrameSelection final {
  public:
    SpriteFrameSelection(const SpriteFrameSelection&) = default;
    SpriteFrameSelection(SpriteFrameSelection&&) noexcept = default;
    SpriteFrameSelection& operator=(const SpriteFrameSelection&) = delete;
    SpriteFrameSelection& operator=(SpriteFrameSelection&&) noexcept = delete;
    ~SpriteFrameSelection() = default;

    [[nodiscard]] std::uint32_t top_level_entry_index() const noexcept;
    [[nodiscard]] std::uint32_t flattened_frame_index() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>&
    group_ordinal() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>&
    group_frame_ordinal() const noexcept;
    [[nodiscard]] double wrapped_elapsed_seconds() const noexcept;
    [[nodiscard]] SpriteFrameSelectionStatus status() const noexcept;
    [[nodiscard]] SpritePlaybackCompatibilityProfile
    compatibility_profile() const noexcept;

  private:
    friend class SpriteFrameSelector;

    SpriteFrameSelection(
        std::uint32_t top_level_entry_index,
        std::uint32_t flattened_frame_index,
        std::optional<std::uint32_t> group_ordinal,
        std::optional<std::uint32_t> group_frame_ordinal,
        double wrapped_elapsed_seconds, SpriteFrameSelectionStatus status,
        SpritePlaybackCompatibilityProfile compatibility_profile) noexcept;

    std::uint32_t top_level_entry_index_{0U};
    std::uint32_t flattened_frame_index_{0U};
    std::optional<std::uint32_t> group_ordinal_;
    std::optional<std::uint32_t> group_frame_ordinal_;
    double wrapped_elapsed_seconds_{0.0};
    SpriteFrameSelectionStatus status_{SpriteFrameSelectionStatus::single};
    SpritePlaybackCompatibilityProfile compatibility_profile_{
        SpritePlaybackCompatibilityProfile::synthetic_explicit_seconds_v1};
};

struct SpriteFrameSelectionResult {
    std::optional<SpriteFrameSelection> selection;
    std::optional<SpritePlaybackError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return selection.has_value();
    }
};

class SpriteFrameSelector final {
  public:
    [[nodiscard]] SpriteFrameSelectionResult
    select(const assets::SpriteAsset& asset, const SpritePlaybackInput& input,
           const SpritePlaybackLimits& limits = {}) const;
};

enum class SpriteBillboardEvidenceProfile {
    public_valve_orientation_profile,
};

struct SpriteBillboardInput {
    assets::SpriteOrientation orientation{
        assets::SpriteOrientation::view_parallel};
    assets::AssetVector3 camera_forward{1.0F, 0.0F, 0.0F};
    assets::AssetVector3 camera_right{0.0F, -1.0F, 0.0F};
    assets::AssetVector3 camera_up{0.0F, 0.0F, 1.0F};
    // Oriented sprites consume an explicit, caller-projected basis. This
    // renderer-neutral module never infers a wire field or Euler convention.
    assets::AssetVector3 oriented_forward{1.0F, 0.0F, 0.0F};
    assets::AssetVector3 oriented_right{0.0F, -1.0F, 0.0F};
    assets::AssetVector3 oriented_up{0.0F, 0.0F, 1.0F};
};

struct SpriteBillboardBasis {
    assets::AssetVector3 right{};
    assets::AssetVector3 up{};
    assets::AssetVector3 normal{};
    assets::SpriteOrientation orientation{
        assets::SpriteOrientation::view_parallel};
    SpriteBillboardEvidenceProfile evidence_profile{
        SpriteBillboardEvidenceProfile::public_valve_orientation_profile};
};

struct SpriteBillboardBasisResult {
    std::optional<SpriteBillboardBasis> basis;
    std::optional<SpritePlaybackError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return basis.has_value();
    }
};

[[nodiscard]] SpriteBillboardBasisResult
make_sprite_billboard_basis(const SpriteBillboardInput& input) noexcept;

struct SpriteQuadVertex {
    assets::AssetVector3 position{};
    assets::AssetVector2 texture_coordinate{};
};

struct SpriteQuadGeometry {
    std::array<SpriteQuadVertex, 4U> vertices{};
};

struct SpriteQuadGeometryResult {
    std::optional<SpriteQuadGeometry> geometry;
    std::optional<SpritePlaybackError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return geometry.has_value();
    }
};

[[nodiscard]] SpriteQuadGeometryResult
make_sprite_quad_geometry(const assets::SpriteIndexedFrame& frame,
                          const SpriteBillboardBasis& basis,
                          const assets::AssetVector3& entity_origin) noexcept;

[[nodiscard]] constexpr std::string_view
to_string(SpritePlaybackErrorCode code) noexcept
{
    switch (code) {
    case SpritePlaybackErrorCode::invalid_configuration:
        return "invalid_configuration";
    case SpritePlaybackErrorCode::evidence_pending:
        return "evidence_pending";
    case SpritePlaybackErrorCode::missing_source_data:
        return "missing_source_data";
    case SpritePlaybackErrorCode::invalid_entry:
        return "invalid_entry";
    case SpritePlaybackErrorCode::invalid_group:
        return "invalid_group";
    case SpritePlaybackErrorCode::invalid_frame_override:
        return "invalid_frame_override";
    case SpritePlaybackErrorCode::invalid_time:
        return "invalid_time";
    case SpritePlaybackErrorCode::unsupported_random_sync:
        return "unsupported_random_sync";
    case SpritePlaybackErrorCode::unsupported_orientation:
        return "unsupported_orientation";
    case SpritePlaybackErrorCode::degenerate_billboard_basis:
        return "degenerate_billboard_basis";
    case SpritePlaybackErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::sprite
