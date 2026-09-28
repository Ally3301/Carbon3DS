#!/usr/bin/env python3
"""Derive head/tail lamp sprite placement from recovered N3P material groups."""
import json, re, struct
from pathlib import Path

def group_points(path, wanted):
    d=path.read_bytes()
    if d[:4]!=b'N3P1': return []
    _,nv,ni,ng,_=struct.unpack_from('<IIIII',d,4)
    verts=[struct.unpack_from('<3f',d,24+i*32) for i in range(nv)]
    io=24+nv*32
    idx=struct.unpack_from('<%dH'%ni,d,io)
    go=io+ni*2; out=[]
    for i in range(ng):
        start,count,mat,_,_=struct.unpack_from('<IIHBB',d,go+i*12)
        if mat in wanted: out += [verts[idx[k]] for k in range(start,start+count)]
    return out

def fit(points):
    if not points: return None
    # Lamp groups contain both sides. Split by local Z to recover one sprite's
    # centre/size, then mirror through the car's coordinate origin at runtime.
    sides=[ [p for p in points if p[2]>=0], [p for p in points if p[2]<0] ]
    usable=[s for s in sides if s]
    if not usable: return None
    def bounds(a,k): return min(p[k] for p in a),max(p[k] for p in a)
    cx=sum((bounds(a,0)[0]+bounds(a,0)[1])*.5 for a in usable)/len(usable)
    cy=sum((bounds(a,1)[0]+bounds(a,1)[1])*.5 for a in usable)/len(usable)
    cz=sum(abs((bounds(a,2)[0]+bounds(a,2)[1])*.5) for a in usable)/len(usable)
    hy=sum((bounds(a,1)[1]-bounds(a,1)[0])*.5 for a in usable)/len(usable)
    hz=sum((bounds(a,2)[1]-bounds(a,2)[0])*.5 for a in usable)/len(usable)
    return cx,cy,cz,max(hy,.035),max(hz,.045)

def derive(car):
    manifest=json.loads((car/'manifest.json').read_text())
    head={int(t['id']) for t in manifest['textures'] if 'HEADLIGHT' in t['name'].upper()}
    tail={int(t['id']) for t in manifest['textures'] if any(x in t['name'].upper() for x in ('BRAKE','TAILIGHT','TAILLIGHT'))}
    # stock body is the reference; body kits are normally authored around the
    # same lamp sockets and retain identical material groups.
    body=car/'parts/body/upgrade_00.n3p'
    if not body.exists(): body=next((car/'parts/body').glob('*.n3p'),None)
    if not body: return None
    h=fit(group_points(body,head)); t=fit(group_points(body,tail))
    return h,t

def patch(cfg,h,t):
    lines=[x for x in cfg.read_text().splitlines() if not x.startswith('light_fit ')]
    if h and t:
        line='light_fit '+' '.join(f'{v:.6f}' for v in (*h,*t))
        at=4 if len(lines)>=4 else len(lines); lines.insert(at,line)
    cfg.write_text('\n'.join(lines)+'\n')

def main():
    root=Path('assets/generated/3ds/vehicles'); count=0
    for car in sorted(p for p in root.iterdir() if p.is_dir()):
        r=derive(car); patch(car/'vehicle.cfg',*r)
        if all(r): count+=1
    print(f'light fits: {count}')
if __name__=='__main__': main()
