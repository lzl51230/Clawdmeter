# Xingzhi Parity 固件

`xingzhi_parity` 是 Xingzhi 版本补齐原版 Clawdmeter 核心体验的独立目标。它不替代 `xingzhi_display_bringup` 或 `xingzhi_serial_meter`：前者继续用于显示驱动排障，后者继续作为最小 USB 串口用量仪表。

## 当前能力

- USB 串口 JSON fallback：兼容 `tools/send_test_payload.py`。
- 串口调试：`tools/xingzhi_debug.py status|screenshot|button|ble|payload|probe`。
- 240x240 framebuffer 截图：RGB565LE framed binary，可转 `.bmp` 或 `.ppm`。
- 三屏 UI：usage、status、splash，通过 `button cycle --event click` 切换。
- 原版 Clawd splash 动画：复用生成帧数据，按 idle/normal/active/heavy 用量增长组选择动画，splash 屏内 `cycle` 切换动画。
- 动作分发器：`cycle`、`exit`、`space`、`shift_tab` 可由串口模拟、实体按键和 BLE HID 共用。
- BLE GATT 用量通路：广播名 `Claude Controller`，沿用原版 service/RX/TX/REQ UUID，BLE payload 与串口 fallback 走同一解析器。
- Windows BLE 调试 CLI：`tools/windows_claude_usage_ble.py` 可发送真实 Claude 用量或固定测试 payload。
- Xingzhi 三键输入：GPIO0 切屏/切换 splash 动画/长按退出 splash，GPIO40 发送 Space，GPIO39 发送 Shift+Tab；HID 通过 BLE keyboard report 输出。
- 电源遥测：GPIO38 读取充电状态，ADC2 channel 6 读取电池电压分段，样本稳定后在 status 屏显示电量。
- HID 电量同步：仅当电源状态为 `valid` 时，将稳定电量发布到 BLE HID battery service。
- IMU 探测：`probe imu` 稳定报告当前 Xingzhi 1.54 WiFi 未发现可用 IMU 配置。

## 电源观察记录

- 2026-05-14，Xingzhi 1.54 WiFi 通过 USB 连接 Windows/WSL 调试机供电时，串口 `status` 稳定报告 `power=valid`、`samples=3`、`adc=2443/2443` 到 `2444/2444`、`battery=100`、`charging=1`。
- 当前未改动 ADC 分段校准，仍使用小智来源的 1970/2062/2154/2246/2338/2430 分段作为初始基线。U6 仅将 `valid` 电量同步到 BLE HID battery；`sampling`、`unavailable` 或 `error` 不发布新的 HID 电量。

## IMU 探测记录

- 2026-05-14，对比本地 `xiaozhi-esp32/main/boards/xingzhi-cube-1.54tft-wifi/` 后，未发现该板的 I2C、IMU、QMI8658、MPU 或 Wire 配置；原版 Clawdmeter 的 Waveshare 目标使用 QMI8658 `0x6b`，但不能直接套用到 Xingzhi。
- 2026-05-14，串口执行 `tools\xingzhi_debug.py probe imu --port COM7` 返回 `status=not_available`、`method=xiaozhi_board_config`、`detail=no_i2c_or_imu_config`、`checked=qmi8658_0x6b`。因此自动旋转在当前 Xingzhi 1.54 WiFi 硬件上记录为不适用，不阻塞其他功能移植。

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

U1 通过标准：`status` 返回 `target=xingzhi_parity`、`width=240`、`height=240`、`framebuffer=ready`，截图命令能回读 240x240 RGB565LE 图像；串口 JSON fallback 仍可接收测试 payload。

U2 通过标准：`button cycle --event click` 能切屏，`button space` 和 `button shift_tab` 能记录动作、事件和计数，且不会改写最近的用量数据。

U3 通过标准：连续执行两次 `button cycle` 后，`status` 分别报告 `screen=status` 和 `screen=splash`；每个屏幕都能截图回读为 240x240 图像。

U4 通过标准：刷写后 `status` 报告 `ble=advertising`、空格安全的 `ble_name=Claude_Controller` 和 `ble_mac`；实际 BLE 广播名仍为 `Claude Controller`。status 屏截图显示 BLE 状态；串口 fallback 发送 `high` 后仍能更新 usage。

U5 通过标准：Windows 侧安装 Bleak 后执行 `py -3 tools\windows_claude_usage_ble.py --test-preset high --require-ack`，工具报告 BLE write 和 TX ack 成功；随后串口 `status` 显示 `source=ble`、`payload=valid`、`detail=limited`，截图显示 high payload 的 88%/82% 读数。

U6 通过标准：串口模拟 `cycle` 后状态切屏；模拟 `space`/`shift_tab` 时 `action` 和 `event` 被记录。未连接 BLE HID 时状态显示 `hid=unavailable` 和 `action_error=hid_unavailable`；BLE 客户端保持连接时状态显示 `hid=available`。实体按键需要人工按压验证，配对为键盘后 Space 与 Shift+Tab 应出现在当前焦点窗口。

2026-05-14 人工验收完成：实体 UI 切换键、BLE HID `Space` 和 BLE HID `Shift+Tab` 已在 Windows 当前焦点窗口验证通过。

U7 通过标准：启动约 5 秒后连续查询 `status`，状态显示 `power=valid`、`samples=3`、`adc=<avg>/<raw>`、`battery=<0-100>` 和 `charging=0|1`；status 屏截图显示 battery 行。当前 USB 供电实测 ADC 约 2443-2444，显示 `battery=100`、`charging=1`。

