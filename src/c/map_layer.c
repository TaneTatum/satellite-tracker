#include "map_layer.h"
#include "app_state.h"
#include "draw_utils.h"

// Deliberately no <math.h> here. libm's float trig (tanf/cosf/acosf) calls
// into newlib's __ieee754_rem_pio2f for argument reduction, which hard-
// faults on real Emery hardware (confirmed via `pebble logs` app-fault
// PC/LR symbolication landing squarely inside that routine) even though it
// runs fine under the QEMU emulator. Pebble's own fixed-point
// sin_lookup()/cos_lookup()/atan2_lookup() sidestep that code path
// entirely and are the standard way to do trig on-watch.

static int32_t deg_to_trig(float deg) {
  return (int32_t)(deg * (float)TRIG_MAX_ANGLE / 360.0f);
}

static float tan_deg(float deg) {
  int32_t angle = deg_to_trig(deg);
  return (float)sin_lookup(angle) / (float)cos_lookup(angle);
}

static GBitmap *s_map_bitmap;

// Solar hour-angle at which the sun is on the horizon for a given latitude,
// added to the subsolar longitude to get the terminator's longitude there.
// Latitudes are pre-clamped to +-85 by the caller to avoid the pole
// singularity in tan(lat).
static float terminator_lon_deg(float lon_ss_deg, float decl_deg, float lat_deg) {
  float cos_arg = -tan_deg(lat_deg) * tan_deg(decl_deg);
  if (cos_arg > 1.0f) cos_arg = 1.0f;
  if (cos_arg < -1.0f) cos_arg = -1.0f;

  // hour_angle = acos(cos_arg), via atan2(sin_arg, cos_arg) since sin_arg
  // (the hour angle is always in [0,180]) is never negative. sin_arg is
  // computed with the integer isqrt rather than sqrtf for the same reason
  // trig above avoids libm.
  float sin_sq = 1.0f - cos_arg * cos_arg;
  if (sin_sq < 0.0f) sin_sq = 0.0f;
  float sin_arg = (float)isqrt32((uint32_t)(sin_sq * 1000000.0f)) / 1000.0f;

  int32_t hour_angle_units = atan2_lookup((int16_t)(sin_arg * 10000.0f), (int16_t)(cos_arg * 10000.0f));
  float hour_angle_deg = (float)hour_angle_units * 360.0f / (float)TRIG_MAX_ANGLE;

  return lon_ss_deg + hour_angle_deg;
}

static void draw_night_terminator(GContext *ctx) {
  time_t temp = time(NULL);
  struct tm *utc = gmtime(&temp);
  if (!utc) return;

  float n = (float)(utc->tm_yday + 1) + utc->tm_hour / 24.0f;
  int32_t decl_angle = deg_to_trig((360.0f / 365.24f) * (n + 10.0f));
  float decl = -23.44f * ((float)cos_lookup(decl_angle) / (float)TRIG_MAX_RATIO);

  float utc_hours = utc->tm_hour + utc->tm_min / 60.0f;
  float lon_ss = 15.0f * (12.0f - utc_hours);
  while (lon_ss > 180.0f) lon_ss -= 360.0f;
  while (lon_ss < -180.0f) lon_ss += 360.0f;

  float top_lon = terminator_lon_deg(lon_ss, decl, 85.0f);
  float bot_lon = terminator_lon_deg(lon_ss, decl, -85.0f);

  // Map-local (0..MAP_W) x for a given longitude.
  float x_top = (top_lon + 180.0f) * MAP_W / 360.0f;
  float x_bottom = (bot_lon + 180.0f) * MAP_W / 360.0f;
  float x_ss = (lon_ss + 180.0f) * MAP_W / 360.0f;
  bool night_on_right = x_ss < (MAP_W / 2.0f);

  // Horizontal-line dither (alternating scanlines) rather than a per-pixel
  // checkerboard: a per-pixel loop over the map (up to ~4000
  // graphics_draw_pixel calls, each with real per-call overhead) run on the
  // very first render at watchface load was slow enough on real hardware to
  // trip the app watchdog and crash on load — the QEMU emulator doesn't
  // enforce that timeout as strictly, so it went unnoticed in testing. A
  // fill_rect per alternating row gives the same ~50% darkening for a
  // fraction of the draw calls (MAP_H/2 instead of MAP_W*MAP_H/~4).
  graphics_context_set_fill_color(ctx, COLOR_BG);
  for (int16_t y = 0; y < MAP_H; y += 2) {
    float x_term = x_top + (x_bottom - x_top) * (y / (float)MAP_H);
    int16_t xt = (int16_t)x_term;
    if (night_on_right) {
      int16_t x0 = xt < 0 ? 0 : xt;
      if (x0 < MAP_W) {
        graphics_fill_rect(ctx, GRect(MAP_X + x0, MAP_Y + y, MAP_W - x0, 1), 0, GCornerNone);
      }
    } else {
      int16_t x1 = xt > MAP_W ? MAP_W : xt;
      if (x1 > 0) {
        graphics_fill_rect(ctx, GRect(MAP_X, MAP_Y + y, x1, 1), 0, GCornerNone);
      }
    }
  }
}

