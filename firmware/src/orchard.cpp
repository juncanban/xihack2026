// 320x240 原生功能页。页面层次参照 apple_bagger_ui_v3.html，宠物绘制独立保留。
#include <Arduino.h>
#include <math.h>
#include "pet_data.h"
#include "audio_manager.h"
#include "orchard_store.h"
#include "fridge_store.h"
#include "orchard_visits.h"
#include "ui_hints.h"
#include "pet_background.h"
using namespace orchard;
namespace {
constexpr uint16_t PAPER = 0xFFFF, INK = 0x18E3, MUTED = 0x7BEF;
constexpr uint16_t GREEN = 0x3349, PALE = 0xE77C, LINE = 0xE71C, FOOT = 0xF79D;
const char* const FOODS[] = {u8"苹果",u8"桃子",u8"梨",u8"菠菜",u8"胡萝卜",u8"鸡蛋"};
const char* const UNITS[] = {u8"个",u8"个",u8"个",u8"棵",u8"根",u8"个"};
const char* const FOOD_ACTIONS[] = {u8"记录吃掉",u8"记录进货",u8"修正今日已吃",u8"修正今日进货",u8"修正当前库存"};
const char* const CRAFTS[] = {u8"单层纸袋",u8"双层纸袋",u8"膜袋",u8"纸膜双层",u8"免套袋光果"};
const char* const TASKS[] = {u8"疏果",u8"修枝",u8"疏花",u8"套袋",u8"采摘"};
constexpr uint8_t TASK_COUNT = 5;
const char* const WEEK[] = {u8"一",u8"二",u8"三",u8"四",u8"五",u8"六",u8"日"};
const uint8_t ROOTS[] = {SCR_TODAY, SCR_CALENDAR, SCR_TREE, SCR_FRIDGE, SCR_TIMER};
struct TreeEntry { const char* label; uint8_t screen; };
const TreeEntry TREE_ENTRIES[] = {
  {u8"陪伴苹果", SCR_PET},
  {u8"树木档案", SCR_ARCHIVE},
  {u8"苹果树设置", SCR_CAPTURE}
};
constexpr uint8_t TREE_ENTRY_COUNT = sizeof TREE_ENTRIES / sizeof TREE_ENTRIES[0];
Date selected = {2026, 1, 1};
bool dateReady = false;
bool timeRequestPending = false;
uint32_t timeRequestAt = 0;
enum CalendarFocus : uint8_t { CAL_BROWSE, CAL_MONTH, CAL_DAYS };
CalendarFocus calendarFocus = CAL_BROWSE;
bool calendarMonthChanged = false;
uint8_t treeRow = 0, archiveRow = 0, captureRow = 0, taskRow = 0, craftRow = 0;
uint8_t guideStep = 0;
uint8_t confirmChoice = 0;
uint8_t foodIndex = 0, foodValue = 1;
fridge::Action foodAction = fridge::EAT;
uint32_t foodEditDate = 0;
enum FoodColumn : uint8_t { FOOD_NAME, FOOD_STOCK, FOOD_EATEN };
FoodColumn foodColumn = FOOD_NAME;
bool foodTimerContext = false;
const uint8_t VISIBLE_FOODS[] = {0, 5};
bool fridgeLastSaveOk = true;
uint8_t captureBatch = 10, timerPreset = 1;
uint8_t timerAction = 1; // 0 reset, 1 start/pause/continue
uint16_t customMinutes = 15;
Countdown timer;
uint8_t timerReturn = SCR_TODAY, alertReturn = SCR_TODAY;
uint8_t petReturn = SCR_TREE;
bool timerAlert = false;
uint32_t lastRefresh = 0, lastDate = 0;
uint32_t timerDrawnSeconds = UINT32_MAX;
uint32_t homeDrawnMinute = UINT32_MAX, homeDrawnDate = UINT32_MAX;
uint8_t homeDrawnState = 0xFF;
const char* toast = nullptr;
uint32_t toastAt = 0;

void text(int x, int y, const char* s, uint16_t color = INK, uint16_t bg = PAPER) {
  drawCn16Bg(x, y, s, color, bg);
}
void ascii(int x, int y, const char* s, uint16_t color = INK, uint16_t bg = PAPER, uint8_t font = 2) {
  uiCanvas().setTextDatum(TL_DATUM); uiCanvas().setTextColor(color, bg); uiCanvas().drawString(s, x, y, font);
}
void footer(const char* hint) {
  using namespace hints;
  if(!toast) switch(curScreen()) {
    case SCR_CALENDAR:
      if(!clockIsSet())bar({OK,u8"校时"});
      else if(calendarFocus==CAL_MONTH)bar({LR,u8"换月"},{DOWN,u8"调日期"},{OK,u8"农事指南"});
      else bar({UD,u8"换周"},{LR,u8"选日"},{OK,u8"农事指南"});
      return;
    case SCR_TREE:bar({UD,u8"选择"},{OK,u8"进入"});return;
    case SCR_CAPTURE:bar({A,u8"返回"});return;
    case SCR_FRIDGE:
      if(foodColumn==FOOD_STOCK)bar({UD,u8"改数量"},{OK,u8"保存"},{LEFT,u8"取消"});
      else if(foodColumn==FOOD_EATEN)bar({UD,u8"选食材"},{LEFT,u8"改库存"},{OK,u8"返回"});
      else bar({UD,u8"选食材"},{LR,u8"选列"},{OK,u8"改库存"});
      return;
    case SCR_TASKS:
      if(orchardDay(code(selected)))bar({UD,u8"选任务"},{OK,u8"查看"},{A,u8"返回"});
      else bar({OK,u8"创建"},{A,u8"返回"});
      return;
    case SCR_CRAFT:bar({UD,u8"选工艺"},{OK,u8"示教"},{A,u8"返回"});return;
    case SCR_GUIDE:bar({UD,u8"翻步"},{OK,guideStep==2?u8"实操":u8"继续"},{A,u8"返回"});return;
    case SCR_PRACTICE:bar({OK,u8"进入"},{A,u8"返回"});return;
    case SCR_CONFIRM:bar({LR,u8"选择"},{OK,u8"确认"},{A,u8"取消"});return;
    case SCR_ALERT:bar({OK,u8"关闭提醒"});return;
    default:break;
  }
  uiCanvas().fillRect(0, 214, 320, 26, FOOT);
  text(6, 219, toast ? toast : hint, MUTED, FOOT);
}
void frame(const char* title, const char* path, const char* hint, bool preservePet = false) {
  if (!preservePet) uiCanvas().fillRect(0, 0, 320, 240, PAPER);
  uiCanvas().fillRect(0, 0, 320, 25, GREEN);
  text(8, 4, title, TFT_WHITE, GREEN);
  uiCanvas().setTextDatum(TR_DATUM); uiCanvas().setTextColor(TFT_WHITE, GREEN);
  uiCanvas().drawString(path, 310, 5, 2); footer(hint);
}
void row(int y, const char* label, bool on, const char* value = nullptr) {
  uint16_t bg = on ? PALE : PAPER;
  uiCanvas().fillRoundRect(8, y, 304, 26, 4, bg);
  ascii(13, y + 4, on ? ">" : " ", GREEN, bg);
  text(28, y + 5, label, on ? GREEN : INK, bg);
  if (value) {
    uiCanvas().setTextDatum(TR_DATUM); uiCanvas().setTextColor(on ? GREEN : MUTED, bg);
    uiCanvas().drawString(value, 302, y + 5, 2);
  }
}
void message(const char* s) { toast = s; toastAt = millis(); screenRedraw(); }
void savedMessage() { message(orchardSave() ? u8"已保存" : u8"仅本次有效，存储不可用"); }
void ensureDate() {
  uint32_t dc = dateCode();
  if (!dateReady && dc && valid(decode(dc))) { selected = decode(dc); dateReady = true; }
}
void shortDate(char* out, size_t size, uint32_t dc) {
  snprintf(out, size, "%04u/%02u/%02u", (unsigned)(dc / 10000), (unsigned)(dc / 100 % 100), (unsigned)(dc % 100));
}
void apple(int x, int y, int r, bool miniature = false) {
  const auto px = [r, miniature](int n) { return miniature ? (n*r+13)/27 : n; };
  const auto& skin = STAGES[saveStage() > 3 ? 3 : saveStage()];
  uiCanvas().fillCircle(x-r/3, y, r, skin.body); uiCanvas().fillCircle(x+r/3, y, r, skin.body);
  uiCanvas().fillCircle(x, y+r/3, r, skin.body);
  uiCanvas().fillEllipse(x-r/2, y-r/5, px(3), r/2, skin.hi);
  uiCanvas().drawLine(x, y-r+px(3), x+px(4), y-r-px(9), 0x7B45);
  uiCanvas().fillEllipse(x+px(10), y-r-px(6), px(9), px(5), 0x4CC9);
  uiCanvas().fillCircle(x-px(9), y+px(2), px(3), INK); uiCanvas().fillCircle(x+px(9), y+px(2), px(3), INK);
  uiCanvas().drawPixel(x-px(10), y+px(1), TFT_WHITE); uiCanvas().drawPixel(x+px(8), y+px(1), TFT_WHITE);
  uiCanvas().fillCircle(x-px(16), y+px(10), px(3), 0xFC10); uiCanvas().fillCircle(x+px(16), y+px(10), px(3), 0xFC10);
  uiCanvas().drawLine(x-px(5), y+px(12), x, y+px(15), INK); uiCanvas().drawLine(x, y+px(15), x+px(5), y+px(12), INK);
}
bool syncFridgeDate() {
  // Direct inventory counters deliberately do not depend on the clock.
  return false;
}
void enter(uint8_t screen, const char* notice = nullptr) {
  if (screen != curScreen()) {
    if (curScreen() == SCR_FRIDGE) {
      if (screen == SCR_TIMER) foodTimerContext = true;
      else if (screen != SCR_ALERT) {
        foodColumn = FOOD_NAME; foodTimerContext = false;
      }
    }
    if (screen == SCR_FRIDGE) {
      if (!(curScreen() == SCR_TIMER && foodTimerContext)) {
        foodIndex = 0; foodColumn = FOOD_NAME;
      }
      foodTimerContext = false;
    } else if (curScreen() == SCR_TIMER && screen != SCR_ALERT) {
      foodTimerContext = false;
    }
  }
  toast = notice;
  if (notice) toastAt = millis();
  ensureDate();
  if (screen == SCR_ARCHIVE) archiveReset();
  if (screen == SCR_STATUS) archiveReset(true);
  if (screen == SCR_CAPTURE) {
    captureBatch = orchardData.batch;
  }
  if (screen == SCR_CONFIRM) confirmChoice = 0; // 明确确认才写完成
  if (screen == SCR_CALENDAR) {
    calendarFocus = CAL_DAYS; calendarMonthChanged = false;
    // Normal entry starts at today. Task/timer/alert returns use resume().
    const ClockSnapshot now = clockNow();
    if (now.valid && valid(now.date)) { selected = now.date; dateReady = true; }
  }
  if (screen == SCR_PET && curScreen() != SCR_PET)
    petReturn = curScreen() == SCR_TODAY ? SCR_TODAY : SCR_TREE;
  if (screen == SCR_TIMER && curScreen() != SCR_TIMER) {timerReturn = curScreen();timerAction = 1;}
  if (screen == curScreen()) screenRedraw(); else setScreen(screen);
}
// 从快捷计时返回时保留原页的选项、日期和未保存草稿。
void resume(uint8_t screen) {
  toast = nullptr;
  if (screen == curScreen()) screenRedraw(); else setScreen(screen);
}
bool isRootPage(uint8_t screen) {
  for(uint8_t root:ROOTS) if(root==screen) return true;
  return false;
}
void back() {
  switch (curScreen()) {
    case SCR_PET: resume(petReturn); break;
    case SCR_STATUS: resume(SCR_ARCHIVE); break;
    case SCR_ADOPTION: case SCR_MEMORIAL: enter(SCR_ARCHIVE); break;
    case SCR_ARCHIVE: case SCR_CAPTURE: enter(SCR_TREE); break;
    case SCR_TASKS: calendarFocus = CAL_DAYS; resume(SCR_CALENDAR); break;
    case SCR_CRAFT: enter(SCR_TASKS); break;
    case SCR_GUIDE: enter(SCR_TASKS); break;
    case SCR_CONFIRM: enter(SCR_GUIDE); break;
    case SCR_PRACTICE: resume(SCR_GUIDE); break;
    case SCR_FRIDGE:
      if (foodColumn != FOOD_NAME) {foodColumn = FOOD_NAME; toast = nullptr; screenRedraw();}
      break;
    case SCR_FRIDGE_EDIT: enter(SCR_FRIDGE); break;
    case SCR_FRIDGE_ITEM: enter(SCR_FRIDGE); break;
    case SCR_CALENDAR:
      if(clockIsSet()) {
        calendarFocus = calendarFocus == CAL_MONTH ? CAL_DAYS : CAL_MONTH;
        calendarMonthChanged = false;screenRedraw();
      }
      break;
    default: break; // C never changes global root pages.

  }
}
// Shared wording and date model for home, month view, and date details.
void visitCountdown(char* out,size_t size,const visits::Activity& a,uint32_t today) {
  switch(visits::phase(a,today)) {
    case visits::BEFORE: snprintf(out,size,u8"距开始%d天",visits::remaining(a,today));break;
    case visits::DURING: snprintf(out,size,u8"距结束%d天",visits::remaining(a,today));break;
    case visits::START: snprintf(out,size,u8"今天开始");break;
    case visits::END: snprintf(out,size,u8"今天结束");break;
    case visits::SINGLE: snprintf(out,size,u8"就在今天");break;
    case visits::PAST: snprintf(out,size,u8"已结束");break;
    default: snprintf(out,size,u8"校时后查看");break;
  }
}
void visitStrip(int y) {
  char b[48];const uint32_t today=dateCode();const auto* a=visits::next(today);
  uiCanvas().fillRect(0,y,320,18,PALE);
  if(!today){text(8,y+1,u8"校时后查看",MUTED,PALE);return;}
  if(!a){text(8,y+1,u8"暂无近期农事",MUTED,PALE);return;}
  text(8,y+1,TASKS[a->task],GREEN,PALE);visitCountdown(b,sizeof b,*a,today);
  text(48,y+1,b,GREEN,PALE);text(280,y+1,u8"演示",MUTED,PALE);
}
void visitCircle(int x,int y,bool selectedDay) {
  // Uneven closed-side polyline; open at upper right, never a standard arc.
  static const int8_t points[][2]={{21,0},{12,0},{5,3},{3,8},{5,13},{13,17},{24,17},{32,14},{36,10},{35,6}};
  const uint16_t color=selectedDay?0xFEA8:0xBAA4;
  for(unsigned i=1;i<sizeof points/sizeof points[0];++i)
    uiCanvas().drawLine(x+points[i-1][0],y+points[i-1][1],x+points[i][0],y+points[i][1],color);
  uiCanvas().drawLine(x+7,y+15,x+17,y+18,color);
}
void visitDateSummary(int y) {
  const uint32_t date=code(selected);unsigned count=0;
  for(const auto& a:visits::SCHEDULE)if(visits::contains(a,date))++count;
  if(!count)return;
  char b[48];snprintf(b,sizeof b,u8"探访认养树 %u项 演示",count);text(12,y,b,GREEN);
}
void calendarDraw() {
  const ClockSnapshot now = clockNow();
  const bool monthFocus = calendarFocus == CAL_MONTH;
  frame(u8"日历", "2/5", monthFocus ? u8"左右换月 下键选日" :
    calendarFocus == CAL_DAYS ? u8"上下换周 左右选日 中按进入" : u8"上下换周 左右选日 中按进入");
  if (!dateReady) { text(24, 90, u8"中按请求电脑校时"); text(24, 118, u8"请开启电脑校时服务", MUTED); visitStrip(194); return; }
  char b[40]; snprintf(b, sizeof b, "%04d / %02d", selected.year, selected.month);
  uiCanvas().fillRoundRect(8, 28, 154, 23, 4, monthFocus ? GREEN : PAPER);
  ascii(12, 32, b, monthFocus ? TFT_WHITE : GREEN, monthFocus ? GREEN : PAPER);
  text(180, 32, monthFocus ? u8"选择月份" : calendarFocus == CAL_DAYS ? u8"选择日期" : u8"中按操作", MUTED);
  for (int col = 0; col < 7; ++col) text(20 + col*44, 54, WEEK[col], MUTED);
  Date first = selected; first.day = 1; int offset = weekday(first);
  for (int d = 1; d <= monthDays(selected.year, selected.month); ++d) {
    int n = d + offset - 1, x = 8 + (n%7)*44, y = 74 + (n/7)*20;
    bool on = d == selected.day; uint16_t bg = on ? GREEN : FOOT;
    uiCanvas().fillRoundRect(x, y, 40, 18, 3, bg);
    snprintf(b, sizeof b, "%d", d); ascii(x+10, y, b, on ? TFT_WHITE : INK, bg);
    Date date = selected; date.day = d;
    auto* r = orchardDay(code(date));
    if (r) uiCanvas().fillCircle(x+33, y+13, 2, on ? TFT_WHITE : GREEN);
    if (code(date) == now.dateCode) {
      // Inset contrasting outline remains visible even when today is selected.
      uiCanvas().drawRoundRect(x+1, y+1, 38, 16, 3, on ? TFT_WHITE : GREEN);
    }
    if(visits::boundary(code(date)))visitCircle(x,y,on);
  }
  visitStrip(194);
}
void tasksDraw() {
  frame(u8"今日农事", "LIST", u8"上下选任务 中按查看 左键返回");
  char b[32]; shortDate(b, sizeof b, code(selected)); ascii(12, 31, b, GREEN);
  auto* r = orchardDay(code(selected));
  if (!r) {
    text(24, 80, u8"今天还没有农事清单");
    text(24, 111, u8"中按创建五项农艺任务", GREEN);
    text(24, 150, u8"五种农艺，按需选择", MUTED);
    visitDateSummary(178);
    return;
  }
  snprintf(b, sizeof b, "%u / %u", completedCount(r->reserved), TASK_COUNT); ascii(246, 31, b, GREEN);
  visitDateSummary(51);
  bool hasVisit=false;for(const auto& a:visits::SCHEDULE)if(visits::contains(a,code(selected)))hasVisit=true;
  for (uint8_t i = 0; i < TASK_COUNT; ++i) {
    const int y=hasVisit?72+i*28:51+i*30;
    row(y, TASKS[i], i == taskRow, (r->reserved & (1<<i)) ? "[OK]" : "[ ]");
    if(hasVisit)for(const auto& a:visits::SCHEDULE)if(a.task==i&&visits::contains(a,code(selected))) {
      text(112,y+5,a.start==a.end?u8"单日体验":code(selected)==a.start?u8"体验开始":code(selected)==a.end?u8"体验结束":u8"体验期间",MUTED,i==taskRow?PALE:PAPER);break;
    }
  }

}

bool fridgeScreen(uint8_t screen) {
  return screen == SCR_FRIDGE || screen == SCR_FRIDGE_ITEM ||
    screen == SCR_FRIDGE_EDIT;
}
bool fridgeTodayReady() {
  return clockIsSet() && fridgeData.date == dateCode();
}
const char* fridgeError(fridge::Result result) {
  switch (result) {
    case fridge::NEED_CLOCK: return u8"请先连接电脑同步时间";
    case fridge::NO_STOCK: return u8"库存不足，请先记录进货";
    case fridge::OVER_LIMIT: return u8"数量超过九十九，请重新调整";
    case fridge::NO_UNDO: return u8"暂无可撤销的记录";
    case fridge::NO_CHANGE: return u8"数量没有变化";
    default: return u8"数量无效，请重新调整";
  }
}
// 小型食材图标，与页面其他内容一起绘制到公共画布。
void foodIcon(uint8_t item, int x, int y) {
  switch (item) {
    case 0:
      uiCanvas().fillCircle(x-4,y,7,0xDB4B); uiCanvas().fillCircle(x+4,y,7,0xDB4B);
      uiCanvas().fillEllipse(x,y+4,9,7,0xDB4B); uiCanvas().fillEllipse(x+5,y-11,5,3,0x6CCB); break;
    case 1:
      uiCanvas().fillEllipse(x,y,10,11,0xED52); uiCanvas().drawLine(x+1,y-6,x-1,y+8,0xCBCE);
      uiCanvas().fillEllipse(x+5,y-12,5,2,0x74AA); break;
    case 2:
      uiCanvas().fillCircle(x,y+4,10,0xBE4D); uiCanvas().fillEllipse(x,y-5,5,8,0xBE4D);
      uiCanvas().drawLine(x,y-11,x+2,y-16,0x8BC8); break;
    case 3:
      uiCanvas().drawLine(x,y-2,x,y+13,0x9D50);
      uiCanvas().fillEllipse(x-5,y-4,4,10,0x64CA); uiCanvas().fillEllipse(x+5,y-3,4,11,0x850D); break;
    case 4:
      for (int j=0;j<20;++j) uiCanvas().drawLine(x-6+j/4,y-7+j,x+6-j/4,y-7+j,0xE4A9);
      uiCanvas().drawLine(x,y-7,x-4,y-15,0x74CA); uiCanvas().drawLine(x,y-7,x+4,y-16,0x74CA); break;
    default: uiCanvas().fillEllipse(x,y,8,12,0xE651); uiCanvas().fillEllipse(x-3,y-4,2,5,0xFF18); break;
  }
}
void foodNumber(int x, int y, uint8_t value, uint8_t item, uint16_t bg, bool known=true) {
  char b[8]; snprintf(b,sizeof b,"%u",value);
  ascii(x,y,known?b:"--",GREEN,bg,4);
  text(x+35,y+8,UNITS[item],MUTED,bg);
}
void fridgeDraw() {
  frame(u8"冰箱助手","4/5",foodColumn==FOOD_STOCK ?
    u8"上下改数 中按保存 左/C取消" : u8"上下选食材 左右选列");
  text(17,39,u8"食材",MUTED); text(166,39,u8"库存",MUTED);
  text(236,39,u8"今天已吃",MUTED);
  for (uint8_t rowIndex=0;rowIndex<2;++rowIndex) {
    const uint8_t i=VISIBLE_FOODS[rowIndex];
    const int y=64+rowIndex*58; const bool on=i==foodIndex;
    const uint16_t bg=on?PALE:FOOT;
    uiCanvas().fillRoundRect(8,y,304,48,5,bg);
    if (on) {
      const int x=foodColumn==FOOD_NAME?12:foodColumn==FOOD_STOCK?148:236;
      const int w=foodColumn==FOOD_NAME?121:foodColumn==FOOD_STOCK?68:66;
      uiCanvas().fillRoundRect(x,y+3,w,42,4,GREEN);
    }
    const bool nameOn=on && foodColumn==FOOD_NAME;
    foodIcon(i,33,y+25);text(54,y+16,FOODS[i],nameOn?TFT_WHITE:INK,nameOn?GREEN:bg);
    for (uint8_t col=1;col<=2;++col) {
      const bool focused=on && foodColumn==col;
      const uint8_t value=col==1?(on && foodColumn==FOOD_STOCK?foodValue:fridgeData.items[i].stock):fridgeData.items[i].eaten;
      char n[8];snprintf(n,sizeof n,"%u",value);
      uiCanvas().setTextDatum(MC_DATUM);uiCanvas().setTextColor(focused?TFT_WHITE:GREEN,focused?GREEN:bg);
      uiCanvas().drawString(n,col==1?178:268,y+24,4);
    }
    if (on && foodColumn==FOOD_STOCK) {
      for (int k=0;k<4;++k) {
        uiCanvas().drawLine(203-k,y+10+k,203+k,y+10+k,TFT_WHITE);
        uiCanvas().drawLine(203-k,y+37-k,203+k,y+37-k,TFT_WHITE);
      }
    }
  }
  char b[72];
  if (foodColumn==FOOD_STOCK) {
    snprintf(b,sizeof b,u8"%s库存 %u > %u",FOODS[foodIndex],fridgeData.items[foodIndex].stock,foodValue);
    text(14,186,b,GREEN);text(256,186,u8"未保存",MUTED);
  } else {
    snprintf(b,sizeof b,u8"%s %s",FOODS[foodIndex],foodColumn==FOOD_NAME?u8"右键改库存":u8"今天已吃");
    text(14,186,b,GREEN);text(248,186,u8"单位",MUTED);text(288,186,UNITS[foodIndex],MUTED);
  }
}
void beginInventoryEdit() {
  foodAction=fridge::SET_STOCK;foodValue=fridgeData.items[foodIndex].stock;
  foodColumn=FOOD_STOCK;toast=nullptr;screenRedraw();
}
void commitInventoryEdit() {
  const fridge::Data before=fridgeData;
  const fridge::Result result=fridge::applyInventory(fridgeData,foodIndex,foodValue);
  if (result==fridge::NO_CHANGE) {foodColumn=FOOD_NAME;message(u8"数量没有变化");return;}
  if (result!=fridge::OK) {message(fridgeError(result));return;}
  fridgeLastSaveOk=fridgeSave();
  if (!fridgeLastSaveOk) {fridgeData=before;message(u8"保存失败，请重试");return;}
  foodColumn=FOOD_NAME;message(u8"已保存");
}
void fridgeItemDraw() {
  frame(FOODS[foodIndex],"FRIDGE",u8"上下选操作 中按进入 左返回");
  text(18,33,u8"库存",MUTED); text(126,33,u8"已吃",MUTED); text(233,33,u8"进货",MUTED);
  const auto& f=fridgeData.items[foodIndex];
  foodNumber(17,51,f.stock,foodIndex,PAPER);
  foodNumber(125,51,f.eaten,foodIndex,PAPER,fridgeTodayReady());
  foodNumber(232,51,f.incoming,foodIndex,PAPER,fridgeTodayReady());
  for (uint8_t i=0;i<5;++i) {
    int y=85+i*25; uint16_t bg=i==foodAction?PALE:PAPER;
    uiCanvas().fillRoundRect(8,y,304,23,4,bg);
    ascii(14,y+3,i==foodAction?">":" ",GREEN,bg);
    text(31,y+4,FOOD_ACTIONS[i],i==foodAction?GREEN:INK,bg);
  }
}
void fridgeEditDraw() {
  frame(FOOD_ACTIONS[foodAction],"FRIDGE",u8"左右改一 上下改十 中按保存");
  text(18,35,FOODS[foodIndex],GREEN);
  text(118,35,foodAction<=fridge::RESTOCK?u8"本次数量":u8"改为总数",MUTED);
  char b[72]; snprintf(b,sizeof b,"%u",foodValue);
  uiCanvas().setTextDatum(MC_DATUM); uiCanvas().setTextColor(GREEN,PAPER); uiCanvas().drawString(b,149,89,6);
  text(192,93,UNITS[foodIndex],MUTED);
  fridge::Item after;
  fridge::Result result=fridge::preview(fridgeData,foodIndex,foodAction,foodValue,dateCode(),after);
  if (result!=fridge::OK && result!=fridge::NO_CHANGE) {
    text(13,137,fridgeError(result),0xB348);
    text(13,172,u8"返回键取消，不改数量",MUTED);
  } else {
    const auto& before=fridgeData.items[foodIndex];
    snprintf(b,sizeof b,u8"库存  %u > %u %s",before.stock,after.stock,UNITS[foodIndex]); text(38,128,b,GREEN);
    if (foodAction==fridge::SET_STOCK) text(38,155,u8"只修正库存，今日记录不变",MUTED);
    else {
      bool incoming=foodAction==fridge::RESTOCK || foodAction==fridge::SET_INCOMING;
      snprintf(b,sizeof b,u8"%s  %u > %u %s",incoming?u8"今日进货":u8"今日已吃",
        incoming?before.incoming:before.eaten,incoming?after.incoming:after.eaten,UNITS[foodIndex]);
      text(38,155,b,MUTED);
    }
    text(62,189,u8"中按保存  返回键取消",GREEN);
  }
}
void beginFoodEdit() {
  syncFridgeDate();
  const auto& f=fridgeData.items[foodIndex];
  foodValue=foodAction==fridge::SET_EATEN?f.eaten:foodAction==fridge::SET_INCOMING?f.incoming:
    foodAction==fridge::SET_STOCK?f.stock:1;
  foodEditDate=dateCode();
  enter(SCR_FRIDGE_EDIT);
}
void commitFoodEdit() {
  syncFridgeDate();
  if (foodAction!=fridge::SET_STOCK && foodEditDate!=dateCode()) {
    foodEditDate=dateCode();
    if (foodAction==fridge::SET_EATEN) foodValue=fridgeData.items[foodIndex].eaten;
    if (foodAction==fridge::SET_INCOMING) foodValue=fridgeData.items[foodIndex].incoming;
    message(u8"日期已变，请重新确认");return;
  }
  fridge::Data before=fridgeData;
  fridge::Result result=fridge::apply(fridgeData,foodIndex,foodAction,foodValue,dateCode());
  if (result!=fridge::OK) {message(fridgeError(result));return;}
  fridgeLastSaveOk=fridgeSave();
  if (!fridgeLastSaveOk) {fridgeData=before;message(u8"保存失败，请重试");return;}
  enter(SCR_FRIDGE,u8"已保存");
}
void timerKeyIcon(int x,int y,int direction) {
  auto& cv=uiCanvas();
  cv.fillCircle(x,y,5,MUTED);cv.fillCircle(x,y,4,FOOT);
  if (!direction) cv.fillCircle(x,y,2,MUTED);
  else {
    cv.drawLine(x-direction,y-2,x+direction,y,MUTED);
    cv.drawLine(x+direction,y,x-direction,y+2,MUTED);
  }
}
void timerDraw() {
  frame(u8"生活计时", "5/5", "",true);
  auto& cv=uiCanvas();
  cv.fillRect(0,214,320,7,PAPER);
  uint32_t seconds = (timer.remainingMs + 999) / 1000;
  timerDrawnSeconds = seconds;
  const bool paused=!timer.running&&!timer.finished&&timer.remainingMs<timer.durationMs;
  char b[24];
  snprintf(b,sizeof b,"%lu min",(unsigned long)(timer.durationMs/60000));
  if (toast) text(8,32,toast,MUTED);
  else {cv.setTextDatum(MC_DATUM);cv.setTextColor(MUTED,PAPER);cv.drawString(b,160,40,2);}
  // A continuous remaining-time arc; one composed frame per changed second.
  for (int i = 0; i < 360; ++i) {
    const float a=(i-90)*0.01745329252f;
    const int x=160+(int)(51*cosf(a)),y=103+(int)(51*sinf(a));
    const bool on=static_cast<uint64_t>(i)*timer.durationMs<static_cast<uint64_t>(timer.remainingMs)*360;
    cv.fillCircle(x,y,2,on?GREEN:LINE);
  }
  snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  cv.setTextDatum(MC_DATUM);cv.setTextColor(INK,PAPER);cv.drawString(b,160,100,4);
  const char* state=timer.finished?u8"时间到了":timer.running?u8"计时中":paused?u8"已暂停":u8"准备开始";
  text(160-static_cast<int>(strlen(state)/3)*8,124,state,MUTED);
  // Two action choices: horizontal focus is executable by the middle key.
  const uint16_t resetBg=timerAction==0?GREEN:PALE, runBg=timerAction==1?GREEN:PALE;
  const uint16_t resetFg=timerAction==0?TFT_WHITE:GREEN, runFg=timerAction==1?TFT_WHITE:GREEN;
  cv.fillRoundRect(36,163,108,35,5,resetBg);cv.fillRoundRect(176,163,108,35,5,runBg);
  // Single connected counterclockwise reset arrow.
  for(int angle=225;angle<=500;++angle) {
    const float a=angle*0.01745329252f;
    cv.fillCircle(55+(int)(7*cosf(a)),180+(int)(7*sinf(a)),1,resetFg);
  }
  cv.drawLine(51,173,51,180,resetFg);cv.drawLine(51,180,56,180,resetFg);
  text(72,174,u8"重置",resetFg,resetBg);
  if(timer.running) {cv.fillRect(196,174,4,15,runFg);cv.fillRect(204,174,4,15,runFg);}
  else for(int i=0;i<14;++i) cv.drawLine(195+i,173+i*8/13,195+i,189-i*8/13,runFg);
  text(219,174,timer.running?u8"暂停":paused?u8"继续":timer.finished?u8"重开":u8"开始",runFg,runBg);
  // Physical input instructions occupy a separate footer, not action buttons.
  cv.fillRect(0,221,320,19,FOOT);cv.drawLine(8,221,312,221,LINE);
  timerKeyIcon(14,231,-1);text(24,223,u8"左右选",MUTED,FOOT);
  timerKeyIcon(112,231,0);text(122,223,u8"中按执行",MUTED,FOOT);
  cv.drawLine(234,225,234,236,MUTED);
  cv.drawLine(231,228,234,225,MUTED);cv.drawLine(234,225,237,228,MUTED);
  cv.drawLine(231,233,234,236,MUTED);cv.drawLine(234,236,237,233,MUTED);
  text(244,223,u8"上下调时",MUTED,FOOT);
}
struct GuideContent { const char* title; const char* first; const char* second; };
const GuideContent GUIDES[5][3] = {
  {{u8"先观察",u8"观察果形与长势",u8"选留健壮的果实"},{u8"疏去弱果",u8"疏去病弱小果",u8"留果量听取指导"},{u8"检查间距",u8"分布均匀不拥挤",u8"避免碰伤留下果"}},
  {{u8"观察枝条",u8"辨认病弱交叉枝",u8"先听现场指导"},{u8"工具消毒",u8"清洁并消毒剪刀",u8"剪口避开枝领"},{u8"检查树冠",u8"保持通风与透光",u8"避免一次剪过多"}},
  {{u8"观察花序",u8"查看花朵与枝势",u8"留花量结合长势"},{u8"选择留花",u8"优先保留健壮花",u8"按指导疏去弱花"},{u8"轻摘复查",u8"轻摘多余花朵",u8"避免拉伤枝条"}},
  {{u8"撑开袋口",u8"一手托住果实",u8"一手撑开袋口"},{u8"对准果柄",u8"向上推袋再收口",u8"不要勒伤果柄"},{u8"检查袋体",u8"袋体平整不贴果",u8"袋底保持通风"}},
  {{u8"查看成熟",u8"按品种判断成熟",u8"先听现场指导"},{u8"托果上抬",u8"托果轻抬再旋转",u8"不要用力拉枝"},{u8"轻放入筐",u8"轻拿轻放不碰伤",u8"分层摆放防挤压"}}
};
constexpr uint16_t rgb565(unsigned c) {return ((c>>8)&0xF800)|((c>>5)&0x07E0)|((c>>3)&0x001F);}
void agronomyIllustration(uint8_t task) {
  const uint16_t wood=rgb565(0x8B7054),red=rgb565(0xE45150),leaf=rgb565(0x4E9B63),paper=rgb565(0xE8DFC9);
  auto& cv=uiCanvas(); cv.fillRoundRect(8,33,104,171,5,paper);
  cv.fillRect(16,66,87,3,wood);
  auto fruit=[&](int x,int y,int r){cv.drawLine(x,68,x,y-r,wood);cv.fillCircle(x,y,r,red);cv.fillEllipse(x+6,y-r-2,6,2,leaf);};
  if(task==0){fruit(35,102,9);fruit(62,100,12);fruit(88,103,7);if(guideStep){cv.drawLine(81,94,94,109,INK);cv.drawLine(94,94,81,109,INK);}cv.drawRoundRect(47,83,30,35,4,GREEN);text(26,168,u8"留壮去弱",GREEN,paper);}
  else if(task==1){cv.fillRect(58,67,3,89,wood);cv.drawLine(59,100,30,81,wood);cv.drawLine(59,123,86,91,wood);cv.fillEllipse(28,80,8,4,leaf);cv.fillEllipse(86,90,6,3,leaf);cv.drawLine(80,120,55,110,INK);cv.drawLine(80,108,55,121,INK);cv.drawRoundRect(78,117,12,10,4,red);cv.drawRoundRect(78,101,12,10,4,red);text(26,168,u8"去除弱枝",GREEN,paper);}
  else if(task==2){for(int i=0;i<3;i++){int x=32+i*28,y=i==1?97:120;cv.drawLine(60,68,x,y,wood);for(int j=-1;j<=1;j++)cv.fillCircle(x+j*5,y,6,rgb565(0xF2B6C2));cv.fillCircle(x,y,3,rgb565(0xF0B52F));}cv.drawRoundRect(46,85,28,26,4,GREEN);text(26,168,u8"选择留花",GREEN,paper);}
  else if(task==3){fruit(60,98,12);int top=guideStep?89:120;cv.fillRect(39,top,43,40,rgb565(0xF8F4E8));cv.drawRect(39,top,43,40,wood);cv.drawLine(39,top,50,top-7,wood);cv.drawLine(82,top,70,top-7,wood);cv.drawLine(60,155,60,139,GREEN);cv.drawLine(60,139,56,144,GREEN);cv.drawLine(60,139,64,144,GREEN);text(26,178,u8"向上套袋",GREEN,paper);}
  else {fruit(60,guideStep==2?125:100,13);cv.drawLine(38,137,60,119,rgb565(0xD8A783));cv.drawLine(60,119,81,137,rgb565(0xD8A783));cv.drawRect(28,147,66,17,wood);cv.drawLine(86,129,86,101,GREEN);cv.drawLine(86,101,82,107,GREEN);text(26,178,u8"托果轻放",GREEN,paper);}
}
void guideDraw() {
  char b[24];snprintf(b,sizeof b,"%u / 3",guideStep+1);
  frame(u8"农艺说明",b,u8"上下翻步 中按继续 左键返回");
  agronomyIllustration(taskRow);
  text(124,36,TASKS[taskRow],GREEN);
  auto* r=orchardDay(code(selected));
  text(236,36,r&&(r->reserved&(1<<taskRow))?u8"已完成":u8"学习中",MUTED);
  const auto& g=GUIDES[taskRow][guideStep];
  text(124,72,g.title);text(124,105,g.first);text(124,130,g.second);
  if(taskRow==3)text(124,158,CRAFTS[craftRow],MUTED);
  else text(124,158,u8"按现场指导操作",MUTED);
  text(124,186,guideStep==2?u8"中按进入实操":u8"中按看下一步",GREEN);
}
}

