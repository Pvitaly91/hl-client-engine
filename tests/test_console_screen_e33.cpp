#include "entity_render/entity_opengl_test_support.hpp"
#include "hud_resources_test_fixture_e29.hpp"
#include "../src/platform/sdl_platform_event_translator.hpp"
#include <hlclient/app/console_screen.hpp>
#include <hlclient/app/console_application.hpp>
#include <hlclient/console/console.hpp>
#include <hlclient/goldsrc/console_background.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/renderer/screen_ui_font.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace {
namespace r=hlclient::renderer;
namespace g=hlclient::goldsrc;
namespace app=hlclient::app;
namespace i=hlclient::input;
namespace f=hlclient::tests::entity_opengl_fixture;

std::vector<std::byte> lmp() {
    std::vector<std::byte> bytes(8+4+2+768);
    bytes[0]=bytes[4]=std::byte{2};bytes[8]=std::byte{0};bytes[9]=std::byte{1};
    bytes[10]=std::byte{2};bytes[11]=std::byte{255};bytes[13]=std::byte{1};
    for(unsigned n=0;n<256;++n) {
        bytes[14+n*3]=static_cast<std::byte>(n);
        bytes[15+n*3]=static_cast<std::byte>(255-n);
        bytes[16+n*3]=std::byte{17};
    }return bytes;
}
auto pixel(const r::opengl::OpenGlFramebufferObservation& observation,r::RenderExtent size,int x,int y) {
    const auto offset=(std::size_t(size.height-1-y)*size.width+x)*4U;
    REQUIRE(offset+3<observation.rgba8.size());
    return std::array<unsigned,3>{observation.rgba8[offset],observation.rgba8[offset+1],observation.rgba8[offset+2]};
}
auto draw(f::HiddenContext& context,const r::RenderScene& scene,r::RenderExtent extent) {
    context.renderer().render(scene,extent);
    auto result=context.renderer().observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(result);CHECK(glGetError()==GL_NO_ERROR);return result;
}
void save(std::string_view name,const r::opengl::OpenGlFramebufferObservation& image,r::RenderExtent extent) {
    const auto directory=hlclient::tests::hud_resources_e29::environment_value("HLCLIENT_E33_SCREENSHOT_DIR");
    if(directory.empty())return;
    std::ofstream file(std::filesystem::path{directory}/name,std::ios::binary);REQUIRE(file.good());
    file<<"P6\n"<<extent.width<<' '<<extent.height<<"\n255\n";
    for(int y=extent.height-1;y>=0;--y)for(int x=0;x<extent.width;++x)
        file.write(reinterpret_cast<const char*>(image.rgba8.data()+(std::size_t(y)*extent.width+x)*4),3);
    REQUIRE(file.good());
}
hlclient::game_api::ConsoleVisualTheme theme() {
    hlclient::game_api::ConsoleVisualTheme t;t.title="HALF-LIFE";t.emblem_circle=true;
    t.emblem={{{-.28F,-.42F,-.06F,-.42F,.12F},{-.06F,-.42F,.3F,.44F,.12F},
        {.05F,-.14F,-.29F,.44F,.12F},{.3F,.44F,.44F,.4F,.12F}}};return t;
}

TEST_CASE("E33 console LMP decodes exact indexed palette with opaque255 and rejects other formats",
    "[e33][console-assets][no-stock]") {
    const auto bytes=lmp();const auto decoded=g::decode_console_lmp_image(bytes);REQUIRE(decoded);
    CHECK(decoded->width==2);CHECK(decoded->height==2);CHECK(decoded->pixels.size()==16);
    CHECK(decoded->pixels[0]==std::byte{0});CHECK(decoded->pixels[1]==std::byte{255});
    CHECK(decoded->pixels[12]==std::byte{255});CHECK(decoded->pixels[13]==std::byte{0});
    CHECK(decoded->pixels[15]==std::byte{255});
    for(std::size_t count=0;count<bytes.size();++count)CHECK_FALSE(g::decode_console_lmp_image(std::span{bytes}.first(count)));
    auto bad=bytes;bad.push_back(std::byte{0});CHECK_FALSE(g::decode_console_lmp_image(bad));
    for(unsigned offset:{0U,4U,12U,13U}){bad=bytes;bad[offset]=std::byte{255};CHECK_FALSE(g::decode_console_lmp_image(bad));}
    bad=bytes;bad[0]=std::byte{'B'};bad[1]=std::byte{'M'};CHECK_FALSE(g::decode_console_lmp_image(bad));
}

TEST_CASE("E33 early console asset is verified without a map and cannot fall through another game root",
    "[e33][console-assets][approved-source][no-stock]") {
    hlclient::tests::ScopedLocalResourceTestRoot root;root.write("valve","gfx/console.lmp",lmp());
    auto ready=g::prepare_console_background(root.path(),"valve","gfx/console.lmp");
    REQUIRE(ready.image);CHECK(ready.status==g::ConsoleBackgroundStatus::ready);CHECK(ready.image->width==2);
    root.create_game("mod");CHECK(g::prepare_console_background(root.path(),"mod","gfx/console.lmp").status==g::ConsoleBackgroundStatus::missing);
    CHECK(g::prepare_console_background(root.path(),"valve","../gfx/console.lmp").status==g::ConsoleBackgroundStatus::rejected);
    root.write("valve","gfx/console.lmp","not an image");
    CHECK(g::prepare_console_background(root.path(),"valve","gfx/console.lmp").status==g::ConsoleBackgroundStatus::rejected);
    CHECK(ready.image->pixels[12]==std::byte{255});
    std::stop_source cancelled;cancelled.request_stop();
    CHECK(g::prepare_console_background(root.path(),"valve","gfx/console.lmp",cancelled.get_token()).status==g::ConsoleBackgroundStatus::cancelled);
}

TEST_CASE("E33 approved original console resource decodes read only without a live session",
    "[e33][console-assets][actual-assets]") {
    const auto base=hlclient::tests::hud_resources_e29::environment_value("HLCLIENT_CONSOLE_ASSET_ROOT");
    if(base.empty())SKIP("Set HLCLIENT_CONSOLE_ASSET_ROOT for approved original console audit");
    const auto ready=g::prepare_console_background(base,"valve","gfx/conback.lmp");
    REQUIRE(ready.status==g::ConsoleBackgroundStatus::ready);REQUIRE(ready.image);
    CHECK(ready.image->width==320);CHECK(ready.image->height==200);CHECK(ready.image->pixels.size()==256000);
    for(std::size_t a=3;a<ready.image->pixels.size();a+=4)REQUIRE(ready.image->pixels[a]==std::byte{255});
}

