#!/usr/bin/env python3
"""模拟一轮 AI 代理工作状态，不依赖 Claude Code。

用于演示/调试：先把 Wio Terminal 刷好固件并运行 vibe_pet_host.py，
再运行本脚本，就能看到小苹果按顺序表演各种状态。

    python simulate.py [--delay 3]
"""
import argparse
import socket
import time

UDP = ("127.0.0.1", 9911)

# 一轮典型工作流：用户提问 -> 思考 -> 干活 -> 等批准 -> 干活 -> 完成 -> 偶尔出错
SCRIPT = [
    "S:THINKING",
    "S:TOOL",
    "S:WAIT",
    "S:THINKING",
    "S:TOOL",
    "T:128",
    "S:TOOL",
    "S:DONE",
    "S:ERROR",
    "S:DONE",
]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--delay", type=float, default=3.0)
    args = ap.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        while True:
            for line in SCRIPT:
                print(f"-> {line}")
                s.sendto((line + "\n").encode("ascii"), UDP)
                time.sleep(args.delay)


if __name__ == "__main__":
    main()
