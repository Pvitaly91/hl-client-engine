#include <hlclient/core/multiplayer_diagnostics.hpp>
#include <hlclient/games/halflife/weapon_diagnostics.hpp>
#include <catch2/catch_test_macros.hpp>
#include "weapon_suite_test_fixture.hpp"
#include <algorithm>
#include <memory>
#include <set>
#include <vector>
#include <cstdlib>
#include <iostream>

namespace {
namespace f=hlclient::tests::weapon_suite_fixture;
namespace d=hlclient::core;
namespace api=hlclient::game_api;
using K=d::MultiplayerActionKind;
using P=d::MultiplayerActionPhase;
struct Sink final:d::MultiplayerDiagnosticSink {
    bool on{true};std::vector<d::MultiplayerDiagnosticEvent> events;
    bool enabled()const noexcept override{return on;}
    bool observe(const d::MultiplayerDiagnosticEvent& event)noexcept override {
        try{events.push_back(event);return true;}catch(...){return false;}
    }
};
void attach(f::Module& module,Sink& sink) {
    module.host.set_diagnostic_sink(&sink);module.observe(0);sink.events.clear();
}
auto accepted(const Sink& sink) {
    std::vector<d::MultiplayerDiagnosticEvent> result;
    for(const auto& event:sink.events)if(const auto* p=std::get_if<d::MultiplayerActionPayload>(&event.payload))
        if((p->phase==P::accepted || p->phase==P::begin) && event.knowledge==d::MultiplayerKnowledge::local_action_accepted)result.push_back(event);
    return result;
}
bool has(const Sink& sink,K kind,P phase,unsigned weapon) {
    return std::ranges::any_of(sink.events,[&](const auto& event){const auto* p=std::get_if<d::MultiplayerActionPayload>(&event.payload);
        return p && p->kind==kind && p->phase==phase && p->weapon_id==weapon;});
}
void coverage(const api::WeaponDiagnosticCapability& capability,unsigned mode,const Sink& sink) {
#ifdef _MSC_VER
    char* flag=nullptr;std::size_t length=0;
    const bool enabled=::_dupenv_s(&flag,&length,"HLCLIENT_WEAPON_DIAGNOSTIC_COVERAGE")==0 && flag!=nullptr;
    std::free(flag);if(!enabled)return;
#else
    if(!std::getenv("HLCLIENT_WEAPON_DIAGNOSTIC_COVERAGE"))return;
#endif
    constexpr std::array names{"primary","secondary","reload","deploy","holster"};
    std::size_t intents=0,actions=0,facts=0,rejections=0;
    for(const auto& event:sink.events)if(const auto* p=std::get_if<d::MultiplayerActionPayload>(&event.payload))
        if(p->weapon_id==capability.weapon_id && p->mode==mode) {
            intents+=p->phase==P::intent;actions+=p->phase==P::accepted || p->phase==P::begin;
            facts+=p->phase==P::server_observed;rejections+=p->phase==P::rejected;
        }
    std::cout<<"[weapon-coverage] | "<<static_cast<unsigned>(capability.weapon_id)<<" | "<<capability.command<<" | "
        <<names.at(mode)<<" | "<<static_cast<unsigned>(capability.modes.at(mode))<<" | "<<intents<<" | "<<actions
        <<" | "<<facts<<" | "<<rejections<<" |\n";
}
void verify_honesty(const Sink& sink) {
    for(const auto& event:sink.events) {
        CHECK(event.knowledge!=d::MultiplayerKnowledge::server_damage_observed);
        CHECK(event.knowledge!=d::MultiplayerKnowledge::server_miss_observed);
        CHECK(event.local_steady_ns>0);
        CHECK(event.identity.network_generation==1);
        CHECK(event.identity.map_generation==1);
        if(const auto* p=std::get_if<d::MultiplayerActionPayload>(&event.payload)) {
            CHECK_FALSE(p->random_seed); // no server RNG is fabricated from a local command
            CHECK_FALSE(p->spread); // no universal spread guessed from event fparam/energy
        }
    }
}
}

