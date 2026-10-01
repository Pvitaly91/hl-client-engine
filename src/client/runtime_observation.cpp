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

std::uint64_t runtime_observation_visual_hash_v2(
    const RuntimeClientObservationState& state) noexcept
{
    auto hash = runtime_observation_visual_hash(state);
    hash_byte(hash, 2U);
    for (const auto& entity : state.packet_entities) {
        hash_optional_double(hash, entity.animation_time_seconds);
        hash_optional_double(hash, entity.frame_rate);
        for (const auto value : {entity.gait_sequence, entity.weapon_model_index}) {
            hash_byte(hash, value ? 1U : 0U);
            if (value) hash_unsigned(hash, *value);
        }
        hash_optional_double(hash, entity.velocity.x);
        hash_optional_double(hash, entity.velocity.y);
        hash_optional_double(hash, entity.velocity.z);
    }
    return hash;
}

std::uint64_t runtime_observation_weapon_hud_hash(
    const RuntimeClientObservationState& state) noexcept
{
    auto hash = kFnvOffset;
    hash_unsigned(hash, state.generation);
    const auto integer = [&](const auto& value) {
        hash_byte(hash, value ? 1U : 0U);
        if (value) hash_unsigned(hash, static_cast<std::uint32_t>(*value));
    };
    hash_byte(hash, state.receiving_client ? 1U : 0U);
    if (state.receiving_client) {
        integer(state.receiving_client->viewmodel_index);
        integer(state.receiving_client->owned_weapon_bits);
        integer(state.receiving_client->weapon_animation);
    }
    const auto& hud = state.weapon_hud;
    hash_unsigned(hash, hud.catalogue.size());
    for (const auto& type : hud.catalogue) {
        hash_byte(hash, type.id);
        for (const unsigned char ch : type.command_name) hash_byte(hash, ch);
        hash_byte(hash, 0U);
        hash_byte(hash, static_cast<std::uint8_t>(type.primary_ammo_type));
        hash_byte(hash, static_cast<std::uint8_t>(type.secondary_ammo_type));
        hash_byte(hash, type.slot);
        hash_byte(hash, type.position);
        hash_byte(hash, type.flags);
    }
    for (const auto& value : hud.reserve_ammo) integer(value);
    for (const auto& value : hud.clips) integer(value);
    integer(hud.active_weapon_id);
    integer(hud.armor);
    integer(hud.hide_flags);
    integer(hud.animation_sequence);
    integer(hud.animation_body);
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
        state.weapon_slots.size() > kMaximumRuntimeObservationWeaponSlots ||
        state.weapon_hud.catalogue.size() > 64U) {
        return false;
    }
    const auto valid_source = [](const RuntimeObservationSource& source) {
        return source.record_identity != 0U && source.record_ordinal != 0U &&
            source.end_bit_offset >= source.start_bit_offset;
    };
    if (state.life_events.size() > 256U ||
        !finite_optional(state.lifecycle.health_before_damage) ||
        !finite_optional(state.lifecycle.health_after_damage) ||
        static_cast<unsigned>(state.lifecycle.state) >
            static_cast<unsigned>(LocalPlayerLifeState::awaiting_respawn)) return false;
    for (const auto& event : state.life_events) {
        if (static_cast<unsigned>(event.kind) >
                static_cast<unsigned>(RuntimeLifeEventKind::init_hud) ||
            !valid_source(event.source) || event.weapon_text.size() > 63U ||
            event.weapon_text.find('\0') != std::string::npos ||
            !std::ranges::all_of(event.damage_origin,
                [](double v) { return std::isfinite(v); })) return false;
    }
    for (const auto* source : {&state.lifecycle.boundary_source,
            &state.lifecycle.last_client_source, &state.lifecycle.pending_death,
            &state.lifecycle.last_damage_source, &state.lifecycle.last_death_source,
            &state.lifecycle.health_before_source, &state.lifecycle.health_after_source,
            &state.lifecycle.armor_before_source, &state.lifecycle.armor_after_source,
            &state.weapon_hud.health_source, &state.weapon_hud.armor_source})
        if (*source && !valid_source(**source)) return false;
    std::array<bool, 64U> seen_weapons{};
    for (std::size_t i = 0U; i < state.weapon_hud.reserve_ammo_sources.size(); ++i) {
        const auto& ammo_source = state.weapon_hud.reserve_ammo_sources[i];
        const auto& clip_source = state.weapon_hud.clip_sources[i];
        if ((ammo_source && (!state.weapon_hud.reserve_ammo[i] || !valid_source(*ammo_source))) ||
            (clip_source && (!state.weapon_hud.clips[i] || !valid_source(*clip_source))))
            return false;
    }
    for (const auto& type : state.weapon_hud.catalogue) {
        if (type.id == 0U || type.id >= 32U || seen_weapons[type.id] ||
            type.command_name.empty() || type.command_name.size() > 63U ||
            type.primary_ammo_type >= 64 || type.secondary_ammo_type >= 64 ||
            type.slot > 9U || type.position > 63U ||
            type.source.record_identity == 0U) return false;
        seen_weapons[type.id] = true;
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
            !finite_optional(entity.animation_time_seconds) ||
            !finite_optional(entity.frame_rate) || !valid_vector(entity.velocity) ||
            (!entity.ordinary_visual_schema &&
                (entity.animation_time_seconds || entity.frame_rate ||
                 entity.gait_sequence || entity.weapon_model_index ||
                 entity.velocity.x || entity.velocity.y || entity.velocity.z)) ||
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
         !valid_vector(state.receiving_client->punch_angle) ||
         !finite_optional(state.receiving_client->next_weapon_attack) ||
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
            !finite_optional(weapon.next_primary_attack) ||
            !finite_optional(weapon.next_secondary_attack) ||
            !finite_optional(weapon.time_weapon_idle) ||
            (weapon.weapon_id && *weapon.weapon_id > 31U)) {
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
