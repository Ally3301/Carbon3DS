#pragma once

#include <stdint.h>

/*
 * PICA texture-env/fragment colour registers use the same byte packing exposed
 * by C2D_Color32(): R in bits 0..7, G 8..15, B 16..23, A 24..31.
 *
 * Do not confuse this with APIs such as C3D_RenderTargetClear, whose example
 * constants are traditionally written as 0xRRGGBBAA.
 */
static inline uint32_t gpu_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)r
         | ((uint32_t)g << 8)
         | ((uint32_t)b << 16)
         | ((uint32_t)a << 24);
}

static inline uint8_t gpu_rgba8_r(uint32_t c) { return (uint8_t)(c); }
static inline uint8_t gpu_rgba8_g(uint32_t c) { return (uint8_t)(c >> 8); }
static inline uint8_t gpu_rgba8_b(uint32_t c) { return (uint8_t)(c >> 16); }
static inline uint8_t gpu_rgba8_a(uint32_t c) { return (uint8_t)(c >> 24); }
