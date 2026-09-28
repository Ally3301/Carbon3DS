# Tools

All Zeebo-specific parsing should remain in this directory or another offline
tooling layer.

## Core decoders

### `zeebo_vehicle.py`

Vehicle RefPack / ELF32 MIPS / EAGL geometry decoder.

Known packet classes:

- `NFSCar_TextureShiny`
- `NFSCar_Window`
- `NFSCar_Gouraud`

### `zeebo_shpm.py`

SHPM texture decoder.

Important: this version contains the corrected indexed-palette interpretation.

### `decode_cdl.py`

World/scene CDL decoder used to recover PocketGarage and Palmont sections.

### `inspect_cdl.py`

Structural CDL validator/indexer.

## Pipelines

### `build_vehicle_assets.py`

```sh
python3 tools/build_vehicle_assets.py \
  assets/source/vehicles_viv \
  assets/generated/3ds/vehicles
```

Produces current N3P runtime intermediates.

### `build_vehicle_global_assets.py`

Builds current shared vehicle/wheel texture assets.

### `recover_tracks.py`

Lossless BIG extraction preserving archive identity.

Do not flatten multiple track archives into one directory.

### `export_world_obj.py`

Debug/inspection exporter, not a runtime solution.

### `pack_runtime.py`

Stages the current generated assets into `romfs/`, converting PNG to T3X.

## Analysis

### `analyze_source.py`

Indexes `jogo.c` for targeted investigation.

### `test_host.py`

Host regression tests for asset/runtime loaders.

## Adding a decoder

A new decoder should:

1. validate signatures/lengths;
2. reject unsafe offsets;
3. preserve unknown fields;
4. emit source identity + hashes where practical;
5. have a fixture/regression test;
6. produce platform-neutral normalized output before a 3DS-specific pack.
