#pragma once
#include "python_357_test_fixture.hpp"
#include "crossbow_test_fixture.hpp"
#include "rpg_test_fixture.hpp"
#include "hornet_test_fixture.hpp"
#include "hand_grenade_test_fixture.hpp"
#include "satchel_test_fixture.hpp"
#include "tripmine_test_fixture.hpp"
#include "snark_test_fixture.hpp"
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/gauss.hpp>
#include <hlclient/games/halflife/egon.hpp>
#include <array>
#include <stdexcept>

namespace hlclient::tests::weapon_suite_fixture {
namespace api=game_api;
namespace c=client;
namespace h=games::halflife;
struct Weapon {
    std::uint8_t id;
    std::string_view command,viewmodel,pmodel,hud_name;
    std::int8_t ammo;
    std::int16_t clip;
    std::uint8_t slot,position,flags;
};
// Project-owned test catalogue. Numeric ammunition indices are assigned by
// this fixture, not claimed constants of a stock server's precache registry.
inline constexpr std::array<Weapon,14> weapons{{
    {1,"weapon_crowbar","models/v_crowbar.mdl","models/p_crowbar.mdl","weapon_crowbar",-1,-1,0,0,0},
    {2,"weapon_9mmhandgun","models/v_9mmhandgun.mdl","models/p_9mmhandgun.mdl","weapon_9mmhandgun",1,17,1,0,0},
    {3,"weapon_357","models/v_357.mdl","models/p_357.mdl","weapon_357",2,6,1,1,0},
    {4,"weapon_9mmAR","models/v_9mmAR.mdl","models/p_9mmAR.mdl","weapon_9mmAR",1,50,2,0,0},
    {6,"weapon_crossbow","models/v_crossbow.mdl","models/p_crossbow.mdl","weapon_crossbow",3,5,2,2,0},
    {7,"weapon_shotgun","models/v_shotgun.mdl","models/p_shotgun.mdl","weapon_shotgun",4,8,2,1,0},
    {8,"weapon_rpg","models/v_rpg.mdl","models/p_rpg.mdl","weapon_rpg",5,1,3,0,4},
    {9,"weapon_gauss","models/v_gauss.mdl","models/p_gauss.mdl","weapon_gauss",6,-1,3,1,0},
    {10,"weapon_egon","models/v_egon.mdl","models/p_egon.mdl","weapon_egon",6,-1,3,2,0},
    {11,"weapon_hornetgun","models/v_hgun.mdl","models/p_hgun.mdl","Hornet Gun",7,-1,3,3,10},
    {12,"weapon_handgrenade","models/v_grenade.mdl","models/p_grenade.mdl","Hand Grenade",8,-1,4,0,24},
    {13,"weapon_tripmine","models/v_tripmine.mdl","models/p_tripmine.mdl","Trip Mine",9,-1,4,2,24},
    {14,"weapon_satchel","models/v_satchel.mdl","models/p_satchel.mdl","Satchel Charge",10,-1,4,1,25},
    {15,"weapon_snark","models/v_squeak.mdl","models/p_squeak.mdl","Snark",11,-1,4,3,24}
}};
inline const Weapon& entry(std::uint8_t id) {
    const auto found=std::find_if(weapons.begin(),weapons.end(),[=](const auto& w){return w.id==id;});
    if(found==weapons.end())throw std::invalid_argument{"not a suite weapon"};
    return *found;
}
inline std::uint32_t model_slot(std::uint8_t id,bool radio=false) noexcept {
    return 100U+id+(radio ? 100U : 0U);
}
inline std::uint32_t remote_model_slot(std::uint8_t id,bool radio=false) noexcept {
    return 300U+id+(radio ? 100U : 0U);
}
inline api::LocalWeaponModelMetadata model(std::uint8_t id,bool radio=false) {
    api::LocalWeaponModelMetadata out;
    if(id==3)out=python_fixture::model();
    else if(id==6)out=crossbow_fixture::model();
    else if(id==8)out=rpg_fixture::model();
    else if(id==11)out=hornet_fixture::model();
    else if(id==12)out=hand_grenade_fixture::model();
    else if(id==13)out=tripmine_fixture::model();
    else if(id==14)out=satchel_fixture::model(radio);
    else if(id==15)out=snark_fixture::model();
    else {
        out.generation=1;out.resource_revision=2;out.resource_name=entry(id).viewmodel;
        out.supported_bodies[0]=out.supported_bodies[1]=out.supported_bodies[2]=true;
        out.selectable_bodies[0]=out.selectable_bodies[1]=out.selectable_bodies[2]=true;
        const auto append=[&](std::initializer_list<std::string_view> labels) {
            for(const auto label:labels)out.sequences.push_back({30,31,false,{},std::string(label)});
        };
        if(id==1)append({"idle1","idle2","idle3","attack1hit","attack1miss","attack2miss","attack2hit","attack3miss","attack3hit"});
        else if(id==2)append({"idle1","idle2","idle3","shoot","shoot_empty","reload_empty","reload","draw","holster","add_silencer"});
        else if(id==4)append({"longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"});
        else if(id==7)append({"sm_idle","shoot","shoot_big","reload","pump","start_reload","draw","reholster","idle4","deepidle"});
        else if(id==9)append({"idle","idle2","fidget","spinup","spin","fire","fire2","holster","draw"});
        else if(id==10)append({"idle1","fidget1","altfireon","altfirecycle","altfireoff","fire1","fire2","fire3","fire4","draw","holster"});
        else throw std::invalid_argument{"missing project-owned model fixture"};
        for(std::size_t i=0;i<out.sequences.size();++i) {
            const bool loops=id==1 || id==2 ? i<=2 : id==4 ? i<=1 :
                id==7 ? i==0 || i>=8 : id==9 ? i<=2 || i==4 :
                i<=1 || i==3 || (i>=5 && i<=8);
            out.sequences[i].looping=loops;
        }
        // Egon's FIRE variants are continuous loop roles, including imported
        // one-frame variants. A malformed fixture must not silently test only
        // the generic viewmodel fallback while bypassing the actual controller.
        if(id==10) {
            out.sequences[5].frame_count=out.sequences[6].frame_count=1;
            if(!h::egon_sequences(out))throw std::logic_error{"invalid Egon suite fixture"};
        }
        // Existing E4/E15 Studio marker contract: a valid eligible shot is
        // materialized by the actual production visual controller.
        const auto marker=[&](std::size_t sequence) {
            out.sequences[sequence].events.push_back({0,5001,0,{std::byte{'1'},std::byte{'1'}}});
        };
        if(id==2){marker(3);marker(4);}
        if(id==4){marker(5);marker(6);marker(7);}
        if(id==7){marker(1);marker(2);}
    }
    out.model_index=model_slot(id,radio);return out;
}
inline c::RuntimeObservationSource source(std::uint64_t record) noexcept {
    return {.record_identity=record,.record_ordinal=static_cast<std::size_t>(record)};
}
inline void select(c::RuntimeClientObservationState& state,std::uint8_t id,
    std::uint64_t record,bool radio=false) {
    state.publication_revision=record;
    state.client_metadata={.generation=state.generation,
        .freshness=c::RuntimeObservationFreshness::observed_in_record,.source=source(record)};
    state.receiving_client->viewmodel_index=model_slot(id,radio);
    state.receiving_client->weapon_animation=0;
    state.weapon_hud.active_weapon_id=id;state.weapon_hud.active_source=source(record);
    state.weapon_hud.animation_source.reset();state.weapon_hud.animation_sequence.reset();
    state.receiving_client->field_of_view=0;
}
inline c::RuntimeClientObservationState state(std::uint8_t id=2,std::uint64_t record=1) {
    auto out=hand_grenade_fixture::state();
    out.weapon_hud={};out.weapon_slots.clear();
    out.receiving_client->owned_weapon_bits=0;
    out.receiving_client->auxiliary_float2=0;out.receiving_client->auxiliary_float3=0;
    out.receiving_client->auxiliary_vector2={0.,0.,0.};
    for(const auto& weapon:weapons) {
        *out.receiving_client->owned_weapon_bits|=1U<<weapon.id;
        out.weapon_hud.catalogue.push_back({.id=weapon.id,.command_name=std::string(weapon.command),
            .primary_ammo_type=weapon.ammo,.secondary_ammo_type=weapon.id==4 ? std::int8_t{12} : std::int8_t{-1},
            .slot=weapon.slot,.position=weapon.position,.flags=weapon.flags,.source=source(record)});
        out.weapon_hud.clips[weapon.id]=weapon.clip;
        out.weapon_slots.push_back({.wire_slot=weapon.id,.clip=weapon.clip,.in_reload=false,.next_reload=0,
            .next_primary_attack=0,.weapon_id=weapon.id,.next_secondary_attack=0,.time_weapon_idle=0,
            .pump_time=0,.special_reload_state=0,
            .auxiliary_integer2=0,.auxiliary_integer3=0,.auxiliary_integer1=0,
            .auxiliary_float2=0,.auxiliary_float3=weapon.id==12 ? -1. : 0.});
    }
    constexpr std::array<unsigned,13> counts{0,100,18,10,20,4,60,8,5,3,3,5,5};
    for(std::size_t i=1;i<counts.size();++i) {
        out.weapon_hud.reserve_ammo[i]=static_cast<std::uint8_t>(counts[i]);
        out.weapon_hud.reserve_ammo_sources[i]=source(record);
    }
    out.weapon_hud.last_message_source=source(record);
    select(out,id,record);return out;
}
inline c::RuntimeWeaponSlotObservation& slot(c::RuntimeClientObservationState& state,std::uint8_t id) {
    return *std::find_if(state.weapon_slots.begin(),state.weapon_slots.end(),[=](const auto& w){return w.weapon_id==id;});
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t sequence,double at,std::uint16_t buttons=1) {
    auto out=hand_grenade_fixture::command(sequence,at,buttons);
    out.shot_context->player_origin=std::array<float,3>{100,80,52};
    out.shot_context->player_ducked=false;return out;
}
inline std::vector<api::ScriptedEventBinding> bindings() {
    constexpr std::array names{"events/crowbar.sc","events/glock1.sc","events/glock2.sc",
        "events/python.sc","events/mp5.sc","events/mp52.sc","events/crossbow1.sc","events/crossbow2.sc",
        "events/shotgun1.sc","events/shotgun2.sc","events/rpg.sc","events/gauss.sc","events/gaussspin.sc",
        "events/egon_fire.sc","events/egon_stop.sc","events/firehornet.sc","events/tripfire.sc","events/snarkfire.sc"};
    std::vector<api::ScriptedEventBinding> result;
    for(std::size_t i=0;i<names.size();++i) {
        api::ScriptedEventBinding b;b.index=static_cast<std::uint16_t>(201+i);
        std::string_view token{names[i]};std::copy(token.begin(),token.end(),b.name.begin());result.push_back(b);
    }return result;
}
struct World final:api::IStaticEffectTrace {
    bool hits{true};mutable std::size_t traces{},placements{};
    api::StaticEffectTrace trace(assets::AssetVector3,assets::AssetVector3 end) const noexcept override {
        ++traces;return {true,false,false,hits,false,hits ? assets::AssetVector3{164,80,80} : end,{-1,0,0}};
    }
    api::PlacementTraceResult placement_trace(const api::PlacementTraceRequest& q) const noexcept override {
        ++placements;
        const auto length=std::hypot(q.end.x-q.start.x,q.end.y-q.start.y,q.end.z-q.start.z);
        if(length>100)return {true,false,false,.5,0U,{164,80,80},{-1,0,0}};
        return {true,false,false,1.,std::nullopt,q.end,{}};
    }
};
struct Module {
    api::GameClientHost host{h::make_half_life_client_module()};
    c::RuntimeClientObservationState observed;
    World world;
    explicit Module(std::uint8_t id=2):observed(state(id)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=8;
        initial.weapon_hud=observed.weapon_hud;initial.lifecycle=observed.lifecycle;
        host.commit_record(std::move(initial));const auto events=bindings();host.configure_scripted_events(events);
        const std::array variants{model(14),model(14,true)};
        if(!host.bind_model_variants(variants))throw std::runtime_error{"valid variants rejected"};
        host.bind_model(model(id));host.observe(observed,0);
    }
    void observe(double at) {host.observe(observed,at);}
    void adopt(std::uint8_t id,double at,std::uint64_t record,bool radio=false) {
        select(observed,id,record,radio);
        // Match normal RX ownership: the authoritative HUD/life record commits
        // before observers consume its selected-model and presentation state.
        api::GameRecordState publication;publication.multiplayer.maximum_clients=8;
        publication.weapon_hud=observed.weapon_hud;publication.lifecycle=observed.lifecycle;
        host.commit_record(std::move(publication));
        host.bind_model(model(id,radio));host.observe(observed,at);
    }
    void submit(std::uint32_t sequence,double at,std::uint16_t buttons=1) {
        const auto cmd=command(sequence,at,buttons);
        (void)host.command_movement_effect(cmd);host.submit(cmd,at);host.prepare_presentation(world,at);
    }
    api::LocalVisualFrame visuals(double at) {
        return host.local_visuals({{100,80,80},{1,0,0},{0,1,0},{0,0,1},{},at});
    }
    api::LocalWeaponPresentationSnapshot sample(double at) {return host.sample(at);}
};
struct Message {std::string_view name;std::vector<std::byte> body;};
inline Message message(std::string_view name,std::initializer_list<unsigned> bytes) {
    Message result{name,{}};
    for(const auto byte:bytes)result.body.push_back(static_cast<std::byte>(byte));
    return result;
}
inline api::GameRecordResult stage_messages(Module& module,const std::vector<Message>& messages,
    std::uint64_t record,c::RuntimeClientObservationState& candidate) {
    candidate=module.observed;select(candidate,*candidate.weapon_hud.active_weapon_id,record);
    std::vector<api::GameMessageView> views;std::size_t cursor=8;
    for(const auto& message:messages) {
        const auto end=cursor+8+message.body.size()*8;
        views.push_back({api::GameMessageKind::user_message,message.name,message.body,
            {.record_identity=record,.record_ordinal=static_cast<std::size_t>(record),.start_bit_offset=cursor,.end_bit_offset=end}});
        cursor=end;
    }
    return module.host.stage_record({&module.observed,candidate,views,1,8});
}
} // namespace hlclient::tests::weapon_suite_fixture
