#include "snark_world_test_fixture.hpp"
#include "snark_sound_pop_test_fixture.hpp"
#include "effect_palette_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
namespace t=hlclient::tests;
namespace app=hlclient::app;
namespace api=hlclient::game_api;
namespace renderer=hlclient::renderer;
namespace wf=t::hand_grenade_world_fixture;
namespace floor_fixture=t::world_render_fixture;
constexpr unsigned width=320,height=240;

// Independently authored masked diamonds, not installed sprite pixels. Match
// the relevant source geometry/frame durations so a ground-origin test also
// exercises animation expiry instead of passing on a one-frame billboard.
std::vector<std::byte> owned_blood_sprite(int size,int frames) {
    auto bytes=t::sprite_fixture::literal_header_and_palette(frames,3,2,0,size,size);
    for(unsigned component=0;component<3;++component)
        bytes[t::sprite_fixture::kPaletteOffset+3+component]=std::byte{200};
    bytes[t::sprite_fixture::kPaletteOffset+255*3]=std::byte{0};
    bytes[t::sprite_fixture::kPaletteOffset+255*3+1]=std::byte{0};
    bytes[t::sprite_fixture::kPaletteOffset+255*3+2]=std::byte{255};
    for(int frame=0;frame<frames;++frame) {
        t::sprite_fixture::FrameSpec image{-size/2,size/2,size,size,
            std::vector<std::byte>(static_cast<std::size_t>(size*size),std::byte{255})};
        const int radius=size/4+frame%3;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x)
            if(std::abs(x-size/2)+std::abs(y-size/2)<=radius)
                image.pixels[static_cast<std::size_t>(y*size+x)]=std::byte{1};
        t::sprite_fixture::append_single_entry(bytes,image);
    }
    return bytes;
}

std::unique_ptr<app::RuntimeReplayLocalAssets> pop_assets(
    const std::filesystem::path& root,std::string_view map,
    hlclient::client::ClientWorldState& world) {
    const auto resources=t::readiness_fixture::parse_resource_list({
        {2,std::string{map},9,0,0},
        {2,"sprites/bloodspray.spr",321,0,0},{2,"sprites/blood.spr",322,0,0}});
    auto made=app::RuntimeReplayLocalAssets::create(resources,
        t::readiness_fixture::parse_server_info(std::string{map}),root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client,"gfx/palette.lmp");
    REQUIRE(made.projection);
    auto state=wf::state();state.packet_entities.clear();wf::stamp(state,1,1);
    REQUIRE(made.projection->reset_generation(state,world));
    return std::move(made.projection);
}

std::shared_ptr<const hlclient::world_render::WorldRenderPackage> ground() {
    // A real static-world draw packet, built by the normal world builder. Its
    // extent uses gameplay units, not the previous 16-unit close-up fixture.
    auto world=floor_fixture::make_textured_world();
    for(auto& vertex:world.world.vertices) {
        vertex.position={vertex.position.x*64-512,vertex.position.y*64-512,0};
        vertex.normal={0,0,1};
    }
    world.world.bounds={{-512,-512,0},{512,512,0}};
    world.world.surfaces[0].bounds=world.world.bounds;
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        std::move(world),floor_fixture::make_lightmap_set({.unlit=true}));
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}

api::TemporaryEffectsBatch committed_pop(api::GameClientHost& host) {
    auto event=t::snark_world_fixture::blood();event.origin={0,0,0};
    host.committed_temporary_effect(event);
    auto batch=host.drain_temporary_effects(0);
    REQUIRE(batch.count==1);
    REQUIRE(batch.effects[0].sprite);REQUIRE(batch.effects[0].sprite_burst);
    CHECK_FALSE(batch.effects[0].secondary_sprite);
    CHECK((batch.effects[0].sprite->model_slot==321 && batch.effects[0].sprite_burst->sprite.model_slot==322));
    CHECK(batch.effects[0].sprite_burst->count==16);
    CHECK((batch.statistics.explosion==0 && batch.statistics.smoke==0 && batch.statistics.sparks==0));
    CHECK_FALSE((batch.effects[0].light || batch.effects[0].particles || batch.effects[0].sound));
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
    return batch;
}

api::TemporaryEffectsBatch previous_profile(api::TemporaryEffectsBatch batch) {
    // A/B control is the former production policy, passed through the same
    // owner, approved importer, materializer, world depth and OpenGL pass.
    auto& primary=*batch.effects[0].sprite;
    primary.scale=16*.15F;primary.vertical_speed_units=80;
    primary.frames_per_second=10;primary.maximum_lifetime_seconds=1;
    primary.gravity_units=160;primary.fade_with_animation=false;primary.tint={1,.85F,0};
    batch.effects[0].sprite_burst.reset();
    batch.effects[0].secondary_sprite=api::TemporarySpriteIntent{322,1.6F,0,1,6,
        api::TemporarySpriteBlend::alpha_blended,1.5,{1,.85F,0},0,false};
    return batch;
}

