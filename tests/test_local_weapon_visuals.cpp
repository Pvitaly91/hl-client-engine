#include "collision_brush_test_fixture.hpp"
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/weapon_visuals.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/entity_render/studio_model_render_asset.hpp>
#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/assets/asset_source.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

namespace {
namespace api=hlclient::game_api;
namespace client=hlclient::client;
api::LocalWeaponModelMetadata model(unsigned weapon=2) {
    api::LocalWeaponModelMetadata m;
    m.generation=1;m.model_index=weapon==2?59:60;m.resource_revision=3;
    m.resource_name=weapon==2?"models/v_9mmhandgun.mdl":"models/v_crowbar.mdl";
    m.supported_bodies.fill(true);m.selectable_bodies.fill(true);
    m.sequences.resize(10,{30,46,false,{}});
    for(unsigned seq:{3U,4U}) {
        hlclient::assets::ModelSequenceEvent e;
        e.frame=0;e.event_number=5001;e.options={std::byte{'1'},std::byte{'1'}};
        m.sequences[seq].events.push_back(e);
    }
    return m;
}
client::RuntimeClientObservationState state(unsigned ordinal=1,unsigned weapon=2,int clip=8) {
    client::RuntimeClientObservationState s;
    s.generation=1;s.publication_revision=ordinal;
    s.lifecycle.life_epoch=1;s.lifecycle.state=client::LocalPlayerLifeState::alive;
    s.client_metadata.generation=1;
    s.client_metadata.freshness=client::RuntimeObservationFreshness::observed_in_record;
    s.client_metadata.source=client::RuntimeObservationSource{.record_identity=ordinal,.record_ordinal=ordinal};
    s.receiving_client.emplace();s.receiving_client->viewmodel_index=weapon==2?59:60;
    s.receiving_client->health=100;
    s.weapon_hud.active_weapon_id=static_cast<std::uint8_t>(weapon);
    s.weapon_hud.catalogue.push_back({.id=static_cast<std::uint8_t>(weapon),
        .command_name=weapon==2?"weapon_9mmhandgun":"weapon_crowbar",.primary_ammo_type=1});
    s.weapon_hud.reserve_ammo[1]=68;s.weapon_hud.clips[weapon]=static_cast<std::int16_t>(clip);
    s.weapon_slots.push_back({.wire_slot=static_cast<std::uint8_t>(weapon),.clip=clip,
        .in_reload=false,.next_primary_attack=0.0,.weapon_id=static_cast<std::uint8_t>(weapon)});
    return s;
}
api::LocalVisualContext view(double t) {
    return {{100,200,64},{1,0,0},{0,-1,0},{0,0,1},{10,0,0},t};
}
struct Fixture {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    Fixture(unsigned weapon=2,int clip=8) {
        host.reset({1,1,{1}});host.bind_model(model(weapon));host.observe(state(1,weapon,clip),0);
    }
    api::LocalVisualFrame sample(double t) {(void)host.sample(t);return host.local_visuals(view(t));}
    void command(unsigned sequence,unsigned buttons,double t) {
        host.submit({1,sequence,static_cast<std::uint16_t>(buttons),t},t);
    }
};
std::vector<std::byte> read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);REQUIRE(in);
    std::vector<char> chars{std::istreambuf_iterator<char>{in},{}};
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(),chars.end(),bytes.begin(),[](char c){return static_cast<std::byte>(c);});
    return bytes;
}
}

