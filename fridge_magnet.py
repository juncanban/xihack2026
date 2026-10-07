#!/usr/bin/env python3
"""冰箱贴 —— 像素小苹果桌面挂件。

一块贴在你桌面（冰箱门）上的像素风小苹果：
  * 无边框、置顶、可拖动，右键菜单（演示/置顶/退出）
  * UDP 127.0.0.1:9911 收取 Claude Code hooks 的真实状态
    （复用 host/hook_event.py，无需任何改动）
  * 没有真实事件时自动进入演示轮播；真实事件随时打断
  * --serial COM8 可选：把状态同步转发给 Wio Terminal 真机
    （此时不要同时运行 host/vibe_pet_host.py，9911 端口二选一）

纯标准库 + Pillow。渲染与 result.html 像素演示同一套画法：
160x120 低分辨率软件光栅化 -> 2x 最近邻放大。

用法：
    python fridge_magnet.py                 # 演示模式
    python fridge_magnet.py --serial COM8   # 联动真机
"""
import argparse
import random
import socket
import sys
import threading
import time

import tkinter as tk
from PIL import Image, ImageTk

W, H = 160, 120
SC = 2

# ---------------- 调色板 (r, g, b) ----------------
def C(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255)

SKY_TOP, SKY_BOT = C(0x141B33), C(0x1B1030)
MOON = C(0xF9E2AF), SKY_TOP
HILL = C(0x241A3E)
GROUND, GRASS = C(0x2D2440), C(0x3B2F55)
OUT = C(0x471217)      # 苹果描边
BODY = C(0xEF4444)     # 苹果主体
DARK = C(0xA31515)     # 脚
SHD = C(0xC62B2B)      # 手臂
STK = (139, 90, 43)    # 果柄
LEAF, LEAFD = C(0x3FBB52), C(0x1F7A2F)
BLUSH = C(0xFF9FB2)
HILI = (255, 150, 150)
SHADE = (120, 10, 20)
EYED = C(0x241F31)
LASH = C(0x5A4A6A)
MOUTH = (71, 18, 23)
TONGUE = C(0xFF8FA3)
STARC, STARP = C(0xCDD6F4), C(0xF5C2E7)
YELLOW, WHITE, GREEN, CYAN, RED = C(0xFFD93D), (255, 255, 255), C(0x66BB6A), C(0x4DD0E1), C(0xFF5252)
PINKC = C(0xF5C2E7)
BLUE = C(0x90CAF9)
LAPTOP = (44, 52, 64)
SCREEN = C(0x8ECAFC)
BUBBLE = (230, 235, 245)
QCOL = (226, 88, 34)

STATES = ["IDLE", "THINKING", "TOOL", "WAIT", "DONE", "ERROR", "SLEEP"]
AUTO_SEQ = ["THINKING", "TOOL", "WAIT", "THINKING", "TOOL", "DONE", "ERROR", "DONE", "IDLE"]

