#include "weapon_suite_test_fixture.hpp"
#include "snark_world_test_fixture.hpp"
#include "snark_sound_pop_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
namespace api=hlclient::game_api;
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
namespace a=hlclient::audio;
namespace r=hlclient::renderer;
namespace f=hlclient::tests::weapon_suite_fixture;
namespace sf=hlclient::tests::snark_sound_pop_fixture;
namespace wf=hlclient::tests::snark_world_fixture;
namespace hw=hlclient::tests::hand_grenade_world_fixture;
struct Samples final:g::SoundAssets {
    std::shared_ptr<const hlclient::assets::AudioAsset> finite,loop;
    std::uint64_t requests{};
    Samples() {
        auto pcm=std::make_shared<hlclient::assets::AudioAsset>();
        pcm->sample_rate=48000;pcm->channel_count=1;pcm->interleaved_samples.assign(480,.125F);
        finite=pcm;pcm=std::make_shared<hlclient::assets::AudioAsset>(*pcm);
        pcm->loop=hlclient::assets::AudioAsset::Loop{0,480};loop=pcm;
    }
    g::SoundAssetResult request(std::uint16_t slot) noexcept override {
        ++requests;return {g::SoundAssetStatus::ready,slot==10 ? loop : finite};
    }
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {
        ++requests;return {g::SoundAssetStatus::ready,finite};
    }
};
g::SoundTime time(double seconds) {
    return g::SoundTime{}+std::chrono::duration_cast<g::SoundTime::duration>(std::chrono::duration<double>{seconds});
}
g::CommittedSound voice(unsigned ordinal,unsigned entity,double at) {
    g::CommittedSound event;event.generation=1;event.record=event.ordinal=ordinal;event.cursor=ordinal*64;
    event.received=time(at);event.opcode=g::RuntimeControlOpcode::svc_sound;
    event.sound.entity_reference=static_cast<std::uint16_t>(entity);event.sound.channel=3;
    event.sound.sound_reference=10;event.sound.origin={0,16,8};return event;
}
g::CommittedSound directory_update(unsigned ordinal,unsigned entity,double at,bool present=false) {
    g::CommittedSoundQueue queue;g::RuntimeControlEvent event;
    event.opcode=g::RuntimeControlOpcode::svc_updateuserinfo;event.provenance.source_generation=1;
    event.body=g::RuntimeControlUserInfoUpdate{static_cast<std::uint8_t>(entity-1),0,0,16,present,{}};
    queue.publish(ordinal,ordinal,event,time(at));g::CommittedSound result;
    REQUIRE(queue.pop(result));return result;
}
}

