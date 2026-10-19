#include "entity_render/entity_opengl_test_support.hpp"
#include "hud_resources_test_fixture_e29.hpp"
#include "weapon_suite_test_fixture.hpp"
#include "hud_nearest_pixel_reference_e311.hpp"
#include <hlclient/app/hud_presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/hud_resource_profile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
namespace api=hlclient::game_api;
namespace hf=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
namespace r=hlclient::renderer;
namespace glf=hlclient::tests::entity_opengl_fixture;
namespace resources=hlclient::tests::hud_resources_e29;
namespace weapons=hlclient::tests::weapon_suite_fixture;
namespace wire=hlclient::tests::sprite_fixture;
constexpr std::array<unsigned,3> background{20,30,40};

struct OwnedPrepared {
    std::unique_ptr<resources::FixtureSource> source{std::make_unique<resources::FixtureSource>()};
    g::HudPreparedAssets prepared;
};
OwnedPrepared owned_prepared(bool original_ammo_geometry=false) {
    OwnedPrepared result;
    const unsigned tile=original_ammo_geometry ? 64U : 32U;
    const unsigned sheet_width=tile*16U,sheet_height=tile*8U;
    std::vector<std::byte> additive(sheet_width*sheet_height,std::byte{4});
    std::vector<std::byte> masked(sheet_width*sheet_height,std::byte{255});
    unsigned ordinal=0;
    const auto append=[&](std::vector<std::string>& rows,std::string_view role,
                          unsigned width,unsigned height,bool mask=false) {
        const bool measured=original_ammo_geometry &&
            (role.starts_with("number_") || role=="ammo" || role=="ammo2");
        if(measured) {width=role.starts_with("number_") ? 40U : 48U;height=48U;}
        const unsigned x=(ordinal%16)*tile+3,y=(ordinal/16)*tile+5;
        REQUIRE(y+height<sheet_height);
        auto& pixels=mask ? masked : additive;
        for(unsigned yy=0;yy<height;++yy)for(unsigned xx=0;xx<width;++xx) {
            const bool ink=xx>0 && xx+1<width && yy>0 && yy+1<height &&
                (((xx+yy+ordinal)%3==0) || xx==width/2 || yy==height/2);
            pixels[(y+yy)*sheet_width+x+xx]=std::byte{static_cast<std::uint8_t>(
                ink ? 1+(ordinal%3) : mask ? 255 : 0)};
        }
        for(unsigned tier:{320U,640U,1280U,2560U}) {
            const unsigned divisor=measured && tier<=640U ? 2U : 1U;
            std::ostringstream row;
            row<<role<<' '<<tier<<' '<<(mask ? "hud_mask_fixture" : "hud_sheet_fixture")
               <<' '<<x<<' '<<y<<' '<<width/divisor<<' '<<height/divisor;
            rows.push_back(row.str());
        }
        ++ordinal;
    };
    const auto descriptor=[&](std::string name,const std::vector<std::string>& rows) {
        std::ostringstream text;text<<rows.size()<<'\n';
        for(const auto& row:rows)text<<row<<'\n';
        result.source->add(std::move(name),text.str());
    };
    std::vector<std::string> base;
    for(unsigned digit=0;digit<10;++digit)append(base,"number_"+std::to_string(digit),8,12);
    for(std::string_view role:{"cross","suit_empty","suit_full","flash_empty","flash_full",
            "flash_beam","selection","item_healthkit","item_battery","dmg_poison","dmg_shock"})
        append(base,role,role=="suit_empty" || role=="suit_full" ? 12U : 16U,
            role=="suit_empty" || role=="suit_full" ? 24U : 12U);
    for(unsigned group=1;group<=5;++group)append(base,"bucket"+std::to_string(group),10,10);
    descriptor("sprites/hud.txt",base);
    for(const auto& type:weapons::weapons) {
        std::vector<std::string> rows;
        append(rows,"weapon",24,12);append(rows,"weapon_s",24,12);
        if(type.ammo>=0)append(rows,"ammo",12,12);
        if(type.id==4)append(rows,"ammo2",12,12);
        if(type.id!=1 && type.id<12)append(rows,"crosshair",13,11,type.id==2);
        if(type.id==2 || type.id==3 || type.id==4 || type.id==6 || type.id==7)
            append(rows,"autoaim",15,13,type.id==2);
        if(type.id==3 || type.id==6)append(rows,"zoom",17,15);
        if(type.id==6)append(rows,"zoom_autoaim",19,17);
        descriptor(std::string(hf::halflife_hud_descriptor_name(type.command)),rows);
    }
    const auto sprite=[&](std::vector<std::byte> pixels,int format) {
        auto bytes=wire::literal_header_and_palette(1,format,2,0,sheet_width,sheet_height);
        const std::array<std::array<unsigned,3>,5> palette{{{0,0,0},{220,180,100},
            {100,200,240},{255,255,255},{255,0,0}}};
        for(unsigned index=0;index<palette.size();++index)for(unsigned c=0;c<3;++c)
            bytes[wire::kPaletteOffset+index*3+c]=std::byte{static_cast<std::uint8_t>(palette[index][c])};
        wire::append_single_entry(bytes,{0,static_cast<int>(sheet_height),static_cast<int>(sheet_width),
            static_cast<int>(sheet_height),std::move(pixels)});
        return bytes;
    };
    result.source->files["sprites/hud_sheet_fixture.spr"]=sprite(std::move(additive),1);
    result.source->files["sprites/hud_mask_fixture.spr"]=sprite(std::move(masked),3);
    for(const unsigned tier:{320U,640U,1280U,2560U}) {
        const unsigned long_side=tier==320U ? 64U : 128U,short_side=tier==320U ? 24U : 48U;
        auto pain=wire::literal_header_and_palette(4,1,2,0,long_side,long_side);
        for(unsigned index=0;index<256U;++index) {
            pain[wire::kPaletteOffset+index*3U]=std::byte{static_cast<std::uint8_t>(index)};
            pain[wire::kPaletteOffset+index*3U+1U]=std::byte{0};
            pain[wire::kPaletteOffset+index*3U+2U]=std::byte{0};
        }
        for(unsigned frame=0;frame<4U;++frame) {
            const unsigned width=frame%2U ? short_side : long_side,height=frame%2U ? long_side : short_side;
            std::vector<std::byte> pixels(width*height,std::byte{0});
            for(unsigned y=1U;y+1U<height;++y)for(unsigned x=1U;x+1U<width;++x)
                if((x+y+frame)%3U==0U)pixels[y*width+x]=std::byte{200};
            wire::append_single_entry(pain,{-static_cast<int>(width/2U),static_cast<int>(height/2U),
                static_cast<int>(width),static_cast<int>(height),std::move(pixels)});
        }
        result.source->files["sprites/"+std::to_string(tier)+"_pain.spr"]=std::move(pain);
    }
    result.prepared=g::prepare_hud_assets(*result.source,hf::halflife_hud_resource_preparation());
    REQUIRE(result.prepared.resources.descriptors.size()==15);
    REQUIRE(result.prepared.sprites.size()==2U+4U);
    REQUIRE(result.prepared.rejected_entries==0);
    return result;
}

