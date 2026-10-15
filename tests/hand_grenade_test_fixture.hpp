#pragma once
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
namespace hlclient::tests::hand_grenade_fixture {
namespace api=game_api;
inline api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=75;m.resource_revision=2;
    m.resource_name="models/v_grenade.mdl";m.supported_bodies[0]=m.selectable_bodies[0]=true;
    constexpr std::array<std::string_view,8> labels{"idle","fidget","pinpull","throw1","throw2","throw3","holster","draw"};
    constexpr std::array<unsigned,8> frames{91,76,25,19,19,19,16,16};
    constexpr std::array<double,8> fps{30,30,45,13,20,30,30,30};
    for(std::size_t i=0;i<labels.size();++i) m.sequences.push_back({fps[i],frames[i],i<2,{},std::string(labels[i])});
    return m;
}
inline client::RuntimeClientObservationState state(unsigned ammo=5,std::size_t record=1) {
    client::RuntimeClientObservationState s;s.generation=1;s.publication_revision=record;s.server_time_seconds=100;
    s.client_metadata={.generation=1,.freshness=client::RuntimeObservationFreshness::observed_in_record,
        .source=client::RuntimeObservationSource{.record_identity=record,.record_ordinal=record}};
    s.receiving_client.emplace();auto& c=*s.receiving_client;
    c.health=100;c.viewmodel_index=75;c.owned_weapon_bits=(1U<<12)|(1U<<2);
    c.next_weapon_attack=0;c.weapon_animation=0;c.water_level=0;
    c.origin={100.0,80.0,52.0};c.view_offset={0.0,0.0,28.0};
    c.punch_angle={0.0,0.0,0.0};c.velocity={0.0,0.0,0.0};
    s.weapon_hud.active_weapon_id=12;s.weapon_hud.active_source=s.client_metadata.source;
    s.weapon_hud.last_message_source=s.client_metadata.source;
    s.weapon_hud.clips[12]=-1;s.weapon_hud.reserve_ammo[4]=static_cast<std::uint8_t>(ammo);
    s.weapon_hud.reserve_ammo_sources[4]=s.client_metadata.source;
    s.weapon_hud.catalogue.push_back({.id=12,.command_name="weapon_handgrenade",.primary_ammo_type=4,
        .slot=4,.position=0,.source=*s.client_metadata.source});
    s.weapon_hud.catalogue.push_back({.id=2,.command_name="weapon_9mmhandgun",.primary_ammo_type=1,
        .slot=1,.position=0,.source=*s.client_metadata.source});
    s.weapon_slots.push_back({.wire_slot=12,.clip=-1,.in_reload=false,.next_reload=0,
        .next_primary_attack=0,.weapon_id=12,.next_secondary_attack=0,.time_weapon_idle=0,
        .auxiliary_float2=0,.auxiliary_float3=-1});
    return s;
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t seq=1,double at=.02,
    std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack,float pitch=0) {
    api::LocalWeaponSubmittedCommand c{1,seq,buttons,at};
    c.shot_context.emplace();c.shot_context->eye={100,80,80};c.shot_context->direction={1,0,0};
    c.shot_context->view_angles_degrees=std::array<float,3>{pitch,0,0};
    c.shot_context->punch_angles_degrees=std::array<float,3>{0,0,0};
    c.shot_context->player_velocity=std::array<float,3>{0,0,0};return c;
}
struct Controller {
    games::halflife::LocalWeaponPresentationController controller;
    client::RuntimeClientObservationState observed;
    explicit Controller(unsigned ammo=5):observed(state(ammo)) {
        controller.configure_maximum_clients(8);controller.bind_model(model());controller.observe(observed,0);
    }
    void submit(std::uint32_t seq,double at,std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack,float pitch=0) {
        controller.submit(command(seq,at,buttons,pitch),at);
    }
};
struct Module {
    api::GameClientHost host{games::halflife::make_half_life_client_module()};
    client::RuntimeClientObservationState observed;
    explicit Module(unsigned ammo=5):observed(state(ammo)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=8;initial.weapon_hud=observed.weapon_hud;
        host.commit_record(std::move(initial));host.bind_model(model());host.observe(observed,0);
    }
};
}
