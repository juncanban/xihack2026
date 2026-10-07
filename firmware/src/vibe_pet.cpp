#include "ui_hints.h"
#include "button_input.h"
#include "pet_background_art.h"
// vibe_pet —— 小苹果桌面宠物(Wio Terminal 版)
//
// 参考 Seeed vibe-pet 的思路:宿主机上的 AI 编码代理(Claude Code 等)
// 通过串口发送状态行,板子把状态画成小苹果的动画。
//
// 串口协议(115200,一行一条,'\n' 结尾):
//   S:IDLE|THINKING|TOOL|WAIT|DONE|ERROR|SLEEP   切换宠物状态
//   T:<n>                                        显示 Token 计数(可选)
//   D:<epoch秒>                                  宿主机时间同步(幂等,回 OK:TIME)
//   N:<名字>                                     改名(可打印ASCII≤12,回 OK:NAME:xx)
//   I                                            查状态,回 I:<名>|<epoch>|<日期>|<累计>|<今日>|<阶段>|<raw>
//   P                                            心跳,喂狗即可
//
// 无宿主机时可用五向键左右切换状态做演示。
//
// 本文件:setup/loop、代理状态机、小苹果动画、串口分发、五向键。
// 其余模块见 pet_data.h 顶注。

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <malloc.h>
#include "pet_data.h"
#include "time_snap.h"
#include "hanzi16.h"   // TERM_CP 节气名(顶栏显示)
#include "audio_manager.h"
extern "C" void* _sbrk(size_t increment); // SAMD core heap break; zero is read-only.

// ---------------- 全局画布与共享状态(正身) ----------------
TFT_eSPI   tft = TFT_eSPI();
PetSprite fb(&tft);   // 苹果区域离屏精灵,避免闪烁

const char* STATE_NAMES[ST_COUNT] = {
  "IDLE", "THINKING", "TOOL", "WAIT", "DONE", "ERROR", "SLEEP"
};
const char* STATE_TAGS[ST_COUNT] = {
  "standing by", "thinking ...", "running tools", "waiting approval !",
  "task done :)", "oops, error !", "zzz ..."
};
const uint16_t STATE_COLORS[ST_COUNT] = {
  TFT_SILVER, TFT_YELLOW, TFT_CYAN, TFT_ORANGE, TFT_GREEN, TFT_RED, TFT_SKYBLUE
};

PetState gState = ST_IDLE;
uint32_t gStateSince = 0;
uint32_t gLastEvent = 0;      // 最近一次收到宿主机事件的时刻
uint32_t gPhysicalKeyCount = 0;
uint32_t gTokens = 0;
char gPetName[13] = DEFAULT_PET_NAME;

// 成长阶段:苹果本体进化(青→红→亮→金);RGB565 上机可微调
const uint32_t STAGE_NEED[3] = {300, 1400, 6000};   // ≈3天/14天/60天@满配额
const StageSkin STAGES[4] = {
  { 0x6DA8,  0x23C4, 0xCE79, 0,                   u8"青苹果" },
  { TFT_RED, 0x8800, 0xFD20, 0,                   u8"红苹果" },
  { 0xFC9F,  0xB800, 0xFFFF, DEC_SPARK,           u8"亮苹果" },
  { 0xFEA0,  0x9D20, 0xFFE0, DEC_HALO | DEC_SPARK, u8"金苹果" },
};

// 进化庆祝(once-anim,自动回落;TamaPetchi 模式)
static uint32_t gCelebrateUntil = 0;
static bool gCelebrationPending = false;

void checkEvolution() {
  uint8_t st = saveStage();
  if (st >= 3) return;
  if (sunTotalQ8() >= STAGE_NEED[st] * 256UL) {
    saveOnEvolve(st + 1, nowEpoch());     // 立即落盘,写卡顿被庆祝动画掩盖
    if (curScreen() == SCR_PET) gCelebrateUntil = millis() + 2500;
    else gCelebrationPending = true;    // 返回宠物页再庆祝，不打断农事/提醒
  }
}

// 宿主机静默多久自动降级
const uint32_t IDLE_AFTER_MS  = 120UL * 1000;  // 2min 无事件 -> IDLE
const uint32_t SLEEP_AFTER_MS = 600UL * 1000;  // 10min 无事件 -> SLEEP

// ---------------- 串口行解析 ----------------
char rxBuf[64];
uint8_t rxLen = 0;

