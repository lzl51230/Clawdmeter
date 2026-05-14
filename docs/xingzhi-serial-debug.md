# Xingzhi 串口调试

`xingzhi_parity` 固件在 USB CDC 上同时保留两类输入：

- JSON 用量 payload：以 `{` 开头，沿用 `tools/send_test_payload.py` 的 newline-delimited JSON。
- 调试命令：以 `XDBG` 开头，用于 Codex 查询状态和回读屏幕。

## 构建与刷写

```powershell
pio run -d firmware -e xingzhi_parity
pio run -d firmware -e xingzhi_parity -t upload --upload-port COM7
```

如果在 Windows 侧只使用 `esptool`：

```powershell
py -3 -m esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 firmware\.pio\build\xingzhi_parity\firmware.factory.bin
```

## 状态查询

```powershell
py -3 tools\xingzhi_debug.py status --port COM7
```

固件返回一行 `XDBG STATUS`，包含 `target=xingzhi_parity`、`screen=usage`、`width=240`、`height=240`、`payload`、`source`、`ble`、`hid`、`power`、`uptime_ms` 和 `framebuffer`。
BLE 阶段后，同一状态行还会包含 `ble=advertising|connected|disconnected|error`、`ble_name` 和 `ble_mac`。状态行字段按空格分隔，因此 `ble_name` 会将空格转为下划线，例如实际广播名 `Claude Controller` 会显示为 `Claude_Controller`。
电源遥测阶段后，状态行还会包含 `power=valid|sampling|unavailable|error`、`battery`、`charging`、`adc=<avg>/<raw>` 和 `samples`。只有 `power=valid` 时，status 屏才会显示具体电量。
Splash 动画阶段后，状态行还会包含 `splash`、`splash_group`、`splash_category` 和 `splash_frame`，便于截图前后确认动画和用量组变化。

## 截图回读

```powershell
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output xingzhi.bmp --raw-output xingzhi.rgb565
```

截图协议返回：

```text
XDBG SCREENSHOT_START width=240 height=240 format=RGB565LE bytes=115200
<115200 raw bytes>
XDBG SCREENSHOT_END
```

主机工具会忽略 start marker 前的启动日志，并把 RGB565LE framebuffer 转为 `.bmp` 或 `.ppm`。
默认截图读取超时为 12 秒；如果串口环境不稳定，可继续用 `--timeout 20` 放宽。

## 模拟按键

三颗按键可先通过串口模拟，后续实体 GPIO 和 HID 会复用同一动作分发器：

```powershell
py -3 tools\xingzhi_debug.py button cycle --event click --port COM7
py -3 tools\xingzhi_debug.py button exit --event click --port COM7
py -3 tools\xingzhi_debug.py button space --event press --port COM7
py -3 tools\xingzhi_debug.py button space --event release --port COM7
py -3 tools\xingzhi_debug.py button shift_tab --event click --port COM7
```

返回 `XDBG ACTION` 字段，包含 `ok`、`action`、`event`、`screen`、`count` 和 `message`。随后运行 `status` 可确认 `screen`、`action`、`event` 与 `action_count`。

连续执行 `button cycle --event click` 会在 `usage`、`status`、`splash` 三屏之间循环。每次切屏后都应先运行 `status` 并捕获截图，再继续下一段实现。
在 splash 屏内执行 `button cycle` 会切换下一段 Clawd 动画，不切出 splash；执行 `button exit` 或实体 cycle 长按会返回上一非 splash 屏。

HID 阶段后，`status` 还会包含 `hid=available|unavailable`。未连接 BLE HID 时，`space` 和 `shift_tab` 会记录动作但返回 `message=hid_unavailable`；这表示动作路径正常，键盘 report 因主机未连接而未发出。

## 用量 Fallback 验证

USB 串口 payload 是 BLE 日常路径之外的调试/救援 fallback。验证 BLE 或 HID 后，仍应确认它可更新用量：

```powershell
py -3 tools\xingzhi_debug.py payload "{\"s\":88,\"w\":82,\"st\":\"limited\",\"ok\":true}" --port COM7
py -3 tools\send_test_payload.py high --port COM7
py -3 tools\xingzhi_debug.py status --port COM7
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output high.bmp
```

通过闸门：状态里 `source=serial`、`payload=valid`，截图显示 high payload 的 session/weekly 读数。

## BLE 写入验证

BLE 固件刷写后，Windows 侧可用固定 payload 验证 GATT 写入：

```powershell
py -3 -m pip install bleak
py -3 tools\windows_claude_usage_ble.py --test-preset high --require-ack
py -3 tools\xingzhi_debug.py status --port COM7
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output ble-high.bmp
```

通过闸门：BLE 工具输出 `BLE write succeeded` 和 `Device acknowledged payload`；串口状态显示 `source=ble`、`payload=valid`、`detail=limited`，截图读数为 88%/82%。

BLE 配对状态异常或 Windows GATT 缓存不一致时，可先通过串口触发恢复：

```powershell
py -3 tools\xingzhi_debug.py ble reset --port COM7
py -3 tools\xingzhi_debug.py status --port COM7
```

`watch` 长期运行模式用于日常观察；扫描、连接、通知、写入、ack/nack 或 polling 失败会按 `--retry-delay` 重试：

```powershell
py -3 -u tools\windows_claude_usage_ble.py --watch --require-ack --poll-interval 60 --retry-delay 5
```

## 硬件能力探测

可选硬件能力先通过 `probe` 记录证据，不把未知硬件能力变成阻塞项：

```powershell
py -3 tools\xingzhi_debug.py probe imu --port COM7
```

当前 Xingzhi 1.54 WiFi 返回 `status=not_available`、`method=xiaozhi_board_config`、`detail=no_i2c_or_imu_config`、`checked=qmi8658_0x6b`。这表示未发现可用 IMU 配置，自动旋转暂不适用于当前硬件。
