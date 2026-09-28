#!/usr/bin/env python3
"""Small, defensive readers for the formats used by the Zeebo asset dumps."""

from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path
from typing import Dict, List, Sequence


REFPACK_HEADERS = (0x10, 0x11, 0x90, 0x91)
MAX_REFPACK_OUTPUT = 64 * 1024 * 1024


class ZeeboFormatError(ValueError):
    """Raised when a Zeebo payload is truncated or structurally invalid."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ZeeboFormatError(message)


def _refpack_offset(data: bytes) -> int | None:
    if len(data) >= 6 and data[4] in REFPACK_HEADERS and data[5] == 0xFB:
        return 4
    if len(data) >= 2 and data[0] in REFPACK_HEADERS and data[1] == 0xFB:
        return 0
    return None


def decode_refpack(data: bytes, max_output: int = MAX_REFPACK_OUTPUT) -> bytes:
    """Decode RefPack while rejecting malformed input instead of indexing past it."""
    header_offset = _refpack_offset(data)
    if header_offset is None:
        raise ZeeboFormatError("not a RefPack stream")

    control = data[header_offset]
    source = header_offset + 2
    if control & 0x01:
        _require(source + 4 <= len(data), "RefPack is missing its extended header")
        source += 4
    if control & 0x80:
        _require(source + 4 <= len(data), "RefPack is missing its 32-bit size")
        output_size = struct.unpack_from(">I", data, source)[0]
        source += 4
    else:
        _require(source + 3 <= len(data), "RefPack is missing its 24-bit size")
        output_size = int.from_bytes(data[source:source + 3], "big")
        source += 3
    _require(output_size <= max_output, f"RefPack output exceeds {max_output} bytes")

    output = bytearray()
    while len(output) < output_size:
        _require(source < len(data), "RefPack ended before its declared output size")
        command = data[source]
        if command <= 0x7F:
            _require(source + 2 <= len(data), "truncated short RefPack command")
            b1 = data[source + 1]
            literal_count = command & 3
            copy_count = ((command & 0x1C) >> 2) + 3
            distance = ((command & 0x60) << 3) + b1 + 1
            source += 2
        elif command <= 0xBF:
            _require(source + 3 <= len(data), "truncated medium RefPack command")
            b1, b2 = data[source + 1], data[source + 2]
            literal_count = (b1 & 0xC0) >> 6
            copy_count = (command & 0x3F) + 4
            distance = ((b1 & 0x3F) << 8) + b2 + 1
            source += 3
        elif command <= 0xDF:
            _require(source + 4 <= len(data), "truncated long RefPack command")
            b1, b2, b3 = data[source + 1], data[source + 2], data[source + 3]
            literal_count = command & 3
            copy_count = ((command & 0x0C) << 6) + b3 + 5
            distance = ((command & 0x10) << 12) + (b1 << 8) + b2 + 1
            source += 4
        elif command <= 0xFB:
            literal_count = ((command & 0x1F) << 2) + 4
            copy_count = 0
            distance = 0
            source += 1
        else:
            literal_count = command & 3
            copy_count = 0
            distance = 0
            source += 1

        _require(source + literal_count <= len(data), "RefPack literal runs past input")
        output.extend(data[source:source + literal_count])
        source += literal_count
        if copy_count:
            _require(distance <= len(output), "RefPack back-reference is out of bounds")
            for _ in range(copy_count):
                if len(output) >= output_size:
                    break
                output.append(output[-distance])
        if command >= 0xFC:
            break

    _require(len(output) == output_size, f"RefPack ended early: {len(output)}/{output_size}")
    return bytes(output)


def maybe_refpack(raw: bytes, max_output: int = MAX_REFPACK_OUTPUT) -> bytes:
    if _refpack_offset(raw) is None:
        return raw
    return decode_refpack(raw, max_output=max_output)


def _cstring(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    return data[offset:].split(b"\0", 1)[0].decode("latin1", "replace")


def parse_mips_elf(data: bytes) -> Dict[str, object]:
    """Read metadata from the relocatable MIPS ELF stored in vehicle .o files."""
    _require(len(data) >= 52 and data[:4] == b"\x7fELF", "not an ELF32 object")
    _require(data[4] == 1 and data[5] == 1, "object is not ELF32 little-endian")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data, 0)
    _, elf_type, machine, _version, _entry, _phoff, section_offset, _flags, _ehsize, _phentsize, _phnum, section_size, section_count, string_index = header
    _require(machine == 8, f"unsupported ELF machine {machine}")
    _require(section_size == 40, "unsupported ELF section-header size")
    _require(section_offset + section_size * section_count <= len(data), "ELF section table is truncated")
    _require(string_index < section_count, "invalid ELF section-string index")

    sections = []
    for index in range(section_count):
        values = struct.unpack_from("<IIIIIIIIII", data, section_offset + index * section_size)
        sections.append({
            "index": index,
            "name_offset": values[0],
            "type": values[1],
            "offset": values[4],
            "size": values[5],
            "link": values[6],
            "entry_size": values[9],
        })
    section_strings = sections[string_index]
    _require(
        section_strings["offset"] + section_strings["size"] <= len(data),
        "ELF section-string table is truncated",
    )
    section_string_data = data[
        section_strings["offset"]:section_strings["offset"] + section_strings["size"]
    ]
    for section in sections:
        section["name"] = _cstring(section_string_data, section["name_offset"])
        _require(
            section["offset"] + section["size"] <= len(data),
            f"ELF section {section['name']} is truncated",
        )

    named = {section["name"]: section for section in sections}
    string_table = named.get(".strtab")
    symbol_table = named.get(".symtab")
    symbols: List[Dict[str, object]] = []
    if string_table and symbol_table:
        strings = data[string_table["offset"]:string_table["offset"] + string_table["size"]]
        entry_size = symbol_table["entry_size"] or 16
        _require(entry_size == 16, "unsupported ELF32 symbol size")
        _require(symbol_table["size"] % entry_size == 0, "truncated ELF symbol table")
        for index in range(symbol_table["size"] // entry_size):
            offset = symbol_table["offset"] + index * entry_size
            name_offset, value, size, info, _other, section_index = struct.unpack_from("<IIIBBH", data, offset)
            name = _cstring(strings, name_offset)
            if not name:
                continue
            symbols.append({
                "name": name,
                "value": value,
                "size": size,
                "bind": info >> 4,
                "type": info & 0x0F,
                "section": section_index,
                "defined": section_index != 0,
            })

    defined = [item["name"] for item in symbols if item["defined"]]
    undefined = [item["name"] for item in symbols if not item["defined"]]
    model_symbols = sorted(name.split(":::", 1)[1] for name in defined if name.startswith("__Model:::"))
    bbox_symbols = sorted(name.split(":::", 1)[1] for name in defined if name.startswith("__BBOX:::"))
    skeleton_symbols = sorted(name.split(":::", 1)[1] for name in defined if name.startswith("__Skeleton:::"))
    bone_symbols = sorted(name.split(":::", 1)[1] for name in defined if name.startswith("__Bone:::"))
    material_references = sorted({
        name for name in undefined
        if "Material_" in name or "Texture" in name or "__EAGL::" in name
    })

    return {
        "format": "ELF32-MIPS-REL",
        "elf_type": elf_type,
        "machine": machine,
        "sections": [
            {"name": section["name"], "offset": section["offset"], "size": section["size"]}
            for section in sections if section["name"]
        ],
        "symbol_count": len(symbols),
        "symbols": symbols,
        "models": model_symbols,
        "bboxes": bbox_symbols,
        "skeletons": skeleton_symbols,
        "bones": bone_symbols,
        "material_references": material_references,
    }


def decode_body_b_mesh(data: bytes, model_name: str | None = None) -> Dict[str, object]:
    """Decode the visual BODY_B display-list streams from a component ELF.

    The small ``geometry.bin`` groups are named ``*_CV`` and are useful as
    collision/variant proxies, but the rendered body lives in the component's
    EAGL primitive streams.  Each BODY_B packet stores a 16-bit vertex count
    followed by 16-byte quantized records and is rendered as a triangle strip.
    """
    decoded = maybe_refpack(data)
    _require(len(decoded) >= 52 and decoded[:4] == b"\x7fELF", "not an ELF32 object")
    _require(decoded[4] == 1 and decoded[5] == 1, "object is not ELF32 little-endian")

    header = struct.unpack_from("<16sHHIIIIIHHHHHH", decoded, 0)
    _, _elf_type, machine, _version, _entry, _phoff, section_offset, _flags, _ehsize, _phentsize, _phnum, section_size, section_count, string_index = header
    _require(machine == 8 and section_size == 40, "unsupported MIPS ELF layout")
    _require(section_offset + section_size * section_count <= len(decoded), "ELF section table is truncated")
    sections = []
    for index in range(section_count):
        values = struct.unpack_from("<IIIIIIIIII", decoded, section_offset + index * section_size)
        sections.append({
            "index": index,
            "name_offset": values[0],
            "type": values[1],
            "offset": values[4],
            "size": values[5],
            "link": values[6],
            "entry_size": values[9],
        })

    _require(string_index < section_count, "invalid ELF section-string index")
    section_strings = sections[string_index]
    _require(
        section_strings["offset"] + section_strings["size"] <= len(decoded),
        "ELF section-string table is truncated",
    )
    section_string_data = decoded[
        section_strings["offset"]:section_strings["offset"] + section_strings["size"]
    ]
    for section in sections:
        section["name"] = _cstring(section_string_data, section["name_offset"])
        _require(
            section["offset"] + section["size"] <= len(decoded),
            f"ELF section {section['name']} is truncated",
        )

    named = {section["name"]: section for section in sections}
    data_section = named.get(".data")
    rel_section = named.get(".rel.data")
    string_table = named.get(".strtab")
    symbol_table = named.get(".symtab")
    _require(data_section is not None, "BODY_B ELF has no .data section")
    _require(rel_section is not None and string_table is not None and symbol_table is not None,
             "BODY_B ELF is missing relocation metadata")

    strings = decoded[
        string_table["offset"]:string_table["offset"] + string_table["size"]
    ]
    symbol_entry_size = symbol_table["entry_size"] or 16
    _require(symbol_entry_size == 16 and symbol_table["size"] % symbol_entry_size == 0,
             "unsupported ELF symbol table")
    symbols: List[Dict[str, object]] = []
    for index in range(symbol_table["size"] // symbol_entry_size):
        offset = symbol_table["offset"] + index * symbol_entry_size
        name_offset, value, size, info, _other, section_index = struct.unpack_from(
            "<IIIBBH", decoded, offset
        )
        symbols.append({
            "name": _cstring(strings, name_offset) if name_offset else "",
            "value": value,
            "size": size,
            "section": section_index,
        })

    body_models = [
        str(symbol["name"]).split(":::", 1)[1]
        for symbol in symbols
        if str(symbol["name"]).startswith("__Model:::")
        and str(symbol["name"]).split(":::", 1)[1].endswith("_BODY_B")
        and int(symbol["section"]) != 0
    ]
    component_models = [
        str(symbol["name"]).split(":::", 1)[1]
        for symbol in symbols
        if str(symbol["name"]).startswith("__Model:::")
        and int(symbol["section"]) != 0
    ]
    # Hood, spoiler and base-kit objects use the same EAGL packet layout but
    # their symbols end in *_HOOD_*, *_SPOILER_* or *_BASE_B rather than
    # *_BODY_B. Prefer BODY_B for the default, while allowing the caller to
    # request any concrete visual component explicitly.
    available_models = body_models or component_models
    _require(available_models, "component has no defined visual model")
    selected_model = model_name or sorted(available_models)[0]
    _require(selected_model in available_models, f"visual model not found: {selected_model}")

    blob = decoded[data_section["offset"]:data_section["offset"] + data_section["size"]]
    reloc_entry_size = rel_section["entry_size"] or 8
    _require(reloc_entry_size == 8 and rel_section["size"] % reloc_entry_size == 0,
             "unsupported MIPS relocation table")
    relocations = []
    for index in range(rel_section["size"] // reloc_entry_size):
        reloc_offset, relocation_info = struct.unpack_from(
            "<II", decoded, rel_section["offset"] + index * reloc_entry_size
        )
        symbol_index = relocation_info >> 8
        relocation_type = relocation_info & 0xFF
        if symbol_index < len(symbols):
            relocations.append((
                reloc_offset,
                str(symbols[symbol_index]["name"]),
                relocation_type,
            ))

    packet_descriptors = []
    for reloc_offset, symbol, relocation_type in relocations:
        _require(reloc_offset + 4 <= len(blob), "BODY_B relocation is outside .data")
        if relocation_type != 2:
            continue
        if symbol.startswith("NFSCar_TextureShiny"):
            stream_offset = struct.unpack_from("<I", blob, reloc_offset + 4)[0]
            packet_descriptors.append((stream_offset, reloc_offset))

    _require(packet_descriptors, "BODY_B has no texture-backed primitive packets")
    vertices: List[tuple[float, ...]] = []
    indices: List[int] = []
    groups: List[Dict[str, object]] = []
    seen_streams = set()
    for packet_index, (stream_offset, descriptor_offset) in enumerate(
        sorted(packet_descriptors)
    ):
        if stream_offset in seen_streams:
            continue
        seen_streams.add(stream_offset)
        _require(stream_offset + 0x18 <= len(blob), "BODY_B primitive header is truncated")
        vertex_count = struct.unpack_from("<H", blob, stream_offset + 0x10)[0]
        record_start = stream_offset + 0x18
        record_end = record_start + vertex_count * 0x10
        _require(vertex_count >= 3 and record_end <= len(blob),
                 "BODY_B vertex stream is truncated")
        # The descriptor immediately follows the stream in the original
        # object. This also rejects accidentally reading a later data block.
        _require(record_end <= descriptor_offset, "BODY_B stream overlaps its descriptor")

        first_vertex = len(vertices)
        first_index = len(indices)
        for record_index in range(vertex_count):
            record_offset = record_start + record_index * 0x10
            uv_u, uv_v, normal_x, normal_y, normal_z, position_x, position_y, position_z = struct.unpack_from(
                "<8h", blob, record_offset
            )
            vertices.append((
                position_x / 409.6,
                position_y / 409.6,
                position_z / 409.6,
                normal_x / 32767.0,
                normal_y / 32767.0,
                normal_z / 32767.0,
                max(0.0, min(1.0, (uv_u - 8192) / 256.0)),
                max(0.0, min(1.0, (uv_v - 8192) / 256.0)),
            ))

        # EAGL state 41=4 is the primitive mode used by these packets:
        # alternating-winding triangle strips, independent per material batch.
        for record_index in range(2, vertex_count):
            a, b, c = record_index - 2, record_index - 1, record_index
            if record_index & 1:
                a, c = c, a
            indices.extend((first_vertex + a, first_vertex + b, first_vertex + c))
        groups.append({
            "first_index": first_index,
            "index_count": len(indices) - first_index,
            "first_vertex": first_vertex,
            "vertex_count": vertex_count,
            "name": f"{selected_model}_PACKET{packet_index:02d}",
            # The TAR relocation belongs to the same material descriptor as
            # NFSCar_TextureShiny. Keep its stable four-character key instead
            # of guessing from packet order; the SHPM converter resolves this
            # key to an exported texture index later.
            "material_id": next(
                (
                    re.search(r";1=([^,;]+)", symbol).group(1)
                    for relocation, symbol, relocation_type in relocations
                    if relocation > descriptor_offset
                    and relocation < descriptor_offset + 0x60
                    and relocation_type == 2
                    and symbol.startswith("__EAGL::TAR:::")
                    and re.search(r";1=([^,;]+)", symbol)
                ),
                "",
            ),
        })

    _require(vertices and indices, "BODY_B decoded without renderable triangles")
    return {
        "model": selected_model,
        "vertices": vertices,
        "indices": indices,
        "groups": groups,
        "packet_count": len(groups),
        "triangle_count": len(indices) // 3,
        "source": "BODY_B",
    }


def inspect_component_file(path: str | Path) -> Dict[str, object]:
    source = Path(path)
    raw = source.read_bytes()
    decoded = maybe_refpack(raw)
    result: Dict[str, object] = {
        "file": source.name,
        "extension": source.suffix.lower(),
        "raw_bytes": len(raw),
        "decoded_bytes": len(decoded),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "decoded_sha256": hashlib.sha256(decoded).hexdigest(),
    }
    if decoded[:4] == b"\x7fELF":
        result.update(parse_mips_elf(decoded))
    else:
        result["format"] = decoded[:4].decode("latin1", "replace") or "binary"
    return result


def inspect_component_directory(source_dir: str | Path) -> Dict[str, object]:
    source = Path(source_dir)
    files = sorted(
        path for path in source.iterdir()
        if path.is_file() and path.suffix.lower() in {".o", ".a"}
    )
    components = []
    errors = []
    for path in files:
        try:
            components.append(inspect_component_file(path))
        except (OSError, ZeeboFormatError, struct.error) as exc:
            errors.append({"file": path.name, "error": f"{type(exc).__name__}: {exc}"})

    models = sorted({name for item in components for name in item.get("models", [])})
    bboxes = sorted({name for item in components for name in item.get("bboxes", [])})
    bones = sorted({name for item in components for name in item.get("bones", [])})
    materials = sorted({
        name for item in components for name in item.get("material_references", [])
    })
    return {
        "source": str(source),
        "component_count": len(components),
        "error_count": len(errors),
        "components": components,
        "models": models,
        "bboxes": bboxes,
        "bones": bones,
        "material_references": materials,
        "errors": errors,
    }


# ---- Extended vehicle packet decoder (2026-09-25) -------------------------

def _vehicle_material_for_packet(relocations, descriptor_offset):
    import re as _re
    for relocation, symbol, relocation_type in relocations:
        if relocation_type != 2:
            continue
        if not (descriptor_offset < relocation < descriptor_offset + 0x60):
            continue
        if not symbol.startswith("__EAGL::TAR:::"):
            continue
        match = _re.search(r";1=([^,;]+)", symbol)
        if match:
            return match.group(1)
    return ""


def _vehicle_strip(first_vertex, count):
    out = []
    for i in range(2, count):
        a, b, c = i - 2, i - 1, i
        if i & 1:
            a, c = c, a
        out.extend((first_vertex + a, first_vertex + b, first_vertex + c))
    return out


def decode_vehicle_model(data: bytes, model_name: str | None = None) -> Dict[str, object]:
    """Decode all observed visual packet types from a Zeebo vehicle component."""
    decoded = maybe_refpack(data)
    _require(len(decoded) >= 52 and decoded[:4] == b"\x7fELF", "not an ELF32 object")
    _require(decoded[4] == 1 and decoded[5] == 1, "not ELF32 little-endian")

    header = struct.unpack_from("<16sHHIIIIIHHHHHH", decoded, 0)
    _, _, machine, _, _, _, section_offset, _, _, _, _, section_size, section_count, string_index = header
    _require(machine == 8 and section_size == 40, "unsupported MIPS ELF")
    _require(section_offset + section_size * section_count <= len(decoded), "ELF section table truncated")

    sections = []
    for i in range(section_count):
        v = struct.unpack_from("<IIIIIIIIII", decoded, section_offset + i * section_size)
        sections.append({
            "index": i,
            "name_offset": v[0],
            "offset": v[4],
            "size": v[5],
            "entry_size": v[9],
        })

    _require(string_index < section_count, "bad ELF string-table index")
    shstr = sections[string_index]
    names = decoded[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for section in sections:
        section["name"] = _cstring(names, section["name_offset"])
        _require(section["offset"] + section["size"] <= len(decoded),
                 f"section {section['name']} truncated")
    named = {s["name"]: s for s in sections}

    data_section = named[".data"]
    rel_section = named[".rel.data"]
    string_table = named[".strtab"]
    symbol_table = named[".symtab"]

    strings = decoded[string_table["offset"]:string_table["offset"] + string_table["size"]]
    sent = symbol_table["entry_size"] or 16
    _require(sent == 16 and symbol_table["size"] % 16 == 0, "unsupported symbol table")
    symbols = []
    for i in range(symbol_table["size"] // 16):
        off = symbol_table["offset"] + i * 16
        no, value, size, info, other, sec = struct.unpack_from("<IIIBBH", decoded, off)
        symbols.append({
            "name": _cstring(strings, no) if no else "",
            "section": sec,
        })

    models = [
        s["name"].split(":::", 1)[1]
        for s in symbols
        if s["section"] != 0 and s["name"].startswith("__Model:::")
    ]
    _require(models, "component has no __Model symbol")
    selected = model_name or sorted(models)[0]
    _require(selected in models, f"model not found: {selected}")

    blob = decoded[data_section["offset"]:data_section["offset"] + data_section["size"]]
    rent = rel_section["entry_size"] or 8
    _require(rent == 8 and rel_section["size"] % 8 == 0, "unsupported relocation table")
    relocs = []
    for i in range(rel_section["size"] // 8):
        ro, ri = struct.unpack_from("<II", decoded, rel_section["offset"] + i * 8)
        si, typ = ri >> 8, ri & 0xFF
        if si < len(symbols):
            relocs.append((ro, symbols[si]["name"], typ))

    supported = {"NFSCar_TextureShiny", "NFSCar_Window", "NFSCar_Gouraud"}
    descriptors = []
    for ro, symbol, typ in relocs:
        if typ == 2 and symbol in supported:
            _require(ro + 8 <= len(blob), "packet relocation outside .data")
            stream = struct.unpack_from("<I", blob, ro + 4)[0]
            descriptors.append((stream, ro, symbol))
    _require(descriptors, "component has no supported visual packets")

    vertices = []
    indices = []
    groups = []
    seen = set()

    for packet_index, (stream, descriptor, renderer) in enumerate(sorted(descriptors)):
        key = (stream, renderer)
        if key in seen:
            continue
        seen.add(key)

        first_v = len(vertices)
        first_i = len(indices)

        if renderer in ("NFSCar_TextureShiny", "NFSCar_Window"):
            # PSP prefixes the otherwise compatible packet header with u32=1.
            # Its vertex record remains the Zeebo 8x int16 layout.
            header = stream + 4 if struct.unpack_from("<I", blob, stream)[0] == 1 else stream
            _require(header + 0x18 <= len(blob), "primitive header truncated")
            count = struct.unpack_from("<H", blob, header + 0x10)[0]
            start = header + 0x18
            end = start + count * 0x10
            _require(count >= 3 and end <= len(blob),
                     f"{renderer} stream truncated")
            for i in range(count):
                uv_u, uv_v, nx, ny, nz, px, py, pz = struct.unpack_from(
                    "<8h", blob, start + i * 0x10
                )
                vertices.append((
                    px / 409.6, py / 409.6, pz / 409.6,
                    nx / 32767.0, ny / 32767.0, nz / 32767.0,
                    (uv_u - 8192) / 256.0,
                    (uv_v - 8192) / 256.0,
                ))
        else:
            # Gouraud stream: count at +0x18; XYZ int16 records at +0x20.
            _require(stream + 0x20 <= len(blob), "Gouraud header truncated")
            count = struct.unpack_from("<H", blob, stream + 0x18)[0]
            start = stream + 0x20
            end = start + count * 6
            _require(count >= 3 and end <= descriptor and end <= len(blob),
                     "Gouraud stream truncated")
            for i in range(count):
                px, py, pz = struct.unpack_from("<3h", blob, start + i * 6)
                vertices.append((
                    px / 819.2, py / 819.2, pz / 819.2,
                    0.0, 0.0, 1.0, 0.0, 0.0,
                ))

        indices.extend(_vehicle_strip(first_v, count))
        groups.append({
            "first_index": first_i,
            "index_count": len(indices) - first_i,
            "first_vertex": first_v,
            "vertex_count": count,
            "renderer": renderer,
            "material_id": _vehicle_material_for_packet(relocs, descriptor),
            "name": f"{selected}_PACKET{packet_index:02d}",
        })

    _require(vertices and indices, "decoded without triangles")
    return {
        "model": selected,
        "vertices": vertices,
        "indices": indices,
        "groups": groups,
        "packet_count": len(groups),
        "triangle_count": len(indices) // 3,
    }
