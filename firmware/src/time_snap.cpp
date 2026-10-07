// time_snap.cpp —— 断电时间快照
//
// SAMD51 无 VBAT,真掉电后硬件 RTC 清零;本模块把 epoch 每 10 秒追加写入
// 板载 QSPI flash 最后一个 4KB 扇区(绕过 FAT,避免文件系统"一写一擦 4KB"
// 写穿磨损;FAT 卷覆盖整片芯片,故约定本设备永远不存大文件,保证尾扇区空闲)。
// NOR 编程只能 1→0,追加写不需要擦;256 条写满才擦一次,10 秒节拍下扇区
// 寿命约 8 年。恢复新鲜度 ≤10 秒 + 断电时长,热插拔场景即秒级。
#include <string.h>
#include "pet_clock.h"
#include "time_snap.h"

uint32_t timeSnapCrc(const TimeSnapRecord& r) {
  // 位反 0xEDB88320,按字节(同 save.cpp);只覆盖 magic+epoch
  uint32_t crc = 0xFFFFFFFFUL;
  const uint8_t* p = (const uint8_t*)&r.magic;
  for (uint8_t i = 0; i < 8; i++) {
    crc ^= p[i];
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}

bool timeSnapRecordValid(const TimeSnapRecord& r) {
  return r.magic == TIME_SNAP_MAGIC && r.pad == 0 &&
         r.crc32 == timeSnapCrc(r) && clockEpochPlausible(r.epoch);
}

TimeSnapScan timeSnapScanSector(const uint8_t* sector, uint32_t size) {
  TimeSnapScan out = {};
  const uint32_t n = size / sizeof(TimeSnapRecord);
  uint32_t i = 0;
  for (; i < n; i++) {
    TimeSnapRecord r;
    memcpy(&r, sector + i * sizeof(r), sizeof(r));
    if (r.magic == 0xFFFFFFFFUL && r.epoch == 0xFFFFFFFFUL &&
        r.crc32 == 0xFFFFFFFFUL && r.pad == 0xFFFFFFFFUL) break;   // 空槽,后面只会更空
    if (timeSnapRecordValid(r)) {
      out.hasValue = true;
      out.epoch = r.epoch;
    }
  }
  out.nextSlot = (uint16_t)i;              // 续写在首个空槽(半写坏槽自然落在边界前)
  out.foreign = !out.hasValue && n && sector[0] != 0xFF;   // 有数据无合法记录
  return out;
}

#if defined(ARDUINO) && defined(WIO_TERMINAL)
#define TIME_SNAP_DEV 1
#endif

#ifdef TIME_SNAP_DEV

#include <Arduino.h>
#include <Seeed_SFUD.h>
#include <sfud.h>

static const uint32_t SNAP_PERIOD_MS = 10UL * 1000;
static const sfud_flash* sDev = nullptr;
static uint32_t    sBase = 0;
static bool        sOk = false;
static bool        sHasValue = false;
static uint16_t    sNextSlot = 0;
static uint32_t    sLastWriteMs = 0;

static bool snapProgram(uint16_t slot, uint32_t epoch) {
  TimeSnapRecord r = { TIME_SNAP_MAGIC, epoch, 0, 0 };
  r.crc32 = timeSnapCrc(r);
  return sfud_write(sDev, sBase + (uint32_t)slot * sizeof(r), sizeof(r),
                    (const uint8_t*)&r) == SFUD_SUCCESS;
}

void timeSnapInit() {
  sDev = sfud_get_device_table();
  if (!sDev || sDev->chip.capacity < 8192) return;         // FS 未挂上,裸区不可用
  sBase = sDev->chip.capacity - 4096UL;
  uint8_t* buf = new uint8_t[4096];                        // 仅启动期,读完整扇区
  if (!buf) return;
  const bool readOk = sfud_read(sDev, sBase, 4096, buf) == SFUD_SUCCESS;
  const TimeSnapScan scan = readOk ? timeSnapScanSector(buf, 4096) : TimeSnapScan{};
  delete[] buf;
  if (!readOk) { Serial.println("SNAP:READFAIL"); return; }
  if (scan.foreign) {                                      // 保护 FAT 数据:不擦不用
    Serial.println("SNAP:FOREIGN");
    return;
  }
  sOk = true;
  sHasValue = scan.hasValue;
  sNextSlot = scan.nextSlot;
  if (scan.hasValue) {
    Serial.print("SNAP:READY:"); Serial.println(scan.nextSlot);
    if (!clockIsSet()) clockRestore(scan.epoch);           // RTC 层未恢复时兜底
  } else {
    Serial.println("SNAP:EMPTY");
  }
}

void timeSnapWriteNow() {
  if (!sOk || !clockIsSet()) return;
  if (sHasValue && sNextSlot >= TIME_SNAP_SLOTS) {         // 扇区写满:擦除重开
    if (sfud_erase(sDev, sBase, 4096) != SFUD_SUCCESS) return;
    sHasValue = false;
    sNextSlot = 0;
  }
  if (snapProgram(sNextSlot, nowEpoch())) {
    sNextSlot++;
    sHasValue = true;
    sLastWriteMs = millis();
  }
}

void timeSnapTick(uint32_t nowMs) {
  if (!sOk || !clockIsSet()) return;
  if (nowMs - sLastWriteMs < SNAP_PERIOD_MS) return;
  timeSnapWriteNow();
}

void timeSnapWipe() {
  if (sOk && sfud_erase(sDev, sBase, 4096) == SFUD_SUCCESS) {
    sHasValue = false;
    sNextSlot = 0;
    sLastWriteMs = 0;
    Serial.println("SNAP:WIPED");
  }
}

#else   // 宿主测试/其他平台:只编译纯逻辑,设备入口留空(测试不编译本文件的设备段)
void timeSnapInit() {}
void timeSnapWriteNow() {}
void timeSnapTick(uint32_t) {}
void timeSnapWipe() {}
#endif
