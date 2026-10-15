#include "goldsrc_sprite_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"

#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {
namespace a=hlclient::assets;
namespace l=hlclient::local_resources;
namespace la=hlclient::local_assets;
namespace spr=hlclient::goldsrc::sprite;
namespace fixture=hlclient::tests::sprite_fixture;

std::filesystem::path environment_path(const wchar_t* name) {
    std::filesystem::path result;
#ifdef _WIN32
    wchar_t* value=nullptr;
    std::size_t count{};
    if(_wdupenv_s(&value,&count,name)==0 && value)result=value;
    std::free(value);
#else
    (void)name;
#endif
    return result;
}

std::shared_ptr<const l::LocalResourceEnvironment> environment(const std::filesystem::path& root) {
    auto roots=l::LocalResourceSearchRoots::create(root,"valve");
    REQUIRE(roots);
    auto made=l::LocalResourceEnvironment::create(std::move(*roots.roots));
    REQUIRE(made);
    return std::shared_ptr<const l::LocalResourceEnvironment>{std::move(made.environment)};
}

// The caller supplies one validated root capability. Every original resource
// is reopened with its stable identity through the existing verified opener;
// no native file read, alternate installation or path discovery is involved.
la::LocalAssetSource open_verified(const std::shared_ptr<const l::LocalResourceEnvironment>& env,
    l::LocalResourceRootId root,std::string_view resource,std::uint64_t maximum_bytes) {
    INFO(resource);
    auto name=l::LocalVirtualResourceName::create(resource);
    REQUIRE(name);
    auto found=env->resolve_exact_root(*name.name,root);
    REQUIRE(found);
    REQUIRE(found.file->file_size()<=maximum_bytes);
    auto locator=env->make_locator(root,*name.name,found.file->identity(),found.file->file_size());
    found.file->close();
    REQUIRE(locator);
    la::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes=maximum_bytes;
    limits.timeout=std::chrono::seconds{2};
    la::LocalAssetSourceOpener opener;
    auto opened=opener.begin(*locator.locator,env,limits);
    REQUIRE(opened);
    for(std::size_t i=0;i<128 && !opened.operation->result() && !opened.operation->error();++i)
        opened.operation->update(std::chrono::steady_clock::now());
    auto result=opened.operation->take_result();
    REQUIRE(result);
    CHECK(result->root_id()==root);
    return std::move(*result);
}

a::SpritePaletteColor palette_entry(std::span<const std::byte> bytes,std::size_t index) {
    REQUIRE(bytes.size()==768);
    REQUIRE(index<256);
    return {std::to_integer<std::uint8_t>(bytes[index*3]),
        std::to_integer<std::uint8_t>(bytes[index*3+1]),
        std::to_integer<std::uint8_t>(bytes[index*3+2])};
}

a::SpriteAsset import_sprite(const la::LocalAssetSource& source) {
    auto imported=spr::GoldSrcSpriteImporter{}.import(source.source());
    REQUIRE(imported);
    return std::move(imported).value();
}

std::size_t check_frame(const a::SpriteIndexedFrame& frame) {
    REQUIRE(frame.indexed_pixels.size()==std::size_t(frame.width)*frame.height);
    REQUIRE(frame.derived_rgba8.size()==frame.indexed_pixels.size()*4);
    CHECK(frame.rgba_evidence==a::SpriteRgbaEvidenceProfile::alpha_test_index_255);
    std::size_t covered{},transparent{};
    bool gray=true;
    for(std::size_t i=0;i<frame.indexed_pixels.size();++i) {
        const auto alpha=std::to_integer<unsigned>(frame.derived_rgba8[i*4+3]);
        if(frame.indexed_pixels[i]==std::byte{255}) {
            ++transparent;
            CHECK(alpha==0);
        } else {
            ++covered;
            CHECK(alpha==255);
            gray=gray && frame.derived_rgba8[i*4]==frame.derived_rgba8[i*4+1] &&
                frame.derived_rgba8[i*4+1]==frame.derived_rgba8[i*4+2];
        }
    }
    CHECK(gray);
    CHECK(covered>0);
    CHECK(transparent>0);
    return covered;
}

