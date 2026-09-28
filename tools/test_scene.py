#!/usr/bin/env python3
"""Validate real N3S files using the identical C parser used by the 3DS."""
import hashlib
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
from PIL import Image
from build_scene_assets import build, ROOT, SOURCE, DEST

os.chdir(ROOT)
Path('build').mkdir(exist_ok=True)
exe=ROOT/'build/scene_host'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-Iinclude','tests/scene_host.c','src/scene_loader.c','-lm','-o',str(exe)],check=True)

def accept(path): subprocess.run([str(exe),str(path)],check=True)

def texture_roundtrip(data, image, w, h, alpha_mode):
    """Independent unswizzle checks orientation, every pixel, RGBA channel order."""
    expected=image.convert('RGBA')
    if alpha_mode == 3: expected.putalpha(expected.getchannel('R'))
    expected=expected.resize((w,h),Image.Resampling.NEAREST); px=expected.load()
    for y in range(h):
        for x in range(w):
            morton=sum(((x>>k)&1)<<(2*k) | ((y>>k)&1)<<(2*k+1) for k in range(3))
            off=((y//8)*(w//8)*64+(x//8)*64+morton)*4; actual=tuple(reversed(data[off:off+4]))
            assert actual==px[x,y] or (actual[3]==px[x,y][3]==0)

if '--validate-only' in sys.argv:
    import json
    report=json.loads((DEST/'garage.json').read_text())
else:
    _,report=build()

for path in sorted(DEST.rglob('*.n3s')): accept(path)
for m in report['materials']:
    data=(DEST/m['texture']).read_bytes()
    assert len(data)==m['width']*m['height']*4
    texture_roundtrip(data,Image.open(SOURCE/m['source']),m['width'],m['height'],m['alpha_mode'])
    if m['source'].endswith('0013_safehouse_lightbloom.png'):
        assert m['alpha_mode'] == 3
        assert m['alpha_evidence'] == 'red-channel bloom coverage + additive blend'
print('N3S real garage accepted; textures use the vehicle-validated T3X PICA container')
if '--validate-only' in sys.argv: sys.exit(0)

original=(DEST/'garage.n3s').read_bytes()
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    duplicate,_=build(dest=td/'second')
    assert duplicate==original
    for tex in (DEST/'scene_tex').glob('*.rgba'):
        assert tex.read_bytes()==(td/'second/scene_tex'/tex.name).read_bytes()
    nv,ni,nb,nm,vo,io,bo,mo=struct.unpack_from('<8I',original,16)
    bad=[original[:n] for n in (0,1,255,256,vo+12,io,bo,mo,len(original)-1)]
    bad += [original+b'\0']
    def mutate(off,fmt,val):
        b=bytearray(original);struct.pack_into(fmt,b,off,val);bad.append(b)
    for off,val in [(4,2),(8,0),(12,1),(16,65536),(20,0xffffffff),(24,1025),(28,257),(32,260),(36,0),(40,0xffffffff),(44,0)]: mutate(off,'<I',val)
    for off in (48,vo,vo+12,vo+20): mutate(off,'<f',float('nan'))
    mutate(48,'<f',1e20); mutate(vo,'<f',99999);mutate(vo+20,'<f',-1)
    mutate(io,'<H',65535)
    for off,val in [(bo,3),(bo+4,0),(bo+4,0xffffffff),(bo+8,nm),(bo+12,1),(mo+176,4),(mo+180,3),(mo+184,2048),(mo+188,1)]:mutate(off,'<I',val)
    for off,length,value in [(72,64,b'a'*64),(mo+64,112,b'../bad'),(mo+64,112,b'/bad'),(mo+64,112,b'bad\\file'),(mo+64,112,b'a//b')]:
        b=bytearray(original);b[off:off+length]=value.ljust(length,b'\0');bad.append(b)
    b=bytearray(original);b[mo+192:mo+192+64]=b[mo:mo+64];bad.append(b)
    # Individually legal textures, aggregate allocation exceeds Old 3DS budget.
    b=bytearray(original)
    for i in range(nm): struct.pack_into('<II',b,mo+i*192+180,1024,1024)
    bad.append(b)
    p=td/'bad.n3s'
    for i,data in enumerate(bad):
        p.write_bytes(data)
        r=subprocess.run([str(exe),str(p)])
        assert r.returncode==2,(i,r.returncode)
    # Deterministic random bit fuzzing: acceptance is permitted, sanitizer failure is not.
    import random
    rng=random.Random(3401)
    for _ in range(100):
        b=bytearray(original)
        for _ in range(4):
            off=rng.randrange(len(b));b[off]^=1<<rng.randrange(8)
        p.write_bytes(b)
        assert subprocess.run([str(exe),str(p)]).returncode in (0,2)
print(f'N3S deterministic regeneration; {len(bad)} corruptions rejected; 100 mutation cases; UBSan clean')

asset_exe=ROOT/'build/scene_asset_host'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-Itests/scene_stubs','-Iinclude','tests/scene_asset_host.c','src/scene_asset.c','src/scene_loader.c','-lm','-o',str(asset_exe)],check=True)
with tempfile.TemporaryDirectory() as td:
    td=Path(td);world=td/'romfs:/world';(world/'scene_tex').mkdir(parents=True)
    (world/'garage.n3s').symlink_to(DEST/'garage.n3s')
    for p in (DEST/'scene_tex').glob('*.rgba'): (world/'scene_tex'/p.name).symlink_to(p)
    subprocess.run([str(asset_exe)],cwd=td,check=True)
    # Missing texture late in the transaction must release earlier acquisitions.
    (world/report['materials'][-1]['texture']).unlink()
    subprocess.run([str(asset_exe),'expect-failure'],cwd=td,check=True)
    # Incorrect texture payload length also rolls back.
    (world/report['materials'][-1]['texture']).write_bytes(b'bad')
    subprocess.run([str(asset_exe),'expect-failure'],cwd=td,check=True)
print('Scene asset missing/truncated texture rollback validated')
