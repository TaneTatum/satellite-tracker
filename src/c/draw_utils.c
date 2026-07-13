#include "draw_utils.h"
#include "app_state.h"

#include <string.h>

GPoint project_latlon_e2(int32_t lat_e2, int32_t lon_e2) {
  int32_t x = MAP_X + ((lon_e2 + 18000) * MAP_W) / 36000;
  int32_t y = MAP_Y + ((9000 - lat_e2) * MAP_H) / 18000;
  return GPoint((int16_t)x, (int16_t)y);
}

void draw_spaced_text(GContext *ctx, const char *text, GFont font, GRect box,
                       GColor color, int8_t tracking_px, GTextAlignment align) {
  size_t len = strlen(text);
  if (len == 0) return;

  GSize glyph_size = graphics_text_layout_get_content_size(
      "0", font, GRect(0, 0, 100, 100), GTextOverflowModeFill, GTextAlignmentLeft);
  int16_t advance = glyph_size.w;

  int16_t total_w = (int16_t)(len * advance + (len - 1) * tracking_px);
  int16_t start_x;
  if (align == GTextAlignmentLeft) {
    start_x = box.origin.x;
  } else if (align == GTextAlignmentRight) {
    start_x = (int16_t)(box.origin.x + box.size.w - total_w);
  } else {
    start_x = (int16_t)(box.origin.x + (box.size.w - total_w) / 2);
  }

  graphics_context_set_text_color(ctx, color);
  char buf[2] = {0, 0};
  int16_t cursor = start_x;
  for (size_t i = 0; i < len; i++) {
    buf[0] = text[i];
    graphics_draw_text(ctx, buf, font, GRect(cursor, box.origin.y, advance + 4, box.size.h),
                        GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    cursor = (int16_t)(cursor + advance + tracking_px);
  }
}

uint32_t isqrt32(uint32_t n) {
  uint32_t res = 0;
  uint32_t bit = 1u << 30;  // second-to-top bit set, for a 32-bit operand
  while (bit > n) bit >>= 2;
  while (bit != 0) {
    if (n >= res + bit) {
      n -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return res;
}

void draw_dashed_segment(GContext *ctx, GPoint p0, GPoint p1,
                          int16_t dash_px, int16_t gap_px, int32_t *phase_px) {
  int32_t dx = p1.x - p0.x;
  int32_t dy = p1.y - p0.y;
  int32_t seg_len = (int32_t)isqrt32((uint32_t)(dx * dx + dy * dy));
  if (seg_len < 1) return;

  int32_t period = dash_px + gap_px;
  int32_t traveled = 0;
  while (traveled < seg_len) {
    int32_t pos = (*phase_px) % period;
    bool on = pos < dash_px;
    int32_t step = on ? (dash_px - pos) : (period - pos);
    if (traveled + step > seg_len) step = seg_len - traveled;
    if (on) {
      GPoint a = GPoint((int16_t)(p0.x + dx * traveled / seg_len),
                         (int16_t)(p0.y + dy * traveled / seg_len));
      GPoint b = GPoint((int16_t)(p0.x + dx * (traveled + step) / seg_len),
                         (int16_t)(p0.y + dy * (traveled + step) / seg_len));
      graphics_draw_line(ctx, a, b);
    }
    traveled += step;
    *phase_px += step;
  }
}
