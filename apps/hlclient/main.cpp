#include <hlclient/app/explicit_file_authentication_provider.hpp>
#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/app/precache_manifest_exit_policy.hpp>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/app/runtime_replay_scene_source.hpp>
#include <hlclient/app/steam_authentication_provider.hpp>
#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/asset_manager.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/core/log.hpp>
#include <hlclient/core/version.hpp>
#include <hlclient/filesystem/game_paths.hpp>
#include <hlclient/filesystem/rooted_file_system.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/goldsrc/precache_manifest_stage.hpp>
#include <hlclient/goldsrc/world_render/world_render_package_stage.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import_stage.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_source.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using hlclient::core::LogLevel;

inline constexpr std::uint64_t kProductionGoldSrcBspMaximumSourceBytes =
    hlclient::goldsrc::bsp::kGoldSrcBspDefaultMaximumSourceBytes;
inline constexpr std::size_t kProductionGoldSrcBspAssetDispatchEvents = 1'024U;
static_assert(
    kProductionGoldSrcBspMaximumSourceBytes ==
    hlclient::goldsrc::bsp::GoldSrcBspImportLimits{}.maximum_source_bytes);
static_assert(kProductionGoldSrcBspMaximumSourceBytes <=
              hlclient::local_resources::kHardMaximumLocalResourceFileSize);
static_assert(
    kProductionGoldSrcBspMaximumSourceBytes /
            hlclient::local_assets::kDefaultLocalAssetSourceReadChunkBytes +
        8U <
    kProductionGoldSrcBspAssetDispatchEvents);

[[nodiscard]] hlclient::goldsrc::PrecacheAssetDispatchStageConfig
production_bsp_asset_dispatch_config() {
  hlclient::goldsrc::PrecacheAssetDispatchStageConfig config;
  config.source_open.maximum_source_bytes =
      kProductionGoldSrcBspMaximumSourceBytes;
  config.maximum_stage_events = kProductionGoldSrcBspAssetDispatchEvents;
  return config;
}

[[nodiscard]] hlclient::goldsrc::WorldTextureImportStageConfig
production_world_texture_import_config() {
  hlclient::goldsrc::WorldTextureImportStageConfig config;
  config.asset_dispatch = production_bsp_asset_dispatch_config();
  return config;
}

[[nodiscard]] hlclient::goldsrc::WorldRenderPackageStageConfig
production_world_render_package_config(
    const hlclient::core::CommandLineOptions &options) {
  hlclient::goldsrc::WorldRenderPackageStageConfig config;
  config.world_textures = production_world_texture_import_config();
  config.build_world_spatial_scene =
      options.stop_after ==
      hlclient::core::ConnectionStopPoint::world_spatial_scene;
  config.world_scene.brushes =
      options.brush_submodels ==
              hlclient::core::BrushSubmodelsOption::static_initial
          ? hlclient::goldsrc::brush_models::GoldSrcWorldSceneBrushMode::
                static_initial
          : hlclient::goldsrc::brush_models::GoldSrcWorldSceneBrushMode::off;
  config.world_scene.extract_spawn =
      options.world_camera == hlclient::core::WorldCameraOption::spawn;
  return config;
}

[[nodiscard]] hlclient::world_preview::WorldPreviewSceneOptions
world_preview_options(
    const hlclient::core::CommandLineOptions &options,
    const std::optional<
        hlclient::goldsrc::brush_models::GoldSrcSpawnCameraExtractionResult>
        &spawn_camera) {
  hlclient::world_preview::WorldPreviewSceneOptions preview;
  switch (options.world_visibility) {
  case hlclient::core::WorldVisibilityOption::all:
    preview.visibility_mode =
        hlclient::world_visibility::WorldVisibilityMode::all;
    break;
  case hlclient::core::WorldVisibilityOption::frustum:
    preview.visibility_mode =
        hlclient::world_visibility::WorldVisibilityMode::frustum_only;
    break;
  case hlclient::core::WorldVisibilityOption::pvs:
    preview.visibility_mode =
        hlclient::world_visibility::WorldVisibilityMode::pvs_only;
    break;
  case hlclient::core::WorldVisibilityOption::pvs_frustum:
    preview.visibility_mode =
        hlclient::world_visibility::WorldVisibilityMode::pvs_and_frustum;
    break;
  }
  preview.brush_submodels =
      options.brush_submodels ==
              hlclient::core::BrushSubmodelsOption::static_initial
          ? hlclient::world_preview::WorldPreviewBrushSubmodelsMode::
                static_instances
          : hlclient::world_preview::WorldPreviewBrushSubmodelsMode::off;
  switch (options.world_camera) {
  case hlclient::core::WorldCameraOption::static_camera:
    preview.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::static_camera;
    break;
  case hlclient::core::WorldCameraOption::orbit:
    preview.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::orbit;
    break;
  case hlclient::core::WorldCameraOption::spawn:
    preview.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::spawn;
    break;
  }
  if (spawn_camera && spawn_camera->descriptor) {
    const auto &descriptor = *spawn_camera->descriptor;
    preview.spawn_camera =
        hlclient::world_preview::WorldPreviewSpawnCameraDescriptor{
            descriptor.position,
            descriptor.forward,
            descriptor.up,
        };
  }
  return preview;
}

class BootstrapSceneSource final : public hlclient::client::IClientSceneSource {
public:
  [[nodiscard]] hlclient::client::SceneUpdateResult
  update(const hlclient::client::FrameTime elapsed) override {
    world_state_.advance(elapsed);
    return {};
  }

  [[nodiscard]] const hlclient::client::ClientWorldState &
  world_state() const noexcept override {
    return world_state_;
  }

  [[nodiscard]] hlclient::client::ClientWorldState &
  mutable_world_state() noexcept {
    return world_state_;
  }

private:
  hlclient::client::ClientWorldState world_state_;
};

[[nodiscard]] std::string_view runtime_freshness_name(
    const hlclient::client::RuntimeObservationFreshness freshness) noexcept {
  using Freshness = hlclient::client::RuntimeObservationFreshness;
  switch (freshness) {
  case Freshness::unavailable:
    return "unavailable";
  case Freshness::retained:
    return "retained";
  case Freshness::observed_in_record:
    return "observed_in_record";
  }
  return "unknown";
}

[[nodiscard]] const hlclient::client::RuntimePacketEntityObservation *
find_runtime_entity(
    const hlclient::client::RuntimeClientObservationState &observation,
    const std::uint32_t entity_number) noexcept {
  const auto found = std::find_if(
      observation.packet_entities.begin(), observation.packet_entities.end(),
      [entity_number](const auto &entity) {
        return entity.entity_number == entity_number;
      });
  return found == observation.packet_entities.end() ? nullptr : &*found;
}

[[nodiscard]] int
report_runtime_replay(const hlclient::app::RuntimeReplaySceneSource &source) {
  const auto summary = source.summary();
  const auto &world = source.world_state();
  const auto &observation = world.runtime_observation();
  std::cout << "runtime_replay source_mode="
            << hlclient::app::to_string(summary.input_kind);
  if (summary.input_kind ==
      hlclient::app::RuntimeReplaySourceInputKind::builtin_fixture) {
    std::cout << " fixture=" << hlclient::goldsrc::to_string(summary.fixture);
  } else if (summary.capture) {
    std::cout
        << " purpose=functional_runtime_capture"
        << " campaign_evidence_eligible=false"
        << " run_id=" << summary.capture->run_id
        << " corpus_hash=" << summary.capture->corpus_structural_sha256
        << " delivered_datagrams=" << summary.capture->delivered_datagram_count
        << " replay_payloads=" << summary.capture->replayed_payload_count
        << " reassembled_payloads="
        << summary.capture->reassembled_payload_count
        << " decompressed_payloads="
        << summary.capture->decompressed_payload_count << " signon_payloads="
        << summary.capture->decoded_server_signon_payload_count
        << " schemas=" << summary.capture->schema_count
        << " baseline_entities=" << summary.capture->baseline_entity_count
        << " baseline_instanced=" << summary.capture->baseline_instanced_count
        << " baseline_payload=" << summary.capture->baseline_payload_ordinal
        << " baseline_sequence=" << summary.capture->baseline_source_sequence
        << " baseline_end="
        << summary.capture->baseline_end_cursor.byte_offset() << ':'
        << summary.capture->baseline_end_cursor.bit_offset()
        << " signon_cursor="
        << summary.capture->signon_cursor.replay_payload_ordinal << ':'
        << summary.capture->signon_cursor.byte_offset << ':'
        << summary.capture->signon_cursor.bit_offset;
  }
  std::cout << " profile=" << hlclient::goldsrc::to_string(summary.profile)
            << " generation=" << summary.generation
            << " input=" << summary.input_records
            << " applied=" << summary.applied_records
            << " failed=" << summary.failed_records
            << " visual_failed=" << summary.visual_failed_records
            << " pending=" << summary.pending_records
            << " updates=" << summary.application_updates
            << " finish_count=" << summary.session_finish_count
            << " publication_revision=" << world.runtime_publication_revision()
            << " entities="
            << (observation ? observation->packet_entities.size() : 0U)
            << " entity2_x=";
  const auto *entity_two =
      observation ? find_runtime_entity(*observation, 2U) : nullptr;
  if (entity_two && entity_two->origin.x) {
    std::cout << *entity_two->origin.x;
  } else {
    std::cout << "unavailable";
  }
  std::cout << " health=";
  if (observation && observation->receiving_client &&
      observation->receiving_client->health) {
    std::cout << *observation->receiving_client->health;
  } else {
    std::cout << "unavailable";
  }
  const auto print_observed_vector = [](const auto &value) {
    if (value.complete())
      std::cout << *value.x << ',' << *value.y << ',' << *value.z;
    else
      std::cout << "unavailable";
  };
  std::cout << " client_origin=";
  if (observation && observation->receiving_client)
    print_observed_vector(observation->receiving_client->origin);
  else
    std::cout << "unavailable";
  std::cout << " client_velocity=";
  if (observation && observation->receiving_client)
    print_observed_vector(observation->receiving_client->velocity);
  else
    std::cout << "unavailable";
  std::cout << " client_view_offset=";
  if (observation && observation->receiving_client)
    print_observed_vector(observation->receiving_client->view_offset);
  else
    std::cout << "unavailable";
  std::cout << " slot2_clip=";
  if (observation && observation->weapon_slots.size() > 2U &&
      observation->weapon_slots[2U].clip) {
    std::cout << *observation->weapon_slots[2U].clip;
  } else {
    std::cout << "unavailable";
  }
  std::cout << " time_freshness="
            << (observation ? runtime_freshness_name(
                                  observation->server_time_metadata.freshness)
                            : "unavailable")
            << " client_freshness="
            << (observation ? runtime_freshness_name(
                                  observation->client_metadata.freshness)
                            : "unavailable")
            << " entity_freshness="
            << (observation ? runtime_freshness_name(
                                  observation->entity_metadata.freshness)
                            : "unavailable")
            << " terminal=" << hlclient::app::to_string(summary.state)
            << " canonical_hash="
            << (observation ? observation->canonical_state_hash : 0U);
  if (summary.input_kind == hlclient::app::RuntimeReplaySourceInputKind::
                                functional_stock_capture &&
      observation) {
    const auto print_source =
        [](const std::string_view name,
           const hlclient::client::RuntimeSubstateMetadata &metadata) {
          std::cout << ' ' << name << '=';
          if (!metadata.source) {
            std::cout << "unavailable";
            return;
          }
          std::cout << metadata.source->record_ordinal << ':'
                    << metadata.source->source_transport_sequence << ':'
                    << metadata.source->start_bit_offset << '-'
                    << metadata.source->end_bit_offset;
        };
    print_source("time_source", observation->server_time_metadata);
    print_source("client_source", observation->client_metadata);
    print_source("entity_source", observation->entity_metadata);
  }
  if (source.local_assets()) {
    const auto &local = source.local_assets()->summary();
    std::cout
        << " visual_binding=public_goldsrc48_model_slot"
        << " asset_source=explicit_read_only_local_root"
        << " map=" << local.map << " precache=" << local.precache_completeness
        << " resources=" << local.captured_resources
        << " imported_studio=" << local.imported_studio
        << " imported_sprites=" << local.imported_sprites
        << " size_matches=" << local.size_matches
        << " size_unavailable=" << local.size_unavailable
        << " historical_byte_identity=not_established"
        << " visual_semantic_hash="
        << (observation ? hlclient::client::runtime_observation_visual_hash(
                              *observation)
                        : 0U)
        << " resolved_instances=" << local.resolved_instances
        << " rendered_instances=" << local.rendered_instances
        << " unsupported_instances=" << local.unsupported_instances
        << " entity_culled=0"
        << " pacing="
        << (source.presentation_paced() ? "observed_completion_prefix_offsets"
                                        : "unpaced")
        << " presentation_seconds=" << source.presentation_duration_seconds()
        << " camera=spectator_first_supported_model_once"
        << " projected_frames=" << local.projected_frames
        << " unchanged_records=" << local.unchanged_records;
    for (const auto &[status, count] : local.coverage) {
      std::cout << ' ' << hlclient::app::to_string(status) << '=' << count;
    }
    for (const auto &[slot, count] : local.rendered_model_slots) {
      std::cout << " model_slot_" << slot << '=' << local.model_names.at(slot)
                << ':' << count;
    }
  }
  if (summary.visual) {
    std::cout << " visual_binding="
              << hlclient::app::to_string(summary.visual->visual_binding)
              << " asset_source="
              << hlclient::app::to_string(summary.visual->asset_source)
              << " stock_model_binding="
              << hlclient::app::to_string(summary.visual->stock_model_binding)
              << " presented_runtime_revision="
              << summary.visual->presented_runtime_revision
              << " visual_frame_revision="
              << summary.visual->visual_frame_revision
              << " static_resource_id=" << summary.visual->static_resource_id
              << " static_resource_revision="
              << summary.visual->static_resource_revision
              << " projected_frames=" << summary.visual->projected_frame_count
              << " unchanged_records=" << summary.visual->unchanged_record_count
              << " resource_builds=" << summary.visual->resource_build_count;
  }
  if (summary.terminal_error) {
    std::cout << " error="
              << hlclient::app::to_string(summary.terminal_error->code);
    if (summary.terminal_error->session_error) {
      const auto &session_error = *summary.terminal_error->session_error;
      std::cout << " session_error="
                << hlclient::goldsrc::to_string(session_error.code)
                << " recovery="
                << hlclient::goldsrc::to_string(session_error.recovery);
      if (session_error.record_ordinal) {
        std::cout << " record=" << *session_error.record_ordinal;
      }
      if (session_error.decoder_error) {
        std::cout << " decoder_error="
                  << hlclient::goldsrc::to_string(*session_error.decoder_error);
      }
      if (session_error.clientdata_error) {
        std::cout << " clientdata_error="
                  << hlclient::goldsrc::to_string(
                         *session_error.clientdata_error);
      }
      if (session_error.delta_error) {
        std::cout << " delta_error="
                  << hlclient::goldsrc::to_string(*session_error.delta_error);
      }
      if (session_error.decoder_cursor) {
        std::cout << " decoder_cursor="
                  << session_error.decoder_cursor->byte_offset() << ':'
                  << session_error.decoder_cursor->bit_offset();
      }
      if (session_error.decoder_wire_opcode) {
        std::cout << " decoder_opcode="
                  << static_cast<unsigned int>(
                         *session_error.decoder_wire_opcode);
      }
    }
  }
  std::cout << '\n' << std::flush;

  if (summary.state == hlclient::app::RuntimeReplaySourceState::completed) {
    return 0;
  }
  return summary.state == hlclient::app::RuntimeReplaySourceState::stopped ? 3
                                                                           : 1;
}

[[nodiscard]] std::vector<std::string>
command_line_arguments(const int argument_count,
#ifdef _WIN32
                       wchar_t *arguments[])
#else
                       char *arguments[])
#endif
{
  std::vector<std::string> result;
  if (argument_count > 1) {
    result.reserve(static_cast<std::size_t>(argument_count - 1));
  }
  for (int index = 1; index < argument_count; ++index) {
#ifdef _WIN32
    const std::wstring_view wide_argument{arguments[index]};
    if (wide_argument.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw std::length_error{"Command-line argument is too long"};
    }
    if (wide_argument.empty()) {
      result.emplace_back();
      continue;
    }

    const int wide_size = static_cast<int>(wide_argument.size());
    const int required_size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argument.data(),
                            wide_size, nullptr, 0, nullptr, nullptr);
    if (required_size <= 0) {
      throw std::runtime_error{
          "Unable to convert a command-line argument to UTF-8"};
    }

    std::string converted(static_cast<std::size_t>(required_size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argument.data(),
                            wide_size, converted.data(), required_size, nullptr,
                            nullptr) != required_size) {
      throw std::runtime_error{
          "Unable to convert a command-line argument to UTF-8"};
    }
    result.push_back(std::move(converted));
#else
    result.emplace_back(arguments[index]);
#endif
  }
  return result;
}

[[nodiscard]] std::vector<std::string_view>
argument_views(const std::vector<std::string> &arguments) {
  std::vector<std::string_view> result;
  result.reserve(arguments.size());
  for (const auto &argument : arguments) {
    result.emplace_back(argument);
  }
  return result;
}

[[nodiscard]] std::filesystem::path
path_from_utf8(const std::string_view text) {
#ifdef _WIN32
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::length_error{"Filesystem path is too long"};
  }
  if (text.empty()) {
    return {};
  }

  const int utf8_size = static_cast<int>(text.size());
  const int required_size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), utf8_size, nullptr, 0);
  if (required_size <= 0) {
    throw std::runtime_error{"Unable to convert a UTF-8 filesystem path"};
  }

  std::wstring converted(static_cast<std::size_t>(required_size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), utf8_size,
                          converted.data(), required_size) != required_size) {
    throw std::runtime_error{"Unable to convert a UTF-8 filesystem path"};
  }
  return std::filesystem::path{std::move(converted)};
#else
  return std::filesystem::path{text};
#endif
}

void print_version() {
  std::cout << hlclient::core::kApplicationName << '\n'
            << "Version: " << hlclient::core::kVersion << '\n'
            << "Platform: " << hlclient::core::build_platform() << '\n'
            << std::flush;
}

[[nodiscard]] std::optional<std::uint64_t> smoke_test_frame_limit() {
  constexpr std::uint64_t maximum_frames = 1'000'000;
#if defined(_MSC_VER)
  char *environment_value = nullptr;
  std::size_t environment_value_size = 0;
  const auto environment_result =
      ::_dupenv_s(&environment_value, &environment_value_size,
                  "HLCLIENT_SMOKE_TEST_FRAMES");
  if (environment_result != 0) {
    throw std::runtime_error{"Unable to read HLCLIENT_SMOKE_TEST_FRAMES"};
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

  std::uint64_t frames = 0;
  const auto conversion =
      std::from_chars(text.data(), text.data() + text.size(), frames, 10);
  if (conversion.ec != std::errc{} ||
      conversion.ptr != text.data() + text.size() || frames == 0 ||
      frames > maximum_frames) {
    throw std::invalid_argument{
        "Invalid HLCLIENT_SMOKE_TEST_FRAMES value (expected 1..1000000)"};
  }
  return frames;
}

void log_renderer_information(const hlclient::renderer::IRenderer &renderer) {
  const auto &renderer_info = renderer.information();
  hlclient::core::log(LogLevel::info,
                      "Renderer vendor: " + renderer_info.vendor);
  hlclient::core::log(LogLevel::info, "Renderer: " + renderer_info.device);
  hlclient::core::log(LogLevel::info,
                      "Renderer version: " + renderer_info.version);
}

void log_challenge_trace(const hlclient::goldsrc::ChallengeTraceEvent &event) {
  using Classification = hlclient::goldsrc::ChallengeTraceClassification;
  if (event.classification == Classification::exchange_started ||
      event.classification == Classification::request_send_started ||
      event.classification == Classification::receive_would_block) {
    return;
  }

  std::string direction;
  std::string classification;
  switch (event.classification) {
  case Classification::request_sent:
    direction = "TX";
    classification = "connectionless getchallenge";
    break;
  case Classification::wrong_endpoint_ignored:
    direction = "RX";
    classification = "wrong endpoint ignored";
    break;
  case Classification::response_truncated:
    direction = "RX";
    classification = "truncated connectionless response";
    break;
  case Classification::response_rejected:
    direction = "RX";
    classification = "rejected connectionless response";
    break;
  case Classification::challenge_accepted:
    direction = "RX";
    classification = "connectionless challenge";
    break;
  case Classification::exchange_timed_out:
    classification = "challenge exchange timed out";
    break;
  case Classification::exchange_cancelled:
    classification = "challenge exchange cancelled";
    break;
  case Classification::network_failure:
    classification = "network failure";
    break;
  case Classification::protocol_failure:
    classification = "protocol failure";
    break;
  case Classification::exchange_started:
  case Classification::request_send_started:
  case Classification::receive_would_block:
    return;
  }

  std::string message{"[net] "};
  if (!direction.empty()) {
    message += direction + ' ';
  }
  message += event.endpoint.to_string() + ' ' + classification;
  if (event.datagram_size != 0U) {
    message += ", " + std::to_string(event.datagram_size) + " bytes";
  }
  if (event.attempt != 0U) {
    message += ", attempt " + std::to_string(event.attempt);
  }
  message += ", elapsed " + std::to_string(event.elapsed.count()) + " ms";
  if (!event.escaped_preview.empty() &&
      event.classification != Classification::challenge_accepted) {
    message += ", preview=" + event.escaped_preview;
  }
  if (!event.context.empty()) {
    message += ", " + event.context;
  }
  hlclient::core::log(LogLevel::info, message);
}

void log_connect_trace(
    const hlclient::goldsrc::ConnectRequestTraceEvent &event) {
  if (event.state !=
      hlclient::goldsrc::ConnectRequestStageState::request_sent) {
    return;
  }

  std::string message = "[net] TX " + event.endpoint.to_string() +
                        " connectionless connect request, " +
                        std::to_string(event.datagram_size) + " bytes";
  hlclient::core::log(LogLevel::info, message);
  hlclient::core::log(LogLevel::info, "[session] connect_sent=true");
  hlclient::core::log(
      LogLevel::info,
      "[net] protocol=" + std::to_string(event.protocol) + " challenge=" +
          std::to_string(event.challenge) + " protocol-info fields=" +
          std::to_string(event.protocol_info_field_names.size()) + " bytes=" +
          std::to_string(event.protocol_info_size) + " user-info fields=" +
          std::to_string(event.user_info_field_names.size()) + " bytes=" +
          std::to_string(event.user_info_size) + " authentication=" +
          hlclient::goldsrc::format_authentication_redaction(
              event.authentication_size));
}

void log_steam_authentication_trace(
    const hlclient::app::SteamAuthenticationTraceEvent &event) {
  std::string message =
      "[auth] provider=steam api=" +
      std::string{hlclient::app::kSteamLegacyAuthenticationApi} +
      " status=" + std::string{hlclient::app::to_string(event.status)};
  if (event.error) {
    message += " error=" + std::string{hlclient::auth::to_string(*event.error)};
  }
  if (event.material_size != 0U) {
    message += " material-size=" + std::to_string(event.material_size);
  }
  if (event.status ==
      hlclient::app::SteamAuthenticationTraceStatus::operation_started) {
    message += " provider_begin_observed=true";
  }
  if (event.status ==
      hlclient::app::SteamAuthenticationTraceStatus::api_initializing) {
    message += " steam_api_init_attempted=true";
  }
  if (event.status ==
      hlclient::app::SteamAuthenticationTraceStatus::api_initialized) {
    message += " steam_api_initialized=true";
  }
  if (event.status ==
      hlclient::app::SteamAuthenticationTraceStatus::material_acquired) {
    message += " fresh_material_acquired=true";
  }
  hlclient::core::log(
      event.status == hlclient::app::SteamAuthenticationTraceStatus::failed
          ? LogLevel::error
          : LogLevel::info,
      message);
}

void log_connect_response_trace(
    const hlclient::goldsrc::ConnectResponseTraceEvent &event) {
  using Classification = hlclient::goldsrc::ConnectResponseTraceClassification;
  if (event.classification == Classification::wait_started ||
      event.classification == Classification::receive_would_block) {
    return;
  }

  std::string classification;
  switch (event.classification) {
  case Classification::wrong_endpoint_ignored:
    classification = "wrong endpoint ignored";
    break;
  case Classification::unrelated_connectionless_ignored:
    classification = "unrelated connectionless packet ignored";
    break;
  case Classification::connect_accepted:
    classification = "connectionless connect-accepted";
    break;
  case Classification::connect_rejected:
    classification = "connectionless connect-rejected";
    break;
  case Classification::response_truncated:
    classification = "truncated connect response";
    break;
  case Classification::unexpected_sequenced_packet_pending_m2_3:
    classification = "unexpected sequenced packet pending M2.3";
    break;
  case Classification::wait_timed_out:
    classification = "connect-response wait timed out";
    break;
  case Classification::wait_cancelled:
    classification = "connect-response wait cancelled";
    break;
  case Classification::network_failure:
    classification = "connect-response network failure";
    break;
  case Classification::protocol_failure:
    classification = "connect-response protocol failure";
    break;
  case Classification::wait_started:
  case Classification::receive_would_block:
    return;
  }

  std::string message =
      "[net] RX " + event.endpoint.to_string() + ' ' + classification;
  if (event.datagram_size != 0U) {
    message += ", " + std::to_string(event.datagram_size) + " bytes";
  }
  message += ", elapsed " + std::to_string(event.elapsed.count()) + " ms";
  hlclient::core::log(LogLevel::info, message);
  if (event.classification == Classification::connect_accepted) {
    hlclient::core::log(LogLevel::info,
                        "[session] connection_accepted=true "
                        "authentication_status=pending_or_unknown");
  }
}

void log_netchan_trace(
    const hlclient::goldsrc::NetchanBootstrapTraceEvent &event) {
  using Classification = hlclient::goldsrc::NetchanBootstrapTraceClassification;
  if (event.classification == Classification::bootstrap_started ||
      event.classification == Classification::receive_would_block) {
    return;
  }

  const bool transmitted =
      event.classification == Classification::acknowledgement_sent;
  std::string classification;
  switch (event.classification) {
  case Classification::wrong_endpoint_ignored:
    classification = "wrong endpoint ignored";
    break;
  case Classification::sequenced_packet_received:
    classification = "sequenced";
    break;
  case Classification::fragment_received:
    classification = "fragment received";
    break;
  case Classification::normal_transfer_completed:
    classification = "normal fragment transfer complete";
    break;
  case Classification::duplicate_sequence_ignored:
    classification = "duplicate sequence ignored";
    break;
  case Classification::older_sequence_ignored:
    classification = "older sequence ignored";
    break;
  case Classification::payload_ready:
    classification = "opaque payload ready";
    break;
  case Classification::acknowledgement_sent:
    classification =
        event.fragmented ? "fragment packet" : "sequenced acknowledgement";
    break;
  case Classification::bootstrap_complete:
    classification = "netchan bootstrap complete";
    break;
  case Classification::datagram_truncated:
    classification = "truncated sequenced datagram";
    break;
  case Classification::normal_transfer_timed_out:
    classification = "normal fragment transfer timed out";
    break;
  case Classification::secondary_stream_pending_m3:
    classification = "unconfirmed secondary fragment stream rejected";
    break;
  case Classification::bootstrap_timed_out:
    classification = "netchan bootstrap timed out";
    break;
  case Classification::bootstrap_cancelled:
    classification = "netchan bootstrap cancelled";
    break;
  case Classification::network_failure:
    classification = "netchan network failure";
    break;
  case Classification::protocol_failure:
    classification = "netchan protocol failure";
    break;
  case Classification::bootstrap_started:
  case Classification::receive_would_block:
    return;
  }

  std::string message = std::string{"[net] "} + (transmitted ? "TX " : "RX ") +
                        event.endpoint.to_string() + ' ' + classification;
  if (event.sequence) {
    message += ", sequence=" + std::to_string(*event.sequence);
  }
  if (event.acknowledgement) {
    message += ", ack=" + std::to_string(*event.acknowledgement);
  }
  message +=
      std::string{", reliable="} + (event.reliable ? "yes" : "no") +
      ", fragment=" + (event.fragmented ? "yes" : "no") +
      ", reliable-ack=" + (event.reliable_acknowledgement ? "yes" : "no");
  if (event.datagram_size != 0U) {
    message += ", datagram=" + std::to_string(event.datagram_size) + " bytes";
  }
  if (event.payload_size != 0U) {
    message +=
        ", opaque-payload=" + std::to_string(event.payload_size) + " bytes";
  }
  if (event.fragment_stream) {
    message += std::string{", stream="} +
               (*event.fragment_stream ==
                        hlclient::goldsrc::NetchanFragmentStream::normal
                    ? "normal"
                    : "unconfirmed-slot-1");
  }
  if (event.local_transfer_id) {
    message +=
        ", transfer=<local:" + std::to_string(*event.local_transfer_id) + '>';
  }
  if (event.fragment_length != 0U) {
    message += ", range=" + std::to_string(event.fragment_offset) + '+' +
               std::to_string(event.fragment_length);
  }
  if (event.covered_size != 0U || event.transfer_size != 0U) {
    message += ", coverage=" + std::to_string(event.covered_size) + '/' +
               std::to_string(event.transfer_size);
  }
  if (transmitted) {
    message += ", tx-count=" + std::to_string(event.transmitted_packet_count);
  }
  hlclient::core::log(LogLevel::info, message);
}

void log_initial_signon_trace(
    const hlclient::goldsrc::InitialSignonTraceEvent &event) {
  using Classification = hlclient::goldsrc::InitialSignonTraceClassification;
  std::string classification;
  switch (event.classification) {
  case Classification::stage_started:
    return;
  case Classification::initial_request_queued:
    classification = "initial request queued";
    break;
  case Classification::initial_request_transmitted:
    classification = "initial request transmitted";
    break;
  case Classification::initial_request_acknowledged:
    classification = "initial request acknowledged";
    break;
  case Classification::service_payload_received:
    classification = "service payload received";
    break;
  case Classification::service_message_decoded:
    classification = "service message decoded";
    break;
  case Classification::signon_boundary_reached:
    classification = "sign-on boundary reached";
    break;
  case Classification::stage_cancelled:
    classification = "stage cancelled";
    break;
  case Classification::stage_timed_out:
    classification = "stage timed out";
    break;
  case Classification::secondary_stream_pending_m3:
    classification = "secondary stream pending M3";
    break;
  case Classification::backpressure:
    classification = "event backpressure";
    break;
  case Classification::network_failure:
    classification = "network failure";
    break;
  case Classification::protocol_failure:
    classification = "protocol failure";
    break;
  }

  std::string message = "[signon] " + classification;
  if (event.request_size != 0U) {
    message += ", request=" + std::to_string(event.request_size) + " bytes";
  }
  if (event.payload_size != 0U) {
    message += ", payload=" + std::to_string(event.payload_size) + " bytes";
  }
  if (event.opcode) {
    message +=
        ", opcode=" + std::to_string(static_cast<unsigned int>(*event.opcode)) +
        " (" + std::string{hlclient::goldsrc::to_string(*event.opcode)} + ')';
    message += ", offset=" + std::to_string(event.byte_offset);
  }
  if (event.byte_count != 0U) {
    message += ", bytes=" + std::to_string(event.byte_count);
  }
  if (event.service_payload_count != 0U) {
    message += ", payload-count=" + std::to_string(event.service_payload_count);
  }
  if (event.transmitted_packet_count != 0U) {
    message += ", tx-count=" + std::to_string(event.transmitted_packet_count);
  }
  hlclient::core::log(LogLevel::info, message);
}

void log_pre_resource_signon_trace(
    const hlclient::goldsrc::PreResourceSignonTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::PreResourceSignonTraceClassification;
  std::string classification;
  switch (event.classification) {
  case Classification::stage_started:
    return;
  case Classification::initial_boundary_reached:
    classification = "initial boundary retained";
    break;
  case Classification::server_info_ready:
    classification = "server-info decoded";
    break;
  case Classification::pre_resource_control:
    classification = "pre-resource control decoded";
    break;
  case Classification::pre_resource_boundary_reached:
    classification = "pre-resource boundary reached";
    break;
  case Classification::stage_cancelled:
    classification = "pre-resource stage cancelled";
    break;
  case Classification::stage_timed_out:
    classification = "pre-resource stage timed out";
    break;
  case Classification::secondary_stream_pending_m3:
    classification = "secondary stream pending M3";
    break;
  case Classification::unsupported_message:
    classification = "unsupported pre-resource message";
    break;
  case Classification::backpressure:
    classification = "pre-resource event backpressure";
    break;
  case Classification::network_failure:
    classification = "pre-resource network failure";
    break;
  case Classification::protocol_failure:
    classification = "pre-resource protocol failure";
    break;
  }

  std::string message = "[signon] " + classification;
  if (event.protocol_version) {
    message += ", protocol=" + std::to_string(*event.protocol_version);
  }
  if (event.maximum_clients) {
    message +=
        ", max-clients=" +
        std::to_string(static_cast<unsigned int>(*event.maximum_clients));
  }
  if (event.multi_client_mode) {
    message += std::string{", multi-client="} +
               (*event.multi_client_mode ? "yes" : "no");
  }
  if (event.opcode) {
    message +=
        ", opcode=" + std::to_string(static_cast<unsigned int>(*event.opcode));
    message += ", offset=" + std::to_string(event.byte_offset);
  }
  if (event.byte_count != 0U) {
    message += ", bytes=" + std::to_string(event.byte_count);
  }
  if (event.string_length != 0U) {
    message += ", string-length=" + std::to_string(event.string_length);
  }
  if (event.boundary_direction) {
    message +=
        ", direction=" +
        std::string{hlclient::goldsrc::to_string(*event.boundary_direction)};
  }
  if (event.evidence_status) {
    message +=
        ", evidence=" +
        std::string{hlclient::goldsrc::to_string(*event.evidence_status)};
  }
  if (event.transmitted_packet_count != 0U) {
    message += ", tx-count=" + std::to_string(event.transmitted_packet_count);
  }
  hlclient::core::log(LogLevel::info, message);
  if (event.classification == Classification::server_info_ready) {
    hlclient::core::log(LogLevel::info,
                        "[session-progress] serverinfo=succeeded");
    hlclient::core::log(
        LogLevel::info,
        "[session] serverinfo_received=true authentication_status="
        "pending_or_unknown");
  }
}

void log_delta_description_trace(
    const hlclient::goldsrc::DeltaDescriptionTraceEvent &event) {
  using Classification = hlclient::goldsrc::DeltaDescriptionTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::pre_resource_boundary_reached:
    return;
  case Classification::delta_schema_decoded:
    hlclient::core::log(
        LogLevel::info,
        "[signon] delta schema decoded: " +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                event.schema_name) +
            ", index=" + std::to_string(event.schema_index) +
            ", fields=" + std::to_string(event.field_count) +
            ", bits=" + std::to_string(event.bits_consumed) +
            ", bytes=" + std::to_string(event.bytes_consumed));
    return;
  case Classification::delta_registry_ready:
    hlclient::core::log(LogLevel::info,
                        "[signon] delta registry ready: schemas=" +
                            std::to_string(event.schema_index) +
                            ", fields=" + std::to_string(event.field_count) +
                            ", bits=" + std::to_string(event.bits_consumed) +
                            ", bytes=" + std::to_string(event.bytes_consumed));
    hlclient::core::log(LogLevel::info,
                        "[session-progress] schema_registry=succeeded");
    hlclient::core::log(
        LogLevel::info,
        "[session] schema_registry_received=true authentication_status="
        "pending_or_unknown");
    return;
  case Classification::post_delta_boundary_reached:
    hlclient::core::log(LogLevel::info,
                        "[signon] next boundary opcode=" +
                            std::to_string(static_cast<unsigned int>(
                                event.boundary_opcode.value_or(0U))) +
                            " offset=" + std::to_string(event.byte_offset));
    return;
  case Classification::stage_cancelled:
    hlclient::core::log(LogLevel::error, "Delta-description stage cancelled");
    return;
  case Classification::stage_timed_out:
    hlclient::core::log(LogLevel::error, "Delta-description stage timed out");
    return;
  case Classification::unsupported_message:
    hlclient::core::log(LogLevel::error,
                        "Unsupported delta-description message");
    return;
  case Classification::backpressure:
    hlclient::core::log(LogLevel::error,
                        "Delta-description event backpressure");
    return;
  case Classification::secondary_stream_pending_m3:
    hlclient::core::log(LogLevel::error, "Secondary stream remains pending M3");
    return;
  case Classification::network_failure:
    hlclient::core::log(LogLevel::error, "Delta-description network failure");
    return;
  case Classification::protocol_failure:
    hlclient::core::log(LogLevel::error, "Delta-description protocol failure");
    return;
  }
}

