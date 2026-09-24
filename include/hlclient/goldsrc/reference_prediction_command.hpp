#pragma once

#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

namespace hlclient::goldsrc {

// Adapts the exact quantized command retained by network history. It never
// samples live input, changes the wire value, or assigns a packet sequence as
// a command identity. This narrow profile covers ordinary dry walk only.
[[nodiscard]] GoldSrcUserCmdState::CreationResult
reference_dry_walk_movement_command(
    GoldSrcUserCmdSequence identity,
    const GoldSrcWireUserCmd& wire) noexcept;

} // namespace hlclient::goldsrc