void drawChrome(bool clearBody); // 改名后重画顶栏(定义在下方)
void setState(PetState s);

void handleLine(const char* line) {
  if (!strcmp(line, "U")) {
    orchardDumpInfo();
  } else if (!strncmp(line, "K:", 2)) {
    static const char* const keys[] = {"LEFT","RIGHT","UP","DOWN","OK","BACK","FARM","TIMER"};
    for (uint8_t i = 0; i < 8; ++i) if (!strcmp(line + 2, keys[i])) {
      dispatchUiButton((UiButton)i);
      Serial.print("OK:KEY:"); Serial.println(keys[i]);
      orchardDumpInfo();
      return;
    }
    Serial.println("ERR:KEY");
  } else if (!strcmp(line, "V:STATS")) {
    char buffer[128];
    uintptr_t heapEnd = reinterpret_cast<uintptr_t>(_sbrk(0));
    uintptr_t stackNow = __get_MSP();
    struct mallinfo memory = mallinfo();
    snprintf(buffer, sizeof buffer,
      "V:{\"ready\":%u,\"frames\":%lu,\"frameUs\":%lu,\"pushBytes\":%lu,\"heapGap\":%lu,\"heapUsed\":%lu}",
      uiCanvasReady() ? 1 : 0, (unsigned long)uiFrameCount(), (unsigned long)uiFrameLastMicros(),
      (unsigned long)uiLastPushBytes(),
      (unsigned long)(stackNow > heapEnd ? stackNow - heapEnd : 0), (unsigned long)memory.uordblks);
    Serial.println(buffer);
  } else if (!strcmp(line, "V:SCREEN")) {
    // 直接回读 LCD 显存；逐行传输只需 960B 栈，不新建整屏帧缓冲。
    uint8_t row[320 * 3];
    Serial.println("V:RGB888:320:240");
    for (int y = 0; y < 240 && Serial; ++y) {
      tft.readRectRGB(0, y, 320, 1, row);
      Serial.write(row, sizeof row);
    }
    Serial.println("V:END");
  } else if (!strcmp(line, "R:REBOOT")) {
    Serial.println("OK:REBOOT"); Serial.flush(); delay(20);
    NVIC_SystemReset();
  } else if (line[0] == 'S' && line[1] == ':') {
    const char* name = line + 2;
    for (int i = 0; i < ST_COUNT; i++) {
      if (strcasecmp(name, STATE_NAMES[i]) == 0) {
        setState((PetState)i); // 串口状态和演示状态共用底栏刷新
        gLastEvent = millis();
        Serial.print("OK:"); Serial.println(STATE_NAMES[i]);
        return;
      }
    }
    Serial.println("ERR:STATE");
  } else if (line[0] == 'T' && line[1] == ':') {
    gTokens = strtoul(line + 2, nullptr, 10);
    gLastEvent = millis();
  } else if (line[0] == 'D' && line[1] == ':') {
    bool first = false;
    if (clockSet(strtoul(line + 2, nullptr, 10), &first)) {
      if (first && !saveAdopted()) saveOnAdopt(nowEpoch());  // 首次同步 = 领养
      timeSnapWriteNow();             // 校时后立即留新鲜快照
    }
    Serial.println("OK:TIME");      // 幂等未变也回,宿主机好确认链路
  } else if (line[0] == 'N' && line[1] == ':') {
    // 改名:只收可打印 ASCII,去首尾空格,≤12 字节(中文名走编译期字模,见 PLAN.md)
    const char* src = line + 2;
    while (*src == ' ') src++;
    char out[13]; uint8_t n = 0;
    for (; *src && n < 12; src++) {
      char c = *src;
      if (c >= 0x21 && c <= 0x7E) out[n++] = c;
    }
    out[n] = '\0';
    if (n) {
      strcpy(gPetName, out);
      saveRename(out);        // 立即落盘
      screenRedraw();         // 重绘当前页，避免串口改名清空新页面
      Serial.print("OK:NAME:"); Serial.println(gPetName);
    } else {
      Serial.println("ERR:NAME");
    }
  } else if (line[0] == 'I' && line[1] == '\0') {
    // 状态回包:<名>|<epoch>|<日期>|<累计点>|<今日点>|<阶段>|<光照raw>|<今日Q8>|<FS诊断>
    const ClockSnapshot now = clockNow();
    char buf[112];
    snprintf(buf, sizeof(buf), "I:%s|%lu|%04u-%02u-%02u|%lu|%u|%u|%u|%u|FS%d,AD%lu,M%u",
             gPetName, (unsigned long)now.utcEpoch,
             (unsigned)now.date.year, (unsigned)now.date.month, (unsigned)now.date.day,
             (unsigned long)(sunTotalQ8() >> 8), sunTodayQ8() >> 8, saveStage(), sunRaw(),
             sunTodayQ8(), saveFsOk() ? 1 : 0,
             (unsigned long)saveAdoptEpoch(), saveMileCount());
    Serial.println(buf);
  } else if (line[0] == 'L' && line[1] == ':') {
    // 光照标定注入:L:<raw> 强制;L: 恢复实读
    if (line[2] == '\0') { sunForceRaw(-1); Serial.println("OK:LIVE"); }
    else { sunForceRaw((int16_t)strtoul(line + 2, nullptr, 10)); Serial.println("OK:FORCE"); }
  } else if (line[0] == 'Q' && line[1] == '\0') {
    saveDumpInfo();
  } else if (line[0] == 'X' && line[1] == '\0') {
    saveFactoryReset();               // 清档案回青苹果;改名/领养重新来
    timeSnapWipe();                   // 时间快照同清,下次校时重新积累
    screenRedraw();
    Serial.println("OK:RESET");
  } else if (line[0] == 'E' && line[1] == '\0') {
    // 调试/演示:强制进化到下一阶段(正式触发走 checkEvolution)
    uint8_t st = saveStage();
    if (st < 3) {
      saveOnEvolve(st + 1, nowEpoch());
      if (curScreen() == SCR_PET) gCelebrateUntil = millis() + 2500;
      else gCelebrationPending = true;
      Serial.print("OK:EVOLVE:"); Serial.println(STAGES[st + 1].name);
    } else {
      Serial.println("ERR:MAX");
    }
  } else if (line[0] == 'P') {
    gLastEvent = millis();
    Serial.println("OK:PONG");
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      rxBuf[rxLen < sizeof(rxBuf) ? rxLen : sizeof(rxBuf) - 1] = '\0';
      if (rxLen) handleLine(rxBuf);
      rxLen = 0;
    } else if (c != '\r' && rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    }
  }
}

