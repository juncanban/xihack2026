// 无设备测试：真实数量逻辑和真实双槽存储，使用内存文件与故障注入。
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "Arduino.h"
#include "Seeed_Arduino_FS.h"
#include "Seeed_SFUD.h"
#include "../firmware/src/fridge_logic.h"

uint32_t fakeMillis = 0;
FakeSerial Serial;
FakeSFUD SFUD;
bool failWrites = false, fsAvailable = true;
std::map<std::string, std::vector<uint8_t>> fakeFiles;
bool saveFsOk() { return fsAvailable; }

enum StoreFault { STORE_NONE, SHORT_WRITE, READBACK_ERROR, STALE_READBACK };
static StoreFault storeFault = STORE_NONE;
static std::vector<uint8_t> staleReadback;
class FridgeFaultFile {
  File file;
public:
  FridgeFaultFile() = default;
  FridgeFaultFile(const char* path, bool write) : file(path, write) {}
  operator bool() const { return static_cast<bool>(const_cast<File&>(file)); }
  size_t size() const { return const_cast<File&>(file).size(); }
  size_t read(uint8_t* bytes, size_t count) {
    if (storeFault == READBACK_ERROR) return 0;
    if (storeFault == STALE_READBACK) {
      const size_t copied = std::min(count, staleReadback.size());
      if (copied) memcpy(bytes, staleReadback.data(), copied);
      return copied;
    }
    return file.read(bytes, count);
  }
  size_t write(const uint8_t* bytes, size_t count) {
    return file.write(bytes, storeFault == SHORT_WRITE ? count / 2 : count);
  }
  void close() { file.close(); }
};
struct FridgeFaultFs {
  FridgeFaultFile open(const char* path, const char* mode) { return FridgeFaultFile(path, mode[0] == 'w'); }
} fridgeFaultSFUD;

// 仅当前测试替换文件类型，固件仍使用 Seeed 原生 File/SFUD。
#define File FridgeFaultFile
#define SFUD fridgeFaultSFUD
#include "../firmware/src/fridge_store.cpp"
#undef SFUD
#undef File