TEST_CASE("E4 accepted B1 Glock fire yields one Studio-marker flash and world shell",
          "[weapon-visuals][game-module]") {
    Fixture f;
    f.command(1,0,0.02);CHECK_FALSE(f.sample(0.02).flash); // capture click
    f.command(2,1,0.04);
    auto first=f.sample(0.04);
    REQUIRE(first.flash);REQUIRE(first.light);REQUIRE(first.shell);
    CHECK(first.light->action==first.flash->action);
    CHECK(first.light->radius_units==Catch::Approx(96.0F));
    const auto fading=f.host.local_visuals(view(0.075));
    REQUIRE(fading.light);
    CHECK(fading.light->intensity<first.light->intensity);
    CHECK_FALSE(fading.shell);
    CHECK(first.flash->variant==11U);CHECK(first.flash->attachment_index==0U);
    CHECK(first.flash->marker_ordinal==0U);
    CHECK(first.shell->origin.x==Catch::Approx(120.0F));
    CHECK(first.shell->origin.y==Catch::Approx(196.0F));
    CHECK(first.shell->origin.z==Catch::Approx(52.0F));
    CHECK(first.shell->velocity.x==Catch::Approx(35.0F));
    CHECK(first.shell->velocity.y<=-50.0F);
    CHECK(first.shell->velocity.z>=100.0F);
    CHECK(f.host.local_visuals(view(0.04)).shell==std::nullopt);
    for(unsigned i=3;i<12;++i) {f.command(i,1,i*0.02);CHECK_FALSE(f.sample(i*0.02).shell);}
    f.host.submit({1,2,1,0.04},0.25);
    f.host.observe(state(2,2,7),0.26);
    CHECK_FALSE(f.sample(0.26).shell);
    f.command(13,1,0.36);
    auto second=f.sample(0.36);REQUIRE(second.shell);
    CHECK(second.shell->action.command_sequence!=first.shell->action.command_sequence);
    CHECK(second.statistics.fire_actions_received==2U);
    CHECK(second.statistics.light_requested==2U);
    Fixture empty(2,0);empty.command(1,1,0.02);
    const auto empty_fire=empty.sample(0.02);
    CHECK_FALSE(empty_fire.shell);CHECK_FALSE(empty_fire.light);
    Fixture crowbar(1);crowbar.command(1,1,0.02);
    const auto crowbar_swing=crowbar.sample(0.02);
    CHECK_FALSE(crowbar_swing.flash);CHECK_FALSE(crowbar_swing.light);
}

TEST_CASE("E5 accepted Glock action emits one immutable command-space world request",
          "[world-impacts][game-module]") {
    Fixture f;
    api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    f.host.submit(command,0.04);
    auto first=f.sample(0.04);
    REQUIRE(first.world_impact);
    CHECK(first.world_impact->origin.x==5);
    CHECK(first.world_impact->direction.x==-1);
    auto turned=view(0.05);
    turned.eye={100,200,64};turned.forward={0,1,0};
    CHECK_FALSE(f.host.local_visuals(turned).world_impact);
    f.host.accepted_world_impact(first.world_impact->action,{0,8,8},0.05);
    auto audio=f.host.drain_audio();
    const auto impact=std::find_if(audio.cues.begin(),audio.cues.begin()+audio.count,
        [](const auto& cue){return cue.kind==api::LocalSoundKind::impact;});
    REQUIRE(impact!=audio.cues.begin()+audio.count);
    CHECK(impact->reference.name()=="player/pl_step1.wav");
    REQUIRE(impact->world_origin);
    CHECK(impact->world_origin->x==0);
    f.host.accepted_world_impact(first.world_impact->action,{0,8,8},0.06);
    CHECK(f.host.drain_audio().count==0);
    f.host.submit(command,0.2); // exact duplicate/confirmation cannot re-arm
    CHECK_FALSE(f.sample(0.2).world_impact);
    Fixture switched;
    switched.host.submit(command,0.04);
    const auto switching=switched.sample(0.04);
    REQUIRE(switching.world_impact);
    switched.host.bind_model(model(1));
    switched.host.accepted_world_impact(switching.world_impact->action,{0,8,8},0.05);
    const auto switched_audio=switched.host.drain_audio();
    CHECK(std::none_of(switched_audio.cues.begin(),switched_audio.cues.begin()+switched_audio.count,
        [](const auto& cue){return cue.kind==api::LocalSoundKind::impact;}));
    Fixture reset;
    reset.host.submit(command,0.04);
    const auto resetting=reset.sample(0.04);
    REQUIRE(resetting.world_impact);
    reset.host.reset({2,2,{2}});
    reset.host.accepted_world_impact(resetting.world_impact->action,{0,8,8},0.05);
    const auto reset_audio=reset.host.drain_audio();
    CHECK(std::none_of(reset_audio.cues.begin(),reset_audio.cues.begin()+reset_audio.count,
        [](const auto& cue){return cue.kind==api::LocalSoundKind::impact;}));
    Fixture cancelled;
    cancelled.host.submit(command,0.04);
    const auto cancelling=cancelled.sample(0.04);
    REQUIRE(cancelling.world_impact);
    cancelled.host.cancel_uncommitted();
    cancelled.host.accepted_world_impact(cancelling.world_impact->action,{0,8,8},0.05);
    const auto cancelled_audio=cancelled.host.drain_audio();
    CHECK(std::none_of(cancelled_audio.cues.begin(),cancelled_audio.cues.begin()+cancelled_audio.count,
        [](const auto& cue){return cue.kind==api::LocalSoundKind::impact;}));
    Fixture missing;
    missing.command(1,1,0.04);
    CHECK_FALSE(missing.sample(0.04).world_impact);
    Fixture empty(2,0);
    empty.host.submit(command,0.04);
    CHECK_FALSE(empty.sample(0.04).world_impact);
    Fixture crowbar(1);
    crowbar.host.submit(command,0.04);
    const auto crowbar_request=crowbar.sample(0.04).world_impact;
    REQUIRE(crowbar_request);
    CHECK(crowbar_request->action.kind==api::LocalWeaponAction::melee_swing);
    CHECK(crowbar_request->maximum_distance_units==32.0F);
    CHECK(crowbar_request->decal_delay_seconds==Catch::Approx(0.2));
}

