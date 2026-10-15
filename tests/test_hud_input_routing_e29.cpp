#include <hlclient/core/command_line.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/game_api/movement_policy.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <initializer_list>
#include <string_view>

TEST_CASE("Q physical edge survives layout translation but not repeats lost focus or uncaptured holds",
    "[last-weapon][e29][hud-input][input][core-only][no-stock]") {
    namespace input=hlclient::input;
    namespace gameplay=hlclient::gameplay_input;
    input::InputStateTracker tracker;
    const auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
    const auto frame=[&](std::initializer_list<input::InputEvent> events,bool request) {
        tracker.begin_frame();for(const auto& event:events)tracker.apply_event(event);
        const auto snapshot=tracker.publish_snapshot();
        CHECK((snapshot.focused() && snapshot.captured() && snapshot.key_pressed(input::PhysicalKey::q))==request);
        const auto intent=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.016);
        REQUIRE(intent);CHECK(intent.intent->pressed_buttons()==0);CHECK(intent.intent->held_buttons()==0);
        CHECK_FALSE(intent.intent->capture_mouse_requested());CHECK_FALSE(intent.intent->release_mouse_requested());
        tracker.end_frame();
    };
    frame({input::InputEvent::focus_gained(),input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::q)},true);
    frame({input::InputEvent::key_pressed(input::PhysicalKey::q,true)},false);
    frame({},false);
    frame({input::InputEvent::key_released(input::PhysicalKey::q)},false);
    frame({input::InputEvent::key_pressed(input::PhysicalKey::q)},true);
    frame({input::InputEvent::focus_lost()},false);
    frame({input::InputEvent::focus_gained(),input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::q,true)},false);
    frame({input::InputEvent::key_pressed(input::PhysicalKey::q)},true);
    frame({input::InputEvent::capture_released(),input::InputEvent::key_released(input::PhysicalKey::q),
        input::InputEvent::key_pressed(input::PhysicalKey::q)},false);
    frame({input::InputEvent::capture_acquired()},false);
    CHECK_FALSE(hlclient::game_api::GameMovementPolicy{}.inventory_last_key);
}

TEST_CASE("E29 UI consumption removes unsent primary and its pending latch only",
    "[e29][hud-input][core-only][no-stock]") {
    namespace input=hlclient::gameplay_input;
    namespace protocol=hlclient::goldsrc;
    const auto primary=input::gameplay_button_mask(input::GameplayButton::attack_primary);
    const auto secondary=input::gameplay_button_mask(input::GameplayButton::attack_secondary);
    const auto movement=input::gameplay_button_mask(input::GameplayButton::move_forward);
    const auto use=input::gameplay_button_mask(input::GameplayButton::use);
    protocol::LiveVisualButtonLatch latch;
    latch.observe(primary|movement|use,true,primary);
    REQUIRE((latch.pending()&primary)!=0U);
    protocol::LiveVisualControlInput current;
    current.held_buttons=primary|secondary|movement|use;
    current.pressed_buttons=primary|use;
    current.released_buttons=primary|secondary;
    current.forward_axis=1.0F;
    current.yaw_degrees=37.0;
    current.consumed_buttons=primary;
    const auto already_submitted=current;
    protocol::consume_live_visual_input_buttons(current,latch);
    CHECK(current.held_buttons==(secondary|movement|use));
    CHECK(current.pressed_buttons==use);
    CHECK(current.released_buttons==(primary|secondary));
    CHECK(current.forward_axis==1.0F);
    CHECK(current.yaw_degrees==37.0);
    CHECK(latch.pending()==(movement|use));
    CHECK((already_submitted.held_buttons&primary)!=0U);
    CHECK((already_submitted.pressed_buttons&primary)!=0U);
    // This engine helper only applies the current mask. The game module owns
    // any cross-frame UI gesture policy and must request each consumed frame.
    // Zero consumption deliberately leaves later held input available.
    protocol::LiveVisualControlInput following;
    following.held_buttons=primary|movement;
    protocol::consume_live_visual_input_buttons(following,latch);
    CHECK(following.held_buttons==(primary|movement));
}

