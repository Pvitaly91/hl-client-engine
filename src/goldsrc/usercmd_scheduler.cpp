#include <hlclient/goldsrc/usercmd_scheduler.hpp>

#include <limits>
#include <new>
#include <stdexcept>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] GoldSrcUserCmdSchedulerUpdateResult failure(
    const GoldSrcUserCmdSchedulerErrorCode code,
    const std::string_view context,
    const std::int64_t next_sample,
    const std::int64_t remainder) noexcept
{
    GoldSrcUserCmdSchedulerUpdateResult result;
    result.error = GoldSrcUserCmdSchedulerError{code, context};
    result.next_sample_time_nanoseconds = next_sample;
    result.duration_remainder_nanoseconds = remainder;
    return result;
}

} // namespace

bool valid_goldsrc_usercmd_scheduler_config(
    const GoldSrcUserCmdSchedulerConfig& config) noexcept
{
    return config.profile ==
               GoldSrcUserCmdSamplingProfile::synthetic_fixed_step_v1 &&
           config.command_interval_nanoseconds > 0U &&
           config.command_interval_nanoseconds <= 255'000'000U &&
           config.maximum_commands_per_update > 0U &&
           config.maximum_commands_per_update <=
               kMaximumUserCmdsPerSchedulerUpdate &&
           config.maximum_command_sequence > 0U;
}

GoldSrcUserCmdScheduler::GoldSrcUserCmdScheduler(
    GoldSrcUserCmdSchedulerConfig config) noexcept
    : config_{config},
      valid_configuration_{valid_goldsrc_usercmd_scheduler_config(config_)}
{
}

bool GoldSrcUserCmdScheduler::valid_configuration() const noexcept
{
    return valid_configuration_;
}

const GoldSrcUserCmdSchedulerConfig& GoldSrcUserCmdScheduler::config() const noexcept
{
    return config_;
}

