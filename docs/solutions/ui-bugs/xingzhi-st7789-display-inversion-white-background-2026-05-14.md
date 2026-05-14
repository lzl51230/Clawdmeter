---
title: Xingzhi ST7789 Physical Screen Shows White Background
date: 2026-05-14
category: ui-bugs
module: firmware/xingzhi-display
problem_type: ui_bug
component: tooling
symptoms:
  - "Xiaozhi ST7789 physical screen appeared to use a white background"
  - "Serial framebuffer screenshots showed usage, status, and splash screens were already dark"
  - "Framebuffer screenshots and physical panel output disagreed"
root_cause: config_error
resolution_type: config_change
severity: medium
related_components:
  - "firmware"
  - "st7789"
  - "xiaozhi-device"
  - "serial-debug"
tags:
  - "xingzhi"
  - "st7789"
  - "display-inversion"
  - "framebuffer-screenshot"
  - "hardware-bringup"
  - "xiaozhi"
---

# Xingzhi ST7789 Physical Screen Shows White Background

## Problem

Xingzhi 适配中，三屏 UI 在串口截图里已经是深色背景，但实体 `xingzhi-cube-1.54tft-wifi` 的 ST7789 屏幕仍显示为白底。问题不在 UI 主题代码，而在 ST7789 面板输出反相配置。

## Symptoms

- 实体屏上 usage、status、splash 都像白底界面。
- `tools/xingzhi_debug.py screenshot` 回读的 framebuffer 是黑底。
- 串口截图和实体屏颜色不一致，说明故障发生在 framebuffer 之后、面板输出阶段。
- 将 `DISPLAY_INVERT_COLOR` 改为 `false` 并刷写后，用户肉眼确认实体屏黑底和颜色正常。

## What Didn't Work

- 继续改 UI 配色不能解释现象，因为串口截图已经证明 framebuffer 是深色。
- 只依赖串口截图不足以判断最终显示效果；截图读取的是内存 framebuffer，不包含 ST7789 控制器执行反相命令后的物理输出。
- 直接沿用 Xiaozhi 参考固件的反相假设会误导当前 Arduino_GFX 路径。上游参考使用面板反相，但本项目在这块硬件上需要关闭 `DISPLAY_INVERT_COLOR`。
- 会话历史搜索没有找到当前会话之外的相关旧结论。（session history）

## Solution

关闭 Xingzhi ST7789 输出反相：

```cpp
// firmware/src/xingzhi_display_cfg.h
constexpr bool BACKLIGHT_OUTPUT_INVERT = false;
constexpr bool DISPLAY_INVERT_COLOR = false;
constexpr uint8_t DISPLAY_ROTATION = 0;
```

修正前：

```cpp
constexpr bool DISPLAY_INVERT_COLOR = true;
```

修正后：

```cpp
constexpr bool DISPLAY_INVERT_COLOR = false;
```

同时让 bring-up 测试屏和串口日志显示真实配置，而不是写死旧状态：

```cpp
draw_label(48, 193, DISPLAY_INVERT_COLOR ? "Invert color: on" : "Invert color: off", COLOR_GRAY);
Serial.printf("Display invert color: %s\n", DISPLAY_INVERT_COLOR ? "true" : "false");
```

## Why This Works

串口截图路径读取的是渲染后的 framebuffer；ST7789 的 `invertDisplay()` 会在面板控制器输出阶段改变最终像素。原配置 `DISPLAY_INVERT_COLOR=true` 时，framebuffer 已经是黑底，但实体面板把颜色反相后显示为白底。

设置为 `DISPLAY_INVERT_COLOR=false` 后，实体面板输出和 framebuffer 颜色一致，因此黑底 UI 在屏幕上也保持黑底。

## Prevention

- 当串口截图和实体屏不一致时，优先检查面板级配置：反相、RGB/BGR 色序、旋转、SPI mode，而不是先改 UI 绘制代码。
- 显示 bring-up 画面里的诊断文本应跟随配置常量，避免硬编码 `Invert color: on` 这类过期信息。
- 每次显示驱动改动都要同时验证两层结果：串口 framebuffer 截图和实体屏肉眼效果。
- 将板级显示结论记录到 `docs/xingzhi-display-bringup.md`，因为 ST7789 反相行为可能随固件栈和板卡变体变化。

## Related Issues

- `docs/xingzhi-display-bringup.md` 记录了 2026-05-14 的实机验证结论。
- `docs/xingzhi-serial-meter.md` 说明显示方向、颜色、背光问题应先回到 `xingzhi_display_bringup` 目标定位。
- GitHub issue 搜索未执行；当前环境未发现可用 `gh`。
