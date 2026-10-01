#pragma once
#include <hlclient/audio/mixer.hpp>
#include <string_view>
namespace hlclient::audio {
class Output {
public:
    virtual ~Output()=default;
    virtual bool submit(const Command&) noexcept=0;
    virtual void set_listener(Listener) noexcept=0;
};
class OfflineOutput final : public Output {
public:
    bool submit(const Command& c) noexcept override { mixer.command(c); return true; }
    void set_listener(Listener l) noexcept override { mixer.listener(l); }
    Mixer mixer;
};
struct PlaybackStatistics { Statistics mixer; std::uint64_t queue_drops{}, queue_failures{}; std::uint32_t queued_frames{}; };
// SDL implementation is private. Disabled/no-device never opens recording.
class Playback final : public Output {
public:
    explicit Playback(bool enabled);
    ~Playback() override;
    Playback(const Playback&)=delete;
    Playback& operator=(const Playback&)=delete;
    bool submit(const Command&) noexcept override;
    void set_listener(Listener) noexcept override;
    std::string_view status() const noexcept;
    PlaybackStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