TEST_CASE("E6 crowbar hit refines one accepted swing pose and keeps independent voices",
          "[crowbar-impacts][game-module]") {
    Fixture f(1);
    f.host.submit({1,1,0,0.02},0.02); // first click captures, no attack action
    CHECK_FALSE(f.host.sample(0.02).identity);
    CHECK_FALSE(f.host.local_visuals(view(0.02)).world_impact);
    CHECK(f.host.drain_audio().count==0U);
    api::LocalWeaponSubmittedCommand command{1,2,1,0.04};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    f.host.submit(command,0.04);
    const auto miss_pose=f.host.sample(0.04);
    REQUIRE(miss_pose.visual);
    CHECK(miss_pose.visual->sequence==4U);
    const auto visual=f.host.local_visuals(view(0.04));
    REQUIRE(visual.world_impact);
    CHECK(visual.world_impact->origin.x==5.0F);
    CHECK(visual.world_impact->direction.x==-1.0F);
    CHECK_FALSE(visual.flash); CHECK_FALSE(visual.light); CHECK_FALSE(visual.shell);
    CHECK_FALSE(f.host.local_visuals(view(0.05)).world_impact);
    auto turned=view(0.05); turned.forward={1,0,0}; turned.eye={100,100,100};
    CHECK_FALSE(f.host.local_visuals(turned).world_impact); // frozen command context
    REQUIRE(f.host.resolved_world_impact(visual.world_impact->action,
        api::LocalWorldImpactOutcome::static_world_hit,{{0,8,8}},0.05));
    const auto hit_pose=f.host.sample(0.05);
    REQUIRE(hit_pose.visual);
    CHECK(hit_pose.visual->sequence==3U);
    CHECK(hit_pose.visual->restart_identity!=miss_pose.visual->restart_identity);
    CHECK(f.host.sample(0.06).visual->restart_identity==hit_pose.visual->restart_identity);
    CHECK_FALSE(f.host.resolved_world_impact(visual.world_impact->action,
        api::LocalWorldImpactOutcome::static_world_hit,{{0,8,8}},0.06));
    const auto audio=f.host.drain_audio();
    CHECK(audio.count==3U);
    CHECK(std::count_if(audio.cues.begin(),audio.cues.begin()+audio.count,
        [](const auto& cue){return cue.kind==api::LocalSoundKind::swing;})==1);
    CHECK(std::count_if(audio.cues.begin(),audio.cues.begin()+audio.count,
        [](const auto& cue){return cue.kind==api::LocalSoundKind::impact;})==2);
    CHECK(audio.cues[0].reference.name()=="weapons/cbar_miss1.wav");
    CHECK((audio.cues[1].reference.name()=="weapons/cbar_hit1.wav" ||
        audio.cues[1].reference.name()=="weapons/cbar_hit2.wav"));
    CHECK(audio.cues[2].reference.name()=="player/pl_step2.wav"); // E7 deterministic second action variant
    for (std::size_t i=1;i<3;++i) {
        REQUIRE(audio.cues[i].world_origin);
        CHECK(audio.cues[i].world_origin->x==0.0F);
        CHECK(audio.cues[i].attenuation==Catch::Approx(0.8F));
        CHECK(audio.cues[i].channel==api::LocalSoundChannel::automatic);
    }
    f.host.reset({2,2,{1}});
    CHECK_FALSE(f.host.resolved_world_impact(visual.world_impact->action,
        api::LocalWorldImpactOutcome::static_world_hit,{{0,8,8}},0.07));
}

