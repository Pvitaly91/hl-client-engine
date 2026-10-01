#pragma once

#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

namespace hlclient::goldsrc {
[[nodiscard]] GoldSrcUserCmdState::CreationResult
reference_jump_duck_weapon_use_movement_command(
    GoldSrcUserCmdSequence identity, const GoldSrcWireUserCmd& wire) noexcept;

// Adapts the exact quantized command retained by network history. It never
// samples live input, changes the wire value, or assigns a packet sequence as
// a command identity. The v1 dry-walk and v2 jump/duck profiles are distinct.
[[nodiscard]] GoldSrcUserCmdState::CreationResult
reference_dry_walk_movement_command(
    GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept;

[[nodiscard]] GoldSrcUserCmdState::CreationResult
reference_jump_duck_movement_command(
    GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept;

[[nodiscard]] GoldSrcUserCmdState::CreationResult
reference_jump_duck_weapon_movement_command(
    GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept;

} // namespace hlclient::goldsrc