TEST_CASE("E28 fifteen simulated minutes keep switched weapon life peer and world-effect owners bounded",
    "[e28][weapon-suite][lifecycle][long-synthetic][audio][no-stock]") {
    f::Module module;sf::bind(module.host);module.host.configure_effect_policy({true});
    auto effects=std::make_unique<hlclient::app::TemporaryEffectPresentation>();effects->reset(1);
    auto shells=std::make_unique<r::TransientVisuals>();
    a::OfflineOutput output;output.set_listener({{}, {0,1,0},.25F,false});Samples samples;
    g::ServerAudio server{output};server.reset(1);
    g::LocalAudio local{output},remote{output,true};g::TemporaryAudio temporary{output};temporary.reset(1);
    std::size_t max_voices{},max_shells{},max_sprites{},max_beams{},max_beam_effect_sprites{},max_egon_beams{},max_gauss_beams{};
    std::uint64_t egon_run_replacements{};
    // Independently authored caller muzzle observation for this metadata-only
    // host fixture. The actual Studio/approved-asset materializer is exercised
    // separately; this numeric anchor makes no stock socket claim.
    const hlclient::assets::AssetVector3 fixture_muzzle{110,80,78};
    std::uint64_t life=1,pop_count{},accepted_fire{},world_survival_checks{};double pcm_energy{};
    std::uint32_t sequence=1;unsigned sound_ordinal=1;
    // Commands and authoritative observations are independent of the render
    // samples. There is no wall-clock sleep and no gameplay/server mutation.
    for(unsigned phase=0;phase<180;++phase) {
        const double start=phase*5.;const auto id=f::weapons[phase%f::weapons.size()].id;
        module.observed=f::state(id,phase*100+1);module.observed.lifecycle.life_epoch=life;
        module.adopt(id,start,phase*100+1);
        if(id==7) {module.observed.weapon_hud.clips[7]=0;f::slot(module.observed,7).clip=0;module.observe(start);}
        sf::entities(module.host,phase*100+2);
        server.consume(directory_update(sound_ordinal++,2,start,true),time(start));
        server.consume(voice(sound_ordinal++,1,start),time(start));
        server.consume(voice(sound_ordinal++,2,start),time(start));server.update(&samples,time(start));
        const auto before_leave=output.mixer.active();
        server.consume(directory_update(sound_ordinal++,2,start),time(start));
        CHECK(output.mixer.active()+1==before_leave); // B only; A's loop remains.
        auto pop=sf::pop(phase*100+3,40,start+.02);
        auto blood=sf::blood(phase*100+4,start+.02);
        if(phase%2) {module.host.committed_temporary_effect(blood);module.host.committed_sound(pop);}
        else {module.host.committed_sound(pop);module.host.committed_temporary_effect(blood);}
        module.host.committed_sound(pop);module.host.committed_temporary_effect(blood);
        const auto batch=module.host.drain_temporary_effects(start+.02);REQUIRE(batch.count==1);
        REQUIRE(batch.effects[0].sprite);REQUIRE(batch.effects[0].sprite_burst);
        CHECK(batch.effects[0].sprite_burst->count==16);CHECK_FALSE(batch.effects[0].particles);
        CHECK_FALSE(batch.effects[0].sound);CHECK_FALSE(batch.effects[0].light);
        ++pop_count;effects->consume(batch,start+.02);
        unsigned submitted=0;bool killed=false,respawned=false,egon_confirmed=false;
        const double release_at=id==10 ? 3.02 : .42;
        for(unsigned frame=0;frame<300;++frame) {
            const double now=start+frame/60.;
            while(submitted<2 && now>=start+(submitted==0 ? .02 : release_at)) {
                const auto buttons=submitted==0 ? static_cast<std::uint16_t>(id==9 ? 1U<<11 : id==7 ? 1U<<13 : 1U) : std::uint16_t{0};
                module.submit(sequence++,start+(submitted==0 ? .02 : release_at),buttons);++submitted;
            }
            if(id==10 && submitted && !egon_confirmed && now>=start+.04) {
                f::slot(module.observed,10).auxiliary_integer3=1;
                f::select(module.observed,10,phase*100+3);module.observe(now);
                egon_confirmed=true;
            }
            if(phase%3==0 && !killed && now>=start+.067) {
                module.observed.receiving_client->health=0;module.observed.lifecycle.state=c::LocalPlayerLifeState::dead;
                module.observe(now);module.host.cancel_uncommitted();killed=true;
                r::RenderScene probe;effects->present(probe,now);CHECK_FALSE(effects->sprites().empty());
                CHECK(module.host.world_entity({wf::monster(),nullptr,{},8}).role==api::WorldEntityPresentationRole::ordinary);
                ++world_survival_checks;
            }
            if(killed && !respawned && now>=start+1.) {
                ++life;module.observed=f::state(id,phase*100+5);module.observed.lifecycle.life_epoch=life;
                module.adopt(id,now,phase*100+5);respawned=true;
            }
            const auto snapshot=module.sample(now);
            if(frame==3 && snapshot.action) ++accepted_fire;
            const auto visual=module.visuals(now);
            if(visual.shell) (void)shells->spawn(*visual.shell,now);
            for(std::size_t i=0;i<visual.extra_shell_count;++i) (void)shells->spawn(visual.extra_shells[i],now);
            shells->update(now);const auto beam=module.host.beam_effects(module.world,now,fixture_muzzle);
            REQUIRE(beam.count<=beam.sprites.size());REQUIRE(beam.impact_count<=beam.impacts.size());
            const auto ribbons=static_cast<std::size_t>(std::count_if(beam.sprites.begin(),
                beam.sprites.begin()+static_cast<std::ptrdiff_t>(beam.count),[](const auto& effect){return effect.ribbon;}));
            r::RenderScene scene;effects->present(scene,now);
            local.update(module.host.drain_audio(),&samples,now,true);
            remote.update(module.host.drain_remote_audio(),&samples,now,true);
            temporary.update(module.host.drain_temporary_effects(now),&samples,now,true);
            server.update(&samples,time(now));std::array<float,256> pcm{};output.mixer.render(pcm);
            REQUIRE(std::ranges::all_of(pcm,[](float value){return std::isfinite(value);}));
            for(const auto value:pcm) pcm_energy+=std::abs(value);
            max_voices=std::max(max_voices,output.mixer.active());max_shells=std::max(max_shells,shells->statistics().active);
            max_sprites=std::max(max_sprites,effects->sprites().size());max_beams=std::max(max_beams,ribbons);
            max_beam_effect_sprites=std::max(max_beam_effect_sprites,beam.count);
            if(id==10) {max_egon_beams=std::max(max_egon_beams,ribbons);egon_run_replacements=std::max(egon_run_replacements,beam.audio_replace_requests);}
            if(id==9) max_gauss_beams=std::max(max_gauss_beams,ribbons);
            REQUIRE(output.mixer.active()<=a::maximum_voices);REQUIRE(shells->statistics().active<=32);
            REQUIRE(effects->sprites().size()<=160);REQUIRE(beam.count<=128);REQUIRE(beam.impact_count<=32);
        }
        module.host.remote_slot_boundary(2);module.host.remote_entities({},8,phase*100+6);
    }
    INFO("simulated_seconds=900 phases=180 max_voices="<<max_voices<<" max_shells="<<max_shells
        <<" max_sprites="<<max_sprites<<" max_beams="<<max_beams<<" fixture_assets=2");
    CHECK(pop_count==180);CHECK(world_survival_checks==60);CHECK(accepted_fire>0);CHECK(pcm_energy>0);
    CHECK(max_sprites==17);CHECK(max_shells>0);CHECK(max_beams>0);CHECK(max_egon_beams>0);
    CHECK(egon_run_replacements>0);CHECK(max_voices>1);
    CHECK(samples.finite.use_count()>=1);CHECK(samples.loop.use_count()>=1);
    module.host.cancel_uncommitted();module.host.remote_slot_boundary(2);
    local.update(module.host.drain_audio(),&samples,901,true);remote.update(module.host.drain_remote_audio(),&samples,901,true);
    r::RenderScene empty;effects->present(empty,905);shells->update(905);
    CHECK(effects->sprites().empty());CHECK(effects->models().empty());CHECK(shells->statistics().active==0);
    CHECK(empty.world_particle_count==0);CHECK(module.host.beam_effects(module.world,905,{}).count==0);
    server.reset(0);temporary.reset(0);module.host.teardown();
    std::array<float,2048> tail{};output.mixer.render(tail);CHECK(output.mixer.active()==0);
    CHECK(samples.finite.use_count()==1);CHECK(samples.loop.use_count()==1);
    std::cout<<"[e28-lifecycle-bounds] simulated_seconds=900 phases=180 max_voices="<<max_voices
        <<" max_shells="<<max_shells<<" max_sprites="<<max_sprites<<" max_beams="<<max_beams
        <<" max_beam_effect_sprites="<<max_beam_effect_sprites<<" max_egon_beams="<<max_egon_beams
        <<" max_gauss_beams="<<max_gauss_beams<<" egon_run_replacements="<<egon_run_replacements
        <<" pop_count="<<pop_count<<" death_world_survival="<<world_survival_checks
        <<" pcm_fixture_instances=2 final_voices="<<output.mixer.active()<<'\n';
}

