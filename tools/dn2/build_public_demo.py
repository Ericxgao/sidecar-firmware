"""Build unmodified upstream SC Snare for the Pi; no installation or device I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--a64-bin', required=True, type=Path,
                        help='Existing AArch64 bare-metal toolchain bin directory')
    args = parser.parse_args()
    tools = args.a64_bin.resolve(strict=True)
    extension = '.exe' if os.name == 'nt' else ''
    cc, cpp = (tools / ('aarch64-none-elf-' + name + extension) for name in ('gcc', 'g++'))
    out = ROOT / 'build/dn2/public-demo'
    out.mkdir(parents=True, exist_ok=True)
    sdk = ROOT / 'sdk/rmp'
    flags = ['-mcpu=cortex-a72', '-O2', '-fno-pic', '-fno-pie', '-mcmodel=small',
             '-ffixed-x18', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections',
             '-fno-common', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-fno-math-errno', '-I'+str(sdk.parent), '-I'+str(sdk)]
    options = dict(cwd=ROOT, check=True, capture_output=True, text=True,
                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)

    def run(command):
        result = subprocess.run([str(arg) for arg in command], **{**options, 'check': False})
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout.strip()

    sources = [ROOT / 'plugins/sc_snare/snare.c', sdk / 'shim/rmp_libc.c', sdk / 'shim/rmp_cxx.cpp']
    objects = []
    for source in sources:
        obj = out / (source.stem + '.o')
        extra = (['-fno-builtin', '-fno-rtti', '-fno-threadsafe-statics', '-fno-use-cxa-atexit']
                 if source.suffix == '.cpp' else ['-ffreestanding', '-fno-tree-loop-distribute-patterns'])
        run([cpp if source.suffix == '.cpp' else cc, *flags, *extra, '-c', source, '-o', obj])
        objects.append(obj)
    elf, package = out / 'sc_snare.a64.elf', out / 'sc_snare.a64.rmp'
    run([cpp, *flags, '-static', '-nostdlib', '-nostartfiles', '-Wl,--emit-relocs',
         '-Wl,--gc-sections', '-Wl,-z,max-page-size=4096', '-Wl,--no-warn-rwx-segments',
         '-Wl,-T,'+str(sdk/'plugin_a64.ld'), '-o', elf, *objects, '-lm', '-lgcc'])
    manifest = ROOT / 'plugins/sc_snare/manifest.json'
    print(run([sys.executable, ROOT/'tools/rmp_pack.py', elf, manifest, package]))
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    (out / 'build-report.json').write_text(json.dumps(dict(
        scope='Unmodified public SC Snare; build/pack only, not live playback evidence',
        compiler=run([cc, '--version']).splitlines()[0], package_sha256=digest(package),
        bytes=package.stat().st_size,
        sources={p.relative_to(ROOT).as_posix(): digest(p) for p in [*sources, manifest]}), indent=2)+'\n')
    print('Built public SC Snare; package is under build/dn2/public-demo/. Nothing installed.')


if __name__ == '__main__':
    main()