struct Module {
    api::GameClientHost host{hf::make_half_life_client_module()};
    hlclient::client::RuntimeClientObservationState state{weapons::state()};
    api::HudPresentationContext context{640,480,false};
    std::uint64_t record{1};
    double clock{};
    explicit Module(const g::HudPreparedAssets& prepared) {
        *state.receiving_client->owned_weapon_bits|=1U<<31;
        state.weapon_hud.health=100;state.weapon_hud.armor=50;
        state.lifecycle.state=hlclient::client::LocalPlayerLifeState::alive;
        state.receiving_client->health=100;state.receiving_client->field_of_view=90;
        for(auto& type:state.weapon_hud.catalogue) {
            if(type.primary_ammo_type>=0)type.primary_maximum_ammo=200;
            if(type.secondary_ammo_type>=0)type.secondary_maximum_ammo=50;
        }
        host.reset({1,1,1});host.set_hud_context(context);
        REQUIRE(host.bind_hud_resources(prepared.resources));commit_observation();host.observe(state,clock);
    }
    void commit_observation() {
        api::GameRecordState committed;
        committed.weapon_hud=state.weapon_hud;committed.lifecycle=state.lifecycle;
        host.commit_record(std::move(committed));
    }
    api::HudState draw() {clock+=.1;return host.hud(state,clock);}
    void select(std::uint8_t id) {
        weapons::select(state,id,++record);state.receiving_client->field_of_view=90;
        commit_observation();clock+=.1;host.observe(state,clock);
    }
    void fov(double value) {
        // Deliberately retain CurWeapon source and identity.
        state.receiving_client->field_of_view=value;state.publication_revision=++record;
        state.client_metadata.source=weapons::source(record);commit_observation();clock+=.1;host.observe(state,clock);
    }
    void viewport(r::RenderExtent extent) {
        context.viewport_width=static_cast<std::uint32_t>(extent.width);
        context.viewport_height=static_cast<std::uint32_t>(extent.height);host.set_hud_context(context);
    }
    void pickup(std::uint8_t amount=9) {
        const std::array<std::byte,2> ammo{std::byte{1},std::byte{amount}};
        const std::array<std::byte,1> weapon{std::byte{7}};
        const std::string item="item_healthkit";
        std::vector<std::byte> item_bytes;
        for(char value:item)item_bytes.push_back(std::byte{static_cast<unsigned char>(value)});
        item_bytes.push_back(std::byte{0});
        ++record;
        std::size_t bit_cursor=0;
        const auto source=[&](std::size_t bytes) {
            const auto begin=bit_cursor;bit_cursor+=bytes*8+8;
            return hlclient::client::RuntimeObservationSource{record,static_cast<std::size_t>(record),1,begin,bit_cursor};
        };
        const std::array<api::GameMessageView,3> messages{{
            {api::GameMessageKind::user_message,"AmmoPickup",ammo,source(ammo.size()),1},
            {api::GameMessageKind::user_message,"WeapPickup",weapon,source(weapon.size()),2},
            {api::GameMessageKind::user_message,"ItemPickup",item_bytes,source(item_bytes.size()),3}}};
        auto staged=host.stage_record({&state,state,messages,1});
        INFO((staged.error ? *staged.error : ""));REQUIRE(staged);
        host.commit_record(std::move(*staged.state));
    }
};

