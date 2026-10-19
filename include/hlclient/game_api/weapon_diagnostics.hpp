#pragma once
#include <hlclient/core/multiplayer_diagnostics.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <array>
#include <span>
#include <string_view>

namespace hlclient::game_api {
enum class WeaponDiagnosticMode : std::uint8_t { primary, secondary, reload, deploy, holster };
enum class WeaponDiagnosticObjectProfile : std::uint8_t {
    unknown, grenade, bolt, rocket, hornet, satchel, tripmine, snark, beam
};
// Module-owned capability metadata, consumed by the actual host hooks. Unknown
// means unsupported, not an additional attack invented for a uniform table.
struct WeaponDiagnosticCapability final {
    std::uint8_t weapon_id{};
    std::string_view command, viewmodel;
    std::array<core::MultiplayerActionKind,5> modes{};
    bool held_primary{}, persistent_objects{};
};
// Synchronous borrowed callback only: no second clock/history/logger, no game
// query, and no callback re-entry into the module. The owning host supplies the
// single diagnostics sink and converts the exact action identity once.
struct WeaponActionDiagnosticObservation final {
    LocalWeaponActionIdentity identity;
    WeaponDiagnosticMode mode{WeaponDiagnosticMode::primary};
    core::MultiplayerActionKind kind{core::MultiplayerActionKind::unknown};
    core::MultiplayerActionPhase phase{core::MultiplayerActionPhase::accepted};
    LocalWeaponActionStatus status{LocalWeaponActionStatus::predicted_pending};
    std::optional<LocalWeaponSubmittedCommand> command;
    std::optional<std::int32_t> clip, reserve;
    std::optional<bool> reloading;
    std::optional<double> next_attack;
    std::uint32_t emitter_count{1};
    std::string_view reason; // compiled diagnostic token, never server text
    std::optional<std::uint32_t> related_command_sequence;
};
class WeaponDiagnosticObserver {
public:
    virtual ~WeaponDiagnosticObserver()=default;
    [[nodiscard]] virtual bool weapon_diagnostics_enabled() const noexcept=0;
    virtual void weapon_action_observed(const WeaponActionDiagnosticObservation&) noexcept=0;
};
}
