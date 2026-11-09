#pragma once

#include <stdint.h>

// Framebuffer resolution of the on-screen display, in pixels.
// The VI layer is 1.5x this size in the 1920x1080 layer coordinate space, so
// handheld mode (720p) maps it 1:1 onto the LCD.
#define OSD_FB_WIDTH  320
#define OSD_FB_HEIGHT 72

// Renders one OSD frame as premultiplied RGBA8888 (R in the lowest byte).
//  pixels    : linear destination, OSD_FB_HEIGHT rows of `stride_px` pixels
//  level     : bar fill, 0..1 (may be an in-between animated value)
//  percent   : number printed on the right, 0..100
//  opacity   : global fade, 0..1 (0 writes a fully transparent frame)
//  offset_y  : vertical slide of the panel in pixels
void osdRenderFrame(uint32_t *pixels, uint32_t stride_px, float level, int percent, float opacity, float offset_y);