void log_movement_environment_trace(
    const hlclient::goldsrc::MovementEnvironmentTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::MovementEnvironmentTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::delta_boundary_reached:
    return;
  case Classification::movement_environment_ready: {
    std::string message = "[signon] movement/environment state decoded";
    if (event.gravity) {
      message += ", gravity=" + std::to_string(*event.gravity);
    }
    if (event.maximum_speed) {
      message += ", max-speed=" + std::to_string(*event.maximum_speed);
    }
    if (event.footsteps) {
      message +=
          std::string{", footsteps="} + (*event.footsteps ? "yes" : "no");
    }
    message += ", sky-name=" +
               hlclient::goldsrc::sanitize_service_text_for_presentation(
                   event.sky_name);
    message += ", bytes=" + std::to_string(event.byte_count);
    message += ", controls=" + std::to_string(event.control_count);
    hlclient::core::log(LogLevel::info, message);
    hlclient::core::log(LogLevel::info,
                        "[session-progress] movevars=succeeded");
    return;
  }
  case Classification::post_environment_control:
    hlclient::core::log(
        LogLevel::info,
        "[signon] post-movevars control opcode=" +
            std::to_string(
                static_cast<unsigned int>(event.opcode.value_or(0U))) +
            " index=" + std::to_string(event.control_index) +
            " offset=" + std::to_string(event.byte_offset) +
            " bytes=" + std::to_string(event.byte_count) +
            " string-length=" + std::to_string(event.string_length));
    return;
  case Classification::post_environment_boundary_reached:
    hlclient::core::log(
        LogLevel::info,
        "[signon] post-movevars boundary opcode=" +
            std::to_string(
                static_cast<unsigned int>(event.opcode.value_or(0U))) +
            " offset=" + std::to_string(event.byte_offset) +
            " unconsumed-body=" + std::to_string(event.byte_count) + " bytes");
    return;
  case Classification::stage_cancelled:
    hlclient::core::log(LogLevel::error,
                        "Movement/environment stage cancelled");
    return;
  case Classification::stage_timed_out:
    hlclient::core::log(LogLevel::error,
                        "Movement/environment stage timed out");
    return;
  case Classification::unsupported_message:
    hlclient::core::log(LogLevel::error,
                        "Unsupported post-movevars service message");
    return;
  case Classification::backpressure:
    hlclient::core::log(LogLevel::error,
                        "Movement/environment event backpressure");
    return;
  case Classification::secondary_stream_pending_m3:
    hlclient::core::log(LogLevel::error, "Secondary stream remains pending M3");
    return;
  case Classification::network_failure:
    hlclient::core::log(LogLevel::error,
                        "Movement/environment network failure");
    return;
  case Classification::protocol_failure:
    hlclient::core::log(LogLevel::error,
                        "Movement/environment protocol failure");
    return;
  }
}

void log_user_info_trace(
    const hlclient::goldsrc::UserInfoSignonTraceEvent &event) {
  using Classification = hlclient::goldsrc::UserInfoSignonTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::movevars_boundary_reached:
    return;
  case Classification::user_info_message_decoded:
    hlclient::core::log(
        LogLevel::info,
        "[signon] user-info message decoded: message-index=" +
            std::to_string(event.message_index) +
            " info-bytes=" + std::to_string(event.info_string_length) +
            " entries=" + std::to_string(event.info_entry_count) +
            " player-name-length=" +
            (event.player_name_length
                 ? std::to_string(*event.player_name_length)
                 : std::string{"unavailable"}));
    return;
  case Classification::first_batch_complete:
    hlclient::core::log(LogLevel::info,
                        "[signon] user-info messages decoded: count=" +
                            std::to_string(event.message_count));
    hlclient::core::log(LogLevel::info,
                        "[session-progress] user_info=succeeded");
    return;
  case Classification::stage_cancelled:
    hlclient::core::log(LogLevel::error, "User-info stage cancelled");
    return;
  case Classification::stage_timed_out:
    hlclient::core::log(LogLevel::error, "User-info stage timed out");
    return;
  case Classification::unsupported_message:
    hlclient::core::log(LogLevel::error, "Unsupported user-info continuation");
    return;
  case Classification::backpressure:
    hlclient::core::log(LogLevel::error, "User-info event backpressure");
    return;
  case Classification::secondary_stream_pending:
    hlclient::core::log(LogLevel::error, "Secondary stream remains pending");
    return;
  case Classification::network_failure:
    hlclient::core::log(LogLevel::error, "User-info network failure");
    return;
  case Classification::protocol_failure:
    hlclient::core::log(LogLevel::error, "User-info protocol failure");
    return;
  }
}

void log_resource_transition_trace(
    const hlclient::goldsrc::ResourceTransitionTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::ResourceTransitionTraceClassification;
  const auto log_failure_metadata = [&event]() {
    if (!event.failure_metadata) {
      return;
    }
    const auto &metadata = *event.failure_metadata;
    const auto number_or_unavailable =
        []<typename T>(const std::optional<T> &value) {
          return value ? std::to_string(*value) : std::string{"unavailable"};
        };
    const auto bool_or_unavailable = [](const std::optional<bool> value) {
      return value ? std::string{*value ? "true" : "false"}
                   : std::string{"unavailable"};
    };
    const auto direction =
        metadata.direction
            ? *metadata.direction ==
                      hlclient::goldsrc::NetchanDirection::server_to_client
                  ? "server_to_client"
                  : "client_to_server"
            : "unavailable";
    hlclient::core::log(
        LogLevel::error,
        "[transition-diagnostic] stage=resource_transition "
        "profile=stock_protocol_48_build_10210 expected_opcode=" +
            std::to_string(metadata.expected_opcode) +
            " actual_opcode=" + number_or_unavailable(metadata.actual_opcode) +
            " cursor_byte_value=" +
            number_or_unavailable(metadata.cursor_byte_value) +
            " cursor=" + std::to_string(metadata.cursor_byte_offset) + ":" +
            std::to_string(metadata.cursor_bit_offset) + " boundary=" +
            std::string{
                hlclient::goldsrc::to_string(metadata.cursor_boundary_kind)} +
            " payload_ordinal=" +
            number_or_unavailable(metadata.payload_ordinal) +
            " payload_ordinal_scope=" +
            std::string{
                hlclient::goldsrc::to_string(metadata.payload_ordinal_scope)});
    hlclient::core::log(
        LogLevel::error,
        "[transition-diagnostic] direction=" + std::string{direction} +
            " source_sequence=" +
            number_or_unavailable(metadata.source_sequence) + " source_ack=" +
            number_or_unavailable(metadata.source_acknowledgement) +
            " source_reliable=" +
            bool_or_unavailable(metadata.source_reliable) + " reassembled=" +
            bool_or_unavailable(metadata.reassembled) + " encoding=" +
            std::string{hlclient::goldsrc::to_string(metadata.wire_encoding)} +
            " wire_size=" + number_or_unavailable(metadata.wire_byte_count) +
            " decoded_size=" +
            number_or_unavailable(metadata.decoded_byte_count) +
            " pending_suffix_start=" +
            (metadata.pending_suffix_byte_offset
                 ? std::to_string(*metadata.pending_suffix_byte_offset) + ":" +
                       number_or_unavailable(metadata.pending_suffix_bit_offset)
                 : std::string{"unavailable"}));
    hlclient::core::log(
        LogLevel::error,
        "[transition-diagnostic] sendres_queued=" +
            std::string{metadata.request_queued ? "true" : "false"} +
            " sendres_transmitted=" +
            std::string{metadata.request_transmitted ? "true" : "false"} +
            " sendres_acknowledged=" +
            std::string{metadata.request_acknowledged ? "true" : "false"} +
            " request_reliable_generation=" +
            number_or_unavailable(metadata.request_reliable_generation) +
            " request_tx_sequence=" +
            number_or_unavailable(metadata.request_transmit_sequence) +
            " request_ack_sequence=" +
            number_or_unavailable(metadata.request_acknowledgement_sequence) +
            " last_category=" +
            (metadata.last_successful_message_category
                 ? std::string{hlclient::goldsrc::to_string(
                       *metadata.last_successful_message_category)}
                 : std::string{"unavailable"}) +
            " last_scope=" +
            std::string{metadata.last_successful_message_category &&
                                *metadata.last_successful_message_category ==
                                    hlclient::goldsrc::
                                        ResourceTransitionLastMessageCategory::
                                            runtime_control_nop
                            ? "resource_transition_payload"
                            : "initial_service_payload"} +
            " last_payload_ordinal=" +
            number_or_unavailable(
                metadata.last_successful_message_payload_ordinal) +
            " last_source_sequence=" +
            number_or_unavailable(
                metadata.last_successful_message_source_sequence) +
            " last_cursor=" +
            number_or_unavailable(
                metadata.last_successful_message_byte_offset) +
            ":" +
            number_or_unavailable(
                metadata.last_successful_message_end_byte_offset) +
            " intermediate_message_count=" +
            std::to_string(metadata.intermediate_message_count) +
            " intermediate_parser_error=" +
            (metadata.intermediate_parser_error
                 ? std::string{hlclient::goldsrc::to_string(
                       *metadata.intermediate_parser_error)}
                 : std::string{"unavailable"}) +
            " parser_error=" +
            (metadata.parser_error ? std::string{hlclient::goldsrc::to_string(
                                         *metadata.parser_error)}
             : metadata.intermediate_parser_error
                 ? std::string{hlclient::goldsrc::to_string(
                       *metadata.intermediate_parser_error)}
                 : std::string{"unavailable"}) +
            " primary_error=" +
            std::string{metadata.intermediate_parser_error
                            ? "intermediate_message_decode_failed"
                            : "transition_control_decode_failed"});
  };
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::user_info_ready:
    return;
  case Classification::transition_request_queued:
    hlclient::core::log(LogLevel::info, "[resource] transition request queued");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] sendres_queued=succeeded");
    return;
  case Classification::transition_request_transmitted:
    hlclient::core::log(LogLevel::info,
                        "[live-runtime] resource_continuation_sent=true");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] sendres_transmitted=succeeded");
    return;
  case Classification::transition_request_acknowledged:
    hlclient::core::log(LogLevel::info,
                        "[resource] transition request acknowledged");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] sendres_acknowledged=succeeded");
    return;
  case Classification::intermediate_message_decoded:
    hlclient::core::log(
        LogLevel::info,
        "[transition-dispatch] category=svc_nop opcode=" +
            std::to_string(event.opcode.value_or(0U)) +
            " cursor=" + std::to_string(event.byte_offset) + ":" +
            std::to_string(event.byte_offset + event.byte_count));
    return;
  case Classification::intermediate_payload_consumed:
    hlclient::core::log(
        LogLevel::info,
        "[transition-dispatch] intermediate_payload_consumed=true bytes=" +
            std::to_string(event.byte_count));
    return;
  case Classification::second_service_transfer_received:
    return;
  case Classification::transition_control_decoded:
    hlclient::core::log(LogLevel::info,
                        "[resource] transition control decoded, bytes=" +
                            std::to_string(event.byte_count));
    hlclient::core::log(LogLevel::info,
                        "[session-progress] resource_transition=succeeded");
    return;
  case Classification::neutral_opcode43_boundary_reached:
    hlclient::core::log(LogLevel::info,
                        "[resource] neutral opcode-43 boundary opcode=" +
                            std::to_string(static_cast<unsigned int>(
                                event.opcode.value_or(0U))) +
                            " offset=" + std::to_string(event.byte_offset));
    return;
  case Classification::stage_cancelled:
    hlclient::core::log(LogLevel::error, "Resource-transition stage cancelled");
    return;
  case Classification::stage_timed_out:
    hlclient::core::log(LogLevel::error, "Resource-transition stage timed out");
    return;
  case Classification::unsupported_message:
    log_failure_metadata();
    hlclient::core::log(LogLevel::error,
                        "[session-progress] resource_transition=failed");
    hlclient::core::log(LogLevel::error,
                        "Unsupported resource-transition message");
    return;
  case Classification::backpressure:
    hlclient::core::log(LogLevel::error,
                        "Resource-transition event backpressure");
    return;
  case Classification::secondary_stream_pending:
    hlclient::core::log(LogLevel::error,
                        "Secondary resource stream remains pending");
    return;
  case Classification::network_failure:
    hlclient::core::log(LogLevel::error, "Resource-transition network failure");
    return;
  case Classification::protocol_failure:
    log_failure_metadata();
    hlclient::core::log(LogLevel::error,
                        "[session-progress] resource_transition=failed");
    hlclient::core::log(LogLevel::error,
                        "Resource-transition protocol failure");
    return;
  }
}

void log_resource_list_trace(
    const hlclient::goldsrc::ResourceListTraceEvent &event) {
  using Classification = hlclient::goldsrc::ResourceListTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::transition_boundary_reached:
  case Classification::post_resource_control:
    return;
  case Classification::resource_list_decoded:
    hlclient::core::log(LogLevel::info,
                        "[resource] list decoded: entries=" +
                            std::to_string(event.resource_count));
    hlclient::core::log(LogLevel::info,
                        "[session-progress] resource_list=succeeded");
    return;
  case Classification::resource_entry_metadata:
    hlclient::core::log(
        LogLevel::info,
        "[resource] entry=" + std::to_string(event.entry_ordinal) + " type=" +
            std::string{event.resource_type
                            ? hlclient::goldsrc::to_string(*event.resource_type)
                            : std::string_view{"unknown"}} +
            " index=" +
            (event.resource_index ? std::to_string(*event.resource_index)
                                  : std::string{"unavailable"}) +
            " name-bytes=" + std::to_string(event.resource_name_byte_count) +
            " size-code=" +
            (event.resource_size_code
                 ? std::to_string(*event.resource_size_code)
                 : std::string{"unavailable"}) +
            " flags=" +
            (event.resource_flags ? std::to_string(static_cast<unsigned int>(
                                        *event.resource_flags))
                                  : std::string{"unavailable"}) +
            " offset=" + std::to_string(event.byte_offset) + ":" +
            std::to_string(event.bit_offset));
    return;
  case Classification::post_resource_boundary_reached:
    hlclient::core::log(LogLevel::info,
                        "[resource] exact post-list boundary offset=" +
                            std::to_string(event.byte_offset) + ":" +
                            std::to_string(event.bit_offset));
    return;
  case Classification::client_response_required:
    hlclient::core::log(LogLevel::info,
                        "[resource] stock client response required; metadata "
                        "only, no response queued");
    return;
  case Classification::unsupported_resource_profile:
    hlclient::core::log(
        LogLevel::error,
        "Unobserved resource-list flags/profile slot is unsupported");
    return;
  case Classification::stage_timed_out:
    hlclient::core::log(LogLevel::error, "Resource-list stage timed out");
    return;
  case Classification::stage_cancelled:
    hlclient::core::log(LogLevel::error, "Resource-list stage cancelled");
    return;
  case Classification::backpressure:
    hlclient::core::log(LogLevel::error, "Resource-list event backpressure");
    return;
  case Classification::secondary_stream_pending:
    hlclient::core::log(LogLevel::error,
                        "Secondary resource stream remains pending");
    return;
  case Classification::network_failure:
    hlclient::core::log(LogLevel::error, "Resource-list network failure");
    return;
  case Classification::protocol_failure:
    hlclient::core::log(LogLevel::error, "Resource-list protocol failure");
    return;
  }
}

void log_resource_client_response_trace(
    const hlclient::goldsrc::ResourceClientResponseTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::ResourceClientResponseTraceClassification;
  const auto log_payload_diagnostic = [&event]() {
    if (!event.payload_diagnostic) {
      return;
    }
    const auto &value = *event.payload_diagnostic;
    const auto optional_number = [](const auto &item) {
      return item ? std::to_string(*item) : std::string{"unavailable"};
    };
    hlclient::core::log(
        LogLevel::info,
        "[resource-response-diagnostic] classification=" +
            std::string{hlclient::goldsrc::to_string(value.classification)} +
            " rx_position=" +
            std::string{hlclient::goldsrc::to_string(value.receive_position)} +
            " payload_ordinal=" + std::to_string(value.payload_ordinal) +
            " source_sequence=" + optional_number(value.source_sequence) +
            " source_ack=" + optional_number(value.source_acknowledgement) +
            " encoding=" +
            std::string{hlclient::goldsrc::to_string(value.wire_encoding)} +
            " wire_size=" + std::to_string(value.wire_byte_count) +
            " decoded_size=" + std::to_string(value.decoded_byte_count) +
            " cursor=" + std::to_string(value.cursor_byte_offset) + ":" +
            std::to_string(value.cursor_bit_offset) + " boundary=" +
            (value.validated_message_boundary
                 ? std::string{"validated_message_boundary"}
                 : std::string{"buffer_start_only"}) +
            " actual_opcode=" + optional_number(value.actual_opcode) +
            " response_queued=" + std::to_string(value.response_queued) +
            " response_transmitted=" +
            std::to_string(value.response_transmitted) +
            " response_acknowledged=" +
            std::to_string(value.response_acknowledged) + " generation=" +
            optional_number(value.reliable_generation) + " first_tx_sequence=" +
            optional_number(value.first_transmit_sequence) +
            " controls_consumed=" +
            std::to_string(value.consumed_control_message_count) +
            " pending_count=" + std::to_string(value.pending_payload_count) +
            " pending_bytes=" + std::to_string(value.pending_byte_count) +
            " last_handoff_cursor=" +
            optional_number(value.last_successful_handoff_cursor));
  };
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::resource_list_ready:
    return;
  case Classification::resource_response_requirements_ready:
    hlclient::core::log(LogLevel::info,
                        "[resource] client response requirements determined");
    return;
  case Classification::consistency_provider_required:
    hlclient::core::log(LogLevel::error,
                        "[resource] a resource-consistency provider is "
                        "required; response not sent");
    return;
  case Classification::resource_response_ready:
    hlclient::core::log(LogLevel::info,
                        "[resource] client response ready, bytes=" +
                            std::to_string(event.semantic_byte_count));
    return;
  case Classification::resource_response_queued:
    hlclient::core::log(LogLevel::info, "[resource] client response queued");
    hlclient::core::log(
        LogLevel::info,
        "[session-progress] resource_response_queued=succeeded");
    return;
  case Classification::resource_response_transmitted:
    hlclient::core::log(LogLevel::debug,
                        "[resource] client response transmitted, generation=" +
                            (event.reliable_generation
                                 ? std::to_string(*event.reliable_generation)
                                 : std::string{"unavailable"}) +
                            " sequence=" +
                            (event.transmit_sequence
                                 ? std::to_string(*event.transmit_sequence)
                                 : std::string{"unavailable"}));
    hlclient::core::log(
        LogLevel::info,
        "[session-progress] resource_response_transmitted=succeeded");
    return;
  case Classification::resource_response_acknowledged:
    hlclient::core::log(LogLevel::info,
                        "[resource] client response acknowledged");
    hlclient::core::log(
        LogLevel::info,
        "[session-progress] resource_response_acknowledged=succeeded");
    return;
  case Classification::pre_transmit_control_consumed:
    log_payload_diagnostic();
    return;
  case Classification::response_completion_ready:
    hlclient::core::log(
        LogLevel::info,
        "[resource] client response completed by exact covering ACK");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] resource_response=succeeded");
    return;
  case Classification::concurrent_tail_observed:
    hlclient::core::log(LogLevel::debug,
                        "[resource] concurrent tail metadata observed, bytes=" +
                            std::to_string(event.payload_byte_count));
    return;
  case Classification::server_continuation_received:
    hlclient::core::log(LogLevel::debug,
                        "[resource] following server payload received, bytes=" +
                            std::to_string(event.payload_byte_count));
    return;
  case Classification::next_server_boundary_reached:
    hlclient::core::log(
        LogLevel::info,
        "[resource] next server boundary opcode=" +
            (event.opcode
                 ? std::to_string(static_cast<unsigned int>(*event.opcode))
                 : std::string{"end-of-payload"}) +
            " offset=0");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] resource_response=succeeded");
    return;
  case Classification::unsupported_response_profile:
    hlclient::core::log(LogLevel::error,
                        "Unsupported post-resource response profile");
    return;
  case Classification::stage_timed_out:
    hlclient::core::log(LogLevel::error,
                        "Post-resource response stage timed out");
    return;
  case Classification::stage_cancelled:
    hlclient::core::log(LogLevel::error,
                        "Post-resource response stage cancelled");
    return;
  case Classification::backpressure:
    hlclient::core::log(LogLevel::error,
                        "Post-resource response event backpressure");
    return;
  case Classification::secondary_stream_pending:
    hlclient::core::log(LogLevel::error,
                        "Secondary post-resource stream remains pending");
    return;
  case Classification::network_failure:
    hlclient::core::log(LogLevel::error,
                        "Post-resource response network failure");
    return;
  case Classification::protocol_failure:
    log_payload_diagnostic();
    hlclient::core::log(LogLevel::error,
                        "Post-resource response protocol failure");
    return;
  }
}

void log_live_runtime_trace(
    const hlclient::goldsrc::LiveRuntimeStageTraceEvent &event) {
  using Type = hlclient::goldsrc::LiveRuntimeStageEventType;
  const auto &metadata = event.metadata;
  switch (metadata.type) {
  case Type::resource_response_ready:
    return;
  case Type::spawn_request_queued:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] spawn_queued=succeeded");
    return;
  case Type::spawn_request_transmitted:
    hlclient::core::log(LogLevel::info,
                        "[live-runtime] spawn_request_transmitted=true");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] spawn_transmitted=succeeded");
    return;
  case Type::spawn_request_acknowledged:
    hlclient::core::log(LogLevel::info,
                        "[live-runtime] spawn_request_acknowledged=true");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] spawn_acknowledged=succeeded");
    return;
  case Type::signon_reply_queued:
    hlclient::core::log(LogLevel::info,
                        "[live-runtime] signon_reply=sendents queued=true");
    return;
  case Type::signon_reply_transmitted:
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] signon_reply=sendents transmitted=true");
    return;
  case Type::signon_reply_acknowledged:
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] signon_reply=sendents acknowledged=true");
    return;
  case Type::service_payload_received:
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] live_service_payloads_received=true "
        "payload_ordinal=" +
            std::to_string(metadata.payload_ordinal) + " source_sequence=" +
            (metadata.source_sequence
                 ? std::to_string(*metadata.source_sequence)
                 : std::string{"unavailable"}) +
            " decoded_size=" + std::to_string(metadata.payload_byte_count) +
            " encoding=" +
            std::string{metadata.decompressed        ? "bzip2"
                        : metadata.wire_uncompressed ? "wire_uncompressed"
                                                     : "unavailable"});
    return;
  case Type::baseline_registry_ready:
    hlclient::core::log(LogLevel::info,
                        "[live-runtime] baseline_entities=" +
                            std::to_string(metadata.baseline_count));
    hlclient::core::log(LogLevel::info,
                        "[session-progress] baselines=succeeded");
    return;
  case Type::runtime_record_applied:
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] runtime_record_applied=true payload_ordinal=" +
            std::to_string(metadata.payload_ordinal) +
            " publication_revision=" +
            std::to_string(metadata.publication_revision) +
            " entities=" + std::to_string(metadata.entity_count));
    hlclient::core::log(LogLevel::info,
                        "[session-progress] runtime_publication=succeeded");
    return;
  case Type::stability_interval_started:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] operational_interval=pending");
    return;
  case Type::stable_runtime_state_ready:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] operational_interval=succeeded");
    return;
  case Type::live_visual_input_ready:
    hlclient::core::log(
        LogLevel::info,
        "[session-progress] live_visual_production_handoff=succeeded");
    return;
  case Type::usercmd_scenario_activated:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] live_usercmd_handoff=succeeded");
    return;
  case Type::usercmd_packet_submitted:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] live_usercmd_transmission=active");
    return;
  case Type::usercmd_server_sample:
    return;
  case Type::live_usercmd_check_ready:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] live_usercmd_check=succeeded");
    return;
  case Type::live_visual_control_ready:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] live_visual_control=succeeded");
    return;
  case Type::timeout:
  case Type::cancelled:
  case Type::backpressure:
  case Type::secondary_stream_pending:
  case Type::network_error:
  case Type::protocol_error:
    return;
  }
}

