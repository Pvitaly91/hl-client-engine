#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::world_visibility {

enum class WorldVisibilityMode {
    all,
    frustum_only,
    pvs_only,
    pvs_and_frustum,
};

enum class WorldPvsFallbackPolicy {
    fail_closed,
    frustum_only,
    all_surfaces,
};

enum class WorldPvsFallbackReason {
    none,
    camera_in_leaf_zero,
    camera_in_solid_leaf,
    camera_point_query_failed,
    pvs_row_unavailable,
    visibility_data_absent,
};

enum class WorldVisibilityCompatibilityProfile {
    renderer_neutral_world_visibility_v1,
};

enum class WorldVisibilityEvidenceProfile {
    goldsrc_leaf_membership_pvs_and_opengl_frustum,
};

// Binds visibility artifacts to the immutable scene they were resolved from
// without introducing a GoldSrc or scene-package dependency into this neutral
// module.
struct WorldVisibilitySceneIdentity {
    std::uint64_t resource_id{0U};
    std::uint64_t revision{0U};
    std::uint64_t visibility_input_signature{0U};
    std::uint64_t draw_input_signature{0U};

    [[nodiscard]] friend bool operator==(
        const WorldVisibilitySceneIdentity&,
        const WorldVisibilitySceneIdentity&) = default;
};

// Binds one draw-list artifact to the exact immutable visibility result that
// selected it. Two independently mixed results can share a scene identity and
// revision, so neither is sufficient as a per-result pairing proof.
struct WorldVisibilityResultSignature {
    std::uint64_t first{0U};
    std::uint64_t second{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return first != 0U && second != 0U;
    }

    [[nodiscard]] friend bool operator==(
        const WorldVisibilityResultSignature&,
        const WorldVisibilityResultSignature&) = default;
};

[[nodiscard]] std::string_view to_string(WorldVisibilityMode mode) noexcept;
[[nodiscard]] std::string_view to_string(
    WorldPvsFallbackPolicy policy) noexcept;
[[nodiscard]] std::string_view to_string(
    WorldPvsFallbackReason reason) noexcept;

struct WorldVisibilityStatistics {
    std::size_t total_world_surface_count{0U};
    std::size_t pvs_candidate_world_surface_count{0U};
    std::size_t frustum_visible_world_surface_count{0U};
    std::size_t visible_world_surface_count{0U};
    std::size_t world_surface_culled_by_pvs_count{0U};
    std::size_t world_surface_culled_by_frustum_count{0U};
    std::size_t total_brush_instance_count{0U};
    std::size_t supported_brush_instance_count{0U};
    std::size_t pvs_visible_brush_instance_count{0U};
    std::size_t frustum_visible_brush_instance_count{0U};
    std::size_t visible_brush_instance_count{0U};
    std::size_t brush_instance_culled_by_pvs_count{0U};
    std::size_t brush_instance_culled_by_frustum_count{0U};
};

class WorldVisibilityResolver;

// Immutable owning CPU visibility result. It deliberately retains indices and
// bounded statistics only: no BSP/PVS source bytes, renderer handles or paths.
class WorldVisibilitySet final {
public:
    WorldVisibilitySet(const WorldVisibilitySet&) = delete;
    WorldVisibilitySet& operator=(const WorldVisibilitySet&) = delete;
    WorldVisibilitySet(WorldVisibilitySet&& other) noexcept;
    WorldVisibilitySet& operator=(WorldVisibilitySet&&) noexcept = delete;
    ~WorldVisibilitySet() = default;

    [[nodiscard]] WorldVisibilityMode requested_mode() const noexcept;
    [[nodiscard]] WorldVisibilityMode applied_mode() const noexcept;
    [[nodiscard]] WorldPvsFallbackReason fallback_reason() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> camera_leaf_index() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> visible_leaf_indices() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> visible_world_surface_indices()
        const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> visible_brush_instance_indices()
        const noexcept;
    [[nodiscard]] const WorldVisibilityStatistics& statistics() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] WorldVisibilitySceneIdentity scene_identity() const noexcept;
    [[nodiscard]] WorldVisibilityResultSignature result_signature() const noexcept;
    [[nodiscard]] WorldVisibilityCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] WorldVisibilityEvidenceProfile evidence_profile() const noexcept;

private:
    friend class WorldVisibilityResolver;

    WorldVisibilitySet(
        WorldVisibilityMode requested_mode,
        WorldVisibilityMode applied_mode,
        WorldPvsFallbackReason fallback_reason,
        std::optional<std::uint32_t> camera_leaf_index,
        std::vector<std::uint32_t> visible_leaf_indices,
        std::vector<std::uint32_t> visible_world_surface_indices,
        std::vector<std::uint32_t> visible_brush_instance_indices,
        WorldVisibilityStatistics statistics,
        std::uint64_t revision,
        WorldVisibilitySceneIdentity scene_identity) noexcept;

    WorldVisibilityMode requested_mode_{WorldVisibilityMode::all};
    WorldVisibilityMode applied_mode_{WorldVisibilityMode::all};
    WorldPvsFallbackReason fallback_reason_{WorldPvsFallbackReason::none};
    std::optional<std::uint32_t> camera_leaf_index_;
    std::vector<std::uint32_t> visible_leaf_indices_;
    std::vector<std::uint32_t> visible_world_surface_indices_;
    std::vector<std::uint32_t> visible_brush_instance_indices_;
    WorldVisibilityStatistics statistics_{};
    std::uint64_t revision_{0U};
    WorldVisibilitySceneIdentity scene_identity_{};
    WorldVisibilityResultSignature result_signature_{};
    WorldVisibilityCompatibilityProfile compatibility_profile_{
        WorldVisibilityCompatibilityProfile::renderer_neutral_world_visibility_v1};
    WorldVisibilityEvidenceProfile evidence_profile_{
        WorldVisibilityEvidenceProfile::
            goldsrc_leaf_membership_pvs_and_opengl_frustum};
};

} // namespace hlclient::world_visibility
