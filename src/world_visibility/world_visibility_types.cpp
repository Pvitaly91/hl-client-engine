#include <hlclient/world_visibility/world_visibility_types.hpp>

#include <type_traits>
#include <utility>

namespace hlclient::world_visibility {
namespace {

constexpr std::uint64_t kFirstOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFirstPrime = 1'099'511'628'211ULL;
constexpr std::uint64_t kSecondOffset = 7'806'984'959'868'165'187ULL;
constexpr std::uint64_t kSecondPrime = 14'029'467'366'897'019'727ULL;

class ResultSignatureHasher final {
public:
    void add(const bool value) noexcept
    {
        add(static_cast<std::uint8_t>(value ? 1U : 0U));
    }

    template <typename Integer>
        requires std::is_integral_v<Integer> &&
            (!std::is_same_v<std::remove_cv_t<Integer>, bool>)
    void add(const Integer value) noexcept
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto remaining = static_cast<Unsigned>(value);
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            const auto byte = static_cast<std::uint8_t>(remaining & 0xFFU);
            first_ ^= byte;
            first_ *= kFirstPrime;
            second_ ^= byte;
            second_ *= kSecondPrime;
            remaining >>= 8U;
        }
    }

    template <typename Enumeration>
        requires std::is_enum_v<Enumeration>
    void add(const Enumeration value) noexcept
    {
        add(static_cast<std::underlying_type_t<Enumeration>>(value));
    }

    [[nodiscard]] WorldVisibilityResultSignature result() const noexcept
    {
        return {
            first_ == 0U ? kFirstOffset : first_,
            second_ == 0U ? kSecondOffset : second_,
        };
    }

private:
    std::uint64_t first_{kFirstOffset};
    std::uint64_t second_{kSecondOffset};
};

[[nodiscard]] WorldVisibilityResultSignature make_result_signature(
    const WorldVisibilityMode requested_mode,
    const WorldVisibilityMode applied_mode,
    const WorldPvsFallbackReason fallback_reason,
    const std::optional<std::uint32_t> camera_leaf_index,
    const std::span<const std::uint32_t> visible_leaf_indices,
    const std::span<const std::uint32_t> visible_world_surface_indices,
    const std::span<const std::uint32_t> visible_brush_instance_indices,
    const WorldVisibilityStatistics& statistics,
    const std::uint64_t revision,
    const WorldVisibilitySceneIdentity scene_identity) noexcept
{
    ResultSignatureHasher hasher;
    hasher.add(static_cast<std::uint32_t>(0x56525332U));
    hasher.add(requested_mode);
    hasher.add(applied_mode);
    hasher.add(fallback_reason);
    hasher.add(camera_leaf_index.has_value());
    hasher.add(camera_leaf_index.value_or(0U));
    hasher.add(revision);
    hasher.add(scene_identity.resource_id);
    hasher.add(scene_identity.revision);
    hasher.add(scene_identity.visibility_input_signature);
    hasher.add(scene_identity.draw_input_signature);

    const auto add_indices = [&hasher](
        const std::span<const std::uint32_t> indices) noexcept {
        hasher.add(static_cast<std::uint64_t>(indices.size()));
        for (const auto index : indices) {
            hasher.add(index);
        }
    };
    add_indices(visible_leaf_indices);
    add_indices(visible_world_surface_indices);
    add_indices(visible_brush_instance_indices);

    hasher.add(static_cast<std::uint64_t>(statistics.total_world_surface_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.pvs_candidate_world_surface_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.frustum_visible_world_surface_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.visible_world_surface_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.world_surface_culled_by_pvs_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.world_surface_culled_by_frustum_count));
    hasher.add(static_cast<std::uint64_t>(statistics.total_brush_instance_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.supported_brush_instance_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.pvs_visible_brush_instance_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.frustum_visible_brush_instance_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.visible_brush_instance_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.brush_instance_culled_by_pvs_count));
    hasher.add(static_cast<std::uint64_t>(
        statistics.brush_instance_culled_by_frustum_count));
    return hasher.result();
}

} // namespace

std::string_view to_string(const WorldVisibilityMode mode) noexcept
{
    switch (mode) {
    case WorldVisibilityMode::all:
        return "all";
    case WorldVisibilityMode::frustum_only:
        return "frustum_only";
    case WorldVisibilityMode::pvs_only:
        return "pvs_only";
    case WorldVisibilityMode::pvs_and_frustum:
        return "pvs_and_frustum";
    }
    return "unknown";
}

std::string_view to_string(const WorldPvsFallbackPolicy policy) noexcept
{
    switch (policy) {
    case WorldPvsFallbackPolicy::fail_closed:
        return "fail_closed";
    case WorldPvsFallbackPolicy::frustum_only:
        return "frustum_only";
    case WorldPvsFallbackPolicy::all_surfaces:
        return "all_surfaces";
    }
    return "unknown";
}

