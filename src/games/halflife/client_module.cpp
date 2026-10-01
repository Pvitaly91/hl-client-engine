#include <hlclient/games/halflife/client_module.hpp>
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
#include <hlclient/games/halflife/remote_effects.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace hlclient::games::halflife {
namespace {
class HalfLifeClientModule final : public game_api::IGameClientModule {
public:
    explicit HalfLifeClientModule(bool mute_glock_fire_sound)
        : movement_(make_movement_policy()), mute_glock_fire_sound_(mute_glock_fire_sound) {}
    void reset(game_api::GameSessionIdentity identity) noexcept override {
        identity_ = identity;
        state_ = {};
        presentation_.reset();
        movement_audio_.reset();
        weapon_visuals_.reset();
        materials_.reset();
        last_impact_.reset();
        awaiting_impact_.reset();
        impact_cue_count_=0;
        shell_cue_count_=0;
        seen_shell_contacts_.fill(std::nullopt);
        next_seen_shell_contact_=0;
        visual_life_epoch_=0;
        evidence_.reset();
        remote_players_.reset();
        remote_effects_.reset(identity.network_generation,identity.receiving_entity);
        weapon_script_ = {};
        respawn_script_ = {};
    }
    void teardown() noexcept override { reset({}); }
    void configure_scripted_events(std::span<const game_api::ScriptedEventBinding> bindings) noexcept override {
        remote_effects_.configure(bindings);
    }
    void committed_scripted_event(const game_api::CommittedScriptedEvent& event) noexcept override {
        remote_effects_.commit(event);
    }
    game_api::RemoteWeaponEffectsBatch drain_remote_effects(double now) noexcept override {
        return remote_effects_.drain(now);
    }
    game_api::LocalAudioBatch drain_remote_audio() noexcept override { return remote_effects_.audio(); }
    void remote_world_impact(const game_api::LocalWeaponActionIdentity& action,
        assets::AssetVector3 point,double time,std::optional<game_api::LocalWorldSurfaceHit> surface) noexcept override {
        remote_effects_.impact(action,point,time,std::move(surface),materials_);
    }
    void remote_shell_contact(const game_api::LocalShellContact& contact) noexcept override {
        remote_effects_.contact(contact);
    }
    const game_api::GameMovementPolicy& movement_policy() const noexcept override { return movement_; }
    game_api::RemotePlayerPresentationPolicy remote_player_policy() const noexcept override {
        return HalfLifeRemotePlayerPresentation::policy();
    }
    game_api::RemotePlayerPresentationIntent remote_player(
        const game_api::RemotePlayerPresentationContext& context) noexcept override {
        if(context.network_generation!=identity_.network_generation ||
            context.map_generation!=identity_.map_generation) {
            game_api::RemotePlayerPresentationIntent result;
            result.status=game_api::RemotePlayerPresentationStatus::invalid_context;
            return result;
        }
        return remote_players_.sample(context);
    }
    game_api::GameRecordResult stage_record(const game_api::GameRecordInput& input) const override {
        return halflife::stage_record(input, input.previous ? &state_ : nullptr);
    }
    void commit_record(game_api::GameRecordState&& state) noexcept override {
        presentation_.commit_pickups(state);
        state_ = std::move(state);
    }
    void bind_model(std::optional<game_api::LocalWeaponModelMetadata> model) override {
        if (weapon_visuals_.bind(model)) {
            awaiting_impact_.reset();
            impact_cue_count_=0;
        }
        presentation_.bind_model(std::move(model));
    }
    void observe(const client::RuntimeClientObservationState& state, double time) override {
        if (state.lifecycle.dead() || (visual_life_epoch_ &&
            visual_life_epoch_!=state.lifecycle.life_epoch)) {
            movement_audio_.cancel_life();
            weapon_visuals_.cancel();
            awaiting_impact_.reset();
            impact_cue_count_=0;
        }
        visual_life_epoch_=state.lifecycle.life_epoch;
        presentation_.observe(state,time);
    }
    void submit(const game_api::LocalWeaponSubmittedCommand& command,double time) override {
        weapon_visuals_.remember_submission(command);
        presentation_.submit(command,time);
    }
    void cancel_uncommitted() noexcept override {
        presentation_.cancel_uncommitted();
        weapon_visuals_.cancel();
        awaiting_impact_.reset();
        impact_cue_count_=0;
    }
    game_api::LocalVisualFrame local_visuals(const game_api::LocalVisualContext& context) noexcept override {
        auto frame=weapon_visuals_.frame(context);
        if (frame.world_impact) awaiting_impact_=frame.world_impact->action;
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
        profile.supplemental_sounds=glock_ricochet_preload();
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
        if (!awaiting_impact_ || *awaiting_impact_!=action || !std::isfinite(time) ||
            !std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) return;
        awaiting_impact_.reset();
        if (impact_cue_count_==impact_cues_.size()) return;
        const auto lookup=materials_.lookup(surface ?
            std::string_view{surface->texture_name.data()} : std::string_view{});
        const auto chosen=material_impact_sound(lookup.kind,action,false);
        const auto ricochet=impact_cue_count_+1U<impact_cues_.size() ?
            glock_ricochet_sound(action) :
            std::optional<game_api::LocalSoundReference>{};
        record_impact(action,surface,lookup,chosen.reference,ricochet);
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
        impact_cues_[impact_cue_count_++]=cue;
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
            contact.action.kind!=game_api::LocalWeaponAction::primary_fire ||
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
        movement_audio_.append_to(batch);
        if (impact_cue_count_>0) for (std::size_t i=0;i<impact_cue_count_ &&
            batch.prepare_count<batch.prepare.size();++i)
            batch.prepare[batch.prepare_count++]=impact_cues_[i].reference;
        if (shell_cue_count_>0 && batch.prepare_count<batch.prepare.size())
            batch.prepare[batch.prepare_count++]=local_impact_assets()->shell_contact_sound;
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
        auto result=presentation_.sample(time);
        weapon_visuals_.observe(result,time);
        return result;
    }
    game_api::ViewmodelIntent viewmodel(const client::RuntimeClientObservationState& state,
        std::optional<game_api::LocalWeaponModelMetadata> model, double time,
        std::optional<game_api::LocalWeaponVisual> visual) override {
        return presentation_.viewmodel(state,model,time,visual);
    }
    game_api::HudState hud(const client::RuntimeClientObservationState& state,double time) override {
        return presentation_.hud(state,time);
    }
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
        if (!build_weapon_selection_request(state,id)) return {};
        const auto found = std::find_if(state.weapon_hud.catalogue.begin(),
            state.weapon_hud.catalogue.end(), [id](const auto& value){ return value.id == id; });
        return game_api::GameCommandRequest{game_api::GameCommandKind::inventory_selection,found->command_name};
    }
    std::optional<std::uint8_t> select_group(const client::RuntimeClientObservationState& state,
        std::uint8_t group, std::optional<std::uint8_t> current) const override {
        return select_owned_weapon_group(state,group,current);
    }
    std::optional<std::uint8_t> cycle_inventory(const client::RuntimeClientObservationState& state,
        std::optional<std::uint8_t> current,int direction) const override {
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
    HalfLifeRemotePlayerPresentation remote_players_;
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
    game_api::GameMovementPolicy movement_;
    bool mute_glock_fire_sound_{};
    HalfLifePresentation presentation_;
    HalfLifeMovementAudio movement_audio_;
    HalfLifeMaterials materials_;
    WeaponVisuals weapon_visuals_;
    RemoteWeaponEffects remote_effects_;
    std::optional<game_api::LocalWeaponActionIdentity> awaiting_impact_;
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
