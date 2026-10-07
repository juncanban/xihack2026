static void testTimerActions() {
  assert(uiCanvasInit());clockInit();screensInit();enter(SCR_TIMER);
  const auto files=fakeFiles;
  assert(timerAction==1&&timer.durationMs==480000&&!timer.running);
  auto actionDrawn=[](int x,uint16_t color) {
    const std::string op="[\"round\","+std::to_string(x)+",163,108,35,5,"+std::to_string(color)+"]";
    return std::find(tft.ops.begin(),tft.ops.end(),op)!=tft.ops.end();
  };
  assert(actionDrawn(176,GREEN)&&actionDrawn(36,PALE));
  for(const auto& op:tft.ops) assert(op.find("25 min")==std::string::npos&&op.find("3 min")==std::string::npos);
  tft.snapshot("artifacts/timer-actions/start_native.json");
  press(UI_LEFT);assert(timerAction==0&&!timer.running&&timer.remainingMs==480000);
  assert(actionDrawn(36,GREEN)&&actionDrawn(176,PALE));
  press(UI_LEFT);assert(timerAction==0);
  tft.snapshot("artifacts/timer-actions/reset_native.json");
  press(UI_UP);assert(timerAction==0&&timer.durationMs==540000);
  press(UI_DOWN);assert(timerAction==0&&timer.durationMs==480000);
  press(UI_OK);assert(timerAction==0&&!timer.running); // Reset does not start.
  press(UI_RIGHT);press(UI_RIGHT);assert(timerAction==1&&timer.durationMs==480000);
  press(UI_OK);assert(timer.running);
  fakeMillis+=1000;tick();
  const auto remaining=timer.remainingMs;
  press(UI_LEFT);assert(timerAction==0&&timer.running&&timer.remainingMs==remaining);
  press(UI_DOWN);assert(timer.running&&timer.durationMs==480000&&timerAction==0);
  press(UI_RIGHT);assert(timerAction==1&&timer.running&&timer.remainingMs==remaining);
  press(UI_OK);assert(!timer.running&&timer.remainingMs==remaining);
  tft.snapshot("artifacts/timer-actions/paused_native.json");
  fakeMillis+=3000;tick();assert(timer.remainingMs==remaining);
  press(UI_OK);assert(timer.running);press(UI_LEFT);press(UI_OK);
  assert(!timer.running&&timer.remainingMs==480000&&timerAction==0);
  // Every operation keeps data independent of timer focus.
  for(int i=0;i<190;++i) press(UI_UP);
  assert(timer.durationMs==180UL*60000&&timerAction==0);
  tft.snapshot("artifacts/timer-actions/max_native.json");
  for(int i=0;i<190;++i) press(UI_DOWN);
  assert(timer.durationMs==60000&&timerAction==0);
  const auto count=drawCounts();press(UI_DOWN);expectFrames(count,0,"lower timer limit");
  press(UI_RIGHT);press(UI_OK);fakeMillis+=61000;tick();assert(curScreen()==SCR_ALERT);
  press(UI_OK);assert(curScreen()==SCR_TIMER&&timerAction==1&&timer.finished);
  press(UI_OK);assert(timer.running&&!timer.finished);
  press(UI_LEFT);press(UI_OK);assert(!timer.running&&!timer.finished);
  press(UI_FARM);assert(curScreen()==SCR_TODAY);
  press(UI_TIMER);assert(curScreen()==SCR_TIMER&&timerAction==1); // Fresh entry default.
  assert(fakeFiles==files);
  std::cout<<"PASS timer actions: visual focus, no presets, left/right non-destructive selection, middle execution, up/down minutes, running guard, 1/180 bounds, C/alert restore and no data writes"<<std::endl;
}
