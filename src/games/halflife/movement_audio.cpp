#include <hlclient/games/halflife/movement_audio.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hlclient::games::halflife {
namespace {
[[nodiscard]] std::string_view family(char material) noexcept {
  switch (material) {
  case 'M': return "player/pl_metal";
  case 'D': return "player/pl_dirt";
  case 'V': return "player/pl_duct";
  case 'G': return "player/pl_grate";
  case 'T': return "player/pl_tile";
  case 'S': return "player/pl_slosh";
  default: return "player/pl_step";
  }
}
constexpr std::array<std::string_view,34> preload{
  "player/pl_step1.wav","player/pl_step2.wav","player/pl_step3.wav","player/pl_step4.wav",
  "player/pl_metal1.wav","player/pl_metal2.wav","player/pl_metal3.wav","player/pl_metal4.wav",
  "player/pl_dirt1.wav","player/pl_dirt2.wav","player/pl_dirt3.wav","player/pl_dirt4.wav",
  "player/pl_duct1.wav","player/pl_duct2.wav","player/pl_duct3.wav","player/pl_duct4.wav",
  "player/pl_grate1.wav","player/pl_grate2.wav","player/pl_grate3.wav","player/pl_grate4.wav",
  "player/pl_tile1.wav","player/pl_tile2.wav","player/pl_tile3.wav","player/pl_tile4.wav",
  "player/pl_tile5.wav","player/pl_ladder1.wav","player/pl_ladder2.wav",
  "player/pl_ladder3.wav","player/pl_ladder4.wav","player/pl_fallpain3.wav",
  "player/pl_slosh1.wav","player/pl_slosh2.wav","player/pl_slosh3.wav","player/pl_slosh4.wav"};
[[nodiscard]] game_api::LocalSoundReference reference(std::string_view name) noexcept {
  game_api::LocalSoundReference out;
  out.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
  if (name.size()<out.sample.size()) std::copy(name.begin(),name.end(),out.sample.begin());
  return out;
}
template<std::size_t N> void label(std::array<char,N>& out,std::string_view value) noexcept {
  std::copy_n(value.begin(),std::min(N-1,value.size()),out.begin());
}
std::string_view category(char code) noexcept {
  switch(code) {
  case 'M':return "metal"; case 'D':return "dirt"; case 'V':return "vent";
  case 'G':return "grate"; case 'T':return "tile"; case 'S':return "slosh";
  default:return "concrete"; // PM_MapTextureTypeStepType has no W/P/Y/F/N step.
  }
}
std::uint32_t tile_roll(std::uint64_t occurrence,std::uint32_t command) noexcept {
  auto value=static_cast<std::uint32_t>(occurrence)*1664525U+command*1013904223U;
  value^=value>>16U; value*=0x7feb352dU; value^=value>>15U;
  return value;
}
} // namespace

void HalfLifeMovementAudio::reset() noexcept {
  checkpoints_.fill({}); checkpoint_next_=pending_count_=preload_next_=0;
  phase_={}; generation_=life_epoch_=emitted_through_=occurrence_=0;
  left_=cancelled_=false; ++epoch_; diagnostic_.reset();
  steps_=ladders_=landings_=suppressed_=replayed_=missing_material_=history_gaps_=0;
  quiet_=movevars_missing_=movevars_disabled_=unsupported_=duplicates_=outbox_limit_=0;
}

void HalfLifeMovementAudio::prepare_resources() noexcept {
  preload_next_=0;
}
std::span<const game_api::LocalSoundReference> HalfLifeMovementAudio::resources() noexcept {
  static const auto tokens=[] {
    std::array<game_api::LocalSoundReference,preload.size()> out{};
    for(std::size_t i=0;i<preload.size();++i) out[i]=reference(preload[i]);
    return out;
  }();
  return tokens;
}
void HalfLifeMovementAudio::cancel_life() noexcept {
  if(cancelled_) return;
  cancelled_=true; pending_count_=0; diagnostic_.reset(); ++epoch_;
}
void HalfLifeMovementAudio::diagnose(const game_api::MovementAudioObservation& o,
    const MaterialLookup& lookup,std::string_view step,std::string_view decision,
    std::string_view sample,float volume) noexcept {
  game_api::MovementAudioDiagnostic d;
  d.serial=++diagnostic_serial_; d.generation=o.generation; d.life_epoch=o.life_epoch;
  d.ordinal=occurrence_; d.command=o.command_sequence; d.cadence_ms=phase_.remaining_ms;
  d.surface_index=o.source_surface_index; d.material_index=o.source_material_index;
  d.texture_index=o.source_texture_index; d.model_index=o.source_model_index;
  d.texture_key=lookup.key; label(d.material,material_name(lookup.kind));
  label(d.classification,material_source_name(lookup.source));
  label(d.step_category,step); label(d.decision,decision);
  using Mode=game_api::MovementAudioMode;
  label(d.movement_mode,o.after_mode==Mode::ground ? "ground" :
      o.after_mode==Mode::airborne ? "airborne" :
      o.after_mode==Mode::ladder ? "ladder" : "unsupported");
  label(d.speed_band,o.total_speed==0 ? "stationary" :
      o.total_speed<((o.ducked || o.after_mode==Mode::ladder) ? 80.F:210.F) ? "walk":"run");
  d.sample=reference(sample); d.speed=o.total_speed; d.volume=volume;
  d.left=left_; d.ducked=o.ducked; d.grounded=o.after_grounded; diagnostic_=d;
}