// ---------------- 静态界面(只画一次) ----------------
// 苹果精灵区域(正身)
const int16_t PET_X = 80, PET_Y = 26, PET_W = 160, PET_H = 186;

void drawChrome(bool clearBody) {
  if (clearBody) uiCanvas().fillRect(0, 0, 320, 240, TFT_BLACK);

  drawPageNavigation();
}

void drawBottomBar() {
  if(!gTokens) {
    hints::bar({hints::LR,u8"切换"},{hints::UD,u8"互动"},{hints::OK,u8"返回"});
    return;
  }
  uiCanvas().fillRect(0, 216, 320, 24, TFT_DARKGREY);
  uiCanvas().setTextDatum(ML_DATUM);
  uiCanvas().setTextColor(STATE_COLORS[gState], TFT_DARKGREY);
  uiCanvas().drawString(STATE_NAMES[gState], 6, 228, 2);
  uiCanvas().setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  if (!gTokens) drawCn16Bg(76, 220, u8"上下互动 左键返回", TFT_LIGHTGREY, TFT_DARKGREY);
  else uiCanvas().drawString(STATE_TAGS[gState], 76, 228, 2);
  if (gTokens) {
    uiCanvas().setTextDatum(MR_DATUM);
    uiCanvas().setTextColor(TFT_GOLD, TFT_DARKGREY);
    char tok[16];
    snprintf(tok, sizeof(tok), "T:%lu", (unsigned long)gTokens);
    uiCanvas().drawString(tok, 314, 228, 2);
  }
}

void setState(PetState s) {
  if (s == gState) return;
  gState = s;
  gStateSince = millis();
  const bool petPageVisible = curScreen() == SCR_PET;
  if (petPageVisible) {
    drawBottomBar();
    audioStateChanged(s, true);
  }
}

// ---------------- 小苹果绘制 ----------------
// 在精灵坐标系 (160x186) 里画苹果,bodyCx/bodyCy 为身体中心

