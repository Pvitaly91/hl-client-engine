#include <hlclient/app/console_application.hpp>
#include <hlclient/app/console_screen.hpp>
#include <hlclient/core/log.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/text_messages.hpp>
#include <hlclient/goldsrc/disconnect_request.hpp>
#include "hud_resources_test_fixture_e29.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
namespace api=hlclient::game_api;
namespace core=hlclient::core;
namespace app=hlclient::app;
using Bytes=std::vector<std::byte>;
Bytes strings(std::initializer_list<std::string_view> values) {
    Bytes result;
    for(const auto value:values) {
        for(const unsigned char c:value)result.push_back(std::byte{c});
        result.push_back(std::byte{});
    }
    return result;
}
struct Message {
    std::string name;
    Bytes bytes;
    api::GameMessageKind kind{api::GameMessageKind::user_message};
    std::optional<api::PlayerDisplayUpdate> player;
};
Message death(unsigned killer,unsigned victim,std::string_view weapon="9mmhandgun") {
    auto bytes=strings({weapon});bytes.insert(bytes.begin(),{std::byte(killer),std::byte(victim)});
    return {"DeathMsg",std::move(bytes)};
}
Message player(unsigned slot,std::string name,bool replacement=false,bool present=true) {
    return {"",{},api::GameMessageKind::player_display,api::PlayerDisplayUpdate{slot,present,replacement,std::move(name)}};
}
Message text(unsigned destination,std::initializer_list<std::string_view> values) {
    auto bytes=strings(values);bytes.insert(bytes.begin(),std::byte(destination));return {"TextMsg",std::move(bytes)};
}
Message say(std::string_view value) {
    auto bytes=strings({value});bytes.insert(bytes.begin(),std::byte{1});return {"SayText",std::move(bytes)};
}
struct Fixture {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    hlclient::client::RuntimeClientObservationState observation;
    std::uint64_t ordinal{1};bool committed{};
    Fixture(){core::initialize_logging();core::reset_console_log();observation.generation=1;host.reset({1,1,1});}
    ~Fixture(){core::reset_console_log();}
    api::GameRecordResult stage(const std::vector<Message>& messages) {
        std::vector<api::GameMessageView> views;
        for(std::size_t i=0;i<messages.size();++i) {
            const auto& message=messages[i];views.push_back({message.kind,message.name,message.bytes,
                {ordinal,static_cast<std::size_t>(ordinal),static_cast<std::uint32_t>(ordinal),i*4096,(i+1)*4096},
                std::uint8_t{201},message.player});
        }
        ++ordinal;
        return host.stage_record({committed?&observation:nullptr,observation,views,1,4,{},10.0});
    }
    api::GameRecordState apply(const std::vector<Message>& messages) {
        auto staged=stage(messages);INFO(staged.error.value_or(""));REQUIRE(staged);
        auto copy=*staged.state;observation.weapon_hud=copy.weapon_hud;observation.lifecycle=copy.lifecycle;
        host.commit_record(std::move(*staged.state));committed=true;return copy;
    }
    std::vector<std::string> drain() {
        std::vector<std::string> result;
        for(auto& record:core::drain_console_log(512).records)result.push_back(std::move(record.message));
        return result;
    }
};
bool has(const std::vector<std::string>& lines,std::string_view text) {
    return std::any_of(lines.begin(),lines.end(),[&](const auto& line){return line.find(text)!=line.npos;});
}
}

