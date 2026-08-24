#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<hlclient::world_preview::WorldPreviewCameraMode> camera_mode;
};

[[nodiscard]] std::optional<std::string> narrow_printable_ascii(
    const std::wstring_view value)
{
    std::string result;
    try {
        result.reserve(value.size());
    } catch (...) {
        return std::nullopt;
    }
    for (const wchar_t character : value) {
        if (character < 0x20 || character > 0x7e) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argument_count,
    wchar_t* arguments[])
{
    Options options;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument != L"--basedir" && argument != L"--game" &&
            argument != L"--map" && argument != L"--camera") {
            return std::nullopt;
        }
        if (index + 1 >= argument_count) {
            return std::nullopt;
        }
        const std::wstring_view value{arguments[++index]};
        if (value.empty()) {
            return std::nullopt;
        }

        if (argument == L"--basedir") {
            if (options.base_directory) {
                return std::nullopt;
            }
            options.base_directory = std::filesystem::path{value};
            continue;
        }

        auto narrow = narrow_printable_ascii(value);
        if (!narrow) {
            return std::nullopt;
        }
        if (argument == L"--game") {
            if (options.game_directory) {
                return std::nullopt;
            }
            options.game_directory = std::move(*narrow);
        } else if (argument == L"--map") {
            if (options.virtual_map) {
                return std::nullopt;
            }
            options.virtual_map = std::move(*narrow);
        } else {
            if (options.camera_mode) {
                return std::nullopt;
            }
            if (*narrow == "static") {
                options.camera_mode =
                    hlclient::world_preview::WorldPreviewCameraMode::
                        static_camera;
            } else if (*narrow == "orbit") {
                options.camera_mode =
                    hlclient::world_preview::WorldPreviewCameraMode::orbit;
            } else {
                return std::nullopt;
            }
        }
    }
    if (!options.base_directory || !options.game_directory ||
        !options.virtual_map || !options.camera_mode) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_world_viewer --basedir <Half-Life root> "
           "--game <directory> --map <maps/name.bsp> "
           "--camera <static|orbit>\n";
}

[[nodiscard]] std::optional<std::uint64_t> smoke_test_frame_limit()
{
    constexpr std::uint64_t maximum_frames = 1'000'000U;
#if defined(_MSC_VER)
    char* environment_value = nullptr;
    std::size_t environment_value_size = 0U;
    const auto environment_result = ::_dupenv_s(
        &environment_value,
        &environment_value_size,
        "HLCLIENT_SMOKE_TEST_FRAMES");
    if (environment_result != 0) {
        throw std::runtime_error{"Unable to read the frame-limit environment"};
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

    std::uint64_t frames = 0U;
    const auto conversion =
        std::from_chars(text.data(), text.data() + text.size(), frames, 10);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != text.data() + text.size() || frames == 0U ||
        frames > maximum_frames) {
        throw std::invalid_argument{"Invalid frame-limit environment"};
    }
    return frames;
}

[[nodiscard]] bool local_source_terminal(
    const hlclient::local_assets::LocalAssetSourceOpenState state) noexcept
{
    using State = hlclient::local_assets::LocalAssetSourceOpenState;
    return state == State::source_ready || state == State::cancelled ||
        state == State::timed_out || state == State::failed;
}

[[nodiscard]] std::optional<hlclient::local_assets::LocalAssetSource>
open_map_source(
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment,
    const hlclient::local_resources::LocalResourceLocator& locator)
{
    hlclient::local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes =
        hlclient::goldsrc::bsp::kGoldSrcBspDefaultMaximumSourceBytes;
    limits.maximum_chunks_per_update = 1U;
    limits.maximum_open_sources = 1U;

    hlclient::local_assets::LocalAssetSourceOpener opener;
    auto started = opener.begin(locator, environment, limits);
    if (!started) {
        const auto code = started.error
                              ? hlclient::local_assets::to_string(
                                    started.error->code)
                              : std::string_view{"source_open_failed"};
        std::cerr << "map-source-open=" << code << '\n';
        return std::nullopt;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 1'000'000U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !local_source_terminal(operation.state());
         ++update) {
        operation.update(now);
    }
    if (operation.state() !=
        hlclient::local_assets::LocalAssetSourceOpenState::source_ready) {
        const auto code = operation.error()
                              ? hlclient::local_assets::to_string(
                                    operation.error()->code)
                              : std::string_view{"source_open_incomplete"};
        std::cerr << "map-source-open=" << code << '\n';
        return std::nullopt;
    }
    return operation.take_result();
}