struct YellowEvidence {
    std::size_t changed_pixels{};
    std::uint64_t signal{};
    unsigned minimum_x{width},maximum_x{};
    unsigned minimum_y{height},maximum_y{};
    unsigned span() const {return changed_pixels ? maximum_x-minimum_x+1 : 0;}
};

YellowEvidence yellow(const std::vector<std::uint8_t>& image,
    const std::vector<std::uint8_t>& baseline) {
    REQUIRE(image.size()==baseline.size());
    YellowEvidence result;
    for(std::size_t pixel=0;pixel<image.size();pixel+=4) {
        const unsigned red=image[pixel],green=image[pixel+1],blue=image[pixel+2];
        const bool changed=red!=baseline[pixel] || green!=baseline[pixel+1] || blue!=baseline[pixel+2];
        if(changed && red>20 && green>20 && green>2*blue && red>green) {
            ++result.changed_pixels;result.signal+=red+green-2*blue;
            const auto x=static_cast<unsigned>(pixel/4)%width;
            result.minimum_x=std::min(result.minimum_x,x);
            result.maximum_x=std::max(result.maximum_x,x);
            const auto y=static_cast<unsigned>(pixel/4)/width;
            result.minimum_y=std::min(result.minimum_y,y);result.maximum_y=std::max(result.maximum_y,y);
        }
    }
    return result;
}

renderer::RenderScene scene_for(
    const std::shared_ptr<const hlclient::world_render::WorldRenderPackage>& floor,float distance) {
    renderer::RenderScene scene;
    scene.clear_color={.04F,.04F,.04F,1};
    scene.camera={{distance,0,64},{0,0,0},{0,0,1},1.04719755F,.1F,2048};
    scene.static_world=renderer::RenderStaticWorld{floor};
    return scene;
}

void present(app::RuntimeReplayLocalAssets& resources,app::TemporaryEffectPresentation& owner,
    renderer::RenderScene& scene,double age) {
    owner.present(scene,age);
    scene.transient_sprite_entities=resources.materialize_temporary_sprites(owner.sprites(),age);
    CHECK((scene.world_effect_light_count==0 && scene.world_particle_count==0 &&
        scene.world_flash_count==0 && scene.world_line_count==0));
    CHECK_FALSE((scene.transient_world_light || scene.first_person_flash));
}

void report_assets_and_frame(const renderer::RenderScene& scene,double age,float distance) {
    std::cout<<"snark-ground-pop distance="<<distance<<" eye_z=64 origin_z=0 age="<<age;
    if(scene.transient_sprite_entities) {
        const auto& draw=*scene.transient_sprite_entities;
        for(const auto& instance:draw.frame->sprite_instances()) {
            const auto& sprite=*draw.package->sprite_assets()[instance.sprite_asset_index];
            const auto& frame=sprite.frames()[instance.selected_frame_index];
            std::cout<<" [frames="<<sprite.frames().size()<<" dimensions="<<frame.geometry.width
                <<"x"<<frame.geometry.height<<" format="<<static_cast<unsigned>(sprite.texture_format())
                <<" profile="<<static_cast<unsigned>(sprite.render_profile())
                <<" selected="<<instance.selected_frame_index<<" scale="<<instance.transform.uniform_scale
                <<" z="<<instance.transform.origin.z<<" opacity="<<instance.opacity<<"]";
        }
    } else std::cout<<" [no sprites]";
    std::cout<<'\n';
}

// Optional offline evidence export. The caller supplies an existing artifact
// directory; this writes generated framebuffer data, never installed assets.
void save_frame(std::string name,const std::vector<std::uint8_t>& pixels) {
    std::filesystem::path directory;
#ifdef _WIN32
    wchar_t* raw=nullptr;std::size_t count{};
    if(_wdupenv_s(&raw,&count,L"HLCLIENT_E271_ARTIFACT_DIR")==0 && raw)directory=raw;
    std::free(raw);
#endif
    if(directory.empty())return;
    REQUIRE(directory.is_absolute());REQUIRE(std::filesystem::is_directory(directory));
    std::ofstream out{directory/(name+".tga"),std::ios::binary};REQUIRE(out);
    const std::array<unsigned char,18> header{0,0,2,0,0,0,0,0,0,0,0,0,
        static_cast<unsigned char>(width&255),static_cast<unsigned char>(width>>8),
        static_cast<unsigned char>(height&255),static_cast<unsigned char>(height>>8),32,0x20};
    out.write(reinterpret_cast<const char*>(header.data()),header.size());
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        const auto at=(static_cast<std::size_t>(height-1-y)*width+x)*4;
        const std::array<std::uint8_t,4> bgra{pixels[at+2],pixels[at+1],pixels[at],pixels[at+3]};
        out.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());
    }
    REQUIRE(out.good());
}

