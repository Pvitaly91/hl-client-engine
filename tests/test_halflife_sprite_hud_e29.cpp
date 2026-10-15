#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/sprite_hud.hpp>
#include <hlclient/games/halflife/hud_resource_profile.hpp>
#include <hlclient/games/halflife/multiplayer.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "weapon_suite_test_fixture.hpp"
#include <limits>

namespace {
namespace api=hlclient::game_api;
namespace client=hlclient::client;
namespace hf=hlclient::games::halflife;
namespace f=hlclient::tests::weapon_suite_fixture;
api::HudResourceSet fixture() {
    api::HudResourceSet resources;resources.revision=1;
    std::uint64_t next=1;
    const auto append=[&](api::HudResourceDescriptor& descriptor,std::string role,
        std::uint32_t resolution,std::uint32_t width=12U,std::uint32_t height=12U) {
        api::HudResourceEntry entry;entry.role=std::move(role);entry.resolution=resolution;
        entry.resource_id=next++;entry.resource_revision=1;entry.source={5U,7U,width,height};
        entry.frame_width=512;entry.frame_height=512;entry.status=api::HudResourceStatus::ready;
        entry.sprite_name="sprites/project_owned.spr";descriptor.entries.push_back(std::move(entry));
    };
    api::HudResourceDescriptor base;base.name="sprites/hud.txt";base.status=api::HudResourceStatus::ready;
    for(const auto resolution:{320U,640U,1280U,2560U}) {
        for(char digit='0';digit<='9';++digit)append(base,"number_"+std::string(1,digit),resolution,8,12);
        for(const auto role:{"cross","suit_empty","suit_full","flash_empty","flash_full","flash_beam",
            "selection","item_healthkit","item_battery","dmg_poison","dmg_shock","item_test"})append(base,role,resolution);
        for(unsigned group=1;group<=5;++group)append(base,"bucket"+std::to_string(group),resolution,10,10);
    }resources.descriptors.push_back(std::move(base));
    for(const auto& weapon:f::weapons) {
        api::HudResourceDescriptor descriptor;descriptor.name=std::string(hf::halflife_hud_descriptor_name(weapon.command));
        descriptor.status=api::HudResourceStatus::ready;
        for(const auto resolution:{320U,640U,1280U,2560U}) {
            append(descriptor,"weapon",resolution,60,20);append(descriptor,"weapon_s",resolution,60,20);
            if(weapon.ammo>=0)append(descriptor,"ammo",resolution);
            if(weapon.id==4)append(descriptor,"ammo2",resolution);
            if(weapon.id!=1 && weapon.id<12)append(descriptor,"crosshair",resolution,13,11);
            if(weapon.id==2 || weapon.id==3 || weapon.id==4 || weapon.id==6 || weapon.id==7)append(descriptor,"autoaim",resolution,15,13);
            if(weapon.id==3 || weapon.id==6)append(descriptor,"zoom",resolution,17,15);
            if(weapon.id==6)append(descriptor,"zoom_autoaim",resolution,19,17);
        }resources.descriptors.push_back(std::move(descriptor));
    }return resources;
}
struct HudModule final {
    api::GameClientHost host{hf::make_half_life_client_module()};
    client::RuntimeClientObservationState state{f::state()};
    api::HudPresentationContext context{640,480,false};
    HudModule() {
        *state.receiving_client->owned_weapon_bits|=1U<<31U;
        host.reset({1,1,1});host.set_hud_context(context);host.observe(state,0);
    }
    void bind() { REQUIRE(host.bind_hud_resources(fixture())); }
    api::HudState draw(double at=1.0) {return host.hud(state,at);}
    api::HudMenuResult input(api::HudMenuInput intent) {return host.hud_menu_input(state,intent);}
    void context_update() {host.set_hud_context(context);}
};
bool is_crosshair(const api::HudState& hud,const api::HudSprite& sprite) {
    return sprite.resource_id==hud.crosshair_resource_id;
}
api::HudResourceSet measured_tier_fixture() {
    auto resources=fixture();
    // Project-owned pixels/metadata with the measured original24/48/72 digit
    // height relation. Resource tier and source density are not one number.
    for(auto& descriptor:resources.descriptors)for(auto& entry:descriptor.entries) {
        const unsigned factor=entry.resolution>=2560 ? 3 : entry.resolution>=1280 ? 2 : 1;
        entry.source.width*=factor;entry.source.height*=factor;
    }
    return resources;
}
api::HudResourceSet measured_ammo_history_fixture() {
    auto resources=fixture();
    // Crop dimensions from the approved E29 coverage, without game pixels.
    // In particular1280 uses40x48 digits and48x48 ammo icons, not the
    // miniature equal-size12px fixture that concealed the history overlap.
    for(auto& descriptor:resources.descriptors)for(auto& entry:descriptor.entries) {
        const auto tier=entry.resolution;
        const unsigned factor=tier>=2560 ? 3U : tier>=1280 ? 2U : 1U;
        if(entry.role.starts_with("number_")) {
            entry.source.width=tier==320 ? 12U : 20U*factor;
            entry.source.height=tier==320 ? 16U : 24U*factor;
        } else if(entry.role=="ammo" || entry.role=="ammo2") {
            entry.source.width=entry.source.height=tier==320 ? 18U : 24U*factor;
        } else if(entry.role=="weapon" || entry.role=="weapon_s") {
            entry.source.width=tier==320 ? 80U : 170U*factor;
            entry.source.height=tier==320 ? 20U : 45U*factor;
        }
        entry.frame_width=1024;entry.frame_height=1024;
    }
    return resources;
}
}

TEST_CASE("HUD pickup history stays separate from clip reserve secondary and mixed-height rows",
    "[e29][e311][hud][pickups][ammo-history-layout][no-stock]") {
    for(const auto extent:std::array<std::array<unsigned,2>,4>{{
        {640,480},{1280,720},{1920,1080},{3440,1800}}})
    for(const auto weapon_id:{2U,4U})for(const float user_scale:{.5F,1.0F,2.0F}) {
        INFO(extent[0]);INFO(extent[1]);INFO(weapon_id);INFO(user_scale);
        HudModule m;REQUIRE(m.host.bind_hud_resources(measured_ammo_history_fixture()));
        m.context.viewport_width=extent[0];m.context.viewport_height=extent[1];
        m.context.hud_scale_automatic=false;m.context.hud_scale=user_scale;m.context_update();
        f::select(m.state,static_cast<std::uint8_t>(weapon_id),2);
        m.state.weapon_hud.reserve_ammo[1]=68;m.state.weapon_hud.reserve_ammo[12]=9;
        m.host.observe(m.state,.1);
        const auto baseline=m.draw(1);
        const auto canonical=client::runtime_observation_canonical_hash(m.state);
        api::GameRecordState record;record.weapon_hud=m.state.weapon_hud;record.lifecycle=m.state.lifecycle;
        record.pickups.push_back({api::GameRecordState::Pickup::Kind::ammunition,1,68,{},f::source(3)});
        record.pickups.push_back({api::GameRecordState::Pickup::Kind::weapon,7,{}, {},f::source(3)});
        record.pickups.push_back({api::GameRecordState::Pickup::Kind::ammunition,12,9,{},f::source(3)});
        m.host.commit_record(std::move(record));
        const auto hud=m.draw(1);
        CHECK(hud.clip==(weapon_id==2 ? 17 : 50));CHECK(hud.primary_reserve==68);
        CHECK(hud.pickup_icons_submitted==3);
        REQUIRE(hud.draw.sprites.size()==baseline.draw.sprites.size()+6U);
        CHECK(api::valid_hud_draw_commands(hud.draw));
        // The actual reserve, magazine and MP5 ammo2 commands remain identical.
        CHECK(std::equal(baseline.draw.sprites.begin(),baseline.draw.sprites.end(),hud.draw.sprites.begin()));
        CHECK(hud.draw.rectangles==baseline.draw.rectangles);
        float readout_top=static_cast<float>(extent[1]);
        const std::size_t readout_sprites=weapon_id==2 ? 5U : 7U;
        REQUIRE(baseline.draw.sprites.size()>=readout_sprites);
        for(std::size_t i=baseline.draw.sprites.size()-readout_sprites;i<baseline.draw.sprites.size();++i)
            readout_top=std::min(readout_top,baseline.draw.sprites[i].y);
        float previous_top=readout_top;
        auto cursor=baseline.draw.sprites.size();
        for(const auto row_size:{3U,1U,2U}) {
            float top=static_cast<float>(extent[1]),bottom=0;
            for(unsigned i=0;i<row_size;++i) {
                const auto& sprite=hud.draw.sprites[cursor++];
                top=std::min(top,sprite.y);bottom=std::max(bottom,sprite.y+sprite.height);
            }
            CHECK(bottom+hud.hud_effective_scale*5.0F<=previous_top+.001F);
            previous_top=top;
        }
        CHECK(client::runtime_observation_canonical_hash(m.state)==canonical);
        CHECK(m.draw(6).pickup_icons_submitted==0); // no hidden permanent copy
    }
}

