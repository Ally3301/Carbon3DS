# Vehicle renderer full review — 2026-09-25

This review was triggered by a hardware capture where:

- the selected HOOD and SPOILER were loaded (non-zero vertex/triangle/group counts);
- the hood opening remained visible;
- the spoiler appeared in an incorrect apparent location;
- vehicle textures still showed stretching/glitches;
- PocketGarage itself remained visually correct.

## Findings

### 1. N3P conversion is not the current geometry culprit

Host comparison still reports:

```text
830/830 generated N3P parts match normalized OBJ geometry
comparison failures: 0
UV mismatch: 0
```

For the captured MAZDA3 specifically:

```text
BODY upgrade_00
bounds x -2.236 .. 2.236
       y  0.000 .. 1.277
       z -0.981 .. 0.981

HOOD style_00
bounds x  0.996 .. 2.141
       y  0.557 .. 0.813
       z -0.828 .. 0.828

SPOILER style_14
bounds x -1.677 .. -1.328
       y  1.145 .. 1.279
       z -0.481 .. 0.479
```

Those coordinates are coherent: hood is at the front and spoiler at the rear
in the same local coordinate space as BODY.

The hardware HUD also reported:

```text
H 65/63/1
S 118/116/1
```

which matches the selected MAZDA3 assets.

### 2. Vehicle indexed drawing was an unnecessary second GPU path

PocketGarage uses its own validated N3S renderer.

Vehicles previously used a separate `C3D_DrawElements` path with a second
linear index buffer for every independent component.

The N3P loader now expands the indexed representation once into a GPU triangle
stream:

```text
source N3P vertices + indices
→ draw_verts[index_count]
→ C3D_DrawArrays
```

The original indexed N3P data remains preserved for validation/identity.

Host tests verify for every real component that:

```text
draw_verts[i] == verts[indices[i]]
```

for all indices.

This removes index-buffer addressing/group offsets as a possible cause of the
customization geometry failure without changing the source geometry.

### 3. Logical UV addressing was wrong before T3X mapping

The earlier T3X fix correctly preserved the `Tex3DS_SubTexture` affine transform,
but the source vehicle UV was passed directly into it.

Audit of the recovered N3P assets found UVs outside 0..1 for 11 of 18 observed
material/renderer combinations.

Examples:

```text
WINDOW 1501  U -2.176 .. 1.910
CARBON 1000  U -0.711 .. 1.875
PAINT  1500  U -0.871 .. 2.508
```

Mapping these raw coordinates into the subtexture affine region can sample
outside the logical image and into tex3ds padding/backing texture.

The vehicle expanded VBO now applies a logical addressing policy first:

```text
TEXTURED    clamp
WINDOW      clamp
UNDERBODY   clamp
CARBON      repeat
PAINT       no sampled texture
UNKNOWN     untouched / no invented mapping
```

Only then does the vertex shader apply the Tex3DS subtexture mapping.

The validated N3S scene renderer is not involved in this policy.

### 4. Vehicle render state is now explicit

Vehicle passes explicitly establish:

- culling;
- alpha test;
- alpha blend;
- depth test/write mode.

This avoids inheriting state from PocketGarage/UI.

Customization meshes are temporarily two-sided until original component cull
flags are understood.

### 5. The current vehicle shader is still intentionally simple

The current shader remains a vertex-lit PICA200 shader with:

- position;
- logical texture coordinate;
- normal;
- projection/modelView;
- directional diffuse/specular approximation.

The renderer does not attempt to reproduce the entire Zeebo EAGL state machine.

## Scene isolation guarantee

This review intentionally did not change:

```text
draw_scene()
src/scene_shader.v.pica
N3S PocketGarage assets
scene conversion tools
```

`test_scene.py --validate-only` remains green.

## Hardware test interpretation

For the next build:

- If HOOD/SPOILER move into the correct positions after the DrawArrays rewrite,
  the old indexed GPU path was the presentation fault.
- If their counts remain correct but they are still misplaced/missing, capture
  the screen and selected option; next investigate matrix/uniform submission.
- If direct textures improve, the logical-UV/T3X bug was contributing.
- If BODY remains visually stable but only WINDOW/CARBON improve, continue
  material-specific reconstruction rather than changing geometry.

## Wheels

No wheel redesign is included here.

The current wheel mesh/placement is known to be procedural and does not yet use
the recovered original hub transform path.