// 苹果身体:三个圆并集,先画深色大圆再画浅色圆形成描边;颜色来自当前阶段皮肤
void drawBody(int cx, int cy, int r, uint32_t t) {
  const StageSkin& sk = STAGES[saveStage()];

  // 金苹果光环(画在果身后面的部分被果身盖住,露出外圈)
  if (sk.decor & DEC_HALO) {
    fb.drawCircle(cx, cy - 2, r + 8, 0xFEA0);
    fb.drawCircle(cx, cy - 2, r + 9, 0x7B40);
  }

  // 深色底层(半径 +3 形成轮廓)
  fb.fillCircle(cx - 14, cy + 6, r + 3, sk.dk);
  fb.fillCircle(cx + 14, cy + 6, r + 3, sk.dk);
  fb.fillCircle(cx, cy - 8, r + 3, sk.dk);
  // 浅色主体
  fb.fillCircle(cx - 14, cy + 6, r, sk.body);
  fb.fillCircle(cx + 14, cy + 6, r, sk.body);
  fb.fillCircle(cx, cy - 8, r, sk.body);
  // 顶部凹槽(果蒂处)
  fb.fillEllipse(cx, cy - r - 4, 12, 8, sk.dk);

  // 高光
  fb.fillCircle(cx - r / 2 - 6, cy - r / 3, 6, sk.hi);
  fb.fillCircle(cx - r / 2 - 1, cy - r / 3 - 5, 3, sk.hi);

  // 亮/金苹果星点
  if (sk.decor & DEC_SPARK) {
    for (int i = 0; i < 3; i++)
      fb.drawPixel(cx - r + random(0, 2 * r), cy - r / 2 - random(0, r / 2),
                   i ? TFT_WHITE : sk.hi);
  }

  // 果柄
  fb.fillRect(cx - 3, cy - r - 12, 6, 14, C_STEM);

  // 叶子(随时间轻微摆动)
  float sway = sinf(t / 600.0f) * 0.25f;
  int lx = cx + 10 + (int)(sinf(sway) * 6);
  fb.fillEllipse(lx + 4, cy - r - 14, 12, 6, C_LEAF);
  fb.drawLine(cx + 2, cy - r - 10, lx + 4, cy - r - 14, TFT_DARKGREEN);
}

// 眼睛:mode 0=睁眼(可带视线) 1=眨眼/闭眼 2=开心(^ ^) 3=X X
void drawEyes(int cx, int cy, int mode, int gazeX, int gazeY) {
  const int ex = 18, eyR = 8;
  int l = cx - ex, r = cx + ex;
  if (mode == 0) {
    fb.fillCircle(l, cy, eyR, TFT_WHITE);
    fb.fillCircle(r, cy, eyR, TFT_WHITE);
    fb.fillCircle(l + gazeX, cy + gazeY, 4, TFT_BLACK);
    fb.fillCircle(r + gazeX, cy + gazeY, 4, TFT_BLACK);
  } else if (mode == 1) {
    fb.fillRect(l - eyR, cy - 1, eyR * 2, 3, C_BODY_DK);
    fb.fillRect(r - eyR, cy - 1, eyR * 2, 3, C_BODY_DK);
  } else if (mode == 2) {  // ^ ^
    for (int e = -1; e <= 1; e += 2) {
      int x = cx + e * ex;
      for (int i = -6; i <= 6; i++) {
        int y = cy + (abs(i) - 5);   // V 形,视觉上近似 ^
        fb.drawPixel(x + i, y, TFT_WHITE);
        fb.drawPixel(x + i, y - 1, TFT_WHITE);
      }
    }
  } else {  // X X
    for (int e = -1; e <= 1; e += 2) {
      int x = cx + e * ex;
      for (int i = -5; i <= 5; i++) {
        fb.drawPixel(x + i, cy + i / 2, TFT_WHITE);
        fb.drawPixel(x + i, cy - i / 2, TFT_WHITE);
      }
    }
  }
}

void drawMouth(int cx, int cy, int mode, uint16_t bodyCol, uint16_t dkCol) {
  if (mode == 0) {  // 微笑:圆的上半被果色盖掉
    fb.fillCircle(cx, cy, 7, TFT_MAROON);
    fb.fillRect(cx - 8, cy - 7, 16, 7, bodyCol);
  } else if (mode == 1) {  // 张嘴(惊讶/干活)
    fb.fillCircle(cx, cy, 6, TFT_MAROON);
    fb.fillCircle(cx, cy + 3, 4, TFT_PINK);
  } else if (mode == 2) {  // 平嘴
    fb.fillRect(cx - 6, cy, 12, 3, dkCol);
  }
}

