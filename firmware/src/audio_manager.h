#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>
#include "pet_data.h"

// First audio backend: short synthesized tones on the Wio Terminal buzzer.
// SD/WAV playback can be added behind the same request interface later.
enum SoundId : uint8_t {
  SOUND_IDLE = 0,
  SOUND_THINKING,
  SOUND_TOOL,
  SOUND_WAIT,
  SOUND_DONE,
  SOUND_ERROR,
  SOUND_SLEEP,
  SOUND_DONE_CELEBRATE,
  SOUND_ERROR_SHAKE,
  SOUND_TIMER_DONE,
  SOUND_COUNT
};

void audioInit();
void audioTick(uint32_t now, bool petPageVisible);
void audioStateChanged(PetState state, bool petPageVisible);
void audioRequest(SoundId sound, bool petPageVisible);

#endif
