#pragma once

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/resource_client_response_stage.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/runtime_control_decoder.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/goldsrc/service_payload_envelope.hpp>
#include <hlclient/goldsrc/stock_spawn_request.hpp>
#include <hlclient/goldsrc/usercmd_scheduler.hpp>
#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using LiveRuntimeStageClock = ResourceClientResponseStageClock;
using LiveRuntimeStageTimePoint = ResourceClientResponseStageTimePoint;

inline constexpr auto kDefaultLiveRuntimeStableInterval =
    std::chrono::seconds{2};
inline constexpr auto kMaximumLiveRuntimeStableInterval =
    std::chrono::seconds{15};
inline constexpr auto kDefaultLiveRuntimeTimeout = std::chrono::seconds{20};
inline constexpr auto kMaximumLiveRuntimeTimeout = std::chrono::seconds{60};
inline constexpr std::size_t kDefaultMaximumLiveRuntimeDriverEventsPerUpdate =
    32U;
inline constexpr std::size_t kMaximumLiveRuntimeDriverEventsPerUpdate = 256U;
inline constexpr std::size_t kDefaultMaximumLiveRuntimeEvents = 128U;
inline constexpr std::size_t kMaximumLiveRuntimeEvents = 1'024U;

enum class LiveRuntimeOperationMode : std::uint8_t {
    runtime_state,
    live_usercmd_check,
    live_visual_control,
};

enum class LiveVisualControlInputSource : std::uint8_t {
    scripted_check,
    scripted_side_check,
    scripted_jump_duck_check,
    scripted_speed_check,
    keyboard_mouse,
};

[[nodiscard]] constexpr bool bounded_live_visual_scenario(
    const LiveVisualControlInputSource source) noexcept {
    return source == LiveVisualControlInputSource::scripted_check ||
           source == LiveVisualControlInputSource::scripted_side_check ||
           source == LiveVisualControlInputSource::scripted_jump_duck_check ||
           source == LiveVisualControlInputSource::scripted_speed_check;
}

// Pinned Valve cl_dll/input.cpp defaults, for live manual input only.
inline constexpr GoldSrcUserCmdMovementSpeedConfig kLiveReferenceManualSpeeds{
    400.0F, 400.0F, 400.0F};
inline constexpr float kLiveReferenceSpeedKeyMultiplier = 0.3F;

struct LiveVisualControlInput final {
    std::uint64_t generation{0U};
    std::uint64_t input_revision{0U};
    float forward_axis{0.0F};
    float side_axis{0.0F};
    double yaw_degrees{0.0};
    double pitch_degrees{0.0};
    bool focused{false};
    bool captured{false};
    bool a_held{false};
    bool d_held{false};
    gameplay_input::GameplayButtonMask held_buttons{0U};
    gameplay_input::GameplayButtonMask pressed_buttons{0U};
};

// Bounded project-action edge latch. The owning stage adds presses only after
// activation and consumes them only after the matching command enters history.
class LiveVisualButtonLatch final {
public:
    void observe(gameplay_input::GameplayButtonMask pressed,
                 bool focused) noexcept {
        if (!focused) { clear(); return; }
        pending_ |= pressed & allowed();
    }
    void consume_after_history_insert(
        gameplay_input::GameplayButtonMask consumed) noexcept {
        pending_ &= ~consumed;
    }
    void clear() noexcept { pending_ = 0U; }
    [[nodiscard]] gameplay_input::GameplayButtonMask pending() const noexcept {
        return pending_;
    }
    [[nodiscard]] static constexpr gameplay_input::GameplayButtonMask
    allowed() noexcept {
        return gameplay_input::gameplay_button_mask(
                   gameplay_input::GameplayButton::jump) |
               gameplay_input::gameplay_button_mask(
                   gameplay_input::GameplayButton::duck);
    }
private:
    gameplay_input::GameplayButtonMask pending_{0U};
};

// The scripted forward/backward check deliberately has no side speed. The
// keyboard source retains the signed project side axis through wire sampling.
[[nodiscard]] GoldSrcUserCmdMovementSpeedConfig live_visual_movement_speeds(
    bool keyboard_mouse, float forward_axis, float side_axis,
    float amplitude) noexcept;

