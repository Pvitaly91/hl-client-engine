#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hlclient::client {

inline constexpr std::size_t kMaximumRuntimeObservationEntities = 16'384U;
inline constexpr std::size_t kMaximumRuntimeObservationWeaponSlots = 64U;

enum class RuntimeObservationProfile : std::uint8_t {
    public_goldsrc48_runtime_replay_v1,
};

enum class RuntimeObservationFreshness : std::uint8_t {
    unavailable,
    retained,
    observed_in_record,
};

enum class RuntimeObservationCompleteness : std::uint8_t {
    unavailable,
    complete_reconstruction,
};

struct RuntimeObservationSource final {
    std::uint64_t record_identity{0U};
    std::size_t record_ordinal{0U};
    std::uint32_t source_transport_sequence{0U};
    std::size_t start_bit_offset{0U};
    std::size_t end_bit_offset{0U};
    // Header metadata from this exact owning service payload. A reliable or
    // reassembled body may predate its carrier ACK and cannot by itself anchor
    // movement reconciliation.
    std::optional<std::uint32_t> carrier_acknowledgement;
    bool source_reliable{false};
    bool reassembled{false};

    [[nodiscard]] friend bool operator==(
        const RuntimeObservationSource&,
        const RuntimeObservationSource&) noexcept = default;
};

struct RuntimeSubstateMetadata final {
    std::uint64_t generation{0U};
    RuntimeObservationFreshness freshness{
        RuntimeObservationFreshness::unavailable};
    RuntimeObservationCompleteness completeness{
        RuntimeObservationCompleteness::unavailable};
    std::optional<RuntimeObservationSource> source;

    [[nodiscard]] friend bool operator==(
        const RuntimeSubstateMetadata&,
        const RuntimeSubstateMetadata&) noexcept = default;
};

struct RuntimeVector3Observation final {
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;

    [[nodiscard]] bool complete() const noexcept
    {
        return x.has_value() && y.has_value() && z.has_value();
    }

    [[nodiscard]] friend bool operator==(
        const RuntimeVector3Observation&,
        const RuntimeVector3Observation&) noexcept = default;
};

struct RuntimePacketEntityObservation final {
    std::uint32_t entity_number{0U};
    RuntimeVector3Observation origin;
    RuntimeVector3Observation angles;
    // Public-reference visual semantics, separate from the historical H
    // observation hash profile. Missing descriptors remain unavailable.
    bool ordinary_visual_schema{false};
    // Neutral protocol fields; absence is not SOLID_NOT/MOVETYPE_NONE.
    std::optional<std::uint32_t> solid;
    std::optional<std::uint32_t> brush_move_type;
    std::optional<std::uint32_t> model_index;
    std::optional<std::uint32_t> sequence;
    std::optional<double> frame;
    // Exact advertised delta semantics, never SDK ABI defaults.
    std::optional<double> animation_time_seconds, frame_rate;
    std::optional<std::uint32_t> gait_sequence, weapon_model_index;
    RuntimeVector3Observation velocity;
    std::optional<std::uint32_t> body;
    std::optional<std::int32_t> skin;
    std::optional<std::uint32_t> render_mode;
    std::optional<std::uint32_t> effects;
    std::array<std::optional<std::uint32_t>, 4U> controllers;
    std::array<std::optional<std::uint32_t>, 2U> blending;
    // Player-state semantics are projected only for the exact public player
    // schema. Entity identity and record coherence still need checking by a
    // prediction consumer before these may be used as the receiving player.
    bool player_movement_schema{false};
    std::optional<std::uint32_t> move_type;
    std::optional<std::uint32_t> use_hull;
    std::optional<double> gravity_multiplier;
    std::optional<double> friction_multiplier;
    RuntimeVector3Observation base_velocity;
    std::optional<bool> spectator;

    [[nodiscard]] friend bool operator==(
        const RuntimePacketEntityObservation&,
        const RuntimePacketEntityObservation&) noexcept = default;
};

