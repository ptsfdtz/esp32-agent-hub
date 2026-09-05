你现在负责继续开发我的 ESP32-S3 桌面 Agent IoT 控制终端。

这是一个真正长期使用的产品，不是 Arduino Demo。

核心目标：

1. 1.3" 128x64 SH1106 OLED
2. 极其丝滑的 UI 动画
3. 旋转编码器交互
4. BACK / CONFIRM / PUSH
5. Wi-Fi 物联网
6. MQTT 双向通信
7. Agent 用量与运行状态
8. PC 状态
9. OTA 升级
10. 后续可以接入 Home Assistant / 多 Agent

==================================================
一、硬件固定，不得修改
==================================================

主控：
ESP32-S3

OLED：
1.3"
128x64
SH1106
I2C
U8g2

OLED 驱动：

U8G2_SH1106_128X64_NONAME_F_HW_I2C

接线：

CON  -> GPIO15
SDA  -> GPIO8
SCL  -> GPIO9
PSH  -> GPIO6
TRA  -> GPIO4
TRS  -> GPIO5
BAK  -> GPIO7
GND  -> GND
VCC  -> 3.3V

含义：

CON = CONFIRM
PSH = 旋钮按下
TRA/TRS = 编码器
BAK = BACK

==================================================
二、产品定位
==================================================

产品名称暂定：

Agent Deck

定位：

ESP32-S3 桌面 IoT Agent Control Center

用户不用打开网页，就能：

- 查看 Codex 用量
- 查看 Agent 是否工作
- 查看当前任务
- 查看 PC 状态
- 查看网络状态
- 控制 Agent
- 接收 Agent 完成通知
- 通过 MQTT 与其他设备/电脑通信

==================================================
三、动画是核心需求
==================================================

UI 必须非常丝滑。

禁止：

- 页面瞬间硬切
- 菜单文字突然跳动
- 使用 delay() 做动画
- 操作时卡顿
- 网络请求导致 UI 停顿

动画目标：

正常目标：

30 FPS

如果性能允许可以达到：

40~50 FPS

但是稳定 30 FPS 比不稳定 60 FPS 更重要。

OLED I2C 设置：

Wire.setClock(400000);

如果经过实际测试确认 OLED 稳定，可以评估更高 I2C Clock，
但默认使用 400kHz。

==================================================
四、动画系统
==================================================

不要在每个页面里分别手写动画。

必须实现统一 Animation Engine。

例如：

class Animation {
public:
    float value;
    float start;
    float target;

    uint32_t startTime;
    uint32_t duration;

    void setTarget(float target, uint32_t duration);
    void update();
    bool running();
};

至少支持：

linear
easeOutCubic
easeInOutCubic

默认 UI 动画推荐：

150~250ms

推荐：

菜单移动：
150ms

页面切换：
200ms

弹窗：
180ms

进度条：
300~500ms

数字变化：
200~300ms

==================================================
五、必须实现的动画
==================================================

1. 页面滑动

例如：

HOME -> AGENT

旧页面：

向左滑出

新页面：

从右侧滑入

不是直接切换。

--------------------------------

2. 菜单选择动画

例如：

> Agent
  PC
  Timer
  Settings

旋钮旋转以后：

选择框平滑移动到下一项。

不能瞬间跳过去。

使用：

currentY
targetY

进行插值。

--------------------------------

3. 滚动菜单

菜单项目超过屏幕显示范围以后：

实现平滑滚动。

不能突然重新排列。

--------------------------------

4. Progress Bar

例如：

5H   72%

████████░░

如果 72% 更新为 80%：

进度条应该从 72% 平滑增长到 80%。

不能瞬间跳变。

--------------------------------

5. 数字动画

例如：

72%

变化成：

75%

可以实现 Tween。

--------------------------------

6. 状态动画

Working：

● Working

允许做轻微呼吸效果。

但是：

动画必须克制。

不要把 OLED 做成游戏机一样乱闪。

==================================================
六、动画设计语言
==================================================

整体设计风格：

极简
黑白
Terminal
Cyber
Engineering

但不要：

大量装饰
复杂边框
大量图标
花哨动画

应该类似：

现代智能硬件
Apple / Nothing / Teenage Engineering
的极简交互思路。

动画的目的：

帮助用户理解页面层级。

不是为了炫技。

==================================================
七、OLED 渲染架构
==================================================

建立：

Renderer
AnimationManager
ScreenManager

不要让每个 Screen 自己随便刷新 OLED。

建议：

Input
  ↓
UI State
  ↓
Animation
  ↓
Renderer
  ↓
U8g2
  ↓
OLED

主循环：

loop() {

    input.update();

    network.update();

    mqtt.update();

    animation.update();

    ui.update();

    renderer.render();

}

全部非阻塞。

禁止大段 delay()。

==================================================
八、帧率控制
==================================================

实现统一 Frame Scheduler。

例如：

constexpr uint32_t FRAME_INTERVAL = 33;

约：

30 FPS

只有以下情况需要持续刷新：

- 动画正在运行
- 页面数据变化
- 倒计时变化
- 用户输入

静止页面可以降低刷新频率。

