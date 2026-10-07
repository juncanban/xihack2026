static void testTreeModel() {
  assert(uiCanvasInit());clockInit();screensInit();
  const auto files=fakeFiles;
  const auto sprites=fb.spriteCreateCount;
  for(uint8_t screen:{static_cast<uint8_t>(SCR_TREE),static_cast<uint8_t>(SCR_CAPTURE)}) {
    for(int repetition=0;repetition<3;++repetition) {
      const auto counts=drawCounts();const auto draws=fakePetDraws;
      enter(screen);
      expectFrames(counts,1,"tree model full frame");
      assert(fakePetDraws==draws+1);
      const bool settings=screen==SCR_CAPTURE;
      const int left=settings?192:8,top=settings?127:52;
      const int w=settings?32:96,h=settings?37:112;
      // A unique source coordinate pattern detects in-place aliasing, stride,
      // byte-order, clipping and overlays across every destination pixel.
      for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const uint16_t expected=static_cast<uint16_t>(1+(y*PET_H/h)*PET_W+x*PET_W/w);
        assert(tft.readPixel(left+x,top+y)==expected);
      }
      assert(tft.readPixel(319,239)==FOOT);
      assert(tft.readPixel(0,205)==PAPER);
    }
  }
  assert(fb.spriteCreateCount==sprites&&fakeFiles==files);
  std::cout<<"PASS shared model: pixel-exact scaling at both sizes, no clipping/aliasing/byte swaps, single-frame redraw and no allocations/storage writes"<<std::endl;
}
