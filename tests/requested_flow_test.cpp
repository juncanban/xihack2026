// Real UI and storage modules, with isolated host display/flash substitutes.
#define main legacy_test_main
#include "orchard_test.cpp"
#undef main
int main() {
  std::signal(SIGABRT,[](int){std::_Exit(EXIT_FAILURE);});
  assert(uiCanvasInit());clockInit();clockSet(1791043200);screensInit();audioInit();tick();
  assert(clockNow().dateCode==20261004);
  press(UI_FARM);assert(curScreen()==SCR_CALENDAR&&code(selected)==20261004);
  press(UI_OK);assert(curScreen()==SCR_TASKS);press(UI_OK);assert(orchardDay(20261004));
  press(UI_OK);assert(curScreen()==SCR_GUIDE);
  press(UI_OK);press(UI_OK);press(UI_OK);assert(curScreen()==SCR_PRACTICE);
  const auto files=fakeFiles;
  press(UI_OK);assert(curScreen()==SCR_CONFIRM);press(UI_RIGHT);press(UI_OK);
  assert(curScreen()==SCR_TASKS&&(orchardDay(20261004)->reserved&1));
  std::cout<<"PASS practice-to-confirm task completion; no automatic completion/storage writes\n"<<std::flush;
  const auto sequence=orchardData.sequence;
  press(UI_OK);press(UI_OK);press(UI_OK);press(UI_OK);press(UI_DOWN);press(UI_OK);press(UI_RIGHT);press(UI_OK);
  assert(curScreen()==SCR_TASKS&&orchardData.sequence==sequence);
  orchardLoad();assert(orchardDay(20261004)->reserved&1);
  std::cout<<"PASS task confirmation, duplicate idempotency, save/reload; no fruit count field\n"<<std::flush;
  press(UI_FARM);assert(curScreen()==SCR_TREE);press(UI_DOWN);press(UI_OK);assert(curScreen()==SCR_ARCHIVE);
  press(UI_BACK);enter(SCR_TODAY);press(UI_OK);assert(curScreen()==SCR_PET);press(UI_OK);assert(curScreen()==SCR_TODAY);
  std::cout<<"PASS archive and pet entry/return (pet renderer/state dispatcher not exercised)\n"<<std::flush;
  enter(SCR_FRIDGE);press(UI_RIGHT);press(UI_UP);press(UI_UP);press(UI_OK);
  assert(fridgeData.items[0].stock==2&&fridgeData.items[0].eaten==0);
  press(UI_RIGHT);press(UI_DOWN);press(UI_DOWN);press(UI_OK);
  assert(fridgeData.items[0].stock==0&&fridgeData.items[0].eaten==2);
  fridgeLoad();assert(fridgeData.items[0].eaten==2);
  std::cout<<"PASS UI add/consume two apples and storage reload\n"<<std::flush;
  press(UI_FARM);assert(curScreen()==SCR_TIMER);
  for(int i=0;i<7;++i)press(UI_DOWN);
  assert(timer.durationMs==60000);press(UI_OK);assert(timer.running);
  fakeMillis+=60000;orchardTick();assert(curScreen()==SCR_ALERT&&timer.finished);
  press(UI_OK);assert(curScreen()==SCR_TIMER);
  std::cout<<"PASS simulated one-minute countdown, alert and return (not real elapsed time)\n"<<std::flush;
}
