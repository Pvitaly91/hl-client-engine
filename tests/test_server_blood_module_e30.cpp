#include "snark_sound_pop_test_fixture.hpp"
#include <hlclient/games/halflife/blood_decal_profile.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <limits>
#include <string>

namespace {
namespace api=hlclient::game_api;
namespace c=hlclient::client;
namespace f=hlclient::tests::snark_sound_pop_fixture;
using Kind=api::EffectResourceBinding::Kind;
struct Module {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    c::RuntimeClientObservationState observation;
    Module() {host.reset({1,1,1});host.set_movement_audio_time_origin(0);observation.generation=1;}
    void bind(std::uint16_t index,std::string_view name) {host.bind_effect_resource({Kind::decal,index,name});}
};
c::CommittedTemporaryEffect decal(std::uint64_t record=1,std::int32_t index=0,
    std::int16_t entity=0,std::uint8_t subtype=116) {
    c::CommittedTemporaryEffect effect;effect.kind=c::TemporaryEffectKind::decal;
    effect.generation=1;effect.record=record;effect.record_ordinal=static_cast<std::size_t>(record);
    effect.message_bit_offset=64;effect.origin={11,-23,47};
    effect.decal_index=index;effect.decal_entity=entity;effect.decal_subtype=subtype;return effect;
}
std::string_view token(const auto& bytes) {
    const auto end=std::find(bytes.begin(),bytes.end(),'\0');
    return {bytes.data(),static_cast<std::size_t>(end-bytes.begin())};
}
void commit_messages(Module& module,std::span<const api::GameMessageView> messages,
    std::optional<double> at=0) {
    auto staged=module.host.stage_record({nullptr,module.observation,messages,1,8,{},at});
    INFO(staged.error.value_or(""));REQUIRE(staged);module.host.commit_record(std::move(*staged.state));
}
}

