# Vehicle texture pipeline fix — 2026-09-25

Hardware result before this patch: the single-stream compositor fixed BODY/HOOD/SPOILER geometry, but changing customization could expose pixel/static-like fringes; lamps and badges also showed persistent edge corruption.

Changes in this revision:

* Keep the validated single-stream vehicle compositor unchanged.
* Explicitly set sampler wrap for every vehicle texture. CARBONFIBRE (1000) repeats; vehicle-local textures and other global materials clamp to edge. This prevents linear filtering from wrapping border samples to the opposite side of a lamp/badge texture.
* Treat `NFSCar_TextureShiny` local RGBA textures as alpha-composited surface decals while retaining depth writes. The recovered Zeebo taillight/badging assets use low alpha extensively; the old opaque `ONE/ZERO` blend made their RGB visible at full strength and produced coloured/noisy blocks.
* Reject only near-zero alpha (`> 4`) to suppress quantization fringe without destroying authored RGBA4444 coverage.
* Restore opaque alpha/blend/depth state after the vehicle pass so state cannot leak into wheels/garage rendering.

Validation:

* `tools/test_host.py`: PASS. 830 real N3P components accepted; malformed data rejected; UV/subtexture mapping tests pass; global material/PAINT/WINDOW/wheel validation passes; 43/43 vehicles retain PAINT/DETAILS metadata.
* `tools/test_scene.py`: cannot execute in this environment because `/opt/devkitpro/tools/bin/tex3ds` is absent. This is an environment limitation, not reported as a pass.

The PAINT `*_DETAILS` map is intentionally still not sampled directly. It contains Zeebo material-mask semantics rather than a conventional diffuse map; applying it as ordinary RGB previously caused a hardware regression. A later material reconstruction should decode those channels before adding specular/carbon/plastic response.
