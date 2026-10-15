#include "entity_render/entity_opengl_test_support.hpp"
#include "hud_resources_test_fixture_e29.hpp"
#include "weapon_suite_test_fixture.hpp"
#include <hlclient/app/hud_presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/hud_resource_profile.hpp>
#include <hlclient/games/halflife/sprite_hud.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <filesystem>
#include "hud_nearest_pixel_reference_e311.hpp"
#include <fstream>

namespace {
namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;
namespace hf=hlclient::games::halflife;
namespace r=hlclient::renderer;
namespace gpu=hlclient::tests::entity_opengl_fixture;
namespace resources=hlclient::tests::hud_resources_e29;
namespace weapon_fixture=hlclient::tests::weapon_suite_fixture;
namespace wire=hlclient::tests::sprite_fixture;
constexpr std::array<unsigned,3> killfeed_background{20,30,40};

g::HudPreparedAssets killfeed_owned_assets() {
    resources::FixtureSource source;
    source.add("sprites/hud.txt",
        "8\n"
        "d_egon 320 killfeed_fixture 8 8 32 16\n"
        "d_skull 320 killfeed_fixture 48 8 32 16\n"
        "d_egon 640 killfeed_fixture 8 8 32 16\n"
        "d_skull 640 killfeed_fixture 48 8 32 16\n"
        "d_egon 1280 killfeed_fixture 8 40 64 32\n"
        "d_skull 1280 killfeed_fixture 88 40 64 32\n"
        "d_egon 2560 killfeed_fixture 8 80 96 48\n"
        "d_skull 2560 killfeed_fixture 128 80 96 48\n");
    std::vector<std::byte> pixels(256U*160U,std::byte{4});
    const std::array<api::HudSourceRectangle,6> rectangles{{
        {8,8,32,16},{48,8,32,16},{8,40,64,32},{88,40,64,32},{8,80,96,48},{128,80,96,48}}};
    for(const auto& rect:rectangles)for(unsigned y=0;y<rect.height;++y)for(unsigned x=0;x<rect.width;++x) {
        const bool ink=x>0 && x+1<rect.width && y>0 && y+1<rect.height &&
            (x==rect.width/2 || y==rect.height/2 || (x+y)%4==0);
        pixels[(rect.y+y)*256U+rect.x+x]=ink ? std::byte{1} : std::byte{0};
    }
    auto bytes=wire::literal_header_and_palette(1,1,2,0,256,160);
    const std::array<std::array<unsigned,3>,5> palette{{{0,0,0},{255,255,255},
        {0,255,0},{255,200,100},{255,0,0}}};
    for(unsigned index=0;index<palette.size();++index)for(unsigned c=0;c<3;++c)
        bytes[wire::kPaletteOffset+index*3U+c]=std::byte{static_cast<std::uint8_t>(palette[index][c])};
    wire::append_single_entry(bytes,{0,160,256,160,std::move(pixels)});
    source.files["sprites/killfeed_fixture.spr"]=std::move(bytes);
    const std::array<api::HudManifestRequest,1> requests{{{"sprites/hud.txt",{}}}};
    auto prepared=g::prepare_hud_assets(source,requests);
    REQUIRE(prepared.ready_entries==8);REQUIRE(prepared.rejected_entries==0);
    REQUIRE(prepared.sprites.size()==1);return prepared;
}

api::HudDrawCommands killfeed_only(api::HudDrawCommands draw) {
    std::erase_if(draw.rectangles,[](const auto& item){return item.draw_order<100;});
    std::erase_if(draw.texts,[](const auto& item){return item.draw_order<100;});
    std::erase_if(draw.sprites,[](const auto& item){return item.draw_order<100;});
    return draw;
}
struct KillfeedModule {
    api::GameClientHost host{hf::make_half_life_client_module()};
    hlclient::client::RuntimeClientObservationState observation{weapon_fixture::state(10)};
    explicit KillfeedModule(const g::HudPreparedAssets& prepared,r::RenderExtent extent,
        std::string killer="HLC_E9_B",std::string victim="HLC_E9_A",unsigned count=1,
        api::DeathNoticeKind kind=api::DeathNoticeKind::normal,std::string weapon="egon") {
        host.reset({1,1,1});
        *observation.receiving_client->owned_weapon_bits|=1U<<31;
        observation.lifecycle.state=hlclient::client::LocalPlayerLifeState::alive;
        host.set_hud_context({static_cast<std::uint32_t>(extent.width),static_cast<std::uint32_t>(extent.height),false});
        REQUIRE(host.bind_hud_resources(prepared.resources));
        api::GameRecordState record;
        record.weapon_hud=observation.weapon_hud;record.lifecycle=observation.lifecycle;
        record.multiplayer.maximum_clients=2;
        record.multiplayer.players[0].present=true;record.multiplayer.players[0].name=killer;
        record.multiplayer.players[1].present=true;record.multiplayer.players[1].name=victim;
        for(unsigned index=0;index<count;++index) {
            api::DeathNoticeState notice;
            notice.source=weapon_fixture::source(index+1);
            notice.killer=kind==api::DeathNoticeKind::world ? 0U : 1U;notice.victim=2U;
            notice.killer_name=killer;notice.victim_name=victim;
            notice.weapon=weapon;notice.kind=kind;notice.created_seconds=0.0;
            record.multiplayer.notices[index]=std::move(notice);
        }
        host.commit_record(std::move(record));host.observe(observation,0.0);
    }
    api::HudDrawCommands draw(double at=1.0) {return killfeed_only(host.hud(observation,at).draw);}
    void commit_death_message(double received_seconds,double frame_seconds,std::uint64_t record) {
        auto next=observation;next.publication_revision=record;
        next.client_metadata.source=weapon_fixture::source(record);
        const std::array<std::byte,7> body{std::byte{1},std::byte{2},std::byte{'e'},
            std::byte{'g'},std::byte{'o'},std::byte{'n'},std::byte{0}};
        const hlclient::client::RuntimeObservationSource source{record,
            static_cast<std::size_t>(record),static_cast<std::uint32_t>(record),0,body.size()*8U};
        const api::GameMessageView message{api::GameMessageKind::user_message,"DeathMsg",body,source,1};
        auto staged=host.stage_record({&observation,next,std::span{&message,1U},1U,2U,{},received_seconds});
        INFO((staged.error ? *staged.error : ""));REQUIRE(staged);
        REQUIRE(staged.state->multiplayer.notices[0]);
        CHECK(staged.state->multiplayer.notices[0]->created_seconds==received_seconds);
        host.commit_record(std::move(*staged.state));observation=std::move(next);
        // Normal live main samples this clock before pumping the receive. A
        // committed message may be a few milliseconds later than that frame.
        host.observe(observation,frame_seconds);
    }
};
r::RenderScene killfeed_scene(const api::HudDrawCommands& commands,const g::HudPreparedAssets& prepared) {
    r::RenderScene result;result.clear_color={20.0F/255,30.0F/255,40.0F/255,1};result.basic_hud=commands;
    const auto materialized=hlclient::app::materialize_hud_sprites(result,prepared);
    CHECK(materialized.missing_resources==0);CHECK(materialized.stale_revisions==0);
    CHECK(materialized.capacity_rejections==0);CHECK(materialized.invalid_commands==0);return result;
}
auto killfeed_pixels(gpu::HiddenContext& context,const r::RenderScene& scene,r::RenderExtent extent) {
    context.renderer().render(scene,extent);
    auto image=context.renderer().observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(image);CHECK(glGetError()==GL_NO_ERROR);return image;
}
std::array<unsigned,3> killfeed_pixel(const r::opengl::OpenGlFramebufferObservation& image,
        r::RenderExtent extent,int x,int y) {
    const auto at=4U*(static_cast<std::size_t>(extent.height-1-y)*static_cast<std::size_t>(extent.width)+static_cast<std::size_t>(x));
    REQUIRE(at+2<image.rgba8.size());return {image.rgba8[at],image.rgba8[at+1],image.rgba8[at+2]};
}
void killfeed_color_close(std::array<unsigned,3> actual,std::array<unsigned,3> expected) {
    for(unsigned c=0;c<3;++c)CHECK(std::abs(static_cast<int>(actual[c])-static_cast<int>(expected[c]))<=3);
}
void save_e311_feed(std::string_view label,const r::opengl::OpenGlFramebufferObservation& image,
        r::RenderExtent extent) {
    const auto output=resources::environment_value("HLCLIENT_E311_HUD_SCREENSHOT_DIR");if(output.empty())return;
    const auto repository=std::filesystem::weakly_canonical(std::filesystem::path{__FILE__}.parent_path().parent_path());
    const auto allowed=repository/"manual-artifacts"/"task-records"/"e31-1-display-appearance-20261011";
    const auto target=std::filesystem::weakly_canonical(output);const auto relative=target.lexically_relative(allowed);
    REQUIRE(!relative.empty());REQUIRE(!relative.is_absolute());
    REQUIRE(std::none_of(relative.begin(),relative.end(),[](const auto& p){return p=="..";}));
    REQUIRE((label=="killfeed-after" || label=="killfeed-before-layout-reference"));
    std::filesystem::create_directories(target);
    std::ofstream file{target/(std::string(label)+".ppm"),std::ios::binary};REQUIRE(file);
    file<<"P6\n"<<extent.width<<' '<<extent.height<<"\n255\n";
    for(int y=0;y<extent.height;++y)for(int x=0;x<extent.width;++x) {
        const auto at=4U*(static_cast<std::size_t>(extent.height-1-y)*extent.width+x);
        const char pixel[3]{static_cast<char>(image.rgba8[at]),static_cast<char>(image.rgba8[at+1]),
            static_cast<char>(image.rgba8[at+2])};file.write(pixel,3);
    }
    REQUIRE(file.good());
}
const api::HudResourceEntry& killfeed_role(const g::HudPreparedAssets& prepared,
        std::string_view name,unsigned tier) {
    const auto base=std::find_if(prepared.resources.descriptors.begin(),prepared.resources.descriptors.end(),
        [](const auto& data){return data.name=="sprites/hud.txt";});REQUIRE(base!=prepared.resources.descriptors.end());
    const auto entry=std::find_if(base->entries.begin(),base->entries.end(),
        [&](const auto& value){return value.role==name && value.resolution==tier;});
    REQUIRE(entry!=base->entries.end());REQUIRE(entry->status==api::HudResourceStatus::ready);return *entry;
}
void killfeed_icon_proof(gpu::HiddenContext& context,const api::HudSprite& command,
        const g::HudPreparedAssets& prepared,r::RenderExtent extent) {
    api::HudDrawCommands isolated;isolated.sprites.push_back(command);
    const auto scene=killfeed_scene(isolated,prepared);REQUIRE(scene.hud_sprite_bindings.size()==1);
    const auto image=killfeed_pixels(context,scene,extent);
    const auto& asset=*scene.hud_sprite_bindings[0].asset;
    REQUIRE(command.frame<asset.frames().size());const auto& frame=asset.frames()[command.frame];
    std::size_t ink{},neutral{};
    const auto left=static_cast<int>(std::ceil(command.x-.5F)),top=static_cast<int>(std::ceil(command.y-.5F));
    const auto right=static_cast<int>(std::ceil(command.x+command.width-.5F)),bottom=static_cast<int>(std::ceil(command.y+command.height-.5F));
    for(int py=top;py<bottom;++py)for(int px=left;px<right;++px) {
        const float source_x=(px+.5F-command.x)/command.width*command.source.width;
        const float source_y=(py+.5F-command.y)/command.height*command.source.height;
        const auto shade=[&](unsigned sx,unsigned sy) {
            const auto at=4U*(static_cast<std::size_t>(command.source.y+sy)*frame.geometry.width+command.source.x+sx);
            REQUIRE(at+3<frame.rgba8.size());auto value=killfeed_background;
            const float alpha=std::to_integer<unsigned>(frame.rgba8[at+3])/255.0F*command.color[3];
            for(unsigned c=0;c<3;++c)value[c]=static_cast<unsigned>(std::lround(std::clamp(
                std::to_integer<unsigned>(frame.rgba8[at+c])*command.color[c]*alpha+killfeed_background[c],0.0F,255.0F)));
            return value;
        };
        auto expected=shade(static_cast<unsigned>(std::floor(source_x)),static_cast<unsigned>(std::floor(source_y)));
        if(px>=0 && py>=0 && px<extent.width && py<extent.height) {
            const auto actual=killfeed_pixel(image,extent,px,py);
            if(!hlclient::tests::hud_nearest_reference::close(actual,expected))
                for(const auto sx:hlclient::tests::hud_nearest_reference::candidates(source_x,command.source.width))
                    for(const auto sy:hlclient::tests::hud_nearest_reference::candidates(source_y,command.source.height)) {
                        const auto candidate=shade(sx,sy);
                        if(hlclient::tests::hud_nearest_reference::close(actual,candidate))expected=candidate;
                    }
        }
        if(expected==killfeed_background)++neutral;else ++ink;
        if(px>=0 && py>=0 && px<extent.width && py<extent.height)
            killfeed_color_close(killfeed_pixel(image,extent,px,py),expected);
    }
    CHECK(ink>0);CHECK(neutral>0);
}
void killfeed_readability_proof(gpu::HiddenContext& context,const api::HudText& text,
        const g::HudPreparedAssets& prepared,r::RenderExtent extent) {
    REQUIRE(!text.text.empty());REQUIRE(text.text[0]=='H');
    CHECK(text.pixel_size>=2.0F);CHECK(text.advance>=text.pixel_size*6.0F);
    CHECK(text.line_height>=text.pixel_size*7.0F);
    api::HudDrawCommands isolated;isolated.texts.push_back(text);
    const auto image=killfeed_pixels(context,killfeed_scene(isolated,prepared),extent);
    int minimum=extent.height,maximum=-1;
    const auto x0=std::max(0,static_cast<int>(text.x)),x1=std::min(extent.width,static_cast<int>(std::ceil(text.x+text.pixel_size*5.0F)));
    const auto y0=std::max(0,static_cast<int>(text.y)),y1=std::min(extent.height,static_cast<int>(std::ceil(text.y+text.pixel_size*7.0F)));
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)if(killfeed_pixel(image,extent,x,y)!=killfeed_background) {
        minimum=std::min(minimum,y);maximum=std::max(maximum,y);
    }
    CHECK(maximum>=minimum);CHECK(maximum-minimum+1>=static_cast<int>(std::floor(text.pixel_size*7.0F))-1);
    CHECK(maximum-minimum+1>=13); // historical1.2px glyph8.4px path must fail
}

