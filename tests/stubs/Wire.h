#pragma once
#include <stdint.h>
#include <vector>
class TwoWire {
public:
  bool connected=true, shortRead=false, failWrite=false;
  uint8_t registers[256]={}, addr=0, reg=0;
  std::vector<uint8_t> tx,rx;
  size_t index=0;
  TwoWire(){registers[0x0f]=0x33;registers[0x27]=8;setAxes(0,0,16000);}
  void setAxes(int16_t x,int16_t y,int16_t z){const int16_t v[]={x,y,z};for(int i=0;i<3;++i){registers[0x28+2*i]=uint16_t(v[i])&255;registers[0x29+2*i]=uint16_t(v[i])>>8;}}
  void begin(){}
  void beginTransmission(uint8_t address){addr=address;tx.clear();}
  void write(uint8_t value){tx.push_back(value);}
  uint8_t endTransmission(){
    if(!connected||addr!=0x19||failWrite)return 2;
    if(tx.empty())return 0;
    reg=tx[0]&0x7f;
    if(tx.size()>1)registers[reg]=tx[1];
    return 0;
  }
  uint8_t requestFrom(uint8_t address,uint8_t count){
    rx.clear();index=0;if(!connected||address!=0x19)return 0;
    const uint8_t n=shortRead?count-1:count;
    for(uint8_t i=0;i<n;++i)rx.push_back(registers[reg+i]);
    return n;
  }
  int available(){return int(rx.size()-index);}
  int read(){return index<rx.size()?rx[index++]:-1;}
};
extern TwoWire Wire1;
