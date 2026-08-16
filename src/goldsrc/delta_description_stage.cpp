#include <hlclient/goldsrc/delta_description_stage.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(const DeltaDescriptionStageState state) noexcept
{
    switch (state) {
    case DeltaDescriptionStageState::post_delta_boundary_reached:
    case DeltaDescriptionStageState::unsupported_message:
    case DeltaDescriptionStageState::timed_out:
    case DeltaDescriptionStageState::cancelled:
    case DeltaDescriptionStageState::backpressure:
    case DeltaDescriptionStageState::secondary_stream_pending_m3:
    case DeltaDescriptionStageState::network_error:
    case DeltaDescriptionStageState::protocol_error:
        return true;
    case DeltaDescriptionStageState::idle:
    case DeltaDescriptionStageState::waiting_for_pre_resource_state:
    case DeltaDescriptionStageState::decoding_delta_stream:
    case DeltaDescriptionStageState::delta_registry_ready:
        return false;
    }
    return true;
}

[[nodiscard]] DeltaDescriptionTraceClassification trace_for_terminal_state(
    const DeltaDescriptionStageState state) noexcept
{
    switch (state) {
    case DeltaDescriptionStageState::timed_out:
        return DeltaDescriptionTraceClassification::stage_timed_out;
    case DeltaDescriptionStageState::cancelled:
        return DeltaDescriptionTraceClassification::stage_cancelled;
    case DeltaDescriptionStageState::backpressure:
        return DeltaDescriptionTraceClassification::backpressure;
    case DeltaDescriptionStageState::secondary_stream_pending_m3:
        return DeltaDescriptionTraceClassification::secondary_stream_pending_m3;
    case DeltaDescriptionStageState::network_error:
        return DeltaDescriptionTraceClassification::network_failure;
    case DeltaDescriptionStageState::unsupported_message:
        return DeltaDescriptionTraceClassification::unsupported_message;
    case DeltaDescriptionStageState::post_delta_boundary_reached:
        return DeltaDescriptionTraceClassification::post_delta_boundary_reached;
    case DeltaDescriptionStageState::idle:
    case DeltaDescriptionStageState::waiting_for_pre_resource_state:
    case DeltaDescriptionStageState::decoding_delta_stream:
    case DeltaDescriptionStageState::delta_registry_ready:
    case DeltaDescriptionStageState::protocol_error:
        return DeltaDescriptionTraceClassification::protocol_failure;
    }
    return DeltaDescriptionTraceClassification::protocol_failure;
}

[[nodiscard]] bool pre_resource_network_error(
    const std::optional<NetchanDriverErrorCode> code) noexcept
{
    return code == NetchanDriverErrorCode::receive_failed ||
           code == NetchanDriverErrorCode::send_failed;
}

} // namespace

bool valid_delta_description_stage_configuration(
    const DeltaDescriptionStageConfig& config) noexcept
{
    return valid_pre_resource_signon_configuration(config.pre_resource) &&
           valid_delta_description_limits(config.delta) &&
           config.maximum_events > 0U &&
           config.maximum_events <= kMaximumDeltaDescriptionStageEvents;
}

DeltaDescriptionSignonState::DeltaDescriptionSignonState(
    PreResourceSignonState pre_resource,
    DeltaDescriptionStreamState delta_stream,
    const DeltaCompatibilityProfile profile) noexcept
    : pre_resource_{std::move(pre_resource)},
      delta_stream_{std::move(delta_stream)},
      profile_{profile}
{
}

const PreResourceSignonState& DeltaDescriptionSignonState::pre_resource() const noexcept
{
    return pre_resource_;
}

const DeltaSchemaRegistryState& DeltaDescriptionSignonState::registry() const noexcept
{
    return delta_stream_.registry;
}

const PostDeltaBoundary& DeltaDescriptionSignonState::boundary() const noexcept
{
    return delta_stream_.boundary;
}

std::size_t DeltaDescriptionSignonState::delta_message_count() const noexcept
{
    return delta_stream_.delta_message_count;
}

std::size_t DeltaDescriptionSignonState::bits_consumed() const noexcept
{
    return delta_stream_.bits_consumed;
}

