#pragma once

#include <hlclient/app/session_receipt.hpp>
#include <hlclient/app/movement_session_diagnostics.hpp>
#include <hlclient/app/remote_movement_diagnostics.hpp>
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
inline constexpr std::uint16_t kUnavailableExcludedPointBoxCount=0xffffU;
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
    std::optional<double> frame_interval_ms,cpu_update_ms,network_ms,parse_commit_ms;
    std::optional<double> prediction_ms,collision_ms,studio_ms,asset_ms,render_ms;
    std::optional<double> present_ms,report_ms,gpu_ms;
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
    void set_fallback_reason(std::string_view) noexcept;
    void set_correction_anchor_provenance(std::string_view) noexcept;
    void set_remote_sample_mode(std::string_view) noexcept;
    void set_blocker_kind(std::string_view) noexcept;
    void set_collision_query_purpose(std::string_view) noexcept;
    void set_authoritative_body_kind(std::string_view) noexcept;
    void set_presentation_reason(std::string_view) noexcept;
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
    std::uint64_t ordinal{},trigger_us{},end_us{},network_generation{},map_generation{};
    unsigned triggers{};
    bool complete{},generation_boundary{};
    std::uint64_t omitted_samples{};
    std::array<std::uint64_t,8> manual_marker_us{};
    std::size_t manual_marker_count{};
    std::uint64_t omitted_manual_markers{};
    std::vector<MovementFrameRecord> frames;
};
struct MovementDiagnosticsStatistics final {
    std::uint64_t accepted_frames{},duplicate_frames{},rejected_records{},invalid_values{};
    std::uint64_t ring_capacity_drops{},retention_evictions{},incident_limit_drops{},incident_sample_drops{};
    std::uint64_t auto_cooldown_suppressions{},generation_boundaries{},allocation_failures{};
    std::uint64_t manual_reservation_suppressions{};
    std::uint64_t gpu_observations{},gpu_unmatched{},gpu_invalid{};
    std::size_t retained_frames{},incidents{},allocated_capacity_bytes{};
    // Detailed buffers retain their original 8 MiB budget. Long-session
    // aggregates have a separate, explicit bounded heap allocation.
    std::size_t session_capacity_bytes{},remote_capacity_bytes{},total_capacity_bytes{};
};

// Collection is bounded and never performs clock queries, file I/O, rendering
// or protocol work. The application owns those observations and final output.
// Off returns before copies/allocations. Encode/summaries are exit/test work.
class MovementIncidentRecorder final {
public:
    // 968 still covers four seconds at 240 Hz. Larger E32.1 causal records
    // retain the same 8 MiB hard limit, eight incidents and 512-frame windows.
    static constexpr std::size_t maximum_recent_frames=968,maximum_incidents=8,maximum_incident_frames=512;
    static constexpr std::size_t manual_reserved_incidents=2,maximum_automatic_incidents=maximum_incidents-manual_reserved_incidents;
    static constexpr std::uint64_t retention_us=4'000'000,pre_incident_us=1'000'000,post_incident_us=1'000'000;
    static constexpr std::uint64_t automatic_cooldown_us=2'000'000,coalesce_us=250'000;
    static constexpr double frame_stall_ms=50.0,correction_trigger_units=8.0;
    static constexpr std::size_t maximum_capacity_bytes=8U*1024U*1024U,maximum_encoded_bytes=16U*1024U*1024U;
    explicit MovementIncidentRecorder(bool enabled=false) noexcept;
    [[nodiscard]] bool enabled() const noexcept {return enabled_;}
    [[nodiscard]] bool requested() const noexcept {return requested_;}
    [[nodiscard]] bool observe(const MovementFrameRecord&,const MovementFrameContext& = {}) noexcept;
    // Complete current per-actor presentation observations, separate from the
    // large local frame ring; caller skips this when capture is unavailable.
    [[nodiscard]] bool observe_remote(const MovementFrameRecord&,std::span<const RemoteMovementObservation>) noexcept;
    [[nodiscard]] bool mark(MovementIncidentTrigger,std::uint64_t monotonic_us) noexcept;
    // Delayed GPU results update only the observed frame identity, never the
    // current frame. Unknown/expired identities stay unmatched explicitly.
    [[nodiscard]] bool observe_gpu(std::uint64_t frame_number,double gpu_ms) noexcept;
    [[nodiscard]] MovementDiagnosticsStatistics statistics() const noexcept;
    [[nodiscard]] const MovementFrameRecord* recent_frame(std::size_t ordinal) const noexcept;
    [[nodiscard]] std::span<const MovementIncident> incidents() const noexcept {return incidents_;}
    [[nodiscard]] MovementMetricSummary window_summary(MovementTimingMetric) const;
    [[nodiscard]] std::optional<std::string> encode(const SessionReceiptIdentity&) const;
private:
    void append_recent(const MovementFrameRecord&) noexcept;
    [[nodiscard]] bool mark_impl(MovementIncidentTrigger,std::uint64_t,bool automatic) noexcept;
    bool requested_{},enabled_{};
    MovementSessionRecorder session_;
    RemoteMovementRecorder remote_;
    std::vector<MovementFrameRecord> recent_;
    std::vector<MovementIncident> incidents_;
    std::size_t first_{},count_{};
    std::optional<std::uint64_t> last_time_,last_frame_,last_auto_,last_correction_;
    std::optional<bool> previous_prediction_active_;
    std::uint64_t network_generation_{},map_generation_{};
    MovementDiagnosticsStatistics statistics_;
};

static_assert(sizeof(MovementFrameRecord)*(MovementIncidentRecorder::maximum_recent_frames+
    MovementIncidentRecorder::maximum_incidents*MovementIncidentRecorder::maximum_incident_frames)+
    sizeof(MovementIncident)*MovementIncidentRecorder::maximum_incidents<=MovementIncidentRecorder::maximum_capacity_bytes);
} // namespace hlclient::app
