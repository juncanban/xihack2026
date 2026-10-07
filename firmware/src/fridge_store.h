#ifndef FRIDGE_STORE_H
#define FRIDGE_STORE_H
#include "fridge_logic.h"

extern fridge::Data fridgeData;
void fridgeLoad();
// 失败不推进 sequence/crc 或写入槽；调用方负责回滚尚未保存的业务修改。
bool fridgeSave();

#endif
