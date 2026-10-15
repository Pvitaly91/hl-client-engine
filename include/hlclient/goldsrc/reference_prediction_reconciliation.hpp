#pragma once

#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/prediction/prediction_history.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc {
class ReferenceVerticalSupportMotion;
struct ReferenceBrushCollisionContext;

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

[[nodiscard]] constexpr std::string_view to_string(const ReferenceRebaseStatus status) noexcept {
    switch (status) {
    case ReferenceRebaseStatus::ready: return "ready";
    case ReferenceRebaseStatus::invalid_profile: return "invalid_profile";
    case ReferenceRebaseStatus::boundary_missing: return "boundary_missing";
    case ReferenceRebaseStatus::history_gap: return "history_gap";
    case ReferenceRebaseStatus::replay_limit: return "replay_limit";
    case ReferenceRebaseStatus::session_mismatch: return "session_mismatch";
    case ReferenceRebaseStatus::simulation_failed: return "simulation_failed";
    case ReferenceRebaseStatus::publication_failed: return "publication_failed";
    }
    return "unknown";
}

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
    const movement::GoldSrcLocalMovementConfig& config = {},
    const ReferenceVerticalSupportMotion* support = nullptr,
    const ReferenceBrushCollisionContext* ladder_context = nullptr,
    const movement::ReferenceLadderMovementPolicy* ladder_policy = nullptr);

struct ReferenceExteriorCornerPresentation final {
    assets::AssetVector3 from{},bend{},to{},sampled_origin{};
    double route_length{};
};
// Render-only replacement for a blocked chord around one static-world convex
// corner. Both full hull legs, endpoints, ground and sampled body are checked.
// At most two perpendicular vertical planes and one bounded bend are used.
// This does not simulate commands or modify the prediction/canonical states.
[[nodiscard]] std::optional<ReferenceExteriorCornerPresentation>
sample_reference_exterior_corner_presentation(
    assets::AssetVector3 from,assets::AssetVector3 to,double alpha,
    hlclient::movement::PlayerMovementHull hull,
    const movement::ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config = {},
    double maximum_route_length = 4.0,
    std::size_t* observed_trace_queries = nullptr);

} // namespace hlclient::goldsrc
