#include <hlclient/goldsrc/decal_assets.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>
#include <hlclient/hash/sha256.hpp>
#include "hud_resources_test_fixture_e29.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <iostream>

namespace {
namespace g=hlclient::goldsrc;
namespace api=hlclient::game_api;
namespace f=hlclient::tests;
namespace resources=hlclient::tests::hud_resources_e29;
api::DecalAssetRequest request(std::string_view name,
    api::DecalImageInterpretation interpretation=api::DecalImageInterpretation::masked_palette_color,
    std::string_view archive="decals.wad") {
    api::DecalAssetRequest result;
    REQUIRE(name.size()<result.texture_name.size());REQUIRE(archive.size()<result.wad_name.size());
    std::copy(name.begin(),name.end(),result.texture_name.begin());
    std::copy(archive.begin(),archive.end(),result.wad_name.begin());
    result.interpretation=interpretation;return result;
}
TEST_CASE("E30 compiled decal dependencies use one verified world root and owning cache",
    "[e30][decal-assets][approved-source][no-stock]") {
    f::ScopedLocalResourceTestRoot root;root.write("valve","maps/project.bsp","world fixture");
    const std::array lumps{f::SyntheticWad3Entry{"{BLOOD1",f::synthetic_goldsrc_miptex("{BLOOD1")},
        f::SyntheticWad3Entry{"{yblood1",f::synthetic_goldsrc_miptex("{yblood1")}};
    const auto wad=f::synthetic_wad3(lumps);root.write("valve","decals.wad",wad.bytes);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        f::readiness_fixture::make_environment(root)};
    const auto resources_for_world=resources::resources_for(environment,"maps/project.bsp");
    const std::array requests{request("{blood1"),request("{yblood1"),request("{absent")};
    const auto prepared=g::prepare_decal_assets(resources_for_world,requests);
    REQUIRE(prepared.count==3U);CHECK(prepared.archive_status==g::DecalAssetStatus::ready);
    CHECK(prepared.source_bytes==wad.bytes.size());CHECK(prepared.source_digest==hlclient::hash::sha256(wad.bytes));
    REQUIRE(prepared.entries[0].status==g::DecalAssetStatus::ready);REQUIRE(prepared.entries[0].texture);
    REQUIRE(prepared.entries[1].status==g::DecalAssetStatus::ready);REQUIRE(prepared.entries[1].texture);
    CHECK(prepared.entries[2].status==g::DecalAssetStatus::absent);
    CHECK(prepared.entries[0].texture->name=="{BLOOD1");CHECK(prepared.entries[1].texture->name=="{yblood1");
    CHECK_FALSE(prepared.entries[0].audit.exact_spelling_match);
    CHECK(prepared.entries[1].audit.exact_spelling_match);
    CHECK(prepared.entries[0].audit.mips[0].unique_indices==256U);
    CHECK(prepared.entries[0].audit.mips[0].index_255_count==1U);
    CHECK(prepared.entries[0].audit.palette[127]==std::array<std::uint8_t,3>{127,128,37});
    for(const auto& mip:prepared.entries[0].texture->mip_levels) {
        REQUIRE(mip.rgba_pixels.size()==mip.width*mip.height*4U);
        CHECK(mip.rgba_pixels[3]==std::byte{255});
    }
    // Already published texture/audit owns its data independently of input.
    root.write("valve","decals.wad","replaced fixture");
    CHECK(prepared.entries[0].texture->mip_levels[0].rgba_pixels[0]==std::byte{0});
    CHECK(prepared.entries[0].audit.source_lump_bytes==lumps[0].payload.size());
}
TEST_CASE("E30 dependency authorization absent malformed and capacity remain distinct",
    "[e30][decal-assets][approved-source][limits][no-stock]") {
    f::ScopedLocalResourceTestRoot root;root.write("valve","maps/project.bsp","world fixture");
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        f::readiness_fixture::make_environment(root)};
    const auto resources_for_world=resources::resources_for(environment,"maps/project.bsp");
    const std::array valid{request("{blood1")};
    CHECK(g::prepare_decal_assets(resources_for_world,valid).entries[0].status==g::DecalAssetStatus::absent);
    const std::array unsafe{request("{blood1",api::DecalImageInterpretation::masked_palette_color,"../decals.wad")};
    CHECK(g::prepare_decal_assets(resources_for_world,unsafe).entries[0].status==g::DecalAssetStatus::not_authorized);
    const std::array unsafe_texture{request("../blood1")};
    CHECK(g::prepare_decal_assets(resources_for_world,unsafe_texture).entries[0].status==g::DecalAssetStatus::not_authorized);
    auto unterminated=request("{blood1");unterminated.texture_name.fill('a');
    const std::array missing_terminator{unterminated};
    CHECK(g::prepare_decal_assets(resources_for_world,missing_terminator).entries[0].status==g::DecalAssetStatus::not_authorized);
    const std::array mixed{request("{blood1"),request("{blood1",api::DecalImageInterpretation::masked_palette_color,"other.wad")};
    CHECK(g::prepare_decal_assets(resources_for_world,mixed).archive_status==g::DecalAssetStatus::not_authorized);
    std::array<api::DecalAssetRequest,17> excessive{};
    CHECK(g::prepare_decal_assets(resources_for_world,excessive).archive_status==g::DecalAssetStatus::capacity);
    root.write("valve","decals.wad","not a WAD");
    CHECK(g::prepare_decal_assets(resources_for_world,valid).entries[0].status==g::DecalAssetStatus::decode_failed);
    root.write_repeated("valve","decals.wad",4U*1024U*1024U+1U,std::byte{0});
    CHECK(g::prepare_decal_assets(resources_for_world,valid).entries[0].status==g::DecalAssetStatus::capacity);
    CHECK(g::prepare_decal_assets({},valid).entries[0].status==g::DecalAssetStatus::absent);
    const auto none=g::prepare_decal_assets(resources_for_world,{});
    CHECK(none.archive_status==g::DecalAssetStatus::ready);CHECK(none.count==0U);CHECK(none.source_bytes==0U);
}
TEST_CASE("E30 exact selected mod world does not fall back to valve WAD",
    "[e30][decal-assets][approved-source][root-ownership][no-stock]") {
    f::ScopedLocalResourceTestRoot root;root.create_game("mod");
    root.write("mod","maps/project.bsp","world fixture");
    const auto wad=f::synthetic_valid_wad3("{blood1");root.write("valve","decals.wad",wad.bytes);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        f::readiness_fixture::make_environment(root,"mod")};
    const std::array requests{request("{blood1")};
    const auto prepared=g::prepare_decal_assets(resources::resources_for(environment,"maps/project.bsp"),requests);
    CHECK(prepared.entries[0].status==g::DecalAssetStatus::absent);CHECK(prepared.source_bytes==0U);
}
TEST_CASE("E30 explicit tint coverage preserves four mip levels without changing ordinary masked decoding",
    "[e30][decal-assets][material][indexed-texture][no-stock]") {
    auto pixels=f::synthetic_goldsrc_miptex("{PROJECT_DECAL");
    const auto palette=pixels.size()-768U;
    const std::array<std::uint8_t,3> tint{36,0,0};
    for(std::size_t i=0;i<255;++i) {
        const auto shade=static_cast<std::byte>(255U-i*255U/254U);
        for(std::size_t c=0;c<3;++c)pixels[palette+i*3U+c]=shade;
    }
    for(std::size_t c=0;c<3;++c)pixels[palette+255U*3U+c]=static_cast<std::byte>(tint[c]);
    const std::array lumps{f::SyntheticWad3Entry{"{PROJECT_DECAL",pixels}};
    const auto wad=f::synthetic_wad3(lumps);
    const auto catalog=g::wad3::GoldSrcWad3CatalogParser::parse(wad.bytes);REQUIRE(catalog);
    const auto* lump=catalog.catalog->find_miptex("{project_decal");REQUIRE(lump);
    const auto ordinary=g::wad3::GoldSrcWad3TextureDecoder::decode(wad.bytes,*lump);REQUIRE(ordinary);
    CHECK(ordinary.texture->alpha_mode==hlclient::assets::WorldTextureAlphaMode::masked_index_255);
    CHECK(ordinary.texture->mip_levels[0].rgba_pixels[0]==std::byte{255});
    CHECK(ordinary.texture->mip_levels[0].rgba_pixels[3]==std::byte{255});
    CHECK(ordinary.texture->mip_levels[0].rgba_pixels[255U*4U+3U]==std::byte{0});
    g::wad3::GoldSrcWad3TextureRequest decode_request;
    decode_request.interpretation=g::indexed_texture::GoldSrcIndexedTextureInterpretation::palette_tint_index_coverage;
    const auto decoded=g::wad3::GoldSrcWad3TextureDecoder::decode(wad.bytes,*lump,decode_request);REQUIRE(decoded);
    CHECK(decoded.texture->alpha_mode==hlclient::assets::WorldTextureAlphaMode::straight_alpha);
    for(std::size_t level=0;level<4;++level) {
        const auto& mip=decoded.texture->mip_levels[level];
        REQUIRE(mip.rgba_pixels.size()==mip.width*mip.height*4U);
        for(std::size_t pixel=0;pixel<mip.rgba_pixels.size()/4U;++pixel) {
            for(std::size_t c=0;c<3;++c)CHECK(mip.rgba_pixels[pixel*4U+c]==static_cast<std::byte>(tint[c]));
            CHECK(mip.rgba_pixels[pixel*4U+3U]==static_cast<std::byte>(static_cast<std::uint8_t>(pixel+level)));
        }
    }
    // The transparent edge retains tint RGB, not white RGB, for consistent
    // straight-alpha interpolation. Selection is explicit, never a white key.
    CHECK(decoded.texture->mip_levels[0].rgba_pixels[3]==std::byte{0});
    CHECK(decoded.texture->mip_levels[0].rgba_pixels[254U*4U+3U]==std::byte{254});
    CHECK(decoded.texture->mip_levels[0].rgba_pixels[255U*4U+3U]==std::byte{255});
    auto incompatible=wad.bytes;
    incompatible[lump->file_offset+palette+3U]=std::byte{0}; // Increasing/non-gray ramp is not authorized semantics.
    const auto rejected=g::wad3::GoldSrcWad3TextureDecoder::decode(incompatible,*lump,decode_request);
    REQUIRE_FALSE(rejected);REQUIRE(rejected.error);REQUIRE(rejected.error->miptex_error);
    CHECK(rejected.error->miptex_error->code==g::indexed_texture::GoldSrcMiptexErrorCode::invalid_configuration);
    CHECK(g::wad3::GoldSrcWad3TextureDecoder::decode(incompatible,*lump)); // Ordinary decode is not globally restricted.
    decode_request.interpretation=static_cast<g::indexed_texture::GoldSrcIndexedTextureInterpretation>(255);
    CHECK_FALSE(g::wad3::GoldSrcWad3TextureDecoder::decode(wad.bytes,*lump,decode_request));
    f::ScopedLocalResourceTestRoot root;root.write("valve","maps/project.bsp","world fixture");
    root.write("valve","decals.wad",wad.bytes);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{
        f::readiness_fixture::make_environment(root)};
    const std::array compiled{request("{project_decal",api::DecalImageInterpretation::palette_tint_index_coverage)};
    const auto ready=g::prepare_decal_assets(resources::resources_for(environment,"maps/project.bsp"),compiled);
    REQUIRE(ready.entries[0].status==g::DecalAssetStatus::ready);REQUIRE(ready.entries[0].texture);
    CHECK(ready.entries[0].texture->alpha_mode==hlclient::assets::WorldTextureAlphaMode::straight_alpha);
    CHECK(ready.entries[0].texture->mip_levels[0].rgba_pixels==decoded.texture->mip_levels[0].rgba_pixels);
    root.write("valve","decals.wad",incompatible);
    const auto wrong_material=g::prepare_decal_assets(resources::resources_for(environment,"maps/project.bsp"),compiled);
    CHECK(wrong_material.entries[0].status==g::DecalAssetStatus::material_rejected);
    CHECK_FALSE(wrong_material.entries[0].texture);
}
TEST_CASE("E30 original twelve blood decal entries have approved bounded palette audit",
    "[e30][decal-assets][actual-assets][asset-audit]") {
    const auto basedir=resources::environment_value("HLCLIENT_E30_ASSET_ROOT");
    if(basedir.empty())SKIP("Set HLCLIENT_E30_ASSET_ROOT for explicit approved read-only blood WAD audit");
    auto roots=hlclient::local_resources::LocalResourceSearchRoots::create(basedir,"valve");REQUIRE(roots);
    auto made=hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    std::array<api::DecalAssetRequest,12> requests;
    for(std::size_t i=0;i<requests.size();++i)
        requests[i]=request(std::string{i<6 ? "{blood" : "{yblood"}+std::to_string(i%6+1),
            api::DecalImageInterpretation::palette_tint_index_coverage);
    const auto prepared=g::prepare_decal_assets(resources::resources_for(environment,"maps/crossfire.bsp"),requests);
    REQUIRE(prepared.archive_status==g::DecalAssetStatus::ready);REQUIRE(prepared.count==12U);REQUIRE(prepared.source_digest);
    std::cout<<"[e30-wad-audit] source_bytes="<<prepared.source_bytes<<" sha256="
        <<hlclient::hash::sha256_hex(*prepared.source_digest)<<'\n';
    for(std::size_t i=0;i<prepared.count;++i) {
        const auto& entry=prepared.entries[i];CAPTURE(i);
        std::cout<<"[e30-wad-name] requested="<<requests[i].texture_name.data()
            <<" source="<<entry.audit.resolved_source_name.data()<<" type="<<unsigned(entry.audit.source_lump_type)
            <<" exact_spelling="<<entry.audit.exact_spelling_match<<" status="<<g::to_string(entry.status)<<'\n';
        REQUIRE(entry.status==g::DecalAssetStatus::ready);REQUIRE(entry.texture);REQUIRE(entry.audit.source_lump_digest);
        bool gray_ramp=true,inverse_ramp=true;
        bool inverse_gray_monotone=entry.audit.palette[0]==std::array<std::uint8_t,3>{255,255,255} &&
            entry.audit.palette[254]==std::array<std::uint8_t,3>{0,0,0};
        for(std::size_t p=0;p<255;++p)for(std::size_t c=0;c<3;++c) {
            gray_ramp=gray_ramp && entry.audit.palette[p][c]==p;
            inverse_ramp=inverse_ramp && entry.audit.palette[p][c]==255U-p;
            inverse_gray_monotone=inverse_gray_monotone && entry.audit.palette[p][c]==entry.audit.palette[p][0] &&
                (p==0 || entry.audit.palette[p][c]<=entry.audit.palette[p-1U][c]);
        }
        CHECK(inverse_gray_monotone);
        std::cout<<"[e30-wad-audit] entry="<<entry.texture->name<<" dimensions="
            <<entry.texture->width<<'x'<<entry.texture->height<<" mip_levels=4 gray_ramp="<<gray_ramp
            <<" inverse_ramp="<<inverse_ramp<<" inverse_gray_monotone="<<inverse_gray_monotone
            <<" lump_sha256="<<hlclient::hash::sha256_hex(*entry.audit.source_lump_digest);
        for(const auto p:{0U,1U,127U,254U,255U}) {
            const auto& color=entry.audit.palette[p];
            std::cout<<" palette"<<p<<'='<<unsigned(color[0])<<','<<unsigned(color[1])<<','<<unsigned(color[2]);
        }
        for(std::size_t m=0;m<4;++m) {
            const auto& mip=entry.audit.mips[m];
            std::cout<<" mip"<<m<<'='<<mip.width<<'x'<<mip.height<<",indices:"<<unsigned(mip.minimum_index)
                <<'-'<<unsigned(mip.maximum_index)<<",unique:"<<mip.unique_indices
                <<",zero:"<<mip.index_zero_count<<",255:"<<mip.index_255_count;
        }
        std::cout<<'\n';
        CHECK(entry.texture->width<=256U);CHECK(entry.texture->height<=256U);
        CHECK(entry.texture->alpha_mode==hlclient::assets::WorldTextureAlphaMode::straight_alpha);
        for(std::size_t m=0;m<4;++m) {
            const auto& mip=entry.texture->mip_levels[m];std::uint32_t transparent{};
            std::uint8_t strongest{};
            for(std::size_t pixel=0;pixel<mip.rgba_pixels.size()/4U;++pixel) {
                for(std::size_t c=0;c<3;++c)
                    CHECK(mip.rgba_pixels[pixel*4U+c]==static_cast<std::byte>(entry.audit.palette[255][c]));
                const auto coverage=std::to_integer<std::uint8_t>(mip.rgba_pixels[pixel*4U+3U]);
                if(!coverage)++transparent;strongest=std::max(strongest,coverage);
            }
            CHECK(transparent==entry.audit.mips[m].index_zero_count);
            CHECK(strongest==entry.audit.mips[m].maximum_index);
        }
    }
}
} // namespace