TEST_CASE("E33 approved original backdrop has source-matched pixels and unsqueezed production crops",
    "[e33][console-assets][actual-assets][console-screen][opengl][actual-context]") {
    const auto base=hlclient::tests::hud_resources_e29::environment_value("HLCLIENT_CONSOLE_ASSET_ROOT");
    if(base.empty())SKIP("Set HLCLIENT_CONSOLE_ASSET_ROOT for approved original framebuffer proof");
    const auto prepared=g::prepare_console_background(base,"valve","gfx/conback.lmp");
    REQUIRE(prepared.status==g::ConsoleBackgroundStatus::ready);REQUIRE(prepared.image);
    for(const auto extent:std::array<r::RenderExtent,2>{{{1920,1080},{3440,1440}}}) {
        auto context=f::try_context(extent.width,extent.height);if(!context||!f::capable_context())SKIP("OpenGL3.3 context unavailable");
        context->initialize_renderer();r::RenderScene scene;scene.clear_color={0,.1F,.3F,1};
        scene.screen_ui=app::build_console_theme(theme(),extent.width,extent.height,.5,true,prepared.image);
        const auto& placement=std::get<r::ScreenUiImage>(scene.screen_ui->commands.front());
        CHECK(std::abs(placement.width/placement.height-1.6F)<.0001F);
        const auto full=draw(*context,scene,extent);
        // GL_LINEAR source sampling with the theme's 25% black readability
        // layer. Compare palette-derived RGB, not just changed-pixel counts.
        const auto expected=[&](int x,int y,unsigned channel) {
            const float sx=(float(x)+.5F-placement.x)/placement.width*prepared.image->width-.5F;
            const float sy=(float(y)+.5F-placement.y)/placement.height*prepared.image->height-.5F;
            const int x0=static_cast<int>(std::floor(sx)),y0=static_cast<int>(std::floor(sy));
            const float tx=sx-std::floor(sx),ty=sy-std::floor(sy);
            const auto sample=[&](int px,int py) {
                px=std::clamp(px,0,static_cast<int>(prepared.image->width)-1);
                py=std::clamp(py,0,static_cast<int>(prepared.image->height)-1);
                return float(std::to_integer<unsigned>(prepared.image->pixels[(std::size_t(py)*prepared.image->width+px)*4+channel]));
            };
            return .75F*((sample(x0,y0)*(1-tx)+sample(x0+1,y0)*tx)*(1-ty)+
                (sample(x0,y0+1)*(1-tx)+sample(x0+1,y0+1)*tx)*ty);
        };
        for(int y=160;y<extent.height-4;y+=179)for(int x=51;x<extent.width;x+=211) {
            const auto actual=pixel(full,extent,x,y);
            for(unsigned channel=0;channel<3;++channel)CHECK(std::abs(float(actual[channel])-expected(x,y,channel))<=2.0F);
        }
        scene.screen_ui=app::build_console_theme(theme(),extent.width,extent.height,.5,false,prepared.image);
        const auto half=draw(*context,scene,extent);
        for(int y=160;y<extent.height/2-4;y+=83)for(int x=51;x<extent.width;x+=211)
            CHECK(pixel(full,extent,x,y)==pixel(half,extent,x,y));
        const auto suffix=std::to_string(extent.width)+"x"+std::to_string(extent.height)+".ppm";
        save("original-startup-"+suffix,full,extent);save("original-console-half-"+suffix,half,extent);
    }
}

TEST_CASE("E33 project font covers Ukrainian alphabet without transliteration or replacement boxes",
    "[e33][console-font][no-stock]") {
    const std::u32string_view alphabet=U"АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопрстуфхцчшщьюя";
    for(auto c:alphabet) {
        const auto glyph=r::screen_ui_glyph(c);CHECK(glyph!=r::screen_ui_glyph(U'?'));
        CHECK(std::any_of(glyph.begin(),glyph.end(),[](auto row){return row!=0;}));
    }
    CHECK(r::screen_ui_glyph(U'Ї')!=r::screen_ui_glyph(U'І'));
    CHECK(r::screen_ui_glyph(U'Ґ')!=r::screen_ui_glyph(U'Г'));
    CHECK(r::screen_ui_glyph(U'Є')!=r::screen_ui_glyph(U'Е'));
    CHECK(r::screen_ui_glyph(U'і')!=r::screen_ui_glyph(U'i'+1));
    const std::string utf8="Привіт, ґрунт!";std::size_t offset{},count{};
    while(offset<utf8.size()){CHECK(r::next_screen_ui_codepoint(utf8,offset)!=0xfffd);++count;}
    CHECK(count==14);
    offset=0;CHECK(r::next_screen_ui_codepoint("\xf0\x80\x80\x80",offset)==0xfffd);
    offset=0;CHECK(r::next_screen_ui_codepoint("\xed\xa0\x80",offset)==0xfffd);
}

TEST_CASE("E33 physical console toggle and bounded Unicode text events are independent of layout",
    "[e33][sdl][console-input][no-stock]") {
    hlclient::platform::detail::SdlPlatformEventTranslator translator;
    for(auto logical:{SDLK_GRAVE,static_cast<SDL_Keycode>(0x0451)}) {
        SDL_Event native{};native.type=SDL_EVENT_KEY_DOWN;native.key.scancode=SDL_SCANCODE_GRAVE;
        native.key.key=logical;native.key.repeat=true;const auto translated=translator.translate(native);REQUIRE(translated);
        const auto& key=std::get<i::InputEvent>(*translated);CHECK(key.key()==i::PhysicalKey::grave);CHECK(key.repeated());
    }
    const std::string text="Їжак ґрунт";SDL_Event native{};native.type=SDL_EVENT_TEXT_INPUT;native.text.text=text.c_str();
    const auto translated=translator.translate(native);REQUIRE(translated);
    const auto text_event=std::get<i::InputEvent>(*translated);CHECK(text_event.type()==i::InputEventType::text_input);CHECK(text_event.text()==text);
    const auto bounded=i::InputEvent::text_input(std::string(127,'a')+"ї");CHECK(bounded.text().size()==127);
    i::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(i::InputEvent::focus_gained());tracker.apply_event(text_event);
    const auto snapshot=tracker.publish_snapshot();CHECK_FALSE(snapshot.key_held(i::PhysicalKey::w));CHECK_FALSE(snapshot.mouse_button_held(i::PhysicalMouseButton::left));tracker.end_frame();
}

