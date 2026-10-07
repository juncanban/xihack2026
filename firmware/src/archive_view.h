#ifndef ARCHIVE_VIEW_H
#define ARCHIVE_VIEW_H
#include <stdint.h>

namespace archiveview {
constexpr int TOP=26, BOTTOM=214, HEIGHT=BOTTOM-TOP, STEP=24;
constexpr int LEVEL_Y=196, LEVEL_H=84;
constexpr int CYCLE_Y=318, CYCLE_ROW_H=70, MEMORY_Y=776;
constexpr int DETAIL_HEIGHT=226;
inline int contentHeight(unsigned milestones) { return MEMORY_Y+40+(milestones?milestones:1)*64+12; }
inline int limit(int height) { return height>HEIGHT?height-HEIGHT:0; }
inline int clamp(int value,int height) { return value<0?0:value>limit(height)?limit(height):value; }
inline bool levelFocused(int offset) { return offset<=LEVEL_Y && offset+HEIGHT>=LEVEL_Y+LEVEL_H; }
struct Progress { uint32_t earned,need; uint8_t percent; bool maximum; };
inline Progress progress(uint8_t stage,uint32_t total,const uint32_t* thresholds) {
  if(stage>=3) return {0,0,100,true};
  const uint32_t lo=stage?thresholds[stage-1]:0, need=thresholds[stage]-lo;
  const uint32_t earned=total<=lo?0:total>=thresholds[stage]?need:total-lo;
  return {earned,need,static_cast<uint8_t>(earned*100/need),false};
}
// Only a single held vertical direction on this same page can repeat.
struct HoldRepeat {
  uint32_t since=0,last=0; int page=-1,dir=0; bool repeating=false;
  int update(uint32_t now,int activePage,int direction) {
    if(activePage<0 || !direction) {page=-1;dir=0;repeating=false;return 0;}
    if(page!=activePage || dir!=direction) {page=activePage;dir=direction;since=last=now;repeating=false;return 0;}
    if(!repeating) {if(uint32_t(now-since)<400)return 0;repeating=true;}
    else if(uint32_t(now-last)<100)return 0;
    last=now;return dir;
  }
};
}
void archiveReset(bool detail=false);
bool archiveScroll(bool detail,int direction);
bool archiveLevelFocused();
int archiveScrollOffset(bool detail=false);
bool archivePetVisible();
int archivePetY();
int archiveHeldDirection(uint8_t held,uint32_t now);
void archiveDraw(bool detail);
void archiveTick();
#endif
