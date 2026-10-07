// 无硬件依赖的日期和计时逻辑，可在桌面回归测试。
#ifndef ORCHARD_LOGIC_H
#define ORCHARD_LOGIC_H
#include <stdint.h>
#include "pet_clock.h"
namespace orchard {
using Date = CalendarDate;
inline bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
inline int monthDays(int year, int month) {
  const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return days[month - 1] + (month == 2 && leap(year));
}
inline bool valid(Date d) {
  return d.year >= 2020 && d.year <= 2099 && d.month >= 1 && d.month <= 12 &&
         d.day >= 1 && d.day <= monthDays(d.year, d.month);
}
inline uint32_t code(Date d) { return d.year * 10000UL + d.month * 100UL + d.day; }
inline Date decode(uint32_t c) { return {(int)(c / 10000), (int)(c / 100 % 100), (int)(c % 100)}; }
// Monday = 0. Gregorian integer calendar, independent of clock synchronization.
inline int weekday(Date d) {
  static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  int y = d.year - (d.month < 3);
  return (y + y / 4 - y / 100 + y / 400 + offsets[d.month - 1] + d.day + 6) % 7;
}
inline Date shiftMonth(Date d, int delta) {
  int m = d.year * 12 + d.month - 1 + delta;
  if (m < 2020 * 12) return {2020, 1, 1};
  if (m > 2099 * 12 + 11) return {2099, 12, 31};
  d.year = m / 12; d.month = m % 12 + 1;
  if (d.day > monthDays(d.year, d.month)) d.day = monthDays(d.year, d.month);
  return d;
}
inline Date shiftDay(Date d, int delta) {
  while (delta > 0) {
    if (code(d) == 20991231) break;
    if (++d.day > monthDays(d.year, d.month)) {
      d.day = 1; if (++d.month > 12) { d.month = 1; ++d.year; }
    }
    --delta;
  }
  while (delta < 0) {
    if (code(d) == 20200101) break;
    if (--d.day < 1) {
      if (--d.month < 1) { d.month = 12; --d.year; }
      d.day = monthDays(d.year, d.month);
    }
    ++delta;
  }
  return d;
}
struct Countdown {
  uint32_t durationMs = 8UL * 60000, remainingMs = 8UL * 60000, lastMs = 0;
  bool running = false, finished = false;
  void setMinutes(uint16_t minutes) {
    if (minutes < 1) minutes = 1;
    if (minutes > 180) minutes = 180;
    durationMs = minutes * 60000UL; reset();
  }
  void reset() { remainingMs = durationMs; running = false; finished = false; }
  bool tick(uint32_t now) {
    if (!running) return false;
    uint32_t elapsed = now - lastMs; lastMs = now;
    if (elapsed >= remainingMs) {
      remainingMs = 0; running = false; finished = true; return true;
    }
    remainingMs -= elapsed; return false;
  }
  bool toggle(uint32_t now) {
    if (running) { bool expired = tick(now); running = false; return expired; }
    if (!remainingMs) reset();
    lastMs = now; running = true;
    return false;
  }
};
struct DayRecord { uint32_t date; uint8_t completed, craft, batch, reserved; };
// 仅保留旧农事 v1 存档布局；提醒页面和触发逻辑已移除。
struct Reminder {
  uint32_t start, end, lastFired;
  uint16_t minute;
  uint8_t enabled, reserved;
};
constexpr uint8_t RECORD_LIMIT = 90, REMINDER_COUNT = 5;
struct Data {
  uint32_t magic; uint16_t version, sequence;
  // rsv 占位保持 v1 存档字节布局与 CRC 覆盖范围不变,旧 farm*.dat 直接兼容。
  uint8_t count, batch, rsv, reserved;
  DayRecord records[RECORD_LIMIT];
  Reminder reminders[REMINDER_COUNT];
  uint32_t crc;
};
inline uint8_t completedCount(uint8_t bits) {
  return (bits & 1) + ((bits >> 1) & 1) + ((bits >> 2) & 1) + ((bits >> 3) & 1) + ((bits >> 4) & 1);
}
}
#endif
