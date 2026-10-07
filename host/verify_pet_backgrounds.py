"""Targeted companion-page verification; stop navigation on physical input."""
import json
from pathlib import Path
import device_console as dc

dc.OUT = Path(__file__).resolve().parents[1] / 'artifacts/pet-backgrounds'
d=dc.Device()
result={}
try:
    initial=d.info(); result['initial']=initial
    assert initial['fw']=='ui-v25-pet-backgrounds',initial
    count=initial['physicalKeys']
    def check():
        info=d.info()
        if info['physicalKeys']!=count: raise RuntimeError('Physical input detected; stopped automatic navigation')
        return info
    def key(k,s):
        check()
        info=d.key(k,s)
        if info['physicalKeys']!=count: raise RuntimeError('Physical input detected; stopped automatic navigation')
    if count or initial['screen']!=4:
        result['navigation']='Skipped: user has operated device or current page is not startup home'
    else:
        key('OK',0)
        for name in ['hardware_dusk','hardware_room','hardware_night']:
            check(); d.capture(name);check()
            key('RIGHT',0)
        check();d.capture('hardware_default_restored');check()
        key('OK',4)
        result['navigation']='Passed: three backgrounds, right wrap, return to home; default restored'
    result['final']=d.info()
except Exception as error:
    result['error']=str(error)
    raise
finally:
    d.close()
    (dc.OUT/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
