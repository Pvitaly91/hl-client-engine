#include <hlclient/games/halflife/sprite_hud.hpp>
#include <hlclient/games/halflife/hud_resource_profile.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace hlclient::games::halflife {
namespace {
using namespace game_api;
constexpr std::array<float,4U> yellow{1.0F,160.0F/255.0F,0.0F,1.0F};
constexpr std::array<float,4U> red{250.0F/255.0F,0.0F,0.0F,1.0F};
constexpr std::array<std::string_view,8U> damage_roles{
    "dmg_poison","dmg_chem","dmg_cold","dmg_drown","dmg_heat","dmg_gas","dmg_rad","dmg_shock"};
constexpr std::array<std::uint32_t,8U> damage_bits{
    1U<<17U,1U<<20U,(1U<<4U)|(1U<<22U),1U<<14U,
    (1U<<3U)|(1U<<21U),1U<<16U,1U<<18U,1U<<8U};
bool usable(const HudResourceEntry* entry) noexcept {
    return entry && entry->status==HudResourceStatus::ready && entry->resource_id &&
        entry->resource_revision && entry->source.width && entry->source.height &&
        entry->source.x<=entry->frame_width && entry->source.y<=entry->frame_height &&
        entry->source.width<=entry->frame_width-entry->source.x &&
        entry->source.height<=entry->frame_height-entry->source.y;
}
bool suit(const client::RuntimeClientObservationState& state) noexcept {
    return state.receiving_client && state.receiving_client->owned_weapon_bits &&
        ((*state.receiving_client->owned_weapon_bits & (1U<<31U))!=0U);
}
bool alive(const client::RuntimeClientObservationState& state) noexcept {
    return !state.lifecycle.dead() && (!state.receiving_client || !state.receiving_client->health ||
        *state.receiving_client->health>0.0);
}
bool after_hud_floors(const client::RuntimeClientObservationState& state,
    const std::optional<client::RuntimeObservationSource>& source) noexcept {
    if(state.lifecycle.last_death_source && (!source ||
        source->record_ordinal<=state.lifecycle.last_death_source->record_ordinal))return false;
    if(state.weapon_hud.hud_reset_source) {
        const auto& floor=*state.weapon_hud.hud_reset_source;
        if(!source || source->record_ordinal<floor.record_ordinal ||
            (source->record_ordinal==floor.record_ordinal && source->start_bit_offset<floor.end_bit_offset))return false;
    }
    return true;
}
bool fresh_hud_binding(const client::RuntimeClientObservationState& state) noexcept {
    if(after_hud_floors(state,state.weapon_hud.active_source))return true;
    // A fresh no-weapon client publication may legitimately choose from its
    // authoritative inventory before the initial CurWeapon arrives.
    return (!state.weapon_hud.active_weapon_id || *state.weapon_hud.active_weapon_id==0U) &&
        state.client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record &&
        after_hud_floors(state,state.client_metadata.source);
}
std::array<float,4U> brightness(std::array<float,4U> color,float value) noexcept {
    for(std::size_t i=0;i<3U;++i)color[i]*=std::clamp(value,0.0F,1.0F);
    return color;
}
void image(HudState& hud,const HudResourceEntry* entry,float x,float y,
    std::array<float,4U> color,std::uint32_t order=10U,
    std::optional<HudSourceRectangle> crop={}) {
    if(!usable(entry) || hud.draw.sprites.size()>=kMaximumHudSprites)return;
    const auto source=crop.value_or(entry->source);
    if(!source.width || !source.height)return;
    hud.draw.sprites.push_back({entry->resource_id,entry->resource_revision,entry->frame,source,
        x,y,static_cast<float>(source.width),static_cast<float>(source.height),color,
        entry->blend,{},order});
}
void rectangle(HudState& hud,float x,float y,float w,float h,std::array<float,4U> color,
    std::uint32_t order=11U) {
    if(w<=0 || h<=0 || x<0 || y<0 || hud.draw.rectangles.size()>=kMaximumHudRectangles)return;
    hud.draw.rectangles.push_back({x,y,w,h,color,order});
}
const client::RuntimeWeaponTypeObservation* weapon(const client::RuntimeClientObservationState& state,
    std::uint8_t id) noexcept {
    const auto found=std::find_if(state.weapon_hud.catalogue.begin(),state.weapon_hud.catalogue.end(),
        [=](const auto& item){return item.id==id;});
    return found==state.weapon_hud.catalogue.end() ? nullptr : &*found;
}
unsigned reserve(const client::RuntimeClientObservationState& state,std::int8_t index) noexcept {
    return index>=0 && static_cast<std::size_t>(index)<state.weapon_hud.reserve_ammo.size()
        ? state.weapon_hud.reserve_ammo[static_cast<std::size_t>(index)].value_or(0U) : 0U;
}
bool owned(const client::RuntimeClientObservationState& state,std::uint8_t id) noexcept {
    return id>0 && id<32 && state.receiving_client && state.receiving_client->owned_weapon_bits &&
        ((*state.receiving_client->owned_weapon_bits>>id)&1U)!=0U;
}
struct PixelLayout final {
    HudDrawCommands& draw;
    const std::size_t sprites,rectangles,texts;
    const float scale,width,height;
    ~PixelLayout() {
        for(std::size_t i=sprites;i<draw.sprites.size();++i) {
            auto& item=draw.sprites[i];item.x*=scale;item.y*=scale;
            item.width*=scale;item.height*=scale;
            if(item.draw_order==20U) { // Reticle anchor is actual gameplay center.
                // Nearest physical pixel, with exact half-pixel ties toward
                // the legacy lower anchor. Flooring fractional scaled sizes
                // otherwise biases aim by up to almost one whole pixel.
                item.x=std::ceil((width-item.width)*.5F-.5F);
                item.y=std::ceil((height-item.height)*.5F-.5F);
            }
            if(item.clip) {item.clip->x*=scale;item.clip->y*=scale;
                item.clip->width*=scale;item.clip->height*=scale;}
        }
        for(std::size_t i=rectangles;i<draw.rectangles.size();++i) {
            auto& item=draw.rectangles[i];item.x*=scale;item.y*=scale;
            item.width*=scale;item.height*=scale;
        }
        for(std::size_t i=texts;i<draw.texts.size();++i) {
            auto& item=draw.texts[i];item.x*=scale;item.y*=scale;
            item.pixel_size*=scale;item.advance*=scale;item.line_height*=scale;
        }
    }
};
}

