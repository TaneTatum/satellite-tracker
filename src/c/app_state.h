#pragma once

#include <pebble.h>

#define SETTINGS_KEY         1
#define POSITION_BATCH_SIZE  16
#define TRAIL_MAX_POINTS     18

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

extern ClaySettings settings;
extern PositionBatch s_batch;
extern uint8_t s_batch_index;
extern OrbitTrail s_trail;
extern bool s_have_data;
extern char s_sat_name[24];
