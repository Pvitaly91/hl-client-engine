#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/app/remote_effect_presentation.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include "local_resource_readiness_test_fixture.hpp"
#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
static_assert(sizeof(hlclient::app::RemoteEffectPresentation)<32U*1024U,
    "Persistent presentation storage must not retain one-shot pellet batches");
#include <algorithm>
#include <cmath>
#include <set>
#include <thread>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
namespace a=hlclient::audio;
namespace assets=hlclient::assets;
namespace app=hlclient::app;
namespace renderer=hlclient::renderer;
namespace readiness=hlclient::tests::readiness_fixture;
namespace world_fixture=hlclient::tests::world_render_fixture;
using Catch::Approx;
using namespace std::chrono_literals;

api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=70;m.resource_revision=2;
    m.resource_name="models/v_shotgun.mdl";m.supported_bodies.fill(true);m.selectable_bodies.fill(true);
    constexpr std::array labels{"sm_idle","shoot","shoot_big","reload","pump","start_reload","draw","reholster","idle4","deepidle"};
    constexpr std::array fps{9.0,20.0,13.0,16.0,13.0,12.0,24.0,30.0,9.0,12.0};
    constexpr std::array frames{21U,21U,21U,10U,11U,9U,13U,11U,21U,61U};
    for(std::size_t i=0;i<labels.size();++i) m.sequences.push_back({fps[i],frames[i],i==0||i>=8,{},labels[i]});
    for(const unsigned i:{1U,2U}) m.sequences[i].events.push_back({0,5001,0,{std::byte{'1'},std::byte{'5'}}});
    // A tempting imported audio marker must not duplicate the actual server
    // insertion/pump sounds. These project-owned markers are a negative control.
    for(const auto& [sequence,token]:std::array{
        std::pair{3U,"weapons/reload1.wav"},std::pair{4U,"weapons/scock1.wav"},std::pair{5U,"weapons/reload3.wav"}}) {
        assets::ModelSequenceEvent event;event.frame=0;event.event_number=5004;
        for(const char letter:std::string_view{token}) event.options.push_back(static_cast<std::byte>(letter));
        m.sequences[sequence].events.push_back(std::move(event));
    }
    return m;
}
c::RuntimeClientObservationState state(int clip=8,int reserve=32,std::size_t record=1,int reload=0) {
    c::RuntimeClientObservationState s;s.generation=1;s.publication_revision=record;
    s.client_metadata={.generation=1,.freshness=c::RuntimeObservationFreshness::observed_in_record,
        .source=c::RuntimeObservationSource{.record_identity=record,.record_ordinal=record}};
    s.receiving_client.emplace();s.receiving_client->viewmodel_index=70;s.receiving_client->health=100;
    s.receiving_client->water_level=0;s.receiving_client->next_weapon_attack=0;
    s.receiving_client->owned_weapon_bits=1U<<7;s.receiving_client->weapon_animation=0;
    s.weapon_hud.active_weapon_id=7;s.weapon_hud.clips[7]=static_cast<std::int16_t>(clip);
    s.weapon_hud.reserve_ammo[2]=static_cast<std::uint8_t>(reserve);
    s.weapon_hud.catalogue.push_back({.id=7,.command_name="weapon_shotgun",.primary_ammo_type=2,.slot=2,
        .source={.record_identity=record,.record_ordinal=record}});
    s.weapon_slots.push_back({.wire_slot=7,.clip=clip,.in_reload=false,.next_reload=0,
        .next_primary_attack=0,.weapon_id=7,.next_secondary_attack=0,.time_weapon_idle=0,
        .pump_time=std::nullopt,.special_reload_state=reload});
    return s;
}
api::LocalWeaponSubmittedCommand command(bool double_shot=false,unsigned sequence=1,double at=.02) {
    api::LocalWeaponSubmittedCommand cmd{1,sequence,double_shot ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack,at};
    cmd.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{100,80,80},{-1,0,0},{0,1,0},{0,0,1}};return cmd;
}
api::LocalVisualContext changed_camera(double at=.02) {
    return {{500,900,700},{0,1,0},{1,0,0},{0,0,1},{-50,0,0},at};
}
struct Module {
    api::GameClientHost host{hl::make_half_life_client_module()};
    c::RuntimeClientObservationState observed;
    explicit Module(c::RuntimeClientObservationState initial=state()):observed(std::move(initial)) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(1000);
        host.bind_model(model());host.observe(observed,0);(void)host.sample(0);
        std::array<api::ScriptedEventBinding,2> bindings{};
        bindings[0].index=97;bindings[1].index=42;
        const std::array tokens{"events/shotgun1.sc","events/shotgun2.sc"};
        for(std::size_t i=0;i<bindings.size();++i) std::copy_n(tokens[i],std::char_traits<char>::length(tokens[i]),bindings[i].name.begin());
        host.configure_scripted_events(bindings);
    }
};
std::vector<api::LocalWorldImpactRequest> pellets(const api::LocalVisualFrame& frame) {
    std::vector<api::LocalWorldImpactRequest> result;if(frame.world_impact) result.push_back(*frame.world_impact);
    REQUIRE(frame.extra_impact_count<=frame.extra_impacts.size());
    for(std::size_t i=0;i<frame.extra_impact_count;++i) result.push_back(frame.extra_impacts[i]);return result;
}
bool same(assets::AssetVector3 p,assets::AssetVector3 q) {return p.x==q.x&&p.y==q.y&&p.z==q.z;}
std::shared_ptr<const hlclient::world_render::WorldRenderPackage> wall() {
    auto world=world_fixture::make_world();
    for(auto& vertex:world.vertices) {const auto p=vertex.position;vertex.position={0,p.x*10,p.y*10};vertex.normal={1,0,0};}
    world.bounds={{0,0,0},{0,160,160}};world.surfaces[0].bounds=world.bounds;
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),world_fixture::make_texture_set(false)},world_fixture::make_lightmap_set({}));
    REQUIRE(built);return std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
}
std::vector<std::byte> wav() {
    std::vector<std::byte> bytes;
    const auto text=[&](std::string_view token){for(const auto c:token) bytes.push_back(static_cast<std::byte>(c));};
    const auto number=[&](std::uint32_t value,unsigned count=4){for(unsigned i=0;i<count;++i) bytes.push_back(static_cast<std::byte>((value>>(8*i))&255));};
    text("RIFF");number(36+4800);text("WAVEfmt ");number(16);number(1,2);number(1,2);
    number(22050);number(22050);number(1,2);number(8,2);text("data");number(4800);
    for(unsigned i=0;i<4800;++i) bytes.push_back(static_cast<std::byte>(i%32<16 ? 160 : 96));return bytes;
}
struct ApprovedSamples {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment;
    std::unique_ptr<g::ApprovedSoundAssets> loader;
    ApprovedSamples() {
        root.write("valve","maps/test.bsp","project-owned metadata fixture");
        for(const auto token:{"weapons/sbarrel1.wav","weapons/dbarrel1.wav","weapons/sshell1.wav",
            "weapons/reload1.wav","weapons/reload3.wav","weapons/scock1.wav",
            "weapons/ric1.wav","weapons/ric2.wav","weapons/ric3.wav","weapons/ric4.wav","weapons/ric5.wav"})
            root.write("valve",std::string{"sound/"}+token,wav());
        const auto resources=readiness::parse_resource_list({{2,"maps/test.bsp",1,0xffffffU,0},
            {0,"weapons/reload1.wav",11,0xffffffU,0},{0,"weapons/reload3.wav",12,0xffffffU,0},
            {0,"weapons/scock1.wav",13,0xffffffU,0}});
        environment=std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>{readiness::make_environment(root)};
        const auto inventory=readiness::build_inventory(resources,*environment);
        auto manifest=readiness::build_manifest(resources,inventory,readiness::parse_server_info("maps/test.bsp"),*environment);
        REQUIRE(manifest);loader=std::make_unique<g::ApprovedSoundAssets>(g::PreparedSoundResources{
            std::make_shared<const g::PrecacheManifestState>(std::move(*manifest.state)),environment});
    }
    g::SoundAssetResult ready(const api::LocalSoundReference& reference) {
        auto result=loader->request_local(reference);const auto deadline=std::chrono::steady_clock::now()+2s;
        while(result.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);result=loader->request_local(reference);
        }
        REQUIRE(result.status==g::SoundAssetStatus::ready);REQUIRE(result.asset);
        CHECK(result.asset->sample_rate==22050);CHECK(result.asset->channel_count==1U);
        CHECK(std::any_of(result.asset->interleaved_samples.begin(),result.asset->interleaved_samples.end(),[](float p){return p!=0;}));
        return result;
    }
    void prepare(const api::LocalAudioBatch& batch) {
        // This fixture intentionally owns only weapon resources. Movement
        // preparation shares the bounded batch but is not a weapon readiness
        // dependency. Require all three new positive paths, not arbitrary
        // unrelated footstep WAVs or merely a nonempty preparation count.
        std::set<std::string_view> required;
        REQUIRE(batch.prepare_count<=batch.prepare.size());
        for(std::size_t i=0;i<batch.prepare_count;++i) {
            const auto name=batch.prepare[i].name();
            if(name=="weapons/sbarrel1.wav" || name=="weapons/dbarrel1.wav" ||
                name=="weapons/sshell1.wav") {required.insert(name);(void)ready(batch.prepare[i]);}
        }
        CHECK(required.size()==3U);
    }
};
struct Sink:a::Output {
    a::OfflineOutput output;std::vector<a::Command> starts;
    bool submit(const a::Command& command) noexcept override {
        if(command.operation==a::Operation::start) starts.push_back(command);return output.submit(command);
    }
    void set_listener(a::Listener listener) noexcept override {output.set_listener(listener);}
};
void deliver_ready_local(g::LocalAudio& playback,Sink& sink,ApprovedSamples& resources,
    const api::LocalAudioBatch& batch,double now) {
    const auto starts_before=sink.starts.size();
    const auto submitted_before=playback.statistics().submitted;
    // A ready cache entry can still transiently return pending through its
    // nonblocking try_lock while incremental movement prewarming is working.
    // Deliver the event once; subsequent ordinary updates contain no cue or
    // new preload. Keep simulation time fixed, hence the production 250 ms
    // event deadline is neither extended nor replaced by this fixture wait.
    playback.update(batch,resources.loader.get(),now,true);
    api::LocalAudioBatch empty;empty.session=batch.session;empty.scope=batch.scope;
    empty.movement_epoch=batch.movement_epoch;
    const auto deadline=std::chrono::steady_clock::now()+2s;
    unsigned updates=1;
    while(sink.starts.size()<starts_before+batch.count && updates<2001U &&
        playback.statistics().resource_pending && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(1ms);
        playback.update(empty,resources.loader.get(),now,true);++updates;
    }
    const auto& stats=playback.statistics();
    INFO("local ready delivery pending="<<stats.resource_pending<<" missing="<<stats.missing
        <<" late="<<stats.late<<" rejected="<<stats.limits<<" updates="<<updates);
    CHECK(stats.resource_pending<=static_cast<std::uint64_t>(updates)*batch.count);
    CHECK(stats.missing==0U);CHECK(stats.late==0U);CHECK(stats.cancelled==0U);
    CHECK(stats.muted==0U);CHECK(stats.limits==0U);
    CHECK(stats.resource_not_authorized==0U);CHECK(stats.resource_open_failed==0U);
    CHECK(stats.resource_decode_failed==0U);CHECK(stats.output_queue_rejected==0U);
    CHECK(stats.impact_rejected==0U);CHECK(stats.shell_rejected==0U);
    CHECK(stats.submitted==submitted_before+batch.count);
    REQUIRE(sink.starts.size()==starts_before+batch.count);
}
void deliver_ready_server(g::ServerAudio& playback,Sink& sink,ApprovedSamples& resources,
    g::SoundTime now,std::size_t expected_starts) {
    // consume() is intentionally outside this helper and happens exactly once.
    // Slot delivery, not a replayed event, is retried after cache contention.
    playback.update(resources.loader.get(),now);
    const auto deadline=std::chrono::steady_clock::now()+2s;
    unsigned updates=1;
    while(sink.starts.size()<expected_starts && updates<2001U &&
        playback.statistics().pending && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(1ms);
        playback.update(resources.loader.get(),now);++updates;
    }
    const auto& stats=playback.statistics();
    INFO("server ready delivery pending="<<stats.pending<<" missing="<<stats.missing
        <<" expired="<<stats.expired<<" rejected="<<stats.voice_rejected<<" updates="<<updates);
    CHECK(stats.pending<=expected_starts);CHECK(stats.missing==0U);
    // Previously completed insertion voices may legitimately expire when the
    // next server sound arrives. The newly queued event must still start;
    // expiry cannot account for every successfully consumed sound.
    CHECK(stats.expired<expected_starts);
    CHECK(stats.unsupported==0U);CHECK(stats.formats==0U);CHECK(stats.limits==0U);
    CHECK(stats.no_voice==0U);CHECK(stats.not_authorized==0U);CHECK(stats.open_failed==0U);
    CHECK(stats.decode_failed==0U);CHECK(stats.voice_rejected==0U);CHECK(stats.invalid_origin==0U);
    CHECK(stats.started==expected_starts);REQUIRE(sink.starts.size()==expected_starts);
}
void pcm_nonzero(Sink& sink) {
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::all_of(pcm.begin(),pcm.end(),[](float p){return std::isfinite(p);}));
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float p){return p!=0;}));
}
}

