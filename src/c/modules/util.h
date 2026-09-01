#pragma once

#include <pebble.h>

#include "data.h"

GPoint util_make_hand_point(int quantity, int intervals, int len, GPoint center);

// Draws a paletted icon resource tinted to `tint`, `icon_size` square,
// centered on `center`. Shared by every icon this face draws (sun-arc
// weather icons, moon-phase icons) so there's exactly one rendering
// technique, not a bespoke one per feature. Overwrites the resource's placeholder
// palette (index 0 = transparent, 1-3 = 33/66/100% of `tint` -- see
// weather_icons_gen.py's save_paletted()) via gbitmap_set_palette(),
// keeping each index's baked alpha level, capped at `max_alpha` (1-3;
// pass 3 for the normal, uncapped case -- capping below 3 uniformly dims
// the whole icon by alpha-blending it toward whatever's behind it, used
// by moon_draw()'s "not actually visible right now" case).
void util_draw_tinted_icon(GContext *ctx, uint32_t resource_id, GPoint center, GColor tint, int icon_size, int max_alpha);

bool util_weather_data_is_valid();

int util_convert_temp(PersistData *persist_data, int val_c);

int util_convert_wind_speed(PersistData *persist_data, int val_kph);