void drawBlush(int cx, int cy) {
  fb.fillCircle(cx - 32, cy, 5, 0xFB18);
  fb.fillCircle(cx + 32, cy, 5, 0xFB18);
}

// 小手:返回位置便于各状态摆姿势
void drawHand(int x, int y, uint16_t bodyCol, uint16_t dkCol) {
  fb.fillCircle(x, y, 7, bodyCol);
  fb.drawCircle(x, y, 7, dkCol);
}

// ---- 主屏渐变光圈:黑底上"半透明"= 径向亮度衰减(预混色阶,无需 alpha) ----
// 光从宠物区右上角洒进来,越亮范围越大;外->内同心圆各像素只写一次,成本可忽略
static uint16_t scaleColor(uint16_t c, uint8_t num, uint8_t den) {
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return ((uint16_t)(r * num / den) << 11) | ((uint16_t)(g * num / den) << 5)
       | (uint16_t)(b * num / den);
}

static void drawSunGlow(uint32_t t, uint16_t lvl) {
  if (lvl < 8) return;                        // 黑暗无光圈
  // 各分区光色:偏暗冷蓝 / 舒适暖金 / 强光黄白 / 暴晒白热
  static const uint16_t GLOW_C[NZONES] = {
    0x0000, 0x3BFF, 0xFEA0, 0xFFE0, 0xFFFF
  };
  uint16_t base = GLOW_C[sunZone()];
  int16_t maxR = (int16_t)((int32_t)180 * lvl / 255) + 20;   // ~20..200
  maxR += (int16_t)(sinf(t / 450.0f) * 3);                   // 轻微呼吸微光
  const uint8_t STEPS = 6;
  for (uint8_t i = 0; i < STEPS; i++) {       // 外圈最暗 -> 锚点最亮
    int16_t rr = (int16_t)((int32_t)maxR * (i + 1) / STEPS);
    fb.fillCircle(PET_W, 0, rr, scaleColor(base, (i + 1) * 3, STEPS * 4));
  }
}

// 左下角徽标:分区标签 + 光照百分比(小图标+文字,M7 换中文字模)
static void drawSunBadge(uint16_t lvl) {
  const ZoneSpec& z = ZONES[sunZone()];
  char buf[20];
  snprintf(buf, sizeof(buf), "%s %u%%", z.label, (unsigned)lvl * 100 / 255);
  fb.setTextDatum(ML_DATUM);
  fb.fillRoundRect(2,158,78,22,4,0x2949);
  fb.setTextColor(TFT_WHITE);
  fb.drawString(buf, 4, 168, 2);
  fb.setTextDatum(MC_DATUM);                  // 还原精灵默认
}

