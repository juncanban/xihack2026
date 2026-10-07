# 日期与时区统一（2026-10-03）

## 后续校时请求修复记录

2026-10-03 排查发现月历请求后 `localDate=0`：电脑仅运行不带串口参数的 `fridge_magnet.py`，未运行 `Q:TIME` 响应服务。已有固件 tick 会在首次校时后初始化日期并重绘；此前“缺少立即重绘”的判断已纠正。

已增加 `host/vibe_pet_host.py --serial-only --port COM8`，独立监听请求并回复真实 `D:<epoch>`，断线后重连；固件增加五秒超时、重复请求保护和成功反馈。目标协议测试、月历请求测试及刷机校验通过，固件标识 `fridge-ui-v8-calendar-time-service`。上一轮已启动后台服务，但未记录实体按键完整往返的真机验证，不能将其记为通过。详情见 `CALENDAR_INTERACTION.md`。本轮电脑 UI 工作未操作该设备。

已修复 PINGPING 顶栏显示 10-02、今日页显示 10-03 的问题，并更新 COM8 上的 Wio Terminal。

根因是旧宠物界面使用 UTC 日期，农事和冰箱页面自行加了八小时。北京时间凌晨 00:00–07:59，两组页面相差一天。

## 共用数据

`firmware/src/pet_clock.h` 定义 `CalendarDate` 和 `ClockSnapshot`。`clockNow()` 从同一个 UTC 软时钟生成北京时间快照，包括有效状态、原始 UTC epoch、年月日、日期码、日序号、星期、时分秒和当前节气。时区偏移仅在 `clockAtEpoch()` 应用一次。

- 顶栏、今日页、成长详情、月历今日标记、冰箱日期和每日营养共用本地日期。
- 领养和纪念事件使用相同换算；陪伴天数按本地午夜计算，未校时显示 `--`。
- 月历选中的历史/未来日期仍由用户控制，不在跨日时强制跳回今天。
- 串口 `D:`、`I:` 的 epoch 和现有档案时间戳仍是标准 UTC Unix 秒；`I:` 的日期列改为北京时间。
- 宠物、农事、冰箱的存档结构保持兼容。本次未增加时钟持久化，断电后仍需校时。

“秋分”表示当前所处节气区间，2026-10-03 仍处于秋分；固件表在 10-08 切换到寒露。现有节气表覆盖 2025–2036，按公历日近似展示，不是精确交节时刻。

## 文件改动清单

| 文件 | 改动 |
| --- | --- |
| `firmware/src/pet_clock.h` | 新增公共日期和时间快照结构，集中定义 UTC+8 偏移和时钟接口。 |
| `firmware/src/clock.cpp` | 统一 UTC 到本地日期的转换，提供 `clockNow()`、`clockAtEpoch()`、`clockDaysSince()`；日期码使用本地日期。 |
| `firmware/src/pet_data.h` | 引入公共时钟头文件，移除旧的分散日期接口声明。 |
| `firmware/src/vibe_pet.cpp` | 顶栏日期、节气和 `I:` 状态回包使用同一个时间快照。 |
| `firmware/src/screens.cpp` | 成长详情、领养日期、纪念事件和陪伴天数统一为本地日期语义。 |
| `firmware/src/orchard.cpp` | 删除页面私有的时区换算，今日、月历、冰箱共用日期源；合并跨日刷新；版本标识更新为 `fridge-ui-v3-local-clock`。 |
| `firmware/src/orchard_logic.h` | 月历 `Date` 复用公共 `CalendarDate` 类型。 |
| `tests/clock_test.cpp` | 新增真实时钟与每日营养逻辑回归测试。 |
| `tests/orchard_test.cpp` | 页面回归使用真实时钟实现，保留硬件模拟。 |
| `tests/run_orchard_tests.ps1` | 将时钟回归加入完整检查流程。 |
| `tests/check_ui_contract.py` | 时钟从逐字节保留检查改由行为测试覆盖，继续检查宠物绘制、营养代码、存档和字模。 |
| `host/verify_clock_hardware.py` | 新增真机日期和存档核对脚本，检测实体按键后停止自动导航。 |

`sun.cpp` 本身没有修改；它原有的 `dateCode()` 调用现在统一取得本地日期。没有增加 Flash/NVS/SD 时间快照文件，也没有改动电脑端自动校时流程。

## 验证

`tests/run_orchard_tests.ps1` 全套通过。新增 `clock_test.cpp` 编译真实 `clock.cpp` 和 `sun.cpp`，验证北京时间午夜、UTC 午夜不重复重置、历史事件和陪伴天数、节气边界、未校时状态以及 millis 回绕。使用独立的标准库日历对照 2020–2099 年每六小时的日期/星期/时间。原页面测试也改为使用真实时钟实现，仅模拟 millis。

PlatformIO 编译、COM8 烧录和 Flash 回读校验通过。RAM 164,816 / 196,608 B，Flash 131,632 / 507,904 B。

真机 LCD 回读确认：

- `artifacts/unified_clock/01_pet.png`：PINGPING 顶栏“秋分 10-03”。
- `artifacts/unified_clock/02_today.png`：2026/10、10/03、星期六。
- `I:` 返回本地日期 `2026-10-03`，`U:` 返回 `localDate=20261003`。

截图后检测到实体按键，已停止自动导航；月历额外真机截图未执行，由桌面回归覆盖。随后只读核对了宠物文件 CRC、农事 CRC/记录、六种食材及冰箱计数，均与更新前一致。证据在 `artifacts/unified_clock/before.json`、`verification.json` 和 `serial_log.jsonl`。源码备份在 `backups/before_unified_clock_20261003`。

## 复现命令

桌面回归和固件编译：

```powershell
./tests/run_orchard_tests.ps1 -Compiler 'D:/Program Files/Webots/msys64/mingw64/bin/g++.exe'
& 'C:/Users/15659/.platformio/penv/Scripts/pio.exe' run -d firmware -e vibe_pet
```

本次更新使用：

```powershell
& 'C:/Users/15659/.platformio/penv/Scripts/pio.exe' run -d firmware -e vibe_pet -t upload --upload-port COM8
& 'C:/Users/15659/.platformio/penv/Scripts/python.exe' host/verify_clock_hardware.py
```

真机验证脚本需更新前、当天采集的 `before.json`（含 `U:`、`I:`、`Q` 结果），并要求无运行中或已到期待处理的倒计时、起始页面为宠物页。它只发送真实电脑时间，不用虚构日期模拟跨日；若检测到实体操作则停止导航。本次脚本在两张截图后中断，不能把整个自动流程记为通过；截图与后续只读存档核对结果如上。
