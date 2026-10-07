// 编译真实 screens/orchard/store，仅替换显示、millis 与 flash 硬件（日期换算使用真实 clock.cpp）。
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <ctime>
#include <csignal>
#include <cstdlib>
#include "Arduino.h"
#include "Wire.h"
TwoWire Wire1;
#include "Seeed_SFUD.h"
#include "../firmware/src/pet_data.h"
uint32_t fakeMillis=0;
bool fsOk=true, failWrites=false;
std::map<std::string,std::vector<uint8_t>> fakeFiles;
FakeSerial Serial; FakeSFUD SFUD; TFT_eSPI tft; PetSprite fb(&tft);
// Record buzzer output while exercising the real audio manager and timer UI.
constexpr uint8_t WIO_BUZZER=0, OUTPUT=1, LOW=0;
std::vector<unsigned> playedTones;
bool buzzerOn=false;
void pinMode(uint8_t,uint8_t) {}
void digitalWrite(uint8_t,uint8_t) {buzzerOn=false;}
void tone(uint8_t,unsigned hz) {playedTones.push_back(hz);buzzerOn=true;}
void noTone(uint8_t) {buzzerOn=false;}
#include "../firmware/src/audio_manager.cpp"
const int16_t PET_X=80,PET_Y=26,PET_W=160,PET_H=186;
uint32_t fakeMicrosTicks=0,fakePetDraws=0;
uint32_t micros() {return fakeMillis*1000+fakeMicrosTicks++;}
uint32_t fakeSunTotal=32*256;
uint16_t fakeSunToday=12*256,fakeSunCap=100,fakeSunRaw=2600;
int8_t fakeSunZone=2;
uint8_t fakeStage=0,fakeMileCount=1;
bool fakeAdopted=true;
PetState gState = ST_IDLE;
uint32_t gPhysicalKeyCount = 0;
char gPetName[13]="PINGPING";
const uint32_t STAGE_NEED[3]={300,1400,6000};
const StageSkin STAGES[4]={{0x6da8,0x23c4,0xce79,0,u8"青苹果"},{0xf800,0x8800,0xfd20,0,u8"红苹果"},{0xfc9f,0xb800,0xffff,DEC_SPARK,u8"亮苹果"},{0xfea0,0x9d20,0xffe0,DEC_HALO|DEC_SPARK,u8"金苹果"}};
const ZoneSpec ZONES[5]={{0,0,"DARK",0},{400,19,"DIM",0},{2400,77,"GOOD",0x7e0},{3300,31,"HIGH",0},{3950,-64,"HOT",0}};
#include "../firmware/src/clock.cpp"
uint8_t saveStage() {return fakeStage;}
bool saveFsOk() {return fsOk;}
bool saveAdopted() {return fakeAdopted;}
uint32_t saveAdoptEpoch() {return 1790899200;}
uint32_t sunTotalQ8() {return fakeSunTotal;}
uint16_t sunTodayQ8() {return fakeSunToday;}
uint16_t sunCapPts() {return fakeSunCap;}
uint16_t sunRaw() {return fakeSunRaw;}
int8_t sunZone() {return fakeSunZone;}
uint8_t saveMileCount() {return fakeMileCount;}
const Milestone* saveMile(uint8_t index) {
  static Milestone rows[8];
  assert(index<8);rows[index]={1790899200+static_cast<uint32_t>(index)*86400,static_cast<uint8_t>(index%4),{0,0,0}};
  return &rows[index];
}
void drawChrome(bool clearBody) {
  auto& canvas=uiCanvas();
  if(clearBody) canvas.fillScreen(TFT_BLACK);
  canvas.fillRect(0,0,320,22,TFT_NAVY);canvas.setTextColor(TFT_WHITE,TFT_NAVY);
  canvas.setTextDatum(ML_DATUM);canvas.drawString(gPetName,6,11,2);
}
void drawBottomBar() {uiCanvas().fillRect(0,216,320,24,TFT_DARKGREY);}
void drawPet(uint32_t) {
  if(curScreen()==SCR_ARCHIVE)uiCanvas().ops.clear(); // No stale draw-command traces in the host-only pet placeholder.
  ++fakePetDraws;fb.fillSprite(curScreen()==SCR_CAPTURE?0xF79D:curScreen()==SCR_TREE?TFT_WHITE:TFT_BLACK);
  if(curScreen()==SCR_TREE||curScreen()==SCR_CAPTURE) {
    for(int y=0;y<PET_H;++y) for(int x=0;x<PET_W;++x)
      fb.drawPixel(x,y,static_cast<uint16_t>(1+y*PET_W+x));
    fb.pushSprite(PET_X,PET_Y);return;
  }
  fb.fillRect(0,0,PET_W,PET_H,TFT_GREEN);fb.pushSprite(curScreen()==SCR_TODAY||curScreen()==SCR_ARCHIVE?160:PET_X,curScreen()==SCR_ARCHIVE?archivePetY():curScreen()==SCR_TODAY?30:PET_Y);
  if(curScreen()==SCR_ARCHIVE)uiCanvas().fillRect(160,archiveview::TOP,160,PET_H-archiveScrollOffset(),TFT_GREEN);
}
#include "../firmware/src/ui_canvas.cpp"
#include "../firmware/src/fridge_store.cpp"
#include "../firmware/src/orchard_store.cpp"
#include "../firmware/src/ui_hints.cpp"
#include "../firmware/src/archive.cpp"
#include "../firmware/src/orchard.cpp"
#include "../firmware/src/screens.cpp"
static void press(UiButton button) {screenButton(button);}
static void shot(const char* name) {tft.snapshot((std::string("artifacts/ui/")+name+".json").c_str());}
static void tick() {fakeMillis+=1000;orchardTick();}
static void chooseFood(uint8_t index) {
  enter(SCR_FRIDGE);
  for(uint8_t n=0;foodIndex!=index&&n<6;++n) press(foodIndex<index?UI_DOWN:UI_UP);
  assert(foodIndex==index);
  press(UI_OK);assert(curScreen()==SCR_FRIDGE_ITEM);
}
static void openFoodAction(uint8_t index,fridge::Action action) {
  chooseFood(index);
  for(uint8_t n=0;static_cast<unsigned>(foodAction)!=static_cast<unsigned>(action)&&n<5;++n) press(UI_DOWN);
  assert(static_cast<unsigned>(foodAction)==static_cast<unsigned>(action));
  press(UI_OK);assert(curScreen()==SCR_FRIDGE_EDIT);
}
static void setFoodValue(uint8_t value) {
  for(unsigned n=0;foodValue!=value&&n<120;++n) {
    int diff=static_cast<int>(value)-static_cast<int>(foodValue);
    press(diff>=10?UI_UP:diff<=-10?UI_DOWN:diff>0?UI_RIGHT:UI_LEFT);
  }
  assert(foodValue==value);
}
static void foodEdit(uint8_t index,fridge::Action action,uint8_t value) {
  openFoodAction(index,action);setFoodValue(value);press(UI_OK);
}
static std::map<std::string,std::vector<uint8_t>> protectedFiles() {
  std::map<std::string,std::vector<uint8_t>> result;
  for(const char* path:{"/pet0.dat","/pet1.dat","/farm0.dat","/farm1.dat"}) {
    auto found=fakeFiles.find(path);if(found!=fakeFiles.end()) result.insert(*found);
  }
  return result;
}
struct DrawCounts {uint32_t direct,submits,frames;};
static DrawCounts drawCounts() {return {tft.directDrawCount,tft.spriteSubmitCount,uiFrameCount()};}
static void expectFrames(const DrawCounts& before,uint32_t expected,const char* operation) {
  const DrawCounts after=drawCounts();
  if(after.direct!=before.direct||after.submits-before.submits!=expected||after.frames-before.frames!=expected) {
    std::cerr<<operation<<": LCD primitives="<<after.direct-before.direct
      <<", sprite submissions="<<after.submits-before.submits<<", complete frames="<<after.frames-before.frames
      <<", expected frames="<<expected<<std::endl;
    assert(false);
  }
}
static void testCanvasInitialization() {
  // The display spy itself must not lose evidence when a later fill clears ops.
  TFT_eSPI probe;
  probe.drawPixel(1,1,TFT_WHITE);probe.fillScreen(TFT_BLACK);probe.fillScreen(TFT_WHITE);
  assert(probe.directDrawCount==3&&probe.spriteSubmitCount==0&&probe.ops.size()==1);
  probe.drawPixel(2,2,TFT_BLACK);probe.fillRect(0,0,320,240,TFT_WHITE);
  assert(probe.directDrawCount==5&&probe.spriteSubmitCount==0&&probe.ops.size()==1);
  assert(!uiCanvasReady());
  const DrawCounts start=drawCounts();
  assert(uiCanvasInit()&&uiCanvasReady());
  assert(fb.getPointer()==displayPixels&&pageSprite.getPointer()==displayPixels);
  assert(fb.width()==PET_W&&fb.height()==PET_H&&pageSprite.width()==320&&pageSprite.height()==240);
  assert(fb.spriteCreateCount==1&&pageSprite.spriteCreateCount==1);
  displayPixels[0]=0x1357;
  assert(uiCanvasInit()&&displayPixels[0]==0x1357);
  assert(fb.spriteCreateCount==1&&pageSprite.spriteCreateCount==1);
  expectFrames(start,0,"canvas init is idempotent and never writes LCD");

  // Exact overlapping copy regression: every source row differs, so a wrong
  // copy direction or compact/full-width stride corrupts a visible pixel.
  const uint16_t guard=0xA55A;
  std::fill(displayPixels,displayPixels+320*240+1,guard);
  std::vector<uint16_t> compact(static_cast<size_t>(PET_W)*PET_H);
  for(int y=0;y<PET_H;++y) for(int x=0;x<PET_W;++x) {
    const uint16_t value=static_cast<uint16_t>(1+(y*197+x)%65534);
    compact[y*PET_W+x]=value;displayPixels[y*PET_W+x]=value;
  }
  assert(&uiCanvas()==&tft);
  uiFrameBegin();uiFrameBegin(); // Nested begin must not discard the first frame.
  assert(&uiCanvas()==static_cast<TFT_eSPI*>(&pageSprite));
  fb.pushSprite(PET_X,PET_Y);
  expectFrames(start,0,"pet composes into arena without LCD submission");
  for(int y=0;y<240;++y) for(int x=0;x<320;++x) {
    const bool inside=x>=PET_X&&x<PET_X+PET_W&&y>=PET_Y&&y<PET_Y+PET_H;
    // The default composing path fills the surroundings with the page
    // background: white on the home page, black elsewhere (ui_canvas.cpp).
    const uint16_t pageBg=curScreen()==SCR_TODAY?TFT_WHITE:TFT_BLACK;
    const uint16_t expected=inside?compact[(y-PET_Y)*PET_W+x-PET_X]:pageBg;
    assert(displayPixels[y*320+x]==expected);
  }
  assert(displayPixels[320*240]==guard);
  uiFrameEnd();uiFrameEnd();
  expectFrames(start,1,"a composed pet frame commits exactly once");
  assert(uiFrameLastMicros()>0 && &uiCanvas()==&tft);
  assert(tft.readPixel(PET_X,PET_Y)==compact.front());
  assert(tft.readPixel(PET_X+PET_W-1,PET_Y+PET_H-1)==compact.back());
  assert(tft.readPixel(0,PET_Y)==(curScreen()==SCR_TODAY?TFT_WHITE:TFT_BLACK));

  // The existing animation path still submits its compact sprite outside a
  // page transaction, without falsely incrementing the page-frame counter.
  const DrawCounts animation=drawCounts();
  fb.fillSprite(TFT_GREEN);fb.pushSprite(PET_X,PET_Y);
  assert(tft.directDrawCount==animation.direct&&tft.spriteSubmitCount==animation.submits+1);
  assert(uiFrameCount()==animation.frames&&tft.readPixel(PET_X,PET_Y)==TFT_GREEN);
  std::cout<<"PASS shared arena binding, idempotent init, overlapping pet composition and animation push"<<std::endl;
}
static void testPageFrameRegression() {
  // Paused/finished timers are static; a running visible second changes once.
  toast=nullptr;timer.setMinutes(1);timerAlert=false;
  auto before=drawCounts();enter(SCR_TIMER);expectFrames(before,1,"enter timer");
  before=drawCounts();for(int i=0;i<3;++i) tick();expectFrames(before,0,"paused timer stays still");
  before=drawCounts();press(UI_OK);expectFrames(before,1,"start timer");
  before=drawCounts();tick();expectFrames(before,1,"running timer visible second");
  before=drawCounts();fakeMillis+=100;orchardTick();expectFrames(before,0,"same timer second");
  before=drawCounts();press(UI_OK);expectFrames(before,1,"pause timer");
  before=drawCounts();for(int i=0;i<3;++i) tick();expectFrames(before,0,"paused remainder stays still");
  timer.setMinutes(1);screenRedraw();press(UI_OK);
  before=drawCounts();fakeMillis+=61000;orchardTick();
  assert(curScreen()==SCR_ALERT);expectFrames(before,1,"timer completion alert");
  before=drawCounts();press(UI_OK);assert(curScreen()==SCR_TIMER);expectFrames(before,1,"dismiss completion");
  assert(timer.finished&&!timer.running);
  before=drawCounts();for(int i=0;i<3;++i) tick();expectFrames(before,0,"finished timer stays still");

  before=drawCounts();enter(SCR_STATUS);expectFrames(before,1,"status initial frame");
  before=drawCounts();fakeMillis+=1000;screenTick1Hz();expectFrames(before,0,"status first stable tick");
  before=drawCounts();for(int i=0;i<3;++i) {fakeMillis+=1000;screenTick1Hz();}
  expectFrames(before,0,"unchanged status stays still");
  before=drawCounts();fakeSunToday+=256;fakeMillis+=1000;screenTick1Hz();expectFrames(before,1,"changed status value");
  before=drawCounts();fakeMillis+=1000;screenTick1Hz();expectFrames(before,0,"status stable after change");
  before=drawCounts();++fakeSunRaw;fakeMillis+=1000;screenTick1Hz();expectFrames(before,1,"changed status light");

  fakeMileCount=8;memoTop=0;
  before=drawCounts();enter(SCR_MEMORIAL);expectFrames(before,1,"memorial initial frame");
  before=drawCounts();press(UI_DOWN);assert(memoTop==1);expectFrames(before,1,"memorial scroll down");
  before=drawCounts();press(UI_UP);assert(memoTop==0);expectFrames(before,1,"memorial scroll up");
  before=drawCounts();press(UI_UP);expectFrames(before,0,"memorial boundary does not redraw");
  fakeMileCount=1;

  // Confirming a successful save must show the final page and toast together,
  // not submit an intermediate page followed by a second toast frame.
  openFoodAction(0,fridge::SET_STOCK);
  const uint8_t stock=fridgeData.items[0].stock;
  setFoodValue(stock==fridge::LIMIT?stock-1:stock+1);
  before=drawCounts();press(UI_OK);
  assert(curScreen()==SCR_FRIDGE&&fridgeLastSaveOk);expectFrames(before,1,"fridge save and toast");
  selected={2026,1,1};enter(SCR_TASKS);
  before=drawCounts();press(UI_BACK);
  assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS);expectFrames(before,1,"return to editable calendar");

  before=drawCounts();const uint32_t petBefore=fakePetDraws;
  enter(SCR_PET);expectFrames(before,1,"return to composed pet page");
  assert(fakePetDraws==petBefore+1);
  assert(tft.readPixel(PET_X,PET_Y)==TFT_GREEN);
  assert(tft.readPixel(PET_X+PET_W-1,PET_Y+PET_H-1)==TFT_GREEN);
  assert(tft.readPixel(0,PET_Y)==TFT_BLACK);
  assert(tft.readPixel(0,0)==TFT_NAVY&&tft.readPixel(0,216)==TFT_DARKGREY);
  assert(uiCanvasInit()&&fb.spriteCreateCount==1&&pageSprite.spriteCreateCount==1);
  std::cout<<"PASS one-frame transitions, stable timer/status, memorial scrolling and pet return"<<std::endl;
}
static void testCalendarInteraction() {
  const auto filesBefore = fakeFiles;
  auto monthBar = [](Date date) {
    enter(SCR_CALENDAR);selected=date;assert(calendarFocus==CAL_BROWSE);
    press(UI_OK);assert(calendarFocus==CAL_MONTH);
  };
  // Enter focuses the month; unchanged current month defaults to real today.
  monthBar({2026,10,15});shot("calendar_month");
  press(UI_UP);assert(calendarFocus==CAL_MONTH&&code(selected)==20261015);
  press(UI_DOWN);assert(calendarFocus==CAL_DAYS&&code(selected)==20261002);
  shot("calendar_today_selected");
  bool todayContrast=false;
  for(const auto& op:tft.ops)
    if(op.find("[\"roundOutline\"")==0&&op.find(",65535]")!=std::string::npos) todayContrast=true;
  assert(todayContrast); // Today's outline is visible on the selected green cell.

  // Same weekday above/below; moving up out of month focuses header.
  press(UI_DOWN);assert(code(selected)==20261009&&weekday(selected)==4);
  shot("calendar_days");
  press(UI_UP);assert(code(selected)==20261002);
  press(UI_UP);assert(calendarFocus==CAL_MONTH&&code(selected)==20261002);
  press(UI_DOWN);assert(calendarFocus==CAL_DAYS&&code(selected)==20261002);
  press(UI_LEFT);assert(code(selected)==20261001);
  press(UI_LEFT);assert(code(selected)==20260930);
  press(UI_RIGHT);assert(code(selected)==20261001);
  selected={2026,10,26};press(UI_DOWN);
  assert(code(selected)==20261102&&weekday(selected)==0&&calendarFocus==CAL_DAYS);
  press(UI_UP);assert(calendarFocus==CAL_MONTH&&code(selected)==20261102);
  press(UI_DOWN);assert(calendarFocus==CAL_DAYS&&code(selected)==20261102); // Other month stays.

  // Changing month preserves day, except a missing day becomes the first.
  monthBar({2026,1,31});press(UI_RIGHT);assert(code(selected)==20260201);
  press(UI_DOWN);assert(calendarFocus==CAL_DAYS&&code(selected)==20260201);
  monthBar({2024,1,29});press(UI_RIGHT);assert(code(selected)==20240229);
  monthBar({2024,1,31});press(UI_RIGHT);assert(code(selected)==20240201);
  monthBar({2026,12,15});press(UI_RIGHT);assert(code(selected)==20270115);
  press(UI_LEFT);assert(code(selected)==20261215);
  monthBar({2026,10,15});press(UI_RIGHT);press(UI_LEFT);press(UI_DOWN);
  assert(code(selected)==20261015); // Changed months, even when back in current month.

  // Both header and date cursor survive shortcuts and task-list return.
  press(UI_OK);assert(curScreen()==SCR_TASKS);press(UI_BACK);
  assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS&&code(selected)==20261015);
  press(UI_TIMER);assert(curScreen()==SCR_TIMER);press(UI_TIMER);
  assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS&&code(selected)==20261015);
  press(UI_BACK);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_BROWSE);
  press(UI_RIGHT);assert(curScreen()==SCR_TREE); // C restored page navigation.
  monthBar({2026,10,15});press(UI_RIGHT);
  press(UI_TIMER);press(UI_TIMER);
  assert(calendarFocus==CAL_MONTH&&code(selected)==20261115&&calendarMonthChanged);
  press(UI_BACK);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_BROWSE);
  press(UI_BACK);assert(curScreen()==SCR_TODAY);

  // A new header focus resets the changed-month flag and selects today only in this month.
  monthBar({2026,10,15});press(UI_DOWN);assert(code(selected)==20261002);
  monthBar({2026,11,15});press(UI_DOWN);assert(code(selected)==20261115);
  monthBar({2026,12,31});press(UI_DOWN);press(UI_RIGHT);assert(code(selected)==20270101);
  press(UI_LEFT);assert(code(selected)==20261231);
  monthBar({2020,1,31});press(UI_LEFT);assert(code(selected)==20200131);
  monthBar({2099,12,15});press(UI_RIGHT);assert(code(selected)==20991215);
  selected={2099,12,31};press(UI_DOWN);press(UI_DOWN);assert(code(selected)==20991231);
  monthBar({2020,1,1});press(UI_DOWN);press(UI_UP);assert(calendarFocus==CAL_MONTH);
  assert(fakeFiles==filesBefore); // Browsing and visiting empty lists must not save anything.
  selected=clockNow().date;enter(SCR_TODAY);
  std::cout<<"PASS calendar focus, default today, weekly/day movement, month fallback, returns and independent today marker"<<std::endl;
}

