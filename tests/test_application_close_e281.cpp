#include <hlclient/app/intentional_session_close.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

namespace {
struct Session {
    bool done{};unsigned requests{},updates{},cancels{},gameplay_actions{};
    bool accepts{true},finishes{true};
    bool terminal() const {return done;}
    bool request_graceful_close(std::chrono::steady_clock::time_point) {++requests;return accepts;}
    void update(std::chrono::steady_clock::time_point) {++updates;done=finishes;}
    void cancel(std::chrono::steady_clock::time_point) {++cancels;done=true;}
};
}
TEST_CASE("E28.1 application close fence is bounded and does not restart terminal sessions", "[e281][application-close][no-stock]") {
    Session session;auto time=std::chrono::steady_clock::time_point{};
    SECTION("silent transport gets one request and bounded pumping") {
        session.finishes=false;
        hlclient::app::finish_intentional_session_close(session,[&]{return time;},[&]{time+=std::chrono::milliseconds{20};});
        CHECK(session.requests==1);CHECK(session.updates<=80);CHECK(session.cancels==1);
        CHECK(time<=std::chrono::steady_clock::time_point{}+std::chrono::milliseconds{1600});
    }
    SECTION("even an unmoving clock cannot create an infinite loop") {
        session.finishes=false;
        hlclient::app::finish_intentional_session_close(session,[&]{return time;},[]{});
        CHECK(session.updates==80);CHECK(session.cancels==1);
    }
    SECTION("request rejection stays cancellation not claimed send") {
        session.accepts=false;
        hlclient::app::finish_intentional_session_close(session,[&]{return time;},[]{});
        CHECK(session.requests==1);CHECK(session.updates==0);CHECK(session.cancels==1);
    }
    SECTION("server-ended or fatal terminal state is untouched") {
        session.done=true;
        hlclient::app::finish_intentional_session_close(session,[&]{return time;},[]{});
        CHECK(session.requests==0);CHECK(session.updates==0);CHECK(session.cancels==0);
    }
    CHECK(session.gameplay_actions==0);CHECK(session.done);
}
TEST_CASE("E28.1 intentional close result cannot disguise forced cancellation as success", "[e281][application-close][no-stock]") {
    using namespace hlclient::app;
    SessionCloseReceipt close;close.reason=SessionCloseReason::window_close;
    close.requested=true;close.eligible=true;close.outcome=SessionCloseOutcome::cancelled;
    CHECK(session_close_failed(close));
    close.outcome=SessionCloseOutcome::pending;CHECK(session_close_failed(close));
    close.outcome=SessionCloseOutcome::not_requested;CHECK(session_close_failed(close));
    close.outcome=SessionCloseOutcome::sent_but_unconfirmed;
    CHECK(session_close_failed(close)); // no OS-send evidence is not a sent exit
    close.successful_send_count=1;CHECK_FALSE(session_close_failed(close));
    close.successful_send_count=0;close.server_disconnect_observed=true;
    close.outcome=SessionCloseOutcome::server_disconnect_confirmed;
    CHECK_FALSE(session_close_failed(close));
    close={};close.reason=SessionCloseReason::window_close;
    close.outcome=SessionCloseOutcome::cancel_without_protocol_disconnect;
    CHECK_FALSE(session_close_failed(close)); // early cancel still has no map-ready evidence
    close.outcome=SessionCloseOutcome::fatal_failure;CHECK(session_close_failed(close));
}
TEST_CASE("E28.1 application intentional close requests transport before cancellation",
    "[e281][application-close][no-stock]") {
    Session session;auto time=std::chrono::steady_clock::time_point{};
    hlclient::app::finish_intentional_session_close(session,[&]{return time;},[&]{time+=std::chrono::milliseconds{20};});
    CHECK(session.requests==1);CHECK(session.updates==1);CHECK(session.cancels==0);
    CHECK(session.gameplay_actions==0);CHECK(session.done);
}
