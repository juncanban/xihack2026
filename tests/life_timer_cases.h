static void testLifeTimer() {
  assert(uiCanvasInit());clockInit();screensInit();
  enter(SCR_TIMER);assert(curScreen()==SCR_TIMER&&timerPreset==1);
  const auto files=fakeFiles;const auto startFrames=uiFrameCount();
  tft.snapshot("artifacts/life-timer/ready_native.json");
  // Filled circle at the selected preset; hollow centers for the other three.
  auto drawn=[](int x,int r,uint16_t color) {
    const std::string op="[\"circle\","+std::to_string(x)+",174,"+std::to_string(r)+","+std::to_string(color)+"]";
    return std::find(tft.ops.begin(),tft.ops.end(),op)!=tft.ops.end();
  };
  assert(drawn(96,5,TFT_WHITE)&&drawn(19,4,PALE));
  press(UI_RIGHT);assert(timerPreset==2&&timer.durationMs==25UL*60000); // 25 min
  assert(drawn(173,5,TFT_WHITE)&&drawn(96,4,PALE));
  press(UI_RIGHT);assert(timerPreset==3&&timer.durationMs==customMinutes*60000UL);
  press(UI_UP);assert(timerPreset==3&&customMinutes==16&&timer.durationMs==16UL*60000);
  press(UI_RIGHT);assert(timerPreset==0&&timer.durationMs==3UL*60000);
  press(UI_OK);assert(timer.running);tft.snapshot("artifacts/life-timer/running_native.json");
  const auto runningDuration=timer.durationMs;press(UI_RIGHT);assert(timerPreset==0&&timer.durationMs==runningDuration);
  press(UI_UP);assert(timerPreset==0&&timer.durationMs==runningDuration);
  fakeMillis+=5000;press(UI_OK);assert(!timer.running&&!timer.finished); // pause
  const auto pausedRemaining=timer.remainingMs;
  fakeMillis+=1000;tick();assert(timer.remainingMs==pausedRemaining);
  tft.snapshot("artifacts/life-timer/paused_native.json");
  press(UI_RIGHT);assert(timerPreset==1&&timer.durationMs==8UL*60000);
  press(UI_LEFT);assert(!timer.running&&timer.remainingMs==timer.durationMs);
  press(UI_DOWN);assert(timerPreset==3&&customMinutes==7&&timer.durationMs==7UL*60000);
  tft.snapshot("artifacts/life-timer/custom_native.json");
  timer.setMinutes(180);customMinutes=180;press(UI_UP);assert(timer.durationMs==180UL*60000);screenRedraw();
  tft.snapshot("artifacts/life-timer/max_native.json");
  timer.setMinutes(1);press(UI_DOWN);assert(timer.durationMs==60000);
  // Preserve the timer mascot while composing the ring, buttons and footer.
  const auto counts=drawCounts();screenRedraw();expectFrames(counts,1,"life timer redraw");
  assert(tft.readPixel(216,119)==TFT_GREEN);
  press(UI_OK);assert(timer.running);fakeMillis+=timer.remainingMs+1;tick();assert(curScreen()==SCR_ALERT);
  press(UI_OK);assert(curScreen()==SCR_TIMER&&timer.finished);
  press(UI_OK);assert(timer.running&&!timer.finished); // completed timer restarts
  press(UI_LEFT);assert(!timer.running&&!timer.finished&&timer.remainingMs==timer.durationMs);
  assert(fakeFiles==files&&uiFrameCount()>startFrames);
  std::cout<<"PASS life timer preset/custom cycle, 1/180 bounds, run lock, paused time, reset, completion/restart, mascot preservation and single-frame drawing"<<std::endl;
}
