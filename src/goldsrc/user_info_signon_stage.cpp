#include <hlclient/goldsrc/user_info_signon_stage.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(const UserInfoSignonStageState state) noexcept
{
    switch (state) {
    case UserInfoSignonStageState::first_batch_complete:
    case UserInfoSignonStageState::unsupported_message:
    case UserInfoSignonStageState::timed_out:
    case UserInfoSignonStageState::cancelled:
    case UserInfoSignonStageState::backpressure:
    case UserInfoSignonStageState::secondary_stream_pending:
    case UserInfoSignonStageState::network_error:
    case UserInfoSignonStageState::protocol_error:
        return true;
    case UserInfoSignonStageState::idle:
    case UserInfoSignonStageState::waiting_for_movevars_state:
    case UserInfoSignonStageState::decoding_user_info:
    case UserInfoSignonStageState::user_info_ready:
        return false;
    }
    return true;
}

[[nodiscard]] UserInfoSignonTraceClassification terminal_trace(
    const UserInfoSignonStageState state) noexcept
{
    switch (state) {
    case UserInfoSignonStageState::first_batch_complete:
        return UserInfoSignonTraceClassification::first_batch_complete;
    case UserInfoSignonStageState::unsupported_message:
        return UserInfoSignonTraceClassification::unsupported_message;
    case UserInfoSignonStageState::timed_out:
        return UserInfoSignonTraceClassification::stage_timed_out;
    case UserInfoSignonStageState::cancelled:
        return UserInfoSignonTraceClassification::stage_cancelled;
    case UserInfoSignonStageState::backpressure:
        return UserInfoSignonTraceClassification::backpressure;
    case UserInfoSignonStageState::secondary_stream_pending:
        return UserInfoSignonTraceClassification::secondary_stream_pending;
    case UserInfoSignonStageState::network_error:
        return UserInfoSignonTraceClassification::network_failure;
    case UserInfoSignonStageState::idle:
    case UserInfoSignonStageState::waiting_for_movevars_state:
    case UserInfoSignonStageState::decoding_user_info:
    case UserInfoSignonStageState::user_info_ready:
    case UserInfoSignonStageState::protocol_error:
        return UserInfoSignonTraceClassification::protocol_failure;
    }
    return UserInfoSignonTraceClassification::protocol_failure;
}

[[nodiscard]] bool movement_network_error(
    const std::optional<NetchanDriverErrorCode> code) noexcept
{
    return code == NetchanDriverErrorCode::receive_failed ||
           code == NetchanDriverErrorCode::send_failed;
}

} // namespace

bool valid_user_info_signon_stage_configuration(
    const UserInfoSignonStageConfig& config) noexcept
{
    return valid_movement_environment_stage_configuration(
               config.movement_environment) &&
           valid_user_info_update_limits(config.user_info) &&
           config.maximum_stage_events > 0U &&
           config.maximum_stage_events <= kMaximumUserInfoStageEvents;
}

UserInfoSignonState::UserInfoSignonState(
    MovementEnvironmentSignonState movement_environment,
    UserInfoUpdateStreamState stream) noexcept
    : movement_environment_{std::move(movement_environment)},
      stream_{std::move(stream)}
{
}

const MovementEnvironmentSignonState&
UserInfoSignonState::movement_environment() const noexcept
{
    return movement_environment_;
}

const UserInfoUpdateStreamState& UserInfoSignonState::stream() const noexcept
{
    return stream_;
}

const std::vector<UserInfoUpdateState>&
UserInfoSignonState::messages() const noexcept
{
    return stream_.messages();
}

std::size_t UserInfoSignonState::message_count() const noexcept
{
    return stream_.message_count();
}

const UserInfoFirstBatchCompletion&
UserInfoSignonState::completion() const noexcept
{
    return stream_.completion();
}

UserInfoUpdateEvidenceProfile UserInfoSignonState::evidence_profile() const noexcept
{
    return UserInfoUpdateEvidenceProfile::stock_capture_and_public_valve_header;
}

