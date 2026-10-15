#include <hlclient/goldsrc/live_runtime_stage.hpp>

#include <hlclient/goldsrc/client_message.hpp>
#include <hlclient/goldsrc/disconnect_request.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/surface_texture_query.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <hlclient/prediction/prediction_visual_correction.hpp>

#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <new>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

namespace hlclient::goldsrc {
namespace {

constexpr std::uint64_t kLiveRuntimeGeneration = 1U;
constexpr std::size_t kDiagnosticTextLimit = 256U;

[[nodiscard]] std::optional<std::uint64_t> actual_monotonic_ns() noexcept {
  const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if (value < 0) return std::nullopt;
  return static_cast<std::uint64_t>(value);
}

class MovementCostScope final {
public:
  MovementCostScope(const bool enabled, double& output) noexcept
      : enabled_{enabled}, output_{output} {
    if (enabled_) started_ = std::chrono::steady_clock::now();
  }
  ~MovementCostScope() {
    if (enabled_) output_ += std::chrono::duration<double, std::milli>{
        std::chrono::steady_clock::now() - started_}.count();
  }
private:
  bool enabled_;
  double& output_;
  std::chrono::steady_clock::time_point started_{};
};

class ReplayDurationScope final {
public:
  explicit ReplayDurationScope(std::optional<double>* duration) noexcept : duration_{duration} {}
  ~ReplayDurationScope() { if (duration_ && !completed_) duration_->reset(); }
  void complete() noexcept { completed_ = true; }
private:
  std::optional<double>* duration_;
  bool completed_{false};
};

[[nodiscard]] bool terminal_state(const LiveRuntimeStageState state) noexcept {
  switch (state) {
  case LiveRuntimeStageState::stable_runtime_state_ready:
  case LiveRuntimeStageState::live_usercmd_check_ready:
  case LiveRuntimeStageState::live_visual_control_ready:
  case LiveRuntimeStageState::timed_out:
  case LiveRuntimeStageState::cancelled:
  case LiveRuntimeStageState::backpressure:
  case LiveRuntimeStageState::secondary_stream_pending:
  case LiveRuntimeStageState::network_error:
  case LiveRuntimeStageState::protocol_error:
    return true;
  case LiveRuntimeStageState::idle:
  case LiveRuntimeStageState::waiting_for_resource_response:
  case LiveRuntimeStageState::waiting_for_baselines:
  case LiveRuntimeStageState::receiving_runtime:
  case LiveRuntimeStageState::waiting_for_live_visual_input:
  case LiveRuntimeStageState::running_usercmd_scenario:
  case LiveRuntimeStageState::running_live_visual_control:
    return false;
  }
  return true;
}

[[nodiscard]] const DeltaSchemaRegistryState &delta_registry_from(
    const ResourceClientResponseSignonState &response) noexcept {
  return response.resource_list()
      .transition()
      .user_info()
      .movement_environment()
      .delta_description()
      .registry();
}

[[nodiscard]] const ServerInfoState &
server_info_from(const ResourceClientResponseSignonState &response) noexcept {
  return response.resource_list()
      .transition()
      .user_info()
      .movement_environment()
      .delta_description()
      .pre_resource()
      .server_info();
}

[[nodiscard]] std::vector<PostMoveVarsUserMessageDefinition>
user_messages_from(const ResourceClientResponseSignonState &response) {
  std::vector<PostMoveVarsUserMessageDefinition> definitions;
  const auto &controls = response.resource_list()
                             .transition()
                             .user_info()
                             .movement_environment()
                             .stream()
                             .controls();
  definitions.reserve(controls.size());
  for (const auto &control : controls) {
    if (const auto *definition =
            std::get_if<PostMoveVarsUserMessageDefinition>(&control.body())) {
      definitions.push_back(*definition);
    }
  }
  return definitions;
}

[[nodiscard]] std::size_t
schema_field_count(const DeltaSchemaRegistryState &registry) noexcept {
  std::size_t count = 0U;
  for (const auto &schema : registry.schemas()) {
    if (schema.fields().size() >
        (std::numeric_limits<std::size_t>::max)() - count) {
      return (std::numeric_limits<std::size_t>::max)();
    }
    count += schema.fields().size();
  }
  return count;
}

[[nodiscard]] std::optional<std::uint8_t>
opcode_at(const OwnedServicePayload &payload,
          const StockRuntimeSourceCursor &cursor) noexcept {
  if (!cursor.byte_aligned() || cursor.byte_offset() >= payload.bytes.size()) {
    return std::nullopt;
  }
  return std::to_integer<std::uint8_t>(payload.bytes[cursor.byte_offset()]);
}

[[nodiscard]] bool padding_only(const OwnedServicePayload &payload) noexcept {
  return payload.bytes.size() <= 8U &&
         std::ranges::all_of(payload.bytes, [](const std::byte value) {
           return value == std::byte{0U};
         });
}

[[nodiscard]] bool
valid_usercmd_scenario(const LiveUserCmdScenarioConfig &scenario, bool lifecycle = false) noexcept {
  if (scenario.command_interval.count() <= 0 ||
      scenario.command_interval > std::chrono::milliseconds{255} ||
      scenario.forward_amplitude <= 0 || scenario.forward_amplitude > 2'047 ||
      !std::isfinite(scenario.fixed_yaw_degrees) ||
      !std::isfinite(scenario.fixed_pitch_degrees) ||
      !std::isfinite(scenario.velocity_tolerance) ||
      !std::isfinite(scenario.origin_tolerance) ||
      scenario.velocity_tolerance <= 0.0 || scenario.origin_tolerance <= 0.0 ||
      scenario.maximum_samples == 0U ||
      scenario.maximum_samples > kMaximumLiveUserCmdSamples ||
      scenario.maximum_transmit_ranges == 0U ||
      scenario.maximum_transmit_ranges > kMaximumLiveUserCmdTransmitRanges) {
    return false;
  }
  std::chrono::milliseconds total{0};
  for (const auto duration : scenario.durations) {
    if (duration.count() <= 0 ||
        duration.count() % scenario.command_interval.count() != 0 ||
        duration > std::chrono::seconds{lifecycle ? 8 : 5}) {
      return false;
    }
    total += duration;
  }
  return total <= std::chrono::seconds{lifecycle ? 40 : 15} &&
         quantize_wire_angle(scenario.fixed_yaw_degrees).has_value() &&
         quantize_wire_angle(scenario.fixed_pitch_degrees).has_value();
}

[[nodiscard]] std::chrono::milliseconds
scenario_duration(const LiveUserCmdScenarioConfig &scenario) noexcept {
  std::chrono::milliseconds total{0};
  for (const auto duration : scenario.durations)
    total += duration;
  return total;
}

[[nodiscard]] std::size_t
phase_index_for_offset(const LiveUserCmdScenarioConfig &scenario,
                       const std::chrono::nanoseconds offset) noexcept {
  auto boundary = std::chrono::nanoseconds{0};
  for (std::size_t index = 0U; index < scenario.durations.size(); ++index) {
    boundary += std::chrono::duration_cast<std::chrono::nanoseconds>(
        scenario.durations[index]);
    if (offset < boundary || index + 1U == scenario.durations.size()) {
      return index;
    }
  }
  return scenario.durations.size() - 1U;
}

[[nodiscard]] constexpr LiveUserCmdInputPhase
phase_from_index(const std::size_t index) noexcept {
  return static_cast<LiveUserCmdInputPhase>(index);
}

// The host synthesizes typed actions for deterministic scenarios using the
// selected module's immutable physical bindings; it owns no game key defaults.
void press_action(input::InputStateTracker& tracker,
                  const game_api::GameMovementPolicy& policy,
                  const gameplay_input::GameplayInputAction action) {
  if (!policy.bindings) return;
  for (const auto& binding : policy.bindings->bindings()) {
    if (binding.action() != action) continue;
    if (binding.physical_key())
      tracker.apply_event(input::InputEvent::key_pressed(*binding.physical_key()));
    else if (binding.physical_mouse_button())
      tracker.apply_event(input::InputEvent::mouse_button_pressed(*binding.physical_mouse_button()));
    return;
  }
}

[[nodiscard]] std::optional<gameplay_input::GameplayInputIntent>
make_scenario_intent(const game_api::GameMovementPolicy& policy,
                     const LiveUserCmdInputPhase phase,
                     const bool side_check = false,
                     const bool jump_duck_check = false,
                     const bool speed_check = false,
                     const bool predicted_actions = false) noexcept {
  try {
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (phase == LiveUserCmdInputPhase::forward) {
      press_action(tracker, policy,
          jump_duck_check ? gameplay_input::GameplayInputAction::jump
          : side_check ? gameplay_input::GameplayInputAction::move_left
                       : gameplay_input::GameplayInputAction::move_forward);
      if (predicted_actions)
        press_action(tracker, policy, gameplay_input::GameplayInputAction::move_forward);
    } else if (phase == LiveUserCmdInputPhase::backward) {
      press_action(tracker, policy,
          jump_duck_check ? gameplay_input::GameplayInputAction::duck
          : speed_check ? gameplay_input::GameplayInputAction::move_forward
          : side_check ? gameplay_input::GameplayInputAction::move_right
                       : gameplay_input::GameplayInputAction::move_backward);
      if (speed_check)
        press_action(tracker, policy, gameplay_input::GameplayInputAction::speed);
      if (predicted_actions) {
        press_action(tracker, policy, gameplay_input::GameplayInputAction::move_forward);
        press_action(tracker, policy, gameplay_input::GameplayInputAction::move_right);
      }
    } else if (phase == LiveUserCmdInputPhase::neutral_tail &&
               predicted_actions) {
      press_action(tracker, policy, gameplay_input::GameplayInputAction::move_forward);
      press_action(tracker, policy, gameplay_input::GameplayInputAction::speed);
    }
    const auto snapshot = tracker.publish_snapshot();
    if (!policy.bindings) return std::nullopt;
    auto intent = gameplay_input::GameplayInputIntentBuilder{}.build(
        snapshot, *policy.bindings, policy.mouse_look, 0.02);
    if (!intent || !intent.intent)
      return std::nullopt;
    return std::move(*intent.intent);
  } catch (...) {
    return std::nullopt;
  }
}

// The bounded weapon check submits ordinary immutable commands through the
// same scheduler. The server remains the sole source of weapon effects.
[[nodiscard]] std::optional<gameplay_input::GameplayInputIntent>
make_weapon_check_intent(const game_api::GameMovementPolicy& policy,
                         const bool attack, const bool reload,
                         const bool move, const bool slow_walk) noexcept {
  try {
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (move)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::move_forward);
    if (slow_walk)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::speed);
    if (attack)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::attack_primary);
    if (reload)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::reload);
    if (!policy.bindings) return std::nullopt;
    auto built = gameplay_input::GameplayInputIntentBuilder{}.build(
        tracker.publish_snapshot(), *policy.bindings,
        policy.mouse_look, 0.02);
    if (!built || !built.intent) return std::nullopt;
    return std::move(*built.intent);
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<gameplay_input::GameplayInputIntent>
make_live_visual_intent(const game_api::GameMovementPolicy& policy,
                        const LiveVisualControlInput &control) noexcept {
  const auto allowed_buttons = policy.held_buttons;
  const auto direction=[](const gameplay_input::GameplayButtonMask buttons,
                          const gameplay_input::GameplayButton positive,
                          const gameplay_input::GameplayButton negative) {
    return static_cast<float>(
        ((buttons & gameplay_input::gameplay_button_mask(positive)) != 0U ? 1 : 0) -
        ((buttons & gameplay_input::gameplay_button_mask(negative)) != 0U ? 1 : 0));
  };
  if (control.generation != kLiveRuntimeGeneration ||
      control.input_revision == 0U ||
      !std::isfinite(control.forward_axis) ||
      !std::isfinite(control.side_axis) || control.forward_axis < -1.0F ||
      control.forward_axis > 1.0F || control.side_axis < -1.0F ||
      control.side_axis > 1.0F || !std::isfinite(control.yaw_degrees) ||
      !std::isfinite(control.pitch_degrees) ||
      ((control.held_buttons | control.pressed_buttons | control.released_buttons | control.consumed_buttons) &
       ~allowed_buttons) != 0U ||
      control.forward_axis != direction(control.held_buttons,
          gameplay_input::GameplayButton::move_forward,
          gameplay_input::GameplayButton::move_backward) ||
      control.side_axis != direction(control.held_buttons,
          gameplay_input::GameplayButton::move_right,
          gameplay_input::GameplayButton::move_left) ||
      (!control.focused &&
       (control.held_buttons != 0U || control.pressed_buttons != 0U))) {
    return std::nullopt;
  }
  try {
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(control.focused ? input::InputEvent::focus_gained()
                                        : input::InputEvent::focus_lost());
    if (control.focused && control.captured) {
      tracker.apply_event(input::InputEvent::capture_acquired());
    }
    if (control.focused && (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::move_forward)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::move_forward);
    if (control.focused && (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::move_backward)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::move_backward);
    if (control.focused && (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::move_left)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::move_left);
    if (control.focused && (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::move_right)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::move_right);
    if (control.focused &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::jump)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::jump);
    if (control.focused &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::duck)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::duck);
    if (control.focused &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::speed)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::speed);
    if (control.focused && control.captured &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::attack_primary)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::attack_primary);
    if (control.focused && control.captured &&
        policy.button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::attack_secondary)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::attack_secondary);
    if (control.focused &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(
            gameplay_input::GameplayButton::reload)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::reload);
    if (control.focused && control.captured &&
        (control.held_buttons & gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::use)) != 0U)
      press_action(tracker, policy, gameplay_input::GameplayInputAction::use);
    const auto snapshot = tracker.publish_snapshot();
    if (!policy.bindings) return std::nullopt;
    auto intent = gameplay_input::GameplayInputIntentBuilder{}.build(
        snapshot, *policy.bindings, policy.mouse_look, 0.02);
    if (!intent || !intent.intent)
      return std::nullopt;
    return std::move(*intent.intent);
  } catch (...) {
    return std::nullopt;
  }
}

void apply_live_runtime_resource_response_profile(
    ResourceClientResponseStageConfig &config) noexcept {
  config.advertisement_profile =
      ClientResourceAdvertisementProfile::no_custom_resources;
  config.pre_transmit_payload_policy =
      ResourceResponsePreTransmitPayloadPolicy::decode_nop_control;
  config.completion_policy =
      ResourceResponseCompletionPolicy::covering_acknowledgement;
  config.post_response_payload_compression =
      ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
  config.resource_list.transition.second_service_payload_compression =
      ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
  config.resource_list.transition.user_info.movement_environment.delta
      .pre_resource.initial_signon.service_payload_envelope.compression_policy =
      ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
  config.resource_list.transition.user_info.movement_environment.delta
      .pre_resource.initial_signon.driver
      .maximum_unfragmented_reliable_payload =
      kOpcode5EmptyResourceResponseSemanticSize - 1U;
  // Stock sign-on transmission can be gated by new client packets. Poll an
  // otherwise idle channel, including after a non-reliable sendres ACK, without
  // resubmitting a semantic command. Ordinary usercmd/ACK TX resets this clock.
  config.resource_list.transition.user_info.movement_environment.delta
      .pre_resource.initial_signon.driver.idle_poll_interval =
      std::chrono::milliseconds{200};
  config.resource_list.transition.user_info.movement_environment.delta
      .pre_resource.initial_signon.driver.accept_udp_split_packets = true;
}

} // namespace

std::string_view to_string(const LiveUserCmdDiagnosticErrorCode code) noexcept {
  switch (code) {
  case LiveUserCmdDiagnosticErrorCode::invalid_transmit_metadata:
    return "invalid_transmit_metadata";
  case LiveUserCmdDiagnosticErrorCode::transmit_retention_exhausted:
    return "transmit_retention_exhausted";
  case LiveUserCmdDiagnosticErrorCode::server_sample_retention_exhausted:
    return "server_sample_retention_exhausted";
  }
  return "unknown";
}

std::optional<LiveUserCmdTransmitRange> inspect_live_usercmd_transmit_range(
    const GoldSrcUserCmdTransmissionEvent& event) noexcept {
  if (event.type != GoldSrcUserCmdTransmissionEventType::move_packet_submitted ||
      !event.outgoing_netchan_sequence || !event.first_new_command_sequence ||
      !event.last_new_command_sequence || event.new_command_count == 0U ||
      *event.first_new_command_sequence == 0U ||
      *event.last_new_command_sequence < *event.first_new_command_sequence ||
      static_cast<std::uint64_t>(*event.last_new_command_sequence) -
              *event.first_new_command_sequence + 1U != event.new_command_count) {
    return {};
  }
  return LiveUserCmdTransmitRange{
      *event.outgoing_netchan_sequence, *event.first_new_command_sequence,
      *event.last_new_command_sequence, event.new_command_count,
      event.backup_command_count};
}

void LiveRuntimeStageError::retain_runtime_failure(RuntimeReplayError failure) noexcept {
    if (code != LiveRuntimeStageErrorCode::runtime_record_failed || runtime_failure) return;
    runtime_code = failure.code;
    runtime_decoder_error = failure.decoder_error;
    runtime_cursor = failure.decoder_cursor;
    wire_opcode = failure.decoder_wire_opcode;
    runtime_record_ordinal = failure.record_ordinal;
    runtime_source_sequence = failure.source_sequence;
    runtime_failure = std::move(failure);
}

GoldSrcUserCmdMovementSpeedConfig live_visual_movement_speeds(
    const bool keyboard_mouse, const float forward_axis, const float side_axis,
    const float amplitude) noexcept {
  const float speed = keyboard_mouse && forward_axis != 0.0F &&
                              side_axis != 0.0F
                          ? amplitude * 0.7071067811865475F
                          : amplitude;
  return {speed, speed, keyboard_mouse ? speed : 0.0F};
}

LiveUserCmdMotionOutcome evaluate_live_usercmd_motion(
    const std::span<const LiveUserCmdServerSample> samples,
    const std::array<std::size_t, kLiveUserCmdPhaseCount> &sent_by_phase,
    const double fixed_yaw_degrees, const double velocity_tolerance,
    const double origin_tolerance) noexcept {
  if (!std::isfinite(fixed_yaw_degrees) || !std::isfinite(velocity_tolerance) ||
      velocity_tolerance <= 0.0 || !std::isfinite(origin_tolerance) ||
      origin_tolerance <= 0.0 ||
      std::ranges::any_of(
          sent_by_phase, [](const std::size_t count) { return count == 0U; })) {
    return LiveUserCmdMotionOutcome::server_motion_unverified;
  }
  bool positive_health = false;
  bool health_observed = false;
  std::array<std::vector<std::pair<double, double>>, kLiveUserCmdPhaseCount>
      values;
  const auto radians = fixed_yaw_degrees * 3.14159265358979323846 / 180.0;
  const auto forward_x = std::cos(radians);
  const auto forward_y = std::sin(radians);
  try {
    for (const auto &sample : samples) {
      if (sample.health) {
        health_observed = true;
        positive_health = positive_health || *sample.health > 0.0;
      }
      if (!sample.origin.x || !sample.origin.y || !sample.velocity.x ||
          !sample.velocity.y) {
        continue;
      }
      const auto index = static_cast<std::size_t>(sample.phase);
      if (index >= values.size()) {
        return LiveUserCmdMotionOutcome::observation_context_blocked;
      }
      values[index].emplace_back(
          *sample.origin.x * forward_x + *sample.origin.y * forward_y,
          *sample.velocity.x * forward_x + *sample.velocity.y * forward_y);
    }
  } catch (...) {
    return LiveUserCmdMotionOutcome::observation_context_blocked;
  }
  if ((health_observed && !positive_health) || values[0].empty() ||
      values[1].empty() || values[3].empty() || values[4].empty()) {
    return LiveUserCmdMotionOutcome::observation_context_blocked;
  }
  auto forward_window = values[1];
  auto backward_window = values[3];
  try {
    forward_window.insert(forward_window.end(), values[2].begin(),
                          values[2].end());
    backward_window.insert(backward_window.end(), values[4].begin(),
                           values[4].end());
  } catch (...) {
    return LiveUserCmdMotionOutcome::observation_context_blocked;
  }
  if (forward_window.size() < 2U || backward_window.empty()) {
    return LiveUserCmdMotionOutcome::observation_context_blocked;
  }
  const auto maximum_component = [](const auto &range,
                                    const std::size_t component) {
    auto value = component == 0U ? range.front().first : range.front().second;
    for (const auto &sample : range) {
      value = (std::max)(value, component == 0U ? sample.first : sample.second);
    }
    return value;
  };
  const auto minimum_component = [](const auto &range,
                                    const std::size_t component) {
    auto value = component == 0U ? range.front().first : range.front().second;
    for (const auto &sample : range) {
      value = (std::min)(value, component == 0U ? sample.first : sample.second);
    }
    return value;
  };
  const auto baseline_velocity = maximum_component(values[0], 1U);
  const auto forward_velocity = maximum_component(forward_window, 1U);
  const auto backward_velocity = minimum_component(backward_window, 1U);
  const auto forward_origin = maximum_component(forward_window, 0U) -
                              minimum_component(forward_window, 0U);
  return forward_velocity >= baseline_velocity + velocity_tolerance &&
                 forward_origin >= origin_tolerance &&
                 backward_velocity <= forward_velocity - velocity_tolerance
             ? LiveUserCmdMotionOutcome::verified
             : LiveUserCmdMotionOutcome::server_motion_unverified;
}

LiveUserCmdMotionOutcome evaluate_live_speed_check(
    const std::span<const LiveUserCmdServerSample> samples,
    const std::array<std::size_t, kLiveUserCmdPhaseCount> &sent_by_phase,
    const double fixed_yaw_degrees, const double origin_tolerance,
    const double velocity_tolerance) noexcept {
  if (!std::isfinite(fixed_yaw_degrees) || !std::isfinite(origin_tolerance) ||
      !std::isfinite(velocity_tolerance) || origin_tolerance <= 0.0 ||
      velocity_tolerance <= 0.0 || sent_by_phase[1] == 0U ||
      sent_by_phase[3] == 0U)
    return LiveUserCmdMotionOutcome::server_motion_unverified;
  const double radians = fixed_yaw_degrees * 3.14159265358979323846 / 180.0;
  const double x = std::cos(radians), y = std::sin(radians);
  struct Window {
    std::optional<double> first_origin, last_origin;
    std::optional<double> maximum_velocity, last_velocity;
    std::size_t count{0U};
  };
  Window normal, slow;
  bool health_observed = false, alive = false;
  std::optional<client::RuntimeObservationSource> last_source;
  std::array<std::pair<double, double>, 3U> settled_vertical{};
  std::size_t neutral_vertical_count = 0U;
  for (const auto &sample : samples) {
    if (sample.generation != kLiveRuntimeGeneration ||
        sample.freshness !=
            client::RuntimeObservationFreshness::observed_in_record ||
        (last_source && sample.source == *last_source))
      return LiveUserCmdMotionOutcome::observation_context_blocked;
    last_source = sample.source;
    if (sample.health) {
      health_observed = true;
      alive |= *sample.health > 0.0;
    }
    if (sample.phase == LiveUserCmdInputPhase::neutral_before &&
        sample.origin.z && sample.velocity.z) {
      settled_vertical[neutral_vertical_count % settled_vertical.size()] =
          {*sample.origin.z, *sample.velocity.z};
      ++neutral_vertical_count;
    }
    if (sample.phase != LiveUserCmdInputPhase::forward &&
        sample.phase != LiveUserCmdInputPhase::backward)
      continue;
    if (!sample.origin.x || !sample.origin.y || !sample.velocity.x ||
        !sample.velocity.y)
      continue;
    auto &window = sample.phase == LiveUserCmdInputPhase::forward ? normal : slow;
    const double origin = *sample.origin.x * x + *sample.origin.y * y;
    const double velocity = *sample.velocity.x * x + *sample.velocity.y * y;
    if (!window.first_origin) window.first_origin = origin;
    window.last_origin = origin;
    window.maximum_velocity = window.maximum_velocity
        ? std::max(*window.maximum_velocity, velocity) : velocity;
    window.last_velocity = velocity;
    ++window.count;
  }
  if ((health_observed && !alive) || normal.count < 2U || slow.count < 2U ||
      neutral_vertical_count < settled_vertical.size())
    return LiveUserCmdMotionOutcome::observation_context_blocked;
  const auto [minimum_z, maximum_z] = std::minmax_element(
      settled_vertical.begin(), settled_vertical.end(),
      [](const auto &a, const auto &b) { return a.first < b.first; });
  if (maximum_z->first - minimum_z->first > origin_tolerance ||
      std::ranges::any_of(settled_vertical, [&](const auto &value) {
        return std::abs(value.second) > velocity_tolerance;
      }))
    return LiveUserCmdMotionOutcome::observation_context_blocked;
  if (*normal.last_origin - *normal.first_origin < origin_tolerance ||
      *slow.last_origin - *slow.first_origin < origin_tolerance ||
      *normal.maximum_velocity < velocity_tolerance ||
      *slow.last_velocity < velocity_tolerance ||
      *normal.maximum_velocity <= *slow.last_velocity + velocity_tolerance)
    return LiveUserCmdMotionOutcome::server_motion_unverified;
  return LiveUserCmdMotionOutcome::verified;
}

LiveJumpDuckEvaluation evaluate_live_jump_duck(
    const std::span<const LiveUserCmdServerSample> samples,
    const std::array<std::size_t, kLiveUserCmdPhaseCount> &sent_by_phase,
    const double origin_tolerance, const double velocity_tolerance,
    const double view_offset_tolerance) noexcept {
  LiveJumpDuckEvaluation result;
  result.outcome = LiveJumpDuckOutcome::buttons_transmitted_server_effect_unverified;
  if (!std::isfinite(origin_tolerance) || origin_tolerance <= 0.0 ||
      !std::isfinite(velocity_tolerance) || velocity_tolerance <= 0.0 ||
      !std::isfinite(view_offset_tolerance) || view_offset_tolerance <= 0.0 ||
      std::ranges::any_of(sent_by_phase,
                          [](const auto count) { return count == 0U; })) {
    result.outcome = LiveJumpDuckOutcome::observation_context_blocked;
    return result;
  }
  std::array<std::size_t, kLiveUserCmdPhaseCount> complete_samples{};
  std::optional<double> baseline_z;
  std::optional<double> baseline_eye_z;
  std::size_t stable_baseline_tail = 0U;
  std::optional<double> peak_z;
  std::optional<double> duck_offset_z;
  std::optional<double> duck_eye_z;
  std::optional<std::uint64_t> generation;
  std::uint64_t last_revision = 0U;
  for (const auto &sample : samples) {
    if (sample.generation == 0U ||
        (generation && sample.generation != *generation) ||
        sample.freshness != client::RuntimeObservationFreshness::observed_in_record ||
        sample.publication_revision <= last_revision ||
        (sample.health && *sample.health <= 0.0)) {
      result.outcome = LiveJumpDuckOutcome::observation_context_blocked;
      return result;
    }
    generation = sample.generation;
    last_revision = sample.publication_revision;
    if (!sample.health || !sample.origin.complete() ||
        !sample.velocity.complete() || !sample.view_offset.z)
      continue;
    const auto index = static_cast<std::size_t>(sample.phase);
    if (index >= complete_samples.size()) {
      result.outcome = LiveJumpDuckOutcome::observation_context_blocked;
      return result;
    }
    ++complete_samples[index];
    const double z = *sample.origin.z;
    const double eye_z = z + *sample.view_offset.z;
    if (index == 0U) {
      stable_baseline_tail =
          std::abs(*sample.velocity.z) <= velocity_tolerance
              ? ((baseline_z && std::abs(z - *baseline_z) <= origin_tolerance)
                     ? (std::min)(stable_baseline_tail + 1U, 3U)
                     : 1U)
              : 0U;
      baseline_z = z;
      baseline_eye_z = eye_z;
      continue;
    }
    if (!baseline_z || !baseline_eye_z)
      continue;
    if (index == 1U || index == 2U) {
      if (z >= *baseline_z + origin_tolerance &&
          *sample.velocity.z >= velocity_tolerance) {
        result.jump_observed = true;
        peak_z = peak_z ? (std::max)(*peak_z, z) : z;
      }
      if (result.jump_observed && peak_z &&
          ((*sample.velocity.z <= -velocity_tolerance &&
            z >= *baseline_z) ||
           (index == 2U && z <= *baseline_z + origin_tolerance &&
            z <= *peak_z - origin_tolerance))) {
        result.descent_observed = true;
      }
    }
    if (index == 3U &&
        *sample.view_offset.z <=
            *baseline_eye_z - *baseline_z - view_offset_tolerance &&
        eye_z <= *baseline_eye_z - view_offset_tolerance) {
      result.duck_observed = true;
      duck_offset_z = duck_offset_z
                          ? (std::min)(*duck_offset_z, *sample.view_offset.z)
                          : *sample.view_offset.z;
      duck_eye_z = duck_eye_z ? (std::min)(*duck_eye_z, eye_z) : eye_z;
    }
    if (index == 4U && duck_offset_z && duck_eye_z &&
        *sample.view_offset.z >= *duck_offset_z + view_offset_tolerance &&
        eye_z >= *duck_eye_z + view_offset_tolerance) {
      result.release_response_observed = true;
    }
  }
  if (complete_samples[0] < 2U || complete_samples[1] == 0U ||
      complete_samples[2] == 0U || complete_samples[3] == 0U ||
      complete_samples[4] == 0U || !baseline_z) {
    result.outcome = LiveJumpDuckOutcome::observation_context_blocked;
  } else if (stable_baseline_tail < 3U) {
    result.outcome = LiveJumpDuckOutcome::environment_limited;
  } else if (result.jump_observed && result.descent_observed &&
             result.duck_observed && result.release_response_observed) {
    result.outcome = LiveJumpDuckOutcome::verified;
  } else if (result.jump_observed && result.descent_observed &&
             result.duck_observed && !result.release_response_observed) {
    result.outcome = LiveJumpDuckOutcome::environment_limited;
  } else if (result.jump_observed && result.descent_observed) {
    result.outcome = LiveJumpDuckOutcome::jump_verified_duck_pending;
  }
  return result;
}

std::string_view to_string(const LiveJumpDuckOutcome outcome) noexcept {
  switch (outcome) {
  case LiveJumpDuckOutcome::not_evaluated: return "not_evaluated";
  case LiveJumpDuckOutcome::verified: return "verified";
  case LiveJumpDuckOutcome::jump_verified_duck_pending:
    return "jump_verified_duck_pending";
  case LiveJumpDuckOutcome::buttons_transmitted_server_effect_unverified:
    return "buttons_transmitted_server_effect_unverified";
  case LiveJumpDuckOutcome::observation_context_blocked:
    return "observation_context_blocked";
  case LiveJumpDuckOutcome::environment_limited: return "environment_limited";
  }
  return "unknown";
}

ResourceClientResponseStageConfig
default_live_runtime_resource_response_stage_config() {
  ResourceClientResponseStageConfig config;
  apply_live_runtime_resource_response_profile(config);
  return config;
}

void apply_live_runtime_compatibility_profile(
    LiveRuntimeStageConfig &config) noexcept {
  apply_live_runtime_resource_response_profile(config.resource_response);
  config.envelope.compression_policy =
      ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
}