float halflife_hud_user_scale(HudPresentationContext context) noexcept {
    return !context.hud_scale_automatic && std::isfinite(context.hud_scale) &&
        context.hud_scale>=.5F && context.hud_scale<=2.0F ? context.hud_scale : 1.0F;
}
float halflife_hud_automatic_scale(HudPresentationContext context,
    const HudResourceDescriptor* descriptor,std::uint32_t tier) noexcept {
    if(!descriptor || tier<=640U || context.viewport_height<=720U)return 1.0F;
    const HudResourceEntry* reference=nullptr;const HudResourceEntry* selected=nullptr;
    for(const auto& entry:descriptor->entries)if(entry.role=="number_0" && usable(&entry)) {
        if(entry.resolution==640U)reference=&entry;
        if(entry.resolution==tier)selected=&entry;
    }
    if(!reference || !selected)return 1.0F;
    // Keep the approved high-resolution crop. The24px original640 digit is
    // the measured reference at720p;1080p is36px, not the native48px double
    // tier jump. This is a compact local-compatible layout, not stock parity.
    const float ratio=static_cast<float>(reference->source.height)/selected->source.height;
    return std::clamp(ratio*std::clamp(static_cast<float>(context.viewport_height)/720.0F,1.0F,2.0F),.25F,2.0F);
}

