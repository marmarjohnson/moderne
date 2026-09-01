#include "moon.h"

#include "../config.h"
#include "data.h"
#include "sun_arc.h"
#include "util.h"

// Same size as every other icon on this face -- kept as its own constant rather than reaching
// into sun_arc.c's SUN_ARC_ICON_SIZE (private to that file, by design;
// weather_icons_gen.py's own SIZE and config.h's ICON_SIZE are likewise
// each their own independent 24/28 constant, not unified across files).
#define MOON_ICON_SIZE 24

// Predicted TOTAL lunar eclipse totality windows (U2-U3, UTC), hardcoded --
// see Development_Guidance.org's Lunar/astronomy specifics for why a short,
// periodically-refreshed table beats deriving eclipses from orbital
// mechanics on-watch: they're precisely predicted years in advance, so
// there's nothing to compute. Source: NASA GSFC's eclipse canon
// (eclipse.gsfc.nasa.gov/lunar.html, eclipse.gsfc.nasa.gov/LEdecade/
// LEdecade2031.html) and theskylive.com's contact-time pages for the two
// rows marked (exact); the other four only had "greatest eclipse" time +
// total-phase duration published there, so their start/end are greatest
// +/- duration/2 (validated to within ~1 minute against the exact rows --
// comfortably inside a single flat-tint icon's margin of visible error).
// Covers 2026-2033; extend whenever a later lunar-feature pass revisits
// this and the table starts running out.
typedef struct {
  time_t start; // U2, UTC
  time_t end;   // U3, UTC
} EclipseWindow;

static const EclipseWindow ECLIPSE_WINDOWS[] = {
  { 1772535866, 1772539365 }, // 2026-03-03 11:04:26-12:02:45 UTC (exact)
  { 1861892179, 1861896460 }, // 2028-12-31 16:16:19-17:27:40 UTC (exact)
  { 1877135478, 1877141581 }, // 2029-06-26 02:31:18-04:13:01 UTC (exact)
  { 1892499381, 1892502603 }, // 2029-12-20 22:16:21-23:10:03 UTC (exact)
  { 1966516911, 1966520871 }, // 2032-04-25 ~14:41-15:47 UTC (approx)
  { 1981737610, 1981740430 }, // 2032-10-18 ~18:40-19:27 UTC (approx)
  { 1997117361, 1997120301 }, // 2033-04-14 ~18:49-19:38 UTC (approx)
  { 2012379413, 2012384153 }, // 2033-10-08 ~10:16-11:35 UTC (approx)
};
#define ECLIPSE_WINDOW_COUNT (int)(sizeof(ECLIPSE_WINDOWS) / sizeof(ECLIPSE_WINDOWS[0]))

// Whether `utc` (a real Unix epoch instant, from mktime() -- never local
// time, eclipse windows above are UTC) falls inside any known totality
// window. Table is tiny and unsorted-safe to scan linearly every draw.
static bool moon_is_eclipse(time_t utc) {
  for (int i = 0; i < ECLIPSE_WINDOW_COUNT; i++) {
    if (utc >= ECLIPSE_WINDOWS[i].start && utc <= ECLIPSE_WINDOWS[i].end) return true;
  }
  return false;
}

// Region below the sun-arc and above the digital time. First-cut geometry, picked
// by measuring an actual baseline render rather than guessed cold: with
// TEST-pinned time/sunrise/sunset, the gap between the arc's apex (y=17 at
// dead center) and the top of the time digits (y=74) is clear across
// roughly x=70..190. MOON_REGION_CENTER_Y=46 sits in the middle of that
// gap with margin on both sides; MOON_REGION_RADIUS=10 is deliberately
// conservative (icon center can move +-10px, so worst-case rendered edges
// are y=24..68, MOON_ICON_SIZE/2=12 inside the measured clear band on
// both sides). Expect this to move once seen rendered for real and
// compared against the sun-arc's own actual position, the same way the
// sun-arc's radius went through R=100->110->112 rather than being right on the first guess.
#define MOON_REGION_CENTER_Y 46
#define MOON_REGION_RADIUS 10

// Icon resources in OUR phase order (0=new, 14=full) -- see
// weather_icons_gen.py's MOON_GLYPHS for how index i maps to a specific
// wi-moon-alt-* codepoint (a half-cycle offset, not the font's own order
// directly -- see that file for why).
static const uint32_t MOON_ICON_RESOURCES[28] = {
  RESOURCE_ID_WI_MOON_0,  RESOURCE_ID_WI_MOON_1,  RESOURCE_ID_WI_MOON_2,
  RESOURCE_ID_WI_MOON_3,  RESOURCE_ID_WI_MOON_4,  RESOURCE_ID_WI_MOON_5,
  RESOURCE_ID_WI_MOON_6,  RESOURCE_ID_WI_MOON_7,  RESOURCE_ID_WI_MOON_8,
  RESOURCE_ID_WI_MOON_9,  RESOURCE_ID_WI_MOON_10, RESOURCE_ID_WI_MOON_11,
  RESOURCE_ID_WI_MOON_12, RESOURCE_ID_WI_MOON_13, RESOURCE_ID_WI_MOON_14,
  RESOURCE_ID_WI_MOON_15, RESOURCE_ID_WI_MOON_16, RESOURCE_ID_WI_MOON_17,
  RESOURCE_ID_WI_MOON_18, RESOURCE_ID_WI_MOON_19, RESOURCE_ID_WI_MOON_20,
  RESOURCE_ID_WI_MOON_21, RESOURCE_ID_WI_MOON_22, RESOURCE_ID_WI_MOON_23,
  RESOURCE_ID_WI_MOON_24, RESOURCE_ID_WI_MOON_25, RESOURCE_ID_WI_MOON_26,
  RESOURCE_ID_WI_MOON_27,
};

