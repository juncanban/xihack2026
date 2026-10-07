# 设计讨论记录

本文记录项目理解和后续 UI/UX、图形框架选型讨论。

## 一 项目运行链路

项目的核心链路是：

```text
Claude Code Hook
  -> Python hook_event.py
  -> UDP 127.0.0.1:9911
  -> Python vibe_pet_host.py
  -> USB 串口
  -> Wio Terminal 固件
  -> LCD 小苹果动画
```

Claude Code 在会话、提交用户消息、调用工具、等待批准和任务结束时触发 Hook 事件。`hook_event.py` 从标准输入读取事件 JSON，并将事件映射为项目状态：

| Claude Code 事件 | 项目状态 |
|---|---|
| `SessionStart` | `IDLE` |
| `UserPromptSubmit` | `THINKING` |
| `PreToolUse` | `TOOL` |
| `PostToolUse` | `THINKING` |
| `Notification` | `WAIT` |
| `Stop` | `DONE` |
| `SessionEnd` | `IDLE` |

UDP 是电脑本机进程之间使用的网络传输协议。项目使用 `127.0.0.1:9911`，表示只在本机传递，不访问互联网。宿主机是运行 Python 程序的 Windows 电脑；Wio Terminal 是被宿主机控制的实体设备。

串口报文例如：

```text
S:TOOL
```

其中 `S:` 是项目自定义的命令前缀，`TOOL` 是状态名。固件解析后把当前状态设置为 `ST_TOOL`，再根据状态和运行时间逐帧绘制动画。

## 二 技术框架

项目不是单一框架，而是由几部分组成：

| 部分 | 技术 |
|---|---|
| 工程管理、编译和烧录 | PlatformIO |
| Wio Terminal 固件框架 | Arduino Framework |
| 固件语言 | C++ |
| LCD 图形库 | TFT_eSPI、TFT_eSprite |
| 宿主机转发 | Python 标准库、pyserial |
| 本机通信 | UDP |
| 桌面挂件 | Tkinter、Pillow |
| 数据存储 | Wio Terminal Flash；可扩展到 SD 卡 |

PlatformIO 负责工程配置、依赖管理、编译、烧录和串口监视，不负责动画本身。动画和页面由 Arduino C++ 固件调用 `TFT_eSPI` 绘制。

## 三 屏幕和绘图方式

Wio Terminal 自带约 2.4 英寸彩色 TFT LCD，分辨率为 **320×240**。固件使用 RGB565 16 位颜色。底层屏幕通信由 `TFT_eSPI` 和 Wio Terminal 板级配置完成，业务代码不直接操作 SPI 引脚。

固件启动时执行：

```cpp
tft.begin();
tft.setRotation(3);
```

项目通过 `TFT_eSprite` 创建内存画布，先在内存中绘制，再使用 `pushSprite()` 一次性提交到 LCD，从而减少闪烁。当前共享的 320×240 RGB565 画布大约占用：

```text
320 × 240 × 2 字节 ≈ 153.6 KB
```

动画不是视频或 GIF，而是实时绘制。固件根据当前状态和 `millis()` 时间绘制背景、苹果身体、眼睛、嘴巴、手臂以及问号、火花、`Zzz` 等效果，主循环末尾约 `delay(30)`，理论刷新频率约 30 FPS。

主要绘图函数包括：

```text
drawBody()
drawEyes()
drawMouth()
drawHand()
drawPet()
```

图形驱动链路为：

```text
状态变量 gState
  -> drawPet(now)
  -> TFT_eSprite 内存画布
  -> TFT_eSPI
  -> Wio Terminal 内部 SPI
  -> 320×240 LCD
```

## 四 桌面挂件和宿主机

`fridge_magnet.py` 是不连接开发板时使用的电脑桌面小苹果。它使用 Tkinter 创建窗口，使用 Pillow 在 `160×120` 软件画布上绘制像素图，再以最近邻方式放大到 `320×240` 窗口中。桌面挂件不是 Wio Terminal 的实体屏幕，也不能替代固件页面和实体按键。

`vibe_pet_host.py` 是宿主机转发程序。它监听 UDP 9911，并通过 USB 串口把状态转发给 Wio Terminal。`fridge_magnet.py` 和 `vibe_pet_host.py` 默认都需要监听 UDP 9911，因此不能同时直接运行；要同时显示桌面挂件并驱动开发板，应使用 `fridge_magnet.py --serial COM8`，不要再启动另一个宿主机转发器。

## 五 LVGL 内存评估

Wio Terminal 使用 SAMD51，片上 SRAM 约 192 KB。LVGL 本身并不一定很大，主要内存压力来自控件对象、字体、图片、绘图缓冲和双缓冲。

当前项目已经有约 153.6 KB 的全屏 RGB565 画布。编译记录中的静态 RAM 约为 10.9 KB，但运行时还要加上画布、栈、堆和其他业务数据。因此，直接在现有全屏画布上叠加 LVGL，容易出现内存不足、随机重启或页面切换崩溃。

推荐的 LVGL 方案是使用局部刷新缓冲：

```text
320 × 20 × 2 字节 ≈ 12.8 KB
320 × 40 × 2 字节 ≈ 25.6 KB
```

局部缓冲比完整 320×240 双缓冲更适合当前硬件。若引入 LVGL，建议先用于冰箱、日历、档案等菜单页面，保留现有 `drawPet()` 作为小苹果动画渲染器，避免一次性重写所有页面。应减少大型中文字体、全屏图片和多份缓存。

## 六 SD 卡能解决什么问题

SD 卡可以减轻资源存储压力，但不能直接替代 SRAM。LVGL 绘制时仍然需要 SRAM 中的运行缓冲，因此 SD 卡不能直接解决全屏 framebuffer、控件对象和动画临时数据的内存问题。

SD 卡适合存放：

- 中文字体文件
- 小苹果精灵帧和图标
- 页面背景资源
- 宠物档案、冰箱历史和配置文件

推荐的资源结构为：

```text
SD 卡
  ├── fonts/
  ├── sprites/
  ├── icons/
  └── config.dat

SRAM
  ├── LVGL 局部绘图缓冲
  ├── 当前页面控件
  ├── 当前动画帧
  └── 少量临时数据
```

SD 卡读取速度比 SRAM 慢，且可能与 LCD 共用 SPI 总线，不适合动画每一帧都直接读取大量数据。更稳妥的做法是在页面切换时读取资源，在动画运行时使用已经缓存到 SRAM 的当前帧或小块数据。

## 七 当前选型结论

当前 `TFT_eSPI + TFT_eSprite + 自定义像素绘图` 最适合小苹果动画，内存和行为都比较可控。若后续重点是丰富菜单、列表、表单和统一控件样式，可以采用“LVGL 局部刷新页面 + 现有自定义宠物动画”的混合方案。SD 卡用于字体、图片和历史数据存储，不能代替绘图运行内存。
