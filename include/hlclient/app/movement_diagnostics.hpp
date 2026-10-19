#pragma once
#include <hlclient/app/multiplayer_timeline.hpp>

#include <hlclient/app/session_receipt.hpp>
#include <hlclient/app/movement_session_diagnostics.hpp>
#include <hlclient/app/remote_movement_diagnostics.hpp>
#include <hlclient/app/movement_velocity_stop_diagnostics.hpp>
#include <hlclient/core/movement_correction_witness.hpp>
#include <hlclient/core/prediction_work_diagnostics.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::app {

// All local stamps share the calling application's steady-clock origin in us.
// Server, command and presentation times are separate observed seconds. Missing
// observations remain null; this recorder does not infer authority or causes.
using MovementDiagnosticVector3 = std::array<double,3>;
using MovementDiagnosticFloat3 = std::array<float,3>;
using MovementCorrectionWitness=core::MovementCorrectionWitness;
inline constexpr std::uint16_t kUnavailableExcludedPointBoxCount=0xffffU;
enum class MovementGpuPassStatus : std::uint8_t {
    not_observed,pending,ready,unsupported,disjoint,invalid,capacity_dropped
};
// Compact delayed GPU observations fit the existing detailed-record budget.
// Values are meaningful only where the corresponding mask bit is set.
struct MovementGpuPassRecord final {
    std::array<float,6> milliseconds{};
    std::uint8_t valid_mask{};
    MovementGpuPassStatus status{MovementGpuPassStatus::not_observed};
};
// Exact adapter output which was accepted by command history, not sampled
// keys reconstructed after a shot. Sample time uses the receipt clock in us.
struct MovementDiagnosticCommand final {
    std::uint64_t sample_us{};
    std::uint32_t sequence{};
    std::array<std::uint16_t,3> angle_turns{};
    std::array<std::int16_t,3> movement{};
    std::uint16_t buttons{};
    std::uint8_t msec{};
};
struct MovementFrameRecord final {
    std::uint64_t monotonic_us{},frame_number{},network_generation{},map_generation{};
    std::uint64_t input_sample_us{},render_submit_us{},limiter_wait_us{},yield_wait_us{};
    bool previous_iteration_waits_known{};
    std::optional<double> frame_interval_ms,cpu_update_ms,network_ms,parse_commit_ms;
    std::optional<double> prediction_ms,collision_ms,studio_ms,asset_ms,render_ms;
    std::optional<double> present_ms,report_ms,gpu_ms;
    MovementGpuPassRecord gpu_passes;
    std::optional<std::uint64_t> socket_read_begin_us,socket_read_us;
    std::optional<std::uint64_t> network_poll_begin_us,network_poll_end_us;
    std::optional<std::uint64_t> parse_begin_us,commit_us,receipt_observed_us;
    std::optional<std::uint64_t> prediction_use_us,presentation_use_us,support_observation_us;
    std::optional<double> svc_time_s,command_time_s,render_sample_time_s,state_age_ms;
    std::optional<double> remote_previous_s,remote_current_s,remote_sample_s,remote_alpha;
    std::optional<double> support_observation_s;
    std::optional<double> raw_correction_units,camera_jump_units,replay_duration_ms,fallback_duration_ms;
    std::optional<std::uint64_t> source_sequence,correction_ordinal,collision_revision;
    // Owned-peer receive counts. Rejected sequences are not measured loss;
    // svc_choke markers do not establish server load or bot causality.
    std::optional<std::uint64_t> datagrams_received,rejected_sequences,choke_markers_total;
    std::optional<std::uint32_t> queue_depth,queue_depth_after,entities,bytes,committed_records,replayed_commands;
    // Latest committed player-schema entries, including receiver when present;
    // not live/visible players, bots or negotiated maxclients.
    std::optional<std::uint32_t> observed_players;
    std::optional<std::uint32_t> blocking_entity,support_entity,support_model_index,support_submodel,support_physent,hull;
    std::optional<bool> prediction_active,startsolid,allsolid,grounded;
    std::optional<double> trace_fraction,support_velocity_z;
    std::optional<MovementDiagnosticVector3> raw_server_origin,predicted_origin,presented_eye,velocity,plane_normal,basevelocity;
    std::optional<MovementDiagnosticVector3> previous_valid_origin,seed_origin,collision_query_start,collision_query_end;
    std::optional<MovementDiagnosticCommand> command;
    std::optional<std::uint64_t> authoritative_source_record_identity;
    std::optional<std::uint32_t> anchor_command,collision_plane_index,authoritative_source_sequence;
    // Optional typed ReferencePredictionAnchorStatus ordinal, not an ACK.
    std::optional<std::uint8_t> anchor_status;
    // Fits existing record alignment padding, preserving the hard8MiB budget
    // and frame capacity. 65535 means unavailable, not zero. Valid count<=8192.
    std::uint16_t excluded_point_boxes{kUnavailableExcludedPointBoxCount};
    std::optional<float> collision_plane_distance;
    std::optional<MovementDiagnosticFloat3> authoritative_velocity,matched_origin_before_correction;
    std::optional<MovementDiagnosticFloat3> matched_velocity_before_correction;
    // Base/final angles use camera pitch-up/yaw/roll degrees. Server punch
    // retains the wire's GoldSrc pitch-down convention; local punch is visual.
    std::optional<MovementDiagnosticFloat3> camera_base_angles,server_punch_angles,local_punch_angles,camera_final_angles;
    std::array<char,24> collision_query_purpose{},authoritative_body_kind{};
    std::array<char,40> presentation_reason{};
    std::array<char,96> fallback_reason{};
    std::array<char,48> correction_anchor_provenance{};
    std::array<char,32> remote_sample_mode{};
    std::array<char,32> blocker_kind{};
    // Application-relative steady clock, never a server-time estimate. The
    // optional duration remains unavailable when this owner was not observed.
    std::optional<std::uint64_t> life_wait_started_us;
    std::optional<double> life_wait_duration_ms;
    std::optional<std::uint32_t> candidate_anchor_command;
    std::array<char,32> recovery_wait_reason{'n','o','n','e'};
    // Appended so existing positional aggregate prefixes remain source-compatible.
    std::optional<core::PredictionWorkDiagnostics> prediction_work;
    void set_fallback_reason(std::string_view) noexcept;
    void set_correction_anchor_provenance(std::string_view) noexcept;
    void set_remote_sample_mode(std::string_view) noexcept;
    void set_blocker_kind(std::string_view) noexcept;
    void set_collision_query_purpose(std::string_view) noexcept;
    void set_authoritative_body_kind(std::string_view) noexcept;
    void set_presentation_reason(std::string_view) noexcept;
    void set_recovery_wait_reason(std::string_view) noexcept;
};