static void draw_graticule(GContext *ctx) {
  graphics_context_set_stroke_color(ctx, COLOR_RULE);
  int32_t phase;

  phase = 0;
  draw_dashed_segment(ctx, GPoint(MAP_X, MAP_Y + MAP_H / 2),
                       GPoint(MAP_X + MAP_W, MAP_Y + MAP_H / 2), 2, 2, &phase);
  phase = 0;
  draw_dashed_segment(ctx, GPoint(MAP_X + MAP_W / 2, MAP_Y),
                       GPoint(MAP_X + MAP_W / 2, MAP_Y + MAP_H), 2, 2, &phase);
}

// True if consecutive minute-by-minute points jumped further sideways than
// any real orbit could move in a minute — i.e. the satellite crossed the
// +/-180 degree antimeridian and project_latlon_e2 wrapped from one edge of
// the map back to the other, rather than actually having moved there.
static bool is_map_wrap(GPoint a, GPoint b) {
  int16_t dx = b.x - a.x;
  if (dx < 0) dx = (int16_t)-dx;
  return dx > MAP_W / 2;
}

static void draw_ground_track(GContext *ctx) {
  if (!settings.ShowTrail) return;
  if (!s_have_data || s_batch.count < 2) return;

  graphics_context_set_stroke_color(ctx, COLOR_BRIGHT);
  graphics_context_set_stroke_width(ctx, 1);

  // Past track (solid) — indices [0, s_batch_index], precomputed by the
  // phone alongside the future half, so full history is available
  // immediately rather than needing to accumulate live minute by minute.
  if (s_batch_index >= 1) {
    GPoint prev = project_latlon_e2(s_batch.lat_e2[0], s_batch.lon_e2[0]);
    for (uint8_t i = 1; i <= s_batch_index; i++) {
      GPoint next = project_latlon_e2(s_batch.lat_e2[i], s_batch.lon_e2[i]);
      if (!is_map_wrap(prev, next)) {
        graphics_draw_line(ctx, prev, next);
      }
      prev = next;
    }
  }

  // Future track (dashed) — indices [s_batch_index, count-1].
  if (s_batch.count > (uint8_t)(s_batch_index + 1)) {
    int32_t phase = 0;
    GPoint prev = project_latlon_e2(s_batch.lat_e2[s_batch_index], s_batch.lon_e2[s_batch_index]);
    for (uint8_t i = s_batch_index + 1; i < s_batch.count; i++) {
      GPoint next = project_latlon_e2(s_batch.lat_e2[i], s_batch.lon_e2[i]);
      if (is_map_wrap(prev, next)) {
        phase = 0;  // reset dash phase so the new segment starts on a dash
      } else {
        draw_dashed_segment(ctx, prev, next, 2, 2, &phase);
      }
      prev = next;
    }
  }
}

static void draw_marker(GContext *ctx) {
  if (!s_have_data || s_batch.count == 0) return;

  GPoint m = project_latlon_e2(s_batch.lat_e2[s_batch_index], s_batch.lon_e2[s_batch_index]);

  graphics_context_set_stroke_color(ctx, COLOR_BRIGHT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, m, 7);

  graphics_draw_line(ctx, GPoint(m.x - 11, m.y), GPoint(m.x - 4, m.y));
  graphics_draw_line(ctx, GPoint(m.x + 4, m.y), GPoint(m.x + 11, m.y));
  graphics_draw_line(ctx, GPoint(m.x, m.y - 11), GPoint(m.x, m.y - 4));
  graphics_draw_line(ctx, GPoint(m.x, m.y + 4), GPoint(m.x, m.y + 11));

  graphics_context_set_fill_color(ctx, COLOR_BRIGHT);
  graphics_fill_circle(ctx, m, 2);
}

static void map_layer_update_proc(Layer *layer, GContext *ctx) {
  if (s_map_bitmap) {
    graphics_draw_bitmap_in_rect(ctx, s_map_bitmap, GRect(MAP_X, MAP_Y, MAP_W, MAP_H));
  }

  draw_night_terminator(ctx);
  draw_graticule(ctx);
  draw_ground_track(ctx);
  draw_marker(ctx);

  graphics_context_set_stroke_color(ctx, COLOR_RULE);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(MAP_X - 1, MAP_Y - 1, MAP_W + 2, MAP_H + 2));
}

Layer *map_layer_create(GRect frame) {
  s_map_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WORLD_MAP);
  Layer *layer = layer_create(frame);
  layer_set_update_proc(layer, map_layer_update_proc);
  return layer;
}

void map_layer_teardown(void) {
  if (s_map_bitmap) {
    gbitmap_destroy(s_map_bitmap);
    s_map_bitmap = NULL;
  }
}
