"""Bounded v19 USB check: no clock setting and no inventory commits."""
import argparse
import json
from pathlib import Path
import device_console

OUT=Path(__file__).resolve().parents[1]/'artifacts/fridge-inventory/hardware'
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
        assert current['fw']=='fridge-v19-direct-inventory',current['fw']
        baseline=json.loads((OUT/'before.json').read_text(encoding='utf-8'))
        for name in ('foods','fridgeSequence','fridgeDate','farmCrc','records','batch','light'):
            assert baseline[name]==current[name],(name,baseline[name],current[name])
        report={'firmware':current['fw'],'data_preserved':True,'clock_command_sent':False,'inventory_commits':0,'navigation_completed':False}
        def save_report():
            (OUT/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        save_report()
        if current['physicalKeys']!=0 or current['screen']!=4:
            report['note']='Read-only verification only: physical input detected or device no longer on boot page.'
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
                assert key('TIMER')['screen']==10
                assert key('TIMER')['screen']==9
                checked();device.capture('inventory_select');checked()
                info=key('RIGHT');assert info['fridgeColumn']==1
                value=info['fridgeValue']
                info=key('UP' if value<99 else 'DOWN')
                assert info['fridgeItem']==0 and info['fridgeValue']==value+(1 if value<99 else -1)
                assert info['foods']==baseline['foods']
                checked();device.capture('inventory_edit');checked()
                info=key('DOWN');assert info['fridgeItem']==5
                info=key('UP');assert info['fridgeItem']==0
                assert info['foods']==baseline['foods'] and info['fridgeSequence']==baseline['fridgeSequence']
                report['navigation_completed']=True
                report['physicalKeys']=info['physicalKeys']
                report['note']='Only entered inventory, checked draft/cancel and two rows; no saved quantity edits.'
            except UserInputDetected:
                report['note']='Physical key activity detected; automatic navigation stopped immediately.'
        save_report()
        print(json.dumps(report,ensure_ascii=False,indent=2))
finally:
    device.close()