void drawPet(uint32_t t) {
  const bool archive = curScreen() == SCR_ARCHIVE;
  const bool treePreview = curScreen() == SCR_TREE || curScreen() == SCR_CAPTURE || curScreen() == SCR_TIMER;
  const bool home = curScreen() == SCR_TODAY || archive || treePreview;
  const uint16_t petBg = curScreen() == SCR_CAPTURE ? 0xF79D : home ? TFT_WHITE : C_BG;
  fb.fillSprite(petBg);
  uint16_t lvl = sunDispLevel();              // 光照显示电平(EMA 平滑)
  if (curScreen() == SCR_PET) petbackground::draw(fb, PET_X, PET_Y, PET_W, PET_H, 0, 0);
  else if (!home) drawSunGlow(t, lvl);
  bool cel = (int32_t)(millis() - gCelebrateUntil) < 0;   // 进化庆祝中

  int cx = PET_W / 2, cy = 118, r = 46;
  int ox = 0, oy = 0;                 // 身体偏移
  int eyeMode = 0, mouthMode = 0, gazeX = 0, gazeY = 0;
  int handLx = cx - r - 6, handLy = cy + 26;   // 默认垂手
  int handRx = cx + r + 6, handRy = cy + 26;

  switch (gState) {
    case ST_IDLE: {
      oy = (int)(sinf(t / 900.0f) * 2);
      if ((t / 3800) % 2 == 1 && (t % 3800) < 160) eyeMode = 1;   // 偶尔眨眼
      mouthMode = 0;
      break;
    }
    case ST_THINKING: {
      gazeX = 0; gazeY = -3;            // 眼睛向上看
      oy = -1;
      // 右手摸头
      handRx = cx + r - 2; handRy = cy - r - 2;
      // 头顶 "..." 气泡
      int ph = (t / 500) % 3;
      fb.setTextColor(TFT_YELLOW, home ? petBg : TFT_YELLOW);
      fb.setTextDatum(MC_DATUM);
      for (int i = 0; i <= ph; i++) fb.drawString(".", cx + 40 + i * 10, cy - r - 22, 4);
      mouthMode = 2;
      break;
    }
    case ST_TOOL: {
      // 快速小碎步抖动 + 手在身前敲打
      oy = (t / 120) % 2 ? -2 : 0;
      handLx = cx - 22; handRx = cx + 22;
      handLy = handRy = cy + r - 6 + ((t / 120) % 2 ? 3 : -1);
      // 敲击火花
      for (int i = 0; i < 3; i++)
        fb.drawPixel(cx - 30 + random(-6, 14), cy + r - 12 + random(-6, 2),
                     random(2) ? TFT_YELLOW : TFT_WHITE);
      eyeMode = 0; gazeX = 0; gazeY = 2; mouthMode = 1;
      break;
    }
    case ST_WAIT: {
      // 上下蹦 + 大问号
      oy = -(int)(fabsf(sinf(t / 220.0f)) * 8);
      handLx = cx - r + 6; handLy = cy - 6;   // 摊手
      handRx = cx + r - 6; handRy = cy - 6;
      fb.setTextColor(TFT_ORANGE, home ? petBg : TFT_ORANGE);
      fb.setTextDatum(MC_DATUM);
      fb.drawString("?", cx, cy - r - 34, 6);
      eyeMode = 0; gazeY = 2; mouthMode = 1;
      break;
    }
    case ST_DONE: {
      // 跳跃 + 五彩纸屑 + 笑眼
      oy = -(int)(fabsf(sinf(t / 260.0f)) * 14);
      eyeMode = 2; mouthMode = 0;
      for (int i = 0; i < 10; i++) {
        uint16_t cols[4] = {TFT_RED, TFT_YELLOW, TFT_GREEN, TFT_CYAN};
        int px = random(PET_W), py = random(PET_H / 2);
        if (py < cy - r) fb.fillRect(px, py, 2, 2, cols[i % 4]);
      }
      handLx = cx - r - 10; handLy = cy - 4;  // 举手
      handRx = cx + r + 10; handRy = cy - 4;
      break;
    }
    case ST_ERROR: {
      // 左右晃 + X 眼
      ox = random(-3, 4);
      eyeMode = 3; mouthMode = 1;
      fb.setTextColor(TFT_RED, home ? petBg : TFT_RED);
      fb.setTextDatum(MC_DATUM);
      fb.drawString("! !", cx, cy - r - 34, 4);
      break;
    }
    case ST_SLEEP: {
      oy = (int)(sinf(t / 1500.0f) * 2);
      eyeMode = 1; mouthMode = 2;
      // 漂浮的 Zzz
      fb.setTextColor(TFT_SKYBLUE, home ? petBg : TFT_SKYBLUE);
      fb.setTextDatum(MC_DATUM);
      int zp = (t / 1200) % 3;
      fb.drawString("z", cx + 44, cy - r - 8 - zp * 12, 4);
      fb.drawString("Z", cx + 58, cy - r - 26 - zp * 10, 4);
      break;
    }
    default: break;
  }

  if (cel) {   // 庆祝覆盖:笑眼 + 纸屑 + LEVEL UP,状态动画照常
    eyeMode = 2; mouthMode = 0;
    for (int i = 0; i < 10; i++) {
      uint16_t cols[4] = {TFT_RED, TFT_YELLOW, TFT_GREEN, TFT_CYAN};
      int px = random(PET_W), py = random(PET_H / 2);
      if (py < cy - r) fb.fillRect(px, py, 2, 2, cols[i % 4]);
    }
    fb.setTextColor(TFT_YELLOW, home ? petBg : TFT_YELLOW);
    fb.setTextDatum(MC_DATUM);
    fb.drawString("LEVEL UP!", cx, cy - r - 40, 4);
  }

  const StageSkin& sk = STAGES[saveStage()];
  drawBody(cx + ox, cy + oy, r, t);
  drawBlush(cx + ox, cy + oy + 14);
  drawEyes(cx + ox, cy + oy - 6, eyeMode, gazeX, gazeY);
  drawMouth(cx + ox, cy + oy + 14, mouthMode, sk.body, sk.dk);
  drawHand(handLx + ox, handLy + oy, sk.body, sk.dk);
  drawHand(handRx + ox, handRy + oy, sk.body, sk.dk);
  if (!home) drawSunBadge(lvl);

  fb.pushSprite(home ? 160 : PET_X, archive ? archivePetY() : home ? 30 : PET_Y);
}

