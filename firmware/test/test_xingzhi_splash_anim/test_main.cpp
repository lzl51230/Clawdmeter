#include <unity.h>

#include <string.h>

#include "xingzhi_splash_anim.h"

void test_splash_anim_starts_on_valid_catalog_frame() {
    XingzhiSplashAnimState state = {};
    xingzhi_splash_anim_init(&state, 100);

    XingzhiSplashSnapshot snapshot = xingzhi_splash_anim_snapshot(&state);
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_GREATER_THAN(0, snapshot.animation_count);
    TEST_ASSERT_GREATER_THAN(0, snapshot.frame_count);
    TEST_ASSERT_EQUAL_UINT16(0, snapshot.animation_index);
    TEST_ASSERT_EQUAL_UINT16(0, snapshot.frame_index);
    TEST_ASSERT_NOT_EQUAL(0, strcmp("-", snapshot.name));

    XingzhiSplashFrame frame = {};
    TEST_ASSERT_TRUE(xingzhi_splash_anim_get_frame(&state, &frame));
    TEST_ASSERT_NOT_NULL(frame.cells);
    TEST_ASSERT_NOT_NULL(frame.palette);
    TEST_ASSERT_EQUAL_UINT8(XINGZHI_SPLASH_GRID_SIZE, frame.width);
    TEST_ASSERT_EQUAL_UINT8(XINGZHI_SPLASH_GRID_SIZE, frame.height);
}

void test_splash_anim_advances_after_frame_hold() {
    XingzhiSplashAnimState state = {};
    xingzhi_splash_anim_init(&state, 1000);

    XingzhiSplashSnapshot snapshot = xingzhi_splash_anim_snapshot(&state);
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_FALSE(xingzhi_splash_anim_tick(&state, 1000 + snapshot.hold_ms - 1));
    TEST_ASSERT_EQUAL_UINT16(0, xingzhi_splash_anim_snapshot(&state).frame_index);

    TEST_ASSERT_TRUE(xingzhi_splash_anim_tick(&state, 1000 + snapshot.hold_ms));
    TEST_ASSERT_EQUAL_UINT16(1, xingzhi_splash_anim_snapshot(&state).frame_index);
}

void test_splash_anim_rejects_invalid_animation_index_without_changing_state() {
    XingzhiSplashAnimState state = {};
    xingzhi_splash_anim_init(&state, 10);
    TEST_ASSERT_TRUE(xingzhi_splash_anim_next(&state, 20));

    XingzhiSplashSnapshot before = xingzhi_splash_anim_snapshot(&state);
    TEST_ASSERT_FALSE(xingzhi_splash_anim_select(&state, before.animation_count + 10, 30));

    XingzhiSplashSnapshot after = xingzhi_splash_anim_snapshot(&state);
    TEST_ASSERT_EQUAL_UINT16(before.animation_index, after.animation_index);
    TEST_ASSERT_EQUAL_UINT16(before.frame_index, after.frame_index);
}

void test_splash_anim_selects_usage_group_and_rotates_when_due() {
    XingzhiSplashAnimState state = {};
    xingzhi_splash_anim_init(&state, 0);

    TEST_ASSERT_TRUE(xingzhi_splash_anim_set_group(&state, 3, 100));
    XingzhiSplashSnapshot snapshot = xingzhi_splash_anim_snapshot(&state);
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_UINT8(3, snapshot.group);
    TEST_ASSERT_EQUAL_STRING("heavy", snapshot.group_name);
    const uint16_t first_animation = snapshot.animation_index;

    TEST_ASSERT_FALSE(xingzhi_splash_anim_rotate_if_due(&state, 100 + XINGZHI_SPLASH_ROTATE_INTERVAL_MS - 1));
    TEST_ASSERT_TRUE(xingzhi_splash_anim_rotate_if_due(&state, 100 + XINGZHI_SPLASH_ROTATE_INTERVAL_MS));

    snapshot = xingzhi_splash_anim_snapshot(&state);
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_UINT8(3, snapshot.group);
    TEST_ASSERT_EQUAL_STRING("heavy", snapshot.group_name);
    TEST_ASSERT_NOT_EQUAL(first_animation, snapshot.animation_index);
}

void test_splash_anim_handles_missing_state_or_frame() {
    XingzhiSplashFrame frame = {};

    TEST_ASSERT_FALSE(xingzhi_splash_anim_tick(nullptr, 0));
    TEST_ASSERT_FALSE(xingzhi_splash_anim_select(nullptr, 0, 0));
    TEST_ASSERT_FALSE(xingzhi_splash_anim_next(nullptr, 0));
    TEST_ASSERT_FALSE(xingzhi_splash_anim_get_frame(nullptr, &frame));
    TEST_ASSERT_FALSE(xingzhi_splash_anim_snapshot(nullptr).valid);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_splash_anim_starts_on_valid_catalog_frame);
    RUN_TEST(test_splash_anim_advances_after_frame_hold);
    RUN_TEST(test_splash_anim_rejects_invalid_animation_index_without_changing_state);
    RUN_TEST(test_splash_anim_selects_usage_group_and_rotates_when_due);
    RUN_TEST(test_splash_anim_handles_missing_state_or_frame);
    return UNITY_END();
}
