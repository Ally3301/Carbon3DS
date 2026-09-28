#!/usr/bin/env python3
"""Build runtime N3P assets from the verified PSP OBJ/texture extraction.

The PSP dump is deliberately used as the input here instead of trying to
guess the EAGL packet layout a second time.  Its OBJ files retain one material
group per original packet and its accompanying ``.spl1`` files retain the
alternative reflection normals for the next renderer pass.  The converter
keeps the existing Zeebo-only traffic set, then replaces every car for which
the PSP extraction has a complete component tree.
"""
from __future__ import annotations

import argparse, json, math, re, shutil, struct
from pathlib import Path

HEADER = struct.Struct("<4sIIIII")
VERTEX = struct.Struct("<8f")
GROUP = struct.Struct("<IIHBB")
RENDERERS = {"1501": 1}  # PSP Window; all other packets use TextureShiny.
SEMANTIC = {"Flat": 1, "Chrome": 2, "Rubber": 3, "Paint": 4,
            "Glass": 5, "Plastic": 6, "Aluminum": 7}

def classify(vehicle: str, model: str):
    suffix = model[len(vehicle)+1:] if model.startswith(vehicle + "_") else model
    for pat, slot, kind in (
        (r"UPGRADE(\d+)_BODY_B$", "body", "upgrade"),
        (r"UPGRADE(\d+)_BASE_B$", "base", "upgrade"),
        (r"HOOD_STYLE(\d+)_B$", "hood", "style"),
        (r"SPOILER_STYLE(\d+)_B$", "spoiler", "style"),
        (r"UPGRADE(\d+)_CREWTAG_SIDES_B$", "crewtag_sides", "upgrade"),
    ):
        m = re.match(pat, suffix)
        if m: return slot, kind, int(m.group(1))
    fixed = {"CREWTAG_HOOD_B": ("crewtag_hood", "fixed")}
    return (*fixed[suffix], -1) if suffix in fixed else ("other", "unknown", -1)

def parse_obj(path: Path):
    pos=[]; tex=[]; normal=[]; groups=[]; current=None; pending_semantic=""; packet_boundary=False
    def begin(material, force=False):
        nonlocal current, packet_boundary
        if force or current is None or current["material"] != material:
            current={"material":material, "semantic":pending_semantic, "faces":[]}; groups.append(current)
        packet_boundary=False
    def idx(text, length):
        value=int(text); return value-1 if value > 0 else length+value
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        bits=raw.split()
        if not bits: continue
        if bits[0] == "v": pos.append(tuple(map(float,bits[1:4])))
        elif bits[0] == "vt": tex.append(tuple(map(float,bits[1:3])))
        elif bits[0] == "vn": normal.append(tuple(map(float,bits[1:4])))
        elif raw.startswith("# psp_material_semantic "):
            pending_semantic=raw.split(None,2)[2].strip()
        elif bits[0] == "g":
            # EAGL packet boundaries remain meaningful even when adjacent
            # packets reuse an identical numeric material id.
            packet_boundary=True
        elif bits[0] == "usemtl": begin(bits[1] if len(bits)>1 else "0", packet_boundary)
        elif bits[0] == "f":
            if current is None: begin("0")
            face=[]
            for word in bits[1:]:
                a=(word.split("/")+["",""])[:3]
                face.append((idx(a[0],len(pos)), idx(a[1],len(tex)) if a[1] else -1,
                             idx(a[2],len(normal)) if a[2] else -1))
            for i in range(1,len(face)-1): current["faces"].append((face[0],face[i],face[i+1]))
    vertices=[]; indices=[]; result=[]; lookup={}
    for group in groups:
        start=len(indices)
        for triangle in group["faces"]:
            for pi,ti,ni in triangle:
                key=(pi,ti,ni)
                if key not in lookup:
                    x,y,z=pos[pi]; u,v=tex[ti] if ti>=0 else (0.,0.); nx,ny,nz=normal[ni] if ni>=0 else (0.,1.,0.)
                    x,y,z=x,z,-y; nx,ny,nz=nx,nz,-ny
                    length=math.sqrt(nx*nx+ny*ny+nz*nz) or 1.
                    lookup[key]=len(vertices); vertices.append((x,y,z,u,v,nx/length,ny/length,nz/length))
                indices.append(lookup[key])
        if len(indices)>start: result.append((start,len(indices)-start,group["material"],group["semantic"]))
    return vertices,indices,result