// This artifact is a decoded-data contact sheet, not a screenshot or a stock
// renderer comparison. Rows 0..2 are spray; 3..5 are drops. Each triplet shows
// source intensity, alpha coverage, and source intensity * host palette195.
// Transparent pixels retain a checkerboard. Tiny drops are enlarged 4x solely
// for inspection; the production source geometry remains unchanged.
void contact_sheet(const std::filesystem::path& directory,
    const a::SpriteSourceAssetData& spray,const a::SpriteSourceAssetData& drops,
    a::SpritePaletteColor yellow) {
    if(directory.empty())return;
    REQUIRE(directory.is_absolute());
    REQUIRE(std::filesystem::is_directory(directory));
    constexpr std::size_t cell=80,width=10*cell,height=6*cell;
    std::vector<std::uint8_t> rgba(width*height*4,255);
    for(std::size_t y=0;y<height;++y)for(std::size_t x=0;x<width;++x) {
        const auto shade=static_cast<std::uint8_t>(((x/8+y/8)&1) ? 48 : 72);
        const auto p=(y*width+x)*4;
        rgba[p]=rgba[p+1]=rgba[p+2]=shade;
    }
    const std::array color{yellow.red,yellow.green,yellow.blue};
    const auto compose=[&](const a::SpriteSourceAssetData& asset,std::size_t first_row,unsigned zoom) {
        for(std::size_t frame_index=0;frame_index<asset.indexed_frames.size();++frame_index) {
            const auto& frame=asset.indexed_frames[frame_index];
            for(unsigned mode=0;mode<3;++mode)for(std::uint32_t sy=0;sy<frame.height;++sy)
                for(std::uint32_t sx=0;sx<frame.width;++sx) {
                    const auto source=(std::size_t(sy)*frame.width+sx)*4;
                    if(frame.derived_rgba8[source+3]==std::byte{0})continue;
                    for(unsigned zy=0;zy<zoom;++zy)for(unsigned zx=0;zx<zoom;++zx) {
                        const auto x=frame_index*cell+8+sx*zoom+zx;
                        const auto y=(first_row+mode)*cell+8+sy*zoom+zy;
                        REQUIRE(x<width);REQUIRE(y<height);
                        const auto pixel=(y*width+x)*4;
                        for(unsigned channel=0;channel<3;++channel) {
                            const auto value=std::to_integer<unsigned>(frame.derived_rgba8[source+channel]);
                            rgba[pixel+channel]=static_cast<std::uint8_t>(mode==1 ? 255 :
                                mode==2 ? (value*color[channel]+127)/255 : value);
                        }
                    }
                }
        }
    };
    compose(spray,0,1);compose(drops,3,4);
    const auto target=directory/"blood-original-contact-sheet.tga";
    // Opt-in generated artifact writes only. Original input assets are read-only.
    std::ofstream output{target,std::ios::binary|std::ios::trunc};
    REQUIRE(output);
    std::array<std::uint8_t,18> header{};
    header[2]=2;header[12]=static_cast<std::uint8_t>(width&255);header[13]=static_cast<std::uint8_t>(width>>8);
    header[14]=static_cast<std::uint8_t>(height&255);header[15]=static_cast<std::uint8_t>(height>>8);
    header[16]=32;header[17]=0x28; // top-origin RGBA8, uncompressed BGRA wire order
    output.write(reinterpret_cast<const char*>(header.data()),header.size());
    for(std::size_t p=0;p<rgba.size();p+=4) {
        const std::array bgra{rgba[p+2],rgba[p+1],rgba[p],rgba[p+3]};
        output.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());
    }
    REQUIRE(output.good());
    std::cout<<"blood-contact-sheet generated=1 width="<<width<<" height="<<height
        <<" rows=source,coverage,host195 source=spray,drops\n";
}
}

TEST_CASE("E27.1 owned palette and masked sprite keep host tint separate from source coverage",
    "[e27-1][snark-blood-audit][owned-fixture]") {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    std::vector<std::byte> palette(768,std::byte{0});
    palette[195*3]=std::byte{12};palette[195*3+1]=std::byte{34};palette[195*3+2]=std::byte{56};
    palette[247*3]=std::byte{78};palette[247*3+1]=std::byte{90};palette[247*3+2]=std::byte{123};
    root.write("valve","gfx/palette.lmp",palette);
    auto bytes=fixture::literal_header_and_palette(2,3,2,1,4,4);
    for(const unsigned index:{1U,2U})for(unsigned channel=0;channel<3;++channel)
        bytes[fixture::kPaletteOffset+index*3+channel]=std::byte{static_cast<std::uint8_t>(index*80)};
    for(unsigned frame=0;frame<2;++frame) {
        fixture::FrameSpec image{-2,2,4,4,std::vector<std::byte>(16,std::byte{255})};
        image.pixels[5+frame]=std::byte{static_cast<std::uint8_t>(frame+1)};
        fixture::append_single_entry(bytes,image);
    }
    root.write("valve","sprites/bloodspray.spr",bytes);
    auto env=environment(root.path());
    // Root IDs are opaque; obtain the actual primary capability by resolving
    // the first virtual resource, then keep it for every companion lookup.
    auto virtual_name=l::LocalVirtualResourceName::create("gfx/palette.lmp");
    REQUIRE(virtual_name);
    auto found=env->resolver().resolve(*virtual_name.name);
    REQUIRE(found);
    const auto primary=found.file->root_id();found.file->close();
    auto source=open_verified(env,primary,"gfx/palette.lmp",768);
    CHECK((palette_entry(source.source().bytes(),195)==a::SpritePaletteColor{12,34,56}));
    CHECK((palette_entry(source.source().bytes(),247)==a::SpritePaletteColor{78,90,123}));
    auto sprite_source=open_verified(env,primary,"sprites/bloodspray.spr",65536);
    auto sprite=import_sprite(sprite_source);
    REQUIRE(sprite.source_data);
    const auto& data=*sprite.source_data;
    REQUIRE(data.indexed_frames.size()==2);
    CHECK(data.groups.empty());
    CHECK(data.texture_format==a::SpriteTextureFormat::alpha_test);
    CHECK(data.indexed_frames[0].derived_rgba8!=data.indexed_frames[1].derived_rgba8);
    CHECK(check_frame(data.indexed_frames[0])==1);
    CHECK(check_frame(data.indexed_frames[1])==1);
    CHECK(data.palette.colors[195]!=palette_entry(source.source().bytes(),195));
}

