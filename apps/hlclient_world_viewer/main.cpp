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
#if defined(HLCLIENT_PREDICTION_VIEWER)
#include <hlclient/local_player/local_player_prediction_controller.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <hlclient/prediction/synthetic_authoritative_player.hpp>
#endif
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

#if defined(HLCLIENT_PREDICTION_VIEWER)
enum class PredictionViewerScenario : std::uint8_t {
    exact,
    small_correction,
    large_correction,
    delayed,
    wall_replay,
    jump_replay,
    duck_replay,
    mixed,
};

enum class PredictionDiagnosticsMode : std::uint8_t {
    off,
    summary,
};
#endif

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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    std::optional<PredictionViewerScenario> prediction_scenario;
    std::size_t authority_delay_commands{8U};
    bool authority_delay_present{false};
    PredictionDiagnosticsMode prediction_diagnostics{
        PredictionDiagnosticsMode::off};
    bool prediction_diagnostics_present{false};
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    Options options;
    options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::player_walk;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument != L"--basedir" && argument != L"--game" &&
            argument != L"--map" && argument != L"--scenario" &&
            argument != L"--authority-delay-commands" &&
            argument != L"--prediction-diagnostics" &&
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
        } else if (argument == L"--scenario") {
            if (options.prediction_scenario) {
                return std::nullopt;
            }
            if (*narrow == "exact") {
                options.prediction_scenario = PredictionViewerScenario::exact;
            } else if (*narrow == "small-correction") {
                options.prediction_scenario =
                    PredictionViewerScenario::small_correction;
            } else if (*narrow == "large-correction") {
                options.prediction_scenario =
                    PredictionViewerScenario::large_correction;
            } else if (*narrow == "delayed") {
                options.prediction_scenario =
                    PredictionViewerScenario::delayed;
            } else if (*narrow == "wall-replay") {
                options.prediction_scenario =
                    PredictionViewerScenario::wall_replay;
            } else if (*narrow == "jump-replay") {
                options.prediction_scenario =
                    PredictionViewerScenario::jump_replay;
            } else if (*narrow == "duck-replay") {
                options.prediction_scenario =
                    PredictionViewerScenario::duck_replay;
            } else if (*narrow == "mixed") {
                options.prediction_scenario = PredictionViewerScenario::mixed;
            } else {
                return std::nullopt;
            }
        } else if (argument == L"--authority-delay-commands") {
            if (options.authority_delay_present || narrow->empty()) {
                return std::nullopt;
            }
            std::uint64_t parsed = 0U;
            const auto conversion = std::from_chars(
                narrow->data(), narrow->data() + narrow->size(), parsed, 10);
            if (conversion.ec != std::errc{} ||
                conversion.ptr != narrow->data() + narrow->size() ||
                parsed > hlclient::prediction::
                    kMaximumSyntheticAuthorityDelayCommands) {
                return std::nullopt;
            }
            options.authority_delay_commands =
                static_cast<std::size_t>(parsed);
            options.authority_delay_present = true;
        } else if (argument == L"--prediction-diagnostics") {
            if (options.prediction_diagnostics_present) {
                return std::nullopt;
            }
            options.prediction_diagnostics_present = true;
            if (*narrow == "off") {
                options.prediction_diagnostics = PredictionDiagnosticsMode::off;
            } else if (*narrow == "summary") {
                options.prediction_diagnostics =
                    PredictionDiagnosticsMode::summary;
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
        !options.virtual_map || !options.prediction_scenario) {
        return std::nullopt;
    }
    if (!options.authority_delay_present &&
        *options.prediction_scenario == PredictionViewerScenario::exact) {
        options.authority_delay_commands = 0U;
    }
    return options;
#else
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
#endif
}

void print_usage()
{
#if defined(HLCLIENT_PREDICTION_VIEWER)
    std::cerr
        << "Usage: hlclient_prediction_viewer --basedir <Half-Life root> "
           "--game <directory> --map <maps/name.bsp> --scenario "
           "<exact|small-correction|large-correction|delayed|wall-replay|"
           "jump-replay|duck-replay|mixed> "
           "[--authority-delay-commands <0..64>] "
           "[--prediction-diagnostics <off|summary>] "
           "[--visibility <all|frustum|pvs|pvs-frustum>] "
           "[--brush-submodels <off|static>] [--cull <none|back>]\n"
        << "  player-walk: click captures, Escape releases, WASD walks, "
           "Space jumps, Ctrl ducks, and the mouse looks; synthetic "
           "authority only, no network commands\n";
#else
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
#endif
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

#if defined(HLCLIENT_PREDICTION_VIEWER)
[[nodiscard]] bool prediction_smoke_requested() noexcept
{
#if defined(_MSC_VER)
    char* frames = nullptr;
    std::size_t frames_size = 0U;
    if (::_dupenv_s(
            &frames, &frames_size, "HLCLIENT_SMOKE_TEST_FRAMES") != 0) {
        std::free(frames);
        return false;
    }
#else
    const char* frames = std::getenv("HLCLIENT_SMOKE_TEST_FRAMES");
#endif
    if (frames == nullptr) {
        return false;
    }
    std::uint64_t parsed_frames = 0U;
    const std::string_view text{frames};
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), parsed_frames, 10);
    const bool requested = parsed.ec == std::errc{} &&
        parsed.ptr == text.data() + text.size() && parsed_frames > 0U;
#if defined(_MSC_VER)
    std::free(frames);
#endif
    return requested;
}
#endif

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

