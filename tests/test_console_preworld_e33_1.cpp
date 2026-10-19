#include <hlclient/app/console_steam_authentication.hpp>
#include <hlclient/app/console_application.hpp>
#include <hlclient/app/console_screen.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {
namespace app=hlclient::app;
namespace auth=hlclient::auth;
using namespace std::chrono_literals;
struct DelayedState {
    std::atomic<bool> entered{},release{};
    std::atomic<unsigned> terminated{},shutdown{};
    std::mutex mutex;
    std::vector<std::thread::id> owners;
    void owner(){std::lock_guard lock(mutex);owners.push_back(std::this_thread::get_id());}
};
struct DelayedApi final:app::ISteamLegacyClientApi {
    std::shared_ptr<DelayedState> state;
    explicit DelayedApi(std::shared_ptr<DelayedState> value):state(std::move(value)){state->owner();}
    ~DelayedApi()override{state->owner();}
    bool initialize()noexcept override{
        state->owner();state->entered=true;
        // Deterministic native-call gate, with a deadline to bound a failing
        // pre-fix synchronous call. No external Steam runtime is loaded.
        const auto deadline=std::chrono::steady_clock::now()+300ms;
        while(!state->release.load() && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        return true;
    }
    void shutdown()noexcept override{state->owner();++state->shutdown;}
    void run_callbacks()noexcept override{state->owner();}
    std::uint32_t app_id()const noexcept override{state->owner();return 70;}
    bool logged_on()const noexcept override{state->owner();return true;}
    int initiate_game_connection(std::span<std::byte> output,std::uint64_t,std::uint32_t,std::uint16_t,bool)noexcept override{
        state->owner();for(std::size_t i=0;i<32;++i)output[i]=std::byte{0x51};return 32;
    }
    void terminate_game_connection(std::uint32_t,std::uint16_t)noexcept override{state->owner();++state->terminated;}
};
auth::AuthenticationRequestContext context(){
    auth::AuthenticationRequestContext value;
    value.remote_endpoint=hlclient::network::NetworkAddress::loopback(27015);
    value.compatibility_profile=hlclient::goldsrc::steam_legacy_connect_profile();
    value.challenge=hlclient::goldsrc::ChallengeToken{123456789};
    value.game_server_steam_id=90071992547410001ULL;value.game_server_secure=true;return value;
}
app::SteamAuthenticationProviderConfig config(const std::shared_ptr<DelayedState>& state){
    app::SteamAuthenticationProviderConfig value;
    value.api_factory=[state](const auto&){return std::make_unique<DelayedApi>(state);};return value;
}
}

TEST_CASE("E33.1 preworld authentication poll leaves UI available during a delayed native call",
          "[e33][e33_1][console-preworld][auth][no-stock]") {
    const auto caller=std::this_thread::get_id();auto state=std::make_shared<DelayedState>();
    auto options=config(state);std::vector<std::thread::id> trace_owners;
    options.trace=[&](const auto&){trace_owners.push_back(std::this_thread::get_id());};
    auto provider=app::make_console_steam_authentication_provider(std::move(options));
    auto begun=provider->begin(context());REQUIRE(begun);
    const auto started=std::chrono::steady_clock::now();
    auto result=begun.operation->update();
    CHECK(std::chrono::steady_clock::now()-started<100ms);
    CHECK(result.state==auth::AuthenticationUpdateState::pending);
    const auto deadline=std::chrono::steady_clock::now()+2s;
    unsigned ui_pumps{};
    while(!state->entered.load() && std::chrono::steady_clock::now()<deadline){++ui_pumps;result=begun.operation->update();std::this_thread::yield();}
    REQUIRE(state->entered.load());
    // Console events can run while the actual provider is still inside init.
    ++ui_pumps;CHECK_FALSE(state->release.load());state->release=true;
    while(result.state==auth::AuthenticationUpdateState::pending && std::chrono::steady_clock::now()<deadline){
        ++ui_pumps;result=begun.operation->update();std::this_thread::yield();
    }
    REQUIRE(result.state==auth::AuthenticationUpdateState::succeeded);REQUIRE(result.session);
    CHECK(ui_pumps>0);auto material=result.session->take_material();REQUIRE(material);
    CHECK_FALSE(result.session->take_material());
    begun.operation.reset();provider.reset();CHECK(state->terminated.load()==0);
    result.session.reset();CHECK(state->terminated.load()==1);CHECK(state->shutdown.load()==1);
    REQUIRE_FALSE(state->owners.empty());
    for(const auto owner:state->owners){CHECK(owner==state->owners.front());CHECK(owner!=caller);}
    REQUIRE_FALSE(trace_owners.empty());for(const auto owner:trace_owners)CHECK(owner==caller);
}