const api::HudResourceEntry& role(const g::HudPreparedAssets& prepared,std::string_view descriptor,
        std::string_view name,unsigned tier) {
    const auto data=std::find_if(prepared.resources.descriptors.begin(),prepared.resources.descriptors.end(),
        [&](const auto& value){return value.name==descriptor;});REQUIRE(data!=prepared.resources.descriptors.end());
    const auto entry=std::find_if(data->entries.begin(),data->entries.end(),
        [&](const auto& value){return value.role==name && value.resolution==tier;});
    REQUIRE(entry!=data->entries.end());REQUIRE(entry->status==api::HudResourceStatus::ready);return *entry;
}
bool matches(const api::HudSprite& command,const api::HudResourceEntry& entry) {
    return command.resource_id==entry.resource_id && command.resource_revision==entry.resource_revision &&
        command.frame==entry.frame && command.source==entry.source;
}
const api::HudSprite& crosshair(const api::HudState& hud,const g::HudPreparedAssets& prepared,
        const Module& module) {
    REQUIRE(hud.crosshair_status=="submitted");
    const auto name=hf::halflife_hud_descriptor_name(weapons::entry(*module.state.weapon_hud.active_weapon_id).command);
    const auto& entry=role(prepared,name,hud.crosshair_role,hud.hud_resource_tier);
    const auto selected=std::find_if(hud.draw.sprites.begin(),hud.draw.sprites.end(),
        [&](const auto& value){return matches(value,entry) && value.draw_order==20;});
    REQUIRE(selected!=hud.draw.sprites.end());
    CHECK(std::count_if(hud.draw.sprites.begin(),hud.draw.sprites.end(),
        [](const auto& value){return value.draw_order==20;})==1);
    CHECK(selected->x==std::ceil((module.context.viewport_width-selected->width)*.5F-.5F));
    CHECK(selected->y==std::ceil((module.context.viewport_height-selected->height)*.5F-.5F));
    CHECK(std::abs(selected->x+selected->width*.5F-module.context.viewport_width*.5F)<=.5F);
    CHECK(std::abs(selected->y+selected->height*.5F-module.context.viewport_height*.5F)<=.5F);
    return *selected;
}
r::RenderScene materialize(const api::HudDrawCommands& commands,const g::HudPreparedAssets& prepared) {
    r::RenderScene scene;scene.clear_color={background[0]/255.0F,background[1]/255.0F,background[2]/255.0F,1};
    scene.basic_hud=commands;
    const auto stats=hlclient::app::materialize_hud_sprites(scene,prepared);
    CHECK(stats.missing_resources==0);CHECK(stats.stale_revisions==0);CHECK(stats.capacity_rejections==0);
    CHECK(stats.invalid_commands==0);CHECK(stats.bindings_created==scene.hud_sprite_bindings.size());
    return scene;
}
std::array<unsigned,3> rgb(const r::opengl::OpenGlFramebufferObservation& image,r::RenderExtent extent,int x,int y) {
    const auto at=4*(static_cast<std::size_t>(extent.height-1-y)*static_cast<std::size_t>(extent.width)+static_cast<std::size_t>(x));
    REQUIRE(at+2<image.rgba8.size());return {image.rgba8[at],image.rgba8[at+1],image.rgba8[at+2]};
}
auto capture(glf::HiddenContext& context,const r::RenderScene& scene,r::RenderExtent extent) {
    context.renderer().render(scene,extent);
    auto image=context.renderer().observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(image);CHECK(glGetError()==GL_NO_ERROR);return image;
}
void save(std::string_view label,const r::opengl::OpenGlFramebufferObservation& image,r::RenderExtent extent) {
    const auto current_output=resources::environment_value("HLCLIENT_E311_HUD_SCREENSHOT_DIR");
    const auto output=current_output.empty() ? resources::environment_value("HLCLIENT_E29_HUD_SCREENSHOT_DIR") : current_output;
    if(output.empty())return;
    const auto repository=std::filesystem::weakly_canonical(std::filesystem::path{__FILE__}.parent_path().parent_path());
    const auto allowed=repository/"manual-artifacts"/"task-records"/
        (current_output.empty() ? "e29-original-sprite-hud-20261010" : "e31-1-display-appearance-20261011");
    const auto target=std::filesystem::weakly_canonical(output);
    const auto relative=target.lexically_relative(allowed);
    REQUIRE(!relative.empty());REQUIRE(!relative.is_absolute());
    REQUIRE(std::none_of(relative.begin(),relative.end(),[](const auto& component){return component=="..";}));
    REQUIRE(std::all_of(label.begin(),label.end(),[](char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='_';}));
    std::filesystem::create_directories(target);
    std::ofstream file{target/(std::string(label)+"-"+std::to_string(extent.width)+"x"+std::to_string(extent.height)+".ppm"),std::ios::binary};
    REQUIRE(file);file<<"P6\n"<<extent.width<<' '<<extent.height<<"\n255\n";
    REQUIRE(image.rgba8.size()==static_cast<std::size_t>(extent.width)*static_cast<std::size_t>(extent.height)*4U);
    std::vector<char> row(static_cast<std::size_t>(extent.width)*3U);
    for(int y=0;y<extent.height;++y) {
        for(int x=0;x<extent.width;++x) {
            const auto at=4U*(static_cast<std::size_t>(extent.height-1-y)*static_cast<std::size_t>(extent.width)+static_cast<std::size_t>(x));
            for(unsigned c=0;c<3;++c)row[static_cast<std::size_t>(x)*3U+c]=static_cast<char>(image.rgba8[at+c]);
        }
        file.write(row.data(),static_cast<std::streamsize>(row.size()));
    }
    REQUIRE(file.good());
}

