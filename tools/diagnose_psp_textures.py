#!/usr/bin/env python3
"""Audit PSP SHPM CLUT decoding visually and from palette statistics.

The PSP vehicle extractor historically assumed palette bytes are RGBA.  This
program reads every recovered ``*.msh`` directly, decodes its CLUT pixels once,
and writes a contact sheet for the six possible RGB byte orders while retaining
byte 3 as alpha. The report records the actual SHPM header and scores each
palette byte as a possible coverage/alpha channel.

It intentionally does *not* alter runtime assets.  Use the contact sheets and
``report.json`` to choose a decoder rule, then change the extractor rather
than adding car-specific colour fixes in pack_runtime.py.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
from zeebo_vehicle import maybe_refpack  # noqa: E402

CLUT4 = 0x105C
CLUT8 = 0x105D
SWIZZLED = 0x2000
# Colour-only candidates always retain byte 3 as alpha. Palette statistics
# across all recovered cars establish it as the coverage lane; testing it as a
# colour byte makes a candidate look artificially transparent.
ORDERS = {
    "RGBA (A=byte 3)": (0, 1, 2, 3),
    "BGRA (A=byte 3)": (2, 1, 0, 3),
    "GBRA (A=byte 3)": (1, 2, 0, 3),
    "GRBA (A=byte 3)": (1, 0, 2, 3),
    "BRGA (A=byte 3)": (2, 0, 1, 3),
    "RBGA (A=byte 3)": (0, 2, 1, 3),
}
FONT = ImageFont.load_default()


def unswizzle(data: bytes, width_bytes: int, height: int) -> bytes:
    if width_bytes < 16 or height < 8:
        return data[: width_bytes * height]
    expected = width_bytes * height
    if width_bytes % 16 or len(data) < expected:
        raise ValueError("invalid swizzled payload")
    out = bytearray(expected)
    blocks = width_bytes // 16
    for y in range(height):
        for x in range(width_bytes):
            source = (x // 16 + (y // 8) * blocks) * 128 + (y & 7) * 16 + (x & 15)
            out[y * width_bytes + x] = data[source]
    return bytes(out)


def parse_shpm(blob: bytes) -> Iterable[tuple[str, bytes]]:
    raw = maybe_refpack(blob)
    if raw[:4] != b"SHPM":
        raise ValueError("decoded source is not SHPM")
    count = struct.unpack_from("<I", raw, 8)[0]
    entries = []
    for index in range(count):
        entry = 16 + index * 8
        ident = raw[entry : entry + 4].decode("latin1", "replace").rstrip("\0 ")
        entries.append((ident or f"{index:04d}", struct.unpack_from("<I", raw, entry + 4)[0]))
    for index, (ident, start) in enumerate(entries):
        end = entries[index + 1][1] if index + 1 < len(entries) else len(raw)
        yield ident, raw[start:end]


def name_from_footer(block: bytes) -> str:
    marker = b"Name="
    at = block.find(marker)
    if at < 0:
        return "unnamed"
    return block[at + len(marker) :].split(b",", 1)[0].split(b"\0", 1)[0].decode("latin1", "replace")


def decode_indices(block: bytes):
    fmt, aux, width, height, padded_w, padded_h, flags0, flags1 = struct.unpack_from("<8H", block, 0)
    if fmt not in (CLUT4, CLUT8):
        raise ValueError(f"unsupported format 0x{fmt:04X}")
    swizzled = bool(flags0 & SWIZZLED)
    if fmt == CLUT4:
        row_bytes = (width + 1) // 2
        palette_count = 16
    else:
        row_bytes = width
        palette_count = 256
    storage_row = (row_bytes + 15) & ~15 if swizzled else row_bytes
    storage_h = (height + 7) & ~7 if swizzled else height
    stored_size = storage_row * storage_h
    pixels = block[16 : 16 + stored_size]
    if len(pixels) != stored_size:
        raise ValueError("truncated pixel payload")
    if swizzled:
        linear = unswizzle(pixels, storage_row, storage_h)
        rows = b"".join(linear[y * storage_row : y * storage_row + row_bytes] for y in range(height))
        palette_at = 16 + stored_size + 16
    else:
        rows = pixels[: row_bytes * height]
        # Unswizzled CLUT blocks have a 20-byte footer before palette data.
        palette_at = 16 + row_bytes * height + 20
    raw_palette = block[palette_at : palette_at + palette_count * 4]
    if len(raw_palette) != palette_count * 4:
        raise ValueError("truncated palette")
    palette = [tuple(raw_palette[i : i + 4]) for i in range(0, len(raw_palette), 4)]
    if fmt == CLUT4:
        indices = []
        for byte in rows:
            indices.extend((byte & 15, byte >> 4))
        indices = indices[: width * height]
    else:
        indices = list(rows[: width * height])
    return indices, palette, {
        "format": f"0x{fmt:04X}", "aux": aux, "width": width, "height": height,
        "padded_width": padded_w, "padded_height": padded_h,
        "flags0": f"0x{flags0:04X}", "flags1": f"0x{flags1:04X}",
        "swizzled": swizzled, "palette_entries": palette_count, "name": name_from_footer(block),
    }


def alpha_score(values: list[int]) -> float:
    """Coverage bytes are normally concentrated near zero/one; colour lanes are not."""
    if not values:
        return 0.0
    extremes = sum(v <= 8 or v >= 247 for v in values) / len(values)
    high = sum(v >= 224 for v in values) / len(values)
    return round(extremes * 0.7 + high * 0.3, 4)


def candidate(indices, palette, size, order, invert_alpha: bool = False, transparent_index: int | None = None, binary_index_coverage: bool = False) -> Image.Image:
    out = Image.new("RGBA", size)
    remapped = [tuple(p[i] for i in order) for p in palette]
    if invert_alpha:
        remapped = [(r, g, b, 255 - a) for r, g, b, a in remapped]
    if transparent_index is not None:
        r, g, b, _a = remapped[transparent_index]
        remapped[transparent_index] = (r, g, b, 0)
    if binary_index_coverage:
        remapped = [(r, g, b, 0 if i == 0 else 255) for i, (r, g, b, _a) in enumerate(remapped)]
    out.putdata([remapped[i] for i in indices])
    return out


def checkerboard(size: tuple[int, int]) -> Image.Image:
    image = Image.new("RGBA", size, (54, 58, 64, 255))
    draw = ImageDraw.Draw(image)
    step = 8
    for y in range(0, size[1], step):
        for x in range(0, size[0], step):
            if ((x // step) + (y // step)) & 1:
                draw.rectangle((x, y, x + step - 1, y + step - 1), fill=(96, 101, 108, 255))
    return image


def sheet(images: dict[str, Image.Image], title: str, meta: dict, output: Path):
    cell_w, cell_h = 260, 196
    columns = 2
    rows = (len(images) + columns - 1) // columns
    canvas = Image.new("RGBA", (cell_w * columns, cell_h * rows + 38), (20, 24, 30, 255))
    draw = ImageDraw.Draw(canvas)
    draw.text((6, 5), title[:76], font=FONT, fill="white")
    draw.text((6, 18), f"{meta['format']}  {meta['width']}x{meta['height']}  aux={meta['aux']}  flags={meta['flags0']}", font=FONT, fill=(178, 205, 220))
    for n, (label, image) in enumerate(images.items()):
        x, y = (n % columns) * cell_w, 38 + (n // columns) * cell_h
        preview = image.copy()
        preview.thumbnail((cell_w - 14, cell_h - 30), Image.Resampling.NEAREST)
        bg = checkerboard(preview.size)
        bg.alpha_composite(preview)
        canvas.alpha_composite(bg, (x + (cell_w - preview.width) // 2, y + 20))
        draw.text((x + 6, y + 4), label, font=FONT, fill=(255, 220, 128))
    canvas.convert("RGB").save(output)


def safe(text: str) -> str:
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in text)[:90]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, default=ROOT / "assets/generated/psp/recovered")
    ap.add_argument("--output", type=Path, default=ROOT / "diagnostics/psp_textures")
    ap.add_argument("--car", action="append", help="restrict to a car directory; may be supplied more than once")
    args = ap.parse_args()
    cars = sorted(p for p in args.source.iterdir() if p.is_dir() and (not args.car or p.name.upper() in {x.upper() for x in args.car}))
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"source": str(args.source), "cars": [], "decoder_candidates": list(ORDERS), "notes": [
        "Each sheet uses the exact same palette bytes. CLUT4 sheets additionally show the alternate low/high-nibble index order; anomalous aux=32 badges also test inverted coverage a targeted transparent palette index 0, and binary index coverage (0 transparent, nonzero opaque).",
        "The alpha candidate score ranks palette bytes that behave like binary/coverage alpha; it does not infer RGB order by itself.",
        "Do not use this report to add per-car hue fixes. Apply the selected order in the SHPM decoder after it is corroborated by PSP reference captures."
    ]}
    for car in cars:
        msh = car / f"{car.name}.msh"
        if not msh.exists():
            continue
        car_report = {"car": car.name, "textures": []}
        try:
            blocks = list(parse_shpm(msh.read_bytes()))
        except Exception as exc:
            car_report["error"] = str(exc); report["cars"].append(car_report); continue
        car_out = args.output / car.name
        car_out.mkdir(exist_ok=True)
        for entry, block in blocks:
            try:
                indices, palette, meta = decode_indices(block)
                images = {label: candidate(indices, palette, (meta["width"], meta["height"]), order) for label, order in ORDERS.items()}
                # CLUT4 stores two texel indices per byte.  A wrong nibble order
                # produces valid-looking geometry with entirely wrong palette
                # colours, which is exactly the failure class seen in CARRERA4S.
                # Expose that alternative without changing the decoder.
                if meta["format"] == "0x105C":
                    swapped = list(indices)
                    for i in range(0, len(swapped) - 1, 2):
                        swapped[i], swapped[i + 1] = swapped[i + 1], swapped[i]
                    for label, order in ORDERS.items():
                        images[f"{label} / swapped CLUT4 nibbles"] = candidate(swapped, palette, (meta["width"], meta["height"]), order)
                # The anomalous CLUT4 badges use the same aux=32 profile as
                # mask textures. Test whether their coverage convention is
                # reversed: emblem alpha = 255 - stored alpha.
                if "BADGING" in meta["name"].upper() and meta["format"] == "0x105C" and meta["aux"] == 32:
                    for label, order in ORDERS.items():
                        images[f"{label} / inverted coverage"] = candidate(indices, palette, (meta["width"], meta["height"]), order, invert_alpha=True)
                    # These anomalous maps use index 0 as the spatially
                    # dominant background, unlike valid CLUT8 badges where
                    # that entry is already transparent. Test a targeted
                    # transparent-index rule without corrupting foreground AA.
                    images["RGBA (A=byte 3) / index 0 transparent"] = candidate(indices, palette, (meta["width"], meta["height"]), ORDERS["RGBA (A=byte 3)"], transparent_index=0)
                    images["RGBA / swapped nibbles / index 0 transparent"] = candidate(swapped, palette, (meta["width"], meta["height"]), ORDERS["RGBA (A=byte 3)"], transparent_index=0)
                    images["RGBA / index 0 background; nonzero opaque"] = candidate(indices, palette, (meta["width"], meta["height"]), ORDERS["RGBA (A=byte 3)"], binary_index_coverage=True)
                    images["RGBA / swapped nibbles; nonzero opaque"] = candidate(swapped, palette, (meta["width"], meta["height"]), ORDERS["RGBA (A=byte 3)"], binary_index_coverage=True)
                filename = f"{entry}_{safe(meta['name'])}.png"
                sheet(images, f"{car.name} / {entry} / {meta['name']}", meta, car_out / filename)
                channels = [[p[i] for p in palette] for i in range(4)]
                record = dict(meta)
                record.update({"entry_id": entry, "sheet": str((car_out / filename).relative_to(args.output)),
                               "palette_alpha_scores": {f"byte_{i}": alpha_score(values) for i, values in enumerate(channels)},
                               "palette_unique_values": {f"byte_{i}": len(set(values)) for i, values in enumerate(channels)},
                               "index_usage": len(Counter(indices))})
                car_report["textures"].append(record)
            except Exception as exc:
                car_report["textures"].append({"entry_id": entry, "error": str(exc)})
        report["cars"].append(car_report)
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = ["<!doctype html><meta charset=utf-8><title>PSP CLUT diagnosis</title>",
             "<style>body{background:#14181e;color:#ddd;font-family:system-ui}img{max-width:520px;border:1px solid #39414d;margin:8px}section{display:inline-block;vertical-align:top}</style>",
             "<h1>PSP SHPM CLUT byte-order diagnosis</h1><p>Each sheet decodes the same raw palette with every RGB order while preserving byte 3 as alpha. CLUT4 sheets also test reversed index nibbles. Badges with the anomalous aux=32 mask profile test inverted alpha coverage transparent palette index 0, and binary index coverage. Inspect against a PSP capture before changing the decoder.</p>"]
    for car in report["cars"]:
        lines.append(f"<h2>{car['car']}</h2>")
        for texture in car.get("textures", []):
            if "sheet" in texture:
                lines.append(f"<section><img src='{texture['sheet']}'><br><small>{texture['name']} — alpha scores {texture['palette_alpha_scores']}</small></section>")
    (args.output / "index.html").write_text("\n".join(lines), encoding="utf-8")
    total = sum(len(car.get("textures", [])) for car in report["cars"])
    print(f"wrote {total} texture sheets to {args.output}")

if __name__ == "__main__":
    main()