void orchardInit() { orchardLoad(); fridgeLoad(); ensureDate(); syncFridgeDate(); lastDate = dateCode(); }

// Shared top-level context, independent from the current subpage.
uint8_t navigationRoot(uint8_t screen) {
  if (screen == SCR_ALERT) screen = alertReturn;
  switch(screen) {
    case SCR_TODAY: return SCR_TODAY;
    case SCR_PET: return petReturn;
    case SCR_TIMER: return SCR_TIMER;
    case SCR_FRIDGE: case SCR_FRIDGE_ITEM: case SCR_FRIDGE_EDIT: return SCR_FRIDGE;
    case SCR_CALENDAR: case SCR_TASKS: case SCR_CRAFT: case SCR_GUIDE:
    case SCR_CONFIRM: case SCR_PRACTICE: return SCR_CALENDAR;
    default: return SCR_TREE;
  }
}
void drawPageNavigation() {
  const char* names[]={u8"今日首页",u8"日历",u8"我的树",u8"冰箱助手",u8"生活计时"};
  const uint16_t nav=0x32C8, soft=0xE77C;
  uint8_t index=0;
  for(uint8_t i=0;i<5;++i) if(ROOTS[i]==navigationRoot(curScreen())) index=i;
  auto& cv=uiCanvas();
  cv.fillRect(0,0,320,25,nav);
  cv.fillRoundRect(113,2,94,21,4,soft);
  const uint8_t indices[]={static_cast<uint8_t>((index+4)%5),index,static_cast<uint8_t>((index+1)%5)};
  const int centers[]={55,160,265};
  for(uint8_t n=0;n<3;++n) {
    const char* label=names[indices[n]];
    const int width=static_cast<int>(strlen(label)/3)*16;
    text(centers[n]-width/2,4,label,n==1?INK:TFT_WHITE,n==1?soft:nav);
  }
}