void moon_draw(GContext *ctx, GRect bounds, struct tm *t) {
  AppState *app_state = data_get_app_state();
  PersistData *persist_data = data_get_persist_data();

  // CONFIG_MOON_DISPLAY (see clay.ts / config.h's MOON_DISPLAY_* -- ALWAYS
  // is the default and originally-shipped behavior, everything below).
  if (strcmp(persist_data->moon_display, MOON_DISPLAY_NEVER) == 0) return;

  // Hard hide, the one case that's never just faded regardless of display
  // mode: no moon data has ever arrived at all. DATA_EMPTY (-1000, see
  // data_init()) is well outside any real altitude's -90..90 range, so
  // this can't misfire against a real (even below-horizon) reading --
  // same "draw nothing rather than guess" rule sun_arc_draw() follows for
  // missing sunrise/sunset.
  if (app_state->moon_altitude == DATA_EMPTY) return;

  const bool is_daytime = sun_arc_is_daytime(t);
  const bool above_horizon = app_state->moon_altitude > 0;
  const bool visible_now = !is_daytime && above_horizon;

  // NIGHT hides only the daytime case (still faded at night when below
  // the horizon, same as ALWAYS). VISIBLE hides everywhere ALWAYS would
  // have faded instead -- no dimmed preview at all, only ever drawn at
  // full strength.
  if (strcmp(persist_data->moon_display, MOON_DISPLAY_NIGHT) == 0 && is_daytime) return;
  if (strcmp(persist_data->moon_display, MOON_DISPLAY_VISIBLE) == 0 && !visible_now) return;

  const int half_w = bounds.size.w / 2;
  const GPoint region_center = GPoint(half_w, MOON_REGION_CENTER_Y);

  // Polar sky-map: azimuth is the angle around the region, altitude is
  // the distance from its center -- overhead (high altitude) lands near
  // the middle, near-horizon (low altitude) lands near the region's edge.
  // Clamped to 0..90 at *both* ends now, not just the top: a below-horizon
  // reading used to be hidden entirely (see the visibility-gate history
  // above), so a negative altitude never reached this line before: now
  // it's drawn faded (see max_alpha below) rather than hidden, and needs
  // a valid radius too -- pinned to the region's outer edge rather than
  // extrapolating past it.
  int alt_clamped = app_state->moon_altitude;
  if (alt_clamped > 90) alt_clamped = 90;
  if (alt_clamped < 0) alt_clamped = 0;
  const int radius = MOON_REGION_RADIUS * (90 - alt_clamped) / 90;

  // util_make_hand_point()'s angle convention (0 = up, clockwise) already
  // matches standard compass bearing (0 = North = up, 90 = East = right)
  // with no conversion needed -- the same convention moon.ts's azimuthDeg
  // uses and the sun-arc's own East-right/West-left sense agrees with.
  const GPoint icon_center = util_make_hand_point(
    app_state->moon_azimuth, 360, radius, region_center
  );

  const int phase = ((app_state->moon_phase % 28) + 28) % 28; // defensive: keep in range
  const uint32_t resource_id = MOON_ICON_RESOURCES[phase];

  // Color: soft yellow normally, blue on the (rare) day a blue moon lands, deep
  // red during a real total lunar eclipse's totality window (see
  // ECLIPSE_WINDOWS above) -- checked first since it's the rarer and more
  // visually distinctive of the two, and both are full-moon-only
  // phenomena that could in principle coincide. mktime() converts the
  // real (sweep/TEST-respecting) local `t` this frame is drawing to a UTC
  // instant -- same technique the FAST/ULTRA sweep block above already
  // uses for moon_astro.c, so a TEST-pinned or swept date that happens to
  // land inside a table window exercises this too, not just real time
  // passing. tm_isdst = -1 (see that block's own comment) rather than
  // trusting whatever DST flag `t` already carried in from localtime().
  struct tm eclipse_check_t = *t;
  eclipse_check_t.tm_isdst = -1;
  const bool eclipse_now = moon_is_eclipse(mktime(&eclipse_check_t));
  const GColor tint = eclipse_now ? GColorDarkCandyAppleRed
    : app_state->moon_blue ? GColorBlueMoon : GColorPastelYellow;

  // Full strength only when actually visible right now (see visible_now
  // above); faded otherwise -- ALWAYS/NIGHT reach this with visible_now
  // false (daytime, or below-horizon-at-night), VISIBLE never does (it
  // already returned above), so this is effectively a no-op dimming for
  // VISIBLE and the real fade behavior for the other two modes.
  const int max_alpha = visible_now ? 3 : 1;

  util_draw_tinted_icon(ctx, resource_id, icon_center, tint, MOON_ICON_SIZE, max_alpha);
}