// Independent expected pixels use the immutable source frame and the emitted
// neutral crop/tint, not renderer-generated geometry or a "pixels changed" sum.
void prove_sprite(glf::HiddenContext& context,const api::HudSprite& command,
        const g::HudPreparedAssets& prepared,r::RenderExtent extent,
        std::optional<api::HudSprite> retired={},bool require_neutral_background=false) {
    api::HudDrawCommands isolated;isolated.sprites.push_back(command);
    const auto scene=materialize(isolated,prepared);REQUIRE(scene.hud_sprite_bindings.size()==1);
    const auto image=capture(context,scene,extent);
    const auto& asset=*scene.hud_sprite_bindings[0].asset;REQUIRE(command.frame<asset.frames().size());
    const auto& frame=asset.frames()[command.frame];
    INFO("sprite destination="<<command.x<<','<<command.y<<','<<command.width<<','<<command.height
        <<" source="<<command.source.x<<','<<command.source.y<<','<<command.source.width<<','<<command.source.height);
    int x0=static_cast<int>(std::floor(command.x))-2,y0=static_cast<int>(std::floor(command.y))-2;
    int x1=static_cast<int>(std::ceil(command.x+command.width))+2,y1=static_cast<int>(std::ceil(command.y+command.height))+2;
    if(retired) {x0=std::min(x0,static_cast<int>(std::floor(retired->x))-2);
        y0=std::min(y0,static_cast<int>(std::floor(retired->y))-2);
        x1=std::max(x1,static_cast<int>(std::ceil(retired->x+retired->width))+2);
        y1=std::max(y1,static_cast<int>(std::ceil(retired->y+retired->height))+2);}
    x0=std::clamp(x0,0,extent.width);y0=std::clamp(y0,0,extent.height);
    x1=std::clamp(x1,0,extent.width);y1=std::clamp(y1,0,extent.height);
    const int stride=std::max(1,std::max(x1-x0,y1-y0)/96);
    std::size_t expected_ink{},neutral_inside{},outside{};
    for(int y=y0;y<y1;y+=stride)for(int x=x0;x<x1;x+=stride) {
        const float local_x=(static_cast<float>(x)+.5F-command.x)/command.width;
        const float local_y=(static_cast<float>(y)+.5F-command.y)/command.height;
        auto expected=background;
        const bool inside=local_x>=0 && local_y>=0 && local_x<1 && local_y<1;
        INFO("local="<<local_x<<','<<local_y<<" texel="<<local_x*command.source.width<<','<<local_y*command.source.height);
        const auto actual=rgb(image,extent,x,y);
        if(inside) {
            const auto shade=[&](unsigned source_x,unsigned source_y) {
                const auto at=4*(static_cast<std::size_t>(command.source.y+source_y)*frame.geometry.width+command.source.x+source_x);
                REQUIRE(at+3<frame.rgba8.size());auto value=background;
                const float alpha=std::to_integer<unsigned>(frame.rgba8[at+3])/255.0F;
                const bool rejected=(command.blend==api::HudSpriteBlend::alpha_test ||
                    frame.profile==hlclient::entity_render::SpriteRenderTextureProfile::alpha_test_masked) && alpha<.5F;
                if(!rejected)for(unsigned c=0;c<3;++c) {
                    const float texel=std::to_integer<unsigned>(frame.rgba8[at+c])*command.color[c];
                    const float coverage=alpha*command.color[3];
                    const float mixed=command.blend==api::HudSpriteBlend::opaque ? texel :
                        command.blend==api::HudSpriteBlend::additive ? texel*coverage+background[c] :
                        texel*coverage+background[c]*(1-coverage);
                    value[c]=static_cast<unsigned>(std::lround(std::clamp(mixed,0.0F,255.0F)));
                }return value;
            };
            const float source_x=local_x*command.source.width,source_y=local_y*command.source.height;
            expected=shade(static_cast<unsigned>(std::floor(source_x)),static_cast<unsigned>(std::floor(source_y)));
            // Keep the original exact expectation except at an interior
            // nearest-texel boundary proven by the Debug original-SPR capture.
            if(!hlclient::tests::hud_nearest_reference::close(actual,expected))
                for(const auto sx:hlclient::tests::hud_nearest_reference::candidates(source_x,command.source.width))
                    for(const auto sy:hlclient::tests::hud_nearest_reference::candidates(source_y,command.source.height)) {
                        const auto candidate=shade(sx,sy);
                        if(hlclient::tests::hud_nearest_reference::close(actual,candidate))expected=candidate;
                    }
            if(expected==background)++neutral_inside;else ++expected_ink;
        } else ++outside;
        INFO(x);INFO(y);
        for(unsigned c=0;c<3;++c)CHECK(std::abs(static_cast<int>(actual[c])-static_cast<int>(expected[c]))<=3);
    }
    CHECK(expected_ink>0);CHECK(outside>0);
    // Background coverage is checked where the source actually has a neutral
    // region, not manufactured by a global color-key.
    CHECK(neutral_inside+expected_ink>0);
    if(require_neutral_background)CHECK(neutral_inside>0);
}