def write_n3p(source: Path, destination: Path):
    vertices,indices,groups=parse_obj(source)
    if not vertices or len(vertices)>65535 or not indices or len(indices)%3 or len(groups)>32:
        raise ValueError(f"unsupported mesh limits: {source}")
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open("wb") as out:
        out.write(HEADER.pack(b"N3P1",1,len(vertices),len(indices),len(groups),0))
        for vertex in vertices: out.write(VERTEX.pack(*vertex))
        out.write(struct.pack("<"+"H"*len(indices),*indices))
        for start,count,material,semantic in groups:
            try: material_id=int(material)
            except ValueError: material_id=0xffff
            out.write(GROUP.pack(start,count,material_id,RENDERERS.get(material,0),SEMANTIC.get(semantic,0)))
    return len(vertices),len(indices)//3

def copy_textures(source: Path, dest: Path):
    # The two-digit PNG filename prefix is an extraction ordinal.  The actual
    # material id lives in the PSP manifest's entry_id field.
    records=json.loads((source/"manifest.json").read_text()).get("vehicle_textures", [])
    copied=[]
    for row in records:
        if not row.get("ok") or not str(row.get("entry_id", "")).isdigit(): continue
        image=source/"vehicle"/str(row.get("file", ""))
        if not image.exists(): continue
        material=int(row["entry_id"])
        target=dest/"textures"/(f"{material:04d}_"+image.name.split("_",1)[-1])
        shutil.copy2(image,target)
        copied.append((material, str(row.get("name", ""))))
    return copied

def build_car(source: Path, textures: Path, dest: Path, old_cfg: Path):
    car=source.name
    if dest.exists(): shutil.rmtree(dest)
    (dest/"textures").mkdir(parents=True)
    parts=[]
    for obj in sorted(source.glob("*.obj")):
        if obj.stem.endswith("_SPLAY"): continue
        slot,kind,index=classify(car,obj.stem)
        if slot == "other": continue
        rel=Path("parts")/slot/(f"{kind}_{index:02d}.n3p" if index>=0 else f"{kind}_{obj.stem}.n3p")
        verts,tris=write_n3p(obj,dest/rel)
        # Preserve reflection normals as a sidecar for the renderer migration.
        splay=source/(obj.stem+".3ds.spl1")
        if splay.exists(): shutil.copy2(splay,(dest/rel).with_suffix(".spl1"))
        parts.append((slot,index,rel.as_posix(),obj.stem,verts,tris))
    texture_rows=copy_textures(textures,dest)
    texture_count=len(texture_rows)
    inherited=[]
    if old_cfg.exists():
        inherited=[line for line in old_cfg.read_text().splitlines() if line.startswith(("wheel_fit ","wheel_fit_axles ","light_fit "))]
    lines=["N3VCFG 1",f"id {car}","psp_texture_alpha mask",*inherited]
    texture_ids=sorted(material for material,_ in texture_rows)
    for number in texture_ids: lines.append(f"texture {number} tex/{number:04d}.t3x")
    detail=next((material for material,name in texture_rows if "DETAIL" in name.upper()), None)
    if detail is not None: lines.append(f"paint_details {detail}")
    # Local decal/lens assets use their recovered palette alpha as coverage.
    # PAINT/DETAILS stays out of this list: it is a TextureShiny material input,
    # not a standalone transparent decal.
    for material, name in texture_rows:
        label = name.upper()
        if "BADGING" in label or "BADGE" in label:
            lines.append(f"texture_alpha coverage {material}")
    for slot,index,rel,name,_,_ in sorted(parts): lines.append(f"part {slot} {index} {rel} {name}")
    (dest/"vehicle.cfg").write_text("\n".join(lines)+"\n")
    (dest/"manifest_psp.json").write_text(json.dumps({"source":"PSP OBJ/SPL1 extraction","parts":[{"slot":a,"index":b,"path":c,"model":d,"vertices":e,"triangles":f} for a,b,c,d,e,f in parts],"textures":texture_count},indent=2)+"\n")
    return len(parts),texture_count

def main():
    p=argparse.ArgumentParser(); p.add_argument("objects",type=Path); p.add_argument("textures",type=Path); p.add_argument("zeebo",type=Path); p.add_argument("output",type=Path); a=p.parse_args()
    if a.output.exists(): shutil.rmtree(a.output)
    shutil.copytree(a.zeebo,a.output)
    cars=parts=textures=0
    for source in sorted(x for x in a.objects.iterdir() if x.is_dir()):
        tex=a.textures/source.name
        if not (tex/"vehicle").is_dir(): continue
        count,images=build_car(source,tex,a.output/source.name,a.zeebo/source.name/"vehicle.cfg")
        cars+=1; parts+=count; textures+=images
    print(json.dumps({"psp_cars":cars,"psp_parts":parts,"psp_textures":textures,"output":str(a.output)},indent=2))
if __name__ == "__main__": main()