const PreResourceSourcePayloadMetadata&
UserInfoSignonState::source_payload() const noexcept
{
    return movement_environment_.delta_description().pre_resource().source_payload();
}

UserInfoSignonStage::UserInfoSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    UserInfoSignonStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback trace_callback)
    : UserInfoSignonStage{
          transport,
          remote_endpoint,
          std::move(config),
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(trace_callback),
          false}
{
}

UserInfoSignonStage::UserInfoSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    UserInfoSignonStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback trace_callback,
    RetainConnectionAtBoundary)
    : UserInfoSignonStage{
          transport,
          remote_endpoint,
          std::move(config),
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(trace_callback),
          true}
{
}

UserInfoSignonStage::UserInfoSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    UserInfoSignonStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback trace_callback,
    const bool retain_connection_at_boundary)
    : config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{valid_user_info_signon_stage_configuration(config_)},
      retain_connection_at_boundary_{retain_connection_at_boundary},
      movement_stage_{
          transport,
          remote_endpoint,
          config_.movement_environment,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          MovementEnvironmentStage::RetainConnectionAtBoundary{}},
      event_slots_(configuration_valid_ ? config_.maximum_stage_events : 0U)
{
}

UserInfoSignonStage::~UserInfoSignonStage() = default;

bool UserInfoSignonStage::start(
    const UserInfoSignonStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != UserInfoSignonStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        set_error(
            UserInfoSignonStageErrorCode::invalid_configuration,
            UserInfoSignonStageState::protocol_error,
            "User-info sign-on configuration is outside project bounds");
        emit_trace(UserInfoSignonTraceClassification::protocol_failure);
        return false;
    }

    bool started = false;
    try {
        started = movement_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try {
            movement_stage_.cancel(now);
        } catch (...) {
        }
        movement_stage_.finalize_retained_boundary(now);
        set_error(
            UserInfoSignonStageErrorCode::movement_stage_start_failed,
            UserInfoSignonStageState::protocol_error,
            "Nested movement/environment stage threw during start");
        emit_trace(UserInfoSignonTraceClassification::protocol_failure);
        return false;
    }
    drain_movement_events();
    if (!started) {
        fail_from_movement();
        if (error_) {
            error_->code = UserInfoSignonStageErrorCode::movement_stage_start_failed;
        }
        return false;
    }
    state_ = UserInfoSignonStageState::waiting_for_movevars_state;
    emit_trace(UserInfoSignonTraceClassification::stage_started);
    return true;
}

