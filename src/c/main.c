/**
 * Satellite Tracker — Emery (200x228) watchface, "mission control" HUD style.
 *
 * Shows a configurable satellite's live position on a world map. Orbit
 * propagation (SGP4) runs on the phone via PebbleKit JS (src/pkjs/index.js),
 * which sends a batch of ~16 future minute-by-minute positions over
 * AppMessage. The watch just steps through the local batch once a minute —
 * no orbital math or TLE parsing happens here, and the phone is only woken
 * roughly once per batch window instead of every minute.
 *
 * Drawing is split across draw_utils.c (shared primitives), map_layer.c
 * (bitmap map + night terminator + ground track + marker), and
 * hud_layers.c (corner-bracket chrome, telemetry grid, clock footer).
 */

#include <pebble.h>

#include "app_state.h"
#include "draw_utils.h"
#include "map_layer.h"
#include "hud_layers.h"

// ============================================================================
// GLOBAL STATE (declared extern in app_state.h for the other modules)
// ============================================================================

ClaySettings settings;
PositionBatch s_batch;
uint8_t s_batch_index;
OrbitTrail s_trail;
bool s_have_data;
char s_sat_name[24];

static Window *s_main_window;
static Layer *s_chrome_layer;
static Layer *s_telemetry_layer;
static Layer *s_map_layer;
static Layer *s_footer_layer;
static bool s_request_pending;

// ============================================================================
// TRAIL
// ============================================================================

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
  layer_mark_dirty(s_footer_layer);

  if (s_have_data && s_batch.count > 0) {
    if (s_batch_index < s_batch.count - 1) {
      s_batch_index++;
    }
    GPoint p = project_latlon_e2(s_batch.lat_e2[s_batch_index], s_batch.lon_e2[s_batch_index]);
    trail_push(p);
    layer_mark_dirty(s_telemetry_layer);
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
    layer_mark_dirty(s_chrome_layer);
    layer_mark_dirty(s_telemetry_layer);
    layer_mark_dirty(s_map_layer);
  }

  if (norad_changed) {
    s_have_data = false;
    s_batch.count = 0;
    s_batch_index = 0;
    s_trail.count = 0;
    s_trail.head = 0;
    s_sat_name[0] = '\0';
    layer_mark_dirty(s_chrome_layer);
    layer_mark_dirty(s_telemetry_layer);
    layer_mark_dirty(s_map_layer);
    request_positions();
  }

  // Position batch payload
  Tuple *name_t = dict_find(iterator, MESSAGE_KEY_SatName);
  if (name_t) {
    snprintf(s_sat_name, sizeof(s_sat_name), "%s", name_t->value->cstring);
    layer_mark_dirty(s_chrome_layer);
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
      layer_mark_dirty(s_telemetry_layer);
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
// WINDOW HANDLERS
// ============================================================================

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  hud_fonts_load();

  s_map_layer = map_layer_create(bounds);
  layer_add_child(window_layer, s_map_layer);

  s_telemetry_layer = telemetry_layer_create(bounds);
  layer_add_child(window_layer, s_telemetry_layer);

  s_footer_layer = footer_layer_create(bounds);
  layer_add_child(window_layer, s_footer_layer);

  // Chrome added last (on top) so its brackets/rules always win any overlap.
  s_chrome_layer = chrome_layer_create(bounds);
  layer_add_child(window_layer, s_chrome_layer);
}

static void main_window_unload(Window *window) {
  map_layer_teardown();
  layer_destroy(s_map_layer);
  layer_destroy(s_telemetry_layer);
  layer_destroy(s_footer_layer);
  layer_destroy(s_chrome_layer);
  hud_fonts_unload();
}

// ============================================================================
// APPLICATION LIFECYCLE
// ============================================================================

static void init(void) {
  prv_load_settings();

  s_main_window = window_create();
  window_set_background_color(s_main_window, COLOR_BG);
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
