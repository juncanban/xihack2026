"""On-device regression using the same dispatcher as physical buttons.

Creates no farm tasks, does not rename/reset/evolve the pet. Temporarily adjusts
batch size, reboots to verify storage, restores it, and tests a real one-minute timer.
"""
import json
import re
import time
from datetime import datetime, timezone, timedelta
from device_console import Device, OUT

PET, STATUS, MEMORIAL, ADOPTION, TODAY, CALENDAR, TREE, ARCHIVE, CAPTURE, REMINDERS, TIMER, TASKS = range(12)
ALERT = 16

def main():
    d = Device()
    results = []
    baseline = None
    temporary_batch = False

    def passed(name):
        results.append(name)
        print("PASS " + name, flush=True)

    def pet_identity():
        line = d.command("I", "I:")
        fields = line.split("|")
        assert "FS1" in fields[-1], line
        return {"name": fields[0][2:], "stage": fields[5], "archive": fields[-1]}

    def go_pet():
        for _ in range(6):
            s = d.info()["screen"]
            if s == PET:
                return
            if s == ALERT:
                d.key("OK")
            elif s == TODAY:
                d.key("BACK", PET)
            else:
                d.key("FARM")
        raise AssertionError("Could not return to pet")

    def open_capture():
        go_pet()
        d.key("OK", TREE)
        if d.info()["treeRow"] == 0:
            d.key("DOWN", TREE)
        d.key("OK", CAPTURE)
        if d.info()["captureRow"]:
            d.key("UP", CAPTURE)

    def set_batch(value):
        open_capture()
        current = d.info()["captureBatch"]
        while current != value:
            current = d.key("RIGHT" if current < value else "LEFT", CAPTURE)["captureBatch"]
        return d.key("OK", CAPTURE)

    try:
        d.reboot()
        baseline = d.info()
        assert baseline["fw"] == "orchard-ui-v3-hw2", baseline
        identity = pet_identity()
        assert "AD0," not in identity["archive"], "Do not create adoption as a test side effect"
        (OUT / "before_verification.json").write_text(json.dumps({"ui": baseline, "pet": identity}, indent=2), encoding="utf-8")
        d.sync()
        expected_date = int(datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d"))
        assert d.info()["localDate"] == expected_date
        passed("boot, existing pet archive, filesystem mount and actual local date")

        for state in ["IDLE", "THINKING", "TOOL", "WAIT", "DONE", "ERROR", "SLEEP", "IDLE"]:
            d.command("S:" + state, "OK:" + state)
        time.sleep(0.2)
        d.capture("01_pet")
        passed("all seven pet serial states and LCD readback")

        d.key("OK", TREE); d.capture("02_tree")
        d.key("OK", ARCHIVE); d.capture("03_archive")
        d.key("OK", STATUS); d.capture("04_status"); d.key("BACK", ARCHIVE)
        d.key("DOWN"); d.key("OK", ADOPTION); d.capture("05_adoption"); d.key("BACK", ARCHIVE)
        d.key("DOWN"); d.key("OK", MEMORIAL); d.capture("06_memorial"); d.key("BACK", ARCHIVE)
        passed("tree archive and all three child pages with return navigation")

        old_batch = baseline["batch"]
        test_batch = old_batch + 1 if old_batch < 99 else old_batch - 1
        temporary_batch = True
        saved = set_batch(test_batch)
        assert saved["batch"] == test_batch
        d.capture("07_capture_saved")
        d.reboot()
        assert d.info()["batch"] == test_batch, "Farm save did not survive a software reboot"
        assert d.info()["sequence"] == saved["sequence"]
        d.sync()
        restored = set_batch(old_batch)
        d.reboot()
        assert d.info()["batch"] == old_batch
        assert d.info()["sequence"] == restored["sequence"]
        temporary_batch = False
        d.sync()
        passed("real farm flash write, reboot reload, baseline restoration and second reboot")

        d.key("FARM", TODAY); d.capture("08_today")
        d.key("RIGHT", CALENDAR); d.capture("09_calendar")
        start_date = d.info()["selected"]
        d.key("OK", CALENDAR)
        assert d.info()["calendarEdit"] == 1
        assert d.key("RIGHT")["selected"] != start_date
        d.key("LEFT")
        assert d.info()["selected"] == start_date
        d.key("UP"); d.key("DOWN")
        d.key("OK", TASKS); d.capture("10_tasks")
        d.key("BACK", CALENDAR)
        go_pet()
        assert d.info()["records"] == baseline["records"]
        passed("calendar selection, month navigation and task browsing without creating records")

        d.key("FARM", TODAY); d.key("FARM", CALENDAR); d.key("FARM", TREE); d.key("FARM", REMINDERS)
        d.capture("11_reminders")
        d.key("OK"); d.key("RIGHT"); d.key("OK")
        d.key("TIMER", TIMER); d.key("TIMER", REMINDERS)
        go_pet()
        assert d.info()["reminders"] == baseline["reminders"], "Draft unexpectedly changed persisted reminders"
        passed("reminder draft editing and timer shortcut return without saving test reminders")

        d.key("TIMER", TIMER)
        duration = d.info()["timerDuration"]
        while duration != 60000:
            duration = d.key("DOWN" if duration > 60000 else "UP", TIMER)["timerDuration"]
        assert d.key("OK", TIMER)["running"] == 1
        d.capture("12_timer_running")
        d.key("TIMER", PET)
        started = time.monotonic()
        while time.monotonic()-started < 70:
            state = d.info()
            if state["screen"] == ALERT:
                break
            assert state["screen"] == PET, "Screen changed during unattended background timer test"
            print("Background timer:", state["timerMs"], "ms", flush=True)
            time.sleep(10)
        else:
            raise AssertionError("One-minute timer did not alert")
        assert state["finished"] == 1 and state["running"] == 0 and state["timerMs"] == 0
        d.capture("13_timer_alert")
        d.key("OK", PET)
        passed("real one-minute background countdown, expiry alert and return to pet")

        d.reboot(); d.sync()
        d.command("S:IDLE", "OK:IDLE")
        final = d.info()
        final_pet = pet_identity()
        assert final["screen"] == PET
        assert final["batch"] == baseline["batch"]
        assert final["records"] == baseline["records"] and final["reminders"] == baseline["reminders"]
        assert final_pet == identity, (identity, final_pet)
        q = d.command("Q", timeout=1.5)
        assert len([line for line in q if "crcOK=1" in line]) == 2, q
        d.capture("14_pet_final")
        passed("final pet identity and archive CRC intact; settings restored; no new tasks or reminders")
        report = {"completed_at": datetime.now(timezone(timedelta(hours=8))).isoformat(), "passed": results,
                  "before": baseline, "after": final, "pet": final_pet, "pet_storage": q,
                  "limits": ["Remote key injection verifies the shared dispatcher, not physical switch orientation", "LCD memory readback is not a camera photo", "Software reboot tested; physical power removal not tested", "Scheduled reminders were not enabled on the live device"]}
        (OUT / "verification_result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print("HARDWARE VERIFICATION PASSED", flush=True)
    finally:
        if temporary_batch and baseline:
            try:
                d.close(); d.connect(); set_batch(baseline["batch"])
                print("Temporary batch setting restored after interruption", flush=True)
            except Exception as exc:
                print("RESTORE REQUIRED:", baseline["batch"], str(exc), flush=True)
        d.close()

if __name__ == "__main__":
    main()
