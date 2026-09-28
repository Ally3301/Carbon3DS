# Native 3DS vehicle material renderer

Date: 2026-09-25

## Decision

Vehicle rendering is now intentionally implemented as a small set of
deterministic Nintendo 3DS/PICA200 material classes.

The port no longer tries to reproduce the original Zeebo/EAGL render-state
machine instruction-for-instruction. `jogo.c` and recovered assets remain the
source for **semantic classification**, while Citro3D owns the actual GPU
implementation.

This change applies only to vehicle rendering.

The validated PocketGarage/N3S rendering path is intentionally unchanged:

- `draw_scene()` unchanged;
- `scene_shader.v.pica` unchanged;
- N3S scene assets unchanged;
- garage textures/material conversion unchanged.

## Material classes

Portable classification lives in:

```text
include/vehicle_material_policy.h
src/vehicle_material_policy.c
```

Current classes:

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

### PAINT

Material 1500.

Current native reconstruction:

```text
selected paint colour × existing vertex lighting
```

The per-car `*_DETAILS` relationship discovered in `jogo.c` remains recorded in
the asset metadata, but it is **not sampled directly**.

The direct `DETAILS × PAINT` experiment visibly regressed vehicle rendering on
hardware and is therefore rejected until the original coordinate/channel
semantics are understood.

### TEXTURED

Direct local or global texture, including known headlight, taillight and
badging packets:

```text
T3X texture × vertex lighting
```

The tex3ds subtexture transform is retained.

### WINDOW

Material 1501 or an `NFSCar_Window` renderer packet.

Recovered WINDOW has black RGB but meaningful alpha. Sampling its RGB literally
made the glass black. The native 3DS path therefore uses:

```text
RGB   = smoky constant × vertex lighting
alpha = recovered WINDOW texture alpha
```

Window pass state is explicit:

```text
alpha blend: SRC_ALPHA / ONE_MINUS_SRC_ALPHA
depth test:  on
depth write: off
culling:     none for now
```

### CARBON / UNDERBODY

Known global textures are sampled directly with vertex lighting. Missing
resources have deterministic fallback colours.

### GOURAUD

No texture binding is invented. The packet uses the existing lit primary colour
path.

### SPECIAL_DYNAMIC

Material 9999.

Host analysis disproved the old assumption that 9999 universally means a
crew-tag decal. `PURSUIT*2` vehicles use 9999 over substantial BODY geometry
while also shipping valid BADGING/DETAILS/HEADLIGHT/TAILLIGHT images.

Until the original `GAME::Material`/CopCarRenderInfo lookup is decoded, 9999 is
rendered as a neutral deterministic surface. Do not bind unrelated local images
by guesswork.

## Explicit GPU state

Every vehicle opaque/window pass now re-establishes:

- culling;
- alpha test;
- alpha blend;
- depth test/write mode.

This prevents PocketGarage/UI/previous-material state from leaking into vehicle
geometry.

Thin HOOD/SPOILER components are currently two-sided (`GPU_CULL_NONE`) while
the original CompiledModel per-component cull flags remain unresolved.

## Host geometry diagnostic

`tools/diagnose_vehicle_host.py` compares generated N3P geometry with normalized
OBJ geometry after the documented coordinate conversion.

Current result:

```text
830/830 component pairs matched
comparison failures: 0
max position error: ~7.1e-9
UV error: 0
normal error: floating-point noise only
```

Therefore a HOOD/SPOILER that is correct in OBJ but deformed/missing on hardware
must be investigated after N3P generation:

```text
semantic selection
→ N3P load/lifetime
→ slot submission
→ GPU state/material
```

It is not justified to rewrite the geometry converter based on the current
evidence.

## Runtime diagnostics

The garage bottom screen now prints:

```text
H <vertices>/<triangles>/<groups>
S <vertices>/<triangles>/<groups>
```

for the currently loaded HOOD and SPOILER.

When cycling a customization option:

- if these counts change as expected but geometry does not, investigate draw
  state/material;
- if they stay zero/wrong, investigate selection/load;
- compare the selected semantic option against the host diagnostic.

## Vehicle families found host-side

Current normalized/generated assets separate into:

```text
29 customizable_racecar
 6 fixed_traffic_direct_texture
 4 fixed_special_direct_materials
 4 special_dynamic_material_path
```

Examples:

- `TAXI`: BODY directly references material 0000 / `TAXI_DETAILS`.
- normal player cars: BODY commonly uses PAINT 1500 plus direct light/badging.
- `PURSUIT*1`: direct local material IDs.
- `PURSUIT*2`: BODY heavily uses 9999 and needs specialized material decoding.

Do not force every vehicle through RaceCarRenderInfo assumptions.

## Next work

After hardware validation of this stable renderer:

1. diagnose HOOD/SPOILER using the new loaded-part counters;
2. resolve CARBONFIBRE accessories separately from PAINT accessories;
3. inspect `CopCarRenderInfo` / `GAME::Material` for 9999;
4. implement separate headlight glow/effect pass;
5. recover wheel/hub transforms before redesigning procedural wheel geometry.
