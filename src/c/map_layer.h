#pragma once

#include <pebble.h>

// Creates the map layer (bitmap, border, night terminator, graticule,
// ground track, marker) sized to the full window bounds, drawing at
// absolute screen coordinates (MAP_X/MAP_Y/MAP_W/MAP_H from app_state.h).
Layer *map_layer_create(GRect frame);

// Destroys the loaded map bitmap resource. Call before layer_destroy()
// on the layer returned by map_layer_create().
void map_layer_teardown(void);