bool valid_live_runtime_stage_configuration(
    const LiveRuntimeStageConfig &config) noexcept {
  return config.game_client && config.game_client->active() &&
         (!config.required_game_directory ||
          (!config.required_game_directory->empty() && config.required_game_directory->size()<=64U &&
           std::ranges::all_of(*config.required_game_directory,[](unsigned char c) {
             return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-';
           }))) &&
         game_api::valid_game_movement_policy(config.game_client->movement_policy()) &&
         valid_resource_client_response_stage_configuration(config.resource_response) &&
         valid_service_payload_envelope_limits(config.envelope) &&
         GoldSrcEntityBaselineDecoder{config.baselines}.valid_configuration() &&
         valid_runtime_replay_limits(config.runtime) &&
         config.stable_interval.count() > 0 &&
         config.stable_interval <= kMaximumLiveRuntimeStableInterval &&
         config.timeout >= config.stable_interval &&
         (config.timeout <= kMaximumLiveRuntimeTimeout ||
          (config.operation_mode == LiveRuntimeOperationMode::live_visual_control &&
           (config.live_visual_input_source == LiveVisualControlInputSource::scripted_fire_reload_presentation_check ||
            config.live_visual_input_source == LiveVisualControlInputSource::scripted_damage_respawn_check) &&
           config.timeout <= std::chrono::seconds{90})) &&
         config.maximum_driver_events_per_update > 0U &&
         config.maximum_driver_events_per_update <=
             kMaximumLiveRuntimeDriverEventsPerUpdate &&
         config.maximum_events > 0U &&
         config.maximum_events <= kMaximumLiveRuntimeEvents &&
         (!config.reference_prediction ||
          config.operation_mode == LiveRuntimeOperationMode::live_visual_control) &&
         (config.operation_mode == LiveRuntimeOperationMode::runtime_state ||
          ((config.operation_mode ==
                LiveRuntimeOperationMode::live_usercmd_check ||
            config.operation_mode ==
                LiveRuntimeOperationMode::live_visual_control) &&
           valid_usercmd_scenario(config.usercmd_scenario,
               config.live_visual_input_source == LiveVisualControlInputSource::scripted_damage_respawn_check)));
}

class LiveRuntimeStage::Implementation final {
public:
  Implementation(
      network::IDatagramTransport &transport,
      const network::NetworkAddress remote_endpoint,
      client::ClientWorldState &target, LiveRuntimeStageConfig config,
      resource_consistency::IResourceConsistencyProvider *consistency_provider,
      LiveRuntimeStageTraceCallback trace_callback,
      InitialSignonTraceCallback initial_trace_callback,
      PreResourceSignonTraceCallback pre_resource_trace_callback,
      DeltaDescriptionTraceCallback delta_trace_callback,
      MovementEnvironmentTraceCallback movement_trace_callback,
      UserInfoSignonTraceCallback user_info_trace_callback,
      ResourceTransitionTraceCallback transition_trace_callback,
      ResourceListTraceCallback resource_list_trace_callback,
      ResourceClientResponseTraceCallback response_trace_callback)
      : target_{target}, config_{std::move(config)},
        trace_callback_{std::move(trace_callback)},
        configuration_valid_{valid_live_runtime_stage_configuration(config_)},
        response_stage_{
            transport,
            remote_endpoint,
            config_.resource_response,
            consistency_provider,
            std::move(initial_trace_callback),
            std::move(pre_resource_trace_callback),
            std::move(delta_trace_callback),
            std::move(movement_trace_callback),
            std::move(user_info_trace_callback),
            std::move(transition_trace_callback),
            std::move(resource_list_trace_callback),
            std::move(response_trace_callback),
            ResourceClientResponseStage::RetainPostResourcePayloadAtBoundary{}},
        event_slots_(configuration_valid_ ? config_.maximum_events : 0U) {
    if (configuration_valid_)
      prediction_movement_config_ = config_.game_client->movement_policy().movement_config;
    if (config_.reference_prediction) {
      prediction_state_ = LiveReferencePredictionState::waiting_for_seed;
      prediction_reason_ = "collision_attachment_pending";
    }
  }

  ~Implementation() {
    if (state_ != LiveRuntimeStageState::idle && !cleanup_done_) {
      cleanup(last_update_.value_or(LiveRuntimeStageTimePoint{}), false);
    }
  }

  [[nodiscard]] bool
  start(const LiveRuntimeStageTimePoint now,
        const network::NetworkAddress &expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime) {
    if (state_ != LiveRuntimeStageState::idle || trace_callback_active_) {
      return false;
    }
    if (!configuration_valid_ || target_.runtime_observation()) {
      fail(LiveRuntimeStageErrorCode::invalid_configuration,
           LiveRuntimeStageState::protocol_error,
           target_.runtime_observation()
               ? "Live runtime target already owns an observation"
               : "Live runtime stage limits are outside project hard caps",
           now);
      return false;
    }
    bool started = false;
    try {
      started = response_stage_.start(now, expected_local_endpoint,
                                      std::move(connection_lifetime));
    } catch (...) {
      fail(LiveRuntimeStageErrorCode::response_stage_start_failed,
           LiveRuntimeStageState::protocol_error,
           "Nested resource-response stage threw during start", now);
      return false;
    }
    if (!started) {
      fail_from_response(now, true);
      return false;
    }
    started_at_ = now;
    last_update_ = now;
    state_ = LiveRuntimeStageState::waiting_for_resource_response;
    return true;
  }

  void reset_movement_update_diagnostics() noexcept {
    if (config_.movement_diagnostics) {
      movement_diagnostics_.enabled = true;
      movement_diagnostics_.network_driver_ms = 0.0;
      movement_diagnostics_.network_parse_commit_ms = 0.0;
      movement_diagnostics_.collision_scene_ms = 0.0;
      movement_diagnostics_.prediction_command_ms = 0.0;
      movement_diagnostics_.prediction_replay_ms = 0.0;
      movement_diagnostics_.driver_queue_before = 0U;
      movement_diagnostics_.driver_queue_after = 0U;
      movement_diagnostics_.processed_driver_events = 0U;
      movement_diagnostics_.committed_records = 0U;
      movement_diagnostics_.committed_bytes = 0U;
      movement_diagnostics_.replayed_commands = 0U;
      movement_diagnostics_.replayed_simulation_ms = 0.0;
      movement_diagnostics_.socket_read_begin_ns.reset();
      movement_diagnostics_.socket_read_end_ns.reset();
      movement_diagnostics_.parse_begin_ns.reset();
      movement_diagnostics_.atomic_commit_ns.reset();
      movement_diagnostics_.prediction_use_ns.reset();
    }
  }