void HalfLifeMovementAudio::rewind(std::uint32_t boundary) noexcept {
  if (phase_.command==boundary) return;
  for (const auto& saved:checkpoints_) if (saved.command==boundary && boundary) {
    phase_=saved; return;
  }
  // A fresh seed has no local timer history. A later exhausted history does:
  // retain its phase instead of manufacturing another first step on each ACK.
  // Rewind only the timer. Heard presentation ordinal/side are monotonic.
  phase_.command=boundary;
  if(!boundary) {
    phase_.remaining_ms=0; // Known initial boundary, not an exhausted suffix.
    phase_.quiet_onset_pending=false;
  }
  ++history_gaps_;
}

void HalfLifeMovementAudio::emit(std::string_view sample,
    game_api::LocalSoundKind kind,game_api::LocalSoundChannel channel,float volume,
    const game_api::MovementAudioObservation& o,std::uint32_t ordinal,bool replay) noexcept {
  if (replay || o.command_sequence<=emitted_through_) {++replayed_; return;}
  if (pending_count_>=pending_.size()) {++suppressed_; ++outbox_limit_; return;}
  auto& cue=pending_[pending_count_++];
  cue={}; cue.reference=reference(sample); cue.kind=kind; cue.channel=channel;
  cue.volume=volume; cue.pitch=1.0F;
  cue.scheduled_seconds=o.scheduled_seconds;
  cue.command_sequence=o.command_sequence; cue.marker_ordinal=ordinal;
  cue.world_origin=o.world_origin; cue.attenuation=o.world_origin ? .8F : 0.0F;
}

