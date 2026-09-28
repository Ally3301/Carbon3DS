#!/usr/bin/env python3
"""Convert recovered Zeebo roadnetwork.bin into bounded NRN1 runtime data."""
from __future__ import annotations
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/source/romfs_raw/roadnetwork.bin"
DEST = ROOT / "assets/generated/3ds/world/palmont/roadnetwork.nrn"
MAGIC = b"NRN1"
VERSION = 2
RAW_HEADER = 0x28
POLYLINE_COUNT_OFFSET = 24
NODE_COUNT = 2168
EDGE_COUNT = 3002
POLYLINE_SIZE = 64
NODE_SIZE = 32
MAX_NODE_EDGES = 7
NODE_START_PADDING = 8
HEADER = struct.Struct("<4sIIIIIffff")
NODE = struct.Struct("<fffHH7H6x")
LINK = struct.Struct("<HH")

def main() -> None:
    raw = SOURCE.read_bytes()
    if len(raw) < 32 or raw[:4] != b"\x00\x60\x04\x80":
        raise SystemExit("unexpected roadnetwork.bin header")
    declared_nodes = struct.unpack_from("<H", raw, 18)[0]
    declared_edges = struct.unpack_from("<I", raw, 20)[0]
    polylines = struct.unpack_from("<I", raw, POLYLINE_COUNT_OFFSET)[0]
    if (declared_nodes, declared_edges, polylines) != (NODE_COUNT, EDGE_COUNT, 537):
        raise SystemExit("unexpected roadnetwork.bin counts")
    node_start = RAW_HEADER + polylines * POLYLINE_SIZE + NODE_START_PADDING
    if node_start + NODE_COUNT * NODE_SIZE > len(raw):
        raise SystemExit("truncated navigation node table")
    nodes = []
    for source_index in range(NODE_COUNT):
        at = node_start + source_index * NODE_SIZE
        x, y, z = struct.unpack_from("<fff", raw, at)
        degree = raw[at + 16]
        if not all(math.isfinite(v) and -10000.0 < v < 10000.0 for v in (x, y, z)):
            continue
        edge_ids = list(struct.unpack_from("<7H", raw, at + 18))
        # The original record reserves seven 16-bit memberships. Some trailing
        # records are non-navigation payloads; discard their invalid IDs.
        usable = [edge for edge in edge_ids[:min(degree, MAX_NODE_EDGES)] if edge < EDGE_COUNT]
        nodes.append((x, -y, z, source_index, usable))
    if len(nodes) < 1800:
        raise SystemExit("too few finite navigation nodes")
    edge_members: list[list[int]] = [[] for _ in range(EDGE_COUNT)]
    for index, (_, _, _, _, edges) in enumerate(nodes):
        for edge in edges:
            if not edge_members[edge] or edge_members[edge][-1] != index:
                edge_members[edge].append(index)
    links: list[tuple[int, int]] = []
    seen: set[tuple[int, int]] = set()
    for members in edge_members:
        for a, b in zip(members, members[1:]):
            key = (a, b) if a < b else (b, a)
            if a != b and key not in seen:
                seen.add(key); links.append(key)
    if not links or len(links) > 8192:
        raise SystemExit("invalid derived road links")
    min_x = min(p[0] for p in nodes); max_x = max(p[0] for p in nodes)
    min_z = min(p[1] for p in nodes); max_z = max(p[1] for p in nodes)
    DEST.parent.mkdir(parents=True, exist_ok=True)
    with DEST.open("wb") as out:
        out.write(HEADER.pack(MAGIC, VERSION, declared_nodes, declared_edges,
                              len(nodes), len(links), min_x, min_z, max_x, max_z))
        for x, z, y, source_index, edges in nodes:
            out.write(NODE.pack(x, z, y, source_index, len(edges), *(edges + [0] * (MAX_NODE_EDGES-len(edges)))))
        for a, b in links: out.write(LINK.pack(a, b))
    print(f"road network: {len(nodes)}/{declared_nodes} nav nodes, {len(links)} derived links, {declared_edges} source edges")
if __name__ == "__main__": main()
