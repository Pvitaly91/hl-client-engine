#pragma once

#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/client/runtime_observation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::game_api {

inline constexpr std::size_t kMaximumRemotePlayerBones = 128U;
inline constexpr std::size_t kMaximumRemotePlayers = 32U;

enum class WorldEntityPresentationRole : std::uint8_t { ordinary, player_proxy, unavailable };
enum class WorldEntityPresentationReason : std::uint8_t {
    ordinary, server_proxy, body_queue_reuse, continuity_boundary,
    missing_reference, invalid_reference, missing_source_player, missing_source_model
};
[[nodiscard]] constexpr std::string_view to_string(WorldEntityPresentationReason reason) noexcept {
    switch(reason) {
    case WorldEntityPresentationReason::ordinary:return "ordinary";
    case WorldEntityPresentationReason::server_proxy:return "server_proxy";
    case WorldEntityPresentationReason::body_queue_reuse:return "body_queue_reuse";
    case WorldEntityPresentationReason::continuity_boundary:return "continuity_boundary";
    case WorldEntityPresentationReason::missing_reference:return "missing_reference";
    case WorldEntityPresentationReason::invalid_reference:return "invalid_renderamt";
    case WorldEntityPresentationReason::missing_source_player:return "missing_source_player";
    case WorldEntityPresentationReason::missing_source_model:return "missing_source_model";
    }
    return "unavailable";
}
// Synchronous read views; no retained RX pointers or mutable session access.
// A proxy selects an already-approved model slot only, never an asset path.
struct WorldEntityPresentationContext final {
    const client::RuntimePacketEntityObservation& entity;
    const client::RuntimePacketEntityObservation* previous{};
    std::span<const client::RuntimePacketEntityObservation> current_entities;
    std::uint32_t maximum_clients{};
    // Already approved/imported metadata, synchronously borrowed only. Optional
    // clocks preserve strict defaults for callers without committed anchors.
    const assets::SkeletalModelAssetData* approved_player_model{};
    std::optional<double> previous_server_seconds, current_server_seconds;
};
struct WorldEntityPresentationIntent final {
    WorldEntityPresentationRole role{WorldEntityPresentationRole::ordinary};
    std::optional<std::uint32_t> model_index, source_player;
    bool discontinuity{};
    WorldEntityPresentationReason reason{WorldEntityPresentationReason::ordinary};
    std::uint32_t animation_only_effect_mask{};
};

// Selected game policy, not hidden host defaults. This presentation delay and
// discontinuity profile never changes simulation or prediction. The application
// separately reports its actual interpolation reserve in usercmd.lerp_msec;
// that metadata is not a mutation of any presented entity transform.
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
    bool server_owned_proxy{};
    // Exact source-key qualification supplied by the selected game policy.
    // Raw effects still own animation restart; these masks concern motion only.
    std::uint32_t current_animation_only_effect_mask{}, previous_animation_only_effect_mask{};
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
    std::uint64_t visual_epoch{};
    std::optional<client::RuntimeObservationSource> death_notice_source;
    double gait_yaw_degrees{};
    // Optional game-selected bone-merge attachment using an exact server model
    // slot. Missing/zero means no request; never a weapon-ID/filename guess.
    std::optional<std::uint32_t> attachment_model_index;
};

} // namespace hlclient::game_api
