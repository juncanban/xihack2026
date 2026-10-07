"""Check the shared local date using real time only; preserve saved user data.

Run after upload with a fresh artifacts/unified_clock/before.json baseline.
"""
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
import device_console as dc

OUT = Path(__file__).resolve().parents[1] / "artifacts" / "unified_clock"
dc.OUT = OUT


def main():
    baseline = json.loads((OUT / "before.json").read_text(encoding="utf-8"))
    d = dc.Device()
    result = {"screens": {}}
    try:
        initial = d.info()
        assert initial["fw"] == "fridge-ui-v3-local-clock", initial
        assert not initial["running"] and not initial["finished"], initial
        expected = datetime.now(timezone(timedelta(hours=8))).strftime("%Y-%m-%d")
        expected_code = int(expected.replace("-", ""))
        assert baseline["ui"]["fridgeDate"] == expected_code, "Fresh same-day baseline required"
        d.sync()
        result["status"] = d.command("I", "I:")
        assert result["status"].split("|")[2] == expected, result["status"]
        key_count = initial["physicalKeys"]

        def capture(name, screen):
            state = d.info()
            assert state["physicalKeys"] == key_count, "Manual navigation detected; stopping UI verification"
            assert state["screen"] == screen and state["localDate"] == expected_code, state
            result["screens"][name] = state
            d.capture(name)

        def key(button, expected_screen):
            assert d.info()["physicalKeys"] == key_count, "Manual navigation detected; stopping UI verification"
            state = d.key(button, expected_screen)
            assert state["physicalKeys"] == key_count, "Manual navigation detected; stopping UI verification"

        capture("01_pet", 0)
        key("FARM", 4)
        capture("02_today", 4)
        key("RIGHT", 5)
        capture("03_calendar", 5)
        key("LEFT", 4)
        key("LEFT", 0)
        final = d.info()
        for field in ("foods", "fridgeDate", "fridgeSequence", "fridgeUndo",
                      "records", "sequence", "farmCrc", "batch"):
            assert final[field] == baseline["ui"][field], (field, final[field], baseline["ui"][field])
        result["save"] = d.command("Q")
        assert result["save"] == baseline["save"], "Saved pet files changed"
        result["final"] = final
        result["checked_at"] = datetime.now(timezone(timedelta(hours=8))).isoformat()
        result["passed"] = True
        print("PASS local dates agree; pet, today and calendar captured; pet/farm/fridge data preserved", flush=True)
    finally:
        (OUT / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        d.close()


if __name__ == "__main__":
    main()
