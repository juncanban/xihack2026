static void testFooterIcons() {
  assert(uiCanvasInit());clockInit();clockSet(1790956800);screensInit();tick();
  const auto files=fakeFiles;
  const auto snap=[](const char* name){tft.snapshot((std::string("artifacts/footer-icons/native/")+name+".json").c_str());};
  // Months and dates have distinct down-key behavior, but the same task entry.
  enter(SCR_CALENDAR);assert(calendarFocus==CAL_DAYS&&code(selected)==20261003);
  snap("calendar_days");
  press(UI_UP);assert(calendarFocus==CAL_MONTH);snap("calendar_month");
  press(UI_OK);assert(curScreen()==SCR_TASKS&&code(selected)==20261003);
  assert(!orchardDay(code(selected)));snap("tasks_empty");
  press(UI_TIMER);assert(curScreen()==SCR_CALENDAR&&calendarFocus==CAL_DAYS&&code(selected)==20261003);
  press(UI_UP);press(UI_RIGHT);assert(selected.month==11&&calendarFocus==CAL_MONTH);
  const auto otherMonth=code(selected);
  press(UI_DOWN);assert(calendarFocus==CAL_DAYS&&code(selected)==otherMonth);
  press(UI_OK);assert(curScreen()==SCR_TASKS&&code(selected)==otherMonth);
  press(UI_TIMER);assert(calendarFocus==CAL_DAYS&&code(selected)==otherMonth);
  press(UI_UP);press(UI_OK);assert(curScreen()==SCR_TASKS&&code(selected)==otherMonth);
  press(UI_TIMER);
  assert(curScreen()==SCR_CALENDAR&&code(selected)==otherMonth&&calendarFocus==CAL_DAYS);
  enter(SCR_TREE);snap("tree");enter(SCR_CALENDAR);
  assert(calendarFocus==CAL_DAYS&&code(selected)==20261003);
  enter(SCR_CAPTURE);snap("settings");enter(SCR_TODAY);snap("home");
  enter(SCR_FRIDGE);snap("fridge_select");press(UI_RIGHT);snap("fridge_edit");
  press(UI_RIGHT);snap("fridge_eaten");press(UI_BACK); // Cancel focus, no save.
  enter(SCR_ARCHIVE);snap("archive");enter(SCR_STATUS);snap("growth");
  enter(SCR_CRAFT);snap("craft");enter(SCR_GUIDE);guideStep=0;screenRedraw();snap("guide");
  guideStep=2;screenRedraw();snap("guide_last");
  enter(SCR_PRACTICE);snap("practice");enter(SCR_CONFIRM);snap("confirm");
  enter(SCR_ALERT);snap("alert");
  clockInit();dateReady=false;enter(SCR_CALENDAR);snap("calendar_unsynced");
  press(UI_OK);assert(curScreen()==SCR_CALENDAR&&timeRequestPending);
  assert(fakeFiles==files);
  std::cout<<"PASS footer icons: month/down vs middle entry, selected-date retention, normal today entry, no task creation or data writes; state-specific native snapshots"<<std::endl;
}
