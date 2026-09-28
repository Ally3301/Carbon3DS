#!/usr/bin/env python3
"""Build Palmont as one normalized OBJ/MTL source plus streamable N3S cells.
The 3DS never parses OBJ: this is an offline assembly/verification layer.
"""
from pathlib import Path
import json, shutil, sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from decode_cdl import Library, decode
from zeebo_shpm import extract_best
from build_scene_assets import build
SOURCE=ROOT/'assets/normalized/world/palmont/source_cdl'
OUT=ROOT/'assets/normalized/world/palmont/textured'
GENERATED=ROOT/'assets/generated/3ds/world'

def material_png(material, cache, destination):
    if material.get('msh') is None:
        p=destination/'untextured.png'
        if not p.exists(): Image.new('RGBA',(64,64),(255,255,255,255)).save(p)
        return p.name
    msh=SOURCE/(material['id'].split('_')[0]+'.msh')
    if msh not in cache:
        decoded=OUT/'_shpm_cache'/msh.stem
        cache[msh]=extract_best(msh,decoded)
    row=cache[msh][material['index']]
    if not row['ok']: raise ValueError(f'{material["id"]}: {row}')
    target=destination/(material['id']+'.png')
    if not target.exists(): shutil.copy2(OUT/'_shpm_cache'/msh.stem/row['file'],target)
    return target.name

def normalize(section, mesh, cache):
    folder=OUT/'sections'/section; textures=folder/'textures';textures.mkdir(parents=True,exist_ok=True)
    origin=[(mesh['bounds'][0][i]+mesh['bounds'][1][i])*0.5 for i in range(3)]
    bindings={key:material_png(mat,cache,textures) for key,mat in mesh['materials'].items()}
    with (folder/(section+'.obj')).open('w') as f:
        f.write(f'mtllib {section}.mtl\no {section}\n')
        for x,y,z,u,v,r,g,b,a in mesh['vertices']:
            f.write(f'v {x-origin[0]:.7g} {y-origin[1]:.7g} {z-origin[2]:.7g} {r/255:.5f} {g/255:.5f} {b/255:.5f}\n')
        for x,y,z,u,v,r,g,b,a in mesh['vertices']: f.write(f'vt {u:.7g} {v:.7g}\n')
        for batch in mesh['batches']:
            key=batch['material']; f.write('usemtl '+key+'\n')
            for a,b,c in mesh['faces'][batch['start']:batch['start']+batch['count']]: f.write(f'f {a+1}/{a+1} {b+1}/{b+1} {c+1}/{c+1}\n')
    with (folder/(section+'.mtl')).open('w') as f:
        for key,name in bindings.items(): f.write(f'newmtl {key}\nmap_Kd textures/{name}\n')
    manifest={'section':section,'origin':origin,'vertices':len(mesh['vertices']),'triangles':len(mesh['faces']),'materials':len(mesh['materials']),'source_sha256':mesh['source_sha256']}
    (folder/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    build(source=folder,dest=GENERATED,obj_name=section+'.obj',mtl_name=section+'.mtl',output_name='palmont/'+section+'.n3s',section_id='opwd_3000/'+section,source_identity='tracks/opwd_3000.viv:'+section+'.cdl+'+section+'.msh')
    return manifest

def main():
    OUT.mkdir(parents=True,exist_ok=True); cache={};lib=Library(SOURCE); report=[]; failed=[]
    for cdl in sorted(SOURCE.glob('*.cdl')):
        try:
            mesh=decode(lib.section(cdl.stem),lib)
            if mesh['faces']: report.append(normalize(cdl.stem,mesh,cache))
        except Exception as exc:
            failed.append({'section':cdl.stem,'error':str(exc)})
            print('FAILED',cdl.stem,exc,flush=True)
    (OUT/'manifest.json').write_text(json.dumps({'sections':report,'failed':failed},indent=2)+'\n')
    # One globally positioned OBJ/MTL for Blender inspection. Runtime still
    # consumes only spatial N3S cells; it never loads this aggregate file.
    full_obj=OUT/'Palmont_full.obj'; full_mtl=OUT/'Palmont_full.mtl'
    with full_obj.open('w') as out, full_mtl.open('w') as mtl:
        out.write('mtllib Palmont_full.mtl\n')
        base=0
        for item in report:
            sec=item['section']; folder=OUT/'sections'/sec; obj=folder/(sec+'.obj')
            origin=item['origin']; material_paths={}
            for line in (folder/(sec+'.mtl')).read_text().splitlines():
                if line.startswith('newmtl '): current=line.split(maxsplit=1)[1]
                elif line.startswith('map_Kd '): material_paths[current]=line.split(maxsplit=1)[1]
            for key,path in material_paths.items():
                mtl.write(f'newmtl {sec}__{key}\nmap_Kd sections/{sec}/{path}\n')
            out.write(f'o {sec}\n')
            for line in obj.read_text().splitlines():
                if line.startswith('v '):
                    parts=line.split(); x,y,z=map(float,parts[1:4])
                    out.write('v %.7g %.7g %.7g %s\n' % (x+origin[0],y+origin[1],z+origin[2],' '.join(parts[4:])))
                elif line.startswith('vt '): out.write(line+'\n')
                elif line.startswith('usemtl '): out.write(f'usemtl {sec}__{line.split(maxsplit=1)[1]}\n')
                elif line.startswith('f '):
                    q=[]
                    for token in line.split()[1:]:
                        a,b=token.split('/');q.append(f'{int(a)+base}/{int(b)+base}')
                    out.write('f '+' '.join(q)+'\n')
            base += item['vertices']
    print('Palmont textured:',len(report),'sections;',len(failed),'failed')
if __name__=='__main__':main()
