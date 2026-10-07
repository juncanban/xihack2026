// pet_data.h —— 跨模块共享的定义:画布、调色板、代理状态、模块入口声明
//
// 常量表的正身定义在各 .cpp(vibe_pet.cpp 为主),这里只放 extern。
#ifndef PET_DATA_H
#define PET_DATA_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pet_clock.h"
#include "archive_view.h"

// Seeed 的 TFT_eSPI 分支没有这几个颜色宏
#define TFT_SILVER   0xC618
#define TFT_SKYBLUE  0x867F
#define TFT_GOLD     0xFEA0

// ---------------- 全局画布 ----------------
extern TFT_eSPI   tft;
// Both views share one RGB565 arena. Only the active screen may draw into it.
// During a page transaction the pet's usual push composes into the full frame.
class PetSprite : public TFT_eSprite {
 public:
  explicit PetSprite(TFT_eSPI* display) : TFT_eSprite(display) {}
  void pushSprite(int32_t x, int32_t y);
 private:
  // The Seeed library frees external buffers in these methods. Never expose
  // them after binding our static arena; initialization sets the depth once.
  using TFT_eSprite::deleteSprite;
  using TFT_eSprite::setColorDepth;
  friend bool uiCanvasInit();
};
extern PetSprite fb;
bool uiCanvasInit();
bool uiCanvasReady();
TFT_eSPI& uiCanvas();          // frame active: RAM; otherwise: physical LCD
void uiFrameBegin();
void uiFrameEnd();
uint32_t uiFrameCount();
uint32_t uiFrameLastMicros();
uint32_t uiLastPushBytes();
void drawPet(uint32_t now);

// ---------------- 代理状态(Claude Code 工作状态,勿与屏幕混淆) ----------------
enum PetState {
  ST_IDLE = 0, ST_THINKING, ST_TOOL, ST_WAIT, ST_DONE, ST_ERROR, ST_SLEEP,
  ST_COUNT
};

extern const char*    STATE_NAMES[ST_COUNT];
extern const char*    STATE_TAGS[ST_COUNT];
extern const uint16_t STATE_COLORS[ST_COUNT];

extern PetState gState;
extern uint32_t gLastEvent;   // 最近一次收到宿主机事件的时刻(watchdog 用)
extern uint32_t gPhysicalKeyCount; // 诊断：区分实体按键与串口测试

// vibe_pet.cpp 的界面函数(切屏重绘共用)
void drawChrome(bool clearBody = true); // false preserves a composed pet frame
void drawBottomBar();         // 底栏代理状态行

// ---------------- 宠物区几何 ----------------
extern const int16_t PET_X, PET_Y, PET_W, PET_H;

// ---------------- 小苹果调色板(M6 起被 STAGES 皮肤表覆盖) ----------------
#define C_BODY    TFT_RED
#define C_BODY_DK 0x8800    // 深红描边
#define C_HI      0xFD20    // 高光橙
#define C_BG      TFT_BLACK
#define C_LEAF    TFT_GREEN
#define C_STEM    0x7B45    // 棕

// ---------------- 宠物名 ----------------
#define DEFAULT_PET_NAME "APPLE"
extern char gPetName[13];     // UTF-8 + NUL;运行期改名限可打印 ASCII(M5 落盘)

// ---------------- 成长阶段(苹果本体进化,M6) ----------------
#define DEC_HALO  0x01        // 金色光环
#define DEC_SPARK 0x02        // 随机星点
struct StageSkin {
  uint16_t    body, dk, hi;   // 果身/描边/高光 RGB565
  uint8_t     decor;          // DEC_*
  const char* name;           // UTF-8 中文名(字模集内)
};
extern const uint32_t STAGE_NEED[3];   // 各阶段累计营养门槛(整点)
extern const StageSkin STAGES[4];
void checkEvolution();                  // 主循环调:达标即进化 + 庆祝

// ---------------- 模块入口 ----------------
// clock.cpp —— 所有页面共用 pet_clock.h 的本地时间快照；存档 epoch 仍为 UTC。

