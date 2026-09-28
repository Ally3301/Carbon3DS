# Roadmap

This is a suggested continuation order, not a requirement to implement every
item in one pass.

## Milestone A — stable asset/runtime contract

- define a native streamable world-section format;
- write an offline packer from normalized CDL output;
- include source hash + section name + material binding metadata;
- write a defensive loader and host tests.

Success criterion: PocketGarage renders from the new native scene format.

## Milestone B — original garage

- render `l3rl_3401` in Citro3D;
- load only the selected vehicle parts;
- reproduce basic opaque/transparent material passes;
- keep current working audio.

Success criterion: original garage + correct selected car on real 3DS.

## Milestone C — Palmont streaming

- use section bounds;
- define nearby/visible-section activation;
- implement memory budget and LRU/unload policy;
- keep textures cached separately from meshes;
- add debug overlay for loaded sections / memory / draw calls.

Success criterion: drive/fly through Palmont without loading the entire city.

## Milestone D — first real race

Use race 4001 as the fixed integration target.

Compose:

```text
opwd_3000
+ race_4001/R1
+ road network
+ decoded route/checkpoint data
```

Success criterion: start, drive route, ordered progress, finish.

## Milestone E — collision and road surface

- decode or derive simplified road/collision representation;
- separate visual mesh from collision;
- ground query + wall/barrier collision;
- reset/respawn.

## Milestone F — vehicles

- fix body-kit selection anomaly;
- fix spoiler visibility;
- recover wheel visual geometry/hubs;
- implement dynamic paint correctly;
- refine transparent/window pass;
- implement wheel rotation/steer/suspension transforms.

## Milestone G — original game systems

After one race is stable:

- opponents/AI;
- nitrous/speedbreaker fidelity;
- traffic;
- police;
- Canyon/Pursuit special rules;
- career;
- crew;
- save;
- frontend/APT behavior.

## Performance guidance

Target Old 3DS before optimizing New 3DS.

Track at minimum:

- linear memory usage;
- texture memory;
- active world sections;
- vertices/triangles submitted;
- draw calls;
- frame time;
- section load latency.

Prefer offline work over per-frame parsing.

## Current gate — PocketGarage

A/B implementation is available via N3S1, with host tests and 3DS cross-build.
Complete the hardware acceptance checklist in `POCKETGARAGE_TEST.md` before
marking A/B complete or starting Palmont. In particular, confirm textures,
placement, orbit, audio preservation and repeated unload/reload on Old 3DS.
