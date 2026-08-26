#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/asset_types.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/entity_visual/entity_visual_projection.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/delta_value_decoder.hpp>
#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/goldsrc/server_info.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/interactive_preview/interactive_entity_visibility.hpp>
#include <hlclient/interactive_preview/interactive_preview_controller.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

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
#include <locale>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t kMaximumViewerVisuals = 63U;
constexpr std::size_t kMaximumOperationUpdates = 1'000'000U;
constexpr float kViewerEntitySpacing = 72.0F;
constexpr hlclient::assets::AssetVector3 kViewerEntityEyeOffset{
    0.0F, 0.0F, 28.0F};
constexpr float kViewerFirstPersonNearPlane = 0.1F;

enum class FixtureKind {
  studio,
  sprite,
  mixed,
};

enum class CameraMode {
  static_camera,
  orbit,
  spawn,
  free_flight,
  entity_first_person,
};

enum class RequestedVisualKind {
  studio,
  sprite,
};

struct Options {
  std::optional<std::filesystem::path> base_directory;
  std::optional<std::string> game_directory;
  std::optional<std::string> virtual_map;
  std::vector<std::string> models;
  std::vector<std::string> sprites;
  std::optional<FixtureKind> fixture;
  std::optional<CameraMode> camera;
  std::optional<std::uint32_t> controlled_entity;
};

struct RequestedVisual {
  RequestedVisualKind kind{RequestedVisualKind::studio};
  std::string virtual_name;
  std::uint16_t model_slot{0U};
  hlclient::local_resources::LocalResourceLocator locator;
};

struct ImportedVisual {
  RequestedVisualKind requested_kind{RequestedVisualKind::studio};
  std::shared_ptr<const hlclient::assets::ModelAsset> model;
  std::shared_ptr<const hlclient::assets::SpriteAsset> sprite;
  std::string importer_id;
  std::uint64_t total_source_bytes{0U};
  std::vector<hlclient::assets::AssetSourceFingerprint> fingerprints;
};

struct PreparedEntityScene {
  std::shared_ptr<const hlclient::entity_render::EntitySceneRenderPackage>
      package;
  std::vector<std::shared_ptr<const hlclient::entity_render::EntityRenderFrame>>
      frames;
  std::size_t requested_studio_count{0U};
  std::size_t requested_sprite_count{0U};
  std::vector<std::size_t> interpolated_counts;
  std::vector<std::size_t> stepped_counts;
};

struct PreparedWorldScene {
  std::shared_ptr<const hlclient::world_scene_render::WorldSceneRenderPackage>
      package;
  std::optional<hlclient::world_preview::WorldPreviewSpawnCameraDescriptor>
      spawn_camera;
};

struct ProjectionHistory {
  std::vector<hlclient::entity_visual::EntityVisualProjectionState> previous;
  std::vector<hlclient::entity_visual::EntityVisualProjectionState> current;
  std::vector<hlclient::goldsrc::InterpolatedEntityFrame> playback;
};

[[nodiscard]] bool looks_like_option(const std::wstring_view value) noexcept {
  return value.size() >= 2U && value[0U] == L'-' && value[1U] == L'-';
}

