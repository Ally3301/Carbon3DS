# Project status

Date of handoff: 2026-09-25.

## Status summary

| Area | Status | Notes |
|---|---|---|
| 3DS bootstrap / Citro3D | Prototype works | Existing runtime |
| Input / cameras | Prototype works | Not yet 1:1 |
| NDSP audio | User-validated | DSP firmware required on target setup |
| Vehicle geometry extraction | Validated | 830/830 components |
| Vehicle local textures | Validated extraction | 159 decoded in normalized v1 |
| Vehicle customization assets | Validated | Parts fit as independent meshes |
| Dynamic body paint | Semantics identified | Material 1500 |
| Wheel textures | Validated | 229/229 |
| Wheel geometry | Unknown | Do not fake as recovered |
| PocketGarage geometry | Validated | Blender visual verification |
| PocketGarage textures | Validated | Corrected SHPM decoder |
| Palmont geometry | Strongly validated | Roads/buildings/bridges visible |
| Palmont material binding | Partial | PNGs valid; shared binding needs runtime work |
| Race overlays | 40/40 decoded | Props/finish/arrows layered over city |
| roadnetwork.bin | Partial decode | 2168 nodes / 3002 edges observed |
| gonkulator.bin | Preserved | Route semantics incomplete |
| WorldObjects/Trigulator/visibility | Preserved | Requires targeted RE |
| APT/CONST frontend | Preserved | Not implemented |
| Physics | Prototype only | Procedural circular test track |
| Collision | Prototype only | Not original world collision |
| Career/police/AI | Not implemented | Future |
| Current world renderer | Procedural test | Must be replaced |

## Runtime warning

The current runtime is useful as a hardware/Citro3D harness. It is **not** the
authoritative implementation of original behavior.

Particularly provisional:

- `src/track.c`
- procedural world generation in `src/renderer.c`
- current camera tuning
- arcade physics parameters
- temporary material fallbacks

## Verified user observations

- Extracted vehicle OBJs visually render correctly.
- Different body/hood/spoiler components physically fit the same car.
- PocketGarage reconstructed scene looks correct in Blender.
- Palmont combined geometry visibly contains coherent city roads/buildings.
- Audio works in the user's 3DS/emulator setup with DSP firmware.

## PocketGarage implementation update — 2026-09-25

Native N3S1 packer, defensive portable C loader, shared texture ownership,
Citro3D scene rendering and garage enter/exit integration are implemented.
Real asset: 3,817 vertices, 1,955 triangles, 47 batches, 42 materials;
158,214 scene file bytes and 1,507,328 texture bytes.
Host tests include malformed data and failure/lifetime coverage.
Vehicle/NDSP source and normalized assets are preserved.

**Milestones A/B are awaiting on-device acceptance, not closed.**
No Palmont work was started. See [POCKETGARAGE_TEST.md](POCKETGARAGE_TEST.md).

## Hardware feedback and vehicle/frontend pass — 2026-09-25

PocketGarage geometry, texture binding, orbit, audio and repeated transitions
were user-validated on hardware at a stable 57 FPS with no observed memory leak.
One material defect remained: `safehouse_lightbloom` appeared as opaque cyan
rectangles. Its runtime texture now derives coverage from the recovered red
bloom channel and renders additively; this correction awaits a hardware recheck.

The next incremental pass is implemented and awaiting hardware validation:

- low front three-quarter garage camera closer to the original presentation;
- procedural four-wheel mesh sized from each vehicle's recovered bounds;
- original recovered stock wheel texture per car, plus recovered BBS/Enkei rims;
- wheel rotation while driving and front-wheel steering animation;
- semantic per-car body/hood/spoiler/paint/rim state across vehicle switches;
- recovered customization icons in a native garage HUD;
- recovered boot background and localized Carbon logo in an initial main menu;
- native text and layout where APT positioning/text behavior is not decoded.

The wheel mesh and hub placement are explicitly port-authored approximations.
They are not recovered original geometry. Palmont work has not started.

## Second hardware report — vehicle/frontend blockers

