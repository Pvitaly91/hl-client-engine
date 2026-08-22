#include <hlclient/goldsrc/movement_environment_stage.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const MovementEnvironmentStageState state) noexcept
{
    switch (state) {
    case MovementEnvironmentStageState::post_environment_boundary_reached:
    case MovementEnvironmentStageState::unsupported_message:
    case MovementEnvironmentStageState::timed_out:
    case MovementEnvironmentStageState::cancelled:
    case MovementEnvironmentStageState::backpressure:
    case MovementEnvironmentStageState::secondary_stream_pending_m3:
    case MovementEnvironmentStageState::network_error:
    case MovementEnvironmentStageState::protocol_error:
        return true;
    case MovementEnvironmentStageState::idle:
    case MovementEnvironmentStageState::waiting_for_delta_state:
    case MovementEnvironmentStageState::decoding_move_vars:
    case MovementEnvironmentStageState::environment_state_ready:
    case MovementEnvironmentStageState::decoding_post_environment_messages:
        return false;
    }
    return true;
}

[[nodiscard]] MovementEnvironmentTraceClassification trace_for_terminal_state(
    const MovementEnvironmentStageState state) noexcept
{
    switch (state) {
    case MovementEnvironmentStageState::post_environment_boundary_reached:
        return MovementEnvironmentTraceClassification::
            post_environment_boundary_reached;
    case MovementEnvironmentStageState::unsupported_message:
        return MovementEnvironmentTraceClassification::unsupported_message;
    case MovementEnvironmentStageState::timed_out:
        return MovementEnvironmentTraceClassification::stage_timed_out;
    case MovementEnvironmentStageState::cancelled:
        return MovementEnvironmentTraceClassification::stage_cancelled;
    case MovementEnvironmentStageState::backpressure:
        return MovementEnvironmentTraceClassification::backpressure;
    case MovementEnvironmentStageState::secondary_stream_pending_m3:
        return MovementEnvironmentTraceClassification::secondary_stream_pending_m3;
    case MovementEnvironmentStageState::network_error:
        return MovementEnvironmentTraceClassification::network_failure;
    case MovementEnvironmentStageState::idle:
    case MovementEnvironmentStageState::waiting_for_delta_state:
    case MovementEnvironmentStageState::decoding_move_vars:
    case MovementEnvironmentStageState::environment_state_ready:
    case MovementEnvironmentStageState::decoding_post_environment_messages:
    case MovementEnvironmentStageState::protocol_error:
        return MovementEnvironmentTraceClassification::protocol_failure;
    }
    return MovementEnvironmentTraceClassification::protocol_failure;
}

[[nodiscard]] bool delta_network_error(
    const std::optional<NetchanDriverErrorCode> code) noexcept
{
    return code == NetchanDriverErrorCode::receive_failed ||
           code == NetchanDriverErrorCode::send_failed;
}

[[nodiscard]] std::size_t control_string_length(
    const PostMoveVarsControl& control) noexcept
{
    if (const auto* const definition =
            std::get_if<PostMoveVarsUserMessageDefinition>(&control.body())) {
        return definition->name.size();
    }
    if (const auto* const text =
            std::get_if<PostMoveVarsStringControl>(&control.body())) {
        return text->value.size();
    }
    return 0U;
}

} // namespace

bool valid_movement_environment_stage_configuration(
    const MovementEnvironmentStageConfig& config) noexcept
{
    return valid_delta_description_stage_configuration(config.delta) &&
           valid_move_vars_limits(config.move_vars) &&
           config.maximum_events > 0U &&
           config.maximum_events <= kMaximumMovementEnvironmentStageEvents;
}

MovementEnvironmentSignonState::MovementEnvironmentSignonState(
    DeltaDescriptionSignonState delta_description,
    MoveVarsStreamState stream) noexcept
    : delta_description_{std::move(delta_description)},
      stream_{std::move(stream)}
{
}

const DeltaDescriptionSignonState&
MovementEnvironmentSignonState::delta_description() const noexcept
{
    return delta_description_;
}

const MoveVarsStreamState& MovementEnvironmentSignonState::stream() const noexcept
{
    return stream_;
}

const MoveVarsState& MovementEnvironmentSignonState::move_vars() const noexcept
{
    return stream_.move_vars();
}

const PostMoveVarsBoundary&
MovementEnvironmentSignonState::boundary() const noexcept
{
    return stream_.boundary();
}

