#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, re, struct, sys
from pathlib import Path
from PIL import Image
FMT_CLUT4=0x105C
FMT_CLUT8=0x105D
PSP_SWIZZLED=0x2000

def import_zeebo_refpack(tools_dir: Path):
    sys.path.insert(0, str(tools_dir))
    from zeebo_vehicle import maybe_refpack
    return maybe_refpack

def psp_unswizzle(data: bytes, width_bytes: int, height: int) -> bytes:
    if width_bytes < 16 or height < 8:
        return data[:width_bytes*height]
    if width_bytes % 16:
        raise ValueError(f"swizzled width {width_bytes} is not 16-byte aligned")
    expected = width_bytes * height
    src = data[:expected]
    if len(src) < expected:
        raise ValueError('truncated swizzled payload')
    dst = bytearray(expected)
    rowblocks = width_bytes // 16
    for y in range(height):
        by=y//8; iy=y&7
        for x in range(width_bytes):
            bx=x//16; ix=x&15
            block_index = bx + by*rowblocks
            src_off = block_index*128 + iy*16 + ix
            dst[y*width_bytes+x] = src[src_off]
    return bytes(dst)

def rgba_palette(raw: bytes, count: int):
    need = count*4
    if len(raw) < need:
        raise ValueError('palette truncated')
    return [tuple(raw[i:i+4]) for i in range(0, need, 4)]


def rgba4444_palette(raw: bytes, count: int):
    """PSP vehicle CLUT4 uses little-endian RGBA4444, not RGBA8888.

    The previous decoder consumed four bytes per entry. That made every other
    16-bit palette word look like a colour+alpha tuple and corrupted CLUT4
    badges/details. The word nibbles are R,G,B,A from high to low.
    """
    need = count * 2
    if len(raw) < need:
        raise ValueError('RGBA4444 palette truncated')
    result=[]
    for i in range(count):
        word = struct.unpack_from('<H', raw, i * 2)[0]
        result.append((((word >> 12) & 15) * 17,
                       ((word >> 8) & 15) * 17,
                       ((word >> 4) & 15) * 17,
                       (word & 15) * 17))
    return result

def footer_name(block: bytes) -> str|None:
    m = re.search(rb"Name=([^,\x00]+)", block)
    if m: return m.group(1).decode('latin1','replace').strip()
    names = re.findall(rb"[A-Z][A-Z0-9_]{4,}", block)
    if names: return names[-1].decode('ascii','replace')
    return None

def decode_psp_shpm_block(block: bytes):
    fmt, aux, w, h, padded_w, padded_h, flags0, flags1 = struct.unpack_from('<8H', block, 0)
    swizzled = bool(flags0 & PSP_SWIZZLED)
    if fmt == FMT_CLUT4:
        logical_row_bytes = (w+1)//2
        if swizzled:
            storage_row_bytes = (logical_row_bytes + 15) & ~15
            storage_h = (h + 7) & ~7
            storage_bytes = storage_row_bytes * storage_h
            # Vehicle-local CLUT4 on PSP: header, swizzled pixels, 16-byte pad, 64-byte RGBA palette, footer.
            pixoff = 16
            paloff = pixoff + storage_bytes + 16
            pixels = block[pixoff:pixoff+storage_bytes]
            palette = rgba4444_palette(block[paloff:paloff+32], 16)
            linear = psp_unswizzle(pixels, storage_row_bytes, storage_h)
            pixels = b''.join(linear[y*storage_row_bytes:y*storage_row_bytes+logical_row_bytes] for y in range(h))
        else:
            pixel_bytes = logical_row_bytes * h
            pixels = block[16:16+pixel_bytes]
            tail = block[16+pixel_bytes:]
            if len(tail) < 84:
                raise ValueError('unswizzled CLUT4 footer/palette truncated')
            palette = rgba4444_palette(tail[20:20+32], 16)
        rgba=[]
        for value in pixels:
            rgba.append(palette[value & 0x0F])
            rgba.append(palette[value >> 4])
        rgba = rgba[:w*h]
    elif fmt == FMT_CLUT8:
        if not swizzled:
            raise ValueError('unobserved unswizzled PSP CLUT8 layout')
        storage_row_bytes = (w + 15) & ~15
        storage_h = (h + 7) & ~7
        storage_bytes = storage_row_bytes * storage_h
        # Vehicle-local CLUT8 on PSP: header, swizzled pixels, 16-byte pad, 1024-byte RGBA palette, footer.
        pixoff = 16
        paloff = pixoff + storage_bytes + 16
        pixels = block[pixoff:pixoff+storage_bytes]
        palette = rgba_palette(block[paloff:paloff+1024], 256)
        linear = psp_unswizzle(pixels, storage_row_bytes, storage_h)
        pixels = b''.join(linear[y*storage_row_bytes:y*storage_row_bytes+w] for y in range(h))
        rgba = [palette[v] for v in pixels]
    else:
        raise ValueError(f'unsupported fmt 0x{fmt:04X}')
    img = Image.new('RGBA', (w,h))
    img.putdata(rgba)
    return img, {
        'format': f'0x{fmt:04X}', 'aux': aux, 'width':w,'height':h,
        'padded_width':padded_w,'padded_height':padded_h,
        'flags0':f'0x{flags0:04X}','flags1':f'0x{flags1:04X}',
        'swizzled':swizzled,'name':footer_name(block), '_palette0': palette[0],
    }