// Compact homepage summary; reads real local date, never the browsing cursor.
void homeActivity(char* out, size_t size, uint32_t today) {
  if (!today) { snprintf(out,size,u8"等待校时"); return; }
  const auto* a=visits::next(today);
  if (!a) { snprintf(out,size,u8"暂无活动"); return; }
  const char* phaseText=u8"进行中";
  switch(visits::phase(*a,today)) {
    case visits::BEFORE: snprintf(out,size,u8"%s · %d天后",TASKS[a->task],visits::remaining(*a,today)); return;
    case visits::START: phaseText=u8"今天开始"; break;
    case visits::END: phaseText=u8"今天结束"; break;
    case visits::SINGLE: phaseText=u8"就在今天"; break;
    default: break;
  }
  snprintf(out,size,u8"%s · %s",TASKS[a->task],phaseText);
}
void homeDrawClockAndActivity() {
  auto& cv=uiCanvas();
  cv.fillRect(0,34,160,180,PAPER);
  const auto now=clockNow();char b[72];
  if(now.valid) {
    snprintf(b,sizeof b,"%02u/%02u",now.date.month,now.date.day);ascii(13,53,b,MUTED);
    text(66,53,u8"星期",MUTED);text(100,53,WEEK[now.weekday],MUTED);
    snprintf(b,sizeof b,"%02u:%02u",now.hour,now.minute);ascii(10,83,b,INK,PAPER,6);
  } else {text(13,53,u8"等待校时",MUTED);ascii(10,83,"--:--",INK,PAPER,6);}
  text(10,138,u8"今天，也一起成长",MUTED);
  cv.drawLine(14,168,142,168,LINE);
  homeActivity(b,sizeof b,now.dateCode);text(10,185,b,INK);
}
void homeDrawPetLabel() {
  ascii(207,57,gPetName,MUTED);
}
void homeDrawBottom() {
  const char* states[]={u8"陪伴中",u8"思考中",u8"忙碌中",u8"等待中",u8"已完成",u8"出错了",u8"休息中"};
  hints::bar({hints::STATE,states[gState < ST_COUNT ? gState : ST_IDLE]},{hints::OK,u8"陪伴"},{hints::AB,u8"翻页"});
}
void homeDraw() {
  auto& cv=uiCanvas();
  cv.fillRect(0,0,320,34,PAPER);
  cv.fillRect(0,34,160,187,PAPER);
  homeDrawClockAndActivity();
  homeDrawPetLabel();
  homeDrawBottom();
  const auto now=clockNow();
  homeDrawnMinute=now.valid ? now.utcEpoch/60 : 0;
  homeDrawnDate=now.dateCode;
  homeDrawnState=gState;
}
void homeTickDraw() {
  const auto now=clockNow();
  const uint32_t minute=now.valid ? now.utcEpoch/60 : 0;
  if (minute != homeDrawnMinute || now.dateCode != homeDrawnDate) {
    homeDrawClockAndActivity();
    homeDrawnMinute=minute;
    homeDrawnDate=now.dateCode;
  }
  if (gState != homeDrawnState) {
    homeDrawBottom();
    homeDrawnState=gState;
  }
  // The pet window covers the name label on every direct partial submit.
  homeDrawPetLabel();
}

