
# Outdated (Replaced By PsP Extraction)

**Visual mesh extracted successfully from psp version of carbon**



# Wheel extraction audit — 2026-09-26 (OUTDATED)

This audit uses the original Brew module (`nfs.mod`) and the supplied untouched
`nfsresources` dump. It replaces an earlier unsupported `rideInfo + 0x124`
claim.

## Confirmed sources

- `wheels.viv` contains 229 RefPack-compressed SHPM images. It contains no
  EAGL/ELF object and therefore no visual wheel mesh.
- `wheel_modable.msh` and `wheel_common.msh` are material libraries only:
  `DUMMY_WHEEL` (1990), `TIRE_BACK` (1995), and `CALIPER` (1996).
- `geometry.bin` is loaded by `CarLoader` as collision-volume data. It is not
  a visual wheel archive.
- `rims.o`, `paintRims.o`, and `PaintRims.o` are frontend EAGL models used by
  the rim-selection UI. Their matching MSH files are SHPM UI textures; they
  are not driving-car wheel geometry.
- `global/compiledubiregistires.viv` is preserved at
  `assets/source/vehicle_global/compiledubiregistires.viv` (SHA-256
  `91fce764acc46c514e23f5c74bfe2705fe652969b197d861d758de52aa44a08b`).
  Its three RefPack records decode to UBI registries, not meshes:
  `frontend.ubc`, `game.ubc`, and `RaceStartCam.ubc`. `frontend.ubc` includes
  PocketGarage's `Wheel` camera setting, which may later improve rim-preview
  framing.

## Full vehicle-component scan

All 830 MIPS/EAGL vehicle components from the 43 original car VIV archives were
inspected by defined `__Model` symbol, model name, renderer packet and material
reference. Each component maps to BODY, BASE, HOOD, SPOILER or crew-tag content.
No component name contains wheel/tire/rim semantics and none binds materials
1990 (`DUMMY_WHEEL`), 1995 (`TIRE_BACK`) or 1996 (`CALIPER`).

The visual wheel is therefore not a hidden component that the N3P converter
failed to classify. Original construction is a separate runtime path driven by
car state and the global wheel texture libraries.

## Actual wheel transform evidence

In the original `nfs.mod`, decompiled as `jogo.c`, the routine at
`FUN_0019f548` iterates four records:

```c
record = car_type_table + car_index * 0xda0 + wheel_index * 0x30;
position = record[0x120..0x12c];
```

It consumes all four vectors together with vehicle orientation before doing a
world query. This is evidence for four per-wheel runtime records. It is not
proof of a serialised wheel mesh or of a `rideInfo + 0x124` table.

The backing `0xda0` car table is runtime-populated: the original module's
corresponding file range is BSS/zero-filled. Extracting its final values needs
one of the following, in order of fidelity:

1. instrument the original Zeebo executable after `allcars.viv` is loaded and
   dump the 43 records;
2. reverse the car-type initialiser that fills the table from original data;
3. retain the current geometry-derived placement as an explicitly authored
   fallback.

## Result 


No recovered source in this dump contains a visual wheel mesh. The project
must not label the current cylinder as recovered. The next extraction task is
runtime-record capture or car-type-initialiser decoding; either can provide
exact placement and wheel axes, while the recovered 229 images remain the
original visual source.

## Composition recovered from `RaceCarRenderInfo`

The original race renderer loads four resources into the vehicle render path:

- the selected per-car wheel atlas from `wheels.viv`;
- material `1990` (`DUMMY_WHEEL`);
- material `1995` (`TIRE_BACK`);
- material `1996` (`CALIPER`).

`FUN_0011d4fc` loads the global wheel material libraries, and
`FUN_0011cbf4` selects the car-specific tire/rim atlas. The global materials
therefore belong to the vehicle renderer, rather than to any individual car
component archive.

The native port now loads the recovered `1995.t3x` and `1996.t3x` beside the
selected wheel atlas. It composes a rotating tire backing, a hub-fixed caliper,
and the rotating authored rim/tire atlas. This recreates the original renderer's
resource model without claiming a recovered, serialized wheel mesh. The exact
Zeebo mesh topology remains an open reverse-engineering task.