避免无意义持续刷 OLED。

==================================================
九、物联网架构
==================================================

ESP32-S3 必须使用 Wi-Fi。

通信设计：

               Internet / LAN
                     │
                     │
            ┌────────▼────────┐
            │   MQTT Broker   │
            └────────┬────────┘
                     │
          ┌──────────┴──────────┐
          │                     │
      ESP32-S3              PC Agent
          │                     │
        OLED                 Codex
                            Claude
                            OpenCode

ESP32 不需要直接登录 ChatGPT。

PC Agent 负责收集真正的 Agent 数据。

ESP32 负责：

显示
控制
交互
IoT 通信

==================================================
十、MQTT
==================================================

MQTT 是核心通信协议。

建议 Topic：

agentdeck/{deviceId}/status

agentdeck/{deviceId}/command

agentdeck/{deviceId}/input

agentdeck/{deviceId}/config

agentdeck/{deviceId}/telemetry

Agent：

agent/codex/status

agent/codex/usage

agent/codex/task

pc/status

==================================================
十一、MQTT Agent 数据
==================================================

例如：

agent/codex/status

{
    "online": true,
    "working": true,
    "model": "gpt-5.x",
    "task": "Implement OLED UI"
}

agent/codex/usage

{
    "five_hour": 72,
    "weekly": 41,
    "five_hour_reset": 12360,
    "weekly_reset": 240000
}

ESP32 收到 MQTT 消息后：

只更新 Model 数据。

UI 自己根据 Model 变化产生动画。

MQTT callback 中禁止直接绘制 OLED。

==================================================
十二、ESP32 控制 Agent
==================================================

按钮操作允许通过 MQTT 发出命令。

例如：

CONFIRM：

agent/codex/command

{
    "action": "confirm"
}

BACK：

{
    "action": "cancel"
}

长按 PUSH：

{
    "action": "stop"
}

后续可以支持：

pause
resume
approve
reject
stop
retry

==================================================
十三、MQTT 在线状态
==================================================

使用：

Last Will and Testament

设备上线：

agentdeck/{deviceId}/status

online

掉线：

offline

使用 retained message。

==================================================
十四、断线恢复
==================================================

必须正确处理：

WiFi 断开
MQTT 断开
PC 离线
Agent 离线

禁止 UI 卡死。

采用：

非阻塞 reconnect

指数退避：

1s
2s
4s
8s
...
最大约 30~60s

UI 永远继续运行。

例如：

CODEX

OFFLINE

Last update
02:13 ago

==================================================
十五、NTP
==================================================

ESP32 使用 NTP 获取时间。

用于：

时钟
Agent Reset 时间
Timer
数据时间戳

断网后继续使用本地时间。

首页可以显示：

09:42

CODEX ●

5H   72%
███████░░

WEEK 41%
████░░░░░░

==================================================
十六、OTA
==================================================

必须支持 OTA 固件升级。

以后设备放在桌子上以后：

不应该每次升级都插 USB。

至少支持：

ArduinoOTA

或者设计 HTTP OTA。

SETTINGS 页面：

Firmware
v0.1.0

OTA
READY

以后 PC 可以推送更新。

==================================================
十七、WiFi 配置
==================================================

第一版允许：

Config.h

配置：

SSID
Password
MQTT Host
MQTT Port

但是架构必须允许以后增加：

WiFi Provisioning

例如设备第一次启动：

AgentDeck-XXXX

创建配置热点。

手机连接以后配置 Wi-Fi。

==================================================
十八、Home Assistant
==================================================

预留 Home Assistant MQTT Discovery。

未来可以自动出现：

Agent Deck

实体例如：

Codex Usage
Codex Status
Codex Weekly Usage
PC Status
Agent Running

并且允许：

Home Assistant
        ↓
MQTT
        ↓
Agent Deck

第一版本可以不实现 HA Discovery，
但是 Topic 和 Model 结构不能阻止未来实现。

==================================================
十九、页面设计
==================================================

一级页面：

HOME
AGENT
PC
IOT
TIMER
SETTINGS

==================================================
二十、HOME
==================================================

示意：

09:42       ●

CODEX

5H      72%
████████░░

WK      41%
████░░░░░░

页面进入时：

标题淡入/滑入。

进度条：

平滑增长。

==================================================
二十一、AGENT
==================================================

例如：

AGENTS

> Codex       ●
  Claude      ○
  OpenCode    ○

选择框：

平滑上下移动。

进入 Agent：

页面 Slide Transition。

==================================================
二十二、PC
==================================================

通过 MQTT：

pc/status

例如：

CPU  32%
RAM  61%
GPU  47%
NET  ↓12M

数据变化时平滑更新。

==================================================
二十三、IOT 页面
==================================================

新增：

IOT

显示：

WiFi
MQTT
PC
Cloud

例如：

IOT STATUS

WiFi   ●
MQTT   ●
PC     ●
Agent  ●

旋钮进入详情：

SSID
IP
RSSI
MQTT Host
Latency

==================================================
二十四、Timer
==================================================

FOCUS

   24:36

  RUNNING

旋钮调整。

CONFIRM 开始/暂停。