void orchardDraw(uint8_t screen) {
  ensureDate();
  switch (screen) {
    case SCR_TODAY: homeDraw(); break;
    case SCR_CALENDAR: calendarDraw(); break;
    case SCR_TREE:
      frame(u8"我的树", "3/5", u8"上下选择 中按进入",true);
      ascii(117, 51, gPetName, INK, PAPER, 4);
      for (uint8_t i = 0; i < TREE_ENTRY_COUNT; ++i) {
        int y = 78 + i * 38;
        uint16_t bg = treeRow == i ? PALE : PAPER;
        uiCanvas().fillRoundRect(111, y, 199, 30, 4, bg);
        ascii(116, y + 6, treeRow == i ? ">" : " ", GREEN, bg);
        text(132, y + 7, TREE_ENTRIES[i].label, GREEN, bg);
      }
      break;
    case SCR_ARCHIVE:
      archiveDraw(false);
      break;
    case SCR_CAPTURE:
      frame(u8"苹果树设置", "TREE > SET", u8"A 返回我的树",true);
      text(14,42,u8"苹果树设置",INK);
      uiCanvas().fillRoundRect(250,37,58,25,4,0xF73B);
      text(255,42,u8"待开发",0x8BA9,0xF73B);
      uiCanvas().fillRoundRect(12,77,296,42,5,FOOT);
      text(25,91,u8"名字",INK,FOOT);
      uiCanvas().setTextDatum(TR_DATUM);uiCanvas().setTextColor(MUTED,FOOT);
      uiCanvas().drawString(gPetName,293,91,2);
      // Preserve the precomposed real-model thumbnail at (192,127,32,37).
      uiCanvas().fillRoundRect(12,125,180,43,5,FOOT);
      uiCanvas().fillRoundRect(224,125,84,43,5,FOOT);
      uiCanvas().fillRect(187,125,42,2,FOOT);
      uiCanvas().fillRect(187,164,42,4,FOOT);
      uiCanvas().fillRect(187,127,5,37,FOOT);
      uiCanvas().fillRect(224,127,5,37,FOOT);
      text(25,139,u8"形象定制",INK,FOOT);
      text(236,139,u8"当前形象",MUTED,FOOT);
      text(80,177,u8"后续通过手机小程序",GREEN);
      text(80,196,u8"修改名字、定制形象",MUTED);
      break;
    case SCR_FRIDGE: fridgeDraw(); break;
    case SCR_FRIDGE_ITEM: fridgeItemDraw(); break;
    case SCR_FRIDGE_EDIT: fridgeEditDraw(); break;
    case SCR_TIMER: timerDraw(); break;
    case SCR_TASKS: tasksDraw(); break;
    case SCR_CRAFT: {
      frame(u8"选择套袋工艺", "1/4", u8"上下选工艺 中按看示教");
      for (uint8_t i = 0; i < 5; ++i) row(34 + i*34, CRAFTS[i], craftRow == i);
      break;
    }
    case SCR_GUIDE: guideDraw(); break;
    case SCR_PRACTICE:
      frame(u8"农事实操", "PRACTICE", u8"中按进入 A返回");
      text(16,40,TASKS[taskRow],GREEN);
      text(16,67,u8"先实操，再确认完成",MUTED);
      row(99,u8"完成确认",true);
      text(16,181,u8"实操不会自动完成任务",MUTED);
      break;
    case SCR_CONFIRM:
      frame(u8"完成确认", "3/4", u8"左右选 中按确认 返回键取消");
      text(24, 64, u8"已经跟着做完这一遍了吗");
      text(24, 99, u8"确认后才为当前任务打勾", MUTED);
      for (int i = 0; i < 2; ++i) {
        uint16_t bg = confirmChoice == i ? GREEN : FOOT;
        uiCanvas().fillRoundRect(24+i*147, 146, 126, 38, 6, bg);
        text(58+i*147, 157, i ? u8"已完成" : u8"再等等", confirmChoice == i ? TFT_WHITE : INK, bg);
      }
      break;
    case SCR_ALERT:
      frame(u8"提醒", "NOTICE", u8"中按确认 返回原页面");
      apple(160, 80, 23);
      text(98, 131, u8"时间到了", GREEN);
      text(43, 166, u8"按中键关闭本次屏幕提醒", MUTED);
      break;
    default: break;
  }
}

