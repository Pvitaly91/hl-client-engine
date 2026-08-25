#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
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

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include <utility>
#include <vector>

namespace {

struct Options {
    std::optional<std::filesystem::path> base_directory;
    std::optional<std::string> game_directory;
    std::optional<std::string> virtual_map;
    std::optional<hlclient::world_preview::WorldPreviewCameraMode> camera_mode;
    hlclient::world_visibility::WorldVisibilityMode visibility_mode{
        hlclient::world_visibility::WorldVisibilityMode::all};
    hlclient::world_preview::WorldPreviewBrushSubmodelsMode brush_submodels{
        hlclient::world_preview::WorldPreviewBrushSubmodelsMode::off};
    hlclient::client::PreviewWorldCullMode cull_mode{
        hlclient::client::PreviewWorldCullMode::none};
    bool visibility_mode_present{false};
    bool brush_submodels_present{false};
    bool cull_mode_present{false};
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
            argument != L"--map" && argument != L"--camera" &&
            argument != L"--visibility" &&
            argument != L"--brush-submodels" && argument != L"--cull") {
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
        } else if (argument == L"--camera") {
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
            } else if (*narrow == "spawn") {
                options.camera_mode =
                    hlclient::world_preview::WorldPreviewCameraMode::spawn;
            } else {
                return std::nullopt;
            }
        } else if (argument == L"--visibility") {
            if (options.visibility_mode_present) {
                return std::nullopt;
            }
            options.visibility_mode_present = true;
            if (*narrow == "all") {
                options.visibility_mode =
                    hlclient::world_visibility::WorldVisibilityMode::all;
            } else if (*narrow == "frustum") {
                options.visibility_mode = hlclient::world_visibility::
                    WorldVisibilityMode::frustum_only;
            } else if (*narrow == "pvs") {
                options.visibility_mode =
                    hlclient::world_visibility::WorldVisibilityMode::pvs_only;
            } else if (*narrow == "pvs-frustum") {
                options.visibility_mode = hlclient::world_visibility::
                    WorldVisibilityMode::pvs_and_frustum;
            } else {
                return std::nullopt;
            }
        } else if (argument == L"--brush-submodels") {
            if (options.brush_submodels_present) {
                return std::nullopt;
            }
            options.brush_submodels_present = true;
            if (*narrow == "off") {
                options.brush_submodels = hlclient::world_preview::
                    WorldPreviewBrushSubmodelsMode::off;
            } else if (*narrow == "static") {
                options.brush_submodels = hlclient::world_preview::
                    WorldPreviewBrushSubmodelsMode::static_instances;
            } else {
                return std::nullopt;
            }
        } else {
            if (options.cull_mode_present) {
                return std::nullopt;
            }
            options.cull_mode_present = true;
            if (*narrow == "none") {
                options.cull_mode =
                    hlclient::client::PreviewWorldCullMode::none;
            } else if (*narrow == "back") {
                options.cull_mode =
                    hlclient::client::PreviewWorldCullMode::back;
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
           "--camera <static|orbit|spawn> "
           "[--visibility <all|frustum|pvs|pvs-frustum>] "
           "[--brush-submodels <off|static>] [--cull <none|back>]\n";
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
        std::cerr << "lightmap-import=" << code;
        if (imported_lightmaps.error &&
            imported_lightmaps.error->surface_index) {
            std::cerr << ";surface="
                      << *imported_lightmaps.error->surface_index;
        }
        if (imported_lightmaps.error && imported_lightmaps.error->extent_code) {
            std::cerr << ";extent="
                      << hlclient::goldsrc::lightmaps::to_string(
                             *imported_lightmaps.error->extent_code);
        }
        if (imported_lightmaps.error &&
            imported_lightmaps.error->surface_index &&
            imported_lightmaps.error->extent_code ==
                hlclient::goldsrc::lightmaps::GoldSrcLightmapExtentErrorCode::
                    sample_limit_exceeded &&
            *imported_lightmaps.error->surface_index < world.surfaces.size()) {
            const auto measured =
                hlclient::goldsrc::lightmaps::calculate_goldsrc_lightmap_extents(
                    world,
                    world.surfaces[*imported_lightmaps.error->surface_index],
                    hlclient::goldsrc::lightmaps::
                        kGoldSrcLightmapHardMaximumSamplesPerSurface);
            if (measured && measured.extents) {
                std::cerr << ";sample-width=" << measured.extents->sample_width
                          << ";sample-height="
                          << measured.extents->sample_height;
            }
        }
        std::cerr << '\n';
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

struct PreparedWorldScene {
    std::shared_ptr<
        const hlclient::world_scene_render::WorldSceneRenderPackage>
        package;
    std::optional<
        hlclient::world_preview::WorldPreviewSpawnCameraDescriptor>
        spawn_camera;
    hlclient::goldsrc::brush_models::GoldSrcWorldSceneBuildStatistics
        build_statistics{};
};

[[nodiscard]] std::optional<PreparedWorldScene> build_world_scene(
    const hlclient::goldsrc::bsp::GoldSrcBspParsedDocument& document,
    std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
        world_package,
    const std::span<const std::byte> retained_bsp_source,
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment>& environment,
    const Options& options)
{
    namespace brush = hlclient::goldsrc::brush_models;
    std::optional<hlclient::world_scene_render::BrushSubmodelRenderLibrary>
        brush_library;
    if (options.brush_submodels == hlclient::world_preview::
            WorldPreviewBrushSubmodelsMode::static_instances) {
        auto built_library = brush::GoldSrcBrushRenderLibraryBuilder::build(
            document,
            retained_bsp_source,
            environment);
        if (!built_library || !built_library.library) {
            const auto code = built_library.error
                ? brush::to_string(built_library.error->code)
                : std::string_view{"brush_library_build_failed"};
            std::cerr << "brush-library=" << code << '\n';
            return std::nullopt;
        }
        brush_library.emplace(std::move(*built_library.library));
    }

    const brush::GoldSrcWorldSceneBuildConfig build_config{
        options.brush_submodels == hlclient::world_preview::
                WorldPreviewBrushSubmodelsMode::static_instances
            ? brush::GoldSrcWorldSceneBrushMode::static_initial
            : brush::GoldSrcWorldSceneBrushMode::off,
        *options.camera_mode ==
            hlclient::world_preview::WorldPreviewCameraMode::spawn,
    };
    auto built_scene = brush::GoldSrcWorldSceneBuilder::build(
        document,
        std::move(world_package),
        std::move(brush_library),
        build_config);
    if (!built_scene || !built_scene.scene_package) {
        const auto code = built_scene.error
            ? brush::to_string(built_scene.error->code)
            : std::string_view{"world_scene_build_failed"};
        std::cerr << "world-scene=" << code << '\n';
        return std::nullopt;
    }

    std::optional<
        hlclient::world_preview::WorldPreviewSpawnCameraDescriptor>
        spawn_camera;
    if (built_scene.spawn_camera && built_scene.spawn_camera->descriptor) {
        const auto& source = *built_scene.spawn_camera->descriptor;
        spawn_camera =
            hlclient::world_preview::WorldPreviewSpawnCameraDescriptor{
                source.position,
                source.forward,
                source.up,
            };
    }
    auto scene_package = std::make_shared<
        const hlclient::world_scene_render::WorldSceneRenderPackage>(
        std::move(*built_scene.scene_package));
    return PreparedWorldScene{
        std::move(scene_package),
        std::move(spawn_camera),
        built_scene.statistics,
    };
}

[[nodiscard]] std::optional<std::uint64_t> count_non_clear_pixels(
    const hlclient::renderer::RenderExtent extent,
    const hlclient::renderer::ClearColor clear_color)
{
    if (extent.width <= 0 || extent.height <= 0) {
        return std::nullopt;
    }
    const auto width = static_cast<std::size_t>(extent.width);
    const auto height = static_cast<std::size_t>(extent.height);
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        return std::nullopt;
    }
    const auto pixel_count = width * height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> pixels(pixel_count * 4U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0,
        0,
        extent.width,
        extent.height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        return std::nullopt;
    }

    const auto channel = [](const float value) {
        return static_cast<int>(std::lround(
            static_cast<double>(std::clamp(value, 0.0F, 1.0F)) * 255.0));
    };
    const std::array expected{
        channel(clear_color.red),
        channel(clear_color.green),
        channel(clear_color.blue),
        channel(clear_color.alpha),
    };
    std::uint64_t non_clear = 0U;
    for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
        bool differs = false;
        for (std::size_t component = 0U; component < 4U; ++component) {
            if (std::abs(static_cast<int>(pixels[pixel * 4U + component]) -
                    expected[component]) > 2) {
                differs = true;
                break;
            }
        }
        if (differs) {
            ++non_clear;
        }
    }
    return non_clear;
}

