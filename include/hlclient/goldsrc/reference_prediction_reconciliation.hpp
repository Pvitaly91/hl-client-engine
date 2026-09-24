#pragma once

#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/prediction/prediction_history.hpp>

#include <memory>
#include <optional>

namespace hlclient::goldsrc {

enum class ReferenceRebaseStatus : std::uint8_t {
    ready,
    invalid_profile,
    boundary_missing,
    history_gap,
    replay_limit,
    session_mismatch,
    simulation_failed,
    publication_failed,
};

struct ReferenceRebaseResult final {
    ReferenceRebaseStatus status{ReferenceRebaseStatus::invalid_profile};
    std::shared_ptr<const prediction::LocalPredictionHistoryState> history;
    std::optional<double> raw_position_error;
    std::size_t replayed_commands{0U};
    std::optional<movement::LocalMovementSimulationErrorCode>
        simulation_error;
};

// A new reconstructed server record may reuse the preceding carrier/history
// boundary. The caller owns source-record deduplication. This function compares
// the matching predicted post-state, then builds a complete replacement before
// publishing it. It never sends, samples input, or mutates canonical state.
[[nodiscard]] ReferenceRebaseResult rebase_reference_prediction(
    const prediction::LocalPredictionHistoryState& previous,
    const hlclient::movement::LocalPlayerMovementState& correction,
    const movement::GoldSrcMovementEnvironment& environment,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config = {});

} // namespace hlclient::goldsrc