static void testCalendarEntry() {
  const auto filesBefore = fakeFiles;
  const Date today = clockNow().date;
  const uint32_t todayCode = code(today);
  // Normal entry from home and from the tree page discards old browsing dates.
  selected={2025,1,15};enter(SCR_TODAY);press(UI_RIGHT);
  assert(curScreen()==SCR_CALENDAR&&code(selected)==todayCode&&calendarFocus==CAL_BROWSE);
  press(UI_OK);assert(calendarFocus==CAL_MONTH);press(UI_RIGHT);press(UI_DOWN);
  const uint32_t browsed=code(selected);assert(browsed!=todayCode);
  // Returning from the date's task list keeps its date and editing focus.
  press(UI_OK);assert(curScreen()==SCR_TASKS);press(UI_BACK);
  assert(curScreen()==SCR_CALENDAR&&code(selected)==browsed&&calendarFocus==CAL_DAYS);
  press(UI_TIMER);press(UI_TIMER);
  assert(curScreen()==SCR_CALENDAR&&code(selected)==browsed&&calendarFocus==CAL_DAYS);
  // C only exits edit mode; actual page departure and re-entry selects today.
  press(UI_BACK);assert(calendarFocus==CAL_BROWSE&&code(selected)==browsed);
  press(UI_RIGHT);assert(curScreen()==SCR_TREE);press(UI_LEFT);
  assert(curScreen()==SCR_CALENDAR&&code(selected)==todayCode&&calendarFocus==CAL_BROWSE);
  press(UI_OK);press(UI_DOWN);press(UI_DOWN);const uint32_t nextWeek=code(selected);
  // Alert dismissal is another resume path, not a new visit to the calendar.
  alertReturn=SCR_CALENDAR;enter(SCR_ALERT);press(UI_OK);
  assert(curScreen()==SCR_CALENDAR&&code(selected)==nextWeek&&calendarFocus==CAL_DAYS);
  press(UI_BACK);press(UI_BACK);assert(curScreen()==SCR_TODAY);
  press(UI_OK);assert(curScreen()==SCR_CALENDAR&&code(selected)==todayCode);
  assert(fakeFiles==filesBefore);
  std::cout<<"PASS calendar entry defaults to today; task/timer/alert resume and edit exit preserve selection\n";
}

