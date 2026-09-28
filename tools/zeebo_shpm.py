from __future__ import annotations

import re
import struct
from pathlib import Path
from typing import Optional

from PIL import Image

from zeebo_vehicle import maybe_refpack

# SHPM texture formats observed in the Carbon Zeebo dump.
#
# Low-byte families line up with the texture-format handling recovered in jogo.c:
#   0x73 : T8 / 256-entry palette
#   0x76 : T4 / 16-entry 16-bit palette
#   0x77 : T4 / 16-entry RGBA8888 palette
#   0x58 : direct 16-bit RGB565 (R in the low five bits)
#   0x5B : direct RGBA8888
#
# The high byte contains flags/storage-class information.  For 0x73 we have
# observed 0x0073, 0x1073 and 0x9073 and they share the same payload layout.
T8_FORMATS = {0x0073, 0x1073, 0x9073}
T4_4444_FORMATS = {0x3076}
# City MSH also uses 8-bit indices with 256 RGBA4444 entries.
T8_4444_FORMATS = {0x1071, 0x1072}
T4_RGBA8888_FORMATS = {0x5077}
RGB565_FORMATS = {0x1058}
RGBA8888_FORMATS = {0x505B}

SUPPORTED_FORMATS = (
    T8_FORMATS
    | T4_4444_FORMATS
    | T8_4444_FORMATS
    | T4_RGBA8888_FORMATS
    | RGB565_FORMATS
    | RGBA8888_FORMATS
)


def _cstring_name(footer: bytes) -> Optional[str]:
    """Recover the useful texture name from an SHPM block footer.

    Normal SHPM resources use "Name=FOO, ...".  The wheel BIG is smaller and
    stores only the final NUL-terminated canonical name, e.g.
    SKYLINE_TIRE_STYLE00 or BBS_STYLE02_CHROME.
    """
    text = footer.decode("latin1", "replace")
    m = re.search(r"Name=([^,\x00]+)", text)
    if m:
        return m.group(1).strip()

    # Wheel VIV entries deliberately omit the Name= metadata wrapper.
    candidates = re.findall(rb"[A-Za-z0-9_]{5,}", footer)
    for raw in reversed(candidates):
        value = raw.decode("ascii", "replace")
        if (
            "_TIRE_" in value
            or "_STYLE" in value
            or value in {"DUMMY_WHEEL", "TIRE_BACK", "CALIPER"}
        ):
            return value
    return None


def load_shpm(path: Path):
    raw = path.read_bytes()
    if raw[:4] != b"SHPM":
        raw = maybe_refpack(raw)
    if raw[:4] != b"SHPM":
        raise ValueError("not SHPM")
    if len(raw) < 16:
        raise ValueError("truncated SHPM")

    count = struct.unpack_from("<I", raw, 8)[0]
    if count > 65535 or 16 + count * 8 > len(raw):
        raise ValueError("invalid SHPM entry table")

    entries = []
    for i in range(count):
        p = 16 + i * 8
        name = raw[p:p+4].decode("latin1", "replace").rstrip("\0 ")
        off = struct.unpack_from("<I", raw, p + 4)[0]
        entries.append((name, off or None))
    first = 16 + count * 8
    return raw, entries, first


def _dimensions(block: bytes):
    if len(block) < 16:
        raise ValueError("texture block shorter than header")
    fmt, aux, width, height, padded_width, padded_height, flags0, flags1 = (
        struct.unpack_from("<8H", block, 0)
    )
    if width <= 0 or height <= 0 or width > 4096 or height > 4096:
        raise ValueError(f"invalid texture dimensions {width}x{height}")
    return fmt, aux, width, height, padded_width, padded_height, flags0, flags1


def _minimum_block_size(data: bytes, start: int):
    if start < 0 or start + 16 > len(data):
        raise ValueError("bad SHPM block offset")
    fmt, aux, w, h, *_ = _dimensions(data[start:start+16])

    if fmt in T8_FORMATS:
        # aux is the palette offset for this family.
        return 16 + 1024 + w * h
    if fmt in T4_4444_FORMATS:
        # 0x3076 uses a fixed palette immediately after the 16-byte header.
        return 16 + 32 + (w * h + 1) // 2
    if fmt in T8_4444_FORMATS:
        return 16 + 512 + w * h
    if fmt in T4_RGBA8888_FORMATS:
        # 0x5077 is different: header, fixed 64-byte RGBA palette, T4 pixels.
        return 16 + 64 + (w * h + 1) // 2
    if fmt in RGB565_FORMATS:
        return 16 + w * h * 2
    if fmt in RGBA8888_FORMATS:
        return 16 + w * h * 4
    raise ValueError(f"unsupported SHPM format 0x{fmt:04X}")


