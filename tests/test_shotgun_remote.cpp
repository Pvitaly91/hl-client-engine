#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/remote_effects.hpp>
#include <hlclient/app/remote_effect_presentation.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace app=hlclient::app;
namespace renderer=hlclient::renderer;
namespace assets=hlclient::assets;
namespace g=hlclient::goldsrc;
namespace audio=hlclient::audio;
namespace fixture=hlclient::tests::world_render_fixture;
using Catch::Approx;

api::ScriptedEventBinding binding(std::uint16_t slot,std::string_view token) {
    api::ScriptedEventBinding result;result.index=slot;
    REQUIRE(token.size()<result.name.size());
    std::copy(token.begin(),token.end(),result.name.begin());return result;
}
struct Module {
    api::GameClientHost host{hl::make_half_life_client_module()};
    Module() {
        host.reset({1,1,1});host.set_movement_audio_time_origin(1000.0);
        const std::array bindings{binding(901,"events/shotgun1.sc"),binding(77,"events/shotgun2.sc")};
        host.configure_scripted_events(bindings);
    }
    api::CommittedScriptedEvent event(bool twice=false,std::uint64_t record=1) const {
        api::CommittedScriptedEvent e;e.generation=1;e.record=e.record_ordinal=record;
        e.emitter_entity=3;e.event_index=twice ? 77 : 901;
        e.message_bit_offset=24;e.received_at_seconds=1001.0;
        // Standing eye at {100,80,80}; immutable aiming basis faces the wall.
        e.origin={100,80,52};e.angles={0,180,0};e.velocity={-50,0,0};return e;
    }
    api::RemoteWeaponEffectsBatch fire(bool twice=false,std::uint64_t record=1) {
        host.committed_scripted_event(event(twice,record));return host.drain_remote_effects(1.0);
    }
};
struct RemoteDrainSummary {
    std::size_t count{};
    api::RemoteWeaponEffectStatistics statistics;
};
// Keep one bounded production batch in this helper's short-lived frame rather
// than reserving a separate 89 KB /Od return temporary for every CHECK in a
// long Win32 lifecycle case. Effect-bearing drains below remain unchanged.
RemoteDrainSummary drain_summary(api::GameClientHost& host,double now) {
    const auto batch=host.drain_remote_effects(now);
    return {batch.count,batch.statistics};
}
static_assert(sizeof(RemoteDrainSummary)<=128U);
bool same(assets::AssetVector3 a,assets::AssetVector3 b) {
    return a.x==b.x && a.y==b.y && a.z==b.z;
}
std::vector<api::LocalWorldImpactRequest> pellets(const api::RemoteWeaponEffect& effect) {
    std::vector<api::LocalWorldImpactRequest> result;
    if(effect.impact) result.push_back(*effect.impact);
    REQUIRE(effect.extra_impact_count<=effect.extra_impacts.size());
    for(std::size_t i=0;i<effect.extra_impact_count;++i) result.push_back(effect.extra_impacts[i]);
    return result;
}
std::shared_ptr<const hlclient::world_render::WorldRenderPackage> wall() {
    auto world=fixture::make_world();
    for(auto& vertex:world.vertices) {
        const auto p=vertex.position;vertex.position={0,p.x*10.0F,p.y*10.0F};vertex.normal={1,0,0};
    }
    world.bounds={{0,0,0},{0,160,160}};world.surfaces[0].bounds=world.bounds;
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),fixture::make_texture_set(false)},fixture::make_lightmap_set({}));
    REQUIRE(built);return std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
}
std::shared_ptr<const assets::WorldTextureAsset> bullet_decal() {
    // Deliberately independent of the wall's uniform STONE color. Reusing the
    // wall texture as the decal would be an invisible opaque A/B fixture.
    auto texture=fixture::make_texture(true);texture.name="{PROJECTSHOT";
    for(auto& mip:texture.mip_levels) {
        for(std::size_t y=0;y<mip.height;++y) for(std::size_t x=0;x<mip.width;++x) {
            const auto at=(y*mip.width+x)*4U;
            const float dx=static_cast<float>(x)+.5F-static_cast<float>(mip.width)*.5F;
            const float dy=static_cast<float>(y)+.5F-static_cast<float>(mip.height)*.5F;
            const auto radius=std::max(.75F,static_cast<float>(mip.width)*.25F);
            mip.rgba_pixels[at]=mip.rgba_pixels[at+1]=mip.rgba_pixels[at+2]=std::byte{8};
            mip.rgba_pixels[at+3]=dx*dx+dy*dy<=radius*radius ? std::byte{255} : std::byte{0};
        }
    }
    return std::make_shared<const assets::WorldTextureAsset>(std::move(texture));
}
struct Samples final:g::SoundAssets {
    std::shared_ptr<const assets::AudioAsset> pcm;
    Samples() {auto p=std::make_shared<assets::AudioAsset>();p->sample_rate=48000;p->channel_count=1;
        p->interleaved_samples.assign(24000,0.125F);pcm=std::move(p);}
    g::SoundAssetResult request(std::uint16_t) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
};
struct Sink final:audio::Output {
    audio::OfflineOutput output;std::vector<audio::Command> starts;
    bool submit(const audio::Command& command) noexcept override {
        if(command.operation==audio::Operation::start) starts.push_back(command);
        return output.submit(command);
    }
    void set_listener(audio::Listener listener) noexcept override {output.set_listener(listener);}
};
}

