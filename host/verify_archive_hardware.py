"""Only verify archive navigation and LCD readbacks; stop on physical input."""
import json
from pathlib import Path
import device_console as dc

OUT = Path(__file__).resolve().parents[1] / "artifacts/archive_hardware"
OUT.mkdir(parents=True, exist_ok=True)
dc.OUT = OUT

class ManualInput(Exception):
    pass

def main():
    result = {"complete": False, "screens": {}}
    before = json.loads((OUT / "before.json").read_text(encoding="utf-8"))
    device = dc.Device()
    try:
        initial = device.info()
        result["initial"] = initial
        assert initial["fw"] == "nav-v16-tree-archive", initial
        count = initial["physicalKeys"]

        def state():
            info = device.info()
            if info["physicalKeys"] != count:
                raise ManualInput("Physical input detected; stopped automatic navigation")
            return info

        def key(button, screen):
            state()
            info = device.key(button)
            if info["physicalKeys"] != count:
                raise ManualInput("Physical input detected; stopped automatic navigation")
            assert info["screen"] == screen, info
            return info

        def capture(name, screen):
            info = state()
            assert info["screen"] == screen, info
            device.capture(name)
            state()
            result["screens"][name] = info

        try:
            if count or initial["screen"] != 4:
                raise ManualInput("Device already operated after upload; preserving current page")
            device.sync()
            state()
            key("FARM", 5)
            key("FARM", 6)
            key("DOWN", 6)
            key("OK", 7)
            capture("01_archive_top", 7)
            key("OK", 7)
            for _ in range(4): key("DOWN", 7)
            info = state()
            assert info["archiveScroll"] == 96 and info["archiveFocused"] == 1, info
            capture("02_level_focus", 7)
            key("OK", 1)
            capture("03_growth_top", 1)
            key("DOWN", 1)
            key("DOWN", 1)
            capture("04_growth_bottom", 1)
            info = key("TIMER", 7)
            assert info["archiveScroll"] == 96 and info["archiveFocused"] == 1, info
            for _ in range(17): key("DOWN", 7)
            capture("05_cycles", 7)
            assert state()["archiveFocused"] == 0
            key("OK", 7)
            for _ in range(64):
                old = state()["archiveScroll"]
                if key("DOWN", 7)["archiveScroll"] == old: break
            else: raise AssertionError("Archive scroll did not stop at bottom")
            capture("06_memorial", 7)
            key("TIMER", 6)
            info = key("OK", 7)
            assert info["archiveScroll"] == 0, info
            result["complete"] = True
        except ManualInput as exc:
            result["navigation_stopped"] = str(exc)
            print(str(exc), flush=True)
        final = device.info()
        result["final"] = final
        for field in ("records", "sequence", "farmCrc", "batch", "foods", "fridgeSequence", "fridgeUndo"):
            assert final[field] == before[field], field
        result["farm_and_fridge_unchanged"] = True
        print(json.dumps({k:v for k,v in result.items() if k != "screens"}, ensure_ascii=False), flush=True)
    finally:
        (OUT / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        device.close()

if __name__ == "__main__":
    main()
