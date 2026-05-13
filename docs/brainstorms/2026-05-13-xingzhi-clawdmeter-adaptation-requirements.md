---
date: 2026-05-13
topic: xingzhi-clawdmeter-adaptation
---

# Xingzhi Clawdmeter Adaptation

## Summary

Adapt Clawdmeter into a first working version for the user's `xingzhi-cube-1.54tft-wifi` ESP32-S3 device. The first version will preserve the existing Xiaozhi firmware before flashing, then show a compact 240x240 usage meter that can be updated by a Windows serial test sender.

---

## Problem Frame

The user has a physical ESP32-S3 device connected over USB. Serial logs identify the current firmware as Xiaozhi and the board SKU as `xingzhi-cube-1.54tft-wifi`. Clawdmeter currently targets a different Waveshare AMOLED board and assumes a larger 480x480 screen, BLE data transport, touch, PMU button input, and IMU rotation.

The immediate need is not a full product port. The useful first step is a reversible hardware adaptation that proves the display and serial data path on the user's actual machine before restoring more advanced interaction.

---

## Actors

- A1. User: wants to safely try Clawdmeter on the plugged-in board without losing the ability to restore Xiaozhi.
- A2. Device: the ESP32-S3 `xingzhi-cube-1.54tft-wifi` board currently visible as Windows `COM7`.
- A3. Windows host: sends test usage payloads over serial.
- A4. Future planner/implementer: converts this scope into a concrete firmware and tooling plan.

---

## Key Flows

- F1. Backup before overwrite
  - **Trigger:** The user is ready to flash Clawdmeter onto the device.
  - **Actors:** A1, A2, A4
  - **Steps:** confirm the board is reachable, install any required flashing utility, read the existing Xiaozhi flash image, and verify the backup artifact exists with the expected size.
  - **Outcome:** the user has a restorable backup before any Clawdmeter firmware is written.
  - **Covered by:** R1, R2

- F2. Display and serial proof
  - **Trigger:** The adapted Clawdmeter firmware has been flashed.
  - **Actors:** A1, A2, A3
  - **Steps:** boot the device, show the compact usage meter, send a test usage payload from Windows, and verify the displayed values update.
  - **Outcome:** the board proves that the adapted display and serial receive path work on the user's machine.
  - **Covered by:** R3, R4, R5, R6

---

## Requirements

**Safety and reversibility**
- R1. The workflow must require backing up the existing Xiaozhi firmware before flashing Clawdmeter.
- R2. The backup step must produce a local artifact that can be checked for presence and expected flash-size completeness.

**Device experience**
- R3. The adapted firmware must target the `xingzhi-cube-1.54tft-wifi` hardware profile identified from Xiaozhi source and serial logs.
- R4. The first UI must be a single compact usage meter optimized for a 240x240 display.
- R5. The meter must show the core usage information: session percentage, weekly percentage, reset timing or status, and whether the last received payload was valid.
- R6. The firmware must accept test usage data over USB serial and update the displayed values without requiring BLE pairing.

**Host-side validation**
- R7. The first host-side tool must be a manually run Windows serial test sender for `COM7`, not a background daemon.
- R8. The sender must support enough test data to verify normal, high-usage, and invalid/error display states.

---

## Acceptance Examples

- AE1. **Covers R1, R2.** Given the board still runs Xiaozhi, when the flashing workflow starts, it first creates a restorable flash backup and reports where it was written before any overwrite happens.
- AE2. **Covers R4, R5.** Given the adapted firmware has booted, when no usage payload has arrived yet, the screen still shows a readable meter state rather than a blank or crashed UI.
- AE3. **Covers R6, R7.** Given the device is on `COM7`, when the Windows test sender sends a valid usage payload, the displayed session and weekly values update.
- AE4. **Covers R8.** Given the sender emits an invalid/error test case, when the firmware receives it, the display communicates that the latest payload is not a normal successful reading.

---

## Success Criteria

- The user can restore confidence before flashing because a Xiaozhi firmware backup exists.
- The adapted firmware boots on the plugged-in board and displays a readable 240x240 usage meter.
- A Windows-side manual test sender can update the meter over `COM7`.
- Planning does not need to decide whether v1 includes BLE, splash animation, real Claude polling, or a background service; those are explicitly deferred.

---

## Scope Boundaries

- Do not preserve the original Bluetooth pairing screen in this version.
- Do not preserve the Clawd splash animation in this version.
- Do not implement BLE GATT data transport.
- Do not implement BLE HID shortcuts for Space or Shift+Tab.
- Do not implement a Windows background service or scheduled task.
- Do not make WSL serial access the primary path.
- Do not implement real Claude usage polling in the Windows sender for this first validation step.

---

## Key Decisions

- Use a single-screen meter first: the 240x240 display has much less room than the original 480x480 target, and the user selected information density over splash or secondary screens.
- Use serial test data first: it directly validates the adapted firmware on `COM7` without introducing BLE or daemon complexity.
- Backup before flashing: the board currently runs Xiaozhi, and the user explicitly chose to preserve that firmware before overwriting it.

---

## Dependencies / Assumptions

- The board remains visible to Windows as `COM7` during implementation and validation.
- The Xiaozhi board profile is accurate for this device: 240x240 SPI ST7789 display, no touch requirement for v1, and board buttons available for later use.
- The current Xiaozhi flash size should be confirmed during planning or backup setup before reading the full image.
- Windows Python is available; required serial packages may need to be installed during implementation.
- Clawdmeter's real Claude usage JSON semantics can be reused for test payload meaning, even though the first sender will not poll Claude.

---

## Outstanding Questions

### Deferred to Planning

- [Affects R2][Technical] Determine the exact backup command and flash size from the device/tooling before reading the existing firmware image.
- [Affects R3, R4][Technical] Decide whether to adapt the existing Arduino/PlatformIO firmware directly or introduce a board abstraction for the Xingzhi hardware profile.
- [Affects R7, R8][Technical] Choose the lightest Windows serial test sender dependency set and installation path.
