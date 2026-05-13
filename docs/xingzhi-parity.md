# Xingzhi Parity 固件

`xingzhi_parity` 是 Xingzhi 版本补齐原版 Clawdmeter 核心体验的独立目标。它不替代 `xingzhi_display_bringup` 或 `xingzhi_serial_meter`：前者继续用于显示驱动排障，后者继续作为最小 USB 串口用量仪表。

## 当前能力

- USB 串口 JSON fallback：兼容 `tools/send_test_payload.py`。
- 串口调试：`tools/xingzhi_debug.py status|screenshot|button`。
- 240x240 framebuffer 截图：RGB565LE framed binary，可转 `.bmp` 或 `.ppm`。
- 三屏 UI：usage、status、splash，通过 `button cycle --event click` 切换。
- 动作分发器：`cycle`、`space`、`shift_tab` 已可串口模拟，实体按键和 HID 后续复用。
- BLE GATT 用量通路：广播名 `Claude Controller`，沿用原版 service/RX/TX/REQ UUID，BLE payload 与串口 fallback 走同一解析器。
- Windows BLE 调试 CLI：`tools/windows_claude_usage_ble.py` 可发送真实 Claude 用量或固定测试 payload。

## 构建与刷写

```powershell
pio run -d firmware -e xingzhi_parity
py -3 -m esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 firmware\.pio\build\xingzhi_parity\firmware.factory.bin
```

## 分段验证闸门

每个后续实现段开始前，先确认串口调试闭环：

```powershell
py -3 tools\xingzhi_debug.py status --port COM7
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output status.bmp
py -3 tools\xingzhi_debug.py button cycle --event click --port COM7
```

U3 通过标准：连续执行两次 `button cycle` 后，`status` 分别报告 `screen=status` 和 `screen=splash`；每个屏幕都能截图回读为 240x240 图像。

U4 通过标准：刷写后 `status` 报告 `ble=advertising`、空格安全的 `ble_name=Claude_Controller` 和 `ble_mac`；实际 BLE 广播名仍为 `Claude Controller`。status 屏截图显示 BLE 状态；串口 fallback 发送 `high` 后仍能更新 usage。

U5 通过标准：Windows 侧安装 Bleak 后执行 `py -3 tools\windows_claude_usage_ble.py --test-preset high --require-ack`，工具报告 BLE write 和 TX ack 成功；随后串口 `status` 显示 `source=ble`、`payload=valid`、`detail=limited`，截图显示 high payload 的 88%/82% 读数。

## Windows BLE 用量发送

```powershell
py -3 -m pip install bleak
py -3 tools\windows_claude_usage_ble.py --test-preset high --require-ack
py -3 tools\windows_claude_usage_ble.py --dry-run
py -3 tools\windows_claude_usage_ble.py --watch
```

`--test-preset` 不访问 Claude API，适合先验证 BLE。真实用量模式读取 `%USERPROFILE%\.claude\.credentials.json` 内的 `accessToken`，调用 Claude Messages API 并把响应头压缩为 `{s,sr,w,wr,st,ok}` 后写入 RX characteristic。

## UI 屏幕

- `usage`：保留已验证的 session、weekly、reset、payload 状态和 high/error 颜色。
- `status`：显示 BLE 状态、最近数据来源、payload 细节、最后动作和恢复提示。BLE 尚未实现时显示 pending/disabled。
- `splash`：提供 Clawdmeter/Xingzhi 识别和占位图形，不包含完整动画。

## 注意事项

不要把 BLE、HID 或 splash 动画加入 `xingzhi_serial_meter`。新增能力应先进入 `xingzhi_parity`，并在串口状态、截图或模拟按键闸门通过后再进入下一段。
