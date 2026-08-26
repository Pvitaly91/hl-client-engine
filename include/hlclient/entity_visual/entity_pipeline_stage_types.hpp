#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::entity_visual {

using EntityPipelineStageTimePoint = std::chrono::steady_clock::time_point;

inline constexpr std::size_t kDefaultMaximumEntityPipelineStageTransitions =
    64U;
inline constexpr std::size_t kHardMaximumEntityPipelineStageTransitions =
    256U;
inline constexpr std::chrono::milliseconds
    kHardMaximumEntityPipelineStageTimeout{60'000};
inline constexpr std::size_t kEntityPipelineStageDiagnosticTextLimit = 256U;

struct EntityPipelineStageLimits {
    std::size_t maximum_transitions{
        kDefaultMaximumEntityPipelineStageTransitions};
    std::optional<std::chrono::milliseconds> timeout;
};

[[nodiscard]] inline bool valid_entity_pipeline_stage_limits(
    const EntityPipelineStageLimits& limits) noexcept
{
    return limits.maximum_transitions > 0U &&
           limits.maximum_transitions <=
               kHardMaximumEntityPipelineStageTransitions &&
           (!limits.timeout ||
            (*limits.timeout > std::chrono::milliseconds::zero() &&
             *limits.timeout <= kHardMaximumEntityPipelineStageTimeout));
}

enum class EntityPipelineStageErrorCode {
    invalid_configuration,
    invalid_transition,
    invalid_input,
    time_moved_backwards,
    transition_limit_reached,
    operation_failed,
    unable_to_retain_result,
};

struct EntityPipelineStageError {
    EntityPipelineStageErrorCode code{
        EntityPipelineStageErrorCode::invalid_configuration};
    std::string context;
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityPipelineStageErrorCode code) noexcept
{
    switch (code) {
    case EntityPipelineStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntityPipelineStageErrorCode::invalid_transition:
        return "invalid_transition";
    case EntityPipelineStageErrorCode::invalid_input: return "invalid_input";
    case EntityPipelineStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case EntityPipelineStageErrorCode::transition_limit_reached:
        return "transition_limit_reached";
    case EntityPipelineStageErrorCode::operation_failed:
        return "operation_failed";
    case EntityPipelineStageErrorCode::unable_to_retain_result:
        return "unable_to_retain_result";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual
