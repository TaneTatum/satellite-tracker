#include "hud_layers.h"
#include "app_state.h"
#include "draw_utils.h"

#include <stdlib.h>
#include <ctype.h>

static GFont s_font_title;
static GFont s_font_label;
static GFont s_font_value;
static GFont s_font_time;
static GFont s_font_date;

void hud_fonts_load(void) {
  s_font_title = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JBMONO_BOLD_12));
  s_font_label = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JBMONO_REGULAR_8));
  s_font_value = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JBMONO_BOLD_13));
  s_font_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JBMONO_BOLD_36));
  s_font_date = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JBMONO_REGULAR_9));
}

void hud_fonts_unload(void) {
  fonts_unload_custom_font(s_font_title);
  fonts_unload_custom_font(s_font_label);
  fonts_unload_custom_font(s_font_value);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_date);
}

// ============================================================================
// CHROME (corner brackets, title, header/footer rules) — static, redrawn
// only on satellite-name or settings changes, not the per-minute tick.
// ============================================================================

static void chrome_layer_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, COLOR_BRIGHT);
  // Corner brackets, 2px-thick L-shapes.
  graphics_fill_rect(ctx, GRect(4, 4, 10, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(4, 4, 2, 10), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(186, 4, 10, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(194, 4, 2, 10), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(4, 222, 10, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(4, 214, 2, 10), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(186, 222, 10, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(194, 214, 2, 10), 0, GCornerNone);

  const char *title = s_sat_name[0] ? s_sat_name : "LOADING...";
  draw_spaced_text(ctx, title, s_font_title, GRect(12, 8, 176, 14),
                    COLOR_BRIGHT, 3, GTextAlignmentCenter);

  graphics_context_set_fill_color(ctx, COLOR_RULE);
  graphics_fill_rect(ctx, GRect(12, 24, 176, 1), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(12, 164, 176, 1), 0, GCornerNone);
}

Layer *chrome_layer_create(GRect frame) {
  Layer *layer = layer_create(frame);
  layer_set_update_proc(layer, chrome_layer_update_proc);
  return layer;
}

// ============================================================================
// TELEMETRY (LAT/LON/ALT grid, 1-3 columns depending on settings)
// ============================================================================

static void format_telemetry(char *lat_buf, size_t lat_sz, char *lon_buf, size_t lon_sz,
                              char *alt_buf, size_t alt_sz) {
  if (!s_have_data || s_batch.count == 0) {
    snprintf(lat_buf, lat_sz, "--.-");
    snprintf(lon_buf, lon_sz, "--.-");
    snprintf(alt_buf, alt_sz, "--KM");
    return;
  }
  int16_t lat_e2 = s_batch.lat_e2[s_batch_index];
  int16_t lon_e2 = s_batch.lon_e2[s_batch_index];
  uint16_t alt = s_batch.alt_km[s_batch_index];

  int lat_whole = abs(lat_e2) / 100;
  int lat_tenth = (abs(lat_e2) % 100) / 10;
  int lon_whole = abs(lon_e2) / 100;
  int lon_tenth = (abs(lon_e2) % 100) / 10;

  snprintf(lat_buf, lat_sz, "%d.%d%c", lat_whole, lat_tenth, lat_e2 >= 0 ? 'N' : 'S');
  snprintf(lon_buf, lon_sz, "%d.%d%c", lon_whole, lon_tenth, lon_e2 >= 0 ? 'E' : 'W');
  snprintf(alt_buf, alt_sz, "%uKM", alt);
}

static void draw_telemetry_column(GContext *ctx, GRect col, const char *label, const char *value) {
  draw_spaced_text(ctx, label, s_font_label, GRect(col.origin.x, 29, col.size.w, 9),
                    COLOR_DIM, 1, GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, COLOR_BRIGHT);
  graphics_draw_text(ctx, value, s_font_value, GRect(col.origin.x, 38, col.size.w, 14),
                      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void telemetry_layer_update_proc(Layer *layer, GContext *ctx) {
  bool show_ll = settings.ShowLatLon;
  bool show_alt = settings.ShowAltitude;
  if (!show_ll && !show_alt) return;

  char lat_buf[8], lon_buf[8], alt_buf[8];
  format_telemetry(lat_buf, sizeof(lat_buf), lon_buf, sizeof(lon_buf), alt_buf, sizeof(alt_buf));

  graphics_context_set_fill_color(ctx, COLOR_RULE);

  if (show_ll && show_alt) {
    draw_telemetry_column(ctx, GRect(12, 29, 59, 22), "LAT", lat_buf);
    draw_telemetry_column(ctx, GRect(71, 29, 59, 22), "LON", lon_buf);
    draw_telemetry_column(ctx, GRect(130, 29, 58, 22), "ALT", alt_buf);
    graphics_fill_rect(ctx, GRect(71, 29, 1, 22), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(130, 29, 1, 22), 0, GCornerNone);
  } else if (show_ll) {
    draw_telemetry_column(ctx, GRect(12, 29, 88, 22), "LAT", lat_buf);
    draw_telemetry_column(ctx, GRect(100, 29, 88, 22), "LON", lon_buf);
    graphics_fill_rect(ctx, GRect(100, 29, 1, 22), 0, GCornerNone);
  } else {
    draw_telemetry_column(ctx, GRect(12, 29, 176, 22), "ALT", alt_buf);
  }
}

Layer *telemetry_layer_create(GRect frame) {
  Layer *layer = layer_create(frame);
  layer_set_update_proc(layer, telemetry_layer_update_proc);
  return layer;
}

// ============================================================================
// FOOTER (time + date)
// ============================================================================

static void footer_layer_update_proc(Layer *layer, GContext *ctx) {
  time_t temp = time(NULL);
  struct tm *local = localtime(&temp);
  if (!local) return;

  static char s_time_buf[8];
  strftime(s_time_buf, sizeof(s_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", local);
  draw_spaced_text(ctx, s_time_buf, s_font_time, GRect(12, 172, 176, 36),
                    COLOR_BRIGHT, 1, GTextAlignmentCenter);

  static char s_date_buf[16];
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", local);
  for (char *p = s_date_buf; *p; p++) *p = (char)toupper((unsigned char)*p);
  draw_spaced_text(ctx, s_date_buf, s_font_date, GRect(12, 211, 176, 9),
                    COLOR_DIM, 3, GTextAlignmentCenter);
}

Layer *footer_layer_create(GRect frame) {
  Layer *layer = layer_create(frame);
  layer_set_update_proc(layer, footer_layer_update_proc);
  return layer;
}
