// 冰箱数量逻辑，无硬件依赖；日期码与设备一致，使用 YYYYMMDD。
#ifndef FRIDGE_LOGIC_H
#define FRIDGE_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace fridge {

constexpr uint8_t FOOD_COUNT = 6, LIMIT = 99;
constexpr uint32_t MAGIC = 0x46524447UL; // FRDG
constexpr uint16_t VERSION = 1;

struct Item { uint8_t stock, eaten, incoming; };
enum Action : uint8_t { EAT, RESTOCK, SET_EATEN, SET_INCOMING, SET_STOCK };
enum Result : uint8_t { OK, NO_CHANGE, BAD_VALUE, NEED_CLOCK, NO_STOCK, OVER_LIMIT, NO_UNDO };

// 仅保存最近一项操作前的数量，不复制整个存档。
struct Undo {
  uint32_t date;
  Item before;
  uint8_t item;
  uint8_t action, valid, reserved[2];
};
struct Data {
  uint32_t magic;
  uint16_t version, sequence;
  uint32_t date;
  Item items[FOOD_COUNT];
  uint8_t reserved[2];
  Undo undo;
  uint32_t crc;
};
static_assert(sizeof(Item) == 3, "Fridge item layout changed");
static_assert(sizeof(Undo) == 12, "Fridge undo layout changed");
static_assert(sizeof(Data) == 48 && offsetof(Data, crc) == 44, "Fridge storage layout changed");

inline bool validDay(uint32_t day) {
  const uint32_t year = day / 10000;
  const uint8_t month = static_cast<uint8_t>(day / 100 % 100);
  const uint8_t date = static_cast<uint8_t>(day % 100);
  if (year < 2020 || year > 2099 || month < 1 || month > 12 || date < 1) return false;
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return date <= days[month - 1] + (month == 2 && leap);
}
inline bool validItem(const Item& item) {
  return item.stock <= LIMIT && item.eaten <= LIMIT && item.incoming <= LIMIT;
}
inline bool sameItem(const Item& a, const Item& b) {
  return a.stock == b.stock && a.eaten == b.eaten && a.incoming == b.incoming;
}
inline void reset(Data& data) {
  memset(&data, 0, sizeof data);
  data.magic = MAGIC;
  data.version = VERSION;
}
// 返回是否修改了数据；未校时、无效日期及同一天均不修改。
inline bool syncDay(Data& data, uint32_t day) {
  if (!validDay(day) || data.date == day) return false;
  data.date = day;
  for (uint8_t i = 0; i < FOOD_COUNT; ++i) {
    data.items[i].eaten = 0;
    data.items[i].incoming = 0;
  }
  memset(&data.undo, 0, sizeof data.undo);
  return true;
}
// 失败时 data 与 out 均不修改；NO_CHANGE 仍返回当前数量供 UI 预览。
inline Result preview(const Data& data, uint8_t item, Action action,
                      uint8_t value, uint32_t day, Item& out) {
  if (item >= FOOD_COUNT || static_cast<uint8_t>(action) > SET_STOCK || value > LIMIT)
    return BAD_VALUE;
  if ((action == EAT || action == RESTOCK) && value == 0) return BAD_VALUE;
  if (action != SET_STOCK && (!validDay(day) || day != data.date)) return NEED_CLOCK;
  const Item& before = data.items[item];
  if (!validItem(before)) return BAD_VALUE;
  int stock = before.stock, eaten = before.eaten, incoming = before.incoming;
  switch (action) {
    case EAT: stock -= value; eaten += value; break;
    case RESTOCK: stock += value; incoming += value; break;
    case SET_EATEN: stock += eaten - value; eaten = value; break;
    case SET_INCOMING: stock += value - incoming; incoming = value; break;
    case SET_STOCK: stock = value; break;
    default: return BAD_VALUE;
  }
  if (stock < 0) return NO_STOCK;
  if (stock > LIMIT || eaten > LIMIT || incoming > LIMIT) return OVER_LIMIT;
  Item next = {static_cast<uint8_t>(stock), static_cast<uint8_t>(eaten), static_cast<uint8_t>(incoming)};
  out = next;
  return sameItem(before, next) ? NO_CHANGE : OK;
}
inline Result apply(Data& data, uint8_t item, Action action, uint8_t value, uint32_t day) {
  Item next;
  const Result result = preview(data, item, action, value, day, next);
  if (result != OK) return result;
  memset(&data.undo, 0, sizeof data.undo);
  data.undo.date = data.date;
  data.undo.before = data.items[item];
  data.undo.item = item;
  data.undo.action = static_cast<uint8_t>(action);
  data.undo.valid = 1;
  data.items[item] = next;
  return OK;
}
inline Result undo(Data& data, uint32_t day) {
  const Undo& previous = data.undo;
  if (!previous.valid || previous.date != data.date) return NO_UNDO;
  if (previous.valid != 1 || previous.item >= FOOD_COUNT || previous.action > SET_STOCK ||
      !validItem(previous.before)) return BAD_VALUE;
  // 实际库存不依赖时钟；其他撤销必须有同一天的有效时钟。
  // 即便是库存修正，已知新日期也要先 syncDay，不能回滚上一日操作。
  if ((previous.action != SET_STOCK && (!validDay(day) || day != data.date)) ||
      (validDay(day) && day != data.date)) return NEED_CLOCK;
  data.items[previous.item] = previous.before;
  memset(&data.undo, 0, sizeof data.undo);
  return OK;
}

// Direct inventory editing has no date dependency. Restocking never changes
// eaten/incoming; only the net reduction at commit counts as eaten.
inline Result previewInventory(const Data& data, uint8_t item, uint8_t value, Item& out) {
  if (item >= FOOD_COUNT || value > LIMIT || !validItem(data.items[item])) return BAD_VALUE;
  const Item& before = data.items[item];
  const unsigned reduction = before.stock > value ? before.stock - value : 0;
  const unsigned eaten = before.eaten + reduction;
  if (eaten > LIMIT) return OVER_LIMIT;
  out = {value, static_cast<uint8_t>(eaten), before.incoming};
  return sameItem(before, out) ? NO_CHANGE : OK;
}
inline Result applyInventory(Data& data, uint8_t item, uint8_t value) {
  Item next;
  const Result result = previewInventory(data, item, value, next);
  if (result != OK) return result;
  memset(&data.undo, 0, sizeof data.undo);
  data.undo.date = data.date;
  data.undo.before = data.items[item];
  data.undo.item = item;
  data.undo.action = SET_STOCK; // Retain the version-1 storage layout/validation.
  data.undo.valid = 1;
  data.items[item] = next;
  return OK;
}

} // namespace fridge
#endif
