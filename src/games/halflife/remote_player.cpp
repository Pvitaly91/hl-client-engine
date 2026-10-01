#include <hlclient/games/halflife/remote_player.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace hlclient::games::halflife {
namespace {
namespace api = game_api;
constexpr double kTransitionSeconds = 0.2;
constexpr double kStationaryYawRate = 4.0;
constexpr double kBackwardBoundaryDegrees = 120.0;
constexpr double kMinimumMovementSpeed = 5.0;

double signed_angle(double value) noexcept {
    auto result = std::remainder(value, 360.0);
    return result == -180.0 ? 180.0 : result;
}
double ease_yaw(double start, double aim, double elapsed) noexcept {
    return signed_angle(start + signed_angle(aim-start) *
        (1.0-std::exp(-kStationaryYawRate*std::max(0.0,elapsed))));
}
bool finite_vector(const client::RuntimeVector3Observation& v) noexcept {
    return v.complete() && std::isfinite(*v.x) && std::isfinite(*v.y) &&
        std::isfinite(*v.z);
}
bool valid_sequence(const assets::ModelSequence& sequence) noexcept {
    return sequence.frame_count>0U && sequence.frame_count<=1'000'000U &&
        std::isfinite(sequence.frames_per_second) && sequence.frames_per_second>=0.0F &&
        sequence.frames_per_second<=1000.0F &&
        std::isfinite(sequence.blend_start[0]) && std::isfinite(sequence.blend_end[0]) &&
        std::isfinite(sequence.linear_movement.x) &&
        sequence.blend_end[0]>=sequence.blend_start[0] &&
        (sequence.blend_count==1U || sequence.blend_count==2U || sequence.blend_count==4U);
}
double normalized_main_frame(const assets::ModelSequence& sequence,double frame) noexcept {
    const auto last=static_cast<double>(sequence.frame_count-1U);
    if ((sequence.source_flags & 1U)!=0U && last>0.0) {
        auto result=std::fmod(frame,last);
        if(result<0.0) result+=last;
        return result;
    }
    return std::clamp(frame,0.0,last);
}
double normalized_gait_phase(const assets::ModelSequence& sequence,double phase) noexcept {
    auto result=std::fmod(phase,static_cast<double>(sequence.frame_count));
    if(result<0.0) result+=sequence.frame_count;
    return result;
}
double normalized_gait_frame(const assets::ModelSequence& sequence,double phase) noexcept {
    // The reference gait clock wraps by numframes, while rotation sampling
    // snaps its final fractional interval beyond numframes-1 to frame zero.
    // Select that numeric coordinate here, without changing the shared sampler.
    const auto result=normalized_gait_phase(sequence,phase);
    return result>static_cast<double>(sequence.frame_count-1U) ? 0.0 : result;
}
double gait_phase_increment(const assets::ModelSequence& sequence,double dx,double dy,
    double dz,double elapsed,double aim) noexcept {
    if(elapsed<=0.0) return 0.0;
    if(sequence.linear_movement.x<=0.0F)
        return static_cast<double>(sequence.frames_per_second)*elapsed;
    if(std::hypot(dx,dy)/elapsed<kMinimumMovementSpeed) return 0.0;
    auto distance=std::sqrt(dx*dx+dy*dy+dz*dz);
    const auto yaw=std::atan2(dy,dx)*180.0/std::numbers::pi;
    if(std::abs(signed_angle(aim-yaw))>kBackwardBoundaryDegrees) distance=-distance;
    return distance/static_cast<double>(sequence.linear_movement.x)*sequence.frame_count;
}
api::RemotePlayerPresentationIntent status(api::RemotePlayerPresentationStatus value) noexcept {
    api::RemotePlayerPresentationIntent result;
    result.status=value;
    return result;
}
std::uint8_t control_byte(double value,double start,double end) noexcept {
    return static_cast<std::uint8_t>(std::clamp(
        std::floor((value-start)*255.0/(end-start)),0.0,255.0));
}
bool player_pitch(api::RemotePlayerStudioSample& sample,const assets::ModelSequence& sequence,
    double& pitch) noexcept {
    const double start=sequence.blend_start[0],end=sequence.blend_end[0];
    if(!std::isfinite(start) || !std::isfinite(end) || end<start) return false;
    const auto aim_pitch=pitch*3.0;
    if(aim_pitch<start) {sample.blending[0]=0U; pitch-=start/3.0;}
    else if(aim_pitch>end) {sample.blending[0]=255U; pitch-=end/3.0;}
    else {sample.blending[0]=end-start<0.1 ? 127U : control_byte(aim_pitch,start,end); pitch=0.0;}
    return true;
}
} // namespace

