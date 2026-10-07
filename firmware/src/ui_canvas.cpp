// One shared RGB565 arena: the pet's 160x186 view and the 320x240 page view
// are never used concurrently. No large allocations/frees on page changes.
#include "pet_data.h"
#include "pet_background_art.h"
#include <cstring>

namespace {
constexpr int16_t SCREEN_W = 320, SCREEN_H = 240;
// TFT_eSprite needs an extra off-screen pixel for its window operations.
alignas(4) uint16_t displayPixels[SCREEN_W * SCREEN_H + 1];
TFT_eSprite pageSprite(&tft);
bool ready = false, composing = false;
uint32_t frameStarted = 0, frames = 0, lastMicros = 0, lastPushBytes = 0;
}

bool uiCanvasInit() {
  if (ready) return true;
  fb.setColorDepth(16);
  pageSprite.setColorDepth(16);
  uint8_t* pixels = reinterpret_cast<uint8_t*>(displayPixels);
  if (!fb.createSprite(PET_W, PET_H, pixels) ||
      !pageSprite.createSprite(SCREEN_W, SCREEN_H, pixels)) return false;
  fb.setTextDatum(MC_DATUM);
  ready = true;
  return true;
}

bool uiCanvasReady() { return ready; }
TFT_eSPI& uiCanvas() {
  return composing ? static_cast<TFT_eSPI&>(pageSprite) : tft;
}
void uiFrameBegin() {
  if (!ready || composing) return;
  frameStarted = micros();
  composing = true;
}
void uiFrameEnd() {
  if (!composing) return;
  pageSprite.pushSprite(0, 0);
  lastPushBytes = SCREEN_W * SCREEN_H * sizeof(uint16_t);
  composing = false;
  ++frames;
  lastMicros = micros() - frameStarted;
}
uint32_t uiFrameCount() { return frames; }
uint32_t uiFrameLastMicros() { return lastMicros; }
uint32_t uiLastPushBytes() { return lastPushBytes; }

