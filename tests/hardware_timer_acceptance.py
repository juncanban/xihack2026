import json, sys, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
import device_console as console
console.OUT=Path(__file__).resolve().parents[1]/'artifacts'/'requested-flow'
d=console.Device()
try:
    before=d.info(); print('BEFORE',json.dumps(before),flush=True)
    assert before['screen']==4 and before['timerDuration']==60000 and not before['running']
    baseline=before['physicalKeys']
    def guard(info):
        assert info['physicalKeys']==baseline,'Physical input detected; stop automatic navigation'
        return info
    def key(k):
        guard(d.info()); return guard(d.key(k))
    print('LONG_BAD_KEY',d.command('K:'+'Z'*200,'ERR:KEY'),flush=True)
    print('BAD_STATE',d.command('S:INVALID','ERR:STATE'),flush=True)
    info=key('TIMER'); assert info['screen']==10
    started=time.perf_counter(); info=key('OK'); assert info['running']
    for _ in range(35):
        time.sleep(2)
        info=guard(d.info())
        if info['finished']:
            break
    elapsed=time.perf_counter()-started
    assert info['finished'] and info['screen']==16 and not info['running'], info
    print('REAL_TIMER_FINISHED_SECONDS',elapsed,'polling_resolution_seconds',2,flush=True)
    print('ALERT_CAPTURE',str(d.capture('timer-alert')),flush=True)
    guard(d.info()); key('OK'); after=key('FARM'); assert after['screen']==before['screen']
    for field in ['farmCrc','sequence','records','foods','fridgeSequence','timerDuration']:
        assert before[field]==after[field],field
    print('AFTER',json.dumps(after),flush=True)
    print('PASS real one-minute timer and alert; no farm/fridge data changes',flush=True)
finally:
    d.close()