## Phase 2 验证闸门

U1 BLE 恢复入口通过标准：执行 `py -3 tools\xingzhi_debug.py ble reset --port COM7` 返回 `ok=1`、`message=pairing_reset`，随后 `status` 显示 `screen=status`、`ble=advertising`、`framebuffer=ready`，截图可见 status 屏恢复结果。若 Windows 仍持有旧 GATT 缓存，先在 Windows 蓝牙设备中移除 `Claude Controller` 后重新连接。

U2 Windows watch 通过标准：执行 `py -3 -u tools\windows_claude_usage_ble.py --test-preset high --require-ack --watch --poll-interval 3 --retry-delay 1` 时，扫描、连接、通知或发送失败会记录日志并重试；成功写入后串口 `status` 显示 `source=ble`、`payload=valid`、`detail=limited`，status 屏截图显示 connected BLE 状态。一次性发送模式遇到 nack 或必需 ack 超时仍返回失败。

U3 splash 动画通过标准：切到 `screen=splash` 后，`status` 显示 `splash=<name>`、`splash_group=idle`、`splash_category=<category>` 和递增的 `splash_frame`；连续截图能看到 Clawd 动画帧变化。

U4 用量驱动动画通过标准：通过 `payload` 或 BLE 发送两次相隔至少 4 分钟的递增 session 样本后，`status` 显示 `splash_group=heavy` 且 splash 名称进入高用量组；随后发送 malformed payload 时 `payload=invalid`，但保留高用量 splash 组和最近有效读数。

U5 splash 按键语义通过标准：在 splash 屏执行 `button cycle` 返回 `message=splash_next` 且仍停留在 `screen=splash`；执行 `button exit` 返回上一非 splash 屏。实体 GPIO0 短按走同一语义，长按退出 splash。

U6 电源/HID 电量通过标准：USB 供电下 `status` 显示 `power=valid`、`battery=100`、`charging=1`、`hid_battery=100`；sampling、unavailable 或 error 状态不会发布新的 HID battery。

U7 IMU probe 通过标准：执行 `py -3 tools\xingzhi_debug.py probe imu --port COM7` 返回 `ok=1`、`status=not_available`、`method=xiaozhi_board_config`、`detail=no_i2c_or_imu_config`、`checked=qmi8658_0x6b`；随后 `status` 和截图仍可正常回读。

## Windows BLE 用量发送

```powershell
py -3 -m pip install bleak
py -3 tools\windows_claude_usage_ble.py --test-preset high --require-ack
py -3 tools\windows_claude_usage_ble.py --require-ack
py -3 tools\windows_claude_usage_ble.py --dry-run
py -3 tools\windows_claude_usage_ble.py --watch
```

`--test-preset` 不访问真实数据源，适合先验证 BLE。默认真实用量模式等同 `--usage-source codex-wsl`，从 WSL 的 `~/.codex/sessions/**/*.jsonl` 读取最近 Codex `token_count` 用量；Windows 下会通过 `wslpath` 自动定位，也可用 `--codex-home` 指定 UNC 路径。Codex payload 会带 `src=codex`，usage 屏标题显示 `Codex`。需要 Claude API 用量时，显式传入 `--usage-source claude`；该模式读取 `%USERPROFILE%\.claude\.credentials.json` 内的 `accessToken`，调用 Claude Messages API 并把响应头压缩为 `{s,sr,w,wr,st,src,ok}` 后写入 RX characteristic，usage 屏标题显示 `Claude`。旧 payload 或测试 preset 缺少 `src` 时默认按 Claude 处理。`--watch` 是第一版长期运行模式；扫描、连接、通知、写入和 ack/nack 失败会记录日志并重试。观察日志时建议使用 `py -3 -u` 避免 Windows stdout 缓冲。

Windows 已配对或已连接 BLE HID 后，设备可能不再出现在扫描结果里。主机工具会在扫描失败时查找 Windows 已配对设备并直连；必要时也可用串口 `status` 中的 `ble_mac` 手动传入 `--address`。

## UI 屏幕

- `usage`：保留已验证的 session、weekly、reset、payload 状态和 high/error 颜色；标题随数据源显示 `Claude` 或 `Codex`。
- `status`：显示 BLE/HID 状态、最近数据来源、payload 细节、最后动作、电源读数和恢复提示。
- `splash`：播放原版 Clawd 像素动画，按用量增长组自动轮换；splash 内 `cycle` 切换动画，`exit` 或长按 cycle 返回上一屏。

## 最终回归矩阵

- 主机工具：`python3 -m unittest discover -s tools/tests`。
- 固件 native：`pio test -d firmware -e native`。
- Xingzhi parity 构建：`pio run -d firmware -e xingzhi_parity`。
- 默认 Waveshare 构建：`pio run -d firmware -e waveshare_amoled_216`，确认原版目标仍可编译。
- 硬件闸门：刷写 `xingzhi_parity` 后依次执行 `status`、`screenshot`、`button cycle`、`payload`、`ble reset`、`probe imu`；涉及 BLE/HID 的交互继续用 Windows 真实配对人工验收。

## 注意事项

不要把 BLE、HID 或 splash 动画加入 `xingzhi_serial_meter`。新增能力应先进入 `xingzhi_parity`，并在串口状态、截图或模拟按键闸门通过后再进入下一段。
