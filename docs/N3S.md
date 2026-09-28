# N3S1 native scene section (version 1)

Implemented for PocketGarage on 2026-09-25. This is a new native contract, not a
Zeebo format. Vehicle N3P1 is unchanged. The experimental decoder N3T output is
not used by this loader.

All integers and IEEE-754 binary32 values are little-endian. Files have exactly
the declared size. No relocation, runtime OBJ/CDL/SHPM parsing, or native struct
casts are used. Unknown versions/flags are rejected. Strings are nonempty ASCII,
NUL-terminated and zero-padded. References are relative paths with no traversal,
absolute paths, backslashes or empty components.

## Header: 256 bytes

- 0: four bytes `N3S1`.
- 4: u32 version = 1.
- 8: u32 exact file size.
- 12: u32 flags = 0.
- 16, 20, 24, 28: u32 vertex, index, batch, material counts.
- 32, 36, 40, 44: u32 absolute offsets of those four arrays.
- 48: six f32 values: minimum XYZ, maximum XYZ, in runtime coordinates.
- 72: 64-byte section identity (`l3rl_3401/A1`).
- 136: 88-byte source identity (`tracks/l3rl_3401.viv:A1.cdl+A1.msh`).
- 224: 32-byte SHA-256 of normalized inputs, **not** of the original archive.

The hash input is the exact OBJ bytes, MTL bytes, manifest bytes, then exact PNG
bytes in first-used material order. `garage.json` records each PNG hash and its
normalized relative path. Source archive identity comes from the validated
handoff; this packer does not reopen or re-decode that archive.

Arrays are tightly concatenated: header, vertices, indices, batches, materials.
The loader requires canonical offsets, rejecting overlap, gaps and trailing data.
A batch array can begin at a non-four-byte-aligned offset; fields are decoded
bytewise. GPU vertex/index buffers are allocated separately in linear memory.

## Vertex and index arrays

Each vertex is 36 bytes: position XYZ, UV, RGBA, all f32. Each index is u16;
triangles are consecutive triples. Vertex identity includes the OBJ position and
UV indices so seams are preserved.

Offline coordinate conversion: `(x,y,z) -> (x,z,-y)` (Z-up to Y-up), without
recentering or scaling. UVs stay as in the visually validated OBJ. RGB comes
from normalized OBJ vertex colors, which already include CDL ambient modulation.
Alpha is 1 because this OBJ does not retain per-vertex alpha. The world shader
passes vertex color through, avoiding a second application of vehicle lighting.

## Batch array

Each 16-byte record holds four u32 values: first index, index count, local
material index, reserved zero. Batches must cover the entire index buffer once,
in order, with nonzero triangle-aligned counts. Centers for transparent sorting
are derived at load time and are not authoritative gameplay/collision data.

## Material array and shared resources

Each 192-byte record contains:

- 0: 64-byte stable namespaced material key (e.g. `l3rl_3401/A1_29`).
- 64: 112-byte external texture path relative to `romfs:/world/`.
- 176: u32 alpha class: 0 opaque, 1 cutout, 2 blended, 3 additive.
- 180, 184: u32 texture width and height.
- 188: u32 reserved zero.

Local material indices are not global identities. Sections may reuse the same
material key and texture reference; each section contains its own binding table.
Material keys must be unique inside a section. A reference-counted GPU cache is
keyed by external texture path and verifies matching dimensions. It supports
sharing between loaded sections; Palmont streaming is not implemented here.

Texture filenames are SHA-256 of conversion recipe identifier plus source PNG.
Payload is headerless PICA tiled RGBA8, exactly `width * height * 4` bytes. The
scene supplies validated dimensions before GPU allocation; no compressed texture
header can request unbounded memory. The packer uses `tex3ds -r -f rgba -z none`,
verifies the uncompressed envelope, and removes its four-byte compression header.
Dimensions smaller than 64 are enlarged to 64 with nearest-neighbor filtering,
so every texture fills its GPU surface and repeat UVs do not sample padding.
No normalized image is modified. GPU wrap is repeat and filtering is linear.

The host test independently unswizzles every texel and checks it against the
normalized PNG after the declared resize. Fully transparent RGB may be zeroed
by tex3ds; alpha and all visible RGBA are checked. Texture storage follows
[tex3ds](https://github.com/devkitPro/tex3ds); the raw payload has no subtexture
atlas metadata, trimming, rotation or border.

Alpha classification in this packer is **inferred from PNG alpha**, not recovered
EAGL state: all 255 -> opaque, only 0/255 -> cutout, otherwise blended. The
validated PocketGarage light bloom is an explicit exception: its red channel is
copied to runtime alpha and class 3 uses additive blending, fixing opaque cyan
lamp planes observed on hardware. Cutout threshold is 127. Blended batches
sort back-to-front by center in view direction and disable depth writes. World
geometry is two-sided because normalized MTL lacks original cull state.
Additive bloom, per-vertex alpha, exact blend factors and triangle-level sorting
remain unresolved. Car windows are drawn after scene transparency, not jointly
sorted with it.

## Defensive limits and ownership

- Maximum file: 4 MiB.
- Vertices: 1..65535; indices: 1..196608, divisible by three.
- Batches: 1..1024; materials: 1..256.
- Texture dimensions: powers of two, 8..1024.
- Global scene texture cache: 256 entries and 8 MiB maximum.
- Nonfinite positions/UV/colors/bounds, out-of-bounds vertices, invalid colors,
  invalid indices/materials, inconsistent shared dimensions and bad ranges fail.

`scene_decode`/`scene_load` return a complete owned section or NULL. Temporary
file data is freed after decode. `scene_asset_load` acquires textures into a
private candidate and publishes only after all loads succeed; failure releases
every prior acquisition. An output asset must initially be empty. Callers must
synchronize pending GPU frames before freeing a published asset.

PocketGarage uses 158,214 file bytes, 3,817 vertices, 5,865 indices, 47 batches,
42 materials and 1,507,328 texture bytes. Metadata allocations add a small amount
to geometry bytes shown on device. These budgets exclude vehicles/audio and are
not a claim that total Old 3DS memory usage has been measured.
