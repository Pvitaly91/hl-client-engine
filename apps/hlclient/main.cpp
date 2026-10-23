#include <hlclient/app/explicit_file_authentication_provider.hpp>
#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/app/hud_presentation.hpp>
#include <hlclient/app/intentional_session_close.hpp>
#include <hlclient/app/external_session.hpp>
#include <hlclient/app/external_diagnostics.hpp>
#include <hlclient/app/external_observation_gate.hpp>
#include <hlclient/app/external_p1_branch_observation.hpp>
#include <hlclient/app/application_presentation_settings.hpp>
#include <hlclient/app/session_receipt.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#include <hlclient/app/multiplayer_presentation_diagnostics.hpp>
#include <hlclient/core/movement_correction_witness.hpp>
#include <hlclient/app/session_receipt_commit.hpp>
#include <hlclient/app/console_application.hpp>
#include <hlclient/app/console_steam_authentication.hpp>
#include <hlclient/app/console_lifecycle.hpp>
#include <hlclient/app/console_screen.hpp>
#include <hlclient/goldsrc/console_background.hpp>
#ifdef _WIN32
#include <hlclient/app/console_session_attempt.hpp>
#include <hlclient/platform/windows/session_receipt_file.hpp>
#include <hlclient/platform/windows/external_launcher.hpp>
#endif
#include <hlclient/app/remote_player_visibility_trace.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#if HLCLIENT_BUILD_GAME_HALFLIFE
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/console_commands.hpp>
#endif
#include <hlclient/app/precache_manifest_exit_policy.hpp>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/app/model_preparation_cache.hpp>
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/app/remote_effect_presentation.hpp>
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/audio/output.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/hud_assets.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/app/runtime_replay_scene_source.hpp>
#include <hlclient/app/steam_authentication_provider.hpp>
#include <hlclient/app/steam_connection_diagnostics.hpp>
#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/asset_manager.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/app/test_start_health_gate.hpp>
#include <hlclient/core/log.hpp>
#include <hlclient/core/version.hpp>
#include <hlclient/filesystem/game_paths.hpp>
#include <hlclient/filesystem/rooted_file_system.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/client_message.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/goldsrc/precache_manifest_stage.hpp>
#include <hlclient/goldsrc/world_render/world_render_package_stage.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import_stage.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
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
#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <mutex>
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
#include <sstream>
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

[[nodiscard]] constexpr hlclient::app::MovementGpuPassStatus movement_gpu_status(
    hlclient::renderer::opengl::OpenGlGpuTimingStatus status) noexcept {
  using Source=hlclient::renderer::opengl::OpenGlGpuTimingStatus;
  using Target=hlclient::app::MovementGpuPassStatus;
  switch(status) {
  case Source::not_observed:return Target::not_observed;
  case Source::pending:return Target::pending;
  case Source::ready:return Target::ready;
  case Source::unsupported:return Target::unsupported;
  case Source::disjoint:return Target::disjoint;
  case Source::invalid:return Target::invalid;
  case Source::capacity_dropped:return Target::capacity_dropped;
  }
  return Target::invalid;
}

[[nodiscard]] constexpr hlclient::app::MovementStaticWorldPath movement_static_world_path(
    hlclient::renderer::opengl::OpenGlStaticWorldPath source) noexcept {
  using Source=hlclient::renderer::opengl::OpenGlStaticWorldPath;
  using Target=hlclient::app::MovementStaticWorldPath;
  switch(source) {
  case Source::none:return Target::none;
  case Source::visible_commands:return Target::visible_commands;
  case Source::original_batches:return Target::original_batches;
  case Source::prepared_bypass:return Target::prepared_bypass;
  case Source::prepared_selected:return Target::prepared_selected;
  }
  return Target::none;
}
[[nodiscard]] constexpr hlclient::app::MovementStaticWorldFallbackReason movement_static_world_fallback(
    hlclient::renderer::opengl::OpenGlStaticWorldFallback source) noexcept {
  using Source=hlclient::renderer::opengl::OpenGlStaticWorldFallback;
  using Target=hlclient::app::MovementStaticWorldFallbackReason;
  switch(source) {
  case Source::none:return Target::none;
  case Source::prepared_unavailable:return Target::prepared_unavailable;
  case Source::insufficient_probe_rejections:return Target::insufficient_probe_rejections;
  case Source::invalid_prepared_batch:return Target::invalid_prepared_batch;
  case Source::fragmented_batch:return Target::fragmented_batch;
  }
  return Target::none;
}
[[nodiscard]] hlclient::app::MovementStaticWorldFrameRecord movement_static_world_frame(
    const hlclient::renderer::opengl::OpenGlStaticWorldFrameReceipt& source) noexcept {
  hlclient::app::MovementStaticWorldFrameRecord result;
  result.path=movement_static_world_path(source.path);
  result.fallback_reason=movement_static_world_fallback(source.fallback);
  if(source.path!=hlclient::renderer::opengl::OpenGlStaticWorldPath::none) {
    result.resource_id=source.resource_id;
    result.resource_revision=source.resource_revision;
  }
  result.considered_blocks=source.considered_blocks;
  result.tested_bounds=source.tested_bounds;
  result.selected_blocks=source.selected_blocks;
  result.considered_surfaces=source.considered_surfaces;
  result.selected_surfaces=source.selected_surfaces;
  result.submitted_draws=source.api_draws;
  result.submitted_triangles=source.submitted_triangles;
  result.prepared_rebuilds=source.prepared_rebuilds;
  result.selection_preparation_ms=source.selection_preparation_ms;
  return result;
}

bool external_launch_active{};
std::string external_last_stage,external_last_reason,external_failure_reason;
void external_status(std::string_view stage,std::string_view reason="none") {
  if(!external_launch_active) return;
  const auto token=[](std::string_view text) {return !text.empty() && text.size()<=96U &&
      std::ranges::all_of(text,[](char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-';});};
  if(!token(stage) || !token(reason)) {stage="error";reason="invalid_diagnostic_category";}
  if(stage=="error") external_failure_reason=reason;
  if(external_last_stage==stage && external_last_reason==reason) return;
  external_last_stage=stage;external_last_reason=reason;
  std::cout<<"[external-status] stage="<<stage<<" reason="<<reason<<'\n'<<std::flush;
}

bool external_cancel_requested(const hlclient::core::CommandLineOptions& options) {
  if(!options.external_cancel_event) return false;
#ifdef _WIN32
  const auto result=::WaitForSingleObject(reinterpret_cast<HANDLE>(*options.external_cancel_event),0);
  if(result==WAIT_OBJECT_0)return true;
  if(result==WAIT_TIMEOUT)return false;
#endif
  external_status("error","cancel_handle_invalid");
  throw std::runtime_error{"External cancellation handle is invalid"};
}

struct SessionReceiptFactMailbox final {
  std::atomic<std::uint64_t> facts{};
  std::mutex authentication_mutex;
  hlclient::app::SteamConnectionDiagnostics authentication;
  const std::chrono::steady_clock::time_point authentication_origin{std::chrono::steady_clock::now()};
};
std::atomic<std::shared_ptr<SessionReceiptFactMailbox>> session_receipt_mailbox;
void receipt_fact_observed(hlclient::app::SessionReceiptFact fact) noexcept {
  const auto mailbox=session_receipt_mailbox.load();
  if(mailbox) mailbox->facts.fetch_or(hlclient::app::receipt_fact(fact),std::memory_order_relaxed);
}
template<class Event> void receipt_authentication_observed(const Event& event) noexcept {
  const auto mailbox=session_receipt_mailbox.load();
  if(!mailbox)return;
  try {
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-mailbox->authentication_origin).count();
    std::lock_guard lock(mailbox->authentication_mutex);
    mailbox->authentication.observe(event,elapsed>0 ? static_cast<std::uint64_t>(elapsed) : 0U);
  } catch(...) {} // Optional observer must never change connection outcome.
}
void receipt_authentication_rejected(std::string_view text) noexcept {
  const auto mailbox=session_receipt_mailbox.load();
  if(!mailbox)return;
  try {
    std::lock_guard lock(mailbox->authentication_mutex);
    mailbox->authentication.rejection(text);
  } catch(...) {}
}
template<class Callback,class Logger,class Observe>
Callback receipt_aware_trace(const bool logging,Logger logger,Observe observe) {
  return Callback{[logging,logger,observe](const auto& event) {
    observe(event); // typed owning callback, independent of diagnostic logging
    if(logging) logger(event);
  }};
}

class ApplicationSessionReceipts final {
public:
  using Clock=std::chrono::steady_clock;
  explicit ApplicationSessionReceipts(const hlclient::core::CommandLineOptions& options,
      bool* integrity_failure=nullptr)
      : origin_(Clock::now()),uncaught_(std::uncaught_exceptions()),external_(options.external_server_client),
        movement_(options.movement_diagnostics),multiplayer_(movement_),integrity_failure_(integrity_failure) {
    movement_.set_multiplayer_clock_origin_ns(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(origin_.time_since_epoch()).count()));
    if(!options.session_receipt_root) return;
    if(integrity_failure_)*integrity_failure_=true; // Constructor failure is durable-output failure.
#ifdef _WIN32
    const auto& root=*options.session_receipt_root;
    const auto path=std::filesystem::path{std::u8string_view{
        reinterpret_cast<const char8_t*>(root.data()),root.size()}};
    if(path.filename().string()!=*options.session_run_id)
      throw std::invalid_argument{"Session receipt directory does not match run identity"};
    auto directory=hlclient::platform::windows::open_secure_output_directory(path);
    const auto process=hlclient::platform::windows::query_current_session_receipt_process_identity();
    const auto clock=hlclient::platform::windows::query_session_receipt_clock();
    if(!directory || !process || !clock) throw std::runtime_error{"Session receipt output unavailable"};
    directory_=std::move(directory.directory);
    journal_.emplace(hlclient::app::SessionReceiptIdentity{*options.session_run_id,
        *options.session_role=="B" ? hlclient::app::SessionReceiptRole::B : hlclient::app::SessionReceiptRole::A,
        process->process_id,process->process_creation},clock->frequency);
    if(!journal_->begin_generation({})) throw std::runtime_error{"Session receipt initialization failed"};
    mailbox_=std::make_shared<SessionReceiptFactMailbox>();
    session_receipt_mailbox.store(mailbox_);
    publish();
    if(failed_) {
      session_receipt_mailbox.store({});
      throw std::runtime_error{"Initial session receipt publication failed"};
    }
#else
    throw std::runtime_error{"Owned session receipts require the Windows reporting backend"};
#endif
    if(integrity_failure_)*integrity_failure_=false;
  }
  ~ApplicationSessionReceipts() {
    if(journal_ && !finished_) {
      facts();
      hlclient::app::SessionCloseReceipt close;
      close.reason=std::uncaught_exceptions()>uncaught_ ? hlclient::app::SessionCloseReason::fatal_failure : hlclient::app::SessionCloseReason::not_observed;
      close.outcome=std::uncaught_exceptions()>uncaught_ ? hlclient::app::SessionCloseOutcome::fatal_failure : hlclient::app::SessionCloseOutcome::not_requested;
      finish(close);
    }
    // run() constructs receipts before all provider/session owners. By now
    // their teardown events have arrived, including Terminate/Shutdown.
#ifdef _WIN32
    if(external_ && mailbox_ && journal_ && directory_) {
      try {
        std::lock_guard lock(mailbox_->authentication_mutex);
        if(mailbox_->authentication.observed()) {
          const auto bytes=mailbox_->authentication.encode(journal_->snapshot()->identity);
          if(!hlclient::platform::windows::secure_atomic_write_new(*directory_,
              L"external-authentication.json",std::as_bytes(std::span{bytes.data(),bytes.size()})))
            hlclient::core::log(LogLevel::warning,"[auth-diagnostics] publication_failed=true");
        }
      } catch(...) {
        try {hlclient::core::log(LogLevel::warning,"[auth-diagnostics] publication_failed=true");}catch(...){}
      }
    }
#endif
    if(mailbox_) session_receipt_mailbox.store({});
    if(integrity_failure_ && failed_)*integrity_failure_=true;
  }
  ApplicationSessionReceipts(const ApplicationSessionReceipts&)=delete;
  ApplicationSessionReceipts& operator=(const ApplicationSessionReceipts&)=delete;
  hlclient::app::SessionReceiptStamp stamp(std::optional<std::uint64_t> sequence={},std::optional<std::uint64_t> publication={}) const noexcept {
    hlclient::app::SessionReceiptStamp result;
    result.process_ms=milliseconds(Clock::now());result.source_sequence=sequence;result.publication_ordinal=publication;
#ifdef _WIN32
    if(const auto clock=hlclient::platform::windows::query_session_receipt_clock()) result.host_qpc_ticks=clock->ticks;
#endif
    return result;
  }
  std::uint64_t milliseconds(Clock::time_point time) const noexcept {
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(time-origin_).count();
    return elapsed>0 ? static_cast<std::uint64_t>(elapsed) : 0U;
  }
  hlclient::app::MovementIncidentRecorder& movement() noexcept {return movement_;}
  hlclient::app::MultiplayerPresentationDiagnostics& multiplayer() noexcept {return multiplayer_;}
  hlclient::core::MultiplayerDiagnosticSink* diagnostic_sink() noexcept {
    return movement_.enabled()?&multiplayer_:nullptr;
  }
  void diagnostic_capabilities(const hlclient::game_api::GameClientHost& host) {
    if(!movement_.enabled() || !diagnostic_capabilities_.empty())return;
    diagnostic_capabilities_="[";bool first=true;
    for(const auto& entry:host.weapon_diagnostic_capabilities()) {
      if(!first)diagnostic_capabilities_+=',';first=false;
      diagnostic_capabilities_+="{\"weapon_id\":"+std::to_string(entry.weapon_id)+
          ",\"command\":"+hlclient::core::external_json_string(entry.command.substr(0,64))+",\"modes\":[";
      for(std::size_t i=0;i<entry.modes.size();++i) {
        if(i)diagnostic_capabilities_+=',';
        diagnostic_capabilities_+=std::to_string(static_cast<unsigned>(entry.modes[i]));
      }
      diagnostic_capabilities_+="],\"held_primary\":"+std::string{entry.held_primary?"true":"false"}+
          ",\"persistent_objects\":"+(entry.persistent_objects?"true":"false")+"}";
    }
    diagnostic_capabilities_+=']';
  }
  void diagnostic(hlclient::core::MultiplayerDiagnosticPayload payload,
      hlclient::core::MultiplayerDiagnosticProducer producer,
      std::optional<std::uint64_t> frame={},std::optional<std::uint64_t> input={},
      std::optional<std::pair<std::uint64_t,std::uint64_t>> source_generation={}) noexcept {
    if(!movement_.enabled())return;
    hlclient::core::MultiplayerDiagnosticEvent event;
    event.identity.producer=producer;
    event.identity.producer_event_id=++diagnostic_event_ids_[static_cast<std::size_t>(producer)];
    if(source_generation) {
      event.identity.network_generation=source_generation->first;
      event.identity.map_generation=source_generation->second;
    } else if(const auto identity=snapshot()) {
      event.identity.network_generation=identity->generation.network_generation;
      event.identity.map_generation=identity->generation.map_generation;
    }
    event.identity.render_frame=frame;event.identity.input_sequence=input;
    event.local_steady_ns=hlclient::core::multiplayer_diagnostic_now_ns();
    event.provenance=hlclient::core::MultiplayerProvenance::runtime_observed;
    event.edge=hlclient::core::MultiplayerEdgeConfidence::exact;
    event.payload=payload;(void)multiplayer_.observe(event);
  }
  std::uint64_t movement_stamp(Clock::time_point time) const noexcept {
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(time-origin_).count();
    return elapsed>0 ? static_cast<std::uint64_t>(elapsed) : 0U;
  }
  std::optional<std::uint64_t> movement_stamp_ns(const std::optional<std::uint64_t> epoch_ns) const noexcept {
    if(!epoch_ns)return {};
    const auto epoch=std::chrono::duration_cast<std::chrono::nanoseconds>(origin_.time_since_epoch()).count();
    if(epoch<0 || *epoch_ns<static_cast<std::uint64_t>(epoch))return {};
    return (*epoch_ns-static_cast<std::uint64_t>(epoch))/1000U;
  }
  void generation(std::uint64_t network,std::uint64_t map,std::string_view identifier) {
    if(!journal_) return;
    const auto normalized=hlclient::app::normalized_session_map_identifier(identifier);
    if(!normalized || !network || !map) return;
    const hlclient::app::SessionReceiptGeneration next{network,map,*normalized};
    if(journal_->snapshot()->generation==next) return;
    multiplayer_.invalidate();
    const bool startup=journal_->snapshot()->generation.network_generation==0U;
    if(!startup && mailbox_) mailbox_->facts.store(0U,std::memory_order_relaxed);
    if(!hlclient::app::begin_published_session_receipt_generation(*journal_,next,
        [&](const auto&) {publish();return !failed_;})) failed_=true;
  }
  void facts(const hlclient::app::SessionReceiptCounters& counters={}) {
    if(!journal_ || failed_) return;
    if(!journal_->observe_facts(mailbox_ ? mailbox_->facts.load(std::memory_order_relaxed) : 0U,counters)) failed_=true;
  }
  void connection_progress() {
    facts();
    milestone(hlclient::app::SessionReceiptStage::connection_established,stamp());
  }
  void close_network_generation(std::uint64_t network) {
    if(journal_ && network && journal_->snapshot()->generation.network_generation==0U) {
      if(!hlclient::app::begin_published_session_receipt_generation(*journal_,{network,0U,"not_observed"},
          [&](const auto&) {publish();return !failed_;})) failed_=true;
      else connection_progress();
    }
  }
  void milestone(hlclient::app::SessionReceiptStage stage,const hlclient::app::SessionReceiptStamp& observed) {
    if(!journal_ || failed_) return;
    const auto index=static_cast<std::size_t>(stage);
    if(index>=1U && index<=hlclient::app::kSessionReceiptMilestoneCount && journal_->snapshot()->milestones[index-1U]) return;
    if(journal_->record(stage,observed)) publish();
  }
  void peer_removed(std::uint8_t entity,std::uint64_t ordinal) {
    if(!journal_ || failed_) return;
    const auto observed=stamp();
    if(!journal_->observe_peer_removal({entity,ordinal,observed.host_qpc_ticks.value_or(0U),observed.process_ms})) failed_=true;
  }
  void receiving_entity(std::uint8_t entity) {
    if(journal_ && !failed_ && !journal_->observe_receiving_entity(entity)) failed_=true;
  }
  void finish(hlclient::app::SessionCloseReceipt close) noexcept {
    if(!journal_ || finished_) return;
    finished_=true;
#ifdef _WIN32
    if(external_) {
      // Optional diagnostic failure never replaces the gameplay close verdict.
      if(movement_.requested()) {
        try {
          auto report=movement_.encode(journal_->snapshot()->identity);
          if(report) {
            const auto last=report->rfind('}');
            const auto metadata=hlclient::app::external_p1_movement_metadata(p1_branch_,
                journal_->snapshot()->identity.run_id,diagnostic_capabilities_,
                hlclient::app::MovementIncidentRecorder::publication_metadata_reserve);
            if(last!=report->npos)report->insert(last,metadata);
          }
          if(!report || !directory_ || !hlclient::platform::windows::secure_atomic_write_new(*directory_,
              L"external-movement.json",std::as_bytes(std::span{report->data(),report->size()})))
            hlclient::core::log(LogLevel::warning,"[movement-diagnostics] publication_failed=true");
        } catch(...) {
          try {hlclient::core::log(LogLevel::warning,"[movement-diagnostics] publication_failed=true");}catch(...){}
        }
      }
      try {
        if(external_diagnostics_.reason=="none" && !external_failure_reason.empty())
          external_diagnostics_.reason=external_failure_reason;
        if(external_diagnostics_.reason=="none" && close.reason==hlclient::app::SessionCloseReason::fatal_failure)
          external_diagnostics_.reason="client_runtime_failure";
        const auto bytes=external_diagnostics_.encode(journal_->snapshot()->identity);
        if(!directory_ || !hlclient::platform::windows::secure_atomic_write_new(*directory_,
            L"external-diagnostics.json",std::as_bytes(std::span{bytes.data(),bytes.size()}))) failed_=true;
        const auto appearance="{\"schema\":\"hlclient.external-player-appearance.v1\",\"run_id\":"+
            hlclient::core::external_json_string(journal_->snapshot()->identity.run_id)+
            ",\"bounded_observations\":["+appearance_observations_+
            "],\"observation_limit\":128,\"limit_reached\":"+(appearance_count_>=128U?"true":"false")+"}\n";
        if(directory_ && !hlclient::platform::windows::secure_atomic_write_new(*directory_,
            L"external-player-appearance.json",std::as_bytes(std::span{appearance.data(),appearance.size()})))
          hlclient::core::log(LogLevel::warning,"[external-render] appearance_receipt_publication_failed=true");
      } catch(...) {failed_=true;}
    }
#endif
    if(failed_) {close.reason=hlclient::app::SessionCloseReason::fatal_failure;close.outcome=hlclient::app::SessionCloseOutcome::fatal_failure;}
    if(!journal_->finish(close)) {failed_=true;return;}
    publish();
  }
  bool failed() const noexcept {return failed_;}
  void observe_reporting_scope(bool post_render,double milliseconds,std::uint64_t frame) noexcept {
    observe_cost(post_render ? render_report_cost_ : receipt_cost_,milliseconds,frame);
  }
  hlclient::app::ExternalSessionDiagnostics& external_diagnostics() noexcept {return external_diagnostics_;}
  const hlclient::app::SessionReceiptSnapshot* snapshot() const noexcept {
    return journal_ ? journal_->snapshot() : nullptr;
  }
  void observe_hud(const hlclient::game_api::HudState& hud) noexcept {
    hud_metrics_={true,hud.hud_resource_tier,hud.hud_canvas_width,hud.hud_canvas_height,
        hud.hud_automatic_scale,hud.hud_user_scale,hud.hud_effective_scale,
        hud.killfeed_font_pixel_size,hud.killfeed_icon_scale,hud.killfeed_maximum_row_width};
  }
  void observe_appearance(const hlclient::app::ReplayLocalVisualSummary& summary,
      std::uint64_t frame) {
    if(!external_ || appearance_count_>=128U)return;
    for(std::size_t i=0;i<summary.remote_player_count && appearance_count_<128U;++i) {
      const auto& player=summary.remote_players[i];
      // Player slot association only; arbitrary world entities cannot fill the journal.
      if(player.entity==0U || player.entity>appearance_keys_.size())continue;
      auto& gate=appearance_keys_[player.entity-1U];
      const auto appearance_key=hlclient::app::external_appearance_key_view(player);
      if(gate.unchanged(appearance_key))continue;
      const ReportingScope timed{*this,appearance_cost_,frame};
      const auto& appearance=player.appearance;
      (void)gate.publish(appearance_key,[&] {
      std::ostringstream row;
      row<<"{\"entity\":"<<player.entity<<",\"modelindex\":"<<player.model_slot
        <<",\"network_generation\":"<<player.network_generation<<",\"map_generation\":"<<player.map_generation
        <<",\"entity_lifetime\":"<<player.entity_lifetime<<",\"occupant_epoch\":"<<appearance.occupant_epoch
        <<",\"appearance_revision\":"<<appearance.appearance_revision
        <<",\"model\":"<<hlclient::core::external_json_string(appearance.fields.model)
        <<",\"requested_visual_model\":"<<hlclient::core::external_json_string(appearance.model_name)
        <<",\"resolved_visual_model\":"<<hlclient::core::external_json_string(player.resolved_visual_model)
        <<",\"status\":"<<hlclient::core::external_json_string(hlclient::game_api::to_string(appearance.status))
        <<",\"topcolor\":"<<(appearance.fields.top_color?std::to_string(*appearance.fields.top_color):"null")
        <<",\"bottomcolor\":"<<(appearance.fields.bottom_color?std::to_string(*appearance.fields.bottom_color):"null")
        <<",\"visual_resource_id\":"<<player.visual_model_resource_id
        <<",\"palette_remapped\":"<<(player.palette_remapped?"true":"false")<<'}';
      const auto bytes=row.str();
      if(appearance_count_++)appearance_observations_+=',';
      appearance_observations_+=bytes;
      return true;
      });
    }
  }
  void external_render(const hlclient::core::CommandLineOptions& options,
      const hlclient::platform::SdlWindow& window,
      const hlclient::renderer::opengl::OpenGlRenderer& renderer,
      std::uint64_t frame) {
    if(!options.external_server_client) return;
    scene_metrics_=renderer.scene_target_statistics();
    const auto image_metrics=renderer.image_adjustment_statistics();
    postprocess_extent_=image_metrics.last_extent;
    const auto* current_session=snapshot();
    p1_branch_.observe(*scene_metrics_,image_metrics,
        current_session?current_session->generation.network_generation:0U,
        current_session?current_session->generation.map_generation:0U);
#ifdef _WIN32
    // Only a completed production draw establishes actual samples/dimensions.
    // Preserve the last valid observation while a resize replaces our exact
    // owned file; never overwrite a caller-supplied or unrelated file.
    const auto& scene=*scene_metrics_;
    const MsaaKey msaa_key{scene.requested_samples,scene.actual_samples,scene.fallback_reason};
    const VideoKey video_key{window.video_state_revision(),scene.requested_samples,
        scene.actual_samples,scene.maximum_samples,scene.framebuffer_complete,
        scene.scene_extent,scene.resolve_extent,postprocess_extent_,scene.viewport,
        scene.fallback_reason,hud_metrics_};
    const bool observe_video=!video_publication_failed_ && video_observations_<16U && !video_gate_.unchanged(video_key);
    const bool observe_msaa=!msaa_publication_failed_ &&
        (!msaa_gate_.unchanged(msaa_key) || msaa_publisher_.retry_pending());
    if(!observe_video && !observe_msaa)return;
    const auto reason=scene.actual_samples==scene.requested_samples ? std::string_view{"none"} :
        hlclient::renderer::opengl::to_string(scene.fallback_reason);
    if(observe_msaa) {
      const ReportingScope timed{*this,msaa_cost_,frame};
      const auto encode_started=Clock::now();
      std::ostringstream report;
      report<<"{\"schema\":\"hlclient.external-msaa.v1\",\"run_id\":"
        <<hlclient::core::external_json_string(options.session_run_id.value_or(""))
        <<",\"requested_samples\":"<<scene.requested_samples<<",\"actual_samples\":"<<scene.actual_samples
        <<",\"fallback_reason\":"<<hlclient::core::external_json_string(reason)<<"}\n";
      const auto bytes=report.str();
      observe_cost(msaa_encode_cost_,std::chrono::duration<double,std::milli>(Clock::now()-encode_started).count(),frame);
      using Publication=hlclient::platform::windows::ExternalMsaaPublicationResult;
      const auto publish_started=Clock::now();
      const auto publication=directory_ ? msaa_publisher_.publish(*directory_,bytes,
          options.session_run_id.value_or(""),Clock::now()) : Publication::failed;
      observe_cost(msaa_publish_cost_,std::chrono::duration<double,std::milli>(Clock::now()-publish_started).count(),frame);
      if(publication==Publication::failed) {
        msaa_publication_failed_=true;
        hlclient::core::log(LogLevel::warning,"[external-render] msaa_receipt_publication_failed=true");
      } else if(publication==Publication::published || publication==Publication::unchanged) {
        (void)msaa_gate_.publish(msaa_key,[]{return true;});
      }
    }
    if(observe_video) {
      (void)video_gate_.publish(video_key,[&] {
        const auto* video=static_cast<const hlclient::platform::VideoWindowState*>(nullptr);
        {
          const ReportingScope timed{*this,metadata_cost_,frame};
          video=&window.cached_video_state();
          metadata_refreshes_=window.video_state_refresh_count();
        }
        const ReportingScope timed{*this,video_cost_,frame};
        const auto leaf=L"external-video-observed-"+std::to_wstring(video_observations_+1U)+L".json";
        ++video_observations_;
        return external_video(options,*video,false,leaf,nullptr,frame);
      });
    }
#else
    (void)window;(void)frame;
#endif
  }
  bool external_video(const hlclient::core::CommandLineOptions& options,
      const hlclient::platform::VideoWindowState& video,bool final,std::wstring_view observation_leaf={},
      const hlclient::app::ConsolePacingStatistics* pacing=nullptr,std::uint64_t frame=0U) {
    if(!options.external_server_client) return true;
#ifdef _WIN32
    const auto encode_started=Clock::now();
    std::ostringstream report;
    report.imbue(std::locale::classic());
    const auto boolean=[](bool value){return value?"true":"false";};
    report<<"{\"schema\":\"hlclient.external-video.v2\",\"run_id\":"
      <<hlclient::core::external_json_string(options.session_run_id.value_or(""))
      <<",\"process_id\":"<<(snapshot()?snapshot()->identity.process_id:0U)
      <<",\"process_creation\":"<<(snapshot()?snapshot()->identity.process_creation:0U)
      <<",\"requested_display\":"<<options.video.display_index
      <<",\"requested_mode\":"<<hlclient::core::external_json_string(hlclient::core::to_string(options.video.mode))
      <<",\"requested_width\":"<<options.video.width<<",\"requested_height\":"<<options.video.height
      <<",\"requested_vsync\":"<<boolean(options.video.vsync)
      <<",\"actual_display\":"<<video.display_index
      <<",\"actual_mode\":"<<hlclient::core::external_json_string(hlclient::core::to_string(video.actual_mode))
      <<",\"window_width\":"<<video.window_extent.width<<",\"window_height\":"<<video.window_extent.height
      <<",\"framebuffer_width\":"<<video.framebuffer_extent.width<<",\"framebuffer_height\":"<<video.framebuffer_extent.height
      <<",\"desktop_width\":"<<video.desktop_extent.width<<",\"desktop_height\":"<<video.desktop_extent.height
      <<",\"display_scale\":"<<video.display_scale<<",\"actual_vsync\":"<<boolean(video.vsync)
      <<",\"actual_display_refresh_hz\":"<<(video.display_refresh_hz?std::to_string(*video.display_refresh_hz):"null")
      <<",\"actual_swap_interval\":"<<(video.actual_swap_interval?std::to_string(*video.actual_swap_interval):"null")
      <<",\"display_timing_semantics\":\"os_current_mode_and_driver_swap_policy_not_scanout_measurement\""
      <<",\"vrr_status\":\"unknown\""
      <<",\"window_x\":"<<video.window_x<<",\"window_y\":"<<video.window_y
      <<",\"display_content_scale\":"<<video.display_content_scale<<",\"window_pixel_density\":"<<video.pixel_density
      <<",\"default_framebuffer_samples_observed\":"<<boolean(video.default_framebuffer_samples_observed)
      <<",\"default_framebuffer_samples\":"<<video.default_framebuffer_samples
      <<",\"default_framebuffer_sample_buffers\":"<<video.default_framebuffer_sample_buffers
      <<",\"requested_msaa_samples\":"<<options.video.msaa_samples
      <<",\"msaa_receipt_attempts\":"<<msaa_publisher_.statistics().attempts
      <<",\"msaa_receipt_publications\":"<<msaa_publisher_.statistics().publications
      <<",\"msaa_receipt_retryable_failures\":"<<msaa_publisher_.statistics().retryable_failures
      <<",\"msaa_receipt_publication_failed\":"<<boolean(msaa_publication_failed_)
      <<",\"video_observation_publication_failed\":"<<boolean(video_publication_failed_)
      <<",\"requested_hud_auto\":"<<boolean(options.hud_scale.automatic)<<",\"requested_hud_scale\":"<<options.hud_scale.scale
      <<",\"safe_window_recovery\":"<<boolean(video.safe_window_recovery)
      <<",\"recovery_reason\":"<<hlclient::core::external_json_string(video.recovery_reason)
      <<",\"brightness\":"<<options.image.brightness<<",\"gamma\":"<<options.image.gamma
      <<",\"contrast\":"<<options.image.contrast<<",\"volume\":"<<options.audio_volume
      <<",\"scene_observed\":"<<boolean(scene_metrics_.has_value())
      <<",\"p1_branch_counters\":"<<p1_branch_.encode(options.session_run_id.value_or(""))
      <<",\"hud_observed\":"<<boolean(hud_metrics_.observed);
    report<<",\"reporting_cost\":{\"scope\":\"this_application_session_inclusive_not_cpu_samples\""
      <<",\"metadata_refreshes\":"<<metadata_refreshes_;
    const auto cost=[&](std::string_view name,const ReportingCost& value) {
      report<<",\""<<name<<"\":{\"observations\":"<<value.count
        <<",\"total_ms\":"<<value.total_ms<<",\"maximum_ms\":"<<value.maximum_ms
        <<",\"maximum_frame\":"<<(value.count?std::to_string(value.maximum_frame):"null")
        <<",\"maximum_network_generation\":"<<value.maximum_network
        <<",\"maximum_map_generation\":"<<value.maximum_map<<'}';
    };
    cost("connection_receipt",receipt_cost_);cost("post_render_report",render_report_cost_);
    cost("window_metadata",metadata_cost_);cost("msaa_encode_publish",msaa_cost_);
    cost("video_encode_write",video_cost_);cost("appearance_encode",appearance_cost_);
    cost("msaa_encode",msaa_encode_cost_);cost("msaa_publish",msaa_publish_cost_);
    cost("video_encode",video_encode_cost_);cost("video_write",video_write_cost_);
    report<<'}';
    if(pacing){
      report<<",\"software_frame_pacing\":{\"profile_enabled\":"<<boolean(pacing->enabled)
        <<",\"counter_scope\":\"live_visual_entry_to_final_video_report_not_previous_attempts_or_idle\""
        <<",\"requested_cap_hz\":"<<pacing->requested_cap_hz
        <<",\"effective_software_cap_hz\":"<<pacing->effective_software_cap_hz
        <<",\"cap_policy\":\"zero_unlimited_existing_frame_start_deadline_does_not_change_physics\""
        <<",\"observations\":"<<pacing->observations<<",\"sleep_calls\":"<<pacing->sleep_calls
        <<",\"last_wait_us\":"<<(pacing->enabled?std::to_string(pacing->last_wait_us):"null")
        <<",\"total_wait_us\":"<<(pacing->enabled?std::to_string(pacing->total_wait_us):"null")
        <<",\"maximum_wait_us\":"<<(pacing->enabled?std::to_string(pacing->maximum_wait_us):"null")<<'}';
    }
    if(scene_metrics_) {
      const auto& scene=*scene_metrics_;
      report<<",\"actual_msaa_samples\":"<<scene.actual_samples<<",\"maximum_msaa_samples\":"<<scene.maximum_samples
        <<",\"msaa_complete\":"<<boolean(scene.framebuffer_complete)
        <<",\"msaa_fallback_reason\":"<<hlclient::core::external_json_string(
            scene.actual_samples==scene.requested_samples ? std::string_view{"none"} :
              hlclient::renderer::opengl::to_string(scene.fallback_reason))
        <<",\"viewport\":["<<scene.viewport[0]<<','<<scene.viewport[1]<<','<<scene.viewport[2]<<','<<scene.viewport[3]<<']'
        <<",\"scene_width\":"<<scene.scene_extent.width<<",\"scene_height\":"<<scene.scene_extent.height
        <<",\"resolve_width\":"<<scene.resolve_extent.width<<",\"resolve_height\":"<<scene.resolve_extent.height
        <<",\"postprocess_width\":"<<postprocess_extent_.width<<",\"postprocess_height\":"<<postprocess_extent_.height
        <<",\"scene_storage_allocations\":"<<scene.storage_allocations<<",\"scene_storage_releases\":"<<scene.storage_releases;
    }
    if(hud_metrics_.observed)report<<",\"hud_canvas_width\":"<<hud_metrics_.width<<",\"hud_canvas_height\":"<<hud_metrics_.height
      <<",\"hud_asset_tier\":"<<hud_metrics_.tier<<",\"hud_automatic_scale\":"<<hud_metrics_.automatic
      <<",\"hud_user_scale\":"<<hud_metrics_.user<<",\"hud_effective_scale\":"<<hud_metrics_.effective
      <<",\"killfeed_font_pixel_size\":"<<hud_metrics_.font<<",\"killfeed_icon_scale\":"<<hud_metrics_.icon
      <<",\"killfeed_maximum_row_width\":"<<hud_metrics_.row_width;
    report<<"}\n";
    const auto bytes=report.str();
    // Startup/final immutable receipts remain synchronous; these cost records
    // describe only completed in-game observational writes, not this final sink.
    if(frame)observe_cost(video_encode_cost_,std::chrono::duration<double,std::milli>(Clock::now()-encode_started).count(),frame);
    const auto write_started=Clock::now();
    const bool written=directory_ && hlclient::platform::windows::secure_atomic_write_new(*directory_,
        observation_leaf.empty() ? (final?L"external-video-final.json":L"external-video-start.json") : observation_leaf,
        std::as_bytes(std::span{bytes.data(),bytes.size()}));
    if(frame)observe_cost(video_write_cost_,std::chrono::duration<double,std::milli>(Clock::now()-write_started).count(),frame);
    if(!written) {
      if(!observation_leaf.empty()) {
        video_publication_failed_=true;
        hlclient::core::log(LogLevel::warning,"[external-render] video_observation_publication_failed=true");
        return false;
      }
      failed_=true;external_status("error","video_receipt_publication_failed");
      throw std::runtime_error{"External video receipt publication failed"};
    }
#else
    (void)video;(void)final;(void)observation_leaf;(void)pacing;(void)frame;
#endif
    return true;
  }
private:
  struct ReportingCost {
    std::uint64_t count{},maximum_frame{},maximum_network{},maximum_map{};
    double total_ms{},maximum_ms{};
    void observe(double ms,std::uint64_t frame,std::uint64_t network,std::uint64_t map) noexcept {
      ++count;total_ms+=ms;
      if(count==1U || ms>maximum_ms){maximum_ms=ms;maximum_frame=frame;maximum_network=network;maximum_map=map;}
    }
  } receipt_cost_,render_report_cost_,metadata_cost_,msaa_cost_,video_cost_,appearance_cost_;
  ReportingCost msaa_encode_cost_,msaa_publish_cost_,video_encode_cost_,video_write_cost_;
  void observe_cost(ReportingCost& cost,double ms,std::uint64_t frame) noexcept {
    const auto* current=snapshot();
    cost.observe(ms,frame,current?current->generation.network_generation:0U,
        current?current->generation.map_generation:0U);
  }
  struct ReportingScope {
    ApplicationSessionReceipts& owner;
    ReportingCost& cost;
    std::uint64_t frame;
    Clock::time_point started{Clock::now()};
    ~ReportingScope(){owner.observe_cost(cost,std::chrono::duration<double,std::milli>(Clock::now()-started).count(),frame);}
  };
  std::uint64_t metadata_refreshes_{};
  void publish() noexcept {
#ifdef _WIN32
    try {
      if(journal_ && directory_ && !hlclient::platform::windows::publish_session_receipt(*directory_,*journal_->snapshot()))
        failed_=true;
    } catch(...) {failed_=true;}
    if(failed_) {
      try {hlclient::core::log(LogLevel::error,"[session-receipt] publication_failed=true");} catch(...) {}
    }
#endif
  }
  Clock::time_point origin_;
  int uncaught_{};
  std::shared_ptr<SessionReceiptFactMailbox> mailbox_;
  std::optional<hlclient::app::SessionReceiptJournal> journal_;
#ifdef _WIN32
  std::optional<hlclient::platform::windows::SecureOutputDirectory> directory_;
  hlclient::platform::windows::ExternalMsaaObservationPublisher msaa_publisher_;
#endif
  using HudMetrics=hlclient::app::ExternalHudObservationMetrics;
  HudMetrics hud_metrics_;
  struct MsaaKey {
    unsigned requested{},actual{};
    hlclient::renderer::opengl::OpenGlMsaaFallback fallback{};
    friend bool operator==(const MsaaKey&,const MsaaKey&)=default;
  };
  struct VideoKey {
    std::uint64_t window_revision{};
    unsigned requested_samples{},actual_samples{},maximum_samples{};
    bool framebuffer_complete{};
    hlclient::renderer::RenderExtent scene{},resolve{},postprocess{};
    std::array<int,4> viewport{};
    hlclient::renderer::opengl::OpenGlMsaaFallback fallback{};
    HudMetrics hud;
    friend bool operator==(const VideoKey&,const VideoKey&)=default;
  };
  std::optional<hlclient::renderer::opengl::OpenGlSceneTargetStatistics> scene_metrics_;
  hlclient::app::ExternalP1BranchObservation p1_branch_;
  hlclient::renderer::RenderExtent postprocess_extent_{};
  hlclient::app::ExternalObservationGate<MsaaKey> msaa_gate_;
  hlclient::app::ExternalObservationGate<VideoKey> video_gate_;
  std::size_t video_observations_{};
  bool msaa_publication_failed_{},video_publication_failed_{};
  std::array<hlclient::app::ExternalObservationGate<hlclient::app::ExternalAppearanceKey>,
      hlclient::game_api::kMaximumRemotePlayers> appearance_keys_;
  std::string appearance_observations_;
  std::size_t appearance_count_{};
  bool failed_{},finished_{};
  bool external_{};
  hlclient::app::MovementIncidentRecorder movement_;
  hlclient::app::MultiplayerPresentationDiagnostics multiplayer_;
  std::string diagnostic_capabilities_;
  std::array<std::uint64_t,static_cast<std::size_t>(hlclient::core::MultiplayerDiagnosticProducer::count)> diagnostic_event_ids_{};
  bool* integrity_failure_{};
  hlclient::app::ExternalSessionDiagnostics external_diagnostics_;
};

hlclient::app::SessionCloseReceipt application_close_receipt(
    const hlclient::goldsrc::GracefulCloseSummary& summary,
    const hlclient::app::SessionCloseReason reason,
    const ApplicationSessionReceipts& receipts,
    const hlclient::app::SessionReceiptStamp& intent,
    const std::optional<hlclient::app::SessionReceiptStamp>& socket_closed,
    const bool socket_error) {
  using O=hlclient::goldsrc::GracefulCloseOutcome;
  using R=hlclient::app::SessionCloseOutcome;
  hlclient::app::SessionCloseReceipt result;
  result.reason=reason;
  switch(summary.outcome) {
    case O::not_requested:result.outcome=R::not_requested;break;
    case O::pending:result.outcome=R::pending;break;
    case O::cancel_without_protocol_disconnect:result.outcome=R::cancel_without_protocol_disconnect;break;
    case O::sent_but_unconfirmed:result.outcome=R::sent_but_unconfirmed;break;
    case O::transport_acknowledged:result.outcome=R::transport_acknowledged;break;
    case O::server_disconnect_confirmed:result.outcome=R::server_disconnect_confirmed;break;
    case O::send_failed:result.outcome=R::send_failed;break;
    case O::cancelled:result.outcome=R::cancelled;break;
    case O::fatal_failure:result.outcome=R::fatal_failure;break;
  }
  result.operation_identity=summary.operation_identity;
  result.session_generation=summary.session_generation;
  result.requested=summary.requested;result.eligible=summary.eligible;result.queued=summary.queued;
  result.send_attempt_count=static_cast<std::uint32_t>(summary.send_attempt_count);
  result.successful_send_count=static_cast<std::uint32_t>(summary.successful_send_count);
  result.support_send_attempt_count=static_cast<std::uint32_t>(summary.support_send_attempt_count);
  result.successful_support_send_count=static_cast<std::uint32_t>(summary.successful_support_send_count);
  result.ack_observed=summary.ack_observed;
  result.server_disconnect_observed=summary.server_disconnect_observed;
  result.deadline_reached=summary.deadline_reached;
  const auto time=[&](const auto& value)->std::optional<std::uint64_t> {
    return value ? std::optional{receipts.milliseconds(*value)} : std::nullopt;
  };
  result.requested_process_ms=time(summary.requested_at);
  result.deadline_process_ms=time(summary.deadline_at);
  result.first_send_process_ms=time(summary.first_send_at);
  result.last_send_process_ms=time(summary.last_send_at);
  if(summary.requested) result.requested_host_qpc_ticks=intent.host_qpc_ticks;
  // Native close() completion, not driver logical cleanup or socket destructor.
  if(socket_closed) {
    result.transport_closed_process_ms=socket_closed->process_ms;
    result.transport_closed_host_qpc_ticks=socket_closed->host_qpc_ticks;
  }
  using E=hlclient::app::SessionCloseTransportError;
  if(summary.transport_error) {
    using D=hlclient::goldsrc::NetchanDriverErrorCode;
    result.transport_error=*summary.transport_error==D::send_failed ? E::send_error :
        *summary.transport_error==D::receive_failed ? E::poll_error : E::invalid_state;
  } else if(summary.would_block_count) result.transport_error=E::would_block;
  if(reason==hlclient::app::SessionCloseReason::fatal_failure) result.outcome=R::fatal_failure;
  if(socket_error) {result.transport_error=E::transport_unavailable;result.outcome=R::fatal_failure;}
  return result;
}

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
  config.texture_import.missing_texture_policy =
      hlclient::goldsrc::MissingWorldTexturePolicy::placeholder_for_absent_name;
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
        << " missing_texture_placeholder_bindings=" << local.missing_texture_placeholder_bindings
        << " size_matches=" << local.size_matches
        << " size_unavailable=" << local.size_unavailable
        << " size_mismatches=" << local.size_mismatches
        << " world_crc_verified=" << local.world_crc_verified
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
  receipt_fact_observed(hlclient::app::SessionReceiptFact::connect_sent);
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
  receipt_authentication_observed(event);
  using AuthStatus=hlclient::app::SteamAuthenticationTraceStatus;
  if(event.status==AuthStatus::operation_started || event.status==AuthStatus::api_initializing ||
     event.status==AuthStatus::api_initialized || event.status==AuthStatus::material_pending ||
     event.status==AuthStatus::material_acquired) external_status("authentication");
  std::string message =
      "[auth] provider=steam api=" +
      std::string{hlclient::app::kSteamLegacyAuthenticationApi} +
      " status=" + std::string{hlclient::app::to_string(event.status)};
  if (event.error) {
    message += " error=" + std::string{hlclient::auth::to_string(*event.error)};
  }
  if (event.phase) {
    message += " phase=" + std::string{hlclient::app::to_string(*event.phase)} +
        " elapsed_ms=" + std::to_string(event.elapsed.count());
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
    receipt_fact_observed(hlclient::app::SessionReceiptFact::steam_api_initialized);
    message += " steam_api_initialized=true";
  }
  if (event.status ==
      hlclient::app::SteamAuthenticationTraceStatus::material_acquired) {
    receipt_fact_observed(hlclient::app::SessionReceiptFact::fresh_material_acquired);
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
    external_status("signon");
    receipt_fact_observed(hlclient::app::SessionReceiptFact::connection_accepted);
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
  if (event.service_message_code) {
    message += ", parser=" + std::string{hlclient::goldsrc::to_string(*event.service_message_code)};
  }
  if (event.stage_error_code) {
    message += ", stage-error=" + std::string{hlclient::goldsrc::to_string(*event.stage_error_code)};
  }
  if (event.service_message_code || event.stage_error_code) {
    if (event.wire_opcode) message += ", failed-opcode=" + std::to_string(*event.wire_opcode);
    if (event.service_error_byte_offset) message += ", error-offset=" + std::to_string(*event.service_error_byte_offset);
    if (event.source_sequence) message += ", source-sequence=" + std::to_string(*event.source_sequence);
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
    receipt_fact_observed(hlclient::app::SessionReceiptFact::serverinfo_received);
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
    receipt_fact_observed(hlclient::app::SessionReceiptFact::schema_registry_received);
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
            " parser_error_byte_offset=" +
            number_or_unavailable(metadata.parser_error_byte_offset) +
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
    receipt_fact_observed(hlclient::app::SessionReceiptFact::resource_continuation_sent);
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

[[nodiscard]] constexpr std::string_view resource_response_body_kind_label(
    const hlclient::goldsrc::NetchanPayloadBodyKind kind) noexcept {
  switch (kind) {
  case hlclient::goldsrc::NetchanPayloadBodyKind::direct_body:
    return "direct_body";
  case hlclient::goldsrc::NetchanPayloadBodyKind::normal_fragment_completion:
    return "normal_fragment_completion";
  case hlclient::goldsrc::NetchanPayloadBodyKind::contemporaneous_fragment_suffix:
    return "contemporaneous_fragment_suffix";
  }
  return "unavailable";
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
            " source_body_kind=" +
            std::string{resource_response_body_kind_label(value.source_body_kind)} +
            " source_reliable=" + std::to_string(value.source_reliable) +
            " reassembled=" + std::to_string(value.reassembled) +
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
  case Classification::post_transmit_control_consumed:
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
  case Type::committed_effect_observed:
    if(metadata.effect_diagnostic) {
      const auto& d=*metadata.effect_diagnostic;
      std::ostringstream row;
      row<<"[effect-wire] record="<<metadata.payload_ordinal<<" bit="<<metadata.message_bit_offset
         <<" svc="<<d.opcode<<" detail="<<d.detail<<" color="<<d.color
         <<" amount="<<d.amount<<" entity="<<d.entity<<" flags="<<d.flags;
      hlclient::core::log(LogLevel::info,row.str());
    }
    return;
  case Type::resource_response_ready:
    return;
  case Type::spawn_request_queued:
    hlclient::core::log(LogLevel::info,
                        "[session-progress] spawn_queued=succeeded");
    return;
  case Type::spawn_request_transmitted:
    receipt_fact_observed(hlclient::app::SessionReceiptFact::spawn_request_transmitted);
    hlclient::core::log(LogLevel::info,
                        "[live-runtime] spawn_request_transmitted=true");
    hlclient::core::log(LogLevel::info,
                        "[session-progress] spawn_transmitted=succeeded");
    return;
  case Type::spawn_request_acknowledged:
    receipt_fact_observed(hlclient::app::SessionReceiptFact::spawn_request_acknowledged);
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
    receipt_fact_observed(hlclient::app::SessionReceiptFact::signon_reply_transmitted);
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] signon_reply=sendents transmitted=true");
    return;
  case Type::signon_reply_acknowledged:
    receipt_fact_observed(hlclient::app::SessionReceiptFact::signon_reply_acknowledged);
    hlclient::core::log(
        LogLevel::info,
        "[live-runtime] signon_reply=sendents acknowledged=true");
    return;
  case Type::service_payload_received:
    receipt_fact_observed(hlclient::app::SessionReceiptFact::live_service_payloads_received);
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
                        "[session-progress] live_usercmd_transmission=active",
                        hlclient::core::LogChannel::diagnostic);
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
              " server_samples=" + std::to_string(check.server_sample_count) +
              " retained_server_samples=" + std::to_string(check.server_samples.size()) +
              " server_samples_evicted=" + std::to_string(check.server_samples_evicted) +
              " transmit_ranges_evicted=" + std::to_string(check.transmit_ranges_evicted));
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
        native += " response_first_tx_sequence=" +
                  (value.first_transmit_sequence
                       ? std::to_string(*value.first_transmit_sequence)
                       : std::string{"unavailable"});
        native += " response_source_body_kind=" +
                  std::string{resource_response_body_kind_label(value.source_body_kind)};
        native += " response_source_reliable=" + std::to_string(value.source_reliable);
        native += " response_reassembled=" + std::to_string(value.reassembled);
        native += " response_wire_size=" + std::to_string(value.wire_byte_count);
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
  hlclient::core::log(LogLevel::info,
      "[texture] placeholder-bindings=" + std::to_string(statistics.placeholder_material_count));
  return textures.renderable_for_world_materials() ? 0 : 1;
}

