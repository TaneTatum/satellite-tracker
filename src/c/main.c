/**
 * Satellite Tracker — Emery (200x228) watchface.
 *
 * Shows a configurable satellite's live position on a stylized world map.
 * Orbit propagation (SGP4) runs on the phone via PebbleKit JS (src/pkjs/index.js),
 * which sends a batch of ~16 future minute-by-minute positions over AppMessage.
 * The watch just steps through the local batch once a minute — no orbital math
 * or TLE parsing happens here, and the phone is only woken roughly once per
 * batch window instead of every minute.
 */

#include <pebble.h>

#define SETTINGS_KEY         1
#define POSITION_BATCH_SIZE  16
#define TRAIL_MAX_POINTS     18
#define NUM_CONTINENTS        6
#define MAX_CONTINENT_POINTS 15

// World map placement within the 200x228 emery screen (preserves 2:1 aspect ratio)
#define MAP_X  5
#define MAP_Y 27
#define MAP_W 190
#define MAP_H 95

// ============================================================================
// DATA TYPES
// ============================================================================

typedef struct {
  uint32_t NoradId;
  bool ShowLatLon;
  bool ShowAltitude;
  bool ShowTrail;
} ClaySettings;

typedef struct {
  int16_t  lat_e2[POSITION_BATCH_SIZE];  // latitude * 100
  int16_t  lon_e2[POSITION_BATCH_SIZE];  // longitude * 100
  uint16_t alt_km[POSITION_BATCH_SIZE];
  uint8_t  count;                        // valid entries
  uint32_t batch_start_epoch;
} PositionBatch;

typedef struct {
  GPoint  points[TRAIL_MAX_POINTS];
  uint8_t count;
  uint8_t head;
} OrbitTrail;

typedef struct {
  int16_t lat;
  int16_t lon;
} LatLon;

// ============================================================================
// STYLIZED CONTINENT OUTLINES (approximate, ~5deg precision, hand-authored)
// ============================================================================

static const LatLon s_na_latlon[] = {
  {70,-165},{70,-130},{60,-95},{50,-80},{45,-65},{40,-75},{30,-80},
  {25,-97},{15,-95},{20,-105},{30,-115},{40,-125},{55,-130},{70,-165}
};
static const LatLon s_sa_latlon[] = {
  {10,-75},{10,-60},{0,-50},{-10,-35},{-20,-40},{-35,-55},
  {-55,-70},{-40,-75},{-20,-80},{0,-80}
};
static const LatLon s_af_latlon[] = {
  {35,-5},{35,10},{30,32},{15,40},{0,45},{-25,35},{-35,20},
  {-30,15},{-20,12},{-5,10},{10,-15},{25,-15},{35,-5}
};
static const LatLon s_ea_latlon[] = {
  {70,20},{70,60},{75,90},{70,130},{60,160},{45,145},{35,130},
  {20,105},{10,100},{5,80},{15,70},{25,60},{35,45},{30,35},{40,30}
};
static const LatLon s_au_latlon[] = {
  {-12,130},{-12,145},{-20,150},{-30,153},{-38,145},
  {-35,135},{-32,115},{-20,113},{-12,130}
};
static const LatLon s_gl_latlon[] = {
  {83,-35},{82,-20},{70,-22},{60,-45},{70,-55},{80,-60},{83,-35}
};

static const LatLon * const s_continent_latlon[NUM_CONTINENTS] = {
  s_na_latlon, s_sa_latlon, s_af_latlon, s_ea_latlon, s_au_latlon, s_gl_latlon
};
static const uint8_t s_continent_counts[NUM_CONTINENTS] = {
  ARRAY_LENGTH(s_na_latlon), ARRAY_LENGTH(s_sa_latlon), ARRAY_LENGTH(s_af_latlon),
  ARRAY_LENGTH(s_ea_latlon), ARRAY_LENGTH(s_au_latlon), ARRAY_LENGTH(s_gl_latlon)
};

// ============================================================================
// GLOBAL STATE
// ============================================================================

static ClaySettings settings;

static Window *s_main_window;
static Layer *s_map_layer;
static TextLayer *s_title_layer;
static TextLayer *s_time_layer;
static TextLayer *s_latlon_layer;
static TextLayer *s_altitude_layer;

static GPoint s_continent_screen_pts[NUM_CONTINENTS][MAX_CONTINENT_POINTS];
static GPath *s_continent_paths[NUM_CONTINENTS];

static PositionBatch s_batch;
static uint8_t s_batch_index;
static OrbitTrail s_trail;
static bool s_have_data;
static bool s_request_pending;
static char s_sat_name[24];

// ============================================================================
// PROJECTION (equirectangular, integer-only — no floats, no trig needed)
// ============================================================================

