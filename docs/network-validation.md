# v0.2.0 软件与通讯验证

日期：2026-09-05。原始 AGENT.md 保持原样；生产路径没有 Mock 数据源、模拟时钟或模拟按钮动作。

## 已完成

| 验证 | 结果 |
| --- | --- |
| `python -m platformio run -e agentdeck -e agentdeck-usb` | 两个 ESP32-S3 目标均编译成功 |
| `python tools/check.py` | 4,331 项 CHECK 通过，另有实际 framebuffer 一致性 assert；断言显式启用，不依赖编译器 NDEBUG 默认值 |
| `python tools/check_network.py` | 生产 C++ JSON 解析/退避测试通过，14 项 Python 数据源/命令测试通过 |
| `python tools/check_mqtt.py` | 真实本机 TCP Broker 端到端测试通过 |
| 本机 `CodexUsage(['codex','app-server'])._query()` | 真实只读调用成功，返回可用的 5h / week 额度窗口；未保存账户凭据或原始响应 |
| `python tools/configure_bridge.py ... --output build/test-bridge-config.json` | 本地配置生成成功，未覆盖用户配置 |
| `git diff --check` | 无空白错误 |

UI 回归覆盖原有四相输入、抖动、长按互斥、快速输入、Tween 中断、millis 回绕、Timer、菜单、切页首帧连续性、partial tile 传输、静止停刷与伙伴动作；新增真实命令请求路由和六种网络数据空缺/错误状态。没有修改 Renderer、Animation、FrameScheduler、Typography 或硬件接线。

菜单输入调整后的回归还验证了：CONFIRM 进入选中项并启动/暂停 Timer；短按 PUSH 在各导航页面不切页、不启动 Timer；Agent 详情只有 CONFIRM 发出人工确认；BACK 返回正常。

已实际查看原版 U8g2 输出的页面合成图，新增 startup offline、usage unknown、GPU unknown、Agent usage unknown、Wi-Fi disconnected、command rejected 文案没有溢出或改变布局。`build/preview/contact-sheet.png`、对应 128×64 PNG/PBM 和 buddy-motion.gif 可由 check.py 重新生成。预览中旧的固定样本只属于测试 fixture，不是固件数据源。

C++ 协议测试覆盖：完整状态更新、非法类型/长度/范围的原子拒绝、未知 GPU/额度、过期心跳、旧时间戳 offline 遗嘱、任务完成时间戳、非法 Topic、消息上限、跨 millis 回绕的指数退避和最大间隔。Bridge 测试覆盖：真实 OS 采样、UTF-8 边界、数据源失效、PID 不匹配、额度窗口映射、handler 回执、离线/不支持/过期拒绝、retained 拒绝、命令去重及 SQLite 跨重启去重。

MQTT 集成测试会在临时 127.0.0.1 端口启动实际 Broker 和 Bridge，采集当前 PC 指标，经 TCP 发布/订阅后送进生产 C++ 解析器。测试实际调用本地 handler，验证重复命令只执行一次；强制关闭 Broker 进程后重启，验证自动重连；最后经 MQTT stop 指令终止由测试创建的实际子进程，并检查 offline 和正常关闭。测试不访问用户的生产 Broker，不停止既有 Agent 任务。

## 尚需实际设备环境验收

本次未获得实际 Wi-Fi / Broker 配置，因此没有烧录或连接用户的真实 ESP32，也没有修改 Wi-Fi、Broker、防火墙或任务计划程序。

1. 使用实际 Secrets.h 和新分区全量烧录，验证 Wi-Fi 连接、真实 RSSI/IP、NTP 同步与断网后本地时间继续运行。
2. 断开 AP、关闭 Broker、恢复 Broker；快速旋转编码器并运行 Timer，确认 UI 不冻结、输入不溢出、自动恢复无须重启。
3. 在设备端完成真实 Agent 状态/用量显示和按钮回执闭环；Claude / OpenCode 的已有 IDE 会话须接上其实际事件和控制接口。
4. 设置 OTA 密码，验证实际升级、认证失败、传输中断、重连和固件重启。尚未验证硬件 OTA 或断电恢复。
5. 连续运行数小时，多次记录串口 s 与 telemetry 的 heap/min_heap、frame/render_us/max_us/over_budget/input_overflow。当前编译和主机测试不能证明实际 OLED 稳定 30FPS，也不能证明 Flash 擦写期间没有短暂停顿。

配置、协议、明确支持的控制动作和部署步骤见 [communication.md](communication.md)。
