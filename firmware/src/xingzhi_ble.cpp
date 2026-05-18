#include "xingzhi_ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <string.h>

namespace {

constexpr const char *DEVICE_NAME = "Claude Controller";
constexpr const char *SERVICE_UUID = "4c41555a-4465-7669-6365-000000000001";
constexpr const char *RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000002";
constexpr const char *TX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000003";
constexpr const char *REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004";
constexpr const char *VOICE_CHAR_UUID = "4c41555a-4465-7669-6365-000000000005";
constexpr const char *VOICE_CTRL_CHAR_UUID = "4c41555a-4465-7669-6365-000000000006";
constexpr size_t BLE_BUF_SIZE = 512;

const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0,        // End Collection
};

NimBLEServer *server = nullptr;
NimBLEHIDDevice *hid_dev = nullptr;
NimBLECharacteristic *input_kbd = nullptr;
NimBLECharacteristic *tx_char = nullptr;
NimBLECharacteristic *req_char = nullptr;
NimBLECharacteristic *voice_char = nullptr;
XingzhiBleState state = XingzhiBleState::Init;
bool need_advertise = false;
char rx_buf[BLE_BUF_SIZE] = {};
char last_error[72] = {};
char pending_error[72] = {};
char voice_control_buf[72] = {};
char mac_str[18] = {};
volatile bool data_ready = false;
volatile bool error_ready = false;
volatile bool has_received_data = false;
volatile bool voice_subscribed = false;
volatile bool voice_control_ready = false;
int hid_battery_level = 100;

void copy_error(const char *message) {
    snprintf(last_error, sizeof(last_error), "%s", message ? message : "");
    snprintf(pending_error, sizeof(pending_error), "%s", message ? message : "");
    error_ready = pending_error[0] != '\0';
}

void start_advertising() {
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->reset();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setAppearance(HID_KEYBOARD);
    adv->enableScanResponse(true);
    adv->setName(DEVICE_NAME);
    const bool ok = adv->start();
    if (ok) {
        state = XingzhiBleState::Advertising;
        snprintf(last_error, sizeof(last_error), "");
    } else {
        state = XingzhiBleState::Error;
        copy_error("advertising_start_failed");
    }
    Serial.printf("Xingzhi BLE: advertising start=%s\n", ok ? "OK" : "FAILED");
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
        state = XingzhiBleState::Connected;
        snprintf(last_error, sizeof(last_error), "");
        Serial.printf("Xingzhi BLE: connected from %s\n", info.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override {
        state = XingzhiBleState::Disconnected;
        need_advertise = true;
        snprintf(last_error, sizeof(last_error), "disconnect_%d", reason);
        Serial.printf("Xingzhi BLE: disconnected reason=%d\n", reason);
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
        std::string value = chr->getValue();
        const size_t length = value.length();
        if (length >= BLE_BUF_SIZE) {
            copy_error("payload_too_large");
            Serial.printf("Xingzhi BLE: rejected oversized payload len=%lu\n", static_cast<unsigned long>(length));
            return;
        }

        memcpy(rx_buf, value.c_str(), length);
        rx_buf[length] = '\0';
        data_ready = true;
        has_received_data = true;
    }
};

class ReqCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &info, uint16_t subValue) override {
        Serial.printf(
            "Xingzhi BLE: req subscribe=%u has_data=%d\n",
            subValue,
            has_received_data ? 1 : 0
        );
        if (subValue != 0 && !has_received_data) {
            xingzhi_ble_request_refresh();
        }
    }
};

class VoiceCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &info, uint16_t subValue) override {
        voice_subscribed = subValue != 0;
        Serial.printf("Xingzhi BLE: voice subscribe=%u\n", subValue);
    }
};

class VoiceControlCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
        std::string value = chr->getValue();
        const size_t length = value.length();
        const size_t copy_len = length < sizeof(voice_control_buf) - 1 ? length : sizeof(voice_control_buf) - 1;
        memcpy(voice_control_buf, value.c_str(), copy_len);
        voice_control_buf[copy_len] = '\0';
        voice_control_ready = true;
    }
};

}  // namespace

