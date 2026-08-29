#pragma once

#include <hlclient/local_player/local_player_movement_controller.hpp>

#include <cstdint>
#include <optional>

namespace hlclient::local_player {

struct PlayerWalkFailureContext final {
    std::uint64_t frame_ordinal{0U};
    std::uint32_t last_valid_command_sequence{0U};
    std::uint64_t last_valid_state_signature{0U};
    std::uint64_t last_valid_camera_revision{0U};
    std::uint64_t last_valid_visibility_revision{0U};
    bool mouse_capture_active{false};
    std::optional<goldsrc::movement::PlayerWallContactDiagnosticFrame>
        movement_diagnostic;
};

struct PlayerWalkFailureSummary final {
    std::optional<LocalPlayerMovementControllerErrorCode> controller_error;
    std::optional<goldsrc::GoldSrcUserCmdSchedulerErrorCode> scheduler_error;
    std::optional<goldsrc::GoldSrcUserCmdInputAdapterErrorCode> command_error;
    std::optional<goldsrc::movement::LocalMovementSimulationErrorCode>
        movement_error;
    std::optional<goldsrc::movement::LocalMovementCollisionErrorCode>
        collision_error;
    PlayerWalkFailureContext context{};
};

struct PlayerWalkFailureDecision final {
    bool newly_latched{false};
    bool clear_input_requested{false};
    bool release_mouse_capture_requested{false};
    bool keep_rendering{true};
};

// One viewer lifetime owns one latch. A failed controller update permanently
// disables simulation but does not invalidate the last committed state/camera
// or the renderer. The retained summary is scalar metadata only.
class PlayerWalkFailureLatch final {
public:
    [[nodiscard]] PlayerWalkFailureDecision latch(
        const LocalPlayerMovementControllerUpdateResult& update,
        PlayerWalkFailureContext context) noexcept;

    [[nodiscard]] bool simulation_enabled() const noexcept;
    [[nodiscard]] bool failure_latched() const noexcept;
    [[nodiscard]] const PlayerWalkFailureSummary* summary() const noexcept;

private:
    std::optional<PlayerWalkFailureSummary> summary_;
};

} // namespace hlclient::local_player