使用 millis()。

禁止 delay()。

==================================================
二十五、Settings
==================================================

> WiFi
  MQTT
  Brightness
  Animation
  Firmware
  About

Animation：

可以：

FULL
REDUCED
OFF

FULL 默认。

Brightness：

调 OLED Contrast。

==================================================
二十六、输入系统
==================================================

统一：

enum class InputEvent {

    NONE,

    ROTATE_LEFT,
    ROTATE_RIGHT,

    PUSH,
    PUSH_LONG,

    BACK,
    BACK_LONG,

    CONFIRM,
    CONFIRM_LONG

};

Encoder 必须使用 Quadrature State Machine。

不能再次出现：

RIGHT
LEFT
RIGHT
LEFT

这种误判。

==================================================
二十七、Model 层
==================================================

建议：

struct AgentStatus {

    String name;

    bool online;
    bool working;

    uint8_t shortUsage;
    uint8_t weeklyUsage;

    uint32_t shortReset;
    uint32_t weeklyReset;

    String model;
    String task;

    uint32_t lastUpdate;
};

另外：

struct NetworkStatus
struct PcStatus
struct DeviceStatus

UI 禁止直接解析 JSON。

网络层解析 JSON 后更新 Model。

==================================================
二十八、建议目录
==================================================

src/

main.cpp

hardware/
    Display.cpp
    Encoder.cpp
    Buttons.cpp

ui/
    UIManager.cpp
    Renderer.cpp
    Animation.cpp
    ScreenTransition.cpp

screens/
    HomeScreen.cpp
    AgentScreen.cpp
    PcScreen.cpp
    IotScreen.cpp
    TimerScreen.cpp
    SettingsScreen.cpp

network/
    WiFiService.cpp
    MQTTService.cpp
    NTPService.cpp
    OTAService.cpp

models/
    AgentStatus.h
    PcStatus.h
    NetworkStatus.h

config/
    Config.h

==================================================
二十九、性能原则
==================================================

ESP32-S3 性能足够。

不要为了节省几十字节代码而破坏架构。

但是：

避免频繁 String 动态分配。

对于频繁更新数据优先考虑：

char[]
固定结构
预分配 buffer

避免长期运行后的 Heap Fragmentation。

==================================================
三十、网络线程不能影响动画
==================================================

这是强制要求。

即使：

MQTT Broker 不存在
PC 关机
WiFi 很差
HTTP 超时

旋钮操作都必须保持丝滑。

任何网络行为不得造成明显 UI 卡顿。

必要时可以考虑：

FreeRTOS Task

例如：

Core / Task A
UI + Input

Core / Task B
WiFi + MQTT

但是：

只有实际需要时再引入，
不要一开始无意义增加复杂度。

==================================================
三十一、第一阶段暂时使用 Mock
==================================================

Mock：

Codex

online = true
working = true

5H:
72%

Weekly:
41%

Reset:
03:26

PC:

CPU 32
RAM 61
GPU 47

Network:

WiFi = online
MQTT = online

==================================================
三十二、第一阶段必须完成
==================================================

先不要实现真实 Codex 数据采集。

先完成：

1. Hardware Driver
2. Encoder
3. Buttons
4. Display
5. InputEvent
6. Animation Engine
7. UI State Machine
8. Screen Transition
9. HOME
10. AGENT
11. PC
12. IOT
13. TIMER
14. SETTINGS
15. Mock Data

我要先在真实 ESP32-S3 上感受：

旋钮手感
菜单动画
页面动画
UI 帧率

如果动画不够顺滑：

先优化动画。

不要急着开发后面的网络功能。

==================================================
三十三、第二阶段
==================================================

完成：

WiFi
NTP
MQTT

并保持 UI 动画完全不卡顿。

==================================================
三十四、第三阶段
==================================================

完成：

PC Agent Bridge

PC 后台程序负责：

Codex
Claude
OpenCode
系统监控

然后统一发送 MQTT。

==================================================
三十五、验收标准
==================================================

最重要：

旋钮快速旋转时：

UI 不乱跳。

连续快速操作：

无明显卡顿。

页面切换：

有连续平滑动画。

动画目标：

稳定约 30 FPS。

MQTT 断线：

UI 不冻结。

WiFi 断线：

Timer / Menu 等本地功能继续正常运行。

Broker 恢复：

自动重连。

设备无需重启。

连续运行：

至少数小时无明显内存泄漏。

==================================================
三十六、开发原则
==================================================

不要把它做成 Demo。

要按一个真正可以长期放在桌面上使用的 IoT 产品开发。

优先级：

1. 输入手感
2. UI 丝滑度
3. 稳定性
4. UI 设计
5. MQTT
6. Agent 数据
7. 扩展能力

现在先：

审查现有工程。

然后告诉我：

1. 现有代码有什么问题
2. 你准备如何设计 Animation Engine
3. 你准备如何实现 30FPS 渲染
4. 如何保证 MQTT 不阻塞 UI
5. 工程目录结构
6. 第一阶段修改计划

然后直接开始实现 Phase 1。

不要只给建议，要实际修改代码、编译并修复错误。