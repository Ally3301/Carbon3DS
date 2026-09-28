# Programming guidance

## Preserve offline/runtime boundaries

Original Zeebo formats are optimized for the original EAGL/BREW environment,
not for PICA200. Decode once on the PC.

A strong runtime file should be:

- versioned;
- bounds-checked;
- little-endian;
- directly uploadable/copyable where practical;
- traceable to source;
- independent of Python tooling at runtime.

## World format suggestion

A future native world section can contain:

```c
struct SceneSectionHeader {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t batch_count;
    uint32_t material_count;
    float bbox_min[3];
    float bbox_max[3];
};
```

Keep external texture/material references separate from geometry so texture
caching can span multiple sections.

Do not commit this exact layout until the remaining CDL sharing semantics are
understood.

## Streaming suggestion

Start simple:

1. load section metadata/bounds only;
2. activate sections in a radius/frustum;
3. use a hard memory budget;
4. asynchronously/preemptively load later only if needed;
5. unload least-recently-used inactive sections.

Correctness first; sophisticated visibility data can be added after the basic
streamer works.

## Vehicle assembly

Model selection should use semantic/original index, not array position.

Recommended state:

```c
typedef struct {
    int bodykit_index;
    int hood_index;
    int spoiler_index;
    uint32_t paint_rgba;
    int wheel_style;
} VehicleCustomization;
```

When changing car:

- destroy all current loaded part resources;
- load new vehicle definition;
- clamp/reset selections against that vehicle's manifest;
- pair body/base by original upgrade index;
- never carry slot-array indexes from the prior car.

This is the first place to inspect for the reported cross-car body-kit bug.

## Material pipeline

Represent material semantics explicitly:

```text
sampled texture
dynamic paint
window/transparent
carbon fibre/global
runtime decal
unresolved EAGL state
```

Do not encode semantics in magic fallback colors in the long term.

## Debug facilities worth adding

On hardware:

- FPS/frame time;
- linear memory usage;
- active section count;
- texture cache count/bytes;
- selected car + selected part source names;
- world section labels;
- collision wireframe;
- race route/checkpoints.

Reverse-engineering projects become much faster when source identity can be
displayed on-device.
