#pragma once

#include <hlclient/game_api/audio.hpp>
#include <hlclient/game_api/movement_audio.hpp>
#include <hlclient/games/halflife/materials.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <span>

namespace hlclient::games::halflife {

// Life-local HL1 presentation policy. The network history stays in the host;
// this small bounded checkpoint ring retains only timer/foot phase for rebase.
class HalfLifeMovementAudio final {
public:
  void reset() noexcept;
  void prepare_resources() noexcept;
  [[nodiscard]] static std::span<const game_api::LocalSoundReference> resources() noexcept;
  void rewind(std::uint32_t boundary) noexcept;
  void observe(const game_api::MovementAudioObservation&, bool replay,
      const HalfLifeMaterials&) noexcept;
  void cancel_life() noexcept;
  void append_to(game_api::LocalAudioBatch&) noexcept;

private:
  struct Phase {
    std::uint32_t command{}, remaining_ms{};
    bool quiet_onset_pending{};
  };
  std::array<Phase,128> checkpoints_{};
  std::size_t checkpoint_next_{};
  Phase phase_{};
  std::uint64_t generation_{}, life_epoch_{}, emitted_through_{};
  std::uint64_t occurrence_{}, epoch_{}, diagnostic_serial_{};
  bool left_{}, cancelled_{};
  std::optional<game_api::MovementAudioDiagnostic> diagnostic_;
  std::array<game_api::LocalSoundCue,32> pending_{};
  std::size_t pending_count_{}, preload_next_{};
  std::uint64_t steps_{}, ladders_{}, landings_{}, suppressed_{},
      replayed_{}, missing_material_{}, history_gaps_{};
  std::uint64_t quiet_{}, movevars_missing_{}, movevars_disabled_{}, unsupported_{},
      duplicates_{}, outbox_limit_{};

  void emit(std::string_view sample, game_api::LocalSoundKind kind,
      game_api::LocalSoundChannel channel, float volume,
      const game_api::MovementAudioObservation&, std::uint32_t ordinal,
      bool replay) noexcept;
  void diagnose(const game_api::MovementAudioObservation&, const MaterialLookup&,
      std::string_view category, std::string_view decision,
      std::string_view sample={}, float volume=0) noexcept;
};

} // namespace hlclient::games::halflife