void orchardButton(UiButton button) {
  uint8_t screen = curScreen();
  bool changed = fridgeScreen(screen) && syncFridgeDate();
  if (screen == SCR_ALERT) {
    if (button == UI_OK || button == UI_BACK || button == UI_TIMER) resume(alertReturn);
    return;
  }
  if (button == UI_BACK) { back(); return; }
  if (button == UI_TIMER && !isRootPage(screen)) { back(); return; }
  if (button == UI_TIMER || button == UI_FARM) {
    uint8_t index=0;
    for(uint8_t i=0;i<5;++i) if(ROOTS[i]==navigationRoot(screen)) index=i;
    enter(ROOTS[(index+(button==UI_TIMER?4:1))%5]);
    return;
  }
  // 清除已有提示本身也是可见变化；其余按键只在值真正改变后重绘。
  changed = changed || toast != nullptr;
  toast = nullptr;
  switch (screen) {
    case SCR_PET:
      if (button == UI_LEFT || button == UI_RIGHT) {
        petbackground::step(button == UI_LEFT ? -1 : 1);
        screenRedraw();
      }
      else if (button == UI_OK) back();
      else if (changed) screenRedraw();
      return;
    case SCR_TODAY:
      if (button == UI_OK) enter(SCR_PET);
      else if (changed) screenRedraw();
      return;
    case SCR_TREE:
      if (button == UI_UP || button == UI_DOWN) {
        treeRow = (treeRow + (button == UI_UP ? TREE_ENTRY_COUNT - 1 : 1)) % TREE_ENTRY_COUNT;
        changed = true;
      }
      else if (button == UI_OK) { enter(TREE_ENTRIES[treeRow].screen); return; }
      break;
    case SCR_ARCHIVE:
    case SCR_STATUS:
      if (button == UI_UP || button == UI_DOWN)
        changed = archiveScroll(screen == SCR_STATUS, button == UI_UP ? -1 : 1) || changed;
      else if (button == UI_OK && screen == SCR_ARCHIVE && archiveLevelFocused()) {enter(SCR_STATUS);return;}
      break;
    case SCR_MEMORIAL: case SCR_ADOPTION:
      if (button == UI_LEFT) back();
      else if (changed) screenRedraw();
      return;
    case SCR_CAPTURE:
      // Read-only placeholder. Future phone customization has no local write path.
      break;
    case SCR_CALENDAR:
      if (button == UI_OK && !clockIsSet()) {
        if (timeRequestPending) return;
        timeRequestPending = true; timeRequestAt = millis();
        // 未校时只能请求宿主机提供真实时间；不猜测日期，也不进入编辑焦点。
        Serial.println("Q:TIME");
        message(u8"已请求电脑校时");
        return;
      }
      if (!clockIsSet()) break;
      if (calendarFocus == CAL_BROWSE) calendarFocus=CAL_DAYS;
      if (calendarFocus == CAL_MONTH) {
        if (button == UI_LEFT || button == UI_RIGHT) {
          const int delta = button == UI_LEFT ? -1 : 1;
          if ((delta < 0 && selected.year == 2020 && selected.month == 1) ||
              (delta > 0 && selected.year == 2099 && selected.month == 12)) break;
          Date next = shiftMonth(selected, delta);
          if (selected.day > monthDays(next.year, next.month)) next.day = 1;
          selected = next; calendarMonthChanged = true; changed = true;
        } else if (button == UI_OK) {
          taskRow = 0; enter(SCR_TASKS); return;
        } else if (button == UI_DOWN) {
          const ClockSnapshot now = clockNow();
          if (!calendarMonthChanged && now.valid && selected.year == now.date.year &&
              selected.month == now.date.month) selected = now.date;
          calendarFocus = CAL_DAYS; changed = true;
        }
      } else {
        if (button == UI_LEFT || button == UI_RIGHT) {
          Date next = shiftDay(selected, button == UI_LEFT ? -1 : 1);
          changed = changed || code(next) != code(selected); selected = next;
        } else if (button == UI_UP || button == UI_DOWN) {
          if (button == UI_UP && selected.day <= 7) {
            calendarFocus = CAL_MONTH; calendarMonthChanged = false; changed = true;
          } else {
            Date next = shiftDay(selected, button == UI_UP ? -7 : 7);
            changed = changed || code(next) != code(selected); selected = next;
          }
        }
        else if (button == UI_OK) { taskRow = 0; enter(SCR_TASKS); return; }
      }
      break;
    case SCR_TASKS: {
      auto* r = orchardDay(code(selected));
      if (button == UI_LEFT) { back(); return; }
      if (!r) {
        if (button == UI_OK) {
          if (!orchardDay(code(selected), true)) message(u8"记录已满，最多保存九十天");
          else savedMessage();
          return;
        }
      } else {
        if (button == UI_UP) { taskRow = (taskRow + TASK_COUNT - 1) % TASK_COUNT; changed = true; }
        else if (button == UI_DOWN) { taskRow = (taskRow + 1) % TASK_COUNT; changed = true; }
        else if (button == UI_OK) { guideStep=0; craftRow=r->craft; enter(taskRow==3?SCR_CRAFT:SCR_GUIDE); return; }
      }
      break;
    }
    case SCR_CRAFT:
      if (button == UI_UP) { craftRow = (craftRow + 4) % 5; changed = true; }
      else if (button == UI_DOWN) { craftRow = (craftRow + 1) % 5; changed = true; }
      else if (button == UI_LEFT) { back(); return; }
      else if (button == UI_OK) { enter(SCR_GUIDE); return; }
      break;
    case SCR_GUIDE:
      if (button == UI_LEFT) { back(); return; }
      if (button == UI_UP && guideStep) { --guideStep; changed=true; }
      else if (button == UI_DOWN && guideStep<2) { ++guideStep; changed=true; }
      else if (button == UI_OK) { if(guideStep<2){++guideStep;changed=true;} else {enter(SCR_PRACTICE);return;} }
      break;
    case SCR_PRACTICE:
      if(button==UI_LEFT){resume(SCR_GUIDE);return;}
      else if(button==UI_OK){enter(SCR_CONFIRM);return;}
      break;
    case SCR_CONFIRM:
      if (button == UI_LEFT || button == UI_RIGHT) { confirmChoice = 1 - confirmChoice; changed = true; }
      else if (button == UI_OK) {
        if (!confirmChoice) enter(SCR_GUIDE);
        else {
          auto* r = orchardDay(code(selected));
          if (r) {
            const auto before=*r;
            bool recordChanged = !(r->reserved & (1<<taskRow)) || (taskRow==3 && r->craft!=craftRow);
            r->reserved |= 1<<taskRow; if(taskRow==3) r->craft=craftRow;
            bool saved = !recordChanged || orchardSave();
            if(!saved){*r=before;message(u8"保存失败，请重试");return;}
            enter(SCR_TASKS,u8"已完成");
          }
        }
        return;
      }
      break;
    case SCR_FRIDGE:
      if (button==UI_UP || button==UI_DOWN) {
        if (foodColumn==FOOD_STOCK) {
          const int value=foodValue+(button==UI_UP?1:-1);
          const uint8_t next=value<0?0:value>fridge::LIMIT?fridge::LIMIT:value;
          changed=changed || next!=foodValue;foodValue=next;
        } else {
          const uint8_t next=button==UI_UP?0:5;
          changed=changed || next!=foodIndex;foodIndex=next;
        }
      }
      else if (button==UI_RIGHT) {
        if (foodColumn==FOOD_NAME) {beginInventoryEdit();return;}
        if (foodColumn==FOOD_STOCK) {foodColumn=FOOD_EATEN;changed=true;}
      } else if (button==UI_LEFT) {
        if (foodColumn==FOOD_EATEN) {beginInventoryEdit();return;}
        if (foodColumn==FOOD_STOCK) {foodColumn=FOOD_NAME;changed=true;}
      } else if (button==UI_OK) {
        if (foodColumn==FOOD_NAME) {beginInventoryEdit();return;}
        if (foodColumn==FOOD_STOCK) {commitInventoryEdit();return;}
        foodColumn=FOOD_NAME;changed=true;
      }
      break;
    case SCR_FRIDGE_ITEM:
      if (button==UI_UP || button==UI_DOWN) {
        foodAction=(fridge::Action)((foodAction+(button==UI_UP?4:1))%5);
        changed = true;
      }
      else if (button==UI_OK) {beginFoodEdit();return;}
      else if (button==UI_LEFT) {back();return;}
      break;
    case SCR_FRIDGE_EDIT:
      if (button==UI_OK) {commitFoodEdit();return;}
      if (button==UI_LEFT || button==UI_RIGHT || button==UI_UP || button==UI_DOWN) {
        int step=(button==UI_UP || button==UI_DOWN)?10:1;
        int value=foodValue+((button==UI_LEFT || button==UI_DOWN)?-step:step);
        int minimum=foodAction<=fridge::RESTOCK?1:0;
        uint8_t next=value<minimum?minimum:value>fridge::LIMIT?fridge::LIMIT:value;
        changed = changed || next != foodValue; foodValue = next;
      }
      break;
    case SCR_TIMER:
      if (button == UI_LEFT || button == UI_RIGHT) {
        timerAction = button == UI_LEFT ? 0 : 1; changed = true;
      } else if (button == UI_OK && timerAction == 0) {
        changed = changed || timer.running || timer.finished || timer.remainingMs != timer.durationMs;
        timer.reset();
      } else if (button == UI_OK) {
        if (timer.toggle(millis())) {
          timerAlert = true;
          audioRequest(SOUND_TIMER_DONE, curScreen() == SCR_PET);
        }
        changed = true;
      } else if (button == UI_UP || button == UI_DOWN) {
        if (timer.running) { message(u8"请先暂停再调整时长"); return; }
        int value = timer.durationMs / 60000 + (button == UI_UP ? 1 : -1);
        customMinutes = value < 1 ? 1 : value > 180 ? 180 : value;
        uint32_t duration = customMinutes * 60000UL;
        changed = changed || timer.durationMs != duration ||
          timer.remainingMs != duration || timer.finished;
        timerPreset = 3; timer.setMinutes(customMinutes);
      }
      break;
    default: break;
  }
  if (changed) screenRedraw();
}