TEST_CASE("Weapon diagnostics exercise the actual fourteen-profile primary capability matrix before sampling",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    constexpr std::array<unsigned,14> ids{1,2,3,4,6,7,8,9,10,11,12,13,14,15};
    constexpr std::array kinds{K::melee,K::fire,K::fire,K::fire,K::projectile,K::pellets,K::projectile,
        K::beam,K::beam,K::projectile,K::throw_object,K::place,K::throw_object,K::throw_object};
    std::set<unsigned> exercised;
    for(std::size_t i=0;i<ids.size();++i) {
        CAPTURE(ids[i]);Sink sink;auto module=std::make_unique<f::Module>(static_cast<std::uint8_t>(ids[i]));attach(*module,sink);
        const auto capabilities=module->host.weapon_diagnostic_capabilities();REQUIRE(capabilities.size()==14);
        REQUIRE(capabilities[i].weapon_id==ids[i]);CHECK(capabilities[i].modes[0]==kinds[i]);
        CHECK(capabilities[i].command==f::weapons[i].command);CHECK(capabilities[i].viewmodel==f::weapons[i].viewmodel);
        const auto before=module->observed;
        module->submit(1,1.0);
        const auto events=accepted(sink);REQUIRE(events.size()==1);
        const auto& event=events.front();const auto& payload=std::get<d::MultiplayerActionPayload>(event.payload);
        CHECK(payload.weapon_id==ids[i]);CHECK(payload.kind==kinds[i]);CHECK(payload.mode==0);
        CHECK(event.identity.command_id==1);REQUIRE(event.identity.action_id);CHECK(event.edge==d::MultiplayerEdgeConfidence::exact);
        CHECK(payload.eligible==true);REQUIRE(payload.origin);REQUIRE(payload.direction);CHECK(payload.command_sample_seconds==1.0);
        CHECK(payload.origin==std::optional{std::array<float,3>{100,80,80}});
        CHECK(module->observed.weapon_hud==before.weapon_hud);
        CHECK(module->observed.receiving_client==before.receiving_client);
        module->submit(1,1.0);(void)module->sample(1.0);(void)module->sample(1.01);(void)module->visuals(1.01);
        CHECK(accepted(sink).size()==1);verify_honesty(sink);exercised.insert(ids[i]);
        coverage(capabilities[i],0,sink);
    }
    CHECK(exercised.size()==14);
}

TEST_CASE("Weapon diagnostics enumerate secondary modes including unsupported intent and non-firing zoom guidance",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    constexpr std::array kinds{K::unknown,K::fire,K::zoom,K::projectile,K::zoom,K::pellets,K::guidance,
        K::charge,K::unknown,K::projectile,K::unknown,K::unknown,K::detonate,K::unknown};
    std::size_t rows=0;
    for(std::size_t i=0;i<f::weapons.size();++i) {
        const auto id=f::weapons[i].id;CAPTURE(id);Sink sink;auto module=std::make_unique<f::Module>(id);
        if(id==14){f::slot(module->observed,14).auxiliary_integer1=1;module->adopt(14,.1,2,true);}
        attach(*module,sink);
        CHECK(module->host.weapon_diagnostic_capabilities()[i].modes[1]==kinds[i]);
        module->submit(1,1.0,1U<<11);
        CHECK(has(sink,kinds[i],P::intent,id));
        const auto actions=accepted(sink);
        if(kinds[i]==K::unknown || kinds[i]==K::zoom || kinds[i]==K::guidance)CHECK(actions.empty());
        else {REQUIRE(actions.size()==1);const auto& p=std::get<d::MultiplayerActionPayload>(actions[0].payload);
            CHECK(p.kind==kinds[i]);CHECK(p.mode==1);CHECK(actions[0].identity.command_id==1);}
        if(kinds[i]==K::zoom) {
            f::select(module->observed,id,3);module->observed.receiving_client->field_of_view=20;module->observe(1.1);
            CHECK(has(sink,K::zoom,P::server_observed,id));CHECK(accepted(sink).empty());
        }
        verify_honesty(sink);++rows;
        coverage(module->host.weapon_diagnostic_capabilities()[i],1,sink);
    }
    CHECK(rows==14);
}