void HalfLifeMovementAudio::observe(const game_api::MovementAudioObservation& o,bool replay,
    const HalfLifeMaterials& materials) noexcept {
  if (!o.generation || !o.command_sequence ||
      !o.command_milliseconds || o.command_milliseconds>250 ||
      !std::isfinite(o.scheduled_seconds) || o.scheduled_seconds<0 ||
      !std::isfinite(o.horizontal_speed) || !std::isfinite(o.total_speed) ||
      !std::isfinite(o.before_vertical_velocity) ||
      o.horizontal_speed<0 || o.total_speed<0 || (o.world_origin &&
          (!std::isfinite(o.world_origin->x) || !std::isfinite(o.world_origin->y) ||
           !std::isfinite(o.world_origin->z)))) return;
  if(o.generation<generation_ || (o.generation==generation_ && o.life_epoch<life_epoch_)) {
    ++duplicates_; return;
  }
  if (generation_!=o.generation || life_epoch_!=o.life_epoch) {
    phase_={}; checkpoints_.fill({}); checkpoint_next_=pending_count_=0;
    emitted_through_=occurrence_=0; left_=cancelled_=false; ++epoch_; diagnostic_.reset();
    generation_=o.generation; life_epoch_=o.life_epoch;
  }
  if(cancelled_) return;
  if (o.command_sequence<=phase_.command) {
    ++duplicates_;
    if(!diagnostic_ || std::string_view{diagnostic_->decision.data()}!="replay_duplicate")
      diagnose(o,materials.lookup(o.texture_name),"unavailable","replay_duplicate");
    return;
  }
  const bool historic=replay || o.command_sequence<=emitted_through_;
  // PM_ReduceTimers occurs before PM_UpdateStepSound. An absent/invalid dry
  // context still advances the local clock, but cannot guess a cue.
  phase_.remaining_ms=phase_.remaining_ms>o.command_milliseconds
      ? phase_.remaining_ms-o.command_milliseconds : 0U;
  const bool valid_surface=o.surface_status==game_api::MovementSurfaceStatus::found &&
      !o.texture_name.empty();
  const auto lookup=materials.lookup(valid_surface ? o.texture_name : std::string_view{});
  const char type=material_code(lookup.kind);
  const bool ladder=o.after_mode==game_api::MovementAudioMode::ladder;
  const bool walking=o.after_mode==game_api::MovementAudioMode::ground &&
      o.after_grounded;
  const bool eligible=(ladder || walking) && o.dry_context &&
      (ladder || valid_surface) && o.total_speed>0.0F && !o.jump_edge;
  const float run_threshold=(o.ducked || ladder) ? 80.0F : 210.0F;
  const bool landed=o.before_mode==game_api::MovementAudioMode::airborne && walking;
  // Local-compatible onset: a due but multiplayer-muted step must not make a
  // newly audible run wait a whole quiet cadence. Only that due quiet decision
  // arms this release; speed/direction changes after a heard step do not.
  const bool quiet_decision=!ladder && o.movevars_footsteps.value_or(false) &&
      o.horizontal_speed<=220.0F;
  if(!landed && eligible && phase_.quiet_onset_pending &&
      o.movevars_footsteps.value_or(false) && !ladder && o.horizontal_speed>220.0F)
    phase_.remaining_ms=0;
  const auto step_category=ladder ? std::string_view{"ladder"} : category(type);
  const auto mute_reason=[&]() -> std::string_view {
    if(!o.movevars_footsteps) {++movevars_missing_; return "movevars_missing";}
    if(!*o.movevars_footsteps) {++movevars_disabled_; return "movevars_disabled";}
    if(!ladder && o.horizontal_speed<=220.0F) {++quiet_; return "multiplayer_quiet";}
    return "selected";
  };
  // PM_UpdateStepSound's zero-timer alternative allows a positive low-speed
  // decision too. Multiplayer suppression still prevents a quiet walk voice.
  if (!landed && eligible && phase_.remaining_ms==0) {
    const bool slow=o.total_speed<run_threshold;
    phase_.remaining_ms=ladder ? 350U : (slow ? 400U : 300U);
    if (o.ducked) phase_.remaining_ms+=100U;
    phase_.quiet_onset_pending=quiet_decision;
    // Replay reconstructs cadence, never toggles a heard foot or selects a new cue.
    if(historic) {++replayed_; diagnose(o,lookup,step_category,"replay_suppressed");}
    else {
    const auto occurrence=++occurrence_;
    left_=!left_; // PM_PlayStepSound updates side before mute.
    const auto variant=static_cast<unsigned>((occurrence*2654435761U+
        o.command_sequence*2246822519U)>>31U)&1U;
    unsigned index=left_ ? (variant?4U:2U) : (variant?3U:1U);
    // Pinned tile special: one in five, deterministic here, not stock RNG parity.
    if(!ladder && type=='T' && tile_roll(occurrence,o.command_sequence)%5U==0U)
      index=5U;
    char buffer[64]{};
    const auto base=ladder ? std::string_view{"player/pl_ladder"} : family(type);
    std::copy(base.begin(),base.end(),buffer);
    const auto offset=base.size();
    buffer[offset]=static_cast<char>('0'+index);
    std::memcpy(buffer+offset+1,".wav",5);
    float volume=ladder ? .35F :
        type=='D' ? (slow?.25F:.55F) :
        type=='V' ? (slow?.4F:.7F) : (slow?.2F:.5F);
    if (o.ducked) volume*=.35F;
    auto decision=mute_reason();
    if (decision=="selected") {
      const auto before=pending_count_;
      emit(buffer,ladder ? game_api::LocalSoundKind::ladder :
          game_api::LocalSoundKind::footstep,game_api::LocalSoundChannel::body,
          volume,o,static_cast<std::uint32_t>(occurrence),false);
      if (pending_count_>before) {
        if (ladder) ++ladders_; else ++steps_;
      } else decision="outbox_limit";
    } else ++suppressed_;
    diagnose(o,lookup,step_category,decision,buffer,volume);
    }
  } else if (walking && !valid_surface) {
    ++missing_material_;
    if(!diagnostic_ || std::string_view{diagnostic_->decision.data()}!="surface_unavailable")
      diagnose(o,lookup,"unavailable","surface_unavailable");
  } else if (!o.dry_context || o.after_mode==game_api::MovementAudioMode::unsupported) {
    ++unsupported_;
    if(!diagnostic_ || std::string_view{diagnostic_->decision.data()}!="unsupported_context")
      diagnose(o,lookup,"unavailable","unsupported_context");
  } else if (o.after_mode==game_api::MovementAudioMode::airborne || (!o.after_grounded && !ladder)) {
    if(!diagnostic_ || std::string_view{diagnostic_->decision.data()}!="airborne")
      diagnose(o,lookup,"unavailable","airborne");
  } else if (o.jump_edge || o.total_speed==0.F) {
    const auto reason=o.jump_edge ? std::string_view{"jump_transition"}:std::string_view{"stationary"};
    if(!diagnostic_ || std::string_view{diagnostic_->decision.data()}!=reason)
      diagnose(o,lookup,step_category,reason);
  }
  if(landed) {
    phase_.remaining_ms=(o.total_speed<run_threshold ? 400U:300U)+(o.ducked ? 100U:0U);
    phase_.quiet_onset_pending=false;
  }
  // Only an actual airborne->supported command can land; a correction, seed,
  // brush swap, spawn or support carry does not pass this predicate.
  if (o.before_mode==game_api::MovementAudioMode::airborne &&
      o.after_grounded && walking && o.dry_context && valid_surface &&
      o.before_vertical_velocity< -350.0F) {
    const float fall=-o.before_vertical_velocity;
    const float volume=fall>580.0F ? 1.0F : .85F;
    if (fall>580.0F) emit("player/pl_fallpain3.wav",
        game_api::LocalSoundKind::fall_pain,game_api::LocalSoundChannel::voice,
        1.0F,o,1U,replay);
    if(!historic) {++occurrence_; left_=!left_;}
    const auto decision=historic ? std::string_view{"replay_suppressed"} : mute_reason();
    if (decision=="selected") {
      const auto base=family(type);
      char buffer[64]{}; std::copy(base.begin(),base.end(),buffer);
      buffer[base.size()]=left_?'2':'1';
      std::memcpy(buffer+base.size()+1,".wav",5);
      emit(buffer,game_api::LocalSoundKind::landing,
          game_api::LocalSoundChannel::body,volume,o,2U,replay);
      if (!replay && o.command_sequence>emitted_through_) ++landings_;
      diagnose(o,lookup,step_category,decision,buffer,volume);
    } else {
      if(historic) ++replayed_; else ++suppressed_;
      diagnose(o,lookup,step_category,decision);
    }
  }
  phase_.command=o.command_sequence;
  checkpoints_[checkpoint_next_++%checkpoints_.size()]=phase_;
  if (!replay) emitted_through_=std::max<std::uint64_t>(emitted_through_,o.command_sequence);
}

