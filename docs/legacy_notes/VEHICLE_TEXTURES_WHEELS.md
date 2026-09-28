# Vehicle textures and wheels — recovered format notes

## Global vehicle libraries

The original Carbon Zeebo RomFS contains four SHPM libraries used by the car renderer:

| file | material ids | recovered names |
|---|---|---|
| `race_car_common.msh` | 1000..1004 | `CARBONFIBRE`, `CarBottom`, `DRIVER`, `MESH`, `Carbon_red_legend` |
| `race_car_modable.msh` | 1500..1501 | `PAINT`, `WINDOW` |
| `wheel_modable.msh` | 1990 | `DUMMY_WHEEL` |
| `wheel_common.msh` | 1995..1996 | `TIRE_BACK`, `CALIPER` |

These are shared by all car VIVs and must not be searched inside each individual vehicle archive.

### PAINT / material 1500

`1500 PAINT` is a 128x128 0x1058 RGB565 texture whose decoded pixels are uniformly
red. It is a modulation source rather than a unique body diffuse map. The original
game exposes `Career.GetCarPaint` / `Career.UpdateSelectedCarPaint`, and the runtime
supplies the selected paint colour.

The 3DS renderer therefore handles material `1500` as a dynamic tint class.
`VehicleAsset.paint_rgba` contains the selected colour. Do not map material 1500 to
a supposed per-car PNG.

### WINDOW / material 1501

`1501 WINDOW` is a 4x4 direct RGBA8888 texture with black RGB and alpha around
0.84–0.89. It is loaded as a shared texture and rendered in the transparent pass.

## SHPM formats currently decoded

- `0x0073`, `0x1073`, `0x9073`: 8bpp indexed, 256-entry RGBA8888 palette.
- `0x3076`: 4bpp indexed, 16-entry ABGR4444-packed palette.
- `0x5077`: 4bpp indexed, 16-entry RGBA8888 palette (64 bytes).
- `0x1058`: direct RGB565.
- `0x505B`: direct RGBA8888.

`0x5077` was recovered from the original texture-format handling in `jogo.c`. The
decompiled size calculation is `width*height/2 + 0x40`, matching a 16-colour
RGBA8888 palette plus packed 4-bit indices.

## wheels.viv

`wheels.viv` is BIGF with 229 RefPack-compressed `.msh` entries. After decompression
all 229 entries are SHPM `0x5077` textures. No 3D mesh payload has been found in
this archive.

The original hashed BIG entry names are not useful. Each SHPM contains its
canonical runtime name in its footer. Examples:

- `SKYLINE_TIRE_STYLE00`
- `240SX_TIRE_STYLE00`
- `BBS_STYLE02_BLACK`
- `BBS_STYLE02_CHROME`
- `ENKEI_STYLE01_*`

`tools/build_vehicle_global_assets.py` resolves these names and writes
`assets_dump/vehicle_global/wheels.json`.

The decompiled game confirms that wheel textures are requested by canonical names,
including `%s_TIRE_STYLE00`, `%s_TIRE_LEGEND`, and selected aftermarket style/color.

## Geometry

`geometry.bin` inside every vehicle VIV is loaded by the original `CarLoader` as
**collision volume data**. It is not a missing visual customization mesh archive.

Wheel transforms are assembled by the original car renderer from per-car wheel data.
The actual common/procedural wheel geometry path is still under investigation. Do
not manufacture wheel meshes and label them as recovered original geometry.

## Runtime layout

After `tools/pack_runtime.py` runs with devkitPro installed:

```text
romfs/
  vehicle_global/
    materials.json
    tex/
      1000.t3x
      1001.t3x
      1002.t3x
      1003.t3x
      1004.t3x
      1501.t3x
      1990.t3x
      1995.t3x
      1996.t3x
    source/
      1500_PAINT.png
  wheels/
    wheels.json
    tex/
      SKYLINE_TIRE_STYLE00.t3x
      BBS_STYLE02_CHROME.t3x
      ...
```

Material 1500 is intentionally not turned into a sampled T3X for car body rendering.
