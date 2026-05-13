---
date: 2026-05-13
topic: xingzhi-parity-completion
---

# Xingzhi 原版体验补齐

## Summary

将 Xingzhi 版本从已验证的串口用量仪表推进到视觉和核心体验接近原版 Clawdmeter 的版本。第一优先级是串口调试通道，让 Codex 能截图回读并模拟按键；随后恢复 BLE 用量传输、Windows 原生用量推送、HID 快捷键和 240x240 多屏 UI。

---

## Problem Frame

当前 Xingzhi 工作已经证明了高风险硬件基础：Xiaozhi 原固件已备份，ST7789 显示已验证，USB 串口用量仪表能在实体设备上渲染 normal、high 和 invalid 状态。但这还不是原版 Clawdmeter 的完整体验。原版主线通过 BLE 配对，从主机 daemon 接收真实 Claude 用量，提供 HID 快捷键，包含多屏 UI，并用 splash 形成视觉识别。

下一阶段不能盲目移植所有 Waveshare 假设。Xingzhi 目标屏幕只有 240x240，硬件交互也不同；但从 Xiaozhi 参考源码看，它有三颗物理按键，并有电池/充电相关板级支持。需求需要明确哪些原版能力要恢复，哪些要用 Xingzhi 的硬件方式替代，以及怎样让 Codex 在没有频繁人工拍照的情况下继续调试实体 UI。

---

## Actors

- A1. 用户：希望 Xingzhi 设备像真正的 Clawdmeter，而不只是测试显示器。
- A2. Codex：需要通过串口观察和驱动设备，迭代 UI 时不依赖用户每次人工描述屏幕。
- A3. Xingzhi 设备：渲染 240x240 UI，接收 BLE 用量数据，发送 HID 快捷键，并保留串口调试能力。
- A4. Windows 主机：运行手动启动的后台脚本，轮询真实 Claude 用量并通过 BLE 更新设备。
- A5. 后续规划/实现者：将本需求拆成可执行阶段和技术计划。

---

## Key Flows

- F1. 串口调试闭环
  - **Trigger:** Codex 或用户需要检查或驱动已刷写的设备。
  - **Actors:** A2, A3
  - **Steps:** 打开 USB 串口，请求屏幕截图或状态摘要，模拟一次按键动作，并观察变化后的屏幕或状态。
  - **Outcome:** Codex 能验证 UI 变化和导航行为，不必每次都请用户拍照或读屏。
  - **Covered by:** R1, R2, R3, R4

- F2. 真实 BLE 用量更新
  - **Trigger:** 用户手动启动 Windows 后台脚本。
  - **Actors:** A1, A3, A4
  - **Steps:** 发现并连接设备，轮询真实 Claude 用量，通过 BLE 发送用量更新，并记录连接和更新状态。
  - **Outcome:** Xingzhi 屏幕从真实用量数据更新，USB 串口不再是日常使用的数据主路径。
  - **Covered by:** R10, R11, R12, R13

- F3. 日常设备交互
  - **Trigger:** 用户按下 Xingzhi 实体按键。
  - **Actors:** A1, A3
  - **Steps:** 一颗按键在 usage、Bluetooth/status 和 splash 视图间切换；另外两颗按键发送原版 Clawdmeter 的 HID 快捷键。
  - **Outcome:** 即使硬件不同，物理交互模型仍接近原版 Clawdmeter。
  - **Covered by:** R5, R6, R7, R8, R14, R15

---

## Requirements

**串口调试基础**
- R1. 下一阶段必须优先实现串口调试通道，再做 BLE、HID 或视觉补齐。
- R2. 串口调试通道必须支持屏幕截图或 framebuffer 回读，回读结果要能被 Codex 用于视觉检查。
- R3. 串口调试通道必须支持模拟三颗实体按键对应的应用层动作。
- R4. 串口调试命令必须与现有串口用量测试 payload 共存，或提供明确的兼容路径，不能丢失当前 USB 验证能力。

**240x240 视觉补齐**
- R5. Xingzhi UI 必须提供原版核心屏幕的紧凑版本：usage、Bluetooth/status 和 splash。
- R6. Usage 屏必须继续显示 session 百分比、weekly 百分比、reset/status 信息，以及最近数据是否有效。
- R7. Bluetooth/status 屏必须让连接状态和恢复路径足够可见，便于配对和排障。
- R8. Splash 屏在本阶段只做占位；它应保留导航位置和基本视觉识别，但不实现完整 Clawd 动画系统。
- R9. 如果规划或实现期间能验证 Xingzhi 硬件读数，UI 应显示电池或充电状态。

**BLE 与 Windows 数据路径**
- R10. Xingzhi 固件必须通过 BLE GATT 接收日常用量更新，而不是把 USB 串口作为正式用户数据路径。
- R11. Windows 主机路径必须是一个手动启动的原生脚本，用于轮询真实 Claude 用量并通过 BLE 写入设备。
- R12. Windows 脚本必须输出清晰日志，覆盖发现、连接、更新成功、失败和重连尝试。
- R13. 引入 BLE 后，USB 串口用量输入仍必须保留为调试或救援 fallback。

**按键与 HID**
- R14. 三颗物理按键必须按已选补齐模型分配：一颗用于 UI/splash 切换，另外两颗用于 BLE HID `Space` 和 `Shift+Tab`。
- R15. HID 行为必须独立于用量数据传输可用，符合原版产品中快捷键作用于当前主机应用的预期。

