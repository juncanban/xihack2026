// save.cpp —— 宠物档案持久化(M5)
//
// Seeed 官方 QSPI flash 文件系统(SFUD + fatfs,板载 4MB),双文件轮转 +
// seq + CRC32 防掉电写坏。写策略绝不进帧循环:脏标记 + 满 1h 刷写;
// 领养/进化/改名立即写。
//
// 单一数据源:活值(累计/今日)由 sun.cpp 持有;本模块只负责
// 启动回灌(sunRestore)与刷写时拉快照,不做第二份记账。
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#include "pet_data.h"

#define SAVE_MAGIC   0x56504554UL   // "VPET"
#define SAVE_VERSION 1
#define MAX_MILE     16
#define NSAVEFILES   2              // 双文件轮转,防写一半掉电

struct PetSave {
  uint32_t magic;
  uint16_t version;
  uint16_t seq;            // 档案序号,单调递增(回绕用 int16 差比较)
  char     name[13];
  uint32_t adoptEpoch;     // 0 = 未领养
  uint32_t totalNutri;     // 累计阳光 Q8(刷写时拉快照)
  uint16_t todayGain;      // 今日已得 Q8
  uint32_t todayDate;      // YYYYMMDD
  uint8_t  stage;          // 0..3
  uint8_t  mCount;
  Milestone mile[MAX_MILE];
  uint32_t crc32;          // 对前面全部字节
};

static PetSave gSave;
static bool    sDirty = false;
static uint32_t sDirtySince = 0;
static uint8_t  sNextFile = 0;
static bool     sFsOk = false;

static const char* SAVE_PATH[NSAVEFILES] = { "/pet0.dat", "/pet1.dat" };

// ---------------- CRC32(位反 0xEDB88320;启动校验用,不查表) ----------------
static uint32_t crc32buf(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (n--) {
    crc ^= *p++;
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}

static uint32_t saveCrc(const PetSave& s) {
  return crc32buf((const uint8_t*)&s, sizeof(PetSave) - sizeof(uint32_t));
}

// seq 回绕安全比较:a 更新返回 true
static bool seqNewer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

static bool loadFile(uint8_t i, PetSave& out) {
  if (!sFsOk) return false;
  File f = SFUD.open(SAVE_PATH[i], "r");
  if (!f) return false;
  bool ok = false;
  if (f.size() == sizeof(PetSave)) {
    PetSave tmp;
    if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) &&
        tmp.magic == SAVE_MAGIC && tmp.version == SAVE_VERSION &&
        tmp.mCount <= MAX_MILE && saveCrc(tmp) == tmp.crc32) {
      out = tmp;
      ok = true;
    }
  }
  f.close();
  return ok;
}

static void markDirty() {
  if (!sDirty) { sDirty = true; sDirtySince = millis(); }
}

// ---------------- 对外接口 ----------------
bool saveLoad() {
  memset(&gSave, 0, sizeof(gSave));
  sFsOk = SFUD.begin(104000000UL);   // 板载 QSPI flash 挂载
  if (!sFsOk) Serial.println("WARN:SFUD mount fail, pet RAM-only");

  bool have = false;
  PetSave cand;
  for (uint8_t i = 0; i < NSAVEFILES; i++) {
    if (!loadFile(i, cand)) continue;
    if (!have || seqNewer(cand.seq, gSave.seq)) { gSave = cand; have = true; }
  }
  if (!have) {
    memset(&gSave, 0, sizeof(gSave));
    gSave.magic = SAVE_MAGIC;
    gSave.version = SAVE_VERSION;
    strcpy(gSave.name, DEFAULT_PET_NAME);
    strcpy(gPetName, DEFAULT_PET_NAME);
  } else {
    strcpy(gPetName, gSave.name[0] ? gSave.name : DEFAULT_PET_NAME);
  }
  // 回灌活值(sun.cpp 持有);日期不符时 sun 自行清今日
  sunRestore(gSave.totalNutri, gSave.todayGain, gSave.todayDate);
  markDirty();   // 无档案/结构升级时 1h 内落一次盘兜底
  return have;
}

void saveOnAdopt(uint32_t epoch) {
  if (gSave.adoptEpoch != 0) return;
  gSave.adoptEpoch = epoch;
  if (gSave.mCount < MAX_MILE) {
    gSave.mile[gSave.mCount] = { epoch, 0, {0, 0, 0} };   // kind 0 = 领养
    gSave.mCount++;
  }
  saveFlushNow();
}

void saveOnEvolve(uint8_t newStage, uint32_t epoch) {   // M6 调用
  gSave.stage = newStage;
  if (gSave.mCount < MAX_MILE) {
    gSave.mile[gSave.mCount] = { epoch, newStage, {0, 0, 0} };
    gSave.mCount++;
  }
  saveFlushNow();
}

