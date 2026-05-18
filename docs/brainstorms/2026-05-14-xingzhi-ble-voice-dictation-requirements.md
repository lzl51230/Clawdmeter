---
date: 2026-05-14
topic: xingzhi-ble-voice-dictation
---

# Xingzhi BLE 语音听写输入 Requirements

## Summary

将 Xingzhi 设备上的 GPIO40 从当前 HID Space 行为改为专用语音听写键：用户长按 GPIO40 时，设备从板载 I2S 麦克风录音；松开后通过 BLE GATT 将短音频发送到 Windows 端工具；Windows 调用硅基流动语音识别 API，并通过剪贴板加 Ctrl+V 把识别文本输入到当前 Windows 焦点窗口。

第一版目标是短句听写，不做实时流式识别、不做语音命令解析，也不保留 GPIO40 的 Space 兼容行为。

## Actors

- 用户：长按 GPIO40 说话，松开后希望文字出现在当前焦点窗口。
- Xingzhi 设备：检测 GPIO40、录音、BLE 上传音频、显示状态并播放提示音。
- Windows helper：接收 BLE 音频、调用硅基流动 ASR、粘贴文本到焦点窗口。
- 硅基流动 ASR 服务：使用 `TeleAI/TeleSpeechASR` 将音频转写为文本。

## Key Flows

1. 长按 GPIO40 超过阈值后开始录音，屏幕和串口进入 recording 状态，并播放开始提示音。
2. 用户松开 GPIO40 后停止录音，将音频分块通过 BLE GATT 发给 Windows helper。
3. Windows helper 收到完整音频后调用硅基流动 ASR，成功后写入剪贴板并发送 Ctrl+V。
4. 设备和串口显示成功、失败、超时、BLE 断开、ASR 错误等状态。
5. GPIO40 短按不触发录音、不发送 Space、不调用 ASR。

## Requirements

### GPIO40 Interaction

- GPIO40 在第一版中必须改为专用语音输入键，不再发送 HID Space。
- 短按低于长按阈值时必须被忽略。
- 长按达到阈值后才开始录音；松开时结束录音并启动上传。
- 单次录音最长 10 秒，到达上限后自动结束并进入上传或错误流程。
- GPIO0 和 GPIO39 的既有行为必须保持不变，除非后续计划明确修改。

### Device Audio, UI, and Debug

- 设备必须使用 Xingzhi 板载 I2S 麦克风采集音频；引脚配置应基于本地 `xiaozhi-esp32` 的 Xingzhi board 配置核对。
- 屏幕必须显示语音状态，至少覆盖 idle、recording、sending、recognizing、done、error。
- 串口调试必须能回读语音状态，并支持每阶段通过串口截图验证后再进入下一阶段。
- 串口调试应支持模拟或观测 GPIO40 语音流程，便于 Codex 在不依赖人工反复按键的情况下定位问题。
- 音频反馈需要提供简单提示音：开始录音、停止/上传、成功、失败。由于当前 Clawdmeter 尚未完整集成 Xingzhi speaker path，该项可作为单独验收阶段。

### BLE Audio Transport

- 音频传输使用 BLE GATT，不使用 USB 串口或 Wi-Fi 作为第一版主链路。
- 第一版采用松开后一次性上传录音，不做实时流式 ASR。
- BLE 传输必须支持分块、完成标记、超时和错误恢复。
- 语音上传期间可以临时占用或暂停普通 usage BLE 更新，但结束后必须恢复正常。

### Windows Helper and SiliconFlow

- Windows helper 必须支持接收设备 BLE 音频并调用硅基流动语音识别。
- 硅基流动 API key 存放在本地 `.env`，变量名使用 `SILICONFLOW_API_KEY`，必须被 `.gitignore` 覆盖，不得提交。
- 语音识别模型必须使用 `TeleAI/TeleSpeechASR`，通过 `https://api.siliconflow.cn/v1/audio/transcriptions` 的 multipart/form-data 接口上传音频。
- 缺少 API key、网络失败、ASR 失败时必须给出清晰错误，并且不得向焦点窗口粘贴错误内容。
- 成功识别后使用剪贴板加 Ctrl+V 向当前 Windows 焦点窗口发送文本。
- 第一版只做听写文本输入，不做语音命令、快捷键解析或自动执行操作。

## Acceptance Examples

- 长按 GPIO40 说一句 10 秒内的中文，松开后文本出现在当前记事本或输入框中。
- 快速短按 GPIO40 后，屏幕状态不进入 recording，Windows 端不收到音频，也不会输入 Space。
- 录音超过 10 秒时设备自动停止录音，并继续上传或显示可恢复错误。
- 未配置 `.env` API key 时，Windows helper 明确报错，设备显示失败状态，不粘贴文本。
- BLE 中断或 ASR 失败后，设备能回到可再次长按的状态，usage 数据链路后续可恢复。
- 每个实现阶段都能通过串口 `status` 和 `screenshot` 类调试命令确认状态后再继续。

## Scope Boundaries

- 不做实时语音流式识别。
- 不使用电脑麦克风作为默认音频来源。
- 不保留 GPIO40 的 Space 行为。
- 不接入 OpenAI、Azure、Whisper 本地模型或其他 ASR provider。
- 不做 Windows 托盘程序、服务化安装或开机自启。
- 不支持超过 10 秒的长段录音。
- 不实现触摸交互或额外 UI 页面。

## Key Decisions

- 音频来源：Xingzhi 设备板载 I2S 麦克风。
- 传输方式：BLE GATT。
- 识别位置：Windows 端调用云端 ASR。
- ASR provider：硅基流动，模型 `TeleAI/TeleSpeechASR`。
- 输入方式：剪贴板写入后发送 Ctrl+V。
- GPIO40 语义：专用语音输入，短按忽略，长按录音。
- 验证策略：分阶段实现，每阶段先串口状态和截图测试，再进入下一阶段。

## Dependencies and Assumptions

- Xingzhi 设备具备可用 I2S 麦克风和 speaker 引脚，需从 `xiaozhi-esp32` board 配置迁移并实机验证。
- Windows helper 可以访问 BLE 设备、硅基流动 API 和当前焦点窗口。
- BLE 带宽足够传输 10 秒以内的压缩或低采样率音频；具体编码、采样率和分块大小在计划阶段确定。
- `.env` 只用于本机私密配置，日志不得打印 API key。

## Deferred to Planning

- 选择音频编码、采样率、缓冲大小和 BLE chunk 协议。
- 决定新增独立 voice GATT service，还是扩展现有 BLE service。
- 确定硅基流动 ASR API 调用方式、错误重试策略和响应解析边界。
- 确定粘贴后是否恢复原剪贴板内容，以及失败时的回滚策略。
- 设计 speaker 提示音的驱动集成和独立验收门。
