#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/app/external_session.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/platform/windows/external_launcher.hpp>
#include <hlclient/platform/windows/executable_launch_path.hpp>
#include <hlclient/platform/windows/external_launch_preflight.hpp>
#include <hlclient/platform/windows/session_receipt_file.hpp>
#include <hlclient/platform/windows/shared_test_storage.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
#include "smooth_scroll.hpp"
#include "smooth_scroll_check.hpp"
#include "startup_journal.hpp"
#include "gpu_preference.hpp"
#include "original_half_life.hpp"

namespace {
namespace core=hlclient::core;
namespace app=hlclient::app;
namespace platform=hlclient::platform;
namespace native=hlclient::platform::windows;
using Clock=std::chrono::steady_clock;
enum Control : int {server=101,name,display,resolution,mode,vsync,brightness,gamma,contrast,volume,
    reset_image,assets,choose_assets,runtime,choose_runtime,connect_button,cancel_button,report_button,status_label,
    image_values,dependency_label,client_label,mode_note,save_button,
    hud_mode,hud_scale,hud_values,reset_hud,msaa,movement_diagnostics,movement_hotkey,open_client_button,system_diagnostics,
    startup_folder,startup_export,show_fps,gpu_choice,gpu_actual,gpu_settings,original_hl_button,background_sky};
struct Positioned {HWND control{};int x{},y{},w{},h{};};
struct Progress {bool finished{},success{},started{},cancel_timed_out{},system_diagnostics_requested{},system_archive_ready{},original_hl{};std::string stage{"checking_resources"},reason,system_helper_status{"not_requested"},actual_gpu;std::filesystem::path report,system_run_folder;std::optional<core::SceneAntialiasingObservation> antialiasing;};
bool fullscreen_close_confirmed(const Progress& progress) noexcept
{
    return progress.finished && progress.success &&
        (progress.reason=="normal_close_completed" || progress.reason=="application_closed");
}
struct Worker {
    std::thread thread;std::mutex mutex;Progress progress;std::atomic_bool cancel{},force{};
    ~Worker(){if(thread.joinable())thread.join();}
    void publish(Progress p){std::scoped_lock guard{mutex};progress=std::move(p);}
    Progress snapshot(){std::scoped_lock guard{mutex};return progress;}
};
struct Ui {
    hlclient::launcher::StartupJournal* startup{};
    HWND window{};HFONT font{};UINT dpi{96};std::vector<Positioned> controls;
    std::filesystem::path executable,settings,storage,last_report;
    core::ExternalLaunchConfiguration configuration;
    core::ExternalLaunchConfiguration settings_baseline;
    std::optional<core::VideoSettings> recovery_video_baseline;
    std::vector<platform::VideoDisplay> displays;
    std::vector<std::pair<int,int>> resolutions;
    core::ExternalLauncherLifecycle lifecycle;
    hlclient::launcher::GpuPreference gpu_preference{hlclient::launcher::GpuPreference::automatic};
    native::BackgroundSkyPolicy sky_policy{native::BackgroundSkyPolicy::original};
    std::unique_ptr<Worker> worker;
    bool close_after{},offline_check{},scroll_timer{};int scroll_pixels{},offline_exit_code{2};std::string settings_status;
    UINT font_dpi{};hlclient::launcher::SmoothScroll scroll;
    ~Ui(){if(font)::DeleteObject(font);}
};
int scaled(Ui&ui,int value){return ::MulDiv(value,static_cast<int>(ui.dpi),96);}
HWND get(Ui&ui,int id){return ::GetDlgItem(ui.window,id);}
void text(Ui&ui,int id,const std::wstring&value){::SetWindowTextW(get(ui,id),value.c_str());}
std::wstring read_control(Ui&ui,int id){const auto h=get(ui,id);const int n=::GetWindowTextLengthW(h);if(n<0 || n>4096)return {};std::wstring out(static_cast<std::size_t>(n)+1,0);::GetWindowTextW(h,out.data(),n+1);out.resize(static_cast<std::size_t>(n));return out;}
void choice(Ui&ui,int id,const std::wstring&value){::SendMessageW(get(ui,id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value.c_str()));}
int selected(Ui&ui,int id){return static_cast<int>(::SendMessageW(get(ui,id),CB_GETCURSEL,0,0));}
void select(Ui&ui,int id,int n){::SendMessageW(get(ui,id),CB_SETCURSEL,static_cast<WPARAM>(n),0);}
int slider(Ui&ui,int id){return static_cast<int>(::SendMessageW(get(ui,id),TBM_GETPOS,0,0));}
void slide(Ui&ui,int id,int value){::SendMessageW(get(ui,id),TBM_SETPOS,TRUE,value);}
constexpr UINT_PTR kWorkerTimer=1,kScrollTimer=2;
double scroll_time(){return std::chrono::duration<double>{Clock::now().time_since_epoch()}.count();}
void stop_scrolling(Ui&ui)
{
    ::KillTimer(ui.window,kScrollTimer);ui.scroll_timer=false;ui.scroll.jump(ui.scroll.position());
}
void position_controls(Ui&ui,bool geometry=false)
{
    // One physical offset for every child avoids relative rounding wobble at
    // fractional DPI. Scroll frames neither recreate fonts nor resize controls.
    const int offset=static_cast<int>(std::lround(ui.scroll.position()*ui.dpi/96.0));
    if(!geometry && offset==ui.scroll_pixels)return;
    ui.scroll_pixels=offset;
    SCROLLINFO info{sizeof(SCROLLINFO),SIF_POS,0,0,0,static_cast<int>(std::lround(ui.scroll.position())),0};
    ::SetScrollInfo(ui.window,SB_VERT,&info,TRUE);
    const UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS|(geometry?0U:SWP_NOSIZE);
    auto batch=::BeginDeferWindowPos(static_cast<int>(ui.controls.size()));
    for(const auto&p:ui.controls){if(!batch)break;batch=::DeferWindowPos(batch,p.control,nullptr,scaled(ui,p.x),scaled(ui,p.y)-offset,scaled(ui,p.w),scaled(ui,p.h),flags);}
    if(!batch || !::EndDeferWindowPos(batch))for(const auto&p:ui.controls)
        ::SetWindowPos(p.control,nullptr,scaled(ui,p.x),scaled(ui,p.y)-offset,scaled(ui,p.w),scaled(ui,p.h),flags);
    // Queue one paint for the composited native form, not one synchronous
    // erase/paint per child. Do not toggle WM_SETREDRAW on the hidden test UI.
    ::RedrawWindow(ui.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
void scroll_to(Ui&ui,double position)
{
    stop_scrolling(ui);ui.scroll.jump(position);position_controls(ui);
}
void scroll_tick(Ui&ui,double now)
{
    static_cast<void>(ui.scroll.advance(now));position_controls(ui);
    if(!ui.scroll.active()){::KillTimer(ui.window,kScrollTimer);ui.scroll_timer=false;}
}
void scroll_wheel(Ui&ui,short delta,double now,UINT lines,bool animate)
{
    if(lines==0U || delta==0)return;
    RECT client{};::GetClientRect(ui.window,&client);
    const double distance=lines==WHEEL_PAGESCROLL?static_cast<double>(client.bottom)*96.0/ui.dpi:
        static_cast<double>(lines)*16.0;
    ui.scroll.move_by(-static_cast<double>(delta)/WHEEL_DELTA*distance,now);
    if(!animate){scroll_to(ui,ui.scroll.target());return;}
    position_controls(ui);
    if(ui.scroll.active() && !ui.scroll_timer){
        ui.scroll_timer=::SetTimer(ui.window,kScrollTimer,16,nullptr)!=0;
        if(!ui.scroll_timer)scroll_to(ui,ui.scroll.target());
    }
    if(!ui.scroll.active())stop_scrolling(ui);
}
void layout(Ui&ui)
{
    stop_scrolling(ui);
    if(!ui.font || ui.font_dpi!=ui.dpi){
        const auto next=::CreateFontW(-scaled(ui,14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(next){const auto previous=ui.font;ui.font=next;ui.font_dpi=ui.dpi;
            for(const auto&p:ui.controls)::SendMessageW(p.control,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
            if(previous)::DeleteObject(previous);}
    }
    RECT client{};::GetClientRect(ui.window,&client);
    const int page=::MulDiv(client.bottom,96,static_cast<int>(ui.dpi));
    int content_height=0;for(const auto&p:ui.controls)content_height=std::max(content_height,p.y+p.h+20);
    ui.scroll.reset(ui.scroll.position(),std::max(0,content_height-page));
    SCROLLINFO scroll{sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE|SIF_POS,0,content_height-1,static_cast<UINT>(std::max(1,page)),static_cast<int>(std::lround(ui.scroll.position())),0};
    ::SetScrollInfo(ui.window,SB_VERT,&scroll,TRUE);
    if(get(ui,gpu_choice))::SendMessageW(get(ui,gpu_choice),CB_SETDROPPEDWIDTH,static_cast<WPARAM>(scaled(ui,490)),0);
    position_controls(ui,true);
}
void scroll_bar(Ui&ui,UINT code,int track,int page)
{
    double position=ui.scroll.position();
    switch(code){
    case SB_THUMBTRACK:case SB_THUMBPOSITION:position=track;break;
    case SB_LINEUP:position-=24;break;case SB_LINEDOWN:position+=24;break;
    case SB_PAGEUP:position-=page;break;case SB_PAGEDOWN:position+=page;break;
    case SB_TOP:position=0;break;case SB_BOTTOM:position=ui.scroll.maximum();break;
    default:break;
    }
    scroll_to(ui,position);
}
void reveal_control(Ui&ui,HWND window)
{
    stop_scrolling(ui);
    const auto p=std::find_if(ui.controls.begin(),ui.controls.end(),[window](const auto&value){return value.control==window;});
    if(p==ui.controls.end())return;
    RECT area{};::GetClientRect(ui.window,&area);const int page=::MulDiv(area.bottom,96,static_cast<int>(ui.dpi));
    const int height=std::min(30,p->h);
    if(p->y<ui.scroll.position())scroll_to(ui,p->y);
    else if(p->y+height>ui.scroll.position()+page)scroll_to(ui,p->y+height-page);
}
LRESULT CALLBACK focus_subclass(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR data)
{
    auto&ui=*reinterpret_cast<Ui*>(data);
    const int control=::GetDlgCtrlID(window);
    if(control==brightness || control==gamma || control==contrast || control==volume){
        // These value controls must not consume a form-scrolling gesture,
        // including when focused. Forward once, bypassing trackbar defaults.
        if(message==WM_MOUSEWHEEL)return ::SendMessageW(ui.window,message,wparam,lparam);
        if(message==WM_MOUSEHWHEEL)return 0;
    }
    if(message==WM_SETFOCUS)reveal_control(ui,window);
    if(message==WM_NCDESTROY)::RemoveWindowSubclass(window,focus_subclass,id);
    return ::DefSubclassProc(window,message,wparam,lparam);
}
HWND add(Ui&ui,int id,const wchar_t*class_name,const wchar_t*label,DWORD style,int x,int y,int w,int h)
{
    HWND child=::CreateWindowExW(std::wstring_view{class_name}==L"EDIT"?WS_EX_CLIENTEDGE:0,class_name,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,ui.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),::GetModuleHandleW(nullptr),nullptr);
    ui.controls.push_back({child,x,y,w,h});if(child && (style&WS_TABSTOP)!=0)::SetWindowSubclass(child,focus_subclass,1,reinterpret_cast<DWORD_PTR>(&ui));return child;
}
void label(Ui&ui,const wchar_t*s,int x,int y,int w,int h=22){static_cast<void>(add(ui,0,L"STATIC",s,0,x,y,w,h));}
void combo(Ui&ui,int id,int x,int y,int w){static_cast<void>(add(ui,id,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,x,y,w,180));}
void button(Ui&ui,int id,const wchar_t*s,int x,int y,int w,bool primary=false){static_cast<void>(add(ui,id,L"BUTTON",s,WS_TABSTOP|(primary?BS_DEFPUSHBUTTON:BS_PUSHBUTTON),x,y,w,30));}
void add_slider(Ui&ui,int id,const wchar_t*s,int y,int low,int high){label(ui,s,28,y+4,130);const auto h=add(ui,id,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_AUTOTICKS,160,y,440,30);::SendMessageW(h,TBM_SETRANGE,TRUE,MAKELPARAM(low,high));}
void update_image_labels(Ui&ui)
{
    const auto rounded=[](float value){auto s=std::to_wstring(value);s.resize(s.find(L'.')+3);return s;};
    text(ui,image_values,L"Яскр. "+rounded(static_cast<float>(slider(ui,brightness))/100.F)+L"   Гамма "+rounded(static_cast<float>(slider(ui,gamma))/100.F)+L"   Контраст "+rounded(static_cast<float>(slider(ui,contrast))/100.F)+L"   Звук "+std::to_wstring(slider(ui,volume))+L"%");
    text(ui,hud_values,selected(ui,hud_mode)==0?L"Авто: масштаб за оригінальним макетом HUD.":L"Масштаб HUD: "+std::to_wstring(slider(ui,hud_scale))+L"%");
    ::EnableWindow(get(ui,hud_scale),selected(ui,hud_mode)==1 && !ui.lifecycle.busy());
}
void update_modes(Ui&ui)
{
    const bool borderless=::SendMessageW(get(ui,mode),BM_GETCHECK,0,0)!=BST_CHECKED;::EnableWindow(get(ui,resolution),!ui.lifecycle.busy());
    text(ui,mode_note,borderless?L"Повний екран: клієнт — робочий стіл; HL — обрана роздільність.":L"Віконний: обраний розмір області гри.");
}
void fill_resolutions(Ui&ui)
{
    ::SendMessageW(get(ui,resolution),CB_RESETCONTENT,0,0);ui.resolutions.clear();
    const int index=selected(ui,display);const auto&d=ui.displays[static_cast<std::size_t>(std::max(0,index))];
    const std::array candidates{std::pair{640,480},std::pair{800,600},std::pair{1024,768},std::pair{1280,720},std::pair{1280,800},std::pair{1600,900},std::pair{1920,1080},std::pair{2560,1440},std::pair{3840,2160},std::pair{ui.configuration.video.width,ui.configuration.video.height}};
    for(const auto [w,h]:candidates)if(w<=d.desktop_width && h<=d.desktop_height && std::find(ui.resolutions.begin(),ui.resolutions.end(),std::pair{w,h})==ui.resolutions.end())ui.resolutions.emplace_back(w,h);
    if(ui.resolutions.empty())ui.resolutions.emplace_back(std::max(640,d.desktop_width),std::max(480,d.desktop_height));
    std::sort(ui.resolutions.begin(),ui.resolutions.end());int current=0;
    for(std::size_t i=0;i<ui.resolutions.size();++i){const auto [w,h]=ui.resolutions[i];choice(ui,resolution,std::to_wstring(w)+L" × "+std::to_wstring(h));if(w==ui.configuration.video.width && h==ui.configuration.video.height)current=static_cast<int>(i);}select(ui,resolution,current);update_modes(ui);
}
bool refresh_displays(Ui&ui)
{
    try{
        const auto previous=static_cast<std::size_t>(std::max(0,selected(ui,display)));
        const auto previous_id=previous<ui.displays.size()?ui.displays[previous].id:0U;
        auto actual=platform::enumerate_video_displays();if(actual.empty()){text(ui,status_label,L"Монітор недоступний. Підключення відкладено.");return false;}
        auto next=std::find_if(actual.begin(),actual.end(),[previous_id](const auto&d){return d.id==previous_id;});
        const bool missing=next==actual.end();const auto index=missing?0U:static_cast<std::size_t>(next-actual.begin());
        ui.displays=std::move(actual);::SendMessageW(get(ui,display),CB_RESETCONTENT,0,0);
        for(const auto&d:ui.displays)choice(ui,display,native::external_widen(d.name)+L" — "+std::to_wstring(d.desktop_width)+L" × "+std::to_wstring(d.desktop_height));
        if(missing&&!ui.recovery_video_baseline)ui.recovery_video_baseline=ui.configuration.video;
        ui.configuration.video.display_index=index;select(ui,display,static_cast<int>(index));
        if(missing){ui.configuration.video.mode=core::VideoWindowMode::windowed;ui.configuration.video.width=std::max(640,ui.displays[index].desktop_width*4/5);ui.configuration.video.height=std::max(480,ui.displays[index].desktop_height*4/5);::SendMessageW(get(ui,mode),BM_SETCHECK,BST_CHECKED,0);text(ui,status_label,L"Монітор змінився. Відновлено безпечний віконний режим.");}
        fill_resolutions(ui);return true;
    }catch(...){text(ui,status_label,L"Не вдалося оновити список моніторів.");return false;}
}
bool collect(Ui&ui,bool show_error)
{
    const int gpu=selected(ui,gpu_choice);if(gpu<0 || gpu>2)return false;
    ui.gpu_preference=static_cast<hlclient::launcher::GpuPreference>(gpu);
    ui.sky_policy=::SendMessageW(get(ui,background_sky),BM_GETCHECK,0,0)==BST_CHECKED?
        native::BackgroundSkyPolicy::depth:native::BackgroundSkyPolicy::original;
    auto c=ui.configuration;c.endpoint=native::external_utf8(read_control(ui,server));c.name=native::external_utf8(read_control(ui,name));c.asset_root=native::external_utf8(read_control(ui,assets));c.steam_api_runtime=native::external_utf8(read_control(ui,runtime));
    c.video.display_index=static_cast<std::size_t>(std::max(0,selected(ui,display)));c.video.mode=::SendMessageW(get(ui,mode),BM_GETCHECK,0,0)==BST_CHECKED?core::VideoWindowMode::windowed:core::VideoWindowMode::borderless;
    const auto i=static_cast<std::size_t>(std::max(0,selected(ui,resolution)));if(i<ui.resolutions.size()){c.video.width=ui.resolutions[i].first;c.video.height=ui.resolutions[i].second;}
    c.video.vsync=::SendMessageW(get(ui,vsync),BM_GETCHECK,0,0)==BST_CHECKED;
    c.show_fps=::SendMessageW(get(ui,show_fps),BM_GETCHECK,0,0)==BST_CHECKED;
    constexpr std::array<unsigned,4> samples{0,2,4,8};
    const auto aa=selected(ui,msaa);if(aa<0 || aa>=static_cast<int>(samples.size()))return false;
    c.video.msaa_samples=samples[static_cast<std::size_t>(aa)];
    c.hud_scale={selected(ui,hud_mode)==0,static_cast<float>(slider(ui,hud_scale))/100.F};
    c.movement_diagnostics=::SendMessageW(get(ui,movement_diagnostics),BM_GETCHECK,0,0)==BST_CHECKED;
    c.image={static_cast<float>(slider(ui,brightness))/100.F,static_cast<float>(slider(ui,gamma))/100.F,static_cast<float>(slider(ui,contrast))/100.F};c.volume=static_cast<unsigned>(slider(ui,volume));
    const auto error=core::validate_external_launch_configuration(c);if(!error.empty()){if(show_error){const auto message=error=="invalid_player_name"?L"Ім’я: 1–31 латинський символ, цифри або пробіли. Символи \\, ; та лапки не підтримуються чинним протоколом.":error=="invalid_endpoint"?L"Введіть IPv4:порт, наприклад 192.168.0.104:27016. Без порту — 27015. Додаткові команди заборонені.":L"Перевірте локальні абсолютні шляхи та налаштування відео.";::MessageBoxW(ui.window,message,L"Некоректні налаштування",MB_OK|MB_ICONWARNING);}return false;}
    c.endpoint=*core::normalize_external_endpoint(c.endpoint);ui.configuration=std::move(c);return true;
}
core::ExternalLaunchConfiguration configuration_for_run(core::ExternalLaunchConfiguration saved,bool system_diagnostics_enabled)
{
    // The system checkbox is deliberately not a persisted game setting. The
    // existing F8/game recorder accompanies this launch only.
    if(system_diagnostics_enabled)saved.movement_diagnostics=true;
    return saved;
}
void fill(Ui&ui)
{
    ::SendMessageW(get(ui,background_sky),BM_SETCHECK,ui.sky_policy==native::BackgroundSkyPolicy::depth?BST_CHECKED:BST_UNCHECKED,0);
    for(const int id:{display,msaa,hud_mode})::SendMessageW(get(ui,id),CB_RESETCONTENT,0,0);
    text(ui,server,native::external_widen(ui.configuration.endpoint));text(ui,name,native::external_widen(ui.configuration.name));text(ui,assets,native::external_widen(ui.configuration.asset_root));text(ui,runtime,native::external_widen(ui.configuration.steam_api_runtime));text(ui,client_label,L"Клієнт: "+ui.executable.wstring());
    for(const auto&d:ui.displays)choice(ui,display,native::external_widen(d.name)+L" — "+std::to_wstring(d.desktop_width)+L" × "+std::to_wstring(d.desktop_height));
    if(ui.configuration.video.display_index>=ui.displays.size()){if(!ui.recovery_video_baseline)ui.recovery_video_baseline=ui.configuration.video;ui.configuration.video.display_index=0;ui.configuration.video.mode=core::VideoWindowMode::windowed;ui.settings_status="display_missing_recovered";}
    if(ui.configuration.video.mode==core::VideoWindowMode::exclusive){if(!ui.recovery_video_baseline)ui.recovery_video_baseline=ui.configuration.video;ui.configuration.video.mode=core::VideoWindowMode::windowed;ui.settings_status="unsupported_exclusive_recovered_to_windowed";}
    select(ui,display,static_cast<int>(ui.configuration.video.display_index));::SendMessageW(get(ui,mode),BM_SETCHECK,ui.configuration.video.mode==core::VideoWindowMode::borderless?BST_UNCHECKED:BST_CHECKED,0);fill_resolutions(ui);
    ::SendMessageW(get(ui,vsync),BM_SETCHECK,ui.configuration.video.vsync?BST_CHECKED:BST_UNCHECKED,0);
    ::SendMessageW(get(ui,show_fps),BM_SETCHECK,ui.configuration.show_fps?BST_CHECKED:BST_UNCHECKED,0);
    choice(ui,msaa,L"Вимкнено");choice(ui,msaa,L"MSAA 2×");choice(ui,msaa,L"MSAA 4×");choice(ui,msaa,L"MSAA 8×");
    select(ui,msaa,ui.configuration.video.msaa_samples==0?0:ui.configuration.video.msaa_samples==2?1:ui.configuration.video.msaa_samples==4?2:3);
    choice(ui,hud_mode,L"Авто");choice(ui,hud_mode,L"Власний масштаб");select(ui,hud_mode,ui.configuration.hud_scale.automatic?0:1);
    slide(ui,hud_scale,static_cast<int>(std::lround(ui.configuration.hud_scale.scale*100)));
    ::SendMessageW(get(ui,movement_diagnostics),BM_SETCHECK,ui.configuration.movement_diagnostics?BST_CHECKED:BST_UNCHECKED,0);
    slide(ui,brightness,static_cast<int>(ui.configuration.image.brightness*100));slide(ui,gamma,static_cast<int>(ui.configuration.image.gamma*100));slide(ui,contrast,static_cast<int>(ui.configuration.image.contrast*100));slide(ui,volume,static_cast<int>(ui.configuration.volume));update_image_labels(ui);
    text(ui,status_label,ui.settings_status=="loaded" || ui.settings_status=="defaults" || ui.settings_status.empty()?L"Готовий. Підключення почнеться після натискання кнопки.":L"Відновлено безпечні налаштування: "+native::external_widen(ui.settings_status));
    // Compare future edits with what this native form actually displayed.
    // Slider rounding must not silently replace a precise console value.
    const auto saved=ui.configuration;
    if(collect(ui,false))ui.settings_baseline=ui.configuration;
    // Recovery is an unsaved edit relative to the profile on disk, not the
    // baseline of an unchanged form. Otherwise a merge can restore fullscreen.
    if(ui.recovery_video_baseline)ui.settings_baseline.video=*ui.recovery_video_baseline;
    ui.configuration=saved;
}
void create_controls(Ui&ui)
{
    static_cast<void>(add(ui,0,L"BUTTON",L"Підключення",BS_GROUPBOX,16,10,610,105));
    label(ui,L"Сервер",28,38,105);static_cast<void>(add(ui,server,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,135,34,460,25));::SendMessageW(get(ui,server),EM_SETLIMITTEXT,128,0);
    label(ui,L"Ім’я гравця",28,75,105);static_cast<void>(add(ui,name,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,135,71,460,25));::SendMessageW(get(ui,name),EM_SETLIMITTEXT,31,0);
    static_cast<void>(add(ui,0,L"BUTTON",L"Відео",BS_GROUPBOX,16,124,610,142));
    label(ui,L"Монітор",28,152,105);combo(ui,display,135,147,460);
    label(ui,L"Роздільність",28,187,105);combo(ui,resolution,135,182,170);
    static_cast<void>(add(ui,mode,L"BUTTON",L"Віконний режим",WS_TABSTOP|BS_AUTOCHECKBOX,316,182,279,24));
    static_cast<void>(add(ui,vsync,L"BUTTON",L"VSync",WS_TABSTOP|BS_AUTOCHECKBOX,28,218,100,24));
    static_cast<void>(add(ui,show_fps,L"BUTTON",L"Показувати FPS",WS_TABSTOP|BS_AUTOCHECKBOX,135,218,165,24));
    static_cast<void>(add(ui,mode_note,L"STATIC",L"",0,310,219,285,37));
    static_cast<void>(add(ui,0,L"BUTTON",L"Зображення й звук",BS_GROUPBOX,16,278,610,201));
    add_slider(ui,brightness,L"Яскравість",302,-50,50);add_slider(ui,gamma,L"Гамма",335,25,400);add_slider(ui,contrast,L"Контраст",368,0,200);add_slider(ui,volume,L"Гучність",401,0,100);
    static_cast<void>(add(ui,image_values,L"STATIC",L"",0,28,439,430,24));button(ui,reset_image,L"Скинути",487,435,108);
    static_cast<void>(add(ui,0,L"BUTTON",L"Локальні ресурси (додатково)",BS_GROUPBOX,16,490,610,150));
    label(ui,L"Half-Life",28,520,84);static_cast<void>(add(ui,assets,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,115,515,388,25));button(ui,choose_assets,L"Папка…",512,513,83);
    label(ui,L"Steam DLL",28,552,84);static_cast<void>(add(ui,runtime,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,115,550,388,25));button(ui,choose_runtime,L"Файл…",512,547,83);
    static_cast<void>(add(ui,dependency_label,L"STATIC",L"Steam має бути запущений. Ресурси й офіційний runtime перевіряються перед входом.",0,28,585,565,21));
    static_cast<void>(add(ui,client_label,L"STATIC",L"",SS_PATHELLIPSIS,28,611,565,20));
    label(ui,L"Налаштування застосовуються до наступного запуску гри.",22,651,605);
    button(ui,connect_button,L"Підключитися",20,681,157,true);button(ui,cancel_button,L"Від’єднатися",187,681,174);button(ui,report_button,L"Відкрити звіт",371,681,133);button(ui,save_button,L"Зберегти",514,681,104);
    button(ui,open_client_button,L"Відкрити клієнт",20,717,202);
    button(ui,original_hl_button,L"Оригінальний Half-Life → сервер",232,717,386);
    static_cast<void>(add(ui,status_label,L"STATIC",L"",0,22,763,595,60));
    // Insert the independent HUD/AA controls before image settings. Native
    // coordinates below are DPI-scaled once; they are never in-game HUD units.
    for(auto& p:ui.controls)if(p.y>=278)p.y+=135;
    const auto hud_group=add(ui,0,L"BUTTON",L"HUD і згладжування",BS_GROUPBOX,16,278,610,123);
    label(ui,L"Масштаб HUD",28,304,110);combo(ui,hud_mode,145,299,175);
    const auto hud_slider=add(ui,hud_scale,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_AUTOTICKS,325,299,158,30);
    ::SendMessageW(hud_slider,TBM_SETRANGE,TRUE,MAKELPARAM(50,200));button(ui,reset_hud,L"Скинути",487,299,108);
    static_cast<void>(add(ui,hud_values,L"STATIC",L"",0,28,337,565,22));
    label(ui,L"Згладжування",28,368,110);combo(ui,msaa,145,363,175);
    label(ui,L"Фактичне MSAA та fallback — у статусі й звіті.",330,367,265,28);
    for(auto& p:ui.controls)if(p.y>=413)p.y+=120;
    for(auto& p:ui.controls)if(p.y>=532)p.y+=52;
    static_cast<void>(add(ui,0,L"BUTTON",L"Діагностика",BS_GROUPBOX,16,412,610,162));
    static_cast<void>(add(ui,movement_diagnostics,L"BUTTON",L"Комплексна діагностика гри",WS_TABSTOP|BS_AUTOCHECKBOX,28,432,565,24));
    static_cast<void>(add(ui,movement_hotkey,L"STATIC",L"Запис усієї сесії. F8 — позначка. Звіт після закриття гри.",0,28,459,565,21));
    static_cast<void>(add(ui,system_diagnostics,L"BUTTON",L"Системна діагностика CPU/GPU",WS_TABSTOP|BS_AUTOCHECKBOX,28,484,565,24));
    button(ui,startup_folder,L"Папка логів запуску",28,518,266);
    button(ui,startup_export,L"Зберегти ZIP логів…",305,518,288);
    label(ui,native::shared_test_storage_root()?L"Мережевий тест: усі журнали у test-data поруч із програмою.":L"Короткий журнал запуску зберігається завжди, навіть без діагностики.",28,553,565,18);
    if(native::shared_test_storage_root())::SendMessageW(get(ui,system_diagnostics),BM_SETCHECK,BST_CHECKED,0);
    // Separate launcher-only choice: never extend the client's strict v3 JSON.
    for(auto&p:ui.controls)if(p.y>=278)p.y+=92;
    for(auto&p:ui.controls)if(p.y==124)p.h+=92;
    label(ui,L"Відеокарта",28,258,105);combo(ui,gpu_choice,135,253,365);
    button(ui,gpu_settings,L"Windows…",510,250,85);
    const auto adapters=ui.offline_check?hlclient::launcher::GpuAdapterLabels{}:hlclient::launcher::read_gpu_adapter_labels();
    choice(ui,gpu_choice,L"Авто — вибирає Windows");
    choice(ui,gpu_choice,adapters.power_saving.empty()?L"Енергозбереження":L"Енергозбереження — "+adapters.power_saving);
    choice(ui,gpu_choice,adapters.high_performance.empty()?L"Висока продуктивність":L"Висока продуктивність — "+adapters.high_performance);
    select(ui,gpu_choice,static_cast<int>(ui.gpu_preference));
    static_cast<void>(add(ui,gpu_actual,L"STATIC",L"GPU застосовується до наступного запуску клієнта або HL. Фактична GPU клієнта з’явиться після запуску.",0,28,290,565,54));
    for(auto&p:ui.controls)if(p.control==get(ui,status_label))p.y+=42;
    label(ui,L"Оригінальний HL: сервер, ім’я, GPU, роздільність і режим вікна. HUD/MSAA та діагностика вище — для нашого клієнта.",22,1152,595,42);
    // One native opt-in control; no shell scripts or process-wide environment edits.
    for(auto& p:ui.controls){if(p.control==hud_group)p.h+=56;else if(p.y>=504)p.y+=56;}
    static_cast<void>(add(ui,background_sky,L"BUTTON",L"Новий режим неба (тестовий)",WS_TABSTOP|BS_AUTOCHECKBOX,28,489,565,24));
    label(ui,L"Знято — старий (original); увімкнено — новий (depth).",28,516,565,22);
    ::EnableWindow(get(ui,cancel_button),FALSE);::EnableWindow(get(ui,report_button),FALSE);layout(ui);fill(ui);
}
void browse(Ui&ui,bool folder)
{
    IFileOpenDialog*dialog{};if(FAILED(::CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
    DWORD flags{};static_cast<void>(dialog->GetOptions(&flags));static_cast<void>(dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST)));
    if(!folder){const COMDLG_FILTERSPEC filter{L"Офіційний Steam runtime",L"steam_api.dll"};static_cast<void>(dialog->SetFileTypes(1,&filter));}
    if(SUCCEEDED(dialog->Show(ui.window))){IShellItem*item{};if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){text(ui,folder?assets:runtime,path);if(folder)text(ui,runtime,(std::filesystem::path{path}/L"steam_api.dll").wstring());::CoTaskMemFree(path);}item->Release();}}dialog->Release();
}
void open_report(Ui& ui) {
    const auto run=std::filesystem::is_directory(ui.last_report)?ui.last_report:ui.last_report.parent_path();
    const auto script=ui.executable.parent_path().parent_path().parent_path().parent_path()/L"scripts"/L"analyze_movement_session.ps1";
    const std::filesystem::path powershell{L"C:\\Program Files\\PowerShell\\7\\pwsh.exe"};
    if(std::filesystem::is_regular_file(run/L"external-movement.json") &&
       std::filesystem::is_regular_file(script) && std::filesystem::is_regular_file(powershell)) {
        // Fixed project-owned script. Paths are native quoted arguments, never
        // interpolated PowerShell expressions or server-supplied commands.
        const auto arguments=L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script.wstring()+
            L"\" -RunDirectory \""+run.wstring()+L"\" -PublishSummary -Open";
        const auto result=::ShellExecuteW(ui.window,L"open",powershell.c_str(),arguments.c_str(),nullptr,SW_HIDE);
        if(reinterpret_cast<INT_PTR>(result)>32)return;
    }
    static_cast<void>(::ShellExecuteW(ui.window,L"open",ui.last_report.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
}
void open_startup_logs(Ui&ui,bool archive)
{
    if(!ui.startup)return;
    if(!archive){const auto folder=ui.startup->directory();if(!folder.empty())
        static_cast<void>(::ShellExecuteW(ui.window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL));return;}
    ui.startup->record("export_requested");
    const auto zip=ui.startup->export_zip();
    if(zip.empty()){::MessageBoxW(ui.window,L"Не вдалося зберегти ZIP. Відкрийте папку логів запуску й скопіюйте її вміст.",L"Логи запуску",MB_OK|MB_ICONERROR);return;}
    const auto arguments=L"/select,\""+zip.wstring()+L"\"";
    static_cast<void>(::ShellExecuteW(ui.window,L"open",L"explorer.exe",arguments.c_str(),nullptr,SW_SHOWNORMAL));
    text(ui,status_label,L"ZIP логів готовий: "+zip.wstring());
}
std::string_view gpu_token(hlclient::launcher::GpuPreference value) noexcept
{
    using G=hlclient::launcher::GpuPreference;
    switch(value){case G::automatic:return "automatic";case G::power_saving:return "power_saving";case G::high_performance:return "high_performance";}
    return "invalid";
}
std::optional<hlclient::launcher::GpuPreference> decode_gpu_choice(std::string_view value) noexcept
{
    if(value.ends_with("\r\n"))value.remove_suffix(2);else if(value.ends_with('\n'))value.remove_suffix(1);
    using G=hlclient::launcher::GpuPreference;
    if(value=="automatic")return G::automatic;
    if(value=="power_saving")return G::power_saving;
    if(value=="high_performance")return G::high_performance;
    return {};
}
std::string_view sky_token(native::BackgroundSkyPolicy policy) noexcept
{
    return policy==native::BackgroundSkyPolicy::depth?"depth":"original";
}
std::optional<native::BackgroundSkyPolicy> decode_sky_choice(std::string_view value) noexcept
{
    while(!value.empty() && (value.back()=='\r' || value.back()=='\n'))value.remove_suffix(1);
    if(value=="original")return native::BackgroundSkyPolicy::original;
    if(value=="depth")return native::BackgroundSkyPolicy::depth;
    return {};
}
std::string observed_client_gpu(std::string_view output)
{
    constexpr std::string_view prefix="[opengl-startup] context=1 renderer=";
    while(!output.empty()){
        const auto end=output.find('\n');auto line=output.substr(0,std::min(end,output.size()));
        if(line.starts_with(prefix)){
            line.remove_prefix(prefix.size());while(!line.empty() && (line.back()=='\r' || line.back()==' '))line.remove_suffix(1);
            if(line.empty() || line.size()>256 || std::any_of(line.begin(),line.end(),[](unsigned char c){return c<32U || c==127U;}))return {};
            return std::string{line};
        }
        if(end==std::string_view::npos)break;output.remove_prefix(end+1);
    }
    return {};
}
std::wstring stage_label(const Progress&p)
{
    if(p.original_hl && p.started && !p.finished)return p.cancel_timed_out?L"Half-Life не закрився. Закрийте його через меню гри.":p.stage=="disconnecting"?L"Закриття оригінального Half-Life…":L"Оригінальний Half-Life запущено. Підключення виконує сама гра.";
    if(p.finished){
        if(p.original_hl && p.success)return L"Оригінальний Half-Life закрито. Можна запустити знову.";
        if(p.reason=="gpu_preference_path_unsupported")return L"Шлях EXE надто довгий або не підтримує безпечний запуск із GPU-пріоритетом. Використайте коротку звичайну папку чи UNC-шлях.";
        if(p.reason=="run_output_path_too_long")return L"Шлях папки тесту надто довгий для журналів. Перемістіть увесь пакет у коротку папку, наприклад D:\\HL-Sky-Test.";
        if(p.reason=="gpu_preference_failed")return L"Не вдалося застосувати вибір GPU. Відкрийте «Windows…» для налаштувань графіки; деталі — у логах запуску.";
        if(p.reason=="startup_cancelled" || p.reason=="cancelled_before_gameplay")return L"Підключення скасовано. Можна спробувати знову.";
        if(p.success)return L"Завершено. Можна підключитися знову.";
        if(p.reason=="active_research_session_conflict")return L"Цей hlclient.exe ізольовано активним дослідницьким тестом. Закрийте його штатно й повторіть підключення.";
        if(p.reason=="official_steam_runtime_unavailable_or_untrusted")return L"Не знайдено перевірений офіційний Steam runtime (x86). Оберіть steam_api.dll зі своєї ліцензованої Half-Life.";
        if(p.reason=="server_full")return L"Сервер заповнений. Звільніть місце на сервері й повторіть підключення. Деталі — у звіті.";
        if(p.reason=="server_password_required_or_rejected")return L"Сервер вимагає пароль або відхилив його. Введення пароля цим лаунчером поки не підтримується. Деталі — у звіті.";
        if(p.reason=="server_protocol_rejected")return L"Сервер відхилив версію протоколу клієнта. Деталі — у звіті.";
        if(p.reason=="server_authentication_rejected")return L"Сервер відхилив автентифікацію клієнта. Перевірте Steam і правила доступу сервера. Деталі — у звіті.";
        if(p.reason=="connection_rejected")return L"Сервер відхилив підключення. Деталі — у звіті.";
        if(p.reason=="challenge_protocol_error")return L"Початкову відповідь сервера (challenge) відхилено перевіркою протоколу. Причина й поле — у звіті external-diagnostics.json.";
        if(p.reason=="challenge_network_error")return L"Помилка мережі під час початкового обміну із сервером (challenge). Деталі — у звіті.";
        if(p.reason=="challenge_configuration_error")return L"Некоректне налаштування початкового обміну із сервером (challenge). Деталі — у звіті.";
        if(p.reason=="protocol_error")return L"Вхід перервано перевіркою протоколу сервера. Це не помилка від’єднання. Деталі — у звіті.";
        return L"Помилка: "+native::external_widen(p.reason)+L". Деталі — у звіті.";}
    if(p.stage=="error")return L"Помилка підключення: "+native::external_widen(p.reason)+L". Завершення й збереження звіту…";
    if(p.stage=="idle")return L"Клієнт відкрито без підключення. Команди connect/retry доступні в ігровій консолі.";
    if(p.stage=="recoverable_error")return L"Підключення не вдалося. Причина — в консолі клієнта; можна підключитися знову без його закриття.";
    if(p.stage=="in_game")return L"У грі. Карту й ігровий стан підтверджено клієнтом.";
    if(p.stage=="authentication")return L"Автентифікація Steam…";
    if(p.stage=="signon")return L"Вхід на сервер (sign-on)…";
    if(p.stage=="preparing_map")return L"Підготовка карти й ресурсів…";
    if(p.stage=="disconnecting")return p.cancel_timed_out?L"Клієнт не відповідає. Натисніть «Від’єднатися» для явного завершення.":L"Від’єднання й збереження звіту…";
    if(p.stage=="connecting")return L"Підключення до сервера…";
    if(p.stage=="system_initializing")return L"Підготовка системної діагностики (до 8 с)…";
    if(p.stage=="system_finalizing")return L"Гру закрито. Збереження системної діагностики та ZIP…";
    return L"Перевірка локальних ресурсів і залежностей…";
}
struct SystemDiagnosticsHelper {
    HANDLE process{};
    std::string status{"not_requested"};
    ~SystemDiagnosticsHelper(){if(process)::CloseHandle(process);}
    SystemDiagnosticsHelper(const SystemDiagnosticsHelper&)=delete;
    SystemDiagnosticsHelper& operator=(const SystemDiagnosticsHelper&)=delete;
    SystemDiagnosticsHelper()=default;
    void start(const std::filesystem::path& launcher,const std::filesystem::path& run,
        const std::string& run_id)
    {
        const auto executable=launcher.parent_path()/L"HLClientSystemDiagnostics.exe";
        const DWORD attributes=::GetFileAttributesW(executable.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0){status="helper_binary_unavailable";return;}
        // Start before the game. The helper anchors the two clocks and waits
        // for our later process-manifest with exact PID and creation FILETIME.
        // No server address, command line, password, or auth token is passed.
        auto command=L"\""+executable.wstring()+L"\" --await-client --run-root \""+run.wstring()+
            L"\" --run-id "+native::external_widen(run_id);
        STARTUPINFOW startup{sizeof(STARTUPINFOW)};PROCESS_INFORMATION launched{};
        if(!::CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
            nullptr,executable.parent_path().c_str(),&startup,&launched)){
            status="helper_launch_error_"+std::to_string(::GetLastError());return;}
        process=launched.hProcess;::CloseHandle(launched.hThread);status="running";
    }
    bool wait_ready(const std::filesystem::path& run,const std::string& run_id)
    {
        if(!process)return false;
        const auto deadline=Clock::now()+std::chrono::seconds{8};
        do{
            if(const auto marker=native::read_external_text(run/L"system-helper-ready.marker",4096);
                marker && marker->find("hlclient.system-helper-ready.v1")!=std::string::npos &&
                marker->find(run_id)!=std::string::npos){status="ready";return true;}
            if(::WaitForSingleObject(process,0)==WAIT_OBJECT_0){status="helper_exited_before_ready";return false;}
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }while(Clock::now()<deadline);
        status="helper_ready_timeout";return false;
    }
    std::string finish()
    {
        if(!process)return status;
        constexpr DWORD kFinalizeLimitMs=120000;
        const DWORD wait=::WaitForSingleObject(process,kFinalizeLimitMs);
        if(wait!=WAIT_OBJECT_0){
            // Only the exact helper process created by this launcher is owned.
            // Its collectors must have already reached their own time bound.
            static_cast<void>(::TerminateProcess(process,126));
            static_cast<void>(::WaitForSingleObject(process,3000));
            return wait==WAIT_TIMEOUT?"helper_finalize_timeout":"helper_wait_failed";
        }
        DWORD code{};
        if(!::GetExitCodeProcess(process,&code))return "helper_exit_unavailable";
        return code==0?"helper_exited_0":"helper_exit_"+std::to_string(code);
    }
};
void run_child(Worker&worker,const core::ExternalLaunchConfiguration configuration,const std::filesystem::path client,const std::filesystem::path launcher,const std::filesystem::path storage,const bool open_client,const bool system_diagnostics_enabled,hlclient::launcher::StartupJournal*startup,const std::optional<hlclient::launcher::GpuPreference> gpu_request={},const native::BackgroundSkyPolicy sky_policy=native::BackgroundSkyPolicy::original)
{
    Progress p;p.system_diagnostics_requested=system_diagnostics_enabled;
    std::filesystem::path run;std::string run_id,client_sha,launcher_sha,config_sha,isolation="not_observed";std::optional<app::SessionReceiptSnapshot> receipt;std::optional<std::uint32_t> exit;std::uint32_t pid{};std::uint64_t creation{};bool forced{};
    SystemDiagnosticsHelper system_helper;
    if(system_diagnostics_enabled)system_helper.status="not_started";
    std::unique_ptr<native::ExternalClientProcess> child_owner;
    native::ExternalFailureMarkers failures;
    const auto observe_failures=[&](std::string_view bytes){const auto observed=native::external_failure_markers(bytes);
        if(failures.primary_reason.empty())failures.primary_reason=observed.primary_reason;
        if(failures.cleanup_reason.empty())failures.cleanup_reason=observed.cleanup_reason;};
    const auto finalize=[&](bool success,std::string reason){p.success=success;p.reason=std::move(reason);
        if(startup){if(exit)startup->client_exit(*exit);startup->record(success?"client_finished":"client_failed",p.reason);}
        bool report_published=false;
        if(!run.empty()){const auto snapshot=receipt?app::encode_session_receipt(*receipt):std::nullopt;
            const auto report=std::string{"{\n\"schema\":\"hlclient.external-run.v1\",\n\"mode\":\"external_server_client\",\n\"server_process_owned\":false,\n\"server_cleanup\":\"not_applicable\",\n\"owned_helpers\":0,\n\"run_id\":"}+core::external_json_string(run_id)+",\n\"target_endpoint\":"+core::external_json_string(configuration.endpoint)+",\n\"client_sha256\":"+core::external_json_string(client_sha)+",\n\"launcher_sha256\":"+core::external_json_string(launcher_sha)+",\n\"config_sha256\":"+core::external_json_string(config_sha)+",\n\"process_id\":"+std::to_string(pid)+",\n\"process_creation\":"+std::to_string(creation)+",\n\"research_isolation\":"+core::external_json_string(isolation)+",\n\"client_exit\":"+(exit?std::to_string(*exit):"null")+",\n\"terminal_reason\":"+core::external_json_string(p.reason)+",\n\"launcher_forced_own_child\":"+(forced?"true":"false")+",\n\"gameplay_observed\":"+(receipt && app::external_gameplay_receipt_ready(*receipt)?"true":"false")+",\n\"receipt_status\":"+core::external_json_string(receipt?"identity_bound":"not_observed")+",\n\"receipt\":"+(snapshot?core::external_json_string(*snapshot):"null")+",\n\"video_start_report\":\"external-video-start.json\",\n\"video_final_report\":\"external-video-final.json\",\n\"server_live_compatibility\":\"manual_confirmation_required\"\n}\n";
            // These leaves are immutable, process-owned outputs; a reference
            // is not a claim that a crashed child managed to publish them.
            auto report_with_helper=report;
            const std::string helper_count_field="\"owned_helpers\":0";
            if(const auto at=report_with_helper.find(helper_count_field);at!=std::string::npos)
                report_with_helper.replace(at,helper_count_field.size(),
                    "\"owned_helpers\":"+std::string{system_helper.process?"1":"0"});
            const auto with_diagnostics=report_with_helper.substr(0,report_with_helper.size()-2)+
                ",\n\"startup_mode\":"+core::external_json_string(open_client?"open_client":"connect")+
                ",\n\"requested_background_sky_policy\":"+core::external_json_string(sky_token(sky_policy))+
                ",\n\"application_result_is_gameplay_acceptance\":false"+
                ",\n\"primary_failure_reason\":"+core::external_json_string(failures.primary_reason.empty()?"none":failures.primary_reason)+
                ",\n\"cleanup_failure_reason\":"+core::external_json_string(failures.cleanup_reason.empty()?"none":failures.cleanup_reason)+
                ",\n\"diagnostics_report\":\"external-diagnostics.json\",\n\"client_status_log\":\"client-status.log\",\n\"movement_report\":\"external-movement.json\",\n\"movement_diagnostics_enabled\":"+(configuration.movement_diagnostics?"true":"false")+
                ",\n\"system_diagnostics_requested\":"+(system_diagnostics_enabled?"true":"false")+
                ",\n\"system_helper_start_status\":"+core::external_json_string(system_helper.status)+
                ",\n\"system_archive_expected_path\":"+(system_diagnostics_enabled?"\"system-diagnostics.zip\"":"null")+
                ",\n\"system_archive_status_at_report_publication\":"+
                    core::external_json_string(system_diagnostics_enabled?"pending_helper":"not_requested")+
                ",\n\"system_completion_report_expected_path\":"+
                    (system_diagnostics_enabled?"\"system-diagnostics-launcher-result.json\"":"null")+
                ",\n\"movement_incident_hotkey\":\"F8\"\n}\n";
            p.report=run/L"external-run-report.json";report_published=native::write_external_text_atomic(p.report,with_diagnostics,false);
            if(!report_published){p.success=false;p.reason="final_report_publication_failed";p.report=run;}}
        if(system_diagnostics_enabled && !run.empty()){
            p.stage="system_finalizing";p.system_run_folder=run;worker.publish(p);
            bool marker_published=false;
            if(system_helper.process){
                const auto marker=std::string{"{\"schema\":\"hlclient.system-launcher-finished.v1\",\"run_id\":"}+
                    core::external_json_string(run_id)+",\"pid\":"+std::to_string(pid)+
                    ",\"creation_filetime\":"+std::to_string(creation)+
                    ",\"report_publication\":"+(report_published?"true":"false")+"}\n";
                marker_published=native::write_external_text_atomic(run/L"launcher-finished.marker",marker,false);
            }
            p.system_helper_status=system_helper.process?system_helper.finish():system_helper.status;
            if(system_helper.process && !marker_published)p.system_helper_status="marker_publication_failed_"+p.system_helper_status;
            std::error_code archive_error;
            const auto archive=run/L"system-diagnostics.zip";
            const bool archive_file_present=std::filesystem::is_regular_file(archive,archive_error);
            const auto archive_bytes=archive_file_present?std::filesystem::file_size(archive,archive_error):0U;
            // The helper writes a temporary ZIP and only publishes the final
            // path after its central directory is complete. A leftover file
            // after a nonzero helper exit is never presented as ready.
            p.system_archive_ready=p.system_helper_status=="helper_exited_0" &&
                archive_file_present && !archive_error && archive_bytes>=22U;
            if(p.system_helper_status=="helper_exited_0" && !p.system_archive_ready)p.system_helper_status="helper_exit_without_archive";
            const auto completion=std::string{"{\"schema\":\"hlclient.system-launcher-result.v1\",\"run_id\":"}+
                core::external_json_string(run_id)+",\"helper_status\":"+
                core::external_json_string(p.system_helper_status)+",\"archive_file_present\":"+
                (archive_file_present?"true":"false")+",\"archive_ready\":"+
                (p.system_archive_ready?"true":"false")+",\"archive_path\":"+
                (p.system_archive_ready?"\"system-diagnostics.zip\"":"null")+
                ",\"report_publication\":"+(report_published?"true":"false")+
                ",\"launcher_marker_publication\":"+(marker_published?"true":"false")+"}\n";
            if(!native::write_external_text_atomic(run/L"system-diagnostics-launcher-result.json",completion,false))
                p.system_helper_status+="_completion_report_write_failed";
        }
        p.finished=true;p.stage=p.success?"completed":"error";worker.publish(p);};
    try{
        if(startup){startup->record("client_launch_requested",open_client?"open_client":"connect");startup->number("game_diagnostics_enabled",configuration.movement_diagnostics?1U:0U);startup->number("system_diagnostics_enabled",system_diagnostics_enabled?1U:0U);startup->number("requested_msaa",configuration.video.msaa_samples);startup->number("requested_width",static_cast<unsigned>(configuration.video.width));startup->number("requested_height",static_cast<unsigned>(configuration.video.height));}
        run_id=native::new_external_run_id();if(run_id.empty()){finalize(false,"run_identity_failed");return;}
        if(startup)startup->record("background_sky_policy_requested",sky_token(sky_policy));
        // The accepted secure-output lock uses a 65-character filename. Reject
        // before creating an unusable run; do not silently redirect diagnostics.
        if((storage/L"runs"/native::external_widen(run_id)).native().size()+66U>=260U){finalize(false,"run_output_path_too_long");return;}
        run=storage/L"runs"/native::external_widen(run_id);std::filesystem::create_directories(run);p.report=run;worker.publish(p);
        auto pinned=native::open_secure_output_directory(run);if(!pinned){finalize(false,"run_directory_unavailable");return;}
        if(system_diagnostics_enabled){
            p.stage="system_initializing";worker.publish(p);
            system_helper.start(launcher,run,run_id);
            static_cast<void>(system_helper.wait_ready(run,run_id));
            p.system_helper_status=system_helper.status;p.stage="checking_resources";worker.publish(p);
        }
        const auto config=core::encode_external_launch_configuration(configuration);if(!config){finalize(false,"configuration_invalid");return;}
        config_sha=hlclient::hash::sha256_hex(*hlclient::hash::sha256(std::as_bytes(std::span{config->data(),config->size()})));
        if(!native::write_external_text_atomic(run/L"launch-config.json",*config,false)){finalize(false,"configuration_publication_failed");return;}
        const auto binary=native::observe_windows_binary_identity(client,native::kMaximumObservedExecutableBytes,{native::AuthenticodePolicy::not_required_for_downloaded_project_owned_binary,false});
        if(!binary){finalize(false,"client_binary_"+std::string{native::to_string(binary.code)});return;}client_sha=hlclient::hash::sha256_hex(binary.identity->sha256);
        const auto launcher_binary=native::observe_windows_binary_identity(launcher,native::kMaximumObservedExecutableBytes,{native::AuthenticodePolicy::not_required_for_downloaded_project_owned_binary,false});if(launcher_binary)launcher_sha=hlclient::hash::sha256_hex(launcher_binary.identity->sha256);
        if(!open_client) {
            const auto conflict=native::observe_research_isolation_conflict(client);isolation=conflict.state==native::ResearchIsolationConflict::present?"present":conflict.state==native::ResearchIsolationConflict::absent?"absent":"query_unavailable";
            if(conflict.state==native::ResearchIsolationConflict::present){finalize(false,"active_research_session_conflict");return;}
            const auto steam=native::observe_official_steam_runtime(native::external_widen(configuration.steam_api_runtime));if(!steam){finalize(false,"official_steam_runtime_unavailable_or_untrusted");return;}
        }
        const auto manifest=std::string{"{\"schema\":\"hlclient.external-launch-identity.v1\",\"run_id\":"}+core::external_json_string(run_id)+",\"client_sha256\":"+core::external_json_string(client_sha)+",\"launcher_sha256\":"+core::external_json_string(launcher_sha)+",\"config_sha256\":"+core::external_json_string(config_sha)+",\"server_process_owned\":false}\n";
        if(!native::write_external_text_atomic(run/L"launch-manifest.json",manifest,false)){finalize(false,"launch_manifest_publication_failed");return;}
        if(worker.cancel){finalize(false,"startup_cancelled");return;}
        if(gpu_request){
            // Match the exact ordinary spelling used by the process launcher,
            // while leaving the original canonical binary identity untouched.
            const auto gpu_path=native::ordinary_executable_launch_path(binary.identity->canonical_path);
            if(!gpu_path){finalize(false,"gpu_preference_path_unsupported");return;}
            const auto gpu=hlclient::launcher::set_gpu_preference(*gpu_path,*gpu_request);
            if(startup){startup->record("gpu_preference_requested",gpu_token(*gpu_request));startup->record(gpu.succeeded()?"gpu_preference_applied":"gpu_preference_failed",{},gpu.win32_error);}
            if(!gpu.succeeded()){finalize(false,"gpu_preference_failed");return;}
        }
        if(worker.cancel){finalize(false,"startup_cancelled");return;}
        child_owner=std::make_unique<native::ExternalClientProcess>();auto&child=*child_owner;
        std::vector<std::wstring> arguments{L"--external-config",(run/L"launch-config.json").wstring(),L"--session-receipt-root",run.wstring(),L"--session-run-id",native::external_widen(run_id),L"--session-role",L"A"};
        if(open_client)arguments.emplace_back(L"--open-client");
        if(!child.launch(*binary.identity,arguments,true,sky_policy)){if(startup)startup->record("child_create_failed",{},child.launch_error());finalize(false,"child_create_failed_"+std::to_string(child.launch_error()));return;}
        if(startup){startup->record("child_created");startup->number("client_pid",child.process_id());}
        pid=child.process_id();creation=child.process_creation();p.started=true;p.stage=open_client?"idle":"connecting";worker.publish(p);
        const auto process_manifest=std::string{"{\"schema\":\"hlclient.external-process-identity.v1\",\"run_id\":"}+core::external_json_string(run_id)+",\"process_id\":"+std::to_string(pid)+",\"process_creation\":"+std::to_string(creation)+",\"client_sha256\":"+core::external_json_string(client_sha)+"}\n";
        const bool process_recorded=native::write_external_text_atomic(run/L"process-manifest.json",process_manifest,false);
        if(!process_recorded)worker.cancel=true;
        if(system_diagnostics_enabled && !process_recorded)system_helper.status="process_manifest_unavailable";
        const app::SessionReceiptIdentity expected{run_id,app::SessionReceiptRole::A,pid,creation};
        std::optional<Clock::time_point> cancelled;
        auto next_video_observation=Clock::now();
        auto next_startup_flush=Clock::now();std::string captured_startup;
        for(;;){const auto state=child.poll();if(worker.cancel && !cancelled){cancelled=Clock::now();child.request_cancel();p.stage="disconnecting";}
            if(worker.force && !forced){forced=child.terminate_owned_child(125);}
            if(cancelled && Clock::now()-*cancelled>std::chrono::seconds{8})p.cancel_timed_out=true;
            const auto log=child.log_snapshot();observe_failures(log.bytes);
            if(p.actual_gpu.empty())p.actual_gpu=observed_client_gpu(log.bytes);
            // A shared test must leave useful error evidence while the child
            // still runs, including when the testing PC loses power later.
            if(startup && Clock::now()>=next_startup_flush){
                if(log.bytes!=captured_startup){startup->capture_client_output(log.bytes);captured_startup=log.bytes;}
                next_startup_flush=Clock::now()+std::chrono::seconds{1};
            }
            const auto marker=native::latest_external_status_marker(log.bytes);
            const auto observed=native::read_latest_external_session_receipt(*pinned.directory,expected);if(observed)receipt=observed.snapshot;
            p.stage=native::external_progress_stage(p.stage,marker,failures,
                receipt && app::external_gameplay_receipt_ready(*receipt),cancelled.has_value());
            if(p.stage=="error")p.reason=marker && marker->stage=="error" ? marker->reason :
                !failures.primary_reason.empty()?failures.primary_reason:failures.cleanup_reason;
            if(!state.running || Clock::now()>=next_video_observation) {
                if(const auto aa_observation=native::read_external_msaa_observation(*pinned.directory,run_id))p.antialiasing=*aa_observation;
                next_video_observation=Clock::now()+std::chrono::seconds{1};
            }
            worker.publish(p);if(!state.running){exit=state.exit_code;if(!exit && failures.primary_reason.empty())failures.primary_reason="child_wait_failed_"+std::to_string(state.native_error);break;}
            std::this_thread::sleep_for(std::chrono::milliseconds{100});}
        const auto final_log=child.finish_log();observe_failures(final_log.bytes);
        if(p.actual_gpu.empty())p.actual_gpu=observed_client_gpu(final_log.bytes);
        if(startup)startup->capture_client_output(final_log.bytes);
        const auto safe_log=native::redacted_external_status_log(final_log.bytes)+"[launcher-capture] observed_bytes="+std::to_string(final_log.observed_bytes)+" byte_truncated="+(final_log.byte_truncated?"true":"false")+" capture_failed="+(final_log.capture_failed?"true":"false")+"\n";
        const bool log_published=native::write_external_text_atomic(run/L"client-status.log",safe_log,false);
        const auto terminal=native::read_latest_external_session_receipt(*pinned.directory,expected,true);if(terminal.snapshot)receipt=terminal.snapshot;
        const auto final_marker=native::latest_external_status_marker(final_log.bytes);
        const bool application_closed=native::external_application_close_succeeded(final_marker,exit,forced);
        const bool legacy_close=terminal && receipt && app::external_close_succeeded(&*receipt,!failures.primary_reason.empty(),!failures.cleanup_reason.empty());
        const bool success=process_recorded && log_published && exit && *exit==0 && (application_closed || legacy_close) && !forced;
        finalize(success,success?(application_closed?"application_closed":app::external_gameplay_receipt_ready(*receipt)?"normal_close_completed":"startup_cancelled"):!process_recorded?"process_manifest_publication_failed":!log_published?"client_status_log_publication_failed":forced?"explicit_owned_child_termination":!failures.primary_reason.empty()?failures.primary_reason:!failures.cleanup_reason.empty()?failures.cleanup_reason:cancelled?"cancelled_before_gameplay":exit && *exit!=0?"client_exit_"+std::to_string(*exit):"gameplay_or_terminal_receipt_not_observed");
    }catch(...){if(child_owner){const auto closed=native::close_external_child_after_failure(*child_owner);forced=closed.forced;exit=closed.exit_code;finalize(false,closed.exited?"launcher_internal_or_filesystem_error":"launcher_failed_child_cleanup_unconfirmed");}else finalize(false,"launcher_internal_or_filesystem_error");}
}
void enable_controls(Ui&ui,bool enabled)
{
    for(const int id:{server,name,display,resolution,mode,vsync,show_fps,brightness,gamma,contrast,volume,reset_image,hud_mode,hud_scale,reset_hud,msaa,background_sky,movement_diagnostics,system_diagnostics,assets,choose_assets,runtime,choose_runtime,connect_button,open_client_button,original_hl_button,gpu_choice,gpu_settings,save_button})::EnableWindow(get(ui,id),enabled);
    ::EnableWindow(get(ui,cancel_button),!enabled);update_modes(ui);update_image_labels(ui);
}
bool save_configuration(Ui&ui)
{
    if(!ui.offline_check && !native::write_external_text_atomic(ui.storage/L"gpu-preference.txt",std::string{gpu_token(ui.gpu_preference)}+"\n",true))return false;
    if(!ui.offline_check && !native::write_external_text_atomic(ui.storage/L"background-sky-policy.txt",std::string{sky_token(ui.sky_policy)}+"\n",true))return false;
    if(!native::merge_external_settings(ui.settings,ui.settings_baseline,ui.configuration))return false;
    const auto loaded=native::load_external_settings(ui.settings,ui.configuration);
    if(loaded.status!="loaded")return false;
    ui.configuration=loaded.configuration;ui.settings_status="loaded";ui.recovery_video_baseline.reset();fill(ui);
    return true;
}
void connect(Ui&ui,const bool open_client=false)
{
    if(ui.lifecycle.busy() || !collect(ui,true) || !refresh_displays(ui) || !collect(ui,true))return;
    if(!save_configuration(ui)){if(ui.startup)ui.startup->record("settings_save_failed");::MessageBoxW(ui.window,L"Не вдалося зберегти налаштування. Натисніть «Зберегти ZIP логів…».",L"Помилка",MB_OK|MB_ICONERROR);return;}
    if(!ui.lifecycle.begin())return;
    ui.last_report.clear();::EnableWindow(get(ui,report_button),FALSE);
    if(ui.configuration.video.mode==core::VideoWindowMode::borderless){const auto encoded=core::encode_external_launch_configuration(ui.configuration);if(!encoded || !native::write_external_text_atomic(ui.storage/L"pending-video.json",*encoded,true)){ui.lifecycle.finish(false);text(ui,status_label,L"Не вдалося зберегти безпечне відновлення відеорежиму.");return;}}
    ui.worker=std::make_unique<Worker>();auto*worker=ui.worker.get();
    const bool system_enabled=::SendMessageW(get(ui,system_diagnostics),BM_GETCHECK,0,0)==BST_CHECKED;
    const auto configuration=configuration_for_run(ui.configuration,system_enabled);
    const auto client=ui.executable,storage=ui.storage;const auto gpu=ui.gpu_preference;const auto sky=ui.sky_policy;
    std::array<wchar_t,32768> path{};const auto n=::GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));const std::filesystem::path launcher{std::wstring{path.data(),n}};
    auto*startup=ui.startup;
    worker->thread=std::thread{[worker,configuration,client,launcher,storage,open_client,system_enabled,startup,gpu,sky]{run_child(*worker,configuration,client,launcher,storage,open_client,system_enabled,startup,gpu,sky);}};enable_controls(ui,false);text(ui,gpu_actual,L"Фактична GPU: очікування нового запуску клієнта…");text(ui,status_label,open_client?L"Відкриття клієнта без підключення…":L"Перевірка локальних ресурсів…");
}
void run_original_half_life(Worker&worker,const hlclient::launcher::original_hl::Plan plan,
    const std::filesystem::path storage,const hlclient::launcher::GpuPreference preference,
    hlclient::launcher::StartupJournal*startup)
{
    namespace stock=hlclient::launcher::original_hl;
    Progress p;p.original_hl=true;std::filesystem::path report;std::optional<DWORD> exit;
    stock::Process child;bool gpu_preference_verified=false;
    const auto finish=[&](bool success,std::string reason){
        p.finished=true;p.success=success;p.reason=std::move(reason);p.stage=success?"completed":"error";
        if(startup){startup->record(success?"original_hl_finished":"original_hl_failed",p.reason);if(exit)startup->number("original_hl_exit",*exit);}
        try{if(!report.empty()){
            const auto json=std::string{"{\"schema\":\"hlclient.original-hl-launch.v1\",\"requested_endpoint\":"}+core::external_json_string(plan.request.endpoint)+
                ",\"executable\":"+core::external_json_string(native::external_utf8(plan.executable.wstring()))+
                ",\"requested_width\":"+std::to_string(plan.request.width)+",\"requested_height\":"+std::to_string(plan.request.height)+
                ",\"requested_windowed\":"+(plan.request.windowed?"true":"false")+",\"gpu_preference\":"+core::external_json_string(gpu_token(preference))+
                ",\"argument_encoding\":\"goldsrc_bare_switches_v1\",\"gpu_preference_verified\":"+(gpu_preference_verified?"true":"false")+
                ",\"process_id\":"+std::to_string(child.pid())+",\"exit_code\":"+(exit?std::to_string(*exit):"null")+
                ",\"status\":"+core::external_json_string(p.reason)+",\"gameplay_or_connection_confirmed\":false,\"actual_gpu\":null,\"launcher_rewrote_game_config\":false}\n";
            if(native::write_external_text_atomic(report,json,false))p.report=report;
            else{p.success=false;p.reason="original_launch_report_write_failed";}
        }}catch(...){p.success=false;p.reason="original_launch_report_write_failed";}
        worker.publish(p);
    };
    try{
        if(startup){startup->record("original_hl_launch_requested");startup->record("gpu_preference_requested",gpu_token(preference));}
        const auto run_id=native::new_external_run_id();if(run_id.empty()){finish(false,"run_identity_failed");return;}
        const auto root=storage/L"runs"/(L"original-hl-"+native::external_widen(run_id));std::filesystem::create_directories(root);
        report=root/L"original-hl-launch-report.json";
        if(worker.cancel){finish(false,"startup_cancelled");return;}
        // Refresh safety gates before touching the exact original-game preference.
        const auto checked=stock::make_plan(plan.request,stock::inspect_installation(plan.installation.root,plan.installation.source),stock::query_processes());
        if(!checked){finish(false,checked.error);return;}
        const auto gpu=hlclient::launcher::set_gpu_preference(checked.executable,preference);
        if(startup)startup->record(gpu.succeeded()?"gpu_preference_applied":"gpu_preference_failed",{},gpu.win32_error);
        if(!gpu.succeeded()){finish(false,"gpu_preference_failed");return;}
        const auto observed_gpu=hlclient::launcher::read_gpu_preference(checked.executable);
        gpu_preference_verified=observed_gpu.succeeded() && observed_gpu.preference==preference;
        if(!gpu_preference_verified){if(startup)startup->record("gpu_preference_readback_failed",{},observed_gpu.win32_error);finish(false,"gpu_preference_failed");return;}
        if(startup){startup->record("gpu_preference_verified");startup->record("original_hl_argument_encoding","goldsrc_bare_switches_v1");}
        if(worker.cancel){finish(false,"startup_cancelled");return;}
        const auto launched=child.launch(checked);
        if(!launched){if(startup)startup->record("original_hl_create_failed",launched.error,launched.system_error);finish(false,launched.error);return;}
        p.started=true;p.stage="original_hl_running";worker.publish(p);
        if(startup)startup->number("original_hl_pid",child.pid());
        std::optional<Clock::time_point> closing;bool close_posted=false;
        for(;;){
            const auto state=child.poll();if(!state.running){exit=state.exit_code;finish(exit && *exit==0,exit && *exit==0?"original_half_life_closed":state.error.empty()?"original_half_life_nonzero_exit":state.error);return;}
            if(worker.cancel && !closing){closing=Clock::now();p.stage="disconnecting";}
            if(closing && !close_posted)close_posted=child.request_close();
            if(closing && Clock::now()-*closing>std::chrono::seconds{8})p.cancel_timed_out=true;
            worker.publish(p);std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }catch(...){
        // Do not detach a still-running owned game on a report/filesystem
        // failure. Keep its lifecycle disabled until it actually exits.
        if(p.started){
            bool close_posted=child.request_close();p.stage="disconnecting";
            const auto closing=Clock::now();
            for(auto state=child.poll();state.running;state=child.poll()){
                if(!close_posted)close_posted=child.request_close();
                p.cancel_timed_out=Clock::now()-closing>std::chrono::seconds{8};
                worker.publish(p);std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
            exit=child.poll().exit_code;
        }
        finish(false,"original_half_life_launcher_error");
    }
}
void launch_original(Ui&ui)
{
    namespace stock=hlclient::launcher::original_hl;
    if(ui.lifecycle.busy() || !collect(ui,true) || !refresh_displays(ui) || !collect(ui,true))return;
    const auto current_plan=[&ui]{
        stock::Request request;request.asset_root=native::external_widen(ui.configuration.asset_root);
        request.endpoint=ui.configuration.endpoint;request.player_name=ui.configuration.name;
        request.width=ui.configuration.video.width;request.height=ui.configuration.video.height;
        request.windowed=ui.configuration.video.mode==core::VideoWindowMode::windowed;
        request.display_index=ui.configuration.video.display_index;
        return stock::make_plan(request,stock::resolve_installation(request.asset_root),stock::query_processes());
    };
    auto plan=current_plan();
    if(!plan){
        if(ui.startup)ui.startup->record("original_hl_preflight_failed",plan.error);
        const auto message=plan.error=="original_half_life_already_running_close_first"?L"Спочатку закрийте вже відкритий Half-Life, щоб вибрані GPU та відеорежим застосувалися до нового запуску.":
            plan.error=="steam_not_running"?L"Запустіть Steam і увійдіть у свій обліковий запис, потім повторіть запуск.":
            plan.error=="stock_display_selection_unsupported"?L"Для оригінального HL підтримується стандартний монітор. Оберіть перший монітор; примусове перемикання монітора GoldSrc не підтверджене.":
            plan.error=="invalid_player_name"?L"Для запуску оригінального HL ім’я не може містити окреме слово з мінусом на початку, +, ;, лапки чи зворотний слеш.":
            L"Не знайдено однозначну встановлену Steam Half-Life. Оберіть її папку steamapps\\common\\Half-Life у полі Half-Life. Причина — у логах запуску.";
        ::MessageBoxW(ui.window,message,L"Оригінальний Half-Life",MB_OK|MB_ICONWARNING);return;
    }
    if(!save_configuration(ui)){text(ui,status_label,L"Не вдалося зберегти налаштування запуску.");return;}
    // Saving merges unedited fields changed by another launcher. Launch exactly
    // that resulting configuration, not a stale pre-save server/video snapshot.
    plan=current_plan();
    if(!plan){if(ui.startup)ui.startup->record("original_hl_post_save_preflight_failed",plan.error);text(ui,status_label,L"Умови запуску Half-Life змінилися. Перевірте налаштування й повторіть запуск. Причина — у логах.");return;}
    if(!ui.lifecycle.begin())return;
    ui.last_report.clear();::EnableWindow(get(ui,report_button),FALSE);
    ui.worker=std::make_unique<Worker>();auto*worker=ui.worker.get();const auto storage=ui.storage;const auto gpu=ui.gpu_preference;auto*startup=ui.startup;
    worker->thread=std::thread{[worker,plan,storage,gpu,startup]{run_original_half_life(*worker,plan,storage,gpu,startup);}};
    enable_controls(ui,false);text(ui,gpu_actual,L"Оригінальний HL: GPU буде застосовано перед запуском. Фактична GPU цим лаунчером не підтверджується.");
    text(ui,status_label,L"Запуск оригінального Half-Life: "+plan.executable.wstring());
}
void cancel(Ui&ui)
{
    if(!ui.worker || !ui.lifecycle.busy())return;const auto p=ui.worker->snapshot();
    if(p.original_hl && p.cancel_timed_out){::MessageBoxW(ui.window,L"Закрийте оригінальний Half-Life через його меню. Лаунчер не завершує сторонні ігри примусово.",L"Half-Life ще працює",MB_OK|MB_ICONINFORMATION);return;}
    if(p.cancel_timed_out){if(::MessageBoxW(ui.window,L"Штатне від’єднання не завершилось за 8 секунд. Примусово завершити лише запущений цим лаунчером клієнт? Сервер залишиться працювати.",L"Клієнт не відповідає",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES)ui.worker->force=true;return;}
    static_cast<void>(ui.lifecycle.cancel());ui.worker->cancel=true;text(ui,status_label,L"Від’єднання…");
}
void tick(Ui&ui)
{
    if(!ui.worker)return;const auto p=ui.worker->snapshot();if(p.started)static_cast<void>(ui.lifecycle.child_started());
    if(!p.actual_gpu.empty())text(ui,gpu_actual,L"Фактично (клієнт): "+native::external_widen(p.actual_gpu));
    auto status=stage_label(p);
    if(p.finished && p.system_diagnostics_requested){
        status+=p.system_archive_ready?
            L"\r\nСистемний ZIP готовий. Доступні й пропущені джерела зазначені у звіті.":
            L"\r\nСистемний ZIP недоступний; часткові результати й причина — у папці сеансу.";
    }
    if(p.antialiasing){const auto&aa=*p.antialiasing;status+=L"\r\nОстаннє підтвердження MSAA: запит "+std::to_wstring(aa.requested_samples)+L"×, фактично "+(aa.actual_samples?std::to_wstring(aa.actual_samples)+L"×":L"вимкнено");
        if(aa.actual_samples<aa.requested_samples)status+=L". Нижчий доступний режим: "+native::external_widen(aa.fallback_reason);}
    if(p.finished && !p.success)status+=L"\r\nНатисніть «Зберегти ZIP логів…» для передачі причини збою.";
    text(ui,status_label,status);
    text(ui,cancel_button,p.original_hl?L"Закрити Half-Life":p.stage=="idle" || p.stage=="recoverable_error"?L"Закрити клієнт":p.stage=="in_game"?L"Від’єднатися":L"Скасувати підключення");
    if(!p.report.empty()){ui.last_report=p.report;::EnableWindow(get(ui,report_button),TRUE);}
    if(!p.finished)return;
    if(p.started && p.system_diagnostics_requested && !p.system_run_folder.empty())
        static_cast<void>(::ShellExecuteW(ui.window,L"open",p.system_run_folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    const bool borderless=ui.configuration.video.mode==core::VideoWindowMode::borderless;
    ui.lifecycle.finish(p.success);ui.worker.reset();enable_controls(ui,true);
    if(p.original_hl){text(ui,status_label,status);if(ui.close_after)::DestroyWindow(ui.window);return;}
    // The child can persist console settings while this form is disabled.
    // Reload them before any subsequent explicit edit/launch, never write the
    // stale launch snapshot back over those newer values on normal close.
    const auto loaded=native::load_external_settings(ui.settings,ui.configuration);
    if(loaded.status=="loaded"){ui.configuration=loaded.configuration;ui.settings_status="loaded";fill(ui);}
    if(borderless){if(fullscreen_close_confirmed(p)){
            // Borderless never changes the desktop mode. A normal application
            // close confirms it without a default-No dialog reverting the profile.
            if(!::DeleteFileW((ui.storage/L"pending-video.json").c_str()) && ::GetLastError()!=ERROR_FILE_NOT_FOUND)
                status+=L"\r\nНе вдалося прибрати маркер відновлення відеорежиму.";
        }
        else{const auto before=ui.configuration;ui.configuration.video.mode=core::VideoWindowMode::windowed;static_cast<void>(native::merge_external_settings(ui.settings,before,ui.configuration));fill(ui);}}
    text(ui,status_label,status);
    if(ui.close_after)::DestroyWindow(ui.window);
}
bool offline_scroll_check(Ui&ui)
{
    struct CheckTrace{const char*phase{"model"};UINT dpi{};bool passed{};
        ~CheckTrace(){if(!passed)std::fprintf(stderr,"launcher_scroll_check_phase=%s dpi=%u\n",phase,dpi);}} check;
    if(!hlclient::launcher::smooth_scroll_check())return false;
    const auto original_dpi=ui.dpi;
    const auto original_config=core::encode_external_launch_configuration(ui.configuration);
    RECT original{};::GetWindowRect(ui.window,&original);
    ::SetWindowPos(ui.window,nullptr,0,0,original.right-original.left,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    for(const UINT dpi:{96U,120U,144U,192U}){
        check.phase="font";check.dpi=dpi;
        ui.dpi=dpi;layout(ui);scroll_to(ui,40);
        const auto font=ui.font;layout(ui);
        if(!font || ui.font!=font || ui.scroll.maximum()<100 || ::IsWindowVisible(ui.window))return false;
        LOGFONTW description{};
        if(!::GetObjectW(font,sizeof(description),&description) || description.lfHeight!=-scaled(ui,14))return false;
        std::vector<RECT> before;std::vector<HFONT> before_fonts;
        for(const auto&p:ui.controls){RECT r{};::GetWindowRect(p.control,&r);before.push_back(r);
            before_fonts.push_back(reinterpret_cast<HFONT>(::SendMessageW(p.control,WM_GETFONT,0,0)));
            std::array<wchar_t,64> type{};::GetClassNameW(p.control,type.data(),static_cast<int>(type.size()));
            if(std::wstring_view{type.data()}!=TRACKBAR_CLASSW && before_fonts.back()!=font)return false;}
        check.phase="geometry";
        const auto offset=ui.scroll_pixels;
        // Small high-resolution wheel inputs must accumulate, without changing
        // font handles or moving even one child immediately to the final target.
        for(int i=0;i<4;++i)scroll_wheel(ui,-30,10.0,3,true);
        if(ui.scroll.target()!=88.0 || ui.scroll.position()!=40.0 || !ui.scroll_timer)return false;
        scroll_tick(ui,10.03);
        if(ui.scroll.position()<=40.0 || ui.scroll.position()>=88.0 || ui.font!=font)return false;
        const int moved=ui.scroll_pixels-offset;
        if(moved<=0)return false;
        for(std::size_t i=0;i<ui.controls.size();++i){RECT r{};::GetWindowRect(ui.controls[i].control,&r);
            if(r.top!=before[i].top-moved || r.bottom!=before[i].bottom-moved ||
                r.left!=before[i].left || r.right!=before[i].right ||
                reinterpret_cast<HFONT>(::SendMessageW(ui.controls[i].control,WM_GETFONT,0,0))!=before_fonts[i])return false;}
        scroll_tick(ui,10.2);
        if(ui.scroll.position()!=88.0 || ui.scroll_timer || ui.scroll.active())return false;
        // Smallest wheel units preserve their fractional displacement too.
        check.phase="fractional_wheel";
        scroll_to(ui,0);
        for(int i=0;i<120;++i)scroll_wheel(ui,-1,11.0,3,true);
        scroll_tick(ui,11.2);
        if(std::abs(ui.scroll.position()-48.0)>.000001 || ui.scroll_timer)return false;
        check.phase="scrollbar";scroll_wheel(ui,-120,12.0,3,true);
        scroll_bar(ui,SB_THUMBTRACK,44,100);
        if(ui.scroll.position()!=44 || ui.scroll.target()!=44 || ui.scroll_timer)return false;
        scroll_bar(ui,SB_LINEDOWN,0,100);if(ui.scroll.position()!=68)return false;
        scroll_bar(ui,SB_LINEUP,0,100);if(ui.scroll.position()!=44)return false;
        scroll_bar(ui,SB_PAGEDOWN,0,50);if(ui.scroll.position()!=94)return false;
        scroll_bar(ui,SB_PAGEUP,0,50);if(ui.scroll.position()!=44)return false;
        scroll_bar(ui,SB_BOTTOM,0,100);if(ui.scroll.position()!=ui.scroll.maximum())return false;
        scroll_bar(ui,SB_TOP,0,100);if(ui.scroll.position()!=0)return false;
        check.phase="focus";scroll_wheel(ui,-120,13.0,3,true);reveal_control(ui,get(ui,save_button));
        const auto revealed=ui.scroll.position();scroll_tick(ui,13.2);
        RECT page{};::GetClientRect(ui.window,&page);
        const auto item=std::find_if(ui.controls.begin(),ui.controls.end(),[&](const auto&p){return p.control==get(ui,save_button);});
        if(item==ui.controls.end() || revealed<=0 || ui.scroll.position()!=revealed || ui.scroll_timer ||
            item->y+std::min(30,item->h)>revealed+::MulDiv(page.bottom,96,static_cast<int>(dpi)))return false;
        reveal_control(ui,get(ui,server));
        if(ui.scroll.position()>34 || ui.scroll_timer)return false;
        check.phase="system_policy";scroll_to(ui,0);scroll_wheel(ui,-120,14.0,0,true);
        if(ui.scroll.position()!=0 || ui.scroll.active())return false;
        scroll_wheel(ui,-120,14.0,3,false);
        if(ui.scroll.position()!=48 || ui.scroll_timer || ui.scroll.active())return false;
        scroll_to(ui,0);scroll_wheel(ui,-120,15.0,WHEEL_PAGESCROLL,false);
        const double expected=std::min(static_cast<double>(ui.scroll.maximum()),page.bottom*96.0/dpi);
        if(ui.scroll.position()!=expected || ui.scroll_timer)return false;
        scroll_wheel(ui,120,16.0,3,true);layout(ui);
        if(ui.scroll_timer || ui.scroll.active() || ui.font!=font)return false;
        check.phase="slider_wheel";
        for(const int id:{brightness,gamma,contrast,volume}){
            const auto control=get(ui,id);const int value=slider(ui,id);
            for(const bool focused:{false,true}){
                ::SendMessageW(control,focused?WM_SETFOCUS:WM_KILLFOCUS,0,0);
                for(const short delta:{short{-120},short{120},short{-30},short{1}}){
                    const WPARAM wheel=MAKEWPARAM(focused?MK_CONTROL:0,static_cast<WORD>(delta));
                    scroll_to(ui,50);::SendMessageW(ui.window,WM_MOUSEWHEEL,wheel,0);
                    const auto expected_target=ui.scroll.target();
                    scroll_to(ui,50);::SendMessageW(control,WM_MOUSEWHEEL,wheel,0);
                    if(slider(ui,id)!=value || std::abs(ui.scroll.target()-expected_target)>.000001)return false;
                    ::SendMessageW(control,WM_MOUSEHWHEEL,wheel,0);
                    if(slider(ui,id)!=value || std::abs(ui.scroll.target()-expected_target)>.000001)return false;
                }
            }
            // Disabling wheel changes must not disable deliberate keyboard edits.
            ::SendMessageW(control,WM_KEYDOWN,VK_RIGHT,0);::SendMessageW(control,WM_KEYUP,VK_RIGHT,0);
            if(slider(ui,id)!=value+1)return false;
            ::SendMessageW(control,WM_KEYDOWN,VK_LEFT,0);::SendMessageW(control,WM_KEYUP,VK_LEFT,0);
            if(slider(ui,id)!=value)return false;
        }
        stop_scrolling(ui);
    }
    // Even when the viewport grows beyond the whole form, no stale target
    // remains to pull the controls away after resize or a DPI transition.
    check.phase="resize";ui.dpi=96;
    int expanded_height=0;for(const auto&p:ui.controls)expanded_height=std::max(expanded_height,p.y+p.h+120);
    ::SetWindowPos(ui.window,nullptr,0,0,original.right-original.left,expanded_height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    layout(ui);scroll_wheel(ui,-120,20.0,3,true);
    if(ui.scroll.maximum()!=0 || ui.scroll.position()!=0 || ui.scroll_timer)return false;
    ::SetWindowPos(ui.window,nullptr,0,0,original.right-original.left,original.bottom-original.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    ui.dpi=original_dpi;layout(ui);scroll_to(ui,0);
    check.phase="state_preservation";
    check.passed=!::IsWindowVisible(ui.window) && collect(ui,false) &&
        core::encode_external_launch_configuration(ui.configuration)==original_config && !ui.worker && !ui.lifecycle.busy();
    return check.passed;
}
bool offline_fullscreen_close_check(Ui&ui)
{
    // Exercise the real completion path with a worker that owns no thread or
    // child. Only this fresh temporary directory is writable by the fixture.
    const auto id=native::new_external_run_id();if(id.empty())return false;
    std::array<wchar_t,32768> temp{};const DWORD count=::GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
    if(!count || count>=temp.size())return false;
    const auto root=std::filesystem::path{temp.data()}/(L"HLClientFullscreenClose-"+native::external_widen(id));
    if(!::CreateDirectoryW(root.c_str(),nullptr))return false;
    struct Restore {
        Ui&ui;std::filesystem::path root,settings,storage,last_report;
        core::ExternalLaunchConfiguration configuration,baseline;
        core::ExternalLauncherLifecycle lifecycle;std::optional<core::VideoSettings> recovery;
        std::string settings_status;
        ~Restore(){
            ui.worker.reset();ui.settings=settings;ui.storage=storage;ui.last_report=last_report;
            ui.configuration=configuration;ui.settings_baseline=baseline;ui.lifecycle=lifecycle;
            ui.recovery_video_baseline=recovery;ui.settings_status=settings_status;fill(ui);enable_controls(ui,true);
            static_cast<void>(::DeleteFileW((root/L"launcher.json").c_str()));
            static_cast<void>(::DeleteFileW((root/L"pending-video.json").c_str()));
            static_cast<void>(::RemoveDirectoryW(root.c_str()));
        }
    } restore{ui,root,ui.settings,ui.storage,ui.last_report,ui.configuration,ui.settings_baseline,
        ui.lifecycle,ui.recovery_video_baseline,ui.settings_status};
    ui.storage=root;ui.settings=root/L"launcher.json";ui.recovery_video_baseline.reset();
    auto fullscreen=ui.configuration;fullscreen.video.mode=core::VideoWindowMode::borderless;
    const auto run_case=[&](const char*reason,bool success,bool external_windowed,bool pending){
        ui.configuration=fullscreen;fill(ui);
        auto persisted=fullscreen;persisted.show_fps=!persisted.show_fps;
        if(external_windowed)persisted.video.mode=core::VideoWindowMode::windowed;
        if(!native::save_external_settings(ui.settings,persisted))return false;
        if(pending && !native::save_external_settings(root/L"pending-video.json",fullscreen))return false;
        if(!ui.lifecycle.begin())return false;
        ui.worker=std::make_unique<Worker>();Progress p;p.finished=true;p.success=success;p.reason=reason;
        ui.worker->publish(p);tick(ui);
        const bool expect_windowed=!success || external_windowed;
        auto expected=persisted;expected.video.mode=expect_windowed?core::VideoWindowMode::windowed:core::VideoWindowMode::borderless;
        const auto disk=native::load_external_settings(ui.settings,fullscreen);
        const bool marker=::GetFileAttributesW((root/L"pending-video.json").c_str())!=INVALID_FILE_ATTRIBUTES;
        return !ui.worker && !ui.lifecycle.busy() && disk.status=="loaded" &&
            core::encode_external_launch_configuration(disk.configuration)==core::encode_external_launch_configuration(expected) &&
            core::encode_external_launch_configuration(ui.configuration)==core::encode_external_launch_configuration(expected) &&
            ::SendMessageW(get(ui,mode),BM_GETCHECK,0,0)==(expect_windowed?BST_CHECKED:BST_UNCHECKED) &&
            marker==(!success && pending);
    };
    return run_case("application_closed",true,false,true) &&
        run_case("normal_close_completed",true,false,true) &&
        run_case("application_closed",true,true,true) &&
        run_case("application_closed",true,false,false) &&
        run_case("client_exit_1",false,false,true);
}
bool offline_control_check(Ui&ui)
{
    for(const auto& reason:{"normal_close_completed","application_closed"}){
        Progress completed;completed.finished=true;completed.success=true;completed.reason=reason;
        if(!fullscreen_close_confirmed(completed))return false;
        completed.success=false;if(fullscreen_close_confirmed(completed))return false;
        completed.success=true;completed.finished=false;if(fullscreen_close_confirmed(completed))return false;
    }
    Progress rejected;rejected.finished=true;rejected.success=true;rejected.reason="cancelled_before_gameplay";
    if(fullscreen_close_confirmed(rejected))return false;
    if(!hlclient::launcher::StartupJournal::is_startup_error("[opengl-startup] context=1 vendor=Intel") ||
        hlclient::launcher::StartupJournal::is_startup_error("[opengl-startup] password=private") ||
        hlclient::launcher::StartupJournal::is_startup_error("[info] unrelated server text"))return false;
    // Exercise the real Win32 controls and the same collection routine used
    // by Connect. No save, preflight, Steam initialization or child process.
    if(read_control(ui,server)!=L"192.168.0.104:27016" || ui.displays.empty() || !collect(ui,false))return false;
    if(!offline_scroll_check(ui)){std::fprintf(stderr,"launcher_scroll_check=failed\n");return false;}
    std::fprintf(stdout,"launcher_scroll_check=passed duration_ms=120 dpi=96,120,144,192 fractional_wheel=verified native_geometry=verified slider_wheel_values=unchanged slider_wheel_routes=form keyboard_edits=verified\n");
    for(const int id:{server,name,display,resolution,mode,vsync,show_fps,brightness,gamma,contrast,volume,reset_image,hud_mode,hud_scale,hud_values,reset_hud,msaa,movement_diagnostics,movement_hotkey,system_diagnostics,startup_folder,startup_export,assets,choose_assets,runtime,choose_runtime,connect_button,open_client_button,original_hl_button,gpu_choice,gpu_settings,gpu_actual,cancel_button,report_button,save_button})if(!get(ui,id))return false;
    if(::SendMessageW(get(ui,gpu_choice),CB_GETCOUNT,0,0)!=3 || selected(ui,gpu_choice)!=0 ||
        read_control(ui,original_hl_button)!=L"Оригінальний Half-Life → сервер" || !::IsWindowEnabled(get(ui,original_hl_button)))return false;
    if(observed_client_gpu("[opengl-startup] context=1 renderer=Test GPU \r\n")!="Test GPU" ||
        !observed_client_gpu("server renderer=forged\n").empty() ||
        !observed_client_gpu("[opengl-startup] context=1 renderer=bad\tGPU\n").empty() ||
        decode_gpu_choice("high_performance\n")!=hlclient::launcher::GpuPreference::high_performance || decode_gpu_choice("invalid"))return false;
    for(int value=0;value<3;++value){select(ui,gpu_choice,value);if(!collect(ui,false) || static_cast<int>(ui.gpu_preference)!=value)return false;}
    select(ui,gpu_choice,0);if(!collect(ui,false))return false;
    if(!get(ui,background_sky) || (::GetWindowLongPtrW(get(ui,background_sky),GWL_STYLE)&BS_TYPEMASK)!=BS_AUTOCHECKBOX ||
        ::SendMessageW(get(ui,background_sky),BM_GETCHECK,0,0)!=BST_UNCHECKED ||
        ui.sky_policy!=native::BackgroundSkyPolicy::original)return false;
    if(decode_sky_choice("original\r\n")!=native::BackgroundSkyPolicy::original ||
        decode_sky_choice("depth\n")!=native::BackgroundSkyPolicy::depth ||
        decode_sky_choice("invalid") || decode_sky_choice("depth extra") || decode_sky_choice(""))return false;
    for(const bool enabled:{true,false,true,false}){
        ::SendMessageW(get(ui,background_sky),BM_SETCHECK,enabled?BST_CHECKED:BST_UNCHECKED,0);
        if(!collect(ui,false) || sky_token(ui.sky_policy)!=(enabled?"depth":"original"))return false;
        fill(ui);
        if(::SendMessageW(get(ui,background_sky),BM_GETCHECK,0,0)!=(enabled?BST_CHECKED:BST_UNCHECKED))return false;
    }
    enable_controls(ui,false);if(::IsWindowEnabled(get(ui,background_sky)))return false;
    enable_controls(ui,true);if(!::IsWindowEnabled(get(ui,background_sky)))return false;
    // Round-trip only this fresh fixture leaf, never the user's saved selection.
    {const auto id=native::new_external_run_id();if(id.empty())return false;
        std::array<wchar_t,32768> temp{};const auto count=::GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
        if(!count || count>=temp.size())return false;
        const auto folder=std::filesystem::path{temp.data()}/(L"HLClientSkyChoice-"+native::external_widen(id));
        if(!::CreateDirectoryW(folder.c_str(),nullptr))return false;
        const auto file=folder/L"background-sky-policy.txt";
        struct Cleanup{std::filesystem::path file,folder;~Cleanup(){static_cast<void>(::DeleteFileW(file.c_str()));static_cast<void>(::RemoveDirectoryW(folder.c_str()));}} cleanup{file,folder};
        if(native::read_external_text(file,32))return false;
        for(const auto policy:{native::BackgroundSkyPolicy::depth,native::BackgroundSkyPolicy::original}){
            if(!native::write_external_text_atomic(file,std::string{sky_token(policy)}+"\n",true))return false;
            const auto saved=native::read_external_text(file,32);if(!saved || decode_sky_choice(*saved)!=policy)return false;
        }
    }
    std::fprintf(stdout,"launcher_sky_checkbox_check=passed default=original toggles=verified persistence=verified busy_disabled=verified\n");
    if(read_control(ui,mode)!=L"Віконний режим" || read_control(ui,show_fps)!=L"Показувати FPS")return false;
    for(const int id:{mode,show_fps})if((::GetWindowLongPtrW(get(ui,id),GWL_STYLE)&BS_TYPEMASK)!=BS_AUTOCHECKBOX)return false;
    if(::SendMessageW(get(ui,mode),BM_GETCHECK,0,0)!=BST_UNCHECKED ||
        ::SendMessageW(get(ui,show_fps),BM_GETCHECK,0,0)!=BST_UNCHECKED ||
        ui.configuration.video.mode!=core::VideoWindowMode::borderless || !::IsWindowEnabled(get(ui,resolution)))return false;
    if(read_control(ui,open_client_button)!=L"Відкрити клієнт" || !::IsWindowEnabled(get(ui,open_client_button)))return false;
    const auto default_diagnostics=native::shared_test_storage_root()?BST_CHECKED:BST_UNCHECKED;
    if(::SendMessageW(get(ui,movement_diagnostics),BM_GETCHECK,0,0)!=default_diagnostics || ::SendMessageW(get(ui,system_diagnostics),BM_GETCHECK,0,0)!=default_diagnostics || read_control(ui,movement_hotkey).find(L"F8")==std::wstring::npos)return false;
    // Both defaults in the shared test remain ordinary, optional controls.
    ::SendMessageW(get(ui,movement_diagnostics),BM_SETCHECK,BST_UNCHECKED,0);
    ::SendMessageW(get(ui,system_diagnostics),BM_SETCHECK,BST_UNCHECKED,0);
    if(!collect(ui,false))return false;
    ::SendMessageW(get(ui,system_diagnostics),BM_SETCHECK,BST_CHECKED,0);
    {const auto saved=ui.configuration;const auto for_this_run=configuration_for_run(saved,true);
        if(saved.movement_diagnostics || !for_this_run.movement_diagnostics ||
            configuration_for_run(saved,false).movement_diagnostics)return false;}
    fill(ui);
    if(::SendMessageW(get(ui,system_diagnostics),BM_GETCHECK,0,0)!=BST_CHECKED ||
        ui.configuration.movement_diagnostics)return false;
    ::SendMessageW(get(ui,movement_diagnostics),BM_SETCHECK,BST_CHECKED,0);
    text(ui,server,L"connect 192.168.0.104:27016");text(ui,name,L"E31 Offline");text(ui,assets,L"D:\\Тест ресурси\\Half Life");text(ui,runtime,L"D:\\Тест ресурси\\Half Life\\steam_api.dll");
    slide(ui,brightness,17);slide(ui,gamma,137);slide(ui,contrast,163);slide(ui,volume,77);
    if(selected(ui,msaa)!=2 || selected(ui,hud_mode)!=0 || ::IsWindowEnabled(get(ui,hud_scale)))return false;
    select(ui,hud_mode,1);slide(ui,hud_scale,75);select(ui,msaa,3);update_image_labels(ui);
    if(!::IsWindowEnabled(get(ui,hud_scale)))return false;
    ::SendMessageW(get(ui,vsync),BM_SETCHECK,BST_UNCHECKED,0);
    ::SendMessageW(get(ui,show_fps),BM_SETCHECK,BST_CHECKED,0);
    ::SendMessageW(get(ui,mode),BM_SETCHECK,BST_UNCHECKED,0);::SendMessageW(ui.window,WM_COMMAND,MAKEWPARAM(mode,BN_CLICKED),0);
    if(!::IsWindowEnabled(get(ui,resolution)) || !collect(ui,false))return false;
    const auto encoded=core::encode_external_launch_configuration(ui.configuration);if(!encoded)return false;const auto decoded=core::decode_external_launch_configuration(*encoded);if(!decoded)return false;const auto&c=*decoded.configuration;
    if(c.endpoint!="192.168.0.104:27016" || c.name!="E31 Offline" || c.asset_root!=native::external_utf8(L"D:\\Тест ресурси\\Half Life") || c.steam_api_runtime!=native::external_utf8(L"D:\\Тест ресурси\\Half Life\\steam_api.dll") || c.video.mode!=core::VideoWindowMode::borderless || c.video.vsync || c.image!=core::ImageAdjustments{.17F,1.37F,1.63F} || c.volume!=77 || c.video.msaa_samples!=8 || c.hud_scale!=core::HudScaleSettings{false,.75F} || !c.movement_diagnostics || !c.show_fps)return false;
    const auto resolution_index=static_cast<std::size_t>(selected(ui,resolution));if(resolution_index>=ui.resolutions.size() || c.video.width!=ui.resolutions[resolution_index].first || c.video.height!=ui.resolutions[resolution_index].second || c.video.display_index!=static_cast<std::size_t>(selected(ui,display)))return false;
    fill(ui);if(::SendMessageW(get(ui,show_fps),BM_GETCHECK,0,0)!=BST_CHECKED)return false;
    ::SendMessageW(get(ui,show_fps),BM_SETCHECK,BST_UNCHECKED,0);
    ::SendMessageW(ui.window,WM_COMMAND,MAKEWPARAM(reset_image,BN_CLICKED),0);::SendMessageW(get(ui,mode),BM_SETCHECK,BST_CHECKED,0);::SendMessageW(ui.window,WM_COMMAND,MAKEWPARAM(mode,BN_CLICKED),0);
    if(!collect(ui,false) || ui.configuration.hud_scale!=core::HudScaleSettings{false,.75F})return false;
    ::SendMessageW(ui.window,WM_COMMAND,MAKEWPARAM(reset_hud,BN_CLICKED),0);
    // A native DPI change moves native controls, not their persisted game units.
    for(const UINT dpi:{96U,120U,144U,192U}){ui.dpi=dpi;layout(ui);if(!collect(ui,false) || ui.configuration.hud_scale!=core::HudScaleSettings{} || ui.configuration.video.msaa_samples!=8 || !ui.configuration.movement_diagnostics)return false;}
    // Reproduce pending-video recovery against an unchanged fullscreen disk
    // profile without touching real settings or launching a client.
    const auto before_recovery=ui.configuration;
    auto disk_profile=before_recovery;disk_profile.video.mode=core::VideoWindowMode::borderless;
    ui.configuration=disk_profile;ui.recovery_video_baseline=disk_profile.video;
    ui.configuration.video.mode=core::VideoWindowMode::windowed;fill(ui);
    if(!collect(ui,false))return false;
    const auto recovered=core::merge_external_launch_configuration(ui.settings_baseline,ui.configuration,disk_profile);
    if(!recovered || recovered->video.mode!=core::VideoWindowMode::windowed ||
        ::SendMessageW(get(ui,mode),BM_GETCHECK,0,0)!=BST_CHECKED)return false;
    ui.recovery_video_baseline.reset();ui.configuration=before_recovery;fill(ui);
    if(!offline_fullscreen_close_check(ui))return false;
    std::fprintf(stdout,"launcher_fps_windowed_check=passed default=borderless windowed_checkbox=verified fps_roundtrip=verified windowed_recovery_merge=verified normal_close_keeps_fullscreen=verified\n");
    return ::IsWindowEnabled(get(ui,resolution)) && !::IsWindowEnabled(get(ui,hud_scale)) && collect(ui,false) && ui.configuration.image==core::ImageAdjustments{} && ui.configuration.hud_scale==core::HudScaleSettings{} && ui.configuration.video.mode==core::VideoWindowMode::windowed && !ui.configuration.show_fps && !ui.worker && !ui.lifecycle.busy();
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam)
{
    Ui*ui=reinterpret_cast<Ui*>(::GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){ui=static_cast<Ui*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);ui->window=hwnd;::SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(ui));}
    if(!ui)return ::DefWindowProcW(hwnd,message,wparam,lparam);
    switch(message){
    case WM_CREATE:create_controls(*ui);::SetTimer(hwnd,kWorkerTimer,100,nullptr);if(ui->offline_check)::PostMessageW(hwnd,WM_APP+1,0,0);return 0;
    case WM_APP+1:ui->offline_exit_code=offline_control_check(*ui)?0:2;::DestroyWindow(hwnd);return 0;
    case WM_DPICHANGED:{ui->dpi=HIWORD(wparam);const auto*r=reinterpret_cast<RECT*>(lparam);::SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout(*ui);return 0;}
    case WM_SIZE:if(!ui->controls.empty())layout(*ui);return 0;
    // The hidden SDL utility window receives the same broadcast and refreshes
    // SDL's display cache. Defer enumeration until after that broadcast.
    case WM_DISPLAYCHANGE: ::PostMessageW(hwnd,WM_APP+2,0,0);return 0;
    case WM_APP+2:if(!ui->lifecycle.busy() && !ui->controls.empty())static_cast<void>(refresh_displays(*ui));return 0;
    case WM_VSCROLL:{SCROLLINFO info{sizeof(SCROLLINFO),SIF_ALL,0,0,0,0,0};::GetScrollInfo(hwnd,SB_VERT,&info);scroll_bar(*ui,LOWORD(wparam),info.nTrackPos,static_cast<int>(info.nPage));return 0;}
    case WM_MOUSEWHEEL:{UINT lines=3;BOOL animate=TRUE;::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);scroll_wheel(*ui,static_cast<short>(HIWORD(wparam)),scroll_time(),lines,animate!=FALSE);return 0;}
    case WM_HSCROLL:update_image_labels(*ui);return 0;
    case WM_TIMER:if(wparam==kScrollTimer)scroll_tick(*ui,scroll_time());else if(wparam==kWorkerTimer)tick(*ui);return 0;
    case WM_COMMAND:{const int id=LOWORD(wparam);if(id==mode && HIWORD(wparam)==BN_CLICKED){update_modes(*ui);return 0;}if(id==hud_mode && HIWORD(wparam)==CBN_SELCHANGE){update_image_labels(*ui);return 0;}if(id==display && HIWORD(wparam)==CBN_SELCHANGE){fill_resolutions(*ui);return 0;}if(HIWORD(wparam)!=BN_CLICKED && id!=IDOK && id!=IDCANCEL)return 0;
        if(id==connect_button || id==IDOK)connect(*ui);else if(id==open_client_button)connect(*ui,true);else if(id==original_hl_button)launch_original(*ui);else if(id==gpu_settings){static_cast<void>(::ShellExecuteW(hwnd,L"open",L"ms-settings:display-advancedgraphics",nullptr,nullptr,SW_SHOWNORMAL));}else if(id==cancel_button || id==IDCANCEL){if(ui->lifecycle.busy())cancel(*ui);else ::SendMessageW(hwnd,WM_CLOSE,0,0);}
        else if(id==reset_image){slide(*ui,brightness,0);slide(*ui,gamma,100);slide(*ui,contrast,100);update_image_labels(*ui);}
        else if(id==reset_hud){select(*ui,hud_mode,0);slide(*ui,hud_scale,100);update_image_labels(*ui);}
        else if(id==choose_assets)browse(*ui,true);else if(id==choose_runtime)browse(*ui,false);
        else if(id==save_button){if(collect(*ui,true))text(*ui,status_label,save_configuration(*ui)?L"Налаштування збережено. Вони діятимуть під час наступного запуску.":L"Не вдалося зберегти налаштування.");}
        else if(id==report_button && !ui->last_report.empty())open_report(*ui);
        else if(id==startup_folder || id==startup_export)open_startup_logs(*ui,id==startup_export);return 0;}
    case WM_CLOSE:if(ui->lifecycle.busy()){if(::MessageBoxW(hwnd,L"Від’єднати клієнт і закрити лаунчер після збереження звіту?",L"Гра ще працює",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES){ui->close_after=true;cancel(*ui);}return 0;}::DestroyWindow(hwnd);return 0;
    case WM_DESTROY: ::KillTimer(hwnd,kWorkerTimer);::KillTimer(hwnd,kScrollTimer);::PostQuitMessage(ui->offline_check?ui->offline_exit_code:0);return 0;
    default:return ::DefWindowProcW(hwnd,message,wparam,lparam);}
}
std::filesystem::path launcher_image()
{
    std::array<wchar_t,32768> image{};const DWORD count=::GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));
    if(!count || count>=image.size())throw std::runtime_error{"launcher_image_path_unavailable"};
    return std::filesystem::path{std::wstring{image.data(),count}};
}
std::filesystem::path offline_journal_root(bool offline)
{
    if(!offline)return {};
    std::array<wchar_t,32768> temp{};const DWORD count=::GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
    return count && count<temp.size()?std::filesystem::path{temp.data()}/L"HLClientEngine"/L"startup":std::filesystem::path{};
}
void initialize_launcher_dpi(hlclient::launcher::StartupJournal&journal)
{
    const auto user32=::GetModuleHandleW(L"user32.dll");
    using SetContext=BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const auto set_context=reinterpret_cast<SetContext>(::GetProcAddress(user32,"SetProcessDpiAwarenessContext"));
    if(set_context && set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))journal.record("dpi_ready","per_monitor_v2");
    else{static_cast<void>(::SetProcessDPIAware());journal.record("dpi_ready","system_fallback");}
}
UINT launcher_system_dpi()
{
    using GetDpi=UINT(WINAPI*)();
    const auto get_dpi=reinterpret_cast<GetDpi>(::GetProcAddress(::GetModuleHandleW(L"user32.dll"),"GetDpiForSystem"));
    if(get_dpi){const auto dpi=get_dpi();if(dpi)return dpi;}
    const auto dc=::GetDC(nullptr);const auto dpi=dc?::GetDeviceCaps(dc,LOGPIXELSX):96;if(dc)::ReleaseDC(nullptr,dc);
    return dpi>0?static_cast<UINT>(dpi):96U;
}
int offline_startup_check(hlclient::launcher::StartupJournal&journal)
{
    journal.record("offline_startup_check");journal.number("game_diagnostics_enabled",0);journal.number("system_diagnostics_enabled",0);
    // Private offline invocation: no saved settings, assets, auth or transport.
    // An invalid child-only video driver exercises the real fatal exception path.
    ::SetEnvironmentVariableW(L"SDL_VIDEODRIVER",L"hlclient-offline-missing-driver");
    ::SetEnvironmentVariableW(L"HLCLIENT_SMOKE_TEST_FRAMES",L"1");
    const auto client=launcher_image().parent_path()/L"hlclient.exe";
    const auto binary=native::observe_windows_binary_identity(client,native::kMaximumObservedExecutableBytes,{native::AuthenticodePolicy::not_required_for_downloaded_project_owned_binary,false});
    if(!binary){journal.record("offline_client_missing");return 2;}
    native::ExternalClientProcess child;
    if(!child.launch(*binary.identity,{L"--open-client",L"--width",L"640",L"--height",L"480",L"--window-mode",L"windowed",L"--msaa",L"off"},false)){
        journal.record("offline_child_create_failed",{},child.launch_error());return 2;}
    auto state=child.poll();const auto deadline=Clock::now()+std::chrono::seconds{15};
    while(state.running && Clock::now()<deadline){std::this_thread::sleep_for(std::chrono::milliseconds{10});state=child.poll();}
    if(state.running){static_cast<void>(child.terminate_owned_child(124));journal.record("offline_child_timeout");return 2;}
    const auto log=child.finish_log();journal.capture_client_output(log.bytes);
    if(state.exit_code)journal.client_exit(*state.exit_code);
    journal.record("offline_startup_failure_captured");
    const auto zip=journal.export_zip();
    const bool passed=state.exit_code && *state.exit_code==1 && !zip.empty() && log.bytes.find("[fatal]")!=std::string::npos;
    const auto json=std::string{"{\"startup_log\":"}+core::external_json_string(native::external_utf8(journal.directory().wstring()))+
        ",\"export_zip\":"+core::external_json_string(native::external_utf8(zip.wstring()))+",\"child_exit\":"+
        (state.exit_code?std::to_string(*state.exit_code):"null")+",\"diagnostics\":false}";
    std::fprintf(stdout,"launcher_startup_check_json=%s\nlauncher_startup_check=%s\n",json.c_str(),passed?"passed":"failed");
    return passed?0:2;
}
int offline_shared_storage_check(hlclient::launcher::StartupJournal&journal)
{
    const auto storage=native::shared_test_storage_root();
    if(!storage){journal.record("shared_storage_check","marker_missing");return 2;}
    ::SetEnvironmentVariableW(L"SDL_VIDEODRIVER",L"hlclient-offline-missing-driver");
    ::SetEnvironmentVariableW(L"HLCLIENT_SMOKE_TEST_FRAMES",L"1");
    core::ExternalLaunchConfiguration configuration;configuration.movement_diagnostics=true;
    configuration.video.width=640;configuration.video.height=480;
    configuration.video.mode=core::VideoWindowMode::windowed;configuration.video.msaa_samples=0;
    Worker worker;
    run_child(worker,configuration,launcher_image().parent_path()/L"hlclient.exe",launcher_image(),*storage,true,true,&journal);
    const auto result=worker.snapshot();
    journal.record("shared_storage_check",result.reason);
    const bool passed=result.finished && result.started && result.reason=="client_runtime_failed" && result.system_archive_ready;
    const auto json=std::string{"{\"storage\":"}+core::external_json_string(native::external_utf8(storage->wstring()))+
        ",\"run\":"+core::external_json_string(native::external_utf8(result.report.wstring()))+
        ",\"startup\":"+core::external_json_string(native::external_utf8(journal.directory().wstring()))+
        ",\"passed\":"+(passed?"true":"false")+"}";
    std::fprintf(stdout,"launcher_shared_storage_check_json=%s\n",json.c_str());return passed?0:2;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show)
{
    const auto options=std::wstring_view{command};
    const bool preview=options==L"--preview-ui";
    const bool offline=preview || options==L"--offline-ui-check" || options==L"--offline-startup-check" || options==L"--offline-shared-storage-check";
    const auto shared_storage=native::shared_test_storage_root();
    hlclient::launcher::StartupJournal journal{shared_storage?*shared_storage/L"startup":offline_journal_root(offline)};
    ::SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    try{
        journal.record("launcher_entry");journal.observe_package();
        journal.record("storage_mode",shared_storage?"shared_test":"local_user");
        if(shared_storage && journal.directory().parent_path()!=*shared_storage/L"startup")
            throw std::runtime_error{"Shared test storage is unavailable. Check write access to test-data beside HLClientLauncher.exe."};
        if(options==L"--offline-startup-check")return offline_startup_check(journal);
        if(options==L"--offline-shared-storage-check")return offline_shared_storage_check(journal);
        initialize_launcher_dpi(journal);
        const HRESULT com=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        struct Com{HRESULT code;~Com(){if(SUCCEEDED(code))::CoUninitialize();}}com_owner{com};
        const INITCOMMONCONTROLSEX common{sizeof(INITCOMMONCONTROLSEX),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};::InitCommonControlsEx(&common);
        const auto sdl_path=launcher_image().parent_path()/L"SDL3.dll";
        journal.record("loading_sdl");
        const auto sdl_module=::LoadLibraryExW(sdl_path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!sdl_module){const auto code=::GetLastError();journal.record("sdl_library_load_failed",{},code);throw std::runtime_error{"SDL3.dll or its x86 runtime dependency could not be loaded. Export startup logs."};}
        struct SdlModule{HMODULE value;~SdlModule(){::FreeLibrary(value);}} sdl_owner{sdl_module};
        platform::SdlRuntime sdl;journal.record("sdl_ready");
        // SDL3 updates its actual display cache from an SDL WindowProc. This
        // hidden, non-GL utility owns no game, network or renderer session.
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> display_events{
            SDL_CreateWindow("HLClient display events",1,1,SDL_WINDOW_HIDDEN|SDL_WINDOW_UTILITY),SDL_DestroyWindow};
        if(!display_events)throw std::runtime_error{"display_event_window_unavailable"};
        Ui ui;ui.startup=&journal;ui.offline_check=options==L"--offline-ui-check";
        if(shared_storage)ui.configuration.movement_diagnostics=true;
        ui.displays=platform::enumerate_video_displays();if(ui.displays.empty())throw std::runtime_error{"display_unavailable"};
        ui.configuration.video.width=std::max(640,ui.displays[0].desktop_width);ui.configuration.video.height=std::max(480,ui.displays[0].desktop_height);
        std::array<wchar_t,32768> binary{};const DWORD n=::GetModuleFileNameW(nullptr,binary.data(),static_cast<DWORD>(binary.size()));if(!n || n>=binary.size())return 2;ui.executable=std::filesystem::path{std::wstring{binary.data(),n}}.parent_path()/L"hlclient.exe";
        journal.number("display_count",ui.displays.size());
        const auto settings=native::default_external_settings_path();if(!settings){journal.record("settings_path_unavailable");throw std::runtime_error{"Windows LocalAppData folder is unavailable"};}ui.settings=*settings;ui.storage=ui.settings.parent_path();
        if(preview){ui.storage=journal.directory()/L"preview-profile";ui.settings=ui.storage/L"launcher.json";std::filesystem::create_directories(ui.storage);journal.record("ui_preview","isolated_profile_no_saved_user_settings");}
        if(!ui.offline_check && !preview){std::filesystem::create_directories(ui.storage);auto loaded=native::load_external_settings(ui.settings,ui.configuration);ui.configuration=std::move(loaded.configuration);ui.settings_status=loaded.status;
            if(loaded.status=="defaults"){
                // Reuse the existing bounded Steam-library discovery. A fresh
                // profile belongs to this PC; never copy another PC's settings.
                const auto installed=hlclient::launcher::original_hl::resolve_installation(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life");
                if(installed){ui.configuration.asset_root=native::external_utf8(installed.root.native());ui.configuration.steam_api_runtime=native::external_utf8((installed.root/L"steam_api.dll").native());journal.record("default_resources","local_steam_installation");}
            }
            if(const auto saved=native::read_external_text(ui.storage/L"background-sky-policy.txt",32)){
                if(const auto parsed=decode_sky_choice(*saved))ui.sky_policy=*parsed;else journal.record("sky_choice_file_invalid","default_original");
            }
            if(const auto saved=native::read_external_text(ui.storage/L"gpu-preference.txt",64)){
                if(const auto parsed=decode_gpu_choice(*saved))ui.gpu_preference=*parsed;else journal.record("gpu_choice_file_invalid");
            }else{const auto preference=hlclient::launcher::read_gpu_preference(ui.executable);if(preference.succeeded())ui.gpu_preference=preference.preference;else journal.record("gpu_preference_read_failed",{},preference.win32_error);}
            if(std::filesystem::exists(ui.storage/L"pending-video.json")){ui.recovery_video_baseline=ui.configuration.video;ui.configuration.video.mode=core::VideoWindowMode::windowed;ui.settings_status="unconfirmed_video_recovered_to_windowed";}}
        WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=instance;type.hCursor=::LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);type.lpszClassName=L"HLClientExternalLauncherE31";if(!::RegisterClassW(&type))return 2;
        ui.dpi=launcher_system_dpi();RECT work{};::SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        HWND window=::CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_COMPOSITED,type.lpszClassName,L"HL Client — підключення до LAN-сервера",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,scaled(ui,664),std::min(scaled(ui,825),static_cast<int>(work.bottom-work.top)),nullptr,nullptr,instance,&ui);if(!window)return 2;
        journal.record("launcher_ui_ready");
        ::ShowWindow(window,ui.offline_check?SW_HIDE:show);::UpdateWindow(window);MSG msg{};while(::GetMessageW(&msg,nullptr,0,0)>0){if(!::IsDialogMessageW(window,&msg)){::TranslateMessage(&msg);::DispatchMessageW(&msg);}}journal.record("launcher_closed");return static_cast<int>(msg.wParam);
    }catch(const std::exception&error){journal.capture_client_output(std::string{"[fatal] "}+error.what()+"\n");journal.record("launcher_startup_failed");const auto zip=journal.export_zip();
        const auto failure_json=std::string{"{\"startup_log\":"}+core::external_json_string(native::external_utf8(journal.directory().wstring()))+",\"export_zip\":"+core::external_json_string(native::external_utf8(zip.wstring()))+"}";
        std::fprintf(stderr,"launcher_startup_failure_json=%s\n",failure_json.c_str());
        if(!offline){const auto message=L"Не вдалося відкрити лаунчер.\r\n"+native::external_widen(error.what())+L"\r\n\r\nЛоги запуску: "+journal.directory().wstring()+L"\r\nZIP: "+zip.wstring();::MessageBoxW(nullptr,message.c_str(),L"HL Client — помилка запуску",MB_OK|MB_ICONERROR);}return 2;
    }catch(...){journal.record("launcher_unknown_failure");const auto zip=journal.export_zip();if(!offline){const auto message=L"Не вдалося відкрити лаунчер. Передайте ZIP логів:\r\n"+zip.wstring();::MessageBoxW(nullptr,message.c_str(),L"HL Client — помилка запуску",MB_OK|MB_ICONERROR);}return 2;}
}
