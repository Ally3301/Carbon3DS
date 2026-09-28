#!/usr/bin/env python3
"""Extract NFS Carbon Own the City PSP vehicle component ELFs to OBJ.

This is intentionally derived from the Zeebo vehicle decoder used by the 3DS
port. PSP files use the same RefPack + ELF32/MIPS + EAGL relocation structure,
but their primitive streams add an 8-byte PSP prefix and can contain multiple
triangle strips per material packet. Quantization also differs:
  UV       : (s16 - 16384) / 512
  normals  : s16 / 32767
  positions: s16 / 8192

Observed PSP NFSCar_TextureShiny/NFSCar_Window records remain 16 bytes:
  <u16ish U, V, s16 Nx,Ny,Nz, s16 Px,Py,Pz> == <8h>
NFSCar_Gouraud records contain only <3h> positions.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# Let this script be dropped next to the port or use --tools-dir.
def import_zeebo_tools(tools_dir: Path):
    sys.path.insert(0, str(tools_dir))
    from zeebo_vehicle import maybe_refpack, ZeeboFormatError, _cstring  # type: ignore
    return maybe_refpack, ZeeboFormatError, _cstring


def _require(cond: bool, msg: str) -> None:
    if not cond:
        raise ValueError(msg)


def _material_for_packet(relocs, descriptor_offset: int) -> str:
    for relocation, symbol, relocation_type in relocs:
        if relocation_type != 2:
            continue
        if not (descriptor_offset < relocation < descriptor_offset + 0x60):
            continue
        if not symbol.startswith("__EAGL::TAR:::"):
            continue
        m = re.search(r";1=([^,;]+)", symbol)
        if m:
            return m.group(1)
    return ""




def _state_info_for_packet(relocs, descriptor_offset: int):
    semantic = ""
    reflection = False
    for relocation, symbol, relocation_type in relocs:
        if relocation_type != 2:
            continue
        if not (descriptor_offset < relocation < descriptor_offset + 0x60):
            continue
        if symbol.startswith("__COORD4:::GAME::Material_"):
            semantic = symbol.rsplit("Material_", 1)[1]
        elif symbol == "__EAGL::GeoPrimState:::GAME::CarReflectionState":
            reflection = True
    return semantic, reflection

def _strip_indices(first_vertex: int, count: int) -> List[int]:
    out: List[int] = []
    for i in range(2, count):
        a, b, c = i - 2, i - 1, i
        if i & 1:
            a, c = c, a
        out.extend((first_vertex + a, first_vertex + b, first_vertex + c))
    return out


def _parse_elf(data: bytes, maybe_refpack, _cstring):
    decoded = maybe_refpack(data)
    _require(len(decoded) >= 52 and decoded[:4] == b"\x7fELF", "not ELF32")
    _require(decoded[4] == 1 and decoded[5] == 1, "not ELF32 little-endian")
    h = struct.unpack_from("<16sHHIIIIIHHHHHH", decoded, 0)
    _, _, machine, _, _, _, shoff, _, _, _, _, shentsize, shnum, shstrndx = h
    _require(machine == 8, f"not MIPS ELF (machine={machine})")
    _require(shentsize == 40, "unsupported section header size")
    _require(shoff + shentsize * shnum <= len(decoded), "section table truncated")

    sections = []
    for i in range(shnum):
        v = struct.unpack_from("<IIIIIIIIII", decoded, shoff + i * shentsize)
        sections.append({"index": i, "name_offset": v[0], "offset": v[4], "size": v[5], "entry_size": v[9]})
    _require(shstrndx < len(sections), "invalid section string table")
    shstr = sections[shstrndx]
    shnames = decoded[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for s in sections:
        s["name"] = _cstring(shnames, s["name_offset"])
        _require(s["offset"] + s["size"] <= len(decoded), f"section {s['name']} truncated")
    named = {s["name"]: s for s in sections}
    for req in (".data", ".strtab", ".symtab", ".rel.data"):
        _require(req in named, f"missing {req}")

    strings_sec = named[".strtab"]
    sym_sec = named[".symtab"]
    strings = decoded[strings_sec["offset"]:strings_sec["offset"] + strings_sec["size"]]
    ent = sym_sec["entry_size"] or 16
    _require(ent == 16 and sym_sec["size"] % 16 == 0, "unsupported symbol table")
    symbols = []
    for i in range(sym_sec["size"] // 16):
        off = sym_sec["offset"] + i * 16
        no, value, size, info, other, sec = struct.unpack_from("<IIIBBH", decoded, off)
        symbols.append({"name": _cstring(strings, no) if no else "", "value": value, "size": size, "section": sec})

    data_sec = named[".data"]
    blob = decoded[data_sec["offset"]:data_sec["offset"] + data_sec["size"]]
    rel_sec = named[".rel.data"]
    rent = rel_sec["entry_size"] or 8
    _require(rent == 8 and rel_sec["size"] % 8 == 0, "unsupported relocation table")
    relocs = []
    for i in range(rel_sec["size"] // 8):
        ro, ri = struct.unpack_from("<II", decoded, rel_sec["offset"] + i * 8)
        si, typ = ri >> 8, ri & 0xff
        if si < len(symbols):
            relocs.append((ro, symbols[si]["name"], typ))
    return decoded, blob, symbols, relocs


def decode_psp_vehicle_model(data: bytes, tools_dir: Path, model_name: str | None = None) -> Dict[str, object]:
    maybe_refpack, _, _cstring = import_zeebo_tools(tools_dir)
    _, blob, symbols, relocs = _parse_elf(data, maybe_refpack, _cstring)

    models = [s["name"].split(":::", 1)[1] for s in symbols if s["section"] != 0 and s["name"].startswith("__Model:::")]
    _require(models, "component has no __Model symbol")
    selected = model_name or sorted(models)[0]
    _require(selected in models, f"model not found: {selected}")

    supported = {"NFSCar_TextureShiny", "NFSCar_Window", "NFSCar_Gouraud"}
    descriptors = []
    for ro, symbol, typ in relocs:
        if typ == 2 and symbol in supported:
            _require(ro + 8 <= len(blob), "packet relocation outside .data")
            stream = struct.unpack_from("<I", blob, ro + 4)[0]
            _require(stream < ro <= len(blob), "invalid stream/descriptor range")
            descriptors.append((stream, ro, symbol))
    _require(descriptors, "component has no supported visual packets")

    vertices: List[Tuple[float, float, float, float, float, float, float, float]] = []
    groups = []
    indices: List[int] = []
    seen = set()

    for packet_index, (stream, descriptor, renderer) in enumerate(sorted(descriptors)):
        key = (stream, renderer)
        if key in seen:
            continue
        seen.add(key)
        payload = blob[stream:descriptor]
        _require(len(payload) >= 32, "PSP primitive packet too small")

        # PSP stream starts with an 8-byte platform prefix. The EAGL primitive
        # header then matches the Zeebo family. A variable list of 4-byte strip
        # descriptors (<u16 count, 04,04>) is terminated by 00 00 00 0b.
        marker = payload.find(b"\x00\x00\x00\x0b", 24)
        _require(marker >= 24 and marker % 4 == 0, "PSP strip table terminator not found")
        strip_counts: List[int] = []
        for off in range(24, marker, 4):
            count, a, b = struct.unpack_from("<HBB", payload, off)
            _require(a == 4 and b == 4, f"unexpected PSP strip descriptor {a:02x} {b:02x}")
            _require(count >= 3, "triangle strip shorter than 3 vertices")
            strip_counts.append(count)
        _require(strip_counts, "packet has no strips")

        record_start = stream + marker + 4
        total_vertices = sum(strip_counts)
        first_group_index = len(indices)
        first_group_vertex = len(vertices)
        strip_ranges = []

        if renderer in ("NFSCar_TextureShiny", "NFSCar_Window"):
            record_size = 16
            needed = record_start + total_vertices * record_size
            _require(needed <= descriptor, f"{renderer} records overlap descriptor")
            cursor = record_start
            local_first = len(vertices)
            for _ in range(total_vertices):
                u, v, nx, ny, nz, px, py, pz = struct.unpack_from("<8h", blob, cursor)
                cursor += 16
                vertices.append((
                    px / 8192.0, py / 8192.0, pz / 8192.0,
                    max(-1.0, min(1.0, nx / 32767.0)),
                    max(-1.0, min(1.0, ny / 32767.0)),
                    max(-1.0, min(1.0, nz / 32767.0)),
                    (u - 16384) / 512.0,
                    (v - 16384) / 512.0,
                ))
        else:
            record_size = 6
            needed = record_start + total_vertices * record_size
            _require(needed <= descriptor, "Gouraud records overlap descriptor")
            cursor = record_start
            local_first = len(vertices)
            for _ in range(total_vertices):
                px, py, pz = struct.unpack_from("<3h", blob, cursor)
                cursor += 6
                vertices.append((px / 8192.0, py / 8192.0, pz / 8192.0, 0.0, 0.0, 0.0, 0.0, 0.0))

        strip_base = local_first
        for strip_idx, count in enumerate(strip_counts):
            first_i = len(indices)
            tri = _strip_indices(strip_base, count)
            indices.extend(tri)
            strip_ranges.append({"first_index": first_i, "index_count": len(tri), "first_vertex": strip_base, "vertex_count": count})
            strip_base += count

        semantic, reflection_state = _state_info_for_packet(relocs, descriptor)
        groups.append({
            "name": f"{selected}_PACKET{packet_index:02d}",
            "renderer": renderer,
            "material_id": _material_for_packet(relocs, descriptor),
            "material_semantic": semantic,
            "car_reflection_state": reflection_state,
            "first_index": first_group_index,
            "index_count": len(indices) - first_group_index,
            "first_vertex": first_group_vertex,
            "vertex_count": total_vertices,
            "strip_count": len(strip_counts),
            "strip_counts": strip_counts,
            "strips": strip_ranges,
        })

    # Generate smooth normals only for Gouraud vertices so OBJ importers have a
    # usable normal stream. Textured/window packets preserve authored normals.
    acc = [[0.0, 0.0, 0.0] for _ in vertices]
    gouraud_vertex = [False] * len(vertices)
    for g in groups:
        if g["renderer"] != "NFSCar_Gouraud":
            continue
        start = g["first_vertex"]
        for i in range(start, start + g["vertex_count"]):
            gouraud_vertex[i] = True
        a0 = g["first_index"]
        a1 = a0 + g["index_count"]
        for j in range(a0, a1, 3):
            ia, ib, ic = indices[j:j+3]
            A, B, C = vertices[ia], vertices[ib], vertices[ic]
            ux, uy, uz = B[0]-A[0], B[1]-A[1], B[2]-A[2]
            vx, vy, vz = C[0]-A[0], C[1]-A[1], C[2]-A[2]
            nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
            l = math.sqrt(nx*nx+ny*ny+nz*nz)
            if l > 1e-12:
                nx, ny, nz = nx/l, ny/l, nz/l
                for ii in (ia, ib, ic):
                    acc[ii][0] += nx; acc[ii][1] += ny; acc[ii][2] += nz
    if any(gouraud_vertex):
        vv = list(vertices)
        for i, flag in enumerate(gouraud_vertex):
            if not flag: continue
            nx, ny, nz = acc[i]
            l = math.sqrt(nx*nx+ny*ny+nz*nz)
            if l <= 1e-12: nx, ny, nz = 0.0, 0.0, 1.0
            else: nx, ny, nz = nx/l, ny/l, nz/l
            x,y,z,_,_,_,u,v = vv[i]
            vv[i] = (x,y,z,nx,ny,nz,u,v)
        vertices = vv

    return {
        "model": selected,
        "vertices": vertices,
        "indices": indices,
        "groups": groups,
        "packet_count": len(groups),
        "strip_count": sum(g["strip_count"] for g in groups),
        "triangle_count": len(indices) // 3,
        "source": "PSP-EAGL",
    }


def safe_name(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", s)


def write_obj(mesh: Dict[str, object], obj_path: Path) -> None:
    obj_path.parent.mkdir(parents=True, exist_ok=True)
    mtl_path = obj_path.with_suffix(".mtl")
    verts = mesh["vertices"]
    indices = mesh["indices"]
    groups = mesh["groups"]

    materials = []
    for g in groups:
        key = g["material_id"] or g["renderer"]
        if key not in materials: materials.append(key)

    with mtl_path.open("w", encoding="utf-8", newline="\n") as f:
        for mat in materials:
            name = safe_name(str(mat))
            f.write(f"newmtl {name}\n")
            # Helpful neutral previews; no texture assumptions are made.
            if str(mat) == "1500": f.write("Kd 0.35 0.35 0.38\nKs 0.65 0.65 0.70\nNs 80\n")
            elif str(mat) == "1501": f.write("Kd 0.18 0.25 0.32\nKs 0.75 0.80 0.90\nNs 100\nd 0.55\n")
            elif str(mat) == "1001": f.write("Kd 0.08 0.08 0.09\nKs 0.08 0.08 0.08\nNs 8\n")
            else: f.write("Kd 0.55 0.55 0.55\nKs 0.20 0.20 0.20\nNs 20\n")
            f.write("\n")

    with obj_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(f"# NFS Carbon OTC PSP model decoded from EAGL/MIPS component\n")
        f.write(f"# vertices={len(verts)} triangles={mesh['triangle_count']} packets={mesh['packet_count']} strips={mesh['strip_count']}\n")
        f.write(f"mtllib {mtl_path.name}\n")
        f.write(f"o {safe_name(str(mesh['model']))}\n")
        for x,y,z,nx,ny,nz,u,v in verts:
            f.write(f"v {x:.9g} {y:.9g} {z:.9g}\n")
        for x,y,z,nx,ny,nz,u,v in verts:
            f.write(f"vt {u:.9g} {v:.9g}\n")
        for x,y,z,nx,ny,nz,u,v in verts:
            f.write(f"vn {nx:.9g} {ny:.9g} {nz:.9g}\n")

        for g in groups:
            f.write(f"g {safe_name(str(g['name']))}\n")
            if g.get("material_semantic"):
                f.write(f"# psp_material_semantic {g['material_semantic']}\n")
            if g.get("car_reflection_state"):
                f.write("# psp_state CarReflectionState\n")
            mat = safe_name(str(g["material_id"] or g["renderer"]))
            f.write(f"usemtl {mat}\n")
            # Keep every PSP strip independent. This is the critical difference
            # from the old experimental extractor which joined packet vertices
            # into a single strip.
            for strip_idx, sr in enumerate(g["strips"]):
                f.write(f"# strip {strip_idx} vertices={sr['vertex_count']}\n")
                a0 = sr["first_index"]
                a1 = a0 + sr["index_count"]
                for j in range(a0, a1, 3):
                    a,b,c = indices[j] + 1, indices[j+1] + 1, indices[j+2] + 1
                    f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def extract_tree(input_root: Path, output_root: Path, tools_dir: Path, include_splay: bool = False):
    report = {"source": str(input_root), "objects": 0, "failed": 0, "vertices": 0, "triangles": 0, "strips": 0, "errors": []}
    files = sorted(input_root.rglob("*.o"))
    for p in files:
        if not include_splay and p.stem.endswith("_splay"):
            continue
        try:
            mesh = decode_psp_vehicle_model(p.read_bytes(), tools_dir)
            rel_dir = p.parent.relative_to(input_root)
            suffix = "_SPLAY" if p.stem.endswith("_splay") else ""
            out = output_root / rel_dir / f"{safe_name(str(mesh['model']))}{suffix}.obj"
            write_obj(mesh, out)
            report["objects"] += 1
            report["vertices"] += len(mesh["vertices"])
            report["triangles"] += mesh["triangle_count"]
            report["strips"] += mesh["strip_count"]
        except Exception as exc:
            report["failed"] += 1
            report["errors"].append({"file": str(p.relative_to(input_root)), "error": f"{type(exc).__name__}: {exc}"})
    output_root.mkdir(parents=True, exist_ok=True)
    (output_root / "extract_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path, help="PSP recovered vehicle root containing car folders")
    ap.add_argument("output", type=Path)
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"), help="Port tools directory containing zeebo_vehicle.py")
    ap.add_argument("--include-splay", action="store_true")
    args = ap.parse_args()
    report = extract_tree(args.input, args.output, args.tools_dir, args.include_splay)
    print(json.dumps(report, indent=2))
    if report["failed"]:
        raise SystemExit(2)

if __name__ == "__main__":
    main()
