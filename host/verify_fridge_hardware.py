"""Verify fridge-ui-v1 on a connected Wio Terminal using its real UI dispatcher.

Temporarily changes fridge data, then restores the starting quantities. Never
modifies pet/farm data. Stops on physical-key interference, keeping the evidence
and current quantities for inspection instead of overwriting user input.
Run with PlatformIO Python. LCD captures are actual RGB888 readbacks.
"""
import copy
import hashlib
import json
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

import device_console

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "hardware_fridge"
OUT.mkdir(parents=True, exist_ok=True)
device_console.OUT = OUT
PET, FRIDGE, TIMER, ALERT, ITEM, EDIT, TODAY = 0, 9, 10, 16, 17, 18, 19
EAT, RESTOCK, SET_EATEN, SET_INCOMING, SET_STOCK = range(5)


class Verification:
    def __init__(self):
        self.device = device_console.Device("COM8")
        self.physical = self.device.info()["physicalKeys"]
        self.result = {"at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
                       "checks": [], "status": "running"}
        self.stage = "startup"
        self.baseline = None

    def save_result(self):
        (OUT / "verification_result.json").write_text(
            json.dumps(self.result, ensure_ascii=False, indent=2), encoding="utf-8")

    def check_input(self, info):
        if info["physicalKeys"] != self.physical:
            raise RuntimeError("Physical key was pressed; stop to preserve user input.")
        return info

    def info(self):
        return self.check_input(self.device.info())

    def key(self, key, expected=None):
        return self.check_input(self.device.key(key, expected))

    def passed(self, text):
        self.result["checks"].append(text)
        self.stage = text
        self.save_result()
        print("PASS " + text, flush=True)

    def home(self):
        for _ in range(10):
            s = self.info()["screen"]
            if s == FRIDGE:
                return self.info()
            if s in (ITEM, EDIT, TODAY):
                self.key("BACK")
            elif s == ALERT:
                self.key("OK")
            elif s == TIMER:
                self.key("TIMER")
            else:
                self.key("FARM")
        raise AssertionError("Cannot navigate to fridge")

    def select(self, item):
        state = self.home()
        for _ in range(6):
            if state["fridgeItem"] == item:
                return state
            state = self.key("DOWN", FRIDGE)
        raise AssertionError("Cannot select food")

    def open_edit(self, item, action, value):
        self.select(item)
        self.key("OK", ITEM)
        for _ in range(action):
            self.key("DOWN", ITEM)
        state = self.key("OK", EDIT)
        for _ in range(30):
            delta = value - state["fridgeValue"]
            if not delta:
                return state
            key = "UP" if delta >= 10 else "DOWN" if delta <= -10 else "RIGHT" if delta > 0 else "LEFT"
            state = self.key(key, EDIT)
        raise AssertionError("Cannot set quantity")

    def edit(self, item, action, value):
        self.open_edit(item, action, value)
        result = self.key("OK", FRIDGE)
        assert result["fridgeSaveOk"] == 1, result
        return result

    def expect_food(self, item, stock, eaten, incoming):
        actual = self.info()["foods"][item]
        assert actual == {"stock": stock, "eaten": eaten, "incoming": incoming}, actual

    def undo(self):
        self.home()
        self.key("RIGHT", TODAY)
        self.key("RIGHT", TODAY)
        assert self.info()["fridgeUndo"] == 0
        self.key("LEFT", FRIDGE)

    def reboot(self, sync=True):
        self.device.reboot()
        self.physical = self.device.info()["physicalKeys"]
        assert self.physical == 0
        if sync:
            self.device.sync()
        return self.info()

    def capture(self, name):
        self.info()
        self.device.capture(name)
        self.info()

    def restore(self):
        # Set a feasible stock before correcting each daily counter, then set
        # the original physical stock. Never assumes corrections are independent.
        for item, original in enumerate(self.baseline["foods"]):
            for action, field in ((SET_EATEN, "eaten"), (SET_INCOMING, "incoming")):
                current = self.info()["foods"][item]
                if current[field] == original[field]:
                    continue
                delta = (current[field] - original[field]) if action == SET_EATEN else (original[field] - current[field])
                bridge = max(0, -delta)
                if current["stock"] != bridge:
                    self.edit(item, SET_STOCK, bridge)
                self.edit(item, action, original[field])
            if self.info()["foods"][item]["stock"] != original["stock"]:
                self.edit(item, SET_STOCK, original["stock"])
        # Leave no test action as an available undo.
        stock = self.baseline["foods"][0]["stock"]
        self.edit(0, SET_STOCK, stock + 1 if stock < 99 else stock - 1)
        self.undo()
        assert self.info()["foods"] == self.baseline["foods"]

    def run(self):
        self.device.sync()
        self.baseline = self.info()
        assert self.baseline["fw"] == "fridge-ui-v1"
        assert not self.baseline["fridgeUndo"], "Existing user undo; inspect before testing"
        assert all(food == {"stock": 0, "eaten": 0, "incoming": 0} for food in self.baseline["foods"]), "Not a fresh fridge; do not overwrite user quantities"
        self.result["baseline"] = copy.deepcopy(self.baseline)
        before = json.loads((OUT / "before_flash.json").read_text(encoding="utf-8"))
        for field in ("records", "sequence", "batch"):
            assert self.baseline[field] == before["ui"][field], field
        self.result["pet_before"] = self.device.command("I", "I:")
        old_pet = before["pet"].split("|")
        new_pet = self.result["pet_before"].split("|")
        assert old_pet[0] == new_pet[0] and old_pet[5] == new_pet[5]
        assert old_pet[-1].split(",AD")[1] == new_pet[-1].split(",AD")[1]
        self.home()
        self.capture("01_fridge_initial")
        self.open_edit(0, EAT, 1)
        assert self.key("OK")["screen"] == EDIT
        assert self.info()["foods"] == self.baseline["foods"]
        self.capture("02_zero_stock")
        self.passed("fresh zero quantities; empty stock rejected; pet and farm baseline retained")

        self.edit(0, RESTOCK, 5)
        self.expect_food(0, 5, 0, 5)
        self.edit(0, EAT, 2)
        self.expect_food(0, 3, 2, 5)
        self.capture("03_apple_inventory")
        self.key("OK", ITEM)
        self.capture("04_apple_operations")
        self.open_edit(0, SET_EATEN, 1)
        self.capture("05_correct_eaten")
        self.key("OK", FRIDGE)
        self.expect_food(0, 4, 1, 5)
        self.edit(0, SET_INCOMING, 4)
        self.expect_food(0, 3, 1, 4)
        self.edit(0, SET_STOCK, 7)
        self.expect_food(0, 7, 1, 4)
        self.undo()
        self.expect_food(0, 3, 1, 4)
        self.passed("eat/restock and all three corrections update only their intended quantities")

        self.edit(3, RESTOCK, 3)
        self.edit(3, EAT, 1)
        self.expect_food(3, 2, 1, 3)
        self.expect_food(0, 3, 1, 4)
        self.capture("06_second_inventory")
        self.key("RIGHT", TODAY)
        self.capture("07_daily_ledger")
        self.key("RIGHT", TODAY)
        self.expect_food(3, 3, 0, 3)
        self.edit(2, RESTOCK, 1)
        saved = copy.deepcopy(self.info()["foods"])
        self.reboot(sync=False)
        assert self.info()["foods"] == saved and self.info()["fridgeUndo"] == 1
        self.home()
        self.capture("08_unsynced_inventory")
        self.open_edit(0, EAT, 1)
        assert self.key("OK")["screen"] == EDIT
        assert self.info()["foods"] == saved
        self.device.sync()
        self.undo()
        self.expect_food(2, 0, 0, 0)
        self.passed("six-item navigation, isolated quantities, restart persistence and saved undo")

        before_draft = copy.deepcopy(self.info()["foods"])
        self.open_edit(0, RESTOCK, 3)
        self.key("TIMER", TIMER)
        while self.info()["timerDuration"] > 60000:
            self.key("DOWN", TIMER)
        self.key("OK", TIMER)
        self.key("TIMER", EDIT)
        assert self.info()["fridgeValue"] == 3 and self.info()["fridgeAction"] == RESTOCK
        print("Waiting for the real 60-second background countdown.", flush=True)
        deadline = time.monotonic() + 75
        samples = []
        while time.monotonic() < deadline:
            state = self.info()
            samples.append({"ms": state["timerMs"], "screen": state["screen"], "keys": state["physicalKeys"]})
            if state["screen"] == ALERT:
                break
            time.sleep(1)
        else:
            raise AssertionError("Countdown did not alert")
        assert state["timerMs"] == 0 and not state["running"]
        self.result["timer_samples"] = samples
        self.capture("09_timer_alert")
        self.key("OK", EDIT)
        assert self.info()["fridgeValue"] == 3
        self.key("BACK", ITEM)
        self.key("BACK", FRIDGE)
        assert self.info()["foods"] == before_draft
        self.key("TIMER", TIMER)
        self.key("LEFT", TIMER)
        self.key("RIGHT", TIMER)  # custom -> 3 minutes
        self.key("RIGHT", TIMER)  # 3 -> 8 minutes
        self.key("TIMER", FRIDGE)
        self.passed("real one-minute background timer; expiry returns to unchanged draft; cancel is non-mutating")

        self.restore()
        self.reboot()
        assert self.info()["foods"] == self.baseline["foods"] and not self.info()["fridgeUndo"]
        for field in ("records", "sequence", "batch", "farmCrc"):
            assert self.info()[field] == self.baseline[field], field
        self.home()
        self.capture("10_fridge_final")
        self.result["final"] = self.info()
        self.result["pet_final"] = self.device.command("I", "I:")
        self.result["files_final"] = self.device.command("Q", timeout=2)
        crc_lines = [line for line in self.result["files_final"] if "expect=" in line]
        assert len(crc_lines) == 2 and all("crcOK=1" in line and "magicOK=1" in line for line in crc_lines)
        self.passed("all temporary quantities restored and verified after reboot; farm CRC and pet slots valid")
        self.result["firmware_sha256"] = hashlib.sha256((ROOT / "firmware/.pio/build/vibe_pet/firmware.bin").read_bytes()).hexdigest()
        self.result["status"] = "passed"
        self.result["completed_at"] = datetime.now(timezone(timedelta(hours=8))).isoformat()
        self.save_result()


if __name__ == "__main__":
    test = Verification()
    try:
        test.run()
    except Exception as error:
        test.result.update(status="failed", failed_stage=test.stage, error=str(error))
        try:
            test.result["state_at_failure"] = test.device.info()
        except Exception:
            pass
        test.save_result()
        raise
    finally:
        test.device.close()
