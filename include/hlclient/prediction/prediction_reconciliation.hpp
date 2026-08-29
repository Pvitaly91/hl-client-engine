#pragma once

#include <hlclient/prediction/local_prediction.hpp>

namespace hlclient::prediction {

struct PredictionReconciliationConfig {
    PredictionStateComparisonConfig comparison{};
    PredictionReconciliationLimits limits{};
    bool ignore_stale_updates{true};
};

struct PredictionReconciliationResult {
    std::shared_ptr<const LocalPredictionHistoryState> history;
    std::shared_ptr<const movement::LocalPlayerMovementState>
        corrected_current_state;
    std::optional<PredictionErrorMetrics> acknowledgement_metrics;
    std::optional<PredictionErrorMetrics> current_correction_metrics;
    PredictionCorrectionClass correction_class{
        PredictionCorrectionClass::exact};
    PredictionReplayStatistics replay_statistics{};
    std::optional<PredictionError> error;
    bool stale_ignored{false};
    bool duplicate_ignored{false};
    bool history_changed{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return history != nullptr && corrected_current_state != nullptr &&
            !error.has_value();
    }
};

class LocalPlayerPredictionReconciler final {
public:
    [[nodiscard]] static PredictionReconciliationResult reconcile(
        const LocalPredictionHistoryState& history,
        const AuthoritativePlayerState& authoritative,
        const goldsrc::movement::GoldSrcMovementEnvironment& environment,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
        const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config =
            {},
        const PredictionReconciliationConfig& config = {});
};

} // namespace hlclient::prediction