enum class LiveUserCmdInputPhase : std::uint8_t {
    neutral_before,
    forward,
    neutral_middle,
    backward,
    neutral_tail,
};

enum class LiveUserCmdMotionOutcome : std::uint8_t {
    not_evaluated,
    verified,
    server_motion_unverified,
    observation_context_blocked,
};

inline constexpr std::size_t kLiveUserCmdPhaseCount = 5U;
// The keyboard route permits a 300-second session. At its 20 ms command
// interval it can submit 15,000 packets; fresh server observations may arrive
// more often. These are bounded diagnostic capacities, allocated as needed.
inline constexpr std::size_t kMaximumLiveUserCmdSamples = 32'768U;
inline constexpr std::size_t kMaximumLiveUserCmdTransmitRanges = 16'384U;

struct LiveUserCmdScenarioConfig final {
    std::chrono::milliseconds command_interval{20};
    std::array<std::chrono::milliseconds, kLiveUserCmdPhaseCount> durations{
        std::chrono::seconds{2}, std::chrono::seconds{1},
        std::chrono::seconds{1}, std::chrono::seconds{1},
        std::chrono::seconds{2}};
    std::int16_t forward_amplitude{100};
    double fixed_yaw_degrees{0.0};
    double fixed_pitch_degrees{0.0};
    double velocity_tolerance{1.0};
    double origin_tolerance{0.125};
    std::size_t maximum_samples{kMaximumLiveUserCmdSamples};
    std::size_t maximum_transmit_ranges{kMaximumLiveUserCmdTransmitRanges};
};

[[nodiscard]] ResourceClientResponseStageConfig
default_live_runtime_resource_response_stage_config();

struct LiveRuntimeStageConfig final {
    ResourceClientResponseStageConfig resource_response{
        default_live_runtime_resource_response_stage_config()};
    ServicePayloadEnvelopeLimits envelope{
        kDefaultMaximumDecompressedServicePayloadSize,
        ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed};
    EntityBaselineDecodeLimits baselines{};
    RuntimeReplayLimits runtime{};
    std::chrono::milliseconds stable_interval{
        kDefaultLiveRuntimeStableInterval};
    std::chrono::milliseconds timeout{kDefaultLiveRuntimeTimeout};
    std::size_t maximum_driver_events_per_update{
        kDefaultMaximumLiveRuntimeDriverEventsPerUpdate};
    std::size_t maximum_events{kDefaultMaximumLiveRuntimeEvents};
    LiveRuntimeOperationMode operation_mode{
        LiveRuntimeOperationMode::runtime_state};
    LiveVisualControlInputSource live_visual_input_source{
        LiveVisualControlInputSource::scripted_check};
    LiveUserCmdScenarioConfig usercmd_scenario{};
    bool reference_prediction{false};
};

enum class LiveReferencePredictionState : std::uint8_t {
    off, waiting_for_seed, active, suspended, failed,
};

[[nodiscard]] constexpr std::string_view to_string(
    const LiveReferencePredictionState state) noexcept {
    switch (state) {
    case LiveReferencePredictionState::off: return "off";
    case LiveReferencePredictionState::waiting_for_seed: return "waiting_for_seed";
    case LiveReferencePredictionState::active: return "active";
    case LiveReferencePredictionState::suspended: return "suspended";
    case LiveReferencePredictionState::failed: return "failed";
    }
    return "unknown";
}

struct LiveReferencePredictionSnapshot final {
    LiveReferencePredictionState state{LiveReferencePredictionState::off};
    std::string_view reason{"off"};
    std::uint64_t generation{0U};
    std::uint64_t prediction_epoch{0U};
    std::size_t local_steps{0U};
    std::size_t accepted_corrections{0U};
    std::size_t replayed_commands{0U};
    std::size_t fallback_count{0U};
    std::size_t history_depth{0U};
    std::optional<double> last_raw_position_error;
    std::optional<double> maximum_raw_position_error;
    std::optional<assets::AssetVector3> canonical_origin;
    std::optional<assets::AssetVector3> predicted_origin;
    std::optional<assets::AssetVector3> presented_origin;
    std::optional<assets::AssetVector3> presented_view_offset;
    std::optional<std::uint64_t> last_record_identity;
    std::optional<std::uint32_t> anchor_command;
    std::optional<ReferencePredictionSeedStatus> last_seed_status;
    std::optional<ReferencePredictionSeedField> last_seed_field;
    std::optional<ReferencePredictionGroundStatus> last_ground_status;
    std::optional<movement::LocalMovementCollisionSessionIdentity>
        collision_identity;
};