// ---------------- 光照分区与营养(防刷分核心,数值上机标定) ----------------
struct ZoneSpec {
  uint16_t    lo;         // raw ADC 下限(12bit,直晒饱和 ~4095)
  int16_t     rateQ8Min;  // Q8 营养点/分钟(256 = 1 点);负 = 暴晒扣分
  const char* label;      // M7 换中文字模
  uint16_t    color;
};
extern const ZoneSpec ZONES[5];
#define NZONES 5
#define DAY_CAP_PTS 100     // 今日配额(整点),拿满后正速率归零

// sun.cpp —— 光照采样 + 营养记账(M3)
void sunInit();
void sunTick();               // 1Hz 自节流
uint16_t sunRaw();            // 最新 16 样本均值
int8_t   sunZone();           // 当前分区下标
uint32_t sunTotalQ8();        // 累计阳光(Q8)
uint16_t sunTodayQ8();        // 今日已得(Q8)
uint32_t sunTodayDate();      // 今日日期码(0=未定)
uint16_t sunCapPts();         // 今日配额(整点)
bool     sunScorching();      // 暴晒中(宠物哭)
uint16_t sunDispLevel();      // 0..255 显示电平(每帧 EMA 平滑,光圈用)
void     sunForceRaw(int16_t raw);  // L: 强制注入;-1 恢复实读
void     sunRestore(uint32_t totalQ8, uint16_t todayQ8, uint32_t todayDate);

// save.cpp —— FlashStorage 持久化(M5)
struct Milestone {
  uint32_t epoch;             // 事件时刻
  uint8_t  kind;              // 0=领养, 1..3=进化到 stage
  uint8_t  rsv[3];
};
bool saveLoad();              // 启动扫槽,回灌 sun.cpp;false = 全新档案
void saveFlushIfDue(uint32_t nowMs);
void saveFlushNow();
void saveOnAdopt(uint32_t epoch);          // 首次时间同步触发(已领养则忽略)
void saveOnEvolve(uint8_t newStage, uint32_t epoch);  // M6
void saveRename(const char* name);
uint32_t saveAdoptEpoch();    // 0 = 未领养
bool     saveAdopted();
uint8_t  saveStage();
uint8_t  saveMileCount();
const Milestone* saveMile(uint8_t i);
bool     saveFsOk();      // QSPI 挂载状态(诊断)
void     saveDumpInfo();  // Q 命令:档案文件诊断
void     saveFactoryReset();  // X 命令:清档案重新养成

// screens.cpp —— 多屏框架 + STATUS/MEMORIAL(M4)
enum ScreenId : uint8_t {
  SCR_PET = 0, SCR_STATUS, SCR_MEMORIAL, SCR_ADOPTION,
  SCR_TODAY, SCR_CALENDAR, SCR_TREE, SCR_ARCHIVE, SCR_CAPTURE,
  SCR_FRIDGE, SCR_TIMER, SCR_TASKS, SCR_CRAFT, SCR_GUIDE,
  SCR_CONFIRM, SCR_ALERT,
  SCR_FRIDGE_ITEM, SCR_FRIDGE_EDIT, SCR_PRACTICE, SCR_COUNT
};
enum UiButton : uint8_t { UI_LEFT, UI_RIGHT, UI_UP, UI_DOWN, UI_OK, UI_BACK, UI_FARM, UI_TIMER };
void screensInit();
void screenRedraw();
void screenButton(UiButton button);
void orchardInit();
uint8_t navigationRoot(uint8_t screen);
void drawPageNavigation();
void orchardDraw(uint8_t screen);
void homeTickDraw();
void orchardButton(UiButton button);
void orchardTick();
void orchardDumpInfo();          // U: 只读页面/计时/农事状态
void dispatchUiButton(UiButton button); // 实体键和串口 K: 共用分发
uint8_t curScreen();
void setScreen(uint8_t scr);     // 切屏并整屏静态绘制
void screenTick1Hz();            // 内容变化才提交完整帧(内部 1Hz 节流)
void memoScroll(int8_t dir);     // MEMORIAL 滚动(M5 有数据后生效)
void drawCn16(int16_t x, int16_t y, const char* utf8, uint16_t color);  // 16px 中文字模
void drawCn16Bg(int16_t x, int16_t y, const char* utf8, uint16_t color, uint16_t bg);

#endif  // PET_DATA_H
