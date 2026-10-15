#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "weapon_suite_test_fixture.hpp"
#include "snark_world_test_fixture.hpp"
#include "tripmine_world_fixture.hpp"
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
namespace f=hlclient::tests::weapon_suite_fixture;
namespace api=hlclient::game_api;
namespace c=hlclient::client;
using Catch::Approx;
bool fire_cue(const api::LocalAudioBatch& batch) {
    return std::any_of(batch.cues.begin(),batch.cues.begin()+batch.count,[](const auto& cue) {
        return cue.kind==api::LocalSoundKind::fire || cue.kind==api::LocalSoundKind::secondary_launch;
    });
}
bool stop_control(const api::LocalAudioBatch& batch,std::uint64_t voice) {
    return std::any_of(batch.controls.begin(),batch.controls.begin()+batch.control_count,[=](const auto& control) {
        return control.identity==voice && control.operation==api::LocalVoiceControl::Operation::stop;
    });
}
void commit_messages(f::Module& module,const std::vector<f::Message>& messages,
    std::uint64_t record,double at) {
    c::RuntimeClientObservationState candidate;
    auto staged=f::stage_messages(module,messages,record,candidate);
    REQUIRE(staged);
    candidate.weapon_hud=staged.state->weapon_hud;candidate.lifecycle=staged.state->lifecycle;
    candidate.life_events=staged.state->life_events;
    module.host.commit_record(std::move(*staged.state));
    module.observed=std::move(candidate);module.observe(at);
}
// Approved, independently authored p-model resources exercise the production
// remote attachment materializer without an installation, GPU or copied mesh.
struct RemoteModels {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    std::unique_ptr<hlclient::app::RuntimeReplayLocalAssets> assets;
    RemoteModels() {
        namespace t=hlclient::tests;
        (void)t::hand_grenade_world_fixture::owned(root);
        // A real usable air leaf/PVS and legal observer volume, rather than
        // relying on the minimal fixture's solid-camera fallback.
        t::SyntheticBspBuilder bsp;
        const std::string entities="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
        const auto entity_bytes=std::as_bytes(std::span{entities});
        bsp.lump(t::SyntheticBspLumpId::entities).assign(entity_bytes.begin(),entity_bytes.end());
        t::SyntheticBspNode node;node.children={-2,-1};
        node.minimum={-256,-256,-64};node.maximum={512,256,256};
        const std::array nodes{node};bsp.set_nodes(nodes);
        t::SyntheticBspLeaf solid;solid.contents=-2;solid.marksurface_count=0;
        solid.minimum={-256,-256,-64};solid.maximum={512,256,0};
        t::SyntheticBspLeaf air;air.contents=-1;air.visibility_offset=0;
        air.minimum={-256,-256,0};air.maximum={512,256,256};
        const std::array leaves{solid,air};bsp.set_leaves(leaves);
        bsp.lump(t::SyntheticBspLumpId::visibility)={std::byte{1}};
        t::SyntheticBspModel world_model;
        world_model.minimum={-256,-256,-64};world_model.maximum={512,256,256};
        const std::array world_models{world_model};bsp.set_models(world_models);
        root.write("valve","maps/test.bsp",bsp.build());
        root.write("valve","models/player.mdl",t::python_fixture::remote_model(false));
        std::vector<resource_list_test_fixture::EntrySpec> entries{
            {2,"maps/test.bsp",9,0,0},{2,"models/player.mdl",7,0,0}};
        for(const auto& weapon:f::weapons) {
            auto bytes=t::python_fixture::remote_model(true);
            // Distinct independent header names distinguish source fingerprints
            // even though these fixtures deliberately reuse one simple geometry.
            t::studio_write_fixed_string(bytes,8,64,weapon.command);
            root.write("valve",weapon.pmodel,bytes);
            entries.push_back({2,std::string(weapon.pmodel),
                static_cast<std::uint16_t>(f::remote_model_slot(weapon.id)),0,0});
        }
        auto made=hlclient::app::RuntimeReplayLocalAssets::create(
            t::readiness_fixture::parse_resource_list(entries),
            t::readiness_fixture::parse_server_info("maps/test.bsp"),root.path(),"valve",
            hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);
        REQUIRE(made.projection);assets=std::move(made.projection);
    }
    hlclient::entity_render::EntityRenderResourceIdentity check(f::Module& module,
        const f::Weapon& weapon,c::ClientWorldState& world,bool initial) {
        namespace t=hlclient::tests;
        auto state=module.observed;auto remote=t::snark_world_fixture::player();
        const auto slot=f::remote_model_slot(weapon.id);
        remote.weapon_model_index=slot;remote.origin={180.,80.,52.};
        world.set_camera({{100,80,80},{180,80,52},{0,0,1},1.04719755F,.1F,4096});
        state.packet_entities={remote};
        t::hand_grenade_world_fixture::stamp(state,initial ? 1U : 2U,initial ? 1. : 2.);
        if(initial)REQUIRE(assets->reset_generation(state,world));
        else t::hand_grenade_world_fixture::project(*assets,state,world);
        const auto anchor=initial ? 0. : 1.;
        REQUIRE(assets->present_entities(module.host,anchor,1,world,{320,240}));
        REQUIRE(assets->present_entities(module.host,anchor+.21,1,world,{320,240}));
        const auto frame=world.entity_frame();REQUIRE(frame);
        const auto child=std::find_if(frame->studio_instances().begin(),frame->studio_instances().end(),
            [](const auto& instance){return instance.entity_number==2 && instance.subinstance_ordinal==1;});
        REQUIRE(child!=frame->studio_instances().end());
        CHECK(child->visibility_status==hlclient::entity_render::RuntimeEntityVisibilityStatus::visible);
        const auto package=world.entity_scene();REQUIRE(package);REQUIRE(package->asset_library());
        const auto records=package->asset_library()->records();
        const auto expected=std::find_if(records.begin(),records.end(),[=](const auto& record) {
            return record.manifest_model_slot()==slot;
        });
        REQUIRE(expected!=records.end());
        REQUIRE(child->studio_asset_index<package->studio_assets().size());
        const auto& child_asset=package->studio_assets()[child->studio_asset_index];REQUIRE(child_asset);
        CHECK(child_asset->source_identity().resource_id==expected->resource_id());
        REQUIRE(child->pose_index<frame->studio_poses().size());
        CHECK(frame->studio_poses()[child->pose_index].model_resource_identity==child_asset->source_identity());
        CHECK(assets->summary().rendered_model_slots.at(slot)==1);
        const auto& summary=assets->summary();REQUIRE(summary.remote_player_count==1);
        CHECK(summary.remote_players[0].intent.attachment_model_index==slot);
        // The frame builder promotes successful submission to its final
        // visibility result. Require visible, never the old culled outcome.
        CHECK(summary.remote_players[0].attachment_status==hlclient::app::ReplayAttachmentStatus::visible);
        CHECK(summary.remote_players[0].weapon_model_index==slot);
        return child_asset->source_identity();
    }
};
}

