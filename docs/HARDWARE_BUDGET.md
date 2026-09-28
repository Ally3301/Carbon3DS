# 3DS graphics budget

## Hardware baseline

The original 3DS has 6 MiB of VRAM in the SoC and 128 MiB FCRAM. VRAM is the
right home for render targets and framebuffer-adjacent resources; linear FCRAM
is appropriate for streamed geometry, CPU-side assets and command buffers.

## Current runtime measurement

The race console now prints `linearSpaceFree()` and `vramSpaceFree()` every
frame. Hardware measurement is authoritative because display buffers, the
loader and system configuration vary by model and launch environment.

## Current top target

The 400x240 RGBA8 colour target consumes about 375 KiB. Its D24S8 depth buffer
consumes another ~375 KiB: roughly 750 KiB before render-target bookkeeping.
The project keeps world textures in linear memory through `C3D_TexInit`, so the
VRAM reading determines whether a small post-effect target is safe.

## Recommended order

1. Keep the current forward PICA shader for paint specular.
2. If at least 1.5 MiB remains free during dense driving, add a 200x120 RGBA8
   glow target plus depth only when needed; this costs about 94 KiB colour, or
   ~188 KiB with D24S8.
3. Composite only emissive masks (headlights, taillights, neon), not the full
   scene.
4. Do not add planar or cubemap reflections until measured GPU time permits it.

A full-resolution secondary target would cost 375 KiB colour before depth and
transfer overhead, so the half-resolution path has the better PSP-style return
on this hardware.