void exercise_pain(glf::HiddenContext& context,const g::HudPreparedAssets& prepared,
        r::RenderExtent extent) {
    Module module{prepared};module.viewport(extent);
    module.context.view=api::HudViewContext{{10.,20.,30.},{1.,0.,0.},{0.,-1.,0.}};
    module.host.set_hud_context(module.context);
    const auto baseline=module.draw();
    REQUIRE(std::none_of(baseline.draw.sprites.begin(),baseline.draw.sprites.end(),
        [](const auto& sprite){return sprite.draw_order==30U;}));
    const auto before=capture(context,materialize(baseline.draw,prepared),extent);

    api::GameRecordState committed;
    committed.weapon_hud=module.state.weapon_hud;committed.lifecycle=module.state.lifecycle;
    hlclient::client::RuntimeLifeEvent damage;
    damage.kind=hlclient::client::RuntimeLifeEventKind::damage;
    damage.source=weapons::source(++module.record);damage.damage_taken=10U;
    // A real committed positive Damage origin within50 units lights all four
    // directions. Nonzero coordinates avoid the original compass-clear signal.
    damage.damage_origin={11.,20.,30.};committed.life_events.push_back(damage);
    module.host.commit_record(std::move(committed));
    const auto hud=module.draw();REQUIRE(api::valid_hud_draw_commands(hud.draw));
    REQUIRE(std::count_if(hud.draw.sprites.begin(),hud.draw.sprites.end(),
        [](const auto& sprite){return sprite.draw_order==30U;})==4);
    const auto after=capture(context,materialize(hud.draw,prepared),extent);
    const unsigned pain_tier=extent.width>2560 && extent.height>1600 ? 2560U :
        extent.width>=1280 && extent.height>720 ? 1280U : extent.width>=640 ? 640U : 320U;
    constexpr std::array<std::string_view,4U> roles{"pain_front","pain_right","pain_rear","pain_left"};
    for(std::size_t frame=0U;frame<roles.size();++frame) {
        CAPTURE(frame,pain_tier,extent.width,extent.height);
        const auto& entry=role(prepared,"sprites/hud.txt",roles[frame],pain_tier);
        REQUIRE(entry.frame==frame);
        const auto arrow=std::find_if(hud.draw.sprites.begin(),hud.draw.sprites.end(),
            [&](const auto& sprite){return sprite.draw_order==30U && matches(sprite,entry);});
        REQUIRE(arrow!=hud.draw.sprites.end());CHECK(arrow->blend==api::HudSpriteBlend::additive);
        const unsigned long_side=pain_tier==320U ? 64U : 128U,short_side=pain_tier==320U ? 24U : 48U;
        const unsigned width=frame%2U ? short_side : long_side,height=frame%2U ? long_side : short_side;
        CHECK(entry.source==api::HudSourceRectangle{0U,0U,width,height});
        CHECK(entry.frame_width==width);CHECK(entry.frame_height==height);
        // Original DrawPain uses native SPR frame pixels independently of
        // health/ammo HUD scale, including the high-resolution sprite tiers.
        CHECK(arrow->width==width);CHECK(arrow->height==height);
        REQUIRE(arrow->x>=0);REQUIRE(arrow->y>=0);
        REQUIRE(arrow->x+arrow->width<=extent.width);REQUIRE(arrow->y+arrow->height<=extent.height);
        const int x0=static_cast<int>(std::floor(arrow->x)),y0=static_cast<int>(std::floor(arrow->y));
        const int x1=static_cast<int>(std::ceil(arrow->x+arrow->width)),y1=static_cast<int>(std::ceil(arrow->y+arrow->height));
        std::size_t red_increased{};bool other_channels_unchanged=true;
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
            const auto previous=rgb(before,extent,x,y),current=rgb(after,extent,x,y);
            if(current[0]>previous[0])++red_increased;
            other_channels_unchanged=other_channels_unchanged &&
                current[1]==previous[1] && current[2]==previous[2];
        }
        CHECK(red_increased>0U);CHECK(other_channels_unchanged);
    }
    CHECK(context.renderer().hud_statistics().last_invalid_commands==0);
}

