"""Final hardware checks after the watchdog/status-bar fix; preserves user records."""
import json
import time
from datetime import datetime, timezone, timedelta
from device_console import Device, OUT

before = json.loads((OUT / "before_final_flash.json").read_text(encoding="utf-8"))
d = Device()
passed = []
try:
    def check(name):
        passed.append(name)
        print("PASS " + name, flush=True)

    current = d.info()
    assert current["fw"] == "orchard-ui-v3-hw2"
    for key in ["records", "sequence", "batch", "reminders"]:
        assert current[key] == before["ui"][key], (key, current, before)
    check("current user farm records and settings survive final firmware upload")
    d.sync()
    for value, state in enumerate(["IDLE", "THINKING", "TOOL", "WAIT", "DONE", "ERROR", "SLEEP"]):
        d.command("S:" + state, "OK:" + state)
        time.sleep(0.12)
        assert d.info()["petState"] == value, state
    d.command("S:THINKING", "OK:THINKING")
    assert d.key("OK", 6)["petState"] == 1
    d.capture("02_tree")
    assert d.info()["petState"] == 1, "Long screen readback incorrectly tripped idle watchdog"
    d.key("OK", 7)
    d.key("OK", 1); d.key("BACK", 7)
    d.key("DOWN"); d.key("OK", 3); d.key("BACK", 7)
    d.key("DOWN"); d.key("OK", 2); d.key("BACK", 7)
    d.key("FARM", 4); d.key("BACK", 0)
    d.command("S:IDLE", "OK:IDLE")
    d.capture("01_pet")
    assert d.info()["petState"] == 0
    check("all serial states, status-bar redraw, archive navigation and watchdog timing after slow operations")

    d.key("TIMER", 10)
    while d.info()["timerDuration"] > 60000:
        d.key("DOWN", 10)
    start = d.key("OK", 10)
    assert start["running"] == 1
    d.capture("12_timer_running")
    d.key("TIMER", 0)
    keys_at_start = d.info()["physicalKeys"]
    limit = time.monotonic() + 70
    while time.monotonic() < limit:
        state = d.info()
        assert state["physicalKeys"] == keys_at_start, "Manual input detected; this unattended run cannot be certified"
        if state["screen"] == 16:
            break
        assert state["screen"] == 0
        print("Background timer:", state["timerMs"], "ms", flush=True)
        time.sleep(10)
    else:
        raise AssertionError("Countdown did not expire")
    assert state["timerMs"] == 0 and state["finished"] == 1 and state["running"] == 0
    d.capture("13_timer_alert")
    d.key("OK", 0)
    check("unattended real one-minute countdown on pet screen, expiry alert, return to pet")

    d.reboot(); d.sync()
    d.command("S:IDLE", "OK:IDLE")
    after = d.info()
    for key in ["records", "sequence", "batch", "reminders"]:
        assert after[key] == before["ui"][key], (key, after, before)
    assert after["screen"] == 0 and after["petState"] == 0
    identity = d.command("I", "I:")
    old_parts, new_parts = before["identity"].split("|"), identity.split("|")
    for part in [0,5,8]:
        assert old_parts[part] == new_parts[part], (old_parts, new_parts)
    q = d.command("Q", timeout=1.5)
    assert len([line for line in q if "crcOK=1" in line]) == 2
    d.capture("14_pet_final")
    check("final reboot, original pet name/adoption/stage/milestones and both pet CRCs; user records retained")
    report = {
        "completed_at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
        "firmware": after["fw"], "passed": passed,
        "before_final_flash": before, "after": after, "identity": identity, "storage": q,
        "earlier_hardware_checks": [
            "Batch setting changed 10 to 11, software reboot reloaded 11, restored 10, second reboot reloaded 10",
            "Calendar, month/day selection, task browsing and unsaved reminder draft navigation verified",
            "First countdown run had user-confirmed manual navigation; timer still expired; final unattended run repeated",
        ],
        "limits": [
            "GPIO physical switch positions and tactile feedback not certified by remote key injection",
            "LCD memory readback is actual display data, not a camera image",
            "Software reboot recovery tested; physical power removal not performed",
            "No scheduled reminder was enabled or task completed by the automated test",
            "User created one farm record while trying buttons; it was preserved",
        ],
    }
    (OUT / "verification_result.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    print("FINAL HARDWARE VERIFICATION PASSED", flush=True)
finally:
    d.close()