  void update(const LiveRuntimeStageTimePoint now) {
    if (state_ == LiveRuntimeStageState::idle || terminal_state(state_) ||
        trace_callback_active_) {
      return;
    }
    if (last_update_ && now < *last_update_) {
      fail(LiveRuntimeStageErrorCode::time_moved_backwards,
           LiveRuntimeStageState::protocol_error,
           "Live runtime stage time moved backwards", now);
      return;
    }
    last_update_ = now;
    reset_movement_update_diagnostics();
    const bool unbounded_keyboard_control =
        !graceful_close_.requested &&
        config_.operation_mode == LiveRuntimeOperationMode::live_visual_control &&
        config_.live_visual_input_source ==
            LiveVisualControlInputSource::keyboard_mouse &&
        state_ == LiveRuntimeStageState::running_live_visual_control;
    if (graceful_close_.requested) {
      update_graceful_close(now);
      return;
    }
    if (!unbounded_keyboard_control && started_at_ &&
        now - *started_at_ >= config_.timeout) {
      fail(LiveRuntimeStageErrorCode::stage_timed_out,
           LiveRuntimeStageState::timed_out,
           "Live runtime stage did not reach its stable stop", now);
      return;
    }

    if (state_ == LiveRuntimeStageState::waiting_for_resource_response) {
      try {
        response_stage_.update(now);
      } catch (...) {
        fail(LiveRuntimeStageErrorCode::response_stage_failed,
             LiveRuntimeStageState::protocol_error,
             "Nested resource-response stage threw during update", now);
        return;
      }
      synchronize_from_response(now);
      if (terminal_state(state_) ||
          state_ == LiveRuntimeStageState::waiting_for_resource_response) {
        return;
      }
    }

    auto *const driver = response_stage_.retained_driver();
    if (driver == nullptr) {
      fail(LiveRuntimeStageErrorCode::retained_driver_missing,
           LiveRuntimeStageState::protocol_error,
           "Live runtime continuation lost its retained driver", now);
      return;
    }
    driver->set_movement_diagnostics_enabled(config_.movement_diagnostics);
    if (config_.movement_diagnostics)
      movement_diagnostics_.driver_queue_before = driver->pending_event_count();
    if (state_ == LiveRuntimeStageState::running_usercmd_scenario ||
        state_ == LiveRuntimeStageState::running_live_visual_control) {
      update_usercmd_before_driver(now);
      if (terminal_state(state_))
        return;
    }
    try {
      const MovementCostScope cost{config_.movement_diagnostics,
          movement_diagnostics_.network_driver_ms};
      driver->update(now);
      last_driver_rx_ = driver->receive_statistics();
      if (config_.movement_diagnostics)
        movement_diagnostics_.receive_statistics = last_driver_rx_;
    } catch (...) {
      fail(LiveRuntimeStageErrorCode::driver_failed,
           LiveRuntimeStageState::network_error,
           "Retained driver threw during live runtime update", now);
      return;
    }
    if (state_ == LiveRuntimeStageState::running_usercmd_scenario ||
        state_ == LiveRuntimeStageState::running_live_visual_control) {
      update_usercmd_after_driver(now);
      if (terminal_state(state_))
        return;
    }
    observe_spawn_request_transmit(now);
    if (terminal_state(state_)) {
      return;
    }
    observe_signon_reply_transmit(now);
    if (terminal_state(state_)) {
      return;
    }

    std::size_t processed = 0U;
    while (processed < config_.maximum_driver_events_per_update &&
           !terminal_state(state_)) {
      auto event = driver->poll_event();
      if (!event) {
        break;
      }
      ++processed;
      handle_driver_event(std::move(*event), now);
    }
    if (config_.movement_diagnostics) {
      movement_diagnostics_.processed_driver_events = processed;
      movement_diagnostics_.driver_queue_after = driver->pending_event_count();
    }
    if (!terminal_state(state_) && driver->pending_event_count() != 0U &&
        processed == config_.maximum_driver_events_per_update) {
      fail(LiveRuntimeStageErrorCode::event_backpressure,
           LiveRuntimeStageState::backpressure,
           "Live runtime driver event budget was exhausted", now);
      return;
    }
    if (!terminal_state(state_) && stable_started_at_ &&
        stable_progress_observed_ && signon_reply_acknowledged_ &&
        now - *stable_started_at_ >= config_.stable_interval) {
      if (config_.operation_mode == LiveRuntimeOperationMode::runtime_state) {
        publish_success(now);
      } else if (config_.operation_mode ==
                 LiveRuntimeOperationMode::live_visual_control) {
        if (state_ != LiveRuntimeStageState::waiting_for_live_visual_input &&
            state_ != LiveRuntimeStageState::running_live_visual_control) {
          d_handoff_interval_ =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - *stable_started_at_);
          state_ = LiveRuntimeStageState::waiting_for_live_visual_input;
          const auto ready = LiveRuntimeStageEvent{
              .type = LiveRuntimeStageEventType::live_visual_input_ready,
              .publication_revision =
                  target_.runtime_observation()
                      ? target_.runtime_observation()->publication_revision
                      : 0U,
              .occurred_at = now};
          push_event(ready);
          emit_trace(ready);
        }
      } else if (state_ != LiveRuntimeStageState::running_usercmd_scenario) {
        activate_usercmd_scenario(now);
      }
    }
    if (!terminal_state(state_) &&
        (state_ == LiveRuntimeStageState::running_usercmd_scenario ||
         (state_ == LiveRuntimeStageState::running_live_visual_control &&
           bounded_live_visual_scenario(config_.live_visual_input_source)))) {
      complete_usercmd_scenario_if_ready(now);
    }
  }

  [[nodiscard]] bool request_graceful_close(const LiveRuntimeStageTimePoint now) {
    if (trace_callback_active_) return false;
    if (graceful_close_.requested) return true;
    graceful_close_.requested = true;
    graceful_close_.operation_identity = 1U;
    graceful_close_.session_generation = kLiveRuntimeGeneration;
    graceful_close_.requested_at = now;
    if (terminal_state(state_)) {
      graceful_close_.outcome = error_ ? GracefulCloseOutcome::fatal_failure
                                     : GracefulCloseOutcome::cancel_without_protocol_disconnect;
      graceful_close_.netchan_closed_at = now;
      return true; // Preserve owning error/result; never relabel it success.
    }
    live_visual_input_.reset();
    button_latch_.clear();
    weapon_command_submissions_.clear();
    if (config_.game_client) config_.game_client->cancel_uncommitted();
    auto* const driver = response_stage_.close_driver();
    if (!driver) {
      graceful_close_.outcome = GracefulCloseOutcome::cancel_without_protocol_disconnect;
      graceful_close_.netchan_closed_at = now;
      state_ = LiveRuntimeStageState::cancelled;
      cleanup(now, true);
      return true;
    }
    if (!driver->request_graceful_close(now)) {
      graceful_close_ = {};
      return false;
    }
    graceful_close_ = driver->graceful_close_snapshot();
    if (driver->last_error()) {
      fail(LiveRuntimeStageErrorCode::driver_failed,
           driver->state() == NetchanDriverState::network_error
               ? LiveRuntimeStageState::network_error : LiveRuntimeStageState::protocol_error,
           "Owning transport failed before graceful close", now,
           std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           driver->last_error()->code);
    } else if (graceful_close_.outcome != GracefulCloseOutcome::pending) {
      state_ = LiveRuntimeStageState::cancelled;
      cleanup(now, true);
    }
    return true;
  }

  [[nodiscard]] GracefulCloseSummary graceful_close_snapshot() const noexcept {
    return graceful_close_;
  }

  void update_graceful_close(const LiveRuntimeStageTimePoint now) {
    auto* const driver = response_stage_.close_driver();
    if (!driver) {
      fail(LiveRuntimeStageErrorCode::retained_driver_missing,
           LiveRuntimeStageState::protocol_error,
           "Graceful close lost its same-session driver", now);
      return;
    }
    try { driver->update(now); }
    catch (...) {
      fail(LiveRuntimeStageErrorCode::driver_failed, LiveRuntimeStageState::network_error,
           "Retained driver threw during graceful close", now);
      return;
    }
    graceful_close_ = driver->graceful_close_snapshot();
    std::size_t processed{};
    while (processed < config_.maximum_driver_events_per_update) {
      auto event = driver->poll_event();
      if (!event) break;
      ++processed;
      if (event->type == NetchanDriverEventType::payload_ready && event->payload) {
        // Close does not apply gameplay, continue sign-on or generate commands.
        // It only recognizes an exact current-channel terminal control.
        auto decoded = ServicePayloadEnvelopeDecoder{config_.envelope}.decode(std::move(*event->payload));
        if (!decoded || !decoded.envelope) {
          fail(LiveRuntimeStageErrorCode::envelope_decode_failed,
               LiveRuntimeStageState::protocol_error,
               "Service envelope failed while closing", now,
               decoded.error ? std::optional{decoded.error->code} : std::nullopt);
          return;
        }
        const auto notice = inspect_disconnect_notice(decoded.envelope->payload.bytes);
        if (notice == DisconnectNoticeStatus::malformed) {
          fail(LiveRuntimeStageErrorCode::runtime_record_failed,
               LiveRuntimeStageState::protocol_error,
               "Terminal server notice is malformed", now);
          return;
        }
        if (notice == DisconnectNoticeStatus::observed) {
          driver->confirm_server_disconnect(now);
          graceful_close_ = driver->graceful_close_snapshot();
        }
      }
    }
    if (driver->last_error()) {
      fail(LiveRuntimeStageErrorCode::driver_failed,
           driver->state() == NetchanDriverState::network_error
               ? LiveRuntimeStageState::network_error : LiveRuntimeStageState::protocol_error,
           "Owning transport failed while closing", now,
           std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           driver->last_error()->code);
      return;
    }
    if (graceful_close_.outcome != GracefulCloseOutcome::pending) {
      state_ = LiveRuntimeStageState::cancelled;
      cleanup(now, true);
    }
  }

  void cancel(const LiveRuntimeStageTimePoint now) {
    if (state_ == LiveRuntimeStageState::idle || terminal_state(state_) ||
        trace_callback_active_) {
      return;
    }
    state_ = LiveRuntimeStageState::cancelled;
    error_.reset();
    result_.reset();
    const LiveRuntimeStageEvent event{
        .type = LiveRuntimeStageEventType::cancelled, .occurred_at = now};
    push_event(event);
    emit_trace(event);
    if (graceful_close_.outcome == GracefulCloseOutcome::pending) {
      if (auto* driver = response_stage_.close_driver()) {
        driver->cancel(now);
        graceful_close_ = driver->graceful_close_snapshot();
      }
    }
    cleanup(now, false);
  }

  [[nodiscard]] bool submit_live_visual_input(
      const LiveVisualControlInput &input,
      const LiveRuntimeStageTimePoint now) noexcept {
    if (graceful_close_.requested || config_.operation_mode !=
            LiveRuntimeOperationMode::live_visual_control ||
        (state_ != LiveRuntimeStageState::waiting_for_live_visual_input &&
         state_ != LiveRuntimeStageState::running_live_visual_control) ||
        input.generation != kLiveRuntimeGeneration ||
        input.input_revision == 0U ||
        (live_visual_input_ &&
         input.input_revision <= live_visual_input_->input_revision) ||
        (!config_.game_client || !make_live_visual_intent(config_.game_client->movement_policy(), input)) ||
        !quantize_wire_angle(input.yaw_degrees) ||
        !quantize_wire_angle(input.pitch_degrees) ||
        (last_update_ && now < *last_update_)) {
      return false;
    }
    live_visual_input_ = input;
    consume_live_visual_input_buttons(*live_visual_input_,button_latch_);
    const auto scoped = config_.game_client->movement_policy().capture_life_scoped_buttons;
    const bool input_active = input.focused && input.captured &&
        !(target_.runtime_observation() && target_.runtime_observation()->lifecycle.dead());
    scoped_button_gate_.filter(scoped, input_active,
        live_visual_input_->held_buttons, live_visual_input_->pressed_buttons, input.released_buttons);
    if (!input_active) button_latch_.clear(scoped);
    if (!input.focused) {
      live_visual_input_->forward_axis = 0.0F;
      live_visual_input_->side_axis = 0.0F;
      live_visual_input_->captured = false;
      live_visual_input_->held_buttons = 0U;
      live_visual_input_->pressed_buttons = 0U;
      button_latch_.clear();
    } else if (state_ == LiveRuntimeStageState::running_live_visual_control) {
      button_latch_.observe(live_visual_input_->pressed_buttons, true,
          target_.runtime_observation() ? config_.game_client->edge_triggered_buttons(
              *target_.runtime_observation()) : 0U);
    }
    live_visual_input_at_ = now;
    return true;
  }

  [[nodiscard]] bool request_weapon_selection(const std::uint8_t weapon_id) {
    if (graceful_close_.requested || config_.operation_mode != LiveRuntimeOperationMode::live_visual_control ||
        state_ != LiveRuntimeStageState::running_live_visual_control ||
        !target_.runtime_observation()) return false;
    if (!config_.game_client) return false;
    const auto request = config_.game_client->inventory_selection(
        *target_.runtime_observation(), weapon_id);
    if (!request) return false;
    const auto built = encode_game_command(*request);
    if (!built || !built.bytes) return false;
    auto *driver = response_stage_.retained_driver();
    if (!driver || driver->terminal()) return false;
    // One application queue operation per physical input edge. Netchan owns
    // retransmission of retained reliable bytes after would_block/packet loss.
    return static_cast<bool>(driver->queue_reliable(*built.bytes));
  }

  [[nodiscard]] bool activate_live_visual_control(
      const LiveRuntimeStageTimePoint now) noexcept {
    if (graceful_close_.requested || config_.operation_mode !=
            LiveRuntimeOperationMode::live_visual_control ||
        state_ != LiveRuntimeStageState::waiting_for_live_visual_input ||
        (config_.live_visual_input_source ==
             LiveVisualControlInputSource::keyboard_mouse &&
         !live_visual_input_) ||
        (last_update_ && now < *last_update_)) {
      return false;
    }
    activate_usercmd_scenario(now);
    return state_ == LiveRuntimeStageState::running_live_visual_control;
  }

  [[nodiscard]] bool attach_reference_prediction_collision(
      std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package) {
    if (!config_.reference_prediction || !package ||
        config_.operation_mode != LiveRuntimeOperationMode::live_visual_control ||
        prediction_collision_ || !response_stage_.result())
      return false;
    std::unique_ptr<movement::WorldOnlyMovementCollision> attached;
    try {
      attached = std::make_unique<movement::WorldOnlyMovementCollision>(
          std::move(package));
    } catch (const std::bad_alloc&) {
      prediction_state_ = LiveReferencePredictionState::failed;
      prediction_reason_ = "prediction_collision_allocation_failed";
      return false;
    }
    if (!attached->valid() || !attached->session_identity())
      return false;
    const auto environment = movement::GoldSrcMovementEnvironmentBuilder::
        from_move_vars(response_stage_.result()->resource_list().transition()
                           .user_info().movement_environment().move_vars(),
            config_.game_client->movement_policy().environment_profile);
    if (!environment)
      return false;
    prediction_environment_.emplace(*environment.environment);
    const auto library = collision::build_brush_collision_model_library(attached->package());
    if (!library) return false;
    prediction_brush_library_ = library.library;
    for (const auto& entry : response_stage_.result()->resource_list().resource_list().entries()) {
      if (entry.type() != ResourceType::model) continue;
      prediction_model_names_.emplace(entry.index().value(), entry.name().bytes());
      if (entry.name().bytes().starts_with('*')) prediction_has_brush_models_ = true;
    }
    prediction_camera_collision_query_ =
        std::make_unique<hlclient::collision::CollisionWorldQuery>(attached->package());
    prediction_collision_ = std::move(attached);
    prediction_state_ = LiveReferencePredictionState::waiting_for_seed;
    prediction_reason_ = "waiting_for_server_seed";
    if (const auto& observed = target_.runtime_observation(); observed)
      refresh_prediction_collision(*observed);
    return true;
  }

  [[nodiscard]] bool attach_reference_prediction_surfaces(
      std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> package) {
    if (!config_.reference_prediction || !package || surface_texture_query_ ||
        !prediction_camera_collision_query_)
      return false;
    try { surface_texture_query_=std::make_unique<SurfaceTextureQuery>(
        std::move(package),prediction_camera_collision_query_->package()); }
    catch (const std::bad_alloc&) { return false; }
    return true;
  }

  void refresh_prediction_collision(const client::RuntimeClientObservationState& observation) {
    if (!prediction_brush_library_ || (!prediction_has_brush_models_ &&
        !config_.game_client->movement_policy().server_box_solid_code &&
        !config_.game_client->movement_policy().server_player_solid_code)) return;
    // Retained entities retain the same immutable context. Select it before
    // seed/replay, never midway through the suffix or from render interpolation.
    if (prediction_brush_context_.scene && observation.entity_metadata.source &&
        observation.entity_metadata.source->record_identity == prediction_brush_context_.record)
      return;
    const MovementCostScope cost{config_.movement_diagnostics,
        movement_diagnostics_.collision_scene_ms};
    auto context = build_reference_brush_collision(prediction_brush_library_,
        prediction_model_names_, observation, kLiveRuntimeGeneration,
        config_.game_client->movement_policy().server_box_solid_code,
        static_cast<std::uint32_t>(client_slot_)+1U,
        config_.game_client->movement_policy().exclude_receiving_client_owned_boxes,
        config_.game_client->movement_policy().server_player_solid_code,
        config_.game_client->movement_policy().nonblocking_point_boxes);
    const auto same_vector=[](const auto& a,const auto& b) {
      return a.x==b.x && a.y==b.y && a.z==b.z;
    };
    const bool repeated_scene=context.scene && prediction_brush_context_.scene &&
        context.generation==prediction_brush_context_.generation &&
        context.server_time==prediction_brush_context_.server_time && context.server_time &&
        context.pushers==prediction_brush_context_.pushers &&
        context.boxes==prediction_brush_context_.boxes &&
        context.scene->model_library()==prediction_brush_context_.scene->model_library() &&
        std::equal(context.scene->instances().begin(),context.scene->instances().end(),
          prediction_brush_context_.scene->instances().begin(),prediction_brush_context_.scene->instances().end(),
          [&](const auto& a,const auto& b) { return a.identity==b.identity && a.role==b.role &&
              same_vector(a.transform.translation,b.transform.translation) &&
              same_vector(a.transform.rotation_degrees,b.transform.rotation_degrees); });
    if (config_.game_client->movement_policy().derived_vertical_support && !repeated_scene) {
      if (!prediction_support_motion_ ||
          !prediction_support_motion_->refresh_same_time_context(context))
        prediction_support_motion_.emplace(prediction_brush_context_, context);
    }
    prediction_presentation_cache_.reset();
    if (!context.scene) {
      prediction_collision_.reset();
      suspend_reference_prediction(context.reason);
    } else {
      if (prediction_brush_context_.scene) for (const auto& current : context.scene->instances()) {
        for (const auto& old : prediction_brush_context_.scene->instances()) {
          if (current.identity != old.identity) continue;
          const auto differs = [](const auto& a, const auto& b) {
            return a.x != b.x || a.y != b.y || a.z != b.z;
          };
          if (differs(current.transform.translation, old.transform.translation) ||
                differs(current.transform.rotation_degrees, old.transform.rotation_degrees)) {
              ++prediction_brush_transform_changes_;
              prediction_last_changed_brush_entity_=current.identity.source_entity_index;
              prediction_last_changed_brush_model_=current.identity.source_model_index;
            }
        }
      }
      prediction_collision_ = std::make_unique<movement::BrushSceneMovementCollision>(context.scene,context.boxes);
    }
    prediction_brush_context_ = std::move(context);
  }

  bool prediction_eye_clear(const assets::AssetVector3& start,
      const assets::AssetVector3& end,
      std::shared_ptr<const collision::BrushCollisionScene> sampled = {}) const {
    if (!sampled) sampled = prediction_brush_context_.scene;
    if (!sampled) return true; // world checked by caller
    collision::BrushCollisionSceneTraceRequest request;
    request.start = start; request.end = end;
    request.query_limits = prediction_movement_config_.collision_query.query_limits;
    const auto trace = collision::BrushCollisionSceneQuery{sampled}.
        trace_hull(request, prediction_presentation_scratch_);
    ++prediction_presentation_trace_queries_;
    return trace && trace.result && !trace.result->trace.start_solid &&
        !trace.result->trace.all_solid && trace.result->trace.fraction >= 1.0;
  }

  static bool compatible_velocity_correction_pose(
      const hlclient::movement::LocalPlayerMovementState& a,
      const hlclient::movement::LocalPlayerMovementState& b) noexcept {
    const auto dry = [](const auto mode) {
      return mode == hlclient::movement::PlayerMovementMode::walking ||
          mode == hlclient::movement::PlayerMovementMode::airborne;
    };
    const auto& x = a.view_offset(); const auto& y = b.view_offset();
    return dry(a.mode()) && dry(b.mode()) && a.hull() == b.hull() &&
        !a.in_duck_transition() && !b.in_duck_transition() &&
        x.x == y.x && x.y == y.y && x.z == y.z;
  }

  bool velocity_correction_context_continuous(const std::uint32_t receiving_entity_number) const {
    const auto& observation = target_.runtime_observation();
    if (!observation || observation->lifecycle.dead()) return false;
    // The spawn setangle slot persists. Only a NEW source is a discontinuity.
    if (observation->view_angle_correction &&
        (!prediction_last_view_angle_source_ ||
         observation->view_angle_correction->source != *prediction_last_view_angle_source_)) return false;
    for (const auto& entity : observation->packet_entities)
      if (entity.entity_number == receiving_entity_number)
        return (entity.effects.value_or(0U) & 32U) == 0U; // GoldSrc EF_NOINTERP
    return false;
  }

  bool verified_server_velocity_correction(
      const hlclient::movement::LocalPlayerMovementState& matched,
      const hlclient::movement::LocalPlayerMovementState& corrected,
      const ReferencePredictionSeed& seed, const LiveRuntimeStageTimePoint now,
      const double raw_error) const {
    // Local-compatible visual policy, not an inferred damage event. A modest
    // position discrepancy must be explained by a newly observed velocity in
    // at most 100 ms, in the same direction (2 units of quantization/contact
    // tolerance). Unclassified large corrections keep the existing snap gate.
    if (!(raw_error > .25 && raw_error <= 32.) || !prediction_last_anchor_at_ ||
        now < *prediction_last_anchor_at_ || now - *prediction_last_anchor_at_ > std::chrono::milliseconds{100} ||
        !compatible_velocity_correction_pose(matched, corrected) ||
        !velocity_correction_context_continuous(seed.receiving_entity_number) ||
        !prediction_collision_ || !prediction_camera_collision_query_) return false;
    const auto& a = matched.origin(); const auto& b = corrected.origin();
    const auto& av = matched.velocity(); const auto& bv = corrected.velocity();
    const double px = double(b.x)-a.x, py = double(b.y)-a.y, pz = double(b.z)-a.z;
    const double vx = double(bv.x)-av.x, vy = double(bv.y)-av.y, vz = double(bv.z)-av.z;
    const double speed2 = vx*vx + vy*vy + vz*vz;
    if (!std::isfinite(speed2) || speed2 < 32.*32.) return false;
    const double interval = (px*vx + py*vy + pz*vz)/speed2;
    if (!(interval > 0. && interval <= .100) ||
        std::hypot(px-vx*interval, py-vy*interval, pz-vz*interval) > 2.) return false;
    const auto body = prediction_collision_->trace_hull(b, a, corrected.hull(),
        prediction_presentation_scratch_, prediction_movement_config_.collision_query);
    ++prediction_presentation_trace_queries_;
    if (!body || !body.result || body.result->start_solid || body.result->all_solid ||
        body.result->fraction < 1.) return false;
    const auto endpoint = prediction_collision_->test_position(a, corrected.hull(),
        prediction_presentation_scratch_, prediction_movement_config_.collision_query);
    ++prediction_presentation_trace_queries_;
    if (!endpoint || !endpoint.result || endpoint.result->status != movement::LocalMovementPositionStatus::free)
      return false;
    const auto& offset = corrected.view_offset();
    hlclient::collision::CollisionTraceRequest eye;
    eye.start = {b.x+offset.x,b.y+offset.y,b.z+offset.z};
    eye.end = {a.x+offset.x,a.y+offset.y,a.z+offset.z};
    eye.hull = hlclient::collision::CollisionHullOrdinal::point;
    eye.limits = prediction_movement_config_.collision_query.query_limits;
    const auto traced = prediction_camera_collision_query_->trace_line(eye, prediction_presentation_scratch_);
    ++prediction_presentation_trace_queries_;
    return traced && traced.result && !traced.result->start_solid && !traced.result->all_solid &&
        traced.result->fraction >= 1. && prediction_eye_clear(eye.start, eye.end);
  }

  [[nodiscard]] LiveReferencePredictionSnapshot
  live_reference_prediction_snapshot(const LiveRuntimeStageTimePoint now) const {
    const auto world_revision = target_.runtime_publication_revision();
    if (prediction_presentation_cache_ && prediction_presentation_cache_at_ == now &&
        prediction_presentation_cache_history_ == prediction_history_.get() &&
        prediction_presentation_cache_world_revision_ == world_revision &&
        prediction_presentation_cache_epoch_ == prediction_epoch_ &&
        prediction_presentation_cache_state_ == prediction_state_)
      return *prediction_presentation_cache_;
    const auto work_started = std::chrono::steady_clock::now();
    std::shared_ptr<const collision::BrushCollisionScene> presentation_scene;
    std::unique_ptr<movement::BrushSceneMovementCollision> presentation_collision;
    const auto publish = [&](LiveReferencePredictionSnapshot snapshot) {
      // Local commands and control-only server publications can change pose or
      // continuity before another clientdata ACK reaches the rebase owner.
      if (prediction_server_velocity_visual_ &&
          (!prediction_history_ || !prediction_server_velocity_pose_ ||
           !compatible_velocity_correction_pose(*prediction_server_velocity_pose_,
               *prediction_history_->current_predicted_state()) ||
           !velocity_correction_context_continuous(prediction_server_velocity_entity_)))
        prediction_visual_correction_.reset();
      if(!prediction_visual_correction_ || !prediction_visual_correction_->active()) {
        prediction_exterior_correction_source_.reset();
        prediction_server_velocity_visual_ = false;
        prediction_server_velocity_pose_.reset();
      }
      if (prediction_visual_correction_ &&
          prediction_visual_correction_->active() &&
          snapshot.presented_origin && snapshot.presented_view_offset &&
          prediction_camera_collision_query_ && prediction_collision_ &&
          prediction_history_) {
        const auto base = *snapshot.presented_origin;
        const auto offset = *snapshot.presented_view_offset;
        gameplay_camera::GameplayCameraStateCreateInfo camera_info;
        camera_info.position = {base.x + offset.x, base.y + offset.y,
                                base.z + offset.z};
        camera_info.mode = gameplay_camera::GameplayCameraMode::player_walk;
        camera_info.revision = std::max<std::uint64_t>(1U, prediction_epoch_);
        const auto camera = gameplay_camera::GameplayCameraState::create(camera_info);
        const auto scratch_before = prediction_presentation_scratch_.retained_bytes();
        if (camera && camera.state) {
          const auto sample = prediction::sample_prediction_visual_correction(
              *prediction_visual_correction_, *camera.state,
              std::chrono::duration<double>{now.time_since_epoch()}.count(),
              prediction_camera_collision_query_.get(),
              prediction_presentation_scratch_,
              prediction_movement_config_.collision_query.query_limits);
          prediction_presentation_trace_queries_ += 2U;
          if (sample && sample.camera && sample.correction) {
            const auto& eye = sample.camera->position();
            const assets::AssetVector3 candidate{eye.x - offset.x,
                                                 eye.y - offset.y,
                                                 eye.z - offset.z};
            const auto checked = (presentation_collision ? presentation_collision.get() : prediction_collision_.get())->trace_hull(
                base, candidate,
                prediction_history_->current_predicted_state()->hull(),
                prediction_presentation_scratch_,
                prediction_movement_config_.collision_query);
            ++prediction_presentation_trace_queries_;
            if(config_.movement_diagnostics && snapshot.collision_query_purpose!="present_command_body" && checked && checked.result &&
                (checked.result->start_solid || checked.result->all_solid || checked.result->fraction<1.0)) {
              snapshot.collision_position_evidence.reset();
              snapshot.collision_trace_evidence=checked.result;
              snapshot.collision_query_hull=prediction_history_->current_predicted_state()->hull();
              snapshot.collision_query_start=base;
              snapshot.collision_query_end=candidate;
              snapshot.collision_query_purpose="present_linear_body";
            }
            const auto correction_source=prediction_visual_correction_->source_authority_update_ordinal();
            const auto sample_seconds=std::chrono::duration<double>{now.time_since_epoch()}.count();
            const bool route_selected=prediction_exterior_correction_source_==correction_source &&
                sample_seconds<prediction_visual_correction_->start_monotonic_time_seconds()+
                    prediction_visual_correction_->duration_seconds();
            bool presented=false;
            if (!route_selected && checked && checked.result && !checked.result->start_solid &&
                !checked.result->all_solid && checked.result->fraction >= 1.0 &&
                prediction_eye_clear(camera_info.position, eye, presentation_scene)) {
              snapshot.presented_origin = candidate;
              snapshot.presentation_reason = prediction_server_velocity_visual_
                  ? "visual_server_velocity_correction" : "visual_correction";
              prediction_visual_correction_.emplace(std::move(*sample.correction));
              prediction_exterior_correction_source_.reset();presented=true;
            } else if(!prediction_server_velocity_visual_ && prediction_history_->current_predicted_state()->mode()==
                hlclient::movement::PlayerMovementMode::walking) {
              const auto& visual=*prediction_visual_correction_;
              const auto initial=visual.initial_position_offset();
              const assets::AssetVector3 route_from{base.x+initial.x,base.y+initial.y,base.z+initial.z};
              const auto effective_seconds=std::max(sample_seconds,visual.last_sample_monotonic_time_seconds());
              const auto alpha=visual.duration_seconds()>0 ? std::clamp((effective_seconds-
                  visual.start_monotonic_time_seconds())/visual.duration_seconds(),0.0,1.0):1.0;
              const auto hull=prediction_history_->current_predicted_state()->hull();
              const auto ordinal=movement::local_movement_collision_hull(hull);
              const auto profile=ordinal?hlclient::collision::standard_collision_hull_profile(*ordinal):std::nullopt;
              const bool eye_inside=profile && offset.x>=profile->clip_mins.x&&offset.x<=profile->clip_maxs.x&&
                  offset.y>=profile->clip_mins.y&&offset.y<=profile->clip_maxs.y&&
                  offset.z>=profile->clip_mins.z&&offset.z<=profile->clip_maxs.z;
              const auto route=eye_inside ? sample_reference_exterior_corner_presentation(route_from,base,alpha,hull,
                  *(presentation_collision?presentation_collision.get():prediction_collision_.get()),
                  prediction_presentation_scratch_,prediction_movement_config_,4.0,&prediction_presentation_trace_queries_):std::nullopt;
              if(route && prediction_epoch_>=visual.new_prediction_revision()) {
                const assets::AssetVector3 route_eye_from{route->from.x+offset.x,route->from.y+offset.y,route->from.z+offset.z};
                const assets::AssetVector3 bend_eye{route->bend.x+offset.x,route->bend.y+offset.y,route->bend.z+offset.z};
                const assets::AssetVector3 route_eye{route->sampled_origin.x+offset.x,route->sampled_origin.y+offset.y,route->sampled_origin.z+offset.z};
                if(prediction_eye_clear(route_eye_from,bend_eye,presentation_scene) &&
                    prediction_eye_clear(bend_eye,camera_info.position,presentation_scene)) {
                  auto routed=prediction::publish_validated_prediction_visual_camera(visual,*camera.state,route_eye,
                      effective_seconds,visual.new_prediction_revision(),correction_source,4.0);
                  if(routed&&routed.camera&&routed.correction) {
                    snapshot.presented_origin=route->sampled_origin;
                    snapshot.presentation_reason="visual_correction_exterior_corner";
                    prediction_visual_correction_.emplace(std::move(*routed.correction));
                    prediction_exterior_correction_source_=correction_source;presented=true;
                  }
                }
              }
            }
            if(!presented) {
              snapshot.presentation_reason = "visual_correction_blocked";
              prediction_visual_correction_.reset();
              prediction_exterior_correction_source_.reset();
            }
          } else {
            snapshot.presentation_reason = "visual_correction_failed";
            prediction_visual_correction_.reset();
            prediction_exterior_correction_source_.reset();
          }
        } else {
          prediction_visual_correction_.reset();
          prediction_exterior_correction_source_.reset();
        }
        if (prediction_presentation_scratch_.retained_bytes() > scratch_before)
          ++prediction_presentation_scratch_growths_;
        snapshot.presentation_scratch_bytes =
            prediction_presentation_scratch_.retained_bytes();
      }
      snapshot.presentation_trace_queries_total =
          prediction_presentation_trace_queries_;
      snapshot.presentation_scratch_growths_total =
          prediction_presentation_scratch_growths_;
      snapshot.presentation_cpu_ms = std::chrono::duration<double, std::milli>{
          std::chrono::steady_clock::now() - work_started}.count();
      prediction_presentation_cache_at_ = now;
      prediction_presentation_cache_history_ = prediction_history_.get();
      prediction_presentation_cache_world_revision_ = world_revision;
      prediction_presentation_cache_epoch_ = prediction_epoch_;
      prediction_presentation_cache_state_ = prediction_state_;
      prediction_presentation_cache_ = snapshot;
      return snapshot;
    };
    LiveReferencePredictionSnapshot out;
    // A stationary door is still an authoritative effect blocker. Motion
    // samples below may replace this pose, but zero derived pusher velocities
    // must not erase the committed brush scene from normal presentation.
    out.presented_brush_scene = prediction_brush_context_.scene;
    if (prediction_support_motion_ && prediction_support_motion_->active())
      out.support_policy = "derived_vertical_support_bounded_250ms_v1";
    out.collision_context_revision = prediction_brush_context_.revision;
    out.collision_brush_count = prediction_brush_context_.solid_count;
    out.collision_excluded_point_boxes = prediction_brush_context_.excluded_point_box_count;
    out.server_brush_transform_changes = prediction_brush_transform_changes_;
    out.last_changed_brush_entity=prediction_last_changed_brush_entity_;
    out.last_changed_brush_model=prediction_last_changed_brush_model_;
    out.step_selections = prediction_step_selections_;
    out.last_fallback_reason = prediction_last_fallback_reason_;
    if (prediction_history_) {
      const auto& ground = prediction_history_->current_predicted_state()->ground_state();
      out.local_grounded = ground.grounded();
      out.ground_hit = ground.hit();
      if (ground.grounded()) out.ground_normal = ground.plane().normal;
    }
    if (const auto& observation = target_.runtime_observation(); observation) {
      if (observation->receiving_client && observation->receiving_client->flags)
        out.server_grounded = (*observation->receiving_client->flags & (1U << 9U)) != 0U;
      for (const auto& entity : observation->packet_entities) if (entity.entity_number == client_slot_ + 1U && entity.base_velocity.complete()) {
        out.base_velocity_status = *entity.base_velocity.x == 0.0 && *entity.base_velocity.y == 0.0 && *entity.base_velocity.z == 0.0 ? "observed_zero" : "nonzero_unsupported";
      }
    }
    out.state = prediction_state_;
    out.reason = prediction_reason_;
    out.generation = kLiveRuntimeGeneration;
    out.prediction_epoch = prediction_epoch_;
    out.local_steps = prediction_local_steps_;
    out.accepted_corrections = prediction_accepted_corrections_;
    out.replayed_commands = prediction_replayed_commands_;
    out.fallback_count = prediction_fallback_count_;
    out.history_depth = prediction_history_ ? prediction_history_->size() : 0U;
    out.last_raw_position_error = prediction_last_error_;
    out.maximum_raw_position_error = prediction_maximum_error_;
    out.last_camera_correction_jump = prediction_last_camera_correction_jump_;
    out.maximum_camera_correction_jump =
        prediction_maximum_camera_correction_jump_;
    out.last_record_identity = prediction_last_record_identity_;
    out.anchor_command = prediction_last_command_boundary_;
    out.last_seed_status = prediction_last_seed_status_;
    out.last_seed_field = prediction_last_seed_field_;
    out.last_ground_status = prediction_last_ground_status_;
    if (config_.movement_diagnostics) {
      out.support_observation_seconds = prediction_brush_context_.server_time;
      out.collision_position_evidence = prediction_collision_position_evidence_;
      out.collision_trace_evidence = prediction_collision_trace_evidence_;
      out.collision_query_hull = prediction_collision_query_hull_;
      out.collision_query_start = prediction_collision_query_start_;
      out.collision_query_end = prediction_collision_query_end_;
      out.seed_origin = prediction_seed_origin_;
      out.previous_valid_origin = prediction_previous_valid_origin_;
      out.rebase_reason = prediction_rebase_reason_;
      out.last_simulation_error = prediction_last_simulation_error_;
      out.matched_origin_before_correction = prediction_matched_origin_before_correction_;
      out.matched_velocity_before_correction = prediction_matched_velocity_before_correction_;
      out.last_anchor_status = prediction_last_anchor_status_;
      out.anchor_coherent_reliable_frame = prediction_anchor_coherent_reliable_frame_;
      out.collision_query_purpose = prediction_collision_query_purpose_;
      out.fallback_duration_ms = prediction_last_fallback_duration_ms_;
      if (prediction_fallback_started_at_) {
        out.fallback_started_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            prediction_fallback_started_at_->time_since_epoch()).count();
        out.fallback_duration_ms = std::max(0.0, std::chrono::duration<double, std::milli>{
            now - *prediction_fallback_started_at_}.count());
      }
    }
    if (prediction_collision_)
      out.collision_identity = prediction_collision_->session_identity();
    if (const auto& observed = target_.runtime_observation(); observed &&
        observed->receiving_client) {
      const auto& origin = observed->receiving_client->origin;
      if (origin.complete())
        out.canonical_origin = assets::AssetVector3{
            static_cast<float>(*origin.x), static_cast<float>(*origin.y),
            static_cast<float>(*origin.z)};
      if(config_.movement_diagnostics) {
        const auto& velocity=observed->receiving_client->velocity;
        if(velocity.complete())out.canonical_velocity=assets::AssetVector3{
            static_cast<float>(*velocity.x),static_cast<float>(*velocity.y),static_cast<float>(*velocity.z)};
        if(observed->client_metadata.source) {
          const auto& source=*observed->client_metadata.source;
          out.canonical_source_record_identity=source.record_identity;
          out.canonical_source_sequence=source.source_transport_sequence;
          out.canonical_body_kind=source.contemporaneous_fragment_suffix ? "fragment_suffix" :
              source.reassembled ? "reassembled_body" : source.source_reliable ? "reliable_body" : "direct_body";
        }
      }
    }
    if (prediction_state_ != LiveReferencePredictionState::active ||
        !prediction_history_)
      return publish(out);
    const auto& latest = prediction_history_->current_predicted_state();
    if (config_.movement_diagnostics) {
      out.command_simulation_seconds = static_cast<double>(latest->simulation_time_nanoseconds()) * 1e-9;
      out.render_sample_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
          (now - config_.usercmd_scenario.command_interval).time_since_epoch()).count();
      if (prediction_support_motion_ && latest->ground_state().hit())
        out.support_velocity_z = prediction_support_motion_->velocity_z_for(*latest->ground_state().hit());
    }
    out.predicted_origin = latest->origin();
    out.predicted_velocity = latest->velocity();
    out.predicted_view_offset = latest->view_offset();
    out.predicted_hull = latest->hull();
    out.predicted_mode = latest->mode();
    out.predicted_old_buttons = latest->old_buttons();
    out.predicted_duck_time_milliseconds =
        latest->duck_time_milliseconds();
    out.predicted_in_duck_transition = latest->in_duck_transition();
    out.presented_origin = latest->origin();
    out.presented_view_offset = latest->view_offset();
    if (prediction_support_motion_ && prediction_support_motion_->active()) {
      presentation_scene = prediction_support_motion_->sample(
          static_cast<double>(latest->simulation_time_nanoseconds()) * 1e-9);
      out.presented_brush_scene = presentation_scene;
      presentation_collision = std::make_unique<movement::BrushSceneMovementCollision>(presentation_scene,prediction_brush_context_.boxes);
    }
    out.presentation_reason = "latest_no_pair";
    out.presentation_scratch_bytes = prediction_presentation_scratch_.retained_bytes();
    if (!usercmd_scheduler_ || !prediction_collision_)
      return publish(out);
    const auto scheduler = usercmd_scheduler_->state();
    const auto interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
        config_.usercmd_scenario.command_interval);
    if (!scheduler.initialized || scheduler.next_command_sequence <= 1U ||
        interval.count() <= 0)
      return publish(out);
    const auto newest_sequence = scheduler.next_command_sequence - 1U;
    const auto command_time = [&](const std::uint32_t sequence)
        -> std::optional<LiveRuntimeStageTimePoint> {
      if (sequence == 0U || sequence > newest_sequence ||
          newest_sequence - sequence > 128U)
        return std::nullopt;
      const auto& sampled = command_sample_times_[sequence % command_sample_times_.size()];
      if (sampled.sequence != sequence)
        return std::nullopt;
      return LiveRuntimeStageTimePoint{std::chrono::nanoseconds{sampled.end_nanoseconds}};
    };
    const auto sample_at = now - config_.usercmd_scenario.command_interval;
    const auto& anchor = prediction_history_->anchor().movement_state();
    auto from_state = anchor;
    auto from_sequence = anchor->source_command_sequence();
    auto from_at = command_time(from_sequence);
    if (!from_at)
      return publish(out);
    if (sample_at < *from_at) {
      if (prediction_pre_anchor_ && prediction_pre_anchor_->source_command_sequence()+1U == from_sequence) {
        from_state = prediction_pre_anchor_;
        from_sequence = from_state->source_command_sequence();
        from_at = command_time(from_sequence);
      } else {
        out.presented_origin = anchor->origin();
        out.presentation_reason = "anchor_before_history";
        return publish(out);
      }
    }
    // Include the retained predecessor -> corrected anchor pair when rebase
    // consumed the newest command. It is presentation-only, never replayed.
    if (!from_at) return publish(out);
    const std::size_t include_anchor=from_sequence < anchor->source_command_sequence() ? 1U : 0U;
    const auto entries=prediction_history_->entries();
    for (std::size_t i=0; i<entries.size()+include_anchor; ++i) {
      const auto& to_state=include_anchor && i==0 ? anchor : entries[i-include_anchor].post_command_state();
      const auto to_sequence = to_state->source_command_sequence();
      const auto to_at = command_time(to_sequence);
      if (!to_at || *to_at <= *from_at || *to_at - *from_at < interval) {
        out.presentation_reason = "invalid_command_timeline";
        return publish(out);
      }
      if (sample_at > *to_at) {
        from_state = to_state;
        from_sequence = to_sequence;
        from_at = to_at;
        continue;
      }
      if (*to_at - *from_at > interval) {
        // Scheduler recovery intentionally skipped unsampled wall time while
        // preserving contiguous immutable command IDs. There is no simulated
        // trajectory across that gap: publish its validated endpoint, retain
        // the exact deadlines, and resume interpolation at the next real pair.
        out.presentation_from_command = from_sequence;
        out.presentation_to_command = to_sequence;
        out.presentation_from_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            from_at->time_since_epoch()).count();
        out.presentation_to_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            to_at->time_since_epoch()).count();
        out.presented_origin = to_state->origin();
        out.presented_view_offset = to_state->view_offset();
        out.presentation_reason = "command_wall_time_gap";
        return publish(out);
      }
      const bool hull_transition = from_state->hull() != to_state->hull();
      const auto duration = std::chrono::duration<double>{*to_at - *from_at}.count();
      const auto fraction = std::clamp(
          std::chrono::duration<double>{sample_at - *from_at}.count() /
              duration, 0.0, 1.0);
      if (!std::isfinite(fraction)) {
        out.presentation_reason = "invalid_alpha";
        return publish(out);
      }
      out.presentation_from_command = from_sequence;
      out.presentation_to_command = to_sequence;
      out.presentation_from_time_ns = std::chrono::duration_cast<
          std::chrono::nanoseconds>(from_at->time_since_epoch()).count();
      out.presentation_to_time_ns = std::chrono::duration_cast<
          std::chrono::nanoseconds>(to_at->time_since_epoch()).count();
      out.presentation_alpha = fraction;
      const auto& from = from_state->origin();
      const auto& to = to_state->origin();
      const auto& from_offset = from_state->view_offset();
      const auto& to_offset = to_state->view_offset();
      const auto support_sample = sample_reference_support_presentation(*from_state,*to_state,fraction,
          prediction_support_motion_ ? &*prediction_support_motion_ : nullptr,
          prediction_presentation_scratch_,prediction_movement_config_.collision_query);
      if (support_sample.scene) {
        presentation_scene = support_sample.scene;
        out.presented_brush_scene = presentation_scene;
        presentation_collision = std::make_unique<movement::BrushSceneMovementCollision>(presentation_scene,prediction_brush_context_.boxes);
      }
      if (hull_transition) {
        if (fraction >= 1.0) {
          out.presented_origin = to;
          out.presented_view_offset = to_offset;
          out.presentation_reason = "hull_transition_endpoint";
          return publish(out);
        }
        // Valve's completed ground duck changes origin and hull together.
        // Before that boundary retain the verified old hull/origin and sample
        // only its eye path. Unduck is a discrete clearance event.
        out.presented_origin = support_sample.trace_start;
        out.presented_view_offset = from_offset;
        out.presentation_reason = "hull_transition_discrete";
        if (from_state->hull() == hlclient::movement::PlayerMovementHull::standing &&
            to_state->hull() == hlclient::movement::PlayerMovementHull::ducked &&
            from_state->ground_state().grounded() &&
            prediction_camera_collision_query_) {
          const assets::AssetVector3 old_eye{
              from.x + from_offset.x, from.y + from_offset.y,
              from.z + from_offset.z};
          const assets::AssetVector3 new_eye{
              to.x + to_offset.x, to.y + to_offset.y,
              to.z + to_offset.z};
          const assets::AssetVector3 eye{
              static_cast<float>(old_eye.x + (new_eye.x - old_eye.x) * fraction),
              static_cast<float>(old_eye.y + (new_eye.y - old_eye.y) * fraction),
              static_cast<float>(old_eye.z + (new_eye.z - old_eye.z) * fraction)};
          hlclient::collision::CollisionTraceRequest request;
          request.start = old_eye;
          request.end = eye;
          request.limits = prediction_movement_config_.collision_query.query_limits;
          ++prediction_presentation_trace_queries_;
          const auto clear = prediction_camera_collision_query_->trace_line(
              request, prediction_presentation_scratch_);
          if (clear && clear.result && !clear.result->start_solid &&
              !clear.result->all_solid && clear.result->fraction >= 1.0 &&
              prediction_eye_clear(old_eye, eye, presentation_scene)) {
            out.presented_view_offset = assets::AssetVector3{
                eye.x - from.x, eye.y - from.y, eye.z - from.z};
            out.presentation_reason = "duck_eye_transition";
          } else {
            out.presentation_reason = "duck_eye_transition_blocked";
          }
        }
        return publish(out);
      }
    const auto candidate = support_sample.origin;
      if (fraction == 0.0 || fraction == 1.0) {
        out.presented_origin = candidate;
        out.presented_view_offset = fraction == 0.0 ? from_offset : to_offset;
        out.presentation_reason = "pair_endpoint";
        return publish(out);
      }
    try {
      const auto scratch_before = prediction_presentation_scratch_.retained_bytes();
      // A legal physics step onto a server box has a non-linear hull route.
      // Use that checked route while its eye continues at command-time alpha;
      // a late blocked straight chord must not roll the camera back mid-pair.
      const auto step_eye = prediction_environment_ ? sample_reference_box_step_eye(
          *from_state, *to_state, fraction, prediction_environment_->step_size(),
          *(presentation_collision ? presentation_collision.get()
                                  : prediction_collision_.get()),
          prediction_presentation_scratch_, prediction_movement_config_) : std::nullopt;
      if (step_eye && prediction_camera_collision_query_) {
        const assets::AssetVector3 old_eye{
            from.x + from_offset.x, from.y + from_offset.y, from.z + from_offset.z};
        const auto& origin = step_eye->origin;
        const auto& offset = step_eye->view_offset;
        const assets::AssetVector3 eye{
            origin.x + offset.x, origin.y + offset.y, origin.z + offset.z};
        hlclient::collision::CollisionTraceRequest request;
        request.start = old_eye;
        request.end = eye;
        request.limits = prediction_movement_config_.collision_query.query_limits;
        ++prediction_presentation_trace_queries_;
        const auto checked = prediction_camera_collision_query_->trace_line(
            request, prediction_presentation_scratch_);
        if (checked && checked.result && !checked.result->start_solid &&
            !checked.result->all_solid && checked.result->fraction >= 1.0 &&
            prediction_eye_clear(old_eye, eye, presentation_scene)) {
          out.presented_origin = origin;
          out.presented_view_offset = offset;
          out.presentation_reason = "box_step_eye";
          if (prediction_presentation_scratch_.retained_bytes() > scratch_before)
            ++prediction_presentation_scratch_growths_;
          out.presentation_scratch_bytes = prediction_presentation_scratch_.retained_bytes();
          return publish(out);
        }
      }
      ++prediction_presentation_trace_queries_;
      const auto clear = (presentation_collision ? presentation_collision.get() : prediction_collision_.get())->trace_hull(
          support_sample.trace_start, candidate, to_state->hull(), prediction_presentation_scratch_,
          prediction_movement_config_.collision_query);
      // Retain the first blocked presentation operation, before any recovery
      // or subsequent visual-correction query can replace its evidence.
      if(config_.movement_diagnostics && clear && clear.result &&
          (clear.result->start_solid || clear.result->all_solid || clear.result->fraction<1.0)) {
        out.collision_position_evidence.reset();
        out.collision_trace_evidence=clear.result;
        out.collision_query_hull=to_state->hull();
        out.collision_query_start=support_sample.trace_start;
        out.collision_query_end=candidate;
        out.collision_query_purpose="present_command_body";
      }
      if (prediction_presentation_scratch_.retained_bytes() > scratch_before)
        ++prediction_presentation_scratch_growths_;
      if (clear && clear.result && !clear.result->start_solid &&
          !clear.result->all_solid && clear.result->fraction >= 1.0) {
        out.presented_origin = candidate;
        out.presented_view_offset = assets::AssetVector3{
            static_cast<float>(from_offset.x +
                (to_offset.x - from_offset.x) * fraction),
            static_cast<float>(from_offset.y +
                (to_offset.y - from_offset.y) * fraction),
            static_cast<float>(from_offset.z +
                (to_offset.z - from_offset.z) * fraction)};
        out.presentation_reason = "interpolated";
      } else {
        const auto recovered = clear && clear.result
            ? recover_reference_ground_presentation(*from_state,*to_state,fraction,
                *clear.result,*(presentation_collision ? presentation_collision.get() : prediction_collision_.get()),prediction_presentation_scratch_,
                prediction_movement_config_)
            : std::nullopt;
        if (recovered) {
          out.presented_origin = *recovered;
          out.presented_view_offset = assets::AssetVector3{
              static_cast<float>(from_offset.x + (to_offset.x-from_offset.x)*fraction),
              static_cast<float>(from_offset.y + (to_offset.y-from_offset.y)*fraction),
              static_cast<float>(from_offset.z + (to_offset.z-from_offset.z)*fraction)};
          out.presentation_reason = "ground_seam";
        } else {
          out.presented_origin = from;
          out.presented_view_offset = from_offset;
          out.presentation_reason = "collision_blocked";
        }
      }
    } catch (const std::exception&) {
      out.presented_origin = from;
      out.presented_view_offset = from_offset;
      out.presentation_reason = "collision_query_failed";
    }
      out.presentation_scratch_bytes = prediction_presentation_scratch_.retained_bytes();
      return publish(out);
    }
    return publish(out);
  }

  [[nodiscard]] std::size_t use_new_submission_count() const noexcept { return use_new_submission_count_; }
  [[nodiscard]] std::optional<LiveRuntimeStageEvent> poll_event() {
    if (event_size_ == 0U) {
      return std::nullopt;
    }
    auto event = std::move(event_slots_[event_head_]);
    event_slots_[event_head_].reset();
    event_head_ = (event_head_ + 1U) % event_slots_.size();
    --event_size_;
    return event;
  }

  [[nodiscard]] std::optional<LiveWeaponCommandSubmission>
  poll_weapon_command_submission() {
    if (weapon_command_submissions_.empty()) return std::nullopt;
    auto result = weapon_command_submissions_.front();
    weapon_command_submissions_.pop_front();
    return result;
  }

  void synchronize_from_response(const LiveRuntimeStageTimePoint now) {
    const bool has_boundary =
        response_stage_.state() ==
        ResourceClientResponseStageState::next_server_boundary_reached;
    const bool ack_completion =
        response_stage_.state() ==
        ResourceClientResponseStageState::response_completion_ready;
    if (!has_boundary && !ack_completion) {
      if (response_stage_.terminal() || response_stage_.error()) {
        fail_from_response(now, false);
      }
      return;
    }
    if (!response_stage_.result()) {
      fail(LiveRuntimeStageErrorCode::initialization_context_missing,
           LiveRuntimeStageState::protocol_error,
           "Resource response completed without owning sign-on context", now);
      return;
    }
    auto *const driver = response_stage_.retained_driver();
    const auto *const retained = response_stage_.retained_source_payload();
    if (driver == nullptr) {
      fail(LiveRuntimeStageErrorCode::retained_driver_missing,
           LiveRuntimeStageState::protocol_error,
           "Resource response did not retain its persistent driver", now);
      return;
    }
    if (has_boundary && retained == nullptr) {
      fail(LiveRuntimeStageErrorCode::retained_payload_missing,
           LiveRuntimeStageState::protocol_error,
           "Resource response did not retain its owning source payload", now);
      return;
    }
    try {
      schemas_ = std::make_shared<const DeltaSchemaRegistryState>(
          delta_registry_from(*response_stage_.result()));
      user_messages_ = user_messages_from(*response_stage_.result());
      const auto &server_info = server_info_from(*response_stage_.result());
      if(config_.required_game_directory && server_info.game_directory()!=*config_.required_game_directory) {
        fail(LiveRuntimeStageErrorCode::unsupported_game_profile,
             LiveRuntimeStageState::protocol_error,
             "Server game directory is incompatible with the selected application profile",now);
        return;
      }
      protocol_ = static_cast<std::uint32_t>(server_info.protocol_version());
      server_count_ = server_info.server_count();
      world_map_crc_ = server_info.world_map_crc();
      client_slot_ = server_info.client_slot();
      max_clients_ = server_info.maximum_clients().value();
      game_directory_ = server_info.game_directory();
      map_file_path_ = server_info.map_file_path();

      const auto &initial_source = response_stage_.result()
                                       ->resource_list()
                                       .transition()
                                       .user_info()
                                       .movement_environment()
                                       .delta_description()
                                       .pre_resource()
                                       .source_payload();
      const auto &transition_source = response_stage_.result()
                                          ->resource_list()
                                          .transition()
                                          .source_payload();
      initial_service_wire_uncompressed_ = initial_source.wire_uncompressed();
      transition_service_wire_uncompressed_ =
          transition_source.wire_uncompressed();
      if (!observe_service_encoding(initial_source.decompressed(),
                                    initial_source.wire_uncompressed()) ||
          !observe_service_encoding(transition_source.decompressed(),
                                    transition_source.wire_uncompressed())) {
        fail(
            LiveRuntimeStageErrorCode::initialization_context_missing,
            LiveRuntimeStageState::protocol_error,
            "Nested live sign-on payload has no valid wire encoding provenance",
            now);
        return;
      }
    } catch (const std::bad_alloc &) {
      fail(LiveRuntimeStageErrorCode::initialization_context_missing,
           LiveRuntimeStageState::protocol_error,
           "Unable to retain live runtime initialization context", now);
      return;
    }
    if (!schemas_ || schemas_->schema_count() == 0U || max_clients_ == 0U) {
      fail(LiveRuntimeStageErrorCode::initialization_context_missing,
           LiveRuntimeStageState::protocol_error,
           "Live runtime initialization context is incomplete", now);
      return;
    }

    const auto event = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::resource_response_ready,
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
    state_ = LiveRuntimeStageState::waiting_for_baselines;

    if (has_boundary) {
      auto payload = *retained;
      const auto boundary = response_stage_.result()->boundary();
      response_stage_.release_retained_source_payload();
      auto cursor = StockRuntimeSourceCursor::create(
          boundary.byte_offset(), boundary.bit_offset(), payload.bytes.size());
      if (!cursor) {
        fail(LiveRuntimeStageErrorCode::initialization_context_missing,
             LiveRuntimeStageState::protocol_error,
             "Post-resource cursor is outside its owning payload", now);
        return;
      }
      process_service_payload(std::move(payload), *cursor, now);
      if (terminal_state(state_)) {
        return;
      }
    }
    queue_spawn_request(*driver, now);
  }

  void queue_spawn_request(NetchanDriver &driver,
                           const LiveRuntimeStageTimePoint now) {
    auto built = StockSpawnRequestBuilder::build(server_count_, world_map_crc_);
    if (!built || !built.encoding) {
      fail(LiveRuntimeStageErrorCode::spawn_request_build_failed,
           LiveRuntimeStageState::protocol_error,
           built.error ? std::string_view{built.error->context}
                       : std::string_view{"Typed stock spawn builder returned "
                                          "no request"},
           now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           built.error ? std::optional{built.error->code} : std::nullopt);
      return;
    }
    spawn_request_.emplace(std::move(*built.encoding));
    NetchanDriverOperationResult queued;
    try {
      queued = driver.queue_reliable(spawn_request_->semantic_bytes());
    } catch (...) {
      fail(LiveRuntimeStageErrorCode::spawn_request_queue_failed,
           LiveRuntimeStageState::network_error,
           "Retained driver threw while queueing stock spawn", now);
      return;
    }
    if (!queued) {
      fail(LiveRuntimeStageErrorCode::spawn_request_queue_failed,
           LiveRuntimeStageState::protocol_error,
           queued.error
               ? std::string_view{queued.error->context}
               : std::string_view{"Retained driver rejected typed stock spawn"},
           now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           std::nullopt,
           queued.error ? std::optional{queued.error->code} : std::nullopt);
      return;
    }
    ++spawn_request_queue_count_;
    const auto event = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::spawn_request_queued,
        .payload_byte_count = spawn_request_->semantic_bytes().size(),
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
  }

  void observe_spawn_request_transmit(const LiveRuntimeStageTimePoint now) {
    if (!spawn_request_ || spawn_request_acknowledged_) {
      return;
    }
    auto *const driver = response_stage_.retained_driver();
    if (driver == nullptr) {
      return;
    }
    const auto &in_flight = driver->session().in_flight_reliable_payload();
    const auto &transfer = driver->session().outgoing_fragment_transfer();
    if (!in_flight || !transfer) {
      return;
    }
    if (!std::ranges::equal(in_flight->bytes,
                            spawn_request_->semantic_bytes()) ||
        !std::ranges::equal(transfer->canonical_bytes,
                            spawn_request_->semantic_bytes()) ||
        !transfer->transfer_id.valid()) {
      fail(LiveRuntimeStageErrorCode::spawn_request_transmit_mismatch,
           LiveRuntimeStageState::protocol_error,
           "Unexpected reliable payload became in-flight before stock spawn",
           now);
      return;
    }
    if (spawn_request_transmitted_) {
      if (!spawn_reliable_generation_ ||
          *spawn_reliable_generation_ != transfer->transfer_id.value() ||
          !spawn_first_transmit_sequence_ ||
          *spawn_first_transmit_sequence_ != in_flight->first_sent_sequence ||
          spawn_reliable_toggle_ != in_flight->toggle ||
          in_flight->send_count < spawn_transmit_count_) {
        fail(LiveRuntimeStageErrorCode::spawn_request_transmit_mismatch,
             LiveRuntimeStageState::protocol_error,
             "Stock spawn reliable generation changed outside driver "
             "retransmission",
             now);
        return;
      }
      spawn_most_recent_transmit_sequence_ =
          in_flight->most_recent_sent_sequence;
      spawn_transmit_count_ = in_flight->send_count;
      return;
    }
    spawn_request_transmitted_ = true;
    spawn_reliable_generation_ = transfer->transfer_id.value();
    spawn_reliable_toggle_ = in_flight->toggle;
    spawn_first_transmit_sequence_ = in_flight->first_sent_sequence;
    spawn_most_recent_transmit_sequence_ = in_flight->most_recent_sent_sequence;
    spawn_transmit_count_ = in_flight->send_count;
    const auto event = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::spawn_request_transmitted,
        .payload_byte_count = spawn_request_->semantic_bytes().size(),
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
  }

  void queue_signon_reply(NetchanDriver &driver,
                          const LiveRuntimeStageTimePoint now) {
    if (signon_reply_) {
      return;
    }
    signon_reply_.emplace(StockSendEntitiesRequestBuilder::build());
    NetchanDriverOperationResult queued;
    try {
      queued = driver.queue_reliable(signon_reply_->semantic_bytes());
    } catch (...) {
      fail(LiveRuntimeStageErrorCode::signon_reply_queue_failed,
           LiveRuntimeStageState::network_error,
           "Retained driver threw while queueing typed sendents reply", now);
      return;
    }
    if (!queued) {
      fail(LiveRuntimeStageErrorCode::signon_reply_queue_failed,
           LiveRuntimeStageState::protocol_error,
           queued.error ? std::string_view{queued.error->context}
                        : std::string_view{"Retained driver rejected typed "
                                           "sendents reply"},
           now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           std::nullopt,
           queued.error ? std::optional{queued.error->code} : std::nullopt);
      return;
    }
    ++signon_reply_queue_count_;
    const auto event = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::signon_reply_queued,
        .payload_byte_count = signon_reply_->semantic_bytes().size(),
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
  }

  void observe_signon_reply_transmit(const LiveRuntimeStageTimePoint now) {
    if (!signon_reply_ || signon_reply_acknowledged_) {
      return;
    }
    auto *const driver = response_stage_.retained_driver();
    if (driver == nullptr) {
      return;
    }
    const auto &in_flight = driver->session().in_flight_reliable_payload();
    const auto &transfer = driver->session().outgoing_fragment_transfer();
    if (!in_flight || !transfer) {
      return;
    }
    if (!std::ranges::equal(in_flight->bytes,
                            signon_reply_->semantic_bytes()) ||
        !std::ranges::equal(transfer->canonical_bytes,
                            signon_reply_->semantic_bytes()) ||
        !transfer->transfer_id.valid()) {
      fail(LiveRuntimeStageErrorCode::signon_reply_transmit_mismatch,
           LiveRuntimeStageState::protocol_error,
           "Unexpected reliable payload became in-flight before typed sendents "
           "reply",
           now);
      return;
    }
    if (signon_reply_transmitted_) {
      if (!signon_reply_reliable_generation_ ||
          *signon_reply_reliable_generation_ != transfer->transfer_id.value() ||
          !signon_reply_first_transmit_sequence_ ||
          *signon_reply_first_transmit_sequence_ !=
              in_flight->first_sent_sequence ||
          signon_reply_reliable_toggle_ != in_flight->toggle ||
          in_flight->send_count < signon_reply_transmit_count_) {
        fail(LiveRuntimeStageErrorCode::signon_reply_transmit_mismatch,
             LiveRuntimeStageState::protocol_error,
             "Typed sendents reliable generation changed outside driver "
             "retransmission",
             now);
        return;
      }
      signon_reply_most_recent_transmit_sequence_ =
          in_flight->most_recent_sent_sequence;
      signon_reply_transmit_count_ = in_flight->send_count;
      return;
    }
    signon_reply_transmitted_ = true;
    signon_reply_reliable_generation_ = transfer->transfer_id.value();
    signon_reply_reliable_toggle_ = in_flight->toggle;
    signon_reply_first_transmit_sequence_ = in_flight->first_sent_sequence;
    signon_reply_most_recent_transmit_sequence_ =
        in_flight->most_recent_sent_sequence;
    signon_reply_transmit_count_ = in_flight->send_count;
    const auto event = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::signon_reply_transmitted,
        .payload_byte_count = signon_reply_->semantic_bytes().size(),
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
  }

  void process_netchan_payload(OwnedNetchanPayload payload,
                               const LiveRuntimeStageTimePoint now) {
    if (payload.bytes.empty() ||
        (payload.bytes.size() <= 8U &&
         std::ranges::all_of(payload.bytes, [](const std::byte value) {
           return value == std::byte{0U};
         }))) {
      return;
    }
    if (config_.movement_diagnostics) {
      movement_diagnostics_.socket_read_begin_ns = payload.socket_read_begin_ns;
      movement_diagnostics_.socket_read_end_ns = payload.socket_read_end_ns;
      movement_diagnostics_.parse_begin_ns = actual_monotonic_ns();
      movement_diagnostics_.source_body_kind=payload.body_kind==NetchanPayloadBodyKind::contemporaneous_fragment_suffix ?
          "fragment_suffix" : payload.body_kind==NetchanPayloadBodyKind::normal_fragment_completion ||
          payload.sequence_flags.fragmented ? "reassembled_body" : payload.sequence_flags.reliable ? "reliable_body" : "direct_body";
    }
    auto decoded = [&] {
      const MovementCostScope parse_cost{config_.movement_diagnostics,
          movement_diagnostics_.network_parse_commit_ms};
      return ServicePayloadEnvelopeDecoder{config_.envelope}.decode(std::move(payload));
    }();
    if (!decoded || !decoded.envelope) {
      fail(LiveRuntimeStageErrorCode::envelope_decode_failed,
           LiveRuntimeStageState::protocol_error,
           decoded.error
               ? std::string_view{decoded.error->context}
               : std::string_view{"Live service envelope was not decoded"},
           now,
           decoded.error ? std::optional{decoded.error->code} : std::nullopt);
      return;
    }
    ++service_envelopes_decoded_;
    auto service = std::move(decoded.envelope->payload);
    const auto terminal_notice = inspect_disconnect_notice(service.bytes);
    if (terminal_notice == DisconnectNoticeStatus::observed) {
      if (auto* driver = response_stage_.close_driver()) {
        driver->confirm_server_disconnect(now);
        graceful_close_ = driver->graceful_close_snapshot();
      }
      fail(LiveRuntimeStageErrorCode::server_disconnected,
           LiveRuntimeStageState::protocol_error,
           "Server disconnected this network generation", now);
      return;
    }
    if (padding_only(service)) {
      return;
    }
    auto cursor =
        StockRuntimeSourceCursor::create(0U, 0U, service.bytes.size());
    if (!cursor) {
      fail(LiveRuntimeStageErrorCode::initialization_context_missing,
           LiveRuntimeStageState::protocol_error,
           "Live service payload cursor is invalid", now);
      return;
    }
    process_service_payload(std::move(service), *cursor, now);
  }

  void process_service_payload(OwnedServicePayload payload,
                               StockRuntimeSourceCursor cursor,
                               const LiveRuntimeStageTimePoint now) {
    if (cursor.absolute_bit_offset() >= payload.bytes.size() * 8U) {
      return;
    }
    if (cursor.byte_aligned()) {
      const auto remaining =
          std::span{payload.bytes}.subspan(cursor.byte_offset());
      if (remaining.size() <= 8U &&
          std::ranges::all_of(remaining, [](const std::byte value) {
            return value == std::byte{0U};
          })) {
        return;
      }
    }
    if (payload_ordinal_ == (std::numeric_limits<std::size_t>::max)()) {
      fail(LiveRuntimeStageErrorCode::initialization_context_missing,
           LiveRuntimeStageState::protocol_error,
           "Live service payload ordinal overflowed", now);
      return;
    }
    if (!observe_service_encoding(payload.decompressed,
                                  payload.wire_uncompressed)) {
      fail(LiveRuntimeStageErrorCode::initialization_context_missing,
           LiveRuntimeStageState::protocol_error,
           "Live service payload has no valid wire encoding provenance", now);
      return;
    }
    ++payload_ordinal_;
    ++received_service_payload_count_;
    const auto received = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::service_payload_received,
        .payload_ordinal = payload_ordinal_,
        .source_sequence = payload.source_sequence,
        .payload_byte_count = payload.bytes.size(),
        .decompressed = payload.decompressed,
        .wire_uncompressed = payload.wire_uncompressed,
        .occurred_at = now};
    push_event(received);
    emit_trace(received);

    if (!runtime_session_) {
      std::size_t control_ordinal = 0U;
      while (cursor.absolute_bit_offset() < payload.bytes.size() * 8U) {
        const auto opcode = opcode_at(payload, cursor);
        if (!opcode) {
          fail(LiveRuntimeStageErrorCode::unsupported_initialization_message,
               LiveRuntimeStageState::protocol_error,
               "Non-byte-aligned initialization message has no supported "
               "framing",
               now);
          return;
        }
        if (*opcode == kGoldSrcSvcSpawnBaselineOpcode) {
          const auto body_cursor = StockRuntimeSourceCursor::create(
              cursor.byte_offset() + 1U, 0U, payload.bytes.size());
          if (!body_cursor) {
            fail(LiveRuntimeStageErrorCode::baseline_decode_failed,
                 LiveRuntimeStageState::protocol_error,
                 "svc_spawnbaseline body cursor is invalid", now, std::nullopt,
                 std::nullopt, std::nullopt,
                 EntityBaselineDecodeErrorCode::invalid_cursor, std::nullopt,
                 std::nullopt, *opcode);
            return;
          }
          auto baseline =
              GoldSrcEntityBaselineDecoder{config_.baselines}.decode(
                  EntityBaselineDecodeInput{
                      &payload, *body_cursor, payload_ordinal_,
                      kLiveRuntimeGeneration, max_clients_},
                  *schemas_);
          if (!baseline || !baseline.registry) {
            fail(
                LiveRuntimeStageErrorCode::baseline_decode_failed,
                LiveRuntimeStageState::protocol_error,
                baseline.error
                    ? std::string_view{baseline.error->context}
                    : std::string_view{"Baseline decoder returned no registry"},
                now, std::nullopt, std::nullopt, std::nullopt,
                baseline.error ? std::optional{baseline.error->code}
                               : std::nullopt,
                std::nullopt, std::nullopt, *opcode);
            return;
          }
          baseline_entity_count_ = baseline.entity_count;
          baseline_instanced_count_ = baseline.instanced_count;
          cursor = baseline.end_cursor;
          RuntimeReplayInitialization initialization;
          initialization.generation = kLiveRuntimeGeneration;
          initialization.game_client = config_.game_client;
          initialization.sound_events = config_.sound_events;
          initialization.max_clients = max_clients_;
          initialization.receiving_player_entity = static_cast<std::uint32_t>(client_slot_) + 1U;
          initialization.schemas = schemas_;
          initialization.user_message_definitions = user_messages_;
          initialization.limits = config_.runtime;
          try {
            for (const auto& identity : response_stage_.result()->resource_list()
                     .transition().user_info().messages())
              initialization.initial_player_identities.push_back(
                  std::make_shared<const UserInfoUpdateState>(identity));
            initialization.baselines =
                std::make_shared<const EntityBaselineRegistryState>(
                    std::move(*baseline.registry));
          } catch (const std::bad_alloc &) {
            fail(LiveRuntimeStageErrorCode::runtime_session_initialize_failed,
                 LiveRuntimeStageState::protocol_error,
                 "Unable to retain live baseline registry", now);
            return;
          }
          auto initialized = RuntimeReplaySession::initialize(
              std::move(initialization), target_);
          if (!initialized || !initialized.session) {
            fail(LiveRuntimeStageErrorCode::runtime_session_initialize_failed,
                 LiveRuntimeStageState::protocol_error,
                 initialized.error
                     ? std::string_view{initialized.error->context}
                     : std::string_view{"Runtime session initialization "
                                        "returned no session"},
                 now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                 initialized.error ? std::optional{initialized.error->code}
                                   : std::nullopt);
            return;
          }
          runtime_session_ = std::move(initialized.session);
          for (auto &pending : pending_hud_messages_) {
            runtime_record_ordinal_ = std::max(
                runtime_record_ordinal_ + 1U, pending.source_ordinal);
            ++runtime_records_attempted_;
            auto retained_record = RuntimeReplayRecord{kLiveRuntimeGeneration,
                    static_cast<std::uint64_t>(runtime_record_ordinal_),
                    runtime_record_ordinal_, std::move(pending.payload),
                    pending.cursor};
            retained_record.received_at=pending.received_at;
            auto applied = runtime_session_->apply_record(retained_record);
            if (!applied) {
              fail(LiveRuntimeStageErrorCode::runtime_record_failed,
                   LiveRuntimeStageState::protocol_error,
                   "Retained pre-baseline HUD message failed replay", now);
              if (error_ && applied.error)
                error_->retain_runtime_failure(std::move(*applied.error));
              return;
            }
            ++applied_runtime_record_count_;
          }
          pending_hud_messages_.clear();
          pending_hud_bytes_ = 0U;
          pending_audio_count_ = pending_audio_bytes_ = 0U;
          state_ = LiveRuntimeStageState::receiving_runtime;
          const auto baseline_event = LiveRuntimeStageEvent{
              .type = LiveRuntimeStageEventType::baseline_registry_ready,
              .payload_ordinal = payload_ordinal_,
              .source_sequence = payload.source_sequence,
              .payload_byte_count = payload.bytes.size(),
              .baseline_count = baseline_entity_count_,
              .occurred_at = now};
          push_event(baseline_event);
          emit_trace(baseline_event);
          break;
        }

        const auto control = RuntimeControlDecoder{}.decode_one(
            RuntimeControlDecodeInput{payload, cursor, kLiveRuntimeGeneration,
                                      payload_ordinal_, user_messages_, schemas_.get()},
            control_ordinal++);
        if (!control || !control.event) {
          fail(LiveRuntimeStageErrorCode::unsupported_initialization_message,
               LiveRuntimeStageState::protocol_error,
               control.error ? std::string_view{control.error->context}
                             : std::string_view{"Initialization control "
                                                "decoder returned no event"},
               now, std::nullopt,
               control.error ? std::optional{control.error->code}
                             : std::nullopt,
               std::nullopt, std::nullopt, std::nullopt, std::nullopt, *opcode);
          return;
        }
        const bool audio_control = control.event->opcode == RuntimeControlOpcode::svc_sound ||
            control.event->opcode == RuntimeControlOpcode::svc_stopsound ||
            control.event->opcode == RuntimeControlOpcode::svc_spawnstaticsound;
        if (control.event->kind == RuntimeControlMessageKind::user_message ||
            control.event->kind == RuntimeControlMessageKind::user_info_update ||
            control.event->kind == RuntimeControlMessageKind::view_angles ||
            control.event->opcode == RuntimeControlOpcode::svc_weaponanim ||
            (audio_control && config_.sound_events)) {
          const auto end = control.event->provenance.end_cursor.byte_offset();
          const auto start = control.event->provenance.start_cursor;
          if (audio_control && (pending_audio_count_>=64U || pending_audio_bytes_+end>(1U<<20U))) {
            config_.sound_events->reject_pending();
            cursor=control.event->provenance.end_cursor;
            continue;
          }
          if ((!audio_control && pending_hud_messages_.size()-pending_audio_count_ >= 64U) || end > payload.bytes.size() ||
              (!audio_control &&
              pending_hud_bytes_ + end > (1U << 20U))) {
            fail(LiveRuntimeStageErrorCode::unsupported_initialization_message,
                 LiveRuntimeStageState::protocol_error,
                 "Pre-baseline HUD handoff exceeds its bounded capacity", now);
            return;
          }
          auto retained = payload;
          retained.bytes.resize(end);
          if(audio_control) {++pending_audio_count_; pending_audio_bytes_+=end;}
          else pending_hud_bytes_ += end;
          pending_hud_messages_.push_back(
              {payload_ordinal_, std::move(retained), start, now});
        }
        cursor = control.event->provenance.end_cursor;
      }
      if (!runtime_session_) {
        return;
      }
    }

    if (cursor.absolute_bit_offset() < payload.bytes.size() * 8U) {
      apply_runtime_record(std::move(payload), cursor, now);
    }
  }

  void apply_runtime_record(OwnedServicePayload payload,
                            const StockRuntimeSourceCursor cursor,
                            const LiveRuntimeStageTimePoint now) {
    if (!runtime_session_) {
      fail(LiveRuntimeStageErrorCode::runtime_session_initialize_failed,
           LiveRuntimeStageState::protocol_error,
           "Runtime record arrived without an initialized session", now);
      return;
    }
    runtime_record_ordinal_ = std::max(
        runtime_record_ordinal_ + 1U, payload_ordinal_);
    const auto identity = static_cast<std::uint64_t>(runtime_record_ordinal_);
    ++runtime_records_attempted_;
    const auto record_bytes = payload.bytes.size();
    auto sound_timed_record=RuntimeReplayRecord{kLiveRuntimeGeneration, identity,
                            runtime_record_ordinal_,
                            std::move(payload), cursor};
    sound_timed_record.received_at=now;
    auto applied = [&] {
      const MovementCostScope parse_cost{config_.movement_diagnostics,
          movement_diagnostics_.network_parse_commit_ms};
      return runtime_session_->apply_record(sound_timed_record);
    }();
    if (!applied || !applied.event) {
      fail(LiveRuntimeStageErrorCode::runtime_record_failed,
           LiveRuntimeStageState::protocol_error,
           applied.error
               ? std::string_view{applied.error->context}
               : std::string_view{"Runtime dispatcher rejected a record"},
           now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           applied.error ? std::optional{applied.error->code} : std::nullopt,
           std::nullopt,
           applied.error ? applied.error->decoder_wire_opcode : std::nullopt);
      if (error_ && applied.error)
        error_->retain_runtime_failure(std::move(*applied.error));
      return;
    }
    ++applied_runtime_record_count_;
    if (config_.movement_diagnostics) {
      movement_diagnostics_.atomic_commit_ns = actual_monotonic_ns();
      if (applied.event->clientdata_observed && applied.event->entities_observed)
        movement_diagnostics_.latest_state_commit_ns = movement_diagnostics_.atomic_commit_ns;
      ++movement_diagnostics_.committed_records;
      movement_diagnostics_.committed_bytes += record_bytes;
      movement_diagnostics_.record_identity = identity;
      movement_diagnostics_.source_transport_sequence = sound_timed_record.payload.source_sequence;
      for (const auto& decoded : applied.event->decoded_batch.events) {
        const auto* control = std::get_if<RuntimeControlEvent>(&decoded);
        if (control && control->kind == RuntimeControlMessageKind::choke)
          ++movement_diagnostics_.choke_markers_total;
      }
      if (const auto& committed = target_.runtime_observation(); committed) {
        movement_diagnostics_.server_time_seconds = committed->server_time_seconds;
        movement_diagnostics_.entity_count = committed->packet_entities.size();
        movement_diagnostics_.observed_player_count = static_cast<std::size_t>(std::count_if(
            committed->packet_entities.begin(), committed->packet_entities.end(),
            [](const auto& entity) { return entity.player_movement_schema; }));
      }
    }
    if(config_.effect_diagnostics) {
      // Only after successful atomic apply; no raw payload, text, auth or
      // uncommitted/duplicate record is exposed through this observation.
      for(const auto& decoded:applied.event->decoded_batch.events) {
        const auto* control=std::get_if<RuntimeControlEvent>(&decoded);
        if(!control) continue;
        if(const auto diagnostic=runtime_effect_diagnostic(*control)) {
          emit_trace(LiveRuntimeStageEvent{
              .type=LiveRuntimeStageEventType::committed_effect_observed,
              .payload_ordinal=runtime_record_ordinal_,
              .occurred_at=now,
              .effect_diagnostic=diagnostic,
              .message_bit_offset=control->provenance.start_cursor.absolute_bit_offset()});
        }
      }
    }
    bool signon_reply_required = false;
    for (const auto &decoded_event : applied.event->decoded_batch.events) {
      const auto *const control =
          std::get_if<RuntimeControlEvent>(&decoded_event);
      if (control == nullptr ||
          control->kind != RuntimeControlMessageKind::signon_control) {
        continue;
      }
      const auto *const signon =
          std::get_if<RuntimeControlSignonControl>(&control->body);
      if (signon == nullptr || signon->signon_number != 1U) {
        fail(LiveRuntimeStageErrorCode::unsupported_initialization_message,
             LiveRuntimeStageState::protocol_error,
             "Live signon-control event has no supported typed reply", now,
             std::nullopt, std::nullopt, std::nullopt, std::nullopt,
             std::nullopt, std::nullopt,
             static_cast<std::uint8_t>(control->opcode));
        return;
      }
      signon_control_observed_ = true;
      signon_reply_required = true;
    }
    if (signon_reply_required && !signon_reply_) {
      auto *const driver = response_stage_.retained_driver();
      if (driver == nullptr || !spawn_request_acknowledged_) {
        fail(LiveRuntimeStageErrorCode::signon_reply_queue_failed,
             LiveRuntimeStageState::protocol_error,
             "svc_signonnum 1 arrived without an acknowledged spawn generation",
             now);
        return;
      }
      queue_signon_reply(*driver, now);
      if (terminal_state(state_)) {
        return;
      }
    }
    if (stable_started_at_ &&
        applied_runtime_record_count_ > stable_start_applied_record_count_) {
      stable_progress_observed_ = true;
    }
    if (applied.event->clientdata_observed) {
      ++clientdata_record_count_;
      clientdata_observed_ = true;
    }
    if (applied.event->entities_observed) {
      ++entity_record_count_;
      entities_observed_ = true;
    }
    server_time_observed_ =
        server_time_observed_ || applied.event->server_time_observed;
    const auto &observation = target_.runtime_observation();
    if (observation && (prediction_life_epoch_ != observation->lifecycle.life_epoch ||
                        prediction_life_deaths_ != observation->lifecycle.deaths)) {
      prediction_life_epoch_ = observation->lifecycle.life_epoch;
      prediction_life_deaths_ = observation->lifecycle.deaths;
      prediction_brush_context_ = {};
      prediction_support_motion_.reset();
      suspend_reference_prediction(observation->lifecycle.dead()
          ? "local_player_dead" : "new_life_waiting_for_fresh_carrier");
      prediction_presentation_cache_.reset();
      prediction_last_anchor_at_.reset();
      prediction_last_record_identity_.reset();
      prediction_life_command_floor_ = generated_usercmd_count_;
      button_latch_.clear();
      const auto scoped = config_.game_client->movement_policy().capture_life_scoped_buttons;
      scoped_button_gate_.invalidate(scoped);
      if (live_visual_input_) {
        live_visual_input_->held_buttons &= ~scoped;
        live_visual_input_->pressed_buttons &= ~scoped;
      }
      // Discard only undelivered visual notifications across a life boundary,
      // never immutable wire history or prepared/transmitted commands.
      weapon_command_submissions_.clear();
    }
    if (observation) refresh_prediction_collision(*observation);
    if ((state_ == LiveRuntimeStageState::running_usercmd_scenario ||
         state_ == LiveRuntimeStageState::running_live_visual_control) &&
        applied.event->clientdata_observed && observation) {
      record_usercmd_server_sample(*observation, now);
      if (terminal_state(state_))
        return;
    }
    const auto event = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::runtime_record_applied,
        .payload_ordinal = payload_ordinal_,
        .source_sequence = applied.event->source_transport_sequence,
        .entity_count = observation ? observation->packet_entities.size() : 0U,
        .publication_revision = applied.event->publication_revision,
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
    if (!stable_started_at_ && server_time_observed_ && clientdata_observed_ &&
        entities_observed_ && observation) {
      stable_started_at_ = now;
      stable_start_applied_record_count_ = applied_runtime_record_count_;
      const auto stable = LiveRuntimeStageEvent{
          .type = LiveRuntimeStageEventType::stability_interval_started,
          .payload_ordinal = payload_ordinal_,
          .source_sequence = applied.event->source_transport_sequence,
          .entity_count = observation->packet_entities.size(),
          .publication_revision = observation->publication_revision,
          .occurred_at = now};
      push_event(stable);
      emit_trace(stable);
    }
  }

  [[nodiscard]] static std::optional<std::int64_t>
  scheduler_time_ns(const LiveRuntimeStageTimePoint value) noexcept {
    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        value.time_since_epoch());
    return duration.count();
  }

  void activate_usercmd_scenario(const LiveRuntimeStageTimePoint now) {
    auto *const driver = response_stage_.retained_driver();
    const auto &observation = target_.runtime_observation();
    if (driver == nullptr || driver->state() != NetchanDriverState::active ||
        !schemas_ || !runtime_session_ || !observation ||
        runtime_session_->generation() != kLiveRuntimeGeneration ||
        observation->generation != kLiveRuntimeGeneration ||
        !signon_control_observed_ || !signon_reply_acknowledged_) {
      fail(LiveRuntimeStageErrorCode::usercmd_observation_failed,
           LiveRuntimeStageState::protocol_error,
           "Production usercmd handoff is incomplete or no longer current",
           now);
      return;
    }
    auto binding = bind_goldsrc_usercmd_schema(
        *schemas_,
        GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
    if (!binding || !binding.binding) {
      fail(LiveRuntimeStageErrorCode::usercmd_schema_binding_failed,
           LiveRuntimeStageState::protocol_error,
           binding.error ? binding.error->context
                         : "Current usercmd schema could not be bound",
           now);
      if (error_ && binding.error) {
        error_->usercmd_schema_code = binding.error->code;
      }
      return;
    }

    scenario_intents_.clear();
    try {
      scenario_intents_.reserve(kLiveUserCmdPhaseCount);
      for (std::size_t index = 0U; index < kLiveUserCmdPhaseCount; ++index) {
        auto intent = make_scenario_intent(
            config_.game_client->movement_policy(), phase_from_index(index),
            config_.live_visual_input_source ==
                LiveVisualControlInputSource::scripted_side_check,
            config_.live_visual_input_source ==
                LiveVisualControlInputSource::scripted_jump_duck_check,
            (config_.live_visual_input_source ==
                 LiveVisualControlInputSource::scripted_speed_check ||
             config_.live_visual_input_source ==
                 LiveVisualControlInputSource::scripted_weapon_check),
            config_.reference_prediction &&
                config_.live_visual_input_source ==
                    LiveVisualControlInputSource::scripted_jump_duck_check);
        if (!intent) {
          fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
               LiveRuntimeStageState::protocol_error,
               "Controlled scenario intent could not be created", now);
          return;
        }
        scenario_intents_.push_back(std::move(*intent));
      }
    } catch (const std::bad_alloc &) {
      fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
           LiveRuntimeStageState::protocol_error,
           "Controlled scenario intent allocation failed", now);
      return;
    }
    const bool live_visual = config_.operation_mode ==
                             LiveRuntimeOperationMode::live_visual_control;
    const bool keyboard =
        live_visual && config_.live_visual_input_source ==
                           LiveVisualControlInputSource::keyboard_mouse;
    gameplay_camera::GameplayCameraStateCreateInfo camera_info;
    camera_info.yaw_degrees =
        keyboard && live_visual_input_
            ? live_visual_input_->yaw_degrees
            : config_.usercmd_scenario.fixed_yaw_degrees;
    camera_info.pitch_degrees =
        keyboard && live_visual_input_
            ? live_visual_input_->pitch_degrees
            : config_.usercmd_scenario.fixed_pitch_degrees;
    auto camera = gameplay_camera::GameplayCameraState::create(camera_info);
    if (!camera || !camera.state) {
      fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
           LiveRuntimeStageState::protocol_error,
           "Explicit test orientation failed camera validation", now);
      return;
    }
    scenario_camera_.emplace(std::move(*camera.state));

    GoldSrcUserCmdSchedulerConfig scheduler_config;
    scheduler_config.command_interval_nanoseconds =
        static_cast<std::uint64_t>(
            config_.usercmd_scenario.command_interval.count()) *
        1'000'000U;
    scheduler_config.maximum_commands_per_update = 8U;
    scheduler_config.profile =
        GoldSrcUserCmdSamplingProfile::stock_protocol_48_live_usercmd_check_v1;
    if (keyboard)
      scheduler_config.lag_policy = GoldSrcUserCmdLagPolicy::discard_unsampled_wall_time;
    usercmd_scheduler_ =
        std::make_unique<GoldSrcUserCmdScheduler>(scheduler_config);

    GoldSrcUserCmdTransmissionConfig transmission_config;
    transmission_config.history.profile =
        GoldSrcUserCmdHistoryProfile::reference_wire_v1;
    transmission_config.history.generation = kLiveRuntimeGeneration;
    transmission_config.history.maximum_entries =
        kMaximumGoldSrcUserCmdHistoryEntries;
    transmission_config.planner.profile =
        GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1;
    transmission_config.planner.desired_backup_commands = 2U;
    transmission_config.planner.maximum_backup_commands = 7U;
    transmission_config.planner.maximum_new_commands = 8U;
    transmission_config.planner.maximum_commands_per_packet = 16U;
    transmission_config.maximum_events = 1'024U;
    transmission_config.maximum_transmission_phases_per_update = 2U;
    transmission_config.externally_owned_driver_update = true;
    transmission_config.timeout = std::chrono::seconds{15};
    usercmd_stage_ = std::make_unique<GoldSrcUserCmdTransmissionStage>(
        *driver, *binding.binding,
        GoldSrcUserCmdSessionPrerequisite{
            GoldSrcUserCmdSessionPrerequisiteProfile::
                production_live_runtime_ready_v1,
            true, kLiveRuntimeGeneration},
        transmission_config);
    if (!usercmd_scheduler_->valid_configuration() ||
        !usercmd_stage_->valid_configuration() || !scenario_camera_) {
      fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
           LiveRuntimeStageState::protocol_error,
           "Production usercmd controller configuration is invalid", now);
      return;
    }
    const auto time_ns = scheduler_time_ns(now);
    if (!time_ns) {
      fail(LiveRuntimeStageErrorCode::usercmd_scheduler_failed,
           LiveRuntimeStageState::protocol_error,
           "Activation time is outside the scheduler domain", now);
      return;
    }
    const auto initial_intent =
        keyboard && live_visual_input_
            ? make_live_visual_intent(config_.game_client->movement_policy(), *live_visual_input_)
            : std::optional<gameplay_input::GameplayInputIntent>{
                  scenario_intents_.front()};
    if (!initial_intent) {
      fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
           LiveRuntimeStageState::protocol_error,
           "Live visual input could not initialize the scheduler", now);
      return;
    }
    const auto initialized = usercmd_scheduler_->update(
        *time_ns, *initial_intent, *scenario_camera_);
    last_scheduler_due_command_count_ = initialized.requests.size();
    if (!initialized || !initialized.requests.empty()) {
      fail(LiveRuntimeStageErrorCode::usercmd_scheduler_failed,
           LiveRuntimeStageState::protocol_error,
           "Production scheduler did not initialize at the handoff boundary",
           now);
      if (error_ && initialized.error) {
        error_->usercmd_scheduler_code = initialized.error->code;
      }
      return;
    }
    usercmd_activated_at_ = now;
    driver_rx_at_input_activation_ = driver->receive_statistics();
    payload_events_consumed_at_input_activation_ = payload_events_consumed_;
    service_envelopes_decoded_at_input_activation_ = service_envelopes_decoded_;
    runtime_records_attempted_at_input_activation_ = runtime_records_attempted_;
    runtime_records_committed_at_input_activation_ = applied_runtime_record_count_;
    clientdata_records_committed_at_input_activation_ = clientdata_record_count_;
    usercmd_activation_time_ns_ = *time_ns;
    if (!d_handoff_interval_) {
      d_handoff_interval_ =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now - *stable_started_at_);
    }
    state_ = live_visual
                 ? LiveRuntimeStageState::running_live_visual_control
                 : LiveRuntimeStageState::running_usercmd_scenario;
    const auto activated = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::usercmd_scenario_activated,
        .publication_revision = observation->publication_revision,
        .input_phase = LiveUserCmdInputPhase::neutral_before,
        .occurred_at = now};
    push_event(activated);
    emit_trace(activated);
  }

  void update_usercmd_before_driver(const LiveRuntimeStageTimePoint now) {
    if (!usercmd_stage_ || !usercmd_scheduler_ || !scenario_camera_ ||
        !usercmd_activated_at_ || !usercmd_activation_time_ns_) {
      fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
           LiveRuntimeStageState::protocol_error,
           "Active usercmd scenario lost an owning component", now);
      return;
    }
    if (usercmd_stage_->has_pending_reference_submission()) {
      last_scheduler_due_command_count_ = 0U;
      // An earlier packet may have survived would_block or been bound by the
      // previous post-driver phase. It owns the exact history revision. Do
      // not advance sample deadlines, command IDs or one-shot consumption
      // until the shared driver pump can finish that existing transaction.
      // Ordinary unsent history without a prepared packet still permits
      // bounded sampling and is deliberately not gated here.
      const auto pending = usercmd_stage_->update_reference(now);
      if (!pending && pending.error &&
          pending.error->code != GoldSrcUserCmdTransmissionErrorCode::event_backpressure) {
        fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
             LiveRuntimeStageState::protocol_error, pending.error->context, now,
             std::nullopt, std::nullopt, std::nullopt, std::nullopt,
             std::nullopt, pending.error->driver_code);
        if (error_) {
          error_->usercmd_transmission_code = pending.error->code;
          error_->usercmd_history_code = pending.error->history_code;
        }
      }
      return;
    }
    const bool keyboard =
        config_.operation_mode ==
            LiveRuntimeOperationMode::live_visual_control &&
        config_.live_visual_input_source ==
            LiveVisualControlInputSource::keyboard_mouse;
    const auto end =
        *usercmd_activated_at_ + scenario_duration(config_.usercmd_scenario);
    const auto capped = keyboard || now < end ? now : end;
    const auto time_ns = scheduler_time_ns(capped);
    if (!time_ns) {
      fail(LiveRuntimeStageErrorCode::usercmd_scheduler_failed,
           LiveRuntimeStageState::protocol_error,
           "Scenario time is outside the scheduler domain", now);
      return;
    }
    std::optional<gameplay_input::GameplayInputIntent> live_intent;
    std::optional<gameplay_camera::GameplayCameraState> live_camera;
    if (keyboard) {
      if (!live_visual_input_ || !live_visual_input_at_) {
        fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
             LiveRuntimeStageState::protocol_error,
             "Keyboard-mouse control has no current typed input", now);
        return;
      }
      auto control = *live_visual_input_;
      if (now - *live_visual_input_at_ > std::chrono::milliseconds{250}) {
        control.forward_axis = 0.0F;
        control.side_axis = 0.0F;
        control.held_buttons = 0U;
        button_latch_.clear();
      }
      auto built_intent = make_live_visual_intent(config_.game_client->movement_policy(), control);
      gameplay_camera::GameplayCameraStateCreateInfo camera_info;
      camera_info.yaw_degrees = control.yaw_degrees;
      camera_info.pitch_degrees = control.pitch_degrees;
      auto created = gameplay_camera::GameplayCameraState::create(camera_info);
      if (!built_intent || !created || !created.state) {
        fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
             LiveRuntimeStageState::protocol_error,
             "Keyboard-mouse command input failed typed validation", now);
        return;
      }
      live_intent.emplace(std::move(*built_intent));
      live_camera.emplace(std::move(*created.state));
    }
    const auto &scheduler_intent =
        keyboard ? *live_intent : scenario_intents_.front();
    const auto &scheduler_camera =
        keyboard ? *live_camera : *scenario_camera_;
    const auto scheduled = usercmd_scheduler_->update(
        *time_ns, scheduler_intent, scheduler_camera);
    if (scheduled && scheduled.discarded_wall_time_samples != 0U) {
      ++scheduler_stall_recoveries_;
      scheduler_discarded_wall_time_samples_ += scheduled.discarded_wall_time_samples;
      // Never apply current input retroactively across an unsampled stall.
      button_latch_.clear();
      auto neutral = *live_visual_input_;
      neutral.forward_axis = 0.0F;
      neutral.side_axis = 0.0F;
      neutral.held_buttons = 0U;
      neutral.pressed_buttons = 0U;
      neutral.released_buttons = 0U;
      auto neutral_intent = make_live_visual_intent(config_.game_client->movement_policy(), neutral);
      if (!neutral_intent) {
        fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
             LiveRuntimeStageState::protocol_error, "Stall neutral input failed validation", now);
        return;
      }
      live_intent.emplace(std::move(*neutral_intent));
    }
    last_scheduler_due_command_count_ = scheduled.requests.size();
    if (!scheduled && scheduled.error &&
        scheduled.error->code ==
            GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded) {
      const auto scheduler_state = usercmd_scheduler_->state();
      if (*time_ns >= scheduler_state.next_sample_time_nanoseconds) {
        const auto overdue = static_cast<std::uint64_t>(*time_ns) -
                             static_cast<std::uint64_t>(
                                 scheduler_state.next_sample_time_nanoseconds);
        last_scheduler_due_command_count_ = static_cast<std::size_t>(
            overdue / usercmd_scheduler_->config()
                          .command_interval_nanoseconds +
            1U);
      }
    }
    if (!scheduled) {
      fail(LiveRuntimeStageErrorCode::usercmd_scheduler_failed,
           LiveRuntimeStageState::protocol_error,
           scheduled.error ? scheduled.error->context
                           : "Production scheduler failed",
           now);
      if (error_ && scheduled.error) {
        error_->usercmd_scheduler_code = scheduled.error->code;
      }
      return;
    }
    for (const auto &request : scheduled.requests) {
      const auto interval_start =
          request.sample_time_nanoseconds -
          static_cast<std::int64_t>(request.sample_duration_nanoseconds);
      if (interval_start < *usercmd_activation_time_ns_) {
        fail(LiveRuntimeStageErrorCode::usercmd_scheduler_failed,
             LiveRuntimeStageState::protocol_error,
             "Scheduler produced a pre-activation command", now);
        return;
      }
      const auto offset = std::chrono::nanoseconds{
          interval_start - *usercmd_activation_time_ns_};
      const auto phase_index = keyboard
                                   ? 0U
                                   : phase_index_for_offset(
                                         config_.usercmd_scenario, offset);
      const bool fire_reload_check =
          config_.live_visual_input_source ==
          LiveVisualControlInputSource::scripted_fire_reload_check ||
          config_.live_visual_input_source ==
          LiveVisualControlInputSource::scripted_fire_reload_presentation_check;
      const bool life_check = config_.live_visual_input_source ==
          LiveVisualControlInputSource::scripted_damage_respawn_check;
      std::optional<gameplay_input::GameplayInputIntent> weapon_intent;
      if (life_check) {
        const auto directive = config_.game_client->scenario_command(
            game_api::GameScenario::damage_respawn, target_.runtime_observation().get(),
            phase_index, std::chrono::duration<double>{offset}.count());
        if (directive.request_self_kill) {
          const auto built_kill = encode_game_command(config_.game_client->self_kill_request());
          auto* driver = response_stage_.retained_driver();
          if (!built_kill || !driver || !driver->queue_reliable(*built_kill.bytes)) {
            fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
                LiveRuntimeStageState::protocol_error, "Typed self-kill request could not be queued", now);
            return;
          }
        }
        if (directive.select_weapon && !request_weapon_selection(*directive.select_weapon)) {
          fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
              LiveRuntimeStageState::protocol_error, "C weapon binding request could not be queued", now);
          return;
        }
        auto intent = make_weapon_check_intent(config_.game_client->movement_policy(),
            directive.buttons != 0U, false, directive.forward, directive.slow_walk);
        if (!intent) {
          fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
              LiveRuntimeStageState::protocol_error, "C typed input rejected", now);
          return;
        }
        weapon_intent.emplace(std::move(*intent));
      } else if (fire_reload_check) {
        auto phase_start = std::chrono::nanoseconds{0};
        for (std::size_t i = 0U; i < phase_index; ++i)
          phase_start += config_.usercmd_scenario.durations[i];
        const auto phase_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(offset - phase_start).count();
        const auto directive = config_.game_client->scenario_command(
            config_.live_visual_input_source ==
                LiveVisualControlInputSource::scripted_fire_reload_presentation_check
                ? game_api::GameScenario::weapon_presentation
                : game_api::GameScenario::weapon_fire_reload,
            target_.runtime_observation().get(), phase_index,
            std::chrono::duration<double>{offset}.count(),
            static_cast<double>(phase_ms) / 1000.0);
        auto built_weapon_intent = make_weapon_check_intent(
            config_.game_client->movement_policy(),
            (directive.buttons & 1U) != 0U, (directive.buttons & (1U << 13U)) != 0U,
            directive.forward, directive.slow_walk);
        if (!built_weapon_intent) {
          fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
               LiveRuntimeStageState::protocol_error,
               "Bounded weapon-check intent could not be created", now);
          return;
        }
        weapon_intent.emplace(std::move(*built_weapon_intent));
      }
      GoldSrcUserCmdBuildContext context;
      context.command_sequence = request.command_sequence;
      context.command_msec = request.command_msec;
      context.command_sample_duration_nanoseconds =
          request.sample_duration_nanoseconds;
      context.command_sample_time_nanoseconds = request.sample_time_nanoseconds;
      const auto &command_intent =
          keyboard ? *live_intent : fire_reload_check || life_check
              ? *weapon_intent : scenario_intents_[phase_index];
      const auto &command_camera = keyboard ? *live_camera : *scenario_camera_;
      const bool reference_speed = keyboard ||
          fire_reload_check || life_check ||
          config_.live_visual_input_source ==
              LiveVisualControlInputSource::scripted_speed_check ||
          config_.live_visual_input_source ==
              LiveVisualControlInputSource::scripted_weapon_check;
      context.movement_speeds = reference_speed
          ? config_.game_client->movement_policy().movement_speeds
          : live_visual_movement_speeds(
                config_.live_visual_input_source ==
                    LiveVisualControlInputSource::scripted_side_check,
                command_intent.forward_axis(), command_intent.side_axis(),
                static_cast<float>(config_.usercmd_scenario.forward_amplitude));
      if (reference_speed)
        context.reference_movement.speed_key_multiplier =
            config_.game_client->movement_policy().speed_key_multiplier;
      if (keyboard || fire_reload_check || life_check) {
        context.reference_button_policy =
            config_.game_client->movement_policy().button_policy;
        if (keyboard) {
          context.one_shot_buttons = button_latch_.pending();
          if(target_.runtime_observation()) context.edge_triggered_buttons =
              config_.game_client->edge_triggered_buttons(*target_.runtime_observation());
        }
      } else if (config_.operation_mode ==
                     LiveRuntimeOperationMode::live_visual_control &&
                 config_.live_visual_input_source ==
                     LiveVisualControlInputSource::scripted_jump_duck_check) {
        context.reference_button_policy =
            GoldSrcReferenceButtonPolicy::jump_duck;
      } else if (config_.live_visual_input_source ==
                     LiveVisualControlInputSource::scripted_speed_check ||
                 config_.live_visual_input_source ==
                     LiveVisualControlInputSource::scripted_weapon_check) {
        context.reference_button_policy =
            GoldSrcReferenceButtonPolicy::jump_duck;
      }
      auto built = usercmd_adapter_.build_reference_wire(
          command_intent, command_camera, context);
      if (!built || !built.command) {
        fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
             LiveRuntimeStageState::protocol_error,
             built.error ? built.error->context
                         : "Controlled command adapter failed",
             now);
        if (error_ && built.error) {
          error_->usercmd_adapter_code = built.error->code;
        }
        return;
      }
      last_sampled_forward_axis_ = command_intent.forward_axis();
      last_sampled_side_axis_ = command_intent.side_axis();
      if (built.command->side != 0)
        ++nonzero_side_generated_count_;
      const auto queued = usercmd_stage_->queue_reference_command(
          request.command_sequence, *built.command, kLiveRuntimeGeneration);
      if (!queued) {
        fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
             LiveRuntimeStageState::protocol_error,
             queued.error ? queued.error->context
                          : "Production history rejected a command",
             now);
        if (error_) {
          error_->usercmd_transmission_code = GoldSrcUserCmdTransmissionErrorCode::history_failed;
          if (queued.error) error_->usercmd_history_code = queued.error->code;
        }
        return;
      }
      if(config_.movement_diagnostics) {
        movement_diagnostics_.latest_command=*built.command;
        movement_diagnostics_.latest_command_sequence=request.command_sequence.value();
        movement_diagnostics_.latest_command_sample_time_ns=request.sample_time_nanoseconds;
      }
      // Command identity counts generated physics steps, not wall-clock
      // slots. A bounded scheduler recovery skips unsampled wall time while
      // retaining contiguous IDs. Preserve the actual sample deadline until
      // the exact TX receipt instead of reconstructing activation+ID*dt.
      command_sample_times_[request.command_sequence.value() %
          command_sample_times_.size()] = {
              request.command_sequence.value(),request.sample_time_nanoseconds};
      if (!requested_forward_by_phase_[phase_index]) {
        requested_forward_by_phase_[phase_index] = built.requested_forward;
        encoded_forward_by_phase_[phase_index] = built.command->forward;
        encoded_side_by_phase_[phase_index] = built.command->side;
        speed_multiplier_by_phase_[phase_index] =
            built.applied_speed_multiplier;
      }
      if (built.one_shot_plan) {
        if (!built.one_shot_plan->commit_after_history_insert(
                request.command_sequence)) {
          fail(LiveRuntimeStageErrorCode::usercmd_adapter_failed,
               LiveRuntimeStageState::protocol_error,
               "Pending button press did not match inserted history identity", now);
          return;
        }
        button_latch_.consume_after_history_insert(
            built.one_shot_plan->consumes_buttons());
      }
      const auto buttons = built.command->buttons;
      const auto count_button = [&](const std::uint16_t bit,
                                    std::size_t &generated,
                                    std::size_t &pressed,
                                    std::size_t &released) {
        generated += (buttons & bit) != 0U;
        pressed += (buttons & bit) != 0U &&
                   (previous_generated_buttons_ & bit) == 0U;
        released += (buttons & bit) == 0U &&
                    (previous_generated_buttons_ & bit) != 0U;
      };
      count_button(kReferenceGoldSrcButtonJump, jump_generated_count_,
                   jump_command_press_count_, jump_command_release_count_);
      count_button(kReferenceGoldSrcButtonDuck, duck_generated_count_,
                   duck_command_press_count_, duck_command_release_count_);
      count_button(kReferenceGoldSrcButtonAttack, attack_generated_count_,
                   attack_command_press_count_, attack_command_release_count_);
      count_button(kReferenceGoldSrcButtonReload, reload_generated_count_,
                   reload_command_press_count_, reload_command_release_count_);
      count_button(kReferenceGoldSrcButtonUse, use_generated_count_,
                   use_command_press_count_, use_command_release_count_);
      previous_generated_buttons_ = buttons;
      ++generated_usercmd_count_;
      ++generated_by_phase_[phase_index];
      if(config_.game_client && target_.runtime_observation()) {
        const auto at=config_.game_client->presentation_seconds(static_cast<double>(request.sample_time_nanoseconds)*1e-9);
        if(at) {
          game_api::LocalWeaponSubmittedCommand command{kLiveRuntimeGeneration,request.command_sequence.value(),buttons,*at};
          const auto& receiving=target_.runtime_observation()->receiving_client;
          // Stock clientdata advertises only view_ofs[2]. Match the existing
          // reference vertical-only eye policy without filling observation
          // fields or accepting a partially advertised horizontal vector.
          const bool vertical_only=receiving && !receiving->view_offset.x &&
              !receiving->view_offset.y && receiving->view_offset.z &&
              std::isfinite(*receiving->view_offset.z);
          if(receiving && receiving->origin.complete() &&
              (receiving->view_offset.complete() || vertical_only)) {
            constexpr double turn=6.2831853071795864769/65536;
            const auto pitch=built.command->angle_turns[0]*turn,yaw=built.command->angle_turns[1]*turn;
            const assets::AssetVector3 forward{static_cast<float>(std::cos(pitch)*std::cos(yaw)),static_cast<float>(std::cos(pitch)*std::sin(yaw)),static_cast<float>(-std::sin(pitch))};
            assets::AssetVector3 eye{static_cast<float>(*receiving->origin.x+receiving->view_offset.x.value_or(0)),static_cast<float>(*receiving->origin.y+receiving->view_offset.y.value_or(0)),static_cast<float>(*receiving->origin.z+*receiving->view_offset.z)};
            if(prediction_history_) {
              const auto& p=*prediction_history_->current_predicted_state();const auto o=p.origin(),v=p.view_offset();eye={o.x+v.x,o.y+v.y,o.z+v.z};
            }
            const assets::AssetVector3 right{static_cast<float>(std::sin(yaw)),static_cast<float>(-std::cos(yaw)),0};
            const assets::AssetVector3 up{right.y*forward.z,right.z*forward.x-right.x*forward.z,-right.y*forward.x+right.x*forward.y};
            command.shot_context=game_api::LocalWeaponSubmittedCommand::ShotContext{eye,forward,right,up};
          }
          const auto impulse=config_.game_client->command_movement_effect(command);
          const auto effect_phase=config_.game_client->command_movement_effect_phase();
          command_effects_[request.command_sequence.value()%command_effects_.size()]={request.command_sequence.value(),impulse.value_or(assets::AssetVector3{}),effect_phase};
        }
      }
      if (config_.reference_prediction)
        predict_committed_reference_command(
            request.command_sequence, *built.command,
            LiveRuntimeStageTimePoint{
                std::chrono::nanoseconds{request.sample_time_nanoseconds}});
    }
    const auto prepared = usercmd_stage_->update_reference(now);
    if (!prepared && prepared.error &&
        prepared.error->code !=
            GoldSrcUserCmdTransmissionErrorCode::event_backpressure) {
      fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
           LiveRuntimeStageState::protocol_error, prepared.error->context, now,
           std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           std::nullopt, prepared.error->driver_code);
      if (error_) {
        error_->usercmd_transmission_code = prepared.error->code;
        error_->usercmd_history_code = prepared.error->history_code;
      }
    }
  }

  void update_usercmd_after_driver(const LiveRuntimeStageTimePoint now) {
    if (!usercmd_stage_)
      return;
    const auto completed = usercmd_stage_->update_reference(now);
    if (!completed && completed.error &&
        completed.error->code !=
            GoldSrcUserCmdTransmissionErrorCode::event_backpressure) {
      fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
           LiveRuntimeStageState::protocol_error, completed.error->context,
           now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           std::nullopt, completed.error->driver_code);
      if (error_) {
        error_->usercmd_transmission_code = completed.error->code;
        error_->usercmd_history_code = completed.error->history_code;
      }
      return;
    }
    while (auto event = usercmd_stage_->poll_event()) {
      if (event->type !=
          GoldSrcUserCmdTransmissionEventType::move_packet_submitted) {
        continue;
      }
      const auto range = inspect_live_usercmd_transmit_range(*event);
      if (!range) {
        fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
             LiveRuntimeStageState::protocol_error,
             "Sent usercmd metadata is incomplete or inconsistent", now);
        if (error_) error_->usercmd_diagnostic_code =
            LiveUserCmdDiagnosticErrorCode::invalid_transmit_metadata;
        return;
      }
      if (!first_new_command_sequence_)
        first_new_command_sequence_ = range->first_new_command_sequence;
      last_new_command_sequence_ = range->last_new_command_sequence;
      if (usercmd_transmit_ranges_.size() >=
          config_.usercmd_scenario.maximum_transmit_ranges) {
        if (!rolling_usercmd_diagnostics()) {
          fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
               LiveRuntimeStageState::protocol_error,
               "Scripted usercmd transmit diagnostic retention was exhausted", now);
          if (error_) error_->usercmd_diagnostic_code =
              LiveUserCmdDiagnosticErrorCode::transmit_retention_exhausted;
          return;
        }
        usercmd_transmit_ranges_.pop_front();
        ++transmit_ranges_evicted_;
      }
      usercmd_transmit_ranges_.push_back(*range);
      std::size_t counted = 0U;
      const auto submitted_history = usercmd_stage_->history();
      if (config_.operation_mode ==
          LiveRuntimeOperationMode::live_visual_control) {
        const auto binding = prediction_carriers_.record_sent(
            *event, submitted_history);
        if (binding == ReferencePredictionAnchorStatus::bound)
          ++prediction_sent_bindings_;
        // The diagnostic anchor status belongs to incoming clientdata binding.
        // Recording a newer outgoing carrier must not overwrite its rejection;
        // successful outgoing bindings are counted separately above.
      }
      const bool keyboard =
          config_.operation_mode ==
              LiveRuntimeOperationMode::live_visual_control &&
          config_.live_visual_input_source ==
              LiveVisualControlInputSource::keyboard_mouse;
      for (std::uint32_t sequence = *event->first_new_command_sequence;
           sequence <= *event->last_new_command_sequence; ++sequence) {
        const auto submitted_command = submitted_history.find(
            *GoldSrcUserCmdSequence::create(sequence));
        if (submitted_command && submitted_command->reference_command &&
            submitted_command->reference_command->side != 0)
          ++nonzero_side_new_submission_count_;
        if (submitted_command && submitted_command->reference_command) {
          if ((submitted_command->reference_command->buttons &
               kReferenceGoldSrcButtonJump) != 0U) {
            ++jump_new_submission_count_;
            if (!first_jump_sent_sequence_)
              first_jump_sent_sequence_ = sequence;
            last_jump_sent_sequence_ = sequence;
          }
          if ((submitted_command->reference_command->buttons &
               kReferenceGoldSrcButtonDuck) != 0U) {
            ++duck_new_submission_count_;
            if (!first_duck_sent_sequence_)
              first_duck_sent_sequence_ = sequence;
            last_duck_sent_sequence_ = sequence;
          }
          attack_new_submission_count_ +=
              (submitted_command->reference_command->buttons &
               kReferenceGoldSrcButtonAttack) != 0U;
          reload_new_submission_count_ +=
              (submitted_command->reference_command->buttons &
               kReferenceGoldSrcButtonReload) != 0U;
          const bool use = (submitted_command->reference_command->buttons & kReferenceGoldSrcButtonUse) != 0U;
          use_new_submission_count_ += use;
          use_clear_after_release_count_ += !use && last_submitted_use_;
          last_submitted_use_ = use;
          // Game policy needs the complete newly submitted command timeline:
          // release edges AND subsequent neutral ticks can finish a held action.
          // Filtering by weapon bits loses both. Backups/replay never enter this
          // first_new..last_new range; the existing bounded FIFO is unchanged.
          if (usercmd_activation_time_ns_) {
            const auto& sampled = command_sample_times_[
                sequence % command_sample_times_.size()];
            if (sampled.sequence != sequence) {
              fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
                   LiveRuntimeStageState::protocol_error,
                   "Submitted command lost its exact scheduler sample time", now);
              return;
            }
            if (weapon_command_submissions_.size() == 128U) {
              weapon_command_submissions_.pop_front();
              ++weapon_command_submission_drops_;
            }
            LiveWeaponCommandSubmission notification{
                kLiveRuntimeGeneration, sequence,
                submitted_command->reference_command->buttons,
                sampled.end_nanoseconds,
                submitted_command->reference_command->angle_turns};
            // Freeze the matching command's pose, never the latest frame's
            // camera or a later command in this submission batch.
            if(prediction_state_==LiveReferencePredictionState::active && prediction_history_) {
              if(const auto* entry=prediction_history_->find_exact(*GoldSrcUserCmdSequence::create(sequence))) {
                const auto& pose=*entry->post_command_state();
                notification.player_origin=pose.origin();
                notification.player_ducked=pose.hull()==hlclient::movement::PlayerMovementHull::ducked;
              }
            }
            if(prediction_state_!=LiveReferencePredictionState::active && !notification.player_origin &&
                target_.runtime_observation() && target_.runtime_observation()->receiving_client) {
              const auto& pose=*target_.runtime_observation()->receiving_client;
              if(pose.origin.complete() && std::isfinite(*pose.origin.x) && std::isfinite(*pose.origin.y) && std::isfinite(*pose.origin.z))
                notification.player_origin=assets::AssetVector3{static_cast<float>(*pose.origin.x),static_cast<float>(*pose.origin.y),static_cast<float>(*pose.origin.z)};
              if(pose.flags) notification.player_ducked=(*pose.flags & 16384U)!=0U;
            }
            weapon_command_submissions_.push_back(std::move(notification));
          }
        }
        const auto offset = config_.usercmd_scenario.command_interval *
                            static_cast<std::int64_t>(sequence - 1U);
        ++sent_by_phase_[keyboard
                             ? 0U
                             : phase_index_for_offset(
                                   config_.usercmd_scenario,
                                   std::chrono::duration_cast<
                                       std::chrono::nanoseconds>(offset))];
        ++counted;
        if (sequence == UINT32_MAX)
          break;
      }
      if (counted != event->new_command_count) {
        fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
             LiveRuntimeStageState::protocol_error,
             "Sent usercmd range disagrees with its new-command count", now);
        return;
      }
      const auto submitted = LiveRuntimeStageEvent{
          .type = LiveRuntimeStageEventType::usercmd_packet_submitted,
          .occurred_at = now};
      push_event(submitted);
      emit_trace(submitted);
    }
  }

  void suspend_reference_prediction(const std::string_view reason) noexcept {
    if (!config_.reference_prediction)
      return;
    if (prediction_state_ == LiveReferencePredictionState::active)
      ++prediction_fallback_count_;
    if (config_.movement_diagnostics) {
      if (prediction_history_)
        prediction_previous_valid_origin_ = prediction_history_->current_predicted_state()->origin();
      if (!prediction_fallback_started_at_ && last_update_)
        prediction_fallback_started_at_ = last_update_;
    }
    prediction_state_ = LiveReferencePredictionState::suspended;
    prediction_last_fallback_reason_ = reason;
    prediction_reason_ = reason;
    prediction_history_.reset();
    prediction_pre_anchor_.reset();
    prediction_visual_correction_.reset();
    prediction_server_velocity_visual_ = false;
    prediction_server_velocity_pose_.reset();
    prediction_exterior_correction_source_.reset();
  }

  void publish_movement_audio(
      const hlclient::movement::LocalPlayerMovementState& before,
      const hlclient::movement::LocalPlayerMovementState& after,
      const GoldSrcUserCmdState& command,
      const LiveRuntimeStageTimePoint sampled_at, bool replay) noexcept {
    if (!config_.game_client || !response_stage_.result()) return;
    game_api::MovementAudioObservation event;
    const auto mode=[](const hlclient::movement::PlayerMovementMode value) {
      using M=hlclient::movement::PlayerMovementMode;
      using A=game_api::MovementAudioMode;
      switch (value) {
      case M::walking: return A::ground;
      case M::airborne: return A::airborne;
      case M::ladder: return A::ladder;
      default: return A::unsupported;
      }
    };
    event.generation=kLiveRuntimeGeneration;
    event.life_epoch=prediction_life_epoch_;
    event.command_sequence=command.command_sequence().value();
    event.command_milliseconds=command.msec();
    event.scheduled_seconds=std::chrono::duration<double>{sampled_at.time_since_epoch()}.count();
    event.before_mode=mode(before.mode()); event.after_mode=mode(after.mode());
    event.before_grounded=before.ground_state().grounded();
    event.after_grounded=after.ground_state().grounded();
    event.before_vertical_velocity=before.velocity().z;
    event.horizontal_speed=std::hypot(after.velocity().x,after.velocity().y);
    event.total_speed=std::hypot(event.horizontal_speed,after.velocity().z);
    event.ducked=after.hull()==hlclient::movement::PlayerMovementHull::ducked;
    event.jump_edge=(command.buttons() & kSyntheticGoldSrcButtonJump)!=0U &&
        (before.old_buttons() & kSyntheticGoldSrcButtonJump)==0U;
    event.world_origin=after.origin(); // Also a sensible spatial origin on a validated ladder.
    event.dry_context=before.last_valid_contents()==hlclient::movement::PlayerMovementContents::empty &&
        after.last_valid_contents()==hlclient::movement::PlayerMovementContents::empty;
    event.movevars_footsteps=response_stage_.result()->resource_list().transition()
        .user_info().movement_environment().move_vars().footsteps();
    if (surface_texture_query_ && after.ground_state().grounded()) {
      auto scene=prediction_brush_context_.scene;
      if (prediction_support_motion_ && prediction_support_motion_->active())
        scene=prediction_support_motion_->sample(
            static_cast<double>(after.simulation_time_nanoseconds())*1e-9);
      const auto surface=surface_texture_query_->at_support(after,scene.get());
      switch (surface.status) {
      case SurfaceTextureResult::Status::found:
        event.surface_status=game_api::MovementSurfaceStatus::found; break;
      case SurfaceTextureResult::Status::unmapped_face:
        event.surface_status=game_api::MovementSurfaceStatus::unmapped_face; break;
      case SurfaceTextureResult::Status::invalid_support:
        event.surface_status=game_api::MovementSurfaceStatus::invalid_support; break;
      case SurfaceTextureResult::Status::geometry_unavailable:
        event.surface_status=game_api::MovementSurfaceStatus::geometry_unavailable; break;
      case SurfaceTextureResult::Status::ambiguous_support:
        event.surface_status=game_api::MovementSurfaceStatus::ambiguous_support; break;
      case SurfaceTextureResult::Status::source_texture_unavailable:
        event.surface_status=game_api::MovementSurfaceStatus::source_texture_unavailable; break;
      }
      event.texture_name=surface.texture_name;
      event.source_surface_index=surface.source_surface_index;
      event.source_material_index=surface.source_material_index;
      event.source_texture_index=surface.source_texture_index;
      event.source_model_index=surface.source_model_index;
      if(surface.world_origin) event.world_origin=surface.world_origin;
      config_.game_client->observe_movement_audio(event,replay);
    } else config_.game_client->observe_movement_audio(event,replay);
  }

  [[nodiscard]] bool append_reference_prediction_command(
      std::shared_ptr<const prediction::LocalPredictionHistoryState>& candidate,
      const GoldSrcUserCmdSequence sequence,
      const GoldSrcWireUserCmd& wire,
      std::string_view& reason,
      const LiveRuntimeStageTimePoint sampled_at, bool replay) {
    ReplayDurationScope replay_duration{config_.movement_diagnostics && replay
        ? &movement_diagnostics_.replayed_simulation_ms : nullptr};
    if (!candidate || !prediction_environment_ || !prediction_collision_) {
      reason = "prediction_context_missing";
      return false;
    }
    const MovementCostScope cost{config_.movement_diagnostics,
        replay ? movement_diagnostics_.prediction_replay_ms
               : movement_diagnostics_.prediction_command_ms};
    if (config_.movement_diagnostics)
      movement_diagnostics_.prediction_use_ns = actual_monotonic_ns();
    auto adapted = config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use
        ? reference_jump_duck_secondary_weapon_use_movement_command(sequence, wire)
        : config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use
        ? reference_jump_duck_weapon_use_movement_command(sequence, wire)
        : reference_jump_duck_weapon_movement_command(sequence, wire);
    if (!adapted) {
      reason = "unsupported_reference_command";
      return false;
    }
    const auto& effect=command_effects_[sequence.value()%command_effects_.size()];
    if(effect.sequence==sequence.value()) {
      if(effect.phase!=game_api::CommandMovementEffectPhase::before_movement &&
         effect.phase!=game_api::CommandMovementEffectPhase::after_movement) {
        reason="invalid_command_movement_effect";return false;
      }
      const std::array<float,3> delta{effect.impulse.x,effect.impulse.y,effect.impulse.z};
      const auto applied=effect.phase==game_api::CommandMovementEffectPhase::after_movement
          ? adapted.state->with_post_velocity_impulse(delta)
          : adapted.state->with_velocity_impulse(delta);
      if(!applied) {reason="invalid_command_movement_effect";return false;}adapted.state.emplace(*applied.state);
    }
    try {
      const auto before = candidate->current_predicted_state();
      const auto simulated = simulate_reference_movement(
          *before, *adapted.state, *prediction_environment_,
          *prediction_collision_, prediction_scratch_,
          prediction_movement_config_, prediction_support_motion_ ? &*prediction_support_motion_ : nullptr,
          config_.game_client->movement_policy().reference_ladder ? &prediction_brush_context_ : nullptr,
          config_.game_client->movement_policy().reference_ladder ? &*config_.game_client->movement_policy().reference_ladder : nullptr);
      if (!simulated) {
        if (config_.movement_diagnostics) {
          prediction_last_simulation_error_ = simulated.error ?
              std::optional{simulated.error->code} : std::nullopt;
          prediction_collision_trace_evidence_ = prediction_scratch_.last_reference_collision_trace;
          prediction_collision_position_evidence_ = prediction_scratch_.last_reference_collision_position;
          prediction_collision_query_hull_ = prediction_scratch_.last_reference_collision_hull;
          prediction_collision_query_start_ = prediction_scratch_.last_reference_collision_start;
          prediction_collision_query_end_ = prediction_scratch_.last_reference_collision_end;
          const bool movement_solid = simulated.error &&
              (simulated.error->code == movement::LocalMovementSimulationErrorCode::player_allsolid ||
               simulated.error->code == movement::LocalMovementSimulationErrorCode::player_startsolid);
          prediction_collision_query_purpose_=movement_solid ? "movement_step" :
              prediction_collision_trace_evidence_ ? "support_carry" :
              prediction_collision_position_evidence_ ? "support_clearance" : "movement_step";
        }
        reason = simulated.error ? movement::to_string(simulated.error->code) : "reference_movement_or_collision_failed";
        return false;
      }
      auto after = std::make_shared<const hlclient::movement::
          LocalPlayerMovementState>(*simulated.state);
      auto command = std::make_shared<const GoldSrcUserCmdState>(*adapted.state);
      const prediction::PredictedCommandAppend entry{
          command, before, after, simulated.statistics,
          prediction::summarize_prediction_touches(
              simulated.touches, false, false)};
      const auto appended = prediction::append_local_prediction_commands(
          *candidate, std::span<const prediction::PredictedCommandAppend>{
              &entry, 1U});
      if (!appended) {
        reason = "prediction_history_or_command_gap";
        return false;
      }
      candidate = appended.history;
      if (config_.movement_diagnostics && replay) {
        ++movement_diagnostics_.replayed_commands;
        if (movement_diagnostics_.replayed_simulation_ms)
          *movement_diagnostics_.replayed_simulation_ms += command->msec();
      }
      prediction_step_selections_ += simulated.statistics.step_success_count;
      publish_movement_audio(*before,*after,*command,sampled_at,replay);
      replay_duration.complete();
      return true;
    } catch (const std::bad_alloc&) {
      reason = "prediction_allocation_failed";
      return false;
    }
  }

  void predict_committed_reference_command(
      const GoldSrcUserCmdSequence sequence,
      const GoldSrcWireUserCmd& wire,
      const LiveRuntimeStageTimePoint sampled_at) {
    if (prediction_state_ != LiveReferencePredictionState::active ||
        !prediction_history_)
      return;
    if (!prediction_last_anchor_at_ ||
        sampled_at - *prediction_last_anchor_at_ >
            std::chrono::milliseconds{250}) {
      suspend_reference_prediction("reference_correction_horizon_exceeded");
      return;
    }
    auto candidate = prediction_history_;
    std::string_view reason;
    if (!append_reference_prediction_command(candidate, sequence, wire, reason,
            sampled_at,false)) {
      suspend_reference_prediction(reason);
      return;
    }
    prediction_history_ = std::move(candidate);
    ++prediction_local_steps_;
  }

  void accept_reference_prediction_seed(
      const ReferencePredictionSeed& seed,
      const ReferencePredictionAnchorResult& anchor,
      const LiveRuntimeStageTimePoint now) {
    if (!prediction_collision_ || !prediction_environment_ ||
        !anchor.last_new_value || !usercmd_stage_)
      return;
    if (config_.movement_diagnostics) {
      prediction_seed_origin_ = assets::AssetVector3{
          static_cast<float>(*seed.origin.x), static_cast<float>(*seed.origin.y),
          static_cast<float>(*seed.origin.z)};
      prediction_collision_position_evidence_.reset();
      prediction_collision_trace_evidence_.reset();
      prediction_collision_query_hull_.reset();
      prediction_collision_query_start_ = prediction_seed_origin_;
      prediction_collision_query_end_ = prediction_seed_origin_;
      prediction_collision_query_purpose_="not_queried";
      prediction_matched_origin_before_correction_.reset();
      prediction_matched_velocity_before_correction_.reset();
    }
    // An inactive owner must not recover by repeatedly replaying stale ACK
    // suffixes. Preflight before ground queries, simulation, or audio rewind,
    // using the existing 250 ms horizon and actual sampled deadlines (sampling
    // may skip wall time). Continuously fresh accepted state can legitimately
    // repeat an older transport ACK; it keeps the existing 64-command rebase
    // contract rather than treating ACK sample age as authoritative state age.
    const bool recovering = prediction_state_ != LiveReferencePredictionState::active ||
        !prediction_history_ || !prediction_last_anchor_at_ || now < *prediction_last_anchor_at_ ||
        now - *prediction_last_anchor_at_ > std::chrono::milliseconds{250};
    const auto history = usercmd_stage_->history();
    const auto suffix_preflight = [&]() -> std::string_view {
      if (seed.generation != kLiveRuntimeGeneration ||
          anchor.generation != seed.generation || history.generation() != seed.generation ||
          !anchor.last_new_command || *anchor.last_new_command != seed.command_boundary ||
          seed.command_boundary.value() <= prediction_life_command_floor_)
        return "reference_seed_command_unavailable";
      const auto* boundary = history.find(seed.command_boundary);
      if (!boundary || !boundary->reference_command)
        return "reference_seed_command_unavailable";
      const auto& sampled = command_sample_times_[seed.command_boundary.value() % command_sample_times_.size()];
      if (sampled.sequence != seed.command_boundary.value())
        return "reference_seed_time_invalid";
      auto previous_sample = LiveRuntimeStageTimePoint{std::chrono::nanoseconds{sampled.end_nanoseconds}};
      if (now < previous_sample)
        return "reference_seed_time_invalid";
      if (recovering && now - previous_sample > std::chrono::milliseconds{250})
        return "reference_seed_horizon_exceeded";
      auto expected = seed.command_boundary.value();
      std::size_t commands = 0U;
      unsigned simulation_milliseconds = 0U;
      for (const auto& entry : history.entries()) {
        const auto sequence = entry.sequence().value();
        if (sequence <= seed.command_boundary.value()) continue;
        if (expected == UINT32_MAX || sequence != expected + 1U ||
            !entry.reference_command || entry.reference_command->msec == 0U ||
            entry.reference_command->msec > 50U || ++commands > 64U)
          return "reference_suffix_unavailable";
        simulation_milliseconds += entry.reference_command->msec;
        if (recovering && simulation_milliseconds > 250U)
          return "reference_suffix_horizon_exceeded";
        const auto& sample = command_sample_times_[sequence % command_sample_times_.size()];
        const auto at = LiveRuntimeStageTimePoint{std::chrono::nanoseconds{sample.end_nanoseconds}};
        if (sample.sequence != sequence || at <= previous_sample || at > now)
          return "reference_suffix_time_invalid";
        previous_sample = at;
        expected = sequence;
      }
      return {};
    }();
    if (!suffix_preflight.empty()) {
      if (config_.movement_diagnostics) prediction_rebase_reason_ = suffix_preflight;
      suspend_reference_prediction(suffix_preflight);
      return;
    }
    if (seed.maximum_speed && *seed.maximum_speed > 0.0 &&
        std::abs(*seed.maximum_speed -
            static_cast<double>(prediction_environment_->maximum_speed())) >
            0.01) {
      suspend_reference_prediction("client_maxspeed_differs_from_movevars");
      return;
    }
    const GoldSrcUserCmdState* post_boundary_command = nullptr;
    std::optional<GoldSrcUserCmdState> retained_post_boundary_command;
    if (prediction_history_) {
      if (const auto* entry = prediction_history_->find_exact(seed.command_boundary))
        post_boundary_command = entry->command().get();
    }
    // A repeated anchor has no retained command entry; a new initial history
    // can likewise begin at a just-submitted post-think endpoint. The exact
    // bounded command sidecar can witness it without replaying the game action.
    // Keep the same generation/life/carrier gates as the accepted seed owner.
    if (!post_boundary_command && seed.generation == kLiveRuntimeGeneration &&
        anchor.generation == seed.generation && anchor.last_new_command &&
        *anchor.last_new_command == seed.command_boundary &&
        seed.command_boundary.value() > prediction_life_command_floor_) {
      const auto& effect = command_effects_[seed.command_boundary.value() % command_effects_.size()];
      if (effect.sequence == seed.command_boundary.value() &&
          effect.phase == game_api::CommandMovementEffectPhase::after_movement &&
          effect.impulse.z > 0.0F) {
        auto adapted = config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use
            ? reference_jump_duck_secondary_weapon_use_movement_command(seed.command_boundary, *anchor.last_new_value)
            : config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use
            ? reference_jump_duck_weapon_use_movement_command(seed.command_boundary, *anchor.last_new_value)
            : reference_jump_duck_weapon_movement_command(seed.command_boundary, *anchor.last_new_value);
        if (adapted && adapted.state) {
          const auto applied = adapted.state->with_post_velocity_impulse(
              {effect.impulse.x, effect.impulse.y, effect.impulse.z});
          if (applied && applied.state) {
            retained_post_boundary_command.emplace(*applied.state);
            post_boundary_command = &*retained_post_boundary_command;
          }
        }
      }
    }
    const auto derived = derive_reference_prediction_ground(
        seed, *anchor.last_new_value, *prediction_collision_,
        prediction_scratch_, prediction_movement_config_,
        hlclient::movement::GoldSrcMovementCommandProfile::
            reference_wire_jump_duck_v2,
        config_.game_client->movement_policy().reference_ladder ? &prediction_brush_context_ : nullptr,
        post_boundary_command);
    prediction_last_ground_status_ = derived.status;
    if (config_.movement_diagnostics) {
      prediction_collision_position_evidence_ = derived.position_evidence;
      prediction_collision_trace_evidence_ = derived.trace_evidence;
      prediction_collision_query_hull_ = derived.queried_hull;
      if (derived.queried_origin) prediction_collision_query_start_ = derived.queried_origin;
      if(derived.position_evidence)
        prediction_collision_query_purpose_=derived.trace_evidence ? "seed_ground_probe" : "seed_position";
      if (derived.trace_evidence && derived.queried_origin)
        prediction_collision_query_end_ = assets::AssetVector3{
            derived.queried_origin->x, derived.queried_origin->y,
            static_cast<float>(static_cast<double>(derived.queried_origin->z) -
                prediction_movement_config_.ground_probe_distance)};
    }
    if (!derived.state) {
      suspend_reference_prediction(to_string(derived.status));
      return;
    }
    // The seed contains server pusher displacement through this source time.
    // Anchor command duration here, not to packet receipt time or render time.
    auto corrected_info = hlclient::movement::local_player_movement_state_create_info(*derived.state);
    if (prediction_brush_context_.server_time && prediction_brush_context_.record == seed.record_identity &&
        *prediction_brush_context_.server_time < 1e9)
      corrected_info.simulation_time_nanoseconds = static_cast<std::uint64_t>(
          std::llround(*prediction_brush_context_.server_time * 1e9));
    else prediction_support_motion_.reset();
    const auto timed_correction = hlclient::movement::LocalPlayerMovementState::create(corrected_info);
    if (!timed_correction) { suspend_reference_prediction("support_time_invalid"); return; }
    const auto& corrected = *timed_correction.state;
    if (prediction_history_) {
      const hlclient::movement::LocalPlayerMovementState* matched = nullptr;
      const auto boundary = corrected.source_command_sequence();
      if (prediction_history_->anchor().movement_state()->source_command_sequence() == boundary)
        matched = prediction_history_->anchor().movement_state().get();
      else if (const auto sequence = GoldSrcUserCmdSequence::create(boundary))
        if (const auto* exact = prediction_history_->find_exact(*sequence)) matched = exact->post_command_state().get();
      if(config_.movement_diagnostics && matched) {
        prediction_matched_origin_before_correction_ = matched->origin();
        prediction_matched_velocity_before_correction_ = matched->velocity();
      }
      const auto before_presentation = live_reference_prediction_snapshot(now);
      const auto before_camera = before_presentation.presented_origin;
      const auto before_camera_offset =
          before_presentation.presented_view_offset;
      const auto old_latest_state = prediction_history_->current_predicted_state();
      const auto old_epoch = prediction_epoch_;
      const auto rebased = [&] {
        const MovementCostScope cost{config_.movement_diagnostics,
            movement_diagnostics_.prediction_replay_ms};
        if (config_.movement_diagnostics)
          movement_diagnostics_.prediction_use_ns = actual_monotonic_ns();
        return rebase_reference_prediction(
            *prediction_history_, corrected, *prediction_environment_,
            *prediction_collision_, prediction_scratch_,
            prediction_movement_config_, prediction_support_motion_ ? &*prediction_support_motion_ : nullptr,
            config_.game_client->movement_policy().reference_ladder ? &prediction_brush_context_ : nullptr,
            config_.game_client->movement_policy().reference_ladder ? &*config_.game_client->movement_policy().reference_ladder : nullptr);
      }();
      if (config_.movement_diagnostics) {
        movement_diagnostics_.replayed_commands += rebased.replayed_commands;
        if (rebased.history) {
          if (movement_diagnostics_.replayed_simulation_ms)
            for (const auto& replayed : rebased.history->entries())
              *movement_diagnostics_.replayed_simulation_ms += replayed.command()->msec();
        } else movement_diagnostics_.replayed_simulation_ms.reset();
        prediction_rebase_reason_ = to_string(rebased.status);
        prediction_last_simulation_error_ = rebased.simulation_error;
      }
      if (!rebased.history) {
        if (config_.movement_diagnostics) {
          prediction_collision_trace_evidence_ = prediction_scratch_.last_reference_collision_trace;
          prediction_collision_position_evidence_ = prediction_scratch_.last_reference_collision_position;
          prediction_collision_query_hull_ = prediction_scratch_.last_reference_collision_hull;
          prediction_collision_query_start_ = prediction_scratch_.last_reference_collision_start;
          prediction_collision_query_end_ = prediction_scratch_.last_reference_collision_end;
          const bool movement_solid = rebased.simulation_error &&
              (*rebased.simulation_error == movement::LocalMovementSimulationErrorCode::player_allsolid ||
               *rebased.simulation_error == movement::LocalMovementSimulationErrorCode::player_startsolid);
          prediction_collision_query_purpose_=movement_solid ? "movement_replay" :
              prediction_collision_trace_evidence_ ? "support_carry" :
              prediction_collision_position_evidence_ ? "support_clearance" : "movement_replay";
        }
        suspend_reference_prediction("reference_rebase_or_suffix_failed");
        return;
      }
      // The visual scope is separate from simulation validity. A successful
      // rebase already validated the authoritative endpoint and saved suffix.
      // An unexplained displacement must snap, not discard that exact history
      // and wait for jump/duck release before it can be seeded again.
      constexpr double kMaximumCorrectionForSmoothView = 16.0;
      if (rebased.raw_position_error) {
        prediction_last_error_ = rebased.raw_position_error;
        prediction_maximum_error_ = std::max(prediction_maximum_error_.value_or(0.0),
            *rebased.raw_position_error);
      }
      const bool velocity_correction = rebased.raw_position_error && matched &&
          verified_server_velocity_correction(*matched, corrected, seed, now, *rebased.raw_position_error);
      if (!rebased.raw_position_error || !std::isfinite(*rebased.raw_position_error)) {
        prediction_last_error_ = rebased.raw_position_error;
        suspend_reference_prediction("reference_correction_measurement_missing");
        return;
      }
      const bool hard_correction_snap =
          *rebased.raw_position_error > kMaximumCorrectionForSmoothView && !velocity_correction;
      if (hard_correction_snap) {
        prediction_visual_correction_.reset();
        prediction_server_velocity_visual_ = false;
        prediction_server_velocity_pose_.reset();
        prediction_exterior_correction_source_.reset();
        prediction_presentation_cache_.reset();
        if (config_.movement_diagnostics) prediction_rebase_reason_ = "ready_hard_correction_snap";
      }
      prediction_last_error_ = rebased.raw_position_error;
      prediction_maximum_error_ = std::max(
          prediction_maximum_error_.value_or(0.0),
          *rebased.raw_position_error);
      prediction_replayed_commands_ += rebased.replayed_commands;
      const auto retained_pre_anchor=prediction_pre_anchor_;
      prediction_pre_anchor_.reset();
      if (!hard_correction_snap && prediction_support_motion_ && prediction_support_motion_->active()) {
        std::shared_ptr<const hlclient::movement::LocalPlayerMovementState> predecessor;
        std::shared_ptr<const hlclient::movement::LocalPlayerMovementState> previous_boundary;
        if (const auto* entry = prediction_history_->find_exact(*GoldSrcUserCmdSequence::create(corrected.source_command_sequence()))) {
          predecessor=entry->pre_command_state();
          previous_boundary=entry->post_command_state();
        } else if (retained_pre_anchor && retained_pre_anchor->source_command_sequence()+1U==corrected.source_command_sequence()) {
          predecessor=retained_pre_anchor;
          previous_boundary=prediction_history_->anchor().movement_state();
        }
        const auto checked_support_pair=[&]() {
          // A corrected latest-command ACK can consume the history endpoint
          // before H3 has rendered its predecessor. Keep that pair for a small
          // vertical support correction only when its complete contact identity
          // and collision-checked presentation path are still witnessed.
          using hlclient::movement::PlayerMovementMode;
          if (!predecessor || !previous_boundary ||
              predecessor->source_command_sequence()+1U!=corrected.source_command_sequence() ||
              predecessor->source_command_sequence()<=prediction_life_command_floor_ ||
              previous_boundary->source_command_sequence()!=corrected.source_command_sequence() ||
              predecessor->mode()!=PlayerMovementMode::walking ||
              previous_boundary->mode()!=PlayerMovementMode::walking ||
              corrected.mode()!=PlayerMovementMode::walking ||
              predecessor->hull()!=corrected.hull() || previous_boundary->hull()!=corrected.hull() ||
              predecessor->in_duck_transition() || previous_boundary->in_duck_transition() || corrected.in_duck_transition() ||
              corrected.simulation_time_nanoseconds()<=predecessor->simulation_time_nanoseconds() ||
              corrected.simulation_time_nanoseconds()-predecessor->simulation_time_nanoseconds()>250'000'000ULL)
            return false;
          const auto same_vector=[](const auto& a,const auto& b) { return a.x==b.x && a.y==b.y && a.z==b.z; };
          if (!same_vector(predecessor->view_offset(),corrected.view_offset()) ||
              !same_vector(previous_boundary->view_offset(),corrected.view_offset()) ||
              std::abs(previous_boundary->origin().x-corrected.origin().x)>.25F ||
              std::abs(previous_boundary->origin().y-corrected.origin().y)>.25F)
            return false;
          const auto& ground=corrected.ground_state();
          const auto matching_ground=[&](const auto& candidate) {
            return candidate.grounded() && candidate.walkable() && candidate.hit()==ground.hit() &&
                candidate.plane().source_plane_index==ground.plane().source_plane_index &&
                same_vector(candidate.plane().normal,ground.plane().normal);
          };
          if (!ground.grounded() || !ground.walkable() || !ground.hit() ||
              ground.hit()->kind!=hlclient::movement::PlayerMovementHitKind::brush_entity ||
              !ground.hit()->stable_instance_ordinal || !ground.hit()->source_entity_index ||
              !ground.plane().source_plane_index || ground.plane().normal.z<=0.F ||
              !matching_ground(predecessor->ground_state()) || !matching_ground(previous_boundary->ground_state()))
            return false;
          const auto velocity=prediction_support_motion_->velocity_z_for(*ground.hit());
          const double contact_delta=(ground.plane().distance-predecessor->ground_state().plane().distance)/ground.plane().normal.z;
          const double previous_delta=(previous_boundary->ground_state().plane().distance-predecessor->ground_state().plane().distance)/ground.plane().normal.z;
          // Match the existing support sampler's 256 ups / 50 ms contact bound.
          // An observed reversal must not bridge an old ascending/descending pair.
          if (!velocity || *velocity==0. || !std::isfinite(contact_delta) || !std::isfinite(previous_delta) ||
              std::abs(contact_delta)>256.*.050 || contact_delta * *velocity<0. || previous_delta * *velocity<0.)
            return false;
          try {
            for (const double alpha:{0.,.5,1.}) {
              const auto sample=sample_reference_support_presentation(*predecessor,corrected,alpha,
                  &*prediction_support_motion_,prediction_presentation_scratch_,prediction_movement_config_.collision_query);
              if (!sample.scene) return false;
              movement::BrushSceneMovementCollision collision{sample.scene,prediction_support_motion_->entity_boxes()};
              const auto position=collision.test_position(sample.origin,corrected.hull(),prediction_presentation_scratch_,prediction_movement_config_.collision_query);
              if (!position || position.result->status!=movement::LocalMovementPositionStatus::free) return false;
              const auto route=collision.trace_hull(sample.trace_start,sample.origin,corrected.hull(),prediction_presentation_scratch_,prediction_movement_config_.collision_query);
              if (!route || route.result->start_solid || route.result->all_solid || route.result->fraction<1.) return false;
              auto down=sample.origin;down.z-=static_cast<float>(prediction_movement_config_.ground_probe_distance);
              const auto support=collision.trace_hull(sample.origin,down,corrected.hull(),prediction_presentation_scratch_,prediction_movement_config_.collision_query);
              if (!support || support.result->start_solid || support.result->all_solid || support.result->fraction>=1. ||
                  support.result->hit!=ground.hit() || !support.result->collision_plane ||
                  support.result->collision_plane->source_plane_index!=ground.plane().source_plane_index ||
                  !same_vector(support.result->collision_plane->normal,ground.plane().normal)) return false;
            }
          } catch (const std::exception&) { return false; }
          return true;
        };
        if (*rebased.raw_position_error<=.25 || checked_support_pair())
          prediction_pre_anchor_=std::move(predecessor);
      }
      // before_camera already includes the residual. Remove it before sampling
      // the new physical eye, then retarget once with the ORIGINAL deadline.
      // Ordinary ACKs must neither double the residual nor prolong its life.
      const double now_seconds = std::chrono::duration<double>{now.time_since_epoch()}.count();
      std::optional<double> velocity_visual_deadline;
      const bool continuing_velocity_visual = prediction_server_velocity_visual_ &&
          prediction_visual_correction_ && prediction_visual_correction_->active();
      if (continuing_velocity_visual) {
        const double end = prediction_visual_correction_->start_monotonic_time_seconds() +
            prediction_visual_correction_->duration_seconds();
        if (end > now_seconds && matched && compatible_velocity_correction_pose(*matched, corrected) &&
            velocity_correction_context_continuous(seed.receiving_entity_number) &&
            (velocity_correction || *rebased.raw_position_error <= kMaximumCorrectionForSmoothView))
          velocity_visual_deadline = end;
        prediction_visual_correction_.reset();
        prediction_server_velocity_visual_ = false;
        prediction_server_velocity_pose_.reset();
        prediction_exterior_correction_source_.reset();
        prediction_presentation_cache_.reset();
      }
      if (velocity_correction && !velocity_visual_deadline && !continuing_velocity_visual)
        velocity_visual_deadline = now_seconds + .100;
      if (velocity_correction && !continuing_velocity_visual) {
        prediction_visual_correction_.reset();
        prediction_exterior_correction_source_.reset();
        prediction_presentation_cache_.reset();
      }
      prediction_history_ = rebased.history;
      config_.game_client->rewind_movement_audio(corrected.source_command_sequence());
      for (const auto& entry:prediction_history_->entries())
        publish_movement_audio(*entry.pre_command_state(),
            *entry.post_command_state(),*entry.command(),now,true);
      ++prediction_accepted_corrections_;
      prediction_epoch_ = prediction_history_->session().prediction_generation;
      prediction_last_record_identity_ = seed.record_identity;
      prediction_last_anchor_at_ = now;
      prediction_last_view_angle_source_ = target_.runtime_observation()->view_angle_correction
          ? std::optional{target_.runtime_observation()->view_angle_correction->source} : std::nullopt;
      if (config_.movement_diagnostics && prediction_fallback_started_at_) {
        prediction_last_fallback_duration_ms_ = std::chrono::duration<double, std::milli>{
            now - *prediction_fallback_started_at_}.count();
        prediction_fallback_started_at_.reset();
      }
      prediction_state_ = LiveReferencePredictionState::active;
      prediction_reason_ = "active";
      const auto after_physical = live_reference_prediction_snapshot(now);
      auto after_camera = after_physical.presented_origin;
      auto after_camera_offset = after_physical.presented_view_offset;
      if (velocity_visual_deadline && before_camera && before_camera_offset && after_camera &&
          after_camera_offset && compatible_velocity_correction_pose(*old_latest_state,
              *prediction_history_->current_predicted_state())) {
        prediction::PredictionVisualCorrectionConfig visual_config;
        visual_config.duration_seconds = std::clamp(*velocity_visual_deadline - now_seconds, 0., .100);
        visual_config.maximum_duration_seconds = .100;
        visual_config.maximum_offset_magnitude = 32.;
        const assets::AssetVector3 old_eye{before_camera->x+before_camera_offset->x,
            before_camera->y+before_camera_offset->y,before_camera->z+before_camera_offset->z};
        const assets::AssetVector3 new_eye{after_camera->x+after_camera_offset->x,
            after_camera->y+after_camera_offset->y,after_camera->z+after_camera_offset->z};
        const auto visual = prediction::begin_prediction_visual_correction(std::nullopt,old_eye,new_eye,
            prediction::PredictionCorrectionClass::small_visual_correction,now_seconds,
            prediction_accepted_corrections_,old_epoch,prediction_epoch_,visual_config);
        if (visual && visual.correction && visual.correction->active()) {
          prediction_visual_correction_.emplace(std::move(*visual.correction));
          prediction_server_velocity_visual_ = true;
          prediction_server_velocity_pose_ = prediction_history_->current_predicted_state();
          prediction_server_velocity_entity_ = seed.receiving_entity_number;
          prediction_presentation_cache_.reset();
          const auto sample = live_reference_prediction_snapshot(now);
          after_camera = sample.presented_origin; after_camera_offset = sample.presented_view_offset;
        }
      }
      if (before_camera && after_camera &&
          after_physical.presentation_reason == "anchor_before_history" &&
          *rebased.raw_position_error <= 0.25 &&
          (!prediction_visual_correction_ ||
           !prediction_visual_correction_->active()) &&
          prediction_camera_collision_query_ &&
          old_latest_state->hull() ==
              prediction_history_->current_predicted_state()->hull() &&
          old_latest_state->mode() ==
              prediction_history_->current_predicted_state()->mode()) {
        const auto& old_offset = old_latest_state->view_offset();
        const auto& new_offset =
            prediction_history_->current_predicted_state()->view_offset();
        const auto dx = static_cast<double>(before_camera->x) - after_camera->x;
        const auto dy = static_cast<double>(before_camera->y) - after_camera->y;
        const auto dz = static_cast<double>(before_camera->z) - after_camera->z;
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (old_offset.x == new_offset.x && old_offset.y == new_offset.y &&
            old_offset.z == new_offset.z && distance > 0.25 &&
            distance <= 4.0) {
          prediction::PredictionVisualCorrectionConfig visual_config;
          visual_config.duration_seconds = 0.020;
          visual_config.maximum_duration_seconds = 0.020;
          visual_config.maximum_offset_magnitude = 4.0;
          const assets::AssetVector3 old_eye{
              before_camera->x + new_offset.x,
              before_camera->y + new_offset.y,
              before_camera->z + new_offset.z};
          const assets::AssetVector3 new_eye{
              after_camera->x + new_offset.x,
              after_camera->y + new_offset.y,
              after_camera->z + new_offset.z};
          const auto visual = prediction::begin_prediction_visual_correction(
              std::nullopt, old_eye, new_eye,
              prediction::PredictionCorrectionClass::small_visual_correction,
              std::chrono::duration<double>{now.time_since_epoch()}.count(),
              prediction_accepted_corrections_, old_epoch, prediction_epoch_,
              visual_config);
          if (visual && visual.correction) {
            prediction_visual_correction_.emplace(std::move(*visual.correction));
            prediction_presentation_cache_.reset();
            const auto corrected_presentation =
                live_reference_prediction_snapshot(now);
            after_camera = corrected_presentation.presented_origin;
            after_camera_offset =
                corrected_presentation.presented_view_offset;
          }
        }
      }
      if (before_camera && before_camera_offset && after_camera &&
          after_camera_offset) {
        const auto dx = static_cast<double>(before_camera->x +
            before_camera_offset->x) -
            static_cast<double>(after_camera->x + after_camera_offset->x);
        const auto dy = static_cast<double>(before_camera->y +
            before_camera_offset->y) -
            static_cast<double>(after_camera->y + after_camera_offset->y);
        const auto dz = static_cast<double>(before_camera->z +
            before_camera_offset->z) -
            static_cast<double>(after_camera->z + after_camera_offset->z);
        prediction_last_camera_correction_jump_ =
            std::sqrt(dx * dx + dy * dy + dz * dz);
        prediction_maximum_camera_correction_jump_ = std::max(
            prediction_maximum_camera_correction_jump_.value_or(0.0),
            *prediction_last_camera_correction_jump_);
        if (prediction_presentation_cache_) {
          prediction_presentation_cache_->last_camera_correction_jump =
              prediction_last_camera_correction_jump_;
          prediction_presentation_cache_->maximum_camera_correction_jump =
              prediction_maximum_camera_correction_jump_;
        }
      }
      return;
    }
    const auto next_epoch = prediction_epoch_ == UINT64_MAX
        ? 0U : prediction_epoch_ + 1U;
    const auto session = prediction::create_prediction_session_identity(
        seed.generation, next_epoch, *prediction_collision_,
        *prediction_environment_, prediction_movement_config_, corrected,
        (config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use ||
         config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use)
            ? prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4
            : prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_v3,
        prediction::PredictionAcknowledgementProfile::
            reference_sent_carrier_boundary_v1);
    if (!session) {
      suspend_reference_prediction("reference_session_identity_invalid");
      return;
    }
    auto initial = prediction::LocalPredictionHistoryState::create_initial(
        corrected, *session.session);
    if (!initial) {
      suspend_reference_prediction("reference_initial_history_failed");
      return;
    }
    auto candidate = initial.history;
    config_.game_client->rewind_movement_audio(seed.command_boundary.value());
    std::size_t appended = 0U;
    std::string_view reason;
    for (const auto& entry : history.entries()) {
      if (entry.sequence().value() <= seed.command_boundary.value())
        continue;
      if (!entry.reference_command || appended >= 64U ||
          !append_reference_prediction_command(
              candidate, entry.sequence(), *entry.reference_command, reason,now,true)) {
        suspend_reference_prediction(reason.empty()
            ? "initial_suffix_unavailable" : reason);
        return;
      }
      ++appended;
    }
    prediction_history_ = std::move(candidate);
    prediction_epoch_ = next_epoch;
    prediction_local_steps_ += appended;
    prediction_last_record_identity_ = seed.record_identity;
    prediction_last_anchor_at_ = now;
    prediction_last_view_angle_source_ = target_.runtime_observation()->view_angle_correction
        ? std::optional{target_.runtime_observation()->view_angle_correction->source} : std::nullopt;
    if (config_.movement_diagnostics && prediction_fallback_started_at_) {
      prediction_last_fallback_duration_ms_ = std::chrono::duration<double, std::milli>{
          now - *prediction_fallback_started_at_}.count();
      prediction_fallback_started_at_.reset();
    }
    prediction_state_ = LiveReferencePredictionState::active;
    prediction_reason_ = "active";
  }

  [[nodiscard]] bool rolling_usercmd_diagnostics() const noexcept {
    return config_.operation_mode == LiveRuntimeOperationMode::live_visual_control &&
        config_.live_visual_input_source == LiveVisualControlInputSource::keyboard_mouse;
  }

  void record_usercmd_server_sample(
      const client::RuntimeClientObservationState &observation,
      const LiveRuntimeStageTimePoint now) {
    if (!usercmd_activated_at_ ||
        observation.client_metadata.generation != kLiveRuntimeGeneration ||
        observation.client_metadata.freshness !=
            client::RuntimeObservationFreshness::observed_in_record ||
        !observation.client_metadata.source || !observation.receiving_client) {
      return;
    }
    const auto &source = *observation.client_metadata.source;
    if (!usercmd_server_samples_.empty() &&
        usercmd_server_samples_.back().source == source) {
      return;
    }
    if (observation.lifecycle.dead()) {
      suspend_reference_prediction("local_player_dead");
    } else if (config_.operation_mode ==
        LiveRuntimeOperationMode::live_visual_control) {
      const auto anchor = prediction_carriers_.bind_observation(
          observation, client_slot_,
          config_.game_client->movement_policy().coherent_unfragmented_movement_frames);
      prediction_last_anchor_status_ = anchor.status;
      if (!anchor.bound() && prediction_state_ ==
              LiveReferencePredictionState::active &&
          prediction_last_anchor_at_ &&
          now - *prediction_last_anchor_at_ > std::chrono::milliseconds{250})
        suspend_reference_prediction("reference_anchor_timeout");
      if (anchor.bound() && anchor.last_new_command &&
          anchor.last_new_command->value() > prediction_life_command_floor_) {
        ++prediction_anchor_bindings_;
        prediction_anchor_coherent_reliable_frame_ = anchor.coherent_reliable_frame;
        prediction_last_command_boundary_ =
            anchor.last_new_command->value();
        const auto movement_command = config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use
            ? reference_jump_duck_secondary_weapon_use_movement_command(*anchor.last_new_command, *anchor.last_new_value)
            : config_.game_client->movement_policy().button_policy == GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use
            ? reference_jump_duck_weapon_use_movement_command(*anchor.last_new_command, *anchor.last_new_value)
            : reference_jump_duck_weapon_movement_command(*anchor.last_new_command, *anchor.last_new_value);
        if (movement_command) {
          ++prediction_command_candidates_;
          prediction_last_command_error_.reset();
        } else if (movement_command.error) {
          prediction_last_command_error_ = movement_command.error->code;
        }
        const auto seed = inspect_reference_prediction_seed(
            observation, client_slot_, anchor, {},
            config_.game_client->movement_policy().crouch_jump_physics_key);
        prediction_last_seed_status_ = seed.status;
        prediction_last_seed_field_ = seed.field;
        if (seed.seed) ++prediction_seed_candidates_;
        if (config_.reference_prediction && prediction_collision_ &&
            (prediction_history_ || observation.lifecycle.respawns == 0U ||
             (anchor.last_new_value->buttons &
                 (kReferenceGoldSrcButtonJump | kReferenceGoldSrcButtonDuck)) == 0U)) {
          std::optional<ReferenceRetainedPredictionButtons> retained;
          if (prediction_history_) {
            const auto boundary = anchor.last_new_command;
            if (boundary) {
              const hlclient::movement::LocalPlayerMovementState* matched =
                  nullptr;
              if (prediction_history_->anchor().movement_state()
                      ->source_command_sequence() == boundary->value())
                matched = prediction_history_->anchor().movement_state().get();
              else if (const auto* entry =
                           prediction_history_->find_exact(*boundary))
                matched = entry->post_command_state().get();
              if (matched)
                retained = ReferenceRetainedPredictionButtons{
                    *boundary, matched->old_buttons(),
                    matched->duck_time_milliseconds(),
                    matched->in_duck_transition()};
            }
          }
          const auto active_seed = inspect_reference_prediction_seed(
              observation, client_slot_, anchor, retained,
              config_.game_client->movement_policy().crouch_jump_physics_key);
          prediction_last_seed_status_ = active_seed.status;
          prediction_last_seed_field_ = active_seed.field;
          if (active_seed.seed) {
            try {
              accept_reference_prediction_seed(*active_seed.seed, anchor, now);
            } catch (const std::exception&) {
              suspend_reference_prediction("reference_seed_or_collision_exception");
            }
          }
          else if (prediction_state_ == LiveReferencePredictionState::active) {
            const bool transient = active_seed.status ==
                    ReferencePredictionSeedStatus::player_entity_unavailable ||
                active_seed.status ==
                    ReferencePredictionSeedStatus::missing_semantic_field ||
                active_seed.status ==
                    ReferencePredictionSeedStatus::clientdata_unavailable;
            if (!transient || !prediction_last_anchor_at_ ||
                now - *prediction_last_anchor_at_ >
                    std::chrono::milliseconds{250})
              suspend_reference_prediction(to_string(active_seed.status));
          }
          else {
            prediction_state_ = LiveReferencePredictionState::waiting_for_seed;
            prediction_reason_ = to_string(active_seed.status);
          }
        }
      }
    }
    if (usercmd_server_samples_.size() >=
        config_.usercmd_scenario.maximum_samples) {
      if (!rolling_usercmd_diagnostics()) {
        fail(LiveRuntimeStageErrorCode::usercmd_observation_failed,
             LiveRuntimeStageState::backpressure,
             "Scripted usercmd server-sample diagnostic retention was exhausted", now);
        if (error_) error_->usercmd_diagnostic_code =
            LiveUserCmdDiagnosticErrorCode::server_sample_retention_exhausted;
        return;
      }
      usercmd_server_samples_.pop_front();
      ++server_samples_evicted_;
    }
    const auto offset =
        now > *usercmd_activated_at_
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                  now - *usercmd_activated_at_)
            : std::chrono::nanoseconds{0};
    const bool keyboard =
        config_.operation_mode ==
            LiveRuntimeOperationMode::live_visual_control &&
        config_.live_visual_input_source ==
            LiveVisualControlInputSource::keyboard_mouse;
    const auto phase_index =
        keyboard ? 0U
                 : phase_index_for_offset(config_.usercmd_scenario, offset);
    const auto phase = phase_from_index(phase_index);
    usercmd_server_samples_.push_back(LiveUserCmdServerSample{
        kLiveRuntimeGeneration, observation.publication_revision, source,
        observation.server_time_seconds, observation.receiving_client->health,
        phase, observation.receiving_client->origin,
        observation.receiving_client->velocity,
        observation.receiving_client->view_offset,
        observation.client_metadata.freshness});
    ++server_sample_count_;
    ++fresh_samples_by_phase_[phase_index];
    const auto sampled = LiveRuntimeStageEvent{
        .type = LiveRuntimeStageEventType::usercmd_server_sample,
        .source_sequence = source.source_transport_sequence,
        .publication_revision = observation.publication_revision,
        .input_phase = phase,
        .occurred_at = now};
    push_event(sampled);
    emit_trace(sampled);
  }

  [[nodiscard]] LiveUserCmdMotionOutcome
  evaluate_usercmd_motion() const {
    const bool side_check =
        config_.live_visual_input_source ==
        LiveVisualControlInputSource::scripted_side_check;
    const std::vector<LiveUserCmdServerSample> samples{
        usercmd_server_samples_.begin(), usercmd_server_samples_.end()};
    return evaluate_live_usercmd_motion(
        samples, sent_by_phase_,
        config_.usercmd_scenario.fixed_yaw_degrees +
            (side_check ? 90.0 : 0.0),
        config_.usercmd_scenario.velocity_tolerance,
        config_.usercmd_scenario.origin_tolerance);
  }

  [[nodiscard]] std::optional<LiveUserCmdCheckState>
  usercmd_snapshot() const {
    if (!usercmd_stage_ || !usercmd_activated_at_)
      return std::nullopt;
    const auto history = usercmd_stage_->history();
    LiveUserCmdCheckState result;
    result.generation = kLiveRuntimeGeneration;
    result.production_handoff_complete = true;
    result.same_driver_retained = true;
    result.schema_binding_current = true;
    result.sendents_complete = signon_reply_acknowledged_;
    result.terminal_rejection_observed = false;
    result.command_interval = config_.usercmd_scenario.command_interval;
    result.durations = config_.usercmd_scenario.durations;
    result.damage_respawn = config_.game_client->damage_respawn_snapshot();
    result.forward_amplitude = config_.usercmd_scenario.forward_amplitude;
    result.fixed_yaw_degrees =
        live_visual_input_ ? live_visual_input_->yaw_degrees
                           : config_.usercmd_scenario.fixed_yaw_degrees;
    result.fixed_pitch_degrees =
        live_visual_input_ ? live_visual_input_->pitch_degrees
                           : config_.usercmd_scenario.fixed_pitch_degrees;
    result.velocity_tolerance = config_.usercmd_scenario.velocity_tolerance;
    result.origin_tolerance = config_.usercmd_scenario.origin_tolerance;
    result.generated_command_count = generated_usercmd_count_;
    result.history_command_count = history.size();
    result.new_command_submission_count =
        usercmd_stage_->new_command_submission_count();
    result.backup_command_submission_count =
        usercmd_stage_->backup_command_submission_count();
    result.transmitted_packet_count =
        usercmd_stage_->transmitted_packet_count();
    result.server_sample_count = server_sample_count_;
    result.transmit_ranges_evicted = transmit_ranges_evicted_;
    result.server_samples_evicted = server_samples_evicted_;
    result.first_new_command_sequence = first_new_command_sequence_;
    result.last_new_command_sequence = last_new_command_sequence_;
    result.driver_rx_total = last_driver_rx_;
    result.driver_rx_at_input_activation = driver_rx_at_input_activation_;
    result.payload_events_consumed_total = payload_events_consumed_;
    result.payload_events_consumed_at_input_activation =
        payload_events_consumed_at_input_activation_;
    result.service_envelopes_decoded_total = service_envelopes_decoded_;
    result.service_envelopes_decoded_at_input_activation =
        service_envelopes_decoded_at_input_activation_;
    result.runtime_records_attempted_total = runtime_records_attempted_;
    result.runtime_records_attempted_at_input_activation =
        runtime_records_attempted_at_input_activation_;
    result.runtime_records_committed_total = applied_runtime_record_count_;
    result.runtime_records_committed_at_input_activation =
        runtime_records_committed_at_input_activation_;
    result.clientdata_records_committed_total = clientdata_record_count_;
    result.clientdata_records_committed_at_input_activation =
        clientdata_records_committed_at_input_activation_;
    result.reference_prediction_sent_bindings =
        prediction_sent_bindings_;
    result.reference_prediction_anchor_bindings =
        prediction_anchor_bindings_;
    result.reference_prediction_seed_candidates =
        prediction_seed_candidates_;
    result.reference_prediction_command_candidates =
        prediction_command_candidates_;
    result.reference_prediction_last_command_error =
        prediction_last_command_error_;
    result.reference_prediction_last_anchor_status =
        prediction_last_anchor_status_;
    result.reference_prediction_last_seed_status =
        prediction_last_seed_status_;
    result.reference_prediction_last_seed_field =
        prediction_last_seed_field_;
    result.reference_prediction_last_command_boundary =
        prediction_last_command_boundary_;
    result.last_sampled_forward_axis = last_sampled_forward_axis_;
    result.last_sampled_side_axis = last_sampled_side_axis_;
    result.nonzero_side_generated_count = nonzero_side_generated_count_;
    result.nonzero_side_new_submission_count =
        nonzero_side_new_submission_count_;
    result.jump_generated_count = jump_generated_count_;
    result.duck_generated_count = duck_generated_count_;
    result.jump_new_submission_count = jump_new_submission_count_;
    result.duck_new_submission_count = duck_new_submission_count_;
    result.attack_generated_count = attack_generated_count_;
    result.reload_generated_count = reload_generated_count_;
    result.attack_command_press_count = attack_command_press_count_;
    result.attack_command_release_count = attack_command_release_count_;
    result.reload_command_press_count = reload_command_press_count_;
    result.reload_command_release_count = reload_command_release_count_;
    result.attack_new_submission_count = attack_new_submission_count_;
    result.reload_new_submission_count = reload_new_submission_count_;
    result.use_generated_count = use_generated_count_;
    result.use_command_press_count = use_command_press_count_;
    result.use_command_release_count = use_command_release_count_;
    result.use_new_submission_count = use_new_submission_count_;
    result.use_clear_after_release_count = use_clear_after_release_count_;
    result.weapon_command_submission_drops =
        weapon_command_submission_drops_;
    result.first_jump_sent_sequence = first_jump_sent_sequence_;
    result.last_jump_sent_sequence = last_jump_sent_sequence_;
    result.first_duck_sent_sequence = first_duck_sent_sequence_;
    result.last_duck_sent_sequence = last_duck_sent_sequence_;
    result.jump_command_press_count = jump_command_press_count_;
    result.jump_command_release_count = jump_command_release_count_;
    result.duck_command_press_count = duck_command_press_count_;
    result.duck_command_release_count = duck_command_release_count_;
    result.last_a_held = live_visual_input_ && live_visual_input_->a_held;
    result.last_d_held = live_visual_input_ && live_visual_input_->d_held;
    const auto scheduler_state = usercmd_scheduler_->state();
    result.scheduler_initialized = scheduler_state.initialized;
    result.scheduler_active = usercmd_activated_at_.has_value();
    result.scheduler_activation_time_nanoseconds =
        usercmd_activation_time_ns_.value_or(0);
    result.scheduler_last_update_time_nanoseconds =
        scheduler_state.last_update_time_nanoseconds;
    result.scheduler_next_sample_time_nanoseconds =
        scheduler_state.next_sample_time_nanoseconds;
    result.scheduler_last_due_command_count =
        last_scheduler_due_command_count_;
    result.scheduler_maximum_commands_per_update =
        usercmd_scheduler_->config().maximum_commands_per_update;
    result.scheduler_stall_recoveries = scheduler_stall_recoveries_;
    result.scheduler_discarded_wall_time_samples = scheduler_discarded_wall_time_samples_;
    result.generated_by_phase = generated_by_phase_;
    result.sent_by_phase = sent_by_phase_;
    result.fresh_samples_by_phase = fresh_samples_by_phase_;
    result.requested_forward_by_phase = requested_forward_by_phase_;
    result.encoded_forward_by_phase = encoded_forward_by_phase_;
    result.encoded_side_by_phase = encoded_side_by_phase_;
    result.speed_multiplier_by_phase = speed_multiplier_by_phase_;
    if (response_stage_.result())
      result.movevars_maximum_speed = response_stage_.result()
          ->resource_list().transition().user_info()
          .movement_environment().move_vars().maximum_speed();
    result.transmit_ranges.assign(usercmd_transmit_ranges_.begin(), usercmd_transmit_ranges_.end());
    result.server_samples.assign(usercmd_server_samples_.begin(), usercmd_server_samples_.end());
    result.motion_outcome = LiveUserCmdMotionOutcome::not_evaluated;
    result.movement_verified = false;
    if (config_.live_visual_input_source ==
        LiveVisualControlInputSource::scripted_jump_duck_check)
      result.jump_duck = evaluate_live_jump_duck(
          result.server_samples, result.sent_by_phase);
    if (config_.live_visual_input_source ==
            LiveVisualControlInputSource::scripted_speed_check ||
        config_.live_visual_input_source ==
            LiveVisualControlInputSource::scripted_weapon_check)
      result.speed_outcome = evaluate_live_speed_check(
          result.server_samples, result.sent_by_phase,
          config_.usercmd_scenario.fixed_yaw_degrees,
          config_.usercmd_scenario.origin_tolerance,
          config_.usercmd_scenario.velocity_tolerance);
    return result;
  }

  void complete_usercmd_scenario_if_ready(const LiveRuntimeStageTimePoint now) {
    const bool life_check = config_.live_visual_input_source ==
        LiveVisualControlInputSource::scripted_damage_respawn_check;
    if (life_check && usercmd_stage_) {
      const auto phase = config_.game_client->damage_respawn_snapshot().phase;
      if (phase == game_api::DamageRespawnPhase::blocked || phase == game_api::DamageRespawnPhase::complete) {
        if (!usercmd_stage_->history().unsent_sequences().empty()) return;
        usercmd_result_ = usercmd_snapshot();
        if (usercmd_result_) {
          usercmd_result_->movement_verified = phase == game_api::DamageRespawnPhase::complete;
          publish_success(now); // app separates feature partial from runtime error
        }
        return;
      }
    }
    if (!usercmd_activated_at_ || !usercmd_stage_ ||
        now < *usercmd_activated_at_ +
                  scenario_duration(config_.usercmd_scenario)) {
      return;
    }
    std::size_t expected = 0U;
    for (const auto duration : config_.usercmd_scenario.durations) {
      expected += static_cast<std::size_t>(
          duration.count() / config_.usercmd_scenario.command_interval.count());
    }
    const auto history = usercmd_stage_->history();
    if (generated_usercmd_count_ != expected ||
        usercmd_stage_->new_command_submission_count() != expected ||
        !history.unsent_sequences().empty()) {
      return;
    }
    const bool jump_duck_check =
        config_.live_visual_input_source ==
        LiveVisualControlInputSource::scripted_jump_duck_check;
    const bool speed_check = config_.live_visual_input_source ==
            LiveVisualControlInputSource::scripted_speed_check ||
        config_.live_visual_input_source ==
            LiveVisualControlInputSource::scripted_weapon_check;
    const bool weapon_actions = config_.live_visual_input_source ==
        LiveVisualControlInputSource::scripted_fire_reload_check ||
        config_.live_visual_input_source ==
        LiveVisualControlInputSource::scripted_fire_reload_presentation_check;
    const auto outcome = jump_duck_check || speed_check || weapon_actions
                             ? LiveUserCmdMotionOutcome::not_evaluated
                             : evaluate_usercmd_motion();
    auto result = usercmd_snapshot();
    if (!result) {
      fail(LiveRuntimeStageErrorCode::usercmd_transmission_failed,
           LiveRuntimeStageState::protocol_error,
           "Completed scenario has no usercmd accounting snapshot", now);
      return;
    }
    result->motion_outcome = outcome;
    result->movement_verified =
        weapon_actions ? true : jump_duck_check
            ? result->jump_duck.outcome == LiveJumpDuckOutcome::verified
            : speed_check
                ? result->speed_outcome == LiveUserCmdMotionOutcome::verified
            : outcome == LiveUserCmdMotionOutcome::verified;
    usercmd_result_ = std::move(*result);
    publish_success(now);
  }

  void handle_driver_event(NetchanDriverEvent event,
                           const LiveRuntimeStageTimePoint now) {
    switch (event.type) {
    case NetchanDriverEventType::payload_ready:
      ++payload_events_consumed_;
      if (!event.payload) {
        fail(LiveRuntimeStageErrorCode::driver_failed,
             LiveRuntimeStageState::protocol_error,
             "Driver published payload_ready without owning payload", now);
        return;
      }
      process_netchan_payload(std::move(*event.payload), now);
      return;
    case NetchanDriverEventType::reliable_payload_acknowledged:
      if (spawn_request_transmitted_ && !spawn_request_acknowledged_) {
        auto *const driver = response_stage_.retained_driver();
        if (!driver || !spawn_reliable_generation_ ||
            !spawn_most_recent_transmit_sequence_ || !event.acknowledgement ||
            !event.completed_reliable_generation ||
            event.acknowledgement->disposition !=
                NetchanAcknowledgementDisposition::advanced ||
            *event.completed_reliable_generation !=
                *spawn_reliable_generation_ ||
            event.acknowledgement->reliable != spawn_reliable_toggle_ ||
            (compare_sequences(event.acknowledgement->sequence,
                               *spawn_most_recent_transmit_sequence_) !=
                 NetchanSequenceComparison::equal &&
             compare_sequences(event.acknowledgement->sequence,
                               *spawn_most_recent_transmit_sequence_) !=
                 NetchanSequenceComparison::newer) ||
            !driver->session().pending_reliable_payload().empty() ||
            driver->session().in_flight_reliable_payload() ||
            driver->session().outgoing_fragment_transfer()) {
          fail(LiveRuntimeStageErrorCode::spawn_request_transmit_mismatch,
               LiveRuntimeStageState::protocol_error,
               "Reliable ACK did not exactly cover and release the stock spawn "
               "generation",
               now);
          return;
        }
        spawn_request_acknowledged_ = true;
        const auto acknowledged = LiveRuntimeStageEvent{
            .type = LiveRuntimeStageEventType::spawn_request_acknowledged,
            .occurred_at = now};
        push_event(acknowledged);
        emit_trace(acknowledged);
        return;
      }
      if (signon_reply_transmitted_ && !signon_reply_acknowledged_) {
        auto *const driver = response_stage_.retained_driver();
        if (!driver || !signon_reply_reliable_generation_ ||
            !signon_reply_most_recent_transmit_sequence_ ||
            !event.acknowledgement || !event.completed_reliable_generation ||
            event.acknowledgement->disposition !=
                NetchanAcknowledgementDisposition::advanced ||
            *event.completed_reliable_generation !=
                *signon_reply_reliable_generation_ ||
            event.acknowledgement->reliable != signon_reply_reliable_toggle_ ||
            (compare_sequences(event.acknowledgement->sequence,
                               *signon_reply_most_recent_transmit_sequence_) !=
                 NetchanSequenceComparison::equal &&
             compare_sequences(event.acknowledgement->sequence,
                               *signon_reply_most_recent_transmit_sequence_) !=
                 NetchanSequenceComparison::newer) ||
            !driver->session().pending_reliable_payload().empty() ||
            driver->session().in_flight_reliable_payload() ||
            driver->session().outgoing_fragment_transfer()) {
          fail(LiveRuntimeStageErrorCode::signon_reply_transmit_mismatch,
               LiveRuntimeStageState::protocol_error,
               "Reliable ACK did not exactly cover and release the typed "
               "sendents generation",
               now);
          return;
        }
        signon_reply_acknowledged_ = true;
        const auto acknowledged = LiveRuntimeStageEvent{
            .type = LiveRuntimeStageEventType::signon_reply_acknowledged,
            .occurred_at = now};
        push_event(acknowledged);
        emit_trace(acknowledged);
      }
      return;
    case NetchanDriverEventType::normal_transfer_started:
    case NetchanDriverEventType::normal_transfer_completed:
      return;
    case NetchanDriverEventType::secondary_stream_pending_m3:
      fail(LiveRuntimeStageErrorCode::driver_failed,
           LiveRuntimeStageState::secondary_stream_pending,
           "Secondary live runtime fragment stream remains pending", now,
           std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
           NetchanDriverErrorCode::secondary_stream_pending_m3);
      return;
    case NetchanDriverEventType::normal_transfer_timed_out:
    case NetchanDriverEventType::channel_timed_out:
      fail(LiveRuntimeStageErrorCode::driver_failed,
           LiveRuntimeStageState::timed_out,
           "Live runtime transfer or channel timed out", now);
      return;
    case NetchanDriverEventType::cancelled:
      state_ = LiveRuntimeStageState::cancelled;
      result_.reset();
      error_.reset();
      cleanup(now, false);
      return;
    case NetchanDriverEventType::network_error:
      fail(LiveRuntimeStageErrorCode::driver_failed,
           LiveRuntimeStageState::network_error,
           "Retained driver reported a live runtime network failure", now);
      return;
    case NetchanDriverEventType::protocol_error:
      fail(LiveRuntimeStageErrorCode::driver_failed,
           LiveRuntimeStageState::protocol_error,
           "Retained driver reported a live runtime protocol failure", now);
      return;
    }
  }

  void publish_success(const LiveRuntimeStageTimePoint now) {
    const auto &observation = target_.runtime_observation();
    if (!runtime_session_ || !observation || !stable_started_at_ ||
        !signon_control_observed_ || !signon_reply_ ||
        !signon_reply_transmitted_ || !signon_reply_acknowledged_ ||
        (config_.operation_mode != LiveRuntimeOperationMode::runtime_state &&
         !usercmd_result_)) {
      fail(LiveRuntimeStageErrorCode::runtime_record_failed,
           LiveRuntimeStageState::protocol_error,
           "Stable stop lacks committed state or typed sendents completion",
           now);
      return;
    }
    result_.emplace(LiveRuntimeState{
        kLiveRuntimeGeneration,
        protocol_,
        server_count_,
        client_slot_,
        max_clients_,
        game_directory_,
        map_file_path_,
        schemas_ ? schemas_->schema_count() : 0U,
        schemas_ ? schema_field_count(*schemas_) : 0U,
        baseline_entity_count_,
        baseline_instanced_count_,
        received_service_payload_count_,
        applied_runtime_record_count_,
        clientdata_record_count_,
        entity_record_count_,
        compressed_service_payload_count_,
        wire_uncompressed_service_payload_count_,
        observation->publication_revision,
        observation->canonical_state_hash,
        observation->packet_entities.size(),
        spawn_request_queue_count_,
        spawn_request_transmitted_,
        spawn_request_acknowledged_,
        signon_reply_queue_count_,
        signon_reply_transmitted_,
        signon_reply_acknowledged_,
        server_time_observed_,
        clientdata_observed_,
        entities_observed_,
        initial_service_wire_uncompressed_,
        transition_service_wire_uncompressed_,
        stable_progress_observed_,
        d_handoff_interval_.value_or(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *stable_started_at_)),
        usercmd_result_});
    runtime_session_->finish();
    const bool usercmd =
        config_.operation_mode == LiveRuntimeOperationMode::live_usercmd_check;
    const bool live_visual = config_.operation_mode ==
                             LiveRuntimeOperationMode::live_visual_control;
    state_ = usercmd
                 ? LiveRuntimeStageState::live_usercmd_check_ready
             : live_visual
                 ? LiveRuntimeStageState::live_visual_control_ready
                 : LiveRuntimeStageState::stable_runtime_state_ready;
    const auto event = LiveRuntimeStageEvent{
        .type = usercmd
                    ? LiveRuntimeStageEventType::live_usercmd_check_ready
                : live_visual
                    ? LiveRuntimeStageEventType::live_visual_control_ready
                    : LiveRuntimeStageEventType::stable_runtime_state_ready,
        .payload_ordinal = payload_ordinal_,
        .entity_count = observation->packet_entities.size(),
        .publication_revision = observation->publication_revision,
        .occurred_at = now};
    push_event(event);
    emit_trace(event);
    cleanup(now, true);
  }

  void fail_from_response(const LiveRuntimeStageTimePoint now,
                          const bool start_failure) {
    const auto &nested = response_stage_.error();
    fail(start_failure ? LiveRuntimeStageErrorCode::response_stage_start_failed
                       : LiveRuntimeStageErrorCode::response_stage_failed,
         response_stage_.state() == ResourceClientResponseStageState::timed_out
             ? LiveRuntimeStageState::timed_out
         : response_stage_.state() ==
                 ResourceClientResponseStageState::backpressure
             ? LiveRuntimeStageState::backpressure
         : response_stage_.state() ==
                 ResourceClientResponseStageState::secondary_stream_pending
             ? LiveRuntimeStageState::secondary_stream_pending
         : response_stage_.state() ==
                 ResourceClientResponseStageState::network_error
             ? LiveRuntimeStageState::network_error
             : LiveRuntimeStageState::protocol_error,
         nested ? std::string_view{nested->context}
                : std::string_view{"Nested resource-response stage ended "
                                   "without context"},
         now, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
         std::nullopt, nested ? nested->driver_code : std::nullopt,
         std::nullopt, nested ? std::optional{nested->code} : std::nullopt,
         std::nullopt, nested ? nested->resource_list_code : std::nullopt,
         nested ? nested->transition_stage_code : std::nullopt,
         nested ? nested->transition_control_code : std::nullopt,
         nested ? nested->transition_failure_metadata : std::nullopt);
    if (error_ && nested) {
      error_->response_payload_diagnostic = nested->payload_diagnostic;
    }
  }

  [[nodiscard]] bool
  observe_service_encoding(const bool decompressed,
                           const bool wire_uncompressed) noexcept {
    if (decompressed == wire_uncompressed) {
      return false;
    }
    auto &count = decompressed ? compressed_service_payload_count_
                               : wire_uncompressed_service_payload_count_;
    if (count == (std::numeric_limits<std::size_t>::max)()) {
      return false;
    }
    ++count;
    return true;
  }

  void
  fail(const LiveRuntimeStageErrorCode code, const LiveRuntimeStageState state,
       const std::string_view context, const LiveRuntimeStageTimePoint now,
       const std::optional<ServicePayloadEnvelopeErrorCode> envelope_code =
           std::nullopt,
       const std::optional<RuntimeControlDecodeErrorCode> control_code =
           std::nullopt,
       const std::optional<ResourceClientResponseStageErrorCode> response_code =
           std::nullopt,
       const std::optional<EntityBaselineDecodeErrorCode> baseline_code =
           std::nullopt,
       const std::optional<RuntimeReplayErrorCode> runtime_code = std::nullopt,
       const std::optional<NetchanDriverErrorCode> driver_code = std::nullopt,
       const std::optional<std::uint8_t> wire_opcode = std::nullopt,
       const std::optional<ResourceClientResponseStageErrorCode>
           response_code_override = std::nullopt,
       const std::optional<StockSpawnRequestErrorCode> spawn_request_code =
           std::nullopt,
       const std::optional<ResourceListStageErrorCode> resource_list_code =
           std::nullopt,
       const std::optional<ResourceTransitionStageErrorCode>
           transition_stage_code = std::nullopt,
       const std::optional<ResourceTransitionControlErrorCode>
           transition_control_code = std::nullopt,
       std::optional<ResourceTransitionFailureMetadata>
           transition_failure_metadata = std::nullopt) noexcept {
    if (terminal_state(state_)) {
      return;
    }
    // A shared driver pump can successfully send an already-owned move and
    // then reject RX in the same update. The transmission stage accounts that
    // irreversible send first. Preserve the driver's actual terminal cause
    // here instead of replacing it with the secondary "send failed" wrapper.
    // close_driver() exposes the same owning driver even during nested-stage
    // teardown; no new transport or diagnostic I/O is introduced.
    const auto* const driver =
        code == LiveRuntimeStageErrorCode::driver_failed ||
                code == LiveRuntimeStageErrorCode::usercmd_transmission_failed ||
                driver_code
            ? response_stage_.close_driver()
            : nullptr;
    const auto* const owning_driver_failure =
        driver != nullptr && driver->last_error()
            ? &*driver->last_error()
            : nullptr;
    auto terminal_cause_state = state;
    auto terminal_cause_code = code;
    const auto terminal_driver_code = owning_driver_failure
        ? std::optional{owning_driver_failure->code} : driver_code;
    if (owning_driver_failure != nullptr && driver->terminal()) {
      if (code == LiveRuntimeStageErrorCode::usercmd_transmission_failed)
        terminal_cause_code = LiveRuntimeStageErrorCode::driver_failed;
      if (owning_driver_failure->code ==
          NetchanDriverErrorCode::secondary_stream_pending_m3) {
        terminal_cause_state = LiveRuntimeStageState::secondary_stream_pending;
      } else {
        switch (driver->state()) {
        case NetchanDriverState::network_error:
          terminal_cause_state = LiveRuntimeStageState::network_error;
          break;
        case NetchanDriverState::timed_out:
          terminal_cause_state = LiveRuntimeStageState::timed_out;
          break;
        case NetchanDriverState::backpressure:
          terminal_cause_state = LiveRuntimeStageState::backpressure;
          break;
        case NetchanDriverState::protocol_error:
          terminal_cause_state = LiveRuntimeStageState::protocol_error;
          break;
        case NetchanDriverState::idle:
        case NetchanDriverState::active:
        case NetchanDriverState::cancelled:
        case NetchanDriverState::closed:
          break;
        }
      }
    }
    state_ = terminal_cause_state;
    if (graceful_close_.requested && graceful_close_.outcome != GracefulCloseOutcome::send_failed) {
      graceful_close_.outcome = GracefulCloseOutcome::fatal_failure;
      graceful_close_.netchan_closed_at = now;
      graceful_close_.transport_error = terminal_driver_code;
    }
    result_.reset();
    try {
      LiveRuntimeStageError error;
      error.code = terminal_cause_code;
      error.response_code =
          response_code_override ? response_code_override : response_code;
      error.resource_list_code = resource_list_code;
      error.transition_stage_code = transition_stage_code;
      error.transition_control_code = transition_control_code;
      error.transition_failure_metadata =
          std::move(transition_failure_metadata);
      error.spawn_request_code = spawn_request_code;
      error.envelope_code = envelope_code;
      error.control_code = control_code;
      error.baseline_code = baseline_code;
      error.runtime_code = runtime_code;
      error.driver_code = terminal_driver_code;
      if (owning_driver_failure != nullptr)
        error.driver_failure = *owning_driver_failure;
      error.wire_opcode = wire_opcode;
      const auto bounded =
          context.substr(0U, (std::min)(context.size(), kDiagnosticTextLimit));
      error.context.assign(bounded.data(), bounded.size());
      error_.emplace(std::move(error));
    } catch (...) {
    }
    const auto type = terminal_cause_state == LiveRuntimeStageState::timed_out
                          ? LiveRuntimeStageEventType::timeout
                      : terminal_cause_state == LiveRuntimeStageState::backpressure
                          ? LiveRuntimeStageEventType::backpressure
                      : terminal_cause_state == LiveRuntimeStageState::secondary_stream_pending
                          ? LiveRuntimeStageEventType::secondary_stream_pending
                      : terminal_cause_state == LiveRuntimeStageState::network_error
                          ? LiveRuntimeStageEventType::network_error
                          : LiveRuntimeStageEventType::protocol_error;
    const LiveRuntimeStageEvent event{.type = type, .occurred_at = now};
    push_event(event);
    emit_trace(event);
    cleanup(now, false);
  }

  void cleanup(const LiveRuntimeStageTimePoint now,
               const bool close_driver) noexcept {
    if (cleanup_done_) {
      return;
    }
    cleanup_done_ = true;
    ++cleanup_count_;
    pending_hud_messages_.clear();
    pending_hud_bytes_ = 0U;
    pending_audio_count_ = pending_audio_bytes_ = 0U;
    if (runtime_session_ &&
        runtime_session_->status() == RuntimeReplaySessionStatus::active) {
      runtime_session_->finish();
    }
    if (auto *const driver = response_stage_.close_driver();
        driver != nullptr && !driver->terminal()) {
      try {
        if (close_driver) {
          driver->close(now);
        } else {
          driver->cancel(now);
        }
      } catch (...) {
      }
    }
    if (!response_stage_.terminal()) response_stage_.cancel(now);
    response_stage_.finalize_retained_boundary(now);
  }

  [[nodiscard]] bool can_push_event() const noexcept {
    return event_size_ < event_slots_.size();
  }

  void push_event(const LiveRuntimeStageEvent &event) noexcept {
    if (!can_push_event()) {
      if (!terminal_state(state_)) {
        state_ = LiveRuntimeStageState::backpressure;
      }
      return;
    }
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index] = event;
    ++event_size_;
  }

  void emit_trace(const LiveRuntimeStageEvent &event) noexcept {
    if (!trace_callback_ || trace_callback_active_) {
      return;
    }
    trace_callback_active_ = true;
    try {
      trace_callback_(LiveRuntimeStageTraceEvent{
          state_, response_stage_.remote_endpoint(), event,
          response_stage_.transmitted_packet_count()});
    } catch (...) {
    }
    trace_callback_active_ = false;
  }

  struct PendingHudMessage final {
    std::size_t source_ordinal{};
    OwnedServicePayload payload;
    StockRuntimeSourceCursor cursor{};
    SoundTime received_at{};
  };
  client::ClientWorldState &target_;
  LiveRuntimeStageConfig config_;
  LiveRuntimeStageTraceCallback trace_callback_;
  bool trace_callback_active_{false};
  bool configuration_valid_{false};
  GracefulCloseSummary graceful_close_;
  ResourceClientResponseStage response_stage_;
  std::vector<std::optional<LiveRuntimeStageEvent>> event_slots_;
  std::size_t event_head_{0U};
  std::size_t event_size_{0U};
  LiveRuntimeStageState state_{LiveRuntimeStageState::idle};
  std::optional<LiveRuntimeState> result_;
  std::optional<LiveRuntimeStageError> error_;
  std::shared_ptr<const DeltaSchemaRegistryState> schemas_;
  std::vector<PostMoveVarsUserMessageDefinition> user_messages_;
  std::unique_ptr<RuntimeReplaySession> runtime_session_;
  std::optional<EncodedStockSpawnRequest> spawn_request_;
  std::optional<EncodedStockSendEntitiesRequest> signon_reply_;
  std::optional<LiveRuntimeStageTimePoint> started_at_;
  std::optional<LiveRuntimeStageTimePoint> stable_started_at_;
  std::optional<LiveRuntimeStageTimePoint> last_update_;
  std::uint32_t max_clients_{0U};
  std::uint32_t protocol_{0U};
  std::uint32_t server_count_{0U};
  std::uint32_t world_map_crc_{0U};
  std::uint8_t client_slot_{0U};
  std::string game_directory_;
  std::string map_file_path_;
  std::size_t payload_ordinal_{0U};
  std::size_t runtime_record_ordinal_{0U};
  std::vector<PendingHudMessage> pending_hud_messages_;
  std::size_t pending_hud_bytes_{0U};
  std::size_t pending_audio_count_{},pending_audio_bytes_{};
  std::size_t baseline_entity_count_{0U};
  std::size_t baseline_instanced_count_{0U};
  std::size_t received_service_payload_count_{0U};
  std::size_t payload_events_consumed_{0U};
  std::size_t service_envelopes_decoded_{0U};
  std::size_t runtime_records_attempted_{0U};
  NetchanDriverReceiveStatistics driver_rx_at_input_activation_{};
  NetchanDriverReceiveStatistics last_driver_rx_{};
  std::size_t payload_events_consumed_at_input_activation_{0U};
  std::size_t service_envelopes_decoded_at_input_activation_{0U};
  std::size_t runtime_records_attempted_at_input_activation_{0U};
  std::size_t runtime_records_committed_at_input_activation_{0U};
  std::size_t clientdata_records_committed_at_input_activation_{0U};
  std::size_t applied_runtime_record_count_{0U};
  std::size_t clientdata_record_count_{0U};
  std::size_t entity_record_count_{0U};
  std::size_t compressed_service_payload_count_{0U};
  std::size_t wire_uncompressed_service_payload_count_{0U};
  std::size_t cleanup_count_{0U};
  std::size_t spawn_request_queue_count_{0U};
  std::optional<std::uint64_t> spawn_reliable_generation_;
  std::optional<NetchanSequence> spawn_first_transmit_sequence_;
  std::optional<NetchanSequence> spawn_most_recent_transmit_sequence_;
  std::uint64_t spawn_transmit_count_{0U};
  bool spawn_reliable_toggle_{false};
  bool spawn_request_transmitted_{false};
  bool spawn_request_acknowledged_{false};
  std::size_t signon_reply_queue_count_{0U};
  std::optional<std::uint64_t> signon_reply_reliable_generation_;
  std::optional<NetchanSequence> signon_reply_first_transmit_sequence_;
  std::optional<NetchanSequence> signon_reply_most_recent_transmit_sequence_;
  std::uint64_t signon_reply_transmit_count_{0U};
  bool signon_reply_reliable_toggle_{false};
  bool signon_control_observed_{false};
  bool signon_reply_transmitted_{false};
  bool signon_reply_acknowledged_{false};
  bool server_time_observed_{false};
  bool clientdata_observed_{false};
  bool entities_observed_{false};
  bool initial_service_wire_uncompressed_{false};
  bool transition_service_wire_uncompressed_{false};
  bool stable_progress_observed_{false};
  std::size_t stable_start_applied_record_count_{0U};
  std::optional<std::chrono::milliseconds> d_handoff_interval_;
  std::unique_ptr<GoldSrcUserCmdScheduler> usercmd_scheduler_;
  std::size_t last_scheduler_due_command_count_{0U};
  std::uint64_t scheduler_stall_recoveries_{0U};
  std::uint64_t scheduler_discarded_wall_time_samples_{0U};
  std::unique_ptr<GoldSrcUserCmdTransmissionStage> usercmd_stage_;
  ReferencePredictionCarrierLedger prediction_carriers_{128U};
  LiveRuntimeMovementDiagnostics movement_diagnostics_;

  std::uint64_t prediction_life_epoch_{}, prediction_life_deaths_{};
  std::size_t prediction_life_command_floor_{};
  std::unique_ptr<movement::ILocalMovementCollision> prediction_collision_;
  std::shared_ptr<const collision::BrushCollisionModelLibrary> prediction_brush_library_;
  std::unique_ptr<SurfaceTextureQuery> surface_texture_query_;
  std::map<std::uint32_t, std::string> prediction_model_names_;
  bool prediction_has_brush_models_{false};
  ReferenceBrushCollisionContext prediction_brush_context_;
  std::optional<ReferenceVerticalSupportMotion> prediction_support_motion_;
  std::shared_ptr<const hlclient::movement::LocalPlayerMovementState> prediction_pre_anchor_;
  std::size_t prediction_brush_transform_changes_{};
  std::optional<std::uint32_t> prediction_last_changed_brush_entity_, prediction_last_changed_brush_model_;
  std::uint64_t prediction_step_selections_{};
  std::string_view prediction_last_fallback_reason_{"none"};
  std::optional<movement::GoldSrcMovementEnvironment> prediction_environment_;
  movement::GoldSrcLocalMovementConfig prediction_movement_config_{};
  movement::GoldSrcLocalMovementScratch prediction_scratch_{};
  std::optional<movement::LocalMovementPositionTest> prediction_collision_position_evidence_;
  std::optional<movement::LocalMovementTrace> prediction_collision_trace_evidence_;
  std::optional<hlclient::movement::PlayerMovementHull> prediction_collision_query_hull_;
  std::optional<assets::AssetVector3> prediction_collision_query_start_, prediction_collision_query_end_;
  std::optional<assets::AssetVector3> prediction_seed_origin_, prediction_previous_valid_origin_;
  std::optional<LiveRuntimeStageTimePoint> prediction_fallback_started_at_;
  double prediction_last_fallback_duration_ms_{};
  std::string_view prediction_rebase_reason_{"not_attempted"};
  std::optional<movement::LocalMovementSimulationErrorCode> prediction_last_simulation_error_;
  std::optional<assets::AssetVector3> prediction_matched_origin_before_correction_;
    std::optional<assets::AssetVector3> prediction_matched_velocity_before_correction_;
  std::string_view prediction_collision_query_purpose_{"unavailable"};
  std::shared_ptr<const prediction::LocalPredictionHistoryState>
      prediction_history_;
  mutable hlclient::collision::CollisionQueryScratch prediction_presentation_scratch_;
  std::unique_ptr<hlclient::collision::CollisionWorldQuery>
      prediction_camera_collision_query_;
  mutable std::optional<prediction::PredictionVisualCorrectionState>
      prediction_visual_correction_;
  mutable bool prediction_server_velocity_visual_{false};
  mutable std::shared_ptr<const hlclient::movement::LocalPlayerMovementState>
      prediction_server_velocity_pose_;
  std::uint32_t prediction_server_velocity_entity_{0U};
  std::optional<client::RuntimeObservationSource> prediction_last_view_angle_source_;
  mutable std::optional<std::uint64_t> prediction_exterior_correction_source_;
  mutable std::optional<LiveReferencePredictionSnapshot> prediction_presentation_cache_;
  mutable std::optional<LiveRuntimeStageTimePoint> prediction_presentation_cache_at_;
  mutable const prediction::LocalPredictionHistoryState*
      prediction_presentation_cache_history_{nullptr};
  mutable std::uint64_t prediction_presentation_cache_world_revision_{0U};
  mutable std::uint64_t prediction_presentation_cache_epoch_{0U};
  mutable std::size_t prediction_presentation_trace_queries_{0U};
  mutable std::size_t prediction_presentation_scratch_growths_{0U};
  mutable LiveReferencePredictionState prediction_presentation_cache_state_{
      LiveReferencePredictionState::off};
  LiveReferencePredictionState prediction_state_{
      LiveReferencePredictionState::off};
  std::string_view prediction_reason_{"off"};
  std::uint64_t prediction_epoch_{0U};
  std::size_t prediction_local_steps_{0U};
  std::size_t prediction_accepted_corrections_{0U};
  std::size_t prediction_replayed_commands_{0U};
  std::size_t prediction_fallback_count_{0U};
  std::optional<double> prediction_last_error_;
  std::optional<double> prediction_maximum_error_;
  std::optional<double> prediction_last_camera_correction_jump_;
  std::optional<double> prediction_maximum_camera_correction_jump_;
  std::optional<std::uint64_t> prediction_last_record_identity_;
  std::optional<ReferencePredictionGroundStatus> prediction_last_ground_status_;
  std::optional<LiveRuntimeStageTimePoint> prediction_last_anchor_at_;
  std::size_t prediction_sent_bindings_{0U};
  std::size_t prediction_anchor_bindings_{0U};
  std::size_t prediction_seed_candidates_{0U};
  std::size_t prediction_command_candidates_{0U};
  std::optional<GoldSrcUserCmdErrorCode> prediction_last_command_error_;
  std::optional<ReferencePredictionAnchorStatus> prediction_last_anchor_status_;
  bool prediction_anchor_coherent_reliable_frame_{false};
  std::optional<ReferencePredictionSeedStatus> prediction_last_seed_status_;
  std::optional<ReferencePredictionSeedField> prediction_last_seed_field_;
  std::optional<std::uint32_t> prediction_last_command_boundary_;
  GoldSrcUserCmdInputAdapter usercmd_adapter_;
  std::vector<gameplay_input::GameplayInputIntent> scenario_intents_;
  std::optional<gameplay_camera::GameplayCameraState> scenario_camera_;
  std::optional<LiveVisualControlInput> live_visual_input_;
  LiveVisualButtonLatch button_latch_;
  game_api::ScopedGameplayButtonGate scoped_button_gate_;
  std::size_t use_generated_count_{}, use_command_press_count_{}, use_command_release_count_{};
  std::size_t use_new_submission_count_{}, use_clear_after_release_count_{};
  bool last_submitted_use_{};
  std::optional<LiveRuntimeStageTimePoint> live_visual_input_at_;
  std::optional<LiveRuntimeStageTimePoint> usercmd_activated_at_;
  std::optional<std::int64_t> usercmd_activation_time_ns_;
  std::size_t generated_usercmd_count_{0U};
  float last_sampled_forward_axis_{0.0F};
  float last_sampled_side_axis_{0.0F};
  std::size_t nonzero_side_generated_count_{0U};
  std::size_t nonzero_side_new_submission_count_{0U};
  std::size_t jump_generated_count_{0U};
  std::size_t duck_generated_count_{0U};
  std::size_t jump_new_submission_count_{0U};
  std::size_t duck_new_submission_count_{0U};
  std::size_t attack_generated_count_{0U};
  std::size_t reload_generated_count_{0U};
  std::size_t attack_command_press_count_{0U};
  std::size_t attack_command_release_count_{0U};
  std::size_t reload_command_press_count_{0U};
  std::size_t reload_command_release_count_{0U};
  std::size_t attack_new_submission_count_{0U};
  std::size_t reload_new_submission_count_{0U};
  struct CommandSampleTime {
    std::uint32_t sequence{};
    std::int64_t end_nanoseconds{};
  };
  // Same bounded retention horizon as the immutable wire history. An unsent
  // history entry cannot be evicted, so its slot cannot be reused before its
  // receipt. Backup/replay sends do not publish another presentation event.
  std::array<CommandSampleTime,kMaximumGoldSrcUserCmdHistoryEntries>
      command_sample_times_{};
  std::deque<LiveWeaponCommandSubmission> weapon_command_submissions_;
  std::size_t weapon_command_submission_drops_{0U};
  std::optional<std::uint32_t> first_jump_sent_sequence_;
  std::optional<std::uint32_t> last_jump_sent_sequence_;
  std::optional<std::uint32_t> first_duck_sent_sequence_;
  std::optional<std::uint32_t> last_duck_sent_sequence_;
  std::size_t jump_command_press_count_{0U};
  std::size_t jump_command_release_count_{0U};
  std::size_t duck_command_press_count_{0U};
  std::size_t duck_command_release_count_{0U};
  std::uint16_t previous_generated_buttons_{0U};
  struct CommandEffect {
    std::uint32_t sequence{};assets::AssetVector3 impulse{};
    game_api::CommandMovementEffectPhase phase{game_api::CommandMovementEffectPhase::before_movement};
  };
  std::array<CommandEffect,512> command_effects_{};
  std::array<std::size_t, kLiveUserCmdPhaseCount> generated_by_phase_{};

  std::array<std::size_t, kLiveUserCmdPhaseCount> sent_by_phase_{};
  std::array<std::size_t, kLiveUserCmdPhaseCount> fresh_samples_by_phase_{};
  std::array<std::optional<float>, kLiveUserCmdPhaseCount> requested_forward_by_phase_{};
  std::array<std::optional<std::int16_t>, kLiveUserCmdPhaseCount> encoded_forward_by_phase_{};
  std::array<std::optional<std::int16_t>, kLiveUserCmdPhaseCount> encoded_side_by_phase_{};
  std::array<std::optional<float>, kLiveUserCmdPhaseCount> speed_multiplier_by_phase_{};
  std::deque<LiveUserCmdTransmitRange> usercmd_transmit_ranges_;
  std::deque<LiveUserCmdServerSample> usercmd_server_samples_;
  std::size_t server_sample_count_{}, transmit_ranges_evicted_{}, server_samples_evicted_{};
  std::optional<std::uint32_t> first_new_command_sequence_, last_new_command_sequence_;
  std::optional<LiveUserCmdCheckState> usercmd_result_;
  bool cleanup_done_{false};
};

