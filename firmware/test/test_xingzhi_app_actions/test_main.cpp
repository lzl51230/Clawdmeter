#include <unity.h>

#include "xingzhi_app_actions.h"
#include "xingzhi_buttons.h"

void test_cycle_click_advances_and_wraps_screens() {
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

    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
    TEST_ASSERT_EQUAL(XingzhiScreen::Splash, state.current_screen);

    xingzhi_actions_dispatch(&state, XingzhiAction::CycleScreen, XingzhiActionEvent::Click);
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
    TEST_ASSERT_EQUAL_STRING("space", xingzhi_action_name(XingzhiAction::HidSpace));
    TEST_ASSERT_EQUAL_STRING("release", xingzhi_event_name(XingzhiActionEvent::Release));
}

void test_hid_action_helper_identifies_keyboard_actions() {
    TEST_ASSERT_TRUE(xingzhi_action_is_hid(XingzhiAction::HidSpace));
    TEST_ASSERT_TRUE(xingzhi_action_is_hid(XingzhiAction::HidShiftTab));
    TEST_ASSERT_FALSE(xingzhi_action_is_hid(XingzhiAction::CycleScreen));
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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cycle_click_advances_and_wraps_screens);
    RUN_TEST(test_hid_space_press_release_balances_intent);
    RUN_TEST(test_shift_tab_click_records_action_without_changing_screen);
    RUN_TEST(test_release_without_press_is_reported);
    RUN_TEST(test_names_are_stable_for_debug_status);
    RUN_TEST(test_hid_action_helper_identifies_keyboard_actions);
    RUN_TEST(test_button_debounce_filters_bounce_until_stable);
    return UNITY_END();
}