void log_precache_manifest_trace(
    const hlclient::goldsrc::PrecacheManifestTraceEvent &event) {
  using Classification = hlclient::goldsrc::PrecacheManifestTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::resource_response_boundary_reached:
    return;
  case Classification::local_inventory_ready:
    hlclient::core::log(
        LogLevel::debug,
        "[local-resource] inventory retained for manifest: entries=" +
            std::to_string(event.entry_count));
    return;
  case Classification::precache_manifest_ready:
  case Classification::local_resources_incomplete:
  case Classification::unsafe_local_resources:
  case Classification::unsupported_local_profile:
  case Classification::local_resource_io_error:
    hlclient::core::log(
        LogLevel::debug,
        "[precache] metadata snapshot: entries=" +
            std::to_string(event.entry_count) +
            ", ready=" + std::to_string(event.ready_count) +
            ", metadata-only=" + std::to_string(event.metadata_only_count) +
            ", missing=" + std::to_string(event.missing_count) +
            ", unsafe=" + std::to_string(event.unsafe_count) +
            ", unsupported=" + std::to_string(event.unsupported_count) +
            ", ambiguous=" + std::to_string(event.ambiguous_count) +
            ", io-error=" + std::to_string(event.io_error_count));
    return;
  case Classification::stage_timed_out:
  case Classification::stage_cancelled:
  case Classification::backpressure:
  case Classification::secondary_stream_pending:
  case Classification::network_failure:
  case Classification::protocol_failure:
    hlclient::core::log(LogLevel::error,
                        "Precache-manifest stage ended before publication");
    return;
  }
}

void log_post_resource_entity_snapshot_trace(
    const hlclient::goldsrc::PostResourceEntitySnapshotTraceEvent &event) {
  using Type = hlclient::goldsrc::PostResourceEntitySnapshotStageEventType;
  const auto &metadata = event.metadata;
  switch (metadata.type) {
  case Type::post_resource_message_received:
    hlclient::core::log(
        LogLevel::debug,
        "[signon] post-resource message opcode=" +
            (metadata.opcode
                 ? std::to_string(static_cast<unsigned int>(*metadata.opcode))
                 : std::string{"unavailable"}) +
            " offset=" + std::to_string(metadata.byte_offset) + ":" +
            std::to_string(metadata.bit_offset));
    return;
  case Type::client_signon_request_ready:
  case Type::client_signon_request_queued:
  case Type::client_signon_request_acknowledged:
    hlclient::core::log(LogLevel::debug,
                        "[signon] typed request metadata bytes=" +
                            std::to_string(metadata.semantic_byte_count));
    return;
  case Type::server_signon_progress:
    hlclient::core::log(LogLevel::debug,
                        "[signon] progression metadata received");
    return;
  case Type::baseline_decoded:
  case Type::baseline_registry_ready:
    hlclient::core::log(LogLevel::debug,
                        "[entity] baselines=" +
                            std::to_string(metadata.baseline_count));
    return;
  case Type::full_entity_snapshot_ready:
  case Type::delta_entity_snapshot_ready:
    hlclient::core::log(
        LogLevel::debug,
        "[entity] snapshot entities=" + std::to_string(metadata.entity_count) +
            " changed=" + std::to_string(metadata.changed_count) +
            " added=" + std::to_string(metadata.added_count) +
            " removed=" + std::to_string(metadata.removed_count) +
            " history=" + std::to_string(metadata.history_count));
    return;
  case Type::entity_removed:
    hlclient::core::log(LogLevel::debug, "[entity] removal metadata received");
    return;
  case Type::snapshot_history_updated:
    hlclient::core::log(LogLevel::debug,
                        "[entity] history=" +
                            std::to_string(metadata.history_count));
    return;
  case Type::unsupported_message:
    hlclient::core::log(
        LogLevel::error,
        "[signon] stock post-resource body remains evidence-pending");
    return;
  case Type::missing_delta_base:
    hlclient::core::log(LogLevel::error,
                        "[entity] exact delta base is unavailable");
    return;
  case Type::timeout:
  case Type::cancelled:
  case Type::backpressure:
  case Type::secondary_stream_pending:
  case Type::network_error:
  case Type::protocol_error:
    hlclient::core::log(
        LogLevel::error,
        "[signon] post-resource entity stage ended without publication");
    return;
  }
}

void log_precache_asset_dispatch_trace(
    const hlclient::goldsrc::PrecacheAssetDispatchTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::PrecacheAssetDispatchTraceClassification;
  const auto resource_metadata =
      " type=" +
      std::string{hlclient::goldsrc::to_string(event.resource_type)} +
      " index=" + std::to_string(event.resource_index) +
      " ordinal=" + std::to_string(event.wire_ordinal);
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::precache_manifest_ready:
    return;
  case Classification::world_entry_selected:
    hlclient::core::log(LogLevel::debug,
                        "[asset] world source selected:" + resource_metadata);
    return;
  case Classification::asset_source_open_started:
  case Classification::asset_source_progress:
    hlclient::core::log(LogLevel::debug,
                        "[asset] source progress: bytes=" +
                            std::to_string(event.progress_bytes) + "/" +
                            std::to_string(event.byte_count));
    return;
  case Classification::asset_source_ready:
    hlclient::core::log(LogLevel::debug, "[asset] source opened: bytes=" +
                                             std::to_string(event.byte_count));
    return;
  case Classification::importer_probe_completed:
  case Classification::importer_selected:
    hlclient::core::log(
        LogLevel::debug,
        "[asset] importer probe: category=" +
            std::string{hlclient::assets::to_string(event.importer_category)} +
            (event.importer_id.empty() ? std::string{}
                                       : ", importer=" + event.importer_id));
    return;
  case Classification::asset_imported:
  case Classification::importer_boundary_reached:
    hlclient::core::log(
        LogLevel::debug,
        "[asset] importer dispatch: category=" +
            std::string{hlclient::assets::to_string(event.importer_category)} +
            ", result=" +
            (event.dispatch_state ? std::string{hlclient::assets::to_string(
                                        *event.dispatch_state)}
                                  : std::string{"unknown"}));
    return;
  case Classification::world_source_unavailable:
  case Classification::source_open_failed:
  case Classification::ambiguous_importer:
  case Classification::import_failed:
  case Classification::stage_timed_out:
  case Classification::stage_cancelled:
  case Classification::backpressure:
  case Classification::network_failure:
  case Classification::protocol_failure:
    hlclient::core::log(
        LogLevel::error,
        "Asset-dispatch stage ended before its successful boundary");
    return;
  }
}

void log_world_texture_import_trace(
    const hlclient::goldsrc::WorldTextureImportTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::WorldTextureImportTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::world_geometry_ready:
  case Classification::texture_import_started:
  case Classification::texture_import_progress:
  case Classification::wad_source_open_started:
  case Classification::wad_source_ready:
    hlclient::core::log(
        LogLevel::debug,
        "[texture] progress: materials=" +
            std::to_string(event.material_count) +
            ", decoded=" + std::to_string(event.texture_count) +
            ", rgba-bytes=" + std::to_string(event.pixel_conversion_bytes));
    return;
  case Classification::world_textures_ready:
  case Classification::world_textures_incomplete:
    hlclient::core::log(
        LogLevel::debug,
        "[texture] publication: textures=" +
            std::to_string(event.texture_count) +
            ", bindings=" + std::to_string(event.binding_count) +
            ", unresolved=" + std::to_string(event.unresolved_binding_count));
    return;
  case Classification::world_geometry_unavailable:
  case Classification::worldspawn_parse_failed:
  case Classification::wad_reference_invalid:
  case Classification::wad_source_unavailable:
  case Classification::wad_source_open_failed:
  case Classification::wad_catalog_failed:
  case Classification::texture_decode_failed:
  case Classification::stage_timed_out:
  case Classification::stage_cancelled:
  case Classification::backpressure:
  case Classification::network_failure:
  case Classification::protocol_failure:
    hlclient::core::log(
        LogLevel::error,
        "World-texture stage ended before a complete publication");
    return;
  }
}

void log_world_render_package_trace(
    const hlclient::goldsrc::WorldRenderPackageTraceEvent &event) {
  using Classification =
      hlclient::goldsrc::WorldRenderPackageTraceClassification;
  switch (event.classification) {
  case Classification::stage_started:
  case Classification::world_textures_ready:
  case Classification::lightmap_import_started:
  case Classification::lightmap_atlases_ready:
  case Classification::render_package_build_started:
    hlclient::core::log(
        LogLevel::debug,
        "[render-package] progress: surfaces=" +
            std::to_string(event.surface_count) +
            ", atlas-pages=" + std::to_string(event.atlas_page_count));
    return;
  case Classification::world_render_package_ready:
    hlclient::core::log(LogLevel::debug,
                        "[render-package] publication: vertices=" +
                            std::to_string(event.vertex_count) +
                            ", indices=" + std::to_string(event.index_count) +
                            ", batches=" + std::to_string(event.batch_count));
    return;
  case Classification::world_textures_incomplete:
  case Classification::lightmap_import_failed:
  case Classification::render_package_failed:
  case Classification::stage_timed_out:
  case Classification::stage_cancelled:
  case Classification::backpressure:
  case Classification::network_failure:
  case Classification::protocol_failure:
    hlclient::core::log(LogLevel::error,
                        "World-render-package stage ended before publication");
    return;
  }
}

