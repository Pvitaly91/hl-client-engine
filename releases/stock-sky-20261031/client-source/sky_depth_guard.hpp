#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include "sky_candidate_api.hpp"

namespace hlclient::renderer::opengl {

// Pure, task-only eligibility test. Bounds must enclose the actual indexed
// positions of every draw being moved before the backdrop. Supply the SAME
// float MVP uploaded for that draw, not a separately recomputed transform.
// A false result means keep the original early-backdrop order; it is never
// permission to omit geometry or change its depth/shader/material behavior.
[[nodiscard]] inline bool sky_depth_guard(const assets::WorldBounds& bounds,
    const renderer::RenderMatrix4& mvp, int depth_bits,
    SkyDepthWitness* witness=nullptr) noexcept {
    const auto invalid=[&](unsigned reason){if(witness)witness->failure=reason;return false;};
    if (depth_bits < 16) return invalid(1);
    const double minimum[3]{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
    const double maximum[3]{bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
            minimum[axis] > maximum[axis]) return invalid(2);
    }
    for (const float value : mvp.values) if (!std::isfinite(value)) return invalid(3);

    const int effective_bits = (std::min)(depth_bits, 24);
    const double epsilon = static_cast<double>(std::numeric_limits<float>::epsilon());
    const double delta = (std::max)(4.0 / (std::ldexp(1.0, effective_bits) - 1.0), 32.0 * epsilon);
    const double scale = 1.0 - 2.0 * delta;
    const double maximum_float = static_cast<double>((std::numeric_limits<float>::max)());
    for (unsigned corner = 0; corner < 8; ++corner) {
        const double position[4]{
            (corner & 1U) ? maximum[0] : minimum[0],
            (corner & 2U) ? maximum[1] : minimum[1],
            (corner & 4U) ? maximum[2] : minimum[2], 1.0};
        double z{}, w{}, z_magnitude{}, w_magnitude{};
        for (unsigned column = 0; column < 4; ++column) {
            // Column-major, column-vector multiplication: rows z and w.
            const double z_term = static_cast<double>(mvp.values[column * 4U + 2U]) * position[column];
            const double w_term = static_cast<double>(mvp.values[column * 4U + 3U]) * position[column];
            z += z_term; w += w_term;
            z_magnitude += std::abs(z_term); w_magnitude += std::abs(w_term);
        }
        // The float-roundoff allowance is not valid across float overflow.
        // This extra fail-closed bound also covers any sum-of-products order.
        if (!std::isfinite(z) || !std::isfinite(w) ||
            !std::isfinite(z_magnitude) || !std::isfinite(w_magnitude) ||
            z_magnitude > maximum_float || w_magnitude > maximum_float) return invalid(4);
        const double q = scale * w - z;
        const double roundoff = 64.0 * epsilon * (z_magnitude + w_magnitude + 1.0);
        if (!std::isfinite(q) || !std::isfinite(roundoff) || !(q > roundoff)) {
            if(witness){witness->corner=corner;witness->z=z;witness->w=w;
                witness->q=q;witness->roundoff=roundoff;witness->delta=delta;witness->scale=scale;
                witness->z_magnitude=z_magnitude;witness->w_magnitude=w_magnitude;}
            return invalid(5);
        }
    }
    return true;
}
} // namespace hlclient::renderer::opengl
