
#!/usr/bin/env python3
"""Rebuild the native 3DS vehicle-part assets from original Zeebo vehicle VIVs.

This is the replacement for the old N3M pipeline. It:
  1. validates/extracts BIG4/BIGF vehicle VIVs,
  2. decodes ELF/MIPS EAGL packets using zeebo_vehicle.py,
  3. writes one runtime-ready N3P1 mesh per customization component,
  4. extracts SHPM textures to PNG using zeebo_shpm.py,
  5. writes a human-readable vehicle.cfg consumed by the 3DS runtime.

N3P1 stores vertices directly in the 3DS runtime coordinate system:
    Zeebo (x, y, z) -> 3DS world-model local (x, z, -y)
and GPU memory order is position, UV, normal.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import struct
import tempfile
from pathlib import Path, PurePosixPath

from zeebo_vehicle import inspect_component_file, decode_vehicle_model
from zeebo_shpm import extract_best

HEADER = struct.Struct("<4sIIIII")
VERTEX = struct.Struct("<8f")      # pos3, uv2, normal3
GROUP = struct.Struct("<IIHBB")    # first index, count, material id, renderer, reserved
RENDERER_ID = {
    "NFSCar_TextureShiny": 0,
    "NFSCar_Window": 1,
    "NFSCar_Gouraud": 2,
}

def big_entries(data: bytes):
    if len(data) < 16 or data[:4] not in (b"BIG4", b"BIGF"):
        raise ValueError("not a BIG4/BIGF archive")
    total = struct.unpack_from("<I", data, 4)[0]
    count, directory_end = struct.unpack_from(">II", data, 8)
    if total != len(data) or not 16 <= directory_end <= len(data):
        raise ValueError("invalid BIG header")
    pos = 16
    result = []
    seen = set()
    for _ in range(count):
        if pos + 8 > directory_end:
            raise ValueError("truncated BIG directory")
        off, size = struct.unpack_from(">II", data, pos)
        nul = data.find(b"\0", pos + 8, directory_end)
        if nul < 0:
            raise ValueError("unterminated BIG name")
        name = data[pos + 8:nul].decode("ascii")
        safe = PurePosixPath(name.replace("\\", "/"))
        if safe.is_absolute() or ".." in safe.parts or len(safe.parts) != 1:
            raise ValueError(f"unsafe BIG name {name!r}")
        if name in seen:
            raise ValueError(f"duplicate BIG entry {name!r}")
        seen.add(name)
        if off < directory_end or size > len(data) - off:
            raise ValueError(f"invalid BIG payload {name!r}")
        result.append((name, off, size))
        pos = nul + 1
    return result

def classify(vehicle: str, model: str):
    suffix = model[len(vehicle)+1:] if model.startswith(vehicle + "_") else model
    patterns = (
        (r"UPGRADE(\d+)_BODY_B$", "body", "upgrade"),
        (r"UPGRADE(\d+)_BASE_B$", "base", "upgrade"),
        (r"HOOD_STYLE(\d+)_B$", "hood", "style"),
        (r"SPOILER_STYLE(\d+)_B$", "spoiler", "style"),
        (r"UPGRADE(\d+)_CREWTAG_SIDES_B$", "crewtag_sides", "upgrade"),
    )
    for pattern, slot, kind in patterns:
        m = re.match(pattern, suffix)
        if m:
            return slot, kind, int(m.group(1)), suffix
    fixed = {
        "CREWTAG_HOOD_B": ("crewtag_hood", "fixed"),
        "BODY_B": ("body", "fixed"),
        "BASE_B": ("base", "fixed"),
        "LEGEND_HOOD_B": ("hood", "legend"),
        "LEGEND_SPOILER_B": ("spoiler", "legend"),
    }
    if suffix in fixed:
        slot, kind = fixed[suffix]
        return slot, kind, -1, suffix
    return "other", "unknown", -1, suffix

def safe_name(value: str):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)

def runtime_path(slot: str, kind: str, index: int, suffix: str):
    if index >= 0:
        name = f"{kind}_{index:02d}.n3p"
    else:
        name = f"{kind}_{safe_name(suffix)}.n3p"
    return Path("parts") / slot / name

def numeric_material(value: str):
    if not value:
        return 0xFFFF
    if value.isdigit():
        number = int(value, 10)
        if 0 <= number <= 0xFFFE:
            return number
    return 0xFFFF

def write_n3p(mesh, output: Path):
    vertices = []
    for x, y, z, nx, ny, nz, u, v in mesh["vertices"]:
        # Same orientation that was proven by the old runtime, but performed
        # offline now. This makes N3P directly memcpy-able into linear memory.
        px, py, pz = x, z, -y
        nnx, nny, nnz = nx, nz, -ny
        length = math.sqrt(nnx*nnx + nny*nny + nnz*nnz)
        if not math.isfinite(length) or length < 1e-10:
            nnx, nny, nnz = 0.0, 1.0, 0.0
        else:
            nnx, nny, nnz = nnx/length, nny/length, nnz/length
        values = (px, py, pz, u, v, nnx, nny, nnz)
        if not all(math.isfinite(q) for q in values):
            raise ValueError("non-finite N3P vertex")
        vertices.append(values)

    indices = mesh["indices"]
    if not vertices or len(vertices) > 65535:
        raise ValueError("N3P vertex count out of range")
    if not indices or len(indices) % 3 or len(indices) > 196608:
        raise ValueError("N3P index count out of range")
    if any(i < 0 or i >= len(vertices) or i > 65535 for i in indices):
        raise ValueError("N3P index out of range")
    groups = mesh["groups"]
    if not groups or len(groups) > 32:
        raise ValueError("N3P group count out of range")

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as f:
        f.write(HEADER.pack(b"N3P1", 1, len(vertices), len(indices), len(groups), 0))
        for vertex in vertices:
            f.write(VERTEX.pack(*vertex))
        f.write(struct.pack("<" + "H"*len(indices), *indices))
        covered = 0
        for group in groups:
            start = int(group["first_index"])
            count = int(group["index_count"])
            if start != covered or count <= 0 or count % 3:
                raise ValueError("N3P groups must cover indices in order")
            renderer = RENDERER_ID[group["renderer"]]
            material = numeric_material(group.get("material_id", ""))
            f.write(GROUP.pack(start, count, material, renderer, 0))
            covered += count
        if covered != len(indices):
            raise ValueError("N3P group coverage mismatch")

def extract_archive(viv: Path, root: Path):
    data = viv.read_bytes()
    dst = root / viv.stem
    dst.mkdir(parents=True, exist_ok=True)
    for name, off, size in big_entries(data):
        (dst / name).write_bytes(data[off:off+size])
    return dst

def build_vehicle(car_dir: Path, output_root: Path):
    vehicle = car_dir.name
    out = output_root / vehicle
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    msh = next(iter(sorted(car_dir.glob("*.msh"))), None)
    textures = []
    if msh is not None:
        textures = extract_best(msh, out / "textures")

    parts = []
    for component in sorted(car_dir.glob("*.o")):
        info = inspect_component_file(component)
        for model in info.get("models", []):
            mesh = decode_vehicle_model(component.read_bytes(), model)
            slot, kind, index, suffix = classify(vehicle, model)

            # The Zeebo archive contains MUSTANG67 UPGRADE02 geometry, but it
            # is not a valid selectable body kit for this vehicle in Carbon.
            # Keeping it in the runtime menu exposes mismatched body/base data.
            if vehicle.upper() == "MUSTANG67" and slot in {"body", "base"} and index == 2:
                continue

            if slot == "other":
                # Preserve unknown components rather than silently discarding
                # them. The runtime ignores this slot until classified.
                slot = "other"
            rel = runtime_path(slot, kind, index, suffix)
            write_n3p(mesh, out / rel)
            parts.append({
                "model": model,
                "source_component": component.name,
                "slot": slot,
                "kind": kind,
                "index": index,
                "suffix": suffix,
                "path": rel.as_posix(),
                "vertices": len(mesh["vertices"]),
                "triangles": mesh["triangle_count"],
                "packets": len(mesh["groups"]),
                "renderers": sorted({g["renderer"] for g in mesh["groups"]}),
                "materials": sorted({g.get("material_id") or "none" for g in mesh["groups"]}),
            })

    # Stable human-readable runtime manifest. Paths have no spaces.
    lines = ["N3VCFG 1", f"id {vehicle}"]

    # Targeted jogo.c analysis of the RaceCarRenderInfo PAINT constructor
    # shows that it looks up a car-specific "_details" image separately from
    # the global "paint" source.  Preserve that semantic relationship instead
    # of pretending DETAILS is a normal mesh material.
    detail_candidates = [
        tex for tex in textures
        if tex.get("ok")
        and "DETAIL" in str(tex.get("name") or "").upper()
        and numeric_material(str(tex.get("id") or "")) != 0xFFFF
    ]
    if len(detail_candidates) == 1:
        detail_material = numeric_material(str(detail_candidates[0]["id"]))
        lines.append(f"paint_details {detail_material}")

    for tex in textures:
        if not tex.get("ok"):
            continue
        material = numeric_material(tex["id"])
        if material == 0xFFFF:
            continue
        # tex3ds output is generated by pack_runtime.py from the PNG source.
        lines.append(f"texture {material} tex/{material:04d}.t3x")
    for p in sorted(parts, key=lambda x:(x["slot"], x["index"], x["model"])):
        lines.append(f"part {p['slot']} {p['index']} {p['path']} {p['model']}")
    (out / "vehicle.cfg").write_text("\n".join(lines) + "\n", encoding="utf-8")

    manifest = {
        "vehicle": vehicle,
        "source_msh": msh.name if msh else None,
        "textures": textures,
        "paint_details_material": (
            numeric_material(str(detail_candidates[0]["id"]))
            if len(detail_candidates) == 1 else None
        ),
        "paint_details_name": (
            detail_candidates[0].get("name")
            if len(detail_candidates) == 1 else None
        ),
        "parts": parts,
        "n3p_version": 1,
        "coordinate_conversion": "(x,y,z)->(x,z,-y)",
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("viv_dir", type=Path, help="Directory containing the original vehicle .viv files")
    ap.add_argument("output", type=Path, help="Destination assets/generated/3ds/vehicles directory")
    args = ap.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    manifests = []
    with tempfile.TemporaryDirectory(prefix="carbon_vehicles_") as tmp:
        recovered = Path(tmp)
        for viv in sorted(args.viv_dir.glob("*.viv")):
            if viv.stem.lower() == "wheels":
                # Wheels are a different MSH asset family and are handled later.
                continue
            car_dir = extract_archive(viv, recovered)
            manifests.append(build_vehicle(car_dir, args.output))

    summary = {
        "vehicles": len(manifests),
        "parts": sum(len(m["parts"]) for m in manifests),
        "textures": sum(sum(1 for t in m["textures"] if t.get("ok")) for m in manifests),
        "slots": {},
    }
    for m in manifests:
        for part in m["parts"]:
            summary["slots"][part["slot"]] = summary["slots"].get(part["slot"], 0) + 1
    (args.output / "manifest_all.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))

if __name__ == "__main__":
    main()
