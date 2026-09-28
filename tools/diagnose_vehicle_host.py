#!/usr/bin/env python3
"""Host-side vehicle asset diagnostics.

Purpose:
- compare generated N3P geometry/UV/normals against normalized OBJ;
- validate slot/index/path relationships without building a .3dsx;
- inventory renderer/material classes per vehicle;
- highlight special non-player material paths such as 9999;
- optionally produce simple solid-slot previews via matplotlib if available.

This tool never edits normalized/source assets.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NORM = ROOT / "assets" / "normalized" / "vehicles"
GEN = ROOT / "assets" / "generated" / "3ds" / "vehicles"

HEADER = struct.Struct("<4sIIIII")
VERTEX = struct.Struct("<8f")
GROUP = struct.Struct("<IIHBB")

def read_n3p(path: Path):
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("short N3P")
    magic, version, nv, ni, ng, flags = HEADER.unpack_from(data, 0)
    if (magic, version, flags) != (b"N3P1", 1, 0):
        raise ValueError("bad N3P header")
    off = HEADER.size
    verts = [VERTEX.unpack_from(data, off + i*VERTEX.size) for i in range(nv)]
    off += nv*VERTEX.size
    indices = struct.unpack_from("<" + "H"*ni, data, off)
    off += 2*ni
    groups = [GROUP.unpack_from(data, off + i*GROUP.size) for i in range(ng)]
    return verts, indices, groups

def read_obj(path: Path):
    pos, uv, nrm = [], [], []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                a = line.split()
                pos.append(tuple(map(float, a[1:4])))
            elif line.startswith("vt "):
                a = line.split()
                uv.append(tuple(map(float, a[1:3])))
            elif line.startswith("vn "):
                a = line.split()
                nrm.append(tuple(map(float, a[1:4])))
    return pos, uv, nrm

def transformed_normal(n):
    nx, ny, nz = n
    out = (nx, nz, -ny)
    length = math.sqrt(sum(q*q for q in out))
    if length < 1e-10:
        return (0.0, 1.0, 0.0)
    return tuple(q/length for q in out)

def compare_part(obj: Path, n3p: Path):
    pos, uv, nrm = read_obj(obj)
    verts, indices, groups = read_n3p(n3p)
    if not (len(pos) == len(uv) == len(nrm) == len(verts)):
        return {"ok": False, "reason": "vertex-array length mismatch"}

    max_pos = max_uv = max_nrm = 0.0
    for p, t, n, v in zip(pos, uv, nrm, verts):
        ep = (p[0], p[2], -p[1])
        et = (t[0], 1.0-t[1])  # normalized OBJ flips V for OBJ viewers
        en = transformed_normal(n)
        max_pos = max(max_pos, math.dist(ep, v[:3]))
        max_uv = max(max_uv, math.dist(et, v[3:5]))
        max_nrm = max(max_nrm, math.dist(en, v[5:8]))
    return {
        "ok": max_pos < 1e-5 and max_uv < 1e-6 and max_nrm < 1e-5,
        "vertices": len(verts),
        "triangles": len(indices)//3,
        "groups": len(groups),
        "max_position_error": max_pos,
        "max_uv_error": max_uv,
        "max_normal_error": max_nrm,
    }

def classify_vehicle(body_materials: set[str], slots: Counter):
    if "1500" in body_materials:
        return "customizable_racecar"
    if "9999" in body_materials:
        return "special_dynamic_material_path"
    if slots == Counter({"body": 1}):
        return "fixed_traffic_direct_texture"
    return "fixed_special_direct_materials"

def audit():
    report = {
        "schema": "nfs-carbon-host-vehicle-audit-v1",
        "vehicles": [],
        "summary": {},
    }
    all_errors = []
    totals = Counter()
    max_slot = Counter()

    for car_dir in sorted(p for p in NORM.iterdir() if p.is_dir()):
        car = car_dir.name
        norm = json.loads((car_dir/"vehicle.json").read_text(encoding="utf-8"))
        gen_dir = GEN/car
        gen = json.loads((gen_dir/"manifest.json").read_text(encoding="utf-8"))
        gmap = {p["model"]: p for p in gen["parts"]}
        slots = Counter(p["slot"] for p in gen["parts"])
        for slot, count in slots.items():
            max_slot[slot] = max(max_slot[slot], count)

        body_materials = set()
        renderer_counts = Counter()
        comparisons = []
        for part in norm["parts"]:
            gp = gmap.get(part["model"])
            if not gp:
                comparisons.append({"model": part["model"], "ok": False,
                                    "reason": "missing generated model"})
                continue
            result = compare_part(car_dir/part["obj"], gen_dir/gp["path"])
            result.update({"model": part["model"], "slot": part["slot"],
                           "index": part["index"], "n3p": gp["path"]})
            comparisons.append(result)
            if not result["ok"]:
                all_errors.append((car, part["model"], result))
            if part["slot"] == "body":
                body_materials.update(g.get("material_id") or "none"
                                      for g in part.get("groups", []))
            for g in part.get("groups", []):
                renderer_counts[g["renderer"]] += 1

        kind = classify_vehicle(body_materials, slots)
        totals[kind] += 1
        report["vehicles"].append({
            "vehicle": car,
            "class": kind,
            "slots": dict(slots),
            "body_materials": sorted(body_materials),
            "local_textures": [
                {"id": t.get("id"), "name": t.get("name")}
                for t in norm.get("textures", []) if t.get("ok")
            ],
            "renderer_packets": dict(renderer_counts),
            "comparisons": comparisons,
        })

    report["summary"] = {
        "vehicles": len(report["vehicles"]),
        "parts_compared": sum(len(v["comparisons"]) for v in report["vehicles"]),
        "comparison_failures": len(all_errors),
        "vehicle_classes": dict(totals),
        "max_options_per_slot": dict(max_slot),
        "runtime_option_limit": 20,
        "option_limit_overflow": any(v > 20 for v in max_slot.values()),
    }
    return report

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=Path, default=Path("vehicle_host_audit.json"))
    ap.add_argument("--car")
    args = ap.parse_args()

    report = audit()
    args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))

    if args.car:
        row = next((v for v in report["vehicles"] if v["vehicle"] == args.car), None)
        if row is None:
            raise SystemExit(f"unknown vehicle: {args.car}")
        print(json.dumps({k: row[k] for k in
              ("vehicle","class","slots","body_materials","local_textures",
               "renderer_packets")}, indent=2))

if __name__ == "__main__":
    main()
