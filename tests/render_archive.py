"""Render only archive target fixtures; stubs are not hardware photos."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from render_ui import render

OUT = Path(__file__).resolve().parents[1] / "artifacts/archive_firmware"
for path in sorted(OUT.glob("[0-9][0-9]_*.json")):
    render(path)
selected = ["02_archive_level", "03_growth_top", "04_growth_bottom", "05_cycles", "06_memorial",
            "07_growth_max", "08_not_adopted", "09_no_memorial", "10_many_memories"]
board = Image.new("RGB", (1050, 950), "#F0F2EC")
draw = ImageDraw.Draw(board)
font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 17)
draw.text((18, 12), "档案专项模拟渲染 · 固件绘制指令与设备字模 · 宠物为测试替身", font=font, fill="#203D30")
for i, name in enumerate(selected):
    x, y = 18 + i % 3 * 346, 55 + i // 3 * 290
    draw.text((x, y), name, font=font, fill="#203D30")
    board.paste(Image.open(OUT / (name + ".png")), (x, y + 28))
board.save(OUT / "overview.png")
print("PASS rendered archive target fixtures only")
