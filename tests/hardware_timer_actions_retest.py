"""Short, non-persistent timer control check for the currently attached board."""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import device_console as console

OUT = Path(__file__).resolve().parents[1] / "artifacts" / "requested-flow"
OUT.mkdir(parents=True, exist_ok=True)
console.OUT = OUT

device = console.Device("COM8")
before = None
baseline_keys = None
try:
    before = device.info()
    assert before["screen"] == 4, "Expected today page; refusing unplanned navigation"
    assert not before["running"] and not before["finished"], "Timer is active; refusing to interrupt it"
    assert before["timerMs"] == before["timerDuration"], "Timer is not at its full preset; refusing to overwrite it"
    baseline_keys = before["physicalKeys"]

    def state():
        info = device.info()
        assert info["physicalKeys"] == baseline_keys, "Physical key detected; stopping automatic navigation"
        return info

    def key(name):
        state()
        info = device.key(name)
        assert info["physicalKeys"] == baseline_keys, "Physical key detected; stopping automatic navigation"
        return info

    protected = ("farmCrc", "sequence", "records", "foods", "fridgeDate", "fridgeSequence", "timerDuration")
    summary_fields = (
        "fw", "screen", "physicalKeys", "localDate", "selected", "timerMs",
        "timerDuration", "running", "finished",
    )
    summarize = lambda info: {field: info.get(field) for field in summary_fields}
    result = {"before": summarize(before), "control_steps": []}

    info = key("TIMER")
    assert info["screen"] == 10 and info["timerDuration"] == before["timerDuration"]
    info = key("LEFT")
    assert info["timerAction"] == 0 and not info["running"]
    info = key("RIGHT")
    assert info["timerAction"] == 1 and not info["running"]
    info = key("OK")
    assert info["running"]
    result["control_steps"].append("start")

    time.sleep(1.0)
    info = state()
    assert info["screen"] == 10 and info["running"] and not info["finished"]
    info = key("LEFT")
    assert info["running"] and info["timerAction"] == 0
    info = key("RIGHT")
    assert info["running"] and info["timerAction"] == 1
    info = key("OK")
    assert not info["running"] and 0 < info["timerMs"] < before["timerDuration"]
    result["control_steps"].append("pause")

    info = key("LEFT")
    assert info["timerAction"] == 0
    info = key("OK")
    assert not info["running"] and info["timerMs"] == before["timerDuration"]
    result["control_steps"].append("reset")
    info = key("RIGHT")
    assert info["timerAction"] == 1
    info = key("FARM")
    assert info["screen"] == before["screen"]

    after = state()
    for field in protected:
        assert after[field] == before[field], field
    result.update({
        "after": summarize(after),
        "business_fields_unchanged": True,
        "compared_business_fields": list(protected),
        "passed": True,
    })
    output = OUT / "hardware-timer-actions-rerun.json"
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({
        "passed": True,
        "firmware": before["fw"],
        "duration_ms": before["timerDuration"],
        "steps": result["control_steps"],
        "physicalKeys": after["physicalKeys"],
        "business_fields_unchanged": True,
        "evidence": str(output),
    }, ensure_ascii=False), flush=True)
except Exception:
    if before is not None and baseline_keys is not None:
        try:
            info = device.info()
            if info["physicalKeys"] == baseline_keys and info["screen"] == 10:
                if info["running"] or info["timerMs"] != before["timerDuration"]:
                    device.key("LEFT")
                    device.key("OK")
                device.key("FARM")
        except Exception:
            pass
    raise
finally:
    device.close()
