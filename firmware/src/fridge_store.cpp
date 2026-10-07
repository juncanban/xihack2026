// 冰箱独立双槽存档，不修改 /pet*.dat 或 /farm*.dat 的布局与内容。
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#include "pet_data.h"
#include "fridge_store.h"

fridge::Data fridgeData;

namespace fridge_store_detail {
static uint8_t nextSlot = 0;
static const char* const paths[] = {"/fridge0.dat", "/fridge1.dat"};

static uint32_t crc(const fridge::Data& data) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
  uint32_t value = 0xFFFFFFFFUL;
  for (size_t n = 0; n < offsetof(fridge::Data, crc); ++n) {
    value ^= bytes[n];
    for (uint8_t bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xEDB88320UL & (0UL - (value & 1)));
  }
  return ~value;
}
static bool valid(const fridge::Data& data) {
  if (data.magic != fridge::MAGIC || data.version != fridge::VERSION || data.crc != crc(data) ||
      (data.date != 0 && !fridge::validDay(data.date)) || data.reserved[0] || data.reserved[1])
    return false;
  for (uint8_t i = 0; i < fridge::FOOD_COUNT; ++i)
    if (!fridge::validItem(data.items[i])) return false;
  const fridge::Undo& previous = data.undo;
  if (previous.valid > 1 || previous.reserved[0] || previous.reserved[1]) return false;
  if (previous.valid && (previous.item >= fridge::FOOD_COUNT || previous.action > fridge::SET_STOCK ||
      previous.date != data.date || !fridge::validItem(previous.before) ||
      (previous.action != fridge::SET_STOCK && !fridge::validDay(previous.date)))) return false;
  return true;
}
static bool newer(uint16_t candidate, uint16_t current) {
  const uint16_t distance = static_cast<uint16_t>(candidate - current);
  return distance != 0 && distance < 0x8000;
}
} // namespace fridge_store_detail

void fridgeLoad() {
  namespace store = fridge_store_detail;
  fridge::reset(fridgeData);
  store::nextSlot = 0;
  if (!saveFsOk()) return;
  bool have = false;
  for (uint8_t slot = 0; slot < 2; ++slot) {
    File file = SFUD.open(store::paths[slot], "r");
    if (!file) continue;
    fridge::Data candidate;
    const bool ok = file.size() == sizeof candidate &&
      file.read(reinterpret_cast<uint8_t*>(&candidate), sizeof candidate) == sizeof candidate && store::valid(candidate);
    file.close();
    if (ok && (!have || store::newer(candidate.sequence, fridgeData.sequence))) {
      fridgeData = candidate;
      have = true;
      store::nextSlot = 1 - slot;
    }
  }
}

bool fridgeSave() {
  namespace store = fridge_store_detail;
  if (!saveFsOk()) return false;
  // 序号和 CRC 先放在副本中，任何失败都不改变 RAM 中的提交状态。
  fridge::Data candidate = fridgeData;
  candidate.sequence = static_cast<uint16_t>(candidate.sequence + 1);
  candidate.crc = store::crc(candidate);
  if (!store::valid(candidate)) return false;
  File file = SFUD.open(store::paths[store::nextSlot], "w");
  if (!file) return false;
  bool ok = file.write(reinterpret_cast<const uint8_t*>(&candidate), sizeof candidate) == sizeof candidate;
  file.close();
  if (ok) {
    file = SFUD.open(store::paths[store::nextSlot], "r");
    fridge::Data check;
    ok = file && file.size() == sizeof check &&
      file.read(reinterpret_cast<uint8_t*>(&check), sizeof check) == sizeof check &&
      store::valid(check) && memcmp(&candidate, &check, sizeof candidate) == 0;
    if (file) file.close();
  }
  if (!ok) return false;
  fridgeData.sequence = candidate.sequence;
  fridgeData.crc = candidate.crc;
  store::nextSlot = 1 - store::nextSlot;
  return true;
}