[[nodiscard]] int render_world(
    PreparedWorldScene prepared,
    const Options& options,
    const std::optional<std::uint64_t> frame_limit)
{
    hlclient::world_preview::WorldPreviewSceneOptions scene_options;
    scene_options.camera_mode = *options.camera_mode;
    scene_options.cull_mode = options.cull_mode;
    scene_options.visibility_mode = options.visibility_mode;
    scene_options.brush_submodels = options.brush_submodels;
    scene_options.spawn_camera = prepared.spawn_camera;
    hlclient::world_preview::WorldPreviewSceneSource scene_source{
        prepared.package,
        scene_options};
    const auto& package = prepared.package->world_package();
    if (!package) {
        std::cerr << "viewer-runtime=world_package_unavailable\n";
        return 1;
    }

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
    std::optional<std::uint64_t> non_clear_pixel_count;
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
        current_extent = window.pixel_extent();
        const auto extent_update = scene_source.set_render_extent(
            hlclient::renderer::RenderExtent{
                current_extent.width,
                current_extent.height,
            });
        if (!extent_update) {
            std::cerr << "scene-extent-update=failed\n";
            return 1;
        }
        const auto updated = scene_source.update(current_time - previous_time);
        if (!updated) {
            std::cerr << "scene-update=failed\n";
            return 1;
        }
        previous_time = current_time;
        const auto render_scene =
            hlclient::client::build_render_scene(scene_source.world_state());
        const auto render_extent = hlclient::renderer::RenderExtent{
            current_extent.width,
            current_extent.height,
        };
        renderer.render(render_scene, render_extent);
        if (frame_limit) {
            non_clear_pixel_count =
                count_non_clear_pixels(render_extent, render_scene.clear_color);
            if (!non_clear_pixel_count) {
                std::cerr << "framebuffer-proof=failed\n";
                return 1;
            }
        }
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
    const auto& scene_statistics = prepared.package->statistics();
    const auto& spatial_statistics =
        prepared.package->spatial_package().statistics();
    const auto& final_visibility =
        scene_source.world_state().world_visibility();
    const auto& final_draw_list =
        scene_source.world_state().visible_draw_list();

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
    std::cout << "spatial-nodes=" << spatial_statistics.node_count << '\n';
    std::cout << "spatial-leaves=" << spatial_statistics.leaf_count << '\n';
    std::cout << "visibility-mode="
              << hlclient::world_visibility::to_string(options.visibility_mode)
              << '\n';
    std::cout << "cull-mode="
              << (options.cull_mode ==
                          hlclient::client::PreviewWorldCullMode::back
                      ? "back"
                      : "none")
              << '\n';
    if (final_visibility) {
        const auto& visibility_statistics = final_visibility->statistics();
        std::cout << "visibility-applied="
                  << hlclient::world_visibility::to_string(
                         final_visibility->applied_mode())
                  << '\n';
        std::cout << "pvs-fallback="
                  << hlclient::world_visibility::to_string(
                         final_visibility->fallback_reason())
                  << '\n';
        const auto pvs_requested =
            options.visibility_mode == hlclient::world_visibility::
                WorldVisibilityMode::pvs_only ||
            options.visibility_mode == hlclient::world_visibility::
                WorldVisibilityMode::pvs_and_frustum;
        std::cout << "pvs-row-available="
                  << (pvs_requested &&
                          final_visibility->fallback_reason() ==
                              hlclient::world_visibility::
                                  WorldPvsFallbackReason::none
                          ? 1
                          : 0)
                  << '\n';
        if (final_visibility->camera_leaf_index()) {
            std::cout << "camera-leaf="
                      << *final_visibility->camera_leaf_index() << '\n';
        } else {
            std::cout << "camera-leaf=unavailable\n";
        }
        std::cout << "visible-world-surfaces="
                  << visibility_statistics.visible_world_surface_count << '/'
                  << visibility_statistics.total_world_surface_count << '\n';
        std::cout << "pvs-culled-world-surfaces="
                  << visibility_statistics.world_surface_culled_by_pvs_count
                  << '\n';
        std::cout << "frustum-culled-world-surfaces="
                  << visibility_statistics
                         .world_surface_culled_by_frustum_count
                  << '\n';
        std::cout << "visible-brush-instances="
                  << visibility_statistics.visible_brush_instance_count << '/'
                  << visibility_statistics.total_brush_instance_count << '\n';
    }
    std::cout << "brush-models=" << scene_statistics.brush_model_count << '\n';
    std::cout << "brush-instances=" << scene_statistics.brush_instance_count
              << '\n';
    std::cout << "brush-supported="
              << scene_statistics.supported_brush_instance_count << '\n';
    std::cout << "brush-unsupported="
              << scene_statistics.unsupported_brush_instance_count << '\n';
    std::cout << "entity-document-parses="
              << prepared.build_statistics.entity_document_parse_count << '\n';
    std::cout << "spawn-camera-applied="
              << (scene_source.spawn_camera_applied() ? 1 : 0) << '\n';
    std::cout << "world-upload-count=" << renderer_statistics.upload_count
              << '\n';
    std::cout << "scene-upload-count=" << renderer_statistics.scene_upload_count
              << '\n';
    std::cout << "brush-upload-count=" << renderer_statistics.brush_upload_count
              << '\n';
    std::cout << "visibility-updates="
              << renderer_statistics.visibility_update_count << '\n';
    std::cout << "rendered-frames="
              << renderer_statistics.rendered_frame_count << '\n';
    std::cout << "draw-calls=" << renderer_statistics.draw_call_count << '\n';
    std::cout << "brush-draw-calls="
              << renderer_statistics.brush_draw_call_count << '\n';
    std::cout << "rendered-commands="
              << (final_draw_list
                      ? final_draw_list->statistics().command_count
                      : 0U)
              << '\n';
    std::cout << "triangles=" << renderer_statistics.triangle_count << '\n';
    if (non_clear_pixel_count) {
        std::cout << "non-clear-pixels=" << *non_clear_pixel_count << '\n';
    }
    std::cout << "gl-error=none\n";
    std::cout << "vsync-enabled=" << (window.vsync_enabled() ? 1 : 0) << '\n';
    std::cout << "network-operations=0\n";
    std::cout << "writes-performed=0\n";

    if (frame_limit && rendered_frames != *frame_limit) {
        std::cerr << "viewer-runtime=frame_limit_not_reached\n";
        return 1;
    }
    if (rendered_frames > 0U &&
        (renderer_statistics.upload_count != 1U ||
            renderer_statistics.scene_upload_count != 1U ||
            renderer_statistics.rendered_frame_count != rendered_frames ||
            !renderer_statistics.scene_present ||
            (frame_limit &&
                (!non_clear_pixel_count || *non_clear_pixel_count == 0U)) ||
            (options.visibility_mode ==
                    hlclient::world_visibility::WorldVisibilityMode::all &&
                (renderer_statistics.draw_call_count == 0U ||
                    renderer_statistics.triangle_count == 0U)))) {
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
    resolved.file.reset();

    auto prepared = [&]() -> std::optional<PreparedWorldScene> {
        auto source = open_map_source(environment, *locator.locator);
        if (!source) {
            return std::nullopt;
        }
        const auto retained_bsp_source = source->source().bytes();
        auto parsed = hlclient::goldsrc::bsp::GoldSrcBspParser::parse(
            retained_bsp_source,
            {},
            hlclient::goldsrc::bsp::GoldSrcBspParseOptions{
                options->brush_submodels == hlclient::world_preview::
                    WorldPreviewBrushSubmodelsMode::static_instances});
        if (!parsed || !parsed.document) {
            const auto code = parsed.error
                ? hlclient::goldsrc::bsp::to_string(parsed.error->code)
                : std::string_view{"bsp_parse_failed"};
            std::cerr << "bsp-import=" << code << '\n';
            return std::nullopt;
        }
        auto document = std::move(*parsed.document);
        // The byte parser has no path input. Attach only the already-validated
        // virtual resource identity; never retain or expose a native path.
        document.world_asset.identity.source_name = *options->virtual_map;
        auto textures = import_complete_textures(
            document.world_asset,
            retained_bsp_source,
            environment);
        if (!textures) {
            return std::nullopt;
        }

        // Keep the canonical document intact for spatial/model association;
        // the M4.3 render package owns an exact copy of world model zero.
        auto world_package = build_world_render_package(
            document.world_asset,
            std::move(*textures),
            retained_bsp_source);
        if (!world_package) {
            return std::nullopt;
        }
        return build_world_scene(
            document,
            std::move(world_package),
            retained_bsp_source,
            environment,
            *options);
    }();

    // The returned scene owns only renderer-neutral immutable packages. The
    // lambda has destroyed the canonical BSP document, source span and opened
    // LocalAssetSource; release the resolver environment before SDL/OpenGL.
    environment.reset();
    if (!prepared) {
        return 1;
    }
    return render_world(
        std::move(*prepared),
        *options,
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