TEST_CASE("E29 zero consumption preserves the existing input and latch contract",
    "[e29][hud-input][core-only][no-stock]") {
    namespace input=hlclient::gameplay_input;
    namespace protocol=hlclient::goldsrc;
    const auto jump=input::gameplay_button_mask(input::GameplayButton::jump);
    const auto reload=input::gameplay_button_mask(input::GameplayButton::reload);
    protocol::LiveVisualButtonLatch latch;latch.observe(jump,true);
    protocol::LiveVisualControlInput current;
    current.held_buttons=jump|reload;current.pressed_buttons=jump;current.released_buttons=reload;
    protocol::consume_live_visual_input_buttons(current,latch);
    CHECK(current.held_buttons==(jump|reload));
    CHECK(current.pressed_buttons==jump);
    CHECK(current.released_buttons==reload);
    CHECK(latch.pending()==jump);
}

TEST_CASE("E29 deferred Escape release suppresses the same unsent mouse command interval",
    "[e29][hud-input][core-only][no-stock]") {
    namespace input=hlclient::gameplay_input;
    namespace protocol=hlclient::goldsrc;
    const auto primary=input::gameplay_button_mask(input::GameplayButton::attack_primary);
    const auto secondary=input::gameplay_button_mask(input::GameplayButton::attack_secondary);
    const auto forward=input::gameplay_button_mask(input::GameplayButton::move_forward);
    protocol::LiveVisualControlInput current;
    current.focused=current.captured=true;current.forward_axis=1.0F;
    current.held_buttons=primary|secondary|forward;current.pressed_buttons=primary|secondary;
    current.released_buttons=secondary;current.yaw_degrees=17.0;
    const auto transmitted=current;
    protocol::LiveVisualButtonLatch latch;latch.observe(primary|secondary,true,primary|secondary);
    protocol::release_live_visual_input_capture(current,primary|secondary);
    protocol::consume_live_visual_input_buttons(current,latch);
    CHECK_FALSE(current.captured);CHECK(current.focused);CHECK(current.forward_axis==1.0F);
    CHECK(current.held_buttons==forward);CHECK(current.pressed_buttons==0U);
    CHECK(current.released_buttons==secondary);CHECK(current.yaw_degrees==17.0);
    CHECK(latch.pending()==0U);CHECK(transmitted.captured);
    CHECK(transmitted.held_buttons==(primary|secondary|forward));
}

TEST_CASE("E29 HUD settings default to original sprites and explicit bounded diagnostics",
    "[e29][hud-input][cli][core-only]") {
    const auto defaults=hlclient::core::parse_command_line({});
    REQUIRE(defaults);CHECK(defaults.options->hud_draw);CHECK(defaults.options->crosshair);
    CHECK_FALSE(defaults.options->hud_fastswitch);CHECK_FALSE(defaults.options->hud_diagnostics);
    const std::array<std::string_view,7> values{"--hud-draw","0","--crosshair","0",
        "--hud-fastswitch","1","--hud-diagnostics"};
    const auto configured=hlclient::core::parse_command_line(values);
    REQUIRE(configured);CHECK_FALSE(configured.options->hud_draw);CHECK_FALSE(configured.options->crosshair);
    CHECK(configured.options->hud_fastswitch);CHECK(configured.options->hud_diagnostics);
    for(const auto flag:std::array<std::string_view,3>{"--hud-draw","--crosshair","--hud-fastswitch"}) {
        for(const auto invalid:std::array<std::string_view,5>{"2","-1","on","true","1;kill"}) {
            const std::array<std::string_view,2> arguments{flag,invalid};
            CHECK_FALSE(hlclient::core::parse_command_line(arguments));
        }
        const std::array<std::string_view,4> duplicate{flag,"1",flag,"0"};
        CHECK_FALSE(hlclient::core::parse_command_line(duplicate));
        const std::array<std::string_view,1> missing{flag};
        CHECK_FALSE(hlclient::core::parse_command_line(missing));
    }
    const std::array<std::string_view,2> duplicate_diagnostics{"--hud-diagnostics","--hud-diagnostics"};
    CHECK_FALSE(hlclient::core::parse_command_line(duplicate_diagnostics));
}
