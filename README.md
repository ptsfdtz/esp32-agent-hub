# Agent Deck

ESP32-S3 桌面 Agent IoT 控制终端，当前固件 **v0.3.0，正式数据全部来自实际通讯**。
现有六页 UI、字体、伙伴动效、布局、旋钮输入和 33ms 帧调度保持不变。

已接入 Wi-Fi、MQTT 双向通信、NTP、带密码 ArduinoOTA、PC Bridge、Codex 真实额度读取，以及 Codex / Claude / OpenCode 真实任务运行器和事件输入。
没有配置或没有心跳时显示离线；没有真实用量/GPU 数据时显示 `--`，时钟同步前显示 `--:--`。不会切回 Mock。

[配置与通讯协议](docs/communication.md) · [本轮验证记录](docs/network-validation.md) · [现有 UI 设计](docs/design-v2.md)

## 开始使用

### Windows 单 EXE

双击 `AgentDeck.exe` 即可。内置 Bridge、Mosquitto 和所需 DLL，自动解包到 `%LOCALAPPDATA%\AgentDeck` 并隐藏运行，自动注册当前用户登录自启。无需安装 Rust、Python 或单独启动 MQTT。重复双击不会启动第二份。Codex 额度需要本机已安装并登录 Codex；新设备仍需首次 BLE 配网，MQTT 指向电脑局域网 IP 的 **1884** 端口。

本地编译：`powershell -ExecutionPolicy Bypass -File tools/build-exe.ps1`，输出 `build/dist/AgentDeck.exe`。构建机器需 Rust/MSVC 与 Mosquitto（默认 `C:\Program Files\mosquitto`，可用 `-MosquittoDirectory` 指定）。已有配置在首次运行时从 EXE 所在目录及上级项目目录迁移，不会打入发布包。运行配置在 `%LOCALAPPDATA%\AgentDeck\bridge\config.json`，日志在同目录 `.state` 中。

GitHub Actions 在 push / PR 时编译固件、运行 UI 和 Rust 测试，并打包、启动验证 EXE 和进程恢复。Actions 的 `AgentDeck-Windows-x64` 产物包含单 EXE；推送 `v*` 标签后自动发布 Release，附带 EXE 与固件文件。EXE 不会自动烧录设备。

升级前在任务管理器结束 AgentDeck 及其 PowerShell、Bridge/Mosquitto 子进程，再运行新版。取消登录自启的命令见下方。

当前 Windows 本机使用 `bridge/mosquitto.local.conf` 的 1884 端口。已有固件与配置时，执行一次 `powershell -ExecutionPolicy Bypass -File tools/install-background.ps1` 安装当前用户登录自启。后台守护每 5 秒检查 MQTT Broker 和 Bridge，退出后自动启动，无需保留终端。网络中断由固件和 Bridge 自动重连。日志位于 `bridge/.state/`。这台电脑需保持原有局域网地址，设备才能连接；可在路由器设置 DHCP 地址保留。

取消登录自启：`Remove-ItemProperty HKCU:\Software\Microsoft\Windows\CurrentVersion\Run -Name AgentDeckBackground`（当前后台进程仍会继续运行）。

在仓库根目录执行：

```powershell
python -m pip install platformio
python -m platformio run -e agentdeck

cargo build --release --manifest-path bridge/Cargo.toml
bridge/target/release/agentdeck-bridge configure --host YOUR_BROKER_IP --device agentdeck-01
bridge/target/release/agentdeck-bridge service --config bridge/config.json
```

新设备无需编辑或重新编译凭据。首次启动会显示 `BLUETOOTH SETUP`；在电脑运行下面的命令，按提示输入 Wi-Fi 和 MQTT 信息：

```powershell
bridge/target/release/agentdeck-bridge provision
```

设备保存配置后自动重启。需要更换网络时，开机期间按住 BACK，再运行同一命令。Wi-Fi 和 MQTT 密码在终端中隐藏输入，并只保存在 ESP32 NVS 和本机忽略提交的配置中。

BLE 写入的网络配置保存在设备 NVS；可选的 Secrets.h 和 bridge/config.json 已忽略，不提交凭据。Broker 账号通过 Bridge 环境变量 `AGENTDECK_MQTT_USER` / `AGENTDECK_MQTT_PASSWORD` 配置。Bridge 使用实际已有的 Broker，不自动部署生产服务。

Codex 额度通过本机 `codex app-server` 只读读取。app-server 初始化成功表示 Codex online；只有实际任务运行器或会话事件心跳才表示 Working。额度接口未登录或不可用时显示未知。
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

最新状态显示修复与 COM7 实机记录见 [实机状态验证](docs/hardware-status-validation.md)。串口 `q` 读取当前状态，`f` 导出 OLED 缓冲；仅按需调用，导出期间有串口开销。

```powershell
python -m pip install ziglang pillow
python tools/check.py
python tools/check_network.py
# 实际 TCP MQTT 本机集成测试，Python/amqtt/paho 仅作为测试 Broker 和客户端
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
bridge/           Rust PC 监控、Codex 额度、任务运行器、真实事件输入、命令处理
tests/           UI / 协议 / Bridge 测试（fixtures 仅限测试）
tools/            构建验证、配置生成、本机 MQTT 集成测试
```

原始设计保留在 AGENT.md。docs/architecture.md、docs/validation.md 中的 Phase 1 描述是历史记录，当前联网实现以本 README 与 communication.md 为准。