TEST_CASE("E17 normal local module composes frozen single double pellets shotshells HUD and ready fire PCM",
    "[e17][shotgun][game-module][composition][weapon-visuals][weapon-audio]") {
    ApprovedSamples resources;
    for(const bool twice:{false,true}) {
        CAPTURE(twice);Module f;const auto original=f.observed;
        const auto before=f.host.hud(f.observed,0);CHECK(before.clip==8);CHECK(before.primary_reserve==32);
        const auto prepared=f.host.drain_audio();resources.prepare(prepared);
        const auto cmd=command(twice);f.host.submit(cmd,.02);const auto shot=f.host.sample(.02);
        REQUIRE(shot.identity);REQUIRE(shot.visual);CHECK(shot.visual->sequence==(twice ? 2U : 1U));
        CHECK(shot.local_punch_pitch_degrees==Approx(twice ? -10 : -5));
        const auto frame=f.host.local_visuals(changed_camera());
        REQUIRE(frame.flash);REQUIRE(frame.light);REQUIRE(frame.shell);
        CHECK(frame.statistics.flash_scheduled==1U);CHECK(frame.statistics.light_requested==1U);
        CHECK(frame.extra_shell_count==(twice ? 1U : 0U));
        CHECK(std::string_view{frame.shell->model_name.data()}=="models/shotgunshell.mdl");
        CHECK(same(frame.shell->origin,{68,86,68})); // SDK fallback32 forward,-12up,+6right; not later camera.
        CHECK(frame.shell->velocity.x==-75);CHECK(frame.shell->velocity.y>=50);CHECK(frame.shell->velocity.y<=70);
        CHECK(frame.shell->velocity.z>=100);CHECK(frame.shell->velocity.z<=150);
        CHECK(frame.shell->action.sub_identity==0U);CHECK(frame.shell->expires_at_seconds==Approx(2.52));
        if(twice) {CHECK(frame.extra_shells[0].action.sub_identity==1U);CHECK(same(frame.extra_shells[0].origin,frame.shell->origin));}
        const auto rays=pellets(frame);REQUIRE(rays.size()==(twice ? 8U : 4U));
        std::set<std::pair<float,float>> pattern;
        for(std::size_t i=0;i<rays.size();++i) {
            CHECK(rays[i].action.sub_identity==i+1U);CHECK(rays[i].action.command_sequence==cmd.sequence);
            CHECK(same(rays[i].origin,{100,80,80}));CHECK(rays[i].direction.x<0);
            CHECK_FALSE(rays[i].tracer);CHECK(rays[i].maximum_distance_units==2048);REQUIRE(rays[i].particles);
            const auto x=rays[i].direction.y/-rays[i].direction.x/(twice ? .17365F : .08716F);
            const auto y=rays[i].direction.z/-rays[i].direction.x/.04362F;
            CHECK(x*x+y*y<=1.00001F);pattern.emplace(rays[i].direction.y,rays[i].direction.z);
        }
        CHECK(pattern.size()==rays.size());CHECK(f.observed==original);
        const auto hud=f.host.hud(f.observed,.02);CHECK(hud.clip==8);CHECK(hud.primary_reserve==32);
        const auto fire=f.host.drain_audio();REQUIRE(fire.count==1U);
        CHECK(fire.cues[0].reference.name()==(twice ? "weapons/dbarrel1.wav" : "weapons/sbarrel1.wav"));
        CHECK(fire.cues[0].kind==api::LocalSoundKind::fire);CHECK_FALSE(fire.cues[0].world_origin);
        (void)resources.ready(fire.cues[0].reference);Sink sink;g::LocalAudio playback(sink);
        deliver_ready_local(playback,sink,resources,fire,.02);REQUIRE(sink.starts.size()==1U);pcm_nonzero(sink);
        playback.update(fire,resources.loader.get(),.02,true);CHECK(sink.starts.size()==1U);
        f.host.submit(cmd,.03);(void)f.host.sample(.03);
        const auto repeated=f.host.local_visuals(changed_camera(.03));CHECK_FALSE(repeated.shell);
        CHECK(repeated.extra_shell_count==0U);CHECK_FALSE(repeated.world_impact);CHECK(repeated.extra_impact_count==0U);
        CHECK(f.host.drain_audio().count==0U);
        api::CommittedScriptedEvent echo;echo.generation=1;echo.record=echo.record_ordinal=2;
        echo.message_bit_offset=8;echo.emitter_entity=1;echo.event_index=twice ? 42 : 97;echo.received_at_seconds=1000.04;
        f.host.committed_scripted_event(echo);const auto confirmed=f.host.sample(.04);
        REQUIRE(confirmed.visual);CHECK(confirmed.visual->restart_identity==shot.visual->restart_identity);
        CHECK(confirmed.recoil_starts==1U);CHECK(confirmed.actions_started==1U);
        f.host.committed_scripted_event(echo);(void)f.host.sample(.04);
        CHECK(f.host.drain_audio().count==0U);CHECK(f.host.drain_remote_effects(.04).count==0U);
        const auto echo_frame=f.host.local_visuals(changed_camera(.04));CHECK_FALSE(echo_frame.shell);
        CHECK_FALSE(echo_frame.world_impact);CHECK(echo_frame.extra_shell_count==0U);CHECK(echo_frame.extra_impact_count==0U);
        auto authoritative=state(twice ? 6 : 7,32,3);f.host.observe(authoritative,.06);
        const auto server_hud=f.host.hud(authoritative,.06);CHECK(server_hud.clip==(twice ? 6 : 7));
        CHECK(server_hud.primary_reserve==32);CHECK(original.weapon_slots.front().clip==8);
    }
}

