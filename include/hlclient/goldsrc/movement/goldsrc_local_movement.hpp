#pragma once
#include <hlclient/goldsrc/movement/goldsrc_movement_config.hpp>

#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/movement/player_wall_contact_diagnostics.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::movement {


enum class LocalMovementSimulationErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_state,
    invalid_environment,
    unsupported_command_profile,
    stock_semantics_pending,
    invalid_command_sequence,
    invalid_command_duration,
    collision_query_failed,
    player_startsolid,
    player_allsolid,
    movement_trace_failed,
    velocity_limit_exceeded,
    substep_limit_exceeded,
    clip_plane_limit_exceeded,
    touch_limit_exceeded,
    allocation_failed,
    statistics_overflow,
    movement_stalled,
    liquid_movement_unsupported,
    ladder_movement_unsupported,
    duck_transition_failed,
    stand_clearance_blocked,
    state_revision_exhausted,
    simulation_time_overflow,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(
    LocalMovementSimulationErrorCode code) noexcept;

struct LocalMovementSimulationError {
    LocalMovementSimulationErrorCode code{
        LocalMovementSimulationErrorCode::invalid_configuration};
    std::optional<LocalMovementCollisionError> collision_error;
    std::string_view context;
};

struct LocalMovementSimulationResult {
    std::optional<hlclient::movement::LocalPlayerMovementState> state;
    std::vector<hlclient::movement::PlayerMovementTouch> touches;
    hlclient::movement::PlayerMovementStatistics statistics{};
    std::optional<LocalMovementSimulationError> error;
    std::uint32_t command_sequence{0U};
    std::uint64_t deterministic_state_signature{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && !error.has_value();
    }
};

struct GoldSrcLocalMovementScratch {
    // General queries and the two speculative walking routes use distinct
    // bounded scratch arenas. A rejected route cannot leave active traversal
    // marks in the selected route, and each arena retains its capacity for
    // reuse by later commands.
    hlclient::collision::CollisionQueryScratch collision;
    hlclient::collision::CollisionQueryScratch direct_candidate_collision;
    hlclient::collision::CollisionQueryScratch step_candidate_collision;
    PlayerMovementDiagnosticRing diagnostics;
    std::optional<PlayerWallContactDiagnosticFrame> last_diagnostic;
    std::uint16_t diagnostic_substep_ordinal{0U};
};

class GoldSrcLocalMovementKernel final {
public:
    [[nodiscard]] static LocalMovementSimulationResult simulate(
        const hlclient::movement::LocalPlayerMovementState& previous_state,
        const GoldSrcUserCmdState& command,
        const GoldSrcMovementEnvironment& environment,
        const ILocalMovementCollision& collision,
        GoldSrcLocalMovementScratch& scratch,
        const GoldSrcLocalMovementConfig& config = {});
};

} // namespace hlclient::goldsrc::movement
