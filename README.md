# Agent Deck

ESP32-S3 桌面 Agent IoT 控制终端。当前完成 **Phase 1 固件实现**：固定硬件、旋钮/按键、六个页面、统一动画、Mock 数据。
真实 Wi-Fi、MQTT、NTP、OTA 和 PC Bridge 尚未接入；屏幕右上角 `M` 表示 Mock。

[查看实际 U8g2 页面与转场像素预览](docs/phase1-preview.png) · [验证记录](docs/validation.md)

## 字体与待机伙伴

正文统一使用 ProFont 11 等宽像素字体，大号计时使用 ProFont 22，数字自动居中。
风格参考代码编辑器的 mono 排版，采用适合 1-bit OLED 的原生点阵；并非直接移植 VS Code 桌面字体。

右上角的小机器人会对操作作出不同回应：旋转时左右看，CONFIRM 开心点头，PUSH 好奇睁眼，BACK 眨眼，长按 PUSH/CONFIRM 显示认真表情。
表情表示收到输入，不代表远程命令执行成功。快速连续操作会从当前视线/位置续接。

首页无操作 20 秒后，待机面板用 200ms 滑入，保留时钟及用量；机器人轻微呼吸、环顾和眨眼。
45 秒后闭眼打盹。首次旋转或按键只唤醒，下一次操作恢复正常功能；Timer 完成通知也会唤醒。
FULL 启用这些效果；REDUCED / OFF 保留静态角色并关闭待机面板和表情运动。
其他页面只保留小角色和轻微眨眼，不遮挡菜单。静止时不会持续 30FPS 刷新，只在短暂动效期间按帧调度。

[查看真实渲染帧生成的待机与交互 GIF](docs/buddy-motion.gif)

## 构建

```powershell
python -m pip install platformio
python -m platformio run
```

固定依赖：Espressif32 6.12.0 / Arduino ESP32 2.0.17 / U8g2 2.36.15。
当前构建目标为 ESP32-S3-DevKitC-1 N8、8MB Flash、不依赖 PSRAM。板卡具体容量需要与实物核对。
默认 `agentdeck` 使用 UART 串口，适合 CH343；`agentdeck-usb` 使用 ESP32-S3 原生 USB CDC。

```powershell
# 确认端口和板卡后烧录；COM7 是开发时发现的 CH343 端口
python -m platformio run -e agentdeck -t upload --upload-port COM7
python -m platformio device monitor -p COM7 -b 115200

# 原生 USB 接口使用此环境
python -m platformio run -e agentdeck-usb
```

固件输出 `.pio/build/agentdeck/firmware.bin`；建议通过 PlatformIO 烧录，以同时写入匹配的 bootloader 和分区表。
`hardware/hardware.ino` 是保留的原始接线测试，不是产品入口。

## 固定接线

OLED：1.3 英寸 128×64 SH1106，`U8G2_SH1106_128X64_NONAME_F_HW_I2C`，400kHz I2C。

| 信号 | ESP32-S3 |
| --- | --- |
| CON / CONFIRM | GPIO15 |
| SDA | GPIO8 |
| SCL | GPIO9 |
| PSH / PUSH | GPIO6 |
| TRA | GPIO4 |
| TRS | GPIO5 |
| BAK / BACK | GPIO7 |
| GND | GND |
| VCC | 3.3V |

输入使用内部上拉、按下接地。若顺时针方向相反，只修改 `src/config/Config.h` 的 `EncoderDirection`；
若实物编码器每格边沿数不同，核实后调整 `EncoderEdgesPerDetent`，不要交换固定接线。

## 操作

| 场景 | 旋钮 | PUSH | CONFIRM | BACK |
| --- | --- | --- | --- | --- |
| HOME | 打开页面菜单 | 页面菜单 | Codex 详情 | 保持 HOME |
| 页面菜单 / AGENT / SETTINGS | 平滑选择与滚动 | 进入 | 进入 | 返回上级 |
| PC | 页面菜单 | 页面菜单 | 页面菜单 | 页面菜单 |
| IOT | 页面菜单 | 网络详情 | 网络详情 | 页面菜单 |
| TIMER | 暂停时 ±1 分钟 | 页面菜单 | 开始/暂停 | 页面菜单 |
| 亮度 / 动画详情 | 调整值 | — | — | 设置列表 |
| Agent 详情 | — | Mock confirm 提示 | Mock confirm 提示 | Agent 列表 |