void UserInfoSignonStage::update(const UserInfoSignonStageTimePoint now)
{
    if (trace_callback_active_ || state_ == UserInfoSignonStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (state_ != UserInfoSignonStageState::waiting_for_movevars_state) {
        return;
    }
    drain_movement_events();
    try {
        movement_stage_.update(now);
    } catch (...) {
        try {
            movement_stage_.cancel(now);
        } catch (...) {
        }
        movement_stage_.finalize_retained_boundary(now);
        set_error(
            UserInfoSignonStageErrorCode::movement_stage_failed,
            UserInfoSignonStageState::protocol_error,
            "Nested movement/environment stage threw during update");
        emit_trace(UserInfoSignonTraceClassification::protocol_failure);
        return;
    }
    drain_movement_events();
    synchronize_from_movement(now);
}

void UserInfoSignonStage::cancel(const UserInfoSignonStageTimePoint now)
{
    if (trace_callback_active_ || state_ == UserInfoSignonStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    try {
        movement_stage_.cancel(now);
    } catch (...) {
        movement_stage_.finalize_retained_boundary(now);
        set_error(
            UserInfoSignonStageErrorCode::movement_stage_failed,
            UserInfoSignonStageState::protocol_error,
            "Nested movement/environment stage threw during cancellation");
        emit_trace(UserInfoSignonTraceClassification::protocol_failure);
        return;
    }
    drain_movement_events();
    state_ = UserInfoSignonStageState::cancelled;
    emit_trace(UserInfoSignonTraceClassification::stage_cancelled);
}

std::optional<UserInfoSignonStageEvent> UserInfoSignonStage::poll_event()
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

UserInfoSignonStageState UserInfoSignonStage::state() const noexcept
{
    return state_;
}

bool UserInfoSignonStage::terminal() const noexcept
{
    return terminal_state(state_);
}

const std::optional<UserInfoSignonState>& UserInfoSignonStage::result() const noexcept
{
    return result_;
}

const std::optional<UserInfoSignonStageError>& UserInfoSignonStage::error() const noexcept
{
    return error_;
}

const network::NetworkAddress& UserInfoSignonStage::remote_endpoint() const noexcept
{
    return movement_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
UserInfoSignonStage::local_endpoint() const noexcept
{
    return movement_stage_.local_endpoint();
}

std::size_t UserInfoSignonStage::pending_event_count() const noexcept
{
    return event_size_;
}

std::size_t UserInfoSignonStage::transmitted_packet_count() const noexcept
{
    return movement_stage_.transmitted_packet_count();
}

std::size_t UserInfoSignonStage::cleanup_count() const noexcept
{
    return movement_stage_.cleanup_count();
}

std::size_t UserInfoSignonStage::request_queue_count() const noexcept
{
    return movement_stage_.request_queue_count();
}

NetchanDriver* UserInfoSignonStage::retained_driver() noexcept
{
    if (!retain_connection_at_boundary_) {
        return nullptr;
    }
    return movement_stage_.retained_driver();
}

void UserInfoSignonStage::finalize_retained_boundary(
    const UserInfoSignonStageTimePoint now) noexcept
{
    movement_stage_.finalize_retained_boundary(now);
}

bool UserInfoSignonStage::can_push_events(const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void UserInfoSignonStage::push_event(UserInfoSignonStageEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void UserInfoSignonStage::drain_movement_events() noexcept
{
    while (movement_stage_.poll_event()) {
    }
}

void UserInfoSignonStage::synchronize_from_movement(
    const UserInfoSignonStageTimePoint now)
{
    if (movement_stage_.state() ==
        MovementEnvironmentStageState::post_environment_boundary_reached) {
        emit_trace(UserInfoSignonTraceClassification::movevars_boundary_reached);
        decode_retained_user_info(now);
        return;
    }
    if (movement_stage_.terminal() || movement_stage_.error()) {
        fail_from_movement();
    }
}

void UserInfoSignonStage::decode_retained_user_info(
    const UserInfoSignonStageTimePoint now)
{
    const auto& movement_result = movement_stage_.result();
    const auto* const source_payload = movement_stage_.retained_source_payload();
    if (!movement_result || source_payload == nullptr) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::retained_payload_missing,
            UserInfoSignonStageState::protocol_error,
            "Movement continuation has no retained owning payload",
            now);
        return;
    }

    state_ = UserInfoSignonStageState::decoding_user_info;
    std::optional<UserInfoUpdateStreamDecodeResult> decoded;
    try {
        const UserInfoUpdateStreamDecoder decoder{config_.user_info};
        decoded.emplace(
            decoder.decode(source_payload->bytes, movement_result->boundary()));
    } catch (...) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::user_info_stream_decode_failed,
            UserInfoSignonStageState::protocol_error,
            "Bounded user-info continuation threw",
            now);
        return;
    }
    if (!*decoded || !decoded->state) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::user_info_stream_decode_failed,
            decoded->error &&
                    decoded->error->code ==
                        UserInfoUpdateStreamErrorCode::wrong_initial_opcode
                ? UserInfoSignonStageState::unsupported_message
                : UserInfoSignonStageState::protocol_error,
            decoded->error
                ? std::string_view{decoded->error->context}
                : std::string_view{"User-info stream returned no owning state"},
            now,
            decoded->error ? std::optional{decoded->error->code} : std::nullopt,
            decoded->error ? decoded->error->parser_code : std::nullopt);
        return;
    }

    const auto& stream_candidate = *decoded->state;
    if (stream_candidate.message_count() >
            (std::numeric_limits<std::size_t>::max)() - 1U ||
        decoded->required_event_count != stream_candidate.message_count() + 1U) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::user_info_stream_decode_failed,
            UserInfoSignonStageState::protocol_error,
            "User-info stream returned inconsistent event metadata",
            now);
        return;
    }
    if (stream_candidate.completion().terminal_condition() !=
            UserInfoBatchTerminalCondition::exact_end_of_payload ||
        stream_candidate.completion().remaining_byte_count() != 0U ||
        stream_candidate.completion().final_byte_offset() != source_payload->bytes.size()) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::first_batch_not_complete,
            UserInfoSignonStageState::unsupported_message,
            "Stock user-info profile requires exact end of first service batch",
            now);
        return;
    }
    if (decoded->required_event_count > config_.maximum_stage_events ||
        !can_push_events(decoded->required_event_count)) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::event_backpressure,
            UserInfoSignonStageState::backpressure,
            "User-info result exceeds bounded event capacity",
            now);
        return;
    }

    std::vector<UserInfoSignonStageEvent> candidate_events;
    std::optional<UserInfoSignonState> candidate_result;
    try {
        candidate_events.reserve(decoded->required_event_count);
        std::size_t message_index = 0U;
        for (const auto& message : stream_candidate.messages()) {
            candidate_events.push_back(UserInfoSignonStageEvent{
                UserInfoSignonStageEventType::user_info_message_decoded,
                message_index,
                message.source_message_offset(),
                message.message_bytes(),
                message.info_string_length(),
                message.info_entry_count(),
                message.player_name_length(),
                message.player_model_length(),
                now,
            });
            ++message_index;
        }
        const auto& completion = stream_candidate.completion();
        candidate_events.push_back(UserInfoSignonStageEvent{
            UserInfoSignonStageEventType::first_batch_complete,
            stream_candidate.message_count(),
            completion.final_byte_offset(),
            completion.bytes_consumed(),
            0U,
            0U,
            std::nullopt,
            std::nullopt,
            now,
        });
        auto built_result = UserInfoSignonState{
            *movement_result,
            std::move(*decoded->state)};
        candidate_result.emplace(std::move(built_result));
    } catch (...) {
        fail_after_retained_boundary(
            UserInfoSignonStageErrorCode::user_info_stream_decode_failed,
            UserInfoSignonStageState::protocol_error,
            "Unable to allocate bounded owning user-info result",
            now);
        return;
    }

    result_.emplace(std::move(*candidate_result));
    for (auto& event : candidate_events) {
        push_event(std::move(event));
    }
    state_ = UserInfoSignonStageState::user_info_ready;
    std::size_t message_index = 0U;
    for (const auto& message : result_->messages()) {
        emit_trace(
            UserInfoSignonTraceClassification::user_info_message_decoded,
            message_index,
            result_->message_count(),
            message.source_message_offset(),
            message.message_bytes(),
            message.info_string_length(),
            message.info_entry_count(),
            message.player_name_length(),
            message.player_model_length());
        ++message_index;
    }
    state_ = UserInfoSignonStageState::first_batch_complete;
    if (!retain_connection_at_boundary_) {
        movement_stage_.finalize_retained_boundary(now);
    }
    emit_trace(
        UserInfoSignonTraceClassification::first_batch_complete,
        result_->message_count(),
        result_->message_count(),
        result_->completion().final_byte_offset(),
        result_->completion().bytes_consumed());
}

