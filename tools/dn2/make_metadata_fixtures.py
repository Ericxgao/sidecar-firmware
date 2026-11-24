"""Original metadata-only RMP fixtures. NOT executable plugins; never install them."""
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import rmp_pack


def main():
    out = ROOT / 'build/dn2/fixtures'
    out.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(('bell', 'tone', 'noise')):
        knobs = [dict(p=p, long=f'Control {p}', short=f'P{p:02}', default=(p * 7 + index) % 128)
                 for p in range(1, 17)]
        knobs[1].update(look='centred', text='bipolar', default=64)
        knobs[8].update(look='list', text='enum', names=['A', 'B', 'C'], default=0)
        knobs[15].update(look='list', text='enum', names=[f'MODE{i:02}' for i in range(36)], default=0)
        manifest = dict(id=f'demo.{name}', version='1.0.0', name=name.upper(), short=name[:4].upper(),
                        licence='Original metadata fixture; no DSP', page2='params', page2_title='MORE CONTROLS',
                        max_voices=1, pitch='note', knobs=knobs)
        metadata, version, compatible = rmp_pack._manifest(manifest)
        # Valid container and metadata, deliberately invalid/null callback table.
        # The test server verifies and reads metadata; it never loads/calls DSP.
        image = bytes(4096)
        body = image + metadata
        body += bytes((-len(body)) % 4)
        header = struct.pack(rmp_pack.HEADER_FMT, rmp_pack.MAGIC_A64,
                             rmp_pack.package_format(manifest), 2, manifest['id'].encode(),
                             version, compatible, len(image), 0, 0, 0, len(metadata),
                             rmp_pack.header_flags(manifest), zlib.crc32(body) & 0xffffffff,
                             struct.pack('<I', len(image)) + bytes(16))
        (out / f'{name}.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (out / f'{name}.rmp').write_bytes(header + body)
    print('Created three metadata-only fixtures (not installable synths).')


if __name__ == '__main__':
    main()
