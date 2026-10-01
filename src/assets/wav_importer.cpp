#include <hlclient/assets/wav_importer.hpp>
#include <bit>
#include <limits>
namespace hlclient::assets {
namespace {
bool tag(std::span<const std::byte> b, std::size_t p, std::string_view s) {
    if (p > b.size() || s.size() > b.size()-p) return false;
    for (std::size_t i=0;i<s.size();++i)
        if (std::to_integer<unsigned char>(b[p+i]) != static_cast<unsigned char>(s[i])) return false;
    return true;
}
std::uint32_t u32(std::span<const std::byte> b, std::size_t p) {
    return std::to_integer<std::uint32_t>(b[p]) | (std::to_integer<std::uint32_t>(b[p+1])<<8U) |
        (std::to_integer<std::uint32_t>(b[p+2])<<16U) | (std::to_integer<std::uint32_t>(b[p+3])<<24U);
}
std::uint16_t u16(std::span<const std::byte> b, std::size_t p) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(b[p]) | (std::to_integer<unsigned>(b[p+1])<<8U));
}
}
AssetProbeConfidence WavImporter::probe(const AssetProbe& p) const noexcept {
    return tag(p.structural_bytes,0,"RIFF") && tag(p.structural_bytes,8,"WAVE") ? 100U : 0U;
}
AudioAssetResult WavImporter::import(const AssetSource& source) const {
    const auto b=source.bytes();
    const auto fail=[&](std::string reason, AssetErrorCode code=AssetErrorCode::MalformedData) {
        return AudioAssetResult::failure({code,source.virtual_path(),std::string{id()},std::move(reason),{}});
    };
    if (b.size()<12 || b.size()>16U*1024U*1024U || !tag(b,0,"RIFF") || !tag(b,8,"WAVE")) return fail("invalid_riff");
    const std::uint64_t end64=8ULL+u32(b,4);
    if (end64!=b.size()) return fail("riff_bounds");
    std::span<const std::byte> fmt, data, cue, list;
    std::size_t chunks=0;
    for(std::size_t p=12;p<b.size();) {
        if (++chunks>1024 || b.size()-p<8) return fail("chunk_header");
        const std::size_t n=u32(b,p+4);
        if(n>b.size()-p-8 || (n&1U)>b.size()-p-8-n) return fail("chunk_bounds");
        auto body=b.subspan(p+8,n);
        auto bind=[&](std::span<const std::byte>& target) { if(!target.empty() || body.empty()) return false; target=body; return true; };
        if(tag(b,p,"fmt ") && !bind(fmt)) return fail("duplicate_fmt");
        if(tag(b,p,"data") && !bind(data)) return fail("duplicate_data");
        if(tag(b,p,"cue ") && !bind(cue)) return fail("duplicate_cue");
        if(tag(b,p,"LIST") && tag(body,0,"adtl") && !bind(list)) return fail("duplicate_loop_list");
        p+=8+n+(n&1U);
    }
    if(fmt.size()<16 || data.empty()) return fail("missing_pcm_chunks");
    const auto channels=u16(fmt,2), bits=u16(fmt,14), align=u16(fmt,12);
    const auto rate=u32(fmt,4);
    if(u16(fmt,0)!=1 || (channels!=1 && channels!=2) || (bits!=8 && bits!=16)) return fail("unsupported_pcm",AssetErrorCode::UnsupportedFormat);
    if(rate<4000 || rate>192000 || align!=channels*(bits/8U) || u32(fmt,8)!=rate*align || data.size()%align) return fail("invalid_pcm_geometry");
    const auto frames=data.size()/align;
    if(frames>static_cast<std::size_t>(rate)*120U || frames*channels>4U*1024U*1024U) return fail("decoded_size_limit");
    AudioAsset asset;
    asset.identity.source_name=source.virtual_path().generic_string();
    asset.sample_rate=rate; asset.channel_count=channels;
    if(!cue.empty()) {
        if(cue.size()<28 || u32(cue,0)!=1 || cue.size()!=28 || !tag(cue,12,"data") || u32(cue,16)!=0 || u32(cue,20)!=0) return fail("unsupported_cue",AssetErrorCode::UnsupportedFormat);
        const auto start=u32(cue,24); std::uint64_t end=frames;
        bool found=false;
        for(std::size_t p=4;p<list.size();) {
            if(list.size()-p<8) return fail("loop_list_header");
            const std::size_t n=u32(list,p+4);
            if(n>list.size()-p-8 || (n&1U)>list.size()-p-8-n) return fail("loop_list_bounds");
            if(tag(list,p,"ltxt") && n>=12 && tag(list,p+16,"mark")) {
                if(found || u32(list,p+8)!=u32(cue,4)) return fail("loop_cue_identity");
                end=static_cast<std::uint64_t>(start)+u32(list,p+12); found=true;
            }
            p+=8+n+(n&1U);
        }
        if(start>=end || end>frames) return fail("loop_bounds");
        asset.loop=AudioAsset::Loop{start,static_cast<std::uint32_t>(end)};
    } else if(!list.empty()) return fail("loop_without_cue");
    asset.interleaved_samples.resize(frames*channels);
    for(std::size_t i=0;i<asset.interleaved_samples.size();++i)
        asset.interleaved_samples[i]=bits==8 ? (static_cast<float>(std::to_integer<unsigned>(data[i]))-128.0F)/128.0F :
            static_cast<float>(std::bit_cast<std::int16_t>(u16(data,i*2)))/32768.0F;
    return AudioAssetResult::success(std::move(asset));
}
}