TEST_CASE("E17 local normal multi hit application keeps buckshot audio separate and shells alive during reload",
    "[e17][shotgun][game-module][composition][world-impacts][shell-audio]") {
    ApprovedSamples resources;Module f;resources.prepare(f.host.drain_audio());
    f.host.submit(command(true),.02);(void)f.host.sample(.02);const auto frame=f.host.local_visuals(changed_camera());
    const auto rays=pellets(frame);REQUIRE(rays.size()==8U);(void)f.host.drain_audio();
    const auto collision=hlclient::tests::collision_brush_fixture::package(true,0,0);const auto render_wall=wall();
    app::WorldImpactPresentation impacts;renderer::TransientVisuals shells;shells.set_collision_world(collision);
    REQUIRE(frame.shell);REQUIRE(shells.spawn(*frame.shell,.02));REQUIRE(frame.extra_shell_count==1U);
    REQUIRE(shells.spawn(frame.extra_shells[0],.02));
    std::set<std::pair<float,float>> hits;
    for(const auto& request:rays) {
        const auto hit=impacts.submit(request,collision,render_wall.get(),{},true,.02);
        REQUIRE(hit.status==app::WorldImpactStatus::hit);REQUIRE(hit.point);
        hits.emplace(hit.point->y,hit.point->z);f.host.accepted_world_impact(request.action,*hit.point,.02,hit.surface);
        f.host.accepted_world_impact(request.action,*hit.point,.02,hit.surface);
    }
    CHECK(hits.size()==8U);CHECK(impacts.active()==8U);impacts.update(.03);CHECK(impacts.particles().size()==128U);
    renderer::RenderScene scene;impacts.append_lines(scene);CHECK(scene.world_line_count==0U);
    const auto hit_audio=f.host.drain_audio();CHECK(hit_audio.count<=8U);
    for(std::size_t i=0;i<hit_audio.count;++i) {
        CHECK(hit_audio.cues[i].reference.name().starts_with("weapons/ric"));
        (void)resources.ready(hit_audio.cues[i].reference);
    }
    CHECK(f.host.drain_audio().count==0U);
    // Server-approved special reload does not cancel already materialized
    // world shells or gate contact audio on the current viewmodel action.
    f.host.observe(state(6,32,2,1),.1);(void)f.host.sample(.1);CHECK(f.host.drain_audio().count==0U);
    unsigned contacts=0;
    for(unsigned tick=1;tick<=240;++tick) {
        const double now=.02+tick/120.0;shells.update(now);
        for(const auto& contact:shells.contacts()) {f.host.shell_contact(contact);f.host.shell_contact(contact);}
        const auto audio=f.host.drain_audio();
        for(std::size_t i=0;i<audio.count;++i) {
            CHECK(audio.cues[i].kind==api::LocalSoundKind::shell_contact);
            CHECK(audio.cues[i].reference.name()=="weapons/sshell1.wav");
            REQUIRE(audio.cues[i].world_origin);CHECK(audio.cues[i].world_origin->x==Approx(0).margin(.2));++contacts;
        }
        if(audio.count) {
            Sink sink;g::LocalAudio isolated(sink);deliver_ready_local(isolated,sink,resources,audio,now);
            CHECK(sink.starts.size()==audio.count);pcm_nonzero(sink);
        }
    }
    CHECK(contacts>=2U);CHECK(contacts<=6U);CHECK(f.observed.weapon_hud.clips[7]==8);
    f.host.reset({2,2,1});f.host.accepted_world_impact(rays[0].action,{0,80,80},.2);
    CHECK(f.host.drain_audio().count==0U);CHECK_FALSE(f.host.local_visuals(changed_camera(.2)).world_impact);
}

