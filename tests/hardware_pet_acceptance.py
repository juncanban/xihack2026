import json, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import device_console as console
console.OUT=Path(__file__).resolve().parents[1]/'artifacts'/'requested-flow'
d=console.Device()
try:
    start=d.info()
    print('START',json.dumps(start))
    assert start['screen']==4, 'Expected today page; refuse unplanned navigation'
    baseline=start['physicalKeys']
    def guard(info):
        assert info['physicalKeys']==baseline, 'Physical key detected; stop navigation'
        return info
    def key(k):
        guard(d.info())
        return guard(d.key(k))
    info=key('OK'); assert info['screen']==0
    states=[]
    for i in range(3):
        info=key('UP'); states.append(info['petState'])
        time.sleep(.2)
        guard(d.info())
        print('EXPRESSION',info['petState'],str(d.capture('pet-expression-'+str(info['petState']))))
        guard(d.info())
    assert len(set(states))==3
    for _ in range(3): info=key('DOWN')
    assert info['petState']==start['petState']
    end=key('BACK'); assert end['screen']==start['screen']
    for field in ['farmCrc','sequence','records','foods','fridgeSequence','timerDuration']:
        assert start[field]==end[field], field
    print('PASS three distinct expressions; original state/page restored; business fields unchanged')
finally:
    d.close()