[[nodiscard]] int report_handshake_result(
    const hlclient::goldsrc::GoldSrcHandshakeCoordinator &handshake) {
  using State = hlclient::goldsrc::GoldSrcHandshakeState;
  switch (handshake.state()) {
  case State::challenge_received:
    if (!handshake.challenge()) {
      hlclient::core::log(
          LogLevel::error,
          "Challenge exchange completed without an owned challenge result");
      return 1;
    }
    hlclient::core::log(LogLevel::info,
                        "Challenge received: " +
                            std::to_string(handshake.challenge()->challenge));
    hlclient::core::log(LogLevel::info, "M1 challenge exchange completed");
    hlclient::core::log(
        LogLevel::info,
        "Connect and sign-on are not implemented yet in challenge-only mode");
    return 0;
  case State::request_sent:
    hlclient::core::log(LogLevel::info,
                        "M2.1 connect request sent exactly once");
    hlclient::core::log(LogLevel::info, "Server acceptance was not determined; "
                                        "netchan and sign-on were not started");
    return 0;
  case State::accepted: {
    if (!handshake.connect_response() ||
        !std::holds_alternative<hlclient::goldsrc::ConnectAccepted>(
            *handshake.connect_response())) {
      hlclient::core::log(
          LogLevel::error,
          "Connect response completed without an accepted result");
      return 1;
    }
    const auto &accepted = std::get<hlclient::goldsrc::ConnectAccepted>(
        *handshake.connect_response());
    hlclient::core::log(LogLevel::info, "GoldSrc connect response accepted");
    hlclient::core::log(LogLevel::info,
                        "User ID: " + std::to_string(accepted.user_id));
    hlclient::core::log(LogLevel::info,
                        "Server build: " +
                            std::to_string(accepted.server_build));
    hlclient::core::log(LogLevel::info, std::string{"Secure: "} +
                                            (accepted.secure ? "yes" : "no"));
    hlclient::core::log(
        LogLevel::info,
        "Immediate acceptance parsed; netchan and sign-on were not started");
    return 0;
  }
  case State::rejected: {
    if (!handshake.connect_response() ||
        !std::holds_alternative<hlclient::goldsrc::ConnectRejected>(
            *handshake.connect_response())) {
      hlclient::core::log(
          LogLevel::error,
          "Connect response completed without a rejection result");
      return 1;
    }
    const auto &rejected = std::get<hlclient::goldsrc::ConnectRejected>(
        *handshake.connect_response());
    hlclient::core::log(LogLevel::error, "GoldSrc connection rejected");
    hlclient::core::log(
        LogLevel::error,
        "Reason: " +
            hlclient::goldsrc::sanitize_connect_rejection_for_presentation(
                rejected.message));
    return 1;
  }
  case State::authentication_failed:
  case State::authentication_timed_out:
    hlclient::core::log(
        LogLevel::error,
        handshake.authentication_error()
            ? "Steam authentication failed: " +
                  std::string{hlclient::auth::to_string(
                      handshake.authentication_error()->code)}
            : "Steam authentication failed without a typed result");
    return 1;
  case State::connect_response_timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc connect-response wait timed out");
    return 1;
  case State::netchan_bootstrap_complete: {
    if (!handshake.netchan_bootstrap_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Netchan bootstrap completed without an owned opaque payload");
      return 1;
    }
    const auto &payload = handshake.netchan_bootstrap_result()->payload;
    hlclient::core::log(LogLevel::info, "GoldSrc netchan bootstrap complete");
    hlclient::core::log(
        LogLevel::info,
        "Opaque transport payload: " + std::to_string(payload.bytes.size()) +
            " bytes from sequence " +
            std::to_string(payload.source_sequence.value()));
    hlclient::core::log(
        LogLevel::info,
        "No svc_* messages were interpreted at this stop; use the explicit "
        "signon-boundary mode for bounded M2.4.1 decoding");
    return 0;
  }
  case State::netchan_timed_out:
    hlclient::core::log(LogLevel::error, "GoldSrc netchan bootstrap timed out");
    return 1;
  case State::signon_boundary_reached: {
    if (!handshake.initial_signon_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Initial sign-on completed without an owned boundary result");
      return 1;
    }
    const auto &result = *handshake.initial_signon_result();
    hlclient::core::log(LogLevel::info,
                        "GoldSrc initial sign-on boundary reached");
    hlclient::core::log(LogLevel::info,
                        "Decoded early service messages: " +
                            std::to_string(result.messages.size()));
    hlclient::core::log(
        LogLevel::info,
        "Boundary opcode: " +
            std::to_string(static_cast<unsigned int>(result.boundary.opcode)) +
            " (" +
            std::string{hlclient::goldsrc::to_string(result.boundary.opcode)} +
            "), offset=" + std::to_string(result.boundary.byte_offset) +
            ", unconsumed-body=" +
            std::to_string(result.boundary.remaining_byte_count) + " bytes");
    hlclient::core::log(
        LogLevel::info,
        "No boundary body, resource list, or server command was executed");
    return 0;
  }
  case State::signon_timed_out:
    hlclient::core::log(LogLevel::error, "GoldSrc initial sign-on timed out");
    return 1;
  case State::signon_unsupported_service:
    hlclient::core::log(LogLevel::error,
                        "Unsupported service opcode before sign-on boundary");
    return 1;
  case State::signon_backpressure:
    hlclient::core::log(LogLevel::error,
                        "Initial sign-on event queue reached its hard bound");
    return 1;
  case State::signon_secondary_stream_pending_m3:
    hlclient::core::log(
        LogLevel::error,
        "Unconfirmed secondary netchan stream remains pending M3");
    return 1;
  case State::pre_resource_boundary_reached: {
    if (!handshake.pre_resource_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Pre-resource sign-on completed without an owned typed result");
      return 1;
    }
    const auto &result = *handshake.pre_resource_result();
    const auto &server_info = result.server_info();
    const auto &boundary = result.boundary();
    hlclient::core::log(LogLevel::info, "[signon] server-info decoded");
    hlclient::core::log(LogLevel::info,
                        "[signon] protocol=" +
                            std::to_string(static_cast<std::uint32_t>(
                                server_info.protocol_version())));
    hlclient::core::log(LogLevel::info,
                        "[signon] max-clients=" +
                            std::to_string(static_cast<unsigned int>(
                                server_info.maximum_clients().value())));
    hlclient::core::log(LogLevel::info,
                        std::string{"[signon] multi-client="} +
                            (server_info.multi_client_mode() ? "yes" : "no"));
    hlclient::core::log(
        LogLevel::info,
        "[signon] game=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                server_info.game_directory()));
    hlclient::core::log(
        LogLevel::info,
        "[signon] map=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                server_info.map_file_path()));
    hlclient::core::log(LogLevel::info,
                        "[signon] confirmed-pre-resource-controls=" +
                            std::to_string(result.controls().size()));
    hlclient::core::log(
        LogLevel::info,
        "[signon] pre-resource boundary opcode=" +
            std::to_string(static_cast<unsigned int>(boundary.opcode())) +
            " offset=" + std::to_string(boundary.byte_offset()) +
            " unconsumed-body=" +
            std::to_string(boundary.remaining_byte_count()) +
            " bytes direction=" +
            std::string{hlclient::goldsrc::to_string(boundary.direction())} +
            " evidence=" +
            std::string{
                hlclient::goldsrc::to_string(boundary.evidence_status())});
    hlclient::core::log(LogLevel::info,
                        "No resource request was sent; the complex boundary "
                        "body remains untouched");
    return 0;
  }
  case State::pre_resource_timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc pre-resource sign-on timed out");
    return 1;
  case State::pre_resource_unsupported_message:
    hlclient::core::log(
        LogLevel::error,
        "Unsupported service opcode before the pre-resource boundary");
    return 1;
  case State::pre_resource_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "Pre-resource sign-on event queue reached its hard bound");
    return 1;
  case State::pre_resource_secondary_stream_pending_m3:
    hlclient::core::log(
        LogLevel::error,
        "Unconfirmed secondary netchan stream remains pending M3");
    return 1;
  case State::delta_schemas_ready: {
    if (!handshake.delta_description_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Delta-description sign-on completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.delta_description_result();
    const auto &server_info = result.pre_resource().server_info();
    const auto &registry = result.registry();
    const auto &boundary = result.boundary();
    for (const auto &schema : registry.schemas()) {
      hlclient::core::log(
          LogLevel::info,
          "[signon] delta schema decoded: " +
              hlclient::goldsrc::sanitize_service_text_for_presentation(
                  schema.name()) +
              ", fields=" + std::to_string(schema.field_count()));
    }
    hlclient::core::log(
        LogLevel::info,
        "[signon] delta registry ready: schemas=" +
            std::to_string(registry.schema_count()) +
            ", fields=" + std::to_string(registry.total_field_count()));
    hlclient::core::log(
        LogLevel::info,
        "[session] serverinfo_received=true schema_registry_received=true "
        "authentication_status=pending_or_unknown "
        "terminal_reason=delta_schemas_ready "
        "protocol=" +
            std::to_string(
                static_cast<std::uint32_t>(server_info.protocol_version())) +
            " max-clients=" +
            std::to_string(static_cast<unsigned int>(
                server_info.maximum_clients().value())) +
            " game=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                server_info.game_directory()) +
            " map=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                server_info.map_file_path()));
    hlclient::core::log(
        LogLevel::info,
        "[signon] next boundary opcode=" +
            std::to_string(static_cast<unsigned int>(boundary.opcode())) +
            " offset=" + std::to_string(boundary.byte_offset()) +
            " unconsumed-body=" +
            std::to_string(boundary.remaining_byte_count()) + " bytes");
    hlclient::core::log(
        LogLevel::info,
        "No post-delta body was parsed and no resource response was sent");
    return 0;
  }
  case State::delta_timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc delta-schema sign-on timed out");
    return 1;
  case State::delta_unsupported_message:
    hlclient::core::log(LogLevel::error,
                        "Unsupported delta-description message");
    return 1;
  case State::delta_backpressure:
    hlclient::core::log(LogLevel::error,
                        "Delta-description event queue reached its hard bound");
    return 1;
  case State::delta_secondary_stream_pending_m3:
    hlclient::core::log(LogLevel::error,
                        "Unconfirmed secondary stream remains pending M3");
    return 1;
  case State::movement_environment_boundary_reached: {
    if (!handshake.movement_environment_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Movement/environment sign-on completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.movement_environment_result();
    const auto &move_vars = result.move_vars();
    const auto &boundary = result.boundary();
    hlclient::core::log(LogLevel::info,
                        "[signon] movement/environment state decoded");
    hlclient::core::log(LogLevel::info,
                        "[signon] gravity=" +
                            std::to_string(move_vars.gravity()));
    hlclient::core::log(LogLevel::info,
                        "[signon] max-speed=" +
                            std::to_string(move_vars.maximum_speed()));
    hlclient::core::log(
        LogLevel::info,
        "[signon] acceleration=" + std::to_string(move_vars.acceleration()) +
            " air-acceleration=" +
            std::to_string(move_vars.air_acceleration()));
    hlclient::core::log(
        LogLevel::info,
        "[signon] friction=" + std::to_string(move_vars.friction()) +
            " step-size=" + std::to_string(move_vars.step_size()) +
            " max-velocity=" + std::to_string(move_vars.maximum_velocity()));
    hlclient::core::log(LogLevel::info,
                        "[signon] footsteps=" +
                            std::string{move_vars.footsteps() ? "yes" : "no"});
    hlclient::core::log(
        LogLevel::info,
        "[signon] sky-name=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                move_vars.sky_name()));
    hlclient::core::log(LogLevel::info,
                        "[signon] confirmed-post-movevars-controls=" +
                            std::to_string(result.control_count()));
    hlclient::core::log(
        LogLevel::info,
        "[signon] next neutral boundary opcode=" +
            std::to_string(static_cast<unsigned int>(boundary.opcode())) +
            " offset=" + std::to_string(boundary.byte_offset()) +
            " unconsumed-body=" +
            std::to_string(boundary.remaining_byte_count()) + " bytes");
    hlclient::core::log(
        LogLevel::info,
        "Move variables were not applied; the boundary body remains untouched "
        "and no resource response was sent");
    return 0;
  }
  case State::movevars_timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc movement/environment sign-on timed out");
    return 1;
  case State::movevars_unsupported_message:
    hlclient::core::log(LogLevel::error,
                        "Unsupported post-movevars service message");
    return 1;
  case State::movevars_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "Movement/environment event queue reached its hard bound");
    return 1;
  case State::movevars_secondary_stream_pending_m3:
    hlclient::core::log(LogLevel::error,
                        "Unconfirmed secondary stream remains pending M3");
    return 1;
  case State::user_info_complete: {
    if (!handshake.user_info_result()) {
      hlclient::core::log(
          LogLevel::error,
          "User-info sign-on completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.user_info_result();
    hlclient::core::log(LogLevel::info,
                        "[signon] user-info messages decoded: count=" +
                            std::to_string(result.message_count()));
    for (const auto &message : result.messages()) {
      hlclient::core::log(
          LogLevel::info,
          "[signon] user-info info-bytes=" +
              std::to_string(message.info_string_length()) +
              " entries=" + std::to_string(message.info_entry_count()) +
              " player-name-length=" +
              (message.player_name_length()
                   ? std::to_string(*message.player_name_length())
                   : std::string{"unavailable"}));
    }
    hlclient::core::log(
        LogLevel::info,
        "[signon] first service batch complete at offset=" +
            std::to_string(result.completion().final_byte_offset()) +
            "; no resource-transition request was sent");
    return 0;
  }
  case State::user_info_timed_out:
    hlclient::core::log(LogLevel::error, "GoldSrc user-info stage timed out");
    return 1;
  case State::user_info_unsupported_message:
    hlclient::core::log(LogLevel::error, "Unsupported user-info continuation");
    return 1;
  case State::user_info_backpressure:
    hlclient::core::log(LogLevel::error,
                        "User-info event queue reached its hard bound");
    return 1;
  case State::user_info_secondary_stream_pending:
    hlclient::core::log(LogLevel::error, "Secondary stream remains pending");
    return 1;
  case State::resource_transition_boundary_reached: {
    if (!handshake.resource_transition_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Resource transition completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.resource_transition_result();
    hlclient::core::log(LogLevel::info,
                        "[resource] transition request acknowledged");
    hlclient::core::log(LogLevel::info,
                        "[resource] transition control decoded, bytes=" +
                            std::to_string(result.control().body_bytes()));
    hlclient::core::log(
        LogLevel::info,
        "[resource] neutral opcode-43 boundary opcode=" +
            std::to_string(
                static_cast<unsigned int>(result.boundary().opcode())) +
            " offset=" + std::to_string(result.boundary().byte_offset()) +
            " unconsumed-body=" +
            std::to_string(result.boundary().remaining_byte_count()) +
            " bytes");
    hlclient::core::log(
        LogLevel::info,
        "Opcode-43 semantics remain evidence-gated; its body was not parsed "
        "and no resource response was sent");
    return 0;
  }
  case State::resource_transition_timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc resource transition timed out");
    return 1;
  case State::resource_transition_unsupported_message:
    hlclient::core::log(LogLevel::error,
                        "Unsupported resource-transition message");
    return 1;
  case State::resource_transition_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "Resource-transition event queue reached its hard bound");
    return 1;
  case State::resource_transition_secondary_stream_pending:
    hlclient::core::log(LogLevel::error,
                        "Secondary resource stream remains pending");
    return 1;
  case State::resource_list_client_response_required: {
    if (!handshake.resource_list_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Resource-list stage completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.resource_list_result();
    const auto &list = result.resource_list();
    std::size_t sound_count = 0U;
    std::size_t model_count = 0U;
    std::size_t decal_count = 0U;
    std::size_t generic_count = 0U;
    std::size_t event_count = 0U;
    for (const auto &entry : list.entries()) {
      switch (entry.type()) {
      case hlclient::goldsrc::ResourceType::sound:
        ++sound_count;
        break;
      case hlclient::goldsrc::ResourceType::model:
        ++model_count;
        break;
      case hlclient::goldsrc::ResourceType::decal:
        ++decal_count;
        break;
      case hlclient::goldsrc::ResourceType::generic:
        ++generic_count;
        break;
      case hlclient::goldsrc::ResourceType::event_script:
        ++event_count;
        break;
      }
    }
    hlclient::core::log(LogLevel::info,
                        "[resource] list decoded: entries=" +
                            std::to_string(list.resource_count()));
    hlclient::core::log(LogLevel::info,
                        "[resource] raw size-code sum=" +
                            std::to_string(list.total_size_code_sum()));
    hlclient::core::log(
        LogLevel::info,
        "[resource] types=sound:" + std::to_string(sound_count) +
            ",model:" + std::to_string(model_count) +
            ",decal:" + std::to_string(decal_count) +
            ",generic:" + std::to_string(generic_count) +
            ",event_script:" + std::to_string(event_count));
    hlclient::core::log(
        LogLevel::info,
        "[resource] consumed bits=" + std::to_string(list.bits_consumed()) +
            " bytes=" + std::to_string(list.bytes_consumed()));
    hlclient::core::log(LogLevel::info,
                        "[resource] next boundary=end-of-payload offset=" +
                            std::to_string(result.boundary().byte_offset()) +
                            ":" +
                            std::to_string(result.boundary().bit_offset()));
    hlclient::core::log(
        LogLevel::info,
        "[resource] required stock client response recorded as metadata; "
        "no response was built, queued, or sent");
    return 0;
  }
  case State::resource_list_unsupported_profile:
    hlclient::core::log(
        LogLevel::error,
        "Unobserved resource-list flags/profile slot is unsupported");
    return 1;
  case State::resource_list_timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc resource-list stage timed out");
    return 1;
  case State::resource_list_backpressure:
    hlclient::core::log(LogLevel::error,
                        "Resource-list event queue reached its hard bound");
    return 1;
  case State::resource_list_secondary_stream_pending:
    hlclient::core::log(LogLevel::error,
                        "Secondary resource-list stream remains pending");
    return 1;
  case State::resource_response_boundary_reached: {
    if (!handshake.resource_client_response_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Post-resource stage completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.resource_client_response_result();
    const auto &boundary = result.boundary();
    hlclient::core::log(
        LogLevel::info,
        "[resource] neutral opcode-5 response lifecycle completed");
    hlclient::core::log(
        LogLevel::info,
        "[resource] semantic bytes=" +
            std::to_string(result.response().bytes_consumed()) +
            " reliable-generation=" +
            std::to_string(result.reliable_lifecycle().reliable_generation()) +
            " transport-sends=" +
            std::to_string(result.reliable_lifecycle().transmit_count()));
    hlclient::core::log(
        LogLevel::info,
        "[resource] next server boundary opcode=" +
            (boundary.opcode()
                 ? std::to_string(static_cast<unsigned int>(*boundary.opcode()))
                 : std::string{"end-of-payload"}) +
            " offset=" + std::to_string(boundary.byte_offset()) +
            " unconsumed-body=" +
            std::to_string(boundary.remaining_byte_count()) + " bytes");
    hlclient::core::log(
        LogLevel::info,
        "The next complex body remains unparsed; no download, cache, "
        "manifest, or asset-loading action was performed");
    return 0;
  }
  case State::resource_response_provider_required:
    hlclient::core::log(
        LogLevel::error,
        "The neutral opcode-5 response requires a configured path-free "
        "resource-consistency provider; no incomplete response was sent");
    return 1;
  case State::resource_response_unsupported_profile:
    hlclient::core::log(LogLevel::error,
                        "Unsupported post-resource response profile");
    return 1;
  case State::resource_response_timed_out:
    hlclient::core::log(LogLevel::error,
                        "Post-resource response stage timed out");
    return 1;
  case State::resource_response_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "Post-resource response event queue reached its hard bound");
    return 1;
  case State::resource_response_secondary_stream_pending:
    hlclient::core::log(
        LogLevel::error,
        "Secondary post-resource response stream remains pending");
    return 1;
  case State::server_baselines_ready:
    hlclient::core::log(
        LogLevel::info,
        "[entity] bounded baseline registry publication reached");
    return 0;
  case State::entity_snapshot_ready:
    hlclient::core::log(
        LogLevel::info,
        "[entity] bounded full/delta snapshot publication reached");
    return 0;
  case State::post_resource_unsupported_message:
    if (!handshake.post_resource_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Post-resource evidence boundary has no owning result");
      return 1;
    }
    hlclient::core::log(
        LogLevel::error,
        "[signon] stock post-resource request and entity wire grammar "
        "remain evidence-pending; the first unconfirmed body is "
        "unconsumed and no continuation request was queued");
    return 2;
  case State::post_resource_missing_delta_base:
    hlclient::core::log(LogLevel::error,
                        "[entity] exact referenced delta base is unavailable");
    return 1;
  case State::post_resource_timed_out:
    hlclient::core::log(LogLevel::error,
                        "Post-resource entity-snapshot stage timed out");
    return 1;
  case State::post_resource_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "Post-resource entity-snapshot event bound was reached");
    return 1;
  case State::post_resource_secondary_stream_pending:
    hlclient::core::log(
        LogLevel::error,
        "Secondary post-resource entity stream remains pending");
    return 1;
  case State::live_runtime_state_ready: {
    if (!handshake.live_runtime_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Live runtime stop completed without an owning result");
      return 1;
    }
    const auto &result = *handshake.live_runtime_result();
    hlclient::core::log(LogLevel::info, "[signon] server-info decoded");
    hlclient::core::log(LogLevel::info,
                        "[signon] protocol=" + std::to_string(result.protocol));
    hlclient::core::log(
        LogLevel::info,
        "[signon] server-count=" + std::to_string(result.server_count) +
            " client-slot=" + std::to_string(result.client_slot));
    hlclient::core::log(LogLevel::info,
                        "[signon] max-clients=" +
                            std::to_string(result.maximum_clients));
    hlclient::core::log(
        LogLevel::info,
        "[signon] game=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                result.game_directory));
    hlclient::core::log(
        LogLevel::info,
        "[signon] map=" +
            hlclient::goldsrc::sanitize_service_text_for_presentation(
                result.map_file_path));
    hlclient::core::log(LogLevel::info,
                        "[signon] delta registry ready: schemas=" +
                            std::to_string(result.schema_count) + ", fields=" +
                            std::to_string(result.schema_field_count));
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] resource_continuation_sent=true "
        "spawn_request_queue_count=" +
            std::to_string(result.spawn_request_queue_count) +
            " spawn_request_transmitted=" +
            std::string{result.spawn_request_transmitted ? "true" : "false"} +
            " spawn_request_acknowledged=" +
            std::string{result.spawn_request_acknowledged ? "true" : "false"} +
            " signon_reply_queue_count=" +
            std::to_string(result.signon_reply_queue_count) +
            " signon_reply_transmitted=" +
            std::string{result.signon_reply_transmitted ? "true" : "false"} +
            " signon_reply_acknowledged=" +
            std::string{result.signon_reply_acknowledged ? "true" : "false"});
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] baseline_entities=" +
            std::to_string(result.baseline_entity_count) +
            " instanced_baselines=" +
            std::to_string(result.baseline_instanced_count) +
            " service_payloads=" +
            std::to_string(result.received_service_payload_count) +
            " applied_records=" +
            std::to_string(result.applied_runtime_record_count));
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] server_time_observed=" +
            std::string{result.server_time_observed ? "true" : "false"} +
            " clientdata_observed=" +
            std::string{result.clientdata_observed ? "true" : "false"} +
            " entities_observed=" +
            std::string{result.entities_observed ? "true" : "false"} +
            " clientdata_records=" +
            std::to_string(result.clientdata_record_count) +
            " entity_records=" + std::to_string(result.entity_record_count));
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] compressed_payloads=" +
            std::to_string(result.compressed_service_payload_count) +
            " wire_uncompressed_payloads=" +
            std::to_string(result.wire_uncompressed_service_payload_count) +
            " initial_encoding=" +
            std::string{result.initial_service_wire_uncompressed
                            ? "wire_uncompressed"
                            : "bzip2"} +
            " transition_encoding=" +
            std::string{result.transition_service_wire_uncompressed
                            ? "wire_uncompressed"
                            : "bzip2"} +
            " stable_progress_observed=" +
            std::string{result.stable_progress_observed ? "true" : "false"});
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] publication_revision=" +
            std::to_string(result.publication_revision) +
            " entities=" + std::to_string(result.entity_count) +
            " canonical_hash=" + std::to_string(result.canonical_state_hash) +
            " stable_interval_ms=" +
            std::to_string(result.stable_interval.count()));
    hlclient::core::log(
        LogLevel::info,
        "[session] serverinfo_received=true schema_registry_received=true "
        "resource_continuation_sent=true live_service_payloads_received=true "
        "client_world_state_published=true");
    if (result.usercmd_check) {
      const auto &check = *result.usercmd_check;
      const auto bool_text = [](const bool value) {
        return value ? std::string{"true"} : std::string{"false"};
      };
      const auto optional_number = [](const std::optional<double> &value) {
        return value ? std::to_string(*value) : std::string{"unavailable"};
      };
      const auto vector_text = [&](const auto &value) {
        if (!value.complete()) {
          return std::string{"unavailable"};
        }
        return std::to_string(*value.x) + "," + std::to_string(*value.y) + "," +
               std::to_string(*value.z);
      };
      hlclient::core::log(
          LogLevel::info,
          "[live-usercmd] production_handoff=" +
              bool_text(check.production_handoff_complete) +
              " same_driver=" + bool_text(check.same_driver_retained) +
              " schema_binding_current=" +
              bool_text(check.schema_binding_current) + " sendents_complete=" +
              bool_text(check.sendents_complete) + " terminal_rejection=" +
              bool_text(check.terminal_rejection_observed) +
              " generation=" + std::to_string(check.generation));
      hlclient::core::log(
          LogLevel::info,
          "[live-usercmd] scenario cadence_ms=" +
              std::to_string(check.command_interval.count()) +
              " durations_ms=" + std::to_string(check.durations[0].count()) +
              "," + std::to_string(check.durations[1].count()) + "," +
              std::to_string(check.durations[2].count()) + "," +
              std::to_string(check.durations[3].count()) + "," +
              std::to_string(check.durations[4].count()) +
              " amplitude=" + std::to_string(check.forward_amplitude) +
              " fixed_orientation=" + std::to_string(check.fixed_yaw_degrees) +
              "," + std::to_string(check.fixed_pitch_degrees) +
              " orientation_source=explicit_test");
      hlclient::core::log(
          LogLevel::info,
          "[live-usercmd] counters generated=" +
              std::to_string(check.generated_command_count) +
              " history=" + std::to_string(check.history_command_count) +
              " new=" + std::to_string(check.new_command_submission_count) +
              " backup=" +
              std::to_string(check.backup_command_submission_count) +
              " sent_packets=" +
              std::to_string(check.transmitted_packet_count) +
              " server_samples=" + std::to_string(check.server_samples.size()));
      for (std::size_t phase = 0U;
           phase < hlclient::goldsrc::kLiveUserCmdPhaseCount; ++phase) {
        const auto input_phase =
            static_cast<hlclient::goldsrc::LiveUserCmdInputPhase>(phase);
        hlclient::core::log(
            LogLevel::info,
            "[live-usercmd-phase] phase=" +
                std::string{hlclient::goldsrc::to_string(input_phase)} +
                " generated=" +
                std::to_string(check.generated_by_phase[phase]) + " sent=" +
                std::to_string(check.sent_by_phase[phase]) + " fresh_samples=" +
                std::to_string(check.fresh_samples_by_phase[phase]));

        std::size_t complete_sample_count = 0U;
        std::string first_origin{"unavailable"};
        std::string last_origin{"unavailable"};
        std::string first_velocity{"unavailable"};
        std::string last_velocity{"unavailable"};
        std::optional<double> projected_origin_min;
        std::optional<double> projected_origin_max;
        std::optional<double> projected_velocity_min;
        std::optional<double> projected_velocity_max;
        const auto yaw_radians =
            check.fixed_yaw_degrees * std::acos(-1.0) / 180.0;
        const auto yaw_x = std::cos(yaw_radians);
        const auto yaw_y = std::sin(yaw_radians);
        for (const auto &sample : check.server_samples) {
          if (sample.phase != input_phase || !sample.origin.complete() ||
              !sample.velocity.complete()) {
            continue;
          }
          const auto projected_origin =
              *sample.origin.x * yaw_x + *sample.origin.y * yaw_y;
          const auto projected_velocity =
              *sample.velocity.x * yaw_x + *sample.velocity.y * yaw_y;
          if (complete_sample_count == 0U) {
            first_origin = vector_text(sample.origin);
            first_velocity = vector_text(sample.velocity);
          }
          last_origin = vector_text(sample.origin);
          last_velocity = vector_text(sample.velocity);
          projected_origin_min =
              projected_origin_min
                  ? std::min(*projected_origin_min, projected_origin)
                  : projected_origin;
          projected_origin_max =
              projected_origin_max
                  ? std::max(*projected_origin_max, projected_origin)
                  : projected_origin;
          projected_velocity_min =
              projected_velocity_min
                  ? std::min(*projected_velocity_min, projected_velocity)
                  : projected_velocity;
          projected_velocity_max =
              projected_velocity_max
                  ? std::max(*projected_velocity_max, projected_velocity)
                  : projected_velocity;
          ++complete_sample_count;
        }
        hlclient::core::log(
            LogLevel::info,
            "[live-usercmd-phase-observation] phase=" +
                std::string{hlclient::goldsrc::to_string(input_phase)} +
                " complete_samples=" + std::to_string(complete_sample_count) +
                " origin_first=" + first_origin + " origin_last=" +
                last_origin + " velocity_first=" + first_velocity +
                " velocity_last=" + last_velocity + " projected_origin_min=" +
                optional_number(projected_origin_min) +
                " projected_origin_max=" +
                optional_number(projected_origin_max) +
                " projected_velocity_min=" +
                optional_number(projected_velocity_min) +
                " projected_velocity_max=" +
                optional_number(projected_velocity_max));
      }
      for (const auto &range : check.transmit_ranges) {
        hlclient::core::log(
            LogLevel::info,
            "[live-usercmd-tx] netchan_sequence=" +
                std::to_string(range.outgoing_netchan_sequence) +
                " first_new=" +
                std::to_string(range.first_new_command_sequence) +
                " last_new=" + std::to_string(range.last_new_command_sequence) +
                " new=" + std::to_string(range.new_command_count) +
                " backup=" + std::to_string(range.backup_command_count));
      }
      for (const auto &sample : check.server_samples) {
        hlclient::core::log(
            LogLevel::info,
            "[live-usercmd-sample] generation=" +
                std::to_string(sample.generation) + " publication_revision=" +
                std::to_string(sample.publication_revision) +
                " record=" + std::to_string(sample.source.record_identity) +
                " ordinal=" + std::to_string(sample.source.record_ordinal) +
                " source_sequence=" +
                std::to_string(sample.source.source_transport_sequence) +
                " freshness=observed_in_record phase=" +
                std::string{hlclient::goldsrc::to_string(sample.phase)} +
                " server_time=" + optional_number(sample.server_time_seconds) +
                " health=" + optional_number(sample.health) +
                " origin=" + vector_text(sample.origin) +
                " velocity=" + vector_text(sample.velocity));
      }
      hlclient::core::log(
          LogLevel::info,
          "[live-usercmd] outcome=" +
              std::string{hlclient::goldsrc::to_string(check.motion_outcome)} +
              " movement_verified=" + bool_text(check.movement_verified) +
              " velocity_tolerance=" +
              std::to_string(check.velocity_tolerance) +
              " origin_tolerance=" + std::to_string(check.origin_tolerance));
      hlclient::core::log(LogLevel::info,
                          "[session] terminal_reason=live_usercmd_check_ready");
      if (check.movement_verified) {
        hlclient::core::log(
            LogLevel::info,
            "[live-usercmd] "
            "result=fresh_project_client_usercmd_server_motion_verified");
        return 0;
      }
      return 2;
    }
    hlclient::core::log(LogLevel::info, "[live-runtime] usercmd_transmitted=0");
    hlclient::core::log(LogLevel::info,
                        "[session] terminal_reason=live_runtime_state_ready");
    return 0;
  }
  case State::live_runtime_timed_out:
    hlclient::core::log(
        LogLevel::error,
        "Live runtime state did not reach its bounded stable interval");
    return 1;
  case State::live_runtime_backpressure:
    hlclient::core::log(LogLevel::error,
                        "Live runtime event queue reached its hard bound");
    return 1;
  case State::live_runtime_secondary_stream_pending:
    hlclient::core::log(LogLevel::error,
                        "Secondary live runtime stream remains pending");
    return 1;
  case State::precache_manifest_ready:
    if (!handshake.precache_manifest_result()) {
      hlclient::core::log(
          LogLevel::error,
          "Precache-manifest stage completed without an owning result");
      return 1;
    }
    hlclient::core::log(LogLevel::info,
                        "[precache] same-session metadata-only manifest ready");
    return 0;
  case State::local_resources_incomplete:
    hlclient::core::log(
        LogLevel::error,
        "[precache] manifest published with incomplete local candidates");
    return 1;
  case State::unsafe_local_resources:
    hlclient::core::log(
        LogLevel::error,
        "[precache] manifest published with security-blocked resources");
    return 1;
  case State::unsupported_local_profile:
    hlclient::core::log(
        LogLevel::error,
        "[precache] manifest published with an unsupported local profile");
    return 1;
  case State::local_resource_io_error:
    hlclient::core::log(
        LogLevel::error,
        "[precache] manifest published with local lookup failures");
    return 1;
  case State::asset_imported:
    return 0;
  case State::importer_boundary_reached:
    return 0;
  case State::world_source_unavailable:
    hlclient::core::log(LogLevel::error,
                        "[asset] selected world source is unavailable");
    return 1;
  case State::asset_source_open_failed:
    hlclient::core::log(
        LogLevel::error,
        "[asset] verified selected-world source opening failed");
    return 1;
  case State::ambiguous_asset_importer:
    hlclient::core::log(LogLevel::error,
                        "[asset] world importer selection is ambiguous");
    return 1;
  case State::asset_import_failed:
    hlclient::core::log(LogLevel::error,
                        "[asset] selected world importer failed");
    return 1;
  case State::asset_dispatch_timed_out:
    hlclient::core::log(LogLevel::error,
                        "[asset] selected-world source opening timed out");
    return 1;
  case State::asset_dispatch_backpressure:
    hlclient::core::log(LogLevel::error,
                        "[asset] dispatch event queue reached its hard bound");
    return 1;
  case State::world_textures_ready:
    return 0;
  case State::world_textures_incomplete:
    hlclient::core::log(
        LogLevel::error,
        "[texture] texture set is owning but incomplete for world materials");
    return 1;
  case State::world_texture_geometry_unavailable:
    hlclient::core::log(
        LogLevel::error,
        "[texture] valid imported CPU world geometry is unavailable");
    return 1;
  case State::world_texture_worldspawn_parse_failed:
    hlclient::core::log(LogLevel::error,
                        "[texture] inert worldspawn parsing failed");
    return 1;
  case State::world_texture_wad_reference_invalid:
    hlclient::core::log(LogLevel::error,
                        "[texture] worldspawn WAD declarations are invalid");
    return 1;
  case State::world_texture_wad_source_unavailable:
    hlclient::core::log(LogLevel::error,
                        "[texture] declared WAD source is unavailable");
    return 1;
  case State::world_texture_wad_source_open_failed:
    hlclient::core::log(LogLevel::error,
                        "[texture] verified WAD source opening failed");
    return 1;
  case State::world_texture_wad_catalog_failed:
    hlclient::core::log(LogLevel::error,
                        "[texture] strict WAD3 catalog parsing failed");
    return 1;
  case State::world_texture_decode_failed:
    hlclient::core::log(LogLevel::error,
                        "[texture] strict indexed texture decoding failed");
    return 1;
  case State::world_texture_timed_out:
    hlclient::core::log(LogLevel::error,
                        "[texture] local texture import timed out");
    return 1;
  case State::world_texture_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "[texture] texture-stage event queue reached its hard bound");
    return 1;
  case State::world_render_package_ready:
    return 0;
  case State::world_render_textures_incomplete:
    hlclient::core::log(
        LogLevel::error,
        "[render-package] complete world textures are required");
    return 1;
  case State::world_render_lightmap_import_failed:
    hlclient::core::log(
        LogLevel::error,
        "[render-package] strict GoldSrc lightmap import failed");
    return 1;
  case State::world_render_package_failed:
    hlclient::core::log(
        LogLevel::error,
        "[render-package] renderer-neutral package validation failed");
    return 1;
  case State::world_render_timed_out:
    hlclient::core::log(LogLevel::error,
                        "[render-package] prerequisite pipeline timed out");
    return 1;
  case State::world_render_backpressure:
    hlclient::core::log(
        LogLevel::error,
        "[render-package] stage event queue reached its hard bound");
    return 1;
  case State::timed_out:
    hlclient::core::log(LogLevel::error,
                        "GoldSrc challenge exchange timed out");
    return 1;
  case State::cancelled:
    hlclient::core::log(LogLevel::error, "GoldSrc handshake was cancelled");
    return 1;
  case State::configuration_error:
  case State::network_error:
  case State::protocol_error:
    if (const auto &live_error = handshake.live_runtime_error()) {
      hlclient::core::log(
          LogLevel::info,
          "[live-runtime] usercmd_submissions=0 usercmd_transmitted=0");
      std::string native =
          "[native-error] live_runtime=" +
          std::string{hlclient::goldsrc::to_string(live_error->code)};
      if (live_error->response_code) {
        native += " response=" + std::string{hlclient::goldsrc::to_string(
                                     *live_error->response_code)};
      }
      if (live_error->resource_list_code) {
        native += " resource_list=" + std::string{hlclient::goldsrc::to_string(
                                          *live_error->resource_list_code)};
      }
      if (live_error->transition_stage_code) {
        native +=
            " transition_stage=" + std::string{hlclient::goldsrc::to_string(
                                       *live_error->transition_stage_code)};
      }
      if (live_error->transition_control_code) {
        native +=
            " transition_parser=" + std::string{hlclient::goldsrc::to_string(
                                        *live_error->transition_control_code)};
      }
      if (live_error->runtime_code) {
        native += " runtime=" + std::string{hlclient::goldsrc::to_string(
                                    *live_error->runtime_code)};
      }
      if (live_error->control_code) {
        native += " control=" + std::string{hlclient::goldsrc::to_string(
                                    *live_error->control_code)};
      }
      if (live_error->wire_opcode) {
        native += " opcode=" + std::to_string(*live_error->wire_opcode);
      }
      if (live_error->transition_failure_metadata &&
          live_error->transition_failure_metadata->intermediate_parser_error) {
        native += " transition_dispatch_parser=" +
                  std::string{hlclient::goldsrc::to_string(
                      *live_error->transition_failure_metadata
                           ->intermediate_parser_error)};
      }
      if (live_error->response_payload_diagnostic) {
        const auto &value = *live_error->response_payload_diagnostic;
        native +=
            " response_classification=" +
            std::string{hlclient::goldsrc::to_string(value.classification)};
        native +=
            " response_rx_position=" +
            std::string{hlclient::goldsrc::to_string(value.receive_position)};
        native += " response_payload_ordinal=" +
                  std::to_string(value.payload_ordinal);
        native += " response_actual_opcode=" +
                  (value.actual_opcode ? std::to_string(*value.actual_opcode)
                                       : std::string{"unavailable"});
        native +=
            " response_source_sequence=" +
            (value.source_sequence ? std::to_string(*value.source_sequence)
                                   : std::string{"unavailable"});
        native += " response_source_ack=" +
                  (value.source_acknowledgement
                       ? std::to_string(*value.source_acknowledgement)
                       : std::string{"unavailable"});
      }
      hlclient::core::log(LogLevel::error, native);
    }
    hlclient::core::log(
        LogLevel::error,
        handshake.error_context().empty()
            ? "GoldSrc handshake failed without diagnostic context"
            : std::string{handshake.error_context()});
    return 1;
  case State::idle:
  case State::waiting_for_challenge:
  case State::waiting_for_authentication:
  case State::building_request:
  case State::request_ready:
  case State::sending_request:
  case State::waiting_for_connect_response:
  case State::waiting_for_netchan:
  case State::waiting_for_signon:
  case State::waiting_for_pre_resource:
  case State::waiting_for_delta_schemas:
  case State::waiting_for_movevars:
  case State::waiting_for_user_info:
  case State::waiting_for_resource_transition:
  case State::waiting_for_resource_list:
  case State::waiting_for_resource_response:
  case State::waiting_for_post_resource_entity_snapshot:
  case State::waiting_for_live_runtime_state:
  case State::waiting_for_precache_manifest:
  case State::waiting_for_asset_dispatch:
  case State::waiting_for_world_textures:
  case State::waiting_for_world_render_package:
    hlclient::core::log(LogLevel::error, "GoldSrc handshake is not terminal");
    return 1;
  }
  return 1;
}

[[nodiscard]] int report_local_resource_inventory(
    const hlclient::goldsrc::ResourceClientResponseSignonState &response,
    const hlclient::local_resources::LocalResourceEnvironment &environment) {
  auto built = hlclient::goldsrc::LocalResourceInventoryBuilder{}.build(
      response.resource_list().resource_list(),
      hlclient::goldsrc::GoldSrcResourceNameMapper{}, environment.resolver());
  if (!built) {
    const auto code = built.error
                          ? hlclient::goldsrc::to_string(built.error->code)
                          : std::string_view{"unable_to_retain_inventory"};
    hlclient::core::log(LogLevel::error,
                        "[local-resource] inventory build failed: " +
                            std::string{code});
    return 1;
  }

  const auto &summary = built.state->summary();
  using Status = hlclient::goldsrc::LocalResourceInventoryStatus;
  const auto unsupported = summary.count(Status::unsupported_name_encoding) +
                           summary.count(Status::unsupported_mapping);
  hlclient::core::log(
      LogLevel::info,
      "[local-resource] inventory: resolved=" +
          std::to_string(summary.count(Status::resolved)) +
          ", missing=" + std::to_string(summary.count(Status::missing)) +
          ", unsafe=" + std::to_string(summary.count(Status::unsafe_name)) +
          ", unsupported=" + std::to_string(unsupported) +
          ", ambiguous=" + std::to_string(summary.count(Status::ambiguous)) +
          ", io-error=" + std::to_string(summary.count(Status::io_error)));
  return 0;
}

[[nodiscard]] int report_precache_manifest(
    const hlclient::goldsrc::PrecacheManifestSignonState &result) {
  const auto &manifest = result.manifest();
  const auto &summary = manifest.readiness_summary();
  using Status = hlclient::goldsrc::LocalResourceReadinessStatus;
  const auto unsupported = summary.count(Status::unsupported_name_encoding) +
                           summary.count(Status::unsupported_mapping);

  hlclient::core::log(
      LogLevel::info,
      "[local-resource] readiness: ready=" +
          std::to_string(summary.count(Status::ready_local_file)) +
          ", metadata-only=" +
          std::to_string(summary.count(Status::metadata_only)) + ", missing=" +
          std::to_string(summary.count(Status::missing_local_file)) +
          ", unsafe=" + std::to_string(summary.count(Status::unsafe_name)) +
          ", unsupported=" + std::to_string(unsupported) + ", ambiguous=" +
          std::to_string(summary.count(Status::ambiguous_local_match)) +
          ", io-error=" +
          std::to_string(summary.count(Status::local_io_error)));
  hlclient::core::log(
      LogLevel::info,
      "[precache] manifest: entries=" + std::to_string(manifest.entry_count()) +
          ", sound-slots=" +
          std::to_string(manifest.sound_slots().slot_count()) +
          ", model-slots=" +
          std::to_string(manifest.model_slots().slot_count()) +
          ", generic-slots=" +
          std::to_string(manifest.generic_slots().slot_count()) +
          ", event-slots=" +
          std::to_string(manifest.event_script_slots().slot_count()) +
          ", decal-slots=" +
          std::to_string(manifest.decal_slots().slot_count()));
  hlclient::core::log(
      LogLevel::info,
      "[precache] world: status=" +
          std::string{hlclient::goldsrc::to_string(
              manifest.world_selection().status())} +
          ", resource-index=" +
          (manifest.world_selection().resource_index()
               ? std::to_string(*manifest.world_selection().resource_index())
               : std::string{"unavailable"}));
  hlclient::core::log(
      LogLevel::info,
      "[precache] completeness=" +
          std::string{hlclient::goldsrc::to_string(manifest.completeness())});
  return hlclient::app::precache_manifest_exit_code(manifest.completeness());
}

[[nodiscard]] int report_asset_dispatch(
    const hlclient::goldsrc::ApprovedAssetDispatchState &result,
    const bool require_world_geometry) {
  const auto &plan = result.plan();
  const auto &dispatched = result.dispatch_result();
  hlclient::core::log(
      LogLevel::info,
      "[asset] world source selected: type=" +
          std::string{hlclient::goldsrc::to_string(plan.resource_type())} +
          " index=" + std::to_string(plan.resource_index()));
  hlclient::core::log(LogLevel::info,
                      "[asset] source opened: bytes=" +
                          std::to_string(result.source_byte_count()));
  auto category = dispatched.selected_category;
  if (category == hlclient::assets::AssetImporterCategory::none &&
      plan.role() == hlclient::assets::AssetDispatchRole::world) {
    category = hlclient::assets::AssetImporterCategory::world;
  }
  const auto dispatch_summary =
      dispatched.state == hlclient::assets::AssetDispatchState::imported
          ? std::string_view{"imported"}
      : dispatched.state ==
              hlclient::assets::AssetDispatchState::importer_not_registered
          ? std::string_view{"no-match"}
      : dispatched.state ==
              hlclient::assets::AssetDispatchState::ambiguous_importer
          ? std::string_view{"ambiguous"}
          : std::string_view{"failed"};
  hlclient::core::log(LogLevel::info,
                      "[asset] importer dispatch: category=" +
                          std::string{hlclient::assets::to_string(category)} +
                          " result=" + std::string{dispatch_summary});
  if (!dispatched.selected_importer_id.empty()) {
    hlclient::core::log(LogLevel::info,
                        "[asset] importer=" + dispatched.selected_importer_id);
  }
  if (!require_world_geometry) {
    return dispatched.state == hlclient::assets::AssetDispatchState::imported ||
                   dispatched.state == hlclient::assets::AssetDispatchState::
                                           importer_not_registered
               ? 0
               : 1;
  }

  if (dispatched.state != hlclient::assets::AssetDispatchState::imported ||
      !dispatched.asset) {
    if (dispatched.error && dispatched.error->context.find(
                                "; classification=") != std::string::npos) {
      hlclient::core::log(LogLevel::error, "[world] face geometry error: " +
                                               dispatched.error->context);
    }
    hlclient::core::log(LogLevel::error,
                        "[world] CPU world geometry was not imported");
    return 1;
  }
  const auto *world =
      std::get_if<hlclient::assets::WorldAsset>(&*dispatched.asset);
  if (world == nullptr || world->vertices.empty() || world->indices.empty() ||
      world->surfaces.empty() || world->indices.size() % 3U != 0U) {
    hlclient::core::log(
        LogLevel::error,
        "[world] imported asset has no valid non-empty CPU geometry");
    return 1;
  }

  const auto finite_vector = [](const hlclient::assets::AssetVector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };
  const bool finite_bounds =
      finite_vector(world->bounds.minimum) &&
      finite_vector(world->bounds.maximum) &&
      world->bounds.minimum.x <= world->bounds.maximum.x &&
      world->bounds.minimum.y <= world->bounds.maximum.y &&
      world->bounds.minimum.z <= world->bounds.maximum.z;
  if (!finite_bounds) {
    hlclient::core::log(LogLevel::error,
                        "[world] imported CPU geometry has invalid bounds");
    return 1;
  }

  hlclient::core::log(LogLevel::info, "[world] BSP geometry imported");
  hlclient::core::log(
      LogLevel::info,
      "[world] canonicalized-face-orientations=" +
          std::to_string(
              world->statistics.canonicalized_face_orientation_count));
  hlclient::core::log(
      LogLevel::info,
      "[world] winding-profile=" +
          std::string{hlclient::goldsrc::bsp::to_string(
              hlclient::goldsrc::bsp::
                  GoldSrcFaceOrientationCompatibilityProfile::
                      valve_qbsp_clockwise_wire_to_counter_clockwise_render)});
  hlclient::core::log(LogLevel::info,
                      "[world] vertices=" +
                          std::to_string(world->vertices.size()));
  hlclient::core::log(LogLevel::info,
                      "[world] triangles=" +
                          std::to_string(world->indices.size() / 3U));
  hlclient::core::log(LogLevel::info,
                      "[world] surfaces=" +
                          std::to_string(world->surfaces.size()));
  hlclient::core::log(LogLevel::info,
                      "[world] materials=" +
                          std::to_string(world->materials.size()));
  hlclient::core::log(LogLevel::info,
                      "[world] source-models=" +
                          std::to_string(world->statistics.source_model_count));
  hlclient::core::log(
      LogLevel::info,
      "[world] skipped-submodel-faces=" +
          std::to_string(world->statistics.skipped_submodel_face_count));
  hlclient::core::log(LogLevel::info, "[world] textures:");
  hlclient::core::log(
      LogLevel::info,
      "[world] embedded=" +
          std::to_string(world->statistics.embedded_texture_reference_count));
  hlclient::core::log(
      LogLevel::info,
      "[world] external=" +
          std::to_string(world->statistics.external_texture_reference_count));
  hlclient::core::log(
      LogLevel::info,
      "[world] missing=" +
          std::to_string(world->statistics.missing_texture_reference_count));
  hlclient::core::log(LogLevel::info, "[world] bounds: finite=true");
  return 0;
}

