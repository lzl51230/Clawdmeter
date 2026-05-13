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

固件返回一行 `XDBG STATUS`，包含 `target=xingzhi_parity`、`screen=usage`、`width=240`、`height=240`、`payload`、`source`、`ble=disabled`、`uptime_ms` 和 `framebuffer`。

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

## 用量 Fallback 验证

BLE 尚未实现时，仍可通过串口发送测试 payload：

```powershell
py -3 tools\send_test_payload.py high --port COM7
py -3 tools\xingzhi_debug.py status --port COM7
py -3 tools\xingzhi_debug.py screenshot --port COM7 --output high.bmp
```

通过闸门：状态里 `source=serial`、`payload=valid`，截图显示 high payload 的 session/weekly 读数。
