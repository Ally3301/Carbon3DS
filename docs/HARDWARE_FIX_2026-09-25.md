# Hardware vehicle fix — 2026-09-25

This patch follows the latest MAZDA3 Garage capture.

## Hood

The recovered MAZDA3 `HOOD_STYLE00_B` is a valid solid shell: 65 source
vertices, 63 triangles, material 1500, with bounds inside the BODY front end.
Its normals point upward and its source coordinates agree with the normalized
Zeebo model.  The remaining runtime hazard was reverse-Z equality: accessories
were drawn after BODY with `GPU_GREATER`.  Coplanar/equal-depth accessory
fragments therefore failed even though the hood was submitted.

BODY/BASE still use strict `GPU_GREATER`; HOOD/SPOILER/crew-tag shells now use
`GPU_GEQUAL` only while those later shells are drawn.  State is restored before
wheels/scene rendering.

## Wheels

The recovered 64x64 wheel images are RGBA cutouts with antialiased alpha.  The
old wheel face inherited the opaque vehicle `ONE/ZERO` blend state, so texels
with tiny non-zero alpha were written as fully opaque RGB.  That is a direct
cause of coloured/noisy pixels around spokes and the tire circle.

Wheel faces now use source-alpha blending plus a very small alpha rejection
threshold.  The procedural tread remains opaque and the blend state is restored
immediately after the face.

## Material scope

PAINT remains dynamic tint + vertex lighting.  The car-specific `*_DETAILS`
resource is intentionally not multiplied as RGB: decoded DETAILS images are
channel-packed/mask-like data (for MAZDA3 the dominant texel is not neutral),
and the previous literal multiply was already shown on hardware to corrupt the
car.  Reproducing the exact Zeebo paint composite requires decoding how the
original EAGL material consumes those channels; guessing it in the 3DS renderer
would regress the current image.
