#pragma once

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
