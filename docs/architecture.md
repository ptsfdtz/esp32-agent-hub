# Agent Deck 工程审查与设计

## 原工程问题

原始 `hardware/hardware.ino` 保留作为接线测试资料，不参与 PlatformIO 产品固件构建。

- 编码器已有四相表，但 `sendBuffer()` 期间停止轮询，快速旋转可能漏边沿；非法跨相没有清除累计值。
- 三个按键只有电平边沿判断，没有消抖、短长按互斥和统一事件。
- OLED 每 50ms 全屏刷新，目标只有 20 FPS；静止画面也一直传输。
- 没有明确设置 400kHz，启动有 1s delay 和全地址 I2C 扫描。
- 硬件、业务状态和绘制混合在单一草稿，缺少产品页面、动画、数据模型和可复现构建。
- 没有测试、帧耗时、输入溢出和内存观察手段；README 为空。

## Phase 1 实现

数据流：`GPIO → InputEvent → ScreenManager → AnimationManager → Renderer → U8g2 → SH1106`。
MockService 只修改固定大小 Model；页面不解析 JSON，不读取 GPIO，不发起传输。

`Animation` 接收显式单调时间，支持 Linear / EaseOutCubic / EaseInOutCubic。
中途改变 target 时先计算当前值，再以它为起点；同一目标不会不断重启动画。
菜单位置、视窗滚动 150ms；页面 200ms；标题 200ms；通知 180ms；数字 250ms；条形进度 400ms。
FULL 使用全部动效，REDUCED 取消位移、保留 100ms 数据过渡，OFF 立即同步到目标。
工作状态仅用 2–3px 点的缓慢变化，不做整屏闪烁。单色 OLED 不伪造灰阶淡入。

页面切换先保存最后实际发送的 1024 字节合成画面，再向相应方向滑出。
新页面在另一侧滑入；动画被打断时再次取实际画面，因此连续操作没有首帧硬跳。
SH1106 的 128 列 × 8 tile rows 可以按字节移动，三个固定合成 buffer 共 3072 字节。
离开的画面在 200ms 转场中冻结，Timer 与 Model 本身继续更新。

菜单以逻辑选中项和动画位置分离；越界钳制，不循环首尾跳跃。超过三项后平滑滚动。
反白背景和文字用 XOR 叠加，选框经过文字时保持可读；菜单区域裁剪在标题下方。

## 30 FPS 调度及限制

FrameScheduler 限制两次发送起点至少相隔 33ms，不积累补帧债务。
动画进行、输入、数据变更才置 dirty；时钟、过期状态、Timer 以所需节拍刷新。
静止菜单在没有动效时停止刷新；FULL 的 Working 标记最多触发 2Hz 更新。
字体/伙伴更新后，FULL 模式小机器人约每 6 秒短暂眨眼，首页 20 秒闲置后进入待机面板。
角色视线、呼吸、眼睑和面板位移均复用 AnimationManager；REDUCED/OFF 下保留静态角色。

400kHz 下 1024 字节加 ACK 的理论线速时间约 23ms，还要加命令、分包和绘制开销。
因此不能声称 30 FPS 已达到，也不应在默认总线上承诺 40–50 FPS。
Phase 1 仍使用同步 U8g2 I2C 发送；所谓非阻塞是没有业务 delay/等待循环，物理传输本身有耗时。
编码器中断在传输期间继续入队，按键在 loop 中消抖。极短按键脉冲可能落在发送窗口内，需要真机验证。

固件记录 last/max render us、超过 33ms 的帧数、总帧数、输入队列溢出、当前和最小 heap。
超预算计数衡量单帧耗时，不等于整段交互的 FPS；实测还需比较连续动画期间的帧数/时间。
网络尚未接入，本阶段不声称完成 MQTT 断线测试或数小时硬件稳定性验收。

## 输入

两路 CHANGE 中断采集四相状态，合法反弹互相抵消，非法跨相清空半步。
默认每四个边沿产生一个 detent；方向由 Config 中的符号决定，不改接线。
64 项固定环形队列可容纳 63 个旋转事件；满时丢弃最新事件并记录计数。
临界区只保护采样和队列，不绘图、不打印、不分配堆内存。
当前 Arduino core 使用普通 GPIO ISR；未来接入 Flash 写入/OTA 时需重新评估 IRAM 完整调用链和输入采集。
按键消抖 20ms，长按 700ms，短按在稳定释放时产生，长按后释放不会再产生短按。

## Phase 2：网络与 UI 隔离方案（尚未实现）

优先选择能够将 DNS/TCP/MQTT 连接分步推进的异步 API。
不能仅在 loop 中每秒调用一次同步 `connect()` 就称为非阻塞。
若选用的库存在同步连接或超时等待，使用独立低优先级网络任务，UI 任务不调用这些函数。
任务间用固定容量数据快照/命令队列交接，禁止共享可变 String 和跨任务直接绘制。
传感/用量更新可按实体合并为最新值；用户命令不能静默覆盖，队列满时显示发送失败。

Wi-Fi 与 MQTT 各自维护 1、2、4、8…最大 60s 的退避时间，增加少量抖动；成功连接后复位。
连接、订阅、心跳、NTP 都以状态机推进；网络任务不能持有 UI 绘制所需的锁。
Broker 不可达、DNS 超时、PC 离线时 Timer 和输入继续运行。
MQTT callback 只验证长度、类型、范围和实体 ID，复制固定结构到队列。
UI 所在线程消费快照并更新 Model revision，由 Tween 产生变化。

设备主题：

| Topic | 用途 |
| --- | --- |
| `agentdeck/{deviceId}/status` | retained online；LWT retained offline |
| `agentdeck/{deviceId}/command` | 设备命令入口 |
| `agentdeck/{deviceId}/input` | 物理输入事件 |
| `agentdeck/{deviceId}/config` | 受验证的设置 |
| `agentdeck/{deviceId}/telemetry` | 帧率、运行时间、内存等 |
| `agent/{agentId}/status` | 在线、工作状态、模型 |
| `agent/{agentId}/usage` | 短期/周用量及 reset |
| `agent/{agentId}/task` | 当前任务 |
| `agent/{agentId}/command` | confirm / cancel / stop 等 |
| `pc/status` | PC 在线与资源使用 |

命令不 retained，增加请求 ID、有效期及 ACK，防止离线重连后执行旧 stop/approve。
Home Assistant Discovery 后续引用稳定 deviceId/agentId 和 availability topic，不让 UI 页码成为实体 ID。
当前 Model 的 `lastUpdate` 用于本地单调时间过期判断；reset 的示例倒计时是 Mock。
真实网络层应把 reset 转为明确的 UTC 截止时间，同时保留单调到期时间，不能混用 Unix timestamp 与 millis。
NTP 同步后显示真实时间，断网使用系统时钟；Timer 始终只依赖单调时间。
OTA 在网络任务内设计、鉴权、测试；未启用时设置页明确显示 `OTA NOT ENABLED`。

## 阶段顺序

1. Phase 1：硬件、事件、六页、统一动效、Mock、编译、主机测试与像素检查。
2. 真机：旋钮方向/detent、快转、短长按、30 FPS、OLED 总线稳定、至少数小时运行。先优化输入和动画。
3. Phase 2：Wi-Fi、NTP、MQTT、断线恢复，重新执行交互回归；再接入 OTA。
4. Phase 3：PC Bridge 和真实 Agent 数据，随后 HA / 多 Agent 扩展。

API 参考：[U8g2 官方参考](https://github.com/olikraus/u8g2/wiki/u8g2reference)、
[PlatformIO ESP32-S3 DevKitC 配置](https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html)。
