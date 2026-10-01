#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/remote_effect_presentation.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>

namespace {
namespace api=hlclient::game_api;
namespace app=hlclient::app;
namespace audio=hlclient::audio;
namespace assets=hlclient::assets;
namespace renderer=hlclient::renderer;
namespace goldsrc=hlclient::goldsrc;
namespace fixture=hlclient::tests::world_render_fixture;

std::shared_ptr<const hlclient::world_render::WorldRenderPackage> remote_wall() {
    auto world=fixture::make_world();
    for(auto& vertex:world.vertices) {
        const auto p=vertex.position;
        vertex.position={0,p.x,p.y};vertex.normal={1,0,0};
    }
    world.bounds={{0,0,0},{0,16,16}};
    world.surfaces[0].bounds=world.bounds;
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),fixture::make_texture_set(false)},fixture::make_lightmap_set({}));
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
}
struct RemoteFixture {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    api::CommittedScriptedEvent event;
    RemoteFixture() {
        host.reset({1,1,{1}});host.set_movement_audio_time_origin(100.0);
        api::ScriptedEventBinding binding;
        binding.index=11;
        constexpr char token[]="events/glock1.sc";
        std::copy_n(token,sizeof(token),binding.name.begin());
        host.configure_scripted_events(std::span{&binding,1});
        event.generation=1;event.record=7;event.message_bit_offset=24;
        event.entry_ordinal=0;event.emitter_entity=2;event.event_index=11;
        // Eye={30,8,8}, ray crosses the actual bounded render wall at {0,8,8}.
        event.origin={30,8,-20};event.angles={0,180,0};event.received_at_seconds=101.0;
    }
    api::RemoteWeaponEffectsBatch shot() {
        host.committed_scripted_event(event);return host.drain_remote_effects(1.0);
    }
};
struct Samples final : goldsrc::SoundAssets {
    std::shared_ptr<const assets::AudioAsset> pcm;
    Samples() {
        auto p=std::make_shared<assets::AudioAsset>();p->sample_rate=48000;p->channel_count=1;
        p->interleaved_samples.assign(4800,0.125F);pcm=std::move(p);
    }
    goldsrc::SoundAssetResult request(std::uint16_t) noexcept override {return {goldsrc::SoundAssetStatus::ready,pcm};}
    goldsrc::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return {goldsrc::SoundAssetStatus::ready,pcm};}
};
struct Sink final : audio::Output {
    audio::OfflineOutput output;
    std::vector<audio::Command> starts;
    bool submit(const audio::Command& command) noexcept override {
        if(command.operation==audio::Operation::start) starts.push_back(command);
        return output.submit(command);
    }
    void set_listener(audio::Listener value) noexcept override {output.set_listener(value);}
};
double isolated_pcm(const api::LocalAudioBatch& cues,double now) {
    Samples samples;Sink sink;sink.set_listener({{10,8,8},{0,1,0},1,false});
    goldsrc::LocalAudio playback(sink,true);
    playback.update(cues,&samples,now,true);
    CHECK(sink.starts.size()==cues.count);
    CHECK(playback.statistics().late==0);
    std::array<float,512> block{};sink.output.mixer.render(block);
    double magnitude=0;
    for(float value:block) {CHECK(std::isfinite(value));magnitude+=std::abs(value);}
    playback.update(cues,&samples,now,true);
    CHECK(sink.starts.size()==cues.count);
    return magnitude;
}
}

