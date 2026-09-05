# 实际通讯与部署（v0.2.0）

生产固件只使用实际 Wi-Fi / MQTT / NTP 输入。没有网络配置就保持离线；没有 Agent 心跳就保持离线；没有额度或 GPU 数据就显示 `--`。旧 MockService 已移至 `tests/fixtures/`，不会编入固件。现有页面布局、字体、动画、输入硬件和 33ms 调度保持原样，页面改动仅涉及实际数据绑定和状态文案。

## 1. 配置设备

从仓库根目录执行：

```powershell
Copy-Item src/config/Secrets.example.h src/config/Secrets.h
```

在 `Secrets.h` 中取消相应 `snprintf` 的注释，填写 SSID、密码、MQTT Host、用户名、密码和 OTA 密码。`deviceId` 默认 `agentdeck-01`；多台设备必须分别配置不同 ID。SSID 支持 32 字节，broker 主机名最多 63 字节。ESP32 必须连接 2.4GHz 网络；MQTT Host 填 Broker 的局域网地址，不能填 PC 上的 `127.0.0.1`。时区默认 `CST-8`（UTC+8），NTP 默认 `pool.ntp.org`，可改为实际可达的局域网 NTP。

没有创建 Secrets.h 仍可编译，设备启动提示 `CONFIGURE WIFI`；不会回退到模拟数据。Secrets.h 已加入 gitignore。

```powershell
python -m platformio run -e agentdeck
# 核对实际串口后，首次写入新分区表和固件
python -m platformio run -e agentdeck -t upload --upload-port COM7
```

固定目标是 ESP32-S3 N8 / 8MB Flash，双 OTA 分区每个 0x330000 字节。此次分区表由原来的默认分区切换为 `default_8MB.csv`，**首次升级应使用 USB / UART 全量烧录，不能直接用旧固件的 OTA 布局推送**。原生 USB 可使用 `agentdeck-usb`。接线没有改变。

## 2. 配置 PC Bridge

Bridge 使用已有 MQTT Broker；不会自动安装或启动生产 Broker。固件目前使用局域网 MQTT TCP + 可选用户名密码，没有 TLS；不要将该端口直接暴露到公网。Broker 应对设备与 Bridge 设置独立账号和 Topic ACL。Bridge 支持 `tls_ca` 连接 TLS Broker，但固件端仍需要受信任的局域网 listener。

```powershell
cargo build --release --manifest-path bridge/Cargo.toml
bridge/target/release/agentdeck-bridge configure --host 192.168.1.10 --device agentdeck-01
# 按 Broker 配置设置环境变量；不要把密码写进仓库
$env:AGENTDECK_MQTT_USER = 'your-user'
$env:AGENTDECK_MQTT_PASSWORD = 'your-password'
bridge/target/release/agentdeck-bridge service --config bridge/config.json
```

生成器只新建文件，不覆盖已有配置；它会为三种 Agent 配置本仓库实际运行器的 `stop` / `cancel` 处理程序。也可以手动复制 `bridge/config.example.json`，其默认 `handlers` 为空，所有控制动作会明确拒绝，直到接入实际处理程序。

保持 Bridge 进程运行。每个 `client_id` 只运行一个实例。需要长期后台运行时，可将上述命令加入 Windows 任务计划程序（工作目录设为仓库根目录并配置相同环境变量）。当前没有创建系统任务。

数据源：

| 内容 | 实际来源 | 无来源时 |
| --- | --- | --- |
| CPU / RAM | psutil 系统采样 | PC 离线或停止更新 |
| 下载速率 | 实际接收字节差值，十进制 kbit/s | 不生成随机流量 |
| GPU | 已安装 NVIDIA 驱动的 nvidia-smi，多卡取最高占用 | JSON null / 屏幕 `--` |
| Codex 5h / week | 当前用户已登录 Codex 的只读 App Server RPC | `available:false` |
| Agent online / working / task | 下面的真实任务运行器或实际事件流 | 离线 |
| 时钟 | ESP32 NTP / 本地系统时钟 | 首次同步前 `--:--` |

Codex 用量每 60 秒读取，最多保留 120 秒有效缓存；不读取 auth.json、不发送模型请求、不触发登录或购买。不把任意配额窗口硬当成 5h / week：只有官方窗口分别为 300 / 10080 分钟才映射，否则标记未知。app-server 初始化成功表示 Codex online；只有实际 runner 或事件心跳才表示 working。

## 3. 接入真实 Agent 状态

方式 A：用运行器包住实际的一次性 CLI 任务。`--task` 仅是屏幕任务标签，不会替你向 Agent 发送任务；`--` 后的命令由你实际决定。

