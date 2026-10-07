# 音频模块开机挂起修复与恢复确认

日期：2026-10-03（Asia/Shanghai）
设备：Wio Terminal，COM8
已烧录固件标识：`orchard-v10-visit-calendar`（含果园活动日历迁移与本次音频修复）

## 问题与根因

烧录果园活动日历固件后，设备表面"黑屏无串口"：屏幕实际停在已绘制的初始页，`READY:vibe_pet` 永不输出，USB 仍枚举 COM8 但设备既不打印也不读串口（首条写入被 CDC 缓冲吸收，后续写入超时）。重新烧录与断电重启均复现。

根因是本次首次上机的音频模块 `audioInit()` 首调 `noTone(WIO_BUZZER)`：

- Seeed 核心 `Tone.cpp` 中 `noTone()` 直接调用 `resetTC()` 复位 TC0；
- 而 TC0 的 GCLK 时钟只在 `tone()` 首次调用路径里使能（`firstTimeRunning` 分支）；
- 时钟未使能时 SWRST 位永不自清，`while(TCx->COUNT16.CTRLA.bit.SWRST)` 死循环，`setup()` 卡死在 `audioInit()`。

因此屏幕停在 `screensInit()` 已画好的初始页、串口永不就绪。原生测试未暴露：测试桩替换了 `tone/noTone`，且 `audio_manager.cpp` 不在测试编译单元内。

## 修复

`firmware/src/audio_manager.cpp`：新增 `buzzerSilence()`，`toneRunning` 标记是否真正发声过。未发声过只 `digitalWrite(AUDIO_PIN, LOW)` 拉低电平；发声过才允许调用 `noTone()`。

| 静音路径 | 处理 |
| --- | --- |
| `audioInit()` | 改为 `pinMode` + `stopCue()`，不再直接调 `noTone()` |
| `stopCue()`（含 cue 结束、切页打断） | 经 `buzzerSilence()`，首次发声前安全 |
| `startNote()` 休息音符（如 IDLE 全休止） | 同上，避免首次 REST 触发挂起 |

规则：任何新代码不得在 `tone()` 至少运行一次之前直接调用 `noTone()`。首次 `tone()` 之后 GCLK 保持使能，后续 `noTone()` 安全。

## 编译与烧录

- PlatformIO 编译通过：RAM **164,888 / 196,608 B（83.9%）**，Flash **137,472 / 507,904 B（27.1%）**。
- 烧录 COM8 校验 `Verify successful`。

## 恢复确认（真机）

| 项目 | 结果 |
| --- | --- |
| 启动 | 正常进入页面，不再挂起 |
| 按键切页 | 恢复正常（用户实体键确认；修复前按键无响应） |
| 校时 | 串口直发 `D:<epoch>` → `OK:TIME`；`U` 命令确认 `localDate:20261003` |
| 果园活动功能 | 首页显示"探访认养树 采摘 10/16-11/02"+底部倒计时条；月历 10 月 16/30 日手绘开口圈正常（用户实体确认） |
| 守护进程 | 已按常规方式独立进程重启，占用 COM8 提供设备侧 `Q:TIME` 应答 |

## 遗留异常（不影响本功能，待后续排查）

1. `I:` 状态查询真机静默：设备不崩溃，`P`/`U`/`K` 等其余命令正常；处理器代码审查未发现格式或参数问题。该命令在 2026-10-03 时钟重构中改写过，不在守护进程与 hooks 使用路径上，`U` 已覆盖其诊断用途。
2. 宿主机 UDP `127.0.0.1:9911` 回环投递对守护进程失效：netstat 归属正确、同程序其他端口回环正常。影响 Claude hooks 的底栏状态联动，不影响设备校时应答。疑似 Windows 防火墙/WFP 状态问题，建议重启系统或检查防火墙提示后复测。

## 证据

- 修改前源码备份：`backups/before_unified_clock_20261003/src/`（无 audio_manager，可对照）；音频修复 diff 仅 `firmware/src/audio_manager.cpp`。
- 桌面快照：`artifacts/ui/overview.png`（31 屏，含活动日起止圈与清单）。
- 过程记录：`ORCHARD_VISIT_CALENDAR.md`（固件迁移与目标验证、编译烧录与真机验证两节）。
- 守护进程日志：`artifacts/logs/host_daemon.log`、`host_daemon.err.log`。
