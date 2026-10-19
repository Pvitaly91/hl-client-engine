#include <hlclient/app/console_lifecycle.hpp>
#include <hlclient/app/console_screen.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace {
namespace app=hlclient::app;
struct AttemptFiles {
    std::filesystem::path root;
    AttemptFiles() {
#ifdef _WIN32
        root=std::filesystem::temp_directory_path()/hlclient::platform::windows::new_external_run_id();
#else
        root=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
        std::filesystem::create_directory(root);
    }
    ~AttemptFiles(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
};
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>{file},std::istreambuf_iterator<char>{}};
}
TEST_CASE("E33 actual application loop preserves failed attempts and reuses UI for clean retry", "[e33][console-application][lifecycle][opengl]") {
    AttemptFiles files;
    hlclient::core::CommandLineOptions options;options.video.width=640;options.video.height=480;options.video.vsync=false;
    options.connect_endpoint="127.0.0.1:27016";options.session_role="A";
#ifdef _WIN32
    options.session_run_id=hlclient::platform::windows::external_utf8(files.root.filename().native());
    options.session_receipt_root=hlclient::platform::windows::external_utf8(files.root.native());
#endif
    std::unique_ptr<app::ConsoleApplication> ui;
    try {ui=std::make_unique<app::ConsoleApplication>(options,
        [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme({},w,h,fraction,full);},
        app::ConsoleGameRegistration{},true,false);
    } catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed OpenGL startup capability unavailable");
        throw;
    }
    auto* initial_window=&ui->window();auto* initial_renderer=&ui->renderer();
    bool retry=true,integrity_failure=false;
    SECTION("Failed connection can be retried in the same window"){}
    SECTION("Closing a recoverable error window is not an application failure"){retry=false;}
    SECTION("A durable-output failure cannot be masked by closing the window"){retry=false;integrity_failure=true;}
    ui->model().set_editor("echo remembered");(void)ui->model().submit();
    std::vector<std::string> ids;std::size_t calls{};
    std::shared_ptr<app::LoadingWork> old;
    const auto result=app::run_console_lifecycle(*ui,false,[&](auto& current,auto& application) {
        ++calls;CHECK(&application.window()==initial_window);CHECK(&application.renderer()==initial_renderer);
        CHECK(application.generation()==calls);CHECK_FALSE(application.in_game_state());
        ids.push_back(current.session_run_id.value_or("none"));
        if(calls==1) {
            old=std::make_shared<app::LoadingWork>(application.generation());application.set_work(old);
            REQUIRE(old->progress(app::LoadingStage::models,1,3));
            if(retry){application.execute("retry");application.begin_frame();CHECK(application.session_stop_requested());}
            else application.request_quit();
            return app::ConsoleAttemptOutcome{2,"Missing maps/offline-fixture.bsp",integrity_failure};
        }
        CHECK(old->cancelled.load());app::LoadingPresentation stage;stage.begin(application.generation());
        CHECK_FALSE(stage.observe(*old));
        application.model().history_previous();CHECK(application.model().editor()=="echo remembered");
        application.in_game();application.request_quit();return app::ConsoleAttemptOutcome{0,"session_complete"};
    },{},10);
    CHECK(result==(integrity_failure?2:0));CHECK(calls==(retry?2U:1U));REQUIRE(ids.size()==calls);
#ifdef _WIN32
    CHECK(ids[0]==*options.session_run_id);
    const auto first=read(files.root/"console-attempt-A-result.json");
    CHECK(first.find("\"result_code\":2")!=first.npos);
    if(retry) {
        CHECK(ids[0]!=ids[1]);
        const auto second=read(files.root/"attempts-A"/ids[1]/"console-attempt-A-result.json");
        CHECK(second.find("\"result_code\":0")!=second.npos);
    }
    const auto summary=read(files.root/"console-application-A-attempts.json");
    CHECK(summary.find(ids[0])!=summary.npos);CHECK(summary.find("session_failed")!=summary.npos);
    if(retry){CHECK(summary.find(ids[1])!=summary.npos);CHECK(summary.find("session_complete")!=summary.npos);}
