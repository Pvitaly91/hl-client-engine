#pragma once
#include <hlclient/game_api/local_visuals.hpp>
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
};
struct RemoteMuzzleFlash final {
    assets::AssetVector3 origin{};
    float radius_units{3.0F};
    std::array<float,4> color{1.0F,0.72F,0.28F,0.85F};
    double starts_at_seconds{}, ends_at_seconds{};
};
struct RemoteWeaponEffect final {
    LocalWeaponActionIdentity action;
    assets::AssetVector3 muzzle_origin{};
    std::optional<RemoteMuzzleFlash> flash;
    std::optional<LocalMuzzleLight> light;
    std::optional<LocalShellEjection> shell;
    std::optional<LocalWorldImpactRequest> impact;
};
struct RemoteWeaponEffectStatistics final {
    std::uint64_t received{}, accepted{}, duplicates{}, local_echo{},
        unresolved{}, unsupported{}, late{}, capacity{}, invalid{},
        fire{}, swing{}, impacts{}, shell_contacts{};
};
struct RemoteWeaponEffectsBatch final {
    std::array<RemoteWeaponEffect,32> effects{};
    std::size_t count{};
    RemoteWeaponEffectStatistics statistics;
};
} // namespace hlclient::game_api
