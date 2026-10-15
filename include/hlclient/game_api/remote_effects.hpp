#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/assets/model_asset_types.hpp>
#include <array>
#include <span>

namespace hlclient::game_api {
// Exact type-local advertised event slot and owning virtual token. This binds
// known game callbacks; the host never opens or executes an event script.
struct ScriptedEventBinding final {
    std::uint16_t index{};
    std::array<char,64> name{};
};
enum class ScriptedEventResolution : std::uint8_t {
    ready, missing_packet, invalid_packet_index, missing_arguments,
    invalid_arguments, unsupported_delay, missing_clock
};
// Published only after the whole RX record commits. All values are owned;
// record/cursor/ordinal identify the event independently of rendering/replay.
struct CommittedScriptedEvent final {
    std::uint64_t generation{}, record{};
    std::uint32_t message_bit_offset{}, entry_ordinal{}, emitter_entity{};
    std::uint16_t event_index{};
    assets::AssetVector3 origin{}, angles{}, velocity{};
    float spread_x{}, spread_y{};
    bool ducking{};
    bool angles_from_entity{};
    // Host receives a steady-clock value and converts once to app-relative time.
    double received_at_seconds{};
    ScriptedEventResolution resolution{ScriptedEventResolution::ready};
    // Host provenance only, no wire grammar change. Identity is not an ordinal.
    std::uint64_t record_ordinal{};
    std::optional<std::uint32_t> weapon_model_index;
    // Owning, exact typed schema values; absent differs from a null-base zero.
    std::optional<std::int32_t> integer_parameter1, integer_parameter2;
    std::optional<float> float_parameter1, float_parameter2;
    std::optional<bool> boolean_parameter1, boolean_parameter2;
    std::uint16_t fire_delay_ticks{}; // 10 ms wire units; unsupported for legacy callbacks.
};
enum class RemoteSocketSource : std::uint8_t {
    unavailable, explicit_model_attachment, validated_pmodel_socket, legacy_player_space_fallback
};
enum class RemotePoseResolution : std::uint8_t {
    resolved, missing, stale, generation_mismatch, lifetime_mismatch, weapon_mismatch, invalid_socket
};
[[nodiscard]] constexpr std::string_view to_string(RemoteSocketSource value) noexcept {
    switch(value) {
    case RemoteSocketSource::unavailable:return "unavailable";
    case RemoteSocketSource::explicit_model_attachment:return "explicit_model_attachment";
    case RemoteSocketSource::validated_pmodel_socket:return "validated_pmodel_socket";
    case RemoteSocketSource::legacy_player_space_fallback:return "legacy_player_space_fallback";
    } return "invalid";
}
[[nodiscard]] constexpr std::string_view to_string(RemotePoseResolution value) noexcept {
    switch(value) {
    case RemotePoseResolution::resolved:return "event_pose_resolved";
    case RemotePoseResolution::missing:return "event_pose_missing";
    case RemotePoseResolution::stale:return "event_pose_stale";
    case RemotePoseResolution::generation_mismatch:return "event_player_generation_mismatch";
    case RemotePoseResolution::lifetime_mismatch:return "event_player_lifetime_mismatch";
    case RemotePoseResolution::weapon_mismatch:return "weapon_model_mismatch";
    case RemotePoseResolution::invalid_socket:return "invalid_socket";
    } return "invalid";
}
// Immutable synchronous model read view. Semantic selection belongs to game.
struct RemoteWeaponModelContext {
    std::uint64_t resource_id{};
    assets::AssetSourceFingerprint revision{};
    std::string_view virtual_name;
    const assets::SkeletalModelAssetData& model;
};
// Plan is copied synchronously. Asset names/bone selection belong to the game;
// generic host evaluates it against its already merged pose, without new IO.
struct RemoteWeaponSocketPlan {
    bool semantic_match{};
    RemoteSocketSource source{RemoteSocketSource::unavailable};
    assets::ModelSocketDefinition muzzle, shell;
    std::optional<std::uint32_t> muzzle_attachment, shell_attachment;
    std::uint32_t semantic_key{}; // opaque selected-module profile discriminator
};
// Owning bounded numeric frame output, not a retained asset/pose/RX pointer.
// Host publishes at most 32 after the scene transaction succeeds. Times use
// the existing application-relative clock; source ordinal is ordering evidence,
// record/frame/resource/lifetime are distinct identities, not sortable clocks.
// An empty batch clears the current view. Reset/teardown clear module storage.
struct RemoteWeaponPoseSample {
    std::uint64_t generation{}, map_generation{}, entity_lifetime{}, source_record{}, source_ordinal{},
        frame_identity{}, resource_id{};
    assets::AssetSourceFingerprint revision{};
    std::uint32_t entity{}, model_slot{}, effects{}, server_sequence{};
    std::optional<std::uint32_t> current_model_slot;
    double observed_at_seconds{}, presented_at_seconds{};
    RemoteSocketSource source{RemoteSocketSource::unavailable};
    assets::EvaluatedModelSocket muzzle, shell;
    std::uint32_t muzzle_bone{}, shell_bone{};
    bool semantic_known{}, semantic_match{};
    std::uint32_t semantic_key{};
};
struct RemoteFirePoseDiagnostic {
    RemotePoseResolution status{RemotePoseResolution::missing};
    RemoteSocketSource source{RemoteSocketSource::legacy_player_space_fallback};
    std::uint64_t lifetime{}, frame_identity{}, resource_id{};
    std::uint32_t model_slot{}, muzzle_bone{}, shell_bone{}, server_sequence{}, effects{};
    double pose_age{}, event_age{};
};
struct RemoteMuzzleFlash final {
    assets::AssetVector3 origin{};
    float radius_units{3.0F};
    std::array<float,4> color{1.0F,0.72F,0.28F,0.85F};
    double starts_at_seconds{}, ends_at_seconds{};
    std::optional<MuzzleSpriteProfile> sprite;
};
struct RemoteWeaponEffect final {
    LocalWeaponActionIdentity action;
    assets::AssetVector3 muzzle_origin{};
    std::optional<RemoteMuzzleFlash> flash;
    std::optional<LocalMuzzleLight> light;
    std::optional<LocalShellEjection> shell;
    std::optional<LocalWorldImpactRequest> impact;
    RemoteFirePoseDiagnostic pose;
    std::array<LocalShellEjection,1> extra_shells{};
    std::size_t extra_shell_count{};
    std::array<LocalWorldImpactRequest,7> extra_impacts{};
    std::size_t extra_impact_count{};
};
struct RemoteWeaponEffectStatistics final {
    std::uint64_t received{}, accepted{}, duplicates{}, local_echo{},
        unresolved{}, unsupported{}, late{}, capacity{}, invalid{},
        fire{}, swing{}, impacts{}, shell_contacts{};
    std::uint64_t secondary_launch{};
};
struct RemoteWeaponEffectsBatch final {
    std::array<RemoteWeaponEffect,32> effects{};
    std::size_t count{};
    RemoteWeaponEffectStatistics statistics;
};
} // namespace hlclient::game_api