#endif
}
TEST_CASE("E33 idle and native cancellation never instantiate a gameplay attempt", "[e33][console-application][lifecycle][opengl]") {
    hlclient::core::CommandLineOptions options;options.video.width=640;options.video.height=480;options.video.vsync=false;
    std::unique_ptr<app::ConsoleApplication> ui;
    try {ui=std::make_unique<app::ConsoleApplication>(options,app::ConsoleTheme{},app::ConsoleGameRegistration{},true,false);}
    catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed OpenGL startup capability unavailable");
        throw;
    }
    bool called{};
    CHECK(app::run_console_lifecycle(*ui,true,[&](auto&,auto&){called=true;return app::ConsoleAttemptOutcome{};},[]{return true;},1)==0);
    CHECK_FALSE(called);CHECK(ui->generation()==0);CHECK(ui->shutdown_requested());
}

hlclient::core::CommandLineOptions retry_options(const AttemptFiles& files) {
    hlclient::core::CommandLineOptions options;
    options.video.width=640;options.video.height=480;options.video.vsync=false;
    options.connect_endpoint="127.0.0.1:27016";options.session_role="A";
    options.stop_after=hlclient::core::ConnectionStopPoint::live_visual_control;
    options.authentication_provider=hlclient::core::AuthenticationProviderKind::steam;
    options.live_input=hlclient::core::LiveInputMode::keyboard_mouse;
    options.live_session_unlimited=true;
#ifdef _WIN32
    options.session_run_id=hlclient::platform::windows::external_utf8(files.root.filename().native());
    options.session_receipt_root=hlclient::platform::windows::external_utf8(files.root.native());
#else
    (void)files;
#endif
    return options;
}

std::unique_ptr<app::ConsoleApplication> retry_ui(const hlclient::core::CommandLineOptions& options,
    std::size_t& presentations) {
    try {
        return std::make_unique<app::ConsoleApplication>(options,
            [&](unsigned,unsigned,double,bool){++presentations;return hlclient::renderer::ScreenUiFrame{};},
            app::ConsoleGameRegistration{},true,false);
    } catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed OpenGL startup capability unavailable");
        throw;
    }
}

