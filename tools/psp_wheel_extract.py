#!/usr/bin/env python3
"""Extract PSP NFS Carbon OTC common wheel geometry and wheel textures.

Input: loadingbe/common.viv (BIG4) from PSP game.
Output:
  wheel_geometry/{WHEEL_SMALL,MEDIUM,LARGE,STOCK}.obj
  wheel_geometry/*_SPLAY.obj
  wheel_textures/*.png (229 observed wheel/tire styles)

Uses the same RefPack/EAGL decoders as the vehicle tools.
"""
from __future__ import annotations
import argparse, json, struct, sys
from pathlib import Path

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import psp_vehicle_extract as veh
import psp_texture_extract as tex

def parse_big(blob: bytes):
    if blob[:4] not in (b'BIGF',b'BIG4'):
        raise ValueError(f'not BIGF/BIG4: {blob[:4]!r}')
    count=struct.unpack_from('>I',blob,8)[0]
    pos=16; out=[]
    for _ in range(count):
        off,size=struct.unpack_from('>II',blob,pos); pos+=8
        end=blob.index(b'\0',pos)
        name=blob[pos:end].decode('latin1','replace'); pos=end+1
        if off+size>len(blob): raise ValueError(f'entry outside archive: {name}')
        out.append((name,off,size))
    return out

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('common_viv',type=Path)
    ap.add_argument('output',type=Path)
    ap.add_argument('--tools-dir',type=Path,default=Path('tools'),help='3DS port tools directory with zeebo_vehicle.py')
    args=ap.parse_args()
    common=args.common_viv.read_bytes()
    entries=parse_big(common)
    byname={n:(o,s) for n,o,s in entries}
    args.output.mkdir(parents=True,exist_ok=True)
    geo=args.output/'wheel_geometry'; geo.mkdir(exist_ok=True)
    txout=args.output/'wheel_textures'; txout.mkdir(exist_ok=True)

    # Four shared PSP wheel meshes discovered in common.viv.
    wheel_objs=['3201850644','356012318','364726668','364993175']
    geometry=[]
    for stem in wheel_objs:
        for splay in (False,True):
            name=stem+('_splay.o' if splay else '.o')
            if name not in byname: continue
            off,size=byname[name]
            mesh=veh.decode_psp_vehicle_model(common[off:off+size], args.tools_dir)
            outname=str(mesh['model'])+('_SPLAY' if splay else '')+'.obj'
            veh.write_obj(mesh,geo/outname)
            geometry.append({'source':name,'model':mesh['model'],'splay':splay,'file':outname,
                             'vertices':len(mesh['vertices']),'triangles':mesh['triangle_count'],
                             'materials':sorted({str(g['material_id']) for g in mesh['groups']}),
                             'packets':[{'material_id':str(g['material_id']),
                                         'semantic':g.get('material_semantic',''),
                                         'reflection_state':bool(g.get('car_reflection_state')),
                                         'vertices':g['vertex_count'],
                                         'triangles':g['index_count']//3,
                                         'strips':g['strip_count']} for g in mesh['groups']]})

    # Nested wheels.viv contains 229 RefPack SHPM textures in the observed build.
    if 'wheels.viv' not in byname: raise ValueError('common.viv does not contain wheels.viv')
    wo,ws=byname['wheels.viv']; wheels=common[wo:wo+ws]
    wentries=parse_big(wheels)
    maybe_refpack=tex.import_zeebo_refpack(args.tools_dir.resolve())
    textures=[]
    for i,(name,off,size) in enumerate(wentries):
        recs=tex.extract_shpm_blob(wheels[off:off+size],name,txout,maybe_refpack)
        for rec in recs:
            if rec.get('ok') and rec.get('file'):
                old=txout/rec['file']
                label=rec.get('name') or Path(name).stem
                newfile=f'{i:03d}_{tex.safe_name(str(label))}.png'
                new=txout/newfile
                if old.exists(): old.replace(new)
                rec['file']=newfile
            rec['member']=name; rec['member_index']=i
            textures.append(rec)
    report={'geometry':geometry,'wheel_texture_count':len(textures),
            'wheel_texture_ok':sum(1 for x in textures if x.get('ok')),'textures':textures}
    (args.output/'wheel_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='textures'},indent=2))

if __name__=='__main__': main()
