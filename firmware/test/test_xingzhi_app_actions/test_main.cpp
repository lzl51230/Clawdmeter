#include <unity.h>

#include "xingzhi_app_actions.h"
#include "xingzhi_buttons.h"

void test_cycle_click_enters_splash_then_advances_animation() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);

    TEST_ASSERT_EQUAL(XingzhiScreen::Usage, state.current_screen);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::CycleScreen,
        XingzhiActionEvent::Click
    );
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(XingzhiScreen::Status, state.current_screen);

    result = xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(XingzhiScreen::Splash, state.current_screen);

    result = xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(result.splash_next);
    TEST_ASSERT_EQUAL_STRING("splash_next", result.message);
    TEST_ASSERT_EQUAL(XingzhiScreen::Splash, state.current_screen);
}

void test_splash_exit_returns_to_previous_non_splash_screen() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);

    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_EQUAL(XingzhiScreen::Status, state.current_screen);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_EQUAL(XingzhiScreen::Splash, state.current_screen);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::ExitSplash,
        XingzhiActionEvent::Click
    );

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("splash_exit", result.message);
    TEST_ASSERT_EQUAL(XingzhiScreen::Status, state.current_screen);
}

void test_cycle_long_press_exits_splash() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::CycleScreen,
        XingzhiActionEvent::LongPress
    );

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(XingzhiScreen::Status, state.current_screen);
}

void test_cycle_after_splash_exit_continues_to_usage() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_EQUAL(XingzhiScreen::Status, state.current_screen);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_EQUAL(XingzhiScreen::Splash, state.current_screen);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_EQUAL(XingzhiScreen::Splash, state.current_screen);
    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::LongPress);
    TEST_ASSERT_EQUAL(XingzhiScreen::Status, state.current_screen);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::CycleScreen,
        XingzhiActionEvent::Click
    );

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("screen_changed", result.message);
    TEST_ASSERT_EQUAL(XingzhiScreen::Usage, state.current_screen);
}

void test_exit_splash_outside_splash_reports_error() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::ExitSplash,
        XingzhiActionEvent::Click
    );

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("not_on_splash", result.message);
    TEST_ASSERT_EQUAL(XingzhiScreen::Usage, state.current_screen);
}

void test_hid_space_press_release_balances_intent() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::HidSpace,
        XingzhiActionEvent::Press
    );
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(state.space_pressed);
    TEST_ASSERT_EQUAL(1U, state.action_count);

    result = xingzhi_actions_dispatch(&state, XingzhiAction::HidSpace, XingzhiActionEvent::Release);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_FALSE(state.space_pressed);
    TEST_ASSERT_EQUAL(2U, state.action_count);
}

void test_shift_tab_click_records_action_without_changing_screen() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::HidShiftTab,
        XingzhiActionEvent::Click
    );

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(XingzhiScreen::Usage, state.current_screen);
    TEST_ASSERT_EQUAL(XingzhiAction::HidShiftTab, state.last_action);
    TEST_ASSERT_EQUAL(XingzhiActionEvent::Click, state.last_event);
}

void test_release_without_press_is_reported() {
    XingzhiActionState state = {};
    xingzhi_actions_init(&state);

    XingzhiActionResult result = xingzhi_actions_dispatch(
        &state,
        XingzhiAction::HidSpace,
        XingzhiActionEvent::Release
    );

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("release_without_press", result.message);
    TEST_ASSERT_FALSE(state.space_pressed);
}

void test_names_are_stable_for_debug_status() {
    TEST_ASSERT_EQUAL_STRING("usage", xingzhi_screen_name(XingzhiScreen::Usage));
    TEST_ASSERT_EQUAL_STRING("status", xingzhi_screen_name(XingzhiScreen::Status));
    TEST_ASSERT_EQUAL_STRING("splash", xingzhi_screen_name(XingzhiScreen::Splash));
    TEST_ASSERT_EQUAL_STRING("exit", xingzhi_action_name(XingzhiAction::ExitSplash));
    TEST_ASSERT_EQUAL_STRING("space", xingzhi_action_name(XingzhiAction::HidSpace));
    TEST_ASSERT_EQUAL_STRING("release", xingzhi_event_name(XingzhiActionEvent::Release));
    TEST_ASSERT_EQUAL_STRING("long_press", xingzhi_event_name(XingzhiActionEvent::LongPress));
}