TEST_CASE("Remote committed Glock executes shared world effects and isolated spatial PCM",
          "[remote-effects][weapon-visuals][weapon-audio][game-module]") {
    RemoteFixture f;
    const auto batch=f.shot();
    REQUIRE(batch.count==1);CHECK(batch.statistics.accepted==1);
    REQUIRE(batch.effects[0].shell);REQUIRE(batch.effects[0].impact);
    const auto fire=f.host.drain_remote_audio();
    REQUIRE(fire.count==1);CHECK(fire.cues[0].kind==api::LocalSoundKind::fire);
    REQUIRE(fire.cues[0].world_origin);
    CHECK(fire.cues[0].world_origin->x==30);
    CHECK(isolated_pcm(fire,1.0)>1.0);
    const auto collision=hlclient::tests::collision_brush_fixture::package(true,0,0);
    const auto world=remote_wall();
    renderer::TransientVisuals shells;shells.set_collision_world(collision);
    app::WorldImpactPresentation impacts;app::RemoteEffectPresentation presenter;
    presenter.consume(batch,f.host,shells,impacts,collision,world.get(),{},true,true,1.0);
    CHECK(presenter.statistics().shells==1);
    CHECK(presenter.statistics().impact_hits==1);
    CHECK(shells.statistics().shells_created==1);CHECK(impacts.active()==1);
    renderer::RenderScene scene;scene.camera.position={40,8,8};
    presenter.present(scene,1.01);
    CHECK(scene.world_flash_count==1);REQUIRE(scene.transient_world_light);
    CHECK(scene.world_flashes[0].center.x==Catch::Approx(10).margin(0.01));
    const auto contact=f.host.drain_remote_audio();
    REQUIRE(contact.count>=1);
    for(std::size_t i=0;i<contact.count;++i) {
        CHECK(contact.cues[i].kind==api::LocalSoundKind::impact);
        REQUIRE(contact.cues[i].world_origin);
        CHECK(contact.cues[i].world_origin->x==Catch::Approx(0).margin(0.2));
    }
    // Fresh output has no fire voice: this signal proves impact independently.
    CHECK(isolated_pcm(contact,1.0)>1.0);
    presenter.consume(batch,f.host,shells,impacts,collision,world.get(),{},true,true,1.0);
    renderer::RenderScene repeated;presenter.present(repeated,1.01);
    CHECK(repeated.world_flash_count==1);CHECK(shells.statistics().shells_created==1);
    CHECK(impacts.active()==1);CHECK(f.host.drain_remote_audio().count==0);
    f.host.committed_scripted_event(f.event);
    CHECK(f.host.drain_remote_effects(1.02).count==0);
    // The shared point-sweep produces a real wall contact after flight. It
    // runs without any shell materializer or camera visibility query.
    unsigned audible_contacts=0;
    for(unsigned step=1;step<=240;++step) {
        const double now=1.0+step/120.0;
        shells.update(now);
        for(const auto& collision_contact:shells.contacts())
            f.host.remote_shell_contact(collision_contact);
        const auto bounce=f.host.drain_remote_audio();
        for(std::size_t i=0;i<bounce.count;++i) {
            CHECK(bounce.cues[i].kind==api::LocalSoundKind::shell_contact);
            REQUIRE(bounce.cues[i].world_origin);
            CHECK(bounce.cues[i].world_origin->x==Catch::Approx(0).margin(0.2));
            CHECK(now>1.1);++audible_contacts;
        }
        if(bounce.count) CHECK(isolated_pcm(bounce,now)>0.0);
    }
    CHECK(audible_contacts>=1);CHECK(audible_contacts<=3);
    renderer::RenderScene expired;presenter.present(expired,3.1);
    CHECK(expired.world_flash_count==0);CHECK_FALSE(expired.transient_world_light);
    presenter.reset();shells.reset();impacts.reset();f.host.teardown();
    CHECK(f.host.drain_remote_audio().count==0);
}

TEST_CASE("Remote readiness skips only missing effects and preserves local light priority",
          "[remote-effects][weapon-visuals][game-module]") {
    for(bool ready_shell:{false,true}) for(bool ready_decal:{false,true}) {
        CAPTURE(ready_shell,ready_decal);
        RemoteFixture f;const auto batch=f.shot();(void)f.host.drain_remote_audio();
        renderer::TransientVisuals shells;app::WorldImpactPresentation impacts;
        app::RemoteEffectPresentation presenter;const auto world=remote_wall();
        const auto collision=hlclient::tests::collision_brush_fixture::package(true,0,0);
        presenter.consume(batch,f.host,shells,impacts,collision,world.get(),{},ready_shell,ready_decal,1.0);
        CHECK(shells.statistics().shells_created==(ready_shell?1U:0U));
        CHECK(impacts.active()==(ready_decal?1U:0U));
        CHECK(f.host.drain_remote_audio().count>=1);
        renderer::RenderScene scene;
        scene.transient_world_light=renderer::RenderPointLight{{100,20,10},20,1,{1,1,1}};
        presenter.present(scene,1.01);
        CHECK(scene.world_flash_count==1);
        REQUIRE(scene.transient_world_light);CHECK(scene.transient_world_light->center.x==100);
        presenter.reset();renderer::RenderScene reset;presenter.present(reset,1.02);
        CHECK(reset.world_flash_count==0);
    }
}

TEST_CASE("Local and remote presentation voices coexist at equal serial and channel",
          "[remote-effects][weapon-audio][audio]") {
    Samples samples;Sink sink;sink.set_listener({{0,0,0},{0,1,0},1,false});
    goldsrc::LocalAudio local(sink),remote(sink,true);
    api::LocalAudioBatch batch;batch.session=1;batch.scope=1;batch.count=1;
    auto& cue=batch.cues[0];cue.scope=1;cue.serial=1;cue.scheduled_seconds=1;
    cue.kind=api::LocalSoundKind::fire;cue.channel=api::LocalSoundChannel::weapon;
    constexpr char token[]="weapons/pl_gun3.wav";
    std::copy_n(token,sizeof(token),cue.reference.sample.begin());
    cue.reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    local.update(batch,&samples,1,true);
    auto other=batch;other.cues[0].world_origin=assets::AssetVector3{0,10,0};
    remote.update(other,&samples,1,true);
    REQUIRE(sink.starts.size()==2);CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    CHECK(sink.output.mixer.active()==2);
    cue.serial=2;cue.channel=api::LocalSoundChannel::automatic;
    other=batch;other.cues[0].world_origin=assets::AssetVector3{0,-10,0};
    local.update(batch,&samples,1,true);remote.update(other,&samples,1,true);
    REQUIRE(sink.starts.size()==4);CHECK(sink.starts[2].voice!=sink.starts[3].voice);
    CHECK(sink.output.mixer.active()==4);
    auto reset=batch;reset.session=2;reset.count=0;local.update(reset,&samples,1.01,true);
    CHECK(sink.output.mixer.active()>=2); // remote owners survive local lifecycle
}