// Re-applies the explicit live-session compatibility profile after a caller
// supplies or replaces nested stage configuration. Historical standalone
// stages keep their strict defaults; only a live-runtime composition calls
// this selector.
void apply_live_runtime_compatibility_profile(
    LiveRuntimeStageConfig& config) noexcept;

[[nodiscard]] bool valid_live_runtime_stage_configuration(
    const LiveRuntimeStageConfig& config) noexcept;

enum class LiveRuntimeStageState : std::uint8_t {
    idle,
    waiting_for_resource_response,
    waiting_for_baselines,
    receiving_runtime,
    waiting_for_live_visual_input,
    stable_runtime_state_ready,
    running_usercmd_scenario,
    running_live_visual_control,
    live_usercmd_check_ready,
    live_visual_control_ready,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class LiveRuntimeStageErrorCode : std::uint8_t {
    invalid_configuration,
    response_stage_start_failed,
    response_stage_failed,
    retained_driver_missing,
    retained_payload_missing,
    initialization_context_missing,
    spawn_request_build_failed,
    spawn_request_queue_failed,
    spawn_request_transmit_mismatch,
    signon_reply_queue_failed,
    signon_reply_transmit_mismatch,
    envelope_decode_failed,
    unsupported_initialization_message,
    baseline_decode_failed,
    runtime_session_initialize_failed,
    runtime_record_failed,
    usercmd_schema_binding_failed,
    usercmd_scheduler_failed,
    usercmd_adapter_failed,
    usercmd_transmission_failed,
    usercmd_observation_failed,
    stage_timed_out,
    event_backpressure,
    driver_failed,
    time_moved_backwards,
};

struct LiveRuntimeStageError final {
    LiveRuntimeStageErrorCode code{
        LiveRuntimeStageErrorCode::invalid_configuration};
    std::optional<ResourceClientResponseStageErrorCode> response_code;
    std::optional<ResourceListStageErrorCode> resource_list_code;
    std::optional<ResourceTransitionStageErrorCode> transition_stage_code;
    std::optional<ResourceTransitionControlErrorCode> transition_control_code;
    std::optional<ResourceTransitionFailureMetadata>
        transition_failure_metadata;
    std::optional<ResourceResponsePayloadDiagnostic>
        response_payload_diagnostic;
    std::optional<StockSpawnRequestErrorCode> spawn_request_code;
    std::optional<ServicePayloadEnvelopeErrorCode> envelope_code;
    std::optional<RuntimeControlDecodeErrorCode> control_code;
    std::optional<EntityBaselineDecodeErrorCode> baseline_code;
    std::optional<RuntimeReplayErrorCode> runtime_code;
    std::optional<GoldSrcUserCmdSchemaBindingErrorCode> usercmd_schema_code;
    std::optional<GoldSrcUserCmdSchedulerErrorCode> usercmd_scheduler_code;
    std::optional<GoldSrcUserCmdInputAdapterErrorCode> usercmd_adapter_code;
    std::optional<GoldSrcUserCmdTransmissionErrorCode>
        usercmd_transmission_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::optional<std::uint8_t> wire_opcode;
    std::string context;
};

struct LiveUserCmdServerSample final {
    std::uint64_t generation{0U};
    std::uint64_t publication_revision{0U};
    client::RuntimeObservationSource source{};
    std::optional<double> server_time_seconds;
    std::optional<double> health;
    LiveUserCmdInputPhase phase{LiveUserCmdInputPhase::neutral_before};
    client::RuntimeVector3Observation origin;
    client::RuntimeVector3Observation velocity;
    client::RuntimeVector3Observation view_offset;
    client::RuntimeObservationFreshness freshness{
        client::RuntimeObservationFreshness::unavailable};
};

enum class LiveJumpDuckOutcome : std::uint8_t {
    not_evaluated,
    verified,
    jump_verified_duck_pending,
    buttons_transmitted_server_effect_unverified,
    observation_context_blocked,
    environment_limited,
};

[[nodiscard]] std::string_view to_string(LiveJumpDuckOutcome) noexcept;

struct LiveJumpDuckEvaluation final {
    LiveJumpDuckOutcome outcome{LiveJumpDuckOutcome::not_evaluated};
    bool jump_observed{false};
    bool descent_observed{false};
    bool duck_observed{false};
    bool release_response_observed{false};
};

[[nodiscard]] LiveJumpDuckEvaluation evaluate_live_jump_duck(
    std::span<const LiveUserCmdServerSample> samples,
    const std::array<std::size_t, kLiveUserCmdPhaseCount>& sent_by_phase,
    double origin_tolerance = 1.0,
    double velocity_tolerance = 1.0,
    double view_offset_tolerance = 1.0) noexcept;

struct LiveUserCmdTransmitRange final {
    std::uint32_t outgoing_netchan_sequence{0U};
    std::uint32_t first_new_command_sequence{0U};
    std::uint32_t last_new_command_sequence{0U};
    std::size_t new_command_count{0U};
    std::size_t backup_command_count{0U};
};

struct LiveUserCmdCheckState final {
    std::uint64_t generation{0U};
    bool production_handoff_complete{false};
    bool same_driver_retained{false};
    bool schema_binding_current{false};
    bool sendents_complete{false};
    bool terminal_rejection_observed{false};
    std::chrono::milliseconds command_interval{};
    std::array<std::chrono::milliseconds, kLiveUserCmdPhaseCount> durations{};
    std::int16_t forward_amplitude{0};
    double fixed_yaw_degrees{0.0};
    double fixed_pitch_degrees{0.0};
    double velocity_tolerance{0.0};
    double origin_tolerance{0.0};
    std::size_t generated_command_count{0U};
    std::size_t history_command_count{0U};
    std::size_t new_command_submission_count{0U};
    std::size_t backup_command_submission_count{0U};
    std::size_t transmitted_packet_count{0U};
    NetchanDriverReceiveStatistics driver_rx_total{};
    NetchanDriverReceiveStatistics driver_rx_at_input_activation{};
    std::size_t payload_events_consumed_total{0U};
    std::size_t payload_events_consumed_at_input_activation{0U};
    std::size_t service_envelopes_decoded_total{0U};
    std::size_t service_envelopes_decoded_at_input_activation{0U};
    std::size_t runtime_records_attempted_total{0U};
    std::size_t runtime_records_attempted_at_input_activation{0U};
    std::size_t runtime_records_committed_total{0U};
    std::size_t runtime_records_committed_at_input_activation{0U};
    std::size_t clientdata_records_committed_total{0U};
    std::size_t clientdata_records_committed_at_input_activation{0U};
    // Passive H2 readiness diagnostics. These never enable prediction or
    // change the canonical state/camera in the existing live mode.
    std::size_t reference_prediction_sent_bindings{0U};
    std::size_t reference_prediction_anchor_bindings{0U};
    std::size_t reference_prediction_seed_candidates{0U};
    std::size_t reference_prediction_command_candidates{0U};
    std::optional<GoldSrcUserCmdErrorCode>
        reference_prediction_last_command_error;
    std::optional<ReferencePredictionAnchorStatus>
        reference_prediction_last_anchor_status;
    std::optional<ReferencePredictionSeedStatus>
        reference_prediction_last_seed_status;
    std::optional<ReferencePredictionSeedField>
        reference_prediction_last_seed_field;
    std::optional<std::uint32_t> reference_prediction_last_command_boundary;
    float last_sampled_forward_axis{0.0F};
    float last_sampled_side_axis{0.0F};
    std::size_t nonzero_side_generated_count{0U};
    std::size_t nonzero_side_new_submission_count{0U};
    std::size_t jump_generated_count{0U};
    std::size_t duck_generated_count{0U};
    std::size_t jump_new_submission_count{0U};
    std::size_t duck_new_submission_count{0U};
    std::optional<std::uint32_t> first_jump_sent_sequence;
    std::optional<std::uint32_t> last_jump_sent_sequence;
    std::optional<std::uint32_t> first_duck_sent_sequence;
    std::optional<std::uint32_t> last_duck_sent_sequence;
    std::size_t jump_command_press_count{0U};
    std::size_t jump_command_release_count{0U};
    std::size_t duck_command_press_count{0U};
    std::size_t duck_command_release_count{0U};
    bool last_a_held{false};
    bool last_d_held{false};
    bool scheduler_initialized{false};
    bool scheduler_active{false};
    std::int64_t scheduler_activation_time_nanoseconds{0};
    std::int64_t scheduler_last_update_time_nanoseconds{0};
    std::int64_t scheduler_next_sample_time_nanoseconds{0};
    std::size_t scheduler_last_due_command_count{0U};
    std::size_t scheduler_maximum_commands_per_update{0U};
    std::array<std::size_t, kLiveUserCmdPhaseCount> generated_by_phase{};
    std::array<std::size_t, kLiveUserCmdPhaseCount> sent_by_phase{};
    std::array<std::size_t, kLiveUserCmdPhaseCount> fresh_samples_by_phase{};
    std::array<std::optional<float>, kLiveUserCmdPhaseCount> requested_forward_by_phase{};
    std::array<std::optional<std::int16_t>, kLiveUserCmdPhaseCount> encoded_forward_by_phase{};
    std::array<std::optional<std::int16_t>, kLiveUserCmdPhaseCount> encoded_side_by_phase{};
    std::array<std::optional<float>, kLiveUserCmdPhaseCount> speed_multiplier_by_phase{};
    std::optional<float> client_maxspeed;
    std::optional<float> movevars_maximum_speed;
    std::vector<LiveUserCmdTransmitRange> transmit_ranges;
    std::vector<LiveUserCmdServerSample> server_samples;
    LiveUserCmdMotionOutcome motion_outcome{
        LiveUserCmdMotionOutcome::not_evaluated};
    bool movement_verified{false};
    LiveJumpDuckEvaluation jump_duck{};
    LiveUserCmdMotionOutcome speed_outcome{LiveUserCmdMotionOutcome::not_evaluated};
};

[[nodiscard]] LiveUserCmdMotionOutcome evaluate_live_speed_check(
    std::span<const LiveUserCmdServerSample> samples,
    const std::array<std::size_t, kLiveUserCmdPhaseCount>& sent_by_phase,
    double fixed_yaw_degrees, double origin_tolerance = 0.125,
    double velocity_tolerance = 1.0) noexcept;

// Pure, bounded receiving-client semantic check used by both the live stage
// and fake-server integration fixtures. Vertical-only changes cannot satisfy
// this horizontal input-response contract.
[[nodiscard]] LiveUserCmdMotionOutcome evaluate_live_usercmd_motion(
    std::span<const LiveUserCmdServerSample> samples,
    const std::array<std::size_t, kLiveUserCmdPhaseCount>& sent_by_phase,
    double fixed_yaw_degrees,
    double velocity_tolerance,
    double origin_tolerance) noexcept;

// Bounded, byte-free publication for the selected live stop. The application
// owns the ClientWorldState into which the existing runtime session committed;
// this result only records the verified live-session geometry and final state.
struct LiveRuntimeState final {
    std::uint64_t generation{0U};
    std::uint32_t protocol{0U};
    std::uint32_t server_count{0U};
    std::uint8_t client_slot{0U};
    std::uint32_t maximum_clients{0U};
    std::string game_directory;
    std::string map_file_path;
    std::size_t schema_count{0U};
    std::size_t schema_field_count{0U};
    std::size_t baseline_entity_count{0U};
    std::size_t baseline_instanced_count{0U};
    std::size_t received_service_payload_count{0U};
    std::size_t applied_runtime_record_count{0U};
    std::size_t clientdata_record_count{0U};
    std::size_t entity_record_count{0U};
    std::size_t compressed_service_payload_count{0U};
    std::size_t wire_uncompressed_service_payload_count{0U};
    std::uint64_t publication_revision{0U};
    std::uint64_t canonical_state_hash{0U};
    std::size_t entity_count{0U};
    std::size_t spawn_request_queue_count{0U};
    bool spawn_request_transmitted{false};
    bool spawn_request_acknowledged{false};
    std::size_t signon_reply_queue_count{0U};
    bool signon_reply_transmitted{false};
    bool signon_reply_acknowledged{false};
    bool server_time_observed{false};
    bool clientdata_observed{false};
    bool entities_observed{false};
    bool initial_service_wire_uncompressed{false};
    bool transition_service_wire_uncompressed{false};
    bool stable_progress_observed{false};
    std::chrono::milliseconds stable_interval{};
    std::optional<LiveUserCmdCheckState> usercmd_check;
};

enum class LiveRuntimeStageEventType : std::uint8_t {
    resource_response_ready,
    spawn_request_queued,
    spawn_request_transmitted,
    spawn_request_acknowledged,
    signon_reply_queued,
    signon_reply_transmitted,
    signon_reply_acknowledged,
    service_payload_received,
    baseline_registry_ready,
    runtime_record_applied,
    stability_interval_started,
    stable_runtime_state_ready,
    live_visual_input_ready,
    usercmd_scenario_activated,
    usercmd_packet_submitted,
    usercmd_server_sample,
    live_usercmd_check_ready,
    live_visual_control_ready,
    timeout,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

struct LiveRuntimeStageEvent final {
    LiveRuntimeStageEventType type{LiveRuntimeStageEventType::protocol_error};
    std::size_t payload_ordinal{0U};
    std::optional<std::uint32_t> source_sequence;
    std::size_t payload_byte_count{0U};
    std::size_t baseline_count{0U};
    std::size_t entity_count{0U};
    std::uint64_t publication_revision{0U};
    bool decompressed{false};
    bool wire_uncompressed{false};
    std::optional<LiveUserCmdInputPhase> input_phase;
    LiveRuntimeStageTimePoint occurred_at{};
};

struct LiveRuntimeStageTraceEvent final {
    LiveRuntimeStageState state{LiveRuntimeStageState::idle};
    network::NetworkAddress endpoint;
    LiveRuntimeStageEvent metadata;
    std::size_t transmitted_packet_count{0U};
};

using LiveRuntimeStageTraceCallback =
    std::function<void(const LiveRuntimeStageTraceEvent&)>;

class LiveRuntimeStage final {
public:
    LiveRuntimeStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        client::ClientWorldState& target,
        LiveRuntimeStageConfig config = {},
        resource_consistency::IResourceConsistencyProvider*
            consistency_provider = nullptr,
        LiveRuntimeStageTraceCallback trace_callback = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback movement_trace_callback = {},
        UserInfoSignonTraceCallback user_info_trace_callback = {},
        ResourceTransitionTraceCallback transition_trace_callback = {},
        ResourceListTraceCallback resource_list_trace_callback = {},
        ResourceClientResponseTraceCallback response_trace_callback = {});
    ~LiveRuntimeStage();

