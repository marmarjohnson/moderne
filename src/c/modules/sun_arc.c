#include "sun_arc.h"

#include "../config.h"
#include "data.h"
#include "util.h"

// One constant radius, one circle, for the entire 180deg path -- horizon
// markers, sunrise/sunset, and the continuous day/night sweep all sit on it.
// An earlier version of this file used two different radii (small for the
// continuous sweep, large for the four fixed positions) to chase zero
// overlap with the time text. That was the wrong tradeoff: it bought a
// property (never touching the text) at the cost of a visible, unexplained
// jump in the icon's distance from center at every phase boundary -- worse
// than the problem it solved. Reverted per your call.
//
// This radius is bezel-safe at every angle (worst case is 45deg, not the
// apex or the horizon -- see the corner-distance math this value was picked
// against) but does NOT avoid the time text: no constant radius can, for
// any icon size down to ~10px, because the display isn't wide enough
// relative to the worst-case time string ("08:00"/"20:00"-class, ~190px+)
// -- verified by simulating the full sweep against the real measured text
// bounding box. In practice this means the icon will pass close to, and on
// some minutes briefly overlap, the time digits during roughly the last
// 40-50 degrees of its approach to each sunrise/sunset. Accepted tradeoff, not hidden.
#define SUN_ARC_ICON_SIZE 24
#define SUN_ARC_RADIUS 112

// Fixed 5-minute windows around sunrise/sunset where the icon is a static
// horizon/sunrise/sunset marker rather than sweeping.
#define TRANSITION_MIN 5

// Where every transition icon's horizon line aligns -- the same separator
// bar draw_digital_time() draws under the time (main_window.c's sep_y).
// Hardcoded to the gabbro value directly (this file only ever compiles for
// gabbro) rather than passing the full per-platform scl_y_pp() argument set.
#define HORIZON_Y_PCT 495

// wi-horizon/wi-sunrise/wi-sunset's own horizontal line sits near the
// *bottom* of their 24px canvas (measured directly from the generated PNGs:
// row 18-20 of 0-23, not the row-12 canvas center), not at the glyph's
// bounding-box center. Centering the canvas on horizon_y therefore drew the
// actual line ~7px below the bar instead of on it -- this offset moves the
// canvas center up so the real line lands exactly on horizon_y. Only
// applies to these three static icons; the sweep/neutral condition icons
// have no "must align with X" requirement, so they stay canvas-centered.
#define TRANSITION_ICON_Y_OFFSET 7

// Weather condition staleness (Development_Guidance.org > Open Considerations > Settled):
// fall back to the clear-sky icons if the last successful fetch is older
// than this, rather than keep showing a condition that may no longer be
// true. Deliberately not applied to sunrise/sunset -- see the same doc
// section for why those two fields age out on different policies.
#define WEATHER_STALE_S (4 * 60 * 60)

// MINUTES_PER_DAY comes from pebble.h itself (HOURS_PER_DAY *
// MINUTES_PER_HOUR) -- no local definition needed.

typedef enum {
  SUN_PHASE_NEUTRAL,       // before sunrise-5m, or after sunset+5m
  SUN_PHASE_HORIZON_EAST,  // sunrise-5m .. sunrise
  SUN_PHASE_SUNRISE,       // sunrise .. sunrise+5m
  SUN_PHASE_DAY_SWEEP,     // sunrise+5m .. sunset-5m
  SUN_PHASE_SUNSET,        // sunset-5m .. sunset
  SUN_PHASE_HORIZON_WEST,  // sunset .. sunset+5m
} SunPhase;

// "HH:MM" -> minutes since midnight, or -1 if the string is empty/malformed
// (no data received yet, e.g. very first launch with no persisted snapshot).
static int parse_minutes_of_day(const char *hhmm) {
  if (strlen(hhmm) < 5) return -1;
  const char h_arr[3] = {hhmm[0], hhmm[1], '\0'};
  const char m_arr[3] = {hhmm[3], hhmm[4], '\0'};
  return atoi(h_arr) * 60 + atoi(m_arr);
}

