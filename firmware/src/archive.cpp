#include "pet_data.h"
#include "archive_view.h"
#include "orchard_visits.h"
#include "ui_hints.h"
#include <cstring>

namespace archive_ui {
constexpr uint16_t PAPER=0xFFFF,INK=0x18E3,GREEN=0x3349,PALE=0xE77C,MUTED=0x7BEF,LINE=0xD6DA,FOOT=0xF79D;
int scroll=0,detailScroll=0,offset=0;
archiveview::HoldRepeat repeat;
uint32_t lastTick=0;
struct Snapshot {
  uint32_t total,date,adoption; int32_t days; uint16_t today,cap; uint8_t stage,miles; bool adopted; char name[13];
  bool operator==(const Snapshot& o) const {
    return total==o.total&&date==o.date&&adoption==o.adoption&&days==o.days&&today==o.today&&cap==o.cap&&
      stage==o.stage&&miles==o.miles&&adopted==o.adopted&&std::strcmp(name,o.name)==0;
  }
};
Snapshot drawn={};
Snapshot snapshot() {
  Snapshot s={sunTotalQ8()>>8,dateCode(),saveAdoptEpoch(),clockDaysSince(saveAdoptEpoch()),
    static_cast<uint16_t>(sunTodayQ8()>>8),sunCapPts(),saveStage(),saveMileCount(),saveAdopted(),{0}};
  std::strncpy(s.name,gPetName,sizeof(s.name)-1);return s;
}
int sy(int y) {return archiveview::TOP+y-offset;}
bool visible(int y,int h) {return y+h>offset && y<offset+archiveview::HEIGHT;}
void cn(int x,int y,const char* s,uint16_t color=INK,uint16_t bg=PAPER) {
  if(visible(y,16))drawCn16Bg(x,sy(y),s,color,bg);
}
void latin(int x,int y,const char* s,uint16_t color=INK,uint16_t bg=PAPER,uint8_t font=2,bool right=false) {
  if(!visible(y,font==6?48:font==4?26:16))return;
  auto& cv=uiCanvas();cv.setTextDatum(right?TR_DATUM:TL_DATUM);cv.setTextColor(color,bg);cv.drawString(s,x,sy(y),font);
}
void dateLabel(int x,int y,uint32_t dc,uint16_t color=MUTED,uint16_t bg=PAPER) {
  char b[24];snprintf(b,sizeof b,"%04lu.%02lu.%02lu",(unsigned long)(dc/10000),(unsigned long)(dc/100%100),(unsigned long)(dc%100));latin(x,y,b,color,bg);
}
void levelCard(int y,bool interactive) {
  if(!visible(y,archiveview::LEVEL_H))return;
  auto& cv=uiCanvas();char b[40];const auto p=archiveview::progress(saveStage(),sunTotalQ8()>>8,STAGE_NEED);
  cv.fillRoundRect(12,sy(y),296,archiveview::LEVEL_H,8,PALE);
  if(interactive&&archiveLevelFocused())cv.drawRoundRect(12,sy(y),296,archiveview::LEVEL_H,8,GREEN);
  snprintf(b,sizeof b,"LV.%u",static_cast<unsigned>(saveStage()>3?4:saveStage()+1));latin(24,y+10,b,GREEN,PALE);
  cn(77,y+10,STAGES[saveStage()>3?3:saveStage()].name,GREEN,PALE);
  snprintf(b,sizeof b,"%u%%",p.percent);latin(294,y+10,b,GREEN,PALE,2,true);
  cv.fillRoundRect(24,sy(y+37),270,8,3,LINE);
  if(p.percent)cv.fillRoundRect(24,sy(y+37),270*p.percent/100,8,3,GREEN);
  cn(24,y+59,interactive?u8"中按查看详情":u8"阶段成长进度",interactive?GREEN:MUTED,PALE);
  if(p.maximum)latin(294,y+59,"MAX",GREEN,PALE,2,true);
  else {snprintf(b,sizeof b,"%lu / %lu",(unsigned long)p.earned,(unsigned long)p.need);latin(294,y+59,b,INK,PALE,2,true);}
}
void adoption() {
  char b[32];cn(12,8,u8"果树档案",GREEN);latin(303,8,gPetName,MUTED,PAPER,2,true);
  if(visible(34,1))uiCanvas().drawLine(12,sy(34),306,sy(34),LINE);
  cn(16,49,u8"已陪伴",MUTED);
  const int32_t days=clockDaysSince(saveAdoptEpoch());
  if(saveAdopted()&&clockIsSet()&&days>=1)snprintf(b,sizeof b,"%ld",(long)days);else std::strcpy(b,"--");
  // Large counters remain in the left half even after many years.
  latin(12,73,b,INK,PAPER,std::strlen(b)>3?4:6);cn(130,101,u8"天");
  cn(16,136,saveAdopted()?u8"领养于":u8"尚未领养",MUTED);
  if(saveAdopted())dateLabel(16,158,clockAtEpoch(saveAdoptEpoch()).dateCode,INK);
  else cn(16,158,u8"校时后领养",MUTED);
  if(saveAdopted()&&!clockIsSet())cn(16,118,u8"校时后查看",MUTED);
}
void cycles() {
  const uint32_t today=dateCode();const auto* next=visits::next(today);
  cn(12,archiveview::CYCLE_Y,u8"农事周期",GREEN);cn(172,archiveview::CYCLE_Y,u8"演示排期",MUTED);
  if(visible(350,416))uiCanvas().fillRoundRect(12,sy(350),296,416,8,PALE);
  const char* names[]={u8"疏果",u8"修枝",u8"疏花",u8"套袋",u8"采摘"};
  for(unsigned i=0;i<visits::COUNT;++i) {
    const auto& a=visits::SCHEDULE[i];const int y=360+i*archiveview::CYCLE_ROW_H;
    if(!visible(y-4,66))continue;
    const bool active=next==&a;const uint16_t bg=active?GREEN:PALE,fg=active?PAPER:INK,sub=active?PALE:MUTED;
    if(active)uiCanvas().fillRoundRect(18,sy(y-4),284,62,6,GREEN);
    uiCanvas().drawLine(28,sy(y+5),28,sy(y+61),active?PALE:LINE);uiCanvas().fillCircle(28,sy(y+9),3,active?PALE:GREEN);
    cn(44,y,a.treeVisit?u8"探访认养树":names[a.task],fg,bg);
    char b[40];const char* state=u8"校时后查看";
    switch(visits::phase(a,today)) {
      case visits::BEFORE:if(active){snprintf(b,sizeof b,u8"%d天后",visits::remaining(a,today));state=b;}else state=u8"未开始";break;
      case visits::START:state=u8"今天开始";break;
      case visits::DURING:state=u8"进行中";break;
      case visits::END:state=u8"今天结束";break;
      case visits::SINGLE:state=u8"就在今天";break;
      case visits::PAST:state=u8"已结束";break;
      default:break;
    }
    cn(205,y,state,fg,bg);
    if(a.start==a.end)snprintf(b,sizeof b,"%04lu.%02lu.%02lu",(unsigned long)(a.start/10000),(unsigned long)(a.start/100%100),(unsigned long)(a.start%100));
    else snprintf(b,sizeof b,"%02lu.%02lu - %02lu.%02lu",(unsigned long)(a.start/100%100),(unsigned long)(a.start%100),(unsigned long)(a.end/100%100),(unsigned long)(a.end%100));
    latin(44,y+28,b,sub,bg);
  }
}
void memories() {
  const unsigned count=saveMileCount();char b[48];cn(12,archiveview::MEMORY_Y,u8"纪念记录",GREEN);
  snprintf(b,sizeof b,"%u",count);latin(299,archiveview::MEMORY_Y,b,MUTED,PAPER,2,true);
  if(!count){cn(24,archiveview::MEMORY_Y+40,u8"暂无纪念记录",MUTED);return;}
  for(unsigned i=0;i<count;++i) {
    const int y=archiveview::MEMORY_Y+40+i*64;if(!visible(y,64))continue;
    const Milestone* m=saveMile(i);if(!m)continue;
    uiCanvas().drawLine(26,sy(y+8),26,sy(y+58),LINE);uiCanvas().fillCircle(26,sy(y+9),3,GREEN);
    dateLabel(44,y,clockAtEpoch(m->epoch).dateCode);
    cn(44,y+26,m->kind?u8"成长为":u8"领养",GREEN);
    if(m->kind)cn(104,y+26,STAGES[m->kind>3?3:m->kind].name,GREEN);
    else latin(89,y+26,gPetName,GREEN);
  }
}
void details() {
  cn(12,8,u8"成长详情",GREEN);latin(305,8,gPetName,MUTED,PAPER,2,true);levelCard(40,false);
  char b[40];for(int i=0;i<2;++i) {
    const int x=i?163:12;if(visible(136,62))uiCanvas().fillRoundRect(x,sy(136),145,62,6,FOOT);
    cn(x+10,142,i?u8"今日获得":u8"累计营养",MUTED,FOOT);
    snprintf(b,sizeof b,"%lu",(unsigned long)(i?(sunTodayQ8()>>8):(sunTotalQ8()>>8)));
    latin(x+10,165,b,INK,FOOT,std::strlen(b)>5?2:4);
    if(i){snprintf(b,sizeof b,"/ %u",sunCapPts());latin(x+134,170,b,MUTED,FOOT,2,true);}
  }
  cn(16,209,u8"今日还可获得",MUTED);const unsigned today=sunTodayQ8()>>8,cap=sunCapPts();
  snprintf(b,sizeof b,"%u pts",cap>today?cap-today:0);latin(300,209,b,GREEN,PAPER,2,true);
}
}