std::string_view to_string(const WorldPvsFallbackReason reason) noexcept
{
    switch (reason) {
    case WorldPvsFallbackReason::none:
        return "none";
    case WorldPvsFallbackReason::camera_in_leaf_zero:
        return "camera_in_leaf_zero";
    case WorldPvsFallbackReason::camera_in_solid_leaf:
        return "camera_in_solid_leaf";
    case WorldPvsFallbackReason::camera_point_query_failed:
        return "camera_point_query_failed";
    case WorldPvsFallbackReason::pvs_row_unavailable:
        return "pvs_row_unavailable";
    case WorldPvsFallbackReason::visibility_data_absent:
        return "visibility_data_absent";
    }
    return "unknown";
}

WorldVisibilitySet::WorldVisibilitySet(
    const WorldVisibilityMode requested_mode,
    const WorldVisibilityMode applied_mode,
    const WorldPvsFallbackReason fallback_reason,
    const std::optional<std::uint32_t> camera_leaf_index,
    std::vector<std::uint32_t> visible_leaf_indices,
    std::vector<std::uint32_t> visible_world_surface_indices,
    std::vector<std::uint32_t> visible_brush_instance_indices,
    const WorldVisibilityStatistics statistics,
    const std::uint64_t revision,
    const WorldVisibilitySceneIdentity scene_identity) noexcept
    : requested_mode_{requested_mode},
      applied_mode_{applied_mode},
      fallback_reason_{fallback_reason},
      camera_leaf_index_{camera_leaf_index},
      visible_leaf_indices_{std::move(visible_leaf_indices)},
      visible_world_surface_indices_{std::move(visible_world_surface_indices)},
      visible_brush_instance_indices_{std::move(visible_brush_instance_indices)},
      statistics_{statistics},
      revision_{revision},
      scene_identity_{scene_identity},
      result_signature_{make_result_signature(
          requested_mode_,
          applied_mode_,
          fallback_reason_,
          camera_leaf_index_,
          visible_leaf_indices_,
          visible_world_surface_indices_,
          visible_brush_instance_indices_,
          statistics_,
          revision_,
          scene_identity_)}
{
}

WorldVisibilitySet::WorldVisibilitySet(WorldVisibilitySet&& other) noexcept
    : requested_mode_{other.requested_mode_},
      applied_mode_{other.applied_mode_},
      fallback_reason_{other.fallback_reason_},
      camera_leaf_index_{other.camera_leaf_index_},
      visible_leaf_indices_{std::move(other.visible_leaf_indices_)},
      visible_world_surface_indices_{
          std::move(other.visible_world_surface_indices_)},
      visible_brush_instance_indices_{
          std::move(other.visible_brush_instance_indices_)},
      statistics_{other.statistics_},
      revision_{other.revision_},
      scene_identity_{other.scene_identity_},
      result_signature_{other.result_signature_},
      compatibility_profile_{other.compatibility_profile_},
      evidence_profile_{other.evidence_profile_}
{
    other.revision_ = 0U;
    other.scene_identity_ = {};
    other.result_signature_ = {};
}

WorldVisibilityMode WorldVisibilitySet::requested_mode() const noexcept
{
    return requested_mode_;
}

WorldVisibilityMode WorldVisibilitySet::applied_mode() const noexcept
{
    return applied_mode_;
}

WorldPvsFallbackReason WorldVisibilitySet::fallback_reason() const noexcept
{
    return fallback_reason_;
}

std::optional<std::uint32_t> WorldVisibilitySet::camera_leaf_index() const noexcept
{
    return camera_leaf_index_;
}

std::span<const std::uint32_t> WorldVisibilitySet::visible_leaf_indices() const noexcept
{
    return visible_leaf_indices_;
}

std::span<const std::uint32_t>
WorldVisibilitySet::visible_world_surface_indices() const noexcept
{
    return visible_world_surface_indices_;
}

std::span<const std::uint32_t>
WorldVisibilitySet::visible_brush_instance_indices() const noexcept
{
    return visible_brush_instance_indices_;
}

const WorldVisibilityStatistics& WorldVisibilitySet::statistics() const noexcept
{
    return statistics_;
}

std::uint64_t WorldVisibilitySet::revision() const noexcept
{
    return revision_;
}

WorldVisibilitySceneIdentity WorldVisibilitySet::scene_identity() const noexcept
{
    return scene_identity_;
}

WorldVisibilityResultSignature WorldVisibilitySet::result_signature() const noexcept
{
    return result_signature_;
}

WorldVisibilityCompatibilityProfile WorldVisibilitySet::compatibility_profile()
    const noexcept
{
    return compatibility_profile_;
}

WorldVisibilityEvidenceProfile WorldVisibilitySet::evidence_profile() const noexcept
{
    return evidence_profile_;
}

} // namespace hlclient::world_visibility
