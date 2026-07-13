#include "draw_utils.h"
#include "app_state.h"

#include <math.h>
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

void draw_dashed_segment(GContext *ctx, GPoint p0, GPoint p1,
                          int16_t dash_px, int16_t gap_px, int32_t *phase_px) {
  int16_t dx = (int16_t)(p1.x - p0.x);
  int16_t dy = (int16_t)(p1.y - p0.y);
  float seg_len = sqrtf((float)(dx * dx + dy * dy));
  if (seg_len < 1.0f) return;

  int32_t period = dash_px + gap_px;
  float traveled = 0.0f;
  while (traveled < seg_len) {
    int32_t pos = (*phase_px) % period;
    bool on = pos < dash_px;
    float step = on ? (float)(dash_px - pos) : (float)(period - pos);
    if (traveled + step > seg_len) step = seg_len - traveled;
    if (on) {
      float t0 = traveled / seg_len;
      float t1 = (traveled + step) / seg_len;
      graphics_draw_line(ctx,
          GPoint((int16_t)(p0.x + dx * t0), (int16_t)(p0.y + dy * t0)),
          GPoint((int16_t)(p0.x + dx * t1), (int16_t)(p0.y + dy * t1)));
    }
    traveled += step;
    *phase_px += (int32_t)step;
  }
}
