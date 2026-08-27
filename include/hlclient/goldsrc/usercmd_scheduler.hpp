#pragma once

#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumUserCmdsPerSchedulerUpdate = 8U;
inline constexpr std::size_t kMaximumUserCmdsPerSchedulerUpdate = 64U;

enum class GoldSrcUserCmdSamplingProfile : std::uint8_t {
    stock_protocol_48_controlled_profile_v1,
    synthetic_fixed_step_v1,
    stock_evidence_pending,
};

struct GoldSrcUserCmdSchedulerConfig {
    std::uint64_t command_interval_nanoseconds{10'000'000U};
    std::size_t maximum_commands_per_update{
        kDefaultMaximumUserCmdsPerSchedulerUpdate};
    std::uint32_t maximum_command_sequence{UINT32_MAX};
    GoldSrcUserCmdSamplingProfile profile{
        GoldSrcUserCmdSamplingProfile::synthetic_fixed_step_v1};
};

[[nodiscard]] bool valid_goldsrc_usercmd_scheduler_config(
    const GoldSrcUserCmdSchedulerConfig& config) noexcept;

struct GoldSrcUserCmdSampleRequest {
    GoldSrcUserCmdSequence command_sequence{};
    std::int64_t sample_time_nanoseconds{0};
    std::uint64_t sample_duration_nanoseconds{0U};
    std::uint8_t command_msec{0U};
    std::uint64_t source_input_sequence{0U};
    bool focused{false};
    bool one_shot_eligible{false};
    double camera_yaw_degrees{0.0};
    double camera_pitch_degrees{0.0};
};

enum class GoldSrcUserCmdSchedulerErrorCode : std::uint8_t {
    invalid_configuration,
    stock_evidence_pending,
    time_moved_backwards,
    time_overflow,
    lag_limit_exceeded,
    sequence_exhausted,
    allocation_failed,
};

struct GoldSrcUserCmdSchedulerError {
    GoldSrcUserCmdSchedulerErrorCode code{
        GoldSrcUserCmdSchedulerErrorCode::invalid_configuration};
    std::string_view context;
};

struct GoldSrcUserCmdSchedulerUpdateResult {
    std::vector<GoldSrcUserCmdSampleRequest> requests;
    std::optional<GoldSrcUserCmdSchedulerError> error;
    std::int64_t next_sample_time_nanoseconds{0};
    std::int64_t duration_remainder_nanoseconds{0};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class GoldSrcUserCmdScheduler final {
public:
    explicit GoldSrcUserCmdScheduler(
        GoldSrcUserCmdSchedulerConfig config = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const GoldSrcUserCmdSchedulerConfig& config() const noexcept;
    [[nodiscard]] GoldSrcUserCmdSchedulerUpdateResult update(
        std::int64_t monotonic_time_nanoseconds,
        const gameplay_input::GameplayInputIntent& intent,
        const gameplay_camera::GameplayCameraState& camera) noexcept;
    void reset() noexcept;

private:
    GoldSrcUserCmdSchedulerConfig config_;
    bool valid_configuration_{false};
    bool initialized_{false};
    std::int64_t last_update_time_nanoseconds_{0};
    std::int64_t next_sample_time_nanoseconds_{0};
    std::int64_t duration_remainder_nanoseconds_{0};
    // One bit wider than the public identity domain so exhaustion is retained
    // explicitly after UINT32_MAX instead of wrapping the internal cursor.
    std::uint64_t next_command_sequence_{1U};
};

} // namespace hlclient::goldsrc
