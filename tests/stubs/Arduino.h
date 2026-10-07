#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
struct FakeSerial {
  std::string output;
  void print(const char*) {}
  void println(const char* s) { output += s; output += '\n'; }
  template<class... Args> void printf(const char*, Args...) {}
};
extern FakeSerial Serial;
