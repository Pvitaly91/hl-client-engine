#include <hlclient/app/multiplayer_presentation_diagnostics.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#include "entity_render/entity_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/renderer/render_camera_math.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <type_traits>
namespace {
using namespace hlclient;
struct Sink:core::MultiplayerDiagnosticSink {
    bool on=true;std::vector<core::MultiplayerDiagnosticEvent> events;
    bool enabled()const noexcept override{return on;}
    bool observe(const core::MultiplayerDiagnosticEvent&e)noexcept override {events.push_back(e);return true;}
};
struct BenchmarkSink final:core::MultiplayerDiagnosticSink {
    bool on=true;
    std::uint64_t digest=14'695'981'039'346'656'037ULL;
    std::uint64_t events{},actions{},view_rays{},command_rays{},source_models{},
        proxy_hits{},incomplete{},exhausted{};
    bool enabled()const noexcept override{return on;}
    void reset() noexcept {
        digest=14'695'981'039'346'656'037ULL;
        events=actions=view_rays=command_rays=source_models=proxy_hits=incomplete=exhausted=0;
    }
    void mix(std::uint64_t value) noexcept {digest^=value;digest*=1'099'511'628'211ULL;}
    template<class T> void mix_optional(const std::optional<T>& value) noexcept {
        mix(value.has_value());
        if(value) {
            if constexpr(std::is_same_v<T,double>)mix(std::bit_cast<std::uint64_t>(*value));
            else mix(static_cast<std::uint64_t>(*value));
        }
    }
    void mix_vector(const std::optional<std::array<float,3>>& value) noexcept {
        mix(value.has_value());
        if(value)for(const float axis:*value)mix(std::bit_cast<std::uint32_t>(axis));
    }
    bool observe(const core::MultiplayerDiagnosticEvent& event) noexcept override {
        ++events;
        mix(event.payload.index());mix(static_cast<std::uint64_t>(event.identity.producer));
        mix(event.identity.producer_event_id);mix_optional(event.identity.source_query_id);
        mix_optional(event.identity.render_frame);
        if(const auto* p=std::get_if<core::MultiplayerGeometryPayload>(&event.payload)) {
            mix(static_cast<std::uint64_t>(p->kind));
            if(p->kind==core::MultiplayerGeometryKind::last_presented_view)++view_rays;
            if(p->kind==core::MultiplayerGeometryKind::command_query)++command_rays;
            if(p->kind==core::MultiplayerGeometryKind::source_model_query)++source_models;
            proxy_hits+=p->proxy_actor.has_value();
            incomplete+=p->query_incomplete.value_or(false);
            exhausted+=p->query_budget_exhausted.value_or(false);
            mix_vector(p->origin);mix_vector(p->direction);mix_vector(p->endpoint);
            mix_optional(p->proxy_actor);mix_optional(p->proxy_model_revision);
            mix_optional(p->hitbox_group);mix_optional(p->fraction);
            mix_optional(p->intersects);mix_optional(p->exact_model_pose);
            mix_optional(p->query_incomplete);mix_optional(p->query_budget_exhausted);
            mix_optional(p->world_blocked);mix_optional(p->blocker_entity);
        } else if(std::holds_alternative<core::MultiplayerActionPayload>(event.payload))++actions;
        return true;
    }
};
struct Blocker:game_api::IStaticEffectTrace {
    bool blocked=true;
    game_api::StaticEffectTrace trace(assets::AssetVector3,assets::AssetVector3)const noexcept override {
        game_api::StaticEffectTrace t;t.valid=true;t.hit=blocked;t.point={5,0,0};
        if(blocked){game_api::EffectSurfaceAttachment a;a.entity=42;t.surface_attachment=a;}return t;
    }
};
TEST_CASE("Comprehensive last completed swap proxy follows actual OpenGL camera effects and projection", "[comprehensive][geometry][opengl][actual-context]") {
    namespace fixture=tests::entity_opengl_fixture;
    auto context=fixture::try_context(320,240);
    if(!context||!fixture::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();auto entities=fixture::studio_scene();auto scene=fixture::render_scene(entities);
    scene.screen_effects.translation={.15F,.1F,.05F};scene.screen_effects.roll_degrees=3;
    scene.camera.vertical_field_of_view_radians=1.1F;
    context->renderer().render(scene,{320,240});
    const auto pixels=context->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(pixels);REQUIRE(pixels.non_clear_pixel_count>0);
    REQUIRE(SDL_GL_SwapWindow(SDL_GL_GetCurrentWindow()));
    Sink sink;app::MultiplayerPresentationDiagnostics bridge{sink};
    bridge.completed_frame(scene,17,1,1,core::multiplayer_diagnostic_now_ns(),320,240);
    REQUIRE_FALSE(sink.events.empty());
    CHECK(std::get<core::MultiplayerPresentationPayload>(sink.events.front().payload).phase==core::MultiplayerPresentationPhase::pose);
    sink.events.clear();
    core::MultiplayerDiagnosticEvent event;event.identity.network_generation=1;event.identity.map_generation=1;event.local_steady_ns=core::multiplayer_diagnostic_now_ns();
    core::MultiplayerActionPayload action;action.phase=core::MultiplayerActionPhase::accepted;
    action.origin=std::array{scene.camera.position.x,scene.camera.position.y,scene.camera.position.z};action.direction=std::array<float,3>{0,1,0};event.payload=action;
    REQUIRE(bridge.observe(event));REQUIRE(sink.events.size()>=3);
    const auto& shown=std::get<core::MultiplayerGeometryPayload>(sink.events[1].payload);
    REQUIRE(shown.origin);CHECK((*shown.origin)[0]==scene.camera.position.x+.15F);
    CHECK(shown.origin!=action.origin);CHECK(shown.vertical_fov_radians==1.1F);
    CHECK(shown.framebuffer_width==320);CHECK(shown.viewport_height==240);
    CHECK(sink.events[1].identity.render_frame==17);CHECK(glGetError()==GL_NO_ERROR);
    context->release_renderer();
}
TEST_CASE("Comprehensive production scene query uses actual pose and reports blocker without claiming server hit", "[comprehensive][geometry][production-path]") {
    namespace fixture=tests::entity_render_fixture;
    auto model=std::make_shared<assets::ModelAsset>(*fixture::model_asset());
    auto skeleton=std::make_shared<assets::SkeletalModelAssetData>(*model->skeletal_data);
    skeleton->hitboxes.push_back({0,2,{{-1,-1,-1},{1,1,1}}});model->skeletal_data=skeleton;
    auto package=fixture::scene_package(fixture::render_assets(true,false,assets::SpriteTextureFormat::normal,false,assets::SpriteOrientation::view_parallel,model));REQUIRE(package);
    entity_render::EntityRenderFrameBuildInput input;input.resource_id=99;input.resource_revision=1;
    input.interpolation={.5,0,1,.5F,1,2,entity_render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back({package.package->studio_assets()[0]->source_identity(),{renderer::RenderMatrix4{}.values}});
    entity_render::StudioEntityRenderInstance actor;actor.entity_number=2;actor.transform.origin={10,0,0};actor.interpolated_bounds={{9,-1,-1},{11,1,1}};input.studio_instances.push_back(actor);
    auto built=entity_render::EntityRenderFrameBuilder{}.build(*package.package,std::move(input));REQUIRE(built);
    renderer::RenderScene scene;scene.camera.position={0,0,0};scene.camera.target={1,0,0};
    scene.dynamic_entities=renderer::RenderDynamicEntities{
        std::make_shared<const entity_render::EntitySceneRenderPackage>(std::move(*package.package)),
        std::make_shared<const entity_render::EntityRenderFrame>(std::move(*built.frame)),{}};
    Sink sink;Blocker blocker;app::MultiplayerPresentationDiagnostics bridge{sink};bridge.set_world_query(&blocker);
    bridge.completed_frame(scene,1,1,1,1000000000,640,480);
    REQUIRE(sink.events.size()==1);
    CHECK(std::get<core::MultiplayerPresentationPayload>(sink.events.front().payload).origin==std::array<float,3>{10,0,0});
    sink.events.clear();
    core::MultiplayerDiagnosticEvent e;e.identity.network_generation=1;e.identity.map_generation=1;e.local_steady_ns=1010000000;
    core::MultiplayerActionPayload a;a.phase=core::MultiplayerActionPhase::accepted;a.origin=std::array<float,3>{};a.direction=std::array<float,3>{1,0,0};e.payload=a;
    REQUIRE(bridge.observe(e));REQUIRE(sink.events.size()==5);
    const auto& query=std::get<core::MultiplayerGeometryPayload>(sink.events[1].payload);
    CHECK(query.intersects==true);CHECK(query.proxy_actor==2);CHECK(query.hitbox_group==2);CHECK(query.exact_model_pose==true);
    CHECK(query.world_blocked==true);CHECK(query.blocker_entity==42);CHECK(sink.events[1].knowledge==core::MultiplayerKnowledge::local_proxy);
    blocker.blocked=false;REQUIRE(bridge.observe(e));
    CHECK(std::get<core::MultiplayerGeometryPayload>(sink.events[6].payload).world_blocked==false);
    a.origin=std::array<float,3>{std::numeric_limits<float>::quiet_NaN(),0,0};e.payload=a;
    const auto count=sink.events.size();REQUIRE(bridge.observe(e));CHECK(sink.events.size()==count+1);
}
TEST_CASE("Two diagnostic rays stay independent and source cache changes with immutable package owner",
    "[comprehensive][geometry][production-path][cache]") {
    namespace fixture=tests::entity_render_fixture;
    const auto make_scene=[](const std::int32_t group) {
        auto model=std::make_shared<assets::ModelAsset>(*fixture::model_asset());
        auto skeleton=std::make_shared<assets::SkeletalModelAssetData>(*model->skeletal_data);
        skeleton->hitboxes.clear();
        skeleton->hitboxes.push_back({0,group,{{-1,-1,-1},{1,1,1}}});
        model->skeletal_data=skeleton;
        auto package=fixture::scene_package(fixture::render_assets(true,false,
            assets::SpriteTextureFormat::normal,false,assets::SpriteOrientation::view_parallel,model));
        REQUIRE(package);
        entity_render::EntityRenderFrameBuildInput input;input.resource_id=99;input.resource_revision=1;
        input.interpolation={.5,0,1,.5F,1,2,entity_render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
        input.studio_poses.push_back({package.package->studio_assets()[0]->source_identity(),
            {renderer::RenderMatrix4{}.values}});
        entity_render::StudioEntityRenderInstance actor;actor.entity_number=2;
        actor.transform.origin={10,0,0};actor.interpolated_bounds={{9,-1,-1},{11,1,1}};
        input.studio_instances.push_back(actor);
        auto built=entity_render::EntityRenderFrameBuilder{}.build(*package.package,std::move(input));
        REQUIRE(built);
        renderer::RenderScene scene;scene.camera.position={0,0,0};scene.camera.target={1,0,0};
        scene.dynamic_entities=renderer::RenderDynamicEntities{
            std::make_shared<const entity_render::EntitySceneRenderPackage>(std::move(*package.package)),
            std::make_shared<const entity_render::EntityRenderFrame>(std::move(*built.frame)),{}};
        return scene;
    };
    auto first=make_scene(2),second=make_scene(4);
    REQUIRE(first.dynamic_entities);REQUIRE(second.dynamic_entities);
    CHECK(first.dynamic_entities->package!=second.dynamic_entities->package);
    CHECK(first.dynamic_entities->package->studio_assets()[0]->source_identity()==
        second.dynamic_entities->package->studio_assets()[0]->source_identity());
    Sink sink;app::MultiplayerPresentationDiagnostics bridge{sink};
    core::MultiplayerDiagnosticEvent e;e.identity.network_generation=1;e.identity.map_generation=1;
    core::MultiplayerActionPayload a;a.phase=core::MultiplayerActionPhase::accepted;
    a.origin=std::array<float,3>{0,4,0};a.direction=std::array<float,3>{1,0,0};e.payload=a;
    for(unsigned pass=0;pass<2;++pass) {
        const auto stamp=1'000'000'000ULL+pass*1'000'000'000ULL;
        bridge.completed_frame(pass?second:first,pass+1,1,1,stamp,640,480);
        sink.events.clear();e.local_steady_ns=stamp+10'000'000ULL;
        REQUIRE(bridge.observe(e));REQUIRE(sink.events.size()==5);
        const auto& view=std::get<core::MultiplayerGeometryPayload>(sink.events[1].payload);
        const auto& command=std::get<core::MultiplayerGeometryPayload>(sink.events[3].payload);
        CHECK(view.kind==core::MultiplayerGeometryKind::last_presented_view);
        CHECK(view.proxy_actor==2);CHECK(view.hitbox_group==(pass?4U:2U));
        CHECK(view.intersects==true);CHECK(view.exact_model_pose==true);
        CHECK(command.kind==core::MultiplayerGeometryKind::command_query);
        CHECK_FALSE(command.proxy_actor);CHECK(command.intersects==false);
        CHECK(command.exact_model_pose==true);
        CHECK(sink.events[1].identity.source_query_id==sink.events[3].identity.source_query_id);
        CHECK(sink.events[2].identity.source_query_id==sink.events[4].identity.source_query_id);
    }
}
TEST_CASE("Comprehensive affine MDL proxy ray handles scale rotation blockers and free controls", "[comprehensive][geometry]") {
    const std::array<float,16> transform{0,2,0,0,-2,0,0,0,0,0,2,0,10,0,0,1};
    const std::array<float,3> lo{-1,-1,-1},hi{1,1,1};
    const auto hit=app::diagnostic_ray_box({0,0,0},{1,0,0},transform,lo,hi);
    REQUIRE(hit);CHECK(std::abs(*hit-8)<1e-6);
    const auto miss=app::diagnostic_ray_box({0,3,0},{1,0,0},transform,lo,hi);REQUIRE(miss);CHECK(std::isinf(*miss));
    auto invalid=transform;invalid[0]=invalid[1]=0;
    CHECK_FALSE(app::diagnostic_ray_box({0,0,0},{1,0,0},invalid,lo,hi));
}
TEST_CASE("Comprehensive last presented proxy remains unknown without real pose and never becomes a server miss", "[comprehensive][geometry]") {
    Sink sink;app::MultiplayerPresentationDiagnostics bridge{sink};
    renderer::RenderScene frame;frame.camera.position={1,2,3};frame.camera.target={2,2,3};
    bridge.completed_frame(frame,77,1,2,1000000000,1920,1080);
    core::MultiplayerDiagnosticEvent e;e.identity.producer=core::MultiplayerDiagnosticProducer::game_host;
    e.identity.producer_event_id=1;e.identity.network_generation=1;e.identity.map_generation=2;
    e.identity.command_id=9;e.identity.action_id=12;e.local_steady_ns=1010000000;
    core::MultiplayerActionPayload action;action.phase=core::MultiplayerActionPhase::accepted;
    action.origin=std::array<float,3>{4,5,6};action.direction=std::array<float,3>{0,1,0};e.payload=action;
    REQUIRE(bridge.observe(e));REQUIRE(sink.events.size()==3);
    const auto& view=std::get<core::MultiplayerGeometryPayload>(sink.events[1].payload);
    const auto& command=std::get<core::MultiplayerGeometryPayload>(sink.events[2].payload);
    CHECK(view.origin==std::array<float,3>{1,2,3});CHECK(command.origin==action.origin);
    CHECK(view.direction!=command.direction);CHECK(view.proxy_age_ms==10);
    CHECK_FALSE(view.intersects.has_value());CHECK_FALSE(command.intersects.has_value());
    CHECK(sink.events[2].knowledge==core::MultiplayerKnowledge::local_proxy);
    CHECK(sink.events[2].identity.render_frame==77);CHECK(sink.events[2].identity.command_id==9);
    bridge.invalidate();REQUIRE(bridge.observe(e));CHECK(sink.events.size()==4);
    sink.on=false;CHECK_FALSE(bridge.observe(e));CHECK(sink.events.size()==4);
}
TEST_CASE("Comprehensive generation boundary and stale frame cannot fabricate geometry", "[comprehensive][geometry]") {
    Sink sink;app::MultiplayerPresentationDiagnostics bridge{sink};renderer::RenderScene frame;
    bridge.completed_frame(frame,1,1,1,100,640,480);
    core::MultiplayerDiagnosticEvent e;e.identity.network_generation=2;e.identity.map_generation=1;e.local_steady_ns=200;
    core::MultiplayerActionPayload a;a.phase=core::MultiplayerActionPhase::accepted;a.origin=std::array<float,3>{};a.direction=std::array<float,3>{1,0,0};e.payload=a;
    REQUIRE(bridge.observe(e));CHECK(sink.events.size()==1);
}
TEST_CASE("Warm production frame and action geometry diagnostics have bounded measured On Off cost", "[comprehensive][geometry][overhead]") {
    namespace fixture=tests::entity_render_fixture;
    auto model=std::make_shared<assets::ModelAsset>(*fixture::model_asset());
    auto skeleton=std::make_shared<assets::SkeletalModelAssetData>(*model->skeletal_data);
    skeleton->hitboxes.push_back({0,2,{{-1,-1,-1},{1,1,1}}});model->skeletal_data=skeleton;
    auto package=fixture::scene_package(fixture::render_assets(true,false,assets::SpriteTextureFormat::normal,false,assets::SpriteOrientation::view_parallel,model));REQUIRE(package);
    entity_render::EntityRenderFrameBuildInput input;input.resource_id=99;input.resource_revision=1;
    input.interpolation={.5,0,1,.5F,1,2,entity_render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back({package.package->studio_assets()[0]->source_identity(),{renderer::RenderMatrix4{}.values}});
    for(std::uint32_t i=1;i<=32;++i){entity_render::StudioEntityRenderInstance actor;actor.entity_number=i;
        actor.transform.origin={10+static_cast<float>(i),0,0};actor.interpolated_bounds={{9,-1,-1},{43,1,1}};input.studio_instances.push_back(actor);}
    auto built=entity_render::EntityRenderFrameBuilder{}.build(*package.package,std::move(input));REQUIRE(built);
    renderer::RenderScene scene;scene.camera.target={1,0,0};
    scene.dynamic_entities=renderer::RenderDynamicEntities{
        std::make_shared<const entity_render::EntitySceneRenderPackage>(std::move(*package.package)),
        std::make_shared<const entity_render::EntityRenderFrame>(std::move(*built.frame)),{}};
    app::MovementIncidentRecorder off{false},on{true};
    app::MultiplayerPresentationDiagnostics off_bridge{off},on_bridge{on};Blocker world;
    off_bridge.set_world_query(&world);on_bridge.set_world_query(&world);
    constexpr std::size_t warm=200,samples=1000;
    std::array<std::array<double,samples>,2> measured{};
    core::MultiplayerDiagnosticEvent action;action.identity.producer=core::MultiplayerDiagnosticProducer::game_host;
    action.identity.network_generation=1;action.identity.map_generation=1;
    action.knowledge=core::MultiplayerKnowledge::local_action_accepted;
    core::MultiplayerActionPayload payload;payload.phase=core::MultiplayerActionPhase::accepted;
    payload.origin=std::array<float,3>{};payload.direction=std::array<float,3>{1,0,0};action.payload=payload;
    for(std::size_t i=0;i<warm+samples;++i){
        action.identity.producer_event_id=i+1;action.identity.action_id=i+1;
        const auto stamp=1'000'000'000ULL+static_cast<std::uint64_t>(i)*10'000'000ULL;
        action.local_steady_ns=stamp+1000;
        for(unsigned ordinal=0;ordinal<2;++ordinal){const auto mode=static_cast<unsigned>((i+ordinal)%2);
            auto& bridge=mode?on_bridge:off_bridge;const auto begin=std::chrono::steady_clock::now();
            bridge.completed_frame(scene,i+1,1,1,stamp,1920,1080);
            static_cast<void>(bridge.observe(action));
            const auto cost=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
            if(i>=warm)measured[mode][i-warm]=cost;
        }
    }
    CHECK(off.statistics().multiplayer_capacity_bytes==0);
    CHECK(on.statistics().multiplayer_capacity_bytes<=app::MultiplayerTimeline::maximum_capacity_bytes);
    for(unsigned mode=0;mode<2;++mode){std::sort(measured[mode].begin(),measured[mode].end());
        std::cout<<"[multiplayer-geometry-cost] scope=frame_observation_and_action_geometry actors=32 rays=2 samples="<<samples
            <<" mode="<<(mode?"on":"off")<<" p50_us="<<measured[mode][499]<<" p95_us="<<measured[mode][949]
            <<" p99_us="<<measured[mode][989]<<" max_us="<<measured[mode].back()<<'\n';
        for(const auto cost:measured[mode])CHECK(std::isfinite(cost));
    }
    // This measures the production observer/query work on one identical warm
    // CPU scene. SDL, networking, simulation, GPU and total live FPS are outside
    // this fixture; it must not be advertised as whole-game overhead.
}

TEST_CASE("Benchmark action-only two-ray geometry diagnostics with stable output identities",
    "[.][optimization-benchmark][geometry][action-only]") {
    namespace fixture=tests::entity_render_fixture;
    auto model=std::make_shared<assets::ModelAsset>(*fixture::model_asset());
    auto skeleton=std::make_shared<assets::SkeletalModelAssetData>(*model->skeletal_data);
    skeleton->hitboxes.push_back({0,2,{{-1,-1,-1},{1,1,1}}});model->skeletal_data=skeleton;
    auto package=fixture::scene_package(fixture::render_assets(true,false,
        assets::SpriteTextureFormat::normal,false,assets::SpriteOrientation::view_parallel,model));
    REQUIRE(package);
    entity_render::EntityRenderFrameBuildInput input;input.resource_id=99;input.resource_revision=1;
    input.interpolation={.5,0,1,.5F,1,2,entity_render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back({package.package->studio_assets()[0]->source_identity(),
        {renderer::RenderMatrix4{}.values}});
    for(std::uint32_t i=1;i<=32;++i) {
        entity_render::StudioEntityRenderInstance actor;actor.entity_number=i;
        actor.transform.origin={10+static_cast<float>(i),0,0};
        actor.interpolated_bounds={{9,-1,-1},{43,1,1}};input.studio_instances.push_back(actor);
    }
    auto built=entity_render::EntityRenderFrameBuilder{}.build(*package.package,std::move(input));
    REQUIRE(built);
    renderer::RenderScene scene;scene.camera.position={0,0,0};scene.camera.target={1,0,0};
    scene.dynamic_entities=renderer::RenderDynamicEntities{
        std::make_shared<const entity_render::EntitySceneRenderPackage>(std::move(*package.package)),
        std::make_shared<const entity_render::EntityRenderFrame>(std::move(*built.frame)),{}};
    BenchmarkSink on,off;off.on=false;Blocker world;
    app::MultiplayerPresentationDiagnostics on_bridge{on},off_bridge{off};
    on_bridge.set_world_query(&world);off_bridge.set_world_query(&world);
    constexpr std::uint64_t frame_ns=1'000'000'000ULL;
    on_bridge.completed_frame(scene,17,1,1,frame_ns,1920,1080);
    off_bridge.completed_frame(scene,17,1,1,frame_ns,1920,1080);
    on.reset();off.reset();
    constexpr std::size_t warmup=200,samples=1000;
    std::array<std::array<double,samples>,2> measured{};
    core::MultiplayerDiagnosticEvent action;
    action.identity.producer=core::MultiplayerDiagnosticProducer::game_host;
    action.identity.network_generation=1;action.identity.map_generation=1;
    action.knowledge=core::MultiplayerKnowledge::local_action_accepted;
    action.local_steady_ns=frame_ns+1'000'000ULL;
    core::MultiplayerActionPayload payload;payload.phase=core::MultiplayerActionPhase::accepted;
    payload.direction=std::array<float,3>{1,0,0};
    for(std::size_t i=0;i<warmup+samples;++i) {
        if(i==warmup){on.reset();off.reset();}
        action.identity.producer_event_id=i+1;action.identity.action_id=i+1;
        payload.origin=i%2==0?std::array<float,3>{0,0,0}:std::array<float,3>{0,4,0};
        action.payload=payload;
        for(unsigned ordinal=0;ordinal<2;++ordinal) {
            const auto mode=static_cast<unsigned>((i+ordinal)%2);
            auto& bridge=mode?on_bridge:off_bridge;
            const auto begin=std::chrono::steady_clock::now();
            const bool accepted=bridge.observe(action);
            const auto elapsed=std::chrono::duration<double,std::micro>(
                std::chrono::steady_clock::now()-begin).count();
            CHECK(accepted==bool(mode));
            if(i>=warmup)measured[mode][i-warmup]=elapsed;
        }
    }
    CHECK(off.events==0);
    CHECK(on.actions==samples);
    CHECK(on.view_rays==samples);
    CHECK(on.command_rays==samples);
    CHECK(on.source_models==2*samples);
    CHECK(on.incomplete==0);CHECK(on.exhausted==0);
    for(unsigned mode=0;mode<2;++mode) {
        auto& values=measured[mode];std::sort(values.begin(),values.end());
        for(const auto value:values)CHECK(std::isfinite(value));
        std::cout<<"[optimization-benchmark] scope=multiplayer_action_two_rays actors=32 samples="<<samples
            <<" mode="<<(mode?"on":"off")
            <<" events="<<(mode?on.events:off.events)
            <<" view_rays="<<(mode?on.view_rays:off.view_rays)
            <<" command_rays="<<(mode?on.command_rays:off.command_rays)
            <<" source_models="<<(mode?on.source_models:off.source_models)
            <<" proxy_hits="<<(mode?on.proxy_hits:off.proxy_hits)
            <<" incomplete="<<(mode?on.incomplete:off.incomplete)
            <<" exhausted="<<(mode?on.exhausted:off.exhausted)
            <<" output_identity_hash="<<(mode?on.digest:off.digest)
            <<" p50_us="<<values[499]<<" p95_us="<<values[949]
            <<" p99_us="<<values[989]<<" max_us="<<values.back()<<'\n';
    }
}
}
