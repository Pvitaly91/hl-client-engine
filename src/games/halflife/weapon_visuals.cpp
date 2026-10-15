#include <hlclient/games/halflife/weapon_visuals.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace hlclient::games::halflife {
namespace {
using V = assets::AssetVector3;
V add(V a, V b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V scaled(V a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
bool finite(V v) noexcept { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
std::optional<std::pair<std::uint32_t,std::uint32_t>> flash_marker(
    const game_api::LocalWeaponModelMetadata& model, std::uint32_t sequence) noexcept {
    const auto firearm=firearm_model(model.resource_name);
    if (firearm==Firearm::unsupported ||
        (firearm==Firearm::python ? !python_sequences(model) || sequence!=2 : firearm==Firearm::shotgun ? !shotgun_sequences(model) || (sequence!=1 && sequence!=2) :
         firearm==Firearm::mp5 ? !mp5_sequences(model) || sequence<5 || sequence>7 :
            sequence != 3U && sequence != 4U) || sequence >= model.sequences.size()) return {};
    const auto& events=model.sequences[sequence].events;
    for (std::size_t i=0;i<events.size();++i) {
        const auto& e=events[i];
        if (e.event_number!=5001 || e.frame!=0 || e.options.empty() || e.options.size()>3) continue;
        unsigned variant=0;
        bool valid=true;
        for (const auto byte:e.options) {
            const auto c=static_cast<unsigned char>(byte);
            if (c<'0'||c>'9') {valid=false;break;}
            variant=variant*10+(c-'0');
        }
        if (valid && variant<=99U) return {{static_cast<std::uint32_t>(i),variant}};
    }
    return {};
}
std::uint32_t variation(const game_api::LocalWeaponActionIdentity& a) noexcept {
    auto x=a.command_sequence ^ static_cast<std::uint32_t>(a.generation) ^
        (a.model_index*0x9e3779b9U);
    x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; return x^(x>>16);
}
}
void WeaponVisuals::reset() noexcept {
    model_.reset(); flash_.reset(); light_.reset(); seen_.fill(std::nullopt);
    last_seen_.reset(); next_seen_=0U; shell_pending_=false; statistics_={};
    seen_sniper_.fill(std::nullopt);next_sniper_=0;
    submitted_.fill(std::nullopt); next_submitted_=0U; pending_impact_.reset();
    tracer_count_=0;
    pending_extra_impact_count_=0; shell_context_.reset(); double_shell_=false;
}
void WeaponVisuals::remember_submission(const game_api::LocalWeaponSubmittedCommand& command) noexcept {
    if (!command.shot_context || !finite(command.shot_context->eye) ||
        !finite(command.shot_context->direction) || !finite(command.shot_context->right) ||
        !finite(command.shot_context->up)) return;
    submitted_[next_submitted_]=SubmittedShot{command.generation,command.sequence,
        *command.shot_context,std::nullopt};
    next_submitted_=(next_submitted_+1U)%submitted_.size();
}
void WeaponVisuals::remember_committed_shot(
    const std::optional<game_api::LocalWeaponActionIdentity>& identity,
    const game_api::CommittedScriptedEvent& event) noexcept {
    if (!model_ || (firearm_model(model_->resource_name)!=Firearm::shotgun &&
        firearm_model(model_->resource_name)!=Firearm::python && firearm_model(model_->resource_name)!=Firearm::crossbow) ||
        !identity || identity->command_sequence!=0 ||
        (identity->kind!=game_api::LocalWeaponAction::primary_fire &&
         identity->kind!=game_api::LocalWeaponAction::secondary_fire) ||
        identity->generation!=event.generation ||
        identity->started_at_seconds!=event.received_at_seconds ||
        event.resolution!=game_api::ScriptedEventResolution::ready ||
        !finite(event.origin) || !finite(event.angles) ||
        !std::isfinite(event.received_at_seconds)) return;
    constexpr float rad=3.14159265358979323846F/180.0F;
    const auto pitch=(event.angles_from_entity ?
        -3.0F*std::remainder(event.angles.x,360.0F) : event.angles.x)*rad;
    const auto yaw=event.angles.y*rad,roll=event.angles.z*rad;
    const auto cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    const auto cr=std::cos(roll),sr=std::sin(roll);
    game_api::LocalWeaponSubmittedCommand::ShotContext context;
    context.eye=event.origin;context.eye.z+=event.ducking ? 12.0F : 28.0F;
    context.direction={cp*cy,cp*sy,-sp};
    context.right={-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp};
    context.up={cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    if(firearm_model(model_->resource_name)==Firearm::python) {
        if(!std::isfinite(event.spread_x) || !std::isfinite(event.spread_y)) return;
        const auto direction=add(add(context.direction,scaled(context.right,event.spread_x)),scaled(context.up,event.spread_y));
        const auto length=std::hypot(direction.x,direction.y,direction.z);
        if(!std::isfinite(length) || length<1e-6F) return;
        context.direction=scaled(direction,1.0F/length);
    }
    // Committed-only actions have no command sequence. Bind their owning
    // context to the exact action time, so an unused earlier echo cannot feed
    // a later action. The same bounded ring/reset rules cover both sources.
    submitted_[next_submitted_]=SubmittedShot{event.generation,0,context,event.received_at_seconds};
    next_submitted_=(next_submitted_+1U)%submitted_.size();
}
bool WeaponVisuals::bind(const std::optional<game_api::LocalWeaponModelMetadata>& model) {
    if (model_==model) return false;
    model_=model;
    flash_.reset(); light_.reset(); shell_pending_=false; pending_impact_.reset();
    submitted_.fill(std::nullopt);
    pending_extra_impact_count_=0;shell_context_.reset();double_shell_=false;
    return true;
}
void WeaponVisuals::cancel() noexcept {
    flash_.reset(); light_.reset(); shell_pending_=false; pending_impact_.reset();
    submitted_.fill(std::nullopt);
    pending_extra_impact_count_=0;shell_context_.reset();double_shell_=false;
}
void WeaponVisuals::observe(const game_api::LocalWeaponPresentationSnapshot& sample,double now,bool sniper) noexcept {
    if (!std::isfinite(now)) return;
    if (!sample.identity && (sample.status==game_api::LocalWeaponActionStatus::server_rejected ||
        sample.status==game_api::LocalWeaponActionStatus::timed_out)) cancel();
    if (flash_ && now>=flash_->ends_at_seconds) {flash_.reset();++statistics_.flash_expired;}
    if (light_ && now>=light_->ends_at_seconds) {light_.reset();++statistics_.light_expired;}
    if (!sample.identity || !sample.action ||
        (*sample.action!=game_api::LocalWeaponAction::primary_fire &&
         *sample.action!=game_api::LocalWeaponAction::secondary_fire &&
         *sample.action!=game_api::LocalWeaponAction::melee_swing) ||
        sample.status==game_api::LocalWeaponActionStatus::server_rejected ||
        sample.status==game_api::LocalWeaponActionStatus::timed_out) return;
    const auto& action=*sample.identity;
    // The pinned RPG callback contains no muzzle flash, shell or hitscan.
    // Flight/laser/trail/explosion are committed server entity mechanisms.
    if(action.weapon_id==8U) return;
    // A late exact crossbow2 echo may refine ordinary provisional mode once.
    // Separate component ownership prevents both lost sniper effects and replay.
    const bool crossbow_sniper=model_ && firearm_model(model_->resource_name)==Firearm::crossbow && sniper;
    auto& seen=crossbow_sniper ? seen_sniper_ : seen_;
    auto& next=crossbow_sniper ? next_sniper_ : next_seen_;
    if (std::any_of(seen.begin(),seen.end(),[&](const auto& previous) {
        return previous && *previous==action;
    })) {
        if (!last_seen_ || *last_seen_!=action) ++statistics_.exact_duplicates_suppressed;
        if (*sample.action==game_api::LocalWeaponAction::melee_swing &&
            (!last_seen_ || *last_seen_!=action))
            ++statistics_.crowbar_duplicates;
        return; // repeated sample, confirmation or replay is not a new attempt
    }
    seen[next]=action;
    next=(next+1U)%seen.size();
    last_seen_=action;
    if (*sample.action==game_api::LocalWeaponAction::melee_swing)
        ++statistics_.crowbar_actions;
    else ++statistics_.fire_actions_received;
    if (!model_ || model_->generation!=action.generation ||
        model_->model_index!=action.model_index || model_->resource_revision!=action.resource_revision ||
        !sample.visual) {++statistics_.unsupported_metadata;return;}
    if (firearm_model(model_->resource_name)==Firearm::crossbow) {
        if(sniper && now>=action.started_at_seconds && now-action.started_at_seconds<=0.25)
            for(auto& submitted:submitted_) if(submitted && submitted->generation==action.generation &&
                submitted->sequence==action.command_sequence &&
                (!submitted->committed_at || *submitted->committed_at==action.started_at_seconds)) {
                pending_impact_=game_api::LocalWorldImpactRequest{action,submitted->context.eye,
                    submitted->context.direction,8192.0F,4.0F,action.started_at_seconds+0.25,0.0};
                pending_impact_->suppress_decal=true;
                submitted.reset();break;
            }
        return; // ordinary mode is server-owned projectile; neither mode ejects brass/flash/tracer.
    }
    if (model_->resource_name=="models/v_crowbar.mdl" &&
        *sample.action==game_api::LocalWeaponAction::melee_swing &&
        now>=action.started_at_seconds && now-action.started_at_seconds<=0.25) {
        for (auto& submitted:submitted_) if (submitted &&
            submitted->generation==action.generation &&
            submitted->sequence==action.command_sequence &&
            (!submitted->committed_at || *submitted->committed_at==action.started_at_seconds)) {
            // Pinned HL SDK Swing uses a 32-unit gun-position line trace.
            // No local hull approximation or damage authority is inferred.
            pending_impact_=game_api::LocalWorldImpactRequest{
                action,submitted->context.eye,submitted->context.direction,
                32.0F,4.0F,action.started_at_seconds+0.25,0.2};
            submitted.reset();
            ++statistics_.crowbar_requests;
            break;
        }
        return; // no Glock flash, light or shell for a crowbar swing
    }
    if (firearm_model(model_->resource_name)!=Firearm::unsupported &&
        (*sample.action==game_api::LocalWeaponAction::primary_fire ||
         *sample.action==game_api::LocalWeaponAction::secondary_fire) &&
        now>=action.started_at_seconds && now-action.started_at_seconds<=0.25) {
        for (auto& submitted:submitted_) if (submitted &&
            submitted->generation==action.generation &&
            submitted->sequence==action.command_sequence &&
            (!submitted->committed_at || *submitted->committed_at==action.started_at_seconds)) {
            pending_impact_=game_api::LocalWorldImpactRequest{
                action,submitted->context.eye,submitted->context.direction,
                8192.0F,4.0F,action.started_at_seconds+0.25,0.0,bullet_impact_particles()};
            if(firearm_model(model_->resource_name)==Firearm::shotgun) {
                const bool double_shot=*sample.action==game_api::LocalWeaponAction::secondary_fire;
                const std::size_t count=double_shot ? 8U : 4U;
                shell_context_=submitted->context;double_shell_=double_shot;
                pending_impact_->maximum_distance_units=2048;
                pending_impact_->action.sub_identity=1;
                pending_impact_->direction=shotgun_pellet_direction(action,0,submitted->context.direction,
                    submitted->context.right,submitted->context.up,double_shot);
                pending_extra_impact_count_=count-1;
                for(std::size_t ordinal=1;ordinal<count;++ordinal) {
                    auto& pellet=pending_extra_impacts_[ordinal-1];pellet=*pending_impact_;
                    pellet.action.sub_identity=static_cast<std::uint32_t>(ordinal+1);
                    pellet.direction=shotgun_pellet_direction(action,static_cast<std::uint32_t>(ordinal),
                        submitted->context.direction,submitted->context.right,submitted->context.up,double_shot);
                }
            }
            if(firearm_model(model_->resource_name)==Firearm::mp5 && (tracer_count_++%2U)==0U) {
                pending_impact_->tracer=game_api::LocalWorldImpactRequest::Tracer{add(
                    submitted->context.eye,scaled(submitted->context.direction,16))};
                pending_impact_->suppress_surface_effects=true;
                pending_impact_->tracer->follow_local_muzzle=true;
                pending_impact_->tracer->width_units=0.65F;
                pending_impact_->tracer->start_offset_units=mp5_tracer_start_offset(action);
                pending_impact_->tracer->lifetime_seconds=0.1014;
                pending_impact_->tracer->travel_speed_units_per_second=12000;
                pending_impact_->tracer->segment_length_units=48;
                // Rounded front occupies 10%; the remaining 90% tapers rearward.
                pending_impact_->tracer->head_taper_fraction=0.90F;
                pending_impact_->tracer->taper_end_diameter_fraction=0.20F;
                pending_impact_->tracer->head_glow={1.25F,48.0F,0.35F};
                pending_impact_->tracer->geometry=game_api::WorldLineGeometry::solid_3d;
                pending_impact_->particles.reset();
            }
            submitted.reset();
            break;
        }
    }
    const auto marker=flash_marker(*model_,sample.visual->sequence);
    if (!marker) {++statistics_.unsupported_metadata;return;}
    constexpr double duration=0.075;
    if (now>=action.started_at_seconds+duration || now<action.started_at_seconds) {
        ++statistics_.late_cues_dropped;return;
    }
    flash_=game_api::LocalMuzzleFlash{action,action.model_index,sample.visual->sequence,
        marker->first,0U,marker->second,2.0F,{1.0F,0.72F,0.28F,0.85F},action.started_at_seconds,
        action.started_at_seconds+duration};
    flash_->sprite=muzzle_sprite_profile(marker->second,variation(action));
    const bool python=firearm_model(model_->resource_name)==Firearm::python;
    light_=game_api::LocalMuzzleLight{action,python ? 128.0F : 96.0F,python ? 1.0F : 0.8F,{1.0F,0.62F,0.28F},
        action.started_at_seconds,action.started_at_seconds+duration};
    ++statistics_.light_requested;
    shell_pending_=firearm_model(model_->resource_name)!=Firearm::python;
    ++statistics_.flash_scheduled;
}
game_api::LocalVisualFrame WeaponVisuals::frame(const game_api::LocalVisualContext& context) noexcept {
    game_api::LocalVisualFrame out;
    out.statistics=statistics_;
    if (!std::isfinite(context.now_seconds)) return out;
    if (pending_impact_) {
        if (context.now_seconds<=pending_impact_->expires_at_seconds &&
            context.now_seconds>=pending_impact_->action.started_at_seconds)
            out.world_impact=std::exchange(pending_impact_,std::nullopt);
        else pending_impact_.reset();
    }
    if(out.world_impact) {
        out.extra_impact_count=pending_extra_impact_count_;
        std::copy_n(pending_extra_impacts_.begin(),out.extra_impact_count,out.extra_impacts.begin());
    }
    pending_extra_impact_count_=0;
    if (light_ && context.now_seconds>=light_->ends_at_seconds) {
        light_.reset(); ++statistics_.light_expired;
    }
    if (light_ && context.now_seconds>=light_->starts_at_seconds) {
        auto light=*light_;
        const auto duration=light.ends_at_seconds-light.starts_at_seconds;
        if (duration>0.0) {
            const auto remaining=(light.ends_at_seconds-context.now_seconds)/duration;
            light.intensity*=static_cast<float>(std::clamp(remaining,0.0,1.0));
            out.light=light;
        }
    }
    if (!flash_) {out.statistics=statistics_; return out;}
    if (context.now_seconds>=flash_->ends_at_seconds) {
        flash_.reset(); shell_pending_=false; ++statistics_.flash_expired;
        out.statistics=statistics_;return out;
    }
    out.flash=flash_;
    if (shell_pending_) {
        shell_pending_=false;
        if (finite(context.eye)&&finite(context.forward)&&finite(context.right)&&
            finite(context.up)&&finite(context.velocity)) {
            const auto v=variation(flash_->action);
            game_api::LocalShellEjection shell;
            shell.action=flash_->action;
            const bool shotgun=model_ && firearm_model(model_->resource_name)==Firearm::shotgun;
            const std::string_view name=shotgun ? "models/shotgunshell.mdl" : "models/shell.mdl";
            std::copy(name.begin(),name.end(),shell.model_name.begin());
            // Both pinned Shotgun event callbacks explicitly request these eye
            // offsets. No shell attachment exists in the inspected v-model;
            // this is the diagnosed SDK eye-space fallback, not a guessed index.
            const auto eye=shotgun && shell_context_ ? shell_context_->eye : context.eye;
            const auto forward=shotgun && shell_context_ ? shell_context_->direction : context.forward;
            const auto right=shotgun && shell_context_ ? shell_context_->right : context.right;
            const auto up=shotgun && shell_context_ ? shell_context_->up : context.up;
            shell.origin=add(add(add(eye,scaled(forward,shotgun ? 32.0F : 20.0F)),scaled(up,-12)),
                scaled(right,shotgun ? 6.0F : 4.0F));
            shell.velocity=add(add(add(context.velocity,scaled(forward,25)),
                scaled(right,50+static_cast<float>(v%21))),
                scaled(up,100+static_cast<float>((v>>8)%51)));
            shell.yaw_degrees=std::atan2(forward.y,forward.x)*180.0F/3.14159265358979323846F;
            shell.starts_at_seconds=flash_->starts_at_seconds;
            shell.expires_at_seconds=flash_->starts_at_seconds+2.5;
            out.shell=shell; ++statistics_.shells_scheduled;
            if(shotgun && double_shell_) {
                auto second=shell;second.action.sub_identity=1;
                second.velocity=add(second.velocity,add(scaled(right,7),scaled(up,11)));
                out.extra_shells[0]=second;out.extra_shell_count=1;++statistics_.shells_scheduled;
            }
            shell_context_.reset();double_shell_=false;
        } else ++statistics_.unsupported_metadata;
    }
    out.statistics=statistics_;
    return out;
}
}