GLYPH = {
    "?": ["01110", "10001", "00001", "00010", "00100", "00000", "00100"],
    "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    "Z": ["11111", "00010", "00100", "01000", "11111"],
    "z": ["1111", "0010", "0100", "1111"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
}

STARS = [((i * 67 + 13) % 160, (i * 41 + 7) % 78) for i in range(18)]

# ---------------- 软件光栅化 ----------------
class FB:
    def __init__(self):
        self.buf = bytearray(W * H * 3)

    def clear_copy(self, base):
        self.buf[:] = base

    def pset(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < W and 0 <= y < H:
            i = (y * W + x) * 3
            self.buf[i:i + 3] = bytes(c)

    def frect(self, x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.pset(xx, yy, c)

    def fcirc(self, cx, cy, r, c):
        r2 = r * r
        for yy in range(int(cy - r), int(cy + r) + 1):
            dy2 = (yy - cy) ** 2
            if dy2 > r2:
                continue
            dx = int((r2 - dy2) ** 0.5)
            for xx in range(int(cx - dx), int(cx + dx) + 1):
                self.pset(xx, yy, c)

    def fell(self, cx, cy, rx, ry, c):
        for yy in range(int(cy - ry), int(cy + ry) + 1):
            k = (yy - cy) / ry
            dx = int(rx * max(0.0, 1 - k * k) ** 0.5)
            for xx in range(int(cx - dx), int(cx + dx) + 1):
                self.pset(xx, yy, c)

    def line(self, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.pset(x0, y0, c)
            self.pset(x0 + 1, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = err * 2
            if e2 > -dy:
                err -= dy; x0 += sx
            if e2 < dx:
                err += dx; y0 += sy

    def glyph(self, ch, x, y, c, sc=1):
        rows = GLYPH.get(ch)
        if not rows:
            return
        for r, row in enumerate(rows):
            for cc, v in enumerate(row):
                if v == "1":
                    self.frect(x + cc * sc, y + r * sc, sc, sc, c)


# ---------------- 静态背景 ----------------
def build_base():
    base = bytearray(W * H * 3)
    tmp = FB()
    for y in range(H):
        k = y / H
        col = tuple(int(a + (b - a) * k) for a, b in zip(SKY_TOP, SKY_BOT))
        for x in range(W):
            tmp.pset(x, y, col)
    # 月亮（像素圆 + 缺角）
    tmp.fcirc(134, 16, 6, MOON[0])
    tmp.fcirc(131, 14, 5, MOON[1])
    # 远山
    for i, (x0, y0, x1, y1, x2, y2) in enumerate([(0, 86, 34, 58, 70, 86), (48, 86, 92, 52, 140, 86)]):
        for yy in range(y1, 87):
            span = (yy - y1) / (y0 - y1)
            left = x1 - span * (x1 - x0)
            right = x1 + span * (x2 - x1)
            for xx in range(int(left), int(right) + 1):
                tmp.pset(xx, yy, HILL)
    # 地面
    tmp.frect(0, 86, W, H - 86, GROUND)
    for x in range(0, W, 8):
        tmp.frect(x, 86, 4, 1, GRASS)
    base[:] = tmp.buf
    return base


# ---------------- 宠物渲染 ----------------
class Pet:
    def __init__(self):
        self.state = "IDLE"
        self.since = time.time()
        self.demo = True
        self.demo_idx = 0
        self.link_flash = 0.0          # 收到真实事件时闪绿点

    def set_state(self, s, real=False):
        if s == self.state:
            return
        self.state = s
        self.since = time.time()
        if real:
            self.link_flash = time.time() + 2.0

    def auto_state(self, now):
        """演示轮播：无真实事件时使用"""
        if now - self.since > 2.6:
            self.demo_idx = (self.demo_idx + 1) % len(AUTO_SEQ)
            self.state = AUTO_SEQ[self.demo_idx]
            self.since = now
        return self.state

    def draw(self, fb, t, now):
        st = self.state
        cx, cy, r = 80, 64, 20
        ox = oy = gx = gy = 0
        eye, mouth = "open", "smile"
        handL = [cx - 24, cy + 16]
        handR = [cx + 24, cy + 16]

        if st == "IDLE":
            oy = round(time_sync(t / 800) * 1.5)
            if int(t / 3400) % 2 == 1 and t % 3400 < 140:
                eye = "blink"
            gx = round(time_sync(t / 1900) * 1.5)
        elif st == "THINKING":
            gy = -1; oy = -1
            handR = [cx + 13, cy - r + 1]
        elif st == "TOOL":
            oy = -1 if int(t / 130) % 2 else 0
            hy = cy + 17 + (1 if int(t / 130) % 2 else -1)
            handL = [cx - 9, hy]; handR = [cx + 9, hy]
            mouth = "open"; gy = 1
        elif st == "WAIT":
            oy = -round(abs(time_sync(t / 200, True)) * 3)
            handL = [cx - 26, cy - 2]; handR = [cx + 26, cy - 2]
            mouth = "open"
        elif st == "DONE":
            oy = -round(abs(time_sync(t / 220, True)) * 6)
            eye = "happy"
            handL = [cx - 28, cy - 6]; handR = [cx + 28, cy - 6]
        elif st == "ERROR":
            ox = round(time_sync(t / 45) * 2)
            eye = "xx"; mouth = "open"
        elif st == "SLEEP":
            oy = round(time_sync(t / 1200))
            eye = "closed"; mouth = "flat"

        # 影子
        jf = max(0.0, -oy) / 6
        sh_r = max(1, int(24 * (1 - .3 * jf)))
        fb.fell(cx + ox, 96, sh_r, max(1, int(4 * (1 - .3 * jf))), (12, 8, 20))

        # 脚
        fb.frect(cx + ox - 9, cy + oy + 22, 6, 3, DARK)
        fb.frect(cx + ox + 3, cy + oy + 22, 6, 3, DARK)

        # 身体
        for dx, dy, rr in [(-7, 3, r + 1), (7, 3, r + 1), (0, -4, r + 1)]:
            fb.fcirc(cx + ox + dx, cy + oy + dy, rr, OUT)
        for dx, dy, rr in [(-7, 3, r), (7, 3, r), (0, -4, r)]:
            fb.fcirc(cx + ox + dx, cy + oy + dy, rr, BODY)
        # 顶部凹槽
        fb.fell(cx + ox, cy + oy - r + 2, 6, 3, OUT)
        # 平涂高光 + 底部暗面
        fb.fell(cx + ox - 10, cy + oy - 10, 4, 6, HILI)
        fb.frect(cx + ox - 6, cy + oy - 16, 2, 2, WHITE)
        fb.pset(cx + ox - 4, cy + oy - 18, WHITE)
        fb.fell(cx + ox + 6, cy + oy + 16, 12, 5, SHADE)

        # 果柄 + 叶
        fb.frect(cx + ox - 1, cy + oy - r - 6, 2, 7, STK)
        ldx = round(time_sync(t / 500) * 1.2)
        fb.frect(cx + ox + 1 + ldx, cy + oy - r - 7, 4, 2, LEAF)
        fb.frect(cx + ox + 2 + ldx, cy + oy - r - 9, 3, 2, LEAF)
        fb.frect(cx + ox + 3 + ldx, cy + oy - r - 8, 2, 1, LEAFD)

        # 腮红
        fb.frect(cx + ox - 13, cy + oy + 6, 3, 2, BLUSH)
        fb.frect(cx + ox + 10, cy + oy + 6, 3, 2, BLUSH)

        # 手臂 + 手
        fb.line(cx + ox - 16, cy + oy + 10, handL[0] + ox, handL[1] + oy, SHD)
        fb.line(cx + ox + 16, cy + oy + 10, handR[0] + ox, handR[1] + oy, SHD)
        fb.fcirc(handL[0] + ox, handL[1] + oy, 3, BODY)
        fb.fcirc(handR[0] + ox, handR[1] + oy, 3, BODY)

        # 眼睛（二次元大眼）
        eyY = cy + oy - 6
        def eye_at(x):
            if eye == "open":
                ggx = int(gx); ggy = int(gy)
                fb.frect(x, eyY, 4, 5, WHITE)
                fb.frect(x + ggx, eyY + 1 + ggy, 3, 4, EYED)
                fb.pset(x + ggx, eyY + 1 + ggy, WHITE)           # 眼神光
                fb.frect(x, eyY, 4, 1, LASH)                     # 上睫毛
            elif eye in ("blink", "closed"):
                fb.frect(x, eyY + 2, 4, 1, (48, 24, 32))
            elif eye == "happy":
                fb.pset(x, eyY, WHITE); fb.pset(x + 3, eyY, WHITE)
                fb.pset(x + 1, eyY + 1, WHITE); fb.pset(x + 2, eyY + 1, WHITE)
                fb.frect(x + 1, eyY + 2, 2, 1, WHITE)
            else:  # xx
                fb.pset(x, eyY, WHITE); fb.pset(x + 2, eyY, WHITE)
                fb.pset(x + 1, eyY + 1, WHITE)
                fb.pset(x, eyY + 2, WHITE); fb.pset(x + 2, eyY + 2, WHITE)
        eye_at(cx + ox - 9)
        eye_at(cx + ox + 5)

        # 嘴
        mY, mX = cy + oy + 5, cx + ox
        if mouth == "smile":
            fb.pset(mX - 2, mY, MOUTH); fb.pset(mX + 2, mY, MOUTH)
            fb.frect(mX - 1, mY + 1, 3, 1, MOUTH)
        elif mouth == "open":
            fb.frect(mX - 1, mY, 3, 2, MOUTH)
            fb.frect(mX - 1, mY + 2, 3, 1, TONGUE)
        else:
            fb.frect(mX - 2, mY, 5, 1, MOUTH)

        # ---- 状态道具 ----
        if st == "THINKING":
            ph = int(t / 420) % 4
            fb.pset(cx + 16, cy + 2, (200, 205, 220))
            fb.frect(cx + 19, cy - 2, 2, 2, (200, 205, 220))
            fb.frect(cx + 23, cy - 14, 13, 9, (46, 52, 70))
            for i in range(3):
                fb.frect(cx + 26 + i * 3, cy - 11, 2, 2,
                         YELLOW if i < ph else (110, 116, 140))
        elif st == "TOOL":
            fb.frect(cx - 11, cy + 20, 22, 6, LAPTOP)
            fb.frect(cx - 9, cy + 15, 18, 5, SCREEN)
            sc = int(t / 3) % 5
            fb.frect(cx - 8, cy + 15 + sc, 5, 1, WHITE)
            fb.frect(cx - 1, cy + 15 + (sc + 2) % 5, 7, 1, WHITE)
            for i in range(2):
                fb.pset(cx - 8 + int(t / 40 + i * 23) % 16, cy + 11,
                        YELLOW if (i + int(t / 90)) % 2 else WHITE)
        elif st == "WAIT":
            bob = round(time_sync(t / 280) * 2)
            fb.frect(cx - 9, cy - r - 16 + bob, 18, 13, (240, 243, 250))
            fb.frect(cx - 2, cy - r - 4 + bob, 4, 3, (240, 243, 250))
            fb.glyph("?", cx - 2, cy - r - 14 + bob, QCOL)
        elif st == "DONE":
            cols = [RED, YELLOW, GREEN, CYAN, PINKC]
            rng = random.Random(int(t / 50) + 7)
            for _ in range(12):
                fb.frect(rng.random() * 160, rng.random() * 70, 2, 1,
                         cols[int(rng.random() * 5) % 5])
            fb.frect(52, 6, 56, 10, (20, 26, 20))
            fb.glyph("Z", 66, 9, GREEN); fb.glyph("+", 76, 9, GREEN)
            fb.glyph("+", 84, 9, GREEN); fb.glyph("Z", 92, 9, GREEN)
        elif st == "ERROR":
            fl = .12 + .10 * time_sync(t / 120)
            fb.frect(0, 0, W, 30, (int(60 + 60 * fl), 8, 12))
            fb.glyph("!", cx - 6, cy - r - 14, RED)
            fb.glyph("!", cx + 3, cy - r - 14, RED)
        elif st == "SLEEP":
            zp = int(t / 900) % 3
            fb.glyph("z", cx + 18, cy - r - 4 - zp * 5, BLUE)
            fb.glyph("Z", cx + 25, cy - r - 14 - zp * 4, BLUE)


def time_sync(w, abs_=False):
    import math
    v = math.sin(w)
    return abs(v) if abs_ else v


# ---------------- 冰箱贴窗口 ----------------
class Magnet:
    def __init__(self, args):
        self.args = args
        self.base = build_base()
        self.fb = FB()
        self.pet = Pet()
        self.last_event = 0.0
        self.ser = None
        self.inbox = []          # UDP 线程 -> 主循环（tkinter 非线程安全，走队列）

        if args.serial:
            try:
                import serial
                self.ser = serial.Serial(args.serial, 115200, timeout=0.1)
                print(f"[magnet] serial -> {args.serial}")
            except Exception as e:
                print(f"[magnet] serial 打开失败（继续纯显示）: {e}", file=sys.stderr)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.05)
        self.udp_enabled = False
        try:
            self.sock.bind(("127.0.0.1", 9911))
            self.udp_enabled = True
        except OSError as e:
            self.sock.close()
            print(f"[magnet] UDP 9911 被占用，当前仅运行本地演示；如需接收 Claude Code 事件，请先关闭 vibe_pet_host.py: {e}", file=sys.stderr)

        self.root = tk.Tk()
        self.root.title("vibe pet 冰箱贴")
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        self.root.geometry(f"+{sw - 360}+{sh - 340}")

        self.img = ImageTk.PhotoImage(Image.new("RGB", (W * SC, H * SC), "black"))
        self.canvas = tk.Canvas(self.root, width=W * SC, height=H * SC,
                                highlightthickness=2, highlightbackground="#30363d",
                                bd=0)
        self.canvas.create_image(0, 0, image=self.img, anchor="nw")
        self.canvas.pack()

        # 拖动
        self._dx = self._dy = 0
        self.canvas.bind("<Button-1>", self._press)
        self.canvas.bind("<B1-Motion>", self._drag)
        # 右键菜单
        self.menu = tk.Menu(self.root, tearoff=0)
        self.demo_var = tk.BooleanVar(value=True)
        self.top_var = tk.BooleanVar(value=True)
        self.menu.add_checkbutton(label="自动演示（无事件时轮播）", variable=self.demo_var)
        self.menu.add_checkbutton(label="窗口置顶", variable=self.top_var,
                                  command=lambda: self.root.attributes(
                                      "-topmost", self.top_var.get()))
        self.menu.add_separator()
        self.menu.add_command(label="退出", command=self.root.destroy)
        self.canvas.bind("<Button-3>", lambda e: self.menu.tk_popup(e.x_root, e.y_root))

        self._upd()
        if self.udp_enabled:
            threading.Thread(target=self._udp_loop, daemon=True).start()

    def _press(self, e):
        self._dx = e.x; self._dy = e.y

    def _drag(self, e):
        self.root.geometry(f"+{e.x_root - self._dx}+{e.y_root - self._dy}")

    def _udp_loop(self):
        if not self.udp_enabled:
            return
        while True:
            try:
                data, _ = self.sock.recvfrom(256)
            except socket.timeout:
                continue
            except OSError:
                return
            # hook_event.py may send state and time in the same UDP datagram.
            for line in data.decode("ascii", "ignore").splitlines():
                line = line.strip()
                if line.startswith("S:") and line[2:] in STATES:
                    self.inbox.append(line[2:])

    def _on_event(self, state):
        self.pet.set_state(state, real=True)
        self.last_event = time.time()
        if self.ser:
            try:
                self.ser.write(f"S:{state}\n".encode("ascii"))
            except Exception:
                pass

    def _upd(self):
        try:
            self._frame()
        except Exception:
            import traceback; traceback.print_exc()
        self.root.after(33, self._upd)     # 无论单帧是否出错，循环不中断

    def _frame(self):
        now = time.time()
        while self.inbox:                     # 处理真实事件（可能连发多条，取最后）
            self._on_event(self.inbox.pop(0))
        t = (now - self.pet.since) * 1000
        # 无真实事件且开启演示时进入轮播
        if self.pet.demo and self.demo_var.get() and now - self.last_event > 6:
            self.pet.auto_state(now)
        self.fb.clear_copy(self.base)
        # 星星闪烁
        for i, (sx, sy) in enumerate(STARS):
            tw = .5 + .5 * time_sync(t / 400 + i * 1.9)
            if tw > .55:
                self.fb.pset(sx, sy, STARC)
            elif tw > .2:
                self.fb.pset(sx, sy, STARP)
        self.pet.draw(self.fb, t, now)
        # 联动指示点（右上角）：收到真实事件后 2 秒内为绿色
        on = now < self.pet.link_flash
        self.fb.frect(W - 6, 2, 3, 3, GREEN if on else (90, 96, 110))

        im = Image.frombytes("RGB", (W, H), bytes(self.fb.buf))
        im = im.resize((W * SC, H * SC), Image.NEAREST)
        self.img.paste(im)

    def run(self):
        self.root.mainloop()


def main():
    ap = argparse.ArgumentParser(description="像素小苹果冰箱贴")
    ap.add_argument("--serial", default=None, help="可选，转发状态到 Wio Terminal 串口")
    args = ap.parse_args()
    Magnet(args).run()


if __name__ == "__main__":
    main()
