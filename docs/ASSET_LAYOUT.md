# Asset layout

## `assets/source/`

Original or minimally transformed source material. Use this when a normalized
interpretation is incomplete or suspect.

```text
assets/source/
├── vehicles_viv/        canonical vehicle BIG archives
├── vehicle_global/      global car/wheel MSH libraries
└── romfs_raw/           source-oriented snapshot of original formats
```

`romfs_raw/` intentionally excludes old port-generated N3M/T3X/PNG/JSON output.
It keeps relevant source formats such as:

- `.o`
- `.msh`
- `.apt`
- `.const`
- `.cdl`
- `.bin`
- `.viv`
- `.mfn`
- `.ubc`

## `assets/normalized/`

Primary implementation handoff.

This layer is platform-neutral and should be preferred by Codex for new systems.

```text
assets/normalized/
├── vehicles/
│   └── <CAR>/
│       ├── vehicle.json
│       ├── materials.json
│       ├── materials.mtl
│       ├── parts/<slot>/*.obj
│       └── textures/*.png
├── shared/
│   ├── vehicle_materials/
│   └── wheels/
├── world/
│   ├── garage/
│   ├── palmont/
│   ├── races/
│   └── races_debug_obj/
├── audio/
├── ui/
└── manifests/
```

OBJ/MTL exists for inspectability. It is not a recommendation to parse OBJ on
the 3DS.

## `assets/generated/3ds/`

Rebuildable current runtime intermediates.

```text
assets/generated/3ds/
├── vehicles/       N3P1 + vehicle.cfg + decoded PNG sources
└── vehicle_global/
```

These files are allowed to change when the runtime format changes.

## `romfs/`

The subset currently copied into the `.3dsx`. This is build output/staging, not
source-of-truth.

## Reference indexes

`reference/indexes/` contains machine-readable indexes for targeted work on
`jogo.c` and CDL structures.

## Why the layers matter

Do not solve a runtime problem by editing `assets/normalized/` manually.

If a normalized asset is wrong:

```text
source binary
→ decoder fix
→ regenerate normalized asset
→ regenerate runtime asset
```

That keeps the project reproducible.

## Native scene output

`assets/generated/3ds/world/garage.n3s` and `garage.json` are reproducible from
normalized garage inputs. `world/scene_tex/<hash>.rgba` holds external shared
PICA RGBA8 payloads. `romfs/world/` is their staging copy. Neither N3P vehicles
nor normalized garage assets are rewritten by this pipeline.
