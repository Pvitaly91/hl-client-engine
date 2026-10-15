#pragma once
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
namespace hlclient::tests::python_fixture {
namespace api=game_api;
inline bool same(assets::AssetVector3 a,assets::AssetVector3 b) noexcept {
    return a.x==b.x && a.y==b.y && a.z==b.z;
}
inline api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=73;m.resource_revision=2;
    m.resource_name="models/v_357.mdl";m.supported_bodies[0]=m.supported_bodies[1]=true;
    m.selectable_bodies[0]=m.selectable_bodies[1]=true;
    constexpr std::array labels{"idle1","fidget1","fire1","reload","holster","draw","idle2","idle3"};
    constexpr std::array frames{71U,171U,31U,111U,16U,16U,61U,89U};
    for(std::size_t i=0;i<labels.size();++i) m.sequences.push_back({i==3 ? 36.0 : 30.0,
        frames[i],i==0 || i>=6,{},labels[i]});
    m.sequences[2].events.push_back({0,5001,0,{std::byte{'3'},std::byte{'1'}}});
    assets::ModelSequenceEvent reload;reload.frame=70;reload.event_number=5004;
    for(const char c:std::string_view{"weapons/357_reload1.wav"}) reload.options.push_back(static_cast<std::byte>(c));
    m.sequences[3].events.push_back(std::move(reload));return m;
}
inline client::RuntimeClientObservationState state(int clip=6,std::size_t record=1) {
    client::RuntimeClientObservationState s;s.generation=1;s.publication_revision=record;
    s.client_metadata={.generation=1,.freshness=client::RuntimeObservationFreshness::observed_in_record,
        .source=client::RuntimeObservationSource{.record_identity=record,.record_ordinal=record}};
    s.receiving_client.emplace();auto& c=*s.receiving_client;c.health=100;c.viewmodel_index=73;
    c.owned_weapon_bits=1U<<3;c.next_weapon_attack=0;c.weapon_animation=0;c.water_level=0;
    c.origin={100.0,80.0,52.0};c.view_offset={0.0,0.0,28.0};c.punch_angle={0.0,0.0,0.0};
    s.weapon_hud.active_weapon_id=3;s.weapon_hud.active_source=s.client_metadata.source;
    s.weapon_hud.clips[3]=static_cast<std::int16_t>(clip);s.weapon_hud.reserve_ammo[1]=18;
    s.weapon_hud.catalogue.push_back({.id=3,.command_name="weapon_357",.primary_ammo_type=1,
        .slot=1,.position=1,.source=*s.client_metadata.source});
    s.weapon_slots.push_back({.wire_slot=3,.clip=clip,.in_reload=false,.next_reload=0,
        .next_primary_attack=0,.weapon_id=3,.next_secondary_attack=0});return s;
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t seq=1,double at=.02,
    std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
    api::LocalWeaponSubmittedCommand c{1,seq,buttons,at};
    c.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{100,80,80},{-1,0,0},{0,1,0},{0,0,1}};return c;
}
inline api::ScriptedEventBinding binding(std::uint16_t slot=927) {
    api::ScriptedEventBinding b;b.index=slot;
    constexpr std::string_view name="events/python.sc";std::copy(name.begin(),name.end(),b.name.begin());return b;
}
inline api::CommittedScriptedEvent event(std::uint32_t emitter=1,std::uint64_t record=2,double at=.03) {
    api::CommittedScriptedEvent e;e.generation=1;e.record=e.record_ordinal=record;
    e.emitter_entity=emitter;e.event_index=927;e.message_bit_offset=24;
    e.received_at_seconds=at;e.origin={100,80,52};e.angles={0,180,0};return e;
}
struct Module {
    api::GameClientHost host{games::halflife::make_half_life_client_module()};
    client::RuntimeClientObservationState observed{state()};
    explicit Module(std::uint32_t maximum_clients=8) {
        host.reset({1,1,1});
        host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=maximum_clients;
        initial.weapon_hud=observed.weapon_hud;initial.lifecycle=observed.lifecycle;
        host.commit_record(std::move(initial));host.bind_model(model());host.observe(observed,0);
        const std::array bindings{binding()};host.configure_scripted_events(bindings);
    }
};
}
