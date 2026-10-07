// One UTC clock, one local calendar view for every screen and daily counter.
#ifndef PET_CLOCK_H
#define PET_CLOCK_H
#include <stdint.h>

struct CalendarDate { int year, month, day; };
struct ClockSnapshot {
  bool valid;
  uint32_t utcEpoch;        // Serial protocol and saved event timestamps stay UTC.
  CalendarDate date;        // Asia/Shanghai (UTC+08:00), applied only here.
  uint32_t dateCode;        // YYYYMMDD, 0 when not synchronized.
  int32_t dayIndex;         // Local calendar days since 1970-01-01.
  uint8_t weekday;         // Monday = 0, Sunday = 6.
  uint8_t hour, minute, second;
  int8_t solarTerm;         // Current solar-term interval, -1 outside table range.
};
constexpr int32_t CLOCK_UTC_OFFSET_SECONDS = 8 * 3600;

void clockInit();
bool clockSet(uint32_t epoch, bool* firstSync = nullptr);
uint32_t nowEpoch();        // UTC Unix seconds, 0 until synchronized.
bool clockIsSet();
bool clockEverSynced();

// ---- 断电恢复(见 TIME_PERSISTENCE.md):串口校时 > 硬件RTC > flash快照 ----
enum { CLOCK_SRC_NONE = 0, CLOCK_SRC_HOST = 1, CLOCK_SRC_RTC = 2, CLOCK_SRC_SNAP = 3 };
#define CLOCK_RTC_TICK_HZ 1024UL   // RTC mode0 计数频率(32768/32,回绕49.7天靠减法)
bool clockEpochPlausible(uint32_t epoch); // 2025-01-01..2100-01-01,恢复闸门
bool clockRestore(uint32_t epoch);   // flash 快照兜底恢复;不算首次串口同步,不影响领养
uint8_t clockRestoreSource();        // CLOCK_SRC_*,上电恢复诊断日志用
ClockSnapshot clockNow();
ClockSnapshot clockAtEpoch(uint32_t utcEpoch); // Also used for saved milestones.
uint32_t dateCode();        // Current local date from clockNow().
int8_t solarTermIndex(uint32_t localDateCode);
int32_t clockDaysSince(uint32_t utcEpoch); // Inclusive local days; 0 if unavailable/future.
#endif
