#include <hlclient/goldsrc/sky_assets.hpp>
#include <hlclient/assets/tga_image.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <algorithm>
#include <chrono>

namespace hlclient::goldsrc {
namespace {
bool safe_name(std::string_view name) {
    return !name.empty() && name.size()<=64 && std::ranges::all_of(name,[](char c) {
        return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-';
    });
}
void rotate_cap(assets::ImageAsset& image,bool upper) {
    const auto source=image.pixels;const auto n=image.width;
    for(std::size_t y=0;y<n;++y)for(std::size_t x=0;x<n;++x) {
        const auto sx=upper ? n-1-y : y,sy=upper ? x : n-1-x;
        std::copy_n(source.data()+(sy*n+sx)*4,4,image.pixels.data()+(y*n+x)*4);
    }
}
}
PreparedSkyAssets prepare_sky_assets(const PreparedSoundResources& resources,
    std::string_view server_name,std::span<const std::byte> entity_lump) {
    PreparedSkyAssets result;
    try {
        std::string fallback;
        if(server_name.empty()) {
            const auto parsed=bsp::GoldSrcEntityDocumentParser::parse_first_entity(entity_lump);
            if(parsed && !parsed.document->empty()) {
                bool worldspawn=false,found=false;
                for(const auto& pair:parsed.document->entities()[0].pairs()) {
                    if(pair.key=="classname" && pair.value=="worldspawn")worldspawn=true;
                    if(pair.key=="skyname") {
                        if(found) {result.status=SkyAssetStatus::invalid_name;return result;}
                        found=true;fallback=pair.value;
                    }
                }
                if(!worldspawn)fallback.clear();
            }
            server_name=fallback;
        }
        if(server_name.empty())return result;
        result.status=SkyAssetStatus::invalid_name;
        if(!safe_name(server_name))return result;
        result.status=SkyAssetStatus::rejected;
        if(!resources.manifest || !resources.environment || !resources.manifest->world_entry() ||
           !resources.manifest->world_entry()->locator())return result;
        const auto& environment=resources.environment;
        const auto root=resources.manifest->world_entry()->locator()->root_id();
        auto sky=std::make_shared<assets::SkyboxAsset>();
        // GoldSrc side names are adapted here, not interpreted by the renderer.
        // The top/bottom source panels require opposite quarter turns to become
        // standard cube faces under the neutral Z-up->cube coordinate mapping.
        constexpr std::array<std::string_view,6> suffixes{"rt","lf","up","dn","bk","ft"};
        for(std::size_t i=0;i<suffixes.size();++i) {
            const auto token="gfx/env/"+std::string{server_name}+std::string{suffixes[i]}+".tga";
            const auto name=local_resources::LocalVirtualResourceName::create(token);
            if(!name)return result;
            auto resolved=environment->resolve_exact_root(*name.name,root);
            if(!resolved) {
                result.status=resolved.code==local_resources::LocalResourceResolutionCode::not_found ?
                    SkyAssetStatus::missing : SkyAssetStatus::rejected;
                return result;
            }
            if(resolved.file->file_size()>17U*1024U*1024U)return result;
            auto locator=environment->make_locator(root,*name.name,resolved.file->identity(),resolved.file->file_size());
            resolved.file->close();if(!locator)return result;
            local_assets::LocalAssetSourceOpenLimits limits;
            limits.maximum_source_bytes=17U*1024U*1024U;limits.timeout=std::chrono::seconds{3};
            local_assets::LocalAssetSourceOpener opener;
            auto opened=opener.begin(*locator.locator,environment,limits);
            if(!opened)return result;
            for(std::size_t n=0;n<512 && !opened.operation->result() && !opened.operation->error();++n)
                opened.operation->update(std::chrono::steady_clock::now());
            auto source=opened.operation->take_result();if(!source)return result;
            auto image=assets::decode_tga_image(source->source().bytes());
            if(!image || image->width!=image->height || image->width>1024 ||
               (i && image->width!=sky->faces[0].width))return result;
            // Environment panels are opaque. Do not reuse masked/decal alpha.
            for(std::size_t a=3;a<image->pixels.size();a+=4)image->pixels[a]=std::byte{255};
            if(i==2 || i==3)rotate_cap(*image,i==2);
            sky->faces[i]=std::move(*image);
        }
        result.skybox=std::move(sky);result.status=SkyAssetStatus::ready;
    } catch(...) {result.skybox.reset();result.status=SkyAssetStatus::rejected;}
    return result;
}
}