api::RemotePlayerPresentationPolicy HalfLifeRemotePlayerPresentation::policy() noexcept {
    // Reference-informed delay/transition; gap/teleport are bounded local-
    // compatible recovery, not stock-engine constants or physics policy.
    return {true,0.1,0.25,128.0,api::kMaximumRemotePlayers,32U};
}
void HalfLifeRemotePlayerPresentation::reset() noexcept {
    players_={}; models_={};
}

bool HalfLifeRemotePlayerPresentation::derive_profile(
    const assets::SkeletalModelAssetData& model,ModelProfile& result) noexcept {
    if(model.bones.empty() || model.bones.size()>api::kMaximumRemotePlayerBones) return false;
    std::optional<std::size_t> pelvis,spine;
    for(std::size_t i=0;i<model.bones.size();++i) {
        if(model.bones[i].name=="Bip01 Pelvis") {
            if(pelvis) return false;
            pelvis=i;
        }
        if(model.bones[i].name=="Bip01 Spine") {
            if(spine) return false;
            spine=i;
        }
    }
    if(!pelvis || !spine || model.bones[*spine].parent_index!=static_cast<std::int32_t>(*pelvis))
        return false;
    const auto descendant=[&](std::size_t bone,std::size_t ancestor) noexcept {
        for(std::size_t visited=0;visited<=model.bones.size();++visited) {
            if(bone==ancestor) return true;
            const auto parent=model.bones[bone].parent_index;
            if(parent==-1) return false;
            if(parent<0 || static_cast<std::size_t>(parent)>=model.bones.size()) return false;
            bone=static_cast<std::size_t>(parent);
        }
        return false;
    };
    std::size_t roots=0U,lower=0U,upper=0U;
    for(std::size_t i=0;i<model.bones.size();++i) {
        const auto parent=model.bones[i].parent_index;
        if(parent==-1) ++roots;
        else if(parent<0 || static_cast<std::size_t>(parent)>=model.bones.size() ||
            descendant(static_cast<std::size_t>(parent),i)) return false;
        if(!descendant(i,*pelvis) && !descendant(*pelvis,i)) return false;
        result.lower_mask[i]=descendant(i,*spine) ? 0U : 1U;
        if(result.lower_mask[i]) ++lower; else ++upper;
    }
    if(roots!=1U || !lower || !upper) return false;
    std::array<bool,4U> seen{};
    for(const auto& controller:model.bone_controllers) {
        if(controller.controller_index<0 || controller.controller_index>=4) continue;
        const auto index=static_cast<std::size_t>(controller.controller_index);
        // HL selects the four torso inputs, not a fixed model-local axis.
        // The approved player model uses XR; independent valid models may
        // declare YR or ZR. Require exactly one supported rotational bit and
        // the matching used channel, never infer an axis from a bone name.
        const auto rotation_channel=controller.source_type==0x0008U ? 3U :
            controller.source_type==0x0010U ? 4U : controller.source_type==0x0020U ? 5U : 6U;
        if(seen[index] || controller.bone_index<0 ||
            static_cast<std::size_t>(controller.bone_index)>=model.bones.size() ||
            !descendant(static_cast<std::size_t>(controller.bone_index),*spine) ||
            rotation_channel>=6U ||
            !std::isfinite(controller.start) || !std::isfinite(controller.end) ||
            controller.start>=controller.end || controller.start>0.0F || controller.end<0.0F)
            return false;
        // The controlling record must actually be used by the declared bone's
        // declared rotational channel; a matching input number alone is insufficient.
        const auto ordinal=static_cast<std::int32_t>(&controller-model.bone_controllers.data());
        if(model.bones[static_cast<std::size_t>(controller.bone_index)].controller_indices[rotation_channel]!=ordinal)
            return false;
        seen[index]=true;
        result.controller_start[index]=controller.start;
        result.controller_end[index]=controller.end;
    }
    if(!std::all_of(seen.begin(),seen.end(),[](bool value){return value;})) return false;
    result.bone_count=model.bones.size();
    return true;
}

