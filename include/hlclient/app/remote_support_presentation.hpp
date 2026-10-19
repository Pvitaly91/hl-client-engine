#pragma once

#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>

namespace hlclient::app {

// Derived visual evidence only; never a movement seed or an authoritative
// onground claim. The rendered actor and its supporting brush use one space.
struct RemoteSupportPresentation final {
  assets::AssetVector3 origin{};
  goldsrc::collision::BrushCollisionInstanceIdentity support{};
  double sample_seconds{}, sampled_support_z{}, rendered_support_z{}, offset_z{};
};

// Contexts are cached once per presentation, in the same oldest-to-newest order
// as the complete player observation history, including the latest publication.
// At most sixteen retained observations plus a same-time current publication.
// Requires two independently grounded raw endpoints and continuous vertical
// PUSH identity through the full suffix. Failing evidence means no adjustment.
// No history, canonical observation, collision scene or command is modified.
[[nodiscard]] std::optional<RemoteSupportPresentation>
sample_remote_vertical_support(
    const goldsrc::RuntimePlayerInterpolationAnchor& anchor,
    const client::RuntimePacketEntityObservation& sampled_actor,
    const client::RuntimeClientObservationState& latest,
    std::span<const goldsrc::ReferenceBrushCollisionContext> contexts,
    const std::shared_ptr<const goldsrc::collision::BrushCollisionScene>& rendered_scene,
    collision::CollisionQueryScratch& scratch);

} // namespace hlclient::app
