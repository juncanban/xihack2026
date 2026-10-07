"""Verify calendar home / tree interaction entry against a fresh pre-upload baseline."""
import json
from pathlib import Path
import device_console as dc

OUT = Path(__file__).resolve().parents[1] / "artifacts" / "calendar_home"
dc.OUT = OUT


class ManualNavigation(Exception):
    pass


def main():
    baseline = json.loads((OUT / "before.json").read_text(encoding="utf-8"))
    result = {"screens": {}, "navigation_complete": False}
    d = dc.Device()
    try:
        initial = d.info()
        assert initial["fw"] == "fridge-ui-v4-calendar-home", initial
        result["initial"] = initial
        d.sync()
        count = initial["physicalKeys"]

        def state():
            value = d.info()
            if value["physicalKeys"] != count:
                raise ManualNavigation("Physical input detected; stopped navigation")
            return value

        def key(button, screen):
            state()
            value = d.key(button, screen)
            if value["physicalKeys"] != count:
                raise ManualNavigation("Physical input detected; stopped navigation")

        def capture(name, screen):
            value = state()
            assert value["screen"] == screen, value
            d.capture(name)
            result["screens"][name] = value

        try:
            if count:
                raise ManualNavigation("Physical keys already used after upload; preserving current page")
            assert initial["screen"] == 4, initial
            assert not initial["running"] and not initial["finished"], initial
            result["boot_home_verified"] = True
            capture("01_home", 4)
            key("BACK", 4)
            key("RIGHT", 5)
            key("RIGHT", 6)
            capture("02_tree", 6)
            key("OK", 0)
            capture("03_companion", 0)
            key("TIMER", 10)
            key("TIMER", 0)
            key("LEFT", 6)
            key("DOWN", 6)
            key("OK", 7)  # Tree archive.
            key("BACK", 6)
            key("DOWN", 6)
            key("OK", 8)  # Settings; do not save.
            key("BACK", 6)
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
        assert result["save"] == baseline["save"], "Saved pet files changed"
        result["user_data_preserved"] = True
        print(json.dumps({k: v for k, v in result.items() if k in
                          ("navigation_complete", "navigation_stopped", "user_data_preserved")}), flush=True)
    finally:
        (OUT / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        d.close()


if __name__ == "__main__":
    main()
