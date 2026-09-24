#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>

#include <cmath>
#include <limits>
#include <type_traits>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::size_t kMovePacketEventCountWithoutBackup = 5U;
inline constexpr std::size_t kMaximumMovePacketEventCount = 6U;

[[nodiscard]] bool terminal_state(
    const GoldSrcUserCmdTransmissionState state) noexcept
{
    switch (state) {
    case GoldSrcUserCmdTransmissionState::history_backpressure:
    case GoldSrcUserCmdTransmissionState::checksum_evidence_pending:
    case GoldSrcUserCmdTransmissionState::signon_evidence_pending:
    case GoldSrcUserCmdTransmissionState::cancelled:
    case GoldSrcUserCmdTransmissionState::timed_out:
    case GoldSrcUserCmdTransmissionState::network_error:
    case GoldSrcUserCmdTransmissionState::protocol_error:
    case GoldSrcUserCmdTransmissionState::closed:
        return true;
    case GoldSrcUserCmdTransmissionState::idle:
    case GoldSrcUserCmdTransmissionState::waiting_for_runtime_ready:
    case GoldSrcUserCmdTransmissionState::sampling_commands:
    case GoldSrcUserCmdTransmissionState::command_history_ready:
    case GoldSrcUserCmdTransmissionState::waiting_for_packet_context:
    case GoldSrcUserCmdTransmissionState::planning_move_packet:
    case GoldSrcUserCmdTransmissionState::encoding_move_packet:
    case GoldSrcUserCmdTransmissionState::waiting_for_unreliable_submission:
    case GoldSrcUserCmdTransmissionState::move_packet_submitted:
    case GoldSrcUserCmdTransmissionState::waiting_for_next_sample:
    case GoldSrcUserCmdTransmissionState::unreliable_backpressure:
    case GoldSrcUserCmdTransmissionState::event_backpressure:
        return false;
    }
    return true;
}

[[nodiscard]] std::int64_t nanoseconds_since_epoch(
    const NetchanDriverTimePoint time) noexcept
{
    static_assert(std::is_same_v<
        NetchanDriverClock::duration,
        std::chrono::nanoseconds>);
    return time.time_since_epoch().count();
}

[[nodiscard]] bool timeout_elapsed(
    const NetchanDriverTimePoint last_progress,
    const NetchanDriverTimePoint now,
    const std::chrono::milliseconds timeout) noexcept
{
    const auto native_timeout =
        std::chrono::duration_cast<NetchanDriverClock::duration>(timeout);
    if (last_progress > NetchanDriverTimePoint::max() - native_timeout) {
        return false;
    }
    return now >= last_progress + native_timeout;
}

[[nodiscard]] GoldSrcUserCmdTransmissionOperationResult operation_error(
    GoldSrcUserCmdTransmissionError error) noexcept
{
    return {std::move(error)};
}

[[nodiscard]] bool stage_components_within_limits(
    const GoldSrcUserCmdTransmissionConfig& config) noexcept
{
    const auto interval_msec =
        config.scheduler.command_interval_nanoseconds / 1'000'000U +
        (config.scheduler.command_interval_nanoseconds % 1'000'000U != 0U
            ? 1U
            : 0U);
    const auto valid_speed = [&config](const float speed) {
        return std::isfinite(speed) && speed >= 0.0F &&
               speed <= config.limits.maximum_move_magnitude;
    };
    return config.lerp_msec <= config.limits.maximum_lerp_msec &&
           interval_msec <= config.limits.maximum_msec &&
           config.scheduler.maximum_command_sequence <=
               config.limits.maximum_command_sequence &&
           config.history.maximum_entries <=
               config.limits.maximum_history_entries &&
           config.history.protected_backup_window >=
               config.planner.desired_backup_commands &&
           config.planner.maximum_backup_commands <=
               config.limits.maximum_backup_commands &&
           config.planner.maximum_new_commands <=
               config.limits.maximum_new_commands &&
           config.planner.maximum_commands_per_packet <=
               config.limits.maximum_commands_per_packet &&
           config.planner.maximum_packet_bits <=
               config.limits.maximum_encoded_bits &&
           config.planner.maximum_packet_bytes <=
               config.limits.maximum_encoded_bytes &&
           valid_speed(config.movement_speeds.forward_speed) &&
           valid_speed(config.movement_speeds.backward_speed) &&
           valid_speed(config.movement_speeds.side_speed);
}

[[nodiscard]] constexpr bool valid_prerequisite_profile(
    const GoldSrcUserCmdSessionPrerequisiteProfile profile) noexcept
{
    return profile == GoldSrcUserCmdSessionPrerequisiteProfile::
                          synthetic_runtime_ready_v1 ||
        profile == GoldSrcUserCmdSessionPrerequisiteProfile::reference_loopback_test_ready_v1 ||
        profile == GoldSrcUserCmdSessionPrerequisiteProfile::production_live_runtime_ready_v1 ||
        profile == GoldSrcUserCmdSessionPrerequisiteProfile::
                       stock_runtime_ready_evidence_pending;
}