TEST_CASE("E28 no-clip Crowbar HUD does not publish signed unavailable ammo fields",
    "[e28][weapon-suite][hud][no-stock]") {
    namespace api=hlclient::game_api;
    namespace c=hlclient::client;
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1});
    api::HudPresentationContext diagnostic_context;
    diagnostic_context.diagnostic_overlay=true; // Existing text/no-clip assertions opt in without changing normal HUD defaults.
    host.set_hud_context(diagnostic_context);
    c::RuntimeClientObservationState state;
    state.generation=1;state.publication_revision=1;
    state.receiving_client.emplace();state.receiving_client->health=100;
    state.receiving_client->owned_weapon_bits=1U<<1;
    state.weapon_hud.active_weapon_id=1;state.weapon_hud.clips[1]=-1;
    state.weapon_hud.catalogue.push_back({.id=1,.command_name="weapon_crowbar",.primary_ammo_type=-1});
    const auto hud=host.hud(state,0);
    CHECK_FALSE(hud.clip);
    CHECK_FALSE(hud.primary_reserve);
    CHECK(state.weapon_hud.clips[1]==-1);
    REQUIRE(hud.draw.texts.size()==1);
    CHECK(hud.draw.texts[0].text.find("CLIP")==std::string::npos);
    CHECK(hud.draw.texts[0].text.find("AMMO")==std::string::npos);
}