[[nodiscard]] int report_world_textures(
    const hlclient::goldsrc::TexturedWorldAssetState &result) {
  const int geometry_result =
      report_asset_dispatch(result.dispatch_state(), true);
  if (geometry_result != 0) {
    return geometry_result;
  }

  const auto &world = result.world();
  const auto &textures = result.textures();
  const auto &statistics = textures.statistics();
  hlclient::core::log(
      LogLevel::info,
      "[texture] BSP sources: embedded=" +
          std::to_string(world.statistics.embedded_texture_reference_count) +
          ", external=" +
          std::to_string(world.statistics.external_texture_reference_count) +
          ", missing=" +
          std::to_string(world.statistics.missing_texture_reference_count));
  hlclient::core::log(LogLevel::info,
                      "[texture] WAD declarations=" +
                          std::to_string(statistics.wad_declaration_count));
  hlclient::core::log(
      LogLevel::info,
      "[texture] WAD resolved=" +
          std::to_string(statistics.wad_archive_resolved_count));
  hlclient::core::log(LogLevel::info,
                      "[texture] WAD missing=" +
                          std::to_string(statistics.wad_archive_missing_count));
  hlclient::core::log(
      LogLevel::info,
      "[texture] decoded: textures=" +
          std::to_string(statistics.decoded_texture_count) +
          ", embedded=" + std::to_string(statistics.embedded_texture_count) +
          ", wad3=" + std::to_string(statistics.wad3_texture_count) +
          ", masked=" + std::to_string(statistics.masked_texture_count) +
          ", rgba-bytes=" + std::to_string(statistics.total_rgba_byte_count));
  hlclient::core::log(LogLevel::info,
                      "[texture] material bindings: resolved=" +
                          std::to_string(statistics.material_binding_count -
                                         statistics.unresolved_material_count) +
                          ", unresolved=" +
                          std::to_string(statistics.unresolved_material_count));
  hlclient::core::log(LogLevel::info,
                      std::string{"[texture] completeness="} +
                          (textures.complete_for_world_materials()
                               ? "complete"
                               : "incomplete"));
  return textures.complete_for_world_materials() ? 0 : 1;
}

[[nodiscard]] int report_world_render_package(
    const hlclient::world_render::WorldRenderPackage &package) {
  const auto &statistics = package.statistics();
  const bool valid =
      package.textured_world().textures.complete_for_world_materials() &&
      package.lightmaps().complete_for_world_surfaces() &&
      !package.vertices().empty() && !package.indices().empty() &&
      !package.materials().empty() && !package.draw_batches().empty();
  hlclient::core::log(valid ? LogLevel::info : LogLevel::error,
                      std::string{"[render-package] completeness="} +
                          (valid ? "complete" : "incomplete"));
  hlclient::core::log(
      LogLevel::info,
      "[render-package] vertices=" + std::to_string(statistics.vertex_count) +
          ", triangles=" + std::to_string(statistics.triangle_count) +
          ", materials=" + std::to_string(statistics.material_count) +
          ", batches=" + std::to_string(statistics.batch_count));
  hlclient::core::log(
      LogLevel::info,
      "[render-package] lightmap-pages=" +
          std::to_string(package.lightmaps().page_count()) + ", cpu-bytes=" +
          std::to_string(statistics.total_cpu_render_byte_count) +
          ", revision=" + std::to_string(package.resource_revision()));
  return valid ? 0 : 1;
}

[[nodiscard]] int report_world_spatial_scene(
    const hlclient::world_preview::WorldPreviewSceneSource &preview) {
  const auto &state = preview.world_state();
  const auto &scene = state.world_scene();
  const auto &visibility = state.world_visibility();
  const auto &draw_list = state.visible_draw_list();
  if (!scene || !visibility || !draw_list) {
    hlclient::core::log(
        LogLevel::error,
        "World spatial scene did not publish bounded visibility data");
    return 1;
  }

  const auto &spatial = scene->spatial_package();
  const auto &spatial_statistics = spatial.statistics();
  const auto &visibility_statistics = visibility->statistics();
  const auto &scene_statistics = scene->statistics();
  const auto &draw_statistics = draw_list->statistics();
  hlclient::core::log(
      LogLevel::info,
      "[spatial] nodes=" + std::to_string(spatial_statistics.node_count) +
          " leaves=" + std::to_string(spatial_statistics.leaf_count) +
          " pvs-rows=" +
          std::to_string(spatial_statistics.unique_pvs_row_count));
  hlclient::core::log(LogLevel::info,
                      "[visibility] mode=" +
                          std::string{hlclient::world_visibility::to_string(
                              visibility->applied_mode())});
  hlclient::core::log(
      LogLevel::info,
      "[visibility] camera-leaf=" +
          (visibility->camera_leaf_index()
               ? std::to_string(*visibility->camera_leaf_index())
               : std::string{"unavailable"}));
  hlclient::core::log(
      LogLevel::info,
      "[visibility] world=" +
          std::to_string(visibility_statistics.visible_world_surface_count) +
          "/" +
          std::to_string(visibility_statistics.total_world_surface_count));
  hlclient::core::log(
      LogLevel::info,
      "[visibility] pvs-culled=" +
          std::to_string(
              visibility_statistics.world_surface_culled_by_pvs_count));
  hlclient::core::log(
      LogLevel::info,
      "[visibility] frustum-culled=" +
          std::to_string(
              visibility_statistics.world_surface_culled_by_frustum_count));
  hlclient::core::log(LogLevel::info,
                      "[brush] models=" +
                          std::to_string(scene_statistics.brush_model_count));
  hlclient::core::log(
      LogLevel::info,
      "[brush] instances=" +
          std::to_string(scene_statistics.brush_instance_count) +
          " supported=" +
          std::to_string(scene_statistics.supported_brush_instance_count) +
          " visible=" +
          std::to_string(visibility_statistics.visible_brush_instance_count) +
          " unsupported=" +
          std::to_string(scene_statistics.unsupported_brush_instance_count));
  hlclient::core::log(
      LogLevel::info,
      "[render] commands=" + std::to_string(draw_statistics.command_count) +
          " uploads=0 draws=0 triangles=" +
          std::to_string(draw_statistics.triangle_count));
  return 0;
}

[[nodiscard]] hlclient::network::UdpSocket open_challenge_socket(
    const hlclient::network::NetworkRuntime &runtime,
    const hlclient::network::NetworkAddress &remote_endpoint) {
  if (!runtime.valid()) {
    throw std::runtime_error{"Network runtime initialization failed: " +
                             runtime.error_message()};
  }

  std::string error;
  auto socket = hlclient::network::UdpSocket::open_ipv4(runtime, error);
  if (!socket) {
    throw std::runtime_error{
        error.empty() ? "Unable to open a nonblocking IPv4 UDP socket" : error};
  }

  const auto loopback_address = hlclient::network::NetworkAddress::loopback(0);
  const auto local_address =
      remote_endpoint.ipv4_host_order() == loopback_address.ipv4_host_order()
          ? loopback_address
          : hlclient::network::NetworkAddress{0U, 0U};
  if (!socket->bind(local_address, error)) {
    throw std::runtime_error{
        error.empty() ? "Unable to bind the challenge UDP socket" : error};
  }
  return std::move(*socket);
}

class HandshakeSession final {
public:
  HandshakeSession(
      const hlclient::network::NetworkAddress remote_endpoint,
      const hlclient::goldsrc::HandshakeStopPoint stop_point,
      std::optional<hlclient::goldsrc::PreparedConnectRequest> prepared_request,
      std::optional<hlclient::auth::AuthenticationSession>
          authentication_session,
      std::unique_ptr<hlclient::auth::IAuthenticationProvider>
          authentication_provider,
      hlclient::goldsrc::ClientConnectionSettings authentication_settings,
      hlclient::goldsrc::ConnectCompatibilityProfile authentication_profile,
      hlclient::resource_consistency::IResourceConsistencyProvider
          *resource_consistency_provider,
      std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
          local_resource_environment,
      const hlclient::assets::AssetImporterRegistries
          *asset_importer_registries,
      hlclient::goldsrc::PrecacheAssetDispatchStageConfig asset_dispatch_config,
      hlclient::goldsrc::WorldTextureImportStageConfig world_texture_config,
      hlclient::goldsrc::WorldRenderPackageStageConfig
          world_render_package_config,
      hlclient::client::ClientWorldState *live_runtime_target,
      const hlclient::goldsrc::LiveRuntimeOperationMode live_runtime_mode,
      const hlclient::goldsrc::LiveVisualControlInputSource
          live_visual_input_source,
      const bool reference_prediction,
      const bool net_trace)
      : local_resource_environment_{std::move(local_resource_environment)},
        authentication_provider_{std::move(authentication_provider)},
        network_runtime_{},
        transport_{open_challenge_socket(network_runtime_, remote_endpoint)},
        handshake_{
            transport_,
            remote_endpoint,
            stop_point,
            std::move(prepared_request),
            {},
            net_trace ? hlclient::goldsrc::
                            ChallengeTraceCallback{&log_challenge_trace}
                      : hlclient::goldsrc::ChallengeTraceCallback{},
            net_trace ? hlclient::goldsrc::
                            ConnectRequestTraceCallback{&log_connect_trace}
                      : hlclient::goldsrc::ConnectRequestTraceCallback{},
            {},
            net_trace
                ? hlclient::goldsrc::
                      ConnectResponseTraceCallback{&log_connect_response_trace}
                : hlclient::goldsrc::ConnectResponseTraceCallback{},
            std::move(authentication_session),
            {},
            net_trace ? hlclient::goldsrc::
                            NetchanBootstrapTraceCallback{&log_netchan_trace}
                      : hlclient::goldsrc::NetchanBootstrapTraceCallback{},
            [&] {
              hlclient::goldsrc::InitialSignonConfig config;
              if (stop_point ==
                  hlclient::goldsrc::HandshakeStopPoint::live_runtime_state) {
                config.service_payload_envelope.compression_policy =
                    hlclient::goldsrc::ServicePayloadCompressionPolicy::
                        accept_bzip2_or_uncompressed;
              }
              return config;
            }(),
            net_trace
                ? hlclient::goldsrc::
                      InitialSignonTraceCallback{&log_initial_signon_trace}
                : hlclient::goldsrc::InitialSignonTraceCallback{},
            {},
            net_trace
                ? hlclient::goldsrc::
                      PreResourceSignonTraceCallback{&log_pre_resource_signon_trace}
                : hlclient::goldsrc::PreResourceSignonTraceCallback{},
            {},
            net_trace
                ? hlclient::goldsrc::
                      DeltaDescriptionTraceCallback{&log_delta_description_trace}
                : hlclient::goldsrc::DeltaDescriptionTraceCallback{},
            {},
            net_trace
                ? hlclient::goldsrc::
                      MovementEnvironmentTraceCallback{&log_movement_environment_trace}
                : hlclient::goldsrc::MovementEnvironmentTraceCallback{},
            {},
            net_trace ? hlclient::goldsrc::
                            UserInfoSignonTraceCallback{&log_user_info_trace}
                      : hlclient::goldsrc::UserInfoSignonTraceCallback{},
            {},
            net_trace
                ? hlclient::goldsrc::
                      ResourceTransitionTraceCallback{&log_resource_transition_trace}
                : hlclient::goldsrc::ResourceTransitionTraceCallback{},
            {},
            net_trace ? hlclient::goldsrc::
                            ResourceListTraceCallback{&log_resource_list_trace}
                      : hlclient::goldsrc::ResourceListTraceCallback{},
            {},
            resource_consistency_provider,
            hlclient::goldsrc::ResourceClientResponseTraceCallback{
                &log_resource_client_response_trace},
            local_resource_environment_,
            {},
            net_trace
                ? hlclient::goldsrc::
                      PrecacheManifestTraceCallback{&log_precache_manifest_trace}
                : hlclient::goldsrc::PrecacheManifestTraceCallback{},
            asset_importer_registries,
            std::move(asset_dispatch_config),
            net_trace
                ? hlclient::goldsrc::
                      PrecacheAssetDispatchTraceCallback{&log_precache_asset_dispatch_trace}
                : hlclient::goldsrc::PrecacheAssetDispatchTraceCallback{},
            std::move(world_texture_config),
            net_trace
                ? hlclient::goldsrc::
                      WorldTextureImportTraceCallback{&log_world_texture_import_trace}
                : hlclient::goldsrc::WorldTextureImportTraceCallback{},
            std::move(world_render_package_config),
            net_trace
                ? hlclient::goldsrc::
                      WorldRenderPackageTraceCallback{&log_world_render_package_trace}
                : hlclient::goldsrc::WorldRenderPackageTraceCallback{},
            {},
            net_trace
                ? hlclient::goldsrc::
                      PostResourceEntitySnapshotTraceCallback{&log_post_resource_entity_snapshot_trace}
                : hlclient::goldsrc::PostResourceEntitySnapshotTraceCallback{},
            authentication_provider_.get(),
            std::move(authentication_settings),
            authentication_profile,
            live_runtime_target,
            [&] {
              hlclient::goldsrc::LiveRuntimeStageConfig config;
              config.operation_mode = live_runtime_mode;
              config.live_visual_input_source = live_visual_input_source;
              config.reference_prediction = reference_prediction;
              if (live_visual_input_source == hlclient::goldsrc::
                      LiveVisualControlInputSource::scripted_jump_duck_check) {
                config.usercmd_scenario.durations = {
                    std::chrono::seconds{2}, std::chrono::milliseconds{1200},
                    std::chrono::seconds{2}, std::chrono::milliseconds{1500},
                    std::chrono::milliseconds{2500}};
              } else if (live_visual_input_source == hlclient::goldsrc::
                      LiveVisualControlInputSource::scripted_speed_check) {
                config.usercmd_scenario.forward_amplitude = static_cast<std::int16_t>(
                    hlclient::goldsrc::kLiveReferenceManualSpeeds.forward_speed);
                config.usercmd_scenario.durations = {
                    std::chrono::seconds{2}, std::chrono::milliseconds{600},
                    std::chrono::seconds{1}, std::chrono::seconds{1},
                    std::chrono::seconds{1}};
              }
              if (live_runtime_mode == hlclient::goldsrc::
                                           LiveRuntimeOperationMode::
                                               live_visual_control) {
                config.timeout = std::chrono::seconds{60};
              }
              return config;
            }(),
            hlclient::goldsrc::LiveRuntimeStageTraceCallback{
                &log_live_runtime_trace}} {
    hlclient::core::log(LogLevel::info, "GoldSrc challenge exchange started");
    hlclient::core::log(LogLevel::info,
                        "Server: " + remote_endpoint.to_string());

    static_cast<void>(
        handshake_.start(hlclient::goldsrc::ChallengeExchangeClock::now()));
    if (handshake_.local_endpoint()) {
      hlclient::core::log(LogLevel::info,
                          "Local endpoint: " +
                              handshake_.local_endpoint()->to_string());
    }
  }

  void update(const hlclient::goldsrc::ChallengeExchangeTimePoint now) {
    handshake_.update(now);
  }

  void cancel(const hlclient::goldsrc::ChallengeExchangeTimePoint now) {
    handshake_.cancel(now);
  }

  [[nodiscard]] bool terminal() const noexcept { return handshake_.terminal(); }

  [[nodiscard]] int
  report_result(const bool require_world_geometry = false,
                const bool require_world_textures = false,
                const bool require_world_render_package = false) const {
    const int handshake_result = report_handshake_result(handshake_);
    if (handshake_.world_render_package_result()) {
      const int package_result = report_world_render_package(
          *handshake_.world_render_package_result());
      return handshake_result != 0 ? handshake_result : package_result;
    }
    if (require_world_render_package) {
      return handshake_result != 0 ? handshake_result : 1;
    }
    if (handshake_.world_texture_result()) {
      const int texture_result =
          report_world_textures(*handshake_.world_texture_result());
      return handshake_result != 0 ? handshake_result : texture_result;
    }
    if (require_world_textures) {
      return handshake_result != 0 ? handshake_result : 1;
    }
    if (handshake_.asset_dispatch_result()) {
      const int dispatch_result = report_asset_dispatch(
          *handshake_.asset_dispatch_result(), require_world_geometry);
      return handshake_result != 0 ? handshake_result : dispatch_result;
    }
    if (handshake_.precache_manifest_result()) {
      const int manifest_result =
          report_precache_manifest(*handshake_.precache_manifest_result());
      return handshake_result != 0 ? handshake_result : manifest_result;
    }
    if (handshake_result != 0 || !local_resource_environment_ ||
        !handshake_.resource_client_response_result()) {
      return handshake_result;
    }
    return report_local_resource_inventory(
        *handshake_.resource_client_response_result(),
        *local_resource_environment_);
  }

  [[nodiscard]] const std::shared_ptr<
      const hlclient::world_render::WorldRenderPackage> &
  world_render_package() const noexcept {
    return handshake_.world_render_package_result();
  }

  [[nodiscard]] const std::shared_ptr<
      const hlclient::world_scene_render::WorldSceneRenderPackage> &
  world_spatial_scene() const noexcept {
    return handshake_.world_spatial_scene_result();
  }

  [[nodiscard]] const std::optional<
      hlclient::goldsrc::brush_models::GoldSrcSpawnCameraExtractionResult> &
  world_spawn_camera() const noexcept {
    return handshake_.world_spawn_camera_result();
  }

  [[nodiscard]] const std::optional<
      hlclient::goldsrc::ApprovedAssetDispatchState> &
  asset_dispatch_state() const noexcept {
    return handshake_.asset_dispatch_result();
  }

  [[nodiscard]] bool live_visual_input_ready() const noexcept {
    return handshake_.live_visual_input_ready();
  }

  [[nodiscard]] bool submit_live_visual_input(
      const hlclient::goldsrc::LiveVisualControlInput &input,
      const hlclient::goldsrc::ChallengeExchangeTimePoint now) noexcept {
    return handshake_.submit_live_visual_input(input, now);
  }

  [[nodiscard]] bool activate_live_visual_control(
      const hlclient::goldsrc::ChallengeExchangeTimePoint now) noexcept {
    return handshake_.activate_live_visual_control(now);
  }

  [[nodiscard]] std::optional<hlclient::goldsrc::LiveUserCmdCheckState>
  live_usercmd_snapshot() const {
    return handshake_.live_usercmd_snapshot();
  }

  [[nodiscard]] bool attach_reference_prediction_collision(
      std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package) {
    return handshake_.attach_reference_prediction_collision(std::move(package));
  }

  [[nodiscard]] hlclient::goldsrc::LiveReferencePredictionSnapshot
  live_reference_prediction_snapshot(const hlclient::goldsrc::ChallengeExchangeTimePoint now) const {
    return handshake_.live_reference_prediction_snapshot(now);
  }

  [[nodiscard]] const std::optional<hlclient::goldsrc::LiveRuntimeState> &
  live_runtime_result() const noexcept {
    return handshake_.live_runtime_result();
  }

  [[nodiscard]] const std::optional<hlclient::goldsrc::LiveRuntimeStageError> &
  live_runtime_error() const noexcept {
    return handshake_.live_runtime_error();
  }

  [[nodiscard]] const hlclient::goldsrc::ResourceListState *
  live_resource_list() const noexcept {
    return handshake_.live_resource_list();
  }

  [[nodiscard]] const hlclient::goldsrc::ServerInfoState *
  live_server_info() const noexcept {
    return handshake_.live_server_info();
  }

private:
  std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
      local_resource_environment_;
  std::unique_ptr<hlclient::auth::IAuthenticationProvider>
      authentication_provider_;
  hlclient::network::NetworkRuntime network_runtime_;
  hlclient::network::UdpDatagramTransport transport_;
  hlclient::goldsrc::GoldSrcHandshakeCoordinator handshake_;
};

struct RuntimeConnectPreparation {
  std::optional<hlclient::goldsrc::PreparedConnectRequest> request;
  std::optional<hlclient::auth::AuthenticationSession> authentication_session;
  std::unique_ptr<hlclient::auth::IAuthenticationProvider> provider;
  hlclient::goldsrc::ClientConnectionSettings settings;
  hlclient::goldsrc::ConnectCompatibilityProfile profile;
};

[[noreturn]] void
throw_authentication_error(const hlclient::auth::AuthenticationError &error) {
  const auto context = error.context.empty()
                           ? "Explicit authentication provider failed"
                           : error.context;
  switch (error.code) {
  case hlclient::auth::AuthenticationErrorCode::unavailable:
  case hlclient::auth::AuthenticationErrorCode::provider_error:
    throw std::runtime_error{context};
  case hlclient::auth::AuthenticationErrorCode::configuration_error:
  case hlclient::auth::AuthenticationErrorCode::invalid_material:
  case hlclient::auth::AuthenticationErrorCode::material_too_large:
  case hlclient::auth::AuthenticationErrorCode::timed_out:
  case hlclient::auth::AuthenticationErrorCode::cancelled:
    throw std::invalid_argument{context};
  }
  throw std::runtime_error{"Explicit authentication provider failed"};
}

[[nodiscard]] RuntimeConnectPreparation prepare_runtime_connect_request(
    const hlclient::core::CommandLineOptions &options,
    const hlclient::network::NetworkAddress &remote_endpoint) {
  if (options.stop_after == hlclient::core::ConnectionStopPoint::challenge) {
    return {};
  }
  hlclient::goldsrc::ClientConnectionSettings settings;
  settings.display_name = options.player_name;
  settings.model = options.player_model;

  if (options.authentication_provider ==
      hlclient::core::AuthenticationProviderKind::steam) {
    if (!options.steam_api_runtime) {
      throw std::invalid_argument{
          "Steam authentication requires --steam-api-runtime"};
    }
    hlclient::app::SteamAuthenticationProviderConfig config;
    config.runtime_library = path_from_utf8(*options.steam_api_runtime);
    config.trace = &log_steam_authentication_trace;
    return RuntimeConnectPreparation{
        std::nullopt,
        std::nullopt,
        std::make_unique<hlclient::app::SteamAuthenticationProvider>(
            std::move(config)),
        std::move(settings),
        hlclient::goldsrc::steam_legacy_connect_profile(),
    };
  }
  if (!options.authentication_material_file) {
    throw std::invalid_argument{
        "Connect request, response, netchan, and sign-on modes require a local "
        "authentication material file"};
  }

  const auto path = path_from_utf8(*options.authentication_material_file);
  hlclient::app::ExplicitFileAuthenticationProvider provider{path};
  hlclient::auth::AuthenticationRequestContext context;
  context.remote_endpoint = remote_endpoint;
  auto begun = provider.begin(context);
  if (!begun || !begun.operation) {
    throw_authentication_error(
        begun.error.value_or(hlclient::auth::AuthenticationError{
            hlclient::auth::AuthenticationErrorCode::provider_error,
            "Explicit authentication provider could not start",
        }));
  }
  auto update = begun.operation->update();
  if (update.state == hlclient::auth::AuthenticationUpdateState::pending) {
    throw std::runtime_error{
        "Explicit file authentication provider did not complete synchronously"};
  }
  if (update.state != hlclient::auth::AuthenticationUpdateState::succeeded ||
      !update.session) {
    throw_authentication_error(
        update.error.value_or(hlclient::auth::AuthenticationError{
            hlclient::auth::AuthenticationErrorCode::provider_error,
            "Explicit authentication provider returned no session",
        }));
  }

  auto authentication_session = std::move(*update.session);
  auto authentication = authentication_session.take_material();
  if (!authentication) {
    throw std::runtime_error{
        "Explicit authentication session returned no material"};
  }
  auto prepared = hlclient::goldsrc::prepare_connect_request(
      settings, std::move(*authentication));
  if (!prepared) {
    throw std::invalid_argument{
        prepared.error ? prepared.error->context
                       : "Unable to prepare the bounded connect request"};
  }
  return RuntimeConnectPreparation{
      std::move(*prepared.value),
      std::move(authentication_session),
      nullptr,
      std::move(settings),
      {},
  };
}

int run_null_renderer(hlclient::client::IClientSceneSource &scene_source,
                      const std::optional<std::uint64_t> configured_frame_limit,
                      HandshakeSession *const challenge_session,
                      hlclient::app::RuntimeReplaySceneSource
                          *const runtime_replay_source = nullptr) {
  hlclient::renderer::null::NullRenderer renderer;
  renderer.initialize();
  log_renderer_information(renderer);
  hlclient::core::log(LogLevel::info, "Client bootstrap complete");

  const std::uint64_t frame_limit = configured_frame_limit.value_or(1);
  hlclient::input::NullInputSource input_source;
  hlclient::input::InputStateTracker input_tracker;
  auto previous_time = std::chrono::steady_clock::now();
  std::uint64_t rendered_frames = 0;
  const auto continue_running = [&]() {
    if (runtime_replay_source != nullptr) {
      return !runtime_replay_source->terminal() &&
             (!configured_frame_limit || rendered_frames < frame_limit);
    }
    return (challenge_session == nullptr && rendered_frames < frame_limit) ||
           (challenge_session != nullptr && !challenge_session->terminal());
  };
  while (continue_running()) {
    input_source.begin_frame();
    input_tracker.begin_frame();
    auto input_event = hlclient::input::InputEvent::focus_lost();
    while (input_source.poll_event(input_event)) {
      input_tracker.apply_event(input_event);
    }
    input_source.end_frame();
    const auto input_snapshot = input_tracker.publish_snapshot();
    input_tracker.end_frame();
    if (!input_snapshot.focused() || input_snapshot.captured() ||
        input_snapshot.relative_mouse_delta() !=
            hlclient::input::RelativeMouseDelta{} ||
        input_snapshot.wheel_delta() != hlclient::input::MouseWheelDelta{}) {
      throw std::logic_error{
          "Null input source produced non-zero headless input"};
    }
    const auto current_time = std::chrono::steady_clock::now();
    if (challenge_session != nullptr) {
      challenge_session->update(current_time);
    }
    const auto elapsed =
        runtime_replay_source != nullptr &&
                runtime_replay_source->diagnostic_visuals_enabled()
            ? hlclient::client::FrameTime{1.0}
            : current_time - previous_time;
    const auto update = scene_source.update(elapsed);
    if (!update) {
      if (runtime_replay_source != nullptr &&
          runtime_replay_source->terminal()) {
        hlclient::core::log(LogLevel::error,
                            "Runtime replay update failed: " + update.error);
        break;
      }
      throw std::runtime_error{"Scene update failed: " + update.error};
    }
    previous_time = current_time;
    renderer.render(
        hlclient::client::build_render_scene(scene_source.world_state()), {});
    ++rendered_frames;

    if (challenge_session != nullptr && !challenge_session->terminal()) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }

  if (runtime_replay_source != nullptr && !runtime_replay_source->terminal()) {
    runtime_replay_source->stop(hlclient::app::RuntimeReplayStopReason::
                                    application_update_limit_reached);
  }

  const auto null_statistics = renderer.statistics();
  hlclient::core::log(LogLevel::info,
                      "Null renderer completed " +
                          std::to_string(null_statistics.rendered_frames) +
                          " frame(s)");
  if (runtime_replay_source != nullptr &&
      runtime_replay_source->visuals_enabled()) {
    hlclient::core::log(
        LogLevel::info,
        "runtime_replay_null frame_revision=" +
            std::to_string(null_statistics.entity_frame_revision.value_or(0U)) +
            " studio_instances=" +
            std::to_string(null_statistics.studio_entity_instance_count) +
            " visible_entities=" +
            std::to_string(null_statistics.visible_entity_count));
  }
  renderer.shutdown();
  if (!renderer.statistics().shutdown) {
    throw std::logic_error{"Null renderer failed to shut down"};
  }
  if (runtime_replay_source != nullptr) {
    return report_runtime_replay(*runtime_replay_source);
  }
  return challenge_session != nullptr ? challenge_session->report_result() : 0;
}