TEST_CASE("E17 resource-bound remote single and double own exact effect multiplicity and immutable aim",
    "[e17][shotgun][remote-effects][game-module]") {
    for(const bool twice:{false,true}) {
        CAPTURE(twice);Module f;const auto prepare=f.host.drain_remote_audio();
        REQUIRE(prepare.prepare_count==6U);
        CHECK(prepare.prepare[3].name()=="weapons/sbarrel1.wav");
        CHECK(prepare.prepare[4].name()=="weapons/dbarrel1.wav");
        CHECK(prepare.prepare[5].name()=="weapons/sshell1.wav");
        const auto batch=f.fire(twice);REQUIRE(batch.count==1U);const auto& shot=batch.effects[0];
        REQUIRE(shot.flash);REQUIRE(shot.light);REQUIRE(shot.shell);
        CHECK(shot.action.weapon_id==7U);CHECK(shot.action.emitter_entity==3U);
        CHECK(shot.action.kind==(twice ? api::LocalWeaponAction::secondary_fire : api::LocalWeaponAction::primary_fire));
        CHECK(std::string_view{shot.shell->model_name.data()}=="models/shotgunshell.mdl");
        CHECK(shot.shell->action.sub_identity==0U);CHECK(shot.shell->expires_at_seconds==Approx(3.5));
        CHECK(shot.extra_shell_count==(twice ? 1U : 0U));
        if(twice) {
            CHECK(std::string_view{shot.extra_shells[0].model_name.data()}=="models/shotgunshell.mdl");
            CHECK(shot.extra_shells[0].action.sub_identity==1U);
            CHECK_FALSE(shot.extra_shells[0].action==shot.shell->action);
            CHECK_FALSE(same(shot.extra_shells[0].velocity,shot.shell->velocity));
        }
        const auto requests=pellets(shot);REQUIRE(requests.size()==(twice ? 8U : 4U));
        std::set<std::pair<float,float>> directions;
        for(std::size_t i=0;i<requests.size();++i) {
            const auto& r=requests[i];CHECK(r.action.sub_identity==i+1U);
            CHECK(r.maximum_distance_units==2048);CHECK_FALSE(r.tracer);
            CHECK_FALSE(r.suppress_surface_effects);REQUIRE(r.particles);
            CHECK(same(r.origin,{100,80,80}));
            const auto length=std::sqrt(r.direction.x*r.direction.x+r.direction.y*r.direction.y+r.direction.z*r.direction.z);
            CHECK(length==Approx(1.0F));
            const auto x=(r.direction.y/-r.direction.x)/(twice ? .17365F : .08716F);
            const auto y=(r.direction.z/-r.direction.x)/.04362F;
            CHECK(std::isfinite(x));CHECK(std::isfinite(y));CHECK(x*x+y*y<=1.00001F);
            directions.emplace(r.direction.y,r.direction.z);
        }
        CHECK(directions.size()==requests.size());
        const auto fire=f.host.drain_remote_audio();REQUIRE(fire.count==1U);
        CHECK(fire.cues[0].reference.name()==(twice ? "weapons/dbarrel1.wav" : "weapons/sbarrel1.wav"));
        CHECK(fire.cues[0].kind==api::LocalSoundKind::fire);REQUIRE(fire.cues[0].world_origin);
        CHECK(fire.cues[0].volume>=(twice ? .98F : .95F));CHECK(fire.cues[0].volume<=1.0F);
        CHECK(fire.cues[0].pitch>=(twice ? .85F : .93F));CHECK(fire.cues[0].pitch<=(twice ? 1.16F : 1.24F));
        CHECK(same(*fire.cues[0].world_origin,{100,80,52}));
        CHECK(fire.cues[0].channel==api::LocalSoundChannel::automatic);
        CHECK(f.host.sample(1.0).recoil_starts==0U); // No listener camera punch or remote-body command.
        f.host.committed_scripted_event(f.event(twice));CHECK(f.host.drain_remote_effects(1.01).count==0U);
        CHECK(f.host.drain_remote_audio().count==0U);
        CHECK(f.host.drain_remote_effects(1.02).count==0U);
        auto local=f.event(twice,2);local.emitter_entity=1;
        f.host.committed_scripted_event(local);CHECK(f.host.drain_remote_effects(1.03).count==0U);
        CHECK(f.host.drain_remote_audio().count==0U);
        // Independent second event creates another legitimate shot, not a duplicate.
        const auto next=f.fire(twice,3);REQUIRE(next.count==1U);
        CHECK(next.effects[0].action.command_sequence!=shot.action.command_sequence);
        CHECK(next.effects[0].extra_shell_count==shot.extra_shell_count);
        CHECK_FALSE(same(pellets(next.effects[0])[0].direction,requests[0].direction));
    }
}

