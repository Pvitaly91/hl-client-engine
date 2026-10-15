#pragma once
#include "hand_grenade_test_fixture.hpp"
#include <hlclient/games/halflife/tripmine_profile.hpp>
namespace hlclient::tests::tripmine_fixture {
namespace api=game_api;namespace c=client;namespace h=games::halflife;
inline api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=75;m.resource_revision=2;
    m.resource_name="models/v_tripmine.mdl";
    for(unsigned body=0;body<4;++body)m.supported_bodies[body]=m.selectable_bodies[body]=true;
    constexpr std::array<std::string_view,9> names{"idle1","idle2","arm1","place","fidget","holster","draw","world","ground"};
    for(unsigned i=0;i<9;++i)m.sequences.push_back({30,i>=7 ? 1U : 31U,i<2,{},std::string(names[i])});
    return m;
}
inline c::RuntimeClientObservationState state(unsigned ammo=3,unsigned record=1) {
    auto s=hand_grenade_fixture::state(ammo,record);s.weapon_hud.active_weapon_id=13;
    s.receiving_client->owned_weapon_bits=(1U<<13)|(1U<<2);s.receiving_client->weapon_animation=6;
    s.weapon_hud.clips[12].reset();s.weapon_hud.clips[13]=-1;
    auto& item=s.weapon_hud.catalogue[0];item.id=13;item.command_name="weapon_tripmine";
    item.slot=4;item.position=2;item.flags=24;s.weapon_slots[0].wire_slot=13;s.weapon_slots[0].weapon_id=13;
    return s;
}
inline api::LocalWeaponSubmittedCommand command(unsigned sequence=1,double at=.02,
    std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
    return hand_grenade_fixture::command(sequence,at,buttons);
}
inline api::ScriptedEventBinding binding(unsigned index=31) {
    api::ScriptedEventBinding out;out.index=static_cast<std::uint16_t>(index);
    constexpr std::string_view name="events/tripfire.sc";std::copy(name.begin(),name.end(),out.name.begin());return out;
}
inline api::CommittedScriptedEvent event(unsigned record=2,unsigned entity=1,unsigned index=31,double at=.03) {
    api::CommittedScriptedEvent e;e.generation=1;e.record=e.record_ordinal=record;e.emitter_entity=entity;
    e.event_index=static_cast<std::uint16_t>(index);e.origin={100,80,52};e.angles={};e.received_at_seconds=at;
    e.weapon_model_index=75;return e;
}
struct Query final:api::IStaticEffectTrace {
    api::PlacementTraceResult result{true,false,false,.5,0U,{164,80,80},{-1,0,0}};
    mutable unsigned calls{};mutable std::optional<api::PlacementTraceRequest> last;
    api::StaticEffectTrace trace(assets::AssetVector3,assets::AssetVector3) const noexcept override {return {};}
    api::PlacementTraceResult placement_trace(const api::PlacementTraceRequest& r) const noexcept override {
        ++calls;last=r;return result;
    }
};
struct Controller {
    h::LocalWeaponPresentationController controller;c::RuntimeClientObservationState observed;
    Query query;
    explicit Controller(unsigned ammo=3):observed(state(ammo)) {
        const std::array bindings{binding()};controller.configure_scripted_events(bindings);
        controller.bind_model(model());controller.observe(observed,0);
    }
    void submit(unsigned sequence=1,double at=.02,std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
        controller.submit(command(sequence,at,buttons),at);controller.prepare_presentation(query,at,1);
    }
    void observe(c::RuntimeClientObservationState s,double at) {observed=std::move(s);controller.observe(observed,at);}
};
struct Module {
    api::GameClientHost host{h::make_half_life_client_module()};c::RuntimeClientObservationState observed;
    Query query;
    explicit Module(unsigned ammo=3):observed(state(ammo)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=8;initial.weapon_hud=observed.weapon_hud;
        host.commit_record(std::move(initial));const std::array bindings{binding()};host.configure_scripted_events(bindings);
        host.bind_model(model());host.observe(observed,0);
    }
    void submit(unsigned seq=1,double at=.02) {host.submit(command(seq,at),at);host.prepare_presentation(query,at);}
};
}
