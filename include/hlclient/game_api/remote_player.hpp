#pragma once

#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/client/runtime_observation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hlclient::game_api {

inline constexpr std::size_t kMaximumRemotePlayerBones = 128U;
inline constexpr std::size_t kMaximumRemotePlayers = 32U;

// Selected game policy, not hidden host defaults. This presentation delay and
// discontinuity profile never changes simulation, wire state or prediction.
struct RemotePlayerPresentationPolicy final {
    bool enabled{};
    double interpolation_delay_seconds{};
    double maximum_observation_gap_seconds{};
    double teleport_distance_units{};
    std::size_t maximum_players{};
    // A selected protocol/game effect bit, never interpreted by the renderer.
    std::uint32_t no_interpolation_effect_mask{};
};

enum class RemotePlayerPresentationStatus : std::uint8_t {
    silent, ready, local_excluded, not_player, missing_fields,
    invalid_context, invalid_sequence, unsupported_skeleton, stale_source
};

struct RemotePlayerStudioSample final {
    std::uint32_t sequence{};
    double frame_coordinate{};
    std::int32_t body{};
    std::uint32_t skin{};
    std::array<std::uint8_t, 4U> controllers{};
    std::array<std::uint8_t, 2U> blending{};
    friend bool operator==(const RemotePlayerStudioSample&,
                           const RemotePlayerStudioSample&) = default;
};

// Synchronous immutable borrows only. The engine owns validated imported model
// data and committed observations. `entity` is the bounded presentation sample;
// current/previous are exact authoritative anchors, not renderer matrices.
// The module retains only numeric presentation state/masks, never these views.
struct RemotePlayerPresentationContext final {
    std::uint64_t network_generation{}, map_generation{}, entity_lifetime{};
    std::uint64_t model_resource_id{};
    assets::AssetSourceFingerprint model_revision{};
    const assets::SkeletalModelAssetData& model;
    const client::RuntimePacketEntityObservation& entity;
    const client::RuntimePacketEntityObservation& current;
    const client::RuntimePacketEntityObservation* previous{};
    std::uint32_t maximum_clients{};
    std::optional<std::uint32_t> receiving_entity;
    std::uint64_t current_record_identity{}, previous_record_identity{};
    // Committed anchor times stay exact even when the renderer holds a pair.
    // A zero interval is valid; sampling time does not replace a raw anchor.
    double current_server_seconds{}, previous_server_seconds{}, sample_server_seconds{};
    bool discontinuity{};
    // Source identities are opaque dedup keys, not an ordering contract.
    // These committed session ordinals supply the actual history order.
    std::uint64_t current_record_ordinal{}, previous_record_ordinal{};
};

// Numeric renderer-neutral requests. The secondary gait replaces selected
// LOCAL bone transforms before hierarchy composition; transition blends the
// previous main sample before that replacement. No effects/audio callbacks.
struct RemotePlayerPresentationIntent final {
    RemotePlayerPresentationStatus status{RemotePlayerPresentationStatus::silent};
    RemotePlayerStudioSample sample;
    std::optional<RemotePlayerStudioSample> previous_sample;
    double previous_weight{};
    std::optional<RemotePlayerStudioSample> gait_sample;
    std::array<std::uint8_t, kMaximumRemotePlayerBones> gait_bone_mask{};
    std::size_t bone_count{};
    // Source-native order: pitch, yaw, roll. The existing materializer converts
    // this to its neutral render-transform convention, exactly once.
    std::array<double, 3U> transform_angles{};
    std::uint64_t transition_identity{};
    double gait_yaw_degrees{};
};

} // namespace hlclient::game_api
