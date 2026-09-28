# Build and test

## Dependencies

Expected devkitPro environment:

- devkitARM
- libctru
- Citro3D
- tex3ds
- picasso
- bin2s
- 3dsxtool

Host-side tools use Python 3. Pillow is required by texture decoders.

`ffmpeg` is used by the current RomFS audio staging step.

## Rebuild vehicle runtime assets

```sh
make rebuild-vehicle-assets
```

Equivalent commands:

```sh
python3 tools/build_vehicle_assets.py \
  assets/source/vehicles_viv \
  assets/generated/3ds/vehicles

python3 tools/build_vehicle_global_assets.py \
  assets/source/vehicle_global \
  assets/source/vehicles_viv/wheels.viv \
  assets/generated/3ds/vehicle_global
```

## Host tests

```sh
make test
```

The asset test suite is intended to catch:

- invalid N3P structure;
- bounds/index corruption;
- archive traversal;
- global material regressions;
- dynamic PAINT assumptions;
- wheel texture extraction regressions.

## Build `.3dsx`

```sh
make -j2
```

`make assets` stages current assets into `romfs/`.

## Clean

```sh
make clean
```

## Do not commit

- `build/`
- `.pyc`
- `__pycache__/`
- locally produced ELF/3DSX/SMDH unless making an explicit release.

## PocketGarage N3S1

Run `make rebuild-scene-assets`, `make test`, then `make -j2`. Scene assets
are also regenerated and validated by `make assets`. `make validate-scenes`
runs the standalone host scene suite. Texture packing additionally needs tex3ds.
See [POCKETGARAGE_TEST.md](POCKETGARAGE_TEST.md) for exact SD paths, controls
and the mandatory hardware acceptance sequence, and [N3S.md](N3S.md) for format.
