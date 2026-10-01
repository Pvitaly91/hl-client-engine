#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/assets/wav_importer.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace hlclient::goldsrc {
struct ApprovedSoundAssets::Impl {
    PreparedSoundResources resources;
    struct Entry {bool used{},requested{}; std::uint16_t index{}; SoundAssetResult result; std::optional<game_api::LocalSoundReference> local;};
    std::array<Entry,512> entries{};
    std::mutex mutex;
    std::condition_variable_any wake;
    std::size_t bytes{};
    bool available{};
    std::jthread worker;
    SoundAssetResult load_local(const game_api::LocalSoundReference& reference,std::stop_token stop) {
        if(!valid_local_sound_reference(reference)) return {SoundAssetStatus::not_authorized,{}};
        if(!resources.manifest || !resources.environment) return {SoundAssetStatus::missing,{}};
        const auto* world=resources.manifest->world_entry();
        if(!world || !world->locator()) return {SoundAssetStatus::missing,{}};
        auto name=local_resources::LocalVirtualResourceName::create("sound/"+std::string{reference.name()});
        if(!name) return {SoundAssetStatus::not_authorized,{}};
        // Same approved installation/root as the selected world, never a second
        // game installation or an invented server sound index.
        auto resolved=resources.environment->resolve_exact_root(*name.name,world->locator()->root_id());
        if(!resolved) return {SoundAssetStatus::missing,{}};
        auto locator=resources.environment->make_locator(resolved.file->root_id(),*name.name,
            resolved.file->identity(),resolved.file->file_size());
        resolved.file->close();
        if(!locator) return {SoundAssetStatus::not_authorized,{}};
        local_assets::LocalAssetSourceOpener opener;
        local_assets::LocalAssetSourceOpenLimits limits; limits.maximum_source_bytes=16U*1024U*1024U;
        limits.timeout=std::chrono::seconds{2};
        auto opened=opener.begin(*locator.locator,resources.environment,limits);
        if(!opened) return {SoundAssetStatus::open_failed,{}};
        for(std::size_t i=0;i<65536 && !opened.operation->result() && !opened.operation->error();++i) {
            if(stop.stop_requested()) {opened.operation->cancel(); return {SoundAssetStatus::missing,{}};}
            opened.operation->update(std::chrono::steady_clock::now());
        }
        auto source=opened.operation->take_result();
        if(!source) return {SoundAssetStatus::open_failed,{}};
        auto decoded=assets::WavImporter{}.import(source->source());
        if(!decoded) return {SoundAssetStatus::decode_failed,{}};
        auto asset=std::make_shared<const assets::AudioAsset>(std::move(decoded).value());
        const auto n=asset->interleaved_samples.size()*sizeof(float);
        if(n>32U*1024U*1024U-bytes) return {SoundAssetStatus::limit,{}};
        bytes+=n; return {SoundAssetStatus::ready,std::move(asset)};
    }
    SoundAssetResult load(std::uint16_t index,std::stop_token stop) {
        if(!resources.manifest || !resources.environment) return {SoundAssetStatus::missing,{}};
        const auto* entry=resources.manifest->find(ResourceType::sound,index);
        if(!entry) return {SoundAssetStatus::missing,{}};
        if(!entry->locator()) return {SoundAssetStatus::missing,{}};
        // The generic resolver owns path safety. Audio additionally rejects
        // engine markers and already-prefixed wire names; never repair these
        // into a different resource or open them as ordinary filenames.
        const auto path=entry->locator()->virtual_name().value();
        if(!path.starts_with("sound/") || path.size()<=6) return {SoundAssetStatus::unsupported,{}};
        const auto name=path.substr(6);
        if(name.front()=='!' || name.front()=='#' || name.front()=='*' || name.front()=='?')
            return {SoundAssetStatus::unsupported,{}};
        if(name.size()>=6) {
            bool doubled=true;
            constexpr std::string_view prefix="sound/";
            for(std::size_t i=0;i<prefix.size();++i) {
                const auto c=name[i]>='A' && name[i]<='Z' ? static_cast<char>(name[i]+('a'-'A')) : name[i];
                doubled=doubled && c==prefix[i];
            }
            if(doubled) return {SoundAssetStatus::unsupported,{}};
        }
        auto plan=AssetDispatchPlanBuilder{}.build(*resources.manifest,*entry);
        if(!plan || plan.plan->role()!=assets::AssetDispatchRole::audio) return {SoundAssetStatus::unsupported,{}};
        ApprovedAssetSourceOpenLimits limits;
        limits.maximum_source_bytes=16U*1024U*1024U;
        limits.timeout=std::chrono::seconds{2};
        ApprovedAssetSourceOpener opener;
        auto opened=opener.begin(*plan.plan,resources.environment,limits);
        if(!opened) return {SoundAssetStatus::open_failed,{}};
        for(std::size_t i=0;i<65536 && !opened.operation->result() && !opened.operation->error();++i) {
            if(stop.stop_requested()) {opened.operation->cancel(); return {SoundAssetStatus::missing,{}};}
            opened.operation->update(std::chrono::steady_clock::now());
        }
        auto source=opened.operation->take_result();
        if(!source) return {SoundAssetStatus::open_failed,{}};
        assets::AssetImporterRegistries registry;
        if(!registry.audio.register_importer(std::make_unique<assets::WavImporter>(),100)) return {SoundAssetStatus::unsupported,{}};
        auto decoded=ApprovedAssetImporterDispatcher{registry}.dispatch(*source,*plan.plan);
        if(!decoded.imported() || !std::holds_alternative<assets::AudioAsset>(*decoded.asset)) return {SoundAssetStatus::decode_failed,{}};
        auto asset=std::make_shared<const assets::AudioAsset>(std::get<assets::AudioAsset>(std::move(*decoded.asset)));
        const auto n=asset->interleaved_samples.size()*sizeof(float);
        if(n>32U*1024U*1024U-bytes) return {SoundAssetStatus::limit,{}};
        bytes+=n;
        return {SoundAssetStatus::ready,std::move(asset)};
    }
    void run(std::stop_token stop) {
        while(!stop.stop_requested()) {
            std::size_t slot=entries.size(); std::uint16_t index{};
            std::optional<game_api::LocalSoundReference> local;
            {
                std::unique_lock lock{mutex};
                wake.wait(lock,stop,[&]{for(const auto& e:entries) if(e.requested) return true; return false;});
                if(stop.stop_requested()) break;
                for(std::size_t i=0;i<entries.size();++i) if(entries[i].requested) {slot=i; entries[i].requested=false; index=entries[i].index; local=entries[i].local; break;}
            }
            if(slot==entries.size()) continue;
            SoundAssetResult result;
            try {result=local ? load_local(*local,stop) : load(index,stop);} catch(...) {result.status=SoundAssetStatus::unsupported;}
            {std::lock_guard lock{mutex}; entries[slot].result=std::move(result);}
        }
    }
    ~Impl() {if(worker.joinable()) {worker.request_stop(); wake.notify_all(); worker.join();}}
};
ApprovedSoundAssets::ApprovedSoundAssets(PreparedSoundResources r):impl_{std::make_unique<Impl>()} {
    impl_->resources=std::move(r);
    try {impl_->worker=std::jthread{[p=impl_.get()](std::stop_token stop){p->run(stop);}}; impl_->available=true;}
    catch (...) {impl_->available=false;}
}
ApprovedSoundAssets::~ApprovedSoundAssets()=default;
std::string_view ApprovedSoundAssets::virtual_name(std::uint16_t index) const noexcept {
    if(!impl_->resources.manifest) return {};
    const auto* entry=impl_->resources.manifest->find(ResourceType::sound,index);
    return entry && entry->locator() ? entry->locator()->virtual_name().value() : std::string_view{};
}
SoundAssetResult ApprovedSoundAssets::request(std::uint16_t index) noexcept {
    if(!impl_->available) return {SoundAssetStatus::limit,{}};
    std::unique_lock lock{impl_->mutex,std::try_to_lock};
    if(!lock) return {};
    for(const auto& e:impl_->entries) if(e.used && !e.local && e.index==index) return e.result;
    for(auto& e:impl_->entries) if(!e.used) {e.used=e.requested=true; e.index=index; impl_->wake.notify_one(); return {};}
    return {SoundAssetStatus::limit,{}};
}
SoundAssetResult ApprovedSoundAssets::request_local(const game_api::LocalSoundReference& r) noexcept {
    if(!valid_local_sound_reference(r)) return {SoundAssetStatus::not_authorized,{}};
    if(!impl_->available) return {SoundAssetStatus::limit,{}};
    std::unique_lock lock{impl_->mutex,std::try_to_lock};
    if(!lock) return {};
    std::size_t locals=0;
    for(const auto& e:impl_->entries) if(e.used && e.local) {
        ++locals;
        // Same local sample shares PCM across model/profile references, not voices.
        if(e.local->name()==r.name()) return e.result;
    }
    if(locals>=64) return {SoundAssetStatus::limit,{}};
    for(auto& e:impl_->entries) if(!e.used) {
        e.used=e.requested=true; e.local=r; impl_->wake.notify_one(); return {};
    }
    return {SoundAssetStatus::limit,{}};
}
}