- 长按 BACK：返回 HOME。
- HOME / PC 长按 PUSH：切换一组 Mock 值，观察 72→80 等平滑变化。
- Agent 详情长按 PUSH：Mock stop 提示，不发送真实命令。
- TIMER 长按 PUSH：复位 25 分钟；计时结束显示通知，切换页面仍继续计时。
- 长按 CONFIRM：已识别并保留，无当前业务动作。
- 长按门槛 700ms；短按在松开后触发。

Settings 提供 WiFi、MQTT、Brightness、Animation、Firmware、About。
FULL 默认；REDUCED 取消位移并缩短数值动画；OFF 关闭动画。亮度和动画设置当前只保存在 RAM，重启恢复默认。
首页时钟是从 09:42 开始的模拟时钟；Codex 72% / 41%，PC 32 / 61 / 47，网络为模拟在线。

## 性能和测试

统一 33ms 帧调度，只有动画/输入/数据/时间变化时刷新。网络代码不参与 Phase 1。
设置 → Firmware 显示最近一次帧处理时间；串口发送小写 `s` 输出：
`frames / render_us / max_us / over_budget / input_overflow / heap / min_heap`。
串口发送小写 `d` 会检测 GPIO8/GPIO9 上的 OLED 常见 I2C 地址：`0x3C` 和 `0x3D`。
启动时也会自动输出一次；若 `0x3D` 应答，固件会自动用该地址初始化 SH1106。
地址探测完成后固件会释放 Wire，再由 U8g2 初始化总线；这是为了避开 Arduino-ESP32 2.0.17 重复初始化硬件 I2C 时可能出现的阻塞。
`render_us` 包含绘制和 I2C 发送，30 FPS 需要真机验证，不能由编译或主机像素预览推断。

```powershell
python -m pip install ziglang pillow
python tools/check.py
```

测试运行真实 C++ 状态机与原版 U8g2 C 绘制代码，覆盖四相抖动/非法跳相/快转、消抖和长按互斥、
Tween 中断、millis 回绕、Timer、菜单与动效模式、切页中断首帧连续性和静止页面停刷。
测试同时输出 `build/preview/contact-sheet.png` 及各页原始 128×64 PNG/PBM。
主机 transport callback 不驱动物理设备，不能代替 OLED 帧率、输入电气和长时间稳定性测试。

真机验收步骤：

1. 检查正反旋转每格仅移动一项，快速反复旋转没有反向误判；`input_overflow=0`。
2. 每个键重复短按、长按；一次长按不会在释放后多执行一次短按。
3. 来回打开页面菜单、详情，切页未结束前按 BACK，画面连续无闪跳。
4. SETTINGS 滚动到 About，再反向回 WiFi，检查选框和文字平滑且无越界。
5. HOME 长按 PUSH 观察数字/进度更新；测试 FULL/REDUCED/OFF。
6. TIMER 运行时切换页面，回来确认时间；测试暂停、复位和完成通知。
7. 连续动画时记录帧数/实际时间，检查单帧 33ms 预算。默认保持 400kHz，不先超频。
8. 连续运行至少数小时，多次采样 heap/min_heap 和输入溢出。MQTT 故障场景待 Phase 2 实现后验收。

## 工程结构

```text
src/
  main.cpp                主循环与接线
  config/Config.h         引脚、节拍、网络配置结构
  hardware/               SH1106、输入中断与 GPIO
  input/                  InputEvent、四相解码、按键状态机
  models/Model.h          Agent / PC / Network / Device
  services/MockService.*  模拟数据源
  ui/                     AnimationManager、FrameScheduler、ScreenManager、Renderer、Timer
  screens/                HOME、AGENT、PC、IOT、TIMER、SETTINGS 和共享绘制
tests/host.cpp            核心行为和实际 framebuffer 验证
tools/check.py            主机构建、测试、像素预览生成
docs/architecture.md     工程审查、动画/帧率设计、MQTT 隔离和阶段路线
```

详细设计和第一阶段改动理由见 [工程审查与架构](docs/architecture.md)。
