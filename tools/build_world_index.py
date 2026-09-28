#!/usr/bin/env python3
"""Build Palmont spatial index from the normalized OBJ sections."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'assets/normalized/world/palmont/obj_sections'
DEST=ROOT/'assets/generated/3ds/world/palmont/world_index.txt'
def main():
 entries=json.loads((SOURCE/'manifest.json').read_text());
 textured=ROOT/'assets/normalized/world/palmont/textured/manifest.json'
 valid={item['section'] for item in json.loads(textured.read_text()).get('sections', [])} if textured.exists() else {item['section'] for item in entries}
 rows=[]
 for e in entries:
  if e['section'] not in valid: continue
  vs=[]
  for line in (SOURCE/e['obj']).read_text().splitlines():
   if line.startswith('v '):
    x,y,z,*_=map(float,line.split()[1:]);vs.append((x,z,-y))
  if not vs: continue
  mnx=min(v[0] for v in vs);mxx=max(v[0] for v in vs);mnz=min(v[2] for v in vs);mxz=max(v[2] for v in vs)
  ox,oy,oz=e['origin']; rows.append((e['section'],e['section']+'.n3s',ox,oz,-oy,mnx,mnz,mxx,mxz))
 DEST.parent.mkdir(parents=True,exist_ok=True)
 DEST.write_text(''.join('%s %s %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n'%r for r in sorted(rows)))
 print('Palmont spatial index:',len(rows),'sections')
if __name__=='__main__':main()