TEST_CASE("E17 normal module committed only double shotgun event freezes its own eye and aim once",
    "[e17][shotgun][game-module][composition][echo][weapon-visuals]") {
    ApprovedSamples resources;Module f;resources.prepare(f.host.drain_audio());
    const auto before=f.observed;
    api::CommittedScriptedEvent event;event.generation=1;event.record=event.record_ordinal=2;
    event.message_bit_offset=24;event.emitter_entity=1;event.event_index=42;
    event.received_at_seconds=1000.02;event.origin={100,80,52};event.angles={0,180,0};
    f.host.committed_scripted_event(event);const auto sample=f.host.sample(.02);
    REQUIRE(sample.identity);REQUIRE(sample.visual);
    CHECK(sample.identity->command_sequence==0U);CHECK(sample.visual->sequence==2U);
    CHECK(sample.visual->source==api::LocalWeaponAnimationSource::service_event);
    CHECK(sample.actions_started==1U);CHECK(sample.secondary_fire_confirmed==1U);
    const auto frame=f.host.local_visuals(changed_camera());
    REQUIRE(frame.flash);REQUIRE(frame.light);REQUIRE(frame.shell);
    CHECK(frame.extra_shell_count==1U);
    CHECK(frame.shell->origin.x==Approx(68));CHECK(frame.shell->origin.y==Approx(86));
    CHECK(frame.shell->origin.z==Approx(68));
    CHECK(std::abs(std::remainder(frame.shell->yaw_degrees-180.0F,360.0F))<.001F);
    const auto rays=pellets(frame);REQUIRE(rays.size()==8U);
    for(std::size_t i=0;i<rays.size();++i) {
        CHECK(same(rays[i].origin,{100,80,80}));CHECK(rays[i].direction.x<0);
        CHECK(rays[i].action.sub_identity==i+1U);CHECK_FALSE(rays[i].tracer);
    }
    const auto fire=f.host.drain_audio();REQUIRE(fire.count==1U);
    CHECK(fire.cues[0].reference.name()=="weapons/dbarrel1.wav");
    Sink sink;g::LocalAudio playback(sink);deliver_ready_local(playback,sink,resources,fire,.02);
    REQUIRE(sink.starts.size()==1U);pcm_nonzero(sink);
    f.host.committed_scripted_event(event);(void)f.host.sample(.03);
    const auto duplicate=f.host.local_visuals(changed_camera(.03));
    CHECK_FALSE(duplicate.shell);CHECK_FALSE(duplicate.world_impact);
    CHECK(duplicate.extra_shell_count==0U);CHECK(duplicate.extra_impact_count==0U);
    CHECK(f.host.drain_audio().count==0U);CHECK(f.host.drain_remote_effects(.03).count==0U);
    CHECK(f.observed==before);CHECK(f.host.hud(f.observed,.03).clip==8);
    f.host.reset({2,2,1});f.host.committed_scripted_event(event);(void)f.host.sample(.04);
    CHECK_FALSE(f.host.local_visuals(changed_camera(.04)).world_impact);
    CHECK(f.host.drain_audio().count==0U);
    // RX can carry a committed timestamp slightly earlier than the current
    // app sample. Context capture must not rewind the controller clock. The
    // 100 ms case keeps still-valid pellet work but expires the 75 ms flash;
    // the 20 ms case also emits both shells exactly once.
    for(const double received:{.9,.98}) {
        CAPTURE(received);Module delayed;resources.prepare(delayed.host.drain_audio());
        delayed.host.observe(state(8,32,2),1.0);(void)delayed.host.sample(1.0);
        auto earlier=event;earlier.record=earlier.record_ordinal=3;
        earlier.received_at_seconds=1000.0+received;
        delayed.host.committed_scripted_event(earlier);const auto late=delayed.host.sample(1.0);
        CHECK(late.error!=api::LocalWeaponPresentationError::invalid_time);
        REQUIRE(late.identity);REQUIRE(late.visual);CHECK(late.visual->sequence==2U);
        CHECK(late.secondary_fire_confirmed==1U);CHECK(late.identity->started_at_seconds==Approx(received));
        const auto effects=delayed.host.local_visuals(changed_camera(1.0));
        const auto late_rays=pellets(effects);REQUIRE(late_rays.size()==8U);
        for(const auto& ray:late_rays) {CHECK(same(ray.origin,{100,80,80}));CHECK(ray.direction.x<0);}
        if(received==.98) {
            REQUIRE(effects.flash);REQUIRE(effects.light);REQUIRE(effects.shell);
            CHECK(effects.extra_shell_count==1U);CHECK(effects.shell->origin.x==Approx(68));
            CHECK(effects.shell->origin.y==Approx(86));CHECK(effects.shell->origin.z==Approx(68));
        } else {
            CHECK_FALSE(effects.flash);CHECK_FALSE(effects.light);CHECK_FALSE(effects.shell);
            CHECK(effects.extra_shell_count==0U);
        }
        const auto audio=delayed.host.drain_audio();REQUIRE(audio.count==1U);
        Sink delayed_sink;g::LocalAudio delayed_playback(delayed_sink);
        deliver_ready_local(delayed_playback,delayed_sink,resources,audio,1.0);
        REQUIRE(delayed_sink.starts.size()==1U);pcm_nonzero(delayed_sink);
        const auto again=delayed.host.local_visuals(changed_camera(1.0));
        CHECK_FALSE(again.world_impact);CHECK_FALSE(again.shell);CHECK(again.extra_impact_count==0U);
        CHECK(again.extra_shell_count==0U);CHECK(delayed.host.drain_audio().count==0U);
    }
}

