#include <hlclient/client/runtime_observation.hpp>

#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::client {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= kFnvPrime;
}

template <typename Value>
void hash_unsigned(std::uint64_t& hash, const Value value) noexcept
{
    auto remaining = static_cast<std::uint64_t>(value);
    for (std::size_t index = 0U; index < sizeof(Value); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(remaining & 0xffU));
        remaining >>= 8U;
    }
}

void hash_optional_double(
    std::uint64_t& hash,
    const std::optional<double>& value) noexcept
{
    hash_byte(hash, value ? 1U : 0U);
    if (value) {
        hash_unsigned(hash, std::bit_cast<std::uint64_t>(*value));
    }
}

void hash_vector(
    std::uint64_t& hash,
    const RuntimeVector3Observation& value) noexcept
{
    hash_optional_double(hash, value.x);
    hash_optional_double(hash, value.y);
    hash_optional_double(hash, value.z);
}

[[nodiscard]] bool finite_optional(
    const std::optional<double>& value) noexcept
{
    return !value || std::isfinite(*value);
}

[[nodiscard]] bool valid_vector(
    const RuntimeVector3Observation& value) noexcept
{
    return finite_optional(value.x) && finite_optional(value.y) &&
        finite_optional(value.z);
}

[[nodiscard]] bool valid_metadata(
    const RuntimeSubstateMetadata& metadata,
    const std::uint64_t generation) noexcept
{
    if (metadata.generation != generation) {
        return false;
    }
    if (metadata.freshness == RuntimeObservationFreshness::unavailable) {
        return metadata.completeness ==
                RuntimeObservationCompleteness::unavailable &&
            !metadata.source;
    }
    return metadata.completeness ==
            RuntimeObservationCompleteness::complete_reconstruction &&
        metadata.source && metadata.source->record_identity != 0U &&
        metadata.source->record_ordinal != 0U &&
        metadata.source->end_bit_offset >= metadata.source->start_bit_offset;
}

} // namespace

std::uint64_t runtime_observation_canonical_hash(
    const RuntimeClientObservationState& state) noexcept
{
    std::uint64_t hash = kFnvOffset;
    hash_unsigned(hash, static_cast<std::uint8_t>(state.profile));
    hash_unsigned(hash, state.generation);
    hash_optional_double(hash, state.server_time_seconds);
    hash_unsigned(hash, state.packet_entities.size());
    for (const auto& entity : state.packet_entities) {
        hash_unsigned(hash, entity.entity_number);
        hash_vector(hash, entity.origin);
        hash_vector(hash, entity.angles);
    }
    hash_byte(hash, state.receiving_client ? 1U : 0U);
    if (state.receiving_client) {
        hash_optional_double(hash, state.receiving_client->health);
        // Receiving-client origin was added after the stable A-H canonical
        // profile. Keep it out of this legacy hash; E validates it directly.
        hash_vector(hash, state.receiving_client->velocity);
        hash_vector(hash, state.receiving_client->view_offset);
    }
    hash_unsigned(hash, state.weapon_slots.size());
    for (const auto& weapon : state.weapon_slots) {
        hash_byte(hash, weapon.wire_slot);
        hash_byte(hash, weapon.clip ? 1U : 0U);
        if (weapon.clip) {
            hash_unsigned(hash, static_cast<std::uint32_t>(*weapon.clip));
        }
        hash_byte(hash, weapon.in_reload ? 1U : 0U);
        if (weapon.in_reload) {
            hash_byte(hash, *weapon.in_reload ? 1U : 0U);
        }
        hash_optional_double(hash, weapon.next_reload);
        hash_optional_double(hash, weapon.next_primary_attack);
    }
    return hash;
}

std::uint64_t runtime_observation_visual_hash(
    const RuntimeClientObservationState& state) noexcept
{
    auto hash = runtime_observation_canonical_hash(state);
    const auto integer = [&](const auto& value) {
        hash_byte(hash, value ? 1U : 0U);
        if (value) { hash_unsigned(hash, static_cast<std::uint32_t>(*value)); }
    };
    for (const auto& entity : state.packet_entities) {
        hash_byte(hash, entity.ordinary_visual_schema ? 1U : 0U);
        integer(entity.model_index); integer(entity.sequence);
        hash_optional_double(hash, entity.frame);
        integer(entity.body); integer(entity.skin);
        integer(entity.render_mode); integer(entity.effects);
        for (const auto& value : entity.controllers) { integer(value); }
        for (const auto& value : entity.blending) { integer(value); }
    }
    return hash;
}