TEST_CASE("E33.2 committed deaths publish pinned console sentences independently of HUD notices","[e33][e33_2][e14][console][no-stock]") {
    Fixture f;
    f.apply({player(1,"Alice"),player(2,"Bob"),death(1,2),death(1,2,"egon"),death(1,2,"gauss"),
        death(0,2,"world"),death(2,2,"grenade"),death(1,2,"teammate"),death(1,255,"sentrygun"),
        death(2,2,"teammate"),death(1,2,"")});
    const auto lines=f.drain();REQUIRE(lines.size()==9);
    CHECK(has(lines,"Alice killed Bob with 9mmhandgun"));CHECK(has(lines,"Alice killed Bob with gluon gun"));
    CHECK(has(lines,"Alice killed Bob with tau cannon"));CHECK(has(lines,"Bob died"));
    CHECK(has(lines,"Bob killed self with grenade"));CHECK(has(lines,"Alice killed his teammate Bob"));
    CHECK(has(lines,"Alice killed a sentrygun"));
    CHECK(std::find(lines.begin(),lines.end(),"[game] Bob killed self")!=lines.end());
    CHECK(std::find(lines.begin(),lines.end(),"[game] Alice killed Bob with ")!=lines.end());
    f.host.set_hud_context({1280,720,false,false});(void)f.host.hud(f.observation,100);
    CHECK(f.drain().empty());
}
TEST_CASE("E33.2 event-time names survive rename departure slot reuse and HUD overflow","[e33][e33_2][e14][console][no-stock]") {
    Fixture f;std::vector<Message> messages{player(1,"Before"),player(2,"Victim")};
    for(unsigned i=0;i<12;++i)messages.push_back(death(1,2));
    messages.push_back(player(1,"After"));messages.push_back(death(1,2));
    messages.push_back(player(2,"",false,false));messages.push_back(player(2,"New",true));
    messages.push_back(death(1,2));
    const auto state=f.apply(messages);const auto lines=f.drain();REQUIRE(lines.size()==14);
    CHECK(std::count_if(lines.begin(),lines.end(),[](const auto& s){return s.find("Before killed Victim")!=s.npos;})==12);
    CHECK(has(lines,"After killed Victim"));CHECK(has(lines,"After killed New"));
    CHECK(std::count_if(state.multiplayer.notices.begin(),state.multiplayer.notices.end(),[](auto& v){return bool(v);})==4);
    f.apply({death(3,4,"unknown_%n;exec")});CHECK(has(f.drain(),"Player 3 killed Player 4 with unknown_%n;exec"));
}
TEST_CASE("E33.2 server text uses real destinations and preserves committed stream order","[e33][e33_2][console][no-stock]") {
    Fixture f;
    f.apply({player(1,"Alice"),player(2,"Bob"),say("\x02Привіт"),death(1,2),
        text(2,{"Visible console"}),text(1,{"Bob has joined the game\n"}),
        text(4,{"Center-only marker"}),text(3,{"Talk marker"}),say("- Bob has left the game\n"),
        {"svc_print",strings({"Server print marker"}),api::GameMessageKind::console_text}});
    const auto lines=f.drain();REQUIRE(lines.size()==7);
    CHECK(lines[0].find("Привіт")!=lines[0].npos);CHECK(lines[1].find("Alice killed Bob")!=lines[1].npos);
    CHECK(has(lines,"Visible console"));CHECK(has(lines,"has joined the game"));CHECK_FALSE(has(lines,"Center-only marker"));
    CHECK(has(lines,"Talk marker"));CHECK(has(lines,"has left the game"));CHECK(has(lines,"Server print marker"));
    f.apply({player(3,"Initial"),player(3,"Renamed"),player(3,"",false,false),{"InitHUD",{}}});
    CHECK(f.drain().empty());
}
TEST_CASE("E33.2 rejected transactions publish nothing and committed identities deduplicate without text hashing","[e33][e33_2][console][no-stock]") {
    Fixture f;f.apply({player(1,"Alice"),player(2,"Bob")});CHECK(f.drain().empty());
    auto invalid=text(1,{"truncated"});invalid.bytes.pop_back();
    CHECK_FALSE(f.stage({death(1,2),say("not committed"),invalid}));CHECK(f.drain().empty());
    const auto copy=f.apply({death(1,2),say("same"),say("same")});CHECK(f.drain().size()==3);
    auto duplicate=copy;f.host.commit_record(std::move(duplicate));CHECK(f.drain().empty());
    f.host.teardown();auto stale=copy;f.host.commit_record(std::move(stale));CHECK(f.drain().empty());
}
TEST_CASE("E33.2 committed events append to normal application console with closed UI and hud_draw zero","[e33][e33_2][console][opengl][no-stock]") {
    Fixture f;double clock{};hlclient::core::CommandLineOptions options;options.video.width=640;options.video.height=480;
    std::unique_ptr<app::ConsoleApplication> instance;
    try {instance=std::make_unique<app::ConsoleApplication>(options,
        [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme({},w,h,fraction,full);},
        app::ConsoleGameRegistration{},true,false,[&]{return clock;});}
    catch(const std::exception& e) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(e)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed OpenGL startup capability unavailable");
        throw;
    }
    auto& ui=*instance;ui.begin_attempt();ui.in_game();ui.execute("hud_draw 0");ui.begin_frame();
    CHECK_FALSE(ui.console_active());f.apply({player(1,"Alice"),player(2,"Bob"),death(1,2,"gauss")});
    ui.begin_frame();CHECK_FALSE(ui.options().hud_draw);
    const auto count=[&] {return std::count_if(ui.model().lines().begin(),ui.model().lines().end(),
        [](const auto& s){return s.find("Alice killed Bob with tau cannon")!=s.npos;});};
    CHECK(count()==1);clock=100;f.host.set_hud_context({640,480,false,false});(void)f.host.hud(f.observation,clock);
    ui.begin_frame();CHECK(count()==1);
    ui.execute("clear");ui.begin_frame();CHECK(count()==0);f.host.set_hud_context({640,480,true});
    (void)f.host.hud(f.observation,clock+1);ui.begin_frame();CHECK(count()==0);
    f.apply({death(1,2,"gauss")});ui.begin_frame();CHECK(count()==1);
    hlclient::input::InputStateTracker tracker;tracker.begin_frame();
    CHECK(ui.route_event(hlclient::input::InputEvent::key_pressed(hlclient::input::PhysicalKey::grave),tracker));
    clock+=1;ui.begin_frame();hlclient::renderer::RenderScene scene;ui.compose(scene,true);
    REQUIRE(scene.screen_ui);CHECK(hlclient::renderer::valid_screen_ui(*scene.screen_ui));
    bool drawn{};
    for(const auto& command:scene.screen_ui->commands)if(const auto* row=std::get_if<hlclient::renderer::ScreenUiText>(&command))
        if(row->text.find("Alice killed Bob with tau cannon")!=row->text.npos) {CHECK(row->clip.has_value());drawn=true;}
    CHECK(drawn);
    const hlclient::renderer::RenderExtent extent{640,480};ui.renderer().render(scene,extent);
    const auto pixels=ui.renderer().observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(pixels);CHECK(pixels.non_clear_pixel_count>0);
    const auto screenshot_dir=hlclient::tests::hud_resources_e29::environment_value("HLCLIENT_E33_SCREENSHOT_DIR");
    if(!screenshot_dir.empty()) {
        std::ofstream image(std::filesystem::path{screenshot_dir}/"e332-committed-console-640x480.ppm",std::ios::binary);
        REQUIRE(image.good());image<<"P6\n640 480\n255\n";
        for(std::size_t y=480;y>0;--y)for(std::size_t x=0;x<640;++x)
            image.write(reinterpret_cast<const char*>(pixels.rgba8.data()+((y-1)*640+x)*4),3);
        REQUIRE(image.good());
    }
    auto without=scene;
    auto& commands=without.screen_ui->commands;
    commands.erase(std::remove_if(commands.begin(),commands.end(),[](const auto& command) {
        const auto* row=std::get_if<hlclient::renderer::ScreenUiText>(&command);
        return row && row->text.find("Alice killed Bob with tau cannon")!=row->text.npos;
    }),commands.end());
    ui.renderer().render(without,extent);
    const auto baseline=ui.renderer().observe_framebuffer(extent,without.clear_color,true);REQUIRE(baseline);
    std::size_t changed{};
    for(std::size_t at=0;at<pixels.rgba8.size();at+=4) {
        if(!std::equal(pixels.rgba8.begin()+at,pixels.rgba8.begin()+at+3,baseline.rgba8.begin()+at)) {
            ++changed;const auto y=479U-(at/4U)/640U;CHECK(y<240U);
        }
    }
    CHECK(changed>100U);
    std::cout<<"[e332-framebuffer] committed_death_glyph_pixels="<<changed<<" outside_half_panel=0\n";
    core::log_server_text("Connection rejected: fixture reason",core::LogLevel::error);
    ui.end_attempt(2,"Local timeout fixture");f.host.teardown();ui.begin_frame();
    CHECK(std::any_of(ui.model().lines().begin(),ui.model().lines().end(),[](const auto& line){return line.find("fixture reason")!=line.npos;}));
    CHECK(std::any_of(ui.model().lines().begin(),ui.model().lines().end(),[](const auto& line){return line.find("Local timeout fixture")!=line.npos;}));
    (void)tracker.publish_snapshot();tracker.end_frame();
}
TEST_CASE("E33.2 titles lookup safely formats short TextMsg optional parameters and destination overrides","[e33][e33_2][console][no-stock]") {
    Fixture f;using D=api::TextDestination;
    REQUIRE(f.host.bind_text_messages({{"JOIN","%s has joined the game\n",D::notify},
        {"NAME","Український гравець\n",{}},{"CENTER","Center token",D::center},
        {"CONSOLE","Console override",D::console},{"CYCLE","#CYCLE",{}}}));
    f.apply({text(2,{"#JOIN","#NAME"}),text(4,{"#CONSOLE"}),text(2,{"#CENTER"}),
        text(2,{"#UNKNOWN","A"}),text(2,{"literal %n %d %s %%","%n;quit"}),text(2,{"short"}),
        text(2,{"%s%s%s%s","1","2","3","4"}),text(2,{"#CYCLE"})});
    const auto output=f.drain();REQUIRE(output.size()==7);
    CHECK(has(output,"Український гравець has joined the game"));CHECK(has(output,"Console override"));
    CHECK_FALSE(has(output,"Center token"));CHECK(has(output,"#UNKNOWN [A]"));
    CHECK(has(output,"literal %n %d %n;quit %"));CHECK(has(output,"short"));CHECK(has(output,"1234"));CHECK(has(output,"#CYCLE"));
    f.host.set_hud_context({640,480,false,true});const auto hud=f.host.hud(f.observation,10);
    CHECK_FALSE(hud.draw_rejected);
    CHECK(std::any_of(hud.draw.texts.begin(),hud.draw.texts.end(),[](const auto& row){return row.text=="Center token";}));
    CHECK(f.drain().empty());
    auto too_many=text(2,{"%s","1","2","3","4","5"});CHECK_FALSE(f.stage({too_many}));
    auto truncated=text(2,{"main","parameter"});truncated.bytes.pop_back();CHECK_FALSE(f.stage({truncated}));CHECK(f.drain().empty());
    CHECK_FALSE(f.host.bind_text_messages({{"../unsafe","text",{}}}));
}
TEST_CASE("E33.2 bounded titles parser rejects incomplete duplicate and oversized catalogues atomically","[e33][e33_2][console][titles][no-stock]") {
    namespace gs=hlclient::goldsrc;
    const auto parse=[](std::string_view value){return gs::parse_text_message_catalogue(std::as_bytes(std::span{value.data(),value.size()}));};
    const auto catalogue=parse("// owned\r\n$effect -1\r\nJOIN\r\n{\r\n%s joined\r\n}\r\n$effect 2\nOTHER\n{\nLiteral\n}\n");
    REQUIRE(catalogue);REQUIRE(catalogue->size()==2);CHECK((*catalogue)[0].destination==api::TextDestination::notify);
    CHECK((*catalogue)[0].text=="%s joined\n");CHECK_FALSE((*catalogue)[1].destination);
    for(const auto value:{"A\n{\nx", "A\n{\nx\n}\nA\n{\ny\n}","$effect -5\nA\n{\nx\n}",
        "../file\n{\nx\n}","A\nB\n{\nx\n}","$effect %n\nA\n{\nx\n}"})CHECK_FALSE(parse(value));
    CHECK_FALSE(parse(std::string(api::kMaximumTextMessageBytes+1,'x')));
    CHECK_FALSE(parse(std::string{"A\0B",3}));
}
TEST_CASE("E33.2 owning initialization payload holds console publication until every subrecord succeeds","[e33][e33_2][console][transaction][no-stock]") {
    Fixture f;f.apply({player(1,"Alice"),player(2,"Bob")});
    {
        api::GameClientHost::ConsolePublicationBatch rejected{&f.host};
        f.apply({death(1,2)});f.apply({say("discarded prefix")});CHECK(f.drain().empty());
        auto truncated=text(2,{"bad suffix"});truncated.bytes.pop_back();CHECK_FALSE(f.stage({truncated}));
    }
    CHECK(f.drain().empty());
    {
        api::GameClientHost::ConsolePublicationBatch accepted{&f.host};
        f.apply({death(1,2)});f.apply({say("accepted suffix")});CHECK(f.drain().empty());accepted.accept();
    }
    const auto events=f.drain();REQUIRE(events.size()==2);
    CHECK(events[0].find("Alice killed Bob")!=events[0].npos);CHECK(events[1].find("accepted suffix")!=events[1].npos);
    {
        api::GameClientHost::ConsolePublicationBatch old_generation{&f.host};f.apply({say("old attempt")});
        f.host.reset({2,2,1});old_generation.accept();
    }
    CHECK(f.drain().empty());
}
TEST_CASE("E33.2 titles preparation uses the selected approved world root with no fallback or supplied paths","[e33][e33_2][console][titles][approved][no-stock]") {
    namespace hf=hlclient::tests::hud_resources_e29;namespace gs=hlclient::goldsrc;
    hlclient::tests::ScopedLocalResourceTestRoot root;root.create_game("sample");
    root.write("sample","maps/test_map.bsp","map");root.write("sample","titles.txt","$effect -2\nSELECTED\n{\nSelected root\n}\n");
    root.write("valve","titles.txt","FOREIGN\n{\nWrong root\n}\n");
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        hlclient::tests::readiness_fixture::make_environment(root,"sample")};
    gs::ApprovedHudAssets assets{hf::resources_for(environment,"maps/test_map.bsp"),{},"titles.txt"};
    const auto prepared=hf::wait_prepared(assets);REQUIRE(prepared);
    REQUIRE(prepared->text_messages_status==api::HudResourceStatus::ready);REQUIRE(prepared->text_messages.size()==1);
    CHECK(prepared->text_messages[0].key=="SELECTED");CHECK(prepared->text_messages[0].destination==api::TextDestination::console);
    Fixture f;REQUIRE(f.host.bind_text_messages(prepared->text_messages));f.apply({text(4,{"#SELECTED"})});
    CHECK(has(f.drain(),"Selected root"));
    gs::ApprovedHudAssets rejected{hf::resources_for(environment,"maps/test_map.bsp"),{},"../titles.txt"};
    REQUIRE(hf::wait_prepared(rejected));CHECK(rejected.snapshot()->text_messages.empty());
}
TEST_CASE("E33.2 accepted disconnect reason is only exposed after complete strict terminal framing","[e33][e33_2][console][no-stock]") {
    namespace gs=hlclient::goldsrc;
    auto wire=strings({"Kicked: fixture reason"});wire.insert(wire.begin(),std::byte{2});
    std::string_view reason;REQUIRE(gs::inspect_disconnect_notice(wire,&reason)==gs::DisconnectNoticeStatus::observed);
    CHECK(reason=="Kicked: fixture reason");wire.push_back(std::byte{255});
    CHECK(gs::inspect_disconnect_notice(wire,&reason)==gs::DisconnectNoticeStatus::malformed);CHECK(reason.empty());
    auto fake=strings({"server disconnect inside print"});fake.insert(fake.begin(),std::byte{8});
    CHECK(gs::inspect_disconnect_notice(fake,&reason)==gs::DisconnectNoticeStatus::not_notice);CHECK(reason.empty());
}
TEST_CASE("E33.2 distinct combat events bypass diagnostic rate filtering while bounded storage remains","[e33][e33_2][console][no-stock]") {
    Fixture f;f.apply({player(1,"passwordPlayer"),player(2,"Bob")});
    for(unsigned batch=0;batch<3;++batch) {
        for(unsigned event=0;event<120;++event)f.apply({death(1,2)});
        const auto output=f.drain();REQUIRE(output.size()==120);CHECK(has(output,"passwordPlayer killed Bob"));
    }
}
TEST_CASE("E33.2 approved original titles prepare read only and resolve through the normal module commit","[e33][e33_2][console][titles][actual-assets]") {
    namespace hf=hlclient::tests::hud_resources_e29;namespace gs=hlclient::goldsrc;
    const auto base=hf::environment_value("HLCLIENT_CONSOLE_ASSET_ROOT");
    if(base.empty())SKIP("Set HLCLIENT_CONSOLE_ASSET_ROOT for read-only original titles verification");
    auto roots=hlclient::local_resources::LocalResourceSearchRoots::create(base,"valve");REQUIRE(roots);
    auto created=hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));REQUIRE(created);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(created.environment)};
    gs::ApprovedHudAssets assets{hf::resources_for(environment,"maps/crossfire.bsp"),{},"titles.txt"};
    const auto prepared=hf::wait_prepared(assets);REQUIRE(prepared);REQUIRE(prepared->text_messages_status==api::HudResourceStatus::ready);
    CHECK(prepared->text_messages.size()==160U);
    Fixture f;REQUIRE(f.host.bind_text_messages(prepared->text_messages));f.apply({text(2,{"#Muted","FixturePlayer"})});
    CHECK(has(f.drain(),"You have muted FixturePlayer."));
}