std::size_t DeltaDescriptionSignonState::bytes_consumed() const noexcept
{
    return delta_stream_.bytes_consumed;
}

DeltaCompatibilityProfile DeltaDescriptionSignonState::profile() const noexcept
{
    return profile_;
}

DeltaDescriptionStage::DeltaDescriptionStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    DeltaDescriptionStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback trace_callback)
    : config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{valid_delta_description_stage_configuration(config_)},
      pre_resource_stage_{
          transport,
          remote_endpoint,
          config_.pre_resource,
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
          PreResourceSignonStage::RetainConnectionAtBoundary{}},
      event_slots_(configuration_valid_ ? config_.maximum_events : 0U)
{
}

DeltaDescriptionStage::~DeltaDescriptionStage() = default;

bool DeltaDescriptionStage::start(
    const DeltaDescriptionStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != DeltaDescriptionStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        set_error(
            DeltaDescriptionStageErrorCode::invalid_configuration,
            DeltaDescriptionStageState::protocol_error,
            "Delta-description stage configuration is outside project bounds");
        emit_trace(DeltaDescriptionTraceClassification::protocol_failure);
        return false;
    }

    bool started = false;
    try {
        started = pre_resource_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try {
            pre_resource_stage_.cancel(now);
        } catch (...) {
        }
        pre_resource_stage_.finalize_retained_boundary(now);
        set_error(
            DeltaDescriptionStageErrorCode::pre_resource_start_failed,
            DeltaDescriptionStageState::protocol_error,
            "Nested pre-resource stage threw during start");
        emit_trace(DeltaDescriptionTraceClassification::protocol_failure);
        return false;
    }
    drain_pre_resource_events();
    if (!started) {
        fail_from_pre_resource();
        if (error_) {
            error_->code = DeltaDescriptionStageErrorCode::pre_resource_start_failed;
        } else {
            set_error(
                DeltaDescriptionStageErrorCode::pre_resource_start_failed,
                DeltaDescriptionStageState::protocol_error,
                "Nested pre-resource stage rejected start");
            emit_trace(DeltaDescriptionTraceClassification::protocol_failure);
        }
        return false;
    }

    state_ = DeltaDescriptionStageState::waiting_for_pre_resource_state;
    emit_trace(DeltaDescriptionTraceClassification::stage_started);
    return true;
}