bool valid_runtime_observation(
    const RuntimeClientObservationState& state) noexcept
{
    if (state.profile !=
            RuntimeObservationProfile::public_goldsrc48_runtime_replay_v1 ||
        state.generation == 0U || state.publication_revision == 0U ||
        !finite_optional(state.server_time_seconds) ||
        !valid_metadata(state.server_time_metadata, state.generation) ||
        !valid_metadata(state.entity_metadata, state.generation) ||
        !valid_metadata(state.client_metadata, state.generation) ||
        state.packet_entities.size() > kMaximumRuntimeObservationEntities ||
        state.weapon_slots.size() > kMaximumRuntimeObservationWeaponSlots) {
        return false;
    }
    if (state.server_time_seconds.has_value() !=
        (state.server_time_metadata.freshness !=
         RuntimeObservationFreshness::unavailable)) {
        return false;
    }
    if (state.receiving_client.has_value() !=
        (state.client_metadata.freshness !=
         RuntimeObservationFreshness::unavailable)) {
        return false;
    }
    if (!state.receiving_client && !state.weapon_slots.empty()) {
        return false;
    }
    if (state.entity_metadata.freshness ==
            RuntimeObservationFreshness::unavailable &&
        !state.packet_entities.empty()) {
        return false;
    }
    std::uint32_t previous_entity = 0U;
    for (const auto& entity : state.packet_entities) {
        if (entity.entity_number == 0U ||
            entity.entity_number <= previous_entity ||
            !valid_vector(entity.origin) || !valid_vector(entity.angles) ||
            !finite_optional(entity.frame) ||
            (entity.frame && *entity.frame < 0.0) ||
            (!entity.player_movement_schema &&
                (entity.move_type || entity.use_hull ||
                 entity.gravity_multiplier || entity.friction_multiplier ||
                 entity.base_velocity.x || entity.base_velocity.y ||
                 entity.base_velocity.z || entity.spectator)) ||
            !finite_optional(entity.gravity_multiplier) ||
            !finite_optional(entity.friction_multiplier) ||
            !valid_vector(entity.base_velocity) ||
            (entity.use_hull && *entity.use_hull > 1U) ||
            !std::ranges::all_of(entity.controllers, [](const auto value) { return !value || *value <= 255U; }) ||
            !std::ranges::all_of(entity.blending, [](const auto value) { return !value || *value <= 255U; })) {
            return false;
        }
        previous_entity = entity.entity_number;
    }
    if (state.receiving_client &&
        (!finite_optional(state.receiving_client->health) ||
         !valid_vector(state.receiving_client->origin) ||
         !valid_vector(state.receiving_client->velocity) ||
         !valid_vector(state.receiving_client->view_offset) ||
         !finite_optional(state.receiving_client->maximum_speed) ||
         (state.receiving_client->maximum_speed &&
             *state.receiving_client->maximum_speed < 0.0) ||
         (state.receiving_client->water_level &&
             *state.receiving_client->water_level > 3U))) {
        return false;
    }
    if (state.view_angle_correction &&
        (!std::isfinite(state.view_angle_correction->pitch_degrees) ||
         !std::isfinite(state.view_angle_correction->yaw_degrees) ||
         !std::isfinite(state.view_angle_correction->roll_degrees) ||
         state.view_angle_correction->source.record_identity == 0U ||
         state.view_angle_correction->source.record_ordinal == 0U ||
         state.view_angle_correction->source.end_bit_offset <
             state.view_angle_correction->source.start_bit_offset)) {
        return false;
    }
    std::uint16_t previous_slot = 0U;
    bool first_slot = true;
    for (const auto& weapon : state.weapon_slots) {
        if (weapon.wire_slot >= kMaximumRuntimeObservationWeaponSlots ||
            (!first_slot && weapon.wire_slot <= previous_slot) ||
            !finite_optional(weapon.next_reload) ||
            !finite_optional(weapon.next_primary_attack)) {
            return false;
        }
        first_slot = false;
        previous_slot = weapon.wire_slot;
    }
    return state.canonical_state_hash != 0U &&
        state.canonical_state_hash ==
            runtime_observation_canonical_hash(state);
}

} // namespace hlclient::client