void saveRename(const char* name) {
  strncpy(gSave.name, name, sizeof(gSave.name) - 1);
  gSave.name[sizeof(gSave.name) - 1] = '\0';
  saveFlushNow();
}

void saveFlushNow() {
  if (!sFsOk) return;
  // 刷写前拉活值快照;注意 seq 必须先自增再算 CRC(CRC 覆盖 seq 字段)
  gSave.totalNutri = sunTotalQ8();
  gSave.todayGain  = sunTodayQ8();
  gSave.todayDate  = sunTodayDate();
  gSave.seq++;
  gSave.crc32 = saveCrc(gSave);
  File f = SFUD.open(SAVE_PATH[sNextFile], "w");
  if (f) {
    f.write((uint8_t*)&gSave, sizeof(gSave));
    f.close();
    sNextFile = (sNextFile + 1) % NSAVEFILES;
  }
  sDirty = false;
}

void saveFlushIfDue(uint32_t nowMs) {
  if (!sDirty) return;
  if (nowMs - sDirtySince < 3600UL * 1000) return;   // 满 1h 才写
  saveFlushNow();
}

// Q 命令诊断:列出档案文件状态
void saveDumpInfo() {
  if (!sFsOk) { Serial.println("Q:no fs"); return; }
  File dir = SFUD.open("/");
  if (dir) {
    File e = dir.openNextFile();
    Serial.println("Q:root:");
    while (e) {
      Serial.print("  "); Serial.print(e.name()); Serial.print(" "); Serial.println(e.size());
      e.close(); // Seeed File destructor does not release its FAT buffer.
      e = dir.openNextFile();
    }
    dir.close();
  } else {
    Serial.println("Q:root open fail");
  }
  for (uint8_t i = 0; i < NSAVEFILES; i++) {
    File f = SFUD.open(SAVE_PATH[i], "r");
    if (!f) { Serial.print(SAVE_PATH[i]); Serial.println(": missing"); continue; }
    uint32_t sz = f.size();
    PetSave tmp;
    bool ok = false;
    if (sz == sizeof(PetSave) && f.read((uint8_t*)&tmp, sizeof tmp) == (int)sizeof tmp)
      ok = (tmp.magic == SAVE_MAGIC && saveCrc(tmp) == tmp.crc32);
    char b[96];
    snprintf(b, sizeof b, "%s: size=%lu expect=%u seq=%u magicOK=%d crcOK=%d",
             SAVE_PATH[i], (unsigned long)sz, (unsigned)sizeof(PetSave),
             tmp.seq, tmp.magic == SAVE_MAGIC, ok);
    Serial.println(b);
    // hexdump 前 24 字节 + CRC 区,定位损坏
    Serial.print("  head:");
    for (int k = 0; k < 24; k++) { Serial.printf(" %02X", ((uint8_t*)&tmp)[k]); }
    Serial.println();
    Serial.print("  storedCRC=");
    Serial.print(tmp.crc32, HEX);
    Serial.print(" calcCRC=");
    Serial.println(saveCrc(tmp), HEX);
    Serial.print("  ramSeq="); Serial.print(gSave.seq);
    Serial.print(" ramName="); Serial.println(gSave.name);
    f.close();
  }
}

// 出厂重置:X 命令,清档案文件回未领养状态(开发/重新养成用)
void saveFactoryReset() {
  gSave.magic = SAVE_MAGIC;
  gSave.version = SAVE_VERSION;
  gSave.seq = 0;
  gSave.name[0] = '\0';
  gSave.adoptEpoch = 0;
  gSave.totalNutri = 0;
  gSave.todayGain = 0;
  gSave.todayDate = 0;
  gSave.stage = 0;
  gSave.mCount = 0;
  if (sFsOk) {
    for (uint8_t i = 0; i < NSAVEFILES; i++) SFUD.remove(SAVE_PATH[i]);
  }
  sunRestore(0, 0, 0);
  strcpy(gPetName, DEFAULT_PET_NAME);
  saveFlushNow();
}

// 只读接口(STATUS/MEMORIAL 屏用)
uint32_t saveAdoptEpoch() { return gSave.adoptEpoch; }
bool     saveAdopted()    { return gSave.adoptEpoch != 0; }
uint8_t  saveStage()      { return gSave.stage; }
uint8_t  saveMileCount()  { return gSave.mCount; }
const Milestone* saveMile(uint8_t i) { return &gSave.mile[i]; }
bool     saveFsOk()       { return sFsOk; }
