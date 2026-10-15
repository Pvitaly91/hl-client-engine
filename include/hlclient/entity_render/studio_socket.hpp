#pragma once
#include <hlclient/entity_render/entity_render_types.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <optional>
namespace hlclient::entity_render {
enum class StudioSocketError { identity, bone, attachment, non_finite, invalid_transform, invalid_basis };
struct StudioSocketResult {
    std::optional<assets::EvaluatedModelSocket> socket;
    std::optional<StudioSocketError> error;
    explicit operator bool() const noexcept { return socket.has_value(); }
};
// Synchronous O(1) evaluation; no import, pose evaluation, ownership or renderer IO.
[[nodiscard]] StudioSocketResult evaluate_studio_socket(
    const goldsrc::studio::StudioPoseModelIdentity&, const assets::SkeletalModelAssetData&,
    const goldsrc::studio::StudioPoseState&, const assets::ModelSocketDefinition&,
    const EntityRenderTransform&) noexcept;
[[nodiscard]] StudioSocketResult evaluate_studio_attachment(
    const goldsrc::studio::StudioPoseModelIdentity&, const assets::SkeletalModelAssetData&,
    const goldsrc::studio::StudioPoseState&, std::uint32_t attachment_index,
    const EntityRenderTransform&) noexcept;
}
