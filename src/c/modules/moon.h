#pragma once

#include <pebble.h>

// The moon icon: sits in the region below the sun-arc and above the digital
// time, tracking the moon's actual sky position and phase.

// Draws the current moon icon (or nothing, if the moon is below the
// horizon right now, or no moon data has ever been received) into
// `bounds`, for the given local time `t` (used only for the daytime-faint
// color check -- the moon's own phase/position are precomputed phone-side
// and read directly from AppState, not derived from `t`).
void moon_draw(GContext *ctx, GRect bounds, struct tm *t);
