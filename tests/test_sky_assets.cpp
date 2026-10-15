#include <hlclient/assets/tga_image.hpp>
#include <hlclient/goldsrc/sky_assets.hpp>
#include "hud_resources_test_fixture_e29.hpp"
#include <catch2/catch_test_macros.hpp>
#include <iostream>

namespace {
namespace a=hlclient::assets;
namespace g=hlclient::goldsrc;
namespace f=hlclient::tests;
namespace r=hlclient::tests::hud_resources_e29;
std::vector<std::byte> tga(unsigned descriptor=32,unsigned bits=24) {
    std::vector<std::byte> bytes(18);bytes[2]=std::byte{2};bytes[12]=bytes[14]=std::byte{2};
    bytes[16]=static_cast<std::byte>(bits);bytes[17]=static_cast<std::byte>(descriptor);
    for(unsigned i=0;i<4;++i) {
        bytes.push_back(static_cast<std::byte>(30+i));bytes.push_back(static_cast<std::byte>(20+i));
        bytes.push_back(static_cast<std::byte>(10+i));if(bits==32)bytes.push_back(static_cast<std::byte>(40+i));
    }
    return bytes;
}
void write_sky(const f::ScopedLocalResourceTestRoot& root,std::string_view game="valve") {
    for(auto suffix:{"rt","lf","up","dn","bk","ft"})root.write(game,std::string{"gfx/env/project"}+suffix+".tga",tga());
}
TEST_CASE("TGA truecolor decoder respects orientation alpha RLE and bounded malformed data",
    "[skybox][tga][no-stock]") {
    for(unsigned descriptor:{0U,16U,32U,48U}) {
        const auto image=a::decode_tga_image(tga(descriptor));REQUIRE(image);REQUIRE(image->pixels.size()==16);
        for(unsigned i=0;i<4;++i) {
            auto x=i%2,y=i/2;if(descriptor&16)x=1-x;if(!(descriptor&32))y=1-y;
            CHECK(image->pixels[(y*2+x)*4]==static_cast<std::byte>(10+i));
            CHECK(image->pixels[(y*2+x)*4+3]==std::byte{255});
        }
    }
    const auto alpha=a::decode_tga_image(tga(40,32));REQUIRE(alpha);CHECK(alpha->pixels[3]==std::byte{40});
    const auto noalpha=a::decode_tga_image(tga(32,32));REQUIRE(noalpha);CHECK(noalpha->pixels[3]==std::byte{255});
    auto packed=tga();packed.resize(18);packed[2]=std::byte{10};
    packed.insert(packed.end(),{std::byte{0x83},std::byte{3},std::byte{2},std::byte{1}});
    const auto image=a::decode_tga_image(packed);REQUIRE(image);CHECK(image->pixels[12]==std::byte{1});
    packed[18]=std::byte{0x84};CHECK_FALSE(a::decode_tga_image(packed));
    const auto valid=tga();for(std::size_t n=0;n<valid.size();++n)CHECK_FALSE(a::decode_tga_image(std::span{valid}.first(n)));
    for(auto offset:{1U,2U,12U,14U,16U,17U}) {
        auto bad=valid;bad[offset]=std::byte{255};CHECK_FALSE(a::decode_tga_image(bad));
    }
}
TEST_CASE("Sky six-face exact-root approval is transactional and server name wins",
    "[skybox][approved-source][no-stock]") {
    f::ScopedLocalResourceTestRoot root;root.write("valve","maps/project.bsp","world fixture");write_sky(root);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{f::readiness_fixture::make_environment(root)};
    const auto resources=r::resources_for(environment,"maps/project.bsp");
    const std::string text="{\"classname\" \"worldspawn\" \"skyname\" \"project\"}";
    const auto lump=std::as_bytes(std::span{text});
    auto ready=g::prepare_sky_assets(resources,"project",{});REQUIRE(ready.skybox);CHECK(ready.status==g::SkyAssetStatus::ready);
    CHECK(ready.skybox->faces[0].pixels[0]==std::byte{10});
    // Original upper/lower panel rotations are opposite, not a global row flip.
    CHECK(ready.skybox->faces[2].pixels[0]==std::byte{11});
    CHECK(ready.skybox->faces[3].pixels[0]==std::byte{12});
    CHECK(g::prepare_sky_assets(resources,"",lump).status==g::SkyAssetStatus::ready);
    CHECK(g::prepare_sky_assets(resources,"absent",lump).status==g::SkyAssetStatus::missing);
    CHECK(g::prepare_sky_assets(resources,"",{}).status==g::SkyAssetStatus::not_requested);
    for(auto unsafe:{"../project","project/rt","c:project","project.tga","project\\rt"})
        CHECK(g::prepare_sky_assets(resources,unsafe,lump).status==g::SkyAssetStatus::invalid_name);
    root.write("valve","gfx/env/projectft.tga","bad");
    const auto bad=g::prepare_sky_assets(resources,"project",{});CHECK_FALSE(bad.skybox);CHECK(bad.status==g::SkyAssetStatus::rejected);
    CHECK(ready.skybox->faces[0].pixels[0]==std::byte{10}); // Owning publication survives replacement.
    root.create_game("mod");root.write("mod","maps/project.bsp","world fixture");
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> mod{f::readiness_fixture::make_environment(root,"mod")};
    const auto missing=g::prepare_sky_assets(r::resources_for(mod,"maps/project.bsp"),"project",{});
    CHECK(missing.status==g::SkyAssetStatus::missing);CHECK_FALSE(missing.skybox); // No fallback into valve.
}
TEST_CASE("Original approved Half-Life sky panels import without a live session",
    "[skybox][actual-assets][asset-audit]") {
    const auto basedir=r::environment_value("HLCLIENT_SKY_ASSET_ROOT");
    if(basedir.empty())SKIP("Set HLCLIENT_SKY_ASSET_ROOT for read-only original sky audit");
    auto roots=hlclient::local_resources::LocalResourceSearchRoots::create(basedir,"valve");REQUIRE(roots);
    auto made=hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    const auto ready=g::prepare_sky_assets(r::resources_for(environment,"maps/crossfire.bsp"),"desert",{});
    REQUIRE(ready.status==g::SkyAssetStatus::ready);REQUIRE(ready.skybox);
    for(const auto& face:ready.skybox->faces) {
        CHECK(face.width==256);CHECK(face.height==256);CHECK(face.pixels.size()==256*256*4);
    }
    std::cout<<"[sky-asset] six original desert TGA panels ready; live_session=0\n";
}
}
