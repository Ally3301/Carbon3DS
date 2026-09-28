# STATUS: DIRECT-SAMPLING EXPERIMENT REJECTED ON HARDWARE

The semantic PAINT/_DETAILS relationship remains valid reverse-engineering
evidence. However, directly sampling DETAILS over material 1500 caused severe
visual regression. The native renderer no longer does this. See
`VEHICLE_NATIVE_RENDERER.md`.

# Vehicle PAINT + DETAILS rendering

Date: 2026-09-25

## Recovered semantic relationship

Targeted `jogo.c` analysis found that the RaceCarRenderInfo paint-material
constructor looks up two resources independently:

- `paint` from the shared/modable race-car library;
- `_details` from the car-specific texture library.

Across the normalized vehicle set, every one of the 43 vehicles has exactly one
decoded local texture whose recovered name contains `DETAILS`.

This explains why the local DETAILS texture can be valid yet absent from normal
numeric mesh-material references: it is a secondary input to material 1500
(PAINT), not a standalone geometry material.

## Runtime representation

`vehicle.cfg` now contains:

```text
paint_details <numeric-local-material-id>
```

The id references the already loaded local texture. No duplicate T3X is created.

`VehicleAsset` stores that semantic material id and exposes:

```c
vehicle_asset_paint_details_texture()
vehicle_asset_paint_details_uv()
vehicle_asset_paint_details_material()
```

## Citro3D reconstruction

For material 1500 the current fragment/TEV reconstruction is:

```text
stage 0 = DETAILS texture × vertex lighting
stage 1 = previous × selected paint colour
```

Conceptually:

```text
output ≈ DETAILS × lighting × PAINT
```

This is evidence-based as to the inputs, but it is **not yet claimed to be the
exact original EAGL BRDF/specular equation**.

All non-PAINT material paths reset TEV stage 1 to passthrough so the paint tint
cannot leak into headlights, badging, windows, carbon fibre or later batches.

## Why this matters for customization

Many hood/spoiler packets use material 1500. If their geometry is already being
submitted correctly, this change can make previously dark/invisible-looking
painted accessories visible without changing N3P geometry.

It does not solve genuine slot-selection, culling or resource-lifetime bugs.

## Validation

Host test:

```text
43/43 vehicles expose semantic PAINT + *_DETAILS binding
```

Hardware validation is still required.

Check several cars with different paint colours, especially:

- 240SX
- SUPRA
- MUSTANG67
- SKYLINE

Then test hoods/spoilers that use material 1500 separately from those using
material 1000 (CARBONFIBRE).

## Next material work

After PAINT hardware validation:

1. make WINDOW blend/depth/cull state explicit;
2. validate material 1000 CARBONFIBRE on hood/spoiler components;
3. diagnose any accessory still absent with slot/group logging;
4. implement separate headlight glow/effect pass.

## Wheels after vehicle materials

The procedural wheel geometry remains a port-authored placeholder. The recovered
wheel texture is not guaranteed to match its UV topology or proportions.

Do not tune it deeply yet.

The `jogo.c` analysis has identified a stronger path for four original wheel/hub
transforms through the derived ride-data table at `rideInfo + 0x124`. After
vehicle materials/customization are stable, recover these transforms and define
a wheel mesh/UV strategy intentionally rather than stretching recovered textures
onto the current placeholder cylinder.
