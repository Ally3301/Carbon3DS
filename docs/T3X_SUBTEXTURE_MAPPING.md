# T3X subtexture mapping fix

Date: 2026-09-25

## Problem

The runtime imported `.t3x` files with `Tex3DS_TextureImportStdio()` and then
freed the `Tex3DS_Texture` metadata while retaining only `C3D_Tex`.

That discarded the first `Tex3DS_SubTexture`, including the UV bounds and the
90-degree rotation state used by tex3ds. UI quads and the vehicle shader then
sampled unmodified logical 0..1 UVs.

This affected the same loader path used by:

- garage category icons;
- boot/frontend textures;
- vehicle-local textures;
- shared vehicle materials (notably the very small WINDOW texture);
- recovered wheel textures.

## Implemented fix

`resource_load_tex()` now:

1. imports the backing `C3D_Tex`;
2. requires at least one subtexture;
3. reads subtexture 0 before freeing the Tex3DS metadata;
4. obtains TopLeft, TopRight and BottomLeft coordinates using the tex3ds helper
   functions;
5. stores an affine `TextureUV` transform.

The logical convention is:

```text
(0,0) top-left
(1,0) top-right
(0,1) bottom-left
```

The transform is:

```text
mapped = topLeft
       + source.u * (topRight - topLeft)
       + source.v * (bottomLeft - topLeft)
```

This handles both ordinary and tex3ds-rotated subtextures.

### UI

UI quads apply this transform on the CPU to all four quad corners.

### Vehicles and wheels

The vehicle PICA shader now receives:

```text
texUvOrigin
texUvU
texUvV
```

and applies the same affine transform to the original per-vertex UV before
sampling. Each material sets the transform belonging to its loaded T3X. The
wheel face uses the wheel texture's transform.

Raw N3S1 world textures are unchanged and continue using their direct scene UVs.

## Validation

Host tests cover:

- identity mapping;
- ordinary subtexture mapping;
- a 90-degree rotated affine case;
- existing 830 N3P validation;
- existing 229 wheel texture validation;
- existing global PAINT/WINDOW checks;
- scene validation.

Host tests pass.

A cross-build/on-device run was not performed in this environment because
devkitPro is unavailable here.

## Hardware acceptance

Validate these before calling the issue closed:

1. all five garage icons are visible and oriented correctly;
2. frontend background/logo are visible and correctly oriented;
3. one known vehicle-local texture (headlight/badging) is correctly aligned;
4. WINDOW no longer samples padded/incorrect backing pixels;
5. stock and aftermarket wheel faces show recognizable recovered imagery.

If the vehicle image is consistently vertically inverted while UI is correct,
do not edit PNGs. Revisit only the logical V convention at the vehicle shader
boundary and compare with a known vehicle texture.

## PICA200/picasso operand-order fix

The first hardware-toolchain build exposed a picasso operand restriction in the
vehicle UV affine transform. Floating-point uniforms are wide source operands.
`mad` supports narrow-wide-narrow or narrow-narrow-wide layouts.

The invalid sequence:

```asm
mad r3, texUvV, intex.yyyy, r3
```

was changed to the mathematically equivalent:

```asm
mad r3, intex.yyyy, texUvV, r3
```

so `texUvV` occupies the valid wide operand position. Host tests were already
passing; this change addresses shader assembly only.
