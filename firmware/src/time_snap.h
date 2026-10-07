// 断电时间快照(见 TIME_PERSISTENCE.md):flash 尾扇区 4KB,只追加免擦除。
// 纯逻辑(记录编解码/扇区扫描)宿主可测;设备 IO 仅在 Wio Terminal 固件内编译。
#ifndef TIME_SNAP_H
#define TIME_SNAP_H
#include <stdint.h>

#define TIME_SNAP_MAGIC 0x4E534D54UL   // "TMSN"
#define TIME_SNAP_SLOTS 256            // 4096 / 16

struct TimeSnapRecord {
  uint32_t magic;
  uint32_t epoch;
  uint32_t crc32;                // 覆盖 magic+epoch
  uint32_t pad;                  // 必须 0
};

struct TimeSnapScan {
  bool hasValue;                 // 扫到合法记录
  uint32_t epoch;                // 最后一条合法记录的时间
  uint16_t nextSlot;             // 续写槽位(最后合法记录之后,跳过半写坏槽)
  bool foreign;                  // 有数据但无合法记录:疑似 FAT 曾占用,不擦不用
};

// ---- 纯逻辑(宿主测试覆盖) ----
uint32_t timeSnapCrc(const TimeSnapRecord& r);          // 位反 0xEDB88320,同 save.cpp
bool timeSnapRecordValid(const TimeSnapRecord& r);      // magic+crc+pad+epoch 闸门
TimeSnapScan timeSnapScanSector(const uint8_t* sector, uint32_t size);  // 4KB 原始扇区

// ---- 设备侧(固件内编译) ----
void timeSnapInit();               // saveLoad 挂载后调用:扫描尾扇区并恢复
void timeSnapTick(uint32_t nowMs); // loop 节拍:每 10s 追加一条
void timeSnapWriteNow();           // 串口校时成功后立即追加
void timeSnapWipe();               // X 出厂重置时擦除
#endif
