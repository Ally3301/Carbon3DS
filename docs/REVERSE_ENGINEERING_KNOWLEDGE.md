# Reverse-engineering knowledge

Only documented evidence should be promoted into runtime architecture.

## `jogo.c`

`reference/decompilation/jogo.c` is large decompiler output, not compilable
original source. Use targeted searches, not full-file interpretation.

Useful indexed evidence:

### PocketGarage

- string `tracks\l3rl_3401.viv`: around line 8553
- use near line 146371
- `PocketGarage_Orbit_Cam`: use near line 146377
- `PocketGarage_Loaded_Group`: use near line 146400
- `PocketGarage_Cam_Controller`: use near line 146978

This led to the validated original garage reconstruction from `A1.cdl + A1.msh`.

### Track streamer

- `SHAPEFILE_TrackStreamer` strings around lines 4700 / 4807
- uses around lines 99008 / 100361
- `R1.cdl` string around line 4797

Recovered track data confirms R1 is race-specific content layered over the city.

### Car loading / compiled model

- `CarLoader.cpp` string around line 13732
- `CompiledModel.cpp` strings around lines 11060+
- `ZeeboGeometry` string around line 11063
- ZeeboGeometry use around line 189115

This matches the recovered vehicle `.o` ELF/MIPS EAGL geometry path.

Targeted inspection around lines 188940-189330 adds these observations:

- compiled-model construction distinguishes `NFSCar_` packet classes and
  excludes `NFSCar_G...` entries from one optimized geometry count;
- component names matching patterns including `_BK_`, `_HOOD_`, `BASE` and
  `BODY` influence per-geometry flags;
- the draw path retains per-geometry records and toggles render state around
  flagged batches.

These are behavioral clues, not a completed semantic decode. Preserve packet
renderer classes and component identity while investigating hood/spoiler bugs.

### Headlight effects

Targeted inspection around lines 74690-74730 shows separate effect lookups for
`HeadlightFlareInner`, `HeadlightFlareOuter` and `HeadlightGlow`, followed by
`copred` and `copblue`. Headlight luminosity is therefore an independent effect
system rather than only a bright vehicle diffuse material. Anchor transforms
and activation rules remain undecoded.

### Dynamic paint

- `Career.GetCarPaint`: uses around lines 103044 / 103135
- `Career.UpdateSelectedCarPaint`: around 103084 / 103177

This supports treating material 1500 as dynamic paint, not a missing per-car PNG.

### Wheel texture naming

- `TRAFFICCAR_TIRE_STYLE00`: around line 9347
- `%s_TIRE_STYLE00`: around line 9350
- uses around lines 161881 / 161897
- wheel library error/name strings around 9345+
- `wheel_modable%s` and `wheel_common%s`: uses around 162253+

`wheels.viv` extraction independently confirms named tire/wheel texture resources.

### Global car materials

- `race_car_common%s`: use around line 162240
- `race_car_modable%s`: use around line 134996
- `wheel_common%s`: around line 162254
- `wheel_modable%s`: around line 162253

### Vehicle collision data

`geometry.bin` is referenced by CarLoader-related strings around line 13748.
Current evidence indicates it is collision-volume data, not missing visual wheel
geometry.

### Camera

`DriveCameraMover(DCC)` use around line 165340.
`DriveCameraMover(Player)` use around line 218176.

Current camera code is inspired by these concepts but is not a recovered 1:1
implementation.

### Frontend

`NFS_GoToScreen` appears around lines 43668, 66681 and 137430.
APT/CONST are preserved but frontend behavior is not reconstructed yet.

### Audio

`Track%d.caf` string near line 514. This identifies original naming/behavior
context; current port uses decoded WAV/OGG through NDSP instead.

## CDL knowledge

Validated properties are documented in `docs/legacy_notes/RECONSTRUCAO.md`.

Important principles:

- a CDL section is not just a triangle list;
- section references can point to shared material/resource sections;
- retain section identity;
- unusual display-list types 2/3/5 must not be thrown away;
- Palmont should remain sectioned for streaming.

## SHPM knowledge

The normalized pack uses the corrected palette interpretation.

Currently decoded families include the formats documented in
`docs/legacy_notes/VEHICLE_TEXTURES_WHEELS.md`.

If a future texture looks noisy:

1. verify SHPM format;
2. verify palette location/packing;
3. verify texel swizzle/layout;
4. verify UV/material binding;
5. only then change runtime sampling.

Do not compensate for decoder errors in shaders.

## PAINT constructor and vehicle DETAILS texture

Targeted analysis of the RaceCarRenderInfo paint constructor shows it
looks up `paint` from the shared/modable race-car texture library and `_details`
from the car-specific texture library, retaining both as inputs to the paint
material. This explains why every recovered vehicle has a `*_DETAILS` image
that is not normally referenced as a direct numeric geometry material. The
port now records the DETAILS numeric source explicitly and uses it with dynamic
material 1500.

## Vehicle family/material divergence

Host-side manifest analysis confirms the extracted vehicles do not all use
the same material convention. Fixed traffic vehicles such as TAXI directly
reference their local DETAILS texture; normal customizable cars commonly use
1500/PAINT plus direct light/badging packets; `PURSUIT*1` uses direct local
materials; `PURSUIT*2` heavily uses material 9999. Runtime classification must
therefore account for RaceCar/Cop/traffic render-info differences rather than
forcing one mapping across all cars.
