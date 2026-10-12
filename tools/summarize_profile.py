"""Summarize actual ORT provider events; CoreML events do not prove ANE use."""
import argparse
import collections
import json
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('profiles',type=Path,nargs='+')
a=p.parse_args()
totals=collections.defaultdict(lambda:{'calls':0,'microseconds':0})
for path in a.profiles:
    for event in json.loads(path.read_text()):
        provider=event.get('args',{}).get('provider')
        if event.get('cat')=='Node' and provider and event.get('name','').endswith('_kernel_time'):
            totals[provider]['calls']+=1
            totals[provider]['microseconds']+=event.get('dur',0)
print(json.dumps({'providers':dict(totals),'ane_execution_verified':False,
    'note':'Provider duration includes ORT event overhead; CoreML hardware scheduling requires Apple profiling.'},indent=2))
