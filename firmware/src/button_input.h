#pragma once
#include "pet_data.h"

struct HardwareButton { uint8_t pin; UiButton event; };
// Facing the LCD: user A=LEFT(WIO_KEY_C), B=CENTER(WIO_KEY_B).
// Keep joystick entries first: archive key-repeat uses bits 2 and 3.
constexpr HardwareButton HARDWARE_BUTTONS[] = {
  {WIO_5S_LEFT, UI_LEFT}, {WIO_5S_RIGHT, UI_RIGHT},
  {WIO_5S_UP, UI_UP}, {WIO_5S_DOWN, UI_DOWN}, {WIO_5S_PRESS, UI_OK},
  {WIO_KEY_C, UI_TIMER}, // User A: previous root / back from child.
  {WIO_KEY_B, UI_FARM}   // User B: next root.
};
constexpr uint8_t HARDWARE_BUTTON_COUNT =
  sizeof(HARDWARE_BUTTONS) / sizeof(HARDWARE_BUTTONS[0]);
