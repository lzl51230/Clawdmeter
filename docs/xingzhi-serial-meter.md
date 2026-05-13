# Xingzhi 串口用量仪表

本阶段在已验证的 `xingzhi-cube-1.54tft-wifi` ST7789 显示配置上运行 240x240 单屏用量仪表。数据来源是 USB 串口 newline-delimited JSON；仍不包含 BLE、HID、daemon、真实 Claude 轮询、触摸、PMU、IMU 或 splash。

后续 parity 工作使用独立的 `xingzhi_parity` 环境，并在 `docs/xingzhi-serial-debug.md` 中记录截图回读、状态查询、BLE 写入和模拟按键。`xingzhi_serial_meter` 继续作为最小串口 fallback，不承载 BLE/HID/UI 扩展。

## 前置条件

- 阶段 1 显示 bring-up 已验证通过。
- Xiaozhi 原固件备份已存在，示例：`backups/xiaozhi-COM7-adaptive-0x1000000.bin`。
- Windows 可通过 `COM7` 或指定端口访问设备。

## 构建

```powershell
pio run -d firmware -e xingzhi_serial_meter
```

如果只在 WSL 中有 PlatformIO，可用：

```bash
/tmp/clawdmeter-platformio-venv/bin/python -m platformio run -d firmware -e xingzhi_serial_meter
```

## 刷写

Windows 有 `pio` 时：

```powershell
pio run -d firmware -e xingzhi_serial_meter -t upload --upload-port COM7
```

Windows 没有 `pio` 时，使用 WSL 生成的 factory image：

```powershell
py -3 -m esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 firmware\.pio\build\xingzhi_serial_meter\firmware.factory.bin
```

## 串口 Payload

固件接收 newline-delimited JSON，字段沿用 `UsageData` 语义：

```json
{"s":42,"sr":37,"w":28,"wr":720,"st":"allowed","ok":true,"valid":true}
```

字段：`s` session 百分比，`sr` session reset 分钟，`w` weekly 百分比，`wr` weekly reset 分钟，`st` 状态，`ok` 表示 payload 成功。缺失 `sr` 或 `wr` 时显示 `Reset --`。

## 发送测试 Preset

```powershell
py -3 tools\send_test_payload.py normal --port COM7
py -3 tools\send_test_payload.py high --port COM7
py -3 tools\send_test_payload.py invalid --port COM7
```

`normal` 应显示绿色或普通状态，`high` 应进入高用量红色状态，`invalid` 应显示 payload 异常但保留上一条有效读数。
发送器打开端口后默认等待 1.2 秒再写入，避免 Windows 打开 USB CDC 串口时触发设备复位导致 payload 丢失；如确认端口不会复位，可用 `--settle-delay 0` 覆盖。

先不打开串口、只检查 JSON：

```powershell
py -3 tools\send_test_payload.py normal --dry-run
```

## 预期屏幕状态

- No data：启动后显示 `NO DATA` 和等待串口提示。
- Normal：显示 session、weekly 百分比、reset 时间和 `Payload OK`。
- High usage：任一百分比达到 80% 以上时显示 `HIGH` 红色状态。
- Invalid/error：malformed JSON 或 `ok:false` payload 显示 `ERROR` / `Payload invalid`。

## 验证命令

```bash
python3 -m unittest discover -s tools/tests
/tmp/clawdmeter-platformio-venv/bin/python -m platformio test -d firmware -e native
/tmp/clawdmeter-platformio-venv/bin/python -m platformio run -d firmware -e xingzhi_serial_meter
/tmp/clawdmeter-platformio-venv/bin/python -m platformio run -d firmware -e xingzhi_display_bringup
```

实体串口验证时，固件日志应包含：

```text
Xingzhi serial meter
Serial meter ready.
Usage update: session=42.0 weekly=28.0 status=allowed
Usage update: session=88.0 weekly=82.0 status=limited
Usage error payload: status=error
```

2026-05-13 实体验证通过：连续发送 `normal`、`high`、`invalid` 后，屏幕停在 `ERROR` / `Payload invalid`，并保留 high payload 的 `88%` session 和 `82%` weekly 读数。

若显示方向、颜色或背光异常，先退回 `xingzhi_display_bringup` 环境排查，不要在 meter UI 中硬补。
