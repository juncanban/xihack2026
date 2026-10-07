"""Verify buffered rendering on the real LCD without editing saved records.

Run with PlatformIO's Python (pyserial), after building/uploading the firmware.
Requires the fresh pre-flash baseline in artifacts/hardware_flicker.
"""
import json
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

import device_console as dc

OUT = Path(__file__).resolve().parents[1] / "artifacts" / "hardware_flicker"
OUT.mkdir(parents=True, exist_ok=True)
dc.OUT = OUT
checks = []


def check(name, **details):
    checks.append({"name": name, **details})
    print("PASS " + name + " " + json.dumps(details, ensure_ascii=False), flush=True)


def stats(d):
    result = json.loads(d.command("V:STATS", "V:")[2:])
    assert result["ready"] == 1, result
    return result


def key(d, button, screen=None, frames=1):
    before = stats(d)
    info = d.key(button, screen)
    after = stats(d)
    if frames is not None:
        assert after["frames"] - before["frames"] == frames, (button, before, after, info)
    assert info["physicalKeys"] == 0, "Physical key pressed during automated test"
    return info


def idle(d, name, screen, seconds=5):
    before = stats(d)
    time.sleep(seconds)
    after = stats(d)
    info = d.info()
    assert info["screen"] == screen and info["physicalKeys"] == 0, info
    assert before["frames"] == after["frames"], (name, before, after)
    check(name, seconds=seconds, extra_frames=0)


def unchanged(d, baseline):
    current = d.info()
    for field in ("records", "sequence", "farmCrc", "batch",
                  "fridgeDate", "fridgeSequence", "fridgeUndo", "foods"):
        assert current[field] == baseline["ui"][field], (field, baseline["ui"][field], current[field])
    saved = d.command("Q", timeout=1.5)
    # Q includes each pet slot's sequence and CRC; byte-for-byte output equality
    # proves that these files have not been changed by navigation/timer testing.
    assert saved == baseline["save"], (saved, baseline["save"])
    check("user_data_preserved", foods=current["foods"], farm_crc=current["farmCrc"])
    return current


