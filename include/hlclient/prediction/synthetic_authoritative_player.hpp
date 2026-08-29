#pragma once

#include <hlclient/prediction/authoritative_player_state.hpp>
#include <hlclient/prediction/local_prediction.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::prediction {

inline constexpr std::size_t kMaximumSyntheticAuthorityDelayCommands = 64U;
inline constexpr std::size_t kDefaultSyntheticAuthorityPendingUpdates = 64U;
inline constexpr std::size_t kMaximumSyntheticAuthorityPendingUpdates = 256U;

enum class SyntheticAuthoritativeScenario : std::uint8_t {
    exact_authority,
    delayed_authority,
    small_position_correction,
    velocity_correction,
    large_position_correction,
    teleport,
    stale_and_duplicate_updates,
    wall_replay,
    jump_replay,
    duck_replay,
    mixed,
};

[[nodiscard]] std::string_view to_string(
    SyntheticAuthoritativeScenario scenario) noexcept;

// Tool/test-only deterministic scenario configuration. Corrections are
// expressed in normalized movement coordinates and are never read from a
// packet, file or live input source.
struct SyntheticAuthoritativePlayerConfig {
    PredictionSessionIdentity session{};
    SyntheticAuthoritativeScenario scenario{
        SyntheticAuthoritativeScenario::exact_authority};
    std::size_t command_delay{0U};
    std::size_t maximum_pending_updates{
        kDefaultSyntheticAuthorityPendingUpdates};
    std::uint32_t correction_command_sequence{1U};
    assets::AssetVector3 small_position_delta{0.5F, 0.0F, 0.0F};
    assets::AssetVector3 velocity_delta{8.0F, 0.0F, 0.0F};
    assets::AssetVector3 large_position_delta{32.0F, 0.0F, 0.0F};
    std::optional<assets::AssetVector3> teleport_origin;
    std::uint64_t first_update_ordinal{1U};
    std::uint64_t maximum_authoritative_updates{UINT64_MAX};
};

[[nodiscard]] bool valid_synthetic_authoritative_player_config(
    const SyntheticAuthoritativePlayerConfig& config) noexcept;

struct SyntheticAuthoritativeSimulatorStatistics {
    std::uint64_t processed_command_count{0U};
    std::uint64_t generated_update_count{0U};
    std::uint64_t correction_count{0U};
    std::uint64_t teleport_count{0U};
    std::size_t delayed_update_high_water_mark{0U};
};

struct SyntheticAuthoritativeSourceStatistics {
    std::uint64_t queued_update_count{0U};
    std::uint64_t polled_update_count{0U};
    std::uint64_t duplicate_update_count{0U};
    std::uint64_t stale_update_count{0U};
    std::uint64_t backpressure_count{0U};
    std::size_t output_queue_high_water_mark{0U};
};

