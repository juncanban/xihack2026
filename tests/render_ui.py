"""Render real firmware draw calls to PNG. ASCII + Chinese use device bitmap fonts.

Run orchard_test.exe first. These are host-rendered fixtures, not hardware photos.
"""
from pathlib import Path
import json
import re
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "ui"
FONT_DIR = Path(os.environ.get("USERPROFILE", "C:/Users/15659")) / ".platformio/packages/framework-arduino-samd-seeed/libraries/Seeed_Arduino_LCD/Fonts"
FONTS = {}
for number, filename, key, height in [(2, "Font16.c", "f16", 16), (4, "Font32rle.c", "f32", 26), (6, "Font64rle.c", "f64", 48)]:
    source = re.sub(r"//[^\n]*", "", (FONT_DIR / filename).read_text())
    widths = list(map(int, re.findall(r"\d+", re.search(r"widtbl_" + key + r"\[96\]\s*=\s*\{(.*?)\}", source, re.S)[1])))
    glyphs = {}
    for cp, data in re.findall(r"chr_" + key + r"_([0-9A-Fa-f]{2})\[\d*\]\s*=\s*\{(.*?)\}", source, re.S):
        cp = int(cp, 16)
        values = [int(n, 16) for n in re.findall(r"0x([0-9A-Fa-f]+)", data)]
        width = widths[cp - 32]
        glyph = Image.new("1", (width, height))
        if number == 2:
            stride = len(values) // height
            for y in range(height):
                for x in range(min(width, stride * 8)):
                    if values[y * stride + x // 8] & (0x80 >> (x % 8)):
                        glyph.putpixel((x, y), 1)
        else:
            index = 0
            for value in values:
                length = (value & 127) + 1
                if value & 128:
                    for n in range(index, min(index + length, width * height)):
                        glyph.putpixel((n % width, n // width), 1)
                index += length
        glyphs[cp] = glyph
    FONTS[number] = (widths, height, glyphs)

def color(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)

def render(path):
    im = Image.new("RGB", (320, 240), "white")
    d = ImageDraw.Draw(im)
    for op in json.loads(path.read_text()):
        name, *a = op
        if name in ("rect", "outline", "round", "roundOutline"):
            x, y, w, h = a[:4]
            if w <= 0 or h <= 0: continue
            bounds = (x, y, x + w - 1, y + h - 1)
            if name == "rect": d.rectangle(bounds, fill=color(a[4]))
            elif name == "outline": d.rectangle(bounds, outline=color(a[4]))
            elif name == "round": d.rounded_rectangle(bounds, radius=a[4], fill=color(a[5]))
            else: d.rounded_rectangle(bounds, radius=a[4], outline=color(a[5]))
        elif name == "pixel": d.point(a[:2], fill=color(a[2]))
        elif name == "hline":
            x, y, w, c = a
            if w > 0: d.line((x, y, x + w - 1, y), fill=color(c))
        elif name == "line": d.line(a[:4], fill=color(a[4]))
        elif name in ("circle", "ellipse"):
            x, y, rx = a[:3]; ry = rx if name == "circle" else a[3]
            d.ellipse((x-rx,y-ry,x+rx,y+ry),fill=color(a[-1]))
        elif name == "text":
            x, y, font, datum, fg, bg, value = a
            widths, height, glyphs = FONTS[font]
            width = sum(widths[ord(ch)-32] for ch in value)
            if datum in (2,5): x -= width
            if datum == 4: x -= width//2
            if datum in (3,4,5): y -= height//2
            d.rectangle((x,y,x+width-1,y+height-1), fill=color(bg))
            for ch in value:
                cp = ord(ch)
                glyph = glyphs.get(cp, glyphs.get(32))
                im.paste(color(fg), (x,y), glyph)
                x += widths[cp-32]
    im.save(path.with_suffix(".png"))
    return im

if __name__ == "__main__":
    order = ["today", "calendar", "tree", "archive", "status", "adoption", "memorial", "capture", "fridge", "fridge_second", "fridge_item", "fridge_edit", "fridge_today", "fridge_unsynced", "timer", "tasks_empty", "tasks_completed", "craft", "guide", "confirm", "calendar_six_rows", "today_unsynced", "alert"]
    labels = ["今日", "农事月历", "我的树", "树木档案", "原成长详情", "领养信息", "原纪念记录", "采集设置", "冰箱库存 · 第一页", "冰箱库存 · 第二页", "单项食材操作", "数量编辑与确认", "今日记账与撤销", "冰箱未校时", "独立倒计时", "当天空记录", "清单确认完成", "选择工艺", "文字示教", "安全确认", "六行月份", "今日未校时", "时间到提醒"]
    assert len(order) == len(labels)
    order += ["calendar_month", "calendar_days", "calendar_today_selected"]
    labels += ["月历 · 月份栏焦点", "月历 · 日期区焦点", "月历 · 选中今天"]
    order += ["calendar_visit", "tasks_visit", "tasks_visit_created"]
    labels += ["月历 · 活动起止圈(演示)", "活动日清单摘要(演示)", "活动日清单与体验状态(演示)"]
    # Only render current fixtures; old reminder snapshots must not leak into this release.
    missing = [name for name in order if not (OUT / f"{name}.json").is_file()]
    if missing:
        raise RuntimeError(f"Missing UI fixtures: {missing}; run orchard_test.exe first")
    images = {name: render(OUT / f"{name}.json") for name in order}
    board = Image.new("RGB", (1440, 75 + ((len(order) + 3) // 4) * 285), "#f6f2e8")
    d = ImageDraw.Draw(board)
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 17)
    d.text((24, 15), "农事与冰箱助手 · 原生固件绘制快照（模拟数据，非真机照片）", font=font, fill="#2f6b4f")
    for index, (name, label) in enumerate(zip(order, labels)):
        x = 24 + index % 4 * 354; y = 55 + index // 4 * 285
        d.text((x, y), f"{index+1:02}  {label}", font=font, fill="#2f6b4f")
        d.rounded_rectangle((x-4,y+27,x+323,y+274),radius=6,fill="#16324f")
        board.paste(images[name], (x, y+31))
    board.save(OUT / "overview.png")
    print(f"Rendered {len(images)} screens; overview: {OUT / 'overview.png'}")