LiveRuntimeStage::LiveRuntimeStage(
    network::IDatagramTransport &transport,
    const network::NetworkAddress remote_endpoint,
    client::ClientWorldState &target, LiveRuntimeStageConfig config,
    resource_consistency::IResourceConsistencyProvider *consistency_provider,
    LiveRuntimeStageTraceCallback trace_callback,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback response_trace_callback)
    : implementation_{std::make_unique<Implementation>(
          transport, remote_endpoint, target, std::move(config),
          consistency_provider, std::move(trace_callback),
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback), std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          std::move(transition_trace_callback),
          std::move(resource_list_trace_callback),
          std::move(response_trace_callback))} {}

LiveRuntimeStage::~LiveRuntimeStage() = default;

bool LiveRuntimeStage::start(
    const LiveRuntimeStageTimePoint now,
    const network::NetworkAddress &expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime) {
  return implementation_->start(now, expected_local_endpoint,
                                std::move(connection_lifetime));
}

void LiveRuntimeStage::update(const LiveRuntimeStageTimePoint now) {
  implementation_->update(now);
}

void LiveRuntimeStage::cancel(const LiveRuntimeStageTimePoint now) {
  implementation_->cancel(now);
}

bool LiveRuntimeStage::request_graceful_close(const LiveRuntimeStageTimePoint now) {
  return implementation_->request_graceful_close(now);
}

