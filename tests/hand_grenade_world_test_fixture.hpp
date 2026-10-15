#pragma once
#include "crossbow_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/goldsrc/server_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>

namespace hlclient::tests::hand_grenade_world_fixture {
namespace api=game_api;namespace c=client;namespace g=goldsrc;
inline void stamp(c::RuntimeClientObservationState& s,unsigned ordinal,double seconds) {
    s.publication_revision=ordinal;s.server_time_seconds=seconds;
    s.entity_metadata=s.server_time_metadata=s.client_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{ordinal,ordinal,1,0,8}};
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
}
inline c::RuntimeClientObservationState state(unsigned slot=237) {
    auto s=crossbow_fixture::state(-1,1,5);s.receiving_client->owned_weapon_bits=1U<<12;
    s.weapon_hud.active_weapon_id=12;s.weapon_hud.clips[6].reset();s.weapon_hud.clips[12]=-1;
    s.weapon_hud.catalogue[0].id=12;s.weapon_hud.catalogue[0].command_name="weapon_handgrenade";
    s.weapon_hud.catalogue[0].slot=4;s.weapon_hud.catalogue[0].position=0;
    s.weapon_slots[0].weapon_id=12;s.weapon_slots[0].wire_slot=12;
    c::RuntimePacketEntityObservation e;e.entity_number=40;e.model_index=slot;
    e.origin={0.0,0.0,0.0};e.angles={0.0,0.0,0.0};e.ordinary_visual_schema=true;
    e.brush_move_type=10;e.sequence=0;e.frame=0;e.frame_rate=1;e.animation_time_seconds=1;e.effects=0;
    s.packet_entities={e};stamp(s,1,1);return s;
}
inline std::unique_ptr<app::RuntimeReplayLocalAssets> owned(ScopedLocalResourceTestRoot& root,unsigned slot=237,bool timed=true) {
    SyntheticBspBuilder bsp;const std::string text="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
    const auto eb=std::as_bytes(std::span{text});bsp.lump(SyntheticBspLumpId::entities).assign(eb.begin(),eb.end());
    SyntheticWad3Entry texture;texture.name="TEST_QUAD";texture.payload=synthetic_goldsrc_miptex("TEST_QUAD",64,64);
    root.write("valve","test.wad",synthetic_wad3({texture}).bytes);root.write("valve","maps/test.bsp",bsp.build());
    auto model=literal_minimal_goldsrc_studio_v10();
    // Independent labels distinguish the two approved model resources. The
    // entity's session slot, never a classname or guessed grenade family, wins.
    studio_write_fixed_string(model,kSyntheticStudioSequenceOffset,32,"owned_timed");
    if(timed)root.write("valve","models/w_grenade.mdl",model);
    studio_write_fixed_string(model,kSyntheticStudioSequenceOffset,32,"owned_contact");
    root.write("valve","models/grenade.mdl",model);
    root.write("valve","sprites/owned_explosion.spr",sprite_fixture::literal_single_sprite(1));
    root.write("valve","sprites/owned_smoke.spr",sprite_fixture::literal_single_sprite(1));
    const auto resources=readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0},
        {2,"models/w_grenade.mdl",static_cast<std::uint16_t>(slot),0,0},{2,"models/grenade.mdl",312,0,0},
        {2,"sprites/owned_explosion.spr",319,0,0},{2,"sprites/owned_smoke.spr",320,0,0}});
    auto result=app::RuntimeReplayLocalAssets::create(resources,readiness_fixture::parse_server_info("maps/test.bsp"),
        root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(result.projection);return std::move(result.projection);
}
inline void project(app::RuntimeReplayLocalAssets& assets,c::RuntimeClientObservationState& s,c::ClientWorldState& world) {
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
    const auto result=assets.project_entities(s,world);INFO((result.error ? result.error->context : "ready"));REQUIRE(result);
}
inline c::CommittedTemporaryEffect effect(unsigned ordinal,double seconds,bool smoke=false) {
    c::CommittedTemporaryEffect e;e.generation=1;e.record=e.record_ordinal=ordinal;e.received_at_seconds=seconds;
    e.kind=smoke ? c::TemporaryEffectKind::smoke : c::TemporaryEffectKind::explosion;
    e.origin={0,0,0};e.sprite_index=smoke ? 320 : 319;e.scale_tenths=smoke ? 40 : 30;
    e.framerate=smoke ? 12 : 15;return e;
}
inline g::CommittedSound sound(unsigned ordinal=1,unsigned entity=40,unsigned slot=51,float y=-100,unsigned channel=2) {
    g::CommittedSound e;e.generation=1;e.record=e.ordinal=ordinal;e.cursor=ordinal*64;
    e.opcode=g::RuntimeControlOpcode::svc_sound;e.sound.entity_reference=static_cast<std::uint16_t>(entity);
    e.sound.sound_reference=static_cast<std::uint16_t>(slot);e.sound.channel=static_cast<std::uint8_t>(channel);
    e.sound.origin={0,y,0};e.sound.volume=slot<60 ? 64 : 140;e.sound.attenuation=51;return e;
}
struct Samples final:g::SoundAssets {
    std::shared_ptr<const assets::AudioAsset> audible,silent;
    std::vector<std::uint16_t> requests;
    Samples() {
        auto sample=std::make_shared<assets::AudioAsset>();sample->sample_rate=48000;sample->channel_count=1;
        sample->interleaved_samples.assign(24000,.125F);audible=sample;
        sample=std::make_shared<assets::AudioAsset>();sample->sample_rate=48000;sample->channel_count=1;
        sample->interleaved_samples.assign(480,0);silent=sample;
    }
    g::SoundAssetResult request(std::uint16_t slot) noexcept override {
        requests.push_back(slot);return {g::SoundAssetStatus::ready,slot==70 ? silent : audible};
    }
    std::string_view virtual_name(std::uint16_t slot) const noexcept override {
        return slot==70 ? "sound/common/null.wav" : slot<60 ? "sound/weapons/grenade_hit1.wav" : "sound/weapons/debris1.wav";
    }
};
struct Sink final:audio::Output {
    audio::OfflineOutput output;std::vector<audio::Command> starts;
    bool submit(const audio::Command& command) noexcept override {
        if(command.operation==audio::Operation::start)starts.push_back(command);return output.submit(command);
    }
    void set_listener(audio::Listener listener) noexcept override {output.set_listener(listener);}
};
}