TEST_CASE("E28 all 182 directed authoritative idle weapon transitions preserve target presentation",
    "[e28][weapon-suite][transitions][game-api][no-stock]") {
    std::set<std::pair<unsigned,unsigned>> exercised;
    auto remote_models=std::make_unique<RemoteModels>();
    for(const auto& from:f::weapons)for(const auto& to:f::weapons) {
        if(from.id==to.id)continue;
        CAPTURE(from.id,from.command,to.id,to.command);
        f::Module module(from.id);
        const auto first=module.sample(0);
        const auto first_pose=module.host.viewmodel(module.observed,f::model(from.id),0,first.visual);
        REQUIRE(first_pose.status==api::ViewmodelStatus::ready);
        c::ClientWorldState remote_world;
        const auto original_attachment=remote_models->check(module,from,remote_world,true);
        const auto request=module.host.inventory_selection(module.observed,to.id);
        REQUIRE(request);CHECK(request->token==to.command);
        CHECK(module.observed.weapon_hud.active_weapon_id==from.id);
        (void)module.host.drain_audio();
        module.adopt(to.id,1,2);
        const auto selected=module.sample(1);
        CHECK_FALSE(selected.identity);CHECK_FALSE(selected.action);
        const auto metadata=f::model(to.id);
        const auto pose=module.host.viewmodel(module.observed,metadata,1,selected.visual);
        REQUIRE(pose.status==api::ViewmodelStatus::ready);
        CHECK(pose.model_index==f::model_slot(to.id));
        REQUIRE(pose.sequence<metadata.sequences.size());
        CHECK(metadata.selectable_bodies[pose.body]);
        CHECK(std::isfinite(pose.frame_coordinate));
        const auto hud=module.host.hud(module.observed,1);
        CHECK(hud.weapon_name==to.hud_name);
        if(to.clip<0)CHECK_FALSE(hud.clip);else CHECK(hud.clip==to.clip);
        if(to.ammo<0)CHECK_FALSE(hud.primary_reserve);
        else CHECK(hud.primary_reserve==*module.observed.weapon_hud.reserve_ammo[static_cast<std::size_t>(to.ammo)]);
        CHECK(api::valid_hud_draw_commands(hud.draw));
        CHECK(module.host.camera(module.observed,0).horizontal_field_of_view_degrees==90);
        const auto visual=module.visuals(1);
        CHECK_FALSE(visual.shell);CHECK_FALSE(visual.flash);CHECK_FALSE(visual.light);CHECK_FALSE(visual.world_impact);
        const auto audio=module.host.drain_audio();
        CHECK_FALSE(fire_cue(audio));CHECK(audio.control_count==0);
        const auto beam=module.host.beam_effects(module.world,1,{});
        CHECK(beam.count==0);CHECK(beam.active_emitters==0);
        CHECK(module.host.drain_remote_effects(1).count==0);
        CHECK(module.host.drain_temporary_effects(1).count==0);
        const auto selected_attachment=remote_models->check(module,to,remote_world,false);
        CHECK(selected_attachment.resource_id!=original_attachment.resource_id);
        exercised.emplace(from.id,to.id);
    }
    CHECK(exercised.size()==182);
}

TEST_CASE("E28 switching charged Gauss and running Egon stops only the owning persistent voice",
    "[e28][weapon-suite][stateful][persistent][no-stock]") {
    for(const auto id:{9U,10U}) {
        CAPTURE(id);f::Module module(static_cast<std::uint8_t>(id));
        module.submit(1,.02,id==9 ? 1U<<11 : 1U);
        auto before=module.host.drain_audio();REQUIRE(before.control_count==1);
        CHECK(before.controls[0].operation==api::LocalVoiceControl::Operation::start);
        const auto voice=before.controls[0].identity;REQUIRE(voice!=0);
        if(id==10) {
            f::slot(module.observed,10).auxiliary_integer3=1;
            f::select(module.observed,10,2);module.observe(.04);
            (void)module.sample(.04);(void)module.visuals(.04);
            CHECK(module.host.beam_effects(module.world,.04,{}).active_emitters==1);
        }
        module.adopt(2,.06,3);
        CHECK(stop_control(module.host.drain_audio(),voice));
        const auto beam=module.host.beam_effects(module.world,.06,{});
        CHECK(beam.active_emitters==0);CHECK(beam.count==0);
        const auto after=module.sample(.06);CHECK_FALSE(after.identity);
        CHECK(module.host.viewmodel(module.observed,f::model(2),.06,after.visual).status==api::ViewmodelStatus::ready);
        CHECK(module.host.drain_audio().control_count==0);
        CHECK(module.host.hud(module.observed,.06).primary_reserve==100);
    }
}