#if defined(HLCLIENT_PREDICTION_VIEWER)
[[nodiscard]] hlclient::prediction::SyntheticAuthoritativeScenario
prediction_authority_scenario(
    const PredictionViewerScenario scenario) noexcept
{
    using Scenario = hlclient::prediction::SyntheticAuthoritativeScenario;
    switch (scenario) {
    case PredictionViewerScenario::exact: return Scenario::exact_authority;
    case PredictionViewerScenario::small_correction:
        return Scenario::small_position_correction;
    case PredictionViewerScenario::large_correction:
        return Scenario::large_position_correction;
    case PredictionViewerScenario::delayed:
        return Scenario::delayed_authority;
    case PredictionViewerScenario::wall_replay: return Scenario::wall_replay;
    case PredictionViewerScenario::jump_replay: return Scenario::jump_replay;
    case PredictionViewerScenario::duck_replay: return Scenario::duck_replay;
    case PredictionViewerScenario::mixed: return Scenario::mixed;
    }
    return Scenario::exact_authority;
}

[[nodiscard]] hlclient::assets::AssetVector3 prediction_add(
    const hlclient::assets::AssetVector3& left,
    const hlclient::assets::AssetVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] bool prediction_vector_finite(
    const hlclient::assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool prediction_state_finite(
    const hlclient::movement::LocalPlayerMovementState& state) noexcept
{
    return prediction_vector_finite(state.origin()) &&
        prediction_vector_finite(state.velocity()) &&
        prediction_vector_finite(state.view_angles()) &&
        prediction_vector_finite(state.view_offset());
}

[[nodiscard]] bool prediction_camera_content_equal(
    const hlclient::gameplay_camera::GameplayCameraState& left,
    const hlclient::gameplay_camera::GameplayCameraState& right) noexcept
{
    return left.position().x == right.position().x &&
        left.position().y == right.position().y &&
        left.position().z == right.position().z &&
        left.yaw_degrees() == right.yaw_degrees() &&
        left.pitch_degrees() == right.pitch_degrees() &&
        left.vertical_fov_radians() == right.vertical_fov_radians() &&
        left.near_plane() == right.near_plane() &&
        left.far_plane() == right.far_plane() &&
        left.mode() == right.mode() &&
        left.anchor_metadata() == right.anchor_metadata() &&
        left.compatibility_profile() == right.compatibility_profile() &&
        left.evidence_profile() == right.evidence_profile();
}

[[nodiscard]] hlclient::assets::AssetVector3 prediction_radial_direction(
    const std::size_t ordinal) noexcept
{
    constexpr std::size_t direction_count = 64U;
    constexpr double to_radians =
        0.017453292519943295769236907684886;
    const auto angle = static_cast<double>(ordinal) *
        (360.0 / static_cast<double>(direction_count)) * to_radians;
    return {static_cast<float>(std::cos(angle)),
        static_cast<float>(std::sin(angle)), 0.0F};
}

[[nodiscard]] bool prediction_position_free(
    const hlclient::goldsrc::movement::ILocalMovementCollision& collision,
    const hlclient::movement::LocalPlayerMovementState& initial,
    const hlclient::assets::AssetVector3& base_offset,
    const hlclient::assets::AssetVector3& delta,
    hlclient::collision::CollisionQueryScratch& scratch)
{
    const auto target = prediction_add(
        prediction_add(initial.origin(), base_offset), delta);
    const auto tested = collision.test_position(target, initial.hull(), scratch);
    return tested && tested.result && tested.result->status ==
        hlclient::goldsrc::movement::LocalMovementPositionStatus::free;
}

[[nodiscard]] std::optional<hlclient::assets::AssetVector3>
prediction_correction_delta(
    const hlclient::goldsrc::movement::ILocalMovementCollision& collision,
    const hlclient::movement::LocalPlayerMovementState& initial,
    const float magnitude,
    const hlclient::assets::AssetVector3& base_offset,
    hlclient::collision::CollisionQueryScratch& scratch)
{
    constexpr std::size_t direction_count = 64U;
    for (std::size_t ordinal = 0U; ordinal < direction_count; ++ordinal) {
        const auto direction = prediction_radial_direction(ordinal);
        const hlclient::assets::AssetVector3 delta{
            direction.x * magnitude, direction.y * magnitude, 0.0F};
        if (prediction_position_free(
                collision, initial, base_offset, delta, scratch)) {
            return delta;
        }
    }
    const std::array vertical{
        hlclient::assets::AssetVector3{0.0F, 0.0F, magnitude},
        hlclient::assets::AssetVector3{0.0F, 0.0F, -magnitude},
    };
    for (const auto& delta : vertical) {
        if (prediction_position_free(
                collision, initial, base_offset, delta, scratch)) {
            return delta;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<hlclient::input::ScriptedInputSource::Script>
build_prediction_smoke_campaign(const std::uint64_t frames)
{
    constexpr std::uint64_t minimum_frames = 1'000U;
    if (frames < minimum_frames ||
        frames > hlclient::input::ScriptedInputSourceLimits::
            hard_maximum_frames) {
        return std::nullopt;
    }
    hlclient::input::ScriptedInputSource::Script campaign;
    campaign.resize(static_cast<std::size_t>(frames));
    campaign.front().push_back(
        hlclient::input::InputEvent::focus_gained());
    campaign.front().push_back(
        hlclient::input::InputEvent::capture_acquired());
    campaign[2U].push_back(hlclient::input::InputEvent::key_pressed(
        hlclient::input::PhysicalKey::w));
    campaign[350U].push_back(hlclient::input::InputEvent::key_pressed(
        hlclient::input::PhysicalKey::d));
    campaign[550U].push_back(hlclient::input::InputEvent::key_released(
        hlclient::input::PhysicalKey::d));
    campaign[600U].push_back(hlclient::input::InputEvent::key_released(
        hlclient::input::PhysicalKey::w));
    campaign[700U].push_back(hlclient::input::InputEvent::key_pressed(
        hlclient::input::PhysicalKey::w));
    campaign[700U].push_back(hlclient::input::InputEvent::key_pressed(
        hlclient::input::PhysicalKey::space));
    campaign[701U].push_back(hlclient::input::InputEvent::key_released(
        hlclient::input::PhysicalKey::space));
    campaign[820U].push_back(hlclient::input::InputEvent::key_pressed(
        hlclient::input::PhysicalKey::left_control));
    campaign[920U].push_back(hlclient::input::InputEvent::key_released(
        hlclient::input::PhysicalKey::left_control));
    campaign.back().push_back(hlclient::input::InputEvent::key_released(
        hlclient::input::PhysicalKey::w));
    return campaign;
}
#endif

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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    std::unique_ptr<hlclient::local_player::LocalPlayerPredictionController>
        prediction_controller;
    std::unique_ptr<
        hlclient::prediction::SyntheticAuthoritativePlayerStateSource>
        prediction_authority;
    std::unique_ptr<hlclient::collision::CollisionWorldQuery>
        prediction_camera_collision;
#endif
    std::optional<hlclient::gameplay_input::GameplayInputBindings>
        player_bindings;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch movement_scratch;
#if defined(HLCLIENT_PREDICTION_VIEWER)
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch
        prediction_authority_scratch;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch
        prediction_replay_scratch;
    hlclient::collision::CollisionQueryScratch prediction_camera_scratch;
    hlclient::collision::CollisionQueryScratch prediction_validation_scratch;
    std::uint64_t prediction_failure_count = 0U;
    std::size_t prediction_last_replay_depth = 0U;
    hlclient::prediction::PredictionCorrectionClass prediction_last_correction{
        hlclient::prediction::PredictionCorrectionClass::exact};
    std::uint64_t prediction_smoothing_samples = 0U;
    std::uint64_t prediction_smoothing_decay_samples = 0U;
    std::uint64_t prediction_camera_publications = 0U;
    std::uint64_t prediction_camera_publication_revision = 0U;
    std::uint64_t prediction_max_camera_revision = 0U;
    std::uint64_t prediction_teleport_one_sample_count = 0U;
    std::optional<std::uint64_t> prediction_teleport_authority_frame;
    std::optional<double> prediction_previous_residual;
    bool prediction_failure_latched = false;
    bool prediction_smoke_wall_found = false;
    bool prediction_start_solid_observed = false;
    bool prediction_all_solid_observed = false;
#else
    ViewerMovementStatistics movement_statistics;
    hlclient::local_player::PlayerWalkFailureLatch movement_failure_latch;
#endif
    std::optional<hlclient::input::ScriptedInputSource> scripted_input;
#if !defined(HLCLIENT_PREDICTION_VIEWER)
    std::optional<
        hlclient::local_player::LocalPlayerMovementCommittedTouchFilter>
        smoke_wall_touch_filter;
    std::uint64_t smoke_wall_contact_count = 0U;
    bool smoke_wall_found = false;
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
        const auto synthetic_scenario = prediction_authority_scenario(
            *options.prediction_scenario);
        if ((synthetic_scenario == hlclient::prediction::
                    SyntheticAuthoritativeScenario::exact_authority &&
                options.authority_delay_commands != 0U) ||
            (synthetic_scenario == hlclient::prediction::
                    SyntheticAuthoritativeScenario::delayed_authority &&
                options.authority_delay_commands == 0U)) {
            std::cerr << "prediction-runtime=invalid_configuration\n";
            return 1;
        }
        const auto prediction_initial_state = player_controller->player_state();
        const auto prediction_environment = player_controller->environment();
        const auto session =
            hlclient::prediction::create_prediction_session_identity(
                1U,
                1U,
                *player_collision,
                prediction_environment,
                player_controller->config().movement,
                prediction_initial_state);
        if (!session || !session.session) {
            std::cerr << "prediction-runtime="
                      << (session.error
                              ? hlclient::prediction::to_string(
                                    session.error->code)
                              : std::string_view{"invalid_session_identity"})
                      << '\n';
            return 1;
        }
        hlclient::local_player::LocalPlayerPredictionControllerConfig
            prediction_config;
        prediction_config.history.maximum_entries =
            hlclient::prediction::kHardMaximumPredictionHistoryEntries;
        prediction_config.history.maximum_authority_delay_commands =
            hlclient::prediction::kMaximumSyntheticAuthorityDelayCommands +
            hlclient::goldsrc::kMaximumUserCmdsPerSchedulerUpdate;
        prediction_config.history.maximum_replay_commands =
            hlclient::prediction::kHardMaximumPredictionReplayCommands;
        prediction_config.reconciliation.limits.maximum_replay_commands =
            hlclient::prediction::kHardMaximumPredictionReplayCommands;
        auto created_prediction = hlclient::local_player::
            LocalPlayerPredictionController::create(
                std::move(*player_controller),
                *session.session,
                prediction_config);
        player_controller.reset();
        if (!created_prediction || !created_prediction.controller) {
            std::cerr << "prediction-runtime="
                      << (created_prediction.error
                              ? hlclient::prediction::to_string(
                                    created_prediction.error->code)
                              : std::string_view{
                                    "controller_initialization_failed"})
                      << '\n';
            return 1;
        }
        prediction_controller = std::move(created_prediction.controller);

        hlclient::prediction::SyntheticAuthoritativePlayerConfig
            authority_config;
        authority_config.session = *session.session;
        authority_config.scenario = synthetic_scenario;
        authority_config.command_delay = options.authority_delay_commands;
        authority_config.maximum_pending_updates = hlclient::prediction::
            kMaximumSyntheticAuthorityPendingUpdates;
        authority_config.correction_command_sequence = 1U;
        hlclient::collision::CollisionQueryScratch correction_scratch;
        const bool needs_small = synthetic_scenario == hlclient::prediction::
                SyntheticAuthoritativeScenario::small_position_correction ||
            synthetic_scenario ==
                hlclient::prediction::SyntheticAuthoritativeScenario::mixed;
        const bool needs_large = synthetic_scenario == hlclient::prediction::
                SyntheticAuthoritativeScenario::large_position_correction ||
            synthetic_scenario ==
                hlclient::prediction::SyntheticAuthoritativeScenario::mixed;
        if (needs_small) {
            const auto delta = prediction_correction_delta(
                *player_collision,
                prediction_initial_state,
                0.5F,
                {},
                correction_scratch);
            if (!delta) {
                std::cerr <<
                    "prediction-runtime=correction_destination_unavailable\n";
                return 1;
            }
            authority_config.small_position_delta = *delta;
        }
        if (needs_large) {
            std::optional<hlclient::assets::AssetVector3> delta;
            constexpr std::array magnitudes{32.0F, 24.0F, 48.0F, 64.0F};
            const auto base_offset = needs_small
                ? authority_config.small_position_delta
                : hlclient::assets::AssetVector3{};
            for (const auto magnitude : magnitudes) {
                delta = prediction_correction_delta(
                    *player_collision,
                    prediction_initial_state,
                    magnitude,
                    base_offset,
                    correction_scratch);
                if (delta) {
                    break;
                }
            }
            if (!delta) {
                std::cerr <<
                    "prediction-runtime=correction_destination_unavailable\n";
                return 1;
            }
            authority_config.large_position_delta = *delta;
        }
        if (synthetic_scenario ==
            hlclient::prediction::SyntheticAuthoritativeScenario::mixed) {
            authority_config.teleport_origin = prediction_initial_state.origin();
        }
        auto created_authority = hlclient::prediction::
            SyntheticAuthoritativePlayerStateSource::create(
                prediction_initial_state,
                prediction_environment,
                authority_config,
                *player_collision,
                prediction_authority_scratch,
                prediction_controller->movement_controller().config().movement);
        if (!created_authority || !created_authority.source) {
            std::cerr << "prediction-runtime="
                      << (created_authority.error
                              ? hlclient::prediction::to_string(
                                    created_authority.error->code)
                              : std::string_view{
                                    "synthetic_authority_initialization_failed"})
                      << '\n';
            return 1;
        }
        prediction_authority = std::make_unique<hlclient::prediction::
            SyntheticAuthoritativePlayerStateSource>(
                std::move(*created_authority.source));
        prediction_camera_collision =
            std::make_unique<hlclient::collision::CollisionWorldQuery>(
                local_player.collision_world);
#endif
        auto built_bindings = hlclient::gameplay_input::GameplayInputBindings::
            project_default_v1();
        if (!built_bindings || !built_bindings.bindings) {
            std::cerr << "movement-runtime=input_bindings_unavailable\n";
            return 1;
        }
        player_bindings.emplace(std::move(*built_bindings.bindings));

#if defined(HLCLIENT_PREDICTION_VIEWER)
        const auto initial_camera = build_client_player_camera(
            prediction_controller->movement_controller().camera());
#else
        const auto initial_camera =
            build_client_player_camera(player_controller->camera());
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
        std::cout << "[prediction] profile="
                  << hlclient::prediction::to_string(
                         prediction_controller->session().prediction_profile)
                  << '\n';
        std::cout << "[prediction] authority=synthetic-in-memory\n";
#else
        std::cout << "[movement] collision=world-only\n";
        std::cout << "[movement] brush-solidity=stock-evidence-pending\n";
#endif
#if defined(HLCLIENT_PREDICTION_VIEWER)
        if (frame_limit) {
            if (*options.prediction_scenario ==
                PredictionViewerScenario::wall_replay) {
                auto campaign = build_smoke_wall_campaign(
                    *frame_limit,
                    prediction_controller->movement_controller(),
                    *player_collision,
                    movement_scratch);
                if (!campaign) {
                    std::cerr << "prediction-runtime=wall_campaign_unavailable\n";
                    return 1;
                }
                const auto campaign_frame_count = campaign->frames.size();
                scripted_input.emplace(
                    std::move(campaign->frames),
                    hlclient::input::ScriptedInputSourceLimits{
                        campaign_frame_count, 16U, 64U});
                prediction_smoke_wall_found = true;
            } else {
                auto campaign = build_prediction_smoke_campaign(*frame_limit);
                if (!campaign) {
                    std::cerr << "prediction-runtime=invalid_smoke_campaign\n";
                    return 1;
                }
                const auto campaign_frame_count = campaign->size();
                scripted_input.emplace(
                    std::move(*campaign),
                    hlclient::input::ScriptedInputSourceLimits{
                        campaign_frame_count, 16U, 64U});
            }
        }
#else
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
#endif
    }

    // Every CPU prerequisite and the immutable package are valid before SDL
    // or an OpenGL context exists.
    [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
    hlclient::platform::SdlWindow window{hlclient::platform::SdlWindowConfig{
#if defined(HLCLIENT_PREDICTION_VIEWER)
        "HL Client Prediction Viewer",
#else
        "HL Client World Viewer",
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    const auto latch_prediction_failure = [&window,
                                              &prediction_controller,
                                              &input_tracker,
                                              &capture_failure_count,
                                              &prediction_failure_count,
                                              &prediction_failure_latched](
        const std::optional<hlclient::prediction::PredictionError>&
            prediction_error,
        const std::optional<hlclient::local_player::
            LocalPlayerMovementControllerError>& movement_error,
        const std::string_view phase) {
        if (prediction_failure_count != UINT64_MAX) {
            ++prediction_failure_count;
        }
        if (prediction_failure_latched) {
            return;
        }
        prediction_failure_latched = true;
        std::cerr << "[prediction] result=failure_latched phase=" << phase
                  << " prediction="
                  << (prediction_error
                          ? hlclient::prediction::to_string(
                                prediction_error->code)
                          : std::string_view{"none"})
                  << " movement="
                  << (movement_error
                          ? hlclient::local_player::to_string(
                                movement_error->code)
                          : std::string_view{"none"})
                  << " action=simulation_disabled rendering=continued\n";
        if (prediction_controller) {
            prediction_controller->cancel();
        }
        input_tracker.reset();
        const auto released = window.request_relative_mouse_capture(false);
        if (!released && capture_failure_count != UINT64_MAX) {
            ++capture_failure_count;
        }
    };
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
        if (prediction_controller && prediction_authority &&
            prediction_camera_collision && player_collision &&
            player_bindings && input_snapshot &&
            !prediction_failure_latched) {
            const auto bounded_input_elapsed = std::min(
                std::chrono::duration<double>{elapsed}.count(), 0.25);
            auto built_intent =
                hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
                    *input_snapshot,
                    *player_bindings,
                    prediction_controller->movement_controller().config().
                        camera.mouse_look_config(),
                    bounded_input_elapsed);
            if (!built_intent || !built_intent.intent) {
                latch_prediction_failure(
                    std::optional<hlclient::prediction::PredictionError>{
                        hlclient::prediction::PredictionError{
                            hlclient::prediction::PredictionErrorCode::
                                invalid_configuration,
                            std::nullopt,
                            "prediction input intent build failed"}},
                    std::nullopt,
                    "input");
            } else {
                std::int64_t movement_time_nanoseconds = 0;
                if (frame_limit) {
                    const auto interval = prediction_controller->
                        movement_controller().config().scheduler.
                            command_interval_nanoseconds;
                    if (rendered_frames >
                        static_cast<std::uint64_t>(
                            (std::numeric_limits<std::int64_t>::max)()) /
                            interval) {
                        latch_prediction_failure(
                            std::optional<
                                hlclient::prediction::PredictionError>{
                                hlclient::prediction::PredictionError{
                                    hlclient::prediction::PredictionErrorCode::
                                        revision_exhausted,
                                    std::nullopt,
                                    "prediction smoke time overflow"}},
                            std::nullopt,
                            "time");
                    } else {
                        movement_time_nanoseconds =
                            static_cast<std::int64_t>(
                                rendered_frames * interval);
                    }
                } else {
                    movement_time_nanoseconds = std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                            current_time.time_since_epoch()).count();
                }
                if (!prediction_failure_latched) {
                    const auto previous_newest = prediction_controller->
                        history()->newest_command_sequence();
                    const auto previous_sequence = previous_newest
                        ? previous_newest->value()
                        : prediction_controller->history()->anchor().
                                  acknowledgement().sequence()
                            ? prediction_controller->history()->anchor().
                                  acknowledgement().sequence()->value()
                            : 0U;
                    auto prediction_update =
                        prediction_controller->update_local_input(
                            movement_time_nanoseconds,
                            *built_intent.intent,
                            *player_collision,
                            movement_scratch);
                    if (!prediction_update) {
                        latch_prediction_failure(
                            prediction_update.prediction_error,
                            prediction_update.movement_error,
                            "local_prediction");
                    } else {
                        std::size_t submitted_count = 0U;
                        for (const auto& entry :
                            prediction_controller->history()->entries()) {
                            if (entry.command_sequence().value() <=
                                previous_sequence) {
                                continue;
                            }
                            prediction_start_solid_observed =
                                prediction_start_solid_observed ||
                                entry.touch_summary().start_solid;
                            prediction_all_solid_observed =
                                prediction_all_solid_observed ||
                                entry.touch_summary().all_solid;
                            const auto submitted =
                                prediction_authority->submit_command(
                                    *entry.command(),
                                    *player_collision,
                                    prediction_authority_scratch);
                            if (!submitted) {
                                latch_prediction_failure(
                                    submitted.error,
                                    std::nullopt,
                                    "authority_submit");
                                break;
                            }
                            ++submitted_count;
                        }
                        if (!prediction_failure_latched &&
                            submitted_count != prediction_update.command_count) {
                            latch_prediction_failure(
                                std::optional<
                                    hlclient::prediction::PredictionError>{
                                    hlclient::prediction::PredictionError{
                                        hlclient::prediction::
                                            PredictionErrorCode::
                                                prediction_command_gap,
                                        std::nullopt,
                                        "prepared command publication gap"}},
                                std::nullopt,
                                "authority_submit");
                        }

                        const auto prediction_time_seconds =
                            static_cast<double>(movement_time_nanoseconds) /
                            1'000'000'000.0;
                        while (!prediction_failure_latched) {
                            auto polled = prediction_authority->poll_next();
                            if (polled.error) {
                                latch_prediction_failure(
                                    polled.error,
                                    std::nullopt,
                                    "authority_poll");
                                break;
                            }
                            if (!polled.state) {
                                break;
                            }
                            auto applied = prediction_controller->
                                apply_authoritative_state(
                                    *polled.state,
                                    prediction_time_seconds,
                                    *player_collision,
                                    prediction_replay_scratch);
                            if (!applied) {
                                latch_prediction_failure(
                                    applied.prediction_error,
                                    applied.movement_error,
                                    "reconciliation");
                                break;
                            }
                            prediction_last_replay_depth =
                                applied.replay_depth;
                            if (applied.event) {
                                prediction_last_correction =
                                    applied.event->correction_class;
                                if (applied.event->correction_class ==
                                    hlclient::prediction::
                                        PredictionCorrectionClass::
                                            teleport_snap) {
                                    const auto& physical_camera =
                                        prediction_controller->
                                            movement_controller().camera();
                                    if (prediction_teleport_authority_frame ||
                                        !applied.camera ||
                                        prediction_controller->
                                            visual_correction().active() ||
                                        !prediction_camera_content_equal(
                                            *applied.camera,
                                            physical_camera)) {
                                        latch_prediction_failure(
                                            std::optional<hlclient::prediction::
                                                PredictionError>{
                                                hlclient::prediction::
                                                    PredictionError{
                                                    hlclient::prediction::
                                                        PredictionErrorCode::
                                                            visual_correction_failed,
                                                    std::nullopt,
                                                    "teleport did not snap at reconciliation"}},
                                            std::nullopt,
                                            "teleport_snap");
                                    } else {
                                        prediction_teleport_authority_frame =
                                            rendered_frames;
                                    }
                                }
                            }
                            for (const auto& entry :
                                prediction_controller->history()->entries()) {
                                prediction_start_solid_observed =
                                    prediction_start_solid_observed ||
                                    entry.touch_summary().start_solid;
                                prediction_all_solid_observed =
                                    prediction_all_solid_observed ||
                                    entry.touch_summary().all_solid;
                            }
                        }

                        if (!prediction_failure_latched) {
                            const bool smoothing_was_active =
                                prediction_controller->visual_correction().
                                    active();
                            auto sampled = prediction_controller->sample_camera(
                                prediction_time_seconds,
                                prediction_camera_collision.get(),
                                prediction_camera_scratch);
                            if (!sampled || !sampled.camera) {
                                latch_prediction_failure(
                                    sampled.prediction_error,
                                    sampled.movement_error,
                                    "camera_smoothing");
                            } else {
                                if (prediction_teleport_authority_frame) {
                                    const auto& physical_camera =
                                        prediction_controller->
                                            movement_controller().camera();
                                    if (*prediction_teleport_authority_frame !=
                                            rendered_frames ||
                                        prediction_controller->
                                            visual_correction().active() ||
                                        !prediction_camera_content_equal(
                                            *sampled.camera,
                                            physical_camera) ||
                                        prediction_teleport_one_sample_count ==
                                            UINT64_MAX) {
                                        latch_prediction_failure(
                                            std::optional<hlclient::prediction::
                                                PredictionError>{
                                                hlclient::prediction::
                                                    PredictionError{
                                                    hlclient::prediction::
                                                        PredictionErrorCode::
                                                            visual_correction_failed,
                                                    std::nullopt,
                                                    "teleport presentation exceeded one sample"}},
                                            std::nullopt,
                                            "teleport_snap");
                                    } else {
                                        ++prediction_teleport_one_sample_count;
                                        prediction_teleport_authority_frame.
                                            reset();
                                    }
                                }
                                if (smoothing_was_active) {
                                    const auto& residual =
                                        prediction_controller->
                                            visual_correction().
                                                current_residual_offset();
                                    const auto magnitude = std::sqrt(
                                        static_cast<double>(residual.x) *
                                                residual.x +
                                            static_cast<double>(residual.y) *
                                                residual.y +
                                            static_cast<double>(residual.z) *
                                                residual.z);
                                    if (prediction_smoothing_samples !=
                                        UINT64_MAX) {
                                        ++prediction_smoothing_samples;
                                    }
                                    if (prediction_previous_residual &&
                                        magnitude + 1.0e-9 <
                                            *prediction_previous_residual &&
                                        prediction_smoothing_decay_samples !=
                                            UINT64_MAX) {
                                        ++prediction_smoothing_decay_samples;
                                    }
                                    prediction_previous_residual = magnitude;
                                } else {
                                    prediction_previous_residual.reset();
                                }

                                const auto& player_state =
                                    prediction_controller->
                                        movement_controller().player_state();
                                const auto tested =
                                    player_collision->test_position(
                                        player_state.origin(),
                                        player_state.hull(),
                                        prediction_validation_scratch,
                                        prediction_controller->
                                            movement_controller().config().
                                                movement.collision_query);
                                if (!tested || !tested.result ||
                                    tested.result->status !=
                                        hlclient::goldsrc::movement::
                                            LocalMovementPositionStatus::free) {
                                    latch_prediction_failure(
                                        std::optional<hlclient::prediction::
                                            PredictionError>{
                                            hlclient::prediction::
                                                PredictionError{
                                                hlclient::prediction::
                                                    PredictionErrorCode::
                                                        authoritative_state_blocking,
                                                std::nullopt,
                                                "predicted player state blocking"}},
                                        std::nullopt,
                                        "state_validation");
                                } else {
                                    if (built_intent.intent->
                                            capture_mouse_requested()) {
                                        const auto capture = window.
                                            request_relative_mouse_capture(true);
                                        if (!capture) {
                                            latch_prediction_failure(
                                                std::nullopt,
                                                std::nullopt,
                                                "input_capture");
                                        }
                                    }
                                    if (!prediction_failure_latched &&
                                        built_intent.intent->
                                            release_mouse_requested()) {
                                        const auto release = window.
                                            request_relative_mouse_capture(
                                                false);
                                        if (!release) {
                                            latch_prediction_failure(
                                                std::nullopt,
                                                std::nullopt,
                                                "input_release");
                                        }
                                    }
                                    const auto player_camera =
                                        build_client_player_camera(
                                            *sampled.camera);
                                    const auto publication_revision =
                                        prediction_camera_publication_revision ==
                                                UINT64_MAX
                                        ? std::optional<std::uint64_t>{}
                                        : std::optional<std::uint64_t>{
                                              prediction_camera_publication_revision +
                                              1U};
                                    if (!prediction_failure_latched &&
                                        (!player_camera ||
                                            !publication_revision ||
                                            !scene_source.
                                                publish_interactive_camera(
                                                    *player_camera,
                                                    hlclient::client::
                                                        InteractiveCameraMetadata{
                                                        input_snapshot->
                                                            sequence(),
                                                        *publication_revision,
                                                        hlclient::client::
                                                            InteractiveCameraMode::
                                                                player_walk,
                                                        std::nullopt,
                                                        hlclient::client::
                                                            ControlledEntityCameraStatus::
                                                                not_applicable}))) {
                                        latch_prediction_failure(
                                            std::nullopt,
                                            std::nullopt,
                                            "camera_publication");
                                    } else if (!prediction_failure_latched) {
                                        prediction_camera_publication_revision =
                                            *publication_revision;
                                        if (prediction_camera_publications !=
                                            UINT64_MAX) {
                                            ++prediction_camera_publications;
                                        }
                                        prediction_max_camera_revision =
                                            (std::max)(
                                                prediction_max_camera_revision,
                                                sampled.camera->revision());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
#else
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
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    const auto& renderer_entity_statistics = renderer.entity_statistics();
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    std::cout << "failed-upload-count="
              << renderer_statistics.failed_upload_count << '\n';
    std::cout << "world-resource-release-count="
              << renderer_statistics.world_resource_release_count << '\n';
    std::cout << "active-world-resources="
              << (renderer_statistics.active_world_resources ? 1 : 0) << '\n';
    std::cout << "studio-upload-count="
              << renderer_entity_statistics.studio_asset_upload_count << '\n';
    std::cout << "sprite-upload-count="
              << renderer_entity_statistics.sprite_asset_upload_count << '\n';
    std::cout << "entity-resource-release-count="
              << renderer_entity_statistics.entity_resource_release_count
              << '\n';
#endif
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
#if !defined(HLCLIENT_PREDICTION_VIEWER)
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
#else
    if (prediction_controller) {
        const auto& prediction_statistics =
            prediction_controller->statistics();
        const auto snap_count = prediction_statistics.large_snaps +
            prediction_statistics.teleports + prediction_statistics.hard_resets;
        std::cout << "input-frames=" << input_frame_count << '\n';
        std::cout << "input-events=" << input_event_count << '\n';
        std::cout << "capture-failures=" << capture_failure_count << '\n';
        std::cout << "[prediction] commands="
                  << prediction_statistics.predicted_commands << '\n';
        std::cout << "[prediction] authority-updates="
                  << prediction_statistics.authoritative_updates << '\n';
        std::cout << "[prediction] acknowledgements="
                  << prediction_statistics.accepted_acknowledgements << '\n';
        std::cout << "[prediction] replays="
                  << prediction_statistics.replay_count << '\n';
        std::cout << "[prediction] replayed-commands="
                  << prediction_statistics.replayed_command_count << '\n';
        std::cout << "[prediction] maximum-replay-depth="
                  << prediction_statistics.maximum_replay_depth << '\n';
        std::cout << "[prediction] small-corrections="
                  << prediction_statistics.small_corrections << '\n';
        std::cout << "[prediction] snaps=" << snap_count << '\n';
        std::cout << "[prediction] history-high-water="
                  << prediction_statistics.history_high_water_mark << '\n';
        std::cout << "[prediction] startsolid="
                  << (prediction_start_solid_observed ? 1 : 0) << '\n';
        std::cout << "[prediction] allsolid="
                  << (prediction_all_solid_observed ? 1 : 0) << '\n';
        std::cout << "[prediction] teleport-one-sample="
                  << prediction_teleport_one_sample_count << '\n';
        if (options.prediction_diagnostics ==
            PredictionDiagnosticsMode::summary) {
            const auto newest =
                prediction_controller->history()->newest_command_sequence();
            const auto acknowledged = prediction_controller->history()->
                anchor().acknowledgement().sequence();
            std::cout << "[prediction] history="
                      << prediction_controller->history()->size() << '\n';
            std::cout << "[prediction] latest-command="
                      << (newest
                              ? newest->value()
                              : prediction_controller->movement_controller().
                                    player_state().source_command_sequence())
                      << '\n';
            if (acknowledged) {
                std::cout << "[prediction] acknowledged="
                          << acknowledged->value() << '\n';
            } else {
                std::cout << "[prediction] acknowledged=none\n";
            }
            std::cout << "[prediction] replay-depth="
                      << prediction_last_replay_depth << '\n';
            std::cout << "[prediction] correction="
                      << hlclient::prediction::to_string(
                             prediction_last_correction)
                      << '\n';
            std::cout << "[prediction] smoothing="
                      << (prediction_controller->visual_correction().active()
                              ? "active"
                              : "inactive")
                      << '\n';
            std::cout << "[prediction] constrained="
                      << (prediction_statistics.
                                      constrained_camera_corrections != 0U
                              ? "true"
                              : "false")
                      << '\n';
            std::cout << "[prediction] failures="
                      << prediction_failure_count << '\n';
        }
    }
#endif

    if (frame_limit && rendered_frames != *frame_limit) {
        std::cerr << "viewer-runtime=frame_limit_not_reached\n";
        return 1;
    }
#if defined(HLCLIENT_PREDICTION_VIEWER)
    const std::uint64_t expected_brush_upload_count =
        options.brush_submodels == hlclient::world_preview::
                WorldPreviewBrushSubmodelsMode::static_instances
        ? 1U
        : 0U;
#endif
    if (rendered_frames > 0U &&
        (renderer_statistics.upload_count != 1U ||
            renderer_statistics.scene_upload_count != 1U ||
#if defined(HLCLIENT_PREDICTION_VIEWER)
            renderer_statistics.brush_upload_count !=
                expected_brush_upload_count ||
            renderer_statistics.failed_upload_count != 0U ||
            renderer_statistics.world_resource_release_count != 0U ||
            !renderer_statistics.active_world_resources ||
            renderer_entity_statistics.studio_asset_upload_count != 0U ||
            renderer_entity_statistics.sprite_asset_upload_count != 0U ||
            renderer_entity_statistics.entity_resource_release_count != 0U ||
            renderer_entity_statistics.entity_scene_present ||
#endif
            !gl_error_none ||
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
#if !defined(HLCLIENT_PREDICTION_VIEWER)
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
#else
    if (!prediction_controller || !prediction_authority ||
        !prediction_camera_collision || !player_collision ||
        prediction_failure_latched || prediction_failure_count != 0U) {
        std::cerr << "prediction-runtime=failure_latched\n";
        return 1;
    }
    const auto& prediction_statistics = prediction_controller->statistics();
    const auto& final_player_state =
        prediction_controller->movement_controller().player_state();
    const auto final_position = player_collision->test_position(
        final_player_state.origin(),
        final_player_state.hull(),
        prediction_validation_scratch,
        prediction_controller->movement_controller().config().movement.
            collision_query);
    const bool small_smoothing_required = frame_limit &&
        *options.prediction_scenario ==
            PredictionViewerScenario::small_correction;
    const bool large_snap_required = frame_limit &&
        (*options.prediction_scenario ==
                PredictionViewerScenario::large_correction ||
            *options.prediction_scenario == PredictionViewerScenario::mixed);
    const bool wall_replay_required = frame_limit &&
        *options.prediction_scenario == PredictionViewerScenario::wall_replay &&
        options.authority_delay_commands > 0U;
    if (!final_position || !final_position.result ||
        final_position.result->status != hlclient::goldsrc::movement::
            LocalMovementPositionStatus::free ||
        !prediction_state_finite(final_player_state) ||
        prediction_start_solid_observed || prediction_all_solid_observed ||
        prediction_statistics.predicted_commands !=
            prediction_authority->simulator().statistics().
                processed_command_count ||
        prediction_statistics.history_backpressure_count != 0U ||
        prediction_statistics.replay_failures != 0U ||
        prediction_controller->history()->size() >
            prediction_controller->history()->limits().
                maximum_authority_delay_commands ||
        (frame_limit &&
            (prediction_camera_publications != rendered_frames ||
                prediction_camera_publication_revision !=
                    prediction_camera_publications ||
                prediction_max_camera_revision >
                    prediction_statistics.predicted_commands +
                        rendered_frames + 2U)) ||
        (small_smoothing_required &&
            (prediction_statistics.small_corrections == 0U ||
                prediction_smoothing_samples < 3U ||
                prediction_smoothing_decay_samples < 2U)) ||
        (large_snap_required && prediction_statistics.large_snaps == 0U) ||
        (wall_replay_required &&
            prediction_statistics.replayed_command_count == 0U) ||
        (frame_limit &&
            *options.prediction_scenario == PredictionViewerScenario::mixed &&
            (prediction_statistics.teleports == 0U ||
                prediction_teleport_authority_frame ||
                prediction_teleport_one_sample_count !=
                    prediction_statistics.teleports)) ||
        (frame_limit && *options.prediction_scenario ==
                PredictionViewerScenario::wall_replay &&
            !prediction_smoke_wall_found)) {
        std::cerr << "prediction-runtime=validation_failed\n";
        return 1;
    }
    std::cout << "[prediction-opengl] result=success\n";
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
    auto smoke_input_profile = smoke_test_input_profile();
    if (frame_limit && !smoke_input_profile) {
        smoke_input_profile = SmokeTestInputProfile::player_wall_contact_v1;
    }
#else
    const auto smoke_input_profile = smoke_test_input_profile();
#endif
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
#if defined(HLCLIENT_PREDICTION_VIEWER)
        if (prediction_smoke_requested() &&
            hlclient::platform::classify_opengl_startup_capability_failure(
                error) != hlclient::platform::
                    OpenGlStartupCapabilityFailure::none) {
            std::cout << "[prediction-opengl] capability=unavailable\n";
            return 0;
        }
#else
        if (scripted_wall_smoke_requested() &&
            hlclient::platform::classify_opengl_startup_capability_failure(
                error) != hlclient::platform::
                    OpenGlStartupCapabilityFailure::none) {
            std::cout << "wall-contact-opengl=capability-unavailable\n";
            return 0;
        }
#endif
        std::cerr << "opengl-render="
                  << hlclient::renderer::opengl::to_string(error.code())
                  << '\n';
    } catch (const std::bad_alloc&) {
        std::cerr << "viewer=allocation_failed\n";
    } catch (const std::runtime_error& error) {
#if defined(HLCLIENT_PREDICTION_VIEWER)
        if (prediction_smoke_requested() &&
            hlclient::platform::classify_opengl_startup_capability_failure(
                error) != hlclient::platform::
                    OpenGlStartupCapabilityFailure::none) {
            std::cout << "[prediction-opengl] capability=unavailable\n";
            return 0;
        }
#else
        if (scripted_wall_smoke_requested() &&
            hlclient::platform::classify_opengl_startup_capability_failure(
                error) != hlclient::platform::
                    OpenGlStartupCapabilityFailure::none) {
            std::cout << "wall-contact-opengl=capability-unavailable\n";
            return 0;
        }
#endif
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
