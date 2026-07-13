#pragma once

#include <pebble.h>

#define SETTINGS_KEY        1

// The phone precomputes one window of minute-by-minute positions spanning
// 45 minutes into the past through 45 minutes into the future, centered on
// "now" at compute time (see WINDOW_NOW_INDEX docs on PositionBatch below).
// This gives the ground track a full past+future history immediately on
// load, instead of the watch having to accumulate "past" live over time
// (which took 45+ minutes to fill in and never showed anything sooner).
#define WINDOW_PAST_MIN     45
#define WINDOW_FUTURE_MIN   45
#define WINDOW_SIZE         (WINDOW_PAST_MIN + WINDOW_FUTURE_MIN + 1)  // 91

// Map content area (matches the redesign mockup's 180x90 viewBox 1:1).
#define MAP_X 10
#define MAP_Y 62
#define MAP_W 180
#define MAP_H 90

typedef struct {
  uint32_t NoradId;
  bool ShowLatLon;
  bool ShowAltitude;
  bool ShowTrail;
  bool ShowDayNight;
} ClaySettings;

typedef struct {
  int16_t  lat_e2[WINDOW_SIZE];  // latitude * 100
  int16_t  lon_e2[WINDOW_SIZE];  // longitude * 100
  uint16_t alt_km[WINDOW_SIZE];
  uint8_t  count;                // valid entries (<=WINDOW_SIZE)
  uint8_t  now_index;            // index of "now" at the time this batch was computed
  uint32_t batch_start_epoch;    // epoch of index 0
} PositionBatch;

extern ClaySettings settings;
extern PositionBatch s_batch;
extern uint8_t s_batch_index;  // current position pointer within s_batch, advances each minute
extern bool s_have_data;
extern char s_sat_name[24];
