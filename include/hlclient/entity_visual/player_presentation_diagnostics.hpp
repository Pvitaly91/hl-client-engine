#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hlclient::entity_visual {

enum class PlayerPresentationDecision : std::uint8_t {
    interpolated, exact_previous, exact_current, held_oldest, held_newest,
    held_only, held_floor, held_current,
};
enum class PlayerContinuityReason : std::uint32_t {
    none=0, first_observation=1U<<0, history_stale=1U<<1,
    no_interpolation_option=1U<<2, explicit_discontinuity=1U<<3,
    model_changed=1U<<4, schema_changed=1U<<5, nointerp_effect=1U<<6,
    teleport_distance=1U<<7, removal=1U<<8, reentry=1U<<9,
    same_time_origin_correction=1U<<10, game_boundary=1U<<11,
    clock_reset=1U<<12,
};
enum class PlayerHoldReason : std::uint32_t {
    none=0, floor_actor_missing=1U<<0, bracket_actor_missing=1U<<1,
    target_before_floor=1U<<2, bracket_before_floor=1U<<3,
    selected_model_mismatch=1U<<4, bracket_regression=1U<<5,
    per_actor_reset=1U<<6,
};
enum class PlayerClockReason : std::uint32_t {
    none=0, initial_anchor=1U<<0, explicit_reset=1U<<1,
    generation_changed=1U<<2, server_time_missing=1U<<3,
    server_time_regressed=1U<<4, snapshot_gap=1U<<5,
    application_gap=1U<<6, mapped_time_invalid=1U<<7,
    mapped_time_drift=1U<<8, oldest_underflow=1U<<9,
    newest_underflow=1U<<10,
};

// Numeric masks are intentionally stable, bounded and renderer/protocol-neutral.
// Floor reasons persist until another continuity floor replaces them; only
// new_boundary_reasons represents a boundary newly observed by this sample.
struct PlayerPresentationDiagnostics final {
    PlayerPresentationDecision decision{PlayerPresentationDecision::held_only};
    std::uint32_t floor_reasons{}, new_boundary_reasons{}, hold_reasons{}, clock_reasons{};
    // The logical continuity threshold can predate the bounded history. The
    // resolved floor identifies the actual retained snapshot used this frame.
    std::size_t floor_record_ordinal{}, resolved_floor_record_ordinal{}, position_previous_record_ordinal{}, position_current_record_ordinal{};
    double alpha{};
};

[[nodiscard]] constexpr std::string_view to_string(PlayerPresentationDecision value) noexcept {
    switch(value) {
    case PlayerPresentationDecision::interpolated:return "interpolated";
    case PlayerPresentationDecision::exact_previous:return "exact_previous";
    case PlayerPresentationDecision::exact_current:return "exact_current";
    case PlayerPresentationDecision::held_oldest:return "held_oldest";
    case PlayerPresentationDecision::held_newest:return "held_newest";
    case PlayerPresentationDecision::held_only:return "held_only";
    case PlayerPresentationDecision::held_floor:return "held_floor";
    case PlayerPresentationDecision::held_current:return "held_current";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(PlayerContinuityReason value) noexcept {
    switch(value) {
    case PlayerContinuityReason::none:return "none";
    case PlayerContinuityReason::first_observation:return "first_observation";
    case PlayerContinuityReason::history_stale:return "history_stale";
    case PlayerContinuityReason::no_interpolation_option:return "no_interpolation_option";
    case PlayerContinuityReason::explicit_discontinuity:return "explicit_discontinuity";
    case PlayerContinuityReason::model_changed:return "model_changed";
    case PlayerContinuityReason::schema_changed:return "schema_changed";
    case PlayerContinuityReason::nointerp_effect:return "nointerp_effect";
    case PlayerContinuityReason::teleport_distance:return "teleport_distance";
    case PlayerContinuityReason::removal:return "removal";
    case PlayerContinuityReason::reentry:return "reentry";
    case PlayerContinuityReason::same_time_origin_correction:return "same_time_origin_correction";
    case PlayerContinuityReason::game_boundary:return "game_boundary";
    case PlayerContinuityReason::clock_reset:return "clock_reset";
    }
    return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(PlayerHoldReason value) noexcept {
    switch(value) {
    case PlayerHoldReason::none:return "none";
    case PlayerHoldReason::floor_actor_missing:return "floor_actor_missing";
    case PlayerHoldReason::bracket_actor_missing:return "bracket_actor_missing";
    case PlayerHoldReason::target_before_floor:return "target_before_floor";
    case PlayerHoldReason::bracket_before_floor:return "bracket_before_floor";
    case PlayerHoldReason::selected_model_mismatch:return "selected_model_mismatch";
    case PlayerHoldReason::bracket_regression:return "bracket_regression";
    case PlayerHoldReason::per_actor_reset:return "per_actor_reset";
    }
    return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(PlayerClockReason value) noexcept {
    switch(value) {
    case PlayerClockReason::none:return "none";
    case PlayerClockReason::initial_anchor:return "initial_anchor";
    case PlayerClockReason::explicit_reset:return "explicit_reset";
    case PlayerClockReason::generation_changed:return "generation_changed";
    case PlayerClockReason::server_time_missing:return "server_time_missing";
    case PlayerClockReason::server_time_regressed:return "server_time_regressed";
    case PlayerClockReason::snapshot_gap:return "snapshot_gap";
    case PlayerClockReason::application_gap:return "application_gap";
    case PlayerClockReason::mapped_time_invalid:return "mapped_time_invalid";
    case PlayerClockReason::mapped_time_drift:return "mapped_time_drift";
    case PlayerClockReason::oldest_underflow:return "oldest_underflow";
    case PlayerClockReason::newest_underflow:return "newest_underflow";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual
