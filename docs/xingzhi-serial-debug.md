# Xingzhi 串口调试

## 本机 Codex 串口同步（Windows / macOS）

运行仓库根目录的 `codexWatch.bat`，默认通过 `COM3`、115200 波特率同步
Windows 本机 Codex，每 60 秒刷新一次。使用 `codexWatch.bat --port COM7`
覆盖端口，或用 `--codex-home` 指定数据目录；此入口不会自动查询 WSL。

macOS 安装 Python 3.11+ 和 `pyserial` 后，双击根目录的 `codexWatch.command`。
它默认自动匹配 `/dev/cu.*` 中 VID/PID 为 `303A:1001` 的设备，多个候选时列出端口并等待，
可用 `./codexWatch.command --port /dev/cu.usbmodemXXXX` 指定。默认同样每 60 秒同步、
每 5 秒重试，按 Ctrl+C 停止；首次绑定后支持设备端口变化时重新连接。
读取 `CODEX_HOME` 或 `~/.codex`，不需要 WSL。依赖安装和参数示例见
[README 的 macOS 支持](../README.md#macos-支持)。

macOS 串口层以一次 ioctl 同时更新 DTR/RTS，避免 pyserial 逐路更新产生瞬间复位组合，
使每次同步释放串口后、再次查询状态或截图时仍保留设备画面。
控制线行为参考 [Espressif 的原子 DTR/RTS 设置说明](https://github.com/espressif/esp-pylib/blob/master/esp_pylib/serial_reset.py)。

2026-10-23 macOS Apple Silicon 验证：115 项 Python 测试通过；通过 macOS 文件关联打开
`codexWatch.command` 后，终端成功自动识别设备并同步本机 Codex。状态回读确认
`source=serial`、`payload=valid`，截图确认模型、思考强度、速度档位和用量正常显示。
连续三次关闭再打开串口后运行时间保持递增、数据保留，BLE/HID 仍连接；Ctrl+C 正常退出。
截图保存在忽略目录 `backups/`。物理拔插恢复已覆盖自动化模拟，实机拔插及 Intel Mac
仍待人工验证。

主画面始终显示 `Codex`，并显示 `DEFAULT`、`PLAN`、`FAST` 和两项用量。
Plan 当前继承默认模型，`plan_mode_reasoning_effort` 不作为模型名读取。
两条模型行分别显示 `model [reasoning_effort]`：DEFAULT 读取
`model_reasoning_effort`，PLAN 读取 `plan_mode_reasoning_effort`，未配置时显示
`[--]`。长模型名会省略，思考强度标签完整保留。
Fast 的显示值为 `ON`、`OFF`、`ULTRAFAST` 或未知值 `--`。用量按日志中的
`window_minutes` 分配到 SESSION（300 分钟）和 WEEKLY（10080 分钟）；缺失窗口
显示 `--%`，而不是 0%。

扩展 JSON 的可选 `cfg` 对象包含 `model`、`plan_model`、`service_tier`，以及
可选的 `reasoning_effort`、`plan_reasoning_effort`；`XDBG STATUS` 回传同名字段。
仅发送 `cfg` 和 `src` 可更新模型状态而不覆盖已有用量。主机每次发送后查询
`XDBG STATUS`，核对 `source=serial`、配置字段和用量接收状态，随后关闭串口。
串口忙或断开后默认每 5 秒重试；刷机前用 Ctrl+C 退出 watcher。
USB 接收缓冲为 1024 字节，JSON 行最多 511 字节，可容纳配置消息及紧随其后的
状态查询，避免长模型名和思考强度字段导致底层接收丢字节。

2026-10-23 已在 COM3 验证：应用分区刷写和校验、启动无数据画面、Windows
真实数据同步、Fast 三种状态及未知状态、长模型名省略、三屏切换、串口占用后
自动恢复，蓝牙和 HID 仍连接。87 项 Python 测试、65 项 native 测试通过，
`xingzhi_parity` 与 `xingzhi_serial_meter` 构建通过。现场截图与旧应用分区备份
保存在本机忽略目录 `backups/`，不提交原始设备数据。

思考强度扩展已补充验证：89 项 Python 测试、67 项 native 测试及两个固件目标
构建通过；实机确认 `[high]`、`[xhigh]`、`[--]` 和长模型名省略显示正常，连续
三次 511 字节配置消息及状态回读成功。

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
语音听写阶段后，状态行还会包含 `voice`、`voice_detail`、`voice_ms`、`audio_*` 和 `voice_tx_*` 字段。`voice=recording|sending|waiting_ack|done|error` 用于确认 GPIO40、麦克风、BLE voice 传输和 host 回写状态。

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
py -3 tools\windows_claude_usage_ble.py --require-ack
py -3 tools\xingzhi_debug.py status --port COM7
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output ble-high.bmp
```

通过闸门：BLE 工具输出 `BLE write succeeded` 和 `Device acknowledged payload`；串口状态显示 `source=ble`、`payload=valid`、`detail=limited`，截图读数为 88%/82%。

默认真实用量来源就是 `--usage-source codex-wsl`，会从 WSL 的 `~/.codex/sessions/**/*.jsonl` 读取最近 Codex 用量事件，并发送 `src=codex` 让 usage 屏标题显示 `Codex`；Windows 下默认通过 `wslpath` 自动定位，也可用 `--codex-home` 指定路径。需要 Claude API 用量时，显式传入 `--usage-source claude`，该模式发送 `src=claude`。旧 payload 或测试 preset 缺少 `src` 时默认按 Claude 处理。

如果 Windows 已连接 `Claude Controller` 但扫描不到广播，工具会从系统已配对设备中回退解析 BLE 地址并直接连接。也可以从 `status` 输出复制 `ble_mac`，显式执行 `py -3 tools\windows_claude_usage_ble.py --address <ble_mac> --test-preset high --require-ack`。

BLE 配对状态异常或 Windows GATT 缓存不一致时，可先通过串口触发恢复：

```powershell
py -3 tools\xingzhi_debug.py ble reset --port COM7
py -3 tools\xingzhi_debug.py status --port COM7
```

`watch` 长期运行模式用于 usage 单功能调试；扫描、连接、通知、写入、ack/nack 或 polling 失败会按 `--retry-delay` 重试：

```powershell
py -3 -u tools\windows_claude_usage_ble.py --watch --require-ack --poll-interval 60 --retry-delay 5
```

## BLE 语音与 ASR 验证

GPIO40 当前是语音听写键：短按忽略，长按开始录音，松开后通过 BLE voice characteristic 上传 WAV。日常使用应启动 combined watcher，让 Codex usage 和语音听写共用一个 BLE 连接；usage polling 和 voice receive 分别运行在独立 asyncio task 中，因此 voice 长时间等待时 usage 仍会按 `--poll-interval` 或设备 `REQ` 刷新。完整听写模式会调用硅基流动 ASR，并把文本粘贴到当前 Windows 焦点窗口。先把焦点放到记事本或输入框，再启动：

```powershell
Set-Content .env "SILICONFLOW_API_KEY=sk-..."
tools\watch_xingzhi_combined.bat --address 94:A9:90:1B:6D:FD
py -3 tools\xingzhi_debug.py button voice --event long_press --port COM7
py -3 tools\xingzhi_debug.py button voice --event release --port COM7
py -3 tools\xingzhi_debug.py status --port COM7
```

`watch_codex_wsl.bat` 和 `watch_xingzhi_voice_dictation.bat` 保留为单功能排障入口，但不要同时运行；两个进程会争用同一个 Xingzhi BLE 外设连接。需要单独排查 voice 时再启动 voice helper：

```powershell
tools\watch_xingzhi_voice_dictation.bat --address 94:A9:90:1B:6D:FD
py -3 tools\windows_xingzhi_voice_dictation.py --watch --receive-timeout 0 --address 94:A9:90:1B:6D:FD --output C:\Windows\Temp\voice.wav
py -3 tools\xingzhi_debug.py button voice --event long_press --port COM7
py -3 tools\xingzhi_debug.py button voice --event release --port COM7
py -3 tools\xingzhi_debug.py status --port COM7
```

通过闸门：helper 输出 `Voice metadata`、`Voice complete`、`Saved WAV` 和 `Pasted transcript`；焦点窗口出现听写文本；串口状态显示 `voice=done`、`voice_tx=done`，`voice_tx_chunks=<n>/<n>`。

排障模式可以只保存 WAV 或只验证硅基流动 `TeleAI/TeleSpeechASR` 转写，不粘贴文本：

```powershell
py -3 tools\windows_xingzhi_voice_dictation.py --dump-only --output C:\Windows\Temp\voice.wav
py -3 tools\windows_xingzhi_voice_dictation.py --input-wav C:\Windows\Temp\voice.wav --asr-only
```

缺少 `.env` 或 `SILICONFLOW_API_KEY` 时，工具会在连接 BLE 前失败；错误日志不会打印密钥。

## 硬件能力探测

可选硬件能力先通过 `probe` 记录证据，不把未知硬件能力变成阻塞项：

```powershell
py -3 tools\xingzhi_debug.py probe imu --port COM7
```

当前 Xingzhi 1.54 WiFi 返回 `status=not_available`、`method=xiaozhi_board_config`、`detail=no_i2c_or_imu_config`、`checked=qmi8658_0x6b`。这表示未发现可用 IMU 配置，自动旋转暂不适用于当前硬件。
