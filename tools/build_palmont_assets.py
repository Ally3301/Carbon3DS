#!/usr/bin/env python3
"""Pack Palmont OBJ sections as low-draw-call N3S1 cells for the 3DS."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'assets/normalized/world/palmont/obj_sections'
DEST=ROOT/'assets/generated/3ds/world/palmont'
HEADER=256
def fixed(text,size):
 data=text.encode('ascii')
 if not data or len(data)>=size: raise ValueError(text)
 return data+bytes(size-len(data))
def pack(entry):
 path=SOURCE/entry['obj']; positions=[]; faces=[]
 for line in path.read_text().splitlines():
  p=line.split()
  if not p: continue
  if p[0]=='v':
   x,y,z,r,g,b=map(float,p[1:7]); positions.append((x-entry['origin'][0],z-entry['origin'][2],-(y-entry['origin'][1]),0.,0.,r,g,b,1.))
  elif p[0]=='f':
   faces.extend(int(v.split('/')[0])-1 for v in p[1:4])
 if not positions or len(positions)>65535 or not faces or len(faces)>196608 or len(faces)%3: raise ValueError(path)
 box=[min(v[k] for v in positions) for k in range(3)]+[max(v[k] for v in positions) for k in range(3)]
 verts=b''.join(struct.pack('<9f',*v) for v in positions); indices=struct.pack('<'+'H'*len(faces),*faces)
 vo=HEADER;io=vo+len(verts);bo=io+len(indices);mo=bo+16
 material=fixed('palmont/'+entry['section'],64)+fixed('palmont/scene_tex/palmont_vertex_white.rgba',112)+struct.pack('<4I',0,64,64,0)
 size=mo+len(material); digest=hashlib.sha256(path.read_bytes()).digest()
 header=struct.pack('<4s11I6f',b'N3S1',1,size,0,len(positions),len(faces),1,1,vo,io,bo,mo,*box)
 header+=fixed('opwd_3000/'+entry['section'],64)+fixed('OBJ:'+entry['obj'],88)+digest
 return header+verts+indices+struct.pack('<4I',0,len(faces),0,0)+material
def main():
 DEST.mkdir(parents=True,exist_ok=True);(DEST/'scene_tex').mkdir(exist_ok=True)
 (DEST/'scene_tex/palmont_vertex_white.rgba').write_bytes(bytes([255])*(64*64*4))
 entries=json.loads((SOURCE/'manifest.json').read_text()); runtime=[]
 for entry in entries:
  data=pack(entry); target=DEST/(entry['section']+'.n3s'); target.write_bytes(data)
  o=entry['origin']; runtime.append((entry['section'],target.name,o[0],o[2],-o[1]))
 (DEST/'manifest.txt').write_text(''.join('%s %s %.6f %.6f %.6f\n'%x for x in sorted(runtime)))
 print(f'Palmont OBJ->N3S: {len(runtime)} sections, one draw per loaded cell')
if __name__=='__main__': main()
