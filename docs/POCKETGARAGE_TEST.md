# PocketGarage build and hardware acceptance

Implementation and host/cross-build validation are available. **Hardware visual,
performance, audio regression and long-run leak acceptance are still pending.**
Do not proceed to Palmont on the assumption that compilation proves this milestone.

## Reproduce

From the repository root, with Python 3/Pillow, host `cc`, and devkitPro installed:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
make rebuild-scene-assets
make test
make -j2
```

`make assets` regenerates and validates scene data before staging it. It also
runs the existing vehicle/audio staging; ffmpeg is still needed by that pipeline.
Standalone scene checks: `make validate-scenes`. Generated assets live in
`assets/generated/3ds/world/`; staged files live in `romfs/world/`.

Output: `nfs3ds.3dsx` with embedded RomFS. Copy it to
`sd:/3ds/nfs3ds/nfs3ds.3dsx` (optionally put `nfs3ds.smdh` alongside it), then
launch from Homebrew Launcher. Keep the already-working DSP firmware setup.

## Acceptance sequence on Old 3DS

1. Boot: PocketGarage loads directly, with the selected Skyline as an independent
   entity. No procedural circular road should appear in the garage.
2. Compare walls, floor, cabinets, signs, markings and props against the validated
   normalized Blender scene. Check UV direction, repeated patterns, brightness,
   transparency and placement while orbiting through a full turn.
3. Circle Pad orbits; its vertical axis adjusts pitch. Check for wall/roof clipping.
   Car is placed at the prototype origin; original garage car/camera transforms
   have not been recovered. Wheels remain absent as before.
4. D-pad left/right changes vehicle; up/down changes customization slot; L/R
   changes the part. Verify the existing behavior still works; the known body-kit
   and spoiler issues have not been fixed in this milestone.
5. Confirm music and part-selection effects still play. The audio implementation
   and its calls were preserved.
6. Record HUD scene identity, scene/texture KiB, triangles, draw calls, free linear
   memory and frame rate. Geometry counters cover the scene only, excluding car.
7. Press SELECT to unload the garage and show the menu. Press A to reload. Repeat
   30 times; compare free linear memory **after returning to the same loaded
   garage and same car**, waiting for loading to finish. Expect no downward trend,
   crash, mixed textures or missing geometry.
8. From the garage press A to enter the existing procedural time trial. SELECT
   returns to the real garage. Repeat 10 times and check audio and memory again.
   This time trial remains a test harness; it is not race 4001.
9. START exits. Relaunch and verify clean boot.

Record model (Old/New 3DS), firmware/homebrew environment, FPS, minimum free
linear memory and transition results. A GPU mock test cannot establish absence
of on-device GPU lifetime bugs or driver allocations.

## Vehicle/frontend regression build

The subsequent build starts on a recovered-asset Carbon main menu. Press A on
`OWN THE CITY` to load PocketGarage. Recheck the following on Old 3DS:

1. Lamp bloom has no cyan rectangular background and fades additively.
2. The initial car view is a low front three-quarter shot; full Circle Pad orbit
   remains usable without the former overhead default.
3. Every car shows four wheels. Check that wheels remain inside arches and do
   not float/sink; record IDs that need per-car overrides.
4. In the RIMS category, L/R cycles stock, BBS chrome and Enkei black. Stock
   texture must follow the selected vehicle ID.
5. BODY, HOOD, SPOILER, PAINT and RIMS choices remain attached to each car after
   switching away and back. BODY and BASE must remain paired.
6. During the placeholder race, front wheels steer and all wheels rotate.
7. The five recovered category icons render with alpha and selection highlight.
8. Repeat menu/garage/race transitions and compare 57 FPS and memory results
   with the accepted prior build.

The main-menu composition and text are native reconstructions from recovered
assets, not a claim of decoded APT layout. The wheel mesh/transforms are also
port-authored approximations, as authorized for this phase.

## Automated coverage

`make test` retains existing vehicle/physics/audio-file checks and adds:

- real garage through the production C scene parser;
- independent texture unswizzle/channel/alpha verification;
- byte-identical regeneration of scene and textures;
- 45 deliberately malformed headers/ranges/indices/materials/paths/budgets;
- 100 deterministic mutation cases under UBSan;
- 32 cycles with two simultaneous sections sharing textures;
- failures at each of 42 GPU allocation positions, with complete rollback;
- missing and truncated texture failure after earlier successful acquisitions.

Resource tests run the production ownership implementation with host GPU stubs.
The ARM11 executable and both PICA shaders are also compiled by the normal build.

## Remaining visual uncertainty

PNG alpha classification is provisional; additive light blooms, missing original
vertex alpha/cull state, and transparent intersections may differ from Zeebo.
The floor near the origin is around Y=0.05, while prototype car placement is
Y=0. Original placement/ground alignment still needs visual confirmation.
The existing orbit camera can enter geometry at extreme pitch; camera collision
is not implemented. No original garage transform is claimed.
