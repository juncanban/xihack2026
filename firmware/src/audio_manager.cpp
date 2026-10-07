#include "audio_manager.h"

namespace {
constexpr uint8_t AUDIO_PIN = WIO_BUZZER;
constexpr uint16_t REST = 0;
constexpr uint16_t SPECIAL_DELAY_MS = 260;

struct Note { uint16_t hz; uint16_t ms; };

// Short game-like cues. They are deliberately small and non-blocking.
const Note THINKING[] = {{392, 120}, {REST, 80}};
const Note IDLE[] = {{REST, 220}};
const Note TOOL[] = {{659, 70}, {784, 100}};
const Note WAIT[] = {{740, 130}, {587, 170}};
const Note DONE[] = {{523, 80}, {659, 80}, {784, 140}};
const Note ERROR[] = {{392, 120}, {262, 180}};
const Note SLEEP[] = {{330, 180}, {REST, 120}};
const Note DONE_CELEBRATE[] = {{784, 80}, {988, 80}, {1175, 180}};
const Note ERROR_SHAKE[] = {{330, 90}, {262, 90}, {330, 90}};
const Note TIMER_DONE[] = {{523, 120}, {659, 120}, {784, 240}, {REST, 180},
                           {523, 120}, {659, 120}, {1047, 300}};

struct Cue { const Note* notes; uint8_t count; };
const Cue CUES[SOUND_COUNT] = {
  {IDLE, sizeof(IDLE) / sizeof(Note)},
  {THINKING, sizeof(THINKING) / sizeof(Note)},
  {TOOL, sizeof(TOOL) / sizeof(Note)},
  {WAIT, sizeof(WAIT) / sizeof(Note)},
  {DONE, sizeof(DONE) / sizeof(Note)},
  {ERROR, sizeof(ERROR) / sizeof(Note)},
  {SLEEP, sizeof(SLEEP) / sizeof(Note)},
  {DONE_CELEBRATE, sizeof(DONE_CELEBRATE) / sizeof(Note)},
  {ERROR_SHAKE, sizeof(ERROR_SHAKE) / sizeof(Note)},
  {TIMER_DONE, sizeof(TIMER_DONE) / sizeof(Note)},
};

const Note* activeNotes = nullptr;
uint8_t activeCount = 0;
uint8_t activeIndex = 0;
uint32_t noteStarted = 0;
bool active = false;
bool timerCueActive = false;
bool toneRunning = false; // 已通过 tone() 启动过蜂鸣(其首次调用才使能 TC0 时钟)
bool specialPending = false;
SoundId pendingSpecial = SOUND_THINKING;
uint32_t specialDue = 0;

// 核心 Tone.cpp 的 noTone() 会直接复位 TC0；在 tone() 从未运行(时钟未使能)时
// 首调它将卡死在 SWRST 同步等待,setup() 停在 audioInit() 且串口 READY 不再输出。
void buzzerSilence() {
  if (toneRunning) { noTone(AUDIO_PIN); toneRunning = false; }
  else digitalWrite(AUDIO_PIN, LOW);
}

void stopCue() {
  buzzerSilence();
  activeNotes = nullptr;
  activeCount = 0;
  activeIndex = 0;
  active = false;
  timerCueActive = false;
}

void startNote(uint32_t now) {
  if (!activeNotes || activeIndex >= activeCount) {
    stopCue();
    return;
  }
  const Note& note = activeNotes[activeIndex];
  noteStarted = now;
  if (note.hz == REST) buzzerSilence();
  else { tone(AUDIO_PIN, note.hz); toneRunning = true; }
}

void startCue(SoundId sound, uint32_t now) {
  if (sound >= SOUND_COUNT) return;
  stopCue();
  activeNotes = CUES[sound].notes;
  activeCount = CUES[sound].count;
  activeIndex = 0;
  active = true;
  timerCueActive = sound == SOUND_TIMER_DONE;
  startNote(now);
}

} // namespace

void audioInit() {
  pinMode(AUDIO_PIN, OUTPUT);
  stopCue(); // 未发声过时 buzzerSilence 只拉低电平，不会触发 noTone 的 TC0 复位挂起。
}

void audioTick(uint32_t now, bool petPageVisible) {
  if (!petPageVisible) {
    specialPending = false;
    if (!timerCueActive) {
      if (active) stopCue();
      return;
    }
  }
  if (specialPending && (int32_t)(now - specialDue) >= 0) {
    specialPending = false;
    startCue(pendingSpecial, now);
    return;
  }
  if (!active || !activeNotes) return;
  if (now - noteStarted < activeNotes[activeIndex].ms) return;
  ++activeIndex;
  startNote(now);
}

void audioRequest(SoundId sound, bool petPageVisible) {
  if (sound == SOUND_TIMER_DONE) {
    if (timerCueActive) return;
    specialPending = false;
    startCue(sound, millis());
    return;
  }
  if (timerCueActive) return;
  if (!petPageVisible) return;
  startCue(sound, millis());
}

void audioStateChanged(PetState state, bool petPageVisible) {
  if (!petPageVisible || state >= ST_COUNT || timerCueActive) return;
  specialPending = false;
  static const SoundId stateSounds[ST_COUNT] = {
    SOUND_IDLE, SOUND_THINKING, SOUND_TOOL, SOUND_WAIT,
    SOUND_DONE, SOUND_ERROR, SOUND_SLEEP
  };
  audioRequest(stateSounds[state], true);
  if (state == ST_DONE) {
    pendingSpecial = SOUND_DONE_CELEBRATE;
    specialPending = true;
    specialDue = millis() + SPECIAL_DELAY_MS;
  } else if (state == ST_ERROR) {
    pendingSpecial = SOUND_ERROR_SHAKE;
    specialPending = true;
    specialDue = millis() + SPECIAL_DELAY_MS;
  }
}