void xingzhi_ble_init() {
    state = XingzhiBleState::Init;
    snprintf(last_error, sizeof(last_error), "");
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setSecurityAuth(true, false, true);

    NimBLEAddress addr = NimBLEDevice::getAddress();
    snprintf(mac_str, sizeof(mac_str), "%s", addr.toString().c_str());
    for (int index = 0; mac_str[index]; ++index) {
        if (mac_str[index] >= 'a' && mac_str[index] <= 'f') {
            mac_str[index] -= 32;
        }
    }

    server = NimBLEDevice::createServer();
    static ServerCallbacks server_callbacks;
    server->setCallbacks(&server_callbacks);

    hid_dev = new NimBLEHIDDevice(server);
    hid_dev->setReportMap(const_cast<uint8_t *>(HID_REPORT_MAP), sizeof(HID_REPORT_MAP));
    hid_dev->setManufacturer("Anthropic");
    hid_dev->setPnp(0x02, 0x05AC, 0x820A, 0x0210);
    hid_dev->setHidInfo(0x00, 0x02);
    hid_battery_level = 100;
    hid_dev->setBatteryLevel(hid_battery_level);
    input_kbd = hid_dev->getInputReport(1);

    NimBLEService *service = server->createService(SERVICE_UUID);
    NimBLECharacteristic *rx_char = service->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    static RxCallbacks rx_callbacks;
    rx_char->setCallbacks(&rx_callbacks);

    tx_char = service->createCharacteristic(
        TX_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    req_char = service->createCharacteristic(
        REQ_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );
    static ReqCallbacks req_callbacks;
    req_char->setCallbacks(&req_callbacks);

    voice_char = service->createCharacteristic(
        VOICE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    static VoiceCallbacks voice_callbacks;
    voice_char->setCallbacks(&voice_callbacks);

    NimBLECharacteristic *voice_control_char = service->createCharacteristic(
        VOICE_CTRL_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    static VoiceControlCallbacks voice_control_callbacks;
    voice_control_char->setCallbacks(&voice_control_callbacks);

    server->start();
    start_advertising();
    Serial.printf("Xingzhi BLE: init complete, MAC=%s\n", mac_str);
}

void xingzhi_ble_tick() {
    if (need_advertise) {
        need_advertise = false;
        start_advertising();
    }
}

XingzhiBleState xingzhi_ble_state() {
    return state;
}

const char *xingzhi_ble_state_name() {
    switch (state) {
    case XingzhiBleState::Advertising:
        return "advertising";
    case XingzhiBleState::Connected:
        return "connected";
    case XingzhiBleState::Disconnected:
        return "disconnected";
    case XingzhiBleState::Error:
        return "error";
    case XingzhiBleState::Init:
    default:
        return "init";
    }
}

const char *xingzhi_ble_device_name() {
    return DEVICE_NAME;
}

const char *xingzhi_ble_mac() {
    return mac_str[0] ? mac_str : "-";
}

const char *xingzhi_ble_last_error() {
    return last_error;
}

bool xingzhi_ble_has_payload() {
    return data_ready;
}

const char *xingzhi_ble_take_payload() {
    data_ready = false;
    return rx_buf;
}

bool xingzhi_ble_has_error() {
    return error_ready;
}

const char *xingzhi_ble_take_error() {
    error_ready = false;
    return pending_error;
}

void xingzhi_ble_send_ack() {
    if (state == XingzhiBleState::Connected && tx_char) {
        tx_char->setValue("{\"ack\":true}");
        tx_char->notify();
    }
}

void xingzhi_ble_send_nack() {
    if (state == XingzhiBleState::Connected && tx_char) {
        tx_char->setValue("{\"err\":true}");
        tx_char->notify();
    }
}

void xingzhi_ble_request_refresh() {
    if (state == XingzhiBleState::Connected && req_char) {
        uint8_t value = 0x01;
        req_char->setValue(&value, 1);
        req_char->notify();
        Serial.println("Xingzhi BLE: refresh requested");
    }
}

bool xingzhi_ble_reset_pairing() {
    if (!server) {
        copy_error("ble_not_initialized");
        return false;
    }

    NimBLEDevice::deleteAllBonds();
    has_received_data = false;
    voice_subscribed = false;
    voice_control_ready = false;
    snprintf(last_error, sizeof(last_error), "");
    Serial.println("Xingzhi BLE: bonds cleared");

    if (server->getConnectedCount() > 0) {
        NimBLEConnInfo peer = server->getPeerInfo(0);
        server->disconnect(peer.getConnHandle());
    }

    need_advertise = true;
    return true;
}

bool xingzhi_ble_voice_subscribed() {
    return state == XingzhiBleState::Connected && voice_char != nullptr && voice_subscribed;
}

bool xingzhi_ble_voice_notify(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        copy_error("voice_empty_notify");
        return false;
    }
    if (!xingzhi_ble_voice_subscribed()) {
        copy_error("voice_not_subscribed");
        return false;
    }
    voice_char->setValue(const_cast<uint8_t *>(data), len);
    voice_char->notify();
    return true;
}

bool xingzhi_ble_has_voice_control() {
    return voice_control_ready;
}

const char *xingzhi_ble_take_voice_control() {
    voice_control_ready = false;
    return voice_control_buf;
}

bool xingzhi_ble_hid_available() {
    return state == XingzhiBleState::Connected && input_kbd != nullptr;
}

bool xingzhi_ble_set_battery_level(int level) {
    if (!hid_dev || level < 0 || level > 100) {
        return false;
    }
    hid_battery_level = level;
    hid_dev->setBatteryLevel(static_cast<uint8_t>(level));
    return true;
}

int xingzhi_ble_battery_level() {
    return hid_battery_level;
}

bool xingzhi_ble_keyboard_press(uint8_t key, uint8_t modifier) {
    if (!xingzhi_ble_hid_available()) {
        copy_error("hid_unavailable");
        return false;
    }
    uint8_t report[8] = {modifier, 0, key, 0, 0, 0, 0, 0};
    input_kbd->setValue(report, sizeof(report));
    input_kbd->notify();
    return true;
}

bool xingzhi_ble_keyboard_release() {
    if (!xingzhi_ble_hid_available()) {
        copy_error("hid_unavailable");
        return false;
    }
    uint8_t report[8] = {0};
    input_kbd->setValue(report, sizeof(report));
    input_kbd->notify();
    return true;
}