TEST_CASE("E17 buckshot deterministic replay is frame independent and not square distributed",
    "[e17][shotgun][remote-effects][deterministic]") {
    for(const bool twice:{false,true}) {
        Module baseline;const auto first=baseline.fire(twice);REQUIRE(first.count==1U);
        const auto expected=pellets(first.effects[0]);
        for(const double fps:{30.0,60.0,144.0,240.0}) {
            Module replay;(void)replay.host.drain_remote_audio();
            replay.host.committed_scripted_event(replay.event(twice));
            const auto actual=replay.host.drain_remote_effects(1.0+1.0/fps);REQUIRE(actual.count==1U);
            const auto samples=pellets(actual.effects[0]);REQUIRE(samples.size()==expected.size());
            for(std::size_t i=0;i<samples.size();++i) {
                CHECK(samples[i].action==expected[i].action);
                CHECK(same(samples[i].origin,expected[i].origin));CHECK(same(samples[i].direction,expected[i].direction));
            }
        }
    }
    // Many independent identities expose square sampling: every normalized
    // spread sample must remain in the disk, including its edge quadrants.
    api::LocalWeaponActionIdentity action;action.generation=7;action.weapon_id=7;action.emitter_entity=3;
    bool positive=false,negative=false;
    for(std::uint32_t identity=1;identity<=256;++identity) {
        action.command_sequence=identity;
        for(std::uint32_t i=0;i<8;++i) {
            const auto d=hl::shotgun_pellet_direction(action,i,{1,0,0},{0,-1,0},{0,0,1},true);
            REQUIRE(d.x>0);const auto x=-d.y/d.x/.17365F,y=d.z/d.x/.04362F;
            CHECK(std::isfinite(x));CHECK(std::isfinite(y));CHECK(x*x+y*y<=1.00001F);
            positive|=x>.25F;negative|=x<-.25F;
        }
    }
    CHECK(positive);CHECK(negative);
}

