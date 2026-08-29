#include <hlclient/local_player/player_walk_failure_latch.hpp>

namespace hlclient::local_player {

PlayerWalkFailureDecision PlayerWalkFailureLatch::latch(
    const LocalPlayerMovementControllerUpdateResult& update,
    const PlayerWalkFailureContext context) noexcept
{
    if (update || summary_) {
        return {};
    }

    PlayerWalkFailureSummary summary;
    summary.context = context;
    if (update.error) {
        summary.controller_error = update.error->code;
        if (update.error->scheduler_error) {
            summary.scheduler_error = update.error->scheduler_error->code;
        }
        if (update.error->command_error) {
            summary.command_error = update.error->command_error->code;
        }
        if (update.error->movement_error) {
            summary.movement_error = update.error->movement_error->code;
            if (update.error->movement_error->collision_error) {
                summary.collision_error =
                    update.error->movement_error->collision_error->code;
            }
        }
    }
    summary_.emplace(summary);
    return PlayerWalkFailureDecision{true, true, true, true};
}

bool PlayerWalkFailureLatch::simulation_enabled() const noexcept
{
    return !summary_.has_value();
}

bool PlayerWalkFailureLatch::failure_latched() const noexcept
{
    return summary_.has_value();
}

const PlayerWalkFailureSummary* PlayerWalkFailureLatch::summary() const noexcept
{
    return summary_ ? &*summary_ : nullptr;
}

} // namespace hlclient::local_player
