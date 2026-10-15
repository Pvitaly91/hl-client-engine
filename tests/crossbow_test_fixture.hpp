#pragma once
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
namespace hlclient::tests::crossbow_fixture {
namespace api=game_api;
inline api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=73;m.resource_revision=2;
    m.resource_name="models/v_crossbow.mdl";
    m.supported_bodies[0]=m.supported_bodies[1]=true;
    m.selectable_bodies[0]=m.selectable_bodies[1]=true;
    constexpr std::array labels{"idle1","idle2","fidget1","fidget2","fire1","fire2","fire3",
        "reload","draw1","draw2","holster1","holster2"};
    constexpr std::array frames{91U,31U,81U,91U,111U,166U,16U,136U,16U,16U,16U,16U};
    for(std::size_t i=0;i<labels.size();++i) m.sequences.push_back({i==4 ? 60.0 : 30.0,
        frames[i],i<2,{},labels[i]});return m;
}
inline client::RuntimeClientObservationState state(int clip=5,std::size_t record=1,int reserve=10) {
    client::RuntimeClientObservationState s;s.generation=1;s.publication_revision=record;
    s.client_metadata={.generation=1,.freshness=client::RuntimeObservationFreshness::observed_in_record,
        .source=client::RuntimeObservationSource{.record_identity=record,.record_ordinal=record}};
    s.receiving_client.emplace();auto& c=*s.receiving_client;c.health=100;c.viewmodel_index=73;
    c.owned_weapon_bits=1U<<6;c.next_weapon_attack=0;c.weapon_animation=0;c.water_level=0;
    c.origin={100.0,80.0,52.0};c.view_offset={0.0,0.0,28.0};c.punch_angle={0.0,0.0,0.0};
    s.weapon_hud.active_weapon_id=6;s.weapon_hud.active_source=s.client_metadata.source;
    s.weapon_hud.clips[6]=static_cast<std::int16_t>(clip);s.weapon_hud.reserve_ammo[1]=static_cast<std::uint8_t>(reserve);
    s.weapon_hud.catalogue.push_back({.id=6,.command_name="weapon_crossbow",.primary_ammo_type=1,
        .slot=2,.position=2,.source=*s.client_metadata.source});
    s.weapon_slots.push_back({.wire_slot=6,.clip=clip,.in_reload=false,.next_reload=0,
        .next_primary_attack=0,.weapon_id=6,.next_secondary_attack=0});return s;
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t seq=1,double at=.02,
    std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
    api::LocalWeaponSubmittedCommand c{1,seq,buttons,at};
    c.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{100,80,80},{-1,0,0},{0,1,0},{0,0,1}};return c;
}
inline api::ScriptedEventBinding binding(bool sniper=false,std::uint16_t slot=901) {
    api::ScriptedEventBinding b;b.index=slot;
    const std::string_view name=sniper ? "events/crossbow2.sc" : "events/crossbow1.sc";
    std::copy(name.begin(),name.end(),b.name.begin());return b;
}
inline api::CommittedScriptedEvent event(bool sniper=false,std::uint32_t emitter=1,
    std::uint64_t record=2,double at=.03,int clip=4,int reserve=10) {
    api::CommittedScriptedEvent e;e.generation=1;e.record=e.record_ordinal=record;
    e.emitter_entity=emitter;e.event_index=sniper ? 902 : 901;e.message_bit_offset=24;
    e.received_at_seconds=at;e.origin={100,80,52};e.angles={0,180,0};
    e.integer_parameter1=clip;e.integer_parameter2=reserve;return e;
}
struct Module {
    api::GameClientHost host{games::halflife::make_half_life_client_module()};
    client::RuntimeClientObservationState observed{state()};
    explicit Module(std::uint32_t clients=8) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=clients;
        initial.weapon_hud=observed.weapon_hud;host.commit_record(std::move(initial));
        host.bind_model(model());host.observe(observed,0);
        const std::array bindings{binding(),binding(true,902)};host.configure_scripted_events(bindings);
    }
    void zoom(double time=.001) {
        observed=state(5,2);observed.receiving_client->field_of_view=20;
        host.observe(observed,time);
    }
};
}