TEST_CASE("Normal console connect wait admits late replies without altering probe deadlines",
    "[e33][console-application][lifecycle][connect-retry]") {
    AttemptFiles files;auto options=retry_options(files);
    constexpr std::chrono::milliseconds ordinary{5'000};
    for(const bool external:{false,true}) {
        options.external_server_client=external;
        CHECK(app::console_connect_response_wait(options,ordinary)==std::chrono::seconds{10});
        CHECK(app::console_connect_response_wait(options,std::chrono::seconds{30})==std::chrono::seconds{30});
    }
    CHECK(app::console_connect_response_wait(options,std::chrono::milliseconds::zero())==std::chrono::milliseconds::zero());
    auto finite=options;finite.live_session_seconds=1;
    CHECK(app::console_connect_response_wait(finite,ordinary)==ordinary);
    auto probe=options;probe.stop_after=hlclient::core::ConnectionStopPoint::connect_response;
    CHECK(app::console_connect_response_wait(probe,ordinary)==ordinary);
    auto scripted=options;scripted.live_input=hlclient::core::LiveInputMode::scripted_check;
    CHECK(app::console_connect_response_wait(scripted,ordinary)==ordinary);
    auto file=options;file.authentication_provider=hlclient::core::AuthenticationProviderKind::file;
    CHECK(app::console_connect_response_wait(file,ordinary)==ordinary);
}

std::string retry_output(app::ConsoleApplication& ui) {
    std::string output;
    for(const auto& line:ui.model().visible_lines(1024,256)){output+=line;output+='\n';}
    return output;
}

TEST_CASE("Console connect timeouts retry fresh durable attempts without blocking the UI",
    "[e33][console-application][lifecycle][opengl][connect-retry]") {
    AttemptFiles files;auto options=retry_options(files);
    bool third_succeeds=false;
    SECTION("All three failures remain failures and the retry chain terminates"){}
    SECTION("Success on the third attempt terminates the retry chain"){third_succeeds=true;}
    SECTION("The normal external launcher also receives three bounded attempts"){options.external_server_client=true;}
    std::size_t presentations{};auto ui=retry_ui(options,presentations);
    using Clock=app::ConsoleApplication::Clock;
    auto now=Clock::time_point{std::chrono::seconds{100}};
    const auto start=now;
    auto* const window=&ui->window();auto* const renderer=&ui->renderer();
    std::size_t calls{},closed{},owners_alive{},owners_destroyed{},prior_presentations{};
    std::vector<Clock::time_point> started,finished;
    std::vector<std::string> ids;
    std::vector<std::shared_ptr<app::LoadingWork>> work;
    bool echo_queued=false;
    struct AttemptOwner {
        std::size_t& alive;std::size_t& destroyed;
        AttemptOwner(std::size_t& a,std::size_t& d):alive(a),destroyed(d){++alive;}
        ~AttemptOwner(){--alive;++destroyed;}
    };
    const auto result=app::run_console_lifecycle(*ui,false,[&](auto& current,auto& application){
        CHECK(owners_alive==0);CHECK(owners_destroyed==calls);
        AttemptOwner owner{owners_alive,owners_destroyed};
        ++calls;started.push_back(now);ids.push_back(current.session_run_id.value_or("none"));
        CHECK(&application.window()==window);CHECK(&application.renderer()==renderer);
        CHECK(application.generation()==calls);
        if(calls>1){
            CHECK(now-finished.back()>=std::chrono::seconds{1});
            CHECK(presentations>prior_presentations+1);
            CHECK(work.back()->cancelled.load());
            CHECK(retry_output(application).find("idle-pump-remains-responsive")!=std::string::npos);
        }
        work.push_back(std::make_shared<app::LoadingWork>(application.generation()));
        application.set_work(work.back());
        CHECK(work.back()->progress(app::LoadingStage::authenticating,0,1));
        prior_presentations=presentations;
        if(calls==3&&third_succeeds)return app::ConsoleAttemptOutcome{0,"session_complete"};
        return app::ConsoleAttemptOutcome{2,"connect_response_timed_out",false,
            app::ConsoleAttemptRetry::connect_response_timeout};
    },[&]{
        now+=std::chrono::milliseconds{100};
        if(calls==1&&!echo_queued){ui->execute("echo idle-pump-remains-responsive");echo_queued=true;}
        // Keep pumping well beyond another due time: a fourth attempt would be
        // observed, not hidden by quitting inside the third runner.
        return now-start>=std::chrono::seconds{6};
    },100,[&](const auto& outcome){
        CHECK(owners_alive==0);CHECK(owners_destroyed==calls);
        CHECK(work.back()->cancelled.load());
        CHECK(outcome.code==((calls==3&&third_succeeds)?0:2));
        finished.push_back(now);++closed;
    },[&]{return now;});
    CHECK(result==0);CHECK(calls==3);CHECK(closed==3);
    CHECK(owners_destroyed==3);CHECK(owners_alive==0);CHECK(echo_queued);
    REQUIRE(started.size()==3);REQUIRE(finished.size()==3);REQUIRE(ids.size()==3);
    const auto output=retry_output(*ui);
    CHECK(output.find("attempt 2 of 3")!=output.npos);
    CHECK(output.find("attempt 3 of 3")!=output.npos);
    CHECK((output.find("retry limit reached")!=output.npos)==!third_succeeds);
    CHECK(output.find("connect_response_timed_out")!=output.npos);
#ifdef _WIN32
    CHECK(ids[0]==*options.session_run_id);CHECK(ids[0]!=ids[1]);CHECK(ids[0]!=ids[2]);CHECK(ids[1]!=ids[2]);
    for(std::size_t i=0;i<ids.size();++i){
        const auto scope=i==0?files.root:files.root/"attempts-A"/ids[i];
        const auto receipt=read(scope/"console-attempt-A-result.json");
        CHECK(receipt.find("\"run_id\":\""+ids[i]+"\"")!=receipt.npos);
        CHECK(receipt.find((i==2&&third_succeeds)?"\"result_code\":0":"\"result_code\":2")!=receipt.npos);
        CHECK(receipt.find("\"gameplay_acceptance\":false")!=receipt.npos);
    }
    const auto summary=read(files.root/"console-application-A-attempts.json");
    for(const auto& id:ids)CHECK(summary.find(id)!=summary.npos);
    CHECK(std::distance(std::filesystem::directory_iterator{files.root/"attempts-A"},
        std::filesystem::directory_iterator{})==2);
#endif
}

TEST_CASE("Console automatic retries require a typed timeout and the normal interactive profile",
    "[e33][console-application][lifecycle][opengl][connect-retry]") {
    AttemptFiles files;auto options=retry_options(files);
    app::ConsoleAttemptOutcome outcome{2,"connect_response_timed_out",false,
        app::ConsoleAttemptRetry::connect_response_timeout};
    bool cancel_in_runner=false,throws=false;
    int expected_result=0;
    SECTION("An identical untyped message is not retry authority"){outcome.retry=app::ConsoleAttemptRetry::none;}
    SECTION("Explicit server authentication rejection is not retried"){outcome={2,"server_authentication_rejected"};}
    SECTION("A protocol failure is not retried"){outcome={2,"response_stage_failed"};}
    SECTION("A send failure is not retried"){outcome={2,"connect_request_send_failed"};}
    SECTION("Cancelled attempts are not retried"){outcome={2,"session_cancelled"};}
    SECTION("Success is not retried even with an erroneous timeout hint"){outcome.code=0;}
    SECTION("Integrity failure is terminal even with a timeout hint"){outcome.integrity_failure=true;expected_result=2;}
    SECTION("Runner exception text does not become retry authority"){throws=true;}
    SECTION("Explicit disconnect during the failed attempt cancels a typed timeout"){cancel_in_runner=true;}
    SECTION("Finite sessions preserve their terminal result"){options.live_session_seconds=1;expected_result=2;}
    SECTION("Non-unlimited sessions do not automatically retry"){options.live_session_unlimited=false;}
    SECTION("One-shot protocol probes do not automatically retry"){options.stop_after=hlclient::core::ConnectionStopPoint::connect_response;}
    SECTION("Scripted input does not automatically retry"){options.live_input=hlclient::core::LiveInputMode::scripted_check;}
    SECTION("File authentication does not automatically retry"){options.authentication_provider=hlclient::core::AuthenticationProviderKind::file;}
    std::size_t presentations{},calls{},clock_reads{};auto ui=retry_ui(options,presentations);
    using Clock=app::ConsoleApplication::Clock;
    auto now=Clock::time_point{std::chrono::seconds{100}};const auto start=now;
    const auto result=app::run_console_lifecycle(*ui,false,[&](auto&,auto& application)->app::ConsoleAttemptOutcome{
        ++calls;
        if(cancel_in_runner){application.execute("disconnect");application.begin_frame();}
        if(throws)throw std::runtime_error{"connect_response_timed_out"};
        return outcome;
    },[&]{now+=std::chrono::milliseconds{250};return now-start>=std::chrono::seconds{5};},50,{},
        [&]{++clock_reads;return now;});
    CHECK(result==expected_result);CHECK(calls==1);CHECK(clock_reads==0);
    CHECK(retry_output(*ui).find("Retrying in 1 second")==std::string::npos);
#ifdef _WIN32
    CHECK_FALSE(std::filesystem::exists(files.root/"attempts-A"));
#endif
}

TEST_CASE("Console local requests override or cancel a pending retry before its deadline",
    "[e33][console-application][lifecycle][opengl][connect-retry]") {
    AttemptFiles files;const auto options=retry_options(files);
    std::string command="disconnect";bool override=false,quit=false;
    SECTION("Disconnect cancels the pending automatic attempt"){}
    SECTION("Quit cancels the pending automatic attempt"){command="quit";quit=true;}
    SECTION("A new endpoint immediately supersedes the pending endpoint and resets the chain"){
        command="connect 127.0.0.1:27017";override=true;
    }
    SECTION("Manual retry of the same endpoint starts a new bounded chain"){
        command="retry";override=true;
    }
    std::size_t presentations{},calls{};auto ui=retry_ui(options,presentations);
    using Clock=app::ConsoleApplication::Clock;
    auto now=Clock::time_point{std::chrono::seconds{100}};const auto start=now;
    std::vector<Clock::time_point> started;std::vector<std::string> endpoints;
    bool request_queued=false;std::uint64_t revision_before{};
    const auto result=app::run_console_lifecycle(*ui,false,[&](auto& current,auto&){
        ++calls;started.push_back(now);endpoints.push_back(current.connect_endpoint.value_or("none"));
        return app::ConsoleAttemptOutcome{2,"connect_response_timed_out",false,
            app::ConsoleAttemptRetry::connect_response_timeout};
    },[&]{
        now+=std::chrono::milliseconds{100};
        if(calls==1&&!request_queued){
            revision_before=ui->session_request_revision();ui->execute(command);request_queued=true;
            // Deliberately do not call begin_frame here: the normal idle pump
            // must accept the local request while waiting for its retry due.
        }
        return now-start>=std::chrono::seconds{6};
    },100,{},[&]{return now;});
    CHECK(result==0);CHECK(request_queued);CHECK(ui->session_request_revision()>revision_before);
    CHECK(calls==(override?4U:1U));
    if(override){
        REQUIRE(started.size()==4);REQUIRE(endpoints.size()==4);
        CHECK(started[1]-started[0]<std::chrono::seconds{1});
        CHECK(started[2]-started[1]>=std::chrono::seconds{1});
        CHECK(started[3]-started[2]>=std::chrono::seconds{1});
        CHECK(endpoints[0]=="127.0.0.1:27016");
        const auto expected=command=="retry"?"127.0.0.1:27016":"127.0.0.1:27017";
        for(std::size_t i=1;i<endpoints.size();++i)CHECK(endpoints[i]==expected);
        CHECK(retry_output(*ui).find("retry limit reached")!=std::string::npos);
    }
    if(quit)CHECK(now-start<std::chrono::seconds{1});
}

TEST_CASE("Console retry clock failures cancel the pending chain without a busy loop",
    "[e33][console-application][lifecycle][opengl][connect-retry]") {
    AttemptFiles files;const auto options=retry_options(files);
    enum class Fault {backwards,throwing,overflow};Fault fault=Fault::backwards;
    SECTION("A backwards retry clock cancels the pending chain"){}
    SECTION("A throwing retry clock cancels the pending chain"){fault=Fault::throwing;}
    SECTION("A retry deadline overflow is not scheduled"){fault=Fault::overflow;}
    std::size_t presentations{},calls{},clock_reads{},polls{};auto ui=retry_ui(options,presentations);
    using Clock=app::ConsoleApplication::Clock;
    const auto result=app::run_console_lifecycle(*ui,false,[&](auto&,auto&){
        ++calls;return app::ConsoleAttemptOutcome{2,"connect_response_timed_out",false,
            app::ConsoleAttemptRetry::connect_response_timeout};
    },[&]{return ++polls>8;},20,{},[&]() -> Clock::time_point {
        ++clock_reads;
        if(fault==Fault::overflow)return Clock::time_point::max();
        if(clock_reads>1&&fault==Fault::throwing)throw std::runtime_error{"offline clock fixture"};
        return Clock::time_point{std::chrono::seconds{clock_reads==1?100:99}};
    });
    CHECK(result==0);CHECK(calls==1);CHECK(clock_reads==(fault==Fault::overflow?1U:2U));
    CHECK(presentations>1);CHECK(retry_output(*ui).find("invalid clock")!=std::string::npos);
}
}
