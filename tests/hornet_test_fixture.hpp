#pragma once
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
namespace hlclient::tests::hornet_fixture {
namespace api=game_api;
inline api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=73;m.resource_revision=2;
    m.resource_name="models/v_hgun.mdl";
    m.supported_bodies[0]=m.supported_bodies[1]=true;
    m.selectable_bodies[0]=m.selectable_bodies[1]=true;
    for(const auto label:{"idle1","fidgetSway","fidgetShake","down","up","Shoot"})
        m.sequences.push_back({30,10,false,{},label});
    m.sequences[0].looping=true;return m;
}
inline client::RuntimeClientObservationState state(unsigned ammo=8,std::size_t record=1) {
    client::RuntimeClientObservationState s;s.generation=1;s.publication_revision=record;
    s.client_metadata={.generation=1,.freshness=client::RuntimeObservationFreshness::observed_in_record,
        .source=client::RuntimeObservationSource{.record_identity=record,.record_ordinal=record}};
    s.receiving_client.emplace();auto& c=*s.receiving_client;
    c.health=100;c.viewmodel_index=73;c.owned_weapon_bits=1U<<11;
    c.next_weapon_attack=0;c.weapon_animation=0;c.water_level=0;
    c.origin={100.0,80.0,52.0};c.view_offset={0.0,0.0,28.0};c.punch_angle={0.0,0.0,0.0};
    s.weapon_hud.active_weapon_id=11;s.weapon_hud.active_source=s.client_metadata.source;
    s.weapon_hud.last_message_source=s.client_metadata.source;
    s.weapon_hud.clips[11]=-1;s.weapon_hud.reserve_ammo[2]=static_cast<std::uint8_t>(ammo);
    s.weapon_hud.reserve_ammo_sources[2]=s.client_metadata.source;
    s.weapon_hud.catalogue.push_back({.id=11,.command_name="weapon_hornetgun",.primary_ammo_type=2,
        .slot=3,.position=3,.source=*s.client_metadata.source});
    s.weapon_slots.push_back({.wire_slot=11,.clip=-1,.in_reload=false,.next_reload=0,
        .next_primary_attack=0,.weapon_id=11,.next_secondary_attack=0});return s;
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t seq=1,double at=.02,
    std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
    api::LocalWeaponSubmittedCommand c{1,seq,buttons,at};
    c.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{100,80,80},{-1,0,0},{0,1,0},{0,0,1}};
    return c;
}
inline api::ScriptedEventBinding binding(std::uint16_t slot=901) {
    api::ScriptedEventBinding b;b.index=slot;constexpr std::string_view name="events/firehornet.sc";
    std::copy(name.begin(),name.end(),b.name.begin());return b;
}
inline api::CommittedScriptedEvent event(int mode=0,std::uint32_t emitter=1,
    std::uint64_t record=2,double at=.03) {
    api::CommittedScriptedEvent e;e.generation=1;e.record=e.record_ordinal=record;
    e.emitter_entity=emitter;e.event_index=901;e.message_bit_offset=24;
    e.received_at_seconds=at;e.origin={100,80,52};e.angles={0,180,0};
    e.integer_parameter1=mode;return e;
}
struct Module {
    api::GameClientHost host{games::halflife::make_half_life_client_module()};
    client::RuntimeClientObservationState observed;
    explicit Module(unsigned ammo=8,std::uint32_t clients=8) : observed(state(ammo)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=clients;
        initial.weapon_hud=observed.weapon_hud;host.commit_record(std::move(initial));
        host.bind_model(model());host.observe(observed,0);
        const std::array events{binding()};host.configure_scripted_events(events);
    }
    void ammo(unsigned count,std::size_t record,double time) {
        observed=state(count,record);host.observe(observed,time);
    }
};
}