[[nodiscard]] std::optional<hlclient::assets::WorldTextureSet>
import_complete_textures(
    const hlclient::assets::WorldAsset& world,
    const std::span<const std::byte> retained_bsp_source,
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment)
{
    auto started = hlclient::goldsrc::WorldTextureImportOperation::begin(
        world,
        retained_bsp_source,
        environment);
    if (!started) {
        const auto code = started.error
                              ? hlclient::goldsrc::to_string(started.error->code)
                              : std::string_view{"texture_import_begin_failed"};
        std::cerr << "texture-import=" << code << '\n';
        return std::nullopt;
    }

    auto& operation = *started.operation;
    constexpr std::size_t maximum_updates = 1'000'000U;
    const auto now = std::chrono::steady_clock::time_point{};
    for (std::size_t update = 0U;
         update < maximum_updates && !operation.terminal();
         ++update) {
        operation.update(now);
    }
    if (!operation.terminal() || operation.result() == nullptr) {
        const auto code = operation.error()
                              ? hlclient::goldsrc::to_string(
                                    operation.error()->code)
                              : std::string_view{"texture_import_incomplete"};
        std::cerr << "texture-import=" << code << '\n';
        return std::nullopt;
    }
    if (!operation.result()->complete_for_world_materials()) {
        std::cerr << "texture-import=textures_incomplete\n";
        return std::nullopt;
    }
    auto textures = operation.take_result();
    if (!textures) {
        std::cerr << "texture-import=result_unavailable\n";
    }
    return textures;
}