TEST_CASE("E31.1 HUD separates source tier compact reference user scale and physical pixels",
    "[e311][hud][scale][no-stock]") {
    const auto resources=measured_tier_fixture();
    for(const auto [width,height,tier]:std::array<std::array<unsigned,3>,5>{{
        {1280,720,640},{1920,1080,1280},{2560,1440,1280},{3440,1440,1280},{3440,1800,2560}}}) {
        HudModule m;REQUIRE(m.host.bind_hud_resources(resources));
        m.context.viewport_width=width;m.context.viewport_height=height;m.context_update();
        const auto native=m.draw();REQUIRE(native.hud_resource_tier==tier);
        CHECK(native.hud_canvas_width==width);CHECK(native.hud_canvas_height==height);
        const float expected=tier==640 ? 1.0F :
            std::min(2.0F,height/720.0F)/(tier==2560 ? 3.0F : 2.0F);
        CHECK(native.hud_automatic_scale==Catch::Approx(expected));
        CHECK(native.hud_user_scale==1.0F);CHECK(native.hud_effective_scale==Catch::Approx(expected));
        CHECK(api::valid_hud_draw_commands(native.draw));
        const auto canonical=client::runtime_observation_canonical_hash(m.state);
        for(const float multiplier:{.5F,1.0F,1.25F,1.5F,2.0F}) {
            m.context.hud_scale_automatic=false;m.context.hud_scale=multiplier;m.context_update();
            const auto scaled=m.draw();CHECK(scaled.hud_resource_tier==tier);
            CHECK(scaled.hud_effective_scale==Catch::Approx(expected*multiplier));
            REQUIRE(scaled.draw.sprites.size()==native.draw.sprites.size());
            for(std::size_t i=0;i<scaled.draw.sprites.size();++i) {
                const auto& item=scaled.draw.sprites[i];const auto& before=native.draw.sprites[i];
                CHECK(item.source==before.source);CHECK(item.resource_id==before.resource_id);
                CHECK(item.width==Catch::Approx(before.width*multiplier));
                CHECK(item.height==Catch::Approx(before.height*multiplier));
                if(is_crosshair(scaled,item)) {
                    CHECK(std::abs(item.x+item.width*.5F-width*.5F)<=.5F);
                    CHECK(std::abs(item.y+item.height*.5F-height*.5F)<=.5F);
                }
            }
            CHECK(api::valid_hud_draw_commands(scaled.draw));
            CHECK(client::runtime_observation_canonical_hash(m.state)==canonical);
        }
    }
}

