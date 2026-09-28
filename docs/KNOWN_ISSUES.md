# Known issues

## Runtime / vehicles

### Body-kit selection anomaly

Observed: one selectable body-kit option can display geometry that appears to
belong to an unrelated car.

Evidence from normalized extraction:

- all 830 vehicle models are namespaced to their own source vehicle;
- standalone OBJs tested across multiple cars look correct;
- body/base pieces physically fit.

Likely investigation area:

- slot-option indexing;
- stale pointer/lifetime after switching car;
- selected index not reset when vehicle changes;
- body/base option arrays using position instead of original upgrade index.

Do not "fix" this by deleting an asset.

### Spoilers may not appear

Standalone spoiler geometry is valid. Check:

- selected slot state;
- N3P load failure;
- draw pass filtering;
- bounds/culling;
- depth/cull state;
- current car switch resetting selected spoiler.

### Vehicle colours can look uniformly red

Material 1500 is dynamic PAINT. Flat red source data is expected. The renderer
must apply selected paint parameters.

### Wheels absent

Wheel texture archive is understood; visual mesh/hub path is not.

## World

### Palmont textures not automatically bind 1:1 in Blender

PNG extraction appears valid. Remaining work is mostly shared material/resource
binding across CDL sections.

Do not interpret the Blender material-link script as authoritative engine logic.

### CDL display-list types 2/3/5

Some world sections contain less-common list types. They are preserved but not
fully semantically decoded.

### Race R1 looks incomplete alone

Expected. R1 is an overlay, not the full city.

## Gameplay

### `src/track.c` is temporary

It implements a circular procedural test track. Do not tune it toward fidelity;
replace it with real world/route systems.

### Physics is temporary

Current arcade vehicle behavior exists only to exercise the runtime.

### APT/CONST not reconstructed

UI PNGs are available, but original frontend layout/logic is not implemented.

## Build/environment

A 3DS cross-build was not produced in the reverse-engineering environment
because devkitPro was unavailable there. Host asset tests were used instead.

## PocketGarage integration (2026-09-25)

N3S1 host validation and ARM/PICA compilation are available. Real 3DS appearance,
audio regression, FPS and repeated transitions still need hardware acceptance.
Material alpha is inferred from PNGs: original additive blending, vertex alpha
and cull state are unavailable in the normalized OBJ/MTL. Transparent scene
batches are center-sorted; car windows are not jointly sorted with them.
Prototype car/camera placement remains; extreme orbit pitch can clip scenery.
See [POCKETGARAGE_TEST.md](POCKETGARAGE_TEST.md).

### PocketGarage light bloom

Hardware showed cyan rectangular planes around lamps. The recovered
`0013_safehouse_lightbloom.png` stores near-opaque alpha but encodes coverage in
red: the cyan background has red zero and the white center has increasing red.
The packer now derives runtime alpha from red and tags this material additive.
Normalized/source PNGs are unchanged. Hardware confirmation is pending.

### Procedural wheel approximation

The port now draws wheels, but the mesh and hub transforms are authored for the
port because original geometry/transforms remain unresolved. Radius, wheelbase
and track are conservatively derived from each selected body/base/hood bounds.
Stock and custom face textures are recovered originals. Validate clipping and
axle position across all 43 cars, especially SUVs, vans and police variants.

### Frontend fidelity

Main menu and garage HUD use recovered Carbon/background/category imagery.
Labels and layout are native reconstructions because APT/CONST positioning,
animation and localized text programs are not decoded. Safehouse multi-car
lineups, original stat values, animated paint/rim transitions and full menu
hierarchy remain future work.

### T3X subtexture metadata is discarded

Hardware showed white/incorrect garage icons, incorrect procedural wheel faces
and a mostly blank initial frontend. `resource_load_tex()` currently frees the
tex3ds container without retaining its `Tex3DS_SubTexture` coordinates or
rotation, while UI and vehicle shaders sample unmodified 0..1 UVs. This is the
leading shared root cause and must be fixed in the runtime loader/shaders rather
than by editing PNG assets. See
`legacy_notes/HANDOFF_CAR_RENDERING_FRONTEND_2026-09-25.md`.

### Vehicle window and headlight presentation

Window groups use a second draw pass but their blend/cull state is not fully
explicit. Hardware shows black/incorrect glass. The original binary loads
`HeadlightFlareInner`, `HeadlightFlareOuter` and `HeadlightGlow` as separate
effect resources, so luminous headlights need an additional effect pass.

The current environment does have devkitPro, superseding the earlier handoff
limitation. Build emits existing vehicle enum warnings, generated assembly/linker
warnings and clock-skew warnings for future-dated input files. No vehicle/audio
source was changed to suppress these unrelated warnings.


### T3X subtexture fix implemented, hardware recheck pending

The runtime now copies tex3ds subtexture mapping before freeing the container and
uses it in UI, vehicle and procedural-wheel sampling. Host affine tests include
a rotated subtexture case. The previous loader bug is therefore fixed in code
but remains open until hardware confirms the visible symptoms are gone.

Do not edit decoded PNGs to compensate if one category still fails; isolate the
remaining convention/state bug first.

## PAINT + DETAILS hardware validation pending

The old flat-tint-only PAINT path has been replaced with a two-input
PAINT+DETAILS path backed by recovered per-car DETAILS textures. This should
improve body, painted hood and painted spoiler presentation. The exact original
EAGL material equation is still not fully decoded, so visual fidelity must be
checked on hardware before considering PAINT solved.

## Vehicle geometry converter cleared by host comparison

All 830 N3P components match normalized OBJ geometry within floating-point
noise, including UVs. Missing/deformed HOOD/SPOILER on hardware should no
longer be attributed to OBJ/N3P conversion without new evidence.

The garage HUD now reports loaded hood/spoiler vertex/triangle/group counts to
separate selection/loading bugs from GPU presentation bugs.

## Material 9999 is context dependent

Host analysis shows `PURSUIT*2` vehicle bodies use material 9999 over major
geometry while shipping separate local BADGING/DETAILS/HEADLIGHT/TAILLIGHT
textures. The previous global interpretation of 9999 as crew-tag/decal is
invalid. The native renderer uses a neutral fallback until the CopCarRenderInfo
material lookup is decoded.

## Customization presentation after DrawArrays rewrite

HOOD/SPOILER source and N3P coordinates are coherent and loaded counts match
the selected assets. Vehicle drawing now bypasses the separate GPU index-buffer
path. If accessories remain misplaced on hardware, the next investigation is
model/uniform submission rather than extraction or N3P conversion.

## Hardware validation required after colour/opaque-pass fix

The previous captures were produced while PAINT and multiple fallback
materials had incorrectly packed PICA colour constants, and the opaque vehicle
pass was still alpha blended. This can visually place accessories 'inside' a
semi-transparent body. Re-evaluate hood/spoiler placement only after testing the
corrected build, including Garage X slot-debug mode.