def _bounds(data: bytes, entries, first: int):
    starts = [off for _, off in entries]
    for i in range(len(starts)):
        if starts[i] is not None:
            if starts[i] < first or starts[i] >= len(data):
                raise ValueError("SHPM entry offset outside container")
            continue

        if i == 0:
            starts[i] = first
        else:
            prev = starts[i-1]
            if prev is None:
                raise ValueError("cannot infer SHPM entry offset")
            starts[i] = prev + _minimum_block_size(data, prev)

    ends = starts[1:] + [len(data)]
    for start, end in zip(starts, ends):
        if start is None or end is None or not (0 <= start < end <= len(data)):
            raise ValueError("invalid SHPM block bounds")
    return starts, ends


def _rgba4444(buf: bytes, endian="<"):
    if len(buf) % 2:
        raise ValueError("odd RGBA4444 palette")
    out = []
    for i in range(0, len(buf), 2):
        v = struct.unpack_from(endian + "H", buf, i)[0]
        # Carbon's 0x3076 palette stores conventional RGBA nibbles as:
        #
        #   bits 15..12 = R
        #   bits 11..8  = G
        #   bits  7..4  = B
        #   bits  3..0  = A
        #
        # This is visible directly in the recovered global palettes:
        # CARBONFIBRE contains entries such as 0x111F/0x444F (opaque
        # greys), while Carbon_red_legend contains 0x711F/0xB21F
        # (opaque dark reds).  The previous reversed-nibble decode turned
        # the low alpha nibble F into a full red channel and the real red
        # nibble into alpha, producing pink carbon and widespread partial
        # transparency.
        out.append((
            ((v >> 12) & 15) * 17,
            ((v >> 8) & 15) * 17,
            ((v >> 4) & 15) * 17,
            (v & 15) * 17,
        ))
    return out


def _rgba8888_palette(buf: bytes, order="RGBA"):
    if len(buf) % 4:
        raise ValueError("misaligned RGBA8888 palette")
    pos = {"R": 0, "G": 1, "B": 2, "A": 3}
    p = [pos[x] for x in order]
    return [
        tuple(buf[i+j] for j in p)
        for i in range(0, len(buf), 4)
    ]


def _decode_indexed(
    block: bytes,
    *,
    palette_offset: int,
    palette_bytes: int,
    palette,
    bpp: int,
    swap_nibbles: bool = False,
):
    _, _, w, h, *_ = _dimensions(block)
    pixel_offset = palette_offset + palette_bytes
    pixel_bytes = (w * h * bpp + 7) // 8
    raw = block[pixel_offset:pixel_offset + pixel_bytes]
    if len(raw) != pixel_bytes:
        raise ValueError("truncated indexed texture pixels")

    im = Image.new("RGBA", (w, h))
    px = im.load()
    if bpp == 8:
        for y in range(h):
            row = y * w
            for x in range(w):
                idx = raw[row + x]
                if idx >= len(palette):
                    raise ValueError("T8 palette index outside palette")
                px[x, y] = palette[idx]
    elif bpp == 4:
        k = 0
        for y in range(h):
            for x in range(0, w, 2):
                value = raw[k]
                k += 1
                if swap_nibbles:
                    i0, i1 = value & 15, value >> 4
                else:
                    i0, i1 = value >> 4, value & 15
                px[x, y] = palette[i0]
                if x + 1 < w:
                    px[x+1, y] = palette[i1]
    else:
        raise ValueError("unsupported indexed bpp")
    return im, pixel_offset + pixel_bytes


def _decode_rgb565_rlo(block: bytes):
    """Decode Zeebo 0x58.

    jogo.c's recovered RGBA->0x58 conversion places R in bits 0..4, G in
    5..10 and B in 11..15.  That makes 0x001F pure red; the PAINT texture is
    intentionally a uniform red runtime-modulated material.
    """
    _, _, w, h, *_ = _dimensions(block)
    size = w * h * 2
    raw = block[16:16+size]
    if len(raw) != size:
        raise ValueError("truncated RGB565 texture")
    im = Image.new("RGBA", (w, h))
    px = im.load()
    k = 0
    for y in range(h):
        for x in range(w):
            v = struct.unpack_from("<H", raw, k)[0]
            k += 2
            r = ((v >> 0) & 31) * 255 // 31
            g = ((v >> 5) & 63) * 255 // 63
            b = ((v >> 11) & 31) * 255 // 31
            px[x, y] = (r, g, b, 255)
    return im, 16 + size


def _decode_direct_rgba8888(block: bytes):
    _, _, w, h, *_ = _dimensions(block)
    size = w * h * 4
    raw = block[16:16+size]
    if len(raw) != size:
        raise ValueError("truncated RGBA8888 texture")
    return Image.frombytes("RGBA", (w, h), raw), 16 + size