// Whole-degree projection, used once at load time for the static continent data.
static GPoint project_latlon(int16_t lat_deg, int16_t lon_deg) {
  int32_t x = MAP_X + ((int32_t)(lon_deg + 180) * MAP_W) / 360;
  int32_t y = MAP_Y + ((int32_t)(90 - lat_deg) * MAP_H) / 180;
  return GPoint((int16_t)x, (int16_t)y);
}

// Centidegree-precision projection, used for the live satellite marker/trail.
static GPoint project_latlon_e2(int32_t lat_e2, int32_t lon_e2) {
  int32_t x = MAP_X + ((lon_e2 + 18000) * MAP_W) / 36000;
  int32_t y = MAP_Y + ((9000 - lat_e2) * MAP_H) / 18000;
  return GPoint((int16_t)x, (int16_t)y);
}

static void trail_push(GPoint p) {
  uint8_t idx = (s_trail.head + s_trail.count) % TRAIL_MAX_POINTS;
  s_trail.points[idx] = p;
  if (s_trail.count < TRAIL_MAX_POINTS) {
    s_trail.count++;
  } else {
    s_trail.head = (s_trail.head + 1) % TRAIL_MAX_POINTS;
  }
}

// ============================================================================
// SETTINGS PERSISTENCE
// ============================================================================

static void prv_default_settings(void) {
  settings.NoradId = 25544;  // ISS (ZARYA)
  settings.ShowLatLon = true;
  settings.ShowAltitude = true;
  settings.ShowTrail = false;
}

static void prv_save_settings(void) {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void prv_load_settings(void) {
  prv_default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

// ============================================================================
// TEXT UPDATES
// ============================================================================

static void update_time_text(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  if (!tick_time) return;

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);
}

static void update_readout_text(void) {
  if (!s_have_data || s_batch.count == 0) {
    text_layer_set_text(s_latlon_layer, "Lat --.--  Lon --.--");
    text_layer_set_text(s_altitude_layer, "Alt -- km");
    return;
  }

  int16_t lat_e2 = s_batch.lat_e2[s_batch_index];
  int16_t lon_e2 = s_batch.lon_e2[s_batch_index];
  uint16_t alt = s_batch.alt_km[s_batch_index];

  int lat_whole = lat_e2 / 100;
  int lat_frac = abs(lat_e2 % 100);
  int lon_whole = lon_e2 / 100;
  int lon_frac = abs(lon_e2 % 100);

  static char s_latlon_buffer[32];
  snprintf(s_latlon_buffer, sizeof(s_latlon_buffer), "%d.%02d%c  %d.%02d%c",
           abs(lat_whole), lat_frac, lat_whole >= 0 ? 'N' : 'S',
           abs(lon_whole), lon_frac, lon_whole >= 0 ? 'E' : 'W');
  text_layer_set_text(s_latlon_layer, s_latlon_buffer);

  static char s_alt_buffer[16];
  snprintf(s_alt_buffer, sizeof(s_alt_buffer), "%u km", alt);
  text_layer_set_text(s_altitude_layer, s_alt_buffer);
}

// ============================================================================
// LAYOUT (reflows time/lat-lon/altitude rows based on which are toggled on)
// ============================================================================

static void prv_relayout(void) {
  GRect bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  bool show_ll = settings.ShowLatLon;
  bool show_alt = settings.ShowAltitude;
  int rows = (show_ll ? 1 : 0) + (show_alt ? 1 : 0);

  GRect time_frame;
  GFont time_font = fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
  GRect row1_frame = GRect(0, 168, bounds.size.w, 20);
  GRect row2_frame = GRect(0, 190, bounds.size.w, 20);

  if (rows == 2) {
    time_frame = GRect(0, 130, bounds.size.w, 34);
  } else if (rows == 1) {
    time_frame = GRect(0, 148, bounds.size.w, 34);
    row1_frame = GRect(0, 186, bounds.size.w, 20);
  } else {
    time_frame = GRect(0, 165, bounds.size.w, 44);
    time_font = fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS);
  }

  layer_set_frame(text_layer_get_layer(s_time_layer), time_frame);
  text_layer_set_font(s_time_layer, time_font);

  if (rows == 2) {
    layer_set_frame(text_layer_get_layer(s_latlon_layer), row1_frame);
    layer_set_frame(text_layer_get_layer(s_altitude_layer), row2_frame);
  } else if (show_ll) {
    layer_set_frame(text_layer_get_layer(s_latlon_layer), row1_frame);
  } else if (show_alt) {
    layer_set_frame(text_layer_get_layer(s_altitude_layer), row1_frame);
  }

  layer_set_hidden(text_layer_get_layer(s_latlon_layer), !show_ll);
  layer_set_hidden(text_layer_get_layer(s_altitude_layer), !show_alt);
}