int run_opengl_renderer(
    hlclient::client::IClientSceneSource &scene_source,
    const std::optional<std::uint64_t> frame_limit,
    HandshakeSession *const challenge_session,
    hlclient::app::RuntimeReplaySceneSource *const runtime_replay_source =
        nullptr,
    const std::optional<std::string> &screenshot_output = std::nullopt) {
  [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
  hlclient::core::log(LogLevel::info, "SDL initialized");

  hlclient::platform::SdlWindow window{hlclient::platform::SdlWindowConfig{
      std::string{hlclient::core::kApplicationName}, 1280, 720}};
  hlclient::core::log(LogLevel::info, "OpenGL context initialized");
  if (!window.vsync_enabled()) {
    hlclient::core::log(LogLevel::warning,
                        "Vertical synchronization is unavailable");
  }

  // Declared after the window so its glad loader is released before the GL
  // context is destroyed.
  hlclient::renderer::opengl::OpenGlRenderer renderer;
  log_renderer_information(renderer);
  hlclient::core::log(LogLevel::info, "Client bootstrap complete");

  auto previous_time = std::chrono::steady_clock::now();
  std::uint64_t rendered_frames = 0;
  std::uint64_t observed_visual_frames = 0U;
  std::uint64_t non_clear_visual_frames = 0U;
  std::uint64_t changed_visual_frames = 0U;
  std::uint64_t last_observed_frame_revision = 0U;
  std::optional<std::uint64_t> last_framebuffer_signature;
  std::optional<std::chrono::steady_clock::time_point> replay_terminal_time;
  bool window_closed = false;
  bool screenshot_saved = false;
  bool running = true;
  while (running) {
    hlclient::platform::PlatformEvent event{hlclient::platform::WindowEvent{}};
    while (window.poll_event(event)) {
      if (const auto *window_event =
              std::get_if<hlclient::platform::WindowEvent>(&event)) {
        if (window_event->type ==
            hlclient::platform::WindowEventType::quit_requested) {
          running = false;
          window_closed = true;
        } else if (window_event->type == hlclient::platform::WindowEventType::
                                             input_capture_recovery_failed) {
          throw std::runtime_error{"SDL input capture recovery failed"};
        } else if (window_event->type == hlclient::platform::WindowEventType::
                                             native_event_limit_exceeded) {
          throw std::runtime_error{"SDL native event hard limit exceeded"};
        }
      }
    }
    if (!running) {
      break;
    }

    const auto current_time = std::chrono::steady_clock::now();
    if (challenge_session != nullptr) {
      challenge_session->update(current_time);
    }
    const auto pixel_extent = window.pixel_extent();
    const hlclient::renderer::RenderExtent extent{pixel_extent.width,
                                                  pixel_extent.height};
    const auto extent_update = scene_source.set_render_extent(extent);
    if (!extent_update) {
      throw std::runtime_error{"Scene extent update failed: " +
                               extent_update.error};
    }
    if (runtime_replay_source == nullptr ||
        !runtime_replay_source->terminal()) {
      const auto update = scene_source.update(current_time - previous_time);
      if (!update) {
        if (runtime_replay_source != nullptr &&
            runtime_replay_source->terminal()) {
          hlclient::core::log(LogLevel::error,
                              "Runtime replay update failed: " + update.error);
          running = false;
          continue;
        }
        throw std::runtime_error{"Scene update failed: " + update.error};
      }
    }
    previous_time = current_time;

    const auto render_scene =
        hlclient::client::build_render_scene(scene_source.world_state());
    renderer.render(render_scene, extent);
    if (runtime_replay_source != nullptr &&
        runtime_replay_source->visuals_enabled() &&
        scene_source.world_state().entity_frame_revision() !=
            last_observed_frame_revision) {
      const auto framebuffer =
          renderer.observe_framebuffer(extent, render_scene.clear_color);
      ++observed_visual_frames;
      if (framebuffer.non_clear_pixel_count != 0U) {
        ++non_clear_visual_frames;
      }
      if (last_framebuffer_signature &&
          *last_framebuffer_signature != framebuffer.color_signature) {
        ++changed_visual_frames;
      }
      last_framebuffer_signature = framebuffer.color_signature;
      last_observed_frame_revision =
          scene_source.world_state().entity_frame_revision();
      std::cout << "runtime_replay_framebuffer visual_frame_revision="
                << last_observed_frame_revision
                << " non_clear_pixels=" << framebuffer.non_clear_pixel_count
                << " signature=" << framebuffer.color_signature << " bounds=";
      if (framebuffer.has_non_clear_bounds) {
        std::cout << framebuffer.minimum_x << ',' << framebuffer.minimum_y
                  << ',' << framebuffer.maximum_x << ','
                  << framebuffer.maximum_y;
      } else {
        std::cout << "unavailable";
      }
      std::cout << '\n' << std::flush;
    }
    if (screenshot_output && !screenshot_saved && runtime_replay_source &&
        runtime_replay_source->state() ==
            hlclient::app::RuntimeReplaySourceState::completed) {
      auto pixels =
          renderer.observe_framebuffer(extent, render_scene.clear_color, true);
      hlclient::platform::save_rgba_framebuffer_png(
          *screenshot_output, extent.width, extent.height, pixels.rgba8);
      screenshot_saved = true;
    }
    window.swap_buffers();

    ++rendered_frames;
    if (runtime_replay_source != nullptr && runtime_replay_source->terminal() &&
        !replay_terminal_time) {
      replay_terminal_time = current_time;
    }
    if (runtime_replay_source != nullptr && replay_terminal_time &&
        current_time - *replay_terminal_time >= std::chrono::seconds{2}) {
      running = false;
    } else if ((challenge_session != nullptr &&
                challenge_session->terminal()) ||
               (frame_limit && rendered_frames >= *frame_limit &&
                challenge_session == nullptr)) {
      running = false;
    }
  }

  const auto &render_statistics = renderer.statistics();
  if (render_statistics.scene_present) {
    hlclient::core::log(
        LogLevel::info,
        "[render] commands=" +
            std::to_string(render_statistics.rendered_command_count) +
            " uploads=" + std::to_string(render_statistics.scene_upload_count) +
            " draws=" + std::to_string(render_statistics.draw_call_count) +
            " triangles=" + std::to_string(render_statistics.triangle_count));
    hlclient::core::log(
        LogLevel::info,
        "[render] visibility-revisions=" +
            std::to_string(render_statistics.visibility_update_count) +
            " brush-draws=" +
            std::to_string(render_statistics.brush_draw_call_count));
  }

  if (challenge_session != nullptr) {
    if (!challenge_session->terminal()) {
      challenge_session->cancel(
          hlclient::goldsrc::ChallengeExchangeClock::now());
    }
    return challenge_session->report_result();
  }
  if (runtime_replay_source != nullptr) {
    if (!runtime_replay_source->terminal()) {
      runtime_replay_source->stop(
          window_closed ? hlclient::app::RuntimeReplayStopReason::explicit_stop
                        : hlclient::app::RuntimeReplayStopReason::
                              application_update_limit_reached);
    }
    const auto entity_statistics = renderer.entity_statistics();
    std::cout << "runtime_replay_opengl context=created"
              << " world_draws=" << renderer.statistics().draw_call_count
              << " world_uploads=" << renderer.statistics().upload_count
              << " screenshot_saved=" << screenshot_saved
              << " entity_frames=" << entity_statistics.entity_frame_count
              << " studio_draws=" << entity_statistics.studio_draw_count
              << " studio_uploads="
              << entity_statistics.studio_asset_upload_count
              << " pose_uploads=" << entity_statistics.pose_ubo_update_count
              << " observed_framebuffers=" << observed_visual_frames
              << " non_clear_framebuffers=" << non_clear_visual_frames
              << " changed_framebuffers=" << changed_visual_frames
              << " gl_errors=0\n"
              << std::flush;
    const auto replay_result = report_runtime_replay(*runtime_replay_source);
    if (replay_result == 0 &&
        (entity_statistics.studio_draw_count == 0U ||
         entity_statistics.studio_asset_upload_count == 0U ||
         non_clear_visual_frames == 0U ||
         (runtime_replay_source->diagnostic_visuals_enabled() &&
          (entity_statistics.studio_asset_upload_count != 1U ||
           observed_visual_frames < 4U ||
           non_clear_visual_frames != observed_visual_frames ||
           changed_visual_frames < 3U)))) {
      hlclient::core::log(LogLevel::error,
                          "Runtime replay OpenGL visual proof did not meet "
                          "draw/upload/framebuffer expectations");
      return 1;
    }
    return replay_result;
  }
  return 0;
}

int run_live_visual_control(
    BootstrapSceneSource &scene_source, HandshakeSession &session,
    const hlclient::core::CommandLineOptions &options) {
  using Clock = std::chrono::steady_clock;
  using InputMode = hlclient::core::LiveInputMode;
  const bool keyboard = options.live_input == InputMode::keyboard_mouse;
  const bool side_check =
      options.live_input == InputMode::scripted_side_check;
  const bool jump_duck_check =
      options.live_input == InputMode::scripted_jump_duck_check;
  const bool speed_check =
      options.live_input == InputMode::scripted_speed_check;
  constexpr auto live_button_mask =
      hlclient::gameplay_input::gameplay_button_mask(
          hlclient::gameplay_input::GameplayButton::jump) |
      hlclient::gameplay_input::gameplay_button_mask(
          hlclient::gameplay_input::GameplayButton::duck);
  constexpr auto live_held_button_mask = live_button_mask |
      hlclient::gameplay_input::gameplay_button_mask(
          hlclient::gameplay_input::GameplayButton::speed);

  [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
  hlclient::platform::SdlWindow window{hlclient::platform::SdlWindowConfig{
      std::string{hlclient::core::kApplicationName}, 1280, 720, false}};
  hlclient::renderer::opengl::OpenGlRenderer renderer;
  hlclient::input::InputStateTracker input_tracker;
  auto bindings =
      hlclient::gameplay_input::GameplayInputBindings::project_default_v1();
  if (!bindings || !bindings.bindings) {
    throw std::runtime_error{"Live visual input bindings are unavailable"};
  }
  hlclient::app::LiveVisualCameraController camera_controller;

  std::optional<std::future<hlclient::app::ReplayLocalAssetsCreateResult>>
      asset_future;
  std::unique_ptr<hlclient::app::RuntimeReplayLocalAssets> local_assets;
  std::optional<hlclient::client::RuntimeObservationSource> last_entity_source;
  std::uint64_t last_projected_publication = 0U;
  std::size_t visual_failures = 0U;
  bool asset_job_started = false;
  bool assets_installed = false;
  bool prediction_collision_attached = false;
  bool renderer_resources_presented = false;
  bool control_activated = false;
  bool window_closed = false;
  bool timed_session_complete = false;
  bool focus_seeded = false;
  std::size_t capture_acquisitions = 0U;
  std::size_t capture_releases = 0U;
  std::size_t focus_loss_count = 0U;
  std::size_t a_held_frames = 0U;
  std::size_t d_held_frames = 0U;
  std::size_t opposing_side_frames = 0U;
  std::size_t nonzero_side_intent_frames = 0U;
  std::size_t jump_input_presses = 0U;
  std::size_t jump_input_releases = 0U;
  std::size_t duck_input_presses = 0U;
  std::size_t duck_input_releases = 0U;
  std::size_t fresh_camera_samples = 0U;
  std::size_t active_fresh_camera_samples = 0U;
  std::size_t moving_fresh_position_changes = 0U;
  std::size_t moving_fresh_unchanged_positions = 0U;
  std::size_t active_presentations = 0U;
  std::size_t prediction_active_frames = 0U;
  std::size_t prediction_fallback_frames = 0U;
  std::size_t prediction_moving_active_frames = 0U;
  std::size_t prediction_moving_presented_changes = 0U;
  std::size_t prediction_moving_simulation_changes = 0U;
  std::optional<hlclient::assets::AssetVector3> last_prediction_presented_origin;
  std::optional<hlclient::assets::AssetVector3> last_prediction_simulated_origin;
  std::size_t loading_presentations = 0U;
  std::size_t moving_presentations = 0U;
  std::size_t moving_presentations_since_sample = 0U;
  std::size_t moving_presentations_since_position_change = 0U;
  std::vector<double> active_frame_intervals_ms;
  std::vector<double> active_update_intervals_ms;
  std::vector<double> fresh_sample_intervals_ms;
  std::vector<double> server_time_increments_ms;
  std::vector<double> moving_frames_per_sample;
  std::vector<double> moving_frames_per_position_change;
  std::vector<double> moving_position_change_intervals_ms;
  std::optional<Clock::time_point> last_active_presentation_at;
  std::optional<Clock::time_point> last_fresh_camera_at;
  std::optional<double> last_fresh_server_time;
  std::optional<Clock::time_point> last_moving_position_change_at;
  std::optional<std::array<float, 3U>> last_moving_origin;
  int last_moving_phase = 0;
  std::size_t server_angle_corrections = 0U;
  std::size_t framebuffer_observations = 0U;
  std::size_t invalid_framebuffer_observations = 0U;
  std::size_t non_clear_framebuffers = 0U;
  std::size_t changed_framebuffers = 0U;
  std::uint64_t successful_presentations = 0U;
  std::size_t preactivation_command_count = 0U;
  std::optional<std::uint64_t> last_framebuffer_signature;
  std::optional<Clock::time_point> asset_preparation_started_at;
  std::optional<Clock::time_point> asset_preparation_completed_at;
  std::optional<Clock::time_point> first_scene_draw_completed_at;
  std::optional<Clock::time_point> first_scene_readback_completed_at;
  std::optional<Clock::time_point> first_scene_presented_at;
  std::optional<Clock::time_point> input_activated_at;
  std::optional<Clock::time_point> last_framebuffer_observed_at;
  std::chrono::nanoseconds longest_update_gap{};
  std::chrono::nanoseconds longest_frame_work{};
  std::chrono::nanoseconds first_render_cpu{};
  std::chrono::nanoseconds first_readback_cpu{};
  std::chrono::nanoseconds first_swap_cpu{};
  std::optional<hlclient::app::LiveVisualCameraSample> first_camera_sample;
  std::optional<hlclient::app::LiveVisualCameraSample> last_camera_sample;
  std::vector<hlclient::app::LiveVisualCameraSample> camera_samples;
  std::uint64_t activation_canonical_revision = 0U;
  std::uint64_t activation_camera_revision = 0U;
  std::uint64_t activation_entity_frame_revision = 0U;
  std::uint64_t activation_static_resource_revision = 0U;
  std::optional<float> minimum_eye_x;
  std::optional<float> maximum_eye_x;
  std::optional<float> minimum_eye_y;
  std::optional<float> maximum_eye_y;
  hlclient::app::LiveVisualViewStatus last_view_status{
      hlclient::app::LiveVisualViewStatus::observation_unavailable};

  const auto observe_camera = [&](const hlclient::app::LiveVisualCameraUpdate &u) {
    last_view_status = u.status;
    if (!u || !u.sample || !u.sample->fresh_server_sample)
      return;
    ++fresh_camera_samples;
    if (control_activated) {
      const auto observed_at = Clock::now();
      ++active_fresh_camera_samples;
      constexpr std::size_t timing_bound = 32'768U;
      if (last_fresh_camera_at &&
          fresh_sample_intervals_ms.size() < timing_bound)
        fresh_sample_intervals_ms.push_back(
            std::chrono::duration<double, std::milli>{
                observed_at - *last_fresh_camera_at}.count());
      last_fresh_camera_at = observed_at;
      if (u.sample->server_time_seconds && last_fresh_server_time &&
          *u.sample->server_time_seconds >= *last_fresh_server_time &&
          server_time_increments_ms.size() < timing_bound)
        server_time_increments_ms.push_back(
            (*u.sample->server_time_seconds - *last_fresh_server_time) * 1000.0);
      last_fresh_server_time = u.sample->server_time_seconds;
      if (moving_presentations_since_sample > 0U &&
          moving_frames_per_sample.size() < timing_bound)
        moving_frames_per_sample.push_back(
            static_cast<double>(moving_presentations_since_sample));
      moving_presentations_since_sample = 0U;
      if (speed_check && input_activated_at) {
        const auto active_ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(observed_at - *input_activated_at);
        const int phase = active_ms >= std::chrono::seconds{2} &&
                                  active_ms < std::chrono::milliseconds{2600}
                              ? 1
                        : active_ms >= std::chrono::milliseconds{3600} &&
                                  active_ms < std::chrono::milliseconds{4600}
                              ? 3 : 0;
        if (phase != 0) {
          if (phase != last_moving_phase) {
            last_moving_origin.reset();
            last_moving_position_change_at.reset();
            moving_presentations_since_position_change = 0U;
          }
          const std::array<float, 3U> origin{
              u.sample->origin.x, u.sample->origin.y, u.sample->origin.z};
          if (!last_moving_origin) {
            last_moving_position_change_at = observed_at;
          } else if (*last_moving_origin == origin) {
            ++moving_fresh_unchanged_positions;
          } else {
            if (moving_presentations_since_position_change > 0U &&
                moving_frames_per_position_change.size() < timing_bound)
              moving_frames_per_position_change.push_back(
                  static_cast<double>(
                      moving_presentations_since_position_change));
            moving_presentations_since_position_change = 0U;
            if (last_moving_position_change_at &&
                moving_position_change_intervals_ms.size() < timing_bound)
              moving_position_change_intervals_ms.push_back(
                  std::chrono::duration<double, std::milli>{
                      observed_at - *last_moving_position_change_at}.count());
            ++moving_fresh_position_changes;
            last_moving_position_change_at = observed_at;
          }
          last_moving_origin = origin;
        }
        last_moving_phase = phase;
      }
    }
    if (u.sample->server_angle_correction_applied)
      ++server_angle_corrections;
    if (!first_camera_sample)
      first_camera_sample = *u.sample;
    last_camera_sample = *u.sample;
    if (camera_samples.size() < 512U)
      camera_samples.push_back(*u.sample);
    minimum_eye_x = minimum_eye_x
                        ? std::min(*minimum_eye_x, u.sample->eye_position.x)
                        : u.sample->eye_position.x;
    maximum_eye_x = maximum_eye_x
                        ? std::max(*maximum_eye_x, u.sample->eye_position.x)
                        : u.sample->eye_position.x;
    minimum_eye_y = minimum_eye_y
                        ? std::min(*minimum_eye_y, u.sample->eye_position.y)
                        : u.sample->eye_position.y;
    maximum_eye_y = maximum_eye_y
                        ? std::max(*maximum_eye_y, u.sample->eye_position.y)
                        : u.sample->eye_position.y;
    std::cout << "live_visual_camera_sample generation=" << u.sample->generation
              << " publication_revision=" << u.sample->publication_revision
              << " source_sequence="
              << u.sample->source.source_transport_sequence << " server_time=";
    if (u.sample->server_time_seconds)
      std::cout << *u.sample->server_time_seconds;
    else
      std::cout << "unavailable";
    std::cout << " origin=" << u.sample->origin.x << ',' << u.sample->origin.y
              << ',' << u.sample->origin.z << " view_offset="
              << u.sample->view_offset.x << ',' << u.sample->view_offset.y
              << ',' << u.sample->view_offset.z << " eye="
              << u.sample->eye_position.x << ',' << u.sample->eye_position.y
              << ',' << u.sample->eye_position.z << " local_angles="
              << u.sample->local_yaw_degrees << ','
              << u.sample->local_pitch_degrees
              << " server_angle_correction="
              << u.sample->server_angle_correction_applied
              << " freshness=observed_in_record\n";
  };

  const auto session_started = Clock::now();
  auto previous_time = session_started;
  auto previous_session_update = session_started;
  bool running = true;
  while (running) {
    input_tracker.begin_frame();
    if (!focus_seeded) {
      input_tracker.apply_event(
          keyboard && window.focus_state() ==
                          hlclient::input::InputFocusState::focused
              ? hlclient::input::InputEvent::focus_gained()
              : hlclient::input::InputEvent::focus_lost());
      focus_seeded = true;
    }
    hlclient::platform::PlatformEvent event{hlclient::platform::WindowEvent{}};
    while (window.poll_event(event)) {
      if (const auto *window_event =
              std::get_if<hlclient::platform::WindowEvent>(&event)) {
        if (window_event->type ==
            hlclient::platform::WindowEventType::quit_requested) {
          running = false;
          window_closed = true;
        } else if (window_event->type == hlclient::platform::WindowEventType::
                                             input_capture_recovery_failed) {
          throw std::runtime_error{"SDL input capture recovery failed"};
        } else if (window_event->type == hlclient::platform::WindowEventType::
                                             native_event_limit_exceeded) {
          throw std::runtime_error{"SDL native event hard limit exceeded"};
        }
      } else if (keyboard) {
        const auto &input_event = std::get<hlclient::input::InputEvent>(event);
        if (input_event.type() == hlclient::input::InputEventType::focus_lost)
          ++focus_loss_count;
        input_tracker.apply_event(input_event);
      }
    }
    if (keyboard && window.focus_state() ==
                        hlclient::input::InputFocusState::unfocused) {
      const auto released = window.request_relative_mouse_capture(false);
      if (!released)
        throw std::runtime_error{"Unable to release mouse after focus loss"};
      if (released.status ==
          hlclient::platform::RelativeMouseCaptureStatus::released)
        ++capture_releases;
    }
    const auto snapshot = input_tracker.publish_snapshot();
    input_tracker.end_frame();
    if (!running)
      break;

    const auto now = Clock::now();
    if (keyboard && options.live_session_seconds &&
        now - session_started >=
            std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
                *options.live_session_seconds)}) {
      timed_session_complete = true;
      break;
    }
    const auto elapsed = now - previous_time;
    const double input_seconds = std::clamp(
        std::chrono::duration<double>{elapsed}.count(), 0.001, 0.25);
    auto built_intent =
        hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
            snapshot, *bindings.bindings,
            hlclient::gameplay_input::MouseLookConfig{}, input_seconds);
    if (!built_intent || !built_intent.intent)
      throw std::runtime_error{"Live visual gameplay intent build failed"};
    const auto &intent = *built_intent.intent;
    if (keyboard && control_activated) {
      const auto jump_bit = hlclient::gameplay_input::gameplay_button_mask(
          hlclient::gameplay_input::GameplayButton::jump);
      const auto duck_bit = hlclient::gameplay_input::gameplay_button_mask(
          hlclient::gameplay_input::GameplayButton::duck);
      jump_input_presses += (intent.pressed_buttons() & jump_bit) != 0U;
      jump_input_releases += (intent.released_buttons() & jump_bit) != 0U;
      duck_input_presses += (intent.pressed_buttons() & duck_bit) != 0U;
      duck_input_releases += (intent.released_buttons() & duck_bit) != 0U;
      const bool a_held = snapshot.key_held(hlclient::input::PhysicalKey::a);
      const bool d_held = snapshot.key_held(hlclient::input::PhysicalKey::d);
      a_held_frames += a_held;
      d_held_frames += d_held;
      opposing_side_frames += a_held && d_held;
      nonzero_side_intent_frames += intent.side_axis() != 0.0F;
    }

    if (keyboard && assets_installed &&
        (session.live_visual_input_ready() || control_activated) &&
        intent.capture_mouse_requested()) {
      const auto captured = window.request_relative_mouse_capture(true);
      if (!captured)
        throw std::runtime_error{"Unable to acquire relative mouse capture"};
      if (captured.status ==
          hlclient::platform::RelativeMouseCaptureStatus::acquired)
        ++capture_acquisitions;
    }
    if (keyboard && intent.release_mouse_requested()) {
      const auto released = window.request_relative_mouse_capture(false);
      if (!released)
        throw std::runtime_error{"Unable to release relative mouse capture"};
      if (released.status ==
          hlclient::platform::RelativeMouseCaptureStatus::released)
        ++capture_releases;
    }

    if (assets_installed && !camera_controller.apply_local_look(intent))
      throw std::runtime_error{"Live visual local look update failed"};

    const auto submit_keyboard_input = [&](const Clock::time_point timestamp) {
      if (!keyboard || !assets_installed)
        return false;
      return session.submit_live_visual_input(
          hlclient::goldsrc::LiveVisualControlInput{
              1U,
              intent.input_sequence(),
              intent.focused() ? intent.forward_axis() : 0.0F,
              intent.focused() ? intent.side_axis() : 0.0F,
              camera_controller.yaw_degrees(),
              camera_controller.pitch_degrees(),
              intent.focused(),
              intent.captured(),
              snapshot.key_held(hlclient::input::PhysicalKey::a),
              snapshot.key_held(hlclient::input::PhysicalKey::d),
              intent.focused() ? intent.held_buttons() & live_held_button_mask : 0U,
              intent.focused() ? intent.pressed_buttons() & live_button_mask
                               : 0U},
          timestamp);
    };
    if (control_activated && keyboard && !submit_keyboard_input(now))
      throw std::runtime_error{"Live keyboard input revision was rejected"};

    if (control_activated && active_update_intervals_ms.size() < 32'768U)
      active_update_intervals_ms.push_back(
          std::chrono::duration<double, std::milli>{
              now - previous_session_update}.count());
    longest_update_gap = std::max(
        longest_update_gap,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - previous_session_update));
    session.update(now);
    previous_session_update = now;
    if (!control_activated) {
      const auto before_activation = session.live_usercmd_snapshot();
      preactivation_command_count = std::max(
          preactivation_command_count,
          before_activation ? before_activation->generated_command_count : 0U);
    }

    if (!asset_job_started && session.live_resource_list() != nullptr &&
        session.live_server_info() != nullptr) {
      auto resources = *session.live_resource_list();
      auto server_info = *session.live_server_info();
      const auto basedir = path_from_utf8(*options.base_directory);
      const auto game = options.game_directory;
      asset_future.emplace(std::async(
          std::launch::async,
          [resources = std::move(resources),
           server_info = std::move(server_info), basedir, game]() {
            return hlclient::app::RuntimeReplayLocalAssets::create(
                resources, server_info, basedir, game,
                hlclient::app::ReplayLocalCameraPolicy::
                    external_live_receiving_client);
          }));
      asset_job_started = true;
      asset_preparation_started_at = Clock::now();
    }
    if (asset_future && !local_assets &&
        asset_future->wait_for(std::chrono::milliseconds{0}) ==
            std::future_status::ready) {
      auto created = asset_future->get();
      if (!created.projection) {
        throw std::runtime_error{
            created.error ? created.error->context
                          : "Live local asset preparation returned no projection"};
      }
      local_assets = std::move(created.projection);
      asset_preparation_completed_at = Clock::now();
    }

    const auto &observation =
        scene_source.mutable_world_state().runtime_observation();
    if (local_assets && observation) {
      if (!assets_installed) {
        const auto projected = local_assets->reset_generation(
            *observation, scene_source.mutable_world_state());
        if (!projected) {
          ++visual_failures;
          throw std::runtime_error{
              projected.error ? projected.error->context
                              : "Live visual generation reset failed"};
        }
        assets_installed = true;
        if (options.reference_prediction &&
            local_assets->collision_world_package()) {
          prediction_collision_attached =
              session.attach_reference_prediction_collision(
                  local_assets->collision_world_package());
        }
        last_projected_publication = observation->publication_revision;
        if (observation->entity_metadata.source)
          last_entity_source = *observation->entity_metadata.source;
      } else if (observation->publication_revision !=
                 last_projected_publication) {
        if (observation->entity_metadata.freshness ==
                hlclient::client::RuntimeObservationFreshness::
                    observed_in_record &&
            observation->entity_metadata.source &&
            (!last_entity_source ||
             *last_entity_source != *observation->entity_metadata.source)) {
          const auto projected = local_assets->project_entities(
              *observation, scene_source.mutable_world_state());
          if (!projected) {
            ++visual_failures;
            throw std::runtime_error{
                projected.error ? projected.error->context
                                : "Live entity projection failed"};
          }
          last_entity_source = *observation->entity_metadata.source;
        } else {
          local_assets->acknowledge_unchanged();
        }
        last_projected_publication = observation->publication_revision;
      }
    }

    if (assets_installed) {
      const auto prediction = options.reference_prediction
          ? session.live_reference_prediction_snapshot(now)
          : hlclient::goldsrc::LiveReferencePredictionSnapshot{};
      std::optional<hlclient::app::LiveVisualPredictedView> predicted_view;
      if (prediction.state ==
              hlclient::goldsrc::LiveReferencePredictionState::active &&
          prediction.presented_origin && prediction.presented_view_offset) {
        predicted_view = hlclient::app::LiveVisualPredictedView{
            *prediction.presented_origin, *prediction.presented_view_offset};
      }
      if (options.reference_prediction && control_activated) {
        if (predicted_view) {
          ++prediction_active_frames;
          bool moving_window = keyboard &&
              (intent.forward_axis() != 0.0F || intent.side_axis() != 0.0F);
          if (speed_check && input_activated_at) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *input_activated_at);
            moving_window =
                (age >= std::chrono::seconds{2} &&
                 age < std::chrono::milliseconds{2600}) ||
                (age >= std::chrono::milliseconds{3600} &&
                 age < std::chrono::milliseconds{4600});
          }
          if (moving_window) {
            const auto changed = [](const auto& left, const auto& right) {
              return left.x != right.x || left.y != right.y ||
                     left.z != right.z;
            };
            ++prediction_moving_active_frames;
            if (last_prediction_presented_origin &&
                changed(*last_prediction_presented_origin,
                        *prediction.presented_origin))
              ++prediction_moving_presented_changes;
            if (prediction.predicted_origin &&
                last_prediction_simulated_origin &&
                changed(*last_prediction_simulated_origin,
                        *prediction.predicted_origin))
              ++prediction_moving_simulation_changes;
          }
          last_prediction_presented_origin = prediction.presented_origin;
          last_prediction_simulated_origin = prediction.predicted_origin;
        } else {
          ++prediction_fallback_frames;
        }
      }
      observe_camera(camera_controller.update(
          scene_source.mutable_world_state().runtime_observation().get(),
          intent, scene_source.mutable_world_state(), false, predicted_view));
    }

    const auto extent_pixels = window.pixel_extent();
    const hlclient::renderer::RenderExtent extent{extent_pixels.width,
                                                  extent_pixels.height};
    const auto extent_update = scene_source.set_render_extent(extent);
    if (!extent_update)
      throw std::runtime_error{"Live visual extent publication failed"};
    const auto scene_update = scene_source.update(elapsed);
    if (!scene_update)
      throw std::runtime_error{"Live visual scene update failed"};
    const auto render_scene = hlclient::client::build_render_scene(
        scene_source.world_state());
    const auto render_started_at = Clock::now();
    renderer.render(render_scene, extent);
    const auto render_completed_at = Clock::now();
    const bool first_scene_draw = !first_scene_draw_completed_at &&
                                  renderer.statistics().draw_call_count > 0U;
    if (first_scene_draw)
      first_render_cpu = std::chrono::duration_cast<std::chrono::nanoseconds>(
          render_completed_at - render_started_at);
    if (!first_scene_draw_completed_at &&
        renderer.statistics().draw_call_count > 0U) {
      first_scene_draw_completed_at = render_completed_at;
    }
    constexpr std::size_t maximum_framebuffer_observations = 8U;
    constexpr auto framebuffer_observation_interval =
        std::chrono::milliseconds{750};
    const bool framebuffer_observation_due =
        !last_framebuffer_observed_at ||
        render_completed_at - *last_framebuffer_observed_at >=
            framebuffer_observation_interval;
    if (assets_installed && camera_controller.camera_revision() != 0U &&
        framebuffer_observations < maximum_framebuffer_observations &&
        framebuffer_observation_due) {
      const auto readback_started_at = Clock::now();
      const auto framebuffer =
          renderer.observe_framebuffer(extent, render_scene.clear_color);
      const auto readback_completed_at = Clock::now();
      if (first_readback_cpu == std::chrono::nanoseconds{}) {
        first_readback_cpu =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                readback_completed_at - readback_started_at);
      }
      if (!first_scene_readback_completed_at)
        first_scene_readback_completed_at = readback_completed_at;
      last_framebuffer_observed_at = readback_completed_at;
      ++framebuffer_observations;
      if (!framebuffer) {
        ++invalid_framebuffer_observations;
      } else {
        if (framebuffer.non_clear_pixel_count != 0U)
          ++non_clear_framebuffers;
        if (last_framebuffer_signature &&
            *last_framebuffer_signature != framebuffer.color_signature)
          ++changed_framebuffers;
        last_framebuffer_signature = framebuffer.color_signature;
      }
      std::cout << "live_visual_framebuffer status="
                << hlclient::renderer::opengl::to_string(framebuffer.status)
                << " source=default_back_buffer"
                << " read_framebuffer=" << framebuffer.read_framebuffer
                << " read_buffer=" << framebuffer.read_buffer
                << " sampled_pixels=" << framebuffer.sampled_pixel_count
                << " non_clear_pixels=" << framebuffer.non_clear_pixel_count
                << " signature=" << framebuffer.color_signature
                << " canonical_revision="
                << scene_source.world_state().runtime_publication_revision()
                << " camera_revision=" << camera_controller.camera_revision()
                << " entity_frame_revision="
                << scene_source.world_state().entity_frame_revision()
                << " static_resource_revision="
                << scene_source.world_state().world_revision() << '\n'
                << std::flush;
    }
    const auto swap_started_at = Clock::now();
    window.swap_buffers();
    const auto swap_completed_at = Clock::now();
    ++successful_presentations;
    if (control_activated) {
      ++active_presentations;
      if (last_active_presentation_at &&
          active_frame_intervals_ms.size() < 32'768U)
        active_frame_intervals_ms.push_back(
            std::chrono::duration<double, std::milli>{
                swap_completed_at - *last_active_presentation_at}.count());
      last_active_presentation_at = swap_completed_at;
      if (speed_check && input_activated_at) {
        const auto active_ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(swap_completed_at - *input_activated_at);
        if ((active_ms >= std::chrono::seconds{2} &&
             active_ms < std::chrono::milliseconds{2600}) ||
            (active_ms >= std::chrono::milliseconds{3600} &&
             active_ms < std::chrono::milliseconds{4600})) {
          ++moving_presentations;
          ++moving_presentations_since_sample;
          ++moving_presentations_since_position_change;
        }
      }
    } else {
      ++loading_presentations;
    }
    if (first_scene_draw && first_swap_cpu == std::chrono::nanoseconds{}) {
      first_swap_cpu = std::chrono::duration_cast<std::chrono::nanoseconds>(
          swap_completed_at - swap_started_at);
    }
    const auto &presented_world = renderer.statistics();
    renderer_resources_presented = renderer_resources_presented ||
        hlclient::app::live_visual_render_ready_for_input(
            presented_world.draw_call_count, presented_world.upload_count,
            1U);
    if (renderer_resources_presented && !first_scene_presented_at)
      first_scene_presented_at = swap_completed_at;

    if (!control_activated && assets_installed &&
        renderer_resources_presented && session.live_visual_input_ready() &&
        last_view_status == hlclient::app::LiveVisualViewStatus::ready) {
      // This timestamp is deliberately sampled after initial upload, draw,
      // readback and presentation. None of that loading time is scheduler
      // backlog, and the first 20 ms interval begins here.
      const auto activation_now = Clock::now();
      const bool input_ready =
          !keyboard || submit_keyboard_input(activation_now);
      if (!input_ready ||
          !session.activate_live_visual_control(activation_now)) {
        throw std::runtime_error{
            "Live visual production handoff rejected its selected input"};
      }
      control_activated = true;
      input_activated_at = activation_now;
      activation_canonical_revision =
          scene_source.world_state().runtime_publication_revision();
      activation_camera_revision = camera_controller.camera_revision();
      activation_entity_frame_revision =
          scene_source.world_state().entity_frame_revision();
      activation_static_resource_revision =
          scene_source.world_state().world_revision();
    }

    const auto frame_completed_at = Clock::now();
    longest_frame_work = std::max(
        longest_frame_work,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            frame_completed_at - now));
    previous_time = frame_completed_at;

    if (session.terminal())
      running = false;
    else
      std::this_thread::yield();
  }

  const auto released = window.request_relative_mouse_capture(false);
  if (!released)
    throw std::runtime_error{"Final live visual mouse release failed"};
  if (released.status ==
      hlclient::platform::RelativeMouseCaptureStatus::released)
    ++capture_releases;
  if (!session.terminal())
    session.cancel(Clock::now());

  const auto usercmd = session.live_runtime_result() &&
                               session.live_runtime_result()->usercmd_check
                           ? session.live_runtime_result()->usercmd_check
                           : session.live_usercmd_snapshot();
  const auto &world_stats = renderer.statistics();
  const auto entity_stats = renderer.entity_statistics();
  const auto local_summary = local_assets
                                 ? std::optional{local_assets->summary()}
                                 : std::nullopt;
  const float eye_span_x = minimum_eye_x && maximum_eye_x
                               ? *maximum_eye_x - *minimum_eye_x
                               : 0.0F;
  const float eye_span_y = minimum_eye_y && maximum_eye_y
                               ? *maximum_eye_y - *minimum_eye_y
                               : 0.0F;
  const bool camera_translation =
      std::hypot(eye_span_x, eye_span_y) >= 0.125F;
  const bool visual_success =
      assets_installed && local_summary &&
      local_summary->imported_studio + local_summary->imported_sprites > 0U &&
      local_summary->resolved_instances > 0U &&
      local_summary->rendered_instances > 0U && world_stats.upload_count > 0U &&
      world_stats.draw_call_count > 0U &&
      entity_stats.studio_asset_upload_count +
              entity_stats.sprite_asset_upload_count >
          0U &&
      entity_stats.studio_draw_count + entity_stats.sprite_draw_count > 0U &&
      non_clear_framebuffers > 0U && invalid_framebuffer_observations == 0U &&
      successful_presentations > 0U && visual_failures == 0U;
  const bool network_success =
      usercmd && usercmd->generated_command_count > 0U &&
      usercmd->transmitted_packet_count > 0U &&
      !usercmd->server_samples.empty();
  const bool scripted_success =
      !keyboard && session.live_runtime_result() && usercmd &&
      usercmd->movement_verified &&
      (speed_check
           ? usercmd->speed_outcome ==
                 hlclient::goldsrc::LiveUserCmdMotionOutcome::verified
       : jump_duck_check
           ? usercmd->jump_duck.outcome ==
                 hlclient::goldsrc::LiveJumpDuckOutcome::verified
           : camera_translation);
  const bool success = visual_success && network_success &&
                       (options.reference_prediction
                            ? keyboard ? (window_closed || timed_session_complete)
                                       : session.live_runtime_result().has_value()
                            : keyboard ? (window_closed || timed_session_complete)
                                       : scripted_success);
  const auto final_prediction =
      session.live_reference_prediction_snapshot(Clock::now());
  const bool prediction_success = options.reference_prediction && success &&
      prediction_collision_attached && prediction_active_frames >= 30U &&
      prediction_moving_active_frames >= 10U &&
      prediction_moving_presented_changes >
          prediction_moving_simulation_changes &&
      final_prediction.local_steps > 0U &&
      final_prediction.accepted_corrections > 0U &&
      final_prediction.replayed_commands > 0U &&
      final_prediction.state ==
          hlclient::goldsrc::LiveReferencePredictionState::active;
  const std::string_view prediction_result = prediction_success
      ? "live_local_prediction_and_reconciliation_verified"
      : !usercmd || usercmd->server_samples.empty()
          ? "live_prediction_integrated_live_pending"
      : prediction_active_frames == 0U
          ? "prediction_seed_or_anchor_contract_partial"
          : "live_prediction_active_accuracy_or_coverage_limited";
  const auto primary_error = [&]() -> std::string_view {
    if (options.reference_prediction && !prediction_success)
      return usercmd && usercmd->server_samples.empty()
          ? "no_fresh_postactivation_clientdata"
          : final_prediction.reason;
    if (success)
      return "none";
    if (session.live_runtime_error())
      return hlclient::goldsrc::to_string(session.live_runtime_error()->code);
    if (!asset_job_started)
      return "live_resource_context_unavailable";
    if (!assets_installed)
      return "live_asset_preparation_incomplete";
    if (!visual_success)
      return "live_visual_evidence_incomplete";
    if (!network_success)
      return "live_network_evidence_incomplete";
    if (jump_duck_check && usercmd)
      return hlclient::goldsrc::to_string(usercmd->jump_duck.outcome);
    if (speed_check && usercmd)
      return hlclient::goldsrc::to_string(usercmd->speed_outcome);
    return "live_scripted_motion_incomplete";
  }();

  const auto print_vector = [](const auto &value) {
    return std::to_string(value.x) + "," + std::to_string(value.y) + "," +
           std::to_string(value.z);
  };
  const auto relative_milliseconds = [&](const auto &value) {
    if (!value)
      return std::string{"unavailable"};
    return std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            *value - session_started)
            .count());
  };
  const auto duration_milliseconds = [](const auto value) {
    return std::chrono::duration<double, std::milli>{value}.count();
  };
  std::cout << "live_visual_timing"
            << " resource_preparation_start_ms="
            << relative_milliseconds(asset_preparation_started_at)
            << " resource_preparation_end_ms="
            << relative_milliseconds(asset_preparation_completed_at)
            << " first_scene_draw_end_ms="
            << relative_milliseconds(first_scene_draw_completed_at)
            << " first_scene_readback_end_ms="
            << relative_milliseconds(first_scene_readback_completed_at)
            << " first_scene_present_end_ms="
            << relative_milliseconds(first_scene_presented_at)
            << " input_activation_ms="
            << relative_milliseconds(input_activated_at)
            << " preactivation_commands=" << preactivation_command_count
            << " first_render_cpu_ms="
            << duration_milliseconds(first_render_cpu)
            << " first_readback_cpu_ms="
            << duration_milliseconds(first_readback_cpu)
            << " first_swap_cpu_ms="
            << duration_milliseconds(first_swap_cpu)
            << " longest_frame_cpu_ms="
            << duration_milliseconds(longest_frame_work)
            << " longest_update_gap_ms="
            << duration_milliseconds(longest_update_gap)
            << " activation_canonical_revision="
            << activation_canonical_revision
            << " activation_camera_revision=" << activation_camera_revision
            << " activation_entity_frame_revision="
            << activation_entity_frame_revision
            << " activation_static_resource_revision="
            << activation_static_resource_revision << '\n';
  if (speed_check) {
    const auto timing_stat = [](std::vector<double> values,
                                const std::string_view which) {
      if (values.empty()) return std::string{"unavailable"};
      std::sort(values.begin(), values.end());
      if (which == "max") return std::to_string(values.back());
      if (which == "p95") {
        const auto index =
            static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1U;
        return std::to_string(values[index]);
      }
      return std::to_string((values[(values.size() - 1U) / 2U] +
                             values[values.size() / 2U]) / 2.0);
    };
    std::cout << "live_speed_timing active_window_ms="
              << (input_activated_at
                  ? std::to_string(std::chrono::duration<double, std::milli>{
                        Clock::now() - *input_activated_at}.count())
                  : std::string{"unavailable"})
              << " loading_presentations=" << loading_presentations
              << " active_presentations=" << active_presentations
              << " moving_presentations=" << moving_presentations
              << " frame_interval_median_ms="
              << timing_stat(active_frame_intervals_ms, "median")
              << " frame_interval_p95_ms="
              << timing_stat(active_frame_intervals_ms, "p95")
              << " frame_interval_max_ms="
              << timing_stat(active_frame_intervals_ms, "max")
              << " local_dispatch_interval_median_ms="
              << timing_stat(active_update_intervals_ms, "median")
              << " local_dispatch_interval_p95_ms="
              << timing_stat(active_update_intervals_ms, "p95")
              << " fresh_clientdata_samples=" << active_fresh_camera_samples
              << " fresh_moving_server_samples="
              << (usercmd ? usercmd->fresh_samples_by_phase[1] +
                                 usercmd->fresh_samples_by_phase[3]
                          : 0U)
              << " moving_fresh_position_changes="
              << moving_fresh_position_changes
              << " moving_fresh_unchanged_positions="
              << moving_fresh_unchanged_positions
              << " moving_position_change_interval_median_ms="
              << timing_stat(moving_position_change_intervals_ms, "median")
              << " moving_position_change_interval_p95_ms="
              << timing_stat(moving_position_change_intervals_ms, "p95")
              << " fresh_clientdata_interval_median_ms="
              << timing_stat(fresh_sample_intervals_ms, "median")
              << " fresh_clientdata_interval_p95_ms="
              << timing_stat(fresh_sample_intervals_ms, "p95")
              << " server_time_increment_median_ms="
              << timing_stat(server_time_increments_ms, "median")
              << " moving_frames_per_fresh_sample_median="
              << timing_stat(moving_frames_per_sample, "median")
              << " moving_frames_per_fresh_sample_max="
              << timing_stat(moving_frames_per_sample, "max")
              << " moving_frames_per_position_change_median="
              << timing_stat(moving_frames_per_position_change, "median")
              << " moving_frames_per_position_change_max="
              << timing_stat(moving_frames_per_position_change, "max")
              << " command_sample_interval_ms=20"
              << " view_policy=latest_receiving_client_sample\n";
  }
  std::cout << "live_visual_scheduler initialized="
            << (usercmd && usercmd->scheduler_initialized)
            << " active=" << (usercmd && usercmd->scheduler_active)
            << " activation_time_ns="
            << (usercmd ? usercmd->scheduler_activation_time_nanoseconds : 0)
            << " last_update_time_ns="
            << (usercmd ? usercmd->scheduler_last_update_time_nanoseconds : 0)
            << " next_sample_time_ns="
            << (usercmd ? usercmd->scheduler_next_sample_time_nanoseconds : 0)
            << " command_interval_ms="
            << (usercmd ? usercmd->command_interval.count() : 0)
            << " last_due_commands="
            << (usercmd ? usercmd->scheduler_last_due_command_count : 0U)
            << " catchup_cap="
            << (usercmd ? usercmd->scheduler_maximum_commands_per_update : 0U)
            << " underlying_error="
            << (session.live_runtime_error() &&
                        session.live_runtime_error()->usercmd_scheduler_code
                    ? hlclient::goldsrc::to_string(*session.live_runtime_error()
                                                       ->usercmd_scheduler_code)
                    : std::string_view{"none"})
            << '\n';
  if (usercmd) {
    const auto phase_name = [side_check, jump_duck_check, speed_check](
        const hlclient::goldsrc::LiveUserCmdInputPhase phase) {
      if (jump_duck_check) {
        switch (phase) {
        case hlclient::goldsrc::LiveUserCmdInputPhase::neutral_before:
          return std::string_view{"neutral_settling"};
        case hlclient::goldsrc::LiveUserCmdInputPhase::forward:
          return std::string_view{"jump_hold"};
        case hlclient::goldsrc::LiveUserCmdInputPhase::neutral_middle:
          return std::string_view{"jump_release"};
        case hlclient::goldsrc::LiveUserCmdInputPhase::backward:
          return std::string_view{"duck_hold"};
        case hlclient::goldsrc::LiveUserCmdInputPhase::neutral_tail:
          return std::string_view{"duck_release_and_neutral_tail"};
        }
      }
      if (side_check &&
          phase == hlclient::goldsrc::LiveUserCmdInputPhase::forward)
        return std::string_view{"left"};
      if (side_check &&
          phase == hlclient::goldsrc::LiveUserCmdInputPhase::backward)
        return std::string_view{"right"};
      if (speed_check &&
          phase == hlclient::goldsrc::LiveUserCmdInputPhase::forward)
        return std::string_view{"normal_forward"};
      if (speed_check &&
          phase == hlclient::goldsrc::LiveUserCmdInputPhase::backward)
        return std::string_view{"shift_forward"};
      return hlclient::goldsrc::to_string(phase);
    };
    for (std::size_t index = 0U;
         index < hlclient::goldsrc::kLiveUserCmdPhaseCount; ++index) {
      const auto phase = static_cast<hlclient::goldsrc::LiveUserCmdInputPhase>(
          index);
      std::cout << "live_visual_phase phase="
                << phase_name(phase)
                << " generated=" << usercmd->generated_by_phase[index]
                << " sent=" << usercmd->sent_by_phase[index]
                << " fresh_server_samples="
                << usercmd->fresh_samples_by_phase[index];
      if (speed_check) {
        const auto value = [](const auto &v) {
          return v ? std::to_string(*v) : std::string{"unavailable"};
        };
        std::cout << " requested_forward="
                  << value(usercmd->requested_forward_by_phase[index])
                  << " speed_multiplier="
                  << value(usercmd->speed_multiplier_by_phase[index])
                  << " encoded_forward="
                  << value(usercmd->encoded_forward_by_phase[index])
                  << " encoded_side="
                  << value(usercmd->encoded_side_by_phase[index]);
      }
      std::cout << '\n';
    }
    for (std::size_t phase_index = 0U;
         phase_index < hlclient::goldsrc::kLiveUserCmdPhaseCount;
         ++phase_index) {
      const auto phase = static_cast<hlclient::goldsrc::LiveUserCmdInputPhase>(
          phase_index);
      const hlclient::goldsrc::LiveUserCmdServerSample *first = nullptr;
      const hlclient::goldsrc::LiveUserCmdServerSample *last = nullptr;
      for (const auto &sample : usercmd->server_samples) {
        if (sample.phase != phase)
          continue;
        if (!first)
          first = &sample;
        last = &sample;
      }
      if (!first)
        continue;
      const auto print_sample = [&](const std::string_view ordinal,
                                    const auto &sample) {
        const auto camera = std::find_if(
            camera_samples.begin(), camera_samples.end(),
            [&](const auto &candidate) {
              return candidate.source == sample.source;
            });
        std::cout << "live_visual_server_sample phase="
                  << phase_name(phase)
                  << " ordinal=" << ordinal
                  << " generation=" << sample.generation
                  << " publication_revision=" << sample.publication_revision
                  << " source_sequence="
                  << sample.source.source_transport_sequence
                  << " server_time=";
        if (sample.server_time_seconds)
          std::cout << *sample.server_time_seconds;
        else
          std::cout << "unavailable";
        std::cout << " origin=";
        if (sample.origin.complete())
          std::cout << *sample.origin.x << ',' << *sample.origin.y << ','
                    << *sample.origin.z;
        else
          std::cout << "unavailable";
        std::cout << " velocity=";
        if (sample.velocity.complete())
          std::cout << *sample.velocity.x << ',' << *sample.velocity.y << ','
                    << *sample.velocity.z;
        else
          std::cout << "unavailable";
        std::cout << " view_offset=";
        if (camera != camera_samples.end())
          std::cout << print_vector(camera->view_offset);
        else
          std::cout << "unavailable";
        std::cout << " eye=";
        if (camera != camera_samples.end())
          std::cout << print_vector(camera->eye_position);
        else
          std::cout << "unavailable";
        std::cout << '\n';
      };
      print_sample("first", *first);
      if (last != first)
        print_sample("last", *last);
    }
  }
  if (jump_duck_check || keyboard) {
    const auto evaluation = usercmd
        ? usercmd->jump_duck
        : hlclient::goldsrc::LiveJumpDuckEvaluation{};
    const bool evaluation_available = jump_duck_check && usercmd &&
        evaluation.outcome !=
            hlclient::goldsrc::LiveJumpDuckOutcome::not_evaluated;
    const auto observed = [evaluation_available](const bool value) {
      return evaluation_available ? (value ? "true" : "false")
                                  : "unavailable";
    };
    const auto command_count = [usercmd](const std::size_t count) {
      return usercmd ? std::to_string(count) : std::string{"unavailable"};
    };
    const auto sent_sequence_range = [](const std::optional<std::uint32_t> first,
                                        const std::optional<std::uint32_t> last) {
      return first && last ? std::to_string(*first) + "-" +
                                 std::to_string(*last)
                           : std::string{"unavailable"};
    };
    std::cout << "live_jump_duck input="
              << (keyboard ? "keyboard-mouse" : "scripted-jump-duck-check")
              << " result=" << hlclient::goldsrc::to_string(evaluation.outcome)
              << " jump_observed=" << observed(evaluation.jump_observed)
              << " descent_observed=" << observed(evaluation.descent_observed)
              << " duck_observed=" << observed(evaluation.duck_observed)
              << " release_response_observed="
              << observed(evaluation.release_response_observed)
              << " jump_input_presses=" << jump_input_presses
              << " jump_input_releases=" << jump_input_releases
              << " duck_input_presses=" << duck_input_presses
              << " duck_input_releases=" << duck_input_releases
              << " jump_command_presses="
              << command_count(usercmd ? usercmd->jump_command_press_count : 0U)
              << " jump_command_releases="
              << command_count(usercmd ? usercmd->jump_command_release_count : 0U)
              << " duck_command_presses="
              << command_count(usercmd ? usercmd->duck_command_press_count : 0U)
              << " duck_command_releases="
              << command_count(usercmd ? usercmd->duck_command_release_count : 0U)
              << " jump_generated="
              << command_count(usercmd ? usercmd->jump_generated_count : 0U)
              << " duck_generated="
              << command_count(usercmd ? usercmd->duck_generated_count : 0U)
              << " jump_new_submitted="
              << command_count(usercmd ? usercmd->jump_new_submission_count : 0U)
              << " duck_new_submitted="
              << command_count(usercmd ? usercmd->duck_new_submission_count : 0U)
              << " jump_sent_sequence="
              << sent_sequence_range(
                     usercmd ? usercmd->first_jump_sent_sequence : std::nullopt,
                     usercmd ? usercmd->last_jump_sent_sequence : std::nullopt)
              << " duck_sent_sequence="
              << sent_sequence_range(
                     usercmd ? usercmd->first_duck_sent_sequence : std::nullopt,
                     usercmd ? usercmd->last_duck_sent_sequence : std::nullopt)
              << " fresh_server_samples="
              << command_count(usercmd ? usercmd->server_samples.size() : 0U)
              << " origin_tolerance=1 velocity_tolerance=1"
              << " view_offset_tolerance=1"
              << " flags=unavailable execution_ack=unavailable\n";
    if (jump_duck_check && usercmd) {
      for (const auto &range : usercmd->transmit_ranges) {
        std::cout << "live_jump_duck_tx netchan_sequence="
                  << range.outgoing_netchan_sequence
                  << " first_new=" << range.first_new_command_sequence
                  << " last_new=" << range.last_new_command_sequence
                  << " new=" << range.new_command_count
                  << " backup=" << range.backup_command_count << '\n';
      }
    }
  }
  if (speed_check) {
    std::cout << "live_speed input=scripted-speed-check result="
              << (usercmd ? hlclient::goldsrc::to_string(usercmd->speed_outcome)
                          : std::string_view{"not_evaluated"})
              << " normal_requested=400 shift_multiplier=0.3"
              << " client_maxspeed=unavailable client_limit_source=unavailable"
              << " movevars_maxspeed="
              << (usercmd && usercmd->movevars_maximum_speed
                      ? std::to_string(*usercmd->movevars_maximum_speed)
                      : std::string{"unavailable"})
              << " movevars_source=signon_movevars"
              << " speed_button_wire_bit=none"
              << " position_source=receiving_client"
              << " origin_tolerance=0.125 velocity_tolerance=1"
              << " execution_ack=unavailable\n";
  }
  if (options.reference_prediction) {
    if (usercmd) {
      const auto& rx = usercmd->driver_rx_total;
      const auto& at = usercmd->driver_rx_at_input_activation;
      const auto delta = [](const std::size_t total, const std::size_t before) {
        return total >= before ? total - before : 0U;
      };
      const auto sequence = [](const std::optional<std::uint32_t> value) {
        return value ? std::to_string(*value) : std::string{"unavailable"};
      };
      std::cout << "live_rx mode=reference driver_updates=" << rx.updates
                << " driver_updates_post_input=" << delta(rx.updates, at.updates)
                << " receive_polls=" << rx.receive_polls
                << " receive_polls_post_input=" << delta(rx.receive_polls, at.receive_polls)
                << " owning_datagrams=" << rx.owning_datagrams
                << " owning_datagrams_post_input=" << delta(rx.owning_datagrams, at.owning_datagrams)
                << " wrong_endpoint=" << rx.wrong_endpoint
                << " accepted_sequences=" << rx.accepted_sequences
                << " accepted_sequences_post_input=" << delta(rx.accepted_sequences, at.accepted_sequences)
                << " rejected_sequences=" << rx.rejected_sequences
                << " payloads_created=" << rx.payloads_created
                << " payloads_created_post_input=" << delta(rx.payloads_created, at.payloads_created)
                << " payloads_consumed=" << usercmd->payload_events_consumed_total
                << " payloads_consumed_post_input=" << delta(usercmd->payload_events_consumed_total, usercmd->payload_events_consumed_at_input_activation)
                << " envelopes_decoded=" << usercmd->service_envelopes_decoded_total
                << " envelopes_decoded_post_input=" << delta(usercmd->service_envelopes_decoded_total, usercmd->service_envelopes_decoded_at_input_activation)
                << " records_attempted=" << usercmd->runtime_records_attempted_total
                << " records_attempted_post_input=" << delta(usercmd->runtime_records_attempted_total, usercmd->runtime_records_attempted_at_input_activation)
                << " records_committed=" << usercmd->runtime_records_committed_total
                << " records_committed_post_input=" << delta(usercmd->runtime_records_committed_total, usercmd->runtime_records_committed_at_input_activation)
                << " clientdata_committed=" << usercmd->clientdata_records_committed_total
                << " clientdata_committed_post_input=" << delta(usercmd->clientdata_records_committed_total, usercmd->clientdata_records_committed_at_input_activation)
                << " samples_delivered_post_input=" << usercmd->server_samples.size()
                << " first_accepted_sequence=" << sequence(rx.first_accepted_sequence)
                << " last_accepted_sequence=" << sequence(rx.last_accepted_sequence)
                << " first_payload_sequence=" << sequence(rx.first_payload_sequence)
                << " last_payload_sequence=" << sequence(rx.last_payload_sequence)
                << "\n";
    }
    const auto vector_or_unavailable = [&](const auto& vector) {
      return vector ? print_vector(*vector) : std::string{"unavailable"};
    };
    std::cout << "live_prediction mode=reference result="
              << prediction_result
              << " state=" << hlclient::goldsrc::to_string(final_prediction.state)
              << " reason=" << primary_error
              << " seed_status="
              << (final_prediction.last_seed_status
                      ? hlclient::goldsrc::to_string(
                            *final_prediction.last_seed_status)
                      : std::string_view{"unavailable"})
              << " seed_field="
              << (final_prediction.last_seed_field
                      ? hlclient::goldsrc::to_string(
                            *final_prediction.last_seed_field)
                      : std::string_view{"unavailable"})
              << " ground_status="
              << (final_prediction.last_ground_status
                      ? hlclient::goldsrc::to_string(
                            *final_prediction.last_ground_status)
                      : std::string_view{"unavailable"})
              << " collision_revision="
              << (final_prediction.collision_identity
                      ? std::to_string(final_prediction.collision_identity
                                           ->collision_world_revision)
                      : std::string{"unavailable"})
              << " collision_attached=" << prediction_collision_attached
              << " sent_carrier_bindings="
              << (usercmd ? usercmd->reference_prediction_sent_bindings : 0U)
              << " received_anchor_bindings="
              << (usercmd ? usercmd->reference_prediction_anchor_bindings : 0U)
              << " seed_candidates="
              << (usercmd ? usercmd->reference_prediction_seed_candidates : 0U)
              << " command_candidates="
              << (usercmd ? usercmd->reference_prediction_command_candidates : 0U)
              << " fresh_server_samples="
              << (usercmd ? usercmd->server_samples.size() : 0U)
              << " active_frames=" << prediction_active_frames
              << " fallback_frames=" << prediction_fallback_frames
              << " moving_active_frames=" << prediction_moving_active_frames
              << " moving_presented_changes="
              << prediction_moving_presented_changes
              << " moving_simulation_changes="
              << prediction_moving_simulation_changes
              << " local_steps=" << final_prediction.local_steps
              << " accepted_corrections="
              << final_prediction.accepted_corrections
              << " replayed_commands=" << final_prediction.replayed_commands
              << " fallback_count=" << final_prediction.fallback_count
              << " epoch=" << final_prediction.prediction_epoch
              << " history_depth=" << final_prediction.history_depth
              << " raw_position_error="
              << (final_prediction.last_raw_position_error
                      ? std::to_string(*final_prediction.last_raw_position_error)
                      : std::string{"unavailable"})
              << " maximum_raw_position_error="
              << (final_prediction.maximum_raw_position_error
                      ? std::to_string(*final_prediction.maximum_raw_position_error)
                      : std::string{"unavailable"})
              << " anchor_command="
              << (final_prediction.anchor_command
                      ? std::to_string(*final_prediction.anchor_command)
                      : std::string{"unavailable"})
              << " source_record="
              << (final_prediction.last_record_identity
                      ? std::to_string(*final_prediction.last_record_identity)
                      : std::string{"unavailable"})
              << " canonical_origin="
              << vector_or_unavailable(final_prediction.canonical_origin)
              << " predicted_origin="
              << vector_or_unavailable(final_prediction.predicted_origin)
              << " presented_origin="
              << vector_or_unavailable(final_prediction.presented_origin)
              << " view_policy="
              << (final_prediction.state ==
                          hlclient::goldsrc::LiveReferencePredictionState::active
                      ? "local_prediction_one_command_lag"
                      : "latest_receiving_client_sample")
              << " execution_ack=reference_carrier_derived\n";
  }
  std::cout << "live_visual_control result="
            << (options.reference_prediction
                    ? prediction_result
                    : success
                    ? speed_check
                          ? "live_normal_speed_and_shift_walk_verified"
                    : jump_duck_check
                          ? "fresh_project_client_jump_duck_server_verified"
                          : "fresh_project_client_live_visual_control_integrated"
                    : "live_visual_scene_verified_input_partial")
            << " input=" << (keyboard ? "keyboard-mouse"
                                      : speed_check
                                            ? "scripted-speed-check"
                                      : jump_duck_check
                                            ? "scripted-jump-duck-check"
                                      : side_check ? "scripted-side-check"
                                                   : "scripted-check")
            << " map="
            << (local_summary ? local_summary->map : std::string{"unavailable"})
            << " view_policy="
            << (options.reference_prediction
                    ? final_prediction.state ==
                              hlclient::goldsrc::LiveReferencePredictionState::active
                          ? "local_prediction_one_command_lag"
                          : "latest_receiving_client_sample"
                    : "ordinary_receiving_client_vertical_offset_v1")
            << " generated="
            << (usercmd ? usercmd->generated_command_count : 0U)
            << " history=" << (usercmd ? usercmd->history_command_count : 0U)
            << " new="
            << (usercmd ? usercmd->new_command_submission_count : 0U)
            << " backup="
            << (usercmd ? usercmd->backup_command_submission_count : 0U)
            << " sent_packets="
            << (usercmd ? usercmd->transmitted_packet_count : 0U)
            << " a_held_frames=" << a_held_frames
            << " d_held_frames=" << d_held_frames
            << " opposing_side_frames=" << opposing_side_frames
            << " nonzero_side_intent_frames=" << nonzero_side_intent_frames
            << " last_a_held=" << (usercmd && usercmd->last_a_held)
            << " last_d_held=" << (usercmd && usercmd->last_d_held)
            << " sampled_forward="
            << (usercmd ? usercmd->last_sampled_forward_axis : 0.0F)
            << " sampled_side="
            << (usercmd ? usercmd->last_sampled_side_axis : 0.0F)
            << " nonzero_side_generated="
            << (usercmd ? usercmd->nonzero_side_generated_count : 0U)
            << " nonzero_side_new_submitted="
            << (usercmd ? usercmd->nonzero_side_new_submission_count : 0U)
            << " first_new_sequence="
            << (usercmd && !usercmd->transmit_ranges.empty()
                    ? usercmd->transmit_ranges.front().first_new_command_sequence
                    : 0U)
            << " last_new_sequence="
            << (usercmd && !usercmd->transmit_ranges.empty()
                    ? usercmd->transmit_ranges.back().last_new_command_sequence
                    : 0U)
            << " current_yaw=" << camera_controller.yaw_degrees()
            << " server_samples="
            << (usercmd ? usercmd->server_samples.size() : 0U)
            << " camera_samples=" << fresh_camera_samples
            << " server_angle_corrections=" << server_angle_corrections
            << " camera_translation=" << camera_translation
            << " eye_span=" << eye_span_x << ',' << eye_span_y
            << " first_eye="
            << (first_camera_sample
                    ? print_vector(first_camera_sample->eye_position)
                    : std::string{"unavailable"})
            << " last_eye="
            << (last_camera_sample
                    ? print_vector(last_camera_sample->eye_position)
                    : std::string{"unavailable"})
            << " canonical_revision="
            << scene_source.world_state().runtime_publication_revision()
            << " camera_revision=" << camera_controller.camera_revision()
            << " entity_frame_revision="
            << scene_source.world_state().entity_frame_revision()
            << " static_resource_revision="
            << scene_source.world_state().world_revision()
            << " decoded="
            << (local_summary ? local_summary->decoded_entities : 0U)
            << " resolved="
            << (local_summary ? local_summary->resolved_instances : 0U)
            << " submitted="
            << (local_summary ? local_summary->rendered_instances : 0U)
            << " unsupported="
            << (local_summary ? local_summary->unsupported_instances : 0U)
            << " world_draws=" << world_stats.draw_call_count
            << " world_uploads=" << world_stats.upload_count
            << " studio_draws=" << entity_stats.studio_draw_count
            << " sprite_draws=" << entity_stats.sprite_draw_count
            << " studio_uploads=" << entity_stats.studio_asset_upload_count
            << " sprite_uploads=" << entity_stats.sprite_asset_upload_count
            << " framebuffer_observations=" << framebuffer_observations
            << " invalid_framebuffer_observations="
            << invalid_framebuffer_observations
            << " non_clear_framebuffers=" << non_clear_framebuffers
            << " changed_framebuffers=" << changed_framebuffers
            << " successful_presentations=" << successful_presentations
            << " preactivation_commands=" << preactivation_command_count
            << " capture_acquired=" << capture_acquisitions
            << " capture_released=" << capture_releases
            << " focus_losses=" << focus_loss_count
            << " asset_job_started=" << asset_job_started
            << " assets_installed=" << assets_installed
            << " renderer_resources_presented="
            << renderer_resources_presented
            << " close="
            << (window_closed
                    ? "requested"
                    : timed_session_complete ? "timed_session_complete"
                                             : "not_requested")
            << " motion="
            << (usercmd ? hlclient::goldsrc::to_string(usercmd->motion_outcome)
                        : std::string_view{"not_evaluated"})
            << " client_world_state_published="
            << (session.live_runtime_result() ? "true" : "false")
            << " movement_verified="
            << (usercmd && usercmd->movement_verified ? "true" : "false")
            << " view_status=" << hlclient::app::to_string(last_view_status)
            << " primary_error=" << primary_error
            << " gl_errors=0 cleanup=complete\n"
            << std::flush;
  return (options.reference_prediction ? prediction_success : success) ? 0 : 2;
}