static constexpr uint32_t TODAY = 20261002, TOMORROW = 20261003;
static bool equal(const fridge::Data& a, const fridge::Data& b) { return memcmp(&a, &b, sizeof a) == 0; }
static void itemIs(const fridge::Item& item, int stock, int eaten, int incoming) {
  assert(item.stock == stock && item.eaten == eaten && item.incoming == incoming);
}
static fridge::Data fixture(uint8_t stock = 10, uint8_t eaten = 2, uint8_t incoming = 3) {
  fridge::Data data;
  fridge::reset(data);
  assert(fridge::syncDay(data, TODAY));
  data.items[0] = {stock, eaten, incoming};
  return data;
}
static void rejected(fridge::Data& data, uint8_t item, fridge::Action action,
                     uint8_t value, uint32_t day, fridge::Result expected) {
  const fridge::Data before = data;
  fridge::Item out = {81,82,83};
  assert(fridge::preview(data, item, action, value, day, out) == expected);
  itemIs(out, 81,82,83);
  assert(equal(data, before));
  assert(fridge::apply(data, item, action, value, day) == expected);
  assert(equal(data, before));
}
static void testActions() {
  using namespace fridge;
  Data data;
  reset(data);
  assert(data.magic == MAGIC && data.version == VERSION && data.sequence == 0 && data.date == 0);
  for (const Item& item : data.items) itemIs(item, 0,0,0);
  assert(!data.undo.valid);
  rejected(data, 0, RESTOCK, 1, 0, NEED_CLOCK);
  assert(apply(data, 5, SET_STOCK, 10, 0) == OK);
  itemIs(data.items[5], 10,0,0);
  assert(data.undo.valid && data.undo.item == 5 && data.date == 0);
  assert(undo(data, 0) == OK);
  itemIs(data.items[5], 0,0,0);
  assert(undo(data, 0) == NO_UNDO);
  assert(syncDay(data, TODAY));
  assert(apply(data, 0, RESTOCK, 10, TODAY) == OK);
  itemIs(data.items[0], 10,0,10);
  const Data beforePreview = data;
  Item out;
  assert(preview(data, 0, EAT, 3, TODAY, out) == OK);
  itemIs(out, 7,3,10);
  assert(equal(data, beforePreview));
  assert(apply(data, 0, EAT, 3, TODAY) == OK);
  itemIs(data.items[0], 7,3,10);
  assert(apply(data, 0, SET_EATEN, 1, TODAY) == OK);
  itemIs(data.items[0], 9,1,10);
  assert(apply(data, 0, SET_EATEN, 4, TODAY) == OK);
  itemIs(data.items[0], 6,4,10);
  assert(apply(data, 0, SET_INCOMING, 12, TODAY) == OK);
  itemIs(data.items[0], 8,4,12);
  assert(apply(data, 0, SET_INCOMING, 5, TODAY) == OK);
  itemIs(data.items[0], 1,4,5);
  assert(apply(data, 0, SET_STOCK, 20, 0) == OK);
  itemIs(data.items[0], 20,4,5);
  const Data unchanged = data;
  assert(apply(data, 0, SET_STOCK, 20, 0) == NO_CHANGE && equal(data, unchanged));
  assert(apply(data, 0, SET_EATEN, 4, TODAY) == NO_CHANGE && equal(data, unchanged));
  assert(apply(data, 0, SET_INCOMING, 5, TODAY) == NO_CHANGE && equal(data, unchanged));
  assert(undo(data, TODAY) == OK);
  itemIs(data.items[0], 1,4,5);
  assert(!data.undo.valid && undo(data, TODAY) == NO_UNDO);
  for (uint8_t i = 1; i < FOOD_COUNT; ++i) itemIs(data.items[i], 0,0,0);
  std::cout << "PASS actions, correction deltas, preview, no-change preservation and zero defaults\n";
}
static void testRejections() {
  using namespace fridge;
  Data data = fixture();
  assert(apply(data, 1, SET_STOCK, 4, 0) == OK); // 已存在的撤销也必须原样保留。
  rejected(data, FOOD_COUNT, EAT, 1, TODAY, BAD_VALUE);
  rejected(data, 255, SET_STOCK, 0, TODAY, BAD_VALUE);
  rejected(data, 0, static_cast<Action>(255), 1, TODAY, BAD_VALUE);
  rejected(data, 0, EAT, 0, TODAY, BAD_VALUE);
  rejected(data, 0, RESTOCK, 0, TODAY, BAD_VALUE);
  for (uint8_t action = EAT; action <= SET_STOCK; ++action) {
    rejected(data, 0, static_cast<Action>(action), 100, TODAY, BAD_VALUE);
    rejected(data, 0, static_cast<Action>(action), 255, TODAY, BAD_VALUE);
  }
  for (uint8_t action = EAT; action < SET_STOCK; ++action) {
    rejected(data, 0, static_cast<Action>(action), 1, 0, NEED_CLOCK);
    rejected(data, 0, static_cast<Action>(action), 1, 20260230, NEED_CLOCK);
    rejected(data, 0, static_cast<Action>(action), 1, TOMORROW, NEED_CLOCK);
  }
  rejected(data, 0, EAT, 11, TODAY, NO_STOCK);
  rejected(data, 0, SET_EATEN, 13, TODAY, NO_STOCK);
  data.items[0] = {1,2,3};
  rejected(data, 0, SET_INCOMING, 1, TODAY, NO_STOCK);
  data.items[0] = {99,2,3};
  rejected(data, 0, RESTOCK, 1, TODAY, OVER_LIMIT);
  rejected(data, 0, SET_EATEN, 1, TODAY, OVER_LIMIT);
  rejected(data, 0, SET_INCOMING, 4, TODAY, OVER_LIMIT);
  data.items[0] = {2,99,99};
  rejected(data, 0, EAT, 1, TODAY, OVER_LIMIT);
  rejected(data, 0, RESTOCK, 1, TODAY, OVER_LIMIT);
  data.items[0] = {100,0,0};
  rejected(data, 0, SET_STOCK, 0, 0, BAD_VALUE);
  data = fixture(99,0,0);
  assert(apply(data, 0, EAT, 99, TODAY) == OK);
  itemIs(data.items[0], 0,99,0);
  assert(undo(data, TODAY) == OK);
  data = fixture(0,0,0);
  assert(apply(data, 0, RESTOCK, 99, TODAY) == OK);
  itemIs(data.items[0], 99,0,99);
  assert(apply(data, 0, SET_INCOMING, 0, TODAY) == OK);
  itemIs(data.items[0], 0,0,0);
  std::cout << "PASS rejected operations leave data/output/undo unchanged, zero and 99 limits\n";
}
static void testDayAndUndo() {
  using namespace fridge;
  assert(validDay(20240229) && !validDay(20260229) && !validDay(20261301));
  assert(validDay(20200101) && validDay(20991231) && !validDay(20191231) && !validDay(21000101));
  Data data = fixture();
  assert(apply(data, 0, EAT, 2, TODAY) == OK);
  const Data before = data;
  assert(!syncDay(data, 0) && equal(data, before));
  assert(!syncDay(data, 20260230) && equal(data, before));
  assert(!syncDay(data, TODAY) && equal(data, before));
  assert(undo(data, 0) == NEED_CLOCK && equal(data, before));
  assert(undo(data, TOMORROW) == NEED_CLOCK && equal(data, before));
  assert(undo(data, TODAY) == OK);
  itemIs(data.items[0], 10,2,3);
  assert(apply(data, 0, EAT, 1, TODAY) == OK);
  assert(apply(data, 5, RESTOCK, 4, TODAY) == OK);
  assert(undo(data, TODAY) == OK);
  itemIs(data.items[0], 9,3,3);
  itemIs(data.items[5], 0,0,0);
  assert(undo(data, TODAY) == NO_UNDO); // 不回到更早的一笔。
  assert(apply(data, 5, RESTOCK, 6, TODAY) == OK);
  assert(syncDay(data, TOMORROW));
  itemIs(data.items[0], 9,0,0);
  itemIs(data.items[5], 6,0,0);
  assert(data.date == TOMORROW && !data.undo.valid && undo(data, TOMORROW) == NO_UNDO);
  const Data newDay = data;
  rejected(data, 0, EAT, 1, TODAY, NEED_CLOCK);
  assert(equal(data, newDay));
  assert(apply(data, 0, SET_STOCK, 8, 0) == OK);
  assert(undo(data, 0) == OK); // 库存修正不需要时钟。
  itemIs(data.items[0], 9,0,0);
  assert(apply(data, 0, SET_STOCK, 8, 0) == OK);
  const Data stockEdit = data;
  assert(undo(data, 20261004) == NEED_CLOCK && equal(data, stockEdit));
  assert(syncDay(data, 20261004) && !data.undo.valid);
  data = fixture();
  assert(apply(data, 0, EAT, 1, TODAY) == OK);
  data.undo.item = FOOD_COUNT;
  const Data corruptUndo = data;
  assert(undo(data, TODAY) == BAD_VALUE && equal(data, corruptUndo));
  std::cout << "PASS dates, unsynchronized clock, same-day undo and cross-day reset\n";
}
static fridge::Data diskData(const char* path) {
  fridge::Data data;
  assert(fakeFiles[path].size() == sizeof data);
  memcpy(&data, fakeFiles[path].data(), sizeof data);
  return data;
}
static void putDisk(const char* path, fridge::Data data) {
  data.crc = fridge_store_detail::crc(data);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
  fakeFiles[path].assign(bytes, bytes + sizeof data);
}
static void assertSaveFailure(StoreFault fault, bool failOpen = false) {
  const fridge::Data before = fridgeData;
  const uint8_t slot = fridge_store_detail::nextSlot;
  storeFault = fault;
  failWrites = failOpen;
  assert(!fridgeSave());
  assert(equal(fridgeData, before) && fridge_store_detail::nextSlot == slot);
  storeFault = STORE_NONE;
  failWrites = false;
}
static void testStorage() {
  using namespace fridge;
  namespace store = fridge_store_detail;
  fakeFiles.clear();
  fakeFiles["/pet0.dat"] = {1,2,3};
  fakeFiles["/farm0.dat"] = {4,5,6};
  fridgeLoad();
  assert(fridgeData.date == 0 && fridgeData.sequence == 0 && store::nextSlot == 0);
  for (const Item& item : fridgeData.items) itemIs(item, 0,0,0);
  assert(syncDay(fridgeData, TODAY));
  assert(apply(fridgeData, 0, RESTOCK, 10, TODAY) == OK && fridgeSave());
  assert(fridgeData.sequence == 1 && store::nextSlot == 1);
  assert(store::valid(diskData(store::paths[0])));
  assert(apply(fridgeData, 0, EAT, 2, TODAY) == OK && fridgeSave());
  assert(fridgeData.sequence == 2 && store::nextSlot == 0);
  fridgeLoad();
  itemIs(fridgeData.items[0], 8,2,10);
  assert(fridgeData.undo.valid && undo(fridgeData, TODAY) == OK);
  itemIs(fridgeData.items[0], 10,0,10);
  fridgeLoad();
  fakeFiles[store::paths[1]][0] ^= 1; // 最新槽 CRC 错误，回退到上一份。
  fridgeLoad();
  itemIs(fridgeData.items[0], 10,0,10);
  assert(fridgeData.sequence == 1 && store::nextSlot == 1);
  assert(apply(fridgeData, 0, SET_STOCK, 11, TODAY) == OK);
  assertSaveFailure(STORE_NONE, true);
  assertSaveFailure(SHORT_WRITE);
  assertSaveFailure(READBACK_ERROR);
  staleReadback = fakeFiles[store::paths[0]]; // CRC 正确的旧内容仍不算回读成功。
  assertSaveFailure(STALE_READBACK);
  assert(fridgeSave() && fridgeData.sequence == 2 && store::nextSlot == 0);
  fridgeLoad();
  itemIs(fridgeData.items[0], 11,0,10);
  assert(apply(fridgeData, 0, SET_STOCK, 12, TODAY) == OK);
  assertSaveFailure(SHORT_WRITE);
  fridgeLoad();
  itemIs(fridgeData.items[0], 11,0,10);
  const Data beforeOffline = fridgeData;
  const uint8_t slot = store::nextSlot;
  fsAvailable = false;
  assert(!fridgeSave() && equal(fridgeData, beforeOffline) && store::nextSlot == slot);
  fsAvailable = true;
  assert(fakeFiles["/pet0.dat"] == std::vector<uint8_t>({1,2,3}));
  assert(fakeFiles["/farm0.dat"] == std::vector<uint8_t>({4,5,6}));

  // CRC 正确但字段无效，也必须拒绝，不能仅凭校验和加载。
  Data invalid = diskData(store::paths[1]);
  invalid.sequence = 3;
  invalid.items[0].stock = 100;
  putDisk(store::paths[0], invalid);
  fridgeLoad();
  assert(fridgeData.sequence == 2 && fridgeData.items[0].stock == 11);
  invalid = fridgeData; invalid.undo.item = FOOD_COUNT;
  putDisk(store::paths[0], invalid);
  fridgeLoad();
  assert(fridgeData.sequence == 2 && fridgeData.items[0].stock == 11);
  invalid = fridgeData; invalid.date = 20260230;
  putDisk(store::paths[0], invalid);
  fridgeLoad();
  assert(fridgeData.date == TODAY);
  const Data beforeInvalidSave = fridgeData;
  fridgeData.items[0].stock = 100;
  assertSaveFailure(STORE_NONE);
  fridgeData = beforeInvalidSave;

  // uint16 序号回绕后，0 比 65535 新。
  Data older = fixture(20,1,2); older.sequence = 65535;
  Data newer = fixture(21,1,2); newer.sequence = 0;
  putDisk(store::paths[0], older); putDisk(store::paths[1], newer);
  fridgeLoad();
  assert(fridgeData.sequence == 0 && fridgeData.items[0].stock == 21 && store::nextSlot == 0);
  putDisk(store::paths[0], newer); putDisk(store::paths[1], older);
  fridgeLoad();
  assert(fridgeData.sequence == 0 && fridgeData.items[0].stock == 21 && store::nextSlot == 1);
  assert(syncDay(fridgeData, TOMORROW) && fridgeSave());
  fridgeLoad();
  itemIs(fridgeData.items[0], 21,0,0);
  assert(fridgeData.date == TOMORROW && !fridgeData.undo.valid);
  fsAvailable = false;
  fridgeLoad();
  assert(fridgeData.date == 0 && fridgeData.sequence == 0 && store::nextSlot == 0);
  for (const Item& item : fridgeData.items) itemIs(item, 0,0,0);
  fsAvailable = true;
  std::cout << "PASS independent slots, persistent undo, CRC/schema fallback, sequence wrap and failure-safe commits\n";
}
int main() {
  testActions();
  testRejections();
  testDayAndUndo();
  testStorage();
  std::cout << "ALL FRIDGE TESTS PASSED\n";
}
