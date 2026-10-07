#!/usr/bin/env python3
"""vibe_pet_host —— 小苹果桌面宠物宿主机守护进程。

架构：
    Claude Code hooks -> hook_event.py --UDP--> 本守护进程 --USB 串口--> Wio Terminal

监听 127.0.0.1:9911 上的行协议（S:STATE / T:n / P），原样转发到
Wio Terminal 的串口。宿主机静默时由固件侧 watchdog 自动降级
（2 分钟 -> IDLE，10 分钟 -> SLEEP），所以守护进程不需要发心跳。

用法：
    python vibe_pet_host.py                # 默认 COM8（与 wio 工程烧录口一致）
    python vibe_pet_host.py --port COM5
    python vibe_pet_host.py --dry-run      # 不开串口，只打印收到的事件

依赖：pyserial（--dry-run 模式不需要）
"""
import argparse
import socket
import sys
import threading
import time
from pathlib import Path

UDP_IP = "127.0.0.1"
UDP_PORT = 9911
AUDIO_LOG = Path(__file__).resolve().parents[1] / "artifacts" / "logs" / "audio.log"


def log_audio(line: str) -> None:
    if not line.startswith("AUDIO:"):
        return
    AUDIO_LOG.parent.mkdir(parents=True, exist_ok=True)
    with AUDIO_LOG.open("a", encoding="utf-8") as stream:
        stream.write(time.strftime("%Y-%m-%d %H:%M:%S ") + line + "\n")


def serial_writer(port: str, baud: int, rx_q: "list[str]", lock: threading.Lock):
    import serial
    while True:
        try:
            serial_session(port, baud, rx_q, lock)
        except (serial.SerialException, OSError) as exc:
            print(f"[host] serial unavailable: {exc}; retry in 2s", flush=True)
            time.sleep(2)


def serial_session(port: str, baud: int, rx_q: "list[str]", lock: threading.Lock):
    import serial
    with serial.Serial(port, baud, timeout=1, write_timeout=3) as ser:
        serve_serial(ser, rx_q, lock)


def serve_serial(ser, rx_q, lock):
    import serial  # pyserial

    print("[host] serial opened", flush=True)
    while True:
        if rx_q:
            with lock:
                line = rx_q.pop(0)
            ser.write(line.encode("ascii"))
            ser.flush()
            print(f"[host] -> {line.strip()}")
        # 顺手读固件回包（READY:/OK:/ERR:），保持缓冲不堵
        if ser.in_waiting:
            echo = ser.readline().decode("ascii", "ignore").strip()
            if echo:
                print(f"[host] <- {echo}")
                log_audio(echo)
                if echo == "Q:TIME":
                    # 设备只提出请求；宿主机提供真实当前时间，固件负责设置时钟。
                    reply = f"D:{int(time.time())}\n"
                    ser.write(reply.encode("ascii"))
                    ser.flush()
                    print(f"[host] -> {reply.strip()}")
        threading.Event().wait(0.02)


def main() -> int:
    ap = argparse.ArgumentParser(description="vibe pet host daemon")
    ap.add_argument("--port", default="COM8", help="Wio Terminal 串口（默认 COM8）")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dry-run", action="store_true", help="只打印事件，不开串口")
    ap.add_argument("--serial-only", action="store_true", help="仅监听串口校时请求，不占用 UDP 9911")
    args = ap.parse_args()

    rx_q: "list[str]" = []
    lock = threading.Lock()

    if args.serial_only and not args.dry_run:
        serial_writer(args.port, args.baud, rx_q, lock)
        return 0

    if not args.dry_run:
        try:
            threading.Thread(
                target=serial_writer, args=(args.port, args.baud, rx_q, lock),
                daemon=True,
            ).start()
        except Exception as e:
            print(f"[host] serial open failed: {e}\n"
                  f"[host] fallback to dry-run mode", file=sys.stderr)
            args.dry_run = True
    else:
        print("[host] dry-run mode")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"[host] listening on udp://{UDP_IP}:{UDP_PORT}")

    while True:
        data, _ = sock.recvfrom(256)
        line = data.decode("ascii", "ignore").strip()
        if not line:
            continue
        if args.dry_run:
            print(f"[host] (dry) {line}")
            continue
        with lock:
            rx_q.append(line + "\n")


if __name__ == "__main__":
    sys.exit(main())
