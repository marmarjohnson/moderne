/**
 * Moon phase/position astronomy -- C port of src/ts/moon.ts's own port of
 * SunCalc (https://github.com/mourner/suncalc), BSD-2-Clause licensed. See
 * moon.ts's header for the full provenance (found vendored in
 * watchface_sources/mit/halcyon, credited there). Ported a second time,
 * language to language, rather than shared: this is on-watch C, moon.ts is
 * phone-side TypeScript, and there is no code-sharing mechanism between
 * the two runtimes on this project. The formulas are identical; only the
 * trig primitives differ, and not by choice -- see below.
 *
 *   Copyright (c) 2025, Vladimir Agafonkin
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are
 *   met:
 *
 *      1. Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 *   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "moon_astro.h"

#include <math.h> // fmodf/roundf only -- see the trig-primitive block below for why

#define MOON_ASTRO_PI 3.14159265f
#define RAD (MOON_ASTRO_PI / 180.0f)
#define J1970 2440588
#define J2000 2451545

// ---------------------------------------------------------------------
// Trig primitives: Pebble's own fixed-point sin_lookup()/cos_lookup(),
// plus hand-rolled atan2/asin/acos/sqrt on top of them -- not libm's
// sinf/cosf/tanf/asinf/acosf/atan2f, despite this being straightforward
// astronomy math that would normally just call those directly (moon.ts
// does, in JS, with no issue).
//
// Confirmed by extensive direct on-device testing (multiple rounds: large
// raw angles, NaN-from-domain-violation guards, reduced composite angles
// -- each fixed one crash site only to hit another) that this SDK's
// libm trig, when it needs genuine range reduction (anything beyond a
// trivial argument), reliably App-faults inside __kernel_rem_pio2(f) --
// gabbro targets Cortex-M3 here (no hardware float at all, everything
// soft-emulated), and this codebase never called into that code path
// before (sun_arc.c and everything else already exclusively use
// sin_lookup/cos_lookup for exactly this kind of angle math). Rather than
// keep chasing which specific inputs this libm's reduction can survive,
// this sidesteps it entirely: sin_lookup/cos_lookup are simple table
// lookups (no iterative reduction, no crash risk for any input
// magnitude), and atan2/asin/acos/sqrt below are each a fixed handful of
// arithmetic ops -- no calls into the suspect library code at all.
// Accuracy is a few hundredths of a degree at worst (see atan_poly()'s
// comment), utterly sufficient for a 24px icon's position.

static float sinf_safe(float rad) {
  const int32_t angle = (int32_t)(rad * (TRIG_MAX_ANGLE / (2.0f * MOON_ASTRO_PI)));
  return (float)sin_lookup(angle) / (float)TRIG_MAX_RATIO;
}

static float cosf_safe(float rad) {
  const int32_t angle = (int32_t)(rad * (TRIG_MAX_ANGLE / (2.0f * MOON_ASTRO_PI)));
  return (float)cos_lookup(angle) / (float)TRIG_MAX_RATIO;
}

static float tanf_safe(float rad) {
  float c = cosf_safe(rad);
  if (c > -1e-6f && c < 1e-6f) c = (c < 0) ? -1e-6f : 1e-6f; // guard divide-by-zero
  return sinf_safe(rad) / c;
}

// A handful of Newton-Raphson iterations, not sqrtf() -- kept in the same
// "no calls into library code we can't fully vouch for" spirit as the
// rest of this block, and it's simple enough (~8 multiply-adds) that
// there's no real cost to being conservative here too. Negative input
// (from float rounding pushing a value that should be exactly 0 slightly
// below it, e.g. 1-x*x for x just past +-1) returns 0 rather than NaN --
// asinf_safe/acosf_safe below lean on this to stay well-defined right at
// the domain edge without needing a separate clamp.
static float sqrtf_safe(float x) {
  if (x <= 0) return 0;
  float g = x > 1 ? x : 1;
  for (int i = 0; i < 8; i++) g = 0.5f * (g + x / g);
  return g;
}

// Minimax polynomial for atan(t), t in [0,1] -- max error ~0.0028rad
// (~0.16deg). atan2f_safe() below reduces any (y,x) to this range via the
// standard reciprocal-and-reflect identities, then reconstructs the full
// -PI..PI angle from the input signs/magnitudes.
static float atan_poly(float t) {
  const float t2 = t * t;
  return t * (0.9998660f
    + t2 * (-0.3302995f + t2 * (0.1801410f + t2 * (-0.0851330f + t2 * 0.0208351f))));
}

static float atan2f_safe(float y, float x) {
  if (x == 0 && y == 0) return 0;
  const float ax = x < 0 ? -x : x;
  const float ay = y < 0 ? -y : y;
  float angle = (ax > ay) ? atan_poly(ay / ax) : (MOON_ASTRO_PI / 2.0f) - atan_poly(ax / ay);
  if (x < 0) angle = MOON_ASTRO_PI - angle;
  if (y < 0) angle = -angle;
  return angle;
}

static float asinf_safe(float x) {
  return atan2f_safe(x, sqrtf_safe(1.0f - x * x));
}

static float acosf_safe(float x) {
  return atan2f_safe(sqrtf_safe(1.0f - x * x), x);
}

// ---------------------------------------------------------------------

// Obliquity of the Earth.
static const float EARTH_OBLIQUITY = RAD * 23.4397f;

// JS Date.valueOf()/DAY_MS in moon.ts, here in whole seconds instead of
// milliseconds -- same formula, same epoch, just a different input unit.
// The epoch subtraction itself is done in double (a single, shallow
// arithmetic op, not trig) so a `time_t` in the billions doesn't lose the
// fractional-day precision that casting straight to float would; only the
// resulting `d` (~9700 as of 2026, comfortably within float's precision)
// is narrowed to float for everything below.
static float moon_astro_to_days(time_t utc_time) {
  const double d = (double)utc_time / 86400.0 - 0.5 + (double)(J1970 - J2000);
  return (float)d;
}

// Every angle below that's (roughly) linear in `d` grows without bound --
// `d` itself is already ~9700 (days since J2000) as of 2026, so e.g.
// sidereal_time's raw degree value is already in the hundreds of
// thousands. Not a crash-safety concern any more (sin_lookup/cos_lookup
// handle any input magnitude via plain integer wraparound), but still
// worth reducing here for float precision: deg_mod360's *input* has ~7
// significant figures to spend and a 6-figure raw value would leave
// almost none for the sub-degree part that actually matters.
static float deg_mod360(float deg) {
  return fmodf(deg, 360.0f);
}

static float right_ascension(float l, float b) {
  return atan2f_safe(
    sinf_safe(l) * cosf_safe(EARTH_OBLIQUITY) - tanf_safe(b) * sinf_safe(EARTH_OBLIQUITY),
    cosf_safe(l)
  );
}

static float declination(float l, float b) {
  return asinf_safe(
    sinf_safe(b) * cosf_safe(EARTH_OBLIQUITY) + cosf_safe(b) * sinf_safe(EARTH_OBLIQUITY) * sinf_safe(l)
  );
}

static float solar_mean_anomaly(float d) {
  return RAD * deg_mod360(357.5291f + 0.98560028f * d);
}

static float ecliptic_longitude(float m) {
  // Equation of center + perihelion of the Earth.
  const float c = RAD * (
    1.9148f * sinf_safe(m) + 0.02f * sinf_safe(2 * m) + 0.0003f * sinf_safe(3 * m)
  );
  const float p = RAD * 102.9372f;
  return m + c + p + MOON_ASTRO_PI;
}

typedef struct {
  float ra;
  float dec;
} EquatorialCoords;

static EquatorialCoords sun_coords(float d) {
  const float m = solar_mean_anomaly(d);
  const float l = ecliptic_longitude(m);
  EquatorialCoords coords = { .dec = declination(l, 0), .ra = right_ascension(l, 0) };
  return coords;
}

typedef struct {
  float ra;
  float dec;
  float dist;
} MoonCoords;

static MoonCoords moon_coords(float d) {
  const float l = RAD * deg_mod360(218.316f + 13.176396f * d); // ecliptic longitude
  const float m = RAD * deg_mod360(134.963f + 13.064993f * d); // mean anomaly
  const float f = RAD * deg_mod360(93.272f + 13.229350f * d);  // mean distance
  const float lon = l + RAD * 6.289f * sinf_safe(m);
  const float lat = RAD * 5.128f * sinf_safe(f);
  MoonCoords coords = {
    .ra = right_ascension(lon, lat),
    .dec = declination(lon, lat),
    .dist = 385001 - 20905 * cosf_safe(m), // km, geocentric distance to the moon
  };
  return coords;
}

static float sidereal_time(float d, float lw) {
  return RAD * deg_mod360(280.16f + 360.9856235f * d) - lw;
}

static float astro_refraction(float h) {
  const float hh = h < 0 ? 0 : h;
  return 0.0002967f / tanf_safe(hh + 0.00312536f / (hh + 0.08901179f));
}

static float moon_altitude(float h, float phi, float dec) {
  return asinf_safe(sinf_safe(phi) * sinf_safe(dec) + cosf_safe(phi) * cosf_safe(dec) * cosf_safe(h));
}

static float azimuth_rad(float h, float phi, float dec) {
  return atan2f_safe(sinf_safe(h), cosf_safe(h) * sinf_safe(phi) - tanf_safe(dec) * cosf_safe(phi));
}

void moon_astro_get_position(
  time_t utc_time, double lat_deg, double lon_deg, int *out_azimuth_deg, int *out_altitude_deg
) {
  const float lw = RAD * -(float)lon_deg;
  const float phi = RAD * (float)lat_deg;
  const float d = moon_astro_to_days(utc_time);
  const MoonCoords c = moon_coords(d);
  const float h = sidereal_time(d, lw) - c.ra;
  float alt = moon_altitude(h, phi, c.dec);
  alt += astro_refraction(alt); // matches SunCalc's own refraction correction
  const float az_from_south = azimuth_rad(h, phi, c.dec);
  float azimuth_deg = fmodf((az_from_south / RAD) + 180 + 360, 360);
  if (azimuth_deg < 0) azimuth_deg += 360;

  *out_azimuth_deg = (int)roundf(azimuth_deg);
  *out_altitude_deg = (int)roundf(alt / RAD);
}

int moon_astro_get_phase_index(time_t utc_time) {
  const float d = moon_astro_to_days(utc_time);
  const EquatorialCoords s = sun_coords(d);
  const MoonCoords m = moon_coords(d);
  const float sun_dist_km = 149598000; // Earth-Sun distance, km
  const float phi = acosf_safe(
    sinf_safe(s.dec) * sinf_safe(m.dec) + cosf_safe(s.dec) * cosf_safe(m.dec) * cosf_safe(s.ra - m.ra)
  );
  const float inc = atan2f_safe(sun_dist_km * sinf_safe(phi), m.dist - sun_dist_km * cosf_safe(phi));
  const float angle = atan2f_safe(
    cosf_safe(s.dec) * sinf_safe(s.ra - m.ra),
    sinf_safe(s.dec) * cosf_safe(m.dec) - cosf_safe(s.dec) * sinf_safe(m.dec) * cosf_safe(s.ra - m.ra)
  );
  const float phase = 0.5f + (0.5f * inc * (angle < 0 ? -1 : 1)) / MOON_ASTRO_PI;

  int idx = ((int)roundf(phase * 28)) % 28;
  if (idx < 0) idx += 28;
  return idx;
}
