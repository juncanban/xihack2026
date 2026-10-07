#!/usr/bin/env python3
"""Claude Code Hook -> UDP 事件转发器。

Claude Code 每次触发 Hook 时以子进程调用本脚本，事件 JSON 从 stdin 传入。
脚本映射成两行协议一次发走：
    S:<STATE>\nD:<epoch秒>\n
（D: 是时间同步，Wio Terminal 无电池时钟，靠每次事件顺带补时；固件幂等。）

UDP 打给本机 vibe_pet_host.py（默认 127.0.0.1:9911），毫秒级返回，不阻塞 Claude。
只依赖标准库。宿主机没开守护进程时静默丢弃，不影响使用。
"""
import json
import socket
import sys
import time

UDP_IP = "127.0.0.1"
UDP_PORT = 9911

# Claude Code Hook 事件 -> 宠物状态
EVENT_STATE = {
    "UserPromptSubmit": "THINKING",   # 收到用户消息，开始思考
    "PreToolUse":       "TOOL",       # 正要调用工具
    "PostToolUse":      "THINKING",   # 工具返回，继续思考
    "Notification":     "WAIT",       # 需要用户批准/确认
    "Stop":             "DONE",       # 本轮任务完成
    "SessionStart":     "IDLE",       # 会话启动（也是断电后补时的第一站）
    "SessionEnd":       "IDLE",
}


def main() -> None:
    name = ""
    try:
        payload = json.load(sys.stdin)
        name = payload.get("hook_event_name", "")
    except Exception:
        pass

    state = EVENT_STATE.get(name)
    lines = []
    if state:
        lines.append(f"S:{state}")
    lines.append(f"D:{int(time.time())}")   # 无 state 的事件也补时，幂等无害
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.sendto(("\n".join(lines) + "\n").encode("ascii"), (UDP_IP, UDP_PORT))
    except OSError:
        pass  # 守护进程不在，静默忽略


if __name__ == "__main__":
    main()