TEST_CASE("E17 reload stage composition relies on one server sound and preserves clip per insertion",
    "[e17][shotgun][game-module][composition][special-reload][weapon-audio]") {
    ApprovedSamples resources;Module f{state(6,2)};resources.prepare(f.host.drain_audio());
    Sink sink;g::LocalAudio local(sink);g::ServerAudio server(sink);server.reset(1);
    server.present({},{{0,0,0},{0,-1,0},1,false},{},1);
    api::LocalWeaponSubmittedCommand reload{1,1,g::kReferenceGoldSrcButtonReload,.02};
    f.host.submit(reload,.02);REQUIRE(f.host.sample(.02).visual);CHECK(f.host.sample(.02).visual->sequence==5U);
    CHECK(f.host.drain_audio().count==0U);
    const std::array observations{state(6,2,2,1),state(6,2,3,2),state(7,1,4,1),state(7,1,5,2),state(8,0,6,1),state(8,0,7,0)};
    const std::array times{.04,.72,1.24,1.32,1.84,1.86};
    const std::array expected_sequences{5U,3U,3U,3U,3U,4U};
    unsigned sound_ordinal=0;
    for(std::size_t i=0;i<observations.size();++i) {
        const auto before=observations[i];f.host.observe(observations[i],times[i]);const auto sample=f.host.sample(times[i]);
        REQUIRE(sample.visual);CHECK(sample.visual->sequence==expected_sequences[i]);CHECK(observations[i]==before);
        const auto hud=f.host.hud(observations[i],times[i]);CHECK(hud.clip==observations[i].weapon_slots[0].clip);
        CHECK(hud.primary_reserve==observations[i].weapon_hud.reserve_ammo[2]);
        const auto cues=f.host.drain_audio();CHECK(cues.count==0U);local.update(cues,resources.loader.get(),times[i],true);
        if(i==1 || i==3 || i==5) {
            const auto slot=static_cast<std::uint16_t>(i==1 ? 11U : i==3 ? 12U : 13U);
            const auto token=i==1 ? "weapons/reload1.wav" : i==3 ? "weapons/reload3.wav" : "weapons/scock1.wav";
            api::LocalSoundReference reference;reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
            std::copy_n(token,std::char_traits<char>::length(token),reference.sample.begin());(void)resources.ready(reference);
            auto loaded=resources.loader->request(slot);const auto deadline=std::chrono::steady_clock::now()+2s;
            while(loaded.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
                std::this_thread::sleep_for(1ms);loaded=resources.loader->request(slot);
            }
            REQUIRE(loaded.status==g::SoundAssetStatus::ready);
            const auto now=g::SoundTime{}+std::chrono::duration_cast<g::SoundTime::duration>(std::chrono::duration<double>{times[i]});
            g::CommittedSound event;event.generation=1;event.record=100+(++sound_ordinal);
            event.ordinal=sound_ordinal;event.cursor=8;event.opcode=g::RuntimeControlOpcode::svc_sound;
            event.sound.channel=1;event.sound.entity_reference=1;event.sound.sound_reference=slot;event.received=now;
            server.consume(event,now);deliver_ready_server(server,sink,resources,now,sound_ordinal);
            CHECK(server.statistics().started==sound_ordinal);CHECK(sink.starts.size()==sound_ordinal);pcm_nonzero(sink);
            server.consume(event,now);server.update(resources.loader.get(),now);CHECK(sink.starts.size()==sound_ordinal);
        }
        const auto frame=f.host.local_visuals(changed_camera(times[i]));CHECK_FALSE(frame.shell);CHECK_FALSE(frame.world_impact);
        CHECK(frame.extra_shell_count==0U);CHECK(frame.extra_impact_count==0U);CHECK_FALSE(frame.flash);CHECK_FALSE(frame.light);
    }
    CHECK(sound_ordinal==3U);CHECK(f.host.drain_audio().count==0U);
    auto dead=state(8,0,8,2);dead.receiving_client->health=0;dead.lifecycle.state=c::LocalPlayerLifeState::dead;
    f.host.observe(dead,1.9);CHECK_FALSE(f.host.sample(1.9).identity);CHECK(f.host.drain_audio().count==0U);
    f.host.teardown();CHECK(f.host.drain_audio().count==0U);
}
