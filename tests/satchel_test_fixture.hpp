#pragma once
#include "hand_grenade_test_fixture.hpp"
#include <stdexcept>
namespace hlclient::tests::satchel_fixture {
namespace api=game_api;namespace c=client;
inline api::LocalWeaponModelMetadata model(bool radio=false) {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=radio ? 85U : 75U;m.resource_revision=2;
    m.resource_name=radio ? "models/v_satchel_radio.mdl" : "models/v_satchel.mdl";
    m.supported_bodies[0]=m.selectable_bodies[0]=true;
    constexpr std::array<std::string_view,5> labels{"idle1","fidget1","draw","fire","holster"};
    for(std::size_t i=0;i<(radio ? 5U : 4U);++i)
        m.sequences.push_back({30,31,i<2,{},std::string(!radio && i==3 ? "drop" : labels[i])});
    return m;
}
inline c::RuntimeClientObservationState state(unsigned ammo=3,int phase=0,std::size_t record=1) {
    auto s=hand_grenade_fixture::state(ammo,record);
    s.receiving_client->viewmodel_index=phase==0 ? 75U : 85U;
    s.receiving_client->owned_weapon_bits=(1U<<14)|(1U<<2);
    s.weapon_hud.active_weapon_id=14;s.weapon_hud.clips[12].reset();s.weapon_hud.clips[14]=-1;
    auto& type=s.weapon_hud.catalogue[0];type.id=14;type.command_name="weapon_satchel";type.position=1;type.flags=25;
    auto& slot=s.weapon_slots[0];slot.weapon_id=14;slot.wire_slot=14;
    slot.auxiliary_float2.reset();slot.auxiliary_float3.reset();slot.auxiliary_integer1=phase;
    return s;
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t seq,double at,std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
    return hand_grenade_fixture::command(seq,at,buttons);
}
struct Controller {
    games::halflife::LocalWeaponPresentationController controller;
    c::RuntimeClientObservationState observed;
    explicit Controller(unsigned ammo=3,int phase=0):observed(state(ammo,phase)) {
        controller.configure_maximum_clients(8);const std::array variants{model(),model(true)};
        controller.bind_model_variants(variants);controller.bind_model(model(phase!=0));controller.observe(observed,0);
    }
    void submit(std::uint32_t seq,double at,std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
        controller.submit(command(seq,at,buttons),at);
    }
    void observe(c::RuntimeClientObservationState s,double at) {
        observed=std::move(s);controller.bind_model(model(observed.receiving_client->viewmodel_index==85));
        controller.observe(observed,at);
    }
};
struct Module {
    api::GameClientHost host{games::halflife::make_half_life_client_module()};
    c::RuntimeClientObservationState observed;
    explicit Module(unsigned ammo=3,int phase=0):observed(state(ammo,phase)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=8;initial.weapon_hud=observed.weapon_hud;
        host.commit_record(std::move(initial));const std::array variants{model(),model(true)};
        if(!host.bind_model_variants(variants)) throw std::runtime_error{"owned metadata rejected"};
        host.bind_model(model(phase!=0));host.observe(observed,0);
    }
};
}
