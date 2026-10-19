#include <hlclient/app/console_application.hpp>
#include <hlclient/app/console_screen.hpp>
#include <hlclient/games/halflife/console_commands.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>

namespace {
namespace app=hlclient::app;
namespace con=hlclient::console;
namespace input=hlclient::input;
using Event=input::InputEvent;
using Key=input::PhysicalKey;
std::unique_ptr<app::ConsoleApplication> make_application(double& clock,std::optional<std::string> startup_config={},bool diagnostics=false) {
    hlclient::core::CommandLineOptions options;options.video.width=640;options.video.height=480;
    options.video.vsync=false;
    options.movement_diagnostics=diagnostics;
    try {
        return std::make_unique<app::ConsoleApplication>(options,
            [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme({},w,h,fraction,full);},
            hlclient::games::halflife::register_console_commands,true,false,[&clock]{return clock;},
            [startup_config=std::move(startup_config)]{return startup_config;});
    }catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            return {};
        throw;
    }
}
bool output_has(app::ConsoleApplication& application,std::string_view text) {
    for(const auto& line:application.model().lines())if(line.find(text)!=std::string::npos)return true;
    return false;
}
TEST_CASE("B080 frame cap reports the applied software policy and actual limiter wait separately from VSync", "[b080][pacing][opengl]") {
    double clock{};
    {
        auto instance=make_application(clock,{},true);
        if(!instance)SKIP("Typed OpenGL startup capability unavailable");
        auto& ui=*instance;
        ui.execute("fps_max 144");ui.begin_frame();ui.pace();
        auto pacing=ui.pacing_statistics();
        CHECK(pacing.enabled);CHECK(pacing.requested_cap_hz==144);
        CHECK(pacing.effective_software_cap_hz==144);CHECK(pacing.observations==1);
        CHECK(pacing.sleep_calls<=1);CHECK(pacing.total_wait_us>=pacing.last_wait_us);
        CHECK(pacing.maximum_wait_us>=pacing.last_wait_us);
        ui.execute("fps_max 1");ui.begin_frame();
        CHECK(ui.pacing_statistics().effective_software_cap_hz==144);
        ui.execute("fps_max 0");ui.begin_frame();ui.pace();
        pacing=ui.pacing_statistics();CHECK(pacing.observations==2);
        CHECK(pacing.effective_software_cap_hz==0);CHECK(pacing.last_wait_us==0);
        ui.reset_pacing_statistics();pacing=ui.pacing_statistics();
        CHECK(pacing.observations==0);CHECK(pacing.sleep_calls==0);
        CHECK(pacing.total_wait_us==0);CHECK(pacing.maximum_wait_us==0);
        const auto video=ui.window().video_state();
        REQUIRE(video.actual_swap_interval);CHECK(*video.actual_swap_interval==0);
        CHECK_FALSE(video.vsync);
        if(video.display_refresh_hz)CHECK(*video.display_refresh_hz>0);
    }
    auto disabled=make_application(clock);
    REQUIRE(disabled);disabled->begin_frame();disabled->pace();
    CHECK_FALSE(disabled->pacing_statistics().enabled);
    CHECK(disabled->pacing_statistics().observations==0);
}
float backdrop_height(const hlclient::renderer::RenderScene& scene) {
    float height{};
    if(scene.screen_ui)for(const auto& command:scene.screen_ui->commands)
        if(const auto* image=std::get_if<hlclient::renderer::ScreenUiImage>(&command);image&&image->clip)
            height=std::max(height,image->clip->height);
    return height;
}
TEST_CASE("E33.1 automatic curtain is elapsed-time based and resets independently of manual console", "[e33][e33_1][loading][no-stock]") {
    for(const auto fps:{30,60,144,240}) {
        app::LoadingCurtain curtain;curtain.reset(0);curtain.advance(10);CHECK(curtain.fraction()==1);
        curtain.presented(10);
        for(int frame=1;double(frame)/fps<.18;++frame)curtain.advance(10+double(frame)/fps);
        curtain.advance(10.18);CHECK(curtain.fraction()==Catch::Approx(.5));
        curtain.presented(10.18);curtain.advance(10.27);CHECK(curtain.fraction()==Catch::Approx(.25));
        curtain.advance(20);CHECK(curtain.fraction()==0);
        curtain.reset(21);curtain.advance(22);CHECK(curtain.fraction()==1);
    }
    app::LoadingCurtain curtain;curtain.reset(1);curtain.presented(1);curtain.advance(1.09);
    const auto before=curtain.fraction();curtain.advance(.5);CHECK(curtain.fraction()==before);
    curtain.advance(std::numeric_limits<double>::quiet_NaN());CHECK(curtain.fraction()==before);
    con::ConsoleController manual;manual.set_open(true,0);manual.advance(.09);
    CHECK(manual.height_fraction()==Catch::Approx(.25));manual.set_open(false,.09);manual.advance(.135);
    CHECK(manual.height_fraction()==Catch::Approx(.125));manual.set_open(true,.135);manual.advance(.3);
    CHECK(manual.height_fraction()==Catch::Approx(.5));
}
TEST_CASE("E33.1 first scene retains loading curtain until successful presentation then retracts", "[e33][e33_1][loading][opengl]") {
    double clock{};auto instance=make_application(clock);if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;ui.begin_attempt();clock=1;ui.begin_frame();
    hlclient::renderer::RenderScene scene;ui.compose(scene,true);
    CHECK(backdrop_height(scene)==Catch::Approx(480)); // readiness candidate is not presentation success
    ui.in_game();ui.compose(scene,true);CHECK(backdrop_height(scene)==Catch::Approx(480));
    clock+=.09;ui.compose(scene,true);const auto moving=backdrop_height(scene);
    CHECK(moving>0);CHECK(moving<480);
    ui.in_game();ui.compose(scene,true);CHECK(backdrop_height(scene)==Catch::Approx(moving));
    clock+=1;ui.compose(scene,true);CHECK(backdrop_height(scene)==0);
    ui.end_attempt(0,"console_disconnect");ui.compose(scene);CHECK(backdrop_height(scene)==Catch::Approx(480));
    ui.begin_attempt();clock+=1;ui.begin_frame();ui.compose(scene,true);CHECK(backdrop_height(scene)==Catch::Approx(480));
    ui.end_attempt(2,"fixture connection failure");clock+=1;ui.compose(scene);CHECK(backdrop_height(scene)==Catch::Approx(480));
    CHECK(output_has(ui,"fixture connection failure"));
}
TEST_CASE("E33.1 default loading labels are English and every preworld stage accepts console data", "[e33][e33_1][loading][console-input][opengl]") {
    double clock{};auto instance=make_application(clock);if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;CHECK(output_has(ui,"console ready"));ui.begin_attempt();
    input::InputStateTracker tracker;tracker.begin_frame();
    for(const auto stage:{app::LoadingStage::idle,app::LoadingStage::connecting,app::LoadingStage::authenticating,
        app::LoadingStage::signon,app::LoadingStage::resources,app::LoadingStage::map,app::LoadingStage::textures,
        app::LoadingStage::models,app::LoadingStage::scene,app::LoadingStage::failed}) {
        const std::string label=app::loading_stage_label(stage);
        for(unsigned char byte:label)CHECK(byte<128);
        ui.set_stage(stage);if(!ui.console_active())CHECK(ui.route_event(Event::key_pressed(Key::grave),tracker));
        clock+=1;ui.begin_frame();ui.model().set_editor("echo Український текст");
        CHECK(ui.route_event(Event::key_pressed(Key::enter),tracker));ui.begin_frame();
        CHECK(output_has(ui,"Український текст"));
        ui.execute("cmdlist;cvarlist;hud_draw 0");ui.begin_frame();
        hlclient::renderer::RenderScene scene;ui.compose(scene);REQUIRE(scene.screen_ui);
        CHECK(hlclient::renderer::valid_screen_ui(*scene.screen_ui));
    }
    ui.model().set_editor("echo незавершений");ui.in_game();clock+=1;ui.begin_frame();
    CHECK(ui.console_active());CHECK(ui.model().editor()=="echo незавершений");
    hlclient::renderer::RenderScene scene;ui.compose(scene);CHECK(backdrop_height(scene)==Catch::Approx(240));
    (void)tracker.publish_snapshot();tracker.end_frame();
}
TEST_CASE("E33 application settings execute real backend mappings and refuse invalid values", "[e33][console-application][opengl]") {
    double clock{};auto instance=make_application(clock);if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;
    ui.execute("sensitivity 6;volume 0.357;hud_draw 0;crosshair 0;hud_fastswitch 1;hlc_gamma 1.25;hlc_brightness 0.1;hlc_contrast 1.2;hlc_hud_scale 1.5;fps_max 240;cl_showfps 1");
    ui.begin_frame();CHECK(ui.sensitivity_scale()==Catch::Approx(2));CHECK(ui.options().audio_volume==36);
    CHECK_FALSE(ui.options().hud_draw);CHECK_FALSE(ui.options().crosshair);CHECK(ui.options().hud_fastswitch);
    CHECK(ui.options().image.gamma==Catch::Approx(1.25));CHECK(ui.options().image.brightness==Catch::Approx(.1));
    CHECK(ui.options().image.contrast==Catch::Approx(1.2));CHECK_FALSE(ui.options().hud_scale.automatic);
    CHECK(ui.options().hud_scale.scale==Catch::Approx(1.5));
    ui.execute("volume 100;sensitivity -1;hlc_msaa 3;fps_max 1");ui.begin_frame();
    CHECK(ui.options().audio_volume==36);CHECK(ui.sensitivity_scale()==Catch::Approx(2));
    CHECK(output_has(ui,"Invalid value"));CHECK(output_has(ui,"Use 0,2,4,8"));
    ui.execute("volume;hlc_msaa;cmdlist;cvarlist;hlc_help volume;hlc_help;say no_server");ui.begin_frame();
    CHECK(output_has(ui,"applied=\"0.36\""));CHECK(output_has(ui,"default="));CHECK(output_has(ui,"not permitted"));
    ui.execute("hlc_msaa 0");ui.begin_frame();CHECK(ui.options().video.msaa_samples==0);
    ui.present_idle();ui.pace();ui.execute("hlc_msaa");ui.begin_frame();CHECK(output_has(ui,"applied=\"0\""));
}
TEST_CASE("E33 application keeps one window and history while fresh attempts cancel old jobs", "[e33][console-application][lifecycle][opengl]") {
    double clock{};auto instance=make_application(clock);if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;auto* window=&ui.window();
    ui.execute("connect 127.0.0.1:27016",con::ExecutionSource::server);ui.begin_frame();CHECK_FALSE(ui.take_connect());
    for(const auto endpoint:{"0.0.0.0:27016","239.0.0.1:27016","255.255.255.255:27016","127.0.0.1:0","127.0.0.1:65536"}) {
        ui.execute(std::string{"connect "}+endpoint);ui.begin_frame();CHECK_FALSE(ui.take_connect());
    }
    ui.execute("connect 127.0.0.1:27016");ui.begin_frame();const auto local_endpoint=ui.take_connect();REQUIRE(local_endpoint);CHECK(*local_endpoint=="127.0.0.1:27016");
    ui.begin_attempt();const auto first=ui.generation();auto old=std::make_shared<app::LoadingWork>(first);ui.set_work(old);
    REQUIRE(old->progress(app::LoadingStage::models,1,2));ui.begin_frame();
    ui.execute("disconnect");ui.begin_frame();CHECK(ui.session_stop_requested());CHECK(old->cancelled.load());
    ui.end_attempt(0,"console_disconnect");CHECK_FALSE(ui.shutdown_requested());CHECK(&ui.window()==window);
    ui.execute("retry");ui.begin_frame();CHECK(ui.take_connect()=="127.0.0.1:27016");ui.begin_attempt();CHECK(ui.generation()==first+1);
    app::LoadingPresentation visible;visible.begin(ui.generation());CHECK_FALSE(visible.observe(*old));
    ui.in_game();CHECK(ui.in_game_state());ui.end_attempt(2,"missing maps/fixture.bsp");
    CHECK(output_has(ui,"missing maps/fixture.bsp"));ui.execute("echo still_alive;clear;echo retained");ui.begin_frame();
    CHECK(output_has(ui,"retained"));CHECK_FALSE(output_has(ui,"still_alive"));
    ui.execute("quit");ui.begin_frame();CHECK(ui.shutdown_requested());
}
TEST_CASE("E33 console loopback opt-in does not relax LAN launcher endpoint policy", "[e33][console-endpoint]") {
    using hlclient::core::normalize_external_endpoint;
    CHECK_FALSE(normalize_external_endpoint("127.0.0.1:27016"));
    CHECK(normalize_external_endpoint("127.0.0.1:27016",true)=="127.0.0.1:27016");
    CHECK(normalize_external_endpoint("127.0.0.1",true)=="127.0.0.1:27015");
    CHECK(normalize_external_endpoint("192.168.0.104:27016",true)=="192.168.0.104:27016");
    for(const auto endpoint:{"0.0.0.0:27016","239.0.0.1:27016","255.255.255.255:27016","127.0.0.1:0","127.0.0.1:65536","127.0.0.1;quit"})
        CHECK_FALSE(normalize_external_endpoint(endpoint,true));
}
TEST_CASE("E33 console input releases attack and movement and never turns editor clicks into fire", "[e33][console-application][console-input][opengl]") {
    double clock{};auto instance=make_application(clock);if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;ui.begin_attempt();ui.in_game();clock=1;
    input::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(Event::focus_gained());
    tracker.apply_event(Event::capture_acquired());tracker.apply_event(Event::key_pressed(Key::w));
    tracker.apply_event(Event::mouse_button_pressed(input::PhysicalMouseButton::left));
    ui.execute("toggleconsole");ui.begin_frame(&tracker);CHECK(ui.console_active());
    CHECK(ui.route_event(Event::key_pressed(Key::w),tracker));
    CHECK(ui.route_event(Event::mouse_button_pressed(input::PhysicalMouseButton::left),tracker));
    auto snap=tracker.publish_snapshot();CHECK_FALSE(snap.key_held(Key::w));CHECK_FALSE(snap.mouse_button_held(input::PhysicalMouseButton::left));tracker.end_frame();
    tracker.begin_frame();clock=1.3;ui.begin_frame(&tracker);
    CHECK(ui.route_event(Event::text_input("echo Україна"),tracker));
    CHECK(ui.route_event(Event::key_pressed(Key::enter),tracker));
    const auto edit_snapshot=tracker.publish_snapshot();CHECK_FALSE(edit_snapshot.mouse_button_pressed(input::PhysicalMouseButton::left));tracker.end_frame();
    tracker.begin_frame();ui.begin_frame(&tracker);CHECK(output_has(ui,"Україна"));
    CHECK(ui.route_event(Event::key_pressed(Key::grave,true),tracker));CHECK(ui.console_active());
    CHECK(ui.route_event(Event::key_pressed(Key::escape),tracker));
    (void)tracker.publish_snapshot();tracker.end_frame();
    tracker.begin_frame();clock=2;ui.begin_frame(&tracker);CHECK_FALSE(ui.console_active());
    tracker.apply_event(Event::focus_gained());
    (void)ui.route_event(Event::key_pressed(Key::w),tracker);const auto resumed=tracker.publish_snapshot();CHECK(resumed.key_held(Key::w));tracker.end_frame();
    tracker.begin_frame();ui.begin_frame(&tracker);(void)ui.route_event(Event::focus_lost(),tracker);
    const auto unfocused=tracker.publish_snapshot();CHECK_FALSE(unfocused.key_held(Key::w));tracker.end_frame();
}
TEST_CASE("E33 coherent loading progress never publishes old or premature ready state", "[e33][loading]") {
    app::LoadingPresentation view;view.begin(2);app::LoadingWork old{1},current{2};
    REQUIRE(old.progress(app::LoadingStage::ready,3,3));CHECK_FALSE(view.observe(old));
    REQUIRE(current.progress(app::LoadingStage::models,3,3));REQUIRE(view.observe(current));CHECK(view.percent==99);
    current.revision.fetch_add(1);CHECK_FALSE(view.observe(current));current.revision.fetch_add(1);
    current.cancelled.store(true);CHECK_FALSE(current.progress(app::LoadingStage::ready,3,3));CHECK_FALSE(view.observe(current));
}
TEST_CASE("E33 automatic configuration restores controls but cannot connect disconnect or quit", "[e33][console-application][lifecycle][opengl]") {
    double clock{};
    auto instance=make_application(clock,"sensitivity 6; bind F +attack; echo settings_loaded; alias later \"connect 127.0.0.1:27016\"; alias later_quit quit; connect 127.0.0.1:27016; retry; disconnect; quit; exit; later; later_quit");
    if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;CHECK(ui.sensitivity_scale()==Catch::Approx(2));CHECK(output_has(ui,"settings_loaded"));
    CHECK(output_has(ui,"Session commands are not permitted in automatic startup configuration"));
    CHECK_FALSE(ui.take_connect());CHECK_FALSE(ui.shutdown_requested());CHECK_FALSE(ui.session_stop_requested());
    ui.execute("later");ui.begin_frame();const auto endpoint=ui.take_connect();REQUIRE(endpoint);CHECK(*endpoint=="127.0.0.1:27016");
    ui.begin_attempt();ui.in_game();clock=1;
    input::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(Event::focus_gained());tracker.apply_event(Event::capture_acquired());
    ui.begin_frame(&tracker);CHECK(ui.route_event(Event::key_pressed(Key::f),tracker));
    CHECK(tracker.publish_snapshot().mouse_button_held(input::PhysicalMouseButton::left));tracker.end_frame();
    ui.execute("later_quit");ui.begin_frame();CHECK(ui.shutdown_requested());
}
TEST_CASE("E33 same-batch disconnect rejects further inventory and held gameplay requests", "[e33][console-application][lifecycle][opengl]") {
    double clock{};auto instance=make_application(clock);if(!instance)SKIP("Typed OpenGL startup capability unavailable");
    auto& ui=*instance;ui.begin_attempt();ui.in_game();clock=1;
    unsigned calls{};app::ConsoleSessionActions actions;
    actions.select_slot=[&](unsigned){++calls;return true;};actions.cycle_inventory=[&](int){++calls;return true;};
    actions.last_inventory=[&]{++calls;return true;};actions.select_weapon=[&](auto){++calls;return true;};actions.request=[&](auto,auto){++calls;return true;};
    ui.set_session_actions(std::move(actions));
    input::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(Event::focus_gained());tracker.apply_event(Event::capture_acquired());
    ui.execute("disconnect;slot1;invnext;invprev;lastinv;weapon_9mmhandgun;kill;+attack;+forward");ui.begin_frame(&tracker);
    const auto snapshot=tracker.publish_snapshot();tracker.end_frame();CHECK(calls==0);CHECK(ui.session_stop_requested());
    CHECK_FALSE(snapshot.key_held(Key::w));CHECK_FALSE(snapshot.mouse_button_held(input::PhysicalMouseButton::left));
}
}
