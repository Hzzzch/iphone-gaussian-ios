"""Linux verification of shared native code; never reports iOS/ANE as verified."""
import argparse
import ctypes as c
import json
from pathlib import Path
import subprocess
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--root', type=Path, required=True)
a = p.parse_args()
build = a.root / 'build-host'
out = a.root / 'reports' / 'host-20261008'
out.mkdir(parents=True, exist_ok=False)
models = a.root / 'models/onnx-scaled-v1'
fixture = a.source / 'dist/snapdragon-stable-v3/fixture'
contract = json.loads((fixture / 'input_contract.json').read_text())
print(contract, flush=True)
def run(binary, output):
    args = [str(binary)] + [str(models / (s + '.onnx')) for s in ('depth', 'features', 'head')]
    args += [str(fixture / 'image.rgb8'), '640', '480', str(output), '49.7776', '4', '1',
             'cpu', 'cpu', 'cpu', '.', 'reuse', '0']
    with output.with_suffix('.log').open('w') as log:
        subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True)
run(a.source / 'build-v6-host/photo_session_bench', out / 'reference')
run(build / 'photo_session_bench', out / 'ios-port')
comparisons = {}
for name in ('rgb', 'depth', 'coords', 'areas', 'means', 'covariances', 'colors', 'opacities'):
    before = np.fromfile(out / 'reference/run-0' / (name+'.f32'), np.float32)
    after = np.fromfile(out / 'ios-port/run-0' / (name+'.f32'), np.float32)
    assert before.shape == after.shape and np.isfinite(after).all()
    error = float(np.linalg.norm(after.astype(np.float64)-before) / max(np.linalg.norm(before.astype(np.float64)), 1e-30))
    comparisons[name] = {'exact': bool(np.array_equal(before, after)), 'relative_l2': error}
    assert error <= 1e-6, (name, error)
scene = out / 'ios-port/run-0'
subprocess.run([str(build/'scene_export_cli'), str(scene), '49.7776'], check=True)
from plyfile import PlyData
from scipy.spatial.transform import Rotation
vertices = PlyData.read(str(scene/'scene.ply'))['vertex']
quaternions = np.stack([vertices['rot_'+str(i)] for i in (1,2,3,0)], axis=1)
rotation = Rotation.from_quat(quaternions).as_matrix()
variance = np.exp(2*np.stack([vertices['scale_'+str(i)] for i in range(3)], axis=1).astype(np.float64))
reconstructed = (rotation * variance[:, None, :]) @ rotation.transpose(0,2,1)
cov = np.fromfile(scene/'covariances.f32', np.float32).reshape(-1,3,3)
cov_error = float(np.linalg.norm(reconstructed-cov)/np.linalg.norm(cov))
assert cov_error < 1e-5
lib=c.CDLL(str(build/'libmobile_onnx.so'))
class Options(c.Structure):
    _fields_=[('backend',c.c_char_p),('library_dir',c.c_char_p),('diagnostics_dir',c.c_char_p),('cache_output',c.c_char_p),('fallback',c.c_int)]
lib.mg_ort_open_ex.argtypes=[c.c_char_p,c.c_int,c.POINTER(Options)];lib.mg_ort_open_ex.restype=c.c_void_p
lib.mg_ort_error.restype=c.c_char_p
rejections={}
for backend in (b'coreml',b'htp'):
    config=Options(backend,None,None,None,1)
    assert not lib.mg_ort_open_ex(b'not-needed.onnx',4,c.byref(config))
    rejections[backend.decode()]=lib.mg_ort_error().decode()
report={'host_tests_passed': True,'comparisons':comparisons,'ply_covariance_relative_l2':cov_error,
    'backend_rejections':rejections,'ios_xcode_build_verified':False,'ios_device_verified':False,
    'coreml_partition_verified':False,'ane_execution_verified':False}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
