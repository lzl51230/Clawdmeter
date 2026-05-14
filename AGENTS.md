# Repository Guidelines

## Project Structure & Module Organization

本仓库由固件、主机工具和资源文件组成。

- `firmware/` 是 PlatformIO ESP32-S3 工程；通用源码在 `firmware/src/`，原版 Waveshare 入口是 `main.cpp`，UI/BLE/PMU 分别在 `ui.cpp`、`ble.cpp`、`power.cpp`。
- Xingzhi 相关源码使用 `xingzhi_*.cpp` 命名；`xingzhi_display_bringup` 只验证 ST7789，`xingzhi_serial_meter` 是最小 USB 串口 fallback，`xingzhi_parity` 承载串口调试、BLE GATT、HID、三屏 UI 和电源遥测。
- `daemon/` 存放 Linux 用户级 BLE daemon；Windows Xingzhi 主机脚本在 `tools/windows_claude_usage_ble.py`。
- `tools/` 存放备份、测试 payload、截图调试和资源转换工具；`tools/tests/` 是 Python 单元测试。
- `firmware/test/` 是 PlatformIO native 测试；`assets/`、`screenshots/` 保存字体、图标、演示和参考截图。

## Build, Test, and Development Commands

- `pio run -d firmware` 构建默认 Waveshare 目标。
- `pio run -d firmware -e xingzhi_parity` 构建 Xingzhi parity 固件。
- `pio run -d firmware -e xingzhi_serial_meter` 构建最小 USB 串口仪表。
- `pio run -d firmware -e xingzhi_display_bringup` 构建 ST7789 显示验证目标。
- `pio test -d firmware -e native` 运行固件 native 测试。
- `python3 -m unittest discover -s tools/tests` 运行主机工具测试。
- Windows 刷写示例：`py -3 -m esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 firmware\.pio\build\xingzhi_parity\firmware.factory.bin`。
- 串口调试示例：`py -3 tools\xingzhi_debug.py status|screenshot|button --port COM7`。
- BLE 测试 payload：`py -3 tools\windows_claude_usage_ble.py --test-preset high --require-ack`。

## Coding Style & Naming Conventions

沿用现有 C++ 风格：4 空格缩进，函数和条件的大括号同行，辅助函数用 `snake_case`，硬件常量用大写宏。Xingzhi 专用实现保持 `xingzhi_` 前缀，不把板级假设写入通用模块。不要手改 `firmware/src/splash_animations.h` 这类生成文件。

## Testing Guidelines

固件改动至少构建受影响的 PlatformIO 环境。修改 `usage_input`、`meter_format`、Xingzhi action/debug/power 时运行 `pio test -d firmware -e native`；修改 Python 工具时运行 `python3 -m unittest discover -s tools/tests`。显示或交互改动必须刷写实体设备，并记录串口 `status`、截图或模拟按键结果。

## Agent-Specific Instructions

保持三个 Xingzhi 目标分工清晰：`xingzhi_display_bringup` 只做显示驱动验证；`xingzhi_serial_meter` 只保留手动 USB JSON fallback；`xingzhi_parity` 才添加串口截图、BLE GATT、HID、三屏 UI 和电源状态。每个实现段完成后，先用串口闸门验证再继续：`status`、`screenshot`、模拟 `button`、BLE 写入确认、HID 可用性或 fallback payload。Parity 工作不要改默认 Waveshare 目标，除非是共享解析/格式化代码且有回归测试。

## Commit & Pull Request Guidelines

提交信息使用简短祈使句或 Conventional Commit，例如 `feat(xingzhi): add serial debug target`。PR 需要说明行为变化、验证命令、测试硬件/端口；固件可见变化附串口日志或截图结论。

## Security & Configuration Tips

不要提交 Claude 凭据、BLE MAC 缓存、本机 systemd 输出、截图原始 dump 或机器专用路径。daemon 读取 `~/.claude/.credentials.json`，Windows BLE 工具也只应读取本机私有凭据；必要的本地设置写进文档或 PR 描述，不写入源码。

# Global Instructions
Agent 新增或修改的说明性文档优先使用简体中文；代码标识、命令、协议字段、frontmatter、第三方引用和已有英文内容可保留必要英文。
本仓库在 Codex 中默认使用 `Serena` 作为首选代码语义检索与编辑工具。