TEST_CASE("E17 committed only shotgun sampling keys separate stable causal occurrences",
    "[e17][shotgun][remote-effects][deterministic][echo]") {
    api::LocalWeaponActionIdentity first;first.generation=1;first.weapon_id=7;
    first.model_index=70;first.resource_revision=2;first.canonical_pre_revision=5;
    first.kind=api::LocalWeaponAction::secondary_fire;first.started_at_seconds=1.0;
    // No local command, emitter or new canonical observation distinguishes
    // these occurrences. Their accepted causal timestamps still do.
    auto next=first;next.started_at_seconds=2.5;
    REQUIRE(first.command_sequence==0U);REQUIRE(first.emitter_entity==0U);
    REQUIRE(first!=next);
    bool different=false;
    for(std::uint32_t pellet=0;pellet<8;++pellet) {
        const auto a=hl::shotgun_pellet_direction(first,pellet,{1,0,0},{0,-1,0},{0,0,1},true);
        const auto repeated=hl::shotgun_pellet_direction(first,pellet,{1,0,0},{0,-1,0},{0,0,1},true);
        const auto b=hl::shotgun_pellet_direction(next,pellet,{1,0,0},{0,-1,0},{0,0,1},true);
        CHECK(same(a,repeated));different|=!same(a,b);
        for(const auto ray:{a,b}) {
            REQUIRE(ray.x>0);const auto x=-ray.y/ray.x/.17365F,y=ray.z/ray.x/.04362F;
            CHECK(std::isfinite(x));CHECK(std::isfinite(y));CHECK(x*x+y*y<=1.00001F);
        }
    }
    CHECK(different);
    const auto variant=hl::shotgun_fire_variant(first);
    const auto volume=hl::shotgun_fire_volume(first),pitch=hl::shotgun_fire_pitch(first);
    (void)hl::shotgun_fire_variant(next);(void)hl::shotgun_fire_volume(next);(void)hl::shotgun_fire_pitch(next);
    CHECK(hl::shotgun_fire_variant(first)==variant);
    CHECK(hl::shotgun_fire_volume(first)==volume);CHECK(hl::shotgun_fire_pitch(first)==pitch);
    // These fixed inputs were independently checked to land in different
    // finite pitch/gain buckets. Do not require that for every possible shot:
    // modulo range selection can legitimately collide for other identities.
    CHECK(hl::shotgun_fire_volume(first)==Approx(.985F));
    CHECK(hl::shotgun_fire_volume(next)==Approx(.990F));
    CHECK(hl::shotgun_fire_pitch(first)==Approx(.97F));
    CHECK(hl::shotgun_fire_pitch(next)==Approx(1.02F));
    auto zero=first;zero.started_at_seconds=0.0;
    auto negative_zero=zero;negative_zero.started_at_seconds=-0.0;
    REQUIRE(zero==negative_zero);
    CHECK(same(hl::shotgun_pellet_direction(zero,0,{1,0,0},{0,-1,0},{0,0,1},true),
        hl::shotgun_pellet_direction(negative_zero,0,{1,0,0},{0,-1,0},{0,0,1},true)));
    CHECK(hl::shotgun_fire_variant(zero)==hl::shotgun_fire_variant(negative_zero));
}

