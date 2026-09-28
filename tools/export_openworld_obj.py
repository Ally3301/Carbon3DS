#!/usr/bin/env python3
"""Export the recovered Palmont CDL geometry into one inspectable OBJ per section."""
from pathlib import Path
import json, sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from decode_cdl import Library, decode, write_obj
SOURCE=ROOT/'assets/normalized/world/palmont/source_cdl'
DEST=ROOT/'assets/normalized/world/palmont/obj_sections'
def main():
    DEST.mkdir(parents=True,exist_ok=True); lib=Library(SOURCE); manifest=[]
    for cdl in sorted(SOURCE.glob('*.cdl')):
        mesh=decode(lib.section(cdl.stem),lib)
        if not mesh['faces']: continue
        out=DEST/(cdl.stem+'.obj'); write_obj(mesh,out)
        lo,hi=mesh['bounds']; origin=[(lo[i]+hi[i])*0.5 for i in range(3)]
        manifest.append({'section':cdl.stem,'obj':out.name,'origin':origin,
                         'triangles':len(mesh['faces']),'source_sha256':mesh['source_sha256']})
    (DEST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Palmont OBJ: {len(manifest)} sections')
if __name__=='__main__': main()
