#!/usr/bin/env python3
"""Recover the global vehicle materials and wheel texture catalog.

The vehicle component VIVs only contain car-local SHPM textures. Carbon also
uses global texture libraries:
  race_car_common.msh   -> material ids 1000..1004
  race_car_modable.msh  -> 1500 PAINT, 1501 WINDOW
  wheel_modable.msh     -> 1990 DUMMY_WHEEL
  wheel_common.msh      -> 1995 TIRE_BACK, 1996 CALIPER

wheels.viv is a BIGF containing 229 RefPack-compressed SHPM files.  It contains
textures, not mesh geometry. Each file resolves to one canonical wheel/tire
texture name such as SKYLINE_TIRE_STYLE00 or BBS_STYLE02_CHROME.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import tempfile
from pathlib import Path, PurePosixPath

from zeebo_shpm import extract_best


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
            raise ValueError("unterminated BIG filename")
        name = data[pos+8:nul].decode("ascii")
        safe = PurePosixPath(name.replace("\\", "/"))
        if safe.is_absolute() or ".." in safe.parts or len(safe.parts) != 1:
            raise ValueError(f"unsafe BIG filename {name!r}")
        if name in seen:
            raise ValueError(f"duplicate BIG entry {name!r}")
        seen.add(name)
        if off < directory_end or size > len(data) - off:
            raise ValueError(f"invalid BIG payload {name!r}")
        result.append((name, off, size))
        pos = nul + 1
    return result


def _safe(value: str):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def _material_role(material: int, name: str):
    return {
        1000: "carbon_fibre",
        1001: "car_bottom",
        1002: "driver",
        1003: "mesh",
        1004: "legend_carbon",
        1500: "paint_dynamic",
        1501: "window",
        1990: "dummy_wheel",
        1995: "tire_back",
        1996: "caliper",
    }.get(material, name.lower())


def build_materials(global_msh_dir: Path, output: Path):
    out = output / "materials"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    rows = []
    expected = (
        "race_car_common.msh",
        "race_car_modable.msh",
        "wheel_modable.msh",
        "wheel_common.msh",
    )
    for filename in expected:
        source = global_msh_dir / filename
        if not source.exists():
            raise FileNotFoundError(source)
        with tempfile.TemporaryDirectory(prefix="carbon_global_") as td:
            extracted = extract_best(source, Path(td))
            for item in extracted:
                if not item.get("ok"):
                    raise ValueError(
                        f"{filename} texture {item.get('id')} failed: "
                        f"{item.get('error')}"
                    )
                if not str(item["id"]).isdigit():
                    raise ValueError(f"non-numeric global material id: {item['id']}")
                material = int(item["id"])
                src_png = Path(td) / item["file"]
                name = item.get("name") or item["id"]
                dst_name = f"{material:04d}_{_safe(name)}.png"
                shutil.copy2(src_png, out / dst_name)
                rows.append({
                    "material": material,
                    "name": name,
                    "role": _material_role(material, name),
                    "source": filename,
                    "png": f"materials/{dst_name}",
                    "format": item.get("format"),
                    "width": item.get("width"),
                    "height": item.get("height"),
                    "variant": item.get("variant"),
                })

    rows.sort(key=lambda row: row["material"])
    (output / "materials.json").write_text(
        json.dumps({"materials": rows}, indent=2) + "\n",
        encoding="utf-8",
    )
    return rows


def _wheel_kind(name: str):
    if name.endswith("_TIRE_STYLE00") or name.endswith("_TIRE_LEGEND"):
        return "stock_tire"
    if "_STYLE" in name:
        return "custom_rim"
    if name == "DUMMY_WHEEL":
        return "dummy"
    return "wheel_texture"


def build_wheels(wheels_viv: Path, output: Path):
    out = output / "wheels"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    data = wheels_viv.read_bytes()
    rows = []
    names = set()

    with tempfile.TemporaryDirectory(prefix="carbon_wheels_") as td:
        td = Path(td)
        for archive_name, off, size in big_entries(data):
            source = td / archive_name
            source.write_bytes(data[off:off+size])
            tex_dir = td / "decoded"
            decoded = extract_best(source, tex_dir)
            if len(decoded) != 1 or not decoded[0].get("ok"):
                raise ValueError(f"wheel SHPM {archive_name} did not decode cleanly")
            item = decoded[0]
            canonical = item.get("name")
            if not canonical:
                raise ValueError(f"wheel SHPM {archive_name} has no canonical name")
            if canonical in names:
                raise ValueError(f"duplicate wheel texture name {canonical}")
            names.add(canonical)

            dst_name = _safe(canonical) + ".png"
            shutil.copy2(tex_dir / item["file"], out / dst_name)
            rows.append({
                "name": canonical,
                "kind": _wheel_kind(canonical),
                "source_entry": archive_name,
                "entry_id": item["id"],
                "png": f"wheels/{dst_name}",
                "format": item.get("format"),
                "width": item.get("width"),
                "height": item.get("height"),
                "variant": item.get("variant"),
            })

    rows.sort(key=lambda row: row["name"])
    (output / "wheels.json").write_text(
        json.dumps({"textures": rows}, indent=2) + "\n",
        encoding="utf-8",
    )
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("global_msh_dir", type=Path)
    ap.add_argument("wheels_viv", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    materials = build_materials(args.global_msh_dir, args.output)
    wheels = build_wheels(args.wheels_viv, args.output)
    summary = {
        "global_materials": len(materials),
        "wheel_textures": len(wheels),
        "wheel_geometry_in_wheels_viv": False,
        "formats": sorted({
            row["format"] for row in materials + wheels if row.get("format")
        }),
    }
    (args.output / "manifest.json").write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