TEST_CASE("E6 miss and invalid world results retain one crowbar swing only",
          "[crowbar-impacts][game-module]") {
    for (const auto outcome:{api::LocalWorldImpactOutcome::miss,
             api::LocalWorldImpactOutcome::start_solid,
             api::LocalWorldImpactOutcome::unsupported_blocker,
             api::LocalWorldImpactOutcome::unavailable,
             api::LocalWorldImpactOutcome::invalid}) {
        Fixture f(1);
        api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
        command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
        f.host.submit(command,0.04);
        const auto before=f.host.sample(0.04);
        const auto request=f.host.local_visuals(view(0.04)).world_impact;
        REQUIRE(request);
        CHECK_FALSE(f.host.resolved_world_impact(request->action,outcome,{},0.05));
        const auto after=f.host.sample(0.05);
        REQUIRE(after.visual); REQUIRE(before.visual);
        CHECK(after.visual->sequence==before.visual->sequence);
        CHECK(after.visual->restart_identity==before.visual->restart_identity);
        const auto audio=f.host.drain_audio();
        CHECK(audio.count==1U);
        CHECK(audio.cues[0].kind==api::LocalSoundKind::swing);
    }
}

TEST_CASE("E5 separate accepted actions retain separate frozen shot contexts",
          "[world-impacts][game-module]") {
    hlclient::games::halflife::WeaponVisuals visuals;
    (void)visuals.bind(model());
    for(const auto [sequence,time,eye,direction]:{
        std::tuple<unsigned,double,hlclient::assets::AssetVector3,hlclient::assets::AssetVector3>{
            10U,0.1,{5,8,8},{-1,0,0}},
        {11U,0.5,{6,7,8},{0,-1,0}}}) {
        api::LocalWeaponSubmittedCommand command{1,sequence,1,time};
        command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{eye,direction};
        visuals.remember_submission(command);
        api::LocalWeaponPresentationSnapshot accepted;
        accepted.identity=api::LocalWeaponActionIdentity{1,2,59,3,sequence,
            api::LocalWeaponAction::primary_fire,time,1};
        accepted.action=api::LocalWeaponAction::primary_fire;
        accepted.visual=api::LocalWeaponVisual{3U,0U,sequence,time};
        accepted.status=api::LocalWeaponActionStatus::predicted_pending;
        visuals.observe(accepted,time);
        auto changed=view(time);changed.eye={100,200,64};changed.forward={1,0,0};
        const auto frame=visuals.frame(changed);
        REQUIRE(frame.world_impact);
        CHECK(frame.world_impact->action.command_sequence==sequence);
        CHECK(frame.world_impact->origin.x==eye.x);
        CHECK(frame.world_impact->direction.y==direction.y);
        CHECK_FALSE(visuals.frame(changed).world_impact);
    }
}

TEST_CASE("E4.1 transient light expires and session reset drops it",
          "[weapon-visuals][game-module]") {
    hlclient::games::halflife::WeaponVisuals visuals;
    (void)visuals.bind(model());
    api::LocalWeaponPresentationSnapshot shot;
    shot.identity=api::LocalWeaponActionIdentity{1,2,59,3,31,
        api::LocalWeaponAction::primary_fire,0.1,1};
    shot.action=api::LocalWeaponAction::primary_fire;
    shot.visual=api::LocalWeaponVisual{3U,0U,31U,0.1};
    shot.status=api::LocalWeaponActionStatus::predicted_pending;
    visuals.observe(shot,0.1);
    REQUIRE(visuals.frame(view(0.1)).light);
    CHECK_FALSE(visuals.frame(view(0.176)).light);
    CHECK(visuals.frame(view(0.176)).statistics.light_expired==1U);
    visuals.observe(shot,0.18);
    CHECK_FALSE(visuals.frame(view(0.18)).light); // exact replay cannot revive it
    visuals.reset();
    CHECK_FALSE(visuals.frame(view(0.2)).light);
    CHECK(visuals.frame(view(0.2)).statistics.light_requested==0U);
}

