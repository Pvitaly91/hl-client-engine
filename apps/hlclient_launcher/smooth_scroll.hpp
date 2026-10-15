#pragma once

#include <algorithm>
#include <cmath>

namespace hlclient::launcher {

// Launcher coordinates remain logical pixels until the Win32 layout converts
// the shared scroll offset to physical pixels. No input fractions are lost.
class SmoothScroll final {
public:
    void reset(double position, int maximum) noexcept
    {
        maximum_ = std::max(0, maximum);
        jump(position);
    }

    void jump(double position) noexcept
    {
        position_ = bounded(position);
        target_ = position_;
        start_position_ = position_;
        start_time_ = 0.0;
        last_time_ = 0.0;
    }

    void move_by(double delta, double now_seconds) noexcept
    {
        if (!std::isfinite(delta) || !std::isfinite(now_seconds) || delta == 0.0) return;
        if (active()) now_seconds = std::max(now_seconds, last_time_);
        static_cast<void>(advance(now_seconds));

        const double pending = target_ - position_;
        const bool reversing = (pending > 0.0 && delta < 0.0) ||
            (pending < 0.0 && delta > 0.0);
        const double next = bounded((reversing ? position_ : target_) + delta);
        // Extra input against a clamped endpoint must not postpone settling.
        if (next == target_) return;

        start_position_ = position_;
        target_ = next;
        start_time_ = now_seconds;
        last_time_ = now_seconds;
    }

    // Returns whether the logical position changed. Calling with an older
    // timestamp cannot move an in-flight animation backwards.
    [[nodiscard]] bool advance(double now_seconds) noexcept
    {
        if (!active() || !std::isfinite(now_seconds)) return false;
        now_seconds = std::max(now_seconds, last_time_);
        last_time_ = now_seconds;
        const double previous = position_;
        if (now_seconds >= start_time_ + duration_seconds) {
            position_ = target_;
        } else {
            const double t = std::clamp((now_seconds - start_time_) / duration_seconds, 0.0, 1.0);
            const double remaining = 1.0 - t;
            const double eased = 1.0 - remaining * remaining * remaining;
            position_ = std::clamp(start_position_ + (target_ - start_position_) * eased,
                std::min(start_position_, target_), std::max(start_position_, target_));
        }
        return position_ != previous;
    }

    [[nodiscard]] double position() const noexcept { return position_; }
    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] int maximum() const noexcept { return maximum_; }
    [[nodiscard]] bool active() const noexcept { return position_ != target_; }

private:
    [[nodiscard]] double bounded(double position) const noexcept
    {
        return std::clamp(std::isnan(position) ? 0.0 : position, 0.0, static_cast<double>(maximum_));
    }

    static constexpr double duration_seconds = 0.120;
    double position_{};
    double target_{};
    double start_position_{};
    double start_time_{};
    double last_time_{};
    int maximum_{};
};

} // namespace hlclient::launcher