TEST_CASE("E28 staged Shotgun reload and two zoom profiles retire across authoritative weapon switches",
    "[e28][weapon-suite][stateful][reload][camera][no-stock]") {
    SECTION("Shotgun insertion is a shell stage and never a retained magazine reload") {
        f::Module module(7);f::slot(module.observed,7).clip=3;
        module.observed.weapon_hud.clips[7]=3;
        f::slot(module.observed,7).special_reload_state=2;
        f::select(module.observed,7,2);module.observe(.02);
        const auto inserting=module.sample(.02);REQUIRE(inserting.visual);
        CHECK(inserting.visual->sequence==3);
        module.adopt(4,.04,3);const auto mp5=module.sample(.04);
        REQUIRE(mp5.visual);CHECK(mp5.visual->sequence<=1);
        f::slot(module.observed,7).special_reload_state=0;module.adopt(7,.06,4);
        const auto returning=module.sample(.06);REQUIRE(returning.visual);
        CHECK(returning.visual->sequence==0);CHECK_FALSE(returning.identity);
        CHECK(module.observed.weapon_hud.clips[7]==3);
    }
    for(const auto zoom_weapon:{3U,6U}) {
        CAPTURE(zoom_weapon);f::Module module(static_cast<std::uint8_t>(zoom_weapon));
        f::select(module.observed,static_cast<std::uint8_t>(zoom_weapon),2);
        module.observed.receiving_client->field_of_view=20;module.observe(.02);
        CHECK(module.host.camera(module.observed,0).horizontal_field_of_view_degrees==20);
        module.adopt(8,.04,3);CHECK(module.host.camera(module.observed,0).horizontal_field_of_view_degrees==90);
        // A retained old SetFOV cannot revive the preceding zoom after holster.
        module.observed.weapon_hud.field_of_view=20;
        module.observed.weapon_hud.field_of_view_source=f::source(2);module.observe(.06);
        CHECK(module.host.camera(module.observed,0).horizontal_field_of_view_degrees==90);
    }
}

TEST_CASE("E28 server holster contracts preserve primed grenade guided RPG and empty Satchel radio",
    "[e28][weapon-suite][stateful][selection][no-stock]") {
    SECTION("grenade prime denies local selection until server correction") {
        f::Module module(12);module.submit(1,.02);
        const auto primed=module.sample(.02);REQUIRE(primed.visual);CHECK(primed.visual->sequence==2);
        CHECK_FALSE(module.host.inventory_selection(module.observed,2));
        CHECK_FALSE(module.host.cycle_inventory(module.observed,12,1));
        module.adopt(2,.04,2);CHECK(module.host.inventory_selection(module.observed,4));
        CHECK_FALSE(module.sample(.04).identity);
    }
    SECTION("guided active rocket blocks a requested holster but never a server correction") {
        f::Module module(8);module.observed.receiving_client->auxiliary_vector2={0.,1.,1.};
        module.observe(.01);CHECK_FALSE(module.host.inventory_selection(module.observed,2));
        CHECK(module.host.inventory_selection(module.observed,8));
        module.observed.receiving_client->auxiliary_vector2={0.,0.,1.};
        module.observe(.02);CHECK(module.host.inventory_selection(module.observed,2));
    }
    SECTION("zero pocket reserve is a usable radio state, not a grenade retirement") {
        f::Module module(14);module.observed.weapon_hud.reserve_ammo[10]=0;
        f::slot(module.observed,14).auxiliary_integer1=1;
        module.adopt(14,.02,2,true);CHECK(module.host.inventory_selection(module.observed,14));
        const auto idle=module.sample(.02);
        const auto pose=module.host.viewmodel(module.observed,f::model(14,true),.02,idle.visual);
        CHECK(pose.status==api::ViewmodelStatus::ready);CHECK(pose.model_index==f::model_slot(14,true));
        CHECK(module.host.hud(module.observed,.02).primary_reserve==0);
        CHECK_FALSE(module.host.hud(module.observed,.02).clip);
        module.submit(1,.04,1U<<11);const auto radio=module.sample(.04);
        REQUIRE(radio.identity);CHECK(radio.identity->kind==api::LocalWeaponAction::remote_detonate);
        CHECK(module.observed.weapon_hud.reserve_ammo[10]==0);
        CHECK(module.host.drain_temporary_effects(.04).count==0);
    }
}