TEST_CASE("E33 screen overlay validates bounds and retains aspect instead of squashing half height",
    "[e33][console-screen][no-stock]") {
    for(const auto size:std::array<r::RenderExtent,3>{{{640,480},{1920,1080},{3440,1440}}}) {
        const auto full=app::build_console_theme(theme(),size.width,size.height,.5,true);
        const auto half=app::build_console_theme(theme(),size.width,size.height,.5,false);
        REQUIRE(r::valid_screen_ui(full));REQUIRE(r::valid_screen_ui(half));
        const auto& a=std::get<r::ScreenUiImage>(full.commands[0]);const auto& b=std::get<r::ScreenUiImage>(half.commands[0]);
        CHECK(a.width==b.width);CHECK(a.height==b.height);CHECK(a.y==b.y);
        CHECK(std::abs(a.width/a.height-800.0F/600.0F)<.0001F);CHECK(b.clip->height==size.height*.5F);
        const auto sliding=app::build_console_theme(theme(),size.width,size.height,.25,false);
        const auto& moving=std::get<r::ScreenUiImage>(sliding.commands[0]);
        CHECK(moving.height==a.height);CHECK(moving.y==a.y-size.height*.25F);
        CHECK(app::build_console_theme(theme(),size.width,size.height,0,false).commands.empty());
    }
    r::ScreenUiFrame bad;bad.commands.emplace_back(r::ScreenUiText{std::string(8193,'a')});CHECK_FALSE(r::valid_screen_ui(bad));
    bad.commands.clear();bad.commands.emplace_back(r::ScreenUiRectangle{0,0,100,100,{1,1,1,1},r::ScreenUiClip{0,0,-1,4}});CHECK_FALSE(r::valid_screen_ui(bad));
    r::null::NullRenderer null;null.initialize();r::RenderScene scene;scene.screen_ui=bad;CHECK_THROWS_AS(null.render(scene,{100,100}),std::invalid_argument);
}

TEST_CASE("E33 reserved physical keys cannot acquire deceptive binds and modifiers stay independent",
    "[e33][console-bind][no-stock]") {
    hlclient::console::ConsoleBindings bindings;
    for(const auto name:{"F8","escape","GRAVE","BACKQUOTE","`","~"}) {
        const auto result=bindings.bind(name,"echo hidden");CHECK_FALSE(result.success);
        CHECK(result.message.find("Reserved")!=std::string::npos);
    }
    for(const auto name:{"ALT","RALT","CTRL","RCTRL","SHIFT","RSHIFT","F3","F4","F5","F6","F7","F9","F10","F11","F12"})
        CHECK(bindings.bind(name,"echo accepted").success);
}

TEST_CASE("E33 connection reset clears GPU scene proofs and timings while retaining screen UI pixels",
    "[e33][console-screen][opengl][actual-context][session-reset]") {
    auto context=f::try_context(320,240);if(!context||!f::capable_context())SKIP("OpenGL3.3 context unavailable");
    context->initialize_renderer();auto scene=f::render_scene(f::studio_scene());
    scene.screen_ui=app::build_console_theme(theme(),320,240,.5,true);
    scene.image_adjustments={.1F,1.2F,1.1F};scene.requested_msaa_samples=2;
    auto& renderer=context->renderer();const auto before=draw(*context,scene,{320,240});
    CHECK(renderer.entity_statistics().studio_asset_upload_count>0);
    CHECK(renderer.statistics().rendered_frame_count>0);
    renderer.render_with_gpu_timing(scene,{320,240},44);
    for(unsigned attempt=0;attempt<2;++attempt) {
        renderer.reset_session_resources();
        CHECK(renderer.statistics().rendered_frame_count==0);
        CHECK_FALSE(renderer.statistics().active_world_resources);
        CHECK(renderer.entity_statistics().studio_asset_upload_count==0);
        CHECK_FALSE(renderer.entity_statistics().active_entity_resources);
        CHECK(renderer.hud_statistics().cached_texture_count==0);
        CHECK(renderer.decal_statistics().cached_texture_count==0);
        CHECK(renderer.gpu_timing_statistics().pending==0);
        CHECK(renderer.gpu_timing_statistics().submitted==0);
        CHECK_FALSE(renderer.poll_gpu_timing());
        CHECK(renderer.scene_target_statistics().resolved_frames==0);
        CHECK(renderer.scene_target_statistics().active_framebuffers==0);
        const auto after=draw(*context,scene,{320,240});
        CHECK(after.rgba8==before.rgba8);
        CHECK(renderer.entity_statistics().studio_asset_upload_count>0);
        CHECK(renderer.statistics().rendered_frame_count==1);
        CHECK(renderer.image_adjustment_statistics().adjusted_frames==1);
    }
}