void exercise(glf::HiddenContext& context,const g::HudPreparedAssets& prepared,
        r::RenderExtent extent,std::string_view prefix) {
    Module module{prepared};module.viewport(extent);
    auto glock=module.draw();CHECK(glock.draw.texts.empty());
    auto glock_cross=crosshair(glock,prepared,module);prove_sprite(context,glock_cross,prepared,extent,{},true);
    auto full=capture(context,materialize(glock.draw,prepared),extent);
    // Old HP/ARM debug-panel interior is untouched in the normal profile.
    for(int y:{10,30,60})for(int x:{12,100,300,590})CHECK(rgb(full,extent,x,y)==background);
    save(std::string(prefix)+"-glock-normal",full,extent);

    const auto& health=role(prepared,"sprites/hud.txt","cross",glock.hud_resource_tier);
    const auto health_command=std::find_if(glock.draw.sprites.begin(),glock.draw.sprites.end(),
        [&](const auto& command){return matches(command,health);});REQUIRE(health_command!=glock.draw.sprites.end());
    prove_sprite(context,*health_command,prepared,extent);
    const auto& armor=role(prepared,"sprites/hud.txt","suit_full",glock.hud_resource_tier);
    const auto fill=std::find_if(glock.draw.sprites.begin(),glock.draw.sprites.end(),[&](const auto& command){
        return command.resource_id==armor.resource_id && command.source.x==armor.source.x &&
            command.source.width==armor.source.width && command.source.y==armor.source.y+armor.source.height/2 &&
            command.source.height==armor.source.height-armor.source.height/2;});
    REQUIRE(fill!=glock.draw.sprites.end());CHECK(fill->height==fill->source.height*glock.hud_effective_scale);
    prove_sprite(context,*fill,prepared,extent);

    const auto prior_source=module.state.weapon_hud.active_source;
    module.fov(40);auto zoom_fallback=module.draw();CHECK(module.state.weapon_hud.active_source==prior_source);
    CHECK(zoom_fallback.crosshair_role=="crosshair");prove_sprite(context,crosshair(zoom_fallback,prepared,module),prepared,extent,glock_cross);
    module.select(7);auto shotgun=module.draw();auto shotgun_cross=crosshair(shotgun,prepared,module);
    prove_sprite(context,shotgun_cross,prepared,extent,glock_cross);
    save(std::string(prefix)+"-shotgun",capture(context,materialize(shotgun.draw,prepared),extent),extent);
    module.select(3);auto python_normal=module.draw();auto python_cross=crosshair(python_normal,prepared,module);
    module.fov(40);auto python_zoom=module.draw();prove_sprite(context,crosshair(python_zoom,prepared,module),prepared,extent,python_cross);
    save(std::string(prefix)+"-python-zoom",capture(context,materialize(python_zoom.draw,prepared),extent),extent);
    module.fov(90);auto python_reset=module.draw();CHECK(python_reset.crosshair_role=="crosshair");
    prove_sprite(context,crosshair(python_reset,prepared,module),prepared,extent,crosshair(python_zoom,prepared,module));
    module.select(6);auto crossbow=module.draw();auto crossbow_normal=crosshair(crossbow,prepared,module);
    module.fov(20);auto crossbow_zoom=module.draw();prove_sprite(context,crosshair(crossbow_zoom,prepared,module),prepared,extent,crossbow_normal);
    save(std::string(prefix)+"-crossbow-zoom",capture(context,materialize(crossbow_zoom.draw,prepared),extent),extent);

    module.select(4);auto mp5=module.draw();CHECK(mp5.clip==50);CHECK(mp5.primary_reserve==100);
    const auto& secondary=role(prepared,"sprites/weapon_9mmAR.txt","ammo2",mp5.hud_resource_tier);
    const auto secondary_command=std::find_if(mp5.draw.sprites.begin(),mp5.draw.sprites.end(),
        [&](const auto& command){return matches(command,secondary);});REQUIRE(secondary_command!=mp5.draw.sprites.end());
    prove_sprite(context,*secondary_command,prepared,extent);
    save(std::string(prefix)+"-mp5-secondary",capture(context,materialize(mp5.draw,prepared),extent),extent);
    module.state.weapon_hud.reserve_ammo[12]=0;auto no_secondary=module.draw();
    CHECK(std::none_of(no_secondary.draw.sprites.begin(),no_secondary.draw.sprites.end(),
        [&](const auto& command){return matches(command,secondary);}));
    module.select(9);auto no_clip=module.draw();CHECK_FALSE(no_clip.clip);CHECK(no_clip.primary_reserve==60);
    CHECK(no_clip.draw.texts.empty());save(std::string(prefix)+"-gauss-no-clip",capture(context,materialize(no_clip.draw,prepared),extent),extent);

    module.select(2);module.state.weapon_hud.health=80;module.state.weapon_hud.armor=75;
    module.commit_observation();
    const auto canonical=hlclient::client::runtime_observation_canonical_hash(module.state);
    module.clock+=.1;auto menu=module.host.hud_menu_input(module.state,{.group_digit=2,.seconds=module.clock});CHECK(menu.menu_open);
    module.clock+=.1;(void)module.host.hud_menu_input(module.state,{.group_digit=2,.seconds=module.clock});
    auto menu_hud=module.draw();CHECK(menu_hud.weapon_menu_candidate==3);CHECK(module.state.weapon_hud.active_weapon_id==2);
    CHECK(menu_hud.health==80);CHECK(menu_hud.armor==75);
    CHECK(std::any_of(menu_hud.draw.rectangles.begin(),menu_hud.draw.rectangles.end(),
        [](const auto& rectangle){return rectangle.draw_order==33 && rectangle.color[1]==1.0F;}));
    for(std::string_view digit:{"number_8","number_0","number_7","number_5"}) {
        const auto& entry=role(prepared,"sprites/hud.txt",digit,menu_hud.hud_resource_tier);
        const auto glyph=std::find_if(menu_hud.draw.sprites.begin(),menu_hud.draw.sprites.end(),[&](const auto& command){
            return matches(command,entry) && command.x<extent.width*.5F && command.y>extent.height*.75F;});
        REQUIRE(glyph!=menu_hud.draw.sprites.end());prove_sprite(context,*glyph,prepared,extent);
    }
    const auto& inactive=role(prepared,"sprites/weapon_9mmhandgun.txt","weapon",menu_hud.hud_resource_tier);
    const auto& active=role(prepared,"sprites/weapon_357.txt","weapon_s",menu_hud.hud_resource_tier);
    for(const auto* entry:{&inactive,&active}) {
        const auto icon=std::find_if(menu_hud.draw.sprites.begin(),menu_hud.draw.sprites.end(),
            [&](const auto& command){return matches(command,*entry) && command.draw_order==30;});
        REQUIRE(icon!=menu_hud.draw.sprites.end());prove_sprite(context,*icon,prepared,extent);
    }
    save(std::string(prefix)+"-weapon-menu",capture(context,materialize(menu_hud.draw,prepared),extent),extent);
    module.clock+=.1;const auto cancel=module.host.hud_menu_input(module.state,{.cancel=true,.seconds=module.clock});CHECK(cancel.cancel_consumed);
    module.pickup();auto pickups=module.draw();CHECK(pickups.pickup_icons_submitted==3);
    CHECK(hlclient::client::runtime_observation_canonical_hash(module.state)==canonical);
    save(std::string(prefix)+"-pickup-history",capture(context,materialize(pickups.draw,prepared),extent),extent);
    module.clock+=5.1;CHECK(module.draw().pickup_icons_submitted==0);
    CHECK(context.renderer().hud_statistics().last_invalid_commands==0);
}
} // namespace