TEST_CASE("Weapon diagnostics reload deploy and holster coverage comes through production host hooks for every profile",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    std::set<unsigned> reloads,deploys,holsters;
    for(const auto& weapon:f::weapons) {
        CAPTURE(weapon.id);Sink sink;auto module=std::make_unique<f::Module>(weapon.id);attach(*module,sink);
        if(weapon.clip>=0) {
            auto& state=module->observed;f::select(state,weapon.id,2);f::slot(state,weapon.id).clip=weapon.clip-1;
            state.weapon_hud.clips[weapon.id]=weapon.clip-1;module->observe(.1);sink.events.clear();
        }
        module->submit(1,1.0,1U<<13);
        if(weapon.clip>=0) {CHECK(has(sink,K::reload,P::accepted,weapon.id));reloads.insert(weapon.id);}
        else {CHECK(has(sink,K::unknown,P::intent,weapon.id));CHECK(accepted(sink).empty());}
        const auto capabilities=module->host.weapon_diagnostic_capabilities();
        const auto capability=std::ranges::find(capabilities,weapon.id,&api::WeaponDiagnosticCapability::weapon_id);REQUIRE(capability!=capabilities.end());
        coverage(*capability,2,sink);
        const auto other=weapon.id==1 ? std::uint8_t{2} : std::uint8_t{1};
        module->adopt(other,2,3);CHECK(has(sink,K::holster,P::server_observed,weapon.id));holsters.insert(weapon.id);
        module->adopt(weapon.id,3,4);CHECK(has(sink,K::deploy,P::server_observed,weapon.id));deploys.insert(weapon.id);
        coverage(*capability,3,sink);coverage(*capability,4,sink);
        verify_honesty(sink);
    }
    CHECK(reloads==std::set<unsigned>{2,3,4,6,7,8});CHECK(deploys.size()==14);CHECK(holsters.size()==14);
}

TEST_CASE("Held automatic shots have distinct exact diagnostic actions without new mouse edges or Draw calls",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    Sink sink;auto module=std::make_unique<f::Module>(4);attach(*module,sink);
    module->submit(1,1.0);module->submit(2,1.11);module->submit(3,1.22);
    const auto events=accepted(sink);REQUIRE(events.size()==3);std::set<std::uint64_t> ids;
    for(std::size_t i=0;i<events.size();++i) {CHECK(events[i].identity.command_id==i+1);REQUIRE(events[i].identity.action_id);ids.insert(*events[i].identity.action_id);}
    CHECK(ids.size()==3);module->submit(3,1.22);(void)module->sample(1.23);CHECK(accepted(sink).size()==3);
    verify_honesty(sink);
}

TEST_CASE("Charge release and continuous beam stop preserve bounded diagnostic lifecycle identities",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    SECTION("Gauss charge and discharge are independent actual actions") {
        Sink sink;auto module=std::make_unique<f::Module>(9);attach(*module,sink);
        module->submit(1,1.0,1U<<11);module->submit(2,1.6,1U<<11);module->submit(3,1.7,0);
        CHECK(has(sink,K::charge,P::begin,9));CHECK(has(sink,K::charge,P::end,9));CHECK(has(sink,K::beam,P::accepted,9));
        const auto actions=accepted(sink);REQUIRE(actions.size()==2);CHECK(actions[0].identity.action_id!=actions[1].identity.action_id);
        CHECK(actions[0].identity.command_id==1);CHECK(actions[1].identity.command_id==3);verify_honesty(sink);
    }
    SECTION("Egon repeats held commands without inventing a new continuous action") {
        Sink sink;auto module=std::make_unique<f::Module>(10);attach(*module,sink);
        module->submit(1,1.0);module->submit(2,1.1);module->submit(3,1.2,0);
        CHECK(has(sink,K::beam,P::begin,10));CHECK(has(sink,K::beam,P::end,10));CHECK(accepted(sink).size()==1);verify_honesty(sink);
    }
    SECTION("Grenade prime then release is observed through the controller, not simulated by logging") {
        Sink sink;auto module=std::make_unique<f::Module>(12);attach(*module,sink);
        module->submit(1,1.0);module->submit(2,1.6,0);module->submit(3,1.7,0);
        CHECK(has(sink,K::throw_object,P::begin,12));CHECK(has(sink,K::throw_object,P::accepted,12));verify_honesty(sink);
    }
}

