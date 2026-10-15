#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace hlclient::games::halflife {
void LocalWeaponPresentationController::clear_satchel_state() noexcept {
    satchel_={};satchel_receipts_.fill(std::nullopt);satchel_ammo_source_.reset();
    satchel_idle_ready_at_.reset();satchel_primary_ready_at_=satchel_secondary_ready_at_=satchel_command_time_=0;
    satchel_release_required_=false;
}
void LocalWeaponPresentationController::bind_model_variants(std::span<const LocalWeaponModelMetadata> models) {
    std::array<std::optional<LocalWeaponModelMetadata>,2> next;
    for(const auto& model:models) if(const auto role=satchel_model_role(model.resource_name);role && satchel_sequences(model))
        next[*role ? 1U : 0U]=model;
    if(next==satchel_models_) return;
    bool boundary=false;
    for(std::size_t i=0;i<next.size();++i) if(satchel_models_[i] && next[i] &&
        (satchel_models_[i]->generation!=next[i]->generation ||
         satchel_models_[i]->resource_revision!=next[i]->resource_revision)) boundary=true;
    if(boundary) {
        finish_pending(LocalWeaponActionStatus::server_rejected);action_.reset();server_visual_.reset();
        clear_satchel_state();satchel_release_required_=true;
    }
    satchel_models_=std::move(next);
}
bool LocalWeaponPresentationController::select_satchel_model(bool radio) {
    const auto& selected=satchel_models_[radio ? 1U : 0U];
    if(!selected || selected->generation!=generation_ || !satchel_sequences(*selected)) {
        counters_.error=LocalWeaponPresentationError::unsupported_model;return false;
    }
    if(model_!=selected) {
        model_=selected;audio_.bind(model_);server_visual_.reset();idle_sequence_=idle_body_=0;
    }
    return true;
}
void LocalWeaponPresentationController::submit_satchel(const LocalWeaponSubmittedCommand& command,
    bool ready,bool secondary_ready) {
    const auto at=command.sample_end_seconds;
    if(at<satchel_command_time_) {counters_.error=LocalWeaponPresentationError::invalid_time;return;}
    satchel_command_time_=at;
    const bool primary=(command.buttons&goldsrc::kReferenceGoldSrcButtonAttack)!=0;
    const bool secondary=(command.buttons&goldsrc::kReferenceGoldSrcButtonAttack2)!=0;
    if(satchel_release_required_) {
        if(!primary && !secondary) satchel_release_required_=false;
        return;
    }
    if(!satchel_.charge_ready || clip_!=-1 || !reserve_ || *reserve_>satchel_maximum_ammo || canonical_dead_) return;
    if(*satchel_.charge_ready==2 && !primary && !secondary && satchel_idle_ready_at_ &&
        at+1e-8>=*satchel_idle_ready_at_) {
        // WeaponIdle presentation only: never detonate/remove/count entities.
        finish_pending(LocalWeaponActionStatus::server_confirmed);action_.reset();
        satchel_.charge_ready=0;satchel_idle_ready_at_.reset();
        if(*reserve_<=satchel_.reserved_ammo) {
            satchel_.retired=true;server_visual_.reset();return;
        }
        if(!select_satchel_model(false)) return;
        server_visual_=LocalWeaponVisual{2U,0U,++restart_identity_,at,
            LocalWeaponAnimationSource::provisional_command,LocalWeaponActionStatus::predicted_pending};
        satchel_primary_ready_at_=satchel_secondary_ready_at_=at+.5;
    }
    if(satchel_.retired || *satchel_.charge_ready==2 || (in_reload_ && *in_reload_)) return;
    if(secondary) {
        if(*satchel_.charge_ready!=1 || !secondary_ready || at+1e-8<satchel_secondary_ready_at_ ||
            !select_satchel_model(true)) return;
        start(LocalWeaponAction::remote_detonate,command.sequence,at,3U,0U,
            std::numeric_limits<double>::max(),false,false);
        if(!action_) return;
        action_->identity.model_index=model_->model_index;
        satchel_.charge_ready=2;satchel_.detonation_time=at;++satchel_.detonations;
        satchel_idle_ready_at_=at+.5;
        satchel_primary_ready_at_=satchel_secondary_ready_at_=at+.5;
        return;
    }
    if(!primary || !ready || at+1e-8<satchel_primary_ready_at_ ||
        *reserve_<=satchel_.reserved_ammo || !select_satchel_model(true)) return;
    const auto receipt=std::find_if(satchel_receipts_.begin(),satchel_receipts_.end(),
        [](const auto& value){return !value || !value->reserved;});
    if(receipt==satchel_receipts_.end()) return;
    // Both first and additional throws use RADIO_DRAW. DROP is a holster pose.
    start(LocalWeaponAction::deploy_charge,command.sequence,at,2U,0U,
        std::numeric_limits<double>::max(),false,false);
    if(!action_) return;
    action_->identity.model_index=model_->model_index;
    *receipt=SatchelReceipt{action_->identity,action_->pre_record_ordinal,true};
    ++satchel_.reserved_ammo;++satchel_.throws;satchel_.charge_ready=1;
    satchel_.last_throw_context=command.shot_context;
    satchel_primary_ready_at_=at+1;satchel_secondary_ready_at_=at+.5;
}
void LocalWeaponPresentationController::observe_satchel(const client::RuntimeClientObservationState& state,
    bool fresh_client,std::optional<std::uint8_t> previous_reserve,bool fresh_ammo,
    std::optional<std::uint32_t> changed_animation,double now) {
    const auto release_receipt=[&](const LocalWeaponActionIdentity& identity) {
        for(auto& receipt:satchel_receipts_) if(receipt && receipt->reserved && receipt->identity==identity) {
            receipt->reserved=false;if(satchel_.reserved_ammo) --satchel_.reserved_ammo;
            return;
        }
    };
    bool coalesced_settled=false;
    if(fresh_ammo && previous_reserve && reserve_ && *reserve_<*previous_reserve) {
        auto consumed=static_cast<unsigned>(*previous_reserve-*reserve_);
        while(consumed--) {
            SatchelReceipt* oldest=nullptr;
            for(auto& receipt:satchel_receipts_) if(receipt && receipt->reserved &&
                (!oldest || receipt->identity.command_sequence<oldest->identity.command_sequence)) oldest=&*receipt;
            if(oldest) release_receipt(oldest->identity);
        }
    }
    const auto slot=std::find_if(state.weapon_slots.begin(),state.weapon_slots.end(),
        [](const auto& item){return item.weapon_id==14U;});
    if(fresh_client) {
        if(slot==state.weapon_slots.end() || !slot->auxiliary_integer1) {
            satchel_.server_charge_ready.reset();satchel_.charge_ready.reset();
            counters_.error=LocalWeaponPresentationError::unsupported_pose;return;
        }
        const auto server=*slot->auxiliary_integer1;
        if(server<0 || server>2) {
            satchel_.server_charge_ready.reset();satchel_.charge_ready.reset();
            counters_.error=LocalWeaponPresentationError::unsupported_pose;return;
        }
        const bool initial=!satchel_.server_charge_ready;
        const bool changed=satchel_.server_charge_ready!=server;
        satchel_.server_charge_ready=server;
        const bool authoritative=initial || changed || slot->auxiliary_integer1_updated_in_record;
        if(authoritative && server==1 && fresh_ammo && previous_reserve && reserve_ &&
            *reserve_>=*previous_reserve && state.receiving_client && state.receiving_client->weapon_animation==2U && client_source_) {
            // A consume plus pickup can retain the same absolute AmmoX count.
            // Fresh mode/draw/count evidence settles one oldest occurrence,
            // without requiring a skip-local svc animation or double settlement.
            SatchelReceipt* oldest=nullptr;
            for(auto& receipt:satchel_receipts_) if(receipt && receipt->reserved &&
                client_source_->record_ordinal>receipt->pre_record &&
                (!oldest || receipt->identity.command_sequence<oldest->identity.command_sequence)) oldest=&*receipt;
            if(oldest) {release_receipt(oldest->identity);coalesced_settled=true;}
        }
        if(authoritative) {
            if(server==0 && client_source_) for(auto& receipt:satchel_receipts_)
                if(receipt && receipt->reserved && client_source_->record_ordinal>receipt->pre_record)
                    release_receipt(receipt->identity);
            const bool different=satchel_.charge_ready && *satchel_.charge_ready!=server;
            if(different) {
                const bool completing=server==0 && satchel_.charge_ready==2;
                if(action_) {
                    if(!completing && action_->identity.kind==LocalWeaponAction::deploy_charge)
                        release_receipt(action_->identity);
                    finish_pending(completing ? LocalWeaponActionStatus::server_confirmed : LocalWeaponActionStatus::server_rejected);
                }
                action_.reset();server_visual_.reset();++satchel_.corrections;
            } else if(action_) {
                finish_pending(LocalWeaponActionStatus::server_confirmed);
                action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
            }
            satchel_.charge_ready=server;
            satchel_.retired=server==0 && reserve_ && *reserve_==0;
            if(server==2) {
                if(satchel_.detonation_time) satchel_idle_ready_at_=*satchel_.detonation_time+.5;
                else if(slot->time_weapon_idle && std::isfinite(*slot->time_weapon_idle))
                    satchel_idle_ready_at_=now+std::max(0.0,*slot->time_weapon_idle);
                else satchel_idle_ready_at_.reset();
            } else satchel_idle_ready_at_.reset();
            if(!satchel_.retired && select_satchel_model(server!=0)) {
                if(different || !server_visual_) {
                    auto sequence=state.receiving_client ? state.receiving_client->weapon_animation : std::nullopt;
                    // An old same-number pose belonged to the opposite model.
                    const bool same_model=state.receiving_client && state.receiving_client->viewmodel_index==model_->model_index;
                    if(!same_model || !sequence || !valid_pose(*sequence,0U)) sequence=2U;
                    server_visual_=LocalWeaponVisual{*sequence,0U,++restart_identity_,now,
                        LocalWeaponAnimationSource::confirmed_weapon_state,LocalWeaponActionStatus::server_corrected};
                }
            }
        }
    }
    if(fresh_ammo && reserve_ && *reserve_>0 && satchel_.retired && satchel_.server_charge_ready==0) {
        satchel_.retired=false;satchel_.charge_ready=0;
        if(select_satchel_model(false)) publish_idle(now);
    }
    if(changed_animation && client_source_ && valid_pose(*changed_animation,0U) && !satchel_.retired) {
        if(action_ && client_source_->record_ordinal>action_->pre_record_ordinal) {
            if(action_->visual.sequence!=*changed_animation) {
                action_->visual.sequence=*changed_animation;action_->visual.restart_identity=++restart_identity_;
                action_->visual.started_at_seconds=now;finish_pending(LocalWeaponActionStatus::server_corrected);
            } else finish_pending(LocalWeaponActionStatus::server_confirmed);
            action_->visual.source=LocalWeaponAnimationSource::clientdata_state;
        }
    }
    const auto& hud=state.weapon_hud;
    if(hud.animation_source && hud.animation_sequence && hud.animation_body &&
        hud.animation_source->record_ordinal>=model_bound_record_ordinal_) {
        const SeenEvent event{*hud.animation_source,*hud.animation_sequence,*hud.animation_body};
        const bool seen=std::any_of(event_history_.begin(),event_history_.end(),
            [&](const auto& value){return value && value->source==event.source;});
        if(!seen) {
            event_history_[event_history_next_++%event_history_.size()]=event;
            if(state.receiving_client && state.receiving_client->viewmodel_index!=model_->model_index) {
                counters_.error=LocalWeaponPresentationError::stale_observation;return;
            }
            if(!valid_pose(event.sequence,event.body)) {counters_.error=LocalWeaponPresentationError::unsupported_pose;return;}
            if(action_ && event.source.record_ordinal>action_->pre_record_ordinal) {
                const bool same=action_->visual.sequence==event.sequence && action_->visual.body==event.body;
                if(same && !coalesced_settled && fresh_ammo && previous_reserve && reserve_ && *reserve_>=*previous_reserve &&
                    action_->identity.kind==LocalWeaponAction::deploy_charge) release_receipt(action_->identity);
                if(!same) {
                    action_->visual.sequence=event.sequence;action_->visual.body=event.body;
                    action_->visual.restart_identity=++restart_identity_;action_->visual.started_at_seconds=now;
                }
                finish_pending(same ? LocalWeaponActionStatus::server_confirmed : LocalWeaponActionStatus::server_corrected);
                action_->visual.source=LocalWeaponAnimationSource::service_event;
            } else if(!satchel_.retired) server_visual_=LocalWeaponVisual{event.sequence,event.body,++restart_identity_,now,
                LocalWeaponAnimationSource::service_event,LocalWeaponActionStatus::retained_server_event};
        }
    }
    if(!server_visual_ && !action_ && !satchel_.retired) publish_idle(now);
}
}