TEST_CASE("E33 SDL Unicode delivery follows editor focus and suppresses the opening physical key text",
    "[e33][sdl][console-input][actual-context]") {
    namespace p=hlclient::platform;
    std::unique_ptr<p::SdlRuntime> runtime;std::unique_ptr<p::SdlWindow> window;
    try {
        runtime=std::make_unique<p::SdlRuntime>();
        window=std::make_unique<p::SdlWindow>(p::SdlWindowConfig{"E33 text lifecycle",64,64,true});
    }catch(const std::exception& error) {
        if(p::classify_opengl_startup_capability_failure(error)!=p::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed hidden window capability unavailable");throw;
    }
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    int count{};auto** windows=SDL_GetWindows(&count);REQUIRE(windows);REQUIRE(count==1);
    auto* native_window=windows[0];const auto id=SDL_GetWindowID(native_window);SDL_free(windows);
    REQUIRE(window->set_text_input_enabled(true));CHECK(window->text_input_enabled());
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;focus.window.windowID=id;REQUIRE(SDL_PushEvent(&focus));
    SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.windowID=id;key.key.scancode=SDL_SCANCODE_GRAVE;REQUIRE(SDL_PushEvent(&key));
    SDL_Event text{};text.type=SDL_EVENT_TEXT_INPUT;text.text.windowID=id;text.text.text="`";REQUIRE(SDL_PushEvent(&text));
    key.key.scancode=SDL_SCANCODE_W;REQUIRE(SDL_PushEvent(&key));
    text.text.text="Україна";REQUIRE(SDL_PushEvent(&text));
    p::PlatformEvent event{p::WindowEvent{}};std::vector<std::string> received;
    while(window->poll_event(event))if(const auto* e=std::get_if<i::InputEvent>(&event);e&&e->type()==i::InputEventType::text_input)
        received.emplace_back(e->text());
    REQUIRE(received.size()==1);CHECK(received[0]=="Україна");
    focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;REQUIRE(SDL_PushEvent(&focus));REQUIRE(SDL_PushEvent(&text));
    while(window->poll_event(event))if(const auto* e=std::get_if<i::InputEvent>(&event))CHECK(e->type()!=i::InputEventType::text_input);
    CHECK_FALSE(SDL_TextInputActive(native_window));CHECK(window->text_input_enabled());
    REQUIRE(window->set_text_input_enabled(false));CHECK_FALSE(window->text_input_enabled());
    focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;REQUIRE(SDL_PushEvent(&focus));REQUIRE(SDL_PushEvent(&text));
    while(window->poll_event(event))if(const auto* e=std::get_if<i::InputEvent>(&event))CHECK(e->type()!=i::InputEventType::text_input);
    CHECK_FALSE(SDL_TextInputActive(native_window));
}

TEST_CASE("E33 production UI renders exact Ukrainian glyph caret clip and final composition above HUD",
    "[e33][console-screen][opengl][actual-context]") {
    auto context=f::try_context(320,240);if(!context||!f::capable_context())SKIP("OpenGL3.3 context unavailable");
    context->initialize_renderer();r::RenderScene scene;scene.clear_color={0,0,1,1};
    scene.basic_hud.emplace();scene.basic_hud->rectangles.push_back({0,0,320,240,{1,0,1,1}});
    scene.image_adjustments={.25F,1.4F,.8F};scene.screen_ui.emplace();
    auto& commands=scene.screen_ui->commands;commands.emplace_back(r::ScreenUiRectangle{0,0,320,120,{0,0,0,1}});
    commands.emplace_back(r::ScreenUiText{"іЇґє",20,20,2,12,20,{1,1,1,1},r::ScreenUiClip{20,20,28,18}});
    commands.emplace_back(r::ScreenUiRectangle{90,30,2,16,{1,.5F,0,1}});
    const auto image=draw(*context,scene,{320,240});
    CHECK(pixel(image,{320,240},24,20)==std::array<unsigned,3>{255,255,255}); // Ukrainian і dot
    CHECK(pixel(image,{320,240},20,20)==std::array<unsigned,3>{0,0,0});
    CHECK(pixel(image,{320,240},48,25)==std::array<unsigned,3>{0,0,0}); // glyph clipped on right
    const auto caret=pixel(image,{320,240},90,32);CHECK(caret[0]==255);CHECK(caret[1]>=127);CHECK(caret[1]<=128);CHECK(caret[2]==0);
    CHECK(pixel(image,{320,240},90,150)==std::array<unsigned,3>{255,0,255}); // HUD survives lower half
    save("console-cyrillic-caret.ppm",image,{320,240});
}

TEST_CASE("E33 production startup backdrop and half console preserve pixels at native and ultrawide extents",
    "[e33][console-screen][opengl][actual-context]") {
    for(const auto extent:std::array<r::RenderExtent,3>{{{640,480},{1920,1080},{3440,1440}}}) {
        auto context=f::try_context(extent.width,extent.height);if(!context||!f::capable_context())SKIP("OpenGL3.3 context unavailable");
        context->initialize_renderer();r::RenderScene scene;scene.clear_color={0,.1F,.3F,1};
        const auto before=draw(*context,scene,extent);
        scene.screen_ui=app::build_console_theme(theme(),extent.width,extent.height,0,true);
        const auto full=draw(*context,scene,extent);
        // Distinct authored ochre backdrop and visible emblem, not mere changed pixels.
        const auto corner=pixel(full,extent,4,4);CHECK(corner[0]>10);CHECK(corner[0]>corner[2]);
        std::size_t amber{};
        for(std::size_t n=0;n<full.rgba8.size();n+=4)if(full.rgba8[n]>85&&full.rgba8[n]>full.rgba8[n+2]*2)++amber;
        CHECK(amber>std::size_t(extent.width)*extent.height/1000);
        scene.screen_ui=app::build_console_theme(theme(),extent.width,extent.height,.5,false);
        const auto half=draw(*context,scene,extent);
        for(int y=8;y<extent.height/2-5;y+=37)for(int x=8;x<extent.width;x+=71)
            CHECK(pixel(full,extent,x,y)==pixel(half,extent,x,y));
        for(int y=extent.height/2+2;y<extent.height;y+=37)for(int x=8;x<extent.width;x+=71)
            CHECK(pixel(before,extent,x,y)==pixel(half,extent,x,y));
        const auto suffix=std::to_string(extent.width)+"x"+std::to_string(extent.height)+".ppm";
        save("startup-before-"+suffix,before,extent);save("startup-after-"+suffix,full,extent);save("console-half-"+suffix,half,extent);
    }
}

TEST_CASE("E33 composed application console shows Ukrainian output editor and caret before world and in game",
    "[e33][console-screen][console-application][opengl][actual-context]") {
    for(const auto extent:std::array<r::RenderExtent,2>{{{1920,1080},{3440,1440}}}) {
        hlclient::core::CommandLineOptions options;options.video.width=extent.width;options.video.height=extent.height;
        options.video.vsync=false;double clock{};std::unique_ptr<app::ConsoleApplication> application;
        try {
            application=std::make_unique<app::ConsoleApplication>(options,
                [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme(theme(),w,h,fraction,full);},
                app::ConsoleGameRegistration{},true,false,[&clock]{return clock;});
        }catch(const std::exception& error) {
            if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
                SKIP("Typed hidden OpenGL startup capability unavailable");throw;
        }
        auto& ui=*application;clock=.25;ui.begin_frame();ui.model().clear();
        i::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(i::InputEvent::focus_gained());
        REQUIRE(ui.route_event(i::InputEvent::text_input("echo Українська консоль: Ґґ Єє Іі Її"),tracker));
        REQUIRE(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::enter),tracker));
        (void)tracker.publish_snapshot();tracker.end_frame();ui.begin_frame();
        ui.model().append("Підключення до сервера — очікування відповіді.");
        ui.model().append("Ресурси перевірено. Консоль працює без карти.");
        ui.model().append("Історія: Вгору/Вниз   Доповнення: Tab   Буфер обміну: Ctrl+C / Ctrl+V");
        REQUIRE(ui.model().set_editor("echo \"Перевірка: ґрунт, їжак, єдність\""));
        const auto render_composed=[&](bool full) {
            r::RenderScene scene;scene.clear_color={.02F,.04F,.06F,1};
            scene.basic_hud.emplace();scene.basic_hud->rectangles.push_back({0,0,float(extent.width),float(extent.height),{.08F,.14F,.18F,1}});
            ui.compose(scene);REQUIRE(scene.screen_ui);REQUIRE(r::valid_screen_ui(*scene.screen_ui));
            bool editor=false,cyrillic=false,caret=false;
            for(const auto& command:scene.screen_ui->commands) {
                if(const auto* text=std::get_if<r::ScreenUiText>(&command)) {
                    editor=editor||text->text.find("Перевірка: ґрунт")!=text->text.npos;
                    cyrillic=cyrillic||text->text.find("Українська консоль")!=text->text.npos;
                }
                if(const auto* rectangle=std::get_if<r::ScreenUiRectangle>(&command))caret=caret||(rectangle->width<=4&&rectangle->height>=11);
            }
            CHECK(editor);CHECK(cyrillic);CHECK(caret);
            ui.renderer().render(scene,extent);
            const auto image=ui.renderer().observe_framebuffer(extent,scene.clear_color,true);REQUIRE(image);
            const auto suffix=std::to_string(extent.width)+"x"+std::to_string(extent.height)+".ppm";
            save(std::string{full?"application-console-startup-":"application-console-half-"}+suffix,image,extent);
            CHECK(glGetError()==GL_NO_ERROR);
        };
        render_composed(true);
        ui.begin_attempt();ui.in_game();clock=1;ui.execute("toggleconsole");ui.begin_frame();
        clock=1.3;ui.begin_frame();REQUIRE(ui.console_active());render_composed(false);
    }
}

