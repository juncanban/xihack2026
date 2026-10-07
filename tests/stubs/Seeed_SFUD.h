#pragma once
#include "Seeed_Arduino_FS.h"
struct FakeSFUD { File open(const char* path, const char* mode) { return File(path, mode[0]=='w'); } };
extern FakeSFUD SFUD;