void killfeed_viewport(gpu::HiddenContext& context,const g::HudPreparedAssets& prepared,
        r::RenderExtent extent,unsigned tier,float pixel) {
    KillfeedModule module{prepared,extent};const auto before_hash=hlclient::client::runtime_observation_canonical_hash(module.observation);
    const auto draw=module.draw();REQUIRE(draw.sprites.size()==1);REQUIRE(draw.texts.size()==2);
    const auto& icon=draw.sprites[0];const auto& entry=killfeed_role(prepared,"d_egon",tier);
    CHECK(icon.resource_id==entry.resource_id);CHECK(icon.resource_revision==entry.resource_revision);
    const float icon_scale=hf::halflife_hud_automatic_scale({static_cast<unsigned>(extent.width),static_cast<unsigned>(extent.height)},
        &prepared.resources.descriptors[0],tier);
    CHECK(icon.source==entry.source);CHECK(icon.width==entry.source.width*icon_scale);CHECK(icon.height==entry.source.height*icon_scale);
    CHECK(icon.y>=34);REQUIRE(icon.clip);CHECK(icon.clip->width==extent.width);CHECK(icon.clip->height==extent.height);
    CHECK(draw.texts[0].text=="HLC_E9_B");CHECK(draw.texts[1].text=="HLC_E9_A");
    for(const auto& text:draw.texts) {
        CHECK(text.text.find('[')==std::string::npos);CHECK(text.text.find("EGON")==std::string::npos);
        CHECK(text.pixel_size==pixel);
        CHECK(std::abs((text.y+3.5F*pixel)-(icon.y+icon.height*.5F))<.01F);
        CHECK(text.x>=0);CHECK(text.x+static_cast<float>(text.text.size())*text.advance<=extent.width);
        killfeed_readability_proof(context,text,prepared,extent);
    }
    CHECK(draw.texts[0].x+static_cast<float>(draw.texts[0].text.size())*draw.texts[0].advance<=icon.x);
    CHECK(draw.texts[1].x>=icon.x+icon.width);
    CHECK(draw.texts[1].x+static_cast<float>(draw.texts[1].text.size())*draw.texts[1].advance<=extent.width-4.0F);
    killfeed_icon_proof(context,icon,prepared,extent);
    const auto visible=killfeed_pixels(context,killfeed_scene(draw,prepared),extent);
    CHECK(visible.non_clear_pixel_count>0);
    api::HudDrawCommands empty;const auto baseline=killfeed_pixels(context,killfeed_scene(empty,prepared),extent);
    CHECK(baseline.non_clear_pixel_count==0);
    const auto expired=module.draw(7.0);CHECK(expired.sprites.empty());CHECK(expired.texts.empty());
    const auto after=killfeed_pixels(context,killfeed_scene(expired,prepared),extent);
    CHECK(after.color_signature==baseline.color_signature);
    CHECK(hlclient::client::runtime_observation_canonical_hash(module.observation)==before_hash);
}
} // namespace

