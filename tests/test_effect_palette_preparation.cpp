#include "hand_grenade_world_test_fixture.hpp"
#include "effect_palette_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Effect palette preparation uses world verified root exact bytes and typed resource results",
    "[e27-1][effect-palette][no-stock]") {
    namespace t=hlclient::tests;namespace wf=t::hand_grenade_world_fixture;
    namespace api=hlclient::game_api;namespace app=hlclient::app;
    for(unsigned mode=0;mode<5;++mode) {
        t::ScopedLocalResourceTestRoot root;(void)wf::owned(root);
        auto palette=t::effect_palette_fixture::bytes();
        if(mode==2)palette.pop_back();if(mode==3)palette.push_back(std::byte{0});
        if(mode!=1)root.write("valve","gfx/palette.lmp",palette);
        const auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0}});
        const auto requested=mode==4 ? std::string_view{} : std::string_view{"gfx/palette.lmp"};
        auto made=app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/test.bsp"),
            root.path(),"valve",app::ReplayLocalCameraPolicy::offline_spectator,requested);
        REQUIRE(made.projection);const auto& result=made.projection->effect_palette();
        const auto expected=mode==0 ? api::EffectPaletteStatus::ready : mode==1 ? api::EffectPaletteStatus::absent :
            mode==4 ? api::EffectPaletteStatus::not_requested : api::EffectPaletteStatus::decode_failed;
        CHECK(result.status==expected);
        if(mode==0)CHECK(result.colors[195]==std::array<std::uint8_t,3>{160,130,20});
    }
    for(const auto token:{"../palette.lmp","gfx/../palette.lmp","gfx/sub/palette.lmp","gfx/palette.wav","D:/palette.lmp"}) {
        t::ScopedLocalResourceTestRoot root;(void)wf::owned(root);
        root.write("valve","gfx/palette.lmp",t::effect_palette_fixture::bytes());
        const auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0}});
        auto made=app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/test.bsp"),
            root.path(),"valve",app::ReplayLocalCameraPolicy::offline_spectator,token);
        REQUIRE(made.projection);CHECK(made.projection->effect_palette().status==api::EffectPaletteStatus::not_authorized);
    }
}
