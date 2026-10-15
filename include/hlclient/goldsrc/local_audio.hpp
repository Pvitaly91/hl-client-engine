#pragma once
#include <hlclient/goldsrc/server_audio.hpp>
#include <hlclient/game_api/audio.hpp>
#include <hlclient/game_api/temporary_effects.hpp>
namespace hlclient::goldsrc {
bool valid_local_sound_reference(const game_api::LocalSoundReference&) noexcept;
struct LocalAudioPlaybackStatistics {
    std::uint64_t submitted{},missing{},late{},cancelled{},duplicates{},limits{},muted{};
    std::uint64_t movement_submitted{},movement_missing{},movement_late{},movement_muted{};
    std::uint64_t impact_submitted{},impact_missing{},impact_late{},impact_muted{},impact_rejected{};
    std::uint64_t shell_submitted{},shell_missing{},shell_late{},shell_muted{},shell_rejected{};
    std::uint64_t resource_pending{},resource_not_authorized{},resource_open_failed{},
        resource_decode_failed{},output_queue_rejected{};
};
// Same E1 loader/mixer/output, with a separate exact voice namespace. No weapon
// interpretation, scene access or IO occurs here. Audio is optional and noexcept.
enum class LocalAudioDomain { local, remote, world };
class LocalAudio final {
public:
    explicit LocalAudio(audio::Output& output, bool remote_effects=false)
        :LocalAudio(output,remote_effects ? LocalAudioDomain::remote : LocalAudioDomain::local){}
    explicit LocalAudio(audio::Output& output, LocalAudioDomain domain)
        :output_(output),voice_namespace_((std::uint64_t{1}<<63) |
            (domain==LocalAudioDomain::remote ? (std::uint64_t{1}<<62) :
             domain==LocalAudioDomain::world ? (std::uint64_t{1}<<61) : 0)){}
    void update(const game_api::LocalAudioBatch&,SoundAssets*,double now,bool audible) noexcept;
    const LocalAudioPlaybackStatistics& statistics() const noexcept { return stats_; }
private:
    audio::Output& output_;
    const std::uint64_t voice_namespace_;
    std::array<std::optional<game_api::LocalSoundCue>,32> pending_{};
    struct PersistentVoice { game_api::LocalVoiceControl control; bool started{}, muted{}; };
    std::array<std::optional<PersistentVoice>,32> persistent_{};
    std::array<std::optional<game_api::LocalVoiceControl>,32> scheduled_controls_{};
    std::uint64_t control_serial_{};
    std::uint64_t session_{},scope_{}, high_serial_{}, next_voice_{}, movement_epoch_{};
    LocalAudioPlaybackStatistics stats_;
};
struct TemporaryAudioEventStatistics final {
    std::uint64_t accepted{},duplicate{},stale{},invalid{},capacity{};
};
// Committed world effects share the existing loader/mixer but neither weapon
// scopes nor entity channels. Exact bounded provenance dedup, not a hash of an
// action or position. Only map/session reset stops this voice namespace.
class TemporaryAudio final {
public:
    explicit TemporaryAudio(audio::Output& output)
        :output_(output),delivery_(output,LocalAudioDomain::world){}
    void reset(std::uint64_t generation=0) noexcept;
    void update(const game_api::TemporaryEffectsBatch&,SoundAssets*,double now,bool audible) noexcept;
    const LocalAudioPlaybackStatistics& statistics() const noexcept { return delivery_.statistics(); }
    const TemporaryAudioEventStatistics& event_statistics() const noexcept { return stats_; }
private:
    audio::Output& output_;
    LocalAudio delivery_;
    std::array<game_api::TemporaryEffectIdentity,256> seen_{};
    std::array<std::uint64_t,256> seen_ordinals_{};
    std::size_t seen_count_{},next_seen_{};
    std::optional<std::array<std::uint64_t,3>> evicted_floor_;
    std::uint64_t generation_{},session_{1},serial_{};
    TemporaryAudioEventStatistics stats_;
};
}
