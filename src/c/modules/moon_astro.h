#pragma once

#include <pebble.h>

// C port of moon.ts's astronomy (SunCalc-derived -- see moon_astro.c's
// header for the license). On-watch use only, by main_window.c's FAST/
// ULTRA sweep test mode, so a swept time can be turned into a real moon
// phase/position locally instead of pinning to fixed demo values -- the normal,
// non-sweep render path is untouched by this: it still just displays
// whatever moon_phase/moon_azimuth/moon_altitude the phone last computed
// and sent, same as before).

// Our phase index, 0..27 (0 = new, 14 = full) for `utc_time` -- see
// weather_icons_gen.py's MOON_GLYPHS for how this maps to an icon
// resource. Mirrors moon.ts's getMoonPhaseIndex().
int moon_astro_get_phase_index(time_t utc_time);

// Moon sky position for `utc_time` at the given location, written to
// `*out_azimuth_deg` (standard compass bearing, 0=N/90=E) and
// `*out_altitude_deg` (degrees above/below horizon). Mirrors moon.ts's
// getMoonPosition().
void moon_astro_get_position(
  time_t utc_time, double lat_deg, double lon_deg, int *out_azimuth_deg, int *out_altitude_deg
);