void UserInfoSignonStage::fail_from_movement() noexcept
{
    const auto& nested_error = movement_stage_.error();
    const auto driver_code = nested_error ? nested_error->driver_code : std::nullopt;
    switch (movement_stage_.state()) {
    case MovementEnvironmentStageState::timed_out:
        state_ = UserInfoSignonStageState::timed_out;
        break;
    case MovementEnvironmentStageState::cancelled:
        state_ = UserInfoSignonStageState::cancelled;
        break;
    case MovementEnvironmentStageState::unsupported_message:
        state_ = UserInfoSignonStageState::unsupported_message;
        break;
    case MovementEnvironmentStageState::backpressure:
        state_ = UserInfoSignonStageState::backpressure;
        break;
    case MovementEnvironmentStageState::secondary_stream_pending_m3:
        state_ = UserInfoSignonStageState::secondary_stream_pending;
        break;
    case MovementEnvironmentStageState::network_error:
        state_ = UserInfoSignonStageState::network_error;
        break;
    case MovementEnvironmentStageState::idle:
        state_ = movement_network_error(driver_code)
            ? UserInfoSignonStageState::network_error
            : UserInfoSignonStageState::protocol_error;
        break;
    case MovementEnvironmentStageState::waiting_for_delta_state:
    case MovementEnvironmentStageState::decoding_move_vars:
    case MovementEnvironmentStageState::environment_state_ready:
    case MovementEnvironmentStageState::decoding_post_environment_messages:
    case MovementEnvironmentStageState::post_environment_boundary_reached:
    case MovementEnvironmentStageState::protocol_error:
        state_ = UserInfoSignonStageState::protocol_error;
        break;
    }
    set_error(
        UserInfoSignonStageErrorCode::movement_stage_failed,
        state_,
        nested_error
            ? std::string_view{nested_error->context}
            : std::string_view{"Nested movement/environment stage terminated without an error"},
        nested_error ? std::optional{nested_error->code} : std::nullopt,
        std::nullopt,
        std::nullopt,
        driver_code);
    emit_trace(terminal_trace(state_));
}