```powershell
bridge/target/release/agentdeck-bridge run --agent codex --task "Review current changes" -- codex exec "Review current changes without editing files"
# 其他 CLI：保持相同结构，将 --agent 和 -- 后面的实际命令改为 claude 或 opencode
```

运行器每秒记录所启动进程的 PID、创建时间和存活状态；工作进程退出后发 offline，成功退出附带完成事件。它适用于一次性任务，不能用来把一个长期等待输入的 shell 当作一直 Working。每种 Agent 默认对应一个槽位，不要让多个生产者同时写同一个槽位。

方式 B：将你实际的 Codex / Claude / OpenCode 扩展、hook 或服务事件转换成下面的 JSON 行，持续写入 `feed`：

```powershell
your-real-event-adapter | bridge/target/release/agentdeck-bridge feed --agent claude
```

每行包含真实 `online`、`working`，可包含 `task`、`model`、`usage`、`completed_at`。feed 按接收时间增加 `ts`，原子写入状态文件。生产者必须每 1–5 秒提供当前实际状态/心跳；15 秒没更新就离线。`usage` 的字段见下表，其值必须来自提供商数据，不能估算或填样例。`completed_at` 是实际成功完成事件的 Unix 秒时间戳。

默认不会附着或接管已经打开的 IDE 会话。要监控其真实任务与审批，需要把该会话自己的事件/控制接口接到此输入和 handler；仅启动 Bridge 不会凭空获得 Claude / OpenCode 的账户额度。中文文本按 UTF-8 字节安全截断，但当前 OLED 字库是否显示对应汉字仍受现有字体限制。

## 4. 双向控制和回执

菜单统一通过旋转编码器选择、按下 CONFIRM 进入；短按旋钮 PUSH 不进入。Timer 通过 CONFIRM 开始/暂停，BACK 返回菜单。

Agent 详情中只有 CONFIRM 发送人工确认 `confirm`，短按 PUSH 不发送确认；BACK 发送 `cancel` 并保留返回上级的导航，长按 PUSH 发送 `stop`。待机唤醒动作仍然只唤醒，不发送命令。HOME / PC 长按不再更改数据。

默认生成配置中的 `stop/cancel` 只终止本运行器启动且 PID + 创建时间仍匹配的实际进程树。`confirm/approve/reject/pause/resume/retry` 需要对应 Agent 的真实控制接口；没有 handler 返回 `rejected/unsupported_action`。不会用模拟键盘输入替代审批接口。

自定义 handler 写在 `handlers.<agent>.<action>`，值为固定 argv 数组。Bridge 使用 `shell=False`，通过 stdin 传入已验证的命令 JSON，附带 `agent`。退出码 0 表示 handler 确认操作成功，其他值表示失败。handler 应在 5 秒内完成、正确释放子进程，并且只在实际控制接口确认成功后返回 0。可执行文件、参数不能由 MQTT 消息指定。

`COMMAND SENT` 只表示写出 MQTT，`COMMAND COMPLETE` 表示匹配 ID 的实际 handler 成功回执。未收到回执超过 12 秒提示 `COMMAND TIMEOUT`；不能据此断言远端没有执行。为避免重复副作用，固件不自动重发。Bridge 将命令 ID 持久保留 24 小时，QoS1 重复投递及 Bridge 重启后不重复执行；执行期间崩溃的记录保持未知并拒绝再执行。

命令须包含 ID、允许的 `device_id`、实际 action、`ts`、`expires_at`，默认有效期 10 秒。retained 命令被 Bridge 拒绝；断线队列不在重连后重放。固件控制请求和远程配置需要先完成 NTP 同步。

## 5. Topic / JSON 契约

目前维持原设计全局 Agent / PC Topic，一套 Broker 上这些 Topic 应由一个 Bridge 管理。多个 PC/同种 Agent 并存时应通过独立 Broker 或后续统一增加 namespace；不能让多个 Bridge 互相覆盖状态。结构预留三个 Agent ID 和设备命名空间，未实现 Home Assistant Discovery。

