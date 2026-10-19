#include <hlclient/goldsrc/console_background.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <chrono>

namespace hlclient::goldsrc {
std::optional<assets::ImageAsset> decode_console_lmp_image(const std::span<const std::byte> bytes) {
    if(bytes.size()<8U)return std::nullopt;
    const auto u32=[&](std::size_t p){std::uint32_t v{};for(unsigned i=0;i<4;++i)
        v|=std::uint32_t(std::to_integer<unsigned char>(bytes[p+i]))<<(8U*i);return v;};
    const auto width=u32(0),height=u32(4);
    if(!width||!height||width>2048U||height>2048U)return std::nullopt;
    const auto count=std::size_t(width)*height,palette=8U+count;
    if(bytes.size()!=palette+2U+768U||bytes[palette]!=std::byte{0}||bytes[palette+1U]!=std::byte{1})
        return std::nullopt;
    assets::ImageAsset image;image.width=width;image.height=height;image.pixels.resize(count*4U);
    for(std::size_t i=0;i<count;++i) {
        const auto color=palette+2U+std::to_integer<unsigned char>(bytes[8U+i])*3U;
        for(std::size_t c=0;c<3;++c)image.pixels[i*4U+c]=bytes[color+c];
        // Console background is opaque. Index255 is not masked coverage here.
        image.pixels[i*4U+3]=std::byte{255};
    }
    return image;
}

PreparedConsoleBackground prepare_console_background(const std::filesystem::path& base,
    const std::string_view game,const std::string_view virtual_name,const std::stop_token stop) {
    PreparedConsoleBackground result;
    try {
        if(stop.stop_requested()){result.status=ConsoleBackgroundStatus::cancelled;return result;}
        const auto name=local_resources::LocalVirtualResourceName::create(virtual_name);
        if(!name){result.status=ConsoleBackgroundStatus::rejected;return result;}
        auto roots=local_resources::LocalResourceSearchRoots::create(base,game);
        if(!roots){result.status=ConsoleBackgroundStatus::invalid_source;return result;}
        const auto root=roots.roots->metadata(0);if(!root)return result;
        auto created=local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));
        if(!created){result.status=ConsoleBackgroundStatus::invalid_source;return result;}
        const std::shared_ptr<const local_resources::LocalResourceEnvironment> environment{std::move(created.environment)};
        auto resolved=environment->resolve_exact_root(*name.name,root->id);
        if(!resolved) {
            result.status=resolved.code==local_resources::LocalResourceResolutionCode::not_found
                ?ConsoleBackgroundStatus::missing:ConsoleBackgroundStatus::rejected;return result;
        }
        constexpr std::size_t maximum_bytes=2048U*2048U+778U;
        if(resolved.file->file_size()>maximum_bytes){result.status=ConsoleBackgroundStatus::rejected;return result;}
        auto locator=environment->make_locator(root->id,*name.name,resolved.file->identity(),resolved.file->file_size());
        resolved.file->close();if(!locator){result.status=ConsoleBackgroundStatus::rejected;return result;}
        local_assets::LocalAssetSourceOpenLimits limits;
        limits.maximum_source_bytes=maximum_bytes;limits.timeout=std::chrono::seconds{3};
        local_assets::LocalAssetSourceOpener opener;
        auto opened=opener.begin(*locator.locator,environment,limits);
        if(!opened){result.status=ConsoleBackgroundStatus::rejected;return result;}
        for(std::size_t n=0;n<512&&!opened.operation->result()&&!opened.operation->error();++n) {
            if(stop.stop_requested()){result.status=ConsoleBackgroundStatus::cancelled;return result;}
            opened.operation->update(std::chrono::steady_clock::now());
        }
        auto source=opened.operation->take_result();
        if(!source){result.status=ConsoleBackgroundStatus::rejected;return result;}
        auto image=decode_console_lmp_image(source->source().bytes());
        if(!image){result.status=ConsoleBackgroundStatus::rejected;return result;}
        result.image=std::make_shared<const assets::ImageAsset>(std::move(*image));
        result.status=ConsoleBackgroundStatus::ready;
    }catch(...){result.image.reset();result.status=ConsoleBackgroundStatus::rejected;}
    return result;
}
}
