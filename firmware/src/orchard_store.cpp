// 农事独立双槽存档；不改动宠物 /pet*.dat 的格式及内容。
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#include <stddef.h>
#include "pet_data.h"
#include "orchard_store.h"
orchard::Data orchardData;
static uint8_t nextSlot = 0;
static const char* const paths[] = {"/farm0.dat", "/farm1.dat"};
static uint32_t crc(const orchard::Data& data) {
  const uint8_t* p = (const uint8_t*)&data;
  uint32_t c = 0xFFFFFFFFUL;
  for (size_t n = 0; n < offsetof(orchard::Data, crc); ++n) {
    c ^= p[n];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320UL & (0UL - (c & 1)));
  }
  return ~c;
}
static bool valid(const orchard::Data& d) {
  if (d.magic != 0x4641524D || d.version != 1 || d.count > orchard::RECORD_LIMIT ||
      d.crc != crc(d) || d.batch < 1 || d.batch > 99) return false;
  for (uint8_t i = 0; i < d.count; ++i) {
    const auto& r = d.records[i];
    if (!orchard::valid(orchard::decode(r.date)) || r.completed > 7 || r.reserved > 31 || r.craft > 4 || r.batch < 1 || r.batch > 99) return false;
    for (uint8_t j = 0; j < i; ++j) if (r.date == d.records[j].date) return false;
  }
  for (const auto& r : d.reminders) {
    if (r.enabled > 1 || r.minute >= 1440) return false;
    if (r.enabled && (!orchard::valid(orchard::decode(r.start)) ||
        !orchard::valid(orchard::decode(r.end)) || r.start > r.end)) return false;
  }
  return true;
}
void orchardLoad() {
  memset(&orchardData, 0, sizeof orchardData);
  orchardData.magic = 0x4641524D; orchardData.version = 1;
  orchardData.batch = 10;
  bool have = false;
  if (!saveFsOk()) return;
  for (uint8_t i = 0; i < 2; ++i) {
    File f = SFUD.open(paths[i], "r");
    if (!f) continue;
    orchard::Data candidate;
    bool ok = f.size() == sizeof candidate &&
      f.read((uint8_t*)&candidate, sizeof candidate) == sizeof candidate && valid(candidate);
    f.close();
    if (ok && (!have || (int16_t)(candidate.sequence - orchardData.sequence) > 0)) {
      orchardData = candidate; have = true; nextSlot = 1 - i;
    }
  }
}
bool orchardSave() {
  if (!saveFsOk()) return false;
  orchardData.sequence++; orchardData.crc = crc(orchardData);
  File f = SFUD.open(paths[nextSlot], "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&orchardData, sizeof orchardData) == sizeof orchardData;
  f.close();
  // 回读确认，只有验证成功才切换写入槽，保留上一份有效记录。
  if (ok) {
    f = SFUD.open(paths[nextSlot], "r");
    orchard::Data check;
    ok = f && f.size() == sizeof check &&
      f.read((uint8_t*)&check, sizeof check) == sizeof check && valid(check);
    if (f) f.close();
  }
  if (ok) nextSlot = 1 - nextSlot;
  return ok;
}
orchard::DayRecord* orchardDay(uint32_t date, bool create) {
  for (uint8_t i = 0; i < orchardData.count; ++i)
    if (orchardData.records[i].date == date) return &orchardData.records[i];
  if (!create || !orchard::valid(orchard::decode(date)) || orchardData.count >= orchard::RECORD_LIMIT) return nullptr;
  auto& r = orchardData.records[orchardData.count++];
  r = {date, 0, 0, orchardData.batch, 0};
  return &r;
}
