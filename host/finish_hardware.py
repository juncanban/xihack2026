"""Final non-destructive checks after the interactive on-device test."""
import json
import time
from datetime import datetime, timezone, timedelta
from device_console import Device, OUT

before = json.loads((OUT / "before_final_flash.json").read_text(encoding="utf-8"))
entries = [json.loads(line) for line in (OUT / "serial_log.jsonl").read_text(encoding="utf-8").splitlines()]
timing = []
for entry in entries:
    for line in entry["reply"]:
        if line.startswith("U:"):
            try:
                state = json.loads(line[2:])
            except json.JSONDecodeError:
                continue
            if state.get("fw") == "orchard-ui-v3-hw2" and state["timerDuration"] == 60000:
                timing.append({"at":entry["at"],"state":state})
assert any(x["state"]["running"] and x["state"]["timerMs"] > 55000 for x in timing)
assert any(x["state"]["finished"] and x["state"]["timerMs"] == 0 for x in timing)
assert sum(x["state"]["running"] and x["state"]["screen"] == 0 for x in timing) >= 5

d = Device()
try:
    previous = d.info()
    assert previous["fw"] == "orchard-ui-v3-hw2"
    for key in ["records", "sequence", "batch", "reminders"]:
        assert previous[key] == before["ui"][key], (key, previous, before)
    d.reboot(); d.sync()
    d.command("S:IDLE", "OK:IDLE")
    time.sleep(0.2)
    final = d.info()
    assert final["screen"] == 0 and final["petState"] == 0
    for key in ["records", "sequence", "batch", "reminders"]:
        assert final[key] == previous[key]
    identity = d.command("I", "I:")
    old, new = before["identity"].split("|"), identity.split("|")
    assert len(new) == 9
    for part in [0,5,8]:
        assert old[part] == new[part], (part, old, new)
    storage = d.command("Q", timeout=1.5)
    assert len([line for line in storage if "crcOK=1" in line]) == 2
    d.capture("14_pet_final")
    report = {
        "completed_at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
        "firmware": final["fw"],
        "passed": [
            "Flash upload and byte verification",
            "All seven pet serial states and status-bar synchronization",
            "Tree archive and growth/adoption/memorial navigation",
            "Today/calendar/day selection/task browsing",
            "Reminder draft editing and timer shortcut return",
            "Batch setting 10->11, software reboot persisted 11, restoration to 10, second reboot persisted 10",
            "Real one-minute countdown continues on pet screen and expires; physical key observed at expiry",
            "Actual LCD memory captures",
            "Final software reboot retains user farm record/settings and original pet identity/CRC",
        ],
        "final": final, "identity": identity, "pet_storage": storage,
        "timer_evidence": timing,
        "human_timer_confirmation": "pending",
        "observations": [
            "First automatic timer run was interrupted by user-confirmed manual page navigation; timer still expired",
            "Second run stayed on pet page during countdown; one physical key occurred at expiry, before the next polling snapshot",
            "A farm record created while the user tried buttons was preserved; automated tests did not create or complete tasks",
        ],
        "limits": [
            "Physical power removal not performed; software reboot recovery tested",
            "No scheduled reminder enabled on the live device",
            "LCD readback confirms display contents, not physical panel color/brightness",
            "Remote key injection does not certify button position or tactile feedback",
        ],
    }
    (OUT / "verification_result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2),encoding="utf-8")
    print("PASS final reboot, pet name/adoption/stage/milestones and both pet CRCs")
    print("PASS existing user farm record retained; batch=10; reminder configuration retained")
    print("PASS device returned to IDLE pet page; serial port released")
finally:
    d.close()
