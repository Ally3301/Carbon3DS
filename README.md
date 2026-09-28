# NFS Carbon Zeebo/Psp → Nintendo 3DS

Native reimplementation/reconstruction project for the Hybrid Zeebo/Psp version of
**Need for Speed Carbon**, targeting Nintendo 3DS homebrew.

## Start here

[`docs/ASSET_LAYOUT.md`](docs/ASSET_LAYOUT.md) - Assets layout 

[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) - Code Architecture

[`tools/README.md`](tools/README.md) - Manual Decoder Tools

## Decrypt Progress

The project is split into four layers:



```text
assets/source/       original/recovered source material used for RE
assets/normalized/   platform-neutral assets ready for implementation
assets/generated/    rebuildable 3DS-oriented intermediates
romfs/               current runtime subset packed into the .3dsx
```

Do not treat `assets/generated/` or `romfs/` as preservation sources.
The canonical implementation-facing asset handoff is `assets/normalized/`.
Raw unresolved data is under `assets/source/`.

**I recently discovered that PSP files follow the same pattern as Zeebo files, so it is possible to implement hybrid files in the runtime; however, I haven't yet created reliable tools to automate this for the PSP version, though it is good to keep it documented.**

**These assets aren't in the repository; you'll have to extract them using the Provided tools with the PSP and Zeebo ROMs of NFS Carbon.**

## Current state

Validated/recovered:

- 43 vehicle archives. (and 23 vehicles archives from Psp ver.)
- 830 independent vehicle geometry components. 
- 159 vehicle-local textures.
- 229 wheel/tire textures.
- PocketGarage original 3D scene. (Zeebo ver.)
- Palmont/open-world CDL geometry split into streamable sections. (Zeebo ver.)
- 40 race-specific R1 overlays. (Zeebo ver.)
- road network structure. (Zeebo ver.)
- WAV/OGG audio assets. (Zeebo ver.)
- thousands of UI/HUD PNG assets. (Zeebo and some Psp Assets.)
- corrected SHPM palette decoding. (works perfectly on Zeebo assets, Psp work in progress)
- decompiled `ZeeboSource.c` indexed for targeted reference.

Current 3DS runtime:

- boots on the 3DS/homebrew stack;
- Citro3D vehicle renderer; (with support for reflections based on PSP MIPS code)
- N3P component loader; 
- body/base/hood/spoiler selection scaffolding; (with support for reflections based on PSP MIPS code)
- dynamic paint material scaffolding; (with support for reflections based on PSP MIPS code)
- NDSP audio; (work in progress, some original audios doesn't work)
- camera/input/test physics; (work in progress.)
- Palmond City Stream render; (Runs at 30 - 45 fps, best implementation needed)

The runtime is intentionally behind the asset reverse engineering. 

## Build

**This repository doesn't have the assets, so you will need to convert the psp and Zeebo roms using the tools provided on [`tools/README.md`](tools/README.md)**

With devkitPro/devkitARM and the 3DS packages installed:

```sh
make rebuild-vehicle-assets
make test
make -j2
```

`make assets` prepares the current RomFS subset.

See [`docs/BUILD_AND_TEST.md`](docs/BUILD_AND_TEST.md).

## Important rule

The old **N3M** vehicle pipeline is obsolete. Do not restore it.

- Preservation/interchange: OBJ + PNG + JSON in `assets/normalized/`.
- Current vehicle runtime intermediate: N3P1 in `assets/generated/3ds/`.
- The execution of the original world still needs work, but the original Zeebo data is already excellent for a better implementation.
- Support for PSP version game data is partially implemented (only works well with psp cars and some simple textures, work in progress.)
- This version of the repository contains only the tools needed to build the .3dsx you will have to provide the assets yourself.
- For now, only Zeebo allows for easy extraction; PSP extraction is possible, but I haven't automated the process yet.

## Repository map

See [`docs/ASSET_LAYOUT.md`](docs/ASSET_LAYOUT.md) and
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