enum class MovementIncidentTrigger : std::uint8_t { manual=1,frame_stall=2,correction=4,fallback=8 };
[[nodiscard]] constexpr unsigned movement_incident_mask(MovementIncidentTrigger trigger) noexcept {
    return static_cast<unsigned>(trigger);
}
enum class MovementTimingMetric : std::uint8_t {
    frame_interval,cpu_update,network,parse_commit,prediction,collision,studio,asset,
    render,present,report,gpu,state_age,raw_correction,camera_jump,replay_duration,fallback_duration,count
};
struct MovementMetricSummary final {
    std::size_t observed{},not_available{};
    std::optional<double> p50,p95,p99,maximum;
};
struct MovementIncident final {
    std::uint64_t ordinal{},trigger_us{},initial_trigger_us{},end_us{},network_generation{},map_generation{};
    unsigned triggers{};
    bool complete{},generation_boundary{},life_boundary{},producer_rejection{};
    std::optional<std::uint64_t> life_epoch;
    std::uint64_t requested_pre_us{},requested_post_us{},sampling_interval_us{};
    std::uint64_t background_samples_omitted{},exact_samples_omitted{},last_sample_bucket{};
    std::uint8_t priority{};
    // Exact trigger/marker frames cannot be displaced by background sampling.
    std::array<std::uint8_t,480> exact_samples{}; // 0 background,1 event,2 manual marker.
    std::uint64_t omitted_samples{};
    std::array<std::uint64_t,8> manual_marker_us{};
    std::size_t manual_marker_count{};
    std::uint64_t omitted_manual_markers{};
    std::vector<MovementFrameRecord> frames;
};
enum class MovementCaptureDecisionKind : std::uint8_t {
    accepted,coalesced,expected_lifecycle,cooldown,manual_reservation,capacity,
    lower_priority,active_windows,allocation_failed,replaced,invalid_timestamp
};
struct MovementCaptureDecision final {
    std::uint64_t ordinal{},monotonic_us{},frame_number{},network_generation{},map_generation{},incident_ordinal{},replacement_ordinal{};
    std::optional<std::uint64_t> life_epoch,source_record_identity,source_sequence,correction_ordinal;
    unsigned trigger{};
    std::uint8_t priority{},context{},owner_state{};
    MovementCaptureDecisionKind decision{};
    std::array<char,96> reason{};
};
struct MovementDiagnosticsStatistics final {
    std::uint64_t accepted_frames{},duplicate_frames{},rejected_records{},invalid_values{};
    std::uint64_t ring_capacity_drops{},retention_evictions{},incident_limit_drops{},incident_sample_drops{};
    std::uint64_t auto_cooldown_suppressions{},generation_boundaries{},allocation_failures{};
    std::uint64_t manual_reservation_suppressions{};
    std::uint64_t lifecycle_summaries{},priority_replacements{},decision_evictions{},life_boundaries{};
    std::uint64_t background_samples_omitted{},exact_samples_omitted{};
    std::uint64_t correction_witnesses_accepted{},correction_witness_duplicates{},correction_witness_invalid{},correction_witness_evictions{};
    std::uint64_t correction_delivery_observations{},correction_delivery_sink_failures{},correction_delivery_published_events{};
    std::uint64_t correction_delivery_counter_resets{},correction_delivery_generation_boundaries{};
    std::uint64_t correction_delivery_duplicate_observations{},correction_delivery_stale_generation_observations{};
    std::size_t retained_decisions{},retained_correction_witnesses{};
    std::uint64_t gpu_observations{},gpu_unmatched{},gpu_invalid{};
    std::uint64_t velocity_stop_duplicates{},velocity_stop_evictions{},velocity_stop_invalid{};
    std::size_t velocity_stops{};
    std::size_t retained_frames{},incidents{},allocated_capacity_bytes{};
    // Detailed buffers retain their original 8 MiB budget. Long-session
    // aggregates have a separate, explicit bounded heap allocation.
    std::size_t session_capacity_bytes{},remote_capacity_bytes{},multiplayer_capacity_bytes{},total_capacity_bytes{};
};

