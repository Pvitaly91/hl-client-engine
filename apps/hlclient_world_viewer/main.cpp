#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/gameplay_camera/render_camera_adapter.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_player/local_player_movement_controller.hpp>
#include <hlclient/local_player/player_walk_failure_latch.hpp>
#include <hlclient/local_player/local_player_spawn_selector.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/input/input_source.hpp>
#include <hlclient/interactive_preview/interactive_preview_controller.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <bit>
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
#include <variant>
#include <vector>

namespace {

enum class MovementDiagnosticsMode : std::uint8_t {
    off,
    summary,
};

enum class SmokeTestInputProfile : std::uint8_t {
    player_wall_contact_v1,
};

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
    MovementDiagnosticsMode movement_diagnostics{
        MovementDiagnosticsMode::off};
    bool movement_diagnostics_present{false};
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
            argument != L"--brush-submodels" && argument != L"--cull" &&
            argument != L"--movement-diagnostics") {
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
            } else if (*narrow == "free-fly") {
                options.camera_mode =
                    hlclient::world_preview::WorldPreviewCameraMode::free_flight;
            } else if (*narrow == "player-walk") {
                options.camera_mode =
                    hlclient::world_preview::WorldPreviewCameraMode::player_walk;
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
        } else if (argument == L"--cull") {
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
        } else {
            if (options.movement_diagnostics_present) {
                return std::nullopt;
            }
            options.movement_diagnostics_present = true;
            if (*narrow == "off") {
                options.movement_diagnostics = MovementDiagnosticsMode::off;
            } else if (*narrow == "summary") {
                options.movement_diagnostics =
                    MovementDiagnosticsMode::summary;
            } else {
                return std::nullopt;
            }
        }
    }
    if (!options.base_directory || !options.game_directory ||
        !options.virtual_map || !options.camera_mode) {
        return std::nullopt;
    }
    if (options.movement_diagnostics == MovementDiagnosticsMode::summary &&
        *options.camera_mode !=
            hlclient::world_preview::WorldPreviewCameraMode::player_walk) {
        return std::nullopt;
    }
    return options;
}

void print_usage()
{
    std::cerr
        << "Usage: hlclient_world_viewer --basedir <Half-Life root> "
           "--game <directory> --map <maps/name.bsp> "
           "--camera <static|orbit|spawn|free-fly|player-walk> "
           "[--visibility <all|frustum|pvs|pvs-frustum>] "
           "[--brush-submodels <off|static>] [--cull <none|back>] "
           "[--movement-diagnostics <off|summary>]\n"
        << "  free-fly: local noclip-style diagnostic camera; no collision, "
           "physics, prediction, or network commands\n"
        << "  player-walk: click captures, Escape releases, WASD walks, "
           "Space jumps, Ctrl ducks, and the mouse looks; local world-only "
           "collision, no prediction or network commands\n";
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

[[nodiscard]] std::optional<SmokeTestInputProfile> smoke_test_input_profile()
{
#if defined(_MSC_VER)
    char* environment_value = nullptr;
    std::size_t environment_value_size = 0U;
    const auto environment_result = ::_dupenv_s(
        &environment_value,
        &environment_value_size,
        "HLCLIENT_SMOKE_TEST_INPUT");
    if (environment_result != 0) {
        throw std::runtime_error{"Unable to read the smoke-input environment"};
    }
    if (environment_value == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned_environment_value{
        environment_value,
        &std::free};
    const std::string_view text{environment_value};
#else
    const char* environment_value = std::getenv("HLCLIENT_SMOKE_TEST_INPUT");
    if (environment_value == nullptr) {
        return std::nullopt;
    }
    const std::string_view text{environment_value};
#endif
    if (text != "player-wall-contact-v1") {
        throw std::invalid_argument{"Invalid smoke-input environment"};
    }
    return SmokeTestInputProfile::player_wall_contact_v1;
}

[[nodiscard]] bool scripted_wall_smoke_requested() noexcept
{
#if defined(_MSC_VER)
    char* input = nullptr;
    char* frames = nullptr;
    std::size_t input_size = 0U;
    std::size_t frames_size = 0U;
    if (::_dupenv_s(&input, &input_size, "HLCLIENT_SMOKE_TEST_INPUT") != 0 ||
        ::_dupenv_s(&frames, &frames_size, "HLCLIENT_SMOKE_TEST_FRAMES") !=
            0) {
        std::free(input);
        std::free(frames);
        return false;
    }
#else
    const char* input = std::getenv("HLCLIENT_SMOKE_TEST_INPUT");
    const char* frames = std::getenv("HLCLIENT_SMOKE_TEST_FRAMES");
#endif
    if (input == nullptr || frames == nullptr ||
        std::string_view{input} != "player-wall-contact-v1") {
#if defined(_MSC_VER)
        std::free(input);
        std::free(frames);
#endif
        return false;
    }
    std::uint64_t parsed_frames = 0U;
    const std::string_view text{frames};
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), parsed_frames, 10);
    const bool requested = parsed.ec == std::errc{} &&
        parsed.ptr == text.data() + text.size() &&
        parsed_frames > 0U;
#if defined(_MSC_VER)
    std::free(input);
    std::free(frames);
#endif
    return requested;
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
    struct PreparedLocalPlayer {
        std::shared_ptr<
            const hlclient::collision::CollisionWorldPackage>
            collision_world;
        hlclient::local_player::LocalPlayerSpawnDescriptor spawn;
        hlclient::movement::LocalPlayerMovementState initial_state;
        hlclient::goldsrc::movement::GoldSrcMovementEnvironment environment;
        hlclient::local_player::LocalPlayerSpawnSelectionStatistics
            spawn_statistics{};
    };
    std::optional<PreparedLocalPlayer> local_player;
};

