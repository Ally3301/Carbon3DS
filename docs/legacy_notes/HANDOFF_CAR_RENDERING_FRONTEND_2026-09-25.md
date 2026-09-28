# Handoff: vehicle rendering, wheels and frontend

Date: 2026-09-25

This note records the exact state after the first PocketGarage hardware runs.
It is the starting point for the next implementation session. Read the normal
project documentation first; this supplements the source-of-truth hierarchy.

## Hardware state reported by the user

The N3S1 PocketGarage scene works on target. Geometry and general scene
rendering are correct, the placeholder track runs at a stable 57 FPS, audio
still works, and repeated use showed no memory leak. The cyan lamp rectangles
were fixed by deriving `safehouse_lightbloom` coverage from its recovered red
channel and rendering that material additively.

Current failures shown by the supplied screenshots:

- procedural wheel faces do not display recovered rim images correctly;
- a customization icon appears as a nearly white square;
- the initial frontend shows a dark field and red rectangle instead of the
  recovered background/logo/menu;
- body and window material presentation is wrong;
- car headlights do not glow;
- some selected hoods and spoilers are absent or incorrect.

The procedural wheel cylinder and animations are port-authored, with the user's
explicit approval. Never describe them as recovered original wheel geometry.
The recovered wheel textures remain authoritative.

## Leading shared texture bug

`src/resources.c::resource_load_tex()` imports a T3X with
`Tex3DS_TextureImportStdio()`, frees the `Tex3DS_Texture` metadata and retains
only `C3D_Tex`. This loses the first `Tex3DS_SubTexture`, including `left`,
`right`, `top`, `bottom` and rotation. Both PICA shaders pass UVs through
unchanged, and `renderer.c::ui_quad()` always emits 0..1 UVs.

This code path is shared by the garage icons, initial frontend images,
vehicle-local textures, common vehicle materials and recovered wheel T3X files.
It is a strong, code-supported explanation for the white icon, broken frontend
and incorrect wheels, and may explain part of the car material problem. Fix the
runtime contract before changing decoded images or normalized assets.

Recommended implementation:

1. Add a runtime texture wrapper containing `C3D_Tex` plus four corner UVs or
   an affine UV transform.
2. Copy subtexture metadata before `Tex3DS_TextureFree()`.
3. Use the official tex3ds `TopLeft`, `TopRight`, `BottomLeft` and `BottomRight`
   helpers; do not infer rotation only from image dimensions.
4. Map source `(u,v)` from top-left, top-right and bottom-left. Confirm the
   decoded vehicle v convention with one known texture.
5. Apply the mapping in `scene_shader.v.pica` for UI and `vshader.v.pica` for
   vehicles/wheels, or pre-transform UI vertices and use a vehicle uniform.
6. Set identity for N3S1 raw scene textures, which do not use tex3ds metadata.
7. Host-test all four corners, including a rotated-subtexture case.

Relevant files are `src/resources.c`, `include/resources.h`,
`src/vehicle_asset.c`, `include/vehicle_asset.h`, `src/renderer.c`, both PICA
shaders, and `/opt/devkitpro/libctru/include/tex3ds.h`.

Do not repair this by flipping or cropping every PNG. That would hide a runtime
metadata bug and violate the asset hierarchy.

## Citro2D text issue

Custom PICA UI rectangles draw, but Citro2D text was absent in the user capture.
The HUD functions switch from the custom scene shader to `C2D_Prepare()` and
`C2D_Flush()` after manually setting Citro3D state. Check the documented
Citro2D/Citro3D composition order, render target, depth state and frame
lifetime. Resolve this after the T3X issue.

## Targeted findings from `jogo.c`

Only small indexed regions were read.

### Compiled vehicle packets

Lines about 188940-189330 correspond to `source\\CompiledModel.cpp` and an
allocation tagged `ZeeboGeometry`. The routine enumerates names beginning with
`NFSCar_` and handles `T`, `W` and `G` discriminator characters differently.
Its optimized geometry count explicitly subtracts `NFSCar_G...` entries before
allocating 28-byte geometry records. This is evidence that Gouraud packets are
not ordinary textured/window optimized batches. The current decoder includes
all three packet classes in N3P. Do not remove Gouraud packets from this clue
alone; compare a failing component's packet inventory first.

