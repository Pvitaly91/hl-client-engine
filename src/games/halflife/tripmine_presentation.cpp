#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::games::halflife {
void LocalWeaponPresentationController::clear_tripmine_state() noexcept {
    tripmine_={};tripmine_attempt_.reset();tripmine_receipts_.fill(std::nullopt);
    tripmine_events_.fill(std::nullopt);tripmine_receipt_next_=tripmine_event_next_=0;
    tripmine_ammo_source_.reset();tripmine_ready_at_=0;tripmine_release_required_=false;
    tripmine_view_offset_known_=false;
}
void LocalWeaponPresentationController::submit_tripmine(const LocalWeaponSubmittedCommand& c,bool ready) {
    const bool attack=(c.buttons&goldsrc::kReferenceGoldSrcButtonAttack)!=0;
    if(tripmine_release_required_) {if(!attack)tripmine_release_required_=false;return;}
    if(!attack || !ready || tripmine_.retired || c.sample_end_seconds+1e-8<tripmine_ready_at_ ||
        clip_!=-1 || !reserve_ || !*reserve_ || *reserve_>tripmine_maximum_ammo ||
        *reserve_<=tripmine_.reserved_ammo || !tripmine_event_index_ || !c.shot_context) return;
    tripmine_ready_at_=c.sample_end_seconds+tripmine_attempt_cycle;
    ++tripmine_.attempts;
    tripmine_attempt_=TripmineAttempt{c}; // no ammo or world-entity mutation
}
void LocalWeaponPresentationController::prepare_presentation(const IStaticEffectTrace& query,
    double now,std::optional<std::uint32_t> receiving) noexcept {
    if(snark_selected_) {while(snark_attempt_count_)prepare_snark(query,now,receiving);return;}
    if(!tripmine_selected_ || !tripmine_attempt_) return;
    const auto pending=*tripmine_attempt_;tripmine_attempt_.reset();
    const auto& c=pending.command;
    if(!supported() || canonical_dead_ || !std::isfinite(now) || now<c.sample_end_seconds ||
        now-c.sample_end_seconds>.25 || !c.shot_context || c.generation!=generation_) {
        ++tripmine_.unavailable;return;
    }
    const auto& pose=*c.shot_context;const auto length=std::hypot(pose.direction.x,pose.direction.y,pose.direction.z);
    if(!std::isfinite(length) || length<.001 || !std::isfinite(pose.eye.x) ||
        !std::isfinite(pose.eye.y) || !std::isfinite(pose.eye.z)) {++tripmine_.unavailable;return;}
    const auto scale=static_cast<float>(tripmine_trace_distance/length);
    PlacementTraceRequest request{pose.eye,{pose.eye.x+pose.direction.x*scale,
        pose.eye.y+pose.direction.y*scale,pose.eye.z+pose.direction.z*scale},generation_,receiving,2U,3U};
    tripmine_.last_trace=request;const auto result=query.placement_trace(request);tripmine_.last_result=result;
    if(!result.valid || !std::isfinite(result.fraction) || result.fraction<0 || result.fraction>1) {
        ++tripmine_.unavailable;return;
    }
    // Pinned event checks fraction<1, not FL_CONVEYOR. Startsolid is conservatively
    // suppressed by this presentation profile; the server still decides placement.
    if(result.start_solid || result.all_solid || result.fraction>=1 || !result.hit_entity) {
        ++tripmine_.no_hit;return;
    }
    start(LocalWeaponAction::deploy_charge,c.sequence,c.sample_end_seconds,
        tripmine_draw_sequence,0U,now+1.0,false,false);
    if(!action_) return;
    if(pending.event_only) {action_->identity.sub_identity=pending.event_ordinal+1U;
        action_->visual.source=LocalWeaponAnimationSource::service_event;}
    ++tripmine_.eligible;
    const bool reserve=!pending.event_only && reserve_ && *reserve_>tripmine_.reserved_ammo;
    auto& receipt=tripmine_receipts_[tripmine_receipt_next_++%tripmine_receipts_.size()];
    if(receipt && receipt->reserved && tripmine_.reserved_ammo)--tripmine_.reserved_ammo;
    receipt=TripmineReceipt{action_->identity,action_->pre_record_ordinal,now+1.0,reserve,false};
    if(reserve)++tripmine_.reserved_ammo;
}
void LocalWeaponPresentationController::observe_tripmine(const client::RuntimeClientObservationState& state,
    std::optional<std::uint8_t> before,bool fresh,double now) {
    if(!reserve_ || *reserve_>tripmine_maximum_ammo) {tripmine_attempt_.reset();return;}
    if(fresh && before && *reserve_<*before && tripmine_ammo_source_) {
        unsigned consumed=*before-*reserve_;
        while(consumed--) {
            TripmineReceipt* oldest=nullptr;
            for(auto& r:tripmine_receipts_) if(r && r->reserved &&
                tripmine_ammo_source_->record_ordinal>r->pre_record &&
                (!oldest || r->identity.started_at_seconds<oldest->identity.started_at_seconds)) oldest=&*r;
            if(!oldest)break;
            oldest->reserved=false;oldest->confirmed=true;
            if(tripmine_.reserved_ammo)--tripmine_.reserved_ammo;
            ++tripmine_.confirmed;
            if(action_ && action_->identity==oldest->identity) finish_pending(LocalWeaponActionStatus::server_confirmed);
        }
        if(*reserve_==0) {tripmine_.retired=true;tripmine_attempt_.reset();}
    }
    if(fresh && *reserve_==0) {tripmine_.retired=true;tripmine_attempt_.reset();}
    if(fresh && *reserve_>0 && tripmine_.retired) {tripmine_.retired=false;publish_idle(now);}
    expire_tripmine_receipts(now);
    if(state.receiving_client && state.receiving_client->view_offset.complete()) {
        const auto& p=state.receiving_client->view_offset;
        tripmine_view_offset_={static_cast<float>(*p.x),static_cast<float>(*p.y),static_cast<float>(*p.z)};
        tripmine_view_offset_known_=std::isfinite(*p.x)&&std::isfinite(*p.y)&&std::isfinite(*p.z);
    }
}
void LocalWeaponPresentationController::expire_tripmine_receipts(double now) noexcept {
    for(auto& r:tripmine_receipts_) if(r && r->reserved && now>=r->expires) {
        r->reserved=false;if(tripmine_.reserved_ammo)--tripmine_.reserved_ammo;
        ++tripmine_.rejected;
        if(action_ && action_->identity==r->identity) finish_pending(LocalWeaponActionStatus::timed_out);
    }
}
void LocalWeaponPresentationController::committed_tripmine_event(const CommittedScriptedEvent& e,
    std::optional<std::uint32_t> receiving) noexcept {
    if(!tripmine_selected_ || !supported() || !receiving || e.emitter_entity!=*receiving ||
        e.generation!=generation_ || !e.record || !e.record_ordinal || e.record_ordinal<=model_bound_record_ordinal_ ||
        e.resolution!=ScriptedEventResolution::ready || e.fire_delay_ticks || life_release_barrier_ ||
        (e.weapon_model_index && *e.weapon_model_index!=model_->model_index)) return;
    for(const auto& old:tripmine_events_) if(old && old->record==e.record && old->cursor==e.message_bit_offset &&
        old->ordinal==e.entry_ordinal) {
        const auto& prior=old->event;
        if(prior.emitter_entity!=e.emitter_entity || prior.event_index!=e.event_index ||
            prior.origin.x!=e.origin.x || prior.origin.y!=e.origin.y || prior.origin.z!=e.origin.z ||
            prior.angles.x!=e.angles.x || prior.angles.y!=e.angles.y || prior.angles.z!=e.angles.z ||
            prior.weapon_model_index!=e.weapon_model_index || prior.angles_from_entity!=e.angles_from_entity) {
            ++counters_.conflicting_duplicates;counters_.error=LocalWeaponPresentationError::conflicting_event;
        } else ++tripmine_.duplicates;
        return;
    }
    tripmine_events_[tripmine_event_next_++%tripmine_events_.size()]=TripmineEvent{e.record,e.message_bit_offset,e.entry_ordinal,e};
    if(tripmine_attempt_)return;
    for(const auto& r:tripmine_receipts_) if(r && e.record_ordinal>r->pre_record &&
        e.received_at_seconds>=r->identity.started_at_seconds &&
        e.received_at_seconds-r->identity.started_at_seconds<=1.25) return;
    if(!tripmine_view_offset_known_ || !std::isfinite(e.received_at_seconds) ||
        e.received_at_seconds<last_time_ || !reserve_ || !*reserve_ || *reserve_>tripmine_maximum_ammo) return;
    constexpr double radians=3.14159265358979323846/180;
    const auto pitch=(e.angles_from_entity ? -3.0*std::remainder(double(e.angles.x),360.0) : e.angles.x)*radians;
    const auto yaw=e.angles.y*radians;
    LocalWeaponSubmittedCommand c{generation_,0U,0U,e.received_at_seconds};c.shot_context.emplace();
    c.shot_context->eye={e.origin.x+tripmine_view_offset_.x,e.origin.y+tripmine_view_offset_.y,e.origin.z+tripmine_view_offset_.z};
    c.shot_context->direction={static_cast<float>(std::cos(pitch)*std::cos(yaw)),
        static_cast<float>(std::cos(pitch)*std::sin(yaw)),static_cast<float>(-std::sin(pitch))};
    ++tripmine_.attempts;tripmine_attempt_=TripmineAttempt{c,true,e.entry_ordinal};
}
}
