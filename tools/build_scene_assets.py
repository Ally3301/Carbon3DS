#!/usr/bin/env python3
"""Pack normalized PocketGarage OBJ/MTL into the documented N3S1 contract."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import os
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/normalized/world/garage'
DEST = ROOT / 'assets/generated/3ds/world'
HEADER = 256

def fixed(s, n):
    b = s.encode('ascii')
    if not b or len(b) >= n:
        raise ValueError('identity too long/empty')
    return b + bytes(n-len(b))

def build(source=SOURCE, dest=DEST, textures=True, obj_name='PocketGarage_fixed.obj',
          mtl_name='PocketGarage_fixed.mtl', manifest_name='manifest.json',
          output_name='garage.n3s', section_id='l3rl_3401/A1',
          source_identity='tracks/l3rl_3401.viv:A1.cdl+A1.msh'):
    obj = source / obj_name
    mtl = source / mtl_name
    mats = {}
    for line in mtl.read_text().splitlines():
        p = line.split()
        if not p: continue
        if p[0] == 'newmtl':
            name = p[1]
            mats[name] = None
        elif p[0] == 'map_Kd':
            path = (source / p[1]).resolve()
            if not path.is_relative_to(source.resolve()):
                raise ValueError('unsafe texture reference')
            mats[name] = path
    positions, uv, verts, indices, batches, lookup = [], [], [], [], [], {}
    material_indices = []
    used = []
    current = None
    for line in obj.read_text().splitlines():
        p = line.split()
        if not p: continue
        if p[0] == 'v':
            a = list(map(float,p[1:]))
            if len(a) != 6: raise ValueError('expected position and RGB')
            x,y,z,r,g,b = a
            positions.append((x,z,-y,r,g,b,1.0))
        elif p[0] == 'vt': uv.append(tuple(map(float,p[1:3])))
        elif p[0] == 'usemtl':
            current = p[1]
            if current not in used:
                used.append(current)
                material_indices.append([])
        elif p[0] == 'f':
            if len(p) != 4 or current not in mats: raise ValueError('invalid triangle/material')
            mi = used.index(current)
            target = material_indices[mi]
            for token in p[1:]:
                pair = tuple(map(int,token.split('/')[:2]))
                if len(pair)!=2 or not (0<pair[0]<=len(positions) and 0<pair[1]<=len(uv)):
                    raise ValueError('invalid OBJ index')
                if pair not in lookup:
                    v=positions[pair[0]-1]; t=uv[pair[1]-1]
                    lookup[pair]=len(verts)
                    verts.append((*v[:3],*t,*v[3:]))
                target.append(lookup[pair])
    for mi, group in enumerate(material_indices):
        if group:
            batches.append([len(indices),len(group),mi,0])
            indices.extend(group)
    if not (0<len(verts)<=65535 and 0<len(indices)<=196608 and 0<len(batches)<=1024 and 0<len(used)<=256):
        raise ValueError('section limits exceeded')
    if any(not math.isfinite(f) for v in verts for f in v): raise ValueError('nonfinite vertex')
    dest.mkdir(parents=True,exist_ok=True)
    materials=[]; provenance=[]
    digest=hashlib.sha256(obj.read_bytes()+mtl.read_bytes()+(source/manifest_name).read_bytes())
    for name in used:
        path=mats[name]
        if path is None: raise ValueError('missing texture')
        png=path.read_bytes(); digest.update(png)
        # Content identity includes the conversion recipe; reusable across sections.
        key=hashlib.sha256(b'N3S1-rgba8-tex3ds-min64-nearest-v1'+png).hexdigest()
        use_t3x = False
        ref='scene_tex/'+key+('.t3x' if use_t3x else '.rgba')
        im=Image.open(path).convert('RGBA')
        # The recovered PocketGarage light bloom uses the red channel as its
        # coverage mask.  Its stored alpha is 249..255, which would turn the
        # cyan zero-red background into an almost opaque rectangle under a
        # conventional alpha blend.  Keep the normalized PNG untouched and
        # express this recovered material convention only in the runtime copy.
        bloom_mask = path.name == '0013_safehouse_lightbloom.png'
        if bloom_mask:
            im.putalpha(im.getchannel('R'))
        alphas=set(im.getchannel('A').getdata())
        mode=3 if bloom_mask else 0 if alphas=={255} else 1 if alphas<={0,255} else 2
        w,h=im.size
        w,h=max(64,w),max(64,h)
        if any(d>1024 or d&(d-1) for d in (w,h)): raise ValueError('non power-of-two texture')
        if textures:
            tex=dest/ref; tex.parent.mkdir(parents=True,exist_ok=True)
            with tempfile.TemporaryDirectory() as td:
                tmp=Path(td)
                im.resize((w,h),Image.Resampling.NEAREST).save(tmp/'image.png')
                tool=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))/'tools/bin/tex3ds'
                if use_t3x:
                    subprocess.run([str(tool),'-f','rgba','-z','auto','-o',str(tmp/'texture.t3x'),str(tmp/'image.png')],check=True,stdout=subprocess.DEVNULL)
                    tex.write_bytes((tmp/'texture.t3x').read_bytes())
                else:
                    subprocess.run([str(tool),'-r','-f','rgba','-z','none','-o',str(tmp/'raw'),str(tmp/'image.png')],check=True,stdout=subprocess.DEVNULL)
                    raw=(tmp/'raw').read_bytes()
                    if len(raw)!=4+w*h*4 or int.from_bytes(raw[:4],'little') != (w*h*4)<<8:
                        raise ValueError('unexpected tex3ds raw layout')
                    tex.write_bytes(raw[4:])
        materials.append(fixed(section_id+'_'+name,64)+fixed(ref,112)+struct.pack('<4I',mode,w,h,0))
        provenance.append(dict(key='l3rl_3401/'+name,source=str(path.relative_to(source)),texture=ref,width=w,height=h,alpha_mode=mode,alpha_evidence='red-channel bloom coverage + additive blend' if bloom_mask else 'PNG alpha; original EAGL blend state unknown',sha256=hashlib.sha256(png).hexdigest()))
    bounds=[min(v[k] for v in verts) for k in range(3)]+[max(v[k] for v in verts) for k in range(3)]
    vo=HEADER; io=vo+len(verts)*36; bo=io+len(indices)*2; mo=bo+len(batches)*16
    size=mo+len(materials)*192
    header=struct.pack('<4s11I6f',b'N3S1',1,size,0,len(verts),len(indices),len(batches),len(materials),vo,io,bo,mo,*bounds)
    header+=fixed(section_id,64)+fixed(source_identity,88)+digest.digest()
    assert len(header)==256
    data=header+b''.join(struct.pack('<9f',*v) for v in verts)+struct.pack('<'+'H'*len(indices),*indices)+b''.join(struct.pack('<4I',*b) for b in batches)+b''.join(materials)
    output=dest/output_name; output.parent.mkdir(parents=True,exist_ok=True); output.write_bytes(data)
    report=dict(format='N3S1',section=section_id,source=source_identity,normalized_sha256=digest.hexdigest(),vertices=len(verts),triangles=len(indices)//3,batches=len(batches),bounds=bounds,scene_bytes=len(data),texture_bytes=sum(m['width']*m['height']*4 for m in {p['texture']:p for p in provenance}.values()),materials=provenance)
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return data,report

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--geometry-only',action='store_true'); a=p.parse_args()
    _,r=build(textures=not a.geometry_only)
    print(f"PocketGarage: {r['vertices']} vertices, {r['triangles']} triangles, {r['batches']} batches, {r['scene_bytes']} scene bytes, {r['texture_bytes']} texture bytes")
