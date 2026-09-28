# Carbon material + geometry-derived wheel fit — 2026-09-25

## New project baseline

This work is based on `NFS-Carbon-3DS-Port-shpm-decoder-fix(1).zip`, which is
now the authoritative project state. The single-channel customization pipeline
and its working hood/spoiler assembly are preserved.

## Carbon/pink material root cause

The remaining pink CARBONFIBRE material was not a Citro3D tint issue.

Recovered SHPM format `0x3076` stores a 16-entry RGBA4444 palette. The palette
values themselves expose the correct nibble order:

```text
CARBONFIBRE:
0x111F
0x444F
0x333F
...
```

These are naturally interpreted as opaque greys:

```text
R=1 G=1 B=1 A=F
R=4 G=4 B=4 A=F
```

The previous decoder reversed the nibbles and interpreted the low alpha nibble
`F` as red. That produced:

- pink/red carbon;
- incorrect alpha;
- incorrect colours in many `0x3076` vehicle textures.

Correct layout:

```text
bits 15..12 = R
bits 11..8  = G
bits  7..4  = B
bits  3..0  = A
```

After regeneration:

```text
1000 CARBONFIBRE      -> opaque dark grey/black
1001 CarBottom        -> opaque dark grey
1003 MESH             -> opaque dark grey
1004 Carbon_red_legend-> opaque dark red
```

All 159 local vehicle PNGs and the 10 global material PNGs were regenerated from
the original VIV/MSH source using the corrected decoder. Vehicle geometry,
customization configs and part selection were not rebuilt.

## Wheel diameter

The current runtime wheel is still a procedural 3DS mesh. The original game has
a stronger four-record wheel transform path in `jogo.c`, but the actual per-car
table has not yet been extracted as a portable asset.

As an immediate visual correction, wheel size/position is now derived offline
from the recovered BODY+BASE geometry:

```text
stock body/base
→ side-profile wheel-arch samples
→ circular fit near front/rear wheel openings
→ front_x / rear_x / radius
```

The resulting values are stored in `vehicle.cfg`:

```text
wheel_fit <front_x> <rear_x> <half_track> <radius> <center_y>
```

The runtime uses these values when available and falls back to the previous
length-based heuristic for old configs.

This is explicitly a geometry-derived approximation, not a claim that the
original Zeebo wheel records have been recovered.

Examples:

```text
MURCIELAGO diameter ≈ 0.779 model units
SUPRA       diameter ≈ 0.882
SKYLINE     diameter ≈ 0.827
CARRERA4S   diameter ≈ 0.849
```

## Validation

Host validation:

```text
C loader accepted 828 real N3P vehicle components
SHPM 0x3076 RGBA4444 nibble order validated on CARBONFIBRE
43/43 geometry-derived wheel_fit records validated
```

PocketGarage scene validation remains green and was not modified.
