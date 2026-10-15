#include <hlclient/goldsrc/player_model_assets.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <limits>
#include <type_traits>

namespace hlclient::goldsrc {
namespace {
using Status=game_api::PlayerAppearanceStatus;
constexpr std::uint64_t maximum_source=16U*1024U*1024U,maximum_bundle=32U*1024U*1024U,
    maximum_total=128U*1024U*1024U;
PreparedPlayerModel failed(Status status) { PreparedPlayerModel result;result.status=status;return result; }
}
bool valid_player_model_virtual_name(std::string_view name) noexcept {
    constexpr std::string_view prefix="models/player/";
    if(!name.starts_with(prefix) || !name.ends_with(".mdl") || name.size()>prefix.size()+32U*2U+5U)return false;
    const auto tail=name.substr(prefix.size());const auto separator=tail.find('/');
    if(separator==std::string_view::npos || separator==0 || separator>32U)return false;
    const auto token=tail.substr(0,separator);
    const auto leaf=tail.substr(separator+1U);
    if(leaf.size()!=token.size()+4U || leaf.substr(0,token.size())!=token)return false;
    return std::all_of(token.begin(),token.end(),[](unsigned char c){
        return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-';
    });
}
std::uint64_t retained_player_model_bytes(const assets::ModelAsset& model) noexcept {
    std::uint64_t total=sizeof(model);
    const auto add=[&](std::uint64_t amount){total=amount>std::numeric_limits<std::uint64_t>::max()-total
        ? std::numeric_limits<std::uint64_t>::max() : total+amount;};
    const auto vector=[&](const auto& values){add(static_cast<std::uint64_t>(values.capacity())*sizeof(typename std::decay_t<decltype(values)>::value_type));};
    const auto string=[&](const std::string& value){add(value.capacity()+1U);};
    string(model.identity.source_name);vector(model.vertices);vector(model.indices);
    if(!model.skeletal_data)return total;
    const auto& d=*model.skeletal_data;add(sizeof(d));
    vector(d.bones);for(const auto& v:d.bones)string(v.name);
    vector(d.bone_controllers);vector(d.hitboxes);vector(d.attachments);for(const auto& v:d.attachments)string(v.name);
    vector(d.bodyparts);for(const auto& v:d.bodyparts){string(v.name);vector(v.submodel_indices);}
    vector(d.submodels);for(const auto& v:d.submodels){string(v.name);vector(v.vertices);vector(v.indices);vector(v.meshes);}
    vector(d.textures);for(const auto& v:d.textures){string(v.source_name);vector(v.rgba8_level_zero);vector(v.indexed_pixels);}
    vector(d.skin_families);for(const auto& v:d.skin_families)vector(v.texture_indices);
    vector(d.sequences);for(const auto& v:d.sequences) {
        string(v.label);vector(v.events);for(const auto& event:v.events)vector(event.options);
        vector(v.pivots);vector(v.animation_blends);
        for(const auto& blend:v.animation_blends) {
            vector(blend.bone_tracks);for(const auto& track:blend.bone_tracks)for(const auto& channel:track.channels) {
                vector(channel.runs);for(const auto& run:channel.runs)vector(run.quantized_values);
            }
        }
    }
    vector(d.sequence_groups);for(const auto& group:d.sequence_groups){string(group.label);string(group.untrusted_source_name);}
    vector(d.transition_table);return total;
}
std::shared_ptr<const assets::ModelAsset> remap_player_model_palette(const assets::ModelAsset& source,
    std::span<const game_api::PlayerPaletteRange> ranges) {
    if(!source.skeletal_data || ranges.size()>256U)return {};
    if(ranges.empty())return std::make_shared<const assets::ModelAsset>(source);
    auto data=std::make_shared<assets::SkeletalModelAssetData>(*source.skeletal_data);
    for(const auto& range:ranges) {
        if(range.texture>=data->textures.size() || range.first>range.last || range.hue_degrees>360U)return {};
        auto& texture=data->textures[range.texture];
        if(texture.width==0 || texture.height==0 || texture.width>4096U || texture.height>4096U ||
            texture.indexed_pixels.size()!=static_cast<std::size_t>(texture.width)*texture.height ||
            texture.rgba8_level_zero.size()!=texture.indexed_pixels.size()*4U)return {};
        for(unsigned index=range.first;index<=range.last;++index) {
            const auto original=texture.palette_rgb[index];
            const float maximum=static_cast<float>(*std::max_element(original.begin(),original.end()));
            const float minimum=static_cast<float>(*std::min_element(original.begin(),original.end()));
            const float chroma=maximum-minimum, h=static_cast<float>(range.hue_degrees%360U)/60.0F;
            const float x=chroma*(1.0F-std::abs(std::fmod(h,2.0F)-1.0F));
            std::array<float,3> rgb{};
            if(h<1)rgb={chroma,x,0};else if(h<2)rgb={x,chroma,0};else if(h<3)rgb={0,chroma,x};
            else if(h<4)rgb={0,x,chroma};else if(h<5)rgb={x,0,chroma};else rgb={chroma,0,x};
            for(std::size_t c=0;c<3;++c)texture.palette_rgb[index][c]=static_cast<std::uint8_t>(
                std::clamp(std::lround(rgb[c]+minimum),0L,255L));
        }
        for(std::size_t pixel=0;pixel<texture.indexed_pixels.size();++pixel) {
            const auto index=texture.indexed_pixels[pixel];
            if(index<range.first || index>range.last)continue;
            for(std::size_t c=0;c<3;++c)texture.rgba8_level_zero[pixel*4U+c]=static_cast<std::byte>(texture.palette_rgb[index][c]);
            // Coverage is copied unchanged, including masked index255.
        }
    }
    auto result=std::make_shared<assets::ModelAsset>(source);result->skeletal_data=std::move(data);return result;
}
PreparedPlayerModel import_player_model_source(const local_assets::LocalAssetSource& source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment> environment,std::stop_token stop) {
    if(!valid_player_model_virtual_name(source.source().virtual_path().generic_string()) || source.byte_count()>maximum_source)
        return failed(Status::not_authorized);
    assets::AssetImporterRegistries registries;
    if(!register_builtin_asset_importers(registries))return failed(Status::asset_import_failed);
    visual_assets::GoldSrcVisualAssetImportLimits limits;
    limits.studio_bundle.bundle.maximum_total_source_bytes=maximum_bundle;
    limits.studio_bundle.companion_source_open.maximum_source_bytes=maximum_source;
    limits.studio_bundle.timeout=std::chrono::seconds{5};
    auto operation=visual_assets::GoldSrcVisualAssetImportOperation::begin(source,environment,registries,limits);
    if(!operation)return failed(Status::asset_import_failed);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    for(std::size_t i=0;i<65536U && !operation.operation->terminal();++i) {
        if(stop.stop_requested() || std::chrono::steady_clock::now()>=deadline) {operation.operation->cancel();break;}
        operation.operation->update(std::chrono::steady_clock::now());
    }
    auto imported=operation.operation->take_result();
    if(!imported)return failed(Status::asset_import_failed);
    const auto* model=std::get_if<assets::ModelAsset>(&imported->asset());
    if(!model || !model->skeletal_data || imported->source_fingerprints().empty())return failed(Status::asset_import_failed);
    PreparedPlayerModel result;
    result.model=std::make_shared<const assets::ModelAsset>(*model);
    result.revision={14695981039346656037ULL,1099511628211ULL};
    for(const auto& fingerprint:imported->source_fingerprints()) {
        result.revision.primary=(result.revision.primary^fingerprint.primary)*1099511628211ULL;
        result.revision.secondary=(result.revision.secondary^fingerprint.secondary)*1099511628211ULL;
    }
    if(!result.revision.primary && !result.revision.secondary)result.revision.primary=1;
    result.source_bytes=std::max(source.byte_count(),imported->dependency_statistics().total_source_bytes);
    result.status=Status::selected_model_ready;
    return result;
}
struct ApprovedPlayerModelAssets::Impl {
    PreparedSoundResources resources;
    std::mutex mutex;std::condition_variable_any wake;
    std::map<std::string,PreparedPlayerModel,std::less<>> cache;
    std::deque<std::string> pending;
    std::uint64_t total_bytes{};
    std::jthread worker;
    explicit Impl(PreparedSoundResources value):resources(std::move(value)),worker([this](std::stop_token stop){run(stop);}){}
    ~Impl(){worker.request_stop();wake.notify_all();}
    PreparedPlayerModel load(std::string_view name,std::stop_token stop) {
        if(!resources.manifest || !resources.environment || !resources.manifest->world_entry() ||
            !resources.manifest->world_entry()->locator())return failed(Status::not_authorized);
        auto virtual_name=local_resources::LocalVirtualResourceName::create(std::string(name));
        if(!virtual_name)return failed(Status::not_authorized);
        auto resolved=resources.environment->resolve_exact_root(*virtual_name.name,
            resources.manifest->world_entry()->locator()->root_id());
        if(!resolved)return failed(resolved.code==local_resources::LocalResourceResolutionCode::not_found
            ? Status::approved_asset_missing : Status::not_authorized);
        if(resolved.file->file_size()>maximum_source){resolved.file->close();return failed(Status::resource_limit);}
        auto locator=resources.environment->make_locator(resolved.file->root_id(),*virtual_name.name,
            resolved.file->identity(),resolved.file->file_size());resolved.file->close();
        if(!locator)return failed(Status::not_authorized);
        local_assets::LocalAssetSourceOpener opener;local_assets::LocalAssetSourceOpenLimits limits;
        limits.maximum_source_bytes=maximum_source;limits.timeout=std::chrono::seconds{3};
        auto opened=opener.begin(*locator.locator,resources.environment,limits);
        if(!opened)return failed(Status::asset_import_failed);
        for(std::size_t i=0;i<65536U && !opened.operation->result() && !opened.operation->error();++i) {
            if(stop.stop_requested()){opened.operation->cancel();break;}
            opened.operation->update(std::chrono::steady_clock::now());
        }
        auto source=opened.operation->take_result();
        return source ? import_player_model_source(*source,resources.environment,stop) : failed(Status::asset_import_failed);
    }
    void run(std::stop_token stop) noexcept {
        while(!stop.stop_requested()) {
            std::string name;
            {std::unique_lock lock(mutex);if(!wake.wait(lock,stop,[&]{return !pending.empty();}))return;
             name=std::move(pending.front());pending.pop_front();}
            PreparedPlayerModel result;
            try {result=load(name,stop);}catch(...){result=failed(Status::asset_import_failed);}
            std::lock_guard lock(mutex);
            if(result.model) {
                const auto retained=retained_player_model_bytes(*result.model);
                if(retained>maximum_total-total_bytes)result=failed(Status::resource_limit);
                else total_bytes+=retained;
            }
            cache[name]=std::move(result);
        }
    }
};
ApprovedPlayerModelAssets::ApprovedPlayerModelAssets(PreparedSoundResources resources):impl_(std::make_unique<Impl>(std::move(resources))){}
ApprovedPlayerModelAssets::~ApprovedPlayerModelAssets()=default;
PreparedPlayerModel ApprovedPlayerModelAssets::request(std::string_view name) noexcept {
    try {
        if(!valid_player_model_virtual_name(name))return failed(Status::not_authorized);
        std::lock_guard lock(impl_->mutex);
        if(const auto found=impl_->cache.find(name);found!=impl_->cache.end())return found->second;
        if(impl_->cache.size()>=kMaximumPlayerModelRequests)return failed(Status::resource_limit);
        impl_->cache.emplace(std::string(name),PreparedPlayerModel{});impl_->pending.emplace_back(name);
        impl_->wake.notify_one();return {};
    }catch(...){return failed(Status::resource_limit);}
}
}
