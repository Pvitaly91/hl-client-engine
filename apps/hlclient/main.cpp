#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/asset_manager.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/core/log.hpp>
#include <hlclient/core/version.hpp>
#include <hlclient/filesystem/game_paths.hpp>
#include <hlclient/filesystem/rooted_file_system.hpp>
#include <hlclient/goldsrc/compatibility.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace {

using hlclient::core::LogLevel;

class BootstrapSceneSource final : public hlclient::client::IClientSceneSource {
public:
    [[nodiscard]] hlclient::client::SceneUpdateResult update(
        const hlclient::client::FrameTime elapsed) override
    {
        world_state_.advance(elapsed);
        return {};
    }

    [[nodiscard]] const hlclient::client::ClientWorldState& world_state() const noexcept override
    {
        return world_state_;
    }

    [[nodiscard]] hlclient::client::ClientWorldState& mutable_world_state() noexcept
    {
        return world_state_;
    }

private:
    hlclient::client::ClientWorldState world_state_;
};

[[nodiscard]] std::vector<std::string> command_line_arguments(
    const int argument_count,
#ifdef _WIN32
    wchar_t* arguments[])
#else
    char* arguments[])
#endif
{
    std::vector<std::string> result;
    if (argument_count > 1) {
        result.reserve(static_cast<std::size_t>(argument_count - 1));
    }
    for (int index = 1; index < argument_count; ++index) {
#ifdef _WIN32
        const std::wstring_view wide_argument{arguments[index]};
        if (wide_argument.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::length_error{"Command-line argument is too long"};
        }
        if (wide_argument.empty()) {
            result.emplace_back();
            continue;
        }

        const int wide_size = static_cast<int>(wide_argument.size());
        const int required_size = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_argument.data(),
            wide_size,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required_size <= 0) {
            throw std::runtime_error{"Unable to convert a command-line argument to UTF-8"};
        }

        std::string converted(static_cast<std::size_t>(required_size), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wide_argument.data(),
                wide_size,
                converted.data(),
                required_size,
                nullptr,
                nullptr) != required_size) {
            throw std::runtime_error{"Unable to convert a command-line argument to UTF-8"};
        }
        result.push_back(std::move(converted));
#else
        result.emplace_back(arguments[index]);
#endif
    }
    return result;
}

[[nodiscard]] std::vector<std::string_view> argument_views(
    const std::vector<std::string>& arguments)
{
    std::vector<std::string_view> result;
    result.reserve(arguments.size());
    for (const auto& argument : arguments) {
        result.emplace_back(argument);
    }
    return result;
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view text)
{
#ifdef _WIN32
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error{"Filesystem path is too long"};
    }
    if (text.empty()) {
        return {};
    }

    const int utf8_size = static_cast<int>(text.size());
    const int required_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        utf8_size,
        nullptr,
        0);
    if (required_size <= 0) {
        throw std::runtime_error{"Unable to convert a UTF-8 filesystem path"};
    }

    std::wstring converted(static_cast<std::size_t>(required_size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            utf8_size,
            converted.data(),
            required_size) != required_size) {
        throw std::runtime_error{"Unable to convert a UTF-8 filesystem path"};
    }
    return std::filesystem::path{std::move(converted)};
#else
    return std::filesystem::path{text};
#endif
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

void print_version()
{
    std::cout << hlclient::core::kApplicationName << '\n'
              << "Version: " << hlclient::core::kVersion << '\n'
              << "Platform: " << hlclient::core::build_platform() << '\n'
              << std::flush;
}

