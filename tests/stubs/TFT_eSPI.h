#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xffff
#define TFT_NAVY 0x000f
#define TFT_GREEN 0x07e0
#define TFT_RED 0xf800
#define TFT_DARKGREY 0x7bef
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define TL_DATUM 0
#define TR_DATUM 2
class TFT_eSPI {
protected:
  uint16_t fg=0, bg=0; int datum=0;
  bool physical=true;
  int surfaceWidth=320, surfaceHeight=240;
  uint16_t* pixels=nullptr;
  std::vector<uint16_t> ownedPixels;
  void pixel(int x,int y,uint16_t color) {
    if(x<0||y<0||x>=surfaceWidth||y>=surfaceHeight) return;
    if(!pixels) {
      ownedPixels.assign(static_cast<size_t>(surfaceWidth)*surfaceHeight,0);
      pixels=ownedPixels.data();
    }
    pixels[y*surfaceWidth+x]=color;
  }
  void rectPixels(int x,int y,int w,int h,uint16_t color) {
    for(int row=std::max(0,y);row<std::min(surfaceHeight,y+h);++row)
      for(int col=std::max(0,x);col<std::min(surfaceWidth,x+w);++col) pixel(col,row,color);
  }
  void changed() { if(physical) ++directDrawCount; }
  void shape(const char* name, std::initializer_list<int> args) {
    changed();
    std::ostringstream out; out << "[\"" << name << "\"";
    for (int v : args) out << ',' << v;
    out << ']'; ops.push_back(out.str());
  }
public:
  // Visible operations feed the existing JSON renderer. Lifetime counters are
  // independent: clearing a frame must never hide an earlier direct LCD write.
  std::vector<std::string> ops;
  uint32_t directDrawCount=0, spriteSubmitCount=0, spriteCreateCount=0;
  explicit TFT_eSPI(bool lcd=true) : physical(lcd) {}
  virtual ~TFT_eSPI() = default;
  virtual void fillScreen(uint16_t c) {
    ops.clear(); shape("rect", {0,0,surfaceWidth,surfaceHeight,c});
    rectPixels(0,0,surfaceWidth,surfaceHeight,c);
  }
  virtual void fillRect(int x,int y,int w,int h,uint16_t c) {
    if(x==0&&y==0&&w>=surfaceWidth&&h>=surfaceHeight) ops.clear();
    shape("rect",{x,y,w,h,c});rectPixels(x,y,w,h,c);
  }
  virtual void drawRect(int x,int y,int w,int h,uint16_t c) { shape("outline",{x,y,w,h,c}); }
  virtual void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c) { shape("round",{x,y,w,h,r,c}); }
  virtual void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c) { shape("roundOutline",{x,y,w,h,r,c}); }
  virtual void fillCircle(int x,int y,int r,uint16_t c) { shape("circle",{x,y,r,c}); }
  virtual void fillEllipse(int x,int y,int rx,int ry,uint16_t c) { shape("ellipse",{x,y,rx,ry,c}); }
  virtual void drawLine(int x,int y,int x2,int y2,uint16_t c) { shape("line",{x,y,x2,y2,c}); }
  virtual void drawFastHLine(int x,int y,int w,uint16_t c) { shape("hline",{x,y,w,c});rectPixels(x,y,w,1,c); }
  virtual void drawPixel(int x,int y,uint16_t c) { shape("pixel",{x,y,c});pixel(x,y,c); }
  virtual void pushImage(int x,int y,int w,int h,uint16_t* data) {
    ++spriteSubmitCount;
    if(!data) return;
    for(int row=0;row<h;++row)
      for(int col=0;col<w;++col) pixel(x+col,y+row,data[row*w+col]);
  }
  virtual void setTextDatum(int d) { datum=d; }
  virtual void setTextColor(uint16_t f,uint16_t b) { fg=f; bg=b; }
  virtual int drawString(const char* s,int x,int y,int font=2) {
    changed();
    std::ostringstream out;
    out << "[\"text\"," << x << ',' << y << ',' << font << ',' << datum << ',' << fg << ',' << bg << ",\"";
    for (const char* p=s; *p; ++p) { if (*p=='"' || *p=='\\') out << '\\'; out << *p; }
    out << "\"]"; ops.push_back(out.str()); return (int)strlen(s)*8;
  }
  int width() const { return surfaceWidth; }
  int height() const { return surfaceHeight; }
  uint16_t readPixel(int x,int y) const {
    return pixels&&x>=0&&y>=0&&x<surfaceWidth&&y<surfaceHeight ? pixels[y*surfaceWidth+x] : 0;
  }
  void submitSprite(const TFT_eSPI& source,int x,int y) {
    ++spriteSubmitCount;
    const bool full=x==0&&y==0&&source.surfaceWidth==surfaceWidth&&source.surfaceHeight==surfaceHeight;
    if(full) ops=source.ops;
    else {
      // Existing fixtures use only full-page snapshots. Preserve translated
      // primitive positions as well for the normal compact pet push path.
      for(const std::string& op:source.ops) {
        const size_t first=op.find(',');
        const size_t second=op.find(',',first+1),third=op.find(',',second+1);
        if(first==std::string::npos||second==std::string::npos||third==std::string::npos) continue;
        const int px=std::stoi(op.substr(first+1,second-first-1));
        const int py=std::stoi(op.substr(second+1,third-second-1));
        std::ostringstream shifted;
        shifted<<op.substr(0,first+1)<<px+x<<','<<py+y<<op.substr(third);
        ops.push_back(shifted.str());
      }
    }
    if(source.pixels)
      for(int row=0;row<source.surfaceHeight;++row)
        for(int col=0;col<source.surfaceWidth;++col) pixel(x+col,y+row,source.pixels[row*source.surfaceWidth+col]);
  }
  void snapshot(const char* path) {
    std::ofstream file(path); file << '[';
    for (size_t i=0; i<ops.size(); ++i) { if(i) file << ','; file << ops[i]; }
    file << ']';
  }
};
class TFT_eSprite : public TFT_eSPI {
  TFT_eSPI* display;
  uint8_t depth=16;
public:
  explicit TFT_eSprite(TFT_eSPI* target=nullptr) : TFT_eSPI(false),display(target) {}
  void setColorDepth(uint8_t value) { depth=value; }
  uint8_t getColorDepth() const { return depth; }
  void* createSprite(int16_t w,int16_t h,uint8_t* external=nullptr) {
    if(w<=0||h<=0||depth!=16) return nullptr;
    surfaceWidth=w;surfaceHeight=h;++spriteCreateCount;ops.clear();
    if(external) pixels=reinterpret_cast<uint16_t*>(external);
    else {ownedPixels.assign(static_cast<size_t>(w)*h+1,0);pixels=ownedPixels.data();}
    return pixels;
  }
  void deleteSprite() { pixels=nullptr;ownedPixels.clear();ops.clear(); }
  void* getPointer() const { return pixels; }
  void fillSprite(uint16_t color) { fillScreen(color); }
  void pushSprite(int32_t x,int32_t y) { if(display&&pixels) display->submitSprite(*this,x,y); }
};
