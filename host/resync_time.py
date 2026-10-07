# -*- coding: utf-8 -*-
"""一键重新校时:断电久了/时间不对时,插回电脑跑本脚本即可。

用法:
    python host/resync_time.py             # 默认 COM8
    python host/resync_time.py --port COM7

流程:读设备时间 -> 发 D:<电脑UTC秒> -> 复核 I: 偏差 <= 3 秒。
校时成功后固件会立刻把新时间写入 flash 快照,之后短断电照常自动恢复。
若报"拒绝访问",先关掉占用串口的 vibe_pet_host.py 守护进程再试。
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description="给苹苹重新校时(以电脑时间为准)")
    ap.add_argument("--port", default="COM8", help="串口号,默认 COM8")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, 115200, timeout=0.5)
    except serial.SerialException as e:
        print(f"FAIL 打不开 {args.port}: {e}")
        print("若提示拒绝访问,先关掉 vibe_pet_host.py;若无串口,检查 USB 线。")
        return 1

    def cmd(text, wait=0.6):
        ser.reset_input_buffer()
        ser.write((text + "\n").encode())
        time.sleep(wait)
        out = []
        while True:
            line = ser.readline()
            if not line:
                break
            out.append(line.decode("utf-8", "replace").strip())
        return out

    def epoch_of(lines):
        for ln in lines:
            if ln.startswith("I:"):
                try:
                    return int(ln.split("|")[1])
                except (IndexError, ValueError):
                    return None
        return None

    before = epoch_of(cmd("I"))
    if before is None:
        print("FAIL 设备无响应(检查是否插好、屏幕是否点亮)")
        ser.close()
        return 1
    if before == 0:
        print("校时前 设备未校时(等待电脑)")
    else:
        local = time.localtime(before + 8 * 3600)
        print(f"校时前 设备={before} "
              f"(显示 {time.strftime('%Y-%m-%d %H:%M:%S', local)}) "
              f"电脑={int(time.time())} 偏差={before - int(time.time())}s")

    r = cmd(f"D:{int(time.time())}")
    if not any("OK:TIME" in ln for ln in r):
        print("FAIL 设备未确认校时", r)
        ser.close()
        return 1

    time.sleep(1.2)
    after = epoch_of(cmd("I"))
    pc2 = int(time.time())
    if after is None:
        print("FAIL 校时后读不到设备时间")
        ser.close()
        return 1
    offset = after - pc2
    print(f"校时后 设备={after} 电脑={pc2} 偏差={offset}s")
    if abs(offset) <= 3:
        print("PASS 时间已同步,新时间已写入断电快照,现在可以拔去接充电宝")
        ser.close()
        return 0
    print("FAIL 偏差仍超 3 秒,请再跑一次;多次失败看 TIME_PERSISTENCE.md")
    ser.close()
    return 1


if __name__ == "__main__":
    sys.exit(main())