HalfLifeRemotePlayerPresentation::ModelProfile*
HalfLifeRemotePlayerPresentation::profile(const api::RemotePlayerPresentationContext& input) noexcept {
    for(auto& candidate:models_) if(candidate.used && candidate.resource_id==input.model_resource_id &&
        candidate.revision==input.model_revision) return &candidate;
    auto found=std::find_if(models_.begin(),models_.end(),[&](const ModelProfile& candidate) {
        if(!candidate.used) return true;
        return std::none_of(players_.begin(),players_.end(),[&](const PlayerState& player) {
            return player.used && player.model==candidate.resource_id && player.revision==candidate.revision;
        });
    });
    if(found==models_.end()) return nullptr;
    *found={}; found->used=true; found->resource_id=input.model_resource_id;
    found->revision=input.model_revision;
    found->supported=derive_profile(input.model,*found);
    return &*found;
}

std::optional<HalfLifeRemotePlayerPresentation::FrameAnchor>
HalfLifeRemotePlayerPresentation::anchor(const client::RuntimePacketEntityObservation& entity,
    const assets::SkeletalModelAssetData& model) noexcept {
    if(!entity.sequence || !entity.frame || !entity.animation_time_seconds || !entity.frame_rate ||
        !entity.body || !entity.skin || *entity.sequence>=model.sequences.size() ||
        !valid_sequence(model.sequences[*entity.sequence]) ||
        !std::isfinite(*entity.frame) || *entity.frame<0.0 || *entity.frame>256.0 ||
        !std::isfinite(*entity.animation_time_seconds) || std::abs(*entity.animation_time_seconds)>1.0e9 ||
        !std::isfinite(*entity.frame_rate) || std::abs(*entity.frame_rate)>16.0 ||
        *entity.body>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        *entity.skin<0 || static_cast<std::size_t>(*entity.skin)>=model.skin_families.size()) return {};
    FrameAnchor result{*entity.sequence,*entity.frame,*entity.animation_time_seconds,*entity.frame_rate,
        static_cast<std::int32_t>(*entity.body),static_cast<std::uint32_t>(*entity.skin)};
    for(std::size_t i=0;i<result.controllers.size();++i) {
        if(!entity.controllers[i] || *entity.controllers[i]>255U) return {};
        result.controllers[i]=static_cast<std::uint8_t>(*entity.controllers[i]);
    }
    for(std::size_t i=0;i<result.blending.size();++i) {
        if(!entity.blending[i] || *entity.blending[i]>255U) return {};
        result.blending[i]=static_cast<std::uint8_t>(*entity.blending[i]);
    }
    return result;
}

api::RemotePlayerStudioSample HalfLifeRemotePlayerPresentation::main_sample(
    const FrameAnchor& anchor_value,const assets::SkeletalModelAssetData& model,double time) noexcept {
    const auto& sequence=model.sequences[anchor_value.sequence];
    const auto raw=anchor_value.wire_frame*static_cast<double>(sequence.frame_count-1U)/256.0 +
        std::max(0.0,time-anchor_value.animation_time)*anchor_value.frame_rate*sequence.frames_per_second;
    return {anchor_value.sequence,normalized_main_frame(sequence,raw),anchor_value.body,
        anchor_value.skin,anchor_value.controllers,anchor_value.blending};
}