TEST_CASE("E28 shared AmmoX pools pickup notifications and replay do not author a second reserve",
    "[e28][weapon-suite][ammo][pickups][transaction][no-stock]") {
    for(const auto pair:std::array{std::array<unsigned,2>{2,4},std::array<unsigned,2>{9,10}}) {
        CAPTURE(pair);f::Module module(static_cast<std::uint8_t>(pair[0]));
        const auto pool=static_cast<unsigned>(f::entry(static_cast<std::uint8_t>(pair[0])).ammo);
        CHECK(f::entry(static_cast<std::uint8_t>(pair[1])).ammo==static_cast<std::int8_t>(pool));
        commit_messages(module,{f::message("AmmoX",{pool,50}),f::message("AmmoPickup",{pool,20}),
            f::message("WeapPickup",{pair[0]})},2,.02);
        CHECK(module.host.hud(module.observed,.02).primary_reserve==50);
        const auto pending=module.host.hud(module.observed,.02);REQUIRE(pending.inventory_feedback.size()==2);
        CHECK(pending.inventory_notifications_received==2);
        module.adopt(static_cast<std::uint8_t>(pair[1]),.04,3);
        CHECK(module.host.hud(module.observed,.04).primary_reserve==50);
        const auto before=module.observed.weapon_hud;
        module.submit(1,.06);module.submit(1,.06);
        CHECK(module.observed.weapon_hud==before);
        CHECK(module.host.hud(module.observed,.06).primary_reserve==50);
        commit_messages(module,{f::message("AmmoX",{pool,49})},4,.08);
        module.adopt(static_cast<std::uint8_t>(pair[0]),.1,5);
        CHECK(module.host.hud(module.observed,.1).primary_reserve==49);
        // Invalid following data rejects the entire candidate, including an
        // otherwise valid preceding absolute counter and pickup notification.
        c::RuntimeClientObservationState candidate;
        const auto failed=f::stage_messages(module,{f::message("AmmoX",{pool,99}),
            f::message("AmmoPickup",{pool,20}),f::message("AmmoX",{64,1})},6,candidate);
        CHECK_FALSE(failed);CHECK(module.host.hud(module.observed,.12).primary_reserve==49);
        CHECK(module.host.hud(module.observed,.12).inventory_notifications_received==2);
        const auto maximum=pool==1 ? 250U : 100U;
        commit_messages(module,{f::message("AmmoPickup",{pool,20}),f::message("AmmoX",{pool,maximum}),
            f::message("WeapPickup",{pair[0]}),f::message("WeapPickup",{pair[0]})},7,.14);
        CHECK(module.host.hud(module.observed,.14).primary_reserve==maximum);
        CHECK(module.observed.weapon_hud.reserve_ammo[pool]==maximum);
        module.adopt(static_cast<std::uint8_t>(pair[1]),.16,8);
        CHECK(module.host.hud(module.observed,.16).primary_reserve==maximum);
    }
}

TEST_CASE("E28 limited-reserve reload and rejected shot keep server ammo until explicit transfer",
    "[e28][weapon-suite][ammo][reload][reconciliation][no-stock]") {
    f::Module module(2);
    module.observed.weapon_hud.clips[2]=0;module.observed.weapon_hud.reserve_ammo[1]=3;
    f::slot(module.observed,2).clip=0;f::select(module.observed,2,2);module.observe(.01);
    module.submit(1,.02,1);CHECK(module.sample(.02).primary_fire_starts==0);
    CHECK_FALSE(fire_cue(module.host.drain_audio()));
    module.submit(2,.04,1U<<13);CHECK(module.sample(.04).reload_starts==1);
    CHECK(module.observed.weapon_hud.clips[2]==0);CHECK(module.observed.weapon_hud.reserve_ammo[1]==3);
    f::slot(module.observed,2).in_reload=true;f::select(module.observed,2,3);module.observe(.06);
    CHECK(module.sample(.06).reload_confirmed==1);
    CHECK(module.host.hud(module.observed,.06).clip==0);
    // The fixture supplies the server's bounded transfer, not a production
    // client calculation. No frame/replay is allowed to perform it a second time.
    f::slot(module.observed,2).in_reload=false;f::slot(module.observed,2).clip=3;
    module.observed.weapon_hud.clips[2]=3;module.observed.weapon_hud.reserve_ammo[1]=0;
    f::select(module.observed,2,4);module.observe(2.1);
    CHECK(module.host.hud(module.observed,2.1).clip==3);
    CHECK(module.host.hud(module.observed,2.1).primary_reserve==0);
    module.submit(3,2.12);const auto pending=module.sample(2.12);REQUIRE(pending.identity);
    CHECK(pending.primary_fire_starts==1);CHECK(module.observed.weapon_hud.clips[2]==3);
    module.submit(3,2.12);CHECK(module.sample(2.12).primary_fire_starts==1);
    module.host.cancel_uncommitted();CHECK_FALSE(module.sample(2.14).identity);
    module.submit(4,2.16,0);module.submit(5,3.0,1);
    CHECK(module.sample(3.0).primary_fire_starts==2);
    CHECK(module.observed.weapon_hud.clips[2]==3);CHECK(module.observed.weapon_hud.reserve_ammo[1]==0);
}

