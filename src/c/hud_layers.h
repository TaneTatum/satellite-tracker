#pragma once

#include <pebble.h>

// Loads/unloads the custom JetBrains Mono font resources shared by the
// chrome, telemetry, and footer layers. Call hud_fonts_load() once before
// creating any of the layers below, and hud_fonts_unload() on window unload.
void hud_fonts_load(void);
void hud_fonts_unload(void);

// Corner brackets, satellite name title, header/footer rules. Only needs
// layer_mark_dirty() on satellite-name or settings changes, not every tick.
Layer *chrome_layer_create(GRect frame);

// LAT/LON/ALT telemetry grid, reflows between 1-3 columns based on
// settings.ShowLatLon / settings.ShowAltitude.
Layer *telemetry_layer_create(GRect frame);

// Time + date footer.
Layer *footer_layer_create(GRect frame);
