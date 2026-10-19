#pragma once

#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace hlclient::goldsrc::studio {

// Raw, authoritative animation anchors from the bracket surrounding the
// presentation clock. In particular, current.frame must not be an already
// interpolated wire frame. Movement and gameplay prediction are not inputs.
struct StudioEntityAnimationContext {
    const assets::SkeletalModelAssetData& model;
    const client::RuntimePacketEntityObservation& current;
    const client::RuntimePacketEntityObservation* previous{};
    StudioPoseInput base;
    std::uint64_t generation{}, model_resource_id{};
    assets::AssetSourceFingerprint model_revision{};
    std::size_t record_ordinal{};
    double current_server_seconds{}, previous_server_seconds{}, sample_server_seconds{};
    double maximum_gap_seconds{0.25};
    bool discontinuity{}, no_interpolation{};
};

// Render-only Studio playback: the SDK frame clock, signed playback, and its
// 200ms sequence transition. Bounded state is keyed by entity and immutable
// model identity. No events, sounds, collision, damage, or commands are emitted.
class StudioEntityAnimationPresentation final {
  public:
    static constexpr std::size_t maximum_entities = 4096U;
    [[nodiscard]] std::optional<StudioPoseCompositionInput>
    sample(const StudioEntityAnimationContext& context) noexcept;
    void reset() noexcept;
    // Call on every accepted raw publication, not only visible entities.
    // Removal cuts a lifetime even if the same model immediately reuses a slot.
    void retain_entities(std::span<const client::RuntimePacketEntityObservation> entities) noexcept;

  private:
    struct State {
        std::uint64_t generation{}, model_resource_id{};
        assets::AssetSourceFingerprint model_revision{};
        std::optional<std::uint32_t> model_slot, move_type;
        std::size_t record_ordinal{};
        double server_seconds{}, sample_seconds{}, transition_seconds{};
        StudioPoseInput last;
        std::optional<StudioPoseInput> transition_previous;
        bool no_interpolation{};
    };
    std::unordered_map<std::uint32_t, State> states_;
};

} // namespace hlclient::goldsrc::studio