void UserInfoSignonStage::fail_after_retained_boundary(
    const UserInfoSignonStageErrorCode code,
    const UserInfoSignonStageState state,
    const std::string_view context,
    const UserInfoSignonStageTimePoint now,
    const std::optional<UserInfoUpdateStreamErrorCode> stream_code,
    const std::optional<UserInfoUpdateErrorCode> parser_code) noexcept
{
    if (terminal_state(state_)) {
        return;
    }
    result_.reset();
    set_error(code, state, context, std::nullopt, stream_code, parser_code);
    movement_stage_.finalize_retained_boundary(now);
    emit_trace(terminal_trace(state_));
}

void UserInfoSignonStage::set_error(
    const UserInfoSignonStageErrorCode code,
    const UserInfoSignonStageState state,
    const std::string_view context,
    const std::optional<MovementEnvironmentStageErrorCode> movement_code,
    const std::optional<UserInfoUpdateStreamErrorCode> stream_code,
    const std::optional<UserInfoUpdateErrorCode> parser_code,
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
    error_->movement_code = movement_code;
    error_->stream_code = stream_code;
    error_->parser_code = parser_code;
    error_->driver_code = driver_code;
    const auto bounded = context.substr(
        0U,
        (std::min)(context.size(), kUserInfoSignonStageDiagnosticTextLimit));
    try {
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
        error_->context.clear();
    }
}

void UserInfoSignonStage::emit_trace(
    const UserInfoSignonTraceClassification classification,
    const std::size_t message_index,
    const std::size_t message_count,
    const std::size_t byte_offset,
    const std::size_t byte_count,
    const std::size_t info_string_length,
    const std::size_t info_entry_count,
    const std::optional<std::size_t> player_name_length,
    const std::optional<std::size_t> player_model_length) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    UserInfoSignonTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.message_index = message_index;
    event.message_count = message_count;
    event.byte_offset = byte_offset;
    event.byte_count = byte_count;
    event.info_string_length = info_string_length;
    event.info_entry_count = info_entry_count;
    event.player_name_length = player_name_length;
    event.player_model_length = player_model_length;
    event.transmitted_packet_count = transmitted_packet_count();
    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

static_assert(std::is_nothrow_move_constructible_v<UserInfoSignonStageEvent>);
static_assert(std::is_nothrow_move_constructible_v<UserInfoSignonState>);

} // namespace hlclient::goldsrc
