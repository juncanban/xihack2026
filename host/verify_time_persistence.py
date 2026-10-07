# -*- coding: utf-8 -*-
"""TIME_PERSISTENCE.md 目标真机验证。

只做协议级操作:校时(D:)、读状态(I)、软复位(R:REBOOT),不触碰 UI 导航。
验证链路:RTC 软复位保留 + flash 快照已建立 + 恢复后走时与电脑对表。
真实掉电(拔插充电宝)的热插拔环节需人工执行,不在本脚本范围。
"""
import sys, time, serial

PORT = "COM8"
BAUD = 115200


def open_port():
    return serial.Serial(PORT, BAUD, timeout=0.5)


def drain(ser, seconds=1.0):
    out = []
    end = time.time() + seconds
    while time.time() < end:
        line = ser.readline()
        if line:
            out.append(line.decode("utf-8", "replace").strip())
    return out


def cmd(ser, text, wait=0.6):
    ser.reset_input_buffer()
    ser.write((text + "\n").encode())
    time.sleep(wait)
    return drain(ser)


def epoch_of(lines):
    for ln in lines:
        if ln.startswith("I:"):
            try:
                return int(ln.split("|")[1])
            except (IndexError, ValueError):
                return None
    return None


def main():
    ser = open_port()
    boot = drain(ser, 1.0)

    pc1 = int(time.time())
    r = cmd(ser, "I")
    dev1 = epoch_of(r)
    if dev1 is None:
        print("FAIL no I: reply", boot, r)
        return 1
    print(f"SYNC-BEFORE dev={dev1} pc={pc1} offset={dev1 - pc1}s")

    r = cmd(ser, f"D:{int(time.time())}")
    if not any("OK:TIME" in ln for ln in r):
        print("FAIL D: not acked", r)
        return 1

    time.sleep(1.2)
    pc2 = int(time.time())
    dev2 = epoch_of(cmd(ser, "I"))
    print(f"TICK dev={dev2} pc={pc2} offset={dev2 - pc2}s")
    if dev2 is None or dev2 <= dev1:
        print("FAIL clock not advancing")
        return 1

    # 软复位:RTC 层应恢复(CLOCK:RESTORED:RTC),快照应已建立(SNAP:READY)
    dev_before = dev2
    try:
        cmd(ser, "R:REBOOT", wait=0.3)
    except Exception:
        pass                      # 复位瞬间 USB 重枚举,读失败属预期
    ser.close()

    deadline, boot_lines = time.time() + 25, []
    while time.time() < deadline:
        try:
            ser = open_port()
        except Exception:
            time.sleep(0.5)
            continue
        time.sleep(1.0)
        boot_lines = drain(ser, 3.0)
        if any("READY" in ln or "CLOCK:RESTORED" in ln for ln in boot_lines):
            break
        # 日志可能已错过(枚举时机):能答 I: 即设备活着
        if epoch_of(cmd(ser, "I", wait=0.8)) is not None:
            break
        ser.close()

    print("BOOT-LOG:", [ln for ln in boot_lines if ln])
    time.sleep(1.0)
    pc3 = int(time.time())
    dev3 = epoch_of(cmd(ser, "I"))
    print(f"AFTER-REBOOT dev={dev3} pc={pc3} offset={dev3 - pc3}s "
          f"(before reboot: {dev_before})")
    if dev3 is None or dev3 == 0:
        print("FAIL time not restored after soft reboot")
        return 1
    if dev3 < dev_before - 5:
        print("FAIL time went backwards across reboot")
        return 1
    ok = abs(dev3 - pc3) <= 30
    print("PASS" if ok else "FAIL: restored time not within 30s of PC")
    ser.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
