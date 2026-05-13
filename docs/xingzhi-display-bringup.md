# Xingzhi 显示 Bring-up

本阶段只验证 `xingzhi-cube-1.54tft-wifi` 的 ST7789 屏幕和背光，不实现串口用量输入、BLE、HID、触摸、PMU、IMU、splash 或用量仪表。

## 目标硬件

- 芯片：ESP32-S3
- 屏幕：240x240 ST7789 SPI
- SPI：SDA GPIO10、SCL GPIO9、DC GPIO8、CS GPIO14、RES GPIO18
- 背光：GPIO13
- 初始参数：SPI mode 3、80 MHz、颜色反转开启

## 1. 确认 Windows 工具

在 Windows PowerShell 中确认 Python、esptool 和 PlatformIO 可用：

```powershell
py -3 -m esptool version
pio --version
```

如果只在 WSL 中构建，也可以用：

```bash
python3 -m platformio --version
```

## 2. 备份当前 Xiaozhi 固件

刷写 Clawdmeter 前必须先备份。默认端口按当前机器使用 `COM7`，默认波特率为较稳的 `460800`：

```powershell
py -3 tools\backup_xingzhi_flash.py --port COM7
```

成功后会在 `backups/` 下生成 `xiaozhi-COM7-...bin`，并校验文件大小。当前 COM7 设备自动识别为 16MB flash。若自动识别失败，可以显式指定：

```powershell
py -3 tools\backup_xingzhi_flash.py --port COM7 --size 16MB
```

如果读取过程中出现 `Serial data stream stopped`，先降速并分块重试：

```powershell
py -3 tools\backup_xingzhi_flash.py --port COM7 --baud 115200 --chunk-size 256KB
```

分块模式默认每块重试 2 次；如果仍不稳，可把块缩小到 `64KB` 并提高 `--retries 5`。当某个块连续失败时，工具会自动把该块二分成更小块读取，最低到 `--min-chunk-size`：

```powershell
py -3 tools\backup_xingzhi_flash.py --port COM7 --baud 115200 --chunk-size 64KB --retries 5
```

如果 stub flasher 不稳定，再追加 `--no-stub`。这会明显更慢，但能排除 stub 兼容性问题。

## 3. 构建显示验证固件

```powershell
pio run -d firmware -e xingzhi_display_bringup
```

该环境通过 `build_src_filter` 只编译 `firmware/src/xingzhi_display_bringup.cpp`，不会带入原 Waveshare 应用的 BLE、LVGL UI、PMU、IMU 或 splash 逻辑。

## 4. 刷写与串口日志

确认备份文件存在且大小正确后，再刷写：

```powershell
pio run -d firmware -e xingzhi_display_bringup -t upload --upload-port COM7
pio device monitor -d firmware -p COM7 -b 115200
```

如果 Windows 没有 `pio` 命令，但 WSL 已经构建出 factory image，可以直接用 Windows esptool 刷写：

```powershell
py -3 -m esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 firmware\.pio\build\xingzhi_display_bringup\firmware.factory.bin
```

刷写会覆盖当前 Xiaozhi 固件；需要恢复时，用备份 bin 按 esptool 写回。

## 5. 观察测试屏

预期画面包含：

- 白色外框和灰色内框，确认没有裁切。
- `TL`、`TR`、`BL`、`BR` 四角标记，确认方向。
- 红、绿、蓝、白色块，确认 RGB 和颜色反转。
- 黑、深灰、灰、白灰阶块，确认亮度。
- `SPI MODE 3` 和 `BL GPIO13` 文本，确认运行的是 bring-up 固件。

如果黑屏，优先检查背光 GPIO13、USB 供电和串口日志。若颜色异常，先调整 `DISPLAY_INVERT_COLOR`。若方向异常，先调整 `DISPLAY_ROTATION`。

## 已验证结果

2026-05-13 在 COM7 的 `xingzhi-cube-1.54tft-wifi` 上完成验证：

- Xiaozhi 原固件已备份到 `backups/xiaozhi-COM7-adaptive-0x1000000.bin`，大小 16MB。
- 本机稳定备份命令为 `py -3 -u tools\backup_xingzhi_flash.py --port COM7 --baud 115200 --chunk-size 64KB --min-chunk-size 4KB --retries 5 --output backups\xiaozhi-COM7-adaptive-0x1000000.bin`。
- `xingzhi_display_bringup` factory image 已刷写并通过 esptool hash 校验。
- 串口日志显示 `Display test screen drawn.` 和周期性 `Display bring-up alive.`。
- 实体屏幕能看到测试画面、四角 `TL/TR/BL/BR`、红绿蓝白色块和外框。

## 进入下一阶段条件

只有在静态测试屏已点亮、边框完整、方向和颜色可判断后，才继续实现 USB 串口用量输入。后续 BLE/HID/多屏能力必须进入独立的 `xingzhi_parity` 目标，不回写到本 bring-up 环境。