TEST_CASE("E33 closed loading console blocks gameplay without editing or executing an invisible command",
    "[e33][console-input][console-application][actual-context]") {
    hlclient::core::CommandLineOptions options;options.video.width=640;options.video.height=480;
    options.video.vsync=false;double clock{};std::unique_ptr<app::ConsoleApplication> application;
    try {
        application=std::make_unique<app::ConsoleApplication>(options,
            [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme(theme(),w,h,fraction,full);},
            app::ConsoleGameRegistration{},true,false,[&clock]{return clock;});
    }catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed hidden OpenGL startup capability unavailable");throw;
    }
    auto& ui=*application;clock=.25;ui.begin_frame();
    REQUIRE(ui.console_active());REQUIRE(ui.window().text_input_enabled());
    REQUIRE(ui.model().set_editor("echo hidden_editor_must_not_execute"));
    ui.begin_attempt();clock=.5;ui.begin_frame();ui.model().clear();
    REQUIRE_FALSE(ui.console_active());CHECK_FALSE(ui.window().text_input_enabled());
    const auto editor=ui.model().editor();const auto cursor=ui.model().cursor();
    i::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(i::InputEvent::focus_gained());
    CHECK(ui.route_event(i::InputEvent::text_input("непомітне введення"),tracker));
    CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::backspace),tracker));
    CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::enter),tracker));
    CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::w),tracker));
    CHECK(ui.route_event(i::InputEvent::mouse_button_pressed(i::PhysicalMouseButton::left),tracker));
    CHECK(ui.route_event(i::InputEvent::mouse_wheel(0,1),tracker));
    CHECK_FALSE(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::f8),tracker));
    ui.begin_frame(&tracker);
    CHECK(ui.model().editor()==editor);CHECK(ui.model().cursor()==cursor);
    CHECK(ui.model().lines().empty());CHECK(ui.model().scroll_offset()==0);
    CHECK_FALSE(ui.window().text_input_enabled());
    REQUIRE(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::grave),tracker));
    clock=.75;ui.begin_frame(&tracker);REQUIRE(ui.console_active());CHECK(ui.window().text_input_enabled());
    CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::enter),tracker));ui.begin_frame(&tracker);
    CHECK(ui.model().editor().empty());
    CHECK(std::any_of(ui.model().lines().begin(),ui.model().lines().end(),[](const auto& line){
        return line=="hidden_editor_must_not_execute";
    }));
    (void)tracker.publish_snapshot();tracker.end_frame();
}

TEST_CASE("E33.1 oldest scrollback fills the viewport instead of collapsing to one record",
    "[e33][e33_1][console-model][console-layout][no-stock]") {
    hlclient::console::ConsoleModel model;
    for(unsigned row=0;row<120;++row)model.append("record-"+std::to_string(row));
    REQUIRE(model.visible_lines(80,12).size()==12);
    model.scroll(10000);
    const auto oldest=model.visible_lines(80,12);
    CHECK(oldest.size()==12);
    REQUIRE_FALSE(oldest.empty());
    CHECK(oldest.front()=="record-0");
    CHECK(oldest.back()=="record-11");
    // Editor/history operations are not output-viewport operations.
    REQUIRE(model.set_editor("echo draft"));model.home();model.end();model.left();model.right();
    model.select_all();CHECK(model.copy_selection()=="echo draft");
    model.history_previous();model.history_next();
    CHECK(model.visible_lines(80,12)==oldest);
    model.append("new output while reading");
    CHECK(model.visible_lines(80,12)==oldest);
    model.bottom();CHECK(model.visible_lines(80,12).back()=="new output while reading");
    model.clear();CHECK(model.visible_lines(80,12).empty());
    model.append("one true record");CHECK(model.visible_lines(80,12)==std::vector<std::string>{"one true record"});
    model.clear();model.append("LF\nCRLF\r\nCR\rfinal");
    CHECK(model.visible_lines(80,12)==std::vector<std::string>{"LF","CRLF","CR","final"});
    // Eviction may discard the anchor, but must then clamp to a complete
    // oldest retained viewport rather than a stale/wrapped one-row index.
    hlclient::console::ConsoleModel bounded{{20,4096,128,8,8}};
    for(unsigned row=0;row<20;++row)bounded.append("old-"+std::to_string(row));
    REQUIRE(bounded.visible_lines(80,6).size()==6);bounded.scroll(10000);
    for(unsigned row=0;row<8;++row)bounded.append("new-"+std::to_string(row));
    CHECK(bounded.visible_lines(80,6)==std::vector<std::string>{"old-8","old-9","old-10","old-11","old-12","old-13"});
}

