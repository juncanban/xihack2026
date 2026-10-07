#include "ui_hints.h"
namespace hints {
constexpr uint16_t MUTED=0x738F,LINE=0xDEDA;
static int labelWidth(const char* s) {
  int width=0;
  for(const uint8_t* p=reinterpret_cast<const uint8_t*>(s);*p;++p)
    if((*p&0xC0)!=0x80)width+=*p<128?9:18;
  return width;
}
static void key(int x,int y,Key k,uint16_t bg) {
  auto& cv=uiCanvas();
  if(k==UD||k==DOWN) {
    cv.drawLine(x,y-6,x,y+6,MUTED);
    cv.drawLine(x-3,y+3,x,y+6,MUTED);cv.drawLine(x,y+6,x+3,y+3,MUTED);
    if(k==UD){cv.drawLine(x-3,y-3,x,y-6,MUTED);cv.drawLine(x,y-6,x+3,y-3,MUTED);}
  } else if(k==LR||k==LEFT) {
    cv.drawLine(x-6,y,x+6,y,MUTED);
    cv.drawLine(x-3,y-3,x-6,y,MUTED);cv.drawLine(x-6,y,x-3,y+3,MUTED);
    if(k==LR){cv.drawLine(x+3,y-3,x+6,y,MUTED);cv.drawLine(x+6,y,x+3,y+3,MUTED);}
  } else if(k==OK) {
    cv.fillCircle(x,y,5,MUTED);cv.fillCircle(x,y,4,bg);cv.fillCircle(x,y,2,MUTED);
  } else if(k==STATE)cv.fillCircle(x,y,2,0x3349);
  else if(k==AB){key(x-8,y,A,bg);key(x+8,y,B,bg);}
  else if(k>=A&&k<=C){
    cv.drawRoundRect(x-7,y-7,15,15,3,MUTED);
    const char label[]={static_cast<char>('A'+k-A),0};
    cv.setTextDatum(MC_DATUM);cv.setTextColor(MUTED,bg);cv.drawString(label,x,y,2);
  }
}
void bar(Item a,Item b,Item c,Item d,int y,int x,int width,uint16_t bg) {
  auto& cv=uiCanvas();const Item items[]={a,b,c,d};
  const int height=y+26>240?240-y:26;
  cv.fillRect(x,y,width,height,bg);cv.drawLine(x,y,x+width-1,y,LINE);
  int count=0,total=0;
  for(const auto& item:items)if(item.label){++count;total+=(item.key==AB?34:20)+labelWidth(item.label);}
  if(!count)return;
  const int gap=count>1?(width-16-total)/(count-1):0;
  int pos=count>1?x+8:x+(width-total)/2;
  for(const auto& item:items)if(item.label){
    const int iw=item.key==AB?34:20;
    key(pos+(item.key==AB?16:7),y+height/2,item.key,bg);
    drawCn16Bg(pos+iw,y+(height-16)/2,item.label,MUTED,bg);
    pos+=iw+labelWidth(item.label)+gap;
  }
}
}