void ground_comparison(app::RuntimeReplayLocalAssets& resources,bool report,bool server_sound=false) {
    auto gl=t::entity_opengl_fixture::try_context(width,height);
    if(!gl)SKIP("OpenGL 3.3 Core context unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    t::snark_sound_pop_fixture::bind(host);
    host.bind_effect_palette(resources.effect_palette());
    REQUIRE(resources.effect_palette().status==api::EffectPaletteStatus::ready);
    const auto batch=[&] {
        if(!server_sound)return committed_pop(host);
        t::snark_sound_pop_fixture::entities(host);
        host.committed_sound(t::snark_sound_pop_fixture::pop());
        auto result=host.drain_temporary_effects(0);REQUIRE(result.count==1);
        CHECK(result.effects[0].identity.source==api::TemporaryEffectIdentity::Source::server_sound);
        CHECK(result.statistics.blood_received==0);CHECK(result.statistics.pop_sound_accepted==1);
        return result;
    }();
    const auto old_batch=previous_profile(batch);
    const auto floor=ground();
    const auto capture=[&](const renderer::RenderScene& scene) {
        gl->renderer().render(scene,{width,height});
        auto image=gl->renderer().observe_framebuffer({width,height},scene.clear_color,true);
        REQUIRE(image);return image;
    };
    for(const float distance:{32.F,64.F,128.F}) {
        INFO("normal ground origin z0, camera eye64, distance="<<distance);
        auto owner_storage=std::make_unique<app::TemporaryEffectPresentation>();
        auto old_storage=std::make_unique<app::TemporaryEffectPresentation>();
        auto spray_storage=std::make_unique<app::TemporaryEffectPresentation>();
        auto drops_storage=std::make_unique<app::TemporaryEffectPresentation>();
        auto& owner=*owner_storage;auto& old_owner=*old_storage;
        auto& spray_owner=*spray_storage;auto& drops_owner=*drops_storage;
        owner.reset(1);old_owner.reset(1);owner.consume(batch,0);old_owner.consume(old_batch,0);
        auto spray_batch=batch,drops_batch=batch;
        spray_batch.effects[0].sprite_burst.reset();drops_batch.effects[0].sprite.reset();
        spray_owner.reset(1);drops_owner.reset(1);spray_owner.consume(spray_batch,0);drops_owner.consume(drops_batch,0);
        const auto baseline=capture(scene_for(floor,distance));
        REQUIRE(baseline.non_clear_pixel_count>0);
        for(const double age:{0.,.1,.25,.45,.8}) {
            INFO("same-clock A/B age="<<age);
            auto scene=scene_for(floor,distance),old_scene=scene;
            present(resources,owner,scene,age);present(resources,old_owner,old_scene,age);
            if(report)report_assets_and_frame(scene,age,distance);
            const auto current=capture(scene),previous=capture(old_scene);
            const auto visible=yellow(current.rgba8,baseline.rgba8);
            const auto control=yellow(previous.rgba8,baseline.rgba8);
            if(age<=.45) {
                CHECK(visible.changed_pixels>4);REQUIRE(scene.transient_sprite_entities);
                CHECK(scene.transient_sprite_entities->frame->sprite_instances().size()==17);
                const auto& spray=scene.transient_sprite_entities->frame->sprite_instances()[0];
                CHECK(spray.selected_frame_index==static_cast<unsigned>(std::floor(age*20)));
                CHECK(spray.transform.origin.z==0);CHECK(spray.transform.uniform_scale==16.F/30.F);
                CHECK(spray.opacity==1);
                for(std::size_t i=1;i<17;++i) {
                    const auto& drop=scene.transient_sprite_entities->frame->sprite_instances()[i];
                    CHECK(drop.transform.uniform_scale==16.F/20.F);CHECK(drop.opacity==1);
                    CHECK(drop.blend_mode==hlclient::entity_render::SpriteBlendMode::alpha_blended);
                    CHECK(drop.texture_format_support==hlclient::entity_render::SpriteRenderTextureSupportStatus::supported_alpha_test_masked);
                }
                // Palette lookup must affect actual GPU channels, preserving
                // source intensity. A brighter arbitrary-yellow quad fails.
                CHECK(visible.signal<control.signal);
            }
            auto spray_scene=scene_for(floor,distance),drops_scene=spray_scene;
            present(resources,spray_owner,spray_scene,age);present(resources,drops_owner,drops_scene,age);
            const auto spray_image=capture(spray_scene),drop_image=capture(drops_scene);
            if(age==.1 || age==.25) {
                const auto mask=yellow(spray_image.rgba8,baseline.rgba8);
                CHECK(mask.changed_pixels>4);
                const auto rectangle=mask.span()*(mask.maximum_y-mask.minimum_y+1);
                CHECK(mask.changed_pixels<rectangle*.95); // Transparent holes, not an opaque tinted quad.
                CHECK(mask.span()<width*.9);CHECK(mask.changed_pixels<width*height*.5);
                CHECK(yellow(drop_image.rgba8,baseline.rgba8).changed_pixels>4);
                CHECK(spray_image.rgba8!=drop_image.rgba8);CHECK(current.rgba8!=spray_image.rgba8);
                CHECK(current.rgba8!=drop_image.rgba8);
            }
            if(age>.5) {CHECK_FALSE(spray_scene.transient_sprite_entities);CHECK(spray_image.rgba8==baseline.rgba8);}
            if(report && distance==128) {
                const auto stamp=std::to_string(static_cast<unsigned>(std::lround(age*1000)));
                save_frame(std::string{server_sound ? "sound-after-" : "te-after-"}+stamp,current.rgba8);
                save_frame("before-"+stamp,previous.rgba8);
                save_frame("spray-"+stamp,spray_image.rgba8);save_frame("drops-"+stamp,drop_image.rgba8);
            }
            if(report)std::cout<<"snark-ground-pixels age="<<age<<" distance="<<distance
                <<" yellow="<<visible.changed_pixels<<" span="<<visible.span()<<" signal="<<visible.signal
                <<" old_yellow="<<control.changed_pixels<<" old_span="<<control.span()
                <<" old_signal="<<control.signal<<'\n';
        }
        auto expired=scene_for(floor,distance);present(resources,owner,expired,1.6);
        CHECK(owner.sprites().empty());CHECK_FALSE(expired.transient_sprite_entities);
        CHECK(capture(expired).rgba8==baseline.rgba8);

        // Move both whole imported billboards below the floor. They still
        // materialize, but the normal static-world depth buffer must hide them.
        auto buried=batch;buried.effects[0].origin.z=-192;
        owner.reset(1);owner.consume(buried,0);
        auto occluded=scene_for(floor,distance);present(resources,owner,occluded,.4);
        REQUIRE(occluded.transient_sprite_entities);
        REQUIRE(occluded.transient_sprite_entities->frame->sprite_instances().size()==17);
        CHECK(capture(occluded).rgba8==baseline.rgba8);
        owner.reset(2);owner.consume(batch,0);
        auto reset=scene_for(floor,distance);present(resources,owner,reset,0);
        CHECK(owner.sprites().empty());CHECK(capture(reset).rgba8==baseline.rgba8);
    }
    GLboolean writes_depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&writes_depth);
    CHECK(writes_depth==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    CHECK(glGetError()==GL_NO_ERROR);gl->release_renderer();
}
}

