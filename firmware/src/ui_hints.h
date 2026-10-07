#pragma once
#include "pet_data.h"
namespace hints {
enum Key : uint8_t { NONE, UD, LR, DOWN, LEFT, OK, A, B, C, AB, STATE };
struct Item { Key key; const char* label; Item(Key k=NONE,const char* s=nullptr):key(k),label(s){} };
void bar(Item a,Item b={},Item c={},Item d={},int y=214,int x=0,int width=320,uint16_t bg=0xF79D);
}