void DeltaDescriptionStage::update(const DeltaDescriptionStageTimePoint now)
{
    if (trace_callback_active_ || state_ == DeltaDescriptionStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (state_ != DeltaDescriptionStageState::waiting_for_pre_resource_state) {
        return;
    }

    drain_pre_resource_events();
    try {
        pre_resource_stage_.update(now);
    } catch (...) {
        try {
            pre_resource_stage_.cancel(now);
        } catch (...) {
        }
        pre_resource_stage_.finalize_retained_boundary(now);
        set_error(
            DeltaDescriptionStageErrorCode::pre_resource_failed,
            DeltaDescriptionStageState::protocol_error,
            "Nested pre-resource stage threw during update");
        emit_trace(DeltaDescriptionTraceClassification::protocol_failure);
        return;
    }
    drain_pre_resource_events();
    synchronize_from_pre_resource(now);
}

void DeltaDescriptionStage::cancel(const DeltaDescriptionStageTimePoint now)
{
    if (trace_callback_active_ || state_ == DeltaDescriptionStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    try {
        pre_resource_stage_.cancel(now);
    } catch (...) {
        pre_resource_stage_.finalize_retained_boundary(now);
        set_error(
            DeltaDescriptionStageErrorCode::pre_resource_failed,
            DeltaDescriptionStageState::protocol_error,
            "Nested pre-resource stage threw during cancellation");
        emit_trace(DeltaDescriptionTraceClassification::protocol_failure);
        return;
    }
    drain_pre_resource_events();
    state_ = DeltaDescriptionStageState::cancelled;
    emit_trace(DeltaDescriptionTraceClassification::stage_cancelled);
}

std::optional<DeltaDescriptionStageEvent> DeltaDescriptionStage::poll_event()
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

DeltaDescriptionStageState DeltaDescriptionStage::state() const noexcept { return state_; }
bool DeltaDescriptionStage::terminal() const noexcept { return terminal_state(state_); }
const std::optional<DeltaDescriptionSignonState>& DeltaDescriptionStage::result() const noexcept { return result_; }
const std::optional<DeltaDescriptionStageError>& DeltaDescriptionStage::error() const noexcept { return error_; }
const network::NetworkAddress& DeltaDescriptionStage::remote_endpoint() const noexcept { return pre_resource_stage_.remote_endpoint(); }
const std::optional<network::NetworkAddress>& DeltaDescriptionStage::local_endpoint() const noexcept { return pre_resource_stage_.local_endpoint(); }
std::size_t DeltaDescriptionStage::pending_event_count() const noexcept { return event_size_; }
std::size_t DeltaDescriptionStage::transmitted_packet_count() const noexcept { return pre_resource_stage_.transmitted_packet_count(); }
std::size_t DeltaDescriptionStage::cleanup_count() const noexcept { return pre_resource_stage_.cleanup_count(); }
std::size_t DeltaDescriptionStage::request_queue_count() const noexcept { return pre_resource_stage_.request_queue_count(); }

bool DeltaDescriptionStage::can_push_events(const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void DeltaDescriptionStage::push_event(DeltaDescriptionStageEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void DeltaDescriptionStage::drain_pre_resource_events() noexcept
{
    while (pre_resource_stage_.poll_event()) {
    }
}

void DeltaDescriptionStage::synchronize_from_pre_resource(
    const DeltaDescriptionStageTimePoint now)
{
    if (pre_resource_stage_.state() ==
        PreResourceSignonStageState::pre_resource_boundary_reached) {
        emit_trace(
            DeltaDescriptionTraceClassification::pre_resource_boundary_reached);
        decode_retained_delta_stream(now);
        return;
    }
    if (pre_resource_stage_.terminal() || pre_resource_stage_.error()) {
        fail_from_pre_resource();
    }
}

void DeltaDescriptionStage::decode_retained_delta_stream(
    const DeltaDescriptionStageTimePoint now)
{
    const auto& pre_resource_result = pre_resource_stage_.result();
    const auto* const source_payload = pre_resource_stage_.retained_source_payload();
    if (!pre_resource_result || source_payload == nullptr) {
        fail_after_retained_boundary(
            DeltaDescriptionStageErrorCode::retained_payload_missing,
            DeltaDescriptionStageState::protocol_error,
            "Pre-resource continuation has no retained owning payload",
            now);
        return;
    }

    state_ = DeltaDescriptionStageState::decoding_delta_stream;
    std::optional<DeltaDescriptionStreamDecodeResult> decoded;
    try {
        const DeltaDescriptionStreamDecoder decoder{config_.delta};
        decoded.emplace(decoder.decode(
            source_payload->bytes,
            pre_resource_result->boundary()));
    } catch (...) {
        fail_after_retained_boundary(
            DeltaDescriptionStageErrorCode::delta_stream_decode_failed,
            DeltaDescriptionStageState::protocol_error,
            "Bounded delta-description continuation threw",
            now);
        return;
    }

    if (!*decoded || !decoded->state) {
        fail_after_retained_boundary(
            DeltaDescriptionStageErrorCode::delta_stream_decode_failed,
            DeltaDescriptionStageState::protocol_error,
            decoded->error
                ? std::string_view{decoded->error->context}
                : std::string_view{"Delta stream returned no owning state"},
            now,
            decoded->error ? std::optional{decoded->error->code} : std::nullopt,
            decoded->error ? decoded->error->parser_code : std::nullopt,
            decoded->error ? decoded->error->registry_code : std::nullopt);
        return;
    }

    const auto& stream_candidate = *decoded->state;
    if (stream_candidate.delta_message_count >
            (std::numeric_limits<std::size_t>::max)() - 2U ||
        decoded->required_event_count !=
            stream_candidate.delta_message_count + 2U) {
        fail_after_retained_boundary(
            DeltaDescriptionStageErrorCode::delta_stream_decode_failed,
            DeltaDescriptionStageState::protocol_error,
            "Delta stream returned inconsistent event metadata",
            now);
        return;
    }
    if (decoded->required_event_count > config_.maximum_events ||
        !can_push_events(decoded->required_event_count)) {
        fail_after_retained_boundary(
            DeltaDescriptionStageErrorCode::event_backpressure,
            DeltaDescriptionStageState::backpressure,
            "Delta result exceeds bounded event capacity",
            now);
        return;
    }

    std::vector<DeltaDescriptionStageEvent> candidate_events;
    std::optional<DeltaDescriptionSignonState> candidate_result;
    try {
        candidate_events.reserve(decoded->required_event_count);
        std::size_t schema_index = 0U;
        for (const auto& schema : stream_candidate.registry.schemas()) {
            candidate_events.push_back(DeltaDescriptionStageEvent{
                DeltaDescriptionStageEventType::delta_schema_decoded,
                schema_index,
                std::string{schema.name()},
                schema.field_count(),
                schema.source_message_offset(),
                schema.message_bits(),
                schema.message_bytes(),
                std::nullopt,
                std::nullopt,
                now,
            });
            ++schema_index;
        }
        candidate_events.push_back(DeltaDescriptionStageEvent{
            DeltaDescriptionStageEventType::delta_registry_ready,
            stream_candidate.registry.schema_count(),
            {},
            stream_candidate.registry.total_field_count(),
            pre_resource_result->boundary().byte_offset(),
            stream_candidate.bits_consumed,
            stream_candidate.bytes_consumed,
            std::nullopt,
            std::nullopt,
            now,
        });
        candidate_events.push_back(DeltaDescriptionStageEvent{
            DeltaDescriptionStageEventType::post_delta_boundary,
            0U,
            {},
            0U,
            stream_candidate.boundary.byte_offset(),
            0U,
            0U,
            stream_candidate.boundary.opcode(),
            stream_candidate.boundary.category(),
            now,
        });
        auto built_result = DeltaDescriptionSignonState{
            *pre_resource_result,
            std::move(*decoded->state),
            DeltaCompatibilityProfile::stock_protocol_48_build_10210};
        candidate_result.emplace(std::move(built_result));
    } catch (...) {
        fail_after_retained_boundary(
            DeltaDescriptionStageErrorCode::delta_stream_decode_failed,
            DeltaDescriptionStageState::protocol_error,
            "Unable to allocate bounded owning delta result",
            now);
        return;
    }

    result_.emplace(std::move(*candidate_result));
    for (auto& event : candidate_events) {
        push_event(std::move(event));
    }
    state_ = DeltaDescriptionStageState::delta_registry_ready;
    for (std::size_t schema_index = 0U;
         schema_index < result_->registry().schemas().size();
         ++schema_index) {
        const auto& schema = result_->registry().schemas()[schema_index];
        emit_trace(
            DeltaDescriptionTraceClassification::delta_schema_decoded,
            schema_index,
            schema.name(),
            schema.field_count(),
            schema.source_message_offset(),
            schema.message_bits(),
            schema.message_bytes());
    }
    emit_trace(
        DeltaDescriptionTraceClassification::delta_registry_ready,
        result_->registry().schema_count(),
        {},
        result_->registry().total_field_count(),
        pre_resource_result->boundary().byte_offset(),
        result_->bits_consumed(),
        result_->bytes_consumed());

    const auto& boundary = result_->boundary();
    state_ = DeltaDescriptionStageState::post_delta_boundary_reached;
    pre_resource_stage_.finalize_retained_boundary(now);
    emit_trace(
        trace_for_terminal_state(state_),
        0U,
        {},
        0U,
        boundary.byte_offset(),
        0U,
        0U,
        boundary.opcode(),
        boundary.category());
}

void DeltaDescriptionStage::fail_from_pre_resource() noexcept
{
    const auto& nested_error = pre_resource_stage_.error();
    const auto driver_code = nested_error ? nested_error->driver_code : std::nullopt;
    switch (pre_resource_stage_.state()) {
    case PreResourceSignonStageState::timed_out:
        state_ = DeltaDescriptionStageState::timed_out;
        break;
    case PreResourceSignonStageState::cancelled:
        state_ = DeltaDescriptionStageState::cancelled;
        break;
    case PreResourceSignonStageState::network_error:
        state_ = DeltaDescriptionStageState::network_error;
        break;
    case PreResourceSignonStageState::unsupported_message:
        state_ = DeltaDescriptionStageState::unsupported_message;
        break;
    case PreResourceSignonStageState::backpressure:
        state_ = DeltaDescriptionStageState::backpressure;
        break;
    case PreResourceSignonStageState::secondary_stream_pending_m3:
        state_ = DeltaDescriptionStageState::secondary_stream_pending_m3;
        break;
    case PreResourceSignonStageState::idle:
        state_ = pre_resource_network_error(driver_code)
            ? DeltaDescriptionStageState::network_error
            : DeltaDescriptionStageState::protocol_error;
        break;
    case PreResourceSignonStageState::waiting_for_initial_boundary:
    case PreResourceSignonStageState::decoding_server_info:
    case PreResourceSignonStageState::server_info_ready:
    case PreResourceSignonStageState::decoding_pre_resource_messages:
    case PreResourceSignonStageState::pre_resource_boundary_reached:
    case PreResourceSignonStageState::protocol_error:
        state_ = DeltaDescriptionStageState::protocol_error;
        break;
    }

    set_error(
        DeltaDescriptionStageErrorCode::pre_resource_failed,
        state_,
        nested_error
            ? std::string_view{nested_error->context}
            : std::string_view{"Nested pre-resource stage terminated without an error"},
        nested_error ? std::optional{nested_error->code} : std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        driver_code);
    emit_trace(trace_for_terminal_state(state_));
}

void DeltaDescriptionStage::fail_after_retained_boundary(
    const DeltaDescriptionStageErrorCode code,
    const DeltaDescriptionStageState state,
    const std::string_view context,
    const DeltaDescriptionStageTimePoint now,
    const std::optional<DeltaDescriptionStreamErrorCode> stream_code,
    const std::optional<DeltaDescriptionErrorCode> parser_code,
    const std::optional<DeltaRegistryErrorCode> registry_code) noexcept
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
        parser_code,
        registry_code);
    pre_resource_stage_.finalize_retained_boundary(now);
    emit_trace(trace_for_terminal_state(state_));
}

void DeltaDescriptionStage::set_error(
    const DeltaDescriptionStageErrorCode code,
    const DeltaDescriptionStageState state,
    const std::string_view context,
    const std::optional<PreResourceSignonErrorCode> pre_resource_code,
    const std::optional<DeltaDescriptionStreamErrorCode> stream_code,
    const std::optional<DeltaDescriptionErrorCode> parser_code,
    const std::optional<DeltaRegistryErrorCode> registry_code,
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
    error_->pre_resource_code = pre_resource_code;
    error_->stream_code = stream_code;
    error_->parser_code = parser_code;
    error_->registry_code = registry_code;
    error_->driver_code = driver_code;
    const auto bounded = context.substr(
        0U,
        (std::min)(context.size(), kDeltaDescriptionStageDiagnosticTextLimit));
    try {
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
        error_->context.clear();
    }
}

void DeltaDescriptionStage::emit_trace(
    const DeltaDescriptionTraceClassification classification,
    const std::size_t schema_index,
    const std::string_view schema_name,
    const std::size_t field_count,
    const std::size_t byte_offset,
    const std::size_t bits_consumed,
    const std::size_t bytes_consumed,
    const std::optional<std::uint8_t> boundary_opcode,
    const std::optional<PostDeltaBoundaryCategory> boundary_category) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    DeltaDescriptionTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.schema_index = schema_index;
    event.schema_name = schema_name;
    event.schema_name_length = schema_name.size();
    event.field_count = field_count;
    event.byte_offset = byte_offset;
    event.bits_consumed = bits_consumed;
    event.bytes_consumed = bytes_consumed;
    event.boundary_opcode = boundary_opcode;
    event.boundary_category = boundary_category;
    event.transmitted_packet_count = transmitted_packet_count();

    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

static_assert(std::is_nothrow_move_constructible_v<DeltaDescriptionStageEvent>);
static_assert(std::is_nothrow_move_constructible_v<DeltaDescriptionSignonState>);

} // namespace hlclient::goldsrc
