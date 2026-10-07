"""Verify month/date focus on device without editing records or inventing clock dates."""
import json
from datetime import datetime, timedelta
from pathlib import Path
import device_console as dc

OUT = Path(__file__).resolve().parents[1] / "artifacts" / "calendar_interaction"
dc.OUT = OUT


class ManualNavigation(Exception):
    pass


def main():
    baseline = json.loads((OUT / "before.json").read_text(encoding="utf-8"))
    result = {"checks": [], "navigation_complete": False}
    d = dc.Device()
    try:
        initial = d.info()
        result["initial"] = initial
        assert initial["fw"] == "fridge-ui-v5-calendar-focus", initial
        d.sync()

        def current():
            value = d.info()
            if value["physicalKeys"]:
                raise ManualNavigation("Physical input detected; preserving user's page and selection")
            return value

        def key(button, screen, focus=None):
            current()
            value = d.key(button)
            if value["physicalKeys"]:
                raise ManualNavigation("Physical input detected; stopping navigation")
            assert value["screen"] == screen, value
            if focus is not None:
                assert value["calendarFocus"] == focus, value
            return value

        try:
            value = current()
            assert value["screen"] == 4 and not value["running"] and not value["finished"], value
            # Preserve the idle timer duration the user chose before flashing.
            duration = baseline["ui"]["timerDuration"]
            if value["timerDuration"] != duration:
                value = key("TIMER", 10)
                while value["timerDuration"] != duration:
                    value = key("UP" if value["timerDuration"] < duration else "DOWN", 10)
                key("TIMER", 4)
            result["checks"].append("idle timer duration restored")
            key("RIGHT", 5, 0)
            value = key("OK", 5, 1)
            today = value["localDate"]
            d.capture("01_month_focus")
            value = key("DOWN", 5, 2)
            assert value["selected"] == today, value
            d.capture("02_today_selected")
            result["checks"].append("month focus and default today")
            value = key("DOWN", 5, 2)
            expected = int((datetime.strptime(str(today), "%Y%m%d") + timedelta(days=7)).strftime("%Y%m%d"))
            assert value["selected"] == expected, value
            d.capture("03_next_week")
            value = key("OK", 11)  # Open list, do not create/confirm a record.
            value = key("BACK", 5, 2)
            assert value["selected"] == expected, value
            key("TIMER", 10)
            value = key("TIMER", 5, 2)
            assert value["selected"] == expected, value
            result["checks"].append("weekly movement, list return, timer return")
            while value["selected"] % 100 > 7:
                value = key("UP", 5, 2)
            selected = value["selected"]
            value = key("UP", 5, 1)
            assert value["selected"] == selected, value
            value = key("RIGHT", 5, 1)
            target = value["selected"]
            value = key("DOWN", 5, 2)
            assert value["selected"] == target and target // 100 != today // 100, value
            result["checks"].append("up to month bar and changing month")
            key("BACK", 5, 0)
            key("RIGHT", 6)
            key("LEFT", 5, 0)
            key("BACK", 4)
            result["navigation_complete"] = True
        except ManualNavigation as exc:
            result["navigation_stopped"] = str(exc)
            print(str(exc), flush=True)

        result["final"] = d.info()
        result["status"] = d.command("I", "I:")
        result["save"] = d.command("Q")
        for field in ("foods", "fridgeDate", "fridgeSequence", "fridgeUndo",
                      "records", "sequence", "farmCrc", "batch"):
            assert result["final"][field] == baseline["ui"][field], field
        assert result["save"] == baseline["save"], "Pet archive changed"
        result["user_data_preserved"] = True
        print(json.dumps({k: v for k, v in result.items() if k in
                          ("checks", "navigation_complete", "navigation_stopped", "user_data_preserved")}), flush=True)
    finally:
        (OUT / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        d.close()


if __name__ == "__main__":
    main()
