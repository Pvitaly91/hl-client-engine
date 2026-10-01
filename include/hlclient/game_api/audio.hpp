#pragma once
#include <hlclient/assets/asset_types.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
namespace hlclient::game_api {
enum class SoundReferenceSource { server_precache_reference, validated_local_model_event, pinned_halflife_client_sound_profile };
// Owning virtual sample token, never an OS path. Local references have no wire index.
struct LocalSoundReference {
    SoundReferenceSource source{SoundReferenceSource::validated_local_model_event};
    std::array<char,64> sample{};
    std::string_view name() const noexcept {
        std::size_t n=0; while(n<sample.size() && sample[n]) ++n;
        return {sample.data(),n};
    }
    bool operator==(const LocalSoundReference&) const = default;
};
enum class LocalSoundKind { fire, reload, deploy, swing, footstep, ladder, landing, fall_pain, impact, shell_contact };
enum class LocalSoundChannel { automatic, weapon, body, voice };
struct LocalSoundCue {
    LocalSoundReference reference;
    std::uint64_t scope{}, serial{};
    double scheduled_seconds{};
    float volume{1}, pitch{1};
    LocalSoundKind kind{};
    std::uint32_t command_sequence{},sequence{},marker_ordinal{0xffffffffU};
    std::uint64_t restart{},loop_iteration{};
    LocalSoundChannel channel{LocalSoundChannel::automatic};
    std::optional<assets::AssetVector3> world_origin;
    float attenuation{0.0F};
};
// Bounded owning presentation diagnostic; labels are selected by the module.
// No borrowed texture/RX storage and no engine knowledge of HL material codes.
struct MovementAudioDiagnostic {
    std::uint64_t serial{}, generation{}, life_epoch{}, ordinal{};
    std::uint32_t command{}, cadence_ms{};
    std::optional<std::uint32_t> surface_index, material_index, texture_index, model_index;
    std::array<char,13> texture_key{};
    std::array<char,32> material{}, step_category{}, decision{};
    std::array<char,48> classification{};
    std::array<char,16> movement_mode{}, speed_band{};
    LocalSoundReference sample;
    float speed{}, volume{};
    bool left{}, ducked{}, grounded{};
};
struct LocalAudioStatistics {
    std::uint64_t actions{},fire{},reload{},deploy{},swing{},markers{},duplicates{},late{},invalid{},cancelled{};
    std::uint64_t timeline_corrections{}; // ownership retained; not itself a duplicate marker
    std::uint64_t footstep{},ladder{},landing{},movement_suppressed{},
        movement_replay{},movement_material_unavailable{},movement_history_gaps{};
    std::uint64_t movement_observations{},movement_clock_dropped{},movement_invalid_time{};
    std::uint64_t movement_quiet{},movement_movevars_missing{},movement_movevars_disabled{},
        movement_unsupported{},movement_duplicates{},movement_outbox_limit{};
};
// Fixed owning outbox drained on the session thread after presentation sampling.
// Scope changes cancel pending local cues, never world audio. Lifetime is independent
// of RX/model storage; at most 32 cues and 32 preload references per update.
struct LocalAudioBatch {
    std::uint64_t session{}; // host lifetime/reset serial, distinct from module scope
    std::uint64_t scope{};
    std::array<LocalSoundCue,32> cues{};
    std::size_t count{};
    std::array<LocalSoundReference,32> prepare{};
    std::size_t prepare_count{};
    LocalAudioStatistics statistics;
    std::uint64_t movement_epoch{}; // life-local queue ownership, independent of weapon scope
    std::optional<MovementAudioDiagnostic> movement_diagnostic;
};
}
