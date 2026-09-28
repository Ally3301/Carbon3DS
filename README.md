# NFS Carbon Zeebo → Nintendo 3DS

Native reimplementation/reconstruction project for the Zeebo version of
**Need for Speed Carbon**, targeting Nintendo 3DS homebrew.

This repository has been reorganized as a clean handoff for continued work with
ChatGPT Codex / GPT-6 Astra Codex.

## Start here

**Read [`CODEX_START_HERE.md`](CODEX_START_HERE.md) before changing code.**

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

## Current state

Validated/recovered:

- 43 vehicle archives.
- 830 independent vehicle geometry components.
- 159 vehicle-local textures.
- 229 wheel/tire textures.
- PocketGarage original 3D scene.
- Palmont/open-world CDL geometry split into streamable sections.
- 40 race-specific R1 overlays.
- road network structure.
- WAV/OGG audio assets.
- thousands of UI/HUD PNG assets.
- corrected SHPM palette decoding.
- decompiled `jogo.c` indexed for targeted reference.

Current 3DS runtime:

- boots on the 3DS/homebrew stack;
- Citro3D vehicle renderer;
- N3P component loader;
- body/base/hood/spoiler selection scaffolding;
- dynamic paint material scaffolding;
- NDSP audio;
- camera/input/test physics;
- **still uses a procedural test road instead of the recovered city**.

The runtime is intentionally behind the asset reverse engineering. The next
developer should consume the normalized assets instead of redoing the extraction.

## Build

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
- World runtime format is deliberately not finalized yet.

## Repository map

See [`docs/ASSET_LAYOUT.md`](docs/ASSET_LAYOUT.md) and
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
