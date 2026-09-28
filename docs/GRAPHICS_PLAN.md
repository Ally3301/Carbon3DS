# PICA200 graphics pass

The current renderer uses custom PICA vertex shaders with per-vertex normals.
The first PSP-style pass keeps the fast forward renderer and assigns painted
body panels a separate, stronger specular material. This produces moving
highlights without a fullscreen pass or extra render target.

Available next steps, ordered by cost:

1. `C3D_LightEnv` plus a Phong LUT for a second material path.
2. A small VRAM render target for a low-resolution glow pass on headlights
   and taillights.
3. A cubemap/planar reflection approximation only after measuring VRAM and
   frame time on hardware.

Streaming review: road prefetch now updates every frame even while loading is
throttled. Asset replacement still synchronizes before release because scene
vertex buffers may be in the submitted GPU command list. Removing that fence
requires a deferred-free queue, not an unsafe direct free.
