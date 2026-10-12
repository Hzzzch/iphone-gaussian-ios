"""Fetch iOS ONNX weights from GitHub Releases or the original SSH server."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--host',help='Use original SSH server instead of GitHub Releases')
p.add_argument('--repo',default='Hzzzch/iphone-gaussian-ios')
p.add_argument('--tag',default='models-onnx-scaled-v1')
p.add_argument('--out',type=Path,default=Path(__file__).resolve().parents[1]/'models/onnx-scaled-v1')
a=p.parse_args()
if a.out.exists(): raise SystemExit('Output exists; select a new directory')
a.out.mkdir(parents=True)
remote='/storage0/home/hezic/iphone-ios/models/onnx-scaled-v1/'
expected=json.loads((Path(__file__).resolve().parents[1]/'app/Resources/model-manifest.json').read_text())
for name,info in expected['files'].items():
    dest=a.out/name
    if a.host:
        subprocess.run(['scp',a.host+':'+remote+name,str(dest)],check=True)
    else:
        subprocess.run(['gh','release','download',a.tag,'--repo',a.repo,
                        '--pattern',name,'--dir',str(a.out)],check=True)
    sha=hashlib.sha256()
    with dest.open('rb') as f:
        for chunk in iter(lambda:f.read(8*1024*1024),b''): sha.update(chunk)
    if dest.stat().st_size!=info['bytes'] or sha.hexdigest()!=info['sha256']:
        raise RuntimeError('Model verification failed: '+name)
(a.out/'manifest.json').write_text(json.dumps(expected,indent=2)+'\n')
print('Verified iOS model folder:',a.out)
