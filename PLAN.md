# vibe_pet 阳光养成系统 — 实施计划

> 状态:M1-M8 已实做并上机验证 | 更新:2026-10-02(实施细节以代码为准:存储=SFUD+fatfs,节气=寿星公式查表,字模=host/gen_hanzi.py)
> 前置:固件已稳定运行(苹果代理状态动画 + 串口协议 S:/T:/P,COM8)

## Context

现有 vibe_pet 是跟 Claude Code 状态联动的像素小苹果。本次扩展:把**真实世界的阳光
变成宠物营养值**,加完整养成系统——认养日期、节气、成长进度、每日配额、纪念页、
**宠物命名**。

**防刷分(已定)**:光照不线性累加。舒适区满分、暴晒区负速率扣分、每日配额硬上限。
**已锁定决策**:苹果本体进化(青→红→亮红→金);本次不用 IMU;新增:宠物可命名。
**传感器现实**(D:\wio 项目证实):板载 ALS-PT19 是模拟光敏三极管,非 lux 计,
强光饱和于 4095 → 分区按 raw ADC 标称 lux,阈值上机标定,防刷分目标不受影响
(舒适室内=高分,饱和直晒=扣分)。

## 宠物命名 🏷️

- **数据**:`PetSave` 加 `char name[13]`(UTF-8 含 NUL,运行期限 ASCII ≤12 字节);
  出厂默认 `APPLE`,编译期可在 pet_data.h 改
- **协议**:新命令 `N:<名字>` → 固件回 `OK:NAME`,写 save 结构(脏标记,随下次
  刷写落盘);`I` 状态回包带上名字
- **中文名(分两层)**:
  - 运行期:仅 ASCII(GLCD 字体直接可渲)
  - 中文名:改 `host/gen_hanzi.py` 字符列表(加入名用字)重新生成 `hanzi16.h`
    并重刷固件 —— 字模子集是编译期固定的,运行期收任意汉字会缺字形;
    进阶方案(暂不做):宿主机把名字的 16×16 字模随 `N:` 一起下发
- **显示**:顶栏左侧用名字替换 "VIBE PET";STATUS 屏首行;
  MEMORIAL 领养条目"**APPLE** 于 2026-10-02 被认养";串口 READY 横幅带名字

## 文件布局(firmware/src/,延续单文件+中文注释风格)

| 文件 | 职责 |
|---|---|
| `pet_data.h` **新** | 常量表(分区/阶段皮肤/配额/默认名)、PetSave、ScreenId、extern |
| `vibe_pet.cpp` 改 | 主循环/PetState 动画/串口分发/按键路由(现有代码为底) |
| `clock.cpp` **新** | 软时钟、epoch↔Y/M/D(Hinnant 算法)、节气查表 |
| `sun.cpp` **新** | 1Hz 采样(复用 D:\wio readLight 整数平均模式)、分区、记账 |
| `save.cpp` **新** | FlashStorage 8 槽轮转、CRC32、里程碑 |
| `screens.cpp` **新** | ScreenId 三屏框架、STATUS/MEMORIAL 绘制 |

`platformio.ini`:`build_src_filter = -<*> +<vibe_pet.cpp> +<clock.cpp> +<sun.cpp> +<save.cpp> +<screens.cpp>`

## 核心数据

```cpp
struct Milestone { uint32_t epoch; uint8_t kind; };  // 0=领养, 1..3=进化到 stage
struct PetSave {   // ~168B
  uint32_t magic; uint16_t version; uint16_t seq; uint32_t crc32;
  char     name[13];         // 宠物名,UTF-8+NUL,默认 "APPLE"
  uint32_t adoptEpoch;       // 0=未领养
  uint32_t totalNutri;       // Q8.8 定点
  uint16_t todayGain;  uint32_t todayDate;   // 20261002 形式,跨日清零
  uint8_t  stage, mCount;
  Milestone mile[16];
};
enum ScreenId : uint8_t { SCR_PET, SCR_STATUS, SCR_MEMORIAL, SCR_COUNT }; // 永不并入 PetState
```

## 可调常量表(全在 pet_data.h,一处集中)