[[nodiscard]] int report_world_render_package(
    const hlclient::world_render::WorldRenderPackage &package) {
  const auto &statistics = package.statistics();
  const bool valid =
      package.textured_world().textures.renderable_for_world_materials() &&
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

  const auto local_address=hlclient::app::client_bind_endpoint(remote_endpoint);
  if (!socket->bind(local_address, error)) {
    throw std::runtime_error{
        error.empty() ? "Unable to bind the challenge UDP socket" : error};
  }
  return std::move(*socket);
}

// Local, endpoint/root-bound opt-in; server packets cannot grant filesystem
// access. Missing, malformed or unrelated permission records keep asset-only
// consistency checks. Re-evaluate for every connection, including reconnects.
[[nodiscard]] bool installation_file_checks_approved(
    const hlclient::core::CommandLineOptions& options) {
#ifdef _WIN32
  if (!options.connect_endpoint || !options.base_directory) return false;
  try {
    const auto settings = hlclient::platform::windows::default_external_settings_path();
    if (!settings) return false;
    const auto text = hlclient::platform::windows::read_external_text(
        settings->parent_path() / L"installation-file-checks.json", 4096U);
    return text && hlclient::core::approved_installation_file_checks(
        *text, *options.connect_endpoint, *options.base_directory,
        options.game_directory);
  } catch (...) {
    return false;
  }
#else
  static_cast<void>(options);
  return false;
#endif
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
      const bool net_trace,
      std::shared_ptr<hlclient::game_api::GameClientHost> game_client,
      std::shared_ptr<hlclient::goldsrc::CommittedSoundQueue> sound_events,
      const bool external_mode=false,const bool movement_diagnostics=false,
      ApplicationSessionReceipts* diagnostic_receipts=nullptr,
      const bool approved_installation_checks=false,
      const hlclient::goldsrc::ConnectResponseWaitConfig response_config={})
      : local_resource_environment_{std::move(local_resource_environment)},
        authentication_provider_{std::move(authentication_provider)},
        network_runtime_{},
        transport_{open_challenge_socket(network_runtime_, remote_endpoint)},
        endpoint_transport_{transport_,external_mode ? std::optional{remote_endpoint} : std::nullopt},
        handshake_{
            endpoint_transport_,
            remote_endpoint,
            stop_point,
            std::move(prepared_request),
            {},
            receipt_aware_trace<hlclient::goldsrc::ChallengeTraceCallback>(net_trace,&log_challenge_trace,
                [](const auto& event) {
                  using C=hlclient::goldsrc::ChallengeTraceClassification;
                  if(event.classification==C::request_sent || event.classification==C::challenge_accepted)
                    receipt_authentication_observed(event);}),
            receipt_aware_trace<hlclient::goldsrc::ConnectRequestTraceCallback>(net_trace,&log_connect_trace,
                [](const auto& event) {receipt_authentication_observed(event);
                  if(event.state==hlclient::goldsrc::ConnectRequestStageState::request_sent)
                  receipt_fact_observed(hlclient::app::SessionReceiptFact::connect_sent);}),
            response_config,
            receipt_aware_trace<hlclient::goldsrc::ConnectResponseTraceCallback>(net_trace,&log_connect_response_trace,
                [](const auto& event) {
                  if(event.classification!=hlclient::goldsrc::ConnectResponseTraceClassification::receive_would_block)
                    receipt_authentication_observed(event);
                  if(event.classification==hlclient::goldsrc::ConnectResponseTraceClassification::connect_accepted)
                  receipt_fact_observed(hlclient::app::SessionReceiptFact::connection_accepted);}),
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
            receipt_aware_trace<hlclient::goldsrc::InitialSignonTraceCallback>(net_trace,&log_initial_signon_trace,
                [diagnostic_receipts](const auto& event) {
                  if(diagnostic_receipts && event.service_message_code)
                    diagnostic_receipts->external_diagnostics().initial_service_failure(
                        hlclient::goldsrc::to_string(*event.service_message_code),
                        event.service_error_byte_offset,event.wire_opcode,event.source_sequence);
                  else if(diagnostic_receipts && event.stage_error_code==
                      hlclient::goldsrc::InitialSignonErrorCode::service_event_backpressure)
                    diagnostic_receipts->external_diagnostics().initial_capacity_failure(
                        hlclient::goldsrc::to_string(*event.stage_error_code),
                        event.service_error_byte_offset,event.wire_opcode,event.source_sequence);
                }),
            {},
            receipt_aware_trace<hlclient::goldsrc::PreResourceSignonTraceCallback>(net_trace,&log_pre_resource_signon_trace,
                [](const auto& event) {if(event.classification==hlclient::goldsrc::PreResourceSignonTraceClassification::server_info_ready)
                  receipt_fact_observed(hlclient::app::SessionReceiptFact::serverinfo_received);}),
            {},
            receipt_aware_trace<hlclient::goldsrc::DeltaDescriptionTraceCallback>(net_trace,&log_delta_description_trace,
                [](const auto& event) {if(event.classification==hlclient::goldsrc::DeltaDescriptionTraceClassification::delta_registry_ready)
                  receipt_fact_observed(hlclient::app::SessionReceiptFact::schema_registry_received);}),
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
            receipt_aware_trace<hlclient::goldsrc::ResourceTransitionTraceCallback>(net_trace,&log_resource_transition_trace,
                [](const auto& event) {if(event.classification==hlclient::goldsrc::ResourceTransitionTraceClassification::transition_request_transmitted)
                  receipt_fact_observed(hlclient::app::SessionReceiptFact::resource_continuation_sent);}),
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
              if (approved_installation_checks)
                config.file_consistency_read_policy = hlclient::goldsrc::
                    FileConsistencyReadPolicy::approved_installation_files;
              config.game_client = game_client;
              if(external_mode) config.required_game_directory="valve";
              if(live_visual_input_source==hlclient::goldsrc::LiveVisualControlInputSource::keyboard_mouse)
                config.sound_events = sound_events;
              config.operation_mode = live_runtime_mode;
              config.live_visual_input_source = live_visual_input_source;
              config.reference_prediction = reference_prediction;
              config.effect_diagnostics = net_trace;
              config.movement_diagnostics = movement_diagnostics;
              if(movement_diagnostics && diagnostic_receipts) {
                config.multiplayer_diagnostics=diagnostic_receipts->diagnostic_sink();
                game_client->set_diagnostic_sink(diagnostic_receipts->diagnostic_sink());
                diagnostic_receipts->diagnostic_capabilities(*game_client);
              }
              if(movement_diagnostics && diagnostic_receipts)
                config.movement_diagnostic_generations=[diagnostic_receipts] {
                  const auto identity=diagnostic_receipts->snapshot();
                  return identity ? std::array{identity->generation.network_generation,identity->generation.map_generation}
                      : std::array<std::uint64_t,2>{};
                };
              if(movement_diagnostics && diagnostic_receipts)
                game_client->set_diagnostic_generations(config.movement_diagnostic_generations);
              if(movement_diagnostics && diagnostic_receipts)
                config.movement_correction_sink=[diagnostic_receipts](const hlclient::core::MovementCorrectionWitness& input) {
                  auto value=input;
                  value.observed_us=diagnostic_receipts->movement_stamp_ns(value.observed_ns);
                  (void)diagnostic_receipts->movement().observe_correction(value);
                };
              if (live_visual_input_source == hlclient::goldsrc::
                      LiveVisualControlInputSource::scripted_jump_duck_check) {
                config.usercmd_scenario.durations = {
                    std::chrono::seconds{2}, std::chrono::milliseconds{1200},
                    std::chrono::seconds{2}, std::chrono::milliseconds{1500},
                    std::chrono::milliseconds{2500}};
              } else if (live_visual_input_source == hlclient::goldsrc::
                             LiveVisualControlInputSource::scripted_speed_check ||
                         live_visual_input_source == hlclient::goldsrc::
                             LiveVisualControlInputSource::scripted_weapon_check) {
                config.usercmd_scenario.forward_amplitude = static_cast<std::int16_t>(
                    game_client->movement_policy().movement_speeds.forward_speed);
                config.usercmd_scenario.durations = {
                    std::chrono::seconds{2}, std::chrono::milliseconds{600},
                    std::chrono::seconds{1}, std::chrono::seconds{1},
                    std::chrono::seconds{1}};
              } else if (live_visual_input_source == hlclient::goldsrc::
                             LiveVisualControlInputSource::scripted_fire_reload_check) {
                config.usercmd_scenario.durations = {
                    std::chrono::milliseconds{1200},
                    std::chrono::milliseconds{1500},
                    std::chrono::milliseconds{2800},
                    std::chrono::milliseconds{1000},
                    std::chrono::milliseconds{500}};
              } else if (live_visual_input_source == hlclient::goldsrc::
                             LiveVisualControlInputSource::scripted_fire_reload_presentation_check) {
                config.usercmd_scenario.durations = {
                    std::chrono::milliseconds{1500}, std::chrono::seconds{3},
                    std::chrono::milliseconds{3500}, std::chrono::seconds{2},
                    std::chrono::seconds{1}};
              }
              if (live_runtime_mode == hlclient::goldsrc::
                                           LiveRuntimeOperationMode::
                                               live_visual_control) {
                config.timeout = std::chrono::seconds{60};
                if (live_visual_input_source == hlclient::goldsrc::
                    LiveVisualControlInputSource::scripted_fire_reload_presentation_check)
                  config.timeout = std::chrono::seconds{90};
                if (live_visual_input_source == hlclient::goldsrc::
                    LiveVisualControlInputSource::scripted_damage_respawn_check) {
                  config.timeout = std::chrono::seconds{90};
                  config.usercmd_scenario.durations.fill(std::chrono::seconds{8});
                }
              }
              return config;
            }(),
            hlclient::goldsrc::LiveRuntimeStageTraceCallback{
                &log_live_runtime_trace}} {
    hlclient::core::log(LogLevel::info, "GoldSrc challenge exchange initialized");
    hlclient::core::log(LogLevel::info,
                        "Server: " + remote_endpoint.to_string());

  }

  void update(const hlclient::goldsrc::ChallengeExchangeTimePoint now) {
    // Construction may precede slow SDL/GL/audio initialization. Start the
    // protocol clock only when its owning loop can actually pump RX/TX.
    if (handshake_.state() == hlclient::goldsrc::GoldSrcHandshakeState::idle) {
      hlclient::core::log(LogLevel::info, "GoldSrc challenge exchange started");
      static_cast<void>(handshake_.start(now));
      if (handshake_.local_endpoint()) {
        hlclient::core::log(LogLevel::info,
                            "Local endpoint: " +
                                handshake_.local_endpoint()->to_string());
      }
    }
    handshake_.update(now);
  }

  void cancel(const hlclient::goldsrc::ChallengeExchangeTimePoint now) {
    handshake_.cancel(now);
  }
  bool request_graceful_close(const hlclient::goldsrc::ChallengeExchangeTimePoint now) {
    return handshake_.request_graceful_close(now);
  }
  hlclient::goldsrc::GracefulCloseSummary graceful_close_snapshot() const noexcept {
    return handshake_.graceful_close_snapshot();
  }
  hlclient::network::SocketNativeError close_transport() noexcept {
    return transport_.close();
  }

  [[nodiscard]] bool terminal() const noexcept { return handshake_.terminal(); }
  [[nodiscard]] bool connect_response_timed_out() const noexcept {
    return handshake_.state() ==
        hlclient::goldsrc::GoldSrcHandshakeState::connect_response_timed_out;
  }
  [[nodiscard]] std::string_view handshake_state() const noexcept {
    using State = hlclient::goldsrc::GoldSrcHandshakeState;
    switch (handshake_.state()) {
    case State::idle: return "idle";
    case State::waiting_for_challenge: return "waiting_for_challenge";
    case State::challenge_received: return "challenge_received";
    case State::waiting_for_authentication: return "waiting_for_authentication";
    case State::authentication_failed: return "authentication_failed";
    case State::authentication_timed_out: return "authentication_timed_out";
    case State::building_request: return "building_request";
    case State::request_ready: return "request_ready";
    case State::sending_request: return "sending_request";
    case State::request_sent: return "request_sent";
    case State::waiting_for_connect_response: return "waiting_for_connect_response";
    case State::accepted: return "accepted";
    case State::rejected: return "rejected";
    case State::connect_response_timed_out: return "connect_response_timed_out";
    case State::waiting_for_netchan: return "waiting_for_netchan";
    case State::netchan_bootstrap_complete: return "netchan_bootstrap_complete";
    case State::netchan_timed_out: return "netchan_timed_out";
    case State::waiting_for_signon: return "waiting_for_signon";
    case State::signon_boundary_reached: return "signon_boundary_reached";
    case State::signon_timed_out: return "signon_timed_out";
    case State::signon_unsupported_service: return "signon_unsupported_service";
    case State::signon_backpressure: return "signon_backpressure";
    case State::signon_secondary_stream_pending_m3: return "signon_secondary_stream_pending_m3";
    case State::waiting_for_pre_resource: return "waiting_for_pre_resource";
    case State::pre_resource_boundary_reached: return "pre_resource_boundary_reached";
    case State::pre_resource_timed_out: return "pre_resource_timed_out";
    case State::pre_resource_unsupported_message: return "pre_resource_unsupported_message";
    case State::pre_resource_backpressure: return "pre_resource_backpressure";
    case State::pre_resource_secondary_stream_pending_m3: return "pre_resource_secondary_stream_pending_m3";
    case State::waiting_for_delta_schemas: return "waiting_for_delta_schemas";
    case State::delta_schemas_ready: return "delta_schemas_ready";
    case State::delta_timed_out: return "delta_timed_out";
    case State::delta_unsupported_message: return "delta_unsupported_message";
    case State::delta_backpressure: return "delta_backpressure";
    case State::delta_secondary_stream_pending_m3: return "delta_secondary_stream_pending_m3";
    case State::waiting_for_movevars: return "waiting_for_movevars";
    case State::movement_environment_boundary_reached: return "movement_environment_boundary_reached";
    case State::movevars_timed_out: return "movevars_timed_out";
    case State::movevars_unsupported_message: return "movevars_unsupported_message";
    case State::movevars_backpressure: return "movevars_backpressure";
    case State::movevars_secondary_stream_pending_m3: return "movevars_secondary_stream_pending_m3";
    case State::waiting_for_user_info: return "waiting_for_user_info";
    case State::user_info_complete: return "user_info_complete";
    case State::user_info_timed_out: return "user_info_timed_out";
    case State::user_info_unsupported_message: return "user_info_unsupported_message";
    case State::user_info_backpressure: return "user_info_backpressure";
    case State::user_info_secondary_stream_pending: return "user_info_secondary_stream_pending";
    case State::waiting_for_resource_transition: return "waiting_for_resource_transition";
    case State::resource_transition_boundary_reached: return "resource_transition_boundary_reached";
    case State::resource_transition_timed_out: return "resource_transition_timed_out";
    case State::resource_transition_unsupported_message: return "resource_transition_unsupported_message";
    case State::resource_transition_backpressure: return "resource_transition_backpressure";
    case State::resource_transition_secondary_stream_pending: return "resource_transition_secondary_stream_pending";
    case State::waiting_for_resource_list: return "waiting_for_resource_list";
    case State::resource_list_client_response_required: return "resource_list_client_response_required";
    case State::resource_list_unsupported_profile: return "resource_list_unsupported_profile";
    case State::resource_list_timed_out: return "resource_list_timed_out";
    case State::resource_list_backpressure: return "resource_list_backpressure";
    case State::resource_list_secondary_stream_pending: return "resource_list_secondary_stream_pending";
    case State::waiting_for_resource_response: return "waiting_for_resource_response";
    case State::resource_response_boundary_reached: return "resource_response_boundary_reached";
    case State::resource_response_provider_required: return "resource_response_provider_required";
    case State::resource_response_unsupported_profile: return "resource_response_unsupported_profile";
    case State::resource_response_timed_out: return "resource_response_timed_out";
    case State::resource_response_backpressure: return "resource_response_backpressure";
    case State::resource_response_secondary_stream_pending: return "resource_response_secondary_stream_pending";
    case State::waiting_for_post_resource_entity_snapshot: return "waiting_for_post_resource_entity_snapshot";
    case State::server_baselines_ready: return "server_baselines_ready";
    case State::entity_snapshot_ready: return "entity_snapshot_ready";
    case State::post_resource_unsupported_message: return "post_resource_unsupported_message";
    case State::post_resource_missing_delta_base: return "post_resource_missing_delta_base";
    case State::post_resource_timed_out: return "post_resource_timed_out";
    case State::post_resource_backpressure: return "post_resource_backpressure";
    case State::post_resource_secondary_stream_pending: return "post_resource_secondary_stream_pending";
    case State::waiting_for_live_runtime_state: return "waiting_for_live_runtime_state";
    case State::live_runtime_state_ready: return "live_runtime_state_ready";
    case State::live_runtime_timed_out: return "live_runtime_timed_out";
    case State::live_runtime_backpressure: return "live_runtime_backpressure";
    case State::live_runtime_secondary_stream_pending: return "live_runtime_secondary_stream_pending";
    case State::waiting_for_precache_manifest: return "waiting_for_precache_manifest";
    case State::precache_manifest_ready: return "precache_manifest_ready";
    case State::local_resources_incomplete: return "local_resources_incomplete";
    case State::unsafe_local_resources: return "unsafe_local_resources";
    case State::unsupported_local_profile: return "unsupported_local_profile";
    case State::local_resource_io_error: return "local_resource_io_error";
    case State::waiting_for_asset_dispatch: return "waiting_for_asset_dispatch";
    case State::asset_imported: return "asset_imported";
    case State::importer_boundary_reached: return "importer_boundary_reached";
    case State::world_source_unavailable: return "world_source_unavailable";
    case State::asset_source_open_failed: return "asset_source_open_failed";
    case State::ambiguous_asset_importer: return "ambiguous_asset_importer";
    case State::asset_import_failed: return "asset_import_failed";
    case State::asset_dispatch_timed_out: return "asset_dispatch_timed_out";
    case State::asset_dispatch_backpressure: return "asset_dispatch_backpressure";
    case State::waiting_for_world_textures: return "waiting_for_world_textures";
    case State::world_textures_ready: return "world_textures_ready";
    case State::world_textures_incomplete: return "world_textures_incomplete";
    case State::world_texture_geometry_unavailable: return "world_texture_geometry_unavailable";
    case State::world_texture_worldspawn_parse_failed: return "world_texture_worldspawn_parse_failed";
    case State::world_texture_wad_reference_invalid: return "world_texture_wad_reference_invalid";
    case State::world_texture_wad_source_unavailable: return "world_texture_wad_source_unavailable";
    case State::world_texture_wad_source_open_failed: return "world_texture_wad_source_open_failed";
    case State::world_texture_wad_catalog_failed: return "world_texture_wad_catalog_failed";
    case State::world_texture_decode_failed: return "world_texture_decode_failed";
    case State::world_texture_timed_out: return "world_texture_timed_out";
    case State::world_texture_backpressure: return "world_texture_backpressure";
    case State::waiting_for_world_render_package: return "waiting_for_world_render_package";
    case State::world_render_package_ready: return "world_render_package_ready";
    case State::world_render_textures_incomplete: return "world_render_textures_incomplete";
    case State::world_render_lightmap_import_failed: return "world_render_lightmap_import_failed";
    case State::world_render_package_failed: return "world_render_package_failed";
    case State::world_render_timed_out: return "world_render_timed_out";
    case State::world_render_backpressure: return "world_render_backpressure";
    case State::timed_out: return "timed_out";
    case State::cancelled: return "cancelled";
    case State::configuration_error: return "configuration_error";
    case State::network_error: return "network_error";
    case State::protocol_error: return "protocol_error";
    }
    return "unknown_handshake_state";
  }

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

  [[nodiscard]] bool request_weapon_selection(const std::uint8_t weapon_id) {
    return handshake_.request_weapon_selection(weapon_id);
  }
  [[nodiscard]] bool request_console_command(const hlclient::goldsrc::ConsoleServerCommandRequest& request) {
    return handshake_.request_console_command(request);
  }

  [[nodiscard]] std::optional<hlclient::goldsrc::LiveUserCmdCheckState>
  live_usercmd_snapshot() const {
    return handshake_.live_usercmd_snapshot();
  }
  [[nodiscard]] std::optional<hlclient::goldsrc::LiveUserCmdReceiptCounters>
  live_usercmd_receipt_counters() const noexcept {
    return handshake_.live_usercmd_receipt_counters();
  }
  [[nodiscard]] std::size_t live_use_new_submission_count() const noexcept {
    return handshake_.live_use_new_submission_count();
  }

  [[nodiscard]] std::optional<hlclient::goldsrc::LiveWeaponCommandSubmission>
  poll_weapon_command_submission() {
    return handshake_.poll_weapon_command_submission();
  }

  [[nodiscard]] bool attach_reference_prediction_collision(
      std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package) {
    return handshake_.attach_reference_prediction_collision(std::move(package));
  }
  [[nodiscard]] bool attach_reference_prediction_surfaces(
      std::shared_ptr<const hlclient::world_scene_render::WorldSceneRenderPackage> package) {
    return handshake_.attach_reference_prediction_surfaces(std::move(package));
  }

  [[nodiscard]] hlclient::goldsrc::LiveReferencePredictionSnapshot
  live_reference_prediction_snapshot(const hlclient::goldsrc::ChallengeExchangeTimePoint now) const {
    return handshake_.live_reference_prediction_snapshot(now);
  }
  [[nodiscard]] hlclient::goldsrc::LiveRuntimeMovementDiagnostics movement_diagnostics_snapshot() const noexcept {
    return handshake_.movement_diagnostics_snapshot();
  }

  [[nodiscard]] const std::optional<hlclient::goldsrc::LiveRuntimeState> &
  live_runtime_result() const noexcept {
    return handshake_.live_runtime_result();
  }

  [[nodiscard]] const std::optional<hlclient::goldsrc::LiveRuntimeStageError> &
  live_runtime_error() const noexcept {
    return handshake_.live_runtime_error();
  }

  [[nodiscard]] std::optional<hlclient::auth::AuthenticationErrorCode>
  authentication_error_code() const noexcept {
    const auto& error=handshake_.authentication_error();
    return error ? std::optional{error->code} : std::nullopt;
  }
  [[nodiscard]] std::optional<std::string_view> external_rejection_reason() const {
    if(const auto& response=handshake_.connect_response();response)
      if(const auto* rejected=std::get_if<hlclient::goldsrc::ConnectRejected>(&*response))
        return hlclient::app::external_rejection_category(rejected->message);
    return std::nullopt;
  }
  [[nodiscard]] std::optional<std::string_view> server_rejection_text() const noexcept {
    if(const auto& response=handshake_.connect_response();response)
      if(const auto* rejected=std::get_if<hlclient::goldsrc::ConnectRejected>(&*response))return rejected->message;
    return std::nullopt;
  }
  [[nodiscard]] bool external_early_failure(hlclient::app::ExternalSessionDiagnostics& diagnostic) const {
    const auto error=handshake_.early_failure_diagnostic();
    if(!error)return false;
    diagnostic.early_failure(error->phase,error->category,error->code,error->byte_offset,error->field);
    return true;
  }

  [[nodiscard]] const hlclient::goldsrc::ResourceListState *
  live_resource_list() const noexcept {
    return handshake_.live_resource_list();
  }

  [[nodiscard]] const hlclient::goldsrc::ServerInfoState *
  live_server_info() const noexcept {
    return handshake_.live_server_info();
  }
  [[nodiscard]] std::string_view live_sky_name() const noexcept {
    return handshake_.live_sky_name();
  }