TEST_CASE("E28 no-clip and exhaustible selection use canonical counts without synthetic reload or ammo",
    "[e28][weapon-suite][ammo][exhaustible][no-stock]") {
    SECTION("Hornet remains selectable at zero and R cannot fabricate a magazine") {
        f::Module module(11);module.observed.weapon_hud.reserve_ammo[7]=0;
        f::select(module.observed,11,2);module.observe(.02);
        CHECK(module.host.inventory_selection(module.observed,11));
        module.submit(1,.04,1U<<13);CHECK(module.sample(.04).reload_starts==0);
        CHECK(module.observed.weapon_hud.active_weapon_id==11);
        CHECK(module.host.hud(module.observed,.04).primary_reserve==0);
        CHECK_FALSE(module.host.hud(module.observed,.04).clip);
        commit_messages(module,{f::message("AmmoX",{7,1})},3,.06);
        CHECK(module.host.hud(module.observed,.06).primary_reserve==1);
    }
    for(const auto id:{12U,13U,15U}) {
        CAPTURE(id);f::Module module(static_cast<std::uint8_t>(id));
        const auto pool=static_cast<unsigned>(f::entry(static_cast<std::uint8_t>(id)).ammo);
        module.observed.weapon_hud.reserve_ammo[pool]=0;
        f::select(module.observed,static_cast<std::uint8_t>(id),2);module.observe(.02);
        CHECK_FALSE(module.host.inventory_selection(module.observed,static_cast<std::uint8_t>(id)));
        module.submit(1,.04);CHECK(module.sample(.04).actions_started==0);
        CHECK(module.observed.weapon_hud.reserve_ammo[pool]==0);
        CHECK_FALSE(module.host.hud(module.observed,.04).clip);
        commit_messages(module,{f::message("AmmoX",{pool,1})},3,.06);
        CHECK(module.host.inventory_selection(module.observed,static_cast<std::uint8_t>(id)));
    }
}

TEST_CASE("E28 profiles do not inherit another weapon's primary shell tracer beam or secondary action",
    "[e28][weapon-suite][cross-profile][no-stock]") {
    SECTION("Python is an impact without brass or MP5 tracer") {
        f::Module module(3);module.submit(1,.02);(void)module.sample(.02);
        const auto effects=module.visuals(.02);CHECK_FALSE(effects.shell);
        REQUIRE(effects.world_impact);CHECK_FALSE(effects.world_impact->tracer);
    }
    SECTION("M203 launches with no primary bullet package") {
        f::Module module(4);module.submit(1,.02,1U<<11);
        const auto p=module.sample(.02);REQUIRE(p.identity);
        CHECK(p.identity->kind==api::LocalWeaponAction::secondary_launch);
        const auto visual=module.visuals(.02);CHECK_FALSE(visual.shell);CHECK_FALSE(visual.world_impact);CHECK_FALSE(visual.flash);
        const auto audio=module.host.drain_audio();REQUIRE(audio.count==1);
        CHECK(audio.cues[0].kind==api::LocalSoundKind::secondary_launch);
    }
    SECTION("Hornet is a launch without bullet traces or brass") {
        f::Module module(11);module.submit(1,.02);CHECK(module.sample(.02).actions_started==1);
        const auto visual=module.visuals(.02);CHECK_FALSE(visual.shell);CHECK_FALSE(visual.world_impact);
        CHECK(module.host.beam_effects(module.world,.02,{}).count==0);
    }
    SECTION("Egon RMB has no invented second mode") {
        f::Module module(10);module.submit(1,.02,1U<<11);CHECK(module.sample(.02).actions_started==0);
        CHECK(module.host.beam_effects(module.world,.02,{}).count==0);CHECK_FALSE(fire_cue(module.host.drain_audio()));
    }
    for(const auto id:{12U,13U,14U,15U}) {
        CAPTURE(id);f::Module module(static_cast<std::uint8_t>(id));
        module.submit(1,.02);(void)module.sample(.02);
        const auto visual=module.visuals(.02);CHECK_FALSE(visual.shell);CHECK_FALSE(visual.world_impact);CHECK_FALSE(visual.light);
        CHECK(module.host.beam_effects(module.world,.02,{}).count==0);
        CHECK(module.host.drain_temporary_effects(.02).count==0);
        CHECK(module.host.drain_follow_events().count==0);
        // Server projectiles/mines/Snarks are never spawned by a local action.
        CHECK(module.observed.packet_entities.empty());
    }
}