std::size_t MovementEnvironmentSignonState::control_count() const noexcept
{
    return stream_.control_count();
}

std::size_t MovementEnvironmentSignonState::bytes_consumed() const noexcept
{
    return stream_.bytes_consumed();
}

MovementEnvironmentStage::MovementEnvironmentStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    MovementEnvironmentStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback trace_callback)
    : config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{
          valid_movement_environment_stage_configuration(config_)},
      delta_stage_{
          transport,
          remote_endpoint,
          config_.delta,
          [this, callback = std::move(initial_trace_callback)](
              const InitialSignonTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try {
                  callback(event);
              } catch (...) {
              }
              trace_callback_active_ = false;
          },
          [this, callback = std::move(pre_resource_trace_callback)](
              const PreResourceSignonTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try {
                  callback(event);
              } catch (...) {
              }
              trace_callback_active_ = false;
          },
          [this, callback = std::move(delta_trace_callback)](
              const DeltaDescriptionTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try {
                  callback(event);
              } catch (...) {
              }
              trace_callback_active_ = false;
          },
          DeltaDescriptionStage::RetainConnectionAtBoundary{}},
      event_slots_(configuration_valid_ ? config_.maximum_events : 0U)
{
}

MovementEnvironmentStage::~MovementEnvironmentStage() = default;

bool MovementEnvironmentStage::start(
    const MovementEnvironmentStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != MovementEnvironmentStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        set_error(
            MovementEnvironmentStageErrorCode::invalid_configuration,
            MovementEnvironmentStageState::protocol_error,
            "Movement/environment stage configuration is outside project bounds");
        emit_trace(MovementEnvironmentTraceClassification::protocol_failure);
        return false;
    }

    bool started = false;
    try {
        started = delta_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try {
            delta_stage_.cancel(now);
        } catch (...) {
        }
        delta_stage_.finalize_retained_boundary(now);
        set_error(
            MovementEnvironmentStageErrorCode::delta_start_failed,
            MovementEnvironmentStageState::protocol_error,
            "Nested delta-description stage threw during start");
        emit_trace(MovementEnvironmentTraceClassification::protocol_failure);
        return false;
    }
    drain_delta_events();
    if (!started) {
        fail_from_delta();
        if (error_) {
            error_->code = MovementEnvironmentStageErrorCode::delta_start_failed;
        } else {
            set_error(
                MovementEnvironmentStageErrorCode::delta_start_failed,
                MovementEnvironmentStageState::protocol_error,
                "Nested delta-description stage rejected start");
            emit_trace(MovementEnvironmentTraceClassification::protocol_failure);
        }
        return false;
    }

    state_ = MovementEnvironmentStageState::waiting_for_delta_state;
    emit_trace(MovementEnvironmentTraceClassification::stage_started);
    return true;
}