void HalfLifeMovementAudio::append_to(game_api::LocalAudioBatch& batch) noexcept {
  for (std::size_t i=0;i<pending_count_;++i)
    if (batch.count<batch.cues.size()) {
      auto cue=pending_[i]; cue.scope=batch.scope;
      batch.cues[batch.count++]=cue;
    } else {++suppressed_; ++outbox_limit_;}
  pending_count_=0;
  // Bounded incremental warm-up through the existing asynchronous E1 cache.
  for (std::size_t n=0;n<6 && preload_next_<preload.size() &&
      batch.prepare_count<batch.prepare.size();++n)
    batch.prepare[batch.prepare_count++]=resources()[preload_next_++];
  batch.movement_epoch=epoch_; batch.movement_diagnostic=diagnostic_;
  batch.statistics.footstep=steps_; batch.statistics.ladder=ladders_;
  batch.statistics.landing=landings_; batch.statistics.movement_suppressed=suppressed_;
  batch.statistics.movement_replay=replayed_;
  batch.statistics.movement_material_unavailable=missing_material_;
  batch.statistics.movement_history_gaps=history_gaps_;
  batch.statistics.movement_quiet=quiet_;
  batch.statistics.movement_movevars_missing=movevars_missing_;
  batch.statistics.movement_movevars_disabled=movevars_disabled_;
  batch.statistics.movement_unsupported=unsupported_;
  batch.statistics.movement_duplicates=duplicates_;
  batch.statistics.movement_outbox_limit=outbox_limit_;
}

} // namespace hlclient::games::halflife
