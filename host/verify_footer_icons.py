"""Bounded calendar/footer verification; no record creation or inventory edits."""
import argparse
import json
from pathlib import Path
import device_console

OUT = Path(__file__).resolve().parents[1] / 'artifacts/footer-icons/hardware'
OUT.mkdir(parents=True, exist_ok=True)
device_console.OUT = OUT
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
device = device_console.Device()

class UserInputDetected(Exception):
    pass

try:
    current = device.info()
    if args.baseline:
        (OUT/'before.json').write_text(json.dumps(current, ensure_ascii=False, indent=2), encoding='utf-8')
        assert not current['running'], 'Timer is running: abort upload to avoid interruption.'
        latest = device.info()
        assert latest['physicalKeys'] == current['physicalKeys'], 'Physical input detected: abort upload.'
        print(json.dumps(current, ensure_ascii=False))
    else:
        assert current['fw'] == 'ui-v24-icon-hints', current['fw']
        before = json.loads((OUT/'before.json').read_text(encoding='utf-8'))
        protected = ('foods','fridgeSequence','fridgeDate','farmCrc','records','sequence','batch')
        def preserved(info):
            for name in protected:
                assert info[name] == before[name], name
        preserved(current)
        def check():
            info = device.info()
            if info['physicalKeys'] != 0:
                raise UserInputDetected()
            return info
        def key(name):
            check()
            info = device.key(name)
            if info['physicalKeys'] != 0:
                raise UserInputDetected()
            return info
        def capture(name):
            check(); device.capture(name); check()
        def month_focus():
            for _ in range(6):
                info=check()
                assert info['screen']==5
                if info['calendarFocus']==1:
                    return info
                info=key('UP')
            raise AssertionError('Month focus not reached')
        try:
            check()
            if current['screen'] != 4:
                raise UserInputDetected()
            # The MCU loses its clock on upload. Supply only the real host clock.
            device.sync(); report['real_clock_sync']=True; check()
            info=key('FARM'); assert info['screen']==5 and info['calendarFocus']==2
            assert info['selected']==info['localDate']
            today=info['selected']
            capture('calendar_days')
            info=month_focus(); capture('calendar_month')
            info=key('DOWN'); assert info['screen']==5 and info['calendarFocus']==2 and info['selected']==today
            info=key('OK'); assert info['screen']==11 and info['selected']==today
            preserved(info);capture('farm_entry')
            info=key('TIMER'); assert info['screen']==5 and info['calendarFocus']==2 and info['selected']==today
            info=month_focus(); chosen=info['selected']
            info=key('OK'); assert info['screen']==11 and info['selected']==chosen
            preserved(info)
            info=key('TIMER'); assert info['screen']==5 and info['selected']==chosen
            report['calendar_checked']=True
            info=key('TIMER');assert info['screen']==4
            info=key('FARM');assert info['screen']==5 and info['calendarFocus']==2 and info['selected']==info['localDate']
            preserved(info);capture('final')
            (OUT/'final.json').write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding='utf-8')
            report.update(physicalKeys=info['physicalKeys'], final_screen=info['screen'], selected_date=info['selected'])
        except UserInputDetected:
            report['note']='Physical input or unexpected page detected: stopped navigation immediately.'
        (OUT/'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False, indent=2))
finally:
    device.close()