void MovementEnvironmentStage::update(
    const MovementEnvironmentStageTimePoint now)
{
    if (trace_callback_active_ || state_ == MovementEnvironmentStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (state_ != MovementEnvironmentStageState::waiting_for_delta_state) {
        return;
    }

    drain_delta_events();
    try {
        delta_stage_.update(now);
    } catch (...) {
        try {
            delta_stage_.cancel(now);
        } catch (...) {
        }
        delta_stage_.finalize_retained_boundary(now);
        set_error(
            MovementEnvironmentStageErrorCode::delta_failed,
            MovementEnvironmentStageState::protocol_error,
            "Nested delta-description stage threw during update");
        emit_trace(MovementEnvironmentTraceClassification::protocol_failure);
        return;
    }
    drain_delta_events();
    synchronize_from_delta(now);
}

void MovementEnvironmentStage::cancel(
    const MovementEnvironmentStageTimePoint now)
{
    if (trace_callback_active_ || state_ == MovementEnvironmentStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    try {
        delta_stage_.cancel(now);
    } catch (...) {
        delta_stage_.finalize_retained_boundary(now);
        set_error(
            MovementEnvironmentStageErrorCode::delta_failed,
            MovementEnvironmentStageState::protocol_error,
            "Nested delta-description stage threw during cancellation");
        emit_trace(MovementEnvironmentTraceClassification::protocol_failure);
        return;
    }
    drain_delta_events();
    state_ = MovementEnvironmentStageState::cancelled;
    emit_trace(MovementEnvironmentTraceClassification::stage_cancelled);
}

std::optional<MovementEnvironmentStageEvent>
MovementEnvironmentStage::poll_event()
{
    if (event_size_ == 0U) {
        return std::nullopt;
    }
    auto event = std::move(event_slots_[event_head_]);
    event_slots_[event_head_].reset();
    event_head_ = (event_head_ + 1U) % event_slots_.size();
    --event_size_;
    return event;
}

MovementEnvironmentStageState MovementEnvironmentStage::state() const noexcept
{
    return state_;
}

bool MovementEnvironmentStage::terminal() const noexcept
{
    return terminal_state(state_);
}

const std::optional<MovementEnvironmentSignonState>&
MovementEnvironmentStage::result() const noexcept
{
    return result_;
}

const std::optional<MovementEnvironmentStageError>&
MovementEnvironmentStage::error() const noexcept
{
    return error_;
}

const network::NetworkAddress&
MovementEnvironmentStage::remote_endpoint() const noexcept
{
    return delta_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
MovementEnvironmentStage::local_endpoint() const noexcept
{
    return delta_stage_.local_endpoint();
}

std::size_t MovementEnvironmentStage::pending_event_count() const noexcept
{
    return event_size_;
}

std::size_t MovementEnvironmentStage::transmitted_packet_count() const noexcept
{
    return delta_stage_.transmitted_packet_count();
}

std::size_t MovementEnvironmentStage::cleanup_count() const noexcept
{
    return delta_stage_.cleanup_count();
}

std::size_t MovementEnvironmentStage::request_queue_count() const noexcept
{
    return delta_stage_.request_queue_count();
}

bool MovementEnvironmentStage::can_push_events(
    const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void MovementEnvironmentStage::push_event(
    MovementEnvironmentStageEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void MovementEnvironmentStage::drain_delta_events() noexcept
{
    while (delta_stage_.poll_event()) {
    }
}

void MovementEnvironmentStage::synchronize_from_delta(
    const MovementEnvironmentStageTimePoint now)
{
    if (delta_stage_.state() ==
        DeltaDescriptionStageState::post_delta_boundary_reached) {
        emit_trace(MovementEnvironmentTraceClassification::delta_boundary_reached);
        decode_retained_move_vars_stream(now);
        return;
    }
    if (delta_stage_.terminal() || delta_stage_.error()) {
        fail_from_delta();
    }
}

void MovementEnvironmentStage::decode_retained_move_vars_stream(
    const MovementEnvironmentStageTimePoint now)
{
    const auto& delta_result = delta_stage_.result();
    const auto* const source_payload = delta_stage_.retained_source_payload();
    if (!delta_result || source_payload == nullptr) {
        fail_after_retained_boundary(
            MovementEnvironmentStageErrorCode::retained_payload_missing,
            MovementEnvironmentStageState::protocol_error,
            "Delta continuation has no retained owning payload",
            now);
        return;
    }

    state_ = MovementEnvironmentStageState::decoding_move_vars;
    std::optional<MoveVarsStreamDecodeResult> decoded;
    try {
        const MoveVarsStreamDecoder decoder{config_.move_vars};
        decoded.emplace(decoder.decode(
            source_payload->bytes,
            delta_result->boundary()));
    } catch (...) {
        fail_after_retained_boundary(
            MovementEnvironmentStageErrorCode::move_vars_stream_decode_failed,
            MovementEnvironmentStageState::protocol_error,
            "Bounded movement/environment continuation threw",
            now);
        return;
    }

    if (!*decoded || !decoded->state) {
        const auto context = decoded->error
            ? std::string_view{decoded->error->context}
            : std::string_view{"Movement/environment stream returned no owning state"};
        fail_after_retained_boundary(
            MovementEnvironmentStageErrorCode::move_vars_stream_decode_failed,
            decoded->error && decoded->error->code ==
                    MoveVarsStreamErrorCode::unsupported_post_movevars_opcode
                ? MovementEnvironmentStageState::unsupported_message
                : MovementEnvironmentStageState::protocol_error,
            context,
            now,
            decoded->error ? std::optional{decoded->error->code} : std::nullopt,
            decoded->error ? decoded->error->parser_code : std::nullopt);
        return;
    }

    const auto& stream_candidate = *decoded->state;
    if (stream_candidate.control_count() >
            (std::numeric_limits<std::size_t>::max)() - 2U ||
        decoded->required_event_count != stream_candidate.control_count() + 2U) {
        fail_after_retained_boundary(
            MovementEnvironmentStageErrorCode::move_vars_stream_decode_failed,
            MovementEnvironmentStageState::protocol_error,
            "Movement/environment stream returned inconsistent event metadata",
            now);
        return;
    }
    if (decoded->required_event_count > config_.maximum_events ||
        !can_push_events(decoded->required_event_count)) {
        fail_after_retained_boundary(
            MovementEnvironmentStageErrorCode::event_backpressure,
            MovementEnvironmentStageState::backpressure,
            "Movement/environment result exceeds bounded event capacity",
            now);
        return;
    }

    state_ = MovementEnvironmentStageState::decoding_post_environment_messages;
    std::vector<MovementEnvironmentStageEvent> candidate_events;
    std::optional<MovementEnvironmentSignonState> candidate_result;
    try {
        candidate_events.reserve(decoded->required_event_count);
        candidate_events.push_back(MovementEnvironmentStageEvent{
            MovementEnvironmentStageEventType::movement_environment_ready,
            0U,
            kMoveVarsOpcode,
            stream_candidate.move_vars().source_message_offset(),
            stream_candidate.move_vars().message_bytes(),
            stream_candidate.move_vars().sky_name().size(),
            now,
        });
        std::size_t control_index = 0U;
        for (const auto& control : stream_candidate.controls()) {
            candidate_events.push_back(MovementEnvironmentStageEvent{
                MovementEnvironmentStageEventType::post_environment_control,
                control_index,
                control.opcode(),
                control.byte_offset(),
                control.byte_count(),
                control_string_length(control),
                now,
            });
            ++control_index;
        }
        const auto& boundary = stream_candidate.boundary();
        candidate_events.push_back(MovementEnvironmentStageEvent{
            MovementEnvironmentStageEventType::post_environment_boundary,
            0U,
            boundary.opcode(),
            boundary.byte_offset(),
            boundary.remaining_byte_count(),
            0U,
            now,
        });
        auto built_result = MovementEnvironmentSignonState{
            *delta_result,
            std::move(*decoded->state)};
        candidate_result.emplace(std::move(built_result));
    } catch (...) {
        fail_after_retained_boundary(
            MovementEnvironmentStageErrorCode::move_vars_stream_decode_failed,
            MovementEnvironmentStageState::protocol_error,
            "Unable to allocate bounded owning movement/environment result",
            now);
        return;
    }

    result_.emplace(std::move(*candidate_result));
    for (auto& event : candidate_events) {
        push_event(std::move(event));
    }
    state_ = MovementEnvironmentStageState::environment_state_ready;
    emit_trace(
        MovementEnvironmentTraceClassification::movement_environment_ready,
        0U,
        kMoveVarsOpcode,
        result_->move_vars().source_message_offset(),
        result_->move_vars().message_bytes(),
        result_->move_vars().sky_name().size(),
        &result_->move_vars(),
        result_->control_count());

    std::size_t control_index = 0U;
    for (const auto& control : result_->stream().controls()) {
        emit_trace(
            MovementEnvironmentTraceClassification::post_environment_control,
            control_index,
            control.opcode(),
            control.byte_offset(),
            control.byte_count(),
            control_string_length(control));
        ++control_index;
    }

    const auto& boundary = result_->boundary();
    state_ = MovementEnvironmentStageState::post_environment_boundary_reached;
    delta_stage_.finalize_retained_boundary(now);
    emit_trace(
        trace_for_terminal_state(state_),
        0U,
        boundary.opcode(),
        boundary.byte_offset(),
        boundary.remaining_byte_count());
}

void MovementEnvironmentStage::fail_from_delta() noexcept
{
    const auto& nested_error = delta_stage_.error();
    const auto driver_code = nested_error ? nested_error->driver_code : std::nullopt;
    switch (delta_stage_.state()) {
    case DeltaDescriptionStageState::timed_out:
        state_ = MovementEnvironmentStageState::timed_out;
        break;
    case DeltaDescriptionStageState::cancelled:
        state_ = MovementEnvironmentStageState::cancelled;
        break;
    case DeltaDescriptionStageState::network_error:
        state_ = MovementEnvironmentStageState::network_error;
        break;
    case DeltaDescriptionStageState::unsupported_message:
        state_ = MovementEnvironmentStageState::unsupported_message;
        break;
    case DeltaDescriptionStageState::backpressure:
        state_ = MovementEnvironmentStageState::backpressure;
        break;
    case DeltaDescriptionStageState::secondary_stream_pending_m3:
        state_ = MovementEnvironmentStageState::secondary_stream_pending_m3;
        break;
    case DeltaDescriptionStageState::idle:
        state_ = delta_network_error(driver_code)
            ? MovementEnvironmentStageState::network_error
            : MovementEnvironmentStageState::protocol_error;
        break;
    case DeltaDescriptionStageState::waiting_for_pre_resource_state:
    case DeltaDescriptionStageState::decoding_delta_stream:
    case DeltaDescriptionStageState::delta_registry_ready:
    case DeltaDescriptionStageState::post_delta_boundary_reached:
    case DeltaDescriptionStageState::protocol_error:
        state_ = MovementEnvironmentStageState::protocol_error;
        break;
    }

    set_error(
        MovementEnvironmentStageErrorCode::delta_failed,
        state_,
        nested_error
            ? std::string_view{nested_error->context}
            : std::string_view{"Nested delta-description stage terminated without an error"},
        nested_error ? std::optional{nested_error->code} : std::nullopt,
        std::nullopt,
        std::nullopt,
        driver_code);
    emit_trace(trace_for_terminal_state(state_));
}

void MovementEnvironmentStage::fail_after_retained_boundary(
    const MovementEnvironmentStageErrorCode code,
    const MovementEnvironmentStageState state,
    const std::string_view context,
    const MovementEnvironmentStageTimePoint now,
    const std::optional<MoveVarsStreamErrorCode> stream_code,
    const std::optional<MoveVarsErrorCode> parser_code) noexcept
{
    if (terminal_state(state_)) {
        return;
    }
    result_.reset();
    set_error(
        code,
        state,
        context,
        std::nullopt,
        stream_code,
        parser_code);
    delta_stage_.finalize_retained_boundary(now);
    emit_trace(trace_for_terminal_state(state_));
}

void MovementEnvironmentStage::set_error(
    const MovementEnvironmentStageErrorCode code,
    const MovementEnvironmentStageState state,
    const std::string_view context,
    const std::optional<DeltaDescriptionStageErrorCode> delta_code,
    const std::optional<MoveVarsStreamErrorCode> stream_code,
    const std::optional<MoveVarsErrorCode> parser_code,
    const std::optional<NetchanDriverErrorCode> driver_code) noexcept
{
    state_ = state;
    error_.reset();
    try {
        error_.emplace();
    } catch (...) {
        return;
    }
    error_->code = code;
    error_->delta_code = delta_code;
    error_->stream_code = stream_code;
    error_->parser_code = parser_code;
    error_->driver_code = driver_code;
    const auto bounded = context.substr(
        0U,
        (std::min)(context.size(),
                   kMovementEnvironmentStageDiagnosticTextLimit));
    try {
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
        error_->context.clear();
    }
}

void MovementEnvironmentStage::emit_trace(
    const MovementEnvironmentTraceClassification classification,
    const std::size_t control_index,
    const std::optional<std::uint8_t> opcode,
    const std::size_t byte_offset,
    const std::size_t byte_count,
    const std::size_t string_length,
    const MoveVarsState* const move_vars,
    const std::size_t control_count) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    MovementEnvironmentTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.control_index = control_index;
    event.opcode = opcode;
    event.byte_offset = byte_offset;
    event.byte_count = byte_count;
    event.string_length = string_length;
    event.control_count = control_count;
    if (move_vars != nullptr) {
        event.gravity = move_vars->gravity();
        event.maximum_speed = move_vars->maximum_speed();
        event.acceleration = move_vars->acceleration();
        event.air_acceleration = move_vars->air_acceleration();
        event.friction = move_vars->friction();
        event.step_size = move_vars->step_size();
        event.maximum_velocity = move_vars->maximum_velocity();
        event.footsteps = move_vars->footsteps();
        event.sky_name = move_vars->sky_name();
    }
    event.transmitted_packet_count = transmitted_packet_count();

    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

static_assert(std::is_nothrow_move_constructible_v<MovementEnvironmentStageEvent>);
static_assert(std::is_nothrow_move_constructible_v<MovementEnvironmentSignonState>);

} // namespace hlclient::goldsrc