TEST_CASE("E17 remote normal application creates separate pellet hits shells and bounded isolated audio",
    "[e17][shotgun][remote-effects][world-impacts][weapon-audio]") {
    Module f;const auto batch=f.fire(true);REQUIRE(batch.count==1U);
    const auto requests=pellets(batch.effects[0]);const auto fire=f.host.drain_remote_audio();REQUIRE(fire.count==1U);
    Samples samples;Sink sink;sink.set_listener({{100,70,80},{0,1,0},1,false});
    g::LocalAudio playback(sink,true);playback.update(fire,&samples,1,true);REQUIRE(sink.starts.size()==1U);
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::all_of(pcm.begin(),pcm.end(),[](float p){return std::isfinite(p);}));
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float p){return p!=0;}));
    const auto collision=hlclient::tests::collision_brush_fixture::package(true,0,0);const auto world=wall();
    renderer::TransientVisuals shells;shells.set_collision_world(collision);
    app::WorldImpactPresentation impacts;app::RemoteEffectPresentation presenter;
    presenter.consume(batch,f.host,shells,impacts,collision,world.get(),{},true,true,1);
    CHECK(presenter.statistics().shells==2U);CHECK(shells.statistics().shells_created==2U);
    CHECK(presenter.statistics().impact_hits==8U);CHECK(impacts.active()==8U);
    impacts.update(1.01);CHECK(impacts.particles().size()==128U);
    renderer::RenderScene scene;scene.camera.position={160,80,80};presenter.present(scene,1.01);
    CHECK(scene.world_flash_count==1U);REQUIRE(scene.transient_world_light);
    impacts.append_lines(scene);CHECK(scene.world_line_count==0U);
    const auto texture=bullet_decal();
    const auto decals=impacts.frame(texture);REQUIRE(decals);CHECK(decals->vertices->size()>=8U*6U);
    std::set<std::pair<float,float>> endpoints;
    for(const auto& request:requests) {
        const auto hit=app::WorldImpactPresentation{}.submit(request,collision,world.get(),{},false,1);
        REQUIRE(hit.point);endpoints.emplace(hit.point->y,hit.point->z);
    }
    CHECK(endpoints.size()==8U);
    const auto hits=f.host.drain_remote_audio();CHECK(hits.count<=8U);
    for(std::size_t i=0;i<hits.count;++i) {
        CHECK(hits.cues[i].reference.name().starts_with("weapons/ric")); // No E7 material contact copy.
        CHECK(hits.cues[i].kind==api::LocalSoundKind::impact);REQUIRE(hits.cues[i].world_origin);
        CHECK(hits.cues[i].world_origin->x==Approx(0).margin(.2));
    }
    playback.update(hits,&samples,1,true);CHECK(sink.starts.size()==1U+hits.count);
    presenter.consume(batch,f.host,shells,impacts,collision,world.get(),{},true,true,1.01);
    CHECK(shells.statistics().shells_created==2U);CHECK(impacts.active()==8U);CHECK(f.host.drain_remote_audio().count==0U);
    unsigned contacts=0;
    for(unsigned tick=1;tick<=240;++tick) {
        const double now=1.0+tick/120.0;shells.update(now);
        for(const auto& contact:shells.contacts()) f.host.remote_shell_contact(contact);
        const auto cues=f.host.drain_remote_audio();
        for(std::size_t i=0;i<cues.count;++i) {
            CHECK(cues.cues[i].kind==api::LocalSoundKind::shell_contact);
            CHECK(cues.cues[i].reference.name()=="weapons/sshell1.wav");++contacts;
        }
    }
    CHECK(contacts>=2U);CHECK(contacts<=6U);
    f.host.reset({2,2,1});f.host.remote_world_impact(requests[0].action,{0,80,80},1.1);
    CHECK(f.host.drain_remote_audio().count==0U);CHECK(f.host.drain_remote_effects(1.1).count==0U);
}