// Whether the sun is above the horizon right now -- exposed (see sun_arc.h)
// for moon_draw()'s daytime-faint check, so that determination is made exactly once, shared,
// rather than moon.c re-deriving it from a second copy of the sunrise/
// sunset parsing above.
bool sun_arc_is_daytime(struct tm *t) {
  AppState *app_state = data_get_app_state();
  const int sunrise_min = parse_minutes_of_day(app_state->sunrise);
  const int sunset_min = parse_minutes_of_day(app_state->sunset);
  if (sunrise_min < 0 || sunset_min < 0) return false;
  const int now_min = t->tm_hour * 60 + t->tm_min;
  return now_min >= sunrise_min && now_min < sunset_min;
}

// Point on the arc at `elapsed`/`total` progress through a sweep. Forward
// (day, reverse=false) goes East (90deg) -> apex (0deg) -> West (-90deg) as
// elapsed/total goes 0 -> 1. Reverse (night) goes West -> apex -> East --
// exactly the mirror image, so it's just the negated quantity rather than a
// second angle derivation:
//   forward quantity/intervals = 1/4 - (elapsed/total)/2
//     at elapsed=0: 1/4 (East); at elapsed=total: -1/4 (West)
//   reverse is forward's angle negated: -1/4 + (elapsed/total)/2
//     at elapsed=0: -1/4 (West); at elapsed=total: 1/4 (East)
// Expressed as a quantity/intervals ratio so util_make_hand_point()
// (TRIG_MAX_ANGLE * quantity / intervals) can be reused as-is instead of
// duplicating its sin_lookup/cos_lookup math.
static GPoint sweep_point(int elapsed, int total, GPoint center, bool reverse) {
  const int32_t intervals = 4 * total;
  int32_t quantity = total - 2 * elapsed;
  if (reverse) quantity = -quantity;
  return util_make_hand_point(quantity, intervals, SUN_ARC_RADIUS, center);
}

