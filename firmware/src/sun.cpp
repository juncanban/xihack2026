// sun.cpp —— 光照采样 + 营养记账(M3)
//
// 板载 ALS-PT19 光敏三极管(WIO_LIGHT/A6,12bit ADC,直晒饱和于 ~4095)。
// 采样复用 D:\wio 项目 readLight 整数平均模式;记账用 Q8 定点防截断:
//   每秒 tick:sFracQ8 += rateQ8;满 60 进位 1 单位(= 1/256 点·秒分辨率)
// 防刷分:舒适区满分、暴晒区负速率扣今日已得(下限 0,不扣累计)、每日配额硬上限。
#include <Arduino.h>
#include "pet_data.h"

const ZoneSpec ZONES[NZONES] = {
  {    0,    0, "DARK",   TFT_DARKGREY },   // <400 夜晚/暗处:无收益
  {  400,   19, "DIM",    TFT_SKYBLUE  },   // 0.25x 爬坡
  { 2400,   77, "COMFY",  TFT_GREEN    },   // 1x 舒适(名义 10k-30k lux)
  { 3300,   31, "BRIGHT", TFT_ORANGE   },   // 0.4x 递减
  { 3950,  -64, "SCORCH", TFT_RED      },   // 饱和直晒:扣今日已得,宠物哭
};

static uint32_t sLastTick = 0;
static uint16_t sRaw = 0;          // 最新 16 样本均值
static int8_t   sZone = 0;
static int16_t  sFracQ8 = 0;       // 进位余量(Q8·点·秒/分)
static uint32_t sTotalQ8 = 0;      // 累计阳光
static uint16_t sTodayQ8 = 0;      // 今日已得
static uint32_t sTodayDate = 0;    // YYYYMMDD,跨日清零
static int16_t  sForcedRaw = -1;   // L: 强制注入(-1 = 实读)
static int32_t  sDisp = 0;         // 显示电平 0..255(EMA)

#define CAP_Q8 ((uint32_t)DAY_CAP_PTS * 256)

// 光照采样:n 次整数平均,抑制单次读数抖动
static uint16_t readLight(uint8_t n) {
  uint32_t s = 0;
  for (uint8_t i = 0; i < n; i++) s += analogRead(WIO_LIGHT);
  return (uint16_t)(s / n);
}

static int8_t zoneOf(uint16_t raw) {
  for (int8_t i = NZONES - 1; i >= 0; i--)
    if (raw >= ZONES[i].lo) return i;
  return 0;
}

void sunInit() {
  pinMode(WIO_LIGHT, INPUT);
  analogReadResolution(12);
  sTodayDate = dateCode();
  sLastTick = millis();
}

void sunTick() {
  uint32_t now = millis();
  if (now - sLastTick < 1000) return;
  uint32_t dt = (now - sLastTick) / 1000;    // 秒(通常 1)
  sLastTick = now;

  // 采样(16 次均值,亚毫秒级)
  sRaw = (sForcedRaw >= 0) ? (uint16_t)sForcedRaw : readLight(16);
  sZone = zoneOf(sRaw);

  // 跨日:今日清零(掉电跨天由 M5 boot 对账兜底)
  uint32_t dc = dateCode();
  if (dc && sTodayDate && dc != sTodayDate) sTodayQ8 = 0;
  if (dc) sTodayDate = dc;

  // 记账:拿满配额后正速率归零;暴晒(负)不受配额限制
  int32_t rate = ZONES[sZone].rateQ8Min;
  if (rate > 0 && sTodayQ8 >= CAP_Q8) rate = 0;
  sFracQ8 += rate * (int32_t)dt;
  if (sFracQ8 > 0) {
    uint32_t gain = (uint32_t)sFracQ8 / 60;
    sFracQ8 %= 60;
    uint32_t room = CAP_Q8 - sTodayQ8;
    if (gain > room) { gain = room; sFracQ8 = 0; }
    sTodayQ8 += (uint16_t)gain;
    sTotalQ8 += gain;
  } else if (sFracQ8 < 0) {
    uint32_t drain = (uint32_t)(-sFracQ8) / 60;
    sFracQ8 = -(int16_t)((uint32_t)(-sFracQ8) % 60);
    if (drain > sTodayQ8) drain = sTodayQ8;    // 下限 0,不变负
    sTodayQ8 -= (uint16_t)drain;
  }
}

uint16_t sunRaw()        { return sRaw; }
int8_t   sunZone()       { return sZone; }
uint32_t sunTotalQ8()    { return sTotalQ8; }
uint16_t sunTodayQ8()    { return sTodayQ8; }
uint32_t sunTodayDate()  { return sTodayDate; }
uint16_t sunCapPts()     { return DAY_CAP_PTS; }
bool     sunScorching()  { return sZone == NZONES - 1; }

// saveLoad 启动回灌:日期不符时 sunTick 的跨日逻辑自行清零今日
void sunRestore(uint32_t totalQ8, uint16_t todayQ8, uint32_t todayDate) {
  sTotalQ8 = totalQ8;
  sTodayQ8 = todayQ8;
  sTodayDate = todayDate;
}

// 显示电平:每帧向 raw 目标 EMA 逼近 -> 30fps 丝滑渐变(采样仍 1Hz)
uint16_t sunDispLevel() {
  int32_t target = (int32_t)sRaw * 255 / 3300;
  if (target > 255) target = 255;
  sDisp += (target - sDisp) / 12;
  if (sDisp < 0) sDisp = 0;
  if (sDisp > 255) sDisp = 255;
  return (uint16_t)sDisp;
}

void sunForceRaw(int16_t raw) { sForcedRaw = raw; }
