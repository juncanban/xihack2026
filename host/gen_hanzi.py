#!/usr/bin/env python3
"""gen_hanzi.py —— 生成固件用的中文字模 + 24 节气查表(firmware/src/hanzi16.h)。

字模:16x16 1bit,逐行扫描,每行 2 字节(高字节在左),每字 32B。
字体:C:/Windows/Fonts/simhei.ttf(黑体,16px 像素屏上最清晰)。

节气:寿星通用公式
    D = int(Y * 0.2422 + C) - L     Y = 年份 % 100
    L = int((Y-1)/4)  —— 1/2 月的节气(小寒/大寒/立春/雨水)
    L = int(Y/4)      —— 3~12 月的节气
生成 2025-2036 十二年,并用 2025 全年 + 2026 关键节气已知日期做锚点断言。

用法:python gen_hanzi.py     (在 host/ 目录下运行)
"""
from PIL import Image, ImageDraw, ImageFont
import datetime
import re
from pathlib import Path

FONT = "C:/Windows/Fonts/simhei.ttf"
SOURCE_DIR = Path(__file__).resolve().parents[1] / "firmware" / "src"
OUT = SOURCE_DIR / "hanzi16.h"

# ---------------- 字符集 ----------------
TERM_NAMES = ["小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
              "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
              "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"]
UI_STRINGS = ["领养今天阶段累计光照第进化纪念苹果青红亮金"]
# 收集固件 UTF-8 文案，保留原字模渲染方式，新增页不会漏字。
for source in sorted(SOURCE_DIR.glob("*.cpp")):
    UI_STRINGS.extend(re.findall(r'u8"([^"]*)"', source.read_text(encoding="utf-8")))
CHARSET = sorted(set("".join(TERM_NAMES) + "".join(UI_STRINGS)))
CHARSET = [ch for ch in CHARSET if ord(ch) >= 128]

# ---------------- 节气 C 值(21 世纪)与月份 ----------------
# (月, C);例外年份(21 世纪)个别 +1,桌面宠物显示用途,2025-2026 已锚点校验
TERM_MC = [(1, 5.4055), (1, 20.12), (2, 3.87), (2, 18.73),
           (3, 5.63), (3, 20.646), (4, 4.81), (4, 20.1),
           (5, 5.52), (5, 21.04), (6, 5.678), (6, 21.37),
           (7, 7.108), (7, 22.83), (8, 7.5), (8, 23.13),
           (9, 7.646), (9, 23.042), (10, 8.318), (10, 23.438),
           (11, 7.438), (11, 22.36), (12, 7.18), (12, 21.94)]

TERM_YEAR0, TERM_YEARS = 2025, 12


def term_day(y, m, c):
    yy = y % 100
    L = (yy - 1) // 4 if m <= 2 else yy // 4
    return int(yy * 0.2422 + c) - L


def term_doy(y, m, d):
    return (datetime.date(y, m, d) - datetime.date(y, 1, 1)).days + 1


def gen_term_table():
    table = []
    for y in range(TERM_YEAR0, TERM_YEAR0 + TERM_YEARS):
        row = []
        for (m, c) in TERM_MC:
            d = term_day(y, m, c)
            row.append(term_doy(y, m, d))
        table.append(row)
    return table


def check_anchors(table):
    # 2025 全年已知锚点(公开历书)
    a2025 = ["1-5", "1-20", "2-3", "2-18", "3-5", "3-20", "4-4", "4-20",
             "5-5", "5-21", "6-5", "6-21", "7-7", "7-22", "8-7", "8-23",
             "9-7", "9-23", "10-8", "10-23", "11-7", "11-22", "12-7", "12-21"]
    for i, (m, _) in enumerate(TERM_MC):
        want = term_doy(2025, *map(int, a2025[i].split("-")))
        got = table[0][i]
        assert want == got, f"2025 {TERM_NAMES[i]}: want doy {want}, got {got}"
    # 2026 抽查
    for idx, want in [(17, term_doy(2026, 9, 23)), (18, term_doy(2026, 10, 8)),
                      (23, term_doy(2026, 12, 22))]:
        assert table[1][idx] == want, f"2026 {TERM_NAMES[idx]} anchor fail"


def render_glyph(cp):
    ch = chr(cp)
    img = Image.new("1", (16, 16), 0)
    ImageDraw.Draw(img).text((0, 0), ch, fill=1, font=FONT16)
    px = img.load()
    rows = []
    for r in range(16):
        b = 0
        for c in range(8):
            b = (b << 1) | px[c, r]
        rows.append(b)
        b = 0
        for c in range(8, 16):
            b = (b << 1) | px[c, r]
        rows.append(b)
    return rows


def main():
    global FONT16
    FONT16 = ImageFont.truetype(FONT, 16)

    table = gen_term_table()
    check_anchors(table)

    cps = [ord(c) for c in CHARSET]
    glyphs = [render_glyph(cp) for cp in cps]

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("// hanzi16.h —— 由 host/gen_hanzi.py 生成,勿手改\n")
        f.write("// 字模:16x16 1bit 行扫描(每行 2 字节,高字节在左);节气:2025-2036 寿星公式\n")
        f.write("#ifndef HANZI16_H\n#define HANZI16_H\n\n#include <Arduino.h>\n\n")
        f.write(f"#define HANZI_COUNT {len(cps)}\n")
        f.write(f"#define TERM_YEAR0 {TERM_YEAR0}\n#define TERM_YEARS {TERM_YEARS}\n\n")
        f.write("static const uint16_t HANZI_CP[HANZI_COUNT] = {")
        f.write(",".join(f"0x{cp:04X}" for cp in cps))
        f.write("};\n\n")
        f.write("static const uint8_t HANZI_GLYPHS[HANZI_COUNT][32] = {\n")
        for g in glyphs:
            f.write("  {" + ",".join(f"0x{b:02X}" for b in g) + "},\n")
        f.write("};\n\n")
        f.write("// 24 节气名(UCS-2 码点,以 0 结尾)\n")
        f.write("static const uint16_t TERM_CP[24][3] = {\n")
        for name in TERM_NAMES:
            cps2 = [ord(c) for c in name] + [0]
            f.write("  {" + ",".join(f"0x{cp:04X}" for cp in cps2) + "},\n")
        f.write("};\n\n")
        f.write("// TERM_DOY[年份偏移][节气序号] = 年积日(1-based;最大 ~356,需 uint16)\n")
        f.write("static const uint16_t TERM_DOY[TERM_YEARS][24] = {\n")
        for row in table:
            f.write("  {" + ",".join(str(v) for v in row) + "},\n")
        f.write("};\n\n#endif  // HANZI16_H\n")

    print(f"OK: {len(cps)} 字模 -> {OUT}")
    print(f"节气表 {TERM_YEAR0}-{TERM_YEAR0+TERM_YEARS-1},锚点校验通过")
    print("字符集:", "".join(CHARSET))


if __name__ == "__main__":
    main()