TEST_CASE("E4 shell is world-space fixed-step visual with bounded lifetime and pool",
          "[weapon-visuals][transient]") {
    api::LocalShellEjection cue;
    cue.action={1,2,59,3,1,api::LocalWeaponAction::primary_fire,0.0,1};
    cue.origin={0,0,100};cue.velocity={60,0,0};cue.expires_at_seconds=2.5;
    hlclient::renderer::TransientVisuals direct,irregular;
    REQUIRE(direct.spawn(cue,0));REQUIRE(irregular.spawn(cue,0));
    CHECK_FALSE(direct.spawn(cue,0));CHECK(direct.statistics().duplicate_spawns==1U);
    direct.update(0.2);
    for(double t:{0.02,0.067,0.103,0.151,0.2}) irregular.update(t);
    REQUIRE(direct.shells().size()==1U);REQUIRE(irregular.shells().size()==1U);
    CHECK(direct.shells()[0].position.x==Catch::Approx(12.0F));
    CHECK(direct.shells()[0].position.z==Catch::Approx(irregular.shells()[0].position.z).margin(0.001));
    CHECK(direct.shells()[0].position.x==Catch::Approx(irregular.shells()[0].position.x).margin(0.001));
    for(unsigned i=2;i<=32;++i) {
        cue.action.command_sequence=i;REQUIRE(direct.spawn(cue,0.2));
    }
    cue.action.command_sequence=33;CHECK_FALSE(direct.spawn(cue,0.2));
    CHECK(direct.statistics().capacity==32U);CHECK(direct.statistics().shells_dropped==1U);
    direct.update(2.6);CHECK(direct.shells().empty());
    CHECK(direct.statistics().active==0U);
}

TEST_CASE("E4 bounded action journal suppresses nonconsecutive replay without suppressing a new fire",
          "[weapon-visuals][game-module]") {
    hlclient::games::halflife::WeaponVisuals visuals;
    (void)visuals.bind(model());
    const auto action=[](unsigned sequence,double started) {
        api::LocalWeaponPresentationSnapshot snapshot;
        snapshot.identity=api::LocalWeaponActionIdentity{1,2,59,3,sequence,
            api::LocalWeaponAction::primary_fire,started,1};
        snapshot.action=api::LocalWeaponAction::primary_fire;
        snapshot.visual=api::LocalWeaponVisual{3U,0U,sequence,started};
        snapshot.status=api::LocalWeaponActionStatus::predicted_pending;
        return snapshot;
    };
    const auto first=action(10,0.1);
    const auto second=action(11,0.4);
    visuals.observe(first,0.1);
    REQUIRE(visuals.frame(view(0.1)).shell);
    visuals.observe(second,0.4);
    REQUIRE(visuals.frame(view(0.4)).shell);
    visuals.observe(first,0.41); // older identity revisited after newer action
    const auto replay=visuals.frame(view(0.41));
    CHECK_FALSE(replay.shell);
    CHECK(replay.statistics.fire_actions_received==2U);
    CHECK(replay.statistics.exact_duplicates_suppressed==1U);
}

