#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::games::halflife {
void LocalWeaponPresentationController::clear_snark_state() noexcept {
    snark_={};snark_attempt_count_=0;snark_attempts_={};snark_receipts_.fill(std::nullopt);snark_events_.fill(std::nullopt);
    snark_receipt_next_=snark_event_next_=0;snark_ammo_source_.reset();snark_recovery_at_.reset();
    snark_ready_at_=0;snark_release_required_=false;
    snark_model_rebind_pending_=false;
}
void LocalWeaponPresentationController::enqueue_snark(const LocalWeaponSubmittedCommand& c,
    bool event_only,std::uint32_t ordinal) noexcept {
    if(snark_attempt_count_==snark_attempts_.size()) {++snark_.unavailable;return;}
    // A single event-first callback can be followed by its immutable command
    // before this frame's borrowed query. Upgrade that occurrence to command
    // provenance; do not collapse independent queued event-only throws.
    if(!event_only && snark_attempt_count_==1 && snark_attempts_[0].event_only &&
        std::abs(snark_attempts_[0].command.sample_end_seconds-c.sample_end_seconds)<=.25) {
        snark_attempts_[0]={c,false,0};return;
    }
    snark_attempts_[snark_attempt_count_++]={c,event_only,ordinal};
}
void LocalWeaponPresentationController::submit_snark(const LocalWeaponSubmittedCommand& c,bool ready) {
    const bool attack=(c.buttons&goldsrc::kReferenceGoldSrcButtonAttack)!=0;
    const bool secondary=(c.buttons&goldsrc::kReferenceGoldSrcButtonAttack2)!=0;
    expire_snark_receipts(c.sample_end_seconds);
    if(snark_release_required_) {if(!attack && !secondary)snark_release_required_=false;return;}
    // ItemPostFrame idles only after both attack buttons are released. This is
    // a weapon-presentation recovery, not a client-owned monster lifetime.
    if(!attack && !secondary && snark_recovery_at_ && c.sample_end_seconds>=*snark_recovery_at_) {
        snark_recovery_at_.reset();action_.reset();server_visual_.reset();
        if(reserve_ && *reserve_==0) {snark_.retired=true;return;}
        if(reserve_ && *reserve_<=snark_maximum_ammo) server_visual_=LocalWeaponVisual{
            snark_up_sequence,0,++restart_identity_,c.sample_end_seconds,
            LocalWeaponAnimationSource::provisional_command,LocalWeaponActionStatus::retained_server_event};
    }
    if(!attack || secondary || !ready || snark_.retired || c.sample_end_seconds+1e-8<snark_ready_at_ ||
        clip_!=-1 || !reserve_ || *reserve_>snark_maximum_ammo || *reserve_<=snark_.reserved_ammo ||
        !snark_event_index_ || !c.shot_context) return;
    snark_ready_at_=c.sample_end_seconds+snark_throw_cycle;
    ++snark_.attempts;enqueue_snark(c,false,0);
    // The pinned shared client weapon trace is a no-hit stub: JustThrown/idle
    // recover even when the independent real EV trace suppresses only THROW.
    snark_recovery_at_=c.sample_end_seconds+snark_recovery_seconds;
}
void LocalWeaponPresentationController::prepare_snark(const IStaticEffectTrace& query,double now,
    std::optional<std::uint32_t> receiving) noexcept {
    if(!snark_attempt_count_)return;
    const auto pending=snark_attempts_[0];
    for(std::size_t i=1;i<snark_attempt_count_;++i)snark_attempts_[i-1]=std::move(snark_attempts_[i]);
    snark_attempts_[--snark_attempt_count_]={};const auto& c=pending.command;
    if(!supported() || canonical_dead_ || !std::isfinite(now) || now<c.sample_end_seconds ||
        now-c.sample_end_seconds>.25 || c.generation!=generation_ || !c.shot_context) {
        ++snark_.unavailable;return;
    }
    // Receipt precedes feasibility: a blocked event echo is still the same
    // attempt and must never resurrect a THROW at a later, moved camera pose.
    auto& receipt=snark_receipts_[snark_receipt_next_++%snark_receipts_.size()];
    if(receipt && receipt->reserved && snark_.reserved_ammo)--snark_.reserved_ammo;
    const auto pre=client_source_ ? client_source_->record_ordinal : 0;
    receipt=SnarkReceipt{{generation_,snark_weapon_id,model_->model_index,model_->resource_revision,
        c.sequence,LocalWeaponAction::throw_release,c.sample_end_seconds,canonical_revision_,0,
        pending.event_only ? pending.event_ordinal+1U : 0U},pre,now+1.25,false,false};
    const auto& pose=*c.shot_context;
    if(!pose.player_origin || !pose.player_ducked) {++snark_.unavailable;return;}
    const auto length=std::hypot(pose.direction.x,pose.direction.y,pose.direction.z);
    const auto& origin=*pose.player_origin;
    if(!std::isfinite(length) || length<.001 || !std::ranges::all_of(origin,[](float v){return std::isfinite(v);})) {
        ++snark_.unavailable;return;
    }
    const assets::AssetVector3 corrected{origin[0],origin[1],origin[2]+(*pose.player_ducked ? 18.F : 0.F)};
    const assets::AssetVector3 forward{static_cast<float>(pose.direction.x/length),
        static_cast<float>(pose.direction.y/length),static_cast<float>(pose.direction.z/length)};
    const auto offset=[&](float distance){return assets::AssetVector3{corrected.x+forward.x*distance,
        corrected.y+forward.y*distance,corrected.z+forward.z*distance};};
    // Existing neutral placement query is a point sweep (GoldSrc hull2), with
    // self excluded and committed players/BBOX blockers. No gameplay result.
    PlacementTraceRequest request{offset(20),offset(64),generation_,receiving,2U,3U};
    snark_.last_trace=request;const auto result=query.placement_trace(request);snark_.last_result=result;
    if(!result.valid || !std::isfinite(result.fraction) || result.fraction<0 || result.fraction>1) {
        ++snark_.unavailable;return;
    }
    if(result.start_solid || result.all_solid || result.fraction<=.25) {++snark_.blocked;return;}
    start(LocalWeaponAction::throw_release,c.sequence,c.sample_end_seconds,snark_throw_sequence,0,
        now+1.25,false,false);
    if(!action_)return;
    if(pending.event_only) {action_->identity.sub_identity=pending.event_ordinal+1;
        action_->visual.source=LocalWeaponAnimationSource::service_event;}
    receipt->identity=action_->identity;receipt->pre_record=action_->pre_record_ordinal;
    receipt->reserved=!pending.event_only;
    if(receipt->reserved)++snark_.reserved_ammo;
    ++snark_.eligible;snark_recovery_at_=c.sample_end_seconds+snark_recovery_seconds;
}
void LocalWeaponPresentationController::observe_snark(const client::RuntimeClientObservationState& state,
    std::optional<std::uint8_t> before,bool fresh,std::optional<std::uint32_t> changed_animation,double now) {
    if(!reserve_ || *reserve_>snark_maximum_ammo) {snark_attempt_count_=0;return;}
    if(fresh && before && *reserve_<*before && snark_ammo_source_) {
        unsigned consumed=*before-*reserve_;
        while(consumed--) {
            SnarkReceipt* oldest=nullptr;
            for(auto& r:snark_receipts_) if(r && !r->confirmed && r->identity.command_sequence &&
                now<r->expires && snark_ammo_source_->record_ordinal>r->pre_record &&
                (!oldest || r->identity.started_at_seconds<oldest->identity.started_at_seconds))oldest=&*r;
            if(!oldest)break;
            oldest->confirmed=true;if(oldest->reserved && snark_.reserved_ammo)--snark_.reserved_ammo;
            oldest->reserved=false;++snark_.confirmed;
            if(!snark_recovery_at_)snark_recovery_at_=oldest->identity.started_at_seconds+snark_recovery_seconds;
            if(action_ && action_->identity==oldest->identity)finish_pending(LocalWeaponActionStatus::server_confirmed);
        }
    }
    if(fresh && *reserve_>0 && snark_.retired) {snark_.retired=false;publish_idle(now);}
    if(fresh && *reserve_==0 && !snark_recovery_at_)snark_.retired=true;
    expire_snark_receipts(now);
    const auto correction=[&](std::uint32_t sequence,std::uint8_t body,LocalWeaponAnimationSource source) {
        if(sequence==snark_throw_sequence || !snark_recovery_at_)return;
        // A fresh recovery/idle/holster is already the server occurrence. Do
        // not restart it from a later lower-precedence released command.
        if(action_) {
            server_visual_=action_->visual;action_.reset();
            server_visual_->status=LocalWeaponActionStatus::retained_server_event;
            // This is authoritative pose correction, not proof of accepted
            // throw/ammo consumption. Only AmmoX settles the action receipt.
            ++counters_.actions_corrected;
        } else server_visual_=LocalWeaponVisual{sequence,body,++restart_identity_,now,
            source,LocalWeaponActionStatus::retained_server_event};
        snark_recovery_at_.reset();
        if(reserve_ && *reserve_==0)snark_.retired=true;
    };
    if(changed_animation && *changed_animation!=snark_throw_sequence && valid_pose(*changed_animation,0)) {
        if(action_) {
            action_->visual={*changed_animation,0,++restart_identity_,now,
                LocalWeaponAnimationSource::clientdata_state,LocalWeaponActionStatus::retained_server_event};
        }
        correction(*changed_animation,0,LocalWeaponAnimationSource::clientdata_state);
    }
    // Exact svc_weaponanim has authority over a derived pose, never over ammo
    // or world entity creation. Repeated retained source cannot restart it.
    const auto& hud=state.weapon_hud;
    if(hud.animation_source && hud.animation_sequence && hud.animation_body &&
        hud.animation_source->record_ordinal>=model_bound_record_ordinal_) {
        const SeenEvent event{*hud.animation_source,*hud.animation_sequence,*hud.animation_body};
        if(std::none_of(event_history_.begin(),event_history_.end(),[&](const auto& e){return e && e->source==event.source;})) {
            event_history_[event_history_next_++%event_history_.size()]=event;
            if(!valid_pose(event.sequence,event.body)) {counters_.error=LocalWeaponPresentationError::unsupported_pose;return;}
            if(action_ && event.source.record_ordinal>action_->pre_record_ordinal) {
                const bool same=action_->visual.sequence==event.sequence && action_->visual.body==event.body;
                if(!same) {action_->visual.sequence=event.sequence;action_->visual.body=event.body;
                    action_->visual.restart_identity=++restart_identity_;action_->visual.started_at_seconds=now;}
                action_->visual.source=LocalWeaponAnimationSource::service_event;
            } else if(!action_) server_visual_=LocalWeaponVisual{event.sequence,event.body,++restart_identity_,now,
                LocalWeaponAnimationSource::service_event,LocalWeaponActionStatus::retained_server_event};
            correction(event.sequence,event.body,LocalWeaponAnimationSource::service_event);
        }
    }
}
void LocalWeaponPresentationController::expire_snark_receipts(double now) noexcept {
    for(auto& r:snark_receipts_) if(r && r->reserved && now>=r->expires) {
        r->reserved=false;if(snark_.reserved_ammo)--snark_.reserved_ammo;++snark_.rejected;
        if(action_ && action_->identity==r->identity)finish_pending(LocalWeaponActionStatus::timed_out);
    }
}
void LocalWeaponPresentationController::committed_snark_event(const CommittedScriptedEvent& e,
    std::optional<std::uint32_t> receiving) noexcept {
    if(!snark_selected_ || snark_model_rebind_pending_ || !supported() || !receiving || e.emitter_entity!=*receiving ||
        e.generation!=generation_ || !e.record || !e.record_ordinal || e.record_ordinal<=model_bound_record_ordinal_ ||
        e.resolution!=ScriptedEventResolution::ready || e.fire_delay_ticks || life_release_barrier_ || snark_release_required_ ||
        (e.weapon_model_index && *e.weapon_model_index!=model_->model_index))return;
    for(const auto& old:snark_events_) if(old && old->event.record==e.record &&
        old->event.message_bit_offset==e.message_bit_offset && old->event.entry_ordinal==e.entry_ordinal) {
        const auto& p=old->event;
        if(p.emitter_entity!=e.emitter_entity || p.event_index!=e.event_index || p.origin.x!=e.origin.x ||
            p.origin.y!=e.origin.y || p.origin.z!=e.origin.z || p.angles.x!=e.angles.x || p.angles.y!=e.angles.y ||
            p.angles.z!=e.angles.z || p.ducking!=e.ducking || p.angles_from_entity!=e.angles_from_entity ||
            p.weapon_model_index!=e.weapon_model_index) {
            ++counters_.conflicting_duplicates;counters_.error=LocalWeaponPresentationError::conflicting_event;
        } else ++snark_.duplicates;
        return;
    }
    snark_events_[snark_event_next_++%snark_events_.size()]=SnarkEvent{e};
    for(std::size_t i=0;i<snark_attempt_count_;++i) {
        const auto& pending=snark_attempts_[i];
        if(!pending.event_only && e.received_at_seconds>=pending.command.sample_end_seconds &&
            e.received_at_seconds-pending.command.sample_end_seconds<=.25)return;
    }
    for(const auto& r:snark_receipts_) if(r && r->identity.command_sequence && e.record_ordinal>r->pre_record &&
        e.received_at_seconds>=r->identity.started_at_seconds && e.received_at_seconds-r->identity.started_at_seconds<=1.25)return;
    if(!std::isfinite(e.received_at_seconds) || e.received_at_seconds<last_time_ ||
        (reserve_ && *reserve_>snark_maximum_ammo))return;
    constexpr double radians=3.14159265358979323846/180;
    const auto pitch=(e.angles_from_entity ? -3.0*std::remainder(double(e.angles.x),360.0) : e.angles.x)*radians;
    const auto yaw=e.angles.y*radians;
    LocalWeaponSubmittedCommand c{generation_,0,0,e.received_at_seconds};c.shot_context.emplace();
    c.shot_context->player_origin=std::array{e.origin.x,e.origin.y,e.origin.z};c.shot_context->player_ducked=e.ducking;
    c.shot_context->direction={static_cast<float>(std::cos(pitch)*std::cos(yaw)),
        static_cast<float>(std::cos(pitch)*std::sin(yaw)),static_cast<float>(-std::sin(pitch))};
    ++snark_.attempts;enqueue_snark(c,true,e.entry_ordinal);
}
}