TEST_CASE("Weapon diagnostics are optional and do not turn empty unsupported or dead inputs into accepted fire",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    SECTION("disabled sink") {Sink sink;sink.on=false;auto module=std::make_unique<f::Module>(4);attach(*module,sink);
        module->submit(1,1.0);CHECK(sink.events.empty());CHECK(module->sample(1.01).actions_started==1);}
    SECTION("empty without reserve") {Sink sink;auto module=std::make_unique<f::Module>(4);attach(*module,sink);
        f::select(module->observed,4,2);f::slot(module->observed,4).clip=0;module->observed.weapon_hud.clips[4]=0;
        module->observed.weapon_hud.reserve_ammo[1]=0;module->observe(.1);sink.events.clear();module->submit(1,1.0);
        CHECK(accepted(sink).empty());CHECK(has(sink,K::fire,P::intent,4));verify_honesty(sink);}
    SECTION("generation mismatch and repeated command") {Sink sink;auto module=std::make_unique<f::Module>(4);attach(*module,sink);
        auto command=f::command(1,1.0);command.generation=2;module->host.submit(command,1.0);CHECK(sink.events.empty());
        module->host.set_diagnostic_sink(nullptr);module->submit(2,1.1);CHECK(sink.events.empty());}
}

TEST_CASE("Persistent weapon appearances log visibility lifetimes and only explicit server owner references",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    Sink sink;auto module=std::make_unique<f::Module>();attach(*module,sink);
    module->host.bind_effect_resource({api::EffectResourceBinding::Kind::model,42,"models/w_satchel.mdl"});
    hlclient::client::RuntimePacketEntityObservation object;object.entity_number=70;object.model_index=42;object.origin={12.,24.,36.};
    module->host.remote_entities(std::span{&object,1},8,10);
    REQUIRE(sink.events.size()==1);const auto first=sink.events.front();
    const auto& enter=std::get<d::MultiplayerLifecyclePayload>(first.payload);
    CHECK(enter.kind==d::MultiplayerLifecycleKind::visibility_enter);CHECK(enter.model_index==42);
    CHECK(enter.object_profile==static_cast<unsigned>(api::WeaponDiagnosticObjectProfile::satchel));
    CHECK_FALSE(enter.owner_entity);CHECK_FALSE(first.identity.related_action_id);CHECK_FALSE(first.identity.related_actor_entity);
    module->host.remote_entities(std::span{&object,1},8,10);module->host.remote_entities(std::span{&object,1},8,11);CHECK(sink.events.size()==1);
    object.owner_entity_reference=2;module->host.remote_entities(std::span{&object,1},8,12);REQUIRE(sink.events.size()==2);
    CHECK(std::get<d::MultiplayerLifecyclePayload>(sink.events.back().payload).owner_entity==2);
    CHECK(sink.events.back().identity.related_actor_entity==2);CHECK_FALSE(sink.events.back().identity.related_action_id);
    module->host.remote_entities({},8,13);REQUIRE(sink.events.size()==3);
    CHECK(std::get<d::MultiplayerLifecyclePayload>(sink.events.back().payload).kind==d::MultiplayerLifecycleKind::visibility_leave);
    module->host.remote_entities(std::span{&object,1},8,14);REQUIRE(sink.events.size()==4);
    CHECK(sink.events.back().identity.actor_visual_lifetime!=first.identity.actor_visual_lifetime);
    for(const auto& event:sink.events) {const auto& p=std::get<d::MultiplayerLifecyclePayload>(event.payload);
        CHECK(p.kind!=d::MultiplayerLifecycleKind::object_spawn);CHECK(p.kind!=d::MultiplayerLifecycleKind::object_remove);}
    sink.events.clear();object.model_index=43;module->host.remote_entities(std::span{&object,1},8,15);
    REQUIRE(sink.events.size()==1);CHECK(std::get<d::MultiplayerLifecyclePayload>(sink.events[0].payload).visible==false);
    module->host.remote_entities(std::span{&object,1},8,16);CHECK(sink.events.size()==1);verify_honesty(sink);
}

