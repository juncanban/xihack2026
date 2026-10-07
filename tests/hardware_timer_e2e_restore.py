"""Run a real one-minute board timer while restoring its original runtime preset."""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import device_console as console

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "full-rerun-20261004"
OUT.mkdir(parents=True, exist_ok=True)
console.OUT = OUT

device = console.Device("COM8")
before = None
baseline_keys = None
restore_needed = False
result = {"passed": False}


def guarded_key(name):
    info = device.info()
    if baseline_keys is not None and info["physicalKeys"] != baseline_keys:
        raise RuntimeError("Physical key detected; stopping automatic navigation")
    info = device.key(name)
    if baseline_keys is not None and info["physicalKeys"] != baseline_keys:
        raise RuntimeError("Physical key detected; stopping automatic navigation")
    return info


def restore_timer(original_ms):
    info = device.info()
    if info["physicalKeys"] != baseline_keys:
        return False
    if info["screen"] == 16:
        info = guarded_key("OK")
    if info["screen"] != 10:
        return False
    if info["running"] or info["finished"] or info["timerMs"] != info["timerDuration"]:
        info = guarded_key("LEFT")
        info = guarded_key("OK")
    while info["timerDuration"] < original_ms:
        info = guarded_key("UP")
    while info["timerDuration"] > original_ms:
        info = guarded_key("DOWN")
    if info["timerDuration"] != original_ms or info["timerMs"] != original_ms:
        return False
    info = guarded_key("LEFT")
    info = guarded_key("OK")
    if info["running"] or info["finished"] or info["timerDuration"] != original_ms or info["timerMs"] != original_ms:
        return False
    guarded_key("RIGHT")
    return guarded_key("FARM")["screen"] == 4


try:
    before = device.info()
    baseline_keys = before["physicalKeys"]
    assert baseline_keys == 0, "Physical key counter is nonzero; preserving current device state"
    assert before["screen"] == 4, "Expected today page; refusing unplanned navigation"
    assert not before["running"] and not before["finished"], "Timer is active; refusing to interrupt it"
    original_ms = before["timerDuration"]
    assert original_ms == before["timerMs"] and original_ms % 60000 == 0

    def state():
        info = device.info()
        assert info["physicalKeys"] == baseline_keys, "Physical key detected; stopping automatic navigation"
        return info

    def key(name):
        state()
        info = device.key(name)
        assert info["physicalKeys"] == baseline_keys, "Physical key detected; stopping automatic navigation"
        return info

    info = key("TIMER")
    assert info["screen"] == 10
    restore_needed = True
    for _ in range(original_ms // 60000 - 1):
        info = key("DOWN")
    assert info["timerDuration"] == 60000 and info["timerMs"] == 60000
    info = key("RIGHT")
    info = key("OK")
    assert info["running"] and not info["finished"]

    started = time.perf_counter()
    last_reported = 0
    while time.perf_counter() - started < 90:
        time.sleep(1)
        info = state()
        elapsed = int(time.perf_counter() - started)
        if elapsed // 15 > last_reported:
            last_reported = elapsed // 15
            print(f"TIMER_WAIT_SECONDS {elapsed}", flush=True)
        if info["finished"]:
            break
    elapsed = time.perf_counter() - started
    assert info["screen"] == 16 and info["finished"] and not info["running"] and info["timerMs"] == 0, info
    device.capture("timer-v32-e2e-alert")
    state()

    info = key("OK")
    assert info["screen"] == 10 and info["finished"]
    info = key("LEFT")
    assert info["timerAction"] == 0
    info = key("OK")
    assert not info["running"] and not info["finished"] and info["timerMs"] == 60000
    for _ in range(original_ms // 60000 - 1):
        info = key("UP")
    assert info["timerDuration"] == original_ms
    info = key("OK")
    assert info["timerMs"] == original_ms and not info["running"] and not info["finished"]
    info = key("RIGHT")
    assert info["timerAction"] == 1
    info = key("FARM")
    assert info["screen"] == 4

    after = state()
    protected = ("farmCrc", "records", "sequence", "batch", "light", "foods", "fridgeDate", "fridgeSequence", "fridgeUndo")
    for field in protected:
        assert after[field] == before[field], field
    assert after["timerDuration"] == after["timerMs"] == original_ms
    assert not after["running"] and not after["finished"]
    result.update({
        "passed": True,
        "firmware": before["fw"],
        "timer_duration_ms": original_ms,
        "test_duration_ms": 60000,
        "elapsed_seconds": round(elapsed, 2),
        "alert_screen": 16,
        "restored": True,
        "physical_keys": after["physicalKeys"],
        "business_fields_unchanged": True,
    })
    print(json.dumps(result, ensure_ascii=False), flush=True)
except Exception as error:
    result["error"] = str(error)
    if restore_needed and before is not None:
        try:
            current = device.info()
            if current["physicalKeys"] == baseline_keys:
                result["restored_after_error"] = restore_timer(before["timerDuration"])
        except Exception as restore_error:
            result["restore_error"] = str(restore_error)
    raise
finally:
    device.close()
    (OUT / "timer-v32-e2e-summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
