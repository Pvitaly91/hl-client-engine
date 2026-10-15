#include <hlclient/game_api/game_client_host.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace hlclient::game_api {
GameClientHost::GameClientHost(std::unique_ptr<IGameClientModule> module)
    : module_(std::move(module)) {
    if (!module_) throw std::invalid_argument{"Game module is required"};
}
GameClientHost::~GameClientHost() { teardown(); }
std::span<const std::string_view> GameClientHost::effect_sprite_preparation() const noexcept {
    return active_ ? module_->effect_sprite_preparation() : std::span<const std::string_view>{};
}
void GameClientHost::reset(GameSessionIdentity identity) noexcept {
    temporary_delivery_={};
    follow_events_={};follow_generation_=identity.network_generation;
    ++audio_session_;
    // RuntimeReplaySession resets the game generation after the live loop has
    // established its steady-clock origin. That origin belongs to this host
    // lifetime, not to the network/map generation.
    module_->reset(identity); active_ = true;
}
void GameClientHost::teardown() noexcept {
    follow_events_={};follow_generation_=0;
    ++audio_session_;
    movement_audio_time_origin_.reset();
    if (active_) { module_->teardown(); active_ = false; }
}
const GameMovementPolicy& GameClientHost::movement_policy() const noexcept {
    return module_->movement_policy();
}
gameplay_input::GameplayButtonMask GameClientHost::edge_triggered_buttons(
    const client::RuntimeClientObservationState& state) const noexcept {
    return active_ ? module_->edge_triggered_buttons(state) & movement_policy().live_buttons : 0U;
}
GameRecordResult GameClientHost::stage_record(const GameRecordInput& input) const {
    if (!active_) return {{}, std::string{"Game session is torn down"}};
    auto result = module_->stage_record(input);
    if (result.message_failure) {
        const auto& failure = *result.message_failure;
        if (failure.message_name.size() > 63U || failure.actual_body_size > 65'536U ||
            (failure.body_byte_offset && *failure.body_byte_offset >= failure.actual_body_size) ||
            std::none_of(input.messages.begin(), input.messages.end(),
                [&](const auto& message) {
                    return message.source == failure.source &&
                        message.registration_id == failure.registration_id &&
                        message.body.size() == failure.actual_body_size;
                }))
            return {{}, std::string{"Game failure metadata violates its input boundary"}};
    }
    if (!result) return result;
    if((!result.state->screen_fades.empty() || !result.state->screen_shakes.empty()) &&
        result.state->screen_effect_seconds != input.presentation_seconds)
        return {{},std::string{"Game screen effect clock differs from the owning record"}};
    if(result.state->screen_fades.size()>32)
        return {{},std::string{"Game screen fade capacity exceeded"}};
    for(const auto& fade:result.state->screen_fades)if(
        !std::isfinite(fade.duration_seconds) || fade.duration_seconds<0 || fade.duration_seconds>=16 ||
        !std::isfinite(fade.hold_seconds) || fade.hold_seconds<0 || fade.hold_seconds>=16 ||
        std::any_of(fade.rgba.begin(),fade.rgba.end(),[](float v){return !std::isfinite(v) || v<0 || v>1;}) ||
        std::none_of(input.messages.begin(),input.messages.end(),[&](const auto& m){return m.source==fade.source;}))
        return {{},std::string{"Game screen fade has invalid bounds or provenance"}};
    if(result.state->screen_shakes.size()>32)return {{},std::string{"Game visual event capacity exceeded"}};
    for(const auto& shake:result.state->screen_shakes)if(!std::isfinite(shake.amplitude) || shake.amplitude<0 || shake.amplitude>=16 ||
        !std::isfinite(shake.duration_seconds) || shake.duration_seconds<0 || shake.duration_seconds>=16 ||
        !std::isfinite(shake.frequency_hz) || shake.frequency_hz<0 || shake.frequency_hz>=256 ||
        std::none_of(input.messages.begin(),input.messages.end(),[&](const auto& message){return message.source==shake.source;}))
        return {{},std::string{"Game visual event has invalid bounds or provenance"}};
    if (result.state->multiplayer.maximum_clients == 0U ||
        result.state->multiplayer.maximum_clients > kMaximumHudPlayers)
        return {{}, std::string{"Game display directory exceeds its bounds"}};
    for (const auto& player : result.state->multiplayer.players)
        if (player.name.size() > kMaximumPlayerDisplayBytes || player.team.size() > 31U ||
            player.appearance.model.size()>32U || player.appearance.top_color.value_or(0)>360U ||
            player.appearance.bottom_color.value_or(0)>360U)
            return {{}, std::string{"Game display text exceeds its bounds"}};
    for (const auto& team : result.state->multiplayer.teams)
        if (team.name.size() > kMaximumPlayerDisplayBytes)
            return {{}, std::string{"Game team display text exceeds its bounds"}};
    for (const auto& notice : result.state->multiplayer.notices) if (notice &&
        (notice->killer_name.size() > kMaximumPlayerDisplayBytes ||
         notice->victim_name.size() > kMaximumPlayerDisplayBytes || notice->weapon.size() > 63U ||
         (notice->created_seconds && (!std::isfinite(*notice->created_seconds) || *notice->created_seconds < 0.0))))
        return {{}, std::string{"Game notice exceeds its bounds"}};
    if (result.state->pickups.size() > 64U)
        return {{}, std::string{"Game pickup event capacity exceeded"}};
    for (const auto& pickup : result.state->pickups) {
        if (pickup.kind > GameRecordState::Pickup::Kind::item ||
            pickup.item_token.size() > 63U ||
            pickup.source.record_identity == 0U ||
            pickup.source.end_bit_offset <= pickup.source.start_bit_offset ||
            std::none_of(input.messages.begin(), input.messages.end(),
                [&](const auto& message) { return message.source == pickup.source; }))
            return {{}, std::string{"Game pickup event has invalid bounds or provenance"}};
    }
    return result;
}
void GameClientHost::commit_record(GameRecordState&& state) noexcept {
    if (active_) module_->commit_record(std::move(state));
}
ScreenEffectsFrame GameClientHost::screen_effects(double now) const noexcept {
    if(!active_ || !std::isfinite(now) || now<0)return {};
    auto frame=module_->screen_effects(now);
    if(!std::isfinite(frame.roll_degrees) || std::abs(frame.roll_degrees)>16 ||
        std::any_of(frame.translation.begin(),frame.translation.end(),[](float v){return !std::isfinite(v) || std::abs(v)>16;}) ||
        std::any_of(frame.overlay_rgba.begin(),frame.overlay_rgba.end(),[](float v){return !std::isfinite(v) || v<0 || v>1;}))return {};
    return frame;
}
void GameClientHost::set_hud_context(HudPresentationContext context) noexcept {
    if (active_) module_->set_hud_context(context);
}
std::span<const HudManifestRequest> GameClientHost::hud_resource_preparation() const noexcept {
    if (!active_) return {};
    const auto requests = module_->hud_resource_preparation();
    return requests.size() <= kMaximumHudResourceDescriptors ? requests : std::span<const HudManifestRequest>{};
}
bool GameClientHost::bind_hud_resources(const HudResourceSet& resources) {
    if (!active_ || resources.descriptors.size() > kMaximumHudResourceDescriptors) return false;
    std::size_t count{};
    for (const auto& descriptor : resources.descriptors) {
        if (descriptor.name.size() > 128U || descriptor.source_sha256.size() > 64U ||
            descriptor.status > HudResourceStatus::submitted ||
            descriptor.entries.size() > kMaximumHudResourceEntries - count) return false;
        count += descriptor.entries.size();
        for (const auto& entry : descriptor.entries) {
            if (entry.role.size() > 63U || entry.sprite_name.size() > 128U || entry.asset_sha256.size() > 64U ||
                entry.status > HudResourceStatus::submitted) return false;
            if (entry.status == HudResourceStatus::ready &&
                (!entry.resource_id || !entry.resource_revision || !entry.source.width || !entry.source.height ||
                 entry.source.x > entry.frame_width || entry.source.width > entry.frame_width - entry.source.x ||
                 entry.source.y > entry.frame_height || entry.source.height > entry.frame_height - entry.source.y)) return false;
        }
    }
    module_->bind_hud_resources(resources);
    return true;
}
HudMenuResult GameClientHost::hud_menu_input(const client::RuntimeClientObservationState& state,
    const HudMenuInput& input) noexcept {
    if (!active_ || !std::isfinite(input.seconds) || input.seconds < 0.0 ||
        input.cycle_direction < -1 || input.cycle_direction > 1 ||
        (input.group_digit && (*input.group_digit < 1U || *input.group_digit > 5U))) return {};
    auto result = module_->hud_menu_input(state,input);
    try {
        if (result.selection && !module_->inventory_selection(state,*result.selection)) result.selection.reset();
    } catch (...) { result.selection.reset(); }
    return result;
}
std::span<const LocalSoundReference> GameClientHost::hud_sound_preparation() const noexcept {
    if (!active_) return {};
    const auto resources = module_->hud_sound_preparation();
    return resources.size() <= 8U ? resources : std::span<const LocalSoundReference>{};
}
void GameClientHost::hud_selection_result(std::uint8_t weapon,bool queued,double seconds) noexcept {
    if(active_ && weapon && std::isfinite(seconds) && seconds>=0.0)
        module_->hud_selection_result(weapon,queued,seconds);
}
std::optional<double> GameClientHost::presentation_seconds(double seconds) const noexcept {
    if (!movement_audio_time_origin_ || !std::isfinite(seconds)) return {};
    const auto relative = seconds - *movement_audio_time_origin_;
    return relative >= 0.0 && std::isfinite(relative) ? std::optional<double>{relative} : std::nullopt;
}
void GameClientHost::bind_model(std::optional<LocalWeaponModelMetadata> model) {
    if (active_) module_->bind_model(std::move(model));
}
std::span<const std::string_view> GameClientHost::local_model_preparation() const noexcept {
    if(!active_) return {};
    const auto names=module_->local_model_preparation();
    return names.size()<=8U ? names : std::span<const std::string_view>{};
}
bool GameClientHost::bind_model_variants(std::span<const LocalWeaponModelMetadata> models) {
    if(!active_ || models.size()>8U) return false;
    for(std::size_t i=0;i<models.size();++i) {
        const auto& model=models[i];
        if(!model.generation || !model.model_index || !model.resource_revision ||
            model.resource_name.size()>128U || !model.resource_name.starts_with("models/") ||
            !model.resource_name.ends_with(".mdl") || model.resource_name.find("..")!=std::string::npos ||
            model.resource_name.find('\\')!=std::string::npos) return false;
        for(std::size_t j=0;j<i;++j)
            if(models[j].model_index==model.model_index || models[j].resource_name==model.resource_name ||
                models[j].generation!=model.generation) return false;
    }
    module_->bind_model_variants(models);return true;
}
void GameClientHost::observe(const client::RuntimeClientObservationState& state, double time) {
    if (active_) module_->observe(state, time);
}
void GameClientHost::submit(const LocalWeaponSubmittedCommand& command, double time) {
    if (active_) module_->submit(command, time);
}
void GameClientHost::prepare_presentation(const IStaticEffectTrace& query, double time) noexcept {
    if(active_) module_->prepare_presentation(query,time);
}
std::optional<assets::AssetVector3> GameClientHost::command_movement_effect(const LocalWeaponSubmittedCommand& c) noexcept {
    return active_ ? module_->command_movement_effect(c) : std::nullopt;
}
CommandMovementEffectPhase GameClientHost::command_movement_effect_phase() const noexcept {
    return active_ ? module_->command_movement_effect_phase()
                   : CommandMovementEffectPhase::before_movement;
}
BeamEffectsFrame GameClientHost::beam_effects(const IStaticEffectTrace& trace,double now,std::optional<assets::AssetVector3> muzzle) noexcept {
    if(active_) return module_->beam_effects(trace,now,muzzle);return {};
}
std::optional<EntityTrailProfile> GameClientHost::entity_trail_profile(
    std::string_view model,std::string_view sprite) const noexcept {
    return active_ ? module_->entity_trail_profile(model,sprite) : std::nullopt;
}
BeamRibbonGlowProfile GameClientHost::beam_ribbon_glow_profile(std::string_view sprite) const noexcept {
    const auto profile=active_ ? module_->beam_ribbon_glow_profile(sprite) : BeamRibbonGlowProfile{};
    return valid_beam_ribbon_glow(profile) ? profile : BeamRibbonGlowProfile{};
}
BeamSceneLightProfile GameClientHost::beam_scene_light_profile(std::string_view sprite,bool sine_strand) const noexcept {
    const auto profile=active_ ? module_->beam_scene_light_profile(sprite,sine_strand) : BeamSceneLightProfile{};
    return valid_beam_scene_light(profile) ? profile : BeamSceneLightProfile{};
}
void GameClientHost::cancel_uncommitted() noexcept {
    if (active_) module_->cancel_uncommitted();
}
LocalWeaponPresentationSnapshot GameClientHost::sample(double time) noexcept {
    return active_ ? module_->sample(time) : LocalWeaponPresentationSnapshot{};
}
LocalAudioBatch GameClientHost::drain_audio() noexcept {
    auto batch=active_ ? module_->drain_audio() : LocalAudioBatch{};
    batch.statistics.movement_observations=movement_audio_observations_;
    batch.statistics.movement_clock_dropped=movement_audio_clock_dropped_;
    batch.statistics.movement_invalid_time=movement_audio_invalid_time_;
    batch.session=audio_session_; return batch;
}
RemotePlayerPresentationPolicy GameClientHost::remote_player_policy() const noexcept {
    return active_ ? module_->remote_player_policy() : RemotePlayerPresentationPolicy{};
}
PlayerAppearanceSelection GameClientHost::player_appearance(std::uint32_t slot,std::string_view base) const {
    return active_ ? module_->player_appearance(slot,base) : PlayerAppearanceSelection{};
}
std::vector<PlayerPaletteRange> GameClientHost::player_palette(
    const PlayerAppearanceFields& fields,const assets::SkeletalModelAssetData& model) const {
    return active_ ? module_->player_palette(fields,model) : std::vector<PlayerPaletteRange>{};
}
bool GameClientHost::player_model_compatible(const assets::SkeletalModelAssetData& model) const noexcept {
    return active_ && module_->player_model_compatible(model);
}
void GameClientHost::configure_scripted_events(std::span<const ScriptedEventBinding> bindings) noexcept {
    if(active_) module_->configure_scripted_events(bindings);
}
void GameClientHost::committed_scripted_event(const CommittedScriptedEvent& event) noexcept {
    if(!active_) return;
    auto relative=event;
    if(!movement_audio_time_origin_) relative.resolution=ScriptedEventResolution::missing_clock;
    else relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0)
        relative.resolution=ScriptedEventResolution::missing_clock;
    module_->committed_scripted_event(relative);
}
RemoteWeaponEffectsBatch GameClientHost::drain_remote_effects(double now) noexcept {
    // Direct returns preserve guaranteed value elision for the fixed batch.
    // The conditional expression creates two large temporaries in Win32 Debug.
    if (active_) return module_->drain_remote_effects(now);
    return {};
}
void GameClientHost::committed_temporary_effect(const client::CommittedTemporaryEffect& event) noexcept {
    const auto index=static_cast<std::size_t>(event.kind);
    if(index>=temporary_delivery_.size())return;
    auto& delivery=temporary_delivery_[index];++delivery.received;
    if(!active_ || !movement_audio_time_origin_) {++delivery.clock_dropped;return;}
    auto relative=event;relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0) {++delivery.clock_dropped;return;}
    if(relative.kind==client::TemporaryEffectKind::beam_follow) {
        if(relative.generation!=follow_generation_) return;
        if(follow_events_.count<follow_events_.events.size())
            follow_events_.events[follow_events_.count++]=relative;
        else ++follow_events_.dropped;
        return;
    }
    module_->committed_temporary_effect(relative);++delivery.forwarded;
}
void GameClientHost::bind_effect_resource(const EffectResourceBinding& binding) noexcept {
    if(active_ && binding.name.size()<=255) module_->bind_effect_resource(binding);
}
std::string_view GameClientHost::effect_palette_preparation() const noexcept {
    return active_ ? module_->effect_palette_preparation() : std::string_view{};
}
std::span<const DecalAssetRequest> GameClientHost::surface_decal_preparation() const noexcept {
    if(!active_)return {};
    const auto requests=module_->surface_decal_preparation();
    return requests.size()<=kMaximumPreparedDecalAssets ? requests : std::span<const DecalAssetRequest>{};
}
void GameClientHost::bind_effect_palette(const EffectPaletteMetadata& palette) noexcept {
    if(active_ && palette.status<=EffectPaletteStatus::decode_failed)module_->bind_effect_palette(palette);
}
void GameClientHost::configure_effect_policy(const EffectPresentationPolicy& policy) noexcept {
    if(active_)module_->configure_effect_policy(policy);
}
std::string_view GameClientHost::temporary_effect_source_label(TemporaryEffectIdentity::Source source) const noexcept {
    if(!active_)return {};
    const auto label=module_->temporary_effect_source_label(source);
    if(label.size()>64 || !std::ranges::all_of(label,[](char c){return (c>='a'&&c<='z') || c=='_';}))return {};
    return label;
}
void GameClientHost::committed_sound(const CommittedSoundObservation& event) noexcept {
    if(!active_ || !movement_audio_time_origin_)return;
    auto relative=event;relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0)return;
    module_->committed_sound(relative);
}
GameClientHost::FollowEvents GameClientHost::drain_follow_events() noexcept {
    auto result=follow_events_;follow_events_.count=0;follow_events_.dropped=0;return result;
}
TemporaryEffectsBatch GameClientHost::drain_temporary_effects(double now) noexcept {
    return active_ ? module_->drain_temporary_effects(now) : TemporaryEffectsBatch{};
}
std::span<const LocalSoundReference> GameClientHost::temporary_sound_preparation() const noexcept {
    return active_ ? module_->temporary_sound_preparation() : std::span<const LocalSoundReference>{};
}
RemoteWeaponSocketPlan GameClientHost::remote_weapon_socket_plan(const RemoteWeaponModelContext& context) noexcept {
    return active_ ? module_->remote_weapon_socket_plan(context) : RemoteWeaponSocketPlan{};
}
void GameClientHost::remote_weapon_poses(std::span<const RemoteWeaponPoseSample> poses) noexcept {
    if(active_) module_->remote_weapon_poses(poses.first(std::min(poses.size(),std::size_t{32})));
}
void GameClientHost::remote_entities(std::span<const client::RuntimePacketEntityObservation> entities,
    std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept {
    if(active_ && entities.size()<=4096U && maximum_clients<=kMaximumRemotePlayers)
        module_->remote_entities(entities,maximum_clients,ordinal);
}
WorldEntityPresentationIntent GameClientHost::world_entity(const WorldEntityPresentationContext& context) const noexcept {
    return active_ ? module_->world_entity(context) : WorldEntityPresentationIntent{};
}
void GameClientHost::remote_slot_boundary(std::uint32_t entity) noexcept {
    if(active_ && entity && entity<=kMaximumRemotePlayers) module_->remote_slot_boundary(entity);
}
LocalAudioBatch GameClientHost::drain_remote_audio() noexcept {
    auto batch=active_ ? module_->drain_remote_audio() : LocalAudioBatch{};
    batch.session=audio_session_;
    return batch;
}
void GameClientHost::remote_world_impact(const LocalWeaponActionIdentity& action,
    assets::AssetVector3 point,double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    if(active_) module_->remote_world_impact(action,point,time,std::move(surface));
}
void GameClientHost::remote_shell_contact(const LocalShellContact& contact) noexcept {
    if(active_) module_->remote_shell_contact(contact);
}
RemotePlayerPresentationIntent GameClientHost::remote_player(
    const RemotePlayerPresentationContext& input) noexcept {
    return active_ ? module_->remote_player(input) : RemotePlayerPresentationIntent{};
}
LocalVisualFrame GameClientHost::local_visuals(const LocalVisualContext& context) noexcept {
    return active_ ? module_->local_visuals(context) : LocalVisualFrame{};
}
std::optional<LocalImpactAssetProfile> GameClientHost::local_impact_assets() const noexcept {
    return active_ ? module_->local_impact_assets() : std::nullopt;
}
std::optional<LocalCrowbarImpactAssetProfile> GameClientHost::local_crowbar_impact_assets() const noexcept {
    return active_ ? module_->local_crowbar_impact_assets() : std::nullopt;
}
void GameClientHost::accepted_world_impact(const LocalWeaponActionIdentity& action,
    assets::AssetVector3 point, double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    if (active_) module_->accepted_world_impact(action, point, time,std::move(surface));
}
bool GameClientHost::resolved_world_impact(const LocalWeaponActionIdentity& action,
    LocalWorldImpactOutcome outcome, std::optional<assets::AssetVector3> point,
    double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    return active_ && module_->resolved_world_impact(action, outcome, point, time,std::move(surface));
}
std::optional<LocalWorldImpactDiagnostic>
GameClientHost::last_world_impact_diagnostic() const noexcept {
    return active_ ? module_->last_world_impact_diagnostic() : std::nullopt;
}
void GameClientHost::shell_contact(const LocalShellContact& contact) noexcept {
    if (active_) module_->shell_contact(contact);
}
void GameClientHost::configure_movement_materials(std::string_view text) noexcept {
    if (active_) module_->configure_movement_materials(text);
}
std::span<const LocalSoundReference> GameClientHost::movement_sound_preparation() const noexcept {
    return active_ ? module_->movement_sound_preparation() : std::span<const LocalSoundReference>{};
}
void GameClientHost::rewind_movement_audio(std::uint32_t boundary) noexcept {
    if (active_) module_->rewind_movement_audio(boundary);
}
void GameClientHost::observe_movement_audio(
    const MovementAudioObservation& observation, bool replay) noexcept {
    if (!active_) return;
    ++movement_audio_observations_;
    if (!movement_audio_time_origin_) {++movement_audio_clock_dropped_; return;}
    auto relative=observation;
    relative.scheduled_seconds-=*movement_audio_time_origin_;
    if (std::isfinite(relative.scheduled_seconds) && relative.scheduled_seconds>=0)
        module_->observe_movement_audio(relative,replay);
    else ++movement_audio_invalid_time_;
}
void GameClientHost::set_movement_audio_time_origin(double seconds) noexcept {
    if (std::isfinite(seconds) && seconds>=0) movement_audio_time_origin_=seconds;
}
ViewmodelIntent GameClientHost::viewmodel(const client::RuntimeClientObservationState& state,
    std::optional<LocalWeaponModelMetadata> model, double time,
    std::optional<LocalWeaponVisual> visual) {
    return active_ ? module_->viewmodel(state,std::move(model),time,visual) : ViewmodelIntent{};
}
HudState GameClientHost::hud(const client::RuntimeClientObservationState& state, double time) {
    auto hud = active_ ? module_->hud(state,time) : HudState{};
    if (!valid_hud_draw_commands(hud.draw)) {hud.draw = {}; hud.draw_rejected = true;}
    return hud;
}
CameraIntent GameClientHost::camera(const client::RuntimeClientObservationState& state, double punch) const noexcept {
    return active_ ? module_->camera(state,punch) : CameraIntent{};
}
void GameClientHost::observe_action_evidence(const client::RuntimeClientObservationState& state,
    const std::optional<GameActionTraffic>& traffic) noexcept {
    if (active_) module_->observe_action_evidence(state,traffic);
}
GameActionEvidenceSnapshot GameClientHost::action_evidence() const noexcept {
    return active_ ? module_->action_evidence() : GameActionEvidenceSnapshot{};
}
std::optional<GameCommandRequest> GameClientHost::inventory_selection(
    const client::RuntimeClientObservationState& state, std::uint8_t id) const {
    return active_ ? module_->inventory_selection(state,id) : std::nullopt;
}
std::optional<std::uint8_t> GameClientHost::select_group(
    const client::RuntimeClientObservationState& state, std::uint8_t group,
    std::optional<std::uint8_t> current) const {
    return active_ ? module_->select_group(state,group,current) : std::nullopt;
}
std::optional<std::uint8_t> GameClientHost::cycle_inventory(
    const client::RuntimeClientObservationState& state,
    std::optional<std::uint8_t> current, int direction) const {
    return active_ ? module_->cycle_inventory(state,current,direction) : std::nullopt;
}
GameCommandRequest GameClientHost::self_kill_request() const {
    return active_ ? module_->self_kill_request() : GameCommandRequest{};
}
std::optional<std::uint8_t> GameClientHost::scenario_inventory_target(
    const client::RuntimeClientObservationState& state, std::size_t phase) const {
    return active_ ? module_->scenario_inventory_target(state,phase) : std::nullopt;
}
GameScenarioDirective GameClientHost::scenario_command(GameScenario mode,
    const client::RuntimeClientObservationState* state, std::size_t phase, double time, double phase_time) noexcept {
    return active_ ? module_->scenario_command(mode,state,phase,time,phase_time) : GameScenarioDirective{};
}
DamageRespawnScriptSnapshot GameClientHost::damage_respawn_snapshot() const noexcept {
    return module_->damage_respawn_snapshot();
}
}