TEST_CASE("E27.1 approved original blood assets expose base palette spray drops and contact sheet",
    "[e27-1][snark-blood-audit][optional-local]") {
    const auto root=environment_path(L"HLCLIENT_LOCAL_GAME_ROOT");
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Exact approved read-only research root opt-in absent");
    auto env=environment(root);
    auto name=l::LocalVirtualResourceName::create("maps/crossfire.bsp");
    REQUIRE(name);
    auto world=env->resolver().resolve(*name.name);
    REQUIRE(world);
    const auto primary=world.file->root_id();world.file->close();
    auto palette=open_verified(env,primary,"gfx/palette.lmp",768);
    REQUIRE(palette.byte_count()==768);
    const auto yellow=palette_entry(palette.source().bytes(),195);
    const auto red=palette_entry(palette.source().bytes(),247);
    CHECK((yellow==a::SpritePaletteColor{203,183,15}));
    CHECK((red==a::SpritePaletteColor{103,0,0}));
    std::cout<<"blood-host-palette source=gfx/palette.lmp bytes=768 index195="
        <<unsigned(yellow.red)<<','<<unsigned(yellow.green)<<','<<unsigned(yellow.blue)
        <<" index247="<<unsigned(red.red)<<','<<unsigned(red.green)<<','<<unsigned(red.blue)<<'\n';
    std::array<a::SpriteAsset,2> imported;
    constexpr std::array names{"sprites/bloodspray.spr","sprites/blood.spr"};
    constexpr std::array expected_width{64U,16U};
    constexpr std::array expected_frames{10U,9U};
    constexpr std::array<std::array<std::size_t,10>,2> coverage{{
        {276,941,998,1000,922,814,665,631,577,134},
        {35,32,21,36,14,24,21,20,32,0}}};
    for(std::size_t component=0;component<names.size();++component) {
        INFO(names[component]);
        auto source=open_verified(env,primary,names[component],65536);
        imported[component]=import_sprite(source);
        REQUIRE(imported[component].source_data);
        const auto& data=*imported[component].source_data;
        CHECK(data.source_version==2);
        CHECK(data.orientation==a::SpriteOrientation::view_parallel);
        CHECK(data.texture_format==a::SpriteTextureFormat::alpha_test);
        CHECK(data.sync_type==a::SpriteSyncType::random);
        CHECK(data.maximum_width==expected_width[component]);
        CHECK(data.maximum_height==expected_width[component]);
        REQUIRE(data.indexed_frames.size()==expected_frames[component]);
        REQUIRE(data.top_level_entries.size()==expected_frames[component]);
        CHECK(data.groups.empty());
        for(std::size_t frame_index=0;frame_index<data.indexed_frames.size();++frame_index) {
            INFO(frame_index);
            const auto& frame=data.indexed_frames[frame_index];
            CHECK(frame.width==expected_width[component]);CHECK(frame.height==expected_width[component]);
            CHECK(frame.origin.x==-static_cast<int>(expected_width[component]/2));
            CHECK(frame.origin.y==static_cast<int>(expected_width[component]/2));
            CHECK_FALSE(frame.source_group_ordinal);CHECK_FALSE(frame.source_group_frame_ordinal);
            CHECK(data.top_level_entries[frame_index].kind==a::SpriteTopLevelEntryKind::single);
            CHECK(data.top_level_entries[frame_index].first_flattened_frame==frame_index);
            CHECK(data.top_level_entries[frame_index].flattened_frame_count==1);
            const auto covered=check_frame(frame);
            CHECK(covered==coverage[component][frame_index]);
            std::cout<<"blood-original-frame resource="<<names[component]<<" frame="<<frame_index
                <<" width="<<frame.width<<" height="<<frame.height
                <<" origin="<<frame.origin.x<<','<<frame.origin.y<<" covered="<<covered<<'\n';
        }
        CHECK(data.palette.colors[195]!=yellow);
        CHECK(data.indexed_frames.front().derived_rgba8!=data.indexed_frames.back().derived_rgba8);
    }
    contact_sheet(environment_path(L"HLCLIENT_E271_ARTIFACT_DIR"),
        *imported[0].source_data,*imported[1].source_data,yellow);
}
