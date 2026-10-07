static void testTreeSettings() {
  assert(uiCanvasInit());clockInit();screensInit();
  // Existing capture settings must survive replacing the editable page.
  orchardData.batch=23;assert(orchardSave());
  fridgeData.items[0]={12,3,0};assert(fridgeSave());
  const auto files=fakeFiles;
  const auto farmBefore=orchardData;
  const auto fridgeBefore=fridgeData;
  const auto stage=fakeStage;
  strcpy(gPetName,"APPLE-TREE12"); // Longest supported name (12 ASCII characters).
  enter(SCR_TREE);treeRow=0;press(UI_DOWN);press(UI_DOWN);
  assert(treeRow==2);
  assert(strcmp(TREE_ENTRIES[2].label,u8"苹果树设置")==0);
  press(UI_OK);
  assert(curScreen()==SCR_CAPTURE&&navigationRoot(curScreen())==SCR_TREE);
  bool hasName=false;
  for(const auto& op:tft.ops) if(op.find("APPLE-TREE12")!=std::string::npos) hasName=true;
  assert(hasName);
  tft.snapshot("artifacts/tree-settings/settings_long_name_native.json");
  for(int i=0;i<3;++i) for(UiButton b:{UI_UP,UI_DOWN,UI_LEFT,UI_RIGHT,UI_OK}) press(b);
  assert(curScreen()==SCR_CAPTURE&&toast==nullptr);
  assert(fakeFiles==files&&fakeStage==stage&&strcmp(gPetName,"APPLE-TREE12")==0);
  assert(memcmp(&orchardData,&farmBefore,sizeof farmBefore)==0);
  assert(memcmp(&fridgeData,&fridgeBefore,sizeof fridgeBefore)==0);
  assert(captureBatch==23); // Even old draft values cannot change.
  press(UI_TIMER);assert(curScreen()==SCR_TREE&&treeRow==2);
  press(UI_OK);press(UI_BACK);assert(curScreen()==SCR_TREE&&treeRow==2);
  alertReturn=SCR_CAPTURE;enter(SCR_ALERT);press(UI_OK);assert(curScreen()==SCR_CAPTURE);
  press(UI_FARM);assert(curScreen()==SCR_FRIDGE);
  assert(fakeFiles==files);
  // Each stage goes through the shared drawPet entry point (host stub).
  for(uint8_t i=0;i<4;++i) {
    fakeStage=i;const auto draws=fakePetDraws;enter(SCR_CAPTURE);
    assert(fakePetDraws==draws+1);
    assert(tft.readPixel(208,145)==static_cast<uint16_t>(1+(18*PET_H/37)*PET_W+16*PET_W/32));
  }
  fakeStage=stage;strcpy(gPetName,"PINGPING");enter(SCR_TREE);
  tft.snapshot("artifacts/tree-settings/menu_native.json");press(UI_OK);
  tft.snapshot("artifacts/tree-settings/settings_native.json");
  assert(fakeFiles==files);
  std::cout<<"PASS tree settings: renamed entry, live name/stage, read-only keys, capture/storage preservation, A/back and alert return"<<std::endl;
}
