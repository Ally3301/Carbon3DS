#!/usr/bin/env python3
from __future__ import annotations
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "assets/generated/3ds/world/palmont/roadnetwork.nrn"
HEADER = struct.Struct("<4sIIIIIffff")
NODE = struct.Struct("<fffHH7H6x")
LINK = struct.Struct("<HH")

def main() -> None:
    data = CACHE.read_bytes()
    magic, version, source_nodes, source_edges, nodes, links, min_x, min_z, max_x, max_z = HEADER.unpack_from(data)
    assert (magic, version, source_nodes, source_edges) == (b"NRN1", 2, 2168, 3002)
    assert 1800 <= nodes <= source_nodes and 1 <= links <= 8192
    assert min_x < max_x and min_z < max_z
    assert len(data) == HEADER.size + nodes * NODE.size + links * LINK.size
    offset = HEADER.size
    for _ in range(nodes):
        x, z, y, _, edge_count, *_ = NODE.unpack_from(data, offset)
        assert math.isfinite(x) and math.isfinite(z) and math.isfinite(y) and edge_count <= 7
        offset += NODE.size
    for _ in range(links):
        a, b = LINK.unpack_from(data, offset)
        assert a < nodes and b < nodes and a != b
        offset += LINK.size
    print(f"NRN1 cache accepted: {nodes} nodes, {links} local links")
if __name__ == "__main__": main()
