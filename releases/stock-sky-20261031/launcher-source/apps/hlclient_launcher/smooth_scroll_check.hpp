#pragma once

#include "smooth_scroll.hpp"

#include <array>
#include <cmath>

namespace hlclient::launcher {

// Deterministic checks shared by the hidden native UI check. They neither
// sleep nor create windows, save settings, or start a client session.
[[nodiscard]] inline bool smooth_scroll_check() noexcept
{
    const auto close_enough = [](double actual, double expected) noexcept {
        return std::abs(actual - expected) < 1.0e-7;
    };

    SmoothScroll scroll;
    if (scroll.position() != 0.0 || scroll.target() != 0.0 || scroll.maximum() != 0 || scroll.active()) return false;

    // A wheel notch split into small deltas retains exactly the same distance.
    for (const int count : std::array{1, 4, 12, 120}) {
        scroll.reset(100.0, 1000);
        for (int i = 0; i < count; ++i) scroll.move_by(48.0 / count, 0.0);
        if (!close_enough(scroll.target(), 148.0) || scroll.position() != 100.0 || !scroll.active()) return false;
        static_cast<void>(scroll.advance(0.120));
        if (!close_enough(scroll.position(), 148.0) || scroll.active()) return false;
    }

    scroll.reset(100.0, 1000);
    scroll.move_by(0.4, 0.0);
    if (!close_enough(scroll.target(), 100.4) || !scroll.active()) return false;
    static_cast<void>(scroll.advance(0.120));
    if (!close_enough(scroll.position(), 100.4) || scroll.active()) return false;

    // Cubic ease-out reaches its exact endpoint within 120 ms, never overshoots,
    // and does not need a timer after settling.
    scroll.reset(100.0, 1000);
    scroll.move_by(48.0, 0.0);
    if (scroll.advance(0.0)) return false;
    double previous = scroll.position();
    for (int frame = 1; frame <= 12; ++frame) {
        static_cast<void>(scroll.advance(static_cast<double>(frame) / 100.0));
        if (scroll.position() < previous || scroll.position() > 148.0) return false;
        if (frame == 6 && !close_enough(scroll.position(), 142.0)) return false;
        previous = scroll.position();
    }
    if (scroll.active() || scroll.position() != 148.0 || scroll.advance(1.0)) return false;

    // Same-direction input accumulates against the endpoint even while moving.
    scroll.reset(100.0, 1000);
    scroll.move_by(12.0, 0.0);
    static_cast<void>(scroll.advance(0.030));
    scroll.move_by(36.0, 0.030);
    if (scroll.target() != 148.0) return false;
    static_cast<void>(scroll.advance(0.150));
    if (scroll.position() != 148.0 || scroll.active()) return false;

    // Reversal discards forward travel still waiting in the target.
    scroll.reset(100.0, 1000);
    scroll.move_by(96.0, 0.0);
    static_cast<void>(scroll.advance(0.030));
    const double reversal_position = scroll.position();
    scroll.move_by(-12.0, 0.030);
    if (scroll.position() != reversal_position || !close_enough(scroll.target(), reversal_position - 12.0)) return false;
    previous = scroll.position();
    for (int frame = 1; frame <= 12; ++frame) {
        static_cast<void>(scroll.advance(0.030 + static_cast<double>(frame) / 100.0));
        if (scroll.position() > previous || scroll.position() < scroll.target()) return false;
        previous = scroll.position();
    }
    if (scroll.active() || !close_enough(scroll.position(), reversal_position - 12.0)) return false;

    // Excess wheel input at either boundary creates no hidden overscroll debt.
    scroll.reset(990.0, 1000);
    scroll.move_by(10000.0, 0.0);
    scroll.move_by(10000.0, 0.060);
    static_cast<void>(scroll.advance(0.120));
    if (scroll.position() != 1000.0 || scroll.target() != 1000.0 || scroll.active()) return false;
    scroll.move_by(-0.4, 0.120);
    if (!close_enough(scroll.target(), 999.6) || !scroll.active()) return false;
    static_cast<void>(scroll.advance(0.240));
    scroll.jump(10.0);
    scroll.move_by(-10000.0, 1.0);
    scroll.move_by(-10000.0, 1.060);
    static_cast<void>(scroll.advance(1.120));
    if (scroll.position() != 0.0 || scroll.target() != 0.0 || scroll.active()) return false;
    scroll.move_by(0.4, 1.120);
    if (!close_enough(scroll.target(), 0.4) || !scroll.active()) return false;

    // Resize/DPI/focus changes are immediate and cancel the pending animation.
    scroll.reset(500.0, 1000);
    scroll.move_by(200.0, 0.0);
    scroll.reset(scroll.position(), 300);
    if (scroll.position() != 300.0 || scroll.target() != 300.0 || scroll.maximum() != 300 || scroll.active()) return false;
    if (scroll.advance(1.0) || scroll.position() != 300.0) return false;
    scroll.reset(300.0, 1000);
    scroll.move_by(-100.0, 1.0);
    scroll.jump(125.5);
    if (scroll.position() != 125.5 || scroll.target() != 125.5 || scroll.active() || scroll.advance(2.0)) return false;
    scroll.jump(2000.0);
    if (scroll.position() != 1000.0 || scroll.active()) return false;
    scroll.jump(-10.0);
    if (scroll.position() != 0.0 || scroll.active()) return false;
    scroll.reset(100.0, -1);
    scroll.move_by(48.0, 0.0);
    if (scroll.maximum() != 0 || scroll.position() != 0.0 || scroll.target() != 0.0 || scroll.active()) return false;

    // A stale timestamp cannot reverse progress within the same animation.
    scroll.reset(100.0, 1000);
    scroll.move_by(48.0, 0.0);
    static_cast<void>(scroll.advance(0.060));
    previous = scroll.position();
    if (scroll.advance(0.030) || scroll.position() != previous) return false;
    static_cast<void>(scroll.advance(0.120));
    return scroll.position() == 148.0 && !scroll.active();
}

} // namespace hlclient::launcher


