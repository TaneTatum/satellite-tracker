#pragma once

#include <pebble.h>

#define COLOR_BG        GColorFromHEX(0x04070d)  // near-black background
#define COLOR_BRIGHT    GColorFromHEX(0x2ee6a6)  // phosphor green - text/lines/marker
#define COLOR_DIM       GColorFromHEX(0x5c9c8a)  // muted green - labels
#define COLOR_RULE      GColorFromHEX(0x1a6b52)  // dividers, grid, borders
#define COLOR_LAND      GColorFromHEX(0x123f30)  // map landmass fill

// Centidegree-precision equirectangular projection onto the map content area.
GPoint project_latlon_e2(int32_t lat_e2, int32_t lon_e2);

// Draws `text` with `tracking_px` extra pixels between each monospace glyph's
// natural advance. `font` must be a monospace font (advance is measured from
// a single "0" character and assumed constant for every glyph).
void draw_spaced_text(GContext *ctx, const char *text, GFont font, GRect box,
                       GColor color, int8_t tracking_px, GTextAlignment align);

// Draws a dashed line from p0 to p1. `*phase_px` must be initialized to 0
// before the first segment of a polyline and threaded through unchanged
// across subsequent segments so the dash pattern stays continuous.
void draw_dashed_segment(GContext *ctx, GPoint p0, GPoint p1,
                          int16_t dash_px, int16_t gap_px, int32_t *phase_px);