TEST_CASE("E17 p_shotgun socket profile validates its own geometry and resolves both shells once",
    "[e17][shotgun][remote-effects][socket-profile]") {
    assets::SkeletalModelAssetData model;
    constexpr std::array<std::string_view,12> names{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1",
        "Bip01 Spine2","Bip01 Spine3","Bip01 Neck","Bip01 R Arm","Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand","Box01"};
    for(std::size_t i=0;i<names.size();++i) {
        assets::ModelBone bone;bone.name=names[i];bone.parent_index=static_cast<int>(i)-1;model.bones.push_back(bone);
    }
    model.bones[11].default_translation={12.295609F,1.386845F,1.667124F};
    model.bones[11].default_rotation_radians={1.598449F,-.010846F,.096108F};
    model.submodels.resize(1);assets::ModelSkinnedVertex vertex;vertex.position_bone_index=11;
    vertex.source_position={-12.26F,-4.01F,-1.57F};model.submodels[0].vertices.push_back(vertex);
    vertex.source_position={12.26F,4.01F,1.57F};model.submodels[0].vertices.push_back(vertex);
    hl::RemoteWeaponEffects profiles;
    auto plan=profiles.socket_plan({21,{3,4},"models/p_shotgun.mdl",model});
    CHECK(plan.semantic_match);CHECK(plan.semantic_key==7U);
    CHECK(plan.source==api::RemoteSocketSource::validated_pmodel_socket);
    CHECK(plan.muzzle.bone_index==11U);CHECK(plan.muzzle.local_origin.x==Approx(12.35));
    CHECK(plan.muzzle.local_origin.y==Approx(2.77));CHECK(plan.muzzle.forward.x==1);
    CHECK(plan.muzzle.right.z==-1);CHECK(plan.shell.local_origin.z==Approx(-1.65));
    for(unsigned failure=0;failure<4;++failure) {
        auto bad=model;
        if(failure==0) bad.bones[11].name="Line02";
        if(failure==1) bad.bones[11].default_rotation_radians.x=0;
        if(failure==2) bad.submodels[0].vertices[1].source_position.x=4.12F;
        if(failure==3) bad.submodels[0].vertices[0].position_bone_index=10;
        CHECK(profiles.socket_plan({21,{3,5+failure},"models/p_shotgun.mdl",bad}).source==api::RemoteSocketSource::unavailable);
    }
    model.attachments={{"muzzle",0,11,{12.35F,2.77F,0},{}},{"shell",0,11,{2,2.77F,-1.65F},{}}};
    plan=profiles.socket_plan({21,{3,10},"models/p_shotgun.mdl",model});
    CHECK(plan.source==api::RemoteSocketSource::explicit_model_attachment);
    REQUIRE(plan.muzzle_attachment);REQUIRE(plan.shell_attachment);
    CHECK(*plan.muzzle_attachment==0U);CHECK(*plan.shell_attachment==1U);
    Module f;api::RemoteWeaponPoseSample pose;
    pose.generation=pose.map_generation=1;pose.entity_lifetime=9;pose.entity=3;
    pose.model_slot=21;pose.current_model_slot=21;pose.source_record=pose.source_ordinal=1;
    pose.frame_identity=17;pose.resource_id=21;pose.observed_at_seconds=pose.presented_at_seconds=1;
    pose.semantic_known=pose.semantic_match=true;pose.semantic_key=7;
    pose.source=api::RemoteSocketSource::validated_pmodel_socket;pose.server_sequence=11;
    pose.muzzle={{70,81,83},{-1,0,0},{0,1,0},{0,0,1}};
    pose.shell={{75,82,82},{-1,0,0},{0,1,0},{0,0,1}};
    f.host.remote_weapon_poses(std::span{&pose,1});auto event=f.event(true);event.weapon_model_index=21;
    f.host.committed_scripted_event(event);const auto batch=f.host.drain_remote_effects(1);REQUIRE(batch.count==1U);
    const auto& shot=batch.effects[0];CHECK(shot.pose.status==api::RemotePoseResolution::resolved);
    CHECK(shot.pose.server_sequence==11U);REQUIRE(shot.flash);REQUIRE(shot.shell);
    CHECK(same(shot.flash->origin,pose.muzzle.origin));CHECK(same(shot.shell->origin,pose.shell.origin));
    REQUIRE(shot.extra_shell_count==1U);CHECK(same(shot.extra_shells[0].origin,pose.shell.origin));
    CHECK_FALSE(same(shot.extra_shells[0].velocity,shot.shell->velocity));
    for(const auto& request:pellets(shot)) CHECK(same(request.origin,{100,80,80}));
    pose.muzzle.origin={170,180,180};pose.shell.origin={175,182,182};
    f.host.remote_weapon_poses(std::span{&pose,1});
    CHECK(same(shot.flash->origin,{70,81,83}));CHECK(same(shot.extra_shells[0].origin,{75,82,82}));
    CHECK(f.host.drain_remote_effects(1.01).count==0U);
}

