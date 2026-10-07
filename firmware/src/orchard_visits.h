#ifndef ORCHARD_VISITS_H
#define ORCHARD_VISITS_H
#include "orchard_logic.h"
namespace visits {
// Fixed 2026 DEMO schedule, kept in program Flash; not orchard opening notices.
// task maps to the existing agronomy ID: fruit, prune, flower, bag, pick.
struct Activity { uint8_t id, task; uint32_t start, end; bool treeVisit; };
constexpr Activity SCHEDULE[] = {
  {1,2,20260420,20260420,false}, {2,0,20260505,20260512,false},
  {3,3,20260604,20260618,false}, {4,4,20261016,20261102,false},
  {5,4,20261030,20261030,true}, {6,1,20261210,20261220,false}
};
constexpr unsigned COUNT = sizeof SCHEDULE / sizeof SCHEDULE[0];
inline bool contains(const Activity& a,uint32_t date) {return date>=a.start&&date<=a.end;}
inline bool boundary(uint32_t date) {
  for(const auto& a:SCHEDULE) if(date==a.start||date==a.end)return true;
  return false;
}
inline const Activity* next(uint32_t today) {
  if(!orchard::valid(orchard::decode(today)))return nullptr;
  const Activity* best=nullptr;
  for(const auto& a:SCHEDULE) {
    if(a.end<today)continue;
    const bool active=a.start<=today, previous=best&&best->start<=today;
    if(!best||(active&&!previous)||(active==previous&&(active?a.end<best->end:a.start<best->start)))best=&a;
  }
  return best;
}
inline int ordinal(uint32_t date) {
  const auto d=orchard::decode(date);int days=0;
  for(int y=2020;y<d.year;++y)days+=orchard::leap(y)?366:365;
  for(int m=1;m<d.month;++m)days+=orchard::monthDays(d.year,m);
  return days+d.day;
}
enum Phase {UNKNOWN, BEFORE, START, DURING, END, SINGLE, PAST};
inline Phase phase(const Activity& a,uint32_t today) {
  if(!orchard::valid(orchard::decode(today)))return UNKNOWN;
  if(today<a.start)return BEFORE;
  if(today>a.end)return PAST;
  if(a.start==a.end)return SINGLE;
  if(today==a.start)return START;
  if(today==a.end)return END;
  return DURING;
}
inline int remaining(const Activity& a,uint32_t today) {
  auto p=phase(a,today);
  return p==BEFORE?ordinal(a.start)-ordinal(today):p==DURING?ordinal(a.end)-ordinal(today):0;
}
}
#endif
