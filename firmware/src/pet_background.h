#pragma once
#include <stdint.h>
namespace petbackground {
inline uint8_t& selected() { static uint8_t value = 0; return value; }
inline void step(int direction) { selected() = (selected() + (direction < 0 ? 2 : 1)) % 3; }
}