TEST_CASE("E28 source command replay and binding aliases preserve one local action",
    "[e28][weapon-suite][replay][aliases][no-stock]") {
    for(const auto id:{1U,2U,3U,4U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U}) {
        CAPTURE(id);f::Module module(static_cast<std::uint8_t>(id));module.submit(1,.02);
        const auto first=module.sample(.02);REQUIRE(first.actions_started>0);
        (void)module.host.drain_audio();(void)module.visuals(.02);
        module.submit(1,.02);const auto repeated=module.sample(.02);
        CHECK(repeated.actions_started==first.actions_started);
        CHECK_FALSE(fire_cue(module.host.drain_audio()));
        const auto canonical=module.observed.weapon_hud;
        module.submit(1,.02);CHECK(module.observed.weapon_hud==canonical);
    }
    for(const auto [id,alias]:std::array<std::pair<unsigned,std::string_view>,3>{{
        {2,"weapon_glock"},{3,"weapon_python"},{4,"weapon_mp5"}}}) {
        CAPTURE(id,alias);f::Module module(static_cast<std::uint8_t>(id));
        auto& catalogue=module.observed.weapon_hud.catalogue;
        std::find_if(catalogue.begin(),catalogue.end(),[=](const auto& w){return w.id==id;})->command_name=alias;
        f::select(module.observed,static_cast<std::uint8_t>(id),2);module.observe(.01);module.submit(1,.02);
        CHECK(module.sample(.02).actions_started==1);
        CHECK(module.host.inventory_selection(module.observed,static_cast<std::uint8_t>(id))->token==alias);
    }
}

TEST_CASE("E28 fixed command timeline has render independent semantic actions and bounded stall recovery",
    "[e28][weapon-suite][fps][stall][no-stock]") {
    for(const auto& weapon:f::weapons) {
        CAPTURE(weapon.id,weapon.command);
        std::optional<std::size_t> baseline;
        for(const auto fps:{30,60,144,240})for(const bool stalled:{false,true}) {
            CAPTURE(fps,stalled);f::Module module(weapon.id);
            double next_frame=0;const auto frame_period=1./fps;
            for(unsigned tick=1;tick<=300;++tick) {
                const double at=tick*.02;
                while(next_frame<=at+1.e-9) {
                    if(next_frame<at-1.e-9 && !(stalled && next_frame>=1 && next_frame<3)) {
                        (void)module.sample(next_frame);(void)module.visuals(next_frame);
                        const auto beam=module.host.beam_effects(module.world,next_frame,{});
                        CHECK(beam.count<=beam.sprites.size());
                        const auto audio=module.host.drain_audio();CHECK(audio.count<=audio.cues.size());
                        CHECK(audio.control_count<=audio.controls.size());
                    }
                    next_frame+=frame_period;
                }
                if(weapon.id==10 && (tick==11 || tick==41 || tick==161 || tick==191)) {
                    // Same authoritative event timeline at every FPS: confirm
                    // each Egon run once, then publish the server's idle state.
                    f::slot(module.observed,10).auxiliary_integer3=(tick==11 || tick==161) ? 1 : 0;
                    f::select(module.observed,10,tick+1);module.observe(at);
                }
                const bool held=(at>=.2 && at<.8) || (at>=3.2 && at<3.8);
                module.submit(tick,at,held ? 1U : 0U);
                (void)module.host.drain_audio();
            }
            const auto terminal=module.sample(6.01);CHECK(terminal.actions_started>0);
            if(!baseline)baseline=terminal.actions_started;
            CHECK(terminal.actions_started==*baseline);
            CHECK(module.host.hud(module.observed,6.01).clip==(weapon.clip<0 ? std::optional<int>{} : std::optional<int>{weapon.clip}));
            CHECK(module.observed.weapon_hud.clips[weapon.id]==weapon.clip);
            const auto beam=module.host.beam_effects(module.world,6.01,{});CHECK(beam.count<=128);
            CHECK(module.host.drain_temporary_effects(6.01).count==0);
            const auto finished=module.host.drain_audio();CHECK(finished.count<=32);CHECK(finished.control_count<=32);
        }
    }
}

