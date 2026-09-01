#pragma once

#include <pebble.h>

// The sun-arc weather icon: sweeps a 180-degree arc across the primary
// (non-tapped) display, tracking sun position and current weather condition.

// Draws the current sun-arc icon (or nothing, if sunrise/sunset have never
// been received at all) into `bounds`, for the given local time `t`.
void sun_arc_draw(GContext *ctx, GRect bounds, struct tm *t);

// Whether the sun is above the horizon right now (false if sunrise/sunset
// have never been received). Exposed for moon_draw()'s daytime-faint check
// -- see sun_arc.c for why this lives here rather than being re-derived a
// second time in moon.c.
bool sun_arc_is_daytime(struct tm *t);
