"""Ensure pet save schema remains intact and all new labels have glyphs."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware/src"
BACKUP = ROOT / "backups/before_orchard_ui_20261002/src"

# Only permit the diagnostic directory enumeration's two explicit closes.
# The Seeed File destructor is empty; repeatedly querying Q must not leak
# one 4 KB FAT buffer per file after adding the shared screen arena.
old_save = (BACKUP / "save.cpp").read_text(encoding="utf-8")
expected_save = old_save.replace(
    '      e = dir.openNextFile();',
    '      e.close(); // Seeed File destructor does not release its FAT buffer.\n      e = dir.openNextFile();'
).replace('    }\n  } else {\n    Serial.println("Q:root open fail");',
          '    }\n    dir.close();\n  } else {\n    Serial.println("Q:root open fail");')
assert (SRC / "save.cpp").read_text(encoding="utf-8") == expected_save, "Pet save logic changed outside diagnostic resource cleanup"

def glyphs(path):
    data = path.read_text(encoding="utf-8")
    cp = re.search(r"HANZI_CP\[HANZI_COUNT\] = \{([^}]+)\}", data)[1]
    codes = [int(n,16) for n in re.findall(r"0x([\dA-Fa-f]+)",cp)]
    body = re.search(r"HANZI_GLYPHS\[HANZI_COUNT\]\[32\] = \{(.*?)\n\};",data,re.S)[1]
    rows = re.findall(r"\{([^}]+)\}", body)
    return {c: tuple(int(n,16) for n in re.findall(r"0x([\dA-Fa-f]+)",row)) for c,row in zip(codes,rows)}

old_glyphs = glyphs(BACKUP / "hanzi16.h")
new_glyphs = glyphs(SRC / "hanzi16.h")
assert all(new_glyphs[c] == bitmap for c,bitmap in old_glyphs.items()), "Existing glyph changed"
texts = []
for path in SRC.glob("*.cpp"):
    texts += re.findall(r'u8"([^"]*)"',path.read_text(encoding="utf-8"))
missing = {ch for text in texts for ch in text if ord(ch)>=128 and ord(ch) not in new_glyphs}
assert not missing, f"Missing glyphs: {missing}"
screens = (SRC / "screens.cpp").read_text(encoding="utf-8")
assert "const int16_t mid = lo + (hi - lo) / 2" in screens, "Hanzi lookup is not binary search"
assert "drawFastHLine" in screens, "Hanzi glyphs are not drawn with horizontal runs"
print(f"PASS save schema unchanged; diagnostic file handles closed; {len(old_glyphs)} existing glyphs preserved; {len(new_glyphs)} total glyphs cover all labels")