[[nodiscard]] constexpr bool sufficient_event_capacity(
    const GoldSrcUserCmdTransmissionConfig& config) noexcept
{
    if (config.maximum_events < kMaximumMovePacketEventCount) {
        return false;
    }
    return config.scheduler.maximum_commands_per_update <=
        (config.maximum_events - kMaximumMovePacketEventCount) / 3U;
}

} // namespace

GoldSrcUserCmdTransmissionStage::GoldSrcUserCmdTransmissionStage(
    NetchanDriver& driver,
    const GoldSrcUserCmdSchemaBinding& binding,
    const GoldSrcUserCmdSessionPrerequisite prerequisite,
    GoldSrcUserCmdTransmissionConfig config)
    : driver_{driver},
      binding_{binding},
      prerequisite_{prerequisite},
      config_{config},
      scheduler_{config_.scheduler},
      history_{config_.history},
      planner_{config_.planner},
      valid_configuration_{
          valid_prerequisite_profile(prerequisite_.profile) &&
          valid_goldsrc_usercmd_limits(config_.limits) &&
          scheduler_.valid_configuration() && history_.valid_configuration() &&
          planner_.valid_configuration() &&
          stage_components_within_limits(config_) &&
          config_.timeout.count() > 0 &&
          config_.timeout <= std::chrono::milliseconds{300'000} &&
          config_.maximum_events > 0U && config_.maximum_events <= 1'024U &&
          sufficient_event_capacity(config_) &&
          config_.maximum_transmission_phases_per_update > 0U &&
          config_.maximum_transmission_phases_per_update <= 2U}
{
    const bool reference = config_.planner.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1;
    if (reference) {
        valid_configuration_ = planner_.valid_configuration() && history_.valid_configuration() &&
            config_.history.profile == GoldSrcUserCmdHistoryProfile::reference_wire_v1 &&
            config_.history.protected_backup_window >= config_.planner.desired_backup_commands &&
            binding_.profile() == GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1 &&
            (prerequisite_.profile == GoldSrcUserCmdSessionPrerequisiteProfile::reference_loopback_test_ready_v1 ||
             (prerequisite_.profile == GoldSrcUserCmdSessionPrerequisiteProfile::production_live_runtime_ready_v1 &&
              prerequisite_.runtime_ready && prerequisite_.session_generation != 0U &&
              prerequisite_.session_generation == config_.history.generation)) &&
            config_.timeout.count() > 0 && config_.timeout <= std::chrono::milliseconds{300'000} &&
            config_.maximum_events >= kMaximumMovePacketEventCount && config_.maximum_events <= 1024 &&
            config_.maximum_transmission_phases_per_update > 0 && config_.maximum_transmission_phases_per_update <= 2;
    } else if (config_.history.profile != GoldSrcUserCmdHistoryProfile::synthetic_v1 ||
               prerequisite_.profile == GoldSrcUserCmdSessionPrerequisiteProfile::reference_loopback_test_ready_v1) {
        valid_configuration_ = false;
    }
    if (valid_configuration_) {
        events_.reserve(config_.maximum_events);
    }
}

bool GoldSrcUserCmdTransmissionStage::valid_configuration() const noexcept
{
    return valid_configuration_;
}

GoldSrcUserCmdTransmissionState
GoldSrcUserCmdTransmissionStage::state() const noexcept
{
    return state_;
}

bool GoldSrcUserCmdTransmissionStage::terminal() const noexcept
{
    return terminal_state(state_);
}

std::size_t GoldSrcUserCmdTransmissionStage::sampled_command_count() const noexcept
{
    return sampled_command_count_;
}

std::size_t
GoldSrcUserCmdTransmissionStage::transmitted_packet_count() const noexcept
{
    return transmitted_packet_count_;
}

std::size_t GoldSrcUserCmdTransmissionStage::new_command_submission_count()
    const noexcept
{
    return new_command_submission_count_;
}

std::size_t GoldSrcUserCmdTransmissionStage::backup_command_submission_count()
    const noexcept
{
    return backup_command_submission_count_;
}

GoldSrcUserCmdHistoryState GoldSrcUserCmdTransmissionStage::history() const
{
    return history_.publish();
}

const std::optional<GoldSrcUserCmdTransmissionError>&
GoldSrcUserCmdTransmissionStage::last_error() const noexcept
{
    return last_error_;
}

std::optional<GoldSrcUserCmdTransmissionEvent>
GoldSrcUserCmdTransmissionStage::poll_event()
{
    if (next_event_index_ >= events_.size()) {
        return std::nullopt;
    }
    auto event = events_[next_event_index_++];
    if (next_event_index_ == events_.size()) {
        events_.clear();
        next_event_index_ = 0U;
    }
    return event;
}

bool GoldSrcUserCmdTransmissionStage::push_event(
    GoldSrcUserCmdTransmissionEvent event) noexcept
{
    if (events_.size() == config_.maximum_events && next_event_index_ != 0U) {
        events_.erase(
            events_.begin(),
            events_.begin() + static_cast<std::ptrdiff_t>(next_event_index_));
        next_event_index_ = 0U;
    }
    if (events_.size() >= config_.maximum_events) {
        return false;
    }
    events_.push_back(std::move(event));
    return true;
}

void GoldSrcUserCmdTransmissionStage::abandon_prepared_move() noexcept
{
    if (!prepared_move_) {
        return;
    }
    try {
        static_cast<void>(
            driver_.abandon_unreliable(std::move(prepared_move_->context)));
    } catch (...) {
        // Terminal cleanup must remain noexcept. The owning plan is still
        // destroyed below and the driver lifetime is closed by the caller.
    }
    static_cast<void>(
        planner_.abandon(std::move(prepared_move_->packet)));
    prepared_move_.reset();
}

GoldSrcUserCmdTransmissionOperationResult
GoldSrcUserCmdTransmissionStage::commit_prepared_move(
    const NetchanDriverTimePoint now)
{
    if (!prepared_move_) {
        return operation_error(GoldSrcUserCmdTransmissionError{
            GoldSrcUserCmdTransmissionErrorCode::packet_context_failed,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "No prepared usercmd move is available for submission"});
    }

    const auto context_identity = prepared_move_->context_identity;
    const auto encoded_bytes = prepared_move_->encoded_bytes;
    const auto encoded_bits = prepared_move_->encoded_bits;
    const auto changed_field_count = prepared_move_->changed_field_count;
    const auto outgoing_sequence = prepared_move_->outgoing_sequence;
    const auto new_count = prepared_move_->packet.new_command_count();
    const auto backup_count = prepared_move_->packet.backup_command_count();
    const auto first_new_command_sequence =
        prepared_move_->first_new_command_sequence;
    const auto last_new_command_sequence =
        prepared_move_->last_new_command_sequence;
    const auto preflight = planner_.preflight(history_, prepared_move_->packet);
    if (!preflight || transmitted_packet_count_ == std::numeric_limits<std::size_t>::max()) {
        return fail(GoldSrcUserCmdTransmissionState::protocol_error,
            {GoldSrcUserCmdTransmissionErrorCode::history_failed, {}, {}, {}, {}, {}, "Pre-send accounting preflight failed"},
            GoldSrcUserCmdTransmissionEventType::protocol_error, now, true);
    }
    bool bound_now = false;
    if (!prepared_move_->driver_owned) {
    const auto submitted = driver_.commit_unreliable(
        std::move(prepared_move_->context),
        prepared_move_->packet.encoded_bytes());
    if (!submitted) {
        static_cast<void>(
            planner_.abandon(std::move(prepared_move_->packet)));
        prepared_move_.reset();
        if (submitted.error &&
            submitted.error->code ==
                NetchanDriverErrorCode::stale_unreliable_context) {
            state_ = GoldSrcUserCmdTransmissionState::waiting_for_packet_context;
            static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
                GoldSrcUserCmdTransmissionEventType::move_context_stale,
                std::nullopt,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                0U,
                outgoing_sequence,
                history_.size(),
            }));
            return {};
        }
        return fail(
            GoldSrcUserCmdTransmissionState::network_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::driver_submission_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                submitted.error ? std::optional{submitted.error->code}
                                : std::nullopt,
                "NetchanDriver rejected the encoded unreliable packet"},
            GoldSrcUserCmdTransmissionEventType::network_error,
            now,
            true);
    }
    prepared_move_->driver_owned = true;
    bound_now = true;
    }

    if (!config_.externally_owned_driver_update) {
        driver_.update(now);
    } else if (bound_now) {
        state_ = GoldSrcUserCmdTransmissionState::waiting_for_unreliable_submission;
        return {};
    }
    if (driver_.last_sent_unreliable_context_identity() != context_identity) {
        if (driver_.state() == NetchanDriverState::active) {
            state_ = GoldSrcUserCmdTransmissionState::unreliable_backpressure;
            return {}; // Driver retains the bound context; never bind/queue twice.
        }
        static_cast<void>(
            planner_.abandon(std::move(prepared_move_->packet)));
        prepared_move_.reset();
        return fail(
            GoldSrcUserCmdTransmissionState::network_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::driver_send_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                driver_.last_error()
                    ? std::optional{driver_.last_error()->code}
                    : std::nullopt,
                "Bound NetchanDriver context produced no matching send receipt"},
            GoldSrcUserCmdTransmissionEventType::network_error,
            now,
            true);
    }
    const auto committed =
        planner_.commit(history_, std::move(prepared_move_->packet));
    prepared_move_.reset();
    if (!committed) {
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::history_failed,
                std::nullopt,
                std::nullopt,
                committed.error && committed.error->history_code
                    ? committed.error->history_code
                    : std::nullopt,
                committed.error ? std::optional{committed.error->code}
                                : std::nullopt,
                std::nullopt,
                "Sent usercmd packet could not be committed to history"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }
    last_progress_at_ = now;
    if (new_count > (std::numeric_limits<std::size_t>::max)() -
            new_command_submission_count_ ||
        backup_count > (std::numeric_limits<std::size_t>::max)() -
            backup_command_submission_count_) {
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            {GoldSrcUserCmdTransmissionErrorCode::counter_exhausted, {}, {},
             {}, {}, {}, "Usercmd submission counters are exhausted"},
            GoldSrcUserCmdTransmissionEventType::protocol_error, now, true);
    }
    ++transmitted_packet_count_;
    new_command_submission_count_ += new_count;
    backup_command_submission_count_ += backup_count;
    state_ = GoldSrcUserCmdTransmissionState::move_packet_submitted;
    auto submitted_event = GoldSrcUserCmdTransmissionEvent{
        GoldSrcUserCmdTransmissionEventType::move_packet_submitted,
        std::nullopt,
        std::nullopt,
        new_count,
        backup_count,
        encoded_bytes,
        encoded_bits,
        changed_field_count,
        outgoing_sequence,
        history_.size(),
    };
    submitted_event.first_new_command_sequence = first_new_command_sequence;
    submitted_event.last_new_command_sequence = last_new_command_sequence;
    static_cast<void>(push_event(std::move(submitted_event)));
    if (backup_count != 0U) {
        static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
            GoldSrcUserCmdTransmissionEventType::backup_commands_included,
            std::nullopt,
            std::nullopt,
            new_count,
            backup_count,
            0U,
            0U,
            0U,
            outgoing_sequence,
            history_.size(),
        }));
    }
    static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
        GoldSrcUserCmdTransmissionEventType::new_commands_included,
        std::nullopt,
        std::nullopt,
        new_count,
        backup_count,
        0U,
        0U,
        0U,
        outgoing_sequence,
        history_.size(),
    }));
    if (driver_.state() != NetchanDriverState::active) {
        return fail(
            GoldSrcUserCmdTransmissionState::network_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::driver_send_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                driver_.last_error()
                    ? std::optional{driver_.last_error()->code}
                    : std::nullopt,
                "Sequence-bound usercmd was sent before the driver became terminal"},
            GoldSrcUserCmdTransmissionEventType::network_error,
            now,
            true);
    }
    state_ = GoldSrcUserCmdTransmissionState::waiting_for_next_sample;
    return {};
}

