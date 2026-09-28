# Full C source audit — 2026-09-25

Scope: all current `src/*.c` and public `include/*.h`.

The validated PocketGarage/N3S conversion and scene renderer were treated as a
known-good subsystem and were not redesigned.

## Executive findings

### HIGH — vehicle GPU constant colours were packed incorrectly

Several vehicle constants were written as if a 32-bit value were
`0xRRGGBBAA`.

For PICA texture-env constant colours, the byte layout used by the 3DS graphics
stack is the same layout exposed by `C2D_Color32`:

```text
bits  0..7  R
bits  8..15 G
bits 16..23 B
bits 24..31 A
```

Example old default paint:

```text
0xB74332FF
```

was intended as:

```text
R=B7 G=43 B=32 A=FF
```

but in the PICA/C2D byte layout it decodes as:

```text
R=FF G=32 B=43 A=B7
```

Therefore the default body material was red-biased and partially transparent.

The same mistake existed in WINDOW, CARBON fallback, UNDERBODY fallback,
SPECIAL_DYNAMIC, DRIVER/MESH fallbacks and wheel tread constants.

Fix:

```text
include/gpu_color.h
```

defines explicit channel packing and host regression tests verify it.

### HIGH — the "opaque" vehicle pass still used source-alpha blending

Before this audit both passes configured:

```text
SRC_ALPHA / ONE_MINUS_SRC_ALPHA
```

even for BODY/BASE/HOOD/SPOILER.

Combined with the incorrectly packed PAINT alpha, nominally opaque vehicle
geometry was blended with the garage behind it. This can make valid accessories
look embedded inside the body and can expose background/underbody detail through
painted surfaces.

Opaque vehicle pass now uses:

```text
src = ONE
dst = ZERO
depth write = ALL
```

WINDOW remains:

```text
SRC_ALPHA / ONE_MINUS_SRC_ALPHA
depth write = COLOR only
```

### HIGH — N3P geometry itself remains cleared

All 830 normalized/generated component pairs have already been compared
host-side with no position/UV mismatch beyond float noise.

The runtime additionally expands N3P indices into a linear GPU triangle stream
and validates:

```text
draw_verts[i] == verts[indices[i]]
```

for every real N3P in host tests.

The captured MAZDA3 selected assets are coherent:

```text
hood style 00:    65 vertices / 63 triangles / 1 group
spoiler style 14: 118 vertices / 116 triangles / 1 group
```

and their local bounds place the hood at the front and spoiler at the rear.

No evidence currently supports rewriting the vehicle geometry extractor.

## Runtime diagnostic added

In Garage, press:

```text
X
```

to toggle solid slot debug rendering:

```text
BODY    red
BASE    green
HOOD    blue
SPOILER yellow
```

WINDOW remains a window pass.

This lets one hardware build answer whether a selected hood/spoiler is
physically submitted in the correct position independently of textures.

## Per-file audit

### `src/main.c`

Status: OK.

Initialization/shutdown order is coherent. Audio failure is intentionally
non-fatal. Renderer/game failures have explicit error paths.

### `src/core.c`

Status: mostly OK.

The depth buffer is cleared to zero and renderer code uses reverse-Z
`GPU_GREATER`, matching normal Citro3D/PICA practice.

Minor cleanup item: `consoleInit(GFX_BOTTOM, NULL)` is called twice during core
initialization. This is redundant but not related to vehicle corruption.

Do not apply `gpu_rgba8()` blindly to `C3D_RenderTargetClear` constants; that API
and devkitPro examples traditionally express clear colours as `0xRRGGBBAA`.

### `src/input.c`

Status: OK for current prototype.

L/R are context-dependent (garage customization vs race nitro/look-back) but the
game state prevents conflicting actions from being consumed simultaneously.

### `src/audio.c`, `src/wav.c`

Status: stable.

User has already validated music/SFX/part-install audio on target with DSP
firmware. No vehicle rendering dependency was found.

### `src/game.c`

Status: selection/lifetime logic is structurally sound.

Car switches are transactional:

```text
load new VehicleAsset
apply per-car customization
FrameSync
free old VehicleAsset
publish new asset
```

Customization is keyed by vehicle-list index.

