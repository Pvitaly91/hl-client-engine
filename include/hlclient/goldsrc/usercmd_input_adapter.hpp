#pragma once

#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc {

struct GoldSrcUserCmdMovementSpeedConfig {
    float forward_speed{400.0F};
    float backward_speed{400.0F};
    float side_speed{400.0F};
};

struct GoldSrcUserCmdBuildContext {
    GoldSrcUserCmdSequence command_sequence{};
    std::uint8_t command_msec{0U};
    std::uint64_t command_sample_duration_nanoseconds{0U};
    std::int64_t command_sample_time_nanoseconds{0};
    std::uint16_t lerp_msec{0U};
    GoldSrcUserCmdMovementSpeedConfig movement_speeds{};
    std::uint8_t light_level{0U};
    // Project gameplay-button edges retained by the caller until this exact
    // command is published to history. They are mapped explicitly and are
    // never treated as native GoldSrc button bits.
    gameplay_input::GameplayButtonMask one_shot_buttons{0U};
    std::optional<std::uint8_t> impulse;
    std::optional<std::uint8_t> weapon_selection;
    std::int32_t impact_index{0};
    std::array<float, 3U> impact_position{};
    std::array<float, 3U> previous_absolute_view_angles{};
    bool strict_unmapped_actions{false};
    GoldSrcUserCmdCompatibilityProfile compatibility_profile{
        GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1};
    GoldSrcUserCmdInputMappingProfile mapping_profile{
        GoldSrcUserCmdInputMappingProfile::synthetic_explicit_v1};
};

enum class GoldSrcUserCmdInputAdapterErrorCode : std::uint8_t {
    invalid_context,
    unsupported_profile,
    stock_evidence_pending,
    unsupported_action,
    unsupported_weapon_selection,
    state_validation_failed,
};

struct GoldSrcUserCmdInputAdapterError {
    GoldSrcUserCmdInputAdapterErrorCode code{
        GoldSrcUserCmdInputAdapterErrorCode::invalid_context};
    std::optional<GoldSrcUserCmdErrorCode> state_code;
    std::string_view context;
};

class GoldSrcUserCmdOneShotPlan final {
public:
    GoldSrcUserCmdOneShotPlan(const GoldSrcUserCmdOneShotPlan&) = delete;
    GoldSrcUserCmdOneShotPlan& operator=(const GoldSrcUserCmdOneShotPlan&) = delete;
    GoldSrcUserCmdOneShotPlan(GoldSrcUserCmdOneShotPlan&& other) noexcept;
    GoldSrcUserCmdOneShotPlan& operator=(
        GoldSrcUserCmdOneShotPlan&& other) noexcept;

    [[nodiscard]] GoldSrcUserCmdSequence command_sequence() const noexcept;
    [[nodiscard]] bool consumes_impulse() const noexcept;
    [[nodiscard]] bool consumes_weapon_selection() const noexcept;
    [[nodiscard]] gameplay_input::GameplayButtonMask consumes_buttons()
        const noexcept;
    [[nodiscard]] bool committed() const noexcept;
    [[nodiscard]] bool commit_after_history_insert(
        GoldSrcUserCmdSequence inserted_sequence) noexcept;
    void abandon() noexcept;

private:
    friend class GoldSrcUserCmdInputAdapter;

    GoldSrcUserCmdOneShotPlan(
        GoldSrcUserCmdSequence sequence,
        gameplay_input::GameplayButtonMask consumes_buttons,
        bool consumes_impulse,
        bool consumes_weapon_selection) noexcept;

    GoldSrcUserCmdSequence sequence_{};
    gameplay_input::GameplayButtonMask consumes_buttons_{0U};
    bool consumes_impulse_{false};
    bool consumes_weapon_selection_{false};
    bool consumable_{true};
    bool committed_{false};
};

struct GoldSrcUserCmdBuildResult {
    std::optional<GoldSrcUserCmdState> command;
    std::optional<GoldSrcUserCmdOneShotPlan> one_shot_plan;
    gameplay_input::GameplayButtonMask ignored_actions{0U};
    std::optional<GoldSrcUserCmdInputAdapterError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return command.has_value();
    }
};

class GoldSrcUserCmdButtonMapping final {
public:
    struct Result {
        std::uint16_t wire_buttons{0U};
        gameplay_input::GameplayButtonMask ignored_actions{0U};
    };

    [[nodiscard]] static Result synthetic_explicit_v1(
        gameplay_input::GameplayButtonMask held_actions) noexcept;
};

class GoldSrcUserCmdInputAdapter final {
public:
    [[nodiscard]] GoldSrcUserCmdBuildResult build(
        const gameplay_input::GameplayInputIntent& intent,
        const gameplay_camera::GameplayCameraState& camera,
        const GoldSrcUserCmdBuildContext& context,
        const GoldSrcUserCmdLimits& limits = {}) const noexcept;
};

} // namespace hlclient::goldsrc