GoldSrcUserCmdTransmissionOperationResult
GoldSrcUserCmdTransmissionStage::queue_impulse(const std::uint8_t impulse) noexcept
{
    if (terminal() || !valid_configuration_ || config_.planner.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1) {
        return operation_error(GoldSrcUserCmdTransmissionError{
            GoldSrcUserCmdTransmissionErrorCode::not_active,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Impulse requires a valid non-terminal transmission stage"});
    }
    if (pending_impulse_) {
        return operation_error(GoldSrcUserCmdTransmissionError{
            GoldSrcUserCmdTransmissionErrorCode::one_shot_pending,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Only one typed impulse may await command-history insertion"});
    }
    pending_impulse_ = impulse;
    return {};
}

GoldSrcUserCmdTransmissionOperationResult GoldSrcUserCmdTransmissionStage::fail(
    const GoldSrcUserCmdTransmissionState state,
    GoldSrcUserCmdTransmissionError error,
    const GoldSrcUserCmdTransmissionEventType event,
    const NetchanDriverTimePoint now,
    const bool discard_pending_input) noexcept
{
    abandon_prepared_move();
    state_ = state;
    last_error_ = error;
    if (discard_pending_input) {
        pending_pressed_buttons_ = 0U;
        pending_impulse_.reset();
    }
    static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
        event,
        std::nullopt,
        std::nullopt,
        0U,
        0U,
        0U,
        0U,
        0U,
        std::nullopt,
        history_.size(),
    }));
    // Every fail() transition is terminal for this stage. close() is
    // idempotent and is a no-op for an already-terminal driver, so always
    // release an otherwise-active retained transport/auth lifetime here.
    driver_.close(now);
    return operation_error(std::move(error));
}

GoldSrcUserCmdTransmissionOperationResult
GoldSrcUserCmdTransmissionStage::update(
    const NetchanDriverTimePoint now,
    const gameplay_input::GameplayInputIntent& intent,
    const gameplay_camera::GameplayCameraState& camera)
{
    if (config_.planner.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1)
        return operation_error({GoldSrcUserCmdTransmissionErrorCode::invalid_configuration, {}, {}, {}, {}, {}, "Reference profile requires typed command entry point"});
    if (!valid_configuration_) {
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::invalid_configuration,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Usercmd transmission configuration is invalid"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }
    if (terminal()) {
        return operation_error(last_error_.value_or(
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::not_active,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Usercmd transmission stage is terminal"}));
    }
    if (last_update_ && now < *last_update_) {
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::scheduler_failed,
                GoldSrcUserCmdSchedulerErrorCode::time_moved_backwards,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Usercmd transmission time moved backwards"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }
    if (!last_progress_at_) {
        last_progress_at_ = now;
    }
    last_update_ = now;
    if (timeout_elapsed(*last_progress_at_, now, config_.timeout)) {
        return fail(
            GoldSrcUserCmdTransmissionState::timed_out,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::timed_out,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Usercmd transmission stalled-progress timeout elapsed"},
            GoldSrcUserCmdTransmissionEventType::timed_out,
            now,
            true);
    }
    if (prerequisite_.profile !=
            GoldSrcUserCmdSessionPrerequisiteProfile::
                synthetic_runtime_ready_v1 ||
        !prerequisite_.runtime_ready) {
        return fail(
            GoldSrcUserCmdTransmissionState::signon_evidence_pending,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::
                    runtime_signon_evidence_pending,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Stock runtime-ready sign-on semantics remain evidence-pending"},
            GoldSrcUserCmdTransmissionEventType::signon_pending,
            now,
            true);
    }
    if (driver_.state() != NetchanDriverState::active) {
        return fail(
            GoldSrcUserCmdTransmissionState::network_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::not_active,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                driver_.last_error()
                    ? std::optional{driver_.last_error()->code}
                    : std::nullopt,
                "Usercmd transmission requires the retained active NetchanDriver"},
            GoldSrcUserCmdTransmissionEventType::network_error,
            now,
            true);
    }

    if (intent.focused()) {
        pending_pressed_buttons_ |= intent.pressed_buttons();
    } else {
        pending_pressed_buttons_ = 0U;
    }

    // A retained move owns both the exact outgoing netchan context and the
    // packet-planner transaction. Commit it before sampling can revise
    // history. This also makes a one-phase event-loop yield a real ownership
    // boundary at which other driver work may invalidate the context safely.
    if (prepared_move_) {
        return commit_prepared_move(now);
    }

    state_ = GoldSrcUserCmdTransmissionState::sampling_commands;
    auto staged_scheduler = scheduler_;
    const auto scheduled = staged_scheduler.update(
        nanoseconds_since_epoch(now), intent, camera);
    if (!scheduled) {
        return fail(
            scheduled.error &&
                    scheduled.error->code ==
                        GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded
                ? GoldSrcUserCmdTransmissionState::history_backpressure
                : GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::scheduler_failed,
                scheduled.error ? std::optional{scheduled.error->code}
                                : std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Usercmd scheduler rejected the caller update"},
            scheduled.error &&
                    scheduled.error->code ==
                        GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded
                ? GoldSrcUserCmdTransmissionEventType::history_backpressure
                : GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }

    if (scheduled.requests.size() >
        std::numeric_limits<std::size_t>::max() - sampled_command_count_) {
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::scheduler_failed,
                GoldSrcUserCmdSchedulerErrorCode::sequence_exhausted,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Sampled-command metadata count is exhausted"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }

    // Sampling is a batch transaction. Scheduler advancement, history
    // insertion, one-shot consumption, counters, and events become visible
    // together only after every due command has passed validation.
    auto staged_history = history_;
    std::vector<GoldSrcUserCmdTransmissionEvent> staged_events;
    staged_events.reserve(scheduled.requests.size() * 3U);
    bool consume_pending_impulse = false;
    gameplay_input::GameplayButtonMask consumed_pressed_buttons{0U};
    for (std::size_t index = 0U; index < scheduled.requests.size(); ++index) {
        const auto& request = scheduled.requests[index];
        GoldSrcUserCmdBuildContext build_context;
        build_context.command_sequence = request.command_sequence;
        build_context.command_msec = request.command_msec;
        build_context.command_sample_duration_nanoseconds =
            request.sample_duration_nanoseconds;
        build_context.command_sample_time_nanoseconds =
            request.sample_time_nanoseconds;
        build_context.lerp_msec = config_.lerp_msec;
        build_context.movement_speeds = config_.movement_speeds;
        build_context.light_level = config_.light_level;
        if (index == 0U && pending_pressed_buttons_ != 0U) {
            build_context.one_shot_buttons = pending_pressed_buttons_;
        }
        if (index == 0U && pending_impulse_) {
            build_context.impulse = pending_impulse_;
        }
        auto built = adapter_.build(intent, camera, build_context, config_.limits);
        if (!built || !built.command) {
            return fail(
                GoldSrcUserCmdTransmissionState::protocol_error,
                GoldSrcUserCmdTransmissionError{
                    GoldSrcUserCmdTransmissionErrorCode::adapter_failed,
                    std::nullopt,
                    built.error ? std::optional{built.error->code}
                                : std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    "Input adapter could not build a bounded usercmd"},
                GoldSrcUserCmdTransmissionEventType::protocol_error,
                now,
                true);
        }
        const auto inserted = staged_history.insert(*built.command);
        if (!inserted) {
            return fail(
                GoldSrcUserCmdTransmissionState::history_backpressure,
                GoldSrcUserCmdTransmissionError{
                    GoldSrcUserCmdTransmissionErrorCode::history_failed,
                    std::nullopt,
                    std::nullopt,
                    inserted.error ? std::optional{inserted.error->code}
                                   : std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    "Usercmd history rejected a sampled command"},
                GoldSrcUserCmdTransmissionEventType::history_backpressure,
                now,
                true);
        }
        if (built.one_shot_plan) {
            if (!built.one_shot_plan->commit_after_history_insert(
                    built.command->command_sequence())) {
                return fail(
                    GoldSrcUserCmdTransmissionState::protocol_error,
                    GoldSrcUserCmdTransmissionError{
                        GoldSrcUserCmdTransmissionErrorCode::history_failed,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        "One-shot command plan did not match its history insertion"},
                    GoldSrcUserCmdTransmissionEventType::protocol_error,
                    now,
                    true);
            }
            consume_pending_impulse =
                consume_pending_impulse || built.one_shot_plan->consumes_impulse();
            consumed_pressed_buttons |= built.one_shot_plan->consumes_buttons();
        }
        staged_events.push_back(GoldSrcUserCmdTransmissionEvent{
                GoldSrcUserCmdTransmissionEventType::usercmd_sampled,
                built.command->command_sequence().value(),
                built.command->source_input_sequence(),
                0U,
                0U,
                0U,
                0U,
                0U,
                std::nullopt,
                staged_history.size(),
            });
        staged_events.push_back(GoldSrcUserCmdTransmissionEvent{
                GoldSrcUserCmdTransmissionEventType::usercmd_history_inserted,
                built.command->command_sequence().value(),
                built.command->source_input_sequence(),
                0U,
                0U,
                0U,
                0U,
                0U,
                std::nullopt,
                staged_history.size(),
            });
        if (inserted.evicted_count != 0U) {
            staged_events.push_back(GoldSrcUserCmdTransmissionEvent{
                GoldSrcUserCmdTransmissionEventType::history_evicted,
                std::nullopt,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                0U,
                std::nullopt,
                staged_history.size(),
            });
        }
    }

    const auto pending_event_count = events_.size() - next_event_index_;
    const auto available_event_count =
        config_.maximum_events - pending_event_count;
    const auto packet_event_reserve = staged_history.unsent_count() != 0U
        ? kMaximumMovePacketEventCount
        : 0U;
    if (staged_events.size() > available_event_count ||
        packet_event_reserve > available_event_count - staged_events.size()) {
        state_ = GoldSrcUserCmdTransmissionState::event_backpressure;
        return operation_error(GoldSrcUserCmdTransmissionError{
            GoldSrcUserCmdTransmissionErrorCode::event_backpressure,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Drain metadata events before retrying the atomic sampling and move transaction"});
    }
    scheduler_ = std::move(staged_scheduler);
    history_ = std::move(staged_history);
    pending_pressed_buttons_ &= ~consumed_pressed_buttons;
    if (consume_pending_impulse) {
        pending_impulse_.reset();
    }
    sampled_command_count_ += scheduled.requests.size();
    if (!scheduled.requests.empty()) {
        last_progress_at_ = now;
    }
    for (auto& event : staged_events) {
        static_cast<void>(push_event(std::move(event)));
    }

    return transmit_pending(now);
}

bool GoldSrcUserCmdTransmissionStage::reference_ready() const noexcept {
    if (!valid_configuration_ || terminal() || !prerequisite_.runtime_ready ||
        driver_.state() != NetchanDriverState::active ||
        !driver_.local_endpoint()) {
        return false;
    }
    if (prerequisite_.profile == GoldSrcUserCmdSessionPrerequisiteProfile::
            production_live_runtime_ready_v1) {
        return prerequisite_.session_generation != 0U &&
            prerequisite_.session_generation == config_.history.generation;
    }
    return prerequisite_.profile == GoldSrcUserCmdSessionPrerequisiteProfile::
               reference_loopback_test_ready_v1 &&
        driver_.remote_endpoint().ipv4_host_order() == 0x7f000001U &&
        driver_.local_endpoint()->ipv4_host_order() == 0x7f000001U;
}

GoldSrcUserCmdHistoryOperationResult GoldSrcUserCmdTransmissionStage::queue_reference_command(
    GoldSrcUserCmdSequence identity, const GoldSrcWireUserCmd& command, std::uint64_t generation)
{
    if (!reference_ready()) return {{GoldSrcUserCmdHistoryError{GoldSrcUserCmdHistoryErrorCode::invalid_configuration, {}, "Reference insertion requires an active bound session prerequisite"}}, 0};
    if (prepared_move_) return {{GoldSrcUserCmdHistoryError{GoldSrcUserCmdHistoryErrorCode::pending_submission, identity, "Pending plan owns history revision"}}, 0};
    return history_.insert(identity, command, generation);
}

GoldSrcUserCmdTransmissionOperationResult GoldSrcUserCmdTransmissionStage::update_reference(NetchanDriverTimePoint now)
{
    // Account an already-sent receipt even after a separate driver pump sees
    // terminal RX. Sending is not reversible.
    if (!terminal() && prepared_move_ && prepared_move_->driver_owned &&
        driver_.last_sent_unreliable_context_identity() == prepared_move_->context_identity)
        return commit_prepared_move(now);
    if (!reference_ready()) return operation_error({GoldSrcUserCmdTransmissionErrorCode::runtime_signon_evidence_pending, {}, {}, {}, {}, {}, "Explicit active loopback or production-live prerequisite required"});
    if (last_update_ && now < *last_update_) return fail(GoldSrcUserCmdTransmissionState::protocol_error,
        {GoldSrcUserCmdTransmissionErrorCode::packet_context_failed, {}, {}, {}, {}, {}, "Reference update time regressed"}, GoldSrcUserCmdTransmissionEventType::protocol_error, now, true);
    last_update_ = now;
    if (!last_progress_at_) last_progress_at_ = now;
    if (timeout_elapsed(*last_progress_at_, now, config_.timeout)) return fail(GoldSrcUserCmdTransmissionState::timed_out,
        {GoldSrcUserCmdTransmissionErrorCode::timed_out, {}, {}, {}, {}, {}, "Reference sender stalled"}, GoldSrcUserCmdTransmissionEventType::timed_out, now, true);
    if (prepared_move_) return commit_prepared_move(now);
    if (config_.maximum_events - (events_.size() - next_event_index_) < kMaximumMovePacketEventCount) {
        state_ = GoldSrcUserCmdTransmissionState::event_backpressure;
        return operation_error({GoldSrcUserCmdTransmissionErrorCode::event_backpressure, {}, {}, {}, {}, {}, "Drain metadata before preparing another move"});
    }
    return transmit_pending(now);
}

GoldSrcUserCmdTransmissionOperationResult GoldSrcUserCmdTransmissionStage::transmit_pending(NetchanDriverTimePoint now)
{
    if (history_.unsent_count() == 0U) {
        state_ = GoldSrcUserCmdTransmissionState::waiting_for_next_sample;
        return {};
    }
    state_ = GoldSrcUserCmdTransmissionState::waiting_for_packet_context;
    auto context = driver_.prepare_unreliable_context();
    if (!context || !context.plan) {
        const auto driver_code = context.error
            ? std::optional{context.error->code}
            : std::nullopt;
        if (driver_code == NetchanDriverErrorCode::unreliable_context_unavailable ||
            driver_code == NetchanDriverErrorCode::unreliable_payload_pending) {
            state_ = GoldSrcUserCmdTransmissionState::unreliable_backpressure;
            static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
                GoldSrcUserCmdTransmissionEventType::unreliable_backpressure,
                std::nullopt,
                std::nullopt,
                0U,
                0U,
                0U,
                0U,
                0U,
                std::nullopt,
                history_.size(),
            }));
            // The retained driver owns legacy/contextual unreliable work and
            // reliable fragment progress. Let one bounded driver update drain
            // that work; otherwise this stage would re-observe the same
            // backpressure forever and could never reach a fresh context.
            if (!config_.externally_owned_driver_update) driver_.update(now);
            if (driver_.state() != NetchanDriverState::active) {
                return fail(
                    GoldSrcUserCmdTransmissionState::network_error,
                    GoldSrcUserCmdTransmissionError{
                        GoldSrcUserCmdTransmissionErrorCode::driver_send_failed,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        driver_.last_error()
                            ? std::optional{driver_.last_error()->code}
                            : std::nullopt,
                        "NetchanDriver failed while draining unrelated pending work"},
                    GoldSrcUserCmdTransmissionEventType::network_error,
                    now,
                    true);
            }
            return {};
        }
        return fail(
            GoldSrcUserCmdTransmissionState::network_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::packet_context_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                driver_code,
                "NetchanDriver could not prepare an outgoing checksum context"},
            GoldSrcUserCmdTransmissionEventType::network_error,
            now,
            true);
    }

    const auto outgoing_sequence = context.plan->next_outgoing_sequence().value();
    state_ = GoldSrcUserCmdTransmissionState::planning_move_packet;
    auto packet_plan = config_.planner.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1
        ? planner_.prepare(history_.publish(), binding_, *context.plan)
        : planner_.prepare(history_.publish(), binding_, outgoing_sequence);
    if (!packet_plan || !packet_plan.plan) {
        static_cast<void>(driver_.abandon_unreliable(std::move(*context.plan)));
        if (config_.planner.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1 &&
            packet_plan.error->code == GoldSrcUserCmdPacketPlannerErrorCode::packet_budget_exceeded) {
            state_ = GoldSrcUserCmdTransmissionState::unreliable_backpressure;
            if (!config_.externally_owned_driver_update) driver_.update(now);
            if (driver_.state() == NetchanDriverState::active) return {};
        }
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::packet_plan_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                packet_plan.error
                    ? std::optional{packet_plan.error->code}
                    : std::nullopt,
                std::nullopt,
                "Usercmd packet planner could not prepare the unsent range"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }
    const auto encoded_bytes = packet_plan.plan->expected_encoded_bytes();
    const auto encoded_bits = packet_plan.plan->expected_encoded_bits();
    const auto changed_field_count = packet_plan.plan->changed_field_count();
    if (encoded_bytes >
        context.plan->maximum_unreliable_payload_size()) {
        static_cast<void>(planner_.abandon(std::move(*packet_plan.plan)));
        static_cast<void>(driver_.abandon_unreliable(std::move(*context.plan)));
        state_ = GoldSrcUserCmdTransmissionState::unreliable_backpressure;
        if (!push_event(GoldSrcUserCmdTransmissionEvent{
                GoldSrcUserCmdTransmissionEventType::unreliable_backpressure,
                std::nullopt,
                std::nullopt,
                0U,
                0U,
                encoded_bytes,
                encoded_bits,
                changed_field_count,
                outgoing_sequence,
                history_.size(),
            })) {
            return fail(
                GoldSrcUserCmdTransmissionState::protocol_error,
                GoldSrcUserCmdTransmissionError{
                    GoldSrcUserCmdTransmissionErrorCode::event_backpressure,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    "Dynamic netchan capacity backpressure could not publish metadata"},
                GoldSrcUserCmdTransmissionEventType::protocol_error,
                now,
                true);
        }
        // The encoded packet is valid under the static packet budget. Only
        // this reliable composition is too narrow, so retain unsent history
        // and let the exact driver make one bounded progress step.
        if (!config_.externally_owned_driver_update) driver_.update(now);
        if (driver_.state() != NetchanDriverState::active) {
            return fail(
                GoldSrcUserCmdTransmissionState::network_error,
                GoldSrcUserCmdTransmissionError{
                    GoldSrcUserCmdTransmissionErrorCode::driver_send_failed,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    driver_.last_error()
                        ? std::optional{driver_.last_error()->code}
                        : std::nullopt,
                    "NetchanDriver failed while draining a narrow reliable composition"},
                GoldSrcUserCmdTransmissionEventType::network_error,
                now,
                true);
        }
        return {};
    }
    // Keep one terminal-event slot available in case the contextual datagram
    // is sent and later RX work makes the driver terminal in the same update.
    const auto packet_event_count = kMovePacketEventCountWithoutBackup +
        (packet_plan.plan->backup_command_count() != 0U ? 1U : 0U);
    const auto pending_packet_event_count = events_.size() - next_event_index_;
    if (packet_event_count >
        config_.maximum_events - pending_packet_event_count) {
        static_cast<void>(planner_.abandon(std::move(*packet_plan.plan)));
        static_cast<void>(driver_.abandon_unreliable(std::move(*context.plan)));
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::event_backpressure,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Move-packet metadata would exceed the bounded event queue"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }
    if (transmitted_packet_count_ ==
        std::numeric_limits<std::size_t>::max()) {
        static_cast<void>(planner_.abandon(std::move(*packet_plan.plan)));
        static_cast<void>(driver_.abandon_unreliable(std::move(*context.plan)));
        return fail(
            GoldSrcUserCmdTransmissionState::protocol_error,
            GoldSrcUserCmdTransmissionError{
                GoldSrcUserCmdTransmissionErrorCode::counter_exhausted,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Transmitted usercmd packet count is exhausted"},
            GoldSrcUserCmdTransmissionEventType::protocol_error,
            now,
            true);
    }
    static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
        GoldSrcUserCmdTransmissionEventType::move_plan_prepared,
        std::nullopt,
        std::nullopt,
        packet_plan.plan->new_command_count(),
        packet_plan.plan->backup_command_count(),
        encoded_bytes,
        encoded_bits,
        changed_field_count,
        outgoing_sequence,
        history_.size(),
    }));
    static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
        GoldSrcUserCmdTransmissionEventType::move_packet_encoded,
        std::nullopt,
        std::nullopt,
        packet_plan.plan->new_command_count(),
        packet_plan.plan->backup_command_count(),
        encoded_bytes,
        encoded_bits,
        changed_field_count,
        outgoing_sequence,
        history_.size(),
    }));

    state_ = GoldSrcUserCmdTransmissionState::waiting_for_unreliable_submission;
    const auto outgoing_context_identity = context.plan->plan_identity();
    prepared_move_.emplace(PreparedMove{
        std::move(*context.plan),
        std::move(*packet_plan.plan),
        outgoing_context_identity,
        encoded_bytes,
        encoded_bits,
        changed_field_count,
        outgoing_sequence,
    });
    const auto& selected = prepared_move_->packet.ordered_sequences();
    const auto backup_count = prepared_move_->packet.backup_command_count();
    if (prepared_move_->packet.new_command_count() != 0U &&
        backup_count < selected.size()) {
        prepared_move_->first_new_command_sequence =
            selected[backup_count].value();
        prepared_move_->last_new_command_sequence = selected.back().value();
    }
    if (config_.maximum_transmission_phases_per_update == 1U) {
        return {};
    }
    return commit_prepared_move(now);
}

