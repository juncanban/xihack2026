// clock.cpp —— 时钟:epoch 基准 + 公历换算 + 节气查表
//
// 时间源三层(设计见 TIME_PERSISTENCE.md):串口 D: 校时 > 硬件 RTC(XOSC32K
// 晶振,挺过复位键/刷机) > flash 尾扇区快照(挺过断电,time_snap.cpp 提供)。
// SAMD51 无 VBAT,真掉电 RTC 清零;长时断电后仍需重新校时,这是物理边界。
// 时长一律做减法:RTC 1024Hz 计数与 millis 同为 49.7 天回绕,无符号减法天然处理。
#include <Arduino.h>
#include "pet_clock.h"
#include "hanzi16.h"   // TERM_DOY 节气表(gen_hanzi.py 生成,2025-2036)

#if defined(ARDUINO) && defined(WIO_TERMINAL)
#define CLOCK_HAS_RTC 1
#endif

static uint32_t sEpochBase = 0;   // 锚点时刻的 epoch 秒
#ifdef CLOCK_HAS_RTC
static uint32_t sTickBase = 0;    // 锚点时刻的 RTC 计数(1024Hz)
#else
static uint32_t sMsBase = 0;      // 锚点时刻的 millis()(宿主测试路径)
#endif
static bool     sTimeSet = false;
static bool     sEverSynced = false;  // 本次上电是否经串口同步过(M5 领养判定用)
static uint8_t  sSource = CLOCK_SRC_NONE;
#ifdef CLOCK_HAS_RTC
static bool     sRtcTrusted = false;  // 本次复位未掉电:RTC 计数/GP 配对可信
#endif

// ---- Howard Hinnant 公历算法(公有域,纯整数) ----
// 天数(1970-01-01 = 0)-> 公历;反之亦然
static int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);                        // [0,399]
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   // [0,365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            // [0,146096]
  return era * 146097 + (int32_t)doe - 719468;
}

static void civilFromDays(int32_t z, int* y, int* m, int* d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);                     // [0,146096]
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0,399]
  const int32_t yy = (int32_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);          // [0,365]
  const unsigned mp = (5 * doy + 2) / 153;                               // [0,11]
  const unsigned dd = doy - (153 * mp + 2) / 5 + 1;                      // [1,31]
  const unsigned mm = mp + (mp < 10 ? 3 : -9);                           // [1,12]
  *y = (int)(yy + (mm <= 2));
  *m = (int)mm;
  *d = (int)dd;
}

bool clockEpochPlausible(uint32_t epoch) {
  return epoch >= 1735689600UL && epoch < 4102444800UL;   // 2025-01-01 .. 2100-01-01
}

#ifdef CLOCK_HAS_RTC
// RTC 直接选 XOSC32K(启动代码已使能晶振并等就绪),不经 GCLK;软复位期间
// OSC32KCTRL/RTC 寄存器均不复位,计数连续。1024Hz + COUNTSYNC 读自动同步。
static uint32_t rtcCount() { return RTC->MODE0.COUNT.reg; }

static void rtcHwInit() {
  OSC32KCTRL->RTCCTRL.reg = OSC32KCTRL_RTCCTRL_RTCSEL_XOSC32K;
  const uint8_t cause = RSTC->RCAUSE.reg;
  sRtcTrusted = (cause & (RSTC_RCAUSE_EXT | RSTC_RCAUSE_SYST | RSTC_RCAUSE_WDT)) != 0;
  const bool running = RTC->MODE0.CTRLA.bit.ENABLE && RTC->MODE0.CTRLA.bit.MODE == 0 &&
                       RTC->MODE0.CTRLA.bit.PRESCALER == RTC_MODE0_CTRLA_PRESCALER_DIV32_Val &&
                       RTC->MODE0.CTRLA.bit.COUNTSYNC;
  if (running) return;                    // 软复位保留:保持计数,GPS 配对仍有效
  RTC->MODE0.CTRLA.bit.ENABLE = 0;
  while (RTC->MODE0.SYNCBUSY.bit.ENABLE) {}
  RTC->MODE0.CTRLA.bit.SWRST = 1;         // POR/外来配置:重置到已知 mode0
  while (RTC->MODE0.SYNCBUSY.bit.SWRST || RTC->MODE0.CTRLA.bit.SWRST) {}
  RTC->MODE0.CTRLA.reg = RTC_MODE0_CTRLA_PRESCALER_DIV32 | RTC_MODE0_CTRLA_COUNTSYNC;
  while (RTC->MODE0.SYNCBUSY.reg) {}
  RTC->MODE0.CTRLA.bit.ENABLE = 1;
  while (RTC->MODE0.SYNCBUSY.bit.ENABLE) {}
}

// (epoch,计数)配对存 GP0/GP1:仅 POR/RTC 软复位会清,挺过复位键与刷机
static void rtcLatch(uint32_t epoch) {
  RTC->MODE0.GP[0].reg = epoch;
  RTC->MODE0.GP[1].reg = rtcCount();
  while (RTC->MODE0.SYNCBUSY.reg & (RTC_MODE0_SYNCBUSY_GP0 | RTC_MODE0_SYNCBUSY_GP1)) {}
}
#endif

