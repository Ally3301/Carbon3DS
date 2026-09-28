# Game systems notes

This file collects programming-oriented knowledge useful when mapping original
behavior into new native systems. It intentionally separates evidence from
prototype implementation.

## File/archive layer

The original game uses EA-style archive/resource systems. Legacy targeted
analysis identified archive/buffer-related code around:

- `FUN_00004b1c` — legacy index around line 21249
- `FUN_0002b21c` — legacy index around line 49054

For the port, keep BIG/RefPack/archive handling offline unless runtime streaming
strictly requires a native container.

## Input

Legacy source analysis found joystick/backend references around:

- `FUN_00006040`
- `FUN_00006294`

The 3DS backend should expose stable abstract actions instead of leaking HID
keys into gameplay systems.

Suggested abstraction:

```c
typedef struct {
    float steer;
    float throttle;
    float brake;
    bool nitro;
    bool look_back;
    bool pause_pressed;
} DriveInput;
```

This makes it possible to tune Zeebo-like control behavior independently from
3DS input hardware.

## Audio

Original decompilation contains `Track%d.caf` naming evidence and other original
audio logic, but the current port sensibly uses decoded WAV/OGG and NDSP.

User validation indicates the current audio path is functioning with DSP
firmware. Preserve it while other systems are unstable.

## Frontend

`NFS_GoToScreen` references show the original frontend is screen/state driven.
APT/CONST likely own much of the original visual/front-end program data.

Recommendation:

- do not block gameplay reconstruction on full APT emulation;
- preserve UI assets and screen identities;
- first implement native screen/state equivalents;
- revisit APT/CONST for layout/animation fidelity later.

## PocketGarage

The original game explicitly references `tracks\l3rl_3401.viv` near the
PocketGarage camera/group creation flow.

This is a useful architecture clue: garage is a scene/presentation context, not
part of the open-world stream.

Treat it as a separate scene with:

- scene geometry/materials;
- selected car instance;
- orbit camera;
- customization state;
- UI overlay.

## Vehicle compiled geometry

Decompiler strings around `CompiledModel.cpp`, `ZeeboGeometry`, and the recovered
ELF/MIPS objects all agree on the compiled geometry path.

The normalized vehicle model should therefore retain:

```text
part identity
renderer packet class
material ID
UV/normal data
source component identity
```

Do not collapse all packets to one undifferentiated material.

## Paint/customization

`Career.GetCarPaint` and `Career.UpdateSelectedCarPaint` show paint belongs to
game state, not just static material data.

A future customization state should be serializable and independent from loaded
GPU resources.

## Wheels

The original code constructs tire texture names such as `%s_TIRE_STYLE00` and
loads shared wheel texture libraries.

Open question: visual wheel geometry/hub transforms.

Likely data sources to correlate:

- vehicle compiled/model state;
- per-car configuration data;
- wheel renderer path;
- collision/geometry metadata;
- shared wheel material libraries.

Do not assume `wheels.viv` itself contains the mesh; extraction disproved that.

## Track streaming

The original game has a `TrackStreamer` concept and streamable sections.

The recovered city validates this architecture: Palmont is naturally sectioned.

Native implementation should retain:

- section ID/name;
- bounds;
- dependencies/shared materials;
- loaded/unloaded state;
- source/race ownership.

## Race composition

Use:

```text
world scene
+ race overlay
+ route/checkpoint data
+ dynamic actors
```

This is a more faithful model than treating every race as its own full map.

## Collision and physics

`Collision.cpp` appears in the decompilation, but the original collision
implementation is not reconstructed.

Avoid coupling car physics directly to render triangles.

Recommended split:

- `RoadSurfaceQuery`
- `StaticCollisionWorld`
- `VehicleDynamics`
- `RaceProgress`

This allows replacing approximation layers independently as more original data
is understood.

## Camera

`DriveCameraMover` references suggest dedicated camera controllers in the
original engine.

Keep camera logic in stateful controller modules instead of hardcoding offsets
inside renderer code.

## Save/career

Not yet reconstructed. When implemented, save **semantic choices**:

- vehicle ID;
- bodykit original index;
- hood original index;
- spoiler original index;
- paint value;
- wheel style;
- progression/unlocks.

Do not save array positions from generated manifests because ordering can change.