int run_asset_dispatch_stop(HandshakeSession &session,
                            const bool require_world_geometry,
                            const bool require_world_textures,
                            const bool require_world_render_package) {
  hlclient::core::log(
      LogLevel::info,
      require_world_render_package
          ? "World-render-package stop is building immutable CPU render data "
            "without SDL or GPU work"
      : require_world_textures
          ? "World-textures stop is resolving CPU mip levels without lightmap, "
            "renderer, or GPU work"
      : require_world_geometry
          ? "World-geometry stop is running without texture, lightmap, "
            "renderer, or GPU work"
          : "Asset-dispatch stop is running without renderer or GPU work");
  while (!session.terminal()) {
    session.update(hlclient::goldsrc::ChallengeExchangeClock::now());
    if (!session.terminal()) {
      // The coordinator owns the bounded retry/timeout lifecycle. Keep
      // this CPU-only pump nonblocking while yielding the remainder of
      // the current scheduler quantum between would-block updates.
      std::this_thread::yield();
    }
  }
  return session.report_result(
      require_world_geometry || require_world_textures ||
          require_world_render_package,
      require_world_textures || require_world_render_package,
      require_world_render_package);
}

[[nodiscard]] int
build_and_report_collision_world(const HandshakeSession &session) {
  const auto &dispatch_state = session.asset_dispatch_state();
  if (!dispatch_state || !dispatch_state->dispatch_result().imported()) {
    hlclient::core::log(
        LogLevel::error,
        "Collision-world boundary has no imported BSP prerequisite");
    return 1;
  }
  const auto attachment = std::dynamic_pointer_cast<
      const hlclient::goldsrc::bsp::GoldSrcBspCollisionImportAttachment>(
      dispatch_state->dispatch_result().attachment);
  if (!attachment) {
    hlclient::core::log(LogLevel::error,
                        "Collision-world boundary did not receive canonical "
                        "BSP collision state");
    return 1;
  }
  const auto built =
      hlclient::goldsrc::collision::GoldSrcCollisionWorldBuilder::build(
          attachment->collision_source());
  if (!built || !built.package || built.package->models().empty()) {
    const auto code =
        built.error ? hlclient::goldsrc::collision::to_string(built.error->code)
                    : std::string_view{"unable_to_publish"};
    hlclient::core::log(LogLevel::error,
                        "Collision-world build failed: " + std::string{code});
    return 1;
  }

  const auto &world = built.package->models().front();
  const hlclient::assets::AssetVector3 center{
      world.source_bounds.minimum.x +
          (world.source_bounds.maximum.x - world.source_bounds.minimum.x) *
              0.5F,
      world.source_bounds.minimum.y +
          (world.source_bounds.maximum.y - world.source_bounds.minimum.y) *
              0.5F,
      world.source_bounds.minimum.z +
          (world.source_bounds.maximum.z - world.source_bounds.minimum.z) *
              0.5F,
  };
  hlclient::collision::CollisionWorldQuery query{built.package};
  hlclient::collision::CollisionQueryScratch scratch;
  std::size_t point_probe_count = 0U;
  std::size_t trace_probe_count = 0U;
  for (const auto hull :
       std::array{hlclient::collision::CollisionHullOrdinal::point,
                  hlclient::collision::CollisionHullOrdinal::standing_32x32x72,
                  hlclient::collision::CollisionHullOrdinal::large_64_cube,
                  hlclient::collision::CollisionHullOrdinal::duck_32x32x36}) {
    if (!query.point_contents(
            hlclient::collision::CollisionPointContentsRequest{center, 0U,
                                                               hull},
            scratch)) {
      hlclient::core::log(LogLevel::error,
                          "Collision-world deterministic point probe failed");
      return 1;
    }
    ++point_probe_count;
    hlclient::collision::CollisionTraceRequest trace;
    trace.start = center;
    trace.end = center;
    trace.hull = hull;
    if (!query.trace_hull(trace, scratch)) {
      hlclient::core::log(
          LogLevel::error,
          "Collision-world deterministic stationary trace failed");
      return 1;
    }
    ++trace_probe_count;
  }

  const auto &statistics = built.package->statistics();
  hlclient::core::log(
      LogLevel::info,
      "[collision] profile=valve_bsp_v30_clip_hulls_v1, planes=" +
          std::to_string(built.package->planes().size()) +
          ", nodes=" + std::to_string(built.package->nodes().size()) +
          ", leaves=" + std::to_string(built.package->leaves().size()) +
          ", clipnodes=" + std::to_string(built.package->clipnodes().size()) +
          ", models=" + std::to_string(built.package->models().size()) +
          ", hull-roots=" + std::to_string(statistics.model_hull_root_count) +
          ", point-probes=" + std::to_string(point_probe_count) +
          ", trace-probes=" + std::to_string(trace_probe_count));
  hlclient::core::log(
      LogLevel::info,
      "Collision-world CPU boundary completed without texture, renderer, "
      "OpenGL, SDL, or movement work");
  return 0;
}