TEST_CASE("E31.1 nearest HUD oracle only admits an adjacent complete texel at interior sampling ties",
          "[e311][hud][no-stock]") {
    namespace reference=hlclient::tests::hud_nearest_reference;
    CHECK((reference::candidates(10.0F,80)==std::array<std::uint32_t,2>{9,10}));
    CHECK((reference::candidates(10.001F,80)==std::array<std::uint32_t,2>{10,10}));
    CHECK((reference::candidates(9.999F,80)==std::array<std::uint32_t,2>{9,9}));
    CHECK((reference::candidates(0.0F,80)==std::array<std::uint32_t,2>{0,0}));
    CHECK((reference::candidates(79.99999F,80)==std::array<std::uint32_t,2>{79,79}));
    CHECK(reference::close({20,30,40},{23,27,43}));
    CHECK_FALSE(reference::close({20,30,40},{24,30,40}));
    CHECK_FALSE(reference::close({200,200,40},{200,30,40}));
}

TEST_CASE("E29 project-owned manifests execute the normal module materializer and GPU HUD chain",
          "[e29][hud][integration][opengl][actual-context][no-stock]") {
    auto owned=owned_prepared();const auto reads=owned.source->reads;
    auto context=glf::try_context(640,480);
    if(!context || !glf::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();exercise(*context,owned.prepared,{640,480},"owned");
    exercise_pain(*context,owned.prepared,{640,480});
    CHECK(owned.source->reads==reads); // no manifest or SPR reads in any HUD frame
}

TEST_CASE("E29 shared materializer rejects invalid missing and stale resource handles without reinterpretation",
          "[e29][hud][integration][no-stock]") {
    auto owned=owned_prepared();Module module{owned.prepared};auto hud=module.draw();
    auto good=materialize(hud.draw,owned.prepared);REQUIRE(!good.hud_sprite_bindings.empty());
    good.basic_hud->sprites[0].resource_id=0xffffeeeeULL;
    auto stats=hlclient::app::materialize_hud_sprites(good,owned.prepared);CHECK(stats.missing_resources==1);
    good.basic_hud=hud.draw;++good.basic_hud->sprites[0].resource_revision;
    stats=hlclient::app::materialize_hud_sprites(good,owned.prepared);CHECK(stats.stale_revisions==1);
    good.basic_hud=hud.draw;good.basic_hud->sprites[0].width=-1;
    stats=hlclient::app::materialize_hud_sprites(good,owned.prepared);CHECK(stats.invalid_commands==1);
    CHECK(good.hud_sprite_bindings.empty());
}

TEST_CASE("E31.1 scaled HUD production pixels keep native extent exact crop coverage and centered crosshair",
          "[e311][hud][scale][opengl][actual-context][no-stock]") {
    auto owned=owned_prepared();const auto reads=owned.source->reads;
    for(const r::RenderExtent extent:{r::RenderExtent{1280,720},{1920,1080},{2560,1440},{3440,1440}}) {
        INFO(extent.width);INFO(extent.height);
        auto context=glf::try_context(extent.width,extent.height);
        if(!context || !glf::capable_context())SKIP("OpenGL3.3 Core context unavailable");
        context->initialize_renderer();Module module{owned.prepared};module.viewport(extent);
        for(const float scale:{.5F,1.0F,1.5F,2.0F}) {
            INFO(scale);module.context.hud_scale_automatic=false;module.context.hud_scale=scale;
            module.host.set_hud_context(module.context);const auto hud=module.draw();
            REQUIRE(api::valid_hud_draw_commands(hud.draw));
            const auto reticle=crosshair(hud,owned.prepared,module);
            prove_sprite(*context,reticle,owned.prepared,extent,{},true);
            const auto& health=role(owned.prepared,"sprites/hud.txt","cross",hud.hud_resource_tier);
            const auto icon=std::find_if(hud.draw.sprites.begin(),hud.draw.sprites.end(),
                [&](const auto& item){return matches(item,health);});REQUIRE(icon!=hud.draw.sprites.end());
            prove_sprite(*context,*icon,owned.prepared,extent,{},true);
            const auto pixels=capture(*context,materialize(hud.draw,owned.prepared),extent);
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
            CHECK(viewport[0]==0);CHECK(viewport[1]==0);CHECK(viewport[2]==extent.width);CHECK(viewport[3]==extent.height);
            CHECK(pixels.rgba8.size()==static_cast<std::size_t>(extent.width)*extent.height*4U);
            if(scale==1.0F)save("e311-owned-auto",pixels,extent);
            CHECK(owned.source->reads==reads);
        }
    }
}

TEST_CASE("HUD ammo pickup pixels cannot overwrite primary or secondary readouts at1080p",
          "[e29][e311][hud][pickups][ammo-history-layout][opengl][actual-context][no-stock]") {
    // Project-owned SPR pixels through the approved resource path, with the
    // actual1280-tier40x48 digit /48x48 ammo crop geometry that regressed.
    auto owned=owned_prepared(true);const auto reads=owned.source->reads;
    const r::RenderExtent extent{1920,1080};
    auto context=glf::try_context(extent.width,extent.height);
    if(!context || !glf::capable_context())SKIP("OpenGL3.3 Core context unavailable");
    context->initialize_renderer();
    for(const auto weapon_id:{2U,4U})for(const float user_scale:{.5F,1.0F,2.0F}) {
        INFO(weapon_id);INFO(user_scale);
        Module control{owned.prepared},picked_up{owned.prepared};
        for(auto* module:{&control,&picked_up}) {
            module->viewport(extent);module->select(static_cast<std::uint8_t>(weapon_id));
            module->state.weapon_hud.reserve_ammo[1]=68;
            module->state.weapon_hud.reserve_ammo[12]=9;
            module->context.hud_scale_automatic=false;module->context.hud_scale=user_scale;
            module->host.set_hud_context(module->context);
        }
        picked_up.pickup(68); // decoded and committed AmmoPickup, not a second AmmoX
        const auto baseline=control.host.hud(control.state,2.0);
        const auto history=picked_up.host.hud(picked_up.state,2.0);
        REQUIRE(baseline.hud_resource_tier==1280);
        CHECK(baseline.hud_automatic_scale==.75F);
        CHECK(history.clip==(weapon_id==2 ? 17 : 50));CHECK(history.primary_reserve==68);
        REQUIRE(history.pickup_icons_submitted==3);
        REQUIRE(history.draw.sprites.size()==baseline.draw.sprites.size()+5U);
        REQUIRE(std::equal(baseline.draw.sprites.begin(),baseline.draw.sprites.end(),history.draw.sprites.begin()));
        const auto before=capture(*context,materialize(baseline.draw,owned.prepared),extent);
        const auto after=capture(*context,materialize(history.draw,owned.prepared),extent);
        const std::size_t ammo_count=weapon_id==2 ? 5U : 7U;
        REQUIRE(baseline.draw.sprites.size()>=ammo_count);
        float left=static_cast<float>(extent.width),top=static_cast<float>(extent.height);
        for(std::size_t i=baseline.draw.sprites.size()-ammo_count;i<baseline.draw.sprites.size();++i) {
            left=std::min(left,baseline.draw.sprites[i].x);
            top=std::min(top,baseline.draw.sprites[i].y);
        }
        // Every pixel covering the normal clip/reserve/secondary panel must
        // stay bit-identical; additive overlap fails even if digits still show.
        std::size_t overwritten{};
        for(int y=std::max(0,static_cast<int>(std::floor(top)));y<extent.height;++y)
            for(int x=std::max(0,static_cast<int>(std::floor(left)));x<extent.width;++x)
                if(rgb(before,extent,x,y)!=rgb(after,extent,x,y))++overwritten;
        CHECK(overwritten==0);
        // History remains visible, separated, bounded and expiring. It is not
        // "fixed" by suppressing the pickup message or its number/icon.
        std::size_t history_ink{};
        for(std::size_t i=baseline.draw.sprites.size();i<history.draw.sprites.size();++i) {
            const auto& sprite=history.draw.sprites[i];
            CHECK(sprite.y+sprite.height<top);
            const int x0=std::max(0,static_cast<int>(std::floor(sprite.x)));
            const int x1=std::min(extent.width,static_cast<int>(std::ceil(sprite.x+sprite.width)));
            const int y0=std::max(0,static_cast<int>(std::floor(sprite.y)));
            const int y1=std::min(extent.height,static_cast<int>(std::ceil(sprite.y+sprite.height)));
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)
                if(rgb(before,extent,x,y)!=rgb(after,extent,x,y))++history_ink;
        }
        CHECK(history_ink>0);
        const auto expired=picked_up.host.hud(picked_up.state,7.1);
        const auto settled=control.host.hud(control.state,7.1);
        CHECK(expired.pickup_icons_submitted==0);
        CHECK(capture(*context,materialize(expired.draw,owned.prepared),extent).rgba8==
            capture(*context,materialize(settled.draw,owned.prepared),extent).rgba8);
        CHECK(owned.source->reads==reads);
    }
}

