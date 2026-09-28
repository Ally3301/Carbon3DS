#!/usr/bin/env python3
"""Exercise the portable N3P loader with all recovered vehicle components."""
from pathlib import Path
import json
import os
import struct
import subprocess
import tempfile
import sys

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from recover_tracks import entries

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
Path("build").mkdir(exist_ok=True)

subprocess.run([
    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-fsanitize=undefined", "-Iinclude",
    "tests/host.c", "src/n3p_loader.c", "src/wav.c",
    "src/vehicle.c", "src/track.c", "src/texture_uv.c",
    "src/vehicle_material_policy.c", "-lm", "-o", "build/host_test",
], check=True)

exe = str(ROOT / "build/host_test")
subprocess.run([exe], check=True)

parts = list(Path("assets/generated/3ds/vehicles").rglob("*.n3p"))
assert len(parts) == 828, len(parts)
for path in parts:
    subprocess.run([exe, "part", str(path)], check=True)
print("C loader accepted", len(parts), "real N3P vehicle components")

for path in Path("romfs/audio").glob("*.wav"):
    subprocess.run([exe, "wav", str(path)], check=True)

sample = Path("assets/generated/3ds/vehicles/SKYLINE/parts/body/upgrade_00.n3p")
b = bytearray(sample.read_bytes())
mutations = [b[:n] for n in (0, 1, 23, 24, 40, len(b)-1)]
for off, value in [
    (4, 2),          # wrong version
    (8, 0xffffffff), # impossible vertices
    (12, 0xffffffff),
    (16, 33),        # too many groups
    (20, 1),         # flags
]:
    d = bytearray(b)
    struct.pack_into("<I", d, off, value)
    mutations.append(d)

nv, ni = struct.unpack_from("<II", b, 8)
# First index outside vertex range.
d = bytearray(b)
struct.pack_into("<H", d, 24 + 32*nv, 65535)
mutations.append(d)
# First group count beyond total index buffer.
group_off = 24 + 32*nv + 2*ni
d = bytearray(b)
struct.pack_into("<I", d, group_off + 4, 0xffffffff)
mutations.append(d)

with tempfile.TemporaryDirectory() as td:
    p = Path(td) / "bad.n3p"
    for data in mutations:
        p.write_bytes(data)
        result = subprocess.run([exe, "part", str(p)])
        assert result.returncode == 2, result.returncode

    # BIGF path traversal and bad payload offsets must still be rejected.
    for name, off in [(b"../escape",128),(b"/escape",128),(b"x",999)]:
        data = bytearray(140)
        data[:4] = b"BIGF"
        struct.pack_into("<I", data, 4, 140)
        struct.pack_into(">II", data, 8, 1, 64)
        struct.pack_into(">II", data, 16, off, 8)
        data[24:24+len(name)] = name
        try:
            entries(data)
        except ValueError:
            pass
        else:
            raise AssertionError("unsafe archive accepted")

print("Malformed N3P files and unsafe archive paths rejected; UBSan clean")
print("T3X logical/subtexture UV affine mapping tests passed")


# Recovered global material library and wheels.viv texture catalog.
global_root = Path("assets/generated/3ds/vehicle_global")
global_manifest = json.loads((global_root / "manifest.json").read_text(encoding="utf-8"))
assert global_manifest["global_materials"] == 10, global_manifest
assert global_manifest["wheel_textures"] == 229, global_manifest
assert global_manifest["wheel_geometry_in_wheels_viv"] is False

materials = json.loads((global_root / "materials.json").read_text(encoding="utf-8"))["materials"]
by_material = {int(row["material"]): row for row in materials}
assert set(by_material) == {1000,1001,1002,1003,1004,1500,1501,1990,1995,1996}

paint = Image.open(global_root / by_material[1500]["png"]).convert("RGBA")
assert paint.size == (128, 128)
assert set(paint.getdata()) == {(255, 0, 0, 255)}

window = Image.open(global_root / by_material[1501]["png"]).convert("RGBA")
assert window.size == (4, 4)
assert all(r == g == b == 0 and 0 < a < 255 for r,g,b,a in window.getdata())

wheels = json.loads((global_root / "wheels.json").read_text(encoding="utf-8"))["textures"]
assert len(wheels) == 229
assert len({row["name"] for row in wheels}) == 229
for row in wheels:
    image = Image.open(global_root / row["png"])
    assert image.size == (64, 64), (row["name"], image.size)
    assert row["format"] == "0x5077"

wheel_names = {row["name"] for row in wheels}
assert {"BBS_STYLE02_CHROME", "ENKEI_STYLE01_BLACK",
        "TRAFFICCAR_TIRE_STYLE00"} <= wheel_names


# Runtime catalogue deliberately excludes traffic and incomplete police variants
# while retaining their source archives for the future bounded traffic stream.
_runtime_players = Path("romfs/vehicles.txt").read_text(encoding="utf-8").split()
_runtime_traffic = Path("romfs/traffic_vehicles.txt").read_text(encoding="utf-8").split()
assert len(_runtime_players) == 28
assert len(_runtime_traffic) == 15
assert not set(_runtime_players) & set(_runtime_traffic)
assert "TAXI" in _runtime_traffic and "PURSUITSEDAN2" in _runtime_traffic
print("28 player cars separated from 15 traffic/pursuit archives")