[[nodiscard]] std::optional<PreparedWorldScene::PreparedLocalPlayer>
prepare_local_player(
    const hlclient::goldsrc::bsp::GoldSrcBspParsedDocument& document)
{
    namespace goldsrc_collision = hlclient::goldsrc::collision;
    namespace goldsrc_movement = hlclient::goldsrc::movement;

    auto built_collision =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(document);
    if (!built_collision || !built_collision.package) {
        const auto code = built_collision.error
            ? goldsrc_collision::to_string(built_collision.error->code)
            : std::string_view{"collision_world_build_failed"};
        std::cerr << "movement-collision-world=" << code << '\n';
        return std::nullopt;
    }

    auto parsed_entities =
        hlclient::goldsrc::bsp::GoldSrcEntityDocumentParser::parse(
            document.entity_lump_bytes);
    if (!parsed_entities || !parsed_entities.document) {
        const auto code = parsed_entities.error
            ? hlclient::goldsrc::bsp::to_string(parsed_entities.error->code)
            : std::string_view{"entity_document_parse_failed"};
        std::cerr << "movement-entity-document=" << code << '\n';
        return std::nullopt;
    }

    goldsrc_movement::WorldOnlyMovementCollision collision{
        built_collision.package};
    hlclient::collision::CollisionQueryScratch collision_scratch;
    auto selected = hlclient::local_player::LocalPlayerSpawnSelector::select(
        *parsed_entities.document,
        collision,
        collision_scratch);
    if (!selected || !selected.descriptor) {
        const auto code = selected.error
            ? hlclient::local_player::to_string(selected.error->code)
            : std::string_view{"no_valid_local_player_spawn"};
        std::cerr << "movement-spawn=" << code << '\n';
        return std::nullopt;
    }

    auto built_environment = goldsrc_movement::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    if (!built_environment || !built_environment.environment) {
        const auto code = built_environment.error
            ? goldsrc_movement::to_string(built_environment.error->code)
            : std::string_view{"movement_environment_build_failed"};
        std::cerr << "movement-environment=" << code << '\n';
        return std::nullopt;
    }

    hlclient::movement::LocalPlayerMovementStateCreateInfo state_info;
    state_info.origin = selected.descriptor->origin;
    state_info.view_angles = selected.descriptor->view_angles_degrees;
    state_info.hull = hlclient::movement::PlayerMovementHull::standing;
    state_info.mode = hlclient::movement::PlayerMovementMode::airborne;
    state_info.ground.contact_position = selected.descriptor->origin;
    state_info.source_command_sequence = 0U;
    auto created_state =
        hlclient::movement::LocalPlayerMovementState::create(state_info);
    if (!created_state || !created_state.state) {
        const auto code = created_state.error
            ? hlclient::movement::to_string(created_state.error->code)
            : std::string_view{"movement_state_build_failed"};
        std::cerr << "movement-state=" << code << '\n';
        return std::nullopt;
    }

    return PreparedWorldScene::PreparedLocalPlayer{
        std::move(built_collision.package),
        std::move(*selected.descriptor),
        std::move(*created_state.state),
        std::move(*built_environment.environment),
        selected.statistics,
    };
}

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

[[nodiscard]] std::optional<hlclient::client::RenderCameraState>
build_client_player_camera(
    const hlclient::gameplay_camera::GameplayCameraState& camera) noexcept
{
    auto built = hlclient::gameplay_camera::build_render_camera(camera);
    if (!built || !built.camera ||
        !hlclient::renderer::is_valid(*built.camera)) {
        return std::nullopt;
    }
    return hlclient::client::RenderCameraState{
        built.camera->position,
        built.camera->target,
        built.camera->up,
        built.camera->vertical_field_of_view_radians,
        built.camera->near_plane,
        built.camera->far_plane,
    };
}

struct ViewerMovementStatistics {
    std::uint64_t command_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t collision_count{0U};
    std::uint64_t step_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
};

[[nodiscard]] bool checked_add(
    std::uint64_t& destination,
    const std::uint64_t value) noexcept
{
    if (destination > UINT64_MAX - value) {
        return false;
    }
    destination += value;
    return true;
}

[[nodiscard]] bool accumulate_movement_statistics(
    ViewerMovementStatistics& destination,
    const hlclient::local_player::LocalPlayerMovementControllerUpdateResult&
        update) noexcept
{
    auto staged = destination;
    if (!checked_add(staged.command_count,
            static_cast<std::uint64_t>(update.generated_command_count)) ||
        !checked_add(staged.jump_count, update.statistics.jump_count) ||
        !checked_add(
            staged.collision_count, update.statistics.collision_hit_count) ||
        !checked_add(staged.step_count, update.statistics.step_success_count) ||
        !checked_add(
            staged.start_solid_count, update.statistics.start_solid_count) ||
        !checked_add(
            staged.all_solid_count, update.statistics.all_solid_count)) {
        return false;
    }
    destination = staged;
    return true;
}

template<class ErrorCode, class ToString>
[[nodiscard]] std::string_view optional_error_label(
    const std::optional<ErrorCode>& code,
    ToString&& to_string_function) noexcept
{
    return code ? to_string_function(*code) : std::string_view{"none"};
}

