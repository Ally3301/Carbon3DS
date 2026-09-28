#!/usr/bin/env python3
"""Derive a conservative wheel placement/radius from the stock vehicle mesh.

This does NOT claim to recover the original Zeebo wheel transform table.
It fits circular wheel-arch evidence in BODY+BASE and writes a stable
geometry-derived approximation into vehicle.cfg:

    wheel_fit <front_x> <rear_x> <half_track> <radius> <center_y>

The original game has a stronger per-car 4x0x30 wheel-record path exposed by
jogo.c (RideInfo/CarRenderInfo). Until that binary table is recovered, this
keeps the procedural 3DS wheels matched to the actual recovered car geometry
instead of a single length-based percentage.
"""
from __future__ import annotations

import argparse
import itertools
import math
import re
import struct
from pathlib import Path


def load_vertices(path: Path):
    data = path.read_bytes()
    if len(data) < 24 or data[:4] != b"N3P1":
        raise ValueError(f"not N3P1: {path}")
    _, vcount, _, _, _ = struct.unpack_from("<IIIII", data, 4)
    end = 24 + vcount * 32
    if end > len(data):
        raise ValueError(f"truncated N3P vertices: {path}")
    return [struct.unpack_from("<8f", data, 24 + i * 32) for i in range(vcount)]


def parse_stock_parts(cfg: Path):
    selected = {}
    fixed = {}
    for line in cfg.read_text(encoding="utf-8").splitlines():
        m = re.match(r"part\s+(body|base)\s+(-?\d+)\s+(\S+)", line)
        if not m:
            continue
        slot, idx, rel = m.group(1), int(m.group(2)), m.group(3)
        if idx == 0:
            selected[slot] = rel
        elif idx == -1:
            fixed[slot] = rel
    for slot, rel in fixed.items():
        selected.setdefault(slot, rel)
    return selected


def circle3(a, b, c):
    x1, y1 = a
    x2, y2 = b
    x3, y3 = c
    d = 2.0 * (x1 * (y2-y3) + x2 * (y3-y1) + x3 * (y1-y2))
    if abs(d) < 1e-7:
        return None
    ux = ((x1*x1+y1*y1)*(y2-y3) + (x2*x2+y2*y2)*(y3-y1)
          + (x3*x3+y3*y3)*(y1-y2)) / d
    uy = ((x1*x1+y1*y1)*(x3-x2) + (x2*x2+y2*y2)*(x1-x3)
          + (x3*x3+y3*y3)*(x2-x1)) / d
    return ux, uy, math.hypot(x1-ux, y1-uy)


def fit_arch(points, guess_x, ymin, ymax, half_width):
    candidates = sorted({
        (round(v[0], 5), round(v[1], 5))
        for v in points
        if abs(v[2]) > half_width * 0.72
        and abs(v[0] - guess_x) < 0.65
        and v[1] < ymin + 0.70 * (ymax-ymin)
    })

    # Keep the exhaustive triplet search deterministic and small.
    if len(candidates) > 50:
        step = max(1, len(candidates) // 50)
        candidates = candidates[::step]

    best = None
    for a, b, c in itertools.combinations(candidates, 3):
        fit = circle3(a, b, c)
        if fit is None:
            continue
        cx, cy, radius = fit
        if not 0.25 < radius < 0.48:
            continue
        if abs(cx - guess_x) > 0.30:
            continue
        if not ymin - 0.08 < cy < ymin + 0.30:
            continue

        errors = [abs(math.hypot(x-cx, y-cy) - radius) for x, y in candidates]
        inliers = sum(e < 0.025 for e in errors)
        score = (inliers, -sum(errors))
        if best is None or score > best[0]:
            best = (score, cx, cy, radius)

    return best[1:] if best and best[0][0] >= 5 else None


def derive(vehicle_dir: Path):
    cfg = vehicle_dir / "vehicle.cfg"
    parts = parse_stock_parts(cfg)
    points = []
    for slot in ("body", "base"):
        rel = parts.get(slot)
        if rel:
            points.extend(load_vertices(vehicle_dir / rel))
    if not points:
        return None

    xs = [v[0] for v in points]
    ys = [v[1] for v in points]
    zs = [v[2] for v in points]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    zmin, zmax = min(zs), max(zs)
    length = xmax - xmin
    half_width = max(abs(zmin), abs(zmax))

    rear_guess = xmin + length * 0.205
    front_guess = xmax - length * 0.205
    rear = fit_arch(points, rear_guess, ymin, ymax, half_width)
    front = fit_arch(points, front_guess, ymin, ymax, half_width)

    fallback_radius = max(0.31, min(0.46, length * 0.082))
    rear_x = rear[0] if rear else rear_guess
    front_x = front[0] if front else front_guess
    rear_radius = rear[2] if rear else fallback_radius
    front_radius = front[2] if front else fallback_radius
    rear_y = rear[1] if rear else ymin + rear_radius * 0.94
    front_y = front[1] if front else ymin + front_radius * 0.94
    half_track = (zmax - zmin) * 0.49
    # old aggregate fit remains for backward-compatible consumers
    radius = (front_radius + rear_radius) * 0.5
    center_y = (front_y + rear_y) * 0.5
    return front_x, rear_x, half_track, radius, center_y, front_radius, rear_radius, front_y, rear_y


def patch_cfg(cfg: Path, fit):
    lines = [
        line for line in cfg.read_text(encoding="utf-8").splitlines()
        if not line.startswith("wheel_fit ")
    ]
    axle = "wheel_fit_axles " + " ".join(f"{x:.6f}" for x in (fit[5], fit[6], fit[7], fit[8]))
    lines = [line for line in lines if not line.startswith("wheel_fit_axles ")]
    value = "wheel_fit " + " ".join(f"{x:.6f}" for x in fit[:5])
    insert_at = 2 if len(lines) >= 2 else len(lines)
    lines.insert(insert_at, value)
    lines.insert(insert_at + 1, axle)
    cfg.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vehicles", type=Path)
    args = ap.parse_args()

    count = 0
    for vehicle in sorted(p for p in args.vehicles.iterdir() if p.is_dir()):
        cfg = vehicle / "vehicle.cfg"
        if not cfg.exists():
            continue
        fit = derive(vehicle)
        if fit is None:
            continue
        patch_cfg(cfg, fit)
        count += 1
        print(
            f"{vehicle.name:18} front={fit[0]: .3f} rear={fit[1]: .3f} "
            f"track={fit[2]:.3f} radius={fit[3]:.3f} diameter={fit[3]*2:.3f}"
        )
    print(f"wheel_fit written for {count} vehicles")


if __name__ == "__main__":
    main()
