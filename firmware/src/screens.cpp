// screens.cpp —— 公共离屏事务 + STATUS/MEMORIAL 屏 + 16px 中文字模渲染
//
// 借鉴 EspTama 的屏幕注册表模式(无许可证,只用模式):ScreenId 正交于
// PetState;LEFT/RIGHT 切屏,进屏画一次静态 + STATUS 动态行 1Hz 局部刷新。
#include <Arduino.h>
#include "pet_data.h"
#include "hanzi16.h"

static ScreenId sCur = SCR_TODAY;
uint8_t curScreen() { return static_cast<uint8_t>(sCur); }
// ---------------- 中文字模 ----------------
static int16_t hanziFind(uint16_t cp) {
  int16_t lo = 0, hi = HANZI_COUNT;
  while (lo < hi) {
    const int16_t mid = lo + (hi - lo) / 2;
    if (HANZI_CP[mid] < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < HANZI_COUNT && HANZI_CP[lo] == cp) return lo;
  return -1;
}

// 画一串 UTF-8:命中字模逐位画;ASCII 用 GLCD font 2 兜底(x 前进 8px)
void drawCn16(int16_t x, int16_t y, const char* utf8, uint16_t color) {
  drawCn16Bg(x, y, utf8, color, TFT_BLACK);
}

void drawCn16Bg(int16_t x, int16_t y, const char* utf8, uint16_t color, uint16_t bg) {
  uint32_t cp = 0;
  uint8_t need = 0;
  for (const uint8_t* p = (const uint8_t*)utf8; *p; p++) {
    uint8_t c = *p;
    if (!need) {
      if (c < 0x80) {                      // ASCII 兜底
        char b[2] = { (char)c, 0 };
        uiCanvas().setTextDatum(ML_DATUM);
        uiCanvas().setTextColor(color, bg);
        uiCanvas().drawString(b, x + 4, y + 8, 2);
        x += 9;
        continue;
      }
      need = (c < 0xE0) ? 1 : 2;
      cp = c & (need == 1 ? 0x1F : 0x0F);
    } else {
      cp = (cp << 6) | (c & 0x3F);
      need--;
    }
    if (need) continue;
    int16_t idx = hanziFind((uint16_t)cp);
    if (idx >= 0) {
      const uint8_t* g = HANZI_GLYPHS[idx];
      for (uint8_t r = 0; r < 16; r++) {
        const uint16_t bits = (uint16_t(g[2 * r]) << 8) | g[2 * r + 1];
        uint8_t col = 0;
        while (col < 16) {
          while (col < 16 && !(bits & (0x8000u >> col))) ++col;
          const uint8_t start = col;
          while (col < 16 && (bits & (0x8000u >> col))) ++col;
          if (col > start)
            uiCanvas().drawFastHLine(x + start, y + r, col - start, color);
        }
      }
    }
    x += 18;
    cp = 0;
  }
}

// 中文标签:与 font2 的 ML_DATUM 垂直居中对齐
static void labelCn(int x, int y, const char* utf8, uint16_t color) {
  drawCn16(x, y - 8, utf8, color);
}

static void label(int x, int y, const char* txt, uint16_t color) {
  uiCanvas().setTextDatum(ML_DATUM);
  uiCanvas().setTextColor(color, TFT_BLACK);
  uiCanvas().drawString(txt, x, y, 2);
}

// ---------------- MEMORIAL 屏 ----------------
static uint8_t memoTop = 0;
#define MEMO_ROWS 6

static void memorialDraw() {
  uint8_t n = saveMileCount();
  uiCanvas().fillRect(0, 26, 320, 188, TFT_BLACK);   // 清宠物区
  if (!n) {
    uiCanvas().setTextDatum(MC_DATUM);
    uiCanvas().setTextColor(TFT_SILVER, TFT_BLACK);
    uiCanvas().drawString("NO MILESTONES YET", 160, 108, 2);
    uiCanvas().setTextColor(TFT_DARKGREY, TFT_BLACK);
    uiCanvas().drawString("sync time to adopt", 160, 132, 2);
    uiCanvas().setTextDatum(ML_DATUM);
    return;
  }
  labelCn(8, 30, u8"纪念", TFT_SILVER);
  uint8_t maxTop = (n > MEMO_ROWS) ? (n - MEMO_ROWS) : 0;
  if (memoTop > maxTop) memoTop = maxTop;
  char buf[32];
  for (uint8_t r = 0; r < MEMO_ROWS; r++) {
    uint8_t idx = memoTop + r;
    if (idx >= n) break;
    const Milestone* m = saveMile(idx);
    const CalendarDate date = clockAtEpoch(m->epoch).date;
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", date.year, date.month, date.day);
    label(20, 52 + r * 26, buf, (idx == 0) ? TFT_GOLD : TFT_WHITE);
    if (m->kind == 0) {
      labelCn(100, 52 + r * 26, u8"领养", (idx == 0) ? TFT_GOLD : TFT_WHITE);
    } else {
      uint8_t k = m->kind > 3 ? 3 : m->kind;
      labelCn(100, 52 + r * 26, u8"进化", TFT_WHITE);
      drawCn16(140, 44 + r * 26, STAGES[k].name, STAGES[k].body);
    }
  }
}

// ---------------- 框架 ----------------
void screensInit() {
  sCur = SCR_TODAY;
  orchardInit();
  screenRedraw();
}

void setScreen(uint8_t scr) {
  if (scr == sCur || scr >= SCR_COUNT) return;
  sCur = (ScreenId)scr;
  screenRedraw();
}

void screenRedraw() {
  uiFrameBegin();
  if (sCur == SCR_ARCHIVE || sCur == SCR_STATUS) {
    archiveDraw(sCur == SCR_STATUS);drawPageNavigation();uiFrameEnd();return;
  }
  if (sCur == SCR_TODAY || sCur == SCR_TREE || sCur == SCR_CAPTURE || sCur == SCR_TIMER) drawPet(millis());
  if (sCur != SCR_PET && sCur != SCR_STATUS && sCur != SCR_MEMORIAL && sCur != SCR_ADOPTION) {
    orchardDraw(sCur);
    drawPageNavigation();
    uiFrameEnd();
    return;
  }
  if (sCur == SCR_PET) {
    // The compact sprite shares the page arena. Compose it first, then draw
    // chrome without clearing its pixels; publish the complete page once.
    drawPet(millis());
    drawChrome(false);
    drawBottomBar();
  } else {
    drawChrome();
  }
  if (sCur == SCR_MEMORIAL) memorialDraw();
  else if (sCur == SCR_ADOPTION) {
    labelCn(12, 44, u8"领养信息", TFT_GREEN);
    label(12, 76, gPetName, TFT_WHITE);
    if (saveAdopted()) {
      const CalendarDate date = clockAtEpoch(saveAdoptEpoch()).date;
      char buf[32]; snprintf(buf, sizeof buf, "%04d-%02d-%02d", date.year, date.month, date.day);
      labelCn(12, 108, u8"领养日期", TFT_SILVER);
      label(110, 108, buf, TFT_GOLD);
      int32_t days = clockDaysSince(saveAdoptEpoch());
      snprintf(buf, sizeof buf, "%ld", (long)days);
      labelCn(12, 140, u8"陪伴天数", TFT_SILVER);
      label(110, 140, clockIsSet() ? buf : "--", TFT_WHITE);
    } else {
      labelCn(12, 110, u8"尚未领养", TFT_SILVER);
      labelCn(12, 142, u8"连接电脑校时后自动领养", TFT_WHITE);
    }
  }
  if (sCur != SCR_PET) {
    uiCanvas().fillRect(0, 216, 320, 24, TFT_DARKGREY);
    drawCn16Bg(8, 220, u8"左键返回树木档案", TFT_WHITE, TFT_DARKGREY);
  }
  drawPageNavigation();
  uiFrameEnd();
}

void screenButton(UiButton button) {
  if (sCur == SCR_MEMORIAL && (button == UI_UP || button == UI_DOWN)) {
    memoScroll(button == UI_UP ? -1 : 1);
    return;
  }
  orchardButton(button);
}

// STATUS 每秒比较可见数据；只有变化才离屏绘制并完整提交。
void screenTick1Hz() {
  orchardTick(); // 倒计时后台运行；冰箱库存不按日期清零。
  archiveTick();
}

void memoScroll(int8_t dir) {
  uint8_t n = saveMileCount();
  if (n <= MEMO_ROWS) return;
  int v = (int)memoTop + dir;
  if (v < 0) v = 0;
  if (v > n - MEMO_ROWS) v = n - MEMO_ROWS;
  if ((uint8_t)v == memoTop) return;
  memoTop = (uint8_t)v;
  screenRedraw();
}