void PetSprite::pushSprite(int32_t x, int32_t y) {
  if (!composing) {
    const uint32_t started = micros();
    if (curScreen() == SCR_TODAY) {
      // Leave the two bottom rows for the static homepage action bar.
      const int16_t HOME_PET_H = PET_H - 2;
      tft.pushImage(x, y, PET_W, HOME_PET_H, displayPixels);
      lastPushBytes = PET_W * HOME_PET_H * sizeof(uint16_t);
    } else {
      TFT_eSprite::pushSprite(x, y);
      lastPushBytes = PET_W * PET_H * sizeof(uint16_t);
    }
    lastMicros = micros() - started;
    return;
  }
  if (curScreen() == SCR_TREE || curScreen() == SCR_CAPTURE || curScreen() == SCR_TIMER) {
    const bool settings = curScreen() == SCR_CAPTURE;
    const bool timing = curScreen() == SCR_TIMER;
    const int width = settings || timing ? 32 : 96, height = settings || timing ? 37 : 112;
    const int left = timing ? 216 : settings ? 192 : 8, top = timing ? 119 : settings ? 127 : 52;
    // Downsample in place: source indices never precede the destination.
    // Raw RGB565 bytes keep the TFT library's byte order unchanged.
    for (int row=0;row<height;++row)
      for (int col=0;col<width;++col)
        displayPixels[row*width+col]=displayPixels[(row*PET_H/height)*PET_W+col*PET_W/width];
    const int count=width*height, tail=SCREEN_W*SCREEN_H-count;
    std::memmove(displayPixels+tail,displayPixels,count*sizeof(uint16_t));
    // The temporary thumbnail sits below both destination rectangles.
    pageSprite.fillRect(0,0,SCREEN_W,tail/SCREEN_W,TFT_WHITE);
    pageSprite.fillRect(0,tail/SCREEN_W,tail%SCREEN_W,1,TFT_WHITE);
    for (int row=0;row<height;++row)
      std::memcpy(displayPixels+(top+row)*SCREEN_W+left,displayPixels+tail+row*width,width*sizeof(uint16_t));
    pageSprite.fillRect(tail%SCREEN_W,tail/SCREEN_W,SCREEN_W-tail%SCREEN_W,1,TFT_WHITE);
    pageSprite.fillRect(0,tail/SCREEN_W+1,SCREEN_W,SCREEN_H-tail/SCREEN_W-1,TFT_WHITE);
    return;
  }
  if (curScreen() == SCR_ARCHIVE) {
    // Reuse the arena tail to clip the unchanged compact pet when y is negative.
    const int tail = SCREEN_W * SCREEN_H - PET_W * PET_H;
    std::memmove(displayPixels + tail, displayPixels, PET_W * PET_H * sizeof(uint16_t));
    const int first = y < archiveview::TOP ? archiveview::TOP - y : 0;
    const int last = y + PET_H > archiveview::BOTTOM ? archiveview::BOTTOM - y : PET_H;
    if (x < 0 || x + PET_W > SCREEN_W || first >= last) {pageSprite.fillRect(0,0,SCREEN_W,SCREEN_H,TFT_WHITE);return;}
    for (int row=first;row<last;++row)
      std::memmove(displayPixels+(y+row)*SCREEN_W+x,displayPixels+tail+row*PET_W,PET_W*sizeof(uint16_t));
    const int top=y+first, height=last-first;
    pageSprite.fillRect(0,0,SCREEN_W,top,TFT_WHITE);
    pageSprite.fillRect(0,top,x,height,TFT_WHITE);
    pageSprite.fillRect(x+PET_W,top,SCREEN_W-x-PET_W,height,TFT_WHITE);
    pageSprite.fillRect(0,top+height,SCREEN_W,SCREEN_H-top-height,TFT_WHITE);
    return;
  }
  // drawPet has just filled the compact pet view at the start of the arena.
  // Move bottom-up: every destination lies after its source, so unread rows
  // cannot be overwritten. Pixels are already in the library's byte order.
  if (x < 0 || y < 0 || x + PET_W > SCREEN_W || y + PET_H > SCREEN_H) return;
  for (int row = PET_H - 1; row >= 0; --row) {
    std::memmove(displayPixels + (y + row) * SCREEN_W + x,
                 displayPixels + row * PET_W, PET_W * sizeof(uint16_t));
  }
  const uint16_t bg = curScreen() == SCR_TODAY ? TFT_WHITE : TFT_BLACK;
  pageSprite.fillRect(0, 0, SCREEN_W, y, bg);
  pageSprite.fillRect(0, y, x, PET_H, bg);
  pageSprite.fillRect(x + PET_W, y, SCREEN_W - x - PET_W, PET_H, bg);
  pageSprite.fillRect(0, y + PET_H, SCREEN_W, SCREEN_H - y - PET_H, bg);
  if (curScreen() == SCR_PET) {
    petbackground::draw(pageSprite,0,26,PET_X,188,0,26);
    petbackground::draw(pageSprite,PET_X+PET_W,26,320-PET_X-PET_W,188,PET_X+PET_W,26);
    petbackground::draw(pageSprite,PET_X,PET_Y+PET_H,PET_W,214-PET_Y-PET_H,PET_X,PET_Y+PET_H);
    pageSprite.setTextDatum(TL_DATUM);
    const uint16_t textColor = petbackground::selected()==2 ? TFT_WHITE : 0x42A9;
    pageSprite.setTextColor(textColor,textColor);
    pageSprite.drawString(gPetName,12,37,1);
    pageSprite.setTextDatum(TR_DATUM);
    pageSprite.drawString(petbackground::selected()==0 ? "1/3" : petbackground::selected()==1 ? "2/3" : "3/3",308,37,1);
  }
}
