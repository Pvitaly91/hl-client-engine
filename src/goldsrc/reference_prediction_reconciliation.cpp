#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>

#include <hlclient/prediction/local_prediction.hpp>

#include <cmath>
#include <new>
#include <vector>

namespace hlclient::goldsrc {

ReferenceRebaseResult rebase_reference_prediction(
    const prediction::LocalPredictionHistoryState& previous,
    const hlclient::movement::LocalPlayerMovementState& correction,
    const movement::GoldSrcMovementEnvironment& environment,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config)
{
    ReferenceRebaseResult result;
    const auto& old_session = previous.session();
    if (old_session.prediction_profile != prediction::
            PredictionCompatibilityProfile::reference_carrier_dry_walk_v1 ||
        correction.command_profile() != hlclient::movement::
            GoldSrcMovementCommandProfile::reference_wire_dry_walk_v1)
        return result;
    const auto boundary = correction.source_command_sequence();
    if (boundary == 0U)
        return {ReferenceRebaseStatus::boundary_missing};
    const hlclient::movement::LocalPlayerMovementState* matched = nullptr;
    if (previous.anchor().movement_state()->source_command_sequence() == boundary)
        matched = previous.anchor().movement_state().get();
    else if (const auto sequence = GoldSrcUserCmdSequence::create(boundary)) {
        if (const auto* entry = previous.find_exact(*sequence))
            matched = entry->post_command_state().get();
    }
    if (!matched)
        return {ReferenceRebaseStatus::boundary_missing};
    const auto& a = matched->origin();
    const auto& b = correction.origin();
    const auto dx = static_cast<double>(a.x) - b.x;
    const auto dy = static_cast<double>(a.y) - b.y;
    const auto dz = static_cast<double>(a.z) - b.z;
    result.raw_position_error = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(*result.raw_position_error))
        return {ReferenceRebaseStatus::publication_failed};
    if (old_session.prediction_generation == UINT64_MAX)
        return {ReferenceRebaseStatus::publication_failed};
    const auto session = prediction::create_prediction_session_identity(
        old_session.session_generation,
        old_session.prediction_generation + 1U, collision, environment, config,
        correction,
        prediction::PredictionCompatibilityProfile::
            reference_carrier_dry_walk_v1,
        prediction::PredictionAcknowledgementProfile::
            reference_sent_carrier_boundary_v1);
    if (!session)
        return {ReferenceRebaseStatus::session_mismatch};
    const auto initial = prediction::LocalPredictionHistoryState::create_initial(
        correction, *session.session, previous.limits());
    if (!initial)
        return {ReferenceRebaseStatus::publication_failed};
    std::shared_ptr<const prediction::LocalPredictionHistoryState> candidate =
        initial.history;
    const auto maximum = previous.limits().maximum_replay_commands;
    try {
        std::vector<prediction::PredictedCommandAppend> suffix;
        suffix.reserve(previous.size());
        auto current = candidate->current_predicted_state();
        std::uint32_t expected = boundary;
        for (const auto& old : previous.entries()) {
            const auto number = old.command_sequence().value();
            if (number <= boundary)
                continue;
            if (expected == UINT32_MAX || number != expected + 1U)
                return {ReferenceRebaseStatus::history_gap};
            if (suffix.size() >= maximum)
                return {ReferenceRebaseStatus::replay_limit};
            const auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
                *current, *old.command(), environment, collision, scratch, config);
            if (!simulated) {
                result.status = ReferenceRebaseStatus::simulation_failed;
                if (simulated.error)
                    result.simulation_error = simulated.error->code;
                return result;
            }
            auto post = std::make_shared<const hlclient::movement::
                LocalPlayerMovementState>(*simulated.state);
            suffix.push_back(prediction::PredictedCommandAppend{
                old.command(), current, post, simulated.statistics,
                prediction::summarize_prediction_touches(
                    simulated.touches, false, false)});
            current = std::move(post);
            expected = number;
        }
        if (!suffix.empty()) {
            const auto appended = prediction::append_local_prediction_commands(
                *candidate, suffix);
            if (!appended)
                return {ReferenceRebaseStatus::publication_failed};
            candidate = appended.history;
        }
        result.status = ReferenceRebaseStatus::ready;
        result.history = std::move(candidate);
        result.replayed_commands = suffix.size();
        return result;
    } catch (const std::bad_alloc&) {
        return {ReferenceRebaseStatus::publication_failed};
    }
}

} // namespace hlclient::goldsrc