TEST_CASE("E29.1 production killfeed uses source weapon icons and readable names at every HUD tier",
          "[e291][hud][killfeed][opengl][actual-context][no-stock]") {
    const auto prepared=killfeed_owned_assets();
    const std::array<std::array<int,4>,5> viewports{{
        {320,240,320,2},{640,480,640,2},{1280,720,640,2},{1920,1080,1280,2},{3440,1800,2560,3}}};
    for(const auto& viewport:viewports) {
        const r::RenderExtent extent{viewport[0],viewport[1]};INFO(extent.width);INFO(extent.height);
        auto context=gpu::try_context(extent.width,extent.height);
        if(!context || !gpu::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
        context->initialize_renderer();killfeed_viewport(*context,prepared,extent,
            static_cast<unsigned>(viewport[2]),static_cast<float>(viewport[3]));
    }
}

TEST_CASE("E31.1 killfeed limits long names independently of source icon tier and keeps notice state",
          "[e311][hud][killfeed][scale][no-stock]") {
    const auto prepared=killfeed_owned_assets();
    for(const r::RenderExtent extent:{r::RenderExtent{1280,720},{1920,1080},{2560,1440},{3440,1800}}) {
        KillfeedModule module{prepared,extent,"H"+std::string(30,'A'),"H"+std::string(30,'B'),4};
        for(const float scale:{.5F,1.0F,1.5F,2.0F}) {
            api::HudPresentationContext context{static_cast<unsigned>(extent.width),static_cast<unsigned>(extent.height)};
            context.hud_scale_automatic=false;context.hud_scale=scale;module.host.set_hud_context(context);
            const auto hud=module.host.hud(module.observation,1.0);const auto draw=killfeed_only(hud.draw);
            REQUIRE(draw.texts.size()==8);REQUIRE(draw.sprites.size()==4);CHECK(api::valid_hud_draw_commands(draw));
            CHECK(hud.killfeed_font_pixel_size==(extent.height>1440 ? 3.0F : 2.0F)*scale);
            for(unsigned row=0;row<4;++row) {
                const auto& killer=draw.texts[row*2];const auto& victim=draw.texts[row*2+1];const auto& icon=draw.sprites[row];
                CHECK(killer.text.size()<31);CHECK(victim.text.size()<31);
                CHECK(killer.text.back()=='-');CHECK(victim.text.back()=='-');
                CHECK(killer.text.starts_with('H'));CHECK(victim.text.starts_with('H'));
                CHECK(killer.x>=extent.width-4*scale-hud.killfeed_maximum_row_width-.01F);
                CHECK(victim.x+victim.text.size()*victim.advance<=extent.width-4*scale+.01F);
                CHECK(killer.x+killer.text.size()*killer.advance<=icon.x);
                CHECK(icon.x+icon.width<=victim.x);
                CHECK(std::abs(killer.y+killer.pixel_size*3.5F-icon.y-icon.height*.5F)<.01F);
                CHECK(victim.pixel_size==killer.pixel_size);CHECK(victim.advance==killer.advance);
                if(row)CHECK(killer.y>=draw.texts[(row-1)*2].y+draw.texts[(row-1)*2].line_height);
            }
            module.host.cancel_uncommitted();CHECK(module.draw(1.2)==draw);
        }
        CHECK(module.draw(6.0).sprites.empty());CHECK(module.draw(6.0).texts.empty());
    }
}

TEST_CASE("E31.1 compact1080 killfeed changes actual glyph coverage without font icon coupling",
          "[e311][hud][killfeed][opengl][actual-context][no-stock]") {
    const auto prepared=killfeed_owned_assets();const r::RenderExtent extent{1920,1080};
    auto context=gpu::try_context(extent.width,extent.height);
    if(!context || !gpu::capable_context())SKIP("OpenGL3.3 Core context unavailable");
    context->initialize_renderer();
    KillfeedModule module{prepared,extent,"H"+std::string(30,'A'),"H"+std::string(30,'B'),4};
    const auto after=module.draw();REQUIRE(after.sprites.size()==4);REQUIRE(after.texts.size()==8);
    const auto actual=killfeed_pixels(*context,killfeed_scene(after,prepared),extent);
    CHECK(actual.maximum_x<extent.width);CHECK(actual.minimum_x>=extent.width-644);
    for(const auto& text:after.texts) {CHECK(text.pixel_size==2);killfeed_readability_proof(*context,text,prepared,extent);}
    for(const auto& icon:after.sprites)killfeed_icon_proof(*context,icon,prepared,extent);
    save_e311_feed("killfeed-after",actual,extent);
    // Controlled historical command-layout reconstruction, not a claim that
    // this frame was produced by the old executable or the user's live run.
    auto historical=after;
    for(unsigned row=0;row<4;++row) {
        auto& killer=historical.texts[row*2];auto& victim=historical.texts[row*2+1];auto& icon=historical.sprites[row];
        killer.text="H"+std::string(30,'A');victim.text="H"+std::string(30,'B');
        killer.pixel_size=victim.pixel_size=4;killer.advance=victim.advance=24;
        killer.line_height=victim.line_height=32;
        icon.y=34+row*36.0F;killer.y=victim.y=icon.y+4;
        victim.x=extent.width-4.0F-victim.text.size()*victim.advance;
        icon.x=victim.x-icon.width;killer.x=icon.x-5-killer.text.size()*killer.advance;
    }
    REQUIRE(api::valid_hud_draw_commands(historical));
    const auto before=killfeed_pixels(*context,killfeed_scene(historical,prepared),extent);
    CHECK(before.minimum_x<actual.minimum_x-500);CHECK(before.non_clear_pixel_count>actual.non_clear_pixel_count*2);
    save_e311_feed("killfeed-before-layout-reference",before,extent);
    const auto expiry=module.draw(6.0);CHECK(expiry.texts.empty());CHECK(expiry.sprites.empty());
    CHECK(killfeed_pixels(*context,killfeed_scene(expiry,prepared),extent).non_clear_pixel_count==0);
}

TEST_CASE("E29.1 killfeed long names four rows and missing weapon fallback stay inside viewport",
          "[e291][hud][killfeed][opengl][actual-context][no-stock]") {
    const auto prepared=killfeed_owned_assets();const r::RenderExtent extent{640,480};
    auto context=gpu::try_context(extent.width,extent.height);
    if(!context || !gpu::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    KillfeedModule module{prepared,extent,"H"+std::string(30,'A'),"H"+std::string(30,'B'),4};
    const auto draw=module.draw();REQUIRE(draw.sprites.size()==4);REQUIRE(draw.texts.size()==8);
    CHECK(api::valid_hud_draw_commands(draw));
    for(const auto& text:draw.texts) {
        CHECK(text.text.size()<31);CHECK(text.pixel_size>=2);
        CHECK(text.x>=0);CHECK(text.y>=0);
        CHECK(text.x+static_cast<float>(text.text.size())*text.advance<=extent.width-4.0F);
        CHECK(text.y+7*text.pixel_size<=extent.height);
    }
    for(unsigned row=1;row<4;++row) {
        CHECK(draw.sprites[row].y>=draw.sprites[row-1].y+draw.sprites[row-1].height);
        CHECK(draw.texts[row*2].y>=draw.texts[(row-1)*2].y+draw.texts[(row-1)*2].line_height);
    }
    const auto image=killfeed_pixels(*context,killfeed_scene(draw,prepared),extent);
    CHECK(image.non_clear_pixel_count>0);CHECK(image.minimum_x>=0);CHECK(image.maximum_x<extent.width);
    KillfeedModule unknown{prepared,extent,"HLC_E9_B","HLC_E9_A",1,api::DeathNoticeKind::normal,"unknown_inert_weapon"};
    const auto fallback=unknown.draw();REQUIRE(fallback.sprites.size()==1);
    CHECK(fallback.sprites[0].source==killfeed_role(prepared,"d_skull",640).source);
    for(const auto& text:fallback.texts)CHECK(text.text.find('[')==std::string::npos);
    killfeed_icon_proof(*context,fallback.sprites[0],prepared,extent);
    KillfeedModule world{prepared,extent,"HLC_E9_B","HLC_E9_A",1,api::DeathNoticeKind::world,"worldspawn"};
    const auto world_draw=world.draw();REQUIRE(world_draw.sprites.size()==1);REQUIRE(world_draw.texts.size()==1);
    CHECK(world_draw.texts[0].text=="HLC_E9_A");CHECK(world_draw.sprites[0].source==killfeed_role(prepared,"d_skull",640).source);
}

TEST_CASE("E29.1 approved original Egon death icon and normal module feed produce matching GPU pixels",
          "[e291][hud][killfeed][opengl][actual-context][actual-assets]") {
    const auto basedir=resources::environment_value("HLCLIENT_E29_ASSET_ROOT");
    if(basedir.empty())SKIP("Set HLCLIENT_E29_ASSET_ROOT for explicit read-only original killfeed sprite validation");
    auto roots=hlclient::local_resources::LocalResourceSearchRoots::create(basedir,"valve");REQUIRE(roots);
    auto made=hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    g::ApprovedHudAssets worker{resources::resources_for(environment,"maps/crossfire.bsp"),hf::halflife_hud_resource_preparation()};
    const auto prepared=resources::wait_prepared(worker);REQUIRE(prepared);
    const r::RenderExtent extent{1920,1080};auto context=gpu::try_context(extent.width,extent.height);
    if(!context || !gpu::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();killfeed_viewport(*context,*prepared,extent,1280,2);
}

TEST_CASE("E29.2 committed killfeed survives an earlier cached frame and focus cancellation then expires normally",
          "[e292][hud][killfeed][opengl][actual-context][no-stock]") {
    const auto prepared=killfeed_owned_assets();const r::RenderExtent extent{640,480};
    auto context=gpu::try_context(extent.width,extent.height);
    if(!context || !gpu::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    KillfeedModule module{prepared,extent,"HLC_E9_B","HLC_E9_A",0};
    const auto before=module.draw(.9);CHECK(before.sprites.empty());CHECK(before.texts.empty());
    const auto baseline=killfeed_pixels(*context,killfeed_scene(before,prepared),extent);
    CHECK(baseline.non_clear_pixel_count==0);
    module.commit_death_message(1.1,1.0,2);
    const auto canonical=hlclient::client::runtime_observation_canonical_hash(module.observation);
    const auto fresh=module.draw(1.0);
    // The historical now<created_seconds erasure loses this just-committed
    // notice before any pixel can be submitted, independent of focus state.
    REQUIRE(fresh.sprites.size()==1);REQUIRE(fresh.texts.size()==2);
    CHECK(fresh.texts[0].text=="HLC_E9_B");CHECK(fresh.texts[1].text=="HLC_E9_A");
    CHECK(fresh.sprites[0].source==killfeed_role(prepared,"d_egon",640).source);
    killfeed_icon_proof(*context,fresh.sprites[0],prepared,extent);
    killfeed_readability_proof(*context,fresh.texts[0],prepared,extent);
    const auto visible=killfeed_pixels(*context,killfeed_scene(fresh,prepared),extent);
    CHECK(visible.color_signature!=baseline.color_signature);
    module.host.cancel_uncommitted();module.host.cancel_uncommitted();
    const auto unfocused=module.draw(1.3);
    CHECK(unfocused==fresh);
    const auto while_unfocused=killfeed_pixels(*context,killfeed_scene(unfocused,prepared),extent);
    CHECK(while_unfocused.color_signature==visible.color_signature);
    module.host.observe(module.observation,1.5); // focus-return frame has no new DeathMsg
    const auto resumed=module.draw(1.5);CHECK(resumed==fresh);
    CHECK(killfeed_pixels(*context,killfeed_scene(resumed,prepared),extent).color_signature==visible.color_signature);
    CHECK(hlclient::client::runtime_observation_canonical_hash(module.observation)==canonical);
    const auto expired=module.draw(7.2);CHECK(expired.sprites.empty());CHECK(expired.texts.empty());
    CHECK(killfeed_pixels(*context,killfeed_scene(expired,prepared),extent).color_signature==baseline.color_signature);
    module.commit_death_message(7.5,7.4,3);
    const auto subsequent=module.draw(7.4);REQUIRE(subsequent.sprites.size()==1);REQUIRE(subsequent.texts.size()==2);
    killfeed_icon_proof(*context,subsequent.sprites[0],prepared,extent);
    const auto subsequent_pixels=killfeed_pixels(*context,killfeed_scene(subsequent,prepared),extent);
    CHECK(subsequent_pixels.color_signature==visible.color_signature);
    CHECK(context->renderer().hud_statistics().texture_upload_count==1); // immutable resource was retained
    const auto second_expiry=module.draw(13.6);CHECK(second_expiry.sprites.empty());CHECK(second_expiry.texts.empty());
    CHECK(killfeed_pixels(*context,killfeed_scene(second_expiry,prepared),extent).color_signature==baseline.color_signature);
}