// 果园活动排期为编译期常量，纯日期逻辑直接在主机断言，无需硬件。
static void testVisitSchedule() {
  using namespace visits;
  // 仅起止日画圈；中间日期与活动外日期都不画圈。
  assert(boundary(20260420)&&boundary(20260604)&&boundary(20260618)&&boundary(20261016));
  assert(boundary(20261030)&&boundary(20261102)&&boundary(20261210)&&boundary(20261220));
  assert(!boundary(20260610)&&!boundary(20261025)&&!boundary(20261003)&&!boundary(20251231));
  // 十月下旬两项活动共用 10-30，月历对该日仍只画一个圈。
  unsigned on1030=0;for(const auto&a:SCHEDULE)if(contains(a,20261030))++on1030;
  assert(on1030==2);
  // 日序号按本地日历天数计算：跨年相邻一天、闰年二月与平年二月各不相同。
  assert(ordinal(20270101)-ordinal(20261231)==1);
  assert(ordinal(20240301)-ordinal(20240228)==2);
  assert(ordinal(20270301)-ordinal(20270228)==1);
  // 以演示今天 10-03 前后检查采摘时段(10-16..11-02)的倒计时阶段。
  const auto& pick=SCHEDULE[3];
  assert(pick.id==4&&pick.start==20261016&&pick.end==20261102);
  assert(phase(pick,20261003)==BEFORE&&remaining(pick,20261003)==13);
  assert(phase(pick,20261016)==START&&phase(pick,20261025)==DURING&&remaining(pick,20261025)==8);
  assert(phase(pick,20261102)==END&&phase(pick,20261103)==PAST);
  assert(phase(SCHEDULE[4],20261030)==SINGLE);
  assert(phase(pick,0)==UNKNOWN&&remaining(pick,0)==0); // 未校时不猜测天数。
  // 进行中的活动优先；同时进行中取更早结束的一项。
  assert(next(20261003)==&SCHEDULE[3]&&next(20261020)==&SCHEDULE[3]);
  assert(next(20261030)==&SCHEDULE[4]&&next(20261031)==&SCHEDULE[3]);
  assert(next(20260410)==&SCHEDULE[0]&&next(20260610)==&SCHEDULE[2]);
  assert(remaining(*next(20260610),20260610)==8);
  // 演示排期固定 2026 年，不递推下一年；未校时没有活动。
  assert(next(20261225)==nullptr&&next(20270105)==nullptr&&next(0)==nullptr);
  std::cout<<"PASS visit boundaries, ordinal day math, countdown phases and active-first ordering"<<std::endl;
}

