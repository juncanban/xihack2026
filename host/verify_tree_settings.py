"""Bounded settings-page verification; no clock/data write commands."""
import argparse
import json
from pathlib import Path
import device_console

OUT=Path(__file__).resolve().parents[1]/'artifacts/tree-settings/hardware'
OUT.mkdir(parents=True,exist_ok=True)
device_console.OUT=OUT
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',action='store_true')
args=parser.parse_args()
device=device_console.Device()
try:
    current=device.info()
    if args.baseline:
        (OUT/'before.json').write_text(json.dumps(current,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(current,ensure_ascii=False))
    else:
        (OUT/'after.json').write_text(json.dumps(current,ensure_ascii=False,indent=2),encoding='utf-8')
        assert current['fw']=='tree-v20-settings-preview',current['fw']
        baseline=json.loads((OUT/'before.json').read_text(encoding='utf-8'))
        protected=('foods','fridgeSequence','fridgeDate','farmCrc','records','sequence','batch')
        def preserved(info):
            for name in protected:
                assert baseline[name]==info[name],(name,baseline[name],info[name])
        preserved(current)
        report={'firmware':current['fw'],'data_preserved':True,'clock_command_sent':False,'navigation_completed':False}
        def save_report():
            (OUT/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        save_report()
        if current['physicalKeys']!=0 or current['screen']!=4:
            report['note']='Read-only verification: physical input detected or no longer on boot page.'
        else:
            class UserInputDetected(Exception): pass
            def checked():
                info=device.info()
                if info['physicalKeys']!=0: raise UserInputDetected()
                return info
            def key(name):
                checked()
                info=device.key(name)
                if info['physicalKeys']!=0: raise UserInputDetected()
                return info
            try:
                assert key('FARM')['screen']==5
                assert key('FARM')['screen']==6
                key('DOWN');info=key('DOWN');assert info['treeRow']==2
                checked();device.capture('tree_menu');checked()
                info=key('OK');assert info['screen']==8
                for name in ('UP','DOWN','LEFT','RIGHT','OK'):
                    info=key(name);assert info['screen']==8;preserved(info)
                info=key('TIMER');assert info['screen']==6 and info['treeRow']==2
                info=key('OK');assert info['screen']==8
                checked();device.capture('tree_settings');info=checked();preserved(info)
                report['navigation_completed']=True
                report['physicalKeys']=info['physicalKeys']
                report['note']='Verified renamed entry, inert edit keys, A return and settings LCD capture; no data changes.'
            except UserInputDetected:
                report['note']='Physical key activity detected; automatic navigation stopped immediately.'
        save_report()
        print(json.dumps(report,ensure_ascii=False,indent=2))
finally:
    device.close()