void orchardTick() {
  uint32_t now = millis();
  if (timer.tick(now)) {
    timerAlert = true;
    audioRequest(SOUND_TIMER_DONE, curScreen() == SCR_PET);
  }
  // A reminder interrupts raw acquisition immediately, before the 1 Hz UI work.
  if (curScreen() != SCR_ALERT && timerAlert) {
    alertReturn = curScreen(); timerAlert = false;
    enter(SCR_ALERT); return;
  }
  if (now - lastRefresh < 1000) return;
  lastRefresh = now;
  ensureDate();
  static bool homeRequested=false;
  static uint32_t homeRequestAt=0;
  if (curScreen()==SCR_TODAY && !clockIsSet() &&
      (!homeRequested || now-homeRequestAt>=10000)) {
    homeRequested=true;homeRequestAt=now;Serial.println("Q:TIME");
  }
  bool fridgeDayChanged=syncFridgeDate();
  bool dirty = fridgeDayChanged && fridgeScreen(curScreen());
  if (timeRequestPending && (clockIsSet() || now - timeRequestAt >= 5000)) {
    timeRequestPending = false;
    if (curScreen() == SCR_CALENDAR) {
      toast = clockIsSet() ? u8"校时成功" : u8"校时超时，请检查电脑服务";
      toastAt = now; dirty = true;
    }
  }
  if (toast && now - toastAt >= 4000) { toast = nullptr; dirty = true; }
  const uint32_t today = dateCode();
  if (lastDate != today) {
    lastDate = today;
    // 首次校时和跨日只刷新显示，不改用户正在浏览的日期/草稿。
    if (curScreen() == SCR_TODAY || curScreen() == SCR_CALENDAR ||
        fridgeScreen(curScreen()) || curScreen() == SCR_STATUS || curScreen() == SCR_ADOPTION ||
        curScreen() == SCR_MEMORIAL) dirty = true;
  }
  if (curScreen() == SCR_TIMER && timer.running &&
      (timer.remainingMs + 999) / 1000 != timerDrawnSeconds) dirty = true;
  if (dirty) screenRedraw();
}