TEST_CASE("E27 original blood composition separates animated anchored spray and moving imported droplets",
    "[e27][snark-world][bloodsprite][pop-visibility][opengl][actual-context][no-stock]") {
    t::ScopedLocalResourceTestRoot root;
    (void)wf::owned(root); // Reuse the existing independently authored BSP/WAD fixture.
    root.write("valve","sprites/bloodspray.spr",owned_blood_sprite(64,10));
    root.write("valve","sprites/blood.spr",owned_blood_sprite(16,9));
    root.write("valve","gfx/palette.lmp",t::effect_palette_fixture::bytes());
    hlclient::client::ClientWorldState world;
    auto resources=pop_assets(root.path(),"maps/test.bsp",world);
    ground_comparison(*resources,false);
}

TEST_CASE("Snark server sound pop reaches approved sprite materialization and production OpenGL pixels",
    "[e27][snark-world][sound-pop][pop-visibility][opengl][actual-context][no-stock]") {
    t::ScopedLocalResourceTestRoot root;(void)wf::owned(root);
    root.write("valve","sprites/bloodspray.spr",owned_blood_sprite(64,10));
    root.write("valve","sprites/blood.spr",owned_blood_sprite(16,9));
    root.write("valve","gfx/palette.lmp",t::effect_palette_fixture::bytes());
    hlclient::client::ClientWorldState world;auto resources=pop_assets(root.path(),"maps/test.bsp",world);
    ground_comparison(*resources,true,true);
}

TEST_CASE("E27 opt-in approved blood assets report actual ground animation and same-clock OpenGL control",
    "[e27][snark-assets][bloodsprite][pop-visibility][opengl][optional-local]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* raw=nullptr;std::size_t count{};
    if(_wdupenv_s(&raw,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && raw)root=raw;
    std::free(raw);
#endif
    if(root.lexically_normal()!=
        std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Exact approved read-only research root opt-in absent; owned ground test remains mandatory");
    hlclient::client::ClientWorldState world;
    auto resources=pop_assets(root,"maps/crossfire.bsp",world);
    ground_comparison(*resources,true);
    ground_comparison(*resources,true,true);
}
