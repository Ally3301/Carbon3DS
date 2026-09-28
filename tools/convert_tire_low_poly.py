#!/usr/bin/env python3
"""Convert the supplied binary-FBX low-poly tyre to portable N3P1."""
from __future__ import annotations
import argparse, struct, zlib
from pathlib import Path

HDR=struct.Struct('<4sIIIII'); VERT=struct.Struct('<8f'); GROUP=struct.Struct('<IIHBB')

def fbx_arrays(data: bytes):
    out={}; version=struct.unpack_from('<I',data,23)[0]
    if not data.startswith(b'Kaydara FBX Binary') or version >= 7500: raise ValueError('FBX version unsupported')
    def prop(pos):
        typ=chr(data[pos]); pos+=1
        if typ in 'fdil':
            cnt, enc, size=struct.unpack_from('<III',data,pos); pos+=12
            raw=data[pos:pos+size]; pos+=size
            if enc: raw=zlib.decompress(raw)
            code={'f':'f','d':'d','i':'i','l':'q'}[typ]
            return pos, struct.unpack('<'+code*cnt,raw)
        if typ == 'Y': return pos+2, None
        if typ in 'CFI': return pos+(1 if typ=='C' else 4), None
        if typ in 'DL': return pos+8, None
        if typ in 'SR':
            size=struct.unpack_from('<I',data,pos)[0]
            return pos+4+size, None
        raise ValueError(f'unsupported FBX property {typ}')
    def walk(pos,end):
        while pos+13<=end:
            node_end, count, _, name_len=struct.unpack_from('<IIIB',data,pos); pos+=13
            if not node_end:return
            name=data[pos:pos+name_len].decode(); pos+=name_len
            values=[]
            for _ in range(count): pos,v=prop(pos); values.append(v)
            if name in ('Vertices','PolygonVertexIndex','Normals','UV','UVIndex'): out[name]=values[0]
            walk(pos,node_end); pos=node_end
    walk(27,len(data))
    return out

def main():
 ap=argparse.ArgumentParser();ap.add_argument('fbx',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
 d=fbx_arrays(a.fbx.read_bytes()); req=('Vertices','PolygonVertexIndex','Normals','UV','UVIndex')
 if any(k not in d for k in req):raise ValueError('missing mesh arrays')
 p=d['Vertices']; poly=d['PolygonVertexIndex']; n=d['Normals']; uv=d['UV']; uvi=d['UVIndex']
 vertices=[]; indices=[]; face=[]; cursor=0
 # This asset's FBX uses source Z as wheel width. Centre it and map its
 # radial X/Y plane to the renderer's X/Y wheel plane (axis Z).
 axial_center=(max(p[2::3])+min(p[2::3]))*0.5
 def emit(corners):
  base=len(vertices)
  for pi,ni,ui in corners:
   x,y,z=p[pi*3:pi*3+3]; nx,ny,nz=n[ni*3:ni*3+3]; u,v=uv[ui*2:ui*2+2]
   # FBX Z-up -> port coordinates used by native car N3P.
   vertices.append((x,-y,z-axial_center,u,v,nx,-ny,nz))
  for j in range(1,len(corners)-1): indices.extend((base,base+j,base+j+1))
 for raw in poly:
  end=raw<0; pi=(-raw-1) if end else raw
  face.append((pi,cursor,uvi[cursor])); cursor+=1
  if end:
   emit(face);face=[]
 if face or cursor!=len(n)//3 or cursor!=len(uvi):raise ValueError('invalid FBX polygon stream')
 if len(vertices)>65535:raise ValueError('too many vertices')
 a.output.parent.mkdir(parents=True,exist_ok=True)
 with a.output.open('wb') as f:
  f.write(HDR.pack(b'N3P1',1,len(vertices),len(indices),1,0))
  for v in vertices:f.write(VERT.pack(*v))
  f.write(struct.pack('<'+'H'*len(indices),*indices));f.write(GROUP.pack(0,len(indices),0xffff,2,0))
 print(f'{a.output}: {len(vertices)} vertices, {len(indices)//3} triangles')
if __name__=='__main__':main()
