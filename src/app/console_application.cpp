#include <hlclient/app/console_application.hpp>
#include <hlclient/app/console_screen.hpp>
#include <hlclient/core/log.hpp>
#include <hlclient/core/version.hpp>
#ifdef _WIN32
#include <hlclient/platform/windows/external_launcher.hpp>
#endif
#include <algorithm>
#include <cmath>
#include <cctype>
#include <thread>

namespace hlclient::app {
namespace {
using namespace console;
using Key=input::PhysicalKey;
using Action=gameplay_input::GameplayInputAction;
using Event=input::InputEvent;
using EventType=input::InputEventType;
std::string key_name(Key key) {
    static const std::pair<Key,const char*> names[]{
        {Key::w,"W"},{Key::a,"A"},{Key::s,"S"},{Key::d,"D"},{Key::space,"SPACE"},
        {Key::left_control,"CTRL"},{Key::left_shift,"SHIFT"},{Key::left_alt,"ALT"},
        {Key::right_control,"RCTRL"},{Key::right_shift,"RSHIFT"},{Key::right_alt,"RALT"},{Key::e,"E"},{Key::r,"R"},
        {Key::escape,"ESCAPE"},{Key::tab,"TAB"},{Key::up,"UPARROW"},{Key::down,"DOWNARROW"},
        {Key::left,"LEFTARROW"},{Key::right,"RIGHTARROW"},{Key::f1,"F1"},{Key::f2,"F2"},
        {Key::f3,"F3"},{Key::f4,"F4"},{Key::f5,"F5"},{Key::f6,"F6"},{Key::f7,"F7"},
        {Key::f8,"F8"},{Key::f9,"F9"},{Key::f10,"F10"},{Key::f11,"F11"},{Key::f12,"F12"},
        {Key::q,"Q"},{Key::digit_1,"1"},{Key::digit_2,"2"},{Key::digit_3,"3"},
        {Key::digit_4,"4"},{Key::digit_5,"5"},{Key::digit_0,"0"},{Key::digit_6,"6"},
        {Key::digit_7,"7"},{Key::digit_8,"8"},{Key::digit_9,"9"},{Key::b,"B"},{Key::c,"C"},
        {Key::f,"F"},{Key::g,"G"},{Key::h,"H"},{Key::i,"I"},{Key::j,"J"},{Key::k,"K"},
        {Key::l,"L"},{Key::m,"M"},{Key::n,"N"},{Key::o,"O"},{Key::p,"P"},{Key::t,"T"},
        {Key::u,"U"},{Key::v,"V"},{Key::x,"X"},{Key::y,"Y"},{Key::z,"Z"},
        {Key::enter,"ENTER"},{Key::home,"HOME"},{Key::end,"END"},{Key::page_up,"PGUP"},
        {Key::page_down,"PGDN"},{Key::backspace,"BACKSPACE"},{Key::delete_key,"DEL"}};
    for(const auto& [value,name]:names)if(value==key)return name;
    return {};
}
std::string mouse_name(input::PhysicalMouseButton button) {
    return "MOUSE"+std::to_string(static_cast<unsigned>(button)+1U);
}
std::string text_argument(const ConsoleValue& value) {return std::get<std::string>(value);}
ArgumentSpec text_spec(std::string name,bool optional=false){return {std::move(name),ValueType::text,optional,{},{},1024};}
}
ConsoleApplication::ConsoleApplication(core::CommandLineOptions options,ConsoleTheme theme,ConsoleGameRegistration game_registration,
    bool hidden,bool use_user_config,std::function<double()> monotonic_seconds,
    std::function<std::optional<std::string>()> startup_config_loader)
    :options_(std::move(options)),window_({std::string{core::kApplicationName}+" - "+options_.player_name,
        options_.video.width,options_.video.height,hidden,options_.video.display_index,
        options_.video.mode,options_.video.vsync,options_.video.msaa_samples}),theme_(std::move(theme)),
        monotonic_seconds_(std::move(monotonic_seconds)) {
    shared_baseline_.endpoint=options_.connect_endpoint.value_or(shared_baseline_.endpoint);
    shared_baseline_.name=options_.player_name;
    if(options_.base_directory)shared_baseline_.asset_root=*options_.base_directory;
    if(options_.steam_api_runtime)shared_baseline_.steam_api_runtime=*options_.steam_api_runtime;
    shared_baseline_.video=options_.video;shared_baseline_.image=options_.image;
    shared_baseline_.hud_scale=options_.hud_scale;shared_baseline_.volume=options_.audio_volume;
    shared_baseline_.movement_diagnostics=options_.movement_diagnostics;
#ifdef _WIN32
    if(use_user_config)shared_settings_path_=platform::windows::default_external_settings_path();
    if(shared_settings_path_)config_=std::make_unique<ConsoleConfigStore>(shared_settings_path_->parent_path()/"console");
#endif
    if(config_)executor_.set_config_store(config_.get());
    dispatch_actions_.request=[this](auto name,auto value){return !session_stop_requested()&&session_actions_.request&&session_actions_.request(name,value);};
    dispatch_actions_.select_slot=[this](unsigned value){return !session_stop_requested()&&session_actions_.select_slot&&session_actions_.select_slot(value);};
    dispatch_actions_.cycle_inventory=[this](int value){return !session_stop_requested()&&session_actions_.cycle_inventory&&session_actions_.cycle_inventory(value);};
    dispatch_actions_.last_inventory=[this]{return !session_stop_requested()&&session_actions_.last_inventory&&session_actions_.last_inventory();};
    dispatch_actions_.select_weapon=[this](auto name){return !session_stop_requested()&&session_actions_.select_weapon&&session_actions_.select_weapon(name);};
    register_commands();register_settings();
    cvars_.observe_applied("hlc_msaa",std::int64_t{0}); // no multisampled scene has been created yet
    if(game_registration)game_registration(commands_,bindings_,dispatch_actions_,[this](Action a,bool down){action(a,down);});
    last_endpoint_=options_.connect_endpoint;
    controller_.set_open(true,seconds());
    model_.append("HL Client Engine - console ready. Type hlc_help for help.");
    std::optional<std::string> startup_config;
    if(startup_config_loader)startup_config=startup_config_loader();
    else if(config_){std::string error;startup_config=config_->read("console.cfg",error);}
    if(startup_config){
        startup_config_=true;
        execute(*startup_config,ExecutionSource::local_config);
        // Process a bounded startup script before any new generation discards
        // pending work. Cycles have an operation-wide budget in the executor.
        for(unsigned step=0;step<32;++step)
            (void)executor_.process(state_,generation_,32,[this](auto line){model_.append(line);});
        executor_.discard_pending();
        startup_config_=false;
    }
    (void)window_.set_text_input_enabled(true);
}
ConsoleApplication::~ConsoleApplication(){(void)window_.set_text_input_enabled(false);}
double ConsoleApplication::seconds() const noexcept {return monotonic_seconds_?monotonic_seconds_():std::chrono::duration<double>{Clock::now()-origin_}.count();}
void ConsoleApplication::set_session_actions(ConsoleSessionActions actions){session_actions_=std::move(actions);}
void ConsoleApplication::set_work(std::shared_ptr<LoadingWork> work){work_=std::move(work);}
void ConsoleApplication::set_stage(LoadingStage stage){loading_.stage=stage;loading_.percent.reset();}
void ConsoleApplication::begin_attempt(){
    renderer_.reset_session_resources();
    ++generation_;loading_.begin(generation_);state_=SessionState::loading;disconnect_=false;
    curtain_.reset(seconds());
    model_.append("Connecting...");
    executor_.discard_pending();pending_actions_.clear();
    bindings_.release_all([](Action,bool){});
    controller_.set_open(false,seconds());
}
void ConsoleApplication::end_attempt(int result,std::string_view reason){
    if(work_)work_->cancelled.store(true);work_.reset();
    session_actions_={};executor_.discard_pending();release_actions(nullptr);disconnect_=false;
    state_=result ? SessionState::error : SessionState::idle;
    curtain_.reset(seconds());
    loading_.stage=result ? LoadingStage::failed : LoadingStage::idle;
    loading_.error=result ? std::string{reason}:std::string{};
    model_.append(result ? "Connection ended: "+std::string{reason}:"Disconnected.");
    controller_.set_open(true,seconds());
    (void)window_.request_relative_mouse_capture(false);
}
void ConsoleApplication::begin_disconnect(){
    state_=SessionState::disconnecting;loading_.stage=LoadingStage::disconnecting;loading_.percent.reset();
    curtain_.reset(seconds());
    if(work_)work_->cancelled.store(true);release_actions(nullptr);
    (void)window_.request_relative_mouse_capture(false);
}
void ConsoleApplication::in_game(){
    if(state_==SessionState::in_game)return;
    state_=SessionState::in_game;loading_.stage=LoadingStage::ready;loading_.percent=100U;
    curtain_.presented(seconds());
    model_.append("Scene ready. Press the key left of 1 to open the console.");
}
void ConsoleApplication::execute(std::string_view command,ExecutionSource source){
    const auto result=executor_.enqueue(command,source,generation_);
    if(!result.success)model_.append(result.message);
}
void ConsoleApplication::action(Action value,bool down){
    if(down&&session_stop_requested())return;
    if(pending_actions_.size()>=256U)return;
    if(value==Action::attack_primary||value==Action::attack_secondary){
        const auto button=value==Action::attack_primary?input::PhysicalMouseButton::left:input::PhysicalMouseButton::right;
        pending_actions_.push_back(down?Event::mouse_button_pressed(button):Event::mouse_button_released(button));return;
    }
    static const std::pair<Action,Key> keys[]{{Action::move_forward,Key::w},{Action::move_backward,Key::s},
        {Action::move_left,Key::a},{Action::move_right,Key::d},{Action::jump,Key::space},{Action::duck,Key::left_control},
        {Action::speed,Key::left_shift},{Action::use,Key::e},{Action::reload,Key::r},{Action::scoreboard,Key::tab}};
    for(const auto& [a,key]:keys)if(a==value){pending_actions_.push_back(down?Event::key_pressed(key):Event::key_released(key));break;}
}
void ConsoleApplication::release_actions(input::InputStateTracker* tracker){
    bindings_.release_all([this](Action a,bool down){action(a,down);});
    if(tracker){for(const auto& event:pending_actions_)tracker->apply_event(event);pending_actions_.clear();
        tracker->apply_event(Event::focus_lost());
        if(window_.focus_state()==input::InputFocusState::focused)tracker->apply_event(Event::focus_gained());}
    else {pending_actions_.clear();pending_release_all_=true;}
}
void ConsoleApplication::toggle(input::InputStateTracker* tracker){
    controller_.toggle(seconds());
    if(controller_.target_open()){release_actions(tracker);(void)window_.request_relative_mouse_capture(false);ignore_opening_text_=true;}
    (void)window_.set_text_input_enabled(controller_.takes_input());
}
void ConsoleApplication::begin_frame(input::InputStateTracker* tracker){
    frame_started_=Clock::now();controller_.advance(seconds());
    (void)window_.set_text_input_enabled(controller_.takes_input());
    window_.set_escape_input_owned(true);
    (void)executor_.process(state_,generation_,32,[this](auto line){model_.append(line);});
    if(tracker&&pending_release_all_){pending_release_all_=false;release_actions(tracker);}
    if(tracker){for(const auto& event:pending_actions_)tracker->apply_event(event);pending_actions_.clear();}
    if(work_)(void)loading_.observe(*work_);
    const auto logs=core::drain_console_log();
    for(const auto& record:logs.records)model_.append(record.message);
    if(logs.dropped_lines)model_.append("[console] dropped log lines: "+std::to_string(logs.dropped_lines));
}
bool ConsoleApplication::route_event(const input::InputEvent& event,input::InputStateTracker& tracker){
    if(event.type()==EventType::focus_lost){control_keys_=shift_keys_=0;control_=shift_=false;release_actions(&tracker);return false;}
    if(event.type()==EventType::focus_gained||event.type()==EventType::capture_released||event.type()==EventType::capture_acquired)return false;
    const bool down=event.type()==EventType::key_pressed;
    if(down||event.type()==EventType::key_released){
        const auto key=event.key();
        if(key==Key::left_control||key==Key::right_control){const unsigned bit=key==Key::left_control?1U:2U;
            if(down)control_keys_|=bit;else control_keys_&=~bit;control_=control_keys_!=0;}
        if(key==Key::left_shift||key==Key::right_shift){const unsigned bit=key==Key::left_shift?1U:2U;
            if(down)shift_keys_|=bit;else shift_keys_&=~bit;shift_=shift_keys_!=0;}
        if(key==Key::grave){if(down&&!event.repeated())toggle(&tracker);return true;}
        if(down&&key==Key::escape&&controller_.takes_input()){
            controller_.set_open(false,seconds());return true;}
        if(key==Key::f8)return false; // diagnostic marker is explicit even while console owns input
    }
    const bool owns=controller_.takes_input()||state_!=SessionState::in_game;
    if(owns){
        // A loading/status screen blocks gameplay but is not an invisible editor.
        // Only the explicit console toggle above may enable command entry here.
        if(!controller_.takes_input())return true;
        if(event.type()==EventType::text_input){
            if(ignore_opening_text_){ignore_opening_text_=false;const auto text=event.text();
                if(text=="`"||text=="~"||text=="ґ"||text=="Ґ"||text=="ё"||text=="Ё")return true;}
            if(!control_)model_.insert(event.text());
        }else if(event.type()==EventType::mouse_wheel)model_.scroll(static_cast<int>(event.wheel_delta().vertical*3));
        else if(down){
            switch(event.key()){
            case Key::enter:if(!event.repeated()){
                const bool safe=executor_.safe_for_history(model_.editor());
                auto command=model_.submit(safe);if(safe)model_.append("] "+command);
                execute(command);model_.bottom();}break;
            case Key::left:model_.left();break;case Key::right:model_.right();break;
            case Key::home:model_.home();break;case Key::end:if(control_)model_.bottom();else model_.end();break;
            case Key::backspace:model_.backspace();break;case Key::delete_key:model_.erase_forward();break;
            case Key::up:model_.history_previous();break;case Key::down:model_.history_next();break;
            case Key::page_up:model_.scroll(12);break;case Key::page_down:model_.scroll(-12);break;
            case Key::tab:{const auto candidates=executor_.complete(model_.editor());model_.complete(candidates);break;}
            case Key::a:if(control_)model_.select_all();break;
            case Key::c:if(control_)(void)window_.set_clipboard_text(model_.selected_all()?model_.copy_selection():loading_.error);break;
            case Key::v:if(control_)model_.insert(window_.clipboard_text());break;
            default:break;
            }
        }
        return true;
    }
    if(event.type()==EventType::text_input)return true;
    std::string key;bool pressed{};
    if(down||event.type()==EventType::key_released){key=key_name(event.key());pressed=down;
        if(event.key()==Key::escape)return false;}
    else if(event.type()==EventType::mouse_button_pressed||event.type()==EventType::mouse_button_released){
        if(window_.capture_state()!=input::InputCaptureState::captured)return false;
        key=mouse_name(event.mouse_button());pressed=event.type()==EventType::mouse_button_pressed;
    }else if(event.type()==EventType::mouse_wheel){
        if(event.wheel_delta().vertical==0)return true;
        key=event.wheel_delta().vertical>0?"MWHEELUP":"MWHEELDOWN";pressed=true;
    }else return false;
    (void)bindings_.key_event(key,pressed,event.repeated(),[this](Action a,bool value){action(a,value);},
        [this](auto command){execute(command,ExecutionSource::local_binding);});
    if(event.type()==EventType::mouse_wheel)(void)bindings_.key_event(key,false,false,[](Action,bool){},[](auto){});
    for(const auto& pending:pending_actions_)tracker.apply_event(pending);pending_actions_.clear();
    return true;
}
void ConsoleApplication::pump_idle(){
    input::InputStateTracker tracker;tracker.begin_frame();begin_frame(&tracker);
    platform::PlatformEvent event{platform::WindowEvent{}};
    while(window_.poll_event(event)){
        if(const auto* value=std::get_if<platform::WindowEvent>(&event)){
            if(value->type==platform::WindowEventType::quit_requested)request_quit();
        }else (void)route_event(std::get<input::InputEvent>(event),tracker);
    }
    (void)tracker.publish_snapshot();tracker.end_frame();present_idle();pace();
}
void ConsoleApplication::compose(renderer::RenderScene& scene,bool scene_ready_for_presentation){
    // A candidate scene is not successful presentation. in_game() is called
    // by the owner only after render/swap and starts the automatic curtain.
    (void)scene_ready_for_presentation;
    controller_.advance(seconds());curtain_.advance(seconds());const auto extent=window_.pixel_extent();
    const auto width=static_cast<unsigned>(std::max(1,extent.width)),height=static_cast<unsigned>(std::max(1,extent.height));
    const bool full=state_!=SessionState::in_game;
    const auto fraction=controller_.height_fraction();
    auto frame=theme_&&(full||fraction>0)?theme_(width,height,.5,full):renderer::ScreenUiFrame{};
    const float scale=std::clamp(std::floor(static_cast<float>(height)/540.0F),1.0F,4.0F);
    const float advance=6.0F*scale,line_height=11.0F*scale,margin=16.0F*scale;
    const float panel=static_cast<float>(height)*(full?1.0F:.5F);
    const std::array<float,4> amber{1.0F,0.72F,0.25F,1.0F};
    scene.screen_ui.reset();
    if(full&&!controller_.takes_input()){
        std::string stage=loading_stage_label(loading_.stage);
        if(loading_.percent)stage+=" "+std::to_string(*loading_.percent)+"%";
        else if(loading_.stage!=LoadingStage::idle&&loading_.stage!=LoadingStage::failed)stage+=" "+std::string(static_cast<std::size_t>(seconds()*3)%4,'.');
        frame.commands.emplace_back(renderer::ScreenUiText{stage,margin,float(height)-line_height*3,scale,advance,line_height,amber,{}});
        frame.commands.emplace_back(renderer::ScreenUiText{"~ Console    disconnect - cancel",margin,float(height)-line_height*2,scale,advance,line_height,amber,{}});
    }
    if((controller_.takes_input()||fraction>0)&&panel>line_height*3&&float(width)>margin*2+advance*4){
        if(full){
            auto status=std::string{loading_stage_label(loading_.stage)};
            if(loading_.percent)status+=" "+std::to_string(*loading_.percent)+"%";
            frame.commands.emplace_back(renderer::ScreenUiText{status,margin,52*scale,scale,advance,line_height,amber,
                renderer::ScreenUiClip{margin,52*scale,float(width)-margin*2,line_height}});
        }
        const auto layout=console_output_layout(width,height,panel);
        const float content_width=layout.output.width;
        const float editor_y=layout.editor_y;
        const auto columns=layout.columns,rows=layout.rows;
        if(rows>0){
            auto lines=model_.visible_lines(columns,rows);
            // Actual text, not rows*maximum columns, consumes renderer budget.
            // Only pathological dense/huge viewports shorten individual rows;
            // each real visible row retains its place and an explicit ellipsis.
            std::size_t bytes{},glyphs{};
            for(const auto& line:lines){bytes+=line.size();for(unsigned char c:line)if((c&0xc0U)!=0x80U)++glyphs;}
            if(bytes>24576U||glyphs>12288U){
                const auto byte_limit=24576U/lines.size(),glyph_limit=12288U/lines.size();
                for(auto& line:lines){
                    std::size_t end{},count{};
                    while(end<line.size()&&count+3<glyph_limit){
                        auto next=end+1;while(next<line.size()&&(static_cast<unsigned char>(line[next])&0xc0U)==0x80U)++next;
                        if(next+3>byte_limit)break;end=next;++count;
                    }
                    if(end<line.size())line=line.substr(0,end)+"...";
                }
            }
            const auto history_clip=layout.output;
            float y=editor_y-line_height-static_cast<float>(lines.size())*line_height;
            for(const auto& line:lines){frame.commands.emplace_back(renderer::ScreenUiText{line,margin,y,scale,advance,line_height,amber,history_clip});y+=line_height;}
        }
        // The editor may contain pasted line separators, but none is executed
        // or rendered as a new input row. It stays one clipped visual line.
        const auto& editor=model_.editor();
        std::vector<std::size_t> starts;starts.reserve(editor.size()+1U);
        for(std::size_t offset=0;offset<editor.size();++offset)
            if((static_cast<unsigned char>(editor[offset])&0xc0U)!=0x80U)starts.push_back(offset);
        starts.push_back(editor.size());
        auto cursor=std::min(model_.cursor(),editor.size());
        while(cursor>0&&cursor<editor.size()&&(static_cast<unsigned char>(editor[cursor])&0xc0U)==0x80U)--cursor;
        const auto cursor_column=static_cast<std::size_t>(std::lower_bound(starts.begin(),starts.end(),cursor)-starts.begin());
        const auto capacity=std::max<std::size_t>(1,columns-3U); // prompt and visible caret
        const auto first=cursor_column>capacity?cursor_column-capacity:0U;
        const auto last=std::min(starts.size()-1U,first+capacity);
        std::string visible;visible.reserve((last-first)*2U);
        for(std::size_t glyph=first;glyph<last;++glyph){
            const auto codepoint=std::string_view{editor}.substr(starts[glyph],starts[glyph+1]-starts[glyph]);
            if(codepoint.size()==1U&&static_cast<unsigned char>(codepoint.front())<32U)visible+=' ';
            else visible+=codepoint;
        }
        const renderer::ScreenUiClip editor_clip{margin,editor_y,content_width,line_height};
        if(model_.selected_all()&&!visible.empty())frame.commands.emplace_back(renderer::ScreenUiRectangle{
            margin+advance*2,editor_y,static_cast<float>(last-first)*advance,line_height,{.28F,.25F,.14F,1},editor_clip});
        frame.commands.emplace_back(renderer::ScreenUiText{(first?"< ":"] ")+visible,margin,editor_y,scale,advance,line_height,amber,editor_clip});
        if(static_cast<unsigned>(seconds()*2)%2==0){
            const float caret_x=std::min(float(width)-margin-scale,
                margin+static_cast<float>(cursor_column-first+2U)*advance);
            frame.commands.emplace_back(renderer::ScreenUiRectangle{caret_x,editor_y,scale,line_height,amber,editor_clip});
        }
        if(model_.scroll_offset())frame.commands.emplace_back(renderer::ScreenUiText{"^^",float(width)-margin-advance*3,margin,scale,advance,line_height,amber,
            renderer::ScreenUiClip{0,0,float(width),panel}});
    }
    // Translate a stable-size manual panel once; wrapping does not change as
    // the panel slides. Automatic loading visibility has a separate owner.
    if(!full)translate_console_panel(frame,float(height*fraction)-panel,width,float(height*fraction));
    if(!full&&curtain_.fraction()>0&&theme_){
        auto automatic=theme_(width,height,.5,true);
        const auto shown=float(height*curtain_.fraction());
        translate_console_panel(automatic,shown-float(height),width,shown);
        automatic.commands.insert(automatic.commands.end(),std::make_move_iterator(frame.commands.begin()),std::make_move_iterator(frame.commands.end()));
        frame=std::move(automatic);
    }
    if(show_fps_)frame.commands.emplace_back(renderer::ScreenUiText{std::to_string(static_cast<unsigned>(std::isfinite(fps_)?std::clamp(fps_,0.0,99999.0):0.0))+" FPS",float(width)-90,8,1,6,9,{1,1,0,1},{}});
    if(!frame.commands.empty())scene.screen_ui=std::move(frame);
}
void ConsoleApplication::present_idle(){
    renderer::RenderScene scene;scene.clear_color={0.08F,0.065F,0.05F,1};compose(scene);
    const auto extent=window_.pixel_extent();renderer_.render(scene,{std::max(1,extent.width),std::max(1,extent.height)});
    window_.swap_buffers();
}
void ConsoleApplication::pace(){
    cvars_.observe_applied("hlc_msaa",std::int64_t{renderer_.scene_target_statistics().actual_samples});
    const auto now=Clock::now();const auto duration=std::chrono::duration<double>{now-last_present_}.count();
    if(duration>0)fps_=1.0/duration;last_present_=now;
    if(options_.movement_diagnostics){++pacing_statistics_.observations;pacing_statistics_.last_wait_us=0;}
    if(fps_max_>0){const auto target=frame_started_+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{1.0/fps_max_});
        if(target>now){
            // Only observe the existing limiter. No additional wait, altered
            // frame deadline, VSync policy or usercmd timestep is introduced.
            const auto wait_begin=options_.movement_diagnostics?Clock::now():Clock::time_point{};
            std::this_thread::sleep_until(target);
            if(options_.movement_diagnostics){
                const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-wait_begin).count();
                const auto wait=static_cast<std::uint64_t>(std::max<std::int64_t>(0,elapsed));
                ++pacing_statistics_.sleep_calls;pacing_statistics_.last_wait_us=wait;
                pacing_statistics_.total_wait_us+=wait;
                pacing_statistics_.maximum_wait_us=std::max(pacing_statistics_.maximum_wait_us,wait);
            }
        }}
}
void ConsoleApplication::request_quit(){++session_request_revision_;shutdown_=true;connect_.reset();if(work_)work_->cancelled.store(true);}
std::optional<std::string> ConsoleApplication::take_connect(){auto value=std::move(connect_);connect_.reset();return value;}
void ConsoleApplication::persist_shared_settings(){
#ifdef _WIN32
    if(!shared_settings_path_)return;
    auto edited=shared_baseline_;edited.image=options_.image;edited.video=options_.video;
    edited.hud_scale=options_.hud_scale;edited.volume=options_.audio_volume;
    const auto parent=platform::windows::open_secure_output_directory(shared_settings_path_->parent_path().parent_path());
    std::error_code error;
    if(parent)std::filesystem::create_directory(shared_settings_path_->parent_path(),error);
    if(parent&&!error&&platform::windows::merge_external_settings(*shared_settings_path_,shared_baseline_,edited))shared_baseline_=std::move(edited);
    else model_.append("Unable to save shared launcher settings.");
#endif
}
void ConsoleApplication::register_commands(){
    const auto add=[&](std::string name,std::string help,std::vector<ArgumentSpec> args,CommandHandler handler,bool variadic=false){
        CommandDescriptor descriptor;descriptor.name=std::move(name);descriptor.description=help;descriptor.usage=descriptor.name+" "+help;
        descriptor.arguments=std::move(args);descriptor.variadic=variadic;
        if(descriptor.name=="connect"||descriptor.name=="retry"||descriptor.name=="disconnect"||descriptor.name=="quit"||descriptor.name=="exit")
            descriptor.semantics=CommandSemantics::session_request;
        if(descriptor.semantics==CommandSemantics::session_request) {
            handler=[this,backend=std::move(handler)](const auto& context,auto args) {
                if(startup_config_)return CommandResult{false,"Session commands are not permitted in automatic startup configuration; enter the command or exec explicitly."};
                return backend(context,args);
            };
        }
        if(help.starts_with("Unsupported:"))descriptor.semantics=CommandSemantics::unsupported;
        if(!commands_.add(std::move(descriptor),std::move(handler)))throw std::runtime_error{"Duplicate console command"};};
    add("toggleconsole","Toggle console",{},[this](auto&,auto){toggle(nullptr);return CommandResult{};});
    add("clear","Clear displayed scrollback",{},[this](auto&,auto){model_.clear();return CommandResult{};});
    add("echo","Print text",{text_spec("text",true)},[](auto&,auto args){std::string text;for(const auto& a:args){if(!text.empty())text+=' ';text+=value_text(a);}return CommandResult{true,text};},true);
    add("cmdlist","List commands",{},[this](auto&,auto){std::string text;for(const auto& d:commands_.descriptors())text+=d.name+"\n";return CommandResult{true,text};});
    add("cvarlist","List settings",{},[this](auto&,auto){std::string text;for(const auto& d:cvars_.descriptors())text+=d.command.name+"\n";return CommandResult{true,text};});
    add("hlc_help","[name] Command and setting help",{text_spec("name",true)},[this](auto&,auto args){
        if(args.empty())return CommandResult{true,"connect <IPv4:port>; disconnect; retry; bind; exec; hlc_writeconfig; cmdlist; cvarlist"};
        const auto name=value_text(args[0]);if(const auto* d=commands_.find(name))return CommandResult{true,d->usage+"\n"+d->description};
        if(const auto* d=cvars_.find(name))return CommandResult{true,d->command.description};return CommandResult{false,"Unknown command: "+name};});
    add("connect","<IPv4:port>",{text_spec("endpoint")},[this](auto&,auto args){const auto endpoint=core::normalize_external_endpoint(text_argument(args[0]),true);
        if(!endpoint)return CommandResult{false,"Invalid IPv4 endpoint"};++session_request_revision_;connect_=*endpoint;last_endpoint_=*endpoint;
        disconnect_=state_==SessionState::loading||state_==SessionState::in_game;if(work_&&disconnect_)work_->cancelled.store(true);return CommandResult{true,"Connecting to "+*endpoint};});
    add("retry","Retry last endpoint",{},[this](auto&,auto){if(!last_endpoint_)return CommandResult{false,"No previous endpoint"};
        ++session_request_revision_;connect_=last_endpoint_;disconnect_=state_==SessionState::loading||state_==SessionState::in_game;
        if(work_&&disconnect_)work_->cancelled.store(true);return CommandResult{};});
    add("disconnect","Return to the console",{},[this](auto&,auto){++session_request_revision_;connect_.reset();disconnect_=true;if(work_)work_->cancelled.store(true);return CommandResult{};});
    for(const auto name:{"quit","exit"})add(name,"Close this application",{},[this](auto&,auto){request_quit();return CommandResult{};});
    add("bind","<key> [command]",{text_spec("key"),text_spec("command",true)},[this](auto&,auto args){
        if(args.size()==1){auto key=text_argument(args[0]);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
            const auto found=bindings_.bindings().find(key);return CommandResult{true,found==bindings_.bindings().end()?"Key is not bound":found->second};}
        return bindings_.bind(text_argument(args[0]),text_argument(args[1]));});
    add("unbind","<key>",{text_spec("key")},[this](auto&,auto args){bindings_.unbind(text_argument(args[0]));return CommandResult{};});
    add("unbindall","Remove all bindings",{},[this](auto&,auto){bindings_.unbind_all();return CommandResult{};});
    add("hlc_bindlist","List bindings",{},[this](auto&,auto){return CommandResult{true,bindings_.persistent_config()};});
    add("hlc_writeconfig","[file.cfg] Save own configuration",{text_spec("file",true)},[this](auto&,auto args){
        if(!config_)return CommandResult{false,"User config directory unavailable"};std::string error;
        const auto name=args.empty()?"console.cfg":text_argument(args[0]);
        const auto text="// hlclient.console-config.v1\n"+cvars_.persistent_config()+bindings_.persistent_config()+executor_.persistent_aliases();
        const bool saved=config_->write(name,text,error);return CommandResult{saved,saved?"Configuration saved":error};});
    for(const auto name:{"reconnect","map","changelevel","rcon","god","noclip","record","playdemo","stop","net_graph","rate","cl_cmdrate","cl_updaterate","ex_interp"})
        add(name,"Unsupported: engine/server authority or scheduler integration is not implemented",{text_spec("argument",true)},
            [](auto&,auto){return CommandResult{false,"Unsupported: no validated backend for this command; use retry for a fresh connection."};},true);
}
void ConsoleApplication::register_settings(){
    const auto add=[&](std::string name,std::string help,ConsoleValue initial,ValueType type,double low,double high,CVarApply apply,bool shared=false,bool restart=false){
        CVarDescriptor d;d.command.name=name;d.command.description=help;d.command.usage=name+" [value]";
        d.command.persistent=!shared;d.command.restart_required=restart;d.value={"value",type,false,low,high,64};d.default_value=initial;
        if(!cvars_.add(std::move(d),initial,std::move(apply)))throw std::runtime_error{"Duplicate cvar"};};
    add("sensitivity","Base mouse multiplier; 3 preserves current 0.10 degrees/pixel; zoom multiplier retained",3.0,ValueType::real,0.1,30,
        [this](const auto& value,auto& actual){sensitivity_=std::get<double>(value);actual=value;return CommandResult{};});
    add("volume","Audio master 0..1",double(options_.audio_volume)/100.0,ValueType::real,0,1,
        [this](const auto& value,auto& actual){options_.audio_volume=static_cast<unsigned>(std::lround(std::get<double>(value)*100));actual=double(options_.audio_volume)/100;persist_shared_settings();return CommandResult{};},true);
    const auto boolean=[&](const char* name,bool& setting){add(name,"0 off, 1 on",setting,ValueType::boolean,0,1,[&setting](const auto& value,auto& actual){setting=std::get<bool>(value);actual=value;return CommandResult{};});};
    boolean("crosshair",options_.crosshair);boolean("hud_draw",options_.hud_draw);boolean("hud_fastswitch",options_.hud_fastswitch);boolean("cl_showfps",show_fps_);
    add("fps_max","Presentation frame cap; 0 unlimited, does not change usercmd timestep",fps_max_,ValueType::real,0,1000,
        [this](const auto& value,auto& actual){const auto fps=std::get<double>(value);if(fps>0&&fps<20)return CommandResult{false,"fps_max must be 0 or 20..1000"};fps_max_=fps;actual=value;return CommandResult{};});
    const auto image_setting=[&](const char* name,float& setting,double low,double high){add(name,"Local image-correction control (shared with launcher)",double(setting),ValueType::real,low,high,
        [this,&setting](const auto& value,auto& actual){setting=static_cast<float>(std::get<double>(value));actual=double(setting);persist_shared_settings();return CommandResult{};},true);};
    image_setting("hlc_gamma",options_.image.gamma,.25,4);image_setting("hlc_brightness",options_.image.brightness,-.5,.5);image_setting("hlc_contrast",options_.image.contrast,0,2);
    add("hlc_hud_scale","0 automatic, or 0.5..2",options_.hud_scale.automatic?0.0:double(options_.hud_scale.scale),ValueType::real,0,2,
        [this](const auto& value,auto& actual){const auto v=std::get<double>(value);if(v>0&&v<.5)return CommandResult{false,"Use 0 or 0.5..2"};
        options_.hud_scale.automatic=v==0;if(v)options_.hud_scale.scale=static_cast<float>(v);actual=value;persist_shared_settings();return CommandResult{};},true);
    add("hlc_msaa","Requested scene MSAA: 0,2,4,8; actual depends on GPU",std::int64_t{options_.video.msaa_samples},ValueType::integer,0,8,
        [this](const auto& value,auto& actual){const auto count=static_cast<unsigned>(std::get<std::int64_t>(value));if(!core::valid_msaa_samples(count))return CommandResult{false,"Use 0,2,4,8"};
        options_.video.msaa_samples=count;actual=std::int64_t{renderer_.scene_target_statistics().actual_samples};persist_shared_settings();
        return CommandResult{true,"Requested; actual MSAA is reported after the next scene render"};},true);
}
}