TEST_CASE("Remote world flash OpenGL faces camera and respects wall depth and expiry",
          "[remote-effects][weapon-visuals][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    const auto world=remote_wall();RemoteFixture host;
    renderer::TransientVisuals shells;app::WorldImpactPresentation impacts;
    const auto observe=[&](const renderer::RenderScene& scene) {
        context->renderer().render(scene,{320,240});
        const auto pixels=context->renderer().observe_framebuffer({320,240},scene.clear_color);
        REQUIRE(pixels);CHECK(glGetError()==GL_NO_ERROR);return pixels.color_signature;
    };
    for(const float side:{0.0F,10.0F,-10.0F}) {
        renderer::RenderScene scene;scene.static_world=renderer::RenderStaticWorld{world};
        scene.camera.position={30,8+side,8};scene.camera.target={0,8,8};
        const auto baseline=observe(scene);
        api::RemoteWeaponEffectsBatch batch;batch.count=1;
        auto& effect=batch.effects[0];effect.action.generation=1;
        effect.action.command_sequence=1;effect.action.emitter_entity=2;
        effect.action.started_at_seconds=1;
        effect.flash=api::RemoteMuzzleFlash{{-4,8,8},1,{1,0.7F,0.2F,1},1,1.075};
        app::RemoteEffectPresentation presenter;
        presenter.consume(batch,host.host,shells,impacts,{},nullptr,{},false,false,1.0);
        auto occluded=scene;presenter.present(occluded,1.01);
        REQUIRE(occluded.world_flash_count==1);
        CHECK(observe(occluded)==baseline);
        presenter.reset();effect.flash->origin.x=4;
        presenter.consume(batch,host.host,shells,impacts,{},nullptr,{},false,false,1.0);
        auto visible=scene;presenter.present(visible,1.01);
        REQUIRE(visible.world_flash_count==1);
        CHECK(observe(visible)!=baseline);
        auto expired=scene;presenter.present(expired,1.08);
        CHECK(expired.world_flash_count==0);CHECK(observe(expired)==baseline);
        // Render-state restoration includes the next ordinary world pass.
        CHECK(observe(scene)==baseline);
    }
    context->release_renderer();
}

TEST_CASE("Remote world flash is depth occluded by Studio geometry before viewmodel clear",
          "[remote-effects][weapon-visuals][opengl][actual-context]") {
    namespace entity=hlclient::entity_render;
    namespace entities=hlclient::tests::entity_render_fixture;
    auto built=entities::scene_package(entities::render_assets(true,false));
    REQUIRE(built);
    auto package=std::make_shared<const entity::EntitySceneRenderPackage>(std::move(*built.package));
    entity::EntityRenderFrameBuildInput frame;
    frame.resource_id=0xE101;frame.resource_revision=1;
    frame.interpolation={0,0,0,0,1,1,entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    // The project-owned XY triangle is posed into a large YZ foreground plane.
    entity::StudioRenderPose pose{package->studio_assets()[0]->source_identity(),{}};
    pose.bone_matrices.push_back({0,20,0,0,0,0,20,0,20,0,0,0,0,-8,-8,1});
    frame.studio_poses.push_back(std::move(pose));
    entity::StudioEntityRenderInstance instance;
    instance.entity_number=2;instance.studio_asset_index=0;instance.pose_index=0;
    instance.interpolated_bounds={{-0.1F,-8,-8},{0.1F,12,12}};
    frame.studio_instances.push_back(instance);
    auto composed=entity::EntityRenderFrameBuilder{}.build(*package,std::move(frame));
    REQUIRE(composed);
    auto dynamic_frame=std::make_shared<const entity::EntityRenderFrame>(std::move(*composed.frame));
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    renderer::RenderScene scene;
    scene.camera.position={20,0,0};scene.camera.target={0,0,0};
    scene.dynamic_entities=renderer::RenderDynamicEntities{package,dynamic_frame,{}};
    const auto observe=[&]() {
        context->renderer().render(scene,{320,240});
        auto pixels=context->renderer().observe_framebuffer({320,240},scene.clear_color);
        REQUIRE(pixels);CHECK(glGetError()==GL_NO_ERROR);return pixels.color_signature;
    };
    const auto baseline=observe();
    scene.world_flash_count=1;
    scene.world_flashes[0]={{-2,0,0},0.5F,{1,0.7F,0.2F,1}};
    CHECK(observe()==baseline);
    scene.world_flashes[0].center.x=2;
    CHECK(observe()!=baseline);
    scene.world_flash_count=0;CHECK(observe()==baseline);
    context->release_renderer();
}
