// Real firmware clock; host gmtime is an independent calendar oracle.
#include <cassert>
#include <cstring>
#include <ctime>
#include <iostream>
#include <vector>
#include "Arduino.h"
uint32_t fakeMillis = 0;
#include "../firmware/src/clock.cpp"
// Only the light sensor is stubbed; test the actual daily nutrition logic too.
constexpr int WIO_LIGHT = 0, INPUT = 0;
constexpr uint16_t TFT_ORANGE = 0xFDA0; // Missing from the minimal host display stub.
void pinMode(int, int) {}
void analogReadResolution(int) {}
int analogRead(int) { return 0; }
#include "../firmware/src/sun.cpp"

static void expect(uint32_t epoch, uint32_t dc, int hour, int minute, int second, int term) {
  const auto t = clockAtEpoch(epoch);
  assert(t.valid && t.utcEpoch == epoch && t.dateCode == dc);
  assert(t.hour == hour && t.minute == minute && t.second == second);
  assert(t.solarTerm == term);
}

#include "../firmware/src/time_snap.cpp"

int main() {
  clockInit();
  const auto empty = clockNow();
  assert(!empty.valid && empty.utcEpoch == 0 && empty.dateCode == 0);
  assert(empty.date.year == 0 && empty.solarTerm == -1 && !clockEverSynced());
  assert(clockDaysSince(1790956800) == 0);

  // Reported failure: UTC Oct 2, 16:00 is local Oct 3, 00:00 (Saturday).
  expect(1790956799, 20261002, 23, 59, 59, 17);
  expect(1790956800, 20261003, 0, 0, 0, 17);
  assert(clockAtEpoch(1790956800).weekday == 5);
  bool first = false;
  assert(clockSet(1790956799, &first) && first);
  fakeMillis += 1000;
  assert(nowEpoch() == 1790956800 && dateCode() == 20261003);
  assert(clockNow().dateCode == dateCode());
  assert(!clockSet(nowEpoch(), &first) && !first);
  assert(clockDaysSince(1790956799) == 2); // Crossed one local midnight.
  assert(clockDaysSince(1790899200) == 2);
  assert(clockDaysSince(1790956801) == 0); // Future event must not underflow.
  assert(clockDaysSince(0) == 0);

  clockSet(1790956600); // Local 23:56:40.
  sunInit(); sunRestore(1000, 500, 20261002);
  fakeMillis += 201000; sunTick(); // Local midnight advances all daily data.
  assert(sunTodayDate() == clockNow().dateCode && sunTodayDate() == 20261003);
  assert(sunTodayQ8() == 0 && sunTotalQ8() == 1000);
  sunRestore(1000, 500, 20261003);
  clockSet(1790985600); // UTC midnight = local 08:00, no second daily reset.
  fakeMillis += 1000; sunTick();
  assert(dateCode() == 20261003 && sunTodayQ8() == 500 && sunTotalQ8() == 1000);

  // Check all fields and leap/month/year transitions across supported UI years.
  for (uint64_t epoch = 1577836800; epoch < 4102444800ULL; epoch += 21600) {
    const auto t = clockAtEpoch((uint32_t)epoch);
    time_t local = (time_t)(epoch + 28800);
    const tm expected = *gmtime(&local);
    assert(t.date.year == expected.tm_year + 1900);
    assert(t.date.month == expected.tm_mon + 1 && t.date.day == expected.tm_mday);
    assert(t.dateCode == (unsigned)((expected.tm_year+1900)*10000 + (expected.tm_mon+1)*100 + expected.tm_mday));
    assert(t.weekday == (expected.tm_wday + 6) % 7);
    assert(t.hour == expected.tm_hour && t.minute == expected.tm_min && t.second == expected.tm_sec);
    assert(t.dayIndex == local / 86400);
  }
  // The current solar-term interval changes at the table's local civil date.
  assert(solarTermIndex(20261003) == 17); // Autumn equinox interval.
  assert(solarTermIndex(20261007) == 17);
  assert(solarTermIndex(20261008) == 18); // Cold dew.
  assert(solarTermIndex(20260101) == 23); // Previous winter solstice.
  assert(solarTermIndex(20260105) == 0);
  assert(solarTermIndex(20241003) == -1 && solarTermIndex(20371003) == -1);
  const time_t coldDewUtc = 1791388800; // 2026-10-07 16:00 UTC.
  assert(clockAtEpoch(coldDewUtc-1).solarTerm == 17);
  assert(clockAtEpoch(coldDewUtc).solarTerm == 18);

  // UTC epoch stays intact, including near UINT32_MAX (offset must not wrap).
  assert(clockAtEpoch(UINT32_MAX).utcEpoch == UINT32_MAX);
  assert(clockAtEpoch(UINT32_MAX).dateCode == 21060207);

  // ---- 断电恢复(TIME_PERSISTENCE.md):恢复闸门/语义 + 快照扇区扫描 ----
  assert(clockEpochPlausible(1735689600) && clockEpochPlausible(4102444799));
  assert(!clockEpochPlausible(1735689599) && !clockEpochPlausible(4102444800));

  std::vector<uint8_t> sec(4096, 0xFF);
  const TimeSnapScan emptyScan = timeSnapScanSector(sec.data(), sec.size());
  assert(!emptyScan.hasValue && !emptyScan.foreign && emptyScan.nextSlot == 0);

  TimeSnapRecord rec = {TIME_SNAP_MAGIC, 1790956800, 0, 0};
  rec.crc32 = timeSnapCrc(rec);
  assert(timeSnapRecordValid(rec));
  TimeSnapRecord bad = rec; bad.pad = 1;
  assert(!timeSnapRecordValid(bad));
  bad = rec; bad.magic ^= 1UL;
  assert(!timeSnapRecordValid(bad));
  bad = rec; bad.epoch = 100;              // 闸门外:即使 CRC 字段未重算也不认
  assert(!timeSnapRecordValid(bad));

  auto put = [&](int slot, uint32_t epoch, bool corrupt) {
    TimeSnapRecord r = {TIME_SNAP_MAGIC, epoch, 0, 0};
    r.crc32 = timeSnapCrc(r);
    if (corrupt) r.epoch ^= 1UL;
    memcpy(sec.data() + slot * sizeof(r), &r, sizeof(r));
  };
  put(0, 1790956800, false); put(1, 1790956900, false);
  put(2, 1790957000, true);  put(3, 1790957100, false);   // 半写坏槽被跳过
  TimeSnapScan s = timeSnapScanSector(sec.data(), sec.size());
  assert(s.hasValue && s.epoch == 1790957100 && s.nextSlot == 4 && !s.foreign);
  std::fill(sec.begin(), sec.end(), 0x5A);                // 纯垃圾:疑似 FAT 曾占用
  s = timeSnapScanSector(sec.data(), sec.size());
  assert(!s.hasValue && s.foreign && s.nextSlot == TIME_SNAP_SLOTS);

  // 恢复语义:设时间但不动领养;已设不覆盖;120 秒幂等照旧
  clockInit();
  assert(!clockRestore(123) && !clockIsSet());             // 闸门外拒收
  assert(clockRestore(1790956800) && clockIsSet());
  assert(clockNow().dateCode == 20261003);
  assert(!clockEverSynced());                              // 恢复 ≠ 串口首次同步
  bool firstSync = true;
  assert(!clockSet(1790956860, &firstSync) && !firstSync); // 差 60s:幂等,不触发领养
  assert(clockSet(1790956800 + 3600, &firstSync) && firstSync); // 差 1h:真同步=首次
  assert(clockEverSynced());
  assert(!clockRestore(1800000000));                       // 快照不得覆盖更高优先级

  fakeMillis = UINT32_MAX - 499;
  clockInit(); clockSet(1790956799);
  fakeMillis = 500;
  assert(dateCode() == 20261003);
  clockInit();
  assert(!clockNow().valid && dateCode() == 0);
  std::cout << "PASS real clock: UTC+8 boundaries, calendar oracle 2020..2099, solar terms, event dates, unsynced, millis wrap, restore semantics, snapshot scan\n";
}