// Collection is bounded and never performs clock queries, file I/O, rendering
// or protocol work. The application owns those observations and final output.
// Off returns before copies/allocations. Encode/summaries are exit/test work.
class MovementIncidentRecorder final : public core::MultiplayerDiagnosticSink {
public:
    // 961 includes both endpoints of four seconds at 240 Hz. Incident windows
    // retain bounded time-bucket backgrounds plus exact trigger/marker frames.
    static constexpr std::size_t maximum_recent_frames=961,maximum_incidents=8,maximum_incident_frames=480;
    static constexpr std::size_t manual_reserved_incidents=2,maximum_automatic_incidents=maximum_incidents-manual_reserved_incidents;
    static constexpr std::uint64_t retention_us=4'000'000,pre_incident_us=1'000'000,post_incident_us=1'000'000;
    static constexpr std::uint64_t manual_pre_incident_us=4'000'000,manual_sampling_interval_us=12'500,automatic_sampling_interval_us=8'333;
    static constexpr std::uint64_t automatic_cooldown_us=2'000'000,coalesce_us=250'000;
    static constexpr double frame_stall_ms=50.0,correction_trigger_units=8.0;
    static constexpr std::size_t maximum_capacity_bytes=8U*1024U*1024U,maximum_encoded_bytes=16U*1024U*1024U;
    static constexpr std::size_t publication_metadata_reserve=16U*1024U;
    static constexpr std::size_t maximum_velocity_stops=16;
    static constexpr std::size_t maximum_capture_decisions=128,maximum_correction_witnesses=24;
    static constexpr std::size_t maximum_top_correction_witnesses=8,maximum_recent_correction_witnesses=16;
    // Keep the four-second recent ring and every existing sidecar reservation.
    // Added per-frame facts consume the same hard budget, not a larger pool.
    static constexpr std::size_t reserved_non_incident_capacity_bytes=
        sizeof(MovementFrameRecord)*maximum_recent_frames+
        sizeof(MovementIncident)*maximum_incidents+
        sizeof(MovementVelocityStopRecord)*maximum_velocity_stops+
        sizeof(MovementCaptureDecision)*maximum_capture_decisions+
        sizeof(MovementCorrectionWitness)*maximum_correction_witnesses;
    static_assert(reserved_non_incident_capacity_bytes<maximum_capacity_bytes);
    static constexpr std::size_t incident_frame_capacity=[] {
        constexpr auto available=(maximum_capacity_bytes-reserved_non_incident_capacity_bytes)/
            maximum_incidents/sizeof(MovementFrameRecord);
        return available<maximum_incident_frames?available:maximum_incident_frames;
    }();
    static_assert(incident_frame_capacity>0U);
    explicit MovementIncidentRecorder(bool enabled=false) noexcept;
    [[nodiscard]] bool enabled() const noexcept override {return enabled_;}
    [[nodiscard]] bool observe(const core::MultiplayerDiagnosticEvent& event) noexcept override {return enabled_ && multiplayer_.observe(event);}
    void set_multiplayer_clock_origin_ns(std::uint64_t origin) noexcept {if(enabled_ && !multiplayer_clock_origin_ns_){multiplayer_clock_origin_ns_=origin;multiplayer_.set_clock_origin_ns(origin);}}
    [[nodiscard]] MultiplayerTimelineStatistics multiplayer_statistics() const noexcept {return multiplayer_.statistics();}
    [[nodiscard]] bool requested() const noexcept {return requested_;}
    [[nodiscard]] bool observe(const MovementFrameRecord&,const MovementFrameContext& = {}) noexcept;
    void set_update_context(const MovementFrameContext& context) noexcept {if(enabled_){update_context_=context;update_context_known_=true;}}
    // Separate unique-event pool: sticky snapshot reads never duplicate a stop,
    // and large causal payloads are not copied into every retained frame.
    [[nodiscard]] bool observe_velocity_stop(const MovementFrameRecord&,const MovementVelocityStopRecord&) noexcept;
    [[nodiscard]] bool observe_correction(const MovementCorrectionWitness&) noexcept;
    // Cumulative counters from the actual producer, never inferred from frame
    // ordinals. New network/map generations begin new counter lifetimes;
    // decreases within one generation are explicit partial-delivery resets.
    void observe_correction_delivery(std::uint64_t network_generation,std::uint64_t map_generation,
        std::uint64_t published_events,std::uint64_t sink_failures) noexcept;
    [[nodiscard]] std::span<const MovementCaptureDecision> capture_decisions() const noexcept {return decisions_;}
    // Complete current per-actor presentation observations, separate from the
    // large local frame ring; caller skips this when capture is unavailable.
    [[nodiscard]] bool observe_remote(const MovementFrameRecord&,std::span<const RemoteMovementObservation>) noexcept;
    [[nodiscard]] bool mark(MovementIncidentTrigger,std::uint64_t monotonic_us) noexcept;
    // Delayed GPU results update only the observed frame identity, never the
    // current frame. Unknown/expired identities stay unmatched explicitly.
    [[nodiscard]] bool observe_gpu(std::uint64_t frame_number,double gpu_ms) noexcept;
    [[nodiscard]] bool observe_gpu_passes(std::uint64_t frame_number,const MovementGpuPassRecord&) noexcept;
    [[nodiscard]] MovementDiagnosticsStatistics statistics() const noexcept;
    [[nodiscard]] const MovementFrameRecord* recent_frame(std::size_t ordinal) const noexcept;
    [[nodiscard]] std::span<const MovementIncident> incidents() const noexcept {return incidents_;}
    [[nodiscard]] MovementMetricSummary window_summary(MovementTimingMetric) const;
    [[nodiscard]] std::optional<std::string> encode(const SessionReceiptIdentity&) const;
private:
    void append_recent(const MovementFrameRecord&) noexcept;
    [[nodiscard]] bool mark_impl(MovementIncidentTrigger,std::uint64_t,bool automatic) noexcept;
    void append_incident(MovementIncident&,const MovementFrameRecord&,bool exact,bool marker=false) noexcept;
    void capture_decision(MovementIncidentTrigger,std::uint64_t,std::uint8_t,MovementCaptureDecisionKind,
        std::uint64_t incident=0,std::uint64_t replacement=0) noexcept;
    bool requested_{},enabled_{};
    MovementSessionRecorder session_;
    RemoteMovementRecorder remote_;
    MultiplayerTimeline multiplayer_;
    std::uint64_t multiplayer_clock_origin_ns_{},multiplayer_prediction_ordinal_{},multiplayer_remote_ordinal_{};
    std::vector<MovementFrameRecord> recent_;
    std::vector<MovementIncident> incidents_;
    std::vector<MovementVelocityStopRecord> velocity_stops_;
    std::vector<MovementCaptureDecision> decisions_;
    std::vector<MovementCorrectionWitness> correction_top_,correction_recent_;
    std::size_t decision_first_{},correction_recent_first_{};
    std::optional<std::array<std::uint64_t,3>> last_correction_witness_identity_;
    std::optional<std::array<std::uint64_t,2>> correction_delivery_generation_;
    std::uint64_t correction_delivery_last_published_{},correction_delivery_last_failures_{};
    std::uint64_t incident_ordinal_{},decision_ordinal_{},correction_events_with_raw_{};
    const MovementCorrectionWitness* correction_event_{}; // Borrowed only during synchronous observe_correction.
    std::size_t velocity_stop_first_{};
    std::optional<std::array<std::uint64_t,3>> last_velocity_stop_identity_;
    std::size_t first_{},count_{};
    std::optional<std::uint64_t> last_time_,last_frame_,last_auto_,last_correction_;
    std::optional<bool> previous_prediction_active_;
    std::optional<std::uint64_t> last_observed_life_epoch_;
    MovementFrameContext context_;
    MovementFrameContext update_context_;
    bool update_context_known_{};
    std::array<char,96> previous_fallback_reason_{};
    std::array<std::optional<std::uint64_t>,6> automatic_class_time_{};
    std::uint64_t network_generation_{},map_generation_{};
    MovementDiagnosticsStatistics statistics_;
};

static_assert(sizeof(MovementFrameRecord)*(MovementIncidentRecorder::maximum_recent_frames+
    MovementIncidentRecorder::maximum_incidents*MovementIncidentRecorder::incident_frame_capacity)+
    sizeof(MovementIncident)*MovementIncidentRecorder::maximum_incidents+
    sizeof(MovementVelocityStopRecord)*MovementIncidentRecorder::maximum_velocity_stops+
    sizeof(MovementCaptureDecision)*MovementIncidentRecorder::maximum_capture_decisions+
    sizeof(MovementCorrectionWitness)*MovementIncidentRecorder::maximum_correction_witnesses<=MovementIncidentRecorder::maximum_capacity_bytes);
} // namespace hlclient::app
