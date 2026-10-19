#pragma once

#include <hlclient/core/prediction_work_diagnostics.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace hlclient::goldsrc::movement {

// Transparent, non-owning diagnostics decorator. A null sink is the production
// fast path: it forwards directly without reading a clock or touching counters.
class MeasuredMovementCollision final : public ILocalMovementCollision {
public:
    explicit MeasuredMovementCollision(
        const ILocalMovementCollision& collision,
        core::PredictionWorkDiagnostics* diagnostics = nullptr) noexcept
        : collision_{collision}, diagnostics_{diagnostics} {}

    [[nodiscard]] LocalMovementCollisionProfile profile() const noexcept override {
        return collision_.profile();
    }
    [[nodiscard]] bool valid() const noexcept override {
        return collision_.valid();
    }
    [[nodiscard]] std::optional<LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override {
        return collision_.session_identity();
    }

    [[nodiscard]] LocalMovementPointContentsQueryResult point_contents(
        const assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override {
        if (!diagnostics_) return collision_.point_contents(point,scratch,config);
        increment(diagnostics_->contents_queries);
        const auto begin=Clock::now();
        auto result=collision_.point_contents(point,scratch,config);
        elapsed(begin);
        if (result && result.result)
            add(diagnostics_->traversal_steps,result.result->traversal_depth);
        return result;
    }

    [[nodiscard]] LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3& origin,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override {
        if (!diagnostics_) return collision_.test_position(origin,hull,scratch,config);
        increment(diagnostics_->position_queries);
        const auto begin=Clock::now();
        auto result=collision_.test_position(origin,hull,scratch,config);
        elapsed(begin);
        if (result && result.result)
            add(diagnostics_->traversal_steps,result.result->traversal_depth);
        return result;
    }

    [[nodiscard]] LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override {
        if (!diagnostics_)
            return collision_.trace_hull(start,end,hull,scratch,config);
        increment(diagnostics_->trace_queries);
        const auto begin=Clock::now();
        auto result=collision_.trace_hull(start,end,hull,scratch,config);
        elapsed(begin);
        if (result && result.result)
            add(diagnostics_->traversal_steps,
                result.result->traversal_statistics.traversal_steps);
        return result;
    }

private:
    using Clock=std::chrono::steady_clock;

    static void increment(std::uint64_t& value) noexcept {
        if (value!=UINT64_MAX) ++value;
    }
    static void add(std::uint64_t& value,const std::size_t amount) noexcept {
        std::uint64_t bounded{};
        if constexpr (sizeof(std::size_t)>sizeof(std::uint64_t))
            bounded=amount>static_cast<std::size_t>(UINT64_MAX)
                ? UINT64_MAX : static_cast<std::uint64_t>(amount);
        else bounded=static_cast<std::uint64_t>(amount);
        value=value>UINT64_MAX-bounded ? UINT64_MAX : value+bounded;
    }
    void elapsed(const Clock::time_point begin) const noexcept {
        const double milliseconds=
            std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        if (!std::isfinite(milliseconds) || milliseconds<0.0) return;
        const double maximum=(std::numeric_limits<double>::max)();
        diagnostics_->collision_query_ms=
            diagnostics_->collision_query_ms>maximum-milliseconds
                ? maximum : diagnostics_->collision_query_ms+milliseconds;
    }

    const ILocalMovementCollision& collision_;
    core::PredictionWorkDiagnostics* diagnostics_{};
};

} // namespace hlclient::goldsrc::movement