[[nodiscard]] std::optional<std::uint64_t> smoke_test_frame_limit()
{
    constexpr std::uint64_t maximum_frames = 1'000'000;
#if defined(_MSC_VER)
    char* environment_value = nullptr;
    std::size_t environment_value_size = 0;
    const auto environment_result = ::_dupenv_s(
        &environment_value,
        &environment_value_size,
        "HLCLIENT_SMOKE_TEST_FRAMES");
    if (environment_result != 0) {
        throw std::runtime_error{"Unable to read HLCLIENT_SMOKE_TEST_FRAMES"};
    }
    if (environment_value == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned_environment_value{
        environment_value,
        &std::free};
    const std::string owned_value{owned_environment_value.get()};
    const std::string_view text{owned_value};
#else
    const char* environment_value = std::getenv("HLCLIENT_SMOKE_TEST_FRAMES");
    if (environment_value == nullptr) {
        return std::nullopt;
    }
    const std::string_view text{environment_value};
#endif

    std::uint64_t frames = 0;
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), frames, 10);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() || frames == 0 ||
        frames > maximum_frames) {
        throw std::invalid_argument{
            "Invalid HLCLIENT_SMOKE_TEST_FRAMES value (expected 1..1000000)"};
    }
    return frames;
}

void log_renderer_information(const hlclient::renderer::IRenderer& renderer)
{
    const auto& renderer_info = renderer.information();
    hlclient::core::log(LogLevel::info, "Renderer vendor: " + renderer_info.vendor);
    hlclient::core::log(LogLevel::info, "Renderer: " + renderer_info.device);
    hlclient::core::log(LogLevel::info, "Renderer version: " + renderer_info.version);
}

int run_null_renderer(
    hlclient::client::IClientSceneSource& scene_source,
    const std::optional<std::uint64_t> configured_frame_limit)
{
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    log_renderer_information(renderer);
    hlclient::core::log(LogLevel::info, "Client bootstrap complete");

    const std::uint64_t frame_limit = configured_frame_limit.value_or(1);
    auto previous_time = std::chrono::steady_clock::now();
    for (std::uint64_t frame = 0; frame < frame_limit; ++frame) {
        const auto current_time = std::chrono::steady_clock::now();
        const auto update = scene_source.update(current_time - previous_time);
        if (!update) {
            throw std::runtime_error{"Scene update failed: " + update.error};
        }
        previous_time = current_time;
        renderer.render(hlclient::client::build_render_scene(scene_source.world_state()), {});
    }

    hlclient::core::log(
        LogLevel::info,
        "Null renderer completed " +
            std::to_string(renderer.statistics().rendered_frames) + " frame(s)");
    renderer.shutdown();
    if (!renderer.statistics().shutdown) {
        throw std::logic_error{"Null renderer failed to shut down"};
    }
    return 0;
}