#include "archive_cases.h"
#include "fridge_inventory_cases.h"
#include "tree_settings_cases.h"
#include "tree_model_cases.h"
#include "life_timer_cases.h"
#include "timer_actions_cases.h"
#include "footer_icons_cases.h"
int main(int argc, char** argv) {
  if(argc==2&&std::strcmp(argv[1],"--pet-backgrounds")==0) {
    assert(petbackground::selected()==0);
    screensInit(); enter(SCR_TODAY); press(UI_OK);
    assert(curScreen()==SCR_PET);
    press(UI_LEFT); assert(curScreen()==SCR_PET&&petbackground::selected()==2);
    press(UI_RIGHT); assert(petbackground::selected()==0);
    press(UI_RIGHT); assert(petbackground::selected()==1);
    press(UI_RIGHT); assert(petbackground::selected()==2);
    press(UI_RIGHT); assert(petbackground::selected()==0);
    press(UI_LEFT); press(UI_OK); assert(curScreen()==SCR_TODAY);
    press(UI_OK); assert(curScreen()==SCR_PET&&petbackground::selected()==2);
    enter(SCR_TREE); treeRow=0; press(UI_OK); press(UI_LEFT);
    assert(curScreen()==SCR_PET&&petbackground::selected()==1);
    press(UI_OK); assert(curScreen()==SCR_TREE);
    std::cout << "PASS pet backgrounds: default, both wraps, three choices, retained selection, return to home/tree" << std::endl;
    return 0;
  }
  if(argc==2&&std::strcmp(argv[1],"--footer-icons")==0) {testFooterIcons();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--timer-actions")==0) {testTimerActions();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--life-timer")==0) {testLifeTimer();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--tree-model")==0) {testTreeModel();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--tree-settings")==0) {testTreeSettings();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--fridge-inventory")==0) {testFridgeInventory();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--archive")==0) {testArchive();return 0;}
  if(argc==2&&std::strcmp(argv[1],"--input-split")==0) {
    assert(uiCanvasInit());clockInit();clockSet(1790956800);screensInit();tick();
    const auto files=fakeFiles;
    for(uint8_t i=0;i<5;++i){
      enter(ROOTS[i]);press(UI_TIMER);assert(curScreen()==ROOTS[(i+4)%5]);press(UI_FARM);assert(curScreen()==ROOTS[i]);
      for(int j=0;j<5;++j){press(UI_TIMER);}assert(curScreen()==ROOTS[i]);
      for(int j=0;j<5;++j){press(UI_FARM);}assert(curScreen()==ROOTS[i]);
      press(UI_BACK);assert(curScreen()==ROOTS[i]);
    }
    for(uint8_t page:{SCR_TODAY,SCR_TREE,SCR_FRIDGE}) {
      enter(page);press(UI_LEFT);assert(curScreen()==page);press(UI_RIGHT);assert(curScreen()==page);
    }
    enter(SCR_TODAY);press(UI_OK);assert(curScreen()==SCR_PET&&navigationRoot(SCR_PET)==SCR_TODAY);press(UI_TIMER);assert(curScreen()==SCR_TODAY);
    enter(SCR_TREE);treeRow=0;press(UI_OK);assert(curScreen()==SCR_PET&&navigationRoot(SCR_PET)==SCR_TREE);press(UI_TIMER);assert(curScreen()==SCR_TREE);
    enter(SCR_TODAY);press(UI_FARM);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS&&code(selected)==20261003);
    press(UI_RIGHT);assert(code(selected)==20261004&&curScreen()==SCR_CALENDAR);
    press(UI_DOWN);assert(code(selected)==20261011);press(UI_UP);assert(code(selected)==20261004);
    press(UI_UP);assert(calendarFocus==CAL_MONTH);press(UI_RIGHT);assert(code(selected)==20261104);press(UI_DOWN);assert(calendarFocus==CAL_DAYS);
    press(UI_OK);assert(curScreen()==SCR_TASKS);press(UI_TIMER);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS&&code(selected)==20261104);
    press(UI_TIMER);assert(curScreen()==SCR_TODAY);press(UI_FARM);assert(code(selected)==20261003&&calendarFocus==CAL_DAYS);
    selected={2026,1,31};calendarFocus=CAL_MONTH;press(UI_RIGHT);assert(code(selected)==20260201);
    selected={2026,12,31};calendarFocus=CAL_DAYS;press(UI_RIGHT);assert(code(selected)==20270101);
    const uint8_t from[]={SCR_GUIDE,SCR_CRAFT,SCR_CONFIRM,SCR_ARCHIVE,SCR_CAPTURE,SCR_STATUS,SCR_ADOPTION,SCR_MEMORIAL,SCR_FRIDGE_ITEM,SCR_FRIDGE_EDIT};
    const uint8_t to[]={SCR_TASKS,SCR_TASKS,SCR_GUIDE,SCR_TREE,SCR_TREE,SCR_ARCHIVE,SCR_ARCHIVE,SCR_ARCHIVE,SCR_FRIDGE,SCR_FRIDGE_ITEM};
    for(unsigned i=0;i<sizeof from;++i){enter(from[i]);press(UI_TIMER);assert(curScreen()==to[i]);}
    enter(SCR_FRIDGE_EDIT);foodValue=7;press(UI_RIGHT);assert(curScreen()==SCR_FRIDGE_EDIT&&foodValue==8);press(UI_TIMER);assert(curScreen()==SCR_FRIDGE_ITEM);
    enter(SCR_TIMER);timer.setMinutes(3);press(UI_RIGHT);assert(curScreen()==SCR_TIMER);press(UI_LEFT);assert(curScreen()==SCR_TIMER);
    timer.toggle(fakeMillis);const auto remaining=timer.remainingMs;press(UI_FARM);assert(curScreen()==SCR_TODAY&&timer.running&&timer.remainingMs==remaining);
    alertReturn=SCR_CALENDAR;enter(SCR_ALERT);press(UI_FARM);assert(curScreen()==SCR_ALERT);press(UI_TIMER);assert(curScreen()==SCR_CALENDAR);
    assert(fakeFiles==files);
    clockInit();enter(SCR_CALENDAR);const auto before=selected;press(UI_RIGHT);assert(code(selected)==code(before));Serial.output.clear();timeRequestPending=false;press(UI_OK);assert(Serial.output.find("Q:TIME")!=std::string::npos&&curScreen()==SCR_CALENDAR);
    std::cout<<"PASS global A/B only, root wrap, pet origin, direct date focus, day/week/month boundaries, A return, editor isolation, alerts and no writes\n";return 0;
  }

  if(argc==2&&std::strcmp(argv[1],"--nav-cycle")==0) {
    assert(uiCanvasInit());clockInit();clockSet(1790956800);screensInit();tick();
    const auto files=fakeFiles;
    for(uint8_t i=0;i<5;++i){
      enter(ROOTS[i]);press(UI_TIMER);assert(curScreen()==ROOTS[(i+4)%5]);
      press(UI_FARM);assert(curScreen()==ROOTS[i]);
      for(int j=0;j<5;++j){press(UI_TIMER);}assert(curScreen()==ROOTS[i]);
      for(int j=0;j<5;++j){press(UI_FARM);}assert(curScreen()==ROOTS[i]);
    }
    enter(SCR_FRIDGE_EDIT);foodValue=7;press(UI_TIMER);assert(curScreen()==SCR_TREE&&foodValue==7);
    enter(SCR_CALENDAR);calendarFocus=CAL_DAYS;press(UI_TIMER);assert(curScreen()==SCR_TODAY);press(UI_FARM);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_BROWSE);
    enter(SCR_TODAY);press(UI_TIMER);assert(curScreen()==SCR_TIMER);press(UI_BACK);assert(curScreen()==SCR_TODAY);
    timer.setMinutes(3);timer.toggle(fakeMillis);const auto remaining=timer.remainingMs;
    press(UI_TIMER);press(UI_FARM);assert(curScreen()==SCR_TODAY&&timer.running&&timer.remainingMs==remaining);
    alertReturn=SCR_TODAY;enter(SCR_ALERT);press(UI_TIMER);press(UI_FARM);assert(curScreen()==SCR_ALERT);press(UI_BACK);assert(curScreen()==SCR_TODAY);
    assert(fakeFiles==files);std::cout<<"PASS A/B bidirectional ring, wrap boundaries, child routing, calendar entry, C return, timer and storage preservation\n";return 0;
  }

  if(argc==2&&std::strcmp(argv[1],"--nav")==0) {
    assert(uiCanvasInit());clockInit();clockSet(1790956800);screensInit();tick();
    auto files=fakeFiles;
    for(uint8_t root:ROOTS) {
      assert(curScreen()==root);assert(navigationRoot(root)==root);
      bool highlight=false;for(const auto& op:tft.ops)if(op.find("113,2,94,21,4,59260")!=std::string::npos)highlight=true;assert(highlight);assert(tft.readPixel(0,0)==0x32C8);
      press(UI_FARM);
    }
    assert(curScreen()==SCR_TODAY);
    enter(SCR_FRIDGE);press(UI_RIGHT);assert(curScreen()==SCR_TIMER);press(UI_TIMER);assert(curScreen()==SCR_FRIDGE);
    enter(SCR_CALENDAR);press(UI_OK);press(UI_RIGHT);assert(curScreen()==SCR_CALENDAR);press(UI_BACK);press(UI_RIGHT);assert(curScreen()==SCR_TREE);
    const uint8_t children[]={SCR_TASKS,SCR_CRAFT,SCR_GUIDE,SCR_CONFIRM,SCR_PRACTICE,SCR_PET,SCR_ARCHIVE,SCR_CAPTURE,SCR_STATUS,SCR_ADOPTION,SCR_MEMORIAL,SCR_FRIDGE_ITEM,SCR_FRIDGE_EDIT};
    for(uint8_t page:children){
      const uint8_t expected=page==SCR_FRIDGE_ITEM||page==SCR_FRIDGE_EDIT?SCR_FRIDGE:page==SCR_TASKS||page==SCR_CRAFT||page==SCR_GUIDE||page==SCR_CONFIRM||page==SCR_PRACTICE?SCR_CALENDAR:SCR_TREE;
      assert(navigationRoot(page)==expected);enter(page);auto before=drawCounts();screenRedraw();expectFrames(before,1,"shared header composition");assert(tft.readPixel(0,0)==0x32C8);
    }
    alertReturn=SCR_FRIDGE_EDIT;enter(SCR_ALERT);assert(navigationRoot(SCR_ALERT)==SCR_FRIDGE);press(UI_FARM);assert(curScreen()==SCR_ALERT);press(UI_OK);assert(curScreen()==SCR_FRIDGE_EDIT);
    assert(fakeFiles==files);assert(tft.directDrawCount==0);
    std::cout<<"PASS rolling navigation, root cycle, child mapping, calendar focus, timer return, header composition and storage preservation\n";return 0;
  }

  if(argc==2&&std::strcmp(argv[1],"--home")==0) {
    assert(uiCanvasInit());clockInit();screensInit();
    auto originalFiles=fakeFiles;
    Serial.output.clear();tick();assert(Serial.output.find("Q:TIME")!=std::string::npos);
    Serial.output.clear();for(int i=0;i<8;++i)tick();assert(Serial.output.empty());
    tick();tick();assert(Serial.output.find("Q:TIME")!=std::string::npos);
    char summary[80];homeActivity(summary,sizeof summary,0);assert(std::strcmp(summary,u8"等待校时")==0);
    homeActivity(summary,sizeof summary,20261003);assert(std::strcmp(summary,u8"采摘 · 13天后")==0);
    homeActivity(summary,sizeof summary,20261016);assert(std::strcmp(summary,u8"采摘 · 今天开始")==0);
    homeActivity(summary,sizeof summary,20261017);assert(std::strcmp(summary,u8"采摘 · 进行中")==0);
    homeActivity(summary,sizeof summary,20261030);assert(std::strcmp(summary,u8"采摘 · 就在今天")==0);
    homeActivity(summary,sizeof summary,20261102);assert(std::strcmp(summary,u8"采摘 · 今天结束")==0);
    homeActivity(summary,sizeof summary,20270101);assert(std::strcmp(summary,u8"暂无活动")==0);
    clockSet(1790956800);tick();originalFiles=fakeFiles;screenRedraw();assert(clockNow().dateCode==20261003);
    auto before=drawCounts();auto petBefore=fakePetDraws;screenRedraw();expectFrames(before,1,"home composed frame");assert(fakePetDraws==petBefore+1);
    assert(tft.readPixel(319,100)==TFT_GREEN);assert(tft.readPixel(0,100)==TFT_WHITE);
    assert(tft.directDrawCount==0);
    selected={2025,1,1};homeActivity(summary,sizeof summary,dateCode());assert(std::strcmp(summary,u8"采摘 · 13天后")==0);
    press(UI_BACK);assert(curScreen()==SCR_TODAY);
    press(UI_LEFT);assert(curScreen()==SCR_TIMER);press(UI_TIMER);assert(curScreen()==SCR_TODAY);
    press(UI_TIMER);press(UI_BACK);assert(curScreen()==SCR_TODAY);
    press(UI_RIGHT);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_BROWSE&&code(selected)==dateCode());press(UI_BACK);
    press(UI_OK);assert(curScreen()==SCR_CALENDAR);press(UI_BACK);
    press(UI_FARM);assert(curScreen()==SCR_CALENDAR);press(UI_BACK);
    timer.setMinutes(1);timer.toggle(fakeMillis);fakeMillis+=61000;orchardTick();assert(curScreen()==SCR_ALERT);press(UI_OK);assert(curScreen()==SCR_TODAY);
    assert(fakeFiles==originalFiles);
    fakeMillis=0xfffffff0;clockInit();clockSet(1790956800);fakeMillis+=1500;assert(nowEpoch()==1790956801);
    fakeMillis+=60000;assert(nowEpoch()==1790956861);
    fakeMillis+=4000000000UL;assert(nowEpoch()==1794956861);
    fakeMillis+=4000000000UL;assert(nowEpoch()==1798956861);
    std::cout<<"PASS home activity phases, request throttling, navigation, alert return, frame composition, storage preservation and clock wrap\n";
    return 0;
  }

  // Failed assertions must terminate a headless Windows run without a crash dialog.
  std::signal(SIGABRT,[](int){std::_Exit(EXIT_FAILURE);});
  if(argc==2&&std::strcmp(argv[1],"--timer-audio")==0) {
    assert(uiCanvasInit());clockInit();clockSet(1790956800);orchardInit();audioInit();
    auto finishSound=[]() {
      for(int i=0;i<20;++i) {fakeMillis+=300;audioTick(fakeMillis,curScreen()==SCR_PET);}
      assert(!buzzerOn);
    };
    // Foreground completion, repeated ticks, and a fresh run after completion.
    for(int run=0;run<2;++run) {
      enter(SCR_TIMER);timer.setMinutes(1);playedTones.clear();press(UI_OK);
      fakeMillis+=59999;orchardTick();assert(playedTones.empty());
      ++fakeMillis;orchardTick();assert(playedTones.size()==1&&timer.finished);
      finishSound();assert(playedTones.size()==6);
      for(int i=0;i<3;++i) {tick();}
      assert(playedTones.size()==6);
    }
    // Background expiry cancels pending pet embellishments and survives navigation.
    enter(SCR_TIMER);timer.setMinutes(1);press(UI_OK);enter(SCR_PET);
    audioStateChanged(ST_DONE,true);playedTones.clear();fakeMillis+=60000;orchardTick();
    assert(playedTones.size()==1&&timer.finished);
    audioStateChanged(ST_ERROR,true);audioRequest(SOUND_TOOL,true);
    enter(SCR_TIMER);finishSound();assert(playedTones.size()==6);
    // Pause exactly at expiry uses toggle() rather than the background tick.
    timer.setMinutes(1);playedTones.clear();press(UI_OK);fakeMillis+=60000;press(UI_OK);
    assert(timer.finished&&playedTones.size()==1);finishSound();
    // Ordinary pause, reset and duration changes stay silent.
    playedTones.clear();timer.setMinutes(1);press(UI_OK);fakeMillis+=100;press(UI_OK);
    fakeMillis+=60000;orchardTick();assert(playedTones.empty());
    enter(SCR_TIMER);press(UI_LEFT);press(UI_RIGHT);assert(playedTones.empty());
    // Timer and cue elapsed arithmetic also work across millis() rollover.
    fakeMillis=UINT32_MAX-100;timer.setMinutes(1);press(UI_OK);
    fakeMillis+=60000;orchardTick();assert(playedTones.size()==1);finishSound();
    assert(playedTones.size()==6);
    // Pet audio retains its visibility restriction after the alert has finished.
    playedTones.clear();audioStateChanged(ST_DONE,false);assert(playedTones.empty());
    audioStateChanged(ST_TOOL,true);assert(buzzerOn);audioTick(fakeMillis,false);assert(!buzzerOn);
    std::cout<<"PASS timer audio: foreground/background, once-only, restart, priority, toggle expiry, pause/reset, rollover and pet visibility\n";
    return 0;
  }
  if(argc==2&&std::strcmp(argv[1],"--agronomy")==0) {
    assert(uiCanvasInit());clockInit();clockSet(1790956800);orchardInit();
    auto* old=orchardDay(20261003,true);old->completed=7;assert(orchardSave());orchardLoad();
    assert(orchardDay(20261003)->completed==7 && orchardDay(20261003)->reserved==0);
    enter(SCR_CALENDAR);press(UI_OK);press(UI_DOWN);press(UI_RIGHT);press(UI_OK);
    assert(curScreen()==SCR_TASKS && code(selected)==20261004);press(UI_OK);
    assert(orchardDay(20261004));shot("agronomy_list");
    for(int i=0;i<5;i++){
      assert(taskRow==i);press(UI_OK);
      if(i==3){assert(curScreen()==SCR_CRAFT);press(UI_OK);}
      assert(curScreen()==SCR_GUIDE && guideStep==0);
      for(int j=0;j<3;j++){assert(guideStep==j);std::string name="agronomy_"+std::to_string(i)+"_"+std::to_string(j);shot(name.c_str());press(UI_OK);}
      assert(curScreen()==SCR_PRACTICE);press(UI_DOWN);press(UI_OK);
      assert(curScreen()==SCR_CONFIRM);press(UI_OK);assert(curScreen()==SCR_GUIDE);
      assert(!(orchardDay(20261004)->reserved&(1<<i)));
      press(UI_OK);press(UI_DOWN);press(UI_OK);press(UI_RIGHT);press(UI_OK);assert(curScreen()==SCR_TASKS);
      assert(orchardDay(20261004)->reserved&(1<<i));press(UI_DOWN);
    }
    assert(taskRow==0);orchardLoad();assert(orchardDay(20261004)->reserved==31);
    assert(orchardDay(20261003)->completed==7);shot("agronomy_done");
    press(UI_OK);press(UI_BACK);assert(curScreen()==SCR_TASKS);
    press(UI_BACK);assert(curScreen()==SCR_CALENDAR && code(selected)==20261004);
    std::cout<<"PASS agronomy date entry, five guides, steps, cancel/confirm, save/reload, legacy preservation and returns\n";return 0;
  }
  if(argc==2&&std::strcmp(argv[1],"--time-request")==0) {
    assert(uiCanvasInit());clockInit();orchardInit();enter(SCR_CALENDAR);
    Serial.output.clear();press(UI_OK);
    assert(Serial.output=="Q:TIME\n" && timeRequestPending && !dateReady);
    press(UI_OK);assert(Serial.output=="Q:TIME\n");
    fakeMillis+=5100;orchardTick();
    assert(!timeRequestPending && !clockIsSet() && toast);
    press(UI_OK);assert(timeRequestPending);
    clockSet(1790956800);fakeMillis+=1100;orchardTick();
    assert(!timeRequestPending && dateReady && code(selected)==20261003);
    assert(calendarFocus==CAL_BROWSE && toast && std::strcmp(toast,u8"校时成功")==0);
    Serial.output.clear();press(UI_OK);
    assert(calendarFocus==CAL_MONTH && Serial.output.empty());
    std::cout<<"PASS time request, debounce, timeout, retry, date refresh and normal focus\n";
    return 0;
  }
  if(argc==2&&std::strcmp(argv[1],"--calendar-entry")==0) {
    assert(uiCanvasInit());
    clockInit();clockSet(1790956800);orchardInit();
    testCalendarEntry();
    return 0;
  }
  testCanvasInitialization();
  using namespace orchard;
  assert(monthDays(2024,2)==29 && monthDays(2026,2)==28 && !leap(2100));
  assert(weekday({2026,10,2})==4);
  assert(code(shiftMonth({2024,1,31},1))==20240229);
  assert(code(shiftMonth({2026,3,31},-1))==20260228);
  assert(code(shiftDay({2026,12,31},1))==20270101);
  assert(code(shiftDay({2024,3,1},-1))==20240229);
  assert(code(shiftDay({2020,1,1},-1))==20200101);
  assert(code(shiftDay({2099,12,31},1))==20991231);
  Countdown c;c.setMinutes(1);c.toggle(0xfffffff0);assert(!c.tick(0x20));assert(c.remainingMs==59952);
  c.toggle(100);uint32_t paused=c.remainingMs;assert(!c.tick(5000)&&c.remainingMs==paused);
  c.toggle(5000);assert(c.tick(65000)&&c.finished&&!c.running);assert(!c.tick(66000));
  c.reset();c.toggle(0);assert(c.toggle(60000)&&c.finished); // 暂停恰好碰到到期也发事件
  testVisitSchedule();
  std::cout << "PASS dates, leap years, timer pause and millis wrap" << std::endl;
  clockInit();screensInit();assert(curScreen()==SCR_TODAY);
  press(UI_BACK);assert(curScreen()==SCR_TODAY);shot("today_unsynced");
  press(UI_FARM);assert(curScreen()==SCR_CALENDAR);press(UI_BACK);
  assert(curScreen()==SCR_TODAY);
  press(UI_LEFT);assert(curScreen()==SCR_TIMER);press(UI_BACK);assert(curScreen()==SCR_TODAY);
  enter(SCR_FRIDGE);shot("fridge_unsynced");
  foodEdit(0,fridge::SET_STOCK,6);assert(fridgeData.items[0].stock==6&&fridgeData.date==0);
  const auto unsynced=fridgeData;
  foodEdit(0,fridge::EAT,1);assert(sameItem(fridgeData.items[0],unsynced.items[0]));
  press(UI_BACK);
  clockSet(1790899200);tick();assert(dateReady&&fridgeData.date==dateCode());
  // 新页使用上海时间，跨 UTC 日期边界也正确。
  clockSet(1790956800); // UTC 2026-10-02 16:00 -> 上海 10-03 00:00
  assert(dateCode()==20261003);clockSet(1790899200);
  testCalendarEntry();
  testCalendarInteraction();
  enter(SCR_TODAY);shot("today");
  press(UI_RIGHT);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_BROWSE);shot("calendar");
  press(UI_RIGHT);assert(curScreen()==SCR_TREE);shot("tree");
  assert(treeRow==0);press(UI_OK);assert(curScreen()==SCR_PET);
  press(UI_TIMER);assert(curScreen()==SCR_TIMER);press(UI_TIMER);assert(curScreen()==SCR_PET);
  press(UI_RIGHT);assert(curScreen()==SCR_PET);
  press(UI_LEFT);assert(curScreen()==SCR_TREE&&treeRow==0);
  press(UI_OK);press(UI_BACK);assert(curScreen()==SCR_TREE&&treeRow==0);
  press(UI_UP);assert(treeRow==2);press(UI_DOWN);assert(treeRow==0);
  press(UI_DOWN);press(UI_OK);assert(curScreen()==SCR_ARCHIVE);shot("archive");
  press(UI_OK);assert(curScreen()==SCR_STATUS);shot("status");press(UI_BACK);
  press(UI_DOWN);press(UI_OK);assert(curScreen()==SCR_ADOPTION);shot("adoption");press(UI_BACK);
  press(UI_DOWN);press(UI_OK);assert(curScreen()==SCR_MEMORIAL);shot("memorial");press(UI_LEFT);
  assert(curScreen()==SCR_ARCHIVE);press(UI_BACK);press(UI_DOWN);press(UI_OK);
  assert(curScreen()==SCR_CAPTURE);press(UI_RIGHT);press(UI_OK);assert(orchardData.batch==11);shot("capture");
  enter(SCR_CALENDAR);press(UI_OK);assert(calendarFocus==CAL_MONTH);press(UI_DOWN);assert(calendarFocus==CAL_DAYS);press(UI_OK);
  assert(curScreen()==SCR_TASKS);shot("tasks_empty");press(UI_OK);
  auto* day=orchardDay(code(selected));assert(day&&day->completed==0&&day->batch==11);
  shot("tasks");press(UI_DOWN);press(UI_DOWN);press(UI_DOWN);assert(taskRow==3);
  press(UI_OK);assert(curScreen()==SCR_CRAFT);shot("craft"); // 仅套袋有工艺选择入口
  press(UI_OK);assert(curScreen()==SCR_GUIDE&&guideStep==0);shot("guide");
  press(UI_OK);press(UI_OK);press(UI_OK);assert(curScreen()==SCR_CONFIRM&&confirmChoice==0);shot("confirm");
  press(UI_OK);assert(curScreen()==SCR_GUIDE&&guideStep==2&&day->completed==0); // 再等等不打勾
  press(UI_OK);press(UI_RIGHT);press(UI_OK);
  assert(curScreen()==SCR_TASKS&&(day->reserved&8)&&day->completed==0);shot("tasks_completed");
  orchardLoad();assert((orchardDay(code(selected))->reserved&8)&&day->completed==0);
  std::cout << "PASS archive routes, calendar creation, safe confirmation, completed record reload" << std::endl;
  // 活动日绘制检查：十月起止圈(16 开营、30 单日)与活动日清单摘要。
  enter(SCR_CALENDAR);selected={2026,10,30};calendarFocus=CAL_DAYS;screenRedraw();shot("calendar_visit");
  press(UI_OK);assert(curScreen()==SCR_TASKS&&code(selected)==20261030);shot("tasks_visit");
  press(UI_OK);assert(orchardDay(code(selected)));shot("tasks_visit_created");
  press(UI_BACK);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS&&code(selected)==20261030);
  // 旧农事文件继续可读，但其中启用的提醒不再调度或弹出。
  uint32_t dc=dateCode();const auto now=clockNow();uint16_t minute=now.hour*60+now.minute;
  orchardData.reminders[0]={dc,dc,0,minute,1,0};assert(orchardSave());
  enter(SCR_FRIDGE);tick();assert(curScreen()==SCR_FRIDGE);
  assert(orchardData.reminders[0].lastFired==0);
  fakeFiles["/pet0.dat"]={0x56,0x50,0x45,0x54,0x01};
  fakeFiles["/pet1.dat"]={0x56,0x50,0x45,0x54,0x02};
  const auto untouchedFiles=protectedFiles();
  const auto farmBefore=orchardData;
  // 从我的树进入冰箱；上下跨两页选六种食材，再进入详情。
  enter(SCR_TREE);press(UI_RIGHT);assert(curScreen()==SCR_FRIDGE);
  for(uint8_t i=0;i<fridge::FOOD_COUNT;++i) {
    foodEdit(i,fridge::SET_STOCK,static_cast<uint8_t>(12+i));
    assert(fridgeData.items[i].stock==12+i);
  }
  foodEdit(0,fridge::EAT,3);
  assert(fridgeData.items[0].stock==9&&fridgeData.items[0].eaten==3&&fridgeData.items[0].incoming==0);
  foodEdit(0,fridge::RESTOCK,5);
  assert(fridgeData.items[0].stock==14&&fridgeData.items[0].eaten==3&&fridgeData.items[0].incoming==5);
  assert(fridgeData.items[1].stock==13&&fridgeData.items[1].eaten==0&&fridgeData.items[1].incoming==0);
  // 修正今日统计按差额回补/扣减库存，不重复记一笔交易。
  foodEdit(0,fridge::SET_EATEN,2);assert(fridgeData.items[0].eaten==2&&fridgeData.items[0].stock==15);
  foodEdit(0,fridge::SET_INCOMING,4);assert(fridgeData.items[0].incoming==4&&fridgeData.items[0].stock==14);
  const uint8_t eatenBeforeCorrection=fridgeData.items[0].eaten;
  const uint8_t incomingBeforeCorrection=fridgeData.items[0].incoming;
  foodEdit(0,fridge::SET_STOCK,11);
  assert(fridgeData.items[0].stock==11&&fridgeData.items[0].eaten==eatenBeforeCorrection&&fridgeData.items[0].incoming==incomingBeforeCorrection);
  // 输入草稿取消不修改数据，快捷计时和其到期弹窗保留当前输入。
  openFoodAction(0,fridge::EAT);setFoodValue(2);shot("fridge_edit");
  auto beforeCancel=fridgeData;press(UI_TIMER);assert(curScreen()==SCR_TIMER);shot("timer");
  press(UI_TIMER);assert(curScreen()==SCR_FRIDGE_EDIT&&foodIndex==0&&foodValue==2);
  assert(static_cast<unsigned>(foodAction)==static_cast<unsigned>(fridge::EAT));
  press(UI_BACK);assert(curScreen()==SCR_FRIDGE_ITEM);
  assert(sameItem(fridgeData.items[0],beforeCancel.items[0]));shot("fridge_item");
  press(UI_BACK);assert(curScreen()==SCR_FRIDGE);shot("fridge");
  for(int i=0;i<3;++i) press(UI_DOWN);
  assert(foodIndex==3);shot("fridge_second");
  press(UI_RIGHT);assert(curScreen()==SCR_FRIDGE); // No separate today page; 已吃 is on the list.
  press(UI_DOWN);assert(foodIndex==4);press(UI_OK);assert(curScreen()==SCR_FRIDGE_ITEM);
  press(UI_LEFT);assert(curScreen()==SCR_FRIDGE);press(UI_LEFT);assert(curScreen()==SCR_TREE);
  // 完成一笔进货后重载并撤销；撤销恢复整项，且不能重复撤销。
  const auto beforeUndo=fridgeData.items[2];
  foodEdit(2,fridge::RESTOCK,2);
  assert(fridgeData.undo.valid&&fridgeData.undo.item==2);
  fridgeLoad();assert(fridgeData.items[2].stock==beforeUndo.stock+2&&fridgeData.undo.valid);
  // Undo data remains persisted for compatibility; no separate today page exposes it.
  // 保存失败必须回滚整笔 UI 操作，并保留上一份可重载的数据。
  const auto beforeFailedSave=fridgeData;
  const auto filesBeforeFailedSave=fakeFiles;
  openFoodAction(1,fridge::RESTOCK);setFoodValue(2);failWrites=true;press(UI_OK);failWrites=false;
  for(uint8_t i=0;i<fridge::FOOD_COUNT;++i) assert(sameItem(fridgeData.items[i],beforeFailedSave.items[i]));
  assert(fakeFiles==filesBeforeFailedSave);
  fridgeLoad();assert(sameItem(fridgeData.items[1],beforeFailedSave.items[1]));
  // 库存不足/超过99时UI必须拒绝整笔操作，零与99仍可作为库存校正。
  foodEdit(5,fridge::SET_STOCK,0);const auto emptyFood=fridgeData;
  const auto emptyFiles=fakeFiles;foodEdit(5,fridge::EAT,1);
  assert(sameItem(fridgeData.items[5],emptyFood.items[5])&&fakeFiles==emptyFiles);
  foodEdit(5,fridge::SET_STOCK,99);const auto fullFood=fridgeData;
  const auto fullFiles=fakeFiles;foodEdit(5,fridge::RESTOCK,1);
  assert(sameItem(fridgeData.items[5],fullFood.items[5])&&fakeFiles==fullFiles);
  foodEdit(5,fridge::SET_STOCK,17);
  // 撤销保存失败同样回滚，保留该笔操作和仍可重试的撤销记录。
  const auto beforeUndoFailure=fridgeData;
  const auto filesBeforeUndoFailure=fakeFiles;
  enter(SCR_FRIDGE);press(UI_RIGHT);failWrites=true;press(UI_RIGHT);failWrites=false;
  assert(sameItem(fridgeData.items[5],beforeUndoFailure.items[5])&&fridgeData.undo.valid);
  assert(fakeFiles==filesBeforeUndoFailure);
  assert(protectedFiles()==untouchedFiles);
  assert(std::memcmp(&orchardData,&farmBefore,sizeof orchardData)==0);
  std::cout << "PASS six food routes, independent counters, corrections, drafts, undo, save rollback, protected pet/farm files" << std::endl;
  // 编辑中到期：弹窗确认后回到同一数量，计时快捷键仍可往返。
  openFoodAction(4,fridge::RESTOCK);setFoodValue(7);
  press(UI_TIMER);timer.setMinutes(1);press(UI_OK);press(UI_TIMER);
  fakeMillis+=61000;orchardTick();assert(curScreen()==SCR_ALERT);press(UI_OK);
  assert(curScreen()==SCR_FRIDGE_EDIT&&foodIndex==4&&foodValue==7);
  press(UI_BACK);
  enter(SCR_GUIDE);press(UI_TIMER);timer.setMinutes(1);press(UI_OK);
  uint32_t start=fakeMillis;press(UI_TIMER);assert(curScreen()==SCR_GUIDE);
  fakeMillis=start+61000;orchardTick();assert(curScreen()==SCR_ALERT);shot("alert");
  press(UI_OK);assert(curScreen()==SCR_GUIDE);
  // 在计时页到点的提醒返回后，快捷键仍返回原页，不陷入提醒页。
  press(UI_TIMER);press(UI_OK);fakeMillis+=61000;orchardTick();assert(curScreen()==SCR_ALERT);
  press(UI_OK);assert(curScreen()==SCR_TIMER);press(UI_TIMER);assert(curScreen()==SCR_GUIDE);
  std::cout << "PASS fridge draft and timer alert restoration, background countdown and return paths" << std::endl;
  // 上海零点清今日统计但保留库存；不能受月历浏览日期影响。
  selected={2025,1,1};const auto beforeMidnight=fridgeData;
  clockSet(1790956800);enter(SCR_FRIDGE);tick();assert(fridgeData.date==20261003);
  for(uint8_t i=0;i<fridge::FOOD_COUNT;++i) {
    assert(fridgeData.items[i].stock==beforeMidnight.items[i].stock);
    assert(fridgeData.items[i].eaten==0&&fridgeData.items[i].incoming==0);
  }
  assert(!fridgeData.undo.valid);fridgeLoad();assert(fridgeData.date==20261003);
  // 编辑跨零点先重置今日计数并再次确认，第二次提交才记新的一天。
  foodEdit(0,fridge::EAT,1);assert(fridgeData.items[0].eaten==1);
  openFoodAction(1,fridge::RESTOCK);setFoodValue(3);
  const uint8_t stockBeforeMidnightEdit=fridgeData.items[1].stock;
  clockSet(nowEpoch()+86400);press(UI_OK);
  assert(fridgeData.date==20261004&&fridgeData.items[0].eaten==0);
  assert(curScreen()==SCR_FRIDGE_EDIT&&foodValue==3);
  assert(fridgeData.items[1].stock==stockBeforeMidnightEdit&&fridgeData.items[1].incoming==0);
  press(UI_OK);
  assert(fridgeData.items[1].stock==stockBeforeMidnightEdit+3&&fridgeData.items[1].incoming==3);
  assert(protectedFiles()==untouchedFiles);
  std::cout << "PASS local-midnight reset, selected-date isolation and transaction across midnight" << std::endl;
  enter(SCR_CALENDAR);selected={2026,8,31};screenRedraw();shot("calendar_six_rows");
  // 冰箱新槽损坏回退上一份，失败写入不碰其余模块。
  foodEdit(5,fridge::SET_STOCK,21);const auto priorFridge=fridgeData;
  const auto beforeNewest=fakeFiles;
  foodEdit(5,fridge::SET_STOCK,22);std::string newestFridgePath;
  for(const auto& file:fakeFiles) {
    if(file.first.find("/fridge")==0) {
      auto old=beforeNewest.find(file.first);
      if(old==beforeNewest.end()||old->second!=file.second) newestFridgePath=file.first;
    }
  }
  assert(!newestFridgePath.empty());fakeFiles[newestFridgePath][0]^=1;fridgeLoad();
  assert(sameItem(fridgeData.items[5],priorFridge.items[5]));
  assert(protectedFiles()==untouchedFiles);
  // 新槽损坏时回退到上一槽，失败写入不覆盖上一份有效存档。
  orchardData.batch=21;assert(orchardSave());orchardData.batch=22;assert(orchardSave());
  fakeFiles[paths[1-nextSlot]][0]^=1;orchardLoad();assert(orchardData.batch==21);
  failWrites=true;orchardData.batch=23;assert(!orchardSave());failWrites=false;orchardLoad();assert(orchardData.batch==21);
  fakeFiles.clear();orchardLoad();
  Date cursor={2026,1,1};
  for(int i=0;i<RECORD_LIMIT;++i) {assert(orchardDay(code(cursor),true));cursor=shiftDay(cursor,1);}
  assert(!orchardDay(code(cursor),true)&&orchardData.count==RECORD_LIMIT);
  testPageFrameRegression();
  fsOk=false;assert(!orchardSave()&&!fridgeSave());
  std::cout << "PASS fridge/farm CRC fallback, failed write recovery, storage unavailable and record capacity" << std::endl;
  assert(tft.directDrawCount==0); // No page test may leak primitive draws to LCD.
  assert(tft.spriteSubmitCount==uiFrameCount()+1); // The only extra push is the animation-path probe.
  std::cout << "ALL TESTS PASSED" << std::endl;
}