int run_evidence_pending_post_resource_stop(HandshakeSession &session) {
  hlclient::core::log(LogLevel::info,
                      "[signon] post-resource phase started; stock entity "
                      "grammar evidence is pending");
  while (!session.terminal()) {
    session.update(hlclient::goldsrc::ChallengeExchangeClock::now());
    if (!session.terminal()) {
      std::this_thread::yield();
    }
  }
  const auto boundary_result = session.report_result();
  if (boundary_result != 0) {
    return boundary_result;
  }
  hlclient::core::log(
      LogLevel::error,
      "[signon] stock post-resource request and entity wire grammar remain "
      "evidence-pending; stopped at the exact unconsumed boundary");
  return 2;
}

int run_evidence_pending_usercmd_stop(HandshakeSession &session) {
  hlclient::core::log(LogLevel::info,
                      "[usercmd] profile=stock_protocol_48_evidence_pending");
  while (!session.terminal()) {
    session.update(hlclient::goldsrc::ChallengeExchangeClock::now());
    if (!session.terminal()) {
      std::this_thread::yield();
    }
  }
  const auto boundary_result = session.report_result();
  if (boundary_result != 0) {
    return boundary_result;
  }
  hlclient::core::log(LogLevel::info, "[usercmd] sampled=0");
  hlclient::core::log(LogLevel::info, "[usercmd] history=0");
  hlclient::core::log(LogLevel::info, "[usercmd] carrier=pending");
  hlclient::core::log(LogLevel::info, "[usercmd] checksum=pending");
  hlclient::core::log(LogLevel::info, "[usercmd] transmitted=0");
  hlclient::core::log(
      LogLevel::error,
      "[usercmd] runtime_signon_evidence_pending; stock move opcode, "
      "envelope, checksum, and unreliable carrier are not enabled");
  return 2;
}

int run(const hlclient::core::CommandLineOptions &options) {
  if (options.authentication_provider ==
      hlclient::core::AuthenticationProviderKind::steam) {
    hlclient::core::log(
        LogLevel::info,
        "[startup] application_entry_observed=true arguments_accepted=true");
  }
  print_version();
  std::cout << '\n' << std::flush;

  if (options.runtime_replay_fixture || options.runtime_replay_capture) {
    const auto scheduling = hlclient::app::RuntimeReplaySchedulingLimits{
        options.runtime_replay_record_budget,
        options.runtime_replay_byte_budget};
    hlclient::app::RuntimeReplaySceneSourceCreateResult source;
    if (options.runtime_replay_capture) {
      source = hlclient::app::RuntimeReplaySceneSource::create_capture(
          *options.runtime_replay_capture, scheduling,
          options.runtime_replay_visuals ==
                  hlclient::core::RuntimeReplayVisualOption::local_assets
              ? options.base_directory
              : std::nullopt,
          options.game_directory,
          options.renderer == hlclient::core::RendererBackend::opengl);
    } else {
      auto fixture_kind =
          hlclient::goldsrc::RuntimeReplayFixtureKind::basic_mixed;
      switch (*options.runtime_replay_fixture) {
      case hlclient::core::RuntimeReplayFixtureOption::basic_mixed:
        fixture_kind = hlclient::goldsrc::RuntimeReplayFixtureKind::basic_mixed;
        break;
      case hlclient::core::RuntimeReplayFixtureOption::missing_entity_base:
        fixture_kind =
            hlclient::goldsrc::RuntimeReplayFixtureKind::missing_entity_base;
        break;
      case hlclient::core::RuntimeReplayFixtureOption::visual_entities:
        fixture_kind =
            hlclient::goldsrc::RuntimeReplayFixtureKind::visual_entities;
        break;
      }
      auto fixture = hlclient::goldsrc::make_runtime_replay_fixture(
          {fixture_kind, 1U, 100U, 10'000U});
      if (!fixture || !fixture.fixture) {
        hlclient::core::log(LogLevel::error,
                            fixture.error
                                ? fixture.error->context
                                : "Unable to build the offline replay fixture");
        return 1;
      }
      source = hlclient::app::RuntimeReplaySceneSource::create(
          std::move(*fixture.fixture), scheduling,
          options.runtime_replay_visuals.has_value());
    }
    if (!source || !source.source) {
      if (source.error && source.error->capture_error) {
        const auto &capture = *source.error->capture_error;
        std::cerr << "runtime_replay_capture error="
                  << hlclient::goldsrc::to_string(capture.code)
                  << " payload=" << capture.replay_payload_ordinal
                  << " cursor=" << capture.cursor.byte_offset() << ':'
                  << capture.cursor.bit_offset();
        if (capture.unsupported_opcode) {
          std::cerr << " opcode="
                    << static_cast<unsigned int>(*capture.unsupported_opcode);
        }
        std::cerr << '\n';
      }
      hlclient::core::log(
          LogLevel::error,
          source.error ? source.error->context
                       : "Unable to create the application replay source");
      return 1;
    }
    const auto started = source.source->start();
    if (!started) {
      hlclient::core::log(
          LogLevel::error,
          started.error ? started.error->context
                        : "Unable to start the application replay source");
      return 1;
    }
    if (options.renderer == hlclient::core::RendererBackend::null) {
      return run_null_renderer(*source.source, smoke_test_frame_limit(),
                               nullptr, source.source.get());
    }
    return run_opengl_renderer(*source.source, smoke_test_frame_limit(),
                               nullptr, source.source.get(),
                               options.runtime_replay_screenshot);
  }

  const bool world_texture_requested =
      options.stop_after == hlclient::core::ConnectionStopPoint::world_textures;
  const bool world_render_package_requested =
      options.stop_after ==
          hlclient::core::ConnectionStopPoint::world_render_package ||
      options.stop_after ==
          hlclient::core::ConnectionStopPoint::world_spatial_scene;
  const bool world_texture_pipeline_requested =
      world_texture_requested || world_render_package_requested;
  const bool production_bsp_dispatch_requested =
      options.stop_after ==
          hlclient::core::ConnectionStopPoint::asset_dispatch ||
      options.stop_after ==
          hlclient::core::ConnectionStopPoint::world_geometry ||
      options.stop_after ==
          hlclient::core::ConnectionStopPoint::collision_world ||
      world_texture_pipeline_requested;

  hlclient::assets::AssetImporterRegistries asset_importers;
  const auto importer_registration =
      hlclient::goldsrc::register_builtin_asset_importers(asset_importers);
  if (!importer_registration) {
    const auto context = importer_registration.error
                             ? importer_registration.error->context
                             : std::string{"unknown registration failure"};
    hlclient::core::log(LogLevel::error,
                        "Unable to register production asset importers: " +
                            context);
    return 1;
  }
  std::unique_ptr<hlclient::filesystem::RootedFileSystem> asset_file_system;
  [[maybe_unused]] std::unique_ptr<hlclient::assets::AssetManager>
      asset_manager;
  if (options.base_directory && !options.resource_consistency_provider) {
    const auto paths = hlclient::filesystem::validate_game_paths(
        path_from_utf8(*options.base_directory),
        path_from_utf8(options.game_directory));
    if (!paths) {
      hlclient::core::log(LogLevel::error, paths.error);
      return 1;
    }
    hlclient::core::log(LogLevel::info, "Half-Life game directory validated");

    auto file_system_result = hlclient::filesystem::RootedFileSystem::create(
        paths.paths->game_directory);
    if (!file_system_result) {
      hlclient::core::log(LogLevel::error,
                          file_system_result.error
                              ? file_system_result.error->context
                              : "Unable to create the asset filesystem");
      return 1;
    }
    asset_file_system = std::move(file_system_result.file_system);
    asset_manager = std::make_unique<hlclient::assets::AssetManager>(
        *asset_file_system, asset_importers);
    hlclient::core::log(
        LogLevel::info,
        "Asset pipeline initialized with the GoldSrc BSP v30 world importer");
  } else if (options.stop_after ==
                 hlclient::core::ConnectionStopPoint::asset_dispatch ||
             options.stop_after ==
                 hlclient::core::ConnectionStopPoint::world_geometry ||
             options.stop_after ==
                 hlclient::core::ConnectionStopPoint::collision_world ||
             world_texture_pipeline_requested) {
    hlclient::core::log(
        LogLevel::info,
        "Approved asset dispatch initialized with the production GoldSrc "
        "BSP v30 world importer");
  } else if (options.resource_consistency_provider) {
    hlclient::core::log(
        LogLevel::info,
        "Asset pipeline remains separate from local resource-consistency mode");
  } else {
    hlclient::core::log(
        LogLevel::info,
        "No Half-Life basedir selected; starting without game assets");
  }

  BootstrapSceneSource scene_source;
  std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
      local_resource_environment;
  std::unique_ptr<
      hlclient::resource_consistency::PreparedLocalResourceConsistencyProvider>
      resource_consistency_provider;
  std::unique_ptr<HandshakeSession> challenge_session;
  if (options.connect_endpoint) {
    const auto address =
        hlclient::network::NetworkAddress::parse(*options.connect_endpoint);
    if (!address || address->port() == 0) {
      hlclient::core::log(LogLevel::error,
                          "Invalid IPv4 endpoint for --connect: " +
                              *options.connect_endpoint);
      return 1;
    }

    scene_source.mutable_world_state().set_connection_requested(true);
    if (hlclient::core::requires_local_resource_consistency_preparation(
            options)) {
      const auto base_directory = path_from_utf8(*options.base_directory);
      auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
          base_directory, options.game_directory);
      if (!roots) {
        const auto code =
            roots.error
                ? hlclient::local_resources::to_string(roots.error->code)
                : std::string_view{"io_error"};
        hlclient::core::log(LogLevel::error,
                            "[local-resource] root validation failed: " +
                                std::string{code});
        return 1;
      }

      auto resolver_limits =
          hlclient::local_resources::LocalResourceResolverLimits{};
      if (production_bsp_dispatch_requested) {
        resolver_limits.maximum_file_size =
            world_texture_pipeline_requested
                ? hlclient::local_resources::kHardMaximumLocalResourceFileSize
                : kProductionGoldSrcBspMaximumSourceBytes;
      }
      auto environment =
          hlclient::local_resources::LocalResourceEnvironment::create(
              std::move(*roots.roots), resolver_limits);
      if (!environment || !environment.environment) {
        const auto code =
            environment.error
                ? hlclient::local_resources::to_string(environment.error->code)
                : std::string_view{"unable_to_retain_environment"};
        hlclient::core::log(LogLevel::error,
                            "[local-resource] environment creation failed: " +
                                std::string{code});
        return 1;
      }
      local_resource_environment = std::shared_ptr<
          const hlclient::local_resources::LocalResourceEnvironment>{
          std::move(environment.environment)};
      const auto root_count = local_resource_environment->root_count();
      auto provider = hlclient::resource_consistency::
          PreparedLocalResourceConsistencyProvider::prepare(
              *local_resource_environment);
      if (!provider) {
        const auto code = provider.error
                              ? hlclient::resource_consistency::to_string(
                                    provider.error->code)
                              : std::string_view{"provider_error"};
        hlclient::core::log(
            LogLevel::error,
            "[local-resource] consistency provider preparation failed: " +
                std::string{code});
        return 1;
      }

      resource_consistency_provider = std::move(provider.provider);
      hlclient::core::log(LogLevel::info,
                          "[local-resource] roots validated: count=" +
                              std::to_string(root_count));
      hlclient::core::log(
          LogLevel::info,
          "[local-resource] consistency material ready: byte-count=" +
              std::to_string(resource_consistency_provider->byte_count()) +
              ", opaque-bytes=" +
              std::to_string(
                  resource_consistency_provider->opaque_byte_count()));
    }

    auto preparation = prepare_runtime_connect_request(options, *address);
    hlclient::goldsrc::HandshakeStopPoint stop_point =
        hlclient::goldsrc::HandshakeStopPoint::challenge;
    switch (options.stop_after) {
    case hlclient::core::ConnectionStopPoint::challenge:
      break;
    case hlclient::core::ConnectionStopPoint::connect_request:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::connect_request;
      break;
    case hlclient::core::ConnectionStopPoint::connect_response:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::connect_response;
      break;
    case hlclient::core::ConnectionStopPoint::netchan_bootstrap:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::netchan_bootstrap;
      break;
    case hlclient::core::ConnectionStopPoint::signon_boundary:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::signon_boundary;
      break;
    case hlclient::core::ConnectionStopPoint::pre_resource:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::pre_resource;
      break;
    case hlclient::core::ConnectionStopPoint::delta_schemas:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::delta_schemas;
      break;
    case hlclient::core::ConnectionStopPoint::movevars:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::movevars;
      break;
    case hlclient::core::ConnectionStopPoint::user_info:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::user_info;
      break;
    case hlclient::core::ConnectionStopPoint::resource_list_boundary:
      stop_point =
          hlclient::goldsrc::HandshakeStopPoint::resource_list_boundary;
      break;
    case hlclient::core::ConnectionStopPoint::resource_list:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::resource_list;
      break;
    case hlclient::core::ConnectionStopPoint::resource_response_boundary:
      stop_point =
          hlclient::goldsrc::HandshakeStopPoint::resource_response_boundary;
      break;
    case hlclient::core::ConnectionStopPoint::server_baselines:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::server_baselines;
      break;
    case hlclient::core::ConnectionStopPoint::entity_snapshot:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::entity_snapshot;
      break;
    case hlclient::core::ConnectionStopPoint::live_runtime_state:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::live_runtime_state;
      break;
    case hlclient::core::ConnectionStopPoint::live_usercmd_check:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::live_runtime_state;
      break;
    case hlclient::core::ConnectionStopPoint::live_visual_control:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::live_runtime_state;
      break;
    case hlclient::core::ConnectionStopPoint::usercmd_boundary:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::usercmd_boundary;
      break;
    case hlclient::core::ConnectionStopPoint::precache_manifest:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::precache_manifest;
      break;
    case hlclient::core::ConnectionStopPoint::asset_dispatch:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::asset_dispatch;
      break;
    case hlclient::core::ConnectionStopPoint::world_geometry:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::asset_dispatch;
      break;
    case hlclient::core::ConnectionStopPoint::collision_world:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::asset_dispatch;
      break;
    case hlclient::core::ConnectionStopPoint::world_textures:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::world_textures;
      break;
    case hlclient::core::ConnectionStopPoint::world_render_package:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::world_render_package;
      break;
    case hlclient::core::ConnectionStopPoint::world_spatial_scene:
      stop_point = hlclient::goldsrc::HandshakeStopPoint::world_spatial_scene;
      break;
    case hlclient::core::ConnectionStopPoint::entity_visual_scene:
      // Stock Protocol 48 visual projection is deliberately sealed at
      // the snapshot boundary. Synthetic playback is supplied by the
      // offline entity viewer and test-owned stage inputs.
      stop_point = hlclient::goldsrc::HandshakeStopPoint::entity_snapshot;
      break;
    }
    challenge_session = std::make_unique<HandshakeSession>(
        *address, stop_point, std::move(preparation.request),
        std::move(preparation.authentication_session),
        std::move(preparation.provider), std::move(preparation.settings),
        preparation.profile, resource_consistency_provider.get(),
        local_resource_environment, &asset_importers,
        production_bsp_dispatch_requested && !world_texture_pipeline_requested
            ? production_bsp_asset_dispatch_config()
            : hlclient::goldsrc::PrecacheAssetDispatchStageConfig{},
        world_texture_pipeline_requested
            ? production_world_texture_import_config()
            : hlclient::goldsrc::WorldTextureImportStageConfig{},
        world_render_package_requested
            ? production_world_render_package_config(options)
            : hlclient::goldsrc::WorldRenderPackageStageConfig{},
        (options.stop_after ==
             hlclient::core::ConnectionStopPoint::live_runtime_state ||
         options.stop_after ==
             hlclient::core::ConnectionStopPoint::live_usercmd_check ||
         options.stop_after ==
             hlclient::core::ConnectionStopPoint::live_visual_control)
            ? &scene_source.mutable_world_state()
            : nullptr,
        options.stop_after ==
                hlclient::core::ConnectionStopPoint::live_usercmd_check
            ? hlclient::goldsrc::LiveRuntimeOperationMode::live_usercmd_check
        : options.stop_after ==
                hlclient::core::ConnectionStopPoint::live_visual_control
            ? hlclient::goldsrc::LiveRuntimeOperationMode::live_visual_control
            : hlclient::goldsrc::LiveRuntimeOperationMode::runtime_state,
        options.live_input == hlclient::core::LiveInputMode::keyboard_mouse
            ? hlclient::goldsrc::LiveVisualControlInputSource::keyboard_mouse
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_jump_duck_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_jump_duck_check
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_speed_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_speed_check
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_side_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_side_check
            : hlclient::goldsrc::LiveVisualControlInputSource::scripted_check,
        options.reference_prediction,
        options.net_trace);
  }

  if (options.stop_after ==
          hlclient::core::ConnectionStopPoint::usercmd_boundary &&
      challenge_session) {
    return run_evidence_pending_usercmd_stop(*challenge_session);
  }

  if ((options.stop_after ==
           hlclient::core::ConnectionStopPoint::server_baselines ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::entity_snapshot ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::entity_visual_scene) &&
      challenge_session) {
    return run_evidence_pending_post_resource_stop(*challenge_session);
  }

  if ((options.stop_after ==
           hlclient::core::ConnectionStopPoint::asset_dispatch ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::world_geometry ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::collision_world ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::world_textures ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::world_render_package ||
       options.stop_after ==
           hlclient::core::ConnectionStopPoint::world_spatial_scene) &&
      challenge_session) {
    const int cpu_result = run_asset_dispatch_stop(
        *challenge_session,
        options.stop_after ==
                hlclient::core::ConnectionStopPoint::world_geometry ||
            options.stop_after ==
                hlclient::core::ConnectionStopPoint::collision_world,
        options.stop_after ==
            hlclient::core::ConnectionStopPoint::world_textures,
        world_render_package_requested);
    if (cpu_result != 0) {
      return cpu_result;
    }

    if (options.stop_after ==
        hlclient::core::ConnectionStopPoint::collision_world) {
      return build_and_report_collision_world(*challenge_session);
    }

    const bool spatial_scene_requested =
        options.stop_after ==
        hlclient::core::ConnectionStopPoint::world_spatial_scene;
    if (spatial_scene_requested) {
      if (!challenge_session->world_spatial_scene()) {
        hlclient::core::log(LogLevel::error,
                            "World spatial-scene boundary completed without an "
                            "immutable scene package");
        return 1;
      }
      auto preview_scene = challenge_session->world_spatial_scene();
      auto spawn_camera = challenge_session->world_spawn_camera();
      auto preview_options = world_preview_options(options, spawn_camera);

      // The stage has already finalized its retained network boundary.
      // Destroy every network/auth/resource owner before either the
      // CPU-only visibility consumer or SDL/OpenGL preview begins.
      challenge_session.reset();
      resource_consistency_provider.reset();
      local_resource_environment.reset();
      hlclient::world_preview::WorldPreviewSceneSource preview_source{
          std::move(preview_scene), std::move(preview_options)};
      const int scene_result = report_world_spatial_scene(preview_source);
      if (scene_result != 0 || !options.view_world) {
        return scene_result;
      }
      return run_opengl_renderer(preview_source, smoke_test_frame_limit(),
                                 nullptr);
    }

    if (!options.view_world) {
      return 0;
    }
    if (!challenge_session->world_render_package()) {
      hlclient::core::log(
          LogLevel::error,
          "World preview requested without an immutable render package");
      return 1;
    }
    auto preview_package = challenge_session->world_render_package();
    // The preview is deliberately disconnected local rendering. Destroy
    // the coordinator, UDP socket, consistency provider and verified
    // resource environment before SDL or an OpenGL context can exist.
    challenge_session.reset();
    resource_consistency_provider.reset();
    local_resource_environment.reset();
    hlclient::world_preview::WorldPreviewSceneSource preview_source{
        std::move(preview_package)};
    return run_opengl_renderer(preview_source, smoke_test_frame_limit(),
                               nullptr);
  }

  const auto frame_limit = smoke_test_frame_limit();
  if (options.stop_after ==
          hlclient::core::ConnectionStopPoint::live_visual_control &&
      challenge_session) {
    return run_live_visual_control(scene_source, *challenge_session, options);
  }
  if (options.renderer == hlclient::core::RendererBackend::null) {
    return run_null_renderer(scene_source, frame_limit,
                             challenge_session.get());
  }
  return run_opengl_renderer(scene_source, frame_limit,
                             challenge_session.get());
}

int application_main(const int argument_count,
#ifdef _WIN32
                     wchar_t *arguments[])
#else
                     char *arguments[])
#endif
{
  hlclient::core::initialize_logging(
#if !defined(NDEBUG)
      LogLevel::debug
#else
      LogLevel::info
#endif
  );

  const auto owned_arguments =
      command_line_arguments(argument_count, arguments);
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
  } catch (const std::exception &exception) {
    hlclient::core::log(LogLevel::fatal, exception.what());
    return 1;
  }
}

} // namespace

#ifdef _WIN32
int wmain(const int argument_count, wchar_t *arguments[])
#else
int main(const int argument_count, char *arguments[])
#endif
{
  try {
    return application_main(argument_count, arguments);
  } catch (const std::exception &exception) {
    std::cerr << "[fatal] " << exception.what() << '\n';
    return 1;
  }
}