TEST_CASE("Owning controller rejection gates are diagnostic facts and repeated reasons are bounded",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    const auto reason_seen=[](const Sink& sink,std::string_view reason) {
        return std::ranges::any_of(sink.events,[&](const auto& event){const auto* p=std::get_if<d::MultiplayerActionPayload>(&event.payload);
            return p && p->phase==P::rejected && std::string_view{p->reason.data()}==reason && p->eligible==false;});
    };
    for(const auto gate:{"timer","reload","ammo","dead"}) {
        CAPTURE(gate);Sink sink;auto module=std::make_unique<f::Module>(4);attach(*module,sink);
        auto& state=module->observed;f::select(state,4,2);
        std::string_view expected;
        if(std::string_view{gate}=="timer") {f::slot(state,4).next_primary_attack=10;expected="local_attack_timer_gate";}
        if(std::string_view{gate}=="reload") {f::slot(state,4).in_reload=true;expected="local_reload_gate";}
        if(std::string_view{gate}=="ammo") {f::slot(state,4).clip=0;state.weapon_hud.clips[4]=0;state.weapon_hud.reserve_ammo[1]=0;expected="local_ammo_gate";}
        if(std::string_view{gate}=="dead") {state.receiving_client->health=0;state.lifecycle.state=hlclient::client::LocalPlayerLifeState::dead;expected="local_dead";}
        module->observe(.1);sink.events.clear();
        for(std::uint32_t sequence=1;sequence<=50;++sequence)module->submit(sequence,1+sequence*.001);
        CHECK(accepted(sink).empty());CHECK(reason_seen(sink,expected));
        const auto count=std::ranges::count_if(sink.events,[](const auto& e){const auto* p=std::get_if<d::MultiplayerActionPayload>(&e.payload);return p && p->phase==P::rejected;});
        CHECK(count<=2);verify_honesty(sink);
    }
    SECTION("Gauss short repeat is rejected by actual local timer") {
        Sink sink;auto module=std::make_unique<f::Module>(9);attach(*module,sink);
        module->submit(1,1.0);module->submit(2,1.01);CHECK(accepted(sink).size()==1);
        CHECK(reason_seen(sink,"local_attack_timer_gate"));
    }
    SECTION("deferred placement is not rejected before its query accepts") {
        for(const auto id:{13U,15U}) {Sink sink;auto module=std::make_unique<f::Module>(static_cast<std::uint8_t>(id));attach(*module,sink);
            module->submit(1,1.0);CHECK(accepted(sink).size()==1);
            CHECK_FALSE(std::ranges::any_of(sink.events,[](const auto& e){const auto* p=std::get_if<d::MultiplayerActionPayload>(&e.payload);return p && p->phase==P::rejected;}));}
    }
}

TEST_CASE("Persistent object diagnostic capacity produces explicit omissions and never fabricates owners",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    Sink sink;auto module=std::make_unique<f::Module>();attach(*module,sink);
    module->host.bind_effect_resource({api::EffectResourceBinding::Kind::model,42,"models/rpgrocket.mdl"});
    std::array<hlclient::client::RuntimePacketEntityObservation,130> objects;
    for(std::size_t i=0;i<objects.size();++i){objects[i].entity_number=static_cast<std::uint32_t>(70+i);objects[i].model_index=42;}
    module->host.remote_entities(objects,8,1);REQUIRE(sink.events.size()==129);
    const auto& omitted=std::get<d::MultiplayerLifecyclePayload>(sink.events.back().payload);CHECK(omitted.omitted_objects==2);
    for(std::size_t i=0;i<128;++i){CHECK_FALSE(sink.events[i].identity.related_actor_entity);CHECK_FALSE(sink.events[i].identity.related_action_id);}
    sink.events.clear();module->host.remote_entities(objects,8,2);REQUIRE(sink.events.size()==1);
    CHECK(std::get<d::MultiplayerLifecyclePayload>(sink.events.front().payload).omitted_objects==2);
}

