#pragma once
#include <chrono>
#include <hlclient/app/session_receipt.hpp>

namespace hlclient::app {

[[nodiscard]] inline bool session_close_failed(const SessionCloseReceipt& close) noexcept {
    using O=SessionCloseOutcome;
    if(close.outcome==O::send_failed || close.outcome==O::fatal_failure) return true;
    const bool intentional=close.reason==SessionCloseReason::window_close ||
        close.reason==SessionCloseReason::duration_limit ||
        close.reason==SessionCloseReason::console_disconnect;
    return intentional && (close.outcome==O::pending || close.outcome==O::cancelled ||
        close.outcome==O::not_requested ||
        (close.eligible && !close.successful_send_count && !close.server_disconnect_observed));
}

// Application close intent is kept separate from fatal/forced cancellation.
// Clock/pacing are supplied by the owning loop; no gameplay update is invoked.
template<class Session,class Now,class Pace>
void finish_intentional_session_close(Session& session,Now&& now,Pace&& pace)
{
    if(session.terminal()) return;
    const auto started=now();
    if(!session.request_graceful_close(started)) {
        if(!session.terminal()) session.cancel(now());
        return;
    }
    // The driver owns its 1500 ms deadline. This independent 1600 ms/80-turn fence
    // bounds an application adapter defect without extending that deadline.
    for(unsigned turns=0;!session.terminal() && turns<80U;++turns) {
        const auto current=now();
        if(current-started>=std::chrono::milliseconds{1600}) break;
        session.update(current); // close-only stage path; never gameplay
        if(!session.terminal()) pace();
    }
    if(!session.terminal()) session.cancel(now());
}

} // namespace hlclient::app