TEST_CASE("E28 known absent peer cannot acquire a late or changed custom beam attachment",
    "[e28][weapon-suite][peer-disconnect][custom-beam][no-stock]") {
    namespace tf=hlclient::tests::tripmine_world_fixture;
    namespace sf=hlclient::tests::snark_world_fixture;
    for(const bool previously_bound:{false,true}) {
        CAPTURE(previously_bound);auto fixture=std::make_unique<tf::Owned>();
        fixture->state.packet_entities={sf::player(),tf::Owned::mine()};
        if(previously_bound)fixture->state.packet_entities.push_back(tf::Owned::beam(50,2));
        fixture->publish(2,2,10);
        if(previously_bound) {
            const auto alive=fixture->scene(10);REQUIRE(alive.follow_trail_entities);
            CHECK(alive.follow_trail_entities->frame->sprite_instances().size()==1);
        }
        api::GameRecordState directory;directory.multiplayer.maximum_clients=8;
        directory.multiplayer.players[1].identity_observed=true;
        directory.multiplayer.players[1].occupant_epoch=1;
        directory.multiplayer.players[1].present=false;
        directory.weapon_hud=fixture->state.weapon_hud;directory.lifecycle=fixture->state.lifecycle;
        fixture->module.host.commit_record(std::move(directory));
        fixture->assets->player_slot_boundary(2);
        fixture->publish(3,2.05,10.05);
        CHECK_FALSE(fixture->scene(10.05).follow_trail_entities);
        // The actor's last packet-entity model is intentionally retained. A
        // second committed channel now proves that its occupant has left.
        auto stale_beam=tf::Owned::beam(50,2);
        if(previously_bound)stale_beam.origin.x=-20.;
        auto world_points=tf::Owned::beam(51);
        world_points.custom_beam->type=c::RuntimeCustomBeamObservation::Type::points;
        world_points.custom_beam->end_reference={};world_points.angles={12.,0.,16.};
        fixture->state.packet_entities={sf::player(),tf::Owned::mine(),stale_beam,world_points,
            tf::Owned::beam(52,40)};
        fixture->publish(4,2.1,10.1);
        const auto scene=fixture->scene(10.1);REQUIRE(scene.follow_trail_entities);
        const auto beams=scene.follow_trail_entities->frame->sprite_instances();
        CHECK(beams.size()==2);
        CHECK(std::none_of(beams.begin(),beams.end(),[](const auto& beam){return beam.entity_number==50;}));
        CHECK(std::any_of(beams.begin(),beams.end(),[](const auto& beam){return beam.entity_number==51;}));
        CHECK(std::any_of(beams.begin(),beams.end(),[](const auto& beam){return beam.entity_number==52;}));
        CHECK(fixture->state.packet_entities.size()==5);
        CHECK(fixture->state.packet_entities[0].model_index==7);
        CHECK(std::any_of(fixture->world.entity_frame()->studio_instances().begin(),
            fixture->world.entity_frame()->studio_instances().end(),[](const auto& instance){return instance.entity_number==40;}));
        CHECK(std::none_of(fixture->world.entity_frame()->studio_instances().begin(),
            fixture->world.entity_frame()->studio_instances().end(),[](const auto& instance){return instance.entity_number==2;}));
        api::GameRecordState replacement;replacement.multiplayer.maximum_clients=8;
        replacement.multiplayer.players[1].identity_observed=true;
        replacement.multiplayer.players[1].occupant_epoch=2;replacement.multiplayer.players[1].present=true;
        replacement.weapon_hud=fixture->state.weapon_hud;replacement.lifecycle=fixture->state.lifecycle;
        fixture->module.host.commit_record(std::move(replacement));fixture->assets->player_slot_boundary(2);
        fixture->publish(5,2.2,10.2);
        const auto reused=fixture->scene(10.2);REQUIRE(reused.follow_trail_entities);
        const auto reused_beams=reused.follow_trail_entities->frame->sprite_instances();
        CHECK(reused_beams.size()==2);
        CHECK(std::none_of(reused_beams.begin(),reused_beams.end(),[](const auto& beam){return beam.entity_number==50;}));
        // Explicit changed server geometry is a new binding occurrence, rather
        // than transferring the unchanged old receipt to the new occupant.
        fixture->state.packet_entities[2].origin.x=-25.;fixture->publish(6,2.3,10.3);
        const auto renewed=fixture->scene(10.3);REQUIRE(renewed.follow_trail_entities);
        CHECK(renewed.follow_trail_entities->frame->sprite_instances().size()==3);
    }
}

TEST_CASE("E28 custom beam waits for an initially unknown source without permanent retirement",
    "[e28][weapon-suite][peer-disconnect][custom-beam][no-stock]") {
    namespace tf=hlclient::tests::tripmine_world_fixture;
    namespace sf=hlclient::tests::snark_world_fixture;
    auto fixture=std::make_unique<tf::Owned>();
    fixture->state.packet_entities={tf::Owned::mine(),tf::Owned::beam(50,2)};
    fixture->publish(2,2,10);CHECK_FALSE(fixture->scene(10).follow_trail_entities);
    fixture->state.packet_entities.insert(fixture->state.packet_entities.begin(),sf::player());
    fixture->publish(3,2.1,10.1);
    const auto ready=fixture->scene(10.1);REQUIRE(ready.follow_trail_entities);
    const auto beams=ready.follow_trail_entities->frame->sprite_instances();REQUIRE(beams.size()==1);
    CHECK(beams[0].entity_number==50);
}
