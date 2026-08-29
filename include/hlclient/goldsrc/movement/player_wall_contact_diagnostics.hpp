#pragma once

#include <hlclient/movement/local_player_movement_state.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace hlclient::goldsrc::movement {

inline constexpr std::size_t kPlayerMovementDiagnosticCapacity = 256U;

enum class PlayerMovementTraceFractionClass : std::uint8_t {
    none,
    zero,
    near_zero,
    partial,
    complete,
};

enum class PlayerMovementPlaneClass : std::uint8_t {
    none,
    finite_unit,
    invalid,
};

enum class PlayerMovementVelocityClass : std::uint8_t {
    none,
    finite_zero,
    finite_into_plane,
    finite_not_into_plane,
    invalid,
};

enum class PlayerMovementDiagnosticResult : std::uint8_t {
    none,
    progressing,
    stable_stop,
    collision_failure,
    movement_failure,
    success,
};

enum class PlayerMovementRemainingTimeClass : std::uint8_t {
    none,
    exhausted,
    retained,
};

enum class PlayerMovementCollisionResultClass : std::uint8_t {
    none,
    no_hit,
    contact,
    start_solid,
    all_solid,
    typed_failure,
};

enum class PlayerMovementDiagnosticHitKind : std::uint8_t {
    none,
    world,
    explicit_synthetic_brush,
};

struct PlayerMovementDiagnosticRuntimeContext final {
    std::uint64_t frame_ordinal{0U};
    std::size_t generated_command_count{0U};
    std::uint64_t camera_revision{0U};
    std::uint64_t visibility_revision{0U};
};

// Metadata only: positions, velocities, BSP bytes, and raw traces deliberately
// do not cross this diagnostic boundary.
struct PlayerWallContactDiagnosticFrame {
    std::uint64_t diagnostic_ordinal{0U};
    std::uint64_t frame_ordinal{0U};
    std::uint32_t command_sequence{0U};
    std::uint64_t state_signature_before{0U};
    std::uint64_t state_signature_after{0U};
    hlclient::movement::PlayerMovementPhase phase{
        hlclient::movement::PlayerMovementPhase::direct_slide};
    std::uint16_t substep_ordinal{0U};
    std::uint16_t generated_command_count{0U};
    std::uint32_t trace_count{0U};
    std::uint16_t slide_bump_ordinal{0U};
    std::uint16_t clip_plane_count{0U};
    std::uint16_t distinct_plane_count{0U};
    PlayerMovementTraceFractionClass fraction_class{
        PlayerMovementTraceFractionClass::none};
    PlayerMovementPlaneClass plane_class{PlayerMovementPlaneClass::none};
    PlayerMovementVelocityClass velocity_class{
        PlayerMovementVelocityClass::none};
    PlayerMovementDiagnosticResult result{
        PlayerMovementDiagnosticResult::none};
    PlayerMovementRemainingTimeClass remaining_time_class{
        PlayerMovementRemainingTimeClass::none};
    PlayerMovementCollisionResultClass collision_result{
        PlayerMovementCollisionResultClass::none};
    PlayerMovementDiagnosticHitKind hit_kind{
        PlayerMovementDiagnosticHitKind::none};
    std::uint64_t camera_revision{0U};
    std::uint64_t visibility_revision{0U};
    bool start_solid{false};
    bool all_solid{false};
    bool hit_present{false};
    bool remaining_time_present{false};
};

// Viewer-owned state is intentionally added at the viewer/controller boundary,
// not guessed by the pure movement kernel. The bounded conversion saturates a
// hostile caller count while retaining the exact scheduler range in normal use.
inline void apply_player_movement_diagnostic_runtime_context(
    PlayerWallContactDiagnosticFrame& frame,
    const PlayerMovementDiagnosticRuntimeContext& context) noexcept
{
    frame.frame_ordinal = context.frame_ordinal;
    frame.generated_command_count = context.generated_command_count > UINT16_MAX
        ? UINT16_MAX
        : static_cast<std::uint16_t>(context.generated_command_count);
    frame.camera_revision = context.camera_revision;
    frame.visibility_revision = context.visibility_revision;
}

class PlayerMovementDiagnosticRing final {
public:
    void clear() noexcept
    {
        size_ = 0U;
        next_ = 0U;
        next_ordinal_ = 0U;
        overwrite_count_ = 0U;
    }

    void push(PlayerWallContactDiagnosticFrame frame) noexcept
    {
        frame.diagnostic_ordinal = next_ordinal_;
        if (next_ordinal_ != UINT64_MAX) {
            ++next_ordinal_;
        }
        records_[next_] = frame;
        next_ = (next_ + 1U) % records_.size();
        if (size_ < records_.size()) {
            ++size_;
        } else {
            if (overwrite_count_ != UINT64_MAX) {
                ++overwrite_count_;
            }
        }
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return kPlayerMovementDiagnosticCapacity;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t overwrite_count() const noexcept
    {
        return overwrite_count_;
    }

    [[nodiscard]] const PlayerWallContactDiagnosticFrame* at_oldest(
        const std::size_t index) const noexcept
    {
        if (index >= size_) {
            return nullptr;
        }
        const auto oldest = size_ == records_.size() ? next_ : 0U;
        return &records_[(oldest + index) % records_.size()];
    }

    [[nodiscard]] const PlayerWallContactDiagnosticFrame* latest()
        const noexcept
    {
        if (size_ == 0U) {
            return nullptr;
        }
        const auto index = next_ == 0U ? records_.size() - 1U : next_ - 1U;
        return &records_[index];
    }

private:
    std::array<PlayerWallContactDiagnosticFrame,
        kPlayerMovementDiagnosticCapacity> records_{};
    std::size_t size_{0U};
    std::size_t next_{0U};
    std::uint64_t next_ordinal_{0U};
    std::uint64_t overwrite_count_{0U};
};

static_assert(std::is_trivially_copyable_v<PlayerWallContactDiagnosticFrame>);
static_assert(std::is_trivially_copyable_v<PlayerMovementDiagnosticRing>);

} // namespace hlclient::goldsrc::movement