TEST_CASE("E28 fifteen simulated minutes bound ordinary history and remove reused server entities immediately",
    "[e28][weapon-suite][long-synthetic][entity-interpolation][no-stock]") {
    g::RuntimeOrdinarySnapshotPresentation owner;g::RuntimeEntityInterpolationOptions options;
    const std::array<std::uint32_t,1> selected{40};options.ordinary_entities=selected;
    options.limits.maximum_snapshot_gap_seconds=.25;options.teleport_distance=128.;options.ordinary_teleport_distance=512.;
    std::size_t history_high{},lifetimes_high{};
    for(unsigned tick=1;tick<=18000;++tick) {
        const double now=tick*.05;auto state=hw::state(237);
        state.packet_entities[0]=wf::monster();state.packet_entities[0].origin.x=static_cast<double>(tick%100)*.25;
        if(tick%100==0)state.packet_entities.clear();
        hw::stamp(state,tick,now);owner.observe(std::make_shared<const c::RuntimeClientObservationState>(state));
        const auto output=owner.sample(state,now,options,.1);REQUIRE(output);
        if(state.packet_entities.empty()) {CHECK(output.state->packet_entities.empty());CHECK(owner.lifetime_count()==0);}
        history_high=std::max(history_high,owner.history_size());lifetimes_high=std::max(lifetimes_high,owner.lifetime_count());
        REQUIRE(owner.history_size()<=16);REQUIRE(owner.lifetime_count()<=1);
    }
    INFO("simulated_seconds=900 history_high="<<history_high<<" entity_lifetimes_high="<<lifetimes_high);
    CHECK(history_high==16);CHECK(lifetimes_high==1);owner.reset();CHECK(owner.history_size()==0);CHECK(owner.lifetime_count()==0);
    std::cout<<"[e28-history-bounds] simulated_seconds=900 publications=18000 history_high="<<history_high
        <<" lifetime_high="<<lifetimes_high<<" final_history="<<owner.history_size()<<'\n';
}

