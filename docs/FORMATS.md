# Formats summary

## Vehicle source chain

```text
<CAR>.viv (BIG4/BIGF)
├── *.o       RefPack/ELF32 MIPS EAGL visual component
├── *.msh     SHPM vehicle-local texture library
└── geometry.bin  collision-related data
```

Normalized representation:

```text
vehicles/<CAR>/
├── vehicle.json
├── parts/<slot>/*.obj
├── textures/*.png
└── materials.json
```

Current runtime representation:

```text
N3P1 component + T3X textures + vehicle.cfg
```

N3P1 is documented in `docs/legacy_notes/N3P.md`.

## Track source chain

```text
CDL + MSH
```

CDL carries compiled scene/display-list data.
MSH commonly provides SHPM material/texture resources.

For Palmont, section identity matters.

## SHPM

Texture/container family. Corrected decoder is `tools/zeebo_shpm.py`.

Do not assume every MSH is a conventional 3D mesh file.

## VIV

EA BIG4/BIGF container. Repeated internal filenames across different track
archives must retain archive context.

## APT / CONST

EA frontend/UI program/data. Preserved but not normalized semantically.

## BIN

Meaning is filename/context-specific. Known examples:

- `roadnetwork.bin`
- `gonkulator.bin`
- `WorldObjects.bin`
- `visiblesections.bin`
- `Trigulator.bin`
- vehicle `geometry.bin`

Never create one generic BIN parser based only on extension.

## Native scene runtime

N3S1 is implemented for PocketGarage: versioned little-endian section geometry,
bounds, namespaced materials and external shared GPU texture references. See
[N3S.md](N3S.md) for byte layout, limits, alpha assumptions and provenance.
