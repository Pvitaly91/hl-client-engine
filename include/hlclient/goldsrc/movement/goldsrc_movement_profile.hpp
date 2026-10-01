#pragma once

#include <cstdint>

namespace hlclient::goldsrc::movement {

enum class GoldSrcMovementEnvironmentProfile : std::uint8_t {
    movevars_dry_walk_subset_v1,
    stock_pm_move_full_compatibility_evidence_pending,
};

} // namespace hlclient::goldsrc::movement