def finish_current_day(d, baseline):
    # Restore the clock only when it is still the stored fridge day. Never let
    # a later rerun reset daily counters as part of a display test. No UI keys
    # are sent here: after core verification the user can resume using them.
    today = int(datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d"))
    if today != baseline["ui"]["fridgeDate"]:
        return {"clock_synced": False, "reason": "stored food day differs from today"}
    d.sync()
    current = unchanged(d, baseline)
    return {"clock_synced": True, "final": current, "render": stats(d)}


def main():
    baseline = json.loads((OUT / "baseline_before_flash.json").read_text(encoding="utf-8"))
    d = dc.Device("COM8")
    try:
        info = d.info()
        assert info["fw"] == "fridge-ui-v2-buffered", info
        assert not info["running"] and info["physicalKeys"] == 0, info
        assert info["screen"] == 0, info
        unchanged(d, baseline)
        memory_before = stats(d)
        for _ in range(4):
            assert d.command("Q", timeout=1.5) == baseline["save"]
        memory_after = stats(d)
        assert memory_after["heapGap"] == memory_before["heapGap"] and memory_after["heapGap"] > 8192, (memory_before, memory_after)
        assert memory_after["heapUsed"] == memory_before["heapUsed"], (memory_before, memory_after)
        check("repeated_save_diagnostics_memory", heap_gap=memory_after["heapGap"],
              heap_used=memory_after["heapUsed"], repeats=4)
        check("frame_arena_ready", **stats(d))
        d.capture("01_pet")

        key(d, "FARM", 4)
        d.capture("02_today")
        idle(d, "static_today", 4)
        key(d, "RIGHT", 5)
        key(d, "UP", 5, frames=0)
        key(d, "RIGHT", 6)
        d.capture("03_tree")
        key(d, "OK", 7)
        key(d, "RIGHT", 7, frames=0)
        # A fixed low ADC is temporary and awards no growth points.
        d.command("L:45", "OK:FORCE")
        time.sleep(1.2)
        key(d, "OK", 1)
        d.capture("04_status")
        idle(d, "unchanged_status", 1)
        before = stats(d)
        d.command("L:60", "OK:FORCE")
        time.sleep(2.2)
        after = stats(d)
        assert after["frames"] - before["frames"] == 1, (before, after)
        check("status_value_change", extra_frames=1)
        idle(d, "status_after_one_change", 1, 3)
        key(d, "BACK", 7)
        key(d, "DOWN", 7)
        key(d, "OK", 3)
        d.capture("05_adoption")
        key(d, "BACK", 7)
        key(d, "DOWN", 7)
        key(d, "OK", 2)
        d.capture("06_memorial")
        key(d, "UP", 2, frames=0)
        key(d, "BACK", 7)
        key(d, "BACK", 6)
        key(d, "RIGHT", 9)
        d.capture("07_fridge")
        idle(d, "static_fridge", 9, 3)
        key(d, "OK", 17)
        d.capture("08_fridge_item")
        key(d, "OK", 18)
        d.capture("09_fridge_edit")
        key(d, "LEFT", 18, frames=0)  # already at the minimum of 1
        key(d, "BACK", 17)  # cancel draft; never submit a saved-food edit
        key(d, "BACK", 9)
        key(d, "RIGHT", 19)
        d.capture("10_fridge_today")

        # Repeat full page/pet transitions to exercise the shared arena and
        # redraw route; this loop does not change any saved records.
        for _ in range(12):
            key(d, "FARM", 4)
            key(d, "LEFT", 0)
            key(d, "OK", 6)
            key(d, "RIGHT", 9)
        check("repeated_navigation", transitions=48, frames_per_action=1)
        key(d, "TIMER", 10)
        while d.info()["timerDuration"] > 60000:
            key(d, "DOWN", 10)
        d.capture("11_timer_idle")
        idle(d, "timer_idle", 10, 5)
        start = key(d, "OK", 10)
        first = stats(d)
        time.sleep(5.2)
        running = d.info()
        last = stats(d)
        delta = last["frames"] - first["frames"]
        assert 4 <= delta <= 6 and running["running"] == 1, (first, last, running)
        check("timer_running", observed_seconds=5.2, extra_frames=delta,
              frame_us=last["frameUs"], remaining_ms=running["timerMs"])
        key(d, "OK", 10)  # pause
        d.capture("12_timer_paused")
        idle(d, "timer_paused", 10, 5)
        key(d, "OK", 10)
        key(d, "TIMER", 9)  # continues in the background
        check("background_timer_started", remaining_ms=d.info()["timerMs"])
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            time.sleep(1)
            info = d.info()
            assert info["physicalKeys"] == 0, info
            if info["screen"] == 16:
                break
        assert info["screen"] == 16 and info["finished"] == 1, info
        d.capture("13_timer_alert")
        idle(d, "completed_alert_static", 16, 3)
        key(d, "OK", 9)
        key(d, "TIMER", 10)
        idle(d, "completed_timer_static", 10, 5)
        key(d, "LEFT", 10)  # restore one minute, paused, as at baseline
        key(d, "TIMER", 9)
        d.command("L:", "OK:LIVE")
        unchanged(d, baseline)
        before_reboot = d.info()
        d.reboot()
        assert d.info()["screen"] == 0
        unchanged(d, baseline)
        check("reboot_and_arena_rebind", **stats(d))
        key(d, "TIMER", 10)
        while d.info()["timerDuration"] > baseline["ui"]["timerDuration"]:
            key(d, "DOWN", 10)
        while d.info()["timerDuration"] < baseline["ui"]["timerDuration"]:
            key(d, "UP", 10)
        key(d, "TIMER", 0)
        d.capture("14_pet_restored")
        final = unchanged(d, baseline)
        handoff = finish_current_day(d, baseline)
        result = {"checks": checks, "before_reboot": before_reboot,
                  "final": handoff.get("final", final), "render": stats(d), "handoff": handoff}
        (OUT / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print("ALL HARDWARE CHECKS PASSED", flush=True)
    finally:
        # Always release the temporary ADC override, including failed assertions.
        try:
            d.command("L:", "OK:LIVE")
        finally:
            d.close()


if __name__ == "__main__":
    main()