| Topic | 方向 | payload / 约束 |
| --- | --- | --- |
| `agentdeck/{id}/status` | ESP → Broker | 文本 `online` / `offline`，retained，LWT offline |
| `agent/{codex,claude,opencode}/status` | Bridge → ESP | `online:bool, working:bool, model?:string, task?:string, ts:uint, completed_at?:uint` |
| `agent/{name}/usage` | Bridge → ESP | `five_hour:0..100, weekly:0..100, five_hour_reset:uint, weekly_reset:uint, ts:uint`，未知为 `available:false, ts:uint` |
| `agent/{name}/task` | 外部生产者 → ESP | `task:string, ts:uint`，不延长 online 心跳 |
| `pc/status` | Bridge → ESP | `online:bool, cpu:0..100, ram:0..100, gpu:null或0..100, down_kbps:uint, ts:uint`；offline 只需 online/ts |
| `agent/{name}/command` | ESP → Bridge | `id:string, device_id:string, action:string, ts:uint, expires_at:uint`，不 retained |
| `agentdeck/{id}/input` | ESP → Broker | 上述控制手势记录，不含本地菜单旋转等输入，不 retained |
| `agentdeck/{id}/ack` | Bridge → ESP | `id:string, status:completed/rejected/failed, reason:string, ts:uint`，不 retained |
| `agentdeck/{id}/telemetry` | ESP → Broker | 每 5 秒：ts、uptime_ms、heap、min_heap、rssi、firmware、ota_ready、time_synced |
| `agentdeck/{id}/command` | 外部 → ESP | `action:"notify", text:string, ts:uint, expires_at:uint` |
| `agentdeck/{id}/config` | 外部 → ESP | `brightness?:16..255, motion?:0..2, ts:uint, expires_at:uint`，RAM 中应用 |

百分比是**已用**额度；reset 是发送时剩余秒数，收到后按本地 millis 差值递减。状态消息每 1–5 秒发送，**不 retained**；15 秒后数据过期。PC LWT offline retained，设备接受连接时生成的旧 offline 时间戳，避免运行数小时后的遗嘱被误判为过期。NTP 同步后，其他消息须有 15 秒内时间戳（最多允许未来 5 秒）；同步前接收时间用于 freshness，所以部署必须遵守状态消息不 retained 的约定。

MQTT payload 上限 1024 字节；model 最多 31 字节，task 79 字节，notify 21 字节；非法类型、超范围、过长或不完整消息整条拒绝，不会部分覆盖 Model。Bridge 发布遥测和回执采用 QoS0，设备订阅请求 QoS1，实际送达级别取发布级别；丢失状态由下一次心跳修复，控制结果由业务 ACK/超时判定。

远程 config 是幂等设值，notify 是短期通知，均不得 retained；PubSubClient 回调不暴露 retained 标志，ESP 端依靠时间戳/有效期拒绝旧消息，不提供 exactly-once 通知语义。设置暂不持久化；网络凭据只在 Secrets.h 中配置。

## 6. NTP、OTA 与 UI 隔离

网络任务固定在 Core 0，UI/Input 留在 Arduino 主循环。TCP 连接超时 1 秒、MQTT socket 超时 1 秒，DNS/Wi-Fi/OTA 均不在 UI 线程执行。Wi-Fi 单次关联最多 10 秒，随后指数退避 1/2/4/8/16/32/60 秒；MQTT 独立退避。FreeRTOS 单槽 snapshot 队列传 POD 拷贝，输入命令与通知各为固定 8 项队列；主循环只做零等待队列操作，不共享可变 Model，也不在 MQTT callback 绘图。

NTP 首次成功后，断网继续由 ESP32 系统时钟计时；重启后仍需重新同步。Focus Timer 继续使用原有 millis，与网络和墙上时钟无关。

OTA 只有配置非空密码并连上 Wi-Fi 才启用，端口默认 3232。首次正确分区烧录后，可用随固定 Arduino ESP32 工具链提供的 espota.py 推送 `.pio/build/agentdeck/firmware.bin`：

```powershell
python "$env:USERPROFILE/.platformio/packages/framework-arduinoespressif32/tools/espota.py" -i DEVICE_IP -p 3232 -a YOUR_OTA_PASSWORD -f .pio/build/agentdeck/firmware.bin
```

OTA 处理在网络任务，成功后按 ArduinoOTA 默认行为重启；失败提示 `OTA FAILED`。Flash 擦写可能造成硬件层面的短暂停顿，不能仅凭双核任务隔离宣称升级期间也保证 30FPS。实际 Wi-Fi 故障输入手感、OLED 帧率、OTA 成功/失败/断电恢复和数小时 heap 稳定性仍需真机验收。

## 接口依据

- [PubSubClient 2.8 API](https://pubsubclient.knolleary.net/api)：MQTT buffer、socket timeout、LWT 与发布订阅接口。
- [Espressif ArduinoOTA](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/ArduinoOTA/examples/BasicOTA/BasicOTA.ino)：固定版本 OTA 接口。
- [rumqttc](https://docs.rs/rumqttc/)：Rust MQTT 客户端及自动重连。
- [Codex App Server](https://learn.chatgpt.com/docs/app-server)：initialize / initialized、account/rateLimits/read、配额窗口及 reset 时间。未使用文档中的任何示例配额作为生产数据。