GoldSrcUserCmdSchedulerUpdateResult GoldSrcUserCmdScheduler::update(
    const std::int64_t monotonic_time_nanoseconds,
    const gameplay_input::GameplayInputIntent& intent,
    const gameplay_camera::GameplayCameraState& camera) noexcept
{
    if (!valid_configuration_) {
        return failure(
            config_.profile == GoldSrcUserCmdSamplingProfile::stock_evidence_pending ||
                    config_.profile ==
                        GoldSrcUserCmdSamplingProfile::
                            stock_protocol_48_controlled_profile_v1
                ? GoldSrcUserCmdSchedulerErrorCode::stock_evidence_pending
                : GoldSrcUserCmdSchedulerErrorCode::invalid_configuration,
            "Only the bounded synthetic fixed-step scheduler is executable",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    }
    if (initialized_ &&
        monotonic_time_nanoseconds < last_update_time_nanoseconds_) {
        return failure(
            GoldSrcUserCmdSchedulerErrorCode::time_moved_backwards,
            "Caller-provided monotonic scheduler time moved backwards",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    }
    if (!initialized_) {
        const auto interval = static_cast<std::int64_t>(
            config_.command_interval_nanoseconds);
        if (monotonic_time_nanoseconds >
            std::numeric_limits<std::int64_t>::max() - interval) {
            return failure(
                GoldSrcUserCmdSchedulerErrorCode::time_overflow,
                "Initial scheduler deadline overflowed",
                0,
                0);
        }
        initialized_ = true;
        last_update_time_nanoseconds_ = monotonic_time_nanoseconds;
        next_sample_time_nanoseconds_ = monotonic_time_nanoseconds + interval;
        GoldSrcUserCmdSchedulerUpdateResult initialized;
        initialized.next_sample_time_nanoseconds = next_sample_time_nanoseconds_;
        return initialized;
    }

    if (monotonic_time_nanoseconds < next_sample_time_nanoseconds_) {
        last_update_time_nanoseconds_ = monotonic_time_nanoseconds;
        GoldSrcUserCmdSchedulerUpdateResult waiting;
        waiting.next_sample_time_nanoseconds = next_sample_time_nanoseconds_;
        waiting.duration_remainder_nanoseconds =
            duration_remainder_nanoseconds_;
        return waiting;
    }

    // Unsigned subtraction yields the exact non-negative mathematical
    // distance for every ordered int64 pair, including INT64_MIN..INT64_MAX.
    const auto elapsed_due =
        static_cast<std::uint64_t>(monotonic_time_nanoseconds) -
        static_cast<std::uint64_t>(next_sample_time_nanoseconds_);
    const auto elapsed_intervals =
        elapsed_due / config_.command_interval_nanoseconds;
    if (elapsed_intervals >= config_.maximum_commands_per_update) {
        return failure(
            GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded,
            "Scheduler catch-up demand exceeds the configured bounded update",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    }
    const auto due_count_wide = elapsed_intervals + 1U;
    const auto due_count = static_cast<std::size_t>(due_count_wide);
    if (next_command_sequence_ > config_.maximum_command_sequence ||
        due_count_wide - 1U >
            static_cast<std::uint64_t>(config_.maximum_command_sequence) -
                next_command_sequence_) {
        return failure(
            GoldSrcUserCmdSchedulerErrorCode::sequence_exhausted,
            "Project-local usercmd sequence domain is exhausted",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    }
    const auto deadline_advance = due_count_wide *
        config_.command_interval_nanoseconds;
    if (next_sample_time_nanoseconds_ >
        std::numeric_limits<std::int64_t>::max() -
            static_cast<std::int64_t>(deadline_advance)) {
        return failure(
            GoldSrcUserCmdSchedulerErrorCode::time_overflow,
            "Scheduler catch-up deadline overflowed",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    }

    GoldSrcUserCmdSchedulerUpdateResult result;
    auto staged_remainder = duration_remainder_nanoseconds_;
    auto staged_sequence = next_command_sequence_;
    auto staged_sample_time = next_sample_time_nanoseconds_;
    try {
        result.requests.reserve(due_count);
        for (std::size_t index = 0U; index < due_count; ++index) {
            const auto accumulated_duration =
                static_cast<std::int64_t>(config_.command_interval_nanoseconds) +
                staged_remainder;
            const auto command_msec =
                static_cast<std::uint8_t>(accumulated_duration / 1'000'000);
            staged_remainder = accumulated_duration % 1'000'000;
            const auto sequence = GoldSrcUserCmdSequence::create(
                static_cast<std::uint32_t>(staged_sequence),
                config_.maximum_command_sequence);
            if (!sequence) {
                return failure(
                    GoldSrcUserCmdSchedulerErrorCode::sequence_exhausted,
                    "Project-local usercmd sequence could not be created",
                    next_sample_time_nanoseconds_,
                    duration_remainder_nanoseconds_);
            }
            result.requests.push_back(GoldSrcUserCmdSampleRequest{
                *sequence,
                staged_sample_time,
                config_.command_interval_nanoseconds,
                command_msec,
                intent.input_sequence(),
                intent.focused(),
                index == 0U && intent.pressed_buttons() != 0U,
                camera.yaw_degrees(),
                camera.pitch_degrees(),
            });
            ++staged_sequence;
            staged_sample_time +=
                static_cast<std::int64_t>(config_.command_interval_nanoseconds);
        }
    } catch (const std::bad_alloc&) {
        return failure(
            GoldSrcUserCmdSchedulerErrorCode::allocation_failed,
            "Scheduler could not allocate its bounded command batch",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    } catch (const std::length_error&) {
        return failure(
            GoldSrcUserCmdSchedulerErrorCode::allocation_failed,
            "Scheduler rejected its bounded command-batch allocation",
            next_sample_time_nanoseconds_,
            duration_remainder_nanoseconds_);
    }

    next_command_sequence_ = staged_sequence;
    next_sample_time_nanoseconds_ = staged_sample_time;
    duration_remainder_nanoseconds_ = staged_remainder;
    last_update_time_nanoseconds_ = monotonic_time_nanoseconds;
    result.next_sample_time_nanoseconds = next_sample_time_nanoseconds_;
    result.duration_remainder_nanoseconds = duration_remainder_nanoseconds_;
    return result;
}

void GoldSrcUserCmdScheduler::reset() noexcept
{
    initialized_ = false;
    last_update_time_nanoseconds_ = 0;
    next_sample_time_nanoseconds_ = 0;
    duration_remainder_nanoseconds_ = 0;
    next_command_sequence_ = 1U;
}

} // namespace hlclient::goldsrc