struct RuntimeReceivingClientObservation final {
    std::optional<double> health;
    // Negotiated clientdata fields. Zero viewmodel explicitly hides the model;
    // an absent descriptor is never treated as a zero.
    std::optional<std::uint32_t> viewmodel_index;
    std::optional<std::uint32_t> owned_weapon_bits;
    std::optional<std::uint32_t> weapon_animation;
    // Optional public clientdata decrement timer; presentation eligibility only.
    std::optional<double> next_weapon_attack;
    // Exact clientdata_t receiving-client coordinates. This is deliberately
    // separate from packet-entity identity and is never inferred from an
    // entity number, setview, camera, or local prediction.
    RuntimeVector3Observation origin;
    RuntimeVector3Observation velocity;
    RuntimeVector3Observation view_offset;
    // Server clientdata punchangle; unavailable components stay absent.
    // Presentation applies it without changing local mouse angles or usercmds.
    RuntimeVector3Observation punch_angle;
    // Public clientdata_t fields; optional means descriptor/value unavailable,
    // never a locally fabricated default.
    std::optional<std::uint32_t> flags;
    std::optional<double> maximum_speed;
    std::optional<std::uint32_t> duck_time;
    std::optional<bool> in_duck;
    std::optional<std::uint32_t> water_level;
    std::optional<std::uint32_t> dead_flag;

    [[nodiscard]] friend bool operator==(
        const RuntimeReceivingClientObservation&,
        const RuntimeReceivingClientObservation&) noexcept = default;
};

struct RuntimeWeaponTypeObservation final {
    std::uint8_t id{0U};
    std::string command_name;
    std::int8_t primary_ammo_type{-1};
    std::int8_t secondary_ammo_type{-1};
    std::uint8_t slot{0U};
    std::uint8_t position{0U};
    std::uint8_t flags{0U};
    RuntimeObservationSource source{};
    [[nodiscard]] friend bool operator==(const RuntimeWeaponTypeObservation&,
        const RuntimeWeaponTypeObservation&) = default;
};

struct RuntimeWeaponHudObservation final {
    std::vector<RuntimeWeaponTypeObservation> catalogue;
    std::array<std::optional<std::uint8_t>, 64U> reserve_ammo{};
    std::array<std::optional<std::int16_t>, 64U> clips{};
    // Exact absolute-counter message source, retained independently per slot.
    // Provenance is not part of the existing canonical/value hash profiles.
    std::array<std::optional<RuntimeObservationSource>, 64U> reserve_ammo_sources{};
    std::array<std::optional<RuntimeObservationSource>, 64U> clip_sources{};
    std::optional<std::uint8_t> active_weapon_id;
    std::optional<std::int16_t> armor;
    std::optional<std::uint8_t> health;
    std::optional<std::uint8_t> field_of_view;
    std::optional<std::uint8_t> hide_flags;
    std::optional<std::uint8_t> animation_sequence;
    std::optional<std::uint8_t> animation_body;
    std::optional<RuntimeObservationSource> active_source;
    std::optional<RuntimeObservationSource> animation_source;
    std::optional<RuntimeObservationSource> last_message_source;
    std::optional<RuntimeObservationSource> health_source, armor_source;
    std::uint64_t revision{0U};
    [[nodiscard]] friend bool operator==(const RuntimeWeaponHudObservation&,
        const RuntimeWeaponHudObservation&) = default;
};

enum class LocalPlayerLifeState : std::uint8_t {
    unknown, alive, dead, awaiting_respawn
};
enum class RuntimeLifeEventKind : std::uint8_t {
    damage, death_notice, reset_hud, init_hud
};
struct RuntimeLifeEvent final {
    RuntimeLifeEventKind kind{};
    RuntimeObservationSource source{};
    std::uint8_t armor_saved{}, damage_taken{}, killer_entity{}, victim_entity{};
    std::uint32_t damage_bits{};
    std::array<double, 3U> damage_origin{};
    std::string weapon_text; // bounded inert server text; never a command/path
    friend bool operator==(const RuntimeLifeEvent&, const RuntimeLifeEvent&) = default;
};
// Local epoch is NOT transport generation or an execution ACK. This snapshot
// is committed with the whole record by the sole renderer-neutral owner.
struct LocalPlayerLifecycle final {
    LocalPlayerLifeState state{LocalPlayerLifeState::unknown};
    std::uint64_t life_epoch{}, deaths{}, respawns{}, damage_events{}, feedback_revision{};
    std::optional<RuntimeObservationSource> boundary_source, last_client_source;
    std::optional<RuntimeObservationSource> pending_death, last_damage_source;
    std::optional<RuntimeObservationSource> last_death_source;
    std::size_t event_record_high_water{};
    std::optional<double> health_before_damage, health_after_damage;
    std::optional<std::int16_t> armor_before_damage, armor_after_damage;
    std::optional<RuntimeObservationSource> health_before_source, health_after_source;
    std::optional<RuntimeObservationSource> armor_before_source, armor_after_source;
    std::optional<std::uint32_t> dead_flag;
    std::optional<std::uint32_t> pre_death_model, post_respawn_model;
    std::optional<std::uint8_t> pre_death_weapon, post_respawn_weapon;
    [[nodiscard]] bool dead() const noexcept {
        return state == LocalPlayerLifeState::dead ||
            state == LocalPlayerLifeState::awaiting_respawn;
    }
    friend bool operator==(const LocalPlayerLifecycle&, const LocalPlayerLifecycle&) = default;
};

