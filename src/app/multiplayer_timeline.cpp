#include <hlclient/app/multiplayer_timeline.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>

namespace hlclient::app {
namespace {
using namespace core;
using Event=MultiplayerDiagnosticEvent;
constexpr std::size_t producer_count=static_cast<std::size_t>(MultiplayerDiagnosticProducer::count);
template<class T> bool enum_valid(T value,T last) noexcept {return static_cast<unsigned>(value)<=static_cast<unsigned>(last);}
bool finite(double value) noexcept {return std::isfinite(value);}
template<class T> bool finite(const std::optional<T>& value) noexcept {
    if(!value)return true;
    if constexpr(std::is_arithmetic_v<T>)return finite(static_cast<double>(*value));
    else return std::ranges::all_of(*value,[](auto x){return finite(x);});
}
bool valid_reason(const std::array<char,48>& reason) noexcept {
    bool ended=false;
    for(const unsigned char c:reason) {
        if(c==0){ended=true;continue;}
        if(ended || !((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    }
    return ended;
}
bool valid(const Event& e) noexcept {
    if(!e.local_steady_ns || !e.identity.producer_event_id ||
        !enum_valid(e.identity.producer,MultiplayerDiagnosticProducer::ordinary_studio) ||
        !enum_valid(e.provenance,MultiplayerProvenance::user_report) ||
        !enum_valid(e.knowledge,MultiplayerKnowledge::cosmetic_feedback) ||
        !enum_valid(e.edge,MultiplayerEdgeConfidence::ambiguous) ||
        !enum_valid(e.server_time_domain,MultiplayerTimeDomain::presentation_seconds) ||
        !finite(e.server_seconds) || (e.server_seconds && e.server_time_domain==MultiplayerTimeDomain::unknown))return false;
    return std::visit([&](const auto& p) noexcept {
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,MultiplayerCommandPayload>) {
            return enum_valid(p.phase,MultiplayerCommandPhase::cancelled) &&
                finite(p.configured_interpolation_seconds) && finite(p.effective_interpolation_seconds) &&
                e.knowledge!=MultiplayerKnowledge::server_damage_observed && e.knowledge!=MultiplayerKnowledge::server_miss_observed;
        } else if constexpr(std::is_same_v<T,MultiplayerNetworkPayload>) {
            if(!enum_valid(p.phase,MultiplayerNetworkPhase::rejected) || p.inclusion_count>p.command_ids.size() ||
                (p.begin_ns && p.end_ns && *p.begin_ns>*p.end_ns) ||
                (p.phase==MultiplayerNetworkPhase::rollback && p.atomic_commit==true))return false;
            // Transport acknowledgement is never execution, fire, damage or miss.
            if(e.knowledge!=MultiplayerKnowledge::unknown && e.knowledge!=MultiplayerKnowledge::command_encoded &&
                e.knowledge!=MultiplayerKnowledge::command_sent && e.knowledge!=MultiplayerKnowledge::transport_acknowledged)return false;
            return e.knowledge!=MultiplayerKnowledge::transport_acknowledged || p.phase==MultiplayerNetworkPhase::transport_ack;
        } else if constexpr(std::is_same_v<T,MultiplayerActionPayload>) {
            return enum_valid(p.kind,MultiplayerActionKind::guidance) && enum_valid(p.phase,MultiplayerActionPhase::feedback) &&
                valid_reason(p.reason) && finite(p.next_attack_seconds) && finite(p.origin) && finite(p.direction) && finite(p.spread) &&
                finite(p.command_angles_degrees) && finite(p.server_punch_degrees) && finite(p.player_origin) && finite(p.player_velocity) &&
                finite(p.command_sample_seconds) && finite(p.action_started_seconds) && finite(p.source_received_steady_seconds) &&
                !(p.phase==MultiplayerActionPhase::feedback && (e.knowledge==MultiplayerKnowledge::server_damage_observed ||
                    e.knowledge==MultiplayerKnowledge::server_miss_observed));
        } else if constexpr(std::is_same_v<T,MultiplayerLifecyclePayload>) {
            return enum_valid(p.kind,MultiplayerLifecycleKind::damage) && finite(p.origin);
        } else if constexpr(std::is_same_v<T,MultiplayerPresentationPayload>) {
            return enum_valid(p.phase,MultiplayerPresentationPhase::feedback) && finite(p.previous_source_seconds) &&
                finite(p.current_source_seconds) && finite(p.sample_seconds) && finite(p.pose_seconds) && finite(p.alpha) &&
                finite(p.configured_delay_seconds) && finite(p.effective_delay_seconds) && finite(p.frame_ms) && finite(p.cpu_ms) &&
                finite(p.present_ms) && finite(p.gpu_ms) && finite(p.origin) && finite(p.angles) && finite(p.velocity) && finite(p.animation_frame) &&
                finite(p.raw_animation_frame) && finite(p.raw_animtime_seconds) && finite(p.raw_framerate) &&
                e.knowledge!=MultiplayerKnowledge::server_damage_observed && e.knowledge!=MultiplayerKnowledge::server_miss_observed;
        } else if constexpr(std::is_same_v<T,MultiplayerClockMappingPayload>) {
            return p.mapping_revision && p.local_anchor_ns && finite(p.server_anchor_seconds) && finite(p.uncertainty_seconds) && p.uncertainty_seconds>=0.;
        } else if constexpr(std::is_same_v<T,MultiplayerGeometryPayload>) {
            return enum_valid(p.kind,MultiplayerGeometryKind::world_blocker) && finite(p.origin) && finite(p.direction) && finite(p.endpoint) &&
                finite(p.camera_up) && finite(p.vertical_fov_radians) && finite(p.near_plane) && finite(p.far_plane) &&
                finite(p.fraction) && (!p.fraction || (*p.fraction>=0. && *p.fraction<=1.)) && finite(p.proxy_age_ms) &&
                e.knowledge!=MultiplayerKnowledge::server_damage_observed && e.knowledge!=MultiplayerKnowledge::server_miss_observed;
        } else if constexpr(std::is_same_v<T,MultiplayerInputPayload>) {
            return finite(p.delta_x) && finite(p.delta_y) && finite(p.wheel) &&
                (e.knowledge==MultiplayerKnowledge::unknown || e.knowledge==MultiplayerKnowledge::input_intent);
        } else if constexpr(std::is_same_v<T,MultiplayerMovementPayload>) {
            return enum_valid(p.phase,MultiplayerPresentationPhase::feedback) && finite(p.raw_server_origin) && finite(p.predicted_origin) &&
                finite(p.presented_eye) && finite(p.velocity) && finite(p.basevelocity) && finite(p.camera_base_angles) &&
                finite(p.server_punch_angles) && finite(p.local_punch_angles) && finite(p.camera_final_angles) &&
                finite(p.raw_correction_units) && finite(p.camera_jump_units) && finite(p.replay_duration_ms) &&
                finite(p.source_seconds) && finite(p.render_sample_seconds);
        } else {
            return finite(p.previous_raw_origin) && finite(p.current_raw_origin) && finite(p.latest_raw_origin) && finite(p.sampled_origin) &&
                finite(p.rendered_origin) && finite(p.previous_source_seconds) && finite(p.current_source_seconds) && finite(p.latest_source_seconds) &&
                finite(p.sample_seconds) && finite(p.alpha) && finite(p.support_offset_z) && p.decision<=7;
        }
    },e.payload);
}
std::uint8_t priority(const Event& e) noexcept {
    if(const auto* p=std::get_if<MultiplayerActionPayload>(&e.payload)) {
        // Remote/server feedback and repeated local cosmetics must not consume
        // the protected pool reserved for identified accepted local actions.
        if(e.knowledge==MultiplayerKnowledge::cosmetic_feedback ||
            e.knowledge==MultiplayerKnowledge::server_event_observed ||
            e.knowledge==MultiplayerKnowledge::server_damage_observed ||
            e.knowledge==MultiplayerKnowledge::server_miss_observed)return 0U;
        const bool identified=e.identity.action_id.value_or(0)!=0 && e.identity.command_id.value_or(0)!=0;
        return identified && (p->phase==MultiplayerActionPhase::accepted || p->phase==MultiplayerActionPhase::begin ||
            p->phase==MultiplayerActionPhase::end || p->phase==MultiplayerActionPhase::cancel)?2U:0U;
    }
    if(const auto* p=std::get_if<MultiplayerNetworkPayload>(&e.payload))return p->phase==MultiplayerNetworkPhase::rollback?2U:p->phase==MultiplayerNetworkPhase::packet_sent?1U:0U;
    if(const auto* p=std::get_if<MultiplayerCommandPayload>(&e.payload))return p->phase==MultiplayerCommandPhase::history_inserted?1U:0U;
    if(const auto* p=std::get_if<MultiplayerInputPayload>(&e.payload))return (p->key || p->mouse_button) && !p->repeat?1U:0U;
    if(const auto* p=std::get_if<MultiplayerMovementPayload>(&e.payload))return p->phase==MultiplayerPresentationPhase::correction?2U:0U;
    return std::holds_alternative<MultiplayerLifecyclePayload>(e.payload) || std::holds_alternative<MultiplayerClockMappingPayload>(e.payload) ||
        std::holds_alternative<MultiplayerGeometryPayload>(e.payload)?2U:0U;
}
bool same_event_identity(const Event& a,const Event& b) noexcept {
    return a.identity.producer==b.identity.producer && a.identity.producer_event_id==b.identity.producer_event_id &&
        a.identity.network_generation==b.identity.network_generation && a.identity.map_generation==b.identity.map_generation;
}
using EventIdentityKey=std::array<std::uint64_t,4>;
EventIdentityKey event_identity_key(const Event& e) noexcept {
    return {static_cast<std::uint64_t>(e.identity.producer),e.identity.producer_event_id,
        e.identity.network_generation,e.identity.map_generation};
}
std::uint64_t sample_key(const Event& e) noexcept {
    const auto phase=std::visit([](const auto& p)->std::uint64_t {
        if constexpr(requires{p.phase;})return static_cast<std::uint64_t>(p.phase);
        else if constexpr(requires{p.kind;})return static_cast<std::uint64_t>(p.kind);
        else return 0;
    },e.payload);
    auto key=static_cast<std::uint64_t>(e.identity.producer)+1U;
    key=key*31U+e.payload.index();key=key*31U+phase;
    key=key*65537U+e.identity.actor_entity.value_or(0);
    key=key*31U+e.identity.network_generation;key=key*31U+e.identity.map_generation;
    key=key*31U+e.identity.actor_visual_lifetime.value_or(0);
    if(const auto* p=std::get_if<MultiplayerActionPayload>(&e.payload))key=key*131U+p->weapon_id*8U+p->mode;
    return key?key:1;
}
template<class T> void number(std::string& out,T value) {
    if constexpr(std::is_same_v<T,bool>)out+=value?"true":"false";
    else {char buffer[64]{};const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
        if(result.ec==std::errc{})out.append(buffer,result.ptr);else out+="null";}
}
template<class T> void json(std::string& out,const T& value) {
    if constexpr(std::is_enum_v<T>)number(out,static_cast<unsigned>(value));
    else if constexpr(std::is_arithmetic_v<T>)number(out,value);
    else if constexpr(requires{value.has_value();}) {if(value)json(out,*value);else out+="null";}
    else {out+='[';bool first=true;for(const auto& v:value){if(!first)out+=',';first=false;json(out,v);}out+=']';}
}
template<class T> void field(std::string& out,std::string_view name,const T& value) {
    out+=",\"";out+=name;out+="\":";json(out,value);
}
std::string event_json(const Event& e) {
    std::string out;out.reserve(2048);out="{\"local_steady_ns\":";json(out,e.local_steady_ns);
    field(out,"server_seconds",e.server_seconds);field(out,"server_time_domain",e.server_time_domain);
    field(out,"provenance",e.provenance);field(out,"knowledge",e.knowledge);field(out,"edge",e.edge);
    out+=",\"identity\":{\"producer\":";json(out,e.identity.producer);
#define ID(name) field(out,#name,e.identity.name)
    ID(producer_event_id);ID(network_generation);ID(map_generation);ID(local_life);ID(actor_entity);ID(actor_occupant);
    ID(actor_visual_lifetime);ID(actor_model_revision);ID(input_sequence);ID(command_id);ID(action_id);ID(carrier_packet);
    ID(source_sequence);ID(source_record);ID(source_event);ID(simulation_iteration);ID(render_frame);ID(scene_revision);ID(pose_revision);
    ID(related_action_id);ID(related_actor_entity);ID(source_query_id);
    ID(source_record_ordinal);ID(source_message_bit_offset);ID(source_entry_ordinal);
#undef ID
    out+="},\"payload\":{\"type\":";json(out,e.payload.index());
    std::visit([&](const auto& p){
        using T=std::decay_t<decltype(p)>;
#define F(name) field(out,#name,p.name)
        if constexpr(std::is_same_v<T,MultiplayerCommandPayload>) {
            F(phase);F(angle_turns);F(movement);F(buttons);F(lerp_msec);F(msec);F(impulse);F(weapon_select);
            F(input_sample_ns);F(deadline_ns);F(build_ns);F(history_insert_ns);F(owner_time_ns);
            F(configured_interpolation_seconds);F(effective_interpolation_seconds);
        } else if constexpr(std::is_same_v<T,MultiplayerNetworkPayload>) {
            F(phase);F(begin_ns);F(end_ns);F(commit_ns);F(acknowledgement);F(reliable_generation);
            F(first_command_id);F(last_command_id);F(bytes);F(command_count);F(backup_count);F(record_count);F(reliable);F(atomic_commit);
            F(inclusion_count);out+=",\"command_ids\":[";
            for(std::size_t i=0;i<p.inclusion_count;++i){if(i)out+=',';json(out,p.command_ids[i]);}out+=']';
        } else if constexpr(std::is_same_v<T,MultiplayerActionPayload>) {
            F(kind);F(phase);F(weapon_id);F(mode);F(emitter_count);F(clip_before);F(clip_after);F(reserve_before);F(reserve_after);
            F(eligible);F(reloading);F(next_attack_seconds);F(origin);F(direction);F(spread);F(random_seed);
            F(command_angles_degrees);F(server_punch_degrees);F(player_origin);F(player_velocity);F(player_ducked);
            F(command_sample_seconds);F(action_started_seconds);
            F(source_received_steady_seconds);F(source_kind);F(source_resource_slot);F(source_flags);F(source_channel);
            F(suppressed_repetitions);
            out+=",\"reason\":\"";out.append(p.reason.data(),static_cast<std::size_t>(std::find(p.reason.begin(),p.reason.end(),'\0')-p.reason.begin()));out+='"';
        } else if constexpr(std::is_same_v<T,MultiplayerLifecyclePayload>) {
            F(kind);F(before);F(after);F(object_entity);F(object_lifetime);F(alive);F(visible);F(input_owned);
            F(model_index);F(object_profile);F(origin);F(owner_entity);
            F(omitted_objects);
        } else if constexpr(std::is_same_v<T,MultiplayerPresentationPayload>) {
            F(phase);F(previous_source_seconds);F(current_source_seconds);F(sample_seconds);F(pose_seconds);F(alpha);
            F(configured_delay_seconds);F(effective_delay_seconds);F(frame_ms);F(cpu_ms);F(present_ms);F(gpu_ms);
            F(origin);F(angles);F(velocity);F(sequence);F(animation_frame);F(held);F(clamped);F(visible);F(cache_hit);
            F(raw_animation_sequence);F(raw_animation_frame);F(raw_animtime_seconds);F(raw_framerate);
            F(raw_controllers);F(raw_blending);F(suppressed_repetitions);F(diagnostic_cache_evictions);
            F(viewport_width);F(viewport_height);F(framebuffer_width);F(framebuffer_height);
        } else if constexpr(std::is_same_v<T,MultiplayerClockMappingPayload>) {
            F(mapping_revision);F(local_anchor_ns);F(server_anchor_seconds);F(uncertainty_seconds);
        } else if constexpr(std::is_same_v<T,MultiplayerGeometryPayload>) {
            F(kind);F(origin);F(direction);F(endpoint);F(fraction);F(proxy_actor);F(blocker_entity);F(intersects);F(startsolid);F(allsolid);
            F(proxy_model_revision);F(world_query_steady_ns);
            F(viewport_width);F(viewport_height);F(framebuffer_width);F(framebuffer_height);
            F(proxy_age_ms);F(hitbox_group);F(exact_model_pose);F(world_blocked);F(query_budget_exhausted);F(query_incomplete);
            F(camera_up);F(vertical_fov_radians);F(near_plane);F(far_plane);
        } else if constexpr(std::is_same_v<T,MultiplayerInputPayload>) {
            F(event_type);F(key);F(mouse_button);F(delta_x);F(delta_y);F(wheel);F(repeat);
            F(sdl_timestamp_ns);F(dequeue_steady_ns);F(focused);F(captured);F(console_owned);F(hud_consumed);F(gameplay_routed);
        } else if constexpr(std::is_same_v<T,MultiplayerMovementPayload>) {
            F(phase);F(raw_server_origin);F(predicted_origin);F(presented_eye);F(velocity);F(basevelocity);
            F(camera_base_angles);F(server_punch_angles);F(local_punch_angles);F(camera_final_angles);
            F(raw_correction_units);F(camera_jump_units);F(replay_duration_ms);F(source_seconds);F(render_sample_seconds);
            F(support_entity);F(support_model);F(hull);F(prediction_active);F(grounded);F(startsolid);F(allsolid);
        } else {
            F(previous_raw_origin);F(current_raw_origin);F(latest_raw_origin);F(sampled_origin);F(rendered_origin);
            F(previous_source_seconds);F(current_source_seconds);F(latest_source_seconds);F(sample_seconds);F(alpha);F(support_offset_z);
            F(previous_source_ordinal);F(current_source_ordinal);F(latest_source_ordinal);F(model_index);F(selected_main);F(selected_gait);F(selected_weapon);
            F(support_entity);F(support_model);F(pose_submitted);F(visible);F(decision);F(floor_reasons);F(new_boundary_reasons);F(hold_reasons);F(clock_reasons);
        }
#undef F
    },e.payload);out+="}}";return out;
}
} // namespace

struct MultiplayerTimeline::Storage final {
    struct ProducerState {std::uint64_t network{},map{},last_id{};};
    struct SampleState {std::uint64_t key{},last_ns{};};
    struct Coverage {std::uint32_t weapon{},mode{};std::uint8_t kind{},phase{};std::uint64_t observations{};};
    struct Window {
        std::uint64_t trigger{},begin{},end{},last_background{},omissions{};
        std::array<Event,maximum_manual_events> events{};
        std::array<std::uint8_t,maximum_manual_events> exact{};
        std::size_t count{};
    };
    std::array<Event,maximum_recent_events> recent{};
    std::array<Event,maximum_exact_events> exact_events{};
    // F8-only membership scratch lives in the already heap-owned timeline,
    // not on the Win32 action/F8 stack. It has no retained evidence semantics.
    std::array<EventIdentityKey,maximum_exact_events> protected_keys{};
    std::array<Window,maximum_manual_windows> manual{};
    std::array<ProducerState,producer_count> producers{};
    std::array<SampleState,256> samples{};
    std::array<Coverage,256> coverage{};
    std::size_t recent_first{},recent_count{},exact_first{},exact_count{},chain_first{},chain_count{},manual_count{},coverage_count{};
    std::uint64_t origin_ns{},latest_ns{};
    MultiplayerTimelineStatistics stats;
    void append_window(Window& window,const Event& e,const std::uint8_t is_exact) noexcept {
        if(e.local_steady_ns<window.begin || e.local_steady_ns>window.end)return;
        for(std::size_t i=0;i<window.count;++i)if(same_event_identity(window.events[i],e))return;
        if(!is_exact && window.last_background && e.local_steady_ns>=window.last_background &&
            e.local_steady_ns-window.last_background<manual_interval_ns){++window.omissions;++stats.manual_omissions;return;}
        if(window.count==maximum_manual_events) {
            const auto distance=[&](std::uint64_t stamp){return stamp>window.trigger?stamp-window.trigger:window.trigger-stamp;};
            // The worst retained observation has the lowest priority, then
            // greatest marker distance. Existing equal-distance ties win over
            // a new observation; within the retained tie prefer the older one.
            std::size_t victim=0;
            for(std::size_t i=1;i<window.count;++i)
                if(window.exact[i]<window.exact[victim] || (window.exact[i]==window.exact[victim] &&
                    distance(window.events[i].local_steady_ns)>=distance(window.events[victim].local_steady_ns)))victim=i;
            ++window.omissions;++stats.manual_omissions;
            if(window.exact[victim]>is_exact || (window.exact[victim]==is_exact &&
                distance(e.local_steady_ns)>=distance(window.events[victim].local_steady_ns)))return;
            for(std::size_t i=victim;i+1<window.count;++i){window.events[i]=window.events[i+1];window.exact[i]=window.exact[i+1];}
            --window.count;
        }
        window.events[window.count]=e;window.exact[window.count]=is_exact;++window.count;
        if(!is_exact)window.last_background=e.local_steady_ns;
    }
};
MultiplayerTimeline::MultiplayerTimeline(const bool enabled) noexcept:requested_{enabled} {
    static_assert(sizeof(Storage)<=maximum_capacity_bytes);
    if(enabled)try{storage_=std::make_unique<Storage>();}catch(...){allocation_failures_=1;}
}
MultiplayerTimeline::~MultiplayerTimeline()=default;
bool MultiplayerTimeline::enabled() const noexcept {return storage_!=nullptr;}
void MultiplayerTimeline::set_clock_origin_ns(const std::uint64_t origin) noexcept {
    if(storage_ && !storage_->origin_ns)storage_->origin_ns=origin;
}
std::size_t MultiplayerTimeline::capacity_bytes() const noexcept {return storage_?sizeof(Storage):0;}
MultiplayerTimelineStatistics MultiplayerTimeline::statistics() const noexcept {
    auto result=storage_?storage_->stats:MultiplayerTimelineStatistics{};result.allocation_failures+=allocation_failures_;return result;
}
bool MultiplayerTimeline::observe(const Event& e) noexcept {
    if(!storage_)return false;
    auto& s=*storage_;++s.stats.observed;
    if(!valid(e)){++s.stats.invalid;return false;}
    const auto producer=static_cast<std::size_t>(e.identity.producer);auto& state=s.producers[producer];
    if(e.identity.network_generation<state.network ||
        (e.identity.network_generation==state.network && e.identity.map_generation<state.map)){++s.stats.stale_generation;return false;}
    if(e.identity.network_generation==state.network && e.identity.map_generation==state.map && e.identity.producer_event_id<=state.last_id)
        {++s.stats.duplicates;return false;}
    state={e.identity.network_generation,e.identity.map_generation,e.identity.producer_event_id};
    ++s.stats.accepted;++s.stats.payload_observations[e.payload.index()];++s.stats.producer_observations[producer];
    s.latest_ns=std::max(s.latest_ns,e.local_steady_ns);
    if(const auto* p=std::get_if<MultiplayerActionPayload>(&e.payload)) {
        auto index=s.coverage_count;
        for(std::size_t i=0;i<s.coverage_count;++i)if(s.coverage[i].weapon==p->weapon_id && s.coverage[i].mode==p->mode &&
            s.coverage[i].kind==static_cast<std::uint8_t>(p->kind) && s.coverage[i].phase==static_cast<std::uint8_t>(p->phase)){index=i;break;}
        if(index<s.coverage_count)++s.coverage[index].observations;
        else if(index<s.coverage.size())s.coverage[s.coverage_count++]={p->weapon_id,p->mode,static_cast<std::uint8_t>(p->kind),static_cast<std::uint8_t>(p->phase),1};
        else ++s.stats.coverage_omissions;
    }
    const auto is_exact=priority(e);
    if(is_exact) {
        auto& count=is_exact==2?s.exact_count:s.chain_count;auto& first=is_exact==2?s.exact_first:s.chain_first;
        const auto capacity=is_exact==2?maximum_action_exact_events:maximum_chain_exact_events;
        const auto offset=is_exact==2?0U:maximum_action_exact_events;
        if(count==capacity){first=(first+1)%capacity;--count;++s.stats.exact_evictions;}
        s.exact_events[offset+(first+count)%capacity]=e;++count;
    }
    for(std::size_t i=0;i<s.manual_count;++i)s.append_window(s.manual[i],e,is_exact);
    if(!is_exact) {
        const auto key=sample_key(e);auto& sample=s.samples[key%s.samples.size()];
        if(sample.key==key && e.local_steady_ns>=sample.last_ns && e.local_steady_ns-sample.last_ns<background_interval_ns)
            {++s.stats.sampled_out;return true;}
        sample={key,e.local_steady_ns};
    }
    // Latest observed local time defines retention, not a server clock. Late
    // producer observations remain valid facts but cannot lengthen this ring.
    while(s.recent_count && s.latest_ns>=s.recent[s.recent_first].local_steady_ns &&
        s.latest_ns-s.recent[s.recent_first].local_steady_ns>pre_window_ns) {
        s.recent_first=(s.recent_first+1)%maximum_recent_events;--s.recent_count;++s.stats.recent_evictions;
    }
    if(s.recent_count==maximum_recent_events){s.recent_first=(s.recent_first+1)%maximum_recent_events;--s.recent_count;++s.stats.recent_evictions;}
    s.recent[(s.recent_first+s.recent_count)%maximum_recent_events]=e;++s.recent_count;return true;
}
bool MultiplayerTimeline::mark_manual_relative_us(const std::uint64_t time) noexcept {
    if(!storage_)return false;auto& s=*storage_;
    if(!s.origin_ns || time>((std::numeric_limits<std::uint64_t>::max)()-s.origin_ns)/1000U || s.manual_count==maximum_manual_windows)
        {++s.stats.manual_marks_dropped;return false;}
    const auto trigger=s.origin_ns+time*1000U;
    if(trigger>(std::numeric_limits<std::uint64_t>::max)()-post_window_ns ||
        (s.manual_count && trigger<s.manual[s.manual_count-1].trigger)){++s.stats.manual_marks_dropped;return false;}
    auto& window=s.manual[s.manual_count++];window.trigger=trigger;window.begin=trigger>pre_window_ns?trigger-pre_window_ns:0;
    window.end=trigger+post_window_ns;++s.stats.manual_marks;
    std::size_t protected_count{};
    for(std::size_t i=0;i<s.exact_count;++i) {
        const auto& e=s.exact_events[(s.exact_first+i)%maximum_action_exact_events];
        s.append_window(window,e,2U);s.protected_keys[protected_count++]=event_identity_key(e);
    }
    for(std::size_t i=0;i<s.chain_count;++i) {
        const auto& e=s.exact_events[maximum_action_exact_events+(s.chain_first+i)%maximum_chain_exact_events];
        s.append_window(window,e,1U);s.protected_keys[protected_count++]=event_identity_key(e);
    }
    std::sort(s.protected_keys.begin(),s.protected_keys.begin()+protected_count);
    for(std::size_t i=0;i<s.recent_count;++i) {
        const auto& e=s.recent[(s.recent_first+i)%maximum_recent_events];
        // Skip copies even if that protected event was itself omitted by the
        // 64-event window cap, so duplicates cannot inflate loss counters.
        const bool protected_copy=std::binary_search(s.protected_keys.begin(),
            s.protected_keys.begin()+protected_count,event_identity_key(e));
        if(!protected_copy)s.append_window(window,e,priority(e));
    }
    return true;
}
std::optional<std::string> MultiplayerTimeline::encode(const std::size_t requested_bytes) const {
    const auto limit=std::min(requested_bytes,maximum_encoded_bytes);if(limit<65'536)return {};
    try {
        if(!enabled()) {
            std::string out="{\"schema\":\"hlclient.multiplayer-timeline.v1\",\"requested\":";
            json(out,requested_);out+=",\"enabled\":false,\"availability\":\"";
            out+=allocation_failures_?"allocation_failed":"disabled";
            out+="\",\"transport_ack_proves_execution\":false";
            if(allocation_failures_)field(out,"allocation_failures",allocation_failures_);
            out+='}';return out;
        }
        const auto start=std::chrono::steady_clock::now();const auto stats=statistics();
        std::string out;out.reserve(limit);out="{\"schema\":\"hlclient.multiplayer-timeline.v1\",\"requested\":";
        json(out,requested_);field(out,"enabled",enabled());
        out+=",\"local_clock\":\"steady_epoch_nanoseconds\",\"run_identity\":\"enclosing_external_movement\"";
        out+=",\"server_outcome_default\":\"unknown\",\"transport_ack_proves_execution\":false,\"proxy_proves_hit\":false";
        field(out,"application_steady_origin_ns",storage_?storage_->origin_ns:0U);
        out+=",\"limits\":{\"memory_bytes\":";json(out,maximum_capacity_bytes);field(out,"encoded_bytes",limit);
        field(out,"recent_events",maximum_recent_events);field(out,"exact_events",maximum_exact_events);
        field(out,"action_exact_events",maximum_action_exact_events);field(out,"command_carrier_exact_events",maximum_chain_exact_events);
        field(out,"manual_windows",maximum_manual_windows);field(out,"manual_events",maximum_manual_events);
        field(out,"manual_pre_ns",pre_window_ns);field(out,"manual_post_ns",post_window_ns);
        field(out,"background_interval_ns",background_interval_ns);field(out,"manual_interval_ns",manual_interval_ns);
        field(out,"encode_deadline_ms",encoding_deadline_ms);out+='}';
        out+=",\"statistics\":{\"observed\":";json(out,stats.observed);
#define S(name) field(out,#name,stats.name)
        S(accepted);S(invalid);S(duplicates);S(stale_generation);S(sampled_out);S(recent_evictions);S(exact_evictions);
        S(manual_omissions);S(manual_marks);S(manual_marks_dropped);S(allocation_failures);S(coverage_omissions);
        S(payload_observations);S(producer_observations);
#undef S
        field(out,"capacity_bytes",capacity_bytes());out+='}';
        std::uint64_t omitted{};bool deadline=false;
        const auto append=[&](const Event& e,bool& comma) {
            if(deadline || std::chrono::steady_clock::now()-start>std::chrono::milliseconds{encoding_deadline_ms}){deadline=true;++omitted;return;}
            const auto encoded=event_json(e);
            if(out.size()+encoded.size()+65'536U>limit){++omitted;return;}
            if(comma)out+=',';out+=encoded;comma=true;
        };
        out+=",\"manual_windows\":[";
        if(storage_)for(std::size_t i=0;i<storage_->manual_count;++i) {
            if(i)out+=',';const auto& window=storage_->manual[i];out+="{\"trigger_ns\":";json(out,window.trigger);
            field(out,"begin_ns",window.begin);field(out,"end_ns",window.end);field(out,"observed_through_ns",storage_->latest_ns);
            field(out,"post_complete",storage_->latest_ns>=window.end);field(out,"omitted",window.omissions);
            std::optional<std::uint64_t> first_stamp,last_stamp;
            for(std::size_t j=0;j<window.count;++j){const auto stamp=window.events[j].local_steady_ns;
                first_stamp=first_stamp?std::min(*first_stamp,stamp):stamp;last_stamp=last_stamp?std::max(*last_stamp,stamp):stamp;}
            field(out,"retained_begin_ns",first_stamp);field(out,"retained_end_ns",last_stamp);
            out+=",\"events\":[";
            bool comma=false;for(std::size_t j=0;j<window.count;++j)append(window.events[j],comma);out+="]}";
        }
        out+="],\"exact_transitions\":[";
        if(storage_){bool comma=false;
            for(std::size_t i=0;i<storage_->exact_count;++i)append(storage_->exact_events[(storage_->exact_first+i)%maximum_action_exact_events],comma);
            for(std::size_t i=0;i<storage_->chain_count;++i)append(storage_->exact_events[maximum_action_exact_events+(storage_->chain_first+i)%maximum_chain_exact_events],comma);}
        out+="],\"recent_events\":[";
        if(storage_){bool comma=false;for(std::size_t i=0;i<storage_->recent_count;++i)append(storage_->recent[(storage_->recent_first+i)%maximum_recent_events],comma);}
        out+="],\"action_coverage\":[";
        if(storage_)for(std::size_t i=0;i<storage_->coverage_count;++i) {
            if(i)out+=',';const auto& row=storage_->coverage[i];out+="{\"weapon_id\":";json(out,row.weapon);
            field(out,"mode",row.mode);field(out,"kind",row.kind);field(out,"phase",row.phase);field(out,"observations",row.observations);out+='}';
        }
        out+=']';field(out,"encoded_events_omitted",omitted);field(out,"encoding_deadline_reached",deadline);
        field(out,"encoding_elapsed_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());out+='}';
        if(out.size()>limit)return {};return out;
    }catch(...){return {};}
}
} // namespace hlclient::app
