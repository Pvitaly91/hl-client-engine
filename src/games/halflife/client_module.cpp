#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/weapon_diagnostics.hpp>
#include <hlclient/games/halflife/server_messages.hpp>
#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/games/halflife/movement_policy.hpp>
#include <hlclient/games/halflife/inventory.hpp>
#include <hlclient/games/halflife/damage_respawn_script.hpp>
#include <hlclient/games/halflife/weapon_presentation_script.hpp>
#include <hlclient/games/halflife/scenario_policy.hpp>
#include <hlclient/games/halflife/action_evidence.hpp>
#include <hlclient/games/halflife/movement_audio.hpp>
#include <hlclient/games/halflife/materials.hpp>
#include <hlclient/games/halflife/weapon_visuals.hpp>
#include <hlclient/games/halflife/remote_player.hpp>
#include <hlclient/games/halflife/player_appearance.hpp>
#include <hlclient/games/halflife/remote_effects.hpp>
#include <hlclient/games/halflife/multiplayer.hpp>
#include <hlclient/games/halflife/temporary_effects.hpp>
#include <hlclient/games/halflife/blood_decal_profile.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/crossbow_profile.hpp>
#include <hlclient/games/halflife/rpg_profile.hpp>
#include <hlclient/games/halflife/gauss.hpp>
#include <hlclient/games/halflife/egon.hpp>
#include <hlclient/games/halflife/sprite_hud.hpp>
#include <hlclient/games/halflife/screen_effects.hpp>
#include <hlclient/games/halflife/hud_resource_profile.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace hlclient::games::halflife {
namespace {
class HalfLifeClientModule final : public game_api::IGameClientModule {
public:
    void set_weapon_diagnostic_observer(game_api::WeaponDiagnosticObserver* observer) noexcept override {
        presentation_.set_weapon_diagnostic_observer(observer);
        gauss_.set_weapon_diagnostic_observer(observer);egon_.set_weapon_diagnostic_observer(observer);
    }
    std::span<const game_api::WeaponDiagnosticCapability> weapon_diagnostic_capabilities() const noexcept override {
        return halflife::weapon_diagnostic_capabilities;
    }
    game_api::WeaponDiagnosticObjectProfile weapon_diagnostic_object_profile(std::string_view model) const noexcept override {
        return halflife::weapon_diagnostic_object_profile(model);
    }
    explicit HalfLifeClientModule(bool mute_glock_fire_sound)
        : movement_(make_movement_policy()), mute_glock_fire_sound_(mute_glock_fire_sound) {}
    void reset(game_api::GameSessionIdentity identity) noexcept override {
        identity_ = identity;
        gauss_.reset(identity);
        egon_.reset(identity);
        screen_effects_.reset();
        shotgun_model_bound_=false;python_model_bound_=false;crossbow_model_bound_=false;rpg_model_bound_=false;
        state_ = {};
        text_messages_.clear();server_text_.reset();
        hud_context_ = {};
        presentation_.reset();
        sprite_hud_.reset();
        movement_audio_.reset();
        weapon_visuals_.reset();
        materials_.reset();
        last_impact_.reset();
        awaiting_impact_.reset();
        awaiting_crossbow_direction_.reset();
        awaiting_extra_impacts_.fill(std::nullopt);
        impact_cue_count_=0;
        shell_cue_count_=0;
        seen_shell_contacts_.fill(std::nullopt);
        next_seen_shell_contact_=0;
        visual_life_epoch_=0;
        evidence_.reset();
        remote_players_.reset();
        remote_death_notices_={};
        remote_effects_.reset(identity.network_generation,identity.receiving_entity,identity.map_generation);
        temporary_effects_.reset(identity.network_generation);
        weapon_script_ = {};
        respawn_script_ = {};
    }
    void teardown() noexcept override { reset({}); }
    void configure_scripted_events(std::span<const game_api::ScriptedEventBinding> bindings) noexcept override {
        gauss_.configure(bindings);
        egon_.configure(bindings);
        remote_effects_.configure(bindings);
        presentation_.configure_scripted_events(bindings);
    }
    void committed_scripted_event(const game_api::CommittedScriptedEvent& event) noexcept override {
        if(known_absent_player(event.emitter_entity))return;
        gauss_.event(event);
        egon_.event(event);
        if(egon_.handles(event.event_index))return;
        if(gauss_.handles(event.event_index))return; // magnitude is not bullet spread
        presentation_.committed_scripted_event(event,identity_.receiving_entity);
        if((shotgun_model_bound_ || python_model_bound_ || crossbow_model_bound_) && identity_.receiving_entity && event.emitter_entity==*identity_.receiving_entity &&
            event.generation==identity_.network_generation)
            weapon_visuals_.remember_committed_shot(presentation_.current_action_identity(),event);
        remote_effects_.commit(event);
    }
    void committed_temporary_effect(const client::CommittedTemporaryEffect& event) noexcept override {
        temporary_effects_.commit(event);
    }
    void bind_effect_resource(const game_api::EffectResourceBinding& binding) noexcept override {
        temporary_effects_.bind_resource(binding);
    }
    std::span<const std::string_view> effect_sprite_preparation() const noexcept override {
        static constexpr std::array<std::string_view,2> sprites{
            "sprites/muzzleflash1.spr", "sprites/muzzleflash2.spr"};
        return sprites;
    }
    std::string_view effect_palette_preparation() const noexcept override {return "gfx/palette.lmp";}
    void bind_effect_palette(const game_api::EffectPaletteMetadata& palette) noexcept override {
        temporary_effects_.bind_palette(palette);
    }
    void configure_effect_policy(const game_api::EffectPresentationPolicy& policy) noexcept override {
        temporary_effects_.configure_policy(policy);
    }
    std::string_view temporary_effect_source_label(game_api::TemporaryEffectIdentity::Source source) const noexcept override {
        return source==game_api::TemporaryEffectIdentity::Source::server_sound ?
            "inferred_from_validated_snark_sound" : source==game_api::TemporaryEffectIdentity::Source::server_record ?
            "received_server_effect" : "game_action";
    }
    void committed_sound(const game_api::CommittedSoundObservation& event) noexcept override {
        temporary_effects_.sound(event);
    }
    std::optional<game_api::EntityTrailProfile> entity_trail_profile(
        std::string_view model,std::string_view sprite) const noexcept override {
        if(model!="models/rpgrocket.mdl" || sprite!="sprites/smoke.spr") return {};
        // Original SDK resource/event, local-compatible smoke tessellation.
        game_api::EntityTrailProfile profile{4,6,22,1.2F,6,5,{1,1,1,1}};
        constexpr std::string_view glow="sprites/hotglow.spr";
        std::copy(glow.begin(),glow.end(),profile.head_sprite.begin());
        // Original rocket model's nozzle is at local X ~= 0, nose at +14.18.
        // Original HL glow asset; local-compatible size/mix, not engine parity.
        profile.head_axis_offset=-1;profile.smoke_opacity=.85F;
        return profile;
    }
    game_api::BeamRibbonGlowProfile beam_ribbon_glow_profile(std::string_view sprite) const noexcept override {
        return sprite=="sprites/xbeam1.spr" ? egon_beam_glow : game_api::BeamRibbonGlowProfile{};
    }
    game_api::BeamSceneLightProfile beam_scene_light_profile(std::string_view sprite,bool sine_strand) const noexcept override {
        return sprite=="sprites/xbeam1.spr" && sine_strand ? egon_observer_beam_light : game_api::BeamSceneLightProfile{};
    }
    game_api::TemporaryEffectsBatch drain_temporary_effects(double now) noexcept override {
        return temporary_effects_.drain(now);
    }
    std::span<const game_api::LocalSoundReference> temporary_sound_preparation() const noexcept override {
        return temporary_effects_.sound_preparation();
    }
    std::span<const game_api::DecalAssetRequest> surface_decal_preparation() const noexcept override {
        return blood_decal_assets;
    }
    game_api::RemoteWeaponSocketPlan remote_weapon_socket_plan(const game_api::RemoteWeaponModelContext& context) noexcept override {
        if(context.virtual_name=="models/p_gauss.mdl")return gauss_.socket_plan(context);
        if(context.virtual_name=="models/p_egon.mdl")return egon_.socket_plan(context);
        return remote_effects_.socket_plan(context);
    }
    void remote_weapon_poses(std::span<const game_api::RemoteWeaponPoseSample> poses) noexcept override {
        gauss_.poses(poses);
        egon_.poses(poses);
        remote_effects_.poses(poses);
    }
    void remote_entities(std::span<const client::RuntimePacketEntityObservation> entities,
        std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept override {
        remote_effects_.entities(entities,maximum_clients,ordinal);
        gauss_.entities(entities,ordinal);
        egon_.entities(entities,ordinal);
        remote_players_.entities(entities,maximum_clients,ordinal);
        temporary_effects_.entities(entities,maximum_clients,ordinal);
    }
    game_api::WorldEntityPresentationIntent world_entity(
        const game_api::WorldEntityPresentationContext& context) const noexcept override {
        if(context.entity.player_movement_schema && known_absent_player(context.entity.entity_number)) {
            game_api::WorldEntityPresentationIntent result;
            result.role=game_api::WorldEntityPresentationRole::unavailable;
            result.reason=game_api::WorldEntityPresentationReason::missing_source_player;
            result.discontinuity=true;return result;
        }
        return remote_players_.world_entity(context);
    }
    void remote_slot_boundary(std::uint32_t entity) noexcept override {
        gauss_.slot_boundary(entity);
        egon_.slot_boundary(entity);
        remote_players_.slot_boundary(entity);remote_effects_.slot_boundary(entity);
        if(entity && entity<=remote_death_notices_.size()) remote_death_notices_[entity-1U].reset();
    }
    game_api::RemoteWeaponEffectsBatch drain_remote_effects(double now) noexcept override {
        return remote_effects_.drain(now);
    }
    game_api::LocalAudioBatch drain_remote_audio() noexcept override { return remote_effects_.audio(); }
    void remote_world_impact(const game_api::LocalWeaponActionIdentity& action,
        assets::AssetVector3 point,double time,std::optional<game_api::LocalWorldSurfaceHit> surface) noexcept override {
        if(const auto direction=remote_effects_.impact(action,point,time,std::move(surface),materials_))
            temporary_effects_.sniper_world_hit(action,point,*direction,time);
    }
    void remote_shell_contact(const game_api::LocalShellContact& contact) noexcept override {
        remote_effects_.contact(contact);
    }
    const game_api::GameMovementPolicy& movement_policy() const noexcept override { return movement_; }
    game_api::ConsoleVisualTheme console_theme() const noexcept override {
        game_api::ConsoleVisualTheme theme;
        theme.title="HALF-LIFE";theme.background_resource="gfx/conback.lmp";
        theme.backdrop={0.09F,0.075F,0.047F};theme.accent={0.95F,0.57F,0.12F,1};
        theme.emblem={{{-0.28F,-0.42F,-0.06F,-0.42F,0.12F},
            {-0.06F,-0.42F,0.30F,0.44F,0.12F},
            {0.05F,-0.14F,-0.29F,0.44F,0.12F},
            {0.30F,0.44F,0.44F,0.40F,0.12F}}};
        theme.emblem_circle=true;return theme;
    }
    gameplay_input::GameplayButtonMask edge_triggered_buttons(
        const client::RuntimeClientObservationState& state) const noexcept override {
        const bool selected=state.generation==identity_.network_generation &&
            ((state.weapon_hud.active_weapon_id==3U && python_model_bound_) ||
             (state.weapon_hud.active_weapon_id==6U && crossbow_model_bound_) ||
             (state.weapon_hud.active_weapon_id==8U && rpg_model_bound_)) &&
            std::any_of(state.weapon_hud.catalogue.begin(),state.weapon_hud.catalogue.end(),
                [&](const auto& w){return w.id==state.weapon_hud.active_weapon_id &&
                    (w.id==8U ? rpg_command(w.command_name) : w.id==3U ? python_command(w.command_name) : crossbow_command(w.command_name));});
        return selected ? gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::attack_secondary) : 0U;
    }
    game_api::RemotePlayerPresentationPolicy remote_player_policy() const noexcept override {
        return HalfLifeRemotePlayerPresentation::policy();
    }
    game_api::PlayerAppearanceSelection player_appearance(std::uint32_t slot,std::string_view base) const override {
        return select_player_appearance(state_.multiplayer,slot,base);
    }
    std::vector<game_api::PlayerPaletteRange> player_palette(const game_api::PlayerAppearanceFields& fields,
        const assets::SkeletalModelAssetData& model) const override { return player_palette_ranges(fields,model); }
    bool player_model_compatible(const assets::SkeletalModelAssetData& model) const noexcept override {
        return HalfLifeRemotePlayerPresentation::compatible_model(model);
    }
    game_api::RemotePlayerPresentationIntent remote_player(
        const game_api::RemotePlayerPresentationContext& context) noexcept override {
        if(!context.server_owned_proxy && known_absent_player(context.entity.entity_number)) {
            game_api::RemotePlayerPresentationIntent result;
            result.status=game_api::RemotePlayerPresentationStatus::stale_source;return result;
        }
        if(context.network_generation!=identity_.network_generation ||
            context.map_generation!=identity_.map_generation) {
            game_api::RemotePlayerPresentationIntent result;
            result.status=game_api::RemotePlayerPresentationStatus::invalid_context;
            return result;
        }
        auto result=remote_players_.sample(context);
        const auto player=context.server_owned_proxy ? context.entity.render_amount.value_or(0U) : context.entity.entity_number;
        if(player && player<=remote_death_notices_.size()) result.death_notice_source=remote_death_notices_[player-1U];
        return result; // Diagnostic only: never used to classify/select/reset.
    }
    game_api::GameRecordResult stage_record(const game_api::GameRecordInput& input) const override {
        return halflife::stage_record(input, input.previous ? &state_ : nullptr,text_messages_);
    }
    std::string_view text_message_preparation() const noexcept override {return "titles.txt";}
    void bind_text_messages(const game_api::TextMessageCatalogue& values) override {text_messages_=values;}
    void commit_record(game_api::GameRecordState&& state) noexcept override {
        for(std::size_t slot=0;slot<state.multiplayer.players.size();++slot) {
            const auto& next=state.multiplayer.players[slot];const auto& previous=state_.multiplayer.players[slot];
            if(next.identity_observed && next.occupant_epoch!=previous.occupant_epoch)
                remote_slot_boundary(static_cast<std::uint32_t>(slot+1U));
        }
        gauss_.maximum_clients(state.multiplayer.maximum_clients);
        presentation_.configure_maximum_clients(state.multiplayer.maximum_clients);
        sprite_hud_.committed(state); // Consume value events before the legacy projection moves them.
        screen_effects_.committed(state);
        server_text_.committed(state);
        presentation_.commit_pickups(state);
        for(const auto& event:state.life_events) if(event.kind==client::RuntimeLifeEventKind::death_notice &&
            event.victim_entity && event.victim_entity<=remote_death_notices_.size())
            remote_death_notices_[event.victim_entity-1U]=event.source;
        state_ = std::move(state);
    }
    std::span<const std::string_view> local_model_preparation() const noexcept override { return satchel_viewmodel_names; }
    void bind_model_variants(std::span<const game_api::LocalWeaponModelMetadata> models) override {
        presentation_.bind_model_variants(models);
    }
    void bind_model(std::optional<game_api::LocalWeaponModelMetadata> model) override {
        gauss_.bind(model);
        egon_.bind(model);
        shotgun_model_bound_=model && model->resource_name=="models/v_shotgun.mdl";
        python_model_bound_=model && python_sequences(*model);
        crossbow_model_bound_=model && crossbow_sequences(*model);
        rpg_model_bound_=model && rpg_sequences(*model);
        if (weapon_visuals_.bind(model)) {
            awaiting_impact_.reset();
            awaiting_crossbow_direction_.reset();
            awaiting_extra_impacts_.fill(std::nullopt);
            impact_cue_count_=0;
        }
        presentation_.bind_model(std::move(model));
    }
    void observe(const client::RuntimeClientObservationState& state, double time) override {
        gauss_.observe(state,time);
        egon_.observe(state,time);
        if (state.lifecycle.dead() || (visual_life_epoch_ &&
            visual_life_epoch_!=state.lifecycle.life_epoch)) {
            movement_audio_.cancel_life();
            weapon_visuals_.cancel();
            weapon_visuals_.reset_fire_cadence();
            awaiting_impact_.reset();
            awaiting_crossbow_direction_.reset();
            awaiting_extra_impacts_.fill(std::nullopt);
            impact_cue_count_=0;
        }
        visual_life_epoch_=state.lifecycle.life_epoch;
        presentation_.observe(state,time);
    }
    void prepare_presentation(const game_api::IStaticEffectTrace& query,double time) noexcept override {
        presentation_.prepare_presentation(query,time,identity_.receiving_entity);
    }
    void submit(const game_api::LocalWeaponSubmittedCommand& command,double time) override {
        if(egon_.selected()) {egon_.command(command);return;}
        if(gauss_.selected()) {(void)gauss_.command(command);return;}
        weapon_visuals_.remember_submission(command);
        presentation_.submit(command,time);
    }
    void cancel_uncommitted() noexcept override {
        gauss_.cancel();
        egon_.cancel();
        presentation_.cancel_uncommitted();
        weapon_visuals_.cancel();
        awaiting_impact_.reset();
        awaiting_crossbow_direction_.reset();
        awaiting_extra_impacts_.fill(std::nullopt);
        impact_cue_count_=0;
    }
    game_api::LocalVisualFrame local_visuals(const game_api::LocalVisualContext& context) noexcept override {
        if(egon_.selected()){egon_.aim(context);return {};}
        if(gauss_.selected())return gauss_.visuals(context.now_seconds);
        auto frame=weapon_visuals_.frame(context);
        if (frame.world_impact) {
            awaiting_impact_=frame.world_impact->action;
            awaiting_crossbow_direction_=frame.world_impact->action.weapon_id==6U ?
                std::optional{frame.world_impact->direction} : std::nullopt;
        }
        awaiting_extra_impacts_.fill(std::nullopt);
        for(std::size_t i=0;i<std::min(frame.extra_impact_count,frame.extra_impacts.size());++i)
            awaiting_extra_impacts_[i]=frame.extra_impacts[i].action;
        return frame;
    }
    std::optional<game_api::LocalImpactAssetProfile> local_impact_assets() const noexcept override {
        game_api::LocalImpactAssetProfile profile;
        // Pinned SDK EV_HLDM_PlayTextureSound uses pl_step1 for concrete;
        // ric1 is an optional separate ricochet, not the material strike.
        constexpr char wad[]="decals.wad", texture[]="{SHOT1";
        constexpr char sound[]="player/pl_step1.wav", shell[]="player/pl_shell1.wav";
        std::copy_n(wad,sizeof(wad),profile.wad_name.begin());
        std::copy_n(texture,sizeof(texture),profile.texture_name.begin());
        std::copy_n(sound,sizeof(sound),profile.sound.sample.begin());
        profile.sound.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        std::copy_n(shell,sizeof(shell),profile.shell_contact_sound.sample.begin());
        profile.shell_contact_sound.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        profile.material_mode=game_api::LocalDecalMaterialMode::white_neutral_modulate;
        profile.material_sounds=material_impact_preload();
        profile.material_sound_count=profile.material_sounds.size();
        profile.supplemental_sounds=bullet_ricochet_preload();
        profile.supplemental_sound_count=profile.supplemental_sounds.size();
        return profile;
    }
    std::optional<game_api::LocalCrowbarImpactAssetProfile>
    local_crowbar_impact_assets() const noexcept override {
        game_api::LocalCrowbarImpactAssetProfile profile;
        // Pinned SDK: Crowbar::Smack -> DecalGunshot(DMG_CLUB) and the
        // ordinary BSP DamageDecal chooses the {shot1..5 registry. {SHOT2 is
        // a deterministic local-compatible choice distinct from Glock E5.
        constexpr char wad[]="decals.wad", texture[]="{SHOT2";
        constexpr char hit1[]="weapons/cbar_hit1.wav";
        constexpr char hit2[]="weapons/cbar_hit2.wav";
        constexpr char concrete[]="player/pl_step1.wav";
        std::copy_n(wad,sizeof(wad),profile.decal.wad_name.begin());
        std::copy_n(texture,sizeof(texture),profile.decal.texture_name.begin());
        profile.decal.material_mode=game_api::LocalDecalMaterialMode::white_neutral_modulate;
        std::copy_n(hit1,sizeof(hit1),profile.strike_sounds[0].sample.begin());
        std::copy_n(hit2,sizeof(hit2),profile.strike_sounds[1].sample.begin());
        std::copy_n(concrete,sizeof(concrete),profile.concrete_contact_sound.sample.begin());
        for (auto& sound:profile.strike_sounds)
            sound.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        profile.concrete_contact_sound.source=
            game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        return profile;
    }
    void accepted_world_impact(const game_api::LocalWeaponActionIdentity& action,
        assets::AssetVector3 point,double time,
        std::optional<game_api::LocalWorldSurfaceHit> surface) noexcept override {
        if (!std::isfinite(time) ||
            !std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) return;
        auto* awaiting=awaiting_impact_ && *awaiting_impact_==action ? &awaiting_impact_ : nullptr;
        for(auto& candidate:awaiting_extra_impacts_) if(candidate && *candidate==action) awaiting=&candidate;
        if(!awaiting) return;
        awaiting->reset();
        if(action.weapon_id==6U) {
            if(!awaiting_crossbow_direction_) return;
            temporary_effects_.sniper_world_hit(action,point,*awaiting_crossbow_direction_,time);
            awaiting_crossbow_direction_.reset();
            if(impact_cue_count_==impact_cues_.size()) return;
            game_api::LocalSoundCue cue;
            cue.reference=crossbow_sound("weapons/xbow_hit1.wav");
            cue.scheduled_seconds=time;cue.volume=1;cue.pitch=1;
            cue.kind=game_api::LocalSoundKind::impact;cue.channel=game_api::LocalSoundChannel::automatic;
            cue.world_origin=point;cue.attenuation=.8F;cue.command_sequence=action.command_sequence;
            cue.marker_ordinal=2;
            impact_cues_[impact_cue_count_++]=cue;return;
        }
        if (impact_cue_count_==impact_cues_.size()) return;
        const auto lookup=materials_.lookup(surface ?
            std::string_view{surface->texture_name.data()} : std::string_view{});
        const bool buckshot=action.weapon_id==7U && action.sub_identity>=1U && action.sub_identity<=8U;
        const auto chosen=material_impact_sound(lookup.kind,action,false);
        const auto ricochet=impact_cue_count_+1U<impact_cues_.size() ?
            bullet_ricochet_sound(action) :
            std::optional<game_api::LocalSoundReference>{};
        record_impact(action,surface,lookup,buckshot ? game_api::LocalSoundReference{} : chosen.reference,ricochet);
        auto cue=game_api::LocalSoundCue{};
        cue.reference=chosen.reference;
        cue.scheduled_seconds=time;
        cue.volume=chosen.volume;
        cue.pitch=1.0F;
        cue.kind=game_api::LocalSoundKind::impact;
        cue.channel=game_api::LocalSoundChannel::automatic;
        cue.world_origin=point;
        cue.attenuation=0.8F;
        cue.command_sequence=action.command_sequence;
        cue.marker_ordinal=0U;
        if(!buckshot) impact_cues_[impact_cue_count_++]=cue;
        if(ricochet && impact_cue_count_<impact_cues_.size()) {
            cue.reference=*ricochet;
            cue.volume=1.0F;
            cue.marker_ordinal=1U;
            impact_cues_[impact_cue_count_++]=cue;
        }
    }
    bool resolved_world_impact(const game_api::LocalWeaponActionIdentity& action,
        game_api::LocalWorldImpactOutcome outcome,
        std::optional<assets::AssetVector3> point,double time,
        std::optional<game_api::LocalWorldSurfaceHit> surface) noexcept override {
        if (!awaiting_impact_ || *awaiting_impact_!=action ||
            action.kind!=game_api::LocalWeaponAction::melee_swing ||
            action.generation!=identity_.network_generation) return false;
        awaiting_impact_.reset();
        if (outcome!=game_api::LocalWorldImpactOutcome::static_world_hit || !point ||
            !std::isfinite(time) || !std::isfinite(point->x) ||
            !std::isfinite(point->y) || !std::isfinite(point->z) ||
            !presentation_.resolve_crowbar_world_hit(action,time)) return false;
        if (impact_cue_count_+2U>impact_cues_.size()) return true;
        const auto profile=local_crowbar_impact_assets();
        const auto lookup=materials_.lookup(surface ?
            std::string_view{surface->texture_name.data()} : std::string_view{});
        const auto chosen=material_impact_sound(lookup.kind,action,true);
        record_impact(action,surface,lookup,chosen.reference);
        const auto variant=(action.command_sequence ^ static_cast<std::uint32_t>(action.generation)) & 1U;
        for (std::uint32_t ordinal=0;ordinal<2U;++ordinal) {
            game_api::LocalSoundCue cue;
            cue.reference=ordinal==0U ? profile->strike_sounds[variant] :
                chosen.reference;
            cue.scheduled_seconds=time;
            cue.volume=ordinal==0U ? crowbar_strike_volume(lookup.kind) : chosen.volume;
            cue.pitch=1.0F;
            cue.kind=game_api::LocalSoundKind::impact;
            cue.channel=game_api::LocalSoundChannel::automatic;
            cue.world_origin=*point;
            cue.attenuation=0.8F;
            cue.command_sequence=action.command_sequence;
            cue.marker_ordinal=ordinal;
            impact_cues_[impact_cue_count_++]=cue;
        }
        return true;
    }
    std::optional<game_api::LocalWorldImpactDiagnostic>
    last_world_impact_diagnostic() const noexcept override { return last_impact_; }
    void shell_contact(const game_api::LocalShellContact& contact) noexcept override {
        if(contact.action.generation!=identity_.network_generation ||
            (contact.action.kind!=game_api::LocalWeaponAction::primary_fire &&
             !(contact.action.weapon_id==7U && contact.action.kind==game_api::LocalWeaponAction::secondary_fire)) ||
            contact.ordinal==0 || contact.ordinal>3 ||
            !std::isfinite(contact.at_seconds) || !std::isfinite(contact.inward_normal_speed) ||
            !std::isfinite(contact.point.x) || !std::isfinite(contact.point.y) ||
            !std::isfinite(contact.point.z) ||
            contact.inward_normal_speed<(contact.ordinal==1 ? 25.0F : 40.0F)) return;
        for(const auto& seen:seen_shell_contacts_) if(seen &&
            seen->action==contact.action && seen->ordinal==contact.ordinal) return;
        if(shell_cue_count_==shell_cues_.size()) return;
        seen_shell_contacts_[next_seen_shell_contact_]={contact.action,contact.ordinal};
        next_seen_shell_contact_=(next_seen_shell_contact_+1)%seen_shell_contacts_.size();
        auto cue=game_api::LocalSoundCue{};
        cue.reference=local_impact_assets()->shell_contact_sound;
        if(contact.action.weapon_id==7U) {
            cue.reference.sample.fill(0);
            constexpr std::string_view sample="weapons/sshell1.wav";
            std::copy(sample.begin(),sample.end(),cue.reference.sample.begin());
        }
        cue.scheduled_seconds=contact.at_seconds;
        cue.volume=contact.ordinal==1 ? 0.7F : contact.ordinal==2 ? 0.45F : 0.3F;
        cue.pitch=1.0F;
        cue.kind=game_api::LocalSoundKind::shell_contact;
        cue.channel=game_api::LocalSoundChannel::automatic;
        cue.world_origin=contact.point;
        cue.attenuation=0.8F;
        cue.command_sequence=contact.action.command_sequence;
        cue.marker_ordinal=contact.ordinal;
        shell_cues_[shell_cue_count_++]=cue;
    }
    game_api::LocalAudioBatch drain_audio() noexcept override {
        auto batch=presentation_.drain_audio();
        sprite_hud_.append_audio(batch);
        gauss_.append_audio(batch);
        movement_audio_.append_to(batch);
        egon_.append_audio(batch);
        if (impact_cue_count_>0) for (std::size_t i=0;i<impact_cue_count_ &&
            batch.prepare_count<batch.prepare.size();++i)
            batch.prepare[batch.prepare_count++]=impact_cues_[i].reference;
        for(std::size_t i=0;i<shell_cue_count_ && batch.prepare_count<batch.prepare.size();++i)
            batch.prepare[batch.prepare_count++]=shell_cues_[i].reference;
        for (std::size_t i=0;i<impact_cue_count_;++i) {
            if (batch.count==batch.cues.size()) break;
            auto cue=impact_cues_[i];
            cue.scope=batch.scope;
            batch.cues[batch.count++]=cue;
        }
        impact_cue_count_=0;
        for (std::size_t i=0;i<shell_cue_count_;++i) {
            if (batch.count==batch.cues.size()) break;
            auto cue=shell_cues_[i];
            cue.scope=batch.scope;
            batch.cues[batch.count++]=cue;
        }
        shell_cue_count_=0;
        if (mute_glock_fire_sound_) {
            // Diagnostic only: suppress the locally predicted Glock fire voice.
            // Keep the action, impact, shell, other weapon and server audio intact.
            batch.count=static_cast<std::size_t>(std::remove_if(
                batch.cues.begin(),batch.cues.begin()+batch.count,
                [](const game_api::LocalSoundCue& cue) {
                    return cue.kind==game_api::LocalSoundKind::fire &&
                        cue.reference.name()=="weapons/pl_gun3.wav";
                })-batch.cues.begin());
        }
        // One serial namespace crosses the E2/E3 shared local-audio sink.
        for (std::size_t i=0;i<batch.count;++i) batch.cues[i].serial=++audio_serial_;
        for (std::size_t i=0;i<batch.control_count;++i) batch.controls[i].serial=++audio_serial_;
        return batch;
    }
    void configure_movement_materials(std::string_view text) noexcept override {
        (void)materials_.configure(text);
        movement_audio_.prepare_resources();
    }
    std::span<const game_api::LocalSoundReference> movement_sound_preparation() const noexcept override {
        return HalfLifeMovementAudio::resources();
    }
    void rewind_movement_audio(std::uint32_t boundary) noexcept override {
        movement_audio_.rewind(boundary);
    }
    void observe_movement_audio(const game_api::MovementAudioObservation& o,
        bool replay) noexcept override { movement_audio_.observe(o,replay,materials_); }
    game_api::LocalWeaponPresentationSnapshot sample(double time) noexcept override {
        if(egon_.selected())return egon_.sample(time);
        (void)egon_.sample(time); // remote persistent lifecycle shares the host clock
        if(gauss_.selected())return gauss_.sample(time);
        auto result=presentation_.sample(time);
        weapon_visuals_.observe(result,time,presentation_.crossbow_sniper_action());
        return result;
    }
    game_api::ViewmodelIntent viewmodel(const client::RuntimeClientObservationState& state,
        std::optional<game_api::LocalWeaponModelMetadata> model, double time,
        std::optional<game_api::LocalWeaponVisual> visual) override {
        return presentation_.viewmodel(state,model,time,visual);
    }
    game_api::ViewmodelIntent viewmodel_borrowed(const client::RuntimeClientObservationState& state,
        const game_api::LocalWeaponModelMetadata* model, double time,
        std::optional<game_api::LocalWeaponVisual> visual) override {
        return presentation_.viewmodel_borrowed(state,model,time,visual);
    }
    game_api::HudState hud(const client::RuntimeClientObservationState& state,double time) override {
        auto hud = presentation_.hud(state,time);
        if(!hud_context_.diagnostic_overlay)hud.draw={};
        sprite_hud_.compose(state,hud,hud_context_,
            presentation_.camera(state,0).horizontal_field_of_view_degrees.value_or(90.0),time);
        const auto first_overlay_rectangle=hud.draw.rectangles.size();
        const auto first_overlay_text=hud.draw.texts.size();
        compose_multiplayer_hud(state_.multiplayer, hud, hud_context_, time,
            sprite_hud_.base_descriptor(),sprite_hud_.base_tier(hud_context_));
        server_text_.compose(hud,hud_context_,time);
        // TAB and the existing feed remain an overlay above gameplay sprites.
        // The renderer orders neutral primitives without knowing HUD meaning.
        for(std::size_t i=first_overlay_rectangle;i<hud.draw.rectangles.size();++i)hud.draw.rectangles[i].draw_order=100U;
        for(std::size_t i=first_overlay_text;i<hud.draw.texts.size();++i)hud.draw.texts[i].draw_order=100U;
        return hud;
    }
    std::span<const game_api::HudManifestRequest> hud_resource_preparation() const noexcept override {
        static const auto requests=halflife_hud_resource_preparation();
        return requests;
    }
    void bind_hud_resources(const game_api::HudResourceSet& resources) override { sprite_hud_.bind(resources); }
    void hud_selection_result(std::uint8_t weapon,bool queued,double seconds) noexcept override {
        sprite_hud_.selection_result(weapon,queued,seconds);
    }
    std::span<const game_api::LocalSoundReference> hud_sound_preparation() const noexcept override {
        return HalfLifeSpriteHud::sounds();
    }
    game_api::HudMenuResult hud_menu_input(const client::RuntimeClientObservationState& state,
        const game_api::HudMenuInput& input) noexcept override {
        if(!presentation_.hand_grenade_can_holster())return sprite_hud_.block_selection(input);
        std::array<std::uint8_t,32U> eligible{};std::size_t count=0;
        for(const auto& type:state.weapon_hud.catalogue)
            if(count<eligible.size() && HalfLifeSpriteHud::has_ammo(state,type) && inventory_selection(state,type.id))
                eligible[count++]=type.id;
        std::sort(eligible.begin(),eligible.begin()+count,[&](auto left,auto right) {
            const auto* a=&*std::find_if(state.weapon_hud.catalogue.begin(),state.weapon_hud.catalogue.end(),[=](const auto& w){return w.id==left;});
            const auto* b=&*std::find_if(state.weapon_hud.catalogue.begin(),state.weapon_hud.catalogue.end(),[=](const auto& w){return w.id==right;});
            return std::tie(a->slot,a->position,a->id)<std::tie(b->slot,b->position,b->id);
        });
        return sprite_hud_.input(state,input,std::span{eligible.data(),count},hud_context_);
    }
    void set_hud_context(game_api::HudPresentationContext context) noexcept override { hud_context_ = context; }
    game_api::CameraIntent camera(const client::RuntimeClientObservationState& state,double punch) const noexcept override {
        return presentation_.camera(state,punch);
    }
    void observe_action_evidence(const client::RuntimeClientObservationState& state,
        const std::optional<game_api::GameActionTraffic>& traffic) noexcept override {
        evidence_.observe(state,traffic);
    }
    game_api::GameActionEvidenceSnapshot action_evidence() const noexcept override {
        return evidence_.snapshot();
    }
    std::optional<game_api::GameCommandRequest> inventory_selection(
        const client::RuntimeClientObservationState& state, std::uint8_t id) const override {
        if (!presentation_.hand_grenade_can_holster() || !rpg_can_select(state,id) ||
            !build_weapon_selection_request(state,id)) return {};
        const auto found = std::find_if(state.weapon_hud.catalogue.begin(),
            state.weapon_hud.catalogue.end(), [id](const auto& value){ return value.id == id; });
        return game_api::GameCommandRequest{game_api::GameCommandKind::inventory_selection,found->command_name};
    }
    std::optional<std::uint8_t> select_group(const client::RuntimeClientObservationState& state,
        std::uint8_t group, std::optional<std::uint8_t> current) const override {
        if (!presentation_.hand_grenade_can_holster()) return {};
        return select_owned_weapon_group(state,group,current);
    }
    std::optional<std::uint8_t> cycle_inventory(const client::RuntimeClientObservationState& state,
        std::optional<std::uint8_t> current,int direction) const override {
        if (!presentation_.hand_grenade_can_holster()) return {};
        return cycle_owned_weapon(state,current,direction);
    }
    game_api::GameCommandRequest self_kill_request() const override {
        return {game_api::GameCommandKind::self_kill,"kill"};
    }
    std::optional<std::uint8_t> scenario_inventory_target(
        const client::RuntimeClientObservationState& state,std::size_t phase) const override {
        const auto name = phase >= 3U ? "weapon_crowbar" : "weapon_9mmhandgun";
        const auto found = std::find_if(state.weapon_hud.catalogue.begin(), state.weapon_hud.catalogue.end(),
            [name](const auto& type) { return type.command_name == name; });
        return found == state.weapon_hud.catalogue.end() ? std::nullopt : std::optional{found->id};
    }
    game_api::GameScenarioDirective scenario_command(game_api::GameScenario mode,
        const client::RuntimeClientObservationState* state, std::size_t phase,
        double time,double phase_time) noexcept override {
        if (mode == game_api::GameScenario::damage_respawn) return respawn_script_.command(state,time);
        if (mode == game_api::GameScenario::weapon_fire_reload)
            return legacy_fire_reload_command(state,phase,phase_time);
        game_api::GameScenarioDirective directive;
        directive.buttons = weapon_script_.buttons(state,phase,time);
        directive.forward = phase == 0U || phase == 4U;
        directive.slow_walk = phase == 4U;
        return directive;
    }
    game_api::DamageRespawnScriptSnapshot damage_respawn_snapshot() const noexcept override {
        return respawn_script_.snapshot();
    }
private:
    bool known_absent_player(std::uint32_t entity) const noexcept {
        if(!entity || entity>state_.multiplayer.maximum_clients || entity>state_.multiplayer.players.size())return false;
        const auto& entry=state_.multiplayer.players[entity-1U];
        return entry.identity_observed && !entry.present;
    }
    std::optional<assets::AssetVector3> command_movement_effect(const game_api::LocalWeaponSubmittedCommand& command) noexcept override {
        if(egon_.selected()){egon_.command(command);return {};}
        return gauss_.command(command);
    }
    game_api::CommandMovementEffectPhase command_movement_effect_phase() const noexcept override {
        // The only nonzero HL command effect is Gauss weapon post-think recoil.
        return gauss_.selected() ? game_api::CommandMovementEffectPhase::after_movement
                                 : game_api::CommandMovementEffectPhase::before_movement;
    }
    game_api::BeamEffectsFrame beam_effects(const game_api::IStaticEffectTrace& query,double now,std::optional<assets::AssetVector3> muzzle) noexcept override {
        auto result=gauss_.frame(query,now,muzzle);
        egon_.append_frame(query,now,muzzle,result);return result;
    }
    GaussPresentation gauss_;
    game_api::TextMessageCatalogue text_messages_;
    ServerTextDisplay server_text_;
    game_api::ScreenEffectsFrame screen_effects(double now) const noexcept override {
        return screen_effects_.frame(now);
    }
    HalfLifeScreenEffects screen_effects_;
    EgonPresentation egon_;
    HalfLifeRemotePlayerPresentation remote_players_;
    std::array<std::optional<client::RuntimeObservationSource>,game_api::kMaximumRemotePlayers> remote_death_notices_{};
    void record_impact(const game_api::LocalWeaponActionIdentity& action,
        const std::optional<game_api::LocalWorldSurfaceHit>& surface,
        const MaterialLookup& lookup,const game_api::LocalSoundReference& sound,
        std::optional<game_api::LocalSoundReference> supplemental={}) noexcept {
        game_api::LocalWorldImpactDiagnostic out;
        out.action=action;
        out.source_surface_index=surface ? surface->source_surface_index : 0U;
        out.normalized_texture_key=lookup.key;
        const auto label=material_name(lookup.kind);
        const auto source_label=material_source_name(lookup.source);
        std::copy(label.begin(),label.end(),out.material_label.begin());
        std::copy(source_label.begin(),source_label.end(),out.classification_source.begin());
        out.selected_sound=sound;
        out.supplemental_sound=std::move(supplemental);
        last_impact_=out;
    }
    game_api::GameSessionIdentity identity_;
    game_api::GameRecordState state_;
    game_api::HudPresentationContext hud_context_;
    HalfLifeSpriteHud sprite_hud_;
    game_api::GameMovementPolicy movement_;
    bool mute_glock_fire_sound_{};
    HalfLifePresentation presentation_;
    HalfLifeMovementAudio movement_audio_;
    HalfLifeMaterials materials_;
    WeaponVisuals weapon_visuals_;
    bool shotgun_model_bound_{};
    bool python_model_bound_{};
    bool crossbow_model_bound_{};
    bool rpg_model_bound_{};
    RemoteWeaponEffects remote_effects_;
    TemporaryEffects temporary_effects_;
    std::optional<game_api::LocalWeaponActionIdentity> awaiting_impact_;
    std::optional<assets::AssetVector3> awaiting_crossbow_direction_;
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,7> awaiting_extra_impacts_{};
    std::optional<game_api::LocalWorldImpactDiagnostic> last_impact_;
    std::array<game_api::LocalSoundCue,32U> impact_cues_{};
    std::size_t impact_cue_count_{};
    std::array<game_api::LocalSoundCue,32U> shell_cues_{};
    std::size_t shell_cue_count_{};
    struct SeenShellContact {
        game_api::LocalWeaponActionIdentity action;
        std::uint32_t ordinal{};
    };
    std::array<std::optional<SeenShellContact>,96U> seen_shell_contacts_{};
    std::size_t next_seen_shell_contact_{};
    std::uint64_t visual_life_epoch_{};
    std::uint64_t audio_serial_{};
    HalfLifeActionEvidence evidence_;
    WeaponPresentationScript weapon_script_;
    DamageRespawnScript respawn_script_;
};
}
std::unique_ptr<game_api::IGameClientModule> make_half_life_client_module(bool mute_glock_fire_sound) {
    return std::make_unique<HalfLifeClientModule>(mute_glock_fire_sound);
}
}
