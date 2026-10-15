#pragma once
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include "effect_palette_test_fixture.hpp"

namespace hlclient::tests::snark_sound_pop_fixture {
namespace api=game_api;
inline void bind(api::GameClientHost& host,unsigned sound=54,bool sprites=true) {
    using Kind=api::EffectResourceBinding::Kind;
    host.bind_effect_palette(effect_palette_fixture::metadata());
    host.bind_effect_resource({Kind::model,237,"models/w_squeak.mdl"});
    host.bind_effect_resource({Kind::sound,static_cast<std::uint16_t>(sound),"squeek/sqk_blast1.wav"});
    if(sprites) {
        host.bind_effect_resource({Kind::model,321,"sprites/bloodspray.spr"});
        host.bind_effect_resource({Kind::model,322,"sprites/blood.spr"});
    }
}
inline client::RuntimePacketEntityObservation creature(unsigned entity=40,unsigned model=237) {
    client::RuntimePacketEntityObservation result;result.entity_number=entity;
    result.model_index=model;result.ordinary_visual_schema=true;return result;
}
inline void entities(api::GameClientHost& host,unsigned ordinal=1,unsigned entity=40,unsigned model=237) {
    const std::array list{creature(entity,model)};host.remote_entities(list,8,ordinal);
}
inline api::CommittedSoundObservation pop(unsigned record=2,unsigned entity=40,double time=0) {
    api::CommittedSoundObservation result;result.generation=1;result.record=result.record_ordinal=record;
    result.message_bit_offset=48;result.entity=entity;result.sample_slot=54;
    result.channel=3;result.origin={0,0,0};result.received_at_seconds=time;return result;
}
inline client::CommittedTemporaryEffect blood(unsigned record=3,double time=0) {
    client::CommittedTemporaryEffect result;result.generation=1;result.record=result.record_ordinal=record;
    result.message_bit_offset=96;result.kind=client::TemporaryEffectKind::blood_sprite;
    result.blood_sprite_indices={321,322};result.palette_color=195;result.blood_scale=16;
    result.received_at_seconds=time;return result;
}
}