**安全与兼容**
- R16. 已有 Xingzhi 备份、display bring-up 和 serial meter 工作流必须继续可用于回归和恢复。
- R17. 添加 Xingzhi 补齐功能时，不应有意破坏原 Waveshare 目标。

**分段计划与验证闸门**
- R18. 基于本需求生成实现 plan 时，必须拆成多个可独立验证的实现段，而不是一次性规划完整补齐。
- R19. 每个实现段必须定义串口调试验证闸门；在该段通过截图/回读、模拟按键或状态查询验证前，不应继续实现下一段。
- R20. 串口调试基础必须作为第一段或最早的阻塞段完成，以便后续 BLE、HID、UI 和 Windows 主机工作都能通过串口调试闭环验证。

---

## Acceptance Examples

- AE1. **Covers R1, R2.** Given Xingzhi 固件已刷写，当 Codex 请求串口截图时，主机能收到足够用于检查当前屏幕的图像数据，而不需要用户拍照。
- AE2. **Covers R3, R5, R14.** Given 设备停在 usage 屏，当 Codex 通过串口模拟切屏按键时，设备像按下实体按键一样进入下一个应用屏幕。
- AE3. **Covers R10, R11, R12.** Given Windows 脚本已启动且设备正在广播，当真实用量轮询成功时，脚本记录 BLE 更新成功，设备 usage 屏随之更新。
- AE4. **Covers R13, R16.** Given BLE 暂不可用，当发送串口测试 payload 时，设备仍能更新或报告调试状态用于恢复。
- AE5. **Covers R14, R15.** Given 设备已作为 HID 配对，当两颗快捷键按下时，当前主机应用分别收到 `Space` 和 `Shift+Tab`。
- AE6. **Covers R7.** Given BLE 连接失败或断开，当用户切到 Bluetooth/status 屏时，屏幕能表达连接问题，并提供足够信息指导恢复。
- AE7. **Covers R18, R19, R20.** Given 后续计划正在执行，当一个实现段完成时，必须先通过该段定义的串口调试验证，再开始下一段实现。

---

## Success Criteria

- Codex 可以通过串口截图/回读和模拟按键迭代 Xingzhi UI 与交互。
- 用户可以运行一个 Windows 命令行进程，通过 BLE 将真实 Claude 用量发送到设备。
- 设备离开 USB 串口会话后，仍可用于日常用量显示和 HID 快捷键。
- 240x240 UI 通过 usage、status 和 splash 屏让 Xingzhi 版本看起来像 Clawdmeter，即使完整动画精修后置。
- 后续规划能直接拆分阶段，不需要重新决定串口调试、BLE、Windows 脚本、HID 或占位 splash 是否在范围内。
- 后续计划包含明确的分段顺序和每段串口调试验证标准，避免在缺少实体反馈的情况下连续堆叠功能。

---

## Scope Boundaries

- 本阶段不实现原版完整 Clawd 动画系统。
- 本阶段不构建 Windows 托盘应用、安装器、服务或开机自启任务。
- Linux/BlueZ daemon 不是本阶段主机端主路径。
- 不强行补齐 Xingzhi 硬件没有的触摸交互或 IMU 自动旋转。
- 不把原 Waveshare 应用本身重构作为 Xingzhi 补齐的一部分。
- 串口调试通道不能变成最终用户用量传输路径；它是开发和救援路径。

---

## Key Decisions

- 串口调试优先：没有截图回读和按键模拟，后续 UI/交互迭代过度依赖人工观察。
- BLE 恢复产品路径：USB 串口已经验证可用，但原版 Clawdmeter 的日常体验是无线用量更新。
- Windows 原生主机优先：用户当前工作流在 Windows 上，只恢复 Linux daemon 无法完成目标体验。
- Splash 先占位、动画后置：导航位置和产品识别重要，但 BLE、调试和 HID 的优先级更高。
- 用物理按键替代缺失触摸：Xingzhi 有三颗按键，补齐应围绕按键导航，而不是假设存在触摸屏。
- 分段实现并设置串口验证闸门：串口调试不仅是产品功能，也是后续实现过程的质量控制手段。

---

## Dependencies / Assumptions

- 当前已完成工作继续可用：Xiaozhi 备份、`xingzhi_display_bringup` 和 `xingzhi_serial_meter`。
- 原版功能基线是 Clawdmeter 主线提交 `40d7af9` 附近的体验。
- `xiaozhi-esp32` 中的 Xingzhi 板级信息准确：240x240 ST7789、GPIO0/GPIO40/GPIO39 三颗按键，以及电池相关读数能力。
- Windows 可以从命令行脚本完成 Claude 用量轮询和 BLE 写入。
- ESP32-S3 资源足以在一个 Xingzhi 固件目标中同时承载 BLE GATT、BLE HID、紧凑 UI、串口调试和串口 fallback。

---

## Outstanding Questions

### Deferred to Planning

- [Affects R2][Technical] 决定串口截图/回读的数据表示和主机侧解码流程。
- [Affects R3][Technical] 定义精确的模拟按键动作，以及第一版是否需要 long-press 行为。
- [Affects R4, R13][Technical] 决定调试命令和 newline-delimited 用量 payload 如何共享 USB 串口通道。
- [Affects R10, R15][Needs research] 验证 BLE GATT 和 BLE HID 在本项目 Arduino ESP32-S3 环境中能否稳定共存。
- [Affects R11][Needs research] 选择 Windows 原生 BLE 机制和 Claude 用量轮询方案。
- [Affects R9][Technical] 在用户硬件上验证 Xingzhi 电池/充电读数后，再决定是否进入可见 UI。