TEST_CASE("E33.1 scroll advances wrapped visual rows and retains logical content across rewrap",
    "[e33][e33_1][console-model][console-layout][no-stock]") {
    hlclient::console::ConsoleModel model;
    model.append("0000000000111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999");
    CHECK(model.visible_lines(10,3)==std::vector<std::string>{"7777777777","8888888888","9999999999"});
    model.scroll(2);
    CHECK(model.visible_lines(10,3)==std::vector<std::string>{"5555555555","6666666666","7777777777"});
    // A logical byte anchor, not the obsolete wrapped-row index, keeps the
    // old endpoint when the framebuffer makes each displayed row twice wide.
    const auto widened=model.visible_lines(20,3);
    REQUIRE_FALSE(widened.empty());CHECK(widened.back()=="66666666667777777777");
    model.append("new-tail-record");CHECK(model.visible_lines(20,3)==widened);
}

TEST_CASE("E33.1 composed full and half console fill actual output rectangles",
    "[e33][e33_1][console-layout][console-screen][console-application][opengl][actual-context]") {
    for(const auto extent:std::array<r::RenderExtent,3>{{{1280,720},{1920,1080},{3440,1440}}}) {
        hlclient::core::CommandLineOptions options;options.video.width=extent.width;options.video.height=extent.height;
        options.video.vsync=false;double clock{};std::unique_ptr<app::ConsoleApplication> application;
        try {
            application=std::make_unique<app::ConsoleApplication>(options,
                [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme(theme(),w,h,fraction,full);},
                app::ConsoleGameRegistration{},true,false,[&clock]{return clock;});
        }catch(const std::exception& error) {
            if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
                SKIP("Typed hidden OpenGL startup capability unavailable");throw;
        }
        auto& ui=*application;clock=.25;ui.begin_frame();ui.model().clear();
        for(unsigned row=0;row<200;++row)ui.model().append("layout-row-"+std::to_string(row));
        const auto inspect=[&](bool full) {
            r::RenderScene scene;scene.clear_color={.02F,.04F,.06F,1};ui.compose(scene);
            REQUIRE(scene.screen_ui);REQUIRE(r::valid_screen_ui(*scene.screen_ui));
            std::vector<const r::ScreenUiText*> output;const r::ScreenUiText* prompt{};
            const auto expected=app::console_output_layout(extent.width,extent.height,float(extent.height)*(full?1.0F:.5F));
            for(const auto& command:scene.screen_ui->commands)if(const auto* text=std::get_if<r::ScreenUiText>(&command)) {
                if(text->clip&&text->clip->y==expected.output.y&&text->clip->height==expected.output.height)output.push_back(text);
                if(text->text.starts_with("] "))prompt=text;
            }
            REQUIRE(prompt);REQUIRE_FALSE(output.empty());REQUIRE(output.front()->clip);
            const auto clip=*output.front()->clip;const auto line_height=output.front()->line_height;
            CHECK(output.size()==static_cast<std::size_t>(std::floor(clip.height/line_height)));
            CHECK(output.front()->y>=clip.y);
            CHECK(output.front()->y<clip.y+line_height);
            CHECK(output.back()->y+line_height<=prompt->y);
            for(const auto* row:output) {
                REQUIRE(row->clip);CHECK(row->clip->y==clip.y);CHECK(row->clip->height==clip.height);
                CHECK(row->y>=clip.y);CHECK(row->y+line_height<=clip.y+clip.height);
            }
            CHECK(prompt->y+prompt->line_height<=float(extent.height)*(full?1.0F:.5F));
            ui.renderer().render(scene,extent);
            const auto image=ui.renderer().observe_framebuffer(extent,scene.clear_color,true);REQUIRE(image);
            CHECK(glGetError()==GL_NO_ERROR);
            const auto* first=output.front();
            // The first glyph is 'l': its left column is set in row0. This is
            // framebuffer/scissor evidence in addition to actual layout bounds.
            const auto glyph=r::screen_ui_glyph(U'l');
            bool found{};
            for(unsigned gy=0;gy<glyph.size()&&!found;++gy)for(unsigned gx=0;gx<5&&!found;++gx)
                if((glyph[gy]&(1U<<(4-gx)))!=0) {
                    const auto rgb=pixel(image,extent,static_cast<int>(first->x+gx*first->pixel_size),
                        static_cast<int>(first->y+gy*first->pixel_size));
                    CHECK(rgb[0]>=250);CHECK(rgb[1]>=180);CHECK(rgb[1]<=186);CHECK(rgb[2]>=60);CHECK(rgb[2]<=68);found=true;
                }
            CHECK(found);
            save(std::string{full?"e331-layout-full-":"e331-layout-half-"}+std::to_string(extent.width)+"x"+
                std::to_string(extent.height)+".ppm",image,extent);
        };
        inspect(true);
        i::InputStateTracker tracker;tracker.begin_frame();tracker.apply_event(i::InputEvent::focus_gained());
        for(unsigned key=0;key<30;++key)CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::page_up),tracker));
        CHECK(ui.route_event(i::InputEvent::mouse_wheel(0,1000),tracker));
        REQUIRE(ui.model().set_editor("echo draft"));
        for(const auto key:{i::PhysicalKey::home,i::PhysicalKey::end,i::PhysicalKey::up,i::PhysicalKey::down,i::PhysicalKey::tab})
            CHECK(ui.route_event(i::InputEvent::key_pressed(key),tracker));
        inspect(true);
        CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::escape),tracker));clock+=.3;ui.begin_frame(&tracker);
        CHECK(ui.route_event(i::InputEvent::key_pressed(i::PhysicalKey::grave),tracker));clock+=.3;ui.begin_frame(&tracker);
        inspect(true);ui.model().bottom();
        (void)tracker.publish_snapshot();tracker.end_frame();
        ui.begin_attempt();ui.in_game();clock=1;ui.execute("toggleconsole");ui.begin_frame();
        clock=1.3;ui.begin_frame();inspect(false);
    }
}

