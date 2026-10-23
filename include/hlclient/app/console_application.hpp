#pragma once

#include <hlclient/console/console.hpp>
#include <hlclient/console/game_actions.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/app/loading_presentation.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <chrono>
#include <functional>
#include <memory>

namespace hlclient::app {
using ConsoleSessionActions=console::SessionActions;
using ConsoleTheme = std::function<renderer::ScreenUiFrame(unsigned,unsigned,double,bool)>;
using ConsoleGameRegistration = std::function<void(console::CommandRegistry&, console::ConsoleBindings&,
    const ConsoleSessionActions&, const std::function<void(gameplay_input::GameplayInputAction,bool)>&)>;

struct ConsolePacingStatistics final {
    bool enabled{};
    double requested_cap_hz{},effective_software_cap_hz{};
    std::uint64_t observations{},sleep_calls{},last_wait_us{},total_wait_us{},maximum_wait_us{};
};

// Application-owned; session callbacks are detached before their owners die.
// The same window, GL context, editor and history survive every attempt.
class ConsoleApplication final {
public:
    using Clock=std::chrono::steady_clock;
    ConsoleApplication(core::CommandLineOptions options,ConsoleTheme theme,ConsoleGameRegistration game_registration,
        bool hidden=false, bool use_user_config=true, std::function<double()> monotonic_seconds={},
        std::function<std::optional<std::string>()> startup_config_loader={});
    ~ConsoleApplication();
    core::CommandLineOptions& options() noexcept {return options_;}
    platform::SdlWindow& window() noexcept {return window_;}
    renderer::opengl::OpenGlRenderer& renderer() noexcept {return renderer_;}
    console::ConsoleModel& model() noexcept {return model_;}
    void set_session_actions(ConsoleSessionActions actions);
    void begin_attempt();
    void begin_disconnect();
    void end_attempt(int result,std::string_view reason);
    void in_game();
    void set_stage(LoadingStage stage);
    void set_work(std::shared_ptr<LoadingWork> work);
    void begin_frame(input::InputStateTracker* tracker=nullptr);
    bool route_event(const input::InputEvent& event,input::InputStateTracker& tracker);
    void pump_idle();
    void compose(renderer::RenderScene& scene,bool scene_ready_for_presentation=false);
    void present_idle();
    void pace();
    [[nodiscard]] ConsolePacingStatistics pacing_statistics() const noexcept {
        auto value=pacing_statistics_;
        value.enabled=options_.movement_diagnostics;
        value.requested_cap_hz=fps_max_;
        value.effective_software_cap_hz=fps_max_;
        return value;
    }
    void reset_pacing_statistics() noexcept {pacing_statistics_={};}
    void request_quit();
    bool shutdown_requested() const noexcept {return shutdown_;}
    bool session_stop_requested() const noexcept {return disconnect_||shutdown_;}
    bool console_active() const noexcept {return controller_.takes_input();}
    bool in_game_state() const noexcept {return state_==console::SessionState::in_game;}
    // Explicit connection intent or shutdown; internal teardown/loading
    // transitions do not count and cannot restart a cancelled retry chain.
    std::uint64_t session_request_revision() const noexcept {return session_request_revision_;}
    std::optional<std::string> take_connect();
    std::uint64_t generation() const noexcept {return generation_;}
    double sensitivity_scale() const noexcept {return sensitivity_/3.0;}
    void execute(std::string_view command,console::ExecutionSource source=console::ExecutionSource::local_console);
private:
    void register_commands();
    void register_settings();
    void action(gameplay_input::GameplayInputAction,bool);
    void release_actions(input::InputStateTracker*);
    void toggle(input::InputStateTracker*);
    void persist_shared_settings();
    double seconds() const noexcept;
    core::CommandLineOptions options_;
    platform::SdlRuntime runtime_;
    platform::SdlWindow window_;
    renderer::opengl::OpenGlRenderer renderer_;
    console::ConsoleModel model_;
    console::ConsoleController controller_;
    console::CommandRegistry commands_;
    console::CVarRegistry cvars_;
    console::CommandExecutor executor_{commands_,cvars_};
    console::ConsoleBindings bindings_;
    std::unique_ptr<console::ConsoleConfigStore> config_;
    ConsoleTheme theme_;
    ConsoleSessionActions session_actions_,dispatch_actions_;
    console::SessionState state_{console::SessionState::idle};
    LoadingPresentation loading_;
    LoadingCurtain curtain_;
    std::shared_ptr<LoadingWork> work_;
    std::vector<input::InputEvent> pending_actions_;
    std::optional<std::string> connect_,last_endpoint_;
    std::uint64_t generation_{},session_request_revision_{};
    bool shutdown_{},disconnect_{},control_{},shift_{},ignore_opening_text_{},pending_release_all_{},startup_config_{};
    unsigned control_keys_{},shift_keys_{};
    double sensitivity_{3.0},fps_max_{};
    core::ExternalLaunchConfiguration shared_baseline_;
    std::optional<std::filesystem::path> shared_settings_path_;
    bool show_fps_{};
    Clock::time_point origin_{Clock::now()},frame_started_{origin_};
    std::optional<double> fps_last_present_;
    double fps_{},fps_window_elapsed_{};
    std::uint64_t fps_window_frames_{};
    ConsolePacingStatistics pacing_statistics_;
    std::function<double()> monotonic_seconds_;
};
}
