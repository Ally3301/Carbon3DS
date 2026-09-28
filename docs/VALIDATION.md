# Validation

Status: **PASS**

- Python tools syntax: 11/11
- Normalized vehicles: 43
- Normalized vehicle parts referenced: 830
- Normalized vehicle-local textures: 159
- Vehicle manifest reference errors: 0
- Races with R1.cdl: 40
- PocketGarage OBJ present: True
- Host test exit code: 0

Host test output:
```text
physics and ordered checkpoint tests passed
C loader accepted 830 real N3P vehicle components
Malformed N3P files and unsafe archive paths rejected; UBSan clean
Global materials, dynamic PAINT, WINDOW and 229 wheel textures validated
```

A 3DS cross-build was not run in this environment because devkitPro is unavailable.

## PocketGarage implementation validation — 2026-09-25

Host suite: PASS (`make test`). Existing 830 N3P checks, physics/checkpoints,
global materials and wheel textures still pass. N3S1 adds deterministic real
garage packing, per-pixel texture roundtrip, 45 invalid inputs, 100 mutation
cases, 32 shared-resource lifecycle cycles, 42 simulated allocation failures
and missing/truncated texture rollback. Production C parser/ownership code is
used; GPU allocation is mocked for lifecycle tests. UBSan reported no errors.

Cross-build: PASS (`make -j2`), including ARM11 code, both PICA shaders and
RomFS staging. Output is `nfs3ds.3dsx`. This supersedes the historical statement
above that devkitPro was unavailable. Existing enum, toolchain/generated-assembly
and future-file timestamp warnings remain.

Hardware validation: PENDING. No 3DS/emulator visual run was performed here.
Textures on GPU, placement, extreme camera angles, blended materials, FPS,
audio regression and GPU lifetime must pass `POCKETGARAGE_TEST.md` before
closing the milestone. No normalized assets or audio source were edited.

## Hardware report and subsequent validation

User hardware report: PocketGarage otherwise rendered correctly, placeholder
gameplay held 57 FPS, audio/functionality worked and no memory leak was observed.
Lamp bloom cyan blocks and incomplete vehicle presentation were reported.

The subsequent host/build validation covers the unchanged 830 N3P components,
all 229 wheel textures, per-vehicle stock texture availability/fallback, five
recovered 64x64 customization icons, corrected bloom texture roundtrip and the
existing malformed N3S/lifetime cases. ARM11 links Citro2D/Citro3D and both PICA
shaders. On-device validation of the new bloom, wheel transforms/animation,
camera and frontend composition remains pending.

## Second hardware report

The bloom correction is visually successful. Garage scene, placeholder track,
audio, stable 57 FPS and no observed leak remain confirmed. The new frontend,
garage icons, wheel faces and vehicle materials did not pass: screenshots show
missing frontend imagery/text, a white icon, incorrect wheel textures, black or
incorrect windows, missing headlight glow and unreliable hood/spoiler display.

The host suite still passes after recording these findings. This does not clear
the GPU rendering failures. The leading next test is preservation of tex3ds
subtexture coordinates/rotation through the runtime texture loader, documented
in `legacy_notes/HANDOFF_CAR_RENDERING_FRONTEND_2026-09-25.md`.


## T3X mapping patch validation

Host validation after the T3X mapping change:

```text
physics and ordered checkpoint tests passed
C loader accepted 830 real N3P vehicle components
Malformed N3P files and unsafe archive paths rejected; UBSan clean
T3X logical/subtexture UV affine mapping tests passed
Global materials, dynamic PAINT, WINDOW, wheel textures and garage icons validated
```

`tools/test_scene.py --validate-only` also passes with pixel-exact normalized
PocketGarage texture validation.

This environment does not have devkitPro, so the modified PICA shader and ARM11
runtime still require a cross-build/on-device validation.


### Environment limitation for this patch

`make test` cannot complete in this analysis environment because the scene test
regeneration calls `/opt/devkitpro/tools/bin/tex3ds`, which is not installed
here. The portable host suite passes, and `tools/test_scene.py --validate-only`
passes against the already-generated N3S1 assets. A devkitPro machine must run
the full `make test && make -j2`.


## Shader assembly follow-up

The first external devkitPro build reached `picasso` and reported:

```text
src/vshader.v.pica:41: error: invalid source2 register: texUvV
```

The UV affine `mad` was reordered to place the uniform in the valid wide-source
slot. Re-run `make clean && make -j2` to validate this fix with the external
devkitPro toolchain.

## Native vehicle renderer host validation

Portable material-class policy tests pass. Existing N3P, malformed input,
T3X UV, global material and wheel asset tests remain green.

`tools/diagnose_vehicle_host.py` reports 830 component comparisons and zero
geometry/UV failures. The current environment still cannot run the external
devkitPro/PICA cross-build, so native GPU appearance remains hardware-pending.

The PocketGarage scene path was explicitly preserved: `draw_scene()` and
`src/scene_shader.v.pica` match the returned pre-rewrite project exactly.

## Vehicle expanded-stream and UV-policy validation

The host loader now verifies every expanded GPU vertex equals the indexed
source vertex selected by its N3P index. All 830 real components pass.
Portable material/UV policy tests also pass. `test_scene.py --validate-only`
continues to accept the real PocketGarage with matching unswizzled texture
pixels.

## C source audit regression coverage

Host tests now include the PICA/C2D RGBA packing contract and explicitly
detect the old `0xB74332FF` mistake. Existing 830-component N3P validation,
expanded-stream validation, texture UV mapping and PocketGarage N3S validation
remain green.