TEST_CASE("E29 approved original SPR HUD integrates pixels state transitions and viewport layouts",
          "[e29][hud][integration][opengl][actual-context][actual-assets]") {
    const auto basedir=resources::environment_value("HLCLIENT_E29_ASSET_ROOT");
    if(basedir.empty())SKIP("Set HLCLIENT_E29_ASSET_ROOT for explicit read-only original HUD asset validation");
    auto roots=hlclient::local_resources::LocalResourceSearchRoots::create(basedir,"valve");REQUIRE(roots);
    auto made=hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    g::ApprovedHudAssets worker{resources::resources_for(environment,"maps/crossfire.bsp"),hf::halflife_hud_resource_preparation()};
    const auto prepared=resources::wait_prepared(worker);REQUIRE(prepared);
    REQUIRE(prepared->sprites.size()==224U+4U);REQUIRE(prepared->resources.descriptors.size()==15);
    for(const r::RenderExtent extent:{r::RenderExtent{640,480},{1280,720},{1920,1080},{3440,1440},{3440,1800}}) {
        INFO(extent.width);INFO(extent.height);
        auto context=glf::try_context(extent.width,extent.height);
        if(!context || !glf::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
        context->initialize_renderer();exercise(*context,*prepared,extent,"original");
        exercise_pain(*context,*prepared,extent);
    }
}
