"""Run in the existing Linux project venv; prepare portable ONNX, never QNN caches."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import shutil
import onnx

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--link', action='store_true', help='Hardlink immutable source weights on the same filesystem')
a = p.parse_args()
if a.out.exists():
    raise SystemExit('Output exists; choose a new directory')
a.out.mkdir(parents=True)
files = {
    'depth.onnx': a.source / 'dist/snapdragon-stable-v3/models/depth.onnx',
    'features.onnx': a.source / 'reports/features-scaled-v5/features-scaled.onnx',
    'head.onnx': a.source / 'dist/snapdragon-stable-v3/models/head.onnx',
}
manifest = {'format': 'mobilegs-ios-onnx-v1', 'files': {}}
audit = {'coreml_partition_tested': False, 'ane_execution_verified': False, 'models': {}}
for name, source in files.items():
    print('Inspecting', source, flush=True)
    model = onnx.load(str(source), load_external_data=False)
    if any(t.data_location == onnx.TensorProto.EXTERNAL for t in model.graph.initializer):
        raise RuntimeError('This pack requires self-contained ONNX: ' + str(source))
    if any(n.op_type == 'EPContext' for n in model.graph.node):
        raise RuntimeError('QNN EPContext models cannot run on iOS')
    onnx.checker.check_model(str(source))
    digest = hashlib.sha256()
    with source.open('rb') as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b''):
            digest.update(block)
    def tensors(items):
        return [{'name': t.name, 'shape': [d.dim_value or d.dim_param for d in t.type.tensor_type.shape.dim],
                 'dtype': t.type.tensor_type.elem_type} for t in items]
    audit['models'][name] = {
        'inputs': tensors(model.graph.input), 'outputs': tensors(model.graph.output),
        'operators': dict(collections.Counter(n.op_type for n in model.graph.node)),
        'opsets': {x.domain: x.version for x in model.opset_import},
        'resize_antialias_nodes': [n.name for n in model.graph.node if n.op_type == 'Resize'
            and any(x.name == 'antialias' and x.i == 1 for x in n.attribute)],
        'source': str(source),
    }
    manifest['files'][name] = {'bytes': source.stat().st_size, 'sha256': digest.hexdigest()}
    del model
    if a.link:
        os.link(source, a.out / name)
    else:
        shutil.copyfile(source, a.out / name)
(a.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(a.out / 'model-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