The routine recognizes component-name suffixes including `_BK_`, `_HOOD_`,
`BASE` and `BODY`, and sets per-geometry flags according to component type and
vertex count. Hood/body/base behavior is therefore not solely a generic mesh
draw in the original. The current renderer draws all loaded slots through one
loop, so missing accessories need both slot-state checks and packet/flag study.

The original draw path binds a geometry resource when it changes, configures
three vertex streams, toggles state flag `0xb50` for selected batches and draws
each record. Retain per-group renderer metadata in N3P.

### Headlight effects

Lines about 74690-74730 load separate resources named `HeadlightFlareInner`,
`HeadlightFlareOuter`, `HeadlightGlow`, `copred` and `copblue`. The first three
are independent effect handles. Headlight glow needs a separate additive effect
pass/entity at light anchors; a bright body diffuse texture cannot reproduce
it. Anchor transforms and enable rules remain unknown. A temporary authored
billboard is acceptable if clearly labeled port-authored.

## Current N3P extraction facts

`tools/zeebo_vehicle.py::decode_vehicle_model()` accepts relocations for
`NFSCar_TextureShiny`, `NFSCar_Window` and `NFSCar_Gouraud`. TextureShiny and
Window use 16-byte vertices with position, normal and UV. Gouraud uses a
position-only stream. Renderer class and material ID remain per N3P group. UV
decode is `(raw - 8192) / 256` and may legitimately exceed 0..1 slightly; do
not clamp in the runtime.

`draw_vehicle_pass()` classifies transparency only from the Window renderer.
It draws body, base, hood, spoiler and crew-tag slots in order. Make window
blend, depth-write and cull state explicit rather than relying on inherited GPU
state. Inspect culling for thin hood/spoiler meshes. Material 1500 remains
dynamic PAINT; 1501 is WINDOW. Do not invent `1500.png`.

## Hood and spoiler diagnostic sequence

Use one fixed failing car and semantic original option index.

1. Log slot, original index, N3P path, vertex/index/group counts and load result
   whenever a transaction commits.
2. Verify body and base share the same original upgrade index after every car
   or kit change.
3. Reset/clamp hood and spoiler values against the new car before publishing it.
4. Log each group's renderer, material, index range and selected draw pass.
5. Add a temporary debug tint per slot to prove geometry submission.
6. Temporarily disable face culling. If the accessory appears, correct
   winding/cull classification rather than duplicating triangles.
7. Validate index ranges with the host N3P loader; never edit OBJ/N3P by hand.
8. Compare the failing part's packet classes with the targeted original branch.

## Wheel follow-up

Fix T3X mapping first, then axle placement. The current wheel is a 16-segment
cylinder sized from body bounds, with one recovered image on both faces. After
mapping is fixed, render one wheel at the origin, verify axis/winding/front-back
UVs, verify the vehicle `+X` length to game `+Z` transform, and tune placement
across a coupe, muscle car, SUV and police car.

## Safe next order

1. Implement and host-test T3X subtexture preservation/mapping.
2. Cross-build and validate frontend, icons, one local car texture and one wheel.
3. Make opaque/window/additive vehicle GPU state explicit.
4. Add deterministic hood/spoiler logging and debug tint; fix proven failure.
5. Add authored additive headlight glow using recovered effect imagery if found,
   otherwise a documented procedural radial texture.
6. Expand customization only after these are stable. Do not start Palmont yet.

Run `make test` and `make -j2`. Hardware acceptance requires visible/oriented
frontend images and five icons, recognizable wheel faces, correct PAINT and
translucent WINDOW behavior, reliable hood/spoiler selection without stale
parts, borderless additive headlights, stable performance near 57 FPS, no leak
or crash across repeated transitions, and unchanged audio.



## T3X subtexture implementation update

The leading loader bug described above has now been implemented as a runtime
fix. `resource_load_tex()` copies the first tex3ds subtexture mapping using the
official corner helpers before freeing the Tex3DS metadata. UI uses mapped quad
corners; the vehicle shader and procedural wheel face receive an affine mapping
uniform. Portable ordinary/rotated mapping tests pass.

Hardware validation is still required. If UI passes but vehicle mapping is
vertically inverted, investigate only the vehicle logical-V convention rather
than modifying source PNGs.
