import json, time, statistics
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
from device_console import Device

d=Device('COM8')
try:
    before=d.info(); print('BEFORE', json.dumps(before, ensure_ascii=False))
    samples=[]
    for _ in range(5):
        t=time.perf_counter(); d.command('U','U:'); samples.append((time.perf_counter()-t)*1000)
    print('U_LATENCY_MS', json.dumps(samples), 'median', statistics.median(samples))
    print('STATS', d.command('V:STATS','V:'))
    print('BAD_KEY', d.command('K:INVALID','ERR:KEY'))
    print('BAD_NAME', d.command('N:','ERR:NAME'))
    after=d.info(); print('AFTER', json.dumps(after, ensure_ascii=False))
    assert after['physicalKeys']==before['physicalKeys'], (before, after)
finally:
    d.close()
