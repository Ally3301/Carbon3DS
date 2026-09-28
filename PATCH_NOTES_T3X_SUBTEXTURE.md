# Patch notes — T3X subtexture mapping

This revision fixes the runtime path that discarded tex3ds subtexture metadata.

Changed implementation files:

- `include/resources.h`
- `include/texture_uv.h` (new)
- `src/resources.c`
- `src/texture_uv.c` (new)
- `include/vehicle_asset.h`
- `src/vehicle_asset.c`
- `src/renderer.c`
- `src/vshader.v.pica`
- `tests/host.c`
- `tools/test_host.py`

Documentation was updated under `docs/`.

## Expected visible impact

Hardware validation should specifically check:

1. garage icons;
2. main-menu background/logo;
3. one known vehicle-local detail texture;
4. window material;
5. stock/custom wheel face texture.

The patch does not edit normalized source PNGs and does not claim to solve
headlight glow, hood/spoiler selection or original wheel geometry.

## Build

```sh
make clean
make test
make -j2
```

A new `.3dsx` must be built; any binary from the previous archive predates this
patch.