TEST_CASE("E33.1 console accepts editing and commands throughout staged preworld loading",
          "[e33][e33_1][console-preworld][opengl]") {
    using Event=hlclient::input::InputEvent;using Key=hlclient::input::PhysicalKey;
    hlclient::core::CommandLineOptions options;options.video.width=640;options.video.height=480;options.video.vsync=false;
    double clock{};std::unique_ptr<app::ConsoleApplication> owner;
    try {
        owner=std::make_unique<app::ConsoleApplication>(options,
            [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme({},w,h,fraction,full);},
            app::ConsoleGameRegistration{},true,false,[&]{return clock;});
    }catch(const std::exception& error){
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed OpenGL startup capability unavailable");
        throw;
    }
    auto& ui=*owner;ui.begin_attempt();auto work=std::make_shared<app::LoadingWork>(ui.generation());ui.set_work(work);
    hlclient::input::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(Event::focus_gained());
    for(const auto stage:{app::LoadingStage::connecting,app::LoadingStage::authenticating,app::LoadingStage::signon,
        app::LoadingStage::resources,app::LoadingStage::map,app::LoadingStage::textures,app::LoadingStage::models,app::LoadingStage::scene}) {
        CAPTURE(static_cast<int>(stage));REQUIRE(work->progress(stage));ui.begin_frame(&tracker);
        if(!ui.console_active())REQUIRE(ui.route_event(Event::key_pressed(Key::grave),tracker));
        clock+=.25;ui.begin_frame(&tracker);REQUIRE(ui.console_active());
        ui.model().set_editor("echo Привіт");REQUIRE(ui.route_event(Event::key_pressed(Key::enter),tracker));ui.begin_frame(&tracker);
        CHECK(ui.model().lines().back()=="Привіт");
        ui.execute("cmdlist;hud_draw 0");ui.begin_frame(&tracker);CHECK_FALSE(ui.options().hud_draw);
        hlclient::renderer::RenderScene scene;ui.compose(scene);REQUIRE(scene.screen_ui);REQUIRE_FALSE(scene.screen_ui->commands.empty());
        ui.present_idle();
        REQUIRE(ui.route_event(Event::key_pressed(Key::grave),tracker));clock+=.25;ui.begin_frame(&tracker);CHECK_FALSE(ui.console_active());
    }
    ui.execute("disconnect");ui.begin_frame(&tracker);CHECK(ui.session_stop_requested());CHECK(work->cancelled.load());
    ui.end_attempt(2,"Synthetic connection failure");CHECK(ui.console_active());
    ui.begin_attempt();CHECK_FALSE(work->progress(app::LoadingStage::ready));
    const auto count=ui.model().lines().size();ui.model().set_editor("echo retained draft");
    ui.execute("echo new attempt");ui.begin_frame(&tracker);CHECK(ui.model().lines().size()>count);
    CHECK(ui.model().editor()=="echo retained draft");
    (void)tracker.publish_snapshot();tracker.end_frame();
}

TEST_CASE("E33.1 cancel returns immediately and native auth teardown keeps pumping the owning UI",
          "[e33][e33_1][console-preworld][auth][no-stock]") {
    auto state=std::make_shared<DelayedState>();unsigned teardown_pumps{};
    const auto caller=std::this_thread::get_id();
    auto provider=app::make_console_steam_authentication_provider(config(state),[&]{
        CHECK(std::this_thread::get_id()==caller);++teardown_pumps;state->release=true;
    });
    auto begun=provider->begin(context());REQUIRE(begun);(void)begun.operation->update();
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(!state->entered.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    REQUIRE(state->entered.load());
    const auto started=std::chrono::steady_clock::now();begun.operation->cancel();
    auto result=begun.operation->update();CHECK(std::chrono::steady_clock::now()-started<100ms);
    CHECK(result.state==auth::AuthenticationUpdateState::failed);REQUIRE(result.error);
    CHECK(result.error->code==auth::AuthenticationErrorCode::cancelled);CHECK_FALSE(result.session);
    begun.operation.reset();provider.reset();CHECK(teardown_pumps>0);
    CHECK(state->terminated.load()==0);CHECK(state->shutdown.load()==1);
    for(const auto owner:state->owners){CHECK(owner==state->owners.front());CHECK(owner!=caller);}
    // A later attempt has a distinct worker and may not receive old material.
    auto next=std::make_shared<DelayedState>();next->release=true;
    auto retry=app::make_console_steam_authentication_provider(config(next));
    auto operation=retry->begin(context());REQUIRE(operation);CHECK_FALSE(retry->begin(context()));
    auto retried=operation.operation->update();
    const auto retry_deadline=std::chrono::steady_clock::now()+2s;
    while(retried.state==auth::AuthenticationUpdateState::pending&&std::chrono::steady_clock::now()<retry_deadline){
        retried=operation.operation->update();std::this_thread::yield();
    }
    CHECK(retried.state==auth::AuthenticationUpdateState::succeeded);
}

TEST_CASE("E33.1 async auth preserves validation failure without initializing Steam",
          "[e33][e33_1][console-preworld][auth][no-stock]") {
    auto state=std::make_shared<DelayedState>();auto invalid=context();invalid.challenge.reset();
    auto provider=app::make_console_steam_authentication_provider(config(state));
    auto begun=provider->begin(invalid);REQUIRE(begun);
    auto result=begun.operation->update();const auto deadline=std::chrono::steady_clock::now()+2s;
    while(result.state==auth::AuthenticationUpdateState::pending&&std::chrono::steady_clock::now()<deadline){
        result=begun.operation->update();std::this_thread::yield();
    }
    CHECK(result.state==auth::AuthenticationUpdateState::failed);REQUIRE(result.error);
    CHECK(result.error->code==auth::AuthenticationErrorCode::configuration_error);
    CHECK_FALSE(state->entered.load());CHECK_FALSE(result.session);
}
