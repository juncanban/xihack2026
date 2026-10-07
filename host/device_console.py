"""Local USB diagnostic helper; U is read-only, K uses the real UI dispatcher.

Use the PlatformIO Python (has pyserial). Screen captures are PPM, no Pillow needed.
"""
import json
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path
import serial

OUT = Path(__file__).resolve().parents[1] / "artifacts" / "hardware"
OUT.mkdir(parents=True, exist_ok=True)

class Device:
    def __init__(self, port="COM8"):
        self.port_name = port
        self.serial = None
        self.connect()

    def connect(self):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                self.serial = serial.Serial(self.port_name, 115200, timeout=0.2, write_timeout=3)
                time.sleep(0.8)
                self.serial.reset_input_buffer()
                return
            except serial.SerialException:
                time.sleep(0.3)
        raise RuntimeError("Device did not reconnect")

    def close(self):
        if self.serial:
            self.serial.close()

    def log(self, command, lines):
        entry = {"at": datetime.now(timezone(timedelta(hours=8))).isoformat(), "command": command, "reply": lines}
        with (OUT / "serial_log.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(entry, ensure_ascii=False) + "\n")

    def command(self, command, prefix=None, timeout=2):
        self.serial.write((command + "\n").encode("ascii"))
        self.serial.flush()
        lines = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.serial.readline().decode("utf-8", "replace").strip()
            if line:
                lines.append(line)
                if prefix and line.startswith(prefix):
                    self.log(command, lines)
                    return line
        self.log(command, lines)
        if prefix:
            raise AssertionError(f"{command}: missing {prefix}: {lines}")
        return lines

    def info(self):
        return json.loads(self.command("U", "U:")[2:])

    def key(self, key, screen=None):
        info = json.loads(self.command("K:" + key, "U:")[2:])
        if screen is not None:
            assert info["screen"] == screen, (key, screen, info)
        return info

    def sync(self):
        self.command("D:" + str(int(time.time())), "OK:TIME")
        time.sleep(1.1)

    def reboot(self):
        self.command("R:REBOOT", "OK:REBOOT")
        self.close()
        time.sleep(1.2)
        self.connect()
        self.command("P", "OK:PONG")

    def capture(self, name):
        self.command("V:SCREEN", "V:RGB888:320:240")
        expected = 320 * 240 * 3
        data = bytearray()
        deadline = time.monotonic() + 20
        while len(data) < expected and time.monotonic() < deadline:
            data.extend(self.serial.read(expected-len(data)))
        assert len(data) == expected, f"Short LCD capture: {len(data)}"
        marker = self.serial.readline().decode("ascii", "replace").strip()
        assert marker == "V:END", marker
        path = OUT / (name + ".ppm")
        path.write_bytes(b"P6\n320 240\n255\n" + data)
        unique = len({bytes(data[i:i+3]) for i in range(0, expected, 3)})
        assert unique > 2, f"LCD readback appears invalid: only {unique} colors"
        print(f"LCD captured: {name}, {unique} colors", flush=True)
        return path

if __name__ == "__main__":
    device = Device()
    try:
        print(json.dumps(device.info(), ensure_ascii=False, indent=2))
        device.capture("pet_live")
    finally:
        device.close()