```cpp
const char* const DEFAULT_PET_NAME = "APPLE";
const ZoneSpec ZONES[5] = {          // raw ADC 下限 / 速率(Q8 点/分) / 标签
  {    0,    0, "黑暗"},   // <400
  {  400,   19, "偏暗"},   // 0.25x
  { 2400,   77, "舒适"},   // 1x ≈0.30点/分,约5.4h 拿满(名义 10k–30k lux)
  { 3300,   31, "强光"},   // 0.4x
  { 3950,  -64, "暴晒"},   // 负速率:只扣 todayGain(下限0),不扣 totalNutri;宠物哭
};
const uint16_t DAY_CAP_PTS   = 100;                // 今日配额,拿满后正速率归零
const uint32_t STAGE_NEED[3] = {300, 1400, 6000};  // ≈3天/14天/60天@满配额
const StageSkin STAGES[4] = {      // 青苹果/红苹果/亮苹果/金苹果:body/dk/hi 色 + 装饰位
  {0x6DA8,...,0}, {0xF800,...,0}, {0xFC9F,...,DEC_SPARK}, {0xFEA0,...,DEC_HALO|DEC_SPARK},
};
```
记账:每秒 `accQ8 += rate*dt`,余量进位到 todayGain(定点防截断);暴晒反向累加。

## 三屏与按键

- **PET**(默认):顶栏左侧宠物名、右侧 "MM-DD 节气";`drawBody` 换
  `STAGES[stage]` 皮肤;暴晒时画泪滴动画;**光照渐变光圈**(见下节,主视觉)
- **STATUS**:首行宠物名 / 认养日期+第N天 / 今日+节气 / 阶段+分段进度条
  (Tamaguino 式量化 fillRect,纯整数)/ 累计阳光 / 今日已得÷上限(剩余)/
  实时光照+区间标签;进屏画一次 + 1Hz 局部刷新实时行(不进 30fps 热路径)
- **MEMORIAL**:时间线列表(领养/每次进化,条目带宠物名),>6 条 UP/DOWN 滚动,空态提示
- 五向键:**LEFT/RIGHT = 切屏**;UP/DOWN = PET 屏保留状态演示 / MEMORIAL 滚动
- 借鉴 EspTama 模式(无许可证,只用模式):`{onEnter, draw, onButton}` 屏幕表分发

## 主屏渐变光圈 ☀️(用户点名的核心视觉)

光强不同,从**右上角**洒进来的半透明光晕范围越大、越亮,高刷新率丝滑跟随。

```
+----------------+------------------+---------------+
| APPLE          |      10-02 秋分   |               |
|                |*                 |               |
| ☀ 舒适 68%     |  *   *           |   ← 光圈锚在宠物区
|                |    *  *  🍎      |     右上角,径向衰减
|                |  *   *           |               |
+----------------+------------------+---------------+
   图标+区间标签      光晕融入黑底        苹果被照亮
```

- **锚点**:宠物区(精灵)右上角 —— 视觉上是"光从右上方洒进来落在苹果上";
  全屏绝对右上角会跨精灵边界产生接缝,不做
- **半透明的实现**:背景纯黑,"半透明" = 径向亮度衰减(向黑混合 = 通道缩放),
  无需 alpha 混合;预混 RGB565 色阶表(~6 阶)同心 `fillCircle`,由外向内画,
  画在 `fillSprite` 之后、`drawBody` 之前(光在苹果身后)
- **强度→光圈映射**:连续化映射(不只 5 档跳变):半径与亮度随平滑后的光强
  线性增长;黑暗=无光圈;舒适=大而暖(金色);暴晒=刺眼白亮 + 宠物哭
- **刷新率**:采样仍 1Hz(记账用);**显示值每帧向最新样本 EMA 插值** + 轻微
  shimmer 波动(millis 驱动)→ 30fps 下丝滑渐变,满足"刷新频率高"
- **成本**:每帧 ~6 次 fillCircle + 查表,占用帧预算可忽略;半径档位查表避免 sinf

## 持久化(已实做:Seeed 官方 QSPI 文件系统)

- ~~FlashStorage 库~~ 实测捆绑版只支持 SAMD21,SAMD51 编译不过
- **实际方案**:SFUD + fatfs(板载 4MB QSPI flash,`SFUD.begin()` + `SFUD.open()`,
  官方 FSReadWrite 示例同款);双文件轮转 `/pet0.dat` `/pet1.dat` + seq + CRC32
  防掉电写坏,读时 CRC 合法且 seq 最大者胜,seq 回绕用 `(int16)(a-b)>0` 比较
- **单一数据源**:活值(累计/今日)由 sun.cpp 持有,save.cpp 只做启动回灌
  (sunRestore)与刷写时拉快照,不做第二份记账
- 写策略:**绝不进帧循环**——脏标记 + 满 1h 刷写;领养/进化/改名立即写
- 磨损:1h 节流 ≈ 26 次/日,QSPI flash 无忧

## 时间与节气

- **软时钟**:uint32 epoch + millis 基准(时长一律减法,防 49.7 天回绕);
  无 RTC 库;断电丢 → 宿主机自动补时