void sun_arc_draw(GContext *ctx, GRect bounds, struct tm *t) {
  AppState *app_state = data_get_app_state();

  const int sunrise_min = parse_minutes_of_day(app_state->sunrise);
  const int sunset_min = parse_minutes_of_day(app_state->sunset);
  // Never had any data at all (first launch, nothing persisted yet, no
  // fetch has completed) -- nothing sensible to draw, so draw nothing
  // rather than guess. Distinct from the WEATHER_ERROR/DATA_EMPTY case
  // below, which still has sunrise/sunset and falls back to the clear-sky
  // icons instead.
  if (sunrise_min < 0 || sunset_min < 0) return;

  const int now_min = t->tm_hour * 60 + t->tm_min;
  const int half_w = bounds.size.w / 2;
  const int horizon_y = scl_y_pp({.g = HORIZON_Y_PCT});
  // The arc's own pivot is horizon_y, not the display's raw geometric
  // center (half_h) -- they're ~1px apart, which used to leave a tiny seam
  // right where the continuous sweep hands off to the fixed horizon
  // position. Pivoting on horizon_y directly makes theta=90deg in
  // sweep_point() land exactly on the same point the fixed-position cases
  // below use, with no discontinuity.
  const GPoint center = GPoint(half_w, horizon_y);

  // Stale weather condition falls back to the clear-sky icons (via
  // data_get_weather_icon()'s WEATHER_ERROR case) even though sunrise/sunset
  // themselves are still trusted -- see WEATHER_STALE_S above.
  const bool weather_stale = (time(NULL) - app_state->last_fetch) > WEATHER_STALE_S;
  const int weather_code = weather_stale ? WEATHER_ERROR : app_state->current_code;

  SunPhase phase;
  if (now_min >= sunrise_min - TRANSITION_MIN && now_min < sunrise_min) {
    phase = SUN_PHASE_HORIZON_EAST;
  } else if (now_min >= sunrise_min && now_min < sunrise_min + TRANSITION_MIN) {
    phase = SUN_PHASE_SUNRISE;
  } else if (now_min >= sunrise_min + TRANSITION_MIN && now_min < sunset_min - TRANSITION_MIN) {
    phase = SUN_PHASE_DAY_SWEEP;
  } else if (now_min >= sunset_min - TRANSITION_MIN && now_min < sunset_min) {
    phase = SUN_PHASE_SUNSET;
  } else if (now_min >= sunset_min && now_min < sunset_min + TRANSITION_MIN) {
    phase = SUN_PHASE_HORIZON_WEST;
  } else {
    phase = SUN_PHASE_NEUTRAL;
  }

  uint32_t resource_id;
  GPoint icon_center;
  // The icon's color always reflects the current temperature, including the
  // horizon/sunrise/sunset markers -- confirmed by your review; corrected
  // from an earlier version of this file that only tinted the day-sweep and
  // neutral phases. "Current temperature" per CONFIG_TEMP_COLOR_SOURCE
  // (default REALFEEL) -- see data_select_temp_color_source().
  const GColor tint = data_get_temp_color_abs(
    data_select_temp_color_source(app_state->current_temp, app_state->current_apparent_temp), t
  );

  switch (phase) {
    case SUN_PHASE_HORIZON_EAST:
      resource_id = RESOURCE_ID_WI_HORIZON;
      icon_center = GPoint(half_w + SUN_ARC_RADIUS, horizon_y - TRANSITION_ICON_Y_OFFSET);
      break;
    case SUN_PHASE_SUNRISE:
      resource_id = RESOURCE_ID_WI_SUNRISE;
      icon_center = GPoint(half_w + SUN_ARC_RADIUS, horizon_y - TRANSITION_ICON_Y_OFFSET);
      break;
    case SUN_PHASE_SUNSET:
      resource_id = RESOURCE_ID_WI_SUNSET;
      icon_center = GPoint(half_w - SUN_ARC_RADIUS, horizon_y - TRANSITION_ICON_Y_OFFSET);
      break;
    case SUN_PHASE_HORIZON_WEST:
      resource_id = RESOURCE_ID_WI_HORIZON;
      icon_center = GPoint(half_w - SUN_ARC_RADIUS, horizon_y - TRANSITION_ICON_Y_OFFSET);
      break;
    case SUN_PHASE_DAY_SWEEP: {
      const int elapsed = now_min - (sunrise_min + TRANSITION_MIN);
      const int total = (sunset_min - TRANSITION_MIN) - (sunrise_min + TRANSITION_MIN);
      if (total <= 0) return; // degenerate day length (shouldn't happen), bail
      icon_center = sweep_point(elapsed, total, center, false);
      resource_id = data_get_weather_icon(weather_code, true);
      break;
    }
    case SUN_PHASE_NEUTRAL:
    default: {
      // Sweeps back across the same upper arc, in reverse (West -> apex ->
      // East), timed to arrive back at the sunrise position exactly as
      // sunrise-5m begins -- corrected by your review from an earlier
      // version that just parked the icon statically at the sunset
      // position all night. Wraps past midnight: `elapsed` and `total` are
      // both measured from sunset+5m forward, adding a full day when `now`
      // has rolled over to the next calendar day.
      const int neutral_start = sunset_min + TRANSITION_MIN;
      const int neutral_end = sunrise_min - TRANSITION_MIN;
      const int elapsed = (now_min >= neutral_start)
        ? (now_min - neutral_start)
        : (now_min + MINUTES_PER_DAY - neutral_start);
      const int total = neutral_end + MINUTES_PER_DAY - neutral_start;
      if (total <= 0) return; // degenerate night length (shouldn't happen), bail
      icon_center = sweep_point(elapsed, total, center, true);
      resource_id = data_get_weather_icon(weather_code, false);
      break;
    }
  }

  util_draw_tinted_icon(ctx, resource_id, icon_center, tint, SUN_ARC_ICON_SIZE, 3);
}