static void clockAnchor(uint32_t epoch) {
  sEpochBase = epoch;
#ifdef CLOCK_HAS_RTC
  sTickBase = rtcCount();
  rtcLatch(epoch);
#else
  sMsBase = millis();
#endif
}

void clockInit() {
  sTimeSet = false;
  sEverSynced = false;
  sSource = CLOCK_SRC_NONE;
#ifdef CLOCK_HAS_RTC
  rtcHwInit();
  if (sRtcTrusted) {                      // 未掉电的复位:从 GP 配对恢复精确时间
    const uint32_t e = RTC->MODE0.GP[0].reg, c = RTC->MODE0.GP[1].reg;
    if (clockEpochPlausible(e)) {
      const uint32_t now = rtcCount();
      const uint32_t cand = e + (now - c) / CLOCK_RTC_TICK_HZ;
      if (clockEpochPlausible(cand)) {    // GP 垃圾/回绕防护
        sEpochBase = cand;
        sTickBase = now;
        sTimeSet = true;
        sSource = CLOCK_SRC_RTC;
      }
    }
  }
#endif
}

// 设置时间(串口 D:)。|新-旧| < 120s 视为幂等不动作。
// 返回是否真的改了;*firstSync(可空)= 本次上电第一次串口同步(M5 领养触发点)
bool clockSet(uint32_t epoch, bool* firstSync) {
  if (firstSync) *firstSync = false;
  if (sTimeSet) {
    uint32_t cur = nowEpoch();
    uint32_t diff = epoch > cur ? epoch - cur : cur - epoch;
    if (diff < 120) return false;          // 幂等:宿主机频繁补时不抖动
  }
  clockAnchor(epoch);
  if (!sEverSynced) {
    sEverSynced = true;
    if (firstSync) *firstSync = true;
  }
  sTimeSet = true;
  sSource = CLOCK_SRC_HOST;
  return true;
}

// 兜底恢复(flash 快照/RTC 内部用):不算首次串口同步,领养语义不受影响;
// 已有更高优先级来源(串口/RTC)时不覆盖。
bool clockRestore(uint32_t epoch) {
  if (sTimeSet || !clockEpochPlausible(epoch)) return false;
  clockAnchor(epoch);
  sTimeSet = true;
  sSource = CLOCK_SRC_SNAP;
  return true;
}

uint8_t clockRestoreSource() { return sSource; }

uint32_t nowEpoch() {
  if (!sTimeSet) return 0;
#ifdef CLOCK_HAS_RTC
  const uint32_t elapsedSeconds = (rtcCount() - sTickBase) / CLOCK_RTC_TICK_HZ;
#else
  const uint32_t elapsedSeconds = (millis() - sMsBase) / 1000;
#endif
  sEpochBase += elapsedSeconds;
#ifdef CLOCK_HAS_RTC
  sTickBase += elapsedSeconds * CLOCK_RTC_TICK_HZ;
#else
  sMsBase += elapsedSeconds * 1000UL;
#endif
  return sEpochBase;
}

bool clockIsSet() { return sTimeSet; }
bool clockEverSynced() { return sEverSynced; }

ClockSnapshot clockAtEpoch(uint32_t epoch) {
  ClockSnapshot out = {};
  out.solarTerm = -1;
  if (!epoch) return out;
  out.valid = true;
  out.utcEpoch = epoch;
  const uint64_t local = (uint64_t)epoch + CLOCK_UTC_OFFSET_SECONDS;
  out.dayIndex = (int32_t)(local / 86400);
  civilFromDays(out.dayIndex, &out.date.year, &out.date.month, &out.date.day);
  out.dateCode = out.date.year * 10000UL + out.date.month * 100UL + out.date.day;
  out.weekday = (out.dayIndex + 3) % 7; // 1970-01-01 was Thursday.
  out.hour = (local % 86400) / 3600;
  out.minute = (local % 3600) / 60;
  out.second = local % 60;
  out.solarTerm = solarTermIndex(out.dateCode);
  return out;
}

ClockSnapshot clockNow() { return clockAtEpoch(nowEpoch()); }
uint32_t dateCode() { return clockNow().dateCode; }

int32_t clockDaysSince(uint32_t epoch) {
  const ClockSnapshot now = clockNow(), start = clockAtEpoch(epoch);
  if (!now.valid || !start.valid || epoch > now.utcEpoch) return 0;
  return now.dayIndex - start.dayIndex + 1;
}

// 当前节气:最后一个 年积日 <= 今天的节气;年初未到小寒归上一年冬至
int8_t solarTermIndex(uint32_t dc) {
  if (!dc) return -1;
  int y = (int)(dc / 10000);
  if (y < TERM_YEAR0 || y >= TERM_YEAR0 + TERM_YEARS) return -1;
  int mo = (int)(dc / 100 % 100), d = (int)(dc % 100);
  int32_t doy = daysFromCivil(y, mo, d) - daysFromCivil(y, 1, 1) + 1;
  int8_t cur = -1;
  const uint16_t* row = TERM_DOY[y - TERM_YEAR0];
  for (int8_t i = 0; i < 24; i++) {
    if (row[i] <= doy) cur = i;
    else break;                       // 表内年积日单调递增
  }
  if (cur < 0) cur = 23;              // 1 月初:上一年冬至
  return cur;
}