TEST_CASE("E4 shell origin and one-time velocity remain world-space across camera motion",
          "[weapon-visuals][coordinates]") {
    hlclient::games::halflife::WeaponVisuals visuals;
    (void)visuals.bind(model());
    api::LocalWeaponPresentationSnapshot shot;
    shot.identity=api::LocalWeaponActionIdentity{1,2,59,3,21,
        api::LocalWeaponAction::primary_fire,0.1,1};
    shot.action=api::LocalWeaponAction::primary_fire;
    shot.visual=api::LocalWeaponVisual{3U,0U,21U,0.1};
    shot.status=api::LocalWeaponActionStatus::predicted_pending;
    visuals.observe(shot,0.1);
    auto crouched=view(0.1);
    crouched.eye={100,200,28}; // lower eye; policy never guesses stance
    crouched.velocity={40,15,0};
    crouched.forward={0,0.173648F,0.984808F}; // +80 pitch, +90 yaw
    crouched.right={1,0,0};
    crouched.up={0,-0.984808F,0.173648F};
    const auto visual=visuals.frame(crouched);
    REQUIRE(visual.shell);
    CHECK(visual.shell->origin.x==Catch::Approx(104.0F));
    CHECK(visual.shell->origin.y==Catch::Approx(200+20*0.173648+12*0.984808).epsilon(0.001));
    CHECK(visual.shell->origin.z==Catch::Approx(28+20*0.984808-12*0.173648).epsilon(0.001));
    CHECK(visual.shell->velocity.x>=90.0F); // inherited 40 plus rightward [50,70]
    hlclient::renderer::TransientVisuals shells;
    REQUIRE(shells.spawn(*visual.shell,0.1));
    shells.update(0.2);
    REQUIRE(shells.shells().size()==1U);
    const auto before=shells.shells()[0].position;
    crouched.eye={-900,400,60};
    crouched.forward={-1,0,0};
    CHECK_FALSE(visuals.frame(crouched).shell);
    CHECK(shells.shells()[0].position.x==before.x);
    CHECK(shells.shells()[0].position.y==before.y);
    CHECK(shells.shells()[0].position.z==before.z);
}

TEST_CASE("E4 decorative point shell sweeps the independently authored BSP wall",
          "[weapon-visuals][transient][collision]") {
    api::LocalShellEjection cue;
    cue.action={1,2,59,3,1,api::LocalWeaponAction::primary_fire,0.0,1};
    cue.origin={5,0,64};cue.velocity={-100,0,0};cue.expires_at_seconds=2.5;
    hlclient::renderer::TransientVisuals runtime;
    runtime.set_collision_world(hlclient::tests::collision_brush_fixture::package(true,0.0,0U));
    REQUIRE(runtime.spawn(cue,0));
    runtime.update(0.1);
    REQUIRE(runtime.shells().size()==1U);
    CHECK(runtime.statistics().collision_contacts>=1U);
    CHECK(runtime.shells()[0].position.x>=-0.2F);
    CHECK(runtime.shells()[0].velocity.x>=0.0F);
}

TEST_CASE("E5.1 actual point-sweep contact reaches HL shell audio once, even after reload",
          "[weapon-visuals][transient][shell-audio]") {
    Fixture f;
    api::LocalShellEjection shell;
    shell.action={1,2,59,3,17,api::LocalWeaponAction::primary_fire,0.0,1};
    shell.origin={5,0,64};shell.velocity={-100,0,0};shell.expires_at_seconds=2.5;
    hlclient::renderer::TransientVisuals runtime;
    runtime.set_collision_world(hlclient::tests::collision_brush_fixture::package(true,0.0,0U));
    REQUIRE(runtime.spawn(shell,0));
    CHECK(runtime.contacts().empty()); // spawn is silent
    f.host.cancel_uncommitted(); // a later weapon action does not cancel an extant casing
    runtime.update(0.1); // no render/materialization or frustum test
    REQUIRE(runtime.contacts().size()==1U);
    const auto first=runtime.contacts()[0];
    CHECK(first.ordinal==1U);
    CHECK(first.inward_normal_speed>=100.0F);
    CHECK(first.point.x==Catch::Approx(0).margin(0.2));
    f.host.shell_contact(first);
    f.host.shell_contact(first); // repeated delivery is not a second voice
    auto batch=f.host.drain_audio();
    REQUIRE(batch.count==1U);
    CHECK(batch.cues[0].kind==api::LocalSoundKind::shell_contact);
    CHECK(batch.cues[0].reference.name()=="player/pl_shell1.wav");
    REQUIRE(batch.cues[0].world_origin);
    CHECK(batch.cues[0].world_origin->x==Catch::Approx(first.point.x));
    runtime.update(0.1);
    CHECK(runtime.contacts().empty());
    CHECK(f.host.drain_audio().count==0U);
    auto weak=first;weak.ordinal=2;weak.inward_normal_speed=10.0F;
    f.host.shell_contact(weak);
    CHECK(f.host.drain_audio().count==0U);
    runtime.update(2.6);
    CHECK(runtime.contacts().empty()); // expiry is silent
    shell.action.command_sequence=18;shell.starts_at_seconds=2.6;
    shell.expires_at_seconds=5.1;
    REQUIRE(runtime.spawn(shell,2.6)); // reused pool slot has fresh contact identity
    runtime.update(2.7);
    REQUIRE(runtime.contacts().size()==1U);
    f.host.shell_contact(runtime.contacts()[0]);
    CHECK(f.host.drain_audio().count==1U);
    f.host.reset({2,2,{1}});
    f.host.shell_contact(first); // stale generation
    CHECK(f.host.drain_audio().count==0U);
}