private:
  std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
      local_resource_environment_;
  std::unique_ptr<hlclient::auth::IAuthenticationProvider>
      authentication_provider_;
  hlclient::network::NetworkRuntime network_runtime_;
  hlclient::network::UdpDatagramTransport transport_;
  hlclient::app::EndpointBoundTransport endpoint_transport_;
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
    const hlclient::network::NetworkAddress &remote_endpoint,
    hlclient::app::ConsoleApplication* console = nullptr) {
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
        console ? hlclient::app::make_console_steam_authentication_provider(
            std::move(config),[console]{console->pump_idle();})
            : std::unique_ptr<hlclient::auth::IAuthenticationProvider>{
                std::make_unique<hlclient::app::SteamAuthenticationProvider>(std::move(config))},
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
    const hlclient::core::CommandLineOptions &options,
    const std::shared_ptr<hlclient::game_api::GameClientHost>& game_client,
    const std::shared_ptr<hlclient::goldsrc::CommittedSoundQueue>& sound_events,
    ApplicationSessionReceipts& receipts,
    hlclient::app::ConsoleApplication* console = nullptr,
    bool* integrity_failure = nullptr) {
  using Clock = std::chrono::steady_clock;
  using InputMode = hlclient::core::LiveInputMode;
  const bool keyboard = options.live_input == InputMode::keyboard_mouse;
  if(keyboard) receipt_fact_observed(hlclient::app::SessionReceiptFact::keyboard_mouse);
  const bool damage_respawn_check = options.live_input == InputMode::scripted_damage_respawn_check;
  // Full-frame readbacks and same-scene A/B rerenders are verification work.
  // Keep them out of the active weapon-check and interactive command loops.
  const bool side_check =
      options.live_input == InputMode::scripted_side_check;
  const bool jump_duck_check =
      options.live_input == InputMode::scripted_jump_duck_check;
  const bool weapon_check =
      options.live_input == InputMode::scripted_weapon_check;
  const bool presentation_check =
      options.live_input == InputMode::scripted_fire_reload_presentation_check;
  const bool fire_reload_check =
      options.live_input == InputMode::scripted_fire_reload_check || presentation_check;
  const bool speed_check =
      options.live_input == InputMode::scripted_speed_check || weapon_check;
  if (!game_client) throw std::runtime_error{"Live game module is unavailable"};
  const auto live_button_mask = game_client->movement_policy().live_buttons;
  const auto live_held_button_mask = game_client->movement_policy().held_buttons;

  // Scripted proofs retain their existing single-session ownership. Normal
  // keyboard play uses the application window/context across every attempt.
  std::unique_ptr<hlclient::platform::SdlRuntime> owned_runtime;
  std::unique_ptr<hlclient::platform::SdlWindow> owned_window;
  std::unique_ptr<hlclient::renderer::opengl::OpenGlRenderer> owned_renderer;
  if(!console) {
    owned_runtime=std::make_unique<hlclient::platform::SdlRuntime>();
    owned_window=std::make_unique<hlclient::platform::SdlWindow>(hlclient::platform::SdlWindowConfig{
      std::string{hlclient::core::kApplicationName}+
          " - "+options.player_name, options.video.width, options.video.height, false,
          options.video.display_index,options.video.mode,options.video.vsync,options.video.msaa_samples});
    owned_renderer=std::make_unique<hlclient::renderer::opengl::OpenGlRenderer>();
  }
  auto& window=console ? console->window() : *owned_window;
  auto& renderer=console ? console->renderer() : *owned_renderer;
  if(console)console->reset_pacing_statistics();
  const auto initial_pacing=console?std::optional{console->pacing_statistics()}:std::optional<hlclient::app::ConsolePacingStatistics>{};
  receipts.external_video(options,window.video_state(),false,{},initial_pacing?&*initial_pacing:nullptr);
  hlclient::input::InputStateTracker input_tracker;
  hlclient::audio::Playback audio_output{keyboard && (console || options.audio_volume>0)};
  hlclient::goldsrc::ServerAudio server_audio{audio_output};
  hlclient::goldsrc::LocalAudio local_audio{audio_output};
  hlclient::goldsrc::TemporaryAudio temporary_audio{audio_output};
  hlclient::goldsrc::LocalAudio remote_audio{audio_output,true};
  hlclient::app::RemoteEffectPresentation remote_effects;
  auto temporary_effect_owner=std::make_unique<hlclient::app::TemporaryEffectPresentation>();
  auto& temporary_effects=*temporary_effect_owner;
  hlclient::game_api::TemporaryEffectStatistics temporary_effect_stats;
  std::uint64_t server_decal_resource_generation{};
  std::array<std::uint64_t,8> server_decal_asset_status_counts{};
  std::array<std::uint64_t,11> server_decal_placement_counts{};
  std::uint64_t server_decal_evictions{};
  std::size_t temporary_effect_trace_budget=16;
  std::uint64_t temporary_effect_trace_revision{};
  std::uint64_t blood_draw_batches{};
  std::uint64_t large_yellow_blood_draw_batches{};
  std::uint64_t blood_rejected_batches{};
  std::size_t entity_effect_trace_budget=16;
  std::uint64_t entity_effect_trace_revision{};
  hlclient::game_api::RemoteWeaponEffectStatistics remote_effect_stats;
  std::size_t remote_effect_trace_budget=16;
  std::size_t remote_socket_trace_budget=64;
  std::uint64_t remote_effect_trace_revision{};
  std::uint64_t remote_effect_resource_generation{};
  hlclient::game_api::LocalAudioStatistics local_audio_stats;
  std::unique_ptr<hlclient::goldsrc::ApprovedSoundAssets> sound_assets;
  std::unique_ptr<hlclient::goldsrc::ApprovedHudAssets> hud_assets;
  std::shared_ptr<const hlclient::goldsrc::HudPreparedAssets> prepared_hud_assets;
  std::uint64_t hud_binding_reset_serial{};
  std::vector<hlclient::goldsrc::SoundSource> audio_sources;
  audio_sources.reserve(8192);
  std::size_t server_audio_trace_budget=32;
  std::array<bool,512> effect_sample_reported{};
  std::size_t remote_visual_trace_budget=32;
  hlclient::app::RemotePlayerVisibilityJournal remote_visibility_journal;
  std::uint64_t server_audio_trace_revision{};
  if (!game_client || !game_client->movement_policy().bindings) {
    throw std::runtime_error{"Live visual input bindings are unavailable"};
  }
  const auto& bindings = game_client->movement_policy().bindings;
  hlclient::app::LiveVisualCameraController camera_controller;
  std::uint64_t diagnostic_hud_deaths{};
  std::optional<int> previous_hud_health, previous_hud_armor, pre_life_hud_health, pre_life_hud_armor;
  std::optional<hlclient::client::RuntimeObservationSource> previous_hud_health_source,
      previous_hud_armor_source, pre_life_hud_health_source, pre_life_hud_armor_source;
  std::size_t post_respawn_rendered_frames{};

  std::optional<std::future<hlclient::app::ReplayLocalAssetsCreateResult>>
      asset_future;
  auto loading_work=std::make_shared<hlclient::app::LoadingWork>(console ? console->generation() : 0U);
  // Detach all closures before their session references expire, including
  // exception paths. Cancellation is signalled before future destruction.
  struct ConsoleAttemptGuard {
    hlclient::app::ConsoleApplication* application;
    std::shared_ptr<hlclient::app::LoadingWork> work;
    ~ConsoleAttemptGuard(){work->cancelled.store(true);if(application)application->set_session_actions({});}
  } console_attempt_guard{console,loading_work};
  std::unique_ptr<hlclient::app::RuntimeReplayLocalAssets> local_assets;
  hlclient::app::ModelPreparationCache model_preparation_cache;
  const auto presentation_model = [&](const hlclient::client::RuntimeClientObservationState& state) {
    return local_assets && state.receiving_client && state.receiving_client->viewmodel_index
        ? local_assets->presentation_model_handle(state.generation, *state.receiving_client->viewmodel_index)
        : hlclient::game_api::LocalWeaponModelHandle{};
  };
  const auto prepare_presentation_variants = [&](const hlclient::client::RuntimeClientObservationState& state) {
    if(!local_assets) return;
    const auto names=game_client->local_model_preparation();
    const auto reset_serial=game_client->reset_serial();
    if(model_preparation_cache.matches(local_assets.get(),state.generation,reset_serial,names)) return;
    std::vector<hlclient::game_api::LocalWeaponModelMetadata> variants;
    variants.reserve(names.size());
    for(const auto name:names) {
      for(const auto& [index,resource]:local_assets->summary().model_names) if(resource==name) {
        if(auto metadata=local_assets->presentation_model(state.generation,index)) variants.push_back(std::move(*metadata));
        break;
      }
    }
    if(!game_client->bind_model_variants(variants)) throw std::runtime_error{"Invalid module model metadata variants"};
    // Asset bindings are immutable within this owner. Do not memoize partial
    // preparation: missing metadata must still be retried after a reset.
    if(variants.size()==names.size())
      (void)model_preparation_cache.remember(local_assets.get(),state.generation,reset_serial,names);
    else model_preparation_cache.invalidate();
  };
  std::optional<hlclient::client::RuntimeObservationSource> last_entity_source;
  std::uint64_t last_projected_publication = 0U;
  std::size_t visual_failures = 0U;
  bool asset_job_started = false;
  bool assets_installed = false;
  bool prediction_collision_attached = false;
  bool renderer_resources_presented = false;
  bool control_activated = false;
  bool window_closed = false;
  bool console_disconnected = false;
  bool external_cancelled = false;
  bool external_gameplay_committed = false;
  bool external_server_reported = false;
  bool timed_session_complete = false;
  bool focus_seeded = false;
  std::size_t capture_acquisitions = 0U;
  std::size_t capture_releases = 0U;
  std::size_t focus_loss_count = 0U;
  std::optional<std::uint8_t> pending_weapon_selection;
  std::optional<std::uint32_t> model_index_before_selection;
  std::optional<Clock::time_point> last_weapon_request_at;
  std::size_t weapon_selection_queued = 0U;
  std::size_t weapon_selection_confirmed = 0U;
  bool scripted_weapon_request_sent = false;
  bool scripted_glock_request_sent = false;
  bool scripted_crowbar_request_sent = false;
  std::optional<hlclient::client::RuntimeObservationSource> last_action_animation_source;
  std::optional<std::int32_t> clip_before_fire;
  std::optional<std::int32_t> clip_after_fire;
  std::optional<std::int32_t> clip_after_reload;
  std::optional<std::uint8_t> reserve_before_reload;
  std::optional<std::uint8_t> reserve_after_reload;
  std::size_t server_confirmed_shots = 0U;
  std::size_t server_confirmed_reload_starts = 0U;
  std::size_t server_confirmed_reload_completions = 0U;
  std::size_t svc_weaponanim_count = 0U;
  std::size_t glock_fire_animation_count = 0U;
  std::size_t glock_reload_animation_count = 0U;
  std::size_t crowbar_attack_animation_count = 0U;
  std::size_t server_punch_observations = 0U;
  std::size_t recoil_presented_frames = 0U;
  std::array<std::size_t, 3U> action_presented_frames{};
  std::optional<std::uint64_t> local_probe_restart;
  double maximum_server_punch_degrees = 0.0;
  std::optional<std::size_t> last_animation_kind;
  std::array<std::optional<hlclient::renderer::RenderScene>, 3U>
      animation_first_scene, animation_later_scene;
  std::array<std::optional<Clock::time_point>, 3U> animation_first_at;
  std::array<std::optional<hlclient::client::RuntimeObservationSource>, 3U>
      animation_probe_source;
  std::array<bool, 3U> animation_pixels_changed{};
  std::size_t viewmodel_presented_frames = 0U;
  hlclient::renderer::TransientVisuals transient_visuals;
  // These fixed, session-owned pools outlive every synchronous update callback.
  // Keep their storage off the Win32 stack: an eight-ray remote batch must not
  // compete with two complete decal/particle/tracer pools on nested RX/update.
  // Allocate once, never per shot/frame; lifetime, bounds and reset are unchanged.
  auto world_impact_storage=std::make_unique<hlclient::app::WorldImpactPresentation>(
      options.debug_freeze_tracer);
  auto crowbar_impact_storage=std::make_unique<hlclient::app::WorldImpactPresentation>();
  auto& world_impacts=*world_impact_storage;
  auto& crowbar_impacts=*crowbar_impact_storage;
  std::array<std::uint64_t,12U> world_impact_status_counts{};
  std::array<std::uint64_t,12U> crowbar_impact_status_counts{};
  std::uint64_t world_impact_sound_requests{};
  std::uint32_t material_impact_diagnostic_lines{};
  std::uint32_t particle_diagnostic_lines{};
  std::uint64_t particle_diagnostic_serial{};
  std::uint32_t movement_diagnostic_lines{};
  std::uint64_t movement_diagnostic_serial{};
  std::uint64_t crowbar_hit_animations{}, crowbar_miss_animations{},
      crowbar_contact_audio_requests{},
      crowbar_decal_pending{}, crowbar_decal_published{}, crowbar_decal_cancelled_stale{};
  std::uint64_t transient_visual_generation = 0U;
  std::optional<hlclient::world_render::WorldRendererResourceIdentity>
      world_impact_resource_identity;
  hlclient::game_api::LocalVisualStatistics local_visual_stats;
  std::uint64_t beam_diagnostic_shots{};unsigned beam_diagnostic_lines{};
  std::size_t flash_render_submissions = 0U;
  std::size_t light_render_submissions = 0U;
  std::size_t light_attachment_missing = 0U;
  std::size_t shell_render_submissions = 0U;
  std::size_t shell_frame_rejected = 0U;
  std::size_t shell_culled = 0U;
  std::size_t visual_resource_missing = 0U;
  std::size_t hud_presented_frames = 0U;
  std::uint64_t last_weapon_hud_revision_logged = 0U;
  std::optional<std::uint32_t> last_viewmodel_index_logged;
  std::size_t weapon_observation_logs = 0U;
  bool viewmodel_pixel_tested = false;
  bool viewmodel_pixels_distinct = false;
  std::optional<std::uint32_t> viewmodel_probe_asset_index;
  std::optional<Clock::time_point> last_viewmodel_probe_at;
  std::size_t viewmodel_probes_for_asset = 0U;
  std::size_t viewmodel_probe_count = 0U;
  std::vector<hlclient::renderer::RenderScene> deferred_visual_probes;
  std::optional<hlclient::renderer::RenderScene> latest_viewmodel_scene;
  std::array<std::optional<hlclient::renderer::opengl::OpenGlFramebufferObservation>,
             3U> pitch_probe_samples;
  std::size_t pitch_probe_count = 0U;
  bool hud_pixel_tested = false;
  bool hud_pixels_distinct = false;
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
  std::size_t prediction_interpolated_frames = 0U;
  std::size_t prediction_endpoint_frames = 0U;
  std::size_t prediction_collision_blocked_frames = 0U;
  std::size_t prediction_visual_correction_frames = 0U;
  double prediction_active_time_ms = 0.0;
  double prediction_fallback_time_ms = 0.0;
  std::size_t prediction_long_stall_frames = 0U;
  std::optional<Clock::time_point> last_prediction_frame_at;
  bool last_prediction_frame_active = false;
  double prediction_presentation_cpu_total_ms = 0.0;
  double prediction_presentation_cpu_max_ms = 0.0;
  std::array<hlclient::goldsrc::LiveReferencePredictionSnapshot, 8U>
      prediction_correction_window{};
  std::size_t prediction_correction_window_count = 0U;
  std::size_t prediction_last_correction_count = 0U;
  constexpr std::size_t h4_phase_count = 5U;
  std::array<std::size_t, h4_phase_count> h4_active_frames{};
  std::array<std::size_t, h4_phase_count> h4_fallback_frames{};
  std::array<std::size_t, h4_phase_count> h4_local_steps{};
  std::array<std::size_t, h4_phase_count> h4_corrections{};
  std::array<std::string, h4_phase_count> h4_last_fallback_reason{};
  std::array<double, h4_phase_count> h4_active_ms{};
  std::array<double, h4_phase_count> h4_fallback_ms{};
  std::size_t h4_jump_rising_frames = 0U;
  std::size_t h4_duck_transition_frames = 0U;
  std::size_t h4_stable_crouch_frames = 0U;
  std::size_t h4_standing_recovery_frames = 0U;
  std::size_t h4_crouch_predicted_horizontal_changes = 0U;
  std::size_t h4_crouch_server_horizontal_changes = 0U;
  std::optional<std::array<float, 2U>> h4_last_predicted_crouch_xy;
  std::optional<std::array<double, 2U>> h4_last_server_crouch_xy;
  std::optional<hlclient::client::RuntimeObservationSource>
      h4_last_server_crouch_source;
  std::size_t h4_last_steps = 0U;
  std::size_t h4_last_corrections = 0U;
  std::optional<std::size_t> h4_last_phase;
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
  std::size_t inventory_notifications = 0U, inventory_feedback_rows = 0U;
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
    if (u && u.sample && u.sample->server_angle_correction_applied)
      ++server_angle_corrections;
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
  if(console) {
    const auto select=[&](std::uint8_t id) {
      const auto& current=scene_source.world_state().runtime_observation();
      if(!current || !control_activated || !game_client->inventory_selection(*current,id))return false;
      const auto now=Clock::now();
      const bool queued=(!last_weapon_request_at || now-*last_weapon_request_at>=
          std::chrono::milliseconds{game_client->movement_policy().inventory_repeat_milliseconds}) &&
          session.request_weapon_selection(id);
      game_client->hud_selection_result(id,queued,std::chrono::duration<double>{now-session_started}.count());
      if(queued) {
        pending_weapon_selection=id;
        model_index_before_selection=current->receiving_client ? current->receiving_client->viewmodel_index : std::nullopt;
        last_weapon_request_at=now;++weapon_selection_queued;
      }
      return queued;
    };
    const auto menu=[&,select](hlclient::game_api::HudMenuInput request) {
      const auto& current=scene_source.world_state().runtime_observation();
      if(!current || !control_activated)return false;
      request.seconds=std::chrono::duration<double>{Clock::now()-session_started}.count();
      const auto result=game_client->hud_menu_input(*current,request);
      return result.selection ? select(*result.selection) : result.menu_open;
    };
    hlclient::app::ConsoleSessionActions actions;
    actions.select_slot=[menu](unsigned slot){hlclient::game_api::HudMenuInput m;m.group_digit=static_cast<std::uint8_t>(slot);return menu(m);};
    actions.cycle_inventory=[menu](int direction){hlclient::game_api::HudMenuInput m;m.cycle_direction=direction;return menu(m);};
    actions.last_inventory=[menu]{hlclient::game_api::HudMenuInput m;m.last_weapon_pressed=true;return menu(m);};
    actions.select_weapon=[&,select](std::string_view name){
      const auto& current=scene_source.world_state().runtime_observation();if(!current)return false;
      for(const auto& weapon:current->weapon_hud.catalogue)if(weapon.command_name==name)return select(weapon.id);
      return false;
    };
    actions.request=[&](std::string_view name,std::string_view argument) {
      using Kind=hlclient::goldsrc::ConsoleServerCommandKind;
      std::optional<Kind> kind;
      if(name=="say")kind=Kind::say;else if(name=="say_team")kind=Kind::say_team;
      else if(name=="status")kind=Kind::status;else if(name=="kill")kind=Kind::kill;else if(name=="drop")kind=Kind::drop;
      return kind && control_activated && session.request_console_command({*kind,std::string{argument}});
    };
    console->set_session_actions(std::move(actions));
  }
  std::cout << "live_visual_startup phase=prepared handshake="
            << session.handshake_state() << std::endl;
  game_client->set_movement_audio_time_origin(
      std::chrono::duration<double>{session_started.time_since_epoch()}.count());
  hlclient::app::TestStartHealthGate test_health_gate(options.test_start_health);
  bool test_health_ready_logged = false;
  bool test_health_100_logged = false;
  auto previous_time = session_started;
  auto previous_session_update = session_started;
  const bool movement_diagnostics_enabled=receipts.movement().enabled();
  window.set_input_diagnostic_clocks(movement_diagnostics_enabled);
  receipts.multiplayer().invalidate();receipts.multiplayer().set_world_query(nullptr);
  std::optional<Clock::time_point> movement_previous_frame,movement_fallback_started;
  struct DiagnosticGpuOwner {std::uint64_t frame{},network{},map{};};
  std::array<DiagnosticGpuOwner,8> diagnostic_gpu_owners{};
  std::uint64_t movement_previous_limiter_wait_us{},movement_previous_yield_wait_us{};
  bool movement_previous_waits_known{};
  const auto movement_elapsed_ms=[](Clock::time_point begin,Clock::time_point end) {
    return std::chrono::duration<double,std::milli>{end-begin}.count();
  };
  bool running = true;
  while (running) {
    hlclient::app::MovementFrameRecord movement_frame;
    hlclient::app::MovementFrameContext movement_context;
    std::optional<hlclient::app::MovementVelocityStopRecord> movement_velocity_stop;
    const auto movement_frame_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
    if(movement_diagnostics_enabled) {
      movement_frame.frame_number=successful_presentations+1U;
      movement_frame.previous_iteration_waits_known=movement_previous_waits_known;
      movement_frame.limiter_wait_us=movement_previous_limiter_wait_us;
      movement_frame.yield_wait_us=movement_previous_yield_wait_us;
    }
    if(external_cancel_requested(options)) {
      external_cancelled=true;
      window_closed=true;
      break;
    }
    input_tracker.begin_frame();
    // Resolve overlay input before applying the ordinary Escape fallback.
    // A group key followed by Escape in the same pump must not release or
    // close the window before its menu intent is considered.
    window.set_escape_input_owned(console || (keyboard && control_activated));
    if (!focus_seeded) {
      input_tracker.apply_event(
          keyboard && window.focus_state() ==
                          hlclient::input::InputFocusState::focused
              ? hlclient::input::InputEvent::focus_gained()
              : hlclient::input::InputEvent::focus_lost());
      focus_seeded = true;
    }
    if(console)console->begin_frame(&input_tracker);
    hlclient::platform::PlatformEvent event{hlclient::platform::WindowEvent{}};
    while (window.poll_event(event)) {
      if (const auto *window_event =
              std::get_if<hlclient::platform::WindowEvent>(&event)) {
        if (window_event->type ==
            hlclient::platform::WindowEventType::quit_requested) {
          running = false;
          window_closed = true;
          if(console)console->request_quit();
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
        const bool console_owned=console && console->route_event(input_event,input_tracker);
        if(!console_owned)
          input_tracker.apply_event(input_event);
        if(movement_diagnostics_enabled) {
          hlclient::core::MultiplayerInputPayload input;
          input.event_type=static_cast<std::uint32_t>(input_event.type());
          // Never retain text or typed key identities while an overlay owns it.
          if(!console_owned && (input_event.type()==hlclient::input::InputEventType::key_pressed ||
              input_event.type()==hlclient::input::InputEventType::key_released))
            input.key=1U+static_cast<std::uint32_t>(input_event.key());
          if(input_event.type()==hlclient::input::InputEventType::mouse_button_pressed ||
              input_event.type()==hlclient::input::InputEventType::mouse_button_released)
            input.mouse_button=1U+static_cast<std::uint32_t>(input_event.mouse_button());
          input.delta_x=static_cast<float>(input_event.relative_mouse_delta().x);
          input.delta_y=static_cast<float>(input_event.relative_mouse_delta().y);
          input.wheel=static_cast<float>(input_event.wheel_delta().vertical);
          input.repeat=input_event.repeated();input.console_owned=console_owned;
          input.gameplay_routed=!console_owned;
          input.focused=window.focus_state()==hlclient::input::InputFocusState::focused;
          input.captured=window.capture_state()==hlclient::input::InputCaptureState::captured;
          if(input_event.sdl_timestamp_ns())input.sdl_timestamp_ns=input_event.sdl_timestamp_ns();
          if(input_event.dequeue_steady_ns())input.dequeue_steady_ns=input_event.dequeue_steady_ns();
          receipts.diagnostic(input,hlclient::core::MultiplayerDiagnosticProducer::input,
              movement_frame.frame_number,input_tracker.published_frame_count()+1U);
        }
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
    if(movement_diagnostics_enabled)movement_frame.input_sample_us=receipts.movement_stamp(Clock::now());
    input_tracker.end_frame();
    if(console && console->session_stop_requested()) {
      window_closed=console->shutdown_requested();
      console_disconnected=!window_closed;
      running=false;
    }
    const bool movement_manual_marker=movement_diagnostics_enabled &&
        snapshot.focus_state()==hlclient::input::InputFocusState::focused &&
        snapshot.key_pressed(hlclient::input::PhysicalKey::f8);
    if (!running)
      break;

    const auto now = Clock::now();
    if (const auto& observation = scene_source.world_state().runtime_observation();
        !keyboard && observation && pending_weapon_selection &&
        observation->weapon_hud.active_weapon_id == pending_weapon_selection &&
        observation->receiving_client &&
        observation->receiving_client->viewmodel_index &&
        (!model_index_before_selection ||
         observation->receiving_client->viewmodel_index !=
             model_index_before_selection)) {
      pending_weapon_selection.reset();
      model_index_before_selection.reset();
      ++weapon_selection_confirmed;
    }
    if (weapon_check && control_activated && !scripted_weapon_request_sent) {
      const auto& observation = scene_source.world_state().runtime_observation();
      if (observation && observation->weapon_hud.active_weapon_id &&
          *observation->weapon_hud.active_weapon_id != 0U) {
        const auto selected = game_client->cycle_inventory(
            *observation, observation->weapon_hud.active_weapon_id, 1);
        if (selected && selected != observation->weapon_hud.active_weapon_id &&
            session.request_weapon_selection(*selected)) {
          scripted_weapon_request_sent = true;
          pending_weapon_selection = selected;
          model_index_before_selection = observation->receiving_client
              ? observation->receiving_client->viewmodel_index
              : std::nullopt;
          last_weapon_request_at = now;
          ++weapon_selection_queued;
        }
      }
    }
    if (fire_reload_check && control_activated) {
      const auto& observation = scene_source.world_state().runtime_observation();
      const auto progress = session.live_usercmd_snapshot();
      if (observation) {
        const auto request_named = [&](const std::size_t phase,
                                       bool& requested) {
          if (requested) return;
          const auto selected = game_client->scenario_inventory_target(*observation,phase);
          if (!selected) return;
          if (observation->weapon_hud.active_weapon_id == *selected) {
            requested = true;
            return;
          }
          if (session.request_weapon_selection(*selected)) {
            requested = true;
            pending_weapon_selection = *selected;
            model_index_before_selection = observation->receiving_client
                ? observation->receiving_client->viewmodel_index : std::nullopt;
            last_weapon_request_at = now;
            ++weapon_selection_queued;
          }
        };
        if (progress && progress->generated_by_phase[3U] > 0U)
          request_named(3U, scripted_crowbar_request_sent);
        else
          request_named(0U, scripted_glock_request_sent);
      }
    }
    if (keyboard && options.live_session_seconds &&
        now - session_started >=
            std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
                *options.live_session_seconds)}) {
      timed_session_complete = true;
      if(console)console->request_quit();
      break;
    }
    const auto elapsed = now - previous_time;
    const double input_seconds = std::clamp(
        std::chrono::duration<double>{elapsed}.count(), 0.001, 0.25);
    auto mouse_look=game_client->movement_policy().mouse_look;
    if(console)mouse_look.sensitivity_scale*=console->sensitivity_scale();
    if (const auto& current=scene_source.world_state().runtime_observation(); current)
      mouse_look.sensitivity_scale*=game_client->camera(*current,0.0).mouse_sensitivity_scale;
    auto built_intent =
        hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
            snapshot, *bindings,
            mouse_look, input_seconds);
    if (!built_intent || !built_intent.intent)
      throw std::runtime_error{"Live visual gameplay intent build failed"};
    const auto &intent = *built_intent.intent;
    hlclient::game_api::HudMenuResult hud_input_result;
    bool capture_released_after_ui{};
    if (keyboard && control_activated) {
      const auto framebuffer = window.pixel_extent();
      game_client->set_hud_context({static_cast<std::uint32_t>(framebuffer.width),
          static_cast<std::uint32_t>(framebuffer.height),
          hlclient::gameplay_input::gameplay_button_is_set(intent.held_buttons(),
              hlclient::gameplay_input::GameplayButton::scoreboard),
          options.hud_draw,options.crosshair,options.hud_fastswitch,options.hud_diagnostics,
          options.hud_scale.automatic,options.hud_scale.scale});
      if (const auto& current = scene_source.world_state().runtime_observation(); current && snapshot.focused()) {
        hlclient::game_api::HudMenuInput menu;
        menu.seconds=std::chrono::duration<double>{now-session_started}.count();
        const auto& keys=game_client->movement_policy().inventory_group_keys;
        for(std::size_t i=0;i<keys.size();++i)if(snapshot.key_pressed(keys[i])) {
          menu.group_digit=static_cast<std::uint8_t>(i+1U);break;
        }
        const auto wheel=snapshot.wheel_delta().vertical;
        menu.cycle_direction=wheel>0.0 ? 1 : wheel<0.0 ? -1 : 0;
        if(const auto key=game_client->movement_policy().inventory_last_key)
          menu.last_weapon_pressed=snapshot.captured() && snapshot.key_pressed(*key);
        const auto primary=hlclient::gameplay_input::gameplay_button_mask(
            hlclient::gameplay_input::GameplayButton::attack_primary);
        menu.primary_pressed=(intent.pressed_buttons()&primary)!=0U;
        menu.primary_held=(intent.held_buttons()&primary)!=0U;
        menu.primary_released=(intent.released_buttons()&primary)!=0U;
        menu.cancel=snapshot.key_pressed(hlclient::input::PhysicalKey::escape);
        hud_input_result=game_client->hud_menu_input(*current,menu);
        if(hud_input_result.selection) {
          const bool queued=(!last_weapon_request_at ||
              now-*last_weapon_request_at>=std::chrono::milliseconds{game_client->movement_policy().inventory_repeat_milliseconds}) &&
              session.request_weapon_selection(*hud_input_result.selection);
          game_client->hud_selection_result(*hud_input_result.selection,queued,menu.seconds);
          if(queued) {
            pending_weapon_selection=hud_input_result.selection;
            model_index_before_selection=current->receiving_client
                ? current->receiving_client->viewmodel_index : std::nullopt;
            last_weapon_request_at=now;++weapon_selection_queued;
          }
        }
      }
    }
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

    if (keyboard && (!console || !console->console_active()) && assets_installed &&
        test_health_gate.ready() &&
        (session.live_visual_input_ready() || control_activated) &&
        intent.capture_mouse_requested()) {
      const auto captured = window.request_relative_mouse_capture(true);
      if (!captured)
        throw std::runtime_error{"Unable to acquire relative mouse capture"};
      if (captured.status ==
          hlclient::platform::RelativeMouseCaptureStatus::acquired)
        ++capture_acquisitions;
    }
    if(keyboard && control_activated && intent.release_mouse_requested() &&
        !hud_input_result.cancel_consumed && !snapshot.captured()) {
      // Preserve the old SDL uncaptured Escape close path after UI refusal.
      running=false;window_closed=true;if(console)console->request_quit();break;
    }
    if (keyboard && intent.release_mouse_requested() && !hud_input_result.cancel_consumed) {
      const auto released = window.request_relative_mouse_capture(false);
      if (!released)
        throw std::runtime_error{"Unable to release relative mouse capture"};
      if (released.status ==
          hlclient::platform::RelativeMouseCaptureStatus::released)
        ++capture_releases;
      capture_released_after_ui=true;
    }

    if (assets_installed && !capture_released_after_ui && !camera_controller.apply_local_look(intent))
      throw std::runtime_error{"Live visual local look update failed"};

    const auto submit_keyboard_input = [&](const Clock::time_point timestamp) {
      if (!keyboard || !assets_installed)
        return false;
      auto input=hlclient::goldsrc::LiveVisualControlInput{
              1U,
              intent.input_sequence(),
              intent.focused() && test_health_gate.ready() ? intent.forward_axis() : 0.0F,
              intent.focused() && test_health_gate.ready() ? intent.side_axis() : 0.0F,
              camera_controller.yaw_degrees(),
              camera_controller.pitch_degrees(),
              intent.focused(),
              intent.captured(),
              snapshot.key_held(hlclient::input::PhysicalKey::a),
              snapshot.key_held(hlclient::input::PhysicalKey::d),
              intent.focused() && test_health_gate.ready() ? intent.held_buttons() & live_held_button_mask : 0U,
              intent.focused() && test_health_gate.ready() ? intent.pressed_buttons() & live_button_mask
                               : 0U,
              intent.released_buttons() & live_button_mask,
              hud_input_result.consume_primary ? hlclient::gameplay_input::gameplay_button_mask(
                  hlclient::gameplay_input::GameplayButton::attack_primary) : 0U};
      if(capture_released_after_ui)hlclient::goldsrc::release_live_visual_input_capture(
          input,game_client->movement_policy().capture_life_scoped_buttons);
      if(movement_diagnostics_enabled) {
        input.input_sample_ns=hlclient::core::multiplayer_diagnostic_now_ns();
        if(local_assets)input.effective_interpolation_seconds=local_assets->effective_remote_interpolation_delay_seconds();
        hlclient::core::MultiplayerInputPayload routed;
        routed.event_type=0x10000U; // normalized gameplay snapshot, not an SDL event
        routed.focused=intent.focused();routed.captured=intent.captured();
        routed.hud_consumed=hud_input_result.consume_primary;routed.gameplay_routed=true;
        receipts.diagnostic(routed,hlclient::core::MultiplayerDiagnosticProducer::input,movement_frame.frame_number,intent.input_sequence());
      } else if(local_assets)input.effective_interpolation_seconds=local_assets->effective_remote_interpolation_delay_seconds();
      return session.submit_live_visual_input(input,timestamp);
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
    const auto presentation_seconds =
        std::chrono::duration<double>{now - session_started}.count();
    if (const auto& before = scene_source.world_state().runtime_observation()) {
      prepare_presentation_variants(*before);
      if (local_assets)
        game_client->bind_model_handle(
            presentation_model(*before));
      game_client->observe(*before, presentation_seconds);
    }
    if(movement_diagnostics_enabled) {
      movement_context.focused=snapshot.focused();
      movement_context.captured=window.capture_state()==hlclient::input::InputCaptureState::captured;
      movement_context.control_active=control_activated;
      if(const auto& observation=scene_source.world_state().runtime_observation();observation) {
        movement_context.life_epoch=observation->lifecycle.life_epoch;
        if(observation->lifecycle.state==hlclient::client::LocalPlayerLifeState::alive)movement_context.alive=true;
        else if(observation->lifecycle.dead())movement_context.alive=false;
      }
      receipts.movement().set_update_context(movement_context);
    }
    const auto movement_network_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
    session.update(now);
    if(console && !asset_job_started && !control_activated) {
      using Stage=hlclient::app::LoadingStage;
      const auto phase=session.handshake_state();
      console->set_stage(phase=="waiting_for_authentication" ? Stage::authenticating
          : phase=="waiting_for_challenge" || phase=="waiting_for_connect_response" ? Stage::connecting
          : phase.find("resource")!=std::string_view::npos ? Stage::resources : Stage::signon);
    }
    if(movement_diagnostics_enabled) {
      const auto movement_network_completed=Clock::now();
      movement_frame.network_poll_begin_us=receipts.movement_stamp(movement_network_started);
      movement_frame.network_poll_end_us=receipts.movement_stamp(movement_network_completed);
      const auto metrics=session.movement_diagnostics_snapshot();
      if(metrics.enabled) {
      movement_frame.network_ms=metrics.network_driver_ms;
      movement_frame.parse_commit_ms=metrics.network_parse_commit_ms;
      movement_frame.prediction_ms=metrics.prediction_command_ms+metrics.prediction_replay_ms;
      movement_frame.prediction_work=metrics.prediction_work;
      movement_frame.prediction_work->command_ms=metrics.prediction_command_ms;
      movement_frame.prediction_work->replay_ms=metrics.prediction_replay_ms;
      movement_frame.collision_ms=metrics.collision_scene_ms;
      movement_frame.replay_duration_ms=metrics.replayed_simulation_ms;
      movement_frame.queue_depth=static_cast<std::uint32_t>(metrics.driver_queue_before);
      movement_frame.queue_depth_after=static_cast<std::uint32_t>(metrics.driver_queue_after);
      movement_frame.entities=static_cast<std::uint32_t>(metrics.entity_count);
      if(metrics.record_identity)movement_frame.observed_players=static_cast<std::uint32_t>(metrics.observed_player_count);
      movement_frame.bytes=static_cast<std::uint32_t>(metrics.committed_bytes);
      movement_frame.committed_records=static_cast<std::uint32_t>(metrics.committed_records);
      movement_frame.replayed_commands=static_cast<std::uint32_t>(metrics.replayed_commands);
      movement_frame.source_sequence=metrics.source_transport_sequence;
      if(metrics.latest_command && metrics.latest_command_sequence && metrics.latest_command_sample_time_ns &&
          *metrics.latest_command_sample_time_ns>=0) {
        if(const auto sampled=receipts.movement_stamp_ns(static_cast<std::uint64_t>(*metrics.latest_command_sample_time_ns));sampled) {
          const auto& command=*metrics.latest_command;
          movement_frame.command=hlclient::app::MovementDiagnosticCommand{
              *sampled,*metrics.latest_command_sequence,command.angle_turns,
              {command.forward,command.side,command.up},command.buttons,command.msec};
        }
      }
      movement_frame.datagrams_received=metrics.receive_statistics.owning_datagrams;
      auto& transport_diagnostic=receipts.external_diagnostics();
      transport_diagnostic.udp_split_parts=metrics.receive_statistics.split_parts;
      transport_diagnostic.udp_split_completed=metrics.receive_statistics.split_completed;
      transport_diagnostic.udp_split_duplicates=metrics.receive_statistics.split_duplicates;
      transport_diagnostic.udp_split_rejected=metrics.receive_statistics.split_rejected;
      transport_diagnostic.udp_split_expired=metrics.receive_statistics.split_expired;
      transport_diagnostic.udp_split_evicted=metrics.receive_statistics.split_evicted;
      movement_frame.rejected_sequences=metrics.receive_statistics.rejected_sequences;
      movement_frame.choke_markers_total=metrics.choke_markers_total;
      movement_frame.svc_time_s=metrics.server_time_seconds;
      movement_frame.socket_read_begin_us=receipts.movement_stamp_ns(metrics.socket_read_begin_ns);
      movement_frame.socket_read_us=receipts.movement_stamp_ns(metrics.socket_read_end_ns);
      movement_frame.parse_begin_us=receipts.movement_stamp_ns(metrics.parse_begin_ns);
      movement_frame.commit_us=receipts.movement_stamp_ns(metrics.atomic_commit_ns);
      movement_frame.prediction_use_us=receipts.movement_stamp_ns(metrics.prediction_use_ns);
      if(const auto committed=receipts.movement_stamp_ns(metrics.latest_state_commit_ns);committed &&
          receipts.movement_stamp(movement_network_completed)>=*committed)
        movement_frame.state_age_ms=static_cast<double>(receipts.movement_stamp(movement_network_completed)-*committed)/1000.0;
      }
    }
    const auto movement_receipt_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
    receipts.connection_progress();
    if(movement_diagnostics_enabled) {
      const auto movement_receipt_completed=Clock::now();
      movement_frame.receipt_observed_us=receipts.movement_stamp(movement_receipt_completed);
      movement_frame.report_ms=movement_elapsed_ms(movement_receipt_started,movement_receipt_completed);
      receipts.observe_reporting_scope(false,*movement_frame.report_ms,successful_presentations+1U);
    }
    if(receipts.failed()) throw std::runtime_error{"Session receipt commit/publication failed"};
    if(options.external_server_client && session.terminal()) {
      auto& diagnostic=receipts.external_diagnostics();
      if(const auto& error=session.live_runtime_error();error) {
        diagnostic.reason=hlclient::goldsrc::to_string(error->code);
        // A facade's generic response failure must not erase a more precise
        // parser failure already observed through the always-on trace.
        const bool initial_service_failure=diagnostic.failure_phase=="initial_signon";
        if(!initial_service_failure) {
          if(error->runtime_decoder_error)diagnostic.protocol_code=hlclient::goldsrc::to_string(*error->runtime_decoder_error);
          else if(error->control_code)diagnostic.protocol_code=hlclient::goldsrc::to_string(*error->control_code);
          else if(error->response_code)diagnostic.protocol_code=hlclient::goldsrc::to_string(*error->response_code);
          else if(error->driver_code)diagnostic.protocol_code=hlclient::goldsrc::to_string(*error->driver_code);
        }
        if(error->response_code && !initial_service_failure) {
          diagnostic.failure_phase="resource_response";
          using ResponseCode=hlclient::goldsrc::ResourceClientResponseStageErrorCode;
          switch(*error->response_code) {
          case ResponseCode::response_acknowledgement_timed_out:
          case ResponseCode::post_response_boundary_timed_out:
            diagnostic.failure_category="timeout";break;
          case ResponseCode::pre_transmit_control_overflow:
          case ResponseCode::post_transmit_control_overflow:
          case ResponseCode::pre_ack_server_payload_overflow:
          case ResponseCode::post_response_payload_overflow:
          case ResponseCode::event_backpressure:
            diagnostic.failure_category="backpressure";break;
          case ResponseCode::provider_required:
          case ResponseCode::consistency_provider_begin_failed:
          case ResponseCode::consistency_provider_failed:
          case ResponseCode::consistency_provider_result_invalid:
          case ResponseCode::consistency_material_invalid:
            diagnostic.failure_category="provider";break;
          default:diagnostic.failure_category="protocol";break;
          }
        }
        if(error->usercmd_scheduler_code)diagnostic.usercmd_scheduler_code=hlclient::goldsrc::to_string(*error->usercmd_scheduler_code);
        if(error->usercmd_adapter_code)diagnostic.usercmd_adapter_code=hlclient::goldsrc::to_string(*error->usercmd_adapter_code);
        if(error->usercmd_transmission_code)diagnostic.usercmd_transmission_code=hlclient::goldsrc::to_string(*error->usercmd_transmission_code);
        if(error->usercmd_history_code)diagnostic.usercmd_history_code=hlclient::goldsrc::to_string(*error->usercmd_history_code);
        if(error->usercmd_diagnostic_code) {
          diagnostic.usercmd_diagnostic_code=hlclient::goldsrc::to_string(*error->usercmd_diagnostic_code);
          diagnostic.failure_phase="usercmd";
          diagnostic.failure_category=*error->usercmd_diagnostic_code==
              hlclient::goldsrc::LiveUserCmdDiagnosticErrorCode::invalid_transmit_metadata ?
              "diagnostic_metadata" : "diagnostic_retention";
          diagnostic.protocol_code=diagnostic.usercmd_diagnostic_code;
        }
        if(error->usercmd_history_code || error->usercmd_transmission_code ||
           error->usercmd_scheduler_code || error->usercmd_adapter_code) {
          diagnostic.failure_phase="usercmd";
          diagnostic.failure_category=error->usercmd_history_code ? "history" :
              error->usercmd_transmission_code ? "transmission" :
              error->usercmd_scheduler_code ? "scheduler" : "adapter";
          diagnostic.protocol_code=error->usercmd_history_code ? diagnostic.usercmd_history_code :
              error->usercmd_transmission_code ? diagnostic.usercmd_transmission_code :
              error->usercmd_scheduler_code ? diagnostic.usercmd_scheduler_code : diagnostic.usercmd_adapter_code;
        }
        if(error->driver_code)diagnostic.driver_code=hlclient::goldsrc::to_string(*error->driver_code);
        if(const auto& failure=error->driver_failure;failure) {
          diagnostic.driver_code=hlclient::goldsrc::to_string(failure->code);
          if(failure->packet_code)diagnostic.driver_packet_code=hlclient::goldsrc::to_string(*failure->packet_code);
          if(failure->session_code)diagnostic.driver_session_code=hlclient::goldsrc::to_string(*failure->session_code);
          if(failure->reassembly_code)diagnostic.driver_reassembly_code=hlclient::goldsrc::to_string(*failure->reassembly_code);
          if(failure->native_error) {
            switch(failure->native_error->domain) {
            case hlclient::network::SocketNativeErrorDomain::none:diagnostic.driver_native_domain="none";break;
            case hlclient::network::SocketNativeErrorDomain::winsock:diagnostic.driver_native_domain="winsock";break;
            case hlclient::network::SocketNativeErrorDomain::posix:diagnostic.driver_native_domain="posix";break;
            }
            diagnostic.driver_native_code=failure->native_error->code;
          }
          if(const auto& packet=failure->packet_metadata;packet) {
            diagnostic.driver_datagram_size=packet->datagram_size;
            diagnostic.driver_datagram_size_is_lower_bound=packet->datagram_size_is_lower_bound;
            if(packet->classification) {
              switch(*packet->classification) {
              case hlclient::goldsrc::NetchanDatagramClassification::connectionless:diagnostic.driver_datagram_classification="connectionless";break;
              case hlclient::goldsrc::NetchanDatagramClassification::sequenced:diagnostic.driver_datagram_classification="sequenced";break;
              case hlclient::goldsrc::NetchanDatagramClassification::unsupported_special:diagnostic.driver_datagram_classification="unsupported_special";break;
              case hlclient::goldsrc::NetchanDatagramClassification::malformed:diagnostic.driver_datagram_classification="malformed";break;
              }
            }
            diagnostic.driver_connectionless_opcode=packet->connectionless_opcode;
            diagnostic.driver_sequence=packet->sequence;
            diagnostic.driver_acknowledgement=packet->acknowledgement;
          }
        }
        if(error->driver_code || error->driver_failure) {
          diagnostic.protocol_code=diagnostic.driver_code;
          diagnostic.failure_phase="driver";
          const auto code=error->driver_failure ? error->driver_failure->code : *error->driver_code;
          switch(code) {
          case hlclient::goldsrc::NetchanDriverErrorCode::receive_failed:
          case hlclient::goldsrc::NetchanDriverErrorCode::send_failed:
          case hlclient::goldsrc::NetchanDriverErrorCode::local_endpoint_unavailable:
          case hlclient::goldsrc::NetchanDriverErrorCode::local_endpoint_changed:diagnostic.failure_category="network";break;
          case hlclient::goldsrc::NetchanDriverErrorCode::fragment_transfer_timed_out:
          case hlclient::goldsrc::NetchanDriverErrorCode::channel_inactivity_timed_out:diagnostic.failure_category="timeout";break;
          case hlclient::goldsrc::NetchanDriverErrorCode::event_backpressure:diagnostic.failure_category="backpressure";break;
          default:diagnostic.failure_category="protocol";break;
          }
        }
        if(error->wire_opcode)diagnostic.wire_opcode=error->wire_opcode;
        if(error->runtime_source_sequence)diagnostic.source_sequence=error->runtime_source_sequence;
        if(error->runtime_record_ordinal)diagnostic.record_ordinal=error->runtime_record_ordinal;
        if(error->runtime_failure && error->runtime_failure->control_error) {
          const auto& control=*error->runtime_failure->control_error;
          diagnostic.runtime_control_failure(hlclient::goldsrc::to_string(control.code),
              control.cursor ? std::optional<std::size_t>{control.cursor->byte_offset()} : std::nullopt);
        }
        // Startup/service failures may precede the runtime record decoder.
        // Retain only its decoder-established opcode and numeric source
        // identity, never a raw payload byte or arbitrary server text.
        if(const auto& payload=error->response_payload_diagnostic;payload) {
          if(!diagnostic.wire_opcode)diagnostic.wire_opcode=payload->actual_opcode;
          if(!diagnostic.source_sequence)diagnostic.source_sequence=payload->source_sequence;
          auto& retained=diagnostic.response_payload.emplace();
          retained.classification=hlclient::goldsrc::to_string(payload->classification);
          retained.receive_position=hlclient::goldsrc::to_string(payload->receive_position);
          retained.wire_encoding=hlclient::goldsrc::to_string(payload->wire_encoding);
          retained.source_body_kind=resource_response_body_kind_label(payload->source_body_kind);
          retained.source_sequence=payload->source_sequence;
          retained.source_acknowledgement=payload->source_acknowledgement;
          retained.first_transmit_sequence=payload->first_transmit_sequence;
          retained.payload_ordinal=payload->payload_ordinal;
          retained.wire_byte_count=payload->wire_byte_count;
          retained.source_reliable=payload->source_reliable;
          retained.reassembled=payload->reassembled;
          retained.response_queued=payload->response_queued;
          retained.response_transmitted=payload->response_transmitted;
          retained.response_acknowledged=payload->response_acknowledged;
        }
        if(const auto& transition=error->transition_failure_metadata;transition) {
          if(transition->parser_error || transition->intermediate_parser_error) {
            diagnostic.resource_transition_failure(
                transition->parser_error ? hlclient::goldsrc::to_string(*transition->parser_error) :
                    hlclient::goldsrc::to_string(*transition->intermediate_parser_error),
                transition->parser_error_byte_offset.value_or(transition->cursor_byte_offset),
                transition->actual_opcode,transition->source_sequence);
          } else {
            if(!diagnostic.wire_opcode)diagnostic.wire_opcode=transition->actual_opcode;
            if(!diagnostic.source_sequence)diagnostic.source_sequence=transition->source_sequence;
          }
        }
      } else if(session.external_early_failure(diagnostic)) {
        // Typed startup failure survived independently of rolling/raw stdout.
      } else if(const auto rejection=session.external_rejection_reason()) {
        diagnostic.reason=*rejection;
        if(const auto text=session.server_rejection_text())receipt_authentication_rejected(*text);
      }
      else if(const auto auth=session.authentication_error_code()) diagnostic.reason=hlclient::app::live_authentication_failure_reason(*auth);
      else diagnostic.reason=session.handshake_state();
      external_status("error",diagnostic.reason);
    }
    if(options.external_server_client && session.live_server_info()) {
      const auto& info=*session.live_server_info();
      auto& diagnostic=receipts.external_diagnostics();
      diagnostic.protocol=static_cast<unsigned>(info.protocol_version());
      diagnostic.max_clients=info.maximum_clients().value();
      diagnostic.game=info.game_directory();
      diagnostic.map=hlclient::app::normalized_session_map_identifier(info.map_file_path()).value_or("rejected_virtual_name");
      if(!hlclient::app::external_half_life_profile(info.game_directory())) {
        external_status("error","unsupported_game");
        throw std::runtime_error{"External server game profile is unsupported (expected valve)"};
      }
      if(!external_server_reported) {
        external_server_reported=true;
        const auto map=hlclient::app::normalized_session_map_identifier(info.map_file_path());
        std::cout<<"[external-server] protocol="<<static_cast<std::uint32_t>(info.protocol_version())
            <<" max_clients="<<unsigned(info.maximum_clients().value())
            <<" game=valve map="<<(map ? *map : "invalid")<<'\n'<<std::flush;
      }
    }
    previous_session_update = now;
    if (options.test_start_health) {
      const auto& observed = scene_source.world_state().runtime_observation();
      if (observed && observed->receiving_client && observed->client_metadata.source)
        test_health_gate.observe(observed->receiving_client->health,
            observed->client_metadata.freshness == hlclient::client::RuntimeObservationFreshness::observed_in_record,
            observed->client_metadata.source->record_identity);
      if (test_health_gate.ready() && !test_health_ready_logged) {
        std::cout << "[test-start-health] requested_start_health=50 client_observed_health=50 source=fresh_clientdata result=ready\n";
        std::cout.flush(); test_health_ready_logged = true;
      }
      if (!test_health_gate.ready() && test_health_gate.observed() == 100.0 && !test_health_100_logged) {
        std::cout << "[test-start-health] requested_start_health=50 client_observed_health=100 source=fresh_clientdata result=pending\n";
        std::cout.flush(); test_health_100_logged = true;
      }
      if (test_health_gate.expired(std::chrono::duration<double>{now-session_started}.count())) {
        std::cerr << "[test-start-health] result=failed reason=fresh_server_50_not_observed client_observed_health=";
        if (test_health_gate.observed()) std::cerr << *test_health_gate.observed(); else std::cerr << "unavailable";
        std::cerr << '\n'; break;
      }
    }
    if (const auto& observed = scene_source.world_state().runtime_observation(); keyboard && observed) {
      hlclient::game_api::GameActionTraffic traffic_view;
      traffic_view.use_new_submission_count = session.live_use_new_submission_count();
      game_client->observe_action_evidence(*observed, traffic_view);
    }
    if (const auto& after = scene_source.world_state().runtime_observation()) {
      prepare_presentation_variants(*after);
      // A pickup/switch must bind before draining its first submitted command.
      // Binding afterwards cancels that action and its frozen shot context.
      if(local_assets) game_client->bind_model_handle(presentation_model(*after));
      game_client->observe(*after, presentation_seconds);
    }
    while (const auto submitted = session.poll_weapon_command_submission()) {
      const auto sample_seconds =
          std::chrono::duration<double>{std::chrono::nanoseconds{
              submitted->command_end_nanoseconds} -
              session_started.time_since_epoch()}.count();
      hlclient::game_api::LocalWeaponSubmittedCommand command{
          submitted->generation,submitted->command_sequence,
          submitted->buttons,sample_seconds};
      if (scene_source.world_state().interactive_camera_metadata()) {
        constexpr double turn_degrees=360.0/65536.0;
        const auto yaw=double(submitted->angle_turns[1])*turn_degrees;
        const auto pitch=-std::remainder(double(submitted->angle_turns[0])*turn_degrees,360.0);
        const auto direction=hlclient::gameplay_camera::forward_from_yaw_pitch(yaw,pitch);
        if (direction) {
          const auto yaw_radians=yaw*3.14159265358979323846/180.0;
          const hlclient::assets::AssetVector3 right{
              static_cast<float>(std::sin(yaw_radians)),static_cast<float>(-std::cos(yaw_radians)),0};
          const hlclient::assets::AssetVector3 up{
              direction->y*right.z-direction->z*right.y,
              direction->z*right.x-direction->x*right.z,
              direction->x*right.y-direction->y*right.x};
          command.shot_context=hlclient::game_api::LocalWeaponSubmittedCommand::ShotContext{
              scene_source.world_state().camera().position,*direction,right,{-up.x,-up.y,-up.z}};
          command.shot_context->view_angles_degrees=std::array{
              static_cast<float>(-pitch),static_cast<float>(yaw),
              static_cast<float>(std::remainder(double(submitted->angle_turns[2])*turn_degrees,360.0))};
          if(submitted->player_origin) command.shot_context->player_origin=std::array{
              submitted->player_origin->x,submitted->player_origin->y,submitted->player_origin->z};
          command.shot_context->player_ducked=submitted->player_ducked;
          if(const auto& observation=scene_source.world_state().runtime_observation();
              observation && observation->receiving_client) {
            const auto owning_vector=[](const auto& value)->std::optional<std::array<float,3>> {
              if(!value.complete()) return {};
              const std::array<double,3> values{*value.x,*value.y,*value.z};
              std::array<float,3> result{};
              for(std::size_t i=0;i<values.size();++i) {
                if(!std::isfinite(values[i]) || std::abs(values[i])>std::numeric_limits<float>::max()) return {};
                result[i]=static_cast<float>(values[i]);
              }
              return result;
            };
            command.shot_context->punch_angles_degrees=owning_vector(observation->receiving_client->punch_angle);
            command.shot_context->player_velocity=owning_vector(observation->receiving_client->velocity);
          }
        }
      }
      game_client->submit(command,presentation_seconds);
    }
    if (const auto& after = scene_source.world_state().runtime_observation()) {
      game_client->observe(*after, presentation_seconds);
    }
    if (keyboard && !intent.focused())
      game_client->cancel_uncommitted();
    if(local_assets) {
      receipts.multiplayer().set_world_query(local_assets.get());
      local_assets->placement_observation(scene_source.world_state().runtime_observation());
      game_client->prepare_presentation(*local_assets,presentation_seconds);
    }
    auto weapon_presentation =
        game_client->sample(presentation_seconds);
    if (fire_reload_check) {
      const auto& observed = scene_source.world_state().runtime_observation();
      const auto traffic = session.live_usercmd_snapshot();
      if (observed) {
        std::optional<hlclient::game_api::GameActionTraffic> traffic_view;
        if (traffic) traffic_view = hlclient::game_api::GameActionTraffic{
            traffic->attack_new_submission_count, traffic->reload_new_submission_count,
            traffic->generated_by_phase};
        game_client->observe_action_evidence(*observed, traffic_view);
        const auto evidence = game_client->action_evidence();
        clip_before_fire = evidence.clip_before_fire;
        clip_after_fire = evidence.clip_after_fire;
        clip_after_reload = evidence.clip_after_reload;
        reserve_before_reload = evidence.reserve_before_reload;
        reserve_after_reload = evidence.reserve_after_reload;
        server_confirmed_shots = evidence.server_confirmed_shots;
        server_confirmed_reload_starts = evidence.server_confirmed_reload_starts;
        server_confirmed_reload_completions = evidence.server_confirmed_reload_completions;
        svc_weaponanim_count = evidence.service_animation_count;
        glock_fire_animation_count = evidence.primary_animation_count;
        glock_reload_animation_count = evidence.reload_animation_count;
        crowbar_attack_animation_count = evidence.melee_animation_count;
        server_punch_observations = evidence.server_punch_observations;
        maximum_server_punch_degrees = evidence.maximum_server_punch_degrees;
        last_animation_kind = evidence.last_animation_kind;
        last_action_animation_source = evidence.last_animation_source;
      }
    }
    if (!control_activated) {
      const auto before_activation = session.live_usercmd_snapshot();
      preactivation_command_count = std::max(
          preactivation_command_count,
          before_activation ? before_activation->generated_command_count : 0U);
    }

    const auto movement_assets_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
    if (!asset_job_started && session.live_resource_list() != nullptr &&
        session.live_server_info() != nullptr) {
      external_status("preparing_map");
      auto resources = *session.live_resource_list();
      auto server_info = *session.live_server_info();
      const auto basedir = path_from_utf8(*options.base_directory);
      const auto game = options.game_directory;
      const std::string effect_palette_dependency{game_client->effect_palette_preparation()};
      const std::string sky_name{session.live_sky_name()};
      std::vector<std::string> effect_sprite_dependencies;
      for(const auto name:game_client->effect_sprite_preparation())
        effect_sprite_dependencies.emplace_back(name);
      const auto impact_profile=game_client->local_impact_assets();
      const auto crowbar_profile=game_client->local_crowbar_impact_assets();
      const auto decal_requests=game_client->surface_decal_preparation();
      std::vector<hlclient::game_api::DecalAssetRequest> decals{decal_requests.begin(),decal_requests.end()};
      if(console)console->set_work(loading_work);
      asset_future.emplace(std::async(
          std::launch::async,
          [resources = std::move(resources),
           server_info = std::move(server_info), basedir, game,effect_palette_dependency,
           effect_sprite_dependencies=std::move(effect_sprite_dependencies),sky_name,
           impact_profile,crowbar_profile,decals=std::move(decals),loading_work]() {
            auto created=hlclient::app::RuntimeReplayLocalAssets::create(
                resources, server_info, basedir, game,
                hlclient::app::ReplayLocalCameraPolicy::
                    external_live_receiving_client,effect_palette_dependency,
                    effect_sprite_dependencies,sky_name,loading_work,
                    hlclient::app::ReplayLocalResourcePolicy::live_standard_hl);
            if(created.projection && loading_work->progress(hlclient::app::LoadingStage::textures)) {
              if(impact_profile)(void)created.projection->prepare_impact_decal(*impact_profile);
              if(!loading_work->cancelled.load() && crowbar_profile)
                (void)created.projection->prepare_impact_decal(crowbar_profile->decal,1U);
              if(!loading_work->cancelled.load())
                (void)created.projection->prepare_surface_decals(decals);
              (void)loading_work->progress(hlclient::app::LoadingStage::scene);
            }
            return created;
          }));
      asset_job_started = true;
      asset_preparation_started_at = Clock::now();
    }
    if (asset_future && !local_assets &&
        asset_future->wait_for(std::chrono::milliseconds{0}) ==
            std::future_status::ready) {
      auto created = asset_future->get();
      if (!created.projection) {
        std::string_view failure_reason="missing_or_invalid_map_resources";
        if(created.error && created.error->context.starts_with("map_crc_mismatch:"))
          failure_reason="map_crc_mismatch";
        else if(created.error && created.error->context.starts_with("map_crc_invalid_bsp:"))
          failure_reason="map_crc_invalid_bsp";
        if(options.external_server_client) {
          auto& diagnostic=receipts.external_diagnostics();
          diagnostic.reason=failure_reason;
          for(const auto& issue:created.resource_issues)
            diagnostic.resource(issue.virtual_name,issue.reason,issue.resource_index,issue.resource_type);
          diagnostic.omitted+=created.resource_issues_omitted;
          std::string_view status="asset_preparation_failed";
          if(created.error && created.error->context.starts_with("map_resource_not_ready: virtual=")) {
            const auto marker=created.error->context.find("; status=");
            if(marker!=std::string::npos) {
              const auto candidate=std::string_view{created.error->context}.substr(marker+9U);
              if(hlclient::app::external_diagnostic_token(candidate))status=candidate;
            }
          }
          if(failure_reason!="missing_or_invalid_map_resources")
            diagnostic.resource(diagnostic.map,failure_reason,0,2);
          else if(created.resource_issues.empty())diagnostic.resource(diagnostic.map,status,0,0);
        }
        external_status("error",failure_reason);
        throw std::runtime_error{
            created.error ? created.error->context
                          : "Live local asset preparation returned no projection"};
      }
      local_assets = std::move(created.projection);
      model_preparation_cache.invalidate();
      std::cout << "[local-resource-validation] policy=live_standard_hl"
          << " size_matches=" << local_assets->summary().size_matches
          << " size_unavailable=" << local_assets->summary().size_unavailable
          << " size_mismatches=" << local_assets->summary().size_mismatches
          << " world_crc_verified=" << local_assets->summary().world_crc_verified
          << '\n' << std::flush;
      std::cout<<"[skybox] status="<<hlclient::goldsrc::to_string(local_assets->skybox_status())
          <<" ready="<<static_cast<bool>(local_assets->skybox())<<'\n';
      const auto& muzzle_resources=local_assets->muzzle_sprite_statistics();
      std::cout<<"[muzzle-sprites] ready="<<muzzle_resources.ready
          <<" missing="<<muzzle_resources.missing<<" rejected="<<muzzle_resources.rejected<<'\n';
      if(options.external_server_client) {
        auto& diagnostic=receipts.external_diagnostics();
        for(const auto& issue:local_assets->summary().resource_issues) {
          diagnostic.resource(issue.virtual_name,issue.reason,issue.resource_index,issue.resource_type);
          std::cout<<"[external-resource] virtual="<<hlclient::core::external_json_string(issue.virtual_name)
            <<" reason="<<hlclient::core::external_json_string(issue.reason)
            <<" type="<<static_cast<unsigned>(issue.resource_type)<<" index="<<issue.resource_index<<'\n';
        }
        diagnostic.omitted+=local_assets->summary().resource_issues_omitted;
        diagnostic.missing_texture_placeholders=local_assets->summary().missing_texture_placeholder_bindings;
        std::cout<<"[external-resource] omitted="<<local_assets->summary().resource_issues_omitted
          <<" missing_texture_placeholders="<<local_assets->summary().missing_texture_placeholder_bindings<<'\n'<<std::flush;
      }
      hud_assets=std::make_unique<hlclient::goldsrc::ApprovedHudAssets>(
          local_assets->sound_resources(),game_client->hud_resource_preparation(),game_client->text_message_preparation());
      if (local_assets->summary().missing_texture_placeholder_bindings != 0U) {
        hlclient::core::log(LogLevel::warning,
            "[texture] missing_texture_placeholder bindings=" +
            std::to_string(local_assets->summary().missing_texture_placeholder_bindings) +
            "; project-generated checkerboard; source textures remain incomplete");
      }
      if (keyboard) {
        sound_assets=std::make_unique<hlclient::goldsrc::ApprovedSoundAssets>(local_assets->sound_resources());
        for(const auto& reference:game_client->movement_sound_preparation())
          (void)sound_assets->request_local(reference);
        for(const auto& reference:game_client->temporary_sound_preparation())
          (void)sound_assets->request_local(reference);
        for(const auto& reference:game_client->hud_sound_preparation())
          (void)sound_assets->request_local(reference);
        // Client-effect samples have no server precache slot. Begin their
        // exact-root verified loads at projection readiness, before fire.
        if (const auto profile=game_client->local_impact_assets()) {
          (void)sound_assets->request_local(profile->sound);
          (void)sound_assets->request_local(profile->shell_contact_sound);
          for(std::size_t i=0;i<profile->supplemental_sound_count;++i)
            (void)sound_assets->request_local(profile->supplemental_sounds[i]);
          for(std::size_t i=0;i<profile->material_sound_count;++i)
            (void)sound_assets->request_local(profile->material_sounds[i]);
        }
        if (const auto profile=game_client->local_crowbar_impact_assets()) {
          for (const auto& sound:profile->strike_sounds)
            (void)sound_assets->request_local(sound);
          (void)sound_assets->request_local(profile->concrete_contact_sound);
        }
      }
      asset_preparation_completed_at = Clock::now();
    }

    if(hud_assets && !prepared_hud_assets)prepared_hud_assets=hud_assets->snapshot();
    if(prepared_hud_assets && hud_binding_reset_serial!=game_client->reset_serial()) {
      if(!game_client->bind_hud_resources(prepared_hud_assets->resources))
        throw std::runtime_error{"Approved HUD metadata violated its bounded contract"};
      if(!game_client->bind_text_messages(prepared_hud_assets->text_messages))
        throw std::runtime_error{"Approved text message metadata violated its bounded contract"};
      hud_binding_reset_serial=game_client->reset_serial();
      if(options.net_trace)std::cout<<"[hud] preparation=complete descriptors="
          <<prepared_hud_assets->resources.descriptors.size()<<" ready="<<prepared_hud_assets->ready_entries
          <<" rejected="<<prepared_hud_assets->rejected_entries<<" dependencies="<<prepared_hud_assets->sprites.size()<<'\n';
    }
    if(movement_diagnostics_enabled)movement_frame.asset_ms=movement_elapsed_ms(movement_assets_started,Clock::now());
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
        game_client->configure_movement_materials(local_assets->movement_materials());
        game_client->bind_effect_palette(local_assets->effect_palette());
        game_client->configure_effect_policy({options.server_blood_presentation});
        if (options.reference_prediction &&
            local_assets->collision_world_package()) {
          prediction_collision_attached =
              session.attach_reference_prediction_collision(
                  local_assets->collision_world_package());
          if (prediction_collision_attached && local_assets->surface_scene())
            (void)session.attach_reference_prediction_surfaces(
                local_assets->surface_scene());
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
          // Retain every authoritative lifetime/history boundary here. The
          // camera-selected presentation below builds the only render frame.
          const auto projected = local_assets->observe_entities(*observation);
          if (!projected) {
            ++visual_failures;
            throw std::runtime_error{
                projected.error ? projected.error->context
                                : "Live entity observation failed"};
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
      if(movement_diagnostics_enabled) {
        using Owner=hlclient::app::MovementCaptureOwnerState;
        // Only a real producer snapshot with an established receipt generation
        // can establish delivery coverage; a synthetic prediction-Off default
        // must remain not_observed instead of implying complete delivery.
        if(options.reference_prediction)
          if(const auto identity=receipts.snapshot();identity && identity->generation.network_generation && identity->generation.map_generation)
            receipts.movement().observe_correction_delivery(identity->generation.network_generation,identity->generation.map_generation,
                prediction.correction_events_published,prediction.correction_sink_failures);
        switch(prediction.state) {
        case hlclient::goldsrc::LiveReferencePredictionState::off:movement_context.prediction_owner_state=Owner::unknown;break;
        case hlclient::goldsrc::LiveReferencePredictionState::waiting_for_seed:movement_context.prediction_owner_state=Owner::waiting;break;
        case hlclient::goldsrc::LiveReferencePredictionState::active:movement_context.prediction_owner_state=Owner::active;break;
        case hlclient::goldsrc::LiveReferencePredictionState::suspended:movement_context.prediction_owner_state=Owner::suspended;break;
        case hlclient::goldsrc::LiveReferencePredictionState::failed:movement_context.prediction_owner_state=Owner::failed;break;
        }
        if(prediction.velocity_stop) {
          const auto& captured=*prediction.velocity_stop;
          const auto& stop=captured.transition;
          auto& value=movement_velocity_stop.emplace();
          const auto numeric=[](const auto& v){return std::array<float,3>{v.x,v.y,v.z};};
          const auto plane=[&](const auto& p){return hlclient::app::MovementStopPlane{
              numeric(p.normal),p.distance,p.source_plane_index};};
          value.capture_ordinal=captured.capture_ordinal;
          value.network_generation=captured.network_generation;value.map_generation=captured.map_generation;
          value.life_epoch=captured.life_epoch;value.life_deaths=captured.life_deaths;
          value.observed_us=receipts.movement_stamp_ns(captured.observed_ns);
          if(captured.command_sample_ns && *captured.command_sample_ns>=0)
            value.command_sample_us=receipts.movement_stamp_ns(static_cast<std::uint64_t>(*captured.command_sample_ns));
          value.prediction_epoch=captured.prediction_epoch;
          value.source_record_identity=captured.source_record_identity;value.source_sequence=captured.source_sequence;
          value.source_server_time_s=captured.source_server_time;value.source_freshness=captured.source_freshness;
          value.scene_record_identity=captured.scene_record_identity;value.scene_revision=captured.scene_revision;
          value.scene_server_time_s=captured.scene_server_time;value.source_matches_scene=captured.source_matches_scene;
          value.history_anchor_command=captured.history_anchor_command;value.history_latest_command=captured.history_latest_command;
          value.predecessor_decision=captured.predecessor_decision;value.replay=captured.replay;
          value.command_sequence=stop.command_sequence;value.command_msec=stop.command_msec;
          if(captured.command) {
            value.command_known=true;value.command_buttons=captured.command->buttons;
            value.command_angle_turns=captured.command->angle_turns;
            value.command_movement={captured.command->forward,captured.command->side,captured.command->up};
          }
          if(stop.collision_identity) {
            value.query_world_revision=stop.collision_identity->collision_world_revision;
            value.query_scene_signature=stop.collision_identity->scene_signature;
          }
          value.simulation_start_ns=stop.simulation_start_ns;value.simulation_end_ns=stop.simulation_end_ns;
          value.phase=static_cast<std::uint8_t>(stop.phase);value.reason=static_cast<std::uint8_t>(stop.reason);
          value.candidate=static_cast<std::uint8_t>(stop.candidate);value.hull=static_cast<std::uint8_t>(stop.hull);
          value.before_origin=numeric(stop.before_origin);value.after_origin=numeric(stop.after_origin);
          value.before_velocity=numeric(stop.before_velocity);value.after_velocity=numeric(stop.after_velocity);
          value.query_start=numeric(stop.query_start);value.query_end=numeric(stop.query_end);
          value.trace_end=numeric(stop.trace.end_position);value.trace_fraction=stop.trace.fraction;
          value.startsolid=stop.trace.start_solid;value.allsolid=stop.trace.all_solid;
          value.in_open=stop.trace.in_open;value.in_liquid=stop.trace.in_liquid;
          value.collision_profile=static_cast<std::uint8_t>(stop.trace.collision_profile);
          value.start_contents=stop.trace.start_contents.source_goldsrc_code;
          value.end_contents=stop.trace.end_contents.source_goldsrc_code;
          if(stop.trace.blocking_contents)value.blocking_contents=stop.trace.blocking_contents->source_goldsrc_code;
          if(stop.trace.hit) {
            value.hit_kind=static_cast<std::uint8_t>(stop.trace.hit->kind);
            value.hit_entity=stop.trace.hit->source_entity_index;value.hit_model=stop.trace.hit->source_model_index;
            value.hit_instance=stop.trace.hit->stable_instance_ordinal;
          }
          if(stop.trace.collision_plane)value.hit_plane=plane(*stop.trace.collision_plane);
          value.clip_plane_count=stop.clip_plane_count;
          for(std::size_t i=0;i<std::min(value.clip_planes.size(),static_cast<std::size_t>(stop.clip_plane_count));++i)
            value.clip_planes[i]=plane(stop.clip_planes[i]);
        }
        const auto vector=[](const hlclient::assets::AssetVector3& v) {
          return hlclient::app::MovementDiagnosticVector3{v.x,v.y,v.z};
        };
        const auto hit_kind=[](const hlclient::movement::PlayerMovementHitKind kind)->std::string_view {
          using K=hlclient::movement::PlayerMovementHitKind;
          switch(kind) {
          case K::world:return "world";case K::explicit_synthetic_brush:return "explicit_synthetic_brush";
          case K::brush_entity:return "brush_entity";case K::server_entity_box:return "server_entity_box";
          }
          return "unavailable";
        };
        movement_frame.network_generation=prediction.generation;
        if(prediction.state==hlclient::goldsrc::LiveReferencePredictionState::active)movement_frame.prediction_active=true;
        else if(control_activated && (prediction.state==hlclient::goldsrc::LiveReferencePredictionState::suspended ||
            prediction.state==hlclient::goldsrc::LiveReferencePredictionState::failed))movement_frame.prediction_active=false;
        if(prediction.accepted_corrections>0U) {
          movement_frame.raw_correction_units=prediction.last_raw_position_error;
          movement_frame.camera_jump_units=prediction.last_camera_correction_jump;
        }
        movement_frame.correction_ordinal=prediction.accepted_corrections;
        movement_frame.set_fallback_reason(prediction.reason);
        movement_frame.set_presentation_reason(prediction.presentation_reason);
        movement_frame.set_collision_query_purpose(prediction.collision_query_purpose);
        movement_frame.anchor_command=prediction.anchor_command;
        movement_frame.candidate_anchor_command=prediction.candidate_anchor_command;
        movement_frame.set_recovery_wait_reason(prediction.recovery_wait_reason);
        if(prediction.life_wait_started_ns && *prediction.life_wait_started_ns>=0) {
          movement_frame.life_wait_started_us=receipts.movement_stamp_ns(
              static_cast<std::uint64_t>(*prediction.life_wait_started_ns));
          // The owner freezes this duration on the first successful seed.
          // Do not recompute it from frame time or include the dead interval.
          movement_frame.life_wait_duration_ms=prediction.life_wait_duration_ms;
        }
        if(prediction.last_anchor_status)movement_frame.anchor_status=static_cast<std::uint8_t>(*prediction.last_anchor_status);
        movement_frame.authoritative_source_record_identity=prediction.canonical_source_record_identity;
        movement_frame.authoritative_source_sequence=prediction.canonical_source_sequence;
        movement_frame.set_authoritative_body_kind(prediction.canonical_body_kind);
        if(prediction.canonical_velocity)movement_frame.authoritative_velocity=hlclient::app::MovementDiagnosticFloat3{
            prediction.canonical_velocity->x,prediction.canonical_velocity->y,prediction.canonical_velocity->z};
        if(prediction.matched_origin_before_correction)movement_frame.matched_origin_before_correction=hlclient::app::MovementDiagnosticFloat3{
            prediction.matched_origin_before_correction->x,prediction.matched_origin_before_correction->y,prediction.matched_origin_before_correction->z};
        if(prediction.matched_velocity_before_correction)movement_frame.matched_velocity_before_correction=hlclient::app::MovementDiagnosticFloat3{
            prediction.matched_velocity_before_correction->x,prediction.matched_velocity_before_correction->y,prediction.matched_velocity_before_correction->z};
        if(prediction.reason=="reference_rebase_or_suffix_failed")movement_frame.set_fallback_reason(
            prediction.last_simulation_error ? hlclient::goldsrc::movement::to_string(*prediction.last_simulation_error) : prediction.rebase_reason);
        if(prediction.anchor_command)movement_frame.set_correction_anchor_provenance(
            prediction.anchor_coherent_reliable_frame ? "coherent_movement_frame_reference_profile" :
                "exact_sent_carrier_receipt_reference_profile");
        if(prediction.canonical_origin)movement_frame.raw_server_origin=vector(*prediction.canonical_origin);
        if(prediction.predicted_origin)movement_frame.predicted_origin=vector(*prediction.predicted_origin);
        if(prediction.predicted_velocity)movement_frame.velocity=vector(*prediction.predicted_velocity);
        if(prediction.ground_normal)movement_frame.plane_normal=vector(*prediction.ground_normal);
        if(prediction.predicted_hull)movement_frame.hull=static_cast<std::uint32_t>(*prediction.predicted_hull);
        movement_frame.grounded=prediction.local_grounded;
        movement_frame.collision_revision=prediction.collision_context_revision;
        if (prediction.collision_context_revision)
          movement_frame.excluded_point_boxes=static_cast<std::uint16_t>(prediction.collision_excluded_point_boxes);
        movement_frame.command_time_s=prediction.command_simulation_seconds;
        if(prediction.render_sample_time_ns && *prediction.render_sample_time_ns>=0) {
          if(const auto sample=receipts.movement_stamp_ns(static_cast<std::uint64_t>(*prediction.render_sample_time_ns));sample)
            movement_frame.render_sample_time_s=static_cast<double>(*sample)/1'000'000.0;
        }
        movement_frame.support_observation_s=prediction.support_observation_seconds;
        movement_frame.support_velocity_z=prediction.support_velocity_z;
        if(prediction.seed_origin)movement_frame.seed_origin=vector(*prediction.seed_origin);
        if(prediction.previous_valid_origin)movement_frame.previous_valid_origin=vector(*prediction.previous_valid_origin);
        if(prediction.collision_query_start)movement_frame.collision_query_start=vector(*prediction.collision_query_start);
        if(prediction.collision_query_end)movement_frame.collision_query_end=vector(*prediction.collision_query_end);
        if(prediction.collision_query_hull)movement_frame.hull=static_cast<std::uint32_t>(*prediction.collision_query_hull);
        if(prediction.collision_trace_evidence) {
          const auto& trace=*prediction.collision_trace_evidence;
          movement_frame.plane_normal.reset();
          movement_frame.startsolid=trace.start_solid;movement_frame.allsolid=trace.all_solid;
          movement_frame.trace_fraction=trace.fraction;
          if(trace.collision_plane) {
            movement_frame.plane_normal=vector(trace.collision_plane->normal);
            movement_frame.collision_plane_index=trace.collision_plane->source_plane_index;
            movement_frame.collision_plane_distance=static_cast<float>(trace.collision_plane->distance);
          }
          if(trace.hit) {
            movement_frame.blocking_entity=trace.hit->source_entity_index;
            movement_frame.set_blocker_kind(hit_kind(trace.hit->kind));
          }
        } else if(prediction.collision_position_evidence) {
          const auto& position=*prediction.collision_position_evidence;
          // A position test establishes embedding, not a collision plane.
          movement_frame.plane_normal.reset();
          movement_frame.startsolid=position.status==hlclient::goldsrc::movement::LocalMovementPositionStatus::blocking;
          if(position.hit) {
            movement_frame.blocking_entity=position.hit->source_entity_index;
            movement_frame.set_blocker_kind(hit_kind(position.hit->kind));
          }
        }
        if(prediction.ground_hit) {
          movement_frame.support_entity=prediction.ground_hit->source_entity_index;
          if(prediction.ground_hit->kind==hlclient::movement::PlayerMovementHitKind::brush_entity)
            movement_frame.support_submodel=prediction.ground_hit->source_model_index;
        }
        if(observation && session.live_server_info()) {
          const auto receiver=static_cast<std::uint32_t>(session.live_server_info()->client_slot())+1U;
          for(const auto& entity:observation->packet_entities) {
            if(movement_frame.support_entity==entity.entity_number)movement_frame.support_model_index=entity.model_index;
            if(entity.entity_number==receiver && entity.base_velocity.complete())
              movement_frame.basevelocity=hlclient::app::MovementDiagnosticVector3{
                  *entity.base_velocity.x,*entity.base_velocity.y,*entity.base_velocity.z};
          }
        }
        if(movement_frame.prediction_active && !*movement_frame.prediction_active && control_activated) {
          if(!movement_fallback_started)movement_fallback_started=now;
          movement_frame.fallback_duration_ms=movement_elapsed_ms(*movement_fallback_started,now);
        } else movement_fallback_started.reset();
      }
      std::optional<hlclient::app::LiveVisualPredictedView> predicted_view;
      if (prediction.state ==
              hlclient::goldsrc::LiveReferencePredictionState::active &&
          prediction.presented_origin && prediction.presented_view_offset) {
        predicted_view = hlclient::app::LiveVisualPredictedView{
            *prediction.presented_origin, *prediction.presented_view_offset};
      }
      if (options.reference_prediction && control_activated) {
        const bool h4_scripted = jump_duck_check && input_activated_at;
        std::size_t h4_phase = 0U;
        if (h4_scripted) {
          const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
              now - *input_activated_at);
          h4_phase = age < std::chrono::milliseconds{2000} ? 0U
              : age < std::chrono::milliseconds{3200} ? 1U
              : age < std::chrono::milliseconds{5200} ? 2U
              : age < std::chrono::milliseconds{6700} ? 3U : 4U;
        }
        if (last_prediction_frame_at) {
          const auto elapsed_ms = std::chrono::duration<double, std::milli>{
              now - *last_prediction_frame_at}.count();
          if (elapsed_ms >= 0.0 && elapsed_ms <= 250.0) {
            if (last_prediction_frame_active)
              prediction_active_time_ms += elapsed_ms;
            else
              prediction_fallback_time_ms += elapsed_ms;
            if (h4_scripted && h4_last_phase) {
              if (last_prediction_frame_active)
                h4_active_ms[*h4_last_phase] += elapsed_ms;
              else
                h4_fallback_ms[*h4_last_phase] += elapsed_ms;
            }
          } else {
            ++prediction_long_stall_frames;
          }
        }
        last_prediction_frame_at = now;
        last_prediction_frame_active = predicted_view.has_value();
        if (h4_scripted) {
          h4_last_phase = h4_phase;
          h4_local_steps[h4_phase] +=
              prediction.local_steps >= h4_last_steps
                  ? prediction.local_steps - h4_last_steps : 0U;
          h4_corrections[h4_phase] +=
              prediction.accepted_corrections >= h4_last_corrections
                  ? prediction.accepted_corrections - h4_last_corrections : 0U;
          h4_last_steps = prediction.local_steps;
          h4_last_corrections = prediction.accepted_corrections;
          if (predicted_view) ++h4_active_frames[h4_phase];
          else {
            ++h4_fallback_frames[h4_phase];
            h4_last_fallback_reason[h4_phase] = prediction.reason;
          }
          if (predicted_view && h4_phase == 1U &&
              prediction.predicted_mode ==
                  hlclient::movement::PlayerMovementMode::airborne &&
              prediction.predicted_velocity &&
              prediction.predicted_velocity->z > 0.0F)
            ++h4_jump_rising_frames;
          if (predicted_view && h4_phase == 3U &&
              prediction.predicted_in_duck_transition.value_or(false))
            ++h4_duck_transition_frames;
          if (predicted_view && h4_phase == 3U &&
              prediction.predicted_hull ==
                  hlclient::movement::PlayerMovementHull::ducked) {
            ++h4_stable_crouch_frames;
            if (prediction.predicted_origin) {
              const std::array<float, 2U> xy{prediction.predicted_origin->x,
                                             prediction.predicted_origin->y};
              if (h4_last_predicted_crouch_xy &&
                  xy != *h4_last_predicted_crouch_xy)
                ++h4_crouch_predicted_horizontal_changes;
              h4_last_predicted_crouch_xy = xy;
            }
          } else {
            h4_last_predicted_crouch_xy.reset();
          }
          if (predicted_view && h4_phase == 4U &&
              prediction.predicted_hull ==
                  hlclient::movement::PlayerMovementHull::standing)
            ++h4_standing_recovery_frames;
          if (h4_phase == 3U && observation &&
              observation->client_metadata.freshness ==
                  hlclient::client::RuntimeObservationFreshness::observed_in_record &&
              observation->client_metadata.source &&
              observation->receiving_client &&
              observation->receiving_client->origin.x &&
              observation->receiving_client->origin.y &&
              observation->receiving_client->flags &&
              (*observation->receiving_client->flags & (1U << 14U)) != 0U &&
              (!h4_last_server_crouch_source ||
               *h4_last_server_crouch_source !=
                   *observation->client_metadata.source)) {
            const std::array<double, 2U> xy{
                *observation->receiving_client->origin.x,
                *observation->receiving_client->origin.y};
            if (h4_last_server_crouch_xy &&
                xy != *h4_last_server_crouch_xy)
              ++h4_crouch_server_horizontal_changes;
            h4_last_server_crouch_xy = xy;
            h4_last_server_crouch_source =
                *observation->client_metadata.source;
          }
        }
        if (predicted_view) {
          ++prediction_active_frames;
          prediction_presentation_cpu_total_ms += prediction.presentation_cpu_ms;
          prediction_presentation_cpu_max_ms = std::max(
              prediction_presentation_cpu_max_ms, prediction.presentation_cpu_ms);
          if (prediction.presentation_reason == "interpolated")
            ++prediction_interpolated_frames;
          else if (prediction.presentation_reason == "collision_blocked")
            ++prediction_collision_blocked_frames;
          else if (prediction.presentation_reason == "visual_correction")
            ++prediction_visual_correction_frames;
          else
            ++prediction_endpoint_frames;
          if (prediction.accepted_corrections != prediction_last_correction_count) {
            prediction_correction_window[
                prediction_correction_window_count % prediction_correction_window.size()] =
                prediction;
            ++prediction_correction_window_count;
            prediction_last_correction_count = prediction.accepted_corrections;
          }
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
          if (fire_reload_check && input_activated_at) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *input_activated_at);
            moving_window = age < std::chrono::milliseconds{1200} ||
                (age >= std::chrono::milliseconds{6500} &&
                 age < std::chrono::milliseconds{7000});
          }
          if (h4_scripted)
            moving_window = h4_phase == 1U || h4_phase == 3U ||
                            h4_phase == 4U;
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
      const auto camera_intent=scene_source.world_state().runtime_observation()
          ? game_client->camera(*scene_source.world_state().runtime_observation(),weapon_presentation.local_punch_pitch_degrees)
          : hlclient::game_api::CameraIntent{};
      const auto camera_update=camera_controller.update(
          scene_source.mutable_world_state().runtime_observation().get(),
          intent, scene_source.mutable_world_state(),
          camera_intent,
          false, predicted_view, window.pixel_extent().height>0 ?
              static_cast<double>(window.pixel_extent().width)/window.pixel_extent().height : 4.0/3.0,
          presentation_seconds);
      observe_camera(camera_update);
      if(movement_diagnostics_enabled && camera_update && camera_update.sample) {
        const auto& sample=*camera_update.sample;
        movement_frame.camera_base_angles=hlclient::app::MovementDiagnosticFloat3{
            static_cast<float>(sample.local_pitch_degrees),static_cast<float>(sample.local_yaw_degrees),0.0F};
        if(sample.server_punch_angle)movement_frame.server_punch_angles=hlclient::app::MovementDiagnosticFloat3{
            sample.server_punch_angle->x,sample.server_punch_angle->y,sample.server_punch_angle->z};
        movement_frame.local_punch_angles=hlclient::app::MovementDiagnosticFloat3{
            static_cast<float>(camera_intent.local_pitch_offset_degrees),0.0F,0.0F};
        movement_frame.camera_final_angles=hlclient::app::MovementDiagnosticFloat3{
            sample.presented_angles_degrees.x,sample.presented_angles_degrees.y,sample.presented_angles_degrees.z};
      }
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
    // Only the already committed owning world attachment may stamp map entry.
    // Decoder callbacks above record narrow sign-on facts, never fabricated
    // publication/readiness from a record which later rolls back.
    if(const auto& committed=scene_source.world_state().runtime_observation();
        committed && committed->generation && session.live_server_info()) {
      // One-shot coordinator owns exactly one established channel epoch (1).
      // Map epoch is separately derived from actual serverinfo, not replay/life.
      receipts.generation(1U,static_cast<std::uint64_t>(session.live_server_info()->server_count())+1U,
          session.live_server_info()->map_file_path());
      receipts.receiving_entity(static_cast<std::uint8_t>(session.live_server_info()->client_slot()+1U));
      const auto commands=session.live_usercmd_receipt_counters();
      hlclient::app::SessionReceiptCounters counters;
      if(commands) {
        counters.new_usercmd_count=commands->new_command_submission_count;
        counters.transmitted_usercmd_packet_count=commands->transmitted_packet_count;
        counters.server_sample_count=commands->server_sample_count;
      }
      const auto plan=hlclient::app::project_session_receipt_commit({true,
          committed->receiving_client && committed->receiving_client->origin.complete(),
          scene_source.world_state().runtime_publication_revision(),
          assets_installed && renderer_resources_presented && successful_presentations>0U,
          control_activated,counters});
      for(const auto fact:{hlclient::app::SessionReceiptFact::world_published,
          hlclient::app::SessionReceiptFact::usercmd_transmitted,
          hlclient::app::SessionReceiptFact::gameplay_committed})
        if(plan.fact_mask & hlclient::app::receipt_fact(fact)) receipt_fact_observed(fact);
      receipts.facts(counters);
      const auto observed=receipts.stamp({},scene_source.world_state().runtime_publication_revision());
      receipts.milestone(hlclient::app::SessionReceiptStage::connection_established,observed);
      receipts.milestone(hlclient::app::SessionReceiptStage::signon_complete,observed);
      if(plan.map_entry_committed)
        receipts.milestone(hlclient::app::SessionReceiptStage::map_entry_committed,observed);
      if(plan.world_ready)
        receipts.milestone(hlclient::app::SessionReceiptStage::world_ready,observed);
      if(plan.first_gameplay_update) {
        receipts.milestone(hlclient::app::SessionReceiptStage::first_gameplay_update,observed);
        external_gameplay_committed=true;
        external_status("in_game");
      }
    } else receipts.facts();
    // Drain committed slot boundaries before visual sampling, never after
    // rendering an old occupant. Audio still consumes each event exactly once.
    if (keyboard && !session.live_runtime_error()) {
      if(sound_events->take_overflow()) {
        if (local_assets) local_assets->invalidate_player_continuity();
        for(std::uint32_t entity=1;entity<=hlclient::game_api::kMaximumRemotePlayers;++entity)
          game_client->remote_slot_boundary(entity);
        server_audio.reset(0); sound_events->reset();
      }
      hlclient::goldsrc::CommittedSound sound_event;
      while(sound_events->pop(sound_event)) {
        if (sound_event.opcode==hlclient::goldsrc::RuntimeControlOpcode::svc_updateuserinfo) {
          if(sound_event.retire_source && sound_event.sound.entity_reference<=32U)
            receipts.peer_removed(static_cast<std::uint8_t>(sound_event.sound.entity_reference),sound_event.ordinal);
          if(local_assets) local_assets->player_slot_boundary(sound_event.sound.entity_reference);
          game_client->remote_slot_boundary(sound_event.sound.entity_reference);
        }
        server_audio.consume(sound_event,now);
        if(options.net_trace && sound_assets && sound_event.sound.sound_reference<effect_sample_reported.size()) {
          const auto slot=sound_event.sound.sound_reference;
          const auto name=sound_assets->virtual_name(slot);
          if(!effect_sample_reported[slot] && !name.empty()) {
            effect_sample_reported[slot]=true;
            hlclient::core::log(LogLevel::info,"[effect-sample] slot="+std::to_string(slot)+
                " fingerprint="+std::to_string(hlclient::goldsrc::effect_sample_fingerprint(name)));
          }
        }
        if(options.net_trace && server_audio_trace_budget) {
          --server_audio_trace_budget;
          const auto receiving=session.live_server_info() ? static_cast<std::uint32_t>(session.live_server_info()->client_slot())+1U : 0U;
          std::cout << "[server-audio] committed generation=" << sound_event.generation
            << " record=" << sound_event.record << " ordinal=" << sound_event.ordinal << " cursor=" << sound_event.cursor
            << " source=" << sound_event.sound.entity_reference
            << " class=" << (sound_event.sound.entity_reference==0 ? "world-or-unknown" : sound_event.sound.entity_reference==receiving ? "local-server" : "other-entity")
            << " channel=" << unsigned(sound_event.sound.channel) << " sample-index=" << sound_event.sound.sound_reference
            << " virtual-resource=" << (sound_assets ? sound_assets->virtual_name(sound_event.sound.sound_reference) : "unbound")
            << " origin=server-event(" << sound_event.sound.origin[0] << ',' << sound_event.sound.origin[1] << ',' << sound_event.sound.origin[2]
            << ") arbitration=delivered-event-no-echo-inference\n";
        }
      }
    }
    const auto entity_prediction_snapshot=session.live_reference_prediction_snapshot(now);
    const auto& entity_brush_presentation=entity_prediction_snapshot.presented_brush_scene;
    if (local_assets && assets_installed && session.live_server_info()) {
      const auto movement_studio_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
      if(movement_diagnostics_enabled) {
        const auto diagnostic_identity=receipts.snapshot();
        local_assets->set_diagnostic_sink(receipts.diagnostic_sink(),diagnostic_identity?diagnostic_identity->generation.map_generation:0U);
      }
      const auto presented=local_assets->present_entities(*game_client,presentation_seconds,
          static_cast<std::uint32_t>(session.live_server_info()->client_slot())+1U,
          scene_source.mutable_world_state(),extent,movement_diagnostics_enabled,entity_brush_presentation);
      if (!presented) throw std::runtime_error{presented.error ? presented.error->context :
          "Remote player presentation failed"};
      if(movement_diagnostics_enabled)movement_frame.studio_ms=movement_elapsed_ms(movement_studio_started,Clock::now());
      if (options.net_trace) {
        const auto& trace_world=scene_source.world_state();
        if (const auto& trace_observation=trace_world.runtime_observation(); trace_observation) {
          const auto trace_receiving=static_cast<std::uint32_t>(session.live_server_info()->client_slot())+1U;
          const auto trace_maximum=std::min<std::uint32_t>(
              session.live_server_info()->maximum_clients().value(),
              static_cast<std::uint32_t>(hlclient::game_api::kMaximumRemotePlayers));
          const auto trace_camera=hlclient::client::build_render_scene(trace_world).camera;
          for (std::uint32_t trace_entity=1U;trace_entity<=trace_maximum;++trace_entity) {
            if (trace_entity==trace_receiving) continue;
            const auto trace_sample=hlclient::app::build_remote_player_visibility_sample(
                trace_entity,*trace_observation,local_assets->summary(),trace_world.entity_frame().get(),trace_camera);
            if (trace_sample) (void)remote_visibility_journal.observe(*trace_sample);
          }
        }
      }
      receipts.observe_appearance(local_assets->summary(),successful_presentations+1U);
      if(options.net_trace && remote_visual_trace_budget) {
        const auto& summary=local_assets->summary();
        for(std::size_t i=0;i<summary.remote_player_count && remote_visual_trace_budget;++i) {
          --remote_visual_trace_budget;
          const auto& player=summary.remote_players[i]; const auto& remote_intent=player.intent;
          std::cout << "[remote-player-visual] entity=" << player.entity << " model-slot=" << player.model_slot
            << " role=" << (player.server_owned_proxy ? "server-owned-proxy" : "player")
            << " source-player=" << player.source_player.value_or(0U)
            << " appearance-status=" << hlclient::game_api::to_string(player.appearance.status)
            << " occupant=" << player.appearance.occupant_epoch << " appearance-revision=" << player.appearance.appearance_revision
            << " requested-model=" << player.appearance.model_name << " resolved-model=" << player.resolved_visual_model
            << " visual-resource=" << player.visual_model_resource_id << " palette-remapped=" << player.palette_remapped
            << " network=" << player.network_generation << " map=" << player.map_generation
            << " lifetime=" << player.entity_lifetime << " visual-epoch=" << remote_intent.visual_epoch
            << " classification=" << hlclient::game_api::to_string(player.classification)
            << " renderfx=" << player.render_effect.value_or(0U) << " renderamt=" << player.render_amount.value_or(0U)
            << " effects=" << player.effects << " nointerp=" << player.no_interpolation
            << " death-notice-observed=" << remote_intent.death_notice_source.has_value()
            << " status=" << static_cast<unsigned>(player.status)
            << " time-domain=public-svc-time-seconds pair=" << summary.remote_previous_seconds << ',' << summary.remote_current_seconds
            << " sample=" << summary.remote_sample_seconds << " alpha=" << summary.remote_alpha
            << " main=" << remote_intent.sample.sequence << ':' << remote_intent.sample.frame_coordinate
            << " gait=" << (remote_intent.gait_sample ? std::to_string(remote_intent.gait_sample->sequence) : "unavailable")
            << ':' << (remote_intent.gait_sample ? remote_intent.gait_sample->frame_coordinate : 0.0)
            << " gait-yaw=" << remote_intent.gait_yaw_degrees << " pitch=" << remote_intent.transform_angles[0]
            << " blend=" << unsigned(remote_intent.sample.blending[0]) << ',' << unsigned(remote_intent.sample.blending[1])
            << " body=" << remote_intent.sample.body << " skin=" << remote_intent.sample.skin
            << " transition=" << remote_intent.transition_identity << " previous-weight=" << remote_intent.previous_weight
            << " pose=" << player.pose_submitted << " visible=" << player.visible
            << " attachment-slot=" << remote_intent.attachment_model_index.value_or(0U)
            << " attachment-status=" << hlclient::app::to_string(player.attachment_status)
            << " matched-bones=" << player.attachment_matched_bones
            << " unmatched-bones=" << player.attachment_bone_count-player.attachment_matched_bones
            << " static-light=" << (player.static_light ? "sampled" : "legacy-fallback") << '\n';
        }
      }
    }
    auto render_scene = hlclient::client::build_render_scene(
        scene_source.world_state());
    if(local_assets)render_scene.skybox=local_assets->skybox();
    hlclient::app::apply_application_scene_settings(render_scene,options);
    const auto visual_now_seconds=std::chrono::duration<double>{
        std::chrono::steady_clock::now()-session_started}.count();
    const auto visual_forward_delta=hlclient::assets::AssetVector3{
        render_scene.camera.target.x-render_scene.camera.position.x,
        render_scene.camera.target.y-render_scene.camera.position.y,
        render_scene.camera.target.z-render_scene.camera.position.z};
    const auto visual_forward_length=std::sqrt(
        visual_forward_delta.x*visual_forward_delta.x+
        visual_forward_delta.y*visual_forward_delta.y+
        visual_forward_delta.z*visual_forward_delta.z);
    const auto visual_horizontal_length=std::hypot(visual_forward_delta.x,
        visual_forward_delta.y);
    hlclient::game_api::LocalVisualContext visual_context;
    visual_context.eye=render_scene.camera.position;
    visual_context.now_seconds=visual_now_seconds;
    if (visual_forward_length>0.0001F && visual_horizontal_length>0.0001F) {
      visual_context.forward={visual_forward_delta.x/visual_forward_length,
          visual_forward_delta.y/visual_forward_length,
          visual_forward_delta.z/visual_forward_length};
      visual_context.right={visual_forward_delta.y/visual_horizontal_length,
          -visual_forward_delta.x/visual_horizontal_length,0.0F};
      const auto f=visual_context.forward, r=visual_context.right;
      visual_context.up={r.y*f.z-r.z*f.y,r.z*f.x-r.x*f.z,r.x*f.y-r.y*f.x};
    }
    if (const auto& current=scene_source.world_state().runtime_observation();
        current && current->receiving_client && current->receiving_client->velocity.complete()) {
      const auto& v=current->receiving_client->velocity;
      visual_context.velocity={static_cast<float>(*v.x),static_cast<float>(*v.y),
          static_cast<float>(*v.z)};
    }
    if (weapon_presentation.action &&
        *weapon_presentation.action==hlclient::game_api::LocalWeaponAction::primary_fire) {
      const auto predicted=session.live_reference_prediction_snapshot(now);
      if (predicted.predicted_velocity) visual_context.velocity=*predicted.predicted_velocity;
    }
    const auto& visual_observation=scene_source.world_state().runtime_observation();
    const auto observed_visual_generation=visual_observation
        ? visual_observation->generation : 0U;
    if (observed_visual_generation!=transient_visual_generation) {
      transient_visuals.reset();
      remote_effects.reset();
      temporary_effects.reset(observed_visual_generation);
      temporary_audio.reset(observed_visual_generation);
      world_impacts.reset();
      world_impacts.bind_server_resources(observed_visual_generation,++server_decal_resource_generation);
      crowbar_decal_cancelled_stale+=crowbar_impacts.pending();
      crowbar_impacts.reset();
      transient_visual_generation=observed_visual_generation;
    }
    if(observed_visual_generation!=0 && observed_visual_generation!=remote_effect_resource_generation &&
        session.live_resource_list()) {
        // Session-generation reset clears module metadata. Rebind the owning
        // approved game palette and explicit startup policy with the slots.
        if(local_assets && assets_installed) {
          game_client->bind_effect_palette(local_assets->effect_palette());
          game_client->configure_effect_policy({options.server_blood_presentation});
        }
        std::array<hlclient::game_api::ScriptedEventBinding,1024> event_bindings{};
        std::size_t event_binding_count{};
        for(const auto& resource:session.live_resource_list()->entries()) {
          // Non-bindable decal metadata must not alias the game effect palette.
          if(!hlclient::goldsrc::resource_has_bindable_index(
              resource.type(),resource.index().value())) continue;
          if(resource.type()==hlclient::goldsrc::ResourceType::model ||
              resource.type()==hlclient::goldsrc::ResourceType::sound ||
              resource.type()==hlclient::goldsrc::ResourceType::decal) {
            game_client->bind_effect_resource({resource.type()==hlclient::goldsrc::ResourceType::model ?
                hlclient::game_api::EffectResourceBinding::Kind::model :
                resource.type()==hlclient::goldsrc::ResourceType::sound ?
                    hlclient::game_api::EffectResourceBinding::Kind::sound : hlclient::game_api::EffectResourceBinding::Kind::decal,
                resource.index().value(),resource.name().bytes()});
          }
          if(resource.type()!=hlclient::goldsrc::ResourceType::event_script ||
              resource.name().bytes().size()>=event_bindings[0].name.size() ||
              event_binding_count==event_bindings.size()) continue;
          auto& binding=event_bindings[event_binding_count++];
          binding.index=resource.index().value();
          std::copy(resource.name().bytes().begin(),resource.name().bytes().end(),binding.name.begin());
        }
        game_client->configure_scripted_events({event_bindings.data(),event_binding_count});
        remote_effect_resource_generation=observed_visual_generation;
    }
    const auto current_impact_resource=local_assets && local_assets->surface_scene()
        ? std::optional{local_assets->surface_scene()->world_package()->resource_identity()}
        : std::nullopt;
    if (current_impact_resource!=world_impact_resource_identity) {
      world_impacts.reset();
      world_impacts.bind_server_resources(observed_visual_generation,++server_decal_resource_generation);
      temporary_effects.reset(observed_visual_generation);
      temporary_audio.reset(observed_visual_generation);
      remote_effects.reset();
      crowbar_decal_cancelled_stale+=crowbar_impacts.pending();
      crowbar_impacts.reset();
      world_impact_resource_identity=current_impact_resource;
    }
    auto impact_brush_scene=options.reference_prediction ?
        session.live_reference_prediction_snapshot(now).presented_brush_scene : nullptr;
    if(!impact_brush_scene && local_assets)impact_brush_scene=local_assets->impact_brush_scene();
    auto visual_frame=game_client->local_visuals(visual_context);
    std::optional<hlclient::app::WorldImpactPresentation::LocalMuzzleAnchor> local_muzzle_anchor;
    local_visual_stats=visual_frame.statistics;
    const auto process_local_impact=[&](const hlclient::game_api::LocalWorldImpactRequest& impact) {
      const bool crowbar=impact.action.kind==
          hlclient::game_api::LocalWeaponAction::melee_swing;
      auto& presenter=crowbar ? crowbar_impacts : world_impacts;
      const auto brush_context=local_assets ? local_assets->brush_impact_context(crowbar ? 1U : 0U) :
          hlclient::app::WorldImpactPresentation::BrushImpactContext{};
      const auto result=presenter.submit(impact,
          local_assets ? local_assets->collision_world_package() : nullptr,
          local_assets && local_assets->surface_scene()
              ? local_assets->surface_scene()->world_package().get() : nullptr,
          impact_brush_scene,
          local_assets && local_assets->impact_decal_status(crowbar ? 1U : 0U)==
              hlclient::app::LocalImpactDecalStatus::ready,visual_now_seconds,&brush_context);
      const auto index=static_cast<std::size_t>(result.status);
      auto& counts=crowbar ? crowbar_impact_status_counts : world_impact_status_counts;
      if (index<counts.size()) ++counts[index];
      if (crowbar) {
        using S=hlclient::app::WorldImpactStatus;
        using O=hlclient::game_api::LocalWorldImpactOutcome;
        const auto outcome=result.status==S::hit ? O::static_world_hit :
            result.status==S::miss ? O::miss :
            result.status==S::start_solid ? O::start_solid :
            result.status==S::unsupported_blocker || result.status==S::surface_unsupported ?
                O::unsupported_blocker :
            result.status==S::invalid_request ? O::invalid : O::unavailable;
        const bool presented=game_client->resolved_world_impact(impact.action,
            outcome,result.point,visual_now_seconds,result.surface);
        if (presented) {
          // The trace refines only presentation. Resampling at the same clock
          // updates the viewmodel before this frame's camera-local pass.
          weapon_presentation=game_client->sample(presentation_seconds);
          ++crowbar_hit_animations;
          crowbar_contact_audio_requests+=2U;
          if (local_assets && local_assets->impact_decal_status(1U)==
              hlclient::app::LocalImpactDecalStatus::ready && crowbar_impacts.pending())
            ++crowbar_decal_pending;
        } else ++crowbar_miss_animations;
      } else if (result.status==hlclient::app::WorldImpactStatus::hit && result.point &&
          !impact.suppress_surface_effects) {
        game_client->accepted_world_impact(impact.action,
            *result.point,visual_now_seconds,result.surface);
        ++world_impact_sound_requests;
      }
      if (options.net_trace && result.status==hlclient::app::WorldImpactStatus::hit &&
          material_impact_diagnostic_lines<32U) {
        if(const auto diagnostic=game_client->last_world_impact_diagnostic();
            diagnostic && diagnostic->action==impact.action) {
          const auto sample=sound_assets ? sound_assets->request_local(diagnostic->selected_sound) :
              hlclient::goldsrc::SoundAssetResult{};
          const auto status=[&]() -> std::string_view {
            using S=hlclient::goldsrc::SoundAssetStatus;
            if(!sound_assets) return "not_requested";
            switch(sample.status) {
            case S::ready:return "ready";case S::pending:return "pending";
            case S::missing:return "missing";case S::not_authorized:return "not_authorized";
            case S::open_failed:return "open_failed";case S::decode_failed:return "decode_failed";
            case S::limit:return "limit";case S::unsupported:return "unsupported";
            }
            return "unknown";
          }();
          std::cout << "material_impact surface_id=" << diagnostic->source_surface_index
              << " texture=" << diagnostic->normalized_texture_key.data()
              << " material=" << diagnostic->material_label.data()
              << " source=" << diagnostic->classification_source.data()
              << " sample=" << diagnostic->selected_sound.name()
              << " supplemental_sample=" << (diagnostic->supplemental_sound ?
                  diagnostic->supplemental_sound->name() : std::string_view{"none"})
              << " resource_status=" << status << '\n';
          ++material_impact_diagnostic_lines;
        }
      }
    };
    if(visual_frame.world_impact) process_local_impact(*visual_frame.world_impact);
    for(std::size_t i=0;i<std::min(visual_frame.extra_impact_count,visual_frame.extra_impacts.size());++i)
      process_local_impact(visual_frame.extra_impacts[i]);
    const auto remote_batch=game_client->drain_remote_effects(visual_now_seconds);
    const auto temporary_batch=game_client->drain_temporary_effects(visual_now_seconds);
    const auto follows=game_client->drain_follow_events();
    if(local_assets) for(std::size_t i=0;i<follows.count;++i)
      local_assets->consume_follow(follows.events[i],visual_now_seconds,game_client.get());
    temporary_effect_stats=temporary_batch.statistics;
    if(local_assets) {
      world_impacts.retire_server_targets(local_assets->server_decal_lifetimes());
      crowbar_impacts.retire_server_targets(local_assets->server_decal_lifetimes());
      for(std::size_t i=0;i<std::min(temporary_batch.count,temporary_batch.effects.size());++i) {
        const auto& effect=temporary_batch.effects[i];
        if(!effect.surface_decal)continue;
        const auto materialized=local_assets->materialize_server_decal(effect,world_impacts,server_decal_resource_generation);
        const auto asset_status=static_cast<std::size_t>(materialized.asset_status);
        if(asset_status<server_decal_asset_status_counts.size())++server_decal_asset_status_counts[asset_status];
        if(materialized.asset_status!=hlclient::goldsrc::DecalAssetStatus::ready)continue;
        const auto status=static_cast<std::size_t>(materialized.placement.status);
        if(status<server_decal_placement_counts.size())++server_decal_placement_counts[status];
        server_decal_evictions+=materialized.placement.evicted ? 1U : 0U;
      }
    }
    temporary_effects.consume(temporary_batch,visual_now_seconds);
    remote_effect_stats=remote_batch.statistics;
    if(options.net_trace) for(std::size_t i=0;i<remote_batch.count && remote_socket_trace_budget;++i) {
      --remote_socket_trace_budget;const auto& effect=remote_batch.effects[i];const auto& pose=effect.pose;
      const auto shell=effect.shell ? effect.shell->origin : hlclient::assets::AssetVector3{};
      std::cout << "[remote-fire-pose] entity=" << effect.action.emitter_entity
        << " weapon-id=" << effect.action.weapon_id
        << " model-slot=" << pose.model_slot << " resource=" << pose.resource_id
        << " pose=" << pose.frame_identity << " lifetime=" << pose.lifetime
        << " source=" << hlclient::game_api::to_string(pose.source)
        << " status=" << hlclient::game_api::to_string(pose.status)
        << " muzzle-bone=" << pose.muzzle_bone << " shell-bone=" << pose.shell_bone
        << " muzzle=" << effect.muzzle_origin.x << ',' << effect.muzzle_origin.y << ',' << effect.muzzle_origin.z
        << " shell=" << shell.x << ',' << shell.y << ',' << shell.z
        << " pose-age=" << pose.pose_age << " event-age=" << pose.event_age
        << " sequence=" << pose.server_sequence << " attack_animation_source=server_sequence"
        << " entity-effects=" << pose.effects << " effect-trigger=deduplicated-scripted-event\n";
    }
    if(options.net_trace && remote_effect_trace_budget &&
        remote_effect_stats.received!=remote_effect_trace_revision) {
      remote_effect_trace_revision=remote_effect_stats.received;--remote_effect_trace_budget;
      std::cout << "[remote-effects] received=" << remote_effect_stats.received
          << " accepted=" << remote_effect_stats.accepted << " unresolved=" << remote_effect_stats.unresolved
          << " unsupported=" << remote_effect_stats.unsupported << " local_echo=" << remote_effect_stats.local_echo
          << " late=" << remote_effect_stats.late << " invalid=" << remote_effect_stats.invalid << '\n';
    }
    transient_visuals.set_collision_world(local_assets ? local_assets->collision_world_package() : nullptr);
    remote_effects.consume(remote_batch,*game_client,transient_visuals,world_impacts,
        local_assets ? local_assets->collision_world_package() : nullptr,
        local_assets && local_assets->surface_scene() ? local_assets->surface_scene()->world_package().get() : nullptr,
        impact_brush_scene,
        local_assets && local_assets->has_shell_model(),
        local_assets && local_assets->impact_decal_status()==hlclient::app::LocalImpactDecalStatus::ready,
        visual_now_seconds,local_assets.get());
    world_impacts.update(visual_now_seconds);
    const auto impact_particles=world_impacts.particles();
    render_scene.world_particle_count=std::min(impact_particles.size(),render_scene.world_particles.size());
    std::copy_n(impact_particles.begin(),render_scene.world_particle_count,render_scene.world_particles.begin());
    if(options.net_trace && particle_diagnostic_lines<64U) {
      const auto last=particle_diagnostic_serial;
      for(const auto& entry:world_impacts.particle_diagnostics()) if(entry && entry->serial>last && particle_diagnostic_lines<64U) {
        ++particle_diagnostic_lines;particle_diagnostic_serial=std::max(particle_diagnostic_serial,entry->serial);
        std::cout << "[impact-particles] serial=" << entry->serial << " generation=" << entry->action.generation
          << " command=" << entry->action.command_sequence << " emitter=" << entry->action.emitter_entity
          << " source=" << (entry->action.emitter_entity ? "remote" : "local")
          << " surface=" << entry->surface << " hit=" << entry->point.x << ',' << entry->point.y << ',' << entry->point.z
          << " requested=" << entry->requested_count << " status=" << hlclient::renderer::to_string(entry->status)
          << " submitted=" << render_scene.world_particle_count << " proof=presentation-submission-not-pixels" << '\n';
      }
    }
    const auto pending_crowbar=crowbar_impacts.pending();
    crowbar_impacts.update(visual_now_seconds);
    crowbar_decal_published+=pending_crowbar-crowbar_impacts.pending();
    if (local_assets) {
      const auto profile=game_client->local_impact_assets();
      render_scene.world_decals=world_impacts.frame(local_assets->impact_decal_texture(),
          profile ? profile->material_mode : hlclient::game_api::LocalDecalMaterialMode::straight_alpha);
      const auto crowbar_profile=game_client->local_crowbar_impact_assets();
      render_scene.secondary_world_decals=crowbar_impacts.frame(
          local_assets->impact_decal_texture(1U),crowbar_profile ?
              crowbar_profile->decal.material_mode :
              hlclient::game_api::LocalDecalMaterialMode::straight_alpha);
    }
    const auto spawn_local_shell=[&](const hlclient::game_api::LocalShellEjection& shell) {
      const auto end=std::find(shell.model_name.begin(),shell.model_name.end(),'\0');
      const std::string_view name{shell.model_name.data(),static_cast<std::size_t>(end-shell.model_name.begin())};
      if(local_assets && end!=shell.model_name.end() && local_assets->has_shell_model(name))
        (void)transient_visuals.spawn(shell,visual_now_seconds);
      else ++visual_resource_missing;
    };
    if(visual_frame.shell) spawn_local_shell(*visual_frame.shell);
    for(std::size_t i=0;i<std::min(visual_frame.extra_shell_count,visual_frame.extra_shells.size());++i)
      spawn_local_shell(visual_frame.extra_shells[i]);
    transient_visuals.update(visual_now_seconds);
    for (const auto& contact:transient_visuals.contacts()) {
      if(contact.action.emitter_entity) game_client->remote_shell_contact(contact);
      else game_client->shell_contact(contact);
    }
    if (local_assets && (!transient_visuals.shells().empty() || !temporary_effects.models().empty())) {
      render_scene.transient_world_entities=local_assets->materialize_shells(
          transient_visuals.shells(),visual_now_seconds,temporary_effects.models());
      if (render_scene.transient_world_entities) {
        const auto& frame=*render_scene.transient_world_entities->frame;
        shell_render_submissions+=frame.statistics().visible_count;
        shell_culled+=frame.studio_instances().size()-frame.statistics().visible_count;
      }
      else ++shell_frame_rejected;
    }
    if (options.reference_prediction && render_scene.static_world &&
        render_scene.static_world->runtime_brushes) {
      if (entity_brush_presentation) {
        const auto sampled = hlclient::app::present_runtime_brushes(
            *render_scene.static_world->runtime_brushes, *entity_brush_presentation,
            entity_prediction_snapshot.generation);
        if (sampled) render_scene.static_world->runtime_brushes = sampled;
      }
    }
    if (keyboard && !session.live_runtime_error()) {
      const auto& audio_observation=scene_source.world_state().runtime_observation();
      if(local_assets && audio_observation) {
        const auto prediction=session.live_reference_prediction_snapshot(now);
        local_assets->sound_sources(*audio_observation,prediction.presented_brush_scene.get(),audio_sources);
      }
      const auto& camera=render_scene.camera;
      const auto fx=camera.target.x-camera.position.x, fy=camera.target.y-camera.position.y;
      const auto length=std::sqrt(fx*fx+fy*fy);
      const hlclient::audio::Vec3 right=length>0 ? hlclient::audio::Vec3{fy/length,-fx/length,0} : hlclient::audio::Vec3{0,-1,0};
      server_audio.present(audio_sources,{camera.position,right,hlclient::app::application_audio_master_volume(options),!snapshot.focused() && !options.audio_on_focus_loss},now,
          session.live_server_info() ? static_cast<std::uint32_t>(session.live_server_info()->client_slot())+1U : 0U);
      server_audio.update(sound_assets.get(),now);
      if(options.net_trace && server_audio_trace_budget) {
        const auto& s=server_audio.statistics();
        const auto revision=s.started+s.stopped+s.updated+s.pending+s.expired+s.missing+s.unsupported+s.limits+s.not_authorized+s.open_failed+s.decode_failed+s.voice_rejected+s.invalid_origin+s.attachment_invalidated;
        if(revision!=server_audio_trace_revision) {
          server_audio_trace_revision=revision; --server_audio_trace_budget;
          std::cout << "[server-audio] submitted=" << s.started << " stopped=" << s.stopped << " changed=" << s.updated
            << " pending=" << s.pending << " expired=" << s.expired << " missing=" << s.missing
            << " not-authorized=" << s.not_authorized << " open-failed=" << s.open_failed << " decode-failed=" << s.decode_failed
            << " queue-rejected=" << s.voice_rejected << " budget=" << s.limits << " unsupported=" << s.unsupported
            << " first-reason=" << server_audio.first_error() << " output=" << audio_output.status()
            << " mixer-started=" << audio_output.statistics().mixer.started << " pcm-frames=" << audio_output.statistics().mixer.frames << '\n';
        }
      }
      const auto local_cues=game_client->drain_audio();
      local_audio_stats=local_cues.statistics;
      // Delivery may follow a slow asset/projection update. Age cues against
      // the fresh host clock, not the timestamp captured before that work.
      const auto audio_delivery_seconds=std::chrono::duration<double>{
          std::chrono::steady_clock::now()-session_started}.count();
      local_audio.update(local_cues,sound_assets.get(),audio_delivery_seconds,
          snapshot.focused() && options.audio_volume>0 && audio_output.status()=="playback_ready");
      remote_audio.update(game_client->drain_remote_audio(),sound_assets.get(),audio_delivery_seconds,
          (snapshot.focused() || options.audio_on_focus_loss) && options.audio_volume>0 &&
          audio_output.status()=="playback_ready");
      temporary_audio.update(temporary_batch,sound_assets.get(),audio_delivery_seconds,
          (snapshot.focused() || options.audio_on_focus_loss) && options.audio_volume>0 &&
          audio_output.status()=="playback_ready");
      if(options.net_trace && local_cues.movement_diagnostic && movement_diagnostic_lines<32U &&
          local_cues.movement_diagnostic->serial!=movement_diagnostic_serial) {
        const auto& d=*local_cues.movement_diagnostic;
        const auto resource=sound_assets && !d.sample.name().empty() ?
            sound_assets->request_local(d.sample) : hlclient::goldsrc::SoundAssetResult{};
        const auto resource_status=[&]() -> std::string_view {
          using S=hlclient::goldsrc::SoundAssetStatus;
          if(!sound_assets || d.sample.name().empty()) return "not_requested";
          switch(resource.status) {
          case S::ready:return "ready"; case S::pending:return "pending";
          case S::missing:return "missing"; case S::not_authorized:return "not_authorized";
          case S::open_failed:return "open_failed"; case S::decode_failed:return "decode_failed";
          case S::limit:return "limit"; case S::unsupported:return "unsupported";
          }
          return "unknown";
        }();
        std::cout << "movement_audio command=" << d.command << " generation=" << d.generation
            << " life_epoch=" << d.life_epoch << " ordinal=" << d.ordinal
            << " grounded=" << (d.grounded ? "true":"false")
            << " movement_mode=" << d.movement_mode.data() << " speed_band=" << d.speed_band.data()
            << " surface_id=" << (d.surface_index ? std::to_string(*d.surface_index):"unavailable")
            << " texture=" << d.texture_key.data() << " material=" << d.material.data()
            << " step_category=" << d.step_category.data() << " classification=" << d.classification.data()
            << " decision=" << d.decision.data() << " side=" << (d.left ? "left":"right")
            << " sample=" << d.sample.name() << " volume=" << d.volume
            << " cadence_ms=" << d.cadence_ms << " speed=" << d.speed
            << " resource_status=" << resource_status
            << " submitted_total=" << local_audio.statistics().movement_submitted << '\n';
        movement_diagnostic_serial=d.serial; ++movement_diagnostic_lines;
      }
    }
    if (assets_installed && local_assets) {
      const auto movement_viewmodel_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
      const auto& hud_observation = scene_source.world_state().runtime_observation();
      if (hud_observation) {
        const auto pose = game_client->viewmodel_handle(*hud_observation,
            presentation_model(*hud_observation), presentation_seconds, weapon_presentation.visual);
        render_scene.first_person_entities = local_assets->materialize_viewmodel(pose,visual_context.eye);
        if(movement_diagnostics_enabled)movement_frame.studio_ms=movement_frame.studio_ms.value_or(0.0)+
            movement_elapsed_ms(movement_viewmodel_started,Clock::now());
        if(render_scene.first_person_entities) {
          if(const auto attachment=local_assets->viewmodel_attachment_zero()) {
            const auto& p=*attachment;const auto& basis=visual_context;
            local_muzzle_anchor=hlclient::app::WorldImpactPresentation::LocalMuzzleAnchor{
              pose.generation,pose.model_index,{
                basis.eye.x+basis.forward.x*p.x-basis.right.x*p.y+basis.up.x*p.z,
                basis.eye.y+basis.forward.y*p.x-basis.right.y*p.y+basis.up.y*p.z,
                basis.eye.z+basis.forward.z*p.x-basis.right.z*p.y+basis.up.z*p.z}};
          }
        }
        if (visual_frame.flash && pose.status==hlclient::game_api::ViewmodelStatus::ready &&
            pose.model_index==visual_frame.flash->model_index &&
            render_scene.first_person_entities) {
          // Frame zero triggers the event; the visible flash follows the
          // current rendered pose, not the historical marker pose.
          const auto marker_attachment=local_assets->viewmodel_attachment_at_frame(
              pose,visual_frame.flash->attachment_index,pose.frame_coordinate);
          if (marker_attachment) {
            if(visual_frame.flash->sprite) {
              const hlclient::app::RuntimeReplayLocalAssets::MuzzleSpriteFrame frame{
                  *marker_attachment,*visual_frame.flash->sprite};
              render_scene.first_person_muzzle_entities=local_assets->materialize_muzzle_sprites(
                  {&frame,1},visual_now_seconds);
              if(render_scene.first_person_muzzle_entities)++flash_render_submissions;
              else ++visual_resource_missing;
            } else {
              render_scene.first_person_flash={*marker_attachment,
                  visual_frame.flash->radius_units,visual_frame.flash->color};
              ++flash_render_submissions;
            }
            if (visual_frame.light &&
                visual_frame.light->action==visual_frame.flash->action) {
              const auto& cue=*visual_frame.light;
              render_scene.transient_first_person_light={*marker_attachment,
                  cue.radius_units,cue.intensity,cue.color};
              const auto& p=*marker_attachment;
              const auto& basis=visual_context;
              const hlclient::assets::AssetVector3 world_center{
                  basis.eye.x+basis.forward.x*p.x-basis.right.x*p.y+basis.up.x*p.z,
                  basis.eye.y+basis.forward.y*p.x-basis.right.y*p.y+basis.up.y*p.z,
                  basis.eye.z+basis.forward.z*p.x-basis.right.z*p.y+basis.up.z*p.z};
              render_scene.transient_world_light={world_center,
                  cue.radius_units,cue.intensity,cue.color};
              ++light_render_submissions;
            }
          } else {++visual_resource_missing; ++light_attachment_missing;}
        }
        hlclient::game_api::HudPresentationContext hud_context{static_cast<std::uint32_t>(extent.width),
            static_cast<std::uint32_t>(extent.height), keyboard && intent.focused() &&
            hlclient::gameplay_input::gameplay_button_is_set(intent.held_buttons(),
                hlclient::gameplay_input::GameplayButton::scoreboard),
            options.hud_draw,options.crosshair,options.hud_fastswitch,options.hud_diagnostics,
            options.hud_scale.automatic,options.hud_scale.scale};
        if(hud_observation->receiving_client &&
            hud_observation->receiving_client->origin.complete() && visual_forward_length>.0001F) {
          const auto& origin=hud_observation->receiving_client->origin;
          hlclient::game_api::HudViewContext view;
          view.player_origin={*origin.x,*origin.y,*origin.z};
          if(last_prediction_frame_active && last_prediction_presented_origin) {
            const auto& presented=*last_prediction_presented_origin;
            view.player_origin={presented.x,presented.y,presented.z};
          }
          view.forward={visual_context.forward.x,visual_context.forward.y,visual_context.forward.z};
          // Camera up includes its visual roll; cross(forward,up) is GoldSrc
          // right (+X forward, -Y right at zero yaw), including pitched views.
          const auto f=visual_context.forward, up=render_scene.camera.up;
          const hlclient::assets::AssetVector3 right{
              f.y*up.z-f.z*up.y,f.z*up.x-f.x*up.z,f.x*up.y-f.y*up.x};
          const double right_length=std::sqrt(right.x*right.x+right.y*right.y+right.z*right.z);
          if(right_length>.0001) {
            view.right={right.x/right_length,right.y/right_length,right.z/right_length};
            hud_context.view=view;
          }
        }
        game_client->set_hud_context(hud_context);
        const auto hud = game_client->hud(*hud_observation, presentation_seconds);
        receipts.observe_hud(hud);
        inventory_notifications = hud.inventory_notifications_received;
        inventory_feedback_rows = hud.inventory_feedback.size();
        if (diagnostic_hud_deaths != hud_observation->lifecycle.deaths) {
          diagnostic_hud_deaths = hud_observation->lifecycle.deaths;
          pre_life_hud_health = previous_hud_health;
          pre_life_hud_armor = previous_hud_armor;
          pre_life_hud_health_source = previous_hud_health_source;
          pre_life_hud_armor_source = previous_hud_armor_source;
        }
        previous_hud_health = hud_observation->weapon_hud.health
            ? std::optional<int>{*hud_observation->weapon_hud.health}
            : hud_observation->receiving_client && hud_observation->receiving_client->health
                ? std::optional<int>{static_cast<int>(std::lround(*hud_observation->receiving_client->health))}
                : std::nullopt;
        previous_hud_armor = hud_observation->weapon_hud.armor;
        previous_hud_health_source = hud_observation->weapon_hud.health
            ? hud_observation->weapon_hud.health_source : hud_observation->client_metadata.source;
        previous_hud_armor_source = hud_observation->weapon_hud.armor_source;
        render_scene.basic_hud = hud.draw;
        if(keyboard && pending_weapon_selection && !hud.weapon_selection_pending &&
            !hud.weapon_selection_request &&
            (hud.weapon_selection_status=="server_confirmed" ||
             hud.weapon_selection_status=="server_selection_changed" ||
             hud.weapon_selection_status=="request_rejected" ||
             hud.weapon_selection_status=="confirmation_timeout")) {
          if(hud.weapon_selection_status=="server_confirmed")++weapon_selection_confirmed;
          pending_weapon_selection.reset();model_index_before_selection.reset();
        }
        if(prepared_hud_assets)(void)hlclient::app::materialize_hud_sprites(render_scene,*prepared_hud_assets);
        viewmodel_presented_frames += render_scene.first_person_entities.has_value();
        hud_presented_frames += render_scene.basic_hud.has_value();
        const auto model_index = hud_observation->receiving_client
            ? hud_observation->receiving_client->viewmodel_index
            : std::nullopt;
        if (weapon_observation_logs < 32U &&
            (hud_observation->weapon_hud.revision != last_weapon_hud_revision_logged ||
             model_index != last_viewmodel_index_logged)) {
          ++weapon_observation_logs;
          last_weapon_hud_revision_logged = hud_observation->weapon_hud.revision;
          last_viewmodel_index_logged = model_index;
          std::cout << "live_weapon_observation generation="
                    << hud_observation->generation
                    << " revision=" << hud_observation->publication_revision
                    << " model_index="
                    << (model_index ? std::to_string(*model_index) : "unknown")
                    << " owned_bits="
                    << (hud_observation->receiving_client &&
                                hud_observation->receiving_client->owned_weapon_bits
                            ? std::to_string(*hud_observation->receiving_client
                                                  ->owned_weapon_bits)
                            : "unknown")
                    << " active_id="
                    << (hud_observation->weapon_hud.active_weapon_id
                            ? std::to_string(*hud_observation->weapon_hud.active_weapon_id)
                            : "unknown")
                    << " health="
                    << (hud.health
                            ? std::to_string(*hud.health)
                            : "unknown")
                    << " armor="
                    << (hud.armor
                            ? std::to_string(*hud.armor)
                            : "unknown")
                    << " clip="
                    << (hud.clip
                            ? std::to_string(*hud.clip)
                            : "unknown")
                    << " reserve="
                    << (hud.primary_reserve
                            ? std::to_string(*hud.primary_reserve)
                            : "unknown")
                    << " weaponanim_sequence="
                    << (hud_observation->weapon_hud.animation_sequence
                            ? std::to_string(*hud_observation->weapon_hud.animation_sequence)
                            : "unavailable")
                    << " weaponanim_body="
                    << (hud_observation->weapon_hud.animation_body
                            ? std::to_string(*hud_observation->weapon_hud.animation_body)
                            : "unavailable")
                    << " weaponanim_source_record="
                    << (hud_observation->weapon_hud.animation_source
                            ? std::to_string(hud_observation->weapon_hud.animation_source->record_ordinal)
                            : "unavailable")
                    << " hud_hash="
                    << hlclient::client::runtime_observation_weapon_hud_hash(
                           *hud_observation)
                    << " viewmodel_ready="
                    << render_scene.first_person_entities.has_value()
                    << " viewmodel_status="
                    << hlclient::app::to_string(local_assets->first_person_status())
                    << '\n';
        }
      }
    }
    const auto render_started_at = Clock::now();
    // Preserve existing remote muzzle priority in the shared bounded flash batch.
    remote_effects.present(render_scene,visual_now_seconds,local_assets.get());
    temporary_effects.present(render_scene,visual_now_seconds);
    if(local_assets) {
      const auto beams=game_client->beam_effects(*local_assets,visual_now_seconds,
          local_muzzle_anchor ? std::optional{local_muzzle_anchor->point} : std::nullopt);
      const auto beam_activity=beams.shots+beams.audio_start_requests+beams.audio_replace_requests+beams.audio_stop_requests;
      if(options.net_trace && beam_activity!=beam_diagnostic_shots && beam_diagnostic_lines<32) {
        std::cout<<"client_beam shots="<<beams.shots<<" submitted_sprites="<<beams.count
            <<" reflections="<<beams.reflections<<" penetrations="<<beams.penetrations
            <<" rejected="<<beams.rejected<<" capacity="<<beams.capacity
            <<" active_emitters="<<beams.active_emitters<<" server_handoff="<<beams.server_handoff
            <<" voice_start_requests="<<beams.audio_start_requests<<" voice_replace_requests="<<beams.audio_replace_requests
            <<" voice_stop_requests="<<beams.audio_stop_requests<<'\n';
        beam_diagnostic_shots=beam_activity;++beam_diagnostic_lines;
      }
      for(std::size_t i=0;i<std::min(beams.impact_count,beams.impacts.size());++i) {
        const auto brush_context=local_assets->brush_impact_context();
        (void)world_impacts.submit(beams.impacts[i],local_assets->collision_world_package(),
            local_assets->surface_scene() ? local_assets->surface_scene()->world_package().get() : nullptr,
            impact_brush_scene,
            local_assets->impact_decal_status()==hlclient::app::LocalImpactDecalStatus::ready,visual_now_seconds,&brush_context);
      }
      world_impacts.update(visual_now_seconds);
      if(beams.impact_count) {
        const auto profile=game_client->local_impact_assets();
        render_scene.world_decals=world_impacts.frame(local_assets->impact_decal_texture(),profile ? profile->material_mode : hlclient::game_api::LocalDecalMaterialMode::straight_alpha);
      }
      local_assets->present_entity_effects(render_scene,visual_now_seconds,{beams.sprites.data(),beams.count},
          session.live_server_info() ? std::optional{static_cast<std::uint32_t>(session.live_server_info()->client_slot())+1U} : std::nullopt,
          beams.server_handoff && local_muzzle_anchor ? std::optional{local_muzzle_anchor->point} : std::nullopt);
    }
    if(local_assets)render_scene.additional_world_decals=world_impacts.frame_batches(
        render_scene.static_world ? render_scene.static_world->runtime_brushes.get() : nullptr);
    if(local_assets)for(auto& batch:crowbar_impacts.frame_batches(
        render_scene.static_world ? render_scene.static_world->runtime_brushes.get() : nullptr)) {
      // Both action pools share the renderer's existing bounded attached pass.
      if(render_scene.additional_world_decals.size()==64U)break;
      render_scene.additional_world_decals.push_back(std::move(batch));
    }
    const auto blood_component_count=[&](auto member) -> std::uint64_t {
      if(!local_assets)return 0;
      return local_assets->temporary_sprite_statistics(1).*member+
          local_assets->temporary_sprite_statistics(2).*member+
          local_assets->temporary_sprite_statistics(3).*member;
    };
    using TemporarySpriteStats=hlclient::app::RuntimeReplayLocalAssets::TemporarySpriteStatistics;
    const auto blood_materialized_before=blood_component_count(&TemporarySpriteStats::submitted);
    const auto blood_rejected_before=blood_component_count(&TemporarySpriteStats::frame_rejected);
    const auto large_yellow_materialized_before=local_assets ? local_assets->temporary_sprite_statistics(2).submitted : 0U;
    render_scene.transient_sprite_entities=local_assets ?
        local_assets->materialize_temporary_sprites(temporary_effects.sprites(),visual_now_seconds) : std::nullopt;
    const bool submitted_blood_batch=local_assets && render_scene.transient_sprite_entities &&
        blood_component_count(&TemporarySpriteStats::submitted)>blood_materialized_before;
    // One failed mixed-color frame remains one failed blood batch, although
    // the materializer records it in every participating diagnostic group.
    if(blood_component_count(&TemporarySpriteStats::frame_rejected)>blood_rejected_before)
      ++blood_rejected_batches;
    const bool submitted_large_yellow_batch=local_assets && render_scene.transient_sprite_entities &&
        local_assets->temporary_sprite_statistics(2).submitted>large_yellow_materialized_before;
    const auto& temporary_audio_stats=temporary_audio.statistics();
    const auto temporary_revision=temporary_effect_stats.received+temporary_audio_stats.submitted+
        temporary_audio_stats.missing+temporary_audio_stats.late+temporary_audio_stats.limits+
        temporary_audio_stats.muted+temporary_audio_stats.resource_pending;
    if(options.net_trace && temporary_effect_trace_budget && temporary_revision!=temporary_effect_trace_revision) {
      temporary_effect_trace_revision=temporary_revision;--temporary_effect_trace_budget;
      const auto resource=local_assets ? local_assets->temporary_sprite_statistics() :
          hlclient::app::RuntimeReplayLocalAssets::TemporarySpriteStatistics{};
      std::cout << "[temporary-effects] received=" << temporary_effect_stats.received
          << " accepted=" << temporary_effect_stats.accepted << " duplicates=" << temporary_effect_stats.duplicate
          << " unsupported_flags=" << temporary_effect_stats.unsupported_flags
          << " deferred_kind=" << temporary_effect_stats.deferred_kind
          << " sprite_submitted=" << resource.submitted << " sprite_absent=" << resource.absent
          << " sprite_unsupported=" << resource.unsupported
          << " main_audio_requested=" << temporary_effect_stats.main_audio_requested
          << " main_audio_suppressed=" << temporary_effect_stats.main_audio_suppressed
          << " audio_submitted=" << temporary_audio_stats.submitted
          << " audio_pending=" << temporary_audio_stats.resource_pending
          << " audio_missing=" << temporary_audio_stats.missing
          << " audio_not_authorized=" << temporary_audio_stats.resource_not_authorized
          << " audio_open_failed=" << temporary_audio_stats.resource_open_failed
          << " audio_decode_failed=" << temporary_audio_stats.resource_decode_failed
          << " audio_late=" << temporary_audio_stats.late
          << " audio_muted=" << temporary_audio_stats.muted
          << " audio_queue_rejected=" << temporary_audio_stats.output_queue_rejected
          << " output=" << audio_output.status()
          << " mixer_started=" << audio_output.statistics().mixer.started << '\n';
    }
    if(options.net_trace && local_assets && entity_effect_trace_budget) {
      const auto& effect=local_assets->entity_effect_statistics();
      const auto revision=effect.received+effect.detached+effect.discontinuity;
      if(revision!=entity_effect_trace_revision) {
        entity_effect_trace_revision=revision;--entity_effect_trace_budget;
        const auto& resource=local_assets->follow_resource_statistics();
        std::cout << "[entity-effects] follow_received=" << effect.received
            << " accepted=" << effect.accepted << " duplicate=" << effect.duplicate
            << " invalid=" << effect.invalid << " capacity=" << effect.capacity
            << " detached=" << effect.detached << " discontinuity=" << effect.discontinuity
            << " segment_submitted=" << resource.submitted << " sprite_absent=" << resource.absent
            << " sprite_unsupported=" << resource.unsupported << " frame_rejected=" << resource.frame_rejected
            << " light_submitted=" << effect.light_submitted << '\n';
      }
    }
    world_impacts.append_lines(render_scene,local_muzzle_anchor);
    world_impacts.append_impact_glows(render_scene);
    if(local_assets)local_assets->append_beam_lights(render_scene);
    render_scene.screen_effects=game_client->screen_effects(visual_now_seconds);
    const bool scene_ready_for_presentation=assets_installed && session.live_visual_input_ready() &&
        last_view_status==hlclient::app::LiveVisualViewStatus::ready;
    if(console)console->compose(render_scene,scene_ready_for_presentation);
    const auto movement_render_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
    if(movement_diagnostics_enabled)movement_frame.render_submit_us=receipts.movement_stamp(movement_render_started);
    if(movement_diagnostics_enabled) {
      hlclient::core::MultiplayerPresentationPayload submitted;
      submitted.phase=hlclient::core::MultiplayerPresentationPhase::submit;
      submitted.sample_seconds=presentation_seconds;
      submitted.origin=std::array{render_scene.camera.position.x,render_scene.camera.position.y,render_scene.camera.position.z};
      receipts.diagnostic(submitted,hlclient::core::MultiplayerDiagnosticProducer::renderer,movement_frame.frame_number);
      movement_frame.cpu_update_ms=movement_elapsed_ms(movement_frame_started,movement_render_started);
      movement_frame.presentation_use_us=receipts.movement_stamp(movement_render_started);
      movement_frame.presented_eye=hlclient::app::MovementDiagnosticVector3{
          render_scene.camera.position.x,render_scene.camera.position.y,render_scene.camera.position.z};
      renderer.render_with_gpu_timing(render_scene,extent,movement_frame.frame_number,
          movement_diagnostics_enabled);
      if(const auto& static_world=renderer.statistics().last_static_world_frame;
          static_world && static_world->frame_number==movement_frame.frame_number)
        movement_frame.static_world=movement_static_world_frame(*static_world);
      if(renderer.gpu_timing_statistics().last_submission_status==
          hlclient::renderer::opengl::OpenGlGpuTimingStatus::pending) {
        // Mirror only the existing eight pending queries' numeric ownership.
        // A result never borrows whichever map/frame happens to be current at poll.
        if(renderer.gpu_timing_statistics().pending==1U)diagnostic_gpu_owners={};
        if(const auto at=std::ranges::find(diagnostic_gpu_owners,0U,&DiagnosticGpuOwner::frame);
            at!=diagnostic_gpu_owners.end()) {
          const auto identity=receipts.snapshot();
          *at={movement_frame.frame_number,identity?identity->generation.network_generation:0U,
              identity?identity->generation.map_generation:0U};
        }
      }
      movement_frame.gpu_passes.status=movement_gpu_status(
          renderer.gpu_timing_statistics().last_submission_status);
      movement_frame.render_ms=movement_elapsed_ms(movement_render_started,Clock::now());
    } else renderer.render(render_scene, extent);
    const auto movement_render_report_started=movement_diagnostics_enabled ? Clock::now() : Clock::time_point{};
    if(options.external_server_client)receipts.external_render(options,window,renderer,successful_presentations+1U);
    if(movement_diagnostics_enabled) {
      const auto report_ms=movement_elapsed_ms(movement_render_report_started,Clock::now());
      movement_frame.report_ms=movement_frame.report_ms.value_or(0.0)+report_ms;
      receipts.observe_reporting_scope(true,report_ms,successful_presentations+1U);
    }
    if(submitted_blood_batch)++blood_draw_batches;
    if(submitted_large_yellow_batch)++large_yellow_blood_draw_batches;
    if (const auto& live = scene_source.world_state().runtime_observation();
        live && live->lifecycle.respawns != 0U && !live->lifecycle.dead())
      ++post_respawn_rendered_frames;
    std::optional<std::uint32_t> current_viewmodel_asset_index;
    if (render_scene.first_person_entities &&
        !render_scene.first_person_entities->frame->studio_instances().empty()) {
      current_viewmodel_asset_index = render_scene.first_person_entities->
          frame->studio_instances().front().studio_asset_index;
    }
    if (current_viewmodel_asset_index != viewmodel_probe_asset_index) {
      viewmodel_probe_asset_index = current_viewmodel_asset_index;
      viewmodel_probes_for_asset = 0U;
      last_viewmodel_probe_at.reset();
    }
    const bool viewmodel_probe_due = (weapon_check || fire_reload_check) &&
        current_viewmodel_asset_index &&
        viewmodel_probe_count < 6U &&
        viewmodel_probes_for_asset < 2U &&
        (!last_viewmodel_probe_at ||
         render_started_at - *last_viewmodel_probe_at >=
             std::chrono::milliseconds{250});
    if ((weapon_check || fire_reload_check) &&
        render_scene.first_person_entities) {
      latest_viewmodel_scene = render_scene;
      if (viewmodel_probe_due && render_scene.static_world) {
        deferred_visual_probes.push_back(render_scene);
        ++viewmodel_probes_for_asset;
        ++viewmodel_probe_count;
        last_viewmodel_probe_at = render_started_at;
      }
    }
    if (presentation_check && weapon_presentation.visual && weapon_presentation.action &&
        render_scene.first_person_entities) {
      const auto& visual = *weapon_presentation.visual;
      const auto kind = static_cast<std::size_t>(*weapon_presentation.action);
      const bool action_pose = kind == 0U ? visual.sequence == 3U || visual.sequence == 4U
          : kind == 1U ? visual.sequence == 5U || visual.sequence == 6U
          : visual.sequence == 4U || visual.sequence == 5U || visual.sequence == 7U;
      if (action_pose) {
        ++action_presented_frames[kind];
        if (!animation_first_scene[kind]) {
          animation_first_scene[kind] = render_scene;
          animation_first_at[kind] = render_started_at;
          local_probe_restart = visual.restart_identity;
        } else if (!animation_later_scene[kind] && animation_first_at[kind] &&
            local_probe_restart == visual.restart_identity &&
            render_started_at - *animation_first_at[kind] >= std::chrono::milliseconds{40}) {
          animation_later_scene[kind] = render_scene;
        }
      }
      if (weapon_presentation.local_punch_pitch_degrees < -0.05)
        ++recoil_presented_frames;
    }
    if (!presentation_check && fire_reload_check && last_animation_kind &&
        last_action_animation_source && render_scene.first_person_entities) {
      const auto kind = *last_animation_kind;
      if (!animation_first_scene[kind]) {
        animation_first_scene[kind] = render_scene;
        animation_first_at[kind] = render_started_at;
        animation_probe_source[kind] = last_action_animation_source;
      } else if (!animation_later_scene[kind] && animation_first_at[kind] &&
                 animation_probe_source[kind] == last_action_animation_source &&
                 render_started_at - *animation_first_at[kind] >=
                     std::chrono::milliseconds{150}) {
        animation_later_scene[kind] = render_scene;
      }
    }
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
    if (!options.external_server_client &&
        (!console || (scene_ready_for_presentation && !console->console_active())) &&
        (!keyboard && !weapon_check && !fire_reload_check && !damage_respawn_check ||
         !input_activated_at) && assets_installed &&
        camera_controller.camera_revision() != 0U &&
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
    if(movement_diagnostics_enabled) {
      hlclient::core::MultiplayerPresentationPayload p;p.phase=hlclient::core::MultiplayerPresentationPhase::swap_begin;
      receipts.diagnostic(p,hlclient::core::MultiplayerDiagnosticProducer::renderer,movement_frame.frame_number);
    }
    window.swap_buffers();
    const auto swap_completed_at = Clock::now();
    ++successful_presentations;
    if(movement_diagnostics_enabled) {
      if(movement_previous_frame)movement_frame.frame_interval_ms=movement_elapsed_ms(*movement_previous_frame,swap_completed_at);
      movement_previous_frame=swap_completed_at;
      const auto completed_identity=receipts.snapshot();
      receipts.multiplayer().completed_frame(render_scene,movement_frame.frame_number,
          completed_identity?completed_identity->generation.network_generation:0U,completed_identity?completed_identity->generation.map_generation:0U,
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(swap_completed_at.time_since_epoch()).count()),extent.width,extent.height);
      hlclient::core::MultiplayerPresentationPayload p;p.phase=hlclient::core::MultiplayerPresentationPhase::swap_end;
      p.origin=std::array{render_scene.camera.position.x,render_scene.camera.position.y,render_scene.camera.position.z};
      p.present_ms=movement_elapsed_ms(swap_started_at,swap_completed_at);p.cpu_ms=movement_frame.cpu_update_ms;
      p.frame_ms=movement_frame.frame_interval_ms;
      p.viewport_width=extent.width;p.viewport_height=extent.height;p.framebuffer_width=extent.width;p.framebuffer_height=extent.height;
      receipts.diagnostic(p,hlclient::core::MultiplayerDiagnosticProducer::renderer,movement_frame.frame_number);
      movement_frame.monotonic_us=receipts.movement_stamp(swap_completed_at);
      if(const auto identity=receipts.snapshot();identity) {
        movement_frame.network_generation=identity->generation.network_generation;
        movement_frame.map_generation=identity->generation.map_generation;
      }
      if(local_assets) {
        const auto& movement_summary=local_assets->summary();
        if(movement_summary.remote_snapshot_mode) {
          movement_frame.remote_previous_s=movement_summary.remote_previous_seconds;
          movement_frame.remote_current_s=movement_summary.remote_current_seconds;
          movement_frame.remote_sample_s=movement_summary.remote_sample_seconds;
          movement_frame.remote_alpha=movement_summary.remote_alpha;
          movement_frame.set_remote_sample_mode(hlclient::app::remote_snapshot_mode_name(*movement_summary.remote_snapshot_mode));
        }
      }
      movement_frame.present_ms=movement_elapsed_ms(swap_started_at,swap_completed_at);
      movement_context.focused=snapshot.focused();
      movement_context.captured=window.capture_state()==hlclient::input::InputCaptureState::captured;
      movement_context.control_active=control_activated;
      if(const auto& life_observation=scene_source.world_state().runtime_observation();life_observation) {
        const auto& life=life_observation->lifecycle;
        movement_context.life_epoch=life.life_epoch;
        if(life.state==hlclient::client::LocalPlayerLifeState::alive)movement_context.alive=true;
        else if(life.dead())movement_context.alive=false;
      }
      if(receipts.movement().observe(movement_frame,movement_context)) {
        if(movement_velocity_stop)(void)receipts.movement().observe_velocity_stop(movement_frame,*movement_velocity_stop);
        if(local_assets && assets_installed)
          if(const auto remote=local_assets->remote_movement_observations();remote)
            (void)receipts.movement().observe_remote(movement_frame,*remote);
      }
      if(movement_manual_marker)(void)receipts.movement().mark(hlclient::app::MovementIncidentTrigger::manual,
          receipts.movement_stamp(swap_completed_at));
      // Poll a bounded eight-query pool. Pending results stay absent, not zero.
      for(std::size_t movement_query=0;movement_query<8U;++movement_query) {
        const auto timing=renderer.poll_gpu_timing();if(!timing)break;
        if(const auto owner=std::ranges::find(diagnostic_gpu_owners,timing->frame_number,&DiagnosticGpuOwner::frame);
            owner!=diagnostic_gpu_owners.end()) {
          hlclient::core::MultiplayerPresentationPayload gpu_event;
          gpu_event.phase=hlclient::core::MultiplayerPresentationPhase::gpu_result;
          // unsupported here means optional per-pass timestamps unavailable;
          // poll_gpu_timing still returned a completed total-time query.
          if((timing->status==hlclient::renderer::opengl::OpenGlGpuTimingStatus::ready ||
              timing->status==hlclient::renderer::opengl::OpenGlGpuTimingStatus::unsupported) &&
              std::isfinite(timing->elapsed_ms) && timing->elapsed_ms>=0.0)
            gpu_event.gpu_ms=timing->elapsed_ms;
          receipts.diagnostic(gpu_event,hlclient::core::MultiplayerDiagnosticProducer::renderer,
              owner->frame,{},std::pair{owner->network,owner->map});
          *owner={};
        }
        if(std::isfinite(timing->elapsed_ms))
          (void)receipts.movement().observe_gpu(timing->frame_number,timing->elapsed_ms);
        (void)receipts.movement().observe_gpu_passes(timing->frame_number,
            {timing->pass_elapsed_ms,timing->valid_pass_mask,
             movement_gpu_status(timing->status),timing->world_subpass_elapsed_ms,
             timing->valid_world_subpass_mask});
      }
    }
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
      if(console)console->in_game();
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

    if(console)console->pace();
    if(movement_diagnostics_enabled){
      movement_previous_limiter_wait_us=console?console->pacing_statistics().last_wait_us:0U;
      movement_previous_yield_wait_us=0U;
    }
    if (session.terminal())
      running = false;
    else {
      const auto yield_started=movement_diagnostics_enabled?Clock::now():Clock::time_point{};
      std::this_thread::yield();
      if(movement_diagnostics_enabled)movement_previous_yield_wait_us=static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-yield_started).count());
    }
    if(movement_diagnostics_enabled)movement_previous_waits_known=true;
  }

  loading_work->cancelled.store(true);
  if(console)console->begin_disconnect();
  if(asset_future && asset_future->valid()) {
    while(asset_future->wait_for(std::chrono::milliseconds{0})!=std::future_status::ready) {
      if(console)console->pump_idle();
      else std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    (void)asset_future->get(); // Cancelled generation is never installed.
  }
  const auto released = window.request_relative_mouse_capture(false);
  if (!released)
    throw std::runtime_error{"Final live visual mouse release failed"};
  if (released.status ==
      hlclient::platform::RelativeMouseCaptureStatus::released)
    ++capture_releases;
  // Retain actual accepted gameplay evidence before any close clears transient
  // coordinator state. This is not reconstructed from planned inputs/log text.
  const auto before_close_usercmd=session.live_usercmd_snapshot();
  const auto before_close_prediction=session.live_reference_prediction_snapshot(Clock::now());
  const auto close_intent_stamp=receipts.stamp();
  external_status("disconnecting");
  game_client->cancel_uncommitted();
  if (window_closed || timed_session_complete || console_disconnected)
    hlclient::app::finish_intentional_session_close(session,[]{return Clock::now();},
        [console]{if(console)console->pump_idle();std::this_thread::sleep_for(std::chrono::milliseconds{20});});
  else if (!session.terminal()) session.cancel(Clock::now());
  const auto close_summary=session.graceful_close_snapshot();
  const auto socket_close_error=session.close_transport();
  const auto socket_closed_stamp=socket_close_error ?
      std::optional<hlclient::app::SessionReceiptStamp>{} : std::optional{receipts.stamp()};
  // Report I/O must never interrupt the bounded network goodbye/native close.
  const auto final_pacing=console?std::optional{console->pacing_statistics()}:std::optional<hlclient::app::ConsolePacingStatistics>{};
  receipts.external_video(options,window.video_state(),true,{},final_pacing?&*final_pacing:nullptr);

  // Expensive same-scene readbacks run only after the scripted command source
  // has stopped. The live 20 ms scheduler never sees verification GPU stalls.
  if (weapon_check || fire_reload_check) {
    const auto pixels = window.pixel_extent();
    const hlclient::renderer::RenderExtent probe_extent{
        pixels.width, pixels.height};
    if (probe_extent.width >= 128 && probe_extent.height >= 128) {
      for (const auto& candidate : deferred_visual_probes) {
        renderer.render(candidate, probe_extent);
        const auto complete = renderer.observe_framebuffer(
            probe_extent, candidate.clear_color);
        if (!viewmodel_pixels_distinct) {
          auto without_model = candidate;
          without_model.first_person_entities.reset();
          renderer.render(without_model, probe_extent);
          const auto world_and_hud = renderer.observe_framebuffer(
              probe_extent, candidate.clear_color);
          viewmodel_pixel_tested = true;
          viewmodel_pixels_distinct = complete && world_and_hud &&
              complete.color_signature != world_and_hud.color_signature;
        }
        if (!hud_pixel_tested && candidate.basic_hud) {
          auto without_hud = candidate;
          without_hud.basic_hud.reset();
          renderer.render(without_hud, probe_extent);
          const auto world_and_model = renderer.observe_framebuffer(
              probe_extent, candidate.clear_color);
          hud_pixel_tested = true;
          hud_pixels_distinct = complete && world_and_model &&
              complete.color_signature != world_and_model.color_signature;
        }
        if (viewmodel_pixels_distinct && hud_pixel_tested)
          break;
      }
      // One retained current-session Studio pose is sampled at all angles.
      // No world/HUD background can affect its framebuffer signature.
      if (latest_viewmodel_scene) {
        constexpr std::array<float, 3U> pitch_degrees{0.0F, 80.0F, -80.0F};
        constexpr float radians = 0.01745329251994329577F;
        for (const float pitch : pitch_degrees) {
          auto probe = *latest_viewmodel_scene;
          probe.static_world.reset();
          probe.dynamic_entities.reset();
          probe.basic_hud.reset();
          probe.camera.position = {123.0F, -77.0F, 32.0F};
          probe.camera.target = {
              probe.camera.position.x + std::cos(pitch * radians),
              probe.camera.position.y,
              probe.camera.position.z + std::sin(pitch * radians)};
          renderer.render(probe, probe_extent);
          pitch_probe_samples[pitch_probe_count] = renderer.observe_framebuffer(
              probe_extent, probe.clear_color);
          ++pitch_probe_count;
        }
      }
      if (fire_reload_check) {
        for (std::size_t kind = 0U; kind < animation_pixels_changed.size();
             ++kind) {
          if (!animation_first_scene[kind] || !animation_later_scene[kind])
            continue;
          const auto isolated = [&](hlclient::renderer::RenderScene scene) {
            scene.static_world.reset();
            scene.dynamic_entities.reset();
            scene.basic_hud.reset();
            renderer.render(scene, probe_extent);
            return renderer.observe_framebuffer(
                probe_extent, scene.clear_color);
          };
          const auto first = isolated(*animation_first_scene[kind]);
          const auto later = isolated(*animation_later_scene[kind]);
          animation_pixels_changed[kind] = first && later &&
              first.color_signature != later.color_signature;
        }
      }
    }
  }

  const auto usercmd = session.live_runtime_result() &&
                               session.live_runtime_result()->usercmd_check
                           ? session.live_runtime_result()->usercmd_check
                           : before_close_usercmd;
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
      (keyboard || non_clear_framebuffers > 0U) &&
      invalid_framebuffer_observations == 0U &&
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
  const bool success = test_health_gate.ready() && visual_success && network_success &&
                       (options.reference_prediction
                            ? keyboard ? (window_closed || timed_session_complete || console_disconnected)
                                       : session.live_runtime_result().has_value()
                            : keyboard ? (window_closed || timed_session_complete || console_disconnected)
                                       : scripted_success);
  const auto final_prediction = before_close_prediction;
  const bool prediction_success = options.reference_prediction && success &&
      prediction_collision_attached && prediction_active_frames >= 30U &&
      prediction_moving_active_frames >= 10U &&
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
  const bool h4_jump_predicted = h4_jump_rising_frames > 0U &&
      h4_active_frames[1U] > 0U && h4_local_steps[1U] > 0U &&
      h4_corrections[1U] > 0U;
  const bool h4_duck_predicted = h4_duck_transition_frames > 0U &&
      h4_stable_crouch_frames > 0U && h4_active_frames[3U] > 0U &&
      h4_local_steps[3U] > 0U && h4_corrections[3U] > 0U;
  const bool h4_crouch_walk_verified =
      h4_crouch_predicted_horizontal_changes > 0U &&
      h4_crouch_server_horizontal_changes > 0U;
  const bool h4_stand_recovered =
      h4_standing_recovery_frames > 0U && h4_local_steps[4U] > 0U;
  const std::string_view h4_result =
      prediction_success && usercmd &&
          usercmd->jump_duck.outcome ==
              hlclient::goldsrc::LiveJumpDuckOutcome::verified &&
          h4_jump_predicted && h4_duck_predicted &&
          h4_crouch_walk_verified && h4_stand_recovered
      ? "live_jump_duck_crouchwalk_prediction_verified"
      : h4_jump_predicted && !h4_duck_predicted
          ? "jump_prediction_verified_duck_pending"
      : h4_duck_predicted &&
            h4_crouch_server_horizontal_changes == 0U
          ? "crouch_walk_prediction_context_blocked"
      : "jump_duck_prediction_implemented_live_pending";
  const bool handshake_failed = session.terminal() && !window_closed &&
      !timed_session_complete && !console_disconnected && !session.live_runtime_result();
  const auto primary_error = [&]() -> std::string_view {
    if (session.live_runtime_error())
      return hlclient::goldsrc::to_string(session.live_runtime_error()->code);
    if(options.external_server_client)
      if(const auto rejection=session.external_rejection_reason())return *rejection;
    if (const auto error=session.authentication_error_code())
      return hlclient::app::live_authentication_failure_reason(*error);
    if (handshake_failed) return session.handshake_state();
    if (keyboard && success) return "none";
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

  if(console && (handshake_failed || session.live_runtime_error())) {
    if(const auto reason=session.server_rejection_text())
      hlclient::core::log_server_text("Connection rejected: "+std::string{*reason},LogLevel::error);
    else if(const auto& failure=session.live_runtime_error();failure) {
      if(failure->code!=hlclient::goldsrc::LiveRuntimeStageErrorCode::server_disconnected)
        hlclient::core::log(LogLevel::error,"Client session failed: "+std::string{primary_error}+
            ". "+failure->context);
    } else hlclient::core::log(LogLevel::error,"Local connection failed: "+std::string{primary_error}+
        ". See the session report for details.");
  }

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
      if (which == "p95" || which == "p99") {
        const auto index =
            static_cast<std::size_t>(std::ceil(values.size() *
                (which == "p99" ? 0.99 : 0.95))) - 1U;
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
              << " frame_interval_p99_ms="
              << timing_stat(active_frame_intervals_ms, "p99")
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
            << " stall_recoveries="
            << (usercmd ? usercmd->scheduler_stall_recoveries : 0U)
            << " discarded_wall_time_samples="
            << (usercmd ? usercmd->scheduler_discarded_wall_time_samples : 0U)
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
              << command_count(usercmd ? usercmd->server_sample_count : 0U)
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
                << " udp_split_parts=" << rx.split_parts
                << " udp_split_completed=" << rx.split_completed
                << " udp_split_rejected=" << rx.split_rejected
                << " udp_split_expired=" << rx.split_expired
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
                << " samples_delivered_post_input=" << usercmd->server_sample_count
                << " retained_server_samples=" << usercmd->server_samples.size()
                << " server_samples_evicted=" << usercmd->server_samples_evicted
                << " transmit_ranges_evicted=" << usercmd->transmit_ranges_evicted
                << " first_accepted_sequence=" << sequence(rx.first_accepted_sequence)
                << " last_accepted_sequence=" << sequence(rx.last_accepted_sequence)
                << " first_payload_sequence=" << sequence(rx.first_payload_sequence)
                << " last_payload_sequence=" << sequence(rx.last_payload_sequence)
                << "\n";
    }
    const auto vector_or_unavailable = [&](const auto& vector) {
      return vector ? print_vector(*vector) : std::string{"unavailable"};
    };
    std::ostringstream correction_pairs;
    const auto first_pair = prediction_correction_window_count >
            prediction_correction_window.size()
        ? prediction_correction_window_count - prediction_correction_window.size()
        : 0U;
    for (auto index = first_pair; index < prediction_correction_window_count;
         ++index) {
      const auto& sample = prediction_correction_window[
          index % prediction_correction_window.size()];
      if (index != first_pair) correction_pairs << ',';
      correction_pairs
          << sample.presentation_from_command.value_or(0U) << ':'
          << sample.presentation_to_command.value_or(0U) << ':'
          << sample.presentation_from_time_ns.value_or(0) << ':'
          << sample.presentation_to_time_ns.value_or(0) << ':'
          << sample.presentation_alpha.value_or(0.0) << ':'
          << sample.presentation_reason;
    }
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
              << (usercmd ? usercmd->server_sample_count : 0U)
              << " active_frames=" << prediction_active_frames
              << " fallback_frames=" << prediction_fallback_frames
              << " moving_active_frames=" << prediction_moving_active_frames
              << " moving_presented_changes="
              << prediction_moving_presented_changes
              << " moving_simulation_changes="
              << prediction_moving_simulation_changes
              << " interpolated_frames=" << prediction_interpolated_frames
              << " endpoint_frames=" << prediction_endpoint_frames
              << " collision_blocked_frames="
              << prediction_collision_blocked_frames
              << " visual_correction_frames="
              << prediction_visual_correction_frames
              << " active_time_ms=" << prediction_active_time_ms
              << " fallback_time_ms=" << prediction_fallback_time_ms
              << " long_stall_frames=" << prediction_long_stall_frames
              << " presentation_cpu_total_ms="
              << prediction_presentation_cpu_total_ms
              << " presentation_cpu_max_ms="
              << prediction_presentation_cpu_max_ms
              << " presentation_trace_queries="
              << final_prediction.presentation_trace_queries_total
              << " presentation_scratch_growths="
              << final_prediction.presentation_scratch_growths_total
              << " presentation_scratch_bytes="
              << final_prediction.presentation_scratch_bytes
              << " correction_pair_window="
              << (prediction_correction_window_count
                      ? correction_pairs.str() : std::string{"unavailable"})
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
              << " last_camera_correction_jump="
              << (final_prediction.last_camera_correction_jump
                      ? std::to_string(*final_prediction.last_camera_correction_jump)
                      : std::string{"unavailable"})
              << " maximum_camera_correction_jump="
              << (final_prediction.maximum_camera_correction_jump
                      ? std::to_string(*final_prediction.maximum_camera_correction_jump)
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
  if (options.reference_prediction && jump_duck_check) {
    constexpr std::array<std::string_view, 5U> names{
        "settle", "jump_hold", "landing", "duck_crouch_walk",
        "unduck_standing_shift"};
    for (std::size_t index = 0U; index < names.size(); ++index) {
      std::cout << "live_h4_phase phase=" << names[index]
                << " active_frames=" << h4_active_frames[index]
                << " fallback_frames=" << h4_fallback_frames[index]
                << " active_ms=" << h4_active_ms[index]
                << " fallback_ms=" << h4_fallback_ms[index]
                << " local_steps=" << h4_local_steps[index]
                << " corrections=" << h4_corrections[index]
                << " fallback_reason="
                << (h4_last_fallback_reason[index].empty()
                        ? "none" : h4_last_fallback_reason[index])
                << " generated=" << (usercmd ? usercmd->generated_by_phase[index] : 0U)
                << " sent=" << (usercmd ? usercmd->sent_by_phase[index] : 0U)
                << " fresh_server_samples="
                << (usercmd ? usercmd->fresh_samples_by_phase[index] : 0U)
                << '\n';
    }
    const auto vector_or_unavailable = [&](const auto& vector) {
      return vector ? print_vector(*vector) : std::string{"unavailable"};
    };
    std::optional<hlclient::assets::AssetVector3> presented_eye;
    if (final_prediction.presented_origin &&
        final_prediction.presented_view_offset) {
      const auto& origin = *final_prediction.presented_origin;
      const auto& offset = *final_prediction.presented_view_offset;
      presented_eye = {origin.x + offset.x, origin.y + offset.y,
                       origin.z + offset.z};
    }
    std::cout << "live_h4_prediction result=" << h4_result
              << " requested_profile=reference_carrier_jump_duck_v2"
              << " effective_profile="
              << (final_prediction.state ==
                      hlclient::goldsrc::LiveReferencePredictionState::active
                      ? "reference_carrier_jump_duck_v2" : "fallback")
              << " jump_rising_frames=" << h4_jump_rising_frames
              << " duck_transition_frames=" << h4_duck_transition_frames
              << " stable_crouch_frames=" << h4_stable_crouch_frames
              << " standing_recovery_frames=" << h4_standing_recovery_frames
              << " crouch_predicted_horizontal_changes="
              << h4_crouch_predicted_horizontal_changes
              << " crouch_server_horizontal_changes="
              << h4_crouch_server_horizontal_changes
              << " old_buttons="
              << final_prediction.predicted_old_buttons.value_or(0U)
              << " old_buttons_origin=exact_predicted_history"
              << " predicted_duck_time_ms="
              << final_prediction.predicted_duck_time_milliseconds.value_or(0U)
              << " predicted_in_duck_transition="
              << final_prediction.predicted_in_duck_transition.value_or(false)
              << " predicted_hull="
              << (final_prediction.predicted_hull
                      ? hlclient::movement::to_string(*final_prediction.predicted_hull)
                      : std::string_view{"unavailable"})
              << " predicted_origin="
              << vector_or_unavailable(final_prediction.predicted_origin)
              << " predicted_velocity="
              << vector_or_unavailable(final_prediction.predicted_velocity)
              << " predicted_view_offset="
              << vector_or_unavailable(final_prediction.predicted_view_offset)
              << " presented_eye=" << vector_or_unavailable(presented_eye)
              << " pair_from="
              << final_prediction.presentation_from_command.value_or(0U)
              << " pair_to="
              << final_prediction.presentation_to_command.value_or(0U)
              << " pair_alpha="
              << final_prediction.presentation_alpha.value_or(0.0)
              << " correction_epoch=" << final_prediction.prediction_epoch
              << " raw_error="
              << final_prediction.last_raw_position_error.value_or(0.0)
              << '\n';
  }
  const auto application_outcome = hlclient::app::evaluate_live_visual_session(
      keyboard, success, session.live_runtime_error().has_value() || handshake_failed, scripted_success,
      prediction_success, options.reference_prediction, primary_error,
      session.authentication_error_code());
  // Equivalent to the existing runner's visual qualifiers, now bound to the
  // owned committed generation rather than retained text. A skipped/failed
  // render, stale record, or mere window creation cannot provide this fact.
  if(keyboard && success && !session.live_runtime_error() &&
      last_view_status==hlclient::app::LiveVisualViewStatus::ready &&
      world_stats.draw_call_count>0U && world_stats.upload_count>0U &&
      non_clear_framebuffers>0U)
    receipt_fact_observed(hlclient::app::SessionReceiptFact::live_visual_verified);
  receipts.facts();
  using CloseReason=hlclient::app::SessionCloseReason;
  const auto close_reason=session.live_runtime_error() &&
      session.live_runtime_error()->code==hlclient::goldsrc::LiveRuntimeStageErrorCode::server_disconnected
      ? CloseReason::server_disconnect : session.live_runtime_error() || handshake_failed
      ? CloseReason::fatal_failure : console_disconnected ? CloseReason::console_disconnect : options.external_server_client &&
        (external_cancelled || window_closed) && !external_gameplay_committed ? CloseReason::startup_cancel
      : window_closed ? CloseReason::window_close
      : timed_session_complete ? CloseReason::duration_limit : CloseReason::startup_cancel;
  receipts.close_network_generation(close_summary.session_generation);
  const auto final_close=application_close_receipt(close_summary,close_reason,receipts,
      close_intent_stamp,socket_closed_stamp,static_cast<bool>(socket_close_error));
  receipts.finish(final_close);
  const bool close_failed=socket_close_error || receipts.failed() ||
      hlclient::app::session_close_failed(final_close);
  if(integrity_failure && (socket_close_error || receipts.failed()))*integrity_failure=true;
  std::cout << "[session-close] reason=" << hlclient::app::to_string(close_reason)
      << " outcome=" << hlclient::goldsrc::to_string(close_summary.outcome)
      << " sent=" << close_summary.successful_send_count
      << " ack=" << close_summary.ack_observed
      << " server_notice=" << close_summary.server_disconnect_observed
      << " native_socket_closed=" << !static_cast<bool>(socket_close_error)
      << " receipt_failed=" << receipts.failed() << '\n';
  const auto& runtime_failure = session.live_runtime_error();
  const auto use_evidence = game_client->action_evidence();
  server_audio.reset(0);
  const auto audio_stats=server_audio.statistics();
  const auto audio_device=audio_output.statistics();
  const auto local_sample_status=[](hlclient::goldsrc::SoundAssetStatus status) -> std::string_view {
    using S=hlclient::goldsrc::SoundAssetStatus;
    switch(status) {
      case S::pending:return "pending";
      case S::ready:return "ready";
      case S::missing:return "absent";
      case S::unsupported:return "unsupported";
      case S::limit:return "resource_limit";
      case S::not_authorized:return "not_authorized";
      case S::open_failed:return "open_failed";
      case S::decode_failed:return "decode_failed";
    }
    return "unknown";
  };
  const auto effect_profile=game_client->local_impact_assets();
  const auto impact_diagnostic=game_client->last_world_impact_diagnostic();
  const auto impact_reference=impact_diagnostic ?
      std::optional{impact_diagnostic->selected_sound} :
      effect_profile ? std::optional{effect_profile->sound} :
      std::optional<hlclient::game_api::LocalSoundReference>{};
  const auto impact_sample_status=sound_assets && impact_reference
      ? local_sample_status(sound_assets->request_local(*impact_reference).status)
      : "not_requested";
  const auto shell_sample_status=sound_assets && effect_profile
      ? local_sample_status(sound_assets->request_local(effect_profile->shell_contact_sound).status)
      : "not_requested";
  const auto use_value = [](const auto& value) { return value ? std::to_string(*value) : "unavailable"; };
  const bool use_delta = (use_evidence.use_health_before && use_evidence.use_health_after &&
          use_evidence.use_health_before != use_evidence.use_health_after) ||
      (use_evidence.use_armor_before && use_evidence.use_armor_after &&
          use_evidence.use_armor_before != use_evidence.use_armor_after);
  std::cout << "[remote-effects-summary] received=" << remote_effect_stats.received
            << " accepted=" << remote_effect_stats.accepted
            << " unresolved=" << remote_effect_stats.unresolved
            << " unsupported=" << remote_effect_stats.unsupported
            << " local_echo=" << remote_effect_stats.local_echo
            << " late=" << remote_effect_stats.late
            << " invalid=" << remote_effect_stats.invalid
            << " fire=" << remote_effect_stats.fire
            << " swing=" << remote_effect_stats.swing
            << " audio_submitted=" << remote_audio.statistics().submitted
            << " audio_late=" << remote_audio.statistics().late
            << " audio_missing=" << remote_audio.statistics().missing
            << " audio_rejected=" << remote_audio.statistics().limits
            << " shells=" << remote_effects.statistics().shells
            << " impact_hits=" << remote_effects.statistics().impact_hits
            << " flash_submissions=" << remote_effects.statistics().flash_submissions << '\n';
  std::cout << "live_application_outcome result=" << application_outcome.application_result
            << " primary_error=" << application_outcome.primary_error;
  // Preserve the owning decoder boundary before bulky optional counters can
  // exhaust a bounded retained diagnostic window (including a peer failure).
  if(runtime_failure && runtime_failure->runtime_failure)
    std::cout << hlclient::goldsrc::runtime_failure_summary(*runtime_failure->runtime_failure);
  if(session.authentication_error_code() && !runtime_failure)
    std::cout << " error_domain=authentication_provider failure_stage=authentication";
  else if(handshake_failed && !runtime_failure)
    std::cout << " error_domain=handshake failure_stage=handshake";
  std::cout << " audio_backend=" << audio_output.status()
            << " audio_error=" << (audio_output.status()=="audio_unavailable" ? "device_unavailable" : server_audio.first_error())
            << " audio_start_messages=" << sound_events->starts << " audio_stop_messages=" << sound_events->stops
            << " audio_static_messages=" << sound_events->statics << " audio_change_messages=" << sound_events->changes
            << " audio_started=" << audio_device.mixer.started << " audio_stopped=" << audio_device.mixer.stopped
            << " audio_updated=" << audio_device.mixer.updated << " audio_duplicates=" << sound_events->duplicates
            << " audio_unsupported=" << audio_stats.unsupported << " audio_missing=" << audio_stats.missing
            << " audio_expired=" << audio_stats.expired << " audio_limits=" << audio_stats.limits
            << " audio_loads=" << audio_stats.loads << " audio_queue_drops=" << (sound_events->dropped+audio_device.queue_drops)
            << " audio_output_frames=" << audio_device.mixer.frames << " audio_queued_frames=" << audio_device.queued_frames
            << " audio_underruns=unmeasured"
            << " audio_sentences=" << audio_stats.sentences << " audio_formats=" << audio_stats.formats
            << " weapon_audio_actions=" << local_audio_stats.actions
            << " weapon_audio_fire=" << local_audio_stats.fire << " weapon_audio_reload=" << local_audio_stats.reload
            << " weapon_audio_deploy=" << local_audio_stats.deploy << " weapon_audio_swing=" << local_audio_stats.swing
            << " weapon_audio_markers=" << local_audio_stats.markers
            << " weapon_audio_duplicates=" << (local_audio_stats.duplicates+local_audio.statistics().duplicates)
            << " weapon_audio_marker_duplicates=" << local_audio_stats.duplicates
            << " weapon_audio_delivery_duplicates=" << local_audio.statistics().duplicates
            << " weapon_audio_timeline_corrections=" << local_audio_stats.timeline_corrections
            << " weapon_audio_late=" << (local_audio_stats.late+local_audio.statistics().late)
            << " weapon_audio_cancelled=" << (local_audio_stats.cancelled+local_audio.statistics().cancelled)
            << " weapon_audio_missing=" << local_audio.statistics().missing
            << " weapon_audio_submitted=" << local_audio.statistics().submitted
            << " weapon_audio_started=" << audio_device.mixer.presentation_started
            << " weapon_audio_invalid=" << (local_audio_stats.invalid+local_audio.statistics().limits)
            << " weapon_audio_muted=" << local_audio.statistics().muted
            << " movement_audio_observations=" << local_audio_stats.movement_observations
            << " movement_audio_clock_dropped=" << local_audio_stats.movement_clock_dropped
            << " movement_audio_invalid_time=" << local_audio_stats.movement_invalid_time
            << " movement_audio_steps=" << local_audio_stats.footstep
            << " movement_audio_ladders=" << local_audio_stats.ladder
            << " movement_audio_landings=" << local_audio_stats.landing
            << " movement_audio_suppressed=" << local_audio_stats.movement_suppressed
            << " movement_audio_replay=" << local_audio_stats.movement_replay
            << " movement_audio_surface_unavailable=" << local_audio_stats.movement_material_unavailable
            << " movement_audio_history_gaps=" << local_audio_stats.movement_history_gaps
            << " movement_audio_quiet=" << local_audio_stats.movement_quiet
            << " movement_audio_movevars_missing=" << local_audio_stats.movement_movevars_missing
            << " movement_audio_movevars_disabled=" << local_audio_stats.movement_movevars_disabled
            << " movement_audio_unsupported=" << local_audio_stats.movement_unsupported
            << " movement_audio_duplicates=" << local_audio_stats.movement_duplicates
            << " movement_audio_outbox_limit=" << local_audio_stats.movement_outbox_limit
            << " movement_audio_submitted=" << local_audio.statistics().movement_submitted
            << " movement_audio_missing=" << local_audio.statistics().movement_missing
            << " movement_audio_late=" << local_audio.statistics().movement_late
            << " movement_audio_muted=" << local_audio.statistics().movement_muted
            << " visual_fire_actions=" << local_visual_stats.fire_actions_received
            << " visual_flash_scheduled=" << local_visual_stats.flash_scheduled
            << " visual_flash_expired=" << local_visual_stats.flash_expired
            << " visual_flash_render_submissions=" << flash_render_submissions
            << " visual_light_requested=" << local_visual_stats.light_requested
            << " visual_light_expired=" << local_visual_stats.light_expired
            << " visual_light_render_submissions=" << light_render_submissions
            << " visual_light_attachment_missing=" << light_attachment_missing
            << " visual_shells_scheduled=" << local_visual_stats.shells_scheduled
            << " visual_shells_created=" << transient_visuals.statistics().shells_created
            << " visual_shells_active=" << transient_visuals.statistics().active
            << " visual_shells_expired=" << transient_visuals.statistics().shells_expired
            << " visual_shell_contacts=" << transient_visuals.statistics().collision_contacts
            << " shell_contact_events=" << transient_visuals.statistics().contact_events
            << " shell_contact_repeat_suppressed=" << transient_visuals.statistics().contact_repeat_suppressed
            << " shell_contact_event_drops=" << transient_visuals.statistics().contact_events_dropped
            << " visual_shell_render_submissions=" << shell_render_submissions
            << " visual_shell_frame_rejected=" << shell_frame_rejected
            << " visual_shell_culled=" << shell_culled
            << " visual_shell_resource_status=" << (local_assets
                ? hlclient::app::to_string(local_assets->shell_resource_status())
                : "pending_local_assets")
            << " visual_shell_frame_status=" << (local_assets
                ? hlclient::app::to_string(local_assets->last_shell_frame_status())
                : "not_attempted")
            << " visual_exact_duplicates=" << local_visual_stats.exact_duplicates_suppressed
            << " visual_late_dropped=" << local_visual_stats.late_cues_dropped
            << " visual_resource_missing=" << visual_resource_missing
            << " visual_pool_capacity=" << transient_visuals.statistics().capacity
            << " impact_asset_status=" << (local_assets
                ? hlclient::app::to_string(local_assets->impact_decal_status())
                : "pending_local_assets")
            << " impact_decal_material=" << (effect_profile && effect_profile->material_mode==
                hlclient::game_api::LocalDecalMaterialMode::white_neutral_modulate
                    ? "white_neutral_modulate" : "straight_alpha")
            << " impact_hits=" << world_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::hit)]
            << " impact_misses=" << world_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::miss)]
            << " impact_unsupported_blockers=" << world_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::unsupported_blocker)]
            << " impact_unmapped_surfaces=" << world_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::surface_unmapped)]
            << " impact_invalid_requests=" << world_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::invalid_request)]
            << " impact_active_decals=" << world_impacts.active()
            << " crowbar_action_accepted=" << local_visual_stats.crowbar_actions
            << " crowbar_action_duplicate_suppressed=" << local_visual_stats.crowbar_duplicates
            << " crowbar_trace_requests=" << local_visual_stats.crowbar_requests
            << " crowbar_trace_miss=" << crowbar_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::miss)]
            << " crowbar_trace_world_hit=" << crowbar_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::hit)]
            << " crowbar_trace_unsupported_blocker=" << crowbar_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::unsupported_blocker)]
            << " crowbar_trace_startsolid=" << crowbar_impact_status_counts[static_cast<std::size_t>(
                hlclient::app::WorldImpactStatus::start_solid)]
            << " crowbar_hit_animation=" << crowbar_hit_animations
            << " crowbar_miss_animation=" << crowbar_miss_animations
            << " crowbar_contact_audio_requested=" << crowbar_contact_audio_requests
            << " crowbar_decal_asset_status=" << (local_assets
                ? hlclient::app::to_string(local_assets->impact_decal_status(1U))
                : "pending_local_assets")
            << " crowbar_decal_pending=" << crowbar_decal_pending
            << " crowbar_decal_published=" << crowbar_decal_published
            << " crowbar_decal_cancelled_stale=" << crowbar_decal_cancelled_stale
            << " impact_audio_requested=" << world_impact_sound_requests
            << " impact_audio_submitted=" << local_audio.statistics().impact_submitted
            << " impact_audio_missing=" << local_audio.statistics().impact_missing
            << " impact_audio_late=" << local_audio.statistics().impact_late
            << " impact_audio_muted=" << local_audio.statistics().impact_muted
            << " impact_audio_rejected=" << local_audio.statistics().impact_rejected
            << " local_audio_resource_pending=" << local_audio.statistics().resource_pending
            << " local_audio_not_authorized=" << local_audio.statistics().resource_not_authorized
            << " local_audio_open_failed=" << local_audio.statistics().resource_open_failed
            << " local_audio_decode_failed=" << local_audio.statistics().resource_decode_failed
            << " local_audio_queue_rejected=" << local_audio.statistics().output_queue_rejected
            << " local_audio_mixer_rejected=" << audio_device.mixer.rejected
            << " impact_audio_resource=" << impact_sample_status
            << " impact_material_last_surface=" << (impact_diagnostic
                ? std::to_string(impact_diagnostic->source_surface_index) : "unavailable")
            << " impact_material_last_texture=" << (impact_diagnostic
                ? impact_diagnostic->normalized_texture_key.data() : "unavailable")
            << " impact_material_last_kind=" << (impact_diagnostic
                ? impact_diagnostic->material_label.data() : "unavailable")
            << " impact_material_last_source=" << (impact_diagnostic
                ? impact_diagnostic->classification_source.data() : "unavailable")
            << " impact_material_last_sample=" << (impact_reference
                ? impact_reference->name() : std::string_view{"unavailable"})
            << " shell_audio_resource=" << shell_sample_status
            << " shell_audio_submitted=" << local_audio.statistics().shell_submitted
            << " shell_audio_missing=" << local_audio.statistics().shell_missing
            << " shell_audio_late=" << local_audio.statistics().shell_late
            << " shell_audio_muted=" << local_audio.statistics().shell_muted
            << " shell_audio_rejected=" << local_audio.statistics().shell_rejected
            << " runtime_error=" << (runtime_failure && runtime_failure->runtime_code
                ? hlclient::goldsrc::to_string(*runtime_failure->runtime_code) : "unavailable")
            << " parser_error=" << (runtime_failure && runtime_failure->runtime_decoder_error
                ? hlclient::goldsrc::to_string(*runtime_failure->runtime_decoder_error) : "unavailable")
            << " baseline_detail=\"" << (runtime_failure && runtime_failure->runtime_failure &&
                runtime_failure->runtime_decoder_error ==
                    hlclient::goldsrc::PacketEntityDecodeErrorCode::invalid_baseline_reference
                ? runtime_failure->runtime_failure->context : "unavailable") << '"'
            << " opcode=" << (runtime_failure && runtime_failure->wire_opcode
                ? std::to_string(*runtime_failure->wire_opcode) : "unavailable")
            << " cursor=" << (runtime_failure && runtime_failure->runtime_cursor
                ? std::to_string(runtime_failure->runtime_cursor->absolute_bit_offset()) : "unavailable")
            << " record=" << (runtime_failure && runtime_failure->runtime_record_ordinal
                ? std::to_string(*runtime_failure->runtime_record_ordinal) : "unavailable")
            << " source_sequence=" << (runtime_failure && runtime_failure->runtime_source_sequence
                ? std::to_string(*runtime_failure->runtime_source_sequence) : "unavailable")
            << " scripted_coverage=" << application_outcome.scripted_coverage
            << " prediction_coverage=" << application_outcome.prediction_coverage
            << " inventory_notifications=" << inventory_notifications
            << " feedback_rows=" << inventory_feedback_rows
            << " brush_candidates=" << (local_summary ? local_summary->brush_candidates : 0U)
            << " collision_revision=" << final_prediction.collision_context_revision
            << " collision_brushes=" << final_prediction.collision_brush_count
            << " collision_excluded_point_boxes=" << final_prediction.collision_excluded_point_boxes
            << " ground_entity=" << (final_prediction.ground_hit && final_prediction.ground_hit->source_entity_index ? std::to_string(*final_prediction.ground_hit->source_entity_index) : "unavailable")
            << " ground_model=" << (final_prediction.ground_hit ? std::to_string(final_prediction.ground_hit->source_model_index) : "unavailable")
            << " ground_normal_x=" << (final_prediction.ground_normal ? std::to_string(final_prediction.ground_normal->x) : "unavailable")
            << " ground_normal_y=" << (final_prediction.ground_normal ? std::to_string(final_prediction.ground_normal->y) : "unavailable")
            << " ground_normal_z=" << (final_prediction.ground_normal ? std::to_string(final_prediction.ground_normal->z) : "unavailable")
            << " grounded_server=" << use_value(final_prediction.server_grounded)
            << " grounded_local=" << use_value(final_prediction.local_grounded)
            << " movement_steps=" << final_prediction.step_selections
            << " brush_server_changes=" << final_prediction.server_brush_transform_changes
            << " brush_render_changes=" << (local_summary ? local_summary->brush_transform_changes : 0U)
            << " brush_server_last_entity=" << (final_prediction.last_changed_brush_entity ? std::to_string(*final_prediction.last_changed_brush_entity) : "unavailable")
            << " brush_server_last_model=" << (final_prediction.last_changed_brush_model ? std::to_string(*final_prediction.last_changed_brush_model) : "unavailable")
            << " brush_render_last_entity=" << (local_summary && local_summary->last_changed_brush_entity ? std::to_string(*local_summary->last_changed_brush_entity) : "unavailable")
            << " brush_render_last_model=" << (local_summary && local_summary->last_changed_brush_model ? std::to_string(*local_summary->last_changed_brush_model) : "unavailable")
            << " base_velocity=" << final_prediction.base_velocity_status
            << " support_policy=" << final_prediction.support_policy
            << " prediction_fallbacks=" << final_prediction.fallback_count
            << " prediction_raw_error=" << use_value(final_prediction.maximum_raw_position_error)
            << " prediction_camera_jump=" << use_value(final_prediction.maximum_camera_correction_jump)
            << " prediction_ground_status=" << (final_prediction.last_ground_status ? hlclient::goldsrc::to_string(*final_prediction.last_ground_status) : "unavailable")
            << " prediction_reason=" << final_prediction.reason
            << " prediction_last_fallback=" << final_prediction.last_fallback_reason
            << " brush_resolved=" << (local_summary ? local_summary->resolved_brushes : 0U)
            << " brush_prepared=" << (local_summary ? local_summary->prepared_brush_models : 0U)
            << " brush_hidden=" << (local_summary ? local_summary->hidden_brushes : 0U)
            << " brush_material_unsupported=" << (local_summary ? local_summary->unsupported_brush_materials : 0U)
            << " brush_submitted=" << world_stats.runtime_brush_submitted_count
            << " brush_culled=" << world_stats.runtime_brush_culled_count
            << " brush_uploads=" << world_stats.brush_upload_count
            << " texture_uploads=" << world_stats.uploaded_base_texture_count
            << " brush_reject_entity=" << (local_summary && local_summary->first_brush_rejection ? std::to_string(local_summary->first_brush_rejection->entity_number) : "unavailable")
            << " brush_reject_slot=" << (local_summary && local_summary->first_brush_rejection ? std::to_string(local_summary->first_brush_rejection->model_slot) : "unavailable")
            << " brush_reject_submodel=" << (local_summary && local_summary->first_brush_rejection && local_summary->first_brush_rejection->submodel ? std::to_string(*local_summary->first_brush_rejection->submodel) : "unavailable")
            << " brush_reject_reason=" << (local_summary && local_summary->first_brush_rejection ? hlclient::app::to_string(local_summary->first_brush_rejection->reason) : "unavailable")
            << " brush_reject_revision=" << (local_summary && local_summary->first_brush_rejection ? std::to_string(local_summary->first_brush_rejection->revision) : "unavailable")
            << " use_press=" << (usercmd ? usercmd->use_command_press_count : 0U)
            << " use_release=" << (usercmd ? usercmd->use_command_release_count : 0U)
            << " use_generated=" << (usercmd ? usercmd->use_generated_count : 0U)
            << " use_transmitted=" << (usercmd ? usercmd->use_new_submission_count : 0U)
            << " use_clear_transmitted=" << (usercmd ? usercmd->use_clear_after_release_count : 0U)
            << " use_sent=" << (usercmd && usercmd->use_new_submission_count > 0U)
            << " use_health_before=" << use_value(use_evidence.use_health_before)
            << " use_health_after=" << use_value(use_evidence.use_health_after)
            << " use_armor_before=" << use_value(use_evidence.use_armor_before)
            << " use_armor_after=" << use_value(use_evidence.use_armor_after)
            << " use_server_effect=" << (use_delta ? "observed_delta_cause_unavailable" : "not_observed")
            << " use_reason=unavailable"
            << " use_prediction_state=" << hlclient::goldsrc::to_string(final_prediction.state)
            << " use_prediction_reason=" << final_prediction.reason << '\n';
  std::cout << "live_visual_control result="
            << ((keyboard || damage_respawn_check) && success && !session.live_runtime_error()
                    ? "fresh_project_client_live_visual_control_integrated"
                : options.reference_prediction
                    ? prediction_result
                    : success
                    ? speed_check
                          ? "live_normal_speed_and_shift_walk_verified"
                    : jump_duck_check
                          ? "fresh_project_client_jump_duck_server_verified"
                          : "fresh_project_client_live_visual_control_integrated"
                    : "live_visual_scene_verified_input_partial")
            << " input=" << (keyboard ? "keyboard-mouse"
                                      : damage_respawn_check ? "scripted-damage-respawn-check"
                                      : presentation_check
                                            ? "scripted-fire-reload-presentation-check"
                                      : fire_reload_check
                                            ? "scripted-fire-reload-check"
                                      : weapon_check
                                            ? "scripted-weapon-check"
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
            << (usercmd ? usercmd->first_new_command_sequence.value_or(0U) : 0U)
            << " last_new_sequence="
            << (usercmd ? usercmd->last_new_command_sequence.value_or(0U) : 0U)
            << " current_yaw=" << camera_controller.yaw_degrees()
            << " server_samples="
            << (usercmd ? usercmd->server_sample_count : 0U)
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
  const auto& final_weapon_observation =
      scene_source.world_state().runtime_observation();
  const bool live_weapon_visual_verified =
      viewmodel_pixels_distinct && hud_pixels_distinct &&
      local_assets &&
      local_assets->first_person_status() ==
          hlclient::app::ReplayLocalVisualStatus::ready_studio &&
      viewmodel_presented_frames > 0U && hud_presented_frames > 0U &&
      final_weapon_observation && final_weapon_observation->receiving_client &&
      final_weapon_observation->weapon_hud.active_weapon_id &&
      *final_weapon_observation->weapon_hud.active_weapon_id != 0U &&
      final_weapon_observation->receiving_client->health &&
      final_weapon_observation->receiving_client->viewmodel_index &&
      *final_weapon_observation->receiving_client->viewmodel_index != 0U;
  const bool live_fire_reload_verified = fire_reload_check &&
      success && prediction_success && live_weapon_visual_verified &&
      usercmd && usercmd->attack_new_submission_count > 0U &&
      usercmd->reload_new_submission_count > 0U &&
      server_confirmed_shots > 0U &&
      server_confirmed_reload_starts > 0U &&
      server_confirmed_reload_completions > 0U &&
      clip_before_fire && clip_after_fire && clip_after_reload &&
      *clip_after_fire < *clip_before_fire &&
      *clip_after_reload > *clip_after_fire &&
      reserve_before_reload && reserve_after_reload &&
      *reserve_after_reload < *reserve_before_reload &&
      glock_fire_animation_count > 0U &&
      glock_reload_animation_count > 0U &&
      crowbar_attack_animation_count > 0U &&
      animation_pixels_changed[0U] && animation_pixels_changed[1U] &&
      animation_pixels_changed[2U] &&
      scripted_crowbar_request_sent && weapon_selection_confirmed > 0U;
  if (fire_reload_check) {
    const auto value = [](const auto& item) {
      return item ? std::to_string(*item) : std::string{"unavailable"};
    };
    std::cout << "live_fire_reload result="
              << (live_fire_reload_verified
                      ? "live_primary_fire_reload_and_weapon_animation_verified"
                      : server_confirmed_shots > 0U
                          ? "primary_fire_verified_reload_pending"
                          : "primary_fire_reload_implemented_live_pending")
              << " attack_generated="
              << (usercmd ? usercmd->attack_generated_count : 0U)
              << " reload_generated="
              << (usercmd ? usercmd->reload_generated_count : 0U)
              << " attack_presses="
              << (usercmd ? usercmd->attack_command_press_count : 0U)
              << " attack_releases="
              << (usercmd ? usercmd->attack_command_release_count : 0U)
              << " reload_presses="
              << (usercmd ? usercmd->reload_command_press_count : 0U)
              << " reload_releases="
              << (usercmd ? usercmd->reload_command_release_count : 0U)
              << " attack_new_submitted="
              << (usercmd ? usercmd->attack_new_submission_count : 0U)
              << " reload_new_submitted="
              << (usercmd ? usercmd->reload_new_submission_count : 0U)
              << " server_confirmed_shots=" << server_confirmed_shots
              << " reload_starts=" << server_confirmed_reload_starts
              << " reload_completions=" << server_confirmed_reload_completions
              << " clip_before_fire=" << value(clip_before_fire)
              << " clip_after_fire=" << value(clip_after_fire)
              << " clip_after_reload=" << value(clip_after_reload)
              << " reserve_before_reload=" << value(reserve_before_reload)
              << " reserve_after_reload=" << value(reserve_after_reload)
              << " svc_weaponanim=" << svc_weaponanim_count
              << " glock_fire_animation=" << glock_fire_animation_count
              << " glock_reload_animation=" << glock_reload_animation_count
              << " crowbar_attack_animation=" << crowbar_attack_animation_count
              << " glock_fire_pixels_changed=" << animation_pixels_changed[0U]
              << " glock_reload_pixels_changed=" << animation_pixels_changed[1U]
              << " crowbar_attack_pixels_changed=" << animation_pixels_changed[2U]
              << " crowbar_selection_requested=" << scripted_crowbar_request_sent
              << " selection_confirmed=" << weapon_selection_confirmed
              << " viewmodel_pixels_distinct=" << viewmodel_pixels_distinct
              << " hud_pixels_distinct=" << hud_pixels_distinct
              << " prediction_active_frames=" << prediction_active_frames
              << " server_punch_observations=" << server_punch_observations
              << " maximum_server_punch_degrees="
              << maximum_server_punch_degrees
              << " punch_available="
              << (final_weapon_observation &&
                  final_weapon_observation->receiving_client &&
                  final_weapon_observation->receiving_client->punch_angle.complete())
              << " punch_value="
              << (final_weapon_observation &&
                  final_weapon_observation->receiving_client &&
                  final_weapon_observation->receiving_client->punch_angle.complete()
                      ? std::to_string(*final_weapon_observation->receiving_client->punch_angle.x) +
                            "," + std::to_string(*final_weapon_observation->receiving_client->punch_angle.y) +
                            "," + std::to_string(*final_weapon_observation->receiving_client->punch_angle.z)
                      : "unavailable")
              << '\n';
  }
  std::cout << "live_weapon_presentation result="
            << (live_weapon_visual_verified
                    ? weapon_selection_confirmed > 0U
                          ? "live_viewmodel_weapon_selection_and_basic_hud_verified"
                          : "live_viewmodel_hud_verified_selection_pending"
                    : "viewmodel_hud_implemented_live_pending")
            << " viewmodel_frames=" << viewmodel_presented_frames
            << " hud_frames=" << hud_presented_frames
            << " selection_queued=" << weapon_selection_queued
            << " selection_confirmed=" << weapon_selection_confirmed
            << " viewmodel_pixel_tested=" << viewmodel_pixel_tested
            << " viewmodel_pixels_distinct=" << viewmodel_pixels_distinct
            << " hud_pixel_tested=" << hud_pixel_tested
            << " hud_pixels_distinct=" << hud_pixels_distinct
            << " viewmodel_status="
            << (local_assets
                    ? hlclient::app::to_string(local_assets->first_person_status())
                    : std::string_view{"assets_unavailable"})
            << " pending_selection="
            << (pending_weapon_selection
                    ? std::to_string(*pending_weapon_selection) : "none")
            << " model_index="
            << (final_weapon_observation &&
                        final_weapon_observation->receiving_client &&
                        final_weapon_observation->receiving_client->viewmodel_index
                    ? std::to_string(*final_weapon_observation->receiving_client
                                          ->viewmodel_index)
                    : "unknown")
            << " active_id="
            << (final_weapon_observation &&
                        final_weapon_observation->weapon_hud.active_weapon_id
                    ? std::to_string(*final_weapon_observation->weapon_hud
                                          .active_weapon_id)
                    : "unknown")
            << " hud_hash="
            << (final_weapon_observation
                    ? hlclient::client::runtime_observation_weapon_hud_hash(
                          *final_weapon_observation)
                    : 0U)
            << " canonical_hash="
            << (final_weapon_observation
                    ? final_weapon_observation->canonical_state_hash : 0U)
            << '\n';
  const auto& level = pitch_probe_samples[0U];
  const bool pitch_pixels_valid = pitch_probe_count == pitch_probe_samples.size() &&
      level && *level && level->non_clear_pixel_count > 0U &&
      level->has_non_clear_bounds &&
      std::all_of(pitch_probe_samples.begin() + 1U,
                  pitch_probe_samples.end(), [&](const auto& sample) {
                    return sample && *sample &&
                        sample->color_signature == level->color_signature &&
                        sample->non_clear_pixel_count ==
                            level->non_clear_pixel_count &&
                        sample->minimum_x == level->minimum_x &&
                        sample->minimum_y == level->minimum_y &&
                        sample->maximum_x == level->maximum_x &&
                        sample->maximum_y == level->maximum_y;
                  });
  std::cout << "live_viewmodel_camera result="
            << (pitch_pixels_valid && live_weapon_visual_verified
                    ? "live_viewmodel_camera_space_verified"
                    : "viewmodel_camera_space_implemented_live_pending")
            << " binding_status="
            << (local_assets
                    ? hlclient::app::to_string(local_assets->first_person_status())
                    : std::string_view{"assets_unavailable"})
            << " render_space=camera_local"
            << " pitch=" << camera_controller.pitch_degrees()
            << " yaw=" << camera_controller.yaw_degrees()
            << " draw_count=" << viewmodel_presented_frames
            << " pixel_observation_valid=" << pitch_pixels_valid
            << " pixel_count="
            << (level ? level->non_clear_pixel_count : 0U)
            << " bounds="
            << (level ? std::to_string(level->minimum_x) + "," +
                            std::to_string(level->minimum_y) + "," +
                            std::to_string(level->maximum_x) + "," +
                            std::to_string(level->maximum_y) : "unknown")
            << " resource_revision="
            << (latest_viewmodel_scene &&
                        latest_viewmodel_scene->first_person_entities
                    ? latest_viewmodel_scene->first_person_entities->frame->
                          resource_revision()
                    : 0U)
            << " model_slot="
            << (final_weapon_observation &&
                        final_weapon_observation->receiving_client &&
                        final_weapon_observation->receiving_client->viewmodel_index
                    ? std::to_string(*final_weapon_observation->receiving_client
                                          ->viewmodel_index)
                    : "unknown")
            << " projection_profile=world_optics_camera_local"
            << " probes=" << pitch_probe_count << '\n';
  const auto presentation = game_client->sample(
      std::chrono::duration<double>{Clock::now() - session_started}.count());
  const bool server_fire_verified = server_confirmed_shots >= 2U;
  const bool server_reload_verified = server_confirmed_reload_completions > 0U &&
      clip_after_reload && clip_after_fire && *clip_after_reload > *clip_after_fire &&
      reserve_before_reload && reserve_after_reload && *reserve_after_reload < *reserve_before_reload;
  const bool fire_presented = presentation.primary_fire_starts >= 2U &&
      presentation.primary_fire_confirmed >= 2U && action_presented_frames[0U] > 0U && animation_pixels_changed[0U];
  const bool reload_presented = presentation.reload_starts > 0U &&
      presentation.reload_confirmed > 0U && action_presented_frames[1U] > 0U && animation_pixels_changed[1U];
  const bool recoil_presented = presentation.recoil_confirmed >= 2U &&
      recoil_presented_frames > 0U && presentation.maximum_visual_recoil > 0.05;
  const bool swing_presented = presentation.melee_swing_starts > 0U &&
      presentation.melee_swing_confirmed > 0U && action_presented_frames[2U] > 0U && animation_pixels_changed[2U];
  const bool hud_updated = server_fire_verified && server_reload_verified && hud_pixels_distinct;
  const bool live_presentation_verified = presentation_check && success && prediction_success &&
      live_weapon_visual_verified && pitch_pixels_valid && scripted_crowbar_request_sent &&
      server_fire_verified && server_reload_verified && fire_presented && reload_presented &&
      recoil_presented && swing_presented && hud_updated;
  if (presentation_check) {
    std::cout << "live_weapon_prediction result="
      << (live_presentation_verified ? "live_client_predicted_weapon_presentation_verified"
                                   : "client_weapon_presentation_implemented_live_pending")
      << " server_fire_verified=" << server_fire_verified
      << " server_reload_verified=" << server_reload_verified
      << " glock_fire_animation_presented=" << fire_presented
      << " glock_reload_animation_presented=" << reload_presented
      << " glock_recoil_presented=" << recoil_presented
      << " crowbar_swing_presented=" << swing_presented
      << " hud_server_state_updated=" << hud_updated
      << " crowbar_hit_status=unavailable"
      << " local_weapon_actions_started=" << presentation.actions_started
      << " local_weapon_actions_confirmed=" << presentation.actions_confirmed
      << " local_weapon_actions_rejected=" << presentation.actions_rejected
      << " local_weapon_actions_corrected=" << presentation.actions_corrected
      << " duplicate_actions_suppressed=" << presentation.duplicate_submissions
      << " conflicting_duplicates=" << presentation.conflicting_duplicates
      << " predicted_fire_animations=" << presentation.primary_fire_starts
      << " confirmed_fire_animations=" << presentation.primary_fire_confirmed
      << " predicted_reload_animations=" << presentation.reload_starts
      << " confirmed_reload_animations=" << presentation.reload_confirmed
      << " predicted_swings=" << presentation.melee_swing_starts
      << " confirmed_swing_actions=" << presentation.melee_swing_confirmed
      << " predicted_recoil_count=" << presentation.recoil_starts
      << " confirmed_recoil_count=" << presentation.recoil_confirmed
      << " rejected_recoil_count=" << presentation.recoil_rejected
      << " maximum_visual_recoil=" << presentation.maximum_visual_recoil
      << " recoil_presented_frames=" << recoil_presented_frames
      << " fire_frames=" << action_presented_frames[0U]
      << " reload_frames=" << action_presented_frames[1U]
      << " swing_frames=" << action_presented_frames[2U]
      << " sequence=" << (presentation.visual ? std::to_string(presentation.visual->sequence) : "unavailable")
      << " body=" << (presentation.visual ? std::to_string(presentation.visual->body) : "unavailable")
      << " source=" << (presentation.visual ? std::to_string(static_cast<int>(presentation.visual->source)) : "unavailable")
      << " restart_identity=" << (presentation.visual ? std::to_string(presentation.visual->restart_identity) : "unavailable")
      << " frame=" << presentation.frame_coordinate
      << " completion=" << presentation.animation_completed
      << " glock_profile=public_hl1_glock_presentation_v1"
      << " crowbar_profile=public_hl1_crowbar_presentation_v1"
      << " recoil_profile=public_hl1_glock_visual_recoil_v1"
      << " manual_validation=not_run" << '\n';
  }
  {
    // Always retain one bounded numeric terminal summary, including ordinary
    // manual launches. These are stage counters, not claims of visible pixels.
    const auto delivery=game_client->temporary_delivery_statistics(hlclient::client::TemporaryEffectKind::blood_sprite);
    hlclient::app::TemporaryEffectPresentation::Statistics owner;
    hlclient::app::RuntimeReplayLocalAssets::TemporarySpriteStatistics material;
    for(std::uint8_t tag=1;tag<=3;++tag) {
      const auto o=temporary_effects.statistics(tag);
      owner.invalid+=o.invalid;owner.stale+=o.stale;owner.sprite_evictions+=o.sprite_evictions;
      const auto m=local_assets ? local_assets->temporary_sprite_statistics(tag) :
          hlclient::app::RuntimeReplayLocalAssets::TemporarySpriteStatistics{};
      material.invalid+=m.invalid;material.submitted+=m.submitted;material.absent+=m.absent;
      material.unsupported+=m.unsupported;
    }
    std::ostringstream blood_summary;
    blood_summary<<"[blood-effects-summary] wire="<<delivery.received
      <<" forwarded="<<delivery.forwarded<<" clock_dropped="<<delivery.clock_dropped
      <<" received="<<temporary_effect_stats.blood_received<<" accepted="<<temporary_effect_stats.blood_sprites
      <<" duplicate="<<temporary_effect_stats.blood_duplicate
      <<" invalid="<<temporary_effect_stats.blood_invalid+owner.invalid+material.invalid
      <<" stale="<<temporary_effect_stats.blood_stale<<" palette="<<temporary_effect_stats.blood_palette
      <<" capacity="<<temporary_effect_stats.blood_capacity+owner.sprite_evictions
      <<" delivery_stale="<<temporary_effect_stats.blood_delivery_stale+owner.stale
      <<" materialized="<<material.submitted<<" absent="<<material.absent<<" unsupported="<<material.unsupported
      <<" frame_rejected="<<blood_rejected_batches<<" draw_batches="<<blood_draw_batches;
    hlclient::core::log(LogLevel::info,blood_summary.str());
    std::ostringstream sound_visual_summary;
    sound_visual_summary<<"[sound-visual-summary] received="<<temporary_effect_stats.pop_sound_received
      <<" accepted="<<temporary_effect_stats.pop_sound_accepted
      <<" entity_rejected="<<temporary_effect_stats.pop_entity_rejected
      <<" duplicate="<<temporary_effect_stats.pop_duplicate
      <<" te_correlated="<<temporary_effect_stats.pop_te_correlated
      <<" resource_missing="<<temporary_effect_stats.pop_resource_missing
      <<" palette_unavailable="<<temporary_effect_stats.blood_palette_unavailable
      <<" policy_suppressed="<<temporary_effect_stats.pop_policy_suppressed
      <<" invalid="<<temporary_effect_stats.pop_invalid
      <<" capacity="<<temporary_effect_stats.pop_capacity;
    hlclient::core::log(LogLevel::info,sound_visual_summary.str());
    if(options.net_trace && temporary_effect_stats.pop_sound_accepted)
      hlclient::core::log(LogLevel::info,"[blood-fallback-profile] source="+
          std::string{game_client->temporary_effect_source_label(hlclient::game_api::TemporaryEffectIdentity::Source::server_sound)}+
          " parameters=inferred");
    // The old aggregate can be entirely red bite blood. Keep a separate
    // large-yellow pipeline summary so a missing server pop cannot be
    // misreported as successful rendering merely because a player was bitten.
    if(options.net_trace) {
      const auto delivery_decals=game_client->temporary_delivery_statistics(hlclient::client::TemporaryEffectKind::decal);
      const auto& draw=renderer.decal_statistics();
      std::ostringstream decal_summary;
      decal_summary<<"[surface-decals-summary] wire="<<delivery_decals.received
        <<" forwarded="<<delivery_decals.forwarded<<" clock_dropped="<<delivery_decals.clock_dropped
        <<" accepted="<<temporary_effect_stats.decal_accepted
        <<" index_unresolved="<<temporary_effect_stats.decal_index_unresolved
        <<" asset_unsupported="<<temporary_effect_stats.decal_asset_unsupported
        <<" target_invalid="<<temporary_effect_stats.decal_target_invalid
        <<" delivery_stale="<<temporary_effect_stats.decal_stale
        <<" queue_capacity="<<temporary_effect_stats.decal_capacity;
      for(std::size_t i=0;i<server_decal_asset_status_counts.size();++i)
        decal_summary<<" asset_"<<hlclient::goldsrc::to_string(static_cast<hlclient::goldsrc::DecalAssetStatus>(i))
          <<'='<<server_decal_asset_status_counts[i];
      for(std::size_t i=0;i<server_decal_placement_counts.size();++i)
        decal_summary<<" placement_"<<hlclient::app::to_string(static_cast<hlclient::app::ServerDecalStatus>(i))
          <<'='<<server_decal_placement_counts[i];
      decal_summary<<" pool_evictions="<<server_decal_evictions<<" active="<<world_impacts.server_active()
        <<" rendered_batches_all_decals="<<draw.rendered_batch_count
        <<" rejected_batches_all_decals="<<draw.rejected_batch_count
        <<" proof=submission_not_stock_pixels";
      hlclient::core::log(LogLevel::info,decal_summary.str());
    }
    const auto yellow_owner=temporary_effects.statistics(2);
    const auto yellow_material=local_assets ? local_assets->temporary_sprite_statistics(2) :
        hlclient::app::RuntimeReplayLocalAssets::TemporarySpriteStatistics{};
    std::ostringstream profile_summary;
    profile_summary<<"[blood-profile-summary] red_accepted="<<temporary_effects.statistics(1).accepted
      <<" yellow_small_accepted="<<temporary_effects.statistics(3).accepted
      <<" yellow_large_accepted="<<yellow_owner.accepted
      <<" yellow_large_stale="<<yellow_owner.stale
      <<" yellow_large_invalid="<<yellow_owner.invalid+yellow_material.invalid
      <<" yellow_large_evicted="<<yellow_owner.sprite_evictions
      <<" yellow_large_materialized="<<yellow_material.submitted
      <<" yellow_large_absent="<<yellow_material.absent
      <<" yellow_large_unsupported="<<yellow_material.unsupported
      <<" yellow_large_expired="<<yellow_material.expired
      <<" yellow_large_frame_rejected="<<yellow_material.frame_rejected
      <<" yellow_large_draw_batches="<<large_yellow_blood_draw_batches;
    hlclient::core::log(LogLevel::info,profile_summary.str());
  }
  if (options.net_trace) {
    // Keep transition evidence at the end of the bounded native log, rather
    // than losing it with the first32 startup pose samples. Queued CPU draws
    // and numeric light samples are not claims about visible GPU pixels.
    std::ostringstream visibility_summary;
    visibility_summary << "[remote-player-visibility-summary] changes=" << remote_visibility_journal.total()
      << " retained=" << remote_visibility_journal.events().size()
      << " dropped=" << remote_visibility_journal.dropped() << " evidence=cpu-frame-only";
    hlclient::core::log(LogLevel::info,visibility_summary.str());
    for (const auto& event : remote_visibility_journal.events()) {
      // Build one owned line before using the existing serialized logger.
      // Concurrent progress messages must not split the numeric evidence row.
      std::ostringstream visibility_line;
      visibility_line << "[remote-player-visibility] generation=" << event.generation
        << " source=" << event.source_identity << " ordinal=" << event.source_ordinal
        << " publication=" << event.publication_revision << " entity=" << event.entity
        << " model=" << event.model_slot.value_or(0U)
        << " stage=" << hlclient::app::to_string(event.stage)
        << " game=" << static_cast<unsigned>(event.game_status)
        << " server-seconds=" << event.server_seconds.value_or(-1.0)
        << " distance=" << event.camera_distance.value_or(-1.0)
        << " effects=" << event.effects.value_or(0U) << " render-mode=" << event.render_mode.value_or(0U)
        << " camera=" << print_vector(event.camera_position)
        << " target=" << print_vector(event.camera_target) << " near=" << event.near_plane
        << " bounds=";
      if (event.posed_bounds)
        visibility_line << print_vector(event.posed_bounds->minimum) << ';' << print_vector(event.posed_bounds->maximum);
      else visibility_line << "unavailable";
      visibility_line << " static-light=";
      if (event.static_light_rgb)
        visibility_line << (*event.static_light_rgb)[0] << ',' << (*event.static_light_rgb)[1]
          << ',' << (*event.static_light_rgb)[2];
      else visibility_line << "fallback";
      hlclient::core::log(LogLevel::info,visibility_line.str());
    }
  }
  if (damage_respawn_check) {
    const auto observed = scene_source.world_state().runtime_observation();
    const auto life = observed ? observed->lifecycle : hlclient::client::LocalPlayerLifecycle{};
    const auto check = usercmd ? usercmd->damage_respawn : hlclient::game_api::DamageRespawnScriptSnapshot{};
    const bool cycle = success && !session.live_runtime_error() &&
        check.phase == hlclient::game_api::DamageRespawnPhase::complete &&
        check.server_alive && check.glock_bound && check.crowbar_bound && life.respawns > 0U &&
        check.post_respawn_commands > 0U && check.post_respawn_samples >= 2U &&
        post_respawn_rendered_frames > 0U && usercmd && usercmd->same_driver_retained &&
        final_prediction.state == hlclient::goldsrc::LiveReferencePredictionState::active;
    const auto optional = [](const auto& v) { return v ? std::to_string(*v) : std::string{"unavailable"}; };
    const auto source_text = [](const auto& value) {
      if (!value) return std::string{"unavailable"};
      return std::to_string(value->record_identity) + "-" +
          std::to_string(value->record_ordinal) + "-" +
          std::to_string(value->source_transport_sequence) + "-" +
          std::to_string(value->start_bit_offset) + "-" +
          std::to_string(value->end_bit_offset) + "-" +
          std::to_string(value->reassembled);
    };
    std::cout << "live_damage_respawn result="
        << (cycle ? "live_death_respawn_verified_damage_pending" : "damage_death_respawn_implemented_live_pending")
        << " application_runtime_result=" << (session.live_runtime_error() ? "error" : success ? "completed" : "incomplete")
        << " phase=" << hlclient::game_api::to_string(check.phase)
        << " blocker=" << check.blocker
        << " generation=" << (observed ? std::to_string(observed->generation) : "unavailable")
        << " life_epoch=" << life.life_epoch << " damage_events=" << life.damage_events
        << " damage_live=not_observed"
        << " health_before=" << optional(life.health_before_damage)
        << " health_after=" << optional(life.health_after_damage)
        << " armor_before=" << optional(life.armor_before_damage)
        << " armor_after=" << optional(life.armor_after_damage)
        << " damage_source=" << source_text(life.last_damage_source)
        << " death_source=" << source_text(life.last_death_source)
        << " health_before_source=" << source_text(life.health_before_source)
        << " health_after_source=" << source_text(life.health_after_source)
        << " armor_before_source=" << source_text(life.armor_before_source)
        << " armor_after_source=" << source_text(life.armor_after_source)
        << " local_deaths=" << life.deaths << " dead_flag=" << optional(life.dead_flag)
        << " respawn_input_submitted=" << check.respawn_input_submitted
        << " server_alive=" << check.server_alive
        << " same_session=" << (usercmd ? std::to_string(usercmd->same_driver_retained) : "unavailable")
        << " glock_bound=" << check.glock_bound << " crowbar_bound=" << check.crowbar_bound
        << " post_respawn_commands=" << check.post_respawn_commands
        << " post_respawn_samples=" << check.post_respawn_samples
        << " post_respawn_frames=" << post_respawn_rendered_frames
        << " pre_model=" << optional(life.pre_death_model)
        << " post_model=" << optional(life.post_respawn_model)
        << " pre_weapon=" << optional(life.pre_death_weapon)
        << " post_weapon=" << optional(life.post_respawn_weapon)
        << " hud_health=" << (observed ? optional(observed->weapon_hud.health) : "unavailable")
        << " hud_armor=" << (observed ? optional(observed->weapon_hud.armor) : "unavailable")
        << " hud_weapon=" << (observed ? optional(observed->weapon_hud.active_weapon_id) : "unavailable")
        << " pre_hud_health=" << optional(pre_life_hud_health)
        << " pre_hud_armor=" << optional(pre_life_hud_armor)
        << " pre_hud_health_source=" << source_text(pre_life_hud_health_source)
        << " pre_hud_armor_source=" << source_text(pre_life_hud_armor_source)
        << " prediction_state=" << hlclient::goldsrc::to_string(final_prediction.state)
        << " prediction_reason=" << final_prediction.reason
        << " prediction_anchor=" << optional(final_prediction.anchor_command)
        << " prediction_history_depth=" << final_prediction.history_depth
        << " prediction_history_end=" << (final_prediction.history_depth && usercmd
            ? std::to_string(usercmd->generated_command_count) : "unavailable")
        << " prediction_replayed_commands=" << final_prediction.replayed_commands
        << " prediction_reseed=" << (final_prediction.last_seed_status
            ? hlclient::goldsrc::to_string(*final_prediction.last_seed_status) : "unavailable")
        << " feature_verified=" << cycle << " manual_validation=not_run\n";
    return success && !session.live_runtime_error() && !close_failed ? 0 : 2;
  }
  if(options.external_server_client) {
    const bool fatal=session.live_runtime_error().has_value() || handshake_failed;
    const bool healthy_close=hlclient::app::external_close_succeeded(receipts.snapshot(),fatal,close_failed);
    if(healthy_close && external_gameplay_committed) {
      external_status("completed","normal_close");
      return 0;
    }
    if(healthy_close && close_reason==CloseReason::startup_cancel) {
      external_status("completed","startup_cancel");
      return 0;
    }
    if(healthy_close && close_reason==CloseReason::console_disconnect) {
      external_status("completed","console_disconnect");
      return 0;
    }
    const auto& diagnostic_reason=receipts.external_diagnostics().reason;
    external_status("error",hlclient::app::external_terminal_failure_reason(
        diagnostic_reason!="none" ? std::string_view{diagnostic_reason} : primary_error,
        close_failed));
    return 2;
  }
  if(close_failed) return 2;
  if(console_disconnected && !session.live_runtime_error())return 0;
  return presentation_check ? (live_presentation_verified ? 0 : 2)
      : fire_reload_check ? (live_fire_reload_verified ? 0 : 2)
      : (keyboard ? application_outcome.exit_code == 0
                  : options.reference_prediction ? prediction_success : success) ? 0 : 2;
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

int run(const hlclient::core::CommandLineOptions &options,
        hlclient::app::ConsoleApplication* console = nullptr,
        bool* integrity_failure = nullptr,
        hlclient::app::ConsoleAttemptRetry* connection_retry = nullptr) {
  if(connection_retry)*connection_retry=hlclient::app::ConsoleAttemptRetry::none;
  // Construct before session/provider owners; it outlives their teardown and
  // records milestones before rolling stdout can evict them.
  ApplicationSessionReceipts receipts{options,integrity_failure};
  const auto startup_failure=[&](std::string_view reason,std::string_view code="not_observed") {
    if(options.external_server_client) {
      receipts.external_diagnostics().reason=reason;
      receipts.external_diagnostics().protocol_code=code;
      external_status("error",reason);
    }
  };
  const auto console_startup_cancelled=[&]() {
    if(!console || (!console->session_stop_requested() && !external_cancel_requested(options)))return false;
    hlclient::app::SessionCloseReceipt close;
    close.reason=console->shutdown_requested() || external_cancel_requested(options)
        ? hlclient::app::SessionCloseReason::startup_cancel : hlclient::app::SessionCloseReason::console_disconnect;
    close.outcome=hlclient::app::SessionCloseOutcome::cancel_without_protocol_disconnect;
    receipts.finish(close);
    return true;
  };
  if(console) {
    console->pump_idle();
    if(console_startup_cancelled())return receipts.failed()?2:0;
    if(!options.base_directory) {
      hlclient::core::log(LogLevel::error,"Connection requires a configured local resource root. Select it in the launcher first.");
      startup_failure("asset_root_not_configured");return 2;
    }
  }
  if(external_cancel_requested(options)) {
    hlclient::app::SessionCloseReceipt close;
    close.reason=hlclient::app::SessionCloseReason::startup_cancel;
    close.outcome=hlclient::app::SessionCloseOutcome::cancel_without_protocol_disconnect;
    receipts.finish(close);
    external_status("completed","startup_cancel");
    return receipts.failed() ? 2 : 0;
  }
  if(options.external_server_client) external_status("checking_resources");
  std::shared_ptr<hlclient::game_api::GameClientHost> game_client;
  const bool requires_game = options.runtime_replay_fixture || options.runtime_replay_capture ||
      options.stop_after == hlclient::core::ConnectionStopPoint::live_runtime_state ||
      options.stop_after == hlclient::core::ConnectionStopPoint::live_usercmd_check ||
      options.stop_after == hlclient::core::ConnectionStopPoint::live_visual_control;
  if (requires_game) {
#if HLCLIENT_BUILD_GAME_HALFLIFE
    // Gameplay selection is explicit composition, independent of --game's
    // resource directory (also used by generic viewers/replay assets).
    game_client = std::make_shared<hlclient::game_api::GameClientHost>(
        hlclient::games::halflife::make_half_life_client_module(options.mute_glock_fire_sound));
#else
    hlclient::core::log(LogLevel::error,"This core-only build has no gameplay module");
    startup_failure("gameplay_module_unavailable");
    return 2;
#endif
  }
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
          options.renderer == hlclient::core::RendererBackend::opengl, game_client);
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
          options.runtime_replay_visuals.has_value(), {}, game_client);
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
    startup_failure("asset_importer_registration_failed");
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
      startup_failure("asset_directory_unavailable");
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
      startup_failure("asset_filesystem_rejected");
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
  auto sound_events=std::make_shared<hlclient::goldsrc::CommittedSoundQueue>();
  if (options.connect_endpoint) {
    const auto address =
        hlclient::network::NetworkAddress::parse(*options.connect_endpoint);
    if (!address || address->port() == 0) {
      hlclient::core::log(LogLevel::error,
                          "Invalid IPv4 endpoint for --connect: " +
                              *options.connect_endpoint);
      startup_failure("invalid_endpoint");
      return 1;
    }

    scene_source.mutable_world_state().set_connection_requested(true);
    const bool prepare_custom_resource =
        hlclient::core::requires_local_resource_consistency_preparation(options);
    const bool prepare_file_consistency = options.base_directory &&
        (options.stop_after == hlclient::core::ConnectionStopPoint::live_runtime_state ||
         options.stop_after == hlclient::core::ConnectionStopPoint::live_usercmd_check ||
         options.stop_after == hlclient::core::ConnectionStopPoint::live_visual_control);
    if (prepare_custom_resource || prepare_file_consistency) {
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
        startup_failure("asset_roots_rejected",code);
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
        startup_failure("asset_environment_failed",code);
        return 1;
      }
      local_resource_environment = std::shared_ptr<
          const hlclient::local_resources::LocalResourceEnvironment>{
          std::move(environment.environment)};
      const auto root_count = local_resource_environment->root_count();
      hlclient::core::log(LogLevel::info,
                          "[local-resource] roots validated: count=" +
                              std::to_string(root_count));
      if (prepare_custom_resource) {
      auto provider = [&] {
        if(!console)return hlclient::resource_consistency::PreparedLocalResourceConsistencyProvider::prepare(*local_resource_environment);
        console->set_stage(hlclient::app::LoadingStage::resources);
        auto preparation=std::async(std::launch::async,[environment=local_resource_environment] {
          return hlclient::resource_consistency::PreparedLocalResourceConsistencyProvider::prepare(*environment);
        });
        while(preparation.wait_for(std::chrono::milliseconds{0})!=std::future_status::ready)console->pump_idle();
        return preparation.get();
      }();
      if(console_startup_cancelled())return receipts.failed()?2:0;
      if (!provider) {
        const auto code = provider.error
                              ? hlclient::resource_consistency::to_string(
                                    provider.error->code)
                              : std::string_view{"provider_error"};
        hlclient::core::log(
            LogLevel::error,
            "[local-resource] consistency provider preparation failed: " +
                std::string{code});
        startup_failure("resource_consistency_prepare_failed",code);
        return 1;
      }

      resource_consistency_provider = std::move(provider.provider);
      hlclient::core::log(
          LogLevel::info,
          "[local-resource] consistency material ready: byte-count=" +
              std::to_string(resource_consistency_provider->byte_count()) +
              ", opaque-bytes=" +
              std::to_string(
                  resource_consistency_provider->opaque_byte_count()));
      }
    }

    auto preparation = [&] {
      if(console) {console->set_stage(hlclient::app::LoadingStage::authenticating);console->pump_idle();}
      try {return prepare_runtime_connect_request(options,*address,console);}
      catch(...) {
        startup_failure(options.authentication_provider==hlclient::core::AuthenticationProviderKind::steam ?
            "steam_authentication_prepare_failed":"authentication_prepare_failed");
        throw;
      }
    }();
    if(console_startup_cancelled())return receipts.failed()?2:0;
    external_status("connecting");
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
                      hlclient::core::LiveInputMode::scripted_weapon_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_weapon_check
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_damage_respawn_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_damage_respawn_check
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_fire_reload_presentation_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_fire_reload_presentation_check
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_fire_reload_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_fire_reload_check
            : options.live_input ==
                      hlclient::core::LiveInputMode::scripted_side_check
                  ? hlclient::goldsrc::LiveVisualControlInputSource::
                        scripted_side_check
            : hlclient::goldsrc::LiveVisualControlInputSource::scripted_check,
        options.reference_prediction,
        options.net_trace, game_client, sound_events, options.external_server_client,options.movement_diagnostics,&receipts,
        installation_file_checks_approved(options),
        [&] {
          hlclient::goldsrc::ConnectResponseWaitConfig response_config;
          if(console)response_config.timeout=hlclient::app::console_connect_response_wait(
              options,response_config.timeout);
          return response_config;
        }());
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
    const auto result=run_live_visual_control(scene_source, *challenge_session, options, game_client, sound_events,receipts,console,integrity_failure);
    if(connection_retry && result!=0 && challenge_session->connect_response_timed_out())
      *connection_retry=hlclient::app::ConsoleAttemptRetry::connect_response_timeout;
    return result;
  }
  if (options.renderer == hlclient::core::RendererBackend::null) {
    return run_null_renderer(scene_source, frame_limit,
                             challenge_session.get());
  }
  return run_opengl_renderer(scene_source, frame_limit,
                             challenge_session.get());
}