int run_opengl_renderer(
    hlclient::client::IClientSceneSource& scene_source,
    const std::optional<std::uint64_t> frame_limit)
{
    [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
    hlclient::core::log(LogLevel::info, "SDL initialized");

    hlclient::platform::SdlWindow window{
        hlclient::platform::SdlWindowConfig{std::string{hlclient::core::kApplicationName}, 1280, 720}};
    hlclient::core::log(LogLevel::info, "OpenGL context initialized");
    if (!window.vsync_enabled()) {
        hlclient::core::log(LogLevel::warning, "Vertical synchronization is unavailable");
    }

    // Declared after the window so its glad loader is released before the GL context is destroyed.
    hlclient::renderer::opengl::OpenGlRenderer renderer;
    log_renderer_information(renderer);
    hlclient::core::log(LogLevel::info, "Client bootstrap complete");

    auto previous_time = std::chrono::steady_clock::now();
    std::uint64_t rendered_frames = 0;
    bool running = true;
    while (running) {
        hlclient::platform::WindowEvent event;
        while (window.poll_event(event)) {
            if (event.type == hlclient::platform::WindowEventType::quit_requested) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        const auto current_time = std::chrono::steady_clock::now();
        const auto update = scene_source.update(current_time - previous_time);
        if (!update) {
            throw std::runtime_error{"Scene update failed: " + update.error};
        }
        previous_time = current_time;

        const auto extent = window.pixel_extent();
        renderer.render(
            hlclient::client::build_render_scene(scene_source.world_state()),
            hlclient::renderer::RenderExtent{extent.width, extent.height});
        window.swap_buffers();

        ++rendered_frames;
        if (frame_limit && rendered_frames >= *frame_limit) {
            running = false;
        }
    }

    return 0;
}

int run(const hlclient::core::CommandLineOptions& options)
{
    print_version();
    std::cout << '\n' << std::flush;

    hlclient::assets::AssetImporterRegistries asset_importers;
    std::unique_ptr<hlclient::filesystem::RootedFileSystem> asset_file_system;
    [[maybe_unused]] std::unique_ptr<hlclient::assets::AssetManager> asset_manager;
    if (options.base_directory) {
        const auto paths = hlclient::filesystem::validate_game_paths(
            path_from_utf8(*options.base_directory),
            path_from_utf8(options.game_directory));
        if (!paths) {
            hlclient::core::log(LogLevel::error, paths.error);
            return 1;
        }
        hlclient::core::log(
            LogLevel::info,
            "Half-Life game directory: " + path_as_utf8(paths.paths->game_directory));

        auto file_system_result =
            hlclient::filesystem::RootedFileSystem::create(paths.paths->game_directory);
        if (!file_system_result) {
            hlclient::core::log(
                LogLevel::error,
                file_system_result.error ? file_system_result.error->context
                                         : "Unable to create the asset filesystem");
            return 1;
        }
        asset_file_system = std::move(file_system_result.file_system);
        asset_manager = std::make_unique<hlclient::assets::AssetManager>(
            *asset_file_system,
            asset_importers);
        hlclient::core::log(
            LogLevel::info,
            "Asset pipeline initialized; no production format importers are registered in M0.1");
    } else {
        hlclient::core::log(
            LogLevel::info,
            "No Half-Life basedir selected; starting without game assets");
    }

    BootstrapSceneSource scene_source;
    if (options.connect_endpoint) {
        const auto address = hlclient::network::NetworkAddress::parse(*options.connect_endpoint);
        if (!address || address->port() == 0) {
            hlclient::core::log(
                LogLevel::error,
                "Invalid IPv4 endpoint for --connect: " + *options.connect_endpoint);
            return 1;
        }

        const hlclient::goldsrc::ConnectionIntent connection{*address};
        scene_source.mutable_world_state().set_connection_requested(true);
        hlclient::core::log(
            LogLevel::info,
            "Requested GoldSrc server: " + connection.server().to_string());
        hlclient::core::log(LogLevel::warning, hlclient::goldsrc::ConnectionIntent::status());
    }

    const auto frame_limit = smoke_test_frame_limit();
    if (options.renderer == hlclient::core::RendererBackend::null) {
        return run_null_renderer(scene_source, frame_limit);
    }
    return run_opengl_renderer(scene_source, frame_limit);
}

int application_main(const int argument_count,
#ifdef _WIN32
                     wchar_t* arguments[])
#else
                     char* arguments[])
#endif
{
    hlclient::core::initialize_logging(
#if !defined(NDEBUG)
        LogLevel::debug
#else
        LogLevel::info
#endif
    );

    const auto owned_arguments = command_line_arguments(argument_count, arguments);
    const auto arguments_without_program = argument_views(owned_arguments);
    const auto parsed = hlclient::core::parse_command_line(
        std::span<const std::string_view>{arguments_without_program});
    if (!parsed) {
        hlclient::core::log(LogLevel::error, parsed.error);
        std::cerr << hlclient::core::command_line_help();
        return 2;
    }

    if (parsed.options->show_help) {
        std::cout << hlclient::core::command_line_help();
        return 0;
    }
    if (parsed.options->show_version) {
        print_version();
        return 0;
    }

    try {
        return run(*parsed.options);
    } catch (const std::exception& exception) {
        hlclient::core::log(LogLevel::fatal, exception.what());
        return 1;
    }
}

} // namespace

#ifdef _WIN32
int wmain(const int argument_count, wchar_t* arguments[])
#else
int main(const int argument_count, char* arguments[])
#endif
{
    try {
        return application_main(argument_count, arguments);
    } catch (const std::exception& exception) {
        std::cerr << "[fatal] " << exception.what() << '\n';
        return 1;
    }
}