void archiveReset(bool detail) {if(detail)archive_ui::detailScroll=0;else archive_ui::scroll=0;}
int archiveScrollOffset(bool detail) {return detail?archive_ui::detailScroll:archive_ui::scroll;}
bool archiveScroll(bool detail,int direction) {
  using namespace archive_ui;
  int& value=detail?detailScroll:scroll;const int next=archiveview::clamp(value+direction*archiveview::STEP,detail?archiveview::DETAIL_HEIGHT:archiveview::contentHeight(saveMileCount()));
  if(value==next)return false;
  value=next;return true;
}
bool archiveLevelFocused() {return archiveview::levelFocused(archive_ui::scroll);}
bool archivePetVisible() {return curScreen()==SCR_ARCHIVE&&archive_ui::scroll<PET_H;}
int archivePetY() {return archiveview::TOP-archive_ui::scroll;}
int archiveHeldDirection(uint8_t held,uint32_t now) {
  using namespace archive_ui;
  const int page=curScreen()==SCR_ARCHIVE?0:curScreen()==SCR_STATUS?1:-1;
  return repeat.update(now,page,held==(1u<<UI_UP)?-1:held==(1u<<UI_DOWN)?1:0);
}
void archiveDraw(bool detail) {
  using namespace archive_ui;
  scroll=archiveview::clamp(scroll,archiveview::contentHeight(saveMileCount()));
  detailScroll=archiveview::clamp(detailScroll,archiveview::DETAIL_HEIGHT);offset=detail?detailScroll:scroll;
  // The base TFT fillScreen uses the portrait LCD width on this sprite view.
  // Explicit page dimensions also clear the rightmost 80 pixels.
  if(!detail&&archivePetVisible())drawPet(millis());else uiCanvas().fillRect(0,0,320,240,PAPER);
  if(detail)details();else {adoption();levelCard(archiveview::LEVEL_Y,true);cycles();memories();}
  auto& cv=uiCanvas();cv.fillRect(312,archiveview::TOP,8,archiveview::HEIGHT,PAPER);
  const int height=detail?archiveview::DETAIL_HEIGHT:archiveview::contentHeight(saveMileCount());
  const int track=archiveview::HEIGHT-12,thumb=track*archiveview::HEIGHT/height;
  cv.fillRoundRect(314,archiveview::TOP+6,3,track,1,LINE);
  cv.fillRoundRect(314,archiveview::TOP+6+(track-thumb)*offset/(archiveview::limit(height)?archiveview::limit(height):1),3,thumb,1,GREEN);
  cv.fillRect(0,0,320,archiveview::TOP,PAPER);cv.fillRect(0,archiveview::BOTTOM,320,26,FOOT);
  if(!detail&&archiveLevelFocused())hints::bar({hints::UD,u8"滚动"},{hints::OK,u8"详情"},{hints::A,u8"返回"});
  else hints::bar({hints::UD,u8"滚动"},{hints::A,u8"返回"});
  drawn=snapshot();lastTick=millis();
}
void archiveTick() {
  using namespace archive_ui;
  if(curScreen()!=SCR_ARCHIVE&&curScreen()!=SCR_STATUS)return;
  const uint32_t now=millis();if(now-lastTick<1000)return;lastTick=now;
  if(!(snapshot()==drawn))screenRedraw();
}