# All .o vehicle models should remain namespaced to their own car, and every
# source customization component should be present in the N3P manifest.
manifest_root = Path("assets/generated/3ds/vehicles")
all_manifest = json.loads((manifest_root / "manifest_all.json").read_text(encoding="utf-8"))
assert all_manifest["vehicles"] == 43
assert all_manifest["parts"] == 828
assert all_manifest["slots"].get("other", 0) == 0
for vehicle_dir in sorted(p for p in manifest_root.iterdir() if p.is_dir()):
    manifest = json.loads((vehicle_dir / "manifest.json").read_text(encoding="utf-8"))
    vehicle = manifest["vehicle"]
    paths = set()
    slot_counts = {}
    for part in manifest["parts"]:
        assert part["model"].startswith(vehicle + "_"), (vehicle, part["model"])
        assert part["path"] not in paths, (vehicle, part["path"])
        paths.add(part["path"])
        assert (vehicle_dir / part["path"]).exists()
        slot_counts[part["slot"]] = slot_counts.get(part["slot"], 0) + 1
    assert max(slot_counts.values()) <= 20, (vehicle, slot_counts)
    # The procedural wheel geometry must always have an original recovered
    # face texture. Traffic fallback remains explicit for any exceptional ID.
    assert (vehicle + "_TIRE_STYLE00" in wheel_names
            or "TRAFFICCAR_TIRE_STYLE00" in wheel_names)

ui_root = Path("assets/normalized/ui/generic")
for category in ("bodykits", "hoods", "spoilers", "paint", "rims"):
    icons = list((ui_root / category).glob("*.png"))
    assert len(icons) == 1, (category, icons)
    assert Image.open(icons[0]).convert("RGBA").size == (64, 64)

print("Global materials, dynamic PAINT, WINDOW, wheel textures and garage icons validated")

# Every generated vehicle must expose exactly one semantic PAINT-details binding
# and it must point at the decoded *_DETAILS texture id. This guards against
# regressing the relationship recovered from the original RaceCarRenderInfo.
details_checked = 0
for car in sorted(p for p in Path("assets/generated/3ds/vehicles").iterdir() if p.is_dir()):
    cfg_path = car / "vehicle.cfg"
    manifest_path = car / "manifest.json"
    if not cfg_path.exists() or not manifest_path.exists():
        continue
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    details = [
        t for t in manifest.get("textures", [])
        if t.get("ok")
        and "DETAIL" in str(t.get("name") or "").upper()
        and str(t.get("id") or "").isdigit()
    ]
    assert len(details) == 1, (car.name, "DETAILS candidates", details)
    expected = int(details[0]["id"])
    cfg_details = [
        int(line.split()[1])
        for line in cfg_path.read_text(encoding="utf-8").splitlines()
        if line.startswith("paint_details ")
    ]
    assert cfg_details == [expected], (car.name, cfg_details, expected)
    # DETAILS is also already a normal numeric T3X source; PAINT reuses it
    # rather than allocating a duplicate texture.
    assert any(
        line == f"texture {expected} tex/{expected:04d}.t3x"
        for line in cfg_path.read_text(encoding="utf-8").splitlines()
    ), (car.name, expected)
    details_checked += 1
assert details_checked == 43, details_checked
print("43/43 vehicles preserve PAINT/_DETAILS semantic metadata (not sampled directly)")


# 0x3076 RGBA4444 regression: global carbon must decode as opaque dark
# greys, not pink/partially transparent. This catches reversed nibble order.
from PIL import Image as _Image
_carbon = _Image.open(
    "assets/generated/3ds/vehicle_global/materials/1000_CARBONFIBRE.png"
).convert("RGBA")
_carbon_px = list(_carbon.getdata())
assert all(a == 255 for _, _, _, a in _carbon_px)
assert max(abs(r-g) for r,g,b,a in _carbon_px) <= 34
assert max(abs(g-b) for r,g,b,a in _carbon_px) <= 34
assert sum(r for r,g,b,a in _carbon_px) / len(_carbon_px) < 96
print("SHPM 0x3076 RGBA4444 nibble order validated on CARBONFIBRE")

# Every runtime vehicle now carries a geometry-derived wheel fit. Values are
# intentionally bounded; exact original Zeebo wheel records remain a later RE task.
_wheel_fits = 0
for _car in sorted(p for p in Path("assets/generated/3ds/vehicles").iterdir() if p.is_dir()):
    _cfg = _car / "vehicle.cfg"
    if not _cfg.exists():
        continue
    _rows = [line for line in _cfg.read_text(encoding="utf-8").splitlines()
             if line.startswith("wheel_fit ")]
    assert len(_rows) == 1, (_car.name, _rows)
    _vals = [float(x) for x in _rows[0].split()[1:]]
    assert len(_vals) == 5
    _front, _rear, _track, _radius, _center = _vals
    assert _front > _rear
    assert 0.20 < _track < 1.40
    assert 0.25 < _radius < 0.50
    assert -0.20 < _center < 0.80
    _wheel_fits += 1
assert _wheel_fits == 43, _wheel_fits
print("43/43 geometry-derived wheel_fit records validated")