- **协议扩展**(向后兼容,host 透传零改动):
  | 命令 | 应答 | 说明 |
  |---|---|---|
  | `D:<epoch秒>` | `OK:TIME` | 幂等:|Δ|<120s 忽略;首次成功 = 写领养里程碑 |
  | `N:<ASCII名>` | `OK:NAME` | 改名,持久化 |
  | `I` | `OK:I,<名>,<epoch>,<tot>,<today>,<stage>,<raw>` | 单行状态回包 |
  | `L:<raw>` | — | 强制光照读数,分区标定用 |
- **未设时**:STATUS 屏提示,领养推迟到首次时间同步(手动设时 UI 暂缓,后续再加)
- **节气**:2025–2036 逐字查表 `TERM_DOY[12][24]`(288B flash),由宿主 Python
  (寿星公式+例外修正)生成,对照 2025 已知锚点零偏差校验(秋分 9/23 等)
- **中文字模(关键 gotcha)**:GLCD 字体无 CJK → `host/gen_hanzi.py`(Pillow 渲染
  系统字体)生成 24 节气名 + UI 用字 + 名用字(可选)约 50–60 字的 16×16 点阵
  `hanzi16.h`(~1.6–2KB flash),`drawCn16` 仅在切屏/日期变更时调用;
  Pillow 不可用时回退拼音

## 宿主机改动

- `hook_event.py`:每次事件发两行 `S:<state>\nD:<epoch>\n`(单 datagram);
  EVENT_STATE 加 `SessionStart: IDLE`;无 state 时只发 D:
- `settings.snippet.json`:修正 6 处过时路径 `D:/xian_hackson/` → `D:/vibe_pet/`;
  新增 SessionStart 钩子块
- `simulate.py`:SCRIPT 动态生成(注入当前 epoch)+ `I`/`--sun raw`/`--name` 测试选项
  (改名走 UDP→守护进程透传,不抢串口)

## 实施顺序(每步独立可验证)

1. **M1 拆文件**:按布局平移现有代码,改 build_src_filter,零行为变化(编译+烧录回归)
2. **M2 软时钟+协议**:clock.cpp + `D:`/`I`/`N:` 命令 + 顶栏名字与 ASCII 日期 + 未设时提示
3. **M3 光照记账+光圈**:sun.cpp 1Hz 采样/分区/配额/暴晒 + `L:` 调试命令 +
   主屏渐变光圈(EMA 平滑 + 色阶表同心圆)
4. **M4 三屏框架**:screens.cpp + 切屏 + STATUS 全栏 + MEMORIAL + 按键路由
5. **M5 持久化**:save.cpp + 领养里程碑 + 跨天对账 + 定时/事件刷写(名字随结构落盘)
6. **M6 进化**:皮肤 + 2.5s 庆祝动画(once-anim 自动回 IDLE,TamaPetchi 模式)+ 里程碑
7. **M7 节气+汉字**:gen_hanzi.py → hanzi16.h → 顶栏 "MM-DD 节气"(可顺带带名用字)
8. **M8 宿主接线**:hook_event.py 双行发送 + SessionStart + 路径修正 + simulate 选项

## 主要风险与对策

- **RAM**:不加第二块精灵(58KB 已占);PetSave ~168B;表全 const 进 flash
- **flash 写卡顿**:只在整点/事件边界写;庆祝动画掩盖
- **跨天掉电**:boot 同步后 `dateCode() != saved.todayDate` → todayGain 清零对账
- **传感器标定**:分区阈值为初值,上机用 `L:` 注入 + 遮光/台灯/窗边/直晒实测校准
- **循环预算**:采样 16 次亚毫秒、1Hz 一次,绝不放 drawPet 内;
  光圈每帧 ~6 次 fillCircle + 查表,预算内
- **名字字形**:运行期任意汉字必缺字形 → ASCII 运行期改名 + 中文名编译期进字模子集

## 验证

1. 每个里程碑 `pio run` + 烧录 COM8 + 串口实测(M1 重点回归现有动画/协议不变)
2. `D:<now>` → `OK:TIME`;`N:PIPIN` → `OK:NAME` 且顶栏变名、断电不丢;
   重启显示"未设时";simulate.py 全链路联调
3. 遮光/台灯/窗边/直晒观察 `I` 的 raw 与分区;暴晒区 todayGain 降到 0 不变负;
   光圈随手电/遮光实时缩放无跳变,30fps 下无卡顿
4. 断电重启:领养日/累计/里程碑/名字保留、今日清零正确;STAGE_NEED 临时改小快速触发进化
5. 顶栏 "09-23 秋分" 对照 2025 锚点;真实 Claude Code 会话见 S:/D: 成对到达
