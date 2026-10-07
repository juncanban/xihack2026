"""Bounded action-focus checks; no clock or persistent data changes."""
import argparse
import json
from pathlib import Path
import device_console
OUT=Path(__file__).resolve().parents[1]/'artifacts/timer-actions/hardware'
OUT.mkdir(parents=True,exist_ok=True)
device_console.OUT=OUT
parser=argparse.ArgumentParser();parser.add_argument('--baseline',action='store_true');args=parser.parse_args()
device=device_console.Device()
try:
    current=device.info()
    if args.baseline:
        (OUT/'before.json').write_text(json.dumps(current,ensure_ascii=False,indent=2),encoding='utf-8')
        assert not current['running'],'Timer running; stop deployment instead of interrupting it.'
        print(json.dumps(current,ensure_ascii=False))
    else:
        assert current['fw']=='timer-v23-action-focus'
        before=json.loads((OUT/'before.json').read_text(encoding='utf-8'))
        protected=('foods','fridgeSequence','fridgeDate','farmCrc','records','sequence','batch','light')
        def preserved(info):
            for name in protected: assert info[name]==before[name],name
        preserved(current)
        report={'firmware':current['fw'],'data_preserved':True,'clock_command_sent':False,'timer_checked':False}
        class UserInputDetected(Exception): pass
        def check():
            info=device.info()
            if info['physicalKeys']!=0: raise UserInputDetected()
            return info
        def key(name):
            check();info=device.key(name)
            if info['physicalKeys']!=0: raise UserInputDetected()
            return info
        def capture(name):
            check();device.capture(name);check()
        try:
            if current['screen']!=4: raise UserInputDetected()
            info=key('TIMER');assert info['screen']==10 and info['timerAction']==1
            capture('start_selected')
            info=key('LEFT');assert info['timerAction']==0 and info['timerMs']==480000 and not info['running']
            capture('reset_selected')
            info=key('UP');assert info['timerDuration']==540000 and info['timerAction']==0
            info=key('DOWN');assert info['timerDuration']==480000
            info=key('RIGHT');assert info['timerAction']==1 and not info['running']
            info=key('OK');assert info['running']
            info=key('LEFT');assert info['running'] and info['timerAction']==0
            capture('running_reset_selected')
            info=key('RIGHT');assert info['running'] and info['timerAction']==1
            info=key('OK');assert not info['running'] and info['timerMs']<480000
            capture('paused_selected')
            info=key('LEFT');assert not info['running'] and info['timerMs']<480000
            info=key('OK');assert not info['running'] and info['timerMs']==480000 and info['timerAction']==0
            info=key('RIGHT');assert info['timerAction']==1 and not info['running']
            preserved(info);capture('final')
            (OUT/'final.json').write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8')
            report.update(timer_checked=True,physicalKeys=info['physicalKeys'],final_duration_ms=info['timerDuration'])
        except UserInputDetected:
            report['note']='Physical input detected or page changed: stopped navigation immediately.'
        (OUT/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False,indent=2))
finally:
    device.close()
