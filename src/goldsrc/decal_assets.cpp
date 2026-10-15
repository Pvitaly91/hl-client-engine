#include <hlclient/goldsrc/decal_assets.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <algorithm>
#include <chrono>
#include <string>

namespace hlclient::goldsrc {
namespace {
constexpr std::size_t kMaximumArchiveBytes=4U*1024U*1024U;
constexpr std::size_t kMaximumDecodedBytes=8U*1024U*1024U;
template<std::size_t N>
std::optional<std::string_view> token(const std::array<char,N>& value) noexcept {
    const auto end=std::find(value.begin(),value.end(),'\0');
    if(end==value.end() || end==value.begin())return {};
    return std::string_view{value.data(),static_cast<std::size_t>(end-value.begin())};
}
bool valid_wad(std::string_view value) noexcept {
    return value.size()>4 && value.ends_with(".wad") && value.find("..")==value.npos &&
        std::ranges::all_of(value,[](char c){return (c>='a'&&c<='z') ||
            (c>='0'&&c<='9') || c=='_' || c=='.';});
}
bool valid_texture(std::string_view value) noexcept {
    return !value.empty() && value.size()<16 && std::ranges::all_of(value,[](char c){
        return c>=0x21 && c<=0x7e && c!='/' && c!='\\' && c!=':';});
}
DecalAssetStatus resolution_status(local_resources::LocalResourceResolutionCode code) noexcept {
    using C=local_resources::LocalResourceResolutionCode;
    if(code==C::not_found)return DecalAssetStatus::absent;
    if(code==C::too_large)return DecalAssetStatus::capacity;
    if(code==C::io_error)return DecalAssetStatus::open_failed;
    return DecalAssetStatus::not_authorized;
}
void audit_record(DecalAssetAudit& audit,std::span<const std::byte> record,
    const indexed_texture::GoldSrcParsedMiptex& metadata) noexcept {
    audit.source_lump_digest=hash::sha256(record);audit.source_lump_bytes=record.size();
    const auto palette=*metadata.palette_byte_offset;
    for(std::size_t i=0;i<256;++i)
        for(std::size_t c=0;c<3;++c)audit.palette[i][c]=std::to_integer<std::uint8_t>(record[palette+i*3+c]);
    for(std::size_t i=0;i<4;++i) {
        const auto& source=metadata.mip_levels[i];auto& mip=audit.mips[i];
        mip.width=source.width;mip.height=source.height;mip.minimum_index=255;
        std::array<bool,256> seen{};
        for(const auto pixel:record.subspan(source.byte_offset,source.byte_count)) {
            const auto index=std::to_integer<std::uint8_t>(pixel);
            mip.minimum_index=std::min(mip.minimum_index,index);mip.maximum_index=std::max(mip.maximum_index,index);
            if(index==0)++mip.index_zero_count;if(index==255)++mip.index_255_count;
            if(!seen[index]){seen[index]=true;++mip.unique_indices;}
        }
    }
}
}
std::string_view to_string(DecalAssetStatus status) noexcept {
    switch(status) {
    case DecalAssetStatus::ready:return "ready";
    case DecalAssetStatus::pending:return "pending";
    case DecalAssetStatus::absent:return "absent";
    case DecalAssetStatus::not_authorized:return "not_authorized";
    case DecalAssetStatus::open_failed:return "open_failed";
    case DecalAssetStatus::decode_failed:return "decode_failed";
    case DecalAssetStatus::material_rejected:return "material_rejected";
    case DecalAssetStatus::capacity:return "capacity";
    }
    return "not_authorized";
}
PreparedDecalAssets prepare_decal_assets(const PreparedSoundResources& resources,
    std::span<const game_api::DecalAssetRequest> requests) noexcept {
    PreparedDecalAssets result;
    result.count=std::min(requests.size(),result.entries.size());
    const auto fail_all=[&](DecalAssetStatus status) {
        result.archive_status=status;
        result.retained_decoded_bytes=0;
        for(std::size_t i=0;i<result.count;++i) {
            result.entries[i].status=status;
            result.entries[i].texture.reset();
        }
    };
    if(requests.size()>result.entries.size()){fail_all(DecalAssetStatus::capacity);return result;}
    if(requests.empty()){result.archive_status=DecalAssetStatus::ready;return result;}
    try {
        const auto wad=token(requests.front().wad_name);
        if(!wad || !valid_wad(*wad)){fail_all(DecalAssetStatus::not_authorized);return result;}
        for(const auto& request:requests) {
            const auto archive=token(request.wad_name),name=token(request.texture_name);
            if(!archive || *archive!=*wad || !name || !valid_texture(*name) ||
                request.interpretation>game_api::DecalImageInterpretation::palette_tint_index_coverage ||
                request.material_mode!=game_api::LocalDecalMaterialMode::lit_straight_alpha) {
                fail_all(DecalAssetStatus::not_authorized);return result;
            }
        }
        const auto* world=resources.manifest ? resources.manifest->world_entry() : nullptr;
        if(!world || !world->locator() || !resources.environment){fail_all(DecalAssetStatus::absent);return result;}
        const auto name=local_resources::LocalVirtualResourceName::create(std::string{*wad});
        if(!name){fail_all(DecalAssetStatus::not_authorized);return result;}
        auto resolved=resources.environment->resolve_exact_root(*name.name,world->locator()->root_id());
        if(!resolved){fail_all(resolution_status(resolved.code));return result;}
        if(resolved.file->file_size()>kMaximumArchiveBytes){fail_all(DecalAssetStatus::capacity);return result;}
        auto locator=resources.environment->make_locator(resolved.file->root_id(),*name.name,
            resolved.file->identity(),resolved.file->file_size());
        resolved.file->close();
        if(!locator){fail_all(DecalAssetStatus::not_authorized);return result;}
        local_assets::LocalAssetSourceOpenLimits limits;
        limits.maximum_source_bytes=kMaximumArchiveBytes;limits.timeout=std::chrono::seconds{2};
        auto opened=local_assets::LocalAssetSourceOpener{}.begin(*locator.locator,resources.environment,limits);
        if(!opened){fail_all(DecalAssetStatus::open_failed);return result;}
        // Four MiB permits at most64 ordinary64KiB updates plus validation;
        // bounded stop is a startup failure, never a late contact retry.
        for(std::size_t update=0;update<128 && !opened.operation->result() && !opened.operation->error();++update)
            opened.operation->update(std::chrono::steady_clock::now());
        auto source=opened.operation->take_result();
        if(!source){fail_all(DecalAssetStatus::open_failed);return result;}
        const auto bytes=source->source().bytes();result.source_bytes=bytes.size();result.source_digest=hash::sha256(bytes);
        const auto catalog=wad3::GoldSrcWad3CatalogParser::parse(bytes,{kMaximumArchiveBytes,4096});
        if(!catalog){fail_all(DecalAssetStatus::decode_failed);return result;}
        result.archive_status=DecalAssetStatus::ready;
        indexed_texture::GoldSrcIndexedTextureLimits texture_limits;
        texture_limits.maximum_dimension=256;texture_limits.maximum_level_zero_texels=256U*256U;
        texture_limits.maximum_decoded_rgba_bytes=256U*256U*4U*2U;
        for(std::size_t slot=0;slot<requests.size();++slot) {
            auto& entry=result.entries[slot];const auto& request=requests[slot];
            const auto texture_name=*token(request.texture_name);
            // WAD texture identity is the existing bounded ASCII case-insensitive
            // name contract. The catalog rejects ambiguous normalized names;
            // neither directory ordinal nor an unrelated variant is a fallback.
            const auto* lump=catalog.catalog->find_miptex(texture_name);
            if(!lump){entry.status=DecalAssetStatus::absent;continue;}
            std::copy_n(lump->source_name.begin(),std::min(lump->source_name.size(),
                entry.audit.resolved_source_name.size()-1),entry.audit.resolved_source_name.begin());
            entry.audit.source_lump_type=lump->type;
            entry.audit.exact_spelling_match=lump->source_name==texture_name;
            wad3::GoldSrcWad3TextureRequest decode_request;decode_request.expected_texture_name=texture_name;
            const bool tint_coverage=request.interpretation==game_api::DecalImageInterpretation::palette_tint_index_coverage;
            if(tint_coverage)decode_request.interpretation=
                indexed_texture::GoldSrcIndexedTextureInterpretation::palette_tint_index_coverage;
            const auto parsed=wad3::GoldSrcWad3TextureParser::parse(bytes,*lump,decode_request,texture_limits);
            if(!parsed){entry.status=DecalAssetStatus::decode_failed;continue;}
            const auto& prepared=*parsed.texture;
            audit_record(entry.audit,bytes.subspan(prepared.record_byte_offset,prepared.record_byte_count),prepared.miptex);
            auto decoded=wad3::GoldSrcWad3TextureDecoder::decode(bytes,*lump,decode_request,texture_limits);
            if(!decoded){entry.status=tint_coverage ? DecalAssetStatus::material_rejected : DecalAssetStatus::decode_failed;continue;}
            if(decoded.texture->alpha_mode!=(tint_coverage ? assets::WorldTextureAlphaMode::straight_alpha :
                assets::WorldTextureAlphaMode::masked_index_255)) {
                entry.status=DecalAssetStatus::material_rejected;continue;
            }
            std::size_t decoded_bytes{};
            for(const auto& mip:decoded.texture->mip_levels)decoded_bytes+=mip.rgba_pixels.size();
            if(decoded_bytes>kMaximumDecodedBytes-result.retained_decoded_bytes) {
                entry.status=DecalAssetStatus::capacity;continue;
            }
            entry.texture=std::make_shared<const assets::WorldTextureAsset>(std::move(*decoded.texture));
            result.retained_decoded_bytes+=decoded_bytes;entry.status=DecalAssetStatus::ready;
        }
    } catch(...) {fail_all(DecalAssetStatus::decode_failed);}
    return result;
}
} // namespace hlclient::goldsrc