void HalfLifeSpriteHud::reset() noexcept { *this=HalfLifeSpriteHud{}; }
void HalfLifeSpriteHud::bind(const HudResourceSet& resources) { resources_=resources; }
void HalfLifeSpriteHud::selection_result(std::uint8_t id,bool queued,double now) noexcept {
    if(!pending_ || *pending_!=id || !pending_started_ || !std::isfinite(now) || now<*pending_started_ ||
        now>*pending_started_+0.25)return;
    if(queued) {pending_queued_=true;selection_status_="queued";}
    else {
        pending_.reset();pending_started_.reset();pending_source_.reset();pending_pre_active_.reset();pending_queued_=false;
        selection_status_="request_rejected";
    }
}
HudMenuResult HalfLifeSpriteHud::block_selection(const HudMenuInput& input) noexcept {
    HudMenuResult result;result.cancel_consumed=input.cancel && menu_open_;
    result.consume_primary=consume_confirmation_hold(input);
    menu_open_=false;candidate_.reset();menu_group_.reset();pending_.reset();pending_started_.reset();
    pending_source_.reset();pending_pre_active_.reset();pending_queued_=false;cue_count_=0;
    selection_status_="switch_blocked";
    // A primed action's physical held/release domain belongs to gameplay.
    // Closing this UI must never manufacture a grenade throw release.
    return result;
}
bool HalfLifeSpriteHud::consume_confirmation_hold(const HudMenuInput& input) noexcept {
    // A menu-spent physical press stays UI-owned until KeyUp. Stock's
    // CL_ResetButtonBits clears attack state without clearing its down-key
    // owner; repeated KeyDown/held frames cannot reassert attack before release.
    if(input.primary_released || (!input.primary_held && !input.primary_pressed))
        primary_confirmation_held_=false;
    return primary_confirmation_held_;
}
std::span<const LocalSoundReference> HalfLifeSpriteHud::sounds() noexcept {
    static const auto references=[] {
        constexpr std::array<std::string_view,4U> names{"common/wpn_hudon.wav",
            "common/wpn_moveselect.wav","common/wpn_select.wav","common/wpn_hudoff.wav"};
        std::array<LocalSoundReference,4U> out{};
        for(std::size_t i=0;i<out.size();++i) {
            out[i].source=SoundReferenceSource::module_profile_dependency;
            std::copy(names[i].begin(),names[i].end(),out[i].sample.begin());
        }return out;
    }();return references;
}
void HalfLifeSpriteHud::sound(std::size_t index,double now) noexcept {
    if(index>=sounds().size() || cue_count_==cues_.size() || !std::isfinite(now) || now<0)return;
    auto& cue=cues_[cue_count_++];cue={};cue.reference=sounds()[index];
    cue.kind=LocalSoundKind::hud_menu;cue.channel=LocalSoundChannel::automatic;
    cue.scheduled_seconds=now;cue.marker_ordinal=static_cast<std::uint32_t>(++ui_ordinal_);
    cue.restart=ui_ordinal_; // local menu event, never a remote emitter
}
void HalfLifeSpriteHud::append_audio(LocalAudioBatch& batch) noexcept {
    for(std::size_t i=0;i<cue_count_ && batch.count<batch.cues.size();++i) {
        auto cue=cues_[i];cue.scope=batch.scope;batch.cues[batch.count++]=cue;
    }
    cue_count_=0;
}
const HudResourceDescriptor* HalfLifeSpriteHud::descriptor(std::string_view name) const noexcept {
    const auto found=std::find_if(resources_.descriptors.begin(),resources_.descriptors.end(),
        [=](const auto& entry){return entry.name==name;});
    return found==resources_.descriptors.end() ? nullptr : &*found;
}
std::uint32_t HalfLifeSpriteHud::preferred_tier(HudPresentationContext c) noexcept {
    if(c.viewport_width>2560U && c.viewport_height>1600U)return 2560U;
    if(c.viewport_width>=1280U && c.viewport_height>720U)return 1280U;
    return c.viewport_width>=640U ? 640U : 320U;
}
std::uint32_t HalfLifeSpriteHud::tier(const HudResourceDescriptor* data,HudPresentationContext c) const noexcept {
    if(!data)return 0U;
    const auto desired=preferred_tier(c);std::uint32_t lower=0U,first=0U;
    for(const auto& entry:data->entries)if(usable(&entry)) {
        if(!first || entry.resolution<first)first=entry.resolution;
        if(entry.resolution<=desired && entry.resolution>lower)lower=entry.resolution;
    }
    if(lower || first)return lower ? lower : first;
    // Preserve a typed pending/missing/invalid role when no decoded tier is
    // ready. A valid declared role is not an intentional absence.
    for(const auto& entry:data->entries)if(entry.resolution) {
        if(!first || entry.resolution<first)first=entry.resolution;
        if(entry.resolution<=desired && entry.resolution>lower)lower=entry.resolution;
    }
    return lower ? lower : first;
}
const HudResourceEntry* HalfLifeSpriteHud::role(const HudResourceDescriptor* data,
    std::string_view name,std::uint32_t resolution) const noexcept {
    if(!data)return nullptr;
    const auto found=std::find_if(data->entries.begin(),data->entries.end(),[=](const auto& entry){
        return entry.role==name && entry.resolution==resolution;});
    return found==data->entries.end() ? nullptr : &*found;
}
bool HalfLifeSpriteHud::has_ammo(const client::RuntimeClientObservationState& state,
    const client::RuntimeWeaponTypeObservation& type) noexcept {
    if(type.id==11U && hornet_command(type.command_name))return true; // server recharge/select-on-empty
    if(type.primary_maximum_ammo==-1 || type.primary_ammo_type==-1 || (type.flags&1U)!=0U)return true;
    const auto clip=type.id<state.weapon_hud.clips.size() ? state.weapon_hud.clips[type.id] : std::nullopt;
    return clip.value_or(0)>0 || reserve(state,type.primary_ammo_type)>0 || reserve(state,type.secondary_ammo_type)>0;
}
void HalfLifeSpriteHud::observe_active_weapon(const client::RuntimeWeaponHudObservation& hud,
    const client::LocalPlayerLifecycle& life) noexcept {
    const auto& source=hud.active_source;
    if(life.dead() || !source || !source->record_identity ||
        (life.last_death_source && source->record_ordinal<=life.last_death_source->record_ordinal) ||
        (hud.hud_reset_source && (source->record_ordinal<hud.hud_reset_source->record_ordinal ||
            (source->record_ordinal==hud.hud_reset_source->record_ordinal &&
             source->start_bit_offset<hud.hud_reset_source->end_bit_offset))) ||
        (confirmed_weapon_source_ && (source->record_ordinal<confirmed_weapon_source_->record_ordinal ||
            (source->record_ordinal==confirmed_weapon_source_->record_ordinal &&
             source->end_bit_offset<=confirmed_weapon_source_->end_bit_offset))))return;
    confirmed_weapon_source_=source;
    if(!hud.active_weapon_id || !*hud.active_weapon_id || *hud.active_weapon_id>=32U)return;
    if(confirmed_weapon_ && confirmed_weapon_!=hud.active_weapon_id)previous_weapon_=confirmed_weapon_;
    confirmed_weapon_=hud.active_weapon_id;
}
void HalfLifeSpriteHud::observe_boundary(const client::RuntimeClientObservationState& state) noexcept {
    const bool reset=(generation_ && generation_!=state.generation) || life_!=state.lifecycle.life_epoch ||
        deaths_!=state.lifecycle.deaths || state.lifecycle.dead() ||
        (state.weapon_hud.hud_reset_source && reset_source_!=state.weapon_hud.hud_reset_source);
    if(reset) {
        primary_confirmation_held_=false;
        menu_open_=false;candidate_.reset();pending_.reset();pending_started_.reset();pending_source_.reset();pending_pre_active_.reset();menu_group_.reset();
        pending_queued_=false;selection_status_.clear();
        confirmed_weapon_.reset();previous_weapon_.reset();confirmed_weapon_source_.reset();
        history_={};damage_expiry_={};cue_count_=0;
        last_health_.reset();last_armor_.reset();last_clip_.reset();last_reserve_.reset();last_weapon_.reset();
    }
    generation_=state.generation;life_=state.lifecycle.life_epoch;deaths_=state.lifecycle.deaths;
    reset_source_=state.weapon_hud.hud_reset_source;
    observe_active_weapon(state.weapon_hud,state.lifecycle);
    if(previous_weapon_ && !owned(state,*previous_weapon_))previous_weapon_.reset();
    if(candidate_) {
        const auto* type=weapon(state,*candidate_);
        if(!owned(state,*candidate_) || !type || !has_ammo(state,*type))candidate_.reset();
    }
    const bool fresh_active=pending_source_ && state.weapon_hud.active_source &&
        (state.weapon_hud.active_source->record_ordinal>pending_source_->record_ordinal ||
         (state.weapon_hud.active_source->record_ordinal==pending_source_->record_ordinal &&
          state.weapon_hud.active_source->end_bit_offset>pending_source_->end_bit_offset));
    const bool actual_new_active=fresh_active && state.weapon_hud.active_weapon_id!=pending_pre_active_;
    const bool confirmed=pending_ && pending_queued_ && fresh_active && state.weapon_hud.active_weapon_id==pending_;
    if(pending_ && (!owned(state,*pending_) || confirmed || (pending_queued_ && actual_new_active))) {
        selection_status_=pending_queued_ && fresh_active && state.weapon_hud.active_weapon_id==pending_
            ? "server_confirmed" : "server_selection_changed";
        pending_.reset();pending_started_.reset();pending_source_.reset();pending_pre_active_.reset();pending_queued_=false;
    }
    if(!alive(state) || !fresh_hud_binding(state) || (state.weapon_hud.hide_flags.value_or(0U)&5U)!=0U) {
        menu_open_=false;candidate_.reset();menu_group_.reset();
        if(!fresh_hud_binding(state)) {
            pending_.reset();pending_started_.reset();pending_source_.reset();pending_pre_active_.reset();pending_queued_=false;
            selection_status_.clear();
        }
    }
}
void HalfLifeSpriteHud::committed(const GameRecordState& state) noexcept {
    if(life_!=state.lifecycle.life_epoch || deaths_!=state.lifecycle.deaths || state.lifecycle.dead() ||
        (state.weapon_hud.hud_reset_source && reset_source_!=state.weapon_hud.hud_reset_source)) {
        primary_confirmation_held_=false;
        history_={};damage_expiry_={};damage_high_water_.reset();menu_open_=false;candidate_.reset();pending_.reset();
        pending_started_.reset();pending_source_.reset();pending_pre_active_.reset();menu_group_.reset();
        pending_queued_=false;selection_status_.clear();
        confirmed_weapon_.reset();previous_weapon_.reset();confirmed_weapon_source_.reset();
        cue_count_=0;
        last_health_.reset();last_armor_.reset();last_clip_.reset();last_reserve_.reset();last_weapon_.reset();
    }
    life_=state.lifecycle.life_epoch;deaths_=state.lifecycle.deaths;reset_source_=state.weapon_hud.hud_reset_source;
    if(state.lifecycle.dead())return;
    observe_active_weapon(state.weapon_hud,state.lifecycle);
    for(const auto& pickup:state.pickups) {
        if(state.lifecycle.last_death_source && (pickup.source.reassembled ||
            pickup.source.record_ordinal<=state.lifecycle.last_death_source->record_ordinal))continue;
        auto& row=history_[history_next_++%history_.size()];row={};row.occupied=true;
        row.kind=pickup.kind;row.id=pickup.identifier;row.amount=pickup.amount.value_or(0U);
        std::copy_n(pickup.item_token.begin(),std::min(pickup.item_token.size(),row.token.size()-1U),row.token.begin());
    }
    for(const auto& event:state.life_events)if(event.kind==client::RuntimeLifeEventKind::damage &&
        (!state.lifecycle.last_death_source || (!event.source.reassembled &&
         event.source.record_ordinal>state.lifecycle.last_death_source->record_ordinal)) &&
        (!damage_high_water_ || event.source.record_ordinal>damage_high_water_->record_ordinal ||
        (event.source.record_ordinal==damage_high_water_->record_ordinal && event.source.end_bit_offset>damage_high_water_->end_bit_offset))) {
        damage_high_water_=event.source;
        for(std::size_t i=0;i<damage_bits.size();++i)if((event.damage_bits&damage_bits[i])!=0U)damage_expiry_[i]=-1.0;
    }
}
HudMenuResult HalfLifeSpriteHud::input(const client::RuntimeClientObservationState& state,
    const HudMenuInput& input,std::span<const std::uint8_t> eligible,HudPresentationContext context) {
    HudMenuResult out;
    if(!state.generation || state.generation<generation_)return out;
    observe_boundary(state);
    out.consume_primary=consume_confirmation_hold(input);
    if(!std::isfinite(input.seconds) || input.seconds<0 || !alive(state) || !fresh_hud_binding(state))return out;
    const auto permitted=[&](std::uint8_t id){return std::find(eligible.begin(),eligible.end(),id)!=eligible.end();};
    // Last-item selection is gameplay input, not a visible inventory bucket.
    // The SDK swaps confirmed active/last items after CanHolster; this client
    // requests its last observed confirmed item through the existing typed
    // server selection path. A request/queue receipt never advances history.
    if(input.last_weapon_pressed && !input.cancel) {
        const bool spent_menu_click=menu_open_ && (input.primary_pressed || input.primary_held);
        menu_open_=false;candidate_.reset();menu_group_.reset();
        if(spent_menu_click){out.consume_primary=true;primary_confirmation_held_=true;}
        if(!pending_ && previous_weapon_ && confirmed_weapon_==state.weapon_hud.active_weapon_id &&
            confirmed_weapon_source_==state.weapon_hud.active_source &&
            previous_weapon_!=confirmed_weapon_ && permitted(*previous_weapon_)) {
            out.selection=previous_weapon_;pending_=previous_weapon_;pending_started_=input.seconds;
            pending_source_=state.weapon_hud.active_source;pending_pre_active_=state.weapon_hud.active_weapon_id;
            pending_queued_=false;selection_status_="awaiting_handoff";
        }
        return out;
    }
    if(!context.hud_draw || !suit(state) || (state.weapon_hud.hide_flags.value_or(0U)&5U)!=0U)return out;
    if(candidate_ && !permitted(*candidate_))candidate_.reset();
    if(input.cancel && (menu_open_ || input.group_digit || input.cycle_direction || input.last_weapon_pressed)) {
        const bool was_open=menu_open_;
        menu_open_=false;candidate_.reset();menu_group_.reset();out.cancel_consumed=true;
        out.consume_primary=input.primary_pressed || input.primary_held;
        if(out.consume_primary)primary_confirmation_held_=true;
        if(was_open)sound(3U,input.seconds);
    } else if(input.group_digit && *input.group_digit>=1U && *input.group_digit<=5U) {
        const auto slot=static_cast<std::uint8_t>(*input.group_digit-1U);
        std::array<std::uint8_t,32U> group{};std::size_t count=0;
        for(const auto id:eligible)if(const auto* type=weapon(state,id);type && type->slot==slot && count<group.size())group[count++]=id;
        const bool same=menu_open_ && menu_group_==slot;
        const auto found=candidate_ ? std::find(group.begin(),group.begin()+count,*candidate_) : group.begin()+count;
        const auto offset=same && found!=group.begin()+count ? (static_cast<std::size_t>(found-group.begin())+1U)%std::max<std::size_t>(count,1U) : 0U;
        menu_group_=slot;menu_open_=true;candidate_=count ? std::optional{group[offset]} : std::nullopt;
        sound(same ? 1U : 0U,input.seconds);
        // Base HL fastswitch applies only to a newly entered single-item bucket.
        if(context.fastswitch && !same && count==1U) {
            out.selection=candidate_;pending_=candidate_;pending_started_=input.seconds;
            pending_queued_=false;selection_status_="awaiting_handoff";
            pending_pre_active_=state.weapon_hud.active_weapon_id;
            pending_source_=state.weapon_hud.active_source;menu_open_=false;candidate_.reset();
        } else if(context.fastswitch && !count)menu_open_=false;
    } else if(input.cycle_direction && !eligible.empty()) {
        const auto current=candidate_ ? candidate_ : state.weapon_hud.active_weapon_id;
        const auto found=current ? std::find(eligible.begin(),eligible.end(),*current) : eligible.end();
        const auto size=eligible.size();
        const auto index=found==eligible.end() ? (input.cycle_direction>0 ? 0U : size-1U) :
            (static_cast<std::size_t>(found-eligible.begin())+size+(input.cycle_direction>0 ? 1U : size-1U))%size;
        candidate_=eligible[index];menu_open_=true;
        if(const auto* type=weapon(state,*candidate_))menu_group_=type->slot;
        sound(1U,input.seconds);
    }
    if(menu_open_ && (input.primary_pressed || input.primary_held)) {
        out.consume_primary=true;
        primary_confirmation_held_=true;
        if(candidate_ && permitted(*candidate_)){out.selection=candidate_;pending_=candidate_;
            pending_queued_=false;selection_status_="awaiting_handoff";
            pending_pre_active_=state.weapon_hud.active_weapon_id;
            pending_started_=input.seconds;pending_source_=state.weapon_hud.active_source;}
        menu_open_=false;candidate_.reset();menu_group_.reset();sound(2U,input.seconds);
    }
    out.menu_open=menu_open_;return out;
}

