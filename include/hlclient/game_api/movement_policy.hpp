#pragma once

#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_config.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_profile.hpp>
#include <hlclient/goldsrc/movement/local_movement_query_config.hpp>
#include <hlclient/goldsrc/usercmd_movement_policy.hpp>

#include <memory>
#include <optional>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hlclient::game_api {

// Immutable session policy. These are project-owned values, not SDK ABI types,
// query services or handles. Server MoveVars are supplied separately and never
// substituted by this policy. A default-constructed policy is unavailable.
struct GameMovementPolicy final {
    std::shared_ptr<const gameplay_input::GameplayInputBindings> bindings;
    gameplay_input::MouseLookConfig mouse_look{};
    gameplay_input::GameplayButtonMask live_buttons{};
    gameplay_input::GameplayButtonMask held_buttons{};
    // Actions requiring current capture; reset invalidates them until a new
    // physical press or release. Other movement/weapon contracts are unchanged.
    gameplay_input::GameplayButtonMask capture_life_scoped_buttons{};
    std::array<input::PhysicalKey, 5U> inventory_group_keys{};
    std::optional<input::PhysicalKey> inventory_last_key;
    std::uint32_t inventory_repeat_milliseconds{100U};
    goldsrc::GoldSrcUserCmdMovementSpeedConfig movement_speeds{0.0F, 0.0F, 0.0F};
    float speed_key_multiplier{0.0F};
    // Explicit opt-in to the bounded, derived vertical PUSH trajectory. This
    // is not stock pusher authority or support for rotating/horizontal trains.
    bool derived_vertical_support{false};
    // Explicit current-snapshot compatibility for ordered coherent ordinary
    // movement frames sharing a packet with reliable controls. Not arbitrary
    // reliable/reassembled data or a transport executed-command ACK claim.
    bool coherent_unfragmented_movement_frames{false};
    // Explicit game selection of server-described axis-aligned blocking boxes.
    // No default shape, classname/model lookup or local object physics.
    std::optional<std::uint32_t> server_box_solid_code;
    // Player-schema boxes require a separate explicit game opt-in. Bounds are
    // still negotiated server fields; never infer a shape from Studio models.
    std::optional<std::uint32_t> server_player_solid_code;
    // Narrow immutable game profile; the host executes it on the same current
    // committed frame. No blanket exemption for point boxes or Studio meshes.
    std::vector<goldsrc::movement::ServerPointBoxExclusionRule> nonblocking_point_boxes;
    bool exclude_receiving_client_owned_boxes{false};
    // Optional game-owned rules for the host's server-marked brush ladder
    // mechanism. Absence never silently selects Half-Life behavior.
    std::optional<goldsrc::movement::ReferenceLadderMovementPolicy> reference_ladder;
    // A concrete game selects its exact server physinfo capability key.
    // Empty disables projection; the engine assigns no meaning to other keys.
    std::string crouch_jump_physics_key;
    goldsrc::GoldSrcReferenceButtonPolicy button_policy{
        goldsrc::GoldSrcReferenceButtonPolicy::none};
    goldsrc::movement::GoldSrcLocalMovementConfig movement_config{};
    goldsrc::movement::GoldSrcMovementEnvironmentProfile environment_profile{
        goldsrc::movement::GoldSrcMovementEnvironmentProfile::
            stock_pm_move_full_compatibility_evidence_pending};
};

[[nodiscard]] bool valid_game_movement_policy(
    const GameMovementPolicy& policy) noexcept;

// Host-owned neutral gate. No object selection, gameplay effects or replay.
class ScopedGameplayButtonGate final {
public:
    void invalidate(gameplay_input::GameplayButtonMask mask) noexcept { blocked_ |= mask; }
    void filter(gameplay_input::GameplayButtonMask mask, bool active,
                gameplay_input::GameplayButtonMask& held,
                gameplay_input::GameplayButtonMask& pressed,
                gameplay_input::GameplayButtonMask released) noexcept {
        if (!active) { invalidate(mask); held &= ~mask; pressed &= ~mask; return; }
        blocked_ &= ~(released | pressed);
        held &= ~(blocked_ & mask);
        pressed &= ~(blocked_ & mask);
    }
private:
    gameplay_input::GameplayButtonMask blocked_{};
};

} // namespace hlclient::game_api