    LiveRuntimeStage(const LiveRuntimeStage&) = delete;
    LiveRuntimeStage& operator=(const LiveRuntimeStage&) = delete;
    LiveRuntimeStage(LiveRuntimeStage&&) = delete;
    LiveRuntimeStage& operator=(LiveRuntimeStage&&) = delete;

    [[nodiscard]] bool start(
        LiveRuntimeStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(LiveRuntimeStageTimePoint now);
    void cancel(LiveRuntimeStageTimePoint now);
    [[nodiscard]] bool submit_live_visual_input(
        const LiveVisualControlInput& input,
        LiveRuntimeStageTimePoint now) noexcept;
    [[nodiscard]] bool activate_live_visual_control(
        LiveRuntimeStageTimePoint now) noexcept;

    [[nodiscard]] std::optional<LiveRuntimeStageEvent> poll_event();
    [[nodiscard]] LiveRuntimeStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<LiveRuntimeState>& result() const noexcept;
    [[nodiscard]] const std::optional<LiveRuntimeStageError>& error()
        const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint()
        const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint()
        const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t usercmd_transmit_count() const noexcept;
    [[nodiscard]] bool live_visual_input_ready() const noexcept;
    // Borrowed committed sign-on metadata owned by this same live stage. These
    // pointers never expose the driver and are null until the nested resource
    // response has completed.
    [[nodiscard]] const ResourceListState* live_resource_list() const noexcept;
    [[nodiscard]] const ServerInfoState* live_server_info() const noexcept;
    [[nodiscard]] std::optional<LiveUserCmdCheckState>
    live_usercmd_snapshot() const;
    [[nodiscard]] bool attach_reference_prediction_collision(
        std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package);
    [[nodiscard]] LiveReferencePredictionSnapshot
    live_reference_prediction_snapshot(LiveRuntimeStageTimePoint now) const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const LiveUserCmdInputPhase phase) noexcept
{
    switch (phase) {
    case LiveUserCmdInputPhase::neutral_before: return "neutral_before";
    case LiveUserCmdInputPhase::forward: return "forward";
    case LiveUserCmdInputPhase::neutral_middle: return "neutral_middle";
    case LiveUserCmdInputPhase::backward: return "backward";
    case LiveUserCmdInputPhase::neutral_tail: return "neutral_tail";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LiveUserCmdMotionOutcome outcome) noexcept
{
    switch (outcome) {
    case LiveUserCmdMotionOutcome::not_evaluated: return "not_evaluated";
    case LiveUserCmdMotionOutcome::verified: return "verified";
    case LiveUserCmdMotionOutcome::server_motion_unverified:
        return "server_motion_unverified";
    case LiveUserCmdMotionOutcome::observation_context_blocked:
        return "observation_context_blocked";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LiveRuntimeStageErrorCode code) noexcept
{
    switch (code) {
    case LiveRuntimeStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LiveRuntimeStageErrorCode::response_stage_start_failed:
        return "response_stage_start_failed";
    case LiveRuntimeStageErrorCode::response_stage_failed:
        return "response_stage_failed";
    case LiveRuntimeStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case LiveRuntimeStageErrorCode::retained_payload_missing:
        return "retained_payload_missing";
    case LiveRuntimeStageErrorCode::initialization_context_missing:
        return "initialization_context_missing";
    case LiveRuntimeStageErrorCode::spawn_request_build_failed:
        return "spawn_request_build_failed";
    case LiveRuntimeStageErrorCode::spawn_request_queue_failed:
        return "spawn_request_queue_failed";
    case LiveRuntimeStageErrorCode::spawn_request_transmit_mismatch:
        return "spawn_request_transmit_mismatch";
    case LiveRuntimeStageErrorCode::signon_reply_queue_failed:
        return "signon_reply_queue_failed";
    case LiveRuntimeStageErrorCode::signon_reply_transmit_mismatch:
        return "signon_reply_transmit_mismatch";
    case LiveRuntimeStageErrorCode::envelope_decode_failed:
        return "envelope_decode_failed";
    case LiveRuntimeStageErrorCode::unsupported_initialization_message:
        return "unsupported_initialization_message";
    case LiveRuntimeStageErrorCode::baseline_decode_failed:
        return "baseline_decode_failed";
    case LiveRuntimeStageErrorCode::runtime_session_initialize_failed:
        return "runtime_session_initialize_failed";
    case LiveRuntimeStageErrorCode::runtime_record_failed:
        return "runtime_record_failed";
    case LiveRuntimeStageErrorCode::usercmd_schema_binding_failed:
        return "usercmd_schema_binding_failed";
    case LiveRuntimeStageErrorCode::usercmd_scheduler_failed:
        return "usercmd_scheduler_failed";
    case LiveRuntimeStageErrorCode::usercmd_adapter_failed:
        return "usercmd_adapter_failed";
    case LiveRuntimeStageErrorCode::usercmd_transmission_failed:
        return "usercmd_transmission_failed";
    case LiveRuntimeStageErrorCode::usercmd_observation_failed:
        return "usercmd_observation_failed";
    case LiveRuntimeStageErrorCode::stage_timed_out:
        return "stage_timed_out";
    case LiveRuntimeStageErrorCode::event_backpressure:
        return "event_backpressure";
    case LiveRuntimeStageErrorCode::driver_failed:
        return "driver_failed";
    case LiveRuntimeStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