TEST_CASE("E33.1 console geometry uses framebuffer pixels including non-unit DPI fixtures",
    "[e33][e33_1][console-layout][no-stock]") {
    struct Fixture { unsigned logical_width,logical_height;float dpi;unsigned width,height; };
    for(const auto& fixture:std::array<Fixture,4>{{{1280,720,1,1280,720},{1920,1080,1,1920,1080},
        {1280,720,1.5F,1920,1080},{1280,720,1.25F,1600,900}}}) {
        CHECK(float(fixture.logical_width)*fixture.dpi==float(fixture.width));
        CHECK(float(fixture.logical_height)*fixture.dpi==float(fixture.height));
        for(const auto fraction:{.5F,1.0F}) {
            const auto layout=app::console_output_layout(fixture.width,fixture.height,float(fixture.height)*fraction);
            CHECK(layout.panel_height==float(fixture.height)*fraction);
            CHECK(layout.output.x==layout.margin);
            CHECK(layout.output.width==float(fixture.width)-2*layout.margin);
            CHECK(layout.rows==static_cast<std::size_t>(std::floor(layout.output.height/layout.line_height)));
            CHECK(layout.columns==static_cast<std::size_t>(std::floor(layout.output.width/layout.advance)));
            CHECK(layout.output.y>=0);CHECK(layout.output.height>0);CHECK(layout.rows>10);
            CHECK(layout.output.y+layout.output.height<=layout.editor_y-layout.line_height);
            CHECK(layout.editor_y+layout.line_height+layout.margin==layout.panel_height);
        }
    }
    for(float panel:{-1.0F,0.0F,1.0F,12.0F}) {
        const auto layout=app::console_output_layout(1280,720,panel);
        CHECK(layout.rows==0);CHECK(layout.output.height>=0);CHECK(layout.editor_y>=0);
    }
    CHECK(app::console_output_layout(0,720,720).rows==0);
    CHECK(app::console_output_layout(1280,0,720).rows==0);
}

TEST_CASE("E33.1 dense Unicode budget shortens rows explicitly without reducing viewport capacity",
    "[e33][e33_1][console-layout][console-screen][console-application][opengl][actual-context]") {
    const r::RenderExtent extent{1280,720};
    hlclient::core::CommandLineOptions options;options.video.width=extent.width;options.video.height=extent.height;
    options.video.vsync=false;double clock{};std::unique_ptr<app::ConsoleApplication> application;
    try {
        application=std::make_unique<app::ConsoleApplication>(options,
            [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme(theme(),w,h,fraction,full);},
            app::ConsoleGameRegistration{},true,false,[&clock]{return clock;});
    }catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed hidden OpenGL startup capability unavailable");throw;
    }
    auto& ui=*application;clock=.25;ui.begin_frame();ui.model().clear();std::string dense;
    for(unsigned glyph=0;glyph<1200;++glyph)dense+="😀";
    for(unsigned line=0;line<100;++line)ui.model().append(dense);
    REQUIRE(ui.model().set_editor("echo Український текст збережено"));
    r::RenderScene scene;ui.compose(scene);REQUIRE(scene.screen_ui);CHECK(r::valid_screen_ui(*scene.screen_ui));
    const auto layout=app::console_output_layout(extent.width,extent.height,float(extent.height));
    std::size_t rows{},bytes{},glyphs{};
    for(const auto& command:scene.screen_ui->commands)if(const auto* text=std::get_if<r::ScreenUiText>(&command)) {
        if(!text->clip||text->clip->y!=layout.output.y||text->clip->height!=layout.output.height)continue;
        ++rows;bytes+=text->text.size();CHECK(hlclient::console::valid_utf8(text->text));
        for(unsigned char c:text->text)if((c&0xc0U)!=0x80U)++glyphs;
        CHECK(text->text.ends_with("..."));CHECK(text->y>=layout.output.y);
        CHECK(text->y+text->line_height<=layout.output.y+layout.output.height);
    }
    CHECK(rows==layout.rows);CHECK(bytes<=24576);CHECK(glyphs<=12288);
    REQUIRE_FALSE(ui.model().lines().empty());CHECK(ui.model().lines().back()==dense);
    CHECK(ui.model().editor()=="echo Український текст збережено");
    ui.renderer().render(scene,extent);CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("E33.1 panel translation moves content and own clips once without vertical scaling",
    "[e33][e33_1][console-layout][console-screen][no-stock]") {
    r::ScreenUiFrame frame;
    frame.commands.emplace_back(r::ScreenUiRectangle{10,120,100,80,{1,1,1,1},r::ScreenUiClip{5,110,120,90}});
    frame.commands.emplace_back(r::ScreenUiText{"visible",20,140,2,12,22,{1,1,1,1},r::ScreenUiClip{20,138,200,22}});
    frame.commands.emplace_back(r::ScreenUiText{"clipped header",20,20,2,12,22,{1,1,1,1},r::ScreenUiClip{20,18,200,22}});
    frame.commands.emplace_back(r::ScreenUiText{"unclipped",10,180,2,12,22,{1,1,1,1},{}});
    app::translate_console_panel(frame,-100,640,200);
    REQUIRE(frame.commands.size()==3);REQUIRE(r::valid_screen_ui(frame));
    const auto& rectangle=std::get<r::ScreenUiRectangle>(frame.commands[0]);
    CHECK(rectangle.y==20);CHECK(rectangle.height==80);REQUIRE(rectangle.clip);
    CHECK(rectangle.clip->y==10);CHECK(rectangle.clip->height==90);
    const auto& text=std::get<r::ScreenUiText>(frame.commands[1]);
    CHECK(text.y==40);CHECK(text.pixel_size==2);CHECK(text.advance==12);CHECK(text.line_height==22);
    REQUIRE(text.clip);CHECK(text.clip->y==38);CHECK(text.clip->height==22);
    const auto& unbounded=std::get<r::ScreenUiText>(frame.commands[2]);
    CHECK(unbounded.y==80);REQUIRE(unbounded.clip);CHECK(unbounded.clip->y==0);CHECK(unbounded.clip->height==200);
    app::translate_console_panel(frame,0,640,0);CHECK(frame.commands.empty());
}

