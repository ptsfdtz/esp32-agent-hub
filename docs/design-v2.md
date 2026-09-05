# Agent Deck：简洁、卡通的 OLED 界面

## 参考与取舍

阅读了以下五个项目的说明，并检查 MiaoUI 图标菜单、upiir 菜单与 Cozmo 眼睛的参考画面：

| 项目 | 本次吸收的设计方向 |
| --- | --- |
| [MiaoUI](https://github.com/JFeng-Z/MiaoUI) | 图标导航、列表层级、可中断非线性动画 |
| [Oled-menu-ESP32](https://github.com/JonathanBytes/Oled-menu-ESP32) | 横向图标排列、选中位置与滚动的连续反馈 |
| [esp32-eyes](https://github.com/playfultechnology/esp32-eyes) | 使用几何参数驱动眼睛、视线和眼睑 |
| [arduino_oled_menu](https://github.com/upiir/arduino_oled_menu) | 清晰选框、列表留白、侧边滚动位置 |
| [arduino_oled_animations](https://github.com/upiir/arduino_oled_animations) | 适合小屏的克制动画和图形尺度 |

采用自主绘制的六枚 24px 图标及双眼几何，没有复制参考仓库的源码/位图，也没有移植它们的硬件引脚、阻塞延时或独立刷新循环。

## 新界面

- Home：模拟时钟、Codex 表情与状态、两组小型用量条。角色不再出现在每页右上角。
- Explore：中央选中图标、两侧相邻图标；文字和位置指示跟随同一 Tween 连续移动；首尾不循环。
- Agents / Settings：三行列表、细圆角轮廓选框和右侧滚动条。文字背景擦除避免移动选框横穿字形。
- Computer：CPU/RAM/GPU 数值与小型用量条；Network 用标签和在线状态对齐。
- Focus：居中数字与简短运行状态；暂停可调整时间、完成通知和计时行为保留。
- 待机：20 秒进入大眼睛，45 秒进入闭眼状态；保留模拟时钟、用量与 mock 标识。一次输入只唤醒。

正文使用 Helvetica 8px 比例字体，标题采用同族粗体；辅助信息用 5×8，小屏计时数字保留 ProFont。固定数据位置，不依赖字符等宽排版。
表情的眼高、视线、眼睑和微笑均通过 AnimationManager 插值，避免闭眼时一帧切换。
Reduced / Off 仍可用；不强制用户观看装饰动画后才能操作。

## 刷新性能

保持 SH1106、GPIO8/9、400kHz 和 33ms 帧调度。
Renderer 比较当前 framebuffer 与上一张已发送的画面，按 8×8 tile 检测变化；每个 tile row 合并最左至最右的变化范围。
范围合计超过 80 tiles 时发送整屏，否则用 U8g2 UpdateDisplayArea 发送局部。
只有 Renderer 操作 OLED 传输，页面和 MQTT Model 更新不直接刷新屏幕。

主机测试捕获真实 U8g2 tile 回调，还原模拟 OLED RAM，并在每帧断言它与 framebuffer 完全相同；覆盖滑动、反向中断、清除旧内容、眼睛、待机与唤醒。
这能检查局部更新正确性，但不能替代实物 OLED 观感和高速旋钮操作测试。

预览：[页面](phase1-preview.png) · [动画](buddy-motion.gif)。