BODY/BASE semantic pairing was checked against generated manifests. All player
customizable cars have matching body/base upgrade sets. The six body-without-base
cases are fixed traffic vehicles (`TAXI`, `SEDAN`, etc.), not player body kits.

Paint palette packing was wrong and is fixed by this audit.

Garage now exposes X slot-debug mode.

### `src/vehicle_asset.c`

Status: mostly sound.

- selected option values use semantic/original indices;
- BODY and BASE are paired by semantic upgrade index;
- slot swaps wait for GPU frame completion before freeing old buffers;
- current per-slot option limit (20) exceeds observed maxima (17 spoilers,
  3 hoods);
- local textures load before global fallbacks.

Known unresolved behavior:

- `9999` is context-dependent and needs CopCar material decoding;
- wheel placement is procedural;
- hood/spoiler changes do not affect placement matrices because recovered
  component vertices are already in common vehicle-local coordinates.

The default PAINT packed colour was wrong and is fixed.

### `src/n3p_loader.c`

Status: strong.

Defensive bounds/count/finite-value checks are present.

Added compile-time ABI assertions:

```text
sizeof(N3PVertex) == 32
sizeof(N3PGroup)  == 12
```

Expanded GPU triangle stream is generated from validated indices and host-tested
against all real components.

### `src/vehicle_material_policy.c`

Status: intentionally conservative.

Known native classes:

```text
PAINT
TEXTURED
WINDOW
CARBON
UNDERBODY
GOURAUD
SPECIAL_DYNAMIC
UNTEXTURED
```

The old direct `PAINT × DETAILS` experiment remains rejected.

### `src/renderer.c`

Status: highest-risk file; audited heavily.

Good/validated:

- vehicle vertex ABI matches the official Citro3D position/UV/normal pattern;
- model matrix is shared by BODY/BASE/HOOD/SPOILER, as required by their common
  recovered coordinate space;
- scene program is rebound after vehicle/wheel draws where required;
- transparent vehicle pass restores vehicle program/modelView after transparent
  scene rendering;
- scene renderer is separate and remains unchanged.

Fixed:

- PICA constant colour packing;
- true opaque blend mode;
- deterministic vehicle depth/blend/cull state;
- vehicle logical UV addressing before Tex3DS subtexture mapping;
- indexed vehicle draw path replaced with validated expanded DrawArrays path.

Still provisional:

- vertex lighting is based on the devkitPro textured-cube style shader, not a
  recovered exact EAGL material model;
- WINDOW appearance is a native approximation;
- headlights do not yet have original glow/flare effects;
- wheel mesh/UV/placement is port-authored.

### `src/resources.c`, `src/texture_uv.c`

Status: current T3X metadata fix is retained.

`Tex3DS_SubTexture` metadata is copied before freeing the tex3ds wrapper.
Logical source UVs are transformed into backing texture UVs through the
preserved affine mapping.

### `src/scene_loader.c`, `src/scene_asset.c`

Status: known-good path.

Defensive N3S validation, source/path validation, cache ownership and texture
budgeting are present.

The real PocketGarage validator remains green. No changes from the vehicle audit
were made to the scene shader or `draw_scene()` path.

### `src/camera.c`

Status: prototype, no evidence of part-relative transform corruption.

All vehicle parts receive the same vehicle model matrix before camera view
multiplication. A camera issue would move the entire car, not only hood/spoiler.

### `src/vehicle.c`, `src/track.c`

Status: gameplay prototypes.

They affect race motion/progression, not Garage component-local geometry.

## Current hypothesis after audit

The strongest code-level fault explaining the existing capture is:

```text
wrong PICA colour byte order
+
opaque vehicle pass still alpha-blended
```

This produces semi-transparent/red-biased painted surfaces even when source
geometry is valid.

The next hardware build should be tested in both:

```text
normal renderer
X slot-debug renderer
```

If solid HOOD/SPOILER are correctly positioned after this patch, the apparent
geometry problem was primarily blending/material presentation.

If solid debug geometry is still misplaced, the next instrument should dump a
few BODY/HOOD/SPOILER object-space vertices and their CPU-transformed
model/view positions on the bottom screen. At that point textures/materials can
be excluded entirely.