TEST_CASE("E33.1 actual GPU translated backdrop is a crop and leaves following frames unscissored",
    "[e33][e33_1][console-layout][console-screen][opengl][actual-context]") {
    const r::RenderExtent extent{1280,720};
    auto context=f::try_context(extent.width,extent.height);if(!context||!f::capable_context())SKIP("OpenGL3.3 context unavailable");
    context->initialize_renderer();r::RenderScene scene;scene.clear_color={.08F,.14F,.18F,1};
    scene.screen_ui=app::build_console_theme(theme(),extent.width,extent.height,1,true);
    const auto full=draw(*context,scene,extent);
    const float shifted=240;app::translate_console_panel(*scene.screen_ui,-shifted,extent.width,float(extent.height)-shifted);
    REQUIRE(r::valid_screen_ui(*scene.screen_ui));
    const auto& image=std::get<r::ScreenUiImage>(scene.screen_ui->commands.front());
    CHECK(image.width/image.height==800.0F/600.0F);REQUIRE(image.clip);CHECK(image.clip->height==480);
    const auto moving=draw(*context,scene,extent);
    for(int y=30;y<440;y+=37)for(int x=30;x<extent.width-30;x+=79)
        CHECK(pixel(moving,extent,x,y)==pixel(full,extent,x,y+static_cast<int>(shifted)));
    for(int y=482;y<extent.height;y+=37)for(int x=30;x<extent.width;x+=79)
        CHECK(pixel(moving,extent,x,y)==std::array<unsigned,3>{20,36,46});
    scene.screen_ui.reset();scene.clear_color={.2F,.4F,.6F,1};
    const auto following=draw(*context,scene,extent);
    for(int y=4;y<extent.height;y+=37)for(int x=4;x<extent.width;x+=79)
        CHECK(pixel(following,extent,x,y)==std::array<unsigned,3>{51,102,153});
    save("e331-loading-translated-1280x720.ppm",moving,extent);
}

TEST_CASE("E33.1 composed loading curtain retracts only after successful presentation without scaling",
    "[e33][e33_1][console-layout][console-screen][console-application][opengl][actual-context]") {
    const r::RenderExtent extent{1280,720};
    hlclient::core::CommandLineOptions options;options.video.width=extent.width;options.video.height=extent.height;
    options.video.vsync=false;double clock{};std::unique_ptr<app::ConsoleApplication> application;
    try {
        application=std::make_unique<app::ConsoleApplication>(options,
            [](unsigned w,unsigned h,double fraction,bool full){return app::build_console_theme(theme(),w,h,fraction,full);},
            app::ConsoleGameRegistration{},true,false,[&clock]{return clock;});
    }catch(const std::exception& error) {
        if(hlclient::platform::classify_opengl_startup_capability_failure(error)!=hlclient::platform::OpenGlStartupCapabilityFailure::none)
            SKIP("Typed hidden OpenGL startup capability unavailable");throw;
    }
    auto& ui=*application;ui.begin_attempt();ui.set_stage(app::LoadingStage::scene);ui.begin_frame();
    const auto capture=[&](const char* name,bool candidate) {
        r::RenderScene scene;scene.clear_color={.08F,.14F,.18F,1};ui.compose(scene,candidate);
        if(scene.screen_ui)REQUIRE(r::valid_screen_ui(*scene.screen_ui));
        ui.renderer().render(scene,extent);
        const auto image=ui.renderer().observe_framebuffer(extent,scene.clear_color,true);REQUIRE(image);
        ui.window().swap_buffers();CHECK(glGetError()==GL_NO_ERROR);
        save(name,image,extent);return std::pair{std::move(scene),image};
    };
    const auto candidate=capture("e331-retract-candidate-000ms-1280x720.ppm",true);
    REQUIRE(candidate.first.screen_ui);REQUIRE_FALSE(candidate.first.screen_ui->commands.empty());
    const auto& candidate_background=std::get<r::ScreenUiImage>(candidate.first.screen_ui->commands.front());
    REQUIRE(candidate_background.clip);CHECK(candidate_background.clip->height==extent.height);
    CHECK_FALSE(ui.in_game_state());
    // This callback follows an actual completed render/swap, not merely the
    // earlier candidate flag or a simulated network acknowledgement.
    ui.in_game();REQUIRE(ui.in_game_state());
    const auto ready=capture("e331-retract-ready-000ms-1280x720.ppm",false);
    REQUIRE(ready.first.screen_ui);
    const auto baseline=std::get<r::ScreenUiImage>(ready.first.screen_ui->commands.front());
    for(const auto milliseconds:{90,180,360}) {
        clock=milliseconds/1000.0;ui.begin_frame();
        const auto name="e331-retract-ready-"+std::to_string(milliseconds)+"ms-1280x720.ppm";
        const auto frame=capture(name.c_str(),false);
        const auto shown=std::max(0.0F,float(extent.height)*(1.0F-float(milliseconds)/360.0F));
        if(milliseconds==360)CHECK_FALSE(frame.first.screen_ui);
        else {
            REQUIRE(frame.first.screen_ui);const auto& background=std::get<r::ScreenUiImage>(frame.first.screen_ui->commands.front());
            CHECK(background.width==baseline.width);CHECK(background.height==baseline.height);
            CHECK(std::abs(background.y-(baseline.y+shown-extent.height))<.01F);
            REQUIRE(background.clip);CHECK(std::abs(background.clip->height-shown)<.01F);
            const auto shift=extent.height-static_cast<int>(shown);
            for(int y=20;y<static_cast<int>(shown)-5;y+=47)for(int x=740;x<extent.width-20;x+=89)
                CHECK(pixel(frame.second,extent,x,y)==pixel(ready.second,extent,x,y+shift));
        }
        // Full game viewport below the translated panel; no projection change,
        // stretched backdrop or leftover scissor cuts the exposed game area.
        for(int y=static_cast<int>(shown)+2;y<extent.height;y+=37)for(int x=10;x<extent.width;x+=89)
            CHECK(pixel(frame.second,extent,x,y)==std::array<unsigned,3>{20,36,46});
    }
}
}
