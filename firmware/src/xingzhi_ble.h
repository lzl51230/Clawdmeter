#pragma once

#include <stddef.h>
#include <stdint.h>

enum class XingzhiBleState {
    Init,
    Advertising,
    Connected,
    Disconnected,
    Error,
};

void xingzhi_ble_init();
void xingzhi_ble_tick();
XingzhiBleState xingzhi_ble_state();
const char *xingzhi_ble_state_name();
const char *xingzhi_ble_device_name();
const char *xingzhi_ble_mac();
const char *xingzhi_ble_last_error();
bool xingzhi_ble_has_payload();
const char *xingzhi_ble_take_payload();
bool xingzhi_ble_has_error();
const char *xingzhi_ble_take_error();
void xingzhi_ble_send_ack();
void xingzhi_ble_send_nack();
void xingzhi_ble_request_refresh();
bool xingzhi_ble_reset_pairing();
bool xingzhi_ble_voice_subscribed();
bool xingzhi_ble_voice_notify(const uint8_t *data, size_t len);
bool xingzhi_ble_has_voice_control();
const char *xingzhi_ble_take_voice_control();
bool xingzhi_ble_hid_available();
bool xingzhi_ble_set_battery_level(int level);
int xingzhi_ble_battery_level();
bool xingzhi_ble_keyboard_press(uint8_t key, uint8_t modifier);
bool xingzhi_ble_keyboard_release();