The lamp bloom correction was confirmed visually: PocketGarage no longer shows
the cyan blocks. Remaining screenshots show incorrect procedural wheel textures,
a white garage icon, a mostly blank initial frontend, incorrect body/window
presentation, no headlight glow, and unreliable hood or spoiler display.

Targeted inspection found that the runtime discards tex3ds subtexture UV and
rotation metadata for every `.t3x`; this affects frontend, garage icons, wheels
and vehicle textures. Targeted `jogo.c` inspection also confirmed distinct
`NFSCar_` packet treatment and separate headlight flare/glow resources. No
normalized assets were edited. Continuation steps are in
`legacy_notes/HANDOFF_CAR_RENDERING_FRONTEND_2026-09-25.md`.


## T3X subtexture runtime fix — 2026-09-25

The shared T3X loader now preserves tex3ds subtexture coordinates/rotation
instead of retaining only the backing `C3D_Tex`. UI quads map the logical image
through the recovered subtexture corners; the vehicle shader applies the same
affine transform per material and to the wheel face.

Portable host tests cover ordinary and rotated mappings and the existing asset
suite still passes. Scene validation still passes. This change is **awaiting
hardware validation** for frontend, garage icons, one known car texture, WINDOW
and wheel faces.

No normalized PNG/OBJ assets were edited.

## PAINT + DETAILS runtime pass — 2026-09-25

Targeted vehicle-material analysis established that `*_DETAILS` is a
secondary texture input to material 1500/PAINT. All 43 normalized vehicles have
exactly one DETAILS texture. The runtime now records this semantic binding in
`vehicle.cfg` and reconstructs PAINT as DETAILS × vertex lighting × selected
paint colour using two Citro3D TEV stages. Host validation passes; hardware
appearance remains pending. No normalized PNG/OBJ was edited.

## Native vehicle material renderer — 2026-09-25

The direct PAINT×DETAILS hardware experiment regressed rendering and has
been rolled back at the renderer level. Per-car DETAILS metadata is preserved
for reverse engineering but is not sampled directly.

Vehicle rendering is now classified into native PICA200 material classes with
explicit blend/depth/cull state. WINDOW uses recovered alpha with a native smoky
RGB material. Host comparison proved all 830 generated N3P components match the
normalized OBJ geometry after coordinate conversion; current HOOD/SPOILER
failures therefore occur later in selection/loading/submission/GPU state.

PocketGarage/N3S scene rendering and `scene_shader.v.pica` were not changed.

## Vehicle renderer full review — 2026-09-25

Vehicle geometry submission was simplified from indexed GPU draws to an
expanded triangle stream (`C3D_DrawArrays`) while preserving indexed N3P on
disk. The host suite validates expansion for all 830 components.

A UV audit also found 11/18 material/renderer combinations with logical UVs
outside 0..1. Vehicle texture addressing now applies class-specific clamp/repeat
before the Tex3DS subtexture transform.

PocketGarage/N3S rendering remains unchanged. Hardware validation is pending.

## Full C audit: vehicle colour/alpha bug — 2026-09-25

A complete pass over the current C runtime found a systemic vehicle-only
presentation bug: PICA TexEnv constant colours were authored as `0xRRGGBBAA`,
while the fragment constant layout stores R in the low byte and A in the high
byte. The default PAINT therefore decoded red-biased with alpha below 1.

The nominal opaque vehicle pass also still used source-alpha blending. Both are
fixed. Garage X now toggles per-slot solid debug colours to diagnose
BODY/BASE/HOOD/SPOILER independently of textures.

N3S/PocketGarage rendering remains untouched.


## Player catalogue and future traffic

`romfs:/vehicles.txt` is now the player-only catalogue. Civilian traffic and
all `PURSUIT*` police archives, including the incomplete `*2` variants, remain
in `romfs:/vehicles` but are listed by `romfs:/traffic_vehicles.txt` instead.
`romfs:/traffic_stream.cfg` reserves a six-car bounded stream pool; it is data
only until the garage/save milestone is complete.
