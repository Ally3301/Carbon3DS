# Known issues 
## Runtime / vehicles

### Body-kit selection anomaly (fixed with PSP geometry cars)

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

**and no, delete the asset doesn't fix this problem on zeebo car assets.**

### Vehicle colours can look uniformly red (on Legacy renderer.)

Material 1500 is dynamic PAINT. Flat red source data is expected. The renderer
must apply selected paint parameters.

**That problem was fixed manually adjusting renderer.c**

### Wheels absent

Zeebo Wheel texture archive is understood but visual mesh/hub path is not.

**Update: However, the wheel geometry from the PSP version is available**

## World

### CDL display-list types 2/3/5

Some world sections contain less-common list types. They are preserved but not
fully semantically decoded. 

**Update: 80% of the world sections was sucessfully decoded, but some textures and categories are unknown**

## Gameplay



### Physics is a placeholder

Current arcade vehicle behavior exists only to exercise the runtime.

**probably will be replaced to the original physics from ZeeboSource.c**


### APT/CONST not reconstructed

UI PNGs are available, but original frontend layout/logic is not implemented.

**probably will be replaced to the original Frontend from ZeeboSource.c**


### Procedural wheel approximation

The port now draws wheels, but the mesh and hub transforms are authored for the
port because original geometry/transforms remain unresolved. Radius, wheelbase
and track are conservatively derived from each selected body/base/hood bounds.
Stock and custom face textures are recovered originals. Validate clipping and
axle position across all 43 cars, especially SUVs, vans and police variants.

**This was removed from the main branch, I extracted the original wheel psp geometry successfully**

### Frontend fidelity

Main menu and garage HUD use recovered Carbon/background/category imagery.
Labels and layout are native reconstructions because APT/CONST positioning,
animation and localized text programs are not decoded. Safehouse multi-car
lineups, original stat values, animated paint/rim transitions and full menu
hierarchy remain future work.



### Vehicle window and headlight presentation

Window groups use a second draw pass but their blend/cull state is not fully
explicit. Hardware shows black/incorrect glass. The original binary loads
`HeadlightFlareInner`, `HeadlightFlareOuter` and `HeadlightGlow` as separate
effect resources, so luminous headlights need an additional effect pass.

**that was fixed by adding MIPS PSP reflection system to the renderer.c**


### T3X subtexture fix implemented, hardware recheck pending

The runtime now copies tex3ds subtexture mapping before freeing the container and
uses it in UI, vehicle and procedural-wheel sampling. Host affine tests include
a rotated subtexture case. The previous loader bug is therefore fixed in code
but remains open until hardware confirms the visible symptoms are gone.


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



## Hardware validation required after colour/opaque-pass fix

The previous captures were produced while PAINT and multiple fallback
materials had incorrectly packed PICA colour constants, and the opaque vehicle
pass was still alpha blended. This can visually place accessories 'inside' a
semi-transparent body. Re-evaluate hood/spoiler placement only after testing the
corrected build, including Garage X slot-debug mode.