// One-shot svc_setangle correction observed in the same committed server
// record. It is not retained into later publications: consumers must apply a
// source at most once, then continue from their local input orientation.
// Angles retain GoldSrc wire semantics: downward-positive pitch in degrees,
// possibly represented as an unsigned turn. Camera consumers convert them;
// protocol observations and their canonical hash remain unchanged.
struct RuntimeViewAngleCorrection final {
    double pitch_degrees{0.0};
    double yaw_degrees{0.0};
    double roll_degrees{0.0};
    RuntimeObservationSource source{};

    [[nodiscard]] friend bool operator==(
        const RuntimeViewAngleCorrection&,
        const RuntimeViewAngleCorrection&) noexcept = default;
};

struct RuntimeWeaponSlotObservation final {
    std::uint8_t wire_slot{0U};
    std::optional<std::int32_t> clip;
    std::optional<bool> in_reload;
    std::optional<double> next_reload;
    std::optional<double> next_primary_attack;
    std::optional<std::uint32_t> weapon_id;
    std::optional<double> next_secondary_attack;
    std::optional<double> time_weapon_idle;

    [[nodiscard]] friend bool operator==(
        const RuntimeWeaponSlotObservation&,
        const RuntimeWeaponSlotObservation&) noexcept = default;
};

// Renderer-neutral, client-owned observation. It deliberately contains no
// packet bytes, delta descriptors, SDK layouts, socket ownership or resource
// handles. Missing semantic fields remain std::nullopt.
class RuntimeClientObservationState final {
public:
    RuntimeObservationProfile profile{
        RuntimeObservationProfile::public_goldsrc48_runtime_replay_v1};
    std::uint64_t generation{0U};
    std::uint64_t publication_revision{0U};
    std::optional<double> server_time_seconds;
    RuntimeSubstateMetadata server_time_metadata;
    RuntimeSubstateMetadata entity_metadata;
    RuntimeSubstateMetadata client_metadata;
    std::vector<RuntimePacketEntityObservation> packet_entities;
    std::optional<RuntimeReceivingClientObservation> receiving_client;
    std::optional<RuntimeViewAngleCorrection> view_angle_correction;
    std::vector<RuntimeWeaponSlotObservation> weapon_slots;
    RuntimeWeaponHudObservation weapon_hud;
    std::vector<RuntimeLifeEvent> life_events; // current committed record only
    LocalPlayerLifecycle lifecycle;
    std::uint64_t canonical_state_hash{0U};

    [[nodiscard]] friend bool operator==(
        const RuntimeClientObservationState&,
        const RuntimeClientObservationState&) noexcept = default;
};

[[nodiscard]] bool valid_runtime_observation(
    const RuntimeClientObservationState& state) noexcept;

// The stable A-H canonical hash excludes publication revision,
// record identity/ordinal, cursor geometry, freshness, and the later E
// receiving-client origin and one-shot view-angle extensions. Those
// exclusions preserve the
// historical replay hash contract while origin remains available as an exact
// typed observation for live movement evaluation.
[[nodiscard]] std::uint64_t runtime_observation_canonical_hash(
    const RuntimeClientObservationState& state) noexcept;

// I visual extension, separate from the stable A-H canonical profile above.
// Includes missingness and all committed ordinary visual semantic fields.
[[nodiscard]] std::uint64_t runtime_observation_visual_hash(
    const RuntimeClientObservationState& state) noexcept;
[[nodiscard]] std::uint64_t runtime_observation_visual_hash_v2(
    const RuntimeClientObservationState& state) noexcept;
[[nodiscard]] std::uint64_t runtime_observation_weapon_hud_hash(
    const RuntimeClientObservationState& state) noexcept;

} // namespace hlclient::client
