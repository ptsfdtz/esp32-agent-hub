# Phase 1 验证记录

日期：2026-09-05。

| 验证 | 结果 |
| --- | --- |
| `python -m platformio run -e agentdeck -e agentdeck-usb` | 两种串口环境编译、链接和生成固件成功；构建日志无 warning/error |
| UART 固件资源 | 静态 RAM 25,476 / 327,680 字节；Flash 312,317 / 3,342,336 字节 |
| 原生 USB 固件资源 | 静态 RAM 25,252 字节；Flash 299,877 字节 |
| `python tools/check.py` | 4,285 项检查通过，另外每帧断言模拟 OLED RAM 等于 framebuffer；包含重复 detent 验证，不代表相同数量的独立场景 |
| 实际 U8g2 framebuffer 检查 | 31 张代表画面：六页、详情、离线、滚动、切页、通知、待机/睡眠/唤醒及交互表情；323 帧 GIF 序列 |
| 源码检查 | 产品 src 无 delay、String、malloc/new；OLED SendBuffer 只在 Renderer |

视觉验证中修复了 Agent 圆点在 XOR 绘制时被对称算法重复抵消的问题，新增在线实心点/离线空心点像素断言。
页面切换与切换中反向返回均验证首帧和上一张实际 framebuffer 完全一致。
像素预览使用原版 U8g2 C 代码、同一套 ScreenManager / Renderer / Screens；只替换传输 callback，未复制页面实现。

预览：[phase1-preview.png](phase1-preview.png)。测试工具会重新生成 `build/preview/`；文档中的 PNG 是本次已检查的快照。

当前字体：Helvetica 正文/标题、5×8 辅助字体、ProFont 22 数字；统一字号入口 `src/ui/Typography.h`，计时数字按真实字宽居中。
新伙伴通过 AnimationManager 的 BuddyLook / BuddyLift / BuddyLid / IdleReveal 通道驱动，无 delay 或独立刷新循环。
新增验证覆盖闲置计时的 millis 回绕、首次唤醒不执行命令、快速反向视线续接、睡眠，以及 REDUCED / OFF 静止行为。
[动画预览](buddy-motion.gif) 直接编码生产 Renderer 的 framebuffer；GIF 时间精度有限，不能代替真实设备 30FPS 测量。

未完成的物理验收：尚未验证旋钮电气抖动与每格边沿数、按钮手感、持续动画的长期 FPS，以及数小时运行稳定性。
仅检测到 COM7 CH343 串口，尚不能据此确认目标板的 Flash/PSRAM 型号。
当前按 DevKitC-1 N8 构建且不使用 PSRAM；烧录前需匹配实际板卡配置。
没有真实网络连接，因此 MQTT/Wi-Fi 故障恢复测试属于后续阶段。

## 2026-09-05 实机启动诊断

已通过 COM7 将 `agentdeck` 环境烧录到 ESP32-S3。设备在 GPIO8/GPIO9 的 I2C 总线上检测到 SH1106 候选地址 `0x3C`；`0x3D` 无应答。
最初程序停在 `U8g2 begin`：PlatformIO 固定的 Arduino-ESP32 2.0.17 在探测阶段已打开 Wire 后，U8g2 再次初始化同一总线时阻塞。
修复方式是完成地址探测后调用 `Wire.end()`，再由 U8g2 使用构造器中的 GPIO8/GPIO9 接管总线。

修复后实机日志完整通过 `U8g2 begin complete` 并进入主循环。两秒采样：

```text
frames=24 render_us=32279 max_us=32435 over_budget=0 input_overflow=0
```

当前一次完整绘制与 400kHz I2C 发送约 32.3ms，未超过 33ms 帧预算；这次短采样不能替代长时间帧率和交互压力测试。

## 简洁卡通界面与局部刷新实测

已将新版烧录 COM7，I2C 0x3C 应答、启动正常。27 秒启动/眨眼/进入待机采样：

```text
frames=17  render_us=849  max_us=31664 over_budget=0 input_overflow=0
frames=90  render_us=1567 max_us=31664 over_budget=0 input_overflow=0
frames=123 render_us=1566 max_us=31664 over_budget=0 input_overflow=0
```

单帧统计包含绘制、差异检测及实际需要的 I2C 发送。0.849–1.567ms 的采样可能没有像素变化或只有局部变化，不能当作完整 OLED 传输时间。
本次最大值 31.664ms，无超过 33ms 的帧；输入溢出为零，但采样不包含人工高速旋钮压力测试。
`heap=363220` 在采样中保持一致，不等于已经证明数小时无泄漏。
局部刷新在主机端经过模拟 OLED RAM 与 framebuffer 的逐帧一致性验证，覆盖擦除、切页中断和待机唤醒。