void HalfLifeSpriteHud::compose(const client::RuntimeClientObservationState& state,HudState& hud,
    HudPresentationContext context,double effective_fov,double now) {
    if(!state.generation || state.generation<generation_)return;
    observe_boundary(state);
    if(pending_ && pending_started_ && now>=*pending_started_+2.0) {
        pending_.reset();pending_started_.reset();pending_source_.reset();pending_pre_active_.reset();pending_queued_=false;
        selection_status_="confirmation_timeout";
    }
    hud.weapon_menu_open=menu_open_;hud.weapon_menu_candidate=candidate_;
    hud.weapon_selection_pending=pending_queued_ ? pending_ : std::nullopt;
    hud.weapon_selection_request=pending_queued_ ? std::nullopt : pending_;
    hud.weapon_selection_status=selection_status_;
    hud.crosshair_status=std::string(to_string(HudResourceStatus::hidden_by_policy));
    if(!std::isfinite(now) || now<0 || (last_time_ && now<*last_time_))return;
    last_time_=now;
    if(hud.health!=last_health_){last_health_=hud.health;health_fade_started_=now;}
    if(hud.armor!=last_armor_){last_armor_=hud.armor;armor_fade_started_=now;}
    if(hud.clip!=last_clip_ || hud.primary_reserve!=last_reserve_ || state.weapon_hud.active_weapon_id!=last_weapon_) {
        last_clip_=hud.clip;last_reserve_=hud.primary_reserve;last_weapon_=state.weapon_hud.active_weapon_id;
        ammo_fade_started_=now;
    }
    // Source-backed 100-minimum HUD fade, independently implemented in the
    // presentation clock. It never predicts or mutates a server counter.
    const auto value_fade=[&](double started) {
        return static_cast<float>((100.0+128.0*std::clamp(1.0-(now-started)/5.0,0.0,1.0))/255.0);
    };
    const auto ammo_fade=static_cast<float>(std::max(100.0,200.0-20.0*std::max(0.0,now-ammo_fade_started_))/255.0);
    for(auto& row:history_)if(row.occupied) {
        if(!row.expires)row.expires=now+5.0;
        else if(now>=*row.expires)row={};
    }
    for(auto& expires:damage_expiry_)if(expires) {
        if(*expires<0)*expires=now+2.0;
        else if(now>=*expires)expires.reset();
    }
    if(!context.hud_draw || context.viewport_width<160U || context.viewport_height<120U ||
        context.viewport_width>32768U || context.viewport_height>32768U)return;
    const auto hidden=state.weapon_hud.hide_flags.value_or(0U);
    const auto* base=descriptor("sprites/hud.txt");const auto base_tier=tier(base,context);
    hud.hud_resource_tier=base_tier;
    hud.hud_canvas_width=context.viewport_width;hud.hud_canvas_height=context.viewport_height;
    hud.hud_automatic_scale=halflife_hud_automatic_scale(context,base,base_tier);
    hud.hud_user_scale=halflife_hud_user_scale(context);
    hud.hud_effective_scale=hud.hud_automatic_scale*hud.hud_user_scale;
    const float width=static_cast<float>(context.viewport_width)/hud.hud_effective_scale;
    const float height=static_cast<float>(context.viewport_height)/hud.hud_effective_scale;
    // All layout is in source units; one final transform produces native
    // framebuffer pixels. The guard also covers hidden-HUD early returns.
    const PixelLayout layout{hud.draw,hud.draw.sprites.size(),hud.draw.rectangles.size(),hud.draw.texts.size(),
        hud.hud_effective_scale,static_cast<float>(context.viewport_width),static_cast<float>(context.viewport_height)};
    const auto base_role=[&](std::string_view name){return role(base,name,base_tier);};
    const auto weapon_data=[&](const auto* type){return type ? descriptor(halflife_hud_descriptor_name(type->command_name)) : nullptr;};
    const auto* active=state.weapon_hud.active_weapon_id ? weapon(state,*state.weapon_hud.active_weapon_id) : nullptr;
    const auto* active_data=weapon_data(active);const auto active_tier=tier(active_data,context);
    const bool active_fresh=after_hud_floors(state,state.weapon_hud.active_source);
    // Crosshair is an independent original resource role, not health's "cross".
    if(context.crosshair && alive(state) && active_fresh && (hidden&5U)==0U && !active &&
        state.weapon_hud.active_weapon_id && *state.weapon_hud.active_weapon_id!=0U)
        hud.crosshair_status=std::string(to_string(HudResourceStatus::not_yet_resolved));
    if(context.crosshair && alive(state) && active_fresh && (hidden&5U)==0U && active) {
        if(!active_data)hud.crosshair_status=std::string(to_string(HudResourceStatus::not_yet_resolved));
        else if(active_data->status!=HudResourceStatus::ready)hud.crosshair_status=std::string(to_string(active_data->status));
        else {
            const bool zoom=std::isfinite(effective_fov) && effective_fov<90.0;
            std::string_view name="crosshair";
            const auto* selected=role(active_data,name,active_tier);
            if(zoom) {if(const auto* value=role(active_data,"zoom",active_tier)) {selected=value;name="zoom";}}
            if(state.weapon_hud.active_on_target.value_or(false)) {
                const auto variant=zoom ? std::string_view{"zoom_autoaim"} : std::string_view{"autoaim"};
                if(const auto* value=role(active_data,variant,active_tier)) {selected=value;name=variant;}
            }
            hud.crosshair_role=std::string(name);
            if(!selected)hud.crosshair_status=std::string(to_string(HudResourceStatus::absent_by_asset_contract));
            else if(!usable(selected))hud.crosshair_status=std::string(to_string(
                selected->status==HudResourceStatus::ready ? HudResourceStatus::invalid_rectangle : selected->status));
            else {
                image(hud,selected,std::floor((width-static_cast<float>(selected->source.width))*.5F),
                    std::floor((height-static_cast<float>(selected->source.height))*.5F),{1,1,1,1},20U);
                hud.crosshair_status=std::string(to_string(HudResourceStatus::submitted));
                hud.crosshair_resource_id=selected->resource_id;
            }
        }
    }
    if((hidden&4U)!=0U)return;
    const auto* digit0=base_role("number_0");
    const float digit_width=usable(digit0) ? static_cast<float>(digit0->source.width) : 0;
    const float digit_height=usable(digit0) ? static_cast<float>(digit0->source.height) : 0;
    float ammo_readout_top=height;
    const auto number=[&](int value,float x,float y,std::array<float,4U> color,bool padded=true) {
        if(!digit_width || value<0)return x;
        value=std::min(value,999);const auto text=std::to_string(value);
        if(padded)x+=(3.0F-static_cast<float>(text.size()))*digit_width;
        for(const char c:text) {image(hud,base_role("number_"+std::string(1,c)),x,y,color);x+=digit_width;}
        return x;
    };
    if(suit(state)) {
        const float baseline=height-digit_height*1.5F;
        if((hidden&8U)==0U && digit_width) {
            const auto health_color=brightness(hud.health && *hud.health<=25 ? red : yellow,
                hud.health && *hud.health<=15 ? 1.0F : value_fade(health_fade_started_));
            const auto* cross=base_role("cross");const float cross_width=usable(cross) ? static_cast<float>(cross->source.width) : digit_width;
            image(hud,cross,cross_width*.5F,baseline,health_color);
            const auto x=number(hud.health.value_or(-1),cross_width+digit_width*.5F,baseline+digit_height*.2F,health_color);
            rectangle(hud,x+digit_width*.5F,baseline+digit_height*.2F,std::max(1.0F,digit_width*.1F),digit_height,health_color);
            const auto* empty=base_role("suit_empty");const auto* full=base_role("suit_full");
            if(usable(empty)) {
                const auto armor_color=brightness(yellow,value_fade(armor_fade_started_));const float armor_x=3.0F*empty->source.width;
                const float armor_y=baseline-static_cast<float>(empty->source.height)/6.0F;
                image(hud,empty,armor_x,armor_y,armor_color);
                if(usable(full) && hud.armor) {
                    auto crop=full->source;const auto skipped=static_cast<std::uint32_t>(
                        static_cast<double>(crop.height)*(100-std::clamp(*hud.armor,0,100))/100.0);
                    crop.y+=skipped;crop.height-=skipped;
                    image(hud,full,armor_x,armor_y+skipped,armor_color,10U,crop);
                }
                (void)number(hud.armor.value_or(-1),armor_x+empty->source.width,baseline+digit_height*.2F,armor_color);
            }
        }
        if((hidden&1U)==0U && alive(state) && active_fresh && active && digit_width) {
            const auto* ammo=role(active_data,"ammo",active_tier);
            const auto* ammo2=role(active_data,"ammo2",active_tier);
            const auto tint=brightness(yellow,ammo_fade);const float y=baseline+digit_height*.2F;
            const float icon_width=usable(ammo) ? static_cast<float>(ammo->source.width) : 0.0F;
            if(active->primary_ammo_type>=0 && hud.primary_reserve) {
                float x=width-(hud.clip ? 8.0F : 4.0F)*digit_width-icon_width;
                if(hud.clip) {
                    x=number(*hud.clip,x,y,tint);
                    rectangle(hud,x+digit_width*.5F,y,std::max(1.0F,digit_width*.1F),digit_height,tint);
                    x+=digit_width+std::max(1.0F,digit_width*.1F);
                }
                x=number(*hud.primary_reserve,x,y,tint);
                const float icon_y=y-(usable(ammo) ? ammo->source.height/8.0F : 0.0F);
                image(hud,ammo,x,icon_y,tint);
                ammo_readout_top=std::min(ammo_readout_top,icon_y);
            }
            const auto secondary=reserve(state,active->secondary_ammo_type);
            if(active->secondary_ammo_type>=0 && secondary>0) {
                const float icon2_width=usable(ammo2) ? static_cast<float>(ammo2->source.width) : 0;
                const float yy=y-digit_height*1.25F;
                const auto x=number(static_cast<int>(secondary),width-4.0F*digit_width-icon2_width,yy,tint);
                const float icon_y=yy-(usable(ammo2) ? ammo2->source.height/8.0F : 0.0F);
                image(hud,ammo2,x,icon_y,tint);
                ammo_readout_top=std::min(ammo_readout_top,icon_y);
            }
        }
        if((hidden&2U)==0U && state.weapon_hud.flashlight_battery && state.weapon_hud.flashlight_on) {
            const auto* empty=base_role("flash_empty");const auto* full=base_role("flash_full");
            if(usable(empty)) {
                const auto battery=std::min<unsigned>(*state.weapon_hud.flashlight_battery,100U);
                const auto tint=brightness(battery<20U ? red : yellow,*state.weapon_hud.flashlight_on ? 225.0F/255.0F : 100.0F/255.0F);
                const float x=width-empty->source.width*1.5F,y=empty->source.height*.5F;
                image(hud,empty,x,y,tint);
                if(usable(full)) {auto crop=full->source;const auto offset=crop.width*(100U-battery)/100U;
                    crop.x+=offset;crop.width-=offset;image(hud,full,x+offset,y,tint,10U,crop);}
                if(*state.weapon_hud.flashlight_on)image(hud,base_role("flash_beam"),width-empty->source.width*.5F,y,tint);
            }
        }
    }
    if(alive(state)) {
        float status_y=height*.5F;
        for(const auto& icon:state.weapon_hud.status_icons)if(icon) {
            const auto* entry=base_role(icon->token);if(!usable(entry))continue;
            status_y-=entry->source.height+5.0F;
            image(hud,entry,5.0F,status_y,{icon->color[0]/255.0F,icon->color[1]/255.0F,icon->color[2]/255.0F,1});
        }
        if((hidden&8U)==0U) {
            float y=height;
            for(std::size_t i=0;i<damage_expiry_.size();++i)if(damage_expiry_[i]) {
                const auto* entry=base_role(damage_roles[i]);if(!usable(entry))continue;
                y-=entry->source.height;image(hud,entry,entry->source.width/8.0F,y-entry->source.height,
                    brightness(yellow,static_cast<float>(std::abs(std::sin(now*2.0)))));
            }
        }
    }
    if(suit(state) && alive(state) && (hidden&1U)==0U) {
        // Original pickup messages are feedback only. Absolute counters and
        // inventory remain the committed server projections above.
        // History is separate feedback, not another reserve counter. Its
        // full row bounds must sit above both primary and secondary readouts.
        // A fixed100-unit offset overlaps48/72px tier digits, and spacing by
        // each current icon's height also overlaps mixed weapon/ammo rows.
        const float history_gap=std::max(5.0F,digit_height*.25F);
        float history_bottom=ammo_readout_top-history_gap;
        for(auto& row:history_)if(row.occupied && row.expires) {
            const HudResourceEntry* entry=nullptr;
            if(row.kind==GameRecordState::Pickup::Kind::item)entry=base_role(std::string_view{row.token.data()});
            else if(row.kind==GameRecordState::Pickup::Kind::weapon) {
                const auto* data=weapon_data(weapon(state,row.id));entry=role(data,"weapon",tier(data,context));
            } else {
                for(const auto& type:state.weapon_hud.catalogue)if(type.primary_ammo_type==row.id || type.secondary_ammo_type==row.id) {
                    const auto* data=weapon_data(&type);entry=role(data,type.primary_ammo_type==row.id ? "ammo" : "ammo2",tier(data,context));
                    if(usable(entry))break;
                }
            }
            if(!usable(entry)){++hud.pickup_icons_unavailable;continue;}
            const auto tint=brightness(yellow,static_cast<float>(std::min(1.0,(*row.expires-now)*80.0/255.0)));
            const float row_height=std::max(static_cast<float>(entry->source.height),
                row.kind==GameRecordState::Pickup::Kind::ammunition ? digit_height : 0.0F);
            const float y=std::min(height-std::max(100.0F,entry->source.height*2.0F),
                history_bottom-row_height);
            history_bottom=y-history_gap;
            const float x=width-entry->source.width-10.0F;
            image(hud,entry,x,y,tint);++hud.pickup_icons_submitted;
            if(row.kind==GameRecordState::Pickup::Kind::ammunition && digit_width)
                (void)number(row.amount,x-4.0F*digit_width,y,tint,false);
        }
        if(menu_open_) {
            float x=10.0F;
            for(std::uint8_t slot=0;slot<5U;++slot) {
                const auto* bucket=base_role("bucket"+std::to_string(slot+1U));
                const float bw=usable(bucket) ? static_cast<float>(bucket->source.width) : 20.0F;
                const float bh=usable(bucket) ? static_cast<float>(bucket->source.height) : 20.0F;
                float column=bw;const bool active_group=menu_group_==slot;
                if(active_group)for(const auto& type:state.weapon_hud.catalogue)if(type.slot==slot && owned(state,type.id)) {
                    const auto* data=weapon_data(&type);const auto* icon=role(data,"weapon_s",tier(data,context));
                    if(usable(icon))column=std::max(column,static_cast<float>(icon->source.width));
                }
                image(hud,bucket,x,10.0F,brightness(yellow,active_group ? 1.0F : .75F),30U);
                float y=10.0F+bh;
                std::array<const client::RuntimeWeaponTypeObservation*,32U> entries{};std::size_t count=0;
                for(const auto& type:state.weapon_hud.catalogue)if(type.slot==slot && owned(state,type.id) && count<entries.size())entries[count++]=&type;
                std::sort(entries.begin(),entries.begin()+count,[](const auto* a,const auto* b){return std::tie(a->position,a->id)<std::tie(b->position,b->id);});
                for(std::size_t i=0;i<count;++i) {
                    const auto* type=entries[i];const bool selected=candidate_==type->id;
                    const bool ammo_available=has_ammo(state,*type);
                    if(active_group) {
                        const auto* data=weapon_data(type);const auto resolution=tier(data,context);
                        const auto* icon=role(data,selected ? "weapon_s" : "weapon",resolution);
                        image(hud,icon,x,y,brightness(ammo_available ? yellow : red,selected ? 1.0F : .75F),30U);
                        if(selected)image(hud,base_role("selection"),x,y,yellow,31U);
                        const auto bar=[&](std::int8_t ammo,std::optional<std::int16_t> maximum,float bx) {
                            if(ammo<0 || !maximum || *maximum<=0)return;
                            const float amount=std::clamp(static_cast<float>(reserve(state,ammo))/static_cast<float>(*maximum),0.0F,1.0F);
                            const float bar_width=std::max(10.0F,column*.12F);
                            rectangle(hud,bx,y,bar_width,3.0F,brightness(yellow,.5F),32U);
                            if(amount>0)rectangle(hud,bx,y,std::max(1.0F,bar_width*amount),3.0F,{0,1,0,1},33U);
                        };
                        bar(type->primary_ammo_type,type->primary_maximum_ammo,x+4.0F);
                        bar(type->secondary_ammo_type,type->secondary_maximum_ammo,x+column*.2F);
                        y+=(usable(icon) ? icon->source.height : bh)+5.0F;
                    } else {
                        rectangle(hud,x,y,bw,bh,brightness(ammo_available ? yellow : red,.5F),30U);y+=bh+5.0F;
                    }
                }
                x+=column+5.0F;
            }
        }
    }
}
} // namespace hlclient::games::halflife
