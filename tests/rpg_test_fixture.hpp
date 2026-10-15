#pragma once
#include "crossbow_test_fixture.hpp"
namespace hlclient::tests::rpg_fixture {
namespace api=game_api;
inline api::LocalWeaponModelMetadata model() {
    auto m=crossbow_fixture::model();m.resource_name="models/v_rpg.mdl";m.sequences.clear();
    for(const auto label:{"idle","fidget","reload","fire","holster1","draw1","holster2","draw2","idle2","fidget2"})
        m.sequences.push_back({30,61,false,{},label});return m;
}
inline client::RuntimeClientObservationState state(int clip=1,std::size_t record=1,int reserve=4) {
    auto s=crossbow_fixture::state(clip,record,reserve);
    s.receiving_client->owned_weapon_bits=1U<<8;s.receiving_client->auxiliary_vector2={0.0,1.0,0.0};
    s.weapon_hud.active_weapon_id=8;s.weapon_hud.catalogue[0].id=8;
    s.weapon_hud.catalogue[0].command_name="weapon_rpg";s.weapon_hud.clips[6].reset();s.weapon_hud.clips[8]=clip;
    s.weapon_slots[0].weapon_id=8;s.weapon_slots[0].wire_slot=8;return s;
}
inline api::ScriptedEventBinding binding() {
    auto b=crossbow_fixture::binding();b.name={};constexpr std::string_view name="events/rpg.sc";
    std::copy(name.begin(),name.end(),b.name.begin());return b;
}
struct Module {
    api::GameClientHost host{games::halflife::make_half_life_client_module()};
    client::RuntimeClientObservationState observed;
    explicit Module(int clip=1) : observed(state(clip)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=8;initial.weapon_hud=observed.weapon_hud;
        host.commit_record(std::move(initial));host.bind_model(model());host.observe(observed,0);
        const std::array events{binding()};host.configure_scripted_events(events);
    }
};
}