TEST_CASE("E17 shotgun event mismatches remain explicit and do not fall back to numeric scripts",
    "[e17][shotgun][remote-effects][lifecycle]") {
    Module f;auto unsupported=f.event();unsupported.event_index=7;
    f.host.committed_scripted_event(unsupported);CHECK(f.host.drain_remote_effects(1).count==0U);
    CHECK(f.host.drain_remote_audio().count==0U);
    unsupported=f.event(false,2);unsupported.resolution=api::ScriptedEventResolution::missing_packet;
    f.host.committed_scripted_event(unsupported);CHECK(f.host.drain_remote_effects(1).count==0U);
    auto late=f.event(true,3);late.received_at_seconds=1000;
    f.host.committed_scripted_event(late);CHECK(f.host.drain_remote_effects(1).count==0U);
    CHECK(f.host.drain_remote_audio().count==0U);
    api::RemoteWeaponPoseSample pose;pose.generation=pose.map_generation=1;pose.entity=3;
    pose.entity_lifetime=9;pose.model_slot=21;pose.current_model_slot=21;
    pose.source_record=pose.source_ordinal=4;pose.frame_identity=17;pose.resource_id=8;
    pose.observed_at_seconds=pose.presented_at_seconds=1;pose.semantic_known=pose.semantic_match=true;
    pose.semantic_key=4;pose.source=api::RemoteSocketSource::validated_pmodel_socket;
    pose.muzzle={{70,80,80},{-1,0,0},{0,1,0},{0,0,1}};pose.shell=pose.muzzle;
    f.host.remote_weapon_poses(std::span{&pose,1});auto switched=f.event(true,4);switched.weapon_model_index=21;
    f.host.committed_scripted_event(switched);const auto batch=f.host.drain_remote_effects(1);REQUIRE(batch.count==1U);
    CHECK(batch.effects[0].pose.status==api::RemotePoseResolution::weapon_mismatch);
    CHECK_FALSE(batch.effects[0].flash);CHECK_FALSE(batch.effects[0].light);CHECK_FALSE(batch.effects[0].shell);
    CHECK(batch.effects[0].extra_shell_count==0U);CHECK(pellets(batch.effects[0]).size()==8U);
    f.host.teardown();CHECK(f.host.drain_remote_audio().count==0U);
}

