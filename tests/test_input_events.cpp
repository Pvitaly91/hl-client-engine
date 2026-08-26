#include <hlclient/input/input_event.hpp>
#include <hlclient/input/input_snapshot.hpp>

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace {

namespace input = hlclient::input;

TEST_CASE("Platform-neutral input events own only bounded typed metadata", "[input][event]")
{
    const auto key = input::InputEvent::key_pressed(input::PhysicalKey::w, true);
    CHECK(key.type() == input::InputEventType::key_pressed);
    CHECK(key.key() == input::PhysicalKey::w);
    CHECK(key.repeated());

    const auto button =
        input::InputEvent::mouse_button_released(input::PhysicalMouseButton::x2);
    CHECK(button.type() == input::InputEventType::mouse_button_released);
    CHECK(button.mouse_button() == input::PhysicalMouseButton::x2);

    const auto motion = input::InputEvent::mouse_motion(-123, 456);
    CHECK(motion.type() == input::InputEventType::mouse_motion);
    CHECK(motion.relative_mouse_delta() == input::RelativeMouseDelta{-123, 456});

    const auto wheel = input::InputEvent::mouse_wheel(-0.5, 1.25);
    CHECK(wheel.type() == input::InputEventType::mouse_wheel);
    CHECK(wheel.wheel_delta() == input::MouseWheelDelta{-0.5, 1.25});
}

TEST_CASE("Every required physical input has a closed identity", "[input][event]")
{
    constexpr input::PhysicalKey keys[]{
        input::PhysicalKey::w,
        input::PhysicalKey::a,
        input::PhysicalKey::s,
        input::PhysicalKey::d,
        input::PhysicalKey::space,
        input::PhysicalKey::left_control,
        input::PhysicalKey::left_shift,
        input::PhysicalKey::e,
        input::PhysicalKey::r,
        input::PhysicalKey::escape,
        input::PhysicalKey::tab,
        input::PhysicalKey::up,
        input::PhysicalKey::down,
        input::PhysicalKey::left,
        input::PhysicalKey::right,
        input::PhysicalKey::f1,
        input::PhysicalKey::f2,
    };
    constexpr input::PhysicalMouseButton buttons[]{
        input::PhysicalMouseButton::left,
        input::PhysicalMouseButton::right,
        input::PhysicalMouseButton::middle,
        input::PhysicalMouseButton::x1,
        input::PhysicalMouseButton::x2,
    };

    for (const auto key : keys) {
        CHECK(input::is_valid(key));
    }
    for (const auto button : buttons) {
        CHECK(input::is_valid(button));
    }
    CHECK_FALSE(input::is_valid(input::PhysicalKey::count));
    CHECK_FALSE(input::is_valid(input::PhysicalMouseButton::count));
    CHECK_FALSE(input::is_valid(static_cast<input::PhysicalKey>(UINT8_MAX)));
    CHECK_FALSE(input::is_valid(static_cast<input::PhysicalMouseButton>(UINT8_MAX)));
}

TEST_CASE("Input snapshots expose no public mutation or borrowed platform state", "[input][snapshot]")
{
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<input::InputSnapshot>);
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<input::InputSnapshot>);
    STATIC_REQUIRE(std::is_copy_constructible_v<input::InputSnapshot>);
    STATIC_REQUIRE(std::is_move_constructible_v<input::InputSnapshot>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<input::InputSnapshot>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<input::InputSnapshot>);
}

} // namespace