// 串口只读快照。K: 命令仍通过同一按键逻辑，不直接改业务状态。
void orchardDumpInfo() {
  // Seeed Print::printf has a short fixed buffer; format long JSON explicitly.
  char buffer[768];
  snprintf(buffer, sizeof buffer, "U:{\"fw\":\"vibe-pet-v1\",\"screen\":%u,\"petState\":%u,\"physicalKeys\":%lu,\"localDate\":%lu,\"selected\":%lu,"
    "\"records\":%u,\"sequence\":%u,\"batch\":%u,\"timerMs\":%lu,"
    "\"timerDuration\":%lu,\"running\":%u,\"finished\":%u,\"calendarEdit\":%u,\"calendarFocus\":%u,"
    "\"archiveRow\":%u,\"treeRow\":%u,\"captureRow\":%u,\"captureBatch\":%u,"
    "\"farmCrc\":%lu,\"fridgeDate\":%lu,\"fridgeSequence\":%u,\"fridgeItem\":%u,"
    "\"fridgeColumn\":%u,\"fridgeAction\":%u,\"fridgeValue\":%u,\"fridgeUndo\":%u,\"fridgeSaveOk\":%u,\"foods\":[",
    curScreen(), (unsigned)gState, (unsigned long)gPhysicalKeyCount, (unsigned long)dateCode(), (unsigned long)code(selected),
    orchardData.count, orchardData.sequence, orchardData.batch,
    (unsigned long)timer.remainingMs, (unsigned long)timer.durationMs,
    timer.running, timer.finished, calendarFocus != CAL_BROWSE, (unsigned)calendarFocus,
    archiveRow, treeRow, captureRow, captureBatch,
    (unsigned long)orchardData.crc,(unsigned long)fridgeData.date,fridgeData.sequence,foodIndex,
    (unsigned)foodColumn,(unsigned)foodAction,foodValue,fridgeData.undo.valid,fridgeLastSaveOk);
  Serial.print(buffer);
  for (uint8_t i = 0; i < fridge::FOOD_COUNT; ++i) {
    const auto& f = fridgeData.items[i];
    snprintf(buffer, sizeof buffer, "%s{\"stock\":%u,\"eaten\":%u,\"incoming\":%u}",
      i ? "," : "", f.stock, f.eaten, f.incoming);
    Serial.print(buffer);
  }
  snprintf(buffer,sizeof buffer,R"(],"timerAction":%u,"archiveScroll":%d,"detailScroll":%d,"archiveFocused":%u})",
    timerAction,
    archiveScrollOffset(),archiveScrollOffset(true),archiveLevelFocused()?1u:0u);
  Serial.println(buffer);
}