// ============================================================================
// APPMESSAGE
// ============================================================================

static void request_positions(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint32(iter, MESSAGE_KEY_NoradId, settings.NoradId);
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_POSITIONS, 1);
  app_message_outbox_send();
  s_request_pending = true;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time_text();

  if (s_have_data && s_batch.count > 0) {
    if (s_batch_index < s_batch.count - 1) {
      s_batch_index++;
    }
    GPoint p = project_latlon_e2(s_batch.lat_e2[s_batch_index], s_batch.lon_e2[s_batch_index]);
    trail_push(p);
    update_readout_text();
    layer_mark_dirty(s_map_layer);

    uint8_t low_water = s_batch.count > 2 ? (uint8_t)(s_batch.count - 2) : 0;
    if (s_batch_index >= low_water && !s_request_pending) {
      request_positions();
    }
  } else if (!s_request_pending) {
    request_positions();
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  bool settings_changed = false;
  bool norad_changed = false;

  Tuple *norad_t = dict_find(iterator, MESSAGE_KEY_NoradId);
  if (norad_t) {
    uint32_t new_id = (norad_t->type == TUPLE_CSTRING)
        ? (uint32_t)atoi(norad_t->value->cstring)
        : (uint32_t)norad_t->value->uint32;
    if (new_id != 0 && new_id != settings.NoradId) {
      settings.NoradId = new_id;
      settings_changed = true;
      norad_changed = true;
    }
  }

  Tuple *lat_lon_t = dict_find(iterator, MESSAGE_KEY_ShowLatLon);
  if (lat_lon_t) { settings.ShowLatLon = lat_lon_t->value->int32 == 1; settings_changed = true; }

  Tuple *alt_toggle_t = dict_find(iterator, MESSAGE_KEY_ShowAltitude);
  if (alt_toggle_t) { settings.ShowAltitude = alt_toggle_t->value->int32 == 1; settings_changed = true; }

  Tuple *trail_t = dict_find(iterator, MESSAGE_KEY_ShowTrail);
  if (trail_t) { settings.ShowTrail = trail_t->value->int32 == 1; settings_changed = true; }

  if (settings_changed) {
    prv_save_settings();
    prv_relayout();
    update_readout_text();
    layer_mark_dirty(s_map_layer);
  }

  if (norad_changed) {
    s_have_data = false;
    s_batch.count = 0;
    s_batch_index = 0;
    s_trail.count = 0;
    s_trail.head = 0;
    text_layer_set_text(s_title_layer, "Loading...");
    layer_mark_dirty(s_map_layer);
    request_positions();
  }

  // Position batch payload
  Tuple *name_t = dict_find(iterator, MESSAGE_KEY_SatName);
  if (name_t) {
    snprintf(s_sat_name, sizeof(s_sat_name), "%s", name_t->value->cstring);
    text_layer_set_text(s_title_layer, s_sat_name);
  }

  Tuple *count_t = dict_find(iterator, MESSAGE_KEY_BatchCount);
  Tuple *lat_arr_t = dict_find(iterator, MESSAGE_KEY_LatArray);
  Tuple *lon_arr_t = dict_find(iterator, MESSAGE_KEY_LonArray);
  Tuple *alt_arr_t = dict_find(iterator, MESSAGE_KEY_AltArray);

  if (count_t && lat_arr_t && lon_arr_t && alt_arr_t) {
    uint8_t count = count_t->value->uint8;
    uint16_t avail_lat = lat_arr_t->length / 2;
    uint16_t avail_lon = lon_arr_t->length / 2;
    uint16_t avail_alt = alt_arr_t->length / 2;
    if (count > POSITION_BATCH_SIZE) count = POSITION_BATCH_SIZE;
    if (count > avail_lat) count = (uint8_t)avail_lat;
    if (count > avail_lon) count = (uint8_t)avail_lon;
    if (count > avail_alt) count = (uint8_t)avail_alt;

    // Cast through void* so the compiler doesn't treat these as the
    // statically zero-length `data[0]` flexible array member (which
    // trips -Wzero-length-bounds/-Werror for any index beyond 0).
    const uint8_t *lat_bytes = (const uint8_t *)(const void *)lat_arr_t->value;
    const uint8_t *lon_bytes = (const uint8_t *)(const void *)lon_arr_t->value;
    const uint8_t *alt_bytes = (const uint8_t *)(const void *)alt_arr_t->value;

    for (uint8_t i = 0; i < count; i++) {
      uint8_t lo, hi;
      lo = lat_bytes[i * 2];
      hi = lat_bytes[i * 2 + 1];
      s_batch.lat_e2[i] = (int16_t)(lo | (hi << 8));

      lo = lon_bytes[i * 2];
      hi = lon_bytes[i * 2 + 1];
      s_batch.lon_e2[i] = (int16_t)(lo | (hi << 8));

      lo = alt_bytes[i * 2];
      hi = alt_bytes[i * 2 + 1];
      s_batch.alt_km[i] = (uint16_t)(lo | (hi << 8));
    }
    s_batch.count = count;
    s_batch_index = 0;
    s_have_data = (count > 0);

    Tuple *epoch_t = dict_find(iterator, MESSAGE_KEY_BatchStartEpoch);
    if (epoch_t) s_batch.batch_start_epoch = epoch_t->value->uint32;

    if (s_have_data) {
      GPoint p = project_latlon_e2(s_batch.lat_e2[0], s_batch.lon_e2[0]);
      trail_push(p);
      update_readout_text();
      layer_mark_dirty(s_map_layer);
    }
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped! reason=%d", reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed! reason=%d", reason);
  s_request_pending = false;
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  s_request_pending = false;
}

