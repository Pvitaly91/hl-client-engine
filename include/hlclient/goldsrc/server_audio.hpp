#pragma once
#include <hlclient/game_api/audio.hpp>
#include <hlclient/goldsrc/runtime_control_decoder.hpp>
#include <hlclient/audio/output.hpp>
#include <chrono>
#include <utility>
namespace hlclient::goldsrc {
using SoundTime=std::chrono::steady_clock::time_point;
struct CommittedSound {
    std::uint64_t generation{}, record{};
    std::size_t ordinal{}, cursor{}, message{};
    RuntimeControlOpcode opcode{};
    RuntimeControlSound sound;
    SoundTime received{};
};
// Sole session thread only. Fixed owning outbox, published after world commit.
class CommittedSoundQueue final {
public:
    void publish(std::uint64_t record,std::size_t ordinal,const RuntimeControlEvent&,SoundTime now=std::chrono::steady_clock::now()) noexcept;
    bool pop(CommittedSound&) noexcept;
    void reset_generation(std::uint64_t generation) noexcept;
    void reset() noexcept {size_=head_=0; generation_=0; high_ordinal_=high_cursor_=0;}
    bool take_overflow() noexcept {return std::exchange(overflow_,false);}
    void reject_pending() noexcept {++dropped; overflow_=true;}
    std::uint64_t duplicates{},dropped{},starts{},stops{},statics{},changes{};
private:
    std::array<CommittedSound,512> events_{};
    std::size_t head_{},size_{},high_ordinal_{},high_cursor_{};
    std::uint64_t generation_{};
    bool overflow_{};
};
enum class SoundAssetStatus {
    pending, ready, missing, unsupported, limit,
    not_authorized, open_failed, decode_failed
};
struct SoundAssetResult { SoundAssetStatus status{SoundAssetStatus::pending}; std::shared_ptr<const assets::AudioAsset> asset; };
class SoundAssets {
public:
    virtual ~SoundAssets()=default;
    // Nonblocking, no file IO. Returned PCM owns its lifetime.
    virtual SoundAssetResult request(std::uint16_t index) noexcept=0;
    // Immutable virtual manifest name, valid only for this loader lifetime.
    virtual std::string_view virtual_name(std::uint16_t) const noexcept {return {};}
    virtual SoundAssetResult request_local(const game_api::LocalSoundReference&) noexcept { return {SoundAssetStatus::unsupported,{}}; }
};
struct SoundSource {std::uint32_t entity{}; audio::Vec3 position{};};
struct ServerAudioStatistics {std::uint64_t started{},stopped{},updated{},expired{},unsupported{},missing{},limits{},no_voice{},loads{},sentences{},formats{},pending{},not_authorized{},open_failed{},decode_failed{},voice_rejected{},invalid_origin{},attachment_invalidated{};};
class ServerAudio final {
public:
    explicit ServerAudio(audio::Output& output):output_{output}{}
    ~ServerAudio() {reset(0);}
    void reset(std::uint64_t generation) noexcept;
    void consume(const CommittedSound&,SoundTime now) noexcept;
    void update(SoundAssets*,SoundTime now) noexcept;
    // One-shots retain the server event origin. Loops may follow valid entity
    // sources (never render visibility); missing sources fall back after 2s.
    void present(std::span<const SoundSource>,audio::Listener,SoundTime now,std::uint32_t receiving_entity) noexcept;
    const ServerAudioStatistics& statistics() const noexcept {return stats_;}
    std::string_view first_error() const noexcept {return first_error_;}
private:
    struct Slot {
        bool used{},playing{},attached{},attachment_valid{true},pending_reported{};
        CommittedSound event;
        audio::Command command;
        SoundTime expires{},seen{};
    };
    void stop(Slot&) noexcept;
    audio::Output& output_;
    std::array<Slot,audio::maximum_voices> slots_{};
    std::uint64_t generation_{},next_voice_{};
    std::size_t high_ordinal_{},high_cursor_{};
    std::uint32_t receiving_entity_{};
    ServerAudioStatistics stats_{};
    std::string_view first_error_{"none"};
    void error(std::string_view value) noexcept {if(first_error_=="none") first_error_=value;}
};
}
