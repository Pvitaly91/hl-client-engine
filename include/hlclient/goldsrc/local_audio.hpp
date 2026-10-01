#pragma once
#include <hlclient/goldsrc/server_audio.hpp>
#include <hlclient/game_api/audio.hpp>
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
class LocalAudio final {
public:
    explicit LocalAudio(audio::Output& output, bool remote_effects=false)
        :output_(output),voice_namespace_((std::uint64_t{1}<<63) |
            (remote_effects ? (std::uint64_t{1}<<62) : 0)){}
    void update(const game_api::LocalAudioBatch&,SoundAssets*,double now,bool audible) noexcept;
    const LocalAudioPlaybackStatistics& statistics() const noexcept { return stats_; }
private:
    audio::Output& output_;
    const std::uint64_t voice_namespace_;
    std::array<std::optional<game_api::LocalSoundCue>,32> pending_{};
    std::uint64_t session_{},scope_{}, high_serial_{}, next_voice_{}, movement_epoch_{};
    LocalAudioPlaybackStatistics stats_;
};
}
