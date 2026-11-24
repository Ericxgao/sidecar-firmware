"""Build/run DN2 codecs and the real USB driver on fake Circle. No hardware I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/dn2'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=shutil.which('clang++') or shutil.which('g++'))
    parser.add_argument('--sanitize', action='store_true', help='Enable ASan and UBSan where available')
    parser.add_argument('--usb', action='store_true', help='Also run the real USB driver and mirror regression with Circle shims')
    parser.add_argument('--native-only', action='store_true', help='Only native event/receiver/link/session checks for control bridge edits')
    parser.add_argument('--native-event', type=Path, help='Also decode a packet produced by the compiled ColdFire encoder')
    parser.add_argument('--native-generic', type=Path, help='Also decode/re-encode the compiled ColdFire sixteen-control packet')
    parser.add_argument('--native-ack', type=Path, help='Also decode an ACK published by the compiled ColdFire session handler')
    parser.add_argument('--recovery-only', type=Path, help='Only run saved eight-part packets through the session and receivers (DN2 out/sidecar-recovery/saved-parts.bin)')
    parser.add_argument('--ui-only', action='store_true', help='Only the native UI protocol and real package metadata checks')
    parser.add_argument('--ui-package', action='append', type=Path, default=[], help='Existing RMPA package for metadata interoperability; repeatable')
    args = parser.parse_args()
    if not args.compiler:
        parser.error('No C++ compiler found; supply --compiler')
    BUILD.mkdir(parents=True, exist_ok=True)
    # A failed new run must not leave a prior success receipt looking current.
    report_file = BUILD / ('recovery-report.json' if args.recovery_only else 'ui-report.json' if args.ui_only else 'codec-report.json')
    report_file.unlink(missing_ok=True)
    compiler = str(Path(args.compiler).resolve())
    env = dict(os.environ)
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    # Match the upstream portable runner: report warnings, but do not turn
    # compiler-specific warnings in unchanged upstream sources into test failures.
    flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-pedantic',
             '-I' + str(ROOT / 'teensy/libraries/RytmHost')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    checks = [('dn2_codec', [ROOT / 'pi/engine/tests/dn2/test_ob21.cpp'], flags),
              ('existing_ob_codec', [ROOT / 'teensy/libraries/RytmHost/extras/ob_block_test.cpp'], flags)]
    checks += [('dn2_native', [ROOT / 'pi/engine/tests/dn2/test_native.cpp'],
                [*flags, '-I' + str(ROOT / 'pi/engine')])]
    checks += [('dn2_native_generic', [ROOT / 'pi/engine/tests/dn2/test_native_generic.cpp'],
                [*flags, '-I' + str(ROOT / 'pi/engine')])]
    checks += [('dn2_native_link', [ROOT / 'pi/engine/tests/dn2/test_native_link.cpp'],
                [*flags, '-I' + str(ROOT / 'pi/engine')])]
    checks += [('dn2_native_session', [ROOT / 'pi/engine/tests/dn2/test_native_session.cpp'],
                [*flags, '-I' + str(ROOT / 'pi/engine')])]
    plugins = ROOT / 'teensy/libraries/RytmPlugins/src'
    checks += [('dn2_native_ui', [ROOT / 'pi/engine/tests/dn2/test_ui.cpp',
                *(plugins / f for f in ('rmp_proto.cpp', 'rmp_manifest.cpp', 'rmp_loader.cpp', 'rmp_meta.cpp'))],
                [*flags, '-Wno-unused-parameter', '-I' + str(ROOT / 'pi/engine'), '-I' + str(ROOT / 'core'), '-I' + str(plugins)])]
    checks += [('dn2_native_builtins', [ROOT / 'pi/engine/tests/dn2/test_builtins.cpp',
                *(plugins / f for f in ('rmp_proto.cpp', 'rmp_manifest.cpp', 'rmp_loader.cpp', 'rmp_meta.cpp'))],
                [*flags, '-Wno-unused-parameter', '-I' + str(ROOT / 'pi/engine'), '-I' + str(ROOT / 'core'), '-I' + str(plugins)])]
    extra_files = []
    control_src = BUILD / 'control_src'
    control_src.mkdir(exist_ok=True)
    for source in ('device/device.cpp', 'notes.cpp'):
        original = ROOT / 'core' / source
        shutil.copyfile(original, control_src / original.name)
        extra_files.append(original)
    control_flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                     '-I' + str(control_src), '-I' + str(ROOT / 'pi/engine/tests/dn2/shim'),
                     '-I' + str(ROOT / 'core'), '-I' + str(ROOT / 'pi/engine'),
                     '-I' + str(ROOT / 'pi/engine/tests')]
    if args.sanitize:
        control_flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    checks += [('dn2_control', [ROOT / 'pi/engine/tests/dn2/test_control.cpp', control_src / 'device.cpp',
                               control_src / 'notes.cpp', ROOT / 'core/device/a4.cpp',
                               ROOT / 'core/device/digitakt.cpp', ROOT / 'pi/engine/midi_in.cpp'], control_flags)]
    checks += [('usb_box', [ROOT / 'pi/engine/tests/test_usb_box.cpp'],
                [*flags, '-I' + str(ROOT / 'pi/engine')])]
    extra_files += list((ROOT / 'pi/engine/tests/dn2/shim').rglob('*.h'))
    extra_files += [ROOT / p for p in ('core/device/dn2.h', 'core/device/device.h', 'core/notes.h',
                                     'pi/engine/dn2_midi.h', 'pi/engine/dn2_native.h', 'pi/engine/dn2_receiver.h', 'pi/engine/dn2_link.h',
                                     'pi/engine/dn2_session.h', 'pi/engine/dn2_transport.h', 'pi/engine/dn2_parts.h',
                                     'pi/engine/dn2_ui.h', 'pi/engine/dn2_ui_wire.h', 'core/user/native_catalog.h',
                                     'pi/engine/midi_in.h', 'pi/engine/usb_box.h')]
    if args.native_event:
        args.native_event = args.native_event.resolve(strict=True)
    if args.native_generic:
        args.native_generic = args.native_generic.resolve(strict=True)
    if args.native_ack:
        args.native_ack = args.native_ack.resolve(strict=True)
    if args.usb:
        engine = ROOT / 'pi/engine'
        tests = engine / 'tests'
        copied = BUILD / 'ob_src'
        copied.mkdir(exist_ok=True)
        for name in ('ob_usb.cpp', 'ob_usb.h', 'ob_follow.cpp', 'ob_follow.h'):
            shutil.copyfile(engine / name, copied / name)
            extra_files.append(engine / name)
        usb_flags = ['-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
                     '-DOB_USB_TEST', '-D_USE_MATH_DEFINES']
        if args.sanitize:
            usb_flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        usb_flags += ['-I' + str(p) for p in (copied, tests / 'shim_ob', engine, ROOT / 'core',
                                            ROOT / 'teensy/libraries/RytmHost')]
        sources = [copied / 'ob_usb.cpp', copied / 'ob_follow.cpp', engine / 'a4_in.cpp',
                   engine / 'output.cpp', ROOT / 'core/device/digitakt.cpp', ROOT / 'core/device/a4.cpp',
                   ROOT / 'core/fx/out.cpp', ROOT / 'core/fx/out_measure.cpp']
        checks += [('usb_driver', [tests / 'test_ob_usb.cpp', *sources], [*usb_flags, '-DRASPPI=5']),
                   ('usb_mirror', [tests / 'test_ob_mirror.cpp', *sources],
                    [*usb_flags, '-DCIRCLE_GADGET_MIRROR', '-DRASPPI=4'])]
        extra_files += list((tests / 'shim_ob').rglob('*.h')) + [ROOT / 'core/fx/out.h']
    results = []
    if args.native_only:
        checks = [c for c in checks if c[0].startswith('dn2_native')]
    if args.ui_only:
        checks = [c for c in checks if c[0] == 'dn2_native_ui']
    if args.recovery_only:
        args.recovery_only = args.recovery_only.resolve(strict=True)
        checks = [('dn2_recovery', [ROOT / 'pi/engine/tests/dn2/test_recovery.cpp'],
                   [*flags, '-I' + str(ROOT / 'pi/engine')])]
    for name, sources, check_flags in checks:
        exe = BUILD / (name + ('.exe' if os.name == 'nt' else ''))
        cmd = [compiler, *check_flags, *(str(p) for p in sources), '-o', str(exe)]
        opts = dict(cwd=ROOT, env=env, capture_output=True, text=True, check=True,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        try:
            compiled = subprocess.run(cmd, **opts)
            if compiled.stderr: print(compiled.stderr, file=sys.stderr)
            run_args = [str(args.native_event)] if name == 'dn2_native' and args.native_event else []
            if name == 'dn2_recovery':
                run_args = [str(args.recovery_only)]
            if name == 'dn2_native_generic' and args.native_generic:
                run_args = [str(args.native_generic)]
            if name == 'dn2_native_link' and args.native_ack:
                run_args = [str(args.native_ack)]
            if name == 'dn2_native_ui':
                run_args = [str(p.resolve(strict=True)) for p in args.ui_package]
            run = subprocess.run([str(exe), *run_args], **opts)
        except subprocess.CalledProcessError as exc:
            print(exc.stdout or '', file=sys.stderr)
            print(exc.stderr or '', file=sys.stderr)
            raise
        print(name + ': ' + run.stdout.strip())
        results.append({'name': name, 'passed': True, 'output': run.stdout.strip()})
    files = [Path(__file__), ROOT / 'pi/engine/tests/dn2/ob21.h', ROOT / 'core/device/dn2_audio.h',
             ROOT / 'pi/engine/dn2_builtins.h', ROOT / 'pi/engine/dn2_receiver.h', ROOT / 'pi/engine/dn2_parts.h',
             *(p for _, sources, _ in checks for p in sources), *extra_files,
             ROOT / 'teensy/libraries/RytmHost/ob_block.h']
    report = {'passed': True, 'compiler': compiler, 'sanitizers': args.sanitize, 'checks': results,
              'scope': 'Synthetic wire fixtures and optionally real driver with fake Circle; no DN2 captures or hardware validation',
              'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    if args.recovery_only:
        report['scope'] = 'Actual DN2 converter/capture packets through Pi session/receivers; synthetic plugin readiness; no filesystem, DSP or hardware claim'
        report['input_sha256'] = hashlib.sha256(args.recovery_only.read_bytes()).hexdigest()
    if args.native_event:
        report['compiled_native_packet_sha256'] = hashlib.sha256(args.native_event.read_bytes()).hexdigest()
    if args.native_generic:
        report['compiled_generic_packet_sha256'] = hashlib.sha256(args.native_generic.read_bytes()).hexdigest()
    if args.native_ack:
        report['compiled_native_ack_sha256'] = hashlib.sha256(args.native_ack.read_bytes()).hexdigest()
    if args.ui_package:
        report['ui_packages'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.ui_package}
    report_file.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
