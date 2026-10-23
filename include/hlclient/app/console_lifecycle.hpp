#pragma once
#include <hlclient/app/console_application.hpp>
#include <hlclient/core/log.hpp>
#ifdef _WIN32
#include <hlclient/app/console_session_attempt.hpp>
#endif
#include <functional>

namespace hlclient::app {
enum class ConsoleAttemptRetry {none,connect_response_timeout};
struct ConsoleAttemptOutcome {
    int code{};
    std::string detail;
    bool integrity_failure{};
    ConsoleAttemptRetry retry{ConsoleAttemptRetry::none};
};
using ConsoleAttemptRunner=std::function<ConsoleAttemptOutcome(core::CommandLineOptions&,ConsoleApplication&)>;

inline bool console_connection_retry_profile(const core::CommandLineOptions& options) noexcept {
    return options.stop_after==core::ConnectionStopPoint::live_visual_control &&
        options.live_input==core::LiveInputMode::keyboard_mouse &&
        options.authentication_provider==core::AuthenticationProviderKind::steam &&
        options.live_session_unlimited && !options.live_session_seconds;
}
inline std::chrono::milliseconds console_connect_response_wait(
    const core::CommandLineOptions& options,const std::chrono::milliseconds ordinary_wait) noexcept {
    // Keep late internet replies within their original socket's lifetime.
    // One-shot probes and finite/scripted harnesses retain their own deadline.
    constexpr std::chrono::milliseconds normal_wait{10'000};
    return console_connection_retry_profile(options) && ordinary_wait>std::chrono::milliseconds::zero() &&
        ordinary_wait<normal_wait ? normal_wait : ordinary_wait;
}

// Owns only application/attempt transitions. Network, game, audio and prediction
// are fresh stack owners in runner; none survive a return or exception here.
inline int run_console_lifecycle(ConsoleApplication& application,const bool start_idle,
    const ConsoleAttemptRunner& runner,const std::function<bool()>& native_cancel={},
    const std::optional<std::uint64_t> idle_frame_limit={},
    const std::function<void(const ConsoleAttemptOutcome&)>& attempt_closed={},
    const std::function<ConsoleApplication::Clock::time_point()>& retry_clock={}) {
    using RetryClock=ConsoleApplication::Clock;
    constexpr unsigned maximum_chain_attempts=3U;
    constexpr auto retry_delay=std::chrono::duration_cast<RetryClock::duration>(std::chrono::seconds{1});
    const auto retry_profile=&console_connection_retry_profile;
    struct PendingRetry {
        std::string endpoint;
        std::uint64_t request_revision{};
        unsigned attempt{};
        RetryClock::time_point due{};
    };
    std::optional<PendingRetry> pending_retry;
    std::optional<RetryClock::time_point> previous_retry_clock;
    const auto retry_now=[&]() -> std::optional<RetryClock::time_point> {
        try {
            const auto now=retry_clock?retry_clock():RetryClock::now();
            if(previous_retry_clock && now<*previous_retry_clock)return std::nullopt;
            previous_retry_clock=now;
            return now;
        } catch(...) {
            return std::nullopt;
        }
    };
    unsigned chain_attempt=1U;
    auto automatic=start_idle ? std::optional<std::string>{} : application.options().connect_endpoint;
#ifdef _WIN32
    ConsoleSessionAttempts attempts{application.options()};
#endif
    int result{},application_result{};std::uint64_t frames{};
    while(!application.shutdown_requested()) {
        if(native_cancel && native_cancel()){application.request_quit();break;}
        auto endpoint=application.take_connect();
        if(endpoint) {
            // An explicit connect/retry always wins over a queued automatic
            // attempt, including when the user chooses the same endpoint.
            pending_retry.reset();previous_retry_clock.reset();chain_attempt=1U;
        } else if(automatic) {
            endpoint=std::move(automatic);chain_attempt=1U;
        } else if(pending_retry) {
            if(application.session_request_revision()!=pending_retry->request_revision ||
                application.session_stop_requested() || !retry_profile(application.options())) {
                pending_retry.reset();
            } else {
                const auto now=retry_now();
                if(!now) {
                    pending_retry.reset();
                    application.model().append("Automatic connection retry cancelled: invalid clock.");
                } else if(*now>=pending_retry->due) {
                    endpoint=std::move(pending_retry->endpoint);
                    chain_attempt=pending_retry->attempt;
                    pending_retry.reset();
                }
            }
        }
        automatic.reset();
        if(endpoint) {
            const auto requested_endpoint=*endpoint;
            const auto request_revision=application.session_request_revision();
            const bool automatic_retry_eligible=retry_profile(application.options());
            auto& options=application.options();options.connect_endpoint=std::move(endpoint);options.open_client=false;
#ifdef _WIN32
            if(!options.no_session_reports) {
                auto scoped=attempts.prepare(options);
                if(!scoped){
                    core::log(core::LogLevel::error,std::string{"[console-application] "}+std::string{attempts.error()});
                    application_result=2;application.end_attempt(2,attempts.error());application.request_quit();break;
                }
                options.session_receipt_root=std::move(scoped->session_receipt_root);
                options.session_run_id=std::move(scoped->session_run_id);options.session_role=std::move(scoped->session_role);
            }
#endif
            application.begin_attempt();application.present_idle();
            ConsoleAttemptOutcome outcome;
            try {outcome=runner(options,application);}
            catch(const std::exception& error) {
                core::log(core::LogLevel::error,error.what());outcome={1,error.what()};
            } catch(...) {outcome={1,"Unknown session failure"};}
            application.set_session_actions({});result=outcome.code;
#ifdef _WIN32
            if(!options.no_session_reports &&
                !attempts.finish(result,result?"session_failed":"session_complete")) {
                result=2;outcome.detail=std::string{attempts.error()};outcome.integrity_failure=true;
                core::log(core::LogLevel::error,std::string{"[console-application] "}+outcome.detail);
                application.model().append(outcome.detail);application.request_quit();
            }
#endif
            outcome.code=result;application.end_attempt(result,outcome.detail);
            if(attempt_closed)attempt_closed(outcome);
            if(outcome.integrity_failure || (result && options.live_session_seconds)) {
                application_result=result ? result : 2;application.request_quit();
            }
            if(outcome.retry==ConsoleAttemptRetry::connect_response_timeout && result &&
                !outcome.integrity_failure && automatic_retry_eligible && retry_profile(options) &&
                !application.shutdown_requested() && !application.session_stop_requested() &&
                application.session_request_revision()==request_revision) {
                if(chain_attempt<maximum_chain_attempts) {
                    const auto now=retry_now();
                    if(now && *now<=RetryClock::time_point::max()-retry_delay) {
                        // finish() and end_attempt() have already retained the
                        // failed attempt. The next runner owns a new UDP/auth
                        // session; no connect bytes or stale replies survive.
                        pending_retry=PendingRetry{requested_endpoint,request_revision,
                            chain_attempt+1U,*now+retry_delay};
                        application.model().append("No connection reply. Retrying in 1 second (attempt "+
                            std::to_string(chain_attempt+1U)+" of "+std::to_string(maximum_chain_attempts)+
                            "). Use disconnect to cancel.");
                    } else {
                        application.model().append("Automatic connection retry cancelled: invalid clock.");
                    }
                } else {
                    application.model().append("Connection retry limit reached (3 attempts).");
                }
            }
            // The production runner also requests shutdown on actual duration
            // expiry. Earlier explicit disconnect/retry keeps the same window;
            // finite managed failures retain the existing terminal contract.
        } else {
            application.pump_idle();
            if(idle_frame_limit && ++frames>=*idle_frame_limit)application.request_quit();
        }
    }
#ifdef _WIN32
    if(!application.options().no_session_reports && !attempts.finalize()){
        core::log(core::LogLevel::error,std::string{"[console-application] "}+std::string{attempts.error()});
        application.model().append(attempts.error());return 2;
    }
#endif
    // A user closing a healthy idle/error window is an application success,
    // not retroactive acceptance of any failed session. Attempts retain their
    // exact result; persistence/native-close failures remain process failures.
    return application_result;
}
}