TEST_CASE("E28 fifteen simulated minutes reuse approved Studio and Snark pop GPU resources after warmup",
    "[e28][weapon-suite][long-synthetic][opengl][approved-assets][no-stock]") {
    auto gl=hlclient::tests::entity_opengl_fixture::try_context();if(!gl)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(hlclient::tests::entity_opengl_fixture::capable_context());gl->initialize_renderer();wf::Owned owned;
    auto effects=std::make_unique<hlclient::app::TemporaryEffectPresentation>();effects->reset(1);
    const auto imported_studio=owned.assets->summary().imported_studio,imported_sprites=owned.assets->summary().imported_sprites;
    std::uint64_t studio_uploads{},sprite_uploads{};
    for(unsigned cycle=0;cycle<180;++cycle) {
        const double now=cycle*5.;owned.state.packet_entities={wf::player(),wf::monster()};
        c::RuntimePacketEntityObservation beam;beam.entity_number=41;beam.origin={-8.,0.,24.};beam.angles={8.,0.,24.};
        beam.custom_beam.emplace();auto& profile=*beam.custom_beam;
        profile.type=c::RuntimeCustomBeamObservation::Type::points;profile.sprite_model_index=321;
        profile.width_units=1.;profile.opacity=255;profile.color={160,130,20};profile.complete=profile.supported=true;
        owned.state.packet_entities.push_back(beam);
        owned.state.packet_entities.front().origin.x=static_cast<double>(cycle%4)*4.;
        owned.publish(cycle*3+2,now+1,now);
        const auto instances=owned.world.entity_frame()->studio_instances();
        REQUIRE(std::ranges::any_of(instances,[](const auto& instance){return instance.entity_number==2 && instance.subinstance_ordinal==1;}));
        auto blood=wf::blood(cycle+1);blood.received_at_seconds=now;owned.host.committed_temporary_effect(blood);
        const auto batch=owned.host.drain_temporary_effects(now);REQUIRE(batch.count==1);effects->consume(batch,now);
        r::RenderScene scene;effects->present(scene,now+.1);
        scene.dynamic_entities=r::RenderDynamicEntities{owned.world.entity_scene(),owned.world.entity_frame(),{}};
        scene.transient_sprite_entities=owned.assets->materialize_temporary_sprites(effects->sprites(),now+.1);
        owned.assets->present_entity_effects(scene,now+.1);REQUIRE(scene.follow_trail_entities);
        CHECK(scene.follow_trail_entities->frame->sprite_instances().size()==1);
        REQUIRE(scene.transient_sprite_entities);scene.clear_color={0,0,0,1};
        scene.camera={{0,0,40},{0,0,16},{0,1,0},1.047F,.1F,128};
        gl->renderer().render(scene,{96,96});REQUIRE(glGetError()==GL_NO_ERROR);
        const auto stats=gl->renderer().entity_statistics();
        if(cycle==0) {studio_uploads=stats.studio_asset_upload_count;sprite_uploads=stats.sprite_asset_upload_count;
            REQUIRE(studio_uploads>0);REQUIRE(sprite_uploads>0);}
        else {CHECK(stats.studio_asset_upload_count==studio_uploads);CHECK(stats.sprite_asset_upload_count==sprite_uploads);}
        CHECK(owned.assets->summary().imported_studio==imported_studio);CHECK(owned.assets->summary().imported_sprites==imported_sprites);
        owned.host.cancel_uncommitted();owned.host.remote_slot_boundary(2);owned.assets->player_slot_boundary(2);
        owned.state.packet_entities.clear();owned.publish(cycle*3+3,now+1.2,now+.2);
        CHECK(std::ranges::none_of(owned.world.entity_frame()->studio_instances(),[](const auto& instance){return instance.entity_number==2;}));
        r::RenderScene retired;effects->present(retired,now+4.);CHECK(effects->sprites().empty());
        retired.dynamic_entities=r::RenderDynamicEntities{owned.world.entity_scene(),owned.world.entity_frame(),{}};
        owned.assets->present_entity_effects(retired,now+4.);CHECK_FALSE(retired.follow_trail_entities);
        CHECK_FALSE(owned.assets->materialize_temporary_sprites(effects->sprites(),now+4.));
        gl->renderer().render(retired,{96,96});REQUIRE(glGetError()==GL_NO_ERROR);
    }
    INFO("simulated_seconds=900 approved_studio_instances="<<imported_studio<<" approved_sprite_instances="<<imported_sprites
        <<" studio_uploads="<<studio_uploads<<" sprite_uploads="<<sprite_uploads);
    const auto final_uploads=gl->renderer().entity_statistics();
    effects->reset(0);owned.host.teardown();gl->release_renderer();
    std::cout<<"[e28-gpu-bounds] simulated_seconds=900 cycles=180 approved_studio_instances="<<imported_studio
        <<" approved_sprite_instances="<<imported_sprites<<" warm_studio_uploads="<<studio_uploads
        <<" warm_sprite_uploads="<<sprite_uploads<<" final_studio_uploads="<<final_uploads.studio_asset_upload_count
        <<" final_sprite_uploads="<<final_uploads.sprite_asset_upload_count<<'\n';
}
