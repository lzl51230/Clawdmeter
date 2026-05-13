# Repository Guidelines

## Project Structure & Module Organization

Clawdmeter is split into firmware, host tooling, and generated assets.

- `firmware/` contains the PlatformIO ESP32-S3 project. Core application code lives in `firmware/src/`, with hardware setup in `display_cfg.h` and `main.cpp`, UI in `ui.cpp`, BLE in `ble.cpp`, and generated splash data in `splash_animations.h`.
- `daemon/` contains the Linux user daemon and systemd unit that polls Claude usage and writes BLE GATT payloads.
- `tools/` contains Node scripts for scraping/converting splash animations and PNG assets, plus Python host utilities such as `backup_xingzhi_flash.py` and `send_test_payload.py`.
- `tools/tests/` contains Python unit tests for host utilities.
- `assets/` stores source fonts, icons, demos, and logos; `screenshots/` stores reference UI captures.
- `firmware/test/` contains PlatformIO native tests for portable parser and formatting helpers.

## Build, Test, and Development Commands

- `pio run -d firmware` builds the firmware.
- `pio run -d firmware -e xingzhi_display_bringup` builds the isolated Xingzhi ST7789 test target.
- `pio run -d firmware -e xingzhi_serial_meter` builds the isolated Xingzhi serial usage meter.
- `pio test -d firmware -e native` runs portable firmware parser/format tests.
- `pio run -d firmware -t upload --upload-port /dev/ttyACM0` flashes the board; adjust the port for your machine.
- `python3 -m unittest discover -s tools/tests` runs host-tool tests.
- `./flash.sh /dev/ttyACM0` wraps the upload command.
- `./install.sh` installs and enables the user-level daemon service.
- `systemctl --user start claude-usage-daemon` starts the daemon; use `journalctl --user -u claude-usage-daemon -f` for logs.
- `./screenshot.sh out.png /dev/ttyACM0` captures the LVGL framebuffer from a flashed device.
- `node tools/scrape_claudepix.js && node tools/convert_to_c.js` regenerates splash animation data.

## Coding Style & Naming Conventions

Follow the existing C++ style: 4-space indentation, braces on the same line for functions and conditionals, `snake_case` for helper functions, and uppercase macros for hardware constants. Keep generated files clearly separated and do not hand-edit `firmware/src/splash_animations.h`. Shell scripts should remain POSIX-friendly Bash with `set -e` when failures must abort.

## Testing Guidelines

At minimum, run the affected PlatformIO environment before submitting firmware changes. Run `pio test -d firmware -e native` when touching `usage_input` or meter formatting, and `python3 -m unittest discover -s tools/tests` when touching Python tools. For UI or display changes, flash the board and capture a screenshot or serial log; compare against `screenshots/` where applicable. For daemon changes, run the script through the systemd service and include relevant log lines.

## Agent-Specific Instructions

Keep `xingzhi_display_bringup` isolated with `build_src_filter`; it is a display-driver validation target only. Keep `xingzhi_serial_meter` isolated as the manual USB serial meter; do not add BLE, HID, touch, PMU, IMU, splash, daemon behavior, or real Claude polling to that environment until those features have their own plan.

## Commit & Pull Request Guidelines

Use short, imperative commit messages matching the project history, for example `Update README to reflect changes in demo and animations` or `Add Bluetooth reset screen`. Pull requests should summarize the behavior change, list verification commands, mention the tested hardware/port, and include screenshots or serial logs for firmware-visible changes.

## Security & Configuration Tips

Do not commit Claude credentials, BLE MAC caches, local systemd output, or machine-specific paths. The daemon reads `~/.claude/.credentials.json`; keep that file private and document any required local setup in the PR instead of encoding it in source.