GracefulCloseSummary LiveRuntimeStage::graceful_close_snapshot() const noexcept {
  return implementation_->graceful_close_snapshot();
}

bool LiveRuntimeStage::submit_live_visual_input(
    const LiveVisualControlInput &input,
    const LiveRuntimeStageTimePoint now) noexcept {
  return implementation_->submit_live_visual_input(input, now);
}

bool LiveRuntimeStage::request_weapon_selection(const std::uint8_t weapon_id) {
  return implementation_->request_weapon_selection(weapon_id);
}

bool LiveRuntimeStage::activate_live_visual_control(
    const LiveRuntimeStageTimePoint now) noexcept {
  return implementation_->activate_live_visual_control(now);
}

bool LiveRuntimeStage::attach_reference_prediction_collision(
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package) {
  return implementation_->attach_reference_prediction_collision(
      std::move(package));
}

bool LiveRuntimeStage::attach_reference_prediction_surfaces(
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> package) {
  return implementation_->attach_reference_prediction_surfaces(std::move(package));
}

LiveReferencePredictionSnapshot
LiveRuntimeStage::live_reference_prediction_snapshot(
    const LiveRuntimeStageTimePoint now) const {
  return implementation_->live_reference_prediction_snapshot(now);
}

LiveRuntimeMovementDiagnostics LiveRuntimeStage::movement_diagnostics_snapshot() const noexcept {
  return implementation_->movement_diagnostics_;
}