api::RemotePlayerPresentationIntent HalfLifeRemotePlayerPresentation::sample(
    const api::RemotePlayerPresentationContext& input) noexcept {
    using Status=api::RemotePlayerPresentationStatus;
    const auto& entity=input.entity;
    const auto& current=input.current;
    if(!entity.player_movement_schema || !current.player_movement_schema || entity.entity_number==0U ||
        entity.entity_number>input.maximum_clients || entity.entity_number>api::kMaximumRemotePlayers)
        return status(Status::not_player);
    if(!input.receiving_entity) return status(Status::missing_fields);
    if(entity.entity_number==*input.receiving_entity) return status(Status::local_excluded);
    if(!input.network_generation || !input.map_generation || !input.entity_lifetime ||
        !input.model_resource_id || (!input.model_revision.primary && !input.model_revision.secondary) ||
        !input.current_record_identity || !input.current_record_ordinal || current.entity_number!=entity.entity_number ||
        input.maximum_clients==0U || input.maximum_clients>api::kMaximumRemotePlayers ||
        !std::isfinite(input.current_server_seconds) || !std::isfinite(input.sample_server_seconds) ||
        std::abs(input.current_server_seconds)>1.0e9 || std::abs(input.sample_server_seconds)>1.0e9 ||
        input.sample_server_seconds>input.current_server_seconds+policy().maximum_observation_gap_seconds)
        return status(Status::invalid_context);
    if(!finite_vector(entity.origin) || !finite_vector(entity.angles) || !finite_vector(current.origin) ||
        !finite_vector(current.angles) || !current.gait_sequence || !entity.gait_sequence || !current.model_index ||
        !entity.model_index || *current.model_index!=*entity.model_index)
        return status(Status::missing_fields);
    if(!current.sequence || *current.sequence>=input.model.sequences.size() ||
        *current.gait_sequence>=input.model.sequences.size()) return status(Status::invalid_sequence);
    const auto main=anchor(current,input.model);
    if(!main) return status(Status::missing_fields);
    if(!valid_sequence(input.model.sequences[*current.gait_sequence])) return status(Status::invalid_sequence);
    if(!entity.body || !entity.skin ||
        *entity.body>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        *entity.skin<0 || static_cast<std::size_t>(*entity.skin)>=input.model.skin_families.size())
        return status(Status::missing_fields);
    std::array<std::uint8_t,4U> sampled_controllers{};
    std::array<std::uint8_t,2U> sampled_blending{};
    for(std::size_t i=0;i<sampled_controllers.size();++i) {
        if(!entity.controllers[i] || *entity.controllers[i]>255U) return status(Status::missing_fields);
        sampled_controllers[i]=static_cast<std::uint8_t>(*entity.controllers[i]);
    }
    for(std::size_t i=0;i<sampled_blending.size();++i) {
        if(!entity.blending[i] || *entity.blending[i]>255U) return status(Status::missing_fields);
        sampled_blending[i]=static_cast<std::uint8_t>(*entity.blending[i]);
    }
    auto& state=players_[entity.entity_number-1U];
    const bool changed=!state.used || state.network!=input.network_generation || state.map!=input.map_generation ||
        state.lifetime!=input.entity_lifetime || state.model!=input.model_resource_id || state.revision!=input.model_revision;
    const bool replaced=state.used && changed;
    if(changed) state={};
    if(state.used && input.current_record_ordinal<state.ordinal) return status(Status::stale_source);
    if(state.used && ((input.current_record_ordinal==state.ordinal &&
            (input.current_record_identity!=state.record || *main!=state.main)) ||
            (input.current_record_identity==state.record && input.current_record_ordinal!=state.ordinal)))
        return status(Status::invalid_context);
    const auto* model_profile=profile(input);
    if(!model_profile || !model_profile->supported) return status(Status::unsupported_skeleton);
    if(!state.used || input.current_record_identity!=state.record) {
        const bool had_state=state.used;
        const auto dt=input.current_server_seconds-input.previous_server_seconds;
        const bool no_interpolation=current.effects &&
            ((*current.effects & policy().no_interpolation_effect_mask)!=0U);
        const bool pair=input.previous && input.previous_record_identity && input.previous_record_ordinal &&
            input.previous_record_ordinal<input.current_record_ordinal &&
            input.previous->entity_number==entity.entity_number &&
            input.previous->model_index==current.model_index && finite_vector(input.previous->origin) &&
            finite_vector(input.previous->angles) && std::isfinite(dt) && dt>=0.0 &&
            dt<=policy().maximum_observation_gap_seconds && !input.discontinuity && !no_interpolation && !replaced;
        const bool adjacent=pair && (!had_state ||
            (state.record==input.previous_record_identity && state.ordinal==input.previous_record_ordinal &&
                std::abs(state.current_time-input.previous_server_seconds)<=1.0e-6));
        // RX may publish several observations before a render sample. Retain
        // only numeric state and catch up through the actual latest previous
        // anchor, under the same generation/model/gap/teleport bounds. This is
        // bounded net displacement, not reconstruction of unseen gait changes.
        const auto missed_dt=input.previous_server_seconds-state.current_time;
        const auto missed_dx=pair ? *input.previous->origin.x-state.current_origin[0] : 0.0;
        const auto missed_dy=pair ? *input.previous->origin.y-state.current_origin[1] : 0.0;
        const auto missed_dz=pair ? *input.previous->origin.z-state.current_origin[2] : 0.0;
        const auto missed_distance=std::sqrt(missed_dx*missed_dx+missed_dy*missed_dy+missed_dz*missed_dz);
        const bool catch_up=pair && had_state && state.ordinal<input.previous_record_ordinal &&
            state.record!=input.previous_record_identity && std::isfinite(missed_dt) && missed_dt>=0.0 &&
            input.current_server_seconds-state.current_time<=policy().maximum_observation_gap_seconds &&
            std::isfinite(missed_distance) && missed_distance<=policy().teleport_distance_units &&
            input.previous->gait_sequence && *input.previous->gait_sequence<input.model.sequences.size() &&
            valid_sequence(input.model.sequences[*input.previous->gait_sequence]) &&
            (input.previous->effects.value_or(0U)&policy().no_interpolation_effect_mask)==0U;
        double distance=0.0,dx=0.0,dy=0.0;
        if(pair) {
            dx=*current.origin.x-*input.previous->origin.x;
            dy=*current.origin.y-*input.previous->origin.y;
            const auto dz=*current.origin.z-*input.previous->origin.z;
            distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        }
        const bool continuous=(adjacent || catch_up) && std::isfinite(distance) && distance<=policy().teleport_distance_units;
        const auto aim=signed_angle(*current.angles.y);
        const auto start_yaw=continuous ? (had_state ? state.current_yaw :
            signed_angle(*input.previous->angles.y)) : aim;
        // Gait zero suppresses processing (jump/death/non-gait activity); its
        // descriptor is not the owner of the retained gait phase. Preserve the
        // last active phase through it instead of wrapping by sequence zero.
        double start_phase=continuous && had_state ? (state.gait_sequence!=0U ?
            normalized_gait_phase(input.model.sequences[state.gait_sequence],state.current_phase) :
            state.current_phase) : 0.0;
        if(continuous && catch_up && *input.previous->gait_sequence!=0U) {
            start_phase+=gait_phase_increment(input.model.sequences[*input.previous->gait_sequence],
                missed_dx,missed_dy,missed_dz,missed_dt,signed_angle(*input.previous->angles.y));
            start_phase=normalized_gait_phase(input.model.sequences[*input.previous->gait_sequence],start_phase);
        }
        const auto prior_main=catch_up ? anchor(*input.previous,input.model) : std::optional{state.main};
        if(had_state && continuous && prior_main && prior_main->sequence!=main->sequence) {
            state.transition_previous=main_sample(*prior_main,input.model,input.current_server_seconds);
            auto prior_pitch=signed_angle(*input.previous->angles.x);
            if(!player_pitch(*state.transition_previous,input.model.sequences[prior_main->sequence],prior_pitch))
                return status(Status::invalid_sequence);
            auto prior_torso=signed_angle(*input.previous->angles.y-state.current_yaw);
            if(prior_torso>kBackwardBoundaryDegrees) prior_torso-=180.0;
            else if(prior_torso<-kBackwardBoundaryDegrees) prior_torso+=180.0;
            for(std::size_t i=0;i<state.transition_previous->controllers.size();++i)
                state.transition_previous->controllers[i]=control_byte(prior_torso/4.0,
                    model_profile->controller_start[i],model_profile->controller_end[i]);
            state.transition_time=input.current_server_seconds;
            state.transition_identity=input.current_record_identity;
        } else if(!continuous) {
            state.transition_previous.reset(); state.transition_identity=0U;
        }
        state.used=true; state.network=input.network_generation; state.map=input.map_generation;
        state.lifetime=input.entity_lifetime; state.model=input.model_resource_id; state.revision=input.model_revision;
        state.record=input.current_record_identity; state.ordinal=input.current_record_ordinal;
        state.main=*main; state.gait_sequence=*current.gait_sequence;
        state.current_origin={*current.origin.x,*current.origin.y,*current.origin.z};
        state.current_time=input.current_server_seconds;
        state.previous_time=continuous ? input.previous_server_seconds : input.current_server_seconds;
        state.previous_phase=start_phase; state.previous_yaw=start_yaw;
        state.moving=continuous && dt>0.0 && std::hypot(dx,dy)/dt>=kMinimumMovementSpeed;
        state.movement_yaw=state.moving ? std::atan2(dy,dx)*180.0/std::numbers::pi : aim;
        state.current_yaw=state.moving ? state.movement_yaw :
            ease_yaw(start_yaw,aim,continuous ? dt : 0.0);
        state.backwards=state.moving && std::abs(signed_angle(aim-state.movement_yaw))>kBackwardBoundaryDegrees;
        state.segment_movement=state.moving ? (state.backwards ? -distance : distance) : 0.0;
        const auto& gait=input.model.sequences[state.gait_sequence];
        const double phase_increment=continuous && state.gait_sequence!=0U ?
            gait_phase_increment(gait,dx,dy,pair ? *current.origin.z-*input.previous->origin.z : 0.0,dt,aim) : 0.0;
        state.current_phase=start_phase+phase_increment;
    }
    api::RemotePlayerPresentationIntent result;
    result.status=Status::ready; result.sample=main_sample(*main,input.model,input.sample_server_seconds);
    // Keep the committed animation anchor, but consume the engine's bounded
    // internally interpolated channels. HL player pitch replaces blend zero
    // and its four torso controls below; the secondary blend remains sampled.
    result.sample.controllers=sampled_controllers;
    result.sample.blending=sampled_blending;
    // Appearance is a discrete sampled endpoint, unlike the current sequence
    // clock. Do not jump its body/skin ahead of the engine's interpolation.
    result.sample.body=static_cast<std::int32_t>(*entity.body);
    result.sample.skin=static_cast<std::uint32_t>(*entity.skin);
    result.bone_count=model_profile->bone_count;
    const auto interval=state.current_time-state.previous_time;
    const auto fraction=interval>0.0 ?
        std::clamp((input.sample_server_seconds-state.previous_time)/interval,0.0,1.0) : 0.0;
    const auto aim=signed_angle(*entity.angles.y);
    auto yaw=state.moving ? state.movement_yaw :
        ease_yaw(state.previous_yaw,aim,std::max(0.0,input.sample_server_seconds-state.previous_time));
    double pitch=signed_angle(*entity.angles.x);
    result.transform_angles={pitch,aim,signed_angle(*entity.angles.z)};
    if(state.gait_sequence!=0U) {
        const auto& sequence=input.model.sequences[main->sequence];
        if(!player_pitch(result.sample,sequence,pitch)) return status(Status::invalid_sequence);
        auto torso=signed_angle(aim-yaw);
        if(torso>kBackwardBoundaryDegrees) {yaw-=180.0; torso-=180.0;}
        else if(torso<-kBackwardBoundaryDegrees) {yaw+=180.0; torso+=180.0;}
        for(std::size_t i=0;i<result.sample.controllers.size();++i)
            result.sample.controllers[i]=control_byte(torso/4.0,
                model_profile->controller_start[i],model_profile->controller_end[i]);
        result.transform_angles[0]=pitch; result.transform_angles[1]=signed_angle(yaw);
        auto gait=result.sample; gait.sequence=state.gait_sequence;
        const auto phase=state.previous_phase+(state.current_phase-state.previous_phase)*fraction;
        gait.frame_coordinate=normalized_gait_frame(input.model.sequences[gait.sequence],phase);
        result.gait_sample=gait; result.gait_bone_mask=model_profile->lower_mask;
    } else {
        // Explicit server gait zero (death/jump/non-gait activity), not a
        // synthetic substitute for a missing descriptor.
        for(std::size_t i=0;i<result.sample.controllers.size();++i)
            result.sample.controllers[i]=control_byte(0.0,
                model_profile->controller_start[i],model_profile->controller_end[i]);
    }
    result.gait_yaw_degrees=signed_angle(yaw);
    if(state.transition_previous) {
        const auto age=std::max(0.0,input.sample_server_seconds-state.transition_time);
        if(age<kTransitionSeconds) {
            result.previous_sample=state.transition_previous;
            result.previous_weight=1.0-age/kTransitionSeconds;
            result.transition_identity=state.transition_identity;
        }
    }
    return result;
}

} // namespace hlclient::games::halflife