TEST_CASE("E31.1 identical physical framebuffer HUD is independent of logical DPI and window mode fixtures",
    "[e311][hud][scale][no-stock]") {
    HudModule m;REQUIRE(m.host.bind_hud_resources(measured_tier_fixture()));
    m.context.viewport_width=1920;m.context.viewport_height=1080;m.context_update();
    const auto expected=m.draw().draw;
    for(const float dpi:{1.0F,1.25F,1.5F,2.0F})for(const auto mode:{"windowed","fullscreen","borderless"}) {
        INFO(dpi);INFO(mode);
        const auto logical_width=1920.0F/dpi,logical_height=1080.0F/dpi;
        // Installed SDL3's GetWindowSizeInPixels boundary yields these physical
        // dimensions. These are conversion fixtures, not live OS-mode claims.
        m.context.viewport_width=static_cast<unsigned>(std::lround(logical_width*dpi));
        m.context.viewport_height=static_cast<unsigned>(std::lround(logical_height*dpi));
        m.context_update();CHECK(m.draw().draw==expected);
    }
    m.context.hud_scale_automatic=false;m.context.hud_scale=1.0F;m.context_update();
    CHECK(m.draw().draw==expected);
    for(float invalid:{0.0F,-1.0F,3.0F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        m.context.hud_scale=invalid;m.context_update();CHECK(m.draw().draw==expected);
    }
}

TEST_CASE("E31.1 scaled HUD preserves menu confirmation consumption and server state",
    "[e311][hud][input][no-stock]") {
    HudModule m;REQUIRE(m.host.bind_hud_resources(measured_tier_fixture()));
    m.context.viewport_width=1920;m.context.viewport_height=1080;
    m.context.hud_scale_automatic=false;m.context.hud_scale=1.5F;m.context_update();
    const auto canonical=client::runtime_observation_canonical_hash(m.state);
    CHECK(m.input({.group_digit=2,.seconds=1.0}).menu_open);
    const auto selected=m.input({.primary_pressed=true,.primary_held=true,.seconds=1.1});
    CHECK(selected.consume_primary);REQUIRE(selected.selection);
    CHECK(m.input({.primary_held=true,.seconds=1.2}).consume_primary);
    CHECK_FALSE(m.input({.primary_released=true,.seconds=1.3}).consume_primary);
    CHECK_FALSE(m.input({.primary_pressed=true,.primary_held=true,.seconds=1.4}).consume_primary);
    CHECK(client::runtime_observation_canonical_hash(m.state)==canonical);
}

TEST_CASE("E29 historically absent crosshair path is resolved only by a ready weapon descriptor",
    "[e29][hud][game-module]") {
    HudModule m;auto before=m.draw();
    CHECK(before.crosshair_status=="not_yet_resolved");CHECK(before.draw.sprites.empty());
    CHECK(before.draw.texts.empty());CHECK(before.draw.rectangles.empty()); // no implicit debug panel
    m.bind();const auto after=m.draw(1.1);CHECK(after.crosshair_status=="submitted");
    const auto found=std::find_if(after.draw.sprites.begin(),after.draw.sprites.end(),[&](const auto& sprite){return is_crosshair(after,sprite);});
    REQUIRE(found!=after.draw.sprites.end());CHECK(found->source==api::HudSourceRectangle{5,7,13,11});
    CHECK(found->x==313);CHECK(found->y==234);CHECK(found->width==13);CHECK(found->height==11);
    CHECK(api::valid_hud_draw_commands(after.draw));
}
TEST_CASE("E29 every weapon fixture selects only descriptor normal zoom and on-target variants",
    "[e29][hud][crosshair]") {
    const auto id=GENERATE(1,2,3,4,6,7,8,9,10,11,12,13,14,15);
    const auto zoom=GENERATE(false,true);const auto target=GENERATE(false,true);
    HudModule m;m.bind();f::select(m.state,static_cast<std::uint8_t>(id),2);
    m.state.weapon_hud.active_on_target=target;m.state.receiving_client->field_of_view=zoom ? 40.0 : 90.0;
    m.host.observe(m.state,.1);const auto before=client::runtime_observation_canonical_hash(m.state);
    const auto hud=m.draw(.2);
    if(id==1 || id>=12) {CHECK(hud.crosshair_status=="absent_by_asset_contract");CHECK(hud.crosshair_resource_id==0);}
    else {
        CHECK(hud.crosshair_status=="submitted");
        const bool has_autoaim=id==2 || id==3 || id==4 || id==6 || id==7;
        const std::string role=zoom && id==6 && target ? "zoom_autoaim" :
            zoom && (id==3 || id==6) ? "zoom" : target && !zoom && has_autoaim ? "autoaim" : "crosshair";
        CHECK(hud.crosshair_role==role);
    }
    CHECK(client::runtime_observation_canonical_hash(m.state)==before);
}
TEST_CASE("E29 crosshair handles FOV-only resource arrival visibility lifecycle and tier thresholds",
    "[e29][hud][crosshair]") {
    HudModule m;m.bind();f::select(m.state,6,2);m.host.observe(m.state,.1);
    CHECK(m.draw(.2).crosshair_role=="crosshair");
    m.state.receiving_client->field_of_view=20.0;m.state.client_metadata.source=f::source(3);
    m.state.publication_revision=3;m.host.observe(m.state,.3);CHECK(m.draw(.4).crosshair_role=="zoom");
    m.context.crosshair=false;m.context_update();CHECK(m.draw(.5).crosshair_status=="hidden_by_policy");
    m.context.crosshair=true;m.context_update();m.state.weapon_hud.hide_flags=1;
    CHECK(m.draw(.6).crosshair_status=="hidden_by_policy");
    m.state.weapon_hud.hide_flags=0;m.state.lifecycle.state=client::LocalPlayerLifeState::dead;
    CHECK(m.draw(.7).crosshair_status=="hidden_by_policy");
    m.state.lifecycle.state=client::LocalPlayerLifeState::alive;++m.state.lifecycle.life_epoch;
    m.state.publication_revision=4;m.state.client_metadata.source=f::source(4);
    m.host.observe(m.state,.8);CHECK(m.draw(.9).crosshair_role=="crosshair");
    for(const auto [width,height,tier]:std::array<std::array<unsigned,3>,5>{{
        {639,480,320},{640,480,640},{1280,720,640},{1920,1080,1280},{3440,1800,2560}}}) {
        m.context.viewport_width=width;m.context.viewport_height=height;m.context_update();
        const auto hud=m.draw(1);CHECK(hud.hud_resource_tier==tier);CHECK(api::valid_hud_draw_commands(hud.draw));
    }
}
TEST_CASE("E29 missing optional variants fallback but invalid or absent normal roles do not invent a crosshair",
    "[e29][hud][crosshair]") {
    HudModule m;auto resources=fixture();auto& descriptor=resources.descriptors[3]; // Python
    std::erase_if(descriptor.entries,[](const auto& e){return e.role=="zoom" || e.role=="zoom_autoaim";});
    REQUIRE(m.host.bind_hud_resources(resources));f::select(m.state,3,2);
    m.state.weapon_hud.active_on_target=true;m.state.receiving_client->field_of_view=40;m.host.observe(m.state,.1);
    CHECK(m.draw(.2).crosshair_role=="crosshair");
    auto& glock=resources.descriptors[2];for(auto& entry:glock.entries)if(entry.role=="crosshair")entry.status=api::HudResourceStatus::missing_file;
    REQUIRE(m.host.bind_hud_resources(resources));f::select(m.state,2,3);m.state.weapon_hud.active_on_target=false;m.host.observe(m.state,.3);
    CHECK(m.draw(.4).crosshair_status=="missing_file");CHECK(m.draw(.4).crosshair_resource_id==0);
}
TEST_CASE("E29 health armor ammo no-clip and flashlight use original source crops without counter writes",
    "[e29][hud][authority]") {
    HudModule m;m.bind();m.state.weapon_hud.health=80;m.state.weapon_hud.armor=50;
    m.state.weapon_hud.flashlight_on=true;m.state.weapon_hud.flashlight_battery=50;
    const auto original=client::runtime_observation_canonical_hash(m.state);const auto hud=m.draw();
    CHECK(hud.health==80);CHECK(hud.armor==50);CHECK(hud.clip==17);CHECK(hud.primary_reserve==100);
    const auto suit=std::find_if(hud.draw.sprites.begin(),hud.draw.sprites.end(),[](const auto& sprite){return sprite.source.height==6 && sprite.source.y==13;});
    REQUIRE(suit!=hud.draw.sprites.end());CHECK(suit->height==6); // cropped fill, not compressed12px icon
    CHECK(client::runtime_observation_canonical_hash(m.state)==original);
    f::select(m.state,9,2);m.host.observe(m.state,.1);const auto no_clip=m.draw(1.1);
    CHECK_FALSE(no_clip.clip);CHECK(no_clip.primary_reserve==60);CHECK(no_clip.draw.texts.empty());
    m.context.hud_draw=false;m.context_update();CHECK(m.draw(1.2).draw.sprites.empty());
}
TEST_CASE("E29 menu candidate selection confirmation cancellation and fastswitch remain distinct from active weapon",
    "[e29][hud][input][inventory]") {
    HudModule m;m.bind();auto open=m.input({.group_digit=2,.seconds=.1});
    CHECK(open.menu_open);CHECK_FALSE(open.selection);CHECK(m.state.weapon_hud.active_weapon_id==2);
    CHECK(m.draw(.2).weapon_menu_candidate==2);
    (void)m.input({.group_digit=2,.seconds=.3});CHECK(m.draw(.4).weapon_menu_candidate==3);
    const auto confirm=m.input({.primary_pressed=true,.primary_held=true,.seconds=.5});
    CHECK(confirm.selection==3);CHECK(confirm.consume_primary);CHECK_FALSE(confirm.menu_open);
    CHECK(m.state.weapon_hud.active_weapon_id==2);CHECK(m.draw(.6).crosshair_role=="crosshair");
    CHECK(m.input({.primary_held=true,.seconds=.7}).consume_primary); // The UI-spent physical press remains suppressed until KeyUp.
    CHECK_FALSE(m.input({.primary_released=true,.seconds=.75}).consume_primary);
    (void)m.input({.cycle_direction=-1,.seconds=.8});CHECK(m.draw(.9).weapon_menu_candidate==1);
    const auto cancel=m.input({.cancel=true,.seconds=1});CHECK(cancel.cancel_consumed);CHECK_FALSE(cancel.menu_open);
    CHECK_FALSE(m.input({.cancel=true,.seconds=1.1}).cancel_consumed); // outside menu remains release-mouse
    m.context.fastswitch=true;m.context_update();const auto fast=m.input({.group_digit=1,.seconds=1.2});
    CHECK(fast.selection==1);CHECK_FALSE(fast.menu_open);
    CHECK(m.input({.group_digit=2,.seconds=1.3}).menu_open); // two weapons is never arbitrary instant switch
    (void)m.input({.cancel=true,.seconds=1.4});
    const auto cues=m.host.drain_audio();REQUIRE(cues.count>0);
    for(std::size_t i=0;i<cues.count;++i)if(cues.cues[i].kind==api::LocalSoundKind::hud_menu) {
        CHECK(hlclient::goldsrc::valid_local_sound_reference(cues.cues[i].reference));
        CHECK_FALSE(cues.cues[i].world_origin);CHECK(cues.cues[i].attenuation==0);
    }
    CHECK(m.host.drain_audio().count==0); // repeated render/drain never repeats UI sound
}
TEST_CASE("E29 empty buckets candidate removal and ammo policies preserve server ownership",
    "[e29][hud][input][inventory]") {
    HudModule m;m.bind();*m.state.receiving_client->owned_weapon_bits=(1U<<31)|(1U<<2);
    auto empty=m.input({.group_digit=5,.seconds=.1});CHECK(empty.menu_open);CHECK_FALSE(empty.selection);
    CHECK_FALSE(m.draw(.2).weapon_menu_candidate);
    auto dismiss=m.input({.primary_pressed=true,.seconds=.3});CHECK(dismiss.consume_primary);CHECK_FALSE(dismiss.selection);
    (void)m.input({.group_digit=2,.seconds=.4});*m.state.receiving_client->owned_weapon_bits=1U<<31;
    auto vanished=m.input({.primary_pressed=true,.seconds=.5});CHECK_FALSE(vanished.selection);CHECK(vanished.consume_primary);
    CHECK_FALSE(m.draw(.6).weapon_selection_pending);
    auto state=f::state(11);state.weapon_hud.reserve_ammo[7]=0;
    CHECK(hf::HalfLifeSpriteHud::has_ammo(state,*std::find_if(state.weapon_hud.catalogue.begin(),state.weapon_hud.catalogue.end(),[](const auto& w){return w.id==11;})));
    state=f::state(2);state.weapon_hud.reserve_ammo[1]=0;state.weapon_hud.clips[2]=0;
    CHECK_FALSE(hf::HalfLifeSpriteHud::has_ammo(state,state.weapon_hud.catalogue[1]));
}
TEST_CASE("E29.1 menu confirmation owns the full physical press before weapon visuals and audio",
    "[e291][e29][hud][input][inventory][weapon-suite]") {
    namespace gi=hlclient::gameplay_input;
    namespace protocol=hlclient::goldsrc;
    const auto primary=gi::gameplay_button_mask(gi::GameplayButton::attack_primary);
    const auto forward=gi::gameplay_button_mask(gi::GameplayButton::move_forward);
    for(const auto target:{2U,3U,4U,9U,10U,12U,13U,15U}) {
        CAPTURE(target);
        f::Module m;*m.observed.receiving_client->owned_weapon_bits|=1U<<31U;m.observe(0);
        m.host.set_hud_context({640,480,false});REQUIRE(m.host.bind_hud_resources(fixture()));
        const auto digit=static_cast<std::uint8_t>(f::entry(static_cast<std::uint8_t>(target)).slot+1U);
        for(unsigned step=0;step<14U;++step) {
            (void)m.host.hud_menu_input(m.observed,{.group_digit=digit,.seconds=.01*step});
            if(m.host.hud(m.observed,.01*step).weapon_menu_candidate==target)break;
        }
        REQUIRE(m.host.hud(m.observed,.09).weapon_menu_candidate==target);
        const auto selected=m.host.hud_menu_input(m.observed,
            {.primary_pressed=true,.primary_held=true,.seconds=.1});
        REQUIRE(selected.selection==target);REQUIRE(selected.consume_primary);
        m.host.hud_selection_result(static_cast<std::uint8_t>(target),true,.1);
        m.adopt(static_cast<std::uint8_t>(target),.11,2);(void)m.host.drain_audio();
        protocol::LiveVisualButtonLatch latch;std::uint32_t sequence=0;
        for(const auto at:{.12,.3,.6}) {
            const auto ui=m.host.hud_menu_input(m.observed,{.primary_held=true,.seconds=at});
            REQUIRE(ui.consume_primary);CHECK_FALSE(ui.selection);CHECK_FALSE(ui.menu_open);
            protocol::LiveVisualControlInput input;input.held_buttons=primary|forward;
            input.pressed_buttons=sequence==0U ? primary : 0U;
            input.consumed_buttons=ui.consume_primary ? primary : 0U;
            latch.observe(input.pressed_buttons,true,primary);
            protocol::consume_live_visual_input_buttons(input,latch);
            CHECK(input.held_buttons==forward);CHECK((latch.pending()&primary)==0U);
            // Normal application submits only the translated, unconsumed
            // attack state to this same production weapon/effect composition.
            m.submit(++sequence,at,static_cast<std::uint16_t>((input.held_buttons&primary)!=0U));
            CHECK(m.sample(at).actions_started==0U);
            const auto visuals=m.visuals(at);CHECK_FALSE(visuals.flash);CHECK_FALSE(visuals.shell);
            CHECK_FALSE(visuals.light);CHECK_FALSE(visuals.world_impact);
            CHECK(m.host.beam_effects(m.world,at,{}).count==0U);
            const auto audio=m.host.drain_audio();CHECK(audio.count==0U);CHECK(audio.control_count==0U);
        }
        CHECK_FALSE(m.host.hud_menu_input(m.observed,{.primary_released=true,.seconds=.8}).consume_primary);
        m.submit(++sequence,.8,0);
        const auto fresh=m.host.hud_menu_input(m.observed,
            {.primary_pressed=true,.primary_held=true,.seconds=1.1});
        CHECK_FALSE(fresh.consume_primary);
        m.submit(++sequence,1.1,1);CHECK(m.sample(1.1).actions_started>0U);
        if(target==2U || target==3U || target==4U) {
            CHECK(m.visuals(1.1).flash.has_value());
            const auto fired=m.host.drain_audio();
            CHECK(std::any_of(fired.cues.begin(),fired.cues.begin()+fired.count,
                [](const auto& cue){return cue.kind==api::LocalSoundKind::fire;}));
        }
    }
}
TEST_CASE("E29.1 confirmation suppression survives pending rejection hidden HUD and resource rebinding",
    "[e291][e29][hud][input][life]") {
    HudModule m;m.bind();(void)m.input({.group_digit=2,.seconds=.1});
    const auto selected=m.input({.primary_pressed=true,.primary_held=true,.seconds=.2});
    REQUIRE(selected.selection);m.host.hud_selection_result(*selected.selection,false,.2);
    m.context.hud_draw=false;m.context_update();
    CHECK(m.input({.primary_held=true,.seconds=.3}).consume_primary);
    m.context.hud_draw=true;m.context_update();m.state.weapon_hud.hide_flags=4;
    CHECK(m.input({.primary_held=true,.seconds=.4}).consume_primary);
    m.state.weapon_hud.hide_flags=0;m.bind();
    CHECK(m.input({.primary_held=true,.seconds=.5}).consume_primary);
    // A real KeyUp plus a new down edge in the same collected frame is not
    // the old spent press; never synthesize a release or alter wire history.
    CHECK_FALSE(m.input({.primary_pressed=true,.primary_held=true,.primary_released=true,.seconds=.6}).consume_primary);
    (void)m.input({.group_digit=2,.seconds=.7});
    REQUIRE(m.input({.primary_pressed=true,.primary_held=true,.seconds=.8}).consume_primary);
    ++m.state.lifecycle.life_epoch;++m.state.lifecycle.deaths;
    CHECK_FALSE(m.input({.primary_held=true,.seconds=.9}).consume_primary);
}
TEST_CASE("E29 selection is queued only after the application's actual validated handoff",
    "[e29][hud][input][inventory]") {
    HudModule m;m.bind();(void)m.input({.group_digit=2,.seconds=.1});
    (void)m.input({.group_digit=2,.seconds=.2});
    REQUIRE(m.input({.primary_pressed=true,.seconds=.3}).selection==3);
    auto hud=m.draw(.31);CHECK(hud.weapon_selection_request==3);CHECK_FALSE(hud.weapon_selection_pending);
    CHECK(hud.weapon_selection_status=="awaiting_handoff");
    m.host.hud_selection_result(3,false,.32);hud=m.draw(.33);
    CHECK_FALSE(hud.weapon_selection_request);CHECK_FALSE(hud.weapon_selection_pending);
    CHECK(hud.weapon_selection_status=="request_rejected");CHECK(hud.weapon_name=="weapon_9mmhandgun");
    (void)m.input({.group_digit=2,.seconds=.4});(void)m.input({.group_digit=2,.seconds=.5});
    REQUIRE(m.input({.primary_pressed=true,.seconds=.6}).selection==3);
    m.host.hud_selection_result(3,true,.61);hud=m.draw(.62);
    CHECK(hud.weapon_selection_pending==3);CHECK_FALSE(hud.weapon_selection_request);CHECK(hud.weapon_selection_status=="queued");
    f::select(m.state,3,2);m.host.observe(m.state,.7);hud=m.draw(.71);
    CHECK_FALSE(hud.weapon_selection_pending);CHECK(hud.weapon_selection_status=="server_confirmed");
    CHECK(hud.weapon_name=="weapon_357");
}
TEST_CASE("E29 old-active clip publications cannot reject an asynchronously queued new weapon",
    "[e29][hud][input][inventory]") {
    HudModule m;m.bind();(void)m.input({.group_digit=2,.seconds=.1});
    (void)m.input({.group_digit=2,.seconds=.2});
    REQUIRE(m.input({.primary_pressed=true,.seconds=.3}).selection==3);
    m.host.hud_selection_result(3,true,.31);
    m.state.weapon_hud.active_source=f::source(2);m.state.weapon_hud.clips[2]=16;
    const auto in_flight=m.draw(.4);CHECK(in_flight.weapon_selection_pending==3);
    CHECK(in_flight.weapon_selection_status=="queued");CHECK(in_flight.weapon_name=="weapon_9mmhandgun");
    f::select(m.state,3,3);m.host.observe(m.state,.5);const auto confirmed=m.draw(.6);
    CHECK_FALSE(confirmed.weapon_selection_pending);CHECK(confirmed.weapon_selection_status=="server_confirmed");
}
TEST_CASE("Q toggles confirmed weapons without promoting requests candidates or repeated clip records",
    "[last-weapon][e29][hud][input][inventory]") {
    HudModule m;m.bind();
    CHECK(m.host.movement_policy().inventory_last_key==hlclient::input::PhysicalKey::q);
    CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.1}).selection);
    f::select(m.state,4,2);m.host.observe(m.state,.2);(void)m.draw(.2);
    const auto initial=client::runtime_observation_canonical_hash(m.state);
    const auto back=m.input({.last_weapon_pressed=true,.seconds=.3});
    REQUIRE(back.selection==2);CHECK_FALSE(back.menu_open);CHECK_FALSE(back.consume_primary);
    CHECK(m.draw(.31).weapon_selection_request==2);CHECK(m.draw(.31).weapon_name=="weapon_9mmAR");
    CHECK(client::runtime_observation_canonical_hash(m.state)==initial);
    CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.32}).selection); // awaiting handoff
    m.host.hud_selection_result(2,true,.33);
    CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.34}).selection); // awaiting server
    m.state.weapon_hud.active_source=f::source(3);m.state.weapon_hud.clips[4]=49;
    CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.4}).selection);
    CHECK(m.draw(.41).weapon_selection_pending==2);
    f::select(m.state,2,4);m.host.observe(m.state,.5);(void)m.draw(.5);
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=.6}).selection==4);
    m.host.hud_selection_result(4,false,.61); // transport/rate/authority rejection preserves last item
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=.7}).selection==4);
    m.host.hud_selection_result(4,true,.71);
    CHECK(m.draw(2.8).weapon_selection_status=="confirmation_timeout");
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=2.9}).selection==4);
    m.host.hud_selection_result(4,false,2.91);
    (void)m.input({.group_digit=4,.seconds=3.0}); // RPG menu candidate is not the last held weapon
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=3.1}).selection==4);
    CHECK_FALSE(m.draw(3.2).weapon_menu_open);CHECK(m.host.drain_audio().count<=1);
}
TEST_CASE("Q follows committed switches while HUD is hidden and ignores stale active records",
    "[last-weapon][e29][hud][input][inventory]") {
    HudModule m;m.bind();m.context.hud_draw=false;m.context_update();
    const auto commit=[&](std::uint8_t id,std::uint64_t ordinal) {
        f::select(m.state,id,ordinal);
        api::GameRecordState record;record.weapon_hud=m.state.weapon_hud;record.lifecycle=m.state.lifecycle;
        m.host.commit_record(std::move(record));
    };
    commit(2,1);commit(4,2);commit(7,3); // no input/draw between committed server switches
    m.state.weapon_hud.hide_flags=5;*m.state.receiving_client->owned_weapon_bits&=~(1U<<31U);
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=.1}).selection==4);
    m.host.hud_selection_result(4,false,.11);
    api::GameRecordState stale;stale.weapon_hud=m.state.weapon_hud;stale.lifecycle=m.state.lifecycle;
    stale.weapon_hud.active_weapon_id=1;stale.weapon_hud.active_source=f::source(2);
    m.host.commit_record(std::move(stale));
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=.2}).selection==4);
    m.host.hud_selection_result(4,false,.21);
    // A real later CurWeapon in the same service record is distinct evidence.
    m.state.weapon_hud.active_weapon_id=3;m.state.weapon_hud.active_source=f::source(3);
    m.state.weapon_hud.active_source->start_bit_offset=16;m.state.weapon_hud.active_source->end_bit_offset=32;
    REQUIRE(m.input({.last_weapon_pressed=true,.seconds=.3}).selection==7);
}
TEST_CASE("Q clears history across life reset generation and lost ownership boundaries",
    "[last-weapon][e29][hud][input][inventory][life]") {
    for(const unsigned boundary:{0U,1U,2U,3U,4U}) {
        CAPTURE(boundary);HudModule m;(void)m.draw(.01);
        f::select(m.state,4,2);(void)m.draw(.02);
        if(boundary==0) {m.state.lifecycle.state=client::LocalPlayerLifeState::dead;}
        if(boundary==1) {++m.state.lifecycle.life_epoch;++m.state.lifecycle.deaths;m.state.lifecycle.last_death_source=f::source(3);}
        if(boundary==2) {m.state.weapon_hud.hud_reset_source=f::source(3);}
        if(boundary==3) {m.host.reset({2,2,1});m.state.generation=2;}
        if(boundary==4) {*m.state.receiving_client->owned_weapon_bits&=~(1U<<2U);}
        CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.1}).selection);
        if(boundary==4) {
            *m.state.receiving_client->owned_weapon_bits|=1U<<2U;
            CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.2}).selection); // re-pickup is not use history
        } else {
            m.state.lifecycle.state=client::LocalPlayerLifeState::alive;
            f::select(m.state,4,4);CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.2}).selection);
            f::select(m.state,7,5);REQUIRE(m.input({.last_weapon_pressed=true,.seconds=.3}).selection==4);
        }
    }
}
TEST_CASE("Q retains CanHolster exhaustible ownership and catalogue selection validation",
    "[last-weapon][e29][hud][input][inventory][life]") {
    f::Module grenade{2};(void)grenade.host.hud(grenade.observed,0);
    grenade.adopt(12,.1,2);grenade.submit(1,.2,1);
    const auto blocked=grenade.host.hud_menu_input(grenade.observed,{.primary_held=true,.last_weapon_pressed=true,.seconds=.3});
    CHECK_FALSE(blocked.selection);CHECK_FALSE(blocked.consume_primary);
    grenade.submit(2,.4,1);CHECK(grenade.sample(.4).action==api::LocalWeaponAction::prime);
    f::Module rpg{2};(void)rpg.host.hud(rpg.observed,0);rpg.adopt(8,.1,2);
    rpg.observed.receiving_client->auxiliary_vector2={0.,1.,1.};
    CHECK_FALSE(rpg.host.hud_menu_input(rpg.observed,{.last_weapon_pressed=true,.seconds=.2}).selection);
    rpg.observed.receiving_client->auxiliary_vector2={0.,0.,0.};
    REQUIRE(rpg.host.hud_menu_input(rpg.observed,{.last_weapon_pressed=true,.seconds=.3}).selection==2);
    for(const bool invalid_name:{false,true}) {
        HudModule m;f::select(m.state,13,2);(void)m.draw(.01);f::select(m.state,2,3);(void)m.draw(.02);
        if(invalid_name) {
            for(auto& type:m.state.weapon_hud.catalogue)if(type.id==13)type.command_name="weapon_tripmine;kill";
        } else m.state.weapon_hud.reserve_ammo[9]=0;
        CHECK_FALSE(m.input({.last_weapon_pressed=true,.seconds=.1}).selection);
    }
}
TEST_CASE("Q never invents a fire action and preserves already-spent menu click ownership",
    "[last-weapon][e29][hud][input][inventory][weapon-suite]") {
    f::Module m{2};(void)m.host.hud(m.observed,0);m.adopt(4,.1,2);
    const auto request=m.host.hud_menu_input(m.observed,{.last_weapon_pressed=true,.seconds=.2});
    REQUIRE(request.selection==2);CHECK_FALSE(request.consume_primary);
    m.host.hud_selection_result(2,true,.21);m.adopt(2,.3,3);m.submit(1,.31,0);
    CHECK(m.sample(.32).action!=api::LocalWeaponAction::primary_fire);
    CHECK(m.sample(.32).action!=api::LocalWeaponAction::secondary_fire);
    CHECK_FALSE(m.visuals(.32).flash);CHECK_FALSE(m.visuals(.32).shell);
    CHECK(m.sample(.32).primary_fire_starts==0);CHECK(m.sample(.32).secondary_fire_starts==0);
    HudModule menu;menu.bind();(void)menu.draw(.01);f::select(menu.state,4,2);(void)menu.draw(.02);
    (void)menu.input({.group_digit=2,.seconds=.1});
    auto selected=menu.input({.primary_pressed=true,.primary_held=true,.last_weapon_pressed=true,.seconds=.2});
    REQUIRE(selected.selection==2);CHECK(selected.consume_primary);CHECK_FALSE(selected.menu_open);
    CHECK(menu.input({.primary_held=true,.seconds=.3}).consume_primary);
    CHECK_FALSE(menu.input({.primary_released=true,.seconds=.4}).consume_primary);
    menu.host.hud_selection_result(2,false,.21);
    selected=menu.input({.primary_pressed=true,.primary_held=true,.last_weapon_pressed=true,.seconds=.5});
    CHECK_FALSE(selected.consume_primary); // deliberate physical fire outside UI remains ordinary gameplay input
    menu.host.hud_selection_result(2,false,.51);
    CHECK_FALSE(menu.input({.cancel=true,.last_weapon_pressed=true,.seconds=.6}).selection);
}
TEST_CASE("E29 cancel wins over a coalesced group wheel and confirm without opening or firing",
    "[e29][hud][input][inventory]") {
    HudModule m;m.bind();
    auto canceled=m.input({.group_digit=2,.primary_pressed=true,.primary_held=true,.cancel=true,.seconds=.1});
    CHECK(canceled.cancel_consumed);CHECK(canceled.consume_primary);CHECK_FALSE(canceled.menu_open);CHECK_FALSE(canceled.selection);
    CHECK_FALSE(m.draw(.2).weapon_menu_candidate);CHECK_FALSE(m.draw(.2).weapon_selection_request);
    canceled=m.input({.cycle_direction=1,.primary_held=true,.cancel=true,.seconds=.3});
    CHECK(canceled.cancel_consumed);CHECK(canceled.consume_primary);CHECK_FALSE(canceled.menu_open);CHECK_FALSE(canceled.selection);
    CHECK(m.host.drain_audio().count==0); // An invisible newly-opened menu must not play hudon/select.
}
TEST_CASE("E29 menu cannot bypass primed grenade or active RPG guidance CanHolster gates",
    "[e29][hud][input][inventory][life]") {
    f::Module grenade{12};*grenade.observed.receiving_client->owned_weapon_bits|=1U<<31;
    grenade.submit(1,.02,1);(void)grenade.host.hud_menu_input(grenade.observed,{.group_digit=2,.seconds=.1});
    const auto confirm=grenade.host.hud_menu_input(grenade.observed,{.primary_pressed=true,.seconds=.2});
    CHECK_FALSE(confirm.selection);CHECK_FALSE(grenade.host.inventory_selection(grenade.observed,2));
    f::Module rpg{8};*rpg.observed.receiving_client->owned_weapon_bits|=1U<<31;
    rpg.observed.receiving_client->auxiliary_vector2={0.,1.,1.};
    (void)rpg.host.hud_menu_input(rpg.observed,{.group_digit=2,.seconds=.1});
    CHECK_FALSE(rpg.host.hud_menu_input(rpg.observed,{.primary_pressed=true,.seconds=.2}).selection);
    CHECK_FALSE(rpg.host.inventory_selection(rpg.observed,2));
}
TEST_CASE("E29 blocked Hand Grenade menu cannot synthesize a throw release from held primary",
    "[e29][hud][input][inventory][grenade-local]") {
    f::Module grenade{12};*grenade.observed.receiving_client->owned_weapon_bits|=1U<<31;
    (void)grenade.host.hud_menu_input(grenade.observed,{.group_digit=2,.seconds=.01});
    grenade.submit(1,.02,1);REQUIRE(grenade.sample(.02).action==api::LocalWeaponAction::prime);
    const auto soft_cancel=grenade.host.hud_menu_input(grenade.observed,
        {.primary_held=true,.cancel=true,.seconds=.1});
    CHECK(soft_cancel.cancel_consumed);CHECK_FALSE(soft_cancel.consume_primary);CHECK_FALSE(soft_cancel.selection);
    const auto blocked=grenade.host.hud_menu_input(grenade.observed,
        {.group_digit=2,.primary_held=true,.seconds=.2});
    CHECK_FALSE(blocked.menu_open);CHECK_FALSE(blocked.consume_primary);CHECK_FALSE(blocked.selection);
    const auto primary=hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary);
    hlclient::goldsrc::LiveVisualControlInput input;input.held_buttons=primary;
    input.consumed_buttons=blocked.consume_primary ? primary : 0;
    hlclient::goldsrc::LiveVisualButtonLatch latch;hlclient::goldsrc::consume_live_visual_input_buttons(input,latch);
    CHECK(input.held_buttons==primary);CHECK(input.released_buttons==0);
    grenade.submit(2,.3,1);CHECK(grenade.sample(.3).action==api::LocalWeaponAction::prime);
    CHECK(grenade.sample(.3).actions_started==1); // No throw_release action was manufactured by UI.
}
TEST_CASE("E29 all five buckets wheel wrapping radio-only Satchel and exhaustible retirement use authoritative eligibility",
    "[e29][hud][input][inventory]") {
    HudModule m;m.bind();
    constexpr std::array<std::uint8_t,5> first{1,2,4,8,12};
    for(std::uint8_t group=1;group<=5;++group) {
        (void)m.input({.group_digit=group,.seconds=.1});CHECK(m.draw(.2).weapon_menu_candidate==first[group-1]);
        (void)m.input({.cancel=true,.seconds=.3});
    }
    (void)m.input({.cycle_direction=-1,.seconds=.4});CHECK(m.draw(.5).weapon_menu_candidate==1);
    (void)m.input({.cycle_direction=-1,.seconds=.6});CHECK(m.draw(.7).weapon_menu_candidate==15);
    (void)m.input({.cycle_direction=1,.seconds=.8});CHECK(m.draw(.9).weapon_menu_candidate==1);
    (void)m.input({.cancel=true,.seconds=1});
    m.state.weapon_hud.reserve_ammo[8]=0;m.state.weapon_hud.reserve_ammo[9]=0;
    m.state.weapon_hud.reserve_ammo[10]=0;m.state.weapon_hud.reserve_ammo[11]=0;
    f::slot(m.state,14).auxiliary_integer1=1; // committed radio state, not a pickup or local placed-charge guess
    (void)m.input({.group_digit=5,.seconds=1.1});CHECK(m.draw(1.2).weapon_menu_candidate==14);
    f::slot(m.state,14).auxiliary_integer1=0;
    const auto retired=m.input({.primary_pressed=true,.seconds=1.3});CHECK_FALSE(retired.selection);
    CHECK(retired.consume_primary); // no accidental throw while the empty candidate disappears
}
TEST_CASE("E29 pickup sprites expire once and reset at life boundaries without modifying ammo",
    "[e29][hud][pickups][life]") {
    HudModule m;m.bind();api::GameRecordState record;record.weapon_hud=m.state.weapon_hud;record.lifecycle=m.state.lifecycle;
    record.pickups.push_back({api::GameRecordState::Pickup::Kind::ammunition,1,17,{},f::source(2)});
    record.pickups.push_back({api::GameRecordState::Pickup::Kind::weapon,2,{}, {},f::source(2)});
    record.pickups.push_back({api::GameRecordState::Pickup::Kind::item,0,{},"item_healthkit",f::source(2)});
    m.host.commit_record(std::move(record));const auto original=client::runtime_observation_canonical_hash(m.state);
    CHECK(m.draw(1).pickup_icons_submitted==3);CHECK(m.draw(1.1).pickup_icons_submitted==3);
    CHECK(m.draw(6).pickup_icons_submitted==0);CHECK(m.draw(6.1).inventory_feedback.empty());
    CHECK(client::runtime_observation_canonical_hash(m.state)==original);
    m.state.lifecycle.state=client::LocalPlayerLifeState::dead;
    CHECK(m.draw(6.2).crosshair_status=="hidden_by_policy");CHECK_FALSE(m.draw(6.2).weapon_menu_open);
}
TEST_CASE("E29 respawn and InitHUD reject retained crosshair ammo and menu until a fresh binding",
    "[e29][hud][crosshair][life]") {
    HudModule m;m.bind();f::select(m.state,6,2);m.state.weapon_hud.active_on_target=true;
    m.state.receiving_client->field_of_view=20;m.host.observe(m.state,.1);
    CHECK(m.draw(.2).crosshair_role=="zoom_autoaim");
    m.state.lifecycle.last_death_source=f::source(3);++m.state.lifecycle.deaths;
    ++m.state.lifecycle.life_epoch;m.state.client_metadata.source=f::source(4);m.state.publication_revision=4;
    m.host.observe(m.state,.3);auto stale=m.draw(.4);
    CHECK(stale.crosshair_status=="hidden_by_policy");CHECK(stale.crosshair_resource_id==0);
    CHECK_FALSE(m.input({.group_digit=2,.seconds=.5}).menu_open);
    f::select(m.state,2,5);m.state.weapon_hud.active_on_target=false;m.host.observe(m.state,.6);
    CHECK(m.draw(.7).crosshair_role=="crosshair");
    m.state.weapon_hud.hud_reset_source=client::RuntimeObservationSource{.record_identity=6,.record_ordinal=6,
        .start_bit_offset=8,.end_bit_offset=24};
    stale=m.draw(.8);CHECK(stale.crosshair_status=="hidden_by_policy");CHECK_FALSE(stale.weapon_menu_candidate);
    m.state.weapon_hud.active_source=client::RuntimeObservationSource{.record_identity=6,.record_ordinal=6,
        .start_bit_offset=24,.end_bit_offset=48};
    CHECK(m.draw(.9).crosshair_status=="submitted"); // legitimate new CurWeapon in the same complete record
    m.state.weapon_hud.active_weapon_id.reset();m.state.weapon_hud.active_source.reset();
    m.state.client_metadata.source=f::source(7);m.state.publication_revision=7;
    CHECK(m.input({.group_digit=2,.seconds=1}).menu_open); // fresh no-active-weapon inventory can still choose
}
TEST_CASE("E29 committed reset cancels queued UI sounds and prior-life Damage cannot restart tiles",
    "[e29][hud][audio][life]") {
    HudModule m;m.bind();(void)m.input({.group_digit=2,.seconds=.1});
    api::GameRecordState record;record.weapon_hud=m.state.weapon_hud;record.lifecycle=m.state.lifecycle;
    record.weapon_hud.hud_reset_source=f::source(2);
    m.state.weapon_hud=record.weapon_hud;m.host.commit_record(std::move(record));
    CHECK(m.host.drain_audio().count==0);
    m.state.lifecycle.last_death_source=f::source(3);++m.state.lifecycle.deaths;++m.state.lifecycle.life_epoch;
    f::select(m.state,2,4);m.state.weapon_hud.hud_reset_source.reset();
    api::GameRecordState damage;damage.weapon_hud=m.state.weapon_hud;damage.lifecycle=m.state.lifecycle;
    client::RuntimeLifeEvent old;old.kind=client::RuntimeLifeEventKind::damage;old.damage_bits=1U<<17U;old.source=f::source(2);
    damage.life_events.push_back(old);old.source=f::source(5);old.source.reassembled=true;damage.life_events.push_back(old);
    m.host.commit_record(std::move(damage));
    const auto resources=fixture();const auto& entries=resources.descriptors.front().entries;
    const auto poison=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.role=="dmg_poison" && e.resolution==640;});
    REQUIRE(poison!=entries.end());const auto first=m.draw(.2);
    CHECK(std::none_of(first.draw.sprites.begin(),first.draw.sprites.end(),[&](const auto& s){return s.resource_id==poison->resource_id;}));
    damage={};damage.weapon_hud=m.state.weapon_hud;damage.lifecycle=m.state.lifecycle;
    old.source=f::source(6);damage.life_events.push_back(old);m.host.commit_record(std::move(damage));
    const auto fresh=m.draw(.3);
    CHECK(std::any_of(fresh.draw.sprites.begin(),fresh.draw.sprites.end(),[&](const auto& s){return s.resource_id==poison->resource_id;}));
}
TEST_CASE("E29 fresh reliable fragmented CurWeapon respects the same life record and HUD bit floors",
    "[e29][hud][crosshair][life][reassembly]") {
    HudModule m;m.bind();m.state.lifecycle.last_death_source=f::source(3);
    ++m.state.lifecycle.deaths;++m.state.lifecycle.life_epoch;
    m.state.weapon_hud.hud_reset_source=client::RuntimeObservationSource{
        .record_identity=4,.record_ordinal=4,.start_bit_offset=8,.end_bit_offset=32};
    m.state.weapon_hud.active_source=client::RuntimeObservationSource{
        .record_identity=2,.record_ordinal=2,.start_bit_offset=40,.end_bit_offset=64,.reassembled=true};
    CHECK(m.draw(.1).crosshair_status=="hidden_by_policy"); // old fragment is not a new-life weapon
    m.state.weapon_hud.active_source=client::RuntimeObservationSource{
        .record_identity=4,.record_ordinal=4,.start_bit_offset=16,.end_bit_offset=24,.reassembled=true};
    CHECK(m.draw(.2).crosshair_status=="hidden_by_policy"); // completion before Init/Reset floor remains stale
    m.state.weapon_hud.active_source=client::RuntimeObservationSource{
        .record_identity=4,.record_ordinal=4,.start_bit_offset=32,.end_bit_offset=64,.reassembled=true};
    CHECK(m.draw(.3).crosshair_status=="submitted"); // valid current record, not blanket fragmentation rejection
}
TEST_CASE("E29 hud_draw hides deathfeed but TAB and notice expiry retain their independent contract",
    "[e29][hud][multiplayer]") {
    api::MultiplayerHudState state;state.maximum_clients=2;
    api::DeathNoticeState notice;notice.killer_name="Alice";notice.victim_name="Bob";notice.weapon="9mmhandgun";
    notice.killer=1;notice.victim=2;notice.created_seconds=0;state.notices[0]=notice;
    api::HudPresentationContext context{640,480,false};context.hud_draw=false;
    api::HudState hud;hf::compose_multiplayer_hud(state,hud,context,1);
    CHECK(hud.draw.texts.empty());REQUIRE(state.notices[0]);
    context.scoreboard_held=true;hf::compose_multiplayer_hud(state,hud,context,1);
    CHECK(std::any_of(hud.draw.texts.begin(),hud.draw.texts.end(),[](const auto& text){return text.text=="PLAYER";}));
    CHECK(std::none_of(hud.draw.texts.begin(),hud.draw.texts.end(),[](const auto& text){return text.text.find("[9mmhandgun]")!=std::string::npos;}));
    hud={};hf::compose_multiplayer_hud(state,hud,context,6);CHECK_FALSE(state.notices[0]);
}
TEST_CASE("E29 message parsing preserves inactive weapon crosshair and stages supplemental server HUD state",
    "[e29][hud][server-messages]") {
    f::Module m;client::RuntimeClientObservationState next;
    auto result=f::stage_messages(m,{f::message("CurWeapon",{2,2,16}),f::message("Flashlight",{1,70}),
        f::message("FlashBat",{60}),f::message("StatusIcon",{1,'i','t','e','m','_','t','e','s','t',0,10,20,30})},2,next);
    REQUIRE(result);CHECK(result.state->weapon_hud.active_on_target==true);
    CHECK(result.state->weapon_hud.flashlight_on==true);CHECK(result.state->weapon_hud.flashlight_battery==60);
    REQUIRE(result.state->weapon_hud.status_icons[0]);CHECK(result.state->weapon_hud.status_icons[0]->token=="item_test");
    m.host.commit_record(std::move(*result.state));
    result=f::stage_messages(m,{f::message("CurWeapon",{0,3,5})},3,next);REQUIRE(result);
    CHECK(result.state->weapon_hud.active_weapon_id==2);CHECK(result.state->weapon_hud.active_on_target==true);
    auto invalid=f::stage_messages(m,{f::message("Flashlight",{2,70})},4,next);CHECK_FALSE(invalid);
    invalid=f::stage_messages(m,{f::message("StatusIcon",{1,'.','.',0,10,20,30})},4,next);CHECK_FALSE(invalid);
    result=f::stage_messages(m,{f::message("ResetHUD",{0})},4,next);REQUIRE(result);
    CHECK_FALSE(result.state->weapon_hud.active_on_target);CHECK_FALSE(result.state->weapon_hud.flashlight_battery);
    CHECK_FALSE(result.state->weapon_hud.status_icons[0]);
}
TEST_CASE("E29 production menu composition delivers each UI effect to the existing nonspatial PCM mixer once",
    "[e29][hud][audio][pcm]") {
    struct Loader final:hlclient::goldsrc::SoundAssets {
        hlclient::goldsrc::SoundAssetStatus status{hlclient::goldsrc::SoundAssetStatus::ready};
        std::shared_ptr<hlclient::assets::AudioAsset> pcm=std::make_shared<hlclient::assets::AudioAsset>();
        Loader(){pcm->sample_rate=48000;pcm->channel_count=1;pcm->interleaved_samples.assign(4800,.125F);}
        hlclient::goldsrc::SoundAssetResult request(std::uint16_t) noexcept override{return {status,pcm};}
        hlclient::goldsrc::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override{return {status,pcm};}
    } loader;
    HudModule m;m.bind();
    const auto proof=[&](std::string_view expected,double time) {
        const auto batch=m.host.drain_audio();REQUIRE(batch.count==1);CHECK(batch.cues[0].reference.name()==expected);
        CHECK(batch.cues[0].kind==api::LocalSoundKind::hud_menu);CHECK_FALSE(batch.cues[0].world_origin);
        hlclient::audio::OfflineOutput output;hlclient::goldsrc::LocalAudio delivery(output);
        delivery.update(batch,&loader,time,true);CHECK(delivery.statistics().submitted==1);
        std::array<float,128> samples{};output.mixer.render(samples);
        CHECK(std::all_of(samples.begin(),samples.end(),[](float sample){return std::isfinite(sample);}));
        CHECK(std::any_of(samples.begin(),samples.end(),[](float sample){return std::abs(sample)>1e-5F;}));
        CHECK(samples[0]==Catch::Approx(samples[1])); // Local UI, not a remote world emitter.
        delivery.update(batch,&loader,time,true);CHECK(delivery.statistics().submitted==1);
        CHECK(m.host.drain_audio().count==0);
    };
    (void)m.input({.group_digit=2,.seconds=.1});proof("common/wpn_hudon.wav",.1);
    (void)m.input({.group_digit=2,.seconds=.2});proof("common/wpn_moveselect.wav",.2);
    (void)m.input({.primary_pressed=true,.seconds=.3});proof("common/wpn_select.wav",.3);
    (void)m.input({.group_digit=2,.seconds=.4});(void)m.host.drain_audio();
    (void)m.input({.cancel=true,.seconds=.5});proof("common/wpn_hudoff.wav",.5);
    (void)m.input({.group_digit=2,.seconds=.6});const auto missing_batch=m.host.drain_audio();
    loader.status=hlclient::goldsrc::SoundAssetStatus::missing;
    hlclient::audio::OfflineOutput missing;hlclient::goldsrc::LocalAudio delivery(missing);
    delivery.update(missing_batch,&loader,.6,true);CHECK(delivery.statistics().submitted==0);
    CHECK(delivery.statistics().missing==1);
    loader.status=hlclient::goldsrc::SoundAssetStatus::pending;
    hlclient::audio::OfflineOutput pending;hlclient::goldsrc::LocalAudio pending_delivery(pending);
    pending_delivery.update(missing_batch,&loader,.6,true);CHECK(pending_delivery.statistics().submitted==0);
    auto empty=missing_batch;empty.count=0;
    pending_delivery.update(empty,&loader,1.0,true);CHECK(pending_delivery.statistics().late==1);
}