def parse_shpm(raw: bytes, maybe_refpack):
    raw = maybe_refpack(raw)
    if raw[:4] != b'SHPM' or len(raw) < 16:
        raise ValueError('not a decoded SHPM')
    count = struct.unpack_from('<I', raw, 8)[0]
    entries=[]
    for i in range(count):
        p=16+i*8
        entry_id = raw[p:p+4].decode('latin1','replace').rstrip('\0 ')
        off = struct.unpack_from('<I', raw, p+4)[0]
        entries.append((entry_id,off))
    ends = [off for _,off in entries[1:]] + [len(raw)]
    return raw, [(entry_id, off, end) for (entry_id, off), end in zip(entries, ends)]

def safe_name(v:str)->str:
    import re
    v = re.sub(r'[^A-Za-z0-9_.-]+','_',v).strip('_')
    return v or 'texture'

def extract_shpm_blob(blob: bytes, source_label: str, out_dir: Path, maybe_refpack):
    raw, entries = parse_shpm(blob, maybe_refpack)
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest=[]
    for index,(entry_id,start,end) in enumerate(entries):
        rec={'index':index,'entry_id':entry_id,'source':source_label}
        try:
            image, meta = decode_psp_shpm_block(raw[start:end])
            display = meta.get('name') or entry_id or f'tex{index:02d}'
            filename = f"{index:02d}_{safe_name(str(display))}.png"
            # Vehicle badge palettes use index 0 as a transparent atlas
            # background, while their stored alpha is a material control lane.
            # Preserve the decoded RGB and derive stable cut-out coverage from
            # the actual index stream rather than treating that lane as opacity.
            background = meta.pop("_palette0")
            if "BADGING" in str(meta.get("name", "")).upper():
                rgba = list(image.getdata())
                image.putdata([(r, g, b, 0 if (r, g, b, a) == background else 255)
                               for r, g, b, a in rgba])
            image.save(out_dir/filename)
            rec.update(meta); rec.update({'ok':True,'file':filename})
        except Exception as exc:
            rec.update({'ok':False,'error':str(exc)})
        manifest.append(rec)
    return manifest

def parse_bigf(blob: bytes):
    if blob[:4] != b'BIGF' or len(blob) < 16: raise ValueError('not BIGF')
    count = struct.unpack_from('>I', blob, 8)[0]
    pos = 16; entries=[]
    for _ in range(count):
        off,size=struct.unpack_from('>II', blob, pos); pos += 8
        end = blob.index(b'\0', pos)
        name = blob[pos:end].decode('latin1','replace'); pos = end+1
        entries.append((name,off,size))
    return entries

def extract_vehicle_dir(car_dir: Path, out_root: Path, maybe_refpack):
    car=car_dir.name; car_out=out_root/car
    records={'car':car,'vehicle_textures':[],'vinyls':[]}
    local_msh = car_dir / f'{car}.msh'
    if local_msh.exists():
        records['vehicle_textures'] = extract_shpm_blob(local_msh.read_bytes(), local_msh.name, car_out/'vehicle', maybe_refpack)
    viv = car_dir/'vinyls.viv'
    if viv.exists():
        viv_blob=viv.read_bytes(); entries=parse_bigf(viv_blob); vinyl_out=car_out/'vinyls'; vinyl_out.mkdir(parents=True, exist_ok=True)
        for member_index,(name,off,size) in enumerate(entries):
            member = viv_blob[off:off+size]
            member_manifest = extract_shpm_blob(member, name, vinyl_out, maybe_refpack)
            for rec in member_manifest:
                oldfile=rec.get('file')
                if rec.get('ok') and oldfile:
                    oldpath = vinyl_out/oldfile
                    canonical = rec.get('name') or Path(name).stem
                    newfile = f'{member_index:03d}_{safe_name(str(canonical))}.png'
                    newpath = vinyl_out/newfile
                    if oldpath.exists(): oldpath.replace(newpath)
                    rec['file']=newfile
                rec['bigf_member']=name; rec['bigf_member_index']=member_index
                records['vinyls'].append(rec)
    car_out.mkdir(parents=True, exist_ok=True)
    (car_out/'manifest.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    return records

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input', type=Path); ap.add_argument('output', type=Path); ap.add_argument('--tools-dir', type=Path, default=Path('tools'))
    args=ap.parse_args(); maybe_refpack = import_zeebo_refpack(args.tools_dir.resolve())
    if (args.input / f'{args.input.name}.msh').exists(): car_dirs=[args.input]
    else: car_dirs = sorted(p for p in args.input.iterdir() if p.is_dir())
    all_records=[]
    for car_dir in car_dirs:
        if not ((car_dir/f'{car_dir.name}.msh').exists() or (car_dir/'vinyls.viv').exists()):
            continue
        all_records.append(extract_vehicle_dir(car_dir, args.output, maybe_refpack))
    summary={
        'cars': len(all_records),
        'vehicle_textures': sum(len(x['vehicle_textures']) for x in all_records),
        'vehicle_ok': sum(sum(1 for r in x['vehicle_textures'] if r.get('ok')) for x in all_records),
        'vinyl_textures': sum(len(x['vinyls']) for x in all_records),
        'vinyl_ok': sum(sum(1 for r in x['vinyls'] if r.get('ok')) for x in all_records),
        'formats': ['0x105C CLUT4 RGBA4444', '0x105D CLUT8 RGBA8888'],
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'manifest.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))

if __name__=='__main__':
    main()