void test_hid_action_helper_identifies_keyboard_actions() {
    TEST_ASSERT_TRUE(xingzhi_action_is_hid(XingzhiAction::HidSpace));
    TEST_ASSERT_TRUE(xingzhi_action_is_hid(XingzhiAction::HidShiftTab));
    TEST_ASSERT_FALSE(xingzhi_action_is_hid(XingzhiAction::CycleScreen));
    TEST_ASSERT_FALSE(xingzhi_action_is_hid(XingzhiAction::ExitSplash));
}

void test_button_debounce_filters_bounce_until_stable() {
    XingzhiButtonDebounce debounce = {};

    XingzhiButtonEvent event = xingzhi_button_debounce_update(
        &debounce,
        XingzhiAction::CycleScreen,
        false,
        0,
        35
    );
    TEST_ASSERT_FALSE(event.active);

    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 10, 35);
    TEST_ASSERT_FALSE(event.active);
    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, false, 15, 35);
    TEST_ASSERT_FALSE(event.active);
    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 20, 35);
    TEST_ASSERT_FALSE(event.active);

    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 54, 35);
    TEST_ASSERT_FALSE(event.active);
    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 55, 35);
    TEST_ASSERT_TRUE(event.active);
    TEST_ASSERT_EQUAL(XingzhiAction::CycleScreen, event.action);
    TEST_ASSERT_EQUAL(XingzhiActionEvent::Press, event.event);

    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 90, 35);
    TEST_ASSERT_FALSE(event.active);
}

void test_button_debounce_reports_long_press_once_and_suppresses_release() {
    XingzhiButtonDebounce debounce = {};

    xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, false, 0, 35, 800);
    xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 10, 35, 800);
    XingzhiButtonEvent event = xingzhi_button_debounce_update(
        &debounce,
        XingzhiAction::CycleScreen,
        true,
        45,
        35,
        800
    );
    TEST_ASSERT_TRUE(event.active);
    TEST_ASSERT_EQUAL(XingzhiActionEvent::Press, event.event);

    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 844, 35, 800);
    TEST_ASSERT_FALSE(event.active);
    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, true, 845, 35, 800);
    TEST_ASSERT_TRUE(event.active);
    TEST_ASSERT_EQUAL(XingzhiActionEvent::LongPress, event.event);

    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, false, 900, 35, 800);
    TEST_ASSERT_FALSE(event.active);
    event = xingzhi_button_debounce_update(&debounce, XingzhiAction::CycleScreen, false, 935, 35, 800);
    TEST_ASSERT_FALSE(event.active);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cycle_click_enters_splash_then_advances_animation);
    RUN_TEST(test_splash_exit_returns_to_previous_non_splash_screen);
    RUN_TEST(test_cycle_long_press_exits_splash);
    RUN_TEST(test_cycle_after_splash_exit_continues_to_usage);
    RUN_TEST(test_exit_splash_outside_splash_reports_error);
    RUN_TEST(test_hid_space_press_release_balances_intent);
    RUN_TEST(test_shift_tab_click_records_action_without_changing_screen);
    RUN_TEST(test_release_without_press_is_reported);
    RUN_TEST(test_names_are_stable_for_debug_status);
    RUN_TEST(test_hid_action_helper_identifies_keyboard_actions);
    RUN_TEST(test_button_debounce_filters_bounce_until_stable);
    RUN_TEST(test_button_debounce_reports_long_press_once_and_suppresses_release);
    return UNITY_END();
}
