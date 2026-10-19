#pragma once

#include <cstdint>

namespace hlclient::core {

// Per-update observations from the authoritative prediction implementation.
// Durations may overlap; counters report actual queries and successful
// traversal steps rather than inferred work or root-cause attribution.
struct PredictionWorkDiagnostics final {
    double command_ms{},replay_ms{},collision_query_ms{};
    std::uint64_t trace_queries{},position_queries{},contents_queries{},traversal_steps{};
};

} // namespace hlclient::core