TEST_CASE("Committed sound temporary and scripted sources are exact deduplicated observations not hit acknowledgements",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    Sink sink;auto module=std::make_unique<f::Module>();attach(*module,sink);
    api::CommittedSoundObservation sound;sound.generation=1;sound.record=50;sound.record_ordinal=7;
    sound.message_bit_offset=72;sound.entry_ordinal=2;sound.entity=4;sound.sample_slot=99;sound.channel=2;sound.flags=16;
    sound.received_at_seconds=1.25;sound.origin={12,24,36};
    module->host.committed_sound(sound);module->host.committed_sound(sound);
    REQUIRE(sink.events.size()==1);const auto& sound_event=sink.events[0];
    CHECK(sound_event.identity.source_record==50);CHECK(sound_event.identity.source_record_ordinal==7);
    CHECK(sound_event.identity.source_message_bit_offset==72);CHECK(sound_event.identity.source_entry_ordinal==2);
    CHECK(sound_event.identity.actor_entity==4);CHECK_FALSE(sound_event.identity.action_id);CHECK_FALSE(sound_event.identity.related_action_id);
    const auto& sound_payload=std::get<d::MultiplayerActionPayload>(sound_event.payload);
    CHECK(sound_payload.source_kind==3);CHECK(sound_payload.source_resource_slot==99);CHECK(sound_payload.source_channel==2);
    CHECK(sound_payload.source_received_steady_seconds==1.25);CHECK_FALSE(sound_event.server_seconds);
    hlclient::client::CommittedTemporaryEffect temporary;temporary.generation=1;temporary.record=50;temporary.record_ordinal=7;
    temporary.message_bit_offset=72;temporary.entry_ordinal=2;temporary.received_at_seconds=1.25;
    temporary.kind=hlclient::client::TemporaryEffectKind::sparks;temporary.origin={12,24,36};
    // Distinct typed source classes do not collide even at the same source tuple.
    module->host.committed_temporary_effect(temporary);module->host.committed_temporary_effect(temporary);
    REQUIRE(sink.events.size()==2);const auto& spark=std::get<d::MultiplayerActionPayload>(sink.events.back().payload);
    CHECK(spark.source_kind==2);CHECK_FALSE(spark.source_resource_slot);CHECK_FALSE(spark.source_flags);
    CHECK_FALSE(sink.events.back().identity.actor_entity);
    api::CommittedScriptedEvent scripted;scripted.generation=1;scripted.record=50;scripted.record_ordinal=7;
    scripted.message_bit_offset=72;scripted.entry_ordinal=2;scripted.received_at_seconds=1.25;scripted.event_index=65500;
    scripted.emitter_entity=4;scripted.origin={12,24,36};
    module->host.committed_scripted_event(scripted);module->host.committed_scripted_event(scripted);
    REQUIRE(sink.events.size()==3);CHECK(std::get<d::MultiplayerActionPayload>(sink.events.back().payload).source_kind==1);
    CHECK(sink.events.back().identity.source_event==65500);
    ++temporary.entry_ordinal;module->host.committed_temporary_effect(temporary);REQUIRE(sink.events.size()==4);
    sound.generation=2;module->host.committed_sound(sound);CHECK(sink.events.size()==4);
    for(const auto& event:sink.events){CHECK(event.knowledge==d::MultiplayerKnowledge::server_event_observed);CHECK_FALSE(event.identity.action_id);}
    verify_honesty(sink);
}

TEST_CASE("Incoming committed damage is a receiving-player fact without attacker or shot attribution",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    Sink sink;auto module=std::make_unique<f::Module>();attach(*module,sink);
    api::GameRecordState record;record.weapon_hud=module->observed.weapon_hud;record.lifecycle=module->observed.lifecycle;
    hlclient::client::RuntimeLifeEvent damage;damage.kind=hlclient::client::RuntimeLifeEventKind::damage;
    damage.source=f::source(10);damage.source.start_bit_offset=80;damage.source.end_bit_offset=168;
    damage.damage_taken=12;damage.damage_origin={100.,200.,300.};
    record.life_events={damage};record.lifecycle.last_damage_source=damage.source;
    record.lifecycle.health_before_damage=100;record.lifecycle.health_after_damage=88;
    auto duplicate=record;module->host.commit_record(std::move(record));module->host.commit_record(std::move(duplicate));
    REQUIRE(sink.events.size()==1);const auto& event=sink.events.front();
    CHECK(event.knowledge==d::MultiplayerKnowledge::server_damage_observed);CHECK(event.identity.actor_entity==1);
    CHECK_FALSE(event.identity.action_id);CHECK_FALSE(event.identity.related_action_id);CHECK_FALSE(event.identity.related_actor_entity);
    CHECK(event.identity.source_record==10);CHECK(event.identity.source_message_bit_offset==80);
    const auto& p=std::get<d::MultiplayerLifecyclePayload>(event.payload);CHECK(p.kind==d::MultiplayerLifecycleKind::damage);
    CHECK(p.before==100);CHECK(p.after==88);CHECK_FALSE(p.owner_entity);
}

TEST_CASE("An arbitrary server FOV change is not invented as an unsupported weapon zoom",
    "[multiplayer-diagnostics][weapon-diagnostics][no-stock]") {
    Sink sink;auto module=std::make_unique<f::Module>(4);attach(*module,sink);
    f::select(module->observed,4,2);module->observed.receiving_client->field_of_view=70;module->observe(.1);
    CHECK_FALSE(has(sink,K::zoom,P::server_observed,4));CHECK(has(sink,K::unknown,P::server_observed,4));verify_honesty(sink);
}
