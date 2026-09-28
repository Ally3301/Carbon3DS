# Suggested prompt for the next Codex session

You are continuing the NFS Carbon Zeebo -> Nintendo 3DS native reimplementation.

Before modifying code, read:
- CODEX_START_HERE.md
- docs/PROJECT_STATUS.md
- docs/ASSET_LAYOUT.md
- docs/REVERSE_ENGINEERING_KNOWLEDGE.md
- docs/KNOWN_ISSUES.md
- docs/ROADMAP.md

Important constraints:
- assets/normalized is the implementation-facing asset source.
- assets/source is the binary source-of-truth for unresolved RE.
- N3M is obsolete and must not be restored.
- Do not parse Zeebo ELF/SHPM/CDL on the 3DS when an offline conversion can do it.
- Keep Palmont sectioned for streaming.
- A race is Palmont + race R1 overlay + route/gameplay data.
- material 1500 is dynamic PAINT, not a missing diffuse image.
- wheels.viv contains textures, not the recovered visual wheel mesh.
- jogo.c is decompiler output; search targeted regions only.
- preserve unknown records rather than guessing.

First task recommendation:
Design and implement a versioned native scene-section packer/loader using the
validated PocketGarage as the smallest end-to-end test. Do not yet load all
Palmont. Add host validation and source identity metadata.
