// Focused archive tests using the existing real UI/clock/storage harness.
static void archiveShot(const char* name) {
  tft.snapshot((std::string("artifacts/archive_firmware/")+name+".json").c_str());
}
static void testArchive() {
  using namespace archiveview;
  assert(uiCanvasInit());clockInit();clockSet(1790956800);screensInit();tick();
  const auto files=fakeFiles;const auto farm=orchardData;
  enter(SCR_TREE);treeRow=1;press(UI_OK);assert(curScreen()==SCR_ARCHIVE&&archiveScrollOffset()==0);
  archiveShot("01_archive_top");
  press(UI_OK);assert(curScreen()==SCR_ARCHIVE);press(UI_LEFT);press(UI_RIGHT);assert(curScreen()==SCR_ARCHIVE);
  const auto atTop=drawCounts();press(UI_UP);assert(archiveScrollOffset()==0);expectFrames(atTop,0,"archive top stops scrolling");
  for(int i=0;i<3;++i)press(UI_DOWN);
  assert(!archiveLevelFocused());press(UI_OK);assert(curScreen()==SCR_ARCHIVE);
  press(UI_DOWN);assert(archiveScrollOffset()==96&&archiveLevelFocused());archiveShot("02_archive_level");
  press(UI_OK);assert(curScreen()==SCR_STATUS&&archiveScrollOffset(true)==0);archiveShot("03_growth_top");
  for(int i=0;i<10;++i)press(UI_DOWN);
  assert(archiveScrollOffset(true)==limit(DETAIL_HEIGHT));archiveShot("04_growth_bottom");
  press(UI_TIMER);assert(curScreen()==SCR_ARCHIVE&&archiveScrollOffset()==96&&archiveLevelFocused());
  press(UI_OK);assert(archiveScrollOffset(true)==0);press(UI_BACK);assert(curScreen()==SCR_ARCHIVE&&archiveScrollOffset()==96);
  while(archiveScrollOffset()<432)press(UI_DOWN);
  archiveShot("05_cycles");press(UI_OK);assert(curScreen()==SCR_ARCHIVE&&!archiveLevelFocused());
  for(int i=0;i<100;++i){press(UI_DOWN);}
  assert(archiveScrollOffset()==limit(contentHeight(fakeMileCount)));archiveShot("06_memorial");
  const auto bottom=drawCounts();press(UI_DOWN);expectFrames(bottom,0,"archive bottom stops scrolling");
  press(UI_OK);assert(curScreen()==SCR_ARCHIVE);
  press(UI_TIMER);assert(curScreen()==SCR_TREE&&treeRow==1);press(UI_OK);assert(archiveScrollOffset()==0);
  press(UI_FARM);assert(curScreen()==SCR_FRIDGE);enter(SCR_ARCHIVE);
  for(int i=0;i<4;++i){press(UI_DOWN);}press(UI_OK);press(UI_FARM);assert(curScreen()==SCR_FRIDGE);
  std::cout<<"PASS archive scrolling, visible-only level focus, details return, no cycle links, owning-root B"<<std::endl;

  // Reminders restore both long-page and detail offsets without writing data.
  enter(SCR_ARCHIVE);for(int i=0;i<4;++i){press(UI_DOWN);}
  timer.setMinutes(1);timer.toggle(fakeMillis);fakeMillis+=61000;orchardTick();
  assert(curScreen()==SCR_ALERT);press(UI_OK);assert(curScreen()==SCR_ARCHIVE&&archiveScrollOffset()==96);
  press(UI_OK);press(UI_DOWN);const int detailOffset=archiveScrollOffset(true);
  timer.setMinutes(1);timer.toggle(fakeMillis);fakeMillis+=61000;orchardTick();
  assert(curScreen()==SCR_ALERT);press(UI_BACK);assert(curScreen()==SCR_STATUS&&archiveScrollOffset(true)==detailOffset);
  std::cout<<"PASS reminder restore keeps archive/detail scroll positions"<<std::endl;

  assert(progress(0,0,STAGE_NEED).percent==0);assert(progress(0,300,STAGE_NEED).percent==100);
  assert(progress(1,300,STAGE_NEED).earned==0&&progress(1,850,STAGE_NEED).percent==50);
  assert(progress(1,850,STAGE_NEED).need==1100);assert(progress(2,6000,STAGE_NEED).percent==100);
  assert(progress(3,6000,STAGE_NEED).maximum&&progress(255,0,STAGE_NEED).percent==100);
  fakeStage=3;fakeSunToday=105*256;screenRedraw();archiveShot("07_growth_max");
  fakeStage=0;fakeSunToday=12*256;screenRedraw();
  auto counts=drawCounts();fakeSunRaw=400;fakeMillis+=1001;screenTick1Hz();expectFrames(counts,0,"light no longer changes growth view");
  counts=drawCounts();fakeSunTotal+=256;fakeMillis+=1001;screenTick1Hz();expectFrames(counts,1,"nutrition still refreshes growth view");
  fakeSunTotal-=256;
  clockInit();fakeAdopted=false;fakeMileCount=0;enter(SCR_ARCHIVE);archiveShot("08_not_adopted");
  for(int i=0;i<100;++i){press(UI_DOWN);}archiveShot("09_no_memorial");assert(archiveScrollOffset()==limit(contentHeight(0)));
  fakeMileCount=8;screenRedraw();for(int i=0;i<100;++i){press(UI_DOWN);}
  assert(archiveScrollOffset()==limit(contentHeight(8)));archiveShot("10_many_memories");
  fakeMileCount=1;fakeAdopted=true;clockSet(1790956800);
  std::cout<<"PASS stage progress, maximum level, daily cap, no light refresh, unsynced/empty/long data"<<std::endl;

  const auto& harvest=visits::SCHEDULE[3];
  assert(visits::remaining(harvest,20261003)==13);assert(visits::phase(harvest,20261016)==visits::START);
  assert(visits::phase(harvest,20261020)==visits::DURING);assert(visits::phase(harvest,20261102)==visits::END);
  assert(visits::phase(harvest,20261103)==visits::PAST);
  assert(visits::next(20261030)==&visits::SCHEDULE[4]);assert(visits::phase(visits::SCHEDULE[4],20261030)==visits::SINGLE);
  assert(visits::next(20270101)==nullptr&&visits::next(0)==nullptr);
  HoldRepeat held;assert(!held.update(100,0,1));assert(!held.update(499,0,1));assert(held.update(500,0,1)==1);
  assert(!held.update(599,0,1));assert(held.update(600,0,1)==1);assert(!held.update(601,0,0));
  assert(!held.update(602,1,1));assert(!held.update(999,1,1));assert(held.update(1002,1,1)==1);
  assert(!held.update(1003,-1,1));assert(!held.update(UINT32_MAX-100,0,-1));assert(held.update(299,0,-1)==-1);
  std::cout<<"PASS schedule boundaries, single visit, no next-year repetition and scoped hold timing"<<std::endl;

  // Exact pixels check both overlapping-copy directions without another image buffer.
  enter(SCR_ARCHIVE);const uint16_t guard=0xA55A;
  for(int off:{0,24,96,168,185}) {
    archive_ui::scroll=off;std::fill(displayPixels,displayPixels+320*240+1,guard);
    for(int y=0;y<PET_H;++y)for(int x=0;x<PET_W;++x)displayPixels[y*PET_W+x]=1+(y*197+x)%65534;
    uiFrameBegin();fb.pushSprite(160,archivePetY());uiFrameEnd();
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
      const int row=y-archivePetY();const bool inside=x>=160&&y>=TOP&&y<BOTTOM&&row>=0&&row<PET_H;
      const uint16_t expected=inside?1+(row*197+x-160)%65534:TFT_WHITE;assert(tft.readPixel(x,y)==expected);
    }
    assert(displayPixels[320*240]==guard);
  }
  assert(fakeFiles==files&&std::memcmp(&orchardData,&farm,sizeof farm)==0);
  assert(tft.directDrawCount==0);
  std::cout<<"PASS original pet pixel clipping, arena bounds, single-frame composition, unchanged farm/storage"<<std::endl;
  std::cout<<"ARCHIVE TARGET TESTS PASSED"<<std::endl;
}
