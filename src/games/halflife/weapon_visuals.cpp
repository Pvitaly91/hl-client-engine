#include <hlclient/games/halflife/weapon_visuals.hpp>
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
    if (model.resource_name != "models/v_9mmhandgun.mdl" ||
        (sequence != 3U && sequence != 4U) || sequence >= model.sequences.size()) return {};
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
    submitted_.fill(std::nullopt); next_submitted_=0U; pending_impact_.reset();
}
void WeaponVisuals::remember_submission(const game_api::LocalWeaponSubmittedCommand& command) noexcept {
    if (!command.shot_context || !finite(command.shot_context->eye) ||
        !finite(command.shot_context->direction)) return;
    submitted_[next_submitted_]=SubmittedShot{command.generation,command.sequence,
        *command.shot_context};
    next_submitted_=(next_submitted_+1U)%submitted_.size();
}
bool WeaponVisuals::bind(const std::optional<game_api::LocalWeaponModelMetadata>& model) {
    if (model_==model) return false;
    model_=model;
    flash_.reset(); light_.reset(); shell_pending_=false; pending_impact_.reset();
    submitted_.fill(std::nullopt);
    return true;
}
void WeaponVisuals::cancel() noexcept {
    flash_.reset(); light_.reset(); shell_pending_=false; pending_impact_.reset();
    submitted_.fill(std::nullopt);
}
void WeaponVisuals::observe(const game_api::LocalWeaponPresentationSnapshot& sample,double now) noexcept {
    if (!std::isfinite(now)) return;
    if (!sample.identity && (sample.status==game_api::LocalWeaponActionStatus::server_rejected ||
        sample.status==game_api::LocalWeaponActionStatus::timed_out)) cancel();
    if (flash_ && now>=flash_->ends_at_seconds) {flash_.reset();++statistics_.flash_expired;}
    if (light_ && now>=light_->ends_at_seconds) {light_.reset();++statistics_.light_expired;}
    if (!sample.identity || !sample.action ||
        (*sample.action!=game_api::LocalWeaponAction::primary_fire &&
         *sample.action!=game_api::LocalWeaponAction::melee_swing) ||
        sample.status==game_api::LocalWeaponActionStatus::server_rejected ||
        sample.status==game_api::LocalWeaponActionStatus::timed_out) return;
    const auto& action=*sample.identity;
    if (std::any_of(seen_.begin(),seen_.end(),[&](const auto& previous) {
        return previous && *previous==action;
    })) {
        if (!last_seen_ || *last_seen_!=action) ++statistics_.exact_duplicates_suppressed;
        if (*sample.action==game_api::LocalWeaponAction::melee_swing &&
            (!last_seen_ || *last_seen_!=action))
            ++statistics_.crowbar_duplicates;
        return; // repeated sample, confirmation or replay is not a new attempt
    }
    seen_[next_seen_]=action;
    next_seen_=(next_seen_+1U)%seen_.size();
    last_seen_=action;
    if (*sample.action==game_api::LocalWeaponAction::melee_swing)
        ++statistics_.crowbar_actions;
    else ++statistics_.fire_actions_received;
    if (!model_ || model_->generation!=action.generation ||
        model_->model_index!=action.model_index || model_->resource_revision!=action.resource_revision ||
        !sample.visual) {++statistics_.unsupported_metadata;return;}
    if (model_->resource_name=="models/v_crowbar.mdl" &&
        *sample.action==game_api::LocalWeaponAction::melee_swing &&
        now>=action.started_at_seconds && now-action.started_at_seconds<=0.25) {
        for (auto& submitted:submitted_) if (submitted &&
            submitted->generation==action.generation &&
            submitted->sequence==action.command_sequence) {
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
    if (model_->resource_name=="models/v_9mmhandgun.mdl" &&
        *sample.action==game_api::LocalWeaponAction::primary_fire &&
        now>=action.started_at_seconds && now-action.started_at_seconds<=0.25) {
        for (auto& submitted:submitted_) if (submitted &&
            submitted->generation==action.generation &&
            submitted->sequence==action.command_sequence) {
            pending_impact_=game_api::LocalWorldImpactRequest{
                action,submitted->context.eye,submitted->context.direction,
                8192.0F,4.0F,action.started_at_seconds+0.25};
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
    light_=game_api::LocalMuzzleLight{action,96.0F,0.8F,{1.0F,0.62F,0.28F},
        action.started_at_seconds,action.started_at_seconds+duration};
    ++statistics_.light_requested;
    shell_pending_=true; ++statistics_.flash_scheduled;
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
            constexpr char name[]="models/shell.mdl";
            std::copy(name,name+sizeof(name),shell.model_name.begin());
            // SDK EV_GetDefaultShellInfo profile, with per-action deterministic variation.
            shell.origin=add(add(add(context.eye,scaled(context.forward,20)),
                scaled(context.up,-12)),scaled(context.right,4));
            shell.velocity=add(add(add(context.velocity,scaled(context.forward,25)),
                scaled(context.right,50+static_cast<float>(v%21))),
                scaled(context.up,100+static_cast<float>((v>>8)%51)));
            shell.yaw_degrees=std::atan2(context.forward.y,context.forward.x)*180.0F/3.14159265358979323846F;
            shell.starts_at_seconds=flash_->starts_at_seconds;
            shell.expires_at_seconds=flash_->starts_at_seconds+2.5;
            out.shell=shell; ++statistics_.shells_scheduled;
        } else ++statistics_.unsupported_metadata;
    }
    out.statistics=statistics_;
    return out;
}
}