TEST_CASE("E4 opt-in installed Glock marker attachment and shell model are inspectable read-only",
          "[weapon-visuals][local-assets]") {
    const auto* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT");
    if(!root) SKIP("opt-in read-only Valve assets unavailable");
    const auto base=std::filesystem::path(root)/"valve";
    auto view_source=hlclient::assets::AssetSource::create("models/v_9mmhandgun.mdl",
        read(base/"models"/"v_9mmhandgun.mdl"));
    REQUIRE(view_source);
    auto view_model=hlclient::goldsrc::studio::GoldSrcStudioModelImporter{}.import(*view_source.source);
    REQUIRE(view_model);REQUIRE(view_model.value().skeletal_data);
    const auto& data=*view_model.value().skeletal_data;
    REQUIRE(data.attachments.size()>=1U);
    for(unsigned sequence:{3U,4U}) {
        REQUIRE(data.sequences.size()>sequence);
        const auto& events=data.sequences[sequence].events;
        const auto found=std::find_if(events.begin(),events.end(),[](const auto& e){
            return e.event_number==5001 && e.frame==0;
        });
        REQUIRE(found!=events.end());
        CHECK(found->options==std::vector<std::byte>{std::byte{'1'},std::byte{'1'}});
    }
    auto shell_source=hlclient::assets::AssetSource::create("models/shell.mdl",
        read(base/"models"/"shell.mdl"));
    REQUIRE(shell_source);
    auto shell=hlclient::goldsrc::studio::GoldSrcStudioModelImporter{}.import(*shell_source.source);
    REQUIRE(shell);REQUIRE(shell.value().skeletal_data);
    CHECK_FALSE(shell.value().skeletal_data->submodels.empty());
    INFO("shell bones=" << shell.value().skeletal_data->bones.size()
         << " sequences=" << shell.value().skeletal_data->sequences.size());
    REQUIRE_FALSE(shell.value().skeletal_data->sequences.empty());
    hlclient::goldsrc::studio::StudioPoseInput shell_pose_input;
    shell_pose_input.compatibility_profile=hlclient::goldsrc::studio::
        StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
    const auto shell_pose=hlclient::goldsrc::studio::StudioPoseEvaluator{}.evaluate(
        {"shell",{0x123U,0x456U}},*shell.value().skeletal_data,shell_pose_input);
    REQUIRE(shell_pose);
    const auto shell_render=hlclient::entity_render::StudioModelRenderAssetBuilder{}.build(
        shell.value(),{1U,1U});
    REQUIRE(shell_render);
    CHECK(hlclient::entity_render::studio_entity_material_support(
        *shell_render.asset,0U,0U).has_value());
    auto sprite_source=hlclient::assets::AssetSource::create("sprites/muzzleflash1.spr",
        read(base/"sprites"/"muzzleflash1.spr"));
    REQUIRE(sprite_source);
    auto sprite=hlclient::goldsrc::sprite::GoldSrcSpriteImporter{}.import(*sprite_source.source);
    REQUIRE(sprite);
    CHECK(sprite.value().source_data->texture_format==hlclient::assets::SpriteTextureFormat::additive);
}