def decode_block(block: bytes, variant: str):
    fmt, aux, w, h, *_ = _dimensions(block)

    if fmt in T8_FORMATS:
        order = variant
        palette = _rgba8888_palette(block[16:16+1024], order)
        im, payload_end = _decode_indexed(
            block,
            palette_offset=16,
            palette_bytes=1024,
            palette=palette,
            bpp=8,
        )
    elif fmt in T8_4444_FORMATS:
        endian = ">" if "_be" in variant else "<"
        palette = _rgba4444(block[16:16+512], endian)
        im, payload_end = _decode_indexed(block, palette_offset=16, palette_bytes=512, palette=palette, bpp=8)
    elif fmt in T4_4444_FORMATS:
        endian = ">" if "_be" in variant else "<"
        palette = _rgba4444(block[16:16+32], endian)
        im, payload_end = _decode_indexed(
            block,
            palette_offset=16,
            palette_bytes=32,
            palette=palette,
            bpp=4,
            swap_nibbles=variant.endswith("_swap"),
        )
    elif fmt in T4_RGBA8888_FORMATS:
        # The decompiled EAGL texture path shows 0x77 is T4 with a 64-byte
        # palette. Unlike 0x73/0x76 the palette sits immediately after the
        # fixed 16-byte header; aux is not a palette offset in this family.
        palette = _rgba8888_palette(block[16:80], "RGBA")
        im, payload_end = _decode_indexed(
            block,
            palette_offset=16,
            palette_bytes=64,
            palette=palette,
            bpp=4,
            swap_nibbles=variant.endswith("_swap"),
        )
    elif fmt in RGB565_FORMATS:
        im, payload_end = _decode_rgb565_rlo(block)
    elif fmt in RGBA8888_FORMATS:
        im, payload_end = _decode_direct_rgba8888(block)
    else:
        raise ValueError(f"unsupported SHPM format 0x{fmt:04X}")

    return im, _cstring_name(block)


def _variants_for_format(fmt: int):
    if fmt in T8_FORMATS:
        return ["RGBA", "BGRA", "ARGB", "ABGR"]
    if fmt in T8_4444_FORMATS:
        return ["rgba4444_le", "rgba4444_be"]
    if fmt in T4_4444_FORMATS:
        return [
            "rgba4444_le",
            "rgba4444_be",
            "rgba4444_le_swap",
            "rgba4444_be_swap",
        ]
    if fmt in T4_RGBA8888_FORMATS:
        return ["rgba8888_t4", "rgba8888_t4_swap"]
    if fmt in RGB565_FORMATS:
        return ["rgb565_rlo"]
    if fmt in RGBA8888_FORMATS:
        return ["rgba8888_direct"]
    return []


def _score(im: Image.Image):
    pixels = list(im.getdata())
    if not pixels:
        return -999.0
    n = len(pixels)
    opaque = sum(p[3] == 255 for p in pixels) / n
    transparent = sum(p[3] == 0 for p in pixels) / n
    unique = min(len(set(pixels)) / 64.0, 1.0)
    # This score only selects byte-order candidates. It deliberately does not
    # reject legitimate flat runtime textures such as PAINT.
    return 0.35 * opaque + 0.65 * unique - (
        0.75 if transparent > 0.98 else 0.0
    )


def extract_best(path: Path, out_dir: Path):
    data, entries, first = load_shpm(path)
    starts, ends = _bounds(data, entries, first)
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = []

    for i, (entry_name, _) in enumerate(entries):
        block = data[starts[i]:ends[i]]
        fmt = struct.unpack_from("<H", block, 0)[0]
        variants = _variants_for_format(fmt)
        if not variants:
            manifest.append({
                "index": i,
                "id": entry_name,
                "ok": False,
                "format": f"0x{fmt:04X}",
                "error": f"unsupported format 0x{fmt:04X}",
            })
            continue

        candidates = []
        for variant in variants:
            try:
                im, footer_name = decode_block(block, variant)
                candidates.append((_score(im), variant, im, footer_name))
            except Exception:
                continue

        if not candidates:
            manifest.append({
                "index": i,
                "id": entry_name,
                "ok": False,
                "format": f"0x{fmt:04X}",
                "error": "all decoders failed",
            })
            continue

        # Stable tie-breaking keeps the historical non-swapped T4 behavior.
        _, variant, im, footer_name = max(
            enumerate(candidates),
            key=lambda pair: (pair[1][0], -pair[0]),
        )[1]

        display = footer_name or entry_name or f"tex{i:02d}"
        safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", display).strip("_") or f"tex{i:02d}"
        filename = f"{entry_name}_{safe[:96]}.png"
        im.save(out_dir / filename)

        manifest.append({
            "index": i,
            "id": entry_name,
            "name": footer_name,
            "file": filename,
            "variant": variant,
            "format": f"0x{fmt:04X}",
            "width": im.width,
            "height": im.height,
            "ok": True,
        })
    return manifest
