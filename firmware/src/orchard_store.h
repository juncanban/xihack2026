#ifndef ORCHARD_STORE_H
#define ORCHARD_STORE_H
#include "orchard_logic.h"
extern orchard::Data orchardData;
void orchardLoad();
bool orchardSave();
orchard::DayRecord* orchardDay(uint32_t date, bool create = false);
#endif