struct SyntheticAuthoritativeSimulationResult {
    std::optional<AuthoritativePlayerState> released_update;
    movement::PlayerMovementStatistics simulation_statistics{};
    std::size_t simulation_touch_count{0U};
    std::uint64_t resulting_state_signature{0U};
    std::optional<PredictionError> error;
    AuthoritativePlayerDiscontinuity discontinuity{
        AuthoritativePlayerDiscontinuity::normal};
    bool command_processed{false};
    bool correction_applied{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

struct SyntheticAuthoritativeSimulatorCreateResult;

// Independent authoritative simulation. It consumes the exact immutable
// command objects in ascending order, owns a state separate from prediction
// history, and uses the same deterministic movement kernel with separate
// caller-owned scratch.
class SyntheticAuthoritativePlayerSimulator final {
public:
    SyntheticAuthoritativePlayerSimulator(
        const SyntheticAuthoritativePlayerSimulator&) = delete;
    SyntheticAuthoritativePlayerSimulator& operator=(
        const SyntheticAuthoritativePlayerSimulator&) = delete;
    SyntheticAuthoritativePlayerSimulator(
        SyntheticAuthoritativePlayerSimulator&& other) noexcept;
    SyntheticAuthoritativePlayerSimulator& operator=(
        SyntheticAuthoritativePlayerSimulator&&) = delete;
    ~SyntheticAuthoritativePlayerSimulator() = default;

    [[nodiscard]] static SyntheticAuthoritativeSimulatorCreateResult create(
        movement::LocalPlayerMovementState initial_state,
        goldsrc::movement::GoldSrcMovementEnvironment environment,
        SyntheticAuthoritativePlayerConfig config,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
        goldsrc::movement::GoldSrcLocalMovementConfig movement_config = {})
        noexcept;

    [[nodiscard]] const movement::LocalPlayerMovementState& current_state()
        const noexcept;
    [[nodiscard]] const SyntheticAuthoritativePlayerConfig& config()
        const noexcept;
    [[nodiscard]] const SyntheticAuthoritativeSimulatorStatistics& statistics()
        const noexcept;
    [[nodiscard]] std::size_t pending_delayed_update_count() const noexcept;
    [[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdSequence>
    next_expected_command_sequence() const noexcept;

    [[nodiscard]] SyntheticAuthoritativeSimulationResult simulate_command(
        const goldsrc::GoldSrcUserCmdState& command,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch) noexcept;

    // Releases one still-delayed state at end-of-stream. It does not run
    // movement or alter the independently simulated current state.
    [[nodiscard]] SyntheticAuthoritativeSimulationResult
    release_next_delayed() noexcept;

private:
    friend class SyntheticAuthoritativePlayerStateSource;

    SyntheticAuthoritativePlayerSimulator(
        movement::LocalPlayerMovementState initial_state,
        goldsrc::movement::GoldSrcMovementEnvironment environment,
        SyntheticAuthoritativePlayerConfig config,
        goldsrc::movement::GoldSrcLocalMovementConfig movement_config) noexcept;

    [[nodiscard]] std::optional<PredictionError> validate_command_sequence(
        const goldsrc::GoldSrcUserCmdState& command) const noexcept;
    [[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdSequence>
    released_sequence_after_accepting(
        const goldsrc::GoldSrcUserCmdState& command) const noexcept;
    void push_delayed(AuthoritativePlayerState state) noexcept;
    [[nodiscard]] std::optional<AuthoritativePlayerState>
    pop_delayed() noexcept;

    std::optional<movement::LocalPlayerMovementState> current_state_;
    goldsrc::movement::GoldSrcMovementEnvironment environment_;
    SyntheticAuthoritativePlayerConfig config_{};
    goldsrc::movement::GoldSrcLocalMovementConfig movement_config_{};
    std::array<std::optional<AuthoritativePlayerState>,
        kMaximumSyntheticAuthorityDelayCommands>
        delayed_updates_{};
    std::size_t delayed_head_{0U};
    std::size_t delayed_count_{0U};
    std::uint64_t next_update_ordinal_{1U};
    bool update_ordinal_exhausted_{false};
    SyntheticAuthoritativeSimulatorStatistics statistics_{};
};

struct SyntheticAuthoritativeSimulatorCreateResult {
    std::optional<SyntheticAuthoritativePlayerSimulator> simulator;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return simulator.has_value() && !error.has_value();
    }
};

struct SyntheticAuthoritativeSourceOperationResult {
    std::optional<PredictionError> error;
    std::size_t queued_update_count{0U};
    std::size_t pending_delayed_update_count{0U};
    std::size_t output_queue_size{0U};
    bool command_processed{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

struct SyntheticAuthoritativeSourceCreateResult;

// Bounded caller-driven in-memory source. No network opcode, transport,
// filesystem path, clock or background worker is present at this boundary.
class SyntheticAuthoritativePlayerStateSource final
    : public IAuthoritativePlayerStateSource {
public:
    SyntheticAuthoritativePlayerStateSource(
        const SyntheticAuthoritativePlayerStateSource&) = delete;
    SyntheticAuthoritativePlayerStateSource& operator=(
        const SyntheticAuthoritativePlayerStateSource&) = delete;
    SyntheticAuthoritativePlayerStateSource(
        SyntheticAuthoritativePlayerStateSource&& other) noexcept;
    SyntheticAuthoritativePlayerStateSource& operator=(
        SyntheticAuthoritativePlayerStateSource&&) = delete;
    ~SyntheticAuthoritativePlayerStateSource() override = default;

    [[nodiscard]] static SyntheticAuthoritativeSourceCreateResult create(
        movement::LocalPlayerMovementState initial_state,
        goldsrc::movement::GoldSrcMovementEnvironment environment,
        SyntheticAuthoritativePlayerConfig config,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
        goldsrc::movement::GoldSrcLocalMovementConfig movement_config = {})
        noexcept;

    [[nodiscard]] SyntheticAuthoritativeSourceOperationResult submit_command(
        const goldsrc::GoldSrcUserCmdState& command,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch) noexcept;

    [[nodiscard]] SyntheticAuthoritativeSourceOperationResult
    flush_next_delayed() noexcept;

    [[nodiscard]] AuthoritativePlayerStatePollResult poll_next() override;

    [[nodiscard]] const SyntheticAuthoritativePlayerSimulator& simulator()
        const noexcept;
    [[nodiscard]] const SyntheticAuthoritativeSourceStatistics& statistics()
        const noexcept;
    [[nodiscard]] std::size_t queued_update_count() const noexcept;

private:
    explicit SyntheticAuthoritativePlayerStateSource(
        SyntheticAuthoritativePlayerSimulator simulator) noexcept;

    [[nodiscard]] bool stale_duplicate_injection_due(
        goldsrc::GoldSrcUserCmdSequence released_sequence) const noexcept;
    [[nodiscard]] std::size_t required_queue_slots(
        goldsrc::GoldSrcUserCmdSequence released_sequence) const noexcept;
    [[nodiscard]] bool has_queue_capacity(std::size_t required) const noexcept;
    void queue_update(const AuthoritativePlayerState& update) noexcept;
    void queue_released_update(const AuthoritativePlayerState& update) noexcept;

    SyntheticAuthoritativePlayerSimulator simulator_;
    std::array<std::optional<AuthoritativePlayerState>,
        kMaximumSyntheticAuthorityPendingUpdates>
        output_updates_{};
    std::size_t output_head_{0U};
    std::size_t output_count_{0U};
    std::optional<AuthoritativePlayerState> previous_base_update_;
    bool stale_duplicate_injected_{false};
    SyntheticAuthoritativeSourceStatistics statistics_{};
};

struct SyntheticAuthoritativeSourceCreateResult {
    std::optional<SyntheticAuthoritativePlayerStateSource> source;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return source.has_value() && !error.has_value();
    }
};

} // namespace hlclient::prediction