void GoldSrcUserCmdTransmissionStage::cancel(
    const NetchanDriverTimePoint now) noexcept
{
    if (terminal()) {
        return;
    }
    pending_pressed_buttons_ = 0U;
    pending_impulse_.reset();
    abandon_prepared_move();
    driver_.cancel(now);
    state_ = GoldSrcUserCmdTransmissionState::cancelled;
    last_error_ = GoldSrcUserCmdTransmissionError{
        GoldSrcUserCmdTransmissionErrorCode::cancelled,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "Usercmd transmission was cancelled"};
    static_cast<void>(push_event(GoldSrcUserCmdTransmissionEvent{
        GoldSrcUserCmdTransmissionEventType::cancelled,
        std::nullopt,
        std::nullopt,
        0U,
        0U,
        0U,
        0U,
        0U,
        std::nullopt,
        history_.size(),
    }));
}

void GoldSrcUserCmdTransmissionStage::close(
    const NetchanDriverTimePoint now) noexcept
{
    if (state_ == GoldSrcUserCmdTransmissionState::closed) {
        return;
    }
    pending_pressed_buttons_ = 0U;
    pending_impulse_.reset();
    abandon_prepared_move();
    driver_.close(now);
    state_ = GoldSrcUserCmdTransmissionState::closed;
}

} // namespace hlclient::goldsrc