TEST_CASE("E17 pellet and both shell identities deduplicate independently through death and respawn",
    "[e17][shotgun][remote-effects][lifecycle][shell-audio]") {
    Module f;hlclient::client::RuntimePacketEntityObservation entity;
    entity.entity_number=3;entity.player_movement_schema=entity.ordinary_visual_schema=true;
    entity.model_index=7;entity.weapon_model_index=21;
    std::array entities{entity};f.host.remote_entities(entities,8,1);
    const auto first=f.fire(true);REQUIRE(first.count==1U);
    const auto requests=pellets(first.effects[0]);(void)f.host.drain_remote_audio();
    for(const auto& request:requests) f.host.remote_world_impact(request.action,{0,80,80},1.01);
    const auto hit_sounds=f.host.drain_remote_audio();CHECK(hit_sounds.count<=8U);
    for(std::size_t i=0;i<hit_sounds.count;++i) CHECK(hit_sounds.cues[i].reference.name().starts_with("weapons/ric"));
    CHECK(drain_summary(f.host,1.01).statistics.impacts==8U);
    for(const auto& request:requests) f.host.remote_world_impact(request.action,{0,80,80},1.02);
    CHECK(f.host.drain_remote_audio().count==0U);CHECK(drain_summary(f.host,1.02).statistics.impacts==8U);
    auto invalid=requests[0].action;invalid.sub_identity=9;
    f.host.remote_world_impact(invalid,{0,80,80},1.02);CHECK(f.host.drain_remote_audio().count==0U);
    auto pending=f.event(true,2);f.host.committed_scripted_event(pending);
    entities[0].weapon_model_index=0;f.host.remote_entities(entities,8,3);
    CHECK(drain_summary(f.host,1.02).count==0U);CHECK(f.host.drain_remote_audio().count==0U);
    // Materialized shells belong to their own world lifetime. Death does not
    // erase the original shot/contact owner, nor replay the queued new shot.
    REQUIRE(first.effects[0].shell);REQUIRE(first.effects[0].extra_shell_count==1U);
    for(const auto action:std::array{first.effects[0].shell->action,first.effects[0].extra_shells[0].action}) {
        const api::LocalShellContact contact{action,1,{0,80,68},80,1.2};
        f.host.remote_shell_contact(contact);f.host.remote_shell_contact(contact);
    }
    const auto shell_sounds=f.host.drain_remote_audio();REQUIRE(shell_sounds.count==2U);
    for(std::size_t i=0;i<shell_sounds.count;++i) {
        CHECK(shell_sounds.cues[i].kind==api::LocalSoundKind::shell_contact);
        CHECK(shell_sounds.cues[i].reference.name()=="weapons/sshell1.wav");
    }
    CHECK(drain_summary(f.host,1.2).statistics.shell_contacts==2U);
    entities[0]=entity;f.host.remote_entities(entities,8,4);
    auto old=f.event(true,5);old.record_ordinal=2;
    f.host.committed_scripted_event(old);CHECK(drain_summary(f.host,1.2).count==0U);
    auto fresh=f.event(true,6);fresh.record_ordinal=4;f.host.committed_scripted_event(fresh);
    const auto respawn=f.host.drain_remote_effects(1.2);REQUIRE(respawn.count==1U);
    CHECK(respawn.effects[0].extra_shell_count==1U);CHECK(pellets(respawn.effects[0]).size()==8U);
    f.host.remote_slot_boundary(3);f.host.committed_scripted_event(f.event(true,7));
    CHECK(drain_summary(f.host,1.2).count==0U);
    f.host.reset({2,2,1});f.host.remote_shell_contact({first.effects[0].shell->action,2,{0,80,68},80,1.3});
    CHECK(f.host.drain_remote_audio().count==0U);
}

TEST_CASE("E17 production OpenGL shows localized multiple remote shotgun impacts without tracers",
    "[e17][shotgun][remote-effects][opengl][actual-context]") {
    Module f;const auto batch=f.fire(true);const auto world=wall();
    const auto collision=hlclient::tests::collision_brush_fixture::package(true,0,0);
    renderer::TransientVisuals shells;app::WorldImpactPresentation impacts;app::RemoteEffectPresentation presenter;
    presenter.consume(batch,f.host,shells,impacts,collision,world.get(),{},false,true,1);
    const auto texture=bullet_decal();
    const auto decals=impacts.frame(texture);REQUIRE(decals);CHECK(impacts.active()==8U);
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();renderer::RenderScene base;
    base.static_world=renderer::RenderStaticWorld{world};base.camera.position={180,80,80};base.camera.target={0,80,80};
    const auto observe=[&](const renderer::RenderScene& scene) {
        context->renderer().render(scene,{320,240});
        auto pixels=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        REQUIRE(pixels);CHECK(glGetError()==GL_NO_ERROR);return pixels;
    };
    const auto before=observe(base);auto marked=base;marked.world_decals=decals;
    const auto after=observe(marked);CHECK(after.color_signature!=before.color_signature);
    CHECK(std::equal(before.rgba8.begin(),before.rgba8.begin()+4,after.rgba8.begin()));
    CHECK(observe(marked).color_signature==after.color_signature);
    auto flashes=base;presenter.present(flashes,1.01);CHECK(flashes.world_flash_count==1U);
    CHECK(observe(flashes).color_signature!=before.color_signature);
    impacts.update(21.01);CHECK_FALSE(impacts.frame(texture));
    CHECK(observe(base).color_signature==before.color_signature);
    context->release_renderer();
}
