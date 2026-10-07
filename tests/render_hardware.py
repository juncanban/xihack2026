"""Convert actual LCD readback PPM files and produce a review sheet."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parents[1] / "artifacts/hardware"
files = sorted(OUT.glob("[0-9][0-9]_*.ppm"))
font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 17)
names = {
    "01_pet": "原宠物页面", "02_tree": "我的树", "03_archive": "树木档案",
    "04_status": "成长详情", "05_adoption": "领养信息", "06_memorial": "纪念记录",
    "07_capture_saved": "采集保存测试（随后已恢复）", "08_today": "今日",
    "09_calendar": "月历", "10_tasks": "当天农事（未创建测试记录）",
    "11_reminders": "提醒设置（未保存测试提醒）", "12_timer_running": "真实一分钟倒计时",
    "13_timer_alert": "到点提醒", "14_pet_final": "验证结束，恢复宠物页",
}
rows = (len(files) + 3) // 4
board = Image.new("RGB", (1440, 65 + rows * 285), "#f6f2e8")
d = ImageDraw.Draw(board)
d.text((24, 16), "Wio Terminal 真机 LCD 显存回读 · 2026-10-02 · 非模拟渲染，非相机照片", font=font, fill="#2f6b4f")
for i, path in enumerate(files):
    im = Image.open(path).convert("RGB")
    im.save(path.with_suffix(".png"))
    x, y = 24 + i % 4 * 354, 56 + i // 4 * 285
    d.text((x, y), names.get(path.stem, path.stem), font=font, fill="#2f6b4f")
    d.rounded_rectangle((x-4, y+27, x+323, y+274), radius=6, fill="#16324f")
    board.paste(im, (x, y+31))
board.save(OUT / "hardware_overview.png")
print(f"Rendered {len(files)} actual LCD readbacks: {OUT / 'hardware_overview.png'}")
