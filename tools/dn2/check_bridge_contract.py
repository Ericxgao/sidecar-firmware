"""Check frozen events against the real DN2 C encoder and Pi C++ codec; no device I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT/'docs/dn2/fixtures/events.json'


def run(args):
    p = subprocess.run([str(a) for a in args], check=False, capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    if p.returncode: raise RuntimeError(p.stderr or p.stdout or f'exit {p.returncode}')
    return p.stdout.strip()


def host(rows, dn2, out):
    cc = shutil.which('clang')
    cpp = shutil.which('clang++')
    if not cc or not cpp: raise RuntimeError('clang and clang++ required')
    generated = []
    for i, r in enumerate(rows):
        packet = bytes.fromhex(r['packet'])
        params = r['params'] + [0]*(16-len(r['params']))
        generated.append('''{
          ScEvent c{}; c.tick=%du;c.sequence=%du;c.track=%du;c.voice=%du;
          c.flags=%du;c.note=%du;c.velocity=%du;c.level=%du;c.reserved=%du;
          const unsigned short values[16]={%s};
          std::memcpy(c.params,values,sizeof values);std::strcpy((char*)c.engine,%s);
          const unsigned char expected[]={%s};
          check(c,expected,sizeof expected);
        }''' % (r['tick'],r['sequence'],r['track'],r['voice'],r['flags'],r['note'],r['velocity'],r['level'],
                r['version']-1,','.join(map(str,params)),json.dumps(r['engine']),','.join(map(str,packet))))
    source = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
extern "C" {
#include "bridge.h"
}
#include "dn2_native.h"
static void check(ScEvent c,const unsigned char* expected,unsigned n) {
    unsigned char buf[128];std::memset(buf,0xa5,sizeof buf);
    assert(sc_encode(&c,buf,n-1)==0);
    for(auto b:buf)assert(b==0xa5);
    assert(sc_encode(&c,buf,sizeof buf)==n && !std::memcmp(buf,expected,n));
    dn2_native::Event p{};assert(dn2_native::decode(expected,n,p));
    assert(p.tick==c.tick && p.sequence==c.sequence && p.track==c.track && p.voice==c.voice);
    assert(p.flags==c.flags && p.note==c.note && p.velocity==c.velocity && p.level==c.level);
    assert(!std::memcmp(p.params,c.params,sizeof c.params));
    assert(!std::strcmp(p.engine,c.reserved?(char*)c.engine:"mega.perkons"));
    if(c.reserved) {
        assert(dn2_native::encode_generic(p,buf,sizeof buf));
        assert(!std::memcmp(buf,expected,n));
    }
    const auto before=p;buf[6]|=128;
    assert(!dn2_native::decode(buf,n,p) && !std::memcmp(&p,&before,sizeof p));
    if(c.reserved) {
        std::memcpy(buf,expected,n);buf[94]|=4; // unused packing-mask bit
        assert(!dn2_native::decode(buf,n,p) && !std::memcmp(&p,&before,sizeof p));
    }
    c.track=16;std::memset(buf,0xa5,sizeof buf);assert(!sc_encode(&c,buf,sizeof buf));
    for(auto b:buf)assert(b==0xa5);
}
int main() {
''' + '\n'.join(generated) + '\nstd::puts("PASS: frozen events, DN2 C -> Pi C++ round trips, bounds and no partial writes");}\n'
    (out/'contract.cpp').write_text(source)
    run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-c',dn2/'csrc/sidecar/wire.c','-o',out/'wire.o'])
    exe = out/'contract.exe'
    run([cpp,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',
         '-I'+str(dn2/'csrc/sidecar'),'-I'+str(ROOT/'pi/engine'),out/'contract.cpp',out/'wire.o','-o',exe])
    print(run([exe]))


def coldfire(rows, dn2, out):
    sys.path.insert(0,str(dn2/'src'))
    from dnfw.patch import cbuild
    import unicorn as uc
    from unicorn import m68k_const as reg
    b=cbuild.build([dn2/'csrc/sidecar/wire.c'],base=0x40001000,entries=['sc_encode','sc_id_ok'])
    cpu=uc.Uc(uc.UC_ARCH_M68K,uc.UC_MODE_BIG_ENDIAN)
    cpu.ctl_set_cpu_model(reg.UC_CPU_M68K_CFV4E)
    cpu.mem_map(0x40000000,0x10000);cpu.mem_write(b.base,b.image)
    cpu.mem_map(0x46a00000,0x10000)
    event,packet,stack,stop=0x46a01000,0x46a02000,0x46a0f000,0x46a0f100
    def call(cap):
        cpu.mem_write(stack,struct.pack('>4I',stop,event,packet,cap))
        cpu.reg_write(reg.UC_M68K_REG_SR,0x2000);cpu.reg_write(reg.UC_M68K_REG_A7,stack)
        cpu.emu_start(b['sc_encode'],stop,count=50000)
        assert cpu.reg_read(reg.UC_M68K_REG_PC)==stop
        return cpu.reg_read(reg.UC_M68K_REG_D0)
    for r in rows:
        values=r['params']+[0]*(16-len(r['params']))
        cpu.mem_write(event,struct.pack('>I20H4B32s',r['tick'],r['sequence'],r['note'],r['velocity'],r['level'],
                                       *values,r['voice'],r['track'],r['flags'],r['version']-1,r['engine'].encode()))
        expected=bytes.fromhex(r['packet']);cpu.mem_write(packet,b'\xa5'*128)
        assert call(len(expected)-1)==0 and bytes(cpu.mem_read(packet,128))==b'\xa5'*128
        assert call(128)==len(expected) and bytes(cpu.mem_read(packet,len(expected)))==expected
    (out/'wire-coldfire.bin').write_bytes(b.image)
    print(f'PASS: same {len(rows)} frozen events through compiled ColdFire encoder ({len(b.image)} bytes)')
    return hashlib.sha256(b.image).hexdigest()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dn2',type=Path,required=True)
    ap.add_argument('--coldfire',action='store_true',help='Also compile and emulate only the portable ColdFire unit')
    ap.add_argument('--m68k-bin',type=Path,help='Existing native ColdFire toolchain directory; no download')
    args=ap.parse_args();args.dn2=args.dn2.resolve()
    if args.m68k_bin:
        os.environ['PATH']=str(args.m68k_bin.resolve(strict=True))+os.pathsep+os.environ.get('PATH','')
    out=ROOT/'build/dn2/contract';out.mkdir(parents=True,exist_ok=True)
    receipt=out/'contract-report.json';receipt.unlink(missing_ok=True)
    start=time.monotonic();rows=json.loads(FIXTURES.read_text())['events']
    host(rows,args.dn2,out)
    cf=coldfire(rows,args.dn2,out) if args.coldfire else None
    files=[FIXTURES,ROOT/'pi/engine/dn2_native.h',args.dn2/'csrc/sidecar/wire.c',args.dn2/'csrc/sidecar/bridge.h']
    receipt.write_text(json.dumps(dict(scope=__doc__,events=len(rows),seconds=time.monotonic()-start,
                                     coldfire_sha256=cf,inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}),indent=2)+'\n')


if __name__=='__main__':main()