TEST_CASE("E30 compiled surface dependencies are original twelve named blood entries not precache indices",
    "[e30][surface-decal][module][resources][no-stock]") {
    Module module;const auto requests=module.host.surface_decal_preparation();REQUIRE(requests.size()==12);
    constexpr std::array<std::string_view,12> names{"{blood1","{blood2","{blood3","{blood4","{blood5","{blood6",
        "{yblood1","{yblood2","{yblood3","{yblood4","{yblood5","{yblood6"};
    for(std::size_t i=0;i<names.size();++i) {
        CHECK(token(requests[i].wad_name)=="decals.wad");CHECK(token(requests[i].texture_name)==names[i]);
        CHECK(requests[i].material_mode==api::LocalDecalMaterialMode::lit_straight_alpha);
    }
    CHECK(module.host.surface_decal_preparation().data()==requests.data());
    module.host.cancel_uncommitted();CHECK(module.host.surface_decal_preparation().size()==12);
}
TEST_CASE("E30 permuted actual server decal names select matching assets including index zero high and sparse indices",
    "[e30][surface-decal][module][mapping][no-stock]") {
    constexpr std::array<std::uint16_t,12> indices{0,511,4000,7,13,255,256,777,1234,4,3999,4095};
    constexpr std::array<std::string_view,12> names{"{blood1","{blood2","{blood3","{blood4","{blood5","{blood6",
        "{yblood1","{yblood2","{yblood3","{yblood4","{yblood5","{yblood6"};
    for(const bool reverse:{false,true}) {
        Module module;for(std::size_t i=0;i<names.size();++i)module.bind(indices[i],names[reverse ? names.size()-1-i : i]);
        for(std::size_t i=0;i<names.size();++i) {
            auto event=decal(i+1,indices[i]);module.host.committed_temporary_effect(event);
            const auto batch=module.host.drain_temporary_effects(0);REQUIRE(batch.count==1);const auto& intent=batch.effects[0];
            REQUIRE(intent.surface_decal);CHECK(intent.surface_decal->asset_slot==(reverse ? names.size()-1-i : i));
            CHECK(intent.surface_decal->server_index==indices[i]);CHECK(intent.surface_decal->target_entity==0);
            CHECK_FALSE(intent.surface_decal->target_model_reference);
            CHECK(intent.identity.generation==1);CHECK(intent.identity.record==i+1);CHECK(intent.identity.message_bit_offset==64);
            CHECK(intent.identity.source==api::TemporaryEffectIdentity::Source::server_record);
            CHECK(intent.origin.x==11);CHECK(intent.origin.y==-23);CHECK(intent.origin.z==47);
            CHECK_FALSE(intent.sprite);CHECK_FALSE(intent.sprite_burst);CHECK_FALSE(intent.sound);CHECK_FALSE(intent.light);
        }
        const auto stats=module.host.drain_temporary_effects(0).statistics;
        CHECK(stats.decal_accepted==12);CHECK(stats.decal_index_unresolved==0);CHECK(stats.decal_asset_unsupported==0);
    }
}
TEST_CASE("E30 unresolved unsupported or invalid server mapping never substitutes enum directory or bullet decal",
    "[e30][surface-decal][module][mapping][reject][no-stock]") {
    Module module;
    for(const auto index:{-1,0,13,511,4000,4096,32767}) {
        auto event=decal(static_cast<std::uint64_t>(index+2),index);module.host.committed_temporary_effect(event);
        CHECK(module.host.drain_temporary_effects(0).count==0);
    }
    CHECK(module.host.drain_temporary_effects(0).statistics.decal_index_unresolved==7);
    std::uint64_t record=50000;
    for(const auto name:{"{shot1","{blood0","{blood7","blood1","{yblood0","{yblood7","{blood1x","../../blood1",""}) {
        module.bind(0,name);module.host.committed_temporary_effect(decal(record++,0));
        CHECK(module.host.drain_temporary_effects(0).count==0);
    }
    CHECK(module.host.drain_temporary_effects(0).statistics.decal_asset_unsupported==9);
    module.bind(0,"{BLOOD1");module.host.committed_temporary_effect(decal(record++,0));
    const auto case_equivalent=module.host.drain_temporary_effects(0);
    REQUIRE(case_equivalent.count==1);REQUIRE(case_equivalent.effects[0].surface_decal);
    CHECK(case_equivalent.effects[0].surface_decal->asset_slot==0);
    module.bind(0,"{blood4");module.host.committed_temporary_effect(decal(record++,0));
    auto batch=module.host.drain_temporary_effects(0);REQUIRE(batch.count==1);REQUIRE(batch.effects[0].surface_decal);
    CHECK(batch.effects[0].surface_decal->asset_slot==3); // Numeric index0 is not SDK BLOOD1 enum13.
    module.bind(0,"{shot1");module.host.committed_temporary_effect(decal(record++,0));
    CHECK(module.host.drain_temporary_effects(0).count==0); // Unsupported rebind does not retain old blood mapping.
    module.bind(4096,"{blood1");module.host.committed_temporary_effect(decal(record++,4096));
    CHECK(module.host.drain_temporary_effects(0).count==0);
}
TEST_CASE("E30 world brush and BSP model target are preserved without world fallback for invalid target",
    "[e30][surface-decal][module][target][no-stock]") {
    Module module;module.bind(511,"{yblood6");std::uint64_t record=1;
    for(const std::uint8_t subtype:{std::uint8_t{13},std::uint8_t{104},std::uint8_t{109},std::uint8_t{116},std::uint8_t{117},std::uint8_t{118}}) {
        auto event=decal(record++,511,subtype==116 || subtype==117 ? 0 : 42,subtype);
        if(subtype==13)event.decal_model_reference=213;
        module.host.committed_temporary_effect(event);auto batch=module.host.drain_temporary_effects(0);
        REQUIRE(batch.count==1);REQUIRE(batch.effects[0].surface_decal);
        CHECK(batch.effects[0].surface_decal->target_entity==static_cast<std::uint32_t>(event.decal_entity));
        CHECK(batch.effects[0].surface_decal->target_model_reference==event.decal_model_reference);
        CHECK(batch.effects[0].surface_decal->asset_slot==11);
    }
    for(const std::int16_t entity:{std::int16_t{-1},std::int16_t{8192},std::int16_t{32767}}) {
        module.host.committed_temporary_effect(decal(record++,511,entity,104));CHECK(module.host.drain_temporary_effects(0).count==0);
    }
    for(const std::int16_t model:{std::int16_t{-1},std::int16_t{0},std::int16_t{1024},std::int16_t{32767}}) {
        auto event=decal(record++,511,42,13);event.decal_model_reference=model;
        module.host.committed_temporary_effect(event);CHECK(module.host.drain_temporary_effects(0).count==0);
    }
    CHECK(module.host.drain_temporary_effects(0).statistics.decal_target_invalid==7);
    for(const std::int16_t model:{std::int16_t{1},std::int16_t{1023}}) {
        auto event=decal(record++,511,8191,13);event.decal_model_reference=model;
        module.host.committed_temporary_effect(event);const auto batch=module.host.drain_temporary_effects(0);
        REQUIRE(batch.count==1);REQUIRE(batch.effects[0].surface_decal);
        CHECK(batch.effects[0].surface_decal->target_entity==8191);
        CHECK(batch.effects[0].surface_decal->target_model_reference==model);
    }
}
TEST_CASE("E30 exact duplicate suppresses only its placement while nearby decals and blood sprite remain independent",
    "[e30][surface-decal][module][ownership][dedup][no-stock]") {
    Module module;module.bind(0,"{blood2");f::bind(module.host);
    const auto first=decal();auto second=first;second.entry_ordinal=1;second.message_bit_offset=128;
    second.origin.x+=.01F;auto spray=f::blood(1);spray.palette_color=247;spray.blood_scale=3;
    spray.origin=first.origin;spray.entry_ordinal=2;spray.message_bit_offset=192;
    module.host.committed_temporary_effect(first);module.host.committed_temporary_effect(first);
    module.host.committed_temporary_effect(second);module.host.committed_temporary_effect(spray);
    const auto batch=module.host.drain_temporary_effects(0);REQUIRE(batch.count==3);
    CHECK(batch.statistics.duplicate==1);CHECK(batch.statistics.decal_accepted==2);CHECK(batch.statistics.blood_sprites==1);
    CHECK(batch.effects[0].surface_decal.has_value());CHECK(batch.effects[1].surface_decal.has_value());
    CHECK_FALSE(batch.effects[2].surface_decal);REQUIRE(batch.effects[2].sprite);REQUIRE(batch.effects[2].sprite_burst);
    CHECK(module.host.drain_temporary_effects(0).count==0);CHECK(module.host.drain_audio().count==0);
}
TEST_CASE("E30 health DeathMsg blood sprite and validated Snark sound never infer surface decal",
    "[e30][surface-decal][module][authority][no-stock]") {
    Module module;module.bind(0,"{blood1");f::bind(module.host);f::entities(module.host);
    const std::array death{std::byte{2},std::byte{1},std::byte{'g'},std::byte{'a'},std::byte{'u'},std::byte{'s'},std::byte{'s'},std::byte{0}};
    const std::array health{std::byte{0}};
    const std::array messages{api::GameMessageView{api::GameMessageKind::user_message,"Health",health,{1,1,1,0,64},90,{}},
        api::GameMessageView{api::GameMessageKind::user_message,"DeathMsg",death,{1,1,1,64,192},91,{}}};
    commit_messages(module,messages);module.observation.weapon_hud.health=0;
    module.observation.lifecycle.state=c::LocalPlayerLifeState::dead;module.observation.lifecycle.life_epoch=1;
    module.host.observe(module.observation,0);CHECK(module.host.drain_temporary_effects(0).count==0);
    auto blood=f::blood(3);blood.palette_color=247;blood.blood_scale=3;module.host.committed_temporary_effect(blood);
    module.host.committed_sound(f::pop());const auto batch=module.host.drain_temporary_effects(0);REQUIRE(batch.count==2);
    for(std::size_t i=0;i<batch.count;++i)CHECK_FALSE(batch.effects[i].surface_decal);
    CHECK(batch.statistics.decal_received==0);CHECK(batch.statistics.decal_accepted==0);
    module.observation.lifecycle.state=c::LocalPlayerLifeState::alive;module.observation.lifecycle.life_epoch=2;
    module.observation.weapon_hud.health=100;module.host.observe(module.observation,.1);module.host.cancel_uncommitted();
    auto after=decal(4);after.received_at_seconds=.1;module.host.committed_temporary_effect(after);
    const auto fresh=module.host.drain_temporary_effects(.1);REQUIRE(fresh.count==1);CHECK(fresh.effects[0].surface_decal.has_value());
}
TEST_CASE("E30 exact received decal and validated Snark sound at the same point are independent causal components",
    "[e30][surface-decal][module][authority][sound-pop][no-stock]") {
    Module module;module.bind(0,"{yblood1");f::bind(module.host);f::entities(module.host);
    const auto sound=f::pop();auto placement=decal(2);placement.origin=sound.origin;
    module.host.committed_sound(sound);module.host.committed_temporary_effect(placement);
    const auto batch=module.host.drain_temporary_effects(0);REQUIRE(batch.count==2);
    CHECK(batch.effects[0].identity.source==api::TemporaryEffectIdentity::Source::server_sound);
    CHECK_FALSE(batch.effects[0].surface_decal);REQUIRE(batch.effects[0].sprite);
    CHECK(batch.effects[1].identity.source==api::TemporaryEffectIdentity::Source::server_record);
    REQUIRE(batch.effects[1].surface_decal);CHECK_FALSE(batch.effects[1].sprite);
    CHECK(batch.statistics.pop_sound_accepted==1);CHECK(batch.statistics.decal_accepted==1);
    CHECK(batch.statistics.pop_te_correlated==0);CHECK(module.host.drain_temporary_effects(0).count==0);
}
TEST_CASE("E30 map generation retires mapping and bounded decal delivery is independent of sprite HUD clocks",
    "[e30][surface-decal][module][clock][generation][bounds][no-stock]") {
    Module module;module.bind(0,"{blood1");auto future=decal();future.received_at_seconds=1.02;
    module.host.committed_temporary_effect(future);module.host.cancel_uncommitted();
    auto batch=module.host.drain_temporary_effects(1);REQUIRE(batch.count==1);CHECK(batch.effects[0].starts_at_seconds==1.02);
    CHECK(batch.effects[0].surface_decal.has_value());CHECK(module.host.drain_temporary_effects(1.02).count==0);
    auto delayed=decal(2);delayed.received_at_seconds=2;module.host.committed_temporary_effect(delayed);
    CHECK(module.host.drain_temporary_effects(4).count==1);
    auto stale=decal(3);stale.received_at_seconds=4;module.host.committed_temporary_effect(stale);
    batch=module.host.drain_temporary_effects(6.001);CHECK(batch.count==0);CHECK(batch.statistics.decal_stale==1);
    module.host.reset({2,2,1});module.host.committed_temporary_effect(decal(4));
    CHECK(module.host.drain_temporary_effects(0).count==0);CHECK(module.host.drain_temporary_effects(0).statistics.stale==1);
    auto new_map=decal(5);new_map.generation=2;module.host.committed_temporary_effect(new_map);
    batch=module.host.drain_temporary_effects(0);CHECK(batch.count==0);CHECK(batch.statistics.decal_index_unresolved==1);
    module.bind(0,"{yblood3");new_map.record=6;new_map.record_ordinal=6;module.host.committed_temporary_effect(new_map);
    batch=module.host.drain_temporary_effects(0);REQUIRE(batch.count==1);REQUIRE(batch.effects[0].surface_decal);
    CHECK(batch.effects[0].surface_decal->asset_slot==8);CHECK(batch.effects[0].identity.generation==2);
    for(std::uint64_t record=10;record<50;++record) {new_map.record=record;new_map.record_ordinal=static_cast<std::size_t>(record);module.host.committed_temporary_effect(new_map);}
    batch=module.host.drain_temporary_effects(0);CHECK(batch.count==32);CHECK(batch.statistics.decal_capacity==8);
}
