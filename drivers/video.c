#include "ntzero/drivers.h"

static NTZ_FRAMEBUFFER g_framebuffer;
static u32 g_framebuffer_ready;

NTSTATUS FramebufferConfigure(const NTZ_FRAMEBUFFER *framebuffer) {
    if (framebuffer == (const NTZ_FRAMEBUFFER *)0 || framebuffer->base == (volatile u32 *)0 ||
        framebuffer->width == 0u || framebuffer->height == 0u ||
        framebuffer->pixels_per_scanline < framebuffer->width) {
        g_framebuffer_ready = 0u;
        return STATUS_INVALID_PARAMETER;
    }
    g_framebuffer = *framebuffer;
    g_framebuffer_ready = 1u;
    return STATUS_SUCCESS;
}

void FramebufferPutPixel(u32 x, u32 y, u32 argb) {
    if (g_framebuffer_ready == 0u || x >= g_framebuffer.width || y >= g_framebuffer.height) return;
    g_framebuffer.base[(usize)y * g_framebuffer.pixels_per_scanline + x] = argb;
}

void FramebufferClear(u32 argb) {
    u32 y;
    u32 x;
    if (g_framebuffer_ready == 0u) return;
    for (y = 0u; y < g_framebuffer.height; ++y) {
        for (x = 0u; x < g_framebuffer.width; ++x) {
            g_framebuffer.base[(usize)y * g_framebuffer.pixels_per_scanline + x] = argb;
        }
    }
}
