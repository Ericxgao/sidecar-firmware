"""Focused portable allocator and slot persistence checks. No hardware I/O."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/dn2'
BUILD.mkdir(parents=True, exist_ok=True)
compiler = shutil.which('clang++') or shutil.which('g++')
assert compiler, 'C++ compiler required'
plugins = ROOT / 'teensy/libraries/RytmPlugins/src'
env = dict(os.environ)
env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
opts = dict(cwd=ROOT, env=env, check=True, capture_output=True, text=True,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
for slots in (5, 8):
    exe = BUILD / f'image-arena-{slots}.exe'
    cmd = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
           '-Wno-unused-parameter', '-I'+str(ROOT/'core'), '-I'+str(plugins),
           *(['-DSIDECAR_EIGHT_ENGINES'] if slots == 8 else []),
           str(ROOT/'pi/engine/tests/dn2/test_image_arena.cpp'),
           *(str(plugins/f) for f in ('rmp_slots.cpp','rmp_loader.cpp','rmp_resolve.cpp','rmp_proto.cpp','rmp_manifest.cpp')),
           '-o', str(exe)]
    for args in (cmd, [str(exe)]):
        result = subprocess.run(args, **{**opts, 'check': False})
        if result.stdout: print(result.stdout.strip())
        if result.returncode: raise RuntimeError(result.stderr or f'exit {result.returncode}')