[[nodiscard]] std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
build_world_render_package(
    hlclient::assets::WorldAsset world,
    hlclient::assets::WorldTextureSet textures,
    const std::span<const std::byte> retained_bsp_source)
{
    auto imported_lightmaps =
        hlclient::goldsrc::lightmaps::GoldSrcWorldLightmapImporter::import(
            world,
            retained_bsp_source);
    if (!imported_lightmaps) {
        const auto code = imported_lightmaps.error
                              ? hlclient::goldsrc::lightmaps::to_string(
                                    imported_lightmaps.error->code)
                              : std::string_view{"lightmap_import_failed"};
        std::cerr << "lightmap-import=" << code << '\n';
        return {};
    }
    if (!imported_lightmaps.lightmap_set->complete_for_world_surfaces()) {
        std::cerr << "lightmap-import=bindings_incomplete\n";
        return {};
    }

    hlclient::world_render::WorldRenderPackageBuilder builder;
    auto built = builder.build(
        hlclient::assets::TexturedWorldAsset{
            std::move(world),
            std::move(textures),
        },
        std::move(*imported_lightmaps.lightmap_set));
    if (!built) {
        const auto code = built.error
                              ? hlclient::world_render::to_string(
                                    built.error->code)
                              : std::string_view{"package_build_failed"};
        std::cerr << "render-package=" << code << '\n';
        return {};
    }
    return std::make_shared<hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] int render_world(
    std::shared_ptr<const hlclient::world_render::WorldRenderPackage> package,
    const hlclient::world_preview::WorldPreviewCameraMode camera_mode,
    const std::optional<std::uint64_t> frame_limit)
{
    hlclient::world_preview::WorldPreviewSceneOptions scene_options;
    scene_options.camera_mode = camera_mode;
    scene_options.cull_mode = hlclient::client::PreviewWorldCullMode::none;
    hlclient::world_preview::WorldPreviewSceneSource scene_source{
        package,
        scene_options};

    // Every CPU prerequisite and the immutable package are valid before SDL
    // or an OpenGL context exists.
    [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
    hlclient::platform::SdlWindow window{hlclient::platform::SdlWindowConfig{
        "HL Client World Viewer",
        1280,
        720,
        frame_limit.has_value(),
    }};
    // Declared after the window so GPU resources are released before context
    // destruction.
    hlclient::renderer::opengl::OpenGlRenderer renderer;

    auto previous_time = std::chrono::steady_clock::now();
    auto current_extent = window.pixel_extent();
    std::uint64_t rendered_frames = 0U;
    bool running = true;
    while (running) {
        hlclient::platform::WindowEvent event;
        while (window.poll_event(event)) {
            if (event.type ==
                hlclient::platform::WindowEventType::quit_requested) {
                running = false;
            } else if (event.extent.width > 0 && event.extent.height > 0) {
                current_extent = event.extent;
            }
        }
        if (!running) {
            break;
        }

        const auto current_time = std::chrono::steady_clock::now();
        const auto updated = scene_source.update(current_time - previous_time);
        if (!updated) {
            std::cerr << "scene-update=failed\n";
            return 1;
        }
        previous_time = current_time;

        current_extent = window.pixel_extent();
        renderer.render(
            hlclient::client::build_render_scene(scene_source.world_state()),
            hlclient::renderer::RenderExtent{
                current_extent.width,
                current_extent.height,
            });
        window.swap_buffers();
        ++rendered_frames;
        if (frame_limit && rendered_frames >= *frame_limit) {
            running = false;
        }
    }

    const auto& renderer_statistics = renderer.statistics();
    const auto& package_statistics = package->statistics();
    const auto& texture_statistics =
        package->textured_world().textures.statistics();
    const auto& lightmap_statistics = package->lightmaps().statistics();

    std::cout << "geometry-vertices=" << package_statistics.vertex_count << '\n';
    std::cout << "geometry-triangles=" << package_statistics.triangle_count
              << '\n';
    std::cout << "geometry-surfaces="
              << package_statistics.source_surface_count << '\n';
    std::cout << "decoded-textures="
              << texture_statistics.decoded_texture_count << '\n';
    std::cout << "masked-textures="
              << texture_statistics.masked_texture_count << '\n';
    std::cout << "lightmap-pages=" << lightmap_statistics.atlas_page_count
              << '\n';
    std::cout << "lightmap-bindings="
              << lightmap_statistics.surface_binding_count << '\n';
    std::cout << "world-upload-count=" << renderer_statistics.upload_count
              << '\n';
    std::cout << "rendered-frames="
              << renderer_statistics.rendered_frame_count << '\n';
    std::cout << "draw-calls=" << renderer_statistics.draw_call_count << '\n';
    std::cout << "triangles=" << renderer_statistics.triangle_count << '\n';
    std::cout << "vsync-enabled=" << (window.vsync_enabled() ? 1 : 0) << '\n';
    std::cout << "network-operations=0\n";
    std::cout << "writes-performed=0\n";

    if (frame_limit && rendered_frames != *frame_limit) {
        std::cerr << "viewer-runtime=frame_limit_not_reached\n";
        return 1;
    }
    if (rendered_frames > 0U &&
        (renderer_statistics.upload_count != 1U ||
            renderer_statistics.rendered_frame_count != rendered_frames ||
            renderer_statistics.draw_call_count == 0U ||
            renderer_statistics.triangle_count == 0U)) {
        std::cerr << "viewer-runtime=world_render_incomplete\n";
        return 1;
    }
    return 0;
}

[[nodiscard]] int run_viewer(
    const int argument_count,
    wchar_t* arguments[])
{
    const auto options = parse_options(argument_count, arguments);
    if (!options) {
        print_usage();
        return 2;
    }
    const auto frame_limit = smoke_test_frame_limit();

    auto virtual_map =
        hlclient::local_resources::LocalVirtualResourceName::create(
            *options->virtual_map);
    if (!virtual_map) {
        const auto code = virtual_map.error
                              ? hlclient::local_resources::to_string(
                                    virtual_map.error->code)
                              : std::string_view{"unsafe_name"};
        std::cerr << "map-name=" << code << '\n';
        return 1;
    }
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        *options->base_directory,
        *options->game_directory);
    if (!roots) {
        const auto code = roots.error
                              ? hlclient::local_resources::to_string(
                                    roots.error->code)
                              : std::string_view{"io_error"};
        std::cerr << "root-validation=" << code << '\n';
        return 1;
    }

    auto resolver_limits =
        hlclient::local_resources::LocalResourceResolverLimits{};
    resolver_limits.maximum_file_size =
        hlclient::local_resources::kHardMaximumLocalResourceFileSize;
    auto created_environment =
        hlclient::local_resources::LocalResourceEnvironment::create(
            std::move(*roots.roots),
            resolver_limits);
    if (!created_environment || !created_environment.environment) {
        const auto code = created_environment.error
                              ? hlclient::local_resources::to_string(
                                    created_environment.error->code)
                              : std::string_view{
                                    "unable_to_retain_environment"};
        std::cerr << "environment=" << code << '\n';
        return 1;
    }
    auto environment = std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>{
        std::move(created_environment.environment)};

    auto resolved = environment->resolver().resolve(*virtual_map.name);
    if (!resolved) {
        std::cerr << "map-resolution="
                  << hlclient::local_resources::to_string(resolved.code)
                  << '\n';
        return 1;
    }
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto source_size = resolved.file->file_size();
    resolved.file->close();
    auto locator = environment->make_locator(
        root_id,
        std::move(*virtual_map.name),
        identity,
        source_size);
    if (!locator) {
        const auto code = locator.error
                              ? hlclient::local_resources::to_string(
                                    locator.error->code)
                              : std::string_view{"invalid_locator"};
        std::cerr << "map-locator=" << code << '\n';
        return 1;
    }

    auto source = open_map_source(environment, *locator.locator);
    if (!source) {
        return 1;
    }
    const auto retained_bsp_source = source->source().bytes();
    hlclient::goldsrc::bsp::GoldSrcBspWorldImporter importer;
    auto imported = importer.import(source->source());
    if (!imported) {
        std::cerr << "bsp-import=failed\n";
        return 1;
    }
    auto world = std::move(imported).value();
    auto textures =
        import_complete_textures(world, retained_bsp_source, environment);
    if (!textures) {
        return 1;
    }

    auto package = build_world_render_package(
        std::move(world),
        std::move(*textures),
        retained_bsp_source);
    if (!package) {
        return 1;
    }
    // The renderer and scene source receive only the immutable package. Close
    // every verified source/environment handle before SDL or OpenGL starts.
    source.reset();
    environment.reset();
    return render_world(
        std::move(package),
        *options->camera_mode,
        frame_limit);
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    try {
        return run_viewer(argument_count, arguments);
    } catch (const hlclient::renderer::opengl::OpenGlRendererError& error) {
        std::cerr << "opengl-render="
                  << hlclient::renderer::opengl::to_string(error.code())
                  << '\n';
    } catch (const std::bad_alloc&) {
        std::cerr << "viewer=allocation_failed\n";
    } catch (const std::exception&) {
        // Exception text may contain platform paths or driver strings. Keep
        // diagnostics bounded and metadata-only at this trust boundary.
        std::cerr << "viewer=failed\n";
    }
    return 1;
}