int run_console_application(hlclient::core::CommandLineOptions options) {
  const bool start_idle=options.open_client;
  // Existing offline test hook isolates bounded idle verification from user
  // configuration. Ordinary launches never require or use this test policy.
  const bool offline_idle_smoke=start_idle && smoke_test_frame_limit().has_value();
  if(start_idle && !options.external_server_client) {
#ifdef _WIN32
    // Standalone idle startup may reuse the user's launcher resource choices,
    // but never invents a default resource root or silently starts connecting.
    if(!offline_idle_smoke)if(const auto path=hlclient::platform::windows::default_external_settings_path())
      if(const auto text=hlclient::platform::windows::read_external_text(*path,hlclient::core::kExternalLaunchMaximumBytes))
        if(const auto config=hlclient::core::decode_external_launch_configuration(*text);config) {
          const auto explicit_base=options.base_directory;
          const auto explicit_runtime=options.steam_api_runtime;
          const auto explicit_show_fps=options.show_fps;
          options.external_server_client=true;
          (void)hlclient::app::apply_external_launch_configuration(*config.configuration,options);
          options.external_server_client=false;
          if(explicit_base)options.base_directory=explicit_base;
          if(explicit_runtime)options.steam_api_runtime=explicit_runtime;
          if(explicit_show_fps)options.show_fps=explicit_show_fps;
        }
#endif
    options.connect_endpoint.reset();
    options.stop_after=hlclient::core::ConnectionStopPoint::live_visual_control;
    options.live_input=hlclient::core::LiveInputMode::keyboard_mouse;
    options.reference_prediction=true;options.live_session_unlimited=true;
    options.authentication_provider=hlclient::core::AuthenticationProviderKind::steam;
  }
  hlclient::game_api::ConsoleVisualTheme theme;
  hlclient::app::ConsoleGameRegistration registration;
#if HLCLIENT_BUILD_GAME_HALFLIFE
  auto theme_module=hlclient::games::halflife::make_half_life_client_module(options.mute_glock_fire_sound);
  theme=theme_module->console_theme();
  registration=&hlclient::games::halflife::register_console_commands;
#endif
  auto background=hlclient::app::make_console_theme_background(theme);
  std::optional<std::future<hlclient::goldsrc::PreparedConsoleBackground>> background_future;
  std::stop_source background_stop;
  hlclient::app::ConsoleApplication console{std::move(options),
      [&,theme](unsigned width,unsigned height,double fraction,bool full) {
        if(background_future && background_future->valid() &&
            background_future->wait_for(std::chrono::milliseconds{0})==std::future_status::ready) {
          auto prepared=background_future->get();
          if(prepared.image)background=std::move(prepared.image);
          hlclient::core::log(LogLevel::info,std::string{"console_background="}+std::string{hlclient::goldsrc::to_string(prepared.status)});
        }
        return hlclient::app::build_console_theme(theme,width,height,fraction,full,background);
      },std::move(registration),false,!smoke_test_frame_limit().has_value()};
  // A visible fallback frame precedes filesystem work, authentication and map
  // parsing. The approved original background replaces it asynchronously.
  console.present_idle();
  hlclient::core::log(LogLevel::info,std::string{"[console-application] first_idle_frame_presented=true start_idle="}+(start_idle?"true":"false"));
  if(start_idle)external_status("idle","console_ready");
  if(!offline_idle_smoke && console.options().base_directory && !theme.background_resource.empty()) {
    const auto base=path_from_utf8(*console.options().base_directory);
    const auto game=console.options().game_directory;
    const std::string resource{theme.background_resource};
    background_future.emplace(std::async(std::launch::async,[base,game,resource,token=background_stop.get_token()] {
      return hlclient::goldsrc::prepare_console_background(base,game,resource,token);
    }));
  }
  const auto result=hlclient::app::run_console_lifecycle(console,start_idle,
      [](auto& current,auto& application) {
        external_failure_reason.clear();external_last_reason.clear();external_last_stage.clear();
        bool integrity_failure{};
        auto connection_retry=hlclient::app::ConsoleAttemptRetry::none;
        int code{};
        try {code=run(current,&application,&integrity_failure,&connection_retry);}
        catch(const std::exception& error) {
          hlclient::core::log(LogLevel::error,error.what());
          return hlclient::app::ConsoleAttemptOutcome{1,error.what(),integrity_failure};
        }
        return hlclient::app::ConsoleAttemptOutcome{code,external_failure_reason.empty()
            ? code ? "Session ended before gameplay was ready; see console diagnostics." : "session_complete"
            : external_failure_reason,integrity_failure,connection_retry};
      },[&]{return external_cancel_requested(console.options());},smoke_test_frame_limit(),
      [](const auto& outcome){external_status(outcome.code ? "recoverable_error" : "idle",
          outcome.code ? "session_failed" : "console_ready");});
  background_stop.request_stop();
  return result;
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
    auto options=*parsed.options;
    external_launch_active=options.external_server_client;
    if(options.external_server_client) {
      external_status("checking_resources");
#ifdef _WIN32
      const auto root=path_from_utf8(*options.session_receipt_root);
      const auto path=path_from_utf8(*options.external_config);
      auto pinned=hlclient::platform::windows::open_secure_output_directory(root);
      std::error_code path_error;
      if(!pinned || !path.is_absolute() || path.filename()!=L"launch-config.json" ||
          !std::filesystem::equivalent(path.parent_path(),pinned.directory->canonical_path(),path_error) || path_error) {
        external_status("error","launch_configuration_path_invalid");
        return 2;
      }
      const auto text=hlclient::platform::windows::read_external_text(path,hlclient::core::kExternalLaunchMaximumBytes);
      if(!text) {external_status("error","launch_configuration_unavailable");return 2;}
      const auto config=hlclient::core::decode_external_launch_configuration(*text);
      if(!config || !hlclient::app::apply_external_launch_configuration(*config.configuration,options)) {
        external_status("error","launch_configuration_invalid");return 2;
      }
#else
      external_status("error","external_launcher_platform_unavailable");return 2;
#endif
    }
    const bool console_path=options.open_client ||
        (options.renderer==hlclient::core::RendererBackend::opengl &&
         options.stop_after==hlclient::core::ConnectionStopPoint::live_visual_control &&
         options.live_input==hlclient::core::LiveInputMode::keyboard_mouse);
    const auto result=console_path ? run_console_application(options) : run(options);
    // The console/window and every session owner have now been destroyed.
    // This marker is application shutdown only, never gameplay acceptance.
    if(console_path && result==0)external_status("completed","application_closed");
    if(options.external_server_client && result!=0 && external_failure_reason.empty())
      external_status("error","client_startup_failed");
    return result;
  } catch (const std::exception &exception) {
    if(external_failure_reason.empty()) external_status("error","client_runtime_failed");
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
