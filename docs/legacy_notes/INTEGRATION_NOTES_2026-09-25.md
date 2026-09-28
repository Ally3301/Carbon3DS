# Vehicle component + texture integration — 25 Sep 2026

This revision keeps the EAGL/N3P vehicle path and extends it with the original
shared car/wheel material libraries.

## Integrated

- 43 vehicle VIV archives.
- 830 independent EAGL vehicle components.
- 100 BODY, 94 BASE, 67 HOOD, 458 SPOILER, 82 side crew-tag and 29 hood
  crew-tag components.
- `NFSCar_TextureShiny`, `NFSCar_Window` and `NFSCar_Gouraud`.
- 159 car-local SHPM textures (the new decoder also recovers RX8 0x5077).
- 10 global material textures from `race_car_common`, `race_car_modable`,
  `wheel_common` and `wheel_modable`.
- 229 canonical wheel/tire textures from `wheels.viv`.
- N3P1 runtime loader with bounds/format validation.
- Runtime vehicle manifest (`vehicle.cfg`) per car.
- Body/base upgrade pairing.
- Dynamic material 1500 / PAINT support.
- Shared 1000..1004 and 1501 material loading.
- Patched CDL world decoder retained; combined-world OBJ exporter included.

## Important recovered semantics

`1500` is `PAINT`, a 128x128 uniform red RGB565 modulation texture. It is not a
missing per-car diffuse map. The actual paint colour is supplied dynamically by
the original game, so the 3DS renderer now treats 1500 as a dynamic tint class.

`1501` is `WINDOW`, a 4x4 RGBA8888 texture with translucent black pixels.

`wheels.viv` is a BIGF containing 229 RefPack-compressed SHPM textures in format
0x5077. It does **not** contain wheel mesh geometry. Canonical names are recovered
from each SHPM footer.

Every vehicle VIV also contains `geometry.bin`; the decompiled CarLoader loads it
as collision-volume data, not visual customization geometry.

## Build verification

`tools/test_host.py` was executed and:

- loaded all 830 real N3P files under UBSan;
- rejected malformed N3P/archive inputs;
- validated all 10 global material entries;
- confirmed PAINT decodes to uniform red;
- confirmed WINDOW alpha data;
- validated all 229 wheel textures as 64x64 0x5077 images;
- verified all 830 visual components remain namespaced to their own vehicle.

The current environment does not contain devkitPro, so no new `.3dsx` was
cross-compiled here. On the devkitPro machine run:

```sh
make -j2
make test
```

`make assets` converts the recovered PNGs to T3X.

## Runtime paths

```text
romfs:/vehicles/<CAR>/vehicle.cfg
romfs:/vehicles/<CAR>/parts/<slot>/*.n3p
romfs:/vehicles/<CAR>/tex/<material>.t3x

romfs:/vehicle_global/tex/1000.t3x
...
romfs:/vehicle_global/tex/1501.t3x

romfs:/wheels/wheels.json
romfs:/wheels/tex/<CANONICAL_WHEEL_NAME>.t3x
```

Material 1500 intentionally has no normal sampled T3X path for the body. The
renderer uses `VehicleAsset.paint_rgba`.

## Still open

- recover the exact common/procedural wheel geometry path and use the 229 wheel
  textures on it;
- reproduce the original paint reflectance/specular behavior, beyond the current
  dynamic colour tint;
- diagnose the reported body-kit selection anomaly and missing visible spoilers;
- integrate the recovered CDL city into the runtime renderer.
