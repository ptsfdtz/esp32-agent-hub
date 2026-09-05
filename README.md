# Agent Deck

ESP32-S3 桌面 Agent IoT 控制终端，当前固件 **v0.2.0，正式数据全部来自实际通讯**。
现有六页 UI、字体、伙伴动效、布局、旋钮输入和 33ms 帧调度保持不变。

已接入 Wi-Fi、MQTT 双向通信、NTP、带密码 ArduinoOTA、PC Bridge、Codex 真实额度读取，以及 Codex / Claude / OpenCode 真实任务运行器和事件输入。
没有配置或没有心跳时显示离线；没有真实用量/GPU 数据时显示 `--`，时钟同步前显示 `--:--`。不会切回 Mock。

[配置与通讯协议](docs/communication.md) · [本轮验证记录](docs/network-validation.md) · [现有 UI 设计](docs/design-v2.md)

## 开始使用

在仓库根目录执行：

```powershell
python -m pip install platformio
Copy-Item src/config/Secrets.example.h src/config/Secrets.h
# 编辑 Secrets.h：实际 Wi-Fi、MQTT Broker 和 OTA 密码
python -m platformio run -e agentdeck

python -m pip install -r bridge/requirements.txt
python tools/configure_bridge.py --host YOUR_BROKER_IP --device agentdeck-01
python -m bridge.service --config bridge/config.json
```

Secrets.h 和 bridge/config.json 已忽略，不提交凭据。Broker 账号通过 Bridge 环境变量 `AGENTDECK_MQTT_USER` / `AGENTDECK_MQTT_PASSWORD` 配置。
没有 Secrets.h 也可编译，但设备保持离线并提示配置 Wi-Fi。Bridge 使用实际已有的 Broker，不自动部署生产服务。

Codex 额度通过本机已登录的 `codex app-server` 只读读取。正在运行的 Agent 状态需要由实际任务运行器或会话事件接入；不根据账号登录成功推断 Working。
运行器用法、Claude/OpenCode 接入和控制 handler 约定见[通讯文档](docs/communication.md)。

## 构建和升级

固定依赖：Espressif32 6.12.0 / Arduino ESP32 2.0.17 / U8g2 2.36.15 / PubSubClient 2.8 / ArduinoJson 6.21.5。
目标 ESP32-S3-DevKitC-1 N8、8MB Flash、不依赖 PSRAM。硬件容量需与实际板卡一致。

```powershell
# COM7 仅是此前开发记录中的 CH343 端口，使用前核对
python -m platformio run -e agentdeck -t upload --upload-port COM7
python -m platformio device monitor -p COM7 -b 115200
# 原生 USB CDC 接口
python -m platformio run -e agentdeck-usb
```

默认 agentdeck 使用 UART，agentdeck-usb 使用原生 USB CDC。
固件输出 `.pio/build/agentdeck/firmware.bin`。此次启用了明确的 8MB 双 OTA 分区，第一次从旧版升级应使用 USB/UART 全量烧录分区表；之后可以通过 ArduinoOTA 推送。OTA 密码未设置则不启用升级服务。

## 固定接线

OLED：1.3 英寸 128×64 SH1106，`U8G2_SH1106_128X64_NONAME_F_HW_I2C`，400kHz I2C。

| 信号 | GPIO |
| --- | --- |
| CONFIRM / CON | 15 |
| SDA / SCL | 8 / 9 |
| PUSH / PSH | 6 |
| 编码器 TRA / TRS | 4 / 5 |
| BACK / BAK | 7 |
| 电源 | 3.3V / GND |

输入上拉、按下接地；方向需要修正时只调整 Config.h 的 EncoderDirection。hardware/hardware.ino 仍是保留的原始接线测试，不是产品入口。

## 操作

| 场景 | 旋钮 | PUSH | CONFIRM | BACK |
| --- | --- | --- | --- | --- |
| HOME | 打开页面菜单 | — | 页面菜单 | 保持 HOME |
| 页面菜单 / AGENT / SETTINGS | 平滑选择 | — | 进入选中项 | 返回上级 |
| PC | 页面菜单 | — | 页面菜单 | 页面菜单 |
| IOT | 页面菜单 | — | 网络详情 | 页面菜单 |
| TIMER | 暂停时 ±1 分钟 | — | 开始/暂停 | 页面菜单 |
| 亮度 / 动画详情 | 调整值 | — | — | 设置列表 |
| Agent 详情 | — | — | 发送人工确认 confirm | 发送 cancel 并返回列表 |

- 菜单统一通过旋钮旋转选择、按下 CONFIRM 进入；短按旋钮 PUSH 不进入。Agent 详情中的 CONFIRM 发送人工确认。
- 长按 BACK 返回 HOME；Agent 详情长按 PUSH 发送 stop。
- HOME / PC 长按 PUSH 不再生成数据。Timer 长按 PUSH 复位 25 分钟。
- 待机首次输入仅唤醒，不发送远程命令；长按门槛仍为 700ms。
- `COMMAND SENT` 仅表示已发送；收到实际 handler 回执才显示完成；不支持的动作会拒绝，12 秒无回执显示超时。
- 亮度和动画设置仍保存在 RAM，FULL / REDUCED / OFF 的视觉行为保持原样。

## 网络与稳定性

Wi-Fi / MQTT / NTP / OTA 在独立 Core 0 任务中处理，主循环通过固定队列零等待交换快照和命令；不共享可变 Model，MQTT callback 不绘制 OLED。
Wi-Fi 和 MQTT 独立指数退避重连；15 秒没收到实际心跳，Agent / PC 转为离线。Focus Timer 继续使用 millis。
任务完成事件通过原有 toast 提示，不添加新页面。

串口小写 `s` 输出 `frames / render_us / max_us / over_budget / input_overflow / heap / min_heap`。
小写 `d` 探测 GPIO8/9 上 0x3C / 0x3D。诊断保留原有实现，不要在帧率测试中反复运行 I2C 探测。

## 验证

```powershell
python -m pip install ziglang pillow
python tools/check.py
python tools/check_network.py
# 实际 TCP MQTT 本机集成测试，amqtt 仅用于测试
python -m pip install -r bridge/requirements-test.txt
python tools/check_mqtt.py
```

UI 主机测试运行真实状态机和 U8g2 framebuffer；测试用样本仅位于 tests/fixtures，不编入固件。
通讯测试包含真实 OS 数据采集、TCP Broker、生产 C++ JSON 解析、回执、重复命令、Broker 重启和真实测试任务停止。
测试 Broker 仅绑定 127.0.0.1 临时端口并在退出时关闭。

编译和本机通讯测试不能替代真机 Wi-Fi 断网输入手感、OLED 30FPS、OTA 写入/恢复和数小时内存稳定性测试。当前验证范围见[验证记录](docs/network-validation.md)。

## 目录

```text
src/config/       固定参数、实际网络配置入口
src/hardware/     OLED / GPIO / 编码器中断
src/input/        输入事件、消抖、四相状态机
src/models/       实际 Agent / PC / 网络 / 设备状态
src/network/      Core 0 网络任务、JSON 协议、重连退避
src/ui/           原有动画、帧调度、页面状态、Renderer、Timer
src/screens/      原有六页布局，绑定真实 Model
bridge/           PC 监控、Codex 额度、任务运行器、真实事件输入、命令处理
tests/           UI / 协议 / Bridge 测试（fixtures 仅限测试）
tools/            构建验证、配置生成、本机 MQTT 集成测试
```

原始设计保留在 AGENT.md。docs/architecture.md、docs/validation.md 中的 Phase 1 描述是历史记录，当前联网实现以本 README 与 communication.md 为准。