// ============================================================================
// MAP DRAWING
// ============================================================================

static void map_layer_update_proc(Layer *layer, GContext *ctx) {
  // Ocean background
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, GRect(MAP_X, MAP_Y, MAP_W, MAP_H), 2, GCornersAll);

  // Continents
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  for (int c = 0; c < NUM_CONTINENTS; c++) {
    if (s_continent_paths[c]) {
      gpath_draw_filled(ctx, s_continent_paths[c]);
      gpath_draw_outline(ctx, s_continent_paths[c]);
    }
  }

  if (!s_have_data || s_batch.count == 0) {
    return;
  }

  // Orbit trail (oldest -> newest, growing radius to fake a fade)
  if (settings.ShowTrail && s_trail.count > 0) {
    graphics_context_set_stroke_color(ctx, GColorPictonBlue);
    for (uint8_t i = 0; i < s_trail.count; i++) {
      uint8_t idx = (s_trail.head + i) % TRAIL_MAX_POINTS;
      GPoint p = s_trail.points[idx];
      uint16_t r = 1 + (uint16_t)(i * 2 / (s_trail.count > 1 ? s_trail.count : 1));
      graphics_context_set_fill_color(ctx, GColorPictonBlue);
      graphics_fill_circle(ctx, p, r);
    }
  }

  // Current position marker
  GPoint marker = project_latlon_e2(s_batch.lat_e2[s_batch_index], s_batch.lon_e2[s_batch_index]);
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, marker, 3);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_circle(ctx, marker, 4);
}

// ============================================================================
// WINDOW HANDLERS
// ============================================================================

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Precompute continent GPaths (screen space) once — no per-frame trig/floats needed.
  for (int c = 0; c < NUM_CONTINENTS; c++) {
    uint8_t count = s_continent_counts[c];
    for (uint8_t p = 0; p < count; p++) {
      s_continent_screen_pts[c][p] = project_latlon(s_continent_latlon[c][p].lat,
                                                      s_continent_latlon[c][p].lon);
    }
    GPathInfo info = { .num_points = count, .points = s_continent_screen_pts[c] };
    s_continent_paths[c] = gpath_create(&info);
  }

  s_map_layer = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
  layer_set_update_proc(s_map_layer, map_layer_update_proc);
  layer_add_child(window_layer, s_map_layer);

  s_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 20));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorWhite);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_title_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(s_title_layer, "Loading...");
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  s_time_layer = text_layer_create(GRect(0, 130, bounds.size.w, 34));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  s_latlon_layer = text_layer_create(GRect(0, 168, bounds.size.w, 20));
  text_layer_set_background_color(s_latlon_layer, GColorClear);
  text_layer_set_text_color(s_latlon_layer, GColorLightGray);
  text_layer_set_font(s_latlon_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_latlon_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_latlon_layer));

  s_altitude_layer = text_layer_create(GRect(0, 190, bounds.size.w, 20));
  text_layer_set_background_color(s_altitude_layer, GColorClear);
  text_layer_set_text_color(s_altitude_layer, GColorLightGray);
  text_layer_set_font(s_altitude_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_altitude_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_altitude_layer));

  prv_relayout();
  update_time_text();
  update_readout_text();
}

static void main_window_unload(Window *window) {
  for (int c = 0; c < NUM_CONTINENTS; c++) {
    if (s_continent_paths[c]) {
      gpath_destroy(s_continent_paths[c]);
      s_continent_paths[c] = NULL;
    }
  }
  layer_destroy(s_map_layer);
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_latlon_layer);
  text_layer_destroy(s_altitude_layer);
}

// ============================================================================
// APPLICATION LIFECYCLE
// ============================================================================

static void init(void) {
  prv_load_settings();

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(512, 256);

  // Ask the phone for an initial batch right away.
  request_positions();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
