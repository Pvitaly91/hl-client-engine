#pragma once
#include "hand_grenade_test_fixture.hpp"
#include <hlclient/games/halflife/snark_profile.hpp>
#include <algorithm>

namespace hlclient::tests::snark_fixture {
namespace api=game_api;
namespace c=client;
namespace h=games::halflife;

inline api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;
    m.generation=1;m.model_index=75;m.resource_revision=2;
    m.resource_name="models/v_squeak.mdl";
    m.supported_bodies[0]=m.selectable_bodies[0]=true;
    constexpr std::array<std::string_view,6> labels{
        "idle1","fidgetfit","fidgetnip","down","up","throw"};
    for(std::size_t i=0;i<labels.size();++i)
        m.sequences.push_back({30,31,i==0,{},std::string(labels[i])});
    return m;
}
inline c::RuntimeClientObservationState state(unsigned ammo=5,std::size_t record=1) {
    auto s=hand_grenade_fixture::state(ammo,record);
    s.weapon_hud.active_weapon_id=15;
    s.receiving_client->owned_weapon_bits=(1U<<15)|(1U<<2);
    s.receiving_client->weapon_animation=4;
    s.weapon_hud.clips[12].reset();s.weapon_hud.clips[15]=-1;
    auto& item=s.weapon_hud.catalogue.front();
    item.id=15;item.command_name="weapon_snark";item.slot=4;item.position=3;item.flags=24;
    auto& slot=s.weapon_slots.front();slot.wire_slot=15;slot.weapon_id=15;
    return s;
}
inline api::LocalWeaponSubmittedCommand command(std::uint32_t sequence=1,double at=.02,
    std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
    auto out=hand_grenade_fixture::command(sequence,at,buttons);
    out.shot_context->player_origin=std::array<float,3>{100,80,52};
    out.shot_context->player_ducked=false;
    return out;
}
inline api::ScriptedEventBinding binding(unsigned index=37) {
    api::ScriptedEventBinding out;out.index=static_cast<std::uint16_t>(index);
    constexpr std::string_view name="events/snarkfire.sc";
    std::copy(name.begin(),name.end(),out.name.begin());return out;
}
inline api::CommittedScriptedEvent event(unsigned record=2,unsigned entity=1,
    unsigned index=37,double at=.03) {
    api::CommittedScriptedEvent out;
    out.generation=1;out.record=out.record_ordinal=record;out.emitter_entity=entity;
    out.event_index=static_cast<std::uint16_t>(index);out.origin={100,80,52};
    out.received_at_seconds=at;out.weapon_model_index=75;return out;
}
struct Query final:api::IStaticEffectTrace {
    api::PlacementTraceResult result{true,false,false,1.0,std::nullopt,{164,80,52},{}};
    mutable unsigned calls{},effect_calls{};
    mutable std::optional<api::PlacementTraceRequest> last;
    api::StaticEffectTrace trace(assets::AssetVector3,assets::AssetVector3) const noexcept override {
        ++effect_calls;return {};
    }
    api::PlacementTraceResult placement_trace(const api::PlacementTraceRequest& request) const noexcept override {
        ++calls;last=request;return result;
    }
};
struct Controller {
    h::LocalWeaponPresentationController controller;
    c::RuntimeClientObservationState observed;
    Query query;
    explicit Controller(unsigned ammo=5):observed(state(ammo)) {
        controller.configure_maximum_clients(8);
        const std::array bindings{binding()};controller.configure_scripted_events(bindings);
        controller.bind_model(model());controller.observe(observed,0);
    }
    void submit(std::uint32_t sequence=1,double at=.02,
        std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
        controller.submit(command(sequence,at,buttons),at);
        controller.prepare_presentation(query,at,1);
    }
    void observe(c::RuntimeClientObservationState next,double at) {
        observed=std::move(next);controller.observe(observed,at);
    }
};
struct Module {
    api::GameClientHost host{h::make_half_life_client_module()};
    c::RuntimeClientObservationState observed;
    Query query;
    explicit Module(unsigned ammo=5):observed(state(ammo)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState initial;initial.multiplayer.maximum_clients=8;
        initial.weapon_hud=observed.weapon_hud;host.commit_record(std::move(initial));
        const std::array bindings{binding()};host.configure_scripted_events(bindings);
        host.bind_model(model());host.observe(observed,0);
    }
    void submit(std::uint32_t sequence=1,double at=.02,
        std::uint16_t buttons=goldsrc::kReferenceGoldSrcButtonAttack) {
        host.submit(command(sequence,at,buttons),at);host.prepare_presentation(query,at);
    }
    void observe(c::RuntimeClientObservationState next,double at) {
        observed=std::move(next);host.observe(observed,at);
    }
};
} // namespace hlclient::tests::snark_fixture
