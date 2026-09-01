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

  // Color: soft yellow normally, blue on the (rare) day a blue moon lands. Eclipse
  // (red) deferred -- not implemented yet.
  const GColor tint = app_state->moon_blue ? GColorBlueMoon : GColorPastelYellow;

  // Full strength only when actually visible right now (see visible_now
  // above); faded otherwise -- ALWAYS/NIGHT reach this with visible_now
  // false (daytime, or below-horizon-at-night), VISIBLE never does (it
  // already returned above), so this is effectively a no-op dimming for
  // VISIBLE and the real fade behavior for the other two modes.
  const int max_alpha = visible_now ? 3 : 1;

  util_draw_tinted_icon(ctx, resource_id, icon_center, tint, MOON_ICON_SIZE, max_alpha);
}