[[nodiscard]] std::string_view scheduler_error_label(
    const hlclient::goldsrc::GoldSrcUserCmdSchedulerErrorCode code) noexcept
{
    using Code = hlclient::goldsrc::GoldSrcUserCmdSchedulerErrorCode;
    switch (code) {
    case Code::invalid_configuration: return "invalid_configuration";
    case Code::stock_evidence_pending: return "stock_evidence_pending";
    case Code::time_moved_backwards: return "time_moved_backwards";
    case Code::time_overflow: return "time_overflow";
    case Code::lag_limit_exceeded: return "lag_limit_exceeded";
    case Code::sequence_exhausted: return "sequence_exhausted";
    case Code::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

[[nodiscard]] std::string_view command_error_label(
    const hlclient::goldsrc::GoldSrcUserCmdInputAdapterErrorCode code) noexcept
{
    using Code = hlclient::goldsrc::GoldSrcUserCmdInputAdapterErrorCode;
    switch (code) {
    case Code::invalid_context: return "invalid_context";
    case Code::unsupported_profile: return "unsupported_profile";
    case Code::stock_evidence_pending: return "stock_evidence_pending";
    case Code::unsupported_action: return "unsupported_action";
    case Code::unsupported_weapon_selection:
        return "unsupported_weapon_selection";
    case Code::state_validation_failed: return "state_validation_failed";
    }
    return "unknown";
}

void log_player_walk_failure(
    const hlclient::local_player::PlayerWalkFailureSummary& summary,
    const MovementDiagnosticsMode diagnostics)
{
    std::cerr
        << "[movement] result=failure_latched controller="
        << optional_error_label(summary.controller_error,
               [](const auto code) {
                   return hlclient::local_player::to_string(code);
               })
        << " scheduler="
        << optional_error_label(summary.scheduler_error,
               [](const auto code) {
                   return scheduler_error_label(code);
               })
        << " command="
        << optional_error_label(summary.command_error,
               [](const auto code) {
                   return command_error_label(code);
               })
        << " movement="
        << optional_error_label(summary.movement_error,
               [](const auto code) {
                   return hlclient::goldsrc::movement::to_string(code);
               })
        << " collision="
        << optional_error_label(summary.collision_error,
               [](const auto code) {
                   return hlclient::goldsrc::movement::to_string(code);
               })
        << " action=simulation_disabled rendering=continued\n";
    if (diagnostics == MovementDiagnosticsMode::summary) {
        std::cerr << "[movement-diagnostics] frame="
                  << summary.context.frame_ordinal
                  << " last-command="
                  << summary.context.last_valid_command_sequence
                  << " state-signature="
                  << summary.context.last_valid_state_signature
                  << " camera-revision="
                  << summary.context.last_valid_camera_revision
                  << " visibility-revision="
                  << summary.context.last_valid_visibility_revision
                  << " capture-active="
                  << (summary.context.mouse_capture_active ? "true" : "false")
                  << '\n';
        if (summary.context.movement_diagnostic) {
            const auto& movement = *summary.context.movement_diagnostic;
            const auto fraction_label = [](const auto value) {
                using Type = decltype(value);
                switch (value) {
                case Type::none: return "none";
                case Type::zero: return "zero";
                case Type::near_zero: return "near_zero";
                case Type::partial: return "partial";
                case Type::complete: return "complete";
                }
                return "unknown";
            };
            const auto result_label = [](const auto value) {
                using Type = decltype(value);
                switch (value) {
                case Type::none: return "none";
                case Type::progressing: return "progressing";
                case Type::stable_stop: return "stable_stop";
                case Type::collision_failure: return "collision_failure";
                case Type::movement_failure: return "movement_failure";
                case Type::success: return "success";
                }
                return "unknown";
            };
            std::cerr << "[movement-diagnostics] bump="
                      << movement.slide_bump_ordinal
                      << " planes=" << movement.clip_plane_count
                      << " distinct-planes=" << movement.distinct_plane_count
                      << " fraction-class="
                      << fraction_label(movement.fraction_class)
                      << " startsolid="
                      << (movement.start_solid ? "true" : "false")
                      << " allsolid="
                      << (movement.all_solid ? "true" : "false")
                      << " exit-classification="
                      << result_label(movement.result) << '\n';
        } else {
            std::cerr << "[movement-diagnostics] bump=unavailable"
                         " planes=unavailable distinct-planes=unavailable"
                         " fraction-class=unavailable startsolid=unavailable"
                         " allsolid=unavailable exit-classification=unavailable\n";
        }
    }
}

struct SelectedSmokeWall final {
    hlclient::movement::PlayerMovementPlane plane{};
    hlclient::movement::PlayerMovementHitIdentity hit{};
};

[[nodiscard]] bool same_selected_wall_plane(
    const hlclient::movement::PlayerMovementPlane& left,
    const hlclient::movement::PlayerMovementPlane& right) noexcept
{
    // Both records originate from the same immutable collision package.  An
    // exact comparison therefore identifies one source plane without a
    // geometry-wide epsilon that could merge nearby parallel walls.
    return left == right;
}

struct SmokeWallCampaign final {
    hlclient::input::ScriptedInputSource::Script frames;
    std::size_t direction_ordinal{0U};
    std::uint32_t source_plane_index{UINT32_MAX};
    SelectedSmokeWall selected_wall{};
};

[[nodiscard]] std::optional<SmokeWallCampaign> build_smoke_wall_campaign(
    const std::uint64_t frames,
    const hlclient::local_player::LocalPlayerMovementController& controller,
    const hlclient::goldsrc::movement::ILocalMovementCollision& collision,
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch& scratch)
{
    constexpr float radial_distance = 2'048.0F;
    constexpr float raised_confirmation_margin = 1.0F;
    constexpr std::size_t direction_count = 32U;
    constexpr double degrees_per_direction = 360.0 / direction_count;
    constexpr std::uint64_t minimum_campaign_frames = 1'000U;
    const auto step_size = controller.environment().step_size();
    const auto raised_confirmation = step_size + raised_confirmation_margin;
    if (frames < minimum_campaign_frames ||
        frames >
            hlclient::input::ScriptedInputSourceLimits::hard_maximum_frames ||
        !std::isfinite(step_size) || step_size <= 0.0F ||
        !std::isfinite(raised_confirmation) ||
        raised_confirmation <= step_size) {
        return std::nullopt;
    }
    const auto& state = controller.player_state();
    std::optional<std::size_t> selected;
    double selected_distance = std::numeric_limits<double>::infinity();
    std::uint32_t selected_plane = UINT32_MAX;
    int selected_mouse_x = 0;
    std::optional<SelectedSmokeWall> selected_wall;
    for (std::size_t ordinal = 0U; ordinal < direction_count; ++ordinal) {
        auto relative_degrees = static_cast<double>(ordinal) *
            degrees_per_direction;
        if (relative_degrees > 180.0) {
            relative_degrees -= 360.0;
        }
        const auto mouse_x = static_cast<int>(std::lround(
            -relative_degrees / 0.1));
        const auto tested_yaw = state.view_angles().y -
            static_cast<float>(mouse_x) * 0.1F;
        const auto wish = hlclient::goldsrc::movement::yaw_only_wish_direction(
            tested_yaw, 1.0F, 0.0F, 1.0F);
        if (!wish || !wish.wish) {
            return std::nullopt;
        }
        const auto& direction = wish.wish->direction;
        const hlclient::assets::AssetVector3 end{
            state.origin().x + direction.x * radial_distance,
            state.origin().y + direction.y * radial_distance,
            state.origin().z,
        };
        const auto trace_at = [&](const float raised) {
            auto start = state.origin();
            start.z += raised;
            auto raised_end = end;
            raised_end.z += raised;
            return collision.trace_hull(
                start,
                raised_end,
                hlclient::movement::PlayerMovementHull::standing,
                scratch.collision,
                controller.config().movement.collision_query);
        };
        const auto traced = trace_at(0.0F);
        const auto raised = trace_at(raised_confirmation);
        if (!traced || !traced.result || !raised || !raised.result) {
            return std::nullopt;
        }
        const auto valid_vertical_hit = [](const auto& trace) {
            return !trace.start_solid && !trace.all_solid &&
                trace.fraction > 0.0 && trace.fraction < 1.0 &&
                trace.collision_plane && trace.hit &&
                std::isfinite(trace.collision_plane->normal.x) &&
                std::isfinite(trace.collision_plane->normal.y) &&
                std::isfinite(trace.collision_plane->normal.z) &&
                std::abs(trace.collision_plane->normal.z) <= 0.2F;
        };
        const auto& trace = *traced.result;
        const auto& raised_trace = *raised.result;
        if (!valid_vertical_hit(trace) || !valid_vertical_hit(raised_trace)) {
            continue;
        }
        const auto source_plane = trace.collision_plane->source_plane_index.
            value_or(UINT32_MAX);
        if (*raised_trace.hit != *trace.hit ||
            !same_selected_wall_plane(
                *raised_trace.collision_plane, *trace.collision_plane)) {
            continue;
        }
        const auto distance = trace.fraction * radial_distance;
        const bool preferred = !selected || distance < selected_distance ||
            (distance == selected_distance && ordinal < *selected) ||
            (distance == selected_distance && ordinal == *selected &&
                source_plane < selected_plane);
        if (preferred) {
            selected = ordinal;
            selected_distance = distance;
            selected_plane = source_plane;
            selected_mouse_x = mouse_x;
            selected_wall = SelectedSmokeWall{
                *trace.collision_plane, *trace.hit};
        }
    }
    if (!selected || !selected_wall) {
        return std::nullopt;
    }

    SmokeWallCampaign campaign;
    campaign.frames.resize(static_cast<std::size_t>(frames));
    campaign.direction_ordinal = *selected;
    campaign.source_plane_index = selected_plane;
    campaign.selected_wall = std::move(*selected_wall);
    auto& first = campaign.frames.front();
    first.push_back(hlclient::input::InputEvent::focus_gained());
    first.push_back(hlclient::input::InputEvent::capture_acquired());
    const auto first_mouse = std::clamp(selected_mouse_x, -900, 900);
    const auto second_mouse = selected_mouse_x - first_mouse;
    if (first_mouse != 0) {
        first.push_back(
            hlclient::input::InputEvent::mouse_motion(first_mouse, 0));
    }
    if (second_mouse != 0) {
        campaign.frames[1U].push_back(
            hlclient::input::InputEvent::mouse_motion(second_mouse, 0));
    }
    campaign.frames[2U].push_back(
        hlclient::input::InputEvent::key_pressed(
            hlclient::input::PhysicalKey::w));
    campaign.frames[350U].push_back(
        hlclient::input::InputEvent::key_pressed(
            hlclient::input::PhysicalKey::d));
    campaign.frames[550U].push_back(
        hlclient::input::InputEvent::key_released(
            hlclient::input::PhysicalKey::d));
    campaign.frames[600U].push_back(
        hlclient::input::InputEvent::key_released(
            hlclient::input::PhysicalKey::w));
    campaign.frames[700U].push_back(
        hlclient::input::InputEvent::key_pressed(
            hlclient::input::PhysicalKey::w));
    campaign.frames[700U].push_back(
        hlclient::input::InputEvent::key_pressed(
            hlclient::input::PhysicalKey::space));
    campaign.frames[701U].push_back(
        hlclient::input::InputEvent::key_released(
            hlclient::input::PhysicalKey::space));
    campaign.frames[820U].push_back(
        hlclient::input::InputEvent::key_pressed(
            hlclient::input::PhysicalKey::left_control));
    campaign.frames[920U].push_back(
        hlclient::input::InputEvent::key_released(
            hlclient::input::PhysicalKey::left_control));
    campaign.frames.back().push_back(
        hlclient::input::InputEvent::key_released(
            hlclient::input::PhysicalKey::w));
    return campaign;
}

[[nodiscard]] std::uint64_t movement_origin_hash(
    const hlclient::assets::AssetVector3& origin) noexcept
{
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    const std::array components{
        std::bit_cast<std::uint32_t>(origin.x),
        std::bit_cast<std::uint32_t>(origin.y),
        std::bit_cast<std::uint32_t>(origin.z),
    };
    for (const auto component : components) {
        for (std::size_t byte = 0U; byte < sizeof(component); ++byte) {
            hash ^= static_cast<std::uint8_t>(component >> (byte * 8U));
            hash *= 1'099'511'628'211ULL;
        }
    }
    return hash;
}

[[nodiscard]] int render_world(
    PreparedWorldScene prepared,
    const Options& options,
    const std::optional<std::uint64_t> frame_limit,
    const std::optional<SmokeTestInputProfile> smoke_input_profile)
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

    const bool player_walk_camera =
        *options.camera_mode ==
        hlclient::world_preview::WorldPreviewCameraMode::player_walk;
    std::optional<hlclient::goldsrc::movement::WorldOnlyMovementCollision>
        player_collision;
    std::optional<hlclient::local_player::LocalPlayerMovementController>
        player_controller;
    std::optional<hlclient::gameplay_input::GameplayInputBindings>
        player_bindings;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch movement_scratch;
    ViewerMovementStatistics movement_statistics;
    hlclient::local_player::PlayerWalkFailureLatch movement_failure_latch;
    std::optional<hlclient::input::ScriptedInputSource> scripted_input;
    std::optional<
        hlclient::local_player::LocalPlayerMovementCommittedTouchFilter>
        smoke_wall_touch_filter;
    std::uint64_t smoke_wall_contact_count = 0U;
    bool smoke_wall_found = false;
    if (player_walk_camera) {
        if (!prepared.local_player) {
            std::cerr << "movement-runtime=preparation_unavailable\n";
            return 1;
        }
        auto& local_player = *prepared.local_player;
        player_collision.emplace(local_player.collision_world);
        if (!player_collision->valid()) {
            std::cerr << "movement-runtime=collision_unavailable\n";
            return 1;
        }
        hlclient::local_player::LocalPlayerMovementControllerConfig
            controller_config;
        controller_config.scheduler.maximum_commands_per_update =
            hlclient::goldsrc::kMaximumUserCmdsPerSchedulerUpdate;
        player_controller.emplace(
            std::move(local_player.initial_state),
            std::move(local_player.environment),
            controller_config);
        if (!player_controller->valid_configuration()) {
            std::cerr << "movement-runtime=controller_initialization_failed\n";
            return 1;
        }
        auto built_bindings = hlclient::gameplay_input::GameplayInputBindings::
            project_default_v1();
        if (!built_bindings || !built_bindings.bindings) {
            std::cerr << "movement-runtime=input_bindings_unavailable\n";
            return 1;
        }
        player_bindings.emplace(std::move(*built_bindings.bindings));

        const auto initial_camera =
            build_client_player_camera(player_controller->camera());
        if (!initial_camera) {
            std::cerr << "movement-runtime=initial_camera_failed\n";
            return 1;
        }
        scene_source.publish_camera_seed(*initial_camera);
        const auto initial_visibility =
            scene_source.update(hlclient::client::FrameTime{0.0});
        if (!initial_visibility) {
            std::cerr << "movement-runtime=initial_visibility_failed\n";
            return 1;
        }
        std::cout << "[movement] collision=world-only\n";
        std::cout << "[movement] brush-solidity=stock-evidence-pending\n";
        if (smoke_input_profile) {
            auto campaign = build_smoke_wall_campaign(
                *frame_limit,
                *player_controller,
                *player_collision,
                movement_scratch);
            if (!campaign) {
                std::cerr << "[movement-smoke] wall-found=false\n";
                return 1;
            }
            smoke_wall_touch_filter.emplace(
                hlclient::local_player::
                    LocalPlayerMovementCommittedTouchFilter{
                        campaign->selected_wall.hit,
                        campaign->selected_wall.plane});
            const auto campaign_frame_count = campaign->frames.size();
            scripted_input.emplace(
                std::move(campaign->frames),
                hlclient::input::ScriptedInputSourceLimits{
                    campaign_frame_count, 16U, 64U});
            smoke_wall_found = true;
            std::cout << "[movement-smoke] wall-found=true direction="
                      << campaign->direction_ordinal << " source-plane="
                      << campaign->source_plane_index << '\n';
        }
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

    const bool interactive_camera =
        *options.camera_mode ==
        hlclient::world_preview::WorldPreviewCameraMode::free_flight;
    std::optional<hlclient::input::InputStateTracker> input_tracker;
    std::optional<
        hlclient::interactive_preview::InteractivePreviewController>
        interactive_controller;
    if (interactive_camera || player_walk_camera) {
        input_tracker.emplace();
    }
    if (interactive_camera) {
        auto created = hlclient::interactive_preview::
            InteractivePreviewController::create_project_default_v1(
                hlclient::interactive_preview::InteractivePreviewMode::
                    free_flight_world,
                scene_source.world_state().camera());
        if (!created || !created.controller) {
            std::cerr << "interactive-camera=initialization_failed\n";
            return 1;
        }
        interactive_controller.emplace(std::move(*created.controller));
        if (!interactive_controller->seed_world_state_camera(scene_source)) {
            std::cerr << "interactive-camera=initial-publication-failed\n";
            return 1;
        }
        const auto initial_visibility =
            scene_source.update(hlclient::client::FrameTime{0.0});
        if (!initial_visibility) {
            std::cerr << "interactive-camera=initial-visibility-failed\n";
            return 1;
        }
    }

    auto previous_time = std::chrono::steady_clock::now();
    auto current_extent = window.pixel_extent();
    std::uint64_t rendered_frames = 0U;
    std::optional<std::uint64_t> non_clear_pixel_count;
    std::uint64_t capture_failure_count = 0U;
    std::uint64_t input_event_count = 0U;
    std::uint64_t input_frame_count = 0U;
    bool input_focus_seeded = false;
    bool running = true;
    while (running) {
        if (input_tracker) {
            input_tracker->begin_frame();
            if (!input_focus_seeded) {
                if (!scripted_input) {
                    input_tracker->apply_event(
                        window.focus_state() ==
                                hlclient::input::InputFocusState::focused
                            ? hlclient::input::InputEvent::focus_gained()
                            : hlclient::input::InputEvent::focus_lost());
                }
                input_focus_seeded = true;
            }
        }
        hlclient::platform::PlatformEvent event{
            hlclient::platform::WindowEvent{}};
        while (window.poll_event(event)) {
            if (const auto* window_event =
                    std::get_if<hlclient::platform::WindowEvent>(&event)) {
                if (window_event->type ==
                    hlclient::platform::WindowEventType::quit_requested) {
                    running = false;
                } else if (window_event->type == hlclient::platform::
                               WindowEventType::input_capture_recovery_failed) {
                    std::cerr << "input-capture-recovery=failed\n";
                    return 1;
                } else if (window_event->type == hlclient::platform::
                               WindowEventType::native_event_limit_exceeded) {
                    std::cerr << "native-event-limit=exceeded\n";
                    return 1;
                } else if (window_event->extent.width > 0 &&
                    window_event->extent.height > 0) {
                    current_extent = window_event->extent;
                }
            } else if (input_tracker && !scripted_input) {
                input_tracker->apply_event(
                    std::get<hlclient::input::InputEvent>(event));
                ++input_event_count;
            }
        }
        if (input_tracker && scripted_input) {
            scripted_input->begin_frame();
            hlclient::input::InputEvent scripted_event =
                hlclient::input::InputEvent::focus_lost();
            while (scripted_input->poll_event(scripted_event)) {
                input_tracker->apply_event(scripted_event);
                if (!checked_add(input_event_count, 1U)) {
                    std::cerr << "movement-runtime=input_counter_overflow\n";
                    return 1;
                }
            }
            scripted_input->end_frame();
        }
        if (input_tracker &&
            window.focus_state() ==
                hlclient::input::InputFocusState::unfocused) {
            const auto recovery =
                window.request_relative_mouse_capture(false);
            if (!recovery) {
                if (!checked_add(capture_failure_count, 1U)) {
                    std::cerr << "movement-runtime=capture_counter_overflow\n";
                    return 1;
                }
                std::cerr << "input-focus-release=failed diagnostic="
                          << recovery.diagnostic << '\n';
                return 1;
            }
        }
        std::optional<hlclient::input::InputSnapshot> input_snapshot;
        if (input_tracker) {
            input_snapshot.emplace(input_tracker->publish_snapshot());
            input_frame_count = input_tracker->published_frame_count();
            input_tracker->end_frame();
        }
        if (!running) {
            break;
        }

        const auto current_time = std::chrono::steady_clock::now();
        const auto elapsed = current_time - previous_time;
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
        if (interactive_controller && input_snapshot) {
            const auto bounded_input_elapsed = std::min(
                std::chrono::duration<double>{elapsed}.count(), 0.25);
            const auto camera_update = interactive_controller->update(
                *input_snapshot,
                bounded_input_elapsed,
                scene_source);
            if (!camera_update) {
                std::cerr << "interactive-camera="
                          << (camera_update.error
                                  ? hlclient::interactive_preview::to_string(
                                        camera_update.error->code)
                                  : std::string_view{"update_failed"})
                          << '\n';
                return 1;
            }
            if (camera_update.capture_mouse_requested) {
                const auto capture =
                    window.request_relative_mouse_capture(true);
                if (!capture) {
                    if (!checked_add(capture_failure_count, 1U)) {
                        std::cerr <<
                            "movement-runtime=capture_counter_overflow\n";
                        return 1;
                    }
                    std::cerr << "input-capture=failed diagnostic="
                              << capture.diagnostic << '\n';
                    (void)window.request_relative_mouse_capture(false);
                    return 1;
                }
            }
            if (camera_update.release_mouse_requested) {
                const auto release =
                    window.request_relative_mouse_capture(false);
                if (!release) {
                    if (!checked_add(capture_failure_count, 1U)) {
                        std::cerr <<
                            "movement-runtime=capture_counter_overflow\n";
                        return 1;
                    }
                    std::cerr << "input-release=failed diagnostic="
                              << release.diagnostic << '\n';
                    (void)window.request_relative_mouse_capture(false);
                    return 1;
                }
            }
        }
        if (player_controller && player_collision && player_bindings &&
            input_snapshot && movement_failure_latch.simulation_enabled()) {
            const auto bounded_input_elapsed = std::min(
                std::chrono::duration<double>{elapsed}.count(), 0.25);
            auto built_intent =
                hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
                    *input_snapshot,
                    *player_bindings,
                    player_controller->config().camera.mouse_look_config(),
                    bounded_input_elapsed);
            if (!built_intent || !built_intent.intent) {
                std::cerr << "movement-input="
                          << (built_intent.error
                                  ? hlclient::gameplay_input::to_string(
                                        built_intent.error->code)
                                  : std::string_view{"intent_build_failed"})
                          << '\n';
                return 1;
            }

            std::int64_t movement_time_nanoseconds = 0;
            if (frame_limit) {
                const auto interval =
                    player_controller->config().scheduler.
                        command_interval_nanoseconds;
                if (rendered_frames >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()) /
                        interval) {
                    std::cerr << "movement-runtime=smoke_time_overflow\n";
                    return 1;
                }
                movement_time_nanoseconds = static_cast<std::int64_t>(
                    rendered_frames * interval);
            } else {
                movement_time_nanoseconds =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        current_time.time_since_epoch())
                        .count();
            }

            auto movement_update = player_controller->update(
                movement_time_nanoseconds,
                *built_intent.intent,
                *player_collision,
                movement_scratch,
                smoke_wall_touch_filter ? &*smoke_wall_touch_filter : nullptr);
            if (!movement_update) {
                auto failure_diagnostic = movement_scratch.last_diagnostic;
                const auto last_camera_revision =
                    player_controller->camera().revision();
                const auto last_visibility_revision =
                    scene_source.world_state().visibility_revision();
                if (failure_diagnostic) {
                    hlclient::goldsrc::movement::
                        apply_player_movement_diagnostic_runtime_context(
                            *failure_diagnostic,
                            hlclient::goldsrc::movement::
                                PlayerMovementDiagnosticRuntimeContext{
                                    rendered_frames,
                                    movement_update.generated_command_count,
                                    last_camera_revision,
                                    last_visibility_revision});
                }
                const auto decision = movement_failure_latch.latch(
                    movement_update,
                    hlclient::local_player::PlayerWalkFailureContext{
                        rendered_frames,
                        player_controller->player_state().
                            source_command_sequence(),
                        hlclient::movement::
                            local_player_movement_state_signature(
                                player_controller->player_state()),
                        last_camera_revision,
                        last_visibility_revision,
                        window.capture_state() == hlclient::input::
                            InputCaptureState::captured,
                        failure_diagnostic,
                    });
                const auto* summary = movement_failure_latch.summary();
                if (!decision.newly_latched || !summary ||
                    !decision.keep_rendering) {
                    std::cerr << "movement-runtime=failure_policy_failed\n";
                    return 1;
                }
                log_player_walk_failure(
                    *summary, options.movement_diagnostics);
                if (decision.clear_input_requested) {
                    player_controller->discard_pending_input();
                    input_snapshot.reset();
                    input_tracker.reset();
                }
                if (decision.release_mouse_capture_requested) {
                    const auto release =
                        window.request_relative_mouse_capture(false);
                    if (!release) {
                        if (!checked_add(capture_failure_count, 1U)) {
                            std::cerr <<
                                "movement-runtime=capture_counter_overflow\n";
                            return 1;
                        }
                        std::cerr <<
                            "[movement] capture-release=degraded\n";
                        if (release.state == hlclient::input::
                                InputCaptureState::captured) {
                            std::cerr <<
                                "movement-runtime=capture_release_failed\n";
                            return 1;
                        }
                    }
                }
            } else {
                if (movement_update.committed_touch_match_count >
                    UINT64_MAX - smoke_wall_contact_count) {
                    std::cerr <<
                        "movement-runtime=wall_contact_counter_overflow\n";
                    return 1;
                }
                smoke_wall_contact_count +=
                    movement_update.committed_touch_match_count;
                if (!accumulate_movement_statistics(
                        movement_statistics, movement_update)) {
                    std::cerr << "movement-runtime=statistics_overflow\n";
                    return 1;
                }

                if (built_intent.intent->capture_mouse_requested()) {
                    const auto capture =
                        window.request_relative_mouse_capture(true);
                    if (!capture) {
                        if (!checked_add(capture_failure_count, 1U)) {
                            std::cerr <<
                                "movement-runtime=capture_counter_overflow\n";
                            return 1;
                        }
                        std::cerr << "input-capture=failed diagnostic="
                                  << capture.diagnostic << '\n';
                        (void)window.request_relative_mouse_capture(false);
                        return 1;
                    }
                }
                if (built_intent.intent->release_mouse_requested()) {
                    const auto release =
                        window.request_relative_mouse_capture(false);
                    if (!release) {
                        if (!checked_add(capture_failure_count, 1U)) {
                            std::cerr <<
                                "movement-runtime=capture_counter_overflow\n";
                            return 1;
                        }
                        std::cerr << "input-release=failed diagnostic="
                                  << release.diagnostic << '\n';
                        (void)window.request_relative_mouse_capture(false);
                        return 1;
                    }
                }

                const auto player_camera =
                    build_client_player_camera(player_controller->camera());
                if (!player_camera ||
                    !scene_source.publish_interactive_camera(
                        *player_camera,
                        hlclient::client::InteractiveCameraMetadata{
                            input_snapshot->sequence(),
                            player_controller->camera().revision(),
                            hlclient::client::InteractiveCameraMode::player_walk,
                            std::nullopt,
                            hlclient::client::ControlledEntityCameraStatus::
                                not_applicable,
                        })) {
                    std::cerr <<
                        "movement-runtime=camera_publication_failed\n";
                    return 1;
                }
            }
        }
        const auto updated = scene_source.update(elapsed);
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
        if (frame_limit &&
            (!smoke_input_profile || rendered_frames + 1U >= *frame_limit)) {
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
    const bool gl_error_none = glGetError() == GL_NO_ERROR;

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
    std::cout << "gl-error=" << (gl_error_none ? "none" : "present") << '\n';
    std::cout << "vsync-enabled=" << (window.vsync_enabled() ? 1 : 0) << '\n';
    std::cout << "network-operations=0\n";
    std::cout << "writes-performed=0\n";
    if (interactive_controller && input_tracker) {
        const auto& camera_statistics = interactive_controller->statistics();
        std::cout << "input-frames="
                  << input_tracker->published_frame_count() << '\n';
        std::cout << "input-events=" << input_event_count << '\n';
        std::cout << "camera-updates="
                  << camera_statistics.published_update_count << '\n';
        std::cout << "camera-changes="
                  << camera_statistics.changed_camera_count << '\n';
        std::cout << "capture-failures=" << capture_failure_count << '\n';
    }
    if (player_controller && player_walk_camera) {
        const auto& state = player_controller->player_state();
        std::cout << "input-frames=" << input_frame_count << '\n';
        std::cout << "input-events=" << input_event_count << '\n';
        std::cout << "capture-failures=" << capture_failure_count << '\n';
        std::cout << "[movement] profile="
                  << hlclient::movement::to_string(
                         state.compatibility_profile())
                  << '\n';
        std::cout << "[movement] commands="
                  << movement_statistics.command_count << '\n';
        std::cout << "[movement] origin-hash="
                  << movement_origin_hash(state.origin()) << '\n';
        std::cout << "[movement] grounded="
                  << (state.ground_state().grounded() ? "true" : "false")
                  << '\n';
        std::cout << "[movement] hull="
                  << hlclient::movement::to_string(state.hull()) << '\n';
        std::cout << "[movement] jumps=" << movement_statistics.jump_count
                  << '\n';
        std::cout << "[movement] collisions="
                  << movement_statistics.collision_count << '\n';
        std::cout << "[movement] steps=" << movement_statistics.step_count
                  << '\n';
        std::cout << "[movement] startsolid="
                  << movement_statistics.start_solid_count << '\n';
        std::cout << "[movement] allsolid="
                  << movement_statistics.all_solid_count << '\n';
    }
    if (smoke_input_profile) {
        std::cout << "[movement-smoke] profile=player-wall-contact-v1"
                  << " wall-found=" << (smoke_wall_found ? "true" : "false")
                  << " rendered-frames=" << rendered_frames
                  << " commands=" << movement_statistics.command_count
                  << " contacts=" << smoke_wall_contact_count
                  << " startsolid=" << movement_statistics.start_solid_count
                  << " allsolid=" << movement_statistics.all_solid_count
                  << " fatal="
                  << (movement_failure_latch.failure_latched() ? "true" :
                                                                    "false")
                  << " gl-error=" << (gl_error_none ? "none" : "present")
                  << " non-clear="
                  << (non_clear_pixel_count && *non_clear_pixel_count > 0U
                          ? "true"
                          : "false")
                  << " world-uploads=" << renderer_statistics.upload_count
                  << " scene-uploads="
                  << renderer_statistics.scene_upload_count
                  << " brush-uploads="
                  << renderer_statistics.brush_upload_count << '\n';
    }

    if (frame_limit && rendered_frames != *frame_limit) {
        std::cerr << "viewer-runtime=frame_limit_not_reached\n";
        return 1;
    }
    if (rendered_frames > 0U &&
        (renderer_statistics.upload_count != 1U ||
            renderer_statistics.scene_upload_count != 1U || !gl_error_none ||
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
    if (smoke_input_profile &&
        (!smoke_wall_found || movement_statistics.command_count == 0U ||
            !smoke_wall_touch_filter || smoke_wall_contact_count == 0U ||
            movement_statistics.start_solid_count != 0U ||
            movement_statistics.all_solid_count != 0U ||
            movement_failure_latch.failure_latched())) {
        std::cerr << "movement-runtime=smoke_wall_campaign_failed\n";
        return 1;
    }
    if (player_controller) {
        if (movement_failure_latch.failure_latched()) {
            return 1;
        }
        if (movement_statistics.start_solid_count != 0U ||
            movement_statistics.all_solid_count != 0U ||
            movement_statistics.command_count !=
                player_controller->player_state().source_command_sequence()) {
            std::cerr << "[movement] result=invalid_summary\n";
            return 1;
        }
        std::cout << "[movement] result=success\n";
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
    const auto smoke_input_profile = smoke_test_input_profile();
    if (smoke_input_profile &&
        (!frame_limit || *frame_limit < 1'000U ||
            *frame_limit > hlclient::input::ScriptedInputSourceLimits::
                hard_maximum_frames ||
            *options->camera_mode != hlclient::world_preview::
                WorldPreviewCameraMode::player_walk)) {
        std::cerr << "viewer-runtime=invalid_smoke_input_contract\n";
        return 2;
    }

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
        auto prepared_scene = build_world_scene(
            document,
            std::move(world_package),
            retained_bsp_source,
            environment,
            *options);
        if (!prepared_scene) {
            return std::nullopt;
        }
        if (*options->camera_mode ==
            hlclient::world_preview::WorldPreviewCameraMode::player_walk) {
            auto local_player = prepare_local_player(document);
            if (!local_player) {
                return std::nullopt;
            }
            prepared_scene->local_player.emplace(std::move(*local_player));
        }
        return prepared_scene;
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
        frame_limit,
        smoke_input_profile);
}

} // namespace

int wmain(const int argument_count, wchar_t* arguments[])
{
    try {
        return run_viewer(argument_count, arguments);
    } catch (const hlclient::renderer::opengl::OpenGlRendererError& error) {
        if (scripted_wall_smoke_requested() &&
            hlclient::platform::classify_opengl_startup_capability_failure(
                error) != hlclient::platform::
                    OpenGlStartupCapabilityFailure::none) {
            std::cout << "wall-contact-opengl=capability-unavailable\n";
            return 0;
        }
        std::cerr << "opengl-render="
                  << hlclient::renderer::opengl::to_string(error.code())
                  << '\n';
    } catch (const std::bad_alloc&) {
        std::cerr << "viewer=allocation_failed\n";
    } catch (const std::runtime_error& error) {
        if (scripted_wall_smoke_requested() &&
            hlclient::platform::classify_opengl_startup_capability_failure(
                error) != hlclient::platform::
                    OpenGlStartupCapabilityFailure::none) {
            std::cout << "wall-contact-opengl=capability-unavailable\n";
            return 0;
        }
        // Shader, draw, upload, swap and other runtime failures remain fatal.
        std::cerr << "viewer=failed\n";
    } catch (const std::exception&) {
        // Exception text may contain platform paths or driver strings. Keep
        // diagnostics bounded and metadata-only at this trust boundary.
        std::cerr << "viewer=failed\n";
    } catch (...) {
        std::cerr << "viewer=unknown_cpp_exception\n";
    }
    return 1;
}
