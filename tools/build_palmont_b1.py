#!/usr/bin/env python3
"""Normalize and pack Palmont B1 through the same OBJ/MTL/PNG N3S path as Garage."""
from pathlib import Path
import json,shutil,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from decode_cdl import Library,decode
from zeebo_shpm import extract_best
from build_scene_assets import build
SOURCE=ROOT/'assets/normalized/world/palmont/source_cdl'
OUT=ROOT/'assets/normalized/world/palmont/textured/B1'

def main():
    mesh=decode(Library(SOURCE).section('B1'),Library(SOURCE))
    origin=[(mesh['bounds'][0][i]+mesh['bounds'][1][i])*0.5 for i in range(3)]
    OUT.mkdir(parents=True,exist_ok=True); tex=OUT/'textures';tex.mkdir(exist_ok=True)
    cache={}
    for key,material in mesh['materials'].items():
        msh=SOURCE/(material['id'].split('_')[0]+'.msh')
        if msh not in cache:
            folder=OUT/'_decoded'/msh.stem
            cache[msh]=extract_best(msh,folder)
        row=cache[msh][material['index']]
        if not row['ok']: raise ValueError(f'{key}: {row}')
        shutil.copy2(OUT/'_decoded'/msh.stem/row['file'],tex/(key+'.png'))
    with (OUT/'B1.obj').open('w') as f:
        f.write('mtllib B1.mtl\no B1\n')
        for x,y,z,u,v,r,g,b,a in mesh['vertices']:
            f.write(f'v {x-origin[0]:.7g} {y-origin[1]:.7g} {z-origin[2]:.7g} {r/255:.5f} {g/255:.5f} {b/255:.5f}\n')
        for x,y,z,u,v,r,g,b,a in mesh['vertices']: f.write(f'vt {u:.7g} {v:.7g}\n')
        for batch in mesh['batches']:
            f.write('usemtl '+batch['material']+'\n')
            for a,b,c in mesh['faces'][batch['start']:batch['start']+batch['count']]: f.write(f'f {a+1}/{a+1} {b+1}/{b+1} {c+1}/{c+1}\n')
    with (OUT/'B1.mtl').open('w') as f:
        for key in mesh['materials']:
            f.write(f'newmtl {key}\nmap_Kd textures/{key}.png\n')
    manifest={'section':'B1','origin':origin,'vertices':len(mesh['vertices']),'triangles':len(mesh['faces']),'materials':len(mesh['materials'])}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    build(source=OUT,dest=ROOT/'assets/generated/3ds/world',obj_name='B1.obj',mtl_name='B1.mtl',output_name='palmont/B1.n3s',section_id='opwd_3000/B1',source_identity='tracks/opwd_3000.viv:B1.cdl+B1.msh')
    print('B1 normalized and packed:',manifest)
if __name__=='__main__': main()