std::optional<LiveRuntimeStageEvent> LiveRuntimeStage::poll_event() {
  return implementation_->poll_event();
}

LiveRuntimeStageState LiveRuntimeStage::state() const noexcept {
  return implementation_->state_;
}

bool LiveRuntimeStage::terminal() const noexcept {
  return terminal_state(implementation_->state_);
}

const std::optional<LiveRuntimeState> &
LiveRuntimeStage::result() const noexcept {
  return implementation_->result_;
}

const std::optional<LiveRuntimeStageError> &
LiveRuntimeStage::error() const noexcept {
  return implementation_->error_;
}

const network::NetworkAddress &
LiveRuntimeStage::remote_endpoint() const noexcept {
  return implementation_->response_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress> &
LiveRuntimeStage::local_endpoint() const noexcept {
  return implementation_->response_stage_.local_endpoint();
}

std::size_t LiveRuntimeStage::pending_event_count() const noexcept {
  return implementation_->event_size_;
}

std::size_t LiveRuntimeStage::transmitted_packet_count() const noexcept {
  return implementation_->response_stage_.transmitted_packet_count();
}

std::size_t LiveRuntimeStage::cleanup_count() const noexcept {
  return implementation_->cleanup_count_;
}

std::size_t LiveRuntimeStage::usercmd_transmit_count() const noexcept {
  return implementation_->usercmd_stage_
             ? implementation_->usercmd_stage_->transmitted_packet_count()
             : 0U;
}

bool LiveRuntimeStage::live_visual_input_ready() const noexcept {
  return implementation_->state_ ==
         LiveRuntimeStageState::waiting_for_live_visual_input;
}

const ResourceListState *LiveRuntimeStage::live_resource_list() const noexcept {
  const auto &response = implementation_->response_stage_.result();
  return response ? &response->resource_list().resource_list() : nullptr;
}

const ServerInfoState *LiveRuntimeStage::live_server_info() const noexcept {
  const auto &response = implementation_->response_stage_.result();
  return response ? &response->resource_list()
                         .transition()
                         .user_info()
                         .movement_environment()
                         .delta_description()
                         .pre_resource()
                         .server_info()
                  : nullptr;
}

std::string_view LiveRuntimeStage::live_sky_name() const noexcept {
  const auto &response = implementation_->response_stage_.result();
  return response ? std::string_view{response->resource_list()
                         .transition().user_info().movement_environment()
                         .move_vars().sky_name()}
                  : std::string_view{};
}

std::optional<LiveUserCmdCheckState>
LiveRuntimeStage::live_usercmd_snapshot() const {
  return implementation_->usercmd_snapshot();
}
std::size_t LiveRuntimeStage::live_use_new_submission_count() const noexcept {
  return implementation_->use_new_submission_count();
}

std::optional<LiveWeaponCommandSubmission>
LiveRuntimeStage::poll_weapon_command_submission() {
  return implementation_->poll_weapon_command_submission();
}

} // namespace hlclient::goldsrc