// ---------------- 五向键 ----------------
// 面向屏幕左 A / 中 B / 右 C；板级字母方向相反，映射见 button_input.h。
void dispatchUiButton(UiButton button) {
  gLastEvent = millis();
  if (curScreen() == SCR_PET && (button == UI_UP || button == UI_DOWN)) {
    setState(button == UI_UP ? (PetState)((gState + 1) % ST_COUNT)
                            : (PetState)((gState + ST_COUNT - 1) % ST_COUNT));
  } else screenButton(button);
}

void pollButtons() {
  static uint8_t lastRaw = 0, stable = 0;
  static uint32_t changedAt = 0;
  uint8_t raw = 0;
  for (uint8_t i = 0; i < HARDWARE_BUTTON_COUNT; ++i)
    if (digitalRead(HARDWARE_BUTTONS[i].pin) == LOW) raw |= (1u << i);
  uint32_t now = millis();
  if (raw != lastRaw) { lastRaw = raw; changedAt = now; }
  if (now - changedAt < 25) return;
  uint8_t pressed = raw & ~stable;
  stable = raw;
  for (uint8_t i = 0; i < HARDWARE_BUTTON_COUNT; ++i) {
    if (!(pressed & (1u << i))) continue;
    ++gPhysicalKeyCount;
    dispatchUiButton(HARDWARE_BUTTONS[i].event);
    break;
  }
  const int repeat = archiveHeldDirection(stable, now);
  if (repeat) dispatchUiButton(repeat < 0 ? UI_UP : UI_DOWN);
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(WIO_5S_LEFT, INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
  pinMode(WIO_5S_UP, INPUT_PULLUP);
  pinMode(WIO_5S_DOWN, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);

  tft.begin();
  tft.setRotation(3);
  if (!uiCanvasInit()) {
    Serial.println("ERR:UI_BUFFER");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Display buffer unavailable", 8, 100, 2);
    while (true) delay(1000);
  }

  // 模块初始化(M1 骨架,均为空实现)
  clockInit();
  sunInit();
  saveLoad();
  timeSnapInit();               // flash 尾扇区快照恢复(RTC 层在 clockInit 内已先行)
  switch (clockRestoreSource()) {
    case CLOCK_SRC_RTC:  Serial.println("CLOCK:RESTORED:RTC"); break;
    case CLOCK_SRC_SNAP: Serial.println("CLOCK:RESTORED:SNAP"); break;
    default: break;
  }
  screensInit();
  audioInit();

  gStateSince = gLastEvent = millis();
  Serial.println("READY:vibe_pet");
}

void loop() {
  pollSerial();
  pollButtons();
  // 串口/按键可能耗时并更新 gLastEvent；使用处理后的时间避免减法下溢误睡眠。
  uint32_t now = millis();
  audioTick(now, curScreen() == SCR_PET);
  sunTick();                    // 1Hz 内部自节流
  saveFlushIfDue(now);          // 内部判脏与节流
  timeSnapTick(now);            // 每 10 秒把时间快照追加到 flash 尾扇区
  checkEvolution();             // 达标进化(内部比较,通常零成本)
  if ((curScreen() == SCR_PET || curScreen() == SCR_TODAY) && gCelebrationPending) {
    gCelebrationPending = false;
    gCelebrateUntil = millis() + 2500;
  }

  // 宿主机静默 watchdog
  if (now - gLastEvent > SLEEP_AFTER_MS)      setState(ST_SLEEP);
  else if (now - gLastEvent > IDLE_AFTER_MS)  setState(ST_IDLE);

  if (curScreen() == SCR_PET) drawPet(now);   // 其他屏静态 + 1Hz 动态行
  else if (curScreen() == SCR_TODAY) {
    drawPet(now);                             // 只提交首页右侧宠物窗口
    homeTickDraw();                           // 左侧/底栏按失效条件局部重画
  } else if (archivePetVisible()) screenRedraw();
  screenTick1Hz();              // 宠物页也运行后台计时与提醒
  delay(30);   // ~30fps
}
