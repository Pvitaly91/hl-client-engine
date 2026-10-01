#include <hlclient/audio/output.hpp>
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
namespace hlclient::audio {
struct Playback::Impl {
    SDL_AudioStream* stream{};
    bool initialized{};
    std::string_view status{"disabled"};
    std::mutex mutex;
    // Single application producer / audio consumer. No main-thread wait or
    // allocation, including while the consumer snapshots listener state.
    std::array<Command,513> commands{};
    std::atomic<std::size_t> head{},tail{};
    Listener listener;
    std::atomic<bool> muted{true};
    std::atomic<bool> overflow{false};
    std::atomic<std::uint64_t> drops{},failures{},frames{},started{},stopped{},updated{},rejected{};
    std::atomic<std::uint32_t> queued{};
    std::atomic<std::uint64_t> presentation_started{};
    std::jthread worker;
    void run(std::stop_token stop) {
        Mixer mixer;
        std::array<Command,512> batch{};
        std::array<float,480*2> pcm{}; // 10 ms, at most 20 ms software queue
        while(!stop.stop_requested()) {
            std::size_t count=0;
            {
                std::lock_guard lock{mutex};
                auto current=listener;
                current.muted=muted.load(std::memory_order_relaxed);
                mixer.listener(current);
            }
            auto read=tail.load(std::memory_order_relaxed);
            const auto write=head.load(std::memory_order_acquire);
            while(read!=write && count<batch.size()) {
                batch[count++]=std::move(commands[read]);
                read=(read+1)%commands.size();
            }
            tail.store(read,std::memory_order_release);
            if(overflow.exchange(false)) {
                mixer.command(Command{Operation::reset});
                SDL_ClearAudioStream(stream);
                // A dropped stop must never leave a charging loop running.
                for(std::size_t i=0;i<count;++i) batch[i]={};
                count=0;
            }
            for(std::size_t i=0;i<count;++i) {mixer.command(batch[i]); batch[i]={};}
            const auto bytes=SDL_GetAudioStreamQueued(stream);
            if(bytes<0) {++failures; break;}
            queued=static_cast<std::uint32_t>(bytes/8);
            if(bytes<480*8) {
                mixer.render(pcm);
                if(!SDL_PutAudioStreamData(stream,pcm.data(),static_cast<int>(pcm.size()*sizeof(float)))) {++failures; break;}
                const auto& s=mixer.statistics(); frames=s.frames; started=s.started; stopped=s.stopped; updated=s.updated; rejected=s.rejected;
                presentation_started=s.presentation_started;
            } else std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }
    ~Impl() {
        if(worker.joinable()) {worker.request_stop(); worker.join();}
        if(stream) SDL_DestroyAudioStream(stream); // closes owned playback device
        if(initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};
Playback::Playback(bool enabled):impl_{std::make_unique<Impl>()} {
    if(!enabled) return;
    impl_->status="audio_unavailable";
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    impl_->initialized=true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32,2,static_cast<int>(output_rate)};
    impl_->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(!impl_->stream || !SDL_ResumeAudioStreamDevice(impl_->stream)) return;
    impl_->status="playback_ready";
    try {impl_->worker=std::jthread{[p=impl_.get()](std::stop_token stop){p->run(stop);}};}
    catch (...) {impl_->status="audio_unavailable";}
}
Playback::~Playback()=default;
bool Playback::submit(const Command& c) noexcept {
    if(impl_->status!="playback_ready" || impl_->failures.load()!=0) return false;
    const auto write=impl_->head.load(std::memory_order_relaxed);
    const auto next=(write+1)%impl_->commands.size();
    if(next==impl_->tail.load(std::memory_order_acquire)) {
        ++impl_->drops;
        // Dropping an optional one-shot must not reset unrelated world loops.
        // E1 stop/change/reset delivery failures retain their critical policy.
        if(c.operation!=Operation::source &&
           !(c.operation==Operation::start && c.presentation_voice && !c.loop_enabled))
            impl_->overflow=true;
        return false;
    }
    impl_->commands[write]=c;
    impl_->head.store(next,std::memory_order_release); return true;
}
void Playback::set_listener(Listener l) noexcept {
    impl_->muted.store(l.muted,std::memory_order_relaxed);
    std::unique_lock lock{impl_->mutex,std::try_to_lock};
    if(lock) impl_->listener=l;
}
std::string_view Playback::status() const noexcept {return impl_->failures.load() ? "audio_unavailable" : impl_->status;}
PlaybackStatistics Playback::statistics() const noexcept {
    return {{impl_->started.load(),impl_->stopped.load(),impl_->updated.load(),impl_->rejected.load(),impl_->frames.load(),impl_->presentation_started.load()},impl_->drops.load(),impl_->failures.load(),impl_->queued.load()};
}
}
