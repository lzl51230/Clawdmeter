# Clawdmeter

A small ESP32 dashboard for keeping an eye on Claude Code or Codex usage.

This fork is adapted for the Xiaozhi/Xingzhi `xingzhi-cube-1.54tft-wifi`
device. It pairs with a Windows host over Bluetooth, shows usage on the 240×240
ST7789 screen, and plays pixel-art Clawd animations that get busier when your
usage rate climbs. GPIO40 is being adapted as a push-to-talk voice dictation
button, while GPIO39 still sends Shift+Tab over BLE HID.

|              Usage meter              |              Clawd animation screen              |
| :-----------------------------------: | :----------------------------------------------: |
| ![Usage meter](assets/demo.jpeg) | ![Clawd animation screen](assets/demo.gif) |

The Clawd animations come from [claudepix](https://claudepix.vercel.app), [@amaanbuilds](https://x.com/amaanbuilds)'s library of pixel-art Clawd sprites, check it out, it's lovely.

## Screens

The Xingzhi parity target boots into the usage screen. The UI button cycles
between Usage, Status, and Splash. While the splash is up, a short press cycles
Clawd animations and a long press returns to the previous non-splash screen.

|              Splash               |              Usage              |                 Status                  |
| :-------------------------------: | :-----------------------------: | :-------------------------------------: |
| ![Splash](screenshots/splash.png) | ![Usage](screenshots/usage.png) | ![Status](screenshots/bluetooth.png) |
|        Clawd animation loop       | Session and weekly utilization  |    Connection/status diagnostics        |

The firmware also auto-rotates every 20 s within the current usage-rate group,
so a long stretch on the splash isn't just one Clawd on loop.

## Hardware

- Xiaozhi/Xingzhi `xingzhi-cube-1.54tft-wifi` — ESP32-S3 device with a 1.54"
  240×240 ST7789 TFT screen.
- USB-C cable for backing up the stock Xiaozhi firmware, flashing Clawdmeter,
  serial debug, and power.
- No hardware modification is required. The adaptation is firmware plus
  host-side tooling.
- The current board profile uses the Xiaozhi ST7789 wiring: SDA GPIO10, SCL
  GPIO9, DC GPIO8, CS GPIO14, RES GPIO18, and BACKLIGHT GPIO13.
- The current parity firmware uses GPIO0 for UI cycle/splash control, GPIO40
  for voice dictation, GPIO39 for Shift+Tab, GPIO38 for charge-state sensing,
  and ADC2 channel 6 for battery voltage telemetry.
- The original [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16)
  target remains in the repository as the upstream/default hardware target.

## Xingzhi display bring-up

The fork includes an isolated first-stage target for `xingzhi-cube-1.54tft-wifi`.
It backs up the current Xiaozhi firmware, then builds a minimal 240×240 ST7789
test screen without BLE, LVGL UI, touch, PMU, IMU, splash, or usage-meter logic.
No hardware modification to the Xingzhi device is required; adaptation is done
through firmware flashing and host-side tools.
See [docs/xingzhi-display-bringup.md](docs/xingzhi-display-bringup.md).

```bash
python3 tools/backup_xingzhi_flash.py --port COM7 --dry-run
pio run -d firmware -e xingzhi_display_bringup
```

After the display test passes, build the manual USB serial meter target and send
test payloads from Windows. See [docs/xingzhi-serial-meter.md](docs/xingzhi-serial-meter.md).

```bash
pio run -d firmware -e xingzhi_serial_meter
python3 tools/send_test_payload.py normal --dry-run
```

The current Xingzhi parity target adds the non-touch Clawdmeter experience for
that board: USB debug screenshots, BLE GATT usage updates, BLE HID keys, three
screens, Clawd splash animations, battery telemetry, and IMU capability probing.
See [docs/xingzhi-parity.md](docs/xingzhi-parity.md) and
[docs/xingzhi-serial-debug.md](docs/xingzhi-serial-debug.md).

```powershell
pio run -d firmware -e xingzhi_parity
py -3 tools\xingzhi_debug.py status --port COM7
```

On Windows, the Xingzhi BLE helper defaults to Codex usage from WSL session logs
and falls back to paired-device direct connect when scanning cannot see an
already paired `Claude Controller`. Codex payloads include `src=codex`, so the
usage screen title changes to `Codex`; Claude API payloads include `src=claude`
and keep the `Claude` title.

```powershell
py -3 -m pip install bleak pyserial esptool
py -3 tools\windows_claude_usage_ble.py --dry-run
py -3 tools\windows_claude_usage_ble.py --require-ack
py -3 tools\windows_claude_usage_ble.py --usage-source claude --require-ack
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output usage.bmp
```

For daily Windows use, run the combined watcher so Codex usage and GPIO40 voice
dictation share one BLE connection. Store `SILICONFLOW_API_KEY=...` in a local
`.env` file for SiliconFlow `TeleAI/TeleSpeechASR` transcription; `.env` is
ignored by git. The combined watcher keeps usage polling in a separate asyncio
task while voice waits for uploads, so usage still refreshes while no recording
is active.

```powershell
tools\watch_xingzhi_combined.bat --address 94:A9:90:1B:6D:FD
```

The single-purpose watchers remain useful for debugging, but do not run
`watch_codex_wsl.bat` and `watch_xingzhi_voice_dictation.bat` at the same time;
the Xingzhi BLE peripheral is treated as a single host connection.

```powershell
tools\watch_codex_wsl.bat --address 94:A9:90:1B:6D:FD
tools\watch_xingzhi_voice_dictation.bat --address 94:A9:90:1B:6D:FD
py -3 tools\windows_xingzhi_voice_dictation.py --watch --receive-timeout 0 --address 94:A9:90:1B:6D:FD --output C:\Windows\Temp\voice.wav
py -3 tools\windows_xingzhi_voice_dictation.py --dump-only --output voice.wav
py -3 tools\windows_xingzhi_voice_dictation.py --input-wav voice.wav --asr-only
```

The current Xingzhi 1.54 WiFi board has no confirmed IMU configuration in the
local Xiaozhi board files, so `probe imu` reports `not_available` and automatic
rotation remains disabled for this target.

## Prerequisites

- Linux (tested on Ubuntu)
- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
- `curl`, `bluetoothctl`, `busctl` (BlueZ Bluetooth stack)
- Claude Code with an active subscription
- For Xingzhi on Windows: Python 3 with `bleak`, `pyserial`, and `esptool`; WSL
  Codex usage uses `~/.codex/sessions/**/*.jsonl` by default

## MacOS support

MacOS is fully supported, that is as soon as you prompt it and create a pull request for it!

I run Linux myself so it's harder for me to test this but anyone who wants MacOS support is welcome to contribute.

## Flash the firmware

```bash
cd firmware
pio run -t upload --upload-port /dev/ttyACM0
```

## Bluetooth pairing

After flashing, the device advertises as "Claude Controller". Pair it once:

```bash
# Scan for the device
bluetoothctl scan le

# When "Claude Controller" appears, pair and trust it
bluetoothctl pair F4:12:FA:C0:8F:E5    # use your device's MAC
bluetoothctl trust F4:12:FA:C0:8F:E5
```

On Xingzhi, the MAC address is shown in the serial `status` output and on the
Status screen; press the UI button to cycle to it.

## Install the daemon

The daemon polls your Claude usage every 60 seconds and sends it to the display over BLE.

```bash
./install.sh
systemctl --user start claude-usage-daemon
```

Check status: `systemctl --user status claude-usage-daemon`

View logs: `journalctl --user -u claude-usage-daemon -f`

For Xingzhi on Windows, use `tools\windows_claude_usage_ble.py` instead of the
Linux user daemon. In watch mode it polls every 60 seconds by default; adjust
with `--poll-interval`.

## How it works

1. The Linux daemon reads your Claude Code OAuth token from `~/.claude/.credentials.json`.
2. It makes a minimal API call to `api.anthropic.com/v1/messages` — one token of Haiku, basically free.
3. The Windows Xingzhi helper defaults to Codex usage by reading the latest WSL
   Codex `token_count` rate-limit event from `~/.codex/sessions/**/*.jsonl`.
4. Both paths write the same compact JSON payload to the ESP32 BLE GATT RX characteristic.
5. The firmware parses it and updates the usage dashboard. When `src` is `codex`,
   the Xingzhi usage header shows `Codex`; missing or unknown `src` defaults to
   `Claude` for backward compatibility.
6. The firmware also tracks the rate of change of session % over a 5-minute window and picks splash animations from the matching mood group.
7. GPIO40 records Xingzhi microphone audio for the voice helper; GPIO39 sends
   Shift+Tab as a BLE HID keyboard input to the paired host.

## Physical buttons

The Xingzhi parity target maps the three hardware buttons to UI, voice, and HID
actions:

| Button       | GPIO   | Function                                                    |
| ------------ | ------ | ----------------------------------------------------------- |
| **UI**       | GPIO 0 | Cycle Usage/Status/Splash; on splash, short press advances animation and long press exits splash |
| **Voice**    | GPIO40 | Long press records microphone audio for BLE voice dictation; short press is ignored |
| **Shift+Tab** | GPIO39 | Send Shift+Tab as BLE HID keyboard input                    |

Shift+Tab goes out as a standard BLE HID keyboard report, so it triggers in
whatever window has focus on the paired host.

## BLE protocol

The device advertises a custom GATT service alongside the standard HID keyboard service:

|                            | UUID                                   |
| -------------------------- | -------------------------------------- |
| **Data Service**           | `4c41555a-4465-7669-6365-000000000001` |
| RX Characteristic (write)  | `4c41555a-4465-7669-6365-000000000002` |
| TX Characteristic (notify) | `4c41555a-4465-7669-6365-000000000003` |
| REQ Characteristic (notify) | `4c41555a-4465-7669-6365-000000000004` |
| Voice Characteristic (notify) | `4c41555a-4465-7669-6365-000000000005` |
| Voice Control Characteristic (write) | `4c41555a-4465-7669-6365-000000000006` |
| **HID Service**            | `00001812-0000-1000-8000-00805f9b34fb` |

JSON payload format (written to RX):

```json
{ "s": 45, "sr": 120, "w": 28, "wr": 7200, "st": "allowed", "src": "codex", "ok": true }
```

Fields: `s` = session %, `sr` = session reset (minutes), `w` = weekly %, `wr` = weekly reset (minutes), `st` = status, `src` = optional usage source (`codex` or `claude`), `ok` = success flag. Older payloads without `src` are treated as Claude usage.

## Recompiling fonts

The `firmware/src/font_*.c` files are pre-compiled LVGL bitmap fonts. Sizes
are roughly 1.9× larger than the Panlee 165 PPI panel this project started on,
to match the 314 PPI of the 2.16" AMOLED.

```bash
npm install -g lv_font_conv
```

Generate each one (one at a time — `lv_font_conv` doesn't like loop-driven invocations) with `--no-compress` (required for LVGL 9):

```bash
# Tiempos Text (titles, 56px)
lv_font_conv --font assets/TiemposText-400-Regular.otf -r 0x20-0x7E \
  --size 56 --format lvgl --bpp 4 --no-compress \
  -o firmware/src/font_tiempos_56.c --lv-include "lvgl.h"

# Styrene B (large numbers 48, panel labels 28, small text 24, minimal 20)
for size in 48 28 24 20; do
  lv_font_conv --font assets/StyreneB-Regular.otf -r 0x20-0x7E \
    --size $size --format lvgl --bpp 4 --no-compress \
    -o firmware/src/font_styrene_${size}.c --lv-include "lvgl.h"
done

# DejaVu Sans Mono (32px, with spinner Unicode chars)
lv_font_conv --font assets/DejaVuSansMono.ttf \
  -r 0x20-0x7E,0xB7,0x2026,0x2722,0x2733,0x2736,0x273B,0x273D \
  --size 32 --format lvgl --bpp 4 --no-compress \
  -o firmware/src/font_mono_32.c --lv-include "lvgl.h"
```

**Important:** `lv_font_conv` v1.5.3 outputs LVGL 8 format. Each generated file must be patched for LVGL 9 compatibility:

1. Remove `#if LVGL_VERSION_MAJOR >= 8` guards around `font_dsc` and the font struct
2. Remove the `.cache` field from `font_dsc`
3. Add `.release_glyph = NULL`, `.kerning = 0`, `.static_bitmap = 0` to the font struct
4. Add `.fallback = NULL`, `.user_data = NULL` to the font struct

Without these patches, fonts compile but render as invisible.

## Converting Lucide icons

The UI uses a small set of [Lucide](https://lucide.dev) icons (bluetooth + battery states) converted to RGB565 / RGB565A8 C arrays for LVGL.

```bash
node tools/png_to_lvgl.js assets/icon_bluetooth_48.png icon_bluetooth_data ICON_BLUETOOTH_WIDTH ICON_BLUETOOTH_HEIGHT
```

Default tint is white (`0xFFFFFF`); Lucide PNGs ship as black-on-transparent and would render invisible against the dark UI without it. Pass `--no-tint` for pre-coloured artwork like the logo. Battery icons use RGB565A8 (alpha plane) so they blend cleanly over the splash; the rest are baked RGB565 over the panel colour. Paste the converter output into `firmware/src/icons.h`.

## Splash animations

The animations come from [claudepix.vercel.app](https://claudepix.vercel.app),
a library of Clawd sprites. `tools/scrape_claudepix.js` evaluates the
site's JavaScript in a Node VM to pull out frame data and palettes, then
`tools/convert_to_c.js` turns everything into RGB565 C arrays and writes
`firmware/src/splash_animations.h`.

To re-pull (e.g. when the source library updates):

```bash
node tools/scrape_claudepix.js
node tools/convert_to_c.js
pio run -d firmware -t upload
```

See `tools/README.md` for details.

## Credits

- Pixel-art Clawd animation by [@amaanbuilds](https://x.com/amaanbuilds), sourced from [claudepix.vercel.app](https://claudepix.vercel.app). Frame data and palettes scraped + converted by the tooling in `tools/`.
- Lucide icon set ([lucide.dev](https://lucide.dev), MIT) for bluetooth and battery UI glyphs.
- Anthropic brand fonts (Tiempos Text, Styrene B) — see licensing warning below.

## Licensing gray area warning

The software in this repository uses and adheres to the Anthropic brand guidelines and uses the same proprietary fonts that Anthropic has a licnese for but this software uses without permission as well as using assets from Anthropic such as the copyrighted Clawd mascot so even though the code in this repo is non-proprietary I will not license it myself under a copyleft license since this repo includes proprietary fonts and copyrighted assets. Please be aware of this if you fork or copy the code from this repo. **You have been warned!**