[[nodiscard]] std::optional<std::string>
narrow_printable_ascii(const std::wstring_view value) {
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

[[nodiscard]] std::optional<Options> parse_options(const int argument_count,
                                                   wchar_t *arguments[]) {
  Options options;
  for (int index = 1; index < argument_count; ++index) {
    const std::wstring_view argument{arguments[index]};
    if (argument != L"--basedir" && argument != L"--game" &&
        argument != L"--map" && argument != L"--model" &&
        argument != L"--sprite" && argument != L"--fixture" &&
        argument != L"--camera" && argument != L"--controlled-entity") {
      return std::nullopt;
    }
    if (index + 1 >= argument_count) {
      return std::nullopt;
    }
    const std::wstring_view value{arguments[++index]};
    if (value.empty() || looks_like_option(value)) {
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
    } else if (argument == L"--model") {
      options.models.push_back(std::move(*narrow));
    } else if (argument == L"--sprite") {
      options.sprites.push_back(std::move(*narrow));
    } else if (argument == L"--fixture") {
      if (options.fixture) {
        return std::nullopt;
      }
      if (*narrow == "studio") {
        options.fixture = FixtureKind::studio;
      } else if (*narrow == "sprite") {
        options.fixture = FixtureKind::sprite;
      } else if (*narrow == "mixed") {
        options.fixture = FixtureKind::mixed;
      } else {
        return std::nullopt;
      }
    } else if (argument == L"--camera") {
      if (options.camera) {
        return std::nullopt;
      }
      if (*narrow == "static") {
        options.camera = CameraMode::static_camera;
      } else if (*narrow == "orbit") {
        options.camera = CameraMode::orbit;
      } else if (*narrow == "spawn") {
        options.camera = CameraMode::spawn;
      } else if (*narrow == "free-fly") {
        options.camera = CameraMode::free_flight;
      } else if (*narrow == "entity-first-person") {
        options.camera = CameraMode::entity_first_person;
      } else {
        return std::nullopt;
      }
    } else {
      if (options.controlled_entity) {
        return std::nullopt;
      }
      std::uint32_t entity_number = 0U;
      const auto conversion = std::from_chars(
          narrow->data(), narrow->data() + narrow->size(), entity_number, 10);
      if (conversion.ec != std::errc{} ||
          conversion.ptr != narrow->data() + narrow->size() ||
          entity_number == 0U) {
        return std::nullopt;
      }
      options.controlled_entity = entity_number;
    }
  }

  if (!options.base_directory || !options.game_directory ||
      !options.virtual_map || !options.fixture || !options.camera ||
      options.models.size() > kMaximumViewerVisuals ||
      options.sprites.size() > kMaximumViewerVisuals ||
      options.models.size() + options.sprites.size() == 0U ||
      options.models.size() + options.sprites.size() > kMaximumViewerVisuals) {
    return std::nullopt;
  }
  if ((*options.fixture == FixtureKind::studio &&
       (options.models.empty() || !options.sprites.empty())) ||
      (*options.fixture == FixtureKind::sprite &&
       (options.sprites.empty() || !options.models.empty())) ||
      (*options.fixture == FixtureKind::mixed &&
       (options.models.empty() || options.sprites.empty()))) {
    return std::nullopt;
  }
  if ((*options.camera == CameraMode::entity_first_person) !=
      options.controlled_entity.has_value()) {
    return std::nullopt;
  }
  return options;
}

void print_usage() {
  std::cerr << "Usage: hlclient_entity_viewer --basedir <Half-Life root> "
               "--game <directory> --map <maps/name.bsp> "
               "[--model <models/name.mdl>]... "
               "[--sprite <sprites/name.spr>]... "
                "--fixture <studio|sprite|mixed> "
                "--camera <static|orbit|spawn|free-fly|entity-first-person> "
                "[--controlled-entity <synthetic-number>]\n"
            << "  free-fly: local noclip-style diagnostic camera; no collision, "
               "physics, prediction, or network commands\n"
            << "  entity-first-person: local camera anchored to a synthetic "
               "playback entity; not live server gameplay\n";
}

void print_failure(const std::string_view classification) {
  std::cerr << "entity-viewer=" << classification << '\n';
}

[[nodiscard]] std::optional<std::uint64_t> smoke_test_frame_limit() {
  constexpr std::uint64_t maximum_frames = 1'000'000U;
#if defined(_MSC_VER)
  char *environment_value = nullptr;
  std::size_t environment_value_size = 0U;
  const auto environment_result =
      ::_dupenv_s(&environment_value, &environment_value_size,
                  "HLCLIENT_SMOKE_TEST_FRAMES");
  if (environment_result != 0) {
    throw std::runtime_error{"Unable to read the frame-limit environment"};
  }
  if (environment_value == nullptr) {
    return std::nullopt;
  }
  const std::unique_ptr<char, decltype(&std::free)> owned_environment_value{
      environment_value, &std::free};
  const std::string owned_value{owned_environment_value.get()};
  const std::string_view text{owned_value};
#else
  const char *environment_value = std::getenv("HLCLIENT_SMOKE_TEST_FRAMES");
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

class LsbBitWriter final {
public:
  void write(const std::uint32_t value, const std::size_t width) {
    for (std::size_t bit = 0U; bit < width; ++bit) {
      if ((bit_length_ & 7U) == 0U) {
        bytes_.push_back(std::byte{0U});
      }
      if (((value >> bit) & 1U) != 0U) {
        const auto byte_index = bit_length_ >> 3U;
        bytes_[byte_index] |= static_cast<std::byte>(1U << (bit_length_ & 7U));
      }
      ++bit_length_;
    }
  }

  void write_string(const std::string_view value) {
    for (const auto character : value) {
      write(static_cast<std::uint8_t>(character), 8U);
    }
    write(0U, 8U);
  }

  void align_zero() {
    while ((bit_length_ & 7U) != 0U) {
      write(0U, 1U);
    }
  }

  void resource_list_terminal_padding() {
    const auto padding_bits = 8U - (bit_length_ & 7U);
    write(0U, padding_bits);
  }

  [[nodiscard]] std::size_t bit_length() const noexcept { return bit_length_; }

  [[nodiscard]] std::vector<std::byte> take_bytes() noexcept {
    return std::move(bytes_);
  }

private:
  std::vector<std::byte> bytes_;
  std::size_t bit_length_{0U};
};

void append_u32_le(std::vector<std::byte> &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::byte>(value & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_string(std::vector<std::byte> &bytes,
                   const std::string_view value) {
  const auto source = std::as_bytes(std::span{value.data(), value.size()});
  bytes.insert(bytes.end(), source.begin(), source.end());
  bytes.push_back(std::byte{0U});
}

struct EncodedResourceList {
  std::vector<std::byte> bytes;
  std::size_t bit_length{0U};
};

[[nodiscard]] EncodedResourceList make_resource_list(const Options &options) {
  const auto count = 1U + options.models.size() + options.sprites.size();
  LsbBitWriter writer;
  writer.write(hlclient::goldsrc::kResourceListOpcode, 8U);
  writer.write(static_cast<std::uint32_t>(count), 12U);

  const auto write_entry = [&writer](const std::string_view name,
                                     const std::uint16_t index) {
    writer.write(
        static_cast<std::uint8_t>(hlclient::goldsrc::ResourceType::model), 4U);
    writer.write_string(name);
    writer.write(index, 12U);
    writer.write(0U, 24U);
    writer.write(0U, 4U);
  };
  write_entry(*options.virtual_map, 0U);
  std::uint16_t model_slot = 1U;
  for (const auto &model : options.models) {
    write_entry(model, model_slot++);
  }
  for (const auto &sprite : options.sprites) {
    write_entry(sprite, model_slot++);
  }
  writer.resource_list_terminal_padding();
  const auto bits = writer.bit_length();
  return EncodedResourceList{writer.take_bytes(), bits};
}

[[nodiscard]] std::vector<std::byte>
make_server_info_body(const Options &options) {
  std::vector<std::byte> body;
  append_u32_le(body, 48U);
  append_u32_le(body, 0x4530'0001U);
  append_u32_le(body, 0x4530'0002U);
  for (std::uint8_t value = 0U; value < 16U; ++value) {
    body.push_back(static_cast<std::byte>(value));
  }
  body.push_back(std::byte{1U});
  body.push_back(std::byte{0U});
  body.push_back(std::byte{0U});
  append_string(body, *options.game_directory);
  append_string(body, "HL Client Offline Entity Viewer");
  append_string(body, *options.virtual_map);
  append_string(body, "offline");
  body.push_back(std::byte{0U});
  return body;
}

[[nodiscard]] std::optional<hlclient::goldsrc::PrecacheManifestState>
build_manifest(
    const Options &options,
    const hlclient::local_resources::LocalResourceEnvironment &environment) {
  const auto encoded = make_resource_list(options);
  auto parsed_list = hlclient::goldsrc::ResourceListParser{}.parse(
      encoded.bytes, 0U, encoded.bit_length);
  if (!parsed_list || !parsed_list.state) {
    print_failure("resource_list_build_failed");
    return std::nullopt;
  }
  auto parsed_server = hlclient::goldsrc::ServerInfoParser{}.parse(
      make_server_info_body(options));
  if (!parsed_server || !parsed_server.state) {
    print_failure("server_info_build_failed");
    return std::nullopt;
  }
  hlclient::goldsrc::GoldSrcResourceNameMapper mapper;
  auto inventory = hlclient::goldsrc::LocalResourceInventoryBuilder{}.build(
      *parsed_list.state, mapper, environment.resolver());
  if (!inventory || !inventory.state) {
    print_failure("local_inventory_failed");
    return std::nullopt;
  }
  auto manifest = hlclient::goldsrc::PrecacheManifestBuilder{}.build(
      *parsed_list.state, *inventory.state, *parsed_server.state, mapper,
      environment);
  if (!manifest || !manifest.state ||
      !manifest.state->complete_for_supported_local_profile()) {
    print_failure("precache_manifest_incomplete");
    return std::nullopt;
  }
  return std::move(*manifest.state);
}

[[nodiscard]] bool local_source_terminal(
    const hlclient::local_assets::LocalAssetSourceOpenState state) noexcept {
  using State = hlclient::local_assets::LocalAssetSourceOpenState;
  return state == State::source_ready || state == State::cancelled ||
         state == State::timed_out || state == State::failed;
}

[[nodiscard]] std::optional<hlclient::local_assets::LocalAssetSource>
open_source(
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment> &environment,
    const hlclient::local_resources::LocalResourceLocator &locator,
    const std::uint64_t maximum_bytes) {
  hlclient::local_assets::LocalAssetSourceOpenLimits limits;
  limits.maximum_source_bytes = maximum_bytes;
  limits.maximum_chunks_per_update = 1U;
  limits.maximum_open_sources = 1U;
  hlclient::local_assets::LocalAssetSourceOpener opener;
  auto started = opener.begin(locator, environment, limits);
  if (!started || !started.operation) {
    return std::nullopt;
  }
  auto &operation = *started.operation;
  const auto now = std::chrono::steady_clock::time_point{};
  for (std::size_t update = 0U; update < kMaximumOperationUpdates &&
                                !local_source_terminal(operation.state());
       ++update) {
    operation.update(now);
  }
  if (operation.state() !=
      hlclient::local_assets::LocalAssetSourceOpenState::source_ready) {
    return std::nullopt;
  }
  return operation.take_result();
}

[[nodiscard]] std::optional<hlclient::local_resources::LocalResourceLocator>
resolve_locator(
    const hlclient::local_resources::LocalResourceEnvironment &environment,
    std::string_view virtual_name) {
  auto safe_name =
      hlclient::local_resources::LocalVirtualResourceName::create(virtual_name);
  if (!safe_name || !safe_name.name) {
    return std::nullopt;
  }
  auto resolved = environment.resolver().resolve(*safe_name.name);
  if (!resolved || !resolved.file) {
    return std::nullopt;
  }
  const auto root_id = resolved.file->root_id();
  const auto identity = resolved.file->identity();
  const auto size = resolved.file->file_size();
  resolved.file->close();
  auto locator = environment.make_locator(root_id, std::move(*safe_name.name),
                                          identity, size);
  if (!locator || !locator.locator) {
    return std::nullopt;
  }
  return std::move(*locator.locator);
}

[[nodiscard]] std::optional<hlclient::assets::WorldTextureSet>
import_world_textures(
    const hlclient::assets::WorldAsset &world,
    const std::span<const std::byte> bsp_source,
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment> &environment,
    const std::string_view failure_prefix) {
  auto started = hlclient::goldsrc::WorldTextureImportOperation::begin(
      world, bsp_source, environment);
  if (!started || !started.operation) {
    print_failure(failure_prefix);
    return std::nullopt;
  }
  auto &operation = *started.operation;
  const auto now = std::chrono::steady_clock::time_point{};
  for (std::size_t update = 0U;
       update < kMaximumOperationUpdates && !operation.terminal(); ++update) {
    operation.update(now);
  }
  if (!operation.terminal() || operation.result() == nullptr ||
      !operation.result()->complete_for_world_materials()) {
    print_failure(failure_prefix);
    return std::nullopt;
  }
  return operation.take_result();
}

[[nodiscard]] std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
build_world_render_package(hlclient::assets::WorldAsset world,
                           hlclient::assets::WorldTextureSet textures,
                           const std::span<const std::byte> bsp_source) {
  auto lightmaps =
      hlclient::goldsrc::lightmaps::GoldSrcWorldLightmapImporter::import(
          world, bsp_source);
  if (!lightmaps || !lightmaps.lightmap_set ||
      !lightmaps.lightmap_set->complete_for_world_surfaces()) {
    print_failure("world_lightmap_import_failed");
    return {};
  }
  auto built = hlclient::world_render::WorldRenderPackageBuilder{}.build(
      hlclient::assets::TexturedWorldAsset{std::move(world),
                                           std::move(textures)},
      std::move(*lightmaps.lightmap_set));
  if (!built || !built.package) {
    print_failure("world_render_package_failed");
    return {};
  }
  return std::make_shared<const hlclient::world_render::WorldRenderPackage>(
      std::move(*built.package));
}

[[nodiscard]] std::optional<PreparedWorldScene> prepare_world_scene(
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment> &environment,
    const hlclient::local_resources::LocalResourceLocator &locator,
    const std::string_view virtual_map, const CameraMode camera_mode) {
  auto source =
      open_source(environment, locator,
                  hlclient::goldsrc::bsp::kGoldSrcBspDefaultMaximumSourceBytes);
  if (!source) {
    print_failure("map_source_open_failed");
    return std::nullopt;
  }
  const auto bsp_source = source->source().bytes();
  auto parsed = hlclient::goldsrc::bsp::GoldSrcBspParser::parse(bsp_source);
  if (!parsed || !parsed.document) {
    print_failure("map_validation_failed");
    return std::nullopt;
  }
  auto document = std::move(*parsed.document);
  document.world_asset.identity.source_name = std::string{virtual_map};
  auto textures =
      import_world_textures(document.world_asset, bsp_source, environment,
                            "world_texture_import_failed");
  if (!textures) {
    return std::nullopt;
  }
  auto world_package = build_world_render_package(
      document.world_asset, std::move(*textures), bsp_source);
  if (!world_package) {
    return std::nullopt;
  }
  const hlclient::goldsrc::brush_models::GoldSrcWorldSceneBuildConfig config{
      hlclient::goldsrc::brush_models::GoldSrcWorldSceneBrushMode::off,
      camera_mode == CameraMode::spawn,
  };
  auto built_scene =
      hlclient::goldsrc::brush_models::GoldSrcWorldSceneBuilder::build(
          document, std::move(world_package), std::nullopt, config);
  if (!built_scene || !built_scene.scene_package) {
    print_failure("world_scene_build_failed");
    return std::nullopt;
  }
  std::optional<hlclient::world_preview::WorldPreviewSpawnCameraDescriptor>
      spawn_camera;
  if (built_scene.spawn_camera && built_scene.spawn_camera->descriptor) {
    const auto &source_camera = *built_scene.spawn_camera->descriptor;
    spawn_camera = hlclient::world_preview::WorldPreviewSpawnCameraDescriptor{
        source_camera.position,
        source_camera.forward,
        source_camera.up,
    };
  }
  return PreparedWorldScene{
      std::make_shared<
          const hlclient::world_scene_render::WorldSceneRenderPackage>(
          std::move(*built_scene.scene_package)),
      std::move(spawn_camera),
  };
}

[[nodiscard]] std::optional<ImportedVisual> import_visual(
    const RequestedVisual &requested,
    const std::shared_ptr<
        const hlclient::local_resources::LocalResourceEnvironment> &environment,
    const hlclient::assets::AssetImporterRegistries &registries) {
  auto source =
      open_source(environment, requested.locator,
                  hlclient::local_resources::kHardMaximumLocalResourceFileSize);
  if (!source) {
    print_failure("visual_source_open_failed");
    return std::nullopt;
  }
  auto started =
      hlclient::goldsrc::visual_assets::GoldSrcVisualAssetImportOperation::
          begin(*source, environment, registries);
  if (!started || !started.operation) {
    print_failure("visual_import_begin_failed");
    return std::nullopt;
  }
  auto &operation = *started.operation;
  const auto now = std::chrono::steady_clock::time_point{};
  for (std::size_t update = 0U;
       update < kMaximumOperationUpdates && !operation.terminal(); ++update) {
    operation.update(now);
  }
  if (!operation.terminal() || operation.result() == nullptr) {
    print_failure("visual_import_failed");
    return std::nullopt;
  }
  const auto &result = *operation.result();
  const auto expected_category =
      requested.kind == RequestedVisualKind::studio
          ? hlclient::assets::AssetImporterCategory::model
          : hlclient::assets::AssetImporterCategory::sprite;
  if (result.selected_category() != expected_category) {
    print_failure("visual_kind_mismatch");
    return std::nullopt;
  }

  ImportedVisual imported;
  imported.requested_kind = requested.kind;
  imported.importer_id = std::string{result.selected_importer_id()};
  imported.total_source_bytes =
      result.dependency_statistics().total_source_bytes;
  imported.fingerprints.assign(result.source_fingerprints().begin(),
                               result.source_fingerprints().end());
  if (const auto *model =
          std::get_if<hlclient::assets::ModelAsset>(&result.asset())) {
    imported.model =
        std::make_shared<const hlclient::assets::ModelAsset>(*model);
  } else if (const auto *sprite =
                 std::get_if<hlclient::assets::SpriteAsset>(&result.asset())) {
    imported.sprite =
        std::make_shared<const hlclient::assets::SpriteAsset>(*sprite);
  } else {
    print_failure("visual_asset_type_invalid");
    return std::nullopt;
  }
  return imported;
}

[[nodiscard]] std::optional<hlclient::goldsrc::EntitySnapshotState>
make_synthetic_snapshot(const std::size_t entity_count,
                        const std::uint32_t reference,
                        const std::int64_t server_time) {
  LsbBitWriter writer;
  writer.write(hlclient::goldsrc::kDeltaDescriptionOpcode, 8U);
  writer.write_string("entity_state_t");
  writer.write(1U, 16U);
  writer.write(1U, 3U);
  writer.write(0x7bU, 8U);
  writer.write(0x0000'0001U, 32U);
  // This otherwise-unused byte only satisfies the neutral snapshot's typed
  // object invariant. Visual values come exclusively from the explicit
  // SyntheticEntityVisualInput records below; no field spelling is read.
  writer.write_string("viewer_tag");
  writer.write(1U, 8U);
  writer.write(8U, 8U);
  writer.write(4'000U, 32U);
  writer.write(4'000U, 32U);
  writer.align_zero();
  auto parsed = hlclient::goldsrc::DeltaDescriptionParser{}.parse(
      writer.take_bytes(), 0U);
  if (!parsed || !parsed.schema) {
    if (parsed.error) {
      std::cerr << "synthetic-schema-code="
                << static_cast<unsigned int>(parsed.error->code) << '\n';
    }
    print_failure("synthetic_schema_failed");
    return std::nullopt;
  }
  const std::vector<hlclient::goldsrc::DeltaScalarValue> values{
      std::uint32_t{1U}};
  auto object =
      hlclient::goldsrc::DeltaObjectBuilder{
          {},
          hlclient::goldsrc::DeltaValueCompatibilityProfile::
              synthetic_neutral_v1}
          .build(*parsed.schema, values);
  if (!object || !object.state) {
    print_failure("synthetic_object_failed");
    return std::nullopt;
  }
  hlclient::goldsrc::DeltaSchemaRegistryBuilder schema_builder;
  if (!schema_builder.insert(*parsed.schema)) {
    print_failure("synthetic_schema_registry_failed");
    return std::nullopt;
  }
  auto schemas = std::move(schema_builder).publish();
  hlclient::goldsrc::EntityBaselineRegistryBuilder baseline_builder{
      schemas,
      {},
      hlclient::goldsrc::EntitySnapshotCompatibilityProfile::
          synthetic_neutral_v1};
  for (std::uint32_t entity = 1U; entity <= entity_count; ++entity) {
    if (!baseline_builder.insert(
            hlclient::goldsrc::EntityBaselineKey::for_entity(entity),
            hlclient::goldsrc::EntitySchemaCategory::ordinary_entity,
            *object.state)) {
      print_failure("synthetic_baseline_failed");
      return std::nullopt;
    }
  }
  auto baselines = std::move(baseline_builder).publish();
  if (!baselines || !baselines.state) {
    print_failure("synthetic_baseline_publish_failed");
    return std::nullopt;
  }
  std::vector<hlclient::goldsrc::EntitySnapshotEntityInput> entities;
  entities.reserve(entity_count);
  for (std::uint32_t entity = 1U; entity <= entity_count; ++entity) {
    entities.push_back(
        hlclient::goldsrc::EntitySnapshotEntityInput::from_baseline(
            entity, hlclient::goldsrc::EntityBaselineKey::for_entity(entity)));
  }
  auto snapshot =
      hlclient::goldsrc::EntityFullSnapshotBuilder{
          {},
          hlclient::goldsrc::EntitySnapshotCompatibilityProfile::
              synthetic_neutral_v1}
          .build(
              hlclient::goldsrc::EntitySnapshotReference::synthetic(reference),
              hlclient::goldsrc::EntityServerTime::synthetic_raw(server_time),
              *baselines.state, entities);
  if (!snapshot || !snapshot.state) {
    print_failure("synthetic_snapshot_failed");
    return std::nullopt;
  }
  return std::move(*snapshot.state);
}

[[nodiscard]] std::optional<ProjectionHistory>
make_projection_history(const std::size_t entity_count,
                        const hlclient::assets::AssetVector3 &scene_center) {
  auto previous_snapshot = make_synthetic_snapshot(entity_count, 1U, 0);
  auto current_snapshot = make_synthetic_snapshot(entity_count, 2U, 1);
  if (!previous_snapshot || !current_snapshot) {
    return std::nullopt;
  }

  const auto project =
      [entity_count,
       scene_center](const hlclient::goldsrc::EntitySnapshotState &snapshot,
                     const float origin_delta)
      -> std::optional<
          std::vector<hlclient::entity_visual::EntityVisualProjectionState>> {
    std::vector<hlclient::entity_visual::SyntheticEntityVisualInput> inputs;
    inputs.reserve(entity_count);
    for (std::uint32_t entity = 1U; entity <= entity_count; ++entity) {
      hlclient::entity_visual::SyntheticEntityVisualInput input;
      input.entity_number = entity;
      input.model_reference = hlclient::entity_visual::
          EntityVisualModelReference::synthetic_model_slot(entity);
      const auto centered_index =
          static_cast<float>(entity - 1U) -
          (static_cast<float>(entity_count) - 1.0F) * 0.5F;
      input.origin = hlclient::entity_visual::EntityVisualVector3{
          scene_center.x + centered_index * kViewerEntitySpacing + origin_delta,
          scene_center.y, scene_center.z};
      input.angles_degrees = hlclient::entity_visual::EntityVisualVector3{
          0.0F, 0.0F, origin_delta};
      input.sequence_index = 0U;
      input.studio_frame_coordinate = origin_delta < 0.0F ? 0.0F : 1.0F;
      input.body_value = 0U;
      input.skin_family_index = 0U;
      input.sprite_frame_index = 0U;
      input.scale = 1.0F;
      input.interpolation_mode =
          hlclient::entity_visual::EntityInterpolationMode::interpolate;
      inputs.push_back(input);
    }
    auto provider = hlclient::entity_visual::
        SyntheticEntityVisualProjectionProvider::create(std::move(inputs));
    if (!provider || !provider.provider) {
      print_failure("synthetic_projection_provider_failed");
      return std::nullopt;
    }
    std::vector<hlclient::entity_visual::EntityVisualProjectionState>
        projections;
    projections.reserve(entity_count);
    for (const auto &entity : snapshot.entities()) {
      auto projected = provider.provider->project(snapshot, entity);
      if (!projected || !projected.state) {
        print_failure("synthetic_projection_failed");
        return std::nullopt;
      }
      projections.push_back(std::move(*projected.state));
    }
    return projections;
  };

  auto previous = project(*previous_snapshot, -8.0F);
  auto current = project(*current_snapshot, 8.0F);
  if (!previous || !current) {
    return std::nullopt;
  }

  hlclient::goldsrc::EntitySnapshotHistoryBuilder history_builder{
      {},
      hlclient::goldsrc::EntitySnapshotCompatibilityProfile::
          synthetic_neutral_v1};
  if (!history_builder.insert(*previous_snapshot) ||
      !history_builder.insert(*current_snapshot)) {
    print_failure("synthetic_snapshot_history_failed");
    return std::nullopt;
  }
  auto published_history = history_builder.publish();
  if (!published_history || !published_history.state ||
      published_history.state->snapshot_count() != 2U) {
    print_failure("synthetic_snapshot_history_publish_failed");
    return std::nullopt;
  }
  const auto snapshots = published_history.state->snapshots();
  auto previous_time =
      hlclient::goldsrc::EntitySnapshotExplicitTime::bind_synthetic_seconds(
          snapshots[0U], 0.0);
  auto current_time =
      hlclient::goldsrc::EntitySnapshotExplicitTime::bind_synthetic_seconds(
          snapshots[1U], 1.0);
  if (!previous_time || !current_time) {
    print_failure("synthetic_snapshot_timeline_failed");
    return std::nullopt;
  }
  std::vector<hlclient::goldsrc::EntitySnapshotExplicitTime> timeline;
  timeline.reserve(2U);
  timeline.push_back(std::move(*previous_time));
  timeline.push_back(std::move(*current_time));

  auto adapted_previous =
      hlclient::goldsrc::EntityInterpolationProjectionAdapter{}.build(
          snapshots[0U], *previous);
  auto adapted_current =
      hlclient::goldsrc::EntityInterpolationProjectionAdapter{}.build(
          snapshots[1U], *current);
  if (!adapted_previous || !adapted_previous.frame || !adapted_current ||
      !adapted_current.frame) {
    const auto *error = adapted_previous.error  ? &*adapted_previous.error
                        : adapted_current.error ? &*adapted_current.error
                                                : nullptr;
    if (error != nullptr) {
      std::cerr << "projection-adapter-code="
                << hlclient::goldsrc::to_string(error->code) << '\n';
    }
    print_failure("synthetic_projection_adapter_failed");
    return std::nullopt;
  }

  constexpr std::array<double, 2U> sample_times{0.25, 0.75};
  std::vector<hlclient::goldsrc::InterpolatedEntityFrame> playback;
  playback.reserve(sample_times.size());
  for (const auto sample_time : sample_times) {
    const auto target =
        hlclient::goldsrc::EntityInterpolationTime::synthetic_seconds(
            sample_time);
    if (!target) {
      print_failure("synthetic_interpolation_time_failed");
      return std::nullopt;
    }
    const auto selected =
        hlclient::goldsrc::EntitySnapshotPairSelector{}.select(
            *published_history.state, timeline, *target);
    if (!selected || !selected.selection) {
      if (selected.error) {
        std::cerr << "snapshot-selection-code="
                  << hlclient::goldsrc::to_string(selected.error->code) << '\n';
      }
      print_failure("synthetic_snapshot_pair_selection_failed");
      return std::nullopt;
    }
    auto interpolated =
        hlclient::goldsrc::EntitySnapshotInterpolator{}.interpolate(
            *selected.selection, adapted_previous.frame->view(),
            adapted_current.frame->view());
    if (!interpolated || !interpolated.frame) {
      if (interpolated.error) {
        std::cerr << "interpolation-code="
                  << hlclient::goldsrc::to_string(interpolated.error->code)
                  << '\n';
      }
      print_failure("synthetic_entity_interpolation_failed");
      return std::nullopt;
    }
    playback.push_back(std::move(*interpolated.frame));
  }
  return ProjectionHistory{
      std::move(*previous),
      std::move(*current),
      std::move(playback),
  };
}

[[nodiscard]] std::shared_ptr<
    const hlclient::entity_visual::EntityVisualAssetLibraryState>
publish_visual_library(const hlclient::goldsrc::PrecacheManifestState &manifest,
                       const std::span<const ImportedVisual> imports,
                       const ProjectionHistory &projections) {
  constexpr std::uint64_t library_resource_id = 0x4530'1001U;
  hlclient::entity_visual::SyntheticModelSlotResolver resolver;
  hlclient::entity_visual::EntityVisualAssetLibraryBuilder builder;
  auto planned = builder.plan(library_resource_id, projections.previous,
                              projections.current, manifest, resolver);
  if (!planned || !planned.plan) {
    print_failure("visual_library_plan_failed");
    return {};
  }
  std::vector<hlclient::entity_visual::EntityVisualAssetImportCompletion>
      completions;
  completions.reserve(planned.plan->requests().size());
  for (const auto &request : planned.plan->requests()) {
    if (request.model_slot() == 0U ||
        static_cast<std::size_t>(request.model_slot()) > imports.size()) {
      print_failure("visual_library_request_invalid");
      return {};
    }
    const auto &imported = imports[request.model_slot() - 1U];
    if (imported.requested_kind == RequestedVisualKind::studio &&
        imported.model) {
      auto candidate =
          hlclient::entity_visual::EntityVisualImportedAssetCandidate::
              studio_model(request.source_key(), imported.model,
                           imported.importer_id, imported.total_source_bytes,
                           imported.fingerprints);
      completions.push_back({
          request.request_index(),
          hlclient::entity_visual::EntityVisualAssetImportCompletionStatus::
              imported,
          std::move(candidate),
      });
    } else if (imported.requested_kind == RequestedVisualKind::sprite &&
               imported.sprite) {
      auto candidate =
          hlclient::entity_visual::EntityVisualImportedAssetCandidate::sprite(
              request.source_key(), imported.sprite, imported.importer_id,
              imported.total_source_bytes, imported.fingerprints);
      completions.push_back({
          request.request_index(),
          hlclient::entity_visual::EntityVisualAssetImportCompletionStatus::
              imported,
          std::move(candidate),
      });
    } else {
      print_failure("visual_library_import_mismatch");
      return {};
    }
  }
  auto published = builder.publish(*planned.plan, completions);
  if (!published || !published.library) {
    print_failure("visual_library_publish_failed");
    return {};
  }
  return std::move(published.library);
}

[[nodiscard]] std::optional<PreparedEntityScene> build_entity_scene(
    std::shared_ptr<
        const hlclient::entity_visual::EntityVisualAssetLibraryState> library,
    const std::span<const RequestedVisual> requested,
    const hlclient::entity_render::EntityRenderResourceIdentity
        world_scene_association,
    const std::span<const hlclient::goldsrc::InterpolatedEntityFrame>
        playback) {
  using namespace hlclient;
  if (playback.size() < 2U) {
    print_failure("entity_playback_history_missing");
    return std::nullopt;
  }
  std::vector<std::shared_ptr<const entity_render::StudioModelRenderAsset>>
      studio_assets;
  std::vector<std::shared_ptr<const entity_render::SpriteRenderAsset>>
      sprite_assets;
  for (std::size_t record_index = 0U; record_index < library->records().size();
       ++record_index) {
    const auto &record = library->records()[record_index];
    const entity_render::EntityRenderResourceIdentity identity{
        record.resource_id(), record.resource_revision()};
    if (record.kind() == entity_visual::EntityVisualAssetKind::studio_model) {
      auto built = entity_render::StudioModelRenderAssetBuilder{}.build(
          *record.model_asset(), identity);
      if (!built || !built.asset) {
        print_failure("studio_render_asset_failed");
        return std::nullopt;
      }
      studio_assets.push_back(
          std::make_shared<const entity_render::StudioModelRenderAsset>(
              std::move(*built.asset)));
    } else {
      auto built = entity_render::SpriteRenderAssetBuilder{}.build(
          *record.sprite_asset(), identity);
      if (!built || !built.asset) {
        print_failure("sprite_render_asset_failed");
        return std::nullopt;
      }
      sprite_assets.push_back(
          std::make_shared<const entity_render::SpriteRenderAsset>(
              std::move(*built.asset)));
    }
  }

  entity_render::EntitySceneRenderPackageCreateInfo package_input;
  package_input.asset_library = library;
  package_input.asset_library_identity = {library->resource_id(),
                                          library->resource_revision()};
  package_input.resource_id = 0x4530'2001U;
  package_input.world_scene_association = world_scene_association;
  package_input.studio_assets = studio_assets;
  package_input.sprite_assets = sprite_assets;
  auto built_package = entity_render::EntitySceneRenderPackageBuilder{}.build(
      std::move(package_input));
  if (!built_package || !built_package.package) {
    print_failure("entity_scene_package_failed");
    return std::nullopt;
  }
  auto package =
      std::make_shared<const entity_render::EntitySceneRenderPackage>(
          std::move(*built_package.package));
  std::vector<std::shared_ptr<const entity_render::EntityRenderFrame>> frames;
  std::vector<std::size_t> interpolated_counts;
  std::vector<std::size_t> stepped_counts;
  frames.reserve(playback.size());
  interpolated_counts.reserve(playback.size());
  stepped_counts.reserve(playback.size());
  goldsrc::studio::StudioPoseCache pose_cache;
  if (!pose_cache.valid_configuration()) {
    print_failure("studio_pose_cache_invalid");
    return std::nullopt;
  }
  for (std::size_t phase = 0U; phase < playback.size(); ++phase) {
    const auto &interpolated = playback[phase];
    if (interpolated.entities().size() != requested.size()) {
      print_failure("entity_interpolation_cardinality_mismatch");
      return std::nullopt;
    }
    const entity_render::EntityRenderResourceIdentity package_identity{
        package->resource_id(), package->resource_revision()};
    entity_render::EntityRenderFrameCompositionInput composition_input;
    composition_input.expected_scene_package_identity = package_identity;
    composition_input.frame_identity = {0x4530'3001U, phase + 1U};
    composition_input.previous_time_seconds = 0.0;
    composition_input.current_time_seconds = 1.0;
    auto composed = entity_render::EntityRenderFrameComposer{}.compose(
        *package, interpolated, composition_input, pose_cache);
    if (!composed || !composed.frame) {
      if (composed.error) {
        std::cerr << "frame-composer-code="
                  << entity_render::to_string(composed.error->code) << '\n';
        if (composed.error->studio_pose_error) {
          std::cerr << "studio-pose-code="
                    << goldsrc::studio::to_string(
                           *composed.error->studio_pose_error)
                    << '\n';
        }
      }
      print_failure("entity_frame_composition_failed");
      return std::nullopt;
    }
    frames.push_back(std::make_shared<const entity_render::EntityRenderFrame>(
        std::move(*composed.frame)));
    interpolated_counts.push_back(interpolated.statistics().interpolated_count);
    stepped_counts.push_back(interpolated.statistics().stepped_count);
  }
  return PreparedEntityScene{
      std::move(package),
      std::move(frames),
      static_cast<std::size_t>(std::ranges::count_if(
          requested,
          [](const RequestedVisual &visual) {
            return visual.kind == RequestedVisualKind::studio;
          })),
      static_cast<std::size_t>(std::ranges::count_if(
          requested,
          [](const RequestedVisual &visual) {
            return visual.kind == RequestedVisualKind::sprite;
          })),
      std::move(interpolated_counts),
      std::move(stepped_counts),
  };
}

struct EntityFirstPersonSeed {
  hlclient::client::RenderCameraState camera;
  hlclient::assets::AssetVector3 explicit_eye_offset;
};

[[nodiscard]] std::optional<EntityFirstPersonSeed>
make_entity_first_person_seed(
    const hlclient::client::RenderCameraState &fallback,
    const hlclient::entity_render::EntityRenderFrame &frame,
    const std::uint32_t controlled_entity) {
  std::optional<hlclient::assets::AssetVector3> controlled_origin;
  bool duplicate_controlled_entity = false;
  const auto find_controlled = [&](const auto &instance) {
    if (instance.entity_number != controlled_entity) {
      return;
    }
    if (controlled_origin) {
      duplicate_controlled_entity = true;
      return;
    }
    controlled_origin = instance.transform.origin;
  };
  for (const auto &instance : frame.studio_instances()) {
    find_controlled(instance);
  }
  for (const auto &instance : frame.sprite_instances()) {
    find_controlled(instance);
  }
  if (!controlled_origin || duplicate_controlled_entity) {
    return std::nullopt;
  }

  std::optional<hlclient::assets::AssetVector3> facing_origin;
  auto nearest_distance_squared = std::numeric_limits<double>::infinity();
  const auto consider_facing_target = [&](const auto &instance) {
    if (instance.entity_number == controlled_entity) {
      return;
    }
    const auto x = static_cast<double>(instance.transform.origin.x) -
                   static_cast<double>(controlled_origin->x);
    const auto y = static_cast<double>(instance.transform.origin.y) -
                   static_cast<double>(controlled_origin->y);
    const auto z = static_cast<double>(instance.transform.origin.z) -
                   static_cast<double>(controlled_origin->z);
    const auto distance_squared = x * x + y * y + z * z;
    if (std::isfinite(distance_squared) && distance_squared > 0.0 &&
        distance_squared < nearest_distance_squared) {
      nearest_distance_squared = distance_squared;
      facing_origin = instance.transform.origin;
    }
  };
  for (const auto &instance : frame.studio_instances()) {
    consider_facing_target(instance);
  }
  for (const auto &instance : frame.sprite_instances()) {
    consider_facing_target(instance);
  }

  const auto eye_offset = kViewerEntityEyeOffset;
  hlclient::client::RenderCameraState seed = fallback;
  seed.position = {controlled_origin->x + eye_offset.x,
                   controlled_origin->y + eye_offset.y,
                   controlled_origin->z + eye_offset.z};
  if (facing_origin) {
    seed.target = {facing_origin->x + eye_offset.x,
                   facing_origin->y + eye_offset.y,
                   facing_origin->z + eye_offset.z};
  } else {
    const auto fallback_x = static_cast<double>(fallback.target.x) -
                            static_cast<double>(fallback.position.x);
    const auto fallback_y = static_cast<double>(fallback.target.y) -
                            static_cast<double>(fallback.position.y);
    const auto fallback_z = static_cast<double>(fallback.target.z) -
                            static_cast<double>(fallback.position.z);
    const auto length =
        std::sqrt(fallback_x * fallback_x + fallback_y * fallback_y +
                  fallback_z * fallback_z);
    if (!std::isfinite(length) || length <= 0.0) {
      return std::nullopt;
    }
    seed.target = {
        static_cast<float>(static_cast<double>(seed.position.x) +
                           fallback_x / length),
        static_cast<float>(static_cast<double>(seed.position.y) +
                           fallback_y / length),
        static_cast<float>(static_cast<double>(seed.position.z) +
                           fallback_z / length),
    };
  }
  seed.up = {0.0F, 0.0F, 1.0F};
  seed.near_plane = kViewerFirstPersonNearPlane;
  const hlclient::renderer::RenderCamera validated{
      seed.position, seed.target, seed.up,
      seed.vertical_field_of_view_radians, seed.near_plane, seed.far_plane};
  if (!hlclient::renderer::is_valid(validated)) {
    return std::nullopt;
  }
  return EntityFirstPersonSeed{seed, eye_offset};
}

[[nodiscard]] int
render_entities(PreparedWorldScene world, PreparedEntityScene prepared,
                const Options &options,
                const std::optional<std::uint64_t> frame_limit) {
  if (prepared.frames.size() < 2U ||
      prepared.interpolated_counts.size() != prepared.frames.size() ||
      prepared.stepped_counts.size() != prepared.frames.size()) {
    print_failure("entity_playback_frames_invalid");
    return 1;
  }
  std::uint64_t maximum_source_frame_revision = 0U;
  for (const auto &frame : prepared.frames) {
    if (!frame) {
      print_failure("entity_playback_frame_missing");
      return 1;
    }
    maximum_source_frame_revision =
        std::max(maximum_source_frame_revision, frame->resource_revision());
  }
  if (maximum_source_frame_revision ==
      std::numeric_limits<std::uint64_t>::max()) {
    print_failure("entity_visibility_revision_exhausted");
    return 1;
  }
  [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
  hlclient::platform::SdlWindow window{hlclient::platform::SdlWindowConfig{
      "HL Client Offline Entity Viewer",
      1280,
      720,
      frame_limit.has_value(),
  }};
  hlclient::renderer::opengl::OpenGlRenderer renderer;

  const bool interactive_camera =
      *options.camera == CameraMode::free_flight ||
      *options.camera == CameraMode::entity_first_person;
  hlclient::world_preview::WorldPreviewSceneOptions world_options;
  switch (*options.camera) {
  case CameraMode::static_camera:
    world_options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::static_camera;
    break;
  case CameraMode::orbit:
    world_options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::orbit;
    break;
  case CameraMode::spawn:
    world_options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::spawn;
    break;
  case CameraMode::free_flight:
    world_options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::free_flight;
    break;
  case CameraMode::entity_first_person:
    world_options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::entity_first_person;
    break;
  }
  world_options.visibility_mode =
      interactive_camera
          ? hlclient::world_visibility::WorldVisibilityMode::pvs_and_frustum
          : hlclient::world_visibility::WorldVisibilityMode::all;
  world_options.brush_submodels =
      hlclient::world_preview::WorldPreviewBrushSubmodelsMode::off;
  world_options.spawn_camera = world.spawn_camera;
  hlclient::world_preview::WorldPreviewSceneSource world_source{world.package,
                                                                 world_options};

  std::optional<hlclient::input::InputStateTracker> input_tracker;
  std::optional<hlclient::interactive_preview::InteractivePreviewController>
      interactive_controller;
  if (interactive_camera) {
    input_tracker.emplace();
    const auto preview_mode =
        *options.camera == CameraMode::free_flight
            ? hlclient::interactive_preview::InteractivePreviewMode::
                  free_flight_world
            : hlclient::interactive_preview::InteractivePreviewMode::
                  entity_first_person;
    auto initial_render_camera = world_source.world_state().camera();
    auto explicit_eye_offset = kViewerEntityEyeOffset;
    if (*options.camera == CameraMode::entity_first_person) {
      const auto seeded = make_entity_first_person_seed(
          initial_render_camera, *prepared.frames.front(),
          *options.controlled_entity);
      if (!seeded) {
        print_failure("entity_camera_seed_failed");
        return 1;
      }
      initial_render_camera = seeded->camera;
      explicit_eye_offset = seeded->explicit_eye_offset;
    }
    auto created = hlclient::interactive_preview::InteractivePreviewController::
        create_project_default_v1(preview_mode, initial_render_camera,
                                  options.controlled_entity,
                                  explicit_eye_offset);
    if (!created || !created.controller) {
      print_failure("interactive_camera_initialization_failed");
      return 1;
    }
    interactive_controller.emplace(std::move(*created.controller));
    if (!interactive_controller->seed_world_state_camera(world_source)) {
      print_failure("interactive_camera_initial_publication_failed");
      return 1;
    }
    const auto initial_visibility =
        world_source.update(hlclient::client::FrameTime{0.0});
    if (!initial_visibility) {
      print_failure("interactive_camera_initial_visibility_failed");
      return 1;
    }
  }

  auto previous_time = std::chrono::steady_clock::now();
  std::uint64_t rendered_frames = 0U;
  std::uint64_t entity_frame_revision_changes = 0U;
  std::optional<std::uint64_t> previous_entity_frame_revision;
  std::uint64_t first_entity_frame_revision = 0U;
  std::uint64_t last_entity_frame_revision = 0U;
  std::uint64_t next_interactive_entity_frame_revision =
      maximum_source_frame_revision + 1U;
  std::uint64_t entity_visibility_refilter_count = 0U;
  std::uint64_t capture_failure_count = 0U;
  std::uint64_t input_event_count = 0U;
  std::shared_ptr<const hlclient::entity_render::EntityRenderFrame>
      last_rendered_entity_frame;
  bool input_focus_seeded = false;
  bool running = true;
  while (running) {
    if (input_tracker) {
      input_tracker->begin_frame();
      if (!input_focus_seeded) {
        input_tracker->apply_event(
            window.focus_state() == hlclient::input::InputFocusState::focused
                ? hlclient::input::InputEvent::focus_gained()
                : hlclient::input::InputEvent::focus_lost());
        input_focus_seeded = true;
      }
    }
    hlclient::platform::PlatformEvent event{hlclient::platform::WindowEvent{}};
    while (window.poll_event(event)) {
      if (const auto *window_event =
              std::get_if<hlclient::platform::WindowEvent>(&event)) {
        if (window_event->type ==
            hlclient::platform::WindowEventType::quit_requested) {
          running = false;
        } else if (window_event->type == hlclient::platform::
                       WindowEventType::input_capture_recovery_failed) {
          print_failure("input_capture_recovery_failed");
          return 1;
        } else if (window_event->type == hlclient::platform::
                       WindowEventType::native_event_limit_exceeded) {
          print_failure("native_event_limit_exceeded");
          return 1;
        }
      } else if (input_tracker) {
        input_tracker->apply_event(
            std::get<hlclient::input::InputEvent>(event));
        ++input_event_count;
      }
    }
    if (input_tracker &&
        window.focus_state() == hlclient::input::InputFocusState::unfocused) {
      const auto recovery = window.request_relative_mouse_capture(false);
      if (!recovery) {
        ++capture_failure_count;
        std::cerr << "input-focus-release=failed diagnostic="
                  << recovery.diagnostic << '\n';
        return 1;
      }
    }
    std::optional<hlclient::input::InputSnapshot> input_snapshot;
    if (input_tracker) {
      input_snapshot.emplace(input_tracker->publish_snapshot());
      input_tracker->end_frame();
    }
    if (!running) {
      break;
    }
    const auto current_time = std::chrono::steady_clock::now();
    const auto elapsed = current_time - previous_time;
    const auto extent = window.pixel_extent();
    const auto render_extent =
        hlclient::renderer::RenderExtent{extent.width, extent.height};
    const auto extent_update = world_source.set_render_extent(render_extent);
    if (!extent_update) {
      print_failure("world_extent_update_failed");
      return 1;
    }
    const auto &entity_frame = prepared.frames[static_cast<std::size_t>(
        rendered_frames % prepared.frames.size())];
    if (interactive_controller && input_snapshot) {
      const auto bounded_input_elapsed = std::min(
          std::chrono::duration<double>{elapsed}.count(), 0.25);
      const auto camera_update = interactive_controller->update(
          *input_snapshot, bounded_input_elapsed, world_source,
          entity_frame.get());
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
        const auto capture = window.request_relative_mouse_capture(true);
        if (!capture) {
          ++capture_failure_count;
          std::cerr << "input-capture=failed diagnostic="
                    << capture.diagnostic << '\n';
          (void)window.request_relative_mouse_capture(false);
          return 1;
        }
      }
      if (camera_update.release_mouse_requested) {
        const auto release = window.request_relative_mouse_capture(false);
        if (!release) {
          ++capture_failure_count;
          std::cerr << "input-release=failed diagnostic="
                    << release.diagnostic << '\n';
          (void)window.request_relative_mouse_capture(false);
          return 1;
        }
      }
    }
    const auto world_update = world_source.update(elapsed);
    if (!world_update) {
      print_failure("world_scene_update_failed");
      return 1;
    }
    std::shared_ptr<const hlclient::entity_render::EntityRenderFrame>
        rendered_entity_frame = entity_frame;
    if (interactive_camera) {
      if (next_interactive_entity_frame_revision ==
          std::numeric_limits<std::uint64_t>::max()) {
        print_failure("entity_visibility_revision_exhausted");
        return 1;
      }
      const auto &camera = world_source.world_state().camera();
      hlclient::interactive_preview::InteractiveEntityVisibilityRefilterInput
          refilter_input;
      refilter_input.camera = {
          camera.position,
          camera.target,
          camera.up,
          camera.vertical_field_of_view_radians,
          camera.near_plane,
          camera.far_plane,
      };
      refilter_input.extent = world_source.options().visibility_extent;
      refilter_input.output_frame_revision =
          next_interactive_entity_frame_revision;
      if (*options.camera == CameraMode::entity_first_person) {
        refilter_input.retained_entity_number = options.controlled_entity;
      }
      const auto &world_visibility =
          world_source.world_state().world_visibility();
      if (world_visibility && world_visibility->camera_leaf_index()) {
        const auto camera_leaf = *world_visibility->camera_leaf_index();
        const auto &spatial = world.package->spatial_package();
        if (spatial.pvs_table().leaf_has_usable_row(camera_leaf)) {
          refilter_input.spatial_package = &spatial;
          refilter_input.camera_leaf_index = camera_leaf;
        }
      }
      const auto filtered = hlclient::interactive_preview::
          InteractiveEntityVisibilityRefilter{}
              .refilter(*prepared.package, *entity_frame, refilter_input);
      if (!filtered || !filtered.frame) {
        std::cerr
            << "entity-visibility-refilter="
            << (filtered.error
                    ? hlclient::interactive_preview::to_string(
                          filtered.error->code)
                    : std::string_view{"failed"})
            << '\n';
        return 1;
      }
      rendered_entity_frame = filtered.frame;
      ++next_interactive_entity_frame_revision;
      ++entity_visibility_refilter_count;
    }
    if (!world_source.publish_dynamic_entities(
            prepared.package, rendered_entity_frame)) {
      print_failure("dynamic_entity_publication_failed");
      return 1;
    }
    previous_time = current_time;
    auto scene =
        hlclient::client::build_render_scene(world_source.world_state());
    renderer.render(scene, render_extent);
    if (rendered_frames == 0U) {
      first_entity_frame_revision = rendered_entity_frame->resource_revision();
    }
    if (previous_entity_frame_revision &&
        *previous_entity_frame_revision !=
            rendered_entity_frame->resource_revision()) {
      ++entity_frame_revision_changes;
    }
    previous_entity_frame_revision = rendered_entity_frame->resource_revision();
    last_entity_frame_revision = rendered_entity_frame->resource_revision();
    last_rendered_entity_frame = std::move(rendered_entity_frame);
    window.swap_buffers();
    ++rendered_frames;
    if (frame_limit && rendered_frames >= *frame_limit) {
      running = false;
    }
  }

  const auto &entity_statistics = renderer.entity_statistics();
  const auto &world_statistics = renderer.statistics();
  const auto uploads = entity_statistics.studio_asset_upload_count +
                       entity_statistics.sprite_asset_upload_count;
  const auto reported_frame_index =
      rendered_frames == 0U ? 0U
                            : static_cast<std::size_t>((rendered_frames - 1U) %
                                                       prepared.frames.size());
  const auto &reported_source_frame = *prepared.frames[reported_frame_index];
  const auto &reported_frame = last_rendered_entity_frame
                                   ? *last_rendered_entity_frame
                                   : reported_source_frame;
  const auto &frame_statistics = reported_frame.statistics();
  const auto &package_statistics = prepared.package->statistics();
  std::cout << "[entity] snapshots=2\n";
  std::cout << "[entity] sample-alpha="
            << reported_source_frame.interpolation().alpha
            << '\n';
  std::cout << "[entity] projected=" << frame_statistics.candidate_count
            << '\n';
  std::cout << "[entity] interpolated="
            << prepared.interpolated_counts[reported_frame_index] << '\n';
  std::cout << "[entity] stepped="
            << prepared.stepped_counts[reported_frame_index] << '\n';
  std::cout << "[entity] visual-assets="
            << package_statistics.visual_asset_count << '\n';
  std::cout << "[entity] studio=" << frame_statistics.studio_instance_count
            << '\n';
  std::cout << "[entity] sprites=" << frame_statistics.sprite_instance_count
            << '\n';
  std::cout << "[entity] unsupported="
            << frame_statistics.unsupported_instance_count << '\n';
  std::cout << "[entity] visible=" << frame_statistics.visible_count << '\n';
  std::cout << "[pose] models=" << package_statistics.studio_asset_count
            << '\n';
  std::cout << "[pose] poses=" << frame_statistics.pose_count << '\n';
  std::cout << "[pose] bones=" << frame_statistics.total_bone_matrix_count
            << '\n';
  std::cout << "[render] studio-draws=" << entity_statistics.studio_draw_count
            << '\n';
  std::cout << "[render] sprite-draws=" << entity_statistics.sprite_draw_count
            << '\n';
  std::cout << "[render] entity-uploads=" << uploads << '\n';
  std::cout << "map-validation=complete\n";
  std::cout << "world-upload=" << world_statistics.upload_count << '\n';
  std::cout << "world-scene-upload=" << world_statistics.scene_upload_count
            << '\n';
  std::cout << "requested-studio=" << prepared.requested_studio_count << '\n';
  std::cout << "requested-sprite=" << prepared.requested_sprite_count << '\n';
  std::cout << "pose-fallbacks=0\n";
  std::cout << "entity-draw-commands=" << reported_frame.statistics().draw_count
            << '\n';
  std::cout << "entity-frame-first-revision=" << first_entity_frame_revision
            << '\n';
  std::cout << "entity-frame-last-revision=" << last_entity_frame_revision
            << '\n';
  std::cout << "entity-frame-revision-changes=" << entity_frame_revision_changes
            << '\n';
  std::cout << "entity-visibility-refilters="
            << entity_visibility_refilter_count << '\n';
  std::cout << "network-operations=0\n";
  std::cout << "writes=0\n";
  std::cout << "frames=" << rendered_frames << '\n';
  std::cout << "entity-uploads=" << uploads << '\n';
  std::cout << "studio-draws=" << entity_statistics.studio_draw_count << '\n';
  std::cout << "sprite-draws=" << entity_statistics.sprite_draw_count << '\n';
  if (interactive_controller && input_tracker) {
    const auto &camera_statistics = interactive_controller->statistics();
    std::cout << "input-frames=" << input_tracker->published_frame_count()
              << '\n';
    std::cout << "input-events=" << input_event_count << '\n';
    std::cout << "camera-updates="
              << camera_statistics.published_update_count << '\n';
    std::cout << "camera-changes=" << camera_statistics.changed_camera_count
              << '\n';
    std::cout << "camera-anchor-missing="
              << camera_statistics.anchor_missing_count << '\n';
    std::cout << "capture-failures=" << capture_failure_count << '\n';
  }

  if ((frame_limit && rendered_frames != *frame_limit) ||
      rendered_frames == 0U || uploads == 0U ||
      !entity_statistics.entity_scene_present ||
      entity_statistics.entity_frame_count != rendered_frames ||
      world_statistics.upload_count != 1U ||
      world_statistics.scene_upload_count != 1U ||
      world_statistics.rendered_frame_count != rendered_frames ||
      !world_statistics.world_present || !world_statistics.scene_present ||
      (rendered_frames >= 2U && entity_frame_revision_changes == 0U) ||
      (prepared.requested_studio_count != 0U &&
       entity_statistics.studio_draw_count == 0U) ||
      (prepared.requested_sprite_count != 0U &&
       entity_statistics.sprite_draw_count == 0U)) {
    print_failure("render_incomplete");
    return 1;
  }
  return 0;
}

[[nodiscard]] int run_viewer(const int argument_count, wchar_t *arguments[]) {
  const auto options = parse_options(argument_count, arguments);
  if (!options) {
    print_usage();
    return 2;
  }
  const auto frame_limit = smoke_test_frame_limit();

  auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
      *options->base_directory, *options->game_directory);
  if (!roots || !roots.roots) {
    print_failure("invalid_local_roots");
    return 1;
  }
  hlclient::local_resources::LocalResourceResolverLimits resolver_limits;
  resolver_limits.maximum_file_size =
      hlclient::local_resources::kHardMaximumLocalResourceFileSize;
  auto created_environment =
      hlclient::local_resources::LocalResourceEnvironment::create(
          std::move(*roots.roots), resolver_limits);
  if (!created_environment || !created_environment.environment) {
    print_failure("local_environment_failed");
    return 1;
  }
  auto environment = std::shared_ptr<
      const hlclient::local_resources::LocalResourceEnvironment>{
      std::move(created_environment.environment)};

  auto map_locator = resolve_locator(*environment, *options->virtual_map);
  if (!map_locator) {
    print_failure("map_resolution_failed");
    return 1;
  }
  auto world = prepare_world_scene(environment, *map_locator,
                                   *options->virtual_map, *options->camera);
  if (!world || !world->package) {
    return 1;
  }
  auto manifest = build_manifest(*options, *environment);
  if (!manifest) {
    return 1;
  }

  std::vector<RequestedVisual> requested;
  requested.reserve(options->models.size() + options->sprites.size());
  std::uint16_t model_slot = 1U;
  const auto append_requested = [&](const RequestedVisualKind kind,
                                    const std::string &virtual_name) -> bool {
    auto locator = resolve_locator(*environment, virtual_name);
    if (!locator) {
      print_failure("visual_resolution_failed");
      return false;
    }
    requested.push_back(
        {kind, virtual_name, model_slot++, std::move(*locator)});
    return true;
  };
  for (const auto &model : options->models) {
    if (!append_requested(RequestedVisualKind::studio, model)) {
      return 1;
    }
  }
  for (const auto &sprite : options->sprites) {
    if (!append_requested(RequestedVisualKind::sprite, sprite)) {
      return 1;
    }
  }

  hlclient::assets::AssetImporterRegistries registries;
  if (!hlclient::goldsrc::register_builtin_asset_importers(registries)) {
    print_failure("importer_registration_failed");
    return 1;
  }
  std::vector<ImportedVisual> imports;
  imports.reserve(requested.size());
  for (const auto &visual : requested) {
    auto imported = import_visual(visual, environment, registries);
    if (!imported) {
      return 1;
    }
    imports.push_back(std::move(*imported));
  }
  const auto &world_bounds = world->package->bounds();
  const hlclient::assets::AssetVector3 world_center{
      (world_bounds.minimum.x + world_bounds.maximum.x) * 0.5F,
      (world_bounds.minimum.y + world_bounds.maximum.y) * 0.5F,
      (world_bounds.minimum.z + world_bounds.maximum.z) * 0.5F,
  };
  auto projections = make_projection_history(requested.size(), world_center);
  if (!projections) {
    return 1;
  }
  auto library = publish_visual_library(*manifest, imports, *projections);
  if (!library) {
    return 1;
  }
  auto prepared = build_entity_scene(
      std::move(library), requested,
      {world->package->resource_id(), world->package->resource_revision()},
      projections->playback);
  environment.reset();
  if (!prepared) {
    return 1;
  }
  return render_entities(std::move(*world), std::move(*prepared), *options,
                         frame_limit);
}

} // namespace

int wmain(const int argument_count, wchar_t *arguments[]) {
  std::cout.imbue(std::locale::classic());
  std::cerr.imbue(std::locale::classic());
  try {
    return run_viewer(argument_count, arguments);
  } catch (const hlclient::renderer::opengl::OpenGlRendererError &error) {
    std::cerr << "opengl-render="
              << hlclient::renderer::opengl::to_string(error.code()) << '\n';
  } catch (const std::bad_alloc &) {
    print_failure("allocation_failed");
  } catch (const std::exception &) {
    print_failure("failed");
  } catch (...) {
    print_failure("failed");
  }
  return 1;
}
